// The library's Sizzle compiled with the switches in Fx.h, and the game's
// effects. See Fx.h.
#pragma GCC optimize("Os")   // cold code: size over speed
#include "../Size.h"        // (first: Size.h)
#include "Fx.h"

namespace fx {
// GOO shades a blob with these (its underside and its glint).
static const uint8_t (&DARKER)[16] = REMAP_DARK;
static const uint8_t (&LIGHTER)[16] = REMAP_LIGHT;
}  // namespace fx

#include <chgame/Sizzle.inl>

namespace fx {

void scroll(int dx, int dy) {
    if (!dx && !dy) return;
    for (auto &p : parts) {
        if (!p.life) continue;
        int x = p.x - dx * 16, y = p.y - dy * 16;
        if (x < -64 * 16 || x > 192 * 16 || y < -64 * 16 || y > 192 * 16) {   // gone for good (and Q4 fits)
            p.life = 0;
            continue;
        }
        p.x = (int16_t)x;
        p.y = (int16_t)y;
    }
    for (auto &f : floats) {
        f.x = (int16_t)(f.x - dx);
        f.y = (int16_t)(f.y - dy);
    }
}

// n particles fanned round a full turn (from a random start), speed lo..hi
// (Q4 px a tick), squashed vertically by vyShift.
static void fan(Kind k, int x, int y, uint8_t n, int lo, int hi, int vyShift, int life0, int life1, uint8_t c0,
                uint8_t c1) {
    int a0 = rnd() & 255;
    for (uint32_t i = 0; i < n; i++) {
        int a = a0 + (int)(i * 256 / n) + rndRange(0, 16), sp = rndRange(lo, hi);
        spawn(k, x, y, (icos(a) * sp) >> 8, ((isin(a) * sp) >> 8) >> vyShift, (uint8_t)rndRange(life0, life1),
              i & 1 ? c1 : c0);
    }
}

void hitSpark(int x, int y, uint8_t colour) {
    spawn(SPARK, x, y, 0, 0, 6, C_BONE);                        // the flash where it struck
    fan(SPARK, x, y, 5, 28, 52, 0, 8, 16, colour, C_OCHRE);
}

void dust(int x, int y, uint8_t n, uint8_t colour) {
    fan(DUST, x, y, n, 16, 34, 1, 16, 28, colour, REMAP_DARK[colour]);
}

void mud(int x, int y, uint8_t n, uint8_t colour) {
    for (uint32_t i = 0; i < n; i++)
        spawn(GOO, x + rndRange(-2, 3), y, rndRange(-20, 21), rndRange(-52, -26), (uint8_t)rndRange(14, 24),
              i & 3 ? colour : REMAP_DARK[colour]);
}

void chips(int x, int y, uint8_t n) {
    // four woods in a word's nibbles (a 4-byte table would be small data, copied into SRAM)
    const uint32_t WOOD = C_BARK | C_PEAT << 4 | C_UMBER << 8 | C_MUD << 12;
    for (uint32_t i = 0; i < n; i++)
        spawn(CONFETTI, x + rndRange(-3, 4), y, rndRange(-30, 31), rndRange(-44, -18), (uint8_t)rndRange(18, 30),
              (uint8_t)(WOOD >> ((rnd() & 3) * 4) & 15));
}

ECRT_OUTLINE void number(int x, int y, const char *text, uint8_t colour) { floatText(text, x, y, colour); }

#ifdef CHSIM
// ~10 us a particle (a few gfx_pixel or a small fill, called from flash), ~30 a float.
uint32_t simCost() {
    uint32_t us = 0;
    for (auto &p : parts) us += p.life ? 10 : 0;
    for (auto &f : floats) us += f.t ? 30 : 0;
    return us;
}
#endif

}  // namespace fx
