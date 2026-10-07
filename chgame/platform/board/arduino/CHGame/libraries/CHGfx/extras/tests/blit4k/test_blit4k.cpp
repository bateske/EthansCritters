/*
 * gfx_blit4k() against a pixel-at-a-time reference: random sprites (key
 * density from none to most), every x phase and far off every edge, both
 * flips, random remaps (entries of 15 included), solid ink, every dither
 * level, and random clip rectangles (empty ones, single columns, every
 * edge). The byte map that remap and ink go through gets every pixel pair
 * (sprites holding each byte value once) with both flips at every x phase,
 * tables with entries above 15, a table changed between calls, and REMAP
 * without a table. The whole framebuffer is compared after each call, so a
 * stray store outside the sprite or the clip fails too.
 *
 * Self-contained, so in a folder of its own (chsim.py test links every
 * extras/tests/*.cpp with the whole library into one program): only
 * CHGfx_blit4k.cpp is compiled with it, under CHSIM (no SRAM sections),
 * with the simulator's Arduino.h:
 *   c++ -std=gnu++17 -O2 -DCHSIM -DCH32X035 -I<CHGfx>/src -I<repo>/tools/chsim/host
 *       extras/tests/blit4k/test_blit4k.cpp <CHGfx>/src/CHGfx_blit4k.cpp
 * Arguments: --quick (fewer cases).
 */
#include <CHGfx.h>
#include <CHGfx_internal.h>
#include <stdio.h>
#include <string.h>

uint8_t gfx_fb[GFX_FB_BYTES] __attribute__((aligned(4)));
GfxClip gfx__clip = {0, 0, GFX_W, GFX_H};

static uint8_t want[GFX_FB_BYTES];
static uint32_t rng = 0x1234567u;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int rr(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

static uint8_t px(const uint8_t *fb, int x, int y) { return (fb[y * GFX_FB_STRIDE + (x >> 1)] >> ((x & 1) * 4)) & 15; }
static void put(uint8_t *fb, int x, int y, uint8_t c) {
    uint8_t &b = fb[y * GFX_FB_STRIDE + (x >> 1)];
    b = (x & 1) ? (uint8_t)((b & 0x0F) | (c << 4)) : (uint8_t)((b & 0xF0) | c);
}

static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* What gfx_blit4k() is documented to do, one pixel at a time. */
static void reference(const uint8_t *src, int x, int y, int ww, int h, uint8_t fl, const uint8_t *remap,
                      uint8_t level) {
    const GfxClip k = gfx__clip;
    int lv = (fl & GFX_B4_DITHER) ? (level > 16 ? 16 : level) : 16;
    int w = ww * 8;
    for (int r = 0; r < h; r++)
        for (int j = 0; j < w; j++) {
            int sj = (fl & GFX_B4_FLIPH) ? w - 1 - j : j;
            uint8_t v = (src[r * ww * 4 + (sj >> 1)] >> ((sj & 1) * 4)) & 15;
            if ((fl & GFX_B4_REMAP) && remap && v != 15) v = remap[v] & 15;   /* (nullptr: tolerated, no remap) */
            if (v == 15 || ((fl & GFX_B4_SOLID) && (fl >> 4) == 15)) continue;     /* ink 15: the key */
            int X = x + j, Y = y + r;
            if (X < k.x0 || X >= k.x1 || Y < k.y0 || Y >= k.y1) continue;
            if (BAYER[Y & 3][X & 3] >= lv) continue;
            put(want, X, Y, (fl & GFX_B4_SOLID) ? (uint8_t)(fl >> 4) : v);
        }
}

static int checks, fails;
static uint32_t buf[16 * 40 + 1];

/* The blit of buf against the reference, on a random framebuffer. */
static void check(int ww, int h, int x, int y, uint8_t fl, const uint8_t *remap, uint8_t level, const char *what) {
    const uint8_t *src = (const uint8_t *)buf;
    for (int i = 0; i < GFX_FB_BYTES; i++) gfx_fb[i] = want[i] = (uint8_t)rnd();
    reference(src, x, y, ww, h, fl, remap, level);
    gfx_blit4k(src, x, y, ww, h, fl, remap, level);
    checks++;
    if (memcmp(gfx_fb, want, GFX_FB_BYTES)) {
        if (fails++ < 10) {
            int bx = 0, by = 0;
            for (int i = 0; i < GFX_W * GFX_H; i++)
                if (px(gfx_fb, i % GFX_W, i / GFX_W) != px(want, i % GFX_W, i / GFX_W)) { bx = i % GFX_W; by = i / GFX_W; break; }
            printf("FAIL %s: w%d h%d at %d,%d flags %02X level %d clip %d,%d-%d,%d: first wrong pixel %d,%d is %d, want %d\n",
                   what, ww, h, x, y, fl, level, gfx__clip.x0, gfx__clip.y0, gfx__clip.x1, gfx__clip.y1, bx, by,
                   px(gfx_fb, bx, by), px(want, bx, by));
        }
    }
}

/* A random sprite (keyPct% of its pixels 15), then check(). */
static void one(int ww, int h, int x, int y, uint8_t fl, const uint8_t *remap, uint8_t level, int keyPct,
                const char *what) {
    uint8_t *src = (uint8_t *)buf;
    for (int i = 0; i < ww * 4 * h; i++) {
        uint8_t b = 0;
        for (int n = 0; n < 2; n++) b |= (uint8_t)((rr(0, 99) < keyPct ? 15 : rr(0, 14)) << (4 * n));
        src[i] = b;
    }
    check(ww, h, x, y, fl, remap, level, what);
}

/* Every byte value once, shuffled: 16 words x 4 rows, every pixel pair the byte map has. */
static void allPairs() {
    uint8_t *src = (uint8_t *)buf;
    for (int i = 0; i < 256; i++) src[i] = (uint8_t)i;
    for (int i = 255; i > 0; i--) {
        int k = rr(0, i);
        uint8_t t = src[i]; src[i] = src[k]; src[k] = t;
    }
}

static void setClip(int x0, int y0, int x1, int y1) {
    gfx__clip.x0 = (int16_t)x0; gfx__clip.y0 = (int16_t)y0;
    gfx__clip.x1 = (int16_t)x1; gfx__clip.y1 = (int16_t)y1;
}

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "--quick");
    uint8_t remap[16];

    /* Every x phase and every flag combination, unclipped, near the middle and across every edge. */
    for (int fl = 0; fl < 16; fl++)
        for (int x = -40; x < 140; x += quick ? 7 : 1) {
            for (int i = 0; i < 16; i++) remap[i] = (uint8_t)rr(0, 15);
            setClip(0, 0, GFX_W, GFX_H);
            uint8_t f = (uint8_t)(fl | (rr(0, 15) << 4));
            one(rr(1, 6), rr(1, 24), x, rr(-30, 130), f, remap, (uint8_t)rr(0, 17), rr(0, 100), "phase");
        }

    /* Random clips: empty, one column, one row, each edge inside a word. */
    int n = quick ? 3000 : 30000;
    for (int i = 0; i < n; i++) {
        int x0 = rr(0, 128), x1 = rr(x0, 128), y0 = rr(0, 128), y1 = rr(y0, 128);
        if (rr(0, 9) == 0) x1 = x0 + (x0 < 128);              /* one column */
        if (rr(0, 19) == 0) x1 = x0;                          /* empty */
        setClip(x0, y0, x1, y1);
        for (int j = 0; j < 16; j++) remap[j] = (uint8_t)rr(0, 15);
        uint8_t f = (uint8_t)rr(0, 255);
        int ww = rr(1, 16);
        one(ww, rr(0, 40), rr(-ww * 8 - 4, 132), rr(-44, 132), f, remap, (uint8_t)rr(0, 16), rr(0, 100), "clip");
    }

    /* The byte map (remap and ink): every pixel pair, both flips, every x phase, remap entries
     * above 15 (only the low nibble counts) and of 15 (transparent), entry 15 not 15 (ignored),
     * every ink including 15 (nothing). */
    setClip(0, 0, GFX_W, GFX_H);
    for (int i = 0; i < (quick ? 64 : 640); i++) {
        static const uint8_t FL[6] = {GFX_B4_REMAP, GFX_B4_SOLID, GFX_B4_REMAP | GFX_B4_SOLID,
                                      GFX_B4_REMAP | GFX_B4_DITHER, GFX_B4_SOLID | GFX_B4_DITHER, GFX_B4_REMAP};
        for (int j = 0; j < 16; j++) remap[j] = (uint8_t)(rr(0, 3) ? rr(0, 15) : rnd());
        allPairs();
        uint8_t f = (uint8_t)(FL[i % 6] | (i / 6 % 2 ? GFX_B4_FLIPH : 0) | (rr(0, 15) << 4));
        check(16, 4, (i % 8) + 8 * rr(-2, 14), rr(-2, 126), f, remap, (uint8_t)rr(0, 16), "map");
    }

    /* The same table changed between calls, and flipped and not in turn: nothing kept from the last call. */
    for (int i = 0; i < 48; i++) {
        for (int j = 0; j < 16; j++) remap[j] = (uint8_t)rr(0, 15);
        allPairs();
        check(16, 4, 3 + i, 40, (uint8_t)(GFX_B4_REMAP | (i & 1)), remap, 16, "map again");
    }

    /* REMAP with no table: drawn plain (or inked), as the reference does. */
    for (int i = 0; i < 32; i++)
        one(rr(1, 8), rr(1, 30), rr(-10, 100), rr(-10, 100),
            (uint8_t)(GFX_B4_REMAP | (i & 1) | (i & 2 ? GFX_B4_SOLID | GFX_B4_INK(rr(0, 15)) : 0)), nullptr, 16,
            rr(0, 60), "no table");

    /* Each dither level alone over a solid block: exactly level/16 of the pixels. */
    setClip(0, 0, GFX_W, GFX_H);
    for (int lv = 0; lv <= 16; lv++) {
        static uint32_t blk[4 * 16];
        memset(blk, 0x11, sizeof blk);
        memset(gfx_fb, 0, sizeof gfx_fb);
        gfx_blit4k((const uint8_t *)blk, 37, 21, 4, 16, GFX_B4_DITHER, nullptr, (uint8_t)lv);
        int lit = 0;
        for (int yy = 21; yy < 37; yy++)
            for (int xx = 37; xx < 69; xx++) lit += px(gfx_fb, xx, yy) == 1;
        checks++;
        if (lit != 32 * 16 * lv / 16) { fails++; printf("FAIL dither level %d: %d of 512 pixels\n", lv, lit); }
    }

    printf("blit4k: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
