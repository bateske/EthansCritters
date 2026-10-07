// What a play frame draws over the world: the squire, the critters, the
// nests, the mushrooms, The Rot's toad and gate (Rot.h), one Item each, kept in feet-y order (the ones
// further up the screen first, so the nearer ones cover them), each over a
// small ground shadow; and drawing a bank frame by its pivot.
//
// Play keeps one List (static since M8d, shared by plan() and the frame):
// in the world stream's first wait it asks each kind to add() what shows,
// records the items' blits (rec, below) and draws every shadow, then every
// item, in that order wherever the stream lets them go (Play.cpp).
#pragma once
#include <stdint.h>

namespace scene {

enum What : uint8_t { PLAYER, CRITTER, NEST, PICKUP, ROT };
struct Item { int16_t x, y; uint8_t what, i; };     // the feet, world px
static const uint8_t MAX_ITEMS = 28;

struct List {
    Item it[MAX_ITEMS];
    uint32_t n;
    void add(int x, int y, uint8_t what, uint8_t i);   // in feet-y order (ties: as added); full: dropped
};

// Whether a thing with its feet at world (x, y) may show in the view whose
// top-left is world (camX, camY) (margins for a sprite up to 40 x 32).
bool onScreen(int x, int y, int camX, int camY);

// Bank frame p (Spool's layout: header, then rows) with its pivot at
// screen (sx, sy), mirrored about the pivot when face < 0 (the sheets face
// right). flags, remap, level: gfx_blit4k's.
void blit(const uint8_t *p, int sx, int sy, int8_t face, uint8_t flags = 0, const uint8_t *remap = nullptr,
          uint8_t level = 16);

// The cached frame f of clip c, or while it is on its way the one shown
// last (lc, lf; 0xFF: none), which then becomes this one. nullptr: nothing yet.
const uint8_t *frameOr(uint8_t c, uint8_t f, uint8_t &lc, uint8_t &lf);

// A shadow on the ground round feet at screen (sx, sy): two darkened rows
// hw px each side of the feet and a narrower one under them.
void shadow(int sx, int sy, int hw);

// The screen box [x0, x1) x [y0, y1) of everything blit() drew since the
// last clearDrawn() (empty: x0 >= x1): where an item just drawn lies (the
// occluders redraw a tree over that).
struct Rect { int16_t x0, y0, x1, y1; };
extern Rect drawn;
void clearDrawn();
void grow(int x, int y, int w, int h);      // drawn takes in [x, x + w) x [y, y + h)

// Recorded draws (Play.cpp draws the sprites inside the world's stream, M8):
// while rec is set, blit() adds a Prim there instead of drawing, up to
// recEnd; past it recReal is set, as an item does that cannot be recorded
// (it streams, the toad; or draws more than blit()s, a chameleon's glint:
// it says where in drawn). A Prim: blit()'s gfx_blit4k() of bank frame p,
// its remap REMAP_ROT (rot) or else REMAP_RED (the only ones blit() is given).
struct Prim { const uint8_t *p; int16_t x, y; uint8_t flags, level, rot, pad; };
extern Prim *rec, *recEnd;
extern bool recReal;
// Draws [a, e), drawn their box.
void play(const Prim *a, const Prim *e);

#ifdef CHSIM
// The simulator charges the card's time but not the drawing's: an estimate
// of the board's, us, from its measured gfx_blit4k and gfx_remapRect
// (CLAUDE.md's table), added up by blit() and shadow() as a frame draws.
extern uint32_t simCost;
uint32_t blitCost(uint32_t wordRows, uint32_t flags);   // a gfx_blit4k of that many words x rows (config.h's ECRT_SIM_P5)
#endif

}  // namespace scene
