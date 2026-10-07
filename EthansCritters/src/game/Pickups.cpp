// The mushrooms. See Pickups.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Bank.h"
#include "../assets/Palette.h"
#include "../assets/WorldData.h"
#include "../engine/Camera.h"
#include "../engine/Spool.h"
#include "../fx/Fx.h"
#include "../../Sounds.h"
#include "Pickups.h"
#include "Player.h"

namespace pickups {

static_assert(COUNT == MUSHROOM_COUNT + NEST_COUNT, "pickups: the 48, then a drop a nest");

static uint8_t taken[(COUNT + 7) / 8];
static uint8_t hopT, hopI;          // the drop hopping out of a fallen nest: ticks left, which
static uint16_t clock;              // the bob and the glint

static const uint8_t HOP_T = 24;
static const uint8_t RED_HEAL = 2, WHITE_HEAL = 6;      // half hearts

static void setTaken(uint32_t i, bool on) {
    uint8_t b = (uint8_t)(1u << (i & 7));
    taken[i >> 3] = (uint8_t)(on ? taken[i >> 3] | b : taken[i >> 3] & ~b);
}

bool there(uint32_t i) { return i < COUNT && !((taken[i >> 3] >> (i & 7)) & 1); }

void reset() {
    for (auto &b : taken) b = 0;
    for (uint32_t n = 0; n < NEST_COUNT; n++) setTaken(MUSHROOM_COUNT + n, true);   // no drop until a nest falls
    hopT = 0;
    clock = 0;
}

void restore() {
    for (uint32_t i = 0; i < MUSHROOM_COUNT; i++) setTaken(i, false);
}

void drop(uint8_t nest) {
    setTaken(MUSHROOM_COUNT + nest, false);
    hopT = HOP_T;
    hopI = (uint8_t)(MUSHROOM_COUNT + nest);
}

void spot(uint32_t i, int &x, int &y) {
    if (i < MUSHROOM_COUNT) {
        x = mushX(MUSHROOMS[i]);
        y = mushY(MUSHROOMS[i]);
    } else {                        // just in front of the fallen nest
        x = NESTS[i - MUSHROOM_COUNT].x;
        y = NESTS[i - MUSHROOM_COUNT].y + 4;
    }
}

ECRT_OUTLINE static bool white(uint32_t i) { return i >= MUSHROOM_COUNT || mushKind(MUSHROOMS[i]) >= MUSH_B2; }

static int iabs(int v) { return v < 0 ? -v : v; }

int near(int x, int y, int r) {
    int best = -1, bd = r + 1;
    for (uint32_t i = 0; i < COUNT; i++) {
        if (!there(i)) continue;
        int mx, my;
        spot(i, mx, my);
        int dx = iabs(mx - x), dy = iabs(my - y), d = dx > dy ? dx + dy / 2 : dy + dx / 2;
        if (d < bd) {
            bd = d;
            best = (int)i;
        }
    }
    return best;
}

void take(uint32_t i) { setTaken(i, true); }

void tick() {
    clock++;
    if (hopT) hopT--;
    uint8_t hp = player::hp();
    if (hp >= player::MAX_HP || player::state() == player::DEAD) return;     // only when hurt
    int x = player::x(), y = player::y();
    for (uint32_t i = 0; i < COUNT; i++) {
        if (!there(i)) continue;
        int mx, my;
        spot(i, mx, my);
        if (iabs(mx - x) > 7 || iabs(my - y) > 5 || (i == hopI && hopT)) continue;
        take(i);
        bool w = white(i);
        player::heal(w ? WHITE_HEAL : RED_HEAL);
        sfx(Sfx::Pickup);
        int sx = camera::screenX(mx), sy = camera::screenY(my);
        fx::hitSpark(sx, sy - 5, w ? C_BONE : C_LICHEN);
        fx::number(sx, sy - 12, w ? "+3" : "+1", w ? C_BONE : C_LICHEN);
        return;
    }
}

// Frame 1 (the glint) for 8 ticks in 128, each at its own moment; a pixel
// up and down about twice a second.
static uint8_t glint(uint32_t i) { return ((clock + i * 53u) & 127) < 8; }
static int bob(uint32_t i) { return (int)((clock + i * 37u) >> 5 & 1); }
static uint8_t clipOf(uint32_t i) { return white(i) ? CLIP_MUSH_WHITE : CLIP_MUSH_RED; }

static bool shows(uint32_t i, int camX, int camY, int &x, int &y) {
    if (!there(i)) return false;
    spot(i, x, y);
    return x > camX - 8 && x < camX + GFX_W + 8 && y > camY && y < camY + 120 + 14;
}

void plan(int camX, int camY) {
    for (uint32_t i = 0; i < COUNT; i++) {
        int x, y;
        if (!shows(i, camX, camY, x, y)) continue;
        spool::want(clipOf(i), 0, spool::PRIO_PICKUP);      // (kept while it glints: draw() falls back on it)
        if (glint(i)) spool::want(clipOf(i), 1, spool::PRIO_PICKUP);
    }
}

void list(scene::List &l, int camX, int camY) {
    for (uint32_t i = 0; i < COUNT; i++) {
        int x, y;
        if (shows(i, camX, camY, x, y)) l.add(x, y, scene::PICKUP, (uint8_t)i);
    }
}

void draw(uint8_t i, int camX, int camY) {
    uint8_t c = clipOf(i);
    const uint8_t *p = spool::peek(c, glint(i));
    if (!p) p = spool::peek(c, 0);
    if (!p) return;
    int x, y;
    spot(i, x, y);
    int lift = bob(i);
    if (i == hopI && hopT) lift = (HOP_T - hopT) * hopT / 8;     // out of the wreck in an arc
    scene::blit(p, x - camX, y - camY + 8 - lift, 1);
}

#if CHGAME_DEBUG
uint32_t left() {
    uint32_t n = 0;
    for (uint32_t i = 0; i < MUSHROOM_COUNT; i++) n += there(i);
    return n;
}
#endif

}  // namespace pickups
