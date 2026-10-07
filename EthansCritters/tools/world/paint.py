#!/usr/bin/env python3
"""Ethan's Critters: the world painter (offline).

    python EthansCritters/tools/world/paint.py [--force]

Paints the swamp, 1024 x 768, as ONE bitmap of palette indices from
tools/world/map.txt (64 x 48 terrain cells of 16 px) and
tools/world/objects.json (props, nests, mushrooms, regions, the gate and the
player start), out of the Swamp tileset quantised by tools/assets/quantise.py
(tileset.py). Nothing is tiled at runtime: the game streams this picture
from the card (worldpack.py has the layout), so every patch of it can be
unique, and it is worn in like a place that is lived in.

Steps:
  1. ground: water everywhere; mud, dirt and sand as soft fields from the
     cells (interpolated between cell centres, roughened with noise), each
     a little wider than the one above it, so dirt has a muddy lip where it
     meets the water; their rims and grit.
  2. grass: the tileset's sand/grass autotiles as dual-grid pieces on a
     grid offset by half a cell; diagonal pinches repaired (the tileset has
     no piece for them); tufts; the forest floor darker.
  3. water: deep pools darker (dithered in), ripples, the waterline glint.
  4. banks and the hut: the tileset's earth-bank strips along runs of B/H
     cells (the face on the cell, its grass top on the row above).
  5. worn in: mud splashed up the banks, trodden trails, scuffed rings and
     debris round the nests.
  6. props, bottom-up by foot y: willows (forest cells get a seeded crowd),
     rocks, sticks, reeds (reed cells get a seeded bed), lily pads, plants,
     brown decor mushrooms, gnawed stumps, the beaver lodges' remains,
     brambles.
  7. The Rot: dead reeds and grass, everything a dithered step darker;
     gnats over it.
  8. occluders: which willow each pixel shows (props drawn after it take
     theirs), and the willows an actor can get behind with exactly those
     pixels (occlpack.py: the card's OCCL section, the table in WorldData).

The world is painted TPH = 4 times, once for each ambient phase (M8), and
for LAYERS = 2 layers: 0 the swamp, 1 healed (The Rot as it is once its
toad is beaten: its willows, reeds and grass alive, no grime, no gnats,
lilies on its pond). Every paint makes the same draws from the one stream
(the extras have their own), so the layout, the terrain and everything
that stands still are the same in all eight, and layer 1 differs from 0
only in and around The Rot. The phases move: the reeds' and cattails' tops
(a pixel, the lean rolling across the swamp as a wave), the lily pads (a
pixel down and up), the shallows' ripples, the swell of the deep water's
chop (bands of 3 rows a pixel each way in turn), the waterline's glint
(lapping), sun flecks on deep water, The Rot's gnats. The willows stand
still (the occluders are cut from the world: a tree keeps a pixel only
where it shows in every phase and layer). The game picks the phase from
the clock and the layer from the save: same blocks a frame, any phase.

Outputs (deterministic: the same inputs give the same bytes):
  out/world/world.png        the world as the panel shows it (1x)
  out/world/regions.png      2x crops: the start and each region
  out/world/phases.gif       2x views cycling through the phases (and The Rot healed)
  out/world/healed.png       The Rot, and the same in the healed layer
  out/world/terrain.png      the collision table, nests, mushrooms, regions
  out/world/occluders.png    what each kept willow redraws over an actor
  out/world/WRLD.bin         the card section (worldpack.py)
  src/assets/WorldData.{h,cpp}   GENERATED: the world's geometry, the terrain
                             table (8 px cells: OPEN SHALLOW DEEP SOLID,
                             run-length coded row by row), regions, start,
                             nests, mushrooms, gate, the occluders' table

tools/mkcard.py calls build() for the section; the paint is cached in
out/world/ under a hash of every input, so an unchanged world costs a load.
"""
import argparse
import hashlib
import json
import math
import pickle
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1]
ROOT = GAME.parent
sys.path.insert(0, str(GAME / "tools" / "assets"))
sys.path.insert(0, str(HERE))
import occlpack as OC  # noqa: E402
import palette as P     # noqa: E402
import tileset as TS    # noqa: E402
import worldpack as WP  # noqa: E402

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageMath  # noqa: E402

MAP = HERE / "map.txt"
OBJECTS = HERE / "objects.json"
OUT = ROOT / "out" / "world"
SRC_ASSETS = GAME / "src" / "assets"
CACHE = OUT / "paint.cache"

W, H = 1024, 768
CELL = 16
MW, MH = W // CELL, H // CELL
TPH = 4                         # ambient phases: the whole world painted once for each (the game picks one)
LAYERS = 2                      # world layers: 0 the swamp, 1 healed (The Rot's toad beaten)
GEO = WP.Geometry(W, H, TPH, LAYERS)
HP = GEO.padded_h               # painted with the padding rows (never on screen)
TCELL = 8                       # the terrain table's cells
TW, TH = W // TCELL, H // TCELL
SEED = 0xEC3
AMB_SEED = 0xA4B                # the ambient extras' own stream (glints, gnats, the healed pond's lilies)

# The ambient phases (M8): what moves from one phase to the next, the same
# step for every group of things a quarter of a cycle apart (phase - group).
SWAY = (0, 1, 0, -1)            # a reed's top, a ripple, a band of the swell: px sideways
BOB = (0, 0, 1, 1)              # a lily pad: px down
LAP = (0, 14, 0, -14)           # the waterline's glint: its noise raised (longer) or lowered
GNAT_LOOPS = (((0, 0), (1, -1), (2, 0), (1, 1)), ((0, 0), (-1, 1), (-2, 0), (-1, -1)),
              ((0, 0), (1, 1), (0, 2), (-1, 1)), ((0, 0), (0, -1), (1, -1), (1, 0)),
              ((0, 0), (2, -1), (0, -1), (-1, 0)))
GNAT_DRIFT = ((0, 0), (1, 0), (1, 1), (0, 1))     # a swarm as a whole

(SOOT, UMBER, PEAT, BARK, MUD, BONE, DEEPMOSS, BOG, MOSS, LICHEN, OCHRE, FOG, SLATE, STONE,
 RUST, WATER) = range(16)
GLOOM = (SOOT, UMBER, DEEPMOSS, BOG, SLATE)        # where a gnat shows lit (elsewhere dark)
HEALED_LILIES = ("F1", "F2", "F1", "F2", "P1", "P2")   # the healed pond's (tileset.py's LILIES; F in flower)

LEGEND = {
    ".": "grass", "s": "sand", "d": "dirt", "m": "mud", "~": "shallow water", "w": "deep water",
    "r": "reeds", "T": "forest", "%": "bramble", "B": "bank", "H": "hut",
}
GRASSY = set(".TBH")            # the grass layer covers these
WATERY = set("~wr")
SOLID_CELLS = set("T%BH")
# The ground under everything (grass goes over it): 0 water, 1 mud, 2 dirt, 3 sand.
GROUND = {"~": 0, "w": 0, "r": 0, "m": 1, "%": 1, "d": 2, ".": 2, "T": 2, "B": 2, "H": 2, "s": 3}

OPEN, SHALLOW, DEEP, SOLID = range(4)          # terrain classes (WorldData.h)
# The squire's body round his feet, [x0, x1) x [y0, y1) (Player.h's sprite,
# 16 x 21 px), and the share of it the willows may hide: the ground behind
# a tree where they would hide more is solid (Painter.hide).
BODY = (-7, 9, -20, 1)
HIDE = 0.5
NEST_KINDS = ("raccoon", "turtle", "beaver", "chameleon")
MUSH_KINDS = ("A2", "A3", "B2", "B3")          # red small, red small, white big, white big

# One step darker, colour by colour (shadows, The Rot): browns down the
# browns, greens down the mosses, greys down the greys, water to soot.
DARKER = (SOOT, SOOT, UMBER, PEAT, BARK, MUD, SOOT, DEEPMOSS, BOG, MOSS, BARK, STONE, UMBER,
          SLATE, UMBER, SOOT)
# Dead and dry (The Rot's reeds, plants and grass).
DEAD = (SOOT, UMBER, PEAT, BARK, MUD, BONE, UMBER, PEAT, BARK, MUD, BARK, STONE, SLATE,
        STONE, UMBER, WATER)
# A dead willow: the leaves gone grey, the wood darker.
DEAD_TREE = (SOOT, SOOT, UMBER, PEAT, BARK, MUD, SOOT, UMBER, SLATE, STONE, PEAT, STONE, UMBER,
             SLATE, UMBER, WATER)
# Dark leaves (bramble undergrowth).
DARK_LEAF = (SOOT, UMBER, UMBER, PEAT, BARK, MUD, SOOT, DEEPMOSS, DEEPMOSS, BOG, PEAT, STONE,
             SLATE, STONE, RUST, WATER)
# Wood gone black (bramble stems).
BLACK_WOOD = (SOOT, SOOT, SOOT, UMBER, PEAT, BARK, SOOT, DEEPMOSS, BOG, MOSS, UMBER, SLATE,
              SOOT, SLATE, RUST, WATER)


class WorldError(ValueError):
    pass


# --- masks: PIL 'L' images, 0 or 255 -------------------------------------------------

def blank(v=0):
    return Image.new("L", (W, HP), v)


def AND(a, b):
    return ImageChops.multiply(a, b)


def OR(a, b):
    return ImageChops.lighter(a, b)


def NOT(a):
    return ImageChops.invert(a)


def MINUS(a, b):
    return ImageChops.subtract(a, b)


def thresh(img, t):
    return img.point(lambda v: 255 if v >= t else 0)


def erode(m, n=1):
    for _ in range(n):
        m = m.filter(ImageFilter.MinFilter(3))
    return m


def dilate(m, n=1):
    for _ in range(n):
        m = m.filter(ImageFilter.MaxFilter(3))
    return m


def shift(m, dx, dy):
    """The mask moved by (dx, dy), nothing wrapping round."""
    out = Image.new("L", m.size, 0)
    out.paste(m, (dx, dy))
    return out


def erode4(m):
    """A cross erosion: a pixel stays if its four neighbours are in."""
    out = m
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        out = AND(out, shift(m, dx, dy))
    return out


def dilate4(m):
    out = m
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        out = OR(out, shift(m, dx, dy))
    return out


def edge_in(m):
    """The mask's own outermost pixels."""
    return MINUS(m, erode4(m))


def rect_mask(x0, y0, x1, y1):
    m = blank()
    m.paste(255, (max(0, x0), max(0, y0), min(W, x1), min(HP, y1)))
    return m


class Rng:
    """random.Random plus image helpers: every draw from the one seed, in order."""
    def __init__(self, seed):
        self.r = random.Random(seed)

    def __getattr__(self, k):
        return getattr(self.r, k)

    def noise(self, scale, amp):
        """Smooth noise about 128, features about `scale` px, roughly +-amp."""
        sw, sh = W // scale + 4, HP // scale + 4
        small = Image.frombytes("L", (sw, sh), self.r.randbytes(sw * sh))
        big = small.resize((sw * scale, sh * scale), Image.BICUBIC).crop((scale, scale, scale + W, scale + HP))
        k = amp / 64.0
        return big.point(lambda v: max(0, min(255, int(128 + (v - 128) * k))))

    def speckle(self, density):
        """A mask with about `density` of its pixels set."""
        t = int(density * 256)
        return Image.frombytes("L", (W, HP), self.r.randbytes(W * HP)).point(lambda v: 255 if v < t else 0)

    def clumps(self, density, scale, t=140, amp=60):
        """Speckle gathered in patches."""
        return AND(self.speckle(density), thresh(self.noise(scale, amp), t))

    def marks(self, density, grow=0.5):
        """Little clusters, the way pixel art stipples: seeds at `density`,
        each grown a pixel right and/or down at random (1-4 px marks)."""
        s = self.speckle(density)
        m = OR(s, AND(shift(s, 1, 0), self.speckle(grow)))
        return OR(m, AND(shift(s, 0, 1), self.speckle(grow * 0.6)))

    def pebbles(self, density):
        """(lit, shade) masks: small stones lit from the top left, a 1-3 px
        mark with its shadow under its right."""
        lit = self.marks(density, 0.5)
        return lit, MINUS(shift(lit, 1, 1), lit)

    def strokes(self, density, length=3):
        """Short horizontal strokes (glints, ripples)."""
        s = self.speckle(density)
        m = s
        for i in range(1, length):
            m = OR(m, AND(shift(s, i, 0), self.speckle(0.8 if i < length - 1 else 0.5)))
        return m

    def blobs(self, scale, t, amp=70):
        """Soft solid patches about `scale` px (a noise field cut at t)."""
        return thresh(self.noise(scale, amp), t)


def bayer_image():
    """The 4x4 Bayer matrix tiled over the world: 16 v + 8 for v = 0..15."""
    B = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
    row = [bytes(B[y][x & 3] * 16 + 8 for x in range(W)) for y in range(4)]
    return Image.frombytes("L", (W, HP), b"".join(row[y & 3] for y in range(HP)))


def dither(level, bay):
    """Ordered dither: on where the Bayer value is under `level` (int or image)."""
    if isinstance(level, int):
        return bay.point(lambda v: 255 if v < level else 0)
    return thresh(ImageChops.subtract(level, bay, 1.0, 0), 1)


def hash4(x, y, salt=0):
    """0..3, scattered over (x, y) (no draws from any stream)."""
    h = (x * 374761393 + y * 668265263 + salt * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h >> 16) & 3


def group_map(cw, ch, salt=0, rows=False):
    """An 'L' image of groups 0..3: cells of cw x ch px scattered by hash4,
    or (rows) bands of ch rows numbered down the world, 0 1 2 3 0 1 ..."""
    gw, gh = W // cw + 1, HP // ch + 1
    if rows:
        small = bytes(((y & 3) for y in range(gh) for _ in range(gw)))
    else:
        small = bytes(hash4(x, y, salt) for y in range(gh) for x in range(gw))
    big = Image.frombytes("L", (gw, gh), small).resize((gw * cw, gh * ch), Image.NEAREST)
    return big.crop((0, 0, W, HP))


def group_masks(gmap):
    return [gmap.point(lambda v, k=k: 255 if v == k else 0) for k in range(4)]


def leaned(part, s, frac=0.45):
    """(part, dx): the part with its rows above `frac` of its height moved s
    px sideways (a reed's top bending), one px wider; dx moves its left edge
    so that its foot stays put."""
    if not s:
        return part, 0
    cut = max(1, int(part.h * frac))
    idx = Image.new("L", (part.w + 1, part.h), TS.HOLE)
    mask = Image.new("L", (part.w + 1, part.h), 0)
    tx, bx = (1, 0) if s > 0 else (0, 1)
    for (y0, y1), x in (((0, cut), tx), ((cut, part.h), bx)):
        idx.paste(part.idx.crop((0, y0, part.w, y1)), (x, y0))
        mask.paste(part.mask.crop((0, y0, part.w, y1)), (x, y0))
    return TS.Part(idx, mask, part.name), -bx


# --- inputs -----------------------------------------------------------------------------

def load_map(path=MAP):
    rows = []
    for n, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        if line.startswith("#") or not line.strip():
            continue
        if len(line) != MW:
            raise WorldError("%s:%d: %d cells, not %d" % (path, n, len(line), MW))
        bad = [c for c in line if c not in LEGEND]
        if bad:
            raise WorldError("%s:%d: %r is not in the legend" % (path, n, bad[0]))
        rows.append(list(line))
    if len(rows) != MH:
        raise WorldError("%s: %d rows, not %d" % (path, len(rows), MH))
    return rows


def load_objects(path=OBJECTS):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def cell(g, x, y):
    return g[min(MH - 1, max(0, y))][min(MW - 1, max(0, x))]


def cell_field(g, pred, resample=Image.BICUBIC):
    """255 where pred(cell) else 0, interpolated between the cell centres
    (16i + 8, 16j + 8) to the world's pixels."""
    src = Image.new("L", (MW + 2, MH + 2))
    px = src.load()
    for y in range(MH + 2):
        for x in range(MW + 2):
            px[x, y] = 255 if pred(cell(g, x - 1, y - 1)) else 0
    big = src.resize(((MW + 2) * CELL, (MH + 2) * CELL), resample)
    return big.crop((CELL, CELL, CELL + W, CELL + HP))


def cell_mask(g, pred):
    """255 over exactly the cells where pred(cell) (the padding: the last row's)."""
    src = Image.new("L", (MW, MH + 1))
    px = src.load()
    for y in range(MH + 1):
        for x in range(MW):
            px[x, y] = 255 if pred(cell(g, x, y)) else 0
    return src.resize((W, (MH + 1) * CELL), Image.NEAREST).crop((0, 0, W, HP))


def soft(field, noise, t):
    """A field roughened by noise and cut at t."""
    return thresh(ImageChops.add(field, noise, 1.0, -128), t)


# --- little sprites drawn here (no tileset art for them) -------------------------------

STUMPS = (
    # a beaver's work: gnawed to a point, chips round it
    ["....0.......",
     "...040......",
     "...0430.....",
     "..04320.....",
     "..043210....",
     ".0432210....",
     ".03222100...",
     ".032221310..",
     ".032211210..",
     "0032211210..",
     "0332211211.0",
     ".000110110..",
     "............"],
    # cut flat, the rings showing
    ["..0000000...",
     ".044444440..",
     "04433333440.",
     "04434443440.",
     "04433333440.",
     ".024444420..",
     ".021222120..",
     ".032212210..",
     ".022212210..",
     "0322122210..",
     "021211212100",
     "00.000000.0.",
     "............"],
)


# Junk and remains round the nests (palette digits, '.' clear).
DEBRIS = {
    "can": ["0000.",          # a tin gone to rust, on its side
            "0EE10",
            "0E5E1",
            "0EE10",
            "0000."],
    "can2": [".000.",         # standing, its lid bent
             "05E10",
             "0EEE0",
             "0E110",
             ".000."],
    "bone": ["00..00",
             "055550",
             "00..00"],
    "rag": [".000..",         # a scrap of the squire's cloth
            "0EE10.",
            "0E1EE0",
            ".00000"],
    "shell": [".000.",        # half an egg
              "05550",
              "0D550",
              ".000."],
    "shell2": ["0.0.0",       # the top, broken
               "05550",
               ".000."],
}


def char_part(rows):
    """A Part from rows of hex digits ('.' clear)."""
    w, h = len(rows[0]), len(rows)
    idx = Image.new("L", (w, h), TS.HOLE)
    mask = Image.new("L", (w, h), 0)
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c != ".":
                idx.putpixel((x, y), int(c, 16))
                mask.putpixel((x, y), 255)
    return TS.Part(idx, mask)


# --- the painter ------------------------------------------------------------------------

class Painter:
    """One paint of the world: ambient phase `phase` (0..TPH-1) of layer 0
    (the swamp) or, healed, layer 1 (The Rot gone: no dead willows, reeds
    or grass, no grime, no gnats; lilies on its pond). Every paint makes the
    same draws from the one stream in the same order (the ambient extras
    have their own), so the layout, the terrain and everything that does
    not move are the same in all of them; the phase only moves what sways,
    bobs, glints and buzzes."""
    def __init__(self, grid, objects, ts=None, phase=0, healed=False):
        self.g = [row[:] for row in grid]
        self.obj = objects
        self.ts = ts or TS.Tileset()
        self.phase, self.healed = phase, healed
        self.rng = Rng(SEED)
        self.amb = Rng(AMB_SEED)                # (glints, the healed pond's lilies: off the main stream)
        self.swarms = []                        # the gnats' swarms (gnats())
        self.bay = bayer_image()
        self.cv = blank(WATER)                  # the world, palette indices
        self.repairs = []
        self.solid = cell_mask(self.g, lambda c: c in SOLID_CELLS)
        self.props = []                         # (foot y, foot x, n, draw, owner)
        self.willows = []                       # (key, flip, foot x, foot y, part, ax, ay), owner id = index + 1
        self.owner = Image.new("I", (W, HP), 0)     # which willow each pixel shows (draw_props), 0 none
        self._owner = None                      # what put() stamps there (None: not drawing props)

    def fill(self, colour, mask):
        self.cv.paste(colour, (0, 0), mask)

    def remap(self, table, mask):
        lut = list(table) + list(range(16, 256))
        self.cv.paste(self.cv.point(lut), (0, 0), mask)

    def put(self, part, x, y):
        """A Part with its top-left at (x, y)."""
        self.cv.paste(part.idx, (x, y), part.mask)
        if self._owner is not None:
            self.owner.paste(self._owner, (x, y), part.mask)

    def put_solid(self, x0, y0, x1, y1):
        self.solid.paste(255, (max(0, x0), max(0, y0), min(W, x1), min(HP, y1)))

    def hide(self):
        """Solid ground wherever the willows would hide HIDE or more of the
        squire's body (BODY, round his feet): the trees whose base line is
        below his feet are drawn over him, so their pixels together (a
        union, from the parts as placed: the same in every phase and layer)
        over his box, by feet row from the bottom up, taking each tree in
        as the row passes above its base. The fronds hang nearly to the
        ground, so this is most of the ground behind a tree: he can only
        dip into a canopy's edges, never vanish behind it (the author:
        going so far behind the trees made no sense for the game)."""
        import numpy as np
        from PIL import Image
        bx0, bx1, by0, by1 = BODY
        need = HIDE * (bx1 - bx0) * (by1 - by0)
        front = np.zeros((HP, W), dtype=np.uint8)            # the trees in front of the row's feet
        solid = np.zeros((HP, W), dtype=bool)
        trees = sorted(self.willows, key=lambda t: -t[3])      # by base line, lowest first
        k, top = 0, HP
        for fy in range(HP - 1, -1, -1):
            while k < len(trees) and trees[k][3] > fy:
                _, _, x, y, p, ax, ay = trees[k]
                k += 1
                m = np.asarray(p.mask.convert("L")) > 0
                x0, y0 = x - ax, y - ay
                sx0, sy0 = max(0, x0), max(0, y0)
                sx1, sy1 = min(W, x0 + p.w), min(HP, y0 + p.h)
                if sx0 < sx1 and sy0 < sy1:
                    front[sy0:sy1, sx0:sx1] |= m[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0]
                    top = min(top, sy0)
            r0, r1 = max(0, fy + by0), min(HP, fy + by1)
            if not k or r1 <= top or r0 >= r1:
                continue
            rows = front[r0:r1].sum(axis=0, dtype=np.int32)
            c = np.concatenate(([0], rows.cumsum()))
            fx = np.arange(W)
            cov = c[np.minimum(W, fx + bx1)] - c[np.maximum(0, fx + bx0)]
            solid[fy] = cov >= need
        self.solid.paste(255, (0, 0, W, HP), Image.fromarray(solid.astype(np.uint8) * 255))

    def wet_at(self, x, y):
        return 0 <= x < W and 0 <= y < HP and self.wet.getpixel((x, y)) > 0

    def phased(self, m, groups, table=None):
        """Mask m with each group's part (groups: group_masks()) moved
        table[(phase - group) & 3] px sideways (SWAY): the ambient motion."""
        table = table or SWAY
        out = blank()
        for k, g in enumerate(groups):
            out = OR(out, shift(AND(m, g), table[(self.phase - k) & 3], 0))
        return out

    # 1. the ground -----------------------------------------------------------------

    def ground(self):
        g, r = self.g, self.rng
        lvl = lambda L: (lambda c: GROUND[c] >= L)
        rough = ImageChops.add(r.noise(20, 34), r.noise(7, 14), 1.0, -128)
        self.mud = soft(cell_field(g, lvl(1)), rough, 100)
        self.dirt = AND(soft(cell_field(g, lvl(2)), rough, 128), self.mud)
        self.sand = AND(soft(cell_field(g, lvl(3)), ImageChops.add(rough, r.noise(5, 10), 1.0, -128), 140),
                        self.dirt)
        # mud: umber, wet and dark; drier peat smears, soot pits, wet glints
        self.fill(UMBER, self.mud)
        smear = AND(self.mud, r.blobs(8, 162))
        self.fill(PEAT, smear)
        self.fill(UMBER, AND(smear, r.marks(0.04)))
        self.fill(SOOT, AND(self.mud, r.marks(0.012, 0.4)))
        self.fill(SLATE, AND(erode(self.mud, 1), r.strokes(0.0035, 3)))
        # dirt: peat; lighter dry patches, grit and pebbles lit from the top left, cracks
        self.fill(PEAT, self.dirt)
        self.fill(BARK, AND(self.dirt, AND(r.blobs(10, 172), r.speckle(0.5))))
        lit, shade = r.pebbles(0.022)
        self.fill(BARK, AND(self.dirt, lit))
        self.fill(UMBER, AND(self.dirt, shade))
        lit, shade = r.pebbles(0.0016)
        self.fill(STONE, AND(erode(self.dirt, 2), lit))
        self.fill(SLATE, AND(erode(self.dirt, 2), shade))
        self.fill(UMBER, AND(self.dirt, r.marks(0.006, 0.8)))
        self.fill(UMBER, AND(edge_in(self.dirt), r.speckle(0.8)))     # its lip on the mud
        # sand: pale but dirty: damp patches, grit, a bone glint, a bark rim dithered in
        self.fill(MUD, self.sand)
        self.fill(BARK, AND(self.sand, AND(r.blobs(9, 175), r.speckle(0.45))))
        self.fill(BARK, AND(self.sand, r.marks(0.018)))
        self.fill(BONE, AND(self.sand, r.marks(0.006)))
        e1 = edge_in(self.sand)
        self.fill(BARK, e1)
        self.fill(BARK, AND(edge_in(MINUS(self.sand, e1)), dither(128, self.bay)))
        self.land = self.mud                    # grass adds to it below

    # 2. grass -----------------------------------------------------------------------

    def repair_pinches(self):
        g = self.g
        grassy = lambda x, y: cell(g, x, y) in GRASSY
        changed = True
        while changed:
            changed = False
            for y in range(-1, MH):
                for x in range(-1, MW):
                    a, b, c, d = grassy(x, y), grassy(x + 1, y), grassy(x, y + 1), grassy(x + 1, y + 1)
                    if a == d and b == c and a != b:
                        cand = [(x + 1, y), (x, y + 1)] if a else [(x, y), (x + 1, y + 1)]
                        cx, cy = [(cx, cy) for cx, cy in cand if 0 <= cx < MW and 0 <= cy < MH][0]
                        self.repairs.append((cx, cy, g[cy][cx]))
                        g[cy][cx] = "."
                        changed = True

    def grass(self):
        self.repair_pinches()
        g, ts, r = self.g, self.ts, self.rng
        grassy = lambda x, y: cell(g, x, y) in GRASSY
        gm = blank()
        for ty in range(-1, MH + 1):
            for tx in range(-1, MW):
                case = (TS.TL * grassy(tx, ty) | TS.TR * grassy(tx + 1, ty) |
                        TS.BL * grassy(tx, ty + 1) | TS.BR * grassy(tx + 1, ty + 1))
                if not case:
                    continue
                pieces = ts.ground[case]
                p = pieces[(tx * 7919 + ty * 104729 + SEED) % len(pieces)]
                x, y = tx * CELL + CELL // 2, ty * CELL + CELL // 2
                self.put(p, x, y)
                gm.paste(255, (x, y), p.mask)
        self.grassm = gm
        self.land = OR(self.land, gm)
        inner = erode(gm, 2)
        # tufts: the tileset's grass-speckle tiles (cols 12-17, rows 12-15), keyed
        tufts = [ts.tile(x * 16, y * 16).keyed([MOSS]) for y in range(12, 16) for x in range(12, 18)]
        tufts = [t for t in tufts if t.mask.getbbox()]
        for _ in range(W * H // 520):
            x, y = r.randrange(W), r.randrange(HP)
            if inner.getpixel((x, y)):
                self.put(r.choice(tufts), x - 8, y - 8)
        # blades: a dark stroke two high with a lit tip, gathered in patches
        s = AND(inner, r.clumps(0.02, 11, 135))
        self.fill(BOG, OR(s, shift(s, 0, -1)))
        self.fill(LICHEN, AND(shift(s, 0, -2), inner))
        # a few lit flecks
        self.fill(LICHEN, AND(inner, r.marks(0.0025, 0.3)))
        # the forest floor: darker, under the canopy
        forest = AND(soft(cell_field(g, lambda c: c == "T"), r.noise(10, 30), 120), gm)
        self.remap([0, 1, 2, 3, 4, 5, 6, DEEPMOSS, BOG, MOSS, 10, 11, 12, 13, 14, 15], forest)
        self.fill(DEEPMOSS, AND(forest, r.marks(0.04)))
        self.forest = forest

    # 3. water -------------------------------------------------------------------------

    def water(self):
        g, r = self.g, self.rng
        wet = NOT(self.land)
        self.wet = wet
        depth = ImageChops.add(cell_field(g, lambda c: c == "w"), r.noise(18, 40), 1.0, -128)
        self.deepm = AND(thresh(depth, 128), wet)
        shallow = MINUS(wet, self.deepm)
        # the shallows show their bottom: silt and pebbles near the banks
        nearbank = AND(shallow, MINUS(dilate(self.land, 10), dilate(self.land, 2)))
        self.fill(PEAT, AND(nearbank, r.clumps(0.05, 6, 130)))
        self.fill(BOG, AND(shallow, r.clumps(0.02, 9, 150)))
        # the drop-off: a dark broken line where the shallows end
        lip = AND(edge_in(self.deepm), thresh(r.noise(6, 80), 100))
        self.fill(SOOT, lip)
        self.fill(DEEPMOSS, AND(edge_in(MINUS(self.deepm, edge_in(self.deepm))), r.speckle(0.5)))
        # deep water's chop: dark troughs in short strokes, thicker further in;
        # each band of 3 rows leans a pixel in turn, a swell rolling down the pond
        swell = group_masks(group_map(W, 3, rows=True))
        for t, dens in ((128, 0.03), (150, 0.05), (175, 0.07)):
            trough = AND(AND(self.deepm, thresh(depth, t)), r.clumps(dens, 9, 120))
            trough = OR(trough, shift(trough, 1, 0))
            self.fill(SOOT, self.phased(OR(trough, AND(shift(trough, 1, 0), r.speckle(0.6))), swell))
        # ripples in the shallows, a pale stroke each, swaying one way and the other in turn
        seeds = AND(shallow, r.clumps(0.008, 14, 160, 80))
        sp = r.speckle(0.5)
        rip = blank()
        for k, g in enumerate(group_masks(group_map(6, 3, 1))):
            m = AND(seeds, g)
            m = OR(m, shift(m, 1, 0))
            m = OR(m, AND(shift(m, 1, 0), sp))
            rip = OR(rip, shift(m, SWAY[(self.phase - k) & 3], 0))
        self.fill(FOG, AND(rip, erode(wet, 3)))
        # the waterline: a glint where the bank above meets the water, lapping
        # (each stretch of it longer, then shorter, in turn)
        lap = group_map(16, 8, 2).point(lambda k: 128 + LAP[(self.phase - k) & 3])
        below_land = AND(wet, shift(self.land, 0, 1))
        side_land = AND(wet, OR(shift(self.land, 1, 0), shift(self.land, -1, 0)))
        self.fill(FOG, AND(below_land, thresh(ImageChops.add(r.noise(9, 70), lap, 1.0, -128), 105)))
        self.fill(FOG, AND(side_land, AND(r.speckle(0.4), thresh(ImageChops.add(r.noise(9, 70), lap, 1.0, -128),
                                                                   130))))
        self.glints()

    def glints(self):
        """Sun on the deep water: a pale fleck here and there, each showing
        in one phase of the four (their own draws)."""
        a = self.amb
        at = group_masks(group_map(4, 2, 3))[self.phase]
        self.fill(FOG, AND(AND(erode(self.deepm, 3), a.strokes(0.0016, 2)), at))

    # 4. banks and the hut -----------------------------------------------------------

    def banks(self):
        g, ts = self.g, self.ts
        nests = [(k, x, y) for k, x, y in self.obj.get("nests", [])]
        for y in range(MH):
            x = 0
            while x < MW:
                if g[y][x] not in "BH":
                    x += 1
                    continue
                x0 = x
                while x < MW and g[y][x] in "BH":
                    x += 1
                self.bank_run(y, x0, x - 1, nests)

    def bank_run(self, y, x0, x1, nests):
        ts, g = self.ts, self.g
        n = x1 - x0 + 1
        hut = [cx for cx in range(x0, x1 + 1) if g[y][cx] == "H"]
        kinds = {}
        if hut:
            # the hut: the door in the middle, windows beside it, burrow holes out to the ends
            m = hut[len(hut) // 2 - 1] if len(hut) > 1 else hut[0]
            for cx in hut:
                d = cx - m
                kinds[cx] = ("door", 0) if d == 0 else ("door", 1) if d == 1 else \
                    ("window", (d + 4) % 4) if d in (-1, 2) else ("hole", (d + 8) % 4)
        for cx in range(x0, x1 + 1):
            if cx in kinds:
                continue
            fx, fy = cx * CELL + 8, y * CELL + CELL
            den = any(k == "raccoon" and abs(nx - fx) < 12 and 0 <= ny - fy < 40 for k, nx, ny in nests)
            h = (cx * 31 + y * 17) % 7
            kinds[cx] = ("hole", 0 if h & 1 else 3) if den else (("roots", h) if h < 3 else ("plain", h))
        for cx in range(x0, x1 + 1):
            kind, v = kinds[cx]
            top, face = TS.BANK_FACES[kind][v % len(TS.BANK_FACES[kind])]
            self.put(ts.tile(*top), cx * CELL, (y - 1) * CELL)
            self.put(ts.tile(*face), cx * CELL, y * CELL)
        self.put(ts.tile(*TS.BANK_CAP_L), (x0 - 1) * CELL, y * CELL)
        self.put(ts.tile(*TS.BANK_CAP_R), (x1 + 1) * CELL, y * CELL)
        # its shadow on the ground below
        sh = rect_mask((x0 - 1) * CELL + 4, (y + 1) * CELL, (x1 + 2) * CELL - 4, (y + 1) * CELL + 3)
        self.remap(DARKER, AND(sh, dither(136, self.bay)))
        self.put_solid(x0 * CELL - 6, y * CELL, (x1 + 1) * CELL + 6, (y + 1) * CELL)

    # 5. worn in ----------------------------------------------------------------------------

    def worn(self):
        r = self.rng
        land, wet = self.land, self.wet
        # mud splashed up from the water onto the bank: a wet lip, then flecks
        near = MINUS(dilate(wet, 2), wet)
        far = MINUS(dilate(wet, 7), dilate(wet, 2))
        lip = AND(AND(near, land), r.blobs(6, 115))
        self.fill(UMBER, lip)
        self.fill(PEAT, AND(lip, r.marks(0.08)))
        self.fill(PEAT, AND(AND(far, self.grassm), r.marks(0.035)))
        self.fill(UMBER, AND(AND(far, self.grassm), r.marks(0.01, 0.3)))
        # trodden trails: the grass worn through to bare earth down the
        # middle, ragged and flattened at the sides
        for pts in self.obj.get("trails", []):
            pts = [tuple(p) for p in pts]
            m = Image.new("L", (W, HP), 0)
            d = ImageDraw.Draw(m)
            d.line(pts, fill=255, width=12, joint="curve")
            for p in pts:
                d.ellipse((p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6), fill=255)
            core = Image.new("L", (W, HP), 0)
            ImageDraw.Draw(core).line(pts, fill=255, width=6, joint="curve")
            bare = AND(OR(AND(core, r.blobs(4, 90)), AND(m, r.blobs(4, 170))), self.grassm)
            self.fill(PEAT, bare)
            lit, shade = r.pebbles(0.03)
            self.fill(BARK, AND(bare, lit))
            self.fill(UMBER, AND(bare, shade))
            self.fill(UMBER, AND(edge_in(bare), r.speckle(0.6)))
            side = AND(MINUS(dilate(m, 2), bare), self.grassm)
            self.fill(BOG, AND(side, r.marks(0.12)))
            self.fill(PEAT, AND(side, r.marks(0.03)))
        # the nests: a scuffed ring of bare ground, and their debris
        for kind, x, y in self.obj.get("nests", []):
            self.nest_ground(kind, x, y)

    def nest_ground(self, kind, x, y):
        r, ts = self.rng, self.ts
        rad = 22 if kind == "beaver" else 18
        m = Image.new("L", (W, HP), 0)
        ImageDraw.Draw(m).ellipse((x - rad, y - rad * 2 // 3, x + rad, y + rad * 2 // 3), fill=255)
        on = AND(AND(m, r.blobs(5, 112)), NOT(self.wet))
        sandy = AND(on, self.sand)              # scuffed sand is darker sand, not earth
        earth = MINUS(on, sandy)
        self.fill(PEAT, earth)
        lit, shade = r.pebbles(0.04)
        self.fill(BARK, AND(earth, lit))
        self.fill(UMBER, AND(earth, shade))
        self.fill(UMBER, AND(edge_in(earth), r.speckle(0.7)))
        self.fill(BARK, AND(sandy, r.speckle(0.45)))
        self.fill(PEAT, AND(sandy, r.marks(0.03)))
        self.fill(BOG, AND(AND(MINUS(dilate(m, 3), m), self.grassm), r.marks(0.15)))
        sticks = [ts.part(b, k).keyed([FOG, WATER], rows=4) for k, b in TS.STICKS.items()]
        bits = []
        if kind == "raccoon":       # trash: bones, a rag, cans gone to rust
            bits = [(BONE, 0.006), (RUST, 0.003), (STONE, 0.004), (SLATE, 0.004)]
        elif kind == "turtle":      # broken shells
            bits = [(BONE, 0.012), (MUD, 0.006)]
        elif kind == "chameleon":   # leaf litter
            bits = [(LICHEN, 0.01), (BOG, 0.012), (OCHRE, 0.003)]
        wide = Image.new("L", (W, HP), 0)
        ImageDraw.Draw(wide).ellipse((x - rad - 10, y - rad, x + rad + 10, y + rad), fill=255)
        for c, dens in bits:
            self.fill(c, AND(AND(wide, NOT(self.wet)), r.marks(dens * 0.6, 0.6)))
        for _ in range(3 if kind != "beaver" else 0):
            s = r.choice(sticks)
            sx, sy = x + r.randrange(-rad - 6, rad + 6), y + r.randrange(-rad // 2, rad // 2 + 4)
            if not self.wet_at(sx, sy):
                self.prop(s, sx, sy, r.random() < 0.5)
        junk = {"raccoon": ("can", "can2", "bone", "rag", "can", "bone"),
                "turtle": ("shell", "shell2", "shell", "shell2", "shell", "shell2"),
                "chameleon": ("bone",)}.get(kind, ())
        for name in junk:
            p = char_part(DEBRIS[name])
            for _ in range(8):                  # somewhere dry, round the ring
                sx, sy = x + r.randrange(-rad - 8, rad + 9), y + r.randrange(-rad // 2 - 2, rad // 2 + 6)
                if not self.wet_at(sx, sy) and not self.solid.getpixel((sx, sy)):
                    self.prop(p, sx, sy, r.random() < 0.5)
                    break

    # 6. props ---------------------------------------------------------------------------

    def add(self, foot_y, foot_x, draw, owner=0):
        self.props.append((foot_y, foot_x, len(self.props), draw, owner))

    def prop(self, part, x, y, flip=False, sway=False, bob=False):
        """A part standing with its foot (bottom middle) at (x, y). Ambient:
        sway, its top leans with the phase (a reed in the wind: the lean
        rolls across the swamp in a wave); bob, it dips a pixel (a lily pad)."""
        p = part.flipped() if flip else part
        x0, y0 = x - p.w // 2, y - p.h + 1
        if sway:
            p, dx = leaned(p, SWAY[(self.phase - ((x + y // 2) // 24)) & 3])
            x0 += dx
        if bob:
            y0 += BOB[(self.phase - hash4(x, y, 4)) & 3]
        self.add(y, x, lambda: self.put(p, x0, y0))

    def rotting(self, x, y):
        """(x, y) is in The Rot and this paint is not healed: its willows,
        reeds and plants dead."""
        x0, y0, x1, y1 = self.rot_rect()
        return not self.healed and x0 <= x < x1 and y0 <= y < y1

    def put_decal(self, part, x, y, flip=False):
        """A flat part centred on (x, y), on the ground under every prop."""
        p = part.flipped() if flip else part
        self.put(p, x - p.w // 2, y - p.h // 2)

    def willow(self, key, x, y, flip=False, dead=False):
        p, ax, ay, (f0, f1, f2, f3) = self.ts.willow(key)
        if key in ("T4", "T5") and not self.wet_at(x, y):
            p = p.keyed([FOG, WATER], rows=8)          # standing on land: no water at its roots
        if dead:
            p = p.recoloured(DEAD_TREE)
        if flip:
            p = p.flipped()
            ax, f0, f2 = p.w - 1 - ax, p.w - f2, p.w - f0
        self.willows.append((key, flip, x, y, p, ax, ay))
        self.add(y, x, lambda: self.put(p, x - ax, y - ay), len(self.willows))
        self.put_solid(x - ax + f0, y - ay + f1, x - ax + f2, y - ay + f3)

    def place_props(self):
        o, ts, r = self.obj, self.ts, self.rng
        inrot = self.rotting
        for spec in o.get("willows", []):
            k, x, y = spec[:3]
            self.willow(k, x, y, "flip" in spec[3:], dead=inrot(x, y))
        rocks = ts.props(TS.ROCKS)
        for k, x, y, *f in o.get("rocks", []):
            p = rocks[k]
            self.prop(p, x, y, "flip" in f)
            if p.w > 8:
                self.put_solid(x - p.w // 2 + 1, y - p.h // 2, x + p.w // 2 - 1, y + 1)
        sticks = ts.props(TS.STICKS)
        for k, x, y, *f in o.get("sticks", []):
            p = sticks[k] if self.wet_at(x, y) else sticks[k].keyed([FOG, WATER], rows=4)
            self.prop(p, x, y, "flip" in f)
        lilies = ts.props(TS.LILIES)
        for k, x, y, *f in o.get("lilies", []):
            self.prop(lilies[k], x, y, "flip" in f, bob=True)
        # reeds and cattails: the water clumps in water, the same clumps dry on land
        wet_reeds, dry_reeds = ts.props(TS.REEDS), ts.props(TS.LAND_REEDS)
        wet_cats, dry_cats = ts.props(TS.CATTAILS), ts.props(TS.LAND_CATTAILS)
        for key, wet_set, dry_set in (("reeds", wet_reeds, dry_reeds), ("cattails", wet_cats, dry_cats)):
            dry_keys = sorted(dry_set)
            for k, x, y, *f in o.get(key, []):
                p = wet_set[k] if self.wet_at(x, y) else dry_set[dry_keys[sorted(wet_set).index(k)]]
                self.prop(p.recoloured(DEAD) if inrot(x, y) else p, x, y, "flip" in f, sway=True)
        decals = ts.props(TS.PEBBLES)
        for k, x, y, *f in o.get("decals", []):
            self.put_decal(decals[k], x, y, "flip" in f)
        plants = ts.props(TS.PLANTS)
        for k, x, y, *f in o.get("plants", []):
            p = plants[k].recoloured(DEAD) if inrot(x, y) else plants[k]
            self.prop(p, x, y, "flip" in f)
        mush = ts.props({k: TS.MUSHROOMS[k] for k in TS.DECOR_MUSHROOMS})
        for k, x, y, *f in o.get("decor_mushrooms", []):
            self.prop(mush[k], x, y, "flip" in f)
        stumps = [char_part(s) for s in STUMPS]
        for i, (x, y) in enumerate(o.get("stumps", [])):
            p = stumps[0] if i % 3 != 2 else stumps[1]
            self.prop(p, x, y, i % 2 == 1)
            self.put_solid(x - 4, y - 4, x + 4, y + 1)
            chips = rect_mask(x - 10, y - 4, x + 10, y + 5)
            self.fill(MUD, AND(AND(chips, NOT(self.wet)), r.speckle(0.06)))
            self.fill(BARK, AND(AND(chips, NOT(self.wet)), r.speckle(0.08)))
        for k, x, y in o.get("nests", []):
            if k == "beaver":
                self.lodge(x, y)
        for x0, y0, x1, y1 in o.get("dams", []):
            self.dam(x0, y0, x1, y1)
        for x, y, n in o.get("rings", []):
            self.ring(x, y, n)
        self.scatter()

    def dam(self, x0, y0, x1, y1):
        """The beavers' dam: sticks and mud packed across the water, firm
        enough to walk on."""
        r, ts = self.rng, self.ts
        m = AND(rect_mask(x0, y0, x1, y1), r.blobs(4, 70))
        m = OR(m, rect_mask(x0 + 3, y0 + 3, x1 - 3, y1 - 3))
        self.fill(UMBER, m)
        self.fill(PEAT, AND(m, r.marks(0.12)))
        self.fill(SOOT, AND(edge_in(m), r.speckle(0.6)))
        sticks = ts.props(TS.STICKS)
        keys = sorted(sticks)
        for i, x in enumerate(range(x0 + 2, x1 - 2, 5)):
            for row in (y0 + 4, y1 - 1):
                p = sticks[r.choice(keys)].keyed([FOG, WATER], rows=4)
                self.prop(p, x + r.randrange(-2, 3), row - r.randrange(0, 3), r.random() < 0.5)
        for _ in range((x1 - x0) // 10):                # what spilled over, down in the water
            x, y = r.randrange(x0, x1), y1 + r.randrange(3, 10)
            self.prop(sticks[r.choice(keys)], x, y, r.random() < 0.5)
        self.wet = MINUS(self.wet, m)
        self.deepm = MINUS(self.deepm, m)
        self.land = OR(self.land, m)

    def ring(self, x, y, n):
        """A ring of brown mushrooms in the grass (a landmark)."""
        mush = self.ts.props({k: TS.MUSHROOMS[k] for k in TS.DECOR_MUSHROOMS})
        keys = sorted(mush)
        for i in range(n):
            a = i * 6.2832 / n
            mx, my = int(x + math.cos(a) * 16), int(y + math.sin(a) * 10)
            self.prop(mush[keys[i % len(keys)]], mx, my, i % 2 == 1)
        inner = Image.new("L", (W, HP), 0)
        ImageDraw.Draw(inner).ellipse((x - 13, y - 8, x + 13, y + 8), fill=255)
        self.fill(LICHEN, AND(AND(inner, self.grassm), self.rng.marks(0.05)))      # greener inside

    def lodge(self, x, y):
        """A beaver lodge's remains: a sodden heap of sticks in the shallows."""
        r, ts = self.rng, self.ts
        heap = Image.new("L", (W, HP), 0)
        ImageDraw.Draw(heap).ellipse((x - 20, y - 11, x + 20, y + 9), fill=255)
        heap = AND(heap, thresh(r.noise(5, 70), 100))
        self.fill(UMBER, heap)
        self.fill(PEAT, AND(heap, r.speckle(0.35)))
        self.fill(SOOT, AND(edge_in(heap), r.speckle(0.7)))
        sticks = ts.props(TS.STICKS)
        keys = sorted(sticks)
        for i in range(14):
            a = i / 14.0 * 6.283
            sx = int(x + math.cos(a) * r.uniform(4, 18))
            sy = int(y + math.sin(a) * r.uniform(2, 9)) + 4
            p = sticks[r.choice(keys)]
            p = p if self.wet_at(sx, sy) else p.keyed([FOG, WATER], rows=4)
            self.prop(p, sx, sy, r.random() < 0.5)

    def scatter(self):
        """Seeded crowds: willows in the forest, reed beds, brambles, the
        odd plant, reed and lily pad."""
        g, r, ts = self.g, self.rng, self.ts
        rot = self.rot_rect()
        inrect = lambda x, y: rot[0] <= x < rot[2] and rot[1] <= y < rot[3]
        inrot = self.rotting
        reeds = ts.props(TS.REEDS)
        dry = ts.props(TS.LAND_REEDS)
        cats = ts.props(TS.CATTAILS)
        dcats = ts.props(TS.LAND_CATTAILS)
        lilies = ts.props(TS.LILIES)
        plants = ts.props(TS.PLANTS)
        sticks = [p.keyed([FOG, WATER], rows=4) for p in ts.props(TS.STICKS).values()]
        dead = lambda p, x, y: p.recoloured(DEAD) if inrot(x, y) else p
        # decals first (flat, under every prop): grit on the sand, blades in the grass
        grit = list(ts.props(TS.PEBBLES).values())
        blades = [ts.part(b) for b in TS.BLADES]
        inner_sand, inner_grass = erode(self.sand, 4), erode(self.grassm, 3)
        for _ in range(W * H // 1400):
            x, y = r.randrange(W), r.randrange(H)
            if inner_sand.getpixel((x, y)):
                self.put_decal(r.choice(grit), x, y, r.random() < 0.5)
            elif inner_grass.getpixel((x, y)) and not self.forest.getpixel((x, y)):
                self.put_decal(r.choice(blades), x, y, r.random() < 0.5)
        # forest: willows on a jittered grid, the trunks inside the forest cells
        for y in range(4, H, 22):
            for x in range(4, W, 24):
                fx, fy = x + r.randrange(-9, 10), y + r.randrange(-7, 8)
                if 0 <= fx < W and 0 <= fy < H and cell(g, fx // CELL, fy // CELL) == "T" \
                        and r.random() < 0.85:
                    self.willow(r.choice(("T1", "T1", "T2", "T3", "T3", "T4", "T5")), fx, fy, r.random() < 0.5,
                                dead=inrot(fx, fy))
        for cy in range(MH):
            for cx in range(MW):
                c = g[cy][cx]
                x0, y0 = cx * CELL, cy * CELL
                if c == "r":                    # a reed bed
                    for _ in range(3):
                        x, y = x0 + r.randrange(CELL), y0 + r.randrange(CELL)
                        src = reeds if self.wet_at(x, y) else dry
                        p = src[r.choice(sorted(src))]
                        self.prop(dead(p, x, y), x, y, r.random() < 0.5, sway=True)
                    if r.random() < 0.6:
                        x, y = x0 + r.randrange(CELL), y0 + r.randrange(CELL)
                        src = cats if self.wet_at(x, y) else dcats
                        self.prop(dead(src[r.choice(sorted(src))], x, y), x, y, sway=True)
                elif c == "~":
                    x, y = x0 + r.randrange(CELL), y0 + r.randrange(CELL)
                    edge = self.land.getpixel((min(W - 1, x + 6), y)) or self.land.getpixel((max(0, x - 6), y))
                    k = r.random()
                    if edge and k < 0.35 and self.wet_at(x, y):
                        src = reeds if k < 0.25 else cats
                        self.prop(dead(src[r.choice(sorted(src))], x, y), x, y, r.random() < 0.5, sway=True)
                    elif not edge and k < 0.1 and self.wet_at(x, y) and not inrect(x, y):
                        self.prop(lilies[r.choice(sorted(lilies))], x, y, r.random() < 0.5, bob=True)
                    elif self.healed and inrect(x, y) and not edge and k < 0.3 and self.wet_at(x, y):
                        # the healed pond in flower: more lilies, most of them blooming, from their
                        # own draws (so everything else is the swamp's to the pixel)
                        a = self.amb
                        self.prop(lilies[a.choice(HEALED_LILIES)], x, y, a.random() < 0.5, bob=True)
                elif c == "%":                  # bramble: dark leafy tangles, black stems through them
                    for _ in range(2):
                        x, y = x0 + r.randrange(CELL), y0 + r.randrange(6, CELL + 6)
                        p = plants[r.choice(("G1", "G2", "G5", "G6"))].recoloured(DARK_LEAF)
                        self.prop(p, x, y, r.random() < 0.5)
                    if r.random() < 0.7:
                        x, y = x0 + r.randrange(CELL), y0 + r.randrange(4, CELL + 4)
                        self.prop(r.choice(sticks).recoloured(BLACK_WOOD), x, y, r.random() < 0.5)
                elif c in ".d" and r.random() < 0.05:
                    x, y = x0 + r.randrange(CELL), y0 + r.randrange(CELL)
                    if not self.wet_at(x, y) and not self.solid.getpixel((x, y)):
                        if c == "." and r.random() < 0.6:
                            self.prop(dead(plants[r.choice(sorted(plants))], x, y), x, y, r.random() < 0.5)
                        else:
                            self.prop(r.choice(sticks), x, y, r.random() < 0.5)
        # under the brambles: dark undergrowth
        br = AND(soft(cell_field(g, lambda c: c == "%"), r.noise(6, 40), 128), NOT(self.wet))
        self.fill(DEEPMOSS, br)
        self.fill(SOOT, AND(br, r.marks(0.08)))
        self.fill(BOG, AND(br, r.marks(0.04)))
        self.bramble = br

    def draw_props(self):
        for _, _, _, draw, owner in sorted(self.props, key=lambda t: (t[0], t[1], t[2])):
            self._owner = owner
            draw()
        self._owner = None
        self.fill(RUST, AND(erode(self.bramble, 2), self.rng.marks(0.0025, 0.7)))   # berries

    # 7. The Rot -------------------------------------------------------------------------

    def rot_rect(self):
        for name, x0, y0, x1, y1 in self.obj.get("regions", []):
            if name == "THE ROT":
                return x0, y0, x1, y1
        return 0, 0, 0, 0

    def rot(self):
        x0, y0, x1, y1 = self.rot_rect()
        if x1 <= x0 or self.healed:
            return
        r = self.rng
        # how far into The Rot: a soft field from its rectangle, ragged at the edge
        f = rect_mask(x0, y0, x1, y1).filter(ImageFilter.GaussianBlur(28))
        f = ImageChops.add(f, r.noise(14, 50), 1.0, -128)
        m = thresh(f, 120)
        # the grass dies off: dull, then dry in patches
        rg = AND(m, self.grassm)
        self.remap([0, 1, 2, 3, 4, 5, 6, DEEPMOSS, BOG, BOG, 10, 11, 12, 13, 14, 15], rg)
        self.remap([0, 1, 2, 3, 4, 5, 6, UMBER, PEAT, BOG, 10, 11, 12, 13, 14, 15], AND(rg, r.blobs(9, 160)))
        self.fill(UMBER, AND(AND(m, self.grassm), r.marks(0.03)))
        # what the toad left on its shore: bones and pale stones
        shore = AND(AND(m, NOT(self.wet)), MINUS(dilate(self.wet, 14), dilate(self.wet, 2)))
        self.fill(BONE, AND(shore, r.marks(0.006, 0.7)))
        self.fill(STONE, AND(shore, r.marks(0.003, 0.4)))
        # the water black with rot: more dark chop; the game's mood fade does the rest
        rotwater = AND(m, self.wet)
        s = AND(rotwater, r.strokes(0.03, 4))
        self.fill(SOOT, s)
        self.fill(DEEPMOSS, AND(rotwater, r.strokes(0.01, 3)))

    def gnats(self, swarms=10):
        """Gnats over The Rot (not healed): swarms of dark specks clear of
        the willows, each gnat on a little loop of its own and the swarm
        drifting, a step a phase. Their own stream; painted over everything
        (a speck over a prop takes its pixel, so no willow owns it)."""
        x0, y0, x1, y1 = self.rot_rect()
        if x1 <= x0 or self.healed:
            return
        a = Rng(AMB_SEED + 1)
        boxes = [(x - ax - 6, y - ay - 6, x - ax + p.w + 6, y - ay + p.h + 6)
                 for _, _, x, y, p, ax, ay in self.willows]
        at = []
        for _ in range(500):
            if len(at) >= swarms:
                break
            sx, sy = a.randrange(x0 + 20, x1 - 20), a.randrange(y0 + 16, y1 - 12)
            if any(bx0 <= sx < bx1 and by0 <= sy < by1 for bx0, by0, bx1, by1 in boxes) or \
                    self.solid.getpixel((sx, sy)) or any(abs(sx - tx) < 48 and abs(sy - ty) < 36 for tx, ty in at):
                continue
            at.append((sx, sy))
        self.swarms = at
        cv, own = self.cv.load(), self.owner.load()
        for sx, sy in at:
            ddx, ddy = GNAT_DRIFT[(self.phase + a.randrange(4)) & 3]
            for _ in range(a.randrange(7, 12)):
                gx, gy = sx + a.randrange(-8, 9), sy + a.randrange(-5, 6)
                loop, start = GNAT_LOOPS[a.randrange(len(GNAT_LOOPS))], a.randrange(4)
                dx, dy = loop[(self.phase + start) & 3]
                px, py = gx + dx + ddx, gy + dy + ddy
                cv[px, py] = STONE if cv[px, py] in GLOOM else SOOT     # (dark on the light, lit on the dark)
                own[px, py] = 0

    # --- terrain ------------------------------------------------------------------------

    def terrain(self):
        """The collision table: 8 px cells, by majority (solid first)."""
        self.hide()
        border = OR(rect_mask(0, 0, W, 4), OR(rect_mask(0, H - 4, W, HP), OR(rect_mask(0, 0, 4, HP),
                                                                              rect_mask(W - 4, 0, W, HP))))
        solid = OR(self.solid, border)
        self.solid = solid
        q = lambda m: m.crop((0, 0, W, H)).reduce(TCELL).tobytes()
        s, d, w = q(solid), q(self.deepm), q(self.wet)
        t = bytearray(TW * TH)
        for i in range(TW * TH):
            t[i] = SOLID if s[i] >= 112 else DEEP if d[i] >= 128 else SHALLOW if w[i] >= 128 else OPEN
        # the gate is open ground here; the game closes it
        gx0, gy0, gx1, gy1 = self.obj["gate"]
        for y in range(gy0 // TCELL, (gy1 + TCELL - 1) // TCELL):
            for x in range(gx0 // TCELL, (gx1 + TCELL - 1) // TCELL):
                if t[y * TW + x] == SOLID:
                    t[y * TW + x] = OPEN
        self.terr = t
        return t

    # --- occluders ----------------------------------------------------------------------

    def occluders(self):
        """The willows an actor can get behind (occlpack.py), each with the
        final world pixels it shows."""
        trees = []
        own = self.owner.load()
        cv = self.cv.load()
        for i, (key, flip, x, y, p, ax, ay) in enumerate(self.willows):
            x0, y0, tid = x - ax, y - ay, i + 1
            px = bytearray(p.w * p.h)
            for j in range(p.h):
                for k in range(p.w):
                    wx, wy = x0 + k, y0 + j
                    px[j * p.w + k] = cv[wx, wy] if 0 <= wx < W and 0 <= wy < HP and own[wx, wy] == tid else 15
            trees.append(OC.Tree(key, flip, x, y, x0, y0, p.w, p.h, ax, ay, px))
        t = self.terr
        walkable = lambda cx, cy: 0 <= cx < TW and 0 <= cy < TH and t[cy * TW + cx] != SOLID
        self.occl = OC.prune(trees, walkable)
        self.all_trees = trees
        return self.occl

    # --- run ----------------------------------------------------------------------------

    def paint(self):
        self.ground()
        self.grass()
        self.water()
        self.banks()
        self.worn()
        self.place_props()
        self.draw_props()
        self.rot()
        self.gnats()
        self.terrain()
        self.occluders()
        return self


# --- the result ------------------------------------------------------------------------

def shared_occluders(paints):
    """The willows an actor can get behind, for every layer, from all the
    paints (paints[layer][phase]): a tree keeps a pixel where it shows in
    every phase of every layer (a reed swaying in front of it in one phase,
    a gnat, takes the pixel out), with each layer's colours (The Rot's
    willows die and heal), so one table serves both layers and a pass is
    right in any phase. It keeps too, in its rectangle, the pixels of every
    willow standing in front of it (base line at or below its own: an actor
    behind it is behind those as well), so its pass puts back the canopy of
    the neighbours that hide parts of it, and where trees meet one pass
    covers both (occlpack.py). Kept or not (prune()) by its own pixels.
    [[occlpack.Tree] per layer]: the same trees, in the same order, with the
    same pixels shown."""
    base = paints[0][0]
    unstable = blank()
    for row in paints:
        for p in row:
            if p is not base:
                d = ImageMath.lambda_eval(lambda e: e["a"] != e["b"], a=base.owner, b=p.owner)
                unstable = OR(unstable, thresh(d.convert("L"), 1))
    own, off = base.owner.load(), unstable.load()
    cvs = [row[0].cv.load() for row in paints]
    layers = [[] for _ in paints]
    bases = [0] + [wl[3] for wl in base.willows]    # each willow's base line, by owner id
    for i, (key, flip, x, y, p, ax, ay) in enumerate(base.willows):
        x0, y0, tid = x - ax, y - ay, i + 1
        pxs = [bytearray(b"\x0f" * (p.w * p.h)) for _ in paints]
        mine = bytearray(p.w * p.h)
        for j in range(p.h):
            wy = y0 + j
            if not 0 <= wy < HP:
                continue
            for k in range(p.w):
                wx = x0 + k
                if not 0 <= wx < W or off[wx, wy]:
                    continue
                o = own[wx, wy]
                if o and bases[o] >= y:             # its own, or a willow's in front of it
                    mine[j * p.w + k] = o == tid
                    for px, cv in zip(pxs, cvs):
                        px[j * p.w + k] = cv[wx, wy]
        for trees, px in zip(layers, pxs):
            trees.append(OC.Tree(key, flip, x, y, x0, y0, p.w, p.h, ax, ay, px, mine))
    t = base.terr
    walkable = lambda cx, cy: 0 <= cx < TW and 0 <= cy < TH and t[cy * TW + cx] != SOLID
    OC.prune(layers[0], walkable)
    return [[t for t, t0 in zip(trees, layers[0]) if t0.useful] for trees in layers]


class World:
    """What build() gives: the bitmaps (images[layer][phase], W x HP
    indices each; idx is layer 0's phase 0), the terrain table, the
    occluders (occl_layers[layer]: occlpack.Tree, the willows an actor can
    get behind; occl is layer 0's) and the objects, plus everything that is
    made from them."""
    def __init__(self, images, terrain, objects, repairs, inputs_hash, occl_layers=((),), trees=0):
        self.images = [list(ph) for ph in images]
        self.idx = self.images[0][0]
        self.terrain = terrain
        self.obj = objects
        self.repairs = repairs
        self.hash = inputs_hash
        self.occl_layers = [list(t) for t in occl_layers]
        self.occl = self.occl_layers[0]
        self.trees = trees              # willows painted (occl: the ones kept)
        self.problems = []
        self.fresh = False              # painted now (not from the cache)
        self._section = None
        self._occl = None

    @property
    def section(self):
        if self._section is None:
            self._section = WP.pack(self.images, GEO)
        return self._section

    @property
    def occl_pack(self):
        """(OCCL section bytes, occlpack.layout()): every layer's trees."""
        if self._occl is None:
            self._occl = OC.pack_layers(self.occl_layers, H)
        return self._occl

    def at(self, x, y):
        return self.terrain[(y // TCELL) * TW + x // TCELL]

    def image(self, layer=0, phase=0):
        return Image.frombytes("L", (W, HP), self.images[layer][phase])


def inputs_hash():
    h = hashlib.sha256()
    for p in (MAP, OBJECTS, HERE / "paint.py", HERE / "tileset.py", HERE / "worldpack.py", HERE / "occlpack.py",
              GAME / "tools" / "assets" / "palette.py", GAME / "tools" / "assets" / "quantise.py",
              GAME / "tools" / "assets" / "sheets.py", TS.S.TILESET.path):
        h.update(p.name.encode())
        h.update(Path(p).read_bytes())
    return h.hexdigest()


def paint_all(key=""):
    """Every layer's every phase (a World, fresh): LAYERS x TPH paints."""
    grid, obj, ts = load_map(), load_objects(), TS.Tileset()
    paints = [[Painter(grid, obj, ts, phase, bool(layer)).paint() for phase in range(TPH)]
              for layer in range(LAYERS)]
    base = paints[0][0]
    for row in paints:
        for p in row:
            if bytes(p.terr) != bytes(base.terr) or len(p.willows) != len(base.willows):
                raise WorldError("a paint's terrain or willows differ from the others'")
    w = World([[p.cv.tobytes() for p in row] for row in paints], bytes(base.terr), base.obj, base.repairs, key,
              shared_occluders(paints), len(base.willows))
    w.fresh = True
    return w


def build(force=False, strict=True):
    """The world (a World), painted or from the cache. strict: raise
    WorldError if objects.json puts something where it cannot be (else the
    problems are in w.problems)."""
    key = inputs_hash()
    w = None
    if not force and CACHE.exists():
        try:
            c = pickle.loads(CACHE.read_bytes())
            if c.get("hash") == key:
                w = World(c["images"], c["terrain"], c["obj"], c["repairs"], key, c["occl"], c["trees"])
        except Exception:
            w = None
    if w is None:
        w = paint_all(key)
        OUT.mkdir(parents=True, exist_ok=True)
        CACHE.write_bytes(pickle.dumps(dict(hash=key, images=w.images, terrain=w.terrain, obj=w.obj,
                                            repairs=w.repairs, occl=w.occl_layers, trees=w.trees)))
    w.problems = validate(w)
    if strict and w.problems:
        raise WorldError("tools/world/objects.json:\n  " + "\n  ".join(w.problems))
    return w


def validate(w):
    """Every spot the game uses must be somewhere a squire can stand: the
    problems found (empty: none)."""
    o = w.obj
    bad = []
    spots = [("start", o["player_start"][0], o["player_start"][1])]
    spots += [("nest %s" % k, x, y) for k, x, y in o["nests"]]
    spots += [("mushroom %s" % k, x, y) for k, x, y in o["mushrooms"]]
    ar = o["arena"]
    spots += [("the toad", ar["toad"][0], ar["toad"][1])] + [("reeds", x, y) for x, y in ar["reeds"]]
    ax0, ay0, ax1, ay1 = ar["rect"]
    tx0, ty0, tx1, ty1 = ar["trigger"]
    if not (ax0 <= tx0 < tx1 <= ax1 and ay0 <= ty0 < ty1 <= ay1 and ty1 + 24 <= o["gate"][1]):
        bad.append("the arena's trigger must lie in it, 24 px or more north of the gate")
    for x, y in [ar["toad"]] + ar["reeds"]:
        if not (ax0 <= x < ax1 and ay0 <= y < ay1):
            bad.append("arena spot (%d, %d) outside the arena" % (x, y))
    for name, x, y in spots:
        if not (0 <= x < W and 0 <= y < H) or w.at(x, y) in (SOLID, DEEP):
            bad.append("%s at (%d, %d) is on %s" % (name, x, y, ("open", "shallow", "deep", "solid")[w.at(x, y)]
                                                     if 0 <= x < W and 0 <= y < H else "nothing"))
    if len(o["nests"]) != 6:
        bad.append("%d nests, the game has 6" % len(o["nests"]))
    if len(o["mushrooms"]) != 48:
        bad.append("%d mushrooms, the game has 48" % len(o["mushrooms"]))
    for k, *_ in o["nests"]:
        if k not in NEST_KINDS:
            bad.append("nest kind %r" % k)
    for k, *_ in o["mushrooms"]:
        if k not in MUSH_KINDS:
            bad.append("mushroom kind %r" % k)
    return bad


# --- previews ----------------------------------------------------------------------------

def render(idx_img):
    """An index image in the panel's colours (RGB)."""
    pal = []
    for i in range(16):
        pal += list(P.rgb888(i))
    pal += [255, 0, 255] * 240
    out = Image.frombytes("P", idx_img.size, idx_img.tobytes())
    out.putpalette(pal)
    return out.convert("RGB")


def previews(w):
    OUT.mkdir(parents=True, exist_ok=True)
    full = render(w.image().crop((0, 0, W, H)))
    full.save(OUT / "world.png")
    # 2x crops: the start, then a view into each region
    o = w.obj
    sx, sy = o["player_start"]
    views = [("start", sx - 128, sy - 120)]
    seen = set()
    for name, x0, y0, x1, y1 in o["regions"]:
        if name in seen:
            continue
        seen.add(name)
        views.append((name.lower(), (x0 + x1) // 2 - 128, (y0 + y1) // 2 - 120))
    tiles = []
    for name, x, y in views:
        x, y = max(0, min(W - 256, x)), max(0, min(H - 240, y))
        tiles.append(full.crop((x, y, x + 256, y + 240)).resize((512, 480), Image.NEAREST))
    sheet = Image.new("RGB", (512 * 3 + 8, 480 * 2 + 4), (30, 26, 22))
    for i, t in enumerate(tiles[:6]):
        sheet.paste(t, ((i % 3) * 516, (i // 3) * 484))
    sheet.save(OUT / "regions.png")
    # the terrain over the world, and the spots
    tint = {OPEN: None, SHALLOW: (80, 170, 255), DEEP: (20, 40, 160), SOLID: (255, 40, 40)}
    ov = full.copy().convert("RGBA")
    lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for ty in range(TH):
        for tx in range(TW):
            c = tint[w.terrain[ty * TW + tx]]
            if c:
                d.rectangle((tx * 8, ty * 8, tx * 8 + 7, ty * 8 + 7), fill=c + (90,))
    for name, x0, y0, x1, y1 in o["regions"][:-1]:
        d.rectangle((x0, y0, x1 - 1, y1 - 1), outline=(255, 255, 255, 160))
        d.text((x0 + 4, y0 + 4), name, fill=(255, 255, 255, 255))
    for k, x, y in o["nests"]:
        d.rectangle((x - 6, y - 6, x + 6, y + 6), outline=(255, 0, 255, 255), width=2)
        d.text((x + 8, y - 6), k, fill=(255, 0, 255, 255))
    for k, x, y in o["mushrooms"]:
        d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(255, 255, 255, 255) if k[0] == "B" else (255, 60, 40, 255))
    gx0, gy0, gx1, gy1 = o["gate"]
    d.rectangle((gx0, gy0, gx1 - 1, gy1 - 1), outline=(255, 255, 0, 255), width=2)
    d.ellipse((sx - 5, sy - 5, sx + 5, sy + 5), outline=(255, 255, 0, 255), width=2)
    ov.alpha_composite(lay)
    ov.convert("RGB").save(OUT / "terrain.png")
    # the occluders: what each kept willow redraws (bright) over the world (dimmed), and its rectangle
    dim = Image.eval(full, lambda v: v // 3)
    occ = dim.copy()
    d = ImageDraw.Draw(occ)
    for t in w.occl:
        ti = Image.frombytes("L", (t.w, t.h), t.pixels)
        occ.paste(render(ti), (t.x0, t.y0), ti.point(lambda v: 0 if v == 15 else 255))
        d.rectangle((t.x0, t.y0, t.x0 + t.w - 1, t.y0 + t.h - 1), outline=(90, 90, 160))
        d.line((t.x - 3, t.y, t.x + 3, t.y), fill=(255, 255, 0))
    occ.save(OUT / "occluders.png")
    ambient_previews(w)


def ambient_views(o):
    """(name, layer, cx, cy): views (128 x 120, as the panel shows them) of
    what moves: the start, a view into each region, The Rot healed."""
    sx, sy = o["player_start"]
    views, rot = [("start", 0, sx - 64, sy - 80)], None
    for name, x0, y0, x1, y1 in o["regions"]:
        at = ((x0 + x1) // 2 - 64, (y0 + y1) // 2 - 60)
        if name == "THE ROT":
            rot = at
        elif len(views) < 4 and all(v[0] != name.lower() for v in views):
            views.append((name.lower(), 0) + at)
    if rot:
        views += [("the rot", 0) + rot, ("the rot healed", 1) + rot]
    return [(n, layer, max(0, min(W - 128, x)), max(0, min(H - 120, y))) for n, layer, x, y in views]


def ambient_previews(w):
    """out/world/phases.gif: the views above at 2x, cycling through the
    phases as the game does (16 ticks a phase); out/world/healed.png: The
    Rot and the healed layer side by side."""
    tiles = ambient_views(w.obj)
    frames = []
    for p in range(TPH):
        sheet = Image.new("RGB", (256 * 3 + 8, 240 * 2 + 4), (30, 26, 22))
        for i, (name, layer, x, y) in enumerate(tiles[:6]):
            t = render(w.image(layer, p).crop((x, y, x + 128, y + 120))).resize((256, 240), Image.NEAREST)
            sheet.paste(t, ((i % 3) * 260, (i // 3) * 244))
        frames.append(sheet)
    frames[0].save(OUT / "phases.gif", save_all=True, append_images=frames[1:], duration=267, loop=0)
    x0, y0, x1, y1 = next((r[1:] for r in w.obj["regions"] if r[0] == "THE ROT"), (0, 0, 128, 120))
    x0, y0, x1, y1 = max(0, x0 - 32), max(0, y0), min(W, x1), min(H, y1 + 40)
    pair = Image.new("RGB", (2 * (x1 - x0) + 8, y1 - y0), (30, 26, 22))
    for k in range(2):
        pair.paste(render(w.image(k, 0).crop((x0, y0, x1, y1))), (k * (x1 - x0 + 8), 0))
    pair.save(OUT / "healed.png")


# --- WorldData.{h,cpp} --------------------------------------------------------------------

BANNER = ("// GENERATED by tools/world/paint.py from tools/world/map.txt and objects.json.\n"
          "// Do not edit: change those and re-run it (tools/mkcard.py does).\n")


def region_table(o):
    names = []
    rects = []
    for name, x0, y0, x1, y1 in o["regions"]:
        if name not in names:
            names.append(name)
        rects.append((x0, y0, x1, y1, names.index(name)))
    return names, rects


RUN_MAX = 64                    # cells a terrain run byte holds (its high 6 bits: length - 1)


def terrain_runs(t):
    """The terrain table (TW x TH classes) run-length coded as the game
    reads it: (runs, starts), a byte a run (class | (length - 1) << 2), and
    each row's first run (plus the end, starts[TH])."""
    runs, starts = bytearray(), []
    for y in range(TH):
        starts.append(len(runs))
        row = t[y * TW:(y + 1) * TW]
        x = 0
        while x < TW:
            n = 1
            while x + n < TW and row[x + n] == row[x] and n < RUN_MAX:
                n += 1
            runs.append(row[x] | (n - 1) << 2)
            x += n
    starts.append(len(runs))
    return bytes(runs), starts


def terrain_unruns(runs, starts):
    """terrain_runs() undone: the flat table."""
    t = bytearray()
    for y in range(TH):
        for b in runs[starts[y]:starts[y + 1]]:
            t += bytes([b & 3]) * ((b >> 2) + 1)
        if len(t) != (y + 1) * TW:
            raise WorldError("terrain row %d: runs add up to %d cells" % (y, len(t) - y * TW))
    return bytes(t)


def terrain_packed(t):
    """The flat table, 2 bits a cell, 4 a byte (the leftmost in the low bits)."""
    packed = bytearray(TW // 4 * TH)
    for i in range(TW * TH):
        packed[i >> 2] |= t[i] << (2 * (i & 3))
    return bytes(packed)


def data_sources(w):
    """(header text, source text)."""
    o = w.obj
    runs, starts = terrain_runs(w.terrain)
    packed = terrain_packed(w.terrain)
    oorder, oshapes, osidx, _, obands, _ = w.occl_pack[1]
    names, rects = region_table(o)
    sx, sy = o["player_start"]
    gx0, gy0, gx1, gy1 = o["gate"]
    ar = o["arena"]
    h = [BANNER.rstrip("\n"),
         "//",
         "// The world the card's WRLD section holds (tools/world/worldpack.py has the",
         "// layout) and what the game needs to know about it without the card: the",
         "// terrain under every 8 px cell, the regions, where the squire starts, the",
         "// nests and mushrooms, the gate to The Rot and the arena behind it. World",
         "// pixels throughout.",
         "#pragma once",
         "#include <stdint.h>",
         "",
         "constexpr int16_t WORLD_W = %d, WORLD_H = %d;" % (W, H),
         "// Card layout: NC column blocks a band, NB bands (every 8 rows, 128 tall),",
         "// TPH ambient phases (the whole world painted once for each: reeds",
         "// swaying, ripples, glints, lily pads, gnats), LAYERS world layers (0 the",
         "// swamp, 1 healed: The Rot as it is once its toad is beaten).",
         "constexpr uint16_t WORLD_NC = %d, WORLD_NB = %d;" % (GEO.nc, GEO.nb),
         "constexpr uint8_t WORLD_TPH = %d, WORLD_LAYERS = %d;" % (GEO.tph, GEO.layers),
         "",
         "// Terrain, a class a cell, run-length coded row by row: a run is a byte,",
         "// the class in its low 2 bits and its length - 1 in the high 6 (runs over",
         "// 64 cells are split; a row's runs add up to TERRAIN_W), and TERRAIN_ROW[y]",
         "// is the index of row y's first run (src/engine/Terrain.cpp walks them).",
         "constexpr uint8_t TERRAIN_CELL = %d, TERRAIN_W = %d, TERRAIN_H = %d;" % (TCELL, TW, TH),
         "enum : uint8_t { T_OPEN, T_SHALLOW, T_DEEP, T_SOLID };",
         "constexpr uint16_t TERRAIN_RUN_COUNT = %d;" % len(runs),
         "extern const uint8_t TERRAIN_RUNS[TERRAIN_RUN_COUNT];",
         "extern const uint16_t TERRAIN_ROW[TERRAIN_H];",
         "#ifdef CHTEST",
         "// The same flat, 2 bits a cell, 4 a byte (the leftmost in the low bits), for",
         "// the host tests to check the runs against (never in the game).",
         "extern const uint8_t TERRAIN_FLAT[TERRAIN_W / 4 * TERRAIN_H];",
         "#endif",
         "",
         "struct WorldRect { int16_t x0, y0, x1, y1; };          // [x0, x1) x [y0, y1)",
         "struct WorldRegion { WorldRect r; uint8_t name; };     // the first that holds a point",
         "struct WorldSpot { int16_t x, y; uint8_t kind; };",
         "",
         "constexpr uint8_t REGION_NAMES = %d, REGION_RECTS = %d;" % (len(names), len(rects)),
         "enum : uint8_t { %s };" % ", ".join("R_" + n.replace("THE ", "").replace(" ", "_") for n in names),
         "extern const char *const REGION_NAME[REGION_NAMES];",
         "extern const WorldRegion REGIONS[REGION_RECTS];",
         "",
         "constexpr int16_t START_X = %d, START_Y = %d;       // the squire's feet" % (sx, sy),
         "constexpr WorldRect GATE = {%d, %d, %d, %d};        // into The Rot: open terrain, the game shuts it"
         % (gx0, gy0, gx1, gy1),
         "// The arena, The Rot's pond: where the toad may land; the squire's feet in",
         "// ARENA_TRIGGER start the fight; the toad rises at TOAD_X, TOAD_Y; its rot",
         "// raccoons come out of the reeds at ARENA_REEDS.",
         "constexpr WorldRect ARENA = {%d, %d, %d, %d};" % tuple(ar["rect"]),
         "constexpr WorldRect ARENA_TRIGGER = {%d, %d, %d, %d};" % tuple(ar["trigger"]),
         "constexpr int16_t TOAD_X = %d, TOAD_Y = %d;" % tuple(ar["toad"]),
         "constexpr uint8_t ARENA_REED_COUNT = %d;" % len(ar["reeds"]),
         "constexpr int16_t ARENA_REEDS[ARENA_REED_COUNT][2] = {%s};"
         % ", ".join("{%d, %d}" % tuple(r) for r in ar["reeds"]),
         "",
         "enum : uint8_t { NEST_RACCOON, NEST_TURTLE, NEST_BEAVER, NEST_CHAMELEON };",
         "constexpr uint8_t NEST_COUNT = %d;" % len(o["nests"]),
         "extern const WorldSpot NESTS[NEST_COUNT];",
         "",
         "// Pickups: A2/A3 red (heal a little), B2/B3 white (heal a lot).",
         "// Each is x | y << 10 | kind << 20 (4 bytes, not a WorldSpot's 6).",
         "enum : uint8_t { MUSH_A2, MUSH_A3, MUSH_B2, MUSH_B3 };",
         "constexpr uint8_t MUSHROOM_COUNT = %d;" % len(o["mushrooms"]),
         "extern const uint32_t MUSHROOMS[MUSHROOM_COUNT];",
         "inline int16_t mushX(uint32_t m) { return (int16_t)(m & 1023); }",
         "inline int16_t mushY(uint32_t m) { return (int16_t)((m >> 10) & 1023); }",
         "inline uint8_t mushKind(uint32_t m) { return (uint8_t)(m >> 20); }",
         "",
         "// Occluders: the willows an actor can get behind (tools/world/occlpack.py;",
         "// src/engine/Occluders.h redraws them over him). A tree's shape (its sheet",
         "// and flip): its rectangle, wWords words x h rows, the trunk's foot (ax, ay)",
         "// in it, whose row is the base line (feet above it: behind the tree), and",
         "// how its rows lie in the card's OCCL section (rowsPerBlock whole rows a",
         "// block, `blocks` blocks from a fresh one).",
         "struct OccluderShape { uint8_t wWords, h, ax, ay, rowsPerBlock, blocks; };",
         "constexpr uint8_t OCCL_SHAPES = %d;" % len(oshapes),
         "extern const OccluderShape OCCL_SHAPE[OCCL_SHAPES];",
         "// The trees by base y, then x, 3 bytes each (little-endian): foot x | (base y",
         "// & 63) << 10 | shape << 16 | shown << 20, base y's band the one it is listed",
         "// in, shown how much of its rectangle it shows (0-15; the rest is covered by",
         "// trees in front of it); their rows in the same order in OCCL after its header.",
         "constexpr uint16_t OCCL_COUNT = %d;" % len(oorder),
         "extern const uint8_t OCCLUDERS[OCCL_COUNT * 3];",
         "// Every 1 << OCCL_BAND_SHIFT rows of base y: the first tree with its base there",
         "// or below and that tree's first block (after the header); [OCCL_BANDS]: the end.",
         "constexpr uint8_t OCCL_BAND_SHIFT = %d, OCCL_BANDS = %d;" % (OC.BAND_SHIFT, len(obands) - 1),
         "// The most rows a tree reaches above its base line, and below it.",
         "constexpr uint8_t OCCL_ABOVE = %d, OCCL_BELOW = %d;" % (max((s[5] for s in oshapes), default=0),
                                                                 max((s[3] - s[5] for s in oshapes), default=0)),
         "struct OccluderBand { uint16_t first, block; };",
         "extern const OccluderBand OCCL_BAND[OCCL_BANDS + 1];",
         "// Each world layer has its own copy of the trees' rows (the same pixels in",
         "// its colours: The Rot's willows dead in layer 0, alive in 1), OCCL_LAYER_BLOCKS",
         "// blocks each, layer 0's first.",
         "constexpr uint16_t OCCL_LAYER_BLOCKS = %d;" % w.occl_pack[1][5],
         ""]
    c = [BANNER.rstrip("\n"), '#include "WorldData.h"', ""]
    t = w.terrain
    c.append("// %s; %d runs" % (" ".join("%s=%d" % (n, t.count(v)) for n, v in
                                          (("open", OPEN), ("shallow", SHALLOW), ("deep", DEEP),
                                           ("solid", SOLID))), len(runs)))
    c.append("const uint8_t TERRAIN_RUNS[TERRAIN_RUN_COUNT] = {")
    for y in range(TH):
        c.append("    " + ",".join("0x%02X" % v for v in runs[starts[y]:starts[y + 1]]) + ",")
    c.append("};")
    c.append("const uint16_t TERRAIN_ROW[TERRAIN_H] = {")
    for y in range(0, TH, 16):
        c.append("    " + ",".join("%d" % v for v in starts[y:min(TH, y + 16)]) + ",")
    c.append("};")
    c.append("#ifdef CHTEST")
    c.append("const uint8_t TERRAIN_FLAT[TERRAIN_W / 4 * TERRAIN_H] = {")
    for y in range(TH):
        c.append("    " + ",".join("0x%02X" % v for v in packed[y * (TW // 4):(y + 1) * (TW // 4)]) + ",")
    c.append("};")
    c.append("#endif")
    c.append("")
    c.append("const char *const REGION_NAME[REGION_NAMES] = {%s};" % ", ".join('"%s"' % n for n in names))
    c.append("const WorldRegion REGIONS[REGION_RECTS] = {")
    for x0, y0, x1, y1, n in rects:
        c.append("    {{%d, %d, %d, %d}, %d},     // %s" % (x0, y0, x1, y1, n, names[n]))
    c.append("};")
    c.append("")
    c.append("const WorldSpot NESTS[NEST_COUNT] = {")
    for k, x, y in o["nests"]:
        c.append("    {%d, %d, NEST_%s}," % (x, y, k.upper()))
    c.append("};")
    c.append("")
    c.append("const uint32_t MUSHROOMS[MUSHROOM_COUNT] = {")
    ms = o["mushrooms"]
    for i in range(0, len(ms), 4):
        c.append("    " + " ".join("%d | %d << 10 | MUSH_%s << 20," % (x, y, k) for k, x, y in ms[i:i + 4]))
    c.append("};")
    c.append("")
    c.append("// %d willows painted, %d an actor can get behind" % (w.trees, len(oorder)))
    c.append("const OccluderShape OCCL_SHAPE[OCCL_SHAPES] = {")
    for key, flip, ww, th, ax, ay, rpb, nb in oshapes:
        c.append("    {%d, %d, %d, %d, %d, %d},     // %s%s" % (ww, th, ax, ay, rpb, nb, key, " flipped" if flip else ""))
    c.append("};")
    c.append("const uint8_t OCCLUDERS[OCCL_COUNT * 3] = {")
    ent = [OC.entry(t, si) for t, si in zip(oorder, osidx)]
    for i in range(0, len(ent), 8):
        c.append("    " + " ".join("%s," % ",".join("0x%02X" % b for b in e) for e in ent[i:i + 8]))
    c.append("};")
    c.append("const OccluderBand OCCL_BAND[OCCL_BANDS + 1] = {")
    c.append("    " + " ".join("{%d, %d}," % b for b in obands))
    c.append("};")
    return "\n".join(h) + "\n", "\n".join(c) + "\n"


def write_if_changed(path, text):
    p = Path(path)
    if p.exists() and p.read_text(encoding="utf-8") == text:
        return False
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return True


def emit_sources(w):
    """src/assets/WorldData.{h,cpp}; True if either changed."""
    hdr, src = data_sources(w)
    a = write_if_changed(SRC_ASSETS / "WorldData.h", hdr)
    b = write_if_changed(SRC_ASSETS / "WorldData.cpp", src)
    return a or b


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--force", action="store_true", help="paint even if the cache matches")
    a = ap.parse_args(argv)
    import time
    t0 = time.time()
    w = build(force=a.force, strict=False)
    t1 = time.time()
    previews(w)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "WRLD.bin").write_bytes(w.section)
    changed = emit_sources(w)
    t2 = time.time()
    counts = [w.terrain.count(v) for v in range(4)]
    print("world %dx%d, %d phases x %d layers: painted %.1fs, packed+previews %.1fs; section %d blocks (%d KB)"
          % (W, H, TPH, LAYERS, t1 - t0, t2 - t1, GEO.blocks, len(w.section) // 1024))
    print("terrain open %d shallow %d deep %d solid %d; %d runs" % (tuple(counts) + (len(terrain_runs(w.terrain)[0]),)))
    print("occluders: %d of %d willows an actor can get behind, OCCL %d blocks (%d a layer)"
          % (len(w.occl), w.trees, len(w.occl_pack[0]) // 512, w.occl_pack[1][5]))
    if w.repairs:
        print("map pinches filled with grass (fix map.txt): %s"
              % ", ".join("(%d,%d)%s" % r for r in w.repairs))
    print("src/assets/WorldData.{h,cpp} %s" % ("written" if changed else "unchanged"))
    for p in w.problems:
        print("PROBLEM: " + p)
    return 1 if w.problems else 0


if __name__ == "__main__":
    sys.exit(main())
