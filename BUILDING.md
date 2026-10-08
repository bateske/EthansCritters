# Building Ethan's Critters

How to build the game, its card data and its releases from this repository,
and what is where. To just play it, take a release: see the
[README](README.md#getting-started).

## Prerequisites

- Python 3.13 with Pillow, pyserial and ziglang (the simulator's compiler is
  `python -m ziglang c++`);
- arduino-cli and the CHGame board package 0.3.0, staged from a CHGame
  checkout with its `tools/release/stage.py`;
- for the card data, the tests and the simulator's scripts: Elthen's sprite
  packs in `Sprites/` (below, [Without the sprite packs](#without-the-sprite-packs));
- for the cart and releases: a CHGame checkout beside this project
  (`../CHGame`) or named by `CHGAME_ROOT`.

## Commands

From the project root, use `./ec` in Git Bash or `ec.cmd` in cmd or
PowerShell:

| What | Command |
|---|---|
| The card file `out/card/CRITTERS.DAT` and `src/assets/CardIndex.h` | `python EthansCritters/tools/mkcard.py` |
| Release build, with its size | `./ec build` |
| Debug build | `./ec build --debug` |
| Upload the game | `./ec upload` |
| Put the card file on the board's SD card over USB | `python tools/card_sync.py` |
| Host tests | `./ec test` |
| Everything that needs no board (tests, every script twice, the build) | `./ec check` |
| A simulator script | `./ec run tools/scripts/<script>.txt out/<script>` |
| The README's GIF | `python EthansCritters/tools/reel.py` |
| The README's banner | `python EthansCritters/tools/card/banner.py` |
| The cart, `out/EthansCritters.chgame` | `python EthansCritters/tools/export_cart.py` |
| A release in `out/release/`; `--publish` tags `v<ECRT_VERSION>` and creates the GitHub release | `python tools/release.py [--publish]` |

## The card

`mkcard.py` is deterministic. The card's build hash is compiled into the
game, so a board running a build with the wrong card says WRONG CARD DATA.
Copy `CRITTERS.DAT` to the root of a FAT32 card; a freshly formatted card
keeps it in one piece, and the game accepts up to eight pieces.

The first boot with a new card file reads all of it once behind a bar
(STIRRING THE SWAMP, about 20 s): the board's card stalled for a second on
its first read of freshly written data, and the game would rather pay that
on the boot screen. It remembers the file's hash in its save, so later
boots go straight to the title.

`card_sync.py` does the copy without taking the card out of the board: it
uploads CHGame's CHSDtoUSB app so the board becomes a USB card reader,
copies and checks the file, ejects the drive, then uploads the game again.

## The cart and releases

The cart is the game as CHGame's SD menu takes it (`spec/chgame.md` in the
CHGame repository): a ZIP with the release image, `CRITTERS.DAT` as the
game's SD file, the box art (`EthansCritters/docs/cart.png`, painted by
`tools/cart.py` from the game's own sprites and tileset) and the licence.
`chgame cart deploy out/EthansCritters.chgame --card E:\` lays out a card:
`GAMES/ETHANSCR.CHG` for the menu, the file at the root for the game.

`tools/release.py` builds a release into the ignored `out/release/`: the
cart, the same laid out as a zip to unzip onto a FAT32 card, and the
release notes. Bump `ECRT_VERSION` in `EthansCritters/config.h` and commit
before each release.

## Without the sprite packs

The source art is not in the repository: Elthen's packs are sold under
Elthen's terms. A clone without it still builds the program, because the
generated sources in `EthansCritters/src/assets/` are committed: `./ec
build` and `./ec upload` work as they are. Take `CRITTERS.DAT` from the
release made from the same commit (its SD card zip on the Releases page).

Rebuilding the card data, the tests and the simulator's scripts need the
packs: put these files, as Elthen ships them, in `Sprites/` (the tools stop
with this list if any is missing):

- `Squire Sprite Sheet.png`
- `Raccoon Sprite Sheet.png`
- `Turtle Sprite Sheet.png`
- `Beaver Sprite Sheet.png` and `.json`
- `Chameleon Sprite Sheet.png` and `.json`
- `Giant Toad Sprite Sheet - Green.png`, `Giant Toad Sprite Sheet.json`
- `Pidgeon Sprite Sheet.png` and `.json`
- `Seagull Sprite Sheet.png` and `.json`
- `Fishes Sprite Sheet.png` and `.json`
- `Ball.png`
- `Swamp Tileset - Blue.png`

## Project layout

| Path | What |
|---|---|
| [`EthansCritters/`](EthansCritters/) | the sketch: `EthansCritters.ino`, `config.h`, `src/` (engine, game, ui, fx, generated assets), `tools/` (the art and card tools, simulator scripts, tests) |
| [`chgame/`](chgame/) | a vendored subset of [bateske/CHGame](https://github.com/bateske/CHGame) at the commit in `chgame/VENDORED.md`, with this project's patches ([`chgame/PATCHES.md`](chgame/PATCHES.md)) |
| [`bench/ECBench/`](bench/ECBench/) | the board benchmark sketch |
| [`docs/`](docs/) | the [streaming report](docs/streaming-report.md) and the README's banner |
| `Sprites/` | the source art: Elthen's purchased sprite packs, not in the repository (ignored by git) |
| [`tools/`](tools/) | `vendor_chgame.py`, `card_sync.py`, `release.py` |
| `out/` | build output and the card file (not in git) |

[CLAUDE.md](CLAUDE.md) is the working manual for changing the game.
