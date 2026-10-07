"""Ethan's Critters: the world on the card (section WRLD of CRITTERS.DAT).

The world is one painted bitmap (tools/world/paint.py), 4 bpp palette
indices, streamed from the card every frame: no tiles at runtime. It is
stored so that the visible window is ONE contiguous card read:

  column block  8 px wide x 128 world rows: 128 rows of 4 bytes, each row
                as gfx_fb holds 8 pixels (two a byte, the even x in the low
                nibble; as a little-endian u32, pixel i is bits 4i..4i+3).
                Exactly 512 B, one card block.
  band b        world rows [8b, 8b + 128): any camera y has its 120 visible
                rows inside one band (b = cy >> 3, rows from r0 = cy & 7).
                Bands every 8 rows overlap 16-fold: card space is free, one
                command per frame is not.
  columns       NC = W / 8 column blocks per band, side by side on the card,
                so a band's columns k0..k0+16 are consecutive blocks.

The window for camera (cx, cy): band cy >> 3, columns cx >> 3 .. (cx + 127) >> 3,
16 blocks when cx % 8 == 0, else 17, one card::stream; the game copies each
block into the framebuffer as it arrives (src/engine/World.cpp).

The world is stored TPH times, once for each ambient phase (since M8c 4:
the whole bitmap painted again with the reeds swaying, the water moving,
the gnats buzzing), and L times over, once for each world layer (2: the
swamp and the healed swamp); the game reads the same window from another
phase's or layer's blocks:

  lba(b, k, layer, phase) = first + 1 + ((b * L + layer) * TPH + phase) * NC + k

Section layout: block 0 the header, then the data blocks in that order.
Header (little-endian; the rest of the block zero):
  0 "WRLD"  4 u16 W  6 u16 H  8 u16 NC  10 u16 NB  12 u8 TPH  13 u8 L
  14 u16 column width (8)  16 u16 band rows (128)  18 u16 band step (8)
  20 u16 view width (128)  22 u16 view height (120)

NB = (H - 120) / 8 + 1 bands, so the bitmap is padded at the bottom to
8 (NB - 1) + 128 rows (the padding is never on screen).
"""
import struct

BLOCK = 512
COLW = 8                # pixels per column block
ROWS = 128              # rows per band (per block)
STEP = 8                # band spacing in rows
VIEW_W, VIEW_H = 128, 120
MAGIC = b"WRLD"
HEAD = struct.Struct("<4sHHHHBBHHHHH")


class Geometry:
    def __init__(self, w, h, tph=1, layers=1):
        if w % COLW or w < VIEW_W or h < VIEW_H or (h - VIEW_H) % STEP:
            raise ValueError("world %dx%d: width a multiple of %d, height 120 + a multiple of %d"
                             % (w, h, COLW, STEP))
        self.w, self.h = w, h
        self.tph, self.layers = tph, layers
        self.nc = w // COLW
        self.nb = (h - VIEW_H) // STEP + 1
        self.padded_h = STEP * (self.nb - 1) + ROWS

    @property
    def data_blocks(self):
        return self.nb * self.layers * self.tph * self.nc

    @property
    def blocks(self):
        return 1 + self.data_blocks

    def block(self, b, k, layer=0, phase=0):
        """A data block's index within the section (the header is block 0)."""
        return 1 + ((b * self.layers + layer) * self.tph + phase) * self.nc + k

    def window(self, cx, cy):
        """(band, r0, k0, n): the blocks the game reads for camera (cx, cy)."""
        if not (0 <= cx <= self.w - VIEW_W and 0 <= cy <= self.h - VIEW_H):
            raise ValueError("camera (%d, %d) outside [0, %d] x [0, %d]"
                             % (cx, cy, self.w - VIEW_W, self.h - VIEW_H))
        k0 = cx >> 3
        return cy >> 3, cy & 7, k0, ((cx + VIEW_W - 1) >> 3) - k0 + 1

    def header(self):
        b = bytearray(BLOCK)
        HEAD.pack_into(b, 0, MAGIC, self.w, self.h, self.nc, self.nb, self.tph, self.layers,
                       COLW, ROWS, STEP, VIEW_W, VIEW_H)
        return bytes(b)


def parse_header(data):
    magic, w, h, nc, nb, tph, layers, colw, rows, step, vw, vh = HEAD.unpack_from(data, 0)
    if magic != MAGIC or (colw, rows, step, vw, vh) != (COLW, ROWS, STEP, VIEW_W, VIEW_H):
        raise ValueError("not a WRLD header")
    g = Geometry(w, h, tph, layers)
    if (g.nc, g.nb) != (nc, nb):
        raise ValueError("WRLD header: NC %d NB %d, the geometry says %d %d" % (nc, nb, g.nc, g.nb))
    return g


def fb_rows(idx, w, rows):
    """Rows of palette indices (row-major, width w) as gfx_fb bytes: one
    bytes object of w / 2 per row."""
    out = []
    for y in range(rows):
        r = idx[y * w:(y + 1) * w]
        out.append(bytes(a | (b << 4) for a, b in zip(r[0::2], r[1::2])))
    return out


def pack(images, geo):
    """The WRLD section's bytes. images[layer][phase]: geo.w x geo.padded_h
    palette indices (bytes, row-major; images shorter than padded_h are padded
    with their last row)."""
    if len(images) != geo.layers or any(len(ph) != geo.tph for ph in images):
        raise ValueError("pack: %d layers x %d phases expected" % (geo.layers, geo.tph))
    cols = []                           # cols[layer][phase][k]: the column's rows, 4 B each, top to bottom
    for ph in images:
        row = []
        for idx in ph:
            n = len(idx) // geo.w
            if n * geo.w != len(idx) or n < geo.h:
                raise ValueError("pack: an image of %d bytes is not %d x >= %d" % (len(idx), geo.w, geo.h))
            if max(idx) > 15:
                raise ValueError("pack: a pixel is not a palette index")
            if n < geo.padded_h:
                idx = bytes(idx) + bytes(idx[(n - 1) * geo.w:n * geo.w]) * (geo.padded_h - n)
            rows = fb_rows(idx, geo.w, geo.padded_h)
            row.append([b"".join(r[4 * k:4 * k + 4] for r in rows) for k in range(geo.nc)])
        cols.append(row)
    out = [geo.header()]
    for b in range(geo.nb):
        for layer in range(geo.layers):
            for phase in range(geo.tph):
                c = cols[layer][phase]
                lo, hi = 4 * STEP * b, 4 * (STEP * b + ROWS)
                out.extend(c[k][lo:hi] for k in range(geo.nc))
    data = b"".join(out)
    assert len(data) == geo.blocks * BLOCK
    return data


# --- reading back (tests, previews) -------------------------------------------

def pixel(section, geo, x, y, layer=0, phase=0):
    """World pixel (x, y) as stored: from the band that starts nearest above."""
    b = min(y // STEP, geo.nb - 1)
    o = geo.block(b, x // COLW, layer, phase) * BLOCK + 4 * (y - STEP * b) + (x % COLW) // 2
    v = section[o]
    return v >> 4 if x & 1 else v & 15


def window_reference(section, geo, cx, cy, layer=0, phase=0):
    """The 128 x 120 view at (cx, cy), pixel by pixel from the blocks the game
    reads (band cy >> 3 only)."""
    b, r0, k0, n = geo.window(cx, cy)
    out = bytearray(VIEW_W * VIEW_H)
    for y in range(VIEW_H):
        for x in range(VIEW_W):
            wx = cx + x
            o = geo.block(b, wx // COLW, layer, phase) * BLOCK + 4 * (r0 + y) + (wx % COLW) // 2
            v = section[o]
            out[y * VIEW_W + x] = v >> 4 if wx & 1 else v & 15
    return out


def window_game(section, geo, cx, cy, layer=0, phase=0):
    """The same view the way World.cpp builds it: each block in read order,
    as little-endian words, shifted into the framebuffer's words (p = cx & 7:
    p == 0 a plain store; else block word w goes in as fb[j-1] |= w << (32-4p)
    (its low 4p bits kept) and fb[j] = w >> 4p, j = k - k0, clipped to the
    16 screen words). Returns the 128 x 120 indices, so tests can compare
    the algorithm with window_reference()."""
    b, r0, k0, n = geo.window(cx, cy)
    p = cx & 7
    fb = [[0x55555555] * 16 for _ in range(VIEW_H)]     # junk: every word must be written
    first = geo.block(b, k0, layer, phase)
    sh = 4 * p
    lo = (1 << (32 - sh)) - 1 if p else 0               # the left word's pixels kept
    for j in range(n):
        blk = section[(first + j) * BLOCK:(first + j + 1) * BLOCK]
        for y in range(VIEW_H):
            w = struct.unpack_from("<I", blk, 4 * (r0 + y))[0]
            row = fb[y]
            if not p:
                row[j] = w
                continue
            if j >= 1:
                row[j - 1] = (row[j - 1] & lo) | ((w << (32 - sh)) & 0xFFFFFFFF)
            if j < 16:
                row[j] = w >> sh
    out = bytearray(VIEW_W * VIEW_H)
    for y in range(VIEW_H):
        for jj in range(16):
            w = fb[y][jj]
            for i in range(8):
                out[y * VIEW_W + 8 * jj + i] = (w >> (4 * i)) & 15
    return out
