// The mushrooms: WorldData.h's 48 (red A2/A3 heal a heart, white B2/B3
// three) and a white one dropped where each nest falls, a "taken" bit each.
// Drawn from the bank (CLIP_MUSH_RED / _WHITE), bobbing a pixel and glinting
// now and then, sorted with the actors. The squire eats one he walks onto
// while he is hurt; a raccoon eats one it gets to first. All of the 48
// come back when the squire dies (a nest's drop is a one-off).
#pragma once
#include <stdint.h>
#include <chgame/Config.h>
#include "Scene.h"

namespace pickups {

static const uint8_t COUNT = 48 + 6;    // MUSHROOM_COUNT, then a drop for each of NEST_COUNT nests

void reset();                       // every mushroom there, no drops (a nest cleared before gave its own)
void restore();                     // the squire died: the 48 back (drops stay as they are)
void drop(uint8_t nest);            // nest fell: its white mushroom hops out
bool there(uint32_t i);
void spot(uint32_t i, int &x, int &y);       // world px of its foot
// The nearest one lying within r px of world (x, y), or -1.
int near(int x, int y, int r);
void take(uint32_t i);              // eaten by someone else (a raccoon)

void tick();                        // a logic step: the squire eats what he stands on
void plan(int camX, int camY);
void list(scene::List &l, int camX, int camY);
void draw(uint8_t i, int camX, int camY);

#if CHGAME_DEBUG
uint32_t left();                    // how many of the 48 are still there
#endif

}  // namespace pickups
