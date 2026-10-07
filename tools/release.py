#!/usr/bin/env python3
"""A release of Ethan's Critters on GitHub Releases.

The release files are too big for the repository's history (the cart is
~14 MB, almost all of it the card's world), so they are never committed:
they are built into out/release/ and attached to a GitHub release instead.

    python tools/release.py              build and check everything, write the notes; publish nothing
    python tools/release.py --publish    then tag v<version>, push the tag and create the release

What it does:
  1. checks: the working tree is clean (the release is a commit), the tag
     v<ECRT_VERSION> (config.h) does not exist yet; with --publish also that
     the repository has an `origin` remote and gh is logged in
  2. EthansCritters/tools/export_cart.py: mkcard, the release build, the
     cover, the cart, the spec's checks
  3. out/release/:
       EthansCritters-<v>.chgame         the cart, for CHGame's tools and menu
       EthansCritters-<v>-sdcard.zip     the same laid out as a card (chgame cart
                                         prepare): unzip onto a FAT32 card's root
       release-notes.md                  what the release page says
  4. --publish: an annotated tag v<v> on HEAD, pushed to origin, and
     `gh release create` with the notes and both files

Bump ECRT_VERSION in EthansCritters/config.h and commit before each
release. Needs the CHGame checkout (../CHGame or $CHGAME_ROOT) for the cart
tools, and gh (https://cli.github.com) for --publish.
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKETCH = ROOT / "EthansCritters"
OUT = ROOT / "out" / "release"
CHGAME = pathlib.Path(os.environ.get("CHGAME_ROOT", ROOT.parent / "CHGame"))
CHGAME_PY = CHGAME / "tools" / "chgame.py"


def sh(argv, capture=False, check=True):
    r = subprocess.run([str(a) for a in argv], cwd=str(ROOT), capture_output=capture, text=True)
    if check and r.returncode:
        raise SystemExit(f"failed: {' '.join(str(a) for a in argv)}\n{(r.stdout or '') + (r.stderr or '')}".strip())
    return r


def version():
    m = re.search(r'#define\s+ECRT_VERSION\s+"([^"]+)"', (SKETCH / "config.h").read_text(encoding="utf-8"))
    if not m:
        raise SystemExit("no ECRT_VERSION in EthansCritters/config.h")
    return m.group(1)


def notes(v, cart, card_zip):
    img = (SKETCH / "build" / "release" / "EthansCritters.ino.bin").stat().st_size
    return f"""# Ethan's Critters {v}

A squire, one big swamp, six critter nests and a giant toad, for the CHGame
handheld. Every pixel of the swamp is streamed off the SD card while you play.

## Download

| File | What it is |
|---|---|
| `{cart.name}` ({cart.stat().st_size / 1e6:.1f} MB) | The cart: the game for CHGame's SD menu and tools. `chgame cart deploy {cart.name} --card E:\\` lays out a card |
| `{card_zip.name}` ({card_zip.stat().st_size / 1e6:.1f} MB) | The same, ready to copy: unzip it onto the root of a FAT32 microSD card |

## Playing it

1. Put the files on the card (either way above). The game's data, `CRITTERS.DAT`
   (44 MB), goes in the card's root, and the menu's files in `GAMES/`.
2. Switch the CHGame on: the SD menu lists ETHAN'S CRITTERS. Pick it and it installs
   ({img:,} B program).
3. The first start with a new card reads the whole data file once (STIRRING THE SWAMP,
   about 20 s); later starts go straight to the title.

D-pad walks, A thrusts, B guards (tap it just before a blow to parry), START pauses on
the map. Clear the six nests, then go through the brambles into The Rot.

## Credits

Game by Kevin Bates (bateske), written with Claude, under the MIT licence (the code; not the
art). Sprites and tileset by Elthen
([elthen.itch.io](https://elthen.itch.io/)), bought as sprite packs; the game's name is
a misreading of theirs. Built on CHGame, CHGfx and CHSd (Apache-2.0 and MIT).
"""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--publish", action="store_true", help="tag, push the tag and create the GitHub release")
    ap.add_argument("--allow-dirty", action="store_true", help="build from an uncommitted tree (never with --publish)")
    a = ap.parse_args(argv)
    v = version()
    tag = f"v{v}"
    dirty = sh(["git", "status", "--porcelain"], capture=True).stdout.strip()
    if dirty and (a.publish or not a.allow_dirty):
        raise SystemExit("the working tree has changes: commit them first (a release is a commit)")
    if sh(["git", "rev-parse", "-q", "--verify", f"refs/tags/{tag}"], capture=True, check=False).returncode == 0:
        raise SystemExit(f"tag {tag} exists: bump ECRT_VERSION in EthansCritters/config.h")
    if a.publish:
        if not sh(["git", "remote", "get-url", "origin"], capture=True, check=False).stdout.strip():
            raise SystemExit("no `origin` remote: create the GitHub repository and push main first "
                             "(gh repo create ... --source . --push)")
        if sh(["gh", "auth", "status"], capture=True, check=False).returncode:
            raise SystemExit("gh is not logged in: gh auth login")
    if not CHGAME_PY.exists():
        raise SystemExit(f"no CHGame checkout at {CHGAME} (set CHGAME_ROOT)")

    OUT.mkdir(parents=True, exist_ok=True)
    cart = OUT / f"EthansCritters-{v}.chgame"
    sh([sys.executable, SKETCH / "tools" / "export_cart.py", "--out", cart])
    card = OUT / "sdcard"
    shutil.rmtree(card, ignore_errors=True)
    sh([sys.executable, CHGAME_PY, "cart", "prepare", cart, card])
    card_zip = OUT / f"EthansCritters-{v}-sdcard.zip"
    with zipfile.ZipFile(card_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(card.rglob("*")):
            if f.is_file():
                z.write(f, f.relative_to(card).as_posix())
    shutil.rmtree(card)
    note_file = OUT / "release-notes.md"
    note_file.write_text(notes(v, cart, card_zip), encoding="utf-8", newline="\n")
    print(f"\n{tag}: {cart.name} {cart.stat().st_size:,} B, {card_zip.name} {card_zip.stat().st_size:,} B, "
          f"notes in {note_file}")
    if not a.publish:
        print("not published (--publish tags, pushes the tag and creates the release)")
        return
    sh(["git", "tag", "-a", tag, "-m", f"Ethan's Critters {v}"])
    sh(["git", "push", "origin", tag])
    sh(["gh", "release", "create", tag, "--title", f"Ethan's Critters {v}", "--notes-file", note_file,
        cart, card_zip])
    print(f"published {tag}")


if __name__ == "__main__":
    main()
