# Streaming unique art from the SD card: what Ethan's Critters found

Ethan's Critters is a complete game for the CHGame handheld: title to win,
six nests, a bramble gate, OLD GULLET the giant toad, death and win clips,
a pause map and a best time. It was built to answer a question: how far can
unique art and animation go on this hardware when nothing is tiled and
everything is streamed from the SD card every frame? And what does that
mean for the library and for performance?

This report gives the answer for the library's author: what was streamed,
how, every number measured on the board, the simulator's estimates, the
flash and RAM budgets, what worked and what did not. Above all it covers
what this means for CHGame: the five patches in `chgame/patches/` and
proposals that are not done yet.

**How to read the numbers.** Every number carries one of three labels:

- **Board:** measured on the board, 2026-10-03. That means a CH32X035 at
  48 MHz and a 16 GB SDHC card (maker 65, "SD16G"), running ECBench
  (`bench/ECBench`, results in `bench/results/`) or a debug build of the
  game (its `Z` lines).
- **Sim:** the simulator with the board's card timing
  (`CHSD_SIM_CMD_US=832 CHSD_SIM_BLK_US=222`) and the game's own model of
  the board's drawing time. Where it can be compared with the board it ran
  about 0.5 ms optimistic (-0.2 to +0.9 ms by scene, section 5.1). The
  scripts use `J 500` (0.5 ms of extra work a frame) to stand in for that.
- **Est.:** worked out from the numbers above, or from the disassembly.

Nothing from M8 (the sprites drawn while the world streams, CHGfx P5, the
ambient phases, the healed swamp, the card-fault handling) has run on the
board yet.

## Contents

1. [The answer in short](#1-the-answer-in-short)
2. [What was streamed](#2-what-was-streamed)
3. [The architecture](#3-the-architecture)
4. [Measured on the board](#4-measured-on-the-board)
5. [Simulator estimates](#5-simulator-estimates)
6. [Budgets](#6-budgets)
7. [What worked](#7-what-worked)
8. [What did not, or not yet](#8-what-did-not-or-not-yet)
9. [What it means for the CHGame library](#9-what-it-means-for-the-chgame-library)
10. [Upstreaming](#10-upstreaming)
11. [Board runs still to do](#11-board-runs-still-to-do)

## 1. The answer in short

- **Unique art goes very far, because card space is free.** What the
  player sees includes:
  - eight unrepeated 1024x768 paintings of the swamp: 4 ambient phases,
    times The Rot dead and healed;
  - 187 sprite frames;
  - a 60x55 px boss in 60 pictures;
  - 163 willows the squire can walk behind (198 before the willows' ground
    was made solid where they would hide half of him);
  - 51 full-screen clip frames.

  That comes to 44.5 MB on the card, 874 times the 50,944 B a game image
  may take. Flash could not hold a single one of the paintings (384 KB
  each).
- **Time on the shared SPI bus is the limit, not space.**
  - At 60 fps the 12 bpp flush holds SPI1 for 8.41 ms. That leaves about
    8.2 ms after `gfx_wait()` for the card and all the drawing.
  - A card command costs 832 us, as much as 3.75 blocks, and each block
    costs 222 us (board).
  - The world under the camera takes one command and 16-17 blocks: 4.6 ms.
    What remains, about 3.5 ms, pays for two or three more commands with
    a few blocks each, shared by the sprite cache, the willows and the
    toad.

  So the art is laid out to turn each frame's needs into a few long reads,
  and card space is spent freely to get there. Every world pixel is stored
  about 14 times over so that any view is one read.
- **The card is reliable, apart from one stall.**
  - 34,000 streamed blocks gave no errors, so no CRC is needed.
  - Never-read data after power-up came back in at most 1.7 ms.
  - The first read after the card was written stalled once, for about
    1.05 s. P2's stream timeout and `sd::recover()` (66 us, against 4.1 ms
    for `init()`) turn a stall into a lost frame.
  - A one-off read of the whole file on first boot keeps the stall out of
    play.
- **On the board almost all play runs at 60 fps** (M8, measured
  2026-10-04).
  - Typical frames take 5.4-7.2 ms after `gfx_wait()`.
  - Willow Hollow (7.4 ms and 30 fps in M6) now holds 60 fps at 6.9 ms,
    and so do twelve critters (8.8 ms and 30 fps in M5; now 7.7 ms).
  - One scene still drops to 30 fps while logic stays at 60 Hz: the toad's
    angry phase (7.45 ms average, about 1 frame in 5 over the 7.9 ms
    budget, with two remapped rot raccoons and the toad's own stream).
  - What got it there, on the board: P5's remap at 237 us instead of 455
    for 32x32, the M8 flash diet's smaller code, and the sprites drawn
    while the world's blocks arrive (0.2-0.4 ms; its per-callback budget
    makes no difference between 60 and 150 us).
- **Flash became the binding limit, not RAM.**
  - By M7 the release image was within 1 KB of the 50,432 B that keeps
    both save pages. About 6 KB of diets made room.
  - Debug builds have to drop about 6 KB of features to upload at all.
  - The release image is now 49,796 B (636 B left). RAM is 17,740 B of
    18,416.
- **What it means for the library: five additive patches are ready to go
  upstream.** Together they keep the three SD games byte-identical.
  - P1: `fat::stream()`.
  - P2: the stream timeout and `recover()`.
  - P3: the simulator's bus-rule check and card timing.
  - P4: `gfx_blit4k()`.
  - P5: `blit4k`'s fast remap.

  The next steps are proposals with expected gains (section 9.2):
  - a reusable streaming module for chgame;
  - a wait hook in `sd::stream()` so the 832 us of command latency can be
    used for drawing;
  - a look at CHSd's 51 us of overhead per block;
  - a CPU-cost model in chsim;
  - a clip rectangle for `blit4k` only;
  - more RAM for the frame cache;
  - the panel's TE pin.

## 2. What was streamed

**CRITTERS.DAT**, 86,711 blocks or 44,396,032 B (`src/assets/CardIndex.h`; 86,998 blocks before the willows' ground was made solid, which pruned 35 trees from OCCL):

| Section | What | Blocks | MB | Pictures |
|---|---|---:|---:|---|
| WRLD | The world: one 1024x768 painted bitmap, in 4 ambient phases x 2 layers (layer 1 is The Rot healed). Stored as column blocks in bands, below | 83,969 | 43.0 | 8 paintings, each the area of 48 screens |
| OCCL | The willows an actor can walk behind, as the world shows them, in row bands, a copy for each layer | 1,595 | 0.82 | 163 trees x 2 |
| BANK | Sprite frames: 10 sheets, 42 clips, raw 4 bpp words trimmed to their box | 100 | 0.05 | 188 stored for 214 played |
| TOAD | OLD GULLET, about 60x55 px, in row bands, each picture again in red for the hit flash | 226 | 0.12 | 30 pictures (and 30 red) for 44 played frames |
| TITL | The title loop | 257 | 0.13 | 16 full-screen frames |
| DEAD | The death clip, THE SWAMP KEEPS YOU | 225 | 0.12 | 14 |
| WINC | The win clip, dawn over the healed pond | 321 | 0.16 | 20 |
| PMAP | The pause map; the fog is drawn over it | 17 | 0.01 | 1 |
| Header | Magic, format, build hash, section table with CRCs | 1 | | |

**The ambient phases.** Each phase repaints the whole swamp:
- the reeds' tops lean in a wave across the beds;
- the shallows' ripples sway;
- the deep water swells;
- the shore's glint laps;
- sun flecks blink;
- the lily pads dip;
- gnats buzz over The Rot.

The willows stay still.

**The healed layer** is The Rot alive again: no dead willows, reeds or
grass, no grime, no gnats, and its pond in flower. It differs from layer 0
only in The Rot's corner.

**Against flash:**

| | Bytes | Compared with a game image (50,944 B) |
|---|---:|---:|
| One full-screen 4 bpp frame | 8,192 | 16% |
| The 51 clip frames | 417,792 | 8.2x |
| One 1024x768 painting, raw 4 bpp | 393,216 | 7.7x |
| The 8 paintings, raw | 3,145,728 | 62x |
| CRITTERS.DAT | 44,396,032 | 871x |

The image holds the code and its tables. The largest tables are the
terrain's run-length rows (1,104 B), the willows' list (594 B) and the
sprite clip table (410 B).

## 3. The architecture

### 3.1 The frame loop and its budget

```
loop():  up to 3 logic ticks at 60 Hz        (they run while the last flush is on the wire)
         plan: draw-list wants, the frame cache's budget, the willows' choice
         gfx_wait()                          (from here to the flush, SPI1 and gfx_chunkScratch() are ours)
         spool::fetch()                      missing sprite frames: 0-2 commands
         world::draw(cx, cy, layer, phase, idle)
                                             1 command, 16-17 blocks; sprites drawn in its waits (M8)
         held sprites + willow passes        1 command and 1-3 blocks each, at most 2
         the toad (Path D)                   1 command, 3-6 blocks, during the boss fight
         particles, banners, HUD
         gfx_flushAsync()                    8.41 ms on the wire
```

| At 60 fps | ms | Source |
|---|---:|---|
| The frame | 16.67 | |
| The 12 bpp flush on the wire, and its start | 8.41 + 0.10 | board, B5 |
| Left after `gfx_wait()` | 8.16 | est. |
| The pacer's budget (`pace::BUDGET_US`) | 7.9 | design |
| The world window: 1 command + 17 blocks | 4.6 (p50 4.4, max 5.5) | board, B2 and B4 |
| A frame-cache fetch: 1 command + 2 blocks | 1.3 | est. from B2 |
| A willow pass: 1 command + 1-3 blocks | ~1.0 (sim said 1.4) | board, M6 |
| The toad: 1 command + 3-6 blocks | 1.5-2.2 | est. |
| A Path F clip frame: 1 command + 16 blocks | 4.0-4.2 | board, title |

### 3.2 The card file and the bus rule

- **Layout.** CRITTERS.DAT is block-aligned sections behind a header
  (`tools/card/cardfile.py`). Block 0 holds the magic, the format, a build
  hash that covers every section, and a CRC for each section. The game
  compiles in the hash (`CardIndex.h`), so a card from another build says
  WRONG CARD DATA.
- **Fragmentation.** The file may be in up to 8 FAT runs. A read crosses
  runs with one command per run. The game does this in its own
  `card::stream()` (`src/engine/Card.cpp`, CHStlView's run splitting,
  which also counts the commands for `Z`); patch P1 lifts the same loop
  into CHSd as `fat::stream()`, which ECBench reads through. The simulator
  always serves the file in two runs, so that path is exercised.
- **The bus rule.** All card access sits between `gfx_wait()` and the next
  flush, with `gfx_chunkScratch()` (1 KB) as the two 512 B landing
  buffers. The simulator reports a BUG on any card call while a flush
  holds SPI1 (patch P3).

### 3.3 The world (`src/engine/World.cpp`)

- **Column blocks.** The world is stored as column blocks 8 px wide and
  128 rows tall. Each is 512 B: 4 B a row, exactly as `gfx_fb` holds 8
  pixels.
- **Bands.** A band starts every 8 rows and is 128 rows tall, so any
  camera's 120 visible rows lie inside one band. A band's 128 columns are
  consecutive blocks, so a frame's world is one command of 16 blocks, or
  17 when x % 8 is not 0. The cost is about 14x redundancy: 82 bands of
  128 rows for a 768-row world.
- **Address.** `lba = first + 1 + ((band * LAYERS + layer) * PHASES + phase) * 128 + column`.
- **The copy.** Each block goes into its framebuffer column, shifted by
  nibbles for x % 8, while the next block arrives by DMA. It is a RAM
  function of 208 B. On the board it takes 26 us aligned and 51.5 us
  shifted, inside the 222 us a block takes, so the copy costs nothing.
- **Phases and layers.** These are free at run time: the same window is
  read from another phase's or layer's blocks. M8c cost +64 B of flash and
  no RAM; every perf script read the same number of blocks per frame.
- **Collision** never touches the card. The terrain is in flash as
  run-length rows of 8 px cells (1,104 B of runs plus a 192 B row index).

### 3.4 The sprite frame cache (`src/engine/Spool.cpp`)

- **The arena.** 3 KB of RAM (2.5 KB in the lean debug build) with a
  32-entry directory.
- **Wants.** Before `gfx_wait()` every actor calls
  `want(clip, frame, priority)`. The priorities, in order: the player,
  attackers, critters, pickups, prefetch.
- **Fetching.** `fetch()` reads the misses in at most two commands,
  reading through gaps of up to 3 blocks to join them. The frames that
  share a block with a wanted one come along while there is room.
- **Eviction.** It never evicts a frame wanted on this screen; otherwise
  the least recently used goes. Compaction (a word loop) is the last
  resort.
- **Format.** A frame is raw 4 bpp words with an 8 B header, which is
  `gfx_blit4k()`'s format (P4).
- **The budget.** Play's `budget()` sets what a fetch may read from what
  the last frame's work left. Right after a frame that went over, nothing
  is fetched and the actors show their last frame a frame longer. At most
  one frame in three goes over, so the pacer holds 60 fps through a fight.

### 3.5 Path D: blitting straight from the landing buffer

Some art is never held in RAM. Each block is drawn from the landing buffer
as it arrives.

- **The toad** (`src/game/Rot.cpp`):
  - Each frame is stored in row bands, `128 / wWords` whole rows a block.
    One command reads the blocks with rows on the playfield, and each is
    blitted with `gfx_blit4k()`.
  - The red hit flash is a second copy on the card. A remapped blit of
    that size ran about 1.1 ms past the DMA (sim, with P4's numbers).
- **The willows** (`src/engine/Occluders.cpp`):
  - The rows of the tree that the actor overlaps go over him, at the
    tree's full width.
  - Each pass crops to the overlap inside the landing buffer. CHGfx's
    variable clip would have cost every CHGfx call about 250 B of flash
    and 70 B of RAM functions (section 9.2, B).

### 3.6 Path F: full-screen clips (`src/engine/PathF.cpp`)

- **Format.** A header block holds the frame count, the frame time and a
  16-colour palette. Each frame is then 16 blocks laid out exactly as
  `gfx_fb`.
- **Cost.** A frame is one command and one word copy per block, done
  under the DMA: 4.38 ms of card time (est.), 4.03-4.18 ms on the board
  for the title.
- **Rendering.** `tools/card/clips.py` renders the clips offline from the
  painted world, the sprites and the toad, with stepped light, mist and
  dithered edges. Each clip's palette is then fitted in CIELAB.

### 3.7 The willows (occluders)

- **Behind.** An actor whose feet are above a tree's base line is behind
  it. Right after he is drawn, a pass streams the tree's rows over him.
- **Limits.** At most two passes a frame: the squire's best tree, then the
  critter nearest him. All but the squire's best are shed when the frame
  has no time left.
- **Data.** 163 trees, stored once per layer.
- **Known gap.** Static items get no pass. 24 of the 48 mushrooms lie
  under canopies and show over them (world data still to fix).

### 3.8 The sprites drawn while the world streams (M8, `src/game/Play.cpp`)

Of each block's 222 us (board), DMA takes about 171 us and the copy only
26-51 us. That leaves most of the time idle. `world::draw()` now calls an
`idle(words)` hook after each copy, telling it how many screen words (8 px
each) hold their final pixels.

- **The first wait** builds the frame's draw list and records each item's
  blits as 12-byte `scene::Prim`s, up to 20 a frame, instead of drawing
  them.
- **Ordering.** Each draw (an item's shadow, the toad's broad shadow, the
  item) waits for two things:
  - the screen words it needs to be final;
  - every earlier draw that touches the same 8x16 px cell.

  So the picture is the old one to the pixel.
- **Drawing.** Each later wait draws from the queue while it has
  `ECRT_HIDE_US` (150 us) left. The last block has no DMA under it, so
  nothing is drawn in its wait.
- **Held back** to be drawn after the stream, together with anything later
  in their cells:
  - the toad, which streams;
  - an actor with a willow pass planned over him;
  - a chameleon showing its glint, which is not a blit;
  - an item whose blits do not fit the 20-entry record.

  A draw that touches the rightmost screen word also ends up after the
  stream, because that word is final only after the last block.
- **The check.** `CHSIM_FLAGS=-DECRT_HIDE_VERIFY=1` charges each early
  draw in full at its old place. With it, all 288 images of all 30
  simulator scripts (as they were in M8d) match the pre-M8d baseline, at
  `ECRT_HIDE_US` 150, 1 and 100000.

### 3.9 Pacing (`src/engine/Pace.h`)

- **Logic** always ticks at 60 Hz, with up to 3 catch-up ticks before a
  draw.
- **Down to 30.** The screen is drawn every tick until 4 of the last 8
  frames take over 7.9 ms after `gfx_wait()`. Then it is drawn every
  second tick (30 fps, about 25 ms of room).
- **Back to 60** after 60 drawn frames that averaged under 6.9 ms with at
  most one over.
- **What is shed first:** willow passes other than the squire's best, then
  the frame cache's reads, the squire's last. Right after a frame that went
  over, nothing is fetched and every actor shows the frame it showed. The
  world stream and the toad are never shed.
- **Why ticks, not `setFrameRate(30)`:** the START hold and `repeat()`
  keep their timing.

### 3.10 A card that stalls, and the warm-up (`src/engine/Card.h`, `EthansCritters.ino`)

- **One gateway.** Every card read goes through `card::stream()`.
- **Timeouts.** A stream waits for the card for 1 s at boot and 32 ms in
  play (`sd::setStreamTimeout()`, P2). The 32 ms is six times the board's
  slowest whole 17-block stream.
- **A failed stream** is recovered with `sd::recover()` (P2), and that
  frame is not flushed: the panel keeps the last picture.
  - After 2 lost frames in a row the logic waits too, so nothing happens
    unseen.
  - After 40 (more than the board's 1.05 s stall), or a `recover()` that
    fails, the card screen shows.
- **The warm-up.** The first boot with new card data reads the whole file
  once, in 512-block streams with the long timeout, behind STIRRING THE
  SWAMP and a bar. The file's hash goes in the save, so later boots skip
  it. For 44.5 MB that takes about 21 s (sim).

### 3.11 The simulator as the main tool

- **Card timing.** P3 gives the simulator's card the board's timing, plus
  jitter, failures and slow first reads.
- **Drawing time.** The game charges its own estimate of the board's
  drawing time to each frame: `scene::blitCost()`, `fx::simCost()` and
  constants for the HUD. So the fetch budget, the 30 fps switch and `Z`'s
  frame times come out as the board's, estimated.
- **Determinism.** Every change is checked by comparing all the images of
  all the scripts.

## 4. Measured on the board

### 4.1 ECBench (`bench/results/2026-10-03-board-summary.txt`, debug build)

**B1, open.** A FAT32 SDHC card (SD16G, 14,916 MB) and a 32 MB test file
in one run.

| Step | us (twice) |
|---|---|
| `sd::init()` | 4,120 / 4,118 |
| `fat::mount()` | 3,724 / 3,421 |
| find + runs | 25,543 / 25,562 |
| Header block | 2,192 / 2,196 |

**B2, `sd::stream()`**, 200 iterations for each n:

| n blocks | random p50 | p95 | max | warm 128 KB p50 | p95 | max |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1,159 | 1,388 | 1,419 | 864 | 895 | 1,365 |
| 2 | 1,379 | 1,610 | 1,613 | 1,087 | 1,117 | 1,118 |
| 4 | 1,537 | 2,031 | 2,057 | 1,527 | 1,561 | 1,563 |
| 8 | 2,666 | 2,939 | 2,944 | 2,417 | 2,449 | 2,450 |
| 17 | 4,417 | 4,916 | 4,964 | 4,406 | 4,443 | 4,447 |
| 32 | 8,024 | 8,251 | 8,270 | 7,727 | 7,771 | 7,775 |

The fit is **832 us per command (random offset; 644 warm) + 221.7 us per
block**, with no errors. A 512 B block at 24 MHz is 170.7 us on the wire,
so about 51 us a block (23%) is overhead.

**B3, cold first reads after power-up.** One block in each of 32 regions
of 1 MB, done twice:
- p50 899 us, p95 1,399, max 1,425 (regions 1, 5, 9, 13, 17 and 21
  about 1.4 ms, both times);
- none over 10 ms, none failed.

No warm-up is needed for data that is merely unread.

**B4, soak.** 2,000 frames of 17-block streams interleaved with flushes,
34,000 blocks in all:
- 0 bad blocks, 0 failed streams;
- 4,570 us avg, 5,524 max.

No CRC is needed.

**B5, drawing**, us per call:

| What | us |
|---|---:|
| `gfx_blit` 32x32 transparent, even x / odd x | 226.1 / 442.2 |
| `sprite4` 24x21, plain / remapped / mirrored | 173.9 / 175.5 / 173.1 |
| `gfx_remapRect` 24x8 | 50.5 |
| **`gfx_blit4k` 32x32 (P4)**: even x, odd x, mirrored, dithered, remapped | **149.3, 169.9, 237.0, 180.2, 454.7** |
| Column copy into fb, x % 8 == 0 / shifted | 26.1 / 51.5 |
| A full screen of column copies, aligned (16) / shifted (17) | 420.1 / 844.1 |
| A full-screen 16-block word copy | 229.4 |
| Flush start / 12 bpp flush on the wire | 102.0 / 8,412.0 |

The column-copy check found 0 bad pixels. P5's cases (`blit4k32_rmflip`,
`_solid`, `blit4k24_odd`, `_remap`) are in ECBench but have not been run.

**B8, faults:**
- 32 never-read blocks with a 5 ms timeout: none timed out; to data p50
  1,150 us, max 1,683.
- 20 forced timeouts: all 20 recovered without `init()` and re-read
  correctly.
- `recover()` 66 us (max 67, 63 on an idle card); `init()` 4,118 us.

### 4.2 The game (debug builds, `Z` lines; CLAUDE.md's table)

These are lean debug builds (`ECRT_LEAN`, section 6.3): features left out
to fit, and a 2.5 KB frame cache where release has 3 KB.

| Scene | Build | After `gfx_wait()` | Rate |
|---|---|---|---|
| The play loop: world 17 blocks + squire + HUD | M4 | 4.9 ms avg / 5.9 max, 0 frames over | 60 fps |
| 8 critters + nests + particles (`slice_perf`) | M5 | 7.1 avg / 8.0 max, 1 frame over; cache deferred 362 of 660; stack 1,312 B | 60 fps |
| 12 critters (stress) | M5 | 8.8 avg / 10.3 max | 30 fps (pacer) |
| The same 8 critters again | M6 | 6.35 avg | 60 fps |
| Lone willow / the hut's willows | M6 | 6.0 / 7.3 avg; a willow pass ~1.0 ms (sim said 1.4) | 60 fps |
| Willow Hollow | M6 | 7.4 avg / 8.5 max | **30 fps** |
| Crowds of 6 / 8 / 10 beavers and chameleons | M6 | 6.7 / 7.0 / 7.4 avg | 60 fps |
| Title loop (16 blocks a frame) | M1 / M7 | 4.03 / 4.18 | 60 fps |
| Boss near / fighting / angry | M7 | 6.5 / 6.8 / 7.3 avg; stack 1,332 B | angry **dips to 30** (42 of 445 frames) |
| The first read after a card write | M7 | **one stall of about 1.05 s**, the first fight after `card_sync` wrote the file; gone on the re-run | (froze for it before M8a) |
| The play walk (`play_perf`) | M8 | 5.4 avg / 7.3 max (more on screen than M4's walk) | 60 fps |
| Crowds of 6 / 8 / 10 | M8 | 6.4 / 6.8 / 7.2 avg; 10: 10 frames over | 60 fps |
| 8 / 12 critters (`slice_perf`) | M8 | 6.75 / 7.74 avg; 12: 146 of 440 over, never 4 of 8 | **60 fps** (12 was 30 in M5) |
| Lone willow / Willow Hollow / the hut's willows | M8 | 5.95 / 6.92 / 7.1 avg; Hollow 0 frames over | Hollow **60 fps** (30 in M6) |
| Boss intro / near / fighting / angry | M8 | 7.4 / 6.5 / 7.0 / 7.45 avg; stack 1,544 B | angry still **30 fps** (152 of 335 frames) |
| The same with the sprites drawn after the stream (`ECRT_HIDE_US 0`) | M8 | Hollow 7.08, fighting 7.23, angry 7.82 avg | angry 30 fps (225 of 263) |
| `ECRT_HIDE_US` 60 / 100 / 150 | M8 | identical within 0.03 ms in every scene | |
| The warm-up of the 44.5 MB card, debug build | M8 | not timed: the first `boss_perf` after the copy stopped during it (the script's handshake gave up, as section 8 warns); the second run went straight to play, so the warm-up had finished and its mark was saved | |

ECBench B5 with P5 (32x32 sprites, debug build; P4 in brackets):
`blit4k` even x 133.6 us (149.3), odd 153.8 (169.9), flipped 193.4 (237.0),
remapped **237.5 (454.7)**, dithered 165.8 (180.2), remapped and flipped
237.1, solid ink 238.0; a 24x21 critter frame 88.1 us plain, 138.6 remapped
(`bench/results/2026-10-04-board-b5-p5.txt`).

### 4.3 Not measured on the board yet

- (Measured on 2026-10-04 and moved to 4.2: P5's blit costs, M8's
  sprites drawn while the world streams, the stack since M8 (`stk=` 1,544
  B), the budget per callback.)
- The warm-up's real time (the debug run that did it lost its handshake).
- Whether the 1.05 s stall comes once per card write or once per 1 MB
  region.
- Whether `recover()` on a card that is still settling fresh data returns
  within 32 ms.
- B6, B7 and B9 from the plan: the auto-pan histogram, a stress run, and
  misses per second in a replay.

## 5. Simulator estimates

All in this section are **sim** unless marked otherwise. The card is set
to the board's timing and the drawing model uses P4's B5 numbers.

### 5.1 How close the simulator is to the board

| Scene | Sim | Board | Board minus sim |
|---|---:|---:|---:|
| Crowds 6 / 8 / 10 (sim M6b, board M7 build) | 6.05 / 6.21 / 6.47 | 6.7 / 7.0 / 7.4 | +0.65 / +0.79 / +0.93 |
| Willow Hollow (M6) | 7.04 | 7.4 | +0.36 |
| 8 critters (M6) | 6.54 | 6.35 | -0.19 |
| Boss near / fighting / angry (M7) | 6.68 / 6.87 / 7.31 | 6.5 / 6.8 / 7.3 | -0.18 / -0.07 / -0.01 |

The error runs from -0.2 to +0.9 ms. In the crowd runs it grows with the
number of actors, which points at per-actor CPU work the model does not
charge, as distinct from the blits it does charge (section 9.2, E). In
the 8-critter slice and the boss fight, though, the board was slightly
faster than the simulator.

The board's runs were lean debug builds (section 6.3), whose frame cache
is 2.5 KB against the simulator's 3 KB (`config.h`'s `ECRT_ARENA`). That
does not explain the gap: with a 2.5 KB cache the final build's simulator
frames move by only -0.05 to +0.09 ms (section 5.4, the 2,560 row).

### 5.2 M8: the sprites drawn while the world streams (avg us after `gfx_wait()`)

| Scene | Before | After | After + `J 500` | P5 model + `J 500` |
|---|---|---|---|---|
| Willow Hollow | 7,042 | 6,987 (drawing est. 737 -> 547) | 60 fps, 7 frames over | 60 fps |
| The hut's willows | 6,744 | 7,139 (the saved time buys passes: 170 made / 30 shed, was 121 / 79) | 60 fps, 8 over | 60 fps |
| Crowds 6 / 8 / 10 | 6,050 / 6,196 / 6,468 | 5,594 / 5,694 / 6,133 | 60 fps | 60 fps |
| 8 / 12 critters | 6,543 / 7,110 | 5,988 / 6,457 | 60 fps (12: 2 over, was 43) | 60 fps |
| Boss intro / near / fighting / angry | 6,765 / 6,682 / 6,872 / 7,314 | 6,611 / 6,522 / 6,747 / 7,086 (angry 46 over, was 62) | angry at 30 fps (152 of 336 frames) | **angry holds 60** (7,445 avg, 0 switches) |

- **What stays after the stream:**
  - draws touching the rightmost screen word (about 2 a frame with twelve
    critters);
  - the toad and what overlaps it in front (about 2.4 a frame in the
    angry phase);
  - glinting chameleons.
- **Not modelled:** the list build in the first wait, which is the old
  `sprites()` work plus about 100-200 us. Only about 150 us of it hides
  in that wait.
- **Sensitivity:** `ECRT_HIDE_US` 120 instead of 150 costs 10-90 us.

### 5.3 CHGfx P5 (est., from the disassembly and a cycle model fitted to B5)

| `gfx_blit4k` 32x32 | P4 (board) | P5 (est.) |
|---|---:|---:|
| Remapped | 455 | ~235 |
| Mirrored | 237 | ~195 |
| Plain, x % 8 == 0 / 3 | 149 / 170 | ~134 / ~153 |

With P5's costs in the model (`-DECRT_SIM_P5=1`), M8b's build (before the
sprites were drawn under the stream) averaged 7.16 ms in the angry phase
and held 60 fps with `J 250`; with P4's costs it went to 30. The final
build with P5's costs averages 6.96 ms there and holds 60 fps with `J 500`
too (section 5.2).

### 5.4 What more RAM for the frame cache would buy (new for this report)

These runs used `CHSIM_FLAGS=-DECRT_ARENA=N`, the board's card timing and
no `J`. Each cell is the average us after `gfx_wait()`, with misses
deferred to a later frame in brackets.

| Arena | 8 critters | 12 critters | 12 standing | Crowd 6 | Crowd 8 | Crowd 10 | Boss angry (frames over) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 2,048 | 5,954 (837) | 5,980 (2,099) | 5,687 (689) | 5,711 (94) | 5,736 (274) | 5,877 (758) | 7,119 (61) |
| 2,560 (lean debug: the board's perf runs) | 6,051 (55) | 6,404 (661) | 6,308 (212) | 5,682 (7) | 5,783 (15) | 6,206 (118) | 7,110 (52) |
| **3,072** (release) | 5,988 (37) | 6,457 (318) | 6,359 (50) | 5,594 (3) | 5,694 (11) | 6,133 (59) | 7,086 (46) |
| 4,096 | 5,851 (23) | 6,340 (213) | 6,122 (18) | 5,467 (2) | 5,515 (5) | 5,975 (42) | 7,058 (37) |
| 5,120 | 5,698 (13) | 6,233 (135) | 5,813 (11) | 5,297 (1) | 5,299 (2) | 5,817 (26) | 6,999 (18) |

- **Each extra KB** saves about 100-300 us a frame in a crowd and makes
  animation smoother (fewer deferred frames). In the boss fight the toad
  is not cached, so it saves less.
- **A 2 KB cache** is cheap only because it gives up: actors keep showing
  stale frames (2,099 deferred with twelve critters).

### 5.5 The warm-up, and a card that stalls

The warm-up reads the whole card in the simulator with the board's timing:

| File | Time | Rate |
|---|---:|---:|
| 6.4 MB (before M8c) | 3.0 s | 2.14 MB/s, 93% of the 2.3 MB/s a 222 us block allows |
| 44.5 MB (now) | 20.9 s | 2.13 MB/s |

With every 1 MB region slow on its first read (`CHSD_SIM_COLD_US`):

| Slow first read | Warm-up time |
|---:|---:|
| 300 ms | 33.5 s |
| 950 ms | 60.8 s |
| 1,050 ms | 66.9 s (42 streams over the 1 s timeout, all recovered) |

`tools/tests/sim_card_faults.py` runs `card_faults.txt` with every
seventh stream failing (`CHSD_SIM_FAIL_EVERY=7`) and 2 ms of jitter
(`CHSD_SIM_JITTER_US=2000`). The game lost 15, 33, 19 and 32 frames in
its four parts and played through (`ec check`, M8 final).

## 6. Budgets

### 6.1 Flash and RAM by milestone

Each image is limited to 50,944 B in all; two save pages need it at
50,432 B or less, and one at 50,688 B or less. RAM is 18,416 B of static
RAM plus a 2 KB stack.

| Milestone | Release image | Release RAM | Debug image |
|---|---:|---:|---:|
| M0 stub | 8,180 | 11,600 | |
| M1b: the card and the title clip | 12,724 | 11,776 | |
| M3+M4: the world, the frame cache, the squire, the HUD | 27,016 | 16,316 | 32,628 |
| M5a: sounds, Sizzle, remaps | 31,320 | 16,604 | |
| M5b and its reviews: critters, combat, nests | 40,216 | 17,272 | 46,752 |
| M6a: the willows; terrain run-length (-1,774 B) | 40,412 | 17,312 | 43,312 (lean build introduced) |
| M6b and its reviews: beavers, chameleons, 6 nests, moods | 44,224 | 17,360 | 47,244 |
| M7a: the gate and the toad | 48,400 | 17,424 | 50,540 |
| M7b and its review: clips, pause map, best time | 49,604 | 17,456 | 50,536 |
| M8b alone: CHGfx P5 | 49,704 | 17,552 | 50,644 |
| M8a alone: stalls, warm-up, a diet of 1,620 B | 48,732 | 17,432 | 50,228 |
| M8a + M8b + M8c merged | 48,908 | 17,528 | 50,400 |
| **M8 final**: sprites drawn under the stream | **49,796** (636 B under 50,432) | **17,740** (676 B free) | **50,536** (RAM 17,504; one save page) |
| After the occluder fixes (three rounds) | 50,416 (16 B under) | 17,776 | 50,920 (no save page: a debug build warms up at every boot) |
| The playtest round: the sword's swipe arc (a card clip, drawn over him and the blade's reach), critters down lanes, the willows' ground solid; the card screen's board and the boot name cut | 50,368 (64 B under) | 17,800 | 50,924 (20 B under 50,944) |

Against the plan's budget:
- **Flash.** The plan expected an image of about 38.9 KB, with 11.5 KB
  spare. The image came in about 11 KB over that, with 636 B spare.
- **RAM.** RAM landed where it was planned, about 17.9 KB.

### 6.2 RAM map (release, from the ELF)

| What | B |
|---|---:|
| `gfx_fb` (128x128 at 4 bpp) | 8,192 |
| The frame cache's arena | 3,072 |
| CHGfx's 12 bpp LUT / chunk buffers (the card's landing buffers) | 1,024 / 1,024 |
| RAM functions: `blit4k` rows 632, `remapRect` 328, `convertSpan` 314, the save's page write 238, the world's column copy 208, `hline` 202, glyphs and text 246 | 2,168 |
| The cache's directory 256 and wants 96 | 352 |
| Critter pool 240, particles 240 + floats 48 | 528 |
| The shared draw list (M8d) | 172 |
| The rest: game state, core, USB, CHGame | about 1,200 |

### 6.3 Where the flash went and came back

**The diets**, each measured on its own build:
- terrain as run-length rows (-1,774 B);
- the waiting and posing states as tables (-1.4 KB and -0.65 KB);
- the birds and the ball dropped (a stretch goal);
- the death and win screens' text replaced by clips;
- M8a's -1,620 B:
  - stats only in debug builds -248;
  - `panel()` for hand-drawn boards -228;
  - outlining small helpers that LTO copied into every caller -224;
  - no jump tables -196;
  - no loop-invariant hoisting -212;
  - and smaller items;
- M8d's outlining (-264 B) and a smaller clip table (-56 B).

**Measurement noise.** Since M7a, GCC's LTO splits the image into two
partitions, and sizes jump ±100-200 B on unrelated changes. Moving the
swamp's drawing into a function of its own once cost +216 B.
`-flto-partition=one` would save about 200 B, but the tools cannot pass a
link flag.

**The debug image is the tighter limit.** A debug build adds about 7 KB
(est.: its image is 740 B over release with about 6.4 KB left out; USB
serial and the debug protocol are about 2.7 KB of it by `config.h`, the
rest the game's debug commands and frame stats), so `ECRT_LEAN` (debug
builds on the board) drops about 6.4 KB:
- the sounds and CHGame's sequencer;
- the core's `delayMicroseconds()`;
- the SELECT overlay;
- the pause map and the title's choice;
- the HUD compass;
- the card screen's board;
- a few debug reports.

Since M8d the debug image must also stay at or under 50,688 B to keep one
save page. Without it the warm-up's mark is lost, and every debug boot
would re-read 44.5 MB.

## 7. What worked

- **One long read a frame for the world.** The band layout makes any view
  one command of 16-17 blocks (4.4 ms p50 on the board), and the copy
  hides under the DMA. Spending 14x the card space for it was the right
  trade.
- **Card space as the free resource.**
  - Four ambient phases and a healed layer cost nothing per frame (the
    same blocks) and 64 B of flash. The whole swamp moves at no cost.
  - The toad's red flash as a second copy, not a remap, saved about 1 ms
    on every hit frame (sim).
- **The card is solid.** No errors in 34,000 blocks; no slow cold reads;
  every forced timeout recovered in 66 us without `init()`.
- **`gfx_blit4k()`.** 149 us for 32x32 against `gfx_blit`'s 226 and 442,
  at any x, with flip, remap, ink and dither. Every sprite, the toad and
  the willows use it.
- **The frame cache with priorities and a budget.** Fights stay at 60 fps
  on the board with 8 critters, nests and particles. A missing frame
  costs a frame of animation, not a frame of time.
- **Path F clips.** 51 full-screen frames rendered offline with light,
  mist and their own palettes, at 4 ms of card time each: cutscenes and a
  title loop that flash could never hold. They animate at 8 frames a
  second (125 ms a frame), and the screen is still drawn at 60 fps, each
  frame read again from the card with the words drawn over it.
- **Pacing in ticks.** Logic at 60 Hz always, drawing at 60 or 30. The
  START hold and repeats keep their timing.
- **Stalls handled as lost frames** (P2 and `card::stream()`). The board's
  1.05 s first-read stall froze the game before M8a; now it costs held
  frames in play, or a progress bar at first boot.
- **The simulator.** With the board's card timing and a drawing model it
  predicted board frame times within about 0.5 ms in most scenes (0.9 ms
  in the biggest crowd). That was enough to set every budget before the
  board saw a build. Deterministic image checks (291 images in 30
  scripts in the final build, contact sheets aside) made each refactor
  and flash diet safe. P3's
  fault knobs exercised every recovery path.

## 8. What did not, or not yet

- **The busiest scenes went to 30 fps on the board:** Willow Hollow
  (7.4 ms), the angry boss (7.3 ms, dips) and twelve critters (8.8 ms).
  M8's sprites drawn under the stream fix the first two in the simulator
  (section 5.2); the board has to confirm it.
- **The command cost dominates the extras.** Each willow pass, toad
  frame or cache miss is a separate command, about 1 ms each. That is why
  there are at most two passes, the passes are shed first, and 24 of the
  48 mushrooms show over the canopies.
- **Flash pressure.**
  - From M7 on, every feature had to be paid for with a diet, and the
    margins went down to 636 B release and 152 B under the one-save-page
    limit for debug.
  - The lean debug build differs from release by about 6.4 KB of
    features and a 2.5 KB frame cache instead of 3 KB, so board perf runs
    are of a different build (the cache size moves frame times by under
    0.1 ms in the simulator, section 5.4).
  - LTO noise of ±200 B makes small changes hard to judge.
- **The simulator's drawing model lives in the game.** It still runs
  about 0.5 ms optimistic, more with many actors, and every new kind of
  drawing needs a hand-written cost (section 9.2, E).
- **The warm-up is long.** A fresh 44.5 MB file means about 21 s of
  STIRRING THE SWAMP on first boot (sim), or up to a minute if every
  region stalls. It is the price of a big file with a 1 s first-read
  stall. If the stall turns out to be once per write and not once per
  region, reading one block per megabyte would do (44 commands).
- **The stack is near its limit.** The deepest static path is now
  1,552 B of 2,048 (est.): a remapped blit drawn inside the world's
  stream callback.
- **Not done from the plan:**
  - birds (a stretch goal, dropped for flash);
  - pre-rotted raccoon frames (the rot raccoons use REMAP_ROT);
  - B6, B7 and B9 on the board;
  - the streaming engine promoted to a chgame module. The plan's P5 was
    that module; the P5 that was built is CHGfx's remap table.
  - the generic packers (planned P7), ECBench as a library example (P8),
    and CHSd docs with board numbers (P9).

## 9. What it means for the CHGame library

### 9.1 The patch set

Every change under `chgame/` is one commit and one `git format-patch`
file. All five are additive: new functions or new files, dropped by the
linker from images that do not call them. None has been offered upstream
yet.

| # | Patch file | What | What it fixes | Cost | Evidence |
|---|---|---|---|---|---|
| P1 | `0002-CHSd-P1-...` | `fat::stream(runs, nRuns, k, n, ...)`: blocks `[k, k+n)` of a file in one `sd::stream()` per FAT run the range touches, checked against the file's length first | Lifts CHStlView's run splitting into CHSd, so a fragmented file streams with no code in the game | New `FatStream.cpp`; only in images that call it | CHSd host tests stream every file of every test image: whole files, every run boundary, random ranges, past the end, and a card failing part way. On the board every ECBench read goes through it (B2, B3, B4's 34,000 blocks, B8; a file in one run). The game itself still splits runs in its own `card::stream()` (section 3.2) |
| P2 | `0001-CHSd-P2-...` | `sd::setStreamTimeout(us)`, `sd::recover()`, `sd::isSdhc()`, `sd::readReg()` | A stalled stream cost up to 1 s and an `init()` at 187.5 kHz (4.1 ms, up to 1 s for ACMD41). Now 32 ms (the game's choice) and a 66 us `recover()` | New functions. `stream()` waits through its own `swait`, with the same 1 s by default. Its one other user, CHStlView, gets a different image | Board B8: 20 of 20 forced timeouts recovered without `init()` and re-read correctly; the game's lost-frame handling in `card_faults.txt` |
| P3 | `0003-CHSd-P3-...` | The simulator's card: a BUG on any `sd::` call during a flush (`sim_flushActive()`); timing knobs `CHSD_SIM_CMD_US`, `_BLK_US`, `_READ_US`, `_JITTER_US`, `_COLD_US`/`_COLD_KB`, `_FAIL_EVERY`, `_INIT_US`; P2's calls | The simulator never checked the bus rule, and its card timing was not the board's | Host and simulator only; the defaults time everything as before | The SD games' simulator images are unchanged (236 images, below) |
| P4 | `0004-CHGfx-P4-...` | `gfx_blit4k(src, x, y, wWords, h, flags, remap, level)`: a keyed blit of raw 4 bpp words at any x. Flip, remap, solid ink and 4x4 Bayer dither, clipped on every edge, with the row loops in SRAM | CHGfx had no fast sprite format for art arriving from a card | 538 B of RAM functions and ~470 B of flash, once called | Board B5: 149 / 170 / 237 / 180 / 455 us (32x32: even, odd, mirrored, dithered, remapped) against `gfx_blit`'s 226 / 442. Host test of ~33,000 cases against a pixel-at-a-time reference |
| P5 | `0005-CHGfx-P5-...` | `blit4k`'s remap and ink through a 256-byte table of pixel pairs built per call on the stack; a tighter copy loop and mirror on every path | A remapped blit cost 3x a plain one, and the hit flash and rot raccoons use it every frame | SRAM part 538 -> 632 B; remapped and inked calls use 256 B more stack | Output bit-identical: 33,617 host checks; CHGfx's 19,962; the game's 271 simulator images. Costs estimated at about 235 / 195 / 134 us (section 5.3), **not yet measured on the board** |

**Byte identity**, re-run for this report on the final `chgame/` (patches
0001-0005 in order):

- **The series applies cleanly** with `git am` to a pristine export of
  `bateske/CHGame` at `e876774c`. The result matches `chgame/`'s 101
  library and tools files byte for byte. The other 15 vendored files are
  unchanged from upstream.
- **The three SD games build byte-identical** with arduino-cli
  (`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`)
  against upstream's libraries and against `chgame/`'s. The build cache
  confirms which library folders each build used.

  | Game | Image (B) | MD5 starts |
  |---|---:|---|
  | CHWords | 50,404 | `0c0e97216cde` |
  | CHWordWheel | 50,376 | `d6cb81fe301f` |
  | CHCrossword | 50,416 | `888562450cc9` |
- **The simulator check passes in both trees.**
  `chgame check --quick --no-device` on the three games gives ALL GOOD
  with the pristine tools and the patched ones. The 236 images their
  scripts leave (83 + 93 + 60) are pixel-identical.
- **The host tests pass:**
  - CHSd's `tests/run_tests.py`, including the FAT32 images: ALL OK. It
    prints a count per test, 36,969 checks in all: 19,080 on the
    pretend card, 2 with no card and 17,887 on the 14 disk images
    (pristine upstream: 23,588 in all);
  - CHGfx's own tests: 19,962 checks, 0 failures;
  - the `blit4k` test, through `./ec test`.

**Status.** The patches are not yet offered upstream; section 10 has the
steps. Only CHSd and CHGfx change. CHGame's library is untouched, so the
plan's version bumps are CHSd 1.0.0 -> 1.1.0 and CHGfx 1.3.0 -> 1.4.0.

### 9.2 Proposals not yet done

These are ordered by expected value. The gains are estimates unless
marked.

#### A. A streaming module in chgame (`chgame/Spool.h` + `Spool.inl`)

Everything that makes the game stream is generic, and most of it has been
measured on the board (the warm-up and the stall policy only in the
simulator; the board measured `recover()` on its own, B8):
- the card file (`Card.cpp`, `CardFormat.h`, `cardfile.py`);
- the stall policy (`card::stream()`);
- the warm-up;
- the frame cache (`Spool.cpp`, `packbank.py`);
- the world streamer (`World.cpp`, `worldpack.py`);
- Path D (Rot.cpp, Occluders.cpp);
- Path F (`PathF.cpp`, `clip.py`);
- the pacer (`Pace.cpp`).

Package it like Sizzle: a header plus an `.inl` the game compiles, so it
sees the game's own config (arena size, run count) and other games'
images do not change. Leave it out of `Stream.h`, whose name Arduino's
own header takes. Sketch:

```cpp
// chgame/Spool.h: art streamed from one card file. Call everything between
// gfx_wait() and the next flush (the bus rule); gfx_chunkScratch() is the landing buffer.
namespace spool {
// The card file: a header block (magic, format, build hash, sections), up to SPOOL_RUNS FAT runs.
enum Status : uint8_t { READY, NO_CARD, NOT_FAT, NO_FILE, FRAGMENTED, BAD_DATA };
struct File { const char *name83; uint16_t format; uint32_t hash, blocks; };
Status open(const File &f);                 // sd::init, fat::mount/find/runs, block 0 checked
Status status();
void timeouts(uint32_t bootUs, uint32_t playUs);   // sd::setStreamTimeout for open() / for play
// A range of the file, each block to fn while the next arrives. False: this frame is lost
// (recovered: skip the flush, try again next frame); after HOLD lost frames hold the logic too,
// after GIVE_UP status() says NO_CARD.
bool stream(uint32_t k, uint32_t n, sd::BlockFn fn, void *ctx);
uint8_t lost();                             // lost frames in a row (0 after a whole frame)
bool warm(void (*bar)(uint32_t done, uint32_t total));   // read the whole file once (first boot)

// The frame cache: frames of raw 4 bpp words with an 8 B header (gfx_blit4k's), SPOOL_ARENA bytes.
void bank(uint32_t first, const BankClip *clips, uint16_t nClips);
void want(uint16_t clip, uint8_t f, uint8_t prio);
void budget(uint8_t cmds, uint8_t blocks);
bool fetch();
const uint8_t *frame(uint16_t clip, uint8_t f);   // nullptr: not yet (wanted again)
const uint8_t *peek(uint16_t clip, uint8_t f);

// A world of column blocks in bands (the window is one read); idle(words) between blocks.
struct World { uint32_t first; uint16_t cols, bands; uint8_t phases, layers, top, rows; };
bool world(const World &w, int cx, int cy, uint32_t layer, uint32_t phase, void (*idle)(uint32_t words));

// Path D: a picture in row bands, blitted from the landing buffer as it arrives.
struct Bands { uint32_t first; uint8_t wWords, h; };
bool blitBands(const Bands &b, int x, int y, uint8_t flags, const uint8_t *remap, int row0, int row1);

// Path F: clips of whole frames with a palette each.
bool clip(uint32_t first);  uint16_t frames();  bool clipFrame(uint16_t k);  bool play(uint32_t t, bool loop);

// Pacing: logic at 60 Hz, drawing at 60 or 30 by the work after gfx_wait().
namespace pace { void start(); void end(); bool half(); bool lately(); uint32_t elapsed(); }
}
```

Plus `tools/spool/`: the card file writer (header, hash, CRCs), the clip
writer, the band packers (world, row bands) and the frame bank packer.
Plus ECBench as `examples/Apps/CHSpoolBench`, so the board numbers can be
reproduced, and `tools/card_sync.py` (copy a file to the board's card
through CHSDtoUSB without taking it out) as a chgame command for every SD
game.

- **Expected cost to a game:** about 3-4 KB of flash for the card, cache,
  world and clip layers (the plan's estimate was 4.15 KB). LTO inlines
  them into the game, so the current image cannot say exactly. RAM is the
  game's arena plus about 420 B (directory, wants, runs).
- **Gain:** the next streaming game starts from a measured engine instead
  of eight milestones. CHStlView could use the stall policy.

#### B. A clip rectangle for `gfx_blit4k()` alone

`gfx_blit4k()` already honours `gfx__clip`. But as long as no code calls
`gfx_setClip()`, LTO folds the clip to the screen's constants in every
CHGfx primitive. One call anywhere makes the clip real in all of them:
about 250 B of flash and 70 B of RAM functions in all.

A rectangle read only by `blit4k`'s setup, `gfx_blit4kClip(x0, y0, x1, y1)`,
would cost about 40-60 B of flash and 8 B of RAM, and only in images that
call it. It would let:
- the willow passes and the toad's bands blit through the clip and drop
  their own cropping;
- the sprites drawn under the stream draw the final part of a sprite
  early and the rest later. Today a draw that touches the rightmost screen
  word waits for after the stream: about 2 a frame, 0.3 ms, with twelve
  critters.

#### C. A wait hook in `sd::stream()`: use the command latency

The CPU spends most of each command's 832 us polling for the card's
first token.
On the board, the busiest frames have three or four commands: the world,
a fetch, a willow pass, the toad. A hook called between those polls would
hand that time to the game:

```cpp
// SdSpi.h, additive
typedef void (*WaitFn)(void *ctx);
void setStreamWait(WaitFn fn, void *ctx);   // called while stream() waits for a token: after the command,
                                            // and between blocks; it must stay off SPI1 and the two buffers
```

- **Why it is safe.** The card holds a token until it is clocked out, so
  a hook that runs long only makes that stream longer by the overrun. The
  game budgets it, as it does `ECRT_HIDE_US`.
- **What can move into it:**
  - into the world's own command latency: the draw list's build
    (`make()`, which only records; today it runs in the first block's
    wait, where only about 150 us of it hides) and the HUD (0.2 ms in the
    model, `SIM_HUD_US`). The HUD can go first only if the sprites keep
    out of its rows 0-7: today a sprite near the top draws into them and
    the HUD covers it (B's clip rectangle would do it);
  - in the boss fight, into the toad's latency (the toad is the first
    item held for after the stream, and no willow pass is planned there):
    the rest of the queue, which `rest()` draws before any held item
    today.

  That is about 0.3-0.6 ms of the angry phase's 7.3 ms on the board in M7
  (est.), which would bring it to about 6.7-7.0 ms, around the pacer's
  6.9 ms calm line, on top of whatever M8's sprites under the stream gain
  there on the board. The fetch and willow planning gain nothing from the
  hook: `play::plan()` already runs before `gfx_wait()`, under the flush.
- **Split-phase is the general form:** `streamBegin()`, `streamPoll()`,
  `streamEnd()`. The hook is the smallest change that gets most of the
  gain.

#### D. CHSd's per-block overhead: 222 us against 171

A block at 24 MHz is 170.7 us on the wire. B2's fit is 221.7 us, so
51 us, or 23%, goes elsewhere. That is 0.87 ms of each 17-block world
frame and about 4 s of the 21 s warm-up.

Candidates:
- the card's own gap between blocks of a multi-block read, which is card
  dependent, and then there is nothing to gain;
- the polled 0xFF bytes while `swait(true)` hunts for the next token;
- `dmaStart()`'s dozen register writes from flash (about 5 cycles an
  instruction);
- the two polled CRC bytes.

**How to measure:** time each phase with `micros()` or a spare GPIO on a
scope in ECBench. Fixes to try:
- DMA the CRC bytes and the first poll bytes with the block (514+ bytes
  into a slightly larger buffer);
- move `dmaStart()` into SRAM.

If half the overhead is CHSd's, the gain is about 0.4 ms a world frame,
about 0.05 ms a two-block fetch, and 2 s off the warm-up.

#### E. A CPU-cost model in chsim

The simulator charges virtual time for the card (P3) but not for the CPU.
This game models its own drawing (`scene::blitCost()`, `fx::simCost()`,
constants for the HUD) and still runs -0.2 to +0.9 ms off. In the crowds
the error grows with the actor count, so per-actor game code is probably
the missing part.

An opt-in `CHSIM_CPU=board` could:
- advance virtual time inside each CHGfx call from a table of board costs.
  B5 has the first entries: `blit4k` at 32x32 (plain, odd x, mirrored,
  dithered, remapped), `gfx_blit`, `sprite4`, `remapRect`, the column copy
  and the flush. A per-call and per-word-row split for `blit4k` needs a
  second size (B5's P5 cases add a 24x21), and fills and text need cases
  of their own.
- optionally charge game code from a per-function or per-basic-block
  count, at the flash's roughly 5 cycles an instruction.

Every game's perf scripts would then be board-like without game-side
models, and the 30 fps switches (Willow Hollow, the angry phase) would
have been predicted before the board runs.

#### F. More RAM

Section 5.4 puts a number on it: each extra KB for the frame cache is
worth about 0.1-0.3 ms a frame in a crowd, plus smoother animation.
Options, smallest risk first:

- **Let a game choose its stack reservation**, which today is a fixed
  2 KB (`__stack_size = 2048` in the board package's
  `link_chgame_app.ld`). The board's `stk=` read 1,312-1,332 B in M5-M7,
  and the static estimate after M8 is 1,552 B (about 1,640 B expected on
  the board). A 1.75 KB reservation would give 256 B back. That needs
  `stk=` on the board first.
- **A 12 bpp flush without the 1 KB LUT.** Convert pixel pairs through a
  16-entry table of 12-bit colours (32 B). The flush's conversion work
  roughly doubles, and it runs in the DMA interrupt while logic runs.
  Measure the CPU it takes from the logic first.
- **A framebuffer mode of 128x120 plus an 8-row strip** for a HUD that
  is redrawn every frame anyway. This saves 384 B: the 512 B of the HUD's
  rows, less 128 B for the strip at 1 bpp. It needs flush support for two
  sources, and is the weakest of the three.

#### G. The panel's TE pin

The panel's tearing-effect pin is not wired, so frames reach the panel
unsynchronised with its scan:
- CHGfx's FRMCTR1 0x05, 0x3A, 0x3A gives about 61.6 Hz by its own formula;
- a 60 fps game beats against that at about 1.6 Hz;
- an 8.41 ms flush takes about half a scan.

A tear line can crawl through scrolling scenes. Nothing about it was
measured or recorded here.

- **On a future board revision:** wire TE to a GPIO with an interrupt.
  A flush started at TE stays ahead of the scan (8.4 ms of writes against
  a 16.2 ms scan), so frames would be tear-free at the cost of up to one
  scan of latency.
- **Until then:** a panel rate far from 60 Hz makes the tear move fast and
  show less. CHGfx already has `gfx_setPanelFrameRate(rtna, fpa, bpa)` to
  try that with. It needs eyes on the board.

#### H. Smaller items

- **A board-package option for `-flto-partition=one`.** About 200 B here,
  and it would remove the ±200 B noise that LTO's partitions add to every
  size measurement.
- **Size guidance for games, in docs/performance.md or a shared
  `chgame/Size.h`:**
  - `no-jump-tables` -196 B;
  - `no-move-loop-invariants` -212 B;
  - `no-partial-inlining` -48 B;
  - outline helpers that LTO copies into many callers, but never on a hot
    path.
- **CHSd's documentation (docs/sd-card.md).**
  - It says some cards take 300-800 ms over never-written blocks. This
    card read never-read data fast after power-up (at most 1.7 ms), but
    stalled 1.05 s once on its first read after a write.
  - Add the board's B1-B8 numbers.
  - Say that CRC is not needed at this soak rate (0 in 34,000 blocks).

## 10. Upstreaming

The exact steps to offer P1-P5 to `bateske/CHGame`:

1. **Branch at the vendored commit:**

   ```
   git clone https://github.com/bateske/CHGame && cd CHGame
   git checkout -b sd-streaming e876774c
   ```

2. **Apply the series.** The patch paths are relative to the repository
   root, because `chgame/` mirrors it:

   ```
   git am /path/to/EthansCritters/chgame/patches/000[1-5]-*.patch
   ```

   This gives five commits: P2, P1, P3, P4, P5. If `main` has moved,
   `git rebase origin/main` and resolve. The series touches only CHSd,
   CHGfx's `blit4k` files, its header, README and keywords, and
   `tools/chsim/host/chgfx_host.cpp` / `sim.h`.
3. **Bump the versions** in `library.properties`, in a commit of their
   own: CHSd 1.0.0 -> 1.1.0, CHGfx 1.3.0 -> 1.4.0.
4. **Run CHSd's tests,** with the FAT32 images:

   ```
   python platform/board/arduino/CHGame/libraries/CHSd/tests/run_tests.py
   ```

   Expect ALL OK. It prints no total: the per-test counts came to 36,969
   checks on 2026-10-04 (19,080 on the pretend card, 2 with no card,
   17,887 on the 14 disk images), against 23,588 without the patches.
5. **Run CHGfx's tests:**

   ```
   python tools/chgame.py --sketch CHGfx test
   ```

   Expect 19,962 checks and 0 failures. Then run the `blit4k` test, which
   sits in its own folder because `chsim.py test` links every
   `extras/tests/*.cpp` together:

   ```
   L=platform/board/arduino/CHGame/libraries/CHGfx
   python -m ziglang c++ -std=gnu++17 -O2 -DCHSIM -DCH32X035 -I $L/src -I tools/chsim/host \
       $L/extras/tests/blit4k/test_blit4k.cpp $L/src/CHGfx_blit4k.cpp -o blit4k && ./blit4k
   ```

   Expect `blit4k: 33617 checks, 0 failures`.
6. **Run `chgame check` on the SD games,** in the patched tree and in a
   pristine clone:

   ```
   for g in CHWords CHWordWheel CHCrossword; do python tools/chgame.py --sketch $g check --quick --no-device; done
   ```

   Expect ALL GOOD for each. Then compare the images their scripts leave
   in each game's `out/` between the two trees. They should be identical:
   236 on 2026-10-03.
7. **Rebuild all 20 games and both apps for size** in both trees, and
   compare the images:

   ```
   for d in platform/board/arduino/CHGame/libraries/CHGame/examples/{Games,Apps}/*/; do
     python tools/chgame.py --sketch "$d" build; done
   md5sum platform/board/arduino/CHGame/libraries/CHGame/examples/*/*/build/release/*.ino.bin
   ```

   - Expect all 20 games and CHSDtoUSB to be byte-identical. None calls
     the new functions; CHSDtoUSB has its own driver.
   - CHStlView changes, because `sd::stream()` now waits through `swait`.
     Record its size, and check it on the board if possible.
   - On 2026-10-03, CHWords, CHWordWheel and CHCrossword were identical at
     50,404 / 50,376 / 50,416 B.
8. **Push the branch and open a pull request.** Use `chgame/PATCHES.md`'s
   table and this section's results as its description, and say that P5's
   board costs (ECBench B5's new cases) are still to be measured.

## 11. Board runs still to do

In order, on the debug build, with the card data copied by
`tools/card_sync.py`:

1. ~~ECBench B5 with P5's cases~~ (done 2026-10-04, section 4.2). Still
   to do: put the measured costs into `scene::blitCost()` and drop
   `ECRT_SIM_P5`.
2. ~~`boss_perf`, `occlude`, `crowd_perf`, `slice_perf`~~ (done: Willow
   Hollow and twelve critters hold 60 fps; the angry phase does not;
   `stk=` 1,544 B).
3. ~~Is part of each block really free for drawing?~~ (done: 60, 100 and
   150 us give the same times, and 0 is 0.2-0.4 ms slower).
4. **The warm-up's time on the board**, from the release build (no
   handshake to lose): copy a new card, then time the STIRRING THE SWAMP
   bar.
5. **The first-read-after-write stall.** Copy a fresh file, then time
   first reads region by region. Is it once per write or once per region?
   That decides whether the warm-up can read one block a megabyte.
6. **`recover()` on a card still busy after a write:** does it come back
   within 32 ms?
7. **Watch for tearing** while walking and in the clips.
