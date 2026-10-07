// The house drawing: rounded rects and panels, span sprites (plain, scaled,
// flipped, rotated), dithering, an in-place colour remap, convex polygons,
// column glyphs and PPOT's 3x5 font.
//
// CHGfx 1.3 has its own versions of several of these (gfx_fillRoundRect,
// gfx_sprite4, gfx_dither, gfx_textFx with CHGfx_Tiny3x5, the same glyphs),
// written for generality; these are the casino games' smaller ones, the text
// about 2 KB smaller in flash. Both work on the same framebuffer: use either.
//
// Everything here draws inside CHGfx's clip rectangle (gfx_setClip), like
// CHGfx's own primitives.
//
// The rule on this chip: code runs from flash with 3 wait states, so a
// function call per pixel costs ~2-3 us. Everything here is built from
// gfx_hline spans (word stores, from SRAM) or tight byte and word loops, the
// hottest from SRAM.
#pragma once
#include <stdint.h>
#include <CHGfx.h>

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------
void fillRound(int x, int y, int w, int h, uint8_t r, uint8_t c);   // r <= 4: pixel-art corners
void roundRect(int x, int y, int w, int h, uint8_t r, uint8_t c);
// fillRound in fill, then roundRect in edge: cards, plates, panels.
void panel(int x, int y, int w, int h, uint8_t r, uint8_t fill, uint8_t edge);
// The same lit from above: a 1 px drop shadow outside (down and right), the
// edge, then inside a lighter top line and a darker bottom one.
void panelLit(int x, int y, int w, int h, uint8_t r, uint8_t fill, uint8_t edge);
// A raised rectangle's rim: light along the top and left, dark along the
// bottom and right (the first form takes them from LIGHTER/DARKER).
void bevel(int x, int y, int w, int h, uint8_t c);
void bevel(int x, int y, int w, int h, uint8_t light, uint8_t dark);
// One step darker / lighter for each house colour (Palette.h): the shading
// that gives panels, cards and chips their depth.
extern const uint8_t DARKER[16], LIGHTER[16];

void dither(int x, int y, int w, int h, uint8_t c, uint8_t phase);      // 50% checker
// A soft shadow (a 50% INK checker) 2 px below and right of a w x h box.
void dropShadow(int x, int y, int w, int h);
// Recolour in place, pixel = remap[pixel] (dimming, highlighting).
void remapRect(int x, int y, int w, int h, const uint8_t *remap);
// A convex polygon, corners in 1/16 px (n <= 8), filled by pixel centres so
// polygons sharing an edge neither overlap nor leave a gap. dither >= 0: a
// 50% checker of c (dither's phase) instead of solid.
void fillConvex(const int16_t *xy, uint8_t n, uint8_t c, int dither = -1);

// ---------------------------------------------------------------------------
// Span sprites
// ---------------------------------------------------------------------------
// span4 art (tools/assets.py pack_span4): w, h, then per row a count and
// (len-1)<<4|colour bytes, colour 15 = skip. Drawn through a remap (a colour
// for each of 0..14; nullptr or RM_ID: as drawn) and scaled (Q8, 256 = 1:1).
enum : uint8_t { SPR_FLIP_V = 1, SPR_FLIP_H = 2 };   // upside down; mirrored (1:1 only)
void sprite4(const uint8_t *data, int x, int y, const uint8_t *remap = nullptr, int scale = 256,
             uint8_t flip = 0);
extern const uint8_t RM_ID[16];                     // the identity remap
// span4 art turned by `angle` (256 = one turn) and scaled (256 = 1:1) about
// its pixel (ax, ay), which lands on screen (px, py). Decodes into the CHGfx
// chunk scratch: render time only, art up to 1 KB unpacked (e.g. 32x60).
void spriteRot(const uint8_t *data, int ax, int ay, int px, int py, uint8_t angle, int scale,
               const uint8_t *remap);
// The same for raw 4 bpp art the caller has put in the chunk scratch (w x h,
// rows of (w + 1) / 2 bytes, low nibble first, 15 = clear).
void rotRaw(int w, int h, int ax, int ay, int px, int py, uint8_t angle, int scale,
            const uint8_t *remap);

// ---------------------------------------------------------------------------
// Glyphs and the 3x5 font
// ---------------------------------------------------------------------------
// Column-major 1 bpp glyph (bit 0 = top row, <= 8 rows).
void glyph(int x, int y, const uint8_t *cols, uint8_t ncols, uint8_t c);
// Row-major 1 bpp glyph up to 16 wide (the leftmost pixel in bit 15).
void glyph16(int x, int y, const uint16_t *rows, uint8_t nrows, uint8_t c);

// PPOT's 3x5 font: capitals, lower case (with descenders), figures and
// punctuation. 4 px advance, '~' = a 2 px space, '\n' = 7 px down. Returns
// the width drawn.
int  text35(int x, int y, const char *str, uint8_t c);
int  text35Width(const char *str);
// The same over its own shade a pixel down and right (embossed).
int  text35s(int x, int y, const char *str, uint8_t c, uint8_t shade = 0);
// The same font doubled (8 px advance, 12 rows with the descender): menus.
void text35x2(int x, int y, const char *str, uint8_t c);
void text35x2s(int x, int y, const char *str, uint8_t c, uint8_t shade = 0);
inline int text35x2Width(const char *str) { return text35Width(str) * 2; }
const uint8_t *glyph35(char ch);                     // its three column bytes, nullptr = none
