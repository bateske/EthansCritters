// 1 bpp scratch masks for outlined, shadowed and gradient lettering and logos.
//
// Outlined, shadowed or gradient text drawn the naive way (the glyphs nine
// times over, pixel by pixel from flash) costs ~5 ms a line on this chip.
// Instead: render the shape once into a bit mask, grow it by one pixel with
// byte-wide ORs for the outline, and paint every row straight into the
// framebuffer, two pixels a byte. Still 5-10 ms for big lettering: draw it
// once onto still screens, not every frame.
//
// CHGfx 1.3's gfx_textFx() works the same way and draws the same pixels with
// CHGfx_Tiny3x5, but brings CHGfx's whole GFXfont text path: about 2 KB more
// flash than this and Draw's 3x5 font.
//
// The mask lives in CHGfx's chunk buffers (gfx_chunkScratch, 1 KB), which
// are idle between gfx_wait() and the next flush - i.e. while drawing.
// Never keep a Mask across frames or use one outside drawing. Masks paint
// anywhere on screen: they ignore CHGfx's clip rectangle.
//
//     Mask m = maskBegin(text35WidthScaled("WIN", 3), 6 * 3);
//     maskText35(m, 0, 0, "WIN", 3);
//     maskDraw(m, 40, 50, GOLD, INK, WINE);       // fill, outline, shadow
#pragma once
#include <stdint.h>

struct Mask {
    uint8_t *bits;       // MSB-first rows, 1 px margin on every side
    uint8_t stride;      // bytes per row
    uint8_t w, h;        // usable size (excluding the margin)
};

Mask maskBegin(int w, int h);                   // cleared; (w + 2) x (h + 2) <= ~8000 px

// The 3x5 font at an integer scale (4*scale px advance). dy, if given,
// offsets each character vertically (wavy banners).
void maskText35(Mask &m, int x, int y, const char *s, uint8_t scale = 1, const int8_t *dy = nullptr);
int  text35WidthScaled(const char *s, uint8_t scale);
// A w x h 1 bpp bitmap (MSB-first rows, zero padding bits) into a mask begun
// at w*scale x h*scale.
void maskBlit1(Mask &m, const uint8_t *bits, uint8_t w, uint8_t h, uint8_t scale = 1);

// Paint the mask with its top-left at (x, y). outline/shadow < 0: none (the
// shadow is the outline moved a pixel down and right, or without an outline
// the shape moved so). ramp, if given, is a fill colour per mask row
// (gradient lettering).
void maskDraw(const Mask &m, int x, int y, uint8_t fill, int outline = -1, int shadow = -1,
              const uint8_t *ramp = nullptr);
// Just the shape in one colour (no outline, no shadow).
void maskPaint(const Mask &m, int x, int y, uint8_t c);
