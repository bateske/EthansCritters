// ECBench's shared parts: the results, the output lines, the card file's
// pattern and the stream helpers every card benchmark uses.
#pragma GCC optimize("Os")
#include "Bench.h"

namespace bench {

Card card;
Results R;
const uint8_t NS[N_SIZES] = {1, 2, 4, 8, 17, 32};

// ---- Output ---------------------------------------------------------------
// One line at a time; the tools read every line that starts "ECBN ".
static char buf[128];

char *line(const char *tag) { return fmtStr(fmtStr(buf, "ECBN "), tag); }

static char *key(char *p, const char *k) {
    *p++ = ' ';
    p = fmtStr(p, k);
    *p++ = '=';
    return p;
}

char *kv(char *p, const char *k, int32_t v) { return fmtInt(key(p, k), v); }
char *ks(char *p, const char *k, const char *s) { return fmtStr(key(p, k), s); }

char *kd(char *p, const char *k, int32_t t) {
    p = key(p, k);
    if (t < 0) { *p++ = '-'; t = -t; }
    p = fmtInt(p, t / 10);
    *p++ = '.';
    *p++ = (char)('0' + t % 10);
    *p = 0;
    return p;
}

static uint32_t lastOut;            // millis() of the last line

void emit(char *end) {
    end[0] = '\n';
    end[1] = 0;
    dbg::print(buf);
    lastOut = millis();
}

// The board's serial reader gives up after 30 s without a line, and a
// failing card (1 s per timeout, init() after) can stretch the quiet stretch
// between results that far: every 10 s a line the tools print but do not
// keep (it does not start "ECBN ").
void alive(const char *what) {
    if (millis() - lastOut < 10000) return;
    emit(fmtStr(fmtStr(buf, "... "), what));
}

// ---- The pattern -----------------------------------------------------------
// Block k >= 1 of BENCH.DAT: word 0 is k, words 1..127 the xorshift32 run
// from k * 2654435761 + 1. In SRAM: it checks every block the soak reads,
// inside the stream callback, where it must finish while the next block's
// 512 bytes arrive (about 170 us at 24 MHz).
RAMFUNC(ecbnPattern) bool patternOk(const uint8_t *blk, uint32_t k) {
    const uint32_t *w = (const uint32_t *)blk;
    uint32_t x = k * 2654435761u + 1, d = w[0] ^ k;
    if (!x) x = 1;
    for (uint32_t i = 1; i < 128; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        d |= w[i] ^ x;
    }
    return !d;
}

static uint32_t rs = 1;
void seed(uint32_t s) { rs = s ? s : 1; }
uint32_t rnd() {
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

// Insertion sort: 200 samples, once per result.
uint32_t pct(uint32_t *v, uint32_t n, uint32_t *p95, uint32_t *hi) {
    for (uint32_t i = 1; i < n; i++) {
        uint32_t x = v[i], j = i;
        for (; j && v[j - 1] > x; j--) v[j] = v[j - 1];
        v[j] = x;
    }
    *p95 = v[n * 95 / 100];
    *hi = v[n - 1];
    return v[n / 2];
}

// ---- Streaming ------------------------------------------------------------
void onBlock(const uint8_t *blk, void *ctx) {
    Check &c = *(Check *)ctx;
    bool ok = c.verify ? patternOk(blk, c.next) : *(const uint32_t *)blk == c.next;
    c.bad += !ok;
    c.next++;
    c.got++;
}

// Blocks [k, k + n) of BENCH.DAT into the chunk scratch's two halves, as
// the game will read its card file (between gfx_wait() and the flush).
bool readFile(uint32_t k, uint32_t n, Check &c) {
    uint8_t *b = gfx_chunkScratch();
    c.next = k;
    c.got = c.bad = 0;
    return fat::stream(card.runs, card.nRuns, k, n, b, b + GFX_CHUNK_BYTES, onBlock, &c);
}

// After a failed stream: the cheap way (CMD12 + CMD13), else a full init()
// (the FAT state and the runs stay valid: the card is the same).
bool heal() {
    if (sd::recover()) return true;
    sd::init();
    return false;
}

}  // namespace bench
