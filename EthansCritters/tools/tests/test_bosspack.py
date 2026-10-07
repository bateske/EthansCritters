#!/usr/bin/env python3
"""Ethan's Critters: tests for the toad packer (tools/assets/bosspack.py).

    python EthansCritters/tools/tests/test_bosspack.py

Every played frame decoded back from the TOAD section the way the game
reads it (row bands of 128 / wWords rows a block from the frame's first
block) lands on the quantised sheet's pixels at its pivot-relative place;
blank trailing frames are dropped, identical pictures stored once, every
frame starts on a fresh block and the unused rest of a block is zero; the
red copies (the hit flash) are REMAP_RED of the pictures, TOAD_RED on; the
generated Boss.{h,cpp} are current and the card's TOAD section is this.
"""
import sys
import unittest
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
ROOT = GAME.parent
sys.path.insert(0, str(GAME / "tools" / "assets"))
sys.path.insert(0, str(GAME / "tools" / "card"))
import bosspack as B  # noqa: E402
import palette as P  # noqa: E402
import quantise as Q  # noqa: E402
import sheets as S  # noqa: E402

PLAYED, ROWS = B.build()
DATA = B.pack(PLAYED)


class ToadTest(unittest.TestCase):
    def test_round_trip(self):
        t = S.TOAD
        idx, sw = Q.quantise_sheet(t), Q.sheet_image(t).size[0]
        cw, ch = t.cell
        for name, first, n in ROWS:
            a = t.anim(dict((c, i) for i, c in B.ROWS)[name])
            self.assertEqual(n, a.drawn, name)                  # blank trailing frames dropped
            for f in range(n):
                fr = PLAYED[first + f]
                got = {}
                for y, row in enumerate(B.decode(DATA, fr)):
                    self.assertEqual(len(row), fr.ww * 8)
                    for x, v in enumerate(row):
                        if v != P.KEY:
                            got[(t.pivot[0] + fr.ox + x, t.pivot[1] + fr.oy + y)] = v
                want = {}
                x0, y0 = (a.col + f) * cw, a.row * ch
                for y in range(ch):
                    for x in range(cw):
                        v = idx[(y0 + y) * sw + x0 + x]
                        if v != P.KEY:
                            want[(x, y)] = v
                self.assertEqual(got, want, "%s frame %d" % (name, f))
                red = P.remap_red()
                self.assertEqual(B.decode(DATA, fr, True), [[red[v] for v in row] for row in B.decode(DATA, fr)])

    def test_layout(self):
        pics = B.distinct(PLAYED)
        self.assertEqual(len({fr.key() for fr in pics}), len(pics))     # each picture once
        block = 0
        for fr in pics:
            self.assertEqual(fr.block, block)                   # each on a fresh block, in order
            self.assertEqual(fr.rows_per_block, 128 // fr.ww)
            for b in range(fr.blocks):                          # the rest of each block is zero
                rows = min(fr.rows_per_block, fr.h - b * fr.rows_per_block)
                tail = DATA[(block + b) * 512 + rows * fr.ww * 4:(block + b + 1) * 512]
                self.assertEqual(tail, bytes(len(tail)))
            block += fr.blocks
        self.assertEqual(len(DATA), 2 * block * 512)                   # and each in red
        self.assertLessEqual(max(fr.blocks for fr in pics), 6)
        self.assertEqual(len(PLAYED), 44)
        self.assertLess(block, 256)                             # ToadFrame.block is a byte
        self.assertIn("TOAD_RED = %d;" % block, B.header_text(PLAYED, ROWS))

    def test_generated_files_are_current(self):
        for name, text in (("Boss.h", B.header_text(PLAYED, ROWS)), ("Boss.cpp", B.source_text(PLAYED, ROWS))):
            self.assertEqual((B.SRC_ASSETS / name).read_text(encoding="utf-8"), text,
                             "%s is stale: run tools/mkcard.py" % name)
        card = ROOT / "out" / "card" / "CRITTERS.DAT"
        if card.exists():
            import cardfile
            self.assertEqual(cardfile.read(card).section("TOAD").data[:len(DATA)], DATA)

    def test_deterministic(self):
        self.assertEqual(B.pack(B.build()[0]), DATA)


if __name__ == "__main__":
    unittest.main()
