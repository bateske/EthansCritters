"""Ethan's Critters: what the shared CHGame tools need to know (the schema is
chgame/tools/gamecfg.py's docstring; `ec test`, `ec check` and `ec redraw`
read this)."""
from pathlib import Path

_TOOLS = Path(__file__).resolve().parent
_CHGAME = _TOOLS.parents[1] / "chgame"
_CHGFX = str(_CHGAME / "platform" / "board" / "arduino" / "CHGame" / "libraries" / "CHGfx")

# The host tests. card: the game's own card code (Card.cpp, PathF.cpp,
# CardFormat.h) on the CRITTERS.DAT tools/mkcard.py makes, through a
# pretend CHSd (tools/tests/stub/ stands in for CHGfx and CHGame).
TESTS = {
    "card": dict(sources=["tools/tests/test_card.cpp", "src/engine/Card.cpp", "src/engine/PathF.cpp",
                          "src/assets/Palette.cpp"],
                 includes=["chsd"], extra_flags=["-I" + str(_TOOLS / "tests" / "stub"), "-DCHGAME_DEBUG=0"],
                 cwd="game"),
    # world: the streamer (World.cpp at every x phase, across a run split),
    # the terrain and moving over it, the camera, on the same card.
    "world": dict(sources=["tools/tests/test_world.cpp", "src/engine/World.cpp", "src/engine/Card.cpp",
                           "src/engine/Terrain.cpp", "src/engine/Camera.cpp", "src/assets/WorldData.cpp"],
                  includes=["chsd", "lib"], extra_flags=["-I" + str(_TOOLS / "tests" / "stub")], cwd="game"),
    # spool: the frame cache (Spool.cpp) on the card's bank, through a pretend card::stream().
    "spool": dict(sources=["tools/tests/test_spool.cpp", "src/engine/Spool.cpp", "src/assets/Bank.cpp"],
                  includes=["chsd", "lib"], cwd="game"),
    # occl: the occluders (Occluders.cpp) on the card's OCCL and WRLD sections, with the real gfx_blit4k:
    # what a pass redraws is the world's own pixels, inside the actor, and all of the tree he is behind.
    "occl": dict(sources=["tools/tests/test_occl.cpp", "src/engine/Occluders.cpp", "src/engine/Card.cpp",
                          "src/assets/WorldData.cpp", _CHGFX + "/src/CHGfx_blit4k.cpp"],
                 includes=["chsd", "lib"], defines=["CHTEST", "CHSIM", "CH32X035"],
                 extra_flags=["-I" + _CHGFX + "/src", "-I" + str(_CHGAME / "tools" / "chsim" / "host")], cwd="game"),
    # blit4k: CHGfx patch P4, gfx_blit4k() against a pixel-at-a-time reference (the test is the library's).
    "blit4k": dict(sources=[_CHGFX + "/extras/tests/blit4k/test_blit4k.cpp", _CHGFX + "/src/CHGfx_blit4k.cpp"],
                   defines=["CHTEST", "CHSIM", "CH32X035"],
                   extra_flags=["-I" + _CHGFX + "/src", "-I" + str(_CHGAME / "tools" / "chsim" / "host")]),
}

# The Python tests, after the executables: the palette, sheets and quantiser
# (tools/assets/), the card file, clip and title (tools/card/, mkcard), the
# Path F clips (clips.py), the world's painter and card layout
# (tools/world/), the sprite bank, the toad.
EXTRA_TEST_SCRIPTS = ["tools/tests/test_assets.py", "tools/tests/test_cardfile.py", "tools/tests/test_clips.py",
                      "tools/tests/test_world.py", "tools/tests/test_packbank.py", "tools/tests/test_bosspack.py"]


def before_tests(ctx):
    """The card and src/assets/CardIndex.h from the current sources (mkcard
    is deterministic and leaves an unchanged CardIndex.h alone), so the host
    tests, the scripts and the device build all see this build's card. A
    failed mkcard stops the run: the old card and CardIndex.h agree with
    each other, so everything after would pass on stale data."""
    r = ctx.run(["tools/mkcard.py"])
    if r.returncode:
        raise SystemExit("mkcard failed:\n" + (r.stdout + r.stderr).strip())
    ctx.log("mkcard: " + r.stdout.splitlines()[0])


# Every script gets the card (nocard.txt pulls it out itself, X0/X1).
# file: out/card/CRITTERS.DAT at the repository root, as chdrive's default.
CARD = dict(scripts="", file="../out/card/CRITTERS.DAT", build=["tools/mkcard.py"])

# A card that stalls (M8a): the boot's warm-up timed against the simulator's
# slow first reads, card_faults.txt with failing streams, a card put back
# cold (held frames, or past card::GIVE_UP the card screen).
SIM_TESTS = ["tools/tests/sim_card_faults.py"]
