#!/usr/bin/env python3
"""Ethan's Critters: tests for the card tools (tools/card/, tools/mkcard.py).

    python EthansCritters/tools/tests/test_cardfile.py

The card file round trip (two sections, block alignment, CRCs, the build
hash, every way a damaged file is refused), the Path F clip layout against
gfx_fb's (even x in the low nibble), CardIndex.h, and that mkcard is
deterministic. (tools/tests/test_card.cpp checks the game's own parser
against the file mkcard wrote.)
"""
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(GAME / "tools" / "card"))
sys.path.insert(0, str(GAME / "tools" / "assets"))
sys.path.insert(0, str(GAME / "tools"))
import cardfile as C  # noqa: E402
import clip  # noqa: E402


def two_sections():
    card = C.CardFile()
    card.add_section("AAAA", bytes(range(256)) * 3)        # 768 B -> 2 blocks
    card.add_section("BB01", b"\x5a" * 512)                # exactly 1 block
    return card


class CardFileTest(unittest.TestCase):
    def test_round_trip(self):
        card = two_sections()
        data = card.to_bytes()
        self.assertEqual(len(data), 4 * C.BLOCK)            # header + 2 + 1
        back = C.parse(data)
        self.assertEqual([s.tag for s in back.sections], ["AAAA", "BB01"])
        self.assertEqual([(s.first, s.blocks) for s in back.sections], [(1, 2), (3, 1)])
        self.assertEqual(back.build_hash, card.build_hash)
        self.assertEqual(back.to_bytes(), data)

    def test_layout(self):
        card = two_sections()
        data = card.to_bytes()
        magic, version, count, bhash, total = struct.unpack_from("<8sHHII", data, 0)
        self.assertEqual((magic, version, count, total), (b"ECRTCARD", 1, 2, 4))
        a, b = card.sections
        # every section on a block boundary, zero padded, CRC over the padded bytes
        self.assertEqual(data[a.first * 512:a.first * 512 + 768], bytes(range(256)) * 3)
        self.assertEqual(data[a.first * 512 + 768:(a.first + a.blocks) * 512], bytes(256))
        self.assertEqual(data[b.first * 512:(b.first + 1) * 512], b"\x5a" * 512)
        for i, s in enumerate(card.sections):
            tag, first, blocks, crc = struct.unpack_from("<4sIII", data, 20 + 16 * i)
            self.assertEqual((tag.decode(), first, blocks), (s.tag, s.first, s.blocks))
            self.assertEqual(crc, zlib.crc32(data[first * 512:(first + blocks) * 512]))
        # the build hash: CRC32 over the used table entries, then the sections
        h = zlib.crc32(data[20:20 + 32] + data[512:])
        self.assertEqual(bhash, h)
        self.assertEqual(data[20 + 32:512], bytes(512 - 52))

    def test_empty_section_takes_a_block(self):
        card = C.CardFile()
        s = card.add_section("NULL", b"")
        self.assertEqual((s.first, s.blocks), (1, 1))
        C.parse(card.to_bytes())

    def test_refused(self):
        data = two_sections().to_bytes()

        def bad(d, what):
            with self.assertRaises(C.CardError, msg=what):
                C.parse(bytes(d))

        d = bytearray(data); d[0] ^= 1; bad(d, "magic")
        d = bytearray(data); d[8] = 2; bad(d, "format")
        d = bytearray(data); d[12] ^= 1; bad(d, "build hash")
        d = bytearray(data); d[16] = 5; bad(d, "total blocks")
        d = bytearray(data); d[700] ^= 0x80; bad(d, "section byte (crc)")
        d = bytearray(data); d[20 + 4] = 2; bad(d, "section not where the last one ended")
        d = bytearray(data); d[300] = 1; bad(d, "junk after the table")
        bad(data[:-512], "cut short")
        bad(data + bytes(512), "trailing block")
        bad(data[:-1], "not whole blocks")

    def test_bad_sections(self):
        card = C.CardFile()
        card.add_section("ONE1", b"x")
        for tag in ("ONE1", "one1", "TOOLONG", "AB"):
            with self.assertRaises(C.CardError, msg=tag):
                card.add_section(tag, b"y")
        for i in range(15):
            card.add_section("S%03d" % i, b"z")
        with self.assertRaises(C.CardError):
            card.add_section("FULL", b"!")
        C.parse(card.to_bytes())                            # 16 sections: the most

    def test_write_and_index(self):
        card = two_sections()
        with tempfile.TemporaryDirectory() as t:
            f = Path(t) / "out" / "CRITTERS.DAT"
            card.write(f)
            self.assertEqual(C.read(f).build_hash, card.build_hash)
            h = Path(t) / "CardIndex.h"
            self.assertTrue(card.emit_index(h))
            self.assertFalse(card.emit_index(h))            # unchanged: not rewritten
            text = h.read_bytes().decode()
            self.assertNotIn("\r", text)
            self.assertIn("GENERATED", text)
            self.assertIn("constexpr uint32_t CARD_HASH = 0x%08Xu;" % card.build_hash, text)
            self.assertIn("constexpr uint16_t CARD_FORMAT = 1;", text)
            self.assertIn("constexpr uint32_t CARD_BLOCKS = 4u;", text)
            self.assertIn("CARD_AAAA_FIRST = 1u, CARD_AAAA_BLOCKS = 2u;", text)
            self.assertIn("CARD_BB01_FIRST = 3u, CARD_BB01_BLOCKS = 1u;", text)


class ClipTest(unittest.TestCase):
    def test_fb_layout(self):
        idx = bytearray(128 * 128)
        idx[0], idx[1] = 3, 12              # row 0: x0 low nibble, x1 high nibble
        idx[8 * 128 + 127] = 9              # row 8 (block 1 of the frame), last pixel
        fb = clip.fb_bytes(idx)
        self.assertEqual(len(fb), 8192)
        self.assertEqual(fb[0], 0xC3)
        self.assertEqual(fb[8 * 64 + 63], 0x90)
        self.assertEqual(clip.fb_indices(fb), idx)

    def test_pack(self):
        a = bytes((x ^ y) & 15 for y in range(128) for x in range(128))
        b = bytes(15 - v for v in a)
        pal = list(range(0, 0x1000, 0x111))[:16]
        data = clip.pack([a, b], pal, ms=80)
        self.assertEqual(len(data), 512 + 2 * 16 * 512)
        self.assertEqual(data[:8], b"CLIP" + struct.pack("<HH", 2, 80))
        self.assertEqual(list(struct.unpack_from("<16H", data, 8)), pal)
        self.assertEqual(data[40:512], bytes(472))
        frames, pal2, ms = clip.unpack(data)
        self.assertEqual((frames, pal2, ms), ([bytearray(a), bytearray(b)], pal, 80))
        # block 1 + 16f + k holds rows 8k .. 8k+7 of frame f
        k, f = 5, 1
        blk = data[(1 + 16 * f + k) * 512:(2 + 16 * f + k) * 512]
        self.assertEqual(blk, clip.fb_bytes(b)[k * 512:(k + 1) * 512])

    def test_refused(self):
        frame = bytes(128 * 128)
        with self.assertRaises(ValueError):
            clip.pack([], [0] * 16)
        with self.assertRaises(ValueError):
            clip.pack([frame], [0] * 15)
        with self.assertRaises(ValueError):
            clip.pack([frame[:-1]], [0] * 16)
        with self.assertRaises(ValueError):
            clip.pack([bytes([255]) * (128 * 128)], [0] * 16)       # a tile hole left in


class MkcardTest(unittest.TestCase):
    def test_title_and_card(self):
        import clips
        import mkcard
        import palette as P
        import title
        still = title.compose()
        self.assertEqual(len(still), 128 * 128)
        self.assertLessEqual(max(still), 15)
        self.assertGreaterEqual(len(set(still)), 12)                # a picture, not a fill
        self.assertEqual(title.compose(), still)                    # deterministic
        loop = title.loop()
        self.assertEqual(len(loop), title.LOOP)
        self.assertEqual(len(set(loop)), title.LOOP)                # every frame its own
        one, two = mkcard.build(previews=False), mkcard.build(previews=False)
        self.assertEqual(one.to_bytes(), two.to_bytes())
        card = C.parse(one.to_bytes())
        t = card.section("TITL")
        self.assertEqual(t.first, 1)
        self.assertEqual(t.blocks, 1 + title.LOOP * clip.FRAME_BLOCKS)
        frames, pal, ms = clip.unpack(t.data)
        self.assertEqual(frames, [bytearray(f) for f in loop])
        self.assertEqual(pal, list(P.RGB444))
        self.assertEqual(ms, title.LOOP_MS)
        # the death and win clips, the pause map (tools/tests/test_clips.py has more)
        for tag, n, ms in (("DEAD", clips.DEATH_N, clips.DEATH_MS), ("WINC", clips.WIN_N, clips.WIN_MS), ("PMAP", 1, 0)):
            frames, pal, got = clip.unpack(card.section(tag).data)
            self.assertEqual((len(frames), got), (n, ms), tag)


if __name__ == "__main__":
    unittest.main()
