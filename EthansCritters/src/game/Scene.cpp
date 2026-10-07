// The play frame's sprites. See Scene.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGfx.h>
#include "../assets/Bank.h"
#include "../assets/Palette.h"
#include "../engine/Spool.h"
#include "../../config.h"
#include "Scene.h"
#ifdef CHSIM
#include "sim.h"                    // sim_bug
#endif

namespace scene {

#ifdef CHSIM
uint32_t simCost;

uint32_t blitCost(uint32_t wr, uint32_t flags) {
#if ECRT_SIM_P5
    // CHGfx P5 (M8b), estimated from its disassembly, not yet measured on the board
    uint32_t k = 101, c = 10;
    if (flags & GFX_B4_FLIPH) k += 31;
    if (flags & (GFX_B4_REMAP | GFX_B4_SOLID)) k += 55, c += 12;
    if (flags & GFX_B4_DITHER) k += 25;
    return c + wr * k / 100;
#else
    // board (P4): 32x32 (4 words x 32 rows) 149 us plain, 237 flipped, 455 remapped, 180 dithered
    uint32_t k = 110;
    if (flags & GFX_B4_FLIPH) k += 70;
    if (flags & (GFX_B4_REMAP | GFX_B4_SOLID)) k += 240;
    if (flags & GFX_B4_DITHER) k += 25;
    return 10 + wr * k / 100;
#endif
}
#endif
Rect drawn;

void clearDrawn() {
    drawn.x0 = drawn.y0 = 0x7FFF;
    drawn.x1 = drawn.y1 = -0x7FFF;
}

void List::add(int x, int y, uint8_t what, uint8_t i) {
    if (n >= MAX_ITEMS) return;
    uint32_t j = n++;
    for (; j && it[j - 1].y > y; j--) it[j] = it[j - 1];
    it[j].x = (int16_t)x;
    it[j].y = (int16_t)y;
    it[j].what = what;
    it[j].i = i;
}

bool onScreen(int x, int y, int camX, int camY) {
    return x > camX - 24 && x < camX + GFX_W + 24 && y > camY - 2 && y < camY + 120 + 32;
}

void grow(int x, int y, int w, int h) {
    if (x < drawn.x0) drawn.x0 = (int16_t)x;
    if (y < drawn.y0) drawn.y0 = (int16_t)y;
    if (x + w > drawn.x1) drawn.x1 = (int16_t)(x + w);
    if (y + h > drawn.y1) drawn.y1 = (int16_t)(y + h);
}

Prim *rec, *recEnd;
bool recReal;

static void draw(const Prim &q) {
    const uint8_t *p = q.p;
#ifdef CHSIM
    simCost += blitCost(p[0] * p[1], q.flags);
#endif
    gfx_blit4k(p + BANK_FRAME_HEADER, q.x, q.y, p[0], p[1], q.flags, q.rot ? REMAP_ROT : REMAP_RED, q.level);
}

void blit(const uint8_t *p, int sx, int sy, int8_t face, uint8_t flags, const uint8_t *remap, uint8_t level) {
    int ww = p[0], ox = (int8_t)p[2], oy = (int8_t)p[3];
    // mirrored about the pivot (d -> -1 - d) over the padded width
    if (face < 0) flags |= GFX_B4_FLIPH;
    int x = face < 0 ? sx - ox - ww * 8 : sx + ox, y = sy + oy;
    grow(x, y, ww * 8, p[1]);
#ifdef CHSIM
    if ((flags & GFX_B4_REMAP) && remap != REMAP_RED && remap != REMAP_ROT) sim_bug("scene::blit: a remap a Prim cannot keep");
#endif
    Prim q = {p, (int16_t)x, (int16_t)y, flags, level, remap == REMAP_ROT, 0};
    if (!rec) draw(q);
    else if (rec < recEnd) *rec++ = q;
    else recReal = true;
}

void play(const Prim *a, const Prim *e) {
    clearDrawn();
    for (; a < e; a++) {
        grow(a->x, a->y, a->p[0] * 8, a->p[1]);
        draw(*a);
    }
}

const uint8_t *frameOr(uint8_t c, uint8_t f, uint8_t &lc, uint8_t &lf) {
    const uint8_t *p = spool::peek(c, f);
    if (p) {
        lc = c;
        lf = f;
    } else if (lc != 0xFF) {
        p = spool::peek(lc, lf);
    }
    return p;
}

void shadow(int sx, int sy, int hw) {
#ifdef CHSIM
    simCost += 6 + (uint32_t)(6 * hw - 4) * 26 / 100;     // board: 24x8 remapRect 50 us
#endif
    gfx_remapRect(sx - hw, sy - 1, 2 * hw, 2, REMAP_DARK);
    gfx_remapRect(sx - hw + 2, sy + 1, 2 * hw - 4, 1, REMAP_DARK);
}

}  // namespace scene
