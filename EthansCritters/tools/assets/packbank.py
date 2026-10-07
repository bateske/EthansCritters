#!/usr/bin/env python3
"""Ethan's Critters: the sprite bank (card section BANK) and its flash index.

Every animation of the sprite sheets (sheets.py) goes on the card as raw
4 bpp frames the game draws with gfx_blit4k() straight from its RAM cache
(src/engine/Spool.cpp), except the fishes (not in the game), the birds and
the ball (not yet in it), the giant toad (the boss: its own section,
bosspack.py), the Chameleon's Disappear /
Reappear (the game dithers Idle instead) and blank trailing frames; then
the art built offline (props.py: the nests, the mushroom pickups, the spawn
puff), after the sheets so their clip numbers stay put.

A frame, 8 + 4 * wWords * h bytes, 4-byte aligned:
    0 u8 wWords   row length in 32-bit words, ceil(w / 8)
    1 u8 h        rows
    2 i8 ox       the art's top-left, relative to the sheet's pivot (the
    3 i8 oy       actor's position, the middle of its feet line)
    4 u8 csum8    the pixel bytes summed, mod 256
    5 u8 flags    0 (reserved)
    6 u8 w        the true width in pixels (<= 8 * wWords)
    7 u8 0
    8 pixels      h rows of wWords words: the frame's opaque box, packed like
                  gfx_fb (even x in the low nibble), 15 (the key) outside the art

A clip (one animation) is a run of whole blocks: k = 512 // slot frames a
block, slot = its biggest frame, frame j of block b at byte j * slot, so a
frame never straddles blocks and a block holds k neighbouring frames (one
read gets the next ones too). A clip's frames that are pixel-identical are
stored once, and a frame map (BANK_FMAP) says which stored frame each
played frame is. The clips of a sheet are adjacent, sheet after sheet.

The flash side, src/assets/Bank.{h,cpp} (written only when changed): the
clip table BANK_CLIPS[CLIP_<SHEET>_<ANIM>] {first block (in the section),
frames as played, frames a block, slot / 4, ms / 10, flags, frame map offset,
frames stored, active frames, size table offset}, the frame map, every
stored frame's size (the cache reserves exactly that), the sheets' first
clips and the clip names (for the sprite lab). (The remap tables, the red
hit flash among them, are the palette's: src/assets/Palette.h.)

    python EthansCritters/tools/assets/packbank.py [--preview]
            writes Bank.{h,cpp}; --preview: out/art/bank.png, every stored
            frame decoded back from the section
tools/mkcard.py packs the section (pack()) into CRITTERS.DAT.
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import palette as P     # noqa: E402
import props           # noqa: E402
import quantise as Q    # noqa: E402
import sheets as S      # noqa: E402

BLOCK = 512
HEADER = struct.Struct("<BBbbBBBB")     # wWords, h, ox, oy, csum8, flags, w, 0
# Left out: the fishes (not in the game), the toad (bosspack.py), and the
# birds and the ball until something uses them (a stretch goal: their clip
# table entries are flash the boss needed, M7).
SKIP_SHEETS = ("fishes", "toad", "pidgeon", "seagull", "ball")
LOOP_ROLES = {"idle", "idle2", "walk", "carry", "swim_idle", "swim", "swim_carry", "fly", "glide",
              "glide2", "sleep", "eat"}
F_LOOP = 0x01                           # clip flags (BANK_LOOP)
NO_MAP = 0xFF
SRC_ASSETS = HERE.parents[1] / "src" / "assets"

PREVIEW = S.ROOT / "out" / "art" / "bank.png"


class Frame:
    def __init__(self, ww, h, ox, oy, w, pixels):
        self.ww, self.h, self.ox, self.oy, self.w = ww, h, ox, oy, w
        self.pixels = pixels                # bytes, h * ww * 4

    @property
    def csum(self):
        return sum(self.pixels) & 0xFF

    def to_bytes(self):
        return HEADER.pack(self.ww, self.h, self.ox, self.oy, self.csum, 0, self.w, 0) + self.pixels

    def __len__(self):
        return HEADER.size + len(self.pixels)

    def __eq__(self, other):
        return self.to_bytes() == other.to_bytes()

    def __hash__(self):
        return hash(self.to_bytes())


class Clip:
    def __init__(self, sheet, anim):
        self.sheet, self.anim = sheet, anim
        self.name = "CLIP_%s_%s" % (sheet.key.upper(), anim.id.upper())
        self.stored = []                    # distinct frames, in first-played order
        self.fmap = []                      # played frame -> stored index
        self.first = 0                      # block, in the section
        self.map_at = NO_MAP
        self.size_at = 0                    # its stored frames' sizes in BANK_FSIZE

    @property
    def slot(self):
        return (max(len(f) for f in self.stored) + 3) & ~3

    @property
    def per_block(self):
        return BLOCK // self.slot

    @property
    def blocks(self):
        return -(-len(self.stored) // self.per_block)

    @property
    def flags(self):
        return F_LOOP if self.anim.role in LOOP_ROLES else 0

    @property
    def active(self):
        a = self.anim.active
        return (a[0] << 4 | a[1]) if a else 0

    def block_of(self, s):
        return self.first + s // self.per_block, (s % self.per_block) * self.slot


def cut(idx, sheet_w, sheet, row, col):
    """The cell's opaque box as a Frame, or None when the cell is empty."""
    cw, ch = sheet.cell
    x0c, y0c = col * cw, row * ch
    xs, ys = [], []
    for y in range(ch):
        line = idx[(y0c + y) * sheet_w + x0c:(y0c + y) * sheet_w + x0c + cw]
        hit = [x for x in range(cw) if line[x] != P.KEY]
        if hit:
            xs += (hit[0], hit[-1])
            ys.append(y)
    if not ys:
        return None
    x0, x1, y0, y1 = min(xs), max(xs), ys[0], ys[-1]
    w, h = x1 - x0 + 1, y1 - y0 + 1
    ww = (w + 7) // 8
    out = bytearray()
    for y in range(y0, y1 + 1):
        base = (y0c + y) * sheet_w + x0c
        px = [idx[base + x0 + i] if i < w else P.KEY for i in range(ww * 8)]
        for i in range(0, ww * 8, 2):
            out.append(px[i] | px[i + 1] << 4)
    ox, oy = x0 - sheet.pivot[0], y0 - sheet.pivot[1]
    if ww > 16 or h > 255 or not -128 <= ox < 128 or not -128 <= oy < 128:
        raise ValueError("%s row %d col %d: %dx%d at %d,%d does not fit a frame header"
                         % (sheet.key, row, col, w, h, ox, oy))
    return Frame(ww, h, ox, oy, w, bytes(out))


def banked_sheets():
    return [s for s in S.ALL if s.kind == "sprite" and s.key not in SKIP_SHEETS] + props.built()


def sheet_pixels(sheet):
    """(indices, sheet width): a PNG sheet quantised, or a built one's own."""
    if hasattr(sheet, "indices"):
        return sheet.indices()
    return Q.quantise_sheet(sheet), Q.sheet_image(sheet).size[0]


def build():
    """The clips, laid out: [Clip] in card order."""
    clips, block = [], 0
    for sheet in banked_sheets():
        idx, sheet_w = sheet_pixels(sheet)
        for a in sheet.anims:
            if a.alpha_of:
                continue
            c = Clip(sheet, a)
            for f in range(a.drawn):
                fr = cut(idx, sheet_w, sheet, a.row, a.col + f)
                if fr is None:
                    raise ValueError("%s: frame %d of %s is empty (mark it blank in sheets.py)"
                                     % (sheet.key, f, a.name))
                if fr in c.stored:
                    c.fmap.append(c.stored.index(fr))
                else:
                    c.fmap.append(len(c.stored))
                    c.stored.append(fr)
            if c.slot > BLOCK:
                raise ValueError("%s: a frame of %d B is over a block" % (c.name, c.slot))
            if len(c.fmap) > 255:
                raise ValueError("%s: over 255 frames" % c.name)
            c.first = block
            block += c.blocks
            clips.append(c)
    fmap_at = size_at = 0
    for c in clips:
        if c.fmap != list(range(len(c.fmap))):
            c.map_at = fmap_at
            fmap_at += len(c.fmap)
        c.size_at = size_at
        size_at += len(c.stored)
    if len(clips) > 255 or block > 0xFFFF:
        raise ValueError("bank too big: %d clips, %d blocks" % (len(clips), block))
    if fmap_at > NO_MAP:
        raise ValueError("frame maps of %d B: BankClip.map is a byte (%d at most)" % (fmap_at, NO_MAP))
    return clips


def pack(clips):
    """The BANK section's bytes."""
    data = bytearray()
    for c in clips:
        assert len(data) == c.first * BLOCK
        for b in range(c.blocks):
            blk = bytearray(BLOCK)
            for j, fr in enumerate(c.stored[b * c.per_block:(b + 1) * c.per_block]):
                raw = fr.to_bytes()
                blk[j * c.slot:j * c.slot + len(raw)] = raw
            data += blk
    return bytes(data)


def decode(data, c, s):
    """Stored frame s of clip c from the section's bytes: (header tuple, pixel rows [[index]])."""
    b, off = c.block_of(s)
    o = b * BLOCK + off
    ww, h, ox, oy, csum, flags, w, z = HEADER.unpack_from(data, o)
    rows = []
    for y in range(h):
        row = data[o + 8 + y * ww * 4:o + 8 + (y + 1) * ww * 4]
        rows.append([v for byte in row for v in (byte & 15, byte >> 4)])
    return (ww, h, ox, oy, csum, flags, w, z), rows


# --- the generated C++ ----------------------------------------------------------

BANNER = ("// GENERATED by tools/assets/packbank.py (tools/mkcard.py runs it). Do not edit:\n"
          "// re-run it.\n")


def header_text(clips):
    sheets = []
    for c in clips:
        if not sheets or sheets[-1] != c.sheet:
            sheets.append(c.sheet)
    total = sum(c.blocks for c in clips)
    frames = sum(len(c.stored) for c in clips)
    L = [BANNER,
         "//\n",
         "// The sprite bank on the card (section BANK of CRITTERS.DAT): where each clip\n",
         "// (one animation of a sheet) is and how to play it. A frame on the card is an\n",
         "// 8 B header {wWords, h, ox, oy, csum8, flags, w, 0} and h rows of wWords words\n",
         "// (gfx_blit4k's format, 15 transparent); ox, oy put its top-left relative to\n",
         "// the actor's position (the sheet's pivot, the middle of the feet line). A\n",
         "// clip's stored frames sit bankPerBlock() to a block at multiples of its slot\n",
         "// (slot4 * 4 bytes), from block `first` of the section; BANK_FMAP maps a played\n",
         "// frame to a stored one when the clip repeats frames (map != BANK_NO_MAP).\n",
         "#pragma once\n#include <stdint.h>\n\n",
         "struct BankClip {\n",
         "    uint16_t first;     // first block, counted from the section's start\n",
         "    uint8_t frames;     // as played\n",
         "    uint8_t slot4;      // bytes a frame takes in its block (its biggest frame) / 4\n",
         "    uint8_t msPer10;    // frame time / 10 ms\n",
         "    uint8_t map;        // offset in BANK_FMAP, or BANK_NO_MAP (frame f is stored f)\n",
         "    uint8_t stored;     // distinct frames on the card\n",
         "    uint8_t active;     // attack: first << 4 | last hurting frame; 0 none\n",
         "    uint16_t sizes;     // offset in BANK_FSIZE of its stored frames' sizes\n",
         "};\n\n",
         "constexpr uint8_t BANK_NO_MAP = 0x%02X;\n" % NO_MAP,
         "constexpr uint16_t BANK_BLOCKS = %d;      // the section, %d stored frames\n" % (total, frames),
         "constexpr uint8_t BANK_FRAME_HEADER = 8;\n",
         "constexpr uint16_t BANK_MAX_SLOT = %d;\n\n" % max(c.slot for c in clips),
         "enum : uint8_t {\n"]
    for i, c in enumerate(clips):
        L.append("    %-34s = %2d,   // %2d frames, %d stored, %d a block (%d B)\n"
                 % (c.name, i, len(c.fmap), len(c.stored), c.per_block, c.slot))
    L.append("    CLIP_COUNT = %d\n};\n\n" % len(clips))
    L.append("enum : uint8_t {\n")
    for i, s in enumerate(sheets):
        L.append("    SHEET_%-10s = %d,\n" % (s.key.upper(), i))
    L.append("    SHEET_COUNT = %d\n};\n\n" % len(sheets))
    L += ["extern const BankClip BANK_CLIPS[CLIP_COUNT];\n",
          "extern const uint8_t BANK_FMAP[];\n",
          "extern const uint8_t BANK_FSIZE[];    // a stored frame's bytes / 4 (header included)\n",
          "extern const uint8_t BANK_SHEET_CLIP[SHEET_COUNT + 1];   // a sheet's first clip; [SHEET_COUNT] = CLIP_COUNT\n",
          "extern const char *const BANK_CLIP_NAME[CLIP_COUNT];   // \"RACCOON RUN\" (the sprite lab)\n",
          "\n",
          "// Stored frame of played frame f.\n",
          "inline uint8_t bankStored(const BankClip &c, uint8_t f) {\n",
          "    return c.map == BANK_NO_MAP ? f : BANK_FMAP[c.map + f];\n",
          "}\n\n",
          "// Stored frames a block (not a field of the table: 2 B a clip less, M8d).\n",
          "inline uint32_t bankPerBlock(const BankClip &c) { return 128u / c.slot4; }\n\n",
          "// Bytes of stored frame s, header included.\n",
          "inline uint32_t bankSize(const BankClip &c, uint32_t s) { return BANK_FSIZE[c.sizes + s] * 4u; }\n\n",
          "// 60 Hz ticks a frame is shown (its ms, rounded; at least 1).\n",
          "inline uint32_t bankTicks(const BankClip &c) {\n",
          "    uint32_t n = (c.msPer10 * 3u + 2) / 5;\n",
          "    return n ? n : 1;\n",
          "}\n"]
    return "".join(L)


def source_text(clips):
    L = [BANNER, "#include \"Bank.h\"\n\n", "const BankClip BANK_CLIPS[CLIP_COUNT] = {\n"]
    for c in clips:
        assert 128 // (c.slot // 4) == c.per_block      # bankPerBlock()
        L.append("    {%4d, %2d, %3d, %2d, 0x%02X, %2d, 0x%02X, %3d},   // %s\n"
                 % (c.first, len(c.fmap), c.slot // 4, c.anim.ms // 10, c.map_at,
                    len(c.stored), c.active, c.size_at, c.name))
    L.append("};\n\n")
    L.append("const uint8_t BANK_FMAP[] = {\n")
    any_map = False
    for c in clips:
        if c.map_at != NO_MAP:
            any_map = True
            L.append("    %s  // %s\n" % (" ".join("%d," % v for v in c.fmap), c.name))
    if not any_map:
        L.append("    0\n")
    L.append("};\n\n")
    L.append("const uint8_t BANK_FSIZE[] = {\n")
    for c in clips:
        L.append("    %s  // %s\n" % (" ".join("%d," % (len(f) // 4) for f in c.stored), c.name))
    L.append("};\n\n")
    firsts, prev = [], None
    for i, c in enumerate(clips):
        if c.sheet is not prev:
            firsts.append(i)
            prev = c.sheet
    firsts.append(len(clips))
    L.append("const uint8_t BANK_SHEET_CLIP[SHEET_COUNT + 1] = {%s};\n\n" % ", ".join(str(v) for v in firsts))
    L.append("const char *const BANK_CLIP_NAME[CLIP_COUNT] = {\n")
    for c in clips:
        L.append("    \"%s %s\",\n" % (c.sheet.key.upper(), c.anim.id.replace("_", " ").upper()))
    L.append("};\n")
    return "".join(L)


def emit(clips, folder=SRC_ASSETS):
    """Writes Bank.h and Bank.cpp where they changed; returns the names written."""
    wrote = []
    for name, text in (("Bank.h", header_text(clips)), ("Bank.cpp", source_text(clips))):
        p = Path(folder) / name
        if p.exists() and p.read_text(encoding="utf-8") == text:
            continue
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        wrote.append(name)
    return wrote


def preview(clips, data, path=PREVIEW, scale=3):
    """Every stored frame decoded from the section, a clip a row, on a checker."""
    from PIL import Image
    cellw = max(c.sheet.cell[0] for c in clips)
    cellh = max(c.sheet.cell[1] for c in clips)
    cols = max(len(c.stored) for c in clips)
    img = Image.new("RGB", (cols * cellw, len(clips) * cellh), (40, 40, 40))
    pal = [P.rgb888(i) for i in range(16)]
    for r, c in enumerate(clips):
        for s in range(len(c.stored)):
            (ww, h, ox, oy, _, _, w, _), rows = decode(data, c, s)
            bx, by = s * cellw + cellw // 2, r * cellh + c.sheet.pivot[1] + (cellh - c.sheet.cell[1])
            for y, row in enumerate(rows):
                for x, v in enumerate(row):
                    if v != P.KEY:
                        img.putpixel((bx + ox + x, by + oy + y), pal[v])
    img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", action="store_true", help="also write out/art/bank.png")
    a = ap.parse_args(argv)
    clips = build()
    data = pack(clips)
    wrote = emit(clips)
    print("bank: %d clips, %d stored frames (%d played), %d blocks; %s"
          % (len(clips), sum(len(c.stored) for c in clips), sum(len(c.fmap) for c in clips),
             len(data) // BLOCK, ("wrote " + ", ".join(wrote)) if wrote else "Bank.{h,cpp} unchanged"))
    if a.preview:
        print("wrote", preview(clips, data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
