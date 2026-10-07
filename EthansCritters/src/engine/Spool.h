// The frame cache: sprite frames from the card's bank (section BANK, made by
// tools/assets/packbank.py) kept in a RAM arena of ECRT_ARENA bytes, ready
// for gfx_blit4k().
//
// A frame of the screen goes:
//   logic; then every actor says what it will draw: want(clip, f, prio)
//   gfx_wait()
//   fetch()      the frames wanted and not cached come from the card in at
//                most two commands (merged across gaps of up to 3 blocks)
//   draw: frame(clip, f) -> the frame, or nullptr if it did not come
//   gfx_flushAsync()
// A frame() that misses is wanted again at the next fetch(), so an actor
// that skips want() still appears, a frame late. peek() looks without
// wanting (an actor's fallback: the frame it showed before).
//
// fetch() never evicts a frame wanted for this screen; it evicts the least
// recently used of the others, and compacts the arena (a word loop) only
// when nothing else may go and the free space is there but in pieces. A
// miss that does not fit, or does not fit the budget, waits (deferred) and
// the actor shows what it showed before. The frames that share a block
// with a wanted one come along while there is room (the next frames of an
// animation, usually). Wants are served by priority: the player first.
//
// Frame layout (Bank.h): 8 B header {wWords, h, ox, oy, csum8, flags, w, 0},
// then h rows of wWords words. Pointers stay valid until the next fetch().
// Call fetch() only between gfx_wait() and the flush (Card.h's bus rule).
#pragma once
#include <stdint.h>
#include <chgame/Config.h>

namespace spool {

enum : uint8_t { PRIO_PLAYER, PRIO_ATTACK, PRIO_CRITTER, PRIO_PICKUP, PRIO_PREFETCH };

static const uint8_t MAX_ENTRIES = 32;      // frames cached at once
static const uint8_t MAX_WANTS = 24;        // distinct frames wanted per screen

// The bank starts at block `first` of the card file (CARD_BANK_FIRST).
// Drops everything (also after the card was opened again).
void begin(uint32_t first);

// Frame f (as played) of clip c will be drawn on this screen. PREFETCH is
// only read when a command goes near it anyway.
void want(uint8_t c, uint8_t f, uint8_t prio = PRIO_CRITTER);

// The cached frame, or nullptr (then wanted for the next fetch()).
const uint8_t *frame(uint8_t c, uint8_t f);
const uint8_t *peek(uint8_t c, uint8_t f);
bool cached(uint8_t c, uint8_t f);          // no side effects (not even LRU)

// Reads the wanted frames that miss. Limits per call: commands (at most 2)
// and blocks read, gaps included. False: the card failed (card::status()).
void setBudget(uint8_t cmds, uint8_t blocks);
bool fetch();

// The last fetch()'s numbers and the totals below are for the debug
// commands, the overlay, the sprite lab and the host test only: a release
// build keeps none (nothing there reads them: 0.25 KB of flash, 20 B RAM).
#if CHGAME_DEBUG || defined(CHTEST)
struct Stats {
    uint8_t cmds, blocks;               // card commands and blocks of the last fetch()
    uint8_t wants, misses, loaded;      // wanted; not cached (prefetch aside); copied in (siblings too)
    uint8_t deferred, evicted, compacted;   // misses left for later (prefetch aside); frames dropped; compactions
    uint8_t hits, drawMisses;           // frame()/peek() found, frame() did not, since the last fetch()
    uint8_t entries;
    uint16_t bytes;                     // arena bytes in use
    uint16_t moved;                     // bytes compaction moved
    uint16_t us;                        // the last fetch() (micros)
};
const Stats &stats();

// Sums since clearTotals(), for the debug commands only: a release build
// keeps none (RAM is the binding limit) and clearTotals() does nothing.
struct Totals { uint32_t fetches, cmds, blocks, misses, loaded, hits, drawMisses, deferred, maxUs; };
const Totals &totals();
void clearTotals();
#else
inline void clearTotals() {}
#endif

#ifdef CHTEST
struct DebugEntry { uint16_t key, off, size, used; };
uint32_t debugDir(DebugEntry *out);         // the directory (MAX_ENTRIES), by offset
const uint8_t *debugArena();
#endif

}  // namespace spool
