#!/usr/bin/env python3
"""Ethan's Critters: the title (Path F clip, section TITL): a still, and
since M7b a loop of LOOP frames.

Composed offline at 128x128 straight in palette indices: the Swamp tileset
and the sprite sheets come quantised from tools/assets/quantise.py, so the
scene is in THE palette pixel for pixel and needs no second quantise. A
little swamp: a sand clearing, a pond with lily pads and reeds, willows
leaning over the top, the squire squaring up to a rearing raccoon, a turtle
on the bank and a beaver in the water; over it "ETHAN'S CRITTERS" in a
chunky lettering of its own (mud blocks with a peat side, a soot outline,
moss growing on top and mud dripping off), and the top and bottom rows
dithered darker, worn and dirty, so the lettering and the game's blinking
"PRESS A" read. The loop (scene(k)): the reeds and cattails sway, glints
drift on the pond and rings spread round the beaver, the squire breathes,
a second raccoon peeks out of the bush and blinks, the turtle blinks, the
beaver bobs, gnats dance; the lettering and everything else stay put (the
animation draws on a random stream of its own, so the still's draws, and
with them the lettering, are the same in every frame).

    python EthansCritters/tools/card/title.py      out/art/title.png (3x)

tools/card/clips.py packs the loop (loop()) into CRITTERS.DAT.
"""
import math
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1]
ROOT = GAME.parent
sys.path.insert(0, str(GAME / "tools" / "assets"))
import palette as P     # noqa: E402
import quantise as Q    # noqa: E402
import sheets as S      # noqa: E402

W = H = 128
SEED = 0xEC27           # every random choice below: same input, same picture
LOOP, LOOP_MS = 16, 125     # the title's loop: 2 s
PREVIEW = ROOT / "out" / "art" / "title.png"

(SOOT, UMBER, PEAT, BARK, MUD, BONE, DEEPMOSS, BOG, MOSS, LICHEN, OCHRE, FOG, SLATE, STONE,
 RUST, WATER) = range(16)

# One step darker, colour by colour (the vignette): browns down the browns,
# greens down the mosses, greys down the greys, all ending in soot.
DARKER = (SOOT, SOOT, UMBER, PEAT, BARK, MUD, SOOT, DEEPMOSS, BOG, MOSS, BARK, STONE, UMBER,
          SLATE, UMBER, DEEPMOSS)

BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


# --- sources --------------------------------------------------------------------

class Src:
    """A quantised sheet: indices row-major, `clear` where it is transparent."""
    def __init__(self, sheet):
        im = Q.sheet_image(sheet)
        self.w, self.h = im.size
        self.idx = Q.quantise_sheet(sheet)
        self.clear = P.KEY if sheet.kind == "sprite" else Q.HOLE
        self.sheet = sheet

    def at(self, x, y):
        return self.idx[y * self.w + x]


_srcs = {}


def src(sheet):
    s = _srcs.get(sheet.key)
    if s is None:
        s = _srcs[sheet.key] = Src(sheet)
    return s


class Canvas:
    def __init__(self, fill=MOSS):
        self.px = bytearray([fill]) * (W * H)

    def get(self, x, y):
        return self.px[y * W + x]

    def put(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[y * W + x] = c

    def blit(self, s, box, dx, dy, flip=False):
        """Pastes s's box (x0, y0, x1, y1) with its top-left at (dx, dy),
        skipping transparent pixels; flip mirrors it left-right."""
        x0, y0, x1, y1 = box
        for y in range(y0, y1):
            for x in range(x0, x1):
                c = s.at(x, y)
                if c != s.clear:
                    ox = (x1 - 1 - x) if flip else (x - x0)
                    self.put(dx + ox, dy + y - y0, c)

    def tile(self, s, sx, sy, dx, dy):
        self.blit(s, (sx, sy, sx + 16, sy + 16), dx, dy)

    def sprite(self, sheet, anim, frame, x, y, flip=False):
        """A sheet's frame with its pivot (the feet) at (x, y)."""
        s = src(sheet)
        a = sheet.anim(anim)
        cw, ch = sheet.cell
        cx, cy = (a.col + frame) * cw, a.row * ch
        px, py = sheet.pivot
        dx = x - ((cw - 1 - px) if flip else px)
        self.blit(s, (cx, cy, cx + cw, cy + ch), dx, y - py, flip)

    def rect(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(H, y1)):
            for x in range(max(0, x0), min(W, x1)):
                self.px[y * W + x] = c


# --- the tileset's pieces (source pixel boxes; see out/art/tileset_preview.png) ---

GRASS = (240, 224)                      # plain grass
GRASS_TUFTS = ((192, 192), (208, 192), (224, 208), (256, 192), (272, 224), (208, 240), (256, 240))
# 9-slices, 16 px tiles: (left, top) of the top-left tile, and the tile steps
SAND = dict(x=128, y=0, edge=(1, 4), corner=5)      # grass frame around sand (6x6 tiles)
POND = dict(x=192, y=128, edge=(1, 2), corner=3)    # grass lip around a hole (4x4): water under it

WILLOW_BIG = (426, 104, 496, 190)
WILLOW_TALL = (513, 112, 575, 191)
REEDS = (65, 193, 96, 223)
CATTAIL = (142, 230, 148, 252)
CATTAIL2 = (173, 232, 181, 254)
LILY = (133, 298, 154, 314)
LILY2 = (167, 299, 185, 312)
LILY_FLOWER = (135, 327, 153, 344)
ROCK = (228, 266, 252, 278)
MUSH_RED = (238, 334, 245, 343)
MUSH_RED_DOTS = (268, 333, 278, 344)
MUSH_WHITE = (231, 364, 247, 378)
STICKS = (192, 385, 223, 398)
BUSH = (357, 224, 380, 250)
GRASS_CLUMP = (488, 230, 504, 247)


def slice9(cv, s, spec, tx0, ty0, tw, th):
    """A 9-slice patch of tw x th tiles at tile-aligned screen (tx0, ty0) px;
    the edges cycle through their source tiles."""
    x, y = spec["x"], spec["y"]
    e0, e1 = spec["edge"]
    k = spec["corner"]
    for j in range(th):
        sy = 0 if j == 0 else k if j == th - 1 else e0 + (j - 1) % (e1 - e0 + 1)
        for i in range(tw):
            sx = 0 if i == 0 else k if i == tw - 1 else e0 + (i - 1) % (e1 - e0 + 1)
            cv.tile(s, x + sx * 16, y + sy * 16, tx0 + i * 16, ty0 + j * 16)


# --- the lettering ----------------------------------------------------------------

# A chunky face of our own, 2 px stems (drawn here, not a scaled system font).
GLYPHS = {
    "E": ["######", "##....", "##....", "#####.", "##....", "##....", "######"],
    "T": ["######", "######", "..##..", "..##..", "..##..", "..##..", "..##.."],
    "H": ["##..##", "##..##", "##..##", "######", "##..##", "##..##", "##..##"],
    "A": [".####.", "##..##", "##..##", "######", "##..##", "##..##", "##..##"],
    "N": ["##..##", "###.##", "######", "##.###", "##..##", "##..##", "##..##"],
    "S": [".#####", "##....", "##....", ".####.", "....##", "....##", "#####."],
    "C": [".#####", "##....", "##....", "##....", "##....", "##....", ".#####"],
    "R": ["#####.", "##..##", "##..##", "#####.", "##.##.", "##..##", "##..##"],
    "I": ["####", ".##.", ".##.", ".##.", ".##.", ".##.", "####"],
    "'": ["##", "##", ".#"],
    " ": ["...."],
    # (M7b: the clips' words)
    "W": ["##...##", "##...##", "##.#.##", "##.#.##", "#######", "###.###", "##...##"],
    "M": ["##...##", "###.###", "#######", "##.#.##", "##...##", "##...##", "##...##"],
    "P": ["#####.", "##..##", "##..##", "#####.", "##....", "##....", "##...."],
    "K": ["##..##", "##.##.", "####..", "###...", "####..", "##.##.", "##..##"],
    "Y": ["##..##", "##..##", "##..##", ".####.", "..##..", "..##..", "..##.."],
    "O": [".####.", "##..##", "##..##", "##..##", "##..##", "##..##", ".####."],
    "U": ["##..##", "##..##", "##..##", "##..##", "##..##", "##..##", ".####."],
    "Q": [".####.", "##..##", "##..##", "##..##", "##.###", "##..##", ".###.#"],
    "L": ["##....", "##....", "##....", "##....", "##....", "##....", "######"],
    "D": ["#####.", "##..##", "##..##", "##..##", "##..##", "##..##", "#####."],
}


def word_mask(text, scale, rng, wobble):
    """The word's face as a set of (x, y): glyphs scaled, a pixel apart,
    each nudged up or down a little (hand-set, not typeset), with a few
    corners chipped off (scaled glyphs only: at 1x a chip eats a stroke)."""
    pts = set()
    x = 0
    for ch in text:
        g = GLYPHS[ch]
        dy = rng.choice(wobble)
        for gy, row in enumerate(g):
            for gx, c in enumerate(row):
                if c == "#":
                    for sy in range(scale):
                        for sx in range(scale):
                            pts.add((x + gx * scale + sx, dy + gy * scale + sy))
        x += len(g[0]) * scale + max(1, scale // 2) + 1
    if scale < 2:
        return pts
    # chip convex corners: a pixel with two open neighbours at right angles
    chips = []
    for (px, py) in sorted(pts):            # (sorted: the same rng draws on any Python)
        open_ = [(dx, dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (px + dx, py + dy) not in pts]
        if len(open_) == 2 and open_[0][0] != open_[1][0] and rng.random() < 0.35:
            chips.append((px, py))
    for p in chips:
        pts.discard(p)
    return pts


def letter(cv, text, scale, top, rng, face_hi, face, face_lo, side, wobble=(0,), moss=0, drips=0):
    """Lettering centred at row `top`: a soot outline round the face and its
    two-pixel peat side (a block of dried mud, seen from a little above),
    the face lit from the top, moss tufts on top edges, mud drips below."""
    pts = word_mask(text, scale, rng, wobble)
    w = max(x for x, _ in pts) + 1
    ox = (W - w) // 2
    face_pts = {(x + ox, y + top) for x, y in pts}
    depth = 2 if scale > 1 else 1
    side_pts = {(x, y + d) for x, y in face_pts for d in range(1, depth + 1)} - face_pts
    body = face_pts | side_pts
    # drips: a run of side colour hanging off a letter's foot
    low = max(y for _, y in face_pts) - 1
    bottoms = sorted(p for p in face_pts if p[1] >= low and (p[0], p[1] + 1) not in face_pts)
    drip_pts = set()
    for _ in range(drips):
        if not bottoms:
            break
        bx, by = bottoms[rng.randrange(len(bottoms))]
        n = rng.choice((2, 3, 3, 4, 5))
        for k in range(1, depth + n + 1):
            drip_pts.add((bx, by + k))
    body |= drip_pts
    outline = set()
    for x, y in body:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                q = (x + dx, y + dy)
                if q not in body:
                    outline.add(q)
    for x, y in outline:
        cv.put(x, y, SOOT)
    for x, y in side_pts:
        cv.put(x, y, side)
    for x, y in drip_pts - side_pts:        # wet mud, lighter than the side
        cv.put(x, y, face_lo if (x, y + 1) in drip_pts else side)
    ys = [y for _, y in face_pts]
    y0, y1 = min(ys), max(ys)
    for x, y in face_pts:
        t = (y - y0) / max(1, y1 - y0)          # 0 top .. 1 bottom
        lit = (x, y - 1) not in face_pts
        if lit:
            c = face_hi
        elif t > 0.62 and (t > 0.8 or (x + y) % 2):
            c = face_lo
        else:
            c = face
        cv.put(x, y, c)
    # wear: a few cracks and pits in the face
    for x, y in sorted(face_pts):
        if rng.random() < 0.035 and (x, y - 1) in face_pts:
            cv.put(x, y, side)
    # moss: clumps on the top edges, hanging a pixel or two down the face
    tops = sorted(p for p in face_pts if (p[0], p[1] - 1) not in face_pts)
    for _ in range(moss):
        x, y = tops[rng.randrange(len(tops))]
        n = rng.choice((2, 3, 3, 4, 5))
        for k in range(n):
            if (x + k, y) not in face_pts:
                break
            cv.put(x + k, y, MOSS)
            if (x + k, y + 1) in face_pts and rng.random() < 0.7:
                cv.put(x + k, y + 1, BOG if rng.random() < 0.5 else MOSS)
                if (x + k, y + 2) in face_pts and rng.random() < 0.3:
                    cv.put(x + k, y + 2, BOG)
            if rng.random() < 0.75:
                cv.put(x + k, y - 1, LICHEN)
                cv.put(x + k, y - 2, SOOT)
    return face_pts


# --- the scene ---------------------------------------------------------------------

def darken(cv, y0, y1, strength):
    """Rows y0..y1 dithered toward soot: strength(y) in sixteenths, 16 = one
    full DARKER step, 32 = two."""
    for y in range(y0, y1):
        s = strength(y)
        for x in range(W):
            c = cv.get(x, y)
            t = BAYER4[y & 3][x & 3]
            k = s
            while k > 0:
                if t < min(16, k):
                    c = DARKER[c]
                k -= 16
            cv.px[y * W + x] = c


def swayed(cv, s, box, dx, dy, k, amp, phase=0.0):
    """A clump of reeds blown about: s's box pasted at (dx, dy) with each row
    pushed sideways, the tips most and the roots not at all, by the loop's
    frame k (amp px at most; None: still)."""
    x0, y0, x1, y1 = box
    h = y1 - y0
    for y in range(y0, y1):
        off = 0
        if k is not None:
            t = 1.0 - (y - y0) / max(1, h - 1)
            off = int(round(amp * t * t * math.sin(2 * math.pi * k / LOOP + phase)))
        for x in range(x0, x1):
            c = s.at(x, y)
            if c != s.clear:
                cv.put(dx + x - x0 + off, dy + y - y0, c)


def breathe(cv, sheet, anim, frame, x, y, flip, waist):
    """The chest rising: the rows of a frame above `waist` (cell rows)
    pasted again a pixel higher over it."""
    s = src(sheet)
    a = sheet.anim(anim)
    cw, ch = sheet.cell
    cx, cy = (a.col + frame) * cw, a.row * ch
    px, py = sheet.pivot
    dx = x - ((cw - 1 - px) if flip else px)
    cv.blit(s, (cx, cy, cx + cw, cy + waist), dx, y - py - 1, flip)


def water_life(cv, k):
    """The loop's moving water: glints drifting on the pond and rings
    spreading round the beaver (on the water only)."""
    r = random.Random(SEED + 1)
    def wet(x, y, c):
        if 0 <= x < W and 0 <= y < H and cv.get(x, y) == WATER:
            cv.put(x, y, c)
    for _ in range(9):
        x0, y, n, ph = r.randrange(70, 124), r.randrange(70, 126), r.choice((2, 2, 3)), r.randrange(LOOP)
        u = (k + ph) % LOOP
        if u < 11:
            for i in range(n):
                wet(x0 + u // 3 + i, y, FOG if u in (3, 4, 5, 6, 7) else STONE)
    for ring in (0, LOOP // 2):                     # round the beaver at (96, 100)
        u = (k + ring) % LOOP
        rx, ry = 7 + u, 3 + u // 2
        for a in range(0, 360, 12 if u < 8 else 20):
            t = math.radians(a)
            wet(int(round(96 + rx * math.cos(t))), int(round(98 + ry * math.sin(t))), STONE if u > 9 else FOG)


def scene(k=None):
    """The title: the still (k None), or frame k of the loop."""
    rng = random.Random(SEED)
    T = src(S.TILESET)
    cv = Canvas()
    # grass, with tufts here and there
    for ty in range(0, H, 16):
        for tx in range(0, W, 16):
            g = GRASS if rng.random() < 0.55 else rng.choice(GRASS_TUFTS)
            cv.tile(T, g[0], g[1], tx, ty)
    # the pond: water, then the grass lip round it (runs off the right and bottom)
    cv.rect(64, 64, W, H, WATER)
    slice9(cv, T, POND, 64, 64, 5, 5)
    # the sand clearing
    slice9(cv, T, SAND, -16, 64, 6, 5)
    # trodden path: sand worn into the grass toward the water
    for _ in range(60):
        x, y = rng.randrange(54, 74), rng.randrange(90, 112)
        cv.put(x, y, rng.choice((MUD, BARK, BARK, PEAT)))
    # water life
    cv.blit(T, LILY, 106, 106)
    cv.blit(T, LILY_FLOWER, 108, 80)
    cv.blit(T, LILY2, 76, 112)
    if k is not None:
        water_life(cv, k)
    swayed(cv, T, REEDS, 66, 80, k, 1.6)
    swayed(cv, T, CATTAIL, 84, 92, k, 1.2, 1.3)
    swayed(cv, T, CATTAIL2, 120, 98, k, 1.2, 2.4)
    # willows lean in over the top
    cv.blit(T, WILLOW_BIG, -26, -14)
    cv.blit(T, WILLOW_TALL, 82, -16)
    if k is not None:               # another raccoon, round the bush, after the mushrooms (its idle: a blink at f4)
        cv.sprite(S.RACCOON, "idle", (k // 2 + 1) % 8, 42, 61, flip=True)
    cv.blit(T, BUSH, 44, 44)
    # ground clutter
    cv.blit(T, ROCK, 2, 104)
    cv.blit(T, STICKS, 40, 112)
    cv.blit(T, MUSH_RED_DOTS, 12, 60)
    cv.blit(T, MUSH_RED, 24, 66)
    cv.blit(T, MUSH_WHITE, 50, 56)
    cv.blit(T, GRASS_CLUMP, 2, 84)
    # the cast: the squire squares up to a rearing raccoon
    if k is None:
        cv.sprite(S.SQUIRE, "thrust", 0, 30, 100)
        cv.sprite(S.RACCOON, "attack", 1, 58, 100, flip=True)
        cv.sprite(S.TURTLE, "idle_blink", 0, 104, 72, flip=True)
        cv.sprite(S.BEAVER, "Idle Water", 0, 96, 100, flip=True)
    else:
        cv.sprite(S.SQUIRE, "thrust", 0, 30, 100)
        if (k // 4) % 2:
            breathe(cv, S.SQUIRE, "thrust", 0, 30, 100, False, 23)
        cv.sprite(S.RACCOON, "attack", 1, 58, 100, flip=True)
        cv.sprite(S.TURTLE, "idle_blink", min(9, max(0, k - 5)), 104, 72, flip=True)
        cv.sprite(S.BEAVER, "Idle Water", (k // 2) % 4, 96, 100, flip=True)
        r = random.Random(SEED + 2)                 # gnats over the bush and the pond
        for gx, gy in ((52, 50), (112, 62)):
            for _ in range(3):
                p = r.random() * 6.283
                cv.put(gx + int(round(3 * math.cos(2 * math.pi * k / LOOP + p))),
                       gy + int(round(2 * math.sin(4 * math.pi * k / LOOP + p))), SOOT)
    # vignette: top (behind the lettering) and bottom (behind PRESS A)
    darken(cv, 0, 46, lambda y: 32 if y < 26 else (46 - y) * 32 // 20)
    darken(cv, 103, H, lambda y: min(26, (y - 103) * 26 // 8 + 4))     # (M7b: deeper, for the best time and the choice)
    # the lettering
    letter(cv, "ETHAN'S", 1, 5, rng, BONE, MUD, BARK, PEAT, wobble=(0, 0, 1), moss=3, drips=1)
    letter(cv, "CRITTERS", 2, 17, rng, BONE, MUD, BARK, PEAT, wobble=(-1, 0, 0, 1), moss=7, drips=4)
    return cv


def compose():
    """The title still: W*H palette indices, row-major."""
    return bytes(scene().px)


def loop():
    """The title's loop: LOOP frames as compose() gives the still."""
    return [bytes(scene(k).px) for k in range(LOOP)]


def preview(idx, path=PREVIEW, scale=3):
    from PIL import Image
    im = Image.new("RGB", (W, H))
    pal = [P.rgb888(i) for i in range(16)]
    im.putdata([pal[c] for c in idx])
    im = im.resize((W * scale, H * scale), Image.NEAREST)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    im.save(path)
    return path


if __name__ == "__main__":
    print(preview(compose()))
