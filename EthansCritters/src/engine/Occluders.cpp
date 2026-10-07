// The willows over the actors behind them. See Occluders.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGfx.h>
#include "../assets/CardIndex.h"
#include "../assets/WorldData.h"
#include "Card.h"
#include "Occluders.h"
#include "Pace.h"

namespace occluders {

// A planned pass: the tree's rectangle's top-left (world px), its first
// block in OCCL (after the header, in its layer's copy), its shape, the
// actors it is made over (bit 0 the squire, bit 1 + i critter i: those
// behind the tree whose boxes overlap it) and the last of them in the draw
// list (the pass is made right after that one is drawn); boxes[k], the
// screen box pass k's actors' drawn boxes have grown to so far this frame.
struct Pass { int16_t x0, y0; uint16_t block, mask; uint8_t shape, last; };
static Pass passes[MAX_PASSES];
static scene::Rect boxes[MAX_PASSES];
static uint8_t nPasses;
static_assert(MAX_PASSES == 2, "plan() picks two");
// The squire's passes made this frame and the last, each its tree's first
// block + 1 (16 bits each, 0 none). A tree redrawn over him last frame is
// timed without this frame's fetch (fetchUs, pace::elapsed() at its end):
// timed with it, his second tree was shed one frame and made the next as
// the frame cache's fetches came and went, and its canopy flickered as he
// walked along it. Made, it counts in the frame's work, so the next fetch
// budget (Play.cpp) leaves room for it. (Made whatever the time, it took
// Willow Hollow and the hut's willows to 30 fps.) Starting a pass is timed
// with the fetch.
static uint32_t madeNow, madeLast, fetchUs;
// passes[0], his best tree, is still to be made this frame: a pass made
// before it (a critter drawn before him) keeps room for it (HIS_US), or the
// always-made pass after it took the frame over (four in a row in the hut's
// fight: 30 fps).
static bool hisDue;
static const uint32_t HIS_US = pace::CMD_US + 2 * pace::BLK_US + 100;   // (his box: ~2 blocks, the last one's blit)
void fetched() { fetchUs = pace::elapsed(); }
#if CHGAME_DEBUG
static Totals tot;
const Totals &totals() { return tot; }
void clearTotals() { tot = Totals(); }
bool off;
#endif

// The boxes round the feet the choice is made with (the drawn ones are
// known only once drawn): half widths, heights.
static const int SQUIRE_HW = 12, SQUIRE_H = 30, CRITTER_HW = 14, CRITTER_H = 26;
static const uint32_t REST_US = 400;        // the frame's work after the sprites (particles, HUD) kept free
// Where a frame's time runs out for a pass (but the squire's best): at 60
// fps the budget; at 30 the pacer's calm line, which it must see the frames
// average under to go back to 60 (Pace.h). With every pass made at 30 fps
// (there is time), a screen that holds 60 fps by shedding them stayed at 30
// once a slow frame or two had put it there (Willow Hollow: 6.8 ms at 60,
// 7.3-8.1 ms at 30 with the critters' passes, never calm again).
static const uint32_t LATE_US = pace::BUDGET_US - REST_US, LATE_HALF_US = pace::CALM_US - REST_US;

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

// The choice, by tree: every tree in view gets the actors behind it whose
// boxes overlap it, one pass over them all (made after the last of them is
// drawn, over the union of their drawn boxes: a raccoon at the squire under
// the same willow costs a few blocks, not a command). Overlaps are weighed
// by how much of its rectangle the card stores (a tree's own pixels and
// those of the willows in front of it). passes[0] is the squire's best
// tree, by his own overlap (x 4, and the critters' added: a tree with a
// raccoon behind it as well wins his ties, and one pass covers both),
// always made: with the willows in front stored
// with each tree it puts back 98.7% of the canopy over him (two passes
// 99.9%; each tree's own pixels alone, 78% and 93%: occlpack.py). The
// other goes to the tree with the most critter behind it, then the most of
// him (his second). (Ranked by everyone's overlap together, his best tree
// lost to one mostly over a raccoon, and was shed for time.)
void plan(const scene::List &l, int camX, int camY, uint32_t layer) {
    nPasses = 0;
    madeLast = madeNow;
    madeNow = 0;
#if CHGAME_DEBUG
    if (off) return;
#endif
    // the trees that can show: base lines from just above the view to OCCL_ABOVE below it
    int lo = camY - OCCL_BELOW, hi = camY + 120 + OCCL_ABOVE;
    uint32_t b0 = lo < 0 ? 0 : (uint32_t)lo >> OCCL_BAND_SHIFT, b1 = (uint32_t)hi >> OCCL_BAND_SHIFT;
    if (b1 >= OCCL_BANDS) b1 = OCCL_BANDS - 1;
    Pass sq;                                // his best tree (sqW: his overlap with it; 0 none)
    uint32_t sqW = 0, rank[MAX_PASSES] = {};    // passes: the best others so far, by rank
    uint32_t blk = OCCL_BAND[b0].block + layer * OCCL_LAYER_BLOCKS, band = b0;
    for (uint32_t i = OCCL_BAND[b0].first, e = OCCL_BAND[b1 + 1].first; i < e; i++) {
        while (i >= OCCL_BAND[band + 1].first) band++;
        const uint8_t *q = OCCLUDERS + 3 * i;
        uint32_t v = q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16;
        const OccluderShape &s = OCCL_SHAPE[(v >> 16) & 15];
        int base = (int)(band << OCCL_BAND_SHIFT | ((v >> 10) & 63));
        Pass p;
        p.x0 = (int16_t)((int)(v & 1023) - s.ax);
        p.y0 = (int16_t)(base - s.ay);
        p.block = (uint16_t)blk;
        p.shape = (uint8_t)((v >> 16) & 15);
        p.mask = 0;
        blk += s.blocks;
        int x1 = p.x0 + s.wWords * 8, y1 = p.y0 + s.h;
        if (x1 <= camX || p.x0 >= camX + GFX_W || y1 <= camY || p.y0 >= camY + 120) continue;
        int ws = 0, wc = 0;                     // his overlap and the critters', weighed
        for (uint32_t k = 0; k < l.n; k++) {
            const scene::Item &it = l.it[k];
            if (it.y >= base) break;            // in feet order: the rest are in front of it too
            bool me = it.what == scene::PLAYER;
            int hw = me ? SQUIRE_HW : CRITTER_HW;
            int ox = imin(it.x + hw, x1) - imax(it.x - hw, p.x0);
            // (the tree reaches below its base, so below any feet above it)
            int oy = it.y + 1 - imax(it.y - (me ? SQUIRE_H : CRITTER_H), p.y0);
            if (ox <= 0 || oy <= 0) continue;
            p.last = (uint8_t)(me ? 0 : 1 + it.i);
            p.mask |= (uint16_t)(1u << p.last);
            int o = ox * oy * (int)((v >> 20) + 1);
            if (me) ws = o;
            else wc += o;
        }
        if (!p.mask) continue;
        uint32_t sw = ws ? (uint32_t)ws << 2 | 1 : 0;      // (his ties: the tree with critters behind it, which
        sw += (uint32_t)wc;                                // covers them too, a pass fewer; ws under 2^14)
        if (ws && sw > sqW) {
            sqW = sw;
            sq = p;
        }
        // (wc under 2^18: 12 critters x 28 x 26 px x 16; ws under 2^14)
        uint32_t r = (uint32_t)wc << 12 | (uint32_t)ws >> 2 | 1, j = MAX_PASSES;
        if (r <= rank[j - 1]) continue;
        for (j--; j && rank[j - 1] < r; j--) {
            rank[j] = rank[j - 1];
            passes[j] = passes[j - 1];
        }
        rank[j] = r;
        passes[j] = p;
    }
    if (sqW) {                              // his best first, then the best other
        uint32_t o = passes[0].block == sq.block;
        passes[1] = passes[o];
        nPasses = (uint8_t)(1 + !!rank[o]);
        passes[0] = sq;
    } else {
        nPasses = (uint8_t)(!!rank[0] + !!rank[1]);
    }
    hisDue = nPasses && (passes[0].mask & 1);
    for (uint32_t k = 0; k < MAX_PASSES; k++) {
        boxes[k].x0 = boxes[k].y0 = 0x7FFF;
        boxes[k].x1 = boxes[k].y1 = -0x7FFF;
    }
}

// A pass's blocks as they arrive, blitted while the next one comes by DMA:
// rows [r, r1) of the tree, only words [lw, rw] of each and in those the
// pixels inside the overlap (lm, rm: the key over the pixels left of it
// in word lw, right of it in word rw). The clip is made in the landing
// buffer itself, which is ours until the next block (gfx_setClip() would
// make every CHGfx call check a clip that is never anything but the
// screen otherwise: ~250 B of flash, ~70 B of RAM functions).
struct Blit { int x, y, ww, rpb, r, r1, lw, rw; uint32_t lm, rm; };

static void onBlock(const uint8_t *b, void *ctx) {
    Blit &c = *(Blit *)ctx;
    int k = c.r % c.rpb, n = c.rpb - k;         // the first row wanted in this block, rows from it
    if (n > c.r1 - c.r) n = c.r1 - c.r;
    uint32_t *row = (uint32_t *)(uintptr_t)b + c.ww * k;
    for (uint32_t *e = row + c.ww * n, *w; row < e; row += c.ww) {
        for (w = row; w < row + c.lw; w++) *w = ~0u;
        *w |= c.lm;
        row[c.rw] |= c.rm;
        for (w = row + c.rw + 1; w < row + c.ww; w++) *w = ~0u;
    }
    gfx_blit4k(b + 4 * c.ww * k, c.x, c.y + c.r, c.ww, n, 0);
    c.r += n;
}

static uint8_t actor(uint8_t what, uint8_t i) { return (uint8_t)(what == scene::PLAYER ? 0 : 1 + i); }

bool planned(uint8_t what, uint8_t i) {
    for (uint32_t k = 0; k < nPasses; k++)
        if (passes[k].mask >> actor(what, i) & 1) return true;
    return false;
}

ECRT_OUTLINE bool after(uint8_t what, uint8_t i, int camX, int camY) {
    uint8_t a = actor(what, i);
    const scene::Rect &d = scene::drawn;
    for (uint32_t k = 0; k < nPasses; k++) {
        Pass &p = passes[k];
        if (!(p.mask >> a & 1)) continue;
        scene::Rect &u = boxes[k];              // the union of its actors' drawn boxes so far
        scene::grow(u.x0, u.y0, u.x1 - u.x0, u.y1 - u.y0);
        u = d;
        if (p.last != a) continue;              // (made after the last of them is drawn)
        const OccluderShape &s = OCCL_SHAPE[p.shape];
        int tx = p.x0 - camX, ty = p.y0 - camY + 8, rpb = s.rowsPerBlock;     // the tree on screen
        // the overlap of its actors' boxes, in the tree's pixels, on the playfield
        const scene::Rect &r = boxes[k];
        int x0 = imax(imax(r.x0, tx), 0) - tx, x1 = imin(imin(r.x1, tx + s.wWords * 8), GFX_W) - tx;
        int y0 = imax(imax(r.y0, ty), 8) - ty, y1 = imin(imin(r.y1, ty + s.h), GFX_H) - ty;
        if (!k) hisDue = false;
        if (x0 >= x1 || y0 >= y1) continue;
        uint32_t b0 = (uint32_t)y0 / rpb, n = (uint32_t)(y1 - 1) / rpb - b0 + 1;
        uint32_t t0 = pace::elapsed();
#ifdef CHSIM
        t0 += scene::simCost;
#endif
        // passes[0], his best tree, is always made; the others when they fit in the time left (and
        // leave room for his, still to come), a tree made last frame timed without the frame cache's
        // fetch (shed one frame and made the next as the fetches came and went, it flickered) unless
        // a frame just went over (then the fetch and that pass together took the hut's fight to 30)
        uint32_t id = p.block + 1u;
        bool again = !pace::lately() && ((madeLast & 0xFFFF) == id || madeLast >> 16 == id);
        if ((k || !(p.mask & 1)) && t0 - (again ? fetchUs : 0) + pace::CMD_US + n * pace::BLK_US +
                                        (k && hisDue ? HIS_US : 0) > (pace::half() ? LATE_HALF_US : LATE_US)) {
#if CHGAME_DEBUG
            tot.shed++;                         // no time left for it
#endif
            continue;
        }
        Blit c = {tx, ty, s.wWords, rpb, y0, y1, x0 >> 3, (x1 - 1) >> 3, (1u << 4 * (x0 & 7)) - 1,
                  x1 & 7 ? ~0u << 4 * (x1 & 7) : 0};
        if (!card::stream(CARD_OCCL_FIRST + 1 + p.block + b0, n, onBlock, &c)) return false;
        madeNow = madeNow << 16 | id;
#ifdef CHSIM
        // the drawing as the board would take it: each block's clip and blit hide under the next
        // one's DMA but the last (blit4k: ~0.25 us a word copied, ~0.75 a word merged, a row)
        int rows = y1 - imax(y0, (int)(b0 + n - 1) * rpb);
        scene::simCost += 10 + (uint32_t)rows * (s.wWords * 35 + (s.wWords + 1) * 75) / 100;
#endif
#if CHGAME_DEBUG
        uint32_t us = pace::elapsed() - t0;
#ifdef CHSIM
        us += scene::simCost;
#endif
        tot.passes++;
        tot.blocks += n;
        tot.sumUs += us;
        if (us > tot.maxUs) tot.maxUs = us;
#endif
    }
    return true;
}

}  // namespace occluders
