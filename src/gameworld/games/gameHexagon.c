#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// microHexagon (valdenthoranar, license: none specified -
// bitbucket.org/valdenthoranar/microhexagon). A Super Hexagon-style
// obstacle-avoidance game: a small hexagon spins at screen center while
// walls (trapezoidal wedge segments cut from the hexagon's own 6 "lanes")
// scroll inward from the edge of the screen toward it; the player orbits
// the hexagon's perimeter (not the screen) trying to slip through the
// gaps as the walls close in, for as long as possible - the on-screen
// "TIME" readout is the score. Three .ino tabs in upstream's own real
// source (microHex.ino/player.ino/walls.ino, all one shared translation
// unit on real Arduino, exactly like this project's own single-file
// requirement) were read in full and merged into this one file.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (see gamePong.c's own header comment for
// why - this dialect has no classes/methods). Every global got a
// `hex`-prefixed name (no linker - every game in this cartridge shares
// one flat namespace). `random(a,b)`/`random(n)` became
// `arand(b-a)`/`arand(n)`. `char`/`byte` locals/fields all became plain
// `int` (no genuine boolean use among them - unlike gameAgaruino.c's own
// `byte`-as-boolean case). Upstream's own ternary operators (`a?b:c`,
// used twice: `random(0,2)==0 ? 1:-1` in the rotation-speed randomizer,
// and the wall-width clamp in drawWalls()) were rewritten as if/else -
// this dialect has no ternary. Upstream never calls `gb.titleScreen()`
// at all (that call is commented out in setup()) - it already implements
// its own hand-rolled MENU/GAME/GAMEOVER state machine
// (`enum GameStates`/`gmStates[]` function-pointer dispatch table), so
// there was no "blocking title screen -> explicit state" conversion
// needed here the way gamePong.c/gameConduit.c needed; ported directly as
// `HEX_STATE_MENU/PLAY/GAMEOVER` + a plain if/else dispatch in
// `gameHexagon_update()` (matching gameAgaruino.c's own established
// convention of using if/else rather than a function-pointer table, even
// though this dialect's function pointers do work - see
// VIRCON32_C_DIALECT.md - for consistency with every other game in this
// project). `gb.pickRandomSeed()` is never called upstream, so there is
// nothing to port there. `gb.battery.show = false;` (setup()/playInit())
// was dropped outright, matching gamePong.c's own precedent - purely
// cosmetic on real hardware. `Serial.begin()`/the one unconditional
// `Serial.print(F("ADDING PATTERN \n"));` debug line inside upstream's
// own `updateWalls()` were dropped (no serial monitor on Vircon32,
// harmless either way). The `#ifdef DEBUG ... gb.getCpuLoad() ...`
// block in `mainGame()` was dropped too - `#define DEBUG` is commented
// out upstream, so that whole block is genuinely dead code on real
// hardware as shipped, not just here.
//
// FUNCTION ORDERING NOTE: this dialect compiles top-to-bottom in one pass
// with no forward-declared function prototypes proven anywhere else in
// this project (checked - no other ported game uses one), so every
// function below had to be reordered from upstream's own tab order to
// guarantee each is fully defined before its first call site (e.g.
// `hexInitGameOver()` had to move ahead of `hexUpdatePlayer()`, and the
// wall-pattern functions ahead of the menu/play/game-over state
// transitions that call them) - purely a file-layout concern, no
// behavioral change.
//
// STATE/DATA FLATTENING: upstream's own `_pattern`/`wall` PROGMEM-backed
// struct-copy machinery (`storePattern()` pgm_read_*-copies `wall_num`/
// `distance`/`length`/`walls` out of a constant template into one of 4
// live RAM slots) exists on real AVR only to move data out of a separate
// flash address space - meaningless on Vircon32, which has one flat
// address space (PROGMEM/pgm_read_* are no-ops here - see avrCompat.h).
// Ported as a plain small-int "which of the 6 templates is this live
// slot showing right now" index (`hexPatTemplate[4]`, -1 = none) instead
// of a live copy: `wall_num`/`length`/the wall array itself are read
// directly from the constant template tables every frame by index
// (`hexPatWallCount()`/`hexPatLength()`/`hexTemplateWallLane/Distance/
// Width()`) rather than copied at spawn time. Verified this changes
// nothing observable: `storePattern()` itself never copies `rotation`/
// `mirror` anyway (`addPatern()`/`initWalls()` always set those
// separately, immediately after/instead of calling it), so the only
// genuine per-instance state a live slot ever needs is exactly
// `distance`/`rotation`/`mirror` - precisely what `hexPatDistance[4]`/
// `hexPatRotation[4]`/`hexPatMirror[4]` store here.
//
// A genuine upstream data quirk found while transcribing the wall
// pattern tables (`wallsData.h`): pattern 4's own real wall array
// (`patt4[]`, the "Zig zag" pattern) actually contains 12 real wall
// entries, but its own `_pattern` descriptor declares `wall_num = 10` -
// only the first 10 are ever iterated by any real loop (`for (i=0;
// i<wall_num; i++)`), so the array's own last two entries are genuinely
// dead, unreachable data on real hardware too. Reproduced exactly:
// `hexPatt4[]` below still declares all 12 real entries (byte-for-byte
// transcription of the real table) but `hexWallCount[3]` (pattern 4's
// slot) is 10, matching upstream's own real `wall_num` field - the last
// two entries are dead here as well, on purpose, not a transcription
// error.
//
// A second real, preserved bug, in upstream's own `updateRoationSpeed()`
// (its own name typo, not reproduced in this port's own clean
// `hexUpdateRotationSpeed()` identifier - only *behavior* bugs are
// preserved, not misspelled identifiers): the speed-cap guard reads
// `abs(rot_speed <SPEED_CAP)` - parentheses around the wrong
// sub-expression. `abs()` ends up applied to the 0/1 *result* of
// `rot_speed < SPEED_CAP`, not to `rot_speed` itself - and `abs()` of a
// 0/1 value is a no-op (abs(0)==0, abs(1)==1), so this is mathematically
// identical to plain `rot_speed < SPEED_CAP` with no absolute value ever
// taken. The probably-intended check was `fabs(rot_speed) < SPEED_CAP`
// (an actual speed-*magnitude* cap); as written, a very negative
// `rot_speed` is NOT capped by this test at all. Preserved exactly below
// (written as the plain comparison it actually reduces to, since the
// `abs()` wrapper changes nothing) - not "fixed" to the probably-intended
// magnitude check. A few lines later, a *different*, genuinely correct
// `abs(rot_speed)` call (choosing the new speed's magnitude before
// re-applying a random sign) is ported as real `fabs(rot_speed)` (a
// proper float absolute value, unlike the buggy comparison above) since
// that one was never wrong to begin with.
//
// A third real, preserved bug, in `updatePump()`: `pump_delay == 0;` (a
// comparison, not the clearly-intended assignment `pump_delay = 0;`) is
// a no-op statement - real, observable consequence: `pumpDelay` is never
// actually reset after the pump's first "pulse" fires, so
// `pumpDelay > PUMP_DELAY` latches true forever from that point on; every
// later time `hexPump` decays back down to exactly 1, it immediately
// re-pulses to `HEX_MAX_PUMP` with none of the initial ~16-frame pause
// repeating. Preserved literally as a bare comparison-statement below.
//
// A fourth, harmless dead-code observation (not a bug, just documented):
// `mainGame()`'s own `if (GameState != GAMEOVER) { ... }` guard is
// provably always true whenever `mainGame()` itself actually runs, since
// the top-level `gmStates[]` dispatcher only ever calls `mainGame()`
// while `GameState == GAME` in the first place (a mid-tick transition to
// GAMEOVER, via `updatePlayer()`'s own `initGameOver()` call partway
// through this same block, doesn't retroactively skip the rest of the
// block that tick either). Preserved as a literal `if` below anyway (it
// costs nothing and matches upstream line-for-line), just noted here so
// it doesn't look like an oversight.
//
// SPRINTF: this dialect has no variadics, so `sprintf(b,"%03d",...)`
// (used upstream for every "TIME"/"BEST"/highscore 3-digit readout) has
// no equivalent. Replaced with a small local `hexFormatScore()` helper
// that computes the hundreds/tens/units digits directly into a
// 0-terminated `int[4]` buffer `gbPrintString()` can print - including
// porting the real optional "blank the leading zero digit(s) with a
// space" cosmetic upstream applies on the HUD (`drawHud()`) and menu
// (`drawMenu()`) 3-digit readouts. A genuine, preserved upstream
// *inconsistency* found while porting this: the exact same
// leading-zero-blanking snippet is present in `drawGameOver()`'s own
// TIME/BEST readouts too, but wrapped in a `/* ... */` block comment
// there - i.e. genuinely dead/disabled on real hardware, unlike the HUD/
// menu copies. So the game-over screen's own TIME/BEST always show a
// full zero-padded "007"-style reading while the HUD and menu blank
// leading zeros to "  7" - preserved exactly as this real inconsistency,
// not normalized to match either one.
//
// ICON GLYPHS: upstream's own `print(F("\25PLAY"))`/`F("\27QUIT")`/
// `F("\26Restart"))`/`F("\27Menu")` embed real Gamebuino font5x7 octal-
// escape icon glyphs (`\25`/`\26`/`\27` = decimal 21/22/23, low-ASCII
// custom icon glyphs real hardware's own font ships in place of the
// usual unprintable control-character range - see gamebuinoShim.c's own
// Font tables comment). Ported as small explicit 0-terminated `int[]`
// arrays with the icon code spelled out as a plain decimal literal
// followed by the rest of the text's own plain ASCII codes
// (`hexPlayText`/`hexQuitText`/`hexRestartText`/`hexMenuText` below) -
// the exact same "a bare quoted string literal can't hold a
// non-printable low-ASCII code" workaround already established by
// gamePunkt.c/gameUfoRace.c/gameSnakeAbc.c for this identical situation.
//
// REAL BITMAP ART RESTORED: all 3 of upstream's real
// `const byte NAME[] PROGMEM = {width,height,...}` bitmaps (`microHex`,
// the menu-screen logo; `hscr`, a small highscore-icon glyph; `gmover`,
// the "GAME OVER"-style banner text bitmap) were copied byte-for-byte
// into plain `int[N] name = {width,height,byte0,...}` arrays below (this
// dialect's own `int[N] name` array-declaration order - see gameConduit.c/
// gameFlappyBirdo.c's own established precedent) - already exactly the
// row-major/MSB-first/`ceil(width/8)`-bytes-per-row format
// `gbDrawBitmap()` expects (independently re-verified here by recounting
// each array's own real element count against its own declared
// width/height before trusting it, the same discipline gameUfoRace.c's
// own header comment describes), so no bit-repacking was needed, only
// the already-valid `0x`-hex literal syntax carried over unchanged.
// `hexDrawMenu()`/`hexDrawGameOver()` draw `hexBitmapMicroHex`/
// `hexBitmapGmover` at upstream's own real (x,y) anchors. MASK CHECK (see
// this project's own established "check for a mask/fill-under-bitmap bug"
// discipline from gameFlappyBirdo.c/gameParachute.c): none of these 3
// bitmaps has a separate upstream `*Mask` array, and none needs one -
// each is a small, fully self-contained opaque icon/logo/banner drawn
// once on top of a screen area upstream itself first clears with a plain
// `setColor(WHITE); fillRect(...)` (ported as `gbSetColor(0)`/
// `gbFillRect(...)`) immediately beforehand, not drawn over live
// busy gameplay pixels needing a real silhouette mask underneath.
// Upstream's own real two-arg `setColor(BLACK,WHITE)` calls around these
// bitmap+text groups became `gbSetColorBg(1,0)` - real Display::
// drawBitmap() itself only ever reads the foreground half either way
// (see gamebuinoShim.h's own header comment on gbSetColorBg()), so this
// only actually affects the opaque-background text prints that follow
// each bitmap in the same upstream block, exactly matching real hardware.
//
// `drawHexagon()`'s own dimmer "connecting spoke" lines from the small
// pump hexagon out to the full-size one use real `GRAY` (`gbSetColor(GB_GRAY)`
// - the shim's own dithered checkerboard color).
//
// NOTES ON REAL-HARDWARE RENDERING QUIRKS, AND THE ONE REAL ARCHITECTURE
// GAP THIS PORT HIT (flagging per this port's own instructions, not
// silently working around and moving on):
// 1. upstream's own `drawWalls()` genuinely needs a filled-triangle
//    primitive (`DRAW_MODE` is hardcoded to `1`, the real filled-triangle
//    path - `DRAW_MODE 0`, an alternate wireframe-only path built from
//    plain `drawLine()` calls, is real upstream code but is dead/
//    unreachable as shipped, since the `#if (DRAW_MODE == 0)` branch never
//    compiles with `DRAW_MODE` fixed at `1` - not ported here either,
//    matching real observed behavior). Filled wall wedges are this game's
//    own core visual identity, so every wall is drawn with the real
//    `gbFillTriangle()` shim primitive (a direct port of real
//    `Display::fillTriangle()`'s own scanline algorithm).
// 2. upstream's own `drawGameOver()` calls `gb.display.fillScreen(INVERT);`
//    for a brief flashing effect right when the round ends (the first
//    6-ish frames). Reading real `Display::fillScreen()` directly
//    (`utility/Display.cpp`) found a real hardware bug: its `color`
//    parameter is entirely unused - the function always fills solid black
//    regardless of what's passed, `INVERT` included. So upstream's own
//    "invert flash" never actually inverts anything on real hardware
//    either - it just flashes solid black for those frames.
//    `gbFillScreen()` matches this real bug exactly (its color argument is
//    ignored, always filling 0xFF), so this is ported as a plain
//    `gbFillScreen(1)` call at the same call site - correct, not a
//    downgrade, since that's what real hardware genuinely does here.
// 3. **No per-game "return to the cartridge's own top-level menu"
//    primitive is exposed to a game's own code at all** (only the
//    global Start-button quit-confirmation dialog, wired entirely inside
//    portVircon32.c, outside any individual game file, can do this - see
//    `currentGameIndex` in that file, never touched by any already-ported
//    game). Upstream's own internal-menu screen calls real
//    `gb.changeGame()` on a Button C press specifically to leave this
//    game for real hardware's own multi-program SD-card game-switcher.
//    This is an architecture gap, not a small missing primitive one
//    function could paper over (reaching into portVircon32.c's own
//    `currentGameIndex` from this file, which no other ported game does,
//    was considered and rejected as against this project's own
//    established grain) - so `hexMenuControls()` below simply doesn't
//    wire Button C to anything on the internal menu screen. The real
//    on-screen "QUIT" text prompt is left drawn exactly as upstream drew
//    it (real, unmodified screen layout) even though the button behind
//    it is now a no-op - leaving the cartridge for the shared menu is
//    still fully reachable the same way every other ported game reaches
//    it, via Start's own global quit-confirmation dialog.
//
// EEPROM: upstream keeps one real single-entry highscore table (`pos` is
// always called as `0` at every real call site - the multi-slot `pos`
// parameter is real, general upstream code, just never exercised beyond
// one slot in practice) at fixed byte offsets, gated by a real 2-byte
// `0x02FF` magic-number validity check. Ported directly:
// `EEPROM.read()/write()` -> `eeprom_read_byte()/eeprom_write_byte()`
// (this shim's own primitives already provide independent per-game
// address-space isolation plus their own magic/checksum corruption
// detection underneath - see eepromShim.c - so upstream's own real
// magic-number check here is redundant-but-harmless belt-and-suspenders,
// kept for fidelity rather than removed). Upstream's own declared-but-
// never-actually-called `readHSCName()` prototype (dead code - nothing
// upstream ever reads the stored name back) is not ported; nothing calls
// it on real hardware either.
//
// MATH: `sin`/`cos`/`floor` port unchanged (`math.h` is included once,
// globally, by main.c ahead of every game file - confirmed real
// functions per VIRCON32_C_DIALECT.md, matching gameUfoRace.c's own
// established precedent for real trig). A local `HEX_PI` constant is
// used for the real `PI` this game needs (hexagon vertex rotation, angle
// wraparound) rather than math.h's own documented `pi` symbol - no other
// game ported into this project so far has actually exercised that exact
// symbol name, so this avoids betting on it sight-unseen; a plain literal
// float constant carries zero risk either way.

#define HEX_CENTER_X 42
#define HEX_CENTER_Y 24
#define HEX_SIDES 6
#define HEX_SMALL_HEX_PERCENT 0.12
#define HEX_LINE_LENGTH 50
#define HEX_PI 3.14159265

#define HEX_PATTERN_NUMBER 4
#define HEX_PATTERN_LIST_NUMBER 6
#define HEX_COLLISION_CORRECTION 0.05
#define HEX_NULL_LANE -1

#define HEX_GAMESPEED_CAP 1.5

#define HEX_CHANGE_ROT_THRESHOLD 80
#define HEX_CHANGE_ROT_ODD 10
#define HEX_SPEED_INCREMENT 0.01
#define HEX_SPEED_CAP 0.08
#define HEX_INIT_ROT_SPEED 0.03

#define HEX_MAX_PUMP 1.3
#define HEX_PUMP_SPEED 0.04
#define HEX_PUMP_DELAY 16
#define HEX_MAX_PUMP_OVER 2.5

#define HEX_FRAMES_PER_SEC 20
#define HEX_SEC_PER_FRAMES ( 1.0 / 20.0 )

#define HEX_PLAYER_DISTANCE_PERCENT 0.16
#define HEX_PLAYER_DEFAULT_DISTANCE ( HEX_LINE_LENGTH * HEX_PLAYER_DISTANCE_PERCENT )

#define HEX_HSC_OFFSET 2
#define HEX_GAME_ID 0x02FF

// -----------------------------------------------------------------------------
// Wall pattern templates - real data copied verbatim from upstream's own
// wallsData.h (lane, distance-along-lane, width; see this file's own
// header comment on pattern 4's own real dead-trailing-data quirk).
// -----------------------------------------------------------------------------

typedef struct
{
    int lane;
    float distance;
    float width;
} HexWall;

// "C * 3 inverted like"
HexWall hexPatt1[15] =
{
    {0,0,0.1}, {1,0,0.1}, {2,0,0.1}, {3,0,0.1}, {4,0,0.1},
    {0,0.3,0.1}, {1,0.3,0.1}, {3,0.3,0.1}, {4,0.3,0.1}, {5,0.3,0.1},
    {0,0.6,0.1}, {1,0.6,0.1}, {2,0.6,0.1}, {3,0.6,0.1}, {4,0.6,0.1}
};

// double spiral
HexWall hexPatt2[22] =
{
    {0,0,0.1}, {1,0.1,0.1}, {2,0.2,0.1}, {3,0.3,0.1}, {4,0.4,0.1}, {5,0.5,0.1},
    {0,0.6,0.1}, {1,0.7,0.1}, {2,0.8,0.1}, {3,0.9,0.1}, {4,1,0.1},
    {3,0,0.1}, {4,0.1,0.1}, {5,0.2,0.1}, {0,0.3,0.1}, {1,0.4,0.1}, {2,0.5,0.1},
    {3,0.6,0.1}, {4,0.7,0.1}, {5,0.8,0.1}, {0,0.9,0.1}, {1,1,0.1}
};

HexWall hexPatt3[11] =
{
    {0,0,0.2}, {0,0.2,0.2}, {0,0.4,0.2},
    {1,0,0.1}, {2,0,0.1}, {3,0,0.1}, {4,0,0.1},
    {5,0.5,0.1}, {4,0.5,0.1}, {3,0.5,0.1}, {2,0.5,0.1}
};

// Zig zag - real wall_num is 10, not 12 (see this file's own header
// comment) - hexWallCount below[3] is 10, so entries 10/11 are dead data
// here too, matching real hardware exactly.
HexWall hexPatt4[12] =
{
    {0,0,1.3}, {3,0,1.3}, {5,0,0.1}, {2,0,0.1}, {4,0.3,0.1}, {1,0.3,0.1},
    {5,0.6,0.1}, {2,0.6,0.1}, {4,0.9,0.1}, {1,0.9,0.1}, {5,1.2,0.1}, {2,1.2,0.1}
};

// 3 walls * 3
HexWall hexPatt5[9] =
{
    {0,0,0.1}, {2,0,0.1}, {4,0,0.1},
    {1,0.3,0.1}, {3,0.3,0.1}, {5,0.3,0.1},
    {2,0.6,0.1}, {4,0.6,0.1}, {0,0.6,0.1}
};

// 2 holes in a circle
HexWall hexPatt6[4] =
{
    {0,0,0.1}, {2,0,0.1}, {4,0,0.1}, {5,0,0.1}
};

int hexWallCount[6] = {15, 22, 11, 10, 9, 4};
float hexPatternLength[6] = {0.9, 1.4, 0.9, 1.5, 0.9, 0.4};

int hexTemplateWallLane( int t, int i )
{
    if( t == 0 ) return hexPatt1[i].lane;
    if( t == 1 ) return hexPatt2[i].lane;
    if( t == 2 ) return hexPatt3[i].lane;
    if( t == 3 ) return hexPatt4[i].lane;
    if( t == 4 ) return hexPatt5[i].lane;
    return hexPatt6[i].lane;
}

float hexTemplateWallDistance( int t, int i )
{
    if( t == 0 ) return hexPatt1[i].distance;
    if( t == 1 ) return hexPatt2[i].distance;
    if( t == 2 ) return hexPatt3[i].distance;
    if( t == 3 ) return hexPatt4[i].distance;
    if( t == 4 ) return hexPatt5[i].distance;
    return hexPatt6[i].distance;
}

float hexTemplateWallWidth( int t, int i )
{
    if( t == 0 ) return hexPatt1[i].width;
    if( t == 1 ) return hexPatt2[i].width;
    if( t == 2 ) return hexPatt3[i].width;
    if( t == 3 ) return hexPatt4[i].width;
    if( t == 4 ) return hexPatt5[i].width;
    return hexPatt6[i].width;
}

// -----------------------------------------------------------------------------
// Real bitmap art (see this file's own header comment)
// -----------------------------------------------------------------------------

int hexBitmapMicroHex[58] =
{
    32, 14,
    0xDB,0x3D,0xF3,0xC0, 0xFB,0x7D,0xF7,0xC0, 0xFB,0x61,0x36,0xC0, 0xDB,0x61,0xF6,0xC0,
    0xDB,0x61,0xE6,0xC0, 0xDB,0x61,0x76,0xC0, 0xDB,0x7D,0x37,0xC0, 0xDB,0x39,0x37,0xC0,
    0x0,0x0,0x0,0x0, 0xAD,0x5D,0xDD,0x80, 0xA9,0x55,0x15,0x40, 0xEC,0x9D,0x55,0x40,
    0xA9,0x55,0x55,0x40, 0xAD,0x55,0xDD,0x40
};

int hexBitmapHscr[10] =
{
    16, 4,
    0xAE,0xD8, 0xE8,0x94, 0xA2,0x98, 0xAE,0xD4
};

int hexBitmapGmover[47] =
{
    24, 15,
    0xFB,0xDE,0xF8, 0xFB,0x5E,0xC0, 0xC3,0x5A,0xC0, 0xDB,0x5A,0xF8, 0xCB,0xDA,0xC0,
    0xFB,0x5A,0xF8, 0xFB,0x5A,0xF8, 0x0,0x0,0x0, 0xFB,0x5E,0xF0, 0xCB,0x58,0xC8,
    0xCB,0x58,0xC8, 0xCB,0x5E,0xF0, 0xCB,0x58,0xC8, 0xF9,0x9E,0xC8, 0xF9,0x9E,0xC8
};

// Real font5x7 low-range icon glyphs (decimal 21/22/23 - see this file's
// own header comment) followed by plain ASCII, 0-terminated.
char hexPlayText[6]    = {21, 80,76,65,89, 0};              // icon + "PLAY"
char hexQuitText[6]    = {23, 81,85,73,84, 0};              // icon + "QUIT"
char hexRestartText[9] = {22, 82,101,115,116,97,114,116, 0}; // icon + "Restart"
char hexMenuText[6]    = {23, 77,101,110,117, 0};           // icon + "Menu"

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

enum HexState
{
    HEX_STATE_MENU = 0,
    HEX_STATE_PLAY = 1,
    HEX_STATE_GAMEOVER = 2
};

int hexGameState;

float hexAngle = 0;
float hexHexagon[6][2];

float hexScore;
float hexGamespeed = 1;
float hexGamespeedIncr = 0.0005;

int hexLastRotation;
float hexRotSpeed = 0.2;

float hexPump;
int hexPumpDelay;

float hexPlayerAngle;
float hexPlayerSpeed = 0.35;

float hexHgScore = 0.0;
int hexGameOverTimer = 0;

float hexWallSpeed = 0.015;

// Live wall-pattern slots (see this file's own header comment on the
// storePattern()-flattening decision). hexPatTemplate[i] == -1 means
// "empty" (matches upstream's own zeroed wall_num==0 slots).
int hexPatTemplate[4];
float hexPatDistance[4];
int hexPatRotation[4];
bool hexPatMirror[4];

// -----------------------------------------------------------------------------
// Live-pattern helpers
// -----------------------------------------------------------------------------

int hexPatWallCount( int slot )
{
    if( hexPatTemplate[slot] == -1 ) return 0;
    return hexWallCount[ hexPatTemplate[slot] ];
}

float hexPatLength( int slot )
{
    if( hexPatTemplate[slot] == -1 ) return 0;
    return hexPatternLength[ hexPatTemplate[slot] ];
}

// -----------------------------------------------------------------------------
// EEPROM highscore (real upstream's own single-entry table, `pos` always 0)
// -----------------------------------------------------------------------------

bool hexIsValidHSC()
{
    int valid = ( eeprom_read_byte( 0 ) << 8 ) + eeprom_read_byte( 1 );
    return valid == HEX_GAME_ID;
}

void hexWriteHSC( int name0, int name1, int name2, int score, int pos )
{
    int p = pos * 5;
    eeprom_write_byte( p + HEX_HSC_OFFSET, name0 );
    eeprom_write_byte( p + 1 + HEX_HSC_OFFSET, name1 );
    eeprom_write_byte( p + 2 + HEX_HSC_OFFSET, name2 );
    eeprom_write_byte( p + 3 + HEX_HSC_OFFSET, ( score >> 8 ) & 0xFF );
    eeprom_write_byte( p + 4 + HEX_HSC_OFFSET, score & 0xFF );
}

int hexReadHSCScore( int pos )
{
    int p = pos * 5;
    int hi = eeprom_read_byte( p + 3 + HEX_HSC_OFFSET );
    int lo = eeprom_read_byte( p + 4 + HEX_HSC_OFFSET );
    return ( hi << 8 ) + lo;
}

void hexInitHighScore()
{
    if( !hexIsValidHSC() )
    {
        eeprom_write_byte( 0, ( HEX_GAME_ID >> 8 ) & 0xFF );
        eeprom_write_byte( 1, HEX_GAME_ID & 0xFF );
        hexWriteHSC( 65, 65, 65, 0, 0 ); // "AAA", 0
    }
}

// -----------------------------------------------------------------------------
// sprintf("%03d", ...) replacement (see this file's own header comment)
// -----------------------------------------------------------------------------

void hexFormatScore( int value, bool blankLeadingZeros, char* outText )
{
    int v = value;
    if( v < 0 ) v = 0;

    int hundreds = ( v / 100 ) % 10;
    int tens = ( v / 10 ) % 10;
    int units = v % 10;

    outText[0] = 48 + hundreds;
    outText[1] = 48 + tens;
    outText[2] = 48 + units;
    outText[3] = 0;

    if( blankLeadingZeros )
    {
        if( outText[0] == 48 )
        {
            outText[0] = 32; // space
            if( outText[1] == 48 )
              outText[1] = 32;
        }
    }
}

// -----------------------------------------------------------------------------
// Hexagon geometry
// -----------------------------------------------------------------------------

void hexComputeHexagon()
{
    float cAngle = cos( HEX_PI * 2.0 / 6.0 );
    float sAngle = sin( HEX_PI * 2.0 / 6.0 );
    int i;

    hexHexagon[0][0] = HEX_LINE_LENGTH * cos( hexAngle );
    hexHexagon[0][1] = HEX_LINE_LENGTH * sin( hexAngle );

    for( i = 1; i < HEX_SIDES; i++ )
    {
        hexHexagon[i][0] = hexHexagon[i-1][0]*cAngle - hexHexagon[i-1][1]*sAngle;
        hexHexagon[i][1] = hexHexagon[i-1][0]*sAngle + hexHexagon[i-1][1]*cAngle;
    }
}

void hexDrawHexagon()
{
    int i;
    for( i = 0; i < HEX_SIDES; i++ )
    {
        float s1x = hexHexagon[i][0] * HEX_SMALL_HEX_PERCENT * hexPump;
        float s1y = hexHexagon[i][1] * HEX_SMALL_HEX_PERCENT * hexPump;

        int iNext = i + 1;
        if( i >= 5 ) iNext = 0;

        float s2x = hexHexagon[iNext][0] * HEX_SMALL_HEX_PERCENT * hexPump;
        float s2y = hexHexagon[iNext][1] * HEX_SMALL_HEX_PERCENT * hexPump;

        gbSetColor( 1 );
        gbDrawLine( HEX_CENTER_X + (int)s1x, HEX_CENTER_Y + (int)s1y, HEX_CENTER_X + (int)s2x, HEX_CENTER_Y + (int)s2y );

        gbSetColor( GB_GRAY );
        gbDrawLine( HEX_CENTER_X + (int)s1x, HEX_CENTER_Y + (int)s1y, HEX_CENTER_X + (int)hexHexagon[i][0], HEX_CENTER_Y + (int)hexHexagon[i][1] );
    }
}

// -----------------------------------------------------------------------------
// Walls
// -----------------------------------------------------------------------------

void hexInitWalls()
{
    int i;
    for( i = 0; i < HEX_PATTERN_NUMBER; i++ )
    {
        hexPatDistance[i] = 0;
        hexPatTemplate[i] = -1;
        hexPatRotation[i] = 0;
        hexPatMirror[i] = false;
    }

    // storePattern(patternList[PATTERN_LIST_NUMBER-1], &patterns[0]);
    hexPatTemplate[0] = HEX_PATTERN_LIST_NUMBER - 1;
    hexPatDistance[0] = 1;
    hexPatMirror[0] = false;
}

void hexAddPattern( int index )
{
    float minPos = 1;
    int i;
    for( i = 0; i < HEX_PATTERN_NUMBER; i++ )
    {
        float endPos = hexPatDistance[i] + hexPatLength( i );
        if( minPos < endPos )
          minPos = endPos;
    }

    hexPatTemplate[index] = arand( HEX_PATTERN_LIST_NUMBER );
    hexPatDistance[index] = minPos;
    hexPatRotation[index] = arand( HEX_SIDES );
    hexPatMirror[index] = ( arand( 2 ) == 1 );
}

void hexUpdateWalls()
{
    int i;
    for( i = 0; i < HEX_PATTERN_NUMBER; i++ )
    {
        if( hexPatDistance[i] + hexPatLength( i ) > 0 )
          hexPatDistance[i] -= hexWallSpeed * hexGamespeed;
        else
          hexAddPattern( i );
    }
}

void hexDrawWalls()
{
    int j, i;
    for( j = 0; j < HEX_PATTERN_NUMBER; j++ )
    {
        int wallCount = hexPatWallCount( j );
        float patLen = hexPatLength( j );

        if( hexPatDistance[j] + patLen > HEX_SMALL_HEX_PERCENT )
        {
            for( i = 0; i < wallCount; i++ )
            {
                float wallDist = hexTemplateWallDistance( hexPatTemplate[j], i );
                float wallWidth = hexTemplateWallWidth( hexPatTemplate[j], i );
                int wallLane = hexTemplateWallLane( hexPatTemplate[j], i );

                if( wallLane != HEX_NULL_LANE
                    && ( hexPatDistance[j] + wallDist ) * hexPump < 1
                    && hexPatDistance[j] + wallDist + wallWidth > HEX_SMALL_HEX_PERCENT )
                {
                    wallLane = ( wallLane + hexPatRotation[j] ) % HEX_SIDES;
                    if( hexPatMirror[j] )
                      wallLane = HEX_SIDES - 1 - wallLane;

                    float width = ( hexPatDistance[j] + wallDist + wallWidth ) * hexPump;
                    if( width >= 1 ) width = 1;

                    float s1x = hexHexagon[wallLane][0];
                    float s1y = hexHexagon[wallLane][1];

                    int wallLaneNext = wallLane + 1;
                    if( wallLane >= 5 ) wallLaneNext = 0;

                    float s2x = hexHexagon[wallLaneNext][0];
                    float s2y = hexHexagon[wallLaneNext][1];

                    float distance = hexPatDistance[j] + wallDist;
                    if( distance <= HEX_SMALL_HEX_PERCENT )
                      distance = HEX_SMALL_HEX_PERCENT;

                    float s3x = s1x * distance * hexPump;
                    float s3y = s1y * distance * hexPump;
                    float s4x = s2x * distance * hexPump;
                    float s4y = s2y * distance * hexPump;

                    s1x = s1x * width;
                    s1y = s1y * width;
                    s2x = s2x * width;
                    s2y = s2y * width;

                    gbSetColor( 1 );

                    // DRAW_MODE 1 (fill) is the only reachable branch
                    // upstream - see this file's own header comment.
                    gbFillTriangle
                    (
                        (int)s1x + HEX_CENTER_X, (int)s1y + HEX_CENTER_Y,
                        (int)s2x + HEX_CENTER_X, (int)s2y + HEX_CENTER_Y,
                        (int)s3x + HEX_CENTER_X, (int)s3y + HEX_CENTER_Y
                    );
                    gbFillTriangle
                    (
                        (int)s4x + HEX_CENTER_X, (int)s4y + HEX_CENTER_Y,
                        (int)s2x + HEX_CENTER_X, (int)s2y + HEX_CENTER_Y,
                        (int)s3x + HEX_CENTER_X, (int)s3y + HEX_CENTER_Y
                    );
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// State transitions / shared controls
// -----------------------------------------------------------------------------

void hexInitMenu()
{
    hexGameState = HEX_STATE_MENU;
    hexRotSpeed = HEX_INIT_ROT_SPEED;
    hexPump = 2;
    hexHgScore = ( (float)hexReadHSCScore( 0 ) ) / 10;
}

void hexPlayInit()
{
    hexInitWalls();

    hexLastRotation = 0;
    hexScore = 0;
    hexPlayerAngle = 0;
    hexPumpDelay = 20;
    hexPump = 2;
    hexGameState = HEX_STATE_PLAY;
    hexRotSpeed = HEX_INIT_ROT_SPEED;
    hexGamespeed = 1;
}

void hexInitGameOver()
{
    hexGameState = HEX_STATE_GAMEOVER;
    hexGameOverTimer = 0;
    hexHgScore = ( (float)hexReadHSCScore( 0 ) ) / 10;
    if( hexScore > hexHgScore )
      hexWriteHSC( 65, 65, 65, (int)( hexScore * 10 ), 0 ); // "AAA"
}

// gameControls() upstream - shared by live gameplay and the game-over
// screen (return to the internal menu / instant restart).
void hexGameControls()
{
    if( gbPressed( BTN_C ) )
      hexInitMenu();
    if( gbPressed( BTN_B ) )
      hexPlayInit();
}

// menuControls() upstream - Button C ("QUIT") is a documented no-op here
// (see this file's own header comment, shim gap #3).
void hexMenuControls()
{
    if( gbPressed( BTN_A ) )
      hexPlayInit();
}

// -----------------------------------------------------------------------------
// Player
// -----------------------------------------------------------------------------

int hexGetLane( int laneCount )
{
    return ( (int)( hexPlayerAngle / ( 2.0 * HEX_PI / laneCount ) ) ) % laneCount;
}

bool hexCheckCollision( int lane, float distance )
{
    int j, i;
    for( j = 0; j < HEX_PATTERN_NUMBER; j++ )
    {
        int wallCount = hexPatWallCount( j );
        for( i = 0; i < wallCount; i++ )
        {
            int wallLane = ( hexTemplateWallLane( hexPatTemplate[j], i ) + hexPatRotation[j] ) % HEX_SIDES;
            if( hexPatMirror[j] )
              wallLane = HEX_SIDES - 1 - wallLane;

            if( wallLane == lane )
            {
                float wStart = hexPatDistance[j] + hexTemplateWallDistance( hexPatTemplate[j], i );
                float wEnd = wStart + hexTemplateWallWidth( hexPatTemplate[j], i );

                if( ( distance + HEX_COLLISION_CORRECTION ) > wStart && ( distance + HEX_COLLISION_CORRECTION ) < wEnd )
                  return true;
            }
        }
    }

    return false;
}

void hexUpdatePlayer()
{
    if( hexCheckCollision( hexGetLane( 6 ), HEX_PLAYER_DISTANCE_PERCENT ) )
      hexInitGameOver();
}

void hexDrawPlayer( float angle )
{
    int x = (int)( cos( hexPlayerAngle + angle ) * HEX_PLAYER_DEFAULT_DISTANCE * hexPump + HEX_CENTER_X );
    int y = (int)( sin( hexPlayerAngle + angle ) * HEX_PLAYER_DEFAULT_DISTANCE * hexPump + HEX_CENTER_Y );

    gbSetColor( 1 );
    gbDrawPixel( x+1, y );
    gbDrawPixel( x-1, y );
    gbDrawPixel( x, y+1 );
    gbDrawPixel( x, y-1 );

    gbSetColor( 0 );
    gbDrawPixel( x, y );
    gbDrawPixel( x+2, y );
    gbDrawPixel( x+1, y+1 );
    gbDrawPixel( x+1, y-1 );
    gbDrawPixel( x, y+2 );
    gbDrawPixel( x, y-2 );
    gbDrawPixel( x-1, y-1 );
    gbDrawPixel( x-1, y+1 );
    gbDrawPixel( x-2, y );
}

void hexControls()
{
    if( hexGameState != HEX_STATE_GAMEOVER )
    {
        if( gbRepeat( BTN_A, 0 ) )
        {
            hexPlayerAngle += hexPlayerSpeed;
            while( hexPlayerAngle > 2 * HEX_PI )
              hexPlayerAngle -= 2 * HEX_PI;

            int lane = hexGetLane( 6 );
            if( hexCheckCollision( lane, HEX_PLAYER_DISTANCE_PERCENT ) )
              hexPlayerAngle = ( ( 2 * HEX_PI / 6 ) * lane ) - 0.05;
        }

        if( gbRepeat( BTN_DOWN, 0 ) )
        {
            hexPlayerAngle -= hexPlayerSpeed;
            while( hexPlayerAngle < 0 )
              hexPlayerAngle += 2 * HEX_PI;

            int lane = hexGetLane( 6 );
            if( hexCheckCollision( lane, HEX_PLAYER_DISTANCE_PERCENT ) )
            {
                int nextLane = ( lane + 1 ) % HEX_SIDES;
                hexPlayerAngle = ( 2 * HEX_PI / 6 ) * nextLane + 0.05;
            }
        }

        while( hexPlayerAngle > 2 * HEX_PI )
          hexPlayerAngle -= 2 * HEX_PI;
        while( hexPlayerAngle < 0 )
          hexPlayerAngle += 2 * HEX_PI;
    }
}

// -----------------------------------------------------------------------------
// Speed/pump/rotation-speed tickers
// -----------------------------------------------------------------------------

void hexUpdateSpeed()
{
    hexGamespeed += hexGamespeedIncr;
    if( hexGamespeed > HEX_GAMESPEED_CAP )
      hexGamespeed = HEX_GAMESPEED_CAP;
}

void hexUpdatePump()
{
    if( hexPump > 1 )
      hexPump -= HEX_PUMP_SPEED;
    else
      hexPump = 1;

    hexPumpDelay++;

    if( hexPump == 1 && hexPumpDelay > HEX_PUMP_DELAY )
    {
        hexPump = HEX_MAX_PUMP;
        // real preserved typo (`==` not `=`) - see this file's own header comment
        hexPumpDelay == 0;
    }
}

void hexUpdateRotationSpeed()
{
    hexLastRotation++;

    // real preserved bug (misplaced abs()) - see this file's own header comment
    if( hexLastRotation > HEX_CHANGE_ROT_THRESHOLD
        && arand( HEX_CHANGE_ROT_ODD + 1 ) == HEX_CHANGE_ROT_ODD
        && ( hexRotSpeed < HEX_SPEED_CAP ) )
    {
        hexLastRotation = 0;

        float sign = 1;
        if( arand( 2 ) != 0 )
          sign = -1;

        hexRotSpeed = sign * ( fabs( hexRotSpeed ) + HEX_SPEED_INCREMENT );
    }
}

void hexUpdateHexagon()
{
    hexAngle += hexRotSpeed;
    if( hexAngle >= 2 * HEX_PI )
      hexAngle -= 2 * HEX_PI;

    hexComputeHexagon();
}

// -----------------------------------------------------------------------------
// Drawing - HUD / menu / game-over screens
// -----------------------------------------------------------------------------

void hexDrawHud()
{
    gbSetColor( 1 );

    char scoreBuf[4];
    hexFormatScore( (int)floor( hexScore ), true, scoreBuf );

    int start = 62;
    if( scoreBuf[0] == 32 )
    {
        start += 4;
        if( scoreBuf[1] == 32 )
          start += 4;
    }

    int i;
    for( i = 0; i < 7; i++ )
      gbDrawFastHLine( start + i, i, 22 - i );

    gbSetColor( 0 );
    gbDrawPixel( 79, 1 );
    gbDrawPixel( 79, 3 );

    gbCursorY = 0;
    gbCursorX = 67;
    gbPrintString( scoreBuf );
    gbCursorX = 81;
    gbPrintNumber( (int)( ( hexScore - floor( hexScore ) ) * 10 ) );
}

void hexDrawMenu()
{
    gbSetColor( 0 );
    gbFillRect( 29, 13, 26, 5 );
    gbSetColorBg( 1, 0 );
    gbDrawBitmap( 29, 4, hexBitmapMicroHex );

    gbCursorX = 32;
    gbCursorY = 34;
    gbPrintString( hexPlayText );

    gbCursorX = 32;
    gbCursorY = 41;
    gbPrintString( hexQuitText );

    gbDrawBitmap( 62, 36, hexBitmapHscr );

    char hgBuf[4];
    hexFormatScore( (int)floor( hexHgScore ), true, hgBuf );

    gbDrawPixel( 74, 42 );
    gbDrawPixel( 74, 44 );

    gbCursorY = 41;
    gbCursorX = 62;
    gbPrintString( hgBuf );
    gbCursorX = 76;
    gbPrintNumber( (int)( ( hexHgScore - floor( hexHgScore ) ) * 10 ) );
}

void hexDrawGameOver()
{
    hexDrawHexagon();
    hexDrawWalls();
    hexDrawPlayer( hexAngle );

    if( hexGameOverTimer < 6 && ( hexGameOverTimer % 3 ) < 2 )
      gbFillScreen( 1 ); // real fillScreen(INVERT) - see this file's own header comment: real hardware's own fillScreen() ignores its color argument and always fills solid black, so upstream's own INVERT flash never actually inverts on real hardware either

    if( hexPump == HEX_MAX_PUMP_OVER )
    {
        gbSetColor( 0 );
        gbFillRect( 31, 2, 21, 15 );
        gbSetColor( 1 );
        gbDrawBitmap( 31, 2, hexBitmapGmover );

        gbSetColorBg( 1, 0 );

        gbCursorX = 66;
        gbCursorY = 6;
        gbPrintString( "TIME" );

        // NOTE: real upstream's own leading-zero blanking is commented
        // out for these two readouts (drawGameOver() only) - preserved
        // as a real inconsistency, see this file's own header comment.
        char scoreBuf[4];
        hexFormatScore( (int)floor( hexScore ), false, scoreBuf );

        gbDrawPixel( 77, 13 );
        gbDrawPixel( 77, 15 );

        gbCursorY = 12;
        gbCursorX = 65;
        gbPrintString( scoreBuf );
        gbCursorX = 79;
        gbPrintNumber( (int)( ( hexScore - floor( hexScore ) ) * 10 ) );

        gbCursorX = 66;
        gbCursorY = 18;
        gbPrintString( "BEST" );

        char bestBuf[4];
        hexFormatScore( (int)floor( hexHgScore ), false, bestBuf );

        gbDrawPixel( 77, 25 );
        gbDrawPixel( 77, 27 );

        gbCursorY = 24;
        gbCursorX = 65;
        gbPrintString( bestBuf );
        gbCursorX = 79;
        gbPrintNumber( (int)( ( hexHgScore - floor( hexHgScore ) ) * 10 ) );

        gbCursorX = 27;
        gbCursorY = 33;
        gbPrintString( hexRestartText );

        gbCursorX = 27;
        gbCursorY = 40;
        gbPrintString( hexMenuText );
    }
}

// -----------------------------------------------------------------------------
// Top-level per-state loops
// -----------------------------------------------------------------------------

void hexMenuLoop()
{
    hexMenuControls();
    hexPump = 2;

    hexAngle += hexRotSpeed;
    if( hexAngle >= 2 * HEX_PI )
      hexAngle -= 2 * HEX_PI;

    hexComputeHexagon();
    hexDrawHexagon();
    hexDrawMenu();
}

void hexMainGame()
{
    if( hexGameState != HEX_STATE_GAMEOVER )
    {
        hexUpdateSpeed();
        hexUpdatePump();
        hexUpdateHexagon();
        hexUpdateRotationSpeed();
        hexUpdateWalls();
        hexUpdatePlayer();
        hexControls();

        hexScore += HEX_SEC_PER_FRAMES;
    }

    hexDrawHexagon();
    hexDrawWalls();
    hexDrawPlayer( hexAngle );

    hexDrawHud();

    hexGameControls();
}

void hexUpdateGameOver()
{
    hexUpdateHexagon();
    hexGameControls();

    hexDrawGameOver();

    hexGameOverTimer++;
    if( hexGameOverTimer > 10 )
    {
        if( hexPump < HEX_MAX_PUMP_OVER )
          hexPump += 0.1;
        else
          hexPump = HEX_MAX_PUMP_OVER;
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameHexagon_init()
{
    gbBegin();
    gbSetFrameRate( HEX_FRAMES_PER_SEC ); // matches upstream's own explicit gb.setFrameRate(FRAMES_PER_SEC) call
    hexInitHighScore();
    hexInitMenu();
}

void gameHexagon_update()
{
    if( !gbUpdate() ) return;

    if( hexGameState == HEX_STATE_MENU ) hexMenuLoop();
    else if( hexGameState == HEX_STATE_PLAY ) hexMainGame();
    else hexUpdateGameOver();

    gbRenderFrame();
}
