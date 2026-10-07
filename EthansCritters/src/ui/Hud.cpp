// The HUD strip. See Hud.h.
//
// The hearts are 8x6 word sprites drawn with gfx_blit4k (one word a row):
// a full one, a half one, an empty one, written as pictures below.
#include "../Size.h"        // (first: Size.h)
#include <CHGame.h>
#include "../assets/Palette.h"
#include "Hud.h"

namespace hud {

// Eight pixels of a picture row as a framebuffer word: r rust, m mud (the
// shine), u umber, p peat, anything else transparent.
constexpr uint32_t px8(const char *s) {
    uint32_t w = 0;
    for (uint32_t i = 0; i < 8; i++) {
        char c = s[i];
        uint32_t v = c == 'r' ? C_RUST : c == 'm' ? C_MUD : c == 'u' ? C_UMBER : c == 'p' ? C_PEAT : 15;
        w |= v << (4 * i);
    }
    return w;
}

alignas(4) static constexpr uint32_t HEARTS[3][6] = {
    {px8(".rr..rr."), px8("rmrrrrrr"), px8("rrrrrrrr"), px8(".rrrrrr."), px8("..rrrr.."), px8("...rr...")},
    {px8(".rr..uu."), px8("rmrruuuu"), px8("rrrruuuu"), px8(".rrruuu."), px8("..rruu.."), px8("...ru...")},
    {px8(".uu..uu."), px8("upuuuuuu"), px8("uuuuuuuu"), px8(".uuuuuu."), px8("..uuuu.."), px8("...uu...")},
};

// The compass needle's tip, by direction (east, then clockwise).
static const int8_t NEEDLE[8][2] = {{2, 0}, {2, 2}, {0, 2}, {-2, 2}, {-2, 0}, {-2, -2}, {0, -2}, {2, -2}};

void draw(uint8_t hp, uint8_t maxHp, int8_t compass, uint8_t nests, uint8_t cleared, uint8_t bar) {
    gfx_fillRect(0, 0, GFX_W, ROWS - 1, C_SOOT);
    gfx_hline(0, ROWS - 1, GFX_W, C_UMBER);
    for (uint32_t i = 0; i * 2 < maxHp; i++) {
        uint32_t h = hp >= i * 2 + 2 ? 0 : hp == i * 2 + 1 ? 1 : 2;
        gfx_blit4k((const uint8_t *)HEARTS[h], 2 + (int)i * 9, 1, 1, 6, 0);
    }
    if (bar) {                      // the toad's bar: a board slot, rust what it has left
        const int bx = GFX_W - BAR_W - 4;
        gfx_fillRect(bx - 2, 0, BAR_W + 4, ROWS - 1, C_UMBER);     // (an umber rim round soot: gfx_rect is not linked)
        gfx_fillRect(bx - 1, 1, BAR_W + 2, ROWS - 3, C_SOOT);
        gfx_fillRect(bx, 2, BAR_W, 3, C_PEAT);
        gfx_fillRect(bx, 2, bar - 1, 3, C_RUST);
        gfx_hline(bx, 2, bar - 1, C_OCHRE);
        return;
    }
    // the compass: a peat ring, a rust needle (a dot with no nest to point to)
    const int cx = 66, cy = 3;
    ring(cx, cy, 3, C_PEAT);
    gfx_pixel(cx, cy, C_MUD);
    if (compass >= 0) {
        uint8_t c = compass & GATE ? C_OCHRE : C_RUST;
        const int8_t *n = NEEDLE[compass & 7];
        gfx_pixel(cx + n[0] / 2, cy + n[1] / 2, c);
        gfx_pixel(cx + n[0], cy + n[1], c);
    }
    // the nests: a pip each, a mossy one when cleared
    for (uint32_t i = 0; i < nests; i++) {
        int px = GFX_W - 6 * (int)(nests - i) - 1;
        gfx_fillRect(px, 1, 5, 5, C_UMBER);
        gfx_fillRect(px + 1, 2, 3, 3, (cleared >> i) & 1 ? C_MOSS : C_PEAT);
    }
}

void centred(int y, const char *s, uint8_t c) { text35s((GFX_W - text35Width(s)) / 2, y, s, c, C_SOOT); }
void centred2(int y, const char *s) { text35x2s((GFX_W - text35x2Width(s)) / 2, y, s, C_BONE, C_SOOT); }

// Soot in three nested rectangles (the rounded outline), then bark in two
// inside them: the edge is what the bark leaves.
void board(int x, int y, int w, int h) {
    for (int i = 0; i < 3; i++) gfx_fillRect(x + 2 - i, y + i, w - 4 + 2 * i, h - 2 * i, C_SOOT);
    for (int i = 1; i < 3; i++) gfx_fillRect(x + 3 - i, y + i, w - 6 + 2 * i, h - 2 * i, C_BARK);
}

// CHGfx's midpoint circle: each step's point mirrored into the eight octants.
void ring(int cx, int cy, int r, uint8_t c) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        for (uint32_t k = 0; k < 8; k++) {
            int a = k & 4 ? y : x, b = k & 4 ? x : y;
            gfx_pixel(cx + (k & 1 ? -a : a), cy + (k & 2 ? -b : b), c);
        }
        if (d < 0) d += 4 * x + 6;
        else d += 4 * (x - y--) + 10;
        x++;
    }
}

}  // namespace hud
