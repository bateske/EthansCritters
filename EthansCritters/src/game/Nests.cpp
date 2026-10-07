// The nests. See Nests.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Bank.h"
#include "../assets/Palette.h"
#include "../assets/WorldData.h"
#include "../engine/Camera.h"
#include "../engine/Spool.h"
#include "../engine/Terrain.h"
#include "../../Sounds.h"
#include "Critter.h"
#include "Nests.h"
#include "Pickups.h"

namespace nests {

struct Nest { uint8_t hp, spawnT, hitT, swing; };
static Nest nest[NEST_COUNT];
static uint8_t done;                // cleared, a bit each
static uint8_t bannerT, bannerKind;
static bool due;

static const int SPAWN_R = 160;                 // px from the squire
static const uint8_t SPAWN_T = 180, BANNER_T = 90, STIR_T = 150, HIT_T = 10;    // (SPAWN_T: 3 s; it was 2, too many at a den)
// By kind (WorldData.h's NEST_*): the critter, how many it keeps alive.
static const uint8_t KIND[4] = {critters::RACCOON, critters::TURTLE, critters::BEAVER, critters::CHAMELEON};
static const uint8_t KEEP[4] = {3, 3, 2, 2};     // (a damaged lodge sends a carrier on top)

// A nest's footprint by kind (WorldData.h's NEST_*): half its width, the
// height of its solid base and of what the sword can hit, px up from its foot.
struct Shape { uint8_t hw, solid, tall; };
static const Shape SHAPE[4] = {{14, 9, 22}, {13, 6, 12}, {17, 9, 20}, {13, 8, 24}};

void reset(uint8_t cleared) {
    done = cleared;
    for (uint32_t i = 0; i < NEST_COUNT; i++) {
        nest[i].hp = (cleared >> i) & 1 ? 0 : HP;
        nest[i].spawnT = nest[i].hitT = 0;
        nest[i].swing = 0xFF;
    }
    bannerT = 0;
    due = false;
}

void heal() {
    for (auto &n : nest) {
        if (n.hp) n.hp = HP;
        n.swing = 0xFF;             // the squire's swings count from 0 again: his first must not be taken for the last
    }
}

uint8_t cleared() { return done; }
bool allCleared() { return done == (1u << NEST_COUNT) - 1; }
uint8_t hp(uint8_t i) { return nest[i].hp; }

ECRT_OUTLINE bool blocks(int x0, int y0, int x1, int y1) {
    for (uint32_t i = 0; i < NEST_COUNT; i++) {
        if (!nest[i].hp) continue;
        const WorldSpot &s = NESTS[i];
        const Shape &h = SHAPE[s.kind];
        if (x1 > s.x - h.hw && x0 < s.x + h.hw && y1 > s.y - h.solid && y0 < s.y) return true;
    }
    return false;
}

// Somewhere for nest i's next critter to appear: in front of a den, round
// the clutch, in the water by a lodge (a carrier, far: out on the pond, at
// least 48 px off); on ground it may stand on, clear of the nest. False: none found.
ECRT_OUTLINE static bool spot(uint32_t i, int &x, int &y, bool far) {
    const WorldSpot &s = NESTS[i];
    for (uint32_t k = 0; k < 4; k++) {
        uint8_t ground = terrain::BLOCK_WALK;           // what it may not stand on (a beaver: only the solid)
        if (s.kind == NEST_TURTLE) {
            x = s.x + (int)critters::rnd(57) - 28;
            y = s.y + (critters::rnd(3) ? 4 + (int)critters::rnd(10) : -8 - (int)critters::rnd(8));
        } else if (s.kind == NEST_BEAVER) {
            int r = far ? 96 : 28, dx = (int)critters::rnd(2 * r + 1) - r, dy = (int)critters::rnd(r + 1) - r / 2;
            x = s.x + dx;
            y = s.y + 6 + dy;
            if ((far && dx * dx + dy * dy < 48 * 48) || (uint8_t)(terrain::at(x, y - 1) - T_SHALLOW) >= 2) continue;
            ground = terrain::tbit(T_SOLID);
        } else {
            x = s.x + (int)critters::rnd(25) - 12;
            y = s.y + 6 + (int)critters::rnd(4);
        }
        if (!terrain::hits(x - 4, y - 3, x + 4, y, ground) && !blocks(x - 4, y - 3, x + 4, y)) return true;
    }
    return false;
}

void tick() {
    if (bannerT) {
        bannerT--;
        if (bannerT == STIR_T && allCleared()) {    // the last nest's banner done: a rumble from The Rot
            sfx(Sfx::Roar);
            camera::shake(40, 2);
        }
    }
    int px = player::x(), py = player::y();
    bool squire = player::state() != player::DEAD;
    for (uint32_t i = 0; i < NEST_COUNT; i++) {
        Nest &n = nest[i];
        if (n.hitT) n.hitT--;
        uint8_t kind = NESTS[i].kind;
        if (!n.hp) continue;
        int dx = NESTS[i].x - px, dy = NESTS[i].y - py;
        if (!squire || dx * dx + dy * dy > SPAWN_R * SPAWN_R) {
            n.spawnT = 0;           // the first comes at once when he is back
            continue;
        }
        if (n.spawnT) {
            n.spawnT--;
            continue;
        }
        // a damaged lodge sends a beaver with a stick (one at a time) to mend it
        bool stick = kind == NEST_BEAVER && n.hp < HP && !critters::carrying((uint8_t)i);
        int x, y;
        if (critters::alive((uint8_t)i) < KEEP[kind] + stick && critters::alive(critters::NO_NEST) < CROWD &&
            spot(i, x, y, stick) && critters::spawn(KIND[kind], x, y, (uint8_t)i, stick))
            n.spawnT = SPAWN_T;
    }
}

// The frame for its HP: intact, battered (5 and under), wrecked (2 and under).
ECRT_OUTLINE static uint8_t stage(uint32_t i) { return nest[i].hp > 5 ? 0 : nest[i].hp > 2 ? 1 : 2; }

void plan(int camX, int camY) {
    for (uint32_t i = 0; i < NEST_COUNT; i++) {
        if (!nest[i].hp || !scene::onScreen(NESTS[i].x, NESTS[i].y, camX, camY)) continue;
        uint8_t c = (uint8_t)(CLIP_NEST_RACCOON + NESTS[i].kind), f = stage(i);
        spool::want(c, f, spool::PRIO_CRITTER);
        // the next state's frame, while a blow (a riposte's 2 HP) from it: wanted all the time
        // the nest shows, its 440 B would be held from a crowd's frames
        if (f < 2 && nest[i].hp <= (f ? 4 : 7)) spool::want(c, (uint8_t)(f + 1), spool::PRIO_CRITTER);
    }
}

// Sorted by the top of its solid base: whoever stands lower than that is
// beside it or in front (the base keeps them out), so is drawn over it.
void list(scene::List &l, int camX, int camY) {
    for (uint32_t i = 0; i < NEST_COUNT; i++)
        if (nest[i].hp && scene::onScreen(NESTS[i].x, NESTS[i].y, camX, camY))
            l.add(NESTS[i].x, NESTS[i].y - SHAPE[NESTS[i].kind].solid, scene::NEST, (uint8_t)i);
}

void draw(uint8_t i, int camX, int camY) {
    uint8_t c = (uint8_t)(CLIP_NEST_RACCOON + NESTS[i].kind), f = stage(i);
    const uint8_t *p = spool::peek(c, f);
    while (!p && f) p = spool::peek(c, --f);      // late: the state before
    if (!p) return;
    uint8_t h = nest[i].hitT;      // a blow: a white flash, then shaking
    scene::blit(p, NESTS[i].x - camX + (h ? (h & 2 ? 1 : -1) : 0), NESTS[i].y - camY + 8, 1,
                h >= HIT_T - 1 ? GFX_B4_SOLID | GFX_B4_INK(C_BONE) : 0);
}

bool repair(uint8_t i) {
    Nest &n = nest[i];
    if (!n.hp || n.hp >= HP) return false;
    n.hp++;
    return true;
}

bool hurtbox(uint8_t i, player::Box &b) {
    if (!nest[i].hp) return false;
    const WorldSpot &s = NESTS[i];
    const Shape &h = SHAPE[s.kind];
    b = {(int16_t)(s.x - h.hw - 1), (int16_t)(s.y - h.tall), (int16_t)(s.x + h.hw + 1), (int16_t)(s.y + 1)};
    return true;
}

uint8_t hurt(uint8_t i, uint8_t dmg, uint8_t swing) {
    Nest &n = nest[i];
    if (!n.hp || n.swing == swing) return MISSED;
    n.swing = swing;
    n.hp = (uint8_t)(dmg >= n.hp ? 0 : n.hp - dmg);
    n.hitT = HIT_T;
    if (n.hp) return HIT;
    done |= (uint8_t)(1u << i);
    bannerT = (uint8_t)(allCleared() ? BANNER_T + STIR_T : BANNER_T);
    bannerKind = NESTS[i].kind;
    due = true;
    pickups::drop(i);
    return CLEARED;
}

static const char *const BANNER[4] = {"DEN CLEARED", "CLUTCH CLEARED", "LODGE CLEARED", "BROOD CLEARED"};

const char *banner() {
    if (!bannerT) return nullptr;
    return allCleared() && bannerT <= STIR_T ? "THE ROT STIRS" : BANNER[bannerKind];
}

bool saveDue() {
    bool d = due;
    due = false;
    return d;
}

}  // namespace nests
