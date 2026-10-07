// B5: what the game's drawing will cost, per call (micros() over many calls;
// the simulator's clock stands still while it draws, so there it reads 0):
// CHGfx's keyed blit, CHGame's span sprites, the remap, a full-screen copy of
// 16 card blocks, the prototype of the world's column copy, and CHGfx patch
// P4's gfx_blit4k (the game's sprite blitter) on the same 32x32 sprite as
// gfx_blit, every flag (since CHGfx P5 remap and ink go through a byte map,
// whose 256-byte table each such call builds first: the 24x21 pair shows
// what that costs a critter-sized frame). All of it between gfx_wait() and
// the flush, the frame's drawing window.
#pragma GCC optimize("Os")
#include "Bench.h"

namespace bench {

// A 24x21 critter-sized span4 sprite (outline, banded body, an eye), about
// 7 runs a row: in flash, as a CHGame game keeps its sprites.
static const uint8_t SPR24[] = {
    0x18, 0x15, 0x02, 0x7F, 0x71, 0x05, 0x5F, 0x21, 0x34, 0x15, 0x21, 0x06, 0x4F, 0x11, 0x34, 0x45,
    0x06, 0x11, 0x06, 0x2F, 0x11, 0x34, 0x45, 0x46, 0x11, 0x07, 0x2F, 0x01, 0x24, 0x45, 0x46, 0x27,
    0x01, 0x08, 0x1F, 0x01, 0x14, 0x45, 0x46, 0x47, 0x02, 0x01, 0x08, 0x0F, 0x11, 0x45, 0x46, 0x17,
    0x20, 0x22, 0x11, 0x09, 0x0F, 0x01, 0x35, 0x46, 0x37, 0x20, 0x22, 0x03, 0x01, 0x0A, 0x0F, 0x01,
    0x15, 0x46, 0x47, 0x02, 0x20, 0x02, 0x23, 0x01, 0x07, 0x0F, 0x01, 0x46, 0x47, 0x42, 0x43, 0x01,
    0x07, 0x11, 0x26, 0x47, 0x42, 0x43, 0x14, 0x11, 0x08, 0x0F, 0x01, 0x06, 0x47, 0x42, 0x43, 0x34,
    0x01, 0x08, 0x0F, 0x01, 0x37, 0x42, 0x43, 0x44, 0x05, 0x01, 0x08, 0x0F, 0x01, 0x17, 0x42, 0x43,
    0x44, 0x25, 0x01, 0x07, 0x0F, 0x11, 0x32, 0x43, 0x44, 0x35, 0x11, 0x08, 0x1F, 0x01, 0x12, 0x43,
    0x44, 0x45, 0x06, 0x01, 0x07, 0x2F, 0x01, 0x33, 0x44, 0x45, 0x16, 0x01, 0x07, 0x2F, 0x11, 0x03,
    0x44, 0x45, 0x26, 0x11, 0x06, 0x4F, 0x11, 0x14, 0x45, 0x26, 0x11, 0x05, 0x5F, 0x21, 0x25, 0x26,
    0x21, 0x02, 0x7F, 0x71,
};

// The plan's red hit flash: shading kept, hues pushed to rust.
static const uint8_t HIT[16] = {0, 2, 2, 14, 14, 5, 2, 2, 14, 14, 14, 14, 2, 14, 5, 15};

// A word copy (newlib's memcpy is a byte loop in flash): Path F's frames and
// any block that lands row-major.
RAMFUNC(ecbnCopy) static void wordCopy(uint32_t *d, const uint32_t *s, uint32_t words) {
    for (uint32_t *e = d + words; d < e; d += 4, s += 4) {
        uint32_t a = s[0], b = s[1], c = s[2], f = s[3];
        d[0] = a; d[1] = b; d[2] = c; d[3] = f;
    }
}

// The world streamer's copy (the plan's "column copy"): rows r0..r0+119 of
// a column block (128 rows of 4 bytes, 8 px a row, low nibble first) into
// framebuffer rows 8..127 at pixel x (-7..127). At x % 8 == 0 it is one
// word store a row; otherwise the 8 px straddle two framebuffer words, and
// a funnel shift puts them in with a read-modify-write of each, the pixels
// either side kept (the neighbouring columns write those).
RAMFUNC(ecbnColumn) void colCopy(const uint32_t *blk, int x, uint32_t r0) {
    const uint32_t *s = blk + r0, *e = s + 120;
    uint32_t *d = (uint32_t *)gfx_fb + 8 * 16 + (x >> 3);    // 16 words a row; x >> 3 is -1 for x < 0
    uint32_t sh = ((uint32_t)x & 7) * 4;
    if (!sh) {
        for (; s < e; s++, d += 16) *d = *s;
        return;
    }
    uint32_t lo = (1u << sh) - 1, rs = 32 - sh;
    if (x < 0) {                                             // only the right-hand word is on screen
        for (; s < e; s++, d += 16) d[1] = (d[1] & ~lo) | (*s >> rs);
    } else if (x > 120) {                                    // only the left-hand word
        for (; s < e; s++, d += 16) d[0] = (d[0] & lo) | (*s << sh);
    } else {
        for (; s < e; s++, d += 16) {
            uint32_t v = *s;
            d[0] = (d[0] & lo) | (v << sh);
            d[1] = (d[1] & ~lo) | (v >> rs);
        }
    }
}

// What the column-copy check expects at world pixel (wx, row).
static uint32_t worldPx(uint32_t wx, uint32_t row) { return (wx * 3 + row * 5 + (wx >> 3)) & 15; }

// Column block c of that test world, into blk.
static void makeColumn(uint32_t *blk, uint32_t c) {
    for (uint32_t r = 0; r < 128; r++) {
        uint32_t w = 0;
        for (uint32_t j = 0; j < 8; j++) w |= worldPx(c * 8 + j, r) << (4 * j);
        blk[r] = w;
    }
}

// The screen at camera x0 (rows r0..r0+119 of the band) built from 16 or 17
// column blocks, against worldPx: the number of wrong pixels.
static uint32_t checkWorld(uint32_t *blk, uint32_t x0, uint32_t r0) {
    gfx_clear(0);
    for (uint32_t c = x0 >> 3; c * 8 < x0 + 128; c++) {
        makeColumn(blk, c);
        colCopy(blk, (int)(c * 8) - (int)x0, r0);
    }
    uint32_t bad = 0;
    for (uint32_t y = 8; y < 128; y++)
        for (uint32_t x = 0; x < 128; x++) bad += gfx_getPixel((int)x, (int)y) != worldPx(x0 + x, r0 + y - 8);
    return bad;
}

// Tenths of a us per call since t0.
static uint32_t per(uint32_t t0, uint32_t calls) { return (micros() - t0) * 10 / calls; }

static const uint32_t CALLS = 256;

void drawBench() {
    show(PG_DRAW);
    gfx_wait();
    uint8_t *spr = gfx_chunkScratch();                       // 32x32 4 bpp: the first half
    uint32_t *blk = (uint32_t *)(spr + GFX_CHUNK_BYTES);     // a column block: the second
    // A round 32x32 sprite, 15 (the key) outside the circle.
    for (uint32_t y = 0; y < 32; y++)
        for (uint32_t x = 0; x < 32; x += 2) {
            uint32_t a = (x - 15) * (x - 15) + (y - 15) * (y - 15) < 225 ? (x + y) & 7 : 15;
            uint32_t b = (x - 14) * (x - 14) + (y - 15) * (y - 15) < 225 ? (x + y + 1) & 7 : 15;
            spr[y * 16 + x / 2] = (uint8_t)(a | b << 4);
        }
    uint32_t *r = R.draw, t0;
    gfx_clear(0);

    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit(spr, 40, 40, 32, 32, 15);
    r[D_BLIT_EVEN] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit(spr, 41, 40, 32, 32, 15);
    r[D_BLIT_ODD] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) sprite4(SPR24, 50, 50);
    r[D_SPR] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) sprite4(SPR24, 50, 50, HIT);
    r[D_SPR_REMAP] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) sprite4(SPR24, 51, 50, nullptr, 256, SPR_FLIP_H);
    r[D_SPR_FLIP] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_remapRect(50, 60, 24, 8, HIT);
    r[D_REMAP] = per(t0, CALLS);
    // gfx_blit4k: the 32x32 sprite is 4 words a row already (key 15)
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 40, 40, 4, 32, 0);
    r[D_B4_EVEN] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 4, 32, 0);
    r[D_B4_ODD] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 4, 32, GFX_B4_FLIPH);
    r[D_B4_FLIP] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 4, 32, GFX_B4_REMAP, HIT);
    r[D_B4_REMAP] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 4, 32, GFX_B4_DITHER, nullptr, 8);
    r[D_B4_DITHER] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 4, 32, GFX_B4_REMAP | GFX_B4_FLIPH, HIT);
    r[D_B4_RMFLIP] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 4, 32, GFX_B4_SOLID | GFX_B4_INK(5));
    r[D_B4_SOLID] = per(t0, CALLS);
    // a critter-sized frame, 3 words x 21 rows (the same bytes read 12 a row), plain and remapped
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 3, 21, 0);
    r[D_B4_24] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) gfx_blit4k(spr, 43, 40, 3, 21, GFX_B4_REMAP, HIT);
    r[D_B4_24RM] = per(t0, CALLS);

    // 16 blocks straight into the framebuffer (a Path F frame), per screen.
    for (uint32_t i = 0; i < 128; i++) blk[i] = i * 0x01010101u;
    t0 = micros();
    for (uint32_t i = 0; i < 16; i++)
        for (uint32_t b = 0; b < 16; b++) wordCopy((uint32_t *)gfx_fb + b * 128, blk, 128);
    r[D_COPY16] = per(t0, 16);

    // Column copies: one block (120 rows), then whole screens of 16 (x % 8
    // == 0) or 17 (x % 8 == 3) blocks.
    makeColumn(blk, 5);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) colCopy(blk, 48, 3);
    r[D_COL0] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < CALLS; i++) colCopy(blk, 51, 3);
    r[D_COLP] = per(t0, CALLS);
    t0 = micros();
    for (uint32_t i = 0; i < 16; i++)
        for (int x = 0; x < 128; x += 8) colCopy(blk, x, 3);
    r[D_WORLD0] = per(t0, 16);
    t0 = micros();
    for (uint32_t i = 0; i < 16; i++)
        for (int x = -5; x < 128; x += 8) colCopy(blk, x, 3);
    r[D_WORLDP] = per(t0, 16);

    // The copy is right at every phase (a test, not timed).
    R.colBad = 0;
    for (uint32_t x0 = 72; x0 < 80; x0++) R.colBad += checkWorld(blk, x0, x0 - 72);   // r0 0..7

    // The flush: the CPU's part of starting one, then the wire.
    t0 = micros();
    gfx_flushAsync();
    r[D_FLUSH_START] = per(t0, 1);
    t0 = micros();
    gfx_wait();
    r[D_FLUSH_WIRE] = per(t0, 1);

    static const char *const NAME[DRAW_N] = {
        "blit32_even", "blit32_odd", "sprite4_24x21", "sprite4_remap", "sprite4_fliph", "remaprect_24x8",
        "copy16_screen", "column_p0", "column_p3", "world16_screen", "world17_screen", "flush_start",
        "flush_wire", "blit4k32_even", "blit4k32_odd", "blit4k32_fliph", "blit4k32_remap", "blit4k32_dither8",
        "blit4k32_rmflip", "blit4k32_solid", "blit4k24_odd", "blit4k24_remap",
    };
    for (uint32_t i = 0; i < DRAW_N; i++) {
        char *p = line("B5");
        p = ks(p, "what", NAME[i]);
        p = kd(p, "us", (int32_t)r[i]);
        emit(p);
    }
    emit(kv(line("B5 column_check"), "bad_px", (int32_t)R.colBad));
    R.have |= 1 << 5;
    show(PG_DRAW);
}

}  // namespace bench
