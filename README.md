# Ethan's Critters

A game for the CHGame handheld: a squire clears the critter nests out of a
big swamp, then beats the giant toad in The Rot. It is also an experiment.
How far can unique art and animation go when it is streamed from the SD
card every frame, on a 48 MHz RISC-V with 20 KB of RAM, a 128x128 panel and
the card on the panel's SPI bus? And what does that mean for the library?

![Ethan's Critters gameplay](EthansCritters/docs/gameplay.gif)

**Download:** the game is on the [Releases](../../releases/latest) page, as a
`.chgame` cart for CHGame's SD menu and tools, and as a zip to unzip onto a
FAT32 microSD card's root. The release files are not in the repository (the
card data makes them ~14 MB a version); `python tools/release.py` builds them.

How to play, the controls and the critters are in the game's own
[README](EthansCritters/README.md). The experiment's full write-up, with
every board measurement, the simulator's estimates, the flash and RAM
budgets and what it all means for the CHGame library, is
[docs/streaming-report.md](docs/streaming-report.md).

The reel above shows, in order:
- the title loop;
- a walk out of The Landing;
- the swamp standing still while its ambient phases play;
- a raccoon fight, a beaver's ambush and a chameleon;
- the squire behind a willow;
- the pause map;
- OLD GULLET rising, dying, and the win clip;
- The Rot healed.

## The streaming experiment

Nothing the player sees is tiled or packed. The world is one 1024x768
painted bitmap, and it is on the card eight times over: painted in four
ambient phases (reeds swaying, ripples and swell, the shore's glint, lily
pads bobbing, gnats over The Rot), so the whole swamp moves at no cost but
card space, and again healed (The Rot alive once its toad is beaten). The
sprites, the toad, the willows that the squire can walk behind and the
full-screen clips are all stored on the card as they are drawn. Every frame
reads what it needs:

| What | Card reads a frame | Time (board, or from the board's 832 + 222 us) |
|---|---|---|
| The world under the camera (`World.cpp`) | 1 command, 16-17 blocks | 4.4 ms p50, 5.5 ms max (board) |
| Sprite frames the cache misses (`Spool.cpp`) | 0-2 commands, a few blocks | ~1.3 ms for 1 command and 2 blocks (from the fit) |
| A willow redrawn over an actor behind it (`Occluders.cpp`) | 1 command, 1-3 blocks | ~1.0 ms (board; the simulator said 1.3-1.6) |
| The giant toad (`Rot.cpp`) | 1 command, 3-6 blocks | ~1.7 ms for 4 blocks (from the fit) |
| A full-screen clip frame (`PathF.cpp`) | 1 command, 16 blocks | 4.0-4.2 ms (board, the title loop) |

What was measured on the board (`bench/ECBench` and the game's debug
build, a 16 GB SDHC card, `bench/results/2026-10-03-board-summary.txt`):

| What | Board |
|---|---|
| `sd::stream` | 832 us a command at a random offset (644 warm) + 222 us a block |
| Soak, 34,000 blocks | 0 bad, 0 failed: no CRC needed |
| Cold first reads | none slow (max 1.4 ms) after power-up |
| The first read after the card was written | one ~1.05 s stall: the first boot with new card data reads the whole file once (STIRRING THE SWAMP: the 44.5 MB file in ~21 s, simulator with the board's timing) |
| `recover()` / `init()` | 66 us / 4.1 ms; every forced timeout recovered without `init()` |
| Full 12 bpp flush | 8.41 ms on the wire, 0.10 ms to start |
| Copying a block into the framebuffer | 26-52 us, hidden under the next block's DMA (and since M8 the sprites drawn in the rest of that wait) |
| `gfx_blit4k` 32x32 (patch P4) | 149 us, 170 at odd x, 237 mirrored, 455 remapped; CHGfx's `gfx_blit` 226 / 442 |
| The play loop (world, squire, HUD) | 4.9 ms avg / 5.9 ms max after `gfx_wait()`: 60 fps |
| 8 critters, nests and particles | 7.1 ms avg / 8.0 ms max (6.35 ms after M6): 60 fps |
| Crowds of 6 / 8 / 10 beavers and chameleons | 6.7 / 7.0 / 7.4 ms avg: 60 fps |
| 12 critters | 8.8 ms avg: the pacer holds 30 fps |
| Willow Hollow, the willows redrawn over whoever walks behind them | 7.4 ms avg: 30 fps |
| The toad fight near / fighting / angry | 6.5 / 6.8 / 7.3 ms avg: the angry phase dips to 30 fps |
| The title loop, a 16-block clip frame | 4.18 ms: 60 fps |

What it comes to: at 60 fps a frame has 16.67 ms. The flush holds the bus
for 8.41 ms, which leaves about 8.2 ms after `gfx_wait()` for the card and
the drawing. The world takes about 4.6 ms of that. What remains pays for
the sprites, the willows and the toad. One card command costs as much as
four blocks, so the art is laid out to turn a frame's needs into a few long
reads, and card space is spent freely: the card file is about 45 MB, 43 MB
of it the world's phases and layers.

Most of the sprites cost no extra time, because they are drawn while the
world's blocks are still arriving (`src/game/Play.cpp`):
- the world's 16-17 blocks arrive by DMA one after another, about 220 us
  each;
- copying one into the framebuffer takes only 26-52 us of that;
- in the rest of each wait the CPU draws any sprite whose columns are
  final, once every sprite that must be drawn before it has been drawn.

The picture is the same to the pixel. In the simulator's model of the
board (not yet measured on the board) that takes 0.1-0.65 ms off the frames
of the crowds, the critter fights and the toad fight. In Willow Hollow the
time it frees goes to more willow passes, and the simulator holds 60 fps
there even with 0.5 ms added for its optimism.

When a frame does not fit, the game draws every second tick (30 fps) while
logic stays at 60 Hz (`src/engine/Pace.h`). It sheds the extra willow
passes first, then the frame cache's reads (the squire's last), and the
actors show the frame they showed.

A read that stalls costs one frame, not a freeze:
- in play a stream waits at most 32 ms for the card
  (`sd::setStreamTimeout()`, patch P2);
- a failed stream is recovered in 66 us (`sd::recover()`) and its frame is
  not sent, so the panel keeps the last picture and the next frame tries
  again;
- only a card that stays silent for over a second goes to the card screen.

[docs/streaming-report.md](docs/streaming-report.md) has all of it:
- every board number;
- the simulator's estimates for M8, with labels;
- what more RAM would buy;
- what worked and what did not;
- proposals for the library: a streaming module, a wait hook in
  `sd::stream()`, CHSd's per-block overhead, a CPU model in the simulator.

[CLAUDE.md](CLAUDE.md) is the working manual for changing the game.

**Size** (the final M8 build):

| Build | Image | Limit | RAM | Limit |
|---|---:|---:|---:|---:|
| Release | 49,796 B | 50,432 B (both save pages) | 17,740 B | 18,416 B, plus a 2 KB stack |
| Lean debug | 50,536 B | 50,688 B (one save page) | 17,504 B | |

Flash, not RAM, became the limit from M7 on.

## Layout

| Path | What |
|---|---|
| `docs/streaming-report.md` | the experiment's write-up: what was streamed, the architecture, the board's numbers, the budgets, what it means for CHGame, how to upstream the patches |
| `EthansCritters/` | the sketch: `EthansCritters.ino`, `config.h`, `src/` (engine, game, ui, fx, generated assets), `tools/` (the art and card tools, simulator scripts, tests) |
| `chgame/` | a vendored subset of [bateske/CHGame](https://github.com/bateske/CHGame) at the commit in `chgame/VENDORED.md`, with this project's patches |
| `bench/ECBench/` | the board benchmark sketch |
| `Sprites/` | the source art: Elthen's purchased sprite packs, not in the repository (ignored by git). Put your own copy of the packs here to build the card |
| `tools/` | `vendor_chgame.py`, `card_sync.py`, `release.py` (a GitHub release: the cart and an SD card zip) |
| `out/` | build output and the card file (not in git) |

## Building and the card

You need Python 3.13 with Pillow, pyserial and ziglang (the simulator's
compiler), arduino-cli, and the CHGame board package 0.3.0 for the board
(staged from a CHGame checkout with its `tools/release/stage.py`). From the
project root, use `./ec` in Git Bash or `ec.cmd` in cmd or PowerShell:

| What | Command |
|---|---|
| The card file `out/card/CRITTERS.DAT` and `src/assets/CardIndex.h` | `python EthansCritters/tools/mkcard.py` |
| Release build, with its size | `./ec build` |
| Host tests | `./ec test` |
| Everything that needs no board (tests, every script twice, the build) | `./ec check` |
| A simulator script | `./ec run tools/scripts/<script>.txt out/<script>` |
| The README's GIF | `./ec gif` |
| Put the card file on the board's SD card over USB | `python tools/card_sync.py` |
| Upload the game | `./ec upload` |
| The cart for the SD game menu, `out/EthansCritters.chgame` (needs a CHGame checkout beside this project, or `CHGAME_ROOT`) | `python EthansCritters/tools/export_cart.py` |
| A release: the cart and the SD card zip in `out/release/`, the notes; `--publish` tags `v<ECRT_VERSION>` and creates the GitHub release with them | `python tools/release.py [--publish]` |

`mkcard.py` is deterministic. The card's build hash is compiled into the
game, so a board running a build with the wrong card says WRONG CARD DATA.
Copy `CRITTERS.DAT` to the root of a FAT32 card; a freshly formatted card
keeps it in one piece, and the game accepts up to eight pieces. The first
boot with a new card file reads all of it once behind a bar (STIRRING THE
SWAMP, about 20 s): the board's card stalled for a second on its first read
of freshly written data, and the game would rather pay that on the boot
screen. It remembers the file's hash in its save, so later boots go
straight to the title.
`card_sync.py` does the copy without taking the card out of the board: it
uploads CHGame's CHSDtoUSB app so the board becomes a USB card reader,
copies and checks the file, ejects the drive, then uploads the game again.

The cart is the game as CHGame's SD menu takes it (`spec/chgame.md` in the
CHGame repository): a ZIP with the release image, `CRITTERS.DAT` as the
game's SD file, the box art (`EthansCritters/docs/cart.png`, painted by
`tools/cart.py` from the game's own sprites and tileset) and the reel.
`chgame cart deploy out/EthansCritters.chgame --card E:\` lays out a card:
`GAMES/ETHANSCR.CHG` for the menu, the file at the root for the game.

### Without the sprite packs

The source art is not in the repository (above, Art). A clone without it
still builds the program, because the generated sources in
`EthansCritters/src/assets/` are committed: `./ec build` and `./ec upload`
work as they are. The program checks the card data's hash, so take
`CRITTERS.DAT` from the release made from the same commit (its SD card zip
on the Releases page). Rebuilding the card data, the tests and the
simulator's scripts need the packs: put these files, as Elthen ships them,
in `Sprites/` (the tools stop with this list if any is missing):
`Squire Sprite Sheet.png`, `Raccoon Sprite Sheet.png`, `Turtle Sprite
Sheet.png`, `Beaver Sprite Sheet.png` and `.json`, `Chameleon Sprite
Sheet.png` and `.json`, `Giant Toad Sprite Sheet - Green.png`, `Giant Toad
Sprite Sheet.json`, `Pidgeon Sprite Sheet.png` and `.json`, `Seagull Sprite
Sheet.png` and `.json`, `Fishes Sprite Sheet.png` and `.json`, `Ball.png`,
`Swamp Tileset - Blue.png`.

## The library patches

Every change under `chgame/` is a patch in a series meant for upstream,
listed in [chgame/PATCHES.md](chgame/PATCHES.md). All of them are additive,
and the three SD games keep byte-identical images:

| # | Where | What |
|---|---|---|
| P1 | CHSd | `fat::stream()`: a range of a file's blocks in one command per FAT run |
| P2 | CHSd | `sd::setStreamTimeout()` and `sd::recover()`: a stalled stream recovers in 66 us, not a 1 s timeout and a re-init |
| P3 | CHSd host, simulator | the bus rule checked in the simulator (a BUG on any card access during a flush), and card timing knobs (`CHSD_SIM_CMD_US`, `CHSD_SIM_BLK_US` ...) |
| P4 | CHGfx | `gfx_blit4k()`: a keyed 4 bpp blit at any x with flip, remap, solid ink and dither, its loops in SRAM |
| P5 | CHGfx | `gfx_blit4k()`'s remap and ink through a table of pixel pairs: a remapped blit about half its cost (estimated; not yet measured on the board) |

Checked on the final tree:
- the series applies with `git am` to upstream `e876774c` and reproduces
  `chgame/` byte for byte;
- CHWords, CHWordWheel and CHCrossword build byte-identical;
- their 236 simulator images are pixel-identical;
- CHSd's and CHGfx's tests pass.

None of the patches has been offered upstream yet; the steps are in
[docs/streaming-report.md](docs/streaming-report.md#10-upstreaming).

## Licence

The code and documentation of Ethan's Critters are under the MIT licence
([LICENSE](LICENSE)). The CHGame code under `chgame/` keeps its own licences (`chgame/NOTICE`,
`chgame/LICENSE`). Elthen's sprite packs are not in the repository and are
not covered by any licence here: they are sold by Elthen, under Elthen's
terms. The game's files (the release's cart and card data, the README's
GIF, the cover) carry the art only in the game's own form.

## Art

The sprite sheets and the swamp tileset (not in the repository; the squire, the
raccoon, turtle, beaver and chameleon, the giant toad, the birds and
fishes, the tileset) are by **Elthen**, bought as sprite packs from
[elthen.itch.io](https://elthen.itch.io/). The game's name is half theirs
too: "Elthen" was misread as "Ethan" when the sheets came in, and Ethan's
Critters stuck.
