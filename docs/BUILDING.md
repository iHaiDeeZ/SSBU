# Building SSBU

[← README](../README.md)

Everything is built in WSL 2 (Ubuntu) on a Windows PC; uploads to the console run from Windows (Git Bash or
PowerShell), because uploads sent from inside WSL were undone on the console. Your dump never leaves your PC and
console: the package, the game data and the shader packs are all made from it, so never share them.

## What you need

- **Windows 10 or 11** with **WSL 2** and Ubuntu, 16 GB of RAM and ~150 GB free in WSL.
- **clang 21** and lld 21 (`apt install clang-21 lld-21 llvm-21`), cmake, ninja, meson, Python 3 with Pillow.
- **OpenOrbis** from [PacBrew](https://github.com/PacBrew) in `/opt/pacbrew/ps4/openorbis` (or `OPENORBIS=`).
- The PS4 build trees of [SMW](https://github.com/iHaiDeeZ/SMW) (`~/smw-build`: the libc++ 21 sysroot, the fixed
  PkgTool) and [Crema](https://github.com/iHaiDeeZ/Crema) (`~/cemups4-build/src/mesa`: RADV with the PS4's GNM
  layer). This port reuses them rather than building them again.
- Your **Super Smash Bros. Ultimate** dump: base game, update 13.0.2, DLC, `prod.keys` and `title.keys`.

## Steps

All paths are in WSL; this repository is `~/ssbu`, the build tree `~/ssbu-build`.

1. **The build tree.** `scripts/seed-build-tree.sh` creates `~/ssbu-build` from copies of SMW's tree.
2. **WindowSmash.** Clone [WindowSmash](https://github.com/SNO2207/WindowSmash) to `~/ssbu-build/src/windowsmash`,
   check out a branch `ps4` at commit `9c1b250` (the one the patch is made against) and
   `git apply ~/ssbu/patches/windowsmash-ps4.patch`. `scripts/save-patch.sh` writes the patch back after a change.
3. **The libraries.**
   - `scripts/build-mesa.sh`: RADV for the PS4 with its shader cache on (Crema's Mesa plus
     `patches/mesa-ssbu.patch`).
   - `scripts/build-dxvk.sh`: DXVK for the PS4. Clone [DXVK](https://github.com/doitsujin/dxvk) to
     `~/ssbu-build/src/dxvk`, branch `ps4` at tag `v3.1.1`, and `git apply ~/ssbu/patches/dxvk-ps4.patch` first.
   - `scripts/build-pkgtool.sh`: PkgTool with room for the large package's chunk hashes.
4. **The game.** Put your keys in `~/ssbu-build/keys`, then
   `~/ssbu-build/venv/bin/python scripts/prepare-game.py --game <your dump folder>`: your dump is decrypted, the
   update applied and the fighter modules laid out. Then, in the WindowSmash checkout,
   `python tools/recomp.py all dump/exefs --out build --generate-only` writes the game's C (about 6 GB).
5. **The shaders.** WindowSmash's `tools/shader_aot.py` fills its `dump/shadercache` with the game's DXBC shaders.
   Then:
   - `scripts/pack-shadercache.py`: them in one `shadercache.pak` (a folder of 45,000 files is slow on the PS4);
   - `scripts/build-mesa-host.sh`, `scripts/build-dxvk-host.sh`, `scripts/build-shaderpack.sh`: every one compiled
     on the PC, by the PS4's own DXVK and RADV, into the caches the console reads.
6. **The data.** `scripts/stage-data.sh` lays out the game data as the console reads it (hard links, no copies).
7. **The package.**
   - `SSBU_VERSION=00.24 scripts/build-ssbu.sh` → `out/SSBU-v00.24.pkg`, the program alone (~250 MB).
   - `SSBU_WITH_DATA=1 SSBU_VERSION=00.24 scripts/build-ssbu.sh` → `out/SSBU-v00.24-full.pkg`, with the game data
     and shader packs inside (~24 GB, half an hour to pack).
8. **The console.** From Windows:
   - `py scripts/upload-data.py` sends the data to `/data/SSBU` (only for the program-only package);
   - `scripts/upload-to-console.sh out/SSBU-v00.24.pkg` puts the package in `/data/pkg` and removes older SSBU
     packages there.

   `PS4_HOST` and `PS4_FTP_PORT` set the console's address (GoldHEN's FTP server is on port 2121).

## After playing

- `scripts/fetch-new-shaders.sh` fetches the shaders the console had to compile itself, for the next shader pack.
- `scripts/crashlog.sh` fetches the logs and reports; `scripts/profile.py` turns a saved profile into a readable one.

## How it fits together

[docs/PLAN.md](PLAN.md) explains the layers (WindowSmash's runtime on its Win32 shim, on the OpenOrbis libc;
Direct3D 11 through DXVK onto RADV) and the milestones of the port.
