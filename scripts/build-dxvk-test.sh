#!/usr/bin/env bash
# SSBU v00.00: build the DXVK test app (tests/dxvk-test) and package it as SSBU00001.
#   scripts/build-dxvk-test.sh            -> out/SSBU-v00.00.pkg
# Needs scripts/build-dxvk.sh first. Run through ~/ssbu (no space in the path).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/ps4-env.sh"
VERSION="${SSBU_VERSION:-00.00}"
BUILD="$PS4_BUILD_ROOT/build/dxvk-test"
[ -f "$PS4_BUILD_ROOT/build/dxvk/src/d3d11/libdxvk_d3d11.a" ] || { echo "build DXVK first: scripts/build-dxvk.sh" >&2; exit 1; }

ps4_cmake -S "$HERE/../tests/dxvk-test" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/../toolchain/ps4-ssbu.cmake" \
    "-DCMAKE_PROJECT_ssbu_dxvk_test_INCLUDE=$HERE/../toolchain/ps4-love-style.cmake" \
    -DPS4_PAID="${PS4_PAID:-0x3800000000000035}" \
    -DPS4_MESA_BUILD="$PS4_BUILD_ROOT/build/mesa" >/dev/null
rm -f "$BUILD/eboot_eboot/eboot.bin"   # (a failed link must not be packaged)
ninja -C "$BUILD"
EBOOT="$BUILD/eboot_eboot/eboot.bin"
[ -f "$EBOOT" ] || { echo "no eboot.bin" >&2; exit 1; }

STAGE="$PS4_BUILD_ROOT/build/ssbu-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys"
cp "$EBOOT" "$STAGE/eboot.bin"
python3 "$HERE/make-art.py" "$STAGE/sce_sys"
echo "$VERSION" > "$STAGE/version.txt"
find "$STAGE" -type d -empty -delete
# Vulkan (RADV on GNM) runs under the plain homebrew identity, as SMW
PKG="$(SFO_STYLE=plain NO_RIGHT_SPRX=1 DOLPHIN_PS4_MODULES_DIR=/nonexistent "$HERE/make-pkg.sh" "$STAGE" SSBU00001 "Smash Ultimate (SSBU)" "$VERSION" SSBURECOMPPS4 "$PS4_BUILD_ROOT/out" | tail -1)"
mkdir -p "$HERE/../out" "$PS4_BUILD_ROOT/oelf"
NAME="SSBU-v$VERSION.pkg"
cp "$PKG" "$HERE/../out/$NAME"
cp "$BUILD/eboot_eboot/eboot.oelf" "$PS4_BUILD_ROOT/oelf/v$VERSION.oelf"
cp "$BUILD/eboot" "$PS4_BUILD_ROOT/oelf/v$VERSION.elf"
ls -la "$HERE/../out/$NAME"
echo "now, from Windows (Git Bash): scripts/upload-to-console.sh out/$NAME"
