// The nests: WorldData.h's six (where, and what lives there), 8 HP each.
// The world is painted with every nest already wrecked, so a standing nest
// is drawn over it from the bank (CLIP_NEST_RACCOON + kind: intact,
// battered, wrecked by its HP) and a cleared one is simply not drawn. While
// it stands its base is solid (Terrain.h's blocker) and the squire's
// thrust breaks it down.
//
// While the squire is within 160 px of a standing nest it keeps some of its
// critters alive (KEEP: 3 raccoons or turtles, 2 beavers or chameleons), a
// new one every 3 s (a puff at the nest), while fewer than CROWD are alive
// in all (a crowd of more is noise, and at 9-12 on screen the board drops
// to 30 fps). A damaged lodge sends out a beaver with a stick as well (one
// at a time, from out on the pond) to mend it: repair(), +1 HP.
//
// A cleared nest: its pip on the HUD fills, a white mushroom hops out
// (Pickups.h), a banner says so for a moment, and the progress is saved
// (Play.h does that at the next drawn frame, after gfx_wait()). After the
// last of the six the banner says THE ROT STIRS (a rumble, a shake): The
// Rot's gate parts once it has had its say (allCleared(): Rot.h).
#pragma once
#include <stdint.h>
#include "Player.h"
#include "Scene.h"

namespace nests {

static const uint8_t HP = 8;
static const uint8_t CROWD = 8;     // no nest spawns while this many critters are alive

void reset(uint8_t cleared);        // all standing at full HP but those cleared (a bit each)
void heal();                        // the squire died: the standing ones back to full HP
uint8_t cleared();                  // a bit per cleared nest
bool allCleared();                  // all six: The Rot stirs (its gate parts: Rot.h)
uint8_t hp(uint8_t i);              // 0: cleared

// Terrain.h's blocker: a standing nest's base.
bool blocks(int x0, int y0, int x1, int y1);

void tick();                        // a logic step: spawning
void plan(int camX, int camY);
void list(scene::List &l, int camX, int camY);
void draw(uint8_t i, int camX, int camY);

// --- combat (Combat.h) ---
bool hurtbox(uint8_t i, player::Box &b);
enum Hurt : uint8_t { MISSED, HIT, CLEARED };
uint8_t hurt(uint8_t i, uint8_t dmg, uint8_t swing);    // one hit a swing
bool repair(uint8_t i);             // a beaver mends it: +1 HP up to HP (false: cleared, or whole)

// The banner after a nest falls (then THE ROT STIRS after the last): its
// text while it shows, else nullptr.
const char *banner();
bool saveDue();                     // a nest fell since the last call

}  // namespace nests
