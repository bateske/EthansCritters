#!/usr/bin/env python3
"""Ethan's Critters: the GitHub README's banner (docs/banner.png) and the
repository's social preview (docs/social.png, 1280x640: GitHub's Settings,
Social preview).

A strip of the painted swamp as the game streams it, the title screen's
own lettering over it, and the cast: the squire and his swipe squaring up
to a rearing raccoon on the left, OLD GULLET at the pond on the right, a
turtle, a beaver and a chameleon about. Drawn in palette indices like the
title (title.py's canvas, sprites and lettering, at the banner's size),
then scaled up by whole pixels.

    python EthansCritters/tools/card/banner.py      docs/banner.png, docs/social.png
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

SCALE = 4
# Each picture: its size (px, before SCALE), the world's top-left under it, the cast (sheet,
# animation, frame, feet x, y, flipped, its shadow's x, y and half-width or None), the band darkened behind the
# lettering (rows above, half its width) and the lettering's two rows (top, scale each).
BANNER = dict(out=ROOT / "docs" / "banner.png", size=(320, 88), crop=(456, 352),   # Turtle Pond's west shore
              cast=[("SQUIRE", "thrust", 4, 26, 74, False, (26, 73, 7)), ("RACCOON", "attack", 1, 56, 73, True, (54, 72, 7)),
                    ("CHAMELEON", "Idle", 0, 40, 48, False, None), ("TURTLE", "idle_blink", 0, 252, 30, True, None),
                    ("BEAVER", "Idle Water", 0, 206, 76, True, None), ("TOAD", "Idle", 0, 276, 84, True, (276, 80, 22))],
              band=(52, 100), title=((5, 2), (23, 3)))
# The social preview is read on phones, at a quarter of its size or less: few things, big. 160 x 80 at
# 8x (1280 x 640): the swamp sunk a step into shadow, the title and three short lines of CHGfx's font
# on the left, the squire squaring up to OLD GULLET on the right.
SOCIAL = dict(out=ROOT / "docs" / "social.png", size=(160, 80), crop=(560, 420), scale=8,
              cast=[("TOAD", "Idle", 0, 136, 79, True, (136, 76, 24)),
                    ("SQUIRE", "thrust", 4, 110, 78, False, (110, 77, 7))],
              sink=1, under=True, band=(0, 0), title=((3, 1), (13, 2)), title_centre=56,
              text=("FOR CHGAME", "44 MB OF SD CARD", "SPRITES STREAMED"), text_at=(4, 44), text_scale=1)
FONT = ROOT / "chgame" / "platform" / "board" / "arduino" / "CHGame" / "libraries" / "CHGfx" / "src" / "CHGfx_font.h"


def font5x7():
    """CHGfx's font: ASCII 32..126, five column bytes a glyph, bit 0 the top row."""
    import re
    text = FONT.read_text(encoding="utf-8")
    body = text[text.index("{", text.index("chgfx_font5x7")) + 1:text.index("};")]
    cols = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    return {chr(32 + i): cols[i * 5:i * 5 + 5] for i in range(95)}


def words(cv, lines, x0, y0, k):
    """Lines of CHGfx's 5x7 font, each pixel k x k, bone with a soot outline (readable on anything)."""
    glyphs = font5x7()
    on = set()
    for n, t in enumerate(lines):
        x = x0
        for ch in t:
            for gx, col in enumerate(glyphs[ch]):
                for gy in range(7):
                    if col >> gy & 1:
                        on |= {(x + gx * k + i, y0 + n * 9 * k + gy * k + j) for i in range(k) for j in range(k)}
            x += 6 * k
    for x, y in on:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1, 2):
                if (x + dx, y + dy) not in on:
                    cv.put(x + dx, y + dy, T.SOOT)
    for x, y in on:
        cv.put(x, y, T.BONE)


def build(pic=BANNER):
    world = CL.world_idx()
    BW, BH = pic["size"]
    T.W, T.H = BW, BH       # title.py's canvas, sprites and lettering, at this picture's size
    cv = T.Canvas()
    x0, y0 = pic["crop"]
    for y in range(BH):
        o = (y0 + y) * CL.WP.W + x0
        cv.px[y * BW:(y + 1) * BW] = world[o:o + BW]
    rng = random.Random(T.SEED)
    for _ in range(pic.get("sink", 0)):          # the swamp a step into shadow: what is on it stands out
        cv.px = bytearray(T.DARKER[c] for c in cv.px)

    def vignette():
        # the vignette: the edges and the band behind the lettering a step or two darker
        def dim(x, y, k):
            c = cv.get(x, y)
            t = T.BAYER4[y & 3][x & 3]
            while k > 0:
                if t < min(16, k):
                    c = T.DARKER[c]
                k -= 16
            cv.px[y * BW + x] = c

        rows, half = pic["band"]
        for y in range(BH):
            for x in range(BW):
                e = min(x, BW - 1 - x, y * 3, (BH - 1 - y) * 4)
                k = max(0, 28 - e) if e < 28 else 0
                dx = abs(x - BW // 2)
                if y < rows and dx < half:
                    k += min(24, (half - dx) // 2) if y < rows - 8 else (rows - y) * 3
                dim(x, y, min(32, k))

    if pic.get("under"):                         # (under the cast: it keeps its colours)
        vignette()

    # the cast, with their shadows
    def shadow(x, y, hw):
        for dy in (-1, 0, 1):
            w = hw - (2 if dy else 0)
            for dx in range(-w, w + 1):
                if 0 <= x + dx < BW and 0 <= y + dy < BH:
                    cv.put(x + dx, y + dy, T.DARKER[cv.get(x + dx, y + dy)])

    for sheet, anim, frame, x, y, flip, sh in pic["cast"]:
        if sh:
            shadow(*sh)
        cv.sprite(getattr(T.S, sheet), anim, frame, x, y, flip=flip)

    if not pic.get("under"):
        vignette()

    (t1, s1), (t2, s2) = pic["title"]
    at = cv
    if "title_centre" in pic:                   # letter() centres on the canvas: shift it to its own centre
        class Shifted:
            def put(self, x, y, c, dx=pic["title_centre"] - BW // 2):
                cv.put(x + dx, y, c)
        at = Shifted()
    T.letter(at, "ETHAN'S", s1, t1, rng, T.BONE, T.MUD, T.BARK, T.PEAT, wobble=(0, 0, 1), moss=4, drips=1)
    T.letter(at, "CRITTERS", s2, t2, rng, T.BONE, T.MUD, T.BARK, T.PEAT, wobble=(-1, 0, 0, 1), moss=9, drips=5)
    if "text" in pic:
        words(cv, pic["text"], *pic["text_at"], pic["text_scale"])
    return cv


def save(cv, pic=BANNER):
    from PIL import Image
    BW, BH = pic["size"]
    im = Image.new("RGB", (BW, BH))
    pal = [T.P.rgb888(i) for i in range(16)]
    im.putdata([pal[c] for c in cv.px])
    k = pic.get("scale", SCALE)
    im = im.resize((BW * k, BH * k), Image.NEAREST)
    Path(pic["out"]).parent.mkdir(parents=True, exist_ok=True)
    im.save(pic["out"], optimize=True)
    return pic["out"]


if __name__ == "__main__":
    for pic in (BANNER, SOCIAL):
        print(save(build(pic), pic))
