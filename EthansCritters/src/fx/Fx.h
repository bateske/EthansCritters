// The swamp's particles and floating numbers: the CHGame library's Sizzle
// (chgame/Sizzle.h lists every switch), configured here and compiled in
// Fx.cpp, with the game's own on top. Earthy: no coins, no confetti look,
// no banner (a cleared nest's banner is painted art from the card).
//
//   SPARK     a blow landing: a cross of light for 8 ticks, then a speck
//   DUST      kicked-up dirt: puffs spreading along the ground
//   GOO       mud and water droplets: a shaded blob trailing a streak,
//             falling hard (shaded with REMAP_DARK and REMAP_LIGHT)
//   CONFETTI  (Sizzle's own, always there) wood chips: 2 px bits tumbling
//
// Sizzle draws in screen space, so everything here takes screen px
// (camera::screenX(wx), camera::screenY(wy)) and the game calls scroll()
// with the camera's movement every tick to keep it on the world.
//
// The game's loop: fx::update() once per logic tick (after scroll()),
// fx::drawParticles() and fx::drawFloats() after the actors, before the HUD;
// fx::clear() on a new screen, a warp or a respawn. RAM: 24 particles x 10 B
// and 4 floats x 12 B.
#pragma once
#include <CHGame.h>
#include "../assets/Palette.h"

#define SIZZLE_CONFIGURED 1
#define SIZZLE_POOL 24
#define SIZZLE_KIND_STAR 0
#define SIZZLE_KIND_GOO 1
#define SIZZLE_HUES_EXPORT 0
#define SIZZLE_HUES {C_RUST, C_OCHRE, C_MOSS, C_FOG, C_STONE}
#define SIZZLE_CONFETTI_COLOURS {C_BARK, C_PEAT, C_UMBER, C_MUD, C_OCHRE, C_BONE}
#define SIZZLE_FLOAT_CHARS 6        // "-2", "+1": short
#define SIZZLE_FLOAT_CLAMP 1
#define SIZZLE_STYLES SIZZLE_WHITE  // (no banner is shown; the fewest styles)
#define SIZZLE_HOLD_BANNER 0
#define SIZZLE_SHAKE 0              // the camera shakes the world instead (Camera.h)
#include <chgame/Sizzle.h>

namespace fx {

// The camera moved by (dx, dy) px (new view minus old, shake included):
// every particle and float moves the other way, so it stays where it was in
// the world. Particles that end up far off screen are dropped.
void scroll(int dx, int dy);

// A blow lands: a cross of light and sparks flying out (colour: the
// sparks; ochre ones go with them).
void hitSpark(int x, int y, uint8_t colour = C_BONE);
// Dirt kicked up: n puffs spreading along the ground, colour and a shade
// darker (sand: C_MUD; dark earth: C_BARK).
void dust(int x, int y, uint8_t n, uint8_t colour = C_MUD);
// Droplets thrown up that fall back hard: mud (C_PEAT), water (C_FOG).
void mud(int x, int y, uint8_t n, uint8_t colour = C_PEAT);
// Wood chips and debris, tumbling up and out (a nest hit, a stick broken).
void chips(int x, int y, uint8_t n);
// A number (or a word of up to 5 letters) rising from (x, y): "-1".
void number(int x, int y, const char *text, uint8_t colour);

#ifdef CHSIM
uint32_t simCost();         // the board's time to draw what flies now, estimated (us)
#endif

}  // namespace fx
