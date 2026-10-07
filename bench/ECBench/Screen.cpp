// ECBench's screen: a page per group of results in the 3x5 font, a title
// bar and a status line. LEFT / RIGHT turn the pages, the debug protocol's
// V<n> shows page n.
#pragma GCC optimize("Os")
#include "Bench.h"

namespace bench {

uint8_t page;
static char stat[40] = "BOOT";

static const char *const TITLE[PAGES] = {"CARD/COLD", "STREAM", "SOAK/RECOVER", "DRAW"};

static void txt(int x, int y, const char *s, uint8_t c) { text35(x, y, s, c); }

// v right-aligned so its last digit ends at xr.
static void num(int xr, int y, int32_t v, uint8_t c) {
    char b[12];
    fmtInt(b, v);
    text35(xr - text35Width(b) + 2, y, b, c);
}

// s right-aligned to end at xr, like num().
static void rtxt(int xr, int y, const char *s, uint8_t c) { text35(xr - text35Width(s) + 2, y, s, c); }

// Tenths, "12.3".
static void dec(int xr, int y, int32_t t, uint8_t c) {
    char b[14], *p = fmtInt(b, t / 10);
    *p++ = '.';
    *p++ = (char)('0' + t % 10);
    *p = 0;
    text35(xr - text35Width(b) + 2, y, b, c);
}

// A label and a value in one of two columns.
static void pair(int col, int y, const char *label, int32_t v, uint8_t c = C_TEXT) {
    int x = col ? 66 : 2;
    txt(x, y, label, C_DIM);
    num(x + 59, y, v, c);
}

static uint8_t usColour(uint32_t us) { return us > 10000 ? C_BAD : us > 2000 ? C_ACC : C_TEXT; }

static void statusLine() {
    gfx_fillRect(0, 121, 128, 7, C_BAR);
    txt(2, 122, stat, C_TEXT);
}

void status(const char *s) {
    char *p = stat;
    for (uint32_t i = 0; s[i] && i < sizeof stat - 1; i++) *p++ = s[i];
    *p = 0;
}

static void pageCard() {
    static const char *const ST[] = {"...", "NO CARD", "NO FAT16/32", "NO BENCH.DAT", "BAD BENCH.DAT", "OK"};
    int y = 9;
    txt(2, y, "CARD", C_DIM);
    txt(22, y, ST[card.state], card.state == CARD_OK ? C_GOOD : card.state ? C_BAD : C_DIM);
    if (card.state >= CARD_NOFS) {
        txt(58, y, card.sdhc ? "SDHC" : "SDSC", C_TEXT);
        txt(78, y, card.pnm, C_TEXT);
        char b[14];
        fmtStr(fmtInt(b, (int32_t)card.capMB), "MB");
        rtxt(126, y, b, C_TEXT);
    }
    pair(0, y += 7, "INIT", (int32_t)card.initUs);
    pair(1, y, "MOUNT", (int32_t)card.mountUs);
    pair(0, y += 7, "OPEN", (int32_t)card.openUs);
    pair(1, y, "HEADER", (int32_t)card.hdrUs);
    pair(0, y += 7, "RUNS", card.nRuns);
    pair(1, y, "FILE MB", (int32_t)(card.blocks / 2048));
    y += 9;
    txt(2, y, R.coldAgain ? "FIRST READ OF EACH MB, AGAIN" : "FIRST READ OF EACH MB, BOOT", C_HEAD);
    if (!(R.have & 1 << 3)) {
        txt(2, y + 7, "-", C_DIM);
        return;
    }
    y += 7;
    uint32_t n = R.coldN < 32 ? R.coldN : 32, slow = 0, fail = 0, hi = 0;
    for (uint32_t r = 0; r < n; r++) {
        uint32_t v = R.cold[r] & 0x7FFFFFFFu;
        bool bad = R.cold[r] >> 31;
        num(30 + (int)(r / 8) * 32, y + (int)(r % 8) * 7, (int32_t)v, bad ? C_BAD : r ? usColour(v) : C_DIM);
    }
    for (uint32_t r = 1; r < R.coldN; r++) {
        uint32_t v = R.cold[r] & 0x7FFFFFFFu;
        slow += v > 10000;
        fail += R.cold[r] >> 31;
        if (v > hi) hi = v;
    }
    y += 8 * 7 + 1;
    pair(0, y, "MAX US", (int32_t)hi, usColour(hi));
    pair(1, y, "OVER 10MS", (int32_t)slow, slow ? C_BAD : C_GOOD);
    pair(0, y += 7, "FAIL", (int32_t)fail, fail ? C_BAD : C_GOOD);
    pair(1, y, "BAD", R.coldBad, R.coldBad ? C_BAD : C_GOOD);
}

static void streamRows(int y, const Pct *q) {
    for (uint32_t s = 0; s < N_SIZES; s++, y += 7) {
        num(11, y, NS[s], C_DIM);
        if (!q[s].p50) {                                 // not measured yet
            rtxt(43, y, "-", C_DIM);
            continue;
        }
        num(43, y, (int32_t)q[s].p50, usColour(q[s].p50 / 2));
        num(75, y, (int32_t)q[s].p95, usColour(q[s].p95 / 2));
        num(111, y, (int32_t)q[s].hi, usColour(q[s].hi / 2));
        num(126, y, q[s].fails + q[s].bad, q[s].fails + q[s].bad ? C_BAD : C_DIM);
    }
}

static void pageStream() {
    txt(2, 9, "RANDOM OFFSETS, US", C_HEAD);
    rtxt(11, 16, "N", C_DIM);
    rtxt(43, 16, "P50", C_DIM);
    rtxt(75, 16, "P95", C_DIM);
    rtxt(111, 16, "MAX", C_DIM);
    rtxt(126, 16, "E", C_DIM);
    if (!(R.have & 1 << 2)) {
        txt(2, 23, "-", C_DIM);
        return;
    }
    streamRows(23, R.pct[0]);
    txt(2, 65, "WARM 128 KB WINDOW", C_HEAD);
    streamRows(72, R.pct[1]);
    if (!R.pct[1][N_SIZES - 1].p50) return;              // the fits come at the end
    txt(2, 114, "CMD", C_DIM);
    num(31, 114, R.fitCmd[0], C_ACC);
    txt(35, 114, "BLK", C_DIM);
    dec(67, 114, R.fitBlk10[0], C_ACC);
    txt(73, 114, "W", C_DIM);
    num(99, 114, R.fitCmd[1], C_TEXT);
    dec(126, 114, R.fitBlk10[1], C_TEXT);
}

static void pageSoak() {
    int y = 9;
    txt(2, y, "SOAK: 17 BLOCKS + FLUSH", C_HEAD);
    if (R.have & 1 << 4) {
        pair(0, y += 7, "FRAMES", (int32_t)R.soakFrames);
        pair(1, y, "BLOCKS", (int32_t)R.soakBlocks);
        pair(0, y += 7, "BAD", (int32_t)R.soakBad, R.soakBad ? C_BAD : C_GOOD);
        pair(1, y, "FAIL", (int32_t)R.soakFails, R.soakFails ? C_BAD : C_GOOD);
        pair(0, y += 7, "RECOVER", (int32_t)R.soakRecovers);
        pair(1, y, "INIT", (int32_t)R.soakInits);
        pair(0, y += 7, "AVG US", (int32_t)R.soakAvg, usColour(R.soakAvg * 2));
        pair(1, y, "MAX US", (int32_t)R.soakHi, usColour(R.soakHi));
        pair(0, y += 7, "OVER 4MS", (int32_t)R.soakOver, R.soakOver ? C_ACC : C_GOOD);
    } else {
        txt(2, y += 7, "-", C_DIM);
        y += 28;
    }
    y += 9;
    txt(2, y, "RECOVERY, TIMEOUT US", C_HEAD);
    num(126, y, (int32_t)R.shortUs, C_HEAD);
    if (!(R.have & 1 << 8)) {
        txt(2, y + 7, "-", C_DIM);
        return;
    }
    pair(0, y += 7, "COLD MBS", R.rgn);
    pair(1, y, "TIMED OUT", R.rgnTimeouts, R.rgnTimeouts ? C_ACC : C_GOOD);
    pair(0, y += 7, "LOST", R.rgnLost, R.rgnLost ? C_BAD : C_GOOD);
    pair(1, y, "TRIES MAX", R.triesHi);
    pair(0, y += 7, "DATA P50", (int32_t)R.toDataP50, usColour(R.toDataP50));
    pair(1, y, "DATA MAX", (int32_t)R.toDataHi, usColour(R.toDataHi));
    pair(0, y += 7, "FORCED", R.forced);
    pair(1, y, "RECOVERED", R.forcedRecovered, R.forcedRecovered == R.forced ? C_GOOD : C_BAD);
    pair(0, y += 7, "REREAD OK", R.forcedReread, R.forcedReread == R.forced ? C_GOOD : C_BAD);
    pair(1, y, "RCV IDLE", (int32_t)R.recIdle);
    pair(0, y += 7, "RECOVER", (int32_t)R.recAvg);
    pair(1, y, "RCV MAX", (int32_t)R.recHi);
    pair(0, y += 7, "INIT", (int32_t)R.initAvg);
    pair(1, y, "INIT MAX", (int32_t)R.initHi);
}

static void pageDraw() {
    static const char *const NAME[DRAW_N] = {
        "BLIT 32X32 EVEN X", "BLIT 32X32 ODD X", "SPRITE4 24X21", "SPRITE4 + REMAP", "SPRITE4 + FLIP H",
        "REMAPRECT 24X8", "COPY 16 BLOCKS /SCR", "COLUMN P=0", "COLUMN P=3", "WORLD 16 COLS /SCR",
        "WORLD 17 COLS /SCR", "FLUSH START", "FLUSH WIRE", "B4K X%8=0", "B4K X%8=3", "B4K FLIP", "B4K REMAP",
        "B4K DITH8", "B4K RM+FL", "B4K SOLID", "B4K 24X21", "B4K 24 RM",
    };
    txt(2, 8, "DRAWING, US PER CALL", C_HEAD);
    if (!(R.have & 1 << 5)) {
        txt(2, 16, "-", C_DIM);
        return;
    }
    // the column copy check in the heading's corner: 6 px a row fits 18 rows,
    // so gfx_blit4k's (32x32 but for the last two) go two a row
    if (R.colBad) num(126, 8, (int32_t)R.colBad, C_BAD);
    else txt(98, 8, "COL OK", C_GOOD);
    for (uint32_t i = 0; i < DRAW_N; i++) {
        uint32_t k = i < D_B4_EVEN ? i * 2 : D_B4_EVEN * 2 + (i - D_B4_EVEN);
        int x = (int)(k & 1) * 64, y = 14 + (int)(k / 2) * 6;
        txt(x + 2, y, NAME[i], C_DIM);
        dec(i < D_B4_EVEN ? 126 : x + 62, y, (int32_t)R.draw[i], C_TEXT);
    }
}

void drawPage() {
    gfx_clear(C_BG);
    gfx_fillRect(0, 0, 128, 7, C_BAR);
    txt(2, 1, "ECBENCH " ECBN_VERSION, C_HEAD);
    txt(52, 1, TITLE[page], C_TEXT);
    char b[6];
    fmtInt(fmtStr(fmtInt(b, page + 1), "/"), PAGES);
    rtxt(126, 1, b, C_DIM);
    switch (page) {
        case PG_CARD: pageCard(); break;
        case PG_STREAM: pageStream(); break;
        case PG_SOAK: pageSoak(); break;
        default: pageDraw(); break;
    }
    statusLine();
}

void step(const char *what, uint32_t i, uint32_t n) {
    char *p = fmtStr(stat, what);
    *p++ = ' ';
    p = fmtInt(p, (int32_t)i);
    *p++ = '/';
    fmtInt(p, (int32_t)n);
    alive(stat);
    statusLine();
    gfx_fillRect(0, 127, (int)(i * 128 / (n ? n : 1)), 1, C_ACC);
    gfx_flushAsync();
}

void show(uint8_t pg) {
    page = pg;
    gfx_wait();
    drawPage();
    gfx_flushAsync();
}

}  // namespace bench
