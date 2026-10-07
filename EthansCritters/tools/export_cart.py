#!/usr/bin/env python3
"""Ethan's Critters as a .chgame: the distributable cart the CHGame's SD menu
installs from (CHGame's spec/chgame.md).

The cart holds the release image, CRITTERS.DAT as the game's SD card file
(the exporter puts it at the card's root, where the game reads it), the
cover (docs/cart.png, drawn by tools/cart.py), the licence, and what
chgame.json says (title, author, genre, buttons; the version from config.h,
the description from the README's first paragraph). No screenshots: CHGame's
spec keeps gameplay GIFs out of carts (the README's reel stays in the repo).

Steps, in order, each stopping the run if it fails:
  1. tools/mkcard.py          the card file and src/assets/CardIndex.h, from the current sources
  2. ec build                 the release image (build/release/EthansCritters.ino.bin)
  3. tools/cart.py            the cover, docs/cart.png (unless --no-art)
  4. chgame export --no-build the cart, out/EthansCritters.chgame (--out for another path)
  5. chgame cart picture      the cart's own cover (the visual menu's splash at power-on): the same art
  6. chgame cart verify       the spec's checks on the result

The exporter and the art toolkit are the CHGame repository's (tools/chcart,
tools/artkit): a checkout beside this project (../CHGame) or at
$CHGAME_ROOT. Nothing of them is vendored here.

    python EthansCritters/tools/export_cart.py [--out FILE] [--no-art] [--no-build]
"""
import argparse
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent          # EthansCritters/tools
SKETCH = HERE.parent
ROOT = SKETCH.parent
CHGAME = pathlib.Path(os.environ.get("CHGAME_ROOT", ROOT.parent / "CHGame"))
CHGAME_PY = CHGAME / "tools" / "chgame.py"
EC = ROOT / "chgame" / "tools" / "chgame.py"            # the vendored tools: ec build


def run(title, argv, cwd=ROOT):
    print(f"== {title}")
    r = subprocess.run([sys.executable] + [str(a) for a in argv], cwd=str(cwd))
    if r.returncode:
        raise SystemExit(f"{title} failed ({r.returncode})")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=str(ROOT / "out" / "EthansCritters.chgame"))
    ap.add_argument("--no-art", action="store_true", help="keep docs/cart.png as it is")
    ap.add_argument("--no-build", action="store_true", help="keep build/release and the card as they are")
    a = ap.parse_args(argv)
    if not CHGAME_PY.exists():
        raise SystemExit(f"no CHGame checkout at {CHGAME} (set CHGAME_ROOT): its tools/chcart exports the cart")
    if not a.no_build:
        run("the card file", [SKETCH / "tools" / "mkcard.py"])
        run("the release build", [EC, "--sketch", SKETCH, "build"])
    if not a.no_art:
        run("the cover", [SKETCH / "tools" / "cart.py"])
    out = pathlib.Path(a.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    run("the cart", [CHGAME_PY, "--sketch", SKETCH, "export", "--no-build", "--out", out])
    run("the cart's cover", [CHGAME_PY, "cart", "picture", out, SKETCH / "docs" / "cart.png"])
    run("the spec's checks", [CHGAME_PY, "cart", "verify", out])
    run("the cart's games", [CHGAME_PY, "cart", "info", out])
    print(f"{out}: {out.stat().st_size:,} B")


if __name__ == "__main__":
    main()
