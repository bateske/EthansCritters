// The world: one painted 1024x768 bitmap on the card (section WRLD, made
// by tools/world/paint.py, laid out by tools/world/worldpack.py), streamed
// into the framebuffer every frame. No tiles at runtime.
//
// On the card the world is column blocks, 8 px wide and 128 world rows tall
// (a 512 B block, each row 4 bytes as gfx_fb holds 8 pixels), in bands that
// start every 8 rows. Any camera's 120 visible rows lie in one band, and a
// band's columns are consecutive blocks, so a frame's world is ONE card read
// of 16 blocks (17 when x is not a multiple of 8): about 4.4 ms on the board.
// Each block is copied into its framebuffer column, shifted by nibbles, as
// the next one arrives by DMA, so the copy costs nothing extra.
//
// The world is on the card WORLD_TPH times (M8), once for each ambient
// phase: the whole bitmap painted again with the reeds' tops leaning the
// other way, the ripples and the swell moved on, the lily pads dipped, the
// gnats buzzed round (tools/world/paint.py), so the swamp moves at no cost
// at all: the same window, from another phase's blocks. And WORLD_LAYERS
// times over: layer 1 is the healed swamp (The Rot alive, its toad beaten),
// the same as layer 0 everywhere else.
//
// Call draw() between gfx_wait() and the next flush (Card.h's bus rule).
#pragma once
#include <stdint.h>

namespace world {

// The view with its top-left at world (cx, cy), 0 <= cx <= WORLD_W - 128,
// 0 <= cy <= WORLD_H - 120 (Camera.h keeps it there), of world layer
// `layer` (< WORLD_LAYERS) in ambient phase `phase` (< WORLD_TPH), into
// framebuffer rows 8..127 (rows 0..7 are the HUD's). False: the card failed
// (card::status() says why); the rows may hold part of the view.
// idle(words), if given, after each block's copy while the next one comes
// (the CPU's idle time: Play draws sprites in it, M8): screen words
// 0..words-1 (8 px each) hold their final pixels; words 16 is the last
// block's (no DMA under that one).
bool draw(int cx, int cy, uint32_t layer, uint32_t phase, void (*idle)(uint32_t words) = nullptr);

// The region (WorldData.h's R_*) holding world point (x, y).
uint8_t regionAt(int x, int y);

}  // namespace world
