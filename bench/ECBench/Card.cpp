// The card benchmarks: B1 (init, mount, open), B3 (cold first reads), B2
// (stream latency), B4 (soak) and B8 (timeouts and recovery). Every step
// is a frame as the game will have it: gfx_wait(), the card, a little
// drawing, gfx_flushAsync() (8.4 ms on the wire, the card's bus busy).
// Only the card calls are timed.
#pragma GCC optimize("Os")
#include <string.h>
#include "Bench.h"

namespace bench {

static uint32_t samples[ECBN_ITERS > 256 ? ECBN_ITERS : 256];

static const uint32_t FAILED = 0x80000000u;

// ---- B1 -------------------------------------------------------------------
// The card's maker and product (CID) and size (CSD, version 1 or 2).
static void cardInfo() {
    uint8_t r[16];
    card.sdhc = sd::isSdhc();
    if (sd::readReg(10, r)) {
        card.mid = r[0];
        for (uint32_t i = 0; i < 5; i++) {
            char ch = (char)r[3 + i];
            card.pnm[i] = ch >= ' ' && ch <= 'z' ? ch : '?';
        }
        card.pnm[5] = 0;
    }
    if (sd::readReg(9, r)) {
        if ((r[0] >> 6) == 1) {                        // (C_SIZE + 1) * 512 KB
            card.capMB = ((((uint32_t)r[7] & 0x3F) << 16 | (uint32_t)r[8] << 8 | r[9]) + 1) >> 1;
        } else {                                       // (C_SIZE + 1) << (C_SIZE_MULT + 2 + READ_BL_LEN)
            uint32_t cs = ((uint32_t)r[6] & 3) << 10 | (uint32_t)r[7] << 2 | r[8] >> 6;
            uint32_t sh = (((uint32_t)r[9] & 3) << 1 | r[10] >> 7) + 2 + (r[5] & 15);
            card.capMB = sh >= 20 ? (cs + 1) << (sh - 20) : (cs + 1) >> (20 - sh);
        }
    }
}

// init, mount, find + runs, the header: each timed. At boot this is the
// card's first use since power-up.
static bool openCard() {
    gfx_wait();
    uint8_t *b = gfx_chunkScratch();
    card.state = CARD_NONE;
    uint32_t t0 = micros();
    bool ok = sd::init();
    card.initUs = micros() - t0;
    if (!ok) return false;
    cardInfo();
    card.state = CARD_NOFS;
    t0 = micros();
    card.mountRc = fat::mount(b);
    card.mountUs = micros() - t0;
    if (card.mountRc) return false;
    card.state = CARD_NOFILE;
    fat::File f;
    t0 = micros();
    card.findRc = fat::find(ECBN_FILE, f, b);
    int8_t n = card.findRc ? 0 : fat::runs(f, card.runs, ECBN_MAX_RUNS, b);
    card.openUs = micros() - t0;
    if (card.findRc || n <= 0) {
        if (!card.findRc) card.findRc = n;
        return false;
    }
    card.nRuns = (uint8_t)n;
    card.state = CARD_BADFILE;
    t0 = micros();
    ok = fat::read(card.runs, card.nRuns, 0, b);
    card.hdrUs = micros() - t0;
    const uint32_t *h = (const uint32_t *)b;
    if (!ok || h[0] != 0x4E424345u || h[1] != 0x31544144u) return false;   // "ECBN" "DAT1"
    card.blocks = h[2];
    if (card.blocks < 2048 + 64 || card.blocks > f.size / 512) return false;
    card.state = CARD_OK;
    return true;
}

bool bootCard() {
    bool ok = openCard();
    R.have |= 1 << 1;
    return ok;
}

static const char *const STATES[] = {"wait", "nocard", "nofs", "nofile", "badfile", "ok"};

void reportB1(bool again) {
    if (again) {
        status("B1 AGAIN");
        openCard();
    }
    char *p = line("B1");
    p = ks(p, "when", again ? "again" : "boot");
    p = ks(p, "card", STATES[card.state]);
    p = kv(p, "init_us", (int32_t)card.initUs);
    p = kv(p, "mount_us", (int32_t)card.mountUs);
    p = kv(p, "open_us", (int32_t)card.openUs);
    p = kv(p, "header_us", (int32_t)card.hdrUs);
    p = kv(p, "runs", card.nRuns);
    p = kv(p, "blocks", (int32_t)card.blocks);
    emit(p);
    p = line("B1");
    p = ks(p, "type", card.sdhc ? "SDHC/SDXC" : "SDSC");
    p = kv(p, "mid", card.mid);
    p = ks(p, "product", card.pnm[0] ? card.pnm : "?");
    p = kv(p, "card_mb", (int32_t)card.capMB);
    p = kv(p, "mount_rc", card.mountRc);
    p = kv(p, "find_rc", card.findRc);
    emit(p);
    for (uint32_t i = 0; i < card.nRuns; i++) {
        p = line("B1 run");
        p = kv(p, "i", (int32_t)i);
        p = kv(p, "lba", (int32_t)card.runs[i].lba);
        p = kv(p, "blocks", (int32_t)card.runs[i].blocks);
        emit(p);
    }
    show(PG_CARD);
}

// ---- B3 -------------------------------------------------------------------
// A block every 1 MB, each the first read of its megabyte: at boot, before
// anything but the header has been read, so the card is as cold as it gets.
// MB 0 holds the header, read just before: the warm reference.
void coldSweep(bool boot) {
    uint32_t n = card.blocks / 2048;
    if (n > ECBN_COLD_MAX) n = ECBN_COLD_MAX;
    R.coldN = (uint8_t)n;
    R.coldAgain = !boot;
    R.coldFail = R.coldBad = 0;
    show(PG_CARD);
    for (uint32_t r = 0; r < n; r++) {
        gfx_wait();
        Check c;
        c.verify = true;
        uint32_t t0 = micros();
        bool ok = readFile(r ? r * 2048 : 1, 1, c);
        uint32_t dt = micros() - t0;
        R.cold[r] = ok ? dt : dt | FAILED;
        if (!ok) {
            R.coldFail++;
            heal();
        }
        R.coldBad += (uint16_t)c.bad;
        step("B3 COLD", r + 1, n);
    }
    R.have |= 1 << 3;
}

void reportB3() {
    uint32_t n = R.coldN;
    if (!n) {
        emit(ks(line("B3"), "error", "no file"));
        return;
    }
    for (uint32_t r = 0; r < n; r++) {
        char *p = line("B3 mb");
        p = kv(p, "r", (int32_t)r);
        p = kv(p, "us", (int32_t)(R.cold[r] & ~FAILED));
        p = kv(p, "ok", !(R.cold[r] & FAILED));
        emit(p);
    }
    // Everything past the warm MB 0.
    uint32_t slow = 0;
    for (uint32_t r = 1; r < n; r++) {
        samples[r - 1] = R.cold[r] & ~FAILED;
        slow += samples[r - 1] > 10000;
    }
    uint32_t p95 = 0, hi = 0, p50 = n > 1 ? pct(samples, n - 1, &p95, &hi) : 0;
    char *p = line("B3");
    p = ks(p, "when", R.coldAgain ? "again" : "boot");
    p = kv(p, "mbs", (int32_t)n);
    p = kv(p, "warm_us", (int32_t)(R.cold[0] & ~FAILED));
    p = kv(p, "p50", (int32_t)p50);
    p = kv(p, "p95", (int32_t)p95);
    p = kv(p, "max", (int32_t)hi);
    p = kv(p, "over10ms", (int32_t)slow);
    p = kv(p, "fail", R.coldFail);
    p = kv(p, "bad", R.coldBad);
    emit(p);
    show(PG_CARD);
}

// ---- B2 -------------------------------------------------------------------
// Does [k, k + n) cross from one of the file's runs to the next (a second
// command)?
static bool splits(uint32_t k, uint32_t n) {
    uint32_t at = 0;
    for (uint32_t i = 0; i + 1 < card.nRuns; i++) {
        at += card.runs[i].blocks;
        if (k < at && k + n > at) return true;
    }
    return false;
}

// Least squares through the six medians: latency = cmd + n * blk.
static void fit(const Pct *p, int32_t &cmd, int32_t &blk10) {
    int64_t sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (uint32_t i = 0; i < N_SIZES; i++) {
        int64_t x = NS[i], y = p[i].p50;
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    int64_t d = N_SIZES * sxx - sx * sx;
    blk10 = (int32_t)((N_SIZES * sxy - sx * sy) * 10 / d);
    cmd = (int32_t)((sy * 10 - blk10 * sx) / (N_SIZES * 10));
}

static void reportPct(const char *mode, uint32_t n, const Pct &q, uint32_t iters) {
    char *p = line("B2");
    p = ks(p, "mode", mode);
    p = kv(p, "n", (int32_t)n);
    p = kv(p, "iters", (int32_t)iters);
    p = kv(p, "p50", (int32_t)q.p50);
    p = kv(p, "p95", (int32_t)q.p95);
    p = kv(p, "max", (int32_t)q.hi);
    p = kv(p, "fail", q.fails);
    p = kv(p, "bad", q.bad);
    emit(p);
}

// n blocks at pseudo-random offsets, over the whole file ("rnd": a new
// region nearly every time) and inside a 128 KB window ("warm"), each read
// followed by a full-screen flush as in the game.
void streamBench(uint32_t iters) {
    if (card.state != CARD_OK) {
        emit(ks(line("B2"), "error", "no file"));
        return;
    }
    if (iters < 8) iters = 8;
    if (iters > sizeof samples / sizeof samples[0]) iters = sizeof samples / sizeof samples[0];
    R.iters = (uint16_t)iters;
    R.splits = 0;
    memset(R.pct, 0, sizeof R.pct);
    R.fitCmd[0] = R.fitCmd[1] = R.fitBlk10[0] = R.fitBlk10[1] = 0;
    R.have |= 1 << 2;                                // the page shows the rows as they come
    show(PG_STREAM);
    static const char *const MODE[2] = {"rnd", "warm"};
    const uint32_t warm0 = card.blocks / 3;
    for (uint32_t m = 0; m < 2; m++) {
        for (uint32_t s = 0; s < N_SIZES; s++) {
            uint32_t n = NS[s];
            Pct &q = R.pct[m][s];
            q.fails = q.bad = 0;
            seed(0xEC0B0000u + m * 64 + n);
            for (uint32_t i = 0; i < iters; i++) {
                uint32_t k = m ? warm0 + rnd() % (ECBN_WARM_SPAN - n) : 1 + rnd() % (card.blocks - 1 - n);
                R.splits += splits(k, n);
                gfx_wait();
                Check c;
                c.verify = false;
                uint32_t t0 = micros();
                bool ok = readFile(k, n, c);
                samples[i] = micros() - t0;
                if (!ok) {
                    q.fails++;
                    heal();
                }
                q.bad += (uint16_t)c.bad;
                char what[16];
                fmtStr(fmtInt(fmtStr(what, "B2 N"), (int32_t)n), m ? " WARM" : " RND");
                step(what, i + 1, iters);
            }
            q.p50 = pct(samples, iters, &q.p95, &q.hi);
            reportPct(MODE[m], n, q, iters);
            show(PG_STREAM);
        }
        fit(R.pct[m], R.fitCmd[m], R.fitBlk10[m]);
        char *p = line("B2 fit");
        p = ks(p, "mode", MODE[m]);
        p = kv(p, "cmd_us", R.fitCmd[m]);
        p = kd(p, "blk_us", R.fitBlk10[m]);
        emit(p);
    }
    emit(kv(line("B2"), "run_splits", R.splits));
    // A file in pieces: 17 blocks across a boundary between runs, which
    // fat::stream() reads as two commands.
    Pct &q = R.split;
    q.p50 = q.p95 = q.hi = q.fails = q.bad = 0;
    if (card.nRuns > 1) {
        uint32_t m = iters / 4;
        seed(0xEC0B5555u);
        for (uint32_t i = 0; i < m; i++) {
            uint32_t at = 0, b = i % (card.nRuns - 1);
            for (uint32_t j = 0; j <= b; j++) at += card.runs[j].blocks;
            uint32_t k = at - 1 - rnd() % (at > 17 ? 16 : at - 1);    // (never the header)
            gfx_wait();
            Check c;
            c.verify = false;
            uint32_t t0 = micros();
            bool ok = readFile(k, 17, c);
            samples[i] = micros() - t0;
            if (!ok) {
                q.fails++;
                heal();
            }
            q.bad += (uint16_t)c.bad;
            step("B2 SPLIT", i + 1, m);
        }
        q.p50 = pct(samples, m, &q.p95, &q.hi);
        reportPct("split", 17, q, m);
    }
    R.have |= 1 << 2;
    show(PG_STREAM);
}

// ---- B4 -------------------------------------------------------------------
// The game's world read, over and over: 17 blocks at a random offset, every
// block's pattern checked in the callback, then a full flush. Counts what
// went wrong; a failed stream is healed (recover(), else init()) and the
// soak goes on.
void soak(uint32_t frames, uint32_t timeoutUs) {
    if (card.state != CARD_OK) {
        emit(ks(line("B4"), "error", "no file"));
        return;
    }
    if (!frames) frames = ECBN_SOAK;
    if (!timeoutUs) timeoutUs = ECBN_TIMEOUT_US;
    sd::setStreamTimeout(timeoutUs);
    R.soakFrames = R.soakBlocks = R.soakBad = R.soakFails = R.soakRecovers = R.soakInits = 0;
    R.soakHi = R.soakOver = 0;
    R.soakTimeout = timeoutUs;
    R.have |= 1 << 4;
    uint64_t sum = 0;                                // (20,000 frames of 1 s timeouts overflow 32 bits)
    show(PG_SOAK);
    seed(0xEC0B4444u);
    for (uint32_t f = 0; f < frames; f++) {
        uint32_t k = 1 + rnd() % (card.blocks - 1 - ECBN_SOAK_BLK);
        gfx_wait();
        Check c;
        c.verify = true;
        uint32_t t0 = micros();
        bool ok = readFile(k, ECBN_SOAK_BLK, c);
        uint32_t dt = micros() - t0;
        if (!ok) {
            R.soakFails++;
            if (heal()) R.soakRecovers++;
            else R.soakInits++;
        }
        R.soakFrames++;
        R.soakBlocks += c.got;
        R.soakBad += c.bad;
        sum += dt;
        if (dt > R.soakHi) R.soakHi = dt;
        R.soakOver += dt > ECBN_BUDGET_US;
        // A line now and then: the board's serial reader gives up after 30 s
        // without one.
        if ((f + 1) % 250 == 0 && f + 1 < frames) {
            char *p = line("B4 progress");
            p = kv(p, "frames", (int32_t)(f + 1));
            p = kv(p, "bad", (int32_t)R.soakBad);
            p = kv(p, "fail", (int32_t)R.soakFails);
            emit(p);
        }
        if ((f + 1) % 64 == 0) {                     // the counts so far (outside the timing)
            R.soakAvg = (uint32_t)(sum / R.soakFrames);
            drawPage();
        }
        step("B4 SOAK", f + 1, frames);
    }
    sd::setStreamTimeout(ECBN_TIMEOUT_US);
    R.soakAvg = R.soakFrames ? (uint32_t)(sum / R.soakFrames) : 0;
    char *p = line("B4");
    p = kv(p, "frames", (int32_t)R.soakFrames);
    p = kv(p, "blocks", (int32_t)R.soakBlocks);
    p = kv(p, "bad", (int32_t)R.soakBad);
    p = kv(p, "fail", (int32_t)R.soakFails);
    p = kv(p, "recovered", (int32_t)R.soakRecovers);
    p = kv(p, "inits", (int32_t)R.soakInits);
    emit(p);
    p = line("B4");
    p = kv(p, "avg_us", (int32_t)R.soakAvg);
    p = kv(p, "max_us", (int32_t)R.soakHi);
    p = kv(p, "over_budget", (int32_t)R.soakOver);
    p = kv(p, "budget_us", ECBN_BUDGET_US);
    p = kv(p, "timeout_us", (int32_t)timeoutUs);
    emit(p);
    R.have |= 1 << 4;
    show(PG_SOAK);
}

// ---- B8 -------------------------------------------------------------------
// 1. Never-read blocks (the middle of each MB: B3 read the starts) with a
//    short stream timeout, as a game would set it: how often it runs out,
//    and how long to the data when every frame tries again after recover().
// 2. recover() on a healthy card, and after a forced timeout (a 1 us token
//    wait on a warm block): does the next stream work without init()?
// 3. init() itself.
void recoveryBench(uint32_t shortUs) {
    if (card.state != CARD_OK) {
        emit(ks(line("B8"), "error", "no file"));
        return;
    }
    if (!shortUs) shortUs = ECBN_SHORT_US;
    R.shortUs = shortUs;
    show(PG_SOAK);
    uint32_t n = card.blocks / 2048;
    if (n > ECBN_COLD_MAX) n = ECBN_COLD_MAX;
    R.rgn = (uint8_t)n;
    R.rgnTimeouts = R.rgnLost = 0;
    R.triesHi = 0;
    uint32_t recSum = 0, recN = 0;
    R.recHi = 0;
    sd::setStreamTimeout(shortUs);
    for (uint32_t r = 0; r < n; r++) {
        uint32_t k = r * 2048 + 1024, tries = 0, timeouts = 0, t0 = 0;
        bool ok;
        Check c;
        c.verify = true;
        do {
            gfx_wait();
            if (!tries) t0 = micros();                   // from the first try, the frames between included
            ok = readFile(k, 1, c);
            tries++;
            if (!ok) {
                timeouts++;
                uint32_t t1 = micros();
                sd::recover();
                uint32_t d = micros() - t1;
                recSum += d;
                recN++;
                if (d > R.recHi) R.recHi = d;
            }
            step("B8 COLD", r + 1, n);
        } while (!ok && tries < ECBN_TRIES);
        uint32_t dt = micros() - t0;
        samples[r] = dt;
        R.rgnTimeouts += timeouts > 0;
        R.rgnLost += !ok;
        if (tries > R.triesHi) R.triesHi = (uint16_t)tries;
        char *p = line("B8 mb");
        p = kv(p, "r", (int32_t)r);
        p = kv(p, "tries", (int32_t)tries);
        p = kv(p, "to_data_us", (int32_t)dt);
        p = kv(p, "ok", ok && !c.bad);
        emit(p);
    }
    uint32_t p95 = 0;
    R.toDataHi = 0;
    R.toDataP50 = n ? pct(samples, n, &p95, &R.toDataHi) : 0;

    // recover() with nothing wrong: its floor.
    gfx_wait();
    uint32_t t0 = micros();
    for (uint32_t i = 0; i < 16; i++) sd::recover();
    R.recIdle = (micros() - t0) / 16;

    // Forced timeouts on warm blocks, then recover() and a normal read.
    R.forced = R.forcedRecovered = R.forcedReread = 0;
    seed(0xEC0B8888u);
    for (uint32_t i = 0; i < ECBN_FORCED; i++) {
        uint32_t k = 1 + rnd() % (card.blocks - 2);
        gfx_wait();
        Check c;
        c.verify = true;
        sd::setStreamTimeout(1);
        bool ok = readFile(k, 1, c);
        sd::setStreamTimeout(shortUs);
        if (!ok) {
            R.forced++;
            uint32_t t1 = micros();
            bool rec = sd::recover();
            uint32_t d = micros() - t1;
            recSum += d;
            recN++;
            if (d > R.recHi) R.recHi = d;
            R.forcedRecovered += rec;
            if (!rec) sd::init();
            sd::setStreamTimeout(ECBN_TIMEOUT_US);
            R.forcedReread += readFile(k, 1, c) && !c.bad;
            sd::setStreamTimeout(shortUs);
        }
        step("B8 FORCED", i + 1, ECBN_FORCED);
    }
    R.recAvg = recN ? recSum / recN : 0;
    sd::setStreamTimeout(ECBN_TIMEOUT_US);

    // init(): the identification at 187.5 kHz, card warm.
    R.initHi = 0;
    uint32_t initSum = 0;
    for (uint32_t i = 0; i < 4; i++) {
        gfx_wait();
        t0 = micros();
        sd::init();
        uint32_t d = micros() - t0;
        initSum += d;
        if (d > R.initHi) R.initHi = d;
        step("B8 INIT", i + 1, 4);
    }
    R.initAvg = initSum / 4;

    char *p = line("B8 cold");
    p = kv(p, "timeout_us", (int32_t)shortUs);
    p = kv(p, "mbs", (int32_t)n);
    p = kv(p, "timed_out", R.rgnTimeouts);
    p = kv(p, "lost", R.rgnLost);
    p = kv(p, "tries_max", R.triesHi);
    p = kv(p, "to_data_p50", (int32_t)R.toDataP50);
    p = kv(p, "to_data_max", (int32_t)R.toDataHi);
    emit(p);
    p = line("B8 forced");
    p = kv(p, "n", R.forced);
    p = kv(p, "recovered", R.forcedRecovered);
    p = kv(p, "reread_ok", R.forcedReread);
    emit(p);
    p = line("B8 cost");
    p = kv(p, "recover_us", (int32_t)R.recAvg);
    p = kv(p, "recover_max", (int32_t)R.recHi);
    p = kv(p, "recover_idle", (int32_t)R.recIdle);
    p = kv(p, "init_us", (int32_t)R.initAvg);
    p = kv(p, "init_max", (int32_t)R.initHi);
    emit(p);
    R.have |= 1 << 8;
    show(PG_SOAK);
}

#ifdef CHSIM
// The simulator's bus-rule check, tripped on purpose: a stream while the
// flush is still on the wire (the simulator reports a BUG).
void busViolation() {
    gfx_wait();
    gfx_flushAsync();
    Check c;
    c.verify = false;
    readFile(1, 1, c);
    gfx_wait();
}
#else
void busViolation() {}
#endif

}  // namespace bench
