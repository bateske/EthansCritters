// The card file's formats, as the game checks them (tools/card/cardfile.py
// and tools/card/clip.py write them and say why they are so). Pure byte
// work, no card and no CHGfx, so the host test runs these very functions on
// the file tools/mkcard.py made (tools/tests/test_card.cpp).
//
// Block 0 of CRITTERS.DAT, little-endian:
//   0 "ECRTCARD"  8 u16 format  10 u16 sections  12 u32 build hash
//   16 u32 total blocks  20 {tag[4], u32 first, u32 blocks, u32 crc32} x 16
// A Path F clip's first block:
//   0 "CLIP"  4 u16 frames  6 u16 frame ms  8 u16 palette[16] (RGB444)
// then 16 blocks a frame, gfx_fb's bytes.
#pragma once
#include <stdint.h>
#include "../assets/CardIndex.h"

namespace cardfmt {

static const uint32_t FRAME_BLOCKS = 16;        // 8 KB: gfx_fb, 8 rows a block

// Every field is naturally aligned in its block (cardfile.py, clip.py) and
// the blocks are read into 4-aligned buffers, so a field is one load (the
// CPU and the file are both little-endian): 52 B less than byte loads.
typedef uint16_t __attribute__((may_alias)) half;
typedef uint32_t __attribute__((may_alias)) word;
inline uint32_t rd16(const uint8_t *p) { return *(const half *)p; }
inline uint32_t rd32(const uint8_t *p) { return *(const word *)p; }

// "ECRT" "CARD" and "CLIP" read as little-endian words.
static const uint32_t MAGIC_LO = 0x54524345u, MAGIC_HI = 0x44524143u, CLIP = 0x50494C43u;

// Block 0 is this build's (magic, format, build hash and length all as in
// CardIndex.h) and the file, fileBlocks long, holds all of it. The hash
// covers every section's bytes, so a match names this build's file; the
// game does not CRC the sections again (the tools do), so a block damaged
// on the card shows as damaged art, not as WRONG CARD DATA.
inline bool headerOk(const uint8_t *b, uint32_t fileBlocks) {
    return rd32(b) == MAGIC_LO && rd32(b + 4) == MAGIC_HI && rd16(b + 8) == CARD_FORMAT &&
           rd32(b + 12) == CARD_HASH && rd32(b + 16) == CARD_BLOCKS && CARD_BLOCKS <= fileBlocks;
}

// A clip header: its frame count (0: not a clip), ms and palette.
inline uint32_t clipFrames(const uint8_t *b) { return rd32(b) == CLIP ? rd16(b + 4) : 0; }
inline uint32_t clipMs(const uint8_t *b) { return rd16(b + 6); }
inline void clipPalette(const uint8_t *b, uint16_t *rgb444) {
    for (uint32_t i = 0; i < 16; i++) rgb444[i] = (uint16_t)rd16(b + 8 + 2 * i);
}

}  // namespace cardfmt
