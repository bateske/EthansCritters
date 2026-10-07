// ECBench's parts: the card file, the results, the benchmarks and the screen.
#pragma once
#include <CHGame.h>
#include <Fat.h>
#include <SdSpi.h>
#include "config.h"

// The bench's colours (its own palette, ECBench.ino).
enum : uint8_t { C_BG = 0, C_TEXT, C_DIM, C_HEAD, C_GOOD, C_BAD, C_ACC, C_BAR };

namespace bench {

// ---- The card and its file ----------------------------------------------
enum CardState : uint8_t { CARD_WAIT, CARD_NONE, CARD_NOFS, CARD_NOFILE, CARD_BADFILE, CARD_OK };

struct Card {
    uint8_t state, nRuns, mid, sdhc;
    int8_t mountRc, findRc;
    char pnm[6];                     // the CID's product name
    uint32_t blocks;                 // the file's, from its header
    uint32_t capMB;                  // the card's, from its CSD
    uint32_t initUs, mountUs, openUs, hdrUs;
    fat::Run runs[ECBN_MAX_RUNS];
};
extern Card card;

// ---- Results ----------------------------------------------------------
struct Pct { uint32_t p50, p95, hi; uint16_t fails, bad; };

enum Draw : uint8_t {
    D_BLIT_EVEN, D_BLIT_ODD, D_SPR, D_SPR_REMAP, D_SPR_FLIP, D_REMAP, D_COPY16,
    D_COL0, D_COLP, D_WORLD0, D_WORLDP, D_FLUSH_START, D_FLUSH_WIRE,
    D_B4_EVEN, D_B4_ODD, D_B4_FLIP, D_B4_REMAP, D_B4_DITHER, D_B4_RMFLIP, D_B4_SOLID, D_B4_24, D_B4_24RM,
    DRAW_N
};

static const uint8_t N_SIZES = 6;
extern const uint8_t NS[N_SIZES];    // B2's block counts: 1 2 4 8 17 32

struct Results {
    uint16_t have;                   // bit b set: Bb has run
    // B3: the first read of each MB of the file (us; bit 31: failed)
    uint8_t coldN, coldAgain;
    uint16_t coldFail, coldBad;
    uint32_t cold[ECBN_COLD_MAX];
    // B2: [random, warm][size]
    uint16_t iters, splits;
    Pct pct[2][N_SIZES];
    Pct split;                       // 17 blocks across a run boundary (a file in pieces only)
    int32_t fitCmd[2], fitBlk10[2];  // us per command, tenths of a us per block
    // B4
    uint32_t soakFrames, soakBlocks, soakBad, soakFails, soakRecovers, soakInits;
    uint32_t soakHi, soakAvg, soakOver, soakTimeout;
    // B5: tenths of a us per call (the WORLD ones per screen)
    uint32_t draw[DRAW_N];
    uint32_t colBad;
    // B8
    uint8_t rgn, rgnTimeouts, rgnLost;
    uint16_t triesHi;
    uint32_t shortUs, toDataP50, toDataHi;
    uint16_t forced, forcedRecovered, forcedReread;
    uint32_t recAvg, recHi, recIdle, initAvg, initHi;
};
extern Results R;

// ---- Output: "ECBN ..." lines on the debug protocol --------------------
char *line(const char *tag);                     // the line buffer, holding "ECBN <tag>"
char *kv(char *p, const char *key, int32_t v);   // " key=v"
char *kd(char *p, const char *key, int32_t t);   // " key=v.t" from tenths
char *ks(char *p, const char *key, const char *s);
void emit(char *end);                            // newline, print
void alive(const char *what);                    // "... what" if nothing went out for 10 s

// ---- The file's pattern (bench/tools/make_bench_file.py) ---------------
bool patternOk(const uint8_t *blk, uint32_t k);  // block k (>= 1) as written
void seed(uint32_t s);
uint32_t rnd();
uint32_t pct(uint32_t *v, uint32_t n, uint32_t *p95, uint32_t *hi);   // sorts v; the median

// What a stream callback checks: blocks next, next + 1, ... (verify: every
// word of the pattern, else only the block number in word 0).
struct Check { uint32_t next, got, bad; bool verify; };
void onBlock(const uint8_t *blk, void *ctx);
bool readFile(uint32_t k, uint32_t n, Check &c); // fat::stream through the file's runs
bool heal();                                     // after a failed stream: recover(), else init()

// ---- The benchmarks (each runs to its end, a frame per step) -----------
bool bootCard();                                 // B1, at boot
void reportB1(bool again);
void coldSweep(bool boot);                       // B3
void reportB3();
void streamBench(uint32_t iters);                // B2
void soak(uint32_t frames, uint32_t timeoutUs);  // B4
void drawBench();                                // B5
void recoveryBench(uint32_t shortUs);            // B8
void busViolation();                             // (simulator) the bus-rule check, on purpose

// ---- The screen ----------------------------------------------------------
enum Page : uint8_t { PG_CARD, PG_STREAM, PG_SOAK, PG_DRAW, PAGES };
extern uint8_t page;
void drawPage();                                 // the current page, status line included
void status(const char *s);                      // the status line's text (drawn by drawPage)
// One step of a benchmark: "WHAT i/n" on the status line (only: the page
// stays as show() drew it), then the flush. Call it where the game would
// flush: after the card, before the next gfx_wait().
void step(const char *what, uint32_t i, uint32_t n);
void show(uint8_t pg);                           // draw a page and flush it

}  // namespace bench
