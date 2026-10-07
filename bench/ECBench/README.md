# ECBench

The board benchmarks behind Ethan's Critters' streaming design (the plan's
B1-B5 and B8): how fast the SD card answers on the panel's SPI bus between
flushes, how cold reads after power-up behave, whether a long run of reads
stays clean, what the drawing primitives cost, and how a stalled stream
recovers. Every card read is followed by a full-screen flush, as in the game.

| # | What | Result lines |
|---|---|---|
| B1 | `sd::init`, `fat::mount`, `fat::find` + `runs`, the header read (us); card type, maker, product, size; the file's runs | `ECBN B1 ...`, `ECBN B1 run ...` |
| B3 | at boot, before anything else reads the file: one block every 1 MB, each the first read of its megabyte (us) | `ECBN B3 mb r= us= ok=`, `ECBN B3 ... p50= p95= max=` |
| B2 | `sd::stream` of 1, 2, 4, 8, 17, 32 blocks, 200 reads each at random offsets over the whole file and in a 128 KB window: p50 / p95 / max, a fit (us per command + us per block), and 17 blocks across a run boundary when the file is in pieces | `ECBN B2 mode= n= p50= p95= max= fail= bad=`, `ECBN B2 fit ...` |
| B4 | soak: frames of 17 blocks at a random offset, every block's pattern checked in the callback, + a flush; bad blocks, failed commands, recover()/init() count, latency | `ECBN B4 ...` |
| B5 | `gfx_blit` 32x32 keyed at even/odd x, `sprite4` 24x21 plain/remap/flip, `gfx_remapRect` 24x8, a 16-block word copy, the world's column copy (p = 0 and 3, per block and per screen, plus a pixel check), the flush, `gfx_blit4k` (CHGfx P4, its remap and ink made fast by P5) on the same 32x32 sprite at x % 8 = 0 and 3, mirrored, remapped, dithered, remapped and mirrored, inked, and a 24x21 frame plain and remapped (us per call) | `ECBN B5 what= us=` |
| B8 | never-read blocks under a 5 ms stream timeout (how often it runs out; time to the data retrying each frame after `recover()`); forced timeouts then `recover()` and a re-read (is `init()` needed?); `recover()` and `init()` (us) | `ECBN B8 ...` |

## The card

    python bench/tools/make_bench_file.py          # out/bench/BENCH.DAT, 32 MB

Copy `out/bench/BENCH.DAT` to the root of a FAT16/FAT32 card (a freshly
formatted card keeps it in one run). Block 0 is a header; block k holds k
and an xorshift32 run seeded from k, so any block is checked where it lands.

## On the board

The board runs use the debug build, whose numbers come back over USB:

    python chgame/tools/chgame.py --sketch bench/ECBench build --debug          # compile only
    python chgame/tools/chgame.py --sketch bench/ECBench run tools/scripts/all.txt out/board_all --device

`run --device` builds the debug image, uploads it and runs the script (start
it with the upload, as `run --device` does: with no driver for 6 s after
boot the sketch starts its own sequence). Results land in
`bench/ECBench/out/<dir>/`: `results.txt` (every `ECBN` line),
`summary.txt` (tables) and the screen snaps.

One benchmark at a time (each `run --device` uploads again, so B1 and B3
run again at boot):

| Benchmark | Script |
|---|---|
| B1 + B3 (boot) | `tools/scripts/boot.txt` |
| B2 | `tools/scripts/b2.txt` |
| B4 (2,000 frames / 20,000) | `tools/scripts/b4.txt` / `tools/scripts/soak20k.txt` |
| B5 | `tools/scripts/b5.txt` |
| B8 | `tools/scripts/b8.txt` (first after boot) |
| all, short | `tools/scripts/quick.txt` |

A reset does not cut the card's power. For B3 as a real cold start: power
the board off and on, let it boot into ECBench, then drive it without an
upload:

    python bench/ECBench/tools/chsim/chdrive.py --device bench/ECBench/tools/scripts/boot.txt bench/ECBench/out/board_cold1

A release build (`build`, `upload`) needs no PC: after boot it runs every
benchmark by itself and then shows the pages in turn (LEFT / RIGHT turn
them, A runs everything again).

## Commands (debug protocol)

`Z0` all, `Z1 [1]` B1 (1: again now), `Z3 [1]` B3 (1: again now),
`Z2 [iters]`, `Z4 [frames] [timeout_us]`, `Z5`, `Z8 [timeout_us]`, `Z9`
(simulator: a stream during a flush, the bus-rule BUG), `V<page>` show page
0-3. In a chdrive script: `bench Z2`, then `summary`.

## In the simulator

    python chgame/tools/chgame.py --sketch bench/ECBench run tools/scripts/all.txt out/bench_all

The card's timing comes from CHSd's simulator knobs (`CHSD_SIM_CMD_US`,
`_BLK_US`, `_READ_US`, `_JITTER_US`, `_COLD_US`, `_COLD_KB`, `_FAIL_EVERY`,
`_INIT_US`; see CHSd's `host/sd_host.cpp`), e.g.

    CHSD_SIM_COLD_US=300000 CHSD_SIM_COLD_KB=512 python chgame/tools/chgame.py --sketch bench/ECBench run tools/scripts/all.txt out/bench_cold

Drawing takes no simulated time, so B5 reads 0 there (its pixel check of
the column copy still runs).
