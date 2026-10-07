// The simulator's SD card (the stand-in for CHSd's src/SdSpi.cpp;
// tools/chsim/chsim.py compiles it for a sketch that includes CHSd): the file
// named by $CHSD_CARD is in the slot. A FAT image (*.img, e.g. from CHSd's
// tools/fatimg.py) is served as the card's blocks; any other file is put on
// a pretend FAT16 card of its own (VCard.h), so the game's FAT code runs in
// the simulator just as on the board. With the variable unset there is no
// card. The debug protocol's eject command pulls it out and puts it back.
//
// The bus rule is checked: any sd:: call while a panel flush still holds
// SPI1 (before gfx_wait()) is a BUG, as it would corrupt both on the board.
//
// Timing is virtual and set from the environment (read once, at the first
// sd:: call), so a sketch can be tried against a slow, jittery, cold or
// failing card. The defaults are what the simulator always did:
//   CHSD_SIM_CMD_US     600   stream(): a command's latency, to the first block
//   CHSD_SIM_BLK_US     170   stream(): each block at the 24 MHz wire rate
//   CHSD_SIM_READ_US    900   read(): a polled single block
//   CHSD_SIM_JITTER_US  0     up to this much more per command (read or
//                             stream), pseudo-random from a fixed seed, so a
//                             run repeats exactly
//   CHSD_SIM_COLD_US    0     a region's first touch since power-up (or since
//                             the card went back in) takes this much longer
//   CHSD_SIM_COLD_KB    1024  the size of such a region
//   CHSD_SIM_FAIL_EVERY 0     every Nth stream() fails part way: the blocks
//                             before a pseudo-random one arrive, then the
//                             token never comes (a stream timeout)
//   CHSD_SIM_INIT_US    0     init()
// A cold region warms up from its first touch whether or not anyone waits:
// a command that gives up (the stream timeout) leaves the card working on
// it, and a later one waits only for what is left. (Whether a real card
// does that after CMD12 is what ECBench's B8 measures.)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "sim.h"
#include "VCard.h"
#include "SdSpi.h"

static FILE *s_card;
static bool s_out, s_image;         // pulled out; a whole image, not a file to wrap
static bool s_lost;                 // out and back in since the last init(): no power, back in SD mode
static vcard::Card s_vc;

namespace {

struct Knobs { uint32_t cmd, blk, rd, jitter, cold, coldBlocks, failEvery, init; };
Knobs s_k;
bool s_kRead;
uint32_t s_timeout = 1000000;       // setStreamTimeout(); 1 s as on the board
uint32_t s_rng = 0x2545F491u;       // fixed: runs repeat
uint32_t s_streams;                 // stream() calls, for CHSD_SIM_FAIL_EVERY
std::vector<uint32_t> s_ready;      // per cold region: when it is warm (0 = never touched)

uint32_t knob(const char *name, uint32_t def) {
    const char *v = getenv(name);
    return v && *v ? (uint32_t)strtoul(v, nullptr, 10) : def;
}

const Knobs &knobs() {
    if (!s_kRead) {
        s_kRead = true;
        s_k.cmd = knob("CHSD_SIM_CMD_US", 600);
        s_k.blk = knob("CHSD_SIM_BLK_US", 170);
        s_k.rd = knob("CHSD_SIM_READ_US", 900);
        s_k.jitter = knob("CHSD_SIM_JITTER_US", 0);
        s_k.cold = knob("CHSD_SIM_COLD_US", 0);
        uint32_t kb = knob("CHSD_SIM_COLD_KB", 1024);
        s_k.coldBlocks = kb ? kb * 2 : 2048;
        s_k.failEvery = knob("CHSD_SIM_FAIL_EVERY", 0);
        s_k.init = knob("CHSD_SIM_INIT_US", 0);
        if (s_k.cmd != 600 || s_k.blk != 170 || s_k.rd != 900 || s_k.jitter || s_k.cold || s_k.failEvery || s_k.init)
            fprintf(stderr, "chsim: SD card cmd=%u blk=%u read=%u jitter=%u cold=%u/%uKB failEvery=%u init=%u (us)\n",
                    (unsigned)s_k.cmd, (unsigned)s_k.blk, (unsigned)s_k.rd, (unsigned)s_k.jitter, (unsigned)s_k.cold,
                    (unsigned)(s_k.coldBlocks / 2), (unsigned)s_k.failEvery, (unsigned)s_k.init);
    }
    return s_k;
}

uint32_t rnd() {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

// A command's extra latency (CHSD_SIM_JITTER_US); no draw when it is 0, so
// the default run is what it always was.
uint32_t jitter() { return s_k.jitter ? rnd() % (s_k.jitter + 1) : 0; }

// How long until lba's region is warm, starting its warm-up on a first touch.
uint32_t coldWait(uint32_t lba) {
    if (!s_k.cold) return 0;
    uint32_t r = lba / s_k.coldBlocks, now = sim_now();
    if (r >= s_ready.size()) s_ready.resize(r + 1, 0);
    if (!s_ready[r]) s_ready[r] = (now + s_k.cold) | 1;        // (| 1: never 0 once touched)
    int32_t left = (int32_t)(s_ready[r] - now);
    return left > 0 ? (uint32_t)left : 0;
}

// The bus rule: SPI1 is the panel's while a flush is in flight.
void busCheck(const char *fn) {
    if (sim_flushActive())
        sim_bug("sd::%s() while a panel flush holds SPI1 (the card shares the bus with the panel): "
                "call gfx_wait() first, and touch the card only before the next gfx_flushAsync()", fn);
}

}  // namespace

static FILE *card() {
    const char *path = getenv("CHSD_CARD");
    if (!s_card && path && (s_card = fopen(path, "rb"))) {
        size_t n = strlen(path);
        s_image = n > 4 && (!strcmp(path + n - 4, ".img") || !strcmp(path + n - 4, ".IMG"));
        fseek(s_card, 0, SEEK_END);
        s_vc.setup(path, (uint32_t)ftell(s_card));
    }
    return s_out ? nullptr : s_card;
}

uint32_t sim_cardBlocks() {
    FILE *f = card();
    if (!f) return 0;
    if (!s_image) return s_vc.blocks();
    fseek(f, 0, SEEK_END);
    return (uint32_t)(ftell(f) / 512);
}

// Pulled out, the card loses power: every region is cold again, and back in
// it answers nothing on SPI until init() (stream(), recover() and readReg()
// fail; read() is left as it always was here).
void sim_cardEject(bool out) {
    s_out = out;
    if (out) {
        s_ready.clear();
        s_lost = true;
    }
}

namespace sd {

bool init() {
    busCheck("init");
    const Knobs &k = knobs();
    if (k.init) sim_advance(k.init);
    if (!card()) return false;
    s_lost = false;
    return true;
}

// A block, taking `us` of virtual time.
static bool fetch(uint32_t lba, uint8_t *dst, uint32_t us) {
    FILE *f = card();
    if (!f || lba >= sim_cardBlocks()) return false;
    sim_advance(us);
    long k = s_image ? (long)lba : s_vc.read(lba, dst);
    if (k < 0) return true;
    memset(dst, 0, 512);            // (a file's last block, past its end)
    return !fseek(f, k * 512, SEEK_SET) && fread(dst, 1, 512, f) > 0;
}

bool read(uint32_t lba, uint8_t *dst) {
    busCheck("read");
    const Knobs &k = knobs();
    uint32_t us = k.rd + jitter();  // default 900: about what a polled block read takes on the board
    if (card() && lba < sim_cardBlocks()) {
        us += coldWait(lba);
        if (us > 1000000) {         // read()'s token wait gives up after 1 s
            sim_advance(1000000);
            return false;
        }
    }
    return fetch(lba, dst, us);
}

// One command's latency, then each block at the 24 MHz wire rate; the
// board's fn runs while the next block arrives, so only the wire is timed.
// As on the board, fn gets a block once the next block's token is in (or
// the stream has ended well), so a token later than the stream timeout (a
// cold region, a failure from CHSD_SIM_FAIL_EVERY) costs the timeout and
// fails the stream with every block before it delivered but the last.
bool stream(uint32_t lba, uint32_t n, uint8_t *buf0, uint8_t *buf1, BlockFn fn, void *ctx) {
    busCheck("stream");
    if (!n) return true;
    const Knobs &k = knobs();
    if (s_lost) {                   // back in without init(): no answer to CMD18
        sim_advance(k.cmd);
        return false;
    }
    uint32_t failAt = n;
    if (k.failEvery && ++s_streams % k.failEvery == 0) failAt = rnd() % n;
    uint32_t wait = k.cmd + jitter();
    const uint8_t *pending = nullptr;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *b = (i & 1) ? buf1 : buf0;
        if (card() && lba + i < sim_cardBlocks()) wait += coldWait(lba + i);
        if (i == failAt || wait > s_timeout) {
            sim_advance(s_timeout);
            return false;
        }
        if (wait) sim_advance(wait);
        wait = 0;
        if (!fetch(lba + i, b, k.blk)) return false;
        if (pending) fn(pending, ctx);
        pending = b;
    }
    fn(pending, ctx);
    return true;
}

void setStreamTimeout(uint32_t us) { s_timeout = us; }

// CMD12 + CMD13 at 12 MHz: a few tens of us on the board, card or not.
bool recover() {
    busCheck("recover");
    sim_advance(40);
    return card() && !s_lost;
}

bool isSdhc() { return sim_cardBlocks() >= 4194304u; }     // over 2 GB

// A made-up CID (maker 0, "CH", product "CHSIM", rev 1.0, a serial, 2026)
// and a CSD version 2.0 with the card's size.
bool readReg(uint8_t cmd, uint8_t *dst) {
    busCheck("readReg");
    if (!card() || s_lost || (cmd != 9 && cmd != 10)) return false;
    sim_advance(knobs().cmd);
    memset(dst, 0, 16);
    if (cmd == 10) {
        memcpy(dst + 1, "CHCHSIM", 7);
        dst[8] = 0x10;
        dst[9] = 0x00; dst[10] = 0xC0; dst[11] = 0xFF; dst[12] = 0xEE;
        dst[13] = 0x01;             // year 2000 + 0x1A = 2026, month 10
        dst[14] = 0xAA;
    } else {
        uint32_t c = sim_cardBlocks() / 1024;
        c = c ? c - 1 : 0;          // C_SIZE: (C_SIZE + 1) * 512 KB
        dst[0] = 0x40;              // CSD_STRUCTURE 1
        dst[7] = (uint8_t)(c >> 16 & 0x3F);
        dst[8] = (uint8_t)(c >> 8);
        dst[9] = (uint8_t)c;
    }
    return true;
}

}  // namespace sd
