// Host test: the game's card code (src/engine/Card.cpp, PathF.cpp and
// CardFormat.h, compiled as they are) on the CRITTERS.DAT tools/mkcard.py
// made, through a pretend CHSd: the file laid out in runs on a pretend card,
// sd::stream() recording its commands, and every way the card can be wrong.
// tools/game.py builds it (stub/ stands in for CHGfx and CHGame) and makes
// the card first.
#include <stdio.h>
#include <string.h>
#include <vector>
#include <CHGfx.h>                     // stub/
#include <Fat.h>
#include <SdSpi.h>
#include "../../src/assets/Palette.h"
#include "../../src/engine/Card.h"
#include "../../src/engine/CardFormat.h"
#include "../../src/engine/PathF.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// --- CHGfx and pal ----------------------------------------------------------------
alignas(4) uint8_t gfx_fb[GFX_FB_BYTES];
alignas(4) static uint8_t scratch[2 * GFX_CHUNK_BYTES];
static int waits;
void gfx_wait() { waits++; }
uint8_t *gfx_chunkScratch() { return scratch; }
static uint16_t palSet[16];
namespace pal {
void init(const uint16_t *b) { memcpy(palSet, b, sizeof palSet); }
}

// --- the pretend card ---------------------------------------------------------------
static std::vector<uint8_t> file;               // CRITTERS.DAT
static std::vector<fat::Run> layout;            // its runs on the card
struct Cmd { uint32_t lba, n; };
static std::vector<Cmd> cmds;
static bool cardIn, readOk, recoverOk;
static int8_t mountRc, findRc, chainRc;
static int failAfter;                           // stream(): blocks delivered before the card stops (-1: never)
static uint32_t timeoutUs;                      // setStreamTimeout()
static int recovers;                            // recover() calls

static void reset(std::vector<uint32_t> pieces) {
    layout.clear();
    uint32_t lba = 5000;
    for (uint32_t b : pieces) {
        layout.push_back({lba, b});
        lba += b + 37;                          // a gap between pieces
    }
    cmds.clear();
    cardIn = readOk = recoverOk = true;
    mountRc = findRc = chainRc = fat::OK;
    failAfter = -1;
    timeoutUs = 0;
    recovers = 0;
}

// The file's block at a card lba, or nullptr.
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
bool init() { return cardIn; }
bool stream(uint32_t lba, uint32_t n, uint8_t *b0, uint8_t *b1, BlockFn fn, void *ctx) {
    cmds.push_back({lba, n});
    for (uint32_t i = 0; i < n; i++) {
        if (failAfter == 0) return false;
        if (failAfter > 0) failAfter--;
        const uint8_t *s = at(lba + i);
        if (!s) return false;
        uint8_t *d = (i & 1) ? b1 : b0;
        memcpy(d, s, 512);
        fn(d, ctx);
        memset(d, 0xEE, 512);                   // a block is valid only during its call
    }
    return true;
}
void setStreamTimeout(uint32_t us) { timeoutUs = us; }
bool recover() {
    recovers++;
    return recoverOk;
}
}  // namespace sd

namespace fat {
int8_t mount(uint8_t *buf) { memset(buf, 0xAA, 512); return mountRc; }
int8_t find(const char *name, File &f, uint8_t *buf) {
    memset(buf, 0xBB, 512);
    f.cluster = 2;
    f.size = (uint32_t)file.size();
    return findRc ? findRc : memcmp(name, "CRITTERSDAT", 11) ? E_NOTFOUND : OK;
}
int8_t runs(const File &, Run *out, uint8_t maxRuns, uint8_t *buf) {
    memset(buf, 0xCC, 512);
    if (chainRc) return chainRc;
    if (layout.size() > maxRuns) return E_FRAG;
    for (size_t i = 0; i < layout.size(); i++) out[i] = layout[i];
    return (int8_t)layout.size();
}
bool read(const Run *run, uint32_t nRuns, uint32_t k, uint8_t *dst) {
    for (uint32_t r = 0; r < nRuns; r++) {
        if (k < run[r].blocks) {
            const uint8_t *s = at(run[r].lba + k);
            if (!readOk || !s) return false;
            memcpy(dst, s, 512);
            return true;
        }
        k -= run[r].blocks;
    }
    return false;
}
}  // namespace fat

// --- the tests ---------------------------------------------------------------------

static void testHeader() {
    uint32_t blocks = (uint32_t)(file.size() / 512);
    const uint8_t *b0 = file.data();
    CHECK(file.size() % 512 == 0);
    CHECK(blocks == CARD_BLOCKS);
    CHECK(cardfmt::headerOk(b0, blocks));
    CHECK(cardfmt::headerOk(b0, blocks + 3));           // a file rounded up is fine
    CHECK(!cardfmt::headerOk(b0, blocks - 1));          // one cut short is not
    const int fields[] = {0, 5, 7, 8, 9, 12, 13, 14, 15, 16, 18};
    for (int i : fields) {
        uint8_t c[512];
        memcpy(c, b0, 512);
        c[i] ^= 0x10;
        CHECK(!cardfmt::headerOk(c, blocks));
    }
    // the section table as CardIndex.h has it
    CHECK(cardfmt::rd16(b0 + 10) >= 1);                 // TITL first, the others after it
    CHECK(memcmp(b0 + 20, "TITL", 4) == 0);
    CHECK(cardfmt::rd32(b0 + 24) == CARD_TITL_FIRST);
    CHECK(cardfmt::rd32(b0 + 28) == CARD_TITL_BLOCKS);
    // the title: a loop (M7b) in the game's palette
    const uint8_t *h = b0 + CARD_TITL_FIRST * 512;
    CHECK(cardfmt::clipFrames(h) > 1 && cardfmt::clipMs(h) > 0);
    CHECK(CARD_TITL_BLOCKS == 1 + cardfmt::clipFrames(h) * cardfmt::FRAME_BLOCKS);
    uint16_t pal[16];
    cardfmt::clipPalette(h, pal);
    CHECK(memcmp(pal, PALETTE, sizeof pal) == 0);
    // the death and win clips (their own palettes: the game's words in
    // slots 0 and 5 stay dark and light) and the pause map (a still in the
    // game's palette: the marks and REMAP_SEPIA's fog go over it)
    const uint32_t clips[3][2] = {{CARD_DEAD_FIRST, CARD_DEAD_BLOCKS}, {CARD_WINC_FIRST, CARD_WINC_BLOCKS},
                                  {CARD_PMAP_FIRST, CARD_PMAP_BLOCKS}};
    for (int i = 0; i < 3; i++) {
        h = b0 + clips[i][0] * 512;
        uint32_t n = cardfmt::clipFrames(h);
        CHECK(n >= 1 && clips[i][1] == 1 + n * cardfmt::FRAME_BLOCKS);
        CHECK((i == 2) == (cardfmt::clipMs(h) == 0));
        cardfmt::clipPalette(h, pal);
        if (i == 2) {
            CHECK(memcmp(pal, PALETTE, sizeof pal) == 0);
        } else {
            uint32_t dark = (pal[0] >> 8) + (pal[0] >> 4 & 15) + (pal[0] & 15);
            uint32_t light = (pal[5] >> 8) + (pal[5] >> 4 & 15) + (pal[5] & 15);
            CHECK(dark <= 6 && light >= 38);
        }
    }
}

static void testTitle(std::vector<uint32_t> pieces, std::vector<Cmd> want) {
    reset(pieces);
    int w = waits;
    CHECK(card::open() == card::READY);
    CHECK(card::status() == card::READY);
    CHECK(waits > w);                                   // the bus rule: gfx_wait() first
    memset(palSet, 0, sizeof palSet);
    CHECK(pathf::open(CARD_TITL_FIRST));
    CHECK(memcmp(palSet, PALETTE, sizeof palSet) == 0);
    const uint8_t *h = &file[CARD_TITL_FIRST * 512];
    CHECK(pathf::frames() == cardfmt::clipFrames(h) && pathf::frameMs() == cardfmt::clipMs(h));
    memset(gfx_fb, 0x77, sizeof gfx_fb);
    cmds.clear();
    w = waits;
    CHECK(pathf::frame(0));
    CHECK(waits > w);
    CHECK(memcmp(gfx_fb, &file[(CARD_TITL_FIRST + 1) * 512], GFX_FB_BYTES) == 0);
    CHECK(cmds.size() == want.size());
    for (size_t i = 0; i < cmds.size() && i < want.size(); i++)
        CHECK(cmds[i].lba == want[i].lba && cmds[i].n == want[i].n);
    CHECK(!pathf::frame(pathf::frames()));              // no frame past the last
}

// pathf::play(): the frame t ticks (50/3 ms each) in, looping or held.
static bool shows(uint32_t first, uint32_t k) {
    return memcmp(gfx_fb, &file[(first + 1 + k * cardfmt::FRAME_BLOCKS) * 512], GFX_FB_BYTES) == 0;
}

static void testPlay() {
    reset({CARD_BLOCKS});
    CHECK(card::open() == card::READY);
    CHECK(pathf::open(CARD_TITL_FIRST));
    uint32_t n = pathf::frames(), ms = pathf::frameMs();
    const uint32_t ticks[] = {0, 1, 7, 8, 59, 60, 1000, 123456};
    for (uint32_t t : ticks) {
        uint32_t k = t * 50 / (3 * ms);
        CHECK(pathf::play(t, true) && shows(CARD_TITL_FIRST, k % n));
        CHECK(pathf::play(t, false) && shows(CARD_TITL_FIRST, k < n ? k : n - 1));
    }
    CHECK(pathf::open(CARD_PMAP_FIRST));                // a still: frame 0 whenever
    CHECK(pathf::play(0, true) && shows(CARD_PMAP_FIRST, 0));
    CHECK(pathf::play(9999, false) && shows(CARD_PMAP_FIRST, 0));
    CHECK(memcmp(palSet, PALETTE, sizeof palSet) == 0);
    CHECK(pathf::open(CARD_DEAD_FIRST));                // its own palette goes to pal::init()
    CHECK(palSet[0] == cardfmt::rd16(&file[CARD_DEAD_FIRST * 512 + 8]));
}

static void testTitles() {
    uint32_t n = CARD_BLOCKS;
    // one piece: the frame (file blocks 2..17) is one command
    testTitle({n}, {{5002, 16}});
    // three pieces: split where the runs split (5000+5, 5042+7, 5086+rest)
    testTitle({5, 7, n - 12}, {{5002, 3}, {5042, 7}, {5086, 6}});
    // eight pieces, the most: blocks 2 | 3-4 | 5-6 | ... one command per piece touched
    testTitle({3, 2, 2, 2, 2, 2, 2, n - 15}, {{5002, 1}, {5040, 2}, {5079, 2}, {5118, 2},
                                               {5157, 2}, {5196, 2}, {5235, 2}, {5274, 3}});
}

static void testErrors() {
    uint32_t n = CARD_BLOCKS;
    reset({n}); cardIn = false;               CHECK(card::open() == card::NO_CARD);
    reset({n}); mountRc = fat::E_READ;        CHECK(card::open() == card::NO_CARD);
    reset({n}); mountRc = fat::E_NOFS;        CHECK(card::open() == card::NOT_FAT);
    reset({n}); mountRc = fat::E_EXFAT;       CHECK(card::open() == card::NOT_FAT);
    reset({n}); findRc = fat::E_NOTFOUND;     CHECK(card::open() == card::NO_FILE);
    reset({n}); findRc = fat::E_READ;         CHECK(card::open() == card::NO_CARD);
    reset({n}); chainRc = fat::E_CHAIN;       CHECK(card::open() == card::FRAGMENTED);
    reset({n}); chainRc = fat::E_READ;        CHECK(card::open() == card::NO_CARD);
    reset({2, 2, 2, 2, 2, 2, 2, 2, n - 16});  CHECK(card::open() == card::FRAGMENTED);   // 9 pieces
    reset({});                                CHECK(card::open() == card::BAD_DATA);     // empty: no runs
    reset({n}); readOk = false;               CHECK(card::open() == card::NO_CARD);
    reset({n - 1});                           CHECK(card::open() == card::BAD_DATA);     // cut short
    CHECK(!pathf::open(CARD_TITL_FIRST));
    std::vector<uint8_t> keep = file;
    reset({n}); file[12] ^= 1;                CHECK(card::open() == card::BAD_DATA);     // another build
    file = keep;
    reset({n}); file[CARD_TITL_FIRST * 512] = 'X';                                       // no clip there
    CHECK(card::open() == card::READY);
    CHECK(!pathf::open(CARD_TITL_FIRST));
    CHECK(card::status() == card::READY);
    file = keep;
}

static void testFailures() {
    uint32_t n = CARD_BLOCKS;
    // the timeouts: CHSd's long one from open(), the short one for the screens that stream every frame
    reset({n});
    timeoutUs = 7;
    CHECK(card::open() == card::READY && timeoutUs == card::BOOT_US);
    card::steady();
    CHECK(timeoutUs == card::PLAY_US);
    // a stall part way through a frame: the frame is lost, the card recovered, the next frame comes
    reset({5, n - 5});
    CHECK(card::open() == card::READY);
    CHECK(pathf::open(CARD_TITL_FIRST));
    memset(gfx_fb, 0, sizeof gfx_fb);
    failAfter = 7;
    CHECK(!pathf::frame(0));
    CHECK(card::status() == card::READY && card::fails == 1 && recovers == 1);
    CHECK(memcmp(gfx_fb, &file[(CARD_TITL_FIRST + 1) * 512], 7 * 512) == 0);     // what came, landed
    failAfter = -1;
    CHECK(pathf::frame(0));                           // the next frame, without open()
    CHECK(memcmp(gfx_fb, &file[(CARD_TITL_FIRST + 1) * 512], GFX_FB_BYTES) == 0);
    CHECK(card::fails == 1 && recovers == 1);         // (the frame loop zeroes it after a whole frame)
    // GIVE_UP lost frames in a row: the card is given up (NO_CARD), as the frame loop never cleared it
    card::fails = 0;
    for (int i = 1; i < card::GIVE_UP; i++) {
        failAfter = 3;
        CHECK(!pathf::frame(1));
    }
    CHECK(card::status() == card::READY && card::fails == card::GIVE_UP - 1);
    failAfter = 3;
    CHECK(!pathf::frame(1));
    CHECK(card::status() == card::NO_CARD);
    size_t before = cmds.size();
    failAfter = -1;
    CHECK(!pathf::frame(0));                          // no more card commands until open()
    CHECK(cmds.size() == before);
    CHECK(card::open() == card::READY && card::fails == 0);     // and back, counting from 0
    CHECK(pathf::open(CARD_TITL_FIRST) && pathf::frame(0));
    // a card recover() cannot bring back (pulled, or power lost): given up at once
    failAfter = 7;
    recoverOk = false;
    CHECK(!pathf::frame(0));
    CHECK(card::status() == card::NO_CARD);
    // a range past the file's end: the part in the file is read, then false
    // (CHSd's fat::stream(), P1, refuses before reading anything): a lost frame
    reset({n});
    CHECK(card::open() == card::READY);
    static uint32_t got;
    got = 0;
    CHECK(!card::stream(n - 1, 2, [](const uint8_t *, void *) { got++; }, nullptr));
    CHECK(card::status() == card::READY && card::fails == 1);
    CHECK(card::stream(n - 1, 1, [](const uint8_t *, void *) { got++; }, nullptr));
    CHECK(got == 2);
}

// The boot's warm-up (the .ino's warm()): the whole file in long streams,
// every block once and in order, one command per run a step touches.
static void testWarm() {
    reset({700, 5, CARD_BLOCKS - 705});
    CHECK(card::open() == card::READY);
    cmds.clear();
    static uint32_t next;
    static bool inOrder;
    next = 0;
    inOrder = true;
    for (uint32_t k = 0; k < CARD_BLOCKS; k += 512) {
        uint32_t m = CARD_BLOCKS - k < 512 ? CARD_BLOCKS - k : 512;
        CHECK(card::stream(k, m, [](const uint8_t *b, void *) {
            inOrder &= memcmp(b, &file[next++ * 512], 512) == 0;
        }, nullptr));
    }
    CHECK(next == CARD_BLOCKS && inOrder);
    uint32_t steps = (CARD_BLOCKS + 511) / 512;
    CHECK(cmds.size() == steps + 2);                  // the steps across 512 and 1024 add one each (700 | 5 | ...)
    CHECK(cmds[1].lba == 5000 + 512 && cmds[1].n == 700 - 512);
    CHECK(cmds[2].lba == 5000 + 700 + 37 && cmds[2].n == 5);
}

int main() {
    const char *path = "../out/card/CRITTERS.DAT";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL: %s is missing (tools/mkcard.py makes it)\n", path);
        return 1;
    }
    uint8_t buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + n);
    fclose(f);
    testHeader();
    testTitles();
    testPlay();
    testErrors();
    testFailures();
    testWarm();
    printf("card: %d checks, %d failed (%s, %u blocks, hash %08X)\n", checks, fails, path,
           (unsigned)(file.size() / 512), (unsigned)CARD_HASH);
    return fails ? 1 : 0;
}
