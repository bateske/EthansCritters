// Host test: the world streamer, the terrain and the camera (src/engine/
// World.cpp, Terrain.cpp, Camera.cpp, compiled as they are) on the
// CRITTERS.DAT tools/mkcard.py made, through a pretend CHSd that serves the
// file in two pieces split inside the world, so windows cross the split.
// world::draw() at every x phase and at the world's edges against the
// section decoded pixel by pixel from another band, in every ambient phase
// and world layer (each its own picture: the phases differ, the layers
// differ in The Rot); its idle hook's word counts (M8d: each word said to be
// final is, and no later block touches it); random walks that never end up
// in a wall; the camera inside the world and following.
// tools/game.py builds it (stub/ stands in for CHGfx).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <CHGfx.h>                     // stub/
#include <Fat.h>
#include <SdSpi.h>
#include "../../src/assets/CardIndex.h"
#include "../../src/assets/WorldData.h"
#include "../../src/engine/Camera.h"
#include "../../src/engine/Card.h"
#include "../../src/engine/Terrain.h"
#include "../../src/engine/World.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// --- CHGfx ------------------------------------------------------------------------
alignas(4) uint8_t gfx_fb[GFX_FB_BYTES];
alignas(4) static uint8_t scratch[2 * GFX_CHUNK_BYTES];
void gfx_wait() {}
uint8_t *gfx_chunkScratch() { return scratch; }

// --- the pretend card: the file in two runs -----------------------------------------
static std::vector<uint8_t> file;
static fat::Run layout[2];
struct Cmd { uint32_t lba, n; };
static std::vector<Cmd> cmds;

static const uint8_t *at(uint32_t lba) {
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
    cmds.push_back({lba, n});
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *s = at(lba + i);
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
            memcpy(dst, at(run[r].lba + k), 512);
            return true;
        }
        k -= run[r].blocks;
    }
    return false;
}
}  // namespace fat

// --- the world as stored ------------------------------------------------------------

static const int VW = 128, VH = 120;

// World pixel (x, y) of that layer and phase from the section, out of the
// band that starts nearest above it (not the band draw() reads, except at
// the bottom).
static uint8_t px(int x, int y, uint32_t layer = 0, uint32_t phase = 0) {
    uint32_t b = (uint32_t)y >> 3;
    if (b > WORLD_NB - 1u) b = WORLD_NB - 1;
    uint32_t slice = (b * WORLD_LAYERS + layer) * WORLD_TPH + phase;
    const uint8_t *blk = &file[(CARD_WRLD_FIRST + 1 + slice * WORLD_NC + (uint32_t)x / 8) * 512];
    uint8_t v = blk[4 * (y - 8 * b) + (x & 7) / 2];
    return x & 1 ? v >> 4 : v & 15;
}

static uint8_t fbPx(int x, int y) {
    uint8_t v = gfx_fb[y * 64 + x / 2];
    return x & 1 ? v >> 4 : v & 15;
}

static void testHeader() {
    const uint8_t *h = &file[CARD_WRLD_FIRST * 512];
    CHECK(memcmp(h, "WRLD", 4) == 0);
    CHECK((h[4] | h[5] << 8) == WORLD_W && (h[6] | h[7] << 8) == WORLD_H);
    CHECK((h[8] | h[9] << 8) == WORLD_NC && (h[10] | h[11] << 8) == WORLD_NB);
    CHECK(h[12] == WORLD_TPH && h[13] == WORLD_LAYERS);
    CHECK(CARD_WRLD_BLOCKS == 1u + (uint32_t)WORLD_NB * WORLD_NC * WORLD_TPH * WORLD_LAYERS);
    CHECK(WORLD_NC * 8 == WORLD_W && (WORLD_NB - 1) * 8 + VH == WORLD_H);
}

// One view: the framebuffer's playfield against the section, the HUD rows
// untouched, and the card read as one command (two across the split).
// Returns how many of its pixels differ from layer 0's phase 0.
static int view(int cx, int cy, uint32_t layer = 0, uint32_t phase = 0) {
    memset(gfx_fb, 0xA5, sizeof gfx_fb);
    cmds.clear();
    CHECK(world::draw(cx, cy, layer, phase));
    uint32_t n = 0;
    for (const Cmd &c : cmds) n += c.n;
    CHECK(n == (cx & 7 ? 17u : 16u));
    CHECK(cmds.size() == 1 || cmds.size() == 2);
    int bad = 0, moved = 0;
    for (int y = 0; y < VH; y++)
        for (int x = 0; x < VW; x++) {
            uint8_t v = fbPx(x, y + 8);
            bad += v != px(cx + x, cy + y, layer, phase);
            moved += v != px(cx + x, cy + y);
        }
    for (int i = 0; i < 8 * 64; i++) bad += gfx_fb[i] != 0xA5;
    if (bad) printf("  view (%d, %d) layer %u phase %u: %d wrong\n", cx, cy, (unsigned)layer, (unsigned)phase, bad);
    CHECK(bad == 0);
    return moved;
}

static void testViews() {
    const int mx = WORLD_W - VW, my = WORLD_H - VH;
    // every x phase, at the edges and inside
    for (int p = 0; p < 8; p++) {
        view(p, p);
        view(mx - p, my - (p & 7));
        view(400 + p, 333);
    }
    view(0, 0);
    view(mx, my);
    view(0, my);
    view(mx, 0);
    // across the split between the file's two runs
    uint32_t split = layout[0].blocks;
    uint32_t rel = split - (CARD_WRLD_FIRST + 1);           // the first block of run 2, in the section
    uint32_t slice = rel / WORLD_NC, slices = (uint32_t)WORLD_TPH * WORLD_LAYERS;
    int b = (int)(slice / slices), k = (int)(rel % WORLD_NC);
    int cx = k > 8 ? (k - 8) * 8 + 3 : 3, cy = b * 8 + 5;
    view(cx, cy, slice % slices / WORLD_TPH, slice % WORLD_TPH);
    CHECK(cmds.size() == 2);
    srand(1234);
    for (int i = 0; i < 300; i++) {
        int x = rand() % (mx + 1), y = rand() % (my + 1);
        uint32_t layer = (uint32_t)rand() % WORLD_LAYERS, phase = (uint32_t)rand() % WORLD_TPH;
        view(x, y, layer, phase);
    }
    // the ambient phases: each moves something in a view of the reeds (Reed Shallows), and
    // only the healed layer's Rot differs from the swamp's (The Landing is the same in both)
    for (uint32_t p = 1; p < WORLD_TPH; p++) CHECK(view(32, 188, 0, p) > 100);
    for (uint32_t p = 0; p < WORLD_TPH; p++) {
        CHECK(view(800, 100, 1, p) > 2000);
        CHECK(view(160, 572, 1, p) == view(160, 572, 0, p));
    }
}

// --- the idle hook (M8d: Play draws the sprites in it) ------------------------------
//
// After each block's copy, idle(words) says screen words 0..words-1 (8 px each)
// hold their final pixels. Play draws into them then, so that must be true: each
// word is checked against the section when it is first said to be final, then
// overwritten with a mark that must still be there after the stream (no later
// block's copy touches a word once it is final). The count rises by one a block,
// from 1 at x % 8 == 0 (block j fills word j) and from 0 otherwise (block j ends
// word j - 1), to 16 at the last block (whose wait has no DMA under it).

static const uint32_t MARK = 0x5A3CC3A5u;
static struct { int cx, cy, calls, done, bad; uint32_t layer, phase; } idl;

static void onIdle(uint32_t words) {
    int want = idl.calls + ((idl.cx & 7) ? 0 : 1);
    idl.calls++;
    if ((int)words != want || words > 16) {
        idl.bad++;
        return;
    }
    for (int w = idl.done; w < (int)words; w++)
        for (int y = 0; y < VH; y++) {
            for (int x = 8 * w; x < 8 * w + 8; x++)
                idl.bad += fbPx(x, y + 8) != px(idl.cx + x, idl.cy + y, idl.layer, idl.phase);
            memcpy(&gfx_fb[(y + 8) * 64 + 4 * w], &MARK, 4);
        }
    if ((int)words > idl.done) idl.done = (int)words;
}

static void idleView(int cx, int cy, uint32_t layer = 0, uint32_t phase = 0) {
    memset(gfx_fb, 0xA5, sizeof gfx_fb);
    idl = {cx, cy, 0, 0, 0, layer, phase};
    CHECK(world::draw(cx, cy, layer, phase, onIdle));
    CHECK(idl.calls == (cx & 7 ? 17 : 16));
    CHECK(idl.done == 16);
    for (int y = 8; y < 128; y++)
        for (int w = 0; w < 16; w++) idl.bad += memcmp(&gfx_fb[y * 64 + 4 * w], &MARK, 4) != 0;
    for (int i = 0; i < 8 * 64; i++) idl.bad += gfx_fb[i] != 0xA5;
    if (idl.bad) printf("  idle view (%d, %d) layer %u phase %u: %d wrong\n", cx, cy, (unsigned)layer, (unsigned)phase,
                        idl.bad);
    CHECK(idl.bad == 0);
}

static void testIdle() {
    const int mx = WORLD_W - VW, my = WORLD_H - VH;
    for (int p = 0; p < 8; p++) {
        idleView(p, p);
        idleView(mx - p, my - p, 1, (uint32_t)p % WORLD_TPH);
        idleView(400 + p, 333, 0, (uint32_t)p % WORLD_TPH);
    }
    // across the split between the file's two runs (two commands: the first one's last block
    // comes after its CMD12, the second's first after a new command)
    uint32_t rel = layout[0].blocks - (CARD_WRLD_FIRST + 1);
    uint32_t slice = rel / WORLD_NC, slices = (uint32_t)WORLD_TPH * WORLD_LAYERS;
    int b = (int)(slice / slices), k = (int)(rel % WORLD_NC);
    for (int p = 0; p < 8; p += 3) {
        cmds.clear();
        idleView(k > 8 ? (k - 8) * 8 + p : p, b * 8 + 5, slice % slices / WORLD_TPH, slice % WORLD_TPH);
        CHECK(cmds.size() == 2);
    }
    srand(4321);
    for (int i = 0; i < 100; i++)
        idleView(rand() % (mx + 1), rand() % (my + 1), (uint32_t)rand() % WORLD_LAYERS, (uint32_t)rand() % WORLD_TPH);
}

// --- terrain -------------------------------------------------------------------------

// The flat table (CHTEST only) the runs were made from.
static uint8_t flat(int cx, int cy) {
    uint32_t i = (uint32_t)cy * TERRAIN_W + cx;
    return (TERRAIN_FLAT[i >> 2] >> (2 * (i & 3))) & 3;
}

// The runs (WorldData.h): each row's add up to the row, and terrain::at()
// (which walks them from where it last was on a row of that parity) gives
// the flat table's class at every cell, asked for in every order: rows
// forwards and backwards, columns both ways, at random, and two rows
// interleaved (a box's cells).
static void testTerrain() {
    int counts[4] = {0, 0, 0, 0};
    for (int y = 0; y < TERRAIN_H; y++) {
        uint32_t cells = 0, end = y + 1 < TERRAIN_H ? TERRAIN_ROW[y + 1] : TERRAIN_RUN_COUNT;
        for (uint32_t i = TERRAIN_ROW[y]; i < end; i++) cells += (TERRAIN_RUNS[i] >> 2) + 1u;
        CHECK(cells == TERRAIN_W);
    }
    int bad = 0;
    for (int y = 0; y < TERRAIN_H; y++)
        for (int x = 0; x < TERRAIN_W; x++) {
            uint8_t t = flat(x, y);
            counts[t]++;
            for (int s = 0; s < 8; s += 7)     // each corner pixel of the cell's row
                bad += terrain::at(x * 8 + s, y * 8 + (7 - s)) != t;
        }
    for (int y = TERRAIN_H - 1; y >= 0; y--)
        for (int x = TERRAIN_W - 1; x >= 0; x--) bad += terrain::at(x * 8 + 3, y * 8 + 4) != flat(x, y);
    for (int x = 0; x < TERRAIN_W; x++)
        for (int y = 0; y < TERRAIN_H; y++) bad += terrain::at(x * 8, y * 8) != flat(x, y);
    srand(77);
    for (int k = 0; k < 200000; k++) {
        int x = rand() % WORLD_W, y = rand() % WORLD_H;
        bad += terrain::at(x, y) != flat(x >> 3, y >> 3);
    }
    for (int k = 0; k < 20000; k++) {        // a box's two rows, cells left and right, jumping about
        int x = rand() % WORLD_W, y = rand() % (WORLD_H - 8);
        for (int d = -24; d <= 24; d += 8) {
            int xx = x + d;
            if (xx < 0 || xx >= WORLD_W) continue;
            bad += terrain::at(xx, y) != flat(xx >> 3, y >> 3);
            bad += terrain::at(xx, y + 8) != flat(xx >> 3, (y + 8) >> 3);
        }
    }
    if (bad) printf("  terrain: %d lookups wrong\n", bad);
    CHECK(bad == 0);
    CHECK(counts[T_OPEN] && counts[T_SHALLOW] && counts[T_DEEP] && counts[T_SOLID]);
    CHECK(terrain::at(-1, 10) == T_SOLID && terrain::at(10, -1) == T_SOLID);
    CHECK(terrain::at(WORLD_W, 10) == T_SOLID && terrain::at(10, WORLD_H) == T_SOLID);
    CHECK(terrain::at(START_X, START_Y - 1) == T_OPEN);
    for (uint32_t i = 0; i < NEST_COUNT; i++) CHECK(terrain::at(NESTS[i].x, NESTS[i].y) <= T_SHALLOW);
    for (uint32_t i = 0; i < MUSHROOM_COUNT; i++)
        CHECK(terrain::at(mushX(MUSHROOMS[i]), mushY(MUSHROOMS[i])) <= T_SHALLOW);
}

// What the runs cost against the flat table, host time, on boxes the way
// movers ask (a few cells on one or two rows, the box drifting): printed,
// not checked (the board: tools/world's trace numbers in Terrain.cpp).
static void benchTerrain() {
    static const int N = 400000;
    int xs[64], ys[64];
    srand(5);
    for (int i = 0; i < 64; i++) {
        xs[i] = rand() % (WORLD_W - 16);
        ys[i] = rand() % (WORLD_H - 8);
    }
    uint32_t sum = 0;
    clock_t t0 = clock();
    for (int k = 0; k < N; k++) {
        int i = k & 63, x = xs[i] + (k >> 6) % 16, y = ys[i] + (k >> 8) % 8;
        sum += terrain::at(x, y) + terrain::at(x + 7, y) + terrain::at(x, y + 3) + terrain::at(x + 7, y + 3);
    }
    clock_t t1 = clock();
    for (int k = 0; k < N; k++) {
        int i = k & 63, x = xs[i] + (k >> 6) % 16, y = ys[i] + (k >> 8) % 8;
        sum -= flat(x >> 3, y >> 3) + flat((x + 7) >> 3, y >> 3) + flat(x >> 3, (y + 3) >> 3) +
               flat((x + 7) >> 3, (y + 3) >> 3);
    }
    clock_t t2 = clock();
    CHECK(sum == 0);
    printf("  terrain lookups (host): runs %.1f ns, flat %.1f ns\n", (t1 - t0) * 1e9 / CLOCKS_PER_SEC / (4.0 * N),
           (t2 - t1) * 1e9 / CLOCKS_PER_SEC / (4.0 * N));
}

// A blocker over one box (Terrain.h's blocker: a toad's bulk on the ground).
static int blockBox[4];
static bool blockBoxFn(int x0, int y0, int x1, int y1) {
    return x1 > blockBox[0] && x0 < blockBox[2] && y1 > blockBox[1] && y0 < blockBox[3];
}

// Random walks from the start: the feet box never ends up on solid ground or
// deep water, and it gets somewhere.
static void testWalks() {
    const uint8_t HW = 4, FH = 4;
    srand(99);
    int stuck = 0, far = 0;
    for (int w = 0; w < 40; w++) {
        int32_t x = START_X << 4, y = START_Y << 4;
        int dx = 0, dy = 0;
        for (int s = 0; s < 3000; s++) {
            if (s % 40 == 0) {
                dx = rand() % 3 - 1;
                dy = rand() % 3 - 1;
            }
            int32_t sp = dx && dy ? 13 : 19;
            terrain::move(x, y, dx * sp, dy * sp, HW, FH, terrain::BLOCK_WALK);
            int px = (int)(x >> 4), py = (int)(y >> 4);
            if (terrain::hits(px - HW, py - FH, px + HW, py, terrain::BLOCK_WALK)) {
                stuck++;
                break;
            }
        }
        int ddx = (int)(x >> 4) - START_X, ddy = (int)(y >> 4) - START_Y;
        far += ddx * ddx + ddy * ddy > 64 * 64;
    }
    CHECK(stuck == 0);
    CHECK(far > 5);
    // straight into a wall: stops against it, the box's edge in the last free pixel
    int32_t x = START_X << 4, y = START_Y << 4;
    for (int s = 0; s < 400; s++) terrain::move(x, y, 0, -19, HW, FH, terrain::BLOCK_WALK);
    int py = (int)(y >> 4);
    CHECK(!terrain::hits((int)(x >> 4) - HW, py - FH, (int)(x >> 4) + HW, py, terrain::BLOCK_WALK));
    CHECK(terrain::hits((int)(x >> 4) - HW, py - FH - 1, (int)(x >> 4) + HW, py - 1, terrain::BLOCK_WALK));
    // every way, from every sub-pixel, walking (19) and knocked back (40): it
    // ends against the wall, not a pixel or two short (a step over 1 px)
    static const int8_t DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int stops = 0, short1 = 0;
    for (int sp = 19; sp <= 40; sp += 21)
        for (int d = 0; d < 4; d++)
            for (int sub = 0; sub < 16; sub++)
                for (int k = 0; k < 6; k++) {
                    int32_t qx = ((START_X - 40 + 16 * k) << 4) + sub, qy = ((START_Y - 2 * k) << 4) + sub;
                    int px0 = (int)(qx >> 4), py0 = (int)(qy >> 4);
                    if (terrain::hits(px0 - HW, py0 - FH, px0 + HW, py0, terrain::BLOCK_WALK)) continue;
                    bool still = false;         // blocked and not nudged: stopped for good
                    for (int s = 0; s < 600 && !still; s++) {
                        int32_t ox = qx, oy = qy;
                        uint8_t b = terrain::move(qx, qy, DIRS[d][0] * sp, DIRS[d][1] * sp, HW, FH,
                                                  terrain::BLOCK_WALK);
                        still = b && qx == ox && qy == oy;
                    }
                    if (!still) continue;
                    stops++;
                    int nx = (int)(qx >> 4) + DIRS[d][0], ny = (int)(qy >> 4) + DIRS[d][1];
                    short1 += !terrain::hits(nx - HW, ny - FH, nx + HW, ny, terrain::BLOCK_WALK);
                }
    CHECK(stops > 100 && short1 == 0);
    if (short1) printf("  %d of %d stops short of the wall\n", short1, stops);
    // and slides along it when pushed diagonally
    int32_t x0 = x;
    uint8_t blocked = 0;
    for (int s = 0; s < 20; s++) blocked |= terrain::move(x, y, 13, -13, HW, FH, terrain::BLOCK_WALK);
    CHECK((blocked & 2) && x > x0);
    // put inside the forest at the world's corner, it walks out
    x = 12 << 4;
    y = 12 << 4;
    for (int s = 0; s < 600; s++) terrain::move(x, y, 13, 13, HW, FH, terrain::BLOCK_WALK);
    py = (int)(y >> 4);
    CHECK(x > (12 << 4) && !terrain::hits((int)(x >> 4) - HW, py - FH, (int)(x >> 4) + HW, py, terrain::BLOCK_WALK));
    // a blocker come down on it against a wall (the toad landing on him by the pond): it walks
    // out of the blocker, but not on into the wall (inside a blocker it used to go anywhere)
    x = START_X << 4;
    y = START_Y << 4;
    for (int s = 0; s < 400; s++) terrain::move(x, y, 0, -19, HW, FH, terrain::BLOCK_WALK);
    int32_t wallY = y;
    blockBox[0] = (int)(x >> 4) - 22;
    blockBox[1] = (int)(y >> 4) - 10;
    blockBox[2] = (int)(x >> 4) + 22;
    blockBox[3] = (int)(y >> 4) + 3;
    terrain::blocker = blockBoxFn;
    for (int s = 0; s < 100; s++) terrain::move(x, y, 0, -19, HW, FH, terrain::BLOCK_WALK);
    py = (int)(y >> 4);
    CHECK(y == wallY && !terrain::hits((int)(x >> 4) - HW, py - FH, (int)(x >> 4) + HW, py, terrain::BLOCK_WALK));
    for (int s = 0; s < 100; s++) terrain::move(x, y, 0, 19, HW, FH, terrain::BLOCK_WALK);
    py = (int)(y >> 4);
    CHECK(!blockBoxFn((int)(x >> 4) - HW, py - FH, (int)(x >> 4) + HW, py));
    terrain::blocker = nullptr;
}

// --- camera --------------------------------------------------------------------------

static bool inWorld() {
    return camera::x >= 0 && camera::y >= 0 && camera::x <= WORLD_W - VW && camera::y <= WORLD_H - VH;
}

static void testCamera() {
    camera::snap(START_X << 4, START_Y << 4);
    CHECK(inWorld());
    int sx = camera::screenX(START_X), sy = camera::screenY(START_Y);
    CHECK(sx > 32 && sx < 96 && sy > 40 && sy < 120);
    // corners: clamped
    camera::snap(0, 0);
    CHECK(camera::x == 0 && camera::y == 0);
    camera::snap(WORLD_W << 4, WORLD_H << 4);
    CHECK(camera::x == WORLD_W - VW && camera::y == WORLD_H - VH);
    // following a walk east: the target stays well inside the view
    int32_t tx = 300 << 4, ty = 400 << 4;
    camera::snap(tx, ty);
    bool ok = true;
    for (int s = 0; s < 400; s++) {
        tx += 19;
        camera::update(tx, ty, 1, 0);
        int x = camera::screenX((int)(tx >> 4));
        ok &= inWorld() && x > 40 && x < 88;
    }
    CHECK(ok);
    CHECK(camera::screenX((int)(tx >> 4)) < 64);            // it looks ahead: more view to the east
    // still inside the dead zone: the view does not move
    int16_t vx = camera::x, vy = camera::y;
    camera::update(tx - (4 << 4), ty + (3 << 4), 1, 0);
    CHECK(camera::x == vx && camera::y == vy);
    // a shake moves the view and stays in the world
    camera::shake(10, 3);
    camera::update(tx, ty, 1, 0);
    CHECK(inWorld() && (camera::x != vx || camera::y != vy));
    for (int s = 0; s < 12; s++) camera::update(tx, ty, 1, 0);
    CHECK(camera::x == vx && camera::y == vy);
}

static void testRegions() {
    CHECK(world::regionAt(START_X, START_Y) < REGION_NAMES);
    CHECK(world::regionAt(-5, -5) == REGIONS[REGION_RECTS - 1].name);
    for (uint32_t i = 0; i + 1 < REGION_RECTS; i++) {
        const WorldRect &r = REGIONS[i].r;
        uint8_t got = world::regionAt((r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2);
        bool earlier = false;           // an earlier rect may hold the middle
        for (uint32_t j = 0; j < i; j++) {
            const WorldRect &q = REGIONS[j].r;
            int mx = (r.x0 + r.x1) / 2, my = (r.y0 + r.y1) / 2;
            earlier |= mx >= q.x0 && mx < q.x1 && my >= q.y0 && my < q.y1;
        }
        CHECK(earlier || got == REGIONS[i].name);
    }
}

int main() {
    const char *path = "../out/card/CRITTERS.DAT";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL: %s is missing (tools/mkcard.py makes it)\n", path);
        return 1;
    }
    uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + n);
    fclose(f);
    uint32_t blocks = (uint32_t)(file.size() / 512);
    // split the file a third of the way into the world, mid-band
    uint32_t split = CARD_WRLD_FIRST + 1 + CARD_WRLD_BLOCKS / 3 + 5;
    layout[0] = {7000, split};
    layout[1] = {7000 + split + 91, blocks - split};
    CHECK(card::open() == card::READY);
    testHeader();
    testViews();
    testIdle();
    testTerrain();
    benchTerrain();
    testWalks();
    testCamera();
    testRegions();
    printf("world: %d checks, %d failed (%u blocks, world %dx%d, terrain %dx%d)\n", checks, fails,
           (unsigned)blocks, WORLD_W, WORLD_H, TERRAIN_W, TERRAIN_H);
    return fails ? 1 : 0;
}
