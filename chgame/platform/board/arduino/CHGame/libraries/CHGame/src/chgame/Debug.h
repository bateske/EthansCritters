// The serial debug protocol (CHGAME_DEBUG builds only; see chgame/Config.h).
//
// Line-based ASCII over the USB CDC port, or the simulator's pipe. The
// simulator, tools/chsim/chdrive.py and tools/device.py run scripts through
// it, on the PC and on the board alike.
//
//   ?          -> the game's hello line, e.g. "CHCR 0.1"
//   S          -> "FB <frame> 8224\n" then 8192 framebuffer + 32 palette bytes
//   K <hex>    hold these buttons (ORed with the real ones); K alone releases
//   L1 / L0    lockstep on / off (on: the game only advances on N)
//   N <k>      run k frames, then answer "OK <frame>"
//   P          -> "PERF rnd=<us> max=<us> late=<n> frames=<n> stk=<b>"
//                 (+ fstk=<b> with a second frame stack, + host ns in the sim)
//   T          -> "PROF <slot>=<us> ..." section timings (CHGAME_PROFILE)
//   B          -> reboot into the CHGame bootloader
//   !          -> "FAULT none", or the crash before the last restart (the
//                 core's fault handler: A restarts after one) as
//                 "FAULT mcause=<hex> mepc=<hex> mtval=<hex> ra=<hex> sp=<hex>"
//   Q          (simulator) -> "CAL <ns> x5": host time of CHGfx's benchmark
//                 primitives, for estimated device times (chdrive's cal)
//   anything else goes to the game's hook: "OK" if it took it, else "ERR"
//
// In a release build every call here is an empty inline, so a sketch calls
// them without #if.
#pragma once
#include <stdint.h>
#include "Config.h"

namespace dbg {

#if CHGAME_DEBUG
// At the top of setup(): the hello line `?` answers (the game's id and
// version: a tool checks it is talking to the right game), and the stack
// painted for its high-water mark (P's stk=).
void begin(const char *hello);
void poll();                        // at the top of loop(): serial input
// Frame timing for P: around the logic ticks and the drawing.
void markUpdateStart();
void markRenderStart();
void markRenderEnd();
void print(const char *s);
void waitInput();                   // the simulator: block until input may have arrived
uint32_t parseNum(const char *&p, uint8_t base);   // skips leading spaces/commas
// The game's own commands: true if handled.
extern bool (*hook)(char cmd, const char *args);
// A game busy with something long (a search) can hold its commands: while
// busy(cmd) is true a command waits (answered HELD at once, OK/ERR once it
// has run) while N, S, P and the rest go on working.
// (The waiting command's buffer exists only in a game that calls this.)
static const uint8_t LINE = 100;    // the longest command line
void holdInto(bool (*busy)(char cmd), char *buf);
inline void holdWhile(bool (*busy)(char cmd)) { static char held[LINE]; holdInto(busy, held); }
// A second stack (frames drawn from inside a search) also reported by P.
void frameStack(uint32_t *lo, uint32_t *hi);
#if CHGAME_PROFILE
// prof(i) charges the time since the previous prof() to slot i (0-11); T
// reports and resets the averages per profStart().
void profStart();
void prof(uint8_t slot);
#else
inline void profStart() {}
inline void prof(uint8_t) {}
#endif
#else
inline void begin(const char *) {}
inline void poll() {}
inline void markUpdateStart() {}
inline void markRenderStart() {}
inline void markRenderEnd() {}
inline void print(const char *) {}
inline void waitInput() {}
inline void holdWhile(bool (*)(char)) {}
inline void frameStack(uint32_t *, uint32_t *) {}
inline void profStart() {}
inline void prof(uint8_t) {}
#endif

}  // namespace dbg
