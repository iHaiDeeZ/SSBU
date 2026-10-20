#!/usr/bin/env python3
"""Prepare the game the way upstream's tools/build_game.py does (steps 2-8: program, fonts, data with the update
applied, DLC list, fighter modules), in the WindowSmash checkout ~/ssbu-build/src/windowsmash, without its desktop
tool check (step 1 wants the Windows/Linux/macOS toolchains; the PS4 build has its own).

  ~/ssbu-build/venv/bin/python scripts/prepare-game.py --game /mnt/e/Switch/SSBU [--keys ~/ssbu-build/keys]
      [--fonts ~/ssbu-build/fonts]

--fonts: a folder with the six .bfttf fonts (scripts/extract-fonts from the game card's own system update) copied
into dump/fonts first, so no firmware folder is needed. Then: tools/recomp.py all dump/exefs --out build
--generate-only makes the C the PS4 build compiles."""
import argparse
import os
import shutil
import sys

WS = os.path.expanduser(os.environ.get("SSBU_WINDOWSMASH", "~/ssbu-build/src/windowsmash"))
sys.path.insert(0, os.path.join(WS, "tools"))
os.chdir(WS)
import build_game  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", required=True)
    ap.add_argument("--keys", default=os.path.expanduser("~/ssbu-build/keys"))
    ap.add_argument("--fonts", default=os.path.expanduser("~/ssbu-build/fonts"))
    a = ap.parse_args()
    if build_game.missing_fonts(build_game.FONTS) and not build_game.missing_fonts(a.fonts):
        os.makedirs(build_game.FONTS, exist_ok=True)
        for n in build_game.FONT_FILES:
            shutil.copyfile(os.path.join(a.fonts, n), os.path.join(build_game.FONTS, n))
        print(f"fonts: the six from {a.fonts}")
    # step 1 only checks for the desktop toolchains: Python itself is all these steps need
    build_game.check_tools = lambda: print(f"  Python {sys.version.split()[0]} (desktop toolchain check skipped)")
    args = argparse.Namespace(game=a.game, keys=a.keys, firmware=None, jobs=None, check=False, prepare_only=True,
                              check_ready=False)
    try:
        build_game.build(args)
    except build_game.Failed as e:
        print(f"\nERROR: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
