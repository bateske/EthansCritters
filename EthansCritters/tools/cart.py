"""docs/cart.png, Ethan's Critters' picture in the visual menu (CHGame's
docs/cover-art.md). `chgame boxart` redraws it; edit this, not the PNG.

THE BRIEF
  Message: a boy with a sword in a dark swamp, and the critters coming for
  him; something huge behind them all. Old Gullet, the giant toad, rises
  out of the pond in the middle of the picture, mouth open, its eyes
  glowing (the rainbow colour: on a Rainbow bootloader they turn through
  the colour wheel, on a Static one they glow pink), with the last of the
  dusk behind it. In front, small, the squire at the lower left in the
  middle of his thrust, the sword's swipe a big bone crescent about to
  land on a raccoon rearing up at him; a second raccoon runs in from the
  right. Willow fronds hang in from both edges, darker than the toad (the
  light is behind it); reeds and cattails stand in the near corners; mist
  lies on the far water. The title sits in the top band over the dark
  upper sky: the game's own title lettering (tools/card/title.py: blocks
  of dried mud, lit from the top, moss on their tops, drips below).

  The pictures are the game's own, at their own size (never enlarged):
  Elthen's sprites (../Sprites, quantised to the game's palette by
  tools/assets/quantise.py as the game shows them), the tileset's willows
  and reeds (tools/world/tileset.py), and the swipe's arc (tools/assets/
  props.py). The cover's eleven colours are the game's palette less soot,
  bone, slate and rust, which the menu's black, cream, grey and red stand
  in for, so every sprite keeps its pixels. The sky, the glow, the water,
  the mist and the vignette are painted with artkit and quantised; the
  sprites and the title go on after, pixel for pixel.

  The art toolkit (artkit, boxart) is the CHGame repository's: a checkout
  beside this project (../CHGame) or at $CHGAME_ROOT.

    python EthansCritters/tools/cart.py          writes docs/cart.png
    python -m artkit show EthansCritters/tools/cart.py --name EthansCritters --out out/art
                                                  previews at 1x and 4x and the house checks
"""
import os
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent          # EthansCritters/tools
ROOT = HERE.parents[1]                                   # the project root
CHGAME = pathlib.Path(os.environ.get("CHGAME_ROOT", ROOT.parent / "CHGame"))
for p in (CHGAME / "tools", HERE / "assets", HERE / "world", HERE / "card"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))
import numpy as np  # noqa: E402
import artkit as ak  # noqa: E402
import quantise as Q  # noqa: E402
import sheets as S  # noqa: E402
import tileset as TS  # noqa: E402
import title as T  # noqa: E402

TITLE_TOP = 5                # the lettering's rows, as on the game's title screen (title.py)

# ---- the palette --------------------------------------------------------------------
# The game's colours (palette.py) less soot, bone, slate and rust: the menu's
# black, cream, grey and red stand in for those (GAME below maps the game's
# sixteen indices onto the cover's names; 15 is a sprite's transparency).
OWN = {"umber": "#553322", "peat": "#885544", "bark": "#BB7755", "mud": "#FFDD99", "ochre": "#FFBB44",
       "deepmoss": "#334422", "bog": "#556633", "moss": "#88AA44", "lichen": "#CCEE66",
       "fog": "#CCEEEE", "water": "#446677"}
RAMPS = [["black", "umber", "peat", "bark", "ochre", "mud", "cream"],
         ["black", "deepmoss", "bog", "moss", "lichen", "cream"],
         ["black", "water", "fog", "cream"],
         ["black", "grey", "fog"]]
PAIRS = [("water", "deepmoss"), ("water", "bog"), ("water", "peat"), ("water", "bark"), ("water", "ochre"),
         ("umber", "deepmoss"), ("peat", "deepmoss"), ("peat", "bog"), ("bark", "moss"), ("grey", "water"),
         ("umber", "water")]
GAME = ["black", "umber", "peat", "bark", "mud", "cream", "deepmoss", "bog", "moss", "lichen", "ochre", "fog",
        "grey", "grey", "red", "water"]
SQUIRE_MAP = GAME[:13] + ["fog", "red", "water"]                 # his blade and mail (stone) lighter
# the willows and reeds a step darker: the light is behind them
DUSK = {"umber": "umber", "peat": "umber", "bark": "peat", "mud": "bark", "ochre": "bark",
        "deepmoss": "deepmoss", "bog": "deepmoss", "moss": "bog", "lichen": "moss", "fog": "water",
        "cream": "fog", "grey": "grey", "black": "black", "red": "red", "water": "water"}
TREE_MAP = [DUSK[c] for c in GAME]

# ---- where things stand (pixels) ---------------------------------------------------------
HORIZON = 60                 # the far shore
SUN = (74, 57, 44, 22)       # the glow behind the toad: centre, radii
WATERLINE = 93               # the toad stands in the pond below this row
BANK = 104                   # the near shore's lowest reach (the bank is solid ground below it)
SHORE = 78                   # the shoreline's middle: it wanders +-4 px along x (shore())
PATCH = (74, 97.5, 32, 5.5)  # the light green patch the toad sits on: centre, radii
FLAT_FROM = 108              # the ground below this row is flat patches, not dithered
TOAD = (74, 98)              # its feet (the sprite's pivot)
SQUIRE = (36, 121)
RACCOON = (66, 122)          # rearing at him, facing left
RACCOON2 = (108, 119)        # running in from the right
# the willows: the big one (T2) with its trunk out of the frame, so only its
# fronds hang in at each edge (key, foot x, foot y, flipped)
WILLOW_L = ("T2", -22, 112, True)
WILLOW_R = ("T2", 150, 112, False)


# ---- the game's art, as indices ----------------------------------------------------------

def frame(sheet, row, col):
    """A sheet's cell as the game's palette indices (15 clear), and its
    pivot within the cell."""
    im = Q.sheet_image(sheet)
    cw, ch = sheet.cell
    cell = im.crop((col * cw, row * ch, col * cw + cw, row * ch + ch))
    idx = np.frombuffer(bytes(Q.map_image(cell, sheet.overrides, True)), dtype=np.uint8).reshape(ch, cw)
    return idx, sheet.pivot


_ts = None


def tileset():
    global _ts
    if _ts is None:
        _ts = TS.Tileset()
    return _ts


def part(table, key):
    """A tileset part as indices (15 clear)."""
    p = tileset().part(getattr(TS, table)[key], key)
    idx = np.asarray(p.idx, dtype=np.uint8).copy()
    idx[np.asarray(p.mask) == 0] = 15
    return idx


def blit(pic, idx, x, y, cmap, flip=False, clip_above=None):
    """Indices (15 clear) onto the picture, their top-left at (x, y),
    through cmap (game index -> cover colour name). clip_above: rows at or
    below this picture row are left out (the toad under the water)."""
    if flip:
        idx = idx[:, ::-1]
    h, w = idx.shape
    for j in range(h):
        py = y + j
        if not 0 <= py < 128 or (clip_above is not None and py >= clip_above):
            continue
        row = idx[j]
        for i in np.nonzero(row != 15)[0]:
            px = x + int(i)
            if 0 <= px < 128:
                pic.idx[py, px] = pic.pal[cmap[int(row[i])]]


def sprite(pic, sheet, row, col, at, cmap=GAME, flip=False, clip_above=None):
    idx, (px, py) = frame(sheet, row, col)
    if flip:
        px = idx.shape[1] - 1 - px
    blit(pic, idx, at[0] - px, at[1] - py, cmap, flip, clip_above)


def willow(pic, key, fx, fy, flip):
    idx = part("WILLOWS", key)
    _, ax, ay, _ = tileset().willow(key)
    if flip:
        ax = idx.shape[1] - 1 - ax
    blit(pic, idx, fx - ax, fy - ay, TREE_MAP, flip)


def swipe_arc():
    import props
    cv = props.swipe()
    return np.asarray(cv.im, dtype=np.uint8).copy(), props.SWIPE_PIVOT


def shadow(pic, x, y, hw, rows=2):
    """A checkered ground shadow under feet at (x, y), hw px each side."""
    for j in range(rows):
        w = hw - j * 2
        for dx in range(-w, w + 1):
            if (x + dx + y + j) & 1:
                pic.px(x + dx, y + j, "black")


def shore(x):
    """The shoreline's row at x (pixels, floats allowed): SHORE wandering
    +-5 px, smooth, the same on every run."""
    x = np.asarray(x, dtype=np.float32)
    n = ak.noise((x, np.full_like(x, 7.0)), scale=9, seed=4, octaves=2)
    return SHORE + 8.0 * (n - 0.5)


def blades(x):
    """Grass blades at the shoreline: 0 for most columns, 1-4 px up for some
    (a column each: the hash of its pixel x)."""
    c = np.floor(np.asarray(x, dtype=np.float32)).astype(np.int64)
    h = ((c * 374761393 + 1442695041) & 0xFFFF) / 65535.0
    h2 = ((c * 668265263 + 974711) & 0xFFFF) / 65535.0
    return np.where(h > 0.5, 1 + np.floor(h2 * 4), 0).astype(np.float32)


def bayer8():
    m = np.array([[0, 2], [3, 1]])
    for _ in range(2):
        m = np.block([[4 * m, 4 * m + 2], [4 * m + 3, 4 * m + 1]])
    return (m + 0.5) / 64.0


SKY_RAMP = ["black", "umber", "peat", "bark", "ochre", "mud"]


def sky(pic):
    """The sky's own dither: every pixel above the far shore is a place on
    SKY_RAMP (night at the top to bark at the horizon, the glow round the
    sun lifting it to ochre and mud, the vignette pulling the corners
    down), drawn between its two neighbouring colours by an 8x8 ordered
    (Bayer) matrix: 64 even steps, no lone dots."""
    xs, ys = np.meshgrid(np.arange(128) + 0.5, np.arange(128) + 0.5)
    t = np.clip(ys / HORIZON, 0, 1)
    v = np.interp(t, [0.0, 0.3, 0.58, 0.82, 1.0], [0.0, 0.55, 1.0, 2.0, 3.0])
    sx, sy, rx, ry = SUN
    g = np.clip(1 - ak.radial((xs, ys), sx, sy, rx, ry), 0, 1)
    v = v + 1.5 * g ** 1.6 + 1.0 * g ** 4
    vig = np.clip(ak.radial((xs, ys), 64, 66, 92, 88) ** 2.2, 0, 1)
    v = np.clip(v * (1 - 0.9 * vig), 0, len(SKY_RAMP) - 1)
    lo = np.floor(v).astype(int)
    f = v - lo
    f = np.where(f < 0.125, 0.0, np.where(f > 0.875, 1.0, f))      # (no lone dots at the ends of a step)
    up = f > np.tile(bayer8(), (16, 16))
    k = np.minimum(lo + up, len(SKY_RAMP) - 1)
    where = (ys < HORIZON + 1) & ~pic.obj("far")
    for i, name in enumerate(SKY_RAMP):
        pic.put(where & (k == i), name)


def patch(pic):
    """The light green patch the toad sits on: an ellipse of moss and bog in
    a checker, its rim bog alone."""
    cx, cy, rx, ry = PATCH
    for y in range(int(cy - ry), int(cy + ry) + 2):
        for x in range(int(cx - rx), int(cx + rx) + 2):
            d = ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2
            if d <= 1:
                pic.px(x, y, "bog" if d > 0.7 or (x + y) & 1 else "moss")


def waves(pic):
    """The pond's own dither, from the water at the far edge to the murk at
    the bank: wavy lines (a 4-row stripe pattern that wobbles along x), so
    the gradient reads as ripples, not as the sky's ordered dither. The
    mist and the glow's reflection, painted before, are left as they are."""
    keep = pic.where("fog", "ochre", "bark", "mud", "peat") | pic.obj("bank")
    line = shore(np.arange(128) + 0.5)
    for y in range(HORIZON + 1, 128):
        for x in range(128):
            if keep[y, x] or y >= line[x]:
                continue
            t = (y - HORIZON) / float(line[x] - HORIZON)
            m = min(1.0, max(0.0, (t - 0.22) / 0.78)) ** 1.4           # how much murk
            wob = int(round(1.6 * np.sin(x / 5.0 + y / 9.0)))
            level = ((y + wob) % 4) / 4.0 + ((x // 7) % 2) * 0.125
            pic.px(x, y, "black" if level < m else "water")


def ripples(pic, idx, px, py):
    """The toad in the pond: its reflection, a plain dark ellipse under it,
    and a line of light along the water at its body."""
    x0 = TOAD[0] - px
    row0 = idx[WATERLINE - 1 - (TOAD[1] - py)]
    xs0 = np.nonzero(row0 != 15)[0]
    if len(xs0):
        cx, rx = x0 + (int(xs0.min()) + int(xs0.max())) / 2.0, (int(xs0.max()) - int(xs0.min())) / 2.0 + 2
        ry = 7.0
        for y in range(WATERLINE + 1, WATERLINE + 1 + int(ry) + 1):
            dy = (y - WATERLINE - 0.5) / ry
            if dy > 1:
                continue
            hw = rx * (1 - dy * dy) ** 0.5
            for x in range(int(round(cx - hw)), int(round(cx + hw)) + 1):
                pic.px(x, y, "deepmoss")
    row = idx[WATERLINE - 1 - (TOAD[1] - py)]
    xs = np.nonzero(row != 15)[0]
    if len(xs):
        for x in range(x0 + int(xs.min()), x0 + int(xs.max()) + 1):
            pic.px(x, WATERLINE, "grey")


def reeds(pic):
    """Water reeds at the near shore's corners, land cattails before them."""
    for key, fx, fy, flip in (("L3", 10, 110, False), ("L6", 118, 112, True), ("L1", 24, 106, False)):
        idx = part("LAND_REEDS", key)
        blit(pic, idx, fx - idx.shape[1] // 2, fy - idx.shape[0] + 1, TREE_MAP, flip)
    for key, fx, fy, flip in (("D2", 4, 126, False), ("D3", 122, 127, True)):
        idx = part("LAND_CATTAILS", key)
        blit(pic, idx, fx - idx.shape[1] // 2, fy - idx.shape[0] + 1, TREE_MAP, flip)


def eyes(pic, idx, px, py):
    """The toad's eyes in the rainbow colour: the ochre of its irises."""
    ys, xs = np.nonzero(idx == 10)
    for x, y in zip(xs, ys):
        if y - py < -36:
            pic.px(TOAD[0] + int(x) - px, TOAD[1] + int(y) - py, "rainbow")


# ---- the painting -------------------------------------------------------------------------

def draw():
    P = ak.Palette(OWN, ramps=RAMPS, pairs=PAIRS)
    cv = ak.Canvas("#000000")
    X, Y = cv.P
    # the sky: night above, the last of the dusk at the horizon (checkers at most: no sparse dots)
    t = ak.linear(cv.P, 0, 0, 0, HORIZON)
    dusk = ak.stops(t, [(0.0, "#070504"), (0.3, "#2A1810"), (0.58, "#553322"), (0.82, "#885544"), (1.0, "#BB7755")])
    cv.paint(1.0, dusk)                                  # (redithered by sky() after the quantiser)
    # the glow behind the toad
    sx, sy, rx, ry = SUN
    g = np.clip(1 - ak.radial(cv.P, sx, sy, rx, ry), 0, 1)
    cv.paint(g ** 1.6 * 0.95 * (Y < HORIZON + 1), "#FFBB44")
    cv.paint(g ** 4 * (Y < HORIZON + 1), "#FFDD99")
    # the far shore: a ragged line of dark trees and reeds against the glow
    n = ak.noise(cv.P, scale=7, seed=5, octaves=2)
    far = (Y >= HORIZON - 4 - 9 * n) & (Y < HORIZON + 1)
    cv.paint(far.astype(np.float32),
             ak.stops(ak.linear(cv.P, 0, HORIZON - 14, 0, HORIZON), [(0, "#1A2416"), (1, "#334422")]), dither=0.5,
             oid=cv.new_id("far"))
    # the pond: the sky's colour at the far edge, murk toward us
    w = ak.linear(cv.P, 0, HORIZON, 0, SHORE + 2)
    pond = ak.stops(w, [(0.0, "#3E5E6E"), (0.35, "#446677"), (0.75, "#2E4448"), (1.0, "#1A2420")])
    cv.paint((Y >= HORIZON + 1).astype(np.float32), pond, dither=0.5)
    # the glow's reflection: a streak under the sun, broken by ripples
    rip = ak.noise(cv.P, scale=3, seed=9)
    streak = np.exp(-((X - sx) / 10.0) ** 2) * np.clip(1 - (Y - HORIZON) / 34.0, 0, 1) * (Y > HORIZON)
    cv.paint(np.clip(streak * (0.35 + 0.65 * (rip > 0.45)), 0, 1) * 0.85, "#BB7755")
    cv.paint(np.clip(streak * (rip > 0.62), 0, 1) * 0.7, "#FFBB44")
    # mist on the far water
    m = ak.noise(cv.P, scale=12, seed=2, octaves=2)
    mist = np.clip(1 - (Y - HORIZON) / 14.0, 0, 1) * (Y > HORIZON) * (0.5 + 0.5 * m) * 0.45
    cv.paint(mist, "#CCEEEE")
    # the near bank: dark ground, mossy in patches, up to a wandering shoreline with grass blades
    # poking up out of it into the water
    gn = ak.noise(cv.P, scale=5, seed=3, octaves=2)
    ground = ak.stops(gn, [(0.0, "#334422"), (0.5, "#334422"), (0.62, "#556633"), (1.0, "#556633")])
    line = shore(X)
    cv.paint((Y >= line).astype(np.float32), ground, dither=0.5, oid=cv.new_id("bank"))
    cv.paint(((Y >= line - blades(X)) & (Y < line)).astype(np.float32), "#334422", dither=0, oid=cv.id_of("bank"))
    cv.tag(Y >= FLAT_FROM, dither=0)                 # (the near ground in flat patches: dither there read as noise)
    # the vignette: dark edges
    v = ak.radial(cv.P, 64, 66, 92, 88) ** 2.2
    cv.paint(np.clip(v, 0, 1) * 0.9, "#000000", blend="multiply")
    pic = cv.quantize(P, allow={"bank": ["black", "deepmoss", "bog", "moss"]})   # (the ground stays green)
    ak.despeckle(pic, need=5, passes=2)
    rows = np.arange(128)[:, None] * np.ones((1, 128), dtype=int)
    for colour, where in (("fog", rows > HORIZON), ("peat", rows > HORIZON), ("water", rows > HORIZON)):
        ak.lonely(pic, colour, where)
    waves(pic)
    sky(pic)

    # the toad, standing in the pond: its rows under the water line go, ripples and its reflection below
    toad, (tpx, tpy) = frame(S.TOAD, 3, 1)
    patch(pic)
    sprite(pic, S.TOAD, 3, 1, TOAD)
    eyes(pic, toad, tpx, tpy)
    # the willows' fronds, framing
    willow(pic, *WILLOW_L)
    willow(pic, *WILLOW_R)
    # the reeds in the near corners
    reeds(pic)
    # the second raccoon, running in from the right
    shadow(pic, RACCOON2[0] - 1, RACCOON2[1] - 3, 8)
    sprite(pic, S.RACCOON, 1, 2, RACCOON2, flip=True)
    # the squire, the swipe's arc, the raccoon it is about to land on
    shadow(pic, SQUIRE[0] + 1, SQUIRE[1] + 1, 8)
    shadow(pic, RACCOON[0] + 1, RACCOON[1] + 1, 8)
    arc, (ax, ay) = swipe_arc()
    blit(pic, arc, SQUIRE[0] - ax, SQUIRE[1] - ay, GAME)          # (behind him, as in the game)
    sprite(pic, S.SQUIRE, 2, 4, SQUIRE, SQUIRE_MAP)
    sprite(pic, S.RACCOON, 2, 1, RACCOON, flip=True)
    # the title: the game's own lettering (title.py's mud blocks, moss and drips), the same two
    # lines at the same rows as the title screen
    lettering(pic)
    return pic.image()


class _Sheet:
    """What title.py's letter() draws on: put(x, y, game colour)."""
    def __init__(self):
        self.px = {}

    def put(self, x, y, c):
        self.px[(x, y)] = c


def lettering(pic):
    import random
    sh = _Sheet()
    rng = random.Random(T.SEED)
    T.letter(sh, "ETHAN'S", 1, TITLE_TOP, rng, T.BONE, T.MUD, T.BARK, T.PEAT, wobble=(0, 0, 1), moss=3, drips=1)
    T.letter(sh, "CRITTERS", 2, TITLE_TOP + 12, rng, T.BONE, T.MUD, T.BARK, T.PEAT, wobble=(-1, 0, 0, 1), moss=7,
             drips=4)
    for (x, y), c in sh.px.items():
        pic.px(x, y, GAME[c])


if __name__ == "__main__":
    import boxart
    out = HERE.parent / "docs" / "cart.png"
    boxart.save(draw(), out)
    print("wrote", out)
