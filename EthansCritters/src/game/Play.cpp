// The play screen. See Play.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../../config.h"
#include "../assets/CardIndex.h"
#include "../assets/Palette.h"
#include "../assets/WorldData.h"
#include "../engine/Camera.h"
#include "../engine/Card.h"
#include "../engine/Occluders.h"
#include "../engine/Pace.h"
#include "../engine/PathF.h"
#include "../engine/Spool.h"
#include "../engine/Terrain.h"
#include "../engine/World.h"
#include "../fx/Fx.h"
#include "../ui/Hud.h"
#include "../../Save.h"
#include "../../Sounds.h"
#include "Combat.h"
#include "Critter.h"
#include "Nests.h"
#include "Pickups.h"
#include "Player.h"
#include "Play.h"
#include "Rot.h"
#include "Scene.h"

namespace play {

static const uint8_t FEET_HW = 4, FEET_H = 4;      // the feet box, 8 x 4 px: Player's feet line x - 4 .. x + 3
static const uint8_t BANNER_TICKS = 120;           // a region's name shows this long on the way in
static const uint8_t WORDS_T = 60, GETUP_T = 170;  // after he falls: the death clip, then A stands him up
static const uint8_t WIN_T = 90;                   // the toad gone: the win clip (its times and A at 255)
// The pause map (tools/card/clips.py draws it): the world at 7/64 inside
// its frame, top-left at MAP_X, MAP_Y; a fog cell (64 world px) is 7 px.
static const int MAP_X = 8, MAP_Y = 24, MAP_CELL = 7;
// The fetch budget: what is kept back for the frame's other work varying
// (a command and a block of the card: Pace.h).
static const int32_t CMD_US = pace::CMD_US, BLK_US = pace::BLK_US, MARGIN_US = 300;

struct State {
    uint32_t seconds, best;         // played since the new game (saved ones too); the best clear time (seconds played at the win)
    uint32_t shown;                 // the clip on the screen (its first card block), 0: the swamp
    uint32_t clipAt;                // chgame.frameCount when it began
    uint16_t workUs;                // the last drawn frame's work after its fetch
    uint16_t card;                  // the card data read through once on this board (Save.h)
    uint8_t region, bannerT;
    uint8_t secT;                   // ticks to the next second played
    uint8_t deadT;                  // ticks since he fell (0: on his feet)
    uint8_t wonT;                   // ticks since the toad died (0: not yet; up to 255)
    uint8_t seen[24];               // the pause map's cells he has walked in (Save.h; a lean build keeps none)
    bool paused, saveDue;           // START: the map; the progress to store at the next drawn frame
    int8_t leanX, leanY;            // the camera's lean toward the toad now (px)
    uint8_t late;                   // A and B pressed during a hit-stop, for when it ends
    uint8_t stepT;                  // to the next footfall
    uint8_t lastSt;                 // his state last tick (a new swing swishes)
    int8_t lastFace;
    uint8_t moodAmt;                // the region's mood now: the palette mixed this far (of 256) ...
    bool moodRot;                   // ... toward The Rot's olive (else Willow Hollow's moss)
    bool overlay;                   // SELECT (debug builds, not lean ones): the frame stats
    uint16_t loadUs;                // J: extra work a drawn frame (debug)
};
static State g;
#ifdef CHSIM
// The simulator charges the card's time (CHSD_SIM_CMD_US / _BLK_US give the
// board's) but not the drawing's: that is modelled from the board's numbers
// (Scene.h's simCost, Fx.h's, the HUD and big text here) and charged to the
// frame (Pace.h), so the fetch budget, the pacing and Z's times are the
// board's, estimated. Z also prints the drawing's share.
struct SimEst { uint32_t frames, sumUs, maxUs, inWait, after; };   // (inWait, after: the draws made in the world's stream, and after it)
static SimEst est;
static const uint32_t SIM_HUD_US = 200, SIM_TEXT_US = 150;
#endif

// The regions' moods, eased in over a second on the way in: the palette's
// sixteen colours mixed toward a colour (amount of 256; pal::setFx, so
// pal::commit() rebuilds CHGfx's table only when a colour really changes):
// The Rot a dark olive (until its toad is beaten: Rot.h), Willow Hollow a
// little dimmer and greener (until its brood is cleared), the others none.
// The red screen of a hurt is gfx_setFade's (Combat.h) and the death's
// grey pal::setDesaturate(): both go over the mood. Going from one mood's
// region to the other's, the first eases out before the second comes in.
static const uint8_t ROT_MOOD = 64, HOLLOW_MOOD = 40;
static const uint16_t OLIVE = 0x330, MOSS = 0x243;       // RGB444

// A region's mood is lifted: all its nests cleared (The Rot has none: its toad beaten).
static bool lifted(uint8_t r) {
    if (r == R_ROT) return rot::beaten();
    for (uint32_t i = 0; i < NEST_COUNT; i++)
        if (nests::hp((uint8_t)i) && world::regionAt(NESTS[i].x, NESTS[i].y) == r) return false;
    return true;
}

// A logic tick of the mood (snap: at once, a new walk; the palette was just set).
static void mood(bool snap) {
    bool rot = g.region == R_ROT;
    uint32_t want = (rot || g.region == R_WILLOW_HOLLOW) && !lifted(g.region) ? (rot ? ROT_MOOD : HOLLOW_MOOD) : 0,
             a = g.moodAmt;
    if (rot != g.moodRot && a && !snap) want = 0;
    else g.moodRot = rot;
    if (a == want && !snap) return;
    a = snap ? want : a < want ? a + 1 : a - 1;
    g.moodAmt = (uint8_t)a;
    uint32_t to = g.moodRot ? OLIVE : MOSS;
    for (uint32_t i = 0; i < 16; i++) {
        int32_t c = PALETTE[i], m = 0;
        for (uint32_t sh = 0; sh < 12; sh += 4) {
            int32_t v = c >> sh & 15, w = to >> sh & 15;
            m |= (v + (((w - v) * (int32_t)a + 128) >> 8)) << sh;
        }
        pal::setFx((uint8_t)i, (uint16_t)m);
    }
}

// Walking and knockback over the world: solid ground and deep water stop
// the feet (sliding along, nudged round corners), the shallows slow them.
// Fighting the toad he stays inside the arena (its trees have pockets the
// camera, bound to the arena, did not show and the toad could not reach),
// within reach of its dust ring wherever it lands (Rot.cpp's hop()).
static void walk(int32_t &x, int32_t &y, int32_t dx, int32_t dy) {
    if (terrain::at((int)(x >> 4), (int)(y >> 4) - 1) == T_SHALLOW) {
        dx = dx * 5 / 8;
        dy = dy * 5 / 8;
    }
    terrain::move(x, y, dx, dy, FEET_HW, FEET_H, terrain::BLOCK_WALK);
    if (rot::fighting()) {
        const int32_t X0 = (ARENA.x0 + 8) << 4, X1 = (ARENA.x1 - 8) << 4, Y0 = (ARENA.y0 + 20) << 4;
        x = x < X0 ? X0 : x > X1 ? X1 : x;
        if (y < Y0) y = Y0;
    }
}

// Terrain.h's blocker: the standing nests, The Rot's gate and its toad.
static bool blocks(int x0, int y0, int x1, int y1) {
    return nests::blocks(x0, y0, x1, y1) || rot::blocks(x0, y0, x1, y1);
}

void boot() {
    uint8_t cleared = 0;
    if (const progress::Record *r = progress::load()) {
        cleared = r->cleared;
        rot::load(r->rot);
        g.seconds = r->seconds;
        g.best = r->best;
        g.card = r->card;
#if ECRT_EXTRAS
        memcpy(g.seen, r->seen, sizeof g.seen);
#endif
    }
    nests::reset(cleared);
}

static void store() {
    progress::Record &r = progress::edit();    // (zeroed)
    r.cleared = nests::cleared();
    r.rot = rot::flags();
    r.card = g.card;
    r.seconds = g.seconds;
    r.best = g.best;
#if ECRT_EXTRAS
    memcpy(r.seen, g.seen, sizeof g.seen);
#endif
    progress::store();
}

static const uint16_t WARM_MARK = (uint16_t)(CARD_HASH | 1);  // (never 0: none)
bool warmed() { return g.card == WARM_MARK; }
void markWarmed() {
    g.card = WARM_MARK;
    store();
}

#if ECRT_EXTRAS
void newGame() {
    nests::reset(0);
    rot::load(0);
    g.seconds = 0;
    memset(g.seen, 0, sizeof g.seen);
    g.saveDue = true;
}

uint32_t best() { return g.best; }
bool saved() { return nests::cleared(); }

char *mmss(char *p, uint32_t s) {          // (by hand: fmtInt inlined here was 16 B more)
    uint32_t m = s / 60;
    for (uint32_t t = m; t >= 10; t /= 10) p++;
    char *e = ++p;
    do *--e = (char)('0' + m % 10);
    while (m /= 10);
    *p++ = ':';
    *p++ = (char)('0' + s % 60 / 10);
    *p++ = (char)('0' + s % 10);
    *p = 0;
    return p;
}
#endif

// The squire on his feet at (x, y), the swamp as it starts (the nests
// cleared stay so): no critters, every mushroom there.
static void stand(int16_t x, int16_t y) {
    player::reset(x, y);
    critters::reset();
    rot::reset();
    camera::bound(0, 0, WORLD_W, WORLD_H);
    camera::snap((int32_t)x << 4, (int32_t)y << 4);
    fx::clear();
    combat::reset();
    pal::setDesaturate(0);
    g.deadT = g.wonT = 0;
    g.shown = 0;
    g.paused = false;
    g.leanX = g.leanY = 0;
    g.lastSt = player::IDLE;
    g.lastFace = 1;
    g.region = world::regionAt(x, y);
    g.bannerT = BANNER_TICKS;
    mood(true);
}

void enter(int16_t x, int16_t y) {
    pal::init(PALETTE);                     // the title's clip has its own
    player::setMover(walk);
    terrain::blocker = blocks;
    nests::reset(nests::cleared());
    pickups::reset();
    fx::reseed();                           // the same sparks every walk (the scripts' frames repeat)
    stand(x, y);
    pace::reset();
    spool::clearTotals();
#if CHGAME_DEBUG
    occluders::clearTotals();
#endif
    g.workUs = 0;
}

// He stands up at the hut: the standing nests whole again, the mushrooms back.
static void respawn() {
    nests::heal();
    pickups::restore();
    stand(START_X, START_Y);
}

// Leaving for the title: the screen's own colours off.
static void leave() {
    gfx_setFade(0);
    pal::setDesaturate(0);
}

// His sounds and dust: a swish as a swing starts, a puff where he turns,
// footfalls (and splashes in the shallows).
static void squireFx() {
    player::State st = player::state();
    if ((st == player::THRUST || st == player::RIPOSTE) && st != g.lastSt) sfx(Sfx::Swish);
    g.lastSt = st;
    int x = player::x(), y = player::y(), sx = camera::screenX(x), sy = camera::screenY(y);
    bool wet = terrain::at(x, y - 1) == T_SHALLOW;
    int8_t f = player::facing();
    if (f != g.lastFace && st == player::WALK) fx::dust(sx, sy, 3, wet ? C_FOG : C_MUD);
    g.lastFace = f;
    if (st != player::WALK) {
        g.stepT = 12;
    } else if (++g.stepT >= 18) {
        g.stepT = 0;
        sfx(Sfx::Step);
        if (wet) fx::mud(sx, sy, 2, C_FOG);
    }
}

void won() {
    g.wonT = 1;
    sfx(Sfx::Win);
    if (!g.best || g.seconds < g.best) g.best = g.seconds;     // (saved with the toad: Rot.h's saveDue())
}

bool tick() {
    if (g.paused) {                 // the map: START again plays on
        g.paused = !chgame.justPressed(START_BUTTON);
        return true;
    }
    if (g.wonT) {                   // the toad beaten: A after the win clip, the title
        if (g.wonT < 255) g.wonT++;
        else if (chgame.justPressed(A_BUTTON)) {
            leave();
            return false;
        }
    }
#if ECRT_EXTRAS                     // (a lean debug build has no pause)
    else if (chgame.justPressed(START_BUTTON) && !g.deadT) {
        g.paused = g.saveDue = true;    // (the time played and the fog kept)
        gfx_setFade(0);                 // (a hurt's red, back when play is)
        return true;
    }
#endif
#if CHGAME_DEBUG && !ECRT_LEAN
    if (chgame.justPressed(SELECT_BUTTON)) g.overlay = !g.overlay;
#endif
    int ox = camera::x, oy = camera::y;
    if (combat::frozen()) {
        g.late |= (uint8_t)(chgame.justPressedMask() & (A_BUTTON | B_BUTTON));
    } else {
        if (g.deadT) {              // the colour drains away, the death clip; A stands him up
            if (g.deadT < 255) g.deadT++;
            pal::setDesaturate((uint8_t)(g.deadT < 24 || g.deadT >= WORDS_T ? 0 : (g.deadT - 24) / 2));
            if (g.deadT >= GETUP_T && chgame.justPressed(A_BUTTON)) {
                respawn();
                return true;
            }
        }
        player::tick(g.late);
        g.late = 0;
        squireFx();
        critters::tick(camera::x + GFX_W / 2, camera::y + 60);
        rot::tick();
        combat::tick();
        nests::tick();
        pickups::tick();
        if (!g.deadT && player::state() == player::DEAD) g.deadT = 1;
        if (!g.wonT && ++g.secT >= 60) {    // (the clock stops at the toad's death: the run's time)
            g.secT = 0;
            g.seconds++;
        }
#if ECRT_EXTRAS
        uint32_t c = (uint32_t)(player::y() >> 6) * 16 + (player::x() >> 6);   // the map's fog lifts where he walks
        g.seen[c >> 3] = (uint8_t)(g.seen[c >> 3] | 1 << (c & 7));
#endif
    }
    int8_t hx, hy;
    player::heading(hx, hy);
    int px = player::x(), py = player::y(), lx, ly;
    rot::lean(lx, ly);              // fighting the toad: the view eased toward it, inside the arena
    g.leanX = (int8_t)(g.leanX + 2 * ((lx > g.leanX) - (lx < g.leanX)));     // (2 px a tick)
    g.leanY = (int8_t)(g.leanY + 2 * ((ly > g.leanY) - (ly < g.leanY)));
    if (rot::fighting()) camera::bound(ARENA.x0, ARENA.y0, ARENA.x1, GATE.y1);
    else camera::bound(0, 0, WORLD_W, WORLD_H);
    camera::update((int32_t)(px + g.leanX) << 4, (int32_t)(py + g.leanY) << 4, hx, hy);
    fx::scroll(camera::x - ox, camera::y - oy);
    fx::update();
    combat::fxTick();
    uint8_t r = world::regionAt(px, py);
    if (r != g.region) {
        g.region = r;
        g.bannerT = BANNER_TICKS;
    } else if (g.bannerT) {
        g.bannerT--;
    }
    mood(false);
    return true;
}

// The draw list (Scene.h): plan()'s actors for the willows' choice, then
// the frame's items (draw()); one, static, so neither puts it on the stack
// (plan()'s sat in loop()'s frame under the whole frame's drawing, M8d).
static scene::List items;

// The actors on screen, in feet order (the squire and the critters; for
// the occluders' choice not the chameleons creeping as shimmers: a tree's
// pass is wasted on them).
static void actorList(scene::List &l, int cx, int cy, bool faint = true) {
    l.n = 0;
    l.add(player::x(), player::y(), scene::PLAYER, 0);
    critters::list(l, cx, cy, faint);
}

// The world's layer (World.h): the healed swamp once the toad is beaten,
// from the next walk on (round its body The Rot is still The Rot until the
// win clip covers it), and its ambient phase: a step every 16 ticks.
static uint32_t layer() { return rot::beaten() && !g.wonT; }
static const uint32_t PHASE_TICKS_LOG2 = 4;
static_assert(!(WORLD_TPH & (WORLD_TPH - 1)), "the phase is the clock's low bits: WORLD_TPH a power of two");

// A clip instead of the swamp: the map (paused), the death clip, the win clip.
static uint32_t clipWanted() {
    return g.paused ? CARD_PMAP_FIRST : g.deadT >= WORDS_T ? CARD_DEAD_FIRST : g.wonT >= WIN_T ? CARD_WINC_FIRST : 0;
}

void plan() {
    if (clipWanted()) return;           // (no frames wanted: no fetch)
    int cx = camera::x, cy = camera::y;
    player::plan();
    critters::plan(cx, cy);
    nests::plan(cx, cy);
    pickups::plan(cx, cy);
    rot::plan(cx, cy);
    scene::List &l = items;
    actorList(l, cx, cy, false);
    if (rot::fighting()) l.n = 0;       // (the arena has no willows, and the toad takes a pass's time)
    occluders::plan(l, cx, cy, layer());    // which of them the willows they are behind get redrawn over
}

// Octant (hud's compass: east, then clockwise) to the nearest nest still
// standing, or once all have fallen to The Rot's gate (hud::GATE added), or
// none when he stands at it. tan 22.5 deg ~ 5/12.
#if !ECRT_LEAN
static int8_t compass() {
    int32_t px = player::x(), py = player::y(), best = 0x7FFFFFFF, bx = 0, by = 0;
    uint8_t done = nests::cleared();
    for (uint32_t i = 0; i < NEST_COUNT; i++) {
        if ((done >> i) & 1) continue;
        int32_t dx = NESTS[i].x - px, dy = NESTS[i].y - py, d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            bx = dx;
            by = dy;
        }
    }
    int8_t gate = 0;
    if (nests::allCleared()) {      // then the gate, and once it is open the toad's pond
        if (rot::beaten()) return hud::NO_COMPASS;
        bool open = rot::flags() & rot::GATE_OPEN;
        bx = (open ? TOAD_X : (GATE.x0 + GATE.x1) / 2) - px;
        by = (open ? TOAD_Y : (GATE.y0 + GATE.y1) / 2) - py;
        best = bx * bx + by * by;
        gate = hud::GATE;
    }
    if (best < 24 * 24) return hud::NO_COMPASS;
    int32_t ax = bx < 0 ? -bx : bx, ay = by < 0 ? -by : by;
    if (ay * 12 < ax * 5) return (int8_t)((bx > 0 ? 0 : 4) + gate);
    if (ax * 12 < ay * 5) return (int8_t)((by > 0 ? 2 : 6) + gate);
    return (int8_t)((bx > 0 ? (by > 0 ? 1 : 7) : (by > 0 ? 3 : 5)) + gate);
}
#endif

#if CHGAME_DEBUG && !ECRT_LEAN
static char *num(char *p, char tag, int32_t v) {
    *p++ = tag;
    p = fmtInt(p, v);
    return fmtStr(p, " ");
}

// The last frame's numbers (this one is not done yet).
static void overlay() {
    const pace::Frame &f = pace::last();
    const spool::Stats &s = spool::stats();
    char buf[40], *p = buf;
    p = num(p, 'C', f.cmds);
    p = num(p, 'B', f.blocks);
    p = fmtInt(p, f.us);
    fmtStr(p, pace::half() ? "US 30" : "US 60");
    text35s(2, hud::ROWS + 2, buf, C_BONE, C_SOOT);
    p = buf;
    p = num(p, 'M', s.misses);
    p = num(p, 'D', s.deferred);
    p = num(p, 'E', s.entries);
    p = fmtInt(p, s.bytes);
    fmtStr(p, "B");
    text35s(2, hud::ROWS + 8, buf, C_MUD, C_SOOT);
}
#endif

// What the frame cache may read this frame: what the last frame's work
// leaves of the budget, one command at least (the squire's frame) unless
// the frame just before went over (see below). At 30 fps there is time to
// spare.
static void budget() {
    if (pace::half()) {
        spool::setBudget(2, 8);
        return;
    }
    int32_t slack = (int32_t)pace::BUDGET_US - MARGIN_US - g.workUs;
    uint8_t cmds = slack >= 2 * CMD_US + 3 * BLK_US ? 2 : 1;
    int32_t b = (slack - cmds * CMD_US) / BLK_US;
    // No room even for one block: this frame goes over if a frame misses. Right after a frame that went
    // over it waits (the actors show the frame they showed, a frame or two longer: at most one frame in
    // three goes over, and the pacer, 4 of 8, keeps 60 fps through a fight, the toad's: Rot.h); else the
    // command takes a block more, the next frame of the animation, which saves the next frame a command.
    if (b < 1 && pace::lately()) {
        spool::setBudget(0, 0);
        return;
    }
    spool::setBudget(cmds, (uint8_t)(b < 1 ? 2 : b > 8 ? 8 : b));
}

#if ECRT_EXTRAS
// Over the map: the fog (the cells he has not walked in, faded to sepia),
// a cross through each cleared nest (the map shows them standing), the
// brambles while the gate is shut, and the squire, blinking.
static void mapMarks() {
    for (uint32_t cy = 0; cy < 12; cy++)
        for (uint32_t cx = 0; cx < 16; cx++) {
            uint32_t c0 = cx;
            while (cx < 16 && !(g.seen[cy * 2 + (cx >> 3)] >> (cx & 7) & 1)) cx++;
            if (cx > c0)
                gfx_remapRect(MAP_X + (int)c0 * MAP_CELL, MAP_Y + (int)cy * MAP_CELL, (int)(cx - c0) * MAP_CELL,
                              MAP_CELL, REMAP_SEPIA);
        }
    uint8_t done = nests::cleared();
    for (uint32_t i = 0; i < NEST_COUNT; i++) {
        if (!(done >> i & 1)) continue;
        int x = MAP_X + NESTS[i].x * MAP_CELL / 64, y = MAP_Y + NESTS[i].y * MAP_CELL / 64;
        for (int d = -3; d <= 3; d++) {
            gfx_pixel(x + d, y + d, C_SOOT);
            gfx_pixel(x + d, y - d, C_SOOT);
        }
    }
    if (!(rot::flags() & rot::GATE_OPEN))
        gfx_fillRect(MAP_X + GATE.x0 * MAP_CELL / 64, MAP_Y + GATE.y1 * MAP_CELL / 64 - 2, 6, 2, C_RUST);
    if (chgame.frameCount & 16) {
        int x = MAP_X + player::x() * MAP_CELL / 64, y = MAP_Y + player::y() * MAP_CELL / 64;
        gfx_fillRect(x - 2, y - 3, 5, 5, C_SOOT);
        gfx_fillRect(x - 1, y - 2, 3, 3, C_BONE);
    }
}
#endif

// A clip instead of the swamp, at its frame rate from when it began (each
// has its own palette: the swamp's comes back with its mood after). Over
// it: A: GET UP once the death clip has played; the win clip's times; the
// map's marks. False: the card failed.
static bool clipScreen(uint32_t want) {
    if (want != g.shown) {
        if (!pathf::open(want)) return false;
        g.shown = want;
        g.clipAt = chgame.frameCount;
    }
    if (!pathf::play(chgame.frameCount - g.clipAt, false)) return false;
    if (g.deadT) {
        if (g.deadT >= GETUP_T && chgame.frameCount % 60 < 40) hud::centred(112, "A: GET UP", C_BONE);
    }
#if ECRT_EXTRAS
    else if (g.paused) {
        mapMarks();
    } else if (g.wonT == 255) {
        char buf[16];
        mmss(fmtStr(buf, "TIME "), g.seconds);
        hud::centred(104, buf, C_BONE);
        mmss(fmtStr(buf, "BEST "), g.best);
        hud::centred(112, buf, C_BONE);
    }
#endif
    return true;
}

// --- the sprites, drawn while the world streams (M8) ------------------------------------
//
// The world is one card stream of 16-17 column blocks (World.h), each copied
// into the framebuffer while the next arrives by DMA: ~222 us a block on the
// board, the copy 26-51 of it, the rest the CPU only waited. So the sprites
// are drawn in that time where they can be. In the first block's wait the
// frame's draw list is made (the items in feet order, each over its shadow,
// the shadows first: no shadow falls on a sprite), each item's blits
// recorded (scene::rec) rather than made, and each draw (a shadow, the
// toad's, an item) given the first wait it may go in:
//   - its screen words (8 px) must hold their final pixels: word j is done
//     with block j's copy (x % 8 == 0), else with block j + 1's;
//   - every draw before it in the list's order that touches the same cell
//     of the screen (8 x 16 px) must have gone first, so the picture is the
//     old one to the pixel (where nothing overlaps, the order cannot show);
//   - some items wait for after the stream, and so does whatever covers
//     them later in the list: the toad (it streams its own rows), an actor
//     a willow's pass is planned over (the pass streams the tree over him
//     right after he is drawn), a chameleon showing its glint (no blit), an
//     item whose blits do not fit the record.
// Each later wait then draws, in that order, what may go while it has time
// left (HIDE_US after the copy; the last block's wait has no DMA under it).
// After the stream: the rest of that queue, then the items held back with
// their passes, in the list's order. (The simulator charges its drawing
// model only for what runs past HIDE_US in a wait: Play.h's Z, DRAW est.)
static const uint32_t HIDE_US = ECRT_HIDE_US, MAX_PRIMS = 20;
static const uint8_t FAR = 0xFF;            // a cell's draws wait for after the stream
static const uint32_t BLOB = 31, BODY = 32; // the draws: k item k's shadow, BLOB the toad's, BODY + k item k

#if UINTPTR_MAX == 0xFFFFFFFFu
typedef uint8_t PrimAt;                     // (a Prim is 12 B on the board: 20 of them in a byte)
#else
typedef uint16_t PrimAt;                    // (the simulator's 64-bit pointers)
#endif
static_assert(sizeof(scene::Prim) * MAX_PRIMS <= (PrimAt)~0u, "Hidden::at holds byte offsets");

struct Hidden {
    scene::Prim prim[MAX_PRIMS];            // the items' blits, recorded
    PrimAt at[scene::MAX_ITEMS + 1];        // item k's: from byte at[k] of prim to at[k + 1] (none: drawn as it draws)
    uint8_t sw[scene::MAX_ITEMS];           // item k's shadow, half its width (0: none)
    uint16_t q[2 * scene::MAX_ITEMS + 1];   // the draws in the waits' order: (screen words needed << 8 | draw), sorted
    uint32_t held;                          // items drawn after the stream
    uint8_t nq, qi;
    bool made;
    int16_t cx, cy;
#if defined(CHSIM) && ECRT_HIDE_VERIFY
    uint16_t cost[64];                      // each queued draw's drawing, charged where it was (config.h)
#endif
};
static Hidden *hid;                         // the frame's, while its world streams

static const scene::Prim *prims(const Hidden &h, uint32_t k) {
    return (const scene::Prim *)((const uint8_t *)h.prim + h.at[k]);
}

// Item it, drawn (or recorded). False: the card failed (the toad).
static bool drawItem(const scene::Item &it, int cx, int cy) {
    switch (it.what) {
    case scene::PLAYER: player::draw(cx, cy - hud::ROWS); break;
    case scene::CRITTER: critters::draw(it.i, cx, cy); break;
    case scene::NEST: nests::draw(it.i, cx, cy); break;
    case scene::ROT: return rot::draw(it.i, cx, cy);
    default: pickups::draw(it.i, cx, cy); break;
    }
    return true;
}

// Draw d touches [x0, x1) x [y0, y1) of the screen: the screen words it needs
// final, at least those before it in its cells did (F, cell by cell; 16:
// after the stream), queued; or, held (or after a held one there), it is
// held too (true) and so is what comes after it in its cells (FAR).
static bool when(Hidden &h, uint8_t (*F)[GFX_W / 8], int x0, int y0, int x1, int y1, uint32_t d, bool hold) {
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > GFX_W ? GFX_W : x1;
    y1 = y1 > GFX_H ? GFX_H : y1;
    uint32_t e = hold ? FAR : 0;
    if (x0 < x1 && y0 < y1) {               // (off the screen: no cells)
        uint32_t w0 = (uint32_t)x0 >> 3, w1 = (uint32_t)(x1 - 1) >> 3, b0 = (uint32_t)y0 >> 4,
                 b1 = (uint32_t)(y1 - 1) >> 4;
        if (e < w1 + 1) e = w1 + 1;
        for (uint32_t b = b0; b <= b1; b++)
            for (uint32_t w = w0; w <= w1; w++)
                if (F[b][w] > e) e = F[b][w];
        for (uint32_t b = b0; b <= b1; b++)
            for (uint32_t w = w0; w <= w1; w++) F[b][w] = (uint8_t)e;
    }
    if (e == FAR) return true;
    uint32_t v = e << 8 | d, j = h.nq++;
    for (; j && h.q[j - 1] > v; j--) h.q[j] = h.q[j - 1];
    h.q[j] = (uint16_t)v;
    return false;
}

// Item k's box (scene::drawn) grown by its shadow (drawn before it, two
// rows at its feet and one under them), for the willow passes over it: a
// pass covered the sprite's box, and the shadow's last row showed through.
static void shadowed(const Hidden &h, uint32_t k) {
    int w = h.sw[k];
    if (w) scene::grow(items.it[k].x - h.cx - w, items.it[k].y - h.cy + hud::ROWS - 1, 2 * w, 3);
}

// The draw list, the items' blits recorded, each draw's first wait (above).
ECRT_OUTLINE static void make(Hidden &h) {     // (outlined: its cells off the stack while the waits draw)
    int cx = h.cx, cy = h.cy;
    scene::List &l = items;
    actorList(l, cx, cy);
    nests::list(l, cx, cy);
    pickups::list(l, cx, cy);
    rot::list(l, cx, cy);
    uint8_t F[GFX_H / 16][GFX_W / 8] = {};
    int bx = -GFX_W, by = 0;                // the toad's feet (none: off the screen)
    for (uint32_t k = 0; k < l.n; k++) {
        const scene::Item &it = l.it[k];
        int w = 0, sx = it.x - cx, sy = it.y - cy + hud::ROWS;
        if (it.what == scene::PLAYER) w = terrain::at(it.x, it.y - 1) == T_SHALLOW ? 0 : 6;
        else if (it.what == scene::CRITTER) w = critters::shadowWidth(it.i);
        else if (it.what == scene::PICKUP) w = 3;
        else if (it.what == scene::ROT && !it.i) {
            bx = sx;
            by = sy;
        }
        h.sw[k] = (uint8_t)w;
        if (w) when(h, F, sx - w, sy - 1, sx + w, sy + 2, k, false);
    }
    when(h, F, bx - 24, by - 2, bx + 24, by + 3, BLOB, false); // rot::shadows(): its broad one, if any
    scene::rec = h.prim;
    scene::recEnd = h.prim + MAX_PRIMS;
    for (uint32_t k = 0; k < l.n; k++) {
        const scene::Item &it = l.it[k];
        scene::Prim *a = scene::rec;
        h.at[k] = (PrimAt)((uint8_t *)a - (uint8_t *)h.prim);
        scene::clearDrawn();
        scene::recReal = false;
        drawItem(it, cx, cy);
        shadowed(h, k);                 // (what touches his shadow waits for his pass too)
        bool hold = it.what <= scene::CRITTER && occluders::planned(it.what, it.i);
        if (scene::recReal) {
            scene::rec = a;             // (none recorded: drawn after the stream as it draws)
            hold = true;
        }
        const scene::Rect &r = scene::drawn;
        if (when(h, F, r.x0, r.y0, r.x1, r.y1, BODY + k, hold)) h.held |= 1u << k;
    }
    h.at[l.n] = (PrimAt)((uint8_t *)scene::rec - (uint8_t *)h.prim);
    scene::rec = nullptr;
    h.made = true;
}

// Queued draw d; its drawing as the simulator models it (none on the board) returned, not charged.
static uint32_t make1(Hidden &h, uint32_t d) {
    uint32_t k = d & 31;
#ifdef CHSIM
    uint32_t s = scene::simCost;
#endif
    if (d == BLOB) rot::shadows(h.cx, h.cy, false);
    else if (d < BLOB) scene::shadow(items.it[k].x - h.cx, items.it[k].y - h.cy + hud::ROWS, h.sw[k]);
    else scene::play(prims(h, k), prims(h, k + 1));
#ifdef CHSIM
    s = scene::simCost - s;
    scene::simCost -= s;
#if ECRT_HIDE_VERIFY
    h.cost[d] = (uint16_t)s;
#endif
    return s;
#else
    return 0;
#endif
}

// World.h's idle: after a block's copy, while the next one comes.
static void idle(uint32_t words) {
    Hidden &h = *hid;
    uint32_t t0 = micros(), us = 0;         // us: the drawing's time as the simulator models it
    if (!h.made) make(h);                   // (the first block's wait)
    while (h.qi < h.nq && words < GFX_W / 8) {
        uint32_t v = h.q[h.qi];
        if (v >> 8 > words || micros() - t0 + us >= HIDE_US) break;
        h.qi++;
        us += make1(h, v & 63);
#ifdef CHSIM
        est.inWait++;
#endif
    }
#if defined(CHSIM) && !ECRT_HIDE_VERIFY
    if (us > HIDE_US) scene::simCost += us - HIDE_US;  // what ran past the block's DMA
#endif
}

// After the stream: the rest of the queue, then the items held back and the
// passes, in the list's order. False: the card failed.
static bool rest(Hidden &h) {
#ifdef CHSIM
    est.after += h.nq - h.qi;
    for (uint32_t k = 0; k < items.n; k++) est.after += h.held >> k & 1;
#endif
    while (h.qi < h.nq) {
        uint32_t s = make1(h, h.q[h.qi++] & 63);
#if defined(CHSIM) && !ECRT_HIDE_VERIFY
        scene::simCost += s;
#else
        (void)s;
#endif
    }
#if defined(CHSIM) && ECRT_HIDE_VERIFY
    for (uint32_t d = 0; d < BODY; d++) scene::simCost += h.cost[d];   // (the shadows and the toad's: before the items)
#endif
    const scene::List &l = items;
    for (uint32_t k = 0; k < l.n; k++) {
        const scene::Item &it = l.it[k];
        uint32_t bit = 1u << k;
#if defined(CHSIM) && ECRT_HIDE_VERIFY
        scene::simCost += h.cost[BODY + k];
#endif
        if (!(h.held & bit)) continue;
        if (h.at[k] == h.at[k + 1]) {   // (the toad, an item past the record; or one with no blits, as before)
            scene::clearDrawn();
            if (!drawItem(it, h.cx, h.cy)) return false;
        } else {
            scene::play(prims(h, k), prims(h, k + 1));
        }
        shadowed(h, k);
        if (it.what <= scene::CRITTER && !occluders::after(it.what, it.i, h.cx, h.cy)) return false;
    }
    return true;
}

bool draw() {
#ifdef CHSIM
    scene::simCost = 0;
#endif
    if (nests::saveDue() | rot::saveDue() | g.saveDue) store();     // before the card is touched: the page is built in the chunk scratch
    g.saveDue = false;
    uint32_t want = clipWanted();
    if (want) return clipScreen(want);
    if (g.shown) {                          // back from a clip: the swamp's colours, its mood
        g.shown = 0;
        mood(true);
    }
    budget();
    if (!spool::fetch()) return false;
    occluders::fetched();
    uint32_t t0 = micros();
#if CHGAME_DEBUG && !ECRT_LEAN
    if (g.loadUs) delayMicroseconds(g.loadUs);
#elif CHGAME_DEBUG
    // (the core's delayMicroseconds() brings a 64-bit division along: 1.3 KB)
    for (uint32_t t = micros(); micros() - t < g.loadUs;) {}
#endif
    Hidden h;
    h.nq = h.qi = 0;
    h.held = 0;
    h.made = false;
#if defined(CHSIM) && ECRT_HIDE_VERIFY
    memset(h.cost, 0, sizeof h.cost);
#endif
    h.cx = (int16_t)camera::x;
    h.cy = (int16_t)camera::y;
    hid = &h;
    bool ok = world::draw(camera::x, camera::y, layer(), chgame.frameCount >> PHASE_TICKS_LOG2 & (WORLD_TPH - 1), idle);
    hid = nullptr;
    if (!ok || !rest(h)) return false;      // (the stream's first wait made the list: it has 16-17 blocks)
    rot::shadows(camera::x, camera::y, true);   // where the toad will come down
    critters::drawShots(camera::x, camera::y);
    fx::drawParticles();
    fx::drawFloats();
    const char *b = nests::banner();
    int by = 13;                            // a nest fell: a board with the news, up under the HUD (clear of him)
    // the toad's name: just over his head (between them as he comes in from the south, the camera
    // leaning up toward the toad: low down it hid him), or low down with him high up
    int fy = player::y() - camera::y + hud::ROWS - 46;
    if (!b && (b = rot::banner())) by = fy >= 13 ? fy : 90;
    if (b) {
        int w = text35x2Width(b) + 12;
        hud::board((GFX_W - w) / 2, by, w, 22);
        gfx_hline((GFX_W - w) / 2 + 3, by + 2, w - 6, C_MUD);
        hud::centred2(by + 5, b);
    }
#if !ECRT_LEAN                       // (a lean debug build: no region names, for flash)
    if (g.bannerT && !g.wonT) hud::centred(GFX_H - 9, REGION_NAME[g.region], C_BONE);
#endif
#if ECRT_LEAN                        // (a lean debug build: no compass, M8d)
    hud::draw(player::hp(), player::MAX_HP, hud::NO_COMPASS, NEST_COUNT, nests::cleared(), rot::bar());
#else
    hud::draw(player::hp(), player::MAX_HP, compass(), NEST_COUNT, nests::cleared(), rot::bar());
#endif
#if CHGAME_DEBUG && !ECRT_LEAN
    if (g.overlay) overlay();
#endif
    uint32_t us = micros() - t0;
#ifdef CHSIM
    // the drawing as the board would take it: the fetch budget and the pacing see it
    uint32_t draw = scene::simCost + fx::simCost() + SIM_HUD_US + (b ? SIM_TEXT_US : 0);
    pace::charge(draw);
    us += draw;
    est.frames++;
    est.sumUs += draw;
    if (draw > est.maxUs) est.maxUs = draw;
#endif
    g.workUs = (uint16_t)(us > 0xFFFF ? 0xFFFF : us);
    return true;
}

#if CHGAME_DEBUG
static char *kv(char *p, const char *k, uint32_t v) {
    p = fmtStr(p, k);
    return fmtInt(p, (int32_t)v);
}

static void stats() {
    const pace::Totals &t = pace::totals();
    const spool::Totals &s = spool::totals();
    uint32_t f = t.frames ? t.frames : 1;
    char buf[280], *p = buf;
    p = kv(p, "PLAY x=", (uint32_t)player::x());
    p = kv(p, " y=", (uint32_t)player::y());
    p = kv(p, " frames=", t.frames);
    p = kv(p, " at30=", t.half);
    p = kv(p, " over=", t.over);
    p = kv(p, " switches=", t.switches);
    p = fmtStr(p, pace::half() ? " mode=30" : " mode=60");
    p = kv(p, " avgUs=", t.sumUs / f);
    p = kv(p, " maxUs=", t.maxUs);
    p = kv(p, " cmds=", t.cmds);
    p = kv(p, " blocks=", t.blocks);
#if !ECRT_LEAN                      // (a lean debug build: the simulator's card_faults reads it)
    p = kv(p, " lost=", card::lost);
#endif
    dbg::print(buf);                // (the rest on the same line: all of it may not fit buf)
    p = kv(buf, " | SPOOL fetches=", s.fetches);
    p = kv(p, " cmds=", s.cmds);
    p = kv(p, " blocks=", s.blocks);
    p = kv(p, " misses=", s.misses);
#if !ECRT_LEAN                      // (a lean debug build: M8d)
    p = kv(p, " loaded=", s.loaded);
#endif
    p = kv(p, " deferred=", s.deferred);
#if !ECRT_LEAN
    p = kv(p, " drawMisses=", s.drawMisses);
#endif
    p = kv(p, " maxUs=", s.maxUs);
    dbg::print(buf);
    const occluders::Totals &o = occluders::totals();
    p = kv(buf, " | OCCL passes=", o.passes);
    p = kv(p, " shed=", o.shed);
    p = kv(p, " blocks=", o.blocks);
    p = kv(p, " avgUs=", o.sumUs / (o.passes ? o.passes : 1));
    p = kv(p, " maxUs=", o.maxUs);
#ifdef CHSIM
    p = kv(p, " | DRAW est avgUs=", est.sumUs / (est.frames ? est.frames : 1));
    p = kv(p, " maxUs=", est.maxUs);
    p = kv(p, " | HIDE inWait=", est.inWait);
    p = kv(p, " after=", est.after);
#endif
    fmtStr(p, "\n");
    dbg::print(buf);
}

#if ECRT_LEAN
// (a lean debug build leaves the report out: ~0.65 KB; A still answers, so scripts run on)
static void actors() { dbg::print("ACTORS -\n"); }
#else
static void actors() {
    char buf[400], *p = critters::report(buf);     // (up to 350 B with the pool full)
    dbg::print(buf);
    p = fmtStr(buf, " | NESTS");
    for (uint32_t i = 0; i < NEST_COUNT; i++) {
        p = fmtStr(p, " ");
        p = fmtInt(p, nests::hp((uint8_t)i));
    }
    p = kv(p, " cleared=", nests::cleared());
    p = kv(p, " | SQUIRE hp=", player::hp());
    p = kv(p, " st=", player::state());
    p = kv(p, " | MUSH ", pickups::left());
    p = rot::report(p);
    fmtStr(p, "\n");
    dbg::print(buf);
}
#endif
#endif

bool hook(char cmd, const char *args) {
#if CHGAME_DEBUG
    switch (cmd) {
    case 'J':
        g.loadUs = (uint16_t)dbg::parseNum(args, 10);
        return true;
    case 'Z':
        while (*args == ' ') args++;
        if (*args == '0') {
            pace::clearTotals();
            spool::clearTotals();
            occluders::clearTotals();
            card::lost = 0;
#ifdef CHSIM
            est = SimEst();
#endif
        } else {
            stats();
        }
        return true;
    case 'C': {                     // C 0 312 660: a raccoon there (no nest; none if the pool is full)
        uint32_t k = dbg::parseNum(args, 10);
        int x = (int)dbg::parseNum(args, 10), y = (int)dbg::parseNum(args, 10);
        while (*args == ' ') args++;
        uint32_t n = *args ? dbg::parseNum(args, 10) : critters::NO_NEST;
        if (n >= NEST_COUNT) n = critters::NO_NEST;
#if ECRT_LEAN                       // (a lean debug build: no C 4 carrier; only beaver_fight.txt, simulator, uses it)
        critters::spawn((uint8_t)(k & 3), x, y, (uint8_t)n, false);
#else
        critters::spawn(k == 4 ? critters::BEAVER : (uint8_t)(k & 3), x, y, (uint8_t)n, k == 4);
#endif
        return true;
    }
#if !ECRT_LEAN                      // (a lean build: the device scripts do not use H)
    case 'H': {                     // H2: two half hearts from the front; H2b from behind
        uint32_t n = dbg::parseNum(args, 10);
        while (*args == ' ') args++;
        int16_t from = (int16_t)(player::x() + (*args == 'b' ? -10 : 10) * player::facing());
        if (player::hit(from, (uint8_t)(n ? n : 1)) == player::HIT_TAKEN) {
            sfx(player::hp() ? Sfx::Hurt : Sfx::Dirge);
            combat::redScreen();
        }
        return true;
    }
#endif
    case 'D': {                     // D0: nest 0 falls; D0 3: three HP off it
        uint32_t i = dbg::parseNum(args, 10), n = dbg::parseNum(args, 10);
        static uint8_t swing = 0x80;        // a swing of its own each time
        player::Box b;
        if (i == NEST_COUNT) {              // D 6 [n]: the toad down to n HP (none: it dies), while the sword could find it
            if (rot::hurtbox(b) && rot::hp() > n) rot::hurt((uint8_t)(rot::hp() - n), swing++);
            return true;
        }
        if (i >= NEST_COUNT) return false;
        if (nests::hurt((uint8_t)i, (uint8_t)(n ? n : nests::HP), swing++) == nests::CLEARED) sfx(Sfx::NestBreak);
        return true;
    }
    case 'A':
        actors();
        return true;
    case 'I':
        while (*args == ' ') args++;
        combat::god = *args == '1';
        return true;
    case 'O':
        while (*args == ' ') args++;
        occluders::off = *args == '0';
        return true;
    }
#else
    (void)cmd;
    (void)args;
#endif
    return false;
}

}  // namespace play
