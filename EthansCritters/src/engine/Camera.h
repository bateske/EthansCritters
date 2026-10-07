// The camera: the world view's top-left, following a target.
//
// The target (the squire's feet, Q4 world px) may wander a dead zone
// (16 x 12 px round the view's focus) before the view moves; the focus
// leads a little the way the target faces, eased in, so more of what is
// ahead shows. The view is clamped to the world, [0, WORLD_W - 128] x
// [0, WORLD_H - 120], or to a smaller rectangle while bound() says so (the
// toad's arena), and moves in whole pixels (the world draws at any x).
// A shake is a free offset: the world is drawn from wherever the camera is.
#pragma once
#include <stdint.h>

namespace camera {

// The view's top-left in world px (shake included): world::draw(x, y).
extern int16_t x, y;

// Puts the view on the target at once (the start, a warp, a respawn).
void snap(int32_t tx, int32_t ty);

// A logic tick: follows the target (Q4), facing (faceX, faceY) in -1..1.
void update(int32_t tx, int32_t ty, int8_t faceX, int8_t faceY);

// Keeps the view inside world rectangle [x0, x1) x [y0, y1) (at least the
// view's size) from the next update() or snap(); bound(0, 0, WORLD_W, WORLD_H)
// again: the whole world.
void bound(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

// Shakes the view for `ticks` logic ticks, up to amp px, dying away.
void shake(uint8_t ticks, uint8_t amp);

// World point to screen (the playfield starts at row 8).
inline int screenX(int wx) { return wx - x; }
inline int screenY(int wy) { return wy - y + 8; }

}  // namespace camera
