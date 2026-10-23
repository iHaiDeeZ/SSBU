#!/usr/bin/env bash
# The SSBU eboot and package: WindowSmash's runtime and the recompiled game compiled for the PS4
# (tools/ps4/build_ps4.py in the upstream checkout, branch ps4 = patches/windowsmash-ps4.patch), linked with DXVK,
# RADV and the PS4 layer (ps4/CMakeLists.txt), packaged as SSBU00001.
#   SSBU_VERSION=00.03 scripts/build-ssbu.sh        -> out/SSBU-v00.03.pkg
#   SSBU_JOBS=12 (compile jobs), SSBU_LINK_ONLY=1 (skip build_ps4.py)
# The game's data stays on the console in /data/SSBU (scripts/stage-data.sh, scripts/upload-data.py).
# The eboot carries code recompiled from YOUR dump of the game: the package is for your console only, never share it.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/ps4-env.sh"
VERSION="${SSBU_VERSION:?SSBU_VERSION=XX.YY}"
WS="${SSBU_WINDOWSMASH:-$PS4_BUILD_ROOT/src/windowsmash}"
OBJ="$PS4_BUILD_ROOT/build/ssbu"
BUILD="$PS4_BUILD_ROOT/build/eboot"
[ -f "$PS4_BUILD_ROOT/build/dxvk/src/d3d11/libdxvk_d3d11.a" ] || { echo "build DXVK first: scripts/build-dxvk.sh" >&2; exit 1; }

if [ "${SSBU_LINK_ONLY:-0}" != 1 ]; then
    # vkd3d-shader (D3DCompile: the runtime's HLSL to DXBC), once
    [ -f "$PS4_BUILD_ROOT/build/vkd3d/libvkd3d-shader.a" ] || ( cd "$WS" && python3 tools/ps4/build_vkd3d_ps4.py \
        "$PS4_BUILD_ROOT/src/vkd3d-2.1" "$PS4_BUILD_ROOT/src/SPIRV-Headers-vulkan-sdk-1.4.363.0/include" "$PS4_BUILD_ROOT/build/vkd3d" )
    # the codecs (libopus, libvpx's VP9 decoder), when stale
    ( cd "$WS" && python3 tools/ps4/build_codecs_ps4.py --out "$PS4_BUILD_ROOT/build" )
    ( cd "$WS" && DXVK_SRC="$PS4_BUILD_ROOT/src/dxvk" VKD3D_SRC="$PS4_BUILD_ROOT/src/vkd3d-2.1" \
        python3 tools/ps4/build_ps4.py --out "$OBJ" --jobs "${SSBU_JOBS:-12}" )
fi
[ -f "$OBJ/objects.rsp" ] || { echo "no $OBJ/objects.rsp" >&2; exit 1; }

ps4_cmake -S "$HERE/../ps4" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/../toolchain/ps4-ssbu.cmake" \
    "-DCMAKE_PROJECT_ssbu_INCLUDE=$HERE/../toolchain/ps4-love-style.cmake" \
    -DPS4_PAID="${PS4_PAID:-0x3800000000000035}" \
    -DPS4_MESA_BUILD="$PS4_BUILD_ROOT/build/mesa-cache" -DSSBU_OBJECTS="$OBJ" >/dev/null
rm -f "$BUILD/eboot_eboot/eboot.bin" "$BUILD/eboot"   # (a failed link must not be packaged)
touch "$OBJ/objects.rsp"   # (relink: the objects may have changed under the same list)
ninja -C "$BUILD"
EBOOT="$BUILD/eboot_eboot/eboot.bin"
[ -f "$EBOOT" ] || { echo "no eboot.bin" >&2; exit 1; }
llvm-size-21 "$BUILD/eboot" || true

STAGE="$PS4_BUILD_ROOT/build/ssbu-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys"
cp "$EBOOT" "$STAGE/eboot.bin"
python3 "$HERE/make-art.py" "$STAGE/sce_sys"
echo "$VERSION" > "$STAGE/version.txt"
# SSBU_WITH_DATA=1: the game's data in the package too (~23 GB): dump/ as scripts/stage-data.sh stages it, its
# romfs.cfg naming /app0/data, the NROs as built now, and the shader packs (DXBC, DXVK, Mesa). The runtime reads
# dump/ from /app0/data when it is there (runtime/ps4/start.c) and copies the DXVK and Mesa packs out once
# (ps4/ssbu_port.cpp). Hard links: nothing is copied here.
if [ "${SSBU_WITH_DATA:-0}" = 1 ]; then
    DATA="$PS4_BUILD_ROOT/data-stage/SSBU/dump"
    PACKS="$PS4_BUILD_ROOT/shaderpack/stage"
    mkdir -p "$STAGE/data/dump/modules" "$STAGE/data/dump/nro" "$STAGE/data/dump/fonts"
    for f in "$DATA/modules/"*; do [ "$(basename "$f")" = romfs.cfg ] || ln "$f" "$STAGE/data/dump/modules/"; done
    sed 's#/data/SSBU/#/app0/data/#g' "$DATA/modules/romfs.cfg" > "$STAGE/data/dump/modules/romfs.cfg"
    ln "$OBJ/nro-so/"*.so "$STAGE/data/dump/nro/"
    ln "$DATA/fonts/"* "$STAGE/data/dump/fonts/"
    ln "$PACKS/dump/shadercache.pak" "$STAGE/data/dump/"
    cp -al "$PACKS/dxvk" "$PACKS/mesa" "$STAGE/data/"
    echo "with the game's data: $(du -sh --apparent-size "$STAGE/data" | cut -f1)"
fi
find "$STAGE" -type d -empty -delete
PKG="$(SFO_STYLE=plain NO_RIGHT_SPRX=1 DOLPHIN_PS4_MODULES_DIR=/nonexistent "$HERE/make-pkg.sh" "$STAGE" SSBU00001 "Smash Ultimate (SSBU)" "$VERSION" SSBURECOMPPS4 "$PS4_BUILD_ROOT/out" | tail -1)"
mkdir -p "$HERE/../out" "$PS4_BUILD_ROOT/oelf"
NAME="SSBU-v$VERSION$([ "${SSBU_WITH_DATA:-0}" = 1 ] && echo -full || true).pkg"
cp "$PKG" "$HERE/../out/$NAME"
cp "$BUILD/eboot_eboot/eboot.oelf" "$PS4_BUILD_ROOT/oelf/v$VERSION.oelf"
cp "$BUILD/eboot" "$PS4_BUILD_ROOT/oelf/v$VERSION.elf"
ls -la "$HERE/../out/$NAME"
echo "now, from Windows (Git Bash): scripts/upload-to-console.sh out/$NAME"
