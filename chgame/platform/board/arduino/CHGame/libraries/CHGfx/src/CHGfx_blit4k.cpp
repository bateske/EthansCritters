/*
 * CHGfx_blit4k.cpp - gfx_blit4k(): a keyed blit of raw 4 bpp words at any
 * x, with flip, remap, solid ink and an ordered-dither fade.
 *
 * Made for art streamed from an SD card into a RAM cache, where frames
 * arrive as whole words: a source row is wWords words, so a row lands in
 * the framebuffer as wWords + 1 word read-modify-writes whatever x is.
 * At x % 8 == s the output word is (cur << 4s) | (prev >> (32 - 4s)), the
 * classic funnel shift, with colour 15 shifted in on both sides.
 *
 * Transparency is found eight pixels at a time (SWAR): a nibble is 15
 * when all its bits are set, so
 *     t = c & c >> 1;  t &= t >> 2;  t &= 0x11111111
 * leaves bit 0 of each key nibble, and t * 15 is the key mask. The
 * opaque mask m = ~that, ANDed with the clip edges and the dither mask;
 * the store is (d & ~m) | (c & m), skipped when m is 0 (most words at a
 * sprite's edges).
 *
 * Split in two for RAM: the setup (clip, masks) runs once a call from
 * flash, and only the row loops sit in SRAM (each GFX_RAMFUNC costs its
 * size in RAM; all of it in SRAM was ~820 bytes, this is ~630). Each
 * source row is first copied into a row of words on the stack with a key
 * word either side, by one of three loops picked once a row: a word copy;
 * a mirror (words from the end, each word's nibbles reversed by a rotate
 * and two delta swaps); or a byte map for remap and ink. The merge loop
 * then funnels two neighbours into each output word (the previous one
 * kept in a register), visits only the columns inside the clip and has no
 * per-flag branches: ~26 instructions a word stored, ~21 a word left
 * alone.
 *
 * The byte map. Remap and ink fold into one colour map M (the remap, then
 * the ink wherever the result is not 15), and M[15] is always 15: the key
 * stays the key whatever the remap table says, and the table's entry 15
 * is never read. M is spread over a table of 256 bytes,
 *     T[lo | hi << 4] = M[lo] | M[hi] << 4
 * (M[hi] | M[lo] << 4 for a flipped blit), so a source byte, two pixels,
 * is one load from T: four a word, ~22 instructions with the stores, and
 * a flipped row is the same loop reading from the row's last byte back.
 * Before, the remap went a nibble at a time (~80 instructions a word) and
 * a remapped blit cost ~3x a plain one. Building T is 64 word stores, 16
 * colours times four low ones (~10 us with M); T lives on the stack of a
 * function of its own, so only remapped and inked blits pay its 256
 * bytes. An ink of 15 is the key: nothing shows.
 *
 * The row function keeps its frame pointer on purpose: with
 * -msave-restore GCC may otherwise save registers through __riscv_save_N,
 * which lives in flash.
 */
#include "CHGfx_internal.h"

#ifdef CHSIM
#define B4_NOSAVE
#else
#define B4_NOSAVE __attribute__((optimize("no-omit-frame-pointer")))
#endif

#define B4_MAXW (GFX_W / 8)
#define B4_FLIP 1u                  /* = GFX_B4_FLIPH */
#define B4_MAP  2u                  /* through the byte map T */

namespace {
struct B4Job {
    const uint32_t *row;            /* the first source row drawn */
    uint32_t *fb;                   /* its framebuffer row, at word column xw (output word 0) */
    uint8_t *tab;                   /* B4_MAP: T, 256 bytes, built by the row function */
    const uint8_t *remap;           /* B4_MAP: M from remap[0..14] ... */
    int words, rows, y;             /* source words a row; rows drawn; the first one's y */
    int oa, ob;                     /* output words inside the clip: [oa, ob), never empty */
    uint32_t sh, rs;                /* funnel: cur << sh | (prev >> 1) >> rs */
    uint32_t ma, mb;                /* clip masks of words oa and ob - 1 */
    uint32_t dither[4];             /* Bayer mask by y & 3 */
    uint32_t keep, ink;             /* ... then c (not 15) -> (c & keep) | ink */
    uint32_t mode;                  /* B4_FLIP, B4_MAP */
};
}

/* The key mask of eight pixels: 0xF in each nibble that is 15. */
static GFX_INLINE uint32_t keyMask(uint32_t c) {
    uint32_t q = c & (c >> 1);
    q &= q >> 2;
    return (q & 0x11111111u) * 15;
}

GFX_RAMFUNC(blit4k) B4_NOSAVE static void blit4kRows(const B4Job &j) {
    uint32_t rb[B4_MAXW + 2];       /* key, the row, key */
    int words = j.words;
    rb[0] = ~0u;
    rb[words + 1] = ~0u;
    if (j.mode & B4_MAP) {
        /* M in T's first 16 bytes */
        uint8_t *tab = j.tab;
        const uint8_t *rp = j.remap;
        uint32_t keep = j.keep, ink = j.ink;
        for (uint32_t i = 0; i < 15; i++) {
            uint32_t c = rp[i] & 15;
            if (c != 15) c = (c & keep) | ink;
            tab[i] = (uint8_t)c;
        }
        tab[15] = 15;
        /* T a word (a high colour and four low ones) at a time, from the
         * top down, so M is read before its bytes are overwritten */
        uint32_t f = (j.mode & B4_FLIP) * 4, *t = (uint32_t *)tab;
        uint32_t l0 = t[0] << f, l1 = t[1] << f, l2 = t[2] << f, l3 = t[3] << f;
        t += 64;
        for (const uint8_t *h = tab + 16; h != tab;) {
            uint32_t v = ((uint32_t)*--h << (4 - f)) * 0x01010101u;
            t -= 4;
            t[0] = v | l0;
            t[1] = v | l1;
            t[2] = v | l2;
            t[3] = v | l3;
        }
    }
    const uint32_t *row = j.row;
    uint32_t *fb = j.fb;
    int n = j.rows, y = j.y;
    do {
        uint32_t *w = rb + 1, *we = w + words, mode = j.mode;
        if (mode & B4_MAP) {        /* two pixels a load from T; flipped, from the row's last byte back */
            const uint8_t *s = (const uint8_t *)row, *tab = j.tab;
            int st = 1;
            if (mode & B4_FLIP) {
                s = (const uint8_t *)(row + words) - 1;
                st = -1;
            }
            uint8_t *o = (uint8_t *)w;
            do {
                uint32_t a = *s; s += st;
                uint32_t b = *s; s += st;
                uint32_t c = *s; s += st;
                uint32_t d = *s; s += st;
                a = tab[a]; b = tab[b]; c = tab[c]; d = tab[d];
                o[0] = (uint8_t)a; o[1] = (uint8_t)b; o[2] = (uint8_t)c; o[3] = (uint8_t)d;
                o += 4;
            } while (o < (uint8_t *)we);
        } else if (mode) {          /* mirrored: words from the end, nibbles reversed */
            const uint32_t *s = row + words;
            do {
                uint32_t v = *--s;
                v = (v >> 16) | (v << 16);
                uint32_t t = (v ^ (v >> 8)) & 0x00FF00FFu;  /* bytes swapped in each half */
                v ^= t ^ (t << 8);
                t = (v ^ (v >> 4)) & 0x0F0F0F0Fu;           /* nibbles swapped in each byte */
                *w++ = v ^ t ^ (t << 4);
            } while (w < we);
        } else {
            const uint32_t *s = row;
            do *w++ = *s++; while (w < we);
        }
        /* merge: output word o is rb[o + 1] funnelled with rb[o] */
        uint32_t dm = j.dither[y & 3], em = j.ma, sh = j.sh, rs = j.rs;
        const uint32_t *r = rb + j.oa;
        uint32_t p = *r, *d = fb + j.oa, *de = fb + j.ob;
        do {
            uint32_t q = *++r;
            uint32_t c = (q << sh) | ((p >> 1) >> rs);
            p = q;
            if (d == de - 1) em &= j.mb;
            uint32_t m = ~keyMask(c) & dm & em;
            em = ~0u;
            if (m) *d = (*d & ~m) | (c & m);
        } while (++d < de);
        row += words;
        fb += GFX_FB_STRIDE / 4;
        y++;
    } while (--n);
}

/* Remapped or inked: T on this function's stack, so other blits do not
 * pay for it. */
static __attribute__((noinline)) void blit4kMapped(B4Job &j) {
    uint32_t tab[64];
    j.tab = (uint8_t *)tab;
    blit4kRows(j);
}

void gfx_blit4k(const uint8_t *src, int x, int y, int wWords, int h, uint8_t flags,
                const uint8_t *remap, uint8_t level) {
    const GfxClip k = gfx__clip;
    if (!(flags & GFX_B4_DITHER) || level > 16) level = 16;
    int y1 = y + h;
    if (y1 > k.y1) y1 = k.y1;
    int r0 = y < k.y0 ? k.y0 - y : 0;
    if (wWords <= 0 || wWords > B4_MAXW || !level || y + r0 >= y1 || k.x1 <= k.x0) return;

    /* Output word o lands on framebuffer word column xw + o; the first
     * and last inside the clip may be only partly inside. */
    B4Job j;
    int xw = x >> 3;
    j.sh = (uint32_t)(x & 7) * 4;
    j.rs = 31 - j.sh;                       /* (prev >> 1) >> rs: no shift by 32 at sh 0 */
    int nOut = wWords + (j.sh != 0);
    j.oa = (k.x0 >> 3) - xw;
    j.ob = ((k.x1 - 1) >> 3) - xw + 1;
    j.ma = j.oa >= 0 ? ~0u << ((k.x0 & 7) * 4) : ~0u;
    j.mb = j.ob <= nOut ? ~0u >> ((7 - ((k.x1 - 1) & 7)) * 4) : ~0u;
    if (j.oa < 0) j.oa = 0;
    if (j.ob > nOut) j.ob = nOut;
    if (j.oa >= j.ob) return;

    /* Bayer thresholds packed a row per 16 bits, nibble i = column i; the
     * framebuffer words start on multiples of 8, so a row's mask is the
     * same four columns twice. */
    for (uint32_t r = 0; r < 4; r++) {
        uint32_t b = (r & 2 ? 0x5D7F91B3u : 0x6E4CA280u) >> ((r & 1) * 16), m = ~0u;
        if (level < 16) {
            m = 0;
            for (uint32_t i = 0; i < 16; i += 4)
                if (((b >> i) & 15) < level) m |= 0x000F000Fu << i;
        }
        j.dither[r] = m;
    }
    j.words = wWords;
    j.row = (const uint32_t *)src + r0 * wWords;
    j.fb = (uint32_t *)gfx_fb + (y + r0) * (GFX_FB_STRIDE / 4) + xw;
    j.rows = y1 - y - r0;
    j.y = y + r0;
    uint32_t mode = flags & GFX_B4_FLIPH;
    j.mode = mode;
    bool rm = (flags & GFX_B4_REMAP) && remap;
    if (!rm && !(flags & GFX_B4_SOLID)) {
        blit4kRows(j);
        return;
    }
    static const uint8_t ID[15] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    j.remap = rm ? remap : ID;
    j.keep = flags & GFX_B4_SOLID ? 0 : 15;
    j.ink = flags & GFX_B4_SOLID ? flags >> 4 : 0;
    j.mode = mode | B4_MAP;
    blit4kMapped(j);
}
