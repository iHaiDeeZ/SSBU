#!/usr/bin/env bash
# RADV for the PS4 with Mesa's shader cache on: Crema's PS4 Mesa (the GNM layer, src/amd/common/ac_ps4_drm.c) at
# its commit, plus patches/mesa-ssbu.patch, cloned to ~/ssbu-build/src/mesa and built as the static libraries the
# eboot links (toolchain/ps4-love-style.cmake: ps4_link_mesa) in ~/ssbu-build/build/mesa-cache.
# The cache's identity is the build id below (-Dradv-build-id), the GPU and what patches/mesa-ssbu.patch adds to it
# (the 32-bit address window, the vertex fetch rule): a build whose shader code differs needs a new id.
#   scripts/build-mesa.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PSB="${PSB:-$HOME/ssbu-build}"
CREMA="${CREMA_MESA:-$HOME/cemups4-build/src/mesa}"
BASE=7d010de97ed0e5d3b02e0d7c6bf0c829298490c9
BUILD_ID=713601e6e34378c5a27e6bcd0ab3164ee4583cd8   # (the first SSBU commit: names the cache, whatever comes after)
SRC="$PSB/src/mesa"
B="$PSB/build/mesa-cache"
# meson and mako from Crema's venv, glslangValidator (Mesa's built-in shaders)
export PATH="$PSB/host-tools/bin:$HOME/cemups4-build/mesa-venv/bin:$PATH"
[ -x "$PSB/host-tools/bin/glslangValidator" ] || cp "$HOME/cemups4-build/host-tools/bin/glslangValidator" "$PSB/host-tools/bin/"

if [ ! -d "$SRC/.git" ]; then
    git clone -q "$CREMA" "$SRC"
    git -C "$SRC" checkout -q -b ssbu "$BASE"
    git -C "$SRC" -c user.name=ssbu -c user.email=ssbu@localhost am -q "$HERE/../patches/mesa-ssbu.patch"
fi
# Crema's cross file with the system's lld (PacBrew's needs a libxml2 this Ubuntu no longer has; only meson's
# checks link anything: Mesa is built as static libraries)
sed 's#-fuse-ld=/opt/pacbrew/ps4/openorbis/bin/ld.lld#-fuse-ld=/usr/bin/ld.lld-21#g' \
    "$HOME/cemups4-build/mesa-ps4-cross.ini" > "$PSB/mesa-ps4-cross.ini"
if [ ! -f "$B/build.ninja" ]; then
    meson setup "$B" "$SRC" -Dbuildtype=release -Db_ndebug=true -Ddefault_library=static -Dvulkan-drivers=amd \
        -Dgallium-drivers= -Dplatforms= -Dllvm=disabled -Damd-use-llvm=false -Dopengl=false -Dgles1=disabled \
        -Dgles2=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dzstd=disabled -Dexpat=disabled \
        -Dxmlconfig=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
        -Dshader-cache=enabled -Dvulkan-layers= -Dtools= -Dvideo-codecs= -Dspirv-tools=disabled -Dperfetto=false \
        -Dselinux=false -Dradv-build-id=$BUILD_ID --cross-file="$PSB/mesa-ps4-cross.ini" > "$PSB/logs/mesa-cache-setup.log"
fi
# (only the libraries: Mesa's tools, ac_ib_parser among them, do not link for the PS4)
ninja -C "$B" src/amd/vulkan/libvulkan_radeon.a src/amd/common/libamd_common.a src/amd/compiler/libaco.a \
    src/amd/addrlib/libaddrlib.a src/vulkan/util/libvulkan_util.a src/compiler/spirv/libvtn.a src/compiler/nir/libnir.a \
    src/compiler/libcompiler.a src/util/libxmlconfig.a src/util/libmesa_util.a src/util/libmesa_util_simd.a \
    src/util/libmesa_util_clflush.a src/util/libmesa_util_clflushopt.a src/util/libparson.a \
    src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a
# the patch, kept with the port
git -C "$SRC" format-patch -q --stdout "$BASE" > "$HERE/../patches/mesa-ssbu.patch"
echo "mesa-ssbu.patch: $(grep -c '^From ' "$HERE/../patches/mesa-ssbu.patch") commits on Crema's $(git -C "$SRC" rev-parse --short "$BASE")"
