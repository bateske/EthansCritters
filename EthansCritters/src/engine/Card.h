// The card: CRITTERS.DAT in the SD card's root, read through CHSd as raw
// 512 B blocks along the file's FAT runs (up to MAX_RUNS pieces).
//
// BUS RULE: the card shares SPI1 with the panel. Everything here calls
// gfx_wait() first and borrows gfx_chunkScratch() (the flush's buffers, idle
// until the next flush) as its landing buffers, so call it only between
// gfx_wait() and the next gfx_flushAsync(), and draw nothing into the
// chunk scratch meanwhile.
#pragma once
#include <stdint.h>
#include <SdSpi.h>
#ifndef CHGAME_DEBUG                // (tools/tests/test_card.cpp: set on its command line)
#include <chgame/Config.h>
#endif

namespace card {

// What is wrong with the card (the boot screen says it).
enum Status : uint8_t {
    READY = 0,
    NO_CARD,        // no card, or it stopped answering         INSERT CARD
    NOT_FAT,        // no FAT16/FAT32 volume (exFAT, NTFS, raw)  CARD NOT FAT
    NO_FILE,        // no CRITTERS.DAT in the root               NO CRITTERS.DAT
    FRAGMENTED,     // over MAX_RUNS pieces, or a broken chain   COPY TO A FRESH CARD
    BAD_DATA,       // not this build's file (CardIndex.h)       WRONG CARD DATA
};
static const uint8_t MAX_RUNS = 8;

// Finds and checks the file: sd::init(), fat::mount(), fat::find(),
// fat::runs() and block 0 against the build (CardFormat.h). Slow (the
// card's identification, milliseconds to a second): boot and retry only.
Status open();
Status status();

// How long a stream waits on the card, for it to be ready and for each
// block's token (sd::setStreamTimeout): open() sets BOOT_US, CHSd's own 1 s
// (a card's first reads after power-up, or of data just written, can take
// most of a second), steady() PLAY_US for the screens that stream every
// frame. PLAY_US: the board's slowest whole 17-block stream was 5.5 ms and
// its slowest single wait ~1.4 ms in 34,000 blocks, so 32 ms trips only on
// a real stall; and a frame it loses (32 ms, the work before it, the 8.4
// ms flush before that) stays inside the three 60 Hz ticks the frame loop
// catches up at once, so the game's clock does not slip.
static const uint32_t BOOT_US = 1000000, PLAY_US = 32000;
void steady();

// Blocks [k, k + n) of the file, each handed to fn(block, ctx) once, in
// order, while the next one arrives by DMA (sd::stream): one command per
// run the range touches. False: the frame is lost (fn has seen the blocks
// before the failure; the range runs past the file, too). The card is then
// sd::recover()ed: with status() still READY the caller drops the frame
// (the frame loop skips its flush: the panel keeps the last one) and tries
// again next frame; after GIVE_UP failures with no whole frame between
// them (~1.3 s of 32 ms timeouts: longer than the ~1.05 s a freshly
// written card stalled on the board), or a card recover() cannot bring
// back, status() is NO_CARD and open() starts it again.
bool stream(uint32_t k, uint32_t n, sd::BlockFn fn, void *ctx);

// Failed streams since the last whole frame (the frame loop zeroes it
// after each frame it flushes; from HOLD on, the logic waits too: the
// screen shows nothing new, so nothing may happen on it unseen).
static const uint8_t HOLD = 2, GIVE_UP = 40;
extern uint8_t fails;

#if CHGAME_DEBUG
// The card's work since the caller last cleared it: sd::stream() commands
// (one per run a range touches) and blocks. The frame stats (Pace.h). And
// the frames lost since Play's Z0 (Z prints them).
struct Use { uint16_t cmds, blocks; };
extern Use used;
extern uint16_t lost;
#endif

}  // namespace card
