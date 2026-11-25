// The piezo sequencer, from CHBlackjack's: the CHGameSound library does far
// more (four-voice synthesis, MOD playback) than these games need and costs
// ~6.5 KB of flash on a 50 KB part. This keeps two of its data formats -
// effect steps and Playtune scores - adds hand-written melodies, and plays
// them with TIM1 channel 2 on
// PB10 (the library's pin setup), driven by the core's 1 kHz SysTick hook
// (osSystickHandler), so no other timer is used.
//
// CHGame Rev1 (ARDUINO_CHGAME_REV1) moves the sound to TIM2 channel 2 on PA1,
// which switches a MOSFET under a magnetic buzzer fed from VSYS, and the LED
// to PB10; PB9 is the backlight there (CHGfx's) and is never touched here.
#include <Arduino.h>
#include "Audio.h"

#ifdef CHSIM
// The simulator is silent: the same interface, no hardware. It remembers the
// last effect so scripts and tests can check what would have sounded.
namespace audio {
static bool simOn = true;
uint8_t simLast = 0xFF;
void begin(const Effect *, uint8_t, bool on) { simOn = on; }
void setOn(bool on) { simOn = on; }
bool on() { return simOn; }
void sfx(uint8_t id) { if (simOn) simLast = id; }
void sfx(uint8_t id, uint8_t) { sfx(id); }
void blip(uint16_t, uint16_t, bool) {}
void note(uint16_t, uint16_t, uint8_t) {}
bool playing() { return false; }
void setMusic(uint8_t) {}
void music(const uint8_t *, bool) {}
void melody(const Melody &, bool) {}
void stopMusic() {}
void loopMusic(bool) {}
bool musicPlaying() { return false; }
void update() {}
void led(Led) {}
}  // namespace audio
#else

namespace audio {

static const Effect *table = nullptr;


// --- Sequencer state (shared with the 1 kHz interrupt) ----------------------
static volatile const Step *fxSteps = nullptr;
static volatile uint8_t fxN = 0, fxI = 0, fxPrio = 0;
static volatile uint16_t fxPitch = 256;          // Q8: sfx()'s transposition
static volatile uint16_t fxT = 0;
static Step oneStep;                            // blip() and note()

static const uint8_t *score = nullptr;           // a Playtune score ...
static const Melody *mel = nullptr;             // ... or a melody
static volatile const uint8_t *scorePos = nullptr;
static volatile uint8_t melI = 0;
static volatile uint16_t melT = 0;
static volatile uint16_t scoreWait = 0;
static volatile bool scoreLoops = true;          // honour the score's restart (0xE0)
static volatile uint8_t notes[4];               // MIDI note per channel, 0 = off
static uint8_t arpT = 0, arpCh = 0, leadHold = 0;
static const uint8_t ARP_MS = 6;                // arpeggio: ms per note
// Lead: how long the melody must be silent before other voices fill in -
// just over the 12 ms gap tools/make_music.py leaves between notes.
static const uint8_t LEAD_HOLD_MS = 20;

static bool started = false, running = false;
static volatile bool soft, glide;               // the effect's flags
static uint8_t musicMode = ARPEGGIO;
static uint16_t lastHz = 0;
static uint8_t ledPattern = 0;
static uint16_t ledT = 0;

extern "C" volatile uint32_t CFGHR_tmpB;        // GPIOB CFGHR is write-only: go through the shadow

#if defined(ARDUINO_CHGAME_REV1)
// CHGame Rev1: TIM2 channel 2 on PA1 (TIM2 remap 000). PA1 high switches the
// buzzer's coil on, so PA1 must never rest high: a stopped timer holding its
// output high would be DC through the coil and its transistor. Silence is the
// channel forced low (OC2M = 100), with PA1 left on the timer; PA1's mode is
// set here once and never from tone() or the SysTick interrupt.
//
// TIM2 is an advanced-control timer on the CH32X035 (RM chapter 12: break,
// dead time, complementary outputs), so it needs MOE like TIM1. Only CC2E is
// enabled: CH1 would be PA0 (VBUS sense, never driven), and C2N, BKIN and
// ETR map to pins this package does not have.
static const uint16_t CH2_PWM1 = 0x6800;        // CHCTLR1: OC2M = 110 (PWM 1), OC2PE
static const uint16_t CH2_LOW  = 0x4800;        // CHCTLR1: OC2M = 100 (forced low), OC2PE

static void hwInit() {
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA;
    RCC->APB1PCENR |= RCC_APB1Periph_TIM2;
    GPIOA->BCR = 1u << 1;                                     // PA1's output latch low
    // TIM2_RM, PCFR1[20:18], to 000: CH2 on PA1. A masked write: the other
    // remaps (and SW_CFG) stay. (The vendor's GPIO_PinRemapConfig names
    // other bits on this chip.)
    AFIO->PCFR1 = AFIO->PCFR1 & ~(7u << 18);
    TIM2->CTLR1 = 0; TIM2->CTLR2 = 0; TIM2->SMCFGR = 0; TIM2->DMAINTENR = 0;
    TIM2->CCER = 0;
    TIM2->CHCTLR1 = CH2_LOW;                                  // silent until tone()
    TIM2->CHCTLR2 = 0;
    TIM2->PSC = 47;                                           // 1 MHz
    TIM2->RPTCR = 0;
    TIM2->ATRLR = 999;
    TIM2->CH2CVR = 0;
    TIM2->CNT = 0;
    TIM2->BDTR = 0x8000;                                      // MOE; no break, no dead time
    TIM2->CCER = 0x10;                                        // CC2E alone
    TIM2->SWEVGR = 1;
    TIM2->INTFR = 0;
    // PA1 to the timer last, once its channel is driving low: alternate-
    // function push-pull. CFGLR reads back, so a masked write.
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(15u << 4)) | (11u << 4);
    running = false; lastHz = 0;
}

// As Rev0's tone() below (the same timing, ATRLR and CH2CVR preloaded), on
// TIM2. Under 16 Hz a period is more than 65,536 counts: ATRLR would wrap and
// the duty land beyond it, which holds PA1 high, so those count as silence.
static void tone(uint16_t hz, bool smooth) {
    if (hz < 16) hz = 0;
    if (hz == lastHz) return;
    lastHz = hz;
    if (!hz) {
        TIM2->CHCTLR1 = CH2_LOW;                    // low at once, whatever the counter does
        TIM2->CH2CVR = 0; TIM2->SWEVGR = 1; TIM2->CTLR1 = 0; TIM2->INTFR = 0;
        running = false;
        return;
    }
    uint32_t period = (1000000u + hz / 2u) / hz;
    if (period < 2) period = 2;
    uint32_t duty = soft ? period / 8 : period / 2;
    if (smooth && running) {
        TIM2->ATRLR = period - 1;
        TIM2->CH2CVR = duty;
        return;
    }
    running = true;
    TIM2->CTLR1 = 0;
    TIM2->ATRLR = period - 1;
    TIM2->CH2CVR = duty;
    TIM2->SWEVGR = 1;
    TIM2->INTFR = 0;
    TIM2->CHCTLR1 = CH2_PWM1;
    TIM2->CTLR1 = 0x81;
}
#else
static void pb10(uint32_t nibble) {
    uint32_t v = (CFGHR_tmpB & ~(15u << 8)) | (nibble << 8);
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
}

static void hwInit() {
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1;
    AFIO->PCFR1 = (AFIO->PCFR1 & ~(7u << 15)) | (1u << 15);   // TIM1 partial remap: CH2 on PB10
    GPIOB->BCR = 1u << 10;
    pb10(11u);                                                // alternate-function push-pull
    TIM1->CTLR1 = 0; TIM1->CTLR2 = 0; TIM1->SMCFGR = 0; TIM1->DMAINTENR = 0;
    TIM1->CCER = 0;
    TIM1->CHCTLR1 = 0x6800;                                   // CH2 PWM1 + preload
    TIM1->CHCTLR2 = 0;
    TIM1->PSC = 47;                                           // 1 MHz
    TIM1->RPTCR = 0;
    TIM1->ATRLR = 999;
    TIM1->CH2CVR = 0;
    TIM1->CNT = 0;
    TIM1->BDTR = 0x8000;                                      // MOE
    TIM1->CCER = 0x10;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    running = false; lastHz = 0;
}

// Effects restart the timer on every change (their sweeps are voiced that
// way, unless they GLIDE). Music passes smooth: a sounding tone changes pitch
// at the end of its current cycle instead (ATRLR and CH2CVR are preloaded),
// so switching notes never clips a cycle - a click on the piezo.
static void tone(uint16_t hz, bool smooth) {
    if (hz == lastHz) return;
    lastHz = hz;
    if (!hz) {
        TIM1->CH2CVR = 0; TIM1->SWEVGR = 1; TIM1->CTLR1 = 0; TIM1->INTFR = 0;
        running = false;
        return;
    }
    uint32_t period = (1000000u + hz / 2u) / hz;
    if (period < 2) period = 2;
    uint32_t duty = soft ? period / 8 : period / 2;
    if (smooth && running) {
        TIM1->ATRLR = period - 1;
        TIM1->CH2CVR = duty;
        return;
    }
    running = true;
    TIM1->CTLR1 = 0;
    TIM1->ATRLR = period - 1;
    TIM1->CH2CVR = duty;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    TIM1->CTLR1 = 0x81;
}
#endif  // ARDUINO_CHGAME_REV1

// MIDI note -> Hz: the top octave (C8..B8), shifted down.
static uint16_t noteHz(uint8_t n) {
    static const uint16_t TOP[12] = {4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};
    int sh = 9 - n / 12;
    return sh >= 0 ? (uint16_t)(TOP[n % 12] >> sh) : 0;
}

static void scoreTick() {
    if (!scorePos) return;
    if (scoreWait) { scoreWait--; return; }
    for (int guard = 0; guard < 16; guard++) {
        uint8_t b = *scorePos;
        if (b < 0x80) {                          // wait, 2 bytes big-endian ms
            scoreWait = (uint16_t)((b << 8) | scorePos[1]);
            scorePos += 2;
            if (scoreWait) { scoreWait--; return; }
            continue;
        }
        uint8_t cmd = b & 0xF0, ch = b & 3;
        if (cmd == 0x90) { notes[ch] = scorePos[1]; scorePos += 2; }
        else if (cmd == 0x80) { notes[ch] = 0; scorePos += 1; }
        else if (b == 0xE0 && scoreLoops) { scorePos = score; }
        else { scorePos = nullptr; for (auto &n : notes) n = 0; return; }
    }
}

// A melody plays through channel 0, a note at a time.
static void melodyTick() {
    if (!scorePos) return;
    uint8_t note = mel->notes[melI * 2];
    uint16_t len = (uint16_t)(mel->notes[melI * 2 + 1] * mel->unitMs);
    notes[0] = note && melT + mel->gapMs < len ? note : 0;
    if (++melT >= len) {
        melT = 0;
        if (++melI >= mel->count) {
            melI = 0;
            if (!scoreLoops) { scorePos = nullptr; notes[0] = 0; }
        }
    }
}

// The music note to sound this millisecond.
static uint16_t musicHz() {
    if (musicMode == LEAD) {
        // Lead: the melody wins. Its short gaps between notes stay silent
        // (filling them with the bass for a few ms is what garbles a tune);
        // the other voices only take over when the melody really rests.
        if (notes[0]) { leadHold = LEAD_HOLD_MS; return noteHz(notes[0]); }
        if (leadHold) { leadHold--; return 0; }
        for (uint8_t k = 1; k < 4; k++) if (notes[k]) return noteHz(notes[k]);
        return 0;
    }
    // Arpeggio: every ARP_MS move on to the next channel that is sounding
    // (at once if the current one stops), so voices take even turns.
    if (++arpT >= ARP_MS || !notes[arpCh]) {
        arpT = 0;
        for (uint8_t k = 0; k < 4; k++) {
            arpCh = (arpCh + 1) & 3;
            if (notes[arpCh]) break;
        }
    }
    return notes[arpCh] ? noteHz(notes[arpCh]) : 0;
}

// The music player, linked in only by music() or melody(): it keeps time
// every millisecond, and gives the note to sound when no effect has the pin.
static uint16_t scorePlayer(bool sound)  { scoreTick();  return sound ? musicHz() : 0; }
static uint16_t melodyPlayer(bool sound) {      // one voice: no arpeggio or lead needed
    melodyTick();
    return sound && notes[0] ? noteHz(notes[0]) : 0;
}
static uint16_t (*player)(bool sound) = nullptr;


extern "C" void osSystickHandler(void) {
    if (!started) return;
    const Step *s = (const Step *)fxSteps;
    uint16_t m = musicMode && player ? player(!s) : 0;
    if (s) {
        const Step &st = s[fxI];
        uint16_t hz = (uint16_t)(st.hz * 20), ms = (uint16_t)(st.ms * 2);
        bool sweep = hz && st.endHz;
        if (sweep) hz = (uint16_t)(hz + ((int32_t)st.endHz * 20 - hz) * fxT / ms);
        hz = (uint16_t)((uint32_t)hz * fxPitch >> 8);             // 256: as written
        bool smooth = glide && sweep && fxT;
        if (++fxT >= ms) {
            fxT = 0;
            if (++fxI >= fxN) { fxSteps = nullptr; fxPrio = 0; }
        }
        tone(hz, smooth);
    } else {
        soft = false;
        tone(m, true);
    }
}

void begin(const Effect *effects, uint8_t count, bool o) {
    table = effects; (void)count;               // (the count documents the table)
#if defined(ARDUINO_CHGAME_REV1)
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    GPIOB->BCR = 1u << 10;                                         // the LED off (active high) ...
    uint32_t v = (CFGHR_tmpB & ~(15u << 8)) | (3u << 8);           // ... then PB10: push-pull output
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
#else
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    uint32_t v = (CFGHR_tmpB & ~(15u << 4)) | (3u << 4);          // PB9 LED: push-pull output
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
    GPIOB->BCR = 1u << 9;
#endif
    setOn(o);
}

void setOn(bool o) {
    if (!o || !table) {
        started = false;
        lastHz = 1; tone(0, false);
        return;
    }
    if (!started) hwInit();
    started = true;
}

bool on() { return started; }

// An effect already sounding above priority `prio` keeps the pin.
static bool busy(uint8_t prio) { return !started || (fxSteps && prio < fxPrio); }

static void start(const Step *st, uint8_t n, uint8_t flags, uint16_t pitch) {
    __disable_irq();
    fxSteps = st; fxN = n; fxI = 0; fxT = 0; fxPrio = flags & 15; fxPitch = pitch;
    soft = (flags & SOFT) != 0; glide = (flags & GLIDE) != 0;
    lastHz = 1;                              // restart the tone with the new duty
    __enable_irq();
}

static void play(uint8_t id, uint16_t pitch) {
    if (!started) return;                    // (started means begin() gave the table)
    const Effect &e = table[id];
    if (!busy(e.flags & 15)) start(e.steps, e.n, e.flags, pitch);
}

void sfx(uint8_t id) { play(id, 256); }

void sfx(uint8_t id, uint8_t n) {
    static const uint16_t SEMI[13] = {256, 271, 287, 304, 323, 342, 362, 384, 406, 431, 456, 483, 512};
    play(id, SEMI[n > 12 ? 12 : n]);
}

// One step made up on the spot; it may cut off effects up to priority `over`.
static void one(uint16_t hz, uint16_t ms, uint8_t flags, uint8_t over) {
    if (busy(over)) return;
    __disable_irq();
    oneStep.hz = (uint8_t)((hz + 10) / 20); oneStep.endHz = 0; oneStep.ms = (uint8_t)((ms + 1) / 2);
    __enable_irq();
    start(&oneStep, 1, flags, 256);
}

void blip(uint16_t hz, uint16_t ms, bool s) { one(hz, ms, s ? SOFT : 0, 1); }

void note(uint16_t hz, uint16_t ms, uint8_t priority) { one(hz, ms, priority & 15, priority & 15); }

bool playing() { return fxSteps != nullptr; }

void setMusic(uint8_t mode) {
    musicMode = mode;
    if (!mode) for (auto &x : notes) x = 0;
}

void music(const uint8_t *s, bool loop) {
    __disable_irq();
    score = s; mel = nullptr; scorePos = s; scoreWait = 0; scoreLoops = loop;
    for (auto &x : notes) x = 0;
    leadHold = 0;
    player = scorePlayer;
    __enable_irq();
}

void melody(const Melody &m, bool loop) {
    __disable_irq();
    score = nullptr; mel = &m; scorePos = m.notes; melI = 0; melT = 0; scoreLoops = loop;
    for (auto &x : notes) x = 0;
    leadHold = 0;
    player = melodyPlayer;
    __enable_irq();
}

void loopMusic(bool o) { scoreLoops = o; }

// With sound or music off the score is not advanced, so it counts as finished.
bool musicPlaying() { return started && musicMode && scorePos; }

void stopMusic() {
    __disable_irq();
    scorePos = nullptr;
    for (auto &x : notes) x = 0;
    __enable_irq();
}

void led(Led p) { ledPattern = p; ledT = 0; }

void update() {
    if (!ledPattern) return;
    ledT++;
    bool lit = false;
    switch (ledPattern) {
        case LED_BLINK:  lit = ledT < 12; if (ledT > 12) ledPattern = 0; break;
        case LED_TRIPLE: lit = (ledT % 16) < 8; if (ledT > 48) ledPattern = 0; break;
        case LED_PARTY:  lit = (ledT % 8) < 4; if (ledT > 240) ledPattern = 0; break;
    }
#if defined(ARDUINO_CHGAME_REV1)
    if (lit && ledPattern) GPIOB->BSHR = 1u << 10;              // the LED, PB10 (PB9 is the backlight)
    else GPIOB->BCR = 1u << 10;
#else
    if (lit && ledPattern) GPIOB->BSHR = 1u << 9;
    else GPIOB->BCR = 1u << 9;
#endif
}

}  // namespace audio
#endif  // CHSIM
