// Sizzle: the particle pool, the pop-up banner and the floating "+$15" texts
// the games share, on top of fx:: (chgame/Fx.h: easing, integer sine,
// randomness and the screen shake).
//
// This is an implementation header, not part of <CHGame.h>. The library is
// compiled apart from a sketch and sees none of its defines, and the twenty
// games differ in what they need (how many particles, which kinds, which
// banner, floats or not) and in the size tuning of the file that holds the
// code. So a game configures it in its own src/fx/Fx.h and the bodies are
// compiled in the game's src/fx/Fx.cpp:
//
//     // src/fx/Fx.h
//     #pragma once
//     #include <CHGame.h>
//     #define SIZZLE_CONFIGURED 1
//     #define SIZZLE_POOL 64            // only what differs from the defaults below
//     #include <chgame/Sizzle.h>
//     namespace fx { void explode(int x, int y, uint8_t n); }     // the game's own extras, if any
//
//     // src/fx/Fx.cpp
//     #pragma GCC optimize("Os")        // line 1: it governs the bodies included below
//     #include "Fx.h"
//     #include <chgame/Sizzle.inl>
//
// Every call site stays fx::burst(), fx::banner() and so on. The switches:
//
//   SIZZLE_POOL                48      particles in the pool
//   SIZZLE_KIND_SPARK/STAR/DUST 1      the kinds that exist (CONFETTI always does)
//   SIZZLE_KIND_COIN           0       bouncing coins; also fountain(Kind, ...) instead of fountain(x, y, n)
//   SIZZLE_KIND_RAIN           0       falling drops, named SIZZLE_RAIN_KIND_NAME (RAIN_DROP; RAIN in the
//                                      card games, whose hue table is then not exported under that name)
//   SIZZLE_KIND_GOO            0       CHBingo's ink blobs
//   SIZZLE_KIND_ORDER          -       the enumerators in another order (CHBoardwalk: COIN last)
//   SIZZLE_BURST               1       burst() exists
//   SIZZLE_COLOUR_INLINE       0       (code shape) drawParticles() reads p.colour in place instead of a local
//   SIZZLE_COIN_LATE           0       (code shape) the COIN case comes after DUST and STAR, as in CHBoardwalk
//   SIZZLE_DUST                3       how DUST draws: 1 one pixel, 2 a puff of size 2,
//                                      3 a puff of the size drawParticles(dust) is given (default 2)
//   SIZZLE_FOUNTAIN_GOLD_COINS = COIN  fountain(COIN, ...) spawns gold coins (else confetti colours)
//   SIZZLE_COIN_FLOOR          122     the row coins bounce on; SIZZLE_COIN_FLOOR_RUNTIME 1 adds setFloor()
//   SIZZLE_HUES                {RED, GOLD, FELT_LT, CYAN, BLUE}      the casino rainbow
//   SIZZLE_HUES_EXPORT         1       exported as `extern const uint8_t SIZZLE_HUES_NAME[5]` (RAIN)
//   SIZZLE_CONFETTI_COLOURS    {RED, GOLD, FELT_LT, CYAN, BLUE, WHITE}
//   SIZZLE_NO_PARTICLES        0       1: spawn(), burst(), fountain() and drawParticles() do nothing
//                                      (a game's device debug build sets it to make room)
//   SIZZLE_FLOATS              1       floatText()/drawFloats(); SIZZLE_FLOAT_CHARS 8 (the text, with its 0),
//                                      SIZZLE_FLOAT_CLAMP 0 (keep it on screen), SIZZLE_FLOAT_BLINK_FIRST 0
//   SIZZLE_BANNER_DROP         0       0: letters of the 3x5 font pop in at 2x, 4x, 3x (maskText35);
//                                      1: the game's display font, letters dropping in one by one
//                                      (the game provides maskFont(), fontWidth() and FONT_H, or
//                                      SIZZLE_FONT_MASK/SIZZLE_FONT_WIDTH/SIZZLE_FONT_H)
//   SIZZLE_BANNER_CHARS        14      the banner's text, with its 0
//   SIZZLE_BANNER_ROWS_UP/DOWN 18/18   (drop: 30/12) rows around the centre activeRows() reports
//   SIZZLE_STYLES              RAINBOW|GOLD|RED|CYAN|WHITE   the BannerStyle enumerators (SIZZLE_BLACK, SIZZLE_GREEN too)
//   SIZZLE_STYLE_RAMPS         = STYLES the styles with a colour ramp of their own (the rest draw as the default)
//   SIZZLE_CYAN_INK            CYAN    (drop) the B_CYAN ink
//   SIZZLE_BANNER_FILL         0       the mask's fill (unused under a ramp; kept per game for identical code)
//   SIZZLE_HOLD_BANNER         1       holdBanner()
//   SIZZLE_BANNER_WRAP         1       the dance phase wraps to 128 (kept per game for identical code)
//   SIZZLE_SHAKE               1       the screen shake joins activeRows(), update() and clear()
//   SIZZLE_BANNER_AFTER(x, y, dy, gap) -  (drop) a pass after the banner is drawn (CHDominoes' half ink)
//
// The games' switches are listed in docs/chgame-library.md.
#pragma once
#include <stdint.h>
#include <CHGame.h>

#ifndef SIZZLE_CONFIGURED
#error "include the game's src/fx/Fx.h (its SIZZLE_* switches, then <chgame/Sizzle.h>), not this header alone"
#endif

// ---------------------------------------------------------------- defaults
#ifndef SIZZLE_POOL
#define SIZZLE_POOL 48
#endif
#ifndef SIZZLE_KIND_SPARK
#define SIZZLE_KIND_SPARK 1
#endif
#ifndef SIZZLE_KIND_STAR
#define SIZZLE_KIND_STAR 1
#endif
#ifndef SIZZLE_KIND_DUST
#define SIZZLE_KIND_DUST 1
#endif
#ifndef SIZZLE_KIND_COIN
#define SIZZLE_KIND_COIN 0
#endif
#ifndef SIZZLE_KIND_RAIN
#define SIZZLE_KIND_RAIN 0
#endif
#ifndef SIZZLE_RAIN_KIND_NAME
#define SIZZLE_RAIN_KIND_NAME RAIN_DROP
#endif
#ifndef SIZZLE_KIND_GOO
#define SIZZLE_KIND_GOO 0
#endif
#ifndef SIZZLE_BURST
#define SIZZLE_BURST 1
#endif
#ifndef SIZZLE_COLOUR_INLINE
#define SIZZLE_COLOUR_INLINE 0
#endif
#ifndef SIZZLE_COIN_LATE
#define SIZZLE_COIN_LATE 0
#endif
#ifndef SIZZLE_DUST
#define SIZZLE_DUST 3
#endif
#ifndef SIZZLE_FOUNTAIN_GOLD_COINS
#define SIZZLE_FOUNTAIN_GOLD_COINS SIZZLE_KIND_COIN
#endif
#ifndef SIZZLE_COIN_FLOOR
#define SIZZLE_COIN_FLOOR 122
#endif
#ifndef SIZZLE_COIN_FLOOR_RUNTIME
#define SIZZLE_COIN_FLOOR_RUNTIME 0
#endif
#ifndef SIZZLE_HUES
#define SIZZLE_HUES {RED, GOLD, FELT_LT, CYAN, BLUE}
#endif
#ifndef SIZZLE_HUES_EXPORT
#define SIZZLE_HUES_EXPORT 1
#endif
#ifndef SIZZLE_HUES_NAME
#define SIZZLE_HUES_NAME RAIN
#endif
#ifndef SIZZLE_CONFETTI_COLOURS
#define SIZZLE_CONFETTI_COLOURS {RED, GOLD, FELT_LT, CYAN, BLUE, WHITE}
#endif
#ifndef SIZZLE_NO_PARTICLES
#define SIZZLE_NO_PARTICLES 0
#endif
#ifndef SIZZLE_FLOATS
#define SIZZLE_FLOATS 1
#endif
#ifndef SIZZLE_FLOAT_CHARS
#define SIZZLE_FLOAT_CHARS 8
#endif
#ifndef SIZZLE_FLOAT_CLAMP
#define SIZZLE_FLOAT_CLAMP 0
#endif
#ifndef SIZZLE_FLOAT_BLINK_FIRST
#define SIZZLE_FLOAT_BLINK_FIRST 0
#endif
#ifndef SIZZLE_BANNER_DROP
#define SIZZLE_BANNER_DROP 0
#endif
#ifndef SIZZLE_BANNER_CHARS
#define SIZZLE_BANNER_CHARS 14
#endif
#ifndef SIZZLE_BANNER_ROWS_UP
#define SIZZLE_BANNER_ROWS_UP (SIZZLE_BANNER_DROP ? 30 : 18)
#endif
#ifndef SIZZLE_BANNER_ROWS_DOWN
#define SIZZLE_BANNER_ROWS_DOWN (SIZZLE_BANNER_DROP ? 12 : 18)
#endif
#define SIZZLE_RAINBOW 1
#define SIZZLE_GOLD 2
#define SIZZLE_RED 4
#define SIZZLE_CYAN 8
#define SIZZLE_WHITE 16
#define SIZZLE_BLACK 32
#define SIZZLE_GREEN 64
#ifndef SIZZLE_STYLES
#define SIZZLE_STYLES (SIZZLE_RAINBOW | SIZZLE_GOLD | SIZZLE_RED | SIZZLE_CYAN | SIZZLE_WHITE)
#endif
#ifndef SIZZLE_STYLE_RAMPS
#define SIZZLE_STYLE_RAMPS SIZZLE_STYLES
#endif
#ifndef SIZZLE_CYAN_INK
#define SIZZLE_CYAN_INK CYAN
#endif
#ifndef SIZZLE_BANNER_FILL
#define SIZZLE_BANNER_FILL 0
#endif
#ifndef SIZZLE_HOLD_BANNER
#define SIZZLE_HOLD_BANNER 1
#endif
#ifndef SIZZLE_BANNER_WRAP
#define SIZZLE_BANNER_WRAP 1
#endif
#ifndef SIZZLE_SHAKE
#define SIZZLE_SHAKE 1
#endif
#ifndef SIZZLE_FONT_MASK
#define SIZZLE_FONT_MASK(m, x, y, s, dy, gap) maskFont(m, x, y, s, dy, gap)
#endif
#ifndef SIZZLE_FONT_WIDTH
#define SIZZLE_FONT_WIDTH(s, gap) fontWidth(s, gap)
#endif
#ifndef SIZZLE_FONT_H
#define SIZZLE_FONT_H FONT_H
#endif
#ifndef SIZZLE_BANNER_AFTER
#define SIZZLE_BANNER_AFTER(x, y, dy, gap)
#endif
// DUST puffs spread along the floor in burst(); the pixel kind does not.
#define SIZZLE_DUST_SPREAD (SIZZLE_KIND_DUST && SIZZLE_DUST >= 2)
#if !SIZZLE_KIND_DUST
#undef SIZZLE_DUST
#define SIZZLE_DUST 0
#endif

#define SIZZLE_CAT_(a, b) a##b
#define SIZZLE_CAT(a, b) SIZZLE_CAT_(a, b)
#define SIZZLE_WORD_RAIN 1
#define SIZZLE_WORD_RAIN_DROP 2
#define SIZZLE_WORD_HUES 3
#if SIZZLE_KIND_RAIN && SIZZLE_HUES_EXPORT && \
    SIZZLE_CAT(SIZZLE_WORD_, SIZZLE_RAIN_KIND_NAME) == SIZZLE_CAT(SIZZLE_WORD_, SIZZLE_HUES_NAME)
#error "SIZZLE_RAIN_KIND_NAME and SIZZLE_HUES_NAME are the same word: name the kind RAIN_DROP, or export the hues as HUES"
#endif

namespace fx {

// ---------------------------------------------------------------- particles
#ifdef SIZZLE_KIND_ORDER
enum Kind : uint8_t { SIZZLE_KIND_ORDER };
#else
enum Kind : uint8_t {
#if SIZZLE_KIND_SPARK
    SPARK,
#endif
    CONFETTI,
#if SIZZLE_KIND_COIN
    COIN,
#endif
#if SIZZLE_KIND_RAIN
    SIZZLE_RAIN_KIND_NAME,
#endif
#if SIZZLE_KIND_STAR
    STAR,
#endif
#if SIZZLE_KIND_DUST
    DUST,
#endif
#if SIZZLE_KIND_GOO
    GOO,
#endif
};
#endif

void spawn(Kind k, int x, int y, int vx16, int vy16, uint8_t life, uint8_t colour);
#if SIZZLE_BURST
void burst(Kind k, int x, int y, uint8_t n, int speed16, uint8_t colour);  // radial
#endif
#if SIZZLE_KIND_COIN
void fountain(Kind k, int x, int y, uint8_t n);                            // confetti or coins, up
#else
void fountain(int x, int y, uint8_t n);                                    // confetti up
#endif
#if SIZZLE_COIN_FLOOR_RUNTIME
void setFloor(int y);                                                      // where coins bounce
#endif
bool particles();                                                          // any still flying
inline bool particlesAlive() { return particles(); }
#if SIZZLE_HUES_EXPORT
extern const uint8_t SIZZLE_HUES_NAME[5];                                  // the casino rainbow
#endif

// ---------------------------------------------------------------- banner
// Big centred lettering with an outline; pops in, holds, blinks out.
enum BannerStyle : uint8_t {
#if SIZZLE_STYLES & SIZZLE_RAINBOW
    B_RAINBOW,
#endif
#if SIZZLE_STYLES & SIZZLE_GOLD
    B_GOLD,
#endif
#if SIZZLE_STYLES & SIZZLE_RED
    B_RED,
#endif
#if SIZZLE_STYLES & SIZZLE_CYAN
    B_CYAN,
#endif
#if SIZZLE_STYLES & SIZZLE_WHITE
    B_WHITE,
#endif
#if SIZZLE_STYLES & SIZZLE_BLACK
    B_BLACK,
#endif
#if SIZZLE_STYLES & SIZZLE_GREEN
    B_GREEN,
#endif
};
void banner(const char *text, BannerStyle s, int cy, uint8_t frames = 70);
#if SIZZLE_HOLD_BANNER
void holdBanner(bool on);            // keep the banner up (before it blinks out) until false
#endif
bool bannerActive();

// ---------------------------------------------------------------- floats
#if SIZZLE_FLOATS
void floatText(const char *text, int x, int y, uint8_t colour);
void drawFloats();
#endif

// Vertical extent of everything transient on screen (particles, floats,
// banner, shake). Returns false if nothing is moving.
bool activeRows(int &lo, int &hi);

void clear();
void update();                      // once per logic tick
#if SIZZLE_DUST == 3
void drawParticles(uint8_t dust = 2);   // dust: the size of a DUST puff in pixels
#else
void drawParticles();
#endif
void drawBanner();

}  // namespace fx
