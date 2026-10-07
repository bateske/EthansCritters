// The HUD: the 8-row strip over the playfield (rows 0-7). Hearts on the
// left (half steps), a compass in the middle pointing to the nearest nest
// still standing (then to The Rot's gate), a pip for each nest on the
// right; while the toad is fought, its bar instead of the compass and the
// pips. Soot, rust and umber, like something scratched on a board; no
// casino gloss.
#pragma once
#include <stdint.h>

namespace hud {

static const uint8_t ROWS = 8;
// The compass: NO_COMPASS, else 0..7: east, then clockwise in 45 degree
// steps (to a nest, a rust needle), + GATE: to The Rot's gate (an ochre one).
enum : int8_t { NO_COMPASS = -1, GATE = 8 };

// The boss bar's inside, px (its fill: of BAR_W).
static const uint8_t BAR_W = 72;

// hp and maxHp in half hearts (maxHp even, at most 10); nests: how many
// pips, cleared: a bit per cleared nest; bar: 0 none (the compass and the
// pips), else 1 + the boss bar's fill (0..BAR_W).
void draw(uint8_t hp, uint8_t maxHp, int8_t compass, uint8_t nests, uint8_t cleared, uint8_t bar = 0);

// Words centred across the screen, shaded with soot (every screen's): the
// 3x5 font in colour c, or twice its size in bone.
void centred(int y, const char *s, uint8_t c);
void centred2(int y, const char *s);

// A board nailed up for words (the card screen's, a nest's banner, the
// toad's name): bark with a soot edge, its corners rounded by 2 px (the
// pixels CHGame's panel(x, y, w, h, 2, C_BARK, C_SOOT) gives, in five fills
// instead of its general rounded shapes: 228 B less).
void board(int x, int y, int w, int h);
// A ring of radius r round (cx, cy) (the compass, the ripple over a diving
// beaver): gfx_circle()'s pixels from a loop over the eight octants (76 B
// less than its unrolled plots).
void ring(int cx, int cy, int r, uint8_t c);

}  // namespace hud
