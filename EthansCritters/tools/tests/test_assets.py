"""Ethan's Critters: tests of the art pipeline's palette, sheet data and quantiser.

    python -m unittest discover -s EthansCritters/tools/tests      (from the repository root)
"""
import sys
import unittest
from collections import Counter
from pathlib import Path

ASSETS = Path(__file__).resolve().parents[1] / "assets"
sys.path.insert(0, str(ASSETS))
import palette as P     # noqa: E402
import quantise as Q    # noqa: E402
import sheets as S      # noqa: E402

from PIL import Image   # noqa: E402


class PaletteTest(unittest.TestCase):
    def test_sixteen_valid_rgb444(self):
        self.assertEqual(len(P.ENTRIES), 16)
        self.assertEqual([e[0] for e in P.ENTRIES], list(range(16)))
        self.assertEqual(len(set(P.NAMES)), 16)
        for i, c in enumerate(P.RGB444):
            self.assertIsInstance(c, int)
            self.assertTrue(0 <= c <= 0xFFF, "entry %d 0x%X is not RGB444" % (i, c))
            for v in P.channels444(i):
                self.assertTrue(0 <= v <= 15)

    def test_unique_colours(self):
        self.assertEqual(len(set(P.RGB444)), 16)

    def test_conversions(self):
        for i in range(16):
            r, g, b = P.channels444(i)
            self.assertEqual(P.rgb888(i), (r * 17, g * 17, b * 17))
            # pal::commit()'s widening
            self.assertEqual(P.rgb565(i), (((r << 1) | (r >> 3)) << 11) | (((g << 2) | (g >> 2)) << 5)
                             | ((b << 1) | (b >> 3)))
        self.assertEqual(P.rgb565(P.index("bone")) >> 11, (P.channels444(P.index("bone"))[0] << 1) | 1)
        self.assertEqual(P.from888(0xFF, 0x00, 0x88), 0xF08)

    def test_roles(self):
        self.assertEqual(P.KEY, 15)
        self.assertEqual(P.WATER, 15)
        self.assertEqual(P.OUTLINE, 0)
        # the outline is the darkest colour
        self.assertEqual(min(range(16), key=lambda i: P.LAB[i][0]), P.OUTLINE)

    def test_every_change_from_the_proposal_is_explained(self):
        self.assertEqual(len(P.PROPOSED), 16)
        changed = {i for i, h in enumerate(P.PROPOSED)
                   if P.from888(*Q.hex_rgb(h)) != P.RGB444[i]}
        self.assertEqual(changed, set(P.TUNED), "TUNED must explain exactly the entries that differ")

    def test_generated_cpp_is_current(self):
        h, c = Q.header_text()
        for name, want in (("Palette.h", h), ("Palette.cpp", c)):
            path = Q.SRC_ASSETS / name
            self.assertTrue(path.exists(), "%s missing: run quantise.py --header" % path)
            self.assertEqual(path.read_bytes().decode("utf-8"), want,
                             "%s is stale: run tools/assets/quantise.py --header" % name)
            self.assertNotIn(b"\r", path.read_bytes())
        self.assertIn("extern const uint16_t PALETTE[16];", h)
        for i in range(16):
            self.assertIn("0x%03X," % P.RGB444[i], c)


class RemapTest(unittest.TestCase):
    """The remap tables (palette.py, written into Palette.{h,cpp})."""
    def test_red_is_a_ramp_by_lightness(self):
        red = P.remap_red()
        self.assertEqual(red[P.KEY], P.KEY)
        ramp = [P.index(n) for _, n in P.RED_RAMP]
        self.assertTrue(set(red[:15]) <= set(ramp))
        order = sorted(range(15), key=lambda i: P.LAB[i][0])
        steps = [ramp.index(red[i]) for i in order]
        self.assertEqual(steps, sorted(steps))                  # lighter never maps darker
        self.assertEqual(red[P.index("rust")], P.index("rust"))
        self.assertGreaterEqual(sum(v == P.index("rust") for v in red), 8)   # it reads red
        self.assertGreaterEqual(len(set(red[:15])), 4)          # the shading survives

    def test_white(self):
        self.assertEqual(P.remap_white(), [P.index("bone")] * 15 + [P.KEY])

    def test_dark_and_light_step_the_right_way(self):
        dark, light = P.remap_dark(), P.remap_light()
        for i in range(16):
            self.assertNotEqual(dark[i], P.KEY)
            self.assertNotEqual(light[i], P.KEY)
            if i != P.OUTLINE:
                self.assertLess(P.LAB[dark[i]][0], P.LAB[i][0] - 7, P.NAMES[i])
            if light[i] != i:
                self.assertGreater(P.LAB[light[i]][0], P.LAB[i][0] + 5, P.NAMES[i])
        self.assertEqual(dark[P.OUTLINE], P.OUTLINE)
        # the ground's own ramps: sand -> bark, grass -> bog, bog -> deepmoss
        self.assertEqual(dark[P.index("mud")], P.index("bark"))
        self.assertEqual(dark[P.index("moss")], P.index("bog"))
        self.assertEqual(dark[P.index("bog")], P.index("deepmoss"))

    def test_rot(self):
        rot = P.remap_rot()
        self.assertEqual(rot[P.KEY], P.KEY)
        self.assertEqual(rot[P.index("rust")], P.index("rust"))
        allowed = {P.index(n) for _, n in P.ROT_RAMP} | {P.index("rust")}
        self.assertTrue(set(rot[:15]) <= allowed)

    def test_tables_in_the_header(self):
        h, c = Q.header_text()
        for name, make, _ in P.REMAPS:
            self.assertIn("extern const uint8_t %s[16];" % name, h)
            self.assertIn("const uint8_t %s[16] = {%s};" % (name, ", ".join(str(v) for v in make())), c)


class MapImageTest(unittest.TestCase):
    def img(self, pixels, w):
        im = Image.new("RGBA", (w, len(pixels) // w))
        im.putdata(pixels)
        return im

    def test_sprite_alpha_and_key(self):
        water = P.rgb888(P.WATER)
        im = self.img([(255, 0, 255, 0), (12, 200, 7, 127), water + (255,), water + (128,)], 4)
        idx = Q.map_image(im, None, sprite=True)
        self.assertEqual(idx[0], P.KEY)         # alpha 0: junk RGB ignored
        self.assertEqual(idx[1], P.KEY)         # alpha < 128
        self.assertNotEqual(idx[2], P.KEY)      # the water colour, opaque, in a sprite
        self.assertNotEqual(idx[3], P.KEY)

    def test_tiles(self):
        water = P.rgb888(P.WATER)
        im = self.img([water + (255,), (1, 2, 3, 0)], 2)
        idx = Q.map_image(im, None, sprite=False)
        self.assertEqual(idx[0], P.WATER)
        self.assertEqual(idx[1], Q.HOLE)

    def test_exact_and_override(self):
        pix = [P.rgb888(i) + (255,) for i in range(15)]
        idx = Q.map_image(self.img(pix, 15), None)
        self.assertEqual(list(idx), list(range(15)))
        idx = Q.map_image(self.img(pix[:1], 1), {P.hex888(0): "rust"})
        self.assertEqual(idx[0], P.index("rust"))
        with self.assertRaises(ValueError):
            Q.map_image(self.img(pix[:1], 1), {P.hex888(0): "swampwater"}, sprite=True)


def cell_alpha_max(im, sheet, r, c):
    cw, ch = sheet.cell
    return im.getchannel("A").crop((c * cw, r * ch, c * cw + cw, r * ch + ch)).getextrema()[1]


class SheetTest(unittest.TestCase):
    def test_metadata_matches_png(self):
        for s in S.ALL:
            with self.subTest(sheet=s.key):
                self.assertTrue(s.path.exists(), s.path)
                im = Q.sheet_image(s)
                cw, ch = s.cell
                self.assertEqual(im.width % cw, 0)
                self.assertEqual(im.height % ch, 0)
                cols, rows = im.width // cw, im.height // ch
                self.assertTrue(0 <= s.pivot[0] < cw and 0 <= s.pivot[1] < ch)
                if s.kind == "tiles":
                    continue
                self.assertTrue(s.anims)
                used = set()
                for a in s.anims:
                    self.assertNotIn(a.row, used, "%s: two animations on row %d" % (s.key, a.row))
                    used.add(a.row)
                    self.assertLess(a.row, rows)
                    self.assertLessEqual(a.col + a.frames, cols)
                    self.assertTrue(0 <= a.blank < a.frames)
                    self.assertGreater(a.ms, 0)
                    self.assertTrue(a.role)
                    for f in range(cols):
                        c = a.col + f if f < a.frames else f
                        top = cell_alpha_max(im, s, a.row, c)
                        if f < a.drawn:
                            # drawn frames: opaque, or (alpha frames) at least visible
                            self.assertGreaterEqual(top, 1 if a.alpha_of else Q.ALPHA_CUT,
                                                    "%s %s f%d is empty" % (s.key, a.name, f))
                        elif f < a.frames or c >= a.col + a.frames:
                            self.assertEqual(top, 0, "%s %s: cell %d should be empty" % (s.key, a.name, c))
                for r in range(rows):
                    if r not in used:
                        for c in range(cols):
                            self.assertEqual(cell_alpha_max(im, s, r, c), 0,
                                             "%s: row %d has art but no animation" % (s.key, r))

    def test_user_confirmed_rows(self):
        def frames(s):
            return [(a.row, a.frames) for a in s.anims]
        self.assertEqual(frames(S.SQUIRE), [(0, 4), (1, 8), (2, 7), (3, 6), (4, 4), (5, 5)])
        self.assertEqual(frames(S.RACCOON), [(0, 8), (1, 8), (2, 4), (3, 4)])
        self.assertEqual(frames(S.TURTLE), [(0, 12), (1, 10), (2, 4), (3, 6), (4, 5), (5, 10)])

    def test_json_sheets(self):
        import json
        for s in S.ALL:
            if not s.json_path:
                continue
            with self.subTest(sheet=s.key):
                with open(s.json_path, encoding="utf-8") as f:
                    n = len(json.load(f)["frames"])
                self.assertEqual(sum(a.frames for a in s.anims), n)
                self.assertTrue(all(a.ms == 100 for a in s.anims))
        names = [a.name for a in S.BEAVER.anims]
        self.assertEqual(names, ["Idle", "Movement", "Movement with Stick", "Bite", "Damage", "Death",
                                 "Idle Water", "Movement Water", "Movement Water with Stick", "Dive",
                                 "Ascent"])
        self.assertEqual([a.frames for a in S.CHAMELEON.anims], [8] * 7)

    def test_alpha_frames(self):
        vanish = [a for a in S.CHAMELEON.anims if a.alpha_of]
        self.assertEqual([a.name for a in vanish], ["Disappear", "Reappear"])
        self.assertTrue(all(a.alpha_of == "idle" for a in vanish))
        for s in S.ALL:
            if s is not S.CHAMELEON:
                self.assertFalse([a for a in s.anims if a.alpha_of])
            self.assertFalse([ln for ln in Q.alpha_report(s) if ln.startswith("WARNING")],
                             "%s: partly transparent pixels outside alpha frames" % s.key)

    def test_overrides_reference_real_colours(self):
        for s in S.ALL:
            with self.subTest(sheet=s.key):
                present = {Q.rgb_hex(c) for c in Q.sheet_colours(s)}
                for hx, target in s.overrides.items():
                    self.assertEqual(hx, hx.lower())
                    self.assertIn(hx, present, "%s: override %s is not a colour of the sheet" % (s.key, hx))
                    i = P.index(target)
                    self.assertTrue(0 <= i < 16)
                    if s.kind == "sprite":
                        self.assertNotEqual(i, P.KEY, "%s: %s -> the sprite key" % (s.key, hx))
                for label, hexes, need in s.ramps:
                    for hx in hexes:
                        self.assertIn(hx, present, "%s ramp %s: %s not in the sheet" % (s.key, label, hx))

    def test_ramps_keep_their_steps(self):
        for s in S.ALL:
            cmap = Q.sheet_map(s)
            for label, got, distinct, need, ok in Q.ramp_checks(s, cmap):
                self.assertTrue(ok, "%s %s: %s" % (s.key, label, got))


class QuantiseSheetsTest(unittest.TestCase):
    def test_no_opaque_sprite_pixel_is_the_key(self):
        for s in S.SPRITE_SHEETS:
            with self.subTest(sheet=s.key):
                im = Q.sheet_image(s)
                idx = Q.quantise_sheet(s)
                alpha = im.getchannel("A").tobytes()
                skip = Q.alpha_cells(s)
                cw, ch = s.cell
                w = im.width
                bad = opaque = 0
                for n, a in enumerate(alpha):
                    y, x = divmod(n, w)
                    if (y // ch, x // cw) in skip:
                        self.assertEqual(idx[n], P.KEY)     # dithered at runtime, not stored
                    elif a >= Q.ALPHA_CUT:
                        opaque += 1
                        bad += idx[n] == P.KEY
                    else:
                        self.assertEqual(idx[n], P.KEY)
                self.assertGreater(opaque, 0)
                self.assertEqual(bad, 0)

    def test_every_colour_has_a_home(self):
        for s in S.ALL:
            for rgb, (i, de, how, n) in Q.sheet_map(s).items():
                self.assertTrue(0 <= i < 16)
                if s.kind == "sprite":
                    self.assertNotEqual(i, P.KEY)

    def test_tileset_water_is_index_15(self):
        cmap = Q.sheet_map(S.TILESET)
        self.assertEqual(cmap[Q.hex_rgb("5483af")][0], P.WATER)
        used = Counter(v[0] for v in cmap.values())
        self.assertIn(P.WATER, used)


if __name__ == "__main__":
    unittest.main()
