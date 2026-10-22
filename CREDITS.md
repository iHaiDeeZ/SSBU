# Credits

[← README](README.md)

## The port

- **ShiroKlein**: the PS4 port (MIT, [LICENSE](LICENSE))

## Built on

| Project | By | Used for | Licence |
| :--- | :--- | :--- | :--- |
| [WindowSmash](https://github.com/SNO2207/WindowSmash) | SNO2207 | The recompiler and runtime: `patches/windowsmash-ps4.patch` applies to it | None specified ([its NOTICE](https://github.com/SNO2207/WindowSmash/blob/main/NOTICE.md)) |
| [DXVK](https://github.com/doitsujin/dxvk) | Philip Rebohle and contributors | Direct3D 11 on Vulkan: `patches/dxvk-ps4.patch` | zlib |
| [Mesa](https://mesa3d.org) (RADV) | The Mesa contributors | The Vulkan driver; `port/mesa/ac_ps4_drm.c` and `patches/mesa-ssbu.patch` | MIT |
| [vkd3d-shader](https://gitlab.winehq.org/wine/vkd3d) | The Wine project | HLSL to DXBC on the console | LGPL 2.1 |
| [FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | AMD | The upscaler, in the WindowSmash patch | MIT |
| [libvpx](https://chromium.googlesource.com/webm/libvpx), [libopus](https://opus-codec.org) | The WebM Project, Xiph.Org | The game's movies and music, through WindowSmash | BSD |
| [LLVM libc++](https://libcxx.llvm.org) | The LLVM project | The C++ library | Apache 2.0 with LLVM exceptions |
| [dlmalloc](https://gee.cs.oswego.edu/dl/html/malloc.html) | Doug Lea | `port/third_party/dlmalloc` | CC0 (public domain) |
| [libjpeg-turbo](https://libjpeg-turbo.org) | libjpeg-turbo contributors | `port/third_party/turbojpeg/turbojpeg.h` | BSD |
| [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) | OpenOrbis | The PS4 libc and system libraries | GPL 3.0 (the toolchain), its libraries as stated there |
| [PacBrew](https://github.com/PacBrew) | PacBrew | The PS4 toolchain packages | As stated there |
| [LibOrbisPkg](https://github.com/maxton/LibOrbisPkg) | maxton | PkgTool, the packages | LGPL 3.0 |
| [DejaVu fonts](https://dejavu-fonts.github.io) | DejaVu authors | The home-screen art (`scripts/make-art.py`) | Bitstream Vera licence |

## Shared with

- **[SMW](https://github.com/iHaiDeeZ/SMW)**: the PS4 layer in `port/` (threads and cores, memory, the Vulkan WSI on
  VideoOut, crash logs, the profiler) and the toolchain setup.
- **[Crema](https://github.com/iHaiDeeZ/Crema)**: RADV with the PS4's GNM layer.
- **[love-ps4](https://github.com/Mari0/love-ps4)**: the reference PS4 setup.
- **[Resonance](https://github.com/iHaiDeeZ/Resonance)**: where the VideoOut WSI began.
- **PSChrome**: the PS4 random source (`port/ps4_random.c`) and the malloc fixes.

## Thanks

- **GoldHEN** and the PS4 homebrew scene, for making any of this possible.

Super Smash Bros. Ultimate is © Nintendo. No part of the game is in this repository.
