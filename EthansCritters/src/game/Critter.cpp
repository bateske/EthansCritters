// The critters. See Critter.h.
//
// A critter is a state (State) and a tick counter t since it began; the
// clip and frame shown come from those (pose()), so nothing about the
// picture is stored but the frame last drawn (shown while a new one is on
// its way from the card). Looping animations run off a shared clock,
// offset per critter so a pack does not move in step. Distances are
// octagonal (max + min / 2): no square roots, within 12% of round.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Bank.h"
#include "../assets/Palette.h"
#include "../assets/WorldData.h"
#include "../engine/Camera.h"
#include "../engine/Spool.h"
#include "../engine/Terrain.h"
#include "../fx/Fx.h"
#include "../ui/Hud.h"
#include "../../Sounds.h"
#include "Critter.h"
#include "Nests.h"
#include "Pickups.h"

namespace critters {

enum State : uint8_t { FREE, SPAWN, IDLE, WANDER, CHASE, TELL, ATTACK, RECOIL, STUN, STEAL, EAT, HOME,
                       SLEEP, WAKE, HIDE, SHELL, UNHIDE, DEAD,
                       DIVE, UNDER, RISE, CARRY, REPAIR, THROW,     // the beaver
                       FADE, SNEAK, APPEAR,                         // the chameleon
                       HURT_ROW };                                  // both: the damage row
// flags: this attack has landed; the turtle has hidden at HP 2; it attacks
// from the squire's west; it is going round something, westward; what it
// makes for lies below it (last seen not level); the beaver carries a
// stick (a raccoon: it is one of the toad's, ROT); it was in the water last tick
enum : uint8_t { LANDED = 1, SHY = 2, WEST = 4, ROUND = 8, WEST_ROUND = 16, BELOW = 32, STICK = 64, WET = 128,
                 ROT = STICK };

struct Critter {                    // 22 B
    int16_t x, y;                   // Q4 world, the feet
    int8_t vx, vy;                  // Q4 a tick: walking
    int8_t kx;                      // Q4 a tick: knocked back along x, dying away (every blow is level)
    uint8_t kind, st, t, hp;
    int8_t face;                    // +1 right, -1 left
    uint8_t flash;                  // red for this many ticks
    uint8_t nest;                   // home (NO_NEST: none)
    uint8_t aux;                    // raccoon: wander ticks left, or the mushroom it is after; turtle: the 2-hit
                                    // window; beaver: ticks until it may dive again
    uint8_t swing;                  // the squire's swing that hit it last
    uint8_t flags;
    uint8_t lc, lf;                 // clip and frame drawn last
    uint8_t roundD;                 // going round: how far this way (half px a unit, up to ROUND_D)
    int8_t lane;                    // chasing: the row it comes at him along, px above or below his feet
};
static Critter pool[MAX];
static uint32_t seed;
static uint16_t clock;              // logic ticks (the loops' shared clock)

// A stick in the air: Q4 world (its ground point; it flies 8 px over it),
// Q4 a tick, ticks flown (0: none), MINE when parried back at the critters.
struct Shot { int16_t x, y; int8_t vx, vy; uint8_t t, flags; };
static Shot shots[SHOTS];
enum : uint8_t { MINE = 1 };

static const uint8_t R_HP = 2, T_HP = 4;
static const uint8_t SPAWN_T = 20;              // the puff: 4 frames of 80 ms
static const uint8_t FLASH_T = 4, STUN_T = 60;
static const int8_t KNOCK = 40, KNOCK_DRAG = 5; // Q4 a tick, as the squire's
static const int STAGGER = 15;                  // knocked back faster than this: it does nothing else
static const uint8_t ROUND_D = 160;             // going round: 80 px a way (the first, 40), then back
static const int REACH_X = 18, REACH_Y = 6;     // px: close enough to attack ...
static const int CLOSE_X = 8;                   // ... but not on top of him (the tell and the guard's side would not read)
static const int LANE_X = 32;                   // px: further off than this it keeps to its lane (approach())
static const int LANE_OFF = 8;                  // px off its lane: first straight up or down onto it
// raccoon
static const int SEE = 64, LOSE = 112, R_LEASH = 128, STEAL_R = 48, STEAL_SAFE = 80;
static const int8_t GALLOP = 26, TROT = 10;     // Q4 a tick: 1.6 and 0.6 px
static const uint8_t TELL_T = 18, SWIPE_T = 8, RECOIL_T = 30, EAT_T = 40, R_DIE_T = 24, R_FADE_T = 24;
// turtle
static const int WAKE_R = 40, T_LEASH = 96, T_LOSE = 120;
static const int8_t CRAWL = 8;                  // 0.5 px a tick
static const uint8_t YAWN_T = 36, T_RECOIL_T = 36, HIDE_T = 40, SHELL_T = 90, WINDOW_T = 90;
// the bite: f0, the gaping f1 held (the tell), f2-f3 hurt, f4
static const uint8_t BITE_GAPE = 6, BITE_SNAP = 18, BITE_T = BITE_SNAP + 18;
static const uint8_t T_FLASH_T = 10, T_FADE_T = 30;
// beaver
static const int B_SEE = 80, B_LOSE = 128, B_LEASH = 160, DIVE_R = 96, THREAT = 48;
static const int8_t SWIM = 20, PADDLE = 12, UNDER_V = 22;  // Q4 a tick: in the water, on land, under it
static const uint8_t DIVE_T = 24, UNDER_MIN = 40, UNDER_MAX = 84, RISE_T = 24, B_TELL_T = 15, B_BITE_T = 12,
                     B_HURT_T = 24, THROW_T = 14, REPAIR_T = 40, DIVE_CD = 100;
static const int8_t SHOT_V = 30;                // Q4 a tick: 1.9 px
static const uint8_t SHOT_LIFE = 56;
// chameleon
static const int C_SEE = 88, C_LOSE = 144, C_LEASH = 176;
static const int8_t CREEP = 9;                  // Q4 a tick: 0.56 px
static const uint8_t FADE_T = 30, APPEAR_T = 12, C_TELL_T = 17, LASH_T = 36, C_LASH_HURTS = 18, C_RECOIL_T = 40,
                     C_HURT_T = 30;
static const uint8_t B_HOLD_T = 20, B_FADE_T = 10, B_DIE_T = B_HOLD_T + B_FADE_T;   // flat, the burst, its fade
static const uint8_t C_HOLD_T = 30, C_FADE_T = 24, C_DIE_T = C_HOLD_T + C_FADE_T;
// by kind: HP; the death row held (the turtle: its red flash), then the fade
static const uint8_t HP_OF[4] = {R_HP, T_HP, 3, 2};
static const uint8_t DIE_HOLD[4] = {R_DIE_T, T_FLASH_T, B_HOLD_T, C_HOLD_T};
static const uint8_t DIE_FADE[4] = {R_FADE_T, T_FADE_T, B_FADE_T, C_FADE_T};

uint32_t rnd(uint32_t n) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return n ? seed % n : 0;
}

void reset() {
    for (auto &c : pool) c.st = FREE;
    for (auto &s : shots) s.t = 0;
    seed = 0x2F6E2B1u;
    clock = 0;
}

// In the water: the shallows or the deep under world px (x, y)'s cell.
static bool wetAt(int x, int y) { return (uint8_t)(terrain::at(x, y - 1) - T_SHALLOW) < 2; }

bool spawn(uint8_t k, int x, int y, uint8_t nest, bool stick) {
    for (auto &c : pool) {
        if (c.st) continue;
        c = Critter();
        c.x = (int16_t)(x << 4);
        c.y = (int16_t)(y << 4);
        c.kind = k;
        c.st = SPAWN;
        c.hp = HP_OF[k];
        c.face = rnd(2) ? 1 : -1;
        c.nest = nest;
        c.aux = k == RACCOON ? 0xFF : 0;
        c.swing = 0xFF;
        c.lc = 0xFF;
        bool wet = k == BEAVER && wetAt(x, y);
        c.flags = (uint8_t)((stick ? STICK : 0) | (wet ? WET : 0));
        if (scene::onScreen(x, y, camera::x, camera::y))
            fx::dust(camera::screenX(x), camera::screenY(y), 5, wet ? C_FOG : C_MUD);
        return true;
    }
    return false;
}

uint32_t alive(uint8_t nest) {
    uint32_t n = 0;
    for (auto &c : pool)
        if (c.st && c.st != DEAD && (nest == NO_NEST || c.nest == nest)) n++;
    return n;
}

bool carrying(uint8_t nest) {
    for (auto &c : pool)
        if (c.st && c.st != DEAD && c.nest == nest && (c.flags & STICK)) return true;
    return false;
}

static int iabs(int v) { return v < 0 ? -v : v; }
static int dist(int dx, int dy) {
    dx = iabs(dx);
    dy = iabs(dy);
    return dx > dy ? dx + dy / 2 : dy + dx / 2;
}
static int px(const Critter &c) { return c.x >> 4; }
static int py(const Critter &c) { return c.y >> 4; }

// The critter being ticked: on screen (its sounds and effects show), and
// the view's top-left (effects are in screen px).
static bool seenNow;
static int viewX, viewY;

static void enter(Critter &c, uint8_t st) {
    c.st = st;
    c.t = 0;
    c.vx = c.vy = 0;
    c.flags &= (uint8_t)~ROUND;
    if (st == ATTACK) {             // a new attack: it may land; heard as it starts (a raccoon's at its tell)
        c.flags &= (uint8_t)~LANDED;
        if (c.kind == BEAVER) c.aux = DIVE_CD;
        if (seenNow && c.kind != RACCOON) sfx(c.kind == CHAMELEON ? Sfx::Thwip : Sfx::Chomp);
    }
}

// The states that only wait: after t ticks in st, critter kind k goes on to next.
struct Timed { uint8_t ks, t, next; };          // ks: kind << 5 | state
#define TM(k, st, t, next) {(uint8_t)((k) << 5 | (st)), (t), (next)}
static const Timed TIMED[] = {
    TM(RACCOON, SPAWN, SPAWN_T, IDLE), TM(RACCOON, TELL, TELL_T, ATTACK), TM(RACCOON, RECOIL, RECOIL_T, CHASE),
    TM(RACCOON, STUN, STUN_T, CHASE), TM(RACCOON, DEAD, R_DIE_T + R_FADE_T, FREE),
    TM(TURTLE, SPAWN, SPAWN_T, SLEEP), TM(TURTLE, ATTACK, BITE_T, RECOIL), TM(TURTLE, RECOIL, T_RECOIL_T, CHASE),
    TM(TURTLE, HIDE, HIDE_T, SHELL), TM(TURTLE, SHELL, SHELL_T, UNHIDE), TM(TURTLE, UNHIDE, HIDE_T, CHASE),
    TM(TURTLE, STUN, STUN_T, CHASE), TM(TURTLE, DEAD, T_FLASH_T + T_FADE_T, FREE),
    TM(BEAVER, SPAWN, SPAWN_T, IDLE), TM(BEAVER, TELL, B_TELL_T, ATTACK), TM(BEAVER, ATTACK, B_BITE_T, RECOIL),
    TM(BEAVER, RECOIL, RECOIL_T, CHASE), TM(BEAVER, DIVE, DIVE_T, UNDER), TM(BEAVER, THROW, THROW_T + 10, RECOIL),
    TM(BEAVER, REPAIR, REPAIR_T, IDLE), TM(BEAVER, HURT_ROW, B_HURT_T, IDLE), TM(BEAVER, STUN, STUN_T, CHASE),
    TM(BEAVER, DEAD, B_DIE_T, FREE),
    TM(CHAMELEON, SPAWN, SPAWN_T, IDLE), TM(CHAMELEON, FADE, FADE_T, SNEAK), TM(CHAMELEON, TELL, C_TELL_T, ATTACK),
    TM(CHAMELEON, ATTACK, LASH_T, RECOIL), TM(CHAMELEON, RECOIL, C_RECOIL_T, FADE),
    TM(CHAMELEON, HURT_ROW, C_HURT_T, FADE), TM(CHAMELEON, STUN, STUN_T, FADE), TM(CHAMELEON, DEAD, C_DIE_T, FREE),
};
#undef TM

// Its wait is over: on to the next state (true), or still waiting / not a waiting state.
static bool waited(Critter &c) {
    uint8_t ks = (uint8_t)(c.kind << 5 | c.st);
    for (const Timed &e : TIMED)
        if (e.ks == ks) {
            if (c.t < e.t) return false;
            enter(c, e.next);
            return true;
        }
    return false;
}

// Home: its nest, or where it is when it has none.
static void home(const Critter &c, int &hx, int &hy) {
    if (c.nest == NO_NEST) {
        hx = px(c);
        hy = py(c);
    } else {
        hx = NESTS[c.nest].x;
        hy = NESTS[c.nest].y + 8;   // in front of it (the nest is solid)
    }
}

// A step over the ground (Terrain.h; the nests stop it too; no corner
// nudges: walk() goes round); raccoons wade slower, turtles are at home in
// the shallows, a beaver swims the deep too (under the water it keeps to
// it). Returns the blocked axes.
static uint8_t step(Critter &c, int dx, int dy) {
    int32_t x = c.x, y = c.y;
    if (c.kind == RACCOON && terrain::at(px(c), py(c) - 1) == T_SHALLOW) {
        dx = dx * 5 / 8;
        dy = dy * 5 / 8;
    }
    uint8_t block = c.kind != BEAVER ? terrain::BLOCK_WALK
                    : c.st == UNDER  ? terrain::tbit(T_SOLID) | terrain::tbit(T_OPEN)
                                     : terrain::tbit(T_SOLID);
    uint8_t b = terrain::move(x, y, dx, dy, 4, 3, block, false);
    c.x = (int16_t)x;
    c.y = (int16_t)y;
    return b;
}

// Velocity toward world px (tx, ty) at speed s (Q4 a tick), in one of 8
// directions (a diagonal once the lesser axis is over half the greater),
// slowing so it does not overshoot.
static void steer(Critter &c, int tx, int ty, int s) {
    int dx = (tx << 4) - c.x, dy = (ty << 4) - c.y, ax = iabs(dx), ay = iabs(dy);
    int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    if (sy) c.flags = (uint8_t)(sy > 0 ? c.flags | BELOW : c.flags & ~BELOW);
    if (ay * 2 < ax) sy = 0;
    if (ax * 2 < ay) sx = 0;
    int v = sx && sy ? s * 11 / 16 : s;
    c.vx = (int8_t)(sx * (v < ax ? v : ax));
    c.vy = (int8_t)(sy * (v < ay ? v : ay));
}

// Walks its velocity; turns to face the way it goes (a puff of dust when
// it wheels round at a gallop). Headed up or down into something solid (a
// nest, a rock) it goes round: along it the way it was edging (the other
// way if that is shut too, or after 40 px, then 80 px each way: a long bank
// is paced under, not run along) until up or down is open again. Headed
// sideways into something it edges down or up, the side what it makes for
// lies. Returns the blocked axes.
static uint8_t walk(Critter &c) {
    if (c.vx && !(c.flags & ROUND)) {
        int8_t f = c.vx > 0 ? 1 : -1;
        if (f != c.face && c.st == CHASE && c.kind == RACCOON)
            fx::dust(px(c) - viewX, py(c) - viewY + 8, 2, terrain::at(px(c), py(c) - 1) == T_SHALLOW ? C_FOG : C_MUD);
        c.face = f;
    }
    if (!c.vx && !c.vy) return 0;
    int sp = iabs(c.vx) > iabs(c.vy) ? iabs(c.vx) : iabs(c.vy);
    if (c.flags & ROUND) {
        uint8_t b = c.vy ? step(c, 0, c.vy) : 2;
        if (!(b & 2)) {
            c.flags &= (uint8_t)~ROUND;
            return 0;
        }
        c.roundD = (uint8_t)(c.roundD + ((sp + 7) >> 3));
        if (c.roundD >= ROUND_D || (step(c, c.flags & WEST_ROUND ? -sp : sp, 0) & 1)) {
            c.flags ^= WEST_ROUND;
            c.roundD = 0;
        }
        c.face = c.flags & WEST_ROUND ? -1 : 1;
        return 0;
    }
    uint8_t b = step(c, c.vx, c.vy);
    if ((b & 2) && iabs(c.vx) < iabs(c.vy)) {
        bool w = c.vx ? c.vx < 0 : c.face < 0;
        c.flags = (uint8_t)((c.flags | ROUND) & ~WEST_ROUND) | (w ? WEST_ROUND : 0);
        c.roundD = ROUND_D / 2;
    } else if ((b & 1) && !c.vy) {
        step(c, 0, c.flags & BELOW ? sp : -sp);
    }
    return b;
}

// Toward world px (tx, ty) at speed s: steer() and walk().
static uint8_t go(Critter &c, int tx, int ty, int s) {
    steer(c, tx, ty, s);
    return walk(c);
}

static void faceTo(Critter &c, int x) { c.face = x >= px(c) ? 1 : -1; }

static bool fighting(const Critter &c) { return c.st == CHASE || c.st == TELL || c.st == ATTACK || c.st == RECOIL; }

// The side of the squire it fights from, chosen as it starts: the side
// with fewer critters already there, else the side it is on (a pack
// spreads round him instead of piling up on one side).
static void pickSide(Critter &c, int sx) {
    int west = 0, east = 0;
    for (auto &o : pool)
        if (&o != &c && fighting(o)) (o.flags & WEST ? west : east)++;
    bool w = west == east ? px(c) < sx : west < east;
    c.flags = (uint8_t)(w ? c.flags | WEST : c.flags & ~WEST);
}

// Could its feet stand at world (x, y)? (the ground, the standing nests)
static bool shut(int x, int y) {
    return terrain::hits(x - 4, y - 3, x + 4, y, terrain::BLOCK_WALK) ||
           (terrain::blocker && terrain::blocker(x - 4, y - 3, x + 4, y));
}

// The point beside the squire it makes for: 14 px to its side of him and,
// while it is further off than LANE_X, along its lane, a row 20 or 32 px
// above or below his feet picked as the chase began: more than LANE_OFF
// off that row it first goes straight up or down onto it (out of its nest
// up or down), then runs along it, and near him comes in to his level (a
// row up or down by its place in the pool) and attacks across. So a den's
// critters come at him from above and below, not all level and straight at
// him (the author found that hard to get through). On top of him it backs
// off to the side it is on; a side it cannot stand on (he is by a nest, a
// bank, the water) it gives up for the other.
static void approach(Critter &c, int sx, int sy, int &tx, int &ty) {
    if (iabs(sx - px(c)) < CLOSE_X && iabs(sy - py(c)) <= REACH_Y)
        c.flags = (uint8_t)(px(c) < sx ? c.flags | WEST : c.flags & ~WEST);
    bool far = iabs(sx - px(c)) > LANE_X;
    ty = sy + (far ? c.lane : (int)((&c - pool) % 3) * 4 - 4);  // (rows apart enough that separate() leaves them be)
    tx = far && iabs(ty - py(c)) > LANE_OFF ? px(c) : sx + (c.flags & WEST ? -14 : 14);
    if (shut(tx, ty) && !shut(2 * sx - tx, ty)) {
        c.flags ^= WEST;
        tx = 2 * sx - tx;
    }
}

static bool inReach(const Critter &c, int sx, int sy) {
    int ax = iabs(sx - px(c));
    return ax >= CLOSE_X && ax <= REACH_X && iabs(sy - py(c)) <= REACH_Y;
}

static bool onScreen(const Critter &c, int camX, int camY) { return scene::onScreen(px(c), py(c), camX, camY); }

static void chase(Critter &c, bool loud) {
    pickSide(c, player::x());
    c.lane = (int8_t)((rnd(2) ? 1 : -1) * (20 + 12 * (int)rnd(2)));   // (approach())
    enter(c, CHASE);
    if (loud) sfx(c.kind == RACCOON ? Sfx::Chitter : Sfx::Hiss);
}

static void raccoon(Critter &c, int sx, int sy, bool squire) {
    int x = px(c), y = py(c), d = dist(sx - x, sy - y), hx, hy;
    bool seen = seenNow;
    home(c, hx, hy);
    switch (c.st) {
    case IDLE:                      // (the toad's rot raccoons go for him wherever he is)
        if (squire && (d < SEE || (c.flags & ROT))) {
            chase(c, seen);
        } else if (!(clock & 31) && (!squire || d > STEAL_SAFE) && (c.aux = (uint8_t)pickups::near(x, y, STEAL_R)) != 0xFF) {
            enter(c, STEAL);
        } else if (c.t >= 40 + (seed & 63)) {       // off somewhere near home
            int tx = hx + (int)rnd(81) - 40, ty = hy + (int)rnd(41) - 16;
            enter(c, WANDER);
            steer(c, tx, ty, TROT);
            c.aux = (uint8_t)(dist(tx - x, ty - y) * 16 / TROT);
            if (!c.aux) enter(c, IDLE);
        }
        break;
    case WANDER:
        if (squire && d < SEE) chase(c, seen);
        else if (walk(c) || c.t >= c.aux) enter(c, IDLE);
        break;
    case STEAL: {
        if (squire && d < SEE) { chase(c, seen); break; }
        if (!pickups::there(c.aux)) { enter(c, IDLE); break; }
        int mx, my;
        pickups::spot(c.aux, mx, my);
        if (dist(mx - x, my - y) <= 2) {
            pickups::take(c.aux);
            enter(c, EAT);
            if (seen) sfx(Sfx::Chomp);
            break;
        }
        if (go(c, mx, my, GALLOP * 5 / 8) == 3) enter(c, IDLE);     // stuck
        break;
    }
    case EAT:
        if (c.t >= EAT_T) {
            if (c.hp < R_HP) {
                c.hp++;
                if (seen) fx::number(x - viewX, y - viewY - 6, "+1", C_LICHEN);
            }
            enter(c, IDLE);
        }
        break;
    case HOME:
        // the squire is ignored until it is well inside its ground again
        if (squire && d < SEE && dist(x - hx, y - hy) < R_LEASH / 2) { chase(c, false); break; }
        if (dist(hx - x, hy - y) <= 12) { enter(c, IDLE); break; }
        if (go(c, hx, hy, GALLOP * 5 / 8) == 3) enter(c, IDLE);
        break;
    case CHASE: {
        if (!squire || (d > LOSE && !(c.flags & ROT)) || (c.nest != NO_NEST && dist(x - hx, y - hy) > R_LEASH)) {
            enter(c, HOME);
            break;
        }
        if (inReach(c, sx, sy)) {
            faceTo(c, sx);
            enter(c, TELL);
            if (seen) sfx(Sfx::Chitter);    // the tell, to the ear too
            break;
        }
        int tx, ty;
        approach(c, sx, sy, tx, ty);
        go(c, tx, ty, GALLOP);
        break;
    }
    case ATTACK:                    // the swipe, lunging in
        if (c.t <= 4) step(c, c.face * 20, 0);
        if (c.t >= SWIPE_T) {
            enter(c, RECOIL);
            if (seen) fx::dust(x - viewX, y - viewY + 8, 3, C_MUD);   // down on all fours
        }
        break;
    }
}

// A turtle lunges for a bite (the snap is heard as it starts: enter()).
static void bite(Critter &c, int sx) {
    faceTo(c, sx);
    enter(c, ATTACK);
}

// A turtle's next step toward its place beside the squire (its velocity
// set): false if that step would take it past its tether. IDLE and CHASE
// ask the same, so it cannot flip between them a tick at a time.
static bool tether(Critter &c, int sx, int sy, int hx, int hy) {
    int tx, ty;
    approach(c, sx, sy, tx, ty);
    steer(c, tx, ty, CRAWL);
    int nx = (c.x + c.vx) >> 4, ny = (c.y + c.vy) >> 4;
    return c.nest == NO_NEST || dist(nx - hx, ny - hy) <= T_LEASH;
}

static void turtle(Critter &c, int sx, int sy, bool squire) {
    int x = px(c), y = py(c), d = dist(sx - x, sy - y), hx, hy;
    home(c, hx, hy);
    switch (c.st) {
    case SLEEP:
        if (squire && d < WAKE_R) {
            faceTo(c, sx);
            enter(c, WAKE);
            if (seenNow) sfx(Sfx::Hiss);
        }
        break;
    case WAKE:                      // the yawn: the warning
        if (c.t >= YAWN_T) chase(c, false);
        break;
    case IDLE:                      // at the end of its tether, watching
        if (squire && inReach(c, sx, sy)) {
            bite(c, sx);
        } else if (squire && d <= T_LOSE && tether(c, sx, sy, hx, hy)) {
            enter(c, CHASE);
        } else if (c.t >= 90 && (!squire || d > T_LOSE)) {
            enter(c, HOME);
        } else if (squire) {
            faceTo(c, sx);
        }
        break;
    case CHASE: {
        if (!squire || d > T_LOSE) { enter(c, HOME); break; }
        if (inReach(c, sx, sy)) {
            bite(c, sx);
            break;
        }
        if (!tether(c, sx, sy, hx, hy)) { enter(c, IDLE); break; }
        walk(c);
        break;
    }
    case HOME:
        if (squire && d < WAKE_R) { chase(c, false); break; }
        if (dist(hx - x, hy - y) <= 10) { enter(c, SLEEP); break; }
        if (go(c, hx, hy, CRAWL) == 3) enter(c, SLEEP);
        break;
    }
}

// Water thrown up where a beaver goes in or comes out (and the sound, on screen).
static void splash(const Critter &c) {
    if (!seenNow) return;
    fx::mud(px(c) - viewX, py(c) - viewY + 8, 5, C_FOG);
    sfx(Sfx::Splash);
}

// The stick leaves a beaver's mouth for where the squire stands.
static void launch(Critter &c, int sx, int sy) {
    c.flags &= (uint8_t)~STICK;
    for (auto &s : shots) {
        if (s.t) continue;
        int x = px(c) + c.face * 8, y = py(c), dx = sx - x, dy = sy - y, d = dist(dx, dy);
        if (!d) d = 1;
        s.x = (int16_t)(x << 4);
        s.y = (int16_t)(y << 4);
        s.vx = (int8_t)(dx * SHOT_V / d);
        s.vy = (int8_t)(dy * SHOT_V / d);
        s.t = 1;
        s.flags = 0;
        sfx(Sfx::Swish);
        return;
    }
}

static void beaver(Critter &c, int sx, int sy, bool squire) {
    int x = px(c), y = py(c), d = dist(sx - x, sy - y), hx, hy;
    bool threat = squire && d < THREAT;
    int sp = c.flags & WET ? SWIM : PADDLE;
    home(c, hx, hy);
    switch (c.st) {
    case IDLE:                      // bobbing by the lodge
        if (c.flags & STICK) enter(c, CARRY);
        else if (squire && d < B_SEE) chase(c, false);
        break;
    case CARRY:                     // to the lodge with a stick, unless he comes near: then it throws it
    case REPAIR:                    // mending the lodge: +1 halfway
        if (threat && (c.st == CARRY || c.t < REPAIR_T / 2)) {
            faceTo(c, sx);
            enter(c, THROW);
            break;
        }
        if (c.st == REPAIR) {
            faceTo(c, hx);
            if (c.t == REPAIR_T / 2) {
                c.flags &= (uint8_t)~STICK;
                if (c.nest != NO_NEST && nests::repair(c.nest) && seenNow) {
                    int nx = hx - viewX, ny = hy - viewY;
                    fx::chips(nx, ny - 12, 6);
                    fx::number(nx, ny - 26, "+1", C_LICHEN);
                    sfx(Sfx::NestHit, 7);
                }
            }
            break;
        }
        // fall through: a carrier goes home
    case HOME:
        if (c.st == HOME && squire && d < B_SEE && dist(x - hx, y - hy) < B_LEASH / 2) {
            chase(c, false);
            break;
        }
        if (dist(hx - x, hy - y) <= 8 || go(c, hx, hy, sp) == 3) enter(c, c.st == CARRY ? REPAIR : IDLE);
        break;
    case THROW:                     // stopped, the stick held still (the tell), then thrown
        if (c.t == THROW_T) launch(c, sx, sy);
        break;
    case CHASE: {
        if (!squire || d > B_LOSE || (c.nest != NO_NEST && dist(x - hx, y - hy) > B_LEASH)) {
            enter(c, HOME);
            break;
        }
        int ux = sx + (x < sx ? -14 : 14);      // where it would come up: water by him?
        if (!c.aux && d > 28 && d < DIVE_R && wetAt(ux, sy) &&
            !terrain::hits(x - 4, y - 3, x + 4, y, terrain::tbit(T_OPEN) | terrain::tbit(T_SOLID))) {
            enter(c, DIVE);
            splash(c);
            break;
        }
        if (inReach(c, sx, sy)) {
            faceTo(c, sx);
            enter(c, TELL);
            break;
        }
        int tx, ty;
        approach(c, sx, sy, tx, ty);
        go(c, tx, ty, sp);
        break;
    }
    case UNDER: {                   // to his side under the water (sliding along the banks), rings on it as it goes
        int tx = sx + (x < sx ? -14 : 14), dx = tx - x, dy = sy - y, n = dist(dx, dy);
        int16_t ox = c.x, oy = c.y;
        if (n) {
            uint8_t b = step(c, dx * UNDER_V / n, dy * UNDER_V / n);
            if (b & 2) step(c, dx < 0 ? -UNDER_V : UNDER_V, 0);
            else if (b & 1) step(c, 0, dy < 0 ? -UNDER_V : UNDER_V);
        }
        if (c.t >= UNDER_MAX || (c.t >= UNDER_MIN && ((c.x == ox && c.y == oy) || n <= 4))) {
            enter(c, RISE);
            c.aux = DIVE_CD;
            faceTo(c, sx);
            splash(c);
        }
        return;                     // (under the water: no splash in or out)
    }
    case RISE:                      // the ascent beside him: the tell; then the bite
        if (c.t >= RISE_T) {
            if (squire && inReach(c, sx, sy)) enter(c, ATTACK);
            else chase(c, false);
        }
        break;
    case DEAD:
        if (c.t == 8 && seenNow) fx::chips(x - viewX, y - viewY + 2, 10);    // the burst
        break;
    }
    bool wet = wetAt(px(c), py(c));     // in or out of the water: a splash
    if (wet != !!(c.flags & WET) && c.st != DIVE && c.st != RISE) splash(c);
    c.flags = (uint8_t)(wet ? c.flags | WET : c.flags & ~WET);
}

// 12-28 px to the squire's side and level: the tongue reaches him.
ECRT_OUTLINE static bool tongueReach(const Critter &c, int sx, int sy) {
    int ax = iabs(sx - px(c));
    return ax >= 12 && ax <= 28 && iabs(sy - py(c)) <= 4;
}

static void chameleon(Critter &c, int sx, int sy, bool squire) {
    int x = px(c), y = py(c), d = dist(sx - x, sy - y), hx, hy;
    home(c, hx, hy);
    switch (c.st) {
    case IDLE:
        if (squire && d < C_SEE) enter(c, FADE);
        break;
    case SNEAK:                     // a shimmer creeping to his side (dust, a rustle); there (or lost): it shows itself
        if (!squire || d > C_LOSE || tongueReach(c, sx, sy) || c.t == 255) {
            faceTo(c, sx);
            enter(c, APPEAR);
            break;
        }
        if (dist(x - hx, y - hy) > C_LEASH) {   // led too far from its brood: back to it unseen (else, shown and
            enter(c, HOME);                     // faded again on the spot, it would flicker there for ever)
            break;
        }
        go(c, sx + (x < sx ? -20 : 20), sy, CREEP);
        if (seenNow && !(c.t % 20)) fx::dust(x - viewX, y - viewY + 8, 1, C_BOG);
        if (seenNow && c.t % 48 == 24) sfx(Sfx::Step, 9);
        break;
    case HOME:                      // still a shimmer, the squire ignored; at the brood it shows itself
        if (dist(hx - x, hy - y) <= 12 || go(c, hx, hy, CREEP) == 3) enter(c, APPEAR);
        break;
    case APPEAR:                    // shown: the tell and the lash if he is still there
        if (c.t >= APPEAR_T) enter(c, squire && tongueReach(c, sx, sy) ? TELL : IDLE);
        break;
    }
}

// Two critters on the same spot shuffle apart (a pack chasing one squire
// would otherwise end up as one sprite); one planted for its blow (the
// tell, the attack) or in its shell is not shoved (into the squire).
static bool planted(const Critter &c) { return c.st == TELL || c.st == ATTACK || c.st == SHELL; }

static bool apart(const Critter &c) { return !c.st || c.st == DEAD || c.st == UNDER; }

static void separate() {
    for (uint32_t i = 0; i < MAX; i++) {
        Critter &a = pool[i];
        if (apart(a)) continue;
        for (uint32_t j = i + 1; j < MAX; j++) {
            Critter &b = pool[j];
            if (apart(b)) continue;
            int dx = b.x - a.x, dy = b.y - a.y;
            if (iabs(dx) >= 10 << 4 || iabs(dy) >= 4 << 4) continue;
            int s = dx > 0 ? 6 : dx < 0 ? -6 : (i & 1 ? 6 : -6);
            if (!planted(a)) step(a, -s, 0);
            if (!planted(b)) step(b, s, 0);
        }
    }
}

void tick(int camX, int camY) {
    clock++;
    int sx = player::x(), sy = player::y();
    bool squire = player::state() != player::DEAD;
    int vx = camX - GFX_W / 2, vy = camY - 60;      // the view's top-left (effects are in screen px)
    viewX = vx;
    viewY = vy;
    for (auto &c : pool) {
        if (!c.st) continue;
        int far = iabs(px(c) - camX), fy = iabs(py(c) - camY);
        if (fy > far) far = fy;
        if (far > DESPAWN_R) { c.st = FREE; continue; }
        if (far > ACTIVE_R) continue;               // asleep where it is
        if (c.t < 255) c.t++;
        if (c.flash) c.flash--;
        if ((c.kind == TURTLE || c.kind == BEAVER) && c.aux) c.aux--;
        if (c.kx) {                 // knocked back: staggered while it is strong, whatever it is doing
            step(c, c.kx, 0);
            c.kx = (int8_t)(c.kx > KNOCK_DRAG ? c.kx - KNOCK_DRAG : c.kx < -KNOCK_DRAG ? c.kx + KNOCK_DRAG : 0);
            if (iabs(c.kx) > STAGGER && c.st != DEAD) continue;
        }
        seenNow = onScreen(c, vx, vy);
        if (waited(c)) continue;
        switch (c.kind) {
        case RACCOON: raccoon(c, sx, sy, squire); break;
        case TURTLE: turtle(c, sx, sy, squire); break;
        case BEAVER: beaver(c, sx, sy, squire); break;
        default: chameleon(c, sx, sy, squire); break;
        }
    }
    separate();
    for (uint32_t j = 0; j < SHOTS; j++) {      // the sticks fly on; into the ground, or spent, they break
        Shot &s = shots[j];
        if (!s.t) continue;
        s.x = (int16_t)(s.x + s.vx);
        s.y = (int16_t)(s.y + s.vy);
        if (++s.t > SHOT_LIFE || terrain::at(s.x >> 4, s.y >> 4) == T_SOLID) shotBreak((uint8_t)j);
    }
}

void shotBreak(uint8_t j) {
    Shot &s = shots[j];
    s.t = 0;
    fx::chips(camera::screenX(s.x >> 4), camera::screenY(s.y >> 4) - 8, 6);
}

// --- the picture ---------------------------------------------------------------------

static uint8_t upTo(uint32_t v, uint32_t last) { return (uint8_t)(v > last ? last : v); }

// What each state shows, by kind: a clip (+ WATER: the beaver's water row,
// its land row's clip + 6, while it is afloat) and how its frame goes:
// LOOP every n ticks on the shared clock (offset per critter); HOLD frame
// b; PLAY from frame b, one every n ticks of the state, up to frame e; BACK
// the same from e down. ANY: the kind's default (its idle).
enum : uint8_t { LOOP = 0, HOLD = 0x40, PLAY = 0x80, BACK = 0xC0, WATER = 0x80, ANY = 31 };
struct Pose { uint8_t ks, clip, how, be; };     // how: mode | n; be: b << 4 | e
#define PS(k, st, clip, how, b, e) \
    {(uint8_t)((k) << 5 | (st)), (uint8_t)(clip), (uint8_t)(how), (uint8_t)((b) << 4 | (e))}
static const Pose POSES[] = {
    PS(RACCOON, WANDER, CLIP_RACCOON_RUN, LOOP | 6, 0, 0), PS(RACCOON, STEAL, CLIP_RACCOON_RUN, LOOP | 6, 0, 0),
    PS(RACCOON, HOME, CLIP_RACCOON_RUN, LOOP | 6, 0, 0), PS(RACCOON, CHASE, CLIP_RACCOON_RUN, LOOP | 4, 0, 0),
    PS(RACCOON, TELL, CLIP_RACCOON_ATTACK, PLAY | 3, 0, 1),   // (f0 is the idle pose: the rear-up is f1)
    PS(RACCOON, ATTACK, CLIP_RACCOON_ATTACK, HOLD, 2, 0), PS(RACCOON, RECOIL, CLIP_RACCOON_ATTACK, HOLD, 3, 0),
    PS(RACCOON, EAT, CLIP_RACCOON_IDLE, LOOP | 3, 0, 0),      // munching: the idle row twice as fast
    PS(RACCOON, STUN, CLIP_RACCOON_IDLE, HOLD, 0, 0), PS(RACCOON, DEAD, CLIP_RACCOON_DEATH, PLAY | 6, 0, 3),
    PS(RACCOON, ANY, CLIP_RACCOON_IDLE, LOOP | 6, 0, 0),
    PS(TURTLE, SPAWN, CLIP_TURTLE_SLEEP, LOOP | 8, 0, 0), PS(TURTLE, SLEEP, CLIP_TURTLE_SLEEP, LOOP | 8, 0, 0),
    PS(TURTLE, WAKE, CLIP_TURTLE_IDLE_YAWN, PLAY | 6, 3, 8),  // the yawn: idle-yawn f3-f8
    PS(TURTLE, CHASE, CLIP_TURTLE_WALK, LOOP | 8, 0, 0), PS(TURTLE, HOME, CLIP_TURTLE_WALK, LOOP | 8, 0, 0),
    PS(TURTLE, ATTACK, CLIP_TURTLE_BITE, HOLD, 0, 0),         // (pose() times the bite)
    PS(TURTLE, HIDE, CLIP_TURTLE_SHELL, PLAY | 4, 0, 9), PS(TURTLE, SHELL, CLIP_TURTLE_SHELL, HOLD, 9, 0),
    PS(TURTLE, DEAD, CLIP_TURTLE_SHELL, HOLD, 9, 0), PS(TURTLE, UNHIDE, CLIP_TURTLE_SHELL, BACK | 4, 0, 9),
    PS(TURTLE, STUN, CLIP_TURTLE_IDLE_BLINK, HOLD, 0, 0), PS(TURTLE, ANY, CLIP_TURTLE_IDLE_BLINK, LOOP | 6, 0, 0),
    PS(BEAVER, HOME, CLIP_BEAVER_MOVEMENT | WATER, LOOP | 6, 0, 0),
    PS(BEAVER, CHASE, CLIP_BEAVER_MOVEMENT | WATER, LOOP | 6, 0, 0),
    PS(BEAVER, CARRY, CLIP_BEAVER_MOVEMENT_WITH_STICK | WATER, LOOP | 6, 0, 0),
    PS(BEAVER, REPAIR, CLIP_BEAVER_MOVEMENT_WITH_STICK | WATER, LOOP | 12, 0, 0),
    PS(BEAVER, THROW, CLIP_BEAVER_MOVEMENT_WITH_STICK | WATER, HOLD, 0, 0),    // the stick held (flung: pose())
    PS(BEAVER, TELL, CLIP_BEAVER_BITE, PLAY | 3, 0, 1), PS(BEAVER, ATTACK, CLIP_BEAVER_BITE, PLAY | 6, 2, 3),
    PS(BEAVER, DIVE, CLIP_BEAVER_DIVE, PLAY | 6, 0, 3), PS(BEAVER, RISE, CLIP_BEAVER_ASCENT, PLAY | 6, 0, 3),
    PS(BEAVER, HURT_ROW, CLIP_BEAVER_DAMAGE, PLAY | 6, 0, 3), PS(BEAVER, DEAD, CLIP_BEAVER_DEATH, PLAY | 8, 0, 1),
    PS(BEAVER, STUN, CLIP_BEAVER_IDLE | WATER, HOLD, 0, 0), PS(BEAVER, ANY, CLIP_BEAVER_IDLE | WATER, LOOP | 6, 0, 0),
    PS(CHAMELEON, SNEAK, CLIP_CHAMELEON_MOVEMENT, LOOP | 6, 0, 0),
    PS(CHAMELEON, HOME, CLIP_CHAMELEON_MOVEMENT, LOOP | 6, 0, 0),
    PS(CHAMELEON, TELL, CLIP_CHAMELEON_TONGUE, HOLD, 1, 0),   // the mouth open
    PS(CHAMELEON, ATTACK, CLIP_CHAMELEON_TONGUE, PLAY | 6, 2, 7),
    PS(CHAMELEON, HURT_ROW, CLIP_CHAMELEON_DAMAGE, PLAY | 6, 0, 4),
    PS(CHAMELEON, DEAD, CLIP_CHAMELEON_DEATH, PLAY | 6, 0, 4), PS(CHAMELEON, STUN, CLIP_CHAMELEON_IDLE, HOLD, 0, 0),
    PS(CHAMELEON, ANY, CLIP_CHAMELEON_IDLE, LOOP | 6, 0, 0),
};
#undef PS

// The clip and frame critter i shows now.
static void pose(const Critter &c, uint32_t i, uint8_t &clip, uint8_t &f) {
    uint32_t t = c.t, st = c.st;
    if (c.kind == RACCOON && st == RECOIL && t >= 8) st = ANY;          // (its landing frame, then the idle)
    if (c.kind == BEAVER && st == THROW && t >= THROW_T) {               // the stick flung: the bite's lunge
        clip = CLIP_BEAVER_BITE;
        f = 2;
        return;
    }
    const Pose *p = POSES;
    while (p->ks != (c.kind << 5 | st) && p->ks != (c.kind << 5 | ANY)) p++;
    clip = p->clip;
    if (clip & WATER) clip = (uint8_t)((clip & ~WATER) + (c.flags & WET ? 6 : 0));
    uint32_t n = p->how & 63, b = p->be >> 4, e = p->be & 15;
    switch (p->how & 0xC0) {
    case LOOP: f = (uint8_t)((clock + i * 23u) / n % BANK_CLIPS[clip].frames); break;
    case HOLD: f = (uint8_t)b; break;
    case PLAY: f = upTo(b + t / n, e); break;
    default: f = (uint8_t)(e - upTo(t / n, e)); break;
    }
    if (c.kind == TURTLE && st == ATTACK)       // the bite: f0, the gaping f1 held (the tell), f2-f3 hurt, f4
        f = t < BITE_GAPE ? 0 : t < BITE_SNAP ? 1 : upTo(2 + (t - BITE_SNAP) / 6, 4);
}

static_assert(CLIP_BEAVER_IDLE_WATER == CLIP_BEAVER_IDLE + 6 &&
              CLIP_BEAVER_MOVEMENT_WATER == CLIP_BEAVER_MOVEMENT + 6 &&
              CLIP_BEAVER_MOVEMENT_WATER_WITH_STICK == CLIP_BEAVER_MOVEMENT_WITH_STICK + 6,
              "pose(): a beaver's water rows are its land rows' clips + 6");

static bool attacking(const Critter &c) {
    return c.st == TELL || c.st == ATTACK || c.st == WAKE || c.st == RISE || c.st == THROW;
}

// Drawn, or shown on screen: not under the water.
static bool shows(const Critter &c, int camX, int camY) { return c.st && c.st != UNDER && onScreen(c, camX, camY); }

// The stick's frame: a turn every 24 ticks, the way it flies.
static uint8_t spin(const Shot &s) {
    uint8_t f = (uint8_t)(s.t / 3 & 7);
    return s.vx < 0 ? (uint8_t)(7 - f) : f;
}

void plan(int camX, int camY) {
    for (auto &s : shots)
        if (s.t) spool::want(CLIP_STICK_SPIN, spin(s), spool::PRIO_ATTACK);
    for (uint32_t i = 0; i < MAX; i++) {
        const Critter &c = pool[i];
        if (!shows(c, camX, camY)) continue;
        uint8_t clip, f;
        pose(c, i, clip, f);
        spool::want(clip, f, attacking(c) ? spool::PRIO_ATTACK : spool::PRIO_CRITTER);
        // the frame shown last is kept until the new one is drawn (a crowd's misses would evict it,
        // and a critter whose new frame is late would vanish)
        if (c.lc != 0xFF && (c.lc != clip || c.lf != f)) spool::want(c.lc, c.lf, spool::PRIO_PREFETCH);
        if (c.st == SPAWN) spool::want(CLIP_SPAWN_PUFF, (uint8_t)(c.t / 5 > 3 ? 3 : c.t / 5), spool::PRIO_CRITTER);
    }
}

// How much of it shows (gfx_blit4k's dither level, 16 whole): appearing
// from its puff, fading when dead, a chameleon fading out, creeping as a
// shimmer, showing itself again.
static uint32_t level(const Critter &c) {
    uint32_t t = c.t, hold = DIE_HOLD[c.kind];
    switch (c.st) {
    case SPAWN: return t * 16 / SPAWN_T;
    case DEAD: return t >= hold ? 16 - (t - hold) * 16 / DIE_FADE[c.kind] : 16;
    }
    if (c.kind != CHAMELEON) return 16;
    switch (c.st) {
    case FADE: return 16 - t * 14 / FADE_T;
    case SNEAK: case HOME: return 2 + (clock >> 3 & 1);
    case APPEAR: return 2 + t * 14 / APPEAR_T;
    }
    return 16;
}

void list(scene::List &l, int camX, int camY, bool faint) {
    for (uint32_t i = 0; i < MAX; i++) {
        const Critter &c = pool[i];
        if (shows(c, camX, camY) && (faint || level(c) >= 8)) l.add(px(c), py(c), scene::CRITTER, (uint8_t)i);
    }
}

uint8_t shadowWidth(uint8_t i) {
    const Critter &c = pool[i];
    if (c.st == SPAWN || level(c) < 12 || (c.st == DEAD && c.t >= DIE_HOLD[c.kind])) return 0;   // (a shimmer: none)
    if (wetAt(px(c), py(c))) return 0;          // the water hides it
    if (c.kind == TURTLE && (c.st == SHELL || c.st == DEAD)) return 5;
    static const uint8_t W[4] = {7, 8, 6, 7};
    return W[c.kind];
}

void draw(uint8_t i, int camX, int camY) {
    Critter &c = pool[i];
    uint8_t clip, f, flags = 0, level = (uint8_t)critters::level(c);
    pose(c, i, clip, f);
    const uint8_t *p = scene::frameOr(clip, f, c.lc, c.lf);
    int sx = px(c) - camX, sy = py(c) - camY + 8;
    uint32_t t = c.t;
    if (level < 16 || (c.st == DEAD && t >= DIE_HOLD[c.kind])) {
        flags = GFX_B4_DITHER;
    } else if (c.st == DEAD) {
        if (c.kind == TURTLE || t < FLASH_T) flags = GFX_B4_REMAP;
    } else if (c.st == STUN && t < 40 && (t & 4)) {
        flags = GFX_B4_SOLID | GFX_B4_INK(C_BONE);  // parried: blinking white
    } else if (c.flash) {
        flags = GFX_B4_REMAP;
    }
    const uint8_t *remap = REMAP_RED;
    if (c.kind == RACCOON && (c.flags & ROT) && !(flags & (GFX_B4_REMAP | GFX_B4_SOLID))) {
        flags |= GFX_B4_REMAP;      // one of the toad's: rotten
        remap = REMAP_ROT;
    }
    if (p) scene::blit(p, sx, sy, c.face, flags, remap, level);
    if (c.st == SPAWN) {            // the puff over it
        const uint8_t *q = spool::peek(CLIP_SPAWN_PUFF, (uint8_t)(t / 5 > 3 ? 3 : t / 5));
        if (q) scene::blit(q, sx, sy, 1);
    }
    if (c.kind == CHAMELEON && c.st == TELL && (t & 4) == 0) {     // the pink glint in its open mouth
        int mx = sx + (c.face > 0 ? 8 : -10);
        if (scene::rec) {           // (Play's sprites in the world's stream: this one after it)
            scene::recReal = true;
            scene::grow(mx, sy - 6, 2, 2);
        } else {
            gfx_fillRect(mx, sy - 6, 2, 2, C_RUST);
            gfx_pixel(mx + (c.face > 0), sy - 6, C_BONE);
        }
    }
}

void drawShots(int camX, int camY) {
    for (auto &c : pool)            // a beaver under the water: a ring on the surface over it, a bubble
        if (c.st == UNDER && onScreen(c, camX, camY)) {
            int sx = px(c) - camX, sy = py(c) - camY + 4;
            hud::ring(sx, sy, 3 + (c.t >> 3 & 1), C_FOG);
            gfx_pixel(sx, sy - (c.t >> 2 & 1), C_BONE);
        }
    for (auto &s : shots) {
        if (!s.t) continue;
        int sx = (s.x >> 4) - camX, sy = (s.y >> 4) - camY + 8;
        scene::shadow(sx, sy, 2);
        if (const uint8_t *p = spool::frame(CLIP_STICK_SPIN, spin(s))) scene::blit(p, sx, sy - 8, 1);
    }
}

// --- combat ----------------------------------------------------------------------------

// In its shell (the sword rings off): hiding, hidden, and coming out until
// its head shows (the second half of the shell row backwards).
static bool hidden(const Critter &c) {
    return c.kind == TURTLE && (c.st == HIDE || c.st == SHELL || (c.st == UNHIDE && c.t < HIDE_T / 2));
}

// Under the water, or half way down or up: nothing can touch a beaver.
static bool submerged(const Critter &c) {
    return c.st == UNDER || (c.st == DIVE && c.t >= DIVE_T / 2) || (c.st == RISE && c.t < RISE_T / 2);
}

bool hurtbox(uint8_t i, player::Box &b) {
    const Critter &c = pool[i];
    if (!c.st || c.st == SPAWN || c.st == DEAD || submerged(c)) return false;
    int x = px(c), y = py(c);
    if (c.kind == RACCOON) {
        b = {(int16_t)(x - 9), (int16_t)(y - 12), (int16_t)(x + 9), (int16_t)(y + 1)};
    } else if (hidden(c)) {         // the shell sits behind the pivot
        int m = x - 4 * c.face;
        b = {(int16_t)(m - 6), (int16_t)(y - 9), (int16_t)(m + 6), (int16_t)(y + 1)};
    } else {                        // the turtle, the beaver, the chameleon (a little wider)
        int hw = c.kind == CHAMELEON ? 12 : 10;
        b = {(int16_t)(x - hw), (int16_t)(y - 11), (int16_t)(x + hw), (int16_t)(y + 1)};
    }
    return true;
}

// A stick's box: 8 px round where it flies, 8 px over its ground point.
ECRT_OUTLINE static void shotBox(const Shot &s, player::Box &b) {
    int x = s.x >> 4, y = s.y >> 4;
    b = {(int16_t)(x - 4), (int16_t)(y - 12), (int16_t)(x + 4), (int16_t)(y - 4)};
}

// Where a stick comes from: behind it, the way it flies.
ECRT_OUTLINE static int16_t shotFrom(const Shot &s) { return (int16_t)((s.x >> 4) - (s.vx < 0 ? -8 : 8)); }

bool attack(uint8_t i, player::Box &b, uint8_t &halves, int16_t &fromX) {
    if (i >= MAX) {                 // a stick in the air (not one parried back)
        const Shot &s = shots[i - MAX];
        if (!s.t || (s.flags & MINE)) return false;
        shotBox(s, b);
        halves = 1;
        fromX = shotFrom(s);
        return true;
    }
    const Critter &c = pool[i];
    if (c.st != ATTACK || (c.flags & LANDED)) return false;
    int x = px(c), y = py(c), n, w, top, h;
    if (c.kind == RACCOON) {        // the swipe: 14 x 10, 4 px out
        n = 4; w = 14; top = 12; h = 10;
    } else if (c.kind == TURTLE) {  // the bite, f2-f3: 12 x 8, 6 px out
        if (c.t < BITE_SNAP || c.t >= BITE_SNAP + 12) return false;
        n = 6; w = 12; top = 10; h = 8;
    } else if (c.kind == BEAVER) {  // the bite, f2: 12 x 8, 2 px out
        if (c.t >= 6) return false;
        n = 2; w = 12; top = 8; h = 8;
    } else {                        // the tongue, f2-f4: from 8 px out to its tip, 6 high at the mouth
        if (c.t >= C_LASH_HURTS) return false;
        n = 8; w = 21; top = 8; h = 6;
    }
    b.x0 = (int16_t)(c.face > 0 ? x + n : x - n - w);
    b.x1 = (int16_t)(b.x0 + w);
    b.y0 = (int16_t)(y - top);
    b.y1 = (int16_t)(b.y0 + h);
    halves = 2;
    fromX = (int16_t)x;
    return true;
}

void landed(uint8_t i, uint8_t outcome) {
    if (i >= MAX) {                 // a stick: parried, it flies back faster at the critters; else it breaks
        Shot &s = shots[i - MAX];
        if (outcome != player::HIT_PARRIED || ECRT_LEAN) {     // (a lean build: it breaks all the same)
            shotBreak((uint8_t)(i - MAX));
            return;
        }
        s.vx = (int8_t)(-s.vx * 5 / 4);
        s.vy = (int8_t)(-s.vy * 5 / 4);
        s.t = 1;
        s.flags = MINE;
        return;
    }
    Critter &c = pool[i];
    c.flags |= LANDED;
    if (outcome == player::HIT_PARRIED) {
        enter(c, STUN);
        c.kx = (int8_t)(-c.face * KNOCK / 2);
    } else if (outcome == player::HIT_BLOCKED) {
        enter(c, RECOIL);
        c.t = c.kind == RACCOON ? 8 : 0;    // (the raccoon skips its landing frame)
        c.kx = (int8_t)(-c.face * KNOCK);
    }
}

uint8_t hurt(uint8_t i, uint8_t &dmg, int16_t fromX, uint8_t swing) {
    Critter &c = pool[i];
    if (!c.st || c.st == SPAWN || c.st == DEAD || c.swing == swing) return MISSED;
    c.swing = swing;
    if (hidden(c)) return TINK;
    bool asleep = c.kind == TURTLE && (c.st == SLEEP || c.st == WAKE);     // (still drowsy while it yawns)
    if (asleep) dmg = (uint8_t)(dmg * 2);
    c.hp = (uint8_t)(dmg >= c.hp ? 0 : c.hp - dmg);
    c.flash = FLASH_T;
    int8_t away = px(c) >= fromX ? 1 : -1;
    if (!c.hp) {
        enter(c, DEAD);
        c.kx = (int8_t)(away * KNOCK);
        return KILLED;
    }
    if (c.kind == RACCOON) {
        c.kx = (int8_t)(away * KNOCK);
        if (c.st == TELL || c.st == ATTACK) {
            enter(c, RECOIL);       // knocked out of its swipe
            c.t = 8;
        } else if (c.st != STUN && c.st != RECOIL) {
            chase(c, false);
        }
        return HURT;
    }
    if (c.kind >= BEAVER) {         // the damage row (a chameleon shown whole), knocked back
        c.kx = (int8_t)(away * KNOCK);
        enter(c, HURT_ROW);
        return HURT;
    }
    c.kx = (int8_t)(away * KNOCK / 2);      // heavier
    faceTo(c, fromX);
    if (c.aux || (c.hp <= 2 && !(c.flags & SHY))) {
        if (c.hp <= 2) c.flags |= SHY;
        enter(c, HIDE);
        c.aux = 0;
    } else {
        c.aux = WINDOW_T;
        if (c.st == SLEEP) enter(c, WAKE);  // (one already yawning yawns on)
        else if (c.st == IDLE || c.st == HOME) chase(c, false);
    }
    return HURT;
}

bool shotBlade(uint8_t j, player::Box &b, uint8_t &dmg, uint8_t &swing, int16_t &fromX) {
    const Shot &s = shots[j];
    if (!s.t || !(s.flags & MINE)) return false;
    shotBox(s, b);
    dmg = 1;
    swing = (uint8_t)(0xFD + j);    // (one hit a stick: the squire's swings rarely come round to these)
    fromX = shotFrom(s);
    return true;
}

#if CHGAME_DEBUG
static const char STATE_NAME[][7] = {"FREE", "SPAWN", "IDLE", "WANDER", "CHASE", "TELL", "ATTACK", "RECOIL",
                                     "STUN", "STEAL", "EAT", "HOME", "SLEEP", "WAKE", "HIDE", "SHELL", "UNHIDE",
                                     "DEAD", "DIVE", "UNDER", "RISE", "CARRY", "REPAIR", "THROW", "FADE", "SNEAK",
                                     "APPEAR", "HURT"};

char *report(char *p) {
    p = fmtStr(p, "ACTORS n=");
    p = fmtInt(p, (int32_t)alive(NO_NEST));
    for (uint32_t i = 0; i < MAX; i++) {
        const Critter &c = pool[i];
        if (!c.st) continue;
        p = fmtStr(p, " ");
        p = fmtInt(p, (int32_t)i);
        static const char TAG[][4] = {":R:", ":T:", ":B:", ":C:", ":S:", ":X:"};
        p = fmtStr(p, TAG[c.flags & STICK ? (c.kind == RACCOON ? 5 : 4) : c.kind]);
        p = fmtStr(p, STATE_NAME[c.st]);
        p = fmtStr(p, ":hp");
        p = fmtInt(p, c.hp);
        p = fmtStr(p, "@");
        p = fmtInt(p, px(c));
        p = fmtStr(p, ",");
        p = fmtInt(p, py(c));
        p = fmtStr(p, "k");
        p = fmtInt(p, c.kx);
    }
    return p;
}
#endif

}  // namespace critters
