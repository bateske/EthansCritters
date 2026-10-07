#!/usr/bin/env python3
"""Ethan's Critters: tests for the sprite bank packer (tools/assets/packbank.py).

    python EthansCritters/tools/tests/test_packbank.py

Every played frame decoded back from the BANK section lands on the
quantised sheet's pixels at its pivot-relative place (the round trip);
what is left out (fishes, birds, ball, toad, alpha rows, blank frames); the block
layout (k frames a block at multiples of the slot, nothing straddles,
clips adjacent, a sheet's clips together); headers, checksums, the frame
map and the size table; the generated Bank.{h,cpp} are current; packing
is deterministic. (The game's own
reading of it: tools/tests/test_spool.cpp.)
"""
import sys
import unittest
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(GAME / "tools" / "assets"))
import packbank as B  # noqa: E402
import palette as P  # noqa: E402
import quantise as Q  # noqa: E402
import sheets as S  # noqa: E402

CLIPS = B.build()
DATA = B.pack(CLIPS)


class BankTest(unittest.TestCase):
    def test_round_trip(self):
        """Every played frame, decoded, is the sheet's cell: same pixels at the same place."""
        for c in CLIPS:
            sheet, a = c.sheet, c.anim
            idx, sw = B.sheet_pixels(sheet)
            cw, ch = sheet.cell
            for f in range(a.drawn):
                (ww, h, ox, oy, csum, flags, w, z), rows = B.decode(DATA, c, c.fmap[f])
                got = {}
                for y, row in enumerate(rows):
                    for x, v in enumerate(row):
                        if v != P.KEY:
                            got[(sheet.pivot[0] + ox + x, sheet.pivot[1] + oy + y)] = v
                want = {}
                x0, y0 = (a.col + f) * cw, a.row * ch
                for y in range(ch):
                    for x in range(cw):
                        v = idx[(y0 + y) * sw + x0 + x]
                        if v != P.KEY:
                            want[(x, y)] = v
                self.assertEqual(got, want, "%s frame %d" % (c.name, f))
                self.assertTrue(rows and all(len(r) == ww * 8 for r in rows))
                self.assertTrue(any(r[0] != P.KEY for r in rows) and any(r[w - 1] != P.KEY for r in rows))
                self.assertTrue(all(v == P.KEY for r in rows for v in r[w:]))   # padding is the key
                self.assertEqual((flags, z), (0, 0))

    def test_what_is_left_out(self):
        keys = {c.sheet.key for c in CLIPS}
        self.assertEqual(keys, {"squire", "raccoon", "turtle", "beaver", "chameleon",
                                "nest", "mush", "spawn", "stick", "gate", "swipe"})
        self.assertFalse([c for c in CLIPS if c.anim.alpha_of])
        for c in CLIPS:
            self.assertEqual(len(c.fmap), c.anim.drawn)          # blank trailing frames dropped
        self.assertEqual(sum(len(c.fmap) for c in CLIPS), 214)
        self.assertEqual(len(CLIPS), 42)

    def test_built_art(self):
        """props.py's clips: after every PNG sheet's (their numbers stay), named as the game
        expects, frames that fit a block, standing on their pivot."""
        names = [c.name for c in CLIPS]
        self.assertEqual(names[32:], ["CLIP_NEST_RACCOON", "CLIP_NEST_TURTLE", "CLIP_NEST_BEAVER",
                                      "CLIP_NEST_CHAMELEON", "CLIP_MUSH_RED", "CLIP_MUSH_WHITE", "CLIP_SPAWN_PUFF",
                                      "CLIP_STICK_SPIN", "CLIP_GATE_PART", "CLIP_SWIPE_ARC"])
        self.assertEqual(names[31], "CLIP_CHAMELEON_DEATH")
        for c in CLIPS[32:]:
            want = {"nest": 3, "mush": 2, "spawn": 4, "stick": 8, "gate": 4, "swipe": 1}[c.sheet.key]
            self.assertEqual((len(c.fmap), len(c.stored)), (want, want), c.name)   # every frame its own
            self.assertEqual(c.flags, 0)                         # the game picks the frame
            for fr in c.stored:
                self.assertLessEqual(len(fr), 512)
                self.assertLessEqual(len(fr), 488, c.name)       # the arena: a nest is the biggest frame
                if c.sheet.key == "stick":                       # (it spins about its middle)
                    off = max(abs(2 * fr.ox + fr.w), abs(2 * fr.oy + fr.h))
                    self.assertLessEqual(off, 4, c.name)         # (the twig: 2 px off at most)
                elif c.sheet.key == "gate":                      # a half: closed, it reaches the pivot
                    self.assertEqual(fr.oy + fr.h - 1, 0, c.name)
                    self.assertLessEqual(fr.ox + fr.w, 0, c.name)
                    self.assertEqual(c.stored[0].ox + c.stored[0].w, 0, c.name)
                elif c.sheet.key == "swipe":                     # the arc: in front of the feet, over the head to under them
                    self.assertGreaterEqual(fr.ox, 0, c.name)
                    self.assertLess(fr.oy, -12, c.name)
                    self.assertGreaterEqual(fr.oy + fr.h, -4, c.name)
                elif c.sheet.key != "spawn":
                    self.assertEqual(fr.oy + fr.h - 1, 0, c.name)    # the base on the pivot's row
                    self.assertLessEqual(abs(2 * fr.ox + fr.w), 8, c.name)   # (about) centred on it

    def test_layout(self):
        block = 0
        sheets_seen = []
        for c in CLIPS:
            self.assertEqual(c.first, block)                     # adjacent, in order
            block += c.blocks
            if not sheets_seen or sheets_seen[-1] != c.sheet.key:
                self.assertNotIn(c.sheet.key, sheets_seen)       # a sheet's clips together
                sheets_seen.append(c.sheet.key)
            self.assertEqual(c.slot % 4, 0)
            self.assertEqual(c.per_block, 512 // c.slot)
            self.assertLessEqual(max(len(f) for f in c.stored), c.slot)
            for s, fr in enumerate(c.stored):
                b, off = c.block_of(s)
                self.assertLessEqual(off + len(fr), 512)          # no frame straddles blocks
                self.assertEqual(DATA[b * 512 + off:b * 512 + off + len(fr)], fr.to_bytes())
        self.assertEqual(len(DATA), block * 512)

    def test_headers_and_tables(self):
        for c in CLIPS:
            self.assertEqual(sorted(set(c.fmap)), list(range(len(c.stored))))
            self.assertEqual(len(set(f.to_bytes() for f in c.stored)), len(c.stored))   # deduplicated
            for fr in c.stored:
                raw = fr.to_bytes()
                self.assertEqual(raw[4], sum(raw[8:]) & 255)
                self.assertEqual(len(raw), 8 + 4 * raw[0] * raw[1])
                self.assertEqual(len(raw) % 4, 0)
        src = B.source_text(CLIPS)
        self.assertIn("BANK_FSIZE[]", src)
        sizes = [len(f) // 4 for c in CLIPS for f in c.stored]
        self.assertEqual(max(sizes) * 4, max(c.slot for c in CLIPS))
        self.assertTrue(all(c.size_at == sum(len(d.stored) for d in CLIPS[:i]) for i, c in enumerate(CLIPS)))
        thrust = next(c for c in CLIPS if c.name == "CLIP_SQUIRE_THRUST")
        self.assertEqual((thrust.active, thrust.anim.ms), (0x45, 50))
        self.assertEqual(next(c for c in CLIPS if c.name == "CLIP_SQUIRE_WALK").flags, B.F_LOOP)
        self.assertEqual(thrust.flags, 0)

    def test_generated_files_are_current(self):
        for name, text in (("Bank.h", B.header_text(CLIPS)), ("Bank.cpp", B.source_text(CLIPS))):
            self.assertEqual((B.SRC_ASSETS / name).read_text(encoding="utf-8"), text,
                             "%s is stale: run tools/mkcard.py" % name)

    def test_deterministic(self):
        self.assertEqual(B.pack(B.build()), DATA)


if __name__ == "__main__":
    unittest.main()
