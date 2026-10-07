// The critters: a pool of up to MAX, each a small state machine at a fixed
// 60 Hz, position in Q4 (1/16 px) world units like the squire's, drawn
// from the frame cache. Only the ones near the camera act (ACTIVE_R px
// from its middle); further out they wait where they are, and past
// DESPAWN_R they are gone (their nest makes more when the squire comes back).
//
//   raccoon (HP 2)  wanders near its den; sees the squire within 64 px and
//       gallops at him; at arm's length (8-18 px to his side: never on top
//       of him) it rears up (the tell, attack f0 then f1, 300 ms, with a
//       chitter) and swipes (f2: a 14 x 10 box in front, a heart), then
//       recoils. A hit flashes it red and knocks it back (no hurt row). It
//       steals: a mushroom within 48 px while the squire is over 80 px away
//       is run to and eaten (gone; it heals 1). Death: the death row, then
//       a dithered fade.
//   turtle (HP 4)   sleeps by its clutch (a hit while asleep, or yawning:
//       double damage); wakes when hit or when the squire comes within 40 px,
//       yawning (the warning, 600 ms), then plods at him and bites (the
//       snap heard and f1 gaping for 200 ms, then f2-f3: a 12 x 8 box in
//       front, a heart); never strays over 96 px from its clutch. Two hits
//       within 1.5 s (or down to HP 2, once) and it hides in its shell for
//       1.5 s: the sword only goes "tink" until its head shows again.
//       Death: a red flash, then the shell fades out.
//   beaver (HP 3)   bobs by its lodge; the water rows in the shallows and
//       the deep (which it swims, the squire cannot), the land rows on land,
//       a splash each way. It sees the squire within 80 px; in the water,
//       with him within 96 px and water beside him, it dives (400 ms, a
//       splash; nothing can touch it under), swims at him under the surface
//       for 0.7-1.4 s (a ring on the water over it, a bubble),
//       comes up right beside him (the ascent, 400 ms: the tell) and bites
//       (a 12 x 8 box in front, a heart); then it does not dive for ~1.7 s
//       (on land, or in the meantime, it rears back for 250 ms and bites).
//       A carrier (sent with a stick when its lodge is damaged) makes for
//       the lodge and mends it (+1 HP, chips flying); threatened within
//       48 px (or hit) it stops, holds still 230 ms (the tell) and throws
//       the stick: it spins at him in a line (a half heart), a guard blocks
//       it (tink, it breaks), a parry sends it back at the critters (in a
//       lean debug build it breaks, config.h). A hit:
//       the damage row and the red flash. Death: flat, then the burst of
//       chips and water.
//   chameleon (HP 2) lurks by its brood; with the squire within 88 px it
//       fades out (Idle dithered 16 -> 2 over 0.5 s) and creeps up as a
//       shimmer (dithered 2-3, a puff of dust at its feet every 1/3 s, a
//       rustle), to 12-28 px to his side and level; there it shows itself
//       (0.2 s), opens its mouth with a pink glint in it (the tell, 280 ms)
//       and lashes its tongue (Thwip; f2-f4, a 21 x 6 box from 8 px in
//       front to the tongue's tip, a heart), then sits still 0.7 s (the
//       moment to hit it) and fades again. Lost (or after 4 s creeping) it
//       shows itself where it is; led over 176 px from its brood, it creeps
//       back to it unseen and shows itself there. A hit shows it whole and
//       stuns it (the damage row, 0.5 s). Death: the death row, then a
//       dithered fade.
//
// The toad's rot raccoons (Rot.h): raccoons drawn rotten (REMAP_ROT) that
// go for the squire wherever he is (they have no nest to keep to).
//
// A parried critter is stunned for a second (blinking white); a blocked
// one is pushed back. Combat.h puts the squire and the critters together.
// The beavers' sticks in flight (SHOTS at most) are attacks too: attack()
// and landed() take them as MAX + j.
#pragma once
#include <stdint.h>
#include <chgame/Config.h>
#include "Player.h"
#include "Scene.h"

namespace critters {

enum Kind : uint8_t { RACCOON, TURTLE, BEAVER, CHAMELEON };
static const uint8_t MAX = 12;
static const uint8_t SHOTS = 2;                    // sticks in the air at once
static const int ACTIVE_R = 200, DESPAWN_R = 320;  // px from the camera's middle, each axis
static const uint8_t NO_NEST = 0xFF;

void reset();                       // none left, nothing in the air
// A critter of kind k with its feet at world (x, y), from nest (NO_NEST:
// none; it then stays round where it is), a beaver carrying a stick when
// stick (a raccoon: one of the toad's rot raccoons): a puff, then it is
// there. False: the pool is full.
bool spawn(uint8_t k, int x, int y, uint8_t nest, bool stick = false);
uint32_t alive(uint8_t nest);       // living critters from that nest (NO_NEST: all)
bool carrying(uint8_t nest);        // one of its beavers has a stick

// The game's own randomness (logic only, so a run repeats): 0..n-1.
uint32_t rnd(uint32_t n);

// One logic step: camX, camY the view's middle in world px.
void tick(int camX, int camY);

// The drawing: plan() before gfx_wait() (spool::want), list() adds those on
// screen to the frame's items, draw() one of them (camX, camY the view's
// top-left; screen y = world y - camY + 8); drawShots() the sticks in the
// air and the rings over beavers under the water, over everything.
void plan(int camX, int camY);
void list(scene::List &l, int camX, int camY, bool faint = true);    // faint false: not the shimmers
void draw(uint8_t i, int camX, int camY);
void drawShots(int camX, int camY);
uint8_t shadowWidth(uint8_t i);     // half its shadow's width, 0: none

// --- combat (Combat.h) ---
// Where the sword finds critter i; false: nowhere (free, dying, appearing, under water).
bool hurtbox(uint8_t i, player::Box &b);
// Attack i (a critter, or MAX + j: stick j) while it hurts and has not
// landed yet: the box, the damage (half hearts) and where it comes from (world x).
bool attack(uint8_t i, player::Box &b, uint8_t &halves, int16_t &fromX);
// Attack i met the squire, with that outcome (parried: a critter is
// stunned, a stick flies back; blocked: pushed back, a stick breaks); it
// lands once an attack.
void landed(uint8_t i, uint8_t outcome);
enum Hurt : uint8_t { MISSED, HURT, KILLED, TINK };
// A blow of dmg from world x fromX, the squire's swing number swing (one
// hit a swing): what came of it; dmg becomes the damage done (a sleeping
// turtle takes double).
uint8_t hurt(uint8_t i, uint8_t &dmg, int16_t fromX, uint8_t swing);
// Stick j flying back at the critters (parried): its box, damage, a swing
// number of its own and where it comes from; shotBreak(j) when it struck.
bool shotBlade(uint8_t j, player::Box &b, uint8_t &dmg, uint8_t &swing, int16_t &fromX);
void shotBreak(uint8_t j);

#if CHGAME_DEBUG
// One line about each critter for the debug command A: kind (S: a beaver
// with a stick, X: a rot raccoon), state, HP, place.
char *report(char *p);
#endif

}  // namespace critters
