// The ground under the world: what each 8 px cell is (WorldData.h's
// TERRAIN_RUNS, painted with the world by tools/world/paint.py), and moving a
// box across it. All in flash: logic never touches the card.
#pragma once
#include <stdint.h>
#include "../assets/WorldData.h"

namespace terrain {

// World pixel (x, y): T_OPEN, T_SHALLOW, T_DEEP or T_SOLID (outside the
// world: T_SOLID).
uint8_t at(int x, int y);

// Bits of the classes a mover may not enter, e.g. BLOCK_WALK.
inline constexpr uint8_t tbit(uint8_t t) { return (uint8_t)(1u << t); }
static const uint8_t BLOCK_WALK = tbit(T_SOLID) | tbit(T_DEEP);

// True if any cell under the box [x0, x1) x [y0, y1) (world px) is in block.
bool hits(int x0, int y0, int x1, int y1, uint8_t block);

// Solid things standing on the ground that move() also stops at (the nests
// while they stand): true if the box [x0, x1) x [y0, y1) (world px) meets
// one. nullptr: none (hits() never asks it).
typedef bool (*BlockFn)(int x0, int y0, int x1, int y1);
extern BlockFn blocker;

// Moves a feet box by (dx, dy) (all Q4: 1/16 px). The box is 2 hw wide and
// h tall with (x, y) the middle of its bottom edge. Each axis moves on its
// own, so a mover slides along a wall; a mover that would only clip a
// corner by a pixel or two is nudged round it. A blocked axis stops at the
// last free pixel. A box that starts inside the ground (put there by a warp
// or a spawn) moves freely until it is out; one inside a blocker only (the
// toad came down on it) moves as if there were no blockers, the ground
// still stopping it. The blocker stops it as the ground does. Returns the
// axes that were blocked (1 x, 2 y). nudge false: no help round corners (a
// critter steers round on its own, and the nudge would fight its steering).
uint8_t move(int32_t &x, int32_t &y, int32_t dx, int32_t dy, uint8_t hw, uint8_t h, uint8_t block,
             bool nudge = true);

}  // namespace terrain
