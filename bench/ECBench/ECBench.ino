// ECBench - the board benchmarks behind Ethan's Critters' streaming design.
//
// The game is to stream its world and sprites off the SD card every frame,
// on the panel's SPI bus, between gfx_wait() and the next flush. These are
// the numbers that design rests on (the plan's B1-B5 and B8), measured the
// way the game will run: each card read followed by a full-screen flush.
//
//   B1  sd::init, fat::mount, fat::find + runs, the header read (us); the
//       card's type, maker, product and size; the file's runs
//   B3  at boot, before anything else reads the file: one block every 1 MB,
//       each the first read of its megabyte since power-up (us each)
//   B2  sd::stream of 1, 2, 4, 8, 17 and 32 blocks at random offsets over
//       the whole file and inside a 128 KB window, 200 times each:
//       p50 / p95 / max (us) and a fit: us per command + us per block
//   B4  soak: frames of 17 blocks at a random offset, every block's pattern
//       checked in the callback, then a flush; bad blocks, failed commands
//   B5  drawing: CHGfx's blit, CHGame's sprite4, remapRect, a 16-block copy,
//       the world's column copy (and a check that it is right), the flush,
//       gfx_blit4k (CHGfx P4, the game's sprite blitter)
//   B8  timeouts: never-read blocks under a 5 ms stream timeout (how often
//       it runs out, how long to the data retrying each frame), recover()
//       after forced timeouts (is init() needed?), recover() and init() (us)
//
// The card holds BENCH.DAT (bench/tools/make_bench_file.py, 32 MB), whose
// every block can be checked where it lands. Results go out on the debug
// protocol as lines starting "ECBN " (a debug build; the simulator always),
// and onto the screen.
//
// Debug protocol commands (tools/chsim/chdrive.py's `bench` runs them):
//   Z0            everything below, in the order of a cold start (the boot
//                 reports, B8, B2, B4, B5)
//   Z1 [1]        B1 as measured at boot; 1: again now
//   Z3 [1]        B3 as measured at boot; 1: again now (warm)
//   Z2 [iters]    B2 (default 200 per size and region)
//   Z4 [frames] [timeout_us]   B4 (default 2000 frames, 1 s timeout)
//   Z5            B5
//   Z8 [timeout_us]            B8 (default 5000)
//   Z9            (simulator) a stream during a flush: the bus-rule BUG
//   V<page>       show page 0-3
// With no driver (a release build, or a debug build no script has taken
// over), the same sequence starts by itself; A starts it again. LEFT and
// RIGHT turn the pages.
#include <CHGame.h>
#include <Fat.h>
#include <SdSpi.h>
#include "config.h"
#include "Bench.h"

using namespace bench;

// Plain colours for reading numbers (RGB444): the background, text, dim
// labels, headings, good, bad, notable, the bars; 8-15 for B5's art.
static const uint16_t PALETTE[16] = {
    0x112, 0xEEE, 0x88A, 0xFC4, 0x6E6, 0xF55, 0x4CF, 0x335,
    0x742, 0x964, 0xB86, 0x485, 0x6A6, 0x8C8, 0xC43, 0x456,
};

static uint32_t bootMs;
static uint8_t autoStep;          // 0: waiting, 1..: the next step of the sequence, AUTO_DONE: finished
static bool hostSeen;             // a script has run a command: no sequence by itself
static uint32_t pageMs;
static const uint8_t AUTO_DONE = 0xFF;

// The sequence of a cold start (B1 and B3 ran in setup()).
static const uint8_t SEQUENCE[] = {8, 2, 4, 5};

static void run(uint32_t which, uint32_t a, uint32_t b) {
    switch (which) {
        case 1: reportB1(a != 0); break;
        case 2: streamBench(a ? a : ECBN_ITERS); break;
        case 3:
            if (a) coldSweep(false);
            reportB3();
            break;
        case 4: soak(a, b); break;
        case 5: drawBench(); break;
        case 8: recoveryBench(a); break;
        case 9: busViolation(); break;
        default:
            reportB1(false);
            reportB3();
            for (uint32_t i = 0; i < sizeof SEQUENCE; i++) run(SEQUENCE[i], 0, 0);
            break;
    }
    status(card.state == CARD_OK ? "DONE" : "NO BENCH.DAT");
    show(page);
}

#if CHGAME_DEBUG
static bool onCommand(char c, const char *args) {
    if (c == 'Z') {
        hostSeen = true;
        uint32_t which = dbg::parseNum(args, 10);
        uint32_t a = dbg::parseNum(args, 10);
        uint32_t b = dbg::parseNum(args, 10);
        if (which > 9 || which == 6 || which == 7) return false;
        run(which, a, b);
        return true;
    }
    if (c == 'V') {
        show((uint8_t)(dbg::parseNum(args, 10) % PAGES));
        return true;
    }
    return false;
}
#endif

void setup() {
    chgame.boot();
    dbg::begin("ECBN " ECBN_VERSION);
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init(PALETTE);
    pal::setCycling(false);
    chgame.setFrameRate(30);
#if CHGAME_DEBUG
    dbg::hook = onCommand;
#endif
    // B1 and B3 first, while the card is as cold as it will ever be.
    status("B1 CARD");
    show(PG_CARD);
    if (bootCard()) coldSweep(true);
    status(card.state == CARD_OK ? "READY: A RUNS ALL" : "NO BENCH.DAT ON THE CARD");
    show(PG_CARD);
    bootMs = millis();
}

void loop() {
    dbg::poll();
    if (!chgame.nextFrame()) return;
    chgame.pollButtons();
    if (chgame.justPressed(A_BUTTON)) autoStep = 1;
    // No driver (lockstep off) and no script command since boot: run it all.
    if (!autoStep && !hostSeen && chgame.lockstep < 0 && millis() - bootMs > ECBN_AUTO_MS) autoStep = 1;
    if (autoStep && autoStep != AUTO_DONE) {
        if (autoStep == 1) {
            reportB1(false);
            reportB3();
        }
        run(SEQUENCE[autoStep - 1], 0, 0);
        autoStep = autoStep < sizeof SEQUENCE ? autoStep + 1 : AUTO_DONE;
        pageMs = millis();
        return;
    }
    if (chgame.justPressed(RIGHT_BUTTON)) page = (uint8_t)((page + 1) % PAGES), pageMs = millis();
    if (chgame.justPressed(LEFT_BUTTON)) page = (uint8_t)((page + PAGES - 1) % PAGES), pageMs = millis();
    // Unattended, the pages take turns.
    if (autoStep == AUTO_DONE && !hostSeen && millis() - pageMs > 5000) {
        page = (uint8_t)((page + 1) % PAGES);
        pageMs = millis();
    }
    gfx_wait();
    drawPage();
    gfx_flushAsync();
}
