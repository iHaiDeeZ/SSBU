#!/usr/bin/env bash
# RADV for the PC with the PS4's identity: the port's own Mesa (~/ssbu-build/src/mesa, patches/mesa-ssbu.patch),
# built natively with drm-shim standing in for the GPU and AC_PS4_HOST_IDENTITY, so that it sees the console's
# device (src/amd/common/ac_ps4_info.h), compiles shaders as the console's RADV does and files them in its shader
# cache under the same keys - for scripts/build-shaderpack.sh. Same build id, compiler (clang 21) and CPU target
# (btver2: no FMA, so constants fold the same - less SSE4a, which Intel CPUs lack and which does no arithmetic) as
# scripts/build-mesa.sh; the libraries in ~/ssbu-build/build/mesa-host (libvulkan_radeon.so,
# libamdgpu_noop_drm_shim.so). (A changed setup: rm -rf ~/ssbu-build/build/mesa-host first.)
#   scripts/build-mesa-host.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PSB="${PSB:-$HOME/ssbu-build}"
SRC="$PSB/src/mesa"
B="$PSB/build/mesa-host"
BUILD_ID="$(sed -n 's/^BUILD_ID=\([0-9a-f]*\).*/\1/p' "$HERE/build-mesa.sh")"   # (the console's: one cache identity)
LIBDRM="${HOST_LIBDRM:-$HOME/cemups4-build/host-radv/prefix}"   # (libdrm 2.4.125 for the PC, as the console's Mesa)
export PATH="$PSB/host-tools/bin:$HOME/cemups4-build/mesa-venv/bin:$PATH"
export PKG_CONFIG_PATH="$LIBDRM/lib/x86_64-linux-gnu/pkgconfig"
export CC=clang-21 CXX=clang++-21
[ -d "$SRC/.git" ] || { echo "no $SRC: run scripts/build-mesa.sh first" >&2; exit 1; }
[ -n "$BUILD_ID" ] || { echo "no BUILD_ID in build-mesa.sh" >&2; exit 1; }

if [ ! -f "$B/build.ninja" ]; then
    flags="-march=btver2 -mno-sse4a -DAC_PS4_ALIGNED_VERTEX_FETCH -DAC_PS4_HOST_IDENTITY"
    meson setup "$B" "$SRC" -Dbuildtype=release -Db_ndebug=true -Dvulkan-drivers=amd -Dgallium-drivers= \
        -Dplatforms= -Dllvm=disabled -Damd-use-llvm=false -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dzstd=disabled -Dexpat=disabled -Dxmlconfig=disabled \
        -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false -Dshader-cache=enabled \
        -Dvulkan-layers= -Dtools=drm-shim -Dvideo-codecs= -Dspirv-tools=disabled -Dperfetto=false -Dselinux=false \
        -Dradv-build-id="$BUILD_ID" -Dc_args="$flags" -Dcpp_args="$flags" > "$PSB/logs/mesa-host-setup.log"
fi
ninja -C "$B" src/amd/vulkan/libvulkan_radeon.so src/amd/drm-shim/libamdgpu_noop_drm_shim.so
ls -la "$B/src/amd/vulkan/libvulkan_radeon.so" "$B/src/amd/drm-shim/libamdgpu_noop_drm_shim.so"
