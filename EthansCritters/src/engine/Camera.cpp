// The camera. See Camera.h.
#include "../Size.h"        // (first: Size.h)
#include "../assets/WorldData.h"
#include "Camera.h"

namespace camera {

static const int32_t VIEW_W = 128, VIEW_H = 120;
static const int32_t DEAD_X = 8 << 4, DEAD_Y = 6 << 4;     // half the dead zone, Q4
static const int32_t LEAD_X = 12 << 4, LEAD_Y = 8 << 4;    // how far ahead it looks, Q4
static const int32_t LEAD_EASE = 6;                        // Q4 a tick: the lead swings round in about a second

int16_t x, y;
static int32_t focusX, focusY;          // the focus (the view's middle), Q4
static int16_t lx, ly;                  // the lead now, Q4
static uint8_t shakeT, shakeAmp;
static int16_t bx0, by0, bx1 = WORLD_W, by1 = WORLD_H;  // where the view may go

static int32_t clampI(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

// The view from the focus and the shake, inside the world.
static void place() {
    int32_t vx = (focusX >> 4) - VIEW_W / 2, vy = (focusY >> 4) - VIEW_H / 2;
    if (shakeT) {
        int32_t a = (shakeAmp * shakeT + 9) / 10;
        if (a < 1) a = 1;
        vx += (shakeT & 2) ? a : -a;
        vy += (shakeT & 1) ? a : -a;
    }
    x = (int16_t)clampI(vx, bx0, bx1 - VIEW_W);
    y = (int16_t)clampI(vy, by0, by1 - VIEW_H);
}

ECRT_OUTLINE void bound(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    bx0 = x0;
    by0 = y0;
    bx1 = x1;
    by1 = y1;
}

// The focus may not wander past where the view would leave the world (else
// the dead zone would have to be walked back across at the edges).
static void clampFocus() {
    focusX = clampI(focusX, (bx0 + VIEW_W / 2) << 4, (bx1 - VIEW_W / 2) << 4);
    focusY = clampI(focusY, (by0 + VIEW_H / 2) << 4, (by1 - VIEW_H / 2) << 4);
}

void snap(int32_t tx, int32_t ty) {
    focusX = tx;
    focusY = ty - (8 << 4);                 // the squire's middle, not his feet
    lx = ly = 0;
    shakeT = 0;
    clampFocus();
    place();
}

ECRT_OUTLINE static int32_t follow(int32_t f, int32_t t, int32_t dead) {
    int32_t d = t - f;
    return d > dead ? t - dead : d < -dead ? t + dead : f;
}

ECRT_OUTLINE static int16_t ease(int16_t v, int32_t to) {
    return (int16_t)(v < to ? (to - v > LEAD_EASE ? v + LEAD_EASE : to) : (v - to > LEAD_EASE ? v - LEAD_EASE : to));
}

void update(int32_t tx, int32_t ty, int8_t faceX, int8_t faceY) {
    lx = ease(lx, faceX * LEAD_X);
    ly = ease(ly, faceY * LEAD_Y);
    focusX = follow(focusX, tx + lx, DEAD_X);
    focusY = follow(focusY, ty - (8 << 4) + ly, DEAD_Y);
    clampFocus();
    if (shakeT) shakeT--;
    place();
}

ECRT_OUTLINE void shake(uint8_t ticks, uint8_t amp) {
    shakeT = ticks;
    shakeAmp = amp;
}

}  // namespace camera
