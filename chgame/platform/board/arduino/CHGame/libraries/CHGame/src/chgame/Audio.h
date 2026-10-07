// Sound and the status LED: a small sequencer for the piezo.
//
// One pin plays one note at a time, driven by TIM1 channel 2 on PB10 and
// stepped by the core's 1 kHz SysTick hook, so no other timer is used. It
// plays two kinds of thing at once:
//
//   effects  short step lists (a pitch, the pitch it sweeps to, a length),
//            each with a priority: a new effect is refused while one of
//            higher priority sounds, and cuts off any other;
//   music    under the effects: a Playtune score (several voices;
//            tools/make_music.py writes them), rendered as a lead line or an
//            arpeggio, or a melody (one line of notes, easy to write by hand).
//
// A game lists its effects once, in the order of its own enum:
//
//     enum class Sfx : uint8_t { Cursor, Win, COUNT };
//     AUDIO_STEPS(CURSOR) = { AUDIO_STEP(2100, 0, 10) };
//     AUDIO_STEPS(WIN)    = { AUDIO_STEP(2093, 0, 60), AUDIO_STEP(4186, 0, 170) };
//     const audio::Effect SOUNDS[] = { AUDIO_EFFECT(CURSOR, 0), AUDIO_EFFECT(WIN, 3) };
//
//     audio::begin(SOUNDS, (uint8_t)Sfx::COUNT);
//     audio::sfx(Sfx::Win);
//
// The simulator is silent: the same interface with no hardware, which
// remembers the last effect asked for (audio::simLast) for scripts and
// tests. The game's tools/audio preview renders the real thing to WAV.
#pragma once
#include <stdint.h>

namespace audio {

// An effect step in three bytes: the pitch and the pitch it sweeps to (0:
// none) in 20 Hz units, and its length in 2 ms units; pitch 0 = a rest.
// 20 Hz is under half a percent of a piezo's 1-4 kHz: no ear hears it, and
// the tables are half the size. A step lasts at most 510 ms.
struct Step { uint8_t hz, endHz, ms; };
#define AUDIO_STEP(hz, end, ms) \
    { (uint8_t)(((hz) + 10) / 20), (uint8_t)(((end) + 10) / 20), (uint8_t)(((ms) + 1) / 2) }
#define AUDIO_REST(ms) { 0, 0, (uint8_t)(((ms) + 1) / 2) }
// An effect's steps, kept in flash. (A plain `static const` table of 8 bytes
// or less is "small data" to the compiler, and this core's link script
// copies small data into SRAM.)
#if defined(__riscv) && !defined(CHSIM)
#define AUDIO_STEPS(name) static const audio::Step name[] __attribute__((section(".rodata.audio." #name)))
#else
#define AUDIO_STEPS(name) static const audio::Step name[]
#endif

// An effect: its steps, and its priority (0-15) with these flags.
enum : uint8_t {
    SOFT  = 0x10,       // a narrow pulse: quieter than everything else (clocks, ticks)
    GLIDE = 0x20,       // sweeps change pitch at a cycle's end instead of restarting it
};
struct Effect { const Step *steps; uint8_t n, flags; };
#define AUDIO_EFFECT(steps, flags) { steps, (uint8_t)(sizeof(steps) / sizeof((steps)[0])), (uint8_t)(flags) }

// The LED pin and the timer; the effects table (in the game's enum order).
void begin(const Effect *effects, uint8_t count, bool on = true);
void setOn(bool on);                // off: silent, effects refused, music stopped in place
bool on();

// Effect `id` (an index into the table, or the game's enum). semitones
// raises the whole effect (0-12): a combo that climbs.
void sfx(uint8_t id);
void sfx(uint8_t id, uint8_t semitones);
template <class E> inline void sfx(E e) { sfx((uint8_t)e); }
template <class E> inline void sfx(E e, uint8_t semitones) { sfx((uint8_t)e, semitones); }
// One note made up on the spot (a typewriter, a click, a counter rolling):
// it cuts off effects of priority 0 and 1, is refused over anything higher,
// and is itself priority 0. (Never over any effect: if (!playing()) blip().)
void blip(uint16_t hz, uint16_t ms, bool soft = false);
// One note at a priority, like an effect.
void note(uint16_t hz, uint16_t ms, uint8_t priority);
bool playing();                     // an effect is sounding

// Music. ARPEGGIO gives a score's sounding voices 6 ms turns; LEAD plays
// its melody (channel 0) and lets the others fill only its real rests.
// The music code is linked in only by a game that calls music() or melody().
enum MusicMode : uint8_t { MUSIC_OFF, ARPEGGIO, LEAD };
void setMusic(uint8_t mode);        // default ARPEGGIO; MUSIC_OFF holds the tune where it is
void music(const uint8_t *score, bool loop = true);   // nullptr: stop

// A melody: (MIDI note, length) pairs - note 0 is a rest, a length counts
// units of unitMs - each note let go gapMs early so repeated notes stay
// apart. Middle C is 60; the piezo sings best from C6 (84) to C8 (108).
//
//     static const uint8_t TUNE[] = { 84,2, 88,2, 91,4, 0,2 };
//     static const audio::Melody TITLE = { TUNE, sizeof TUNE / 2, 105, 14 };
//     audio::melody(TITLE);
struct Melody { const uint8_t *notes; uint8_t count, unitMs, gapMs; };
void melody(const Melody &m, bool loop = true);       // replaces any score
void stopMusic();
void loopMusic(bool on);            // off: the score stops at its end instead of repeating
bool musicPlaying();                // false while sound or music is off

void update();                      // once per logic tick: the LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

#ifdef CHSIM
extern uint8_t simLast;             // the last effect played (0xFF: none yet)
#endif

}  // namespace audio
