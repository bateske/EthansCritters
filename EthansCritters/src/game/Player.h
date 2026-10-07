// The squire: 8-way walking, the sword thrust, the guard and the parry, the
// riposte, getting hurt and dying. Logic at a fixed 60 Hz, position in Q4
// (1/16 px) world units; drawn from the frame cache (src/engine/Spool.h).
//
// Controls: D-pad walks (~72 px/s, diagonals as fast); A thrusts (the blade
// hurts on frames 4-5 over the swipe's arc, a crescent drawn in front of
// him from over his head to under his feet, 2..24 px out: a critter a
// little above or below him is hit too; A in a thrust's last 10 ticks,
// or the hurt row's, thrusts again as it ends); B held guards (frontal hits
// do nothing); B tapped just before a hit (10 ticks) parries it, but opens
// no new window within 24 ticks of the last (mashing B parries nothing); A
// soon after a parry (or pressed under the guard just before it) ripostes
// (2 damage).
// The sheet faces right; facing left is drawn mirrored and walking up or
// down keeps the last left/right facing.
//
// A frame of the screen: tick() (each logic step), plan() before
// gfx_wait() (what draw() will need from the card), draw() after fetch().
#pragma once
#include <stdint.h>

namespace player {

enum State : uint8_t { IDLE, WALK, THRUST, GUARD, RIPOSTE, HURT, DEAD };
enum Hit : uint8_t { HIT_IGNORED, HIT_TAKEN, HIT_BLOCKED, HIT_PARRIED };

struct Box { int16_t x0, y0, x1, y1; };     // world px, [x0, x1) x [y0, y1)

static const uint8_t MAX_HP = 10;           // half hearts: 5 hearts

void reset(int16_t x, int16_t y);           // world px of the feet; full health, facing right
// How the feet move over the ground: fn moves (x, y) (Q4 world px) by
// (dx, dy) as far as the ground lets it (the world: Terrain.h's box move,
// slowed in the shallows). Walking and knockback both go through it.
// nullptr: anywhere.
typedef void (*MoveFn)(int32_t &x, int32_t &y, int32_t dx, int32_t dy);
void setMover(MoveFn fn);
// One logic step; reads chgame's buttons. late: presses (A_BUTTON, B_BUTTON
// bits) made while the game was stopped (hit-stop), taken as made now.
void tick(uint8_t late = 0);
void plan();                                // spool::want() of the frames draw() needs
void draw(int camX, int camY);              // screen = world - camera

int16_t x();                                // world px
int16_t y();
int8_t facing();                            // +1 right, -1 left
void heading(int8_t &dx, int8_t &dy);       // the way he last walked, each -1..1 (the camera's lead)
State state();
uint8_t hp();
void heal(uint8_t halves);
void push(int16_t q4);                      // knocked along x, q4 a tick and dying away (a recoil off a shell)

Box body();                                 // where a critter's attack lands
// The blade while it hurts (thrust frames 4-5, the riposte's too): its box
// (the swipe's arc, CLIP_SWIPE_ARC: props.py), the damage (1; a riposte 2)
// and the swing's number, so a critter takes one hit a swing.
bool blade(Box &b, uint8_t &damage, uint8_t &swing);
// A critter's attack from world x fromX lands: parried (the attacker should
// be stunned) or blocked when it comes from the front at the right time,
// else taken (hurt, knocked back, briefly invulnerable), or ignored. any:
// no guard or parry stops it (the toad's landing shakes the ground).
Hit hit(int16_t fromX, uint8_t halves, bool any = false);

}  // namespace player
