// The compiler's choices that keep the game small (flash is the limit after
// RAM: config.h, CLAUDE.md). Every hand-written source of the game includes
// this first, so it covers what it includes after it too (a pragma lasts to
// the end of the file); the generated src/assets/* hold only tables.
//
// On top of the build's -Os (M8a, each measured on the release image by
// taking it out alone): switch statements as compare chains, not jump
// tables in flash (-196 B: the critters', the toad's and the screens' state
// machines are switches of 5-25 cases); no hoisting of loop invariants
// (-212 B, nearly all of it in the logic's loops over the critters, which
// run under the flush with time to spare); no partly inlined copies of a
// function's head (-48 B). The card's callbacks and the blitters are the
// same code but for a few bytes (world::toFb identical; the frame cache's
// and the willows' callbacks +-6 B), and no hot loop has a switch.
#pragma once
#pragma GCC optimize("no-jump-tables", "no-partial-inlining", "no-move-loop-invariants")

// A small function called from many places that the link-time optimiser
// would copy into each: one copy and a call each is smaller (sfx(), the
// camera's shake and ease, the toad's hop times, the frame cache's key and
// insert, ...: -224 B in all; never on a hot path).
#define ECRT_OUTLINE __attribute__((noinline))
