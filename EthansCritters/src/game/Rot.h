// The Rot: its bramble gate, the arena behind it and the giant toad there,
// OLD GULLET (M7).
//
// The gate: a wall of brambles across the gap into The Rot (WorldData.h's
// GATE; the world is painted open there), solid while it stands (Terrain.h's
// blocker), drawn from the bank (CLIP_GATE_PART: its left half, mirrored
// for the right). Once every nest is cleared and THE ROT STIRS has had its
// say, the brambles part as the squire comes near (a rasp, the ground
// shakes, 3 frames pulling back to the sides) and the gate stays open
// (saved: GATE_OPEN).
//
// The arena (WorldData.h's ARENA, The Rot's pond): his feet in ARENA_TRIGGER
// start the fight. The brambles close behind him (he cannot leave: dying
// sends him back to the hut, and the fight starts again from the top next
// time), the toad rises out of the pond's murk (dithered in, splashes),
// croaks (a roar, the ground shakes, its name on a board) and its bar
// fills on the HUD; the camera stays inside the arena and leans toward
// the toad, so both are in view, and he stays inside it too (Play.cpp's
// walk(): its trees have pockets out of the camera's and the toad's reach).
//
// The toad (24 HP, ~60 x 55 px, streamed straight from the card's TOAD
// section every drawn frame: Path D, src/assets/Boss.h). It sits puffing
// its throat (now and then blinking) for a second or so, facing him, then:
//   HOP     toward him, up to 64 px (a landing past the arena's margins
//           comes down at them): it crouches (the tell, and a shadow
//           grows where it will land), is airborne (it cannot be hit, it
//           hurts nobody) and lands: the ground shakes, a ring of dust, a
//           heart off him if he is within ~20 px of where it came down (no
//           guard stops that: step away from the shadow). Landing on him
//           he walks out of its bulk (Terrain.h), not on through walls.
//   TONGUE  when he stands level with it in front, within ~54 px: its mouth
//           gapes (the tell, 300 ms, a glint in its throat, a hiss), then
//           the tongue lashes out ~34 px at mouth height for a heart and a
//           half. A guard blocks it (the tongue snaps back); a parry stuns
//           the toad for 1.2 s (agape, eyes shut): the big opening.
// The sword: a red flash (the card holds every frame in red as well: a
// remapped blit this size would cost a millisecond), a thud and the number
// each blow; every third blow (not while stunned) staggers it (the damage
// row), and it answers at once (a sit after it let a sword at its side
// stagger it again before it moved, to its death). At half its HP it
// roars again and turns faster: shorter tells, hops twice in a row, and
// every ~12 s it croaks two rot raccoons out of the reeds (raccoons drawn
// rotten, the raccoon's own fight; two alive at most). Its death: the death
// row after a long hit-stop and a big shake (its rot raccoons fall, those
// still in their puff as they come out of it), a dithered fade, The Rot's
// mood lifts, the brambles part again and play::won() takes it from there
// (saved: BEATEN).
//
// Logic in tick() (60 Hz, after the critters, before Combat.h's blows);
// the drawing: plan() before gfx_wait() (the gate's frame), list() adds
// the toad and the gate to the frame's items (sorted with the squire by
// their feet), shadows() before and after the sprites, draw() between
// gfx_wait() and the flush (the toad reads the card: Card.h's bus rule).
#pragma once
#include <stdint.h>
#include <chgame/Config.h>
#include "Player.h"
#include "Scene.h"

namespace rot {

enum : uint8_t { GATE_OPEN = 1, BEATEN = 2 };       // the saved flags (Save.h)
static const uint8_t HP = 24;

void load(uint8_t flags);           // at power-up: the saved flags
uint8_t flags();
void reset();                       // a new walk or a respawn: no fight, the gate as saved, the toad in the murk
bool fighting();                    // the fight is on (the gate shut behind him, the camera in the arena)
bool beaten();                      // the toad is dead (The Rot's mood is lifted)
bool saveDue();                     // the gate opened or the toad died since the last call

void tick();
// Terrain.h's blocker: the gate while it stands, the toad's bulk on the ground.
bool blocks(int x0, int y0, int x1, int y1);

void plan(int camX, int camY);
void list(scene::List &l, int camX, int camY);
// The toad's shadow (over false: before the sprites), and where it will land
// (over true: after them, so it falls over the squire standing there).
void shadows(int camX, int camY, bool over);
// Item i (0 the toad, 1 the gate). False: the card failed (card::status()).
bool draw(uint8_t i, int camX, int camY);

// The camera's lean toward the toad while they fight (px from the squire;
// Play eases it), the boss bar (Hud.h's draw: 0 none), and the board
// with its name while it rises (else nullptr).
void lean(int &dx, int &dy);
uint8_t bar();
const char *banner();

// --- combat (Combat.h) ---
bool hurtbox(player::Box &b);       // where the sword finds it (not airborne, rising or dying)
// A blow of dmg, the squire's swing number swing (one hit a swing):
// critters::MISSED, HURT or KILLED.
uint8_t hurt(uint8_t dmg, uint8_t swing);
// Its attack while it hurts and has not landed: the box, half hearts, where
// it comes from (world x) and whether a guard can stop it (any: no).
bool attack(player::Box &b, uint8_t &halves, int16_t &fromX, bool &any);
void landed(uint8_t outcome);       // player::Hit: parried, it is stunned

#if CHGAME_DEBUG
char *report(char *p);              // (debug A) " | TOAD st hp x,y gate"
uint8_t hp();                       // (debug D 6)
#endif

}  // namespace rot
