"""Ethan's Critters: the Swamp tileset as the world painter's parts.

The tileset (Sprites/Swamp Tileset - Blue.png) is quantised once with
tools/assets/quantise.py (its sheet overrides, tiles mode: 15 is the swamp
water, transparent pixels come back as HOLE), so everything here is palette
indices already and the painter composites indices: what it paints is
exactly what the panel shows.

Parts (source boxes in tileset pixels; every one checked by eye against the
sheet, see out/art/tileset_preview.png):

  ground    the sand/grass autotiles, read as DUAL-GRID pieces: a 16x16 tile
            whose four corners are each grass or not (16 cases). They are
            found by classifying every tile of the two autotile blocks
            (cols 2-19 rows 0-5, the convex patch, the concave frame and the
            ring) and the extra set (cols 20-37 rows 0-5) by their corners,
            keeping those whose grass edge crosses every mixed side near its
            middle, so any two of them join. The sand is keyed out: the
            pieces are grass over whatever the painter laid down before.
            There is no piece with two diagonal corners (a pinch): the
            painter repairs the map so none is needed.
  props     willows T1-T5 (with the trunk's footprint), rocks, sticks, reeds
            and cattails (water and land versions), lily pads, small plants,
            grass blades, sand decals (grit and pebbles), the brown decor
            mushrooms (the red and white ones are pickups the game draws).
  banks     the earth-bank strips (rows 16-21, cols 21-32): a grass top tile
            over a face tile, faces plain, with roots, with burrow holes,
            windows and the hut's door; rounded end caps.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1]
sys.path.insert(0, str(GAME / "tools" / "assets"))
import palette as P     # noqa: E402
import quantise as Q    # noqa: E402
import sheets as S      # noqa: E402

(SOOT, UMBER, PEAT, BARK, MUD, BONE, DEEPMOSS, BOG, MOSS, LICHEN, OCHRE, FOG, SLATE, STONE,
 RUST, WATER) = range(16)
GREENS = (DEEPMOSS, BOG, MOSS, LICHEN)
HOLE = Q.HOLE
T = 16

# Dual-grid corner bits: a piece's case is the OR of its grass corners.
TL, TR, BL, BR = 1, 2, 4, 8
PINCHES = (TL | BR, TR | BL)

# --- props: (x0, y0, x1, y1) search boxes; the part is the box's opaque pixels -------

WILLOWS = {             # land willows T1-T3, water willows T4-T5 (roots standing in water)
    "T1": (360, 97, 410, 160),
    "T2": (426, 104, 496, 190),
    "T3": (513, 112, 575, 191),
    "T4": (357, 161, 411, 224),
    "T5": (425, 197, 471, 251),
}
ROCKS = {
    "R1": (201, 268, 213, 277),     # small grey stone
    "R2": (228, 266, 252, 278),     # long flat stone
    "R3": (261, 265, 282, 280),     # big mossy boulder
    "R4": (296, 267, 310, 278),     # round stone
    "R5": (216, 271, 221, 274),     # pebble
}
STICKS = {              # branches standing in water (blue at their feet); land: feet keyed out
    "S1": (192, 385, 223, 398), "S2": (229, 384, 243, 399), "S3": (272, 385, 287, 400),
    "S4": (288, 392, 318, 407), "S5": (321, 392, 352, 405), "S6": (257, 396, 283, 415),
    "S7": (193, 398, 223, 413), "S8": (240, 398, 254, 413), "S9": (363, 389, 372, 408),
    "S10": (266, 423, 281, 438), "S11": (201, 424, 215, 439), "S12": (234, 424, 248, 439),
    "S13": (294, 425, 313, 440), "S14": (228, 403, 234, 410), "S15": (334, 429, 340, 436),
}
REEDS = {               # water reeds (blue at their feet); +160 rows: the same on land
    "W1": (32, 192, 64, 224), "W2": (64, 192, 96, 224), "W3": (96, 192, 128, 224),
    "W4": (32, 224, 64, 256), "W5": (64, 240, 96, 290), "W6": (96, 224, 128, 256),
    "W7": (32, 272, 64, 304), "W8": (96, 272, 128, 304), "W9": (64, 304, 96, 336),
}
LAND_REEDS = {k.replace("W", "L"): (x0, y0 + 160, x1, y1 + 160) for k, (x0, y0, x1, y1) in REEDS.items()}
CATTAILS = {"C1": (142, 230, 148, 252), "C2": (173, 232, 181, 254), "C3": (140, 261, 147, 285),
            "C4": (173, 267, 180, 286)}
LAND_CATTAILS = {"D1": (143, 390, 148, 411), "D2": (174, 392, 181, 413), "D3": (140, 421, 146, 444),
                 "D4": (174, 427, 180, 445)}
LILIES = {"P1": (133, 298, 154, 314), "P2": (167, 299, 185, 312),
          "F1": (135, 327, 153, 344), "F2": (165, 328, 186, 346)}
PLANTS = {              # ferns and clumps on land
    "G1": (357, 224, 380, 250), "G2": (390, 234, 409, 250), "G3": (484, 203, 508, 217),
    "G4": (488, 230, 504, 247), "G5": (517, 193, 538, 217), "G6": (515, 229, 538, 252),
}
BLADES = [(322, 194, 326, 199), (334, 194, 337, 198), (343, 195, 347, 200), (337, 197, 341, 204),
          (329, 198, 333, 204), (348, 200, 351, 204), (322, 201, 325, 205), (340, 203, 344, 208),
          (333, 204, 336, 208), (345, 206, 349, 211), (326, 207, 330, 212), (335, 210, 338, 215),
          (322, 211, 326, 216), (330, 213, 333, 217), (345, 213, 349, 218), (59, 216, 63, 223),
          (294, 198, 301, 201), (308, 206, 315, 209), (296, 215, 303, 218), (296, 229, 300, 234)]
PEBBLES = {             # decals for sand: little patches of grit and grey stones
    "E1": (610, 3, 622, 9), "E2": (611, 17, 623, 26), "E3": (612, 36, 620, 41), "E4": (610, 51, 622, 56),
    "E5": (613, 67, 619, 72), "E6": (610, 81, 622, 88), "E7": (610, 101, 621, 108),
}
MUSHROOMS = {           # A2 A3 red (small heals), B2 B3 white (big heals): pickups, not painted
    "A1": (202, 332, 213, 345), "A2": (232, 334, 245, 344), "A3": (268, 333, 278, 344),
    "A4": (297, 335, 311, 343), "B1": (204, 365, 214, 376), "B2": (231, 364, 247, 378),
    "B3": (266, 365, 278, 377), "B4": (295, 365, 313, 378),
}
DECOR_MUSHROOMS = ("A1", "A4", "B4")    # the brown ones: baked into the world

# --- the earth banks: rows 16-21, cols 21-32 ------------------------------------------
# A bank is a strip two tiles tall: the top (grass, its fringe hanging over
# the face) on the row above the face. Faces by kind, as (top, face) tile
# origins; the face rows are 17, 19 and 21.
BANK_FACES = {
    "plain": [((x, 256), (x, 272)) for x in (352, 368, 384, 400, 448, 464, 480, 496)],
    "roots": [((x, 320), (x, 336)) for x in (352, 368, 384, 400, 448, 464)],
    "hole": [((352, 288), (352, 304)), ((368, 288), (368, 304)), ((480, 288), (480, 304)),
             ((496, 288), (496, 304))],
    "window": [((384, 288), (384, 304)), ((400, 288), (400, 304)), ((448, 288), (448, 304)),
               ((464, 288), (464, 304))],
    "door": [((416, 288), (416, 304)), ((432, 288), (432, 304))],
    "doorway": [((416, 256), (416, 272)), ((432, 256), (432, 272))],
}
BANK_CAP_L = (336, 272)                 # rounded ends (face row only)
BANK_CAP_R = (512, 272)


class Part:
    """A piece of the tileset: indices and mask (PIL 'L' images, 255 = opaque)."""
    def __init__(self, idx, mask, name=""):
        self.idx = idx
        self.mask = mask
        self.name = name
        self.w, self.h = idx.size

    def keyed(self, colours, rows=None):
        """A copy with these colours made transparent (rows: only in the last
        `rows` rows, e.g. the water at a stick's foot)."""
        idx = self.idx.copy()
        mask = self.mask.copy()
        ip, mp = idx.load(), mask.load()
        y0 = 0 if rows is None else max(0, self.h - rows)
        for y in range(y0, self.h):
            for x in range(self.w):
                if ip[x, y] in colours:
                    mp[x, y] = 0
        return Part(idx, mask, self.name)

    def recoloured(self, table):
        """A copy through a 16-entry index table (opaque pixels only)."""
        lut = list(table) + [HOLE] * (256 - len(table))
        return Part(self.idx.point(lut), self.mask, self.name)

    def flipped(self):
        from PIL import Image
        return Part(self.idx.transpose(Image.FLIP_LEFT_RIGHT), self.mask.transpose(Image.FLIP_LEFT_RIGHT),
                    self.name)

    def opaque_box(self):
        return self.mask.getbbox()


class Tileset:
    def __init__(self):
        from PIL import Image
        sheet = S.TILESET
        w, h = Q.sheet_image(sheet).size
        self.w, self.h = w, h
        self.idx = Image.frombytes("L", (w, h), bytes(Q.quantise_sheet(sheet)))
        self.mask = self.idx.point(lambda v: 0 if v == HOLE else 255)
        self._px = self.idx.load()
        self.ground = self._ground_pieces()

    # --- raw access -------------------------------------------------------------------

    def at(self, x, y):
        return self._px[x, y]

    def part(self, box, name="", tight=True):
        """The box's pixels; tight: cropped to its opaque pixels."""
        x0, y0, x1, y1 = box
        p = Part(self.idx.crop(box), self.mask.crop(box), name)
        if tight:
            bb = p.mask.getbbox()
            if bb is None:
                raise ValueError("tileset box %r (%s) is empty" % (box, name))
            if bb != (0, 0, x1 - x0, y1 - y0):
                p = Part(p.idx.crop(bb), p.mask.crop(bb), name)
        return p

    def tile(self, x, y):
        return self.part((x, y, x + T, y + T), tight=False)

    # --- the ground autotiles ----------------------------------------------------------

    def _grass(self, x, y):
        return self._px[x, y] in GREENS

    def classify(self, tx, ty):
        """A tile's dual-grid case (grass corners), or None if it is not a clean
        piece: each corner's 4x4 must be clearly grass or clearly not, a side
        between two grass corners all grass, between two others no grass, and
        a mixed side must change over once, in its middle (5..11)."""
        x0, y0 = tx * T, ty * T
        g = self._grass

        def frac(cx, cy):
            return sum(g(x0 + x, y0 + y) for y in range(cy, cy + 4) for x in range(cx, cx + 4)) / 16.0

        corners = (frac(0, 0), frac(12, 0), frac(0, 12), frac(12, 12))
        if any(0.25 < v < 0.75 for v in corners):
            return None
        case = sum(1 << i for i, v in enumerate(corners) if v >= 0.75)
        sides = (  # (pixels along the side, corner bit at its start, at its end)
            ([(x, 0) for x in range(T)], TL, TR),
            ([(x, T - 1) for x in range(T)], BL, BR),
            ([(0, y) for y in range(T)], TL, BL),
            ([(T - 1, y) for y in range(T)], TR, BR),
        )
        for pts, a, b in sides:
            s = [g(x0 + x, y0 + y) for x, y in pts]
            ca, cb = bool(case & a), bool(case & b)
            if ca == cb:
                if sum(v != ca for v in s) > 1:
                    return None
            else:
                changes = [i for i in range(1, T) if s[i] != s[i - 1]]
                if not changes or not 5 <= changes[0] <= 11 or not 5 <= changes[-1] <= 11:
                    return None
        return case

    def _ground_pieces(self):
        """{case: [Part, ...]}: grass over anything, the sand keyed out."""
        pieces = {}
        for ty in range(0, 6):
            for tx in range(2, 38):
                if self.mask.getpixel((tx * T + 8, ty * T + 8)) == 0 and self.at(tx * T, ty * T) == HOLE:
                    continue
                case = self.classify(tx, ty)
                if case is None or case == 0:
                    continue
                p = self.tile(tx * T, ty * T)
                keep = p.keyed([c for c in range(16) if c not in GREENS])
                keep.name = "ground %d,%d" % (tx, ty)
                pieces.setdefault(case, []).append(keep)
        missing = [c for c in range(1, 16) if c not in PINCHES and c not in pieces]
        if missing:
            raise ValueError("no ground piece for cases %r" % missing)
        return pieces

    # --- props -------------------------------------------------------------------------

    def props(self, table):
        return {k: self.part(b, k) for k, b in table.items()}

    def willow(self, key):
        """(Part, anchor x, anchor y, footprint (x0, y0, x1, y1)) relative to
        the part: the anchor is the middle of the trunk's foot, the footprint
        the box the trunk and roots stand on (solid ground)."""
        p = self.part(WILLOWS[key], key)
        ip, mp = p.idx.load(), p.mask.load()
        wood = (UMBER, PEAT, BARK)              # the trunk and roots (the canopy is green)
        ay = max(y for y in range(p.h) for x in range(p.w) if mp[x, y] and ip[x, y] in wood)
        # the trunk just above the root flare: its middle is the anchor, it is the footprint
        cols = [x for y in range(ay - 8, ay - 4) for x in range(p.w) if mp[x, y] and ip[x, y] in wood]
        lo, hi = min(cols), max(cols)
        ax = (lo + hi) // 2
        return p, ax, ay, (lo - 1, ay - 6, hi + 2, ay + 1)
