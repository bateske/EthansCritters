// The house palette: sixteen named colours, and the tricks a palette allows.
//
// CHGfx draws with palette indices (0-15) and applies the palette while it
// converts the framebuffer for the panel, so recolouring an index recolours
// every pixel that uses it, for free. pal:: keeps a staging copy in RGB444
// (the 12 bpp panel's own grid, so output, fades and the simulator agree)
// and pal::commit() works out the final colours once a frame: the felt
// theme, FX_A/FX_B colour cycling, a flash, desaturation and the fade.
// CHGfx stages gfx_setPalette() itself (it takes effect when the next flush
// starts), so commit() may run while a frame is still going out; it only
// calls it when a colour really changed, because each change costs CHGfx a
// lookup-table rebuild.
//
// Every frame: pal::tick() once per logic tick, pal::commit() before the
// flush.
#pragma once
#include <stdint.h>

// The house colours, by index. A game that needs other colours in a slot
// passes its own table to pal::init() and names the slot itself (CHDominoes
// has BONE and SLATE where SKIN and CYAN are).
enum : uint8_t {
    INK = 0, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE,
    GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B,
};

namespace pal {

extern const uint16_t HOUSE[16];            // the colours above, RGB444

// The felt (FELT_DK, FELT, FELT_LT) can be re-dyed as a whole.
enum Theme : uint8_t { GREEN, BLUE_FELT, RED_FELT, PURPLE, THEME_COUNT };
extern const uint16_t FELTS[THEME_COUNT][3];

// What FX_A and FX_B do by themselves (while cycling is on):
//   CASINO   FX_A runs through a rainbow (banner outlines, sparkle);
//            FX_B shimmers gold - white - gold
//   TARGETS  FX_A shimmers cyan - white (squares a piece can go to),
//            FX_B pulses red - gold (pieces it can take)
//   HOVER    FX_A fades black - white - black about once a second (a cursor
//            outline); FX_B as CASINO
//   FIRE     FX_A flickers red - gold - white (a hot hand); FX_B as CASINO
enum Mode : uint8_t { CASINO, TARGETS, HOVER, FIRE };

void init(const uint16_t *base = HOUSE);   // these colours, the green felt; commits at once
void setThemes(const uint16_t (*felts)[3], uint8_t count);   // a game's own felts (default FELTS)
void setTheme(uint8_t theme);
uint8_t theme();
void setMode(uint8_t mode);
void setFade(uint8_t level);                // 0 = black .. 16 = full colour
uint8_t fade();
void setDesaturate(uint8_t amount);         // 0 = colour .. 16 = grey
void flash(uint8_t index, uint16_t rgb444, uint8_t frames);  // one colour, briefly
void setFx(uint8_t index, uint16_t rgb444); // set a colour by hand (FX_A/FX_B with cycling off)
void setCycling(bool on);                   // FX_A/FX_B animate by themselves (default on)
void tick();                                // once per logic tick, before commit
void resetClock();                          // restart the FX_A/FX_B cycle (the debug protocol)
void commit();                              // once per frame, before the flush
uint16_t rgb444(uint8_t index);             // a staged colour

}  // namespace pal
