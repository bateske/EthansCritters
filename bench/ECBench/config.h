// ECBench build switches: the board benchmarks behind Ethan's Critters'
// streaming design (the plan's B1-B5 and B8).
//
// Built like the game: board package 0.3.0+, Peripherals "Game", Optimize
// "Smallest + LTO". The board runs use the debug build (USB Serial and the
// debug protocol, so a script captures the numbers):
//   python chgame/tools/chgame.py --sketch bench/ECBench build --debug
// A release build runs everything by itself and shows the numbers on screen.
#pragma once

#define ECBN_VERSION    "0.1"

// The CHGame library's switches (CHGAME_DEBUG: the debug protocol).
#include <chgame/Config.h>

// The card file (bench/tools/make_bench_file.py), 8.3 as the directory
// stores it.
#define ECBN_FILE       "BENCH   DAT"
#define ECBN_MAX_RUNS   8               // the game accepts 8; a fresh card gives 1

// B3: one block every 1 MB, up to this many MB.
#define ECBN_COLD_MAX   64

// B2: iterations per (n, region) unless the command says otherwise; the
// warm region's size in blocks.
#define ECBN_ITERS      200
#define ECBN_WARM_SPAN  256

// B4: frames unless the command says otherwise; a frame over this many us
// of streaming is counted as over budget (17 blocks of world in about 6 ms
// after gfx_wait() is what 60 fps leaves).
#define ECBN_SOAK       2000
#define ECBN_SOAK_BLK   17
#define ECBN_BUDGET_US  4000

// B8: the short stream timeout, the tries per block before giving up, and
// the forced-timeout rounds.
#define ECBN_SHORT_US   5000
#define ECBN_TRIES      400
#define ECBN_FORCED     20

// stream()'s timeout everywhere else: CHSd's default.
#define ECBN_TIMEOUT_US 1000000

// Unattended: with no driver on the debug protocol (lockstep off), the whole
// sequence starts by itself this long after boot. A debug build waits
// longer, so a script started with the upload takes over first.
#if CHGAME_DEBUG && !defined(CHSIM)
#define ECBN_AUTO_MS    6000
#else
#define ECBN_AUTO_MS    1500
#endif
