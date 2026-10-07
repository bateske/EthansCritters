// The effects as step lists (a pitch, the pitch it sweeps to or 0, the
// milliseconds; pitches in 20 Hz and lengths in 2 ms steps), their
// priorities in SOUNDS, and the title's tune.
//
// How a piezo is made earthy: tones alternating every 2-4 ms read as noise
// (scratch, crunch, spray); a sweep that is not GLIDE restarts its tone
// every millisecond, which rasps (a hiss, a swish); a low sweep must GLIDE,
// or each restart cuts its cycle short and it never sounds; a dying ring is
// pings that shorten with longer gaps between.
#pragma GCC optimize("Os")
#include "src/Size.h"       // (first: Size.h)
#include "Sounds.h"
#if !ECRT_LEAN

// The sword through the air: a rasping rise and fall.
AUDIO_STEPS(SWISH)   = { AUDIO_STEP(1300, 3000, 18), AUDIO_STEP(3000, 1200, 26) };
// A blow landing: a click, then a low drop.
AUDIO_STEPS(THUD)    = { AUDIO_STEP(1500, 0, 2), AUDIO_STEP(480, 120, 56) };
// Steel on a shell: a clank, then a ring dying away in shorter pings.
AUDIO_STEPS(TINK)    = { AUDIO_STEP(4000, 0, 8), AUDIO_STEP(2600, 0, 4), AUDIO_STEP(4000, 0, 16), AUDIO_REST(6),
                         AUDIO_STEP(4000, 0, 8), AUDIO_REST(10), AUDIO_STEP(4000, 0, 4) };
// The squire hit: a rough grunt, then a yelp falling away.
AUDIO_STEPS(HURT)    = { AUDIO_STEP(1400, 0, 4), AUDIO_STEP(700, 0, 4), AUDIO_STEP(1300, 0, 4), AUDIO_STEP(650, 0, 4),
                         AUDIO_STEP(1800, 600, 120) };
// A raccoon: two bursts of scratchy chatter and a squeak.
AUDIO_STEPS(CHITTER) = { AUDIO_STEP(2500, 0, 4), AUDIO_STEP(1700, 0, 4), AUDIO_STEP(2700, 0, 4), AUDIO_STEP(1800, 0, 4),
                         AUDIO_STEP(2500, 0, 4), AUDIO_REST(26), AUDIO_STEP(2800, 0, 4), AUDIO_STEP(1900, 0, 4),
                         AUDIO_STEP(3000, 0, 4), AUDIO_STEP(2000, 0, 4), AUDIO_STEP(3200, 2400, 20) };
// A turtle: a spit of noise, then a rasp sliding down.
AUDIO_STEPS(HISS)    = { AUDIO_STEP(3800, 0, 2), AUDIO_STEP(2400, 0, 2), AUDIO_STEP(4200, 0, 2), AUDIO_STEP(2700, 0, 2),
                         AUDIO_STEP(4000, 1500, 200) };
// The chameleon's tongue: a wet flick out, the snap back, a drip.
AUDIO_STEPS(THWIP)   = { AUDIO_STEP(500, 3200, 34), AUDIO_STEP(2800, 800, 22), AUDIO_REST(8), AUDIO_STEP(1400, 900, 10) };
// A bite: two crunches (low tones with clicks between), the second harder.
AUDIO_STEPS(CHOMP)   = { AUDIO_STEP(900, 0, 2), AUDIO_STEP(320, 0, 6), AUDIO_STEP(700, 0, 2), AUDIO_STEP(260, 0, 8),
                         AUDIO_REST(40), AUDIO_STEP(1000, 0, 2), AUDIO_STEP(300, 0, 6), AUDIO_STEP(800, 0, 2),
                         AUDIO_STEP(240, 0, 12) };
// Into the water: a spray, then bubbles blooping up.
AUDIO_STEPS(SPLASH)  = { AUDIO_STEP(3400, 0, 2), AUDIO_STEP(1100, 0, 2), AUDIO_STEP(3000, 0, 2), AUDIO_STEP(900, 0, 2),
                         AUDIO_STEP(2600, 0, 2), AUDIO_STEP(800, 0, 4), AUDIO_STEP(2200, 0, 2), AUDIO_STEP(700, 0, 6),
                         AUDIO_STEP(600, 1300, 22), AUDIO_REST(14), AUDIO_STEP(700, 1500, 18), AUDIO_REST(20),
                         AUDIO_STEP(800, 1700, 14) };
// A mushroom eaten: a munch, then a warm chirp up (no coin's ding).
AUDIO_STEPS(PICKUP)  = { AUDIO_STEP(500, 0, 6), AUDIO_REST(20), AUDIO_STEP(1100, 1700, 50), AUDIO_STEP(1700, 2300, 40),
                         AUDIO_STEP(2300, 0, 30) };
// A blow on a nest: a woody knock, sticks crackling.
AUDIO_STEPS(NESTHIT) = { AUDIO_STEP(1300, 0, 2), AUDIO_STEP(560, 240, 36), AUDIO_STEP(2200, 0, 2), AUDIO_REST(6),
                         AUDIO_STEP(1800, 0, 2), AUDIO_REST(10), AUDIO_STEP(2600, 0, 2) };
// A nest falls: a crash rumbling down, then a heavy "dum, dum - DUM" up a fifth.
AUDIO_STEPS(NESTBREAK) = {
    AUDIO_STEP(3000, 0, 2), AUDIO_STEP(500, 0, 6), AUDIO_STEP(2600, 0, 2), AUDIO_STEP(420, 0, 8),
    AUDIO_STEP(2200, 0, 2), AUDIO_STEP(360, 0, 10), AUDIO_STEP(1800, 0, 2), AUDIO_STEP(300, 160, 60), AUDIO_REST(40),
    AUDIO_STEP(784, 0, 90), AUDIO_REST(30), AUDIO_STEP(784, 0, 90), AUDIO_REST(30),
    AUDIO_STEP(1175, 0, 120), AUDIO_STEP(1175, 1245, 200) };
// The giant toad: a deep growl swelling and sinking (a low tone and short
// higher grunts in turn, about 40 times a second).
AUDIO_STEPS(ROAR)    = {
    AUDIO_STEP(150, 190, 20), AUDIO_STEP(330, 0, 6), AUDIO_STEP(170, 210, 20), AUDIO_STEP(360, 0, 6),
    AUDIO_STEP(190, 240, 20), AUDIO_STEP(400, 0, 6), AUDIO_STEP(220, 260, 24), AUDIO_STEP(440, 0, 6),
    AUDIO_STEP(240, 280, 24), AUDIO_STEP(460, 0, 6), AUDIO_STEP(250, 270, 24), AUDIO_STEP(440, 0, 6),
    AUDIO_STEP(240, 220, 24), AUDIO_STEP(400, 0, 6), AUDIO_STEP(210, 180, 24), AUDIO_STEP(360, 0, 6),
    AUDIO_STEP(180, 150, 24), AUDIO_STEP(320, 0, 6), AUDIO_STEP(150, 110, 60) };
// The squire falls: a slow minor line down, E D C B C A, the last note sinking.
AUDIO_STEPS(DIRGE)   = { AUDIO_STEP(1319, 0, 300), AUDIO_REST(40), AUDIO_STEP(1175, 0, 300), AUDIO_REST(40),
                         AUDIO_STEP(1047, 0, 300), AUDIO_REST(40), AUDIO_STEP(988, 0, 240), AUDIO_STEP(1047, 0, 120),
                         AUDIO_STEP(880, 0, 420), AUDIO_STEP(880, 760, 400) };
// The swamp cleared: a few notes on a reed pipe, up and settling (D G A B, A G A, D).
AUDIO_STEPS(WIN)     = { AUDIO_STEP(1175, 0, 110), AUDIO_STEP(1568, 0, 110), AUDIO_STEP(1760, 0, 110),
                         AUDIO_STEP(1976, 0, 220), AUDIO_STEP(1760, 0, 110), AUDIO_STEP(1568, 0, 110),
                         AUDIO_STEP(1760, 0, 160), AUDIO_REST(40), AUDIO_STEP(2349, 0, 400) };
// A footstep: a soft low tick.
AUDIO_STEPS(STEP)    = { AUDIO_STEP(300, 0, 4), AUDIO_STEP(180, 0, 6) };

using audio::GLIDE;
using audio::SOFT;
const audio::Effect SOUNDS[(int)Sfx::COUNT] = {
    AUDIO_EFFECT(SWISH, 1 | SOFT),      AUDIO_EFFECT(THUD, 3 | GLIDE),     AUDIO_EFFECT(TINK, 5),
    AUDIO_EFFECT(HURT, 7 | GLIDE),      AUDIO_EFFECT(CHITTER, 2),          AUDIO_EFFECT(HISS, 3),
    AUDIO_EFFECT(THWIP, 3 | GLIDE),     AUDIO_EFFECT(CHOMP, 4),            AUDIO_EFFECT(SPLASH, 2 | GLIDE),
    AUDIO_EFFECT(PICKUP, 5 | GLIDE),    AUDIO_EFFECT(NESTHIT, 5 | GLIDE),  AUDIO_EFFECT(NESTBREAK, 8 | GLIDE),
    AUDIO_EFFECT(ROAR, 8 | GLIDE),      AUDIO_EFFECT(DIRGE, 10 | GLIDE),   AUDIO_EFFECT(WIN, 10),
    AUDIO_EFFECT(STEP, 0 | SOFT),
};

void soundsBegin() { audio::begin(SOUNDS, (uint8_t)Sfx::COUNT); }

ECRT_OUTLINE void sfx(Sfx s, int8_t semitones) {
    if (semitones > 0) audio::sfx((uint8_t)s, (uint8_t)semitones);
    else audio::sfx((uint8_t)s);
}

// The title's tune: a folk air in D dorian, (MIDI note, length in units of
// 115 ms) pairs, 0 a rest; each note let go 20 ms early. 42 notes, 84 bytes.
#define N(note, len) (uint8_t)(note), len
#define R(len) 0, len
enum : uint8_t { C6 = 84, D6 = 86, E6 = 88, F6 = 89, G6 = 91, A6 = 93, B6 = 95, C7 = 96, D7 = 98 };
static const uint8_t TUNE[] = {
    N(D6, 2), N(F6, 1), N(G6, 1), N(A6, 3), N(G6, 1),           N(F6, 2), N(E6, 1), N(D6, 1), N(E6, 2), N(C6, 2),
    N(D6, 2), N(F6, 1), N(G6, 1), N(A6, 2), N(C7, 2),           N(B6, 1), N(A6, 1), N(G6, 1), N(E6, 1), N(A6, 4),
    N(D7, 2), N(C7, 1), N(A6, 1), N(G6, 2), N(A6, 2),           N(F6, 1), N(G6, 1), N(A6, 1), N(F6, 1), N(E6, 2), N(C6, 2),
    N(D6, 2), N(E6, 1), N(F6, 1), N(G6, 1), N(E6, 1), N(C6, 2), N(D6, 6), R(2),
};
// (8 bytes: kept out of small data, which this core copies into SRAM)
#if defined(__riscv) && !defined(CHSIM)
__attribute__((section(".rodata.audio.TITLE_TUNE")))
#endif
static const audio::Melody TITLE_TUNE = { TUNE, sizeof TUNE / 2, 115, 20 };

void playSong(Song s, bool loop) {
    if (s == Song::Title) audio::melody(TITLE_TUNE, loop);
    else audio::stopMusic();
}

#endif  // !ECRT_LEAN
