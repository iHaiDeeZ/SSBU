#!/usr/bin/env bash
# Commits the PS4 work in the WindowSmash checkout (branch ps4) and exports it as patches/windowsmash-ps4.patch
# (against upstream's commit the branch starts from), and DXVK's as patches/dxvk-ps4.patch.
#   save-patch.sh "<commit message>"
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MSG="${1:-PS4 port work}"
WS="${SSBU_WINDOWSMASH:-$HOME/ssbu-build/src/windowsmash}"
DX="$HOME/ssbu-build/src/dxvk"
git -C "$WS" add -A runtime tools
git -C "$WS" diff --cached --quiet || git -C "$WS" commit -qm "$MSG"
BASE="$(git -C "$WS" merge-base ps4 origin/main)"
git -C "$WS" diff "$BASE" ps4 > "$HERE/../patches/windowsmash-ps4.patch"
echo "windowsmash-ps4.patch: $(wc -l < "$HERE/../patches/windowsmash-ps4.patch") lines against $(git -C "$WS" rev-parse --short "$BASE")"
if [ -d "$DX/.git" ]; then
    git -C "$DX" add -A
    git -C "$DX" diff --cached --quiet || git -C "$DX" commit -qm "$MSG"
    git -C "$DX" diff v3.1.1 ps4 > "$HERE/../patches/dxvk-ps4.patch"
    echo "dxvk-ps4.patch: $(wc -l < "$HERE/../patches/dxvk-ps4.patch") lines"
fi
