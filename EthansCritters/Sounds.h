// The game's sounds, for the CHGame library's piezo sequencer
// (chgame/Audio.h): earthy effects, not casino ones - low thuds, scratchy
// chitters (fast alternating steps read as noise on a piezo), a hiss that
// rasps downwards, wet blips and bubbles, a warm chirp for a mushroom, a
// heavy stinger when a nest falls, a toad's roar, a slow dirge - and the
// title's tune, a melody that loops under them.
//
// Each effect has a priority: one sounding refuses lower ones and is cut
// off by equal or higher ones, so a hurt is never lost to a footstep.
// `./ec audio out/audio` renders every effect and the tune to WAV.
#pragma once
#include "config.h"                // ECRT_LEAN: silent
#include <chgame/Audio.h>         // the CHGame library's sound engine

enum class Sfx : uint8_t {
    Swish,      // the sword cuts the air (soft)
    Thud,       // a blow lands on a critter (jitter it: sfx(Thud, rnd 0..3))
    Tink,       // steel on a shell, a guard or a parry
    Hurt,       // the squire is hit
    Chitter,    // a raccoon
    Hiss,       // a turtle
    Thwip,      // the chameleon's tongue
    Chomp,      // a bite
    Splash,     // into the water, a beaver diving
    Pickup,     // a mushroom eaten
    NestHit,    // a blow on a nest
    NestBreak,  // a nest falls
    Roar,       // the boss
    Dirge,      // the squire dies
    Win,        // the swamp is cleared
    Step,       // a footstep (soft, lowest: anything cuts it)
    COUNT
};

// The music: the title's tune, or none.
enum class Song : uint8_t { None, Title };

#if !ECRT_LEAN
// The effects, in Sfx order (audio::begin's table).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];

void soundsBegin();                                 // once, in setup()
// Effect s, raised by semitones (0-12; below 0 plays it as written).
void sfx(Sfx s, int8_t semitones = 0);
void playSong(Song s, bool loop = true);            // None stops it
#else
// A lean debug build (config.h) is silent: no tables, no sequencer.
inline void soundsBegin() {}
inline void sfx(Sfx, int8_t = 0) {}
inline void playSong(Song, bool = true) {}
#endif
