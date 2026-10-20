# SSBU on PS4: plan

The base is [WindowSmash](https://github.com/SNO2207/WindowSmash) (SNO2207): a static AArch64 → C recompiler for
Super Smash Bros. Ultimate plus a native runtime (Maxwell GPU command streams and shaders, nn services, audio, VP9,
netplay). Its Linux build already runs the same runtime on POSIX through a Win32 shim (`runtime/linux`) and draws its
Direct3D 11 renderer through DXVK on Vulkan, compiling shaders with vkd3d-shader. This port does the same on the PS4,
with SMW's PS4 stack underneath:

| Layer | Windows (upstream) | Linux (upstream) | PS4 (this port) |
| :--- | :--- | :--- | :--- |
| Game code | clang-cl, AVX2/FMA | clang, AVX2/FMA | clang 21, `-march=btver2` (Jaguar: AVX, F16C, BMI1; no AVX2, no FMA) |
| OS layer | Win32 | `runtime/linux/win32.c` | that shim on the OpenOrbis libc + SMW's `port/` (heap, threads/cores, lazy direct memory, crash log) |
| Graphics | D3D11 | DXVK 3.1.1 native → Vulkan | DXVK built for PS4 → SMW's RADV (Crema's GNM build), WSI = SMW's `ps4_vk_wsi` (headless surface → VideoOut) |
| Shaders | d3dcompiler (DXBC) | vkd3d-shader (HLSL → DXBC) | vkd3d-shader built for PS4, plus a DXBC cache made on the PC |
| Audio | WASAPI | SDL2 | sceAudioOut |
| Pads | XInput / SDL3 | SDL2 | scePad (up to 4 DS4) |
| Package | `ssbu.exe` | `ssbu` | `eboot.bin` in `SSBU-vXX.YY.pkg`, title `SSBU00001`, data in `/data/SSBU` |

## Milestones

1. **Graphics proof (no game needed): done in v00.02.** DXVK on RADV on the console: a D3D11 test app
   (tests/dxvk-test) clears the screen and draws a spinning triangle at 59.9 fps with GPU-side VideoOut flips.
   Lessons: the system modules must be loaded before any call into them (port/ps4_modules.cpp; v00.00/v00.01 died
   silently in sceVideoOutOpen), and only binary semaphores are dropped from submits (DXVK's timelines work).
2. **Runtime on PS4:** the Win32 shim on the OpenOrbis libc (futex → address waits, write watch by mprotect on 16 KiB
   pages as on macOS, fibers, files under `/data/SSBU`), the runtime linked with stub modules.
3. **Game build** (needs the user's SSBU dump: game + update, `prod.keys`/`title.keys`, firmware): upstream's
   `recomp.py all --generate-only`, then the generated C compiled for PS4. Fighter NROs: static link first; if the
   eboot is too big, prx modules loaded on demand.
4. **First boot**, then memory (≈5 GB on the PS4 vs upstream's 16 GB recommendation), speed, audio, pads.

## Known risks

- **CPU:** Jaguar at 1.6 GHz; the game is locked to 60 fps logic. The recompiled code, the Maxwell command-stream
  translation and DXVK all run on it. NEON FMLA is compiled to `fmaf()`, which is software on Jaguar: needs a fast
  path (float FMA through double is exact for the product).
- **Memory:** upstream recommends 16 GB; the Linux build is a 1.2 GB executable before the game's own memory.
- **Shaders:** ~40,000 programs for 13.0.5; HLSL compiles on Jaguar are slow, so a PC-made cache is needed.
- **Size:** game data ~17 GB in `/data/SSBU` (RomFS read in place).
