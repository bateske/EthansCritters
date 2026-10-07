// Blows. See Combat.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Palette.h"
#include "../assets/WorldData.h"
#include "../engine/Camera.h"
#include "../fx/Fx.h"
#include "../../Sounds.h"
#include "Combat.h"
#include "Critter.h"
#include "Nests.h"
#include "Player.h"
#include "Rot.h"

namespace combat {

static uint8_t stopT;               // hit-stop: logic ticks still frozen
static uint8_t tintT;               // the red screen: ticks left
#if CHGAME_DEBUG
bool god;
#endif

static const uint8_t STOP_HIT = 3, STOP_CLEAR = 6, STOP_KILL = 18;    // (STOP_KILL: the toad's death)
static const uint8_t TINT_T = 18, TINT = 96;    // gfx_setFade amount (of 255), dying away over 0.3 s
static const uint16_t RUST565 = 0xEA26;          // C_RUST (0xE43) as RGB565

void reset() {
    stopT = tintT = 0;
    gfx_setFade(0);
}

// Spends a frozen tick: stop(t) holds the logic for t whole ticks after the
// one that hit (counting down in fxTick() would lose one to that tick).
bool frozen() {
    if (!stopT) return false;
    stopT--;
    return true;
}

void fxTick() {
    if (tintT) {
        tintT--;
        gfx_setFade((uint8_t)(TINT * tintT / TINT_T), RUST565);
    }
}

#if CHGAME_DEBUG
void redScreen() {
    tintT = TINT_T;
    gfx_setFade(TINT, RUST565);
}
#endif

static void stop(uint8_t t) {
    if (t > stopT) stopT = t;
}

static bool meet(const player::Box &a, const player::Box &b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

// The middle of where two boxes meet, on screen.
static void where(const player::Box &a, const player::Box &b, int &sx, int &sy) {
    int x0 = a.x0 > b.x0 ? a.x0 : b.x0, x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    int y0 = a.y0 > b.y0 ? a.y0 : b.y0, y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    sx = camera::screenX((x0 + x1) / 2);
    sy = camera::screenY((y0 + y1) / 2);
}

// A blow landed at screen (sx, sy) for d: a spark, a thud, the number, a
// little shake, the hit-stop.
static void blow(int sx, int sy, uint8_t d) {
    char n[4] = {'-', (char)('0' + d), 0};
    fx::hitSpark(sx, sy, d > 1 ? C_OCHRE : C_BONE);
    sfx(Sfx::Thud, (int8_t)(fx::rnd() % 4));
    fx::number(sx, sy - 6, n, d > 1 ? C_OCHRE : C_BONE);
    camera::shake(4, 1);
    stop(STOP_HIT);
}

// The blades: the squire's (the thrust's hurting frames), and the sticks
// parried back at the critters (one hit each, then they break).
static void sword() {
    player::Box b, h;
    uint8_t dmg, swing;
    int16_t from;
    int sx, sy;
    for (uint32_t k = 0; k <= (ECRT_LEAN ? 0 : critters::SHOTS); k++) {     // (a lean build: no stick comes back)
        if (k ? !critters::shotBlade((uint8_t)(k - 1), b, dmg, swing, from) : !player::blade(b, dmg, swing)) continue;
        if (!k) from = player::x();
        bool struck = false;
        for (uint32_t i = 0; i < critters::MAX; i++) {
            if (!critters::hurtbox((uint8_t)i, h) || !meet(b, h)) continue;
            uint8_t d = dmg, r = critters::hurt((uint8_t)i, d, from, swing);
            if (r == critters::MISSED) continue;
            struck = true;
            where(b, h, sx, sy);
            if (r == critters::TINK) {  // steel on a shell
                fx::hitSpark(sx, sy, C_SLATE);
                sfx(Sfx::Tink);
                if (!k) player::push((int16_t)(-player::facing() * 24));
                stop(STOP_HIT - 1);
                continue;
            }
            blow(sx, sy, d);
        }
        uint8_t t;                  // the toad
        if (rot::hurtbox(h) && meet(b, h) && (t = rot::hurt(dmg, swing)) != critters::MISSED) {
            struck = true;
            where(b, h, sx, sy);
            blow(sx, sy, dmg);
            if (t == critters::KILLED) {
                camera::shake(40, 4);
                stop(STOP_KILL);
            }
        }
        for (uint32_t i = 0; i < NEST_COUNT; i++) {
            if (!nests::hurtbox((uint8_t)i, h) || !meet(b, h)) continue;
            uint8_t r = nests::hurt((uint8_t)i, dmg, swing);
            if (r == nests::MISSED) continue;
            struck = true;
            where(b, h, sx, sy);
            if (r == nests::HIT) {
                fx::chips(sx, sy, 6);
                fx::hitSpark(sx, sy, C_MUD);
                sfx(Sfx::NestHit);
                camera::shake(6, 2);
                stop(STOP_HIT);
            } else {                    // it falls
                int nx = camera::screenX(NESTS[i].x), ny = camera::screenY(NESTS[i].y);
                fx::chips(nx, ny - 10, 16);
                fx::dust(nx, ny, 8, C_MUD);
                sfx(Sfx::NestBreak);
                camera::shake(30, 4);
                stop(STOP_CLEAR);
            }
        }
        if (k && struck) critters::shotBreak((uint8_t)(k - 1));
    }
}

static void teeth() {
    player::Box a, body = player::body();
    uint8_t halves;
    int16_t from;
    // the critters' attacks, the sticks, the toad's (the last)
    for (uint32_t i = 0; i <= critters::MAX + critters::SHOTS; i++) {
        bool toad = i == critters::MAX + critters::SHOTS, any = false;
        if (!(toad ? rot::attack(a, halves, from, any) : critters::attack((uint8_t)i, a, halves, from)) ||
            !meet(a, body))
            continue;
        uint8_t r = player::HIT_IGNORED;
#if CHGAME_DEBUG
        if (!god)
#endif
            r = player::hit(from, halves, any);
        if (toad) rot::landed(r);
        else critters::landed((uint8_t)i, r);
        int sx, sy;
        where(a, body, sx, sy);
        if (r == player::HIT_TAKEN) {
            sfx(player::hp() ? Sfx::Hurt : Sfx::Dirge);
            fx::hitSpark(sx, sy, C_RUST);
            tintT = TINT_T;
            gfx_setFade(TINT, RUST565);
            camera::shake(12, 3);
            stop(STOP_HIT);             // the blow is felt (the red screen fades on through it)
        } else if (r == player::HIT_BLOCKED) {
            fx::hitSpark(sx, sy, C_FOG);
            sfx(Sfx::Tink);
        } else if (r == player::HIT_PARRIED) {
            fx::hitSpark(sx, sy, C_BONE);
            fx::hitSpark(sx, sy, C_OCHRE);
            sfx(Sfx::Tink, 5);
            camera::shake(4, 1);
            stop(STOP_HIT + 1);
        }
    }
}

void tick() {
    sword();
    teeth();
}

}  // namespace combat
