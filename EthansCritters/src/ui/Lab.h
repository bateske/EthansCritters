// The sprite lab (temporary, M4; B on the title): the squire with every
// move on plain earth, the HUD, and a parade of every critter clip in the
// bank (the nests, pickups and spawn puff too), four at a time, one
// flashing red (remap), one fading (dither), one mirrored, so every
// animation streams through the frame cache and shows; the effects
// (src/fx/Fx.h) and sounds (Sounds.h) shown off in turn, and on his moves.
// SELECT: the cache's numbers on screen; START: back to the title.
#pragma once
#include <stdint.h>

namespace lab {

void enter();                       // the card is open (the bank is on it)
bool tick();                        // one logic step; false: leave (START)
void plan();                        // before gfx_wait(): what the screen will draw
bool draw();                        // after gfx_wait(): the card reads, the screen; false: the card failed

// Debug commands (the simulator's scripts): H<n>[b] hurts the squire by n
// half hearts from the front (b: from behind), R revives him, Z prints the
// cache's totals, C<c> parades clips c, c+1 ... (C44: the nests), F<n>
// shows effect n (0 spark, 1 dust, 2 mud, 3 water, 4 chips, 5 a number),
// M<dx> <dy> moves the effects as if the camera moved (fx::scroll).
bool hook(char cmd, const char *args);

}  // namespace lab
