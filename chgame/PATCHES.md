# Changes to the vendored CHGame code

Every change this project makes under `chgame/` is listed here, one commit
and one patch file (`patches/NNNN-*.patch`, from `git format-patch`) each,
so they can be offered upstream as a series. All are meant to be additive:
the three SD games (CHWords, CHWordWheel, CHCrossword) must keep
byte-identical images, which is checked in a full CHGame checkout before
anything is sent.

P1-P3 were checked on 2026-10-03: CHWords, CHWordWheel and CHCrossword
built with arduino-cli (`CHGame:ch32v:rev0:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`,
CHGfx and CHGame from `chgame/`) give byte-identical `.bin` files with
CHSd from `out/upstream` and with the patched CHSd (50,404, 50,376 and
50,416 B); `chgame check --quick --no-device` of the three passes with the
patched simulator, and all 236 images its scripts leave are pixel-identical
to the pristine simulator's; CHSd's `tests/run_tests.py` passes.

The patch files are numbered in the order they apply (P2 was committed
before P1). Apply them in that order with `git am`, from the root of a
CHGame checkout: `chgame/` mirrors its layout, so the paths match.

| # | Where | Change | Commit (this repository) / patch |
|---|---|---|---|
| P1 | CHSd `src/FatStream.cpp` (new), `src/Fat.h` | `fat::stream(run, nRuns, k, n, buf0, buf1, fn, ctx)`: blocks `[k, k+n)` of a file through `sd::stream()`, one command per run the range touches (lifted from CHStlView's `card::stream()`), the range checked before anything is read. Host tests: `tests/test_fat.cpp` streams every file of every image (whole files, every run boundary, random ranges, past the end, a card failing part way). | `9c2d420`, `patches/0002-CHSd-P1-fat-stream-a-file-s-block-range-through-its-.patch` |
| P2 | CHSd `src/SdSpi.h`, `src/SdSpi.cpp` | `sd::setStreamTimeout(us)` (default 1 s, as before) through a wait of `stream()`'s own (`swait`), so `wait()`/`read()`/`init()` are untouched; `sd::recover()` after a failed stream (CMD12 always ends a stream, so no stale block can follow: wait out busy, CMD12 again, CMD13 status; false = still busy, power lost or no card, then `init()`); `sd::isSdhc()` and `sd::readReg(9 or 10, dst16)` (CSD/CID) for benchmark reports. All new functions, dropped by the linker from images that do not call them, except that `stream()` now waits through `swait` (the same 1 s by default): its one user, CHStlView, gets a different image; the games do not call it. | `b41d029`, `patches/0001-CHSd-P2-sd-setStreamTimeout-sd-recover-isSdhc-readRe.patch` |
| P3 | CHSd `host/sd_host.cpp`; `tools/chsim/host/chgfx_host.cpp`, `sim.h` | The simulator's card: a BUG on any `sd::` call while a flush holds SPI1 (`sim_flushActive()`, new, reads the flush state without moving time); timing from `CHSD_SIM_CMD_US` (600), `_BLK_US` (170), `_READ_US` (900), `_JITTER_US` (0, fixed seed), `_COLD_US` (0) per `_COLD_KB` (1024) region from its first touch, `_FAIL_EVERY` (0, the Nth stream fails part way), `_INIT_US` (0); `setStreamTimeout`, `recover`, `isSdhc`, `readReg`; as on the board, `stream()` hands a block to fn once the next block's token is in, so a failed stream loses the block before the failure too, and a card pulled and put back fails `stream()`/`recover()`/`readReg()` until `init()` (`read()` unchanged). The defaults time everything as before. | `de595db`, `patches/0003-CHSd-P3-the-simulator-s-card-checks-the-bus-rule-and.patch` |
| P4 | CHGfx `src/CHGfx_blit4k.cpp` (new), `src/CHGfx.h`, `extras/tests/blit4k/test_blit4k.cpp` (new), README, keywords | `gfx_blit4k(src, x, y, wWords, h, flags, remap, level)`: a keyed blit of raw 4 bpp words (wWords 1..16 a row, 15 the key) at any x (funnel shift, SWAR key mask, read-modify-write of whole framebuffer words), clipped on every edge; `GFX_B4_FLIPH` (the padded width), `GFX_B4_REMAP` (15 stays the key), `GFX_B4_SOLID` + `GFX_B4_INK(c)` (0..14), `GFX_B4_DITHER` (4x4 Bayer anchored to the screen, level 0..16). Setup in flash, the row loops in SRAM: 538 B of RAM and ~470 B of flash once called, no calls out of the SRAM part (frame pointer kept, so no `__riscv_save`). Host test: ~33,000 random cases against a pixel-at-a-time reference; in its own folder because `chsim.py test` links every `extras/tests/*.cpp` together (CHGfx's own 19,962 checks still pass). Checked 2026-10-03: CHWords, CHWordWheel, CHCrossword byte-identical with and without it (50,404, 50,376, 50,416 B). | `04736fe`, `patches/0004-CHGfx-P4-gfx_blit4k-a-keyed-word-blit-at-any-x-with-.patch` |
| P5 | CHGfx `src/CHGfx_blit4k.cpp`, `extras/tests/blit4k/test_blit4k.cpp`, `src/CHGfx.h` and README (comments) | `gfx_blit4k`'s remap and ink made fast: they fold into one colour map M (the remap, then the ink over all but 15; M[15] always 15, so the key stays the key and the table's entry 15 is still never read), spread over a 256-byte table of pixel pairs (`T[lo \| hi << 4] = M[lo] \| M[hi] << 4`, swapped for a mirrored blit) that each remapped or inked call builds (64 word stores, ~10 us) on the stack of a function of its own, so plain, mirrored and dithered calls neither build it nor go deeper: a load per two pixels instead of a nibble at a time (~22 instructions a source word, was ~80). All paths: the copy loop picked once a row (no flag tests per word), the mirror a rotate and two delta swaps (no spills), the merge keeping the previous word in a register (~26 instructions a word stored, was ~28). Output bit-identical: the host test (P4's ~33,000 cases plus every pixel pair with both flips at every x phase, entries above 15 and of 15, a table changed between calls, REMAP without a table: 33,617 checks) and CHGfx's 19,962 pass; Ethan's Critters' 271 simulator images are pixel-identical. SRAM part 538 -> 632 B, still no calls out of it (checked in the LTO image); remapped and inked calls take 256 B more stack. Expected on the board (32x32; disassembly and a cycle model fitted to P4's B5 numbers): remapped ~235 us (455), mirrored ~195 (237), plain ~134 / ~153 at x % 8 = 0 / 3 (149 / 170). Checked 2026-10-03: applies on `out/upstream` after 0001-0004 and gives `chgame/`'s files; CHWords, CHWordWheel, CHCrossword byte-identical with the pristine, P4 and P5 CHGfx (50,404, 50,376, 50,416 B). | `f7f5dbc`, `patches/0005-CHGfx-P5-gfx_blit4k-remaps-and-inks-through-a-table-.patch` |

## Status and evidence (P1-P5, the whole series)

**Upstream:** not offered yet. The steps (branch at `e876774c`, `git am`
0001-0005, version bumps CHSd 1.1.0 and CHGfx 1.4.0, the tests, the size
check of every game) are in `docs/streaming-report.md`, section 10
(Upstreaming). M8d (the sprites drawn while the world streams) changed
nothing under `chgame/`, so the series is final as listed.

**Re-checked on 2026-10-03 after M8, on the final `chgame/`** (for the
report):

- **The series applies.** 0001-0005 apply in order with `git am` to a
  pristine export of `out/upstream` (`e876774c`). The result equals
  `chgame/`'s 101 files under `platform/board/arduino/CHGame/libraries/`
  and `tools/` byte for byte. The other 15 vendored files are unchanged
  from upstream, so every difference from upstream is in a patch.
- **The SD games are byte-identical.** CHWords, CHWordWheel and
  CHCrossword built with arduino-cli (the FQBN above, all three libraries
  given with `--library`) give the same `.bin` with upstream's CHGame,
  CHGfx and CHSd and with `chgame/`'s: 50,404, 50,376 and 50,416 B, MD5
  starting `0c0e97216cde`, `d6cb81fe301f` and `888562450cc9`. The build
  cache shows each build used the intended library folders.
- **The simulator check passes in both trees.**
  `chgame check --quick --no-device` on the three games gives ALL GOOD
  with the pristine tools and with the patched ones. The 236 images their
  scripts leave (83, 93, 60) are pixel-identical.
- **The host tests pass:**
  - CHSd's `tests/run_tests.py`, full, with the FAT32 images: ALL OK.
    It prints a count per test and no total: 36,969 checks in all, 19,080
    on the pretend card, 2 with no card and 17,887 on the 14 disk images
    (pristine upstream: 23,588);
  - CHGfx's tests (`chgame --sketch CHGfx test`): 19,962 checks, 0
    failures;
  - the `blit4k` test: `ec test`, or standalone in a CHGame checkout with
    the command in `docs/streaming-report.md` section 10, step 5 (33,617
    checks).
- **Dry run of the upstreaming steps** (review, 2026-10-04): a clone of
  `out/upstream` at `e876774c`, `git am` of 0001-0005 (clean), then from
  its root CHSd's `run_tests.py` (ALL OK, the counts above), `chgame.py
  --sketch CHGfx test` (19,962, 0 failures), the `blit4k` command (33,617,
  0 failures) and `chgame.py --sketch CHWords check --quick --no-device`
  (ALL GOOD). The clone's 116 vendored files equal `chgame/`'s.

**On the board** (ECBench, `bench/results/`):
- P1: every ECBench read (B2, B3, B4, B8) goes through `fat::stream()`,
  on a file in one run: B4's 34,000 blocks with 0 bad and 0 failed. (Ethan's
  Critters itself still splits runs in its own `card::stream()`, the
  loop P1 was lifted from.)
- P2: 20 of 20 forced timeouts recovered by `sd::recover()` without
  `init()` and re-read correctly; `recover()` takes 66 us and `init()`
  4,118 us.
- P4: `gfx_blit4k` 32x32 takes 149 us even x, 170 odd, 237 mirrored,
  180 dithered, 455 remapped. `gfx_blit` takes 226 / 442.
- P5: not measured yet. ECBench B5 has its cases (`blit4k32_rmflip`,
  `_solid`, `blit4k24_odd`, `_remap`).

## Not needed after all

- `tools/device.py` reading the board from an environment variable: the
  staged board package `0.3.0-local` (installed from `tools/release/stage.py`
  in a CHGame checkout) has the `rev0` board the tools ask for.
