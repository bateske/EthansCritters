// test_card.cpp's stand-in for CHGfx: the framebuffer and the chunk scratch
// Card.cpp and PathF.cpp use, nothing else (the test defines them).
#pragma once
#include <stdint.h>

#define GFX_W           128
#define GFX_FB_BYTES    8192
#define GFX_CHUNK_BYTES 512
extern uint8_t gfx_fb[GFX_FB_BYTES];
void gfx_wait();
uint8_t *gfx_chunkScratch();
