// The willows over the actors behind them. The trees are painted into the
// world, so an actor that walks behind one is drawn over its canopy: this
// puts the tree back over him. The card's OCCL section holds each tree an
// actor can get behind as the world shows it (its own pixels and those of
// the willows in front of it, which are in front of whoever is behind it
// too; 15 for the rest of its rectangle: tools/world/occlpack.py), in row
// bands, once for each world layer (World.h); WorldData.h has where they
// stand and their shapes.
//
// An actor is behind a tree when its feet are above the tree's base line
// (the trunk's foot). A pass is a tree's: right after the last of the
// actors behind it that overlap it is drawn (the draw list is in feet
// order, so whatever is drawn later is in front of the tree, and whatever
// was drawn between them is behind it), it streams the tree's rows over the
// union of their drawn boxes (one card command, the row blocks it needs)
// and blits those rows over them, straight from the card's landing buffer
// as each block arrives, clipped to that box (no clip rectangle: a variable
// clip costs every CHGfx draw call its checks, ~250 B of flash, ~70 B of
// RAM functions). Anything else drawn in that box before is behind the tree
// too (a mushroom, a puff). Actors in front of the tree are drawn after it
// as usual. Static items (mushrooms, nests) get no pass of their own: none
// stands behind a canopy (objects.json; test_world.py checks).
//
// A pass costs a card command and 1-3 blocks (board: ~1.1-1.6 ms), so at
// most MAX_PASSES a frame: the squire's best tree (the one covering most of
// him, and whatever critter is behind it with him), then a tree with a
// critter behind it, before his second (which adds ~1% of his canopy); the
// rest are shed (those actors are simply drawn over the canopy). A pass is
// shed too when the frame has no time left for it (at 60 fps: by BUDGET_US
// less what the HUD needs; at 30 by the pacer's calm line, so the passes
// alone never keep it at 30), except the squire's best tree, which is
// always redrawn (it may push the frame over, and the pacer to 30 fps); a
// tree of his redrawn the frame before is timed without the frame cache's
// fetch (made one frame and shed the next as the fetches came and went, it
// flickered). What the passes took counts in the frame's work, so the next
// frame's fetch budget leaves room for them (Play.cpp).
//
// plan() before gfx_wait() (the choice), after() between gfx_wait() and the
// flush (the card: Card.h's bus rule).
#pragma once
#include <stdint.h>
#include <chgame/Config.h>
#include "../game/Scene.h"

namespace occluders {

static const uint8_t MAX_PASSES = 2;

// The actors in l (only the squire and the critters on screen, feet in
// world px) in the view whose top-left is world (camX, camY): which get a
// pass this frame, over which trees, in the colours of world layer `layer`
// (the trees and the pixels they show are the same in every layer).
void plan(const scene::List &l, int camX, int camY, uint32_t layer);

// The frame's fetch (Play's frame cache) is done: a tree of the squire's
// redrawn last frame is timed from here (the fetch varies from frame to
// frame; the next frame's fetch budget makes room for the pass).
void fetched();

// A pass is planned over actor (what, i) (it may yet be shed, or find no
// overlap): Play draws it after the world's stream, and what covers it then
// (every actor a pass is over: its pass waits for the last of them).
bool planned(uint8_t what, uint8_t i);

// Item (what, i) has just been drawn, scene::drawn its screen box (called
// for every actor a pass is over, in the draw list's order): the trees
// planned over it whose last actor it is are redrawn over the part of their
// actors' boxes they cover. False: the card failed (card::status()).
bool after(uint8_t what, uint8_t i, int camX, int camY);

#if CHGAME_DEBUG
// Since clearTotals() (debug builds: Play's Z): passes made, passes shed
// for time, their card blocks, microseconds (all of a pass: the command,
// the blocks, the last block's blit) summed and the worst.
struct Totals { uint32_t passes, shed, blocks, sumUs, maxUs; };
const Totals &totals();
void clearTotals();
// No passes at all (Play's O0; O1 again): the scripts' before and after.
extern bool off;
#endif

}  // namespace occluders
