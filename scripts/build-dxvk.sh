#!/usr/bin/env bash
# DXVK 3.1.1 (Direct3D 11 on Vulkan) for the PS4: static libraries linked into the eboot, drawing on SMW's RADV.
# Source: ~/ssbu-build/src/dxvk, branch ps4 (patches/dxvk-ps4.patch: option native_ps4, the VideoOut WSI in
# src/wsi/ps4, the app's vkGetInstanceProcAddr instead of dlopen("libvulkan.so")).
#   scripts/build-dxvk.sh            configure (once) and build into ~/ssbu-build/build/dxvk
#   DXVK_RECONFIGURE=1 scripts/...   configure again
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../toolchain/env.sh"
SRC="$PSB/src/dxvk"
OUT="$PSB/build/dxvk"
CROSS="$PSB/build/dxvk-ps4-cross.ini"
VENV="$PSB/venv"

# meson (a venv of its own) and a host glslang (DXVK's built-in shaders are GLSL)
[ -x "$VENV/bin/meson" ] || { python3 -m venv "$VENV" && "$VENV/bin/pip" install -q meson; }
mkdir -p "$PSB/host-tools/bin"
[ -x "$PSB/host-tools/bin/glslang" ] || cp "${HOST_GLSLANG:-$HOME/dolphinps4-build/host-tools/bin/glslang}" "$PSB/host-tools/bin/glslang"
export PATH="$VENV/bin:$PSB/host-tools/bin:$PATH"

[ -d "$SRC" ] || { echo "no DXVK source in $SRC (git clone --recursive -b v3.1.1 https://github.com/doitsujin/dxvk.git, then apply patches/dxvk-ps4.patch on a branch ps4)" >&2; exit 1; }
if [ -f "$HERE/../patches/dxvk-ps4.patch" ] && [ "$(git -C "$SRC" rev-parse --abbrev-ref HEAD)" != ps4 ]; then
    git -C "$SRC" checkout -q -b ps4
    git -C "$SRC" apply "$HERE/../patches/dxvk-ps4.patch"
fi

flags=(-target "$PS4_TARGET" -D_GNU_SOURCE -D__PS4__ -D__OPENORBIS__ -D__ORBIS__ -DPS4 -D__BSD_VISIBLE -D_BSD_SOURCE
       -fPIC -funwind-tables -march=btver2 -isysroot "$OPENORBIS" -isystem "$PS4_SYSROOT/libc-overlay"
       -isystem "$OPENORBIS/include" -isystem "$PS4_SYSROOT/include")
link=(-fuse-ld=lld -nostdlib -Wl,-m,elf_x86_64 -Wl,-pie -Wl,--eh-frame-hdr
      -Wl,--script,"$OPENORBIS/link.x" -L"$OPENORBIS/lib" "$OPENORBIS/lib/crt1.o" -lkernel -lc
      -lclang_rt.builtins-x86_64 -lSceLibcInternal)
q() { local out="" a; for a in "$@"; do out+="${out:+, }'$a'"; done; printf '[%s]' "$out"; }
cat > "$CROSS" <<EOF
[binaries]
c = '$PS4_CC'
cpp = '$PS4_CXX'
ar = '$PS4_AR'
strip = 'llvm-strip-$CLANG_VER'

[properties]
needs_exe_wrapper = true

[built-in options]
c_args = $(q "${flags[@]}")
cpp_args = $(q -nostdinc++ -isystem "$PS4_SYSROOT/include/c++/v1" "${flags[@]}")
c_link_args = $(q "${link[@]}")
cpp_link_args = $(q "${link[@]}" "$PS4_SYSROOT/lib/libc++.a" "$PS4_SYSROOT/lib/libc++abi.a" "$OPENORBIS/lib/libunwind.a")

[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

if [ ! -f "$OUT/build.ninja" ] || [ "${DXVK_RECONFIGURE:-0}" = 1 ]; then
    rm -rf "$OUT"
    meson setup "$OUT" "$SRC" --cross-file "$CROSS" --buildtype release -Db_ndebug=true \
        -Ddefault_library=static -Dnative_ps4=true -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false
fi
ninja -C "$OUT" -j"${JOBS}"
find "$OUT" -name '*.a' -printf '%P %s\n' | sort
