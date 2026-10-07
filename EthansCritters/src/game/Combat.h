// Blows: the squire's thrust against the critters and the nests, the
// critters' attacks against the squire, and what each feels like.
//
//   the thrust (Player.h's blade, frames 4-5) hurts what it meets once a
//   swing: 1, a riposte 2. A critter or a nest hit: a spark, a thud (a
//   knock on wood for a nest, with chips), the number, and the game stops
//   for 3 ticks (hit-stop; audio and particles go on); a nest that falls
//   stops it for 6 with a big shake. A shelled turtle only rings ("tink")
//   and pushes him back.
//   a critter's attack (its active frames) on the squire: blocked when he
//   guards facing it (tink, it is pushed back), parried when he tapped B
//   just before (it is stunned a second, he may riposte; the game stops 4
//   ticks), else he is hurt: the hurt row and red flash (Player.h), the
//   game stops 3 ticks, the screen flushes red for 0.3 s (gfx_setFade),
//   the camera shakes, a second of blinking invulnerability. A beaver's
//   stick in flight is an attack too (a half heart); parried, it flies back
//   and strikes the critters (and nests) like the blade, once, and breaks.
//   The toad (Rot.h) takes blows like a critter (the killing one stops the
//   game for 18 ticks with a big shake) and attacks like one: its tongue
//   (guarded, parried: it is stunned) and its landing's dust ring (no guard
//   or parry stops that).
#pragma once
#include <stdint.h>
#include <chgame/Config.h>

namespace combat {

void reset();                       // no hit-stop, no red screen
void tick();                        // after the squire and the critters have moved
bool frozen();                      // hit-stop: the game's logic waits this tick (once a tick: it counts it)
void fxTick();                      // every tick, frozen or not: the red screen fades

#if CHGAME_DEBUG
extern bool god;                    // (debug) the squire takes no harm
void redScreen();                   // (debug: H) the red screen of a hurt
#endif

}  // namespace combat
