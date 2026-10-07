# Ethan's Critters: guide for agents

A game for the CHGame handheld (CH32X035 RISC-V at 48 MHz, 128x128 panel,
microSD on the panel's SPI bus), built on the CHGame library, CHGfx and CHSd
from bateske/CHGame. A squire explores one big swamp and clears out the
critter nests in it; the look is earthy, dirty and worn (not the casino look
of the CHGame example games). The game is also an experiment: how far can
unique art and animation go when streamed from the SD card, and what does
that mean for the library and for performance. The approved plan (kept
outside the repository) set the architecture, budgets, patch set and
milestones M0-M8; docs/streaming-report.md records how they came out.

## Layout

| Path | What |
|---|---|
| `EthansCritters/` | the sketch: `.ino`, `config.h` (`ECRT_*`), `src/**` (`src/Size.h` first in every hand-written source: the compiler's size choices, `ECRT_OUTLINE`), `tools/` (game.py, chsim/chdrive.py, scripts/, assets/, world/, tests/) |
| `chgame/` | vendored subset of bateske/CHGame at the commit in `chgame/VENDORED.md`, relative layout kept (`tools/`, `platform/board/arduino/CHGame/libraries/{CHGame,CHGfx,CHSd}`) |
| `chgame/PATCHES.md`, `chgame/patches/` | every change we make under `chgame/`, one commit each, for upstreaming |
| `bench/ECBench/` | the board benchmark sketch |
| `Sprites/` | source art by Elthen (elthen.itch.io; credited in both READMEs, and the misread name is where "Ethan" comes from), read-only; never modify. Purchased packs: ignored by git and never published on their own (the game's files carry the art in game form, which is fine); a fresh clone needs a copy put here before mkcard or the tests run |
| `out/` | build output and generated card data (`out/card/CRITTERS.DAT`), ignored by git |
| `tools/vendor_chgame.py` | re-vendors chgame/ from a CHGame checkout |
| `out/upstream/` | a full CHGame checkout at the vendored commit (reference, staging, upstream checks) |
| `EthansCritters/tools/world/` | the world: `map.txt` + `objects.json` (authored; since M7a also the arena: rect, trigger, the toad's spot, the reeds), `paint.py` (painter, makes `src/assets/WorldData.*`; since M8c the world 4 ambient phases x 2 layers, the healed swamp the second; since the playtest round `Painter.hide()`: the ground where the willows would hide half the squire is solid), `worldpack.py` (card layout WRLD), `occlpack.py` (the willows as occluders, card section OCCL, a copy a layer), `tileset.py` (the tileset's parts) |
| `EthansCritters/tools/assets/` | `sheets.py` (the source sheets as data), `quantise.py`, `palette.py` (the colours, the remap tables; REMAP_SEPIA since M7b), `props.py` (built art: nests, mushrooms, puff, stick, the bramble gate, the sword's swipe arc), `packbank.py` (card section BANK, `src/assets/Bank.*`), `bosspack.py` (the toad: card section TOAD in row bands, and each frame again in red for the hit flash; `src/assets/Boss.*`) |
| `EthansCritters/tools/card/` | `cardfile.py` (CRITTERS.DAT's header and sections), `clip.py` (the Path F clip format), `title.py` (the title: the still and its 16-frame loop), `clips.py` (M7b: renders the clips TITL, DEAD, WINC, PMAP; cached in `out/art/clips.cache`, previews in `out/art/clips/`) |
| `EthansCritters/chgame.json`, `tools/cart.py`, `tools/export_cart.py`, `docs/cart.png` | the cart (CHGame's `spec/chgame.md`): what the exporter needs to know, the cover's recipe (artkit; the game's own sprites and tileset at 1x, the title screen's own lettering from `tools/card/title.py`), the one-command export (`out/EthansCritters.chgame`: card, build, cover, export, the cart's cover, verify) and the cover itself. The exporter (`tools/chcart`), `boxart.py` and `artkit` are the CHGame checkout's (`../CHGame` or `$CHGAME_ROOT`, pip-installed), not vendored: see The cart below |
| `README.md`, `EthansCritters/README.md`, `EthansCritters/docs/gameplay.gif` | the project's README (the experiment's numbers, building, the card, the patches), the game's (CHGame's game-README format), the reel (`tools/scripts/gameplay.txt`, `./ec gif`, <= 1 MB) |
| `docs/streaming-report.md` | the experiment's write-up for the CHGame author (M8e): what was streamed, the architecture, every board number, the simulator's estimates (labelled), the budgets by milestone, what worked, the patch set and proposals for the library, the upstreaming steps, the board runs still to do. Keep it in step when a board number comes in |

## Commands (from the project root; `ec.cmd` on cmd/PowerShell, `./ec` in Git Bash)

| What | Command |
|---|---|
| Release build + size | `./ec build` |
| Debug build | `./ec build --debug` |
| Upload (release / debug) | `./ec upload [--debug]` |
| Simulator script | `./ec run tools/scripts/<s>.txt out/<s>` (paths relative to the sketch) |
| Everything without a board | `./ec check` |
| Host tests | `./ec test` |
| Size report | `./ec size --top 30` |
| World paint + previews (`out/world/`, `phases.gif`, `healed.png`; mkcard runs it, cached by input hash; 8 paints, ~26 s) | `python EthansCritters/tools/world/paint.py` |
| The clips again + previews (`out/art/clips/`; mkcard runs it, cached by input hash) | `python EthansCritters/tools/card/clips.py [--force]` |
| The README's GIF (`EthansCritters/docs/gameplay.gif`) | `./ec gif` |
| Board probe (safe) | `./ec uploader probe` |
| The cart, `out/EthansCritters.chgame` (needs the CHGame checkout) | `python EthansCritters/tools/export_cart.py [--no-build] [--no-art]` |
| A release (`out/release/`: `EthansCritters-<v>.chgame`, `-sdcard.zip`, `release-notes.md`; `--publish`: tag `v<ECRT_VERSION>`, push it, `gh release create`) | `python tools/release.py [--publish]` |
| The cover's previews (`out/art/EthansCritters_{1x,4x,menu}.png`) and the house checks | `python -m artkit show EthansCritters/tools/cart.py --name EthansCritters --out out/art` |
| Put the card file on the board's SD card (CHSDtoUSB, copy, eject, re-upload) | `python tools/card_sync.py [FILES] [--then release\|debug\|none]` |
| Another sketch (bench) | `python chgame/tools/chgame.py --sketch bench/ECBench <command>` |

The board package is `CHGame:ch32v@0.3.0-local` (staged from upstream with
`tools/release/stage.py` in `out/upstream`). FQBN release
`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`.
Python 3.13 with Pillow, pyserial and ziglang (the simulator's compiler is
`python -m ziglang c++`).

## Rules

1. **Simulator first.** Everything but timing and sound is checked on the PC.
   The simulator is deterministic: a change that should not alter the picture
   must give identical frames.
2. **Measure size after every change** (`./ec build`): image <= 50,432 B (both
   save pages), static RAM <= 18,416 B (+2 KB stack). RAM is the binding limit.
3. **The bus rule.** The SD card and the panel share SPI1. Touch the card
   only between `gfx_wait()` and the next `gfx_flushAsync()`/`gfx_flush()`.
   `gfx_chunkScratch()` (1 KB) is the SD landing buffer and is also the
   flush's buffer: idle only in that same window.
4. **Generated files are not edited by hand** (`src/assets/*` from
   `tools/assets/`, the card data from the packers). Re-run the tool.
5. **Library changes go in `chgame/` as listed patches.** Additive only
   (new functions/files), so other games' images do not change; record each in
   `chgame/PATCHES.md`. CHSd's `read()`, `wait()` and `Fat.cpp` must stay
   byte-identical (the SD games have tens of bytes to spare).
6. **Identifiers:** config prefix `ECRT_`, `save::magic("ECRT")`,
   `dbg::begin("ECRT ...")`, chdrive `IDENT = "ECRT"`, package `CRITTERS.CHG`,
   card file `CRITTERS.DAT` (8.3, card root).
7. **Line endings LF** (`.gitattributes`); write files from Python with
   `newline="\n"` and UTF-8. `Sprites/` is not tracked (purchased packs).
8. **Commits** only when asked; end messages with the Co-Authored-By line.

## Measured on the board (2026-10-03, ECBench debug build, 16 GB SDHC "SD16G")

Use these, not the simulator's defaults, when budgeting a frame
(`bench/results/2026-10-03-board-summary.txt` has the full tables).

| What | Board |
|---|---|
| `sd::stream` | 832 us per command (random offset; 644 warm) + 222 us per block |
| 17-block stream (the world window) | p50 4.4 ms, max 5.5 ms; 16 blocks (title) 4.03 ms in game |
| cold first reads | no slow ones (max 1.4 ms), no warm-up needed |
| soak 34,000 blocks | 0 bad, 0 failed: no CRC needed |
| `recover()` / `init()` | 66 us / 4.1 ms; forced timeouts all recovered without init |
| full 12 bpp flush on the wire / start | 8.41 ms / 0.10 ms |
| column copy (128x4 B block into fb, nibble shift) | 26 us (x%8 == 0), 51.5 us (shifted): hidden under each block's DMA |
| full-screen 16-block copy into fb | 229 us |
| `sprite4` 24x21 (span4; remap/flip same) | 174 us |
| `gfx_blit` 32x32 transparent | 226 us even x, 442 us odd x |
| `gfx_remapRect` 24x8 | 50 us |
| `gfx_blit4k` 32x32 (P4) | 149 us even x, 170 odd, 237 flipped, 180 dithered, 455 remapped |
| the play loop (world 17 blocks + squire + HUD), debug build | 4.9 ms avg / 5.9 ms max after `gfx_wait()`: 60 fps, 0 frames over budget |
| M5 slice, 8 critters + nests + particles (slice_perf, debug) | 7.1 ms avg / 8.0 ms max: 60 fps (1 frame over); cache deferred 362 of 660 frames; stack 1,312 B |
| M5 slice, 12 critters (stress) | 8.8 ms avg / 10.3 ms max: the pacer holds 30 fps (the sim's draw model is ~0.5 ms optimistic) |
| M6 (board, debug): lone willow / hut willows | 6.0 / 7.3 ms avg, 60 fps; a willow pass ~1.0 ms (sim said 1.4) |
| M6: Willow Hollow | 7.4 ms avg / 8.5 max: drops to 30 fps (the M8 target) |
| M6: crowds of 6 / 8 / 10 beavers+chameleons | 6.7 / 7.0 / 7.4 ms avg, 60 fps; M5 slice 8 now 6.35 ms |
| M7: title loop (16 blocks/frame) | 4.18 ms, 60 fps |
| M7: boss fight near / fighting / angry | 6.5 / 6.8 / 7.3 ms avg; the angry phase dips to 30 fps (42 of 445 frames); stack 1,332 B |
| M8 (board, debug, 2026-10-04): walk / crowds 6-8-10 / slice 8-12 | 5.4 / 6.4-6.8-7.2 / 6.75-7.74 ms avg: all 60 fps (12 critters now holds 60) |
| M8: Willow Hollow / hut willows / lone willow | 6.92 / 7.1 / 5.95 ms avg: Hollow now 60 fps |
| M8: boss intro / near / fighting / angry | 7.4 / 6.5 / 7.0 / 7.45 ms avg; the angry phase still 30 fps; stack (`stk=`) 1,544 B |
| M8: `ECRT_HIDE_US` 0 / 60 / 100 / 150 | 0 is 0.2-0.4 ms slower; 60-150 identical |
| M8: `gfx_blit4k` 32x32 with P5 | 134 even, 154 odd, 193 flipped, **237 remapped** (was 455), 166 dithered; 24x21: 88 plain, 139 remapped |
| **first read after a card write** | **~1.05 s stall once** (boss_perf's first fight after card_sync wrote CRITTERS.DAT; gone on the re-run). Before M8a the game froze for it (CHSd's default 1 s timeout); since M8a the first boot with new card data reads the whole file once (the warm-up, below), and a stall in play loses frames, not a second |

Not yet measured on the board (M6b, the simulator with the board knobs):
`crowd_perf.txt` 6 / 8 / 10 beavers and chameleons 6.05 / 6.21 / 6.47 ms
avg, 7.56 / 7.08 / 7.52 max, 0 frames over (their frames are smaller than
the raccoons', the shimmers dithered); `slice_perf` 8 / 12 is 6.54 / 7.12
ms there against 7.1 / 8.8 on the board, so expect ~+0.5 ms. Willow
Hollow with its brood (`occlude.txt` hollow) 7.04 avg / 7.89 max, 0 over
(6.86 / 7.80 without chameleons). That is close to the line: with the
sim's ~0.5 ms optimism added (`J 500`), Willow Hollow with the squire
behind its willows and critters about drops to 30 fps (his best pass,
1.1-1.6 ms, is always made), so expect 30 fps there on the board.

**Flash is the next limit.** After M6b's reviews: release 44,224 B (RAM
17,360 B), debug 47,244 B (of the 50,944 a debug image may take; M6b
itself 44,164 / 47,180, the chameleon's HOME +60 B); after M6a they were
40,412 / 43,312, after M5 40,216 / 46,752. M6a made room (terrain
run-length 3,072 -> 1,298 B, the bank's clip table 14 -> 12 B a clip, the
mushrooms 6 -> 4 B each) and spent it on the occluders (~1.9 KB). M6b's
cast, nests, moods and stick cost ~3.8 KB after a diet in Critter.cpp: the
states that only wait are a table (`TIMED`, 3 B each) and what each state
shows is a table (`POSES`, 4 B each) instead of switches (-1.4 KB and
-0.65 KB; the raccoon's and turtle's frames and `A` lines identical).
`ECRT_LEAN` (debug builds on the board, config.h) drops ~4.1 KB: the
sounds, the tune and CHGame's sequencer (silent), the core's
`delayMicroseconds()` and its 64-bit division (J busy-waits on
`micros()`), A's actor report (A still answers), the card screen's
speckle. M7 needs ~4-5 KB: 50,432 - 44,224 = 6.2 KB. The debug image is
the tighter limit: 50,944 - 47,244 = 3.7 KB, less than M7 needs (its
sounds aside, which a lean build drops), so M7 must trim the lean build
too, or its debug build will not upload (measured: the SELECT overlay out
of a lean build, -332 B; Z prints the same numbers). Next candidates if
needed: the bird clips in the bank (stretch goal, ~0.25 KB of tables),
the ripple particles over a diving beaver (`fx::ripple`, ~0.1 KB; the ring
drawn over it stays), the stick parried back at the critters (~0.3 KB). (The occluders'
coverage was fixed on the card instead, after M8: see Occluders below.)

**After M7a (the gate and the toad):** release 48,400 B (RAM 17,424 B,
992 free), debug 50,540 B (404 under the 50,944 a debug image may take).
The Rot (`src/game/Rot.cpp`, ~4 KB of code standalone) plus its hooks cost
~4.2 KB after a diet (a table for the waiting states, the rot raccoons out
of the reeds in turn, no stun stars); made room: the birds and the ball out
of the bank (a stretch goal; `packbank.SKIP_SHEETS`, ~0.25 KB of tables;
the lab's clip numbers moved: nests 32, mushrooms 36), `fx::ripple` gone
(the ring over a diving beaver stays), `G` gone. Note: since M7a GCC's LTO
splits the image into 2 partitions (it was 1); `-flto-partition=one` (a
link flag the tools cannot pass) would save only ~200 B. `ECRT_LEAN` now
also drops: the SELECT overlay (Z prints the same), `H` (the device
scripts do not use it), the parried stick flying back at the critters (it
breaks like a blocked one), the card screen's hint lines (the two big
lines stay). M7b has 50,432 - 800 - 48,400 = ~1.2 KB of release flash and
~100 B of debug image left (50,644 - 50,540): it must trim the lean build
further (e.g. leave M7b's own extras, the pause map, out of it).

**After M7b (the clips, the pause map, the win, the README):** release
49,472 B (RAM 17,448 B, 968 free; 960 B under 50,432, so 160 B left above
the 800 B margin), debug 50,424 B (520 under 50,944). M7b's core (the
title loop, the death and win clips in play, `pathf::play`, REMAP_SEPIA)
cost only +112 B, because the death and win text screens went (their words
are in the clips now) and the screens share `hud::centred`/`centred2`.
M7b's extras cost 960 B and sit under `ECRT_EXTRAS` (config.h; off in a lean
build): the pause map and its fog, the title's best time and CONTINUE / NEW
GAME choice, the win clip's times. A lean build has no pause (START does
nothing), carries on from PRESS A, and shows the win clip without times.
Nothing is left for M8 in the release image without a diet. Candidates:
`play::mmss` (120 B, fmtInt inlined; two digits by hand ~70 B), the card
screen's board (`drawCard` 244 B), or folding `Rot.cpp`'s banner/lean code.
**After the M7 gameplay review:** release 49,604 B (RAM 17,456 B), debug
50,536 B: +132 / +112 B for the fixes in the M7 review notes under The Rot
below and the title's choice. Not done for flash: the fog lifting over
what he sees rather than the cell under his feet (+184 B as a 4-corner
loop: the map stays mostly sepia on a real walk), a cross over the map's
toad once it is beaten (~30 B).
**After CHGfx P5 (M8b, `chgame/PATCHES.md`; not yet on the board):**
release 49,704 B (RAM 17,552 B), debug 50,644 B (RAM 17,280 B): +100 /
+96 B. `gfx_blit4k`'s remap and ink go through a 256-byte table of pixel
pairs that each such call builds on its stack; expected 32x32: remapped
~235 us (455), mirrored ~195 (237), plain ~134 / ~153 at x % 8 = 0 / 3
(149 / 170): B5 has them, plus `blit4k32_rmflip`, `_solid`,
`blit4k24_odd`, `_remap`. A remapped critter is now the deepest static
stack path (debug image 1,456 B; the occluder pass, which the board's
1,332 B `stk=` was, 1,328): re-measure P's `stk=`. The simulator's draw
model (`scene::blitCost()` since M8d, used by Rot.cpp too) still charges
P4's numbers; with P5's estimate (k 101, +31 mirrored, +55 and 12 us a
call remapped or inked, +25 dithered; `CHSIM_FLAGS=-DECRT_SIM_P5=1` since
M8d) boss_perf's angry phase is 7.16 ms avg (7.31) and holds 60 fps with
`J 250` (it went to 30 before), not with `J 500`. Once B5 has measured P5
on the board, put its numbers in `blitCost()` and drop the switch.

**After M8a (card faults, the warm-up, a flash diet):** release 48,732 B
(RAM 17,432 B, 984 free; 1,700 B under 50,432, so ~1.1 KB above a 600 B
margin for the rest of M8), debug 50,228 B (716 under 50,944, and under
50,432: both save pages). The card work cost +748 B (`sd::recover()` and
the stream timeout in CHSd's code ~0.2 KB, the warm-up screen ~0.2 KB, the
held frames, `play::store()` called from two places); the diet took 1,620 B
off that build. Each item below measured by undoing it alone on the final
build (they overlap a little): the frame cache's per-fetch stats only in
debug builds (`spool::stats()` had no reader in release: -248 B, -20 B
RAM); `hud::board()` for CHGame's `panel(..., 2, C_BARK, C_SOOT)` (five
fills, same pixels; `roundRect`/`fillRound` gone: -228 B); `ECRT_OUTLINE`
(Size.h) on small helpers LTO copied into every caller (sfx, the camera's
shake/ease/follow, the toad's crouch/air times and gate test, the frame
cache's key and insert, the nests' blocks and stage, the sticks' box and
origin, the white mushroom test, `fx::number`: -224 B); Size.h's pragmas
(no jump tables -196 B, no loop-invariant hoisting -212 B, no partial
inlining -48 B; switch conversion off gave 0 and was dropped);
`hud::ring()` for `gfx_circle` (an octant loop: -76 B); fills for
`gfx_rect` (the boss bar's rim, the pips, the map's marker: -56 B);
CardFormat.h's fields as single loads (-52 B); the card screen's words in
one string, no pointer table (-40 B); the save record read and written in
place (`progress::load()/edit()/store()`: -28 B); the nails in a loop (-20
B); `mmss` by hand (-16 B); a nest's spawn spot checked in one place (-12
B). Every snap and GIF of every script identical before and after (256
images), the board-knob `Z` lines identical. Next candidates: the
unspecified argument order of `rndRange` in Fx.cpp's `mud`/`chips` (see
Gotchas) blocks outlining it (~50 B); the bank's clip table (12 B a clip)
could lose its `flags` byte to `msPer10`'s top bit (~40 B).

**After the M8 merge (M8d):** M8a + M8b + M8c merged: release 48,908 B
(RAM 17,528 B), debug 50,400 B. The sprites drawn in the world's stream
(below) cost ~1.3 KB as first written; made room for it: `ECRT_OUTLINE` on
the frame cache's `reserve()` (inlined twice: -188 B), the camera's
`bound()`, `spool::setBudget()`, `card::check()`, the chameleon's
`tongueReach()` (each tried alone by a script over every function of the
game, then kept greedily: -264 B together; outlining `fetch()`, `teeth()`,
`plan()`, `tryCard()` or `budget()` made the image bigger, LTO's two
partitions shifting); the bank's clip table 12 -> 10 B a clip
(`packbank.py`: `flags`, which nothing read, and `perBlock`, now
`bankPerBlock()` = 128 / slot4: -56 B); the draw list one static
`scene::List` shared by `plan()` and the frame (+172 B RAM, -288 B on the
deepest stack path: `plan()`'s list sat in `loop()`'s frame under the
whole frame's drawing). After: release 49,796 B (636 B under 50,432; RAM 17,740 B, 676
free), debug 50,536 B (RAM 17,504 B; under 50,688, so it keeps one save
page: the warm-up's mark needs it, or every boot of a debug build would
read the 44.5 MB again). `ECRT_LEAN` now also drops (~700 B of the debug
image, each measured as it went in): the HUD's compass (the needle and
`compass()`: -352 B), the card screen's board (its words stay, on the mud:
-144 B), the boot screens' name and READING THE CARD (-76 B), `Z`'s
`loaded=` and `drawMisses=` (-56 B), the warm-up's bar (its words stay:
-52 B), and A's stub says `ACTORS -` (-20 B). A static estimate of the
deepest stack (the disassembly; RAM functions' `__riscv_save` calls and
the blitter's SRAM row loop counted): debug 1,552 B, a remapped blit
drawn in the world's stream (was 1,248, a remapped critter after it; the
board's `stk=` read 84 B over the static figure in M7): re-measure P's
`stk=` on the board.

So at 60 fps: 16.67 - 8.41 (flush) - 0.1 = 8.2 ms after `gfx_wait()`; the
world window takes ~4.6 ms of it, a frame-cache fetch ~1.3 ms (1 command, 2
blocks). The simulator's card can be set to match with
`CHSD_SIM_CMD_US=832 CHSD_SIM_BLK_US=222`.

## The frame loop and the scripts

- `loop()`: logic ticks at 60 Hz (up to 3 catch-up ticks before a draw),
  then `gfx_wait()`, the card, the drawing, `gfx_flushAsync()`. Play is
  drawn at 60 fps, or at 30 (every second tick) while the time after
  `gfx_wait()` is over 7.9 ms (`src/engine/Pace.h`); logic stays at 60 Hz.
- Screens (M7b): boot, the card screen, the title (its 16-frame loop from
  TITL, the best time and, with a nest cleared in the save, CONTINUE / NEW
  GAME: LEFT CONTINUE, RIGHT NEW GAME, up/down the other, A picks (a toggle
  put a LEFT "to make sure" on NEW GAME, and A wiped the save); NEW GAME is
  `play::newGame()`: no nests, gate shut, clock and fog reset, best kept),
  play. In play START pauses (the map; START plays on; the library's 3 s
  START hold still exits), and
  play::draw() shows a clip instead of the swamp (`clipWanted()`: paused
  PMAP, `deadT >= WORDS_T` DEAD, `wonT >= WIN_T` WINC; `plan()` and the fetch
  are skipped then): it opens the clip (its palette), plays it at its rate
  from when it began (`pathf::play(t, loop)`), and draws the words over it
  (A: GET UP at GETUP_T; the times at wonT 255, when A goes to the title);
  back from a clip, `mood(true)` gives the swamp its colours again. The
  clock (`g.seconds`, saved) counts play ticks since the new game, not while
  paused, and stops at the toad's death (the run's time; `won()` keeps the
  best). The record (Save.h, version 2: old saves are ignored) gained the
  map's fog, 16 x 12 cells of 64 world px, a bit each, set where his feet
  are every tick; it is stored at a nest, the gate, the toad, a pause and a
  new game. A clip frame is 1 command + 16 blocks: 4.38 ms of card time with
  the board knobs (`title_loop`, `death`, `pause` perf lines); the first
  frame of a clip adds its header (1 + 1 block, ~1.05 ms), the map adds its
  fog's remapRects (all fogged: ~9,400 px, ~2.5 ms by the board's remapRect
  number: ~7 ms in all, still 60 fps), a pause adds the page write once.
- A card that stalls (M8a, `src/engine/Card.h`): every card user (world,
  frame cache, willow passes, toad, clips, title, the sprite lab) reads
  through `card::stream()`. `card::open()` sets CHSd's long stream timeout
  (`BOOT_US`, 1 s), `card::steady()` (once the title's clip is open) the
  short one, `PLAY_US` = 32 ms (the board's slowest whole 17-block stream
  was 5.5 ms, its slowest single wait ~1.4 ms in 34,000 blocks; and a lost
  frame, 32 ms + its work + the 8.4 ms flush before it, stays inside the
  three ticks the loop catches up, so the clock does not slip). A failed
  stream calls `sd::recover()`: back, `status()` stays READY and `stream()`
  returns false, the screen's draw returns false, and `loop()` drops the
  frame: no flush (the panel keeps the last one; the framebuffer is half
  old, half new and is redrawn whole next frame), no `pace::end()` (a lost
  frame does not push the pacer to 30 fps), `card::fails` counts on; from
  `HOLD` (2) lost frames in a row the logic ticks wait too (nothing happens
  unseen); after `GIVE_UP` (40, >= 1.3 s of timeouts: longer than the
  board's 1.05 s stall) or a `recover()` that fails (pulled out, power lost,
  or still busy after 32 ms), `status()` is NO_CARD and the card screen
  shows, as before M8a. A whole frame zeroes `fails`. In the simulator a
  lost frame calls `sim_present()` (lockstep waits for a sketch that stops
  presenting). A stall while the title's clip is opened says INSERT CARD
  (A tries again), not WRONG CARD DATA. Debug: `Z` prints `lost=` (failed
  streams since `Z0`). `card_faults.txt` walks past every card user
  (`lost=0` with the plain simulator); `tools/tests/sim_card_faults.py`
  (`ec check`'s simulator tests) runs it with `CHSD_SIM_FAIL_EVERY=7
  CHSD_SIM_JITTER_US=2000` (frames lost per part 14/32/19/32 in M8a,
  15/33/19/32 since the M8 merge: the merged M8a-c gives it already, M8d
  keeps it; played through), times the warm-up, and puts a card back cold
  (`CHSD_SIM_COLD_US` 1 s a region: 30 frames held, play goes on; 2 s:
  past GIVE_UP, the card screen). Not measured on the board yet: whether
  `recover()` after a timeout on a card that is still busy settling data
  returns false within 32 ms (then a stall in play is the card screen, A
  and the title); B8 on ECBench measures forced timeouts on an idle card.
- The warm-up (M8a, `EthansCritters.ino` `warm()`): the save record's
  `card` field (Save.h; the two bytes of padding the record had, so a
  version 2 save from before M8a still loads: it reads 0) holds
  `CARD_HASH | 1`'s low half once this board has read the card's data
  through. `tryCard()` with a different mark goes to WARM: STIRRING THE
  SWAMP over the mud and a bar, the whole file in 512-block streams (one
  command each but where the file's runs split) with the long timeout and
  a flush for the bar after each, then `play::markWarmed()` saves the mark
  and the card is opened again (the title). Its frames do not wait for the
  60 Hz ticks and `loop()` skips `dbg::poll()` while it runs, so the
  scripts' frames and commands land as before (the simulator's save is
  empty, so every script warms up at its boot: ~21 s of virtual time since
  M8c's 44.5 MB card; the first `perf` line of a script includes those
  frames). A debug build prints `WARM ms=N lost=N` at its end. Measured in
  the simulator with the board's card (`sim_card_faults.py`): 6.4 MB in
  3.0 s (2.14 MB/s, 93% of the wire's 2.3); the merged M8 card, 44.5 MB, in
  20.9 s; with every 1 MB region slow on its first read
  (`CHSD_SIM_COLD_US`) 300 / 950 / 1050 ms: 33.5 / 60.8 / 66.9 s (1050: 42
  streams over the 1 s timeout, recovered). Open (M8d): a once-only wait of
  ~21 s (up to a minute if every region of a fresh card stalls) is long;
  measure on the board whether the ~1.05 s stall is one per card write or
  one per region, and if it is one per write, read a block a megabyte
  instead (44 commands). A re-copy of the same data keeps the mark (no
  warm-up): its stall is then the held frames above.
- In the simulator's lockstep `wait N` runs N ticks but draws only every
  third (the catch-up), `step N` draws every tick as the board does while
  frames fit: perf scripts use `step` (`tools/scripts/play_perf.txt`).
- Debug commands (`say ...`): `W x y` starts play with the squire's feet at
  world (x, y); in play `Z` prints position, frame stats (frames, at 30 fps,
  over budget, switches, mode, avg/max us, card commands/blocks, card
  streams lost since M8a) and the
  frame cache's totals (and in the simulator `DRAW est` and, since M8d,
  `HIDE inWait= after=`: draws made in the world's stream and after it;
  a lean build leaves `loaded=` and `drawMisses=` out), `Z0` clears them,
  `J us` adds work after
  `gfx_wait()` (the pacing under load), `C k x y [nest]` puts a critter (0
  raccoon, 1 turtle, 2 beaver, 3 chameleon, 4 a beaver with a stick) at
  world (x, y), from that nest (none: home is where it stands), `H n[b]`
  hurts the squire by n half hearts from the front (b: behind; the red
  screen too since M6b; not in a lean build), `D i [n]` knocks n HP
  off nest i (none: it falls), `D 6 [n]` puts the toad down to n HP while
  the sword could find it (none: it dies; M7a), `A` prints the actors
  (each critter's kind, state, HP, place; `X` a rot raccoon), the nests' HP,
  the squire's HP and state, the mushrooms left and the toad (`TOAD
  state:hp@x,y gateN`, gate 0 shut 1 parting 2 open 3 closing), `I1`/`I0`
  god mode on/off, `O0`/`O1` the occluder passes off/on; `X0`/`X1` pull/put
  back the card (simulator); the sprite lab has `H n[b]`, `R`, `Z`, `C clip`,
  `F n`, `M dx dy`. SELECT in play (debug builds, not lean ones) toggles the
  stats overlay. The protocol owns ? S ! K L N P T Q B: never use those
  letters.
- The fetch budget (Play's `budget()`, M7a): with no room left for even one
  block the frame goes over if anything misses; then the command reads a
  block more (the next frame of the animation), and right after a frame
  that went over (`pace::lately()`, the last two) nothing is fetched at all
  (the actors show their frame a frame or two longer): at most one frame in
  three goes over, so the pacer (4 of 8) holds 60 fps through a fight.
  slice_perf / crowd_perf / occlude give the same numbers as before it.
- The simulator charges the card's time but not the drawing's: in play
  (CHSIM only) the drawing is modelled from the board's blit4k/remapRect
  numbers (`scene::simCost`, `fx::simCost()`, HUD and big text in
  `Play.cpp`) and charged to the frame (`pace::charge`), so with the board
  knobs the fetch budget, the 30 fps switch and `Z`'s times are the board's,
  estimated (`Z` also prints the drawing's share, `DRAW est`). The `perf`
  line (`rnd`) is still the card's time alone.
- `occlude.txt`: the willows over the squire, each snap with the passes off
  and on (O0/O1), walked round a lone willow, behind the hut's willows with
  a raccoon, Willow Hollow; then three perf runs (run it with the board
  knobs). Since M6a the squire is (correctly) behind the hut's southern
  willows in `death.txt` (falling, words, get_up) and `raccoon_fight.txt`
  (eat), and behind willows in `walk_world.txt` (hollow) and one frame of
  `play_perf.txt` (walk_end).
- The fight scripts: `raccoon_fight.txt` (thrust, parry and riposte, block,
  a hit taken, a mushroom eaten, the den broken: banner, drop, pip;
  re-timed in the playtest round: the swipe's arc hits where the raccoon
  arrives and the den spawns every 3 s),
  `turtle_fight.txt` (asleep, yawning hit for double damage, shell tink,
  bite, a kill), `death.txt` (die, THE SWAMP KEEPS YOU, up at the hut),
  `slice_perf.txt` (8 and 12 critters, run it with the board knobs). They
  are timed to the tick: a change to the AI, the timings or the RNG moves
  the beats, so re-check their `A` lines and snaps after one. When
  timing one: a hit-stop of n holds the logic n whole ticks after the
  tick that hit (3 a blow or a hurt, 4 a parry, 6 a nest falling); A in
  the last 10 ticks of a thrust (riposte, hurt row) queues the next
  thrust; B opens a parry window (10 ticks) at most every 24 ticks unless
  the last one parried. To find a beat, expand `step N` into N x
  (`step 1`, `say A`) and diff the state changes (`tap X 1` is two frames).
- M6b scripts (run them with the board knobs): `beaver_fight.txt` (a dive,
  the ring under the water, the ascent and bite, a hit and a kill with the
  burst; a carrier mending the lodge; three carriers put 44 px off in Reed
  Shallows' open water: a throw blocked, one taken, one parried back into
  the beaver; the lodge cleared), `chameleon_fight.txt` (the fade, the
  shimmer, appearing, the pink tell, the tongue; a shimmer struck and
  shown whole; kills while fading and creeping; the brood cleared and
  Willow Hollow's mood lifted; one led over 176 px off creeping home as
  a shimmer, its HOME state), `moods.txt` (each region's mood, the
  Hollow -> Rot crossing, a hurt's red over the olive, the Hollow lifted),
  `full_loop.txt` (all six nests with I1, warps and D, then THE ROT STIRS
  and the ochre compass to the gate), `crowd_perf.txt` (6, 8, 10 beavers
  and chameleons). The beats are timed to the tick like the M5 ones; the
  stick throws use `C 4` carriers so they do not depend on the lodge's
  spawns. Since M6b `death.txt`'s falling snap has the red screen (H).
- M7a scripts (run them with the board knobs): `gate.txt` (the brambles
  shut and solid, all six nests down with D, THE ROT STIRS, the brambles
  part, through into The Rot), `boss_fight.txt` (into the arena: the
  brambles close, the toad rises and croaks its name; its first hop comes
  down on him (the growing shadow, the dust ring); its tongue blocked,
  taken, parried (stun, riposte, blows); the red flash; the stagger; `D 6
  12`: angry, two rot raccoons, a double hop; `D 6 1` and the killing blow:
  hit-stop, death row, fade, the mood lifting, THE SWAMP IS QUIET, the
  title), `boss_perf.txt` (Z after: intro, near, fighting, angry). Timed to
  the tick like the others, and more fragile: the toad's sits use the
  shared RNG, `A`/`Z` lines themselves shift the timing a little (their
  printing costs the simulator's virtual time: calibrate with snaps, which
  do not), and the pacing mode changes how many ticks a `step` is.
- The Rot (M7a, `src/game/Rot.h`): the gate is a blocker (Play's `blocks()`
  chains it after the nests'), drawn from CLIP_GATE_PART (the left half,
  mirrored); saved in `progress::Record.rot` (GATE_OPEN, BEATEN). The toad
  is a scene item (`scene::ROT`, i 0; the gate i 1) Y-sorted with the
  squire, streamed from TOAD every drawn frame (one command, 3-6 blocks;
  only the blocks with rows on the playfield), its hit flash read from the
  red copy (TOAD_RED blocks on: a REMAP blit of that size ran ~1.1 ms past
  the DMA, every hit frame over budget). During the fight the camera is
  bound to the arena (`camera::bound`) and leans toward the toad, and no
  occluder pass is planned (the arena's two willows were taken out of
  objects.json). Board-knob sim numbers: intro 6.77 ms avg, near 6.68,
  fighting 6.85 (8 of 194 frames over: his thrust frames' misses), angry
  with both rot raccoons at him 7.36 avg / 9.05 max, 71 of 488 over, 60 fps
  held (0 switches) only because of the budget rule above. The angry phase
  has less than 0.25 ms to spare: with `J 250` added (M7 review) it
  switches to 30 fps (130 of 358 frames) and stays there, as it does with
  `J 500` (the board's usual ~0.5 ms; 174 of 313), because its average
  never gets under the pacer's calm line (6.9 ms) again. Expect the
  second half of the fight at 30 fps on the board (logic stays 60 Hz);
  near and fighting keep 60 with `J 500` (7.18 / 7.35 ms avg). Measure
  `boss_perf.txt` on the board before spending flash on it (pre-rotted
  raccoon frames in the bank would take the REMAP off two blits, ~0.15 ms
  each while on screen).
- M7 gameplay review (fixed, each found in the simulator): a stagger is
  answered at once (`Rot.cpp` tick: the sit after HURT has wait 0; after a
  whole sit, mashing A at its side staggered it again before it moved,
  24 HP to none without one attack); its rot raccoons are struck every
  DIE tick (struck only at the blow, those still in their puff lived on
  and could kill him under the win clip: THE SWAMP KEEPS YOU after the
  win, the times never shown); a hop past the arena's margins comes down
  at them (it gave up, and by the west wall sat still while he struck it)
  and during the fight his feet stay in ARENA inset 8 / 20 (Play's
  `walk()`: the trees have pockets west, east and north the arena-bound
  camera did not show, him off screen); a mover inside a blocker (the toad
  came down on him) walks out as if there were no blockers but the ground
  still stops it (`terrain::move`'s `loose`; it went through walls and
  over deep water while it overlapped; `test_world` checks); the toad's
  name board goes just over his head (low down it hid him as he came in).
  `boss_fight.txt` re-timed from the double hop on (`D 6 1` while it sits:
  D 6 is ignored in the air). `boss_perf` after: 6.77 / 6.68 / 6.87 /
  7.31 ms avg (angry 8.86 max, 62 of 488 over), 0 switches. Left: the toad
  sits forever when no landing near him is clear of the pond (he cannot
  reach it either); NEW GAME has no confirmation; CONTINUE after a win
  walks a finished swamp.
- M7b scripts: `title_loop.txt` (the loop's frames, perf), `death.txt`
  (updated: the colour drains, the death clip from its first frames to
  held, A: GET UP, the hut), `pause.txt` (a new game, a den down, the map
  with the fog, the cross, the gate's bar and him blinking; on again; all
  nests down and the gate open, the map again, The Rot's olive back after
  it), `win.txt` (every nest down, into the arena, `D 6` kills the toad,
  the win clip frame by frame, the times, the title with BEST and the
  choice, NEW GAME: the nests back), `gameplay.txt` (the README's reel:
  `./ec gif` records and joins its clips; since M8e 13 clips, 905 KB after
  the playtest round (940 before), with
  the ambient phases standing still by Reed Shallows' pond (03) and The Rot
  healed after the win (12, 13): keep it under 1 MB).
  `play_walk.txt` and `boss_fight.txt` changed at their ends (START now
  pauses; after the toad: the win clip, its times, A, the title).
- The clips (`tools/card/clips.py`): composed from the painted world
  (`world_idx()`, ambient phase 0; the win clip's dawn shot from the
  world's healed layer, the one the game streams after the win), the
  quantised sheets and the toad, in RGB: light in steps with ordered-
  dithered edges (`step()`), mist, glints; then fitted to 16 colours of the
  clip's own (`fit()`: k-means in CIELAB, the held last frame weighted,
  pins: slot 0 dark and 5 light for the game's words, 14 the tunic's rust,
  10 a gold for the dawn) and mapped back (`index()`: nearest, or an ordered
  two-colour mix where marked soft). DEAD and WINC are close shots, a
  64x64 of the world doubled (`zoom2`), with the chunky lettering of the
  title (`title.py`'s glyphs, more letters since M7b). PMAP is in the
  game's palette (the marks and REMAP_SEPIA need it), the map at 7/64 at
  `MAP_X, MAP_Y` = 8, 24 with 7 px fog cells: Play.cpp's constants must
  match (`tools/tests/test_clips.py` checks), and so must the clip lengths
  against GETUP_T - WORDS_T and 255 - WIN_T ticks.
- M8c, the streaming showcase (pure card data): the world is on the card
  8 times, `WORLD_TPH` = 4 ambient phases x `WORLD_LAYERS` = 2 (WRLD 83,969
  blocks, 43.0 MB; CRITTERS.DAT 86,998 blocks, 44.5 MB, was 6.4 MB: the
  warm-up and `card_sync` must handle it; FAT32 with 32 KB clusters keeps
  `fat::runs()` short, the sim's FAT16 4 KB clusters add ~32 ms to the boot
  frame's card open, nothing after). paint.py paints each phase as a whole
  bitmap (every paint the same draws from the one stream; the extras, sun
  flecks, gnats, the healed pond's lilies, have their own, so layout and
  terrain are identical and the phase-neutral paint is the M7 world to the
  byte): reeds' and cattails' tops lean +-1 px (`SWAY`, a wave rolling
  across the beds), lily pads dip a pixel (`BOB`), the shallows' ripples
  sway, the deep chop swells (bands of 3 rows), the waterline's glint laps
  (`LAP`), flecks blink on deep water, The Rot's gnats (10 swarms, lit on
  the dark, dark on the light) buzz. The willows stand still: OCCL keeps a
  tree's pixel only where it shows in every phase and layer (95 of 267,071
  pixels lost to reeds swaying in front), and holds a copy of its rows a
  layer (`OCCL_LAYER_BLOCKS`; The Rot's 29 kept willows differ). Layer 1,
  healed (`Painter(healed=True)`: no dead willows/reeds/grass, no grime, no
  gnats, its pond in flower), equals layer 0 outside (614, 0)-(1024, 331).
  In game (+64 B flash, 0 RAM): `world::draw(cx, cy, layer, phase)`,
  `occluders::plan(..., layer)`; Play: phase `frameCount >> 4 & 3` (a step
  every 16 ticks, ~1.1 s a cycle), layer `rot::beaten() && !g.wonT` (the
  healed swamp from the first walk with the toad dead; round its body The
  Rot stays until the win clip covers it). Same blocks a frame: every perf
  script's play numbers identical with the board knobs (play_perf 7 fewer
  commands of 1,743: fewer windows cross the sim card's run split).
  Previews `out/world/phases.gif`, `healed.png`; scripts `ambient.txt` (the
  same spots at 4 phases, GIFs), `healed.txt` (The Rot, the win by D, the
  same places healed, the healed willow's pass). Tests: test_world.py
  (round trip every phase and layer, the phases move no willow, the
  healed layer only in The Rot), test_world.cpp / test_occl.cpp (draw()
  and the passes in every layer and phase).
- The sprites drawn in the world's stream (M8d, Play.cpp's "the sprites,
  drawn while the world streams"): the world's 16-17 blocks are one card
  stream, each copied into the framebuffer while the next arrives by DMA
  (222 us a block on the board, the copy 26-51); `world::draw()`'s `idle`
  callback (World.h) gets that time after each copy with the count of
  screen words that hold their final pixels (word j: after block j at x %
  8 == 0, else j + 1; the last block has no DMA under it, so nothing is
  drawn in its wait). In the first wait `make()` builds the draw list (the
  shared static `items`), the shadows' widths, and records each item's
  blits (`scene::rec`: `scene::blit()` stores a `Prim`, 12 B; the remap
  must be REMAP_RED or REMAP_ROT, the simulator BUGs otherwise); each draw
  (item k's shadow, the toad's broad shadow, item k) gets the screen words
  it needs and, through a grid of 8 x 16 px cells holding the latest need
  so far, waits for every earlier draw that touches its cells, so the
  picture is the old one to the pixel. Held for after the stream (and so
  is anything later in their cells: `FAR`): the toad (it streams), an actor
  with a willow's pass planned over him (`occluders::planned()`), a
  chameleon showing its glint (`scene::recReal`: not a blit), an item
  whose blits do not fit (20 Prims; 19 the most seen, slice_perf's puffs).
  The queue (uint16 `words << 8 | draw`, sorted) is drawn in order while a
  wait has time left (`ECRT_HIDE_US`, 150 us after the copy, by `micros()`
  on the board and the drawing model in the simulator); after the stream
  `rest()` draws the rest of the queue, then the held items in list order,
  each followed by its pass. Simulator model: a wait's drawing is charged
  only past `ECRT_HIDE_US`. Checked: `CHSIM_FLAGS=-DECRT_HIDE_VERIFY=1`
  charges each draw made in a wait in full and at its old place, so the
  timing is the old one, and `ec check` gave all 288 images of all 30
  scripts identical to the merged M8 baseline, also with `ECRT_HIDE_US`
  1 (one draw a wait) and 100000 (every draw as early as it can go), and
  the board-knob `Z` lines identical; the plain `ec check` differs in one
  image, play_walk's SELECT overlay (its frame time, 3818 -> 3690 us).
  (Review, re-done in a separate tree: the same, plus the reel's 13 GIFs and
  `hidden.txt` (a crowd of twelve, the hut's willows, Willow Hollow, the
  right edge; ~560 frames, every one a GIF frame), at 150 / 1 / 100000,
  with and without the board knobs: after a change to the drawing order,
  compare its GIFs with VERIFY against the build before.)
  `test_world.cpp`'s idle test checks World.cpp's word counts: every word
  said to be final holds its final pixels and no later block touches it,
  at every x phase and across the file's run split.
  Board-knob perf (`CHSD_SIM_CMD_US=832 CHSD_SIM_BLK_US=222`, before ->
  after, avg us; the model is P4's): occlude lone 5638 -> 5479, Willow
  Hollow 7042 -> 6987 (DRAW est 737 -> 547; 147 passes made, 34 shed, was
  136 / 45), the hut 6744 -> 7139 (its passes 170 made / 30 shed, was 121 /
  79: the time saved goes to the willows), crowd_perf 6 / 8 / 10 6050 /
  6196 / 6468 -> 5594 / 5694 / 6133, slice_perf 8 / 12 / standing 6543 /
  7110 / 7004 -> 5988 / 6457 / 6359, boss_perf intro / near / fighting /
  angry 6765 / 6682 / 6872 / 7314 -> 6611 / 6522 / 6747 / 7086 (angry over
  62 -> 46 of 488). With `J 500` (the sim's usual optimism) everything holds
  60 fps but the angry phase (152 of 336 at 30, as before: 227 of 261);
  with P5's estimate in the model as well (`-DECRT_SIM_P5=1`) the angry
  phase holds 60 with `J 500` too (7445 avg, 62 of 488 over, 0 switches;
  without J 6958). `ECRT_HIDE_US` 120 instead of 150 costs 10-90 us
  (crowds, slice). What stays after the stream: draws touching screen word
  15 (slice_perf twelve ~2 a frame), the toad and what overlaps it in
  front (the angry phase ~2.4 a frame), glinting chameleons. The pacer was
  left alone: with P4's model the angry phase at 30 averages ~7.5 ms with
  `J 500` (also with the 60 fps fetch budget at 30: tried), far over the
  calm line, so a laxer way back would only flip between 30 and 60.
  CPU not modelled: `make()` (the list, the records, the grid: the old
  `sprites()`'s work and ~100-200 us more) runs in the first wait, where
  ~150 us of it hides.
- The sprite lab (B on the title) is in the simulator only by default
  (`ECRT_LAB` in config.h): 3.5 KB of flash and ~190 B of RAM on the board.
- Occluders (M6a, `src/engine/Occluders.h`): an actor with its feet above a
  willow's base line is behind it; right after it is drawn, a pass streams
  the tree's rows it overlaps from the card's OCCL section (each kept tree
  stored as the world shows it and the willows in front of it, 15
  elsewhere; since the occluder fix after M8) and blits them over it,
  clipped to the overlap in the landing buffer (no `gfx_setClip`: a
  variable clip costs every CHGfx call ~250 B flash, ~70 B RAM functions).
  At most 2 passes a frame (the squire's best trees, then the nearest
  critter's); all but the squire's best are shed when the frame has no time
  left: at 60 fps by the budget (7.9 ms less 0.4 for the HUD), at 30 fps by
  the pacer's calm line (6.9 - 0.4 ms). (Until the M6 review every pass was
  made at 30 fps; those passes alone kept the frames over the calm line, so
  the hut's willows and the south of Willow Hollow, which hold 60 fps by
  shedding them, stayed at 30 fps for good after one slow moment.) The
  pacer can still stay at 30 where 60 would fit: at 30 the frame cache may
  read 2 commands and 8 blocks, and with the squire's pass that takes
  frames over 7.9 ms.
  The occluder fix (after M8, the author's report: at the edge of a willow
  next to another he showed over the neighbour, flickering as he moved):
  two causes, both fixed. (1) The choice weighs rectangles, and a tree's
  card image held only its own pixels, so at a canopy's edge his pass often
  went to a tree its neighbour hid there; now each tree also stores the
  pixels of every willow whose base is at or below its own (they are in
  front of whoever is behind it, so always right over him: paint.py's
  `shared_occluders`, occlpack.py). Over every pose of his walk and idle
  frames under willows, one pass puts back 98.7% of the canopy (was 78%),
  two 99.9% (was 93%); poses missing 8+ px 16.3% -> 0.7%. No flash (the
  table's `shown` nibbles change), the same 198 trees (pruned by their own
  pixels), the same blocks. (2) His second pass was timed with the frame
  cache's fetch, which comes and goes as he walks: made one frame, shed
  the next (46 toggles in 267 frames along the hut's and the Hollow's
  edges). A tree of his made last frame is now timed without the fetch
  (`occluders::fetched()`, called by Play after `spool::fetch()`): 25
  toggles, the rest new trees waiting for time. (Made whatever the time,
  15 toggles, but Willow Hollow and the hut went to 30 fps.) Board-knob
  `occlude.txt`: the hut 7139 -> 6411 us avg, Hollow 6987 -> 7100 (10 of
  110 frames over, 0 at 30 fps); with `J 500` both hold 60 (over 7 -> 10,
  8 -> 15). Every other script's images identical but `hidden.txt`'s
  willow walks (the canopy now over him at the edges) and two timing
  shifts; `test_occl.cpp`'s `testAgain` checks the timing rule. Cost:
  release +112 B (49,908), RAM +16, debug 50,672 B: 16 B under the
  50,688 a debug image with one save page may take.
  The second round (the author: mushrooms and enemies sometimes not
  occluded). Mushrooms: 25 stood behind canopies (16 hidden whole) and,
  static, got no pass; moved in objects.json to the nearest open ground
  clear of every canopy they would be behind, reachable, in the same
  region (the arena's in the arena), 4-86 px off; `test_world.py`'s
  `test_mushrooms_clear` checks it. `raccoon_fight.txt` and `death.txt`
  still eat one on their walk (#1, now at 244,670, 24 px earlier on it:
  hp 8 -> 10, 48 -> 47 left, every later beat the same). Critters: the
  choice is now by tree, not by actor: a pass is over every actor behind
  its tree that overlaps it (`Pass::mask`), made after the last of them
  (`last`) over the union of their drawn boxes (`boxes[k]`, grown with
  `scene::grow`); the squire's trees first, then trees with a critter
  behind them before his second (which adds ~1% of his canopy; `plan()`
  ranks the best three straight into `passes`); the nearest-critter
  distance went for flash (critters near him share his tree's pass now).
  Board-knob `occlude.txt` hut with two raccoons: 6411 -> 6920 us avg,
  140 passes made (was 102), 3 of 120 over, 60 fps; the rest within 40 us.
  `test_occl.cpp`: scene1 calls after() for every actor in list order,
  `testShared` (a critter beside him under his tree: one pass, after it).
  Flash: release 50,236 B (196 under 50,432; RAM 17,788), debug 50,680 B
  (8 B under 50,688), after a diet: the distance (-84 B), the union by
  `scene::grow` (-28), no clamp on the weight (-16), ranking into `passes`
  (-32), `ECRT_OUTLINE` on `occluders::after`, `nests::spot` and
  `pace::elapsed` (a search over every function, each alone: those three
  together -76; `rest` and most others made it bigger). Any debug-build
  feature now needs a diet first.
  The third round (the author: his shadow shows through the trees; raccoons
  still come through them, entering the screen, at the start going down
  into the hut's southern willows). Found with a per-frame trace (every
  actor's drawn box, every pass planned, made, shed) checked against the
  painted world's canopy pixels, over `hut_raccoons.txt` (new: that walk,
  five willows overlapping, raccoons at him; run it with the board knobs):
  (1) a pass covered the sprite's box, and the shadow's last row (under the
  feet) showed: Play's `shadowed()` grows the box by the shadow, in `make()`
  (what touches it waits for the pass) and before `after()`; (2) ranked by
  everyone's overlap, his best tree lost to one mostly over a raccoon and
  was shed: his best is now by his own overlap (x4, critters' added, which
  breaks his ties toward a tree a raccoon is behind too), the other by the
  critters' overlap, then his; (3) a critter's pass made before his
  (critter drawn first) left no room for his always-made one: four frames
  over in a row, 30 fps; a pass made while his is still due keeps `HIS_US`
  (a command, two blocks, 100 us) for it; (4) a repeated pass's fetch
  exemption now holds for every pass but not right after a frame that
  went over (`pace::lately()`). Tried and dropped: making his best tree
  over him alone when the union would not fit (never fired), wider OCCL
  images (+16 px a side: critter px missing only 17.3k -> 12.0k as the
  passes stand). `hut_raccoons` before -> after: frames with 8+ px of canopy
  missing 831 -> 42 (a critter's 68 -> 33), squire px missing 27,580 -> 442
  (his shadow), critters' 9,020 -> 6,810; 60 fps with and without `J 500`
  (over 53 / 76 -> 57 / 83). Board knobs: Willow Hollow 7079 -> 6908 us
  avg, the hut with two raccoons 6920 -> 7166 (9 of 120 over, 60 fps), the
  rest within 15 us. What is left is time: in the densest willows two
  passes a frame are what 60 fps affords. Flash: release 50,416 B (16 B
  under 50,432), RAM 17,776; the lean debug image 50,920 B no longer fits
  a save page (config.h: a debug build saves nothing, and warms up at every
  boot, ~21 s); it is 24 B under the 50,944 any image may take.
  The playtest round (the author, 2026-10-06; not yet on the board). Three
  changes, each in the simulator: (1) **The willows' ground.** He could walk
  deep behind a canopy and vanish; now `paint.py`'s `Painter.hide()` (run
  by `terrain()`) makes solid every feet position where the willows whose
  base line is below his feet would cover `HIDE` (half) or more of his
  body (`BODY`, 16 x 21 px round his feet): a sweep by feet row from the
  bottom up, the trees' masks (as placed, so the same in every phase and
  layer) taken in as the row passes above each base, their union box-summed
  over his body. The fronds hang nearly to the ground, so this is most of
  the ground behind a tree: he dips into a canopy's edge, never behind it
  (the scratchpad check over every walkable feet position: at most 73% of
  him under the trees anywhere, 115 positions over 60%, none over 75%).
  Fewer trees have walkable ground behind them: 163 kept of 289 (was 198),
  OCCL 797 blocks a layer (941), 1,145 terrain runs, CRITTERS.DAT 86,711
  blocks (44.4 MB). Scripts that warped him into a canopy's middle (now
  solid: a warped actor stands in the ground and is drawn over trees that
  are no longer occluders) moved to the deepest standable spot behind the
  same trees: `occlude.txt` (lone willow 340,500 and 396,492; the hut
  162,478 with its raccoons 184,470 / 140,470; Willow Hollow 431,158),
  `hidden.txt`, `hut_raccoons.txt`'s walk unchanged, `card_faults.txt`,
  `healed.txt` (720,206), `ambient.txt` (916,400), `world_perf.txt` (84,680;
  636,142), `moods.txt` (604,212: the walk east still crosses into The
  Rot). Board-knob `occlude.txt`: lone 5,448 us avg, Hollow 6,880 (3 of 110
  over), the hut spot with two raccoons 7,745 (26 of 120 over, 60 fps held:
  all three behind three trees at the canopies' top edge, 240 passes, none
  shed; the old spot was 7,166). (2) **The critters come down lanes.** A
  den's critters all came level and straight at him. `chase()` picks a lane
  (`Critter::lane`, +-20 or +-32 px from his feet; the struct is 22 B, +24 B
  RAM) and `approach()` makes for it while the critter is over `LANE_X` (32
  px) off: more than `LANE_OFF` (8 px) off the lane, straight up or down
  onto it first (out of the nest up or down; blocked by the nest or water
  it edges round as before: at the Landing's den, pond south and den north,
  they still come level), then along it, and inside LANE_X in to his row.
  `nests::SPAWN_T` 120 -> 180 (3 s). Turtles (`tether()`), beavers and the
  rot raccoons share `approach()`; chameleons have their own creep. (3)
  **The sword's swipe.** `props.py`'s `swipe()`: one crescent, 21 x 34 px,
  bone with an ochre rim, convex side forward, from over his head to under
  his feet (`CLIP_SWIPE_ARC`, 416 B, a block; 42 clips, 188 stored for 214
  played). `player::plan()` wants it at PRIO_PLAYER whenever the thrust
  clip shows (a fetch once a swing), `draw()` blits it over him on frames
  4-6 (solid ochre every other pair of ticks, frame 6 dithered to half),
  and `blade()`'s box is the arc's: 2..24 px in front, -28..+6 rows round
  his feet (`ARC_*`), so a critter a little above or below is hit too (the
  old blade was 7..19 x -11..-4). Then, at the author's ask: the arc half as
  tall (`SWIPE_SQUASH` 2: 21 x 17 px, cell 32 x 24, pivot (6, 18), 4..20 x
  -17..-2 round his feet; `ARC_Y0/Y1` -18..1) and drawn before him, so his
  sword shows (release 50,372 B, debug 50,928 B; every beat kept). Flash: +124 B as written; cut for it, in
  every build, the card screen's board and the name over the mud (its
  words stand on the speckled mud), and in the lean build the region names
  on the way in, Z's `lost=` (the simulator's card_faults still reads it)
  and `C 4`'s stick carrier (only beaver_fight.txt, simulator, uses it).
  After: release **50,368 B** (64 B under 50,432), RAM 17,800; debug
  **50,924 B** (20 B under 50,944), RAM 17,568. Scripts: `raccoon_fight.txt`
  re-timed (the parry's B at tell + 12, the block beat 56 ticks later for
  the slower den; the trace method below), the reel's raccoon clip
  (`gameplay.txt`: `wait 31` -> `wait 50` before B); the turtle, beaver,
  chameleon, boss and death scripts kept every beat. A tick-by-tick trace
  (`step N` -> N x (`step 1`, `say A`)) shifts the pacer with its printing,
  so its tick counts drift once a frame goes to 30 fps: find a beat with
  it, then confirm with a plain probe script. Board runs to do: P's `stk=`
  with the arc drawn over him (two Prims an item), `occlude.txt`'s hut
  spot, `boss_perf`.
  A pass costs 1 command + 1-3 blocks (4 over a tall critter frame):
  1.27-1.41 ms average, 1.56 ms worst (`occlude.txt` with the board
  knobs). The deepest stack is now a pass's blit (sprites ->
  occluders::after -> card::stream -> sd::stream -> its onBlock ->
  gfx_blit4k), ~160 B deeper than M5's (1,312 B on the board): a static
  estimate from the disassembly keeps it well under 2 KB; re-measure P's
  `stk=` on the board. Debug `O0`/`O1` turn the
  passes off/on; `Z` prints `OCCL passes shed blocks avgUs maxUs`.
  Chameleons creeping as shimmers are left out of the choice (Play's
  `actorList(..., false)`): a pass is wasted on them.
  `out/world/occluders.png` shows what each kept tree redraws. Static items
  (mushrooms, nests) get no passes of their own, so none may stand behind
  a canopy (fixed after M8, above; `test_mushrooms_clear`); one drawn in a
  pass's box before it is covered with the actors.
- Terrain is run-length by rows (`TERRAIN_RUNS`, `TERRAIN_ROW`; the flat
  table only under CHTEST for the host test); `terrain::at` keeps the last
  run per row parity: 0.2-0.3 run steps a lookup in play (3 from the row's
  start). Counted in the simulator (M6 review): `slice_perf` with 12
  critters makes ~60-66 lookups a tick (139 at most, 0.07-0.44 steps
  each), `crowd_perf` up to 23: a few hundred us a tick on the board at
  worst, in the logic, which runs under the flush.

## The cart

The game as a `.chgame` (CHGame's `spec/chgame.md`; the reference tools in
a CHGame checkout's `tools/chcart`, `../CHGame` or `$CHGAME_ROOT`, newer than the vendored subset):
`python EthansCritters/tools/export_cart.py` runs mkcard, `ec build`,
`tools/cart.py`, `chgame export --no-build`, `chgame cart picture` (the
cart's own cover, the visual menu's splash, is the game's box art) and
`chgame cart verify`, and writes `out/EthansCritters.chgame` (14.3 MB:
CRITTERS.DAT deflates 44.4 -> 13.4 MB). `chgame.json` gives id
`ethans-critters`, the title, author, genre, `sdcard: ../out/card` (so the
card file travels in the cart and lands at the card's root), the buttons;
the version comes from config.h, the description from the README's first
paragraph; no screenshots (CHGame's spec dropped them from carts on
2026-10-07: gameplay GIFs stay in the game's project; a `screenshots` key in
chgame.json is now an error, so keep it out). `chgame cart prepare` lays out
`CRITTERS.DAT`, `GAMES/ETHANSCR.CHG` (59,904 B: the image and the picture),
`COVER.PIC`, `MENU.BG`, `MENU.IDX`, `SYSTEM.PIC`. `launch` is not set, so a
card made from it shows the menu (and the cover) at power-on; `chgame cart
launch out/EthansCritters.chgame ethans-critters` would make it boot
straight into the game like a cartridge.

The cover (`docs/cart.png`, 128 x 128) follows CHGame's `docs/cover-art.md`
and the picture rule: 11 own colours plus the menu's cream, grey, black and
red and the rainbow colour. The own colours are the game's palette less
soot, bone, slate and rust, which the fixed four stand in for (`GAME` in
cart.py maps the sixteen indices), so every sprite keeps its pixels: Old
Gullet (the attack frame, mouth open) on the grass on a light green
moss-and-bog checker patch (`PATCH`), the squire's thrust frame with the
swipe's arc behind him, two raccoons, T2 willow fronds hanging in at both
edges (TREE_MAP: a step darker, the light behind them), land reeds and
cattails. artkit paints the far shore, the pond, the glow's reflection,
the mist and the bank (a wandering shoreline at `SHORE` 78 +-4 px with 1-4
px grass blades, `shore()`/`blades()`; flat, undithered patches below
`FLAT_FROM` 108; the bank quantised to greens and black only) and the
vignette; after the quantiser two passes of the recipe's own replace its
dither: `waves()` (wavy 4-row stripes from water to black at the shore)
and `sky()` (each sky pixel a place on the ramp black-umber-peat-bark-
ochre-mud, the glow and vignette folded in, an 8x8 Bayer matrix between
neighbours, fractions under 1/8 or over 7/8 snapped). The toad's eyes are
the rainbow colour (they turn on a Rainbow
bootloader, glow pink on a Static one). The title is the title screen's
own lettering: `title.py`'s `letter()` (the mud blocks, moss, drips) on a
dict canvas, the same two lines at the same rows, through `GAME` (a pixel
font from CHGame's scout, Dank-Depths, was tried first and dropped: the
author wants the cover to match the game). Lint: 11 own colours, rainbow
used, dark edges, 3 doubled tiles (the 2x glyphs, as on the title screen),
~90 lone pixels (the sprites' and the lettering's own details).
Re-run `export_cart.py` after a release build or a card change; the cover
only changes when cart.py or the art does.

**Releases.** The cart is never committed: with the card data it is ~14
MB a version, and git keeps every version (the author asked to keep the
repository small). `tools/release.py` builds a release into the ignored
`out/release/`: the cart, the same laid out by `chgame cart prepare` as a
zip to unzip onto a FAT32 card (`CRITTERS.DAT` at the root, `GAMES/` the
menu's), and `release-notes.md` (download, playing it, credits). It
refuses a dirty tree (a release is a commit; `--allow-dirty` for a dry
run) or an existing tag. `--publish` also needs an `origin` remote and `gh
auth`: it tags `v<ECRT_VERSION>` (config.h) on HEAD, pushes the tag and
runs `gh release create` with the notes and both files. Bump
`ECRT_VERSION` and commit before each release. The READMEs link
`../../releases/latest` (the root) and `../../../releases/latest` (the
game's), which GitHub resolves to the repository's latest release. The
CHGame checkout's tools now append a `[record]` block after the CHG
file's picture (ETHANSCR.CHG 65,607 B; `chgpack verify` ok).
The board package (0.3.0-local) was restaged from CHGame on 2026-10-07
with its LTO in one partition (CHGame's review follow-ups): the same
sources now build a release image of 50,180 B (was 50,372: the ~200 B
this guide estimated for `-flto-partition=one`), RAM 17,800 unchanged.
The simulator does not use the board package, so its frames are as before.

## The device

The board enumerates as USB `16C0:27DD` (a COM port on Windows) with the SD menu
bootloader. Before a device run: say a debug build or script is about to run,
check no other upload/drive process uses the port, never open the port with
bare pyserial (`./ec uploader probe` is safe). After: put the release build
back (`./ec upload`).

## Gotchas (from the CHGame repo's own manual)

- Arduino macros clash with names: `sq`, `map`, `word`, `min`, `max`.
- Flash runs code at ~5 cycles/instruction; hot loops go in SRAM with
  `RAMFUNC(name)` (each costs its size in RAM). newlib's `memmove`/`memcpy`
  are byte loops in flash: write word loops for big copies.
- `double` is software and big; use integers / fixed point. No `snprintf`
  (3.5 KB): `fmtInt`, `fmtStr`.
- Static objects with default member initialisers generate constructor code.
- `pal::` cycles slots 14/15 unless `pal::setCycling(false)`; `pal::setFade`
  is 0..16, CHGfx's `gfx_setFade` is 0..255.
- Sizzle (`chgame/Sizzle.h/.inl`) is compiled in the game's `Fx.cpp`, draws
  in screen space, and its default colours are casino slots: override them.
- `chgame run` drops `$CHSD_CARD` unless `--card`; our chdrive.py supplies
  `out/card/CRITTERS.DAT` by default.
- In the simulator, `gfx_busy()` advances virtual time; the VCard serves a
  plain file as a FAT16 card split into two runs.
- (M8a) A new hand-written source starts with `#include "<rel>/Size.h"`
  (after a `#pragma GCC optimize("Os")` if it has one: an optimize level
  set after it would bring the jump tables back). Mark a small helper
  `ECRT_OUTLINE` only when the size report says it is copied into many
  callers, and never one on a hot path (the card's callbacks, the blits).
- (M8d) An item's draw function (Player, Critter, Nests, Pickups, Rot) is
  called in the world stream's first wait with `scene::rec` set: only its
  `scene::blit()`s are kept (REMAP_RED or REMAP_ROT as the remap), and
  drawn later from the record. Anything else it draws (a fill, a stream)
  must check `scene::rec`, then set `scene::recReal` and `scene::grow()`
  its box instead of drawing; the item is then called again after the
  stream to draw as it does (the chameleon's glint, the toad).
- C++ leaves the order of a call's arguments open: Fx.cpp's `mud()` and
  `chips()` call `rndRange()` several times in one `spawn(...)`, so GCC on
  the board and clang in the simulator may draw the numbers in different
  orders (the particles differ in detail, never in kind). Anything that
  changes how those calls compile can change the board's order too: give
  them locals in order first if they are ever touched.
