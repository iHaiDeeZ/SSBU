#!/usr/bin/env bash
# The shader pack: every DXBC shader of the game's shader cache made on the PC through the PS4's DXVK on a PC copy of
# the console's RADV (tools/shaderpack/shaderpack.cpp), filling the two caches the game reads on the console:
#   <out>/mesa/mesa_shader_cache_sf/<uuid>/AMD RADV BONAIRE/foz_cache.foz (+_idx): RADV's GPU code
#   <out>/dxvk/0e01b720619d1962.dxvk.bin (+.lut): DXVK's IR (on the console: cdc70b6c9a46842e, the hash of /eboot.bin)
# Needs scripts/build-mesa-host.sh and scripts/build-dxvk-host.sh. Batches of SHADERPACK_BATCH shaders a process (each
# keeps what it makes until it ends), SHADERPACK_THREADS threads each.
#   scripts/build-shaderpack.sh [list of DXBC paths]      (default: every .dxbc in windowsmash dump/shadercache)
#   SHADERPACK_OUT=<dir> SHADERPACK_FIRST=<n> SHADERPACK_COUNT=<n> scripts/build-shaderpack.sh ...
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PSB="${PSB:-$HOME/ssbu-build}"
SP="$PSB/shaderpack"
OUT="${SHADERPACK_OUT:-$SP/out}"
MESA="$PSB/build/mesa-host"
DXVK_SRC="$PSB/src/dxvk"
DXVK="$PSB/build/dxvk-host"
TOOL="$SP/shaderpack"
mkdir -p "$SP" "$OUT/mesa" "$OUT/dxvk" "$OUT/logs"

# the tool, when its source is newer
if [ ! -x "$TOOL" ] || [ "$HERE/../tools/shaderpack/shaderpack.cpp" -nt "$TOOL" ] || [ "$DXVK/src/d3d11/libdxvk_d3d11.a" -nt "$TOOL" ]; then
    libs=()
    for a in src/d3d11/libdxvk_d3d11.a src/dxgi/libdxvk_dxgi.a src/dxvk/libdxvk.a src/wsi/libwsi.a src/vulkan/libvkcommon.a \
             src/spirv/libspirv.a src/util/libutil.a subprojects/dxbc-spirv/libdxbc_spv.a \
             subprojects/libdisplay-info/libdisplay-info.a; do libs+=("$DXVK/$a"); done
    clang++-21 -std=c++20 -O2 -march=btver2 -mno-sse4a -I"$DXVK_SRC/include/native" -I"$DXVK_SRC/include/native/windows" \
        -I"$DXVK_SRC/include/native/directx" -I"$DXVK_SRC/include/vulkan/include" \
        "$HERE/../tools/shaderpack/shaderpack.cpp" -o "$TOOL" -Wl,--start-group "${libs[@]}" -Wl,--end-group -ldl -lpthread
fi

LIST="${1:-$SP/all.txt}"
if [ -z "${1:-}" ]; then
    find "$PSB/src/windowsmash/dump/shadercache" -maxdepth 1 -name 'v*_*.dxbc' | sort > "$LIST"
fi
TOTAL="$(wc -l < "$LIST")"
FIRST="${SHADERPACK_FIRST:-0}"
END=$(( FIRST + ${SHADERPACK_COUNT:-$TOTAL} ))
[ "$END" -gt "$TOTAL" ] && END="$TOTAL"
BATCH="${SHADERPACK_BATCH:-2500}"

# The console's settings: Mesa's single-file cache, DXVK's options (its dxvk.conf, the port's DXVK_CONFIG:
# ps4/ssbu_port.cpp) - and drm-shim standing in for a gfx7 GPU, whose description ac_ps4_info.h replaces.
SHIM="$MESA/src/amd/drm-shim/libamdgpu_noop_drm_shim.so"
export AMDGPU_GPU_ID=bonaire
export MESA_SHADER_CACHE_DIR="$OUT/mesa" MESA_DISK_CACHE_SINGLE_FILE=1 MESA_SHADER_CACHE_MAX_SIZE=16G
export DXVK_SHADER_CACHE_PATH="$OUT/dxvk" DXVK_LOG_PATH="$OUT/logs" DXVK_LOG_LEVEL=info
export DXVK_CONFIG="dxvk.enableGraphicsPipelineLibrary = True" DXVK_CONFIG_FILE="$SP/console-dxvk.conf"
for (( at = FIRST; at < END; at += BATCH )); do
    n=$(( END - at < BATCH ? END - at : BATCH ))
    LD_PRELOAD="$SHIM" "$TOOL" "$MESA/src/amd/vulkan/libvulkan_radeon.so" "$LIST" "$at" "$n" "${SHADERPACK_THREADS:-20}" 2>&1 |
        grep -v '^info:' || true
done
find "$OUT/mesa" "$OUT/dxvk" -type f -printf '%P %s\n' | sort
