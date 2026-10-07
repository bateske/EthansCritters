"""Ethan's Critters: the willows as occluders (section OCCL of CRITTERS.DAT).

The willows are painted into the world bitmap (paint.py), so an actor that
walks behind one would be drawn over its canopy. The game puts the canopy
back: right after drawing an actor whose feet are above a tree's base line
(behind it) and whose sprite overlaps the tree, it streams the rows of the
tree that the overlap covers and blits them over the actor, clipped to the
overlap (src/engine/Occluders.h).

What is stored for a tree is what the world shows of it and of the willows
in front of it: in its rectangle, the final world pixels owned by it or by
any willow whose base line is at or below its own (the tree's own paint,
not covered by a prop drawn after it, with The Rot's grime and the berries
painted later included), every other pixel 15 (the key). An actor behind
the tree is behind those willows too, so they are right over him as well:
a pass never comes up empty because the neighbours in front hide the tree
where he stands, and where two trees meet one pass covers both. Over every
place the squire can stand under willows (his walk and idle frames), his
best pass alone puts back 98.7% of the canopy over him and two 99.9%
(0.7% of poses miss 8 px or more); with each tree's own pixels only it was
78% and 93% (16% of poses), the "wrong tree" at the edge of a canopy.
Where trees overlap they store the same pixels, the world's, so the
redraws are right in any order, and a rock or a reed standing in front of
a trunk keeps its place.

A tree's shape is its sheet (T1-T5) and flip: the rectangle (w x h, stored
wWords words wide) and the anchor (ax, ay) in it, the middle of the trunk's
foot, whose row is the base line (tileset.py's willow()). Only the trees an
actor can get behind are kept: an actor's feet on ground that is not solid,
above the base, its sprite (up to 64 x 36 round the feet) over a pixel the
tree itself shows (deep in the forest, nothing walks).

Section layout: block 0 the header, then each tree in table order (by base
y, then foot x) from a fresh block: rowsPerBlock = 512 // (4 wWords) whole
rows a block, each row wWords words in gfx_blit4k's format (pixel i of a
word in bits 4i..4i+3, 15 transparent), the rest of a block 15s. With world
layers (M8: layer 1 the healed swamp, whose willows in The Rot are alive),
the trees again for each further layer, laid out the same: layer l's tree
data starts `data blocks` x l blocks after the header. The trees and the
pixels they show are the same in every layer (paint.py keeps a pixel only
where its tree shows in all of them); only their colours differ.
Header (little-endian): 0 "OCCL"  4 u16 trees  6 u16 shapes  8 u16 data
blocks (a layer's)  10 u16 layers.

The table (src/assets/WorldData.h, written by paint.py):
  OCCL_SHAPE[s]   {wWords, h, ax, ay, rowsPerBlock, blocks}
  OCCLUDERS[3i]   tree i: foot x | (base y & 63) << 10 | shape << 16 |
                  shown << 20 (24 bits, LE); base y's band is the one the
                  tree is listed in; shown: how much of its rectangle is
                  stored (not 15), 0-15 (the game weighs an overlap with
                  it: a rectangle half empty is worth less)
  OCCL_BAND[b]    {the first tree with base y >= b << OCCL_BAND_SHIFT, its
                   first data block}; [OCCL_BANDS] = {count, data blocks}
"""
import struct

BLOCK = 512
MAGIC = b"OCCL"
HEAD = struct.Struct("<4sHHHH")
BAND_SHIFT = 6                  # tree bands by base y: 64 rows each
REACH_X, REACH_UP, REACH_DOWN = 32, 34, 2   # an actor's sprite round its feet (px): the prune's box
MIN_PIXELS = 32                 # a tree showing fewer under any actor is not kept


class Tree:
    """One willow as painted: sheet key, flip, the trunk's foot (x, base y),
    the rectangle's top-left (x0, y0), w x h, the anchor (ax, ay), the
    pixels stored for it (w * h indices, 15 where the world shows something
    else: its own and the willows' in front of it) and which of them are its
    own (w * h flags; none given: every pixel stored)."""
    def __init__(self, key, flip, x, y, x0, y0, w, h, ax, ay, pixels, own=None):
        self.key, self.flip = key, flip
        self.x, self.y = x, y
        self.x0, self.y0, self.w, self.h = x0, y0, w, h
        self.ax, self.ay = ax, ay
        self.pixels = bytes(pixels)
        self.own = bytes(v != 15 for v in self.pixels) if own is None else bytes(own)
        self.useful = True

    @property
    def shape(self):
        return (self.key, self.flip)

    def owned(self):
        """World pixels the tree itself shows: (x, y) pairs."""
        out = []
        for j in range(self.h):
            row = self.own[j * self.w:(j + 1) * self.w]
            for i, v in enumerate(row):
                if v:
                    out.append((self.x0 + i, self.y0 + j))
        return out

    def owned_box(self):
        """The bounding box of what it shows itself (x0, y0, x1, y1), or None."""
        px = self.owned()
        if not px:
            return None
        xs, ys = [p[0] for p in px], [p[1] for p in px]
        return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def words(w):
    return (w + 7) // 8


def shape_geometry(w, h):
    """(wWords, rowsPerBlock, blocks) of a w x h tree."""
    ww = words(w)
    rpb = BLOCK // (4 * ww)
    return ww, rpb, (h + rpb - 1) // rpb


def prune(trees, walkable, cell=8, least=MIN_PIXELS):
    """Marks each tree useful or not: useful when at least `least` of the
    pixels it shows itself can be under the sprite of an actor behind it (fewer:
    nobody would see the canopy missing, and a pass would be wasted on it).
    walkable(cx, cy): an actor's feet may stand in that terrain cell. A
    conservative test by cells. Returns the useful ones."""
    rx, up = (REACH_X + cell - 1) // cell, (REACH_UP + cell - 1) // cell
    for t in trees:
        cells = {}
        for x, y in t.owned():
            cells[(x // cell, y // cell)] = cells.get((x // cell, y // cell), 0) + 1
        seen = 0
        for (cx, cy), n in cells.items():
            for fy in range(cy - 1, cy + up + 1):           # feet from just above the pixel to REACH_UP below
                if fy * cell >= t.y:                        # not above the base: in front
                    break
                if any(walkable(fx, fy) for fx in range(cx - rx, cx + rx + 1)):
                    seen += n
                    break
        t.useful = seen >= least
    return [t for t in trees if t.useful]


def layout(trees, world_h):
    """The table: (trees in order, shapes [(key, flip, wWords, h, ax, ay,
    rpb, blocks)], each tree's shape index and first data block, bands)."""
    order = sorted(trees, key=lambda t: (t.y, t.x, t.key, t.flip))
    shapes, index = [], {}
    for t in sorted(order, key=lambda t: (t.key, t.flip)):
        if t.shape not in index:
            ww, rpb, nb = shape_geometry(t.w, t.h)
            index[t.shape] = len(shapes)
            shapes.append((t.key, t.flip, ww, t.h, t.ax, t.ay, rpb, nb))
    for t in order:
        s = shapes[index[t.shape]]
        if (s[3], s[4], s[5], words(t.w)) != (t.h, t.ax, t.ay, s[2]):
            raise ValueError("tree %s at (%d, %d): not its shape's size" % (t.key, t.x, t.y))
        if not (0 <= t.x < 1024 and 0 <= t.y < 1024):
            raise ValueError("tree %s at (%d, %d): outside the table's 10 bits" % (t.key, t.x, t.y))
    firsts, blk = [], 0
    for t in order:
        firsts.append(blk)
        blk += shapes[index[t.shape]][7]
    nbands = (world_h + (1 << BAND_SHIFT) - 1) >> BAND_SHIFT
    bands = []
    for b in range(nbands + 1):
        i = next((k for k, t in enumerate(order) if t.y >= b << BAND_SHIFT), len(order))
        bands.append((i, firsts[i] if i < len(order) else blk))
    return order, shapes, [index[t.shape] for t in order], firsts, bands, blk


def shown(t):
    """How much of its rectangle (wWords words wide) a tree shows, 0-15."""
    return round(15 * sum(1 for v in t.pixels if v != 15) / (8 * words(t.w) * t.h))


def entry(t, shape):
    """A tree's 3 table bytes."""
    assert BAND_SHIFT == 6                      # the entry's base offset is 6 bits
    if shape > 15:
        raise ValueError("%d shapes: the table has 4 bits for one" % (shape + 1))
    v = t.x | (t.y & ((1 << BAND_SHIFT) - 1)) << 10 | shape << 16 | shown(t) << 20
    return bytes((v & 255, (v >> 8) & 255, v >> 16))


def tree_rows(t, ww):
    """The tree's rows as gfx_blit4k words (padded with 15)."""
    out = bytearray()
    for j in range(t.h):
        row = list(t.pixels[j * t.w:(j + 1) * t.w]) + [15] * (8 * ww - t.w)
        out += bytes(row[i] | row[i + 1] << 4 for i in range(0, 8 * ww, 2))
    return bytes(out)


def pack(trees, world_h):
    """(section bytes, layout(...)): the OCCL section of one layer."""
    return pack_layers([trees], world_h)


def pack_layers(layers, world_h):
    """(section bytes, layout(...)): the OCCL section, layers[l] the trees
    of world layer l: the same trees (place, shape, the pixels shown) in
    each, only their colours may differ; the table is layer 0's."""
    lay = layout(layers[0], world_h)
    order, shapes, sidx, firsts, bands, nblocks = lay
    data = [HEAD.pack(MAGIC, len(order), len(shapes), nblocks, len(layers)) + bytes(BLOCK - HEAD.size)]
    key = lambda o: [(entry(t, s), bytes(v == 15 for v in t.pixels)) for t, s in zip(o[0], o[2])]
    want = key(lay)
    for trees in layers:
        o = layout(trees, world_h)
        if key(o) != want:
            raise ValueError("pack_layers: the layers' trees differ in more than their colours")
        for t, s in zip(o[0], o[2]):
            _, _, ww, h, _, _, rpb, nb = shapes[s]
            rows = tree_rows(t, ww)
            rb = 4 * ww
            for k in range(nb):
                chunk = rows[k * rpb * rb:(k + 1) * rpb * rb]
                data.append(chunk + b"\xFF" * (BLOCK - len(chunk)))
    out = b"".join(data)
    assert len(out) == (1 + len(layers) * nblocks) * BLOCK
    return out, lay


def tree_pixel(section, lay, i, x, y, layer=0):
    """Tree i's stored pixel at (x, y) of its rectangle, in that layer (tests)."""
    order, shapes, sidx, firsts, bands, nblocks = lay
    _, _, ww, h, _, _, rpb, _ = shapes[sidx[i]]
    o = (1 + layer * nblocks + firsts[i] + y // rpb) * BLOCK + (y % rpb) * 4 * ww + x // 2
    v = section[o]
    return v >> 4 if x & 1 else v & 15


def view_counts(trees, w, h, inside, step=16, vw=128, vh=120):
    """For cameras every `step` px: how many trees' rectangles show in the
    view, split by inside(view middle) (Willow Hollow): two lists of counts."""
    a, b = [], []
    for cy in range(0, h - vh + 1, step):
        for cx in range(0, w - vw + 1, step):
            n = sum(1 for t in trees if t.x0 < cx + vw and t.x0 + t.w > cx and t.y0 < cy + vh and t.y0 + t.h > cy)
            (a if inside(cx + vw // 2, cy + vh // 2) else b).append(n)
    return a, b
