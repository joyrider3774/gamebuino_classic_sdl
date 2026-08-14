#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Parachute (Jicehel, license: none specified upstream - no LICENSE file in
// the repo - github.com/jicehel/Parachute_Gamebuino). A Nintendo Game &
// Watch "Parachute" remake: a helicopter drops paratroopers into one of 3
// lanes, the player slides a rowboat left/right across those same 3 lanes
// to catch them before they hit the water, where a shark and drowning
// swimmers take over the animation instead. Score increases per catch, and
// the game speeds up (shorter spawn delay, faster fall rate) as the score
// grows - there is no upstream game-over condition at all (misses are
// merely tallied, never checked against any limit), so this port doesn't
// invent one either.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why).
// `random(N)`/`int(random(N))` became `arand(N)` (this dialect's own
// established RNG helper). Upstream's own blocking `gb.titleScreen(F(
// "Parachute"), titleScreenBitmap)` - called once in setup(), and again
// from inside loop() as a "pause" gesture on a Button C press, with no
// state reset either time - was converted into an explicit
// PARA_STATE_TITLE/PARA_STATE_PLAY state machine exactly like gamePong.c's
// own PongState (dismissed by a genuine Button A press, re-entered from
// play on Button C, resuming play afterward with score/lanes/parachutes
// untouched, matching upstream's own "titleScreen() as a pause menu, not a
// reset" behavior).
//
// UPDATE: real bitmap art restored. An earlier pass of this port (before
// this shim had a drawBitmap() primitive at all) replaced every one of
// upstream's own hand-drawn PROGMEM sprite bitmaps with small geometric
// stand-ins (rects/lines/circles) built from this shim's own primitives.
// Now that gbDrawBitmap()/gbDrawBitmapRotated() exist (matching real
// Gamebuino Classic's own Display::drawBitmap() format/behavior exactly -
// see gamebuinoShim.h's own header comment on those two functions), every
// one of those bitmaps has been restored below and every placeholder
// drawing function deleted - the rowboat (barque), the helicopter body
// (Helico) + single spinning-blade sprite (Helice, drawn 2-4 times per
// frame at different offsets exactly like upstream), the 5 real falling-
// paratrooper stage bitmaps (Para_1..Para_5 - stages 6 and 7 reuse Para_5
// at different coordinates, matching upstream exactly, not a 7th/8th
// distinct bitmap), the 3 real "missed paratrooper drowning" bitmaps
// (Noye_1, Noye_2, Noye_2 again horizontally flipped for the 3rd stage -
// matching upstream's own `drawBitmap(14, 39, Noye_2, 0, FLIPH)`), the 2
// real swimmer bitmaps (Nageur used for 2 stages, Parra_m for the last),
// the shark's own single fin bitmap (aileron - reused unflipped for 3
// stages and horizontally flipped for 2 more, matching upstream's own
// `shark[]` array which really is `{aileron,aileron,aileron,aileron,
// aileron,requin_m}`, not 5 distinct fin bitmaps) and full-shark bitmap
// (requin_m), the 88x48 background water bitmap (subBackgroundBitmap -
// drawn at (0,0); its rightmost 4 columns fall past this shim's own
// 84-wide LCD_WIDTH and are silently clipped by gbDrawPixel()'s own bounds
// check, exactly matching real hardware, which has the same 84px-wide
// display upstream's own bitmap already overhangs by the same 4 columns),
// and the 80x32 title-screen logo bitmap (titleScreenBitmap), now drawn in
// paraUpdateTitle() where only plain text stood before.
//
// One real upstream bitmap was found genuinely unused and was NOT ported:
// `Miss[]` (8x5) is declared but never referenced by any `drawBitmap()`
// call anywhere in the 408-line source (confirmed by checking every
// remaining reference) - dead art on real hardware too, not just here.
//
// UPDATE: real score text restored. `paraUpdatePlay()`'s own score line
// used to read "SCORE " + the number at a hand-tuned x=48, adapted for
// this shim's old fixed 8x8 font; now reads upstream's own real
// "Score :    " (with its own literal colon and trailing spaces) + the
// number at upstream's own real cursorX=30, both practical now that
// gbSetFont()/real fonts exist (upstream's own default font3x5 - its own
// `setFont(font5x7)` call here is commented out, dead code, in the real
// source). Upstream's own real `setColor(BLACK, WHITE)` (an explicit WHITE
// background so the text overwrites whatever's behind it, not just the
// "on" pixels) has no equivalent here - this shim's `gbSetColor()` only
// supports a single foreground color, the same established limitation
// gameFlappyBirdo.c's own header comment documents - harmless here since
// nothing else is ever drawn under this text before it prints.
//
// B-binary-literal conversion method: this .ino file declares every real
// bitmap byte using Arduino's `Bxxxxxxxx` binary-literal syntax (not valid
// in this project's own C dialect - only `titleScreenBitmap` and the
// sibling project's own bitmaps use plain `0x..` hex, which needed no
// conversion at all). Rather than hand-converting ~30 bitmaps' worth of
// bytes (528 of them for the background bitmap alone), every `const byte
// NAME[] PROGMEM = { width, height, B........, ... };` block was extracted
// from the real .ino source with a small Python script (regex over the
// full file, in original declaration order) that parses each `Bxxxxxxxx`
// token as base-2 and each `0x..` token as base-16, emits every byte as a
// decimal-free `0x..` hex literal (matching gameConduit.c's own existing
// bitmap-literal style in this same project), and cross-checks the
// resulting byte count against `ceil(width/8) * height` computed from
// each bitmap's own declared header - every one of the 17 real bitmaps in
// the source (16 used + the 1 dead `Miss[]` above) matched its expected
// byte count exactly, a strong end-to-end correctness signal on top of
// the conversion itself being entirely mechanical. Spot-checked several
// conversions by hand afterward against the script's own output: `barque`'s
// first two bytes `B00111000,B00000000` -> 0x38,0x0 (00111000b = 32+16+8 =
// 56 = 0x38); `Helice`'s single byte `B11111000` -> 0xF8 (128+64+32+16+8 =
// 248 = 0xF8); `Para_1`'s second byte `B00100111` -> 0x27 (32+4+2+1 = 39 =
// 0x27) - all three matched the script's own output exactly.
//
// Several upstream globals turned out to be genuinely dead code once read
// closely (confirmed by checking every remaining reference in the 408-line
// source, not assumed) and were dropped outright rather than ported:
// - `long highscore;` and `#include <EEPROM.h>` - despite the include,
//   nothing in the source ever calls EEPROM.read()/.write() anywhere, and
//   `highscore` itself is never read, written, or displayed after being
//   declared. A genuinely vestigial, never-wired-up feature - there is no
//   real high-score persistence to port through.
// - `byte gameState;` and `short x;` - declared, never read or written
//   anywhere else in the file.
// - The analog joystick support (`JoyX_pin`/`JoyX_pos`/`analogRead(...)`)
//   and `manage_joystick` - `manage_joystick` is initialized to 0 in
//   initGame() and never assigned anywhere else in the source, so every
//   `... && manage_joystick == 1` branch that would read the analog stick
//   is permanently dead even on real hardware. Vircon32 has no analog
//   stick input anyway.
// - `gb.battery.show = false;` - no battery API exists in this shim
//   (purely cosmetic on real hardware too).
//
// Two real upstream oddities found while reading the source closely:
// - Test_Barque()'s own miss-handling branch reads `Nb_Parachutes_launched
//   == 1;` where an assignment (`= 1`) was obviously intended - a bare
//   comparison statement whose result is simply discarded, so the count of
//   in-flight parachutes is never actually capped back down to 1 the way
//   the surrounding code (which *does* zero out every other slot) clearly
//   means it to be. Initially preserved exactly as upstream wrote it in
//   paraTestBoat() below (real cartridges ship with this exact behavior,
//   leaving harmless invisible "ghost" slots that quietly cycle through
//   the drowning-swimmer animation instead of disappearing immediately) -
//   **since fixed to a real `= 1` assignment at direct user request**,
//   once this behavior was explained and the user asked for the actual fix
//   rather than the faithful-typo reproduction. See paraTestBoat()'s own
//   comment for the fix itself.
// - Dessine_Para()'s own case 6 (lane 1 or lane 3 sub-position) contains a
//   single corrupted, non-ASCII character in its Y coordinate literal
//   (`gb.display.drawBitmap(16, 1<corrupted-char>, Para_5)`) - almost
//   certainly meant to be the coordinate 17, matching both the neighboring
//   lane-2 branch of the very same case and the very next case's own
//   fixed Y of 17. Normalized to 17 below (the paraFall5Bitmap draw at
//   (16, 17)) as an obvious encoding artifact with no plausible alternate
//   reading, not a load-bearing quirk.
//
// paraPosition[]'s backing array was sized 32 (PARA_MAX_PARACHUTES) rather
// than upstream's fixed 10 (Position_Parachute[10]) - a pure storage-
// capacity cushion, not a behavior change. Upstream's own
// Nb_Parachutes_launched has no explicit cap and is only indirectly kept
// in check by ordinary catch/miss removal, so at a high enough score (fast
// spawn rate, slow removal) it could in principle grow past its own
// 10-slot array - undefined behavior on real AVR hardware that often goes
// unnoticed; Vircon32 has no memory protection at all, so this port simply
// gives the array more headroom instead of reproducing that risk.

// -----------------------------------------------------------------------
//   Real upstream sprite bitmaps, restored via gbDrawBitmap() (see the
//   B-literal conversion method described in this file's own header
//   comment above). Each array is { width, height, byte0, byte1, ... },
//   copied byte-for-byte from the real .ino source in original row-major,
//   MSB-first order.
// -----------------------------------------------------------------------

// Boat (upstream: barque) - the player's rowboat, 16x7.
int paraBoatBitmap[16]=
{ 16, 7, 0x38,0x0,0x5C,0x0,0x78,0x0,0x30,0x0,0xDC,0x0,0xFF,0xFF,0xCF,0xFE };

// Helicopter (upstream: Helico body 24x10, Helice single blade-segment
// 8x1 - drawn 2-4 times per frame at different offsets for the spinning-
// blade animation, matching upstream's own anim_helico() exactly).
int paraHelicoBodyBitmap[32]=
{ 24, 10, 0x4,0x0,0x0,0x4,0x4,0x0,0xE,0x4,0x0,0x1F,0x1F,0x0,0x1F,0xF9,0x80,
  0x1E,0xA8,0xC0,0xE,0xAF,0xC0,0x7,0xFF,0x80,0x3,0x6,0x0,0x3,0x6,0x0 };
int paraBladeBitmap[3]= { 8, 1, 0xF8 };

// Falling-paratrooper stages (upstream: Para_1..Para_5). Stages 6 and 7
// both reuse Para_5 at different screen coordinates, matching upstream's
// own Dessine_Para() exactly - there is no separate 6th/7th bitmap.
int paraFall1Bitmap[10]= { 8, 8, 0x4,0x27,0x33,0x9F,0xDC,0x7F,0x32,0x60 };
int paraFall2Bitmap[24]=
{ 16, 11, 0x0,0x18,0x0,0x38,0x1C,0x70,0x3C,0xE0,0x9D,0xC0,0x6F,0x0,0x1E,0x0,
  0x7F,0x0,0xD9,0x80,0x30,0x0,0x60,0x0 };
int paraFall3Bitmap[32]=
{ 16, 15, 0x1,0x80,0x3,0x80,0x7,0x80,0xD,0x0,0x1B,0x0,0x1E,0x0,0x4,0x0,0x38,
  0x0,0x3C,0x0,0x38,0x0,0x9B,0x0,0x7C,0x0,0x30,0x0,0x50,0x0,0xD8,0x0 };
int paraFall4Bitmap[36]=
{ 16, 17, 0xF,0xE0,0x3B,0xF8,0x73,0xDC,0xE7,0xCE,0xFF,0xCE,0xE4,0xFE,0x84,
  0x4E,0x44,0x42,0x24,0xC4,0x17,0xC8,0xD,0xD0,0x7,0xE0,0x1,0x90,0x1,0xF0,
  0x1B,0x80,0xC,0xE0,0x0,0x30 };
int paraFall5Bitmap[56]=
{ 24, 18, 0x3,0xE0,0x0,0x1F,0xF8,0x0,0x39,0xFE,0x0,0x71,0xFF,0x0,0x73,0xFB,
  0x80,0xFF,0xF9,0x80,0xC7,0x79,0xC0,0x82,0x1F,0xC0,0x42,0xB,0xC0,0x23,0x8,
  0xC0,0x17,0x90,0x40,0xB,0x91,0x80,0x25,0xBE,0x0,0x37,0x60,0x0,0x1F,0xE0,
  0x0,0x23,0x0,0x0,0x3F,0xF0,0x0,0x30,0x60,0x0 };

// Drowning/swimmer stages (upstream: Noye_1, Noye_2 [also horizontally
// flipped for the 3rd drowning stage], Nageur, Parra_m).
int paraDrown1Bitmap[12]=
{ 16, 5, 0x44,0x40,0xE,0x0,0xAA,0x80,0x1F,0x0,0xE,0x0 };
int paraDrown2Bitmap[12]=
{ 16, 5, 0x0,0x40,0x4E,0x0,0xD,0x40,0x3F,0x0,0xE,0x0 };
int paraSwimBitmap[7]= { 8, 5, 0x40,0xEE,0xFF,0x1E,0x7C };
int paraSwimFinalBitmap[7]= { 8, 5, 0xC,0xB6,0x86,0xFC,0x3C };

// Shark (upstream: aileron - the fin, reused unflipped for 3 stages and
// horizontally flipped for 2 more; requin_m - the full shark body).
int paraSharkFinBitmap[10]= { 8, 8, 0x10,0x70,0x70,0xF0,0x0,0x0,0x0,0x0 };
int paraSharkBodyBitmap[14]=
{ 16, 6, 0x0,0x38,0xC1,0xF8,0xF6,0xD0,0xFF,0x80,0x7E,0x30,0xFF,0xF0 };

// Background water (upstream: subBackgroundBitmap, 88x48 - 4px wider than
// this shim's own 84px-wide LCD_WIDTH; the overhang is silently clipped by
// gbDrawPixel()'s own bounds check, matching real hardware exactly).
int paraBackgroundBitmap[530]=
{ 88, 48,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x10,0xE9,0x88,0xB5,0x7B,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0xB5,0x55,0x75,0x52,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xDD,
  0x9D,0x3D,0x53,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x95,0x55,0x75,0x52,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x95,0x54,0xB5,0xD3,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x20,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x1A,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x6E,0xA7,0xC0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xFF,0xFF,0xF8,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0xFF,0xFF,0xFC,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x7F,0xFD,0x74,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xFF,0x48,0x10,0x0,0x0,0x0,0x0,0x0,0x0,
  0x4,0xB0,0x9C,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0xCD,0xB0,0x18,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x1E,0xFF,0xF0,0x38,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x7F,
  0xFF,0xF0,0x30,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0xFF,0xFF,0xF0,0x70,0x0,0x0,
  0x0,0x0,0x0,0x0,0x2,0x7F,0x7F,0xE0,0x60,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x44,
  0xFB,0xB0,0x60,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0xF9,0xC0,0xE0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0xA8,0xC0,0xE0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x20,0xE0,0xE0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xF0,0xE0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xF0,0xF0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x7,0xF0,0xFE,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1F,0xF0,0xFF,0x80,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x3F,0xF0,0xFF,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0xFF,0xF0,0xFF,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xFF,0xF0,0xFE,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0xFF,0xF0,0xFC,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x7,0xF0,0xFC,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0xF0,0xF8,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x1,0xF0,0xF8,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0xF0,0xF8,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xF0,0xF8,0x0,0x0,0x0,0x0,
  0x38,0x0,0x0,0x0,0x0,0x70,0xF0,0xF3,0x1E,0x61,0xF9,0xFC,0xCF,0xE0,0x10,
  0x20,0xF0,0x0,0x4,0x0,0x0,0x0,0x40,0x0,0x30,0x0,0x0,0x0,0xE0,0x80,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xE,0x0,0x0,0x0,0x0,0x8,0x0,0x3,0xF1,
  0xC3,0xE0,0xF1,0xE0,0x0,0x0,0x0,0x8,0x0,0x0,0xF,0x3F,0x0,0xE,0x0,0x0,
  0x7C,0xF,0x3E,0x0,0x0,0x0,0xC0,0x0,0xC0,0x7C,0x0,0x0,0x0,0x8,0x0,0x0,
  0x0,0x1,0xF0,0x0,0x0,0x0,0x0,0x0,0x8,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x30,0x0,0x1,0xF9,0xE1,0xF3,0xC7,0xFC,0xF,
  0x80,0x0,0x0,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x10 };

// Title screen logo (upstream: titleScreenBitmap, 80x32 - already real
// hex 0x.. literals upstream, not B-binary, so this one needed no
// conversion at all).
int paraTitleBitmap[322]=
{ 80, 32,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,0x0,0x0,0x0,0x0,0xFF,0xFF,0xFF,
  0xFF,0xFF,0xC0,0x0,0x0,0x0,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,0xF0,0x0,0x0,0x0,0x3,
  0xFF,0xFF,0xFF,0xFF,0xFF,0xF0,0x0,0x0,0x0,0x3,0xE0,0x0,0x0,0x1,0xFF,0xF0,0x0,
  0x0,0x0,0x3,0xE0,0x0,0x4F,0x9,0xFD,0xF0,0x0,0x0,0x0,0x3,0xE0,0x0,0x3F,0x80,
  0xAB,0xF0,0x0,0x0,0x0,0x3,0xE1,0xE0,0xE7,0xC2,0xFF,0xF0,0x0,0x0,0x0,0x3,0xFF,
  0xF1,0xFF,0xE1,0x29,0xF0,0x0,0x0,0x0,0x3,0xFF,0x81,0x89,0xE3,0xFD,0xF0,0x0,0x0,
  0x0,0x3,0xFF,0x81,0xC9,0xE1,0xFF,0xF0,0x0,0x0,0x0,0x3,0xFF,0x81,0x4E,0x20,0xF9,
  0xF0,0x0,0x0,0x0,0x3,0xF6,0xE0,0x36,0x50,0x67,0xF0,0x0,0x0,0x0,0x3,0xF0,0x0,
  0x1F,0xA0,0x27,0xF0,0x0,0x0,0x0,0x3,0xFC,0x0,0x7D,0x0,0xF,0xF0,0x0,0x0,0x0,
  0x3,0xFC,0x0,0x37,0x0,0x1F,0xF0,0x0,0x0,0x0,0x3,0xF8,0x7B,0x22,0x1,0xFF,0xF0,
  0x0,0x0,0x0,0x3,0xF8,0x3,0xF0,0x0,0x7,0xF0,0x0,0x0,0x0,0x3,0xFF,0x80,0x80,
  0x0,0x7,0xF0,0x0,0x0,0x0,0x3,0xF8,0x0,0x0,0x19,0x87,0xF0,0x0,0x0,0x0,0x3,
  0xE0,0x0,0x1,0xF2,0x87,0xF0,0x0,0x0,0x0,0x3,0xF0,0x72,0x80,0x78,0x1,0xF0,0x0,
  0x0,0x0,0x3,0xFF,0xFF,0xFF,0xFF,0xFF,0xF0,0x0,0x0,0x0,0x3,0xFF,0xFF,0xFF,0xFF,
  0xFF,0xF0,0x0,0x0,0x0,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,0x0,0x0,0x0,0x0,0xFF,
  0xFF,0xFF,0xFF,0xFF,0xC0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0 };

enum ParaState
{
    PARA_STATE_TITLE = 0,
    PARA_STATE_PLAY = 1
};

int paraState;

int paraScore;
int paraMisses;
int paraBoatPosition;   // 1 (left lane) .. 3 (right lane)
int paraMoveTick;
int paraSpawnDelay;
int paraSpawnCount;
int paraSpeedMax;

#define PARA_MAX_PARACHUTES 32
int paraNbLaunched;
// Encodes each in-flight parachute as (lane * 10 + stage), exactly matching
// upstream's own Position_Parachute[] packing - lane 1..3 while falling,
// lane 4 once it has missed and become a "swimmer" in the water. Index 0 is
// never used (upstream's own loops always run from index 1), matching
// upstream exactly.
int paraPosition[PARA_MAX_PARACHUTES];

int paraSharkAnim;
int paraHelicoAnim;
int paraBladeSpeed;

// -----------------------------------------------------------------------
//   Game logic - a direct port of Test_Barque()/Dessine_Para()/anim_*()
// -----------------------------------------------------------------------

// Direct port of Test_Barque(). currentIndex is the paraPosition[] slot of
// the parachute being tested; colonne is its lane (1..3).
void paraTestBoat( int colonne, int currentIndex )
{
    int count;

    if( colonne == 4 - paraBoatPosition )
    {
        // caught - remove this slot, shifting everything above it down
        paraScore = paraScore + 1;
        for( count = currentIndex; count < paraNbLaunched; count++ )
          paraPosition[ count ] = paraPosition[ count + 1 ];
        paraNbLaunched = paraNbLaunched - 1;
    }
    else
    {
        // missed - this slot becomes a "lane 4" (in the water) swimmer
        paraMisses = paraMisses + 1;
        paraPosition[ currentIndex ] = 4 * 10 + colonne;
        paraSharkAnim = colonne;
        paraMoveTick = 0;

        if( paraNbLaunched > 1 )
        {
            for( count = 2; count <= paraNbLaunched; count++ )
              paraPosition[ count ] = 0;

            // FIXED (was preserved verbatim earlier - see this file's own
            // header comment for the original reasoning): upstream's own
            // real source has `Nb_Parachutes_launched == 1;` here, a
            // genuine `==`/`=` typo that makes this a no-op comparison
            // instead of actually capping the count back down to 1, so
            // every other in-flight paratrooper's slot got zeroed out
            // above but never actually stopped being tracked (left sitting
            // at an otherwise-unused (column 0, position 0) state). Fixed
            // at direct user request once this behavior was actually
            // explained and the user asked for the real fix rather than
            // the faithful-typo reproduction.
            paraNbLaunched = 1;
        }

        paraSpawnCount = paraSpawnDelay;
    }
}

// Direct port of Dessine_Para(). colX is the lane (1..3 while falling, 4
// once in the water), posY the current fall/drown stage, index this
// parachute's own paraPosition[] slot (only needed for the lane-4/stage-7
// "swimmer finished, remove from the list" case). Bitmap choice/position
// per stage matches upstream's own real drawBitmap() call sites exactly.
void paraDrawParachute( int colX, int posY, int index )
{
    int count;

    if( colX < 4 )
    {
        if( posY == 1 ) gbDrawBitmap( 72 - 6 * colX, 9 - 3 * colX, paraFall1Bitmap );
        else if( posY == 2 ) gbDrawBitmap( 67 - 7 * colX, 11 - 3 * colX, paraFall2Bitmap );
        else if( posY == 3 ) gbDrawBitmap( 56 - 6 * colX, 14 - 5 * colX, paraFall3Bitmap );
        else if( posY == 4 ) gbDrawBitmap( 54 - 8 * colX, 15 - ( colX - 1 ) * 4, paraFall4Bitmap );
        else if( posY == 5 ) gbDrawBitmap( 62 - 16 * colX, 12 + ( 3 - colX ) * 3, paraFall5Bitmap );
        else if( posY == 6 )
        {
            if( colX == 2 ) gbDrawBitmap( 29, 17, paraFall5Bitmap );
            else gbDrawBitmap( 16, 17, paraFall5Bitmap ); // normalized upstream corrupted-char typo, see header
        }
        else if( posY == 7 ) gbDrawBitmap( 13, 17, paraFall5Bitmap );
        // posY == 8 is unreachable in practice - every lane's own
        // catch/miss threshold (4 + colX) fires at or before posY 7.
    }
    else
    {
        if( posY == 1 ) gbDrawBitmap( 48, 39, paraDrown1Bitmap );
        else if( posY == 2 ) gbDrawBitmap( 30, 39, paraDrown2Bitmap );
        else if( posY == 3 ) gbDrawBitmapRotated( 14, 39, paraDrown2Bitmap, 0, 1 ); // FLIPH
        else if( posY == 4 ) gbDrawBitmap( 21, 43, paraSwimBitmap );
        else if( posY == 5 ) gbDrawBitmap( 37, 43, paraSwimBitmap );
        else if( posY == 6 ) gbDrawBitmap( 62, 43, paraSwimFinalBitmap );
        else if( posY == 7 )
        {
            // finished drowning - remove this slot, shifting the rest down
            for( count = index; count < paraNbLaunched; count++ )
              paraPosition[ count ] = paraPosition[ count + 1 ];
            paraNbLaunched = paraNbLaunched - 1;
        }
    }
}

// Direct port of anim_para().
void paraAnimateParachutes()
{
    int i;
    int colX;
    int posY;

    for( i = 1; i <= paraNbLaunched; i++ )
    {
        colX = paraPosition[ i ] / 10;
        posY = paraPosition[ i ] - colX * 10;
        paraDrawParachute( colX, posY, i );

        if( paraMoveTick == 0 )
        {
            if( ( posY >= 4 + colX ) && ( colX <= 3 ) )
              paraTestBoat( colX, i );
            else
              paraPosition[ i ] = paraPosition[ i ] + 1;
        }
    }
}

// Direct port of anim_shark(). Upstream's own shark[] sprite table really
// is `{aileron,aileron,aileron,aileron,aileron,requin_m}` - just one real
// fin bitmap reused (unflipped for stages 1-3, horizontally flipped via
// FLIPH for stages 4-5) plus one full-shark-body bitmap for stage 6, not
// 6 distinct sprites - reproduced here exactly the same way.
void paraAnimateShark()
{
    if( paraSharkAnim == 1 ) gbDrawBitmap( 62, 38, paraSharkFinBitmap );
    else if( paraSharkAnim == 2 ) gbDrawBitmap( 46, 38, paraSharkFinBitmap );
    else if( paraSharkAnim == 3 ) gbDrawBitmap( 32, 38, paraSharkFinBitmap );
    else if( paraSharkAnim == 4 ) gbDrawBitmapRotated( 5, 42, paraSharkFinBitmap, 0, 1 ); // FLIPH
    else if( paraSharkAnim == 5 ) gbDrawBitmapRotated( 24, 42, paraSharkFinBitmap, 0, 1 ); // FLIPH
    else if( paraSharkAnim == 6 ) gbDrawBitmap( 48, 40, paraSharkBodyBitmap );
    else if( paraSharkAnim == 7 ) paraSharkAnim = 0;

    if( paraMoveTick <= 0 )
    {
        if( ( paraSharkAnim > 0 ) || ( ( paraSharkAnim == 0 ) && ( arand( 50 ) < 5 ) ) )
          paraSharkAnim = paraSharkAnim + 1;
    }
}

// Direct port of anim_helico(). Each blade "frame" is really the same
// single 8x1 Helice bitmap drawn 2-4 times at different offsets, exactly
// like upstream's own switch(helico_anim).
void paraAnimateHelico()
{
    paraBladeSpeed = paraBladeSpeed - 1;
    if( paraBladeSpeed == 0 )
    {
        if( paraHelicoAnim == 0 )
        {
            gbDrawBitmap( 65, 0, paraBladeBitmap );
            gbDrawBitmap( 71, 0, paraBladeBitmap );
        }
        else if( paraHelicoAnim == 1 )
        {
            gbDrawBitmap( 65, 0, paraBladeBitmap );
            gbDrawBitmap( 71, 0, paraBladeBitmap );
            gbDrawBitmap( 73, 1, paraBladeBitmap );
            gbDrawBitmap( 79, 1, paraBladeBitmap );
        }
        else if( paraHelicoAnim == 2 )
        {
            gbDrawBitmap( 73, 1, paraBladeBitmap );
            gbDrawBitmap( 79, 1, paraBladeBitmap );
        }
        else if( paraHelicoAnim == 3 )
          paraHelicoAnim = 0;

        paraHelicoAnim = paraHelicoAnim + 1;
        paraBladeSpeed = 5;
    }
}

// -----------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------

// Direct port of initGame().
void paraInitGame()
{
    paraMisses = 0;
    paraScore = 0;
    paraBoatPosition = 1;
    paraSpawnDelay = 4;
    paraSharkAnim = 0;
    paraSpeedMax = 25;
    paraHelicoAnim = 0;
    paraBladeSpeed = 5;
    paraMoveTick = paraSpeedMax;
    paraSpawnCount = paraSpawnDelay;
    paraNbLaunched = 0;
}

void paraBeginTitle()
{
    paraState = PARA_STATE_TITLE;
}

void paraBeginPlay()
{
    paraState = PARA_STATE_PLAY;
}

void paraUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( 2, 0, paraTitleBitmap );

    gbFontSize = 1;
    gbCursorX = 14;
    gbCursorY = 39;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      paraBeginPlay();
}

void paraUpdatePlay()
{
    // pause the game if C is pressed - matches upstream's own re-entry
    // into gb.titleScreen() here, with no state reset either way
    if( gbPressed( BTN_C ) )
    {
        paraBeginTitle();
        return;
    }

    gbSetColor( 1 );
    gbDrawBitmap( 0, 0, paraBackgroundBitmap );

    // move the boat - single-step per press, matching upstream's own
    // edge-triggered gb.buttons.pressed() (not the auto-repeating
    // gb.buttons.repeat()) for this game's boat control
    if( gbPressed( BTN_LEFT ) || gbPressed( BTN_A ) )
    {
        if( paraBoatPosition > 1 )
          paraBoatPosition = paraBoatPosition - 1;
    }
    if( gbPressed( BTN_RIGHT ) || gbPressed( BTN_B ) )
    {
        if( paraBoatPosition < 3 )
          paraBoatPosition = paraBoatPosition + 1;
    }
    if( paraBoatPosition < 1 ) paraBoatPosition = 1;
    if( paraBoatPosition > 3 ) paraBoatPosition = 3;

    gbDrawBitmap( paraBoatPosition * 16 - 8, 30, paraBoatBitmap );

    if( paraMoveTick > 0 )
      paraMoveTick = paraMoveTick - 1;
    else
    {
        paraSpawnCount = paraSpawnCount - 1;
        if( ( paraSpawnCount < 1 ) && ( arand( 6 - paraScore / 200 ) < 2 ) )
        {
            int lane = arand( 3 ) + 1;
            paraNbLaunched = paraNbLaunched + 1;
            paraPosition[ paraNbLaunched ] = lane * 10 + 1;
            paraSpawnCount = paraSpawnDelay - paraScore / 100;
        }
        paraMoveTick = paraSpeedMax - paraScore / 100;
    }

    gbDrawBitmap( 65, 0, paraHelicoBodyBitmap );

    paraAnimateShark();
    paraAnimateParachutes();
    paraAnimateHelico();

    // Real upstream's own `setColor(BLACK, WHITE)` - a genuine opaque WHITE
    // text background, not just BLACK ink: without it, the background
    // bitmap's own real palm-tree pixels (already drawn underneath, at
    // this same (0,0) corner) show through between glyphs. Found by
    // comparing against a real-hardware screenshot showing clean text -
    // restored via gbSetColorBg() once it existed (see gamebuinoShim.c's
    // own header comment on it).
    gbSetColorBg( 1, 0 );
    gbFontSize = 1;
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Score :    " );
    gbCursorX = 30;
    gbCursorY = 0;
    gbPrintNumber( paraScore );
    gbSetColor( 1 ); // restore a plain transparent foreground for every other draw call this frame
}

void gameParachute_init()
{
    gbBegin();
    paraInitGame();
    paraBeginTitle();
}

void gameParachute_update()
{
    if( !gbUpdate() ) return;

    if( paraState == PARA_STATE_TITLE ) paraUpdateTitle();
    else paraUpdatePlay();

    gbRenderFrame();
}
