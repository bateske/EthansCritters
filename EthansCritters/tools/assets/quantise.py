#!/usr/bin/env python3
"""Ethan's Critters: the quantiser (a library for the packers, and a CLI).

Maps the source art onto THE palette (palette.py):

  sprites  alpha < 128 -> 15 (the transparent key); an opaque pixel takes its
           sheet's override if it has one, else the nearest colour by CIELAB
           delta-E (CIEDE2000) among 0..14. Never 15. Transparency is decided
           by alpha only: alpha-0 pixels carry junk RGB.
  tiles    all 16 colours (15 is the swamp water). There is no key: a pixel
           with alpha < 128 comes back as HOLE (255, not a colour), because
           the world painter composites its layers before it quantises.

The Chameleon's Disappear/Reappear frames (whole-frame alpha) are reported,
not quantised: the game dithers the Idle frames instead (gfx_blit4k DITHER).

CLI (from the repository root):
  python EthansCritters/tools/assets/quantise.py --preview   out/art/: palette.png,
         <sheet>_preview.png, tileset_preview.png, ground_check.png, remaps.png, report.txt
  python EthansCritters/tools/assets/quantise.py --header    EthansCritters/src/assets/Palette.{h,cpp}:
         the colours and the remap tables (REMAP_RED ..., palette.py's REMAPS)
"""
import argparse
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import palette as P     # noqa: E402
import sheets as S      # noqa: E402

HOLE = 255              # tiles: a transparent pixel
ALPHA_CUT = 128
OUT = S.ROOT / "out" / "art"
SRC_ASSETS = S.GAME / "src" / "assets"
DE_WARN = 12.0          # report: a mapping this far off is flagged


# --- colour mapping -----------------------------------------------------------

def hex_rgb(h):
    h = h.lstrip("#").lower()
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def rgb_hex(rgb):
    return "%02x%02x%02x" % tuple(rgb[:3])


_near = {}


def nearest(rgb, sprite=True):
    """(index, delta-E) of the nearest palette colour; sprites never get 15."""
    key = (tuple(rgb[:3]), sprite)
    hit = _near.get(key)
    if hit is None:
        lab = P.lab(rgb)
        hit = None
        for i in range(P.COUNT):
            if sprite and i == P.KEY:
                continue
            d = P.delta_e(lab, P.LAB[i])
            if hit is None or d < hit[1]:
                hit = (i, d)
        _near[key] = hit
    return hit


def norm_overrides(overrides, sprite=True):
    """{hex or (r,g,b): name or index} -> {(r,g,b): index}."""
    out = {}
    for k, v in (overrides or {}).items():
        rgb = hex_rgb(k) if isinstance(k, str) else tuple(k[:3])
        i = P.index(v)
        if not 0 <= i < P.COUNT:
            raise ValueError("override %s -> %r is not a palette index" % (k, v))
        if sprite and i == P.KEY:
            raise ValueError("override %s -> %d: 15 is the sprite key" % (k, i))
        out[rgb] = i
    return out


def colour_index(rgb, ov, sprite=True):
    """(index, delta-E, 'override' | 'nearest') for one opaque colour."""
    rgb = tuple(rgb[:3])
    if rgb in ov:
        i = ov[rgb]
        return i, P.delta_e(P.lab(rgb), P.LAB[i]), "override"
    i, d = nearest(rgb, sprite)
    return i, d, "nearest"


def _rgba(img):
    from PIL import Image
    if isinstance(img, (str, Path)):
        img = Image.open(img)
    return img if img.mode == "RGBA" else img.convert("RGBA")


def map_image(rgba, sheet_overrides=None, sprite=True):
    """Index array (bytearray, row-major, width*height) for an RGBA image.
    sprite: alpha < 128 -> 15, opaque -> 0..14. tiles: alpha < 128 -> HOLE."""
    im = _rgba(rgba)
    ov = norm_overrides(sheet_overrides, sprite)
    data = im.tobytes()
    out = bytearray(len(data) // 4)
    clear = P.KEY if sprite else HOLE
    memo = {}
    for n in range(len(out)):
        o = n * 4
        if data[o + 3] < ALPHA_CUT:
            out[n] = clear
            continue
        k = data[o:o + 3]
        i = memo.get(k)
        if i is None:
            i = memo[k] = colour_index((k[0], k[1], k[2]), ov, sprite)[0]
        out[n] = i
    return out


# --- sheets -------------------------------------------------------------------

_images = {}


def sheet_image(sheet):
    im = _images.get(sheet.key)
    if im is None:
        im = _images[sheet.key] = _rgba(sheet.path)
    return im


def alpha_cells(sheet):
    """(row, col) of the frames the game dithers instead of drawing."""
    return {(a.row, a.col + f) for a in sheet.anims if a.alpha_of for f in range(a.frames)}


def _opaque_pixels(sheet):
    """Yields (x, y, rgb) of every pixel quantised (alpha >= 128, not in an alpha frame)."""
    im = sheet_image(sheet)
    w, h = im.size
    cw, ch = sheet.cell
    skip = alpha_cells(sheet)
    data = im.tobytes()
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            if data[o + 3] >= ALPHA_CUT and (y // ch, x // cw) not in skip:
                yield x, y, (data[o], data[o + 1], data[o + 2])


def sheet_colours(sheet):
    return Counter(rgb for _, _, rgb in _opaque_pixels(sheet))


def sheet_map(sheet):
    """{rgb: (index, delta-E, how, pixels)} for every quantised source colour."""
    sprite = sheet.kind == "sprite"
    ov = norm_overrides(sheet.overrides, sprite)
    return {rgb: colour_index(rgb, ov, sprite) + (n,) for rgb, n in sheet_colours(sheet).items()}


def quantise_sheet(sheet):
    """The whole sheet as indices; alpha frames are left transparent."""
    sprite = sheet.kind == "sprite"
    im = sheet_image(sheet)
    idx = map_image(im, sheet.overrides, sprite)
    cw, ch = sheet.cell
    w = im.size[0]
    clear = P.KEY if sprite else HOLE
    for r, c in alpha_cells(sheet):
        for y in range(r * ch, r * ch + ch):
            idx[y * w + c * cw:y * w + c * cw + cw] = bytes([clear]) * cw
    return idx


def merged_neighbours(sheet, cmap):
    """Different source colours that touch (4-neighbours) and became one index:
    the detail the quantiser lost. Counter of (rgbA, rgbB) -> touching edges."""
    im = sheet_image(sheet)
    w, h = im.size
    cw, ch = sheet.cell
    skip = alpha_cells(sheet)
    data = im.tobytes()

    def px(x, y):
        o = (y * w + x) * 4
        if data[o + 3] < ALPHA_CUT or (y // ch, x // cw) in skip:
            return None
        return data[o], data[o + 1], data[o + 2]

    lost = Counter()
    for y in range(h):
        for x in range(w):
            a = px(x, y)
            if a is None:
                continue
            for b in ((px(x + 1, y) if x + 1 < w else None), (px(x, y + 1) if y + 1 < h else None)):
                if b is not None and b != a and b in cmap and cmap[a][0] == cmap[b][0]:
                    lost[tuple(sorted((a, b)))] += 1
    return lost


def ramp_checks(sheet, cmap):
    """[(label, [(hex, index)], distinct, needed, ok)] for the sheet's ramps."""
    sprite = sheet.kind == "sprite"
    ov = norm_overrides(sheet.overrides, sprite)
    out = []
    for label, hexes, need in sheet.ramps:
        got = []
        for hx in hexes:
            rgb = hex_rgb(hx)
            i = cmap[rgb][0] if rgb in cmap else colour_index(rgb, ov, sprite)[0]
            got.append((hx, i))
        distinct = len({i for _, i in got})
        out.append((label, got, distinct, need, distinct >= need))
    return out


def alpha_report(sheet):
    """Lines describing the alpha frames, and any stray partial alpha."""
    im = sheet_image(sheet)
    w = im.size[0]
    cw, ch = sheet.cell
    data = im.tobytes()

    def cell(r, c):
        rows = []
        for y in range(r * ch, r * ch + ch):
            o = (y * w + c * cw) * 4
            rows.append(data[o:o + cw * 4])
        return b"".join(rows)

    lines = []
    for a in sheet.anims:
        if not a.alpha_of:
            continue
        src = sheet.anim(a.alpha_of)
        parts = []
        for f in range(a.frames):
            c = cell(a.row, a.col + f)
            alphas = sorted({c[i + 3] for i in range(0, len(c), 4)} - {0})
            match = "?"
            for g in range(src.frames):
                d = cell(src.row, src.col + g)
                if all((c[i + 3] > 0) == (d[i + 3] == 255) and (c[i + 3] == 0 or c[i:i + 3] == d[i:i + 3])
                       for i in range(0, len(c), 4)):
                    match = str(g)
                    break
            level = (alphas[-1] * 8 + 127) // 255 if alphas else 0
            parts.append("f%d=%s@%s(dither %d/8)" % (f, src.id + match, "/".join(map(str, alphas)) or "0", level))
        lines.append("%s: whole-frame alpha, not quantised; drawn as %s dithered: %s"
                     % (a.name, src.name, " ".join(parts)))
    skip = alpha_cells(sheet)
    stray = Counter()
    hgt = im.size[1]
    for y in range(hgt):
        for x in range(w):
            al = data[(y * w + x) * 4 + 3]
            if 0 < al < 255 and (y // ch, x // cw) not in skip:
                stray[(y // ch, x // cw)] += 1
    for (r, c), n in sorted(stray.items()):
        lines.append("WARNING: %d partly transparent pixels in cell row %d col %d (cut at alpha %d)"
                     % (n, r, c, ALPHA_CUT))
    return lines


def cell_empty(sheet, r, c, cut=1):
    """True if no pixel of the cell has alpha >= cut."""
    im = sheet_image(sheet)
    cw, ch = sheet.cell
    box = im.getchannel("A").crop((c * cw, r * ch, c * cw + cw, r * ch + ch))
    return box.getextrema()[1] < cut


# --- previews -----------------------------------------------------------------

def _font(size):
    from PIL import ImageFont
    try:
        return ImageFont.load_default(size)
    except TypeError:       # Pillow without FreeType sizes
        return ImageFont.load_default()


def render(idx, w, h, sprite=True):
    """Indices -> RGBA image in the panel's colours (RGB444 x 17)."""
    from PIL import Image
    lut = [P.rgb888(i) + (255,) for i in range(P.COUNT)]
    clear = P.KEY if sprite else HOLE
    out = bytearray(w * h * 4)
    for n, i in enumerate(idx):
        if i != clear:
            out[n * 4:n * 4 + 4] = bytes(lut[i])
    return Image.frombytes("RGBA", (w, h), bytes(out))


CHECK_A, CHECK_B, GRID = (124, 124, 124), (146, 146, 146), (96, 96, 96)


def checker(w, h, scale, cell, sq=4):
    """Mid-grey checker (sq source pixels a square) with the cell grid, all of it
    only ever visible through transparency."""
    from PIL import Image, ImageDraw
    W, H = w * scale, h * scale
    bg = Image.new("RGBA", (W, H), CHECK_A + (255,))
    d = ImageDraw.Draw(bg)
    s = sq * scale
    for y in range(0, H, s):
        for x in range((y // s) % 2 * s, W, 2 * s):
            d.rectangle([x, y, x + s - 1, y + s - 1], fill=CHECK_B + (255,))
    if cell:
        for x in range(0, W, cell[0] * scale):
            d.line([(x, 0), (x, H - 1)], fill=GRID + (255,))
        for y in range(0, H, cell[1] * scale):
            d.line([(0, y), (W - 1, y)], fill=GRID + (255,))
    return bg


def zoom_on_checker(img, scale, cell):
    from PIL import Image
    big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    bg = checker(img.width, img.height, scale, cell)
    bg.alpha_composite(big)
    return bg


BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def dither_preview(sheet, idx):
    """Fills the alpha frames with their source frames stippled (Bayer 4x4) at
    the frame's level, as gfx_blit4k DITHER will draw them."""
    im = sheet_image(sheet)
    w = im.size[0]
    cw, ch = sheet.cell
    data = im.tobytes()
    for a in sheet.anims:
        if not a.alpha_of:
            continue
        src = sheet.anim(a.alpha_of)
        for f in range(a.frames):
            x0, y0 = (a.col + f) * cw, a.row * ch
            al = max(data[((y0 + y) * w + x0 + x) * 4 + 3] for y in range(ch) for x in range(cw))
            level = (al * 8 + 127) // 255
            # the frame's own pixels say which source frame it is: copy colours
            # from the matching source cell (same shape, see alpha_report)
            sx0 = (src.col + min(f, src.frames - 1)) * cw
            for g in range(src.frames):
                gx = (src.col + g) * cw
                if all((data[((y0 + y) * w + x0 + x) * 4 + 3] > 0)
                       == (data[((src.row * ch + y) * w + gx + x) * 4 + 3] == 255)
                       for y in range(ch) for x in range(cw)):
                    sx0 = gx
                    break
            for y in range(ch):
                for x in range(cw):
                    v = idx[(src.row * ch + y) * w + sx0 + x]
                    show = v != P.KEY and BAYER4[y & 3][x & 3] < level * 2
                    idx[(y0 + y) * w + x0 + x] = v if show else P.KEY
    return idx


def label_rows(img, sheet, scale, x, y0, font, colour=(230, 230, 220)):
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    ch = sheet.cell[1] * scale
    for a in sheet.anims:
        tag = a.name + (" (dither)" if a.alpha_of else "")
        d.text((x, y0 + a.row * ch + ch // 2 - 8), tag, fill=colour, font=font)
        d.text((x, y0 + a.row * ch + ch // 2 + 6), "%d f %s" % (a.frames, a.role), fill=(170, 170, 160),
               font=_font(11))


def sheet_preview(sheet, scale=3):
    """Original on top, quantised below (side by side if the sheet is tall)."""
    from PIL import Image, ImageDraw
    sprite = sheet.kind == "sprite"
    im = sheet_image(sheet)
    w, h = im.size
    scale = max(scale, 96 // max(w, h))       # the 4x4 ball would be a speck at 3x
    idx = quantise_sheet(sheet)
    if sprite:
        idx = dither_preview(sheet, idx)
    q = render(idx, w, h, sprite)
    a = zoom_on_checker(im, scale, sheet.cell)
    b = zoom_on_checker(q, scale, sheet.cell)
    font, small = _font(16), _font(13)
    margin = 190 if sheet.anims and sheet.key != "ball" else 8
    head = 30
    tall = h > w
    if tall:
        W = margin + a.width + 16 + b.width + 8
        H = head + 22 + a.height + 8
    else:
        W = margin + a.width + 8
        H = head + 22 + a.height + 10 + 22 + b.height + 8
    W = max(W, 640)
    out = Image.new("RGBA", (W, H), (40, 38, 34, 255))
    d = ImageDraw.Draw(out)
    d.text((8, 6), "%s  (%s, %dx%d, cells %dx%d, pivot %d,%d)  %dx" % (
        sheet.key, sheet.file, w, h, sheet.cell[0], sheet.cell[1], sheet.pivot[0], sheet.pivot[1], scale),
        fill=(236, 226, 200), font=font)
    if tall:
        d.text((margin, head), "original", fill=(200, 200, 190), font=small)
        d.text((margin + a.width + 16, head), "quantised", fill=(200, 200, 190), font=small)
        out.alpha_composite(a, (margin, head + 22))
        out.alpha_composite(b, (margin + a.width + 16, head + 22))
        if margin > 8:
            label_rows(out, sheet, scale, 8, head + 22, small)
    else:
        d.text((margin, head), "original", fill=(200, 200, 190), font=small)
        out.alpha_composite(a, (margin, head + 22))
        yb = head + 22 + a.height + 10
        d.text((margin, yb), "quantised", fill=(200, 200, 190), font=small)
        out.alpha_composite(b, (margin, yb + 22))
        if margin > 8:
            label_rows(out, sheet, scale, 8, head + 22, small)
            label_rows(out, sheet, scale, 8, yb + 22, small)
    return out


def palette_png():
    from PIL import Image, ImageDraw
    sw, sh, cols = 220, 150, 4
    pad = 10
    W = pad + cols * (sw + pad)
    H = 50 + 4 * (sh + pad) + pad
    out = Image.new("RGB", (W, H), (40, 38, 34))
    d = ImageDraw.Draw(out)
    d.text((pad, 12), "Ethan's Critters palette (RGB444; the panel shows exactly these)", fill=(236, 226, 200),
           font=_font(20))
    big, mid, small = _font(30), _font(20), _font(14)
    for i in range(P.COUNT):
        x = pad + (i % cols) * (sw + pad)
        y = 50 + (i // cols) * (sh + pad)
        rgb = P.rgb888(i)
        d.rectangle([x, y, x + sw - 1, y + sh - 1], fill=rgb)
        L = P.LAB[i][0]
        ink = P.rgb888(0) if L > 55 else P.rgb888(5)
        d.text((x + 10, y + 8), "%d" % i, fill=ink, font=big)
        d.text((x + 10, y + 48), P.NAMES[i], fill=ink, font=mid)
        d.text((x + 10, y + 76), "0x%03X  #%s" % (P.RGB444[i], P.hex888(i)), fill=ink, font=small)
        note = "world water / sprite key" if i == P.KEY else ("outline" if i == P.OUTLINE else "")
        if note:
            d.text((x + 10, y + 96), note, fill=ink, font=small)
    return out


def tileset_preview(scale=2):
    from PIL import Image, ImageDraw
    t = S.TILESET
    im = sheet_image(t)
    q = render(quantise_sheet(t), im.width, im.height, sprite=False)
    b = zoom_on_checker(q, scale, None)
    out = Image.new("RGBA", (b.width + 16, b.height + 40), (40, 38, 34, 255))
    ImageDraw.Draw(out).text((8, 8), "tileset quantised (%s) %dx; checker = transparent" % (t.file, scale),
                             fill=(236, 226, 200), font=_font(16))
    out.alpha_composite(b, (8, 32))
    return out


# Ground swatches from the tileset (16x16 source tiles) and the mushroom rows
# (red A2/A3 and white B2/B3 heal; the brown ones are decor).
GROUNDS = (("sand", (32, 0)), ("grass", (240, 224)), ("dirt", (96, 96)), ("water", (32, 144)))
MUSHROOMS = (("mushrooms 1", (200, 328, 316, 346)), ("mushrooms 2", (200, 360, 316, 378)))


def ground_check(scale=3):
    """Every actor's first idle frame, and the mushrooms, quantised, standing
    on the quantised grounds: do they read against the world?"""
    from PIL import Image, ImageDraw
    t = S.TILESET
    tim = sheet_image(t)
    tq = render(quantise_sheet(t), tim.width, tim.height, sprite=False)
    tiles = {n: tq.crop((x, y, x + 16, y + 16)) for n, (x, y) in GROUNDS}
    sw = 128                      # swatch width, source pixels
    rows = []
    for s in S.SPRITE_SHEETS:
        if s.use not in ("player", "critter", "bird", "boss"):
            continue
        im = sheet_image(s)
        q = render(quantise_sheet(s), im.width, im.height)
        idle = s.anims[0]
        cw, ch = s.cell
        frame = q.crop((idle.col * cw, idle.row * ch, idle.col * cw + cw, idle.row * ch + ch))
        rows.append((s.key, frame, s.pivot))
    for name, box in MUSHROOMS:
        m = tq.crop(box)
        rows.append((name, m, (m.width // 2, m.height - 1)))
    font = _font(14)
    lab = 110
    heights = [max(40, pivot[1] + 10) for _, _, pivot in rows]   # the toad is 96 tall
    W = lab + len(GROUNDS) * (sw * scale + 8)
    H = 34 + sum(hh * scale + 8 for hh in heights)
    out = Image.new("RGBA", (W, H), (40, 38, 34, 255))
    d = ImageDraw.Draw(out)
    for gi, (gname, _) in enumerate(GROUNDS):
        d.text((lab + gi * (sw * scale + 8), 10), gname, fill=(236, 226, 200), font=font)
    y = 34
    for (name, frame, pivot), shh in zip(rows, heights):
        d.text((8, y + shh * scale // 2 - 8), name, fill=(236, 226, 200), font=font)
        for gi, (gname, _) in enumerate(GROUNDS):
            sw_img = Image.new("RGBA", (sw, shh))
            for ty in range(0, shh, 16):
                for tx in range(0, sw, 16):
                    sw_img.paste(tiles[gname], (tx, ty))
            sw_img.alpha_composite(frame, (sw // 2 - pivot[0], shh - 8 - pivot[1]))
            out.alpha_composite(sw_img.resize((sw * scale, shh * scale), Image.NEAREST),
                                (lab + gi * (sw * scale + 8), y))
        y += shh * scale + 8
    return out


def remaps_png(scale=4):
    """Every actor's first idle frame through each remap table (palette.py's
    REMAPS) on mid grey and on the grass: the hit flash, the shadow, the rot."""
    from PIL import Image, ImageDraw
    t = S.TILESET
    tim = sheet_image(t)
    grass = render(quantise_sheet(t), tim.width, tim.height, sprite=False).crop((240, 224, 256, 240))
    actors = [s for s in S.SPRITE_SHEETS if s.use in ("player", "critter")]
    tables = [("plain", list(range(P.COUNT)))] + [(n[6:].lower(), make()) for n, make, _ in P.REMAPS]
    cw = max(s.cell[0] for s in actors)
    ch = max(s.cell[1] for s in actors)
    font = _font(14)
    lab = 90
    W = lab + len(tables) * (cw * scale + 6)
    H = 30 + len(actors) * (ch * scale + 6)
    out = Image.new("RGBA", (W, H), (40, 38, 34, 255))
    d = ImageDraw.Draw(out)
    for j, (name, _) in enumerate(tables):
        d.text((lab + j * (cw * scale + 6), 8), name, fill=(236, 226, 200), font=font)
    for i, s in enumerate(actors):
        im = sheet_image(s)
        idx = quantise_sheet(s)
        a = s.anims[0]
        y = 30 + i * (ch * scale + 6)
        d.text((8, y + ch * scale // 2 - 8), s.key, fill=(236, 226, 200), font=font)
        for j, (name, table) in enumerate(tables):
            cell = bytearray()
            for yy in range(s.cell[1]):
                o = (a.row * s.cell[1] + yy) * im.width + a.col * s.cell[0]
                cell += bytes(table[v] if v != P.KEY else P.KEY for v in idx[o:o + s.cell[0]])
            spr = render(cell, s.cell[0], s.cell[1])
            bg = Image.new("RGBA", s.cell, (124, 124, 124, 255))
            if j % 2:
                for gx in range(0, s.cell[0], 16):
                    for gy in range(0, s.cell[1], 16):
                        bg.paste(grass, (gx, gy))
            bg.alpha_composite(spr)
            out.alpha_composite(bg.resize((s.cell[0] * scale, s.cell[1] * scale), Image.NEAREST),
                                (lab + j * (cw * scale + 6), y))
    return out


# --- report -------------------------------------------------------------------

def report_text():
    L = []
    L.append("Ethan's Critters: palette and quantiser report")
    L.append("made by tools/assets/quantise.py --preview; delta-E is CIEDE2000; '!!' marks delta-E > %g" % DE_WARN)
    L.append("")
    L.append("PALETTE")
    for i in range(P.COUNT):
        L.append("  %2d %-10s 0x%03X  #%s  %s" % (i, P.NAMES[i], P.RGB444[i], P.hex888(i), P.USES[i]))
    fails = []
    for s in S.ALL:
        sprite = s.kind == "sprite"
        im = sheet_image(s)
        L.append("")
        L.append("=" * 78)
        L.append("%s: %s  %dx%d  cells %dx%d  pivot %s  use %s  (%s)" % (
            s.key, s.file, im.width, im.height, s.cell[0], s.cell[1], s.pivot, s.use,
            "sprite: 15 = transparent" if sprite else "tiles: 15 = water, no key"))
        if s.notes:
            L.append("  note: " + s.notes)
        for a in s.anims:
            extra = ("  blank %d" % a.blank if a.blank else "") + ("  dither of %s" % a.alpha_of if a.alpha_of else "")
            L.append("  row %2d  %-28s %2d frames x %d ms  role %s%s" % (a.row, a.name, a.frames, a.ms, a.role, extra))
        cmap = sheet_map(s)
        total = sum(v[3] for v in cmap.values())
        L.append("  colours (%d, %d px): source -> index name  dE  pixels  how" % (len(cmap), total))
        for rgb, (i, de, how, n) in sorted(cmap.items(), key=lambda kv: -kv[1][3]):
            flag = " !!" if de > DE_WARN else ""
            L.append("    %s -> %2d %-10s %5.1f %6d  %s%s" % (rgb_hex(rgb), i, P.NAMES[i], de, n, how, flag))
        worst = sorted(cmap.items(), key=lambda kv: -kv[1][1])[:3]
        L.append("  worst: " + ", ".join("%s->%s dE %.1f (%d px)" % (rgb_hex(r), P.NAMES[v[0]], v[1], v[3])
                                         for r, v in worst))
        used = Counter()
        for i, de, how, n in cmap.values():
            used[i] += n
        L.append("  indices used: " + " ".join("%d:%s" % (i, P.NAMES[i]) for i in sorted(used)))
        checks = ramp_checks(s, cmap)
        if checks:
            L.append("  distinct levels preserved:")
            for label, got, distinct, need, ok in checks:
                L.append("    %-4s %-14s %s  (%d distinct, need %d)" % (
                    "ok" if ok else "FAIL", label, " ".join("%s->%s" % (hx, P.NAMES[i]) for hx, i in got),
                    distinct, need))
                if not ok:
                    fails.append("%s %s" % (s.key, label))
        lost = merged_neighbours(s, cmap)
        if lost:
            L.append("  touching source colours merged into one index (most edges first):")
            for (a, b), n in lost.most_common(8):
                L.append("    %s + %s -> %-10s %5d edges" % (rgb_hex(a), rgb_hex(b), P.NAMES[cmap[a][0]], n))
        if sprite and any(v[0] == P.KEY for v in cmap.values()):
            fails.append("%s maps an opaque pixel to 15" % s.key)
            L.append("  FAIL: an opaque pixel maps to 15")
        for line in alpha_report(s):
            L.append("  " + line)
        blanks = ["%s f%d-%d" % (a.name, a.drawn, a.frames - 1) for a in s.anims if a.blank]
        if blanks:
            L.append("  blank frames (listed by the json, empty in the PNG): " + ", ".join(blanks))
    L.append("")
    L.append("SUMMARY: " + ("all distinct-level checks pass" if not fails else "FAILED: " + "; ".join(fails)))
    return "\n".join(L) + "\n", fails


# --- the generated C++ ----------------------------------------------------------

def enum_name(i):
    return "C_WATER" if i == P.WATER else "C_" + P.NAMES[i].upper()


def header_text():
    """(Palette.h, Palette.cpp) as quantise.py --header writes them."""
    banner = ("// GENERATED by tools/assets/quantise.py --header from tools/assets/palette.py.\n"
              "// Do not edit: change palette.py and re-run.\n")
    h = [banner, "//\n",
         "// The game's sixteen colours in RGB444 (0xRGB, pal::init()'s format; the\n",
         "// panel runs at 12 bpp, so these are exactly what it shows). Index 0 is the\n",
         "// outline; 15 is the swamp water in the world and the transparent key in\n",
         "// sprites (no sprite pixel uses it).\n",
         "#pragma once\n#include <stdint.h>\n\n",
         "extern const uint16_t PALETTE[16];\n\n",
         "enum : uint8_t {\n"]
    for i in range(P.COUNT):
        h.append("    %-11s = %2d,   // 0x%03X %s\n" % (enum_name(i), i, P.RGB444[i], P.NAMES[i]))
    h.append("    C_OUTLINE   = C_SOOT,\n")
    h.append("    C_KEY       = C_WATER,   // sprites: transparent\n")
    h.append("};\n\n")
    h.append("// Colour -> colour tables for gfx_blit4k's GFX_B4_REMAP, gfx_remapRect() and\n"
             "// gfx_sprite4(), made from the colours by palette.py (15, the sprite key,\n"
             "// stays 15 except in REMAP_DARK, REMAP_LIGHT and REMAP_SEPIA, which also shade\n"
             "// the water).\n")
    for name, _, what in P.REMAPS:
        h.append("extern const uint8_t %s[16];%s// %s\n" % (name, " " * (13 - len(name)), what))
    c = [banner, "#include \"Palette.h\"\n\n", "const uint16_t PALETTE[16] = {\n"]
    for i in range(P.COUNT):
        c.append("    0x%03X,  // %2d %s\n" % (P.RGB444[i], i, P.NAMES[i]))
    c.append("};\n")
    for name, make, what in P.REMAPS:
        t = make()
        c.append("\n// %s: %s\n// %s\n" % (name, what, " ".join("%s>%s" % (P.NAMES[i][:4], P.NAMES[v][:4])
                                                            for i, v in enumerate(t) if i != v)))
        c.append("const uint8_t %s[16] = {%s};\n" % (name, ", ".join(str(v) for v in t)))
    return "".join(h), "".join(c)


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--preview", action="store_true", help="write the preview PNGs and report.txt to --out")
    ap.add_argument("--header", action="store_true", help="write src/assets/Palette.h and Palette.cpp")
    ap.add_argument("--out", type=Path, default=OUT, help="preview folder (default out/art)")
    args = ap.parse_args(argv)
    if not (args.preview or args.header):
        ap.print_help()
        return 2
    if args.header:
        h, c = header_text()
        write_text(SRC_ASSETS / "Palette.h", h)
        write_text(SRC_ASSETS / "Palette.cpp", c)
        print("wrote", SRC_ASSETS / "Palette.h", "and Palette.cpp")
    if args.preview:
        out = args.out
        out.mkdir(parents=True, exist_ok=True)
        palette_png().save(out / "palette.png")
        for s in S.ALL:
            if s.kind == "sprite":
                sheet_preview(s).save(out / ("%s_preview.png" % s.key))
        tileset_preview().save(out / "tileset_preview.png")
        ground_check().save(out / "ground_check.png")
        remaps_png().save(out / "remaps.png")
        text, fails = report_text()
        write_text(out / "report.txt", text)
        print("wrote", out, "(%d sheets)" % len(S.ALL))
        if fails:
            print("distinct-level checks FAILED:", "; ".join(fails))
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
