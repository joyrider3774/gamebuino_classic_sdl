#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// World's Hardest Game (Sorunome, license: none specified -
// github.com/Sorunome/Worlds-Hardest-Game-Gamebuino), the same author as
// this project's already-ported gameBlockdude.c. A Gamebuino port of the
// well-known Flash game: walk a small square through 22 real bundled
// levels, dodging patrolling black "ball" enemies (rectangles moving back
// and forth along fixed point-to-point patrol paths), collecting every
// coin in the level, then reaching the level's own goal-tile zone to
// advance. Touching an enemy resets you to your most recent checkpoint
// (not necessarily the level start - see the checkpoint/save-point system
// below) and increments a visible "tries" counter.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the full reasoning). Every global/
// function got a `whg`-prefixed name (this cartridge has no linker - every
// game shares one flat global namespace). `byte` became `int` throughout
// (see avrCompat.h). `BLACK`/`WHITE`/`NOROT`/`NOFLIP` have no equivalent
// named constants anywhere in this project (every other ported game just
// uses the literal 0/1 values directly - confirmed by grepping the whole
// src/ tree) - ported the same way: `BLACK`->1, `WHITE`->0, `NOROT`->0,
// `NOFLIP`->0 at each real call site.
//
// STRUCTS INSTEAD OF CLASSES: upstream's own real `Coin`/`Enemy` C++
// classes (heap-allocated via `new` into `Coin *coins[70]`/
// `Enemy *enemies[45]` pointer arrays, freed with `delete` in
// `destroyMap()`) were flattened into plain `WhgCoin`/`WhgEnemy` structs
// plus fixed `WhgCoin whgCoins[70]`/`WhgEnemy whgEnemies[45]` arrays - the
// same "no dynamic allocation anywhere in this project's ported games"
// treatment already established in gameBlockdude.c's own header comment.
// `whgNumCoins`/`whgNumEnemies` simply reset to 0 at the start of every
// `whgLoadMap()` call instead of upstream's own explicit `delete` loop -
// exactly equivalent in observable behavior, since every reader of
// `whgCoins[]`/`whgEnemies[]` everywhere in this file only ever iterates
// `0..whgNumCoins/Enemies-1`. Upstream's `Enemy::nextPoint` field is
// declared and initialized but never actually read or written anywhere
// else in the real class (dead code in the original) - not reproduced
// here.
//
// REAL BITMAP ART PORTED: `checkers` (a 6x6 dithered boundary-wall
// texture), `logo` (the real 64x36 title-screen splash, shown via
// upstream's own `gb.titleScreen(logo)`), and `ok`/`ko` (the real 8x7
// level-complete/incomplete icons, upstream's own comment credits these
// as "from bub" - a different staged game in this project's own `more
// games/` tree) were all copied byte-for-byte into plain
// `int[N] whgXxxBitmap = { width, height, ... }` arrays (this dialect's
// own `int[N] name` array-declaration order) - the exact shape
// `gbDrawBitmap()` expects. Every one of upstream's own byte literals here
// was already `0x`-prefixed hex (or plain decimal for `ok`'s first byte,
// `0x2`) - no Arduino `B00000000`-style binary literals anywhere in this
// upstream source (grepped directly to confirm). No solid fill/mask layer
// is drawn underneath any of these upstream (confirmed by re-reading every
// real `gb.display.drawBitmap(...)` call site directly - none are preceded
// by a `setColor(GRAY)`+mask-bitmap pair or a plain fillRect), so none was
// needed here either - see gameFlappyBirdo.c's own header comment for the
// two real cases in this project where that assumption was wrong; checked
// for specifically here and confirmed not to apply.
//
// LEVEL DATA - MECHANICALLY REGENERATED, NOT HAND-TRANSCRIBED: upstream's
// real `levels.ino` encodes each of its 22 levels as one dense
// `const byte gamemapN[] PROGMEM` byte table (header: winTile,width,
// height; then width*height tile bytes; then an initial 2-byte spawn
// point; then a variable number of enemy-patrol/coin records, terminated
// by a reserved 0xFF byte), built almost entirely out of a parameterized
// `TILETOPX(x)` macro (`x*MAPTILESIZE+1`, MAPTILESIZE=6) rather than plain
// literals, with individual enemy patrol-point lists of wildly varying
// length (anywhere from 1 to 8 waypoints) wrapped across multiple source
// lines. Hand-transcribing ~4600 real data values across 22 arrays while
// manually evaluating every `TILETOPX(...)` call and getting every fixed-
// size `int[N]` array declaration's own N exactly right (this dialect
// requires an explicit, exact size - see VIRCON32_C_DIALECT.md - there is
// no `int[] arr = {...}` size inference to fall back on) is exactly the
// kind of mechanical, error-prone-by-hand task a real upstream author
// would never have attempted manually either (hence the macro in the
// first place). Instead, a small verified Python script
// (parse the real `levels.ino` text directly, substitute
// MAPFLAGCOIN/ENEMY/END->253/254/255 and MAPTILESIZE->6, evaluate every
// `TILETOPX(x)` call to `x*6+1`, then flatten each level's own full
// comma-separated initializer list) produced every `whgMapN` array below
// byte-for-byte from the real source, with the resulting element count
// then cross-checked BY HAND against the real byte-layout arithmetic for
// two representative levels before trusting the rest: `whgMap1` (4 fixed-
// length enemies, no coins) = 3 header + 108 tiles + 2 spawn + 4*6 enemy
// bytes + 1 end = 138, and `whgMap3` (11 enemies with irregular 4/5-
// waypoint patrol lists, 1 coin) = 3 + 20 + 2 + 126 + 3 + 1 = 155 - both
// match the script's own independently-computed counts exactly.
// MAPFLAGCOIN/MAPFLAGENEMY/MAPFLAGEND (0xFD/0xFE/0xFF in the real source)
// are ported as WHG_MAPFLAGCOIN/ENEMY/END (253/254/255) below.
//
// A REAL, LOAD-BEARING PARSING QUIRK IN `whgLoadMap()`, PRESERVED
// EXACTLY: upstream's own real `loadMap()` while-loop does NOT explicitly
// skip over an enemy's own variable-length patrol-point list, or a coin's
// own y-coordinate byte, after building that entity - it relies entirely
// on every one of those interior data bytes failing to equal any of the
// three reserved flag values (253/254/255, real coordinate values in this
// game never get that large), so the *same* outer while-loop iteration
// harmlessly re-reads each one as a "flag" that matches neither the enemy
// nor coin branch, silently consuming it one byte at a time via the loop's
// own unconditional `i++`, until real bytes belonging to the NEXT entity
// record are reached. This is genuinely how the format is meant to work
// (there is no explicit patrol-list length anywhere in the data), not a
// bug - ported as an ordinary `while` loop with sequential `i = i + 1`
// statements, matching this exactly. One real deviation, made
// deliberately rather than by oversight: upstream's own C++ constructs
// the `Enemy` in a single expression, `new Enemy(pgm_read_byte(gamemap+
// (i++)), gamemap+i)` - whether the *second* argument (`gamemap+i`, meant
// to point just past the speed byte) is evaluated before or after the
// *first* argument's own `i++` side effect is unspecified evaluation
// order in C/C++, and this real upstream code silently depends on
// whichever order the actual avr-gcc build happened to choose. Rather
// than risk reproducing that same ambiguity in a different compiler's own
// unspecified order (and this dialect's own evaluation-order rules for
// nested-call arguments are not something this project has had reason to
// pin down elsewhere), this port resolves it deterministically with
// explicit sequential statements (read+advance the speed byte, THEN take
// the pointer) - matching the clearly-intended reading of the algorithm
// (the one that actually produces working, playable levels), not a
// behavior change.
//
// A REAL, GENUINELY MORE-VISIBLE-HERE-THAN-ON-REAL-HARDWARE EEPROM BUG,
// FOUND AND FIXED (not preserved - the same class of issue
// gameBlockdude.c's own header comment already documented once for this
// same author's own other game, and fixed there too): upstream's real
// `refreshLevelMenu()` reads one raw EEPROM byte per level (`byte done =
// EEPROM.read(curPick-1);`) and shows the "ok" checkmark icon whenever
// `done>0`. A genuinely never-written AVR EEPROM cell reads back as 255
// (0xFF, real hardware's own factory-erased state - see eepromShim.c's own
// header comment for why this shim deliberately matches that instead of
// defaulting to 0), and 255 > 0 is true - so on a completely fresh save,
// EVERY level showed as already completed, not just the specific "moves"
// nonsense-readout side effect Blockdude's own analogous bug caused (that
// game's own check was `moves>0` on a genuinely separate byte from its own
// "is it done" logic - checked directly, WHG's own single `done` byte
// here plays both roles at once, so this port's version of the bug was
// the more visible of the two: real hardware normally masks this with
// whatever residue happens to be sitting in a chip that's been used for
// other sketches before, but this shim's own per-game EEPROM slots are
// deterministically blank 0xFF the first time any save slot is ever
// claimed - see eepromShim.c - so this fired reliably, for every level,
// the first time this cartridge was ever run on a fresh save slot. Fixed
// in `whgDrawLevelMenu()`: the fresh-cell sentinel (255) is normalized
// back to 0 right after reading, matching this project's own established
// EEPROM convention - safe here since real upstream's own `eeprom_write_
// byte()` call site only ever writes a literal 1 for "done," never 255.
//
// TWO MORE REAL QUIRKS IN `whgUpdatePlay()`, BOTH TRACED DIRECTLY THROUGH
// UPSTREAM'S OWN `doTitleScreen()`/`doMainMenu()`/`loop()` AND PRESERVED:
// (1) `tries` (the on-screen death counter) is reset to 0 only inside
// upstream's real `doMainMenu()` (i.e. only when a level is freshly picked
// from the level-select menu) - NOT inside `loadMap()` itself. That means
// auto-advancing through several levels in a row without ever revisiting
// the level menu (beating one level's own goal loads the next one
// directly, in the same real function) keeps accumulating the SAME
// running `tries` total across all of them; only a fresh menu-driven level
// pick resets it. Ported as two distinct call sites below rather than one
// shared "start a level" helper: the level-menu Button-A handler resets
// `whgTries` itself before entering PLAY, while the win-and-advance path
// in `whgUpdatePlay()` calls `whgLoadMap()` directly, deliberately leaving
// `whgTries` untouched, exactly matching upstream.
// (2) Completing a level that ISN'T the last one calls upstream's own
// plain, non-blocking `loadMap()` - execution then falls straight through
// to `loop()`'s own trailing `if(gb.buttons.pressed(BTN_C)) doTitleScreen
// (false);` check in the very same tick. So on real hardware, finishing a
// level while still holding Button C yanks you straight to the level-
// select menu before you ever see a single frame of the level you just
// auto-advanced into - preserved verbatim below by NOT returning early
// after the non-final-level `whgLoadMap()` call, letting the same tick's
// own trailing Button-C check still run. Completing the genuinely LAST
// level instead calls upstream's own real *blocking* `doTitleScreen(false)`
// (which, given `doTitle=false`, actually lands directly on the level-
// select menu rather than the title screen - read carefully from the real
// do-while's own two-parameter dance, see below) - since that fully
// resolves before real `loop()` continues, this port's own equivalent
// (`whgBeginLevelMenu()`) DOES return immediately after, matching the
// "blocking call -> explicit state transition ends this tick" treatment
// used throughout this project (see gameBlockdude.c's own header comment
// for the same idea applied to its own nested blocking loops).
//
// STATE MACHINE: upstream's own real structure is `setup()` calling
// `doTitleScreen(true)` once, a real live-forever `loop()` for gameplay,
// and `doTitleScreen(byte doTitle)`'s own real do-while (`do{ destroyMap();
// if(doTitle){ titleScreen(...); ...settings...; } doTitle=true; }while(
// !doMainMenu());` - note `doTitle` is unconditionally forced true right
// after its own first use, every time). Reading this carefully (not just
// assuming "doTitleScreen" always shows the title) reveals THREE distinct
// real entry behaviors depending on the caller's own `doTitle` argument:
// boot (`doTitleScreen(true)`) shows the title screen, then the level
// menu; a Button-C press from the level menu itself re-enters the SAME
// do-while with `doTitle` already forced true from the previous pass, so
// it shows the title screen again; but Button-C mid-game or finishing the
// final level both call `doTitleScreen(false)` - `doTitle` starts false,
// so the very first do-while pass skips the title screen entirely and
// goes straight to the level menu (only a SUBSEQUENT Button-C press from
// there, if any, would show the title, since `doTitle` is forced true by
// then). Converted into an explicit `WhgState` enum (WHG_STATE_TITLE /
// WHG_STATE_LEVELMENU / WHG_STATE_PLAY) with three distinct transitions
// matching this exactly: TITLE -A-> LEVELMENU; LEVELMENU -C-> TITLE,
// -A-> PLAY (loads the picked level, resets `whgTries`); PLAY -C-> directly
// to LEVELMENU (title screen skipped, matching `doTitleScreen(false)`);
// finishing the last level -> directly to LEVELMENU too. Real Gamebuino's
// own `gb.titleScreen()` waits specifically for Button A - reused directly
// here as the dismiss gesture, matching gamePong.c's/gameBlockdude.c's own
// precedent, with `md_armInputAGate()` (already wired globally into this
// project's own dispatch loop) preventing the menu-launch A-press itself
// from bleeding into an instant title-screen dismissal. Real `gb.display.
// persistence`/`gb.battery.show` have no equivalent in this shim - dropped
// outright (purely cosmetic/an incremental-redraw optimization real
// hardware needed but this project's own "always fully redraw every real
// tick" convention doesn't - see gameBlockdude.c's own header comment for
// the same reasoning applied to its own real `persistence` usage).
// Upstream's own real `refreshLevelMenu()`'s "erase old moves display"
// `fillRect(0,24,48,5)` call is dead code inherited by copy-paste from
// this same author's own Blockdude (which DOES print a real moves count
// into that exact region) - WHG's own level menu never actually prints
// anything there, so nothing is ever drawn into that region here to need
// erasing in the first place, consistent with this project's own "no
// incremental erase needed, full redraw every tick" convention.
//
// `gb.setFrameRate(30)` IS A REAL, EXPLICIT UPSTREAM CALL (inside
// `doTitleScreen()`, guarded by its own `if(doTitle)` so it only actually
// executes once, on the very first real boot) - unlike gamePong.c (which
// never calls `setFrameRate()` at all and so runs at this shim's own
// 20fps default), this game deliberately overrides that default to 30fps
// for its ENTIRE session. Ported as a single `gbSetFrameRate(30)` call in
// `gameWhg_init()`.
//
// EEPROM: real `EEPROM.read(level)`/`EEPROM.write(level,1)` (a single
// plain byte per level - not the two-byte word Blockdude's own real move-
// counter needed) port directly to this shim's own `eeprom_read_byte()`/
// `eeprom_write_byte()`, one byte per level at addresses 0-21 (levels are
// 0-indexed internally), far under this shim's own 1024-byte-per-game
// EEPROM slot.
//
// A REAL DEAD-CODE BOUNDS CHECK THAT BECOMES LIVE (BUT STILL HARMLESS)
// ONCE PORTED: `getTileAtPos()`'s own real lower-bound guard
// (`playerY+yoffset<0`) can never actually fire on real hardware, since
// `playerX`/`playerY`/the offsets are all real unsigned `byte`s there -
// a byte "going negative" instead silently wraps to a huge positive value,
// which the function's OWN SEPARATE upper-bound guard
// (`>= mapHeight*MAPTILESIZE`) then reliably catches instead (every real
// map's own `mapHeight*MAPTILESIZE` is far smaller than 255). This shim's
// `whgPlayerX`/`whgPlayerY` are plain signed `int`s (see avrCompat.h - no
// narrower integer types exist in this dialect), so walking off the top/
// left edge of a level genuinely can produce a negative value here, and
// the lower-bound guard now actually fires for real - but it returns the
// exact same result (0, "blocked") the upper-bound guard would otherwise
// have caught via wraparound on real hardware, so this is not a behavior
// change, just a different one of two redundant guards doing the catching.
//
// `random()`/`gb.pickRandomSeed()` are never called anywhere in this real
// upstream source (grepped directly to confirm) - every enemy's own
// movement is a fully deterministic, fixed point-to-point patrol path
// baked into the level data itself, not randomized - so no `arand()`/
// `gbPickRandomSeed()` port was needed here at all, matching
// gameBlockdude.c's own same finding for its own upstream source.
//
// Upstream's own real `Coin::draw()` calls
// `gb.display.drawRoundRect(drawX,drawY,4,4,1)` (a rounded-corner outline
// rectangle, 1px corner radius), a direct port of real `Display::
// drawRoundRect()` provided by `gbDrawRoundRect()` in `gamebuinoShim.h`/
// `.c` - `whgCoinDraw()` calls `gbDrawRoundRect(drawX,drawY,4,4,1)`
// directly, matching upstream's own real call exactly.

#define WHG_MAPTILESIZE  6
#define WHG_MAPHEADER    3
#define WHG_MAPFLAGCOIN  253
#define WHG_MAPFLAGENEMY 254
#define WHG_MAPFLAGEND   255
#define WHG_NUM_LEVELS   22
#define WHG_MAX_ENEMIES  45
#define WHG_MAX_COINS    70

enum WhgState
{
    WHG_STATE_TITLE     = 0,
    WHG_STATE_LEVELMENU = 1,
    WHG_STATE_PLAY      = 2
};

int whgState;

int whgPlayerX;
int whgPlayerY;
int whgMapX;
int whgMapY;

int whgNumEnemies;
int whgNumCoins;
bool whgDead;
bool whgFrameskip;
int whgTries;
int whgWinTile;
bool whgPotentialWin;

int* whgMapData;     // upstream's own real "gamemap" pointer
int* whgSpawnPoints; // upstream's own real "spawnpoints" pointer
int whgCurLevel;      // 0-based - upstream's own "curLevelNum"
int whgLevelPick;     // 1-based, level-select cursor - upstream's own "curPick"
int whgCurSavePoint;
int whgMapWidth;
int whgMapHeight;

typedef struct
{
    int x;
    int y;
    int have; // 0=not collected, 1=collected this life, 2=permanently kept (past a checkpoint)
} WhgCoin;
WhgCoin whgCoins[WHG_MAX_COINS];

typedef struct
{
    int x;
    int y;
    int s;           // patrol speed, pixels/update - 0 means stationary
    int* points;      // current target waypoint pair, into whgMapData
    int* startPoints; // first waypoint pair, to loop the patrol back to
} WhgEnemy;
WhgEnemy whgEnemies[WHG_MAX_ENEMIES];

// -----------------------------------------------------------------------------
// Real upstream sprite/UI bitmaps - see this file's own header comment.
// -----------------------------------------------------------------------------

int whgCheckersBitmap[8] = { 6, 6, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
int whgOkBitmap[9] = { 8, 7, 0x2, 0x4, 0x88, 0x48, 0x50, 0x30, 0x20 }; // upstream's own comment: "from bub"
int whgKoBitmap[9] = { 8, 7, 0x82, 0x44, 0x28, 0x10, 0x28, 0x44, 0x82 }; // upstream's own comment: "from bub"

// Real upstream title-screen logo, shown via `gb.titleScreen(logo)` -
// copied byte-for-byte, verified 8*36=288 real body bytes (ceil(64/8)=8
// bytes per row * 36 rows) plus the 2 real width/height header bytes = 290.
int whgLogoBitmap[290] =
{
    64, 36, 0x00, 0xF0, 0x00, 0x00, 0x04, 0x00, 0x07, 0xFC, 0x01, 0x28,
    0x00, 0x20, 0x0A, 0x00, 0x1F, 0x80, 0x8A, 0x35, 0x93, 0x28, 0x05, 0x00,
    0x38, 0xFF, 0xAA, 0xFD, 0x52, 0x90, 0x32, 0x00, 0xEF, 0xFE, 0xAB, 0xD5,
    0x92, 0x88, 0x68, 0x01, 0xFF, 0xE0, 0xDA, 0x65, 0x5B, 0x30, 0x94, 0x07,
    0xF9, 0x80, 0x01, 0x28, 0x00, 0x00, 0x60, 0x1F, 0xE2, 0x00, 0xFF, 0xFF,
    0xFF, 0xE3, 0x10, 0x3F, 0x84, 0x00, 0xAD, 0x89, 0x9A, 0x22, 0xC0, 0x7E,
    0x18, 0x00, 0xA8, 0xAA, 0xB7, 0x62, 0xA0, 0xF8, 0x30, 0x00, 0x8A, 0x9A,
    0x9B, 0x49, 0x01, 0xF0, 0x70, 0x00, 0xA8, 0xAA, 0xBB, 0x50, 0x81, 0xE0,
    0xF8, 0x00, 0xAA, 0xA9, 0x97, 0x56, 0x01, 0xC3, 0x8C, 0x00, 0xFF, 0xFF,
    0xFD, 0xD2, 0x03, 0x87, 0x06, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x03, 0x86,
    0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x8F, 0x0C, 0x80, 0x00, 0x07,
    0xFF, 0xFF, 0x1B, 0xFF, 0x9A, 0x40, 0x00, 0x0B, 0x83, 0x80, 0x66, 0x63,
    0xF5, 0x20, 0x00, 0x0E, 0xA3, 0xFC, 0xCC, 0x88, 0xEA, 0x10, 0x00, 0x0C,
    0x82, 0x11, 0xD9, 0x9C, 0x65, 0x8C, 0x00, 0x0F, 0x93, 0xFD, 0xF1, 0x0C,
    0x1B, 0x42, 0x00, 0x08, 0x2A, 0x30, 0xF3, 0x7C, 0x04, 0xB1, 0x00, 0x0A,
    0x13, 0x16, 0xE6, 0x0C, 0x03, 0x68, 0x00, 0x0E, 0x02, 0xCF, 0x6C, 0x5C,
    0x00, 0x94, 0x00, 0x0A, 0x06, 0x38, 0x19, 0x5C, 0x00, 0x6B, 0x00, 0x0C,
    0x07, 0x4E, 0x42, 0xAC, 0x00, 0x16, 0x00, 0x08, 0xC6, 0xA5, 0x9A, 0xAC,
    0x00, 0x09, 0x00, 0x08, 0xC2, 0xA5, 0x50, 0x0C, 0x00, 0x06, 0x00, 0x0B,
    0x02, 0x10, 0x10, 0x6C, 0x00, 0x01, 0x00, 0x0B, 0x31, 0xFF, 0xE0, 0x6C,
    0x00, 0x00, 0x00, 0x04, 0x30, 0x00, 0x01, 0x8C, 0x00, 0x00, 0x00, 0x04,
    0xC6, 0x00, 0x71, 0x9C, 0x00, 0x00, 0x00, 0x02, 0xC9, 0x55, 0x70, 0x38,
    0x00, 0x00, 0x00, 0x01, 0x87, 0x00, 0x21, 0xF0, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0x00,
    0x00, 0x00
};

// Real upstream level-menu header text, built as an explicit int array
// (matching gameBlockdude.c's own `dudeLevelMenuHeaderText`, which uses
// this EXACT same real text - both are this same author's own real
// "Level Menu" screen) since it embeds two of real Gamebuino's own low-
// ASCII icon glyphs (0x11/0x10 - left/right arrow glyphs) plus two real
// '\n' line breaks: "Level Menu\n\nLevel  <left-arrow>  <right-arrow>".
char whgLevelMenuHeaderText[24] =
{
    76, 101, 118, 101, 108, 32, 77, 101, 110, 117, 10, 10,
    76, 101, 118, 101, 108, 32, 32, 17, 32, 32, 16, 0
};

// -----------------------------------------------------------------------------
// Real upstream level data - all 22 real bundled levels, mechanically
// regenerated from the real upstream source - see this file's own header
// comment for exactly how and why (not hand-transcribed).
// -----------------------------------------------------------------------------

int whgMap1[138] =
{
    3, 18, 6, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 3, 3, 3, 2, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 3, 3, 3, 2, 2, 2, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 3, 3, 3, 2, 2, 2, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 0, 3, 3, 3, 2, 2, 2, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 3, 3, 3, 2, 2, 2,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 7,
    16, 254, 2, 79, 7, 25, 7, 254, 2, 25, 13, 79, 13, 254, 2, 79,
    19, 25, 19, 254, 2, 25, 25, 79, 25, 255,
};

int whgMap2[189] =
{
    3, 18, 6, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 2, 2, 2, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 7,
    16, 254, 2, 19, 1, 19, 31, 254, 2, 31, 1, 31, 31, 254, 2, 43,
    1, 43, 31, 254, 2, 55, 1, 55, 31, 254, 2, 67, 1, 67, 31, 254,
    2, 79, 1, 79, 31, 254, 2, 25, 31, 25, 1, 254, 2, 37, 31, 37,
    1, 254, 2, 49, 31, 49, 1, 254, 2, 61, 31, 61, 1, 254, 2, 73,
    31, 73, 1, 254, 2, 85, 31, 85, 1, 253, 52, 16, 255,
};

int whgMap3[155] =
{
    2, 4, 5, 1, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 1, 1,
    2, 2, 1, 1, 1, 1, 1, 10, 16, 254, 2, 1, 7, 19, 7, 19,
    25, 1, 25, 254, 2, 7, 7, 19, 7, 19, 25, 1, 25, 1, 7, 254,
    2, 11, 7, 19, 7, 19, 25, 1, 25, 1, 7, 254, 2, 19, 13, 19,
    25, 1, 25, 1, 7, 19, 7, 254, 2, 19, 19, 19, 25, 1, 25, 1,
    7, 19, 7, 254, 2, 19, 25, 1, 25, 1, 7, 19, 7, 254, 2, 13,
    25, 1, 25, 1, 7, 19, 7, 19, 25, 254, 2, 7, 25, 1, 25, 1,
    7, 19, 7, 19, 25, 254, 2, 1, 25, 1, 7, 19, 7, 19, 25, 254,
    2, 1, 19, 1, 7, 19, 7, 19, 25, 1, 25, 254, 2, 1, 13, 1,
    7, 19, 7, 19, 25, 1, 25, 253, 1, 1, 255,
};

int whgMap4[234] =
{
    3, 18, 8, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 2, 2, 2, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 2, 2, 2, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 7, 22, 254, 5, 19, 1, 19, 43, 254, 5, 31, 1, 31,
    43, 254, 5, 43, 1, 43, 43, 254, 5, 55, 1, 55, 43, 254, 5, 67,
    1, 67, 43, 254, 5, 79, 1, 79, 43, 254, 5, 25, 43, 25, 1, 254,
    5, 37, 43, 37, 1, 254, 5, 49, 43, 49, 1, 254, 5, 61, 43, 61,
    1, 254, 5, 73, 43, 73, 1, 254, 5, 85, 43, 85, 1, 253, 19, 1,
    253, 85, 1, 253, 19, 43, 253, 85, 43, 255,
};

int whgMap5[205] =
{
    3, 12, 10, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1,
    2, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0,
    0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1,
    1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 3, 3, 1,
    0, 0, 1, 0, 0, 1, 0, 0, 1, 3, 3, 1, 1, 1, 1, 0,
    0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0,
    1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1,
    1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 7, 7, 254, 2, 19,
    1, 19, 19, 1, 19, 1, 1, 254, 2, 19, 19, 19, 37, 1, 37, 1,
    19, 254, 2, 19, 37, 19, 55, 1, 55, 1, 37, 254, 2, 37, 1, 37,
    19, 55, 19, 55, 1, 254, 2, 37, 19, 37, 37, 55, 37, 55, 19, 254,
    2, 37, 37, 37, 55, 55, 55, 55, 37, 254, 2, 19, 7, 37, 7, 37,
    49, 19, 49, 253, 1, 55, 253, 55, 1, 253, 55, 55, 255,
};

int whgMap6[351] =
{
    4, 18, 10, 2, 2, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1,
    1, 1, 1, 1, 1, 2, 2, 0, 0, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1,
    1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0,
    0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 4, 4, 1, 1, 0,
    0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 4, 4, 1,
    1, 0, 0, 1, 1, 1, 1, 3, 3, 1, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 0, 0, 1, 1, 1, 1, 3, 3, 1, 1, 1, 1, 0,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 4, 4, 52, 40, 254, 2, 1, 13, 7,
    13, 7, 19, 1, 19, 254, 0, 7, 28, 254, 0, 1, 40, 254, 2, 7,
    55, 1, 55, 1, 49, 7, 49, 254, 0, 16, 13, 254, 0, 16, 49, 254,
    0, 25, 4, 254, 2, 31, 19, 25, 19, 25, 13, 31, 13, 254, 0, 25,
    40, 254, 2, 31, 55, 25, 55, 25, 49, 31, 49, 254, 0, 40, 7, 254,
    0, 34, 37, 254, 0, 43, 43, 254, 2, 55, 7, 49, 7, 49, 1, 55,
    1, 254, 0, 49, 16, 254, 2, 49, 31, 55, 31, 55, 16, 55, 31, 254,
    2, 73, 1, 79, 1, 79, 7, 73, 7, 254, 0, 79, 16, 254, 0, 73,
    28, 254, 2, 73, 37, 79, 37, 79, 43, 73, 43, 254, 0, 73, 52, 254,
    0, 88, 7, 254, 2, 79, 49, 94, 49, 94, 55, 94, 49, 254, 2, 103,
    7, 97, 7, 97, 1, 103, 1, 254, 0, 97, 16, 253, 100, 52, 255,
};

int whgMap7[210] =
{
    3, 9, 10, 0, 0, 2, 2, 2, 0, 3, 3, 3, 0, 0, 2, 2,
    2, 0, 3, 3, 3, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
    1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 19, 4, 254,
    1, 13, 19, 20, 19, 254, 1, 20, 25, 13, 25, 254, 1, 13, 31, 20,
    31, 254, 1, 20, 37, 13, 37, 254, 1, 7, 37, 0, 37, 254, 1, 0,
    43, 7, 43, 254, 1, 7, 49, 0, 49, 254, 1, 13, 49, 13, 56, 254,
    1, 19, 56, 19, 49, 254, 1, 25, 49, 25, 56, 254, 1, 31, 56, 31,
    49, 254, 1, 37, 49, 37, 56, 254, 1, 43, 49, 50, 49, 254, 1, 50,
    43, 43, 43, 254, 1, 43, 37, 50, 37, 254, 1, 30, 37, 37, 37, 254,
    1, 37, 31, 30, 31, 254, 1, 30, 25, 37, 25, 254, 1, 37, 19, 30,
    19, 255,
};

int whgMap8[178] =
{
    3, 10, 10, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0,
    0, 3, 3, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 2, 0, 0, 0, 0, 28, 52, 254, 3, 1, 13, 1, 43, 254,
    3, 7, 43, 7, 13, 254, 3, 13, 13, 13, 43, 254, 3, 19, 43, 19,
    13, 254, 3, 25, 13, 25, 43, 254, 3, 31, 43, 31, 13, 254, 3, 37,
    13, 37, 43, 254, 3, 43, 43, 43, 13, 254, 3, 49, 13, 49, 43, 254,
    3, 55, 43, 55, 13, 254, 3, 1, 25, 55, 25, 254, 3, 55, 31, 1,
    31, 255,
};

int whgMap9[326] =
{
    3, 20, 10, 2, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0, 1, 1, 1, 1, 1,
    1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0,
    1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1,
    1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1,
    1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1,
    1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 0, 1,
    1, 1, 1, 1, 1, 1, 0, 0, 3, 3, 3, 7, 4, 254, 3, 1,
    55, 1, 19, 254, 3, 7, 19, 7, 55, 254, 3, 13, 55, 13, 19, 254,
    4, 19, 55, 19, 43, 254, 3, 25, 55, 25, 1, 254, 3, 31, 1, 31,
    55, 254, 3, 37, 55, 37, 1, 254, 4, 43, 1, 43, 13, 254, 3, 49,
    55, 49, 1, 254, 3, 55, 1, 55, 55, 254, 3, 61, 55, 61, 1, 254,
    4, 67, 55, 67, 43, 254, 3, 73, 55, 73, 1, 254, 3, 79, 1, 79,
    55, 254, 3, 85, 55, 85, 1, 254, 4, 91, 1, 91, 13, 254, 4, 97,
    13, 97, 1, 254, 3, 103, 1, 103, 37, 254, 3, 109, 37, 109, 1, 254,
    3, 115, 1, 115, 37, 255,
};

int whgMap10[338] =
{
    3, 20, 6, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 2, 2, 1, 1, 1,
    1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 3, 3, 2,
    2, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1,
    1, 3, 3, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 4, 16, 254, 1, 13,
    1, 19, 1, 19, 7, 13, 7, 254, 1, 25, 1, 31, 1, 31, 7, 25,
    7, 254, 1, 13, 13, 19, 13, 19, 19, 13, 19, 254, 1, 25, 13, 31,
    13, 31, 19, 25, 19, 254, 1, 13, 25, 19, 25, 19, 31, 13, 31, 254,
    1, 25, 25, 31, 25, 31, 31, 25, 31, 254, 1, 37, 25, 43, 25, 43,
    31, 37, 31, 254, 1, 49, 1, 55, 1, 55, 7, 49, 7, 254, 1, 61,
    1, 67, 1, 67, 7, 61, 7, 254, 1, 49, 13, 55, 13, 55, 19, 49,
    19, 254, 1, 61, 13, 67, 13, 67, 19, 61, 19, 254, 1, 49, 25, 55,
    25, 55, 31, 49, 31, 254, 1, 61, 25, 67, 25, 67, 31, 61, 31, 254,
    1, 73, 1, 79, 1, 79, 7, 73, 7, 254, 1, 85, 1, 91, 1, 91,
    7, 85, 7, 254, 1, 97, 1, 103, 1, 103, 7, 97, 7, 254, 1, 85,
    13, 91, 13, 91, 19, 85, 19, 254, 1, 97, 13, 103, 13, 103, 19, 97,
    19, 254, 1, 85, 25, 91, 25, 91, 31, 85, 31, 254, 1, 97, 25, 103,
    25, 103, 31, 97, 31, 253, 31, 1, 253, 49, 1, 253, 67, 31, 253, 85,
    31, 255,
};

int whgMap11[262] =
{
    3, 16, 12, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 3, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 7, 7, 254, 0, 19, 13, 254, 0, 37, 19, 254, 0, 37,
    49, 254, 0, 37, 61, 254, 0, 73, 43, 254, 2, 25, 25, 79, 25, 254,
    2, 25, 43, 79, 43, 254, 2, 13, 1, 13, 55, 254, 2, 79, 67, 79,
    13, 254, 2, 1, 55, 79, 55, 254, 2, 91, 13, 13, 13, 254, 2, 37,
    58, 37, 67, 37, 13, 255,
};

int whgMap12[323] =
{
    3, 14, 4, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2,
    2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 4, 10, 254, 2, 13,
    1, 13, 19, 254, 2, 19, 19, 19, 1, 254, 2, 25, 1, 25, 19, 254,
    2, 31, 19, 31, 1, 254, 2, 37, 1, 37, 19, 254, 2, 43, 19, 43,
    1, 254, 2, 49, 1, 49, 19, 254, 2, 55, 19, 55, 1, 254, 2, 61,
    1, 61, 19, 254, 2, 67, 19, 67, 1, 253, 13, 1, 253, 19, 1, 253,
    25, 1, 253, 31, 1, 253, 37, 1, 253, 43, 1, 253, 49, 1, 253, 55,
    1, 253, 61, 1, 253, 67, 1, 253, 13, 7, 253, 19, 7, 253, 25, 7,
    253, 31, 7, 253, 37, 7, 253, 43, 7, 253, 49, 7, 253, 55, 7, 253,
    61, 7, 253, 67, 7, 253, 13, 13, 253, 19, 13, 253, 25, 13, 253, 31,
    13, 253, 37, 13, 253, 43, 13, 253, 49, 13, 253, 55, 13, 253, 61, 13,
    253, 67, 13, 253, 13, 19, 253, 19, 19, 253, 25, 19, 253, 31, 19, 253,
    37, 19, 253, 43, 19, 253, 49, 19, 253, 55, 19, 253, 61, 19, 253, 67,
    19, 253, 16, 4, 253, 22, 4, 253, 28, 4, 253, 34, 4, 253, 40, 4,
    253, 46, 4, 253, 52, 4, 253, 58, 4, 253, 64, 4, 253, 16, 10, 253,
    22, 10, 253, 28, 10, 253, 34, 10, 253, 40, 10, 253, 46, 10, 253, 52,
    10, 253, 58, 10, 253, 64, 10, 253, 16, 16, 253, 22, 16, 253, 28, 16,
    253, 34, 16, 253, 40, 16, 253, 46, 16, 253, 52, 16, 253, 58, 16, 253,
    64, 16, 255,
};

int whgMap13[422] =
{
    3, 16, 6, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 3, 3, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 4, 16, 254, 2, 13, 1, 43, 31, 73, 1, 79, 7, 55,
    31, 25, 1, 13, 13, 31, 31, 61, 1, 79, 19, 67, 31, 37, 1, 13,
    25, 19, 31, 49, 1, 79, 31, 49, 1, 19, 31, 13, 25, 37, 1, 67,
    31, 79, 19, 61, 1, 31, 31, 13, 13, 25, 1, 55, 31, 79, 7, 73,
    1, 43, 31, 254, 2, 79, 31, 49, 1, 19, 31, 13, 25, 37, 1, 67,
    31, 79, 19, 61, 1, 31, 31, 13, 13, 25, 1, 55, 31, 79, 7, 73,
    1, 43, 31, 13, 1, 43, 31, 73, 1, 79, 7, 55, 31, 25, 1, 13,
    13, 31, 31, 61, 1, 79, 19, 67, 31, 37, 1, 13, 25, 19, 31, 49,
    1, 254, 2, 13, 31, 43, 1, 73, 31, 79, 25, 55, 1, 25, 31, 13,
    19, 31, 1, 61, 31, 79, 13, 67, 1, 37, 31, 13, 7, 19, 1, 49,
    31, 79, 1, 49, 31, 19, 1, 13, 7, 37, 31, 67, 1, 79, 13, 61,
    31, 31, 1, 13, 19, 25, 31, 55, 1, 79, 25, 73, 31, 43, 1, 254,
    2, 79, 1, 49, 31, 19, 1, 13, 7, 37, 31, 67, 1, 79, 13, 61,
    31, 31, 1, 13, 19, 25, 31, 55, 1, 79, 25, 73, 31, 43, 1, 13,
    31, 43, 1, 73, 31, 79, 25, 55, 1, 25, 31, 13, 19, 31, 1, 61,
    31, 79, 13, 67, 1, 37, 31, 13, 7, 19, 1, 49, 31, 254, 3, 13,
    1, 13, 31, 254, 3, 19, 31, 19, 1, 254, 3, 25, 1, 25, 31, 254,
    3, 31, 31, 31, 1, 254, 3, 37, 1, 37, 31, 254, 3, 43, 31, 43,
    1, 254, 3, 49, 1, 49, 31, 254, 3, 55, 31, 55, 1, 254, 3, 61,
    1, 61, 31, 254, 3, 67, 31, 67, 1, 254, 3, 73, 1, 73, 31, 254,
    3, 79, 31, 79, 1, 255,
};

int whgMap14[265] =
{
    3, 20, 8, 3, 3, 3, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1,
    1, 1, 1, 0, 0, 1, 0, 3, 3, 3, 0, 1, 0, 1, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1,
    0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0,
    1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1,
    0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0,
    1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 2, 112, 40, 254, 2, 37, 1, 37, 31, 254, 2, 55, 1, 55,
    31, 254, 2, 73, 1, 73, 31, 254, 2, 91, 1, 91, 31, 254, 2, 109,
    1, 109, 31, 254, 2, 55, 1, 37, 1, 254, 2, 55, 19, 37, 19, 254,
    2, 73, 13, 55, 13, 254, 2, 73, 31, 55, 31, 254, 2, 91, 1, 73,
    1, 254, 2, 91, 19, 73, 19, 254, 2, 109, 13, 91, 13, 254, 2, 109,
    31, 91, 31, 253, 49, 7, 253, 49, 25, 253, 67, 19, 253, 85, 7, 253,
    85, 25, 253, 103, 19, 253, 109, 7, 255,
};

int whgMap15[243] =
{
    3, 18, 8, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3,
    3, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 3, 3, 4, 4, 254, 4, 13, 1, 13, 43, 254, 2, 19, 1, 19,
    43, 254, 1, 25, 1, 25, 43, 254, 4, 31, 1, 31, 43, 254, 2, 37,
    1, 37, 43, 254, 1, 43, 1, 43, 43, 254, 4, 49, 1, 49, 43, 254,
    2, 55, 1, 55, 43, 254, 1, 61, 1, 61, 43, 254, 4, 67, 1, 67,
    43, 254, 2, 73, 1, 73, 43, 254, 1, 79, 1, 79, 43, 254, 4, 85,
    1, 85, 43, 254, 2, 91, 1, 91, 43, 253, 13, 43, 253, 52, 22, 253,
    91, 1, 255,
};

int whgMap16[426] =
{
    3, 18, 10, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1,
    1, 1, 1, 1, 1, 2, 2, 1, 1, 0, 0, 0, 0, 1, 1, 0,
    0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1,
    1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0,
    0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0,
    0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1,
    1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0,
    0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 1, 1, 3, 3, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 3, 3, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 100, 4, 254, 2, 92, 8, 60, 8, 60,
    0, 92, 0, 254, 2, 60, 0, 92, 0, 92, 8, 60, 8, 254, 2, 68,
    20, 60, 20, 60, 12, 68, 12, 254, 2, 60, 12, 68, 12, 68, 20, 60,
    20, 254, 2, 92, 32, 60, 32, 60, 24, 92, 24, 254, 2, 60, 24, 92,
    24, 92, 32, 60, 32, 254, 2, 92, 44, 84, 44, 84, 36, 92, 36, 254,
    2, 84, 36, 92, 36, 92, 44, 84, 44, 254, 2, 92, 56, 60, 56, 60,
    48, 92, 48, 254, 2, 60, 48, 92, 48, 92, 56, 60, 56, 254, 2, 56,
    56, 48, 56, 48, 48, 56, 48, 254, 2, 48, 48, 56, 48, 56, 56, 48,
    56, 254, 2, 44, 56, 36, 56, 36, 24, 44, 24, 254, 2, 36, 24, 44,
    24, 44, 56, 36, 56, 254, 2, 44, 20, 36, 20, 36, 12, 44, 12, 254,
    2, 36, 12, 44, 12, 44, 20, 36, 20, 254, 2, 44, 8, 12, 8, 12,
    0, 44, 0, 254, 2, 12, 0, 44, 0, 44, 8, 12, 8, 254, 2, 8,
    8, 0, 8, 0, 0, 8, 0, 254, 2, 0, 0, 8, 0, 8, 8, 0,
    8, 254, 2, 8, 44, 8, 12, 0, 12, 0, 44, 254, 2, 0, 12, 0,
    44, 8, 44, 8, 12, 254, 2, 8, 56, 8, 48, 0, 48, 0, 56, 254,
    2, 0, 48, 0, 56, 8, 56, 8, 48, 255,
};

int whgMap17[362] =
{
    3, 20, 10, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 4, 52, 254, 3, 1,
    43, 1, 13, 254, 3, 7, 13, 7, 43, 254, 3, 13, 43, 13, 13, 254,
    3, 19, 13, 19, 43, 254, 3, 25, 43, 25, 13, 254, 3, 31, 13, 31,
    43, 254, 3, 37, 43, 37, 13, 254, 3, 43, 13, 43, 43, 254, 3, 49,
    43, 49, 13, 254, 3, 55, 13, 55, 43, 254, 3, 61, 43, 61, 13, 254,
    3, 67, 13, 67, 43, 254, 3, 73, 43, 73, 13, 254, 3, 79, 13, 79,
    43, 254, 3, 85, 43, 85, 13, 254, 3, 91, 13, 91, 43, 254, 3, 97,
    43, 97, 13, 254, 3, 103, 13, 103, 43, 254, 3, 109, 43, 109, 13, 254,
    3, 115, 13, 115, 43, 254, 3, 115, 13, 1, 13, 254, 3, 1, 19, 115,
    19, 254, 3, 115, 25, 1, 25, 254, 3, 1, 31, 115, 31, 254, 3, 115,
    37, 1, 37, 254, 3, 1, 43, 115, 43, 255,
};

int whgMap18[374] =
{
    2, 20, 10, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
    0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 58, 46, 254, 2, 49,
    44, 49, 30, 254, 2, 43, 24, 43, 38, 254, 2, 37, 32, 37, 18, 254,
    2, 31, 12, 31, 26, 254, 2, 25, 20, 25, 6, 254, 2, 67, 30, 67,
    44, 254, 2, 73, 38, 73, 24, 254, 2, 79, 18, 79, 32, 254, 2, 85,
    26, 85, 12, 254, 2, 91, 6, 91, 20, 254, 2, 1, 43, 1, 55, 254,
    2, 7, 55, 7, 43, 254, 2, 13, 43, 13, 55, 254, 2, 19, 55, 19,
    43, 254, 2, 25, 43, 25, 55, 254, 2, 31, 55, 31, 43, 254, 2, 37,
    43, 37, 55, 254, 2, 43, 55, 43, 43, 254, 2, 73, 55, 73, 43, 254,
    2, 79, 43, 79, 55, 254, 2, 85, 55, 85, 43, 254, 2, 91, 43, 91,
    55, 254, 2, 97, 55, 97, 43, 254, 2, 103, 43, 103, 55, 254, 2, 109,
    55, 109, 43, 254, 2, 115, 43, 115, 55, 253, 16, 4, 253, 100, 4, 253,
    1, 49, 253, 115, 49, 255,
};

int whgMap19[605] =
{
    4, 20, 10, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 4, 4, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 4,
    4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 3, 3, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 2, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 2,
    2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 4, 40, 254, 1, 13,
    13, 13, 1, 254, 1, 13, 19, 13, 7, 254, 1, 13, 25, 13, 13, 254,
    1, 13, 43, 13, 31, 254, 1, 13, 49, 13, 37, 254, 1, 13, 55, 13,
    43, 254, 2, 19, 43, 19, 1, 254, 2, 19, 49, 19, 7, 254, 2, 19,
    55, 19, 13, 254, 2, 25, 19, 25, 1, 254, 2, 25, 37, 25, 19, 254,
    2, 25, 55, 25, 37, 254, 1, 31, 1, 31, 13, 254, 1, 31, 7, 31,
    19, 254, 1, 31, 13, 31, 25, 254, 1, 31, 31, 31, 43, 254, 1, 31,
    37, 31, 49, 254, 1, 31, 43, 31, 55, 254, 1, 37, 1, 37, 13, 254,
    1, 37, 7, 37, 19, 254, 1, 37, 13, 37, 25, 254, 1, 37, 31, 37,
    43, 254, 1, 37, 37, 37, 49, 254, 1, 37, 43, 37, 55, 254, 2, 43,
    43, 43, 1, 254, 2, 43, 49, 43, 7, 254, 2, 43, 55, 43, 13, 254,
    2, 49, 1, 49, 43, 254, 2, 49, 7, 49, 49, 254, 2, 49, 13, 49,
    55, 254, 2, 55, 1, 55, 19, 254, 2, 55, 19, 55, 37, 254, 2, 55,
    37, 55, 55, 254, 2, 61, 19, 61, 1, 254, 2, 61, 37, 61, 19, 254,
    2, 61, 55, 61, 37, 254, 1, 67, 1, 67, 13, 254, 1, 67, 7, 67,
    19, 254, 1, 67, 13, 67, 25, 254, 1, 67, 31, 67, 43, 254, 1, 67,
    37, 67, 49, 254, 1, 67, 43, 67, 55, 254, 2, 73, 43, 73, 1, 254,
    2, 73, 49, 73, 7, 254, 2, 73, 55, 73, 13, 254, 1, 79, 1, 79,
    13, 254, 1, 79, 7, 79, 19, 254, 1, 79, 13, 79, 25, 254, 1, 79,
    31, 79, 43, 254, 1, 79, 37, 79, 49, 254, 1, 79, 43, 79, 55, 254,
    2, 85, 1, 85, 19, 254, 2, 85, 19, 85, 37, 254, 2, 85, 37, 85,
    55, 254, 2, 91, 1, 91, 43, 254, 2, 91, 7, 91, 49, 254, 2, 91,
    13, 91, 55, 254, 2, 97, 43, 97, 1, 254, 2, 97, 49, 97, 7, 254,
    2, 97, 55, 97, 13, 254, 1, 103, 1, 103, 13, 254, 1, 103, 7, 103,
    19, 254, 1, 103, 13, 103, 25, 254, 1, 103, 31, 103, 43, 254, 1, 103,
    37, 103, 49, 254, 1, 103, 43, 103, 55, 253, 112, 28, 255,
};

int whgMap20[546] =
{
    4, 18, 12, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
    3, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 4, 64, 254, 2, 13,
    1, 13, 25, 254, 2, 19, 25, 19, 1, 254, 2, 25, 1, 25, 25, 254,
    2, 31, 25, 31, 1, 254, 2, 37, 1, 37, 25, 254, 2, 43, 25, 43,
    1, 254, 2, 49, 1, 49, 25, 254, 2, 55, 25, 55, 1, 254, 2, 61,
    1, 61, 25, 254, 2, 67, 25, 67, 1, 254, 2, 73, 1, 73, 25, 254,
    2, 79, 25, 79, 1, 254, 2, 85, 1, 85, 25, 254, 2, 91, 25, 91,
    1, 254, 2, 97, 1, 97, 25, 254, 2, 13, 67, 13, 43, 254, 2, 19,
    43, 19, 67, 254, 2, 25, 67, 25, 43, 254, 2, 31, 43, 31, 67, 254,
    2, 37, 67, 37, 43, 254, 2, 43, 43, 43, 67, 254, 2, 49, 67, 49,
    43, 254, 2, 55, 43, 55, 67, 254, 2, 61, 67, 61, 43, 254, 2, 67,
    43, 67, 67, 254, 2, 73, 67, 73, 43, 254, 2, 79, 43, 79, 67, 254,
    2, 85, 67, 85, 43, 254, 2, 91, 43, 91, 67, 254, 2, 97, 67, 97,
    43, 254, 2, 37, 49, 13, 49, 254, 2, 37, 55, 13, 55, 254, 2, 37,
    61, 13, 61, 254, 2, 37, 67, 13, 67, 254, 2, 67, 43, 43, 43, 254,
    2, 67, 49, 43, 49, 254, 2, 67, 55, 43, 55, 254, 2, 67, 61, 43,
    61, 254, 2, 97, 49, 73, 49, 254, 2, 97, 55, 73, 55, 254, 2, 97,
    61, 73, 61, 254, 2, 97, 67, 73, 67, 254, 2, 13, 1, 37, 1, 254,
    2, 13, 7, 37, 7, 254, 2, 13, 13, 37, 13, 254, 2, 13, 25, 37,
    25, 254, 2, 43, 1, 67, 1, 254, 2, 43, 13, 67, 13, 254, 2, 43,
    19, 67, 19, 254, 2, 43, 25, 67, 25, 254, 2, 73, 1, 97, 1, 254,
    2, 73, 7, 97, 7, 254, 2, 73, 13, 97, 13, 254, 2, 73, 25, 97,
    25, 255,
};

int whgMap21[350] =
{
    3, 20, 10, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 2, 2, 3, 3, 3, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 109, 4, 254, 3, 13,
    55, 13, 19, 254, 3, 19, 19, 19, 55, 254, 3, 25, 55, 25, 19, 254,
    3, 31, 19, 31, 55, 254, 3, 37, 55, 37, 19, 254, 3, 43, 19, 43,
    55, 254, 3, 49, 55, 49, 19, 254, 3, 55, 19, 55, 55, 254, 3, 61,
    55, 61, 19, 254, 3, 67, 19, 67, 55, 254, 3, 73, 55, 73, 19, 254,
    3, 79, 19, 79, 55, 254, 3, 85, 55, 85, 19, 254, 3, 91, 19, 91,
    55, 254, 3, 97, 55, 97, 19, 254, 3, 103, 19, 103, 55, 254, 3, 103,
    19, 13, 19, 254, 3, 13, 25, 103, 25, 254, 3, 103, 31, 13, 31, 254,
    3, 13, 37, 103, 37, 254, 3, 103, 43, 13, 43, 254, 3, 13, 49, 103,
    49, 254, 3, 103, 55, 13, 55, 253, 13, 55, 253, 103, 55, 255,
};

int whgMap22[605] =
{
    2, 20, 12, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
    2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 4, 64, 254, 1, 1, 55, 1, 1, 254, 1, 7, 1, 7,
    55, 254, 1, 13, 55, 13, 1, 254, 1, 19, 1, 19, 55, 254, 1, 25,
    55, 25, 1, 254, 1, 31, 1, 31, 55, 254, 1, 37, 55, 37, 1, 254,
    1, 43, 1, 43, 55, 254, 1, 49, 55, 49, 1, 254, 1, 55, 1, 55,
    55, 254, 1, 61, 55, 61, 1, 254, 1, 67, 1, 67, 55, 254, 1, 73,
    55, 73, 1, 254, 1, 79, 1, 79, 55, 254, 1, 85, 55, 85, 1, 254,
    1, 91, 1, 91, 55, 254, 1, 97, 55, 97, 1, 254, 1, 103, 1, 103,
    55, 254, 1, 109, 55, 109, 1, 254, 1, 115, 1, 115, 55, 254, 3, 1,
    55, 1, 7, 109, 7, 109, 55, 254, 3, 1, 49, 1, 1, 109, 1, 109,
    49, 254, 3, 7, 49, 7, 1, 115, 1, 115, 49, 254, 3, 7, 55, 7,
    7, 115, 7, 115, 55, 254, 3, 4, 52, 4, 4, 112, 4, 112, 52, 254,
    3, 109, 7, 109, 55, 1, 55, 1, 7, 254, 3, 109, 1, 109, 49, 1,
    49, 1, 1, 254, 3, 115, 1, 115, 49, 7, 49, 7, 1, 254, 3, 115,
    7, 115, 55, 7, 55, 7, 7, 254, 3, 112, 4, 112, 52, 4, 52, 4,
    4, 254, 3, 13, 43, 13, 19, 97, 19, 97, 43, 254, 3, 13, 37, 13,
    13, 97, 13, 97, 37, 254, 3, 19, 37, 19, 13, 103, 13, 103, 37, 254,
    3, 19, 43, 19, 19, 103, 19, 103, 43, 254, 3, 16, 40, 16, 16, 100,
    16, 100, 40, 254, 3, 97, 19, 97, 43, 13, 43, 13, 19, 254, 3, 97,
    13, 97, 37, 13, 37, 13, 13, 254, 3, 103, 13, 103, 37, 19, 37, 19,
    13, 254, 3, 103, 19, 103, 43, 19, 43, 19, 19, 254, 3, 100, 16, 100,
    40, 16, 40, 16, 16, 254, 3, 25, 31, 85, 31, 254, 3, 25, 25, 85,
    25, 254, 3, 31, 25, 91, 25, 254, 3, 31, 31, 91, 31, 254, 3, 28,
    28, 88, 28, 253, 4, 4, 253, 112, 4, 253, 112, 52, 255,
};

// Direct equivalent of upstream's own real `const byte *gamemaps[]`
// pointer table - implemented as an if-chain returning a pointer instead
// of an actual `int*[22]` array declaration, matching gameBlockdude.c's
// own `dudeGetMapData()` precedent exactly (sidesteps any need to confirm
// pointer-array declaration syntax in this dialect at all).
int* whgGetMapData( int level )
{
    if( level == 0 ) return whgMap1;
    if( level == 1 ) return whgMap2;
    if( level == 2 ) return whgMap3;
    if( level == 3 ) return whgMap4;
    if( level == 4 ) return whgMap5;
    if( level == 5 ) return whgMap6;
    if( level == 6 ) return whgMap7;
    if( level == 7 ) return whgMap8;
    if( level == 8 ) return whgMap9;
    if( level == 9 ) return whgMap10;
    if( level == 10 ) return whgMap11;
    if( level == 11 ) return whgMap12;
    if( level == 12 ) return whgMap13;
    if( level == 13 ) return whgMap14;
    if( level == 14 ) return whgMap15;
    if( level == 15 ) return whgMap16;
    if( level == 16 ) return whgMap17;
    if( level == 17 ) return whgMap18;
    if( level == 18 ) return whgMap19;
    if( level == 19 ) return whgMap20;
    if( level == 20 ) return whgMap21;
    return whgMap22;
}

// -----------------------------------------------------------------------------
// Coin / Enemy helpers - direct ports of upstream's own real `Coin`/`Enemy`
// class methods (see this file's own header comment on why these became
// plain structs + fixed arrays instead of heap-allocated classes).
// -----------------------------------------------------------------------------

void whgCoinInit( int i, int x, int y )
{
    whgCoins[ i ].x = x;
    whgCoins[ i ].y = y;
    whgCoins[ i ].have = 0;
}

// Direct port of upstream's own real `Coin::draw()`.
void whgCoinDraw( int i )
{
    if( whgCoins[ i ].have > 0 ) return;

    if( gbCollideRectRect( whgCoins[ i ].x, whgCoins[ i ].y, 4, 4, whgPlayerX, whgPlayerY, 4, 4 ) )
    {
        whgCoins[ i ].have = 1;
        gbPlayTick();
    }

    int drawX = whgCoins[ i ].x + whgMapX;
    int drawY = whgCoins[ i ].y + whgMapY;
    if( ( drawX > 86 ) || ( drawY > 68 ) || ( drawX < -4 ) || ( drawY < -4 ) ) return;

    // real upstream draws a rounded-corner outline here, via the real
    // gbDrawRoundRect() shim primitive (see this file's own header comment).
    gbDrawRoundRect( drawX, drawY, 4, 4, 1 );
}

void whgCoinReset( int i )
{
    if( whgCoins[ i ].have != 2 )
      whgCoins[ i ].have = 0;
}

void whgCoinStick( int i )
{
    if( whgCoins[ i ].have == 1 )
      whgCoins[ i ].have = 2;
}

bool whgCoinHave( int i )
{
    return whgCoins[ i ].have > 0;
}

void whgEnemyInit( int i, int speed, int* pts )
{
    whgEnemies[ i ].x = pts[ 0 ];
    whgEnemies[ i ].y = pts[ 1 ];
    whgEnemies[ i ].s = speed;
    whgEnemies[ i ].points = pts;
    whgEnemies[ i ].startPoints = pts;
}

// Direct port of upstream's own real `Enemy::update()`.
void whgEnemyUpdate( int i )
{
    if( whgEnemies[ i ].s == 0 ) return; // stationary - no need to update

    int nx = whgEnemies[ i ].points[ 0 ];
    int ny = whgEnemies[ i ].points[ 1 ];
    int x = whgEnemies[ i ].x;
    int y = whgEnemies[ i ].y;
    int s = whgEnemies[ i ].s;

    if( nx > x )
    {
        x = x + s;
        if( nx < x ) x = nx;
    }
    if( nx < x )
    {
        x = x - s;
        if( nx > x ) x = nx;
    }
    if( ny > y )
    {
        y = y + s;
        if( ny < y ) y = ny;
    }
    if( ny < y )
    {
        y = y - s;
        if( ny > y ) y = ny;
    }

    whgEnemies[ i ].x = x;
    whgEnemies[ i ].y = y;

    if( ( nx == x ) && ( ny == y ) )
    {
        whgEnemies[ i ].points = whgEnemies[ i ].points + 2;
        int tmp = whgEnemies[ i ].points[ 0 ];
        if( ( tmp == WHG_MAPFLAGENEMY ) || ( tmp == WHG_MAPFLAGCOIN ) || ( tmp == WHG_MAPFLAGEND ) )
          whgEnemies[ i ].points = whgEnemies[ i ].startPoints;
    }
}

// Direct port of upstream's own real `Enemy::draw()`.
void whgEnemyDraw( int i )
{
    int x = whgEnemies[ i ].x;
    int y = whgEnemies[ i ].y;

    if( gbCollideRectRect( x, y, 4, 4, whgPlayerX, whgPlayerY, 4, 4 ) )
      whgDead = true;

    int drawX = x + whgMapX;
    int drawY = y + whgMapY;
    if( ( drawX > 86 ) || ( drawY > 68 ) || ( drawX < -4 ) || ( drawY < -4 ) ) return;

    gbFillRect( drawX, drawY + 1, 4, 2 );
    gbFillRect( drawX + 1, drawY, 2, 4 );
}

// -----------------------------------------------------------------------------
// Level loading / drawing / player state
// -----------------------------------------------------------------------------

void whgResetPlayer()
{
    whgDead = false;
    whgPlayerX = whgSpawnPoints[ 0 ];
    whgPlayerY = whgSpawnPoints[ 1 ];

    int i;
    for( i = 0; i < whgNumCoins; i = i + 1 )
      whgCoinReset( i );
}

// Direct port of upstream's own real `loadMap()` - see this file's own
// header comment for the real byte-skipping parsing quirk preserved here.
void whgLoadMap()
{
    whgMapData = whgGetMapData( whgCurLevel );
    whgNumEnemies = 0;
    whgNumCoins = 0;
    whgMapWidth = whgMapData[ 1 ];
    whgMapHeight = whgMapData[ 2 ];

    int i = WHG_MAPHEADER + whgMapWidth * whgMapHeight;
    whgSpawnPoints = whgMapData + i;

    while( whgMapData[ i ] != WHG_MAPFLAGEND )
    {
        int flag = whgMapData[ i ];
        i = i + 1;

        if( flag == WHG_MAPFLAGENEMY )
        {
            int speed = whgMapData[ i ];
            i = i + 1;
            whgEnemyInit( whgNumEnemies, speed, whgMapData + i );
            whgNumEnemies = whgNumEnemies + 1;
        }
        else if( flag == WHG_MAPFLAGCOIN )
        {
            int cx = whgMapData[ i ];
            i = i + 1;
            int cy = whgMapData[ i ];
            whgCoinInit( whgNumCoins, cx, cy );
            whgNumCoins = whgNumCoins + 1;
        }
    }

    whgWinTile = whgMapData[ 0 ];
    whgCurSavePoint = 2;

    whgResetPlayer();
}

// Direct port of upstream's own real `getTileAtPos()` - see this file's
// own header comment on the real dead-lower-bound-check-becomes-live
// (but still harmless) nuance from porting real unsigned bytes to signed
// ints.
int whgGetTileAtPos( int xoffset, int yoffset )
{
    if( ( whgPlayerY + yoffset < 0 ) || ( whgPlayerX + xoffset < 0 ) ||
        ( whgPlayerY + yoffset >= whgMapHeight * WHG_MAPTILESIZE ) ||
        ( whgPlayerX + xoffset >= whgMapWidth * WHG_MAPTILESIZE ) )
      return 0;

    int tile = whgMapData[ ( ( whgPlayerY + yoffset ) / WHG_MAPTILESIZE ) * whgMapWidth +
                            ( ( whgPlayerX + xoffset ) / WHG_MAPTILESIZE ) + WHG_MAPHEADER ];

    whgPotentialWin = whgPotentialWin || ( tile == whgWinTile );

    if( ( tile > whgCurSavePoint ) && ( !whgPotentialWin ) )
    {
        whgSpawnPoints = whgSpawnPoints + 2 * ( tile - whgCurSavePoint );
        whgCurSavePoint = whgCurSavePoint + ( tile - whgCurSavePoint );

        int i;
        for( i = 0; i < whgNumCoins; i = i + 1 )
          whgCoinStick( i );
    }

    return tile;
}

// Direct port of upstream's own real `drawWorld()`.
void whgDrawWorld()
{
    if( whgPlayerX + whgMapX < 20 ) whgMapX = whgMapX + 1;
    if( whgPlayerX + whgMapX > 64 ) whgMapX = whgMapX - 1;
    if( whgPlayerY + whgMapY < 10 ) whgMapY = whgMapY + 1;
    if( whgPlayerY + whgMapY > 38 ) whgMapY = whgMapY - 1;

    gbFillScreen( 1 ); // BLACK
    gbSetColor( 0 );   // WHITE

    int x, y;
    for( y = 0; y < whgMapHeight; y = y + 1 )
    {
        for( x = 0; x < whgMapWidth; x = x + 1 )
        {
            int tile = whgMapData[ y * whgMapWidth + x + WHG_MAPHEADER ];
            int drawX = x * WHG_MAPTILESIZE + whgMapX;
            int drawY = y * WHG_MAPTILESIZE + whgMapY;
            if( ( drawX > 86 ) || ( drawY > 68 ) || ( drawX < -4 ) || ( drawY < -4 ) ) continue;

            if( tile == 1 ) gbFillRect( drawX, drawY, WHG_MAPTILESIZE, WHG_MAPTILESIZE );
            else if( tile != 0 ) gbDrawBitmap( drawX, drawY, whgCheckersBitmap );
        }
    }

    // still WHITE from the loop above - matches upstream's own real
    // "no setColor() call between the wall loop and this fillRect" order.
    gbFillRect( whgPlayerX + whgMapX + 1, whgPlayerY + whgMapY + 1, 2, 2 );
    gbSetColor( 1 ); // BLACK
    gbDrawRect( whgPlayerX + whgMapX, whgPlayerY + whgMapY, 4, 4 );

    int i;
    for( i = 0; i < whgNumEnemies; i = i + 1 )
    {
        if( !whgFrameskip ) whgEnemyUpdate( i );
        whgEnemyDraw( i );
    }
    for( i = 0; i < whgNumCoins; i = i + 1 )
      whgCoinDraw( i );

    gbCursorX = 1;
    gbCursorY = 1;
    gbSetColorBg( 0, 1 ); // WHITE ink, BLACK background
    gbPrintNumber( whgTries );

    whgFrameskip = !whgFrameskip;
}

// -----------------------------------------------------------------------------
// States - see this file's own header comment for the real
// doTitleScreen()/doMainMenu() transition logic this mirrors.
// -----------------------------------------------------------------------------

void whgBeginTitle()
{
    whgState = WHG_STATE_TITLE;
}

void whgBeginLevelMenu()
{
    whgLevelPick = whgCurLevel + 1;
    whgState = WHG_STATE_LEVELMENU;
}

void whgUpdateTitle()
{
    gbSetColor( 1 );
    gbCursorX = 28;
    gbCursorY = 0;
    gbPrintString( "PRESS A" );
    gbDrawBitmap( 10, 8, whgLogoBitmap );

    if( gbPressed( BTN_A ) )
      whgBeginLevelMenu();
}

// Merged real upstream `drawLevelMenu()`/`refreshLevelMenu()` into one
// function, matching gameBlockdude.c's own `dudeDrawLevelMenu()` precedent
// (this shim always fully redraws every tick, so the real incremental
// erase-and-redraw split isn't needed here - see this file's own header
// comment on the real dead "erase old moves display" code this drops).
void whgDrawLevelMenu()
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( whgLevelMenuHeaderText );

    gbCursorX = 32;
    gbCursorY = 12;
    if( whgLevelPick < 10 )
      gbPrintString( " " );
    gbPrintNumber( whgLevelPick );

    // Fixed here, not preserved - see this file's own header comment for
    // the real "fresh EEPROM byte decodes as 255, which is > 0" bug this
    // replaces (every level falsely showed as already completed on a
    // fresh save). A fresh-cell sentinel (255) is normalized back to 0
    // right after reading, matching this project's own established EEPROM
    // convention.
    int done = eeprom_read_byte( whgLevelPick - 1 );
    if( done == 255 ) done = 0;

    gbSetColor( 1 );
    if( done > 0 ) gbDrawBitmap( 48, 11, whgOkBitmap );
    else gbDrawBitmap( 48, 11, whgKoBitmap );
}

// Direct port of upstream's own real `chooseLevel()`.
void whgUpdateLevelMenu()
{
    if( gbPressed( BTN_C ) )
    {
        whgBeginTitle();
        return;
    }

    if( gbPressed( BTN_RIGHT ) )
    {
        whgLevelPick = whgLevelPick + 1;
        if( whgLevelPick > WHG_NUM_LEVELS ) whgLevelPick = WHG_NUM_LEVELS;
    }
    if( gbPressed( BTN_LEFT ) )
    {
        whgLevelPick = whgLevelPick - 1;
        if( whgLevelPick < 1 ) whgLevelPick = 1;
    }

    if( gbPressed( BTN_A ) )
    {
        whgCurLevel = whgLevelPick - 1;
        whgLoadMap();
        whgTries = 0; // matches upstream's own real doMainMenu() - see header comment
        whgState = WHG_STATE_PLAY;
        return;
    }

    whgDrawLevelMenu();
}

// Direct port of upstream's own real `loop()`'s gameplay body - see this
// file's own header comment for the two real preserved quirks around
// `whgTries` and the "still holding C" early-menu-yank behavior.
void whgUpdatePlay()
{
    whgPotentialWin = false;

    if( gbRepeat( BTN_UP, 0 ) )
    {
        whgPlayerY = whgPlayerY - 1;
        if( ( whgGetTileAtPos( 0, 0 ) == 0 ) || ( whgGetTileAtPos( 3, 0 ) == 0 ) )
          whgPlayerY = whgPlayerY + 1;
    }
    if( gbRepeat( BTN_DOWN, 0 ) )
    {
        whgPlayerY = whgPlayerY + 1;
        if( ( whgGetTileAtPos( 0, 3 ) == 0 ) || ( whgGetTileAtPos( 3, 3 ) == 0 ) )
          whgPlayerY = whgPlayerY - 1;
    }
    if( gbRepeat( BTN_LEFT, 0 ) )
    {
        whgPlayerX = whgPlayerX - 1;
        if( ( whgGetTileAtPos( 0, 0 ) == 0 ) || ( whgGetTileAtPos( 0, 3 ) == 0 ) )
          whgPlayerX = whgPlayerX + 1;
    }
    if( gbRepeat( BTN_RIGHT, 0 ) )
    {
        whgPlayerX = whgPlayerX + 1;
        if( ( whgGetTileAtPos( 3, 0 ) == 0 ) || ( whgGetTileAtPos( 3, 3 ) == 0 ) )
          whgPlayerX = whgPlayerX - 1;
    }

    whgDrawWorld();

    if( whgDead )
    {
        whgTries = whgTries + 1;
        gbPlayCancel();
        whgResetPlayer();
    }
    else if( whgPotentialWin )
    {
        bool win = true;
        int i;
        for( i = 0; i < whgNumCoins; i = i + 1 )
          win = win && whgCoinHave( i );

        if( win )
        {
            gbPlayOK();
            eeprom_write_byte( whgCurLevel, 1 );
            whgCurLevel = whgCurLevel + 1;

            if( whgCurLevel >= WHG_NUM_LEVELS )
            {
                whgCurLevel = whgCurLevel - 1;
                whgBeginLevelMenu();
                return; // matches upstream's own real blocking doTitleScreen(false) call - see header comment
            }

            whgLoadMap(); // whgTries deliberately NOT reset here - see header comment
        }
    }

    if( gbPressed( BTN_C ) )
      whgBeginLevelMenu();
}

void gameWhg_init()
{
    gbBegin();
    gbSetFrameRate( 30 ); // real upstream setFrameRate(30) override - see header comment

    whgCurLevel = 0;
    whgFrameskip = false;

    whgBeginTitle();
}

void gameWhg_update()
{
    if( !gbUpdate() ) return;

    if( whgState == WHG_STATE_TITLE ) whgUpdateTitle();
    else if( whgState == WHG_STATE_LEVELMENU ) whgUpdateLevelMenu();
    else whgUpdatePlay();

    gbRenderFrame();
}
