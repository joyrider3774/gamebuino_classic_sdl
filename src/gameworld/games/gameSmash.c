#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Smash-and-Crash (Skyrunner65, license: none specified -
// github.com/Skyrunner65/Smash-and-Crash). An Arkanoid-named but actually
// endless-survival platformer: a small robot stands on a fixed arrangement
// of platforms (one of 4 selectable maps) while one of four randomly
// rotating "disasters" (falling meteor / homing-ish arrow / bouncing ball /
// a stationary black hole) threatens it every ~200 frames; move left/right,
// jump, hold B while moving to run faster, survive as long as possible -
// touching a hazard, falling off the bottom, or landing wrong all end the
// round, with the survived frame count as the score.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every upstream global/function got
// a `smash`-prefixed name (checked against every other prefix already in
// use in this cartridge before picking it - not taken). `boolean` became
// `bool`. `random(N)`/`random(min,max)` became `arand(N)`/`min + arand(max
// - min)` (this project's own established RNG helper).
//
// TWO REAL BLOCKING-WIDGET CALLS, NEITHER WITH A SHIM EQUIVALENT, BOTH
// CONVERTED TO EXPLICIT STATES (matching gamePong.c's own "blocking call ->
// resumable state" treatment, and gameCrazyCar.c's own precedent for
// exactly this situation):
//   1. `gb.titleScreen(F(" "), Title)` -> SMASH_STATE_TITLE. Real
//      `Gamebuino::titleScreen()` prints the passed name at cursor (0,12)
//      then draws the passed logo at `(0, 12+logoOffset)`, where
//      `logoOffset` is the active font's own real height if the name
//      string is non-empty (confirmed directly against the real
//      Gamebuino.cpp source - see gameInvaders.c's own header comment for
//      the fullest writeup of this formula already done in this project).
//      Upstream's own name string is `" "` (one space) - non-empty by
//      length, so the offset applies: with font5x7 active (set in setup()
//      just before this call, height 7+1=8), the logo lands at exactly
//      `(0, 20)` - and the logo is 64x28, so 20+28=48 lands flush with the
//      real screen's own bottom edge, a good sanity check that the formula
//      was applied correctly. The space character itself is invisible, so
//      it isn't drawn; a "PRESS A" hint (this project's own established
//      stand-in for the real blinking-A-icon prompt real titleScreen()
//      draws, see gameSimonbuino.c/gameFlappyBirdo.c) fills the resulting
//      gap above the logo instead.
//   2. `gb.menu(menu, 4)` (Survival/Status/Controls/Change Game) -> a
//      custom SMASH_STATE_MENU list, modeled directly on gameCrazyCar.c's
//      own difficulty-menu precedent for this exact situation (up/down to
//      move a ">" cursor, A to confirm, B to cancel back to the title -
//      matching upstream's own `case -1: gb.titleScreen(...)`).
//      "Change Game" (upstream: `gb.changeGame()`, a real-hardware
//      "switch to a different game on the SD card" OS feature) has no
//      equivalent in this single-cartridge menu model - this cartridge's
//      actual equivalent gesture is the real Start button, handled
//      globally by portVircon32.c outside any individual game's own code
//      - so selecting it here just returns to the title screen, the
//      closest available outcome rather than a dead, unresponsive button.
//
// `maps()` (the map-picker: LEFT/RIGHT with `gb.buttons.repeat(..,15)` to
// cycle Lonely/Ridge/Tower/Hill, A to confirm) is a real, already-
// non-blocking-shaped upstream loop (its own `while(gb.update())`) and
// ported nearly verbatim as SMASH_STATE_MAPSELECT. `play()` (the main
// blocking gameplay loop, gated by its own `alive`/`pause` flags) became
// SMASH_STATE_PLAY, with those two flags preserved as-is rather than split
// into further states, since upstream itself already treats them as
// sub-modes of one screen, not separate blocking calls.
//
// `gb.buttons.repeat(BTN_x, N)` calls port directly as `gbRepeat(BTN_x, N)`
// - this shim's `gbRepeat()` now matches real `Buttons::repeat()` exactly
// (period<=1 fires every held frame, period>1 fires once then every period
// frames), so every one of this game's own repeat() calls (movement at
// period 2, jump/pause at period 20, run-modifier at period 1, map-cycling
// at period 15) was carried over unchanged.
//
// `gb.collideBitmapBitmap()` (real pixel-perfect bitmap collision) ported
// as this shim's own real `gbCollideBitmapBitmap()` throughout - every
// real upstream collision check in this game (player/meteor/arrow vs.
// platform/platform2, and the 4 hazards vs. the player) uses it, matching
// upstream's own exact call shape one-for-one.
//
// A REAL BUG, FOUND AND FIXED: an earlier pass through this file used
// `gbCollideRectRect()` instead (this shim had no `gbCollideBitmapBitmap()`
// primitive yet at the time this game was first ported - it was promoted
// to the shared shim only once `gameDescent.c`'s own port needed it later
// in the same batch, and this file was never revisited afterward). That
// substitution was reasoned to be exact for the platform/platform2 sprites
// specifically (both are genuinely solid-filled bitmaps, every real byte
// 0xFF, so a bounding-box test against either one alone really is
// pixel-exact) - but that reasoning never checked the PLAYER's own
// bitmap (`smashPlayerBitmap`), which has real transparent pixels of its
// own, including a fully empty bottom row (every column's real byte has
// its bit 7 clear). A rect-rect test still treats the player as a solid
// 8x8 block regardless, so the instant a jump moved the player up by its
// own real 1px impulse, the coarser rect test still reported "still
// touching the platform" (the two bounding boxes still technically
// overlapped by a few pixels) even though the real, pixel-perfect test
// upstream actually uses would not have - the player's own real empty
// bottom row means no actual "on" pixel from the player ever touches the
// platform's own real top row at that offset. The immediate re-collision
// snapped the player straight back to standing (`smashPlayerY` reset,
// `smashPlayerGrav = 0`) within the very same frame, before the jump
// could ever become visible - a complete, permanent inability to jump at
// all, confirmed directly via Puppeteer (held Button A continuously for
// 80+ frames, well past the real `gbRepeat(BTN_A,20)` threshold multiple
// times over, with the player never leaving the ground). Fixed by
// restoring the real `gbCollideBitmapBitmap()` calls project-wide in this
// file, matching upstream exactly - re-verified via Puppeteer afterward
// that a single A-press now visibly lifts the player off the platform.
//
// SEVERAL REAL, VERIFIED UPSTREAM BUGS - all preserved deliberately,
// per this project's own "preserve real upstream behavior by default"
// norm, since none of them are catastrophic and every one was traced
// through by hand rather than guessed at:
//
//   1. **Ball/arrow horizontal speed is not what the source says it is.**
//      `arrowx`/`ballx`/`bally`/`ballyv` are all declared plain `int` (not
//      `float`) upstream, yet stepped by float literals (`arrowx = arrowx
//      + 1.5;`, `ballx = ballx - 1.5;`) - every such assignment truncates
//      toward zero on write-back to the `int`. Traced by hand: adding 1.5
//      to an integer and truncating always yields `+1` (e.g. 3+1.5=4.5 ->
//      4), but *subtracting* 1.5 and truncating always yields `-2` (e.g.
//      3-1.5=1.5 -> 1, a drop of 2) - a real, asymmetric consequence of
//      `int` truncation rounding toward zero either way. So `arrowx`
//      (always `+1.5`) actually advances at a real 1px/frame (not the
//      intended 1.5), while `ballx` moving left (`way==0`, `-1.5`) actually
//      advances at a real 2px/frame - twice its own rightward counterpart.
//      Reproduced here with explicit `(int)(...)` casts on every such
//      line, both to guarantee this dialect truncates the same way
//      standard C does and to make the real, intentional-looking-but-buggy
//      truncation visible rather than accidental-looking.
//   2. **The ball's vertical speed is similarly not 1.5.** `int ballyv =
//      -1.5;` truncates to `-1` at first assignment, and every later
//      `ballyv = 1.5;`/`ballyv = -1.5;` (on hitting the top/bottom of its
//      own bounce range) truncates to plain `+1`/`-1` too - so `ballyv` is
//      really always exactly Β±1, never Β±1.5, and (unlike point 1 above)
//      this one isn't even asymmetric, since `bally = bally + ballyv` is a
//      plain int+int add with no further truncation surprise.
//   3. **The ball never actually moves right, ever, for the game's entire
//      real life.** `way = random(0,1);` - and real Arduino `random(min,
//      max)` has an EXCLUSIVE upper bound, so `random(0,1)` can only ever
//      return `0`, never `1` - a well-known real Arduino gotcha, not a
//      typo. Ported faithfully as `arand(1)` (this shim's own established
//      `min + arand(max-min)` mapping, `arand(1)` always returning 0 for
//      the exact same reason). The practical effect: every time the ball
//      wraps offscreen, `way` is deterministically reset to 0, so `ballx`
//      is always reset to 82 (moving left) - the entire `way==1`
//      (`ballx = 1`, moving right) branch is real, permanently dead code,
//      on real hardware as much as here.
//   4. **The Black Hole disaster is real but under-weighted.** `disaster =
//      random(1,12);` - again an exclusive upper bound, so `disaster`'s
//      real range is `[1,11]`, not `[1,12]`. Meteor/Arrow/Ball each own a
//      3-value window (1-3/4-6/7-9), but Black Hole's own checked window
//      (`>=10 && <=12`) can only ever actually be hit by 10 or 11 - a real
//      2-in-11 chance versus everyone else's 3-in-11. Ported as `1 +
//      arand(11)`, which reproduces this exact imbalance automatically
//      (no extra code needed - `arand()`'s own exclusive upper bound
//      matches Arduino's `random()` for free here).
//   5. **The Ridge map's "Arrow Collision" block tests the METEOR's
//      position against 2 of its 4 platforms, not the arrow's.** Read
//      directly off upstream: `(collideBitmapBitmap(arrowx,arrowy,arrow,
//      0,28,platform2)) || (...,60,28,platform2) || (collideBitmapBitmap(
//      meteorx,meteory,meteor,18,44,platform2)) || (...meteorx,meteory,
//      meteor,42,44,platform2))` - the last two conditions plainly use
//      `meteorx`/`meteory`/`meteor`, not the arrow's own variables/sprite,
//      inside a block whose own comment and reset action
//      (`arrowx=0;arrowy=random(...)`) are both about the arrow. A real
//      copy-paste artifact: on the Ridge map, the arrow only genuinely
//      resets on touching the two TOP platforms, but *also* resets
//      whenever the METEOR (regardless of the arrow's own real position)
//      happens to touch either of the two BOTTOM platforms. Copied
//      verbatim, variable mix-up and all.
//   6. **The Tower map's player-landing check has a fifth, bogus branch**:
//      `else if(collideBitmapBitmap(playerx,playery,player,0,0,platform)
//      == true){ playergrav = 1; }` - no platform is ever actually drawn
//      at `(0,0)` on this map (the real drawn set is `(0,44,platform)`,
//      `(30,28,platform2)`, `(30,16,platform2)`, `(18,0,platform2)`,
//      `(42,0,platform2)`). Since `platform` is 88px wide (the *whole*
//      screen width) and 4px tall, this phantom check's own real bounding
//      box spans the full width of row y=0-3 - so whenever the player's
//      own box reaches that height (jumping near the very top of the
//      screen), this branch fires anywhere across the full width, setting
//      `playergrav = 1` (a small downward nudge) instead of falling into
//      the normal `else` branch's `playergrav = playergrav + 1` gravity
//      accumulation. The real, if surely unintended, effect: an invisible
//      full-width "soft ceiling" gently nudges the player back down near
//      the top of this one map, rather than a real climbable platform
//      being there. Copied verbatim (same coordinates, same bitmap).
//   7. **The Tower map's meteor-vs-platform collision only checks 3 of its
//      5 drawn platforms** (`(18,0,platform2)`, `(42,0,platform2)`,
//      `(0,44,platform)` - both `(30,28,platform2)` and `(30,16,
//      platform2)` are never tested), so a falling meteor on this map can
//      visibly clip straight through either of those two middle platforms
//      instead of exploding on contact. Copied verbatim (same 3 checks,
//      no 4th/5th added).
//   8. **`arrowy`'s own reset range is inconsistent between call sites**:
//      most resets use `random(0,34)`, but the in-play wraparound reset
//      (`if(arrowx >= 80){ ...; arrowy = random(6,34); }`) and the Tower
//      map's own Arrow Collision reset both use `random(6,34)` instead -
//      copied verbatim at each call site (`arand(34)` vs `6 + arand(28)`)
//      rather than normalized to one range.
//
// Sound: `gb.sound.playNote(pitch,dur,channel)` ported as `gbPlayNote(
// pitch,dur)` (channel dropped, matching this shim's single-channel
// design). Every `gb.sound.command(...)` call (a real, raw low-level
// Sound::command() tracker effect - e.g. a pitch slide/vibrato layered on
// top of the note that was just played) was dropped outright: this is the
// same already-documented, deliberate scope limit as the rest of the real
// pattern/track player (`gamebuinoShim.h`'s own header comment - only
// one-shot `playNote`/`playTick`/`playOK`/`playCancel` exist), not a new
// gap. The 5 real upstream call sites that play an identical "player died"
// note+command sequence (falling off the bottom, and each of the 4 hazard
// types) were consolidated into one local `smashKillPlayer()` helper - a
// plain de-duplication of literally identical code, not a behavior change.
//
// `gb.getCpuLoad()`/`gb.getFreeRam()` (the Status menu screen) and
// `gb.battery.show` are real hardware-introspection features with no
// meaning on a virtual console - matching this project's own established
// "drop `battery.show`, it's cosmetic" precedent, the Status screen itself
// is kept (so the real menu still has 4 working entries) but shows literal
// "N/A" placeholders instead of inventing fake numbers. This is a
// deliberate simplification of a real-hardware-only concept, not a shim
// gap that needs fixing.
//
// The real upstream `"    Loading...."` message (printed, then
// immediately overwritten the same tick by `maps()`'s own first
// `gb.update()` cycle, with no delay in between) was dropped outright - on
// real hardware whether this is ever actually visible for even one frame
// depends on exact SPI/display-flip timing this project has no way to
// reproduce faithfully, and either way it's purely cosmetic with zero
// gameplay effect.
//
// Real icon glyphs `\25`/`\26`/`\27` (octal - ASCII 21/22/23, real
// Gamebuino low-range button-icon glyphs, confirmed already covered by
// this shim's font tables) can't be embedded in a plain quoted string
// literal, so each line using one was built as an explicit `int[]` array
// instead (see gameTaquin.c's own `taqRestartText` for the established
// precedent this follows exactly).
//
// Bitmap fill/mask check (per this project's own established "look for a
// GRAY mask/fill pass drawn before the real outline bitmap" bug class -
// see gameFlappyBirdo.c's own header comment for that bug's full history):
// checked directly against both real source files (SmashandCrash.ino,
// Maps.ino) - every single `gb.display.drawBitmap(...)` call in this game
// stands alone, with no `setColor(GRAY)` or plain fill drawn underneath it
// first anywhere in the real source. That bug class does not apply here.
//
// All 9 real PROGMEM bitmaps (player, platform, platform2, meteor, arrow,
// point, Title, blow, ball, bhole) are restored verbatim below, with every
// real `B00000000`-style Arduino binary literal mechanically converted to
// `0x`-prefixed hex (byte-for-byte, computed directly from the real source
// rather than hand-transcribed, to rule out transcription error).

#define SMASH_STATE_TITLE 0
#define SMASH_STATE_MENU 1
#define SMASH_STATE_MAPSELECT 2
#define SMASH_STATE_STATUS 3
#define SMASH_STATE_CONTROLS 4
#define SMASH_STATE_PLAY 5

int smashState;
int smashMenuIndex;  // 0=Survival 1=Status 2=Controls 3=Change Game
int smashMapscroll;  // 0=Lonely 1=Ridge 2=Tower 3=Hill

// Player - real fixed 8x8 sprite
int smashPlayerX;
int smashPlayerY;
int smashPlayerFlip; // 0=NOFLIP 1=FLIPH
int smashPlayerYv;   // real upstream global, never actually read after init - kept for fidelity
int smashPlayerGrav;
bool smashPlayerJump;

int smashFrames;
bool smashPause;
bool smashAlive;

int smashMeteorX;
int smashMeteorY;

int smashChange;
int smashDisaster;

int smashArrowX;
int smashArrowY;

int smashBallX;
int smashBallY;
int smashWay;   // see header comment point 3 - always 0 in real play
int smashBallYv;

// -----------------------------------------------------------------------------
// Real bitmaps - see this file's own header comment for the B-literal ->
// hex conversion and the "no separate fill/mask layer" check.
// -----------------------------------------------------------------------------

int smashPlayerBitmap[10]= { 8, 8, 0x3C,0x6A,0x6A,0x6A,0x3C,0x24,0x24,0x00 };
int smashPlatformBitmap[46]= { 88, 4,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
int smashPlatform2Bitmap[14]= { 24, 4,
    0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF };
int smashMeteorBitmap[10]= { 8, 8, 0x18,0x3C,0x24,0x66,0x42,0xC3,0x81,0x7E };
int smashArrowBitmap[10]= { 8, 8, 0xFC,0xCE,0xC7,0xFF,0xFF,0xC7,0x86,0xFC };
int smashPointBitmap[10]= { 8, 8, 0x03,0x0F,0x3F,0xFF,0xFF,0x3F,0x0F,0x03 };
int smashBlowBitmap[6]= { 8, 4, 0xFF,0xFF,0xFF,0xFF };
int smashBallBitmap[8]= { 8, 6, 0x3C,0x7E,0x7E,0x7E,0x7E,0x3C };
int smashBholeBitmap[34]= { 8, 32,
    0x18,0x3C,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,
    0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x3C,0x18 };
int smashTitleBitmap[226]= { 64, 28,
    0x78,0x63,0x0E,0x0F,0x0C,0x60,0x00,0x00,
    0xCC,0x77,0x1B,0x19,0x8C,0x60,0x00,0x00,
    0xC0,0x7F,0x31,0x98,0x0C,0x60,0x00,0x00,
    0xF0,0x7F,0x31,0x9E,0x0C,0x60,0x00,0x00,
    0x7C,0x7F,0x31,0x8F,0x8F,0xE0,0x00,0x00,
    0x06,0x6B,0x3F,0x80,0xCC,0x60,0x00,0x00,
    0xC6,0x63,0x31,0x98,0xCC,0x60,0x00,0x00,
    0x7C,0x63,0x31,0x8F,0x8C,0x60,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x86,0x33,0xE0,0x00,0xFC,0x00,0x00,
    0x06,0xC7,0x33,0x30,0x01,0xCC,0x00,0x1E,
    0x0C,0x67,0xB3,0x18,0x03,0x8C,0x00,0x3F,
    0x0C,0x67,0xF3,0x18,0x03,0xFC,0x00,0x3F,
    0x0C,0x67,0xF3,0x18,0x03,0xFC,0x00,0x3F,
    0x0F,0xE6,0xF3,0x18,0x03,0x8C,0x00,0x3F,
    0x0C,0x66,0x73,0x30,0x01,0x84,0x00,0x1E,
    0x0C,0x66,0x33,0xE0,0x00,0xFC,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0F,0x30,0x01,0xE3,0xF0,0x70,0x78,0x63,
    0x15,0x80,0x03,0x33,0x18,0xD8,0xCC,0x63,
    0x15,0xB0,0x06,0x03,0x19,0x8C,0xC0,0x63,
    0x15,0x80,0x06,0x03,0x19,0x8C,0xF0,0x63,
    0x0F,0x00,0x06,0x03,0x39,0x8C,0x7C,0x7F,
    0x09,0x00,0x06,0x03,0xE1,0xFC,0x06,0x63,
    0xFF,0xFF,0xF3,0x33,0x71,0x8C,0xC6,0x63,
    0xFF,0xFF,0xF1,0xE3,0x39,0x8C,0x7C,0x63 };

// -----------------------------------------------------------------------------
// Text using real Gamebuino low-range button-icon glyphs (ASCII 21/22/23 -
// real octal \25/\26/\27) - built as explicit int[] arrays since these
// aren't printable in a plain quoted string literal (see gameTaquin.c's
// own taqRestartText for the established precedent).
// -----------------------------------------------------------------------------

char smashCtrlJumpText[7]= { 21, 32, 74, 117, 109, 112, 0 };            // "\x15 Jump"
char smashCtrlRunText[6]= { 22, 32, 82, 117, 110, 0 };                   // "\x16 Run"
char smashCtrlPauseText[8]= { 23, 32, 80, 97, 117, 115, 101, 0 };        // "\x17 Pause"
char smashPressBText[8]= { 80, 114, 101, 115, 115, 32, 22, 0 };          // "Press \x16"
char smashResumeText[12]= { 22, 32, 116, 111, 32, 114, 101, 115, 117, 109, 101, 0 }; // "\x16 to resume"

// -----------------------------------------------------------------------------
// Shared kills - see this file's own header comment on the 5 real,
// identical upstream "player died" sound+flag call sites.
// -----------------------------------------------------------------------------

void smashKillPlayer()
{
    gbPlayNote( 1, 28 );
    smashAlive = false;
}

// -----------------------------------------------------------------------------
// Reset - mirrors upstream's own real "Load Survival" reset block (used
// both at cold boot, harmlessly, and every time Survival is (re)selected).
// -----------------------------------------------------------------------------

void smashResetGameState()
{
    gbPickRandomSeed(); // no-op - see gamebuinoShim.h's own header comment

    smashPlayerX = 38;
    smashPlayerY = 36;
    smashPlayerFlip = 0;
    smashPlayerYv = 1;
    smashPlayerGrav = 1;
    smashPlayerJump = true;

    smashFrames = -1;

    smashMeteorY = 0;
    smashMeteorX = arand( 76 );

    smashChange = 0;
    smashDisaster = 0;

    smashArrowX = 0;
    smashArrowY = arand( 34 );

    smashBallX = 0;
    smashBallY = 6 + arand( 36 );
    smashWay = arand( 1 ); // see header comment point 3 - always 0
    smashBallYv = (int)( -1.5 ); // see header comment point 2 - truncates to -1

    smashAlive = true;
    smashPause = false;
}

// -----------------------------------------------------------------------------
// State transitions
// -----------------------------------------------------------------------------

void smashBeginTitle()
{
    smashState = SMASH_STATE_TITLE;
    gbSetFont( gbFont5x7 );
}

void smashBeginMenu()
{
    smashState = SMASH_STATE_MENU;
    gbSetFont( gbFont5x7 );
}

void smashBeginMapSelect()
{
    smashState = SMASH_STATE_MAPSELECT;
    gbSetFont( gbFont5x7 );
}

void smashBeginStatus()
{
    smashState = SMASH_STATE_STATUS;
    gbSetFont( gbFont5x7 );
}

void smashBeginControls()
{
    smashState = SMASH_STATE_CONTROLS;
    gbSetFont( gbFont5x7 );
}

void smashBeginPlay()
{
    smashResetGameState();
    smashState = SMASH_STATE_PLAY;
    gbSetFont( gbFont3x5 );
}

// -----------------------------------------------------------------------------
// Title
// -----------------------------------------------------------------------------

void smashUpdateTitle()
{
    gbSetColor( 1 );

    // real name string was a single space (invisible) - see header comment
    // for the real (0, 12+logoOffset) placement this reproduces
    gbCursorX = 21;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    gbDrawBitmap( 0, 20, smashTitleBitmap );

    if( gbPressed( BTN_A ) )
      smashBeginMenu();
}

// -----------------------------------------------------------------------------
// Menu (real gb.menu(Survival/Status/Controls/Change Game) equivalent)
// -----------------------------------------------------------------------------

void smashUpdateMenu()
{
    gbSetColor( 1 );

    gbCursorX = 8;
    gbCursorY = 2;
    if( smashMenuIndex == 0 ) gbPrintString( ">Survival" );
    else gbPrintString( " Survival" );

    gbCursorX = 8;
    gbCursorY = 14;
    if( smashMenuIndex == 1 ) gbPrintString( ">Status" );
    else gbPrintString( " Status" );

    gbCursorX = 8;
    gbCursorY = 26;
    if( smashMenuIndex == 2 ) gbPrintString( ">Controls" );
    else gbPrintString( " Controls" );

    gbCursorX = 8;
    gbCursorY = 38;
    if( smashMenuIndex == 3 ) gbPrintString( ">Change Game" );
    else gbPrintString( " Change Game" );

    if( gbPressed( BTN_DOWN ) )
    {
        smashMenuIndex = smashMenuIndex + 1;
        if( smashMenuIndex > 3 ) smashMenuIndex = 0;
    }
    if( gbPressed( BTN_UP ) )
    {
        smashMenuIndex = smashMenuIndex - 1;
        if( smashMenuIndex < 0 ) smashMenuIndex = 3;
    }
    // real gb.menu()'s own cancel (-1) return re-shows the title screen
    if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        smashBeginTitle();
        return;
    }
    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( smashMenuIndex == 0 )
          smashBeginMapSelect();
        else if( smashMenuIndex == 1 )
          smashBeginStatus();
        else if( smashMenuIndex == 2 )
          smashBeginControls();
        else
          // real gb.changeGame() - no in-game equivalent, see header
          // comment - closest available outcome is back to the title
          smashBeginTitle();
    }
}

// -----------------------------------------------------------------------------
// Map select (real maps())
// -----------------------------------------------------------------------------

void smashUpdateMapSelect()
{
    gbSetColor( 1 );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );
    gbDrawBitmap( 24, 38, smashPointBitmap );
    gbDrawBitmapRotated( 52, 38, smashPointBitmap, 0, 1 ); // NOROT, FLIPH

    if( gbRepeat( BTN_LEFT, 15 ) )
    {
        smashMapscroll = smashMapscroll - 1;
        if( smashMapscroll < 0 ) smashMapscroll = 3;
    }
    if( gbRepeat( BTN_RIGHT, 15 ) )
    {
        smashMapscroll = smashMapscroll + 1;
        if( smashMapscroll > 3 ) smashMapscroll = 0;
    }
    if( gbPressed( BTN_A ) )
    {
        smashBeginPlay();
        return;
    }

    if( smashMapscroll == 0 )
    {
        gbCursorX = 24;
        gbCursorY = 16;
        gbPrintString( "Lonely" );
    }
    if( smashMapscroll == 1 )
    {
        gbCursorX = 28;
        gbCursorY = 16;
        gbPrintString( "Ridge" );
    }
    if( smashMapscroll == 2 )
    {
        gbCursorX = 28;
        gbCursorY = 16;
        gbPrintString( "Tower" );
    }
    if( smashMapscroll == 3 )
    {
        gbCursorX = 32;
        gbCursorY = 16;
        gbPrintString( "Hill" );
    }
}

// -----------------------------------------------------------------------------
// Status (real getCpuLoad()/getFreeRam() screen - see header comment: no
// meaning on this virtual console, shown as "N/A" placeholders instead)
// -----------------------------------------------------------------------------

void smashUpdateStatus()
{
    gbSetColor( 1 );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "CPU: N/A" );

    gbCursorX = 2;
    gbCursorY = 14;
    gbPrintString( "Free RAM: N/A" );

    gbCursorX = 2;
    gbCursorY = 34;
    gbPrintString( smashPressBText );

    if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        smashBeginMenu();
    }
}

// -----------------------------------------------------------------------------
// Controls
// -----------------------------------------------------------------------------

void smashUpdateControls()
{
    gbSetColor( 1 );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( smashCtrlJumpText );

    gbCursorX = 2;
    gbCursorY = 14;
    gbPrintString( smashCtrlRunText );

    gbCursorX = 2;
    gbCursorY = 26;
    gbPrintString( smashCtrlPauseText );

    if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        smashBeginMenu();
    }
}

// -----------------------------------------------------------------------------
// Play (real play()) - alive/pause sub-modes preserved exactly as upstream
// modeled them (one screen, two flags), not split into further states.
// -----------------------------------------------------------------------------

void smashUpdatePlay()
{
    gbSetColor( 1 );

    if( smashAlive && !smashPause )
    {
        // Counter
        smashFrames = smashFrames + 1;
        gbCursorX = 0;
        gbCursorY = 0;
        gbPrintNumber( smashFrames );

        // Player
        smashPlayerY = smashPlayerY + smashPlayerGrav;
        gbDrawBitmapRotated( smashPlayerX, smashPlayerY, smashPlayerBitmap, 0, smashPlayerFlip );

        if( gbRepeat( BTN_LEFT, 2 ) && ( smashPlayerX > 0 ) )
        {
            smashPlayerX = smashPlayerX - 2;
            smashPlayerFlip = 1;
        }
        if( gbRepeat( BTN_RIGHT, 2 ) && ( smashPlayerX < 76 ) )
        {
            smashPlayerX = smashPlayerX + 2;
            smashPlayerFlip = 0;
        }
        if( gbRepeat( BTN_LEFT, 2 ) && ( smashPlayerX > 0 ) && gbRepeat( BTN_B, 1 ) )
        {
            gbPlayNote( 6, 1 );
            smashPlayerX = smashPlayerX - 3;
            smashPlayerFlip = 1;
        }
        if( gbRepeat( BTN_RIGHT, 2 ) && ( smashPlayerX < 76 ) && gbRepeat( BTN_B, 1 ) )
        {
            gbPlayNote( 6, 1 );
            smashPlayerX = smashPlayerX + 3;
            smashPlayerFlip = 0;
        }
        if( gbRepeat( BTN_A, 20 ) && smashPlayerJump )
        {
            gbPlayNote( 30, 5 );
            smashPlayerY = smashPlayerY - 1;
            smashPlayerGrav = -7;
        }
        if( gbRepeat( BTN_C, 20 ) )
          smashPause = true;

        // Disaster selection (which hazard type is currently active)
        if( smashChange == smashFrames )
        {
            gbPickRandomSeed();
            smashDisaster = 1 + arand( 11 ); // see header comment point 4 - real range [1,11]
            smashChange = smashChange + 200;
        }

        // Meteor
        if( ( smashDisaster >= 1 ) && ( smashDisaster <= 3 ) )
        {
            gbDrawBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap );
            smashMeteorY = smashMeteorY + 1;
            smashArrowX = 0;
            smashBallX = 0;
        }
        // Arrow
        if( ( smashDisaster >= 4 ) && ( smashDisaster <= 6 ) )
        {
            gbDrawBitmap( smashArrowX, smashArrowY, smashArrowBitmap );
            smashArrowX = (int)( smashArrowX + 1.5 ); // see header comment point 1 - real +1/frame
            smashMeteorY = 0;
            smashBallX = 0;
        }
        if( smashArrowX >= 80 )
        {
            smashArrowX = 0;
            smashArrowY = 6 + arand( 28 ); // see header comment point 8
        }
        // Ball
        if( ( smashDisaster >= 7 ) && ( smashDisaster <= 9 ) )
        {
            gbDrawBitmap( smashBallX, smashBallY, smashBallBitmap );
            smashArrowX = 0;
            smashMeteorY = 0;
        }
        if( smashBallY <= 0 )
          smashBallYv = (int)( 1.5 ); // truncates to 1 - see header comment point 2
        if( smashBallY >= 42 )
          smashBallYv = (int)( -1.5 ); // truncates to -1
        if( ( smashBallX <= 0 ) || ( smashBallX >= 83 ) )
        {
            smashBallY = arand( 42 );
            smashWay = arand( 1 ); // see header comment point 3 - always 0
            if( smashWay == 0 ) smashBallX = 82;
            if( smashWay == 1 ) smashBallX = 1; // real, permanently dead branch
        }
        if( smashWay == 0 )
        {
            smashBallX = (int)( smashBallX - 1.5 ); // see header comment point 1 - real -2/frame
            smashBallY = smashBallY + smashBallYv;
        }
        if( smashWay == 1 )
        {
            smashBallX = (int)( smashBallX + 1.5 ); // real +1/frame - never actually reached
            smashBallY = smashBallY + smashBallYv;
        }
        // Black Hole
        if( ( smashDisaster >= 10 ) && ( smashDisaster <= 12 ) )
        {
            gbDrawBitmap( 77, 10, smashBholeBitmap );
            smashPlayerX = smashPlayerX + 2;
            smashArrowX = 0;
            smashMeteorY = 0;
            smashBallX = 0;
        }

        // Death code
        if( smashPlayerY >= 50 )
          smashKillPlayer();
        if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, smashMeteorX, smashMeteorY, smashMeteorBitmap ) && ( smashDisaster >= 1 ) && ( smashDisaster <= 3 ) )
          smashKillPlayer();
        if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, smashArrowX, smashArrowY, smashArrowBitmap ) && ( smashDisaster >= 4 ) && ( smashDisaster <= 6 ) )
          smashKillPlayer();
        if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, smashBallX, smashBallY, smashBallBitmap ) && ( smashDisaster >= 7 ) && ( smashDisaster <= 9 ) )
          smashKillPlayer();
        if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 77, 10, smashBholeBitmap ) && ( smashDisaster >= 10 ) && ( smashDisaster <= 12 ) )
          smashKillPlayer();

        // Lonely map
        if( smashMapscroll == 0 )
        {
            gbDrawBitmap( 0, 44, smashPlatformBitmap );
            gbDrawBitmap( 30, 28, smashPlatform2Bitmap );

            if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 0, 44, smashPlatformBitmap ) )
            {
                smashPlayerY = 38; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 30, 28, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 22; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else
            {
                smashPlayerGrav = smashPlayerGrav + 1;
                smashPlayerJump = false;
            }

            if( gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 0, 44, smashPlatformBitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 30, 28, smashPlatform2Bitmap ) )
            {
                gbDrawBitmap( smashMeteorX, smashMeteorY + 4, smashBlowBitmap );
                smashMeteorY = -6;
                smashMeteorX = arand( 76 );
            }
            if( gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 30, 28, smashPlatform2Bitmap ) )
            {
                smashArrowX = 0;
                smashArrowY = arand( 34 );
            }
        }

        // Ridge map
        if( smashMapscroll == 1 )
        {
            gbDrawBitmap( 0, 28, smashPlatform2Bitmap );
            gbDrawBitmap( 60, 28, smashPlatform2Bitmap );
            gbDrawBitmap( 18, 44, smashPlatform2Bitmap );
            gbDrawBitmap( 42, 44, smashPlatform2Bitmap );

            if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 0, 28, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 22; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 60, 28, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 22; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 18, 44, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 38; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 42, 44, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 38; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else
            {
                smashPlayerGrav = smashPlayerGrav + 1;
                smashPlayerJump = false;
            }

            if( gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 0, 28, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 60, 28, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 18, 44, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 42, 44, smashPlatform2Bitmap ) )
            {
                gbDrawBitmap( smashMeteorX, smashMeteorY + 4, smashBlowBitmap );
                smashMeteorY = -6;
                smashMeteorX = arand( 76 );
            }
            // real upstream bug (see header comment point 5): the last two
            // checks here test the METEOR's own position, not the arrow's
            if( gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 0, 28, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 60, 28, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 18, 44, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 42, 44, smashPlatform2Bitmap ) )
            {
                smashArrowX = 0;
                smashArrowY = arand( 34 );
            }
        }

        // Tower map
        if( smashMapscroll == 2 )
        {
            gbDrawBitmap( 0, 44, smashPlatformBitmap );
            gbDrawBitmap( 30, 28, smashPlatform2Bitmap );
            gbDrawBitmap( 30, 16, smashPlatform2Bitmap );
            gbDrawBitmap( 18, 0, smashPlatform2Bitmap );
            gbDrawBitmap( 42, 0, smashPlatform2Bitmap );

            if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 0, 44, smashPlatformBitmap ) )
            {
                smashPlayerY = 38; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 30, 28, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 22; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 30, 16, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 10; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            // real upstream bug (see header comment point 6): a phantom
            // full-width check against a platform that is never drawn here
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 0, 0, smashPlatformBitmap ) )
            {
                smashPlayerGrav = 1;
            }
            else
            {
                smashPlayerGrav = smashPlayerGrav + 1;
                smashPlayerJump = false;
            }

            // real upstream bug (see header comment point 7): only 3 of
            // the 5 drawn platforms are actually checked here
            if( gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 18, 0, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 42, 0, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 0, 44, smashPlatformBitmap ) )
            {
                gbDrawBitmap( smashMeteorX, smashMeteorY + 4, smashBlowBitmap );
                smashMeteorY = -6;
                smashMeteorX = arand( 76 );
            }
            if( gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 30, 28, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 30, 16, smashPlatform2Bitmap ) )
            {
                smashArrowX = 0;
                smashArrowY = 6 + arand( 28 ); // see header comment point 8
            }
        }

        // Hill map
        if( smashMapscroll == 3 )
        {
            gbDrawBitmap( 0, 44, smashPlatformBitmap );
            gbDrawBitmap( 18, 32, smashPlatform2Bitmap );
            gbDrawBitmap( 42, 32, smashPlatform2Bitmap );
            gbDrawBitmap( 30, 20, smashPlatform2Bitmap );

            if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 0, 44, smashPlatformBitmap ) )
            {
                smashPlayerY = 38; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 18, 32, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 26; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 42, 32, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 26; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else if( gbCollideBitmapBitmap( smashPlayerX, smashPlayerY, smashPlayerBitmap, 30, 20, smashPlatform2Bitmap ) )
            {
                smashPlayerY = 14; smashPlayerGrav = 0; smashPlayerJump = true;
            }
            else
            {
                smashPlayerGrav = smashPlayerGrav + 1;
                smashPlayerJump = false;
            }

            if( gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 0, 44, smashPlatformBitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 18, 32, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 42, 32, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashMeteorX, smashMeteorY, smashMeteorBitmap, 30, 20, smashPlatform2Bitmap ) )
            {
                gbDrawBitmap( smashMeteorX, smashMeteorY + 4, smashBlowBitmap );
                smashMeteorY = 0;
                smashMeteorX = arand( 76 );
            }
            if( gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 18, 32, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 42, 32, smashPlatform2Bitmap ) || gbCollideBitmapBitmap( smashArrowX, smashArrowY, smashArrowBitmap, 30, 20, smashPlatform2Bitmap ) )
            {
                smashArrowX = 0;
                smashArrowY = arand( 34 );
            }
        }
    }
    else if( smashAlive && smashPause )
    {
        gbSetFont( gbFont5x7 );
        gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

        gbCursorX = 24;
        gbCursorY = 4;
        gbPrintString( "PAUSED" );

        gbCursorX = 12;
        gbCursorY = 20;
        gbPrintNumber( smashFrames );
        gbPrintString( " frames" );

        gbCursorX = 8;
        gbCursorY = 36;
        gbPrintString( smashResumeText );

        if( gbPressed( BTN_B ) )
        {
            gbSetFont( gbFont3x5 );
            smashPause = false;
        }
    }
    else // !smashAlive
    {
        gbSetFont( gbFont5x7 );
        gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

        gbCursorX = 16;
        gbCursorY = 2;
        gbPrintString( "You died!" );

        gbCursorX = 28;
        gbCursorY = 18;
        gbPrintString( "Time:" );

        gbCursorX = 8;
        gbCursorY = 26;
        gbPrintNumber( smashFrames );
        gbPrintString( " frames." );

        gbCursorX = 20;
        gbCursorY = 38;
        gbPrintString( smashPressBText );

        if( gbPressed( BTN_B ) )
        {
            gbPlayCancel();
            smashBeginMenu();
        }
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameSmash_init()
{
    gbBegin();
    gbSetColor( 1 );
    smashMenuIndex = 0;
    smashMapscroll = 0;
    smashResetGameState();
    smashBeginTitle();
}

void gameSmash_update()
{
    if( !gbUpdate() ) return;

    if( smashState == SMASH_STATE_TITLE ) smashUpdateTitle();
    else if( smashState == SMASH_STATE_MENU ) smashUpdateMenu();
    else if( smashState == SMASH_STATE_MAPSELECT ) smashUpdateMapSelect();
    else if( smashState == SMASH_STATE_STATUS ) smashUpdateStatus();
    else if( smashState == SMASH_STATE_CONTROLS ) smashUpdateControls();
    else smashUpdatePlay();

    gbRenderFrame();
}
