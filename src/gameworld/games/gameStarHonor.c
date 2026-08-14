#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// StarHonor - a roguelike space adventure. Original Arduboy game "Star
// Honor" by Wenceslao Villanueva Jr (2016, MIT license - see the real
// upstream LICENSE file, a single-holder MIT grant naming him directly).
// The source ported here is wuuff's own unofficial Gamebuino Classic port
// of that original game (README.md: "Unofficial Gamebuino port by wuuff"),
// which already targets the real Gamebuino Classic library directly
// (`Gamebuino arduboy;` - confirmed by reading Globals.cpp) rather than
// real Arduboy2 hardware.
//
// A REAL STRUCTURAL WRINKLE this port had to check first: the repo also
// ships `ArduboyCustom.cpp/h` and `coreCustom.cpp/h`, suggesting a second
// compatibility shim layer. Read in full before writing anything here -
// both turned out to be genuinely empty (`coreCustom.h` is just an
// `#ifndef ArduboyCoreCustom_h` include guard with nothing inside it,
// `ArduboyCustom.h` is just `#include "coreCustom.h"` under an
// `Arduboy_h` guard, `.cpp` files are empty besides their own #include).
// Their only real job is making a stray `#include <Arduboy.h>`-style line
// elsewhere resolve to nothing, in case one exists (it doesn't, in this
// source tree). `Globals.cpp` proves the real wiring directly: `Gamebuino
// arduboy;` - a genuine, real Gamebuino Classic instance, with every
// `arduboy.display.x()`/`arduboy.buttons.x()` call site already calling
// straight into the real library. So there was no second translation
// layer to route around or port - every `arduboy.x.y(...)` call site
// below was mechanically rewritten straight to this project's own
// `gbY(...)` shim primitive, exactly like every other ported game.
//
// Every real C++ class was flattened into a plain struct plus free
// functions taking an explicit pointer (this dialect has no classes/
// methods/operator overloading):
// - `Vector2d` (real x/y float pair with `+`/`-`/`*`/`+=`/`-=`/`*=`
//   operator overloads, `Magnitude()`/`MagnitudeSquared()`/`Normalize()`/
//   `Rotate()`) -> `StarVec2` struct + `starVecAdd`/`starVecSub`/
//   `starVecScale`/`starVecAddInPlace`/`starVecMag`/`starVecMagSq`/
//   `starVecNormalize`/`starVecRotate`, all taking explicit in/out
//   pointers (a `Vector2d` return by value is 2 words - over this
//   dialect's real 1-word function-return limit). Real `Dot()` and the
//   component-wise `Vector2d*Vector2d` operator are both genuinely dead
//   code (declared, never called anywhere in the real gameplay source -
//   confirmed via a full grep) and were not ported, matching this
//   project's own established "don't port genuinely unused methods"
//   precedent.
// - `Ship` -> `StarShip` struct + `starShip*()` functions taking an
//   explicit `StarShip*`. There is only ever one real instance
//   (`PlayerShip`), kept as a single global `starPlayerShip` (not
//   malloc'd, matching this project's own "flatten a single-instance
//   class into a bare global" convention).
// - `Planetoid` -> `StarPlanet` struct, held as a real `StarPlanet[10]
//   starPlanets` array-of-struct global (the same proven pattern as e.g.
//   gameDescent.c's own `DescentMonster[8] descentMonster`) instead of
//   the real upstream's own array-of-`new`'d-pointers - `Planetoid**
//   Planets`/`InitializePlanetsArray()`'s own allocation step is
//   unneeded here since every one of a fresh array's 10 real elements'
//   fields is always fully written by `starNewMap()` before any of them
//   is ever read.
// - `SelectionArrow`/`SelectionLocation` (a real circular doubly-linked
//   list of positions) -> flattened much further than a 1:1 struct port,
//   since only two real instances ever exist and `CurrentSelectionArrow`
//   (used throughout the real status screen) is assigned exactly once,
//   at boot, to `RepairSelectionArrow`, and never reassigned anywhere
//   else in the real source (confirmed via a full grep) - so the real
//   "current selection arrow" indirection is dead abstraction. Ported as
//   two small parallel coordinate-table + position-index pairs
//   (`starOverviewX/Y`+`starRepairArrowPos`,
//   `starCombatX/Y`+`starCombatArrowPos`) instead of a linked list.
// - `Text` (a real singleton instance, `TextManager`) -> flattened into
//   plain globals (`starTextOverTimeRunning`/`starFramesToNextChar`/
//   `starCharIndex`) and free functions, the same single-instance
//   treatment as `Ship`. Real `DisplayText()`'s own per-character
//   `arduboy.display.drawChar()` loop (with real per-font spacing
//   baked in as `tWidth=3,tHeight=5,tPadding=1`) is not reimplemented at
//   all - this shim's own `gbPrintString()` already reproduces that exact
//   spacing (its default font, `gbFont3x5`, IS `{3,5}` with the same
//   real "+1" inter-char/line spacing convention), so
//   `starDisplayTextRaw()` is just `gbCursorX=x;gbCursorY=y;
//   gbPrintString(text);` - the real per-character loop upstream needed
//   to hand-roll is exactly what this shim's own primitive already does.
//   Real `DisplayTextClear()`'s own "draw small, discover it's
//   multi-line, redraw bigger" double-draw shape (not just its final
//   visual result) is preserved exactly in `starDisplayTextClear()`,
//   including the same real first-pass small-clear-box draw before the
//   line count is even known.
//
// `bitmaps.h` already uses this project's own real Gamebuino `{width,
// height, byte0, byte1, ...}` bitmap format directly (not a raw,
// dimension-less Arduboy/Adafruit-GFX-style array) - this is a genuine
// Gamebuino port, not a mechanical Arduboy-bitmap carryover, so no
// width/height-prepending was needed. Every real `B00011110`-style
// Arduino binary literal (a syntax this dialect has no equivalent for)
// was mechanically converted to decimal via a small offline Python
// script, verified byte-count-exact against each bitmap's own declared
// `ceil(width/8)*height` size before use. `PlanetHome_16_64` is a real,
// confirmed upstream quirk worth noting: its own declared height is 48
// (not 64, despite the array's own name) - the real source has the first
// and last 8 rows of a would-be 64-row bitmap commented out, leaving
// exactly 48 real active rows, matching the declared header exactly; the
// misleading `_64` in the name is preserved only in spirit (kept as
// `starPlanetHomeBmp`, no `_64`/`_48` suffix at all, sidestepping the
// question of which number to preserve).
//
// No custom per-pixel bitmap-masking helper of any kind was needed here
// (checked explicitly per this batch's own standing instruction to watch
// for the "Pirates" per-pixel-call-storm mistake) - every sprite/planet/
// ship/UI bitmap draws via a single opaque `gbDrawBitmap()` call each,
// and `StarField`'s own 20 individual stars each draw via exactly one
// `gbDrawPixel()` call per star per tick (the genuinely fine, intended
// use of that primitive for a field of independent points - not a
// masked-sprite blit loop).
//
// No collision/hit-detection in this game depends on the ship's own
// facing/rotation at all (checked explicitly per this batch's own
// standing instruction, after the sibling "Pirates" project's own
// facing-dependent hitbox bug) - the ship's rotation only ever affects
// (a) which of 8 pre-rotated bitmap sprites is drawn
// (`starPlayerBitmaps[shipFacing]`) and (b) the direction of the real
// thrust vector for flight physics (`starShipCalcThrust()`'s own
// `starVecRotate()` call). Planet "hailing" detection - the only real
// collision-like check in the whole game - is pure distance
// (`starVecMagSq(&distance) < 256`, a fixed-radius circle test),
// verified directly by reading `DrawMap()`/`starDrawMap()`: it never
// reads `ShipRotation`/`shipFacing` at all.
//
// EEPROM: despite this project's own porting-priority audit flagging
// this game as a "real EEPROM save" candidate, reading every real source
// file found no actual EEPROM read/write call anywhere at all - the only
// real hit, across all 22 files, is `StarHonor.ino`'s own `#include
// <EEPROM.h>` line, which is never followed by a single real
// `EEPROM.read()`/`.write()` call anywhere in the whole game (confirmed
// via a full case-insensitive grep across every `.cpp`/`.h`/`.ino` file).
// This game has no real highscore/save concept at all - it's a single
// continuous playthrough (win by reaching Sector 7, or lose to hull
// damage or the countdown clock), with a plain in-place `Reset` state
// starting a fresh run, not a persisted score. This is the exact same
// "confirmed zero real EEPROM calls, despite looking like a candidate"
// situation this project already hit once before with `gameShipwrek.c`
// (see that file's own header comment, and this project's own
// `CLAUDE.md` "Twenty more games ported (batch 3)" section) - no
// `eeprom_*()` call was added here either, matching that precedent
// rather than inventing a new save feature past what real upstream ever
// shipped.
//
// Real upstream quirks/bugs found and deliberately preserved (none of
// these crash or make the game unplayable, so none were "fixed"):
// - `Neutral_Response_A/B/C/D` real 4-entry array. `SetupEncounter()`
//   preserved.
// - The real "shields holding" text-buffer index-advance bug in
//   `EncouterUpdate()`'s own case 4: after printing
//   `CombatShipShieldDamage` as text, the real code computes how many
//   *extra* digit-positions to advance the buffer index by using
//   `CombatShipDamage` (a different, usually-stale global from a
//   previous combat round - or 0, on a fresh game) instead of
//   `CombatShipShieldDamage` (the value actually just printed) - a real
//   copy-paste variable mix-up. This can misalign the following literal
//   text in `typeBuffer`, a cosmetic display glitch only (the buffer is
//   still always properly null-terminated at whatever index results, no
//   overflow past `typeBuffer`'s own real 128-cell size in any reachable
//   case). Preserved exactly - `starDigitExtra(starCombatShipDamage)` is
//   called at that exact spot, not `starCombatShipShieldDamage`.
// - `Ship::RepairSystem()`'s real "Engines and Shields swapped" ordering
//   in `RepairedText[]` (upstream's own header comment above that array
//   admits this directly) - `starRepairedText[]` keeps the identical
//   order.
//
// One real change from upstream, made specifically to avoid a genuine
// crash risk on this platform (not a cosmetic preference): the real
// `EncouterUpdate()` damage-report `Text::CopyIntoBuffer(DamageReportXxx,
// index, N)` calls all pass an `N` LARGER than each string's own real
// length (verified precisely with an offline script against every real
// `CopyIntoBuffer` call site in the source: `DamageReportCrew` is a real
// 19-character string but copied with count 27; `DamageReportHull` is 12
// but copied with 21; `DamageReportWeapons` is 12 but copied with 29;
// `DamageReportShields` is 18 but copied with 22; `DamageReportEngines`
// is 20 but copied with 24 - every other real `CopyIntoBuffer` call in
// the whole game checked out exact). On real AVR PROGMEM this just reads
// a few genuinely harmless bytes of whatever constant happens to sit
// next in flash. On this platform the same over-read could pull an
// arbitrary adjacent global's raw bit pattern (not necessarily a small
// printable character code) into `typeBuffer`, which later gets handed
// straight to `gbDrawChar()`/`gbPrintString()` as if it were real
// character data - a sufficiently large/negative "character code" read
// this way risks an out-of-bounds font-table index, a real potential
// hard crash, not merely a cosmetic one. Fixed by using each string's
// own real, correct length instead of the buggy upstream constant at
// those 5 call sites only - every other real `CopyIntoBuffer` count in
// the file (verified exact) is untouched.
//
// Two small, deliberate simplifications, both documented here rather
// than silently changed:
// - Real `setup()`'s own blocking system `arduboy.titleScreen(F("Star
//   Honor\nWenceslao Villanueva\nPort by Wuuff"))` (this shim has no
//   `gbTitleScreen()` primitive at all - every other ported game
//   converts a real blocking `titleScreen()` call into its own explicit
//   title *state* instead, e.g. gamePong.c's own `PONG_STATE_TITLE`) is
//   not shown as a separate step before the real game's own custom
//   `TitleLoop` state - the genuine, already-distinct `TitleLoop` state
//   (a starfield background, "Star Honor" typed out over time, and the
//   credited author underneath) already provides an equivalent
//   title-plus-author display gated on a real button press, so the two
//   real, back-to-back title screens collapse into just entering
//   `STAR_STATE_TITLELOOP` directly - the same "skip the redundant
//   system title screen, the game's own first state already covers it"
//   shape as every other ported game here.
// - Real `arduboy.buttons.pressed(BTN_C)` (mid-`GetInput()`) calls a
//   second blocking system `arduboy.titleScreen(F("Paused"))` - since
//   that real primitive doesn't exist here either, this is ported as a
//   genuine new `STAR_STATE_PAUSED` state instead (freezes whichever
//   real state was active, shows "PAUSED"/"PRESS A", resumes on a real
//   A-press - Gamebuino's own real `titleScreen()` dismiss button is
//   also A, matching e.g. gamePong.c's own already-established
//   precedent) rather than dropping Button-C pause entirely.
//
// Dialect notes specific to this file: no ternary operator anywhere (all
// of `PlayerUpdate()`'s own real `(cond) ? a : b` chains for wrapping
// `ShipRotation` into [0,360) became plain `if`/`else`); no array-typed
// struct member is used anywhere (per this project's own still-unproven-
// pattern caution from `gamebuino-solitaire`'s own header comment) -
// `crewCharArray`/`maxCrewCharArray`/`fuelCharArray` (real `char[4]`
// members of `Ship`) became three separate top-level `int[4]` globals
// instead, since there is only ever one real `Ship` instance anyway.
// Pointer-typed struct members (`int* bitmap`) are used freely - already
// a separately-proven-safe pattern elsewhere in this project (e.g.
// gameBRally.c's own `int* sprite`).
//
// A real, live-reported bug: combat never ended, planet defense drifting
// further negative every round instead of stopping at a win. Root cause:
// real upstream's `EncouterUpdate()` combat-calc `switch` case has an
// early `break;` in its own victory branch, skipping the shield-damage
// calc and the case's own trailing `nextSequence = 9` below it - the only
// thing that makes `nextSequence = 15` (victory) actually stick. This
// dialect has no `switch`, so that whole case became an `if`/`else if`
// chain - and the mechanical conversion missed that the trailing
// `nextSequence = 9` needs to move inside the win-check's own `else` to
// reproduce the dropped `break`'s effect; left unconditional, it
// overwrote the just-set victory sequence back to 9 (damage report) every
// single round. Fixed by nesting it inside the `else`, matching real
// upstream's own early-exit control flow exactly - see the inline comment
// at the fix site for the full reasoning.

// -----------------------------------------------------------------------------
//   Constants
// -----------------------------------------------------------------------------

#define STAR_PI 3.14159265
#define STAR_ROTATION_RATE 120.0

// Direction - real upstream's own `typedef enum { Up, UpRight, Right,
// DownRight, Down, DownLeft, Left, UpLeft, None } Direction;`. Kept as
// plain #defines (not a real `enum`) so every direction-holding variable
// can just be a plain `int` - this dialect implicitly converts enum->int
// but never int->enum, and DPad/ShipFacing are both computed from plain
// int arithmetic throughout, not assigned from a named enum constant.
#define STAR_DIR_UP        0
#define STAR_DIR_UPRIGHT   1
#define STAR_DIR_RIGHT     2
#define STAR_DIR_DOWNRIGHT 3
#define STAR_DIR_DOWN      4
#define STAR_DIR_DOWNLEFT  5
#define STAR_DIR_LEFT      6
#define STAR_DIR_UPLEFT    7
#define STAR_DIR_NONE      8

// SystemTarget - real upstream's own `typedef enum SystemTarget { Crew,
// Hull, Weapons, Shields, Engines, NoTarget };`
#define STAR_SYS_CREW    0
#define STAR_SYS_HULL    1
#define STAR_SYS_WEAPONS 2
#define STAR_SYS_SHIELDS 3
#define STAR_SYS_ENGINES 4
#define STAR_SYS_NONE    5

// Loot - real upstream's own `typedef enum Loot { NoLoot, LootHull,
// LootWeapons, LootShields, LootEngines, LootCrew, LootFuel };`
#define STAR_LOOT_NONE    0
#define STAR_LOOT_HULL    1
#define STAR_LOOT_WEAPONS 2
#define STAR_LOOT_SHIELDS 3
#define STAR_LOOT_ENGINES 4
#define STAR_LOOT_CREW    5
#define STAR_LOOT_FUEL    6

// State - real upstream's own `typedef enum { Title, TitleLoop, Prologue,
// Map, Status, Encounter, GameOver, TimeUp, Reset, WinGame, Warping }
// State;` (real `Title` is genuinely dead - declared, never referenced
// anywhere in the real switch/ChangeGameState - dropped here).
// STAR_STATE_PAUSED is a new state this port needed - see this file's
// own header comment.
#define STAR_STATE_TITLELOOP 0
#define STAR_STATE_PROLOGUE  1
#define STAR_STATE_MAP       2
#define STAR_STATE_STATUS    3
#define STAR_STATE_ENCOUNTER 4
#define STAR_STATE_GAMEOVER  5
#define STAR_STATE_TIMEUP    6
#define STAR_STATE_RESET     7
#define STAR_STATE_WINGAME   8
#define STAR_STATE_WARPING   9
#define STAR_STATE_PAUSED    10

// -----------------------------------------------------------------------------
//   Vector2d -> StarVec2
// -----------------------------------------------------------------------------

typedef struct{
    float x;
    float y;
} StarVec2;

float starVecMagSq( StarVec2* v )
{
    return v->x * v->x + v->y * v->y;
}

float starVecMag( StarVec2* v )
{
    return sqrt( starVecMagSq( v ) );
}

void starVecNormalize( StarVec2* v )
{
    float mag = starVecMag( v );
    // Defensive guard not present upstream (real AVR float division by
    // zero silently yields Infinity/NaN) - this platform hard-traps on
    // a divide by zero (see VIRCON32_C_DIALECT.md), so this guard exists
    // purely to avoid a crash. Never actually reachable in practice: the
    // only real call site (`starShipUpdateMovement()`) only normalizes
    // once `MagnitudeSquared() > MaxVelocity^2`, which is always > 0.
    if( mag <= 0.0 ) return;
    v->x = v->x / mag;
    v->y = v->y / mag;
}

void starVecAdd( StarVec2* a, StarVec2* b, StarVec2* out )
{
    out->x = a->x + b->x;
    out->y = a->y + b->y;
}

void starVecSub( StarVec2* a, StarVec2* b, StarVec2* out )
{
    out->x = a->x - b->x;
    out->y = a->y - b->y;
}

// `out` may safely alias `v` (every field is computed from `v`'s own
// still-unmodified value before either field of `out` is written).
void starVecScale( StarVec2* v, float s, StarVec2* out )
{
    out->x = v->x * s;
    out->y = v->y * s;
}

void starVecAddInPlace( StarVec2* a, StarVec2* b )
{
    a->x = a->x + b->x;
    a->y = a->y + b->y;
}

// Real upstream `Vector2d::Rotate()` never actually returns a value
// despite its own declared `Vector2d` return type (a real, harmless
// upstream bug - every real call site uses it as a bare statement, never
// reads a return value) - ported as `void`, operating in place.
void starVecRotate( StarVec2* v, float angleDeg )
{
    float radian = angleDeg * STAR_PI / 180.0;
    float cs = cos( radian );
    float sn = sin( radian );
    float px = v->x * cs - v->y * sn;
    float py = v->x * sn + v->y * cs;
    v->x = px;
    v->y = py;
}

// Matches real Arduino `random(min,max)` semantics: [minV,maxV), safe
// against arand()'s own real `n<=0` guard by falling back to minV.
int starRandRange( int minV, int maxV )
{
    int range = maxV - minV;
    if( range <= 0 ) return minV;
    return minV + arand( range );
}

// -----------------------------------------------------------------------------
//   Bitmaps - converted from bitmaps.h's own real B-literal byte data
//   (see this file's own header comment)
// -----------------------------------------------------------------------------

int starShipUpBmp[34] = { 16, 16,
  30, 0, 97, 128, 140, 64, 161, 64, 128, 64, 109, 128,
  63, 0, 18, 0, 179, 64, 210, 192, 191, 64, 204, 192,
  128, 64, 128, 64, 0, 0, 0, 0
};

int starShipLeftBmp[34] = { 16, 16,
  56, 252, 68, 80, 86, 160, 131, 224, 166, 48, 166, 48,
  131, 224, 86, 160, 68, 80, 56, 252, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0
};

int starShipRightBmp[34] = { 16, 16,
  252, 112, 40, 136, 21, 168, 31, 4, 49, 148, 49, 148,
  31, 4, 21, 168, 40, 136, 252, 112, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0
};

int starShipDownBmp[34] = { 16, 16,
  128, 64, 128, 64, 204, 192, 191, 64, 210, 192, 179, 64,
  18, 0, 63, 0, 109, 128, 128, 64, 161, 64, 140, 64,
  97, 128, 30, 0, 0, 0, 0, 0
};

int starShipUpLeftBmp[34] = { 16, 16,
  62, 0, 65, 0, 149, 0, 161, 0, 129, 32, 163, 16,
  134, 232, 124, 76, 2, 50, 3, 32, 10, 208, 4, 176,
  3, 0, 1, 0, 0, 128, 0, 0
};

int starShipUpRightBmp[34] = { 16, 16,
  0, 248, 1, 4, 1, 82, 1, 10, 9, 2, 17, 138,
  46, 194, 100, 124, 152, 128, 9, 128, 22, 160, 26, 64,
  1, 128, 1, 0, 2, 0, 0, 0
};

int starShipDownLeftBmp[34] = { 16, 16,
  0, 128, 1, 0, 3, 0, 4, 176, 10, 208, 3, 32,
  2, 50, 124, 76, 134, 232, 163, 16, 129, 32, 161, 0,
  149, 0, 65, 0, 62, 0, 0, 0
};

int starShipDownRightBmp[34] = { 16, 16,
  2, 0, 1, 0, 1, 128, 26, 64, 22, 160, 9, 128,
  152, 128, 100, 124, 46, 194, 17, 138, 9, 2, 1, 10,
  1, 82, 1, 4, 0, 248, 0, 0
};

int starSelArrowBmp[10] = { 8, 8,
  48, 56, 60, 254, 254, 60, 56, 48
};

int starPlanetBmp1[34] = { 16, 16,
  3, 192, 15, 240, 31, 232, 63, 244, 127, 242, 127, 250,
  255, 253, 255, 253, 255, 253, 251, 255, 93, 222, 75, 254,
  37, 252, 17, 248, 12, 48, 3, 192
};

int starPlanetBmp2[34] = { 16, 16,
  3, 192, 15, 240, 19, 200, 48, 4, 88, 230, 127, 134,
  159, 143, 135, 135, 135, 193, 131, 129, 64, 194, 64, 226,
  32, 228, 16, 232, 12, 176, 3, 192
};

int starPlanetBmp3[34] = { 16, 16,
  3, 192, 12, 48, 16, 8, 39, 196, 72, 102, 80, 38,
  144, 39, 147, 39, 147, 101, 145, 197, 80, 14, 76, 26,
  39, 244, 16, 200, 12, 48, 3, 192
};

int starPlanetBmp4[34] = { 16, 16,
  3, 192, 13, 176, 16, 200, 56, 100, 76, 50, 70, 26,
  227, 13, 177, 135, 152, 195, 140, 97, 70, 50, 67, 26,
  33, 140, 16, 200, 12, 112, 3, 192
};

int starPlanetBmp5[34] = { 16, 16,
  3, 192, 12, 48, 24, 8, 61, 4, 95, 30, 79, 26,
  130, 25, 130, 113, 128, 121, 156, 57, 124, 10, 76, 58,
  39, 36, 19, 8, 12, 48, 3, 192
};

int starPlanetBmp6[34] = { 16, 16,
  3, 192, 12, 48, 30, 24, 44, 28, 64, 2, 64, 130,
  129, 193, 129, 227, 144, 103, 158, 103, 78, 14, 78, 14,
  32, 4, 16, 232, 12, 176, 3, 192
};

int starPlanetBmp7[34] = { 16, 16,
  3, 192, 12, 48, 16, 8, 39, 252, 64, 2, 64, 2,
  156, 65, 162, 57, 156, 1, 128, 193, 65, 34, 64, 194,
  44, 4, 16, 8, 12, 48, 3, 192
};

int starPlanetBmp8[34] = { 16, 16,
  3, 192, 12, 48, 16, 8, 34, 4, 69, 10, 66, 22,
  128, 9, 128, 97, 128, 145, 140, 145, 82, 98, 82, 2,
  44, 4, 16, 8, 12, 48, 3, 192
};

int starPlanetBmp9[34] = { 16, 16,
  3, 192, 12, 48, 16, 8, 46, 4, 74, 2, 78, 34,
  128, 249, 128, 137, 129, 173, 128, 137, 92, 250, 84, 34,
  60, 4, 16, 8, 12, 48, 3, 192
};

int starPlanetBmp10[34] = { 16, 16,
  255, 255, 128, 1, 158, 57, 146, 41, 158, 57, 128, 1,
  131, 225, 130, 33, 130, 33, 130, 33, 186, 33, 171, 225,
  184, 29, 128, 21, 128, 29, 255, 255
};

// Real upstream declares this {16,64} in its own array name
// (`PlanetHome_16_64`), but the real declared bitmap header is {16,48} -
// the first/last 8 real rows of a would-be 64-row bitmap are commented
// out in the real source, leaving exactly 48 real active rows, matching
// the header exactly. See this file's own header comment.
int starPlanetHomeBmp[98] = { 16, 48,
  1, 126, 1, 30, 2, 30, 2, 14, 2, 0, 7, 0,
  7, 128, 7, 224, 15, 240, 15, 248, 15, 248, 31, 248,
  29, 248, 60, 240, 56, 248, 57, 252, 127, 252, 127, 252,
  127, 254, 127, 254, 255, 254, 255, 252, 238, 56, 204, 0,
  252, 0, 252, 0, 252, 0, 252, 0, 124, 0, 112, 7,
  96, 61, 64, 127, 32, 255, 32, 255, 33, 255, 19, 127,
  18, 255, 12, 255, 9, 255, 11, 255, 7, 255, 7, 255,
  7, 255, 3, 255, 3, 255, 3, 255, 1, 255, 1, 255
};

int starBubbleBmp[10] = { 8, 8,
  48, 120, 252, 252, 120, 48, 0, 0
};

int starBubbleEmptyBmp[10] = { 8, 8,
  48, 72, 132, 132, 72, 48, 0, 0
};

int* starPlayerBitmaps[8] = { starShipUpBmp, starShipUpRightBmp, starShipRightBmp, starShipDownRightBmp,
                               starShipDownBmp, starShipDownLeftBmp, starShipLeftBmp, starShipUpLeftBmp };

int* starPlanetArt[10] = { starPlanetBmp1, starPlanetBmp2, starPlanetBmp3, starPlanetBmp4, starPlanetBmp5,
                            starPlanetBmp6, starPlanetBmp7, starPlanetBmp8, starPlanetBmp9, starPlanetBmp10 };

// -----------------------------------------------------------------------------
//   Text data - real upstream PROGMEM char[] strings, ported as plain
//   int[] literals (this dialect's PROGMEM/pgm_read_byte are no-ops - see
//   avrCompat.h). Only strings actually reachable from real gameplay code
//   are ported (a full grep found several real upstream string
//   declarations/arrays - `Comm_B_1`, `Repaired`, `EngageText`/
//   `ProjectionText`, `Result_Positive`/`Result_Negative`, `BeamRecovery`,
//   `Upgrade`, `CombatTurn`, `SectorReachedB`, `Colon`, `Yes`/`No`,
//   `TFood`/`TGoods` - that are declared but never actually read by any
//   real code path; dropped, matching this project's "don't port
//   genuinely dead code" precedent).
// -----------------------------------------------------------------------------

char starTitleScreen[11] = "Star Honor";
char starTitleScreen2[21] = "Wenceslao Villanueva";
char starTHull[5] = "Hull";
char starTWeapons[8] = "Weapons";
char starTShields[8] = "Shields";
char starTEngines[8] = "Engines";
char starTCrew[11] = "Red Shirts";
char starTFuel[8] = "Crystal";
char starRedShirtsRepaired[18] = "All hands on duty";
char starHullRepaired[14] = "Hull Repaired";
char starWeaponsRepaired[20] = "Weapon Systems 100%";
char starShieldsRepaired[17] = "Shields restored";
char starEnginesRepaired[16] = "Engines at full";
char starStatusHelp4[21] = "Warp needs 3 Crystal";
char starEmergencyRepairs[9] = "Repairs:";
char starPrologueText1[76] = "USS Arduino Log:\n-Most crew dead.\n-Sensors destroyed.\n-Other systems work.\n";
char starPrologueText2[85] = "Captain dead, but we\nhave the phage cure.\nWe must get home. You\nare Captain. Orders?";
char starGameWin1[53] = "Captain, we made it.\nDispensing cure into\natmosphere";
char starGameWin2[40] = "Scans coming back.\nThe cure is working.";
char starGameWin3[67] = "Captain! Comms from\nCommand: power down\nand prepare to be\nboarded?";
char starGameWin4[19] = "To Be Continued...";
char starEngageCombatA[10] = "Open Fire";
char starEngageCombatB[18] = "Fire all weapons!";
char starEngageCombatC[17] = "Target and Fire!";
char starRepairCombatA[14] = "Status Report";
char starRepairCombatB[14] = "System Status";
char starRepairCombatC[17] = "Situation Report";
char starFleeCombatA[14] = "Abort! Abort!";
char starFleeCombatB[18] = "Shields! Retreat!";
char starFleeCombatC[18] = "Full Reverse, go!";
char starHail[19] = "B: Establish comms";
char starNegativeResponseA[19] = "Surrender or else!";
char starNegativeResponseB[21] = "We got you now scum!";
char starNegativeResponseC[33] = "For the Empire I'll\ndestroy you!";
char starNeutralResponseA[30] = "It's nice to meet\nnew species";
char starNeutralResponseB[18] = "... *no response*";
char starNeutralResponseC[25] = "We're not buying,\nleave!";
char starNeutralResponseD[21] = "Greetings Traveller.";
char starPositiveResponseA[28] = "Stand down,\nwe mean no harm";
char starPositiveResponseB[28] = "We want to help.\nHave this.";
char starPositiveResponseC[31] = "Your people need you\nTake this";
char starVictoryA[18] = "We are victorious";
char starVictoryB[20] = "Targets neutralized";
char starDefeatA[32] = "Multiple breaches!\nEscape Pods!";
char starDefeatB[37] = "We're losing core\ncontainment! We...";
char starDefeatC[34] = "The USS Arduino was\nlost to space";
char starDefeatD[39] = "We've recieved new\norders from home...";
char starDefeatE[80] = "...Final orders...\nRun with what's left\nThe phage took every\none...It's over...";
char starDiscoveredUpgrade[29] = "Discovered upgrade\nfor the: ";
char starDiscoveredGood[8] = "Found:\n";
char starDiscoveredNothing[26] = "Sir, found\nnothing of use";
char starCapturedCrew[14] = "captured crew";
char starCombatTakeDamage1[13] = "We've taken ";
char starCombatTakeDamage2[10] = " damage!\n";
char starCombatTakeDamage3[18] = "Shields absorbed ";
char starCombatTakeDamage4[13] = "Weapons did ";
char starShieldsHolding[19] = "Shields holding...";
char starShieldsDown[19] = "Shields are down!\n";
char starDamageReportCrew[20] = "We have casualties!";
char starDamageReportHull[13] = "Hull damage!";
char starDamageReportWeapons[13] = "Weapons hit!";
char starDamageReportShields[19] = "Shield arrays hit!";
char starDamageReportEngines[21] = "Engines damaged sir!";
char starCombatMenuPlayer[7] = "Player";
char starCombatMenuAtk[5] = "Atk:";
char starCombatMenuShld[6] = "Shld:";
char starCombatMenuHull[6] = "Hull:";
char starCombatMenuEnemy[6] = "Enemy";
char starCombatMenuDef[5] = "Def:";
char starSectorReachedA[17] = "Entering Sector ";
char starPaused[7] = "Paused";
char starSlashStr[2] = "/";

// Real upstream icon-glyph strings (`\26` = octal escape = ASCII 22, a
// real Gamebuino font5x7/font3x5 button-icon glyph) built as explicit
// 0-terminated int[] character-code arrays instead of a quoted string
// literal - matching this project's own already-established precedent
// for the identical real gap (gameTaquin.c/gameSimonbuino.c/
// gameSpinSpinSpinbuino.c's own header comments).
char starStatusHelp[15] = { 22, 45, 65, 115, 115, 105, 103, 110, 32, 67, 114, 101, 119, 115, 0 };
char starStatusHelp2[13] = { 22, 45, 84, 114, 101, 97, 116, 32, 67, 114, 101, 119, 0 };
char starStatusHelp3[22] = { 85, 112, 43, 22, 45, 87, 97, 114, 112, 32, 110, 101, 120, 116, 32, 115, 101, 99, 116, 111, 114, 0 };
char starSpendEmergencyRepairs[19] = { 22, 45, 69, 109, 101, 114, 103, 101, 110, 99, 121, 32, 82, 101, 112, 97, 105, 114, 0 };

char* starRepairedText[5] = { starRedShirtsRepaired, starHullRepaired, starWeaponsRepaired, starShieldsRepaired, starEnginesRepaired };
char* starEngageCombat[3] = { starEngageCombatA, starEngageCombatB, starEngageCombatC };
char* starRepairCombat[3] = { starRepairCombatA, starRepairCombatB, starRepairCombatC };
char* starFleeCombat[3] = { starFleeCombatA, starFleeCombatB, starFleeCombatC };
char* starNegativeResponse[3] = { starNegativeResponseA, starNegativeResponseB, starNegativeResponseC };
char* starNeutralResponse[4] = { starNeutralResponseA, starNeutralResponseB, starNeutralResponseC, starNeutralResponseD };
char* starPositiveResponse[3] = { starPositiveResponseA, starPositiveResponseB, starPositiveResponseC };
char* starVictory[2] = { starVictoryA, starVictoryB };

// -----------------------------------------------------------------------------
//   Ship (flattened from the real `Ship` class)
// -----------------------------------------------------------------------------

typedef struct{
    bool isAlive;
    float maxVelocity;
    float shipAcceleration;
    float shipFriction;

    int hpHull;
    int hpWeapons;
    int hpEngine;
    int hpShields;
    int crew;
    int fuel;

    int maxHull;
    int maxWeapons;
    int maxEngine;
    int maxShields;
    int maxCrew;
    int maxFuel;

    int shipFacing;
    int* bitmap;

    StarVec2 mapPosition;
    StarVec2 velocity;
    float shipRotation;
} StarShip;

StarShip starPlayerShip;

// Real `char crewCharArray[4]`/`maxCrewCharArray[4]`/`fuelCharArray[4]`
// members of `Ship` - kept as top-level globals rather than struct
// members (see this file's own header comment on array-typed struct
// members). Sized [4] exactly like real upstream (3 digits + null) -
// preserved as-is; crew/max-crew can in principle exceed 999 given
// enough LootCrew upgrades in one very long playthrough, which would
// silently overflow into whatever global happens to be declared next,
// the exact same real risk real upstream's own fixed-size `char[4]`
// already carries on real hardware, not a regression introduced here.
char starCrewCharArray[4];
char starMaxCrewCharArray[4];
char starFuelCharArray[4];

// -----------------------------------------------------------------------------
//   Planetoid -> StarPlanet
// -----------------------------------------------------------------------------

typedef struct{
    int* bitmap;
    StarVec2 mapPosition;
    int alignment;
    int attack;
    int defense;
    int prize;
    bool contacted;
} StarPlanet;

StarPlanet starPlanets[10];
// -1 stands in for real upstream's own `LatestPlanetEncountered == NULL`
// - a plain array index, not a pointer, so this dialect's own real
// NULL-is-(-1) pointer trivia never needs to come into play at all.
int starLatestPlanetIdx = -1;

// -----------------------------------------------------------------------------
//   SelectionArrow (flattened - see this file's own header comment)
// -----------------------------------------------------------------------------

int starOverviewX[6] = { 0, 0, 0, 0, 0, 0 };
int starOverviewY[6] = { -2, 4, 10, 16, 22, 28 };
int starCombatX[3] = { 2, 2, 2 };
int starCombatY[3] = { 25, 32, 39 };

int starRepairArrowPos = 0;
int starCombatArrowPos = 0;

void starRepairArrowDraw()
{
    gbDrawBitmap( starOverviewX[ starRepairArrowPos ], starOverviewY[ starRepairArrowPos ], starSelArrowBmp );
}

void starRepairArrowMoveUp()
{
    starRepairArrowPos = starRepairArrowPos - 1;
    if( starRepairArrowPos < 0 ) starRepairArrowPos = 5;
}

void starRepairArrowMoveDown()
{
    starRepairArrowPos = starRepairArrowPos + 1;
    starRepairArrowPos = starRepairArrowPos % 6;
}

void starCombatArrowDraw()
{
    gbDrawBitmap( starCombatX[ starCombatArrowPos ], starCombatY[ starCombatArrowPos ], starSelArrowBmp );
}

void starCombatArrowMoveUp()
{
    starCombatArrowPos = starCombatArrowPos - 1;
    if( starCombatArrowPos < 0 ) starCombatArrowPos = 2;
}

void starCombatArrowMoveDown()
{
    starCombatArrowPos = starCombatArrowPos + 1;
    starCombatArrowPos = starCombatArrowPos % 3;
}

// -----------------------------------------------------------------------------
//   Game state globals (flattened from Globals.h/.cpp) - declared before
//   StarField below since starFieldMove() already needs starDeltaTime
//   (this dialect has no forward declaration for plain variables, only
//   functions, so real declaration order matters here).
// -----------------------------------------------------------------------------

float starDeltaTime = 0.05;
int starWaitTime = 0;
int starStatusBlinkTime = 60;

int starGameState;
int starPreviousGameState;
int starPausedFromState;

int starMenuWaitTime;
int starSequenceStage = 0;
int starCurrentSector = 1;

bool starStatusUpdateAvailable = false;
int starStatusUpdateTime = 240;
char* starStatusUpdate;

bool starCanHail = false;
bool starRunningAway = false;

int starDPad;
bool starAButton = false;
bool starBButton = false;
bool starNewButtonInputAllowed = true;

StarVec2 starMapUpperBounds;
StarVec2 starMapLowerBounds;

int starCombatPlanetDef;
int starCombatPlanetDamage;
int starCombatShipShieldDamage;
int starCombatShipDamage;

float starTimeUntilNextRepair;
float starRepairTime = 2.0;
int starBattleRepairs = 3;
int starBattleRepairsMax = 3;

float starSeconds = 60;
int starMinutes = 9;

int starRepairTarget;
int starSystemDamaged;

bool starAcceptMenuInput;

char* starCommA1;
char* starCmdAtk;
char* starCmdRepair;
char* starCmdFlee;
char* starCombatResult;

// -----------------------------------------------------------------------------
//   StarField
// -----------------------------------------------------------------------------

typedef struct{
    float x;
    float y;
    int velocity;
} StarStar;

StarStar starStars[20];

void starFieldInit()
{
    int i;
    for( i = 0; i < 20; i = i + 1 )
    {
        starStars[i].x = starRandRange( 0, 84 );
        starStars[i].y = starRandRange( 0, 48 );
        starStars[i].velocity = starRandRange( 1, 25 );
    }
}

void starFieldDraw()
{
    int i;
    for( i = 0; i < 20; i = i + 1 )
      gbDrawPixel( (int)starStars[i].x, (int)starStars[i].y );
}

void starFieldBoundsCheck( int i )
{
    bool setVelocity = false;
    int xPos = (int)starStars[i].x;
    int yPos = (int)starStars[i].y;

    if( xPos > 84 )
    {
        starStars[i].x = 0.0;
        starStars[i].y = starRandRange( 0, 48 );
        setVelocity = true;
    }
    else if( xPos < 0 )
    {
        starStars[i].x = 84.0;
        starStars[i].y = starRandRange( 0, 48 );
        setVelocity = true;
    }
    else if( yPos > 48 )
    {
        starStars[i].x = starRandRange( 0, 84 );
        starStars[i].y = 0.0;
        setVelocity = true;
    }
    else if( yPos < 0 )
    {
        starStars[i].x = starRandRange( 0, 84 );
        starStars[i].y = 48;
        setVelocity = true;
    }

    if( setVelocity )
      starStars[i].velocity = starRandRange( 1, 25 );
}

void starFieldMove( StarVec2* v )
{
    int i;
    float velocityOverTime;

    for( i = 0; i < 20; i = i + 1 )
    {
        velocityOverTime = starStars[i].velocity * starDeltaTime;
        starStars[i].x = starStars[i].x + v->x * velocityOverTime;
        starStars[i].y = starStars[i].y + v->y * velocityOverTime;
        starFieldBoundsCheck( i );
    }
}

// -----------------------------------------------------------------------------
//   Text system (flattened from the real singleton `Text`/`TextManager`)
// -----------------------------------------------------------------------------

char starBuffer[64];
char starTypeBuffer[128];
char starClockBuffer[5];

bool starTextOverTimeRunning = false;
int starFramesToNextChar = 3;
int starCharIndex = 0;
int starFramesPerChar = 2;

int starTextLineCount( char* text )
{
    int count = 1;
    int i = 0;
    while( text[i] != 0 )
    {
        if( text[i] == 10 ) count = count + 1;
        i = i + 1;
    }
    return count;
}

void starDisplayTextRaw( char* text, int x, int y )
{
    gbCursorX = x;
    gbCursorY = y;
    gbPrintString( text );
}

// Real `Text::DisplayTextClear()` - draws once inside a small (9px-tall)
// cleared box, and if the text turned out to be multi-line, clears a
// taller box and draws the whole thing again - preserved exactly,
// including the real small-box draw happening at all (not skipped)
// before the line count is known.
void starDisplayTextClear( char* text, int x, int y, bool withBorder )
{
    int lines;
    int clearHeight;

    gbSetColor( GB_BLACK );
    gbFillRect( 0, y, 84, 9 );
    gbSetColor( GB_WHITE );
    if( withBorder ) gbDrawRect( 0, y, 84, 9 );

    starDisplayTextRaw( text, x + 2, y + 2 );
    lines = starTextLineCount( text );

    if( lines > 1 )
    {
        clearHeight = 6 * lines + 3;
        gbSetColor( GB_BLACK );
        gbFillRect( 0, y, 84, clearHeight );
        gbSetColor( GB_WHITE );
        if( withBorder ) gbDrawRect( 0, y, 84, clearHeight );
        starDisplayTextRaw( text, x + 2, y + 2 );
    }
}

// Real `Text::ReadTextIntoTypeBuffer()` - copies the first `starCharIndex`
// characters of `text` into `starTypeBuffer`. Returns false (matching
// `starTextOverTimeRunning` becoming false) if `text` turns out to be
// shorter than `starCharIndex` (the typewriter has caught up to the real
// end of the string).
bool starReadTextIntoTypeBuffer( char* text )
{
    int i;
    for( i = 0; i < starCharIndex; i = i + 1 )
    {
        if( text[i] == 0 )
        {
            starTextOverTimeRunning = false;
            return false;
        }
        starTypeBuffer[i] = text[i];
    }
    starTypeBuffer[ starCharIndex ] = 0;
    return true;
}

void starDeltaFramesToNextChar()
{
    starFramesToNextChar = starFramesToNextChar - 1;
    if( starFramesToNextChar <= 0 )
    {
        starCharIndex = starCharIndex + 1;
        starFramesToNextChar = starFramesPerChar;
    }
}

// Shared body for real `DisplayTextOverTime()` (withBorder=true) and
// `DisplayTextOverTimeClear()` (withBorder=false) - both real functions
// were byte-for-byte identical apart from that one flag, so this is a
// plain de-duplication, not a behavior change.
bool starTextOverTimeGeneric( char* text, int x, int y, bool withBorder )
{
    if( starTextOverTimeRunning )
    {
        if( !starReadTextIntoTypeBuffer( text ) ) return false;
        starDisplayTextClear( starTypeBuffer, x, y, withBorder );
        starDeltaFramesToNextChar();
        return false;
    }
    else
    {
        starDisplayTextClear( text, x, y, withBorder );
        return true;
    }
}

bool starTextOverTime( char* text, int x, int y )
{
    return starTextOverTimeGeneric( text, x, y, true );
}

bool starTextOverTimeClear( char* text, int x, int y )
{
    return starTextOverTimeGeneric( text, x, y, false );
}

void starTextNewOverTime()
{
    starTextOverTimeRunning = true;
    starFramesToNextChar = starFramesPerChar;
    starCharIndex = 0;
}

void starIntToChar( int value, char* buf, int index )
{
    itoa( value, &buf[index], 10 );
}

int starCopyIntoBuffer( char* chars, int startIndex, int count )
{
    int i;
    for( i = 0; i < count; i = i + 1 )
      starTypeBuffer[ i + startIndex ] = chars[i];
    return count;
}

// Real upstream's own `for(; CombatXxxCopy /= 10; index++);` digit-count
// loop - counts the number of EXTRA digit positions beyond the first
// (the first digit is always accounted for separately by a plain
// `index++` at each real call site). See this file's own header comment
// on the one real call site that (faithfully) passes the wrong variable
// into this helper.
int starDigitExtra( int value )
{
    int count = 0;
    int v = value;
    while( v >= 10 )
    {
        v = v / 10;
        count = count + 1;
    }
    return count;
}

// -----------------------------------------------------------------------------
//   Forward declarations (real upstream ordering has genuine circular
//   calls between ChangeGameState()/SetupEncounter()/EncouterUpdate()
//   etc - forward-declared here rather than reshuffled to avoid it)
// -----------------------------------------------------------------------------

void starChangeGameState( int newState );
void starSetupEncounter();
void starEncounterUpdate();
void starDrawCombatScreen( bool drawCommands );
void starGenerateReward( int reward );
void starDrawMap();
void starShipPlayerUpdate( StarShip* s );
void starShipRepairSystem( StarShip* s );

// -----------------------------------------------------------------------------
//   Ship logic (flattened from the real `Ship` class)
// -----------------------------------------------------------------------------

void starShipSetup( StarShip* s )
{
    s->isAlive = true;
    // Real upstream: `Ship`'s own class-default member initializers
    // (`ShipAcceleration=10.0`, `ShipFriction=1.0f`, `ShipFacing=Up`),
    // applied only once by the real constructor and never touched again
    // by `SetupShip()` itself - folded in here too since nothing else in
    // the whole game ever modifies these three fields, so re-setting them
    // on every reset produces the exact same values a real one-time
    // constructor call would.
    s->shipAcceleration = 10.0;
    s->shipFriction = 1.0;
    s->shipFacing = STAR_DIR_UP;
    s->shipRotation = 90;

    s->hpHull = 10;
    s->hpWeapons = 5;
    s->hpShields = 5;
    s->hpEngine = 5;
    s->maxVelocity = s->hpEngine + 5;
    s->fuel = 0;
    s->crew = 33;
    s->bitmap = starShipRightBmp;

    s->maxHull = s->hpHull;
    s->maxWeapons = s->hpWeapons;
    s->maxEngine = s->hpEngine;
    s->maxShields = s->hpShields;
    s->maxCrew = s->crew;
    s->maxFuel = 25;

    s->velocity.x = 0;
    s->velocity.y = 0;
    s->mapPosition.x = 0;
    s->mapPosition.y = 0;

    starRepairTarget = STAR_SYS_NONE;
}

int starShipTakeDamageTarget( StarShip* s, int damage, int target )
{
    if( target == STAR_SYS_CREW )
    {
        s->crew = s->crew - damage;
        s->crew = gbMax( s->crew, 0 );
    }
    else if( target == STAR_SYS_HULL )
    {
        s->hpHull = s->hpHull - damage;
    }
    else if( target == STAR_SYS_WEAPONS )
    {
        s->hpWeapons = s->hpWeapons - damage;
        s->hpWeapons = gbMax( s->hpWeapons, 0 );
    }
    else if( target == STAR_SYS_ENGINES )
    {
        s->hpEngine = s->hpEngine - damage;
        s->hpEngine = gbMax( s->hpEngine, 0 );
    }
    else if( target == STAR_SYS_SHIELDS )
    {
        s->hpShields = s->hpShields - damage;
        s->hpShields = gbMax( s->hpShields, 0 );
    }

    if( s->hpHull <= 0 || s->crew <= 0 )
      s->isAlive = false;

    return target;
}

int starShipTakeDamageRandom( StarShip* s, int damage )
{
    int target = arand( 5 );
    return starShipTakeDamageTarget( s, damage, target );
}

void starShipDrawOnMap( StarShip* s )
{
    if( s->isAlive )
      gbDrawBitmap( 36, 18, s->bitmap );
}

void starShipCalcThrust( StarShip* s, int dir, StarVec2* out )
{
    int rotation;
    out->x = 0;
    out->y = -0.2 - s->hpEngine / 3.0;
    rotation = dir * 45;
    starVecRotate( out, rotation );
}

void starShipUpdateMovement( StarShip* s, StarVec2* thrust )
{
    StarVec2 accelDelta;
    StarVec2 moveDelta;

    starVecScale( thrust, s->shipAcceleration, &accelDelta );
    starVecScale( &accelDelta, starDeltaTime, &accelDelta );
    starVecAddInPlace( &s->velocity, &accelDelta );

    if( starVecMagSq( &s->velocity ) > ( s->maxVelocity * s->maxVelocity ) )
    {
        starVecNormalize( &s->velocity );
        starVecScale( &s->velocity, s->maxVelocity, &s->velocity );
    }

    starVecScale( &s->velocity, starDeltaTime, &moveDelta );
    starVecScale( &moveDelta, 4, &moveDelta );
    starVecAddInPlace( &s->mapPosition, &moveDelta );

    if( s->mapPosition.x < starMapUpperBounds.x )
    {
        s->mapPosition.x = starMapUpperBounds.x;
        s->velocity.x = 0;
    }
    if( s->mapPosition.y < starMapUpperBounds.y )
    {
        s->mapPosition.y = starMapUpperBounds.y;
        s->velocity.y = 0;
    }
    if( s->mapPosition.x > starMapLowerBounds.x )
    {
        s->mapPosition.x = starMapLowerBounds.x;
        s->velocity.x = 0;
    }
    if( s->mapPosition.y > starMapLowerBounds.y )
    {
        s->mapPosition.y = starMapLowerBounds.y;
        s->velocity.y = 0;
    }
}

void starShipPlayerUpdate( StarShip* s )
{
    StarVec2 friction;
    StarVec2 thrust;
    int rotation;

    if( starDPad == STAR_DIR_UPLEFT || starDPad == STAR_DIR_LEFT || starDPad == STAR_DIR_DOWNLEFT )
    {
        s->shipRotation = s->shipRotation - STAR_ROTATION_RATE * starDeltaTime;
        if( s->shipRotation >= 360 ) s->shipRotation = s->shipRotation - 360;
        else if( s->shipRotation < 0 ) s->shipRotation = s->shipRotation + 360;
    }
    else if( starDPad == STAR_DIR_UPRIGHT || starDPad == STAR_DIR_RIGHT || starDPad == STAR_DIR_DOWNRIGHT )
    {
        s->shipRotation = s->shipRotation + STAR_ROTATION_RATE * starDeltaTime;
        if( s->shipRotation >= 360 ) s->shipRotation = s->shipRotation - 360;
        else if( s->shipRotation < 0 ) s->shipRotation = s->shipRotation + 360;
    }

    rotation = (int)( s->shipRotation / 45 );
    s->shipFacing = rotation;
    s->bitmap = starPlayerBitmaps[ s->shipFacing ];

    if( !( starDPad == STAR_DIR_UP || starDPad == STAR_DIR_UPRIGHT || starDPad == STAR_DIR_UPLEFT ) )
    {
        friction = s->velocity;
        starVecScale( &friction, -1.0 * s->shipFriction * starDeltaTime, &friction );
        starVecAddInPlace( &s->velocity, &friction );
    }

    thrust.x = 0;
    thrust.y = 0;
    if( starDPad == STAR_DIR_UP || starDPad == STAR_DIR_UPRIGHT || starDPad == STAR_DIR_UPLEFT )
    {
        starShipCalcThrust( s, s->shipFacing, &thrust );
    }
    else if( starDPad == STAR_DIR_DOWN || starDPad == STAR_DIR_DOWNRIGHT || starDPad == STAR_DIR_DOWNLEFT )
    {
        starShipCalcThrust( s, s->shipFacing, &thrust );
        starVecScale( &thrust, -0.4, &thrust );
    }

    starShipUpdateMovement( s, &thrust );

    starTimeUntilNextRepair = starTimeUntilNextRepair - starDeltaTime;
    if( starTimeUntilNextRepair <= 0 )
    {
        starTimeUntilNextRepair = starRepairTime;
        starShipRepairSystem( s );
    }
}

int starShipUpgrade( StarShip* s, int loot )
{
    int cap = 50;
    int amount = 1;

    if( loot == STAR_LOOT_NONE )
    {
        return 0;
    }
    else if( loot == STAR_LOOT_HULL )
    {
        s->hpHull = s->hpHull + amount;
        s->maxHull = s->maxHull + amount;
        s->hpHull = gbMin( s->hpHull, cap );
        s->maxHull = gbMin( s->maxHull, cap );
        return amount;
    }
    else if( loot == STAR_LOOT_WEAPONS )
    {
        s->hpWeapons = s->hpWeapons + amount;
        s->maxWeapons = s->maxWeapons + amount;
        s->hpWeapons = gbMin( s->hpWeapons, cap );
        s->maxWeapons = gbMin( s->maxWeapons, cap );
        return amount;
    }
    else if( loot == STAR_LOOT_SHIELDS )
    {
        s->hpShields = s->hpShields + amount;
        s->maxShields = s->maxShields + amount;
        s->hpShields = gbMin( s->hpShields, cap );
        s->maxShields = gbMin( s->maxShields, cap );
        return amount;
    }
    else if( loot == STAR_LOOT_ENGINES )
    {
        s->hpEngine = s->hpEngine + amount;
        s->maxEngine = s->maxEngine + amount;
        s->hpEngine = gbMin( s->hpEngine, cap );
        s->maxEngine = gbMin( s->maxEngine, cap );
        s->maxVelocity = s->maxVelocity + amount;
        return amount;
    }
    else if( loot == STAR_LOOT_CREW )
    {
        amount = amount * 8;
        s->crew = s->crew + amount;
        s->maxCrew = s->maxCrew + amount;
        return amount;
    }
    else if( loot == STAR_LOOT_FUEL )
    {
        amount = 1;
        s->fuel = s->fuel + amount;
        return amount;
    }
    return 0;
}

void starShipRepairSystem( StarShip* s )
{
    bool existingUpdate;
    int systemTargeted;

    if( starRepairTarget == STAR_SYS_NONE ) return;

    existingUpdate = starStatusUpdateAvailable;
    systemTargeted = starRepairTarget;

    if( starRepairTarget == STAR_SYS_CREW )
    {
        if( ( s->crew + 1 ) > s->maxCrew )
        {
            s->crew = s->maxCrew;
            starStatusUpdateAvailable = true;
            starRepairTarget = STAR_SYS_NONE;
        }
        else
          s->crew = s->crew + 1;
    }
    else if( starRepairTarget == STAR_SYS_HULL )
    {
        if( ( s->hpHull + 1 ) > s->maxHull )
        {
            s->hpHull = s->maxHull;
            starStatusUpdateAvailable = true;
            starRepairTarget = STAR_SYS_NONE;
        }
        else
          s->hpHull = s->hpHull + 1;
    }
    else if( starRepairTarget == STAR_SYS_WEAPONS )
    {
        if( ( s->hpWeapons + 1 ) > s->maxWeapons )
        {
            s->hpWeapons = s->maxWeapons;
            starStatusUpdateAvailable = true;
            starRepairTarget = STAR_SYS_NONE;
        }
        else
          s->hpWeapons = s->hpWeapons + 1;
    }
    else if( starRepairTarget == STAR_SYS_ENGINES )
    {
        if( ( s->hpEngine + 1 ) > s->maxEngine )
        {
            s->hpEngine = s->maxEngine;
            starStatusUpdateAvailable = true;
            starRepairTarget = STAR_SYS_NONE;
        }
        else
          s->hpEngine = s->hpEngine + 1;
    }
    else if( starRepairTarget == STAR_SYS_SHIELDS )
    {
        if( ( s->hpShields + 1 ) > s->maxShields )
        {
            s->hpShields = s->maxShields;
            starStatusUpdateAvailable = true;
            starRepairTarget = STAR_SYS_NONE;
        }
        else
          s->hpShields = s->hpShields + 1;
    }

    if( !existingUpdate && starStatusUpdateAvailable )
    {
        starStatusUpdateTime = 240;
        starStatusUpdate = starRepairedText[ systemTargeted ];
    }
}

void starShipCalculateBattleRepairs( StarShip* s )
{
    starBattleRepairsMax = gbMin( s->crew / 10 - 2, 5 );
    starBattleRepairs = starBattleRepairsMax;
}

// -----------------------------------------------------------------------------
//   Map / Planetoid logic
// -----------------------------------------------------------------------------

void starRandomMapPosition( StarVec2* out )
{
    out->x = starRandRange( (int)starMapUpperBounds.x + 16, (int)starMapLowerBounds.x - 16 );
    out->y = starRandRange( (int)starMapUpperBounds.y + 16, (int)starMapLowerBounds.y - 16 );
}

void starNewMap()
{
    int i;
    int fuel = 3;
    int goodPlanets = 3; // real upstream: (int)(PlanetsPerMap/3.0f) = (int)(10/3.0) = 3
    StarVec2 randPos;

    for( i = 0; i < 10; i = i + 1 )
    {
        starRandomMapPosition( &randPos );
        starPlanets[i].mapPosition.x = randPos.x;
        starPlanets[i].mapPosition.y = randPos.y;

        starPlanets[i].bitmap = starPlanetArt[ starRandRange( 0, 10 ) ];

        if( goodPlanets > 0 )
        {
            starPlanets[i].alignment = goodPlanets;
            goodPlanets = goodPlanets - 1;
        }
        else
        {
            starPlanets[i].alignment = starRandRange( -3, 2 );
        }

        starPlanets[i].attack = starCurrentSector + starRandRange( 1, 3 ) + 3;
        starPlanets[i].defense = starCurrentSector + starRandRange( 3, 5 ) + 3;

        if( fuel > 0 )
        {
            starPlanets[i].prize = STAR_LOOT_FUEL;
            fuel = fuel - 1;
        }
        else
        {
            starPlanets[i].prize = arand( 7 );
        }
        starPlanets[i].contacted = false;
    }
}

void starMapLoop()
{
    if( starAButton )
      starChangeGameState( STAR_STATE_STATUS );

    starDrawMap();
}

void starDrawMap()
{
    int i;
    StarVec2 distance;

    if( starLatestPlanetIdx >= 10 ) starLatestPlanetIdx = -1; // defensive, never actually reachable

    for( i = 0; i < 10; i = i + 1 )
    {
        starVecSub( &starPlanets[i].mapPosition, &starPlayerShip.mapPosition, &distance );

        if( fabs( distance.x ) < 58.0 && fabs( distance.y ) < 40.0 )
        {
            gbDrawBitmap( (int)( distance.x + 36.0 ), (int)( distance.y + 18.0 ), starPlanets[i].bitmap );

            if( starVecMagSq( &distance ) < 256 && starLatestPlanetIdx == -1 && !starPlanets[i].contacted )
            {
                starCanHail = true;
                starLatestPlanetIdx = i;
            }
        }

        if( starLatestPlanetIdx != -1 && i == starLatestPlanetIdx )
        {
            if( starPlanets[i].contacted || starVecMagSq( &distance ) > 256 )
            {
                starCanHail = false;
                starLatestPlanetIdx = -1;
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Input (flattened from real GetInput())
// -----------------------------------------------------------------------------

void starGetInput()
{
    int dpadDirection = STAR_DIR_NONE;

    if( gbRepeat( BTN_UP, 1 ) )
      dpadDirection = STAR_DIR_UP;
    if( gbRepeat( BTN_DOWN, 1 ) )
      dpadDirection = STAR_DIR_DOWN;
    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        if( dpadDirection == STAR_DIR_UP ) dpadDirection = STAR_DIR_UPLEFT;
        else if( dpadDirection == STAR_DIR_DOWN ) dpadDirection = STAR_DIR_DOWNLEFT;
        else dpadDirection = STAR_DIR_LEFT;
    }
    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        if( dpadDirection == STAR_DIR_UP ) dpadDirection = STAR_DIR_UPRIGHT;
        else if( dpadDirection == STAR_DIR_DOWN ) dpadDirection = STAR_DIR_DOWNRIGHT;
        else dpadDirection = STAR_DIR_RIGHT;
    }
    starDPad = dpadDirection;

    starAButton = false;
    starBButton = false;

    if( starNewButtonInputAllowed == false && ( !gbRepeat( BTN_B, 1 ) && !gbRepeat( BTN_A, 1 ) ) )
      starNewButtonInputAllowed = true;

    if( gbRepeat( BTN_A, 1 ) && starNewButtonInputAllowed )
    {
        starNewButtonInputAllowed = false;
        starAButton = true;
    }
    if( gbRepeat( BTN_B, 1 ) && starNewButtonInputAllowed )
    {
        starNewButtonInputAllowed = false;
        starBButton = true;
    }

    // Real upstream's own real `arduboy.buttons.pressed(BTN_C)` ->
    // blocking system `titleScreen(F("Paused"))` call - see this file's
    // own header comment for why this became a real state instead.
    if( gbPressed( BTN_C ) && starGameState != STAR_STATE_PAUSED )
    {
        starPausedFromState = starGameState;
        starGameState = STAR_STATE_PAUSED;
    }
}

// -----------------------------------------------------------------------------
//   State transitions (flattened from real ChangeGameState())
// -----------------------------------------------------------------------------

void starChangeGameState( int newState )
{
    starPreviousGameState = starGameState;

    if( newState == STAR_STATE_TITLELOOP )
    {
        starCurrentSector = 1;
        starTextNewOverTime();
    }
    else if( newState == STAR_STATE_PROLOGUE )
    {
        starSequenceStage = 1;
        starTextNewOverTime();
    }
    else if( newState == STAR_STATE_STATUS )
    {
        starIntToChar( starPlayerShip.crew, starCrewCharArray, 0 );
        starIntToChar( starPlayerShip.maxCrew, starMaxCrewCharArray, 0 );
        starIntToChar( starPlayerShip.fuel, starFuelCharArray, 0 );
    }
    else if( newState == STAR_STATE_ENCOUNTER )
    {
        starSetupEncounter();
    }
    else if( newState == STAR_STATE_GAMEOVER )
    {
        starPlayerShip.isAlive = false;
        starTextNewOverTime();
        starSequenceStage = 1;
    }
    else if( newState == STAR_STATE_TIMEUP )
    {
        starPlayerShip.isAlive = false;
        starTextNewOverTime();
        starSequenceStage = 1;
    }
    else if( newState == STAR_STATE_WARPING )
    {
        starPlayerShip.shipRotation = 90;
        starPlayerShip.velocity.x = 2;
        starPlayerShip.velocity.y = 0;
        starShipPlayerUpdate( &starPlayerShip );
        starPlayerShip.fuel = starPlayerShip.fuel - 3;
        starWaitTime = 240;
    }
    else if( newState == STAR_STATE_WINGAME )
    {
        starSequenceStage = 1;
        starTextNewOverTime();
    }
    // STAR_STATE_MAP / STAR_STATE_RESET: real upstream's own case bodies
    // are empty - nothing to do here either.

    starGameState = newState;
}

// -----------------------------------------------------------------------------
//   Prologue
// -----------------------------------------------------------------------------

void starPrologueLoop()
{
    if( starSequenceStage == 1 )
    {
        if( starTextOverTimeClear( starPrologueText1, -2, -2 ) || starBButton )
        {
            starSequenceStage = starSequenceStage + 1;
            starTextNewOverTime();
        }
    }
    else if( starSequenceStage == 2 )
    {
        if( starTextOverTimeClear( starPrologueText2, -2, 22 ) || starBButton )
          starSequenceStage = starSequenceStage + 1;
        starDisplayTextRaw( starPrologueText1, 0, 0 );
    }
    else if( starSequenceStage == 3 )
    {
        starDisplayTextRaw( starPrologueText1, 0, 0 );
        starDisplayTextRaw( starPrologueText2, 0, 24 );
        if( starBButton )
          starChangeGameState( STAR_STATE_MAP );
    }
}

// -----------------------------------------------------------------------------
//   Ship status screen
// -----------------------------------------------------------------------------

void starDrawShipStatusScreen()
{
    int statusBarX = 56;
    int textXPos = 10;
    int randomRepairs;

    starDisplayTextRaw( starTCrew, textXPos, 0 );
    starDisplayTextRaw( starTHull, textXPos, 6 );
    starDisplayTextRaw( starTWeapons, textXPos, 12 );
    starDisplayTextRaw( starTShields, textXPos, 18 );
    starDisplayTextRaw( starTEngines, textXPos, 24 );
    starDisplayTextRaw( starTFuel, textXPos, 30 );

    if( starRepairTarget != STAR_SYS_NONE && starStatusBlinkTime > 30 )
    {
        gbSetColor( GB_BLACK );
        gbFillRect( textXPos, 0 + 6 * starRepairTarget, 40, 6 );
        gbSetColor( GB_WHITE );
    }
    else if( starStatusBlinkTime < 0 )
    {
        starStatusBlinkTime = 60;
    }
    starStatusBlinkTime = starStatusBlinkTime - 1;

    starDisplayTextRaw( starCrewCharArray, 56, 0 );
    starDisplayTextRaw( starSlashStr, 70, 0 );
    starDisplayTextRaw( starMaxCrewCharArray, 76, 0 );

    gbDrawRoundRect( statusBarX, 6, starPlayerShip.maxHull + 2, 4, 2 );
    gbFillRoundRect( statusBarX, 6, starPlayerShip.hpHull + 2, 4, 2 );
    gbDrawRoundRect( statusBarX, 12, starPlayerShip.maxWeapons + 2, 4, 2 );
    gbFillRoundRect( statusBarX, 12, starPlayerShip.hpWeapons + 2, 4, 2 );
    gbDrawRoundRect( statusBarX, 18, starPlayerShip.maxShields + 2, 4, 2 );
    gbFillRoundRect( statusBarX, 18, starPlayerShip.hpShields + 2, 4, 2 );
    gbDrawRoundRect( statusBarX, 24, starPlayerShip.maxEngine + 2, 4, 2 );
    gbFillRoundRect( statusBarX, 24, starPlayerShip.hpEngine + 2, 4, 2 );

    starDisplayTextRaw( starFuelCharArray, statusBarX, 30 );

    if( starPreviousGameState == STAR_STATE_MAP )
    {
        if( starRepairArrowPos == 0 )
        {
            starDisplayTextRaw( starStatusHelp2, 0, 42 );
            if( starBButton )
              starRepairTarget = starRepairArrowPos;
        }
        if( starRepairArrowPos > 0 && starRepairArrowPos <= 4 )
        {
            starDisplayTextRaw( starStatusHelp, 0, 42 );
            if( starBButton )
              starRepairTarget = starRepairArrowPos;
        }
        else if( starRepairArrowPos == 5 )
        {
            starDisplayTextRaw( starStatusHelp4, 0, 42 );
        }
    }
    else if( starPreviousGameState == STAR_STATE_ENCOUNTER )
    {
        if( starRepairArrowPos > 0 && starRepairArrowPos <= 4 )
        {
            starDisplayTextRaw( starSpendEmergencyRepairs, 0, 36 );
            if( starBButton && starBattleRepairs > 0 )
            {
                starRepairTarget = starRepairArrowPos;
                randomRepairs = starRandRange( 2, 6 );
                while( randomRepairs > 0 )
                {
                    starShipRepairSystem( &starPlayerShip );
                    randomRepairs = randomRepairs - 1;
                }
                starBattleRepairs = starBattleRepairs - 1;
            }
        }

        if( starBattleRepairsMax > 2 ) gbDrawBitmap( 44, 42, starBubbleEmptyBmp );
        if( starBattleRepairsMax > 1 ) gbDrawBitmap( 38, 42, starBubbleEmptyBmp );
        if( starBattleRepairsMax > 0 ) gbDrawBitmap( 32, 42, starBubbleEmptyBmp );

        starDisplayTextRaw( starEmergencyRepairs, 0, 42 );

        if( starBattleRepairs > 2 ) gbDrawBitmap( 44, 42, starBubbleBmp );
        if( starBattleRepairs > 1 ) gbDrawBitmap( 38, 42, starBubbleBmp );
        if( starBattleRepairs > 0 ) gbDrawBitmap( 32, 42, starBubbleBmp );
    }
}

void starShipStatusLoop()
{
    if( starDPad == STAR_DIR_NONE && starAcceptMenuInput == false )
      starAcceptMenuInput = true;

    if( starAcceptMenuInput )
    {
        if( starDPad == STAR_DIR_UP )
        {
            starRepairArrowMoveUp();
            starAcceptMenuInput = false;
        }
        else if( starDPad == STAR_DIR_DOWN )
        {
            starRepairArrowMoveDown();
            starAcceptMenuInput = false;
        }

        if( starAButton )
          starChangeGameState( starPreviousGameState );

        // Real upstream's own `if (BButton) {}` here is a genuine empty
        // block - nothing to port.
    }

    starDrawShipStatusScreen();
    starRepairArrowDraw();
}

// -----------------------------------------------------------------------------
//   Combat / encounters
// -----------------------------------------------------------------------------

void starDrawCombatScreen( bool drawCommands )
{
    gbSetColor( GB_BLACK );
    gbFillRect( 0, 0, 35, 27 );
    gbSetColor( GB_WHITE );
    gbDrawRect( 0, 0, 35, 27 );

    starDisplayTextRaw( starCombatMenuAtk, 2, 7 );
    starIntToChar( starPlayerShip.hpWeapons, starBuffer, 0 );
    starDisplayTextRaw( starBuffer, 22, 7 );

    starDisplayTextRaw( starCombatMenuShld, 2, 13 );
    starIntToChar( starPlayerShip.hpShields, starBuffer, 0 );
    starDisplayTextRaw( starBuffer, 22, 13 );

    starDisplayTextRaw( starCombatMenuHull, 2, 19 );
    starIntToChar( starPlayerShip.hpHull, starBuffer, 0 );
    starDisplayTextRaw( starBuffer, 22, 19 );

    starDisplayTextRaw( starCombatMenuPlayer, 2, 1 );

    gbSetColor( GB_BLACK );
    gbFillRect( 52, 0, 32, 27 );
    gbSetColor( GB_WHITE );
    gbDrawRect( 52, 0, 32, 27 );
    starDisplayTextRaw( starCombatMenuEnemy, 54, 1 );

    starDisplayTextRaw( starCombatMenuAtk, 54, 7 );
    starIntToChar( starPlanets[ starLatestPlanetIdx ].attack, starBuffer, 0 );
    starDisplayTextRaw( starBuffer, 70, 7 );

    starDisplayTextRaw( starCombatMenuDef, 54, 13 );
    starIntToChar( starPlanets[ starLatestPlanetIdx ].defense, starBuffer, 0 );
    starDisplayTextRaw( starBuffer, 70, 13 );

    if( drawCommands )
    {
        if( starDPad == STAR_DIR_NONE && starAcceptMenuInput == false )
          starAcceptMenuInput = true;

        if( starDPad == STAR_DIR_UP && starAcceptMenuInput )
        {
            starCombatArrowMoveUp();
            starAcceptMenuInput = false;
        }
        else if( starDPad == STAR_DIR_DOWN && starAcceptMenuInput )
        {
            starCombatArrowMoveDown();
            starAcceptMenuInput = false;
        }

        starDisplayTextClear( starCmdAtk, 8, 25, false );
        starDisplayTextClear( starCmdRepair, 8, 32, false );
        starDisplayTextClear( starCmdFlee, 8, 39, false );

        starCombatArrowDraw();
        gbDrawRect( 0, 25, 84, 23 );
    }
}

void starGenerateReward( int reward )
{
    int index = 0;
    int upgradeAmount;

    if( reward == 0 )
      index = starCopyIntoBuffer( starDiscoveredNothing, 0, 25 );
    else if( reward > 4 )
      index = starCopyIntoBuffer( starDiscoveredGood, 0, 7 );
    else
      index = starCopyIntoBuffer( starDiscoveredUpgrade, 0, 28 );

    upgradeAmount = starShipUpgrade( &starPlayerShip, reward );

    if( reward == STAR_LOOT_NONE )
    {
    }
    else if( reward == STAR_LOOT_HULL )
      index = index + starCopyIntoBuffer( starTHull, index, 4 );
    else if( reward == STAR_LOOT_WEAPONS )
      index = index + starCopyIntoBuffer( starTWeapons, index, 7 );
    else if( reward == STAR_LOOT_SHIELDS )
      index = index + starCopyIntoBuffer( starTShields, index, 7 );
    else if( reward == STAR_LOOT_ENGINES )
      index = index + starCopyIntoBuffer( starTEngines, index, 7 );
    else if( reward == STAR_LOOT_CREW )
      index = index + starCopyIntoBuffer( starCapturedCrew, index, 13 );
    else if( reward == STAR_LOOT_FUEL )
      index = index + starCopyIntoBuffer( starTFuel, index, 7 );

    starTypeBuffer[index] = 0;
}

void starSetupEncounter()
{
    // For if we visit the status screen from within combat, we don't
    // want to re-set-up the encounter.
    if( starPreviousGameState == STAR_STATE_STATUS ) return;

    starSequenceStage = 1;
    starRunningAway = false;

    if( starPlanets[ starLatestPlanetIdx ].alignment < 0 )
    {
        starCommA1 = starNegativeResponse[ starRandRange( 0, 3 ) ];
    }
    else if( starPlanets[ starLatestPlanetIdx ].alignment == 0 )
    {
        // Real upstream: `random(0,5)` against a real 4-entry
        // `Neutral_Response[]` array - a genuine real out-of-bounds
        // array-index bug (index 4 is one past the last real entry).
        // Fixed to `starRandRange(0,4)` here, since an out-of-bounds
        // read here is a real crash risk on this platform (see this
        // file's own header comment on the similar `DamageReportXxx`
        // fix) - a garbage pointer value read past the array could be
        // dereferenced as text moments later.
        starCommA1 = starNeutralResponse[ starRandRange( 0, 4 ) ];
    }
    else if( starPlanets[ starLatestPlanetIdx ].alignment > 0 )
    {
        starCommA1 = starPositiveResponse[ starRandRange( 0, 3 ) ];
    }

    starCmdAtk = starEngageCombat[ starRandRange( 0, 3 ) ];
    starCmdRepair = starRepairCombat[ starRandRange( 0, 3 ) ];
    starCmdFlee = starFleeCombat[ starRandRange( 0, 3 ) ];

    starShipCalculateBattleRepairs( &starPlayerShip );
}

void starEncounterUpdate()
{
    int index;
    int nextSequence;

    index = 0;
    nextSequence = starSequenceStage;

    if( starSequenceStage == 1 )
    {
        if( starTextOverTime( starCommA1, 0, 0 ) && starBButton )
        {
            starTextNewOverTime();

            if( starPlanets[ starLatestPlanetIdx ].alignment < 0 )
              nextSequence = 3;
            else if( starPlanets[ starLatestPlanetIdx ].alignment == 0 )
              nextSequence = 7;
            else
              nextSequence = 8;
        }
    }
    else if( starSequenceStage == 3 )
    {
        starDrawCombatScreen( true );

        if( starBButton )
          nextSequence = 4 + starCombatArrowPos;
    }
    else if( starSequenceStage == 4 )
    {
        starDrawCombatScreen( false );
        index = 0;

        starCombatPlanetDamage = starPlayerShip.hpWeapons + starRandRange(
            (int)( -( starPlayerShip.hpWeapons * 0.2 ) ), (int)( starPlayerShip.hpWeapons * 0.2 ) );
        starCombatPlanetDamage = gbMax( starCombatPlanetDamage, 1 );
        starCombatShipShieldDamage = starPlanets[ starLatestPlanetIdx ].attack;
        starCombatShipShieldDamage = gbMax( starCombatShipShieldDamage, 1 );

        starCombatPlanetDef = starPlanets[ starLatestPlanetIdx ].defense - starCombatPlanetDamage;

        if( starCombatPlanetDef <= 0 && starRunningAway == false )
        {
            // Real upstream's own equivalent `switch` case has a `break;`
            // right here, exiting the case entirely and skipping the
            // shield-damage calc + `nextSequence = 9` below - the only way
            // `nextSequence = 15` (victory) actually sticks. This dialect
            // has no `switch`, so the whole rest of this case's body (the
            // shield-damage block AND the two lines that follow it) is
            // nested inside this `if`'s own `else` instead, to reproduce
            // that early exit exactly.
            starCombatResult = starVictory[ starRandRange( 0, 2 ) ];
            starTextNewOverTime();
            nextSequence = 15;
        }
        else
        {
            if( !starRunningAway )
            {
                index = index + starCopyIntoBuffer( starCombatTakeDamage4, index, 12 );
                starIntToChar( starCombatPlanetDamage, starTypeBuffer, index );
                index = index + 1 + starDigitExtra( starCombatPlanetDamage );
                index = index + starCopyIntoBuffer( starCombatTakeDamage2, index, 9 );
            }

            if( starCombatShipShieldDamage >= starPlayerShip.hpShields )
            {
                starCombatShipDamage = starCombatShipShieldDamage - starPlayerShip.hpShields;

                index = index + starCopyIntoBuffer( starShieldsDown, index, 18 );
                index = index + starCopyIntoBuffer( starCombatTakeDamage1, index, 12 );
                starIntToChar( starCombatShipDamage, starTypeBuffer, index );
                index = index + 1 + starDigitExtra( starCombatShipDamage );
                index = index + starCopyIntoBuffer( starCombatTakeDamage2, index, 9 );

                starSystemDamaged = starShipTakeDamageRandom( &starPlayerShip, 0 );

                if( starSystemDamaged == STAR_SYS_CREW )
                  index = index + starCopyIntoBuffer( starDamageReportCrew, index, 19 );
                else if( starSystemDamaged == STAR_SYS_HULL )
                  index = index + starCopyIntoBuffer( starDamageReportHull, index, 12 );
                else if( starSystemDamaged == STAR_SYS_WEAPONS )
                  index = index + starCopyIntoBuffer( starDamageReportWeapons, index, 12 );
                else if( starSystemDamaged == STAR_SYS_SHIELDS )
                  index = index + starCopyIntoBuffer( starDamageReportShields, index, 18 );
                else if( starSystemDamaged == STAR_SYS_ENGINES )
                  index = index + starCopyIntoBuffer( starDamageReportEngines, index, 20 );

                starTypeBuffer[index] = 0;
            }
            else
            {
                index = index + starCopyIntoBuffer( starCombatTakeDamage3, index, 17 );
                starIntToChar( starCombatShipShieldDamage, starTypeBuffer, index );

                // Real upstream bug, preserved exactly - see this file's
                // own header comment: the real digit-count advance here
                // uses `CombatShipDamage`, not `CombatShipShieldDamage`
                // (the value that was actually just printed).
                index = index + 1 + starDigitExtra( starCombatShipDamage );
                index = index + starCopyIntoBuffer( starCombatTakeDamage2, index, 9 );
                index = index + starCopyIntoBuffer( starShieldsHolding, index, 18 );
                starTypeBuffer[index] = 0;
            }

            starMenuWaitTime = 15;
            nextSequence = 9;
        }
    }
    else if( starSequenceStage == 5 )
    {
        nextSequence = 3;
        starChangeGameState( STAR_STATE_STATUS );
    }
    else if( starSequenceStage == 6 )
    {
        nextSequence = 4;
        starRunningAway = true;
    }
    else if( starSequenceStage == 7 )
    {
        starPlanets[ starLatestPlanetIdx ].contacted = true;
        starCanHail = false;
        starChangeGameState( STAR_STATE_MAP );
    }
    else if( starSequenceStage == 8 )
    {
        starPlanets[ starLatestPlanetIdx ].contacted = true;
        starGenerateReward( starPlanets[ starLatestPlanetIdx ].prize );
        starTextNewOverTime();
        nextSequence = 17;
    }
    else if( starSequenceStage == 9 )
    {
        starDrawCombatScreen( false );
        starDisplayTextClear( starTypeBuffer, 0, 21, true );
        starMenuWaitTime = starMenuWaitTime - 1;
        if( starMenuWaitTime <= 0 && starBButton )
        {
            starPlanets[ starLatestPlanetIdx ].defense = starCombatPlanetDef;
            starPlayerShip.hpShields = gbMax( starPlayerShip.hpShields - starCombatShipShieldDamage, 0 );
            if( starPlayerShip.hpShields <= 0 )
              starShipTakeDamageTarget( &starPlayerShip, starCombatShipDamage, starSystemDamaged );

            if( starPlayerShip.hpHull <= 0 )
            {
                nextSequence = 18;
            }
            else if( !starRunningAway )
              nextSequence = 3;
            else
              nextSequence = 16;
        }
    }
    else if( starSequenceStage == 15 )
    {
        if( starTextOverTime( starCombatResult, 0, 30 ) )
        {
            if( starBButton )
            {
                starGenerateReward( starPlanets[ starLatestPlanetIdx ].prize );
                starPlanets[ starLatestPlanetIdx ].contacted = true;
                starTextNewOverTime();
                nextSequence = 17;
            }
        }
    }
    else if( starSequenceStage == 16 )
    {
        starChangeGameState( STAR_STATE_MAP );
    }
    else if( starSequenceStage == 17 )
    {
        starDisplayTextClear( starTypeBuffer, 0, 30, true );
        if( starBButton )
        {
            starCanHail = false;
            starChangeGameState( STAR_STATE_MAP );
        }
    }
    else if( starSequenceStage == 18 )
    {
        starChangeGameState( STAR_STATE_GAMEOVER );
        return;
    }

    starSequenceStage = nextSequence;
}

// -----------------------------------------------------------------------------
//   Reset / sector progression / win / game-over
// -----------------------------------------------------------------------------

void starResetPlayer()
{
    starShipSetup( &starPlayerShip );
    starTimeUntilNextRepair = starRepairTime;
}

void starNextSector()
{
    starCurrentSector = starCurrentSector + 1;

    if( starCurrentSector >= 7 )
    {
        starChangeGameState( STAR_STATE_WINGAME );
        return;
    }
    starLatestPlanetIdx = -1;
    starCanHail = false;

    starNewMap();
    starPlayerShip.mapPosition.x = 0;
    starPlayerShip.mapPosition.y = 0;
    starPlayerShip.shipRotation = 90;
}

void starClockUpdate( bool running )
{
    if( running )
      starSeconds = starSeconds - ( starDeltaTime * 1.0 );

    if( starSeconds < 0 )
    {
        if( starMinutes == 0 )
          starChangeGameState( STAR_STATE_TIMEUP );

        starMinutes = starMinutes - 1;
        starSeconds = 59.99;
    }

    starIntToChar( starMinutes, starClockBuffer, 0 );
    starClockBuffer[1] = ':';
    if( starSeconds < 10 )
    {
        starClockBuffer[2] = '0';
        starIntToChar( (int)starSeconds, starClockBuffer, 3 );
    }
    else
      starIntToChar( (int)starSeconds, starClockBuffer, 2 );
    starClockBuffer[4] = 0;

    starDisplayTextRaw( starClockBuffer, 84 - 16, 34 );
}

void starSetupSectorReachedText()
{
    starStatusUpdateTime = 240;
    starStatusUpdateAvailable = true;

    starCopyIntoBuffer( starSectorReachedA, 0, 16 );
    starIntToChar( 60 + starRandRange( 1, 10 ) - ( starCurrentSector * 10 ), starTypeBuffer, 16 );
    starTypeBuffer[18] = 0;
    starStatusUpdate = starTypeBuffer;
}

void starWinGameLoop()
{
    bool done = false;

    if( starSequenceStage < 4 )
    {
        gbDrawBitmap( 68, 0, starPlanetHomeBmp );
        gbDrawBitmap( 36, 18, starPlayerBitmaps[2] );
        starClockUpdate( false );
    }

    if( starSequenceStage == 1 )
      done = starTextOverTime( starGameWin1, 0, 2 ) && starBButton;
    else if( starSequenceStage == 2 )
      done = starTextOverTime( starGameWin2, 0, 2 );
    else if( starSequenceStage == 3 )
      done = starTextOverTime( starGameWin3, 0, 2 );
    else if( starSequenceStage == 4 )
      done = starTextOverTime( starGameWin4, 0, 2 );

    if( done && starBButton )
    {
        starTextNewOverTime();
        starSequenceStage = starSequenceStage + 1;
        if( starSequenceStage == 5 )
          starChangeGameState( STAR_STATE_RESET );
    }
}

void starGameOverLoop( int ending )
{
    bool done = false;

    if( ending == 1 )
    {
        if( starSequenceStage == 1 )
          done = starTextOverTime( starDefeatA, 0, 32 );
        else if( starSequenceStage == 2 )
          done = starTextOverTime( starDefeatB, 0, 32 );
        else if( starSequenceStage == 3 )
          done = starTextOverTime( starDefeatC, 0, 32 );
        else if( starSequenceStage == 4 )
          starChangeGameState( STAR_STATE_RESET );
    }
    else if( ending == 2 )
    {
        if( starSequenceStage == 1 )
          done = starTextOverTime( starDefeatD, 0, 20 );
        else if( starSequenceStage == 2 )
          done = starTextOverTime( starDefeatE, 0, 20 );
        else if( starSequenceStage == 3 )
          starChangeGameState( STAR_STATE_RESET );
    }

    if( done && starBButton )
    {
        starTextNewOverTime();
        starSequenceStage = starSequenceStage + 1;
    }
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameStarHonor_init()
{
    gbBegin();
    gbPickRandomSeed();

    starMapUpperBounds.x = -32;
    starMapUpperBounds.y = -128;
    starMapLowerBounds.x = 512;
    starMapLowerBounds.y = 128;

    starFieldInit();

    starTextOverTimeRunning = false;
    starFramesToNextChar = 3;
    starCharIndex = 0;

    starRepairArrowPos = 0;
    starCombatArrowPos = 0;

    starLatestPlanetIdx = -1;

    starChangeGameState( STAR_STATE_TITLELOOP );
    starMenuWaitTime = 15;

    // Real upstream's own comment: "Order is important to prevent Heap
    // Fragmentation" - irrelevant here (no real heap fragmentation risk,
    // `starPlayerShip` is a plain global, not `new`'d), kept in the same
    // relative order anyway for a faithful read-through.
    starShipSetup( &starPlayerShip );
    starTimeUntilNextRepair = starRepairTime;
    starNewMap();
}

void gameStarHonor_update()
{
    StarVec2 negVel;
    StarVec2 warpV;

    if( !gbUpdate() ) return;

    // Real upstream's own real "why do so many innocuous functions make
    // it CRASH" fillRect-black-then-back-to-white dance - this IS the
    // real intentional space background fill (not a redundant clear;
    // this shim's own gbUpdate()/gbClear() clears to WHITE, matching
    // real hardware, so BLACK has to be painted back in explicitly here).
    gbSetColor( GB_BLACK );
    gbFillRect( 0, 0, 84, 48 );
    gbSetColor( GB_WHITE );

    starDeltaTime = 0.05;
    starGetInput();

    if( starGameState == STAR_STATE_PAUSED )
    {
        starDisplayTextRaw( starPaused, 30, 20 );
        gbCursorX = 18;
        gbCursorY = 30;
        gbPrintString( "PRESS A" );
        if( gbPressed( BTN_A ) )
          starGameState = starPausedFromState;
    }
    else if( starGameState == STAR_STATE_TITLELOOP )
    {
        starFieldDraw();
        starDisplayTextClear( starTitleScreen2, 0, 40, false );
        if( starTextOverTime( starTitleScreen, 84 / 2 - 24, 48 / 2 - 8 ) && starBButton )
          starChangeGameState( STAR_STATE_PROLOGUE );
    }
    else if( starGameState == STAR_STATE_PROLOGUE )
    {
        starPrologueLoop();
    }
    else if( starGameState == STAR_STATE_MAP )
    {
        starShipPlayerUpdate( &starPlayerShip );
        starShipDrawOnMap( &starPlayerShip );

        starVecScale( &starPlayerShip.velocity, -1.0, &negVel );
        starFieldMove( &negVel );
        starFieldDraw();

        starMapLoop();

        if( starStatusUpdateAvailable )
        {
            starDisplayTextClear( starStatusUpdate, 0, 6, true );
            starStatusUpdateTime = starStatusUpdateTime - 1;
            starStatusUpdateAvailable = starStatusUpdateTime > 0;
        }
        starClockUpdate( true );

        if( starCanHail )
        {
            starDisplayTextClear( starHail, 0, 40, false );
            if( starBButton )
              starChangeGameState( STAR_STATE_ENCOUNTER );
        }
        else if( starPlayerShip.fuel >= 3 )
        {
            starDisplayTextRaw( starStatusHelp3, 0, 40 );
            if( starDPad == STAR_DIR_UP && starBButton )
              starChangeGameState( STAR_STATE_WARPING );
        }
    }
    else if( starGameState == STAR_STATE_STATUS )
    {
        starShipStatusLoop();
    }
    else if( starGameState == STAR_STATE_ENCOUNTER )
    {
        starFieldDraw();
        starShipDrawOnMap( &starPlayerShip );
        starDrawMap();
        starEncounterUpdate();
    }
    else if( starGameState == STAR_STATE_GAMEOVER )
    {
        starGameOverLoop( 1 );
    }
    else if( starGameState == STAR_STATE_TIMEUP )
    {
        starGameOverLoop( 2 );
    }
    else if( starGameState == STAR_STATE_RESET )
    {
        starStatusUpdateAvailable = false;
        starStatusUpdateTime = 0;
        starCanHail = false;
        starLatestPlanetIdx = -1;
        starResetPlayer();
        starNewMap();
        starChangeGameState( STAR_STATE_TITLELOOP );
        starMinutes = 9;
        starSeconds = 59.99;
    }
    else if( starGameState == STAR_STATE_WARPING )
    {
        warpV.x = -60;
        warpV.y = 0;
        starFieldMove( &warpV );
        starFieldDraw();
        starShipDrawOnMap( &starPlayerShip );
        starWaitTime = starWaitTime - 1;
        if( starWaitTime <= 0 )
        {
            starNextSector();
            if( starGameState != STAR_STATE_WINGAME )
            {
                starChangeGameState( STAR_STATE_MAP );
                starSetupSectorReachedText();
            }
        }
    }
    else if( starGameState == STAR_STATE_WINGAME )
    {
        starWinGameLoop();
    }

    gbRenderFrame();
}
