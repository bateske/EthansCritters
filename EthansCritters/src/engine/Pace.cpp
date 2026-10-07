// Frame pacing. See Pace.h.
#include "../Size.h"        // (first: Size.h)
#include <Arduino.h>
#include <chgame/Config.h>
#include "Card.h"
#include "Pace.h"

namespace pace {

#if CHGAME_DEBUG
static Frame f;                 // the stats only the debug overlay and Z read (RAM is the limit)
static Totals tot;
#endif
static uint32_t t0;
static uint8_t hist;            // over budget, a bit a drawn frame (bit 0 the latest)
static uint8_t winN, winOver;   // at 30 fps: the window's frames, and those over budget
static uint32_t winUs;          // ... and their time
static bool halfRate;

bool half() { return halfRate; }
bool lately() { return hist & 3; }

#ifdef CHSIM
static uint32_t charged;
void charge(uint32_t us) { charged += us; }
#endif

#if CHGAME_DEBUG
const Frame &last() { return f; }
const Totals &totals() { return tot; }
void clearTotals() { tot = Totals(); }
#endif

void reset() {
    hist = 0;
    halfRate = false;
#if CHGAME_DEBUG
    clearTotals();
#endif
}

void start() {
    t0 = micros();
#if CHGAME_DEBUG
    card::used.cmds = card::used.blocks = 0;
#endif
}

ECRT_OUTLINE uint32_t elapsed() {
#ifdef CHSIM
    return micros() - t0 + charged;
#else
    return micros() - t0;
#endif
}

void end() {
    uint32_t us = micros() - t0;    // unsigned: right across micros() wrapping
#ifdef CHSIM
    us += charged;
    charged = 0;
#endif
    bool over = us > BUDGET_US, was = halfRate;
    hist = (uint8_t)(hist << 1 | over);
    if (!halfRate) {
        uint32_t n = 0;
        for (uint32_t h = hist; h; h &= h - 1) n++;
        if (n >= 4) {
            halfRate = true;
            winN = winOver = 0;
            winUs = 0;
        }
    } else {
        winUs += us;
        winOver = (uint8_t)(winOver + over);
        if (++winN >= CALM_FRAMES) {
            if (winUs < (uint32_t)CALM_US * CALM_FRAMES && winOver <= 1) {
                halfRate = false;
                hist = 0;
            }
            winN = winOver = 0;
            winUs = 0;
        }
    }
#if CHGAME_DEBUG
    f.us = (uint16_t)(us > 0xFFFF ? 0xFFFF : us);
    f.cmds = (uint8_t)card::used.cmds;
    f.blocks = card::used.blocks;
    tot.frames++;
    tot.sumUs += us;
    if (us > tot.maxUs) tot.maxUs = us;
    tot.cmds += f.cmds;
    tot.blocks += f.blocks;
    tot.half += was;
    tot.over += over;
    tot.switches += was != halfRate;
#endif
}

}  // namespace pace
