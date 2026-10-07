// Motion maths and the screen shake. Integer maths only: soft-float trig
// once cost a demo on this chip 8.5 KB of flash and half its frame rate.
//
// A game's own effects (its particle kinds, banners, floating texts) live in
// the same namespace in the game's code, built from these.
#pragma once
#include <stdint.h>

namespace fx {

// Easing: t in 0..n -> 0..256 (OUT_BACK and OUT_BOUNCE overshoot). Each
// curve is a 17-point table; ease() is inline, so a call with a constant
// curve links only that curve's table.
enum Ease : uint8_t { LINEAR, OUT_CUBIC, OUT_BACK, IN_OUT, OUT_BOUNCE };
extern const int16_t EASE_CUBIC[17], EASE_BACK[17], EASE_INOUT[17], EASE_BOUNCE[17];
int easeCurve(const int16_t *curve, int t, int n);     // curve nullptr: linear
inline int ease(Ease e, int t, int n) {
    return easeCurve(e == OUT_CUBIC ? EASE_CUBIC : e == OUT_BACK ? EASE_BACK : e == IN_OUT ? EASE_INOUT
                     : e == OUT_BOUNCE ? EASE_BOUNCE : nullptr, t, n);
}
inline int bounce(int t, int n) { return easeCurve(EASE_BOUNCE, t, n); }
int isin(int a);                    // a in 1/256 turns -> -256..256
inline int icos(int a) { return isin(a + 64); }

// Presentation-only randomness (never touches game outcomes): xorshift32,
// restarted by reseed() so scripted runs repeat.
uint32_t rnd();
int rndRange(int lo, int hi);       // lo .. hi-1
void reseed();

// Screen shake: shake() starts it; applyShake() moves rows y0..y1 of the
// finished framebuffer (call it after drawing, before the flush) and
// shakeTick() counts it down once per logic tick. fill < 0: rows the move
// uncovers keep their pixels shifted in place; else they are filled.
void shake(uint8_t frames, uint8_t amplitude);
bool shaking();
void applyShake(int y0, int y1, int fill = -1);
void shakeTick();
void shakeStop();

}  // namespace fx
