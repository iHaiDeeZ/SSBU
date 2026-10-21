#!/usr/bin/env bash
# DXVK for the PC as the PS4 build is: the same source and options (native_ps4: its code paths - shader libraries
# compiled where the shader is made, the app's vkGetInstanceProcAddr - and static libraries), the PC's compiler
# with the PS4's CPU target (clang 21, btver2 less SSE4a, which Intel CPUs lack). The shader pack tool
# (tools/shaderpack, scripts/build-shaderpack.sh) links it, so that it turns DXBC into the same SPIR-V and pipeline
# libraries as the game does on the console.
#   scripts/build-dxvk-host.sh       -> ~/ssbu-build/build/dxvk-host (a changed setup: remove it first)
set -euo pipefail
PSB="${PSB:-$HOME/ssbu-build}"
SRC="$PSB/src/dxvk"
OUT="$PSB/build/dxvk-host"
export PATH="$PSB/venv/bin:$PSB/host-tools/bin:$PATH"   # (meson; glslang for DXVK's built-in shaders)
export CC=clang-21 CXX=clang++-21
[ -d "$SRC/.git" ] || { echo "no DXVK source in $SRC: scripts/build-dxvk.sh first" >&2; exit 1; }

if [ ! -f "$OUT/build.ninja" ]; then
    meson setup "$OUT" "$SRC" --buildtype release -Db_ndebug=true -Ddefault_library=static -Dnative_ps4=true \
        -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false -Dc_args="-march=btver2 -mno-sse4a" \
        -Dcpp_args="-march=btver2 -mno-sse4a"
fi
ninja -C "$OUT"
find "$OUT" -name '*.a' -printf '%P %s\n' | sort
