// The squire's progress across power cycles, in the CHGame library's save
// pages (chgame/Save.h, magic "ECRT"): which nests are cleared, whether The
// Rot's gate is open and its toad beaten, the time played since the new
// game, the best time, where he has been (the pause map's fog), and which
// card data the boot has read through once (a fresh copy stalls on its
// first read: Play.h's warmed()).
// Loaded at boot, so play continues from the title (CONTINUE) with the
// cleared nests still cleared; stored when a nest falls, the gate opens,
// the toad dies, play pauses, a new game starts or the boot has read a
// new card through, because a page write
// stops the CPU for a few ms with interrupts off. The page is built in
// gfx_chunkScratch(), the card's landing buffer too: call store() between
// gfx_wait() and the flush, and never inside a card read.
#pragma once
#include <stdint.h>

namespace progress {

struct Record {
    uint8_t cleared;                // a bit per nest (WorldData.h's NESTS)
    uint8_t rot;                    // The Rot (src/game/Rot.h): rot::GATE_OPEN, rot::BEATEN
    uint16_t card;                  // the card data this board has read through once (Play.h's warmed()): its
                                    // build hash, low half | 1; 0 (the padding these bytes were before M8): none
    uint32_t seconds;               // played since the new game (CONTINUE keeps counting)
    uint32_t best;                  // the best time to clear the swamp (seconds played at the toad's death), 0: none yet
    uint8_t seen[24];               // the pause map's 16 x 12 cells he has walked in (64 world px each), a bit each
};

// Read and written in place (no copy of the record on the stack: 28 B less
// than save::load()/store()): load() is the newest saved record, in
// flash (nullptr: none); edit() a zeroed one in gfx_chunkScratch() to fill,
// then store() writes it.
const Record *load();
Record &edit();
bool store();

}  // namespace progress
