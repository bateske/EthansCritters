// Path F clips. See PathF.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "Card.h"
#include "CardFormat.h"
#include "PathF.h"

namespace pathf {

static uint32_t first;                  // the open clip's header block
static uint16_t nFrames, ms;

uint16_t frames() { return nFrames; }
uint16_t frameMs() { return ms; }

// The header block, copied out of the landing buffer (valid only during
// the call).
typedef uint32_t __attribute__((may_alias)) word;

static void keepHeader(const uint8_t *b, void *ctx) {
    for (uint32_t i = 0; i < 10; i++) ((word *)ctx)[i] = ((const word *)b)[i];
}

bool open(uint32_t blk) {
    alignas(4) uint8_t h[40];       // (4-aligned: CardFormat.h reads its fields as words)
    nFrames = 0;
    if (!card::stream(blk, 1, keepHeader, h) || !cardfmt::clipFrames(h)) return false;
    uint16_t rgb[16];
    cardfmt::clipPalette(h, rgb);
    pal::init(rgb);
    first = blk;
    nFrames = (uint16_t)cardfmt::clipFrames(h);
    ms = (uint16_t)cardfmt::clipMs(h);
    return true;
}

// A block is eight framebuffer rows as they go in gfx_fb: copy it on in
// words (newlib's memcpy is a byte loop in flash), four a turn. It runs
// while the next block's DMA does, so its ~40 us a block costs nothing.
static void toFb(const uint8_t *b, void *ctx) {
    word *&d = *(word **)ctx;
    const word *s = (const word *)b;
    for (word *e = d + 512 / 4; d < e; d += 4, s += 4) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
    }
}

bool frame(uint16_t k) {
    if (k >= nFrames) return false;
    word *d = (word *)gfx_fb;
    return card::stream(first + 1 + (uint32_t)k * cardfmt::FRAME_BLOCKS, cardfmt::FRAME_BLOCKS, toFb, &d);
}

// A tick is 50/3 ms.
bool play(uint32_t t, bool loop) {
    uint32_t k = ms ? t * 50 / (3u * ms) : 0;
    if (k >= nFrames) k = loop ? k % nFrames : nFrames - 1u;
    return frame((uint16_t)k);
}

}  // namespace pathf
