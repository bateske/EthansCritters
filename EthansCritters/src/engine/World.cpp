// The world from the card. See World.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGfx.h>
#include <chgame/RamFunc.h>
#include "../assets/CardIndex.h"
#include "../assets/WorldData.h"
#include "Card.h"
#include "World.h"

namespace world {

typedef uint32_t __attribute__((may_alias)) word;

static const uint32_t VIEW_ROWS = 120, TOP = 8, ROW_WORDS = GFX_W / 8;

// One window's copy, from block to block.
struct Copy {
    word *dst;          // framebuffer row TOP, the word this block lands in
    uint32_t r0;        // the band row at the top of the view
    uint32_t sh;        // 4 * (cx & 7): the block's pixels sit this many bits into word j - 1
    uint32_t j;         // the block's screen word (0..16)
    void (*idle)(uint32_t words);
};

// A column block (128 rows of 8 px, low nibble first) into the framebuffer:
// band rows r0..r0+119 to framebuffer rows 8..127. At x % 8 == 0 a store a
// row; otherwise the block's left pixels end word j - 1 (whose first pixels,
// the previous block's, stay) and its right ones start word j (whose last
// pixels the next block fills). From SRAM: it runs while the next block's
// DMA does (board: 26 us aligned, 51 us shifted, under the 222 us of a block).
RAMFUNC(ecrtWorldCol) static void toFb(const uint8_t *blk, void *ctx) {
    Copy &c = *(Copy *)ctx;
    const word *s = (const word *)blk + c.r0, *e = s + VIEW_ROWS;
    word *d = c.dst;
    uint32_t sh = c.sh;
    if (!sh) {
        for (; s < e; s++, d += ROW_WORDS) *d = *s;
    } else if (!c.j) {                  // the view's left edge: only the right part shows
        for (; s < e; s++, d += ROW_WORDS) *d = *s >> sh;
    } else {
        uint32_t ls = 32 - sh, keep = (1u << ls) - 1;
        if (c.j == ROW_WORDS) {         // the right edge: only the left part shows
            for (; s < e; s++, d += ROW_WORDS) d[-1] = (d[-1] & keep) | (*s << ls);
        } else {
            for (; s < e; s++, d += ROW_WORDS) {
                uint32_t v = *s;
                d[-1] = (d[-1] & keep) | (v << ls);
                *d = v >> sh;
            }
        }
    }
    c.dst++;
    c.j++;
    // the screen words before this block's (and, at x % 8 == 0, its own) hold their final pixels
    if (c.idle) c.idle(sh ? c.j - 1 : c.j);
}

bool draw(int cx, int cy, uint32_t layer, uint32_t phase, void (*idle)(uint32_t words)) {
    uint32_t x = (uint32_t)cx, y = (uint32_t)cy, p = x & 7;
    Copy c = {(word *)gfx_fb + TOP * ROW_WORDS, y & 7, 4 * p, 0, idle};
    // lba = first + 1 + ((band * L + layer) * TPH + phase) * NC + column
    uint32_t k = CARD_WRLD_FIRST + 1 + (((y >> 3) * WORLD_LAYERS + layer) * WORLD_TPH + phase) * WORLD_NC + (x >> 3);
    return card::stream(k, p ? ROW_WORDS + 1 : ROW_WORDS, toFb, &c);
}

uint8_t regionAt(int x, int y) {
    const WorldRegion *r = REGIONS, *e = REGIONS + REGION_RECTS - 1;
    for (; r < e; r++)
        if (x >= r->r.x0 && x < r->r.x1 && y >= r->r.y0 && y < r->r.y1) break;
    return r->name;                     // the last holds everything
}

}  // namespace world
