#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// PinBall (Clement83, https://github.com/Clement83/pinBall, license: none
// specified in the real repo - confirmed directly, no LICENSE file, no
// license comment in either real source file). A small real-time pinball
// table: two flippers (Button UP/DOWN), a pull-back launch spring (hold
// Button LEFT to lower the spring, release to fire the ball up the right
// lane), 5 real bumper targets and a scattering of scoring wall segments/
// "flasher" strips, plus a shake mechanic (Button A jolts the ball's own
// velocity by a small random amount, real upstream's own crude stand-in
// for a real hardware "nudge the table" feature).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)`/`pinbY(...)` call (this dialect has no classes/methods - see
// gamebuinoShim.h's own header comment). Upstream's own blocking
// `gb.titleScreen(pbStartMenu)` (called once from `setup()`, and again any
// time Button C is pressed mid-game via `goTitleScreen()`) became an
// explicit PINB_STATE_TITLE/PINB_STATE_PLAY state machine, the same
// "blocking widget -> explicit resumable state" treatment used throughout
// this project (see gamePong.c's own header comment). Real
// `Gamebuino::titleScreen(logo)` draws its passed-in bitmap at a fixed
// (0,12) screen anchor and no text of its own - confirmed previously
// against the real Gamebuino.cpp source during gameArmageddon.c's/
// gameUfoRace.c's own ports of that same function, reused directly here -
// the "PRESS A" prompt is this port's own added affordance, matching every
// other restored real titleScreen() call in this project.
//
// STRUCT-BY-VALUE REWRITES: upstream's own `Droite` (4 floats), `Circle`
// (5 floats) and `Vecteur` (2 floats) are all well over this dialect's
// one-word function-parameter/return-value limit (VIRCON32_C_DIALECT.md
// section 4/5). Every function that took one of these by value now takes a
// pointer instead (`pinbCollisionSegment(PinbLine* d1)` etc, using the
// single global `pinbBall` directly rather than threading a `Circle`
// parameter through, since upstream only ever calls these with the one
// real ball anyway), and every function that returned a `Vecteur`
// (`ProjectionI`/`GetNormale`/`CalculerVecteurV2`) became `void` with a
// result out-pointer as its last parameter, matching this project's own
// already-proven `b2Vec2`-style rewrite idiom.
//
// REAL UPSTREAM QUIRKS PRESERVED DELIBERATELY (not porting mistakes):
// - Restarting via Button C (title screen) -> Button A does NOT reset
//   `pinbScore`, the flasher timers, or the ball's own velocity - only
//   the ball's position, life count, and flipper geometry are reset
//   (`pinbInitGame()`, a direct port of real `initGame()`, never touches
//   any of those). There is no real "game over" screen at all: losing the
//   ball just decrements `pinbLives`, and once it goes below 0 it wraps
//   straight back to 2 rather than ending the game - a real, deliberate
//   (if unusual) "the game never actually ends" design, ported exactly.
// - `CollisionBallExtremiter()` is a genuine dead stub in the real source
//   - declared, called, but its body is only ever `return false;` with no
//   real implementation at all. Rather than port a separate always-false
//   function, `pinbCollisionSegment()` just falls through to `return
//   false;` directly at that same point.
// - `collideDroite()` (a real, complete segment/segment intersection
//   routine) is declared and fully defined in the real source but never
//   actually called from anywhere - genuine dead code, confirmed via a
//   direct grep of the real .ino for any call site. Omitted from this
//   port entirely; nothing reachable is lost.
// - `#define FORCE_FLIPPER 5` is likewise defined but never referenced
//   anywhere else in the real source - also genuine dead code, omitted.
// - `#define FROTTEMENT 0.98;` has a real trailing semicolon baked into
//   the macro body itself (a genuine upstream typo). Reproduced exactly
//   as `PINB_FROTTEMENT` (matching this project's own established "match
//   upstream #defines exactly" precedent from the Skibuino macro-parity
//   audit) since it's harmless at both of its real call sites (each one
//   a statement-level `*=` compound assignment - the stray semicolon just
//   becomes one extra, harmless empty statement).
// - `GetNormale()` computes its AC vector from the ball's raw `x`/`y`,
//   while `CollisionDroite()`/`ProjectionI()` both instead use `x+2`/`y+2`
//   - a real asymmetry already present in the original source (not a
//   porting slip), reproduced exactly in `pinbGetNormale()`/
//   `pinbCollisionDroite()`/`pinbProjectionI()`.
// - The real `background` bitmap's own declared width is 88px against
//   this real 84px-wide screen (528 = ceil(88/8)*48 data bytes, confirmed
//   by counting the real array's own element count) - a genuine upstream
//   data mismatch, copied byte-for-byte unmodified; `gbDrawBitmap()`
//   simply clips at the real screen edge with no visible artifact.
//
// A REAL PLATFORM-FORCED FIX (not a preference): `pinbGetNormale()` gained
// an explicit `norme == 0` guard before its own final `nx/norme`,`ny/norme`
// normalization divide. On real AVR hardware, the degenerate case (the
// ball's center sitting exactly collinear with a wall segment's own
// direction vector - rare, but a real, reachable state during normal
// physics) computes a zero-length normal vector and silently divides by
// zero, which real AVR float hardware turns into a harmless Infinity/NaN
// (the resulting bounce velocity is garbage, but the game keeps running).
// Vircon32 instead hard-traps the CPU on any float division by zero
// (VIRCON32_C_DIALECT.md section 17.3) - an unguarded port would crash the
// emulator outright the first time this case is hit, not just misbehave
// visually. Guarded to return a zero vector instead, which
// `pinbCalculerVecteurV2()` then treats as "no bounce this frame" (the
// ball's velocity passes through unchanged) - the closest available
// real-hardware-equivalent outcome, not a new invented behavior.
//
// OTHER DIALECT REWRITES: no ternary operator anywhere in this dialect -
// every `cond ? a : b` (the spring width pick, the velocity clamps, the
// flipper bitmap/position picks) became an explicit if/else. Arduino
// `random(0,20)` became `arand(20)` (this project's own established safe
// RNG helper - see avrCompat.h). `abs()` on a float velocity became
// `fabs()` (math.h) - Vircon32's own `abs()` is int-only. `pow(10,i)`
// became `pow(10.0,(float)i)` with explicit float casts (Vircon32's
// `pow()` is float-only). Every float coordinate passed into an int-typed
// shim parameter (`gbDrawBitmap`/`gbFillRect`/`gbCollideRectRect`/
// `gbDrawLine`) needed an explicit `(int)` cast, matching this project's
// own already-established precedent (e.g. gameFlappyBirdo.c's own
// `(int)flapPlayerY` casts).
//
// SHIM GAPS: none found. Every real primitive this game needs
// (`gbCollideRectRect`, `gbDrawBitmap`/`gbDrawBitmapRotated` with FLIPV,
// `gbDrawLine`, `gbFillRect`, `sqrt`/`pow`/`fabs` from math.h, `arand()`)
// already existed in the shim.
//
// FRAME RATE: real upstream's own `gb.setFrameRate(40)` call sits at the
// very end of `setup()`, AFTER the first blocking `gb.titleScreen()` call
// already returned - so on real hardware, only the very first title
// screen ever runs at `gb.begin()`'s own 20fps default; gameplay, and
// every later title-screen revisit via Button C, all run at the real 40fps
// this game actually wants. This port instead calls `gbSetFrameRate(40)`
// once, up front, applying uniformly to both - a deliberate simplification
// rather than an oversight: the title screen only ever draws one static
// bitmap with no time-based animation of its own, so which frame rate
// polls it is not visibly different either way.
//
// EEPROM: real upstream PinBall has no highscore/EEPROM concept at all -
// confirmed directly (no `#include <EEPROM.h>`, no reference to EEPROM or
// a persisted highscore anywhere in either real source file). `pinbScore`
// is a genuine session-only running total, so no EEPROM persistence was
// added here.

typedef struct{
    float x1, y1, x2, y2;
} PinbLine;

typedef struct{
    float x1, y1, x2, y2;
    int time;
} PinbFlasher;

typedef struct{
    float x, y;
} PinbVec;

typedef struct{
    float x, y, r, vx, vy;
} PinbCircle;

#define PINB_GRAVITE -0.1
#define PINB_FORCE_RESSORT 6
#define PINB_MAX_VITESSE_BALL 5
#define PINB_TIME_FORCE 5
#define PINB_FROTTEMENT 0.98;
#define PINB_TIME_FLASH 30

#define PINB_NB_DROITE 22
#define PINB_NB_BUMPER 19
#define PINB_NB_FLASHER 9

enum PinbState
{
    PINB_STATE_TITLE = 0,
    PINB_STATE_PLAY = 1
};

int pinbState;

int pinbScore = 0;
int pinbLives = 2;
bool pinbIsRessortHaut = true;
bool pinbIsLeftFlipperPressed = false;
bool pinbIsRightFlipperPressed = false;
bool pinbIsDebug = false;
int pinbTimeForce = 0;

PinbCircle pinbBall;
PinbLine pinbFlipR;
PinbLine pinbFlipL;

// Real upstream `droites[]` - the fixed static wall segments making up the
// table's own playfield geometry, byte-for-byte the same coordinates.
PinbLine pinbLines[22] = {
    { 0, 0, 84, 0 },
    { 0, 13, 11, 0 },
    { 8, 11, 15, 4 },
    { 16, 4, 27, 4 },
    { 16, 11, 19, 9 },
    { 20, 9, 30, 9 },
    { 50, 5, 70, 9 },
    { 71, 9, 71, 16 },
    { 65, -1, 77, 10 },
    { 75, 8, 75, 40 },
    { 56, 41, 49, 36 },
    { 67, 39, 67, 31 },
    { 67, 39, 10, 42 },
    { 10, 42, 10, 48 },
    { 10, 48, 66, 48 },
    { 66, 48, 75, 40 },
    { 0, 29, 14, 45 },
    { 8, 31, 16, 38 },
    { 16, 38, 27, 38 },
    { 15, 30, 20, 33 },
    { 20, 33, 30, 33 },
    { 0, 46, 84, 46 }
};

// Real upstream `bumper[]` - 5 real bumper targets, 4 segments each
// (indices 0-3/4-7/8-11 score 10pts, 12-15/16-18 score 30pts, everything
// else in between scores 20pts - matches `updateBall()`'s own real
// `if(i<4 || (i>7 && i<12)) ... else if(i>11) ... else ...` scoring split).
PinbLine pinbBumper[19] = {
    { 43, 4, 44, 5 },
    { 44, 5, 43, 7 },
    { 43, 7, 40, 5 },
    { 40, 5, 43, 4 },
    { 43, 16, 44, 18 },
    { 44, 18, 43, 20 },
    { 43, 20, 40, 18 },
    { 40, 18, 43, 16 },
    { 43, 29, 44, 31 },
    { 44, 31, 43, 33 },
    { 43, 33, 40, 31 },
    { 40, 31, 43, 29 },
    { 60, 15, 63, 18 },
    { 63, 18, 60, 21 },
    { 60, 21, 53, 18 },
    { 53, 18, 60, 15 },
    { 63, 31, 63, 35 },
    { 63, 35, 55, 34 },
    { 53, 34, 63, 31 }
};

// Real upstream `flasher[]` - decorative wall strips that flash on/off for
// PINB_TIME_FLASH ticks and add a small score whenever the ball touches
// one (the `time` field doubling as both "still flashing" state and hit
// cooldown, exactly like real upstream).
PinbFlasher pinbFlasher[9] = {
    { 22, 12, 29, 15, 30 },
    { 22, 16, 29, 19, 0 },
    { 22, 20, 29, 23, 0 },
    { 22, 24, 29, 27, 0 },
    { 22, 28, 29, 31, 0 },
    { 14, 14, 21, 17, 0 },
    { 14, 18, 21, 21, 0 },
    { 14, 22, 21, 25, 0 },
    { 14, 26, 21, 29, 30 }
};

// -----------------------------------------------------------------------
// Real upstream bitmaps - `int name[N] = { width, height, byte0, ... }`,
// every byte copied verbatim from the real PROGMEM tables (converted from
// this dialect's own array declaration order - see this file's own header
// comment).
// -----------------------------------------------------------------------

int pinbCpt0Bitmap[5] = { 8, 3, 0xF8, 0x88, 0xF8 };
int pinbCpt1Bitmap[5] = { 8, 3, 0x90, 0xF8, 0x80 };
int pinbCpt2Bitmap[5] = { 8, 3, 0xE8, 0xA8, 0xB8 };
int pinbCpt3Bitmap[5] = { 8, 3, 0xA8, 0xA8, 0xF8 };
int pinbCpt4Bitmap[5] = { 8, 3, 0x38, 0x20, 0xF8 };
int pinbCpt5Bitmap[5] = { 8, 3, 0xB8, 0xA8, 0xE8 };
int pinbCpt6Bitmap[5] = { 8, 3, 0xF8, 0xA8, 0xE8 };
int pinbCpt7Bitmap[5] = { 8, 3, 0x8, 0x8, 0xF8 };
int pinbCpt8Bitmap[5] = { 8, 3, 0xF8, 0xA8, 0xF8 };
int pinbCpt9Bitmap[5] = { 8, 3, 0x38, 0x28, 0xF8 };

int* pinbNumberBitmaps[10] = {
    pinbCpt0Bitmap, pinbCpt1Bitmap, pinbCpt2Bitmap, pinbCpt3Bitmap, pinbCpt4Bitmap,
    pinbCpt5Bitmap, pinbCpt6Bitmap, pinbCpt7Bitmap, pinbCpt8Bitmap, pinbCpt9Bitmap
};

int pinbBackgroundBitmap[530] = {
    88, 48, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x0, 0x0, 0x8, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x7F, 0xFE, 0xDA, 0x40, 0xFF, 0xD0, 0x0, 0x0, 0x0, 0x70, 0x0, 0x0,
    0xFE, 0xA, 0xA0, 0xFF, 0xA0, 0x0, 0x0, 0x0, 0xF8, 0x0, 0x0, 0xE, 0xF9, 0x20, 0xFF, 0x41,
    0xFF, 0xF0, 0x1, 0x6C, 0x0, 0x0, 0x1, 0xF8, 0x0, 0xFE, 0x82, 0x0, 0x0, 0x1, 0x4, 0x3F,
    0x80, 0x0, 0x79, 0x0, 0xFD, 0x4, 0x0, 0x0, 0x1, 0x7C, 0xF, 0xFF, 0x0, 0x3A, 0x80, 0xFA,
    0x8, 0x0, 0x0, 0x0, 0xF8, 0x0, 0x7F, 0xF0, 0x3A, 0x80, 0xF4, 0x10, 0xFF, 0xFC, 0x0, 0x70,
    0x0, 0x0, 0xFC, 0x18, 0x0, 0xE8, 0x21, 0xFF, 0xFE, 0x0, 0x0, 0x0, 0x0, 0xE, 0x19, 0x0,
    0xD0, 0x1, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x6, 0x1A, 0x80, 0xA0, 0x1, 0xC0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x6, 0x19, 0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x8,
    0x0, 0x80, 0x0, 0x1, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x3, 0xB, 0x80, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x3, 0x8, 0x80, 0x0, 0x1, 0xF0, 0x0, 0x0, 0x70, 0x0, 0x1C, 0x1,
    0x8, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF8, 0x0, 0x3E, 0x1, 0x8, 0x0, 0x0, 0x0, 0x1,
    0xF0, 0x1, 0x14, 0x0, 0x55, 0x0, 0xB, 0x80, 0x0, 0x0, 0x0, 0x0, 0x1, 0x54, 0x0, 0x55,
    0x0, 0xA, 0x80, 0x0, 0x1, 0xF0, 0x0, 0x1, 0x44, 0x0, 0x41, 0x0, 0xA, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0xF8, 0x0, 0x3E, 0x0, 0x8, 0x0, 0x0, 0x0, 0x1, 0xF0, 0x0, 0x70, 0x0,
    0x1C, 0x0, 0xA, 0x20, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0,
    0x1, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x0, 0x1, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x1, 0xF0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x70, 0x0, 0x0, 0x0, 0x8,
    0x0, 0x80, 0x0, 0x1, 0xF0, 0x0, 0xF8, 0x0, 0x0, 0x0, 0x8, 0x0, 0x40, 0x0, 0x0, 0x0,
    0x1, 0x6C, 0x0, 0x0, 0x0, 0x8, 0x0, 0xA0, 0x1, 0xC0, 0x0, 0x1, 0x4, 0x0, 0x3E, 0x18,
    0x8, 0x0, 0xD0, 0x1, 0xF0, 0x0, 0x1, 0x7C, 0x1, 0xD5, 0xC, 0x8, 0x0, 0xE8, 0x21, 0xFF,
    0xFE, 0x0, 0xF8, 0xF, 0xD5, 0xC, 0x8, 0x0, 0xF4, 0x10, 0xFF, 0xFC, 0x0, 0x70, 0x7, 0xC1,
    0xE, 0x8, 0x0, 0xFA, 0x8, 0x0, 0x0, 0x0, 0x0, 0x3, 0xFF, 0xE, 0x8, 0x0, 0xFD, 0x4,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xE, 0x18, 0x0, 0xFE, 0x82, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x0, 0xE, 0x18, 0x0, 0xFF, 0x41, 0xFF, 0xF0, 0x0, 0x0, 0x30, 0x0, 0x1C, 0x18, 0x0, 0xFF,
    0xA0, 0x0, 0x0, 0x0, 0x0, 0x3C, 0x0, 0xF8, 0x18, 0x0, 0xFF, 0xD0, 0x0, 0x0, 0x0, 0x0,
    0x3F, 0xFF, 0xF0, 0x38, 0x0, 0x0, 0x8, 0x0, 0x0, 0x0, 0x0, 0x3F, 0xFF, 0x0, 0x38, 0x0,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x0, 0x78, 0x0, 0xFF, 0xF8, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0xF8, 0x0, 0xFF, 0xF8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF, 0xF8,
    0x0, 0xFF, 0xF8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xF8, 0x0, 0xFF, 0xF8, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x7F, 0xFF, 0xF8, 0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xF8, 0x0
};

int pinbBallBitmap[6] = { 8, 4, 0x60, 0xF0, 0xD0, 0x60 };

int pinbFlipperBasBitmap[11] = { 8, 9, 0x60, 0x60, 0xE0, 0xE0, 0xE0, 0xC0, 0xC0, 0x80, 0x80 };
int pinbFlipperHautBitmap[9] = { 8, 7, 0x40, 0xF0, 0x38, 0x38, 0x1E, 0xE, 0x3 };

int pinbRessortBasBitmap[6] = { 8, 4, 0x8, 0x58, 0xA8, 0x8 };
int pinbRessortHautBitmap[10] = { 16, 4, 0x0, 0x40, 0xAA, 0xC0, 0x55, 0x40, 0x0, 0x40 };

int pinbTitleBitmap[242] = {
    64, 30, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x7, 0x87, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xE, 0x47, 0x37, 0xE0,
    0x0, 0x0, 0x0, 0x0, 0x1F, 0x67, 0x37, 0xEE, 0x60, 0x0, 0x0, 0x0, 0x1F, 0xE7, 0x37, 0xEE,
    0x6F, 0xFE, 0x1, 0xF8, 0x1F, 0xE7, 0xE1, 0x8F, 0x6F, 0xFE, 0x6, 0x6, 0x1F, 0xE7, 0x1, 0x8F,
    0x60, 0x0, 0x9, 0xF9, 0xF, 0xC7, 0x1, 0x8F, 0x60, 0x0, 0x17, 0xFE, 0x87, 0x87, 0x7, 0xEE,
    0xE0, 0x0, 0x16, 0xE, 0x80, 0x7, 0x7, 0xEE, 0xE0, 0x0, 0x2E, 0x7, 0x40, 0x0, 0x7, 0xEE,
    0x60, 0x0, 0x2F, 0xE7, 0x40, 0x7, 0xE0, 0xE, 0x60, 0x0, 0x2E, 0x7, 0x40, 0x7, 0x33, 0xC0,
    0x7, 0x0, 0x2E, 0x7, 0x40, 0x7, 0x37, 0x6E, 0x7, 0x0, 0x2F, 0xE7, 0x40, 0x7, 0x37, 0x6E,
    0x7, 0x0, 0x2E, 0x7, 0x40, 0x7, 0xE7, 0xEE, 0x7, 0x0, 0x1E, 0xE, 0x80, 0x7, 0x37, 0x6E,
    0x7, 0x0, 0x1F, 0xFE, 0x80, 0x7, 0x37, 0x6E, 0x7, 0x0, 0xF, 0xFD, 0x0, 0x7, 0x37, 0x6E,
    0x7, 0x0, 0x7, 0xFE, 0x0, 0x7, 0xE7, 0x6E, 0x7, 0x0, 0x1, 0xF8, 0x0, 0x0, 0x7, 0x6E,
    0x7, 0xF8, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x80, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0xF8, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x7E, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xC0, 0x0, 0x0,
    0x0, 0x0
};

// -----------------------------------------------------------------------
// Real segment/circle collision math - direct ports of upstream's own
// CollisionDroite()/CollisionSegment()/ProjectionI()/GetNormale()/
// CalculerVecteurV2()/rebond(), rewritten to pointer parameters/out-
// parameters (see this file's own header comment for why).
// -----------------------------------------------------------------------

bool pinbCollisionDroite( PinbLine* d1 )
{
    float ux = d1->x2 - d1->x1;
    float uy = d1->y2 - d1->y1;
    float acx = ( pinbBall.x + 2 ) - d1->x1;
    float acy = ( pinbBall.y + 2 ) - d1->y1;
    float numerateur = ux * acy - uy * acx;
    if( numerateur < 0 )
      numerateur = -numerateur;
    float denominateur = sqrt( ux * ux + uy * uy );
    float ci = numerateur / denominateur;
    if( ci < 2 )
      return true;
    return false;
}

bool pinbCollisionSegment( PinbLine* d1 )
{
    if( pinbCollisionDroite( d1 ) == false )
      return false;

    PinbVec ab;
    ab.x = d1->x2 - d1->x1;
    ab.y = d1->y2 - d1->y1;
    PinbVec ac;
    ac.x = ( pinbBall.x + 2 ) - d1->x1;
    ac.y = ( pinbBall.y + 2 ) - d1->y1;
    PinbVec bc;
    bc.x = ( pinbBall.x + 2 ) - d1->x2;
    bc.y = ( pinbBall.y + 2 ) - d1->y2;

    float pscal1 = ab.x * ac.x + ab.y * ac.y;
    float pscal2 = ( -ab.x ) * bc.x + ( -ab.y ) * bc.y;
    if( pscal1 >= 0 && pscal2 >= 0 )
      return true;

    // Real upstream `CollisionBallExtremiter()` is a genuine dead stub -
    // always returns false (see this file's own header comment) - inlined
    // directly rather than ported as its own separate always-false
    // function.
    return false;
}

void pinbProjectionI( PinbLine* d1, PinbVec* out )
{
    float ux = d1->x2 - d1->x1;
    float uy = d1->y2 - d1->y1;
    float acx = ( pinbBall.x + 2 ) - d1->x1;
    float acy = ( pinbBall.y + 2 ) - d1->y1;
    float ti = ( ux * acx + uy * acy ) / ( ux * ux + uy * uy );
    out->x = d1->x1 + ti * ux;
    out->y = d1->y1 + ti * uy;
}

void pinbGetNormale( PinbLine* d1, PinbVec* out )
{
    float ux = d1->x2 - d1->x1;
    float uy = d1->y2 - d1->y1;
    float acx = pinbBall.x - d1->x1; // real upstream: no +2 offset here (see this file's own header comment)
    float acy = pinbBall.y - d1->y1;
    float parenthesis = ux * acy - uy * acx;
    float nx = -uy * parenthesis;
    float ny = ux * parenthesis;
    float norme = sqrt( nx * nx + ny * ny );

    // Platform-forced guard - see this file's own header comment.
    if( norme == 0 )
    {
        out->x = 0;
        out->y = 0;
        return;
    }

    out->x = nx / norme;
    out->y = ny / norme;
}

void pinbCalculerVecteurV2( PinbVec* n, PinbVec* out )
{
    float pscal = pinbBall.vx * n->x + pinbBall.vy * n->y;
    out->x = pinbBall.vx - 2 * pscal * n->x;
    out->y = pinbBall.vy - 2 * pscal * n->y;
}

void pinbRebond( PinbLine* obst )
{
    PinbVec n;
    pinbGetNormale( obst, &n );
    PinbVec impact;
    pinbProjectionI( obst, &impact );
    pinbBall.x = impact.x - 2;
    pinbBall.y = impact.y - 2;
    PinbVec reb;
    pinbCalculerVecteurV2( &n, &reb );
    pinbBall.vx = reb.x;
    pinbBall.vy = reb.y;
    pinbBall.vx = pinbBall.vx * PINB_FROTTEMENT;
    pinbBall.vy = pinbBall.vy * PINB_FROTTEMENT;
}

// -----------------------------------------------------------------------
// Game logic - direct ports of upstream's own initGame()/updateWorld()/
// updateBall()/drawBall()/drawWorld().
// -----------------------------------------------------------------------

void pinbInitGame()
{
    // real upstream also sets `gb.battery.show = false;` here - purely
    // cosmetic on real hardware, dropped outright (same established
    // precedent as gamePong.c's own port).
    pinbLives = 2;
    pinbBall.x = 40;
    pinbBall.y = 43;
    // real upstream does NOT reset Ball.vx/vy here - a deliberate quirk,
    // see this file's own header comment.

    pinbFlipR.x1 = 8;
    pinbFlipR.y1 = 32;
    pinbFlipR.x2 = 6;
    pinbFlipR.y2 = 23;

    pinbFlipL.x1 = 8;
    pinbFlipL.y1 = 10;
    pinbFlipL.x2 = 6;
    pinbFlipL.y2 = 19;
}

void pinbUpdateWorld()
{
    if( pinbTimeForce > 0 )
    {
        pinbTimeForce = pinbTimeForce - 1;
        if( pinbIsLeftFlipperPressed )
        {
            pinbFlipL.x2 = 16 - pinbTimeForce;
            pinbFlipL.y2 = 20 - ( PINB_TIME_FORCE - pinbTimeForce );
        }
        if( pinbIsRightFlipperPressed )
        {
            pinbFlipR.x2 = 16 - pinbTimeForce;
            pinbFlipR.y2 = 26 - ( pinbTimeForce / 2 );
        }
    }

    pinbIsRessortHaut = true;
    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        pinbIsRessortHaut = false;
        pinbTimeForce = PINB_TIME_FORCE;
    }

    if( gbPressed( BTN_UP ) )
    {
        pinbIsLeftFlipperPressed = true;
        pinbTimeForce = PINB_TIME_FORCE;
    }
    else if( gbReleased( BTN_UP ) )
    {
        pinbFlipL.x2 = 6;
        pinbFlipL.y2 = 19;
        pinbIsLeftFlipperPressed = false;
    }

    if( gbPressed( BTN_DOWN ) )
    {
        pinbIsRightFlipperPressed = true;
        pinbTimeForce = PINB_TIME_FORCE;
    }
    else if( gbReleased( BTN_DOWN ) )
    {
        pinbFlipR.x2 = 6;
        pinbFlipR.y2 = 23;
        pinbIsRightFlipperPressed = false;
    }
}

void pinbUpdateBall()
{
    if( gbPressed( BTN_A ) )
    {
        // "shake the pinball" - real upstream's own crude nudge mechanic
        pinbBall.vx = pinbBall.vx + ( ( (float)arand( 20 ) - 10.0 ) / 10.0 );
        pinbBall.vy = pinbBall.vy + ( ( (float)arand( 20 ) - 10.0 ) / 10.0 );
    }
    else
    {
        pinbBall.vx = pinbBall.vx + PINB_GRAVITE;
    }

    if( fabs( pinbBall.vx ) > PINB_MAX_VITESSE_BALL )
    {
        if( pinbBall.vx > 0 ) pinbBall.vx = PINB_MAX_VITESSE_BALL;
        else pinbBall.vx = -PINB_MAX_VITESSE_BALL;
    }
    if( fabs( pinbBall.vy ) > PINB_MAX_VITESSE_BALL )
    {
        if( pinbBall.vy > 0 ) pinbBall.vy = PINB_MAX_VITESSE_BALL;
        else pinbBall.vy = -PINB_MAX_VITESSE_BALL;
    }

    int springW;
    if( pinbIsRessortHaut ) springW = 10;
    else springW = 5;

    if( gbCollideRectRect( 13, 43, springW, 4, (int)pinbBall.x, (int)pinbBall.y, 2, 2 ) )
    {
        if( pinbIsRessortHaut )
        {
            if( pinbTimeForce > 0 )
            {
                float percent = 1.0 - ( ( pinbBall.x - 18.0 ) / 5.0 );
                float force = PINB_FORCE_RESSORT * percent;
                pinbBall.vx = pinbBall.vx + force;
                pinbBall.x = 23;
            }
        }
        else
        {
            pinbBall.x = 18;
        }

        if( pinbBall.vx < 0 )
          pinbBall.vx = -pinbBall.vx * 0.5;
    }

    pinbBall.vx = pinbBall.vx * 0.98; // friction

    pinbBall.x = pinbBall.x + pinbBall.vx;
    pinbBall.y = pinbBall.y + pinbBall.vy;

    if( pinbCollisionSegment( &pinbFlipR ) )
    {
        pinbRebond( &pinbFlipR );
        if( pinbIsRightFlipperPressed && pinbTimeForce > 0 )
        {
            if( pinbBall.vx < 1 )
              pinbBall.vx = 1;
            pinbBall.vx = pinbBall.vx * 1.5;
            pinbBall.vy = pinbBall.vy * 1.5;
        }
    }
    else if( pinbCollisionSegment( &pinbFlipL ) )
    {
        pinbRebond( &pinbFlipL );
        if( pinbIsLeftFlipperPressed && pinbTimeForce > 0 )
        {
            if( pinbBall.vx < 1 )
              pinbBall.vx = 1;
            pinbBall.vx = pinbBall.vx * 1.5;
            pinbBall.vy = pinbBall.vy * 1.5;
        }
    }

    int i;
    for( i = 0; i < PINB_NB_DROITE; i = i + 1 )
    {
        if( pinbCollisionSegment( &pinbLines[i] ) )
        {
            pinbRebond( &pinbLines[i] );
            break;
        }
    }

    for( i = 0; i < PINB_NB_BUMPER; i = i + 1 )
    {
        if( pinbCollisionSegment( &pinbBumper[i] ) )
        {
            pinbRebond( &pinbBumper[i] );
            pinbBall.x = pinbBall.x * 1.05;
            pinbBall.y = pinbBall.y * 1.05;

            if( i < 4 || ( i > 7 && i < 12 ) )
              pinbScore = pinbScore + 10;
            else if( i > 11 )
              pinbScore = pinbScore + 30;
            else
              pinbScore = pinbScore + 20;
            break;
        }
    }

    for( i = 0; i < PINB_NB_FLASHER; i = i + 1 )
    {
        if( pinbFlasher[i].time > 0 )
          pinbFlasher[i].time = pinbFlasher[i].time - 1;

        PinbLine dF;
        dF.x1 = pinbFlasher[i].x1;
        dF.y1 = pinbFlasher[i].y1;
        dF.x2 = pinbFlasher[i].x2;
        dF.y2 = pinbFlasher[i].y2;
        if( pinbCollisionSegment( &dF ) )
        {
            pinbFlasher[i].time = PINB_TIME_FLASH;
            pinbScore = pinbScore + 1;
            break;
        }
    }

    if( pinbScore > 100000 )
      pinbScore = 999999;

    // prevent the ball from going out of the screen
    if( pinbBall.x > LCDWIDTH )
      pinbBall.x = LCDWIDTH - 2;

    if( pinbBall.y < 0 )
      pinbBall.y = 2;
    if( pinbBall.y > LCDHEIGHT )
      pinbBall.y = LCDHEIGHT - 2;

    if( pinbBall.x < -5 )
    {
        pinbBall.vx = 0;
        pinbBall.vy = 0;
        pinbBall.x = 40;
        pinbBall.y = 43;
        pinbLives = pinbLives - 1;

        if( pinbLives < 0 )
          pinbLives = 2;
    }
}

void pinbDrawBall()
{
    gbDrawBitmap( (int)pinbBall.x, (int)pinbBall.y, pinbBallBitmap );
}

void pinbDrawWorld()
{
    if( pinbIsDebug )
    {
        int i;
        for( i = 0; i < PINB_NB_BUMPER; i = i + 1 )
          gbDrawLine( (int)pinbBumper[i].x1, (int)pinbBumper[i].y1, (int)pinbBumper[i].x2, (int)pinbBumper[i].y2 );

        for( i = 0; i < PINB_NB_DROITE; i = i + 1 )
          gbDrawLine( (int)pinbLines[i].x1, (int)pinbLines[i].y1, (int)pinbLines[i].x2, (int)pinbLines[i].y2 );

        gbDrawLine( (int)pinbFlipR.x1, (int)pinbFlipR.y1, (int)pinbFlipR.x2, (int)pinbFlipR.y2 );
        gbDrawLine( (int)pinbFlipL.x1, (int)pinbFlipL.y1, (int)pinbFlipL.x2, (int)pinbFlipL.y2 );
    }
    else
    {
        gbDrawBitmap( 0, 0, pinbBackgroundBitmap );

        int rightFlipY;
        if( pinbIsRightFlipperPressed ) rightFlipY = 25;
        else rightFlipY = 23;
        int* rightFlipBitmap;
        if( pinbIsRightFlipperPressed ) rightFlipBitmap = pinbFlipperHautBitmap;
        else rightFlipBitmap = pinbFlipperBasBitmap;
        gbDrawBitmapRotated( 8, rightFlipY, rightFlipBitmap, 0, 2 ); // NOROT, FLIPV

        int* leftFlipBitmap;
        if( pinbIsLeftFlipperPressed ) leftFlipBitmap = pinbFlipperHautBitmap;
        else leftFlipBitmap = pinbFlipperBasBitmap;
        gbDrawBitmap( 8, 10, leftFlipBitmap );
    }

    int* springBitmap;
    if( pinbIsRessortHaut ) springBitmap = pinbRessortHautBitmap;
    else springBitmap = pinbRessortBasBitmap;
    gbDrawBitmap( 13, 43, springBitmap );

    gbSetColor( GB_INVERT );
    int i;
    for( i = 0; i < pinbLives; i = i + 1 )
      gbDrawBitmap( 2 + ( i * 5 ), 43, pinbBallBitmap );

    for( i = 0; i < PINB_NB_FLASHER; i = i + 1 )
    {
        if( pinbFlasher[i].time > 0 && ( gbFrameCount % 4 > 1 ) )
          gbFillRect( (int)pinbFlasher[i].x1, (int)pinbFlasher[i].y1,
                      (int)( pinbFlasher[i].x2 - pinbFlasher[i].x1 ),
                      (int)( pinbFlasher[i].y2 - pinbFlasher[i].y1 ) );
    }

    gbSetColor( GB_BLACK );

    // score digits - real upstream draws 6 digits right-to-left (ones
    // digit at the bottom, x=78), spaced 4px apart vertically.
    for( i = 0; i < 6; i = i + 1 )
    {
        int divisor = (int)pow( 10.0, (float)i );
        int digit = ( pinbScore / divisor ) % 10;
        gbDrawBitmap( 78, 43 - ( 4 * i ), pinbNumberBitmaps[digit] );
    }
}

// -----------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------

void pinbBeginTitle()
{
    pinbState = PINB_STATE_TITLE;
}

void pinbBeginPlay()
{
    pinbInitGame();
    pinbState = PINB_STATE_PLAY;
}

void pinbUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 0, 12, pinbTitleBitmap );
    gbCursorX = 21;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      pinbBeginPlay();
}

void pinbUpdatePlay()
{
    // pause back to the title screen if C is pressed - real upstream's
    // own `goTitleScreen()` (a blocking call) consumes the rest of this
    // real tick, matching gamePong.c's own identical treatment.
    if( gbPressed( BTN_C ) )
    {
        pinbBeginTitle();
        return;
    }

    if( gbPressed( BTN_B ) )
      pinbIsDebug = !pinbIsDebug;

    pinbUpdateWorld();
    pinbUpdateBall();
    pinbDrawBall();
    pinbDrawWorld();
}

void gamePinball_init()
{
    gbBegin();
    gbSetFrameRate( 40 ); // see this file's own header comment on real upstream's own frame-rate timing
    pinbBeginTitle();
}

void gamePinball_update()
{
    if( !gbUpdate() ) return;

    if( pinbState == PINB_STATE_TITLE ) pinbUpdateTitle();
    else pinbUpdatePlay();

    gbRenderFrame();
}
