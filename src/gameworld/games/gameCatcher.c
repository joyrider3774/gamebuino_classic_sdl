#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Gamebuino-Catcher (qubist, License: none specified upstream - no LICENSE
// file in the repo - github.com/qubist/Gamebuino-Catcher). A physics-based
// catch game: nudge a small rocket-thruster-equipped "catcher" around the
// screen (accelerating with the D-pad, everything drifting under real
// gravity/wind/air-resistance) to catch balls that fall from the top of the
// screen before they hit the ground. Each round ("level") drops one more
// ball than the last; missing any ball repeats the same level; catching
// every ball advances to the next, with gravity creeping up and (from level
// 2 on) a random left/right wind kicking in, shown by an animated flag.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every one of upstream's own bare
// globals/functions was given a `catch`-prefixed name (this single compiled
// cartridge shares one flat global namespace across every game, confirmed
// unused by every other game staged in this project so far).
//
// STATE MACHINE: upstream's own blocking `gb.titleScreen(F(""), logo)` -
// called once in setup(), and again from inside loop() (via a full re-call
// of setup() itself - see the Button-C quirk below) - was converted into an
// explicit CATCH_STATE_TITLE/CATCH_STATE_LEVELUP/CATCH_STATE_PLAY state
// machine, matching the "blocking loop -> explicit resumable state"
// treatment used throughout this project (see gamePong.c's own header
// comment). Real Gamebuino's own titleScreen() waits specifically for
// Button A, matching Vircon32's own menu-select button - reused directly
// here as the dismiss gesture. Upstream's own title text argument was blank
// (`F("")`), so "PRESS A" below the logo is this port's own UI text (not a
// literal upstream string), matching gameFlappyBirdo.c's/gameSnakeAbc.c's
// own identical treatment of a real-logo-plus-blank-title title screen.
//
// Upstream's own `if (level_up) { ...; return; }` block (recomputes the new
// round's ball count/gravity/wind/next-spawn-delay, shows a 2-second
// "-- Catcher! --" / "Missed N" / "Level N" screen, then returns without
// running any of that tick's normal game logic) is ported as
// `catchBeginLevelUp()` (the one-time per-round setup) plus
// `catchUpdateLevelUp()` (the per-tick redraw + countdown), both invoked
// together, in the same tick the flag is first noticed, from the top of
// `catchUpdatePlay()` - exactly mirroring upstream's own "notice the flag,
// compute the new round, draw the screen, return without drawing gameplay"
// shape for that specific tick; subsequent ticks dispatch straight to
// `catchUpdateLevelUp()` while `catchState == CATCH_STATE_LEVELUP` (matching
// upstream's own `delayt > 0` early-return, which skips straight past the
// `level_up` check too). The very first entry into level 1 (at boot, right
// after the title screen's own Button-A dismiss) is NOT special-cased
// separately - it goes through this exact same path, since `catchLevelUp`
// starts `true` from `catchResetGame()` exactly like upstream's own
// `level_up` starts `true` from `setup()`.
//
// TIMING: upstream gates two real things on `millis()` (Vircon32 has no
// wall-clock primitive to read, the same gap gameSnakeAbc.c's own header
// comment already documents and works around identically):
//   - `delayt` (the level-up screen's own 2-second pause) -> `catchLevelUpTimer`,
//     a tick countdown. Since upstream never itself calls
//     `gb.setFrameRate()` for anything other than the `setFrameRate(10)` in
//     setup() (kept verbatim below), and this shim's own `gbUpdate()` tick
//     throttle honors that exactly (confirmed directly against its own
//     accumulator - it returns true exactly `fps` times per 60 real engine
//     ticks), 1 game tick is exactly 100ms at this game's real 10fps, so
//     2000ms becomes exactly 20 ticks (`CATCH_LEVELUP_TICKS`) - no rounding.
//   - `bt`/`dt` (the per-round random ball-spawn timer, re-armed as
//     `millis() + random(0, next_ball_max)` every time it fires) ->
//     `catchSpawnTimer`, a tick countdown re-armed as `arand(catchNextBallMaxTicks)`
//     every time it reaches zero. `next_ball_max` itself (ms, 10000 down to
//     a floor of 3000, in steps of 500) becomes `catchNextBallMaxTicks`
//     (ticks, 100 down to a floor of 30, in steps of 5) - every constant
//     divided by exactly 100, matching the same 100ms-per-tick relationship,
//     again no rounding needed.
// `gb.frameCount` (used for two real animation-parity checks: the catcher's
// own thruster-flame flicker, and the wind flag's own advance-every-other-
// tick pacing) maps directly onto this shim's own real `gbFrameCount`, a
// plain incrementing tick counter declared in gamebuinoShim.h/.c.
//
// DATA STRUCTURE: upstream's own `sprite` struct carries a `const unsigned
// char *bitmap` pointer (either `catcher_bm` or `ball_bm`) and a `void
// (*draw_fun)(void*)` function pointer (either `&draw_catcher` or `0`/never
// called, since every ball's own `draw_fun` stays null for its whole
// lifetime). This dialect's own reference doc (`VIRCON32_C_DIALECT.md`)
// documents function pointers and pointer struct fields as now supported by
// the current compiler - but no other game ported into this project so far
// has actually exercised either inside a struct, so (matching this
// project's own established "don't be the first file betting on an
// unproven-here pattern" caution, the same reasoning gamePong.c/
// gameAgaruino.c give for avoiding `switch`) `CatchSprite` below instead
// carries a plain `int kind` (0=catcher, 1=ball) and every draw call/thrust-
// effect dispatch is an explicit `if`/`else` on it or on the sprite's own
// index - there were only ever two distinct bitmap values and one distinct
// draw-function value in the real game anyway, so nothing is lost.
// `ax`/`ay` were renamed `vx`/`vy` purely for readability (they are used as
// velocity throughout `move()`, not acceleration - upstream's own naming,
// not this port's) - a naming-only change, not a behavioral one.
// Sprite-array compaction (`kill_ball()`'s own shift-down-by-one loop) is
// ported as an explicit field-by-field copy rather than a whole-struct `=`
// assignment, for the same "don't be the first to rely on an unproven-here
// pattern" reason (whole-struct assignment is documented as supported too,
// but likewise untested by any other file in this project so far, and a
// 7-field manual copy is a trivial, guaranteed-safe substitute at this
// array's real max size of 10).
//
// `flags_bm[]` (upstream's own array of 6 bitmap pointers, flag1_bm..
// flag6_bm, indexed by `(int)flap`) is similarly NOT ported as an array of
// bitmap pointers - `catchDrawFlag(index, x, y, flip)` below dispatches to
// the right named bitmap via a plain `if`/`else if` chain instead, avoiding
// betting on pointer-array-of-bitmaps, a pattern no other file here has
// exercised either.
//
// REAL BITMAP ART RESTORED - every one of upstream's own real
// `const byte NAME[] PROGMEM = {...}` bitmaps (logo, catcher_bm, ball_bm,
// flag0_bm..flag6_bm) is reproduced below as a real
// `int[N] catchXxxBitmap = { width, height, byte0, byte1, ... }` array (this
// dialect's own `int[N] name` array-declaration order, not C's `int
// name[N]`), exactly the format `gbDrawBitmap()`/`gbDrawBitmapRotated()`
// expect - not a single one was replaced with a geometric placeholder.
// `catcher_bm`/`ball_bm`/`flag0_bm` use upstream's own Arduino
// `B00000000`-style binary literals (not valid syntax in this dialect) -
// each was hand-converted to hex and double-checked bit by bit:
//   catcher_bm: B11000011,B11000011,B01111110 -> 0xC3,0xC3,0x7E
//   ball_bm:    B00011000,B01100110,B00011000 -> 0x18,0x66,0x18
//   flag0_bm:   B10000000,B11000000,B10100000,B10010000,B11010000,
//               B10110000,B10000000,B10000000
//               -> 0x80,0xC0,0xA0,0x90,0xD0,0xB0,0x80,0x80
//               (upstream's own adjacent `// Old hex flag0_bm` comment shows
//               a DIFFERENT, stale set of bytes from an earlier version of
//               this same bitmap - not trusted here; the values above were
//               computed fresh from the real, currently-active B-literal
//               array, not copied from that comment.)
// flag1_bm..flag6_bm were already real hex in the upstream source and are
// copied verbatim (no conversion needed) - drawn via `catchDrawFlag()`
// above. No mask/fill layer needed underneath any of these: every one is a
// small, fully self-contained outline sprite (checked directly against
// their real decoded bits before trusting that), not a bitmap that upstream
// first backed with a separate solid GRAY/fillRect layer the way
// gameFlappyBirdo.c's own pipe sprite needed (see that file's own header
// comment for the bug class this project found and fixed twice there) - so
// there is no bleed-through risk here to guard against.
//
// A REAL BUG FOUND IN UPSTREAM'S OWN BITMAP DATA: `logo[]` declares itself
// `{ 84, 48, ... }` (width, height) but its real PROGMEM byte array only
// actually contains 440 data bytes - exactly 40 rows of the real 11
// bytes-per-row (`ceil(84/8)`) a width-84 bitmap needs, not the 48 rows its
// own height header claims (which would need 528 bytes, 88 more than are
// actually present). Counted directly and independently twice against the
// real upstream source before trusting this conclusion (once by raw
// character/comma counting, once by re-parsing each of the 40 real data
// lines individually) - not a transcription slip made in this port. On real
// hardware this means `Display::drawBitmap()` would read 8 rows' worth (88
// bytes) of whatever real PROGMEM data happens to sit immediately after
// `logo[]` in flash - genuinely undefined/build-dependent content, not
// necessarily even blank. Restored below with the height field corrected to
// **40** (matching the real data actually present) rather than either (a)
// leaving it at 48, which in this shim would make `gbDrawBitmap()` read 88
// `int` cells past the end of this array's own real declared size - a
// genuine out-of-bounds read of whatever global happens to be declared
// right after it in this shared, linker-less single binary, a real risk
// class this project has already chosen to defensively avoid even where
// upstream's own equivalent was comparatively harmless (see
// gameSnakeAbc.c's own `sabcSnakeMaxSize` clamp and its header comment's
// reasoning) - or (b) inventing 8 rows of pixel data that were never part
// of the real upstream art. Net visible effect: the title logo now
// occupies the top 40 of the real 48 display rows and leaves the bottom 8
// blank, instead of whatever real hardware happens to render into its own
// undefined overrun there.
//
// `gb.battery.show = false;` was dropped outright (a real-hardware-only
// cosmetic battery indicator, matching every other port's own treatment).
// `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a documented no-op
// (this project's own established precedent for every upstream
// `randomSeed()`-style call). `random(0, N)`/`random(N)` became `arand(N)`
// throughout (this dialect's own established RNG helper). `any_button_pressed()`
// is defined upstream but never actually called anywhere in the real
// 486-line source (confirmed by checking every remaining reference before
// dropping it) - left out entirely, matching gameSnakeAbc.c's own
// precedent for dropping confirmed-dead upstream code. Upstream never calls
// any `EEPROM.*` function anywhere (no persistence exists in the real
// game), so none was added here either.
//
// Multiple real upstream quirks were found while reading the source
// closely and preserved deliberately rather than "fixed", per this
// project's own "preserve real upstream behavior/bugs by default" norm:
//
// 1. **Exact-bitmask-only movement/thrust.** Both the catcher's own D-pad
//    acceleration (upstream's `switch(pad_hit){ case PAD_L: ...}`) and its
//    thruster-flame visual (`draw_catcher()`'s own `switch(pad_hit)`) only
//    ever match when `pad_hit` is EXACTLY one single direction/button bit -
//    holding two or more buttons at once (e.g. Left+Up together) matches
//    none of the cases, so the catcher gets neither acceleration nor a
//    flame that tick. Ported below as `if (catchPadHit == CATCH_PAD_L)` /
//    `else if (... == CATCH_PAD_R)` / etc (an `if`/`else-if` chain
//    reproducing `switch`'s own real fallthrough-free case-matching
//    exactly, matching gamePong.c's/gameAgaruino.c's own precedent of
//    avoiding `switch` itself, whose support in this dialect is unproven).
// 2. **`catchMove()`'s three independent (not `else`-chained) per-ball
//    collision checks.** Upstream's own `move()` has three SEPARATE `if`
//    blocks per ball each tick - "caught by the catcher" (kills the ball,
//    scores a point), "bounced off the catcher's own sides" (an elastic
//    velocity swap), and "hit the ground" (kills the ball, costs a point) -
//    with no `return`/`else` between them. Since the "caught" check can
//    call `catchKillBall(i)` (which immediately shifts the sprite array
//    down, so index `i` now holds what used to be sprite `i+1`, or is
//    stale if `i` was the last live sprite), the SAME tick's side-bounce
//    and ground-miss checks right after still read `catchSprites[i]` -
//    now referring to a different, already-shifted sprite (or leftover
//    data past the new, smaller `catchSpriteCount`). Ported exactly as
//    upstream wrote it (three unguarded, sequential `if` blocks, no early
//    return after a kill) rather than "fixed" into an early-return chain -
//    a genuine, if obscure, piece of the original game's own real
//    collision feel (occasionally a caught ball's late, stale side-bounce/
//    ground-miss check fires against a different, wrong sprite that same
//    tick), not a porting mistake.
// 3. **A real round-1 asymmetry.** `setup()`'s own direct `add_ball()` call
//    (which upstream always makes once, unconditionally, right after
//    `reset_catcher()`) bypasses the `level_balls` spawn-timer entirely -
//    so round 1 genuinely starts with one ball ALREADY on screen, plus up
//    to one MORE spawned later via the timer (`level_balls` is set to
//    `level`, which becomes 1 for round 1) - two balls total for round 1.
//    Every later round N instead starts from zero balls on screen (the
//    level-up trigger only fires once `catchSpriteCount == 1`, i.e. no
//    balls left) and spawns exactly N balls via the timer alone. Preserved
//    exactly (`catchResetGame()` keeps upstream's own unconditional
//    `catchAddBall()` call site) rather than smoothed into a uniform
//    "N balls per round N" rule.
// 4. **A round only advances the level on a clean pass.** `if
//    (balls_missed == 0) level++;` - missing even one ball in a round
//    repeats the SAME level number next round (though `next_ball_max`'s own
//    decay, `gravity`'s own increase, and a freshly rerolled `wind` all
//    still advance regardless of pass/fail, matching upstream exactly).
// 5. **Button C's own mid-game "restart" was a genuine, real correctness
//    hazard, not a clean soft-reset - FIXED HERE, NOT PRESERVED, on direct
//    request once flagged as a live risk, not just a cosmetic quirk.**
//    Upstream's own
//    `pad_check()` (which is what clears a bit out of the persistent
//    `pad_hit` bitmask, via `gb.buttons.released(...)`) is only ever called
//    from the very tail of the normal per-tick gameplay path - never while
//    `delayt > 0` (the level-up pause) or from inside the real, separately
//    blocking `gb.titleScreen()` call itself (which setup() re-enters every
//    time Button C triggers a restart, since upstream's own fix for "how do
//    I get back to the title screen mid-game" is to just call `setup()`
//    again outright). Ported faithfully: `catchPadCheck()` below is
//    likewise only ever called from the tail of `catchUpdatePlay()`, never
//    from `catchUpdateTitle()`/`catchUpdateLevelUp()` - exactly mirroring
//    upstream's own real call-site structure, not a new gap this port
//    introduced. The concern: this shim's own `gbPressed()`/`gbReleased()`
//    are single-tick edge pulses computed unconditionally by `gbUpdate()`
//    every logical tick regardless of whether any game code reads them
//    that specific tick (confirmed directly in `gamebuinoShim.c`'s own
//    `gbUpdateButtons()`) - so if a player releases Button C at any point
//    while the (re-shown) title screen or the following level-up screen is
//    up, which is the ordinary, easy way to use this feature (tap C, let
//    go, then press A), that release edge is never read while it's true
//    and is gone for good by the time `catchPadCheck()` next runs. That
//    would leave `catchPadHit` stuck holding exactly `CATCH_PAD_C` going
//    into the first real gameplay tick after the level-up screen closes -
//    which is also the exact tick `catchUpdatePlay()` itself checks `if
//    (catchPadHit == CATCH_PAD_C) catchResetGame();`, so it could
//    immediately restart again, and again, effectively soft-locking the
//    game the first time this feature is used (unless some OTHER button
//    happens to be held at that exact instant). **Since confirmed against
//    the real library source** (`more games/Gamebuino-Classic/utility/
//    Buttons.cpp`, available in this project but not checked when this
//    port was first written): real `Buttons::update()` is called
//    unconditionally every real tick regardless of what game code reads
//    that tick (identical to this shim's own `gbUpdateButtons()`), and
//    real `released()` is a genuine single-tick pulse (`states[button] ==
//    0xFF`, reset to idle the very next tick) - the exact same "lost if
//    nothing reads it that tick" shape this shim's own `gbReleased()` has.
//    This is real, load-bearing upstream behavior, not a porting artifact:
//    a real cartridge has this exact soft-lock risk too. Fixed here by
//    clearing `catchPadHit` at the top of `catchResetGame()` - the
//    defensive reset upstream itself never had.
// 6. **The wind flag genuinely cycles through 6 real frames, not 5.**
//    `FLAG_COUNT` is 5, but it only gates the wrap-around point
//    (`if (flap > FLAG_COUNT) flap = 0;`, i.e. `flap` legitimately reaches
//    5 - selecting `flag6_bm`, the 6th real bitmap - before wrapping) - the
//    name is a little misleading but the real animation (6 distinct
//    frames, `flag1_bm`..`flag6_bm`) is preserved exactly.
//
// `gb.display.setFont(font5x7)` is restored as a real `gbSetFont(
// gbFont5x7)` call in `catchResetGame()` - upstream calls it once in
// setup() and never switches away, so (matching gamePong.c's own identical
// treatment) it stays set for the whole game, title screen included.

#define CATCH_CW 8
#define CATCH_CH 3

#define CATCH_R 0.2          // real upstream rocket impulse per tick
#define CATCH_MAX_VX 2.0     // real upstream MAX_AX
#define CATCH_MAX_VY 1.5     // real upstream MAX_AY
#define CATCH_AIR_RESISTANCE 0.005

#define CATCH_MAX_SPRITES 10
#define CATCH_CATCHER 0      // sprite index of the catcher itself
#define CATCH_KIND_CATCHER 0
#define CATCH_KIND_BALL 1

#define CATCH_FLAG_COUNT 5   // see header comment point 6 - a wrap threshold, not a real frame count

#define CATCH_PAD_U 0x01
#define CATCH_PAD_D 0x02
#define CATCH_PAD_L 0x04
#define CATCH_PAD_R 0x08
#define CATCH_PAD_A 0x10
#define CATCH_PAD_B 0x20
#define CATCH_PAD_C 0x40

#define CATCH_LEVELUP_TICKS 20                  // 2000ms @ this game's real 10fps (100ms/tick)
#define CATCH_NEXT_BALL_MAX_TICKS_INITIAL 100   // upstream's own next_ball_max, 10000ms / 100ms-per-tick
#define CATCH_NEXT_BALL_MAX_TICKS_FLOOR 30      // upstream's own 3000ms floor
#define CATCH_NEXT_BALL_MAX_TICKS_STEP 5        // upstream's own 500ms decay step

typedef struct
{
    float x;
    float y;
    float vx; // upstream's own "ax" - really a velocity, see header comment
    float vy; // upstream's own "ay"
    int w;
    int h;
    int kind; // CATCH_KIND_CATCHER or CATCH_KIND_BALL - see header comment
} CatchSprite;

CatchSprite catchSprites[CATCH_MAX_SPRITES];
int catchSpriteCount = 1;

float catchGravity;
float catchWind;

int catchScore;
int catchLevel;
int catchLevelBalls;
int catchBallsMissed;
int catchMissedThisRound; // snapshot of catchBallsMissed, frozen for the whole level-up display
bool catchLevelUp;

int catchNextBallMaxTicks;
int catchSpawnTimer;
int catchLevelUpTimer;

int catchPadHit;

int catchFlagDir; // 0=NOFLIP, 1=FLIPH
int catchFlagPos;
float catchFlap;

enum CatchState
{
    CATCH_STATE_TITLE   = 0,
    CATCH_STATE_LEVELUP = 1,
    CATCH_STATE_PLAY    = 2
};

int catchState;

// -----------------------------------------------------------------------------
// Real bitmap art - see header comment for the B-literal conversions and the
// real logo[] short-array bug found and fixed (height corrected 48 -> 40).
// -----------------------------------------------------------------------------

int catchCatcherBitmap[5]=
{
    8, 3,
    0xC3, 0xC3, 0x7E
};

int catchBallBitmap[5]=
{
    8, 3,
    0x18, 0x66, 0x18
};

int catchFlag0Bitmap[10]=
{
    8, 8,
    0x80, 0xC0, 0xA0, 0x90, 0xD0, 0xB0, 0x80, 0x80
};

int catchFlag1Bitmap[10]=
{
    8, 8,
    0xE1, 0x9F, 0x81, 0xE1, 0x9E, 0x80, 0x80, 0x80
};

int catchFlag2Bitmap[10]=
{
    8, 8,
    0xB0, 0xCF, 0x81, 0xB1, 0xCF, 0x80, 0x80, 0x80
};

int catchFlag3Bitmap[10]=
{
    8, 8,
    0x98, 0xE7, 0x81, 0x99, 0xE7, 0x80, 0x80, 0x80
};

int catchFlag4Bitmap[10]=
{
    8, 8,
    0x8C, 0xF3, 0x81, 0x8D, 0xF3, 0x80, 0x80, 0x80
};

int catchFlag5Bitmap[10]=
{
    8, 8,
    0x86, 0xF9, 0x81, 0x87, 0xF9, 0x80, 0x80, 0x80
};

int catchFlag6Bitmap[10]=
{
    8, 8,
    0xC3, 0xBD, 0x81, 0xC3, 0xBC, 0x80, 0x80, 0x80
};

// Real upstream header said {84, 48} - corrected to {84, 40} below (see
// header comment: the real data is genuinely only 40 rows / 440 bytes, not
// the 528 bytes a real height of 48 would need).
int catchLogoBitmap[442]=
{
84, 40,
0x73,0x11,0x1C,0x19,0xC4,0x68,0x0,0x0,0x0,0x0,0x0,0x94,0xBB,0xA4,0x22,0x4E,0x88,0x0,0x0,0x0,0x0,0x0,0x94,0x91,0x24,0x22,0x44,0x8E,0x7,0x80,0x0,0x0,0x0,0x73,0x11,0x1E,0x19,0xE4,0x6A,0xF,0xC0,0x0,0x0,0x0,0x10,0x0,0x0,0x0,0x0,0x0,0x78,0x78,0x0,0x0,0x0,0x72,0x60,0x0,0x0,0x0,0x0,0x78,0x78,0x0,0x0,0x0,0x2,0xAF,0x7,0x24,0x80,0x0,0xF,0xC0,0x0,0x0,0x0,0x0,0xCA,0x89,0x24,0x80,0x0,0x7,0x80,0x0,0x0,0x0,0x0,0x8A,0x89,0x24,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x60,0x7,0xB6,0x80,0x1E,0x0,0x78,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1E,0x0,0x78,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1E,0x0,0x78,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1E,0x0,0x78,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xF,0xFF,0xF0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x7,0xFF,0xE0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x55,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x13,0x0,0x0,0x2B,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1C,0xE0,0x0,0x1A,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x10,0x20,0x0,0x14,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x13,0x20,0x0,0x8,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1C,0xE0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x10,0x0,0x0,0x8,0x0,0x0,0x0,0x0,0x7F,0x0,0x0,0x10,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xFF,0x0,0x0,0x10,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xFE,0x7,0xC7,0xF8,0xFC,0xC6,0x7E,0x7E,0x0,0x0,0x0,0xE0,0xF,0xE3,0xF1,0xF8,0xC6,0x7C,0x7F,0x0,0x0,0x0,0xC0,0xC,0x60,0xC1,0xC0,0xC6,0x60,0x63,0x0,0x0,0x0,0xC0,0xC,0x60,0xC1,0x80,0xFE,0x78,0x63,0x0,0x0,0x0,0xC0,0xF,0xE0,0xC1,0x80,0xEE,0x78,0x7E,0x0,0x0,0x0,0xC0,0xE,0xE0,0xC1,0x80,0xC6,0x60,0x7E,0x0,0x0,0x0,0xC0,0xC,0x60,0xC1,0xC0,0xC6,0x60,0x67,0x0,0x0,0x0,0xE0,0xC,0x60,0xC1,0xF8,0xC6,0x7C,0x63,0x0,0x0,0x0,0xFE,0x8,0x20,0xC0,0xFC,0x82,0x7E,0x61,0x0,0x0,0x0,0xFF,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
};

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

float catchFabs( float v )
{
    if( v < 0 ) return -v;
    return v;
}

void catchDrawFlag( int idx, int x, int y, int flip )
{
    if( idx == 0 ) gbDrawBitmapRotated( x, y, catchFlag1Bitmap, 0, flip );
    else if( idx == 1 ) gbDrawBitmapRotated( x, y, catchFlag2Bitmap, 0, flip );
    else if( idx == 2 ) gbDrawBitmapRotated( x, y, catchFlag3Bitmap, 0, flip );
    else if( idx == 3 ) gbDrawBitmapRotated( x, y, catchFlag4Bitmap, 0, flip );
    else if( idx == 4 ) gbDrawBitmapRotated( x, y, catchFlag5Bitmap, 0, flip );
    else gbDrawBitmapRotated( x, y, catchFlag6Bitmap, 0, flip );
}

// Direct port of upstream's own draw_catcher() - the catcher's own thruster-
// flame pixels, drawn only while exactly one direction is held (see header
// comment point 1).
void catchDrawThrust()
{
    int cx = (int)catchSprites[ CATCH_CATCHER ].x;
    int cy = (int)catchSprites[ CATCH_CATCHER ].y;
    int i = gbFrameCount & 1;

    if( catchPadHit == CATCH_PAD_D )
    {
        if( i == 0 )
        {
            gbDrawPixel( cx + 2, cy + 3 );
            gbDrawPixel( cx + 5, cy + 3 );
            gbDrawPixel( cx + 3, cy + 4 );
            gbDrawPixel( cx + 4, cy + 3 );
            gbDrawPixel( cx + 3, cy + 5 );
            gbDrawPixel( cx + 4, cy + 4 );
        }
        else
        {
            gbDrawPixel( cx + 2, cy + 4 );
            gbDrawPixel( cx + 5, cy + 4 );
            gbDrawPixel( cx + 3, cy + 3 );
            gbDrawPixel( cx + 4, cy + 4 );
            gbDrawPixel( cx + 3, cy + 4 );
            gbDrawPixel( cx + 4, cy + 5 );
        }
    }
    else if( catchPadHit == CATCH_PAD_L )
    {
        gbDrawPixel( cx - 1 - i, cy + 0 );
        gbDrawPixel( cx - 2 - i, cy + 1 );
        gbDrawPixel( cx - 1 - i, cy + 2 );
    }
    else if( catchPadHit == CATCH_PAD_R )
    {
        gbDrawPixel( cx + 8 + i, cy + 0 );
        gbDrawPixel( cx + 9 + i, cy + 1 );
        gbDrawPixel( cx + 8 + i, cy + 2 );
    }
}

// -----------------------------------------------------------------------------
// Sprite management - direct ports of reset_ball()/add_ball()/kill_ball()/reset_catcher()
// -----------------------------------------------------------------------------

void catchResetBall( int i )
{
    catchSprites[ i ].x = arand( LCDWIDTH - CATCH_CW - 1 );
    catchSprites[ i ].y = 0;
    catchSprites[ i ].vx = 0;
    catchSprites[ i ].vy = 0;
    catchSprites[ i ].w = CATCH_CW;
    catchSprites[ i ].h = CATCH_CH;
    catchSprites[ i ].kind = CATCH_KIND_BALL;
}

// Defensively clamped against overflowing CATCH_MAX_SPRITES - upstream has
// no equivalent guard (a real latent OOB write on real hardware too at a
// high enough level), but this project avoids a genuine OOB write in this
// shared, linker-less single binary even where upstream's own version was
// comparatively harmless - matching gameSnakeAbc.c's own identical
// precedent (see its own header comment).
void catchAddBall()
{
    if( catchSpriteCount >= CATCH_MAX_SPRITES ) return;
    catchResetBall( catchSpriteCount );
    catchSpriteCount = catchSpriteCount + 1;
}

void catchKillBall( int b )
{
    int i;
    catchSpriteCount = catchSpriteCount - 1;
    for( i = b; i < catchSpriteCount; i = i + 1 )
    {
        catchSprites[ i ].x = catchSprites[ i + 1 ].x;
        catchSprites[ i ].y = catchSprites[ i + 1 ].y;
        catchSprites[ i ].vx = catchSprites[ i + 1 ].vx;
        catchSprites[ i ].vy = catchSprites[ i + 1 ].vy;
        catchSprites[ i ].w = catchSprites[ i + 1 ].w;
        catchSprites[ i ].h = catchSprites[ i + 1 ].h;
        catchSprites[ i ].kind = catchSprites[ i + 1 ].kind;
    }
    if( catchLevelBalls == 0 && catchSpriteCount == 1 )
      catchLevelUp = true;
}

void catchResetCatcher()
{
    catchSprites[ CATCH_CATCHER ].x = LCDWIDTH / 2;
    catchSprites[ CATCH_CATCHER ].y = LCDHEIGHT / 2;
    catchSprites[ CATCH_CATCHER ].vx = 0;
    catchSprites[ CATCH_CATCHER ].vy = 0;
}

// -----------------------------------------------------------------------------
// Physics - direct port of upstream's own move(). See header comment point 2
// for the real, deliberately preserved "stale post-kill index" quirk below.
// -----------------------------------------------------------------------------

void catchMove()
{
    int i;
    for( i = 0; i < catchSpriteCount; i = i + 1 )
    {
        catchSprites[ i ].vy = catchSprites[ i ].vy + catchGravity;

        if( catchWind > 0 )
        {
            if( ( catchWind - catchSprites[ i ].vx ) > 0 )
              catchSprites[ i ].vx = catchSprites[ i ].vx + catchWind / 50;
        }
        else if( catchWind < 0 )
        {
            if( ( catchWind - catchSprites[ i ].vx ) < 0 )
              catchSprites[ i ].vx = catchSprites[ i ].vx + catchWind / 50;
        }
        if( catchSprites[ i ].vx > 0 ) catchSprites[ i ].vx = catchSprites[ i ].vx - CATCH_AIR_RESISTANCE;
        if( catchSprites[ i ].vx < 0 ) catchSprites[ i ].vx = catchSprites[ i ].vx + CATCH_AIR_RESISTANCE;

        catchSprites[ i ].x = catchSprites[ i ].x + catchSprites[ i ].vx;
        catchSprites[ i ].y = catchSprites[ i ].y + catchSprites[ i ].vy;

        int w = catchSprites[ i ].w;
        int h = catchSprites[ i ].h;
        float x = catchSprites[ i ].x;
        float y = catchSprites[ i ].y;

        if( x < 0 ) catchSprites[ i ].x = ( LCDWIDTH - w ) - 0.1;
        if( x >= LCDWIDTH - w ) catchSprites[ i ].x = 0;
        if( y < 0 ) catchSprites[ i ].y = ( LCDHEIGHT - h ) - 0.1;
        if( y >= LCDHEIGHT - h )
        {
            catchSprites[ i ].vy = catchSprites[ i ].vy / 2.5;
            catchSprites[ i ].vy = catchSprites[ i ].vy * -1;
            catchSprites[ i ].y = catchSprites[ i ].y + catchSprites[ i ].vy;
            if( y >= LCDHEIGHT - h ) catchSprites[ i ].y = LCDHEIGHT - h - 1;
        }

        if( i > 0 )
        {
            float x0 = catchSprites[ CATCH_CATCHER ].x;
            float y0 = catchSprites[ CATCH_CATCHER ].y;

            if( x >= x0 - 3 && x <= x0 + 3 && y >= y0 - 1 && y < y0 + 3 )
            {
                catchScore = catchScore + 1;
                gbPlayOK();
                catchKillBall( i );
            }
            if( ( x > x0 - 5 && x < x0 - 3 && y >= y0 - 2 && y < y0 + 1 ) ||
                ( x > x0 + 3 && x < x0 + 5 && y >= y0 - 2 && y < y0 + 1 ) )
            {
                float by = catchSprites[ i ].vy;
                float cy = catchSprites[ CATCH_CATCHER ].vy;
                catchSprites[ i ].vy = catchSprites[ i ].vy + ( cy - by );
                catchSprites[ CATCH_CATCHER ].vy = catchSprites[ CATCH_CATCHER ].vy + ( by - cy );
                catchSprites[ i ].y = catchSprites[ i ].y + catchSprites[ i ].vy;
                catchSprites[ CATCH_CATCHER ].y = catchSprites[ CATCH_CATCHER ].y + catchSprites[ CATCH_CATCHER ].vy;

                float bx = catchSprites[ i ].vx;
                float cx = catchSprites[ CATCH_CATCHER ].vx;
                catchSprites[ i ].vx = catchSprites[ i ].vx + ( cx - bx );
                catchSprites[ CATCH_CATCHER ].vx = catchSprites[ CATCH_CATCHER ].vx + ( bx - cx );
                catchSprites[ i ].x = catchSprites[ i ].x + catchSprites[ i ].vx;
                catchSprites[ CATCH_CATCHER ].x = catchSprites[ CATCH_CATCHER ].x + catchSprites[ CATCH_CATCHER ].vx;
            }
            if( y >= LCDHEIGHT - CATCH_CH - 2 && catchFabs( catchSprites[ i ].vy ) < 0.1 )
            {
                catchKillBall( i );
                catchBallsMissed = catchBallsMissed + 1;
                catchScore = catchScore - 1;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Input - direct port of upstream's own pad_check(). See header comment
// point 5 for why this is only ever called from catchUpdatePlay()'s own
// tail, exactly matching upstream's real call-site structure.
// -----------------------------------------------------------------------------

bool catchPadCheck()
{
    bool retVal = false;

    if( gbPressed( BTN_DOWN ) )  { catchPadHit = catchPadHit | CATCH_PAD_D; retVal = true; }
    if( gbReleased( BTN_DOWN ) ) catchPadHit = catchPadHit & ~CATCH_PAD_D;
    if( gbPressed( BTN_UP ) )    { catchPadHit = catchPadHit | CATCH_PAD_U; retVal = true; }
    if( gbReleased( BTN_UP ) )   catchPadHit = catchPadHit & ~CATCH_PAD_U;
    if( gbPressed( BTN_LEFT ) )  { catchPadHit = catchPadHit | CATCH_PAD_L; retVal = true; }
    if( gbReleased( BTN_LEFT ) ) catchPadHit = catchPadHit & ~CATCH_PAD_L;
    if( gbPressed( BTN_RIGHT ) ) { catchPadHit = catchPadHit | CATCH_PAD_R; retVal = true; }
    if( gbReleased( BTN_RIGHT ) )catchPadHit = catchPadHit & ~CATCH_PAD_R;
    if( gbPressed( BTN_A ) )     { catchPadHit = catchPadHit | CATCH_PAD_A; retVal = true; }
    if( gbReleased( BTN_A ) )    catchPadHit = catchPadHit & ~CATCH_PAD_A;
    if( gbPressed( BTN_B ) )     { catchPadHit = catchPadHit | CATCH_PAD_B; retVal = true; }
    if( gbReleased( BTN_B ) )    catchPadHit = catchPadHit & ~CATCH_PAD_B;
    if( gbPressed( BTN_C ) )     { catchPadHit = catchPadHit | CATCH_PAD_C; retVal = true; }
    if( gbReleased( BTN_C ) )    catchPadHit = catchPadHit & ~CATCH_PAD_C;

    return retVal;
}

// -----------------------------------------------------------------------------
// Lifecycle - catchResetGame() is defined here (ahead of catchUpdatePlay(),
// which calls it on a Button-C restart - see header comment point 5) so
// every call site is textually after its definition, rather than betting on
// forward-reference support this dialect's own reference doc doesn't
// explicitly confirm.
// -----------------------------------------------------------------------------

// Direct port of upstream's own setup() - also the target of upstream's own
// mid-game Button-C "restart" (which upstream implements by just calling
// setup() again outright - see header comment point 5). Fixed here, not
// preserved: `catchPadHit` is now explicitly cleared, unlike real
// upstream's own `setup()` (which never resets `pad_hit` either) - this
// is the defensive reset point 5 flags as missing, added on direct
// request. Without it, a Button-C release that lands while the re-shown
// title/level-up screen is up (the ordinary way to use this feature - tap
// C, let go, then press A) is never read while it's true, leaving
// `catchPadHit` stuck holding exactly CATCH_PAD_C into the first real
// gameplay tick - which is also the exact tick that re-triggers
// `catchResetGame()`, risking a repeating auto-restart loop the first
// time a player uses the mid-game restart.
void catchResetGame()
{
    gbBegin();
    gbSetFont( gbFont5x7 ); // real upstream's own setup()-time call - stays set for the whole game, title screen included
    gbSetFrameRate( 10 );

    catchPadHit = 0;

    catchScore = 0;
    catchSpriteCount = 1;
    catchResetCatcher();
    catchSprites[ CATCH_CATCHER ].w = CATCH_CW;
    catchSprites[ CATCH_CATCHER ].h = CATCH_CH;
    catchSprites[ CATCH_CATCHER ].kind = CATCH_KIND_CATCHER;

    catchAddBall(); // see header comment point 3 - a real round-1-only asymmetry, preserved

    catchGravity = 0.005;
    catchWind = 0;
    catchLevel = 0;
    catchBallsMissed = 0;
    catchMissedThisRound = 0;
    catchLevelUp = true;
    catchLevelBalls = 0;
    catchNextBallMaxTicks = CATCH_NEXT_BALL_MAX_TICKS_INITIAL;
    catchSpawnTimer = 0;
    catchLevelUpTimer = 0;
    catchFlap = 0;

    catchState = CATCH_STATE_TITLE;
}

// -----------------------------------------------------------------------------
// Round transitions - direct port of upstream's own `if (level_up) {...}`
// block, split into a one-time setup half and a per-tick redraw half (see
// header comment's STATE MACHINE section).
// -----------------------------------------------------------------------------

void catchBeginLevelUp()
{
    catchResetCatcher();
    if( catchBallsMissed == 0 )
      catchLevel = catchLevel + 1;
    catchLevelUp = false;
    catchMissedThisRound = catchBallsMissed;
    catchBallsMissed = 0;
    catchLevelBalls = catchLevel;

    if( catchNextBallMaxTicks > CATCH_NEXT_BALL_MAX_TICKS_FLOOR )
      catchNextBallMaxTicks = catchNextBallMaxTicks - CATCH_NEXT_BALL_MAX_TICKS_STEP;
    catchSpawnTimer = arand( catchNextBallMaxTicks );
    catchGravity = catchGravity + 0.001;

    if( catchLevel > 1 )
    {
        catchWind = (float)catchLevel * 0.05 + (float)arand( 10 ) / 20.0;
        if( arand( 2 ) )
        {
            catchWind = catchWind * -1;
            catchFlagDir = 1; // FLIPH
            catchFlagPos = 5;
        }
        else
        {
            catchFlagDir = 0; // NOFLIP
            catchFlagPos = 13;
        }
    }
    else
      catchWind = 0;

    catchLevelUpTimer = CATCH_LEVELUP_TICKS;
    catchState = CATCH_STATE_LEVELUP;
}

void catchUpdateLevelUp()
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "-- Catcher! --" );

    if( catchMissedThisRound > 0 )
    {
        gbCursorX = 20;
        gbCursorY = 10;
        gbPrintString( "Missed " );
        gbPrintNumber( catchMissedThisRound );
    }

    gbCursorX = 20;
    gbCursorY = 20;
    gbPrintString( "Level " );
    gbPrintNumber( catchLevel );

    catchLevelUpTimer = catchLevelUpTimer - 1;
    if( catchLevelUpTimer <= 0 )
      catchState = CATCH_STATE_PLAY;
}

// -----------------------------------------------------------------------------
// Title screen
// -----------------------------------------------------------------------------

void catchUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( 0, 0, catchLogoBitmap );

    gbCursorX = 21;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      catchState = CATCH_STATE_PLAY;
}

// -----------------------------------------------------------------------------
// Main gameplay tick - direct port of upstream's own loop() body (minus the
// title/level-up branches, handled above).
// -----------------------------------------------------------------------------

void catchUpdatePlay()
{
    // Freshly entered this round (or the very first round after the title
    // screen) - compute it and show the level-up screen immediately, same
    // as upstream's own top-of-loop() check (see header comment).
    if( catchLevelUp )
    {
        catchBeginLevelUp();
        catchUpdateLevelUp();
        return;
    }

    if( gbRepeat( BTN_RIGHT, 1 ) || gbRepeat( BTN_LEFT, 1 ) || gbRepeat( BTN_DOWN, 1 ) )
      gbPlayTick();

    if( catchPadHit == CATCH_PAD_L )
    {
        if( catchSprites[ CATCH_CATCHER ].vx < CATCH_MAX_VX )
          catchSprites[ CATCH_CATCHER ].vx = catchSprites[ CATCH_CATCHER ].vx + CATCH_R;
    }
    else if( catchPadHit == CATCH_PAD_R )
    {
        if( catchSprites[ CATCH_CATCHER ].vx > -CATCH_MAX_VX )
          catchSprites[ CATCH_CATCHER ].vx = catchSprites[ CATCH_CATCHER ].vx - CATCH_R;
    }
    else if( catchPadHit == CATCH_PAD_U )
    {
        if( catchSprites[ CATCH_CATCHER ].vy < CATCH_MAX_VY )
          catchSprites[ CATCH_CATCHER ].vy = catchSprites[ CATCH_CATCHER ].vy + CATCH_R;
    }
    else if( catchPadHit == CATCH_PAD_D )
    {
        if( catchSprites[ CATCH_CATCHER ].vy > -CATCH_MAX_VY )
          catchSprites[ CATCH_CATCHER ].vy = catchSprites[ CATCH_CATCHER ].vy - CATCH_R;
    }

    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintNumber( catchScore );

    int i;
    for( i = 0; i < catchSpriteCount; i = i + 1 )
    {
        if( catchSprites[ i ].kind == CATCH_KIND_CATCHER )
          gbDrawBitmap( (int)catchSprites[ i ].x, (int)catchSprites[ i ].y, catchCatcherBitmap );
        else
          gbDrawBitmap( (int)catchSprites[ i ].x, (int)catchSprites[ i ].y, catchBallBitmap );

        if( i == CATCH_CATCHER )
          catchDrawThrust();
    }

    if( catchWind != 0 )
    {
        catchDrawFlag( (int)catchFlap, catchFlagPos, LCDHEIGHT - 9, catchFlagDir );
        if( gbFrameCount & 1 )
          catchFlap = catchFlap + 1;
    }
    else
      gbDrawBitmap( 13, LCDHEIGHT - 9, catchFlag0Bitmap );

    if( catchFlap > CATCH_FLAG_COUNT )
      catchFlap = 0;

    gbDrawFastHLine( 0, LCDHEIGHT - 1, LCDWIDTH );

    catchMove();

    if( catchSpawnTimer > 0 )
      catchSpawnTimer = catchSpawnTimer - 1;
    if( catchSpawnTimer <= 0 && catchLevelBalls > 0 )
    {
        catchLevelBalls = catchLevelBalls - 1;
        catchSpawnTimer = arand( catchNextBallMaxTicks );
        catchAddBall();
    }

    catchPadCheck();
    if( catchPadHit == CATCH_PAD_C )
      catchResetGame();
}

// -----------------------------------------------------------------------------
// Engine entry points
// -----------------------------------------------------------------------------

void gameCatcher_init()
{
    catchResetGame();
}

void gameCatcher_update()
{
    if( !gbUpdate() ) return;


    if( catchState == CATCH_STATE_TITLE ) catchUpdateTitle();
    else if( catchState == CATCH_STATE_LEVELUP ) catchUpdateLevelUp();
    else catchUpdatePlay();

    gbRenderFrame();
}
