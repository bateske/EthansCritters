// Ethan's Critters build switches.
//
// Built like the CHGame library's games: board package 0.3.0+, Peripherals
// "Game", Optimize "Smallest + LTO", USB "Upload only" for release
// (`ec build`; `ec build --debug` keeps USB Serial for the debug protocol).
#pragma once

#define ECRT_VERSION    "1.0"

// The CHGame library's switches: CHGAME_DEBUG (the serial debug protocol;
// always on in the simulator, on the board only in a debug build).
#include <chgame/Config.h>

#define ECRT_FPS        60

// A debug build on the board carries the protocol and USB Serial (~2.7 KB of
// flash, a few hundred bytes of RAM): ECRT_LEAN trims what a perf run does
// not need, ~4.1 KB (M6a): the sounds and the title's tune with CHGame's
// sequencer (Sounds.h: silent, 2.0 KB), the core's delayMicroseconds() and
// the 64-bit division it brings (Play's J waits on micros(): 1.3 KB), A's
// actor report (A still answers: 0.65 KB), the card screen's speckled mud;
// since M7a also ~0.6 KB more: the SELECT stats overlay (Z prints the
// same), the H command (no device script uses it), a parried stick flying
// back at the critters (it breaks), the card screen's hint lines; since
// M7b 0.96 KB more, M7b's extras (ECRT_EXTRAS below): the pause map (START
// does nothing) and its fog (not kept), the title's best time and its
// CONTINUE / NEW GAME choice (A carries on), the win clip's times (the
// title loop and the death and win clips themselves stay); since M8d ~0.7
// KB more, so the debug image keeps a save page (<= 50,688 B: the warm-up's
// mark is saved): the HUD's compass, the card screen's board (its words
// stay), the warm-up's bar (its words stay), the boot screens' name and
// READING THE CARD, Z's loaded= and drawMisses=, A's stub's words. Since
// the second occluder fix (after M8) the lean image no longer fits a save
// page (50,920 B): a debug build on the board saves nothing (CHGame's save
// turns itself off), so it reads the card through (the warm-up, ~21 s) at
// every boot, not once per card copy. The release build keeps both pages.
// Since the playtest round (the sword's swipe arc) a lean build also leaves
// out the region names on the way in, Z's lost= and C 4's stick carrier.
// The simulator keeps everything (its scripts check all of it).
#if CHGAME_DEBUG && !defined(CHSIM)
#define ECRT_LEAN       1
#else
#define ECRT_LEAN       0
#endif

// M7b's extras: the pause map and its fog, the title's best time and its
// CONTINUE / NEW GAME choice, the win clip's times. Not in a lean debug
// build (config above): its image must stay small enough to upload.
#ifndef ECRT_EXTRAS
#define ECRT_EXTRAS     (!ECRT_LEAN)
#endif

// The sprite frame cache (src/engine/Spool.cpp), bytes of RAM: the slack of
// the RAM budget, so the debug build on the board gets less.
#ifndef ECRT_ARENA
#if ECRT_LEAN
#define ECRT_ARENA      2560
#else
#define ECRT_ARENA      3072
#endif
#endif

// The sprite lab (B on the title, src/ui/Lab.h), a development screen: in
// the simulator only (its scripts run there), unless asked for. It costs
// ~3.5 KB of flash and ~190 B of RAM that the game needs on the board.
#ifndef ECRT_LAB
#if CHGAME_DEBUG && !ECRT_LEAN
#define ECRT_LAB        1
#else
#define ECRT_LAB        0
#endif
#endif

// The sprites drawn while the world streams (M8, Play.cpp): ECRT_HIDE_US is
// how long each block's wait draws after its copy (the board: a block takes
// ~222 us, the copy 26-51 of it). ECRT_HIDE_VERIFY (the simulator only, for
// checking the order: CHSIM_FLAGS=-DECRT_HIDE_VERIFY=1) charges each draw
// made in a wait at its place in the old order and in full, so the frames'
// timing, and with it every script's pictures, must be exactly as without
// the hidden drawing.
#ifndef ECRT_HIDE_US
#define ECRT_HIDE_US    150
#endif
#ifndef ECRT_HIDE_VERIFY
#define ECRT_HIDE_VERIFY 0
#endif

// The simulator's drawing model (src/game/Scene.cpp's blitCost): CHGfx P4's
// gfx_blit4k as measured on the board (0), or P5's estimate (1, M8b; not
// yet measured: CHSIM_FLAGS=-DECRT_SIM_P5=1).
#ifndef ECRT_SIM_P5
#define ECRT_SIM_P5     0
#endif
