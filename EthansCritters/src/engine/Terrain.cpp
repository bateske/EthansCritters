// The ground and moving over it. See Terrain.h.
#include "../Size.h"        // (first: Size.h)
#include "Terrain.h"

namespace terrain {

static const int NUDGE = 3;             // px: how far round a corner a blocked mover is helped

BlockFn blocker;

// The table is run-length coded by rows (WorldData.h), so a cell is found by
// walking its row's runs. A box asks for its cells a row or two at a time,
// so the last run found on an even and on an odd row is kept and the next
// walk along that row starts there, either way: on recorded play (8-12
// critters, walking) that is 0.2-0.3 steps a lookup, against 3 (at most
// 10) from the row's start. Zeroed, both marks are valid: row 0's first
// run starts at x 0.
struct Mark { uint16_t run; uint8_t row, x0; };
static Mark mark[2];

uint8_t at(int x, int y) {
    if ((unsigned)x >= (unsigned)WORLD_W || (unsigned)y >= (unsigned)WORLD_H) return T_SOLID;
    uint32_t cx = (uint32_t)x >> 3, cy = (uint32_t)y >> 3;
    Mark &m = mark[cy & 1];
    uint32_t i = m.run, x0 = m.x0;
    if (m.row != cy) {
        i = TERRAIN_ROW[cy];
        x0 = 0;
        m.row = (uint8_t)cy;
    }
    while (cx < x0) x0 -= (TERRAIN_RUNS[--i] >> 2) + 1u;
    for (uint32_t n; cx >= x0 + (n = (TERRAIN_RUNS[i] >> 2) + 1u); i++) x0 += n;
    m.run = (uint16_t)i;
    m.x0 = (uint8_t)x0;
    return TERRAIN_RUNS[i] & 3;
}

bool hits(int x0, int y0, int x1, int y1, uint8_t block) {
    for (int y = y0 & ~7; y < y1; y += 8)       // each cell the box touches once
        for (int x = x0 & ~7; x < x1; x += 8)
            if (block & tbit(at(x, y))) return true;
    return false;
}

static bool loose;                      // this move began inside a blocker: blockers do not stop it

// 1: the box meets the ground it may not enter, 2: a blocker (unless loose), 0: neither.
static uint8_t boxHits(int32_t x, int32_t y, uint8_t hw, uint8_t h, uint8_t block) {
    int px = (int)(x >> 4), py = (int)(y >> 4);
    if (hits(px - hw, py - h, px + hw, py, block)) return 1;
    return blocker && !loose && blocker(px - hw, py - h, px + hw, py) ? 2 : 0;
}

// One axis: a step of d along it (ax: x if the axis is x), the other axis's
// position o. Returns true if blocked; then the mover goes up to the wall
// a pixel at a time (a step over 1 px, walking or knocked back, would else
// stop it 1-2 px short), and is nudged sideways (n) if a step of up to
// NUDGE px that way would have let it through.
static bool axis(int32_t &a, int32_t &o, int32_t d, bool isX, uint8_t hw, uint8_t h, uint8_t block,
                 bool nudge) {
    auto hit = [&](int32_t pa, int32_t po) {
        return isX ? boxHits(pa, po, hw, h, block) : boxHits(po, pa, hw, h, block);
    };
    if (!hit(a + d, o)) {
        a += d;
        return false;
    }
    int32_t px = d > 0 ? 16 : -16;
    a = d > 0 ? (a | 15) : (a & ~15);
    while (!hit(a + px, o)) a += px;    // at the latest before a + d's pixel, which hits
    if (nudge) {
        int32_t step = d < 0 ? -d : d;
        for (int n = 1; n <= NUDGE; n++) {
            int32_t s = !hit(a + d, o - 16 * n) ? -step : !hit(a + d, o + 16 * n) ? step : 0;
            if (s) {
                if (!hit(a, o + s)) o += s;
                break;
            }
        }
    }
    return true;
}

uint8_t move(int32_t &x, int32_t &y, int32_t dx, int32_t dy, uint8_t hw, uint8_t h, uint8_t block, bool nudge) {
    loose = false;
    uint8_t in = boxHits(x, y, hw, h, block);
    if (in == 1) {                      // already inside the ground (a warp, a spawn): free to walk out
        x += dx;
        y += dy;
        return 0;
    }
    // inside a blocker (the toad came down on him): out of it as if there were none, the ground still
    // stopping him (he used to go anywhere while he overlapped it: through walls, over deep water)
    loose = in;
    uint8_t blocked = 0;
    if (dx && axis(x, y, dx, true, hw, h, block, nudge && !dy)) blocked |= 1;
    if (dy && axis(y, x, dy, false, hw, h, block, nudge && !dx)) blocked |= 2;
    return blocked;
}

}  // namespace terrain
