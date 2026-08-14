#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# Generates src/playdate/Source/thumbnails/thumb_NN.png (NN = zero-padded
# registration index, matching menuGameList.c's own addGame() call order -
# the same registration-index-keyed convention every other port's own
# thumbnail set already uses) from this project's own real, already-captured
# gameplay screenshots (metadata/screenshots/<TITLE>.bmp - a full 640x360
# raw canvas capture, LCD content at the real (26,12)-(613,347) sub-rect,
# see machineDependent.h's own MD_SCREEN_WIDTH/HEIGHT and src/sdl3/
# sdlBackend.c's own GAME_ORIGIN_X/Y=26/12, GAME_SCALE=7 for where that
# 588x336 crop rectangle comes from).
#
# Point-sample downscaled (ImageMagick's -sample, NOT a blurring resize
# filter), matching the sibling Tinyjoypad_SDL project's own thumbnail
# pipeline precedent exactly - see that project's own src/playdate/main.c
# header comment for why a blur would be wrong for pixel-art LCD content.
#
# Not wired into any build step - a one-off asset-preparation script, run by
# hand whenever menuGameList.c's own registration order/count changes (the
# same "no checked-in generator is expected to run automatically" precedent
# gamebuino_classic_vircon32/CLAUDE.md documents for its own thumbnail
# compositing step).
# -----------------------------------------------------------------------------

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MENU_LIST = ROOT / "src" / "gameworld" / "menuGameList.c"
SCREENSHOTS = ROOT / "metadata" / "screenshots"
OUT_DIR = Path(__file__).resolve().parent.parent / "Source" / "thumbnails"

# Playdate's own GB_GRAY rendering (kColorGrey, a real native LCDPattern
# fill - see sdlBackend.c's own "Grayscale rendering" comment in CLAUDE.md)
# is visually IDENTICAL to the checkerboard dither every port shows with
# gbRealGrayColor off, never the flat SOLID gray SDL2/SDL3 show with it on
# (their own default - gamesMain_init()) - Playdate has no equivalent
# "solid" look at all on a strictly 1-bit panel. SCREENSHOTS therefore
# should point at a dedicated gray-OFF capture, not the shared
# metadata/screenshots/ directory (which stays gray-ON, matching SDL2/
# SDL3's own real default and every other consumer of that directory -
# the README's own display, SDL's own assets/thumbnails/). Regenerate that
# source directory with:
#
#   cd src/sdl3/build && ./GamebuinoClassicSDL3 -ms -ns -nd -gray 0
#
# (or the SDL2 binary, either produces pixel-identical dither content),
# then point SCREENSHOTS at wherever that batch run's own working
# directory was, run this script, and discard the temporary gray-off
# capture afterward - matching this project's own established "no
# checked-in generator for a one-off asset-staging step" precedent (see
# CLAUDE.md's "Thumbnail generation" section).

# The real LCD sub-rect within every 640x360 raw screenshot capture (see this
# script's own header comment above).
CROP_X, CROP_Y, CROP_W, CROP_H = 26, 12, 588, 336

# Thumbnail size shown in the Playdate-native menu (main.c's own
# MENU_THUMB_W/H) - 2x the real native 84x48 LCD resolution, the same clean-
# multiplier reasoning as every other port's own GAME_SCALE choice.
THUMB_W, THUMB_H = 168, 96


def main():
    text = MENU_LIST.read_text()
    titles = re.findall(r'addGame\(\s*"([^"]*)"', text)
    if not titles:
        print("No addGame() calls found - check MENU_LIST path/regex.", file=sys.stderr)
        sys.exit(1)

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    missing = []
    for i, title in enumerate(titles):
        src = SCREENSHOTS / f"{title}.bmp"
        if not src.exists():
            missing.append(title)
            continue

        dst = OUT_DIR / f"thumb_{i:02d}.png"
        crop_spec = f"{CROP_W}x{CROP_H}+{CROP_X}+{CROP_Y}"
        sample_spec = f"{THUMB_W}x{THUMB_H}"
        cmd = [
            "magick", str(src),
            "-crop", crop_spec, "+repage",
            "-sample", sample_spec,
            str(dst),
        ]
        subprocess.run(cmd, check=True)

    print(f"Generated {len(titles) - len(missing)} thumbnails into {OUT_DIR}")
    if missing:
        print(f"Missing screenshots for {len(missing)} games:", file=sys.stderr)
        for t in missing:
            print(f"  {t}", file=sys.stderr)


if __name__ == "__main__":
    main()
