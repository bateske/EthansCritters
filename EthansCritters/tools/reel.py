#!/usr/bin/env python3
"""Ethan's Critters: the README's reel (docs/gameplay.gif), cut like a trailer.

    python EthansCritters/tools/reel.py               shoot, then cut
    python EthansCritters/tools/reel.py --no-shoot    cut again from what out/reel holds
    python EthansCritters/tools/reel.py --sheets      also contact sheets of every take
    python EthansCritters/tools/reel.py --script S --sheets --no-cut    scout another script

Shooting runs tools/scripts/gameplay.txt in the simulator, as `ec run` does
(the same game: a take keeps a frame a tick whatever its `rec start EVERY`
says, and recording changes nothing the game does), and keeps every frame
of each `rec start` .. `rec stop NAME` take in out/reel/NAME.raw, the
framebuffer dumps as the game drew them. --sheets draws each take's frames
numbered by tick (out/reel/sheets/), to find a beat's in and out.

Cutting follows CUT below, a shot a line: the take, its in and out (ticks
into the take), the ticks between the reel's frames (3: 20 a second), and
how it hands over to the next shot:

    cut          a straight cut (also between two pieces of one take: seamless)
    dip N        the shot sinks into soot over its last N frames, dithered like
                 the game's own fades, and the next rises out of it over N
    mix N        the shot dissolves into the next over N frames, both playing
                 (a jump in time on one view: the map filling up)

The last shot's dip ends the reel in soot; the GIF loops to the title, whole
(its first frame is what shows while the GIF loads). `hold` keeps a shot's
last frame on (the end card only: the reel otherwise never stops on a still).

The budget: no more than 1 MB (CHGame's game-README rule), or the reel goes
to out/reel/gameplay.gif and docs/ is left alone. A frame where the camera
moves (a pan, a shake) changes the whole picture and costs ~10 KB; a frame
where only the actors move costs ~0.3 KB. So holding on the action is cheap
and moving the camera is not: in a run of whole-picture frames every
`thin`-th is kept (a shake still reads at 10 or 7 a second), and the takes
are staged with the camera still where they can be.
"""
import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
ROOT = GAME.parent
WORK = ROOT / "out" / "reel"
SCRIPT = GAME / "tools" / "scripts" / "gameplay.txt"
OUT = GAME / "docs" / "gameplay.gif"
LIMIT = 1_000_000
W = H = 128
SCALE = 2
DUMP = W * H // 2 + 32          # a framebuffer dump: 4 bpp pixels, then 16 RGB565 colours

BAYER4 = np.array(((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5)), np.int16)
SOOT = np.array((0x22, 0x22, 0x11), np.uint8)       # the game's soot (0x221) as the panel shows it
HEAVY = 0.35                    # a frame changing more of the screen than this costs a whole picture


@dataclass
class Shot:
    take: str           # out/reel/<take>.raw
    t0: int             # in: the first tick shown
    t1: int             # out: the tick after the last shown
    step: int = 3       # ticks a reel frame (3: 20 a second)
    then: str = "cut"   # the hand-over to the next shot: "cut", "dip N", "mix N"
    thin: int = 2       # in a run of frames that each change most of the screen (a shake, a pan),
                        # keep every thin-th: they cost a whole picture each, still ones next to nothing
    hold: int = 0       # ms the last frame stays (only the end card: a still, then its dip)


# The edit (see the header). Ticks are the take's own, from its `rec start`.
CUT = [
    Shot("title", 0, 270, then="dip 4"),
    Shot("den", 0, 150),
    Shot("den", 150, 232, thin=3),               # the den falls: the big shake
    Shot("beaver", 0, 204),
    Shot("chameleon", 0, 240, then="mix 6"),
    Shot("map", 50, 194),
    Shot("gate", 130, 200),
    Shot("toad", 0, 66),
    Shot("toad", 66, 170, thin=3),               # its croak: the ground shakes
    Shot("fight", 0, 100, then="mix 4"),
    Shot("fight", 150, 224),
    Shot("fight", 224, 430, thin=3),             # the blows' shakes, the death row, the fade
    Shot("fight", 477, 528),
    Shot("win", 0, 112, then="dip 10", hold=1100),
]


# --- shooting ---------------------------------------------------------------------------

def shoot(script, defines=()):
    """Runs the script in the simulator, keeping every frame of each take."""
    sys.path.insert(0, str(GAME / "tools" / "chsim"))
    import chdrive                                          # noqa: E402  (the game's driver)
    chdrive.default_card()
    from chdrivelib import SimTransport                     # noqa: E402
    from chsim import build                                 # noqa: E402

    class Camera(chdrive.CrittersDriver):
        def op(self, name, args, outdir):
            if name != "rec":
                return super().op(name, args, outdir)
            if args[0] == "start":
                self.rec, self.rec_count, self.rec_every = [], 0, 1
            else:
                takes, self.rec = self.rec, None
                (WORK / f"{args[1]}.raw").write_bytes(b"".join(takes))
                print(f"  {args[1]}: {len(takes)} ticks", flush=True)
            return True

    WORK.mkdir(parents=True, exist_ok=True)
    for old in WORK.glob("*.raw"):
        old.unlink()
    t = SimTransport(build(GAME, list(defines)))
    d = Camera(t, chdrive.IDENT)
    d.verbose = False
    try:
        d.run(str(script), str(WORK / "script"))
    finally:
        t.close()
    bugs = [b for b in t.bugs if b.startswith(("BUG", "=="))]
    if bugs:
        raise SystemExit(f"{len(bugs)} simulator bug report(s)")


def frames(take):
    """A take's frames, (n, H, W, 3) RGB as the 12 bpp panel shows them."""
    raw = (WORK / f"{take}.raw").read_bytes()
    n = len(raw) // DUMP
    a = np.frombuffer(raw, np.uint8)[:n * DUMP].reshape(n, DUMP)
    fb = a[:, :W * H // 2].reshape(n, H, W // 2)
    idx = np.empty((n, H, W), np.uint8)
    idx[:, :, 0::2] = fb & 15
    idx[:, :, 1::2] = fb >> 4
    pal = a[:, W * H // 2:].copy().view("<u2").astype(np.int32)        # (n, 16)
    rgb = np.stack(((pal >> 12) & 15, (pal >> 7) & 15, (pal >> 1) & 15), -1).astype(np.uint8) * 17
    return rgb[np.arange(n)[:, None, None], idx]


# --- cutting -----------------------------------------------------------------------------

def bayer(h=H, w=W):
    return np.tile(BAYER4, (h // 4, w // 4))


def sink(f, k):
    """Frame f sunk k sixteenths into soot, dithered (the game's own fades)."""
    out = f.copy()
    out[bayer() < k] = SOOT
    return out


def mix(a, b, k):
    """a giving way to b, k sixteenths of the way, dithered."""
    return np.where((bayer() < k)[:, :, None], b, a)


def assemble(cut, verbose=True):
    """The reel: (frame, ms) pairs at 1x, and each shot's place in it."""
    shots = []
    for s in cut:
        f = frames(s.take)
        if s.t1 > len(f):
            raise SystemExit(f"{s.take}: out {s.t1} past its {len(f)} ticks")
        pick = list(f[s.t0:s.t1:s.step])
        ms = 1000 * s.step / 60
        kept, run = [[pick[0], ms]], 0
        for p in pick[1:]:
            heavy = (p != kept[-1][0]).any(axis=2).mean() > HEAVY
            run = run + 1 if heavy else 0
            if heavy and run % s.thin != 1 and s.thin > 1:
                kept[-1][1] += ms            # shown a frame longer instead
            else:
                kept.append([p, ms])
        kept += [[kept[-1][0], ms] for _ in range(int(s.hold / ms))]
        shots.append(kept)
    for i, s in enumerate(cut):
        how = s.then.split()
        n = int(how[1]) if len(how) > 1 else 0
        a = shots[i]
        last = i + 1 == len(shots)
        # the last shot hands over to the first (the GIF loops), but only on its own side: the
        # reel's first frame is the title whole, as it shows while the GIF loads
        b = [None] * n if last else shots[i + 1]
        if how[0] == "dip":             # a sinks into soot, b rises out of it
            for k in range(n):
                a[len(a) - n + k][0] = sink(a[len(a) - n + k][0], 16 * (k + 1) // n)
                if not last:
                    b[k][0] = sink(b[k][0], 16 - 16 * (k + 1) // (n + 1))
        elif how[0] == "mix" and not last:      # a dissolves into b, both playing
            for k in range(n):
                a[len(a) - n + k][0] = mix(a[len(a) - n + k][0], b[k][0], 16 * (k + 1) // (n + 1))
            del b[:n]
        elif how[0] != "cut":
            raise SystemExit(f"{s.take}: no hand-over {s.then!r}")
    reel, acc, at = [], 0.0, []
    for s, kept in zip(cut, shots):
        at.append(sum(d for _, d in reel))
        for p, ms in kept:
            # GIF delays are whole centiseconds: carry the remainder so the reel keeps time
            acc += ms
            d = int(round(acc / 10.0)) * 10
            acc -= d
            reel.append((p, d))
        if verbose:
            print(f"  {s.take:14} {s.t0:4}-{s.t1:<4} {len(kept):4} frames {sum(m for _, m in kept) / 1000:5.2f} s"
                  f"  at {at[-1] / 1000:5.1f} s  then {s.then}")
    return reel, at


def encode(reel, path):
    """Writes the reel at SCALE: identical neighbours merged, each frame a
    palette of the colours it shows, Pillow's delta frames."""
    merged = []
    for f, d in reel:
        if merged and np.array_equal(merged[-1][0], f):
            merged[-1][1] += d
        else:
            merged.append([f, d])
    ims = [Image.fromarray(np.repeat(np.repeat(f, SCALE, 0), SCALE, 1)) for f, _ in merged]
    path.parent.mkdir(parents=True, exist_ok=True)
    ims[0].save(path, save_all=True, append_images=ims[1:], duration=[d for _, d in merged], loop=0)
    return len(merged), sum(d for _, d in merged) / 1000


def sheets(takes, every=6, cols=8):
    """Contact sheets at 2x: every `every`-th tick of each take, numbered by tick."""
    out = WORK / "sheets"
    out.mkdir(parents=True, exist_ok=True)
    tw, th = 2 * W + 4, 2 * H + 14
    for take in takes:
        f = frames(take)
        pick = list(range(0, len(f), every))
        rows = (len(pick) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * tw, rows * th), (40, 40, 40))
        dr = ImageDraw.Draw(sheet)
        for k, i in enumerate(pick):
            x, y = (k % cols) * tw, (k // cols) * th
            sheet.paste(Image.fromarray(np.repeat(np.repeat(f[i], 2, 0), 2, 1)), (x, y + 14))
            dr.text((x + 2, y + 1), str(i), fill=(255, 220, 0))
        sheet.save(out / f"{take}.png")
    print(f"sheets: {out}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--script", default=str(SCRIPT), help="the script to shoot (default gameplay.txt)")
    ap.add_argument("--no-shoot", action="store_true", help="cut from what out/reel holds")
    ap.add_argument("--no-cut", action="store_true", help="shoot only")
    ap.add_argument("--sheets", action="store_true", help="contact sheets of every take (out/reel/sheets)")
    ap.add_argument("--every", type=int, default=6, help="ticks between a sheet's frames")
    ap.add_argument("-D", dest="defines", action="append", default=[])
    a = ap.parse_args(argv)
    if not a.no_shoot:
        shoot(a.script, a.defines)
    if a.sheets:
        sheets(sorted(p.stem for p in WORK.glob("*.raw")), a.every)
    if a.no_cut or not CUT:
        return 0
    reel, _ = assemble(CUT)
    made = WORK / "gameplay.gif"
    n, secs = encode(reel, made)
    size = made.stat().st_size
    print(f"reel: {len(CUT)} shots, {n} frames, {secs:.1f} s, {size:,} B of {LIMIT:,}")
    if size > LIMIT:
        print(f"over the limit by {size - LIMIT:,} B: docs/gameplay.gif is unchanged; the reel is {made}")
        return 1
    OUT.parent.mkdir(parents=True, exist_ok=True)
    made.replace(OUT)
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
