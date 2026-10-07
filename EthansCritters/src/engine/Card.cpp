// The card through CHSd. See Card.h.
//
// The run splitting is CHStlView's card::stream(); CHSd's own fat::stream()
// (patch P1) will replace it once that is committed.
#include "../Size.h"        // (first: Size.h)
#include <CHGfx.h>
#include <Fat.h>
#include "Card.h"
#include "CardFormat.h"

namespace card {

static fat::Run runs[MAX_RUNS];
static uint8_t nRuns;
static Status st = NO_CARD;            // until open()
uint8_t fails;
#if CHGAME_DEBUG
Use used;
uint16_t lost;
#endif

Status status() { return st; }

ECRT_OUTLINE static Status check(uint8_t *buf) {
    if (!sd::init()) return NO_CARD;
    int8_t rc = fat::mount(buf);
    if (rc) return rc == fat::E_READ ? NO_CARD : NOT_FAT;      // E_NOFS, E_EXFAT
    fat::File f;
    if ((rc = fat::find("CRITTERSDAT", f, buf))) return rc == fat::E_READ ? NO_CARD : NO_FILE;
    rc = fat::runs(f, runs, MAX_RUNS, buf);
    if (rc < 0) return rc == fat::E_READ ? NO_CARD : FRAGMENTED;    // E_FRAG, E_CHAIN
    if (!rc) return BAD_DATA;                                       // an empty file
    nRuns = (uint8_t)rc;
    uint32_t blocks = 0;
    for (uint32_t i = 0; i < nRuns; i++) blocks += runs[i].blocks;
    if (!fat::read(runs, nRuns, 0, buf)) return NO_CARD;
    return cardfmt::headerOk(buf, blocks) ? READY : BAD_DATA;
}

Status open() {
    gfx_wait();
    nRuns = fails = 0;
    sd::setStreamTimeout(BOOT_US);
    st = check(gfx_chunkScratch());
    if (st) nRuns = 0;
    return st;
}

void steady() { sd::setStreamTimeout(PLAY_US); }

bool stream(uint32_t k, uint32_t n, sd::BlockFn fn, void *ctx) {
    gfx_wait();
    uint8_t *b0 = gfx_chunkScratch(), *b1 = b0 + GFX_CHUNK_BYTES;
    for (const fat::Run *r = runs, *e = runs + nRuns; n && r < e; r++) {
        if (k >= r->blocks) {
            k -= r->blocks;
            continue;
        }
        uint32_t take = r->blocks - k;
        if (take > n) take = n;
#if CHGAME_DEBUG
        used.cmds++;
        used.blocks = (uint16_t)(used.blocks + take);
#endif
        if (!sd::stream(r->lba + k, take, b0, b1, fn, ctx)) break;
        n -= take;
        k = 0;
    }
    if (!n) return true;
    // The frame is lost. CMD12 has ended the stream: the card is waited out
    // and asked how it is (66 us on the board); still there, the next frame
    // tries again, until it has failed too often.
#if CHGAME_DEBUG
    lost++;
#endif
    if (!sd::recover() || ++fails >= GIVE_UP) {
        st = NO_CARD;                   // init() again before the next read (SdSpi.h)
        nRuns = 0;
    }
    return false;
}

}  // namespace card
