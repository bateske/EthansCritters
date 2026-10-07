// The Rot: its gate, its arena, the giant toad. See Rot.h.
//
// The toad is a state and a tick counter t since it began, like a critter:
// the frame it shows comes from those (frame()), and so does where it is
// in a hop (place(): from its take-off point toward the landing point over
// the airborne frames, lifted on an arc; kept in gx, gy, lift each tick).
// It is drawn straight from the card's landing buffer: one card::stream()
// of the blocks of its frame's rows that show, each blitted while the next
// one arrives by DMA.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Bank.h"
#include "../assets/Boss.h"
#include "../assets/CardIndex.h"
#include "../assets/Palette.h"
#include "../assets/WorldData.h"
#include "../engine/Camera.h"
#include "../engine/Card.h"
#include "../engine/Pace.h"
#include "../engine/Spool.h"
#include "../engine/Terrain.h"
#include "../fx/Fx.h"
#include "../ui/Hud.h"
#include "../../Sounds.h"
#include "Critter.h"
#include "Nests.h"
#include "Play.h"
#include "Rot.h"

namespace rot {

enum St : uint8_t { WAIT, RISE, ROAR, IDLE, HOP, TELL, LASH, STUN, HURT, DIE, GONE };
enum Gate : uint8_t { SHUT, PARTING, OPEN, CLOSING };

struct Toad {                       // 24 B
    int16_t x, y;                   // its feet, world px (in a hop: where it left the ground)
    int16_t tx, ty;                 // a hop's landing point
    int16_t gx, gy;                 // its feet on the ground now (in a hop: along the way)
    uint16_t callT;                 // (angry) ticks to its next call for rot raccoons
    uint8_t st, t;                  // what it does, ticks since (up to 255)
    uint8_t hp, flash;              // red for this many ticks
    uint8_t hits;                   // blows since it last staggered
    uint8_t swing;                  // the squire's swing that hit it last
    uint8_t hops;                   // hops still to come in this run of them
    uint8_t sits;                   // sits so far (every third one blinks; rot raccoons called, by reed)
    uint8_t wait;                   // this sit's ticks
    uint8_t lift;                   // px off the ground (in a hop)
    int8_t face;                    // +1 right (the sheet's way), -1 left
    bool landed;                    // this attack has met him
};
static Toad k;
static uint8_t gate, gateT;         // the gate, and ticks left of its parting or closing
static uint8_t saved;               // GATE_OPEN, BEATEN
static uint8_t gl = 0xFF, gf;       // the gate's frame drawn last (while the next is on its way)
static bool due;

static const int GX = (GATE.x0 + GATE.x1) / 2, GY = GATE.y1 - 6;   // the wall's foot, its middle
static const int BASE_HW = 22, BASE_H = 10;     // its bulk on the ground (solid)
static const int BODY_HW = 26, BODY_H = 44, BODY_FRONT = 6;  // where the sword finds it (its legs reach out in front)
static const int LANE = 10, REACH = 54;         // the tongue: when he is this level with it, this near
static const int TONGUE_X0 = 12, TONGUE_X1 = 46, TONGUE_Y0 = 14, TONGUE_Y1 = 6;  // its box: px out, px up
static const int RING = 16;                     // the landing's dust ring: half its box (feet within ~20 px)
static const int LIFT = 12;                     // a hop's height, px
static const uint8_t RISE_T = 60, ROAR_T = 90, OPEN_T = 4, LASH_T = 36, LASH_HURTS = 18, STUN_T = 72,
                     HURT_T = 24, LAND_T = 18, FLASH_T = 4;
static const uint8_t DIE_HOLD = 96, DIE_END = 144;  // the death row (8 ticks a frame) held, then the fade
static const uint8_t PART_T = 40, CLOSE_T = 24;
static const uint16_t CALL_T = 720;
// The states that only wait: after this many ticks it sits again (0: not one of them).
static const uint8_t SIT_AFTER[] = {0, 0, ROAR_T, 0, 0, 0, LASH_T, STUN_T, HURT_T};

static int iabs(int v) { return v < 0 ? -v : v; }
static int within(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clamp(int v, int m) { return within(v, -m, m); }
static uint8_t upTo(uint32_t v, uint32_t last) { return (uint8_t)(v > last ? last : v); }

// At half its HP it is angry: quicker tells, longer and double hops, rot raccoons.
static bool angry() { return k.hp <= HP / 2; }
ECRT_OUTLINE static uint32_t crouchT() { return angry() ? 9 : 14; }
ECRT_OUTLINE static uint32_t airT() { return angry() ? 20 : 24; }

void load(uint8_t f) { saved = f; }
uint8_t flags() { return saved; }
bool beaten() { return saved & BEATEN; }
bool fighting() { return k.st != WAIT && k.st != GONE; }

bool saveDue() {
    bool d = due;
    due = false;
    return d;
}

void reset() {
    k.st = (saved & BEATEN) ? GONE : WAIT;
    gate = (saved & GATE_OPEN) ? OPEN : SHUT;
    gateT = 0;
}

static void enter(uint8_t st) {
    k.st = st;
    k.t = 0;
    k.landed = false;
}

static void sit() {
    enter(IDLE);
    k.sits++;
    k.wait = (uint8_t)(angry() ? 40 + critters::rnd(20) : 70 + critters::rnd(30));
}

static bool airborne() { return k.lift; }

// Where its feet are on the ground (gx, gy) and how high it is (lift), from its state.
static void place() {
    int x = k.x, y = k.y, lift = 0, a = (int)airT(), u = k.t - (int)crouchT();
    if (k.st == HOP && u > 0 && u < a) {
        x += (k.tx - k.x) * u / a;
        y += (k.ty - k.y) * u / a;
        lift = 1 + 4 * LIFT * u * (a - u) / (a * a);
    }
    k.gx = (int16_t)x;
    k.gy = (int16_t)y;
    k.lift = (uint8_t)lift;
}

// A hop at him (to land just behind his feet: it comes down on him, and he
// stays in front of it), up to 64 px (angry: 76), onto ground it can sit on
// in the arena (else half as far). False: nowhere to land.
static bool hop() {
    int dx = player::x() - k.x, dy = player::y() - 6 - k.y, ax = iabs(dx), ay = iabs(dy);
    int d = ax > ay ? ax + ay / 2 : ay + ax / 2, m = angry() ? 76 : 64;     // (octagonal: within 12% of round)
    if (d > m) {
        dx = dx * m / d;
        dy = dy * m / d;
    }
    for (int n = 2; n; n--, dx /= 2, dy /= 2) {
        // kept inside the arena's margins, so it comes down as near him as it can (a landing past
        // them was given up: by the wall it sat on while he struck it); he stays within its
        // dust ring's reach of them (Play.cpp's walk())
        int x = k.x + dx, y = k.y + dy;
        x = within(x, ARENA.x0 + 28, ARENA.x1 - 29);
        y = within(y, ARENA.y0 + 12, ARENA.y1 - 9);
        if (terrain::hits(x - BASE_HW, y - BASE_H, x + BASE_HW, y, terrain::BLOCK_WALK)) continue;
        k.tx = (int16_t)x;
        k.ty = (int16_t)y;
        if (dx) k.face = dx < 0 ? -1 : 1;
        enter(HOP);
        return true;
    }
    return false;
}

// After a sit: the tongue if he stands level in front within its reach,
// else a hop at him (angry: two).
static void choose() {
    int dx = player::x() - k.x, ax = iabs(dx);
    k.face = dx < 0 ? -1 : 1;
    if (iabs(player::y() - k.y) <= LANE && ax >= 20 && ax <= REACH) {
        enter(TELL);
        sfx(Sfx::Hiss);
        return;
    }
    k.hops = angry();
    if (!hop()) {
        sit();
        k.wait = 20;
    }
}

// The brambles: parting once every nest is cleared and the squire comes
// near (saved at once), closing behind him when the fight begins.
static void gateTick() {
    if (gateT) {
        if (!--gateT) gate = gate == CLOSING ? SHUT : OPEN;
        return;
    }
    if (gate == SHUT && k.st == WAIT && nests::allCleared() && !nests::banner() &&
        iabs(player::x() - GX) + iabs(player::y() - GY) < 112) {
        gate = PARTING;
        gateT = PART_T;
        sfx(Sfx::Hiss);
        camera::shake(24, 2);
        saved |= GATE_OPEN;
        due = true;
    }
}

void tick() {
    gateTick();
    int sx = player::x(), sy = player::y();
    bool squire = player::state() != player::DEAD;
    if (k.st == WAIT) {             // his feet in the arena: the brambles shut behind him, the toad rises
        if (!(saved & BEATEN) && gate == OPEN && squire && sx >= ARENA_TRIGGER.x0 && sx < ARENA_TRIGGER.x1 &&
            sy >= ARENA_TRIGGER.y0 && sy < ARENA_TRIGGER.y1) {
            k = Toad();
            k.x = k.tx = TOAD_X;
            k.y = k.ty = TOAD_Y;
            k.hp = HP;
            k.swing = 0xFF;
            k.callT = CALL_T;
            k.face = -1;            // (he comes in from the gate, south-west of it)
            enter(RISE);
            gate = CLOSING;
            gateT = CLOSE_T;
            sfx(Sfx::Hiss);
            place();
        }
        return;
    }
    if (k.st == GONE) return;
    if (k.t < 255) k.t++;
    if (k.flash) k.flash--;
    if (angry() && k.st != DIE && squire && !--k.callT) {   // a croak: its rot raccoons out of the reeds
        k.callT = CALL_T;
        sfx(Sfx::Roar, 9);
        for (uint32_t n = critters::alive(critters::NO_NEST); n < 2; n++) {
            const int16_t *r = ARENA_REEDS[k.sits++ % ARENA_REED_COUNT];
            critters::spawn(critters::RACCOON, r[0], r[1], critters::NO_NEST, true);
        }
    }
    if (k.st < sizeof SIT_AFTER && SIT_AFTER[k.st] && k.t >= SIT_AFTER[k.st]) {
        // a stagger is answered at once (IDLE below): after a whole sit a sword at its side staggered
        // it again before it ever moved (stun-locked from 24 HP to none by mashing A)
        bool answer = k.st == HURT;
        sit();
        if (answer) k.wait = 0;
    }
    switch (k.st) {
    case RISE:                      // out of the murk: dithered in, the water thrown off it
        if (k.t == 24) {
            sfx(Sfx::Splash);
            fx::mud(camera::screenX(k.x), camera::screenY(k.y) - 8, 8, C_FOG);
        }
        if (k.t >= RISE_T) {
            enter(ROAR);
            sfx(Sfx::Roar);
            camera::shake(40, 3);
        }
        break;
    case IDLE:
        if (iabs(sx - k.x) > 6) k.face = sx < k.x ? -1 : 1;
        if (squire && k.t >= k.wait) choose();
        break;
    case HOP: {
        uint32_t c = crouchT(), a = airT();
        if (k.t == c + a) {         // down: the ground shakes, dust (water in the shallows) thrown out
            k.x = k.tx;
            k.y = k.ty;
            camera::shake(16, 3);
            sfx(Sfx::Thud);
            fx::dust(camera::screenX(k.x), camera::screenY(k.y), 10,
                     terrain::at(k.x, k.y - 1) == T_SHALLOW ? C_FOG : C_MUD);
        }
        if (k.t >= c + a + LAND_T && !(k.hops && (k.hops--, hop()))) sit();
        break;
    }
    case TELL:                      // the mouth open (the tell), then the lash
        if (k.t >= OPEN_T + (angry() ? 11 : 18)) {
            enter(LASH);
            sfx(Sfx::Thwip);
        }
        break;
    case DIE:                       // the death row, held, faded out: the swamp is quiet
        // its rot raccoons fall with it, those still coming out of the reeds as soon as they are out
        // (struck only at the blow, those lived on and could kill him under the win clip)
        for (uint32_t i = 0; i < critters::MAX; i++) {
            uint8_t d = 9;
            critters::hurt((uint8_t)i, d, k.x, 0xFC);
        }
        if (k.t >= DIE_END) {
            k.st = GONE;
            saved |= BEATEN;
            due = true;
            gate = PARTING;
            gateT = PART_T;
            play::won();
        }
        break;
    }
    place();
}

bool blocks(int x0, int y0, int x1, int y1) {
    if (gate != OPEN && x1 > GATE.x0 && x0 < GATE.x1 && y1 > GATE.y0 && y0 < GATE.y1) return true;
    return fighting() && !airborne() && x1 > k.x - BASE_HW && x0 < k.x + BASE_HW && y1 > k.y - BASE_H && y0 < k.y;
}

// --- the picture ----------------------------------------------------------------------

static uint8_t gateFrame() {
    return (uint8_t)(gate == PARTING ? (PART_T - gateT) * 4 / PART_T : gate == CLOSING ? (gateT - 1) * 4 / CLOSE_T : 0);
}

ECRT_OUTLINE static bool gateShows(int camX, int camY) { return gate != OPEN && scene::onScreen(GX, GY, camX, camY); }

// The toad's frame now (TOAD_FRAME's index).
static uint8_t frame() {
    uint32_t t = k.t;
    switch (k.st) {
    case ROAR: return (uint8_t)(TOAD_IDLE + t / 4 % TOAD_IDLE_N);       // a croak: the throat pumping
    case IDLE: return (uint8_t)((k.sits % 3 == 2 ? TOAD_BLINK : TOAD_IDLE) + t / 8 % TOAD_IDLE_N);
    case HOP: {                     // the crouch (f0-f1), airborne (f2-f4), down (f5-f7)
        uint32_t c = crouchT(), a = airT();
        if (t < c) return (uint8_t)(TOAD_HOP + (2 * t >= c));
        if (t < c + a) return (uint8_t)(TOAD_HOP + 2 + 3 * (t - c) / a);
        return (uint8_t)(TOAD_HOP + 5 + upTo((t - c - a) / 6, 2));
    }
    case TELL: return (uint8_t)(TOAD_TONGUE + (t >= OPEN_T));          // f1: the mouth open, held
    case LASH: return (uint8_t)(TOAD_TONGUE + 2 + upTo(t / 6, 5));     // f2-f4 out, f5-f7 back
    case STUN: return TOAD_HURT + 2;                                    // agape, eyes shut
    case HURT: return (uint8_t)(TOAD_HURT + upTo(t / 6, TOAD_HURT_N - 1));
    case DIE: return (uint8_t)(TOAD_DEATH + upTo(t / 8, TOAD_DEATH_N - 1));
    }
    return TOAD_IDLE;               // rising
}

void plan(int camX, int camY) {
    if (gateShows(camX, camY)) spool::want(CLIP_GATE_PART, gateFrame(), spool::PRIO_CRITTER);
}

void list(scene::List &l, int camX, int camY) {
    if (fighting() && k.gx > camX - 48 && k.gx < camX + GFX_W + 48 && k.gy > camY - 4 && k.gy < camY + 184)
        l.add(k.gx, k.gy, scene::ROT, 0);
    if (gateShows(camX, camY)) l.add(GX, GY, scene::ROT, 1);
}

// A broad shadow round feet at screen (sx, sy): an ellipse hw px each side, 5 rows.
static void blob(int sx, int sy, int hw) {
    static const uint8_t ROW[5] = {5, 7, 8, 7, 5};      // eighths of hw
    for (int r = 0; r < 5; r++) {
        int w = hw * ROW[r] / 8;
        gfx_remapRect(sx - w, sy - 2 + r, 2 * w, 1, REMAP_DARK);
    }
#ifdef CHSIM
    scene::simCost += 30 + (uint32_t)hw * 2;            // (Scene.h's shadow model: ~0.26 us a px)
#endif
}

void shadows(int camX, int camY, bool over) {
    if (!fighting() || k.st == RISE || (k.st == DIE && k.t > DIE_HOLD)) return;
    if (!over) blob(k.gx - camX, k.gy - camY + hud::ROWS, 24 - k.lift);
    else if (k.st == HOP && k.t < crouchT() + airT()) {    // where it will come down: a shadow growing, over him
        int hw = (int)(4 + 20 * k.t / (crouchT() + airT())), sx = k.tx - camX, sy = k.ty - camY + hud::ROWS;
        blob(sx, sy, hw);
        blob(sx, sy, hw / 2);       // (darker in the middle)
    }
}

// A frame's blocks as they land: its rows [r, r1) blitted, a block's worth at a time.
struct Band { int x, y, ww, rpb, r, r1; uint8_t flags, level; };

static void onBand(const uint8_t *b, void *ctx) {
    Band &c = *(Band *)ctx;
    int n = c.r1 - c.r;
    if (n > c.rpb) n = c.rpb;
    gfx_blit4k(b, c.x, c.y + c.r, c.ww, n, c.flags, nullptr, c.level);
    c.r += n;
}

bool draw(uint8_t i, int camX, int camY) {
    if (i) {                        // the gate: its left half, and mirrored the right
        const uint8_t *p = scene::frameOr(CLIP_GATE_PART, gateFrame(), gl, gf);
        if (p) {
            scene::blit(p, GX - camX, GY - camY + hud::ROWS, 1);
            scene::blit(p, GX - camX, GY - camY + hud::ROWS, -1);
        }
        return true;
    }
    const ToadFrame &fr = TOAD_FRAME[frame()];
    int sx = k.gx - camX, sy = k.gy - camY + hud::ROWS - k.lift;
    Band c;
    c.ww = fr.wWords;
    c.rpb = (int)toadRows(fr);
    c.x = k.face < 0 ? sx - fr.ox - c.ww * 8 : sx + fr.ox;
    c.y = sy + fr.oy;
    // how much of it shows (gfx_blit4k's dither level): rising out of the murk, fading dead
    uint32_t lv = k.st == RISE ? 2 + k.t * 14u / RISE_T
                  : k.st == DIE && k.t > DIE_HOLD ? 16 - (k.t - DIE_HOLD) * 16u / (DIE_END - DIE_HOLD) : 16;
    c.level = (uint8_t)lv;
    c.flags = (uint8_t)((k.face < 0 ? GFX_B4_FLIPH : 0) | (lv < 16 ? GFX_B4_DITHER : 0));
    // only the blocks holding rows that show on the playfield
    int r0 = c.y < hud::ROWS ? hud::ROWS - c.y : 0, r1 = GFX_H - c.y;
    if (r1 > fr.h) r1 = fr.h;
    if (r0 >= r1 || c.x >= GFX_W || c.x + c.ww * 8 <= 0 || !lv) return true;
    if (scene::rec) {               // (Play's sprites in the world's stream: the toad streams, so after it)
        scene::recReal = true;
        scene::grow(c.x, c.y + r0, c.ww * 8, r1 - r0);
        return true;
    }
    uint32_t b0 = (uint32_t)(r0 / c.rpb), n = (uint32_t)((r1 - 1) / c.rpb) - b0 + 1;
    c.r = (int)b0 * c.rpb;
    c.r1 = r1;
#ifdef CHSIM
    // the drawing as the board would take it (Scene.h's blit model): each block's blit hides under
    // the next one's DMA but for what it runs over, and the last one's
    for (uint32_t j = 0; j < n; j++) {
        int rows = r1 - (int)(b0 + j) * c.rpb;
        uint32_t us = scene::blitCost((uint32_t)((rows > c.rpb ? c.rpb : rows) * c.ww), c.flags);
        scene::simCost += j + 1 < n ? (us > pace::BLK_US ? us - pace::BLK_US : 0) : us;
    }
#endif
    // (hit: its red copy, Boss.h's TOAD_RED on: a REMAP_RED blit of this size would run ~1.1 ms past the DMA)
    if (!card::stream(CARD_TOAD_FIRST + fr.block + (k.flash ? TOAD_RED : 0) + b0, n, onBand, &c)) return false;
    if (k.st == TELL && k.t >= OPEN_T && !(k.t & 4))    // a glint deep in its gaping throat
        gfx_fillRect(k.face > 0 ? sx + 10 : sx - 12, sy - 32, 2, 2, C_BONE);
    return true;
}

void lean(int &dx, int &dy) {
    dx = fighting() ? clamp((k.gx - player::x()) * 5 / 8, 48) : 0;
    dy = fighting() ? clamp((k.gy - 24 - player::y()) / 2, 28) : 0;
}

uint8_t bar() { return fighting() && k.st != RISE ? (uint8_t)(1 + hud::BAR_W * k.hp / HP) : 0; }

const char *banner() { return k.st == ROAR ? "OLD GULLET" : nullptr; }

// --- combat ---------------------------------------------------------------------------

bool hurtbox(player::Box &b) {
    if (k.st < ROAR || k.st >= DIE || airborne()) return false;
    b = {(int16_t)(k.x - BODY_HW), (int16_t)(k.y - BODY_H), (int16_t)(k.x + BODY_HW), (int16_t)(k.y + BODY_FRONT)};
    return true;
}

uint8_t hurt(uint8_t dmg, uint8_t swing) {
    if (k.swing == swing) return critters::MISSED;
    k.swing = swing;
    bool calm = !angry();
    k.hp = (uint8_t)(dmg >= k.hp ? 0 : k.hp - dmg);
    k.flash = FLASH_T;
    if (!k.hp) {
        enter(DIE);                 // (its rot raccoons fall with it: tick())
        sfx(Sfx::Roar);
        return critters::KILLED;
    }
    if (calm && angry()) {          // half its HP gone: it roars, and turns
        sfx(Sfx::Roar);
        camera::shake(30, 3);
        k.callT = 120;
    }
    if (k.st != STUN && ++k.hits >= 3) {   // every third blow staggers it
        k.hits = 0;
        enter(HURT);
    }
    return critters::HURT;
}

bool attack(player::Box &b, uint8_t &halves, int16_t &fromX, bool &any) {
    fromX = k.x;
    any = k.st == HOP;
    if (k.landed) return false;
    if (any) {                      // the landing: a ring of dust round it (no guard stops it)
        if (k.t != crouchT() + airT()) return false;
        b = {(int16_t)(k.x - RING), (int16_t)(k.y - 12), (int16_t)(k.x + RING), (int16_t)(k.y - 6)};
        halves = 2;
        return true;
    }
    if (k.st != LASH || k.t >= LASH_HURTS) return false;
    b.x0 = (int16_t)(k.face > 0 ? k.x + TONGUE_X0 : k.x - TONGUE_X1);
    b.x1 = (int16_t)(b.x0 + TONGUE_X1 - TONGUE_X0);
    b.y0 = (int16_t)(k.y - TONGUE_Y0);
    b.y1 = (int16_t)(k.y - TONGUE_Y1);
    halves = 3;
    return true;
}

void landed(uint8_t outcome) {
    k.landed = true;
    if (outcome == player::HIT_PARRIED) enter(STUN);
    else if (outcome == player::HIT_BLOCKED && k.st == LASH) k.t = LASH_HURTS;     // the tongue snaps back
}

#if CHGAME_DEBUG
uint8_t hp() { return k.hp; }

char *report(char *p) {
    static const char NAME[][5] = {"WAIT", "RISE", "ROAR", "IDLE", "HOP", "TELL", "LASH", "STUN", "HURT", "DIE",
                                   "GONE"};
    p = fmtStr(p, " | TOAD ");
    p = fmtStr(p, NAME[k.st]);
    p = fmtStr(p, ":hp");
    p = fmtInt(p, k.hp);
    p = fmtStr(p, "@");
    p = fmtInt(p, k.gx);
    p = fmtStr(p, ",");
    p = fmtInt(p, k.gy);
    p = fmtStr(p, " gate");
    return fmtInt(p, gate);
}
#endif

}  // namespace rot
