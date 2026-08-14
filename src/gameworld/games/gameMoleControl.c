#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Mole Control (Markus Klingler / grafMakulaDer2te,
// github.com/grafMakulaDer2te/mole-control, v0.1.1 / Jan 2024 - License:
// NONE SPECIFIED, confirmed by reading the real repo directly: no LICENSE/
// COPYING file anywhere, no license header in README.md, change.log or the
// single `mole-control.ino` source file itself, whose entire header comment
// is the two lines `Mole Control by Markus Klingler (grafMakulaDer2te)` /
// `//classic Version`). The same real author as this cartridge's own
// ANOTHER 2048 (gameAnother2048.c) - same one-file sketch shape, same
// free-spinning-loop + millis()-debounce structure, same counter-based
// stand-in for an RNG, so several of the porting decisions below are
// deliberately identical to that file's own.
//
// THE REAL MECHANIC, as actually implemented (not as the name suggests):
// a fixed 3x3 grid of holes fills the left two thirds of the LCD, with a
// live score/level/lives readout down the right edge. A hammer cursor (a
// plain 12x13 outline rectangle, not a hammer sprite) is moved between the
// 9 holes with the D-pad and swung with Button A. Moles pop up one at a
// time, at a random hole, on a fixed timer:
//   - A spawn attempt happens every `moleNowMinTimeNextMole` ms (2500ms at
//     level 1). One random hole of the 9 is picked; if that hole is
//     already showing a mole, the attempt is simply skipped and nothing
//     spawns until the next one - so at most one new mole per interval,
//     and occasionally none.
//   - A spawned mole stays up for `moleMoleTimeoutTimeNow` ms (5000ms at
//     level 1) and then burrows away on its own, costing one life.
//   - Swinging at a hole that has a mole scores a point and clears it;
//     swinging at an empty hole costs one life.
//   - Every `moleScoreLevelUpNow` points (5 at first, then 5 + level/2)
//     the level goes up: the mole timeout drops 200ms per level (floor
//     500ms), the spawn interval drops 300ms per level (floor 300ms), and
//     the (dead - see below) max interval drops 100ms per level (floor
//     500ms). So the game gets faster in both directions at once.
//   - The game starts with 11 lives; at 0 the mole logic freezes entirely
//     and a "Game Over" box is drawn over the frozen board.
// Upstream's own README describes slightly different numbers ("2.5 seconds
// at the beginning", "10 lives at the beginning") than its own code
// actually implements (5000ms, 11 lives) - the code is what is ported here.
//
// Every real `gB.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the full reasoning). `int8_t`/
// `int16_t`/`int32_t`/`uint8_t`/`uint16_t`/`uint32_t`/`long` all became
// plain `int` (avrCompat.h aliasing - this dialect has exactly one 32-bit
// integer width; see the one place below where that difference is
// deliberately undone). Upstream's own `int16_t(...)` C++ functional cast
// became a plain `(int)` cast. The seven `#define BTN_*_PIN`/`#define
// SCR_*` pin constants and `#define BuzzerPin` at the top of the real
// sketch are dropped: they are real hardware pin numbers, never referenced
// anywhere in the sketch's own code (and each is written with a stray
// trailing semicolon inside the macro body upstream, so they could not have
// been used in an expression even if something had tried) - the identical
// situation gameAnother2048.c's own header comment documents for this same
// author's other sketch. `gb.pickRandomSeed()` became `gbPickRandomSeed()`,
// a documented no-op; `gb.battery.show = false;` was dropped outright
// (purely cosmetic on real hardware, no equivalent exists or is needed
// here); `delay(200)` (a real blocking boot/restart pause with no gameplay
// effect of its own) was dropped too - all three matching every other port
// in this project.
//
// millis() -> A VIRTUAL 50ms-PER-TICK CLOCK: this game's whole design is
// millis()-driven (mole timeouts, spawn interval, input debounce), and
// Vircon32 has no wall-clock primitive to read (the same gap
// gameGemgem.c's/gameCatcher.c's/gameSnakeAbc.c's own header comments
// document). `moleMillis` stands in for it: upstream never calls
// `gb.setFrameRate()`, so it runs at real Gamebuino Classic's own 20fps
// default (50ms per tick, which this shim's own gbUpdate() throttle honors
// exactly), and `moleMillis` advances by exactly 50 on every real logic
// tick. Every one of upstream's own real millisecond constants is a whole
// multiple of 50 (5000/2500/500/300/200/100), so every deadline lands
// exactly on a tick with no rounding drift at all, and every upstream
// formula is kept literally in milliseconds rather than being rewritten
// into ticks. The clock keeps advancing during the title screen too,
// exactly like real millis() does while `gb.titleScreen()` blocks - which
// matters: see the restart bug below, and note that a player who lingers
// more than 5 seconds on the title screen gets a mole immediately on the
// first gameplay tick (upstream's own `level_timeNextMole` starts at the
// literal value 5000, i.e. an absolute deadline initialized from a
// duration - real hardware behaves the same way, since its millis() is
// already several seconds past 5000 by the time a player dismisses the
// blocking title screen).
//
// THE FREE-SPINNING loop() -> ONE PASS PER LOGIC TICK: upstream's input
// block, `checkTimoutMoles()`, `randGenMoles()` and `rand_counter++` all
// run on every single `loop()` iteration (thousands per second on real
// hardware), with only `drawGamePad()` gated behind `if(gb.update())`.
// Here the whole body runs once per real 20fps logic tick, in upstream's
// own textual order (input, then draw, then the timeout/spawn logic, so a
// mole that spawns on tick N first becomes visible on tick N+1 exactly like
// upstream). Two real consequences, both handled deliberately:
//   - The 200ms debounce still does real work and is kept literally
//     (`moleDebounceNextMillis = moleMillis + 200`): it locks the whole
//     input block out for 4 ticks after any accepted press, so the hammer
//     steps deliberately rather than sliding. (Note upstream's own
//     `#define debounceDelay 100` is never used anywhere - the real code
//     writes the literal 200 instead; the unused macro is dropped.) A press
//     that lands inside that window is lost here exactly as it is upstream,
//     where `gb.buttons.pressed()` only latches for one 50ms frame and so
//     has long expired by the time the 200ms lockout lifts.
//   - `rand_counter` is REPLACED WITH arand(), DELIBERATELY - the same
//     decision, for the same reason, as gameAnother2048.c's own (see that
//     file's own header comment). Upstream has no real RNG call anywhere:
//     it free-runs a plain counter (`rand_counter++` per loop iteration,
//     plus a scramble - `-= 5678`, `&= ~0x00000F0F`, `+= ~rand_counter`,
//     `|= 0x000000FF`, `+= 1234; = ~rand_counter`, `*= 3 + 1` - inside
//     each individual button branch) and samples `rand_counter % 9` at
//     spawn time to pick which hole gets the mole. On real hardware, with
//     many thousands of loop iterations between two spawns, that is
//     effectively uniform noise over the 9 holes, which is exactly what
//     the formula stands in for. Reproducing the counter literally here
//     would NOT reproduce it: this port's own loop runs at a fixed 20 ticks
//     per second and spawn attempts are always an exact whole number of
//     ticks apart (50 ticks at level 1), so `rand_counter % 9` would step
//     by a fixed amount every time and cycle through the 9 holes in a
//     fixed, fully predictable order for the entire session. So the
//     counter, all six of its per-button scrambles, and upstream's own
//     `positive_modulo()` helper (dead code - it exists only to keep that
//     counter non-negative and is never actually called anywhere in the
//     real sketch) are all dropped in favour of a single `arand( 9 )`,
//     which reproduces the real observable behavior (a uniformly random
//     hole) rather than the exact counter arithmetic. The one place this
//     needs care is that upstream evaluates `rand_counter % 9` TWICE in
//     `randGenMoles()` - once to test the hole and once to fill it - with
//     the counter unchanged in between, so both reads select the same
//     hole; `moleRandGenMoles()` below therefore samples arand() once into
//     a local and uses it for both, rather than rolling twice.
//
// A REAL DEAD "RANDOM INTERVAL" - THE SPAWN RATE IS NOT RANDOM AT ALL:
// upstream's own `randGenMoles()` computes `timeInterval = max - min` and
// `randTimeInterval = rand_counter % timeInterval`, then never uses
// `randTimeInterval` for anything - the line it looks like it was written
// for instead reads `level_timeNextMole = millis() + constrain(
// (level_nowMinTimeNextMole), level_nowMinTimeNextMole,
// level_nowMaxTimeNextMole)`, i.e. a `constrain()` of a value against
// itself as its own lower bound, which is provably just
// `level_nowMinTimeNextMole` unchanged. So every spawn attempt is exactly
// `min` milliseconds after the last one, with the whole min/max pair
// collapsing to "min" and `level_nowMaxTimeNextMole` only ever mattering
// via the `if(max < min) max = min` clamp in `setLevel()`. That real
// behavior (a fixed, non-random spawn interval) is preserved exactly;
// `moleRandGenMoles()` below just writes the already-reduced form with
// this note attached rather than carrying the `constrain()` call. The two
// genuinely dead lines are NOT carried over, deliberately: `rand_counter %
// timeInterval` is a modulo whose divisor is a runtime value that a
// sufficiently unlucky state could make zero, and a modulo by zero
// hard-traps the CPU on this platform where AVR silently yields garbage
// (VIRCON32_C_DIALECT.md section 17.3) - there is no reason to carry a
// crash risk for a value provably nothing reads.
//
// THE BUTTON-C RESTART IS GENUINELY BROKEN UPSTREAM - PRESERVED, WITH REAL
// AVR NARROWING EMULATED ON PURPOSE: upstream's own restart branch resets
// score/level/lives correctly, but sets its three real timing variables to
// `<duration constant> + millis()` instead of the plain duration constant:
//     level_nowMinTimeNextMole   = 2500 + millis();
//     level_nowMaxTimeNextMole   = 5000 + millis();
//     level_moleTimeoutTimeNow   = 5000 + millis();
// All three are real `int16_t` on real hardware, so the sum narrows modulo
// 65536 into a signed 16-bit value. The result is a real, shipped bug with
// two distinct regimes: restart within the first ~30 seconds of a session
// and the values stay positive but enormous (a 12-second-plus gap between
// moles, moles that never time out); restart after that and they wrap
// NEGATIVE, so every spawn deadline and every mole's own timeout is
// already in the past the moment it is written - moles spawn every single
// tick and immediately expire, draining all 10 lives in well under a second
// and dropping straight back to "Game Over".
//   This port reproduces that, rather than either "fixing" it or letting
// this dialect's always-32-bit `int` silently change it. Leaving the
// narrowing out is NOT the neutral choice: a plain 32-bit `2500 + millis()`
// stays large and positive forever, which would make a restarted game sit
// completely inert with no moles at all for minutes - a behavior real
// hardware never has. So `moleNarrowS16()` below applies the real AVR
// truncation at exactly those three assignment sites (the same "emulate the
// real narrow-int wraparound the original depended on" treatment
// gameUnderTheTower.c's own `uttNarrowS8()` and gameDarkShmup.c's own
// `& 255`/`& 65535` masks already establish in this project). Note the
// resulting game-over-on-restart is not a soft-lock: this cartridge's own
// global Start-button quit-confirmation dialog returns to the shared menu
// from any state, and relaunching runs gameMoleControl_init() fresh - the
// exact equivalent of the power cycle a real Gamebuino owner would need,
// the same reasoning gameAnother2048.c's own preserved "no way back after
// winning" quirk already uses.
//
// TWO MORE REAL UPSTREAM QUIRKS, PRESERVED: the game starts with ELEVEN
// lives (`int8_t lifes = 11;`) but the Button-C restart resets to TEN -
// both values are upstream's own, kept exactly (upstream's own README
// documents only the 10). And `clickMole()`'s own miss branch writes
// `moleTimeoutMillis[...] = 0` on a hole that is already 0 by definition
// (it only reaches that branch because the hole was empty) - a harmless
// real no-op, carried over verbatim.
//
// TITLE SCREEN: upstream's own blocking `gb.titleScreen(F("Mole Control"),
// titleScreenImage)` - called once from setup(), and again (still blocking,
// mid-loop) whenever Button C is pressed to restart - is converted into an
// explicit MOLE_STATE_TITLE state dismissed by a genuine `gbPressed(BTN_A)`,
// the same "blocking widget -> explicit resumable state" treatment used
// throughout this project (see gamePong.c's own header comment). The real
// `Gamebuino::titleScreen()` library function draws its logo bitmap at a
// fixed (0,12) screen anchor - confirmed against the real Gamebuino.cpp
// source during gameFlappyBirdo.c's/gameUfoRace.c's/gameArmageddon.c's own
// ports of that same real function, and reused unchanged here. This game's
// own logo is 24x20, so at (0,12) it occupies y 12..31 and x 0..23 with
// zero clipping (left-aligned, exactly as real hardware draws it - not
// re-centered). The caption line is upstream's own real `initScreenText`
// string in upstream's own real `initScreenFont` (font5x7); the "PRESS A"
// prompt is this port's own added UI affordance, exactly as every other
// port restoring a real `titleScreen()` call has needed. No mole/timeout
// logic runs while the title is up, matching real hardware exactly (its own
// `loop()` is never entered while `titleScreen()` blocks) - only the
// millisecond clock keeps running, as described above.
//
// RESTART ORDERING PRESERVED: upstream's own Button C branch calls the
// blocking `titleScreen()` FIRST and only resets score/level/lives/timers
// afterward, once the title screen returns - so the old board is still in
// memory (though not on screen) for as long as the title is up, and, far
// more importantly, the `+ millis()` bug above samples the clock at the
// moment the title is DISMISSED, not at the moment C is pressed. Reproduced
// exactly via moleRestartPending: C enters MOLE_STATE_TITLE, and the whole
// reset runs at the moment Button A dismisses it.
//
// REAL BITMAP ART RESTORED: all three real upstream bitmaps - the 24x20
// `titleScreenImage` logo, the 16x10 `holeIcon` and the 16x10 `moleIcon` -
// are carried over byte-for-byte, with every `B00000000`-style Arduino
// binary literal converted to plain hex (this dialect has no such literal
// syntax) and each array's own body byte count verified against its own
// declared width/height header before being trusted (24 wide -> 3 bytes per
// row x 20 rows = 60; 16 wide -> 2 bytes per row x 10 rows = 20). Both
// icons declare a 16px width while only their leftmost 10 columns carry any
// content, which is upstream's own (the trailing columns are simply zero
// bits and draw nothing). Checked for the mask/fill-under-bitmap bug class
// found in gameFlappyBirdo.c/gameParachute.c: it does not apply - each of
// the three is drawn as a single self-contained sprite with no separate
// mask or fill layer anywhere in the real source, onto a framebuffer this
// shim's own gbUpdate() has already cleared, and a hole and a mole are
// mutually exclusive at any given grid cell so they never overlap.
//
// ONE PURE-PERFORMANCE REWRITE, OUTPUT PROVABLY IDENTICAL: upstream's own
// `drawGamePad()` builds the entire score/level/lives readout - 6 separate
// cursor-positioned prints - INSIDE its own 3x3 nested hole loop, so all
// six are drawn 9 times per frame at the exact same coordinates with the
// exact same values. On real hardware that is merely wasteful; here it is
// ~120,000 instructions of a ~250,000-instruction frame budget spent
// redrawing identical text (see VIRCON32_C_DIALECT.md section 16 - every
// call costs a flat ~10+2*argcount instructions, and a font3x5 glyph is
// 24 per-pixel iterations). The readout is hoisted out of the loop below.
// The output is byte-for-byte identical, not merely similar: every draw
// this game makes is a transparent "on"-bits-only BLACK draw (upstream
// never calls `setColor()` at all outside its own game-over box, and
// gbSetColor(GB_BLACK) sets color and background alike, so nothing here
// ever paints a WHITE or opaque-background pixel), which makes every draw
// a pure OR into the framebuffer - so both the repetition and the relative
// order of the hole sprites and the text are provably irrelevant to the
// resulting pixels. The identical "hoist/gate the redundant call, not just
// the work inside it" lesson is documented project-wide in CLAUDE.md's own
// performance-pass section.
//
// SOUND: upstream has none at all - no `gb.sound.*` call anywhere in the
// real sketch (its `#define BuzzerPin 3;` is one of the unused,
// unusable-as-written pin macros described above). None was invented, so
// this port is silent exactly like the original.
//
// EEPROM: upstream has none - no `#include <EEPROM.h>`, no `EEPROM.read()`/
// `.write()` call anywhere in the real source (confirmed by direct grep),
// and no highscore concept of any kind: the score readout is a live,
// session-only counter that the Button-C restart zeroes. None was invented,
// matching this project's own "don't invent a highscore real upstream never
// had" precedent.
//
// NO SHIM GAPS: every primitive this port needs already exists
// (gbDrawBitmap, gbDrawRect, gbFillRect, gbPrintString/gbPrintNumber,
// gbSetFont with the real gbFont3x5/gbFont5x7 tables, gbSetColor with the
// real GB_GRAY dither, gbPressed, arand). Nothing in gamebuinoShim.h/.c or
// any other shared file was changed.

// -----------------------------------------------------------------------------
// Layout/tuning constants - upstream's own real #define values, verbatim
// (prefixed for this cartridge's single flat namespace). Every one of these
// is a single literal token upstream, so none of them can suffer the
// unparenthesized-multi-term-macro hazard gameSkibuino.c's own header
// comment documents.
// -----------------------------------------------------------------------------

#define MOLE_INIT_SCREEN_TEXT "Mole Control"

#define MOLE_HOLES_START_X 5
#define MOLE_HOLES_START_Y 8
#define MOLE_HOLES_STEP_X 15
#define MOLE_HOLES_STEP_Y 12

#define MOLE_HAMMER_OFFSET_X -1
#define MOLE_HAMMER_OFFSET_Y -1
#define MOLE_HAMMER_WIDTH 12
#define MOLE_HAMMER_HIGHT 13

#define MOLE_SCORE_X 50
#define MOLE_SCORE_STRING_Y 0
#define MOLE_SCORE_STRING_TEXT "Score:"
#define MOLE_SCORE_Y 6
#define MOLE_LEVEL_STRING_Y 12
#define MOLE_LEVEL_STRING_TEXT "Level:"
#define MOLE_LEVEL_Y 18
#define MOLE_LIFES_STRING_Y 24
#define MOLE_LIFES_STRING_TEXT "Lifes:"
#define MOLE_LIFES_Y 30

#define MOLE_GAMEOVER_X 15
#define MOLE_GAMEOVER_Y1 17
#define MOLE_GAMEOVER_TEXT1 "Game"
#define MOLE_GAMEOVER_Y2 27
#define MOLE_GAMEOVER_TEXT2 "Over"
#define MOLE_GAMEOVER_SQUARE_START_X 13
#define MOLE_GAMEOVER_SQUARE_START_Y 15
#define MOLE_GAMEOVER_SQUARE_WIDTH 27
#define MOLE_GAMEOVER_SQUARE_HEIGHT 21

#define MOLE_INITAL_SCORE_LEVEL_UP 5
#define MOLE_ADD_SCORE_LEVEL_UP 0.5

#define MOLE_INITIAL_MOLE_TIMEOUT_TIME 5000
#define MOLE_END_MOLE_TIMOUT_TIME 500
#define MOLE_SUBST_MOLE_TIMEOUT_TIME 200

#define MOLE_INITAL_MIN_TIME_NEXT_MOLE 2500
#define MOLE_END_MIN_TIME_NEXT_MOLE 300
#define MOLE_SUBST_MIN_TIME_NEXT_MOLE 300

#define MOLE_INITAL_MAX_TIME_NEXT_MOLE 5000
#define MOLE_END_MAX_TIME_NEXT_MOLE 500
#define MOLE_SUBST_MAX_TIME_NEXT_MOLE 100

// Upstream's own literal `debounceNextMillis = millis() + 200` (its own
// `#define debounceDelay 100` is never used - see this file's own header
// comment).
#define MOLE_DEBOUNCE_MILLIS 200

// One real 20fps logic tick, in milliseconds - see this file's own header
// comment on the virtual clock.
#define MOLE_MS_PER_TICK 50

enum MoleState
{
    MOLE_STATE_TITLE = 0,
    MOLE_STATE_PLAY = 1
};

// -----------------------------------------------------------------------------
// Real upstream art - see this file's own header comment.
// -----------------------------------------------------------------------------

// 24x20 `titleScreenImage`
int moleTitleBitmap[62]=
{
    24, 20,
    0x0F, 0xFF, 0x00,
    0x0F, 0xFF, 0x00,
    0x30, 0x00, 0xC0,
    0x30, 0x00, 0xC0,
    0xFF, 0xFF, 0xF0,
    0xFF, 0xFF, 0xF0,
    0xFF, 0xFF, 0xF0,
    0xFF, 0xFF, 0xF0,
    0xFF, 0x0F, 0xF0,
    0xFF, 0x0F, 0xF0,
    0xC0, 0x00, 0x30,
    0xC0, 0x00, 0x30,
    0xCF, 0x0F, 0x30,
    0xCF, 0x0F, 0x30,
    0xC0, 0xF0, 0x30,
    0xC0, 0xF0, 0x30,
    0xF0, 0x00, 0xF0,
    0xF0, 0x00, 0xF0,
    0x0F, 0xFF, 0x00,
    0x0F, 0xFF, 0x00
};

// 16x10 `holeIcon` - an empty hole (only the bottom 4 rows carry content)
int moleHoleIcon[22]=
{
    16, 10,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x3F, 0x00,
    0xFF, 0xC0,
    0xFF, 0xC0,
    0x3F, 0x00
};

// 16x10 `moleIcon` - a mole poking out of the same hole
int moleMoleIcon[22]=
{
    16, 10,
    0x3F, 0x00,
    0x40, 0x80,
    0xFF, 0xC0,
    0xFF, 0xC0,
    0xF3, 0xC0,
    0x80, 0x40,
    0xB3, 0x40,
    0x8C, 0x40,
    0xC0, 0xC0,
    0x3F, 0x00
};

// -----------------------------------------------------------------------------
// State - upstream's own globals, one for one.
// -----------------------------------------------------------------------------

int moleHammerX = 0;
int moleHammerY = 0;

int moleScore = 0;
int moleLifes = 11; // upstream's own real initial value - its restart path uses 10

int moleLevel = 1;
int moleScoreLevelUpNow = MOLE_INITAL_SCORE_LEVEL_UP;
int moleScoreSinceLastLevel = 0;

int moleMoleTimeoutTimeNow = MOLE_INITIAL_MOLE_TIMEOUT_TIME;
int moleNowMinTimeNextMole = MOLE_INITAL_MIN_TIME_NEXT_MOLE;
int moleNowMaxTimeNextMole = MOLE_INITAL_MAX_TIME_NEXT_MOLE;

// Upstream initializes this absolute deadline from a plain duration
// constant (`int32_t level_timeNextMole = level_nowMaxTimeNextMole;`) - see
// this file's own header comment.
int moleTimeNextMole = MOLE_INITAL_MAX_TIME_NEXT_MOLE;

// Absolute deadline (in virtual ms) each hole's own mole burrows away at,
// or 0 for "no mole here". Indexed (3 * row) + column, upstream's own.
int moleTimeoutMillis[9];

int moleMillis = 0;
int moleDebounceNextMillis = 0;

int moleState = MOLE_STATE_TITLE;
// Upstream's Button C branch shows the title screen first and only resets
// the game once it is dismissed - see this file's own header comment.
bool moleRestartPending = false;

// -----------------------------------------------------------------------------
// Game logic - direct ports of upstream's own same-named functions.
// -----------------------------------------------------------------------------

// Truncates to a real signed 16-bit value, reproducing what an AVR
// `int16_t` assignment does to an out-of-range sum - needed at exactly
// three sites, all in the restart path (see this file's own header comment
// on the restart bug).
int moleNarrowS16( int value )
{
    int narrowed = value & 65535;
    if( narrowed > 32767 ) narrowed = narrowed - 65536;
    return narrowed;
}

void moleSetLevel()
{
    if( moleScoreSinceLastLevel >= moleScoreLevelUpNow )
    {
        moleLevel++;
        moleScoreSinceLastLevel = 0;
        moleScoreLevelUpNow = MOLE_INITAL_SCORE_LEVEL_UP + (int)( (float)moleLevel * MOLE_ADD_SCORE_LEVEL_UP );

        moleNowMinTimeNextMole = moleNowMinTimeNextMole - MOLE_SUBST_MIN_TIME_NEXT_MOLE;
        if( moleNowMinTimeNextMole < MOLE_END_MIN_TIME_NEXT_MOLE ) moleNowMinTimeNextMole = MOLE_END_MIN_TIME_NEXT_MOLE;
        moleNowMaxTimeNextMole = moleNowMaxTimeNextMole - MOLE_SUBST_MAX_TIME_NEXT_MOLE;
        if( moleNowMaxTimeNextMole < MOLE_END_MAX_TIME_NEXT_MOLE ) moleNowMaxTimeNextMole = MOLE_END_MAX_TIME_NEXT_MOLE;
        if( moleNowMaxTimeNextMole < moleNowMinTimeNextMole ) moleNowMaxTimeNextMole = moleNowMinTimeNextMole; // let max never be smaler than min
        moleMoleTimeoutTimeNow = moleMoleTimeoutTimeNow - MOLE_SUBST_MOLE_TIMEOUT_TIME;
        if( moleMoleTimeoutTimeNow < MOLE_END_MOLE_TIMOUT_TIME ) moleMoleTimeoutTimeNow = MOLE_END_MOLE_TIMOUT_TIME;
    }
}

void moleClickMole()
{
    int index = ( 3 * moleHammerY ) + moleHammerX;

    if( moleTimeoutMillis[ index ] > 0 )
    {
        moleTimeoutMillis[ index ] = 0;
        moleScore++;
        moleScoreSinceLastLevel++;
        moleSetLevel();
    }
    else
    {
        if( moleLifes > 0 ) moleLifes--;
        moleTimeoutMillis[ index ] = 0; // already 0 by definition - upstream's own real no-op, kept
    }
}

// A mole left up past its own deadline burrows away by itself and costs a
// life. Every expired hole is handled on the same tick, so several lives
// can genuinely be lost at once - upstream's own behavior.
void moleCheckTimoutMoles()
{
    int it;

    for( it = 0; it < 9; it++ )
    {
        if( ( moleTimeoutMillis[ it ] < moleMillis ) && ( moleTimeoutMillis[ it ] > 0 ) )
        {
            if( moleLifes > 0 ) moleLifes--;
            moleTimeoutMillis[ it ] = 0;
        }
    }
}

// Upstream's own randGenMoles(). The next deadline is always exactly
// `moleNowMinTimeNextMole` away (upstream's own `constrain(min, min, max)`
// reduces to `min`), and a spawn attempt on an already-occupied hole simply
// does nothing - both real, deliberate behavior, see this file's own header
// comment.
void moleRandGenMoles()
{
    int position;

    if( moleMillis > moleTimeNextMole )
    {
        moleTimeNextMole = moleMillis + moleNowMinTimeNextMole;

        // one roll used for both the test and the fill, matching upstream's
        // own two reads of an unchanged rand_counter
        position = arand( 9 );
        if( !moleTimeoutMillis[ position ] ) moleTimeoutMillis[ position ] = moleMillis + moleMoleTimeoutTimeNow;
    }
}

// -----------------------------------------------------------------------------
// Drawing - direct port of upstream's own drawGamePad()/gameOver().
// -----------------------------------------------------------------------------

void moleDrawGamePad()
{
    int ix;
    int iy;
    int xNow;
    int yNow;

    for( ix = 0; ix < 3; ix++ )
    {
        for( iy = 0; iy < 3; iy++ )
        {
            // calc x and y
            xNow = MOLE_HOLES_START_X + ( ix * MOLE_HOLES_STEP_X );
            yNow = MOLE_HOLES_START_Y + ( iy * MOLE_HOLES_STEP_Y );

            // draw moles and holes
            if( moleTimeoutMillis[ ( 3 * iy ) + ix ] > 0 ) gbDrawBitmap( xNow, yNow, moleMoleIcon );
            else gbDrawBitmap( xNow, yNow, moleHoleIcon );

            // draw hammer
            if( ( ix == moleHammerX ) && ( iy == moleHammerY ) )
              gbDrawRect( xNow + MOLE_HAMMER_OFFSET_X, yNow + MOLE_HAMMER_OFFSET_Y, MOLE_HAMMER_WIDTH, MOLE_HAMMER_HIGHT );
        }
    }

    // display score and lives - drawn once here rather than 9 times inside
    // the loop above, which produces byte-identical output (see this file's
    // own header comment on the one pure-performance rewrite)
    gbSetFont( gbFont3x5 ); // upstream's own `#define gamepad_font font3x5`
    gbCursorX = MOLE_SCORE_X;
    gbCursorY = MOLE_SCORE_STRING_Y;
    gbPrintString( MOLE_SCORE_STRING_TEXT );
    gbCursorX = MOLE_SCORE_X;
    gbCursorY = MOLE_SCORE_Y;
    gbPrintNumber( moleScore );
    gbCursorX = MOLE_SCORE_X;
    gbCursorY = MOLE_LEVEL_STRING_Y;
    gbPrintString( MOLE_LEVEL_STRING_TEXT );
    gbCursorX = MOLE_SCORE_X;
    gbCursorY = MOLE_LEVEL_Y;
    gbPrintNumber( moleLevel );
    gbCursorX = MOLE_SCORE_X;
    gbCursorY = MOLE_LIFES_STRING_Y;
    gbPrintString( MOLE_LIFES_STRING_TEXT );
    gbCursorX = MOLE_SCORE_X;
    gbCursorY = MOLE_LIFES_Y;
    gbPrintNumber( moleLifes );
}

void moleGameOver()
{
    gbSetColor( GB_GRAY );
    gbFillRect( MOLE_GAMEOVER_SQUARE_START_X, MOLE_GAMEOVER_SQUARE_START_Y, MOLE_GAMEOVER_SQUARE_WIDTH, MOLE_GAMEOVER_SQUARE_HEIGHT );
    gbSetColor( GB_BLACK );
    gbDrawRect( MOLE_GAMEOVER_SQUARE_START_X, MOLE_GAMEOVER_SQUARE_START_Y, MOLE_GAMEOVER_SQUARE_WIDTH, MOLE_GAMEOVER_SQUARE_HEIGHT );
    gbCursorX = MOLE_GAMEOVER_X;
    gbCursorY = MOLE_GAMEOVER_Y1;
    gbSetFont( gbFont5x7 ); // upstream's own `#define gameover_font font5x7`
    gbPrintString( MOLE_GAMEOVER_TEXT1 );
    gbCursorX = MOLE_GAMEOVER_X;
    gbCursorY = MOLE_GAMEOVER_Y2;
    gbPrintString( MOLE_GAMEOVER_TEXT2 );
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

// Restored real gb.titleScreen(F("Mole Control"), titleScreenImage) screen -
// see this file's own header comment on the confirmed real (0,12) logo
// anchor and the added "PRESS A" prompt.
void moleUpdateTitle()
{
    int it;

    gbSetColor( GB_BLACK );
    gbSetFont( gbFont5x7 ); // upstream's own `#define initScreenFont font5x7`

    gbCursorX = 6;
    gbCursorY = 2;
    gbPrintString( MOLE_INIT_SCREEN_TEXT );

    gbDrawBitmap( 0, 12, moleTitleBitmap );

    gbCursorX = 30;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbPickRandomSeed(); // upstream calls this right after every titleScreen() return

        if( moleRestartPending )
        {
            // == upstream's own real post-titleScreen() restart block, run
            // at exactly the moment upstream runs it (see this file's own
            // header comment on the restart ordering AND on the real
            // `+ millis()` bug the three narrowed lines below reproduce)
            moleRestartPending = false;

            moleScore = 0;
            moleLevel = 1;
            moleLifes = 10;

            moleScoreLevelUpNow = MOLE_INITAL_SCORE_LEVEL_UP;
            moleNowMinTimeNextMole = moleNarrowS16( MOLE_INITAL_MIN_TIME_NEXT_MOLE + moleMillis );
            moleNowMaxTimeNextMole = moleNarrowS16( MOLE_INITAL_MAX_TIME_NEXT_MOLE + moleMillis );
            moleMoleTimeoutTimeNow = moleNarrowS16( MOLE_INITIAL_MOLE_TIMEOUT_TIME + moleMillis );
            moleScoreSinceLastLevel = 0;

            for( it = 0; it < 9; it++ )
              moleTimeoutMillis[ it ] = 0;
        }

        moleState = MOLE_STATE_PLAY;
    }
}

// Upstream's own loop() body, in upstream's own order: the debounce-gated
// input block, then the draw, then the timeout/spawn logic (or the game
// over box).
void moleUpdatePlay()
{
    bool btnPressed = false;

    if( moleMillis > moleDebounceNextMillis )
    {
        if( gbPressed( BTN_DOWN ) )
        {
            moleHammerY++;
            if( moleHammerY > 2 ) moleHammerY = 2;
            btnPressed = true;
        }
        if( gbPressed( BTN_UP ) )
        {
            moleHammerY--;
            if( moleHammerY < 0 ) moleHammerY = 0;
            btnPressed = true;
        }
        if( gbPressed( BTN_LEFT ) )
        {
            moleHammerX--;
            if( moleHammerX < 0 ) moleHammerX = 0;
            btnPressed = true;
        }
        if( gbPressed( BTN_RIGHT ) )
        {
            moleHammerX++;
            if( moleHammerX > 2 ) moleHammerX = 2;
            btnPressed = true;
        }
        if( gbPressed( BTN_A ) )
        {
            moleClickMole();
            btnPressed = true;
        }
        if( gbPressed( BTN_C ) ) // restart
        {
            // Upstream's own C branch deliberately does NOT set btnPressed,
            // so it arms no debounce of its own - kept exactly. The reset
            // itself runs once the title screen is dismissed, matching
            // upstream's own blocking-call ordering.
            moleRestartPending = true;
            moleState = MOLE_STATE_TITLE;
            return;
        }

        if( btnPressed ) moleDebounceNextMillis = moleMillis + MOLE_DEBOUNCE_MILLIS;
    }

    moleDrawGamePad();

    if( moleLifes > 0 )
    {
        moleCheckTimoutMoles();
        moleRandGenMoles();
    }
    else moleGameOver();
}

void gameMoleControl_init()
{
    int it;

    gbBegin();
    // real Display::Display()'s own default - sets color AND background
    // alike, so a launch never inherits another game's leaked background
    gbSetColor( GB_BLACK );
    gbSetFont( gbFont5x7 ); // upstream's own setup()-time setFont(initScreenFont)

    moleHammerX = 0;
    moleHammerY = 0;

    moleScore = 0;
    moleLifes = 11;

    moleLevel = 1;
    moleScoreLevelUpNow = MOLE_INITAL_SCORE_LEVEL_UP;
    moleScoreSinceLastLevel = 0;

    moleMoleTimeoutTimeNow = MOLE_INITIAL_MOLE_TIMEOUT_TIME;
    moleNowMinTimeNextMole = MOLE_INITAL_MIN_TIME_NEXT_MOLE;
    moleNowMaxTimeNextMole = MOLE_INITAL_MAX_TIME_NEXT_MOLE;
    moleTimeNextMole = MOLE_INITAL_MAX_TIME_NEXT_MOLE;

    for( it = 0; it < 9; it++ )
      moleTimeoutMillis[ it ] = 0;

    moleMillis = 0;
    moleDebounceNextMillis = 0;
    moleRestartPending = false;
    moleState = MOLE_STATE_TITLE;
}

void gameMoleControl_update()
{
    if( !gbUpdate() ) return;

    // The virtual millisecond clock keeps running in every state, exactly
    // like real millis() does while gb.titleScreen() blocks - see this
    // file's own header comment.
    moleMillis = moleMillis + MOLE_MS_PER_TICK;

    if( moleState == MOLE_STATE_PLAY ) moleUpdatePlay();
    // re-checked, not chained with `else`: a Button C restart inside
    // moleUpdatePlay() switches to the title state mid-tick, and this draws
    // it on that same tick instead of leaving one blank frame behind
    if( moleState == MOLE_STATE_TITLE ) moleUpdateTitle();

    gbRenderFrame();
}
