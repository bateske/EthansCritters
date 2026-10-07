// The play screen: the squire in the swamp. The world is streamed from the
// card every drawn frame (World.h), the camera follows him (Camera.h), his
// feet meet the terrain (Terrain.h: solid ground and deep water stop him,
// the shallows slow him) and the standing nests (Nests.h); the critters
// (Critter.h) come out of their nests at him, the blows land (Combat.h),
// the mushrooms heal (Pickups.h); everything is drawn from the frame cache
// (Spool.h) sorted by feet y (Scene.h), a willow redrawn over an actor
// behind it (Occluders.h), the particles and floating numbers over it all
// (Fx.h), the HUD strip on top.
//
// Each region has its mood (The Rot olive, Willow Hollow dim and green:
// gfx_setFade, eased in; the red screen of a hurt shares it), lifted once
// its nests are cleared (The Rot's once its toad is beaten). The HUD's
// compass points to the nearest nest still standing, then to The Rot's
// gate, once it is open to the toad's pond. Behind the gate: the arena and
// the giant toad (Rot.h); while they fight the camera stays in the arena
// and leans toward the toad, he stays in it too (its trees have pockets
// out of view and out of the toad's reach), the HUD shows its bar, and no
// willow is redrawn over anyone (the arena has none; the toad's frame
// takes the card time a pass would). The toad beaten: won(), the brambles part, and
// the win clip (Path F: THE SWAMP IS QUIET at dawn) with the time played
// since the new game and the best time under it; A: the title.
//
// He dies at no hearts: the death row, the colour drains, the death clip
// (THE SWAMP KEEPS YOU), and A stands him up at the hut with full hearts,
// the cleared nests still cleared, every mushroom back and every critter
// gone home. START pauses: the map of the swamp (a Path F still) with the
// fog over the cells he has not walked in, the nests he cleared crossed
// out, the gate's brambles while it is shut and himself, blinking; START
// plays on (held 3 s it is the library's exit). A nest cleared, the gate
// opened, the toad beaten, a pause or a new game is saved (Save.h) at the
// next drawn frame, before the card is read.
//
// A drawn frame, in order: plan() before gfx_wait() (the frames it will
// need, the occluder passes), then after it draw(): the save if one is
// due, spool::fetch() (its budget is what the last frame's work left of
// the 7.9 ms, the last frame's passes included: a command costs ~0.83 ms
// on the board, a block ~0.22), world::draw() with the shadows and the
// sprites drawn in its blocks' waits where they can be (M8d, Play.cpp),
// the rest of them (each actor that is behind a willow followed by its
// pass), the effects, the banners, the HUD (the caller then commits the
// palette and flushes). SELECT (debug builds, not a lean one) shows the frame stats over
// the playfield: card commands, blocks, microseconds after gfx_wait(), 60
// or 30 fps, and the cache's misses, deferred frames, entries and bytes.
#pragma once
#include <stdint.h>
#include "../../config.h"

namespace play {

void boot();                        // at power-up: the saved progress (Save.h)
// The card in the slot has had all its data read once on this board since
// it was written (the save's mark of its build hash): a card's first read
// of freshly written data stalled ~1.05 s on the board, so the boot reads
// a new card's file through once (with the long timeout) and then marks
// it, saved at once (after gfx_wait()).
bool warmed();
void markWarmed();
#if ECRT_EXTRAS                     // (not in a lean debug build: no choice on the title, no times)
void newGame();                     // the title's NEW GAME: no nests cleared, the gate shut, the clock and fog reset (the best time kept)
uint32_t best();                    // the best time (seconds), 0: none yet
bool saved();                       // there is progress to carry on with (a nest cleared): the title offers CONTINUE
char *mmss(char *p, uint32_t s);    // s seconds as m:ss at p (its end returned)
#endif
void enter(int16_t x, int16_t y);   // a new walk, the squire's feet at world (x, y); the card is open
bool tick();                        // one logic step; false: leave for the title (A after the win clip)
void plan();                        // before gfx_wait()
bool draw();                        // after gfx_wait(); false: the card failed (card::status())
void won();                         // (Rot.h) the toad is dead: the swamp is quiet, the best time kept

// Debug commands: Z prints the frame stats and the cache's totals since
// Z0 (or the walk's start); J <us> adds that much work after gfx_wait()
// to every drawn frame (the pacing under load; J0 ends it); C <kind> <x>
// <y> [nest] puts a critter (0 raccoon, 1 turtle, 2 beaver, 3 chameleon, 4
// a beaver with a stick) at world (x, y), from that nest (none: its home
// is where it stands); H <n>[b] hurts
// the squire by n half hearts from the front (b: from behind; not in a lean
// build); D <nest> [n] knocks n HP off a nest (none: all, it
// falls), D 6 [n] the toad down to n HP while it can be hit (none: it
// dies); A prints
// the actors (each critter's kind, state, HP and place), the nests' HP and
// what is cleared, the squire's HP and the mushrooms left, the toad's
// state, HP, place and the gate's state; I1 / I0: he takes no harm / does again; O0 / O1: no willow is
// redrawn over an actor behind it / they are again (Occluders.h). Z also
// prints the occluders' passes: made, shed for time, blocks, us.
bool hook(char cmd, const char *args);

}  // namespace play
