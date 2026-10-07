// The frame cache. See Spool.h.
//
// The arena is a heap of frames with a directory sorted by offset: a new
// frame goes in the first hole big enough (its exact size is in the flash
// table BANK_FSIZE, so holes get reused exactly), evicting the least
// recently used frames this screen does not want until one is. Compaction
// (sliding every frame down, a word loop, inside fetch() only) is the last
// resort, when nothing more may be evicted and the free space is there but
// in pieces: compacting whenever that was enough (before evicting) moved
// ~1.2 KB a fetch in the host test's random run, eviction first moves ~3 B
// for 3% more misses.
//
// fetch() plans before it reads: each miss, by priority, gets its bytes
// reserved (a PENDING directory entry) and its block joined to a command;
// the stream callback then copies each frame from its block into its
// reservation, and the block's other frames into whatever room is left.
#include "../Size.h"        // (first: Size.h)
#include <stdint.h>
#ifdef CHTEST
uint32_t micros();
#else
#include <Arduino.h>
#endif
#include "../../config.h"
#include "../assets/Bank.h"
#include "Card.h"
#include "Spool.h"

namespace spool {

typedef uint32_t __attribute__((may_alias)) word;

struct Entry {
    uint16_t key;                   // clip << 8 | stored frame
    uint16_t off;                   // into the arena, a multiple of 4
    uint16_t size;                  // bytes; with PENDING: reserved, not filled yet
    uint16_t used;                  // the epoch it was last wanted or drawn
};
static const uint16_t PENDING = 0x8000;
static const uint16_t GAP = 4;      // blocks apart that one command still joins (3 between)

struct Want { uint16_t key; uint8_t prio; };

static word arena[ECRT_ARENA / 4];
static Entry dir[MAX_ENTRIES];
static Want wants[MAX_WANTS];
static uint8_t nDir, nWants;
static uint16_t epoch;              // one a fetch()
static uint32_t bankFirst;
static uint8_t maxCmds = 2, maxBlocks = 6;
#if CHGAME_DEBUG || defined(CHTEST)
static Stats st;
static Totals tot;
const Totals &totals() { return tot; }
void clearTotals() { tot = Totals(); }
const Stats &stats() { return st; }
#define STAT(e) e                   // (the stats: Spool.h)
#else
#define STAT(e)
#endif
// offsets are uint16_t and PENDING is a size's top bit; frames are whole words
static_assert(ECRT_ARENA % 4 == 0 && ECRT_ARENA < 0x8000, "ECRT_ARENA: a multiple of 4, under 32 KB");

ECRT_OUTLINE void setBudget(uint8_t cmds, uint8_t blocks) {
    maxCmds = cmds > 2 ? 2 : cmds;
    maxBlocks = blocks;
}

void begin(uint32_t first) {
    bankFirst = first;
    nDir = nWants = 0;
}

ECRT_OUTLINE static uint16_t keyOf(uint8_t c, uint8_t f) {
    return (uint16_t)(c << 8 | bankStored(BANK_CLIPS[c], f));
}

static int find(uint16_t key) {
    for (uint32_t i = 0; i < nDir; i++)
        if (dir[i].key == key) return (int)i;
    return -1;
}

static uint32_t sizeOf(const Entry &e) { return e.size & ~PENDING; }

static void copyWords(word *d, const word *s, uint32_t bytes) {
    for (uint32_t n = bytes / 4; n; n--) *d++ = *s++;
}

void want(uint8_t c, uint8_t f, uint8_t prio) {
    uint16_t k = keyOf(c, f);
    uint32_t worst = 0;
    for (uint32_t i = 0; i < nWants; i++) {
        if (wants[i].key == k) {
            if (prio < wants[i].prio) wants[i].prio = prio;
            return;
        }
        if (wants[i].prio > wants[worst].prio) worst = i;
    }
    if (nWants < MAX_WANTS) worst = nWants++;
    else if (wants[worst].prio <= prio) return;     // full of more important frames
    wants[worst].key = k;
    wants[worst].prio = prio;
}

const uint8_t *peek(uint8_t c, uint8_t f) {
    int i = find(keyOf(c, f));
    if (i < 0) return nullptr;
    dir[i].used = epoch;
    STAT(st.hits++);
    return (const uint8_t *)arena + dir[i].off;
}

bool cached(uint8_t c, uint8_t f) { return find(keyOf(c, f)) >= 0; }

#ifdef CHTEST
uint32_t debugDir(DebugEntry *out) {
    for (uint32_t i = 0; i < nDir; i++) {
        out[i].key = dir[i].key;
        out[i].off = dir[i].off;
        out[i].size = dir[i].size;
        out[i].used = dir[i].used;
    }
    return nDir;
}
const uint8_t *debugArena() { return (const uint8_t *)arena; }
#endif

const uint8_t *frame(uint8_t c, uint8_t f) {
    const uint8_t *p = peek(c, f);
    if (!p) {
        STAT(st.drawMisses++);
        want(c, f);
    }
    return p;
}

// --- the arena ----------------------------------------------------------------------

// The first hole of `need` bytes: the directory index to insert at and the offset.
static bool fit(uint32_t need, uint32_t &at, uint32_t &off) {
    uint32_t end = 0;
    for (uint32_t i = 0; i <= nDir; i++) {
        uint32_t next = i < nDir ? dir[i].off : ECRT_ARENA;
        if (next - end >= need) {
            at = i;
            off = end;
            return true;
        }
        if (i < nDir) end = dir[i].off + sizeOf(dir[i]);
    }
    return false;
}

ECRT_OUTLINE static void insert(uint32_t at, uint16_t key, uint32_t off, uint32_t size, uint16_t used) {
    for (uint32_t i = nDir; i > at; i--) dir[i] = dir[i - 1];
    dir[at].key = key;
    dir[at].off = (uint16_t)off;
    dir[at].size = (uint16_t)size;
    dir[at].used = used;
    nDir++;
}

static void removeAt(uint32_t i) {
    for (nDir--; i < nDir; i++) dir[i] = dir[i + 1];
}

static uint32_t usedBytes() {
    uint32_t n = 0;
    for (uint32_t i = 0; i < nDir; i++) n += sizeOf(dir[i]);
    return n;
}

// The least recently used frame this screen does not want, or -1.
static int oldest() {
    int best = -1;
    uint32_t age = 0;
    for (uint32_t i = 0; i < nDir; i++) {
        const Entry &e = dir[i];
        uint32_t a = (uint16_t)(epoch - e.used);
        if (!(e.size & PENDING) && a && a >= age) {
            best = (int)i;
            age = a;
        }
    }
    return best;
}

// Every frame slid down to close the holes (in offset order, so a forward
// word copy never overwrites what it has still to move).
static void compact() {
    uint32_t to = 0;
    for (uint32_t i = 0; i < nDir; i++) {
        Entry &e = dir[i];
        if (e.off != to) {
            if (!(e.size & PENDING)) {
                copyWords(arena + to / 4, arena + e.off / 4, e.size);
                STAT(st.moved += e.size);
            }
            e.off = (uint16_t)to;
        }
        to += sizeOf(e);
    }
    STAT(st.compacted++);
}

// Room for `need` bytes and an entry, reserved for key: evicts what this
// screen does not want, oldest first, and compacts when that is enough.
ECRT_OUTLINE static bool reserve(uint16_t key, uint32_t need) {
    for (;;) {
        uint32_t at, off;
        if (nDir < MAX_ENTRIES && fit(need, at, off)) {
            insert(at, key, off, need | PENDING, epoch);
            return true;
        }
        int i = oldest();
        if (i >= 0) {
            removeAt((uint32_t)i);
            STAT(st.evicted++);
        } else if (nDir < MAX_ENTRIES && ECRT_ARENA - usedBytes() >= need) {
            compact();
        } else {
            return false;
        }
    }
}

// --- the stream ---------------------------------------------------------------------

// Bank block blk has arrived: the frames reserved in it go to their
// places, then its other frames, while there is room (never evicting).
static void take(const uint8_t *b, uint32_t blk) {
    int clip = -1;
    for (uint32_t i = 0; i < nDir; i++) {
        Entry &e = dir[i];
        if (!(e.size & PENDING)) continue;
        const BankClip &c = BANK_CLIPS[e.key >> 8];
        uint32_t s = e.key & 255;
        uint32_t pb = bankPerBlock(c);
        if (c.first + s / pb != blk) continue;
        clip = e.key >> 8;
        const uint8_t *src = b + (s % pb) * c.slot4 * 4u;
        uint32_t size = sizeOf(e);
        if (size != BANK_FRAME_HEADER + 4u * src[0] * src[1]) {    // not the frame: dropped (wanted again later)
            removeAt(i--);
            continue;
        }
        copyWords(arena + e.off / 4, (const word *)src, size);
        e.size = (uint16_t)size;
        STAT(st.loaded++);
    }
    if (clip < 0) return;
    const BankClip &c = BANK_CLIPS[clip];
    uint32_t pb = bankPerBlock(c), s0 = (blk - c.first) * pb, s1 = s0 + pb;
    if (s1 > c.stored) s1 = c.stored;
    for (uint32_t s = s0; s < s1 && nDir < MAX_ENTRIES; s++) {
        uint16_t key = (uint16_t)(clip << 8 | s);
        if (find(key) >= 0) continue;
        const uint8_t *src = b + (s - s0) * c.slot4 * 4u;
        uint32_t size = bankSize(c, s), at, off;
        if (size != BANK_FRAME_HEADER + 4u * src[0] * src[1] || !fit(size, at, off)) continue;
        insert(at, key, off, size, (uint16_t)(epoch - 1));     // as if drawn last screen
        copyWords(arena + off / 4, (const word *)src, size);
        STAT(st.loaded++);
    }
}

static void onBlock(const uint8_t *b, void *ctx) {
    uint32_t &blk = *(uint32_t *)ctx;
    take(b, blk++);
}

struct Miss { uint16_t key, block; uint8_t prio; };
struct Cmd { uint16_t lo, hi; };

bool fetch() {
#if CHGAME_DEBUG || defined(CHTEST)
    uint32_t t0 = micros();
    tot.hits += st.hits;                    // the draws since the last fetch()
    tot.drawMisses += st.drawMisses;
    st = Stats();
#endif
    epoch++;

    // What is cached is this screen's now (never evicted below); the rest is a miss.
    Miss miss[MAX_WANTS];
    uint32_t nm = 0;
    for (uint32_t i = 0; i < nWants; i++) {
        int e = find(wants[i].key);
        if (e >= 0) {
            dir[e].used = epoch;
            continue;
        }
        const BankClip &c = BANK_CLIPS[wants[i].key >> 8];
        Miss m;
        m.key = wants[i].key;
        m.block = (uint16_t)(c.first + (wants[i].key & 255) / bankPerBlock(c));
        m.prio = wants[i].prio;
        STAT(if (m.prio < PRIO_PREFETCH) st.misses++);
        // by priority, then block (an insertion sort: a couple of dozen at most)
        uint32_t j = nm++;
        for (; j && (miss[j - 1].prio > m.prio || (miss[j - 1].prio == m.prio && miss[j - 1].block > m.block)); j--)
            miss[j] = miss[j - 1];
        miss[j] = m;
    }
    STAT(st.wants = nWants);
    nWants = 0;

    // The plan: each miss joins a command (inside it, or stretching it over
    // a gap of up to 3 blocks), or starts one, within the budget, if its
    // bytes can be reserved. A prefetch never starts a command.
    Cmd cmd[2];
    uint32_t nc = 0, blocks = 0;
    for (uint32_t i = 0; i < nm; i++) {
        const Miss &m = miss[i];
        int join = -1;
        uint32_t grow = 0;
        for (uint32_t c = 0; c < nc; c++) {
            uint32_t lo = cmd[c].lo < m.block ? cmd[c].lo : m.block;
            uint32_t hi = cmd[c].hi > m.block ? cmd[c].hi : m.block;
            uint32_t g = (hi - lo) - (uint32_t)(cmd[c].hi - cmd[c].lo);
            if (m.block + GAP >= cmd[c].lo && m.block <= cmd[c].hi + GAP && blocks + g <= maxBlocks &&
                (join < 0 || g < grow)) {
                join = (int)c;
                grow = g;
            }
        }
        if ((join < 0 && (m.prio >= PRIO_PREFETCH || nc >= maxCmds || blocks >= maxBlocks)) ||
            !reserve(m.key, bankSize(BANK_CLIPS[m.key >> 8], m.key & 255))) {
            STAT(if (m.prio < PRIO_PREFETCH) st.deferred++);
            continue;
        }
        if (join < 0) {
            cmd[nc].lo = cmd[nc].hi = m.block;
            nc++;
            blocks++;
        } else {
            if (m.block < cmd[join].lo) cmd[join].lo = m.block;
            if (m.block > cmd[join].hi) cmd[join].hi = m.block;
            blocks += grow;
        }
    }
    // two commands that ended up close: one (a command costs ~4 blocks)
    if (nc == 2) {
        Cmd &a = cmd[cmd[0].lo < cmd[1].lo ? 0 : 1], &b = cmd[cmd[0].lo < cmd[1].lo ? 1 : 0];
        if (b.lo <= a.hi + GAP && (uint32_t)(b.hi > a.hi ? b.hi : a.hi) - a.lo + 1 <= maxBlocks) {
            if (b.hi > a.hi) a.hi = b.hi;
            cmd[0] = a;
            nc = 1;
        }
    }

    bool ok = true;
    for (uint32_t c = 0; c < nc && ok; c++) {
        uint32_t blk = cmd[c].lo, n = cmd[c].hi - cmd[c].lo + 1u;
        ok = card::stream(bankFirst + blk, n, onBlock, &blk);
        STAT(st.cmds++);
        STAT(st.blocks += (uint8_t)n);
    }
    for (uint32_t i = 0; i < nDir; i++)         // reservations that got nothing
        if (dir[i].size & PENDING) removeAt(i--);

#if CHGAME_DEBUG || defined(CHTEST)
    st.entries = nDir;
    st.bytes = (uint16_t)usedBytes();
    st.us = (uint16_t)(micros() - t0);
    tot.fetches++;
    tot.cmds += st.cmds;
    tot.blocks += st.blocks;
    tot.misses += st.misses;
    tot.loaded += st.loaded;
    tot.deferred += st.deferred;
    if (st.us > tot.maxUs) tot.maxUs = st.us;
#endif
    return ok;
}

}  // namespace spool
