// Host test: the frame cache (src/engine/Spool.cpp, compiled as it is, with
// the generated src/assets/Bank.cpp) on the CRITTERS.DAT tools/mkcard.py
// made, through a pretend card::stream() that records its commands: every
// frame of every clip arrives byte for byte; the plan (merging across gaps
// of up to 3 blocks, at most 2 commands, the block budget, priorities,
// prefetch riding along); siblings; LRU eviction that never drops a frame
// this screen wants; compaction; a card failing part way; and a random
// stress run checking the directory and every cached byte after each fetch.
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../../src/assets/Bank.h"
#include "../../src/assets/CardIndex.h"
#include "../../src/engine/Card.h"
#include "../../src/engine/Spool.h"
#include "../../config.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace spool;

uint32_t micros() { return 0; }

// --- the pretend card ---------------------------------------------------------------
static std::vector<uint8_t> file;
struct Cmd { uint32_t k, n; };
static std::vector<Cmd> cmds;
static int failAfter = -1;                      // blocks delivered before the card stops

namespace card {
bool stream(uint32_t k, uint32_t n, sd::BlockFn fn, void *ctx) {
    alignas(4) static uint8_t buf[2][512];
    cmds.push_back({k, n});
    for (uint32_t i = 0; i < n; i++) {
        if (failAfter == 0) return false;
        if (failAfter > 0) failAfter--;
        if ((k + i + 1) * 512 > file.size()) return false;
        uint8_t *d = buf[i & 1];
        memcpy(d, &file[(k + i) * 512], 512);
        fn(d, ctx);
        memset(d, 0xEE, 512);                   // a block is valid only during its call
    }
    return true;
}
}  // namespace card

// Stored frame s of clip c as the card has it, and its size.
static const uint8_t *cardFrame(uint32_t c, uint32_t s, uint32_t *size = nullptr) {
    const BankClip &k = BANK_CLIPS[c];
    const uint8_t *p = &file[(CARD_BANK_FIRST + k.first + s / bankPerBlock(k)) * 512 + (s % bankPerBlock(k)) * k.slot4 * 4u];
    if (size) *size = 8 + 4u * p[0] * p[1];
    return p;
}

static bool sameAsCard(uint32_t c, uint32_t f, const uint8_t *p) {
    uint32_t size;
    const uint8_t *q = cardFrame(c, bankStored(BANK_CLIPS[c], (uint8_t)f), &size);
    return p && memcmp(p, q, size) == 0;
}

// The bank block of played frame f of clip c, and a frame in a given block.
static uint32_t blockOf(uint32_t c, uint32_t f) {
    const BankClip &k = BANK_CLIPS[c];
    return k.first + bankStored(k, (uint8_t)f) / bankPerBlock(k);
}
struct Pick { uint8_t c, f; };
static Pick inBlock(uint32_t blk) {
    for (uint32_t c = 0; c < CLIP_COUNT; c++)
        for (uint32_t f = 0; f < BANK_CLIPS[c].frames; f++)
            if (blockOf(c, f) == blk) return {(uint8_t)c, (uint8_t)f};
    return {0xFF, 0};
}

// The directory is sound: by offset, no overlaps, inside the arena, nothing
// pending, keys unique, and every frame's bytes are the card's.
static bool sound() {
    DebugEntry d[MAX_ENTRIES];
    uint32_t n = debugDir(d), end = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (d[i].off < end || (d[i].size & 0x8000) || d[i].off % 4 || d[i].off + d[i].size > ECRT_ARENA) return false;
        end = d[i].off + d[i].size;
        for (uint32_t j = 0; j < i; j++)
            if (d[j].key == d[i].key) return false;
        uint32_t c = d[i].key >> 8, s = d[i].key & 255, size;
        if (c >= CLIP_COUNT || s >= BANK_CLIPS[c].stored) return false;
        const uint8_t *q = cardFrame(c, s, &size);
        if (size != d[i].size || memcmp(debugArena() + d[i].off, q, size)) return false;
    }
    return true;
}

static void fresh(uint8_t cmdsMax = 2, uint8_t blocksMax = 6) {
    begin(CARD_BANK_FIRST);
    setBudget(cmdsMax, blocksMax);
    cmds.clear();
    failAfter = -1;
}

// --- the tests ----------------------------------------------------------------------

// Every played frame of every clip, wanted alone after a fresh start: one
// command, the card's bytes, the header's checksum and fields.
static void testEveryFrame() {
    uint32_t n = 0;
    for (uint32_t c = 0; c < CLIP_COUNT; c++) {
        const BankClip &k = BANK_CLIPS[c];
        CHECK(k.frames && k.stored && k.stored <= k.frames && k.slot4 && bankPerBlock(k) == 512 / (k.slot4 * 4u));
        for (uint32_t f = 0; f < k.frames; f++) {
            fresh();
            want((uint8_t)c, (uint8_t)f, PRIO_PLAYER);
            CHECK(fetch());
            CHECK(cmds.size() == 1 && cmds[0].n == 1 && cmds[0].k == CARD_BANK_FIRST + blockOf(c, f));
            const uint8_t *p = frame((uint8_t)c, (uint8_t)f);
            CHECK(sameAsCard(c, f, p));
            if (!p) continue;
            uint32_t sum = 0, bytes = 4u * p[0] * p[1];
            for (uint32_t i = 0; i < bytes; i++) sum += p[8 + i];
            CHECK((sum & 255) == p[4]);
            CHECK(p[0] >= 1 && p[0] <= 16 && p[6] <= p[0] * 8 && p[6] > p[0] * 8 - 8 && p[5] == 0 && p[7] == 0);
            CHECK(8 + bytes <= k.slot4 * 4u);
            CHECK(((uintptr_t)p & 3) == 0);
            n++;
        }
    }
    CHECK(n == 214);                            // every played frame (packbank's count)
}

// Siblings: one want brings the block's other frames; the next screen needs no command.
static void testSiblings() {
    fresh();
    const BankClip &k = BANK_CLIPS[CLIP_RACCOON_IDLE];
    CHECK(bankPerBlock(k) == 3);
    want(CLIP_RACCOON_IDLE, 0);
    CHECK(fetch());
    CHECK(stats().loaded == 3 && stats().cmds == 1);
    CHECK(cached(CLIP_RACCOON_IDLE, 1) && cached(CLIP_RACCOON_IDLE, 2) && !cached(CLIP_RACCOON_IDLE, 3));
    cmds.clear();
    want(CLIP_RACCOON_IDLE, 1);
    want(CLIP_RACCOON_IDLE, 2);
    CHECK(fetch());
    CHECK(cmds.empty() && stats().misses == 0);
    CHECK(sameAsCard(CLIP_RACCOON_IDLE, 2, frame(CLIP_RACCOON_IDLE, 2)));
    // a frame() miss is wanted at the next fetch
    CHECK(!frame(CLIP_TURTLE_WALK, 0));
    CHECK(stats().drawMisses == 1);
    CHECK(fetch());
    CHECK(sameAsCard(CLIP_TURTLE_WALK, 0, frame(CLIP_TURTLE_WALK, 0)));
}

// The plan: gaps of up to 3 blocks read through, two commands at most, the
// block budget, priorities, and prefetch.
static void plan(std::vector<uint32_t> blocks, std::vector<uint8_t> prios, std::vector<Cmd> want_,
                 uint8_t blocksMax, int deferred, const char *what) {
    fresh(2, blocksMax);
    for (size_t i = 0; i < blocks.size(); i++) {
        Pick p = inBlock(blocks[i]);
        want(p.c, p.f, prios[i]);
    }
    CHECK(fetch());
    bool same = cmds.size() == want_.size();
    for (size_t i = 0; same && i < cmds.size(); i++)
        same = cmds[i].k == CARD_BANK_FIRST + want_[i].k && cmds[i].n == want_[i].n;
    if (!same || stats().deferred != deferred) {
        fails++;
        printf("FAIL plan %s: %d commands (", what, (int)cmds.size());
        for (const Cmd &c : cmds) printf(" %u+%u", (unsigned)(c.k - CARD_BANK_FIRST), (unsigned)c.n);
        printf(" ), %d deferred\n", stats().deferred);
    }
    checks++;
    for (size_t i = 0; i < blocks.size(); i++) {
        Pick p = inBlock(blocks[i]);
        if (cached(p.c, p.f)) CHECK(sameAsCard(p.c, p.f, peek(p.c, p.f)));
    }
}

static void testPlan() {
    const uint8_t P = PRIO_PLAYER, C = PRIO_CRITTER, F = PRIO_PREFETCH;
    plan({40}, {C}, {{40, 1}}, 6, 0, "one");
    plan({40, 41}, {C, C}, {{40, 2}}, 6, 0, "adjacent");
    plan({40, 44}, {C, C}, {{40, 5}}, 6, 0, "gap of 3 read through");
    plan({40, 45}, {C, C}, {{40, 1}, {45, 1}}, 6, 0, "gap of 4: two commands");
    plan({40, 60, 80}, {C, C, P}, {{80, 1}, {40, 1}}, 6, 1, "three apart: the player first, one deferred");
    plan({60, 40, 80}, {C, P, C}, {{40, 1}, {60, 1}}, 6, 1, "priority, then block order");
    plan({40, 44}, {C, C}, {{40, 1}, {44, 1}}, 3, 0, "budget: two commands instead of a long one");
    plan({40, 44, 70}, {C, C, C}, {{40, 1}, {44, 1}}, 2, 1, "budget: out of blocks");
    plan({40}, {F}, {}, 6, 0, "a prefetch alone reads nothing (and is no miss)");
    plan({40, 42, 70}, {C, F, F}, {{40, 3}}, 6, 0, "a prefetch rides along, never alone");
    plan({40, 42, 46}, {C, C, C}, {{40, 7}}, 8, 0, "chained gaps");
    plan({40, 46, 43}, {P, P, C}, {{40, 7}}, 8, 0, "two commands joined by a third");
}

// Eviction: least recently used first; never a frame this screen wants.
// Clips with one frame a block, so no siblings blur the picture.
static void testEviction() {
    fresh(2, 16);
    const uint8_t T = CLIP_SQUIRE_THRUST, R = CLIP_SQUIRE_PARRY, G = CLIP_CHAMELEON_TONGUE;
    CHECK(bankPerBlock(BANK_CLIPS[T]) == 1 && bankPerBlock(BANK_CLIPS[R]) == 1 && bankPerBlock(BANK_CLIPS[G]) == 1);
    // one frame a screen: thrust 0..6, then parry 0..5; the arena (3 KB) fills
    // and the oldest thrust frames go first
    for (uint8_t f = 0; f < 7; f++) { want(T, f); CHECK(fetch()); }
    for (uint8_t f = 0; f < 7; f++) CHECK(cached(T, f));
    for (uint8_t f = 0; f < 6; f++) { want(R, f); CHECK(fetch()); CHECK(cached(R, f)); }
    CHECK(!cached(T, 0) && cached(R, 0) && cached(R, 5) && cached(T, 6));
    for (uint8_t f = 1; f < 7; f++)             // evicted strictly oldest first
        if (cached(T, f)) for (uint8_t g = f; g < 7; g++) CHECK(cached(T, g));
    CHECK(sound());

    // all of it wanted at once, more than fits: what is loaded stays, the rest waits
    fresh(2, 32);
    uint32_t total = 0;
    for (uint8_t f = 0; f < 7; f++) want(T, f, PRIO_PLAYER);
    for (uint8_t f = 0; f < 6; f++) want(R, f, PRIO_PLAYER);
    for (uint8_t f = 0; f < 6; f++) want(G, f, PRIO_PLAYER);
    CHECK(fetch());
    uint32_t got = 0;
    for (uint8_t f = 0; f < 7; f++) got += cached(T, f);
    for (uint8_t f = 0; f < 6; f++) got += cached(R, f);
    for (uint8_t f = 0; f < 6; f++) got += cached(G, f);
    total = 19;
    CHECK(got == stats().loaded && got + stats().deferred == total && stats().deferred > 0);
    CHECK(stats().evicted == 0 && sound());
    for (uint8_t f = 0; f < 7; f++) if (cached(T, f)) CHECK(sameAsCard(T, f, peek(T, f)));
    // next screen: the tongue frames that waited, and thrust 0 and 1 again:
    // those two are this screen's, the other thrust/parry frames are not
    bool t0 = cached(T, 0), t1 = cached(T, 1);
    for (uint8_t f = 0; f < 6; f++) want(G, f, PRIO_CRITTER);
    want(T, 0, PRIO_PLAYER);
    want(T, 1, PRIO_PLAYER);
    CHECK(fetch());
    CHECK(t0 && t1 && cached(T, 0) && cached(T, 1));
    for (uint8_t f = 0; f < 6; f++) CHECK(cached(G, f));
    CHECK(stats().evicted > 0 && sound());
    CHECK(sameAsCard(T, 0, peek(T, 0)) && sameAsCard(G, 5, peek(G, 5)));
}

// Compaction: holes everywhere, none big enough, the free total enough.
static void testCompaction() {
    fresh(2, 32);
    // small frames (raccoon, 3 a block, ~100-150 B) fill most of the arena...
    const uint8_t T = CLIP_SQUIRE_THRUST;
    uint8_t small[] = {CLIP_RACCOON_IDLE, CLIP_RACCOON_RUN, CLIP_RACCOON_ATTACK, CLIP_RACCOON_DEATH,
                       CLIP_TURTLE_WALK, CLIP_TURTLE_BITE, CLIP_BEAVER_IDLE, CLIP_BEAVER_MOVEMENT,
                       CLIP_BEAVER_BITE, CLIP_TURTLE_IDLE_BLINK, CLIP_TURTLE_SLEEP, CLIP_BEAVER_DIVE,
                       CLIP_BEAVER_ASCENT, CLIP_BEAVER_DAMAGE, CLIP_STICK_SPIN, CLIP_BEAVER_IDLE_WATER};
    std::vector<Pick> loaded;
    for (uint8_t c : small)
        for (uint8_t f = 0; f < BANK_CLIPS[c].frames && loaded.size() < 24; f++)
            if (bankStored(BANK_CLIPS[c], f) == f) loaded.push_back({c, f});
    // load them a few a screen, all kept wanted (so nothing is evicted)...
    for (size_t i = 0; i < loaded.size(); i += 4) {
        for (size_t j = 0; j <= i + 3 && j < loaded.size(); j++) want(loaded[j].c, loaded[j].f, PRIO_PLAYER);
        CHECK(fetch());
    }
    // ...then keep every other one: the rest are evicted by the big frames
    // below only if there is no other way; first make holes by wanting only
    // every other one while a big thrust frame asks for room
    DebugEntry d[MAX_ENTRIES];
    uint32_t before = debugDir(d);
    CHECK(before == MAX_ENTRIES || before >= 20);
    uint32_t compactions = 0;
    for (uint8_t f = 0; f < 7; f++) {
        for (size_t j = 0; j < loaded.size(); j += 2)
            if (cached(loaded[j].c, loaded[j].f)) want(loaded[j].c, loaded[j].f, PRIO_PLAYER);
        want(T, f, PRIO_PLAYER);
        CHECK(fetch());
        compactions += stats().compacted;
        CHECK(sound());
        for (size_t j = 0; j < loaded.size(); j++)
            if (cached(loaded[j].c, loaded[j].f)) CHECK(sameAsCard(loaded[j].c, loaded[j].f, peek(loaded[j].c, loaded[j].f)));
        CHECK(sameAsCard(T, f, peek(T, f)));
    }
    CHECK(compactions > 0);
    printf("compaction: %u compactions in 7 screens\n", (unsigned)compactions);
}

// The card stops part way: what came is kept, nothing is left half-reserved.
static void testCardFailure() {
    fresh(2, 16);
    want(CLIP_TURTLE_IDLE_YAWN, 0);
    CHECK(fetch());
    CHECK(cached(CLIP_TURTLE_IDLE_YAWN, 0));
    uint32_t b = blockOf(CLIP_SQUIRE_THRUST, 0);
    want(CLIP_SQUIRE_THRUST, 0);
    want(CLIP_SQUIRE_THRUST, 1);
    want(CLIP_SQUIRE_THRUST, 2);
    CHECK(blockOf(CLIP_SQUIRE_THRUST, 2) == b + 2);
    failAfter = 1;
    CHECK(!fetch());
    CHECK(cached(CLIP_SQUIRE_THRUST, 0) && !cached(CLIP_SQUIRE_THRUST, 1) && !cached(CLIP_SQUIRE_THRUST, 2));
    CHECK(cached(CLIP_TURTLE_IDLE_YAWN, 0) && sound());
    CHECK(sameAsCard(CLIP_SQUIRE_THRUST, 0, peek(CLIP_SQUIRE_THRUST, 0)));
}

// More wants than the list holds: the least important give way.
static void testWantsFull() {
    fresh(2, 64);
    uint32_t n = 0;                             // the squire's frames: all distinct
    for (uint8_t c = CLIP_SQUIRE_IDLE; c <= CLIP_SQUIRE_DEATH && n < MAX_WANTS + 4; c++)
        for (uint8_t f = 0; f < BANK_CLIPS[c].frames && n < MAX_WANTS + 4; f++, n++) want(c, f, PRIO_PREFETCH);
    CHECK(n == MAX_WANTS + 4);
    want(CLIP_RACCOON_RUN, 7, PRIO_PLAYER);
    CHECK(fetch());
    CHECK(stats().wants == MAX_WANTS && cached(CLIP_RACCOON_RUN, 7));
}

// Random screens: actors wanting random frames at random priorities, some
// drawing what they wanted; after every fetch the directory is sound, every
// frame wanted at it and cached before it is still there, and the cached
// bytes are the card's.
static void testStress() {
    fresh(2, 6);
    uint32_t rng = 99, evicted = 0, compacted = 0, deferred = 0, moved = 0, maxMoved = 0;
    auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    Pick actor[10];
    for (Pick &a : actor) a = {(uint8_t)(rnd() % CLIP_COUNT), 0};
    for (int screen = 0; screen < 4000; screen++) {
        std::vector<Pick> pinned;
        uint32_t n = 3 + rnd() % 8;
        for (uint32_t i = 0; i < n; i++) {
            Pick &a = actor[i];
            if (rnd() % 20 == 0) a.c = (uint8_t)(rnd() % CLIP_COUNT), a.f = 0;
            else if (rnd() % 4 == 0) a.f = (uint8_t)((a.f + 1) % BANK_CLIPS[a.c].frames);
            if (cached(a.c, a.f)) pinned.push_back(a);
            want(a.c, a.f, (uint8_t)(i == 0 ? PRIO_PLAYER : PRIO_CRITTER));
            if (rnd() % 3 == 0) want(a.c, (uint8_t)((a.f + 1) % BANK_CLIPS[a.c].frames), PRIO_PREFETCH);
        }
        CHECK(fetch());
        CHECK(stats().cmds <= 2 && stats().blocks <= 6);
        evicted += stats().evicted;
        compacted += stats().compacted;
        deferred += stats().deferred;
        moved += stats().moved;
        if (stats().moved > maxMoved) maxMoved = stats().moved;
        for (const Pick &p : pinned) CHECK(cached(p.c, p.f));
        if (!sound()) { CHECK(false); break; }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *p = frame(actor[i].c, actor[i].f);
            if (p) CHECK(sameAsCard(actor[i].c, actor[i].f, p));
        }
        if (fails > 20) break;
    }
    const Totals &t = totals();
    printf("stress: 4000 screens, %u commands, %u blocks, %u misses, %u loaded, %u deferred, "
           "%u evicted, %u compactions (%u B moved, at most %u in a fetch)\n", (unsigned)t.cmds,
           (unsigned)t.blocks, (unsigned)t.misses, (unsigned)t.loaded, (unsigned)deferred, (unsigned)evicted,
           (unsigned)compacted, (unsigned)moved, (unsigned)maxMoved);
    CHECK(evicted > 0 && compacted > 0);
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
    CHECK(file.size() == CARD_BLOCKS * 512u);
    testEveryFrame();
    testSiblings();
    testPlan();
    testEviction();
    testCompaction();
    testCardFailure();
    testWantsFull();
    testStress();
    printf("spool: %d checks, %d failed (arena %d B, %d clips, %d bank blocks)\n", checks, fails, ECRT_ARENA,
           CLIP_COUNT, BANK_BLOCKS);
    return fails ? 1 : 0;
}
