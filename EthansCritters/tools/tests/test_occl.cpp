// Host test: the occluders (src/engine/Occluders.cpp, with Card.cpp and
// CHGfx's real gfx_blit4k) on the CRITTERS.DAT tools/mkcard.py made, through
// a pretend CHSd serving the file in two pieces. For actors all over the
// world, with the view on them: the world's pixels in the framebuffer, the
// actor's box filled with a colour the world never has there, plan() and
// after(), then every pixel checked: outside the box nothing changed; inside
// it, a pixel is either the actor's still or exactly the world's, and the
// world's only where a tree the actor is behind shows (OCCL's pixel,
// matching the world: the section is right and the pass found its rows);
// an actor behind exactly one tree gets every pixel of it back; an actor in
// front of every tree it overlaps gets nothing. The time budget: a
// critter's pass is shed when the frame has no time left, the squire's
// best tree never is, a tree of his redrawn the frame before is timed
// without the fetch. All of it in each world layer (OCCL's copy of the
// trees in that layer's colours: The Rot's willows dead, then healed) and
// in each ambient phase of the world (the pixels a tree keeps are the
// world's in every phase). tools/game.py builds it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <CHGfx.h>
#include <CHGfx_internal.h>
#include <Fat.h>
#include <SdSpi.h>
#include "../../src/assets/CardIndex.h"
#include "../../src/assets/WorldData.h"
#include "../../src/engine/Card.h"
#include "../../src/engine/Occluders.h"
#include "../../src/engine/Pace.h"
#include "../../src/game/Scene.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// --- CHGfx (the real blit4k is linked), Pace and Scene as Occluders.cpp uses them ----------
alignas(4) uint8_t gfx_fb[GFX_FB_BYTES];
GfxClip gfx__clip = {0, 0, GFX_W, GFX_H};
alignas(4) static uint8_t scratch[2 * GFX_CHUNK_BYTES];
void gfx_wait() {}
uint8_t *gfx_chunkScratch() { return scratch; }

static uint32_t nowUs;              // what pace::elapsed() says
namespace pace {
uint32_t elapsed() { return nowUs; }
bool half() { return false; }
bool lately() { return false; }
}
namespace scene {
Rect drawn;
uint32_t simCost;
void clearDrawn() {
    drawn.x0 = drawn.y0 = 0x7FFF;
    drawn.x1 = drawn.y1 = -0x7FFF;
}
void grow(int x, int y, int w, int h) {        // (Scene.cpp's)
    if (x < drawn.x0) drawn.x0 = (int16_t)x;
    if (y < drawn.y0) drawn.y0 = (int16_t)y;
    if (x + w > drawn.x1) drawn.x1 = (int16_t)(x + w);
    if (y + h > drawn.y1) drawn.y1 = (int16_t)(y + h);
}
void List::add(int x, int y, uint8_t what, uint8_t i) {
    uint32_t j = n++;
    for (; j && it[j - 1].y > y; j--) it[j] = it[j - 1];
    it[j].x = (int16_t)x;
    it[j].y = (int16_t)y;
    it[j].what = what;
    it[j].i = i;
}
}  // namespace scene

// --- the pretend card: the file in two runs -----------------------------------------
static std::vector<uint8_t> file;
static fat::Run layout[2];
static uint32_t streams;
static uint32_t afterHim;            // streams when the squire's after() was done (scene1)

static const uint8_t *blockAt(uint32_t lba) {
    uint32_t k = 0;
    for (const fat::Run &r : layout) {
        if (lba >= r.lba && lba < r.lba + r.blocks) {
            uint32_t b = k + lba - r.lba;
            return b * 512 < file.size() ? &file[b * 512] : nullptr;
        }
        k += r.blocks;
    }
    return nullptr;
}

namespace sd {
bool init() { return true; }
bool stream(uint32_t lba, uint32_t n, uint8_t *b0, uint8_t *b1, BlockFn fn, void *ctx) {
    streams++;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *s = blockAt(lba + i);
        if (!s) return false;
        uint8_t *d = (i & 1) ? b1 : b0;
        memcpy(d, s, 512);
        fn(d, ctx);
        memset(d, 0xEE, 512);                   // a block is valid only during its call
    }
    return true;
}
void setStreamTimeout(uint32_t) {}
bool recover() { return true; }
}  // namespace sd

namespace fat {
int8_t mount(uint8_t *) { return OK; }
int8_t find(const char *, File &f, uint8_t *) {
    f.cluster = 2;
    f.size = (uint32_t)file.size();
    return OK;
}
int8_t runs(const File &, Run *out, uint8_t, uint8_t *) {
    out[0] = layout[0];
    out[1] = layout[1];
    return 2;
}
bool read(const Run *run, uint32_t nRuns, uint32_t k, uint8_t *dst) {
    for (uint32_t r = 0; r < nRuns; r++) {
        if (k < run[r].blocks) {
            memcpy(dst, blockAt(run[r].lba + k), 512);
            return true;
        }
        k -= run[r].blocks;
    }
    return false;
}
}  // namespace fat

// --- the world and the trees as stored ------------------------------------------------

static uint32_t layer, phase;          // the world layer and ambient phase under test

static uint8_t worldPx(int x, int y) {
    uint32_t b = (uint32_t)y >> 3;
    if (b > WORLD_NB - 1u) b = WORLD_NB - 1;
    uint32_t slice = (b * WORLD_LAYERS + layer) * WORLD_TPH + phase;
    const uint8_t *blk = &file[(CARD_WRLD_FIRST + 1 + slice * WORLD_NC + (uint32_t)x / 8) * 512];
    uint8_t v = blk[4 * (y - 8 * b) + (x & 7) / 2];
    return x & 1 ? v >> 4 : v & 15;
}

struct Tree { int x0, y0, w, h, base, block; const OccluderShape *s; };
static std::vector<Tree> trees;

static void loadTrees() {
    uint32_t blk = 0, band = 0;
    for (uint32_t i = 0; i < OCCL_COUNT; i++) {
        while (i >= OCCL_BAND[band + 1].first) band++;
        if (i == OCCL_BAND[band].first) CHECK(blk == OCCL_BAND[band].block);
        const uint8_t *q = OCCLUDERS + 3 * i;
        uint32_t v = q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16;
        const OccluderShape &s = OCCL_SHAPE[(v >> 16) & 15];
        Tree t;
        t.base = (int)(band << OCCL_BAND_SHIFT | ((v >> 10) & 63));
        t.x0 = (int)(v & 1023) - s.ax;
        t.y0 = t.base - s.ay;
        t.w = s.wWords * 8;
        t.h = s.h;
        t.block = (int)blk;
        t.s = &s;
        CHECK(s.rowsPerBlock == 512 / (4 * s.wWords) && s.blocks == (s.h + s.rowsPerBlock - 1) / s.rowsPerBlock);
        CHECK(trees.empty() || t.base >= trees.back().base);
        trees.push_back(t);
        blk += s.blocks;
    }
    CHECK(blk == OCCL_BAND[OCCL_BANDS].block && OCCL_BAND[OCCL_BANDS].first == OCCL_COUNT);
    CHECK(blk == OCCL_LAYER_BLOCKS && CARD_OCCL_BLOCKS == 1 + WORLD_LAYERS * blk);
    CHECK(memcmp(&file[CARD_OCCL_FIRST * 512], "OCCL", 4) == 0);
}

// Tree t's stored pixel at (x, y) of its rectangle (15: not its own), in this layer's copy.
static uint8_t treePx(const Tree &t, int x, int y) {
    int rpb = t.s->rowsPerBlock;
    uint32_t k = CARD_OCCL_FIRST + 1 + layer * OCCL_LAYER_BLOCKS + t.block + y / rpb;
    const uint8_t *b = &file[k * 512 + (y % rpb) * 4 * t.s->wWords];
    uint8_t v = b[x / 2];
    return x & 1 ? v >> 4 : v & 15;
}

// Every pixel a tree stores is the world's there. Trees that overlap store the same pixels where a
// willow in front of them both shows (its own, and its neighbours' in front of it: occlpack.py).
static void testSection() {
    std::vector<uint8_t> own(WORLD_W * WORLD_H, 0);
    int bad = 0, shared = 0, shown = 0;
    for (const Tree &t : trees)
        for (int y = 0; y < t.h; y++)
            for (int x = 0; x < t.w; x++) {
                uint8_t v = treePx(t, x, y);
                int wx = t.x0 + x, wy = t.y0 + y;
                if (v == 15) continue;
                shown++;
                if (wx < 0 || wy < 0 || wx >= WORLD_W || wy >= WORLD_H) {
                    bad++;
                    continue;
                }
                bad += v != worldPx(wx, wy);
                shared += own[wy * WORLD_W + wx]++ != 0;
            }
    if (bad)
        printf("  section, layer %u phase %u: %d pixels not the world's\n", (unsigned)layer, (unsigned)phase, bad);
    CHECK(bad == 0 && shared > 1000 && shown > 50000);
}

// --- passes ----------------------------------------------------------------------------

static uint8_t fbPx(int x, int y) { return (gfx_fb[y * GFX_FB_STRIDE + x / 2] >> ((x & 1) * 4)) & 15; }
static void fbPut(int x, int y, uint8_t c) {
    uint8_t &b = gfx_fb[y * GFX_FB_STRIDE + x / 2];
    b = x & 1 ? (uint8_t)((b & 0x0F) | c << 4) : (uint8_t)((b & 0xF0) | c);
}

struct Result { int restored, kept, wrong, outside, missed; };

// The squire's feet at world (fx, fy) with the view on him, his box hw each
// side of his feet and h tall; critters too (cx, cy pairs, k of them).
static Result scene1(int fx, int fy, int hw, int h, const int *crit = nullptr, int k = 0) {
    int camX = fx - 64, camY = fy - 68;
    if (camX < 0) camX = 0;
    if (camY < 0) camY = 0;
    if (camX > WORLD_W - 128) camX = WORLD_W - 128;
    if (camY > WORLD_H - 120) camY = WORLD_H - 120;
    for (int y = 8; y < 128; y++)
        for (int x = 0; x < 128; x++) fbPut(x, y, worldPx(camX + x, camY + y - 8));
    scene::simCost = 0;                         // a new frame's drawing (as Play's draw() starts it)
    scene::List l;
    l.n = 0;
    l.add(fx, fy, scene::PLAYER, 0);
    for (int i = 0; i < k; i++) l.add(crit[2 * i], crit[2 * i + 1], scene::CRITTER, (uint8_t)i);
    occluders::plan(l, camX, camY, layer);
    // the squire drawn: his box in a colour the world does not have at each pixel
    int sx = fx - camX, sy = fy - camY + 8;
    scene::drawn = {(int16_t)(sx - hw), (int16_t)(sy - h), (int16_t)(sx + hw), (int16_t)(sy + 1)};
    static uint8_t before[GFX_FB_BYTES];
    for (int y = sy - h; y < sy + 1; y++)
        for (int x = sx - hw; x < sx + hw; x++)
            if (x >= 0 && x < 128 && y >= 8 && y < 128) fbPut(x, y, (uint8_t)((fbPx(x, y) + 7) % 15));
    memcpy(before, gfx_fb, sizeof before);
    // the actors in the draw list's order, each followed by after(): the critters drawn as nothing (an
    // empty box), so a pass made after one of them (the last of a tree's actors) covers his box alone
    scene::Rect his = scene::drawn;
    afterHim = streams;
    for (uint32_t j = 0; j < l.n; j++) {
        bool me = l.it[j].what == scene::PLAYER;
        if (me) scene::drawn = his;
        else scene::clearDrawn();
        CHECK(occluders::after(l.it[j].what, l.it[j].i, camX, camY));
        if (me) afterHim = streams;
    }
    Result r = {0, 0, 0, 0, 0};
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            uint8_t now = fbPx(x, y), was = (before[y * GFX_FB_STRIDE + x / 2] >> ((x & 1) * 4)) & 15;
            bool in = x >= sx - hw && x < sx + hw && y >= sy - h && y < sy + 1 && y >= 8;
            if (!in) {
                r.outside += now != was;
                continue;
            }
            int wx = camX + x, wy = camY + y - 8;
            uint8_t world = worldPx(wx, wy);
            // the trees he is behind that show this pixel
            int showing = 0;
            for (const Tree &t : trees)
                if (t.base > fy && wx >= t.x0 && wx < t.x0 + t.w && wy >= t.y0 && wy < t.y0 + t.h &&
                    treePx(t, wx - t.x0, wy - t.y0) != 15)
                    showing++;
            if (now == was) {
                r.kept++;
                r.missed += showing;
            } else if (now == world && showing) {
                r.restored++;
            } else {
                r.wrong++;
            }
        }
    return r;
}

// How many trees the squire at (fx, fy) is behind and overlaps (box hw x h).
static int behind(int fx, int fy, int hw, int h) {
    int n = 0;
    for (const Tree &t : trees)
        if (t.base > fy && t.x0 < fx + hw && t.x0 + t.w > fx - hw && t.y0 < fy + 1 && t.y0 + t.h > fy - h) n++;
    return n;
}

static void testPasses() {
    srand(321 + 7 * layer + phase);
    int one = 0, front = 0, many = 0, wrong = 0, outside = 0, missed = 0, restored = 0;
    for (int k = 0; k < 4000 && (one < 150 || front < 150 || many < 150); k++) {
        // near a tree: up to 90 px above its base, to 8 px below it
        const Tree &t = trees[rand() % trees.size()];
        int fx = t.x0 + rand() % (t.w + 16) - 8, fy = t.base - 90 + rand() % 98;
        if (fx < 16 || fy < 40 || fx >= WORLD_W - 16 || fy >= WORLD_H - 2) continue;
        int hw = 6 + rand() % 7, h = 18 + rand() % 12;     // inside plan()'s squire box: what it chose covers him
        int nb = behind(fx, fy, 12, 30);                    // (the trees plan() weighs, in that box)
        nowUs = 0;
        Result r = scene1(fx, fy, hw, h);
        wrong += r.wrong;
        outside += r.outside;
        restored += r.restored;
        if (nb == 0) {
            front++;
            CHECK(r.restored == 0 && r.wrong == 0);
        } else if (nb == 1) {
            one++;
            missed += r.missed;
        } else {
            many++;
        }
    }
    if (wrong || outside || missed)
        printf("  passes: %d pixels wrong, %d changed outside the actor, %d of one tree's missed\n", wrong, outside,
               missed);
    CHECK(wrong == 0 && outside == 0 && missed == 0);
    CHECK(one >= 150 && front >= 150 && many >= 150 && restored > 10000);
    printf("  passes, layer %u phase %u: %d actors behind one tree, %d behind several, %d in front; %d pixels put "
           "back\n", (unsigned)layer, (unsigned)phase, one, many, front, restored);
}

// The budget: with the frame's time gone, a critter's own pass is shed, the
// squire's best tree is still redrawn; with time, both are made.
static void testBudget() {
    int done = 0;
    for (size_t i = 0; i < trees.size() && done < 20; i++) {
        const Tree &t = trees[i];
        int fx = t.x0 + t.w / 2, fy = t.base - 12;
        if (fx < 16 || fy < 40 || fx >= WORLD_W - 16 || behind(fx, fy, 12, 30) != 1) continue;
        int crit[2] = {fx + 30, fy - 2};
        nowUs = 0;
        uint32_t s0 = streams;
        scene1(fx, fy, 10, 26, crit, 1);
        CHECK(streams >= s0 + 1);                   // his pass (and the critter's, if it has a tree of its own)
        nowUs = pace::BUDGET_US;                    // no time left
        s0 = streams;
        Result r = scene1(fx, fy, 10, 26, crit, 1);
        CHECK(streams == s0 + 1 && r.restored > 0); // his best tree all the same, the critter's own shed
        done++;
    }
    CHECK(done >= 8);
    printf("  budget: %d squires behind one tree, a critter by each\n", done);
}

// A critter beside him behind the same tree, its feet a little lower (drawn
// after him): one pass over both, made after the critter, with no time
// left (it is his best tree), and his box covered by it.
static void testShared() {
    int done = 0;
    for (size_t i = 0; i < trees.size() && done < 20; i++) {
        const Tree &t = trees[i];
        int fx = t.x0 + t.w / 2, fy = t.base - 14;
        int crit[2] = {fx + 6, fy + 2};
        if (fx < 16 || fy < 40 || fx >= WORLD_W - 16 || behind(fx, fy, 12, 30) != 1 ||
            behind(crit[0], crit[1], 14, 26) != 1)
            continue;
        nowUs = pace::BUDGET_US;
        uint32_t s0 = streams;
        Result r = scene1(fx, fy, 10, 26, crit, 1);
        CHECK(afterHim == s0 && streams == s0 + 1 && r.restored > 0 && r.wrong == 0);
        done++;
    }
    CHECK(done >= 8);
    printf("  shared: %d squires and a critter behind one tree, one pass after both\n", done);
}

// A tree redrawn over the squire is redrawn the next frame with no time
// left (shed one frame and made the next, it flickered as he walked); one
// not redrawn last frame still waits for the time.
static void testAgain() {
    int done = 0;
    srand(99);
    for (int k = 0; k < 20000 && done < 20; k++) {
        const Tree &t = trees[rand() % trees.size()];
        int fx = t.x0 + rand() % t.w, fy = t.base - 4 - rand() % 40;
        if (fx < 16 || fy < 40 || fx >= WORLD_W - 16 || behind(fx, fy, 12, 30) < 2) continue;
        nowUs = 0;
        uint32_t s0 = streams;
        scene1(fx, fy, 10, 26);
        if (streams != s0 + 2) continue;            // (his box over only one of them)
        nowUs = pace::BUDGET_US;                    // the fetch took the frame's time: both again
        occluders::fetched();
        s0 = streams;
        scene1(fx, fy, 10, 26);
        CHECK(streams == s0 + 2);
        scene1(16, 40, 10, 26);                     // a frame with neither (the world's corner: no willows)
        s0 = streams;
        scene1(fx, fy, 10, 26);                     // back, no time: his best tree only
        CHECK(streams == s0 + 1);
        done++;
    }
    CHECK(done >= 10);
    printf("  again: %d squires behind two trees, their passes timed without a fetch that took the time\n", done);
}

int main() {
    const char *path = "../out/card/CRITTERS.DAT";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL: %s is missing (tools/mkcard.py makes it)\n", path);
        return 1;
    }
    static uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + n);
    fclose(f);
    uint32_t blocks = (uint32_t)(file.size() / 512);
    // split the file inside the occluders' section, so passes cross the split
    uint32_t split = CARD_OCCL_FIRST + CARD_OCCL_BLOCKS / 2 + 3;
    layout[0] = {5000, split};
    layout[1] = {5000 + split + 77, blocks - split};
    CHECK(card::open() == card::READY);
    loadTrees();
    // every layer's trees in every phase of its world; the passes in each layer (phases 0 and 2)
    int differ = 0;
    for (layer = 0; layer < WORLD_LAYERS; layer++)
        for (phase = 0; phase < WORLD_TPH; phase++) {
            testSection();
            if (!(phase & 1)) testPasses();
        }
    for (const Tree &t : trees)                 // The Rot's willows: dead in layer 0, alive when healed
        for (int y = 0; y < t.h; y++)
            for (int x = 0; x < t.w; x++) {
                layer = 0;
                uint8_t a = treePx(t, x, y);
                layer = WORLD_LAYERS - 1;
                differ += a != treePx(t, x, y);
            }
    CHECK(differ > 1000);
    layer = phase = 0;
    testBudget();
    testAgain();
    testShared();
    printf("occl: %d checks, %d failed (%u trees, %u shapes, %u blocks)\n", checks, fails, (unsigned)OCCL_COUNT,
           (unsigned)OCCL_SHAPES, (unsigned)CARD_OCCL_BLOCKS);
    return fails ? 1 : 0;
}
