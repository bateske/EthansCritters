// SdSpi - a read-only SPI-mode microSD block driver (CHSd, MIT: see NOTICE).
//
// HypeRunner's clean-room driver cut down to what reading a game's data
// file needs: identify the card, read one 512-byte block. No writing, no
// CRC (the games' files check their own blocks or records), and DMA only in
// stream(), for a sketch that reads whole files every frame.
//
// BUS RULE: SPI1 is shared with the LCD. Call these only after gfx_wait(),
// before the next flush. SPI1 is handed back exactly as CHGfx left it.
//
// After a read fails, call init() before reading again: a block whose token
// came late may still be on its way, and init()'s CMD0 is what clears it.
// (After a failed stream() there is a cheaper way: recover(), below.)
#pragma once
#include <stdint.h>

namespace sd {

bool init();                                // false: no card, or not one this can read
bool read(uint32_t lba, uint8_t *dst);      // one block; false: the card did not deliver

// Streaming, for a sketch that reads a file over and over (CHStlView reads
// a whole model every frame): n blocks from lba in one multi-block read
// (CMD18) at 24 MHz, each block landing by DMA in buf0 / buf1 in turn while
// fn(block, ctx) works on the one before. fn must keep off SPI1 and the two
// buffers' other half; it gets each block once, in order. Costs about 0.6 ms
// of card latency per call plus 0.17 ms per block, against ~1.5 ms per block
// for read(). No CRC is checked (a block garbled on the wire reaches fn as
// it came). DMA1 channels 2 and 3 are borrowed and left off: CHGfx sets
// channel 3 up afresh for every flush. False: the card stopped delivering
// (init() before the next read, as above). The games never call it, so it
// is not in their images.
typedef void (*BlockFn)(const uint8_t *block, void *ctx);
bool stream(uint32_t lba, uint32_t n, uint8_t *buf0, uint8_t *buf1, BlockFn fn, void *ctx);

// How long stream() waits for the card: for it to be ready before the
// command, and for each block's start token. Default 1 s, as read() (some
// cards take 300-770 ms over a block's first read after power-up); a
// sketch that streams every frame can set a few ms and treat a timeout as
// "not this frame". Applies to recover() and readReg() too.
void setStreamTimeout(uint32_t us);

// After a failed stream(), instead of init(): stream() always ends with
// CMD12, after which the card sends nothing more (a multi-block read stops
// at CMD12's end bit), so no stale block can be taken for an answer and the
// card needs no reset. recover() waits out the card's busy time, sends CMD12
// once more and asks the card's status (CMD13). True: stream() again. False:
// the card is still busy (within the stream timeout: try again next frame),
// has lost power (idle) or is gone; after a few falses, init(). Costs a few
// tens of us, against init()'s identification at 187.5 kHz (milliseconds,
// and up to a second for ACMD41).
bool recover();

// Card information, after init(): block addressing (SDHC/SDXC) or byte
// addressing (SDSC), and the 16-byte CSD (CMD9) or CID (CMD10) register:
// readReg(10, buf) gives the maker's id (byte 0), the product name (bytes
// 3-7), the serial (bytes 9-12) and the date. For a benchmark's report.
bool isSdhc();
bool readReg(uint8_t cmd, uint8_t *dst16);

}  // namespace sd
