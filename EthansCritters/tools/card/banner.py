#!/usr/bin/env python3
"""Ethan's Critters: the GitHub README's banner (docs/banner.png).

A strip of the painted swamp as the game streams it, the title screen's
own lettering over it, and the cast: the squire and his swipe squaring up
to a rearing raccoon on the left, OLD GULLET at the pond on the right, a
turtle, a beaver and a chameleon about. Drawn in palette indices like the
title (title.py's canvas, sprites and lettering, at the banner's size),
then scaled up by whole pixels.

    python EthansCritters/tools/card/banner.py      docs/banner.png
"""
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1]
ROOT = GAME.parent
sys.path.insert(0, str(HERE))
import clips as CL      # noqa: E402  (the world, as the clips take it)
import title as T       # noqa: E402

BW, BH, SCALE = 320, 88, 4
OUT = ROOT / "docs" / "banner.png"
CROP = (456, 352)       # the world's top-left under the banner: Turtle Pond's west shore


def build():
    world = CL.world_idx()
    T.W, T.H = BW, BH       # title.py's canvas, sprites and lettering, at the banner's size
    cv = T.Canvas()
    x0, y0 = CROP
    for y in range(BH):
        o = (y0 + y) * CL.WP.W + x0
        cv.px[y * BW:(y + 1) * BW] = world[o:o + BW]
    rng = random.Random(T.SEED)

    # the cast, with their shadows
    def shadow(x, y, hw):
        for dy in (-1, 0, 1):
            w = hw - (2 if dy else 0)
            for dx in range(-w, w + 1):
                c = cv.get(x + dx, y + dy) if 0 <= x + dx < BW and 0 <= y + dy < BH else None
                if c is not None:
                    cv.put(x + dx, y + dy, T.DARKER[c])

    S = T.S
    shadow(26, 73, 7)
    cv.sprite(S.SQUIRE, "thrust", 4, 26, 74)
    shadow(54, 72, 7)
    cv.sprite(S.RACCOON, "attack", 1, 56, 73, flip=True)
    cv.sprite(S.CHAMELEON, "Idle", 0, 40, 48)
    cv.sprite(S.TURTLE, "idle_blink", 0, 252, 30, flip=True)
    cv.sprite(S.BEAVER, "Idle Water", 0, 206, 76, flip=True)
    shadow(276, 80, 22)
    cv.sprite(S.TOAD, "Idle", 0, 276, 84, flip=True)

    # the vignette: the edges and the band behind the lettering a step or two darker
    def dim(x, y, k):
        c = cv.get(x, y)
        t = T.BAYER4[y & 3][x & 3]
        while k > 0:
            if t < min(16, k):
                c = T.DARKER[c]
            k -= 16
        cv.px[y * BW + x] = c

    for y in range(BH):
        for x in range(BW):
            e = min(x, BW - 1 - x, y * 3, (BH - 1 - y) * 4)
            k = max(0, 28 - e) if e < 28 else 0
            dx = abs(x - BW // 2)
            if y < 52 and dx < 100:
                k += min(24, (100 - dx) // 2) if y < 44 else (52 - y) * 3
            dim(x, y, min(32, k))

    T.letter(cv, "ETHAN'S", 2, 5, rng, T.BONE, T.MUD, T.BARK, T.PEAT, wobble=(0, 0, 1), moss=4, drips=1)
    T.letter(cv, "CRITTERS", 3, 23, rng, T.BONE, T.MUD, T.BARK, T.PEAT, wobble=(-1, 0, 0, 1), moss=9, drips=5)
    return cv


def save(cv, path=OUT):
    from PIL import Image
    im = Image.new("RGB", (BW, BH))
    pal = [T.P.rgb888(i) for i in range(16)]
    im.putdata([pal[c] for c in cv.px])
    im = im.resize((BW * SCALE, BH * SCALE), Image.NEAREST)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    im.save(path, optimize=True)
    return path


if __name__ == "__main__":
    print(save(build()))
