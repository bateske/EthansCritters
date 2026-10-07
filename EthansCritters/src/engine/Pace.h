// Frame pacing and the frame stats: logic always ticks at 60 Hz; the
// screen is drawn every tick (60 fps) while the work after gfx_wait() fits,
// else every second tick (30 fps) until it fits comfortably again.
//
// At 60 fps a frame has 16.67 ms: the flush holds SPI1 for 8.41 ms and
// starts in 0.1 ms, which leaves ~8.2 ms from gfx_wait() to the next
// flush (the world's window alone is ~4.6 ms of it on the board). Over
// BUDGET_US in 4 of the last 8 drawn frames: 30 fps. Back to 60 after a
// window of CALM_FRAMES drawn frames (2 s at 30 fps) that averaged under
// CALM_US with at most one frame over BUDGET_US, so a screen near the line
// does not flip back and forth, while a card that is only sometimes slow
// (a stray slow command) keeps 60. Drawing at 30 fps leaves ~25 ms after
// gfx_wait(); logic timing (START held, repeat()) stays right because the
// ticks do not slow down.
//
// A drawn frame: start() just after gfx_wait(), end() just before the
// flush. The card's commands and blocks come from card::used.
#pragma once
#include <stdint.h>

namespace pace {

static const uint16_t BUDGET_US = 7900;     // over this after gfx_wait(), 60 fps misses the flush slot
static const uint16_t CALM_US = 6900;       // a millisecond under it, on average
static const uint8_t CALM_FRAMES = 60;
// What the card costs on the board (CLAUDE.md): a command (sd::stream at a
// random offset) and each block it reads. The frame's card work is planned
// with these (the frame cache's budget, the occluders' passes).
static const uint16_t CMD_US = 832, BLK_US = 222;

void reset();                   // 60 fps, fresh history (a new screen)
void start();                   // just after gfx_wait()
void end();                     // just before the flush
uint32_t elapsed();             // microseconds since start() (the simulator: the drawing charged so far not included)
bool half();                    // draw every second tick (30 fps)
bool lately();                  // one of the last two drawn frames went over BUDGET_US
#ifdef CHSIM
// The simulator charges the card's time but not the drawing's: a screen
// adds its estimate of the board's drawing time to this frame's (Play.h).
void charge(uint32_t us);
#endif

// The stats (debug builds only: the overlay and Z read them). The last
// drawn frame: microseconds from start() to end(), card commands and blocks.
struct Frame { uint16_t us, blocks; uint8_t cmds; };
const Frame &last();

// Since reset() or clearTotals(): frames drawn, of them at 30 fps and over
// BUDGET_US, mode switches, the sum and worst of the frame times, card
// commands and blocks.
struct Totals { uint32_t frames, half, over, switches, sumUs, maxUs, cmds, blocks; };
const Totals &totals();
void clearTotals();

}  // namespace pace
