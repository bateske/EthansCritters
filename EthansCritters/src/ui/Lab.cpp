// The sprite lab. See Lab.h.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Bank.h"
#include "../assets/CardIndex.h"
#include "../assets/Palette.h"
#include "../engine/Spool.h"
#include "../fx/Fx.h"
#include "../game/Player.h"
#include "../../Sounds.h"
#include "Hud.h"
#include "Lab.h"

namespace lab {

static const uint8_t SLOTS = 4;
static const int PARADE_Y = 120;            // the parade's feet line
enum Look : uint8_t { PLAIN, RED, FADE, MIRROR };   // slot i's look

struct Slot {
    uint8_t clip, frame;
    uint8_t lastClip, lastFrame;            // shown last (while the new one is on its way)
    uint16_t t;
};
struct State {
    Slot slot[SLOTS];
    uint8_t next;                           // the clip the next slot to change takes
    uint8_t newest;                         // the slot that changed last (its name is shown)
    uint8_t said, sayT;                     // the last hit's outcome, shown for sayT ticks
    uint8_t demoT, demo;                    // the next effect shown off, and when
    uint8_t swing, stepT;                   // the squire's last swing (one spark each), footfall timer
    player::State st;                       // his state last tick (a new thrust swishes)
    bool quiet;                             // SELECT: no numbers
};
static State lab;

// Lab ground the squire may stand on: the trodden earth above the bank
// (both ends of his feet, x - 4 and x + 3), each axis on its own.
static bool stand(int32_t qx, int32_t qy) {
    int x = (int)(qx >> 4), y = (int)(qy >> 4);
    return x - 4 >= 4 && x + 3 < 124 && y >= 30 && y < 93;
}

static void mover(int32_t &x, int32_t &y, int32_t dx, int32_t dy) {
    if (dx && stand(x + dx, y)) x += dx;
    if (dy && stand(x, y + dy)) y += dy;
}

static uint8_t critterClip(uint32_t n) {
    uint32_t first = BANK_SHEET_CLIP[SHEET_RACCOON], count = CLIP_COUNT - first;     // every sheet but the squire's
    return (uint8_t)(first + n % count);
}

static void take(uint8_t i) {
    Slot &s = lab.slot[i];
    s.clip = critterClip(lab.next++);
    s.t = 0;
    lab.newest = i;
}

void enter() {
    pal::init(PALETTE);
    spool::begin(CARD_BANK_FIRST);
    spool::setBudget(2, 8);
    player::reset(64, 70);
    player::setMover(mover);
    fx::clear();
    fx::reseed();                           // the same sparks every visit (the scripts' frames repeat)
    lab = State();
    for (uint8_t i = 0; i < SLOTS; i++) {
        take(i);
        lab.slot[i].lastClip = 0xFF;
        lab.slot[i].t = (uint16_t)(i * 23);     // so they do not all change at once
    }
}

// The effects, shown off in turn on the ground (and by F<n>).
static void demo(uint8_t n, int x, int y) {
    switch (n % 6) {
    case 0: fx::hitSpark(x, y); sfx(Sfx::Thud, (int8_t)(fx::rnd() % 3)); break;
    case 1: fx::dust(x, y, 6); sfx(Sfx::Step); break;
    case 2: fx::mud(x, y, 6, C_UMBER); sfx(Sfx::Chomp); break;     // (peat would vanish on the lab's peat)
    case 3: fx::mud(x, y, 6, C_FOG); sfx(Sfx::Splash); break;
    case 4: fx::chips(x, y, 8); sfx(Sfx::NestHit); break;
    default: fx::number(x, y - 8, "-1", C_RUST); sfx(Sfx::Pickup); break;
    }
}

// The squire's moves make their sounds and effects (the game's own will
// come from play; here they show what Sounds.h and Fx.h give it).
static void squireFx() {
    player::State st = player::state();
    if ((st == player::THRUST || st == player::RIPOSTE) && st != lab.st) sfx(Sfx::Swish);
    lab.st = st;
    player::Box b;
    uint8_t dmg, swing;
    if (player::blade(b, dmg, swing) && swing != lab.swing) {      // the tip strikes the air
        lab.swing = swing;
        fx::hitSpark(player::facing() > 0 ? b.x1 : b.x0, (b.y0 + b.y1) / 2, dmg > 1 ? C_OCHRE : C_BONE);
    }
    if (st == player::WALK && ++lab.stepT >= 18) {
        lab.stepT = 0;
        fx::dust(player::x(), player::y(), 2);
        sfx(Sfx::Step);
    }
}

bool tick() {
    if (chgame.justPressed(START_BUTTON)) return false;
    if (chgame.justPressed(SELECT_BUTTON)) lab.quiet = !lab.quiet;
    player::tick();
    squireFx();
    if (++lab.demoT >= 50) {
        lab.demoT = 0;
        demo(lab.demo++, 104, 56);
    }
    fx::update();
    if (lab.sayT) lab.sayT--;
    for (uint8_t i = 0; i < SLOTS; i++) {
        Slot &s = lab.slot[i];
        const BankClip &c = BANK_CLIPS[s.clip];
        uint32_t len = bankTicks(c) * c.frames, shown = len * 2 > 72 ? len * 2 : 72;   // two loops, at least 1.2 s
        if (++s.t >= shown) take(i);
        s.frame = (uint8_t)(s.t / bankTicks(BANK_CLIPS[s.clip]) % BANK_CLIPS[s.clip].frames);
    }
    return true;
}

void plan() {
    player::plan();
    for (uint8_t i = 0; i < SLOTS; i++) {
        const Slot &s = lab.slot[i];
        spool::want(s.clip, s.frame, spool::PRIO_CRITTER);
        spool::want(s.clip, (uint8_t)((s.frame + 1) % BANK_CLIPS[s.clip].frames), spool::PRIO_PREFETCH);
    }
}

// Trodden earth with scuffs, and the parade's darker bank.
static void ground() {
    gfx_fillRect(0, hud::ROWS, GFX_W, 92 - hud::ROWS, C_PEAT);
    gfx_dither(6, 20, 30, 10, C_UMBER, 0);
    gfx_dither(70, 14, 44, 8, C_UMBER, 1);
    gfx_dither(30, 52, 50, 14, C_UMBER, 0);
    gfx_dither(96, 70, 26, 12, C_BARK, 1);
    gfx_dither(8, 78, 34, 8, C_BARK, 0);
    uint32_t r = 0x9E3779B9u;                   // the same pebbles every frame
    for (uint32_t i = 0; i < 40; i++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        int x = (int)(r & 127), y = hud::ROWS + (int)((r >> 8) % 82);
        gfx_pixel(x, y, (r >> 20) & 1 ? C_BARK : C_UMBER);
    }
    gfx_fillRect(0, 92, GFX_W, GFX_H - 92, C_UMBER);
    gfx_dither(0, 92, GFX_W, 3, C_SOOT, 0);
    gfx_hline(0, 92, GFX_W, C_SOOT);
}

static void drawSlot(uint8_t i) {
    Slot &s = lab.slot[i];
    const uint8_t *p = spool::peek(s.clip, s.frame);
    if (p) {
        s.lastClip = s.clip;
        s.lastFrame = s.frame;
    } else if (s.lastClip != 0xFF) {
        p = spool::peek(s.lastClip, s.lastFrame);
    }
    if (!p) return;
    uint8_t flags = 0, level = 16;
    if (i == RED && (s.t >> 3) & 1) flags = GFX_B4_REMAP;
    if (i == FADE) {
        flags = GFX_B4_DITHER;
        uint32_t ph = (s.t >> 1) & 31;
        level = (uint8_t)(ph > 16 ? 32 - ph : ph);
    }
    if (i == MIRROR) flags = GFX_B4_FLIPH;
    int ww = p[0], ox = (int8_t)p[2], oy = (int8_t)p[3], x = 16 + 32 * i;
    gfx_blit4k(p + BANK_FRAME_HEADER, flags & GFX_B4_FLIPH ? x - ox - ww * 8 : x + ox, PARADE_Y + oy, ww, p[1],
               flags, REMAP_RED, level);
}

static void number(char *&p, char tag, int32_t v) {
    *p++ = tag;
    p = fmtInt(p, v);
    *p++ = ' ';
    *p = 0;
}

static void numbers() {
#if CHGAME_DEBUG                    // (the frame cache keeps its stats in debug builds only: Spool.h)
    const spool::Stats &st = spool::stats();
    char buf[40], *p = buf;
    number(p, 'C', st.cmds);
    number(p, 'B', st.blocks);
    number(p, 'M', st.misses);
    number(p, 'L', st.loaded);
    number(p, 'D', st.deferred);
    text35s(2, 10, buf, C_MUD, C_SOOT);
    p = buf;
    number(p, 'H', st.hits);
    number(p, 'E', st.entries);
    p = fmtInt(p, st.bytes);
    p = fmtStr(p, "B ");
    p = fmtInt(p, st.us);
    p = fmtStr(p, "US");
    text35s(2, 16, buf, C_MUD, C_SOOT);
#endif
}

static const char *const SAID[] = {"", "OUCH", "BLOCK", "PARRY!"};

bool draw() {
    bool ok = spool::fetch();
    ground();
    player::draw(0, 0);
    if (lab.sayT) {
        const char *w = SAID[lab.said];
        text35s(player::x() - text35Width(w) / 2, player::y() - 30, w, lab.said == player::HIT_PARRIED ? C_BONE : C_MUD,
                C_SOOT);
    }
    for (uint8_t i = 0; i < SLOTS; i++) {
        char n[4];
        fmtInt(n, lab.slot[i].clip);
        text35(16 + 32 * i - 3, 97, n, i == lab.newest ? C_BONE : C_BARK);
        drawSlot(i);
    }
    text35s(2, 86, BANK_CLIP_NAME[lab.slot[lab.newest].clip], C_BONE, C_SOOT);
    fx::drawParticles();
    fx::drawFloats();
    hud::draw(player::hp(), player::MAX_HP, (int8_t)(chgame.frameCount / 30 % 8), 6, 0x05);
    if (!lab.quiet) numbers();
    return ok;
}

bool hook(char cmd, const char *args) {
#if CHGAME_DEBUG
    if (cmd == 'H') {                       // H2: two half hearts from the front; H2b from behind
        uint32_t n = dbg::parseNum(args, 10);
        while (*args == ' ') args++;
        int16_t from = (int16_t)(player::x() + (*args == 'b' ? -10 : 10) * player::facing());
        lab.said = player::hit(from, (uint8_t)(n ? n : 1));
        lab.sayT = 40;
        if (lab.said == player::HIT_TAKEN) sfx(player::hp() ? Sfx::Hurt : Sfx::Dirge);
        else if (lab.said != player::HIT_IGNORED) sfx(Sfx::Tink);
        return true;
    }
    if (cmd == 'F') {                       // F3: effect 3 (spark, dust, mud, water, chips, number)
        demo((uint8_t)dbg::parseNum(args, 10), 96, 56);
        return true;
    }
    if (cmd == 'M') {                       // M20 0: the camera moved 20 px right (fx::scroll)
        int dx = (int)dbg::parseNum(args, 10);
        fx::scroll(dx, (int)dbg::parseNum(args, 10));
        return true;
    }
    if (cmd == 'C') {                       // C44: the parade from clip 44 on (P and S are the protocol's)
        uint32_t c = dbg::parseNum(args, 10);
        lab.next = (uint8_t)(c >= BANK_SHEET_CLIP[SHEET_RACCOON] ? c - BANK_SHEET_CLIP[SHEET_RACCOON] : 0);
        for (uint8_t i = 0; i < SLOTS; i++) take(i);
        return true;
    }
    if (cmd == 'R') {
        player::reset(64, 70);
        return true;
    }
    if (cmd == 'Z') {
        const spool::Totals &t = spool::totals();
        char buf[120], *p = buf;
        p = fmtStr(p, "SPOOL fetches=");  p = fmtInt(p, (int32_t)t.fetches);
        p = fmtStr(p, " cmds=");          p = fmtInt(p, (int32_t)t.cmds);
        p = fmtStr(p, " blocks=");        p = fmtInt(p, (int32_t)t.blocks);
        p = fmtStr(p, " misses=");        p = fmtInt(p, (int32_t)t.misses);
        p = fmtStr(p, " loaded=");        p = fmtInt(p, (int32_t)t.loaded);
        p = fmtStr(p, " deferred=");      p = fmtInt(p, (int32_t)t.deferred);
        p = fmtStr(p, " hits=");          p = fmtInt(p, (int32_t)t.hits);
        p = fmtStr(p, " drawMisses=");    p = fmtInt(p, (int32_t)t.drawMisses);
        p = fmtStr(p, " maxUs=");         p = fmtInt(p, (int32_t)t.maxUs);
        p = fmtStr(p, "\n");
        dbg::print(buf);
        return true;
    }
#else
    (void)cmd;
    (void)args;
#endif
    return false;
}

}  // namespace lab
