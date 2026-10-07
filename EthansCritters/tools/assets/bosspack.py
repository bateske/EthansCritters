#!/usr/bin/env python3
"""Ethan's Critters: the giant toad on the card (section TOAD) and its flash index.

The boss is too big for the frame cache (a frame is ~1.6 KB, the cache 3 KB):
the game streams the frame it shows straight from the card every drawn frame
and blits each block as it lands, while the next one comes by DMA (Path D,
src/game/Boss.cpp), so the section is laid out for that:

  every played frame of every toad row (sheets.py's TOAD: Idle, Idle 2,
  Movement, Attack, Damage, Death; blank trailing frames dropped), trimmed
  to its opaque box, raw 4 bpp packed like gfx_fb (even x in the low
  nibble), 15 (the key) outside the art, rows of wWords = ceil(w / 8)
  words; in row bands: a block holds rowsPerBlock = 128 // wWords whole
  rows (the rest of it is zero), and each frame starts on a fresh block.
  A frame pixel-identical to one already stored is stored once.

Then all of it again in red (palette.py's REMAP_RED applied to every pixel,
the same layout TOAD_RED blocks on): the hit flash. gfx_blit4k's REMAP does
the same at draw time, but on a frame this size it runs ~1.1 ms past the
blocks' DMA on the board (3.5 us a word-row against 1.1 plain), enough to
push every flashed frame of the fight over the 60 fps budget; card space is
free, so the flash is a second copy and costs nothing.

No header, no checksum: the flash table says where everything is, and the
card's build hash covers the bytes (CardIndex.h).

The flash side, src/assets/Boss.{h,cpp} (written only when changed):
TOAD_FRAME[played frame] = {first block (in the section), wWords, h, ox, oy}
with ox, oy the art's top-left relative to the sheet's pivot (the middle of
the toad's feet line); a row's first played frame (TOAD_IDLE, TOAD_HOP ...)
and its length. rowsPerBlock is 128 / wWords (toadRows()).

    python EthansCritters/tools/assets/bosspack.py [--preview]
            writes Boss.{h,cpp}; --preview: out/art/toad_card.png, every
            played frame decoded back from the section at its pivot
tools/mkcard.py packs the section (pack()) into CRITTERS.DAT.
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import palette as P     # noqa: E402
import quantise as Q    # noqa: E402
import sheets as S      # noqa: E402

BLOCK = 512
SRC_ASSETS = HERE.parents[1] / "src" / "assets"
PREVIEW = S.ROOT / "out" / "art" / "toad_card.png"
# The rows as the game names them (sheets.py's ids -> Boss.h's constants).
ROWS = (("idle", "TOAD_IDLE"), ("idle_2", "TOAD_BLINK"), ("movement", "TOAD_HOP"), ("attack", "TOAD_TONGUE"),
        ("damage", "TOAD_HURT"), ("death", "TOAD_DEATH"))


class Frame:
    def __init__(self, ww, h, ox, oy, pixels):
        self.ww, self.h, self.ox, self.oy = ww, h, ox, oy
        self.pixels = pixels                # h * ww * 4 bytes
        self.block = 0                      # in the section, once laid out

    @property
    def rows_per_block(self):
        return BLOCK // (4 * self.ww)

    @property
    def blocks(self):
        return -(-self.h // self.rows_per_block)

    def key(self):
        return (self.ww, self.h, self.ox, self.oy, self.pixels)


def cut(idx, sheet_w, sheet, row, col):
    """The cell's opaque box as a Frame (None: an empty cell)."""
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
    w = x1 - x0 + 1
    ww = (w + 7) // 8
    out = bytearray()
    for y in range(y0, y1 + 1):
        base = (y0c + y) * sheet_w + x0c
        px = [idx[base + x0 + i] if i < w else P.KEY for i in range(ww * 8)]
        for i in range(0, ww * 8, 2):
            out.append(px[i] | px[i + 1] << 4)
    ox, oy = x0 - sheet.pivot[0], y0 - sheet.pivot[1]
    if ww > 16 or y1 - y0 + 1 > 255 or not -128 <= ox < 128 or not -128 <= oy < 128:
        raise ValueError("toad row %d col %d: does not fit the frame table" % (row, col))
    return Frame(ww, y1 - y0 + 1, ox, oy, bytes(out))


def build(sheet=S.TOAD):
    """(played frames [Frame, shared when identical], rows [(const name, first, count)])."""
    idx = Q.quantise_sheet(sheet)
    sheet_w = Q.sheet_image(sheet).size[0]
    played, rows, stored, block = [], [], {}, 0
    for ident, name in ROWS:
        a = sheet.anim(ident)
        rows.append((name, len(played), a.drawn))
        for f in range(a.drawn):
            fr = cut(idx, sheet_w, sheet, a.row, a.col + f)
            if fr is None:
                raise ValueError("toad: frame %d of %s is empty (mark it blank in sheets.py)" % (f, a.name))
            k = fr.key()
            if k in stored:
                fr = stored[k]
            else:
                fr.block = block
                block += fr.blocks
                stored[k] = fr
            played.append(fr)
    if block > 255:
        raise ValueError("TOAD: %d blocks, the table's block is a byte" % block)
    return played, rows


def distinct(played):
    seen, out = set(), []
    for fr in played:
        if id(fr) not in seen:
            seen.add(id(fr))
            out.append(fr)
    return out


def red(pixels):
    """Packed pixels through REMAP_RED (the key stays the key)."""
    t = P.remap_red()
    return bytes(t[b & 15] | t[b >> 4] << 4 for b in pixels)


def pack(played):
    """The TOAD section's bytes: every picture, then every picture in red."""
    data = bytearray()
    for flash in (False, True):
        for fr in distinct(played):
            px = red(fr.pixels) if flash else fr.pixels
            rpb, row = fr.rows_per_block, fr.ww * 4
            for b in range(fr.blocks):
                chunk = px[b * rpb * row:(b + 1) * rpb * row]
                data += chunk + bytes(BLOCK - len(chunk))
    return bytes(data)


def decode(data, fr, flash=False):
    """A frame's pixel rows [[index]] read back from the section as the game
    reads it (flash: its red copy)."""
    rpb, row, rows = fr.rows_per_block, fr.ww * 4, []
    base = fr.block + (len(data) // BLOCK // 2 if flash else 0)
    for y in range(fr.h):
        o = (base + y // rpb) * BLOCK + (y % rpb) * row
        rows.append([v for byte in data[o:o + row] for v in (byte & 15, byte >> 4)])
    return rows


# --- the generated C++ ----------------------------------------------------------------

BANNER = ("// GENERATED by tools/assets/bosspack.py (tools/mkcard.py runs it). Do not edit:\n"
          "// re-run it.\n")


def header_text(played, rows):
    d = distinct(played)
    L = [BANNER,
         "//\n",
         "// The giant toad on the card (section TOAD of CRITTERS.DAT), streamed every\n",
         "// drawn frame and blitted from the landing buffer block by block\n",
         "// (src/game/Boss.cpp). A frame is h rows of wWords words (gfx_blit4k's\n",
         "// format, 15 transparent) in row bands: 128 / wWords whole rows a block, from\n",
         "// its first block; ox, oy put its top-left relative to the toad's position\n",
         "// (the middle of its feet line). Played frames that are the same picture\n",
         "// share their blocks.\n",
         "#pragma once\n#include <stdint.h>\n\n",
         "struct ToadFrame { uint8_t block, wWords, h; int8_t ox, oy; };\n\n",
         "constexpr uint16_t TOAD_RED = %d;         // %d pictures for %d played frames; each again in red\n"
         % (sum(f.blocks for f in d), len(d), len(played)),
         "                                         // (the hit flash) this many blocks on\n",
         "constexpr uint8_t TOAD_MAX_BLOCKS = %d;    // the most a frame takes\n" % max(f.blocks for f in d),
         "\n// Each row's first played frame, and its length.\n",
         "enum : uint8_t {\n"]
    for name, first, n in rows:
        L.append("    %-12s = %2d,  %-14s = %d,\n" % (name, first, name + "_N", n))
    L.append("    TOAD_FRAMES = %d\n};\n\n" % len(played))
    L += ["extern const ToadFrame TOAD_FRAME[TOAD_FRAMES];\n\n",
          "// Whole rows a block of frame f holds.\n",
          "inline uint32_t toadRows(const ToadFrame &f) { return 128u / f.wWords; }\n"]
    return "".join(L)


def source_text(played, rows):
    L = [BANNER, "#include \"Boss.h\"\n\n", "const ToadFrame TOAD_FRAME[TOAD_FRAMES] = {\n"]
    names = {first + i: "%s %d" % (name, i) for name, first, n in rows for i in range(n)}
    for i, fr in enumerate(played):
        L.append("    {%3d, %2d, %2d, %3d, %3d},   // %s: %d blocks\n"
                 % (fr.block, fr.ww, fr.h, fr.ox, fr.oy, names[i], fr.blocks))
    L.append("};\n")
    return "".join(L)


def emit(played, rows, folder=SRC_ASSETS):
    """Writes Boss.h and Boss.cpp where they changed; returns the names written."""
    wrote = []
    for name, text in (("Boss.h", header_text(played, rows)), ("Boss.cpp", source_text(played, rows))):
        p = Path(folder) / name
        if p.exists() and p.read_text(encoding="utf-8") == text:
            continue
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        wrote.append(name)
    return wrote


def preview(played, rows, data, path=PREVIEW, scale=2):
    """Every played frame decoded from the section, a row of the sheet a row."""
    from PIL import Image
    cw, ch = S.TOAD.cell
    px, py = S.TOAD.pivot
    cols = max(n for _, _, n in rows)
    img = Image.new("RGB", (cols * cw, len(rows) * ch), (60, 60, 70))
    pal = [P.rgb888(i) for i in range(16)]
    for r, (name, first, n) in enumerate(rows):
        for i in range(n):
            fr = played[first + i]
            for y, row in enumerate(decode(data, fr)):
                for x, v in enumerate(row):
                    if v != P.KEY:
                        img.putpixel((i * cw + px + fr.ox + x, r * ch + py + fr.oy + y), pal[v])
    img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", action="store_true", help="also write out/art/toad_card.png")
    a = ap.parse_args(argv)
    played, rows = build()
    data = pack(played)
    wrote = emit(played, rows)
    d = distinct(played)
    print("toad: %d played frames, %d pictures (and red), %d blocks (%d a frame at most); %s"
          % (len(played), len(d), len(data) // BLOCK, max(f.blocks for f in d),
             ("wrote " + ", ".join(wrote)) if wrote else "Boss.{h,cpp} unchanged"))
    if a.preview:
        print("wrote", preview(played, rows, data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
