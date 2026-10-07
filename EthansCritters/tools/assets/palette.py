"""Ethan's Critters: THE palette (the single source of truth).

Sixteen colours, authored on the RGB444 grid because the panel runs at
12 bpp: what is written here is exactly what the board shows, and the
simulator, the previews and pal::init() agree. Everything else (the
quantiser, src/assets/Palette.{h,cpp}, the packers) reads this table.

The look is earthy, dirty and worn: soot to bone through browns, four
mosses, three greys, one rust accent and the swamp water. Index 0 is the
outline. Index 15 is the swamp water in the world and the transparent key
in sprites, so no opaque sprite pixel may use it.

Started from the earthy proposal in the plan (RGB888 rounded to RGB444);
TUNED says what changed after looking at the quantised previews
(tools/assets/quantise.py --preview) and why.
"""

# index, name, RGB444, what it is for
ENTRIES = (
    (0,  "soot",       0x221, "outline, deepest shadow"),
    (1,  "umber",      0x532, "dark brown: shadows, tunic shade, shell darks, belts"),
    (2,  "peat",       0x854, "brown shade, wet earth, the dirt"),
    (3,  "bark",       0xB75, "wood, fur, banks, sand shade"),
    (4,  "mud",        0xFD9, "sand and dry mud, skin shade, hair light"),
    (5,  "bone",       0xFFD, "skin, whites, cream"),
    (6,  "deepmoss",   0x342, "darkest foliage"),
    (7,  "bog",        0x563, "dark moss, grass tufts, reed shade"),
    (8,  "moss",       0x8A4, "the grass, reeds, lily pads"),
    (9,  "lichen",     0xCE6, "light moss, grass edges, turtle skin"),
    (10, "ochre",      0xFB4, "gold: the squire's hair, cattails, beaks"),
    (11, "fog",        0xCEE, "light cool grey: water glints, rock and fur lights"),
    (12, "slate",      0x666, "dark grey, fur, rock shade"),
    (13, "stone",      0xBBA, "grey: rock, steel, fur"),
    (14, "rust",       0xE43, "the one red accent: tunic, tongues, mushrooms"),
    (15, "swampwater", 0x467, "world water; the sprite transparent key"),
)

# The plan's proposal (RGB888) and what was changed, entry by entry.
PROPOSED = ("1d1a16", "3b2a22", "66432f", "977250", "bca27b", "e4d6b4", "26351f", "405a2c",
            "627c39", "93a457", "b8893c", "8ea69c", "5a5752", "a19d90", "a2382e", "3d5653")
# 2026-10-03, second pass after the user's review: the panel's viewing angle washes
# colours out, so each ramp's brightest colour goes to (near) full value and the
# shades below it are re-spaced to keep their steps visible off-axis.
TUNED = {
    1: "0x322 -> 0x532: a dark brown that still reads as brown at an angle (the first pass, 0x432, sank into soot on the panel)",
    2: "0x643 -> 0x854: the wet-earth shade, one step up so the dirt and the beaver's shade stay brown off-axis",
    3: "0x975 -> 0xB75: wood, fur and banks, redder and brighter (the banks' own colour, lifted for the panel)",
    4: "0xBA7 -> 0xFD9: the sand at full value; also the squire's skin shade and blond hair light",
    5: "0xDDB -> 0xFFD: full-value warm cream for skin and whites (rounding gave a green-grey that looked ill)",
    6: "0x232 -> 0x342: the darkest foliage, lifted so canopy shadows keep their green off-axis",
    7: "0x453 -> 0x563: dark moss, re-spaced under the brighter grass",
    8: "0x673 -> 0x8A4: the grass, brighter so the world is not murky on the TN panel",
    9: "0x9A5 -> 0xCE6: the green ramp's top at near full value: grass edges, turtle skin",
    10: "0xB84 -> 0xFB4: full-value dusty gold: hair, cattails, beaks (the drab brown hair of the first pass is gone)",
    11: "0x8A9 -> 0xCEE: light cool grey near full value: water glints, rock and fur lights, sleeves",
    12: "0x555 -> 0x666: dark grey, a step lighter, neutral",
    13: "0x998 -> 0xBBA: grey ramp re-spaced under fog",
    14: "0xA33 -> 0xE43: the one red accent, brighter (tunic, mushrooms, tongues) so it survives the viewing angle",
    15: "0x455 -> 0x467: the swamp water, bluer and lighter so ponds read as water, not slate",
}

COUNT = 16
OUTLINE = 0
KEY = 15        # sprites: transparent
WATER = 15      # world: swamp water

NAMES = tuple(e[1] for e in ENTRIES)
RGB444 = tuple(e[2] for e in ENTRIES)
USES = tuple(e[3] for e in ENTRIES)


def index(name):
    """Palette index of a colour name (or an int index, passed through)."""
    if isinstance(name, int):
        return name
    return NAMES.index(name)


def channels444(i):
    c = RGB444[i]
    return (c >> 8) & 15, (c >> 4) & 15, c & 15


def rgb888(i):
    """(r, g, b) 0..255: each 4-bit channel times 17, as the panel shows it."""
    r, g, b = channels444(i)
    return r * 17, g * 17, b * 17


def hex888(i):
    return "%02x%02x%02x" % rgb888(i)


def rgb565(i):
    """The 16-bit value pal::commit() sends to CHGfx (same bit widening)."""
    r, g, b = channels444(i)
    return (((r << 1) | (r >> 3)) << 11) | (((g << 2) | (g >> 2)) << 5) | ((b << 1) | (b >> 3))


def from888(r, g, b):
    """Round an RGB888 colour to the nearest RGB444 value."""
    q = lambda v: min(15, (v * 15 + 127) // 255)
    return (q(r) << 8) | (q(g) << 4) | q(b)


# --- colour science (CIELAB, D65, sRGB) shared by the quantiser and tests ---

def _lin(v):
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def lab(rgb):
    """sRGB (r, g, b) 0..255 -> CIELAB (L, a, b), D65 white."""
    r, g, b = (_lin(v) for v in rgb[:3])
    x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047
    y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b)
    z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883

    def f(t):
        return t ** (1.0 / 3.0) if t > 216.0 / 24389.0 else (24389.0 / 27.0 * t + 16.0) / 116.0

    fx, fy, fz = f(x), f(y), f(z)
    return 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)


def delta_e(lab1, lab2):
    """CIEDE2000 colour difference."""
    from math import atan2, cos, degrees, exp, hypot, radians, sin, sqrt
    L1, a1, b1 = lab1
    L2, a2, b2 = lab2
    c1, c2 = hypot(a1, b1), hypot(a2, b2)
    cm = (c1 + c2) / 2.0
    g = 0.5 * (1.0 - sqrt(cm ** 7 / (cm ** 7 + 25.0 ** 7)))
    a1p, a2p = a1 * (1.0 + g), a2 * (1.0 + g)
    c1p, c2p = hypot(a1p, b1), hypot(a2p, b2)
    h1p = degrees(atan2(b1, a1p)) % 360.0 if c1p else 0.0
    h2p = degrees(atan2(b2, a2p)) % 360.0 if c2p else 0.0
    dLp = L2 - L1
    dCp = c2p - c1p
    if c1p * c2p == 0:
        dhp = 0.0
    else:
        dhp = h2p - h1p
        if dhp > 180.0:
            dhp -= 360.0
        elif dhp < -180.0:
            dhp += 360.0
    dHp = 2.0 * sqrt(c1p * c2p) * sin(radians(dhp) / 2.0)
    Lm = (L1 + L2) / 2.0
    cmp_ = (c1p + c2p) / 2.0
    if c1p * c2p == 0:
        hm = h1p + h2p
    elif abs(h1p - h2p) <= 180.0:
        hm = (h1p + h2p) / 2.0
    elif h1p + h2p < 360.0:
        hm = (h1p + h2p + 360.0) / 2.0
    else:
        hm = (h1p + h2p - 360.0) / 2.0
    t = (1.0 - 0.17 * cos(radians(hm - 30.0)) + 0.24 * cos(radians(2.0 * hm))
         + 0.32 * cos(radians(3.0 * hm + 6.0)) - 0.20 * cos(radians(4.0 * hm - 63.0)))
    dtheta = 30.0 * exp(-(((hm - 275.0) / 25.0) ** 2))
    rc = 2.0 * sqrt(cmp_ ** 7 / (cmp_ ** 7 + 25.0 ** 7))
    sl = 1.0 + 0.015 * (Lm - 50.0) ** 2 / sqrt(20.0 + (Lm - 50.0) ** 2)
    sc = 1.0 + 0.045 * cmp_
    sh = 1.0 + 0.015 * cmp_ * t
    rt = -sin(radians(2.0 * dtheta)) * rc
    return sqrt((dLp / sl) ** 2 + (dCp / sc) ** 2 + (dHp / sh) ** 2
                + rt * (dCp / sc) * (dHp / sh))


LAB = tuple(lab(rgb888(i)) for i in range(COUNT))


# --- remap tables: colour -> colour, 16 entries (gfx_blit4k REMAP, gfx_remapRect,
# gfx_sprite4). quantise.py --header writes them into src/assets/Palette.{h,cpp},
# so they follow any change to the colours above. Index 15 always stays 15 (the
# sprite key, and the world's water under a remapRect keeps its own colour
# unless the table says otherwise: REMAP_DARK darkens it too).

# The hit flash: every colour to the step of a red ramp its lightness (CIELAB
# L*) falls in, so the shading survives and the sprite reads red: the darks
# stay soot/umber/peat, every mid and light tone becomes rust, only the
# near-white stays bone (eyes, a blade's glint).
RED_RAMP = ((19.0, "soot"), (33.0, "umber"), (48.0, "peat"), (95.0, "rust"), (101.0, "bone"))
# Swamp rot (corrupted critters): a grey ramp by lightness with mouldy green
# darks (bog sits between deepmoss and slate, so a mid shade and a mid light
# stay apart); the one red (eyes, tongues) is kept.
ROT_RAMP = ((19.0, "soot"), (34.0, "deepmoss"), (45.0, "bog"), (62.0, "slate"), (84.0, "stone"),
            (101.0, "fog"))
# The pause map's fog (M7b): every colour, the water too, to the parchment's
# browns by lightness, so the swamp he has not walked yet is a faded sepia
# print on the map's paper.
SEPIA_RAMP = ((30.0, "peat"), (70.0, "bark"), (101.0, "mud"))
DARK_L = 0.68           # REMAP_DARK aims at this much of a colour's lightness
LIGHT_L = 22.0          # REMAP_LIGHT aims this much lighter


def _ramp(ramp, i):
    return next(index(n) for top, n in ramp if LAB[i][0] < top)


def _nearest(target, cands):
    return min(cands, key=lambda j: delta_e(target, LAB[j]))


def remap_red():
    return [i if i == KEY else _ramp(RED_RAMP, i) for i in range(COUNT)]


def remap_white():
    """Every opaque colour to bone (a white hit flash; on gfx_blit4k,
    GFX_B4_SOLID | GFX_B4_INK(bone) draws the same without a table)."""
    return [i if i == KEY else index("bone") for i in range(COUNT)]


def remap_dark():
    """Each colour to a darker one of its own family (a shadow on the ground,
    gfx_remapRect): the nearest by delta-E to it at DARK_L of its lightness,
    among the colours at least 8 L* darker (never 15); soot stays soot."""
    out = []
    for i in range(COUNT):
        L, a, b = LAB[i]
        c = [j for j in range(COUNT) if j != KEY and LAB[j][0] <= L - 8]
        out.append(_nearest((L * DARK_L, a * 0.8, b * 0.8), c) if c else OUTLINE)
    return out


def remap_light():
    """Each colour to a lighter one of its own family (Sizzle's GOO highlight,
    a glint): the nearest to it LIGHT_L lighter, among the colours at least 6 L*
    lighter (never 15); the lightest stay."""
    out = []
    for i in range(COUNT):
        L, a, b = LAB[i]
        c = [j for j in range(COUNT) if j != KEY and LAB[j][0] >= L + 6]
        out.append(_nearest((min(100.0, L + LIGHT_L), a * 0.9, b * 0.9), c) if c else i)
    return out


def remap_rot():
    rust = index("rust")
    return [i if i in (KEY, rust) else _ramp(ROT_RAMP, i) for i in range(COUNT)]


def remap_sepia():
    return [_ramp(SEPIA_RAMP, i) for i in range(COUNT)]


# (C name, table maker, what it is for) in Palette.h order.
REMAPS = (
    ("REMAP_RED", remap_red, "the hit flash: a red ramp by lightness, the shading kept"),
    ("REMAP_WHITE", remap_white, "every opaque colour to bone: a white flash"),
    ("REMAP_DARK", remap_dark, "each colour a step darker: shadows on the ground (gfx_remapRect)"),
    ("REMAP_LIGHT", remap_light, "each colour a step lighter: highlights, glints"),
    ("REMAP_ROT", remap_rot, "swamp rot: greys and mouldy greens, the red kept (corrupted critters)"),
    ("REMAP_SEPIA", remap_sepia, "the pause map's fog: the parchment's browns by lightness, the water too"),
)


if __name__ == "__main__":
    for i in range(COUNT):
        print("%2d %-10s 0x%03X #%s  565=0x%04X  %s" % (i, NAMES[i], RGB444[i], hex888(i), rgb565(i), USES[i]))
