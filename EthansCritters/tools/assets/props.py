#!/usr/bin/env python3
"""Ethan's Critters: sprite-bank art built offline (no source PNG of its own).

Composed here from the Swamp tileset's pieces (tools/world/tileset.py: its
sticks, reeds, plants, rocks and mushrooms, already palette indices) and a
few pixels drawn by hand, then banked like any sheet (packbank.py adds
built() after the PNG sheets, so the PNG clips keep their numbers):

  nest    one clip per nest type, 3 frames: intact, battered, wrecked (the
          game picks the frame by the nest's health; the world under it is
          painted cleared, so a nest that falls is simply not drawn). The
          pivot is the middle of the base: the nest's place (WorldData.h's
          NESTS).
            raccoon    a heap of trash and sticks with a dark hole
            turtle     a scrape in the sand, eggs in it, reeds behind
            beaver     a dome of sticks and mud, its foot in the water
            chameleon  a leafy tangle with eggs in a cup of twigs
  mush    the pickups: red (the A3 fly agaric) and white (the B3 dome),
          frames idle and glint (a 2 px bone highlight; the game shows it
          for a moment now and then, and bobs the mushroom 1 px itself)
  spawn   a poof of dust rising and breaking up, 4 frames (a critter appears)
  stick   a beaver's stick spinning end over end, 8 frames
  gate    the bramble wall across The Rot's gate (WorldData.h's GATE), one
          half of it (the game mirrors it for the other): closed, then
          pulled back to the side in 3 frames as the brambles part; dark
          reeds and leaves, dead stalks, thorny canes with berries
  swipe   the sword's swipe, one frame: a wide, squashed crescent in front
          of the squire at his blade's height (the game blinks it ochre
          behind him over the thrust's hurting frames; it is the blade's reach)

A frame must fit a 512 B block with its 8 B header: 32 px wide and up to
31 rows, or 33-40 wide and up to 25 rows (tools/tests/test_packbank.py
holds them to 488 B: the frame cache's arena is 3 KB).

    python EthansCritters/tools/assets/props.py      out/art/props.png (6x, every frame) and
            props_world.png (each frame where the game draws it, over out/world/world.png)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[0] / "world"))
import palette as P     # noqa: E402
import sheets as S      # noqa: E402

(SOOT, UMBER, PEAT, BARK, MUD, BONE, DEEPMOSS, BOG, MOSS, LICHEN, OCHRE, FOG, SLATE, STONE,
 RUST, KEY) = range(16)
PREVIEW = S.ROOT / "out" / "art" / "props.png"

_ts = None


def tileset():
    global _ts
    if _ts is None:
        import tileset as TS
        _ts = TS.Tileset()
    return _ts


def part(table, key, land=False):
    """A tileset piece (tileset.py's tables) as a Part; land: the water's
    glints keyed out (the sticks are drawn standing in water)."""
    import tileset as TS
    p = tileset().part(getattr(TS, table)[key], key)
    if land:
        p = p.keyed([FOG, KEY])
    return p


def noise(x, y, seed=0):
    """0..255, the same for the same (x, y, seed): the texture of earth."""
    h = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 255


class Canvas:
    """A frame being composed: palette indices, KEY where nothing is."""
    def __init__(self, w, h):
        from PIL import Image, ImageDraw
        self.w, self.h = w, h
        self.im = Image.new("L", (w, h), KEY)
        self.d = ImageDraw.Draw(self.im)

    def put(self, p, x, y, flip=False):
        """A tileset Part with its top-left at (x, y)."""
        if flip:
            p = p.flipped()
        self.im.paste(p.idx, (x, y), p.mask)

    def foot(self, p, fx, fy, flip=False):
        """A tileset Part standing with its bottom middle at (fx, fy)."""
        self.put(p, fx - p.w // 2, fy - p.h + 1, flip)

    def rows(self, x, y, rows):
        """Hand-drawn pixels: rows of hex digits, '.' clear."""
        for j, r in enumerate(rows):
            for i, c in enumerate(r):
                if c != "." and 0 <= x + i < self.w and 0 <= y + j < self.h:
                    self.im.putpixel((x + i, y + j), int(c, 16))

    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.im.putpixel((x, y), c)

    def get(self, x, y):
        return self.im.getpixel((x, y)) if 0 <= x < self.w and 0 <= y < self.h else KEY

    def ellipse(self, box, c):
        self.d.ellipse(box, fill=c)

    def where(self, test, c, box=None):
        """Every opaque pixel (in box) for which test(x, y, colour) holds becomes c."""
        x0, y0, x1, y1 = box or (0, 0, self.w, self.h)
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                v = self.im.getpixel((x, y))
                if v != KEY and test(x, y, v):
                    self.im.putpixel((x, y), c)

    def outline(self, c=SOOT, below=None):
        """A 1 px outline round everything opaque (4-neighbours); below: only
        down to this row (the foot stands on the ground)."""
        src = self.im.copy()
        g = src.load()
        for y in range(self.h if below is None else below + 1):
            for x in range(self.w):
                if g[x, y] != KEY:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < self.w and 0 <= yy < self.h and g[xx, yy] != KEY:
                        self.im.putpixel((x, y), c)
                        break

    def clip(self, bottom):
        """Nothing below row `bottom`."""
        self.where(lambda x, y, v: y > bottom, KEY)

    def trim(self, x0, x1):
        """Nothing outside columns [x0, x1) (a frame 32 px wide fits a block)."""
        self.where(lambda x, y, v: not x0 <= x < x1, KEY)


def egg(cv, x, y, c=BONE, shade=STONE, small=False):
    """An egg, top-left (x, y), soot outlined: 4x5 (small: 4x4)."""
    mid = ["0%X%X0" % (c, c)] * (1 if small else 2)
    cv.rows(x, y, [".00."] + mid + ["0%X%X0" % (c, shade), ".00."])


# --- the nests ------------------------------------------------------------------------------

NEST_CELL = (40, 32)
NEST_PIVOT = (20, 31)
BASE = NEST_PIVOT[1]

# Junk the raccoons dragged home (the world painter's DEBRIS, tools/world/paint.py,
# lies round the cleared den): a tin, a bone, an eggshell.
TIN = ["0000", "0DC0", "0DC0", "0000"]
TIN_LYING = ["00000", "0DDC0", "00000"]
BONE_BIT = ["00..00", "055550", "00..00"]
SHELL_BIT = [".000.", "05550", ".000."]


def heap(cv, box, seed, top=PEAT, low=UMBER, lit=BARK, grit=True):
    """A heap of earth: an ellipse cut flat at the base, shaded by noise:
    darker low down, lit on its upper left, pebbles and holes in it."""
    x0, y0, x1, y1 = box
    cv.ellipse(box, top)
    mid = (y0 + min(y1, BASE)) // 2 + 1
    cx = (x0 + x1) // 2
    cv.where(lambda x, y, v: v == top and noise(x, y, seed) < 60 + (y - mid) * 40, low, box)
    cv.where(lambda x, y, v: v == top and y < mid and noise(x, y, seed + 1) < 140 - (x - cx) * 10 - (y - y0) * 8,
             lit, box)
    if grit:
        cv.where(lambda x, y, v: noise(x, y, seed + 2) < 10, SOOT, box)
        cv.where(lambda x, y, v: noise(x, y, seed + 3) < 8, STONE, box)


def raccoon_den(state):
    cv = Canvas(*NEST_CELL)
    if state == 0:      # a tall heap, sticks over it, the dark hole, junk stuck in it
        heap(cv, (5, 12, 34, 46), 1)
        cv.put(part("STICKS", "S13", True), 4, 7)
        cv.put(part("STICKS", "S12", True), 22, 6, flip=True)
        cv.put(part("STICKS", "S14", True), 15, 9)
        cv.rows(27, 17, TIN)
        cv.rows(6, 24, SHELL_BIT)
        cv.rows(28, 27, BONE_BIT)
        hole = (13, 20, 26, 37)
    elif state == 1:    # lower, a stick knocked away, the tin rolled out
        heap(cv, (6, 15, 33, 47), 2)
        cv.put(part("STICKS", "S13", True), 5, 10)
        cv.put(part("STICKS", "S14", True), 24, 12, flip=True)
        cv.rows(7, 25, SHELL_BIT)
        cv.rows(30, 28, TIN_LYING)
        hole = (13, 22, 26, 38)
    else:               # slumped to a half-heap, the hole's roof fallen in, a stick splayed
        heap(cv, (7, 20, 32, 48), 3)
        cv.put(part("STICKS", "S11", True), 3, 17)
        cv.rows(26, 28, BONE_BIT)
        hole = (12, 23, 27, 40)
    cv.outline(below=BASE - 1)
    cv.ellipse(hole, SOOT)
    hx0, hy0, hx1, _ = hole
    for x in range(hx0 + 3, hx1 - 2):
        cv.px(x, hy0 - 1, UMBER)
    if state == 2:
        cv.rows(hx0 + 3, hy0 - 3, ["0.00..0", "00000000", ".0....0."])     # its roof caved in
    cv.clip(BASE)
    return cv


def turtle_clutch(state):
    cv = Canvas(*NEST_CELL)
    # reeds behind the scrape (their feet hidden by its back rim)
    reeds = [("D4", 9, 24, False), ("D1", 30, 25, True), ("D2", 26, 26, False), ("D4", 13, 25, True)]
    for k, fx, fy, flip in reeds[:{0: 4, 1: 3, 2: 1}[state]]:
        cv.foot(part("LAND_CATTAILS", k), fx, fy, flip)
    # the scrape: a rim of sand scuffed up round a damp hollow
    cv.ellipse((4, 21, 35, 31), MUD)
    cv.ellipse((8, 22, 31, 30), BARK)
    cv.ellipse((10, 22, 29, 26), PEAT)
    cv.where(lambda x, y, v: v == PEAT and y >= 25 and noise(x, y, 4) < 128, BARK, (8, 22, 32, 31))
    cv.where(lambda x, y, v: v == MUD and noise(x, y, 5) < 40, BARK)
    cv.where(lambda x, y, v: v == MUD and y >= 30 and noise(x, y, 6) < 140, BARK)
    eggs = {0: ((11, 22), (15, 21), (20, 21), (24, 22), (13, 25), (18, 25), (22, 25)),
            1: ((13, 22), (18, 21), (22, 24), (14, 25)),
            2: ((17, 23),)}[state]
    for x, y in eggs:
        egg(cv, x, y)
    if state == 1:
        cv.rows(25, 26, ["0.0.0", "05550", ".000."])            # a hatched one, its top broken
    if state == 2:
        cv.rows(9, 25, [".000.", "05550", "0D550", ".000."])     # half shells
        cv.rows(24, 26, ["0.0.0", "05550", ".000."])
    cv.clip(BASE)
    return cv


def beaver_lodge(state):
    cv = Canvas(*NEST_CELL)
    if state == 0:
        dome = (2, 12, 37, 52)
        sticks = [("S7", 3, 9, False), ("S5", 13, 10, True), ("S12", 22, 13, False), ("S4", 6, 16, True),
                  ("S1", 15, 17, False)]
    elif state == 1:
        dome = (3, 15, 36, 54)
        sticks = [("S7", 4, 13, False), ("S12", 24, 15, False), ("S1", 13, 19, True)]
    else:
        dome = (5, 21, 34, 58)
        sticks = [("S5", 4, 18, False), ("S11", 26, 18, True)]
    heap(cv, dome, 7 + state, top=PEAT, low=UMBER, lit=BARK)
    for k, x, y, flip in sticks:
        cv.put(part("STICKS", k, True), x, y, flip)
    cv.outline(below=BASE - 1)
    if state == 1:          # a hole torn in its roof
        cv.ellipse((15, 15, 23, 20), SOOT)
        cv.rows(14, 14, ["0.2...20.", ".0.....0."])
    # the way in, under the water line: a dark arch, and the water's glints at the foot
    x0, x1 = (15, 24) if state < 2 else (13, 26)
    cv.ellipse((x0, BASE - 7, x1, BASE + 7), SOOT)
    for x in range(1, 39):
        if cv.get(x, BASE) != KEY and (x // 3) % 3 != 1 and not x0 < x < x1:
            cv.px(x, BASE, FOG)
    cv.px(x0 + 2, BASE, FOG)
    cv.px(x1 - 2, BASE, FOG)
    cv.clip(BASE)
    return cv


def chameleon_brood(state):
    cv = Canvas(*NEST_CELL)
    if state < 2:       # the tangle: forked branches with leaves through them
        cv.foot(part("STICKS", "S9", True), 10, 27)
        cv.foot(part("STICKS", "S9", True), 30, 26, flip=True)
    leaves = [("G3", 20, 16, False), ("G4", 12, 24, False), ("G2", 28, 24, True), ("G2", 19, 25, False)]
    for k, fx, fy, flip in leaves[:{0: 4, 1: 2, 2: 0}[state]]:
        cv.foot(part("PLANTS", k), fx, fy, flip)
    if state == 2:      # stripped: twigs, a clump of leaves fallen by the cup
        cv.foot(part("STICKS", "S14", True), 12, 28)
        cv.foot(part("STICKS", "S15", True), 28, 28)
        cv.foot(part("PLANTS", "G2"), 27, 29, flip=True)
    # the cup of twigs, and the eggs in it
    cv.ellipse((9, 25, 31, 32), SOOT)
    cv.ellipse((10, 26, 30, 31), PEAT)
    cv.where(lambda x, y, v: v == PEAT and noise(x, y, 11) < 110, UMBER, (10, 26, 31, 32))
    cv.where(lambda x, y, v: v == PEAT and y == 26 and noise(x, y, 12) < 120, BARK, (10, 26, 31, 27))
    eggs = {0: ((12, 23), (16, 22), (20, 23), (24, 22)), 1: ((15, 23), (21, 22)), 2: ((18, 23),)}[state]
    for x, y in eggs:
        egg(cv, x, y, c=BONE, shade=LICHEN, small=True)
    if state:
        cv.rows(24, 25, ["0.0.", "0550", ".00."])               # a shell, broken open
    cv.clip(BASE)
    cv.trim(4, 36)
    return cv


NEST_KINDS = (("raccoon", raccoon_den), ("turtle", turtle_clutch), ("beaver", beaver_lodge),
              ("chameleon", chameleon_brood))


# --- the mushrooms ----------------------------------------------------------------------------

MUSH_CELL = (16, 16)
MUSH_PIVOT = (8, 15)
# (tileset key, where the glint goes, relative to the part's top-left)
MUSH_KINDS = (("red", "A3", ((2, 2), (3, 2))), ("white", "B3", ((2, 3), (1, 4))))


def mushroom(key, glint, lit):
    cv = Canvas(*MUSH_CELL)
    p = part("MUSHROOMS", key)
    x, y = MUSH_PIVOT[0] - p.w // 2, MUSH_PIVOT[1] - p.h + 1
    cv.put(p, x, y)
    if lit:
        for gx, gy in glint:
            cv.px(x + gx, y + gy, BONE)
    return cv


# --- the spawn puff ---------------------------------------------------------------------------

PUFF_CELL = (24, 20)
PUFF_PIVOT = (12, 19)
# A poof of dust: bubbles of cloud (fog, a stone underside, a bone glint, no
# outline: it is air), (x, y, r) about the pivot, rising and breaking up.
PUFF_BUBBLES = (
    ((-4, -3, 3), (3, -3, 3), (0, -5, 3)),
    ((-6, -5, 4), (5, -5, 4), (0, -9, 4), (-1, -3, 3), (3, -11, 2)),
    ((-8, -8, 3), (7, -9, 3), (-2, -13, 3), (4, -15, 2), (-6, -14, 2), (1, -6, 2)),
    ((-9, -11, 1), (8, -12, 2), (-3, -16, 1), (4, -18, 1), (0, -9, 1)),
)


def puff(f):
    cv = Canvas(*PUFF_CELL)
    px, py = PUFF_PIVOT
    for bx, by, r in PUFF_BUBBLES[f]:
        cx, cy = px + bx, py + by
        cv.ellipse((cx - r, cy - r, cx + r, cy + r), STONE)
    for bx, by, r in PUFF_BUBBLES[f]:   # the lit tops
        cx, cy = px + bx, py + by
        if r > 1:
            cv.ellipse((cx - r + 1, cy - r, cx + r - 1, cy + r - 2), FOG)
        cv.px(cx - r // 2, cy - r + 1, BONE)
    return cv


# --- the thrown stick -------------------------------------------------------------------------

STICK_CELL = (16, 16)
STICK_PIVOT = (8, 8)        # its middle: the game puts that where it flies
STICK_FRAMES = 8            # a turn in 45 degree steps (the twig makes the turn read)


def stick(f):
    """A beaver's stick spinning end over end, frame f of STICK_FRAMES: a
    bark stick 11 px long and 2 thick (lit on top, peat beneath), a twig
    with a leaf a third of the way along, soot outlined."""
    import math
    cv = Canvas(*STICK_CELL)
    a = 2 * math.pi * f / STICK_FRAMES
    ux, uy = math.cos(a), math.sin(a)       # along the stick
    nx, ny = -uy, ux                        # across it
    cx, cy = STICK_PIVOT[0] + 0.5, STICK_PIVOT[1] + 0.5
    for y in range(cv.h):
        for x in range(cv.w):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            along, across = dx * ux + dy * uy, dx * nx + dy * ny
            if abs(along) <= 5.6 and abs(across) <= 0.9:
                lit = across * (ny if abs(ny) > 0.3 else nx) < 0     # the side up (or left) catches the light
                cv.px(x, y, MUD if abs(along) > 4.6 else BARK if lit else PEAT)
    # the twig: off the stick's side at a third, a leaf at its end
    tx, ty = cx - 2 * ux + 2 * nx, cy - 2 * uy + 2 * ny
    cv.px(int(tx), int(ty), PEAT)
    cv.px(int(tx + 1.4 * nx), int(ty + 1.4 * ny), MOSS)
    cv.outline()
    return cv


# --- the sword's swipe --------------------------------------------------------------------------

SWIPE_CELL = (32, 24)
SWIPE_PIVOT = (6, 18)       # the squire's feet: the arc sweeps in front of him at his blade's height
SWIPE_ARC = (10, 9, 17, 6, 16)      # the crescent: an ellipse's centre (x, y) and x radius (the y
                                    # radius is half: the arc is half as tall as it is wide), and the
                                    # offset and radius of the ellipse cut out of it (toward him)
SWIPE_SQUASH = 2.0                  # y is scaled by this: the arc's height is its width / 2


def swipe():
    """The sword's swipe: one squashed crescent, convex side forward, in
    front of the squire from his chest to his feet (17 rows, 21 wide):
    bone, an ochre rim on the outside and at the tips. The game draws it
    behind him over the thrust's hurting frames, blinking ochre, and it is
    the blade's reach (Player.h's blade())."""
    cv = Canvas(*SWIPE_CELL)
    cx, cy, r, off, r2 = SWIPE_ARC
    for y in range(cv.h):
        for x in range(cv.w):
            dx, dy = x + 0.5 - cx, (y + 0.5 - cy) * SWIPE_SQUASH
            d1 = dx * dx + dy * dy
            d2 = (dx + off) ** 2 + dy * dy
            if d1 <= r * r and d2 > r2 * r2:
                rim = d1 > (r - 1.2) ** 2 or d2 < (r2 + 1.2) ** 2
                cv.px(x, y, OCHRE if rim else BONE)
    return cv


# --- the bramble gate -------------------------------------------------------------------------

GATE_CELL = (24, 40)        # one half of the gap (WorldData.h's GATE is 48 px wide), 40 tall
GATE_PIVOT = (24, 39)       # the gap's middle, on the wall's foot line: the game draws the
                            # left half there and the same frame mirrored for the right one
GATE_PULL = (0, 6, 12, 18)  # px the brambles are pulled back to the side, frame by frame

# The wall's makings: (table, key, foot x, foot y, flip, recolour); foot x in the
# half's own px (24 is the gap's middle). Back to front.
DARK_LEAF = (SOOT, UMBER, UMBER, PEAT, BARK, MUD, SOOT, DEEPMOSS, DEEPMOSS, BOG, PEAT, STONE,
             SLATE, STONE, RUST, KEY)       # (paint.py's brambles: dark leaves ...)
DEAD = (SOOT, UMBER, PEAT, BARK, MUD, BONE, UMBER, PEAT, BARK, MUD, BARK, STONE, SLATE,
        STONE, UMBER, KEY)                  # (... and dead stalks)
GATE_CLUMPS = (
    ("LAND_REEDS", "L5", 8, 39, False, DARK_LEAF), ("LAND_REEDS", "L5", 22, 37, True, DARK_LEAF),
    ("LAND_REEDS", "L2", 2, 39, True, DARK_LEAF), ("LAND_CATTAILS", "D3", 17, 30, False, DEAD),
    ("LAND_CATTAILS", "D1", 5, 27, True, DEAD), ("LAND_REEDS", "L9", 14, 39, False, DARK_LEAF),
    ("PLANTS", "G1", 20, 39, True, DARK_LEAF), ("PLANTS", "G5", 6, 39, False, DARK_LEAF),
)
# Thorny canes arching up through it: (x0, y0) its foot, (x1, y1) its tip.
GATE_CANES = ((2, 39, 22, 6), (23, 38, 3, 10), (10, 39, 23, 20), (0, 24, 18, 2), (20, 39, 6, 22))


def gate(f):
    """Frame f of the bramble gate's left half: f 0 the closed wall, then
    pulled back GATE_PULL[f] px toward the side, the canes and stalks
    leaning away from the gap (the game shows nothing once it is open)."""
    from PIL import Image
    cv = Canvas(*GATE_CELL)
    pull = GATE_PULL[f]
    # how far a thing at x is pulled: things by the gap the most, the side ones hardly
    shift = lambda x: -(pull * (x + 6)) // 30
    # the undergrowth: a dark tangle along the foot, thinning as it parts
    for y in range(18, 40):
        for x in range(24):
            reach = 24 - pull - (2 if y < 26 else 0) + (noise(x, y, 31) >> 6)
            if x < reach and noise(x, y, 30) < 150 + (y - 18) * 5:
                v = noise(x, y, 32)
                cv.px(x, y, SOOT if v < 70 else DEEPMOSS if v < 210 else BOG)
    for table, key, fx, fy, flip, rec in GATE_CLUMPS:
        if table == "LAND_CATTAILS" and fx + shift(fx) > 12:
            continue            # a stalk by the gap is gone once it parts
        p = part(table, key).recoloured(rec)
        if flip:
            p = p.flipped()
        if pull:            # leaning away from the gap: each row shifted by its height
            lean = pull // 6
            im = p.idx.transform(p.idx.size, Image.AFFINE, (1, -lean / p.h, lean, 0, 1, 0), Image.NEAREST)
            mk = p.mask.transform(p.mask.size, Image.AFFINE, (1, -lean / p.h, lean, 0, 1, 0), Image.NEAREST)
            import tileset as TS
            p = TS.Part(im, mk, p.name)
        cv.foot(p, fx + shift(fx), fy)
    # the canes: soot, a peat light along their top, thorns every few px, berries
    for x0, y0, x1, y1 in GATE_CANES:
        x1 = x1 + shift(x1) - pull // 3
        x0 = x0 + shift(x0) // 2
        n = max(abs(x1 - x0), abs(y1 - y0))
        for i in range(n + 1):
            bend = (i * (n - i) * 6) // (n * n)             # a curve, bowed upward
            x = x0 + (x1 - x0) * i // n
            y = y0 + (y1 - y0) * i // n - bend
            cv.px(x, y, SOOT)
            cv.px(x + (1 if x1 < x0 else -1), y, UMBER)
            cv.px(x, y - 1, PEAT)
            if i % 4 == 2:                                  # a thorn
                cv.px(x + (1 if (i // 4) & 1 else -1), y - 2, BONE if i % 12 == 2 else STONE if i % 8 == 2 else PEAT)
            if i % 9 == 5 and noise(x, y, 33) < 140:        # a berry
                cv.px(x, y + 1, RUST)
    cv.trim(0, 24)
    cv.clip(GATE_PIVOT[1])
    return cv


# --- as sheets for packbank -------------------------------------------------------------------

class Built:
    """A sheet made here: what packbank reads of a sheets.Sheet (key, cell,
    pivot, anims, kind) and its pixels (indices(): a grid, a row per anim)."""
    kind = "sprite"
    use = "built"

    def __init__(self, key, cell, pivot, rows, ms=100, role="still"):
        self.key, self.cell, self.pivot = key, cell, pivot
        self.overrides = {}
        self._rows = rows                       # [(anim name, [Canvas])]
        self.anims = [S.Anim(name, r, len(frames), role, ms=ms) for r, (name, frames) in enumerate(rows)]
        self._idx = None

    def anim(self, ident):
        return next(a for a in self.anims if a.id == ident)

    def indices(self):
        """(bytearray of indices, row-major, sheet width)."""
        if self._idx is None:
            cw, ch = self.cell
            cols = max(len(fr) for _, fr in self._rows)
            w, h = cols * cw, len(self._rows) * ch
            idx = bytearray([KEY]) * (w * h)
            for r, (name, frames) in enumerate(self._rows):
                for f, cv in enumerate(frames):
                    if (cv.w, cv.h) != (cw, ch):
                        raise ValueError("%s %s f%d: %dx%d, not the cell" % (self.key, name, f, cv.w, cv.h))
                    data = cv.im.tobytes()
                    for y in range(ch):
                        o = (r * ch + y) * w + f * cw
                        idx[o:o + cw] = data[y * cw:(y + 1) * cw]
            self._idx = (idx, w)
        return self._idx

    def __repr__(self):
        return "Built(%r)" % self.key


def built():
    """The built sheets, in bank order."""
    return [
        Built("nest", NEST_CELL, NEST_PIVOT, [(k, [fn(s) for s in range(3)]) for k, fn in NEST_KINDS],
              role="nest"),
        Built("mush", MUSH_CELL, MUSH_PIVOT, [(k, [mushroom(t, g, lit) for lit in (False, True)])
                                              for k, t, g in MUSH_KINDS], role="pickup"),
        Built("spawn", PUFF_CELL, PUFF_PIVOT, [("puff", [puff(f) for f in range(len(PUFF_BUBBLES))])],
              ms=80, role="spawn"),
        Built("stick", STICK_CELL, STICK_PIVOT, [("spin", [stick(f) for f in range(STICK_FRAMES)])],
              ms=50, role="projectile"),
        Built("gate", GATE_CELL, GATE_PIVOT, [("part", [gate(f) for f in range(len(GATE_PULL))])],
              ms=100, role="gate"),
        Built("swipe", SWIPE_CELL, SWIPE_PIVOT, [("arc", [swipe()])], ms=50, role="attack"),
    ]


def preview(sheets=None, path=PREVIEW, scale=6):
    """Every frame at `scale`, on mid grey, its pivot marked."""
    from PIL import Image, ImageDraw
    sheets = sheets or built()
    pad = 6
    rows = [(s, a) for s in sheets for a in s.anims]
    W = pad + max(len(s._rows[a.row][1]) * (s.cell[0] * scale + pad) for s, a in rows) + 120
    H = pad + sum(s.cell[1] * scale + pad for s, _ in rows)
    out = Image.new("RGB", (W, H), (96, 92, 84))
    d = ImageDraw.Draw(out)
    pal = [P.rgb888(i) for i in range(16)]
    y = pad
    for s, a in rows:
        cw, ch = s.cell
        d.text((pad, y + 4), "%s %s" % (s.key, a.id), fill=(240, 236, 220))
        for f, cv in enumerate(s._rows[a.row][1]):
            x = pad + 110 + f * (cw * scale + pad)
            d.rectangle([x, y, x + cw * scale - 1, y + ch * scale - 1], fill=(120, 116, 106))
            for yy in range(ch):
                for xx in range(cw):
                    v = cv.im.getpixel((xx, yy))
                    if v != KEY:
                        d.rectangle([x + xx * scale, y + yy * scale, x + xx * scale + scale - 1,
                                     y + yy * scale + scale - 1], fill=pal[v])
            px, py = s.pivot
            d.rectangle([x + px * scale + scale // 2 - 1, y + py * scale + scale // 2 - 1,
                         x + px * scale + scale // 2 + 1, y + py * scale + scale // 2 + 1], fill=(255, 0, 255))
        y += ch * scale + pad
    path.parent.mkdir(parents=True, exist_ok=True)
    out.save(path)
    return path


WORLD_PNG = S.ROOT / "out" / "world" / "world.png"
IN_WORLD = S.ROOT / "out" / "art" / "props_world.png"


def in_world(sheets=None, path=IN_WORLD, scale=3):
    """Every frame where the game draws it, over the painted world
    (out/world/world.png, paint.py's preview): the nests at the first nest
    of their kind, the pickups and the puff on sand, grass and dirt."""
    import json
    from PIL import Image
    sheets = sheets or built()
    world = Image.open(WORLD_PNG).convert("RGB")
    obj = json.loads((S.GAME / "tools" / "world" / "objects.json").read_text(encoding="utf-8"))
    spots = {k: (x, y) for k, x, y in reversed(obj["nests"])}
    pal = [P.rgb888(i) for i in range(16)]
    vw, vh = 72, 52
    tiles = []
    for s in sheets:
        for a in s.anims:
            if s.key == "nest":
                places = [spots[a.id]]
            elif s.key == "gate":
                g = obj["gate"]
                places = [((g[0] + g[2]) // 2, g[3] - 6)]
            else:
                places = [(244, 694), (196, 302), (312, 650)]     # sand by the Landing, grass, the den's dirt
            for (wx, wy) in places:
                for cv in s._rows[a.row][1]:
                    view = world.crop((wx - vw // 2, wy - vh + 12, wx + vw // 2, wy + 12)).copy()
                    ox, oy = vw // 2 - s.pivot[0], vh - 12 - s.pivot[1]
                    for yy in range(cv.h):
                        for xx in range(cv.w):
                            v = cv.im.getpixel((xx, yy))
                            if v != KEY and 0 <= ox + xx < vw and 0 <= oy + yy < vh:
                                view.putpixel((ox + xx, oy + yy), pal[v])
                                if s.key == "gate":         # and the other half, mirrored
                                    view.putpixel((ox + 2 * s.pivot[0] - 1 - xx, oy + yy), pal[v])
                    tiles.append(view.resize((vw * scale, vh * scale), Image.NEAREST))
    cols = 6
    pad = 4
    out = Image.new("RGB", (cols * (vw * scale + pad) + pad, -(-len(tiles) // cols) * (vh * scale + pad) + pad),
                    (40, 38, 34))
    for i, t in enumerate(tiles):
        out.paste(t, (pad + (i % cols) * (vw * scale + pad), pad + (i // cols) * (vh * scale + pad)))
    out.save(path)
    return path


if __name__ == "__main__":
    print("wrote", preview())
    if WORLD_PNG.exists():
        print("wrote", in_world())
