#!/usr/bin/env python3
"""Ethan's Critters: tests for the Path F clips (tools/card/clips.py).

    python EthansCritters/tools/tests/test_clips.py

The clips render the same twice (the cache against a fresh render), each
frame is the game's 128x128 in its clip's sixteen colours, the death and
win clips keep the game's words readable (slot 0 dark, 5 light), the pause
map is in the game's palette and sits where Play.cpp draws its marks
(MAP_X, MAP_Y, MAP_CELL), the palette fit keeps its pins, and the ordered
two-colour mix lands between the two colours.
"""
import re
import sys
import unittest
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(GAME / "tools" / "card"))
sys.path.insert(0, str(GAME / "tools" / "assets"))
import clips  # noqa: E402
import palette as P  # noqa: E402
import title  # noqa: E402


def light(v):
    return (v >> 8) + (v >> 4 & 15) + (v & 15)


class ClipsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.clips = {c.tag: c for c in clips.build()}

    def test_order_and_shape(self):
        self.assertEqual([c.tag for c in clips.build()], ["TITL", "DEAD", "WINC", "PMAP"])
        for c in self.clips.values():
            self.assertEqual(len(c.palette), 16)
            for f in c.frames:
                self.assertEqual(len(f), 128 * 128)
                self.assertLessEqual(max(f), 15)

    def test_counts_and_rates(self):
        c = self.clips
        self.assertEqual((len(c["TITL"].frames), c["TITL"].ms), (title.LOOP, title.LOOP_MS))
        self.assertEqual((len(c["DEAD"].frames), c["DEAD"].ms), (clips.DEATH_N, clips.DEATH_MS))
        self.assertEqual((len(c["WINC"].frames), c["WINC"].ms), (clips.WIN_N, clips.WIN_MS))
        self.assertEqual((len(c["PMAP"].frames), c["PMAP"].ms), (1, 0))
        # the game shows A: GET UP and the times once a clip has played: Play.cpp's
        # GETUP_T - WORDS_T and 255 - WIN_T ticks (60 Hz) after it begins
        src = (GAME / "src" / "game" / "Play.cpp").read_text(encoding="utf-8")
        words, getup = (int(v) for v in re.search(r"WORDS_T = (\d+), GETUP_T = (\d+)", src).groups())
        win = int(re.search(r"WIN_T = (\d+)", src).group(1))
        self.assertLessEqual(clips.DEATH_N * clips.DEATH_MS * 60 // 1000, getup - words)
        self.assertLessEqual(clips.WIN_N * clips.WIN_MS * 60 // 1000, 255 - win)

    def test_palettes(self):
        c = self.clips
        self.assertEqual(c["TITL"].palette, list(P.RGB444))
        self.assertEqual(c["PMAP"].palette, list(P.RGB444))
        for tag in ("DEAD", "WINC"):
            pal = c[tag].palette
            self.assertLessEqual(light(pal[0]), 6, tag)        # C_SOOT: the words' shade
            self.assertGreaterEqual(light(pal[5]), 38, tag)    # C_BONE: the words
            self.assertEqual(len(set(pal)), 16, tag)           # no colour wasted
            self.assertNotEqual(pal, list(P.RGB444), tag)      # its own

    def test_map_where_play_draws(self):
        src = (GAME / "src" / "game" / "Play.cpp").read_text(encoding="utf-8")
        m = re.search(r"MAP_X = (\d+), MAP_Y = (\d+), MAP_CELL = (\d+)", src)
        self.assertEqual(tuple(int(v) for v in m.groups()), (clips.MAP_X, clips.MAP_Y, clips.MAP_CELL))
        f = self.clips["PMAP"].frames[0]
        # the inked border round the map, soot
        for x in range(clips.MAP_X, clips.MAP_X + clips.MAP_W):
            self.assertEqual(f[(clips.MAP_Y - 1) * 128 + x], clips.SOOT)
            self.assertEqual(f[(clips.MAP_Y + clips.MAP_H) * 128 + x], clips.SOOT)

    def test_fit_keeps_pins(self):
        cv = title.Canvas(clips.MOSS)
        for x in range(64):
            for y in range(128):
                cv.put(x, y, clips.RUST if y < 20 else clips.WATER)
        pal = clips.fit([clips.Lit(cv)], {0: 0x123, 5: 0xFED})
        self.assertEqual((pal[0], pal[5]), (0x123, 0xFED))
        self.assertIn(P.RGB444[clips.MOSS], pal)
        self.assertIn(P.RGB444[clips.WATER], pal)

    def test_mix(self):
        pal = [0x000] * 16
        pal[1], pal[2] = 0x444, 0x888
        cv = title.Canvas(0)
        lit = clips.Lit(cv)
        for n in range(len(lit.c)):
            lit.c[n] = (102.0, 102.0, 102.0)            # half way: 0x666
            lit.soft[n] = 1
        f = clips.index([lit], pal)[0]
        self.assertEqual(set(f), {1, 2})
        self.assertLess(abs(f.count(1) - f.count(2)), 128 * 128 // 8)

    def test_deterministic(self):
        fresh = clips.build(force=True)
        for a in fresh:
            b = self.clips[a.tag]
            self.assertEqual((a.frames, a.palette, a.ms), (b.frames, b.palette, b.ms), a.tag)


if __name__ == "__main__":
    unittest.main()
