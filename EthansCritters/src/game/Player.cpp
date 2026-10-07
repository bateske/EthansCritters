// The squire. See Player.h.
//
// One state machine: what the squire does (State), a tick counter t since
// it started, and from those the clip and frame to show. Frame times come
// from the bank (sheets.py: thrust and parry 50 ms a frame, so the thrust's
// blade is out on ticks 12-17 of 21).
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Bank.h"
#include "../assets/Palette.h"
#include "../engine/Spool.h"
#include "Player.h"
#include "Scene.h"

namespace player {

static const int16_t SPEED = 19, DIAG = 13;     // Q4 a tick: 71 px/s, 49 px/s each way on a diagonal
static const uint8_t PARRY_TICKS = 10;          // a hit this soon after B is pressed is parried
static const uint8_t PARRY_LOCK = 24;           // ... and B opens no new window this soon after the last (mashing B parries nothing)
static const uint8_t RIPOSTE_TICKS = 30;        // A this soon after a parry ripostes
static const uint8_t A_EARLY = 8;               // ... or this soon before it
static const uint8_t A_BUFFER = 10;             // A this soon before a thrust or the hurt row ends: the next thrust at once
static const uint8_t INVULN_TICKS = 60;         // after a hit: the hurt row, then blinking
static const uint8_t FLASH_TICKS = 4;           // drawn red after a hit (ticks, so as long at 30 fps)
static const int16_t KNOCK = 40;                // Q4 a tick, slowing by KNOCK_DRAG
static const int16_t KNOCK_DRAG = 5;
static const uint8_t RIPOSTE_LEAD = 3;          // parry frames 3-5 lead into the thrust
static const uint8_t ARC_LAST = 6;              // the swipe's arc shows from the first hurting frame to this one (faded)
// The arc's reach (props.py's swipe: the crescent's box round its pixels), px from his feet:
// in front, from his chest to his feet, so a critter a little above or below is hit too.
static const int16_t ARC_X0 = 2, ARC_X1 = 24, ARC_Y0 = -18, ARC_Y1 = 1;

struct Squire {
    int16_t x, y;                   // Q4 world
    int16_t knock;                  // Q4 a tick, along x
    uint16_t t;                     // ticks in this state
    uint16_t walkT;                 // ticks walked (the walk cycle keeps going across stops)
    State st;
    int8_t face;
    int8_t hx, hy;                  // the way it last walked
    uint8_t hp, parryT, riposteT, invuln, flash, swing;
    uint8_t aT;                     // A pressed this long ago (under the guard: a riposte if the parry comes now; else the next thrust)
    bool riposte;                   // this thrust is a riposte
    uint8_t clip, frame;            // to show
    uint8_t lastClip, lastFrame;    // shown last (while the new one is on its way)
    uint8_t parryCD;                // ticks until B may open a parry window again
};
static Squire s;
static MoveFn mover;

void setMover(MoveFn fn) { mover = fn; }

int16_t x() { return (int16_t)(s.x >> 4); }
int16_t y() { return (int16_t)(s.y >> 4); }
int8_t facing() { return s.face; }
void heading(int8_t &dx, int8_t &dy) {
    dx = s.hx;
    dy = s.hy;
}
State state() { return s.st; }
uint8_t hp() { return s.hp; }

void heal(uint8_t halves) {
    if (s.st == DEAD) return;
    s.hp = (uint8_t)(s.hp + halves > MAX_HP ? MAX_HP : s.hp + halves);
}

void push(int16_t q4) { s.knock = q4; }

void reset(int16_t px, int16_t py) {
    s = Squire();
    s.x = (int16_t)(px << 4);
    s.y = (int16_t)(py << 4);
    s.face = 1;
    s.hp = MAX_HP;
    s.lastClip = 0xFF;
}

static uint32_t tpf(uint8_t c) { return bankTicks(BANK_CLIPS[c]); }

static void enter(State st) {
    s.st = st;
    s.t = 0;
}

static void startThrust(bool riposte) {
    enter(riposte ? RIPOSTE : THRUST);
    s.riposte = riposte;
    if (++s.swing == 0xFF) s.swing = 0;    // (0xFF: what a critter or nest never hit holds)
}

// A move is over: the thrust A asked for while it played, else stand.
static void done() {
    if (s.aT) {
        s.aT = 0;
        startThrust(false);
    } else {
        enter(IDLE);
    }
}

static void move(int32_t dx, int32_t dy) {
    int32_t qx = s.x, qy = s.y;
    if (mover) {
        mover(qx, qy, dx, dy);
    } else {
        qx += dx;
        qy += dy;
    }
    s.x = (int16_t)qx;
    s.y = (int16_t)qy;
}

// The clip and frame the state shows at tick t.
static void pose() {
    uint32_t t = s.t, n;
    switch (s.st) {
    case IDLE:
        s.clip = CLIP_SQUIRE_IDLE;
        s.frame = (uint8_t)(t / tpf(CLIP_SQUIRE_IDLE) % BANK_CLIPS[CLIP_SQUIRE_IDLE].frames);
        break;
    case WALK:
        s.clip = CLIP_SQUIRE_WALK;
        s.frame = (uint8_t)(s.walkT / tpf(CLIP_SQUIRE_WALK) % BANK_CLIPS[CLIP_SQUIRE_WALK].frames);
        break;
    case THRUST:
        s.clip = CLIP_SQUIRE_THRUST;
        s.frame = (uint8_t)(t / tpf(CLIP_SQUIRE_THRUST));
        break;
    case GUARD:                     // raise the guard (f0-f2) and hold it
        s.clip = CLIP_SQUIRE_PARRY;
        n = t / tpf(CLIP_SQUIRE_PARRY);
        s.frame = (uint8_t)(n > 2 ? 2 : n);
        break;
    case RIPOSTE:                   // swing back (parry f3-f5), then the thrust from f1
        n = tpf(CLIP_SQUIRE_PARRY) * RIPOSTE_LEAD;
        if (t < n) {
            s.clip = CLIP_SQUIRE_PARRY;
            s.frame = (uint8_t)(3 + t / tpf(CLIP_SQUIRE_PARRY));
        } else {                    // in the thrust's own frame time, as tick() ends it
            s.clip = CLIP_SQUIRE_THRUST;
            s.frame = (uint8_t)(1 + (t - n) / tpf(CLIP_SQUIRE_THRUST));
        }
        break;
    case HURT:
        s.clip = CLIP_SQUIRE_DAMAGE;
        s.frame = (uint8_t)(t / tpf(CLIP_SQUIRE_DAMAGE));
        break;
    case DEAD:                      // fall, then lie still
        s.clip = CLIP_SQUIRE_DEATH;
        n = t / tpf(CLIP_SQUIRE_DEATH);
        s.frame = (uint8_t)(n >= BANK_CLIPS[CLIP_SQUIRE_DEATH].frames ? BANK_CLIPS[CLIP_SQUIRE_DEATH].frames - 1 : n);
        break;
    }
}

void tick(uint8_t late) {
    if (s.parryT) s.parryT--;
    if (s.riposteT) s.riposteT--;
    if (s.invuln) s.invuln--;
    if (s.flash) s.flash--;
    if (s.knock) {                  // knocked back, whatever it is doing
        move(s.knock, 0);
        s.knock = (int16_t)(s.knock > 0 ? (s.knock > KNOCK_DRAG ? s.knock - KNOCK_DRAG : 0)
                                        : (s.knock < -KNOCK_DRAG ? s.knock + KNOCK_DRAG : 0));
    }
    if (s.t < 0xFFFF) s.t++;        // (lying dead for good)
    bool a = chgame.justPressed(A_BUTTON) || (late & A_BUTTON);
    bool bEdge = chgame.justPressed(B_BUTTON) || (late & B_BUTTON), b = chgame.pressed(B_BUTTON) || bEdge;
    if (s.aT) s.aT--;
    if (s.parryCD) s.parryCD--;
    if (bEdge && s.st != HURT && s.st != DEAD && !s.parryCD) {
        s.parryT = PARRY_TICKS;
        s.parryCD = PARRY_LOCK;
    }
    switch (s.st) {
    case IDLE:
    case WALK: {
        if (a || s.aT) {            // (a riposte still, if the parry was just now)
            s.aT = 0;
            startThrust(s.riposteT != 0);
            s.riposteT = 0;
            break;
        }
        if (b) { enter(GUARD); break; }
        int dx = chgame.pressed(RIGHT_BUTTON) - chgame.pressed(LEFT_BUTTON);
        int dy = chgame.pressed(DOWN_BUTTON) - chgame.pressed(UP_BUTTON);
        if (!dx && !dy) {
            if (s.st != IDLE) enter(IDLE);
            break;
        }
        if (s.st != WALK) enter(WALK);
        if (dx) s.face = (int8_t)dx;
        s.hx = (int8_t)dx;
        s.hy = (int8_t)dy;
        int32_t v = dx && dy ? DIAG : SPEED;
        move(dx * v, dy * v);
        s.walkT++;
        break;
    }
    case THRUST:
        if (a) s.aT = A_BUFFER;
        if (s.t >= tpf(CLIP_SQUIRE_THRUST) * BANK_CLIPS[CLIP_SQUIRE_THRUST].frames) done();
        break;
    case GUARD:                     // a tap still shows the guard for the parry window, a parry for the riposte's
        if (a) s.aT = A_EARLY;
        if (s.aT && s.riposteT) { s.aT = s.riposteT = 0; startThrust(true); break; }
        if (!b && s.t >= PARRY_TICKS && !s.riposteT) enter(IDLE);
        break;
    case RIPOSTE:
        if (a) s.aT = A_BUFFER;
        if (s.t >= tpf(CLIP_SQUIRE_PARRY) * RIPOSTE_LEAD +
                   tpf(CLIP_SQUIRE_THRUST) * (BANK_CLIPS[CLIP_SQUIRE_THRUST].frames - 1u)) done();
        break;
    case HURT:
        if (a) s.aT = A_BUFFER;
        if (s.t >= tpf(CLIP_SQUIRE_DAMAGE) * BANK_CLIPS[CLIP_SQUIRE_DAMAGE].frames) done();
        break;
    case DEAD:
        break;
    }
    pose();
}

Box body() {
    int16_t px = x(), py = y();
    Box b = {(int16_t)(px - 6), (int16_t)(py - 19), (int16_t)(px + 7), (int16_t)(py + 1)};
    return b;
}

bool blade(Box &b, uint8_t &damage, uint8_t &swing) {
    if (s.clip != CLIP_SQUIRE_THRUST || (s.st != THRUST && s.st != RIPOSTE)) return false;
    uint8_t act = BANK_CLIPS[CLIP_SQUIRE_THRUST].active;
    if (s.frame < act >> 4 || s.frame > (act & 15)) return false;
    int16_t px = x(), py = y();
    // the arc's box in front of him, mirrored about the pivot (d -> -1 - d) when facing left
    b.x0 = (int16_t)(s.face > 0 ? px + ARC_X0 : px - ARC_X1);
    b.x1 = (int16_t)(b.x0 + ARC_X1 - ARC_X0);
    b.y0 = (int16_t)(py + ARC_Y0);
    b.y1 = (int16_t)(py + ARC_Y1);
    damage = s.riposte ? 2 : 1;
    swing = s.swing;
    return true;
}

Hit hit(int16_t fromX, uint8_t halves, bool any) {
    if (s.st == DEAD || s.invuln) return HIT_IGNORED;
    bool front = !any && (fromX - x()) * s.face >= 0;
    if (front && s.parryT) {
        s.parryT = s.parryCD = 0;           // (a clean parry may be followed by another at once)
        s.riposteT = RIPOSTE_TICKS;
        if (s.st != GUARD) {
            enter(GUARD);
            s.t = (uint16_t)(2 * tpf(CLIP_SQUIRE_PARRY));     // the guard is up
        }
        pose();
        return HIT_PARRIED;
    }
    if (front && s.st == GUARD) {
        s.knock = (int16_t)(-s.face * KNOCK / 2);
        return HIT_BLOCKED;
    }
    s.hp = (uint8_t)(halves >= s.hp ? 0 : s.hp - halves);
    s.flash = FLASH_TICKS;
    s.parryT = s.riposteT = 0;
    s.knock = (int16_t)(fromX > x() ? -KNOCK : KNOCK);
    if (!s.hp) {
        enter(DEAD);
        s.knock /= 2;
    } else {
        enter(HURT);
        s.invuln = INVULN_TICKS;
    }
    pose();
    return HIT_TAKEN;
}

// The thrust's first hurting frame (the arc shows from there).
static uint8_t firstHurt() { return (uint8_t)(BANK_CLIPS[CLIP_SQUIRE_THRUST].active >> 4); }

void plan() {
    spool::want(s.clip, s.frame, spool::PRIO_PLAYER);
    const BankClip &c = BANK_CLIPS[s.clip];
    if (s.frame + 1 < c.frames) spool::want(s.clip, (uint8_t)(s.frame + 1), spool::PRIO_PREFETCH);
    if (s.clip == CLIP_SQUIRE_THRUST) spool::want(CLIP_SWIPE_ARC, 0, spool::PRIO_PLAYER);  // (the arc: a block, once a swing)
}

void draw(int camX, int camY) {
    const uint8_t *p = spool::peek(s.clip, s.frame);
    if (p) {
        s.lastClip = s.clip;
        s.lastFrame = s.frame;
    } else if (s.lastClip != 0xFF) {
        p = spool::peek(s.lastClip, s.lastFrame);  // the new frame is late: hold the old one
    }
    // after the hurt row, blink while still invulnerable
    if (!p || (s.st != HURT && s.invuln && (s.invuln & 4))) return;
    // the swipe's arc behind him while the blade is out (his sword stays in view): bone, blinking
    // ochre every other pair of ticks, dithered away on the frame after the last hurting one
    const uint8_t *arc;
    if (s.clip == CLIP_SQUIRE_THRUST && s.frame >= firstHurt() && s.frame <= ARC_LAST &&
        (arc = spool::peek(CLIP_SWIPE_ARC, 0))) {
        bool last = s.frame == ARC_LAST;
        scene::blit(arc, x() - camX, y() - camY, s.face,
                    (uint8_t)((s.t & 2 ? GFX_B4_SOLID | GFX_B4_INK(C_OCHRE) : 0) | (last ? GFX_B4_DITHER : 0)),
                    nullptr, last ? 8 : 16);
    }
    scene::blit(p, x() - camX, y() - camY, s.face, s.flash ? GFX_B4_REMAP : 0, REMAP_RED);
}

}  // namespace player
