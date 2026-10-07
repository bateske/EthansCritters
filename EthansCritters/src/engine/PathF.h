// Path F: full-screen clips streamed from the card straight into gfx_fb:
// the title's loop, the death and win clips and the pause map
// (tools/card/clips.py renders them offline, each with its own palette).
//
// A clip (tools/card/clip.py) is a header block with its frame count, frame
// ms and palette, then 16 blocks a frame, each block eight framebuffer rows
// exactly as gfx_fb holds them: a frame costs one card command (two across
// a run boundary) and a word copy per block, done while the next block
// arrives, and nothing else. A frame replaces the whole framebuffer, so
// whatever goes over it (PRESS A) is drawn after it, every frame.
//
// Call these between gfx_wait() and the next flush (Card.h's bus rule).
#pragma once
#include <stdint.h>

namespace pathf {

// The clip whose header is block `first` of the card file: its palette goes
// to pal::init(). False: the card failed (card::status() says so) or there
// is no clip there.
bool open(uint32_t first);
uint16_t frames();
uint16_t frameMs();              // 0: a still

// Frame k of the open clip into gfx_fb. False: the card failed, or no frame k.
bool frame(uint16_t k);

// The frame of the open clip t logic ticks (60 Hz) after it began, at its
// frame rate: looping, or held on its last frame. False: the card failed.
bool play(uint32_t t, bool loop);

}  // namespace pathf
