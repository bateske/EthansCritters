#!/usr/bin/env python3
"""Ethan's Critters: tests for the world tools (tools/world/).

    python EthansCritters/tools/tests/test_world.py

The card layout (worldpack.py) round trip: for random cameras and all 8 x
phases, the view unpacked the way the game builds it (each column block
shifted into framebuffer words) equals the view read pixel by pixel from
the blocks and the bitmap's crop, on a small random world (also with 4
ambient phases and 2 layers, each its own picture) and on the real one at
every ambient phase and layer; the header, the addresses, the padding. The
painter: deterministic, every spot the game uses on open ground, the
dual-grid pieces it is built from, WorldData.{h,cpp} and the card's WRLD
section made from the same paint, the terrain's runs; the ambient phases
(M8): every phase moves the swamp but no willow, the healed layer is the
swamp's outside The Rot and different inside it, the gnats only over The
Rot. The occluders (occlpack.py): the section and the table round trip
(every layer), each tree kept shows exactly the world's pixels in every
phase of its layer and no pixel is two trees', how many show in a view. (tools/tests/test_world.cpp
runs the game's own World.cpp and Terrain.cpp on the card, test_occl.cpp
its Occluders.cpp.)
"""
import random
import re
import struct
import sys
import unittest
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
ROOT = GAME.parent
sys.path.insert(0, str(GAME / "tools" / "world"))
sys.path.insert(0, str(GAME / "tools" / "card"))
import worldpack as WP  # noqa: E402

VW, VH = WP.VIEW_W, WP.VIEW_H


def crop(idx, w, cx, cy):
    out = bytearray(VW * VH)
    for y in range(VH):
        out[y * VW:(y + 1) * VW] = idx[(cy + y) * w + cx:(cy + y) * w + cx + VW]
    return out


class PackTest(unittest.TestCase):
    def setUp(self):
        r = random.Random(7)
        self.geo = WP.Geometry(256, 120 + 8 * 9)                 # 32 columns, 10 bands
        self.idx = bytes(r.randrange(16) for _ in range(self.geo.w * self.geo.h))
        self.data = WP.pack([[self.idx]], self.geo)

    def test_geometry(self):
        g = self.geo
        self.assertEqual((g.nc, g.nb, g.padded_h), (32, 10, 8 * 9 + 128))
        self.assertEqual(len(self.data), g.blocks * WP.BLOCK)
        self.assertEqual(g.block(0, 0), 1)
        self.assertEqual(g.block(2, 5), 1 + 2 * 32 + 5)
        real = WP.Geometry(1024, 768)
        self.assertEqual((real.nc, real.nb, real.padded_h, real.blocks), (128, 82, 776, 1 + 82 * 128))
        phased = WP.Geometry(1024, 768, tph=4, layers=2)
        self.assertEqual(phased.block(3, 7, layer=1, phase=2), 1 + ((3 * 2 + 1) * 4 + 2) * 128 + 7)
        with self.assertRaises(ValueError):
            WP.Geometry(1020, 768)
        with self.assertRaises(ValueError):
            WP.Geometry(1024, 770)

    def test_header(self):
        g = WP.parse_header(self.data)
        self.assertEqual((g.w, g.h, g.nc, g.nb, g.tph, g.layers), (256, 192, 32, 10, 1, 1))
        self.assertEqual(self.data[:4], b"WRLD")
        self.assertEqual(self.data[24:512], bytes(488))

    def test_blocks(self):
        # a block is 128 rows of 4 bytes, gfx_fb's packing: pixel i of the 8 in bits 4i..4i+3
        g, idx = self.geo, self.idx
        for b, k in ((0, 0), (3, 7), (9, 31)):
            blk = self.data[g.block(b, k) * 512:(g.block(b, k) + 1) * 512]
            for r in (0, 1, 77, 127):
                y = min(8 * b + r, g.h - 1)                     # the padding repeats the last row
                word = struct.unpack_from("<I", blk, 4 * r)[0]
                for i in range(8):
                    self.assertEqual((word >> (4 * i)) & 15, idx[y * g.w + 8 * k + i])

    def test_windows(self):
        g, r = self.geo, random.Random(3)
        cams = [(0, 0), (g.w - VW, g.h - VH), (0, g.h - VH), (g.w - VW, 0)]
        cams += [(8 * r.randrange(16) + p, r.randrange(g.h - VH + 1)) for p in range(8) for _ in range(3)]
        cams += [(g.w - VW - p, p) for p in range(8)]
        for cx, cy in cams:
            want = crop(self.idx, g.w, cx, cy)
            b, r0, k0, n = g.window(cx, cy)
            self.assertEqual(n, 16 if cx % 8 == 0 else 17)
            self.assertLessEqual(k0 + n, g.nc)
            self.assertEqual(WP.window_reference(self.data, g, cx, cy), want, (cx, cy))
            self.assertEqual(WP.window_game(self.data, g, cx, cy), want, (cx, cy))
        with self.assertRaises(ValueError):
            g.window(g.w - VW + 1, 0)

    def test_pixel(self):
        g = self.geo
        for x, y in ((0, 0), (255, 191), (100, 150), (7, 183)):
            self.assertEqual(WP.pixel(self.data, g, x, y), self.idx[y * g.w + x])

    def test_phases_and_layers(self):
        # every (layer, phase) its own picture, each window read from its own blocks
        r = random.Random(17)
        g = WP.Geometry(256, 120 + 8 * 5, tph=4, layers=2)
        imgs = [[bytes(r.randrange(16) for _ in range(g.w * g.h)) for _ in range(4)] for _ in range(2)]
        data = WP.pack(imgs, g)
        self.assertEqual(len(data), (1 + g.nb * 2 * 4 * g.nc) * WP.BLOCK)
        h = WP.parse_header(data)
        self.assertEqual((h.tph, h.layers), (4, 2))
        for layer in range(2):
            for phase in range(4):
                for cx, cy in ((0, 0), (g.w - VW, g.h - VH), (13, 21), (64, 40), (100 + phase, 3 * layer)):
                    want = crop(imgs[layer][phase], g.w, cx, cy)
                    self.assertEqual(WP.window_reference(data, g, cx, cy, layer, phase), want, (cx, cy, layer, phase))
                    self.assertEqual(WP.window_game(data, g, cx, cy, layer, phase), want, (cx, cy, layer, phase))
                self.assertEqual(WP.pixel(data, g, 77, 150, layer, phase), imgs[layer][phase][150 * g.w + 77])
        # the address the game reads: band 2, layer 1, phase 3, column 5
        self.assertEqual(g.block(2, 5, 1, 3), 1 + ((2 * 2 + 1) * 4 + 3) * g.nc + 5)

    def test_refuses(self):
        g = self.geo
        with self.assertRaises(ValueError):
            WP.pack([[self.idx[:-1]]], g)
        with self.assertRaises(ValueError):
            WP.pack([[bytes([16]) * len(self.idx)]], g)
        with self.assertRaises(ValueError):
            WP.pack([[self.idx], [self.idx]], g)


class WorldTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import paint
        cls.P = paint
        cls.w = paint.build()

    def test_real_windows(self):
        P, w = self.P, self.w
        data = w.section
        g = WP.parse_header(data)
        self.assertEqual((g.w, g.h), (P.W, P.H))
        r = random.Random(11)
        self.assertEqual((g.tph, g.layers), (P.TPH, P.LAYERS))
        cams = [(0, 0), (P.W - VW, P.H - VH)] + [(8 * r.randrange(112) + p, r.randrange(P.H - VH + 1))
                                                 for p in range(8)]
        for i, (cx, cy) in enumerate(cams):
            for layer in range(P.LAYERS):
                for phase in range(P.TPH):
                    if i > 2 and (i + phase + layer) % 3:
                        continue                # (a third of them past the corners: Python's reads are slow)
                    want = crop(w.images[layer][phase], P.W, cx, cy)
                    self.assertEqual(WP.window_game(data, g, cx, cy, layer, phase), want, (cx, cy, layer, phase))

    def test_bitmap(self):
        P, w = self.P, self.w
        self.assertEqual(len(w.idx), P.W * P.HP)
        self.assertLessEqual(max(w.idx), 15)
        counts = [w.idx.count(bytes([c])) for c in range(16)]
        self.assertGreater(min(counts[c] for c in (0, 1, 2, 3, 6, 7, 8, 15)), 1000)   # earth, moss, water

    def test_deterministic(self):
        P = self.P
        again = P.Painter(P.load_map(), P.load_objects()).paint()
        self.assertEqual(again.cv.tobytes(), self.w.idx)
        self.assertEqual(bytes(again.terr), self.w.terrain)
        again = P.Painter(P.load_map(), P.load_objects(), phase=2, healed=True).paint()
        self.assertEqual(again.cv.tobytes(), self.w.images[1][2])
        self.assertEqual(bytes(again.terr), self.w.terrain)

    def test_ambient(self):
        """The phases move what they should and nothing else: every phase
        differs from the next (reeds, ripples, the swell, lilies, gnats)
        but no willow's pixel does, and the healed layer is the swamp's
        outside The Rot (and its fringe), not inside it."""
        from PIL import Image, ImageChops
        P, w = self.P, self.w
        self.assertEqual(len(w.images), P.LAYERS)
        x0, y0, x1, y1 = next(r[1:] for r in w.obj["regions"] if r[0] == "THE ROT")
        img = lambda layer, phase: Image.frombytes("L", (P.W, P.HP), w.images[layer][phase])
        diff = lambda a, b: ImageChops.difference(a, b).point(lambda v: 255 if v else 0)
        own = set()
        for t in w.occl:
            own.update(t.owned())
        for layer in range(P.LAYERS):
            self.assertEqual(len(w.images[layer]), P.TPH)
            for phase in range(P.TPH):
                d = diff(img(layer, phase), img(layer, (phase + 1) % P.TPH))
                n = sum(1 for v in d.tobytes() if v)
                self.assertGreater(n, 10000, (layer, phase))      # the whole swamp moves
                self.assertLess(n, P.W * P.H // 10, (layer, phase))
                dp = d.load()
                self.assertFalse([p for p in own if dp[p]], "a willow moves")
        for phase in range(P.TPH):
            d = diff(img(0, phase), img(1, phase))
            bb = d.getbbox()
            self.assertIsNotNone(bb)
            self.assertTrue(x0 - 40 <= bb[0] and y0 - 40 <= bb[1] and bb[2] <= x1 + 40 and bb[3] <= y1 + 40,
                            "the healed layer differs outside The Rot: %r" % (bb,))
            self.assertGreater(sum(1 for v in d.crop((x0, y0, x1, y1)).tobytes() if v), 20000)
        # the gnats: only over The Rot, and not when healed
        for healed in (False, True):
            p = P.Painter(P.load_map(), P.load_objects(), healed=healed)
            for step in (p.ground, p.grass, p.water, p.banks, p.worn, p.place_props, p.draw_props, p.rot,
                         p.gnats):
                step()
            if healed:
                self.assertEqual(p.swarms, [])
            else:
                self.assertGreaterEqual(len(p.swarms), 6)
                self.assertTrue(all(x0 <= x < x1 and y0 <= y < y1 for x, y in p.swarms))

    def test_spots(self):
        P, w = self.P, self.w
        self.assertEqual(P.validate(w), [])
        self.assertEqual(w.repairs, [], "map.txt has pinches the painter had to fill")
        self.assertEqual(len(w.terrain), P.TW * P.TH)
        sx, sy = w.obj["player_start"]
        self.assertEqual(w.at(sx, sy), P.OPEN)
        gx0, gy0, gx1, gy1 = w.obj["gate"]
        for y in range(gy0, gy1, 8):
            for x in range(gx0, gx1, 8):
                self.assertNotEqual(w.at(x, y), P.SOLID)
        kinds = sorted(k for k, *_ in w.obj["nests"])
        self.assertEqual(kinds, ["beaver", "beaver", "chameleon", "raccoon", "raccoon", "turtle"])
        whites = [m for m in w.obj["mushrooms"] if m[0][0] == "B"]
        self.assertTrue(8 <= len(whites) <= 20)

    def test_ground_pieces(self):
        import tileset as TS
        ts = TS.Tileset()
        for case in range(1, 16):
            if case in TS.PINCHES:
                self.assertNotIn(case, ts.ground)
                continue
            self.assertIn(case, ts.ground)
            for p in ts.ground[case]:
                # each corner's 3x3 is grass exactly when the case says so
                m = p.mask.load()
                for bit, (x0, y0) in ((TS.TL, (0, 0)), (TS.TR, (13, 0)), (TS.BL, (0, 13)), (TS.BR, (13, 13))):
                    on = sum(m[x, y] > 0 for y in range(y0, y0 + 3) for x in range(x0, x0 + 3))
                    self.assertEqual(on >= 5, bool(case & bit), (p.name, case, bit))

    def test_sources_and_card(self):
        P, w = self.P, self.w
        hdr, src = P.data_sources(w)
        self.assertEqual((P.SRC_ASSETS / "WorldData.h").read_text(encoding="utf-8"), hdr)
        self.assertEqual((P.SRC_ASSETS / "WorldData.cpp").read_text(encoding="utf-8"), src)
        consts = dict((k, int(v)) for k, v in re.findall(r"(\w+) = (\d+)", hdr))
        g = WP.Geometry(P.W, P.H, P.TPH, P.LAYERS)
        self.assertEqual((consts["WORLD_W"], consts["WORLD_H"], consts["WORLD_NC"], consts["WORLD_NB"]),
                         (g.w, g.h, g.nc, g.nb))
        self.assertEqual((consts["WORLD_TPH"], consts["WORLD_LAYERS"]), (P.TPH, P.LAYERS))
        self.assertEqual(consts["OCCL_LAYER_BLOCKS"], w.occl_pack[1][5])
        self.assertEqual((consts["TERRAIN_W"], consts["TERRAIN_H"]), (P.TW, P.TH))
        # the terrain runs as the game walks them, and the flat table the host tests check them against
        body = src.split("TERRAIN_RUNS[TERRAIN_RUN_COUNT] = {")[1].split("};")[0]
        runs = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", body))
        body = src.split("TERRAIN_ROW[TERRAIN_H] = {")[1].split("};")[0]
        starts = [int(v) for v in re.findall(r"\d+", body)]
        self.assertEqual(len(runs), consts["TERRAIN_RUN_COUNT"])
        self.assertEqual(len(starts), P.TH)
        self.assertEqual(P.terrain_unruns(runs, starts + [len(runs)]), w.terrain)
        self.assertLess(len(runs) + 2 * len(starts), 1600)         # flash: was 3,072 B flat
        body = src.split("TERRAIN_FLAT[TERRAIN_W / 4 * TERRAIN_H] = {")[1].split("};")[0]
        packed = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", body)]
        self.assertEqual(len(packed), P.TW * P.TH // 4)
        for i in range(P.TW * P.TH):
            self.assertEqual((packed[i >> 2] >> (2 * (i & 3))) & 3, w.terrain[i])
        # the card in out/ carries this paint (mkcard ran before the tests)
        card = ROOT / "out" / "card" / "CRITTERS.DAT"
        if card.exists():
            import cardfile
            c = cardfile.read(card)
            self.assertEqual(c.section("WRLD").data, w.section)

    def test_terrain_runs(self):
        P = self.P
        r = random.Random(5)
        for t in (bytes(P.TW * P.TH), bytes([3]) * (P.TW * P.TH),
                  bytes(r.randrange(4) for _ in range(P.TW * P.TH)), self.w.terrain):
            runs, starts = P.terrain_runs(t)
            self.assertEqual(P.terrain_unruns(runs, starts), t)
            self.assertTrue(all((b >> 2) + 1 <= P.RUN_MAX for b in runs))


class OcclTest(unittest.TestCase):
    """The occluders (occlpack.py): the section, the table, the trees kept."""
    @classmethod
    def setUpClass(cls):
        import paint
        import occlpack
        cls.P, cls.OC = paint, occlpack
        cls.w = paint.build()

    def test_synthetic(self):
        OC = self.OC
        r = random.Random(9)
        trees = []
        for k in range(40):
            w, h = r.choice(((50, 63), (70, 86), (46, 54)))
            px = bytes(r.choice((15, 15, 3, 8)) for _ in range(w * h))
            x, y = r.randrange(1024), r.randrange(768)
            trees.append(OC.Tree("T%d" % (w // 10), False, x, y, x - w // 2, y - h + 1, w, h, w // 2, h - 1, px))
        data, lay = OC.pack(trees, 768)
        order, shapes, sidx, firsts, bands, nb = lay
        self.assertEqual(len(data), (1 + nb) * 512)
        self.assertEqual(data[:4], b"OCCL")
        self.assertEqual([t.y for t in order], sorted(t.y for t in trees))
        for i, t in enumerate(order):
            for y in (0, t.h // 2, t.h - 1):
                for x in (0, t.w // 3, t.w - 1):
                    self.assertEqual(OC.tree_pixel(data, lay, i, x, y), t.pixels[y * t.w + x])
            e = OC.entry(t, sidx[i])
            v = e[0] | e[1] << 8 | e[2] << 16
            self.assertEqual((v & 1023, (v >> 10) & 63, (v >> 16) & 15, v >> 20),
                             (t.x, t.y & 63, sidx[i], OC.shown(t)))
        for b, (first, blk) in enumerate(bands[:-1]):
            self.assertTrue(all(t.y >= b << OC.BAND_SHIFT for t in order[first:]))
            self.assertTrue(all(t.y < b << OC.BAND_SHIFT for t in order[:first]))
            self.assertEqual(blk, firsts[first] if first < len(order) else nb)
        for key, flip, ww, h, ax, ay, rpb, blocks in shapes:
            self.assertEqual((rpb, blocks), (512 // (4 * ww), (h + rpb - 1) // rpb))

    def test_world_trees(self):
        P, OC, w = self.P, self.OC, self.w
        self.assertGreater(w.trees, len(w.occl))         # the forest's inner trees are left out
        self.assertGreater(len(w.occl), 100)
        self.assertEqual(len(w.occl_layers), P.LAYERS)
        own = {}
        for layer, trees in enumerate(w.occl_layers):
            self.assertEqual([(t.key, t.x, t.y) for t in trees], [(t.key, t.x, t.y) for t in w.occl])
            imgs = w.images[layer]
            for t, t0 in zip(trees, w.occl):
                self.assertEqual(bytes(v == 15 for v in t.pixels), bytes(v == 15 for v in t0.pixels))
                n = 0
                for j in range(t.h):
                    for i in range(t.w):
                        v = t.pixels[j * t.w + i]
                        if v == 15:
                            continue
                        o = (t.y0 + j) * P.W + t.x0 + i
                        for idx in imgs:                            # what the world shows, in every phase
                            self.assertEqual(v, idx[o], (layer, t.key, t.x, t.y, i, j))
                        if not layer and t0.own[j * t.w + i]:
                            self.assertNotIn(o, own)                # no pixel its own twice
                            own[o] = t
                        n += 1
                self.assertFalse([i for i, v in enumerate(t0.own) if v and t.pixels[i] == 15])   # its own all stored
                self.assertGreaterEqual(n, OC.MIN_PIXELS)
        # The Rot's willows: dead in layer 0, alive in the healed layer
        self.assertGreater(sum(1 for a, b in zip(w.occl_layers[0], w.occl_layers[-1]) if a.pixels != b.pixels), 5)
        data, lay = w.occl_pack
        self.assertEqual(len(data), (1 + P.LAYERS * lay[5]) * 512)
        self.assertEqual(struct.unpack_from("<4sHHHH", data, 0), (b"OCCL", len(w.occl), len(lay[1]), lay[5], P.LAYERS))
        r = random.Random(4)
        for layer, trees in enumerate(w.occl_layers):
            at = {(t.key, t.x, t.y): t for t in trees}
            for _ in range(400):
                i = r.randrange(len(lay[0]))
                t = lay[0][i]
                x, y = r.randrange(t.w), r.randrange(t.h)
                self.assertEqual(OC.tree_pixel(data, lay, i, x, y, layer), at[(t.key, t.x, t.y)].pixels[y * t.w + x])
        hdr, src = P.data_sources(w)
        body = src.split("OCCLUDERS[OCCL_COUNT * 3] = {")[1].split("};")[0]
        ent = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", body))
        order, shapes, sidx, firsts, bands, nb = lay
        self.assertEqual(ent, b"".join(OC.entry(t, s) for t, s in zip(order, sidx)))
        card = ROOT / "out" / "card" / "CRITTERS.DAT"
        if card.exists():
            import cardfile
            self.assertEqual(cardfile.read(card).section("OCCL").data, data)

    def test_mushrooms_clear(self):
        """No mushroom stands behind a canopy: a static item gets no pass
        (Occluders.h), so one under a willow it is behind would show over
        the canopy. Checked against what the kept trees store (their own
        pixels and the willows' in front of them), over the mushroom's
        pixels' box (the white one's frames: 6 px left of its foot, 5 right,
        11 up, and it bobs up a pixel)."""
        w = self.w
        bad = []
        for i, (k, x, y) in enumerate(w.obj["mushrooms"]):
            for t in w.occl:
                x0, y0, x1, y1 = x - 6, y - 12, x + 6, y + 1
                if t.y <= y or x1 <= t.x0 or x0 >= t.x0 + t.w or y1 <= t.y0 or y0 >= t.y0 + t.h:
                    continue
                n = sum(1 for j in range(max(0, y0 - t.y0), min(t.h, y1 - t.y0))
                        for c in range(max(0, x0 - t.x0), min(t.w, x1 - t.x0)) if t.pixels[j * t.w + c] != 15)
                if n:
                    bad.append((i, k, x, y, t.key, t.x, t.y, n))
        self.assertFalse(bad, "mushrooms behind a canopy (objects.json)")

    def test_density(self):
        """How many trees an actor can get behind show in a view (printed;
        the plan wanted at most two a view outside Willow Hollow, which the
        seeded forests' edges do not keep: Occluders.h passes two at most)."""
        P, OC, w = self.P, self.OC, self.w
        wh = [r for r in w.obj["regions"] if r[0] == "WILLOW HOLLOW"]
        inside = lambda x, y: any(x0 <= x < x1 and y0 <= y < y1 for _, x0, y0, x1, y1 in wh)
        a, b = OC.view_counts(w.occl, P.W, P.H, inside, step=32)
        for name, L in (("Willow Hollow", a), ("elsewhere", b)):
            print("\n  occluders a view, %s: average %.1f, at most 2 in %d%% of views, most %d"
                  % (name, sum(L) / len(L), 100 * sum(1 for v in L if v <= 2) // len(L), max(L)), end="")
        print()
        self.assertLess(sum(b) / len(b), sum(a) / len(a))


if __name__ == "__main__":
    unittest.main()
