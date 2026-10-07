"""Ethan's Critters: the source sheets as data.

For every PNG in Sprites/: the cell size, the pivot (the point the game
places on the actor's position: the middle of the feet line), the
animations (name, row, frames, ms per frame, gameplay role) and the colour
overrides the quantiser applies before its nearest-colour search, so that
ramps keep their steps.

Sheets with an Aseprite .json take their animations from it: the name is in
the frame key ("Beaver Sprite Sheet (Idle) 0.ase"), the row and column come
from the frame rect, the ms from "duration". The roles, blank frames and
alpha frames below are checked against the json and the PNG by
tools/tests/test_assets.py.

Terms:
  role   what the game uses the animation for (idle, walk, attack, guard,
         hurt, death, ...); anims.toml maps behaviour to these later.
  blank  trailing frames the json lists but that are empty in the PNG (a
         pause after the last drawn frame); the packer drops them.
  alpha  frames drawn with whole-frame alpha < 255 (Chameleon Disappear /
         Reappear); not quantised: the game draws them by dithering the
         frames of `alpha_of` at runtime (gfx_blit4k DITHER).
  active (first, last) frames of an attack that hurt (the game's hitbox
         is live only then); tools/assets/packbank.py puts it in the clip
         table, src/assets/Bank.cpp.
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1]                  # EthansCritters/
ROOT = GAME.parent                      # the repository
SPRITES = ROOT / "Sprites"

DEFAULT_MS = 100                        # sheets without json: the Aseprite exports' rate


class Anim:
    def __init__(self, name, row, frames, role, ms=DEFAULT_MS, col=0, blank=0, alpha_of=None, active=None):
        self.name = name                # as in the source (json) or as confirmed by the user
        self.id = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
        self.row = row
        self.col = col                  # first column (always 0 in these sheets)
        self.frames = frames            # as listed, blank ones included
        self.role = role
        self.ms = ms                    # per frame (int)
        self.blank = blank
        self.alpha_of = alpha_of        # id of the animation dithered at runtime
        self.active = active            # (first, last) hurting frames of an attack

    @property
    def drawn(self):
        return self.frames - self.blank

    def __repr__(self):
        return "Anim(%r, row=%d, frames=%d, role=%r)" % (self.name, self.row, self.frames, self.role)


class Sheet:
    def __init__(self, key, file, cell, pivot, use, anims=(), overrides=None, ramps=(),
                 kind="sprite", notes="", json=None):
        self.key = key
        self.file = file
        self.path = SPRITES / file
        self._json = json               # the .json's name when it is not the PNG's
        self.cell = cell                # (w, h)
        self.pivot = pivot              # (x, y) inside a cell
        self.use = use                  # player, critter, boss, bird, prop, world, skip
        self.anims = anims if callable(anims) else list(anims)   # a loader for json sheets
        self.overrides = dict(overrides or {})   # source hex -> palette name
        self.ramps = list(ramps)        # (label, [source hex ...], distinct indices needed)
        self.kind = kind                # sprite (15 = transparent) or tiles (15 = water)
        self.notes = notes

    @property
    def json_path(self):
        p = SPRITES / self._json if self._json else self.path.with_suffix(".json")
        return p if p.exists() else None

    def anim(self, ident):
        for a in self.anims:
            if a.id == ident or a.name == ident:
                return a
        raise KeyError(ident)

    def __repr__(self):
        return "Sheet(%r)" % self.key


_KEY = re.compile(r"\((.+)\) (\d+)\.ase$")


def anims_from_json(path, cell, roles):
    """Animations of an Aseprite export, rows and columns from the frame rects.
    roles: json name -> role, or (role, {blank=, alpha_of=}). A name missing
    from roles is an error, so a re-exported sheet with a new row is noticed."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    frames = data["frames"]
    items = frames.items() if isinstance(frames, dict) else ((fr["filename"], fr) for fr in frames)
    order, seen = [], {}
    for key, fr in items:
        m = _KEY.search(key)
        if not m:
            raise ValueError("%s: unexpected frame key %r" % (path.name, key))
        name, n = m.group(1), int(m.group(2))
        r = fr["frame"]
        if (r["w"], r["h"]) != tuple(cell) or fr.get("rotated") or fr.get("trimmed"):
            raise ValueError("%s: frame %r is not a plain %dx%d cell" % (path.name, key, cell[0], cell[1]))
        if r["x"] % cell[0] or r["y"] % cell[1]:
            raise ValueError("%s: frame %r is off the cell grid" % (path.name, key))
        if name not in seen:
            seen[name] = []
            order.append(name)
        seen[name].append((n, r["x"] // cell[0], r["y"] // cell[1], fr["duration"]))
    anims = []
    for name in order:
        fl = sorted(seen[name])
        rows = {f[2] for f in fl}
        if len(rows) != 1:
            raise ValueError("%s: %r spans rows %s" % (path.name, name, sorted(rows)))
        col0 = fl[0][1]
        if [f[0] for f in fl] != list(range(len(fl))) or [f[1] for f in fl] != list(range(col0, col0 + len(fl))):
            raise ValueError("%s: %r frames are not consecutive cells" % (path.name, name))
        ms = {f[3] for f in fl}
        if len(ms) != 1:
            raise ValueError("%s: %r has mixed durations %s" % (path.name, name, sorted(ms)))
        if name not in roles:
            raise ValueError("%s: animation %r has no role in sheets.py" % (path.name, name))
        role = roles[name]
        extra = {}
        if isinstance(role, tuple):
            role, extra = role
        anims.append(Anim(name, rows.pop(), len(fl), role, ms=ms.pop(), col=col0, **extra))
    return anims


# --- the sheets -------------------------------------------------------------

# Ramps listed below must keep this many distinct palette indices after
# quantising (the "distinct levels preserved" check in report.txt and the
# tests). The overrides are what makes them pass.

SQUIRE = Sheet(
    "squire", "Squire Sprite Sheet.png", (64, 32), (32, 31), "player",
    anims=[  # user-confirmed rows; faces right. ms: the game's pace (M4)
        Anim("idle", 0, 4, "idle", ms=160),
        Anim("walk", 1, 8, "walk", ms=90),
        Anim("thrust", 2, 7, "attack", ms=50, active=(4, 5)),   # blade 7..19 px out on f4-f5
        Anim("parry", 3, 6, "guard", ms=50),    # f0-f2 raise the guard, f3-f5 swing back (riposte)
        Anim("damage", 4, 4, "hurt", ms=80),
        Anim("death", 5, 5, "death", ms=120),
    ],
    overrides={
        # hair: c6851e is the main hair, cea117 its light; ochre + mud keep
        # it blond (bark made it drab brown)
        "c6851e": "ochre",
        "cea117": "mud",
        "d7b84d": "mud",        # chest emblem, with c6851e
        "ffd19c": "bone",       # skin
        "e59363": "mud",        # skin shade (cheek, neck)
        "f1f0fd": "fog",        # eye whites (bone would melt them into the face)
        "535a48": "slate",      # pupils
        "9e293b": "rust",       # tunic: the one red
        "741a33": "peat",       # tunic shade: warm, and apart from the umber belt
        "dad9cc": "fog",        # sleeves: grey linen, so the hands and blade stand off them
        "b4b4ae": "stone",      # sleeve shade
        "ffffff": "bone",       # blade
        "9299a4": "stone",      # belt buckle
        "3b3b39": "umber",      # belt, trousers
        "32322f": "soot",       # trousers shade
        "af764b": "bark",       # boots
        "8f623f": "peat",       # boots shade
    },
    ramps=[
        ("hair", ["cea117", "c6851e"], 2),
        ("skin", ["ffd19c", "e59363"], 2),
        ("tunic", ["9e293b", "741a33"], 2),
        ("tunic vs skin", ["9e293b", "e59363", "ffd19c"], 3),
        ("hair vs skin", ["c6851e", "ffd19c"], 2),
        ("sleeve", ["dad9cc", "b4b4ae"], 2),
        ("blade vs sleeve", ["ffffff", "dad9cc"], 2),
        ("sleeve vs hand", ["dad9cc", "ffd19c"], 2),
        ("eye vs skin", ["f1f0fd", "ffd19c"], 2),
        ("boots", ["af764b", "8f623f"], 2),
        ("outline vs belt", ["2f2f2e", "3b3b39"], 2),
        ("tunic shade vs belt", ["741a33", "3b3b39"], 2),
    ],
)

RACCOON = Sheet(
    "raccoon", "Raccoon Sprite Sheet.png", (32, 32), (16, 31), "critter",
    anims=[
        Anim("idle", 0, 8, "idle"),
        Anim("run", 1, 8, "walk"),
        Anim("attack", 2, 4, "attack", active=(2, 2)),     # rear up (f1 tell), swipe (f2)
        Anim("death", 3, 4, "death"),
    ],
    overrides={
        "3b3b39": "umber",      # mask, dark fur and tail rings: apart from the soot outline
        "555555": "slate",
        "8e8e8e": "stone",
        "b5b5b5": "fog",        # face, light tail rings: the light step
        "f1f0fd": "fog",        # eye whites: bone read as orange eyes; the mask frames them
    },
    ramps=[("greys", ["2f2f2e", "3b3b39", "555555", "8e8e8e", "b5b5b5"], 5)],
)

TURTLE = Sheet(
    "turtle", "Turtle Sprite Sheet.png", (32, 32), (16, 31), "critter",
    anims=[
        Anim("idle_yawn", 0, 12, "idle"),   # also the wake-up and the boss roar
        Anim("idle_blink", 1, 10, "idle2"),
        Anim("walk", 2, 4, "walk"),
        Anim("sleep", 3, 6, "sleep"),
        Anim("bite", 4, 5, "attack", active=(2, 3)),
        Anim("shell", 5, 10, "shell"),      # hide; reversed to come out
    ],
    overrides={
        "55b456": "lichen",     # skin
        "4a924c": "moss",       # skin shade
        "916451": "bark",       # shell
        "69483a": "peat",       # shell shade
        "433a35": "umber",      # shell dark
        "ffffff": "bone",
        "bfbfbf": "stone",
        "c74b48": "rust",
        "a94340": "rust",
    },
    ramps=[
        ("skin", ["55b456", "4a924c"], 2),
        ("shell", ["916451", "69483a", "433a35"], 3),
    ],
)

BEAVER = Sheet(
    "beaver", "Beaver Sprite Sheet.png", (32, 32), (16, 20), "critter",
    anims=lambda: anims_from_json(BEAVER.json_path, BEAVER.cell, {
        "Idle": "idle",
        "Movement": "walk",
        "Movement with Stick": "carry",
        "Bite": ("attack", {"active": (2, 2)}),
        "Damage": "hurt",
        "Death": ("death", {"blank": 2}),   # f1 is the wood-chip burst
        "Idle Water": "swim_idle",
        "Movement Water": "swim",
        "Movement Water with Stick": "swim_carry",
        "Dive": "dive",
        "Ascent": "surface",
    }),
    overrides={
        "825235": "bark",       # fur
        "694129": "peat",       # fur shade
        "3b3b39": "umber",      # tail
        "69483a": "umber",      # stick (and the death burst's sticks)
        "916451": "bark",       # stick light
        "97b6e4": "fog",        # water ring
        "7a9ed4": "fog",
        "e0e0e0": "bone",       # teeth
        "c5c5c8": "fog",
        "94949a": "stone",
        "5b8635": "moss",       # leaves on the stick
    },
    ramps=[
        ("fur", ["825235", "694129"], 2),
        ("fur shade vs stick", ["694129", "69483a"], 2),
        ("stick", ["916451", "69483a"], 2),
    ],
)

CHAMELEON = Sheet(
    "chameleon", "Chameleon Sprite Sheet.png", (64, 32), (32, 20), "critter",
    anims=lambda: anims_from_json(CHAMELEON.json_path, CHAMELEON.cell, {
        "Idle": "idle",
        "Movement": "walk",
        "Tongue": ("attack", {"active": (2, 4)}),     # tell, then lash f2-f4
        "Damage": ("hurt", {"blank": 3}),
        "Death": ("death", {"blank": 3}),
        "Disappear": ("vanish", {"alpha_of": "idle"}),
        "Reappear": ("appear", {"alpha_of": "idle"}),
    }),
    overrides={
        "466c24": "bog",        # the greens become the swamp's own mosses:
        "5d9030": "moss",       # its camouflage really works
        "6fab3a": "lichen",
        "c169a7": "rust",       # tongue
        "f88bd9": "rust",
        "4f4f4e": "slate",
        "f1f0fd": "bone",
    },
    ramps=[("greens", ["466c24", "5d9030", "6fab3a"], 3)],
)

PIDGEON = Sheet(
    "pidgeon", "Pidgeon Sprite Sheet.png", (32, 32), (16, 21), "bird",
    anims=lambda: anims_from_json(PIDGEON.json_path, PIDGEON.cell, {
        "Idle": "idle",
        "Movement": "walk",
        "Peck": "eat",
        "Flight": "fly",
    }),
    overrides={
        "9299a4": "fog",        # back and wings: a blue-grey pigeon
        "676c73": "slate",
        "4c4c4b": "umber",
        "d5d7db": "bone",
        "653574": "umber",      # neck sheen: purple has no slot, keep it dark
        "7e4e8d": "peat",
        "cb7bba": "rust",       # feet
        "4b7922": "bog",
        "629b2f": "moss",
    },
    ramps=[("greys", ["d5d7db", "9299a4", "676c73", "4c4c4b"], 4)],
)

SEAGULL = Sheet(
    "seagull", "Seagull Sprite Sheet.png", (32, 32), (16, 22), "bird",
    anims=lambda: anims_from_json(SEAGULL.json_path, SEAGULL.cell, {
        "Idle": "idle",
        "Idle 2": "idle2",
        "Flap": "fly",
        "Glide": "glide",
        "Glide 2": "glide2",
        "Damage": "hurt",
        "Death": ("death", {"blank": 7}),
    }),
    overrides={
        "d5d7db": "bone",
        "9299a4": "stone",
        "676c73": "slate",
        "4c4c4b": "umber",
        "d8852b": "ochre",      # beak
        "cb7bba": "rust",       # feet
    },
    ramps=[("greys", ["d5d7db", "9299a4", "676c73"], 3)],
)


def _fish_roles():
    names = ["Anchovy", "Tuna Fish", "Shortfin Batfish", "Sailfish", "Great Barracuda",
             "Silver Salmon", "Alaska Pollock", "Red Parrot Fish", "Clown Fish", "Atlantic Cod",
             "Frontosa", "Blue Tang"]
    roles = {}
    for n in names:
        roles[n + " - Movement"] = "swim"
        roles[n + " - Death"] = "death"
    roles["Tuna Fish -  Death"] = "death"   # the export's double space
    return roles


FISHES = Sheet(
    "fishes", "Fishes Sprite Sheet.png", (64, 32), (32, 16), "skip",
    anims=lambda: anims_from_json(FISHES.json_path, FISHES.cell, _fish_roles()),
    notes="not in the game (decided with the user); previewed only",
)

TOAD = Sheet(
    "toad", "Giant Toad Sprite Sheet - Green.png", (128, 96), (64, 72), "boss",
    json="Giant Toad Sprite Sheet.json",
    anims=lambda: anims_from_json(TOAD.json_path, TOAD.cell, {
        "Idle": "idle",
        "Idle 2": "idle2",
        "Movement": "walk",
        "Attack": "attack",
        "Damage": ("hurt", {"blank": 4}),
        "Death": "death",
    }),
    overrides={
        "5d9030": "moss",       # skin: the chameleon's greens, so the same mosses
        "466c24": "bog",
        "39571e": "deepmoss",   # warts, skin shade
        "e7e7e7": "bone",       # belly
        "bdbdbd": "fog",        # belly shade
        "f1f0fd": "bone",
        "f88bd9": "rust",       # tongue
        "c169a7": "peat",       # tongue shade, mouth: keeps the tongue's volume
        "eb8224": "ochre",      # eyes
        "ebc824": "mud",
    },
    ramps=[
        ("skin", ["5d9030", "466c24", "39571e"], 3),
        ("belly", ["e7e7e7", "bdbdbd"], 2),
        ("eye", ["eb8224", "ebc824"], 2),
        ("tongue", ["f88bd9", "c169a7"], 2),
    ],
    notes="added to Sprites/ during M1a (json names the PNG without ' - Green'); the boss "
          "(M7), on the card in its own section (bosspack.py), not in the bank",
)

BALL = Sheet(
    "ball", "Ball.png", (4, 4), (2, 2), "prop",
    anims=[Anim("ball", 0, 1, "projectile")],     # assumed: the beaver's mud spit
    overrides={
        "285daa": "peat",       # blue in the source; as mud spit it reads as mud
        "2d6ac5": "bark",
    },
)

TILESET = Sheet(
    "tileset", "Swamp Tileset - Blue.png", (16, 16), (0, 0), "world", kind="tiles",
    overrides={
        "5483af": "swampwater",  # the water
        "7fb3e4": "fog",         # water edge glint
        "c5c5c8": "fog",         # rocks: light top
        "c6c8c3": "fog",
        "b4b4b8": "fog",
        "a4a4a8": "stone",       # rocks: shade
        "94949a": "stone",
        "6da03f": "moss",        # grass fill
        "6da23e": "moss",
        "78ac4a": "lichen",      # grass light
        "83b655": "lichen",
        "5b8635": "bog",         # grass tufts, a step under the fill
        "64933b": "bog",
        "4a9a51": "moss",        # reeds, lily pads, moss on rocks: moss reads on the water
        "387e33": "bog",         # their shade
        "376929": "deepmoss",
        "45632b": "deepmoss",
        "235828": "deepmoss",
        "25602a": "deepmoss",
        "d6ba95": "mud",         # sand
        "c7a687": "mud",
        "c4a787": "mud",
        "b09072": "bark",        # sand shade
        "926449": "bark",        # dirt bank
        "80573f": "peat",        # dirt
        "644330": "umber",
        "e27870": "rust",        # mushroom caps
        "ba5b54": "rust",
        "faefd9": "bone",        # mushroom cream
        "e6e6ae": "bone",
    },
    ramps=[
        ("grass", ["6da03f", "5b8635", "78ac4a"], 3),
        ("dirt", ["926449", "80573f"], 2),
        ("sand vs grass", ["d6ba95", "6da03f"], 2),
        ("mushroom", ["e27870", "faefd9"], 2),
        ("rock", ["c5c5c8", "94949a"], 2),
        ("lily pad vs water", ["4a9a51", "387e33", "5483af"], 3),
    ],
)

ALL = [SQUIRE, RACCOON, TURTLE, BEAVER, CHAMELEON, PIDGEON, SEAGULL, FISHES, TOAD, BALL, TILESET]
SPRITE_SHEETS = [s for s in ALL if s.kind == "sprite"]
BY_KEY = {s.key: s for s in ALL}


# The files the tools read from Sprites/: Elthen's sprite packs, bought, and
# not in the repository (not ours to republish). Every tool that reads art
# imports this module, so a clone without them stops here, saying what to do.
SOURCE_FILES = (
    "Squire Sprite Sheet.png", "Raccoon Sprite Sheet.png", "Turtle Sprite Sheet.png",
    "Beaver Sprite Sheet.png", "Beaver Sprite Sheet.json", "Chameleon Sprite Sheet.png",
    "Chameleon Sprite Sheet.json", "Giant Toad Sprite Sheet - Green.png", "Giant Toad Sprite Sheet.json",
    "Pidgeon Sprite Sheet.png", "Pidgeon Sprite Sheet.json", "Seagull Sprite Sheet.png",
    "Seagull Sprite Sheet.json", "Fishes Sprite Sheet.png", "Fishes Sprite Sheet.json", "Ball.png",
    "Swamp Tileset - Blue.png",
)


def _check_sources():
    missing = [f for f in SOURCE_FILES if not (SPRITES / f).is_file()]
    if missing:
        raise SystemExit(
            f"Sprites/ lacks {len(missing)} of the {len(SOURCE_FILES)} source files: {', '.join(missing)}\n"
            "They are Elthen's sprite packs (https://elthen.itch.io/), bought, and not in the repository.\n"
            "Put your copy in Sprites/ to build the card. The program builds without them (ec build), and\n"
            "plays with the CRITTERS.DAT of the release made from the same commit (README: Building).")


def _resolve():
    # JSON-backed sheets read their animations on import, once every sheet
    # exists (the loaders refer to the sheet objects).
    for s in ALL:
        if callable(s.anims):
            s.anims = s.anims()


_check_sources()
_resolve()


if __name__ == "__main__":
    for s in ALL:
        print("%-10s %-30s cell %dx%d pivot %s use %s" % (s.key, s.file, s.cell[0], s.cell[1], s.pivot, s.use))
        for a in s.anims:
            extra = (" blank %d" % a.blank if a.blank else "") + (" alpha of %s" % a.alpha_of if a.alpha_of else "")
            print("    row %2d  %-28s %2d x %3d ms  %s%s" % (a.row, a.name, a.frames, a.ms, a.role, extra))
