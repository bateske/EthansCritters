#!/usr/bin/env python3
"""Ethan's Critters: build the card file, out/card/CRITTERS.DAT.

    python EthansCritters/tools/mkcard.py [--out FILE] [--no-index]

Packs every section (tools/card/cardfile.py has the format) and regenerates
src/assets/CardIndex.h, the build hash and section blocks the game is
compiled against (only written when it changed, so an unchanged card costs
no rebuild). Deterministic: the same sources give the same bytes, so the
simulator runs repeat and a card made on another PC matches this build.

Copy CRITTERS.DAT to the root of the SD card (a freshly formatted FAT32
card keeps it in one piece; the game accepts up to 8 pieces). The simulator
gets it by itself (tools/chsim/chdrive.py).

Sections:
  TITL  the title's loop (Path F clip, tools/card/title.py via clips.py)
  BANK  the sprite bank (tools/assets/packbank.py): every animation's
        frames for gfx_blit4k; src/assets/Bank.{h,cpp} index it
  WRLD  the world, one painted bitmap in column blocks (tools/world/paint.py
        paints it, cached in out/world/; tools/world/worldpack.py lays it
        out); also regenerates src/assets/WorldData.{h,cpp}
  OCCL  the willows an actor can get behind, each as the world shows it, in
        row bands (tools/world/occlpack.py; WorldData.h indexes them)
  TOAD  the giant toad, every frame in row bands for streaming straight to
        the screen (tools/assets/bosspack.py); src/assets/Boss.{h,cpp} index it
  DEAD  the death clip, WINC the win clip, PMAP the pause map (Path F clips
        with their own palettes, tools/card/clips.py; cached in out/art/;
        previews in out/art/clips/ when they are rendered again)
"""
import argparse
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parent
sys.path.insert(0, str(GAME / "tools" / "card"))
sys.path.insert(0, str(GAME / "tools" / "assets"))
import bosspack  # noqa: E402
import cardfile  # noqa: E402
import clip  # noqa: E402
import clips  # noqa: E402
import packbank  # noqa: E402
sys.path.insert(0, str(GAME / "tools" / "world"))
import paint as world_paint  # noqa: E402

OUT = ROOT / "out" / "card" / "CRITTERS.DAT"
INDEX = GAME / "src" / "assets" / "CardIndex.h"


def build(previews=True):
    """The card (a cardfile.CardFile), section by section; previews: also
    write the pictures to out/art/."""
    card = cardfile.CardFile()
    cl = {c.tag: c for c in clips.build()}     # (M7b: the title's loop, the death and win clips, the map)
    if previews and clips.fresh:
        clips.previews(list(cl.values()))
    t = cl.pop("TITL")
    card.add_section("TITL", clip.pack(t.frames, t.palette, t.ms))
    card.bank = packbank.build()                # the sprite bank (M4)
    card.add_section("BANK", packbank.pack(card.bank))
    # the world (and the WorldData.{h,cpp} the game is compiled against)
    world = world_paint.build()
    world_paint.emit_sources(world)
    if previews and world.fresh:
        world_paint.previews(world)
    card.add_section("WRLD", world.section)
    card.add_section("OCCL", world.occl_pack[0])
    card.toad = bosspack.build()                # the boss (M7)
    card.add_section("TOAD", bosspack.pack(card.toad[0]))
    for c in cl.values():                       # the death and win clips, the pause map (M7b)
        card.add_section(c.tag, clip.pack(c.frames, c.palette, c.ms))
    return card


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=str(OUT), help="the card file (default out/card/CRITTERS.DAT)")
    ap.add_argument("--no-index", action="store_true", help="leave src/assets/CardIndex.h alone")
    a = ap.parse_args(argv)
    card = build()
    data = card.write(a.out)
    cardfile.parse(data)                # what was written reads back
    wrote = False if a.no_index else card.emit_index(INDEX)
    bank = [] if a.no_index else packbank.emit(card.bank)
    toad = [] if a.no_index else bosspack.emit(*card.toad)
    print("%s: %d blocks (%d KB), hash %08X" % (a.out, card.total_blocks, len(data) // 1024, card.build_hash))
    for s in card.sections:
        print("  %s  blocks %5d..%-5d crc %08X" % (s.tag, s.first, s.first + s.blocks - 1, s.crc))
    if not a.no_index:
        print("%s %s" % (INDEX.relative_to(ROOT).as_posix(), "written" if wrote else "unchanged"))
        print("src/assets/Bank.{h,cpp} %s" % (("written: " + ", ".join(bank)) if bank else "unchanged"))
        print("src/assets/Boss.{h,cpp} %s" % (("written: " + ", ".join(toad)) if toad else "unchanged"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
