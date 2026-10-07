#!/usr/bin/env python3
"""Ethan's Critters: the Path F clips, rendered offline (sections TITL, DEAD,
WINC and PMAP of CRITTERS.DAT).

How far can unique images go: every frame of these is a whole 128x128
picture of its own on the card (16 blocks, one card command a shown frame),
composed here from the painted world (tools/world/paint.py), the quantised
sprite sheets (tools/assets/) and the toad, and each clip has its OWN
sixteen colours (Path F's clip header carries a palette; src/engine/PathF.h
plays a clip at its frame rate and the game draws its words over it).

  TITL  the title's loop (tools/card/title.py: LOOP frames in the game's
        palette; the reeds sway, the water glints, the squire breathes, a
        raccoon blinks; the lettering stays)
  DEAD  THE SWAMP KEEPS YOU (DEATH_N frames at 8 fps, then held under the
        game's A: GET UP): a close shot (64x64 of the world, doubled), the
        squire face-down in the mud at dusk, the light closing in on him,
        the critters edging in out of the dark, fireflies
  WINC  THE SWAMP IS QUIET (WIN_N frames, then held under the game's run
        time and best time): The Rot's pond in its murk, the dead toad
        sinking, the mist rising over it; under the mist a close shot of
        the pond healed (the world's healed layer, the game's after the
        win), the mist lifting at dawn, the squire raising his sword
  PMAP  the pause map (a still in the game's palette, so the game's marks
        and REMAP_SEPIA's fog go over it): the world at 7/64 inked on
        parchment with the regions' names, the nests, the toad and a key;
        Play.cpp's MAP_X / MAP_Y / MAP_CELL must match ours

DEAD and WINC are composed in RGB (the world and the sprites in the game's
colours, then dusk or dawn light in steps with ordered-dithered edges, mist,
glints), and fitted to sixteen colours of their own (fit(): k-means in
CIELAB over every frame of the clip, the held last frame counted more,
slots pinned for the game's words, 0 C_SOOT's dark and 5 C_BONE's light,
and for the squire's tunic and the dawn's gold), then mapped back to the
nearest colour (index(); an ordered two-colour mix only where a pixel is
marked soft): the art stays crisp where it is art.

    python EthansCritters/tools/card/clips.py      previews: out/art/clips/

tools/mkcard.py packs build()'s clips (cached in out/art/clips.cache under a
hash of every input: an unchanged card costs a load).
"""
import hashlib
import math
import pickle
import random
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1]
ROOT = GAME.parent
sys.path.insert(0, str(GAME / "tools" / "assets"))
sys.path.insert(0, str(GAME / "tools" / "world"))
sys.path.insert(0, str(HERE))
import palette as P     # noqa: E402
import sheets as S      # noqa: E402
import title as T       # noqa: E402
import paint as WP      # noqa: E402

W = H = 128
N = W * H
OUT = ROOT / "out" / "art" / "clips"
CACHE = ROOT / "out" / "art" / "clips.cache"
FONT_SRC = (ROOT / "chgame" / "platform" / "board" / "arduino" / "CHGame" / "libraries" / "CHGame" / "src" /
            "chgame" / "Draw.cpp")

(SOOT, UMBER, PEAT, BARK, MUD, BONE, DEEPMOSS, BOG, MOSS, LICHEN, OCHRE, FOG, SLATE, STONE,
 RUST, WATER) = range(16)
CLEAR = 16              # an overlay canvas's transparent pixel

DEATH_N, DEATH_MS = 14, 125
WIN_N, WIN_MS = 20, 125
MAP_X, MAP_Y, MAP_CELL = 8, 24, 7       # Play.cpp's: the map's top-left, a fog cell (64 world px)
MAP_W, MAP_H = 16 * MAP_CELL, 12 * MAP_CELL
TOAD = tuple(WP.load_objects()["arena"]["toad"])   # where the toad rose (world)

BAYER4 = T.BAYER4
BAYER8 = ((0, 32, 8, 40, 2, 34, 10, 42), (48, 16, 56, 24, 50, 18, 58, 26),
          (12, 44, 4, 36, 14, 46, 6, 38), (60, 28, 52, 20, 62, 30, 54, 22),
          (3, 35, 11, 43, 1, 33, 9, 41), (51, 19, 59, 27, 49, 17, 57, 25),
          (15, 47, 7, 39, 13, 45, 5, 37), (63, 31, 55, 23, 61, 29, 53, 21))
RGB = [P.rgb888(i) for i in range(16)]


class Clip:
    """A clip as the card takes it: frames (W*H palette indices each), its
    palette (16 RGB444), ms a frame (0: a still)."""
    def __init__(self, tag, frames, palette, ms):
        self.tag, self.frames, self.palette, self.ms = tag, frames, list(palette), ms


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def smooth(t):
    t = clamp(t)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c, d, t):
    return (c[0] + (d[0] - c[0]) * t, c[1] + (d[1] - c[1]) * t, c[2] + (d[2] - c[2]) * t)


# --- the world ----------------------------------------------------------------------

def world_idx(healed=False):
    """The painted world (WP.W x WP.HP indices) as the game streams it, in
    ambient phase 0: layer 0, or healed layer 1 (The Rot gone: its willows,
    reeds and grass alive), the one the game shows once the toad is beaten."""
    return WP.build().images[1 if healed else 0][0]


def view(idx, x0, y0):
    """The world's 128x128 at (x0, y0) as a canvas."""
    cv = T.Canvas()
    for y in range(H):
        o = (y0 + y) * WP.W + x0
        cv.px[y * W:(y + 1) * W] = idx[o:o + W]
    return cv


def zoom2(cv):
    """The canvas's top-left 64x64, twice the size."""
    out = T.Canvas()
    for y in range(H):
        row = cv.px[(y >> 1) * W:(y >> 1) * W + 64]
        out.px[y * W:(y + 1) * W] = bytes(c for c in row for _ in (0, 1))
    return out


# --- drawing in palette indices -------------------------------------------------------

def sprite(cv, sheet, anim, frame, x, y, flip=False, level=16, below=None, remap=None):
    """A sheet's frame with its pivot at (x, y); flip mirrors it; level
    0..16 dithers it in (Bayer 4x4); below: rows from that screen y down are
    under water; remap: colour -> colour."""
    s = T.src(sheet)
    a = sheet.anim(anim)
    cw, ch = sheet.cell
    px, py = sheet.pivot
    cx, cy = (a.col + frame) * cw, a.row * ch
    dx, dy = x - ((cw - 1 - px) if flip else px), y - py
    for j in range(ch):
        sy = dy + j
        if below is not None and sy >= below:
            break
        if not 0 <= sy < H:
            continue
        for i in range(cw):
            c = s.at(cx + i, cy + j)
            if c == s.clear:
                continue
            sx = dx + (cw - 1 - i if flip else i)
            if level < 16 and BAYER4[sy & 3][sx & 3] >= level:
                continue
            cv.put(sx, sy, remap[c] if remap else c)


def overlay(cv, ov, level=16):
    """An overlay canvas (CLEAR where empty) onto cv, dithered in by level."""
    for n, c in enumerate(ov.px):
        if c != CLEAR and (level >= 16 or BAYER4[(n >> 7) & 3][n & 3] < level):
            cv.px[n] = c


def words(lines, rng_seed, face_hi=BONE, face=MUD, face_lo=BARK, side=PEAT, moss=True):
    """Lettering in title.py's chunky face on an empty overlay: lines of
    (text, scale, top)."""
    ov = T.Canvas(CLEAR)
    rng = random.Random(rng_seed)
    for text, scale, top in lines:
        T.letter(ov, text, scale, top, rng, face_hi, face, face_lo, side, wobble=(-1, 0, 0, 1) if scale > 1 else (0, 0, 1),
                 moss=(7 if scale > 1 else 3) if moss else 0, drips=4 if scale > 1 else 1)
    return ov


def ring(cv, cx, cy, rx, ry, c, step=10, only=None):
    """An ellipse of single pixels (a ripple), on colour `only` if given."""
    for a in range(0, 360, step):
        t = math.radians(a)
        x, y = int(round(cx + rx * math.cos(t))), int(round(cy + ry * math.sin(t)))
        if 0 <= x < W and 0 <= y < H and (only is None or cv.get(x, y) in only):
            cv.put(x, y, c)


# --- the light: RGB frames -------------------------------------------------------------

class Lit:
    """A frame in RGB (floats, the game's colours to start with) and, a pixel
    each, whether the light made its colour (then it may be dithered)."""
    def __init__(self, cv):
        self.c = [RGB[i] for i in cv.px]
        self.soft = bytearray(N)

    def put(self, x, y, rgb, soft=0):
        if 0 <= x < W and 0 <= y < H:
            self.c[y * W + x] = rgb
            self.soft[y * W + x] = soft


def _hash2(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def noise(x, y, seed=0):
    """Smooth value noise, 0..1, a lattice step of 1."""
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = smooth(x - ix), smooth(y - iy)
    a, b = _hash2(ix, iy, seed), _hash2(ix + 1, iy, seed)
    c, d = _hash2(ix, iy + 1, seed), _hash2(ix + 1, iy + 1, seed)
    return lerp(lerp(a, b, fx), lerp(c, d, fx), fy)


def fbm(x, y, seed=0):
    return 0.6 * noise(x, y, seed) + 0.3 * noise(x * 2.1, y * 2.1, seed + 7) + 0.1 * noise(x * 4.3, y * 4.3, seed + 13)


# --- sixteen colours of its own ----------------------------------------------------------

def _lab(c):
    return P.lab((clamp(c[0], 0, 255), clamp(c[1], 0, 255), clamp(c[2], 0, 255)))


def _d2(p, q):
    return (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2


def fit(frames, pins, hold=4):
    """A clip's palette: pins {slot: RGB444} kept, the other slots k-means
    centres (CIELAB) of every frame's colours, each colour weighted by its
    pixel count ** 0.6 so a small accent (a tunic, a tongue, the words)
    keeps a colour of its own, the last frame (held on the screen) counted
    `hold` times. 16 RGB444 values, the free ones by lightness."""
    hist = {}
    for i, f in enumerate(frames):
        wf = hold if i == len(frames) - 1 else 1
        for c in f.c:
            k = (int(clamp(c[0], 0, 255) * 15 / 255 + 0.5), int(clamp(c[1], 0, 255) * 15 / 255 + 0.5),
                 int(clamp(c[2], 0, 255) * 15 / 255 + 0.5))
            hist[k] = hist.get(k, 0) + wf
    pts = sorted(hist)
    rgb = [(r * 17.0, g * 17.0, b * 17.0) for r, g, b in pts]
    lab = [_lab(c) for c in rgb]
    wt = [hist[p] ** 0.6 for p in pts]
    fixed = [(s, ((v >> 8) * 17.0, (v >> 4 & 15) * 17.0, (v & 15) * 17.0)) for s, v in sorted(pins.items())]
    cent = [_lab(c) for _, c in fixed]
    crgb = [c for _, c in fixed]
    nfix = len(cent)
    while len(cent) < 16:          # the next centre: the colour worst served by those there (weighted)
        d = [w * min(_d2(l, c) for c in cent) for l, w in zip(lab, wt)]
        k = max(range(len(d)), key=lambda i: d[i])
        if d[k] <= 0:
            break
        cent.append(lab[k])
        crgb.append(rgb[k])
    for _ in range(24):
        sums = [[0.0, 0.0, 0.0, 0.0] for _ in cent]
        for l, c, w in zip(lab, rgb, wt):
            j = min(range(len(cent)), key=lambda j: _d2(l, cent[j]))
            s = sums[j]
            s[0] += c[0] * w
            s[1] += c[1] * w
            s[2] += c[2] * w
            s[3] += w
        moved = False
        for j in range(nfix, len(cent)):
            s = sums[j]
            if s[3]:
                c = (s[0] / s[3], s[1] / s[3], s[2] / s[3])
                if _d2(c, crgb[j]) > 0.01:
                    moved = True
                crgb[j] = c
                cent[j] = _lab(c)
        if not moved:
            break
    q = [P.from888(int(c[0] + 0.5), int(c[1] + 0.5), int(c[2] + 0.5)) for c in crgb]
    free = sorted(set(q[nfix:]) - set(pins.values()), key=lambda v: _lab(rgb444(v))[0])
    pal, it = [], iter(free)
    for s in range(16):
        pal.append(pins[s] if s in pins else next(it, 0))
    return pal


def rgb444(v):
    return ((v >> 8) * 17, (v >> 4 & 15) * 17, (v & 15) * 17)


def index(frames, pal):
    """The frames in the palette's indices: each pixel to its nearest
    colour (CIELAB), or where the light made it (soft) an ordered mix of the
    two colours that best make it (Bayer 8x8)."""
    plab = [_lab(rgb444(v)) for v in pal]
    near, mixes = {}, {}

    def nearest(c):
        k = (int(c[0]), int(c[1]), int(c[2]))
        v = near.get(k)
        if v is None:
            l = _lab(c)
            v = near[k] = min(range(16), key=lambda j: _d2(l, plab[j]))
        return v

    def two(c):
        k = (int(c[0]) >> 1, int(c[1]) >> 1, int(c[2]) >> 1)
        v = mixes.get(k)
        if v is None:
            l = _lab(c)
            best = None
            for a in range(16):
                la = plab[a]
                ea = _d2(l, la)
                if best is None or ea < best[0]:
                    best = (ea, a, a, 0.0)
                for b in range(a + 1, 16):
                    lb = plab[b]
                    vx, vy, vz = lb[0] - la[0], lb[1] - la[1], lb[2] - la[2]
                    vv = vx * vx + vy * vy + vz * vz
                    if vv < 1e-6 or vv > 1600.0:        # (no mixes of far-apart colours: they read as noise)
                        continue
                    t = ((l[0] - la[0]) * vx + (l[1] - la[1]) * vy + (l[2] - la[2]) * vz) / vv
                    if t <= 0.0 or t >= 1.0:
                        continue
                    e = ((la[0] + t * vx - l[0]) ** 2 + (la[1] + t * vy - l[1]) ** 2 + (la[2] + t * vz - l[2]) ** 2
                         + 0.08 * vv * t * (1 - t))
                    if e < best[0]:
                        best = (e, a, b, t)
            v = mixes[k] = best[1:]
        return v

    out = []
    for f in frames:
        px = bytearray(N)
        for n, c in enumerate(f.c):
            if f.soft[n]:
                a, b, t = two(c)
                px[n] = b if BAYER8[(n >> 7) & 7][n & 7] < t * 64 else a
            else:
                px[n] = nearest(c)
        out.append(bytes(px))
    return out


# --- light in steps --------------------------------------------------------------------------

def step(v, levels, x, y):
    """v (0..1) as one of `levels` steps, ordered-dithered between two
    (Bayer 4x4): light and mist in bands with dithered edges, as a pixel
    artist shades, rather than a colour of its own every pixel."""
    lv = clamp(v) * (levels - 1)
    b = int(lv)
    if b < levels - 1 and BAYER4[y & 3][x & 3] < (lv - b) * 16:
        b += 1
    return b


def grey(c, t):
    yv = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
    return mix(c, (yv, yv, yv), t)


# --- DEAD: the swamp keeps you ---------------------------------------------------------------

DEATH_AT = (460, 516)           # 64x64 of the world, twice the size: the beaver pond's mud shore, a stump
FALLEN = (38, 46)               # where he lies (its 64x64)
# The dusk, by light step: the dark's cold violet, the dim, the last of the light (warm).
DUSK = ((lambda c: (lambda g: (g[0] * 0.30 + 6, g[1] * 0.28 + 6, g[2] * 0.42 + 20))(grey(c, 0.55))),
        (lambda c: (lambda g: (g[0] * 0.64 + 6, g[1] * 0.58 + 6, g[2] * 0.70 + 16))(grey(c, 0.3))),
        (lambda c: (c[0] * 0.96 + 6, c[1] * 0.88 + 2, c[2] * 0.76)))


def death_scene(k, idx, ov):
    """Frame k: the squire face-down in the mud, the critters edging in out
    of the dark, the words dithered in; the dusk light closing in on him.
    Drawn at 1x in a 64x64 corner of the world, then doubled (a close shot)."""
    t = k / (DEATH_N - 1)
    cv = view(idx, *DEATH_AT)
    fx, fy = FALLEN
    # the beaver: its head out of the water, watching, rings round it
    bx, by = 52, 21
    if k >= 2:
        ring(cv, bx, by + 1, 6 + (k % 4), 2 + (k % 4) // 2, FOG, 15, only=(WATER,))
        sprite(cv, S.BEAVER, "Idle Water", (k // 2) % 4, bx, by, flip=True, level=min(16, (k - 2) * 4))
    cast = []
    # a raccoon slinks in from the left along the mud, then sits and stares
    rx = int(lerp(-12, fx - 28, smooth(min(1.0, k / 9))))
    cast.append((fy + 2, lambda: sprite(cv, S.RACCOON, "run" if k < 9 else "idle", k % 8, rx, fy + 2)))
    # another from the right, along the shore behind him
    rx2 = int(lerp(78, fx + 12, smooth(min(1.0, (k - 1) / 9))))
    cast.append((fy - 7, lambda: sprite(cv, S.RACCOON, "run" if k < 10 else "idle", (k + 3) % 8, rx2, fy - 7,
                                        flip=True)))
    # the turtle, slow, up from below
    ty = int(lerp(80, fy + 14, smooth(min(1.0, k / 12))))
    cast.append((ty, lambda: sprite(cv, S.TURTLE, "walk" if k < 12 else "idle_blink", k % 4, 14, ty)))
    # the chameleon in the grass: dithered in on his right
    if k >= 4:
        cast.append((fy + 9, lambda: sprite(cv, S.CHAMELEON, "idle", k % 8, fx + 14, fy + 9, flip=True,
                                            level=min(16, (k - 4) * 3))))
    cast.append((fy, lambda: sprite(cv, S.SQUIRE, "death", 4, fx, fy)))
    for _, draw in sorted(cast, key=lambda c: c[0]):
        draw()
    cv = zoom2(cv)
    lv = min(16, max(0, (k - 1) * 3))
    overlay(cv, ov, lv)
    # dusk: a pool of the last light on him, closing in, the rest cold
    lit = Lit(cv)
    sx, sy = 2 * fx, 2 * fy - 6
    r0, r1 = lerp(40, 26, t), lerp(104, 76, t)
    for y in range(H):
        for x in range(W):
            n = y * W + x
            if ov.px[n] != CLEAR and BAYER4[y & 3][x & 3] < lv:
                continue                                    # the words: their own colours
            d = math.hypot(x - sx, (y - sy) * 1.3)
            lit.c[n] = DUSK[step((r1 - d) / (r1 - r0), 3, x, y)](lit.c[n])
    # fireflies in the dark, their own light
    r = random.Random(0xDEAD)
    for _ in range(6):
        x0, y0, ph = r.randrange(8, 120), r.randrange(44, 124), r.random() * 6.28
        x = int(round(x0 + 4 * math.sin(k * 0.5 + ph)))
        y = int(round(y0 + 3 * math.cos(k * 0.35 + ph)))
        if (k + int(ph * 3)) % 5:
            lit.put(x, y, (238, 221, 119))
    return lit


def death():
    idx = world_idx()
    ov = words([("THE SWAMP", 1, 5), ("KEEPS YOU", 2, 16)], 0xD1E, BONE, FOG, STONE, SLATE)
    frames = [death_scene(k, idx, ov) for k in range(DEATH_N)]
    pal = fit(frames, {0: 0x112, 5: 0xEED, 14: 0xC43})
    return Clip("DEAD", index(frames, pal), pal, DEATH_MS)


# --- WINC: the swamp is quiet ---------------------------------------------------------------

WIDE_AT = (776, 120)            # the wide shot: The Rot's pond, rotten
SUNK = (70, 80)                 # where the toad goes down (screen, its feet)
HERO = (30, 100)                # the squire on the shore (screen)
CLOSE_AT = (812, 150)           # the close shot, twice the size: 64x64 of the healed pond's shore
HERO2 = (16, 60)                # the squire in it (its 64x64)
VEIL = 9                        # the frames before: the wide shot; from it: the close one, under the mist
MIST = (232, 208, 188)          # dawn mist, warm
# The murk under the mist, and the dawn by light step (the shade, the sun).
MURK = lambda c: (lambda g: (g[0] * 0.70 + 16, g[1] * 0.72 + 16, g[2] * 0.66 + 16))(grey(c, 0.4))
DAWN = ((lambda c: (c[0] * 0.82 + 10, c[1] * 0.80 + 4, c[2] * 0.90 + 8)),
        (lambda c: (c[0] * 1.04 + 22, c[1] * 0.96 + 10, c[2] * 0.80)))
SKY = (222, 140, 112)           # the dawn sky the water gives back


def chop(cv):
    """The deep water's dark chop (soot and deepmoss strokes on the water):
    which pixels, so the light can make it water rather than ink."""
    m = bytearray(N)
    for n, c in enumerate(cv.px):
        if c in (SOOT, DEEPMOSS):
            x, y = n & 127, n >> 7
            if any(0 <= x + dx < W and 0 <= y + dy < H and cv.px[(y + dy) * W + x + dx] == WATER
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                m[n] = 1
    return m


def win_scene(k, rot, healed, ov):
    """Frame k: the wide shot, the toad sinking in the murk under the mist
    rising; the mist lifting at dawn off the close shot, the healed pond, the
    squire raising his sword."""
    if k < VEIL:
        cv = view(rot, *WIDE_AT)
        tx, ty = SUNK
        sink = int(round(50 * smooth(k / (VEIL - 2))))
        sprite(cv, S.TOAD, "death", min(7, 1 + k), tx, ty + sink, flip=True, below=ty - 2)
        for j in range(3):      # rings spreading from where it goes down
            u = (k + j * 3) % 9
            ring(cv, tx, ty - 2, 12 + 4 * u, 4 + u, FOG if u < 5 else STONE, 6, only=(WATER, SOOT, DEEPMOSS))
        sprite(cv, S.SQUIRE, "idle", (k // 2) % 4, *HERO)
    else:
        cv = view(healed, *CLOSE_AT)
        u = k - VEIL
        if u < 7:               # the last rings off the pond
            ring(cv, 30, 28, 12 + 3 * u, 4 + u, FOG, 15, only=(WATER,))
        sprite(cv, S.SQUIRE, "parry" if k >= 13 else "idle", min(3, k - 13) if k >= 13 else (k // 2) % 4, *HERO2)
        cv = zoom2(cv)
    deep = chop(cv)
    lv = min(16, max(0, (k - 14) * 3))
    overlay(cv, ov, lv)
    lit = Lit(cv)
    glints = random.Random(0x6117)
    for y in range(H):
        for x in range(W):
            n = y * W + x
            if ov.px[n] != CLEAR and BAYER4[y & 3][x & 3] < lv:
                continue
            c = lit.c[n]
            if deep[n]:
                c = (44, 60, 70)
            if k < VEIL:        # the murk, the mist thickening over it
                c = MURK(c)
                m = smooth((k - 3) / (VEIL - 4)) * (0.55 + 0.6 * fbm(x / 26.0 + k * 0.15, y / 11.0 - k * 0.1, 3))
            else:               # dawn off the top right; the mist lifting, the foot of the frame first
                sun = 1.0 - math.hypot(x - 150, (y + 20) * 1.2) / 210.0
                if cv.px[n] == WATER or deep[n]:
                    c = mix(c, SKY, clamp(sun) * 0.42)
                c = DAWN[step(sun * 2.0 - 0.45, 2, x, y)](c)
                u = (k - VEIL) / (WIN_N - 1 - VEIL)
                edge = lerp(1.3, -0.5, smooth(u * 1.6))
                m = clamp((edge - y / H) * 2.0) * (0.65 + 0.5 * fbm(x / 24.0 - k * 0.1, y / 10.0, 4))
                m *= 1.0 - smooth((u - 0.45) / 0.45)            # gone by the last frames
            s = step(m, 4, x, y)
            if s:
                c = mix(c, MIST, (0.0, 0.42, 0.72, 1.0)[s])
            if y >= 100 and k >= VEIL:      # the foot of the frame, a step darker under the game's times
                if step((y - 100) / 14.0, 2, x, y):
                    c = (c[0] * 0.55, c[1] * 0.55, c[2] * 0.62)
            lit.c[n] = c
    if k >= VEIL:               # the sun's glints on the water, twinkling
        for _ in range(40):
            x, y, ph = glints.randrange(40, 128), glints.randrange(0, 90), glints.randrange(7)
            if cv.px[y * W + x] == WATER and (k + ph) % 7 < 3 and x + (90 - y) > 90:
                lit.put(x, y, (255, 236, 170))
    return lit


def win():
    rot, healed = world_idx(), world_idx(healed=True)
    ov = words([("THE SWAMP", 1, 5), ("IS QUIET", 2, 16)], 0x51E)
    frames = [win_scene(k, rot, healed, ov) for k in range(WIN_N)]
    pal = fit(frames, {0: 0x211, 5: 0xFFD, 10: 0xFC6, 14: 0xE43}, hold=8)
    return Clip("WINC", index(frames, pal), pal, WIN_MS)


# --- PMAP: the pause map ----------------------------------------------------------------------

def font35():
    """CHGame's 3x5 font (Draw.cpp's FONT35: '!'..'z', columns, bit 0 the top
    row), so the map's names are in the game's own letters."""
    src = FONT_SRC.read_text(encoding="utf-8")
    body = src[src.index("FONT35[FONT35_LAST"):]
    body = body[body.index("{") + 1:body.index("};")]
    cols = [tuple(int(v, 16) for v in g.split(",")) for g in re.findall(r"\{(0x[0-9A-Fa-f]{2}(?:,0x[0-9A-Fa-f]{2}){2})\}", body)]
    return {chr(ord("!") + i): c for i, c in enumerate(cols)}


def text35(cv, x, y, s, c, shade=None, font=None):
    """Like the game's text35s: 4 px a letter, shaded one down and right."""
    font = font or font35()
    for dx, dy, col in (((1, 1, shade),) if shade is not None else ()) + ((0, 0, c),):
        cx = x
        for ch in s:
            g = font.get(ch)
            if g:
                for i, bits in enumerate(g):
                    for j in range(6):
                        if bits >> j & 1:
                            cv.put(cx + i + dx, y + j + dy, col)
            cx += 4


# Region names on the map, world px of their middle.
MAP_NAMES = (("THE ROT", 900, 44), ("WILLOW HOLLOW", 420, 44), ("REED", 140, 164), ("SHALLOWS", 140, 228),
             ("THE LANDING", 210, 716), ("BEAVER DAM", 640, 372))
# Icons, '.' clear (digits: palette indices, a-f: 10-15): a nest still
# standing (the game crosses it out once cleared: Play.cpp's mapMarks), the toad.
NEST_MARK = (".000.", "0eee0", "0e5e0", "0eee0", ".000.")
TOAD_MARK = (".00...00.", "0aa000aa0", "088888880", "085555580", ".0888880.", "..0...0..")


def icon(cv, rows, x, y):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch != ".":
                cv.put(x + i, y + j, int(ch, 16))


def cross(cv, x, y):
    """The game's cross over a cleared nest (Play.cpp's mapMarks), centred at (x, y)."""
    for d in range(-3, 4):
        cv.put(x + d, y + d, SOOT)
        cv.put(x + d, y - d, SOOT)


def parchment(cv, seed):
    """The map's paper: mud with bone and bark in it, stained, burnt dark
    toward the edges."""
    for y in range(H):
        for x in range(W):
            e = min(x, y, W - 1 - x, H - 1 - y)
            n = fbm(x / 9.0, y / 9.0, seed)
            burn = e + (n - 0.5) * 9
            st = fbm(x / 5.0 + 40, y / 5.0, seed + 3)
            if burn < 1.2:
                c = SOOT
            elif burn < 3:
                c = UMBER if BAYER4[y & 3][x & 3] < 10 else SOOT
            elif burn < 5:
                c = PEAT if BAYER4[y & 3][x & 3] < 11 else UMBER
            elif burn < 7.5:
                c = BARK if BAYER4[y & 3][x & 3] < 9 else PEAT
            elif st > 0.68:
                c = BARK if BAYER4[y & 3][x & 3] < 6 else MUD
            elif st < 0.3 and BAYER4[y & 3][x & 3] < 3:
                c = BONE
            else:
                c = MUD
            cv.px[y * W + x] = c


def pause_map():
    """The swamp from above on parchment, in the game's palette."""
    idx = world_idx()
    cv = T.Canvas(MUD)
    parchment(cv, 0x9A9)
    # the world at 7/64: each map pixel the mean of its 9.14 px of world (in
    # linear light), mapped to the palette, a two-colour dither where between
    lit = Lit(T.Canvas(MUD))
    lin = [tuple(P._lin(v) for v in RGB[i]) for i in range(16)]
    for my in range(MAP_H):
        y0, y1 = my * 64 // MAP_CELL, (my + 1) * 64 // MAP_CELL
        for mx in range(MAP_W):
            x0, x1 = mx * 64 // MAP_CELL, (mx + 1) * 64 // MAP_CELL
            r = g = b = 0.0
            for y in range(y0, y1):
                o = y * WP.W
                for x in range(x0, x1):
                    q = lin[idx[o + x]]
                    r += q[0]
                    g += q[1]
                    b += q[2]
            k = (y1 - y0) * (x1 - x0)
            enc = lambda v: 255.0 * (v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055)
            c = (enc(r / k), enc(g / k), enc(b / k))
            # a touch more contrast and colour than an average gives
            yv = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
            c = tuple(clamp(yv + (v - yv) * 1.35 + (v - 128) * 0.12, 0, 255) for v in c)
            n = (MAP_Y + my) * W + MAP_X + mx
            lit.c[n] = c
            lit.soft[n] = 1
    pal = list(P.RGB444)
    m = index([lit], pal)[0]
    for my in range(MAP_H):
        for mx in range(MAP_W):
            n = (MAP_Y + my) * W + MAP_X + mx
            cv.px[n] = m[n]
    # its border: inked, a bark rule round it
    for x in range(MAP_X - 2, MAP_X + MAP_W + 2):
        for y, c in ((MAP_Y - 1, SOOT), (MAP_Y + MAP_H, SOOT), (MAP_Y - 2, BARK), (MAP_Y + MAP_H + 1, BARK)):
            if MAP_X - 1 <= x < MAP_X + MAP_W + 1 or c == BARK:
                cv.put(x, y, c)
    for y in range(MAP_Y - 2, MAP_Y + MAP_H + 2):
        for x, c in ((MAP_X - 1, SOOT), (MAP_X + MAP_W, SOOT), (MAP_X - 2, BARK), (MAP_X + MAP_W + 1, BARK)):
            if MAP_Y - 1 <= y < MAP_Y + MAP_H + 1 or c == BARK:
                cv.put(x, y, c)
    mpx = lambda wx: MAP_X + wx * MAP_CELL // 64
    mpy = lambda wy: MAP_Y + wy * MAP_CELL // 64
    # the toad in its pond, the nests still standing
    icon(cv, TOAD_MARK, mpx(TOAD[0]) - 4, mpy(TOAD[1]) - 7)
    o = WP.load_objects()
    for kind, wx, wy in o["nests"]:
        icon(cv, NEST_MARK, mpx(wx) - 2, mpy(wy) - 2)
    # the names
    font = font35()
    for name, wx, wy in MAP_NAMES:
        text35(cv, mpx(wx) - (len(name) * 4 - 1) // 2, mpy(wy) - 2, name, BONE, SOOT, font)
    # the header, in the title's lettering, inked
    rng = random.Random(0x3A9)
    T.letter(cv, "THE SWAMP", 1, 8, rng, BARK, PEAT, UMBER, SOOT, wobble=(0, 0, 1), moss=4, drips=2)
    # the key under it
    ky = MAP_Y + MAP_H + 6
    icon(cv, NEST_MARK, MAP_X + 2, ky)
    text35(cv, MAP_X + 10, ky, "NEST", PEAT, None, font)
    icon(cv, NEST_MARK, MAP_X + 32, ky)
    cross(cv, MAP_X + 34, ky + 2)
    text35(cv, MAP_X + 40, ky, "CLEARED", PEAT, None, font)
    icon(cv, TOAD_MARK, MAP_X + 72, ky - 1)
    text35(cv, MAP_X + 84, ky, "TOAD", PEAT, None, font)

    return Clip("PMAP", [bytes(cv.px)], P.RGB444, 0)


# --- the lot ------------------------------------------------------------------------------

def inputs_hash():
    h = hashlib.sha256()
    for p in (Path(__file__), HERE / "title.py", HERE / "clip.py", GAME / "tools" / "assets" / "palette.py",
              GAME / "tools" / "assets" / "quantise.py", GAME / "tools" / "assets" / "sheets.py", FONT_SRC):
        h.update(p.name.encode())
        h.update(p.read_bytes())
    for s in (S.SQUIRE, S.RACCOON, S.TURTLE, S.BEAVER, S.CHAMELEON, S.TOAD, S.TILESET):
        h.update(s.path.read_bytes())
    h.update(WP.inputs_hash().encode())
    return h.hexdigest()


fresh = False                   # build() rendered them now (not from the cache)


def build(force=False):
    """[Clip] in card order: TITL, DEAD, WINC, PMAP (cached)."""
    global fresh
    key = inputs_hash()
    fresh = False
    if not force and CACHE.exists():
        try:
            c = pickle.loads(CACHE.read_bytes())
            if c.get("hash") == key:
                return [Clip(*t) for t in c["clips"]]
        except Exception:
            pass
    fresh = True
    clips = [Clip("TITL", T.loop(), P.RGB444, T.LOOP_MS), death(), win(), pause_map()]
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_bytes(pickle.dumps(dict(hash=key, clips=[(c.tag, c.frames, c.palette, c.ms) for c in clips])))
    return clips


def image(frame, pal, scale=1):
    from PIL import Image
    im = Image.new("RGB", (W, H))
    p = [rgb444(v) for v in pal]
    im.putdata([p[c] for c in frame])
    return im.resize((W * scale, H * scale), Image.NEAREST) if scale > 1 else im


def previews(clips, scale=3):
    """out/art/clips/: each clip as a GIF (scale x) and all frames on a sheet."""
    from PIL import Image
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for c in clips:
        ims = [image(f, c.palette, scale) for f in c.frames]
        name = OUT / ("%s.gif" % c.tag.lower())
        if len(ims) > 1:
            ims[0].save(name, save_all=True, append_images=ims[1:], duration=c.ms, loop=0)
        else:
            ims[0].save(OUT / ("%s.png" % c.tag.lower()))
        rows.append([image(f, c.palette) for f in c.frames])
    cols = max(len(r) for r in rows)
    sheet = Image.new("RGB", (cols * 130, len(rows) * 130), (30, 26, 22))
    for j, r in enumerate(rows):
        for i, im in enumerate(r):
            sheet.paste(im, (i * 130, j * 130))
    sheet.save(OUT / "sheet.png")
    return OUT


if __name__ == "__main__":
    print(previews(build(force="--force" in sys.argv)))
