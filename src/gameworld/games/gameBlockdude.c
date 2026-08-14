#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Blockdude (Sorunome, license: none specified -
// github.com/Sorunome/blockdude-gamebuino). A Sokoban-style push/lift-block
// puzzle: walk a little character through 11 real bundled levels, picking
// up blocks (Button DOWN, facing one), carrying them, and dropping them
// (Button DOWN again) to build steps up to ledges you can't otherwise climb
// (Button UP, or automatically LEFT/RIGHT into a one-tile-high step),
// finally reaching the level's own door tile to win. A per-level move
// counter is saved to EEPROM (best score only) so the level-select menu can
// show a checkmark/cross icon plus the best move count for every level
// already beaten.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every global/function got a
// `dude`-prefixed name (this cartridge has no linker - every game shares
// one flat global namespace). Upstream's own real `Block` C++ class (x, y,
// islift fields + draw()/is()/lift()/put() methods, heap-allocated via
// `new`/`delete` into a `Block *blocks[70]` pointer array, re-allocated
// fresh every `loadLevel()` call) was flattened into a plain `DudeBlock`
// struct plus a fixed `DudeBlock[70] dudeBlocks` array (no dynamic
// allocation is used anywhere else in this project's own ported games
// either) - `dudeNumBlocks` simply resets to 0 on every `dudeLoadLevel()`
// call instead of upstream's own explicit `delete` loop, which is exactly
// equivalent in observable behavior (every reader of `dudeBlocks[]`
// everywhere in this file only ever iterates `0..dudeNumBlocks-1`, so
// stale entries past that index are always dead data, identical to how
// upstream's own freshly-`new`'d replacement objects were dead data the
// instant the old ones were `delete`d).
//
// REAL BITMAP ART PORTED: every one of upstream's own real
// `const byte NAME[] PROGMEM = { width, height, ... }` arrays (wall, door,
// block, dude_left, dude_right, ok, ko, and the 64x35 title-screen logo)
// was copied byte-for-byte into a plain `int[N] dudeXxxBitmap = { width,
// height, byte0, byte1, ... }` array below (this dialect's own `int[N]
// name` array-declaration order, not C's `int name[N]`) - the exact shape
// `gbDrawBitmap()` expects, no conversion needed since every one of
// upstream's own byte literals here was already `0x`-prefixed hex (no
// Arduino `B00000000`-style binary literals anywhere in this particular
// upstream source). Verified: header width/height bytes match each real
// array's own actual byte count (`ceil(width/8)*height`) for every one of
// the 8 bitmaps. No solid fill/mask layer is drawn underneath any of
// these upstream either (confirmed by re-reading every real
// `gb.display.drawBitmap(...)` call site directly - none are preceded by
// a `setColor(GRAY)`+mask-bitmap pair or a plain fillRect) - every sprite
// here really is a single self-contained outline bitmap on real hardware
// too, so none was needed here (see gameFlappyBirdo.c's own header comment
// for the two real cases in this project where that assumption was wrong
// and had to be fixed - checked for specifically here and confirmed not
// to apply).
//
// STATE MACHINE: upstream's own structure is a real, live-forever `loop()`
// containing THREE nested blocking calls - `chooseLevel()` (a level-select
// screen with its own internal `while(1)`), `playMap()` (gameplay, another
// `while(1)`, which can itself call `moveWorld()`, a THIRD nested `while(1)`
// entered on a Button A press to freely pan the camera and preview the
// whole level), plus `gb.titleScreen(logo)` (real Gamebuino's own blocking
// splash, shown once at boot and again whenever Button C is pressed from
// the level-select screen). All four were converted into an explicit
// DudeState enum (DUDE_STATE_TITLE / DUDE_STATE_LEVELMENU /
// DUDE_STATE_PLAY / DUDE_STATE_VIEWMAP), matching the "blocking loop ->
// explicit resumable state" treatment used throughout this project (see
// gamePong.c's own header comment). One real behavioral wrinkle from
// flattening a blocking sub-loop into a state: on real hardware, the
// instant `moveWorld()` returns (Button A released), `playMap()`'s own
// *same* already-in-progress tick immediately keeps going - checking the
// B-held-reset/C-quit/door-reached/falling-loop logic using button state
// current as of right that moment, all still within what was originally
// one single `gb.update()`-gated iteration. This port instead ends the
// PLAY tick the instant DUDE_STATE_VIEWMAP is entered/exited, so that
// remaining logic runs on the *next* real tick instead of later in the
// same one - at most one extra 1/20th-second frame of delay, invisible in
// practice since the player's grid position never changes while the
// camera-pan view is open, so every one of those checks evaluates
// identically either way. Real `Gamebuino::titleScreen()`'s own exact
// internal text/logo layout isn't reproduced (it's a real library
// internal, not upstream's own code to port) - this port draws the real
// logo bitmap directly (`dudeLogoBitmap`, centered horizontally at
// x=10, y=7) with this port's own "PRESS A" prompt above it, matching the
// same treatment gameFlappyBirdo.c already established for its own real
// `gb.titleScreen()` splash bitmap.
//
// `gb.display.persistence = true/false` (upstream sets `false` in
// `loadLevel()`, `true` in `drawLevelMenu()`) has no equivalent in this
// shim at all - every game here always fully redraws from a freshly
// cleared buffer every real tick (see this project's own CLAUDE.md
// "Thumbnail atlas, quit-confirmation dialog..." section for why that's
// safe: no game here has a stale-frame problem to guard against).
// Upstream's own level-menu `persistence=true` was specifically there so
// its own `refreshLevelMenu()` could redraw just the changed
// icon/move-count region (via two explicit `setColor(WHITE)`+`fillRect()`
// erase calls) without a full-screen redraw every tick - not needed here,
// so `dudeDrawLevelMenu()` below just redraws the whole screen unconditionally
// every tick instead, with no erase step at all (nothing is ever left over
// to erase, since the previous frame was already fully cleared).
//
// EEPROM: upstream's own real `EEPROM.read()/EEPROM.write()` calls (in
// `refreshLevelMenu()` and `loop()`) are ported through this shim's own
// `eeprom_read_word()`/`eeprom_write_word()` (see eepromShim.h) rather than
// two hand-rolled `eeprom_read_byte()`/`eeprom_write_byte()` calls each -
// a direct, mechanical equivalent, not a behavioral change: `eeprom_
// read_word(addr)` computes exactly `(read(addr)<<8)|read(addr+1)` and
// `eeprom_write_word(addr,v)` writes exactly `write(addr,(v>>8)&0xFF);
// write(addr+1,v&0xFF)`, bit-for-bit the same two-byte hi/lo split
// upstream's own `EEPROM.read(pos)*256 + EEPROM.read(pos+1)` /
// `EEPROM.write(pos, moves>>8); EEPROM.write(pos+1, moves&0xFF)` pairs
// already did by hand. Each level's own best-move-count lives at word
// address `level*2` (levels 0-10 use addresses 0-21, far under this
// shim's own 1024-byte-per-game EEPROM slot).
//
// A GENUINE UPSTREAM QUIRK, FOUND AND FIXED (not preserved):
// `eeprom_read_word()`/real `EEPROM.read()` both return 255 (0xFF) per
// byte for a genuinely never-written cell (matching real AVR EEPROM's own
// actual factory-erased state - see eepromShim.c's own header comment for
// why this shim deliberately matches that instead of defaulting to 0).
// That means a truly fresh two-byte word decodes as 65535, NOT 0 - real
// `refreshLevelMenu()`'s own check is only ever `moves > 0` (never
// `moves != 65535`), so upstream's own real level-select screen shows the
// "ok" checkmark icon AND a nonsense "Moves 65535" readout for a level
// that has genuinely never been completed, on a genuinely fresh
// EEPROM/memory card. On real hardware this is normally masked by
// leftover residue from whatever sketch last used that chip's EEPROM
// (rarely a clean, uniform 0xFF in practice) - but this shim's own
// per-game EEPROM slots are deliberately, deterministically blank (all
// 0xFF) the very first time any given save slot is ever claimed (see
// eepromShim.c), so this quirk reliably fired for real, for every unplayed
// level, the first time this cartridge was ever run on a given save slot -
// flagged as a genuine negative player-experience bug and fixed:
// `dudeDrawLevelMenu()` now normalizes a fresh-cell 65535 back to 0 right
// after reading it, so both the checkmark/cross icon and the move-count
// readout correctly treat an unplayed level as unplayed.
//
// A SECOND GENUINE UPSTREAM QUIRK, FOUND AND FIXED (not preserved): real
// `loop()`'s own level-auto-advance path (`if(moves>0){ ...; if(curlevel
// <11){curlevel++;continue;} }`) calls `loadLevel()` for the new level
// without ever resetting `doLift`/`liftBlock`. If a level is completed
// (walked onto the door tile) while still carrying a lifted block, that
// `doLift=true` state survives into the very next auto-advanced level
// untouched - `drawWorld()`'s own unconditional "if(doLift) draw a block
// above the player" branch then draws a phantom floating block above the
// player's head at the start of the new level, even though nothing has
// actually been lifted there yet; pressing DOWN to "drop" it would even
// call `dudeBlockPut()` on whatever real block object happens to occupy
// `dudeLiftBlock`'s own stale index in the *new* level's own freshly
// populated `dudeBlocks[]` array (or silently no-op if that index is past
// the new level's own real block count). Fixed in `dudeLevelComplete()`:
// `dudeDoLift`/`dudeLiftBlock` are now explicitly reset on the auto-advance
// path, matching every other level-load site in this file.
//
// A THIRD REAL BUG, FOUND AND FIXED - THE OFF-BY-ONE BOUNDS BUG: real
// `loop()`'s own auto-advance condition is `if(curlevel < 11)`, but
// `gamemaps[]` only ever has 11 real entries (valid indices 0-10) - so
// completing the actual LAST level (`curlevel==10`) still satisfies
// `10 < 11`, increments `curlevel` to a genuinely out-of-bounds 11, and
// the next `loadLevel()` call reads `gamemaps[11]`. On real AVR hardware
// this merely reads whatever garbage byte happens to sit in flash right
// after the `gamemaps[]` table itself (typically decoding as a
// permanently-unsolvable "phantom" level full of erased-flash 0xFF bytes,
// since no tile value 0xFF ever matches the real door tile 3) rather than
// crashing outright - but reading an out-of-bounds C array index in this
// dialect has no such "it's still just flash memory" safety net, so this
// port treats it as a genuine memory-safety bug rather than a preservable
// gameplay quirk. Fixed in `dudeLevelComplete()` below: the auto-advance
// condition is `dudeCurLevel < DUDE_NUM_LEVELS - 1` (i.e. `< 10`, not
// `< 11`) so completing the real final level correctly returns to the
// level-select menu instead of attempting to load a level that was never
// staged in the first place.
//
// `random()` is never called anywhere in this particular upstream source
// (level layouts are all fixed data, not generated) - so no
// `arand()`/`gbPickRandomSeed()` port was needed here at all, unlike most
// other games in this project.

#define DUDE_NUM_LEVELS 11
#define DUDE_MAX_BLOCKS 70

enum DudeState
{
    DUDE_STATE_TITLE     = 0,
    DUDE_STATE_LEVELMENU = 1,
    DUDE_STATE_PLAY      = 2,
    DUDE_STATE_VIEWMAP   = 3
};

int dudeState;

int dudeCurLevel;  // 0-based - upstream's own "curlevel"
int dudeLevelPick; // 1-based, level-select cursor - upstream's own "curPick"

int* dudeMapData;  // points at one of dudeMap1..dudeMap11 below
int dudeMapWidth;
int dudeMapHeight;
int dudeMapX;
int dudeMapY;
int dudeSavedMapX; // camera position saved/restored around DUDE_STATE_VIEWMAP
int dudeSavedMapY;

int dudePlayerX;
int dudePlayerY;
bool dudeLookLeft;

bool dudeDoLift;
int dudeLiftBlock; // index into dudeBlocks[] - see header comment on the carry-over-state quirk

int dudeMoves;

typedef struct
{
    int x;
    int y;
    bool islift;
} DudeBlock;
DudeBlock dudeBlocks[DUDE_MAX_BLOCKS];
int dudeNumBlocks;

// -----------------------------------------------------------------------------
// Real upstream sprite bitmaps - see this file's own header comment.
// -----------------------------------------------------------------------------

int dudeWallBitmap[10]= { 8, 8, 0xFB, 0xFB, 0x00, 0xFE, 0xFE, 0x00, 0xFB, 0xFB };
int dudeDoorBitmap[10]= { 8, 8, 0x7E, 0x42, 0x42, 0x42, 0x46, 0x42, 0x42, 0x7E };
int dudeBlockBitmap[10]= { 8, 8, 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF };
int dudeDudeLeftBitmap[10]= { 8, 8, 0x1C, 0x7E, 0x12, 0x22, 0x14, 0x2A, 0x08, 0x36 };
int dudeDudeRightBitmap[10]= { 8, 8, 0x38, 0x7E, 0x48, 0x44, 0x28, 0x54, 0x10, 0x6C };
int dudeOkBitmap[9]= { 8, 7, 0x02, 0x04, 0x88, 0x48, 0x50, 0x30, 0x20 }; // upstream's own comment: "from bub"
int dudeKoBitmap[9]= { 8, 7, 0x82, 0x44, 0x28, 0x10, 0x28, 0x44, 0x82 }; // upstream's own comment: "from bub"

// Real upstream title-screen logo, shown via `gb.titleScreen(logo)` -
// copied byte-for-byte, verified 8*35=280 real body bytes (64/8=8 bytes
// per row * 35 rows) plus the 2 real width/height header bytes = 282.
int dudeLogoBitmap[282]=
{
    64, 35,
    0xFF, 0x80, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x80, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xDD, 0x80, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x80, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF, 0x9F, 0x1F, 0xF8, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF,
    0x9F, 0x1F, 0xD8, 0xD8, 0xD8, 0xDD, 0xD8, 0xDB, 0x9B, 0x1B, 0xF8, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF,
    0x9F, 0x1F, 0xFF, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF, 0x9F, 0xFF, 0xFF, 0x80, 0xF8, 0xF8, 0xF8, 0xF8,
    0x1F, 0xF0, 0xDD, 0x80, 0xD8, 0xD8, 0xD8, 0xD8, 0x1B, 0xB0, 0xFF, 0x80, 0xF8, 0xF8, 0xF8, 0xF8,
    0x1F, 0xF0, 0xFF, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF, 0x9F, 0xFF, 0xF8, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF,
    0x9F, 0x1F, 0xD8, 0xD8, 0xD8, 0xDD, 0xD8, 0xDD, 0x9B, 0x1B, 0xF8, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF,
    0x9F, 0x1F, 0xFF, 0xF8, 0xF8, 0xFF, 0xF8, 0xFF, 0x9F, 0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xDD, 0x80, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0xFF, 0x80, 0x00, 0x01, 0xD8, 0x00,
    0x00, 0x00, 0xFF, 0x80, 0x00, 0x00, 0x4C, 0x22, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x13,
    0x14, 0x00, 0x00, 0x0F, 0xF1, 0xC0, 0x25, 0x09, 0x98, 0x3F, 0x00, 0x08, 0x13, 0xF0, 0x25, 0x8A,
    0x51, 0x21, 0x00, 0x08, 0x12, 0x40, 0x2C, 0xCA, 0x6E, 0x21, 0x00, 0x08, 0x12, 0x20, 0x18, 0x71,
    0x80, 0x21, 0x00, 0x08, 0x11, 0x40, 0x30, 0x00, 0x00, 0x23, 0x00, 0x08, 0x12, 0xA0, 0x40, 0x00,
    0x00, 0x21, 0x00, 0x08, 0x10, 0x80, 0x00, 0x00, 0x00, 0x21, 0x00, 0x0F, 0xF3, 0x60, 0x00, 0x00,
    0x00, 0x3F, 0xDF, 0xDF, 0xDF, 0xDC, 0x03, 0x7F, 0x7F, 0x7F, 0xDF, 0xDF, 0xDF, 0xDC, 0x03, 0x7F,
    0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFE, 0xFE, 0xFC, 0x03, 0xFB,
    0xFB, 0xFB, 0xFE, 0xFE, 0xFE, 0xFC, 0x03, 0xFB, 0xFB, 0xFB
};

// Real upstream level-menu header text - built as an explicit int array
// (like gameTaquin.c's own taqRestartText) since it embeds two of real
// Gamebuino's own low-ASCII icon glyphs (0x11/0x10, upstream's own real
// `\x11`/`\x10` hex escapes - left/right arrow glyphs) plus two real '\n'
// line breaks, none of which are plain printable-ASCII string-literal
// content: "Level Menu\n\nLevel  <left-arrow>  <right-arrow>".
char dudeLevelMenuHeaderText[24]=
{
    76, 101, 118, 101, 108, 32, 77, 101, 110, 117, 10, 10,
    76, 101, 118, 101, 108, 32, 32, 17, 32, 32, 16, 0
};

// -----------------------------------------------------------------------------
// Real upstream level data - all 11 real bundled levels, ported verbatim
// (width, height, then width*height tile bytes: 0=nothing, 1=block,
// 2=start, 3=door/end, 4=wall - matching upstream's own real comment).
// -----------------------------------------------------------------------------

int dudeMap1[122]=
{
    20, 6,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 4,
    4, 3, 0, 0, 4, 0, 0, 0, 4, 0, 1, 0, 4, 0, 1, 0, 2, 0, 0, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

int dudeMap2[222]=
{
    22, 10,
    0, 4, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0,
    4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0,
    4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0,
    4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 1, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 1, 0, 1, 1, 2, 0, 0, 4,
    0, 4, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    0, 0, 0, 0, 0, 4, 0, 0, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int dudeMap3[211]=
{
    19, 11,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 4, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0,
    4, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4,
    4, 0, 4, 4, 4, 0, 0, 0, 0, 2, 0, 0, 0, 4, 1, 0, 4, 4, 0,
    4, 0, 4, 0, 4, 0, 0, 0, 0, 4, 0, 0, 4, 4, 4, 4, 4, 0, 0,
    4, 0, 4, 0, 4, 1, 1, 0, 4, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0,
    4, 3, 4, 0, 4, 4, 4, 4, 4, 4, 0, 4, 4, 0, 0, 0, 0, 0, 0,
    4, 4, 4, 0, 4, 4, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0,
};

int dudeMap4[386]=
{
    24, 16,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0,
    0, 0, 0, 4, 4, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 0,
    0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 4, 4, 4,
    4, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0,
    4, 3, 0, 0, 0, 0, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 0, 0,
    4, 4, 4, 4, 4, 0, 4, 0, 1, 0, 0, 0, 1, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 4, 0, 4, 0, 1, 0, 4, 0, 4, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int dudeMap5[288]=
{
    22, 13,
    0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0,
    0, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 4, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 3, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 4, 0, 4, 4, 4, 0, 0, 0, 0, 0, 4, 4, 0, 4, 0, 0, 0, 0, 0, 1, 4,
    0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 4, 0, 0, 0, 1, 1, 4,
    0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 4, 0, 0, 1, 1, 1, 4,
    0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 4, 4, 4, 4, 4, 4, 4,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0,
};

int dudeMap6[275]=
{
    21, 13,
    0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4,
    0, 4, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 4,
    4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4,
    0, 4, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 1, 0, 0, 4, 4, 4,
    0, 4, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 4, 2, 1, 1, 1, 0, 4, 0, 0,
    0, 4, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 0, 4, 0, 0,
    0, 4, 4, 4, 4, 4, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 4, 4, 4, 0, 0,
    0, 0, 0, 0, 0, 4, 0, 0, 0, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 4, 4, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int dudeMap7[338]=
{
    24, 14,
    0, 0, 4, 0, 0, 0, 4, 4, 4, 4, 4, 0, 0, 0, 4, 4, 0, 0, 0, 4, 4, 4, 0, 0,
    0, 4, 0, 4, 0, 4, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0, 4, 0, 4, 0, 0, 0, 4, 0,
    0, 4, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4,
    4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4,
    4, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4,
    4, 4, 0, 0, 0, 4, 0, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 4, 0, 4, 0, 0,
    0, 4, 0, 0, 0, 4, 0, 1, 0, 0, 0, 0, 4, 4, 0, 1, 0, 2, 4, 4, 4, 4, 0, 0,
    0, 4, 4, 0, 0, 4, 0, 1, 1, 1, 0, 0, 4, 4, 0, 1, 1, 1, 4, 0, 0, 0, 0, 0,
    0, 0, 4, 0, 0, 4, 4, 4, 4, 4, 4, 0, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0,
    0, 0, 4, 4, 0, 4, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int dudeMap8[461]=
{
    27, 17,
    0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 0, 0,
    4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 0,
    4, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 1, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 4,
    4, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 4, 4, 0, 4, 0, 0, 4,
    4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 3, 0, 4,
    0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4, 4, 0, 4,
    0, 0, 4, 0, 0, 0, 0, 1, 0, 4, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 4,
    0, 0, 4, 0, 0, 0, 0, 1, 4, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 4, 4, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 0, 1, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 1, 1, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4,
    4, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 0, 1, 1, 1, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

int dudeMap9[322]=
{
    20, 16,
    0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 4, 4, 4, 4, 4,
    0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4,
    0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 4,
    0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 1, 1, 4,
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 4, 4, 4, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 4,
    4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 4,
    4, 4, 0, 0, 0, 0, 4, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 1, 4,
    0, 4, 0, 0, 0, 0, 4, 4, 1, 0, 0, 4, 4, 0, 0, 0, 4, 4, 4, 4,
    0, 4, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 0, 0, 4, 4, 0, 0, 0,
    0, 4, 4, 4, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 4, 4, 0, 0, 0, 0,
    0, 0, 0, 4, 0, 4, 4, 0, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0,
    0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int dudeMap10[515]=
{
    27, 19,
    0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0,
    0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0,
    4, 4, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 4, 1, 0, 0, 0, 1, 1, 1, 0, 1, 4, 4, 0,
    4, 0, 0, 4, 4, 0, 0, 4, 0, 0, 0, 4, 4, 4, 4, 4, 0, 0, 1, 4, 4, 4, 0, 4, 4, 0, 4,
    4, 0, 0, 0, 4, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 4, 4, 4, 0, 0, 4,
    4, 0, 0, 0, 4, 4, 0, 0, 4, 4, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 3, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4,
    0, 4, 0, 0, 0, 0, 0, 1, 0, 0, 0, 4, 0, 4, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 4, 4, 4, 4, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4,
    0, 0, 0, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4,
    0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0,
    0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4, 0,
    0, 0, 0, 4, 1, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 1, 1, 4, 0,
    0, 0, 0, 4, 1, 1, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 1, 1, 1, 4, 0,
    0, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 0,
};

int dudeMap11[553]=
{
    29, 19,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 1, 4, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 0, 4,
    4, 1, 0, 0, 0, 4, 4, 4, 0, 1, 4, 4, 0, 0, 0, 0, 0, 1, 0, 0, 4, 4, 0, 0, 3, 0, 4, 0, 4,
    4, 1, 1, 0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 2, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 4,
    4, 4, 4, 0, 0, 1, 1, 4, 0, 0, 0, 0, 0, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4,
    4, 0, 0, 0, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 4, 4, 0, 0, 0, 4, 4, 4, 0, 0, 4,
    4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 4, 0, 0, 1, 0, 4,
    4, 1, 1, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 0, 4, 0, 4, 1, 0, 0, 0, 0, 4, 0, 0, 4, 4, 4, 4,
    4, 4, 4, 4, 0, 1, 0, 0, 0, 4, 4, 4, 0, 0, 4, 0, 4, 4, 1, 0, 0, 4, 0, 1, 0, 4, 0, 0, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 4, 4, 4, 0, 0, 1, 4, 0, 0, 0, 4, 0, 0, 0, 4,
    4, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 4, 0, 0, 0, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 0, 4,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 4, 4, 0, 0, 0, 0, 4, 0, 4,
    4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 0, 1, 1, 4, 0, 4,
    4, 1, 4, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 0, 4,
    4, 4, 1, 4, 4, 4, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 4,
    4, 1, 4, 1, 4, 1, 4, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

// Direct equivalent of upstream's own real `const byte *gamemaps[]`
// pointer table - implemented as an if-chain returning a pointer instead
// of an actual `int*[11]` array declaration, sidestepping any need to
// confirm pointer-array declaration syntax in this dialect at all (every
// other pointer usage already proven in this project's own files is a
// single pointer variable/parameter, never an array of them).
int* dudeGetMapData( int level )
{
    if( level == 0 ) return dudeMap1;
    if( level == 1 ) return dudeMap2;
    if( level == 2 ) return dudeMap3;
    if( level == 3 ) return dudeMap4;
    if( level == 4 ) return dudeMap5;
    if( level == 5 ) return dudeMap6;
    if( level == 6 ) return dudeMap7;
    if( level == 7 ) return dudeMap8;
    if( level == 8 ) return dudeMap9;
    if( level == 9 ) return dudeMap10;
    return dudeMap11;
}

// -----------------------------------------------------------------------------
// Block helpers - direct ports of upstream's own real `Block` class methods
// (see this file's own header comment on why this became a plain struct +
// fixed array instead of a heap-allocated class).
// -----------------------------------------------------------------------------

bool dudeBlockIs( int i, int ix, int iy )
{
    return ( !dudeBlocks[ i ].islift ) && ( dudeBlocks[ i ].x == ix ) && ( dudeBlocks[ i ].y == iy );
}

int dudeGetTile( int x, int y )
{
    return dudeMapData[ 2 + y * dudeMapWidth + x ];
}

bool dudeCanMove( int cx, int cy )
{
    if( dudeGetTile( cx, cy ) == 4 ) return false;

    int i;
    for( i = 0; i < dudeNumBlocks; i++ )
      if( dudeBlockIs( i, cx, cy ) ) return false;

    return true;
}

void dudeBlockLift( int i )
{
    dudeBlocks[ i ].islift = true;
}

// Direct port of upstream's own real `Block::put()` - lets the block fall
// as far as it can before settling.
void dudeBlockPut( int i, int nx, int ny )
{
    while( dudeCanMove( nx, ny + 1 ) )
      ny = ny + 1;

    dudeBlocks[ i ].x = nx;
    dudeBlocks[ i ].y = ny;
    dudeBlocks[ i ].islift = false;
}

void dudeBlockDraw( int i )
{
    if( dudeBlocks[ i ].islift ) return; // drawn as part of the player instead - see dudeDrawWorld()

    int drawX = dudeBlocks[ i ].x * 8 - dudeMapX;
    int drawY = dudeBlocks[ i ].y * 8 - dudeMapY;
    if( ( drawX > 86 ) || ( drawY > 68 ) || ( drawX < -8 ) || ( drawY < -8 ) ) return;

    gbDrawBitmap( drawX, drawY, dudeBlockBitmap );
}

// -----------------------------------------------------------------------------
// Level loading / drawing
// -----------------------------------------------------------------------------

void dudeLoadLevel()
{
    dudeMapData = dudeGetMapData( dudeCurLevel );
    dudeMapWidth = dudeMapData[ 0 ];
    dudeMapHeight = dudeMapData[ 1 ];

    dudeNumBlocks = 0;

    int x, y;
    for( y = 0; y < dudeMapHeight; y++ )
    {
        for( x = 0; x < dudeMapWidth; x++ )
        {
            int tile = dudeGetTile( x, y );

            if( tile == 2 )
            {
                // upstream's own real magic camera-centering offset,
                // literally captioned "it works, OK!?!?!?!" in the real
                // source - preserved verbatim, not re-derived.
                dudeMapX = 8 * ( x - 5 ) + 2;
                dudeMapY = 8 * ( y - 2 ) - 4;
                dudePlayerX = x;
                dudePlayerY = y;
            }
            else if( tile == 1 )
            {
                dudeBlocks[ dudeNumBlocks ].x = x;
                dudeBlocks[ dudeNumBlocks ].y = y;
                dudeBlocks[ dudeNumBlocks ].islift = false;
                dudeNumBlocks = dudeNumBlocks + 1;
            }
        }
    }
}

// Direct port of upstream's own real `drawWorld()`.
void dudeDrawWorld()
{
    gbSetColor( 1 );

    int x, y;
    for( y = 0; y < dudeMapHeight; y++ )
    {
        for( x = 0; x < dudeMapWidth; x++ )
        {
            int drawX = x * 8 - dudeMapX;
            int drawY = y * 8 - dudeMapY;
            if( ( drawX > 86 ) || ( drawY > 68 ) || ( drawX < -8 ) || ( drawY < -8 ) ) continue;

            int tile = dudeGetTile( x, y );
            if( tile == 3 ) gbDrawBitmap( drawX, drawY, dudeDoorBitmap );
            else if( tile == 4 ) gbDrawBitmap( drawX, drawY, dudeWallBitmap );
        }
    }

    int i;
    for( i = 0; i < dudeNumBlocks; i++ )
      dudeBlockDraw( i );

    int drawX = dudePlayerX * 8 - dudeMapX;
    int drawY = dudePlayerY * 8 - dudeMapY;
    if( !( ( drawX > 86 ) || ( drawY > 68 ) || ( drawX < -8 ) || ( drawY < -8 ) ) )
    {
        if( dudeLookLeft ) gbDrawBitmap( drawX, drawY, dudeDudeLeftBitmap );
        else gbDrawBitmap( drawX, drawY, dudeDudeRightBitmap );
    }

    drawY = drawY - 8;
    if( dudeDoLift && !( ( drawX > 86 ) || ( drawY > 68 ) || ( drawX < -8 ) || ( drawY < -8 ) ) )
      gbDrawBitmap( drawX, drawY, dudeBlockBitmap );
}

// Direct port of upstream's own real `climb()`.
void dudeClimb()
{
    int tmpX;
    if( dudeLookLeft ) tmpX = dudePlayerX - 1;
    else tmpX = dudePlayerX + 1;

    bool canClimb = ( !dudeCanMove( tmpX, dudePlayerY ) ) && dudeCanMove( tmpX, dudePlayerY - 1 ) && dudeCanMove( dudePlayerX, dudePlayerY - 1 )
        && ( ( !dudeDoLift ) || ( dudeCanMove( tmpX, dudePlayerY - 2 ) && dudeCanMove( dudePlayerX, dudePlayerY - 2 ) ) );

    if( canClimb )
    {
        dudePlayerY = dudePlayerY - 1;
        dudePlayerX = tmpX;
        dudeMapY = dudeMapY - 8;
        if( dudeLookLeft ) dudeMapX = dudeMapX - 8;
        else dudeMapX = dudeMapX + 8;
        gbPlayTick();
        dudeMoves = dudeMoves + 1;
    }
}

// -----------------------------------------------------------------------------
// States
// -----------------------------------------------------------------------------

void dudeBeginTitle()
{
    dudeState = DUDE_STATE_TITLE;
}

void dudeBeginLevelMenu()
{
    dudeLevelPick = dudeCurLevel + 1;
    dudeState = DUDE_STATE_LEVELMENU;
}

void dudeBeginViewMap()
{
    dudeSavedMapX = dudeMapX;
    dudeSavedMapY = dudeMapY;
    dudeState = DUDE_STATE_VIEWMAP;
    dudeDrawWorld(); // matches upstream's own real moveWorld() drawing once immediately, before its own input loop begins
}

// Direct port of upstream's own real `loop()`'s "moves>0" branch (see this
// file's own header comment for the real off-by-one bounds bug fixed here).
void dudeLevelComplete()
{
    if( dudeMoves > 0 )
    {
        int oldMoves = eeprom_read_word( dudeCurLevel * 2 );
        if( ( dudeMoves < oldMoves ) || ( oldMoves == 0 ) )
          eeprom_write_word( dudeCurLevel * 2, dudeMoves );

        if( dudeCurLevel < DUDE_NUM_LEVELS - 1 )
        {
            dudeCurLevel = dudeCurLevel + 1;
            dudeLoadLevel();
            dudeMoves = 0;
            // Fixed here, not preserved: real upstream's own auto-advance
            // path never resets doLift/liftBlock either - see this file's
            // own header comment for the full mechanism - so completing a
            // level while still carrying a lifted block left a phantom
            // floating block drawn above the player at the start of the
            // next level, and could apply DOWN to whatever real block
            // happened to occupy the stale liftBlock index in the new
            // level's own block array. Reset explicitly here instead.
            dudeDoLift = false;
            dudeLiftBlock = 0;
            return;
        }
    }

    dudeBeginLevelMenu();
}

void dudeUpdateTitle()
{
    gbSetColor( 1 );
    gbCursorX = 28;
    gbCursorY = 0;
    gbPrintString( "PRESS A" );
    gbDrawBitmap( 10, 7, dudeLogoBitmap );

    if( gbPressed( BTN_A ) )
      dudeBeginLevelMenu();
}

// Direct port of upstream's own real `drawLevelMenu()`/`refreshLevelMenu()`,
// merged into one function since this shim always fully redraws every
// tick (see this file's own header comment on why the real incremental
// erase-and-redraw split isn't needed here).
void dudeDrawLevelMenu()
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( dudeLevelMenuHeaderText );

    gbCursorX = 32;
    gbCursorY = 12;
    if( dudeLevelPick < 10 )
      gbPrintString( " " );
    gbPrintNumber( dudeLevelPick );

    // Fixed here, not preserved: real upstream's own check is only ever
    // `moves > 0`, never `moves != 65535` - see this file's own header
    // comment for the full "fresh EEPROM word decodes as 65535, not 0"
    // mechanism. A genuinely fresh save (every 0xFF byte) therefore falsely
    // showed the "ok" checkmark and a nonsense "Moves 65535" readout for
    // every never-completed level. `savedMoves` is normalized to 0 for the
    // fresh-cell sentinel right after reading it, so both checks below
    // (checkmark/cross icon, and whether to print a move count at all)
    // correctly treat a fresh level as not-yet-completed.
    int savedMoves = eeprom_read_word( ( dudeLevelPick - 1 ) * 2 );
    if( savedMoves == 65535 ) savedMoves = 0;

    gbSetColor( 1 );
    if( savedMoves > 0 ) gbDrawBitmap( 48, 11, dudeOkBitmap );
    else gbDrawBitmap( 48, 11, dudeKoBitmap );

    if( savedMoves > 0 )
    {
        gbCursorX = 0;
        gbCursorY = 24;
        gbPrintString( "Moves  " );
        gbPrintNumber( savedMoves );
    }
}

// Direct port of upstream's own real `chooseLevel()`.
void dudeUpdateLevelMenu()
{
    if( gbPressed( BTN_C ) )
    {
        dudeBeginTitle();
        return;
    }

    if( gbPressed( BTN_RIGHT ) )
    {
        dudeLevelPick = dudeLevelPick + 1;
        if( dudeLevelPick > DUDE_NUM_LEVELS ) dudeLevelPick = DUDE_NUM_LEVELS;
    }
    if( gbPressed( BTN_LEFT ) )
    {
        dudeLevelPick = dudeLevelPick - 1;
        if( dudeLevelPick < 1 ) dudeLevelPick = 1;
    }

    if( gbPressed( BTN_A ) )
    {
        dudeCurLevel = dudeLevelPick - 1;
        dudeLoadLevel();
        dudeMoves = 0;
        dudeDoLift = false;
        dudeState = DUDE_STATE_PLAY;
        dudeDrawWorld();
        return;
    }

    dudeDrawLevelMenu();
}

// Direct port of upstream's own real `moveWorld()` (converted to a
// per-tick state - see this file's own header comment on the one small,
// harmless timing difference this introduces around re-entering
// DUDE_STATE_PLAY).
void dudeUpdateViewMap()
{
    if( gbReleased( BTN_A ) )
    {
        dudeMapX = dudeSavedMapX;
        dudeMapY = dudeSavedMapY;
        dudeState = DUDE_STATE_PLAY;
        dudeDrawWorld();
        return;
    }

    if( gbPressed( BTN_LEFT ) ) dudeMapX = dudeMapX - 8;
    if( gbPressed( BTN_RIGHT ) ) dudeMapX = dudeMapX + 8;
    if( gbPressed( BTN_UP ) ) dudeMapY = dudeMapY - 8;
    if( gbPressed( BTN_DOWN ) ) dudeMapY = dudeMapY + 8;

    dudeDrawWorld();
}

// Direct port of upstream's own real `playMap()`'s per-tick body.
void dudeUpdatePlay()
{
    if( gbPressed( BTN_LEFT ) )
    {
        if( dudeCanMove( dudePlayerX - 1, dudePlayerY ) )
        {
            if( dudeDoLift && !dudeCanMove( dudePlayerX - 1, dudePlayerY - 1 ) )
            {
                dudeBlockPut( dudeLiftBlock, dudePlayerX, dudePlayerY - 1 );
                dudeDoLift = false;
            }
            dudePlayerX = dudePlayerX - 1;
            dudeMapX = dudeMapX - 8;
            gbPlayTick();
            dudeMoves = dudeMoves + 1;
        }
        else if( dudeLookLeft )
          dudeClimb();

        dudeLookLeft = true;
    }

    if( gbPressed( BTN_RIGHT ) )
    {
        if( dudeCanMove( dudePlayerX + 1, dudePlayerY ) )
        {
            if( dudeDoLift && !dudeCanMove( dudePlayerX + 1, dudePlayerY - 1 ) )
            {
                dudeBlockPut( dudeLiftBlock, dudePlayerX, dudePlayerY - 1 );
                dudeDoLift = false;
            }
            dudePlayerX = dudePlayerX + 1;
            dudeMapX = dudeMapX + 8;
            gbPlayTick();
            dudeMoves = dudeMoves + 1;
        }
        else if( !dudeLookLeft )
          dudeClimb();

        dudeLookLeft = false;
    }

    if( gbPressed( BTN_DOWN ) )
    {
        int tmpX;
        if( dudeLookLeft ) tmpX = dudePlayerX - 1;
        else tmpX = dudePlayerX + 1;

        if( dudeDoLift )
        {
            if( dudeCanMove( tmpX, dudePlayerY - 1 ) )
            {
                dudeBlockPut( dudeLiftBlock, tmpX, dudePlayerY - 1 );
                dudeDoLift = false;
                gbPlayOK();
                dudeMoves = dudeMoves + 1;
            }
        }
        else
        {
            if( dudeCanMove( tmpX, dudePlayerY - 1 ) && dudeCanMove( dudePlayerX, dudePlayerY - 1 ) )
            {
                int i;
                for( i = 0; i < dudeNumBlocks; i++ )
                {
                    if( dudeBlockIs( i, tmpX, dudePlayerY ) )
                    {
                        dudeDoLift = true;
                        dudeBlockLift( i );
                        dudeLiftBlock = i;
                        gbPlayOK();
                        dudeMoves = dudeMoves + 1;
                        break;
                    }
                }
            }
        }
    }

    if( gbPressed( BTN_UP ) )
      dudeClimb();

    if( gbPressed( BTN_A ) )
    {
        dudeBeginViewMap();
        return;
    }

    if( gbHeld( BTN_B, 10 ) )
    {
        dudeLoadLevel();
        gbPlayCancel();
        dudeDoLift = false;
        dudeMoves = 0;
    }

    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        dudeBeginLevelMenu();
        return;
    }

    if( dudeGetTile( dudePlayerX, dudePlayerY ) == 3 )
    {
        dudeLevelComplete();
        return;
    }

    while( dudeCanMove( dudePlayerX, dudePlayerY + 1 ) )
    {
        dudeMapY = dudeMapY + 8;
        dudePlayerY = dudePlayerY + 1;
        if( dudeGetTile( dudePlayerX, dudePlayerY ) == 3 )
        {
            dudeLevelComplete();
            return;
        }
    }

    dudeDrawWorld();
}

void gameBlockdude_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 ); // matches upstream's own real explicit setFont(font3x5) call in drawLevelMenu() - also this shim's own real default already, set explicitly here for clarity

    dudeCurLevel = 0;
    dudeLookLeft = true;
    dudeDoLift = false;

    dudeBeginTitle();
}

void gameBlockdude_update()
{
    if( !gbUpdate() ) return;

    if( dudeState == DUDE_STATE_TITLE ) dudeUpdateTitle();
    else if( dudeState == DUDE_STATE_LEVELMENU ) dudeUpdateLevelMenu();
    else if( dudeState == DUDE_STATE_VIEWMAP ) dudeUpdateViewMap();
    else dudeUpdatePlay();

    gbRenderFrame();
}
