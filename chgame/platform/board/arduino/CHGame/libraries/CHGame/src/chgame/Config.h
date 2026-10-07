// The CHGame library's build switches.
//
// The library is compiled on its own, so it cannot see a sketch's config.h:
// set these with build.extra_flags (chgame build --debug passes
// -DCHGAME_DEBUG=1), and read them in the sketch from here.
//
//   CHGAME_DEBUG    the serial debug protocol (chgame/Debug.h): screenshots,
//                   input injection, lockstep, perf. Always on in the
//                   simulator, which is driven through it; off on the board
//                   unless asked for (it costs ~2 KB and needs USB Serial).
//   CHGAME_PROFILE  the section profiler (dbg::prof and the T command).
#pragma once

#ifndef CHGAME_DEBUG
#ifdef CHSIM
#define CHGAME_DEBUG 1
#else
#define CHGAME_DEBUG 0
#endif
#endif

#ifndef CHGAME_PROFILE
#define CHGAME_PROFILE 0
#endif
