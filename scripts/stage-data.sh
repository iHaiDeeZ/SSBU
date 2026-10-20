#!/usr/bin/env bash
# Lays out the game data the PS4 runtime reads from /data/SSBU (runtime/ps4/start.c: the tree is the working folder),
# from WindowSmash's dump/ (scripts/prepare-game.py made it from your own dump):
#   dump/modules/   main/sdk/subsdk0 and the 98 fighter modules (*.reloc.bin), aoc.cfg (your DLC), the game data:
#                   base-<title>.nca (decrypted base, ~14.5 GB) + romfs_patch.bin and romfs.layers (the update),
#                   romfs.cfg naming them where they are on the console
#   dump/fonts/     the six system fonts
#   stage-data.sh [out dir]   (default ~/ssbu-build/data-stage/SSBU; hard links, no copies)
# Then copy the folder's contents to /data/SSBU on the console over FTP (scripts/upload-data.py, from Windows: it
# resumes, skipping files already there at the same size). The data is your game: never upload it anywhere else.
set -euo pipefail
WS="${SSBU_WINDOWSMASH:-$HOME/ssbu-build/src/windowsmash}"
OUT="${1:-$HOME/ssbu-build/data-stage/SSBU}"
M="$WS/dump/modules"
[ -f "$M/romfs.cfg" ] || { echo "no $M/romfs.cfg: run scripts/prepare-game.py first" >&2; exit 1; }
mkdir -p "$OUT/dump/modules" "$OUT/dump/fonts"
link() { ln -f "$1" "$2" 2>/dev/null || cp -f "$1" "$2"; }
for f in "$M"/*.reloc.bin "$M/aoc.cfg" "$M/romfs.layers" "$M"/base-*.nca "$M/romfs_patch.bin"; do
    [ -f "$f" ] && link "$f" "$OUT/dump/modules/$(basename "$f")"
done
for f in "$WS"/dump/fonts/*.bfttf; do link "$f" "$OUT/dump/fonts/$(basename "$f")"; done
# romfs.cfg: the same lines, with the console's paths for the files it names
python3 - "$M/romfs.cfg" "$OUT/dump/modules/romfs.cfg" <<'EOF'
import os, sys
src, dst = sys.argv[1], sys.argv[2]
out = []
for line in open(src).read().splitlines():
    if line.startswith("/"):
        line = "/data/SSBU/dump/modules/" + os.path.basename(line)
    out.append(line)
open(dst, "w", newline="\n").write("\n".join(out) + "\n")
EOF
echo "romfs.cfg:"; sed 's/^/  /' "$OUT/dump/modules/romfs.cfg"
find "$OUT" -type f | wc -l | sed 's/^/files: /'
du -shL "$OUT" | sed 's/^/size: /'
