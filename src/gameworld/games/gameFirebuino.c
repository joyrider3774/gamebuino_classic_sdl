#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// FireBuino! (Luis Alejandro Dominguez Bueno - LADBSoft, graphics by Erico
// Patricio Monteiro, LGPLv3 - github.com/ladbsoft/makerbuino-firebuino). A
// remake of the 1980 Game&Watch title "Fire!": two firemen hold a
// stretcher that slides across three lanes, catching people jumping from a
// burning building's windows and bouncing them into a waiting ambulance.
// Three misses (a survivor hits the ground) ends the game. Real source is
// two upstream `.ino` tabs sharing one real Arduino translation unit
// (FIRBUINO.ino/graphics.ino, both confirmed to exist and read in full
// before porting - no other source files in the staged directory besides
// the *.HEX/*.INF build artifacts).
//
// BOARD-COMPATIBILITY CHECK (per this port's own specific brief): this
// game targets MakerBuino hardware, a different-but-Gamebuino-Classic-API-
// compatible board. Its top-level `#include <Gamebuino.h>` is the real
// Gamebuino Classic library, not a MakerBuino-specific one, and a full
// read of both real source files found no MakerBuino-specific quirk at
// all - no accelerometer/tilt input, no non-standard button naming (only
// real `BTN_UP/DOWN/LEFT/RIGHT/A/B/C`), no display call outside the real
// `Display` API this shim already reproduces. Every real API surface used
// (`gb.begin()`, `gb.pickRandomSeed()`, `gb.battery.show`,
// `gb.titleScreen()`, `gb.menu()`, `gb.display.*`, `gb.buttons.pressed()`,
// `gb.sound.playTick()/playCancel()/playOK()`, `gb.getDefaultName()`,
// `gb.keyboard()`, `EEPROM.read()/write()`) is genuine, standard Gamebuino
// Classic API - this sketch happens to also run on MakerBuino because that
// board is itself built to be a drop-in-compatible clone, not because the
// sketch reaches for anything board-specific. No incompatibility to paper
// over.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the full reasoning). Upstream's own
// `byte`/`boolean` fields became plain `int` (this project's own
// established avrCompat.h convention; `boolean` itself isn't in
// avrCompat.h's own typedef list, so every real `boolean` field here was
// hand-converted to `int` the same way). `random(N)`/`random(min,max)`
// became `arand(N)`/`min + arand(max-min)`, preserving every real upstream
// range quirk exactly (see "Preserved real upstream behavior" below).
// Every real `B00000000`-style Arduino binary literal in `graphics.ino`
// (every bitmap/position table there uses this style; this file has no
// plain `0x` hex tables at all) was mechanically converted to decimal,
// byte-count verified against each table's own declared
// `{width,height,...}` header before being pasted in below. Global naming
// prefix: `fireb` (verified unused by every other game shipped or
// concurrently in flight in this project).
//
// CLASS Survivor -> FIXED STRUCT ARRAY WITH AN EXPLICIT "active" FLAG.
// Upstream's own `class Survivor` is a genuine dynamically-allocated
// object (`new`/`delete`, `Survivor* survivors[10]`, empty slots are real
// `NULL`) - this dialect has no classes/methods at all (confirmed via
// VIRCON32_C_DIALECT.md), so `Survivor` becomes a plain `FirebSurvivor`
// struct, and the pointer array becomes `FirebSurvivor[FIREB_MAX_SURVIVORS]
// firebSurvivors` with a real `active` field standing in for upstream's
// own null-vs-non-null sentinel - matching this project's own established
// "fixed struct array + state flag" convention for every other upstream
// dynamic-object array already ported here (`AgarBall[]` in
// gameAgaruino.c, `FlapPipe[]` in gameFlappyBirdo.c, `CdefMonster[]` in
// gameCastleDefence.c - none of those use real dynamic allocation either,
// despite this dialect's `misc.h` genuinely providing `malloc`/`free`).
//
// A REAL UPSTREAM INCONSISTENCY, NORMALIZED (not preserved) - traced
// through rather than assumed: `checkBounces()` is the ONLY one of
// upstream's four loops over `survivors[]` (the others are
// `moveSurvivors()`, `drawSurvivors()`, and the "find a blank slot" loop
// in `spawnSurvivor()`) that omits a null-check before dereferencing
// `survivors[i]->_bounced` - a real, latent bug on actual AVR hardware
// (dereferencing a null `Survivor*` reads whatever raw memory happens to
// sit at that low address, since AVR has no MMU to fault on it). This
// port's own `FirebSurvivor[]` has no such foreign-memory case to
// replicate - every element is always a real, fully-initialized struct,
// never actual garbage - so `firebCheckBounces()` below adds the same
// `active` guard the other three loops already use, for consistency, not
// as a defensive "just in case" measure: traced through, an inactive
// slot's own fields always default to `step=0`/`bounced=false` (this
// dialect's own confirmed global/struct-array zero-init), and `step==0`
// never matches any of the three real bounce-trigger steps (3/9/15), so
// even an UNGUARDED version would behave identically here - the guard is
// added purely to match the other three loops' own style, with zero
// behavioral difference either way in this specific representation.
//
// PRESERVED REAL UPSTREAM BEHAVIOR (not bugs to fix - traced through and
// kept exactly as shipped):
// - `movePlayer()`'s own real LEFT/A and RIGHT/B checks are FOUR SEPARATE
//   `if` statements, not two OR'd conditions - so pressing both LEFT and A
//   (or RIGHT and B) on the exact same real tick moves the stretcher TWO
//   steps instead of one. A genuine, easily-reached upstream quirk (both
//   buttons are documented as equivalent "move left"/"move right" inputs
//   in the real README), reproduced literally in `firebMovePlayer()`
//   below rather than collapsed into an OR.
// - `spawnSurvivor()`'s own real `if (floorNo < 0) break;` check is
//   genuinely unreachable dead code: `floorNo` only ever decrements one
//   step at a time inside a `while` loop that itself breaks the instant
//   `floorNo` reaches 0 (before any further decrement), so it can never
//   go negative regardless of signedness. Ported as literal dead code
//   (`if (floorNo < 0) break;`) rather than removed, matching this
//   project's own "preserve real structure even when a branch is
//   provably inert" precedent (e.g. gameCastleDefence.c's own preserved
//   real bugs).
// - `loadHighscores()`'s own real per-iteration UNCONDITIONAL
//   `minHighscore = highscoreScores[j];` (not gated behind any
//   comparison) looks like a bug at first glance but is real, correct,
//   intentional code: since the table is always kept sorted descending by
//   `saveHighscore()`'s own bubble-up, the LAST loop iteration (index 4,
//   the lowest-ranked real entry) is exactly the "current minimum
//   highscore in the table" the rest of the game needs - both this real
//   game's own header comment ("Based on code from Crabator, by Rodot")
//   and gameUfoRace.c's own near-identical `initHighscore()` (crediting
//   the same real author/pattern) independently confirm this is a
//   deliberate, reused community idiom, not a mistake - ported literally
//   as the same unconditional per-iteration overwrite in
//   `firebLoadHighscores()` below.
//
// HIGHSCORE NAME ENTRY - DROPPED, DOCUMENTED (matching an exact existing
// precedent, not a new gap): upstream's own `gb.getDefaultName(tmp)` +
// `gb.keyboard(tmp, NAME_LETTERS+1)` (a real on-screen text-entry widget)
// has no equivalent anywhere in this shim - already found and documented
// identically by gameUfoRace.c/gameArmageddon.c/gameCastleDefence.c's own
// highscore tables. Per-name storage is dropped entirely rather than
// faked with a placeholder string: `firebHighscores[]` is a plain
// scores-only table, and `firebUpdateHighscores()` below shows only the 5
// real scores, right-aligned at upstream's own real screen position, with
// nothing invented in the name column upstream used to occupy (upstream's
// own real "-" placeholder for a zero-score name slot is dropped along
// with the rest of the name column, not kept as an orphaned dash).
//
// EEPROM - REAL PERSISTENCE, SIMPLIFIED LAYOUT (scores only, no names -
// see above). Upstream's own real `EEPROM.read()`/`EEPROM.write()` calls
// in `loadHighscores()`/`saveHighscore()` became this project's own
// `eeprom_read_word()`/`eeprom_write_word()` (2 bytes per score, address
// `i*2` for entry `i` - `eeprom_read_word()`/`_write_word()` already
// handle the real MSB/LSB byte packing internally, so this port needs no
// manual byte-splitting the way upstream's own raw `EEPROM.read()`/
// `.write()` byte-pair calls did). Upstream's own real
// `(highscoreScores[j]==0xFFFF) ? 0 : highscoreScores[j]` fresh-EEPROM
// sentinel check is preserved exactly (`eepromShim.c`'s own memory-card-
// backed cells default to 0xFF per byte too, matching real factory-erased
// AVR EEPROM - see eepromShim.h's own header comment).
//
// gb.battery.show=false, dropped outright (purely cosmetic on real
// hardware, matching every other port's own treatment). `survivorCount`
// (a real upstream global, incremented in `initGame()`/`spawnSurvivor()`
// but never read anywhere else in either real source file) is dropped
// entirely - genuinely dead, write-only data with zero observable effect,
// not a quirk worth preserving. `loadHighscores()` is called once from
// `gameFirebuino_init()` rather than deferred until after the first real
// title-screen dismissal the way real `setup()` sequences it
// (`titleScreen(); initGame(); loadHighscores();`) - a harmless
// reordering: no draw or comparison anywhere reads highscore data before
// the first real game-over, long after either ordering would have loaded
// it.
//
// BLOCKING LOOPS -> STATE MACHINE (matching this project's own established
// "blocking loop -> explicit resumable state" treatment - see gamePong.c's
// own header comment): `FIREB_STATE_TITLE`/`_MENU`/`_HIGHSCORES`/`_ABOUT`/
// `_PLAYING`/`_PAUSED`/`_GAMEOVER`. Real `gb.titleScreen(F("FireBuino!"),
// titleScreenBitmap)` became `firebUpdateTitle()`, dismissed by a genuine
// fresh `gbPressed(BTN_A)` exactly like gamePong.c/gameConduit.c's own
// title conversions - `firebInitGame()` runs on that same press, matching
// upstream's own real "titleScreen() blocks, THEN initGame() runs" order
// at every real call site (including the one real exception this
// normalizes away: upstream's own `gb.menu()` cancel/Button-B path shows
// titleScreen() WITHOUT a following initGame() call - since nothing can
// have changed game state between a still-fresh menu and cancelling out
// of it, always pairing the two here is provably behavior-identical).
// Real `gb.menu(menu, MENULENGTH)` became a hand-rolled `firebUpdateMenu()`
// (Up/Down navigate, A confirms, B cancels back to the title screen),
// matching gameConduit.c's/gameCrazyTown.c's own established replacement
// for this exact real widget. Real `drawHighScores()` - a blocking
// `while(true)` loop with TWO distinct real call sites (menuScreen()'s own
// "High scores" option, and saveHighscore()'s own tail after a genuine new
// high score) - became one shared `FIREB_STATE_HIGHSCORES` with a
// remembered `firebHighscoreReturnState` field, matching gameUfoRace.c's
// own identical `ufoHighscoreReturnState` precedent for exactly this
// shape of dual-purpose real screen.
//
// Text with an embedded real icon glyph (upstream's own `"\x17: Menu"`/
// `"\x15: Continue"`/`"\x17: Quit"`/`"\x17: Back"`) can't be written as a
// plain quoted string literal (ASCII 21/23 sit outside the printable
// range this dialect's string literals accept directly) - built as
// explicit `int[]` arrays instead, matching gameTaquin.c's/
// gameSimonbuino.c's own established precedent for the exact same real
// icon glyphs (21 = the real D-pad-arrow icon, 23 = the real reset/back
// icon - both already proven correct against this shim's real ported
// `gbFont3x5`/`gbFont5x7` tables by those two files).
//
// A second, genuine real use of `gbSetColorBg(color, bg)` (Parachute was
// the only real instance found by this project's own prior audit, per
// CLAUDE.md): `drawScore()`'s own real `gb.display.setColor(WHITE,
// BLACK)` for the large (`fontSize=2`) score digits in "new" mode is a
// real opaque WHITE-on-BLACK text draw (the score sits inside a solid
// black box baked into the background art), restored here via
// `gbSetColorBg(GB_WHITE, GB_BLACK)` rather than approximated - not a new
// shim gap, the primitive already existed.
//
// A REAL, PROJECT-WIDE SHIM BUG FOUND AND FIXED (not FireBuino-specific -
// this is a genuine `gamebuinoShim.c` bug, confirmed to also silently
// affect the already-shipped gamePong.c, not something this file's own
// code triggers uniquely): `gbDrawChar()`'s internal `gbDrawCharPixel()`
// helper computed a size-2 glyph's on-screen position as `(x+col)*2`
// instead of the real, correct `x + col*2` - identical only when the
// caller's own cursor position (`x`/`y`) is exactly 0. Every other cursor
// position gets doubled along with the glyph-local offset, silently
// shifting size-2 text and, once `x*2` alone exceeds `LCD_WIDTH`/
// `LCD_HEIGHT`, dropping it off-screen entirely (every pixel of the glyph
// fails `gbDrawPixel()`'s own bounds check). Found here because
// `firebDrawScore()`'s own real `cursorX` values (52-76, `fontSize=2`)
// landed the whole score digit off the 84px-wide screen - confirmed via a
// Puppeteer screenshot showing a fully blank score box where a white "0"
// should have been. Traced the same bug to gamePong.c's own real
// `gbFontSize=2` score display (`pongOpponentScore`, `cursorX=57`) by
// temporarily reverting the fix and capturing live gameplay: the
// opponent's score was completely absent and the player's own score
// (`cursorX=15`) rendered shifted into the middle of the playfield instead
// of its real top-left corner - a genuine, previously-unnoticed regression
// in an already-shipped game, not a hypothetical. Fixed directly in this
// port's own isolated `gamebuinoShim.c` copy: `gbDrawCharPixel()` now
// takes the cursor anchor (`x`,`y`) and the glyph-local (`col`,`row`)
// as four separate parameters instead of one pre-added `x+col` pair, and
// only the local offset is multiplied by `gbFontSize` internally
// (`x + col*size`) - `gbDrawChar()`'s own two call sites were updated to
// pass all four values instead of adding them together first. Verified
// with a full before/after Puppeteer comparison on both this game and
// gamePong.c. This fix is NOT yet in the real shared `gamebuinoShim.c` -
// see this port's own final report for the exact diff to merge.
//
// `subBackgroundBitmap`/`backgroundBitmap` are genuinely 88 pixels wide
// (`{88,48,...}`) on an 84-pixel-wide real LCD - a real 4px upstream
// overdraw, not a porting mistake: `gbDrawBitmap()` already clips every
// column past `LCD_WIDTH` per-pixel (see gamebuinoShim.c), so the extra 4
// columns are silently and safely discarded, matching whatever real
// hardware's own `Display::drawBitmap()` does with the same real
// out-of-range columns.
//
// No other shim gap was found - `gbDrawBitmap()`/`gbSetColorBg()`/
// `gbRepeat()`/`gbPrintNumber()`/real `gbFont3x5`/`eeprom_read_word()`/
// `eeprom_write_word()` all exist and are used directly as documented,
// with no local workaround needed for any of them.

#define FIREB_STATE_TITLE 0
#define FIREB_STATE_MENU 1
#define FIREB_STATE_HIGHSCORES 2
#define FIREB_STATE_ABOUT 3
#define FIREB_STATE_PLAYING 4
#define FIREB_STATE_PAUSED 5
#define FIREB_STATE_GAMEOVER 6

#define FIREB_MENU_LEN 4
#define FIREB_HIGHSCORE_COUNT 5
#define FIREB_MAX_SURVIVORS 10
#define FIREB_SURVIVOR_STEPS 20

// -----------------------------------------------------------------------------
// Real upstream bitmap/position tables (graphics.ino), converted verbatim:
// every real `B00000000`-style binary literal -> decimal, `const byte` ->
// `int`, `PROGMEM` dropped, and each table's own leading empty `[]`
// dimension (upstream's real "one row per isClassic 0/1 graphics variant"
// convention) resolved to its real row count. `firebIsClassic` (0 or 1)
// indexes every one of these exactly like upstream's own `isClassic`
// boolean did.
// -----------------------------------------------------------------------------

int firebTitleScreenBitmap[242]=
{64,30,224,0,0,7,142,120,224,96,
         224,0,0,15,236,13,249,240,
         224,0,0,31,248,15,31,152,
         252,0,0,24,24,6,14,12,
         227,192,0,120,16,4,4,13,
         226,56,0,236,7,3,128,7,
         226,9,129,128,31,207,193,193,
         226,15,17,0,24,216,231,240,
         226,14,193,6,48,96,62,120,
         226,15,193,143,48,64,60,56,
         226,15,128,217,224,0,24,28,
         226,15,128,16,128,48,0,39,
         250,15,0,0,0,112,0,1,
         254,15,0,0,112,112,0,0,
         255,206,0,96,240,120,0,0,
         255,250,0,64,224,97,128,1,
         255,248,1,64,120,231,224,1,
         255,248,3,192,12,143,240,11,
         255,249,3,192,4,159,248,14,
         255,250,135,200,7,254,248,30,
         255,255,135,136,39,252,120,35,
         255,255,203,56,3,236,112,1,
         231,255,248,216,3,46,96,7,
         227,255,248,194,1,38,244,126,
         224,255,255,96,131,48,115,224,
         224,120,255,255,134,30,0,0,
         224,0,7,255,28,28,0,0,
         224,0,1,248,30,14,0,0,
         224,0,0,0,15,6,0,0,
         224,0,0,0,0,0,0,0
};

int firebSubBackgroundBitmap[2][530]=
{
  {88,48,0,0,0,0,0,0,191,255,255,255,240,
         0,0,0,0,7,133,127,255,255,255,240,
         0,0,0,0,31,224,47,255,255,255,240,
         0,0,0,0,60,17,95,255,255,255,240,
         0,0,0,0,56,0,47,255,255,255,240,
         0,0,0,0,112,0,87,255,255,255,240,
         0,0,0,0,112,0,11,255,255,255,240,
         0,0,0,0,112,1,87,255,255,255,240,
         0,0,0,0,112,0,10,191,255,255,240,
         0,0,0,0,48,0,69,127,255,255,240,
         0,0,0,0,56,0,2,175,255,255,240,
         0,0,0,0,28,0,21,95,255,255,240,
         0,0,0,0,7,128,0,11,255,255,240,
         0,0,0,68,0,0,0,85,255,255,240,
         0,0,0,0,0,0,0,2,191,255,240,
         0,0,1,85,16,0,49,21,255,255,240,
         0,0,0,136,0,0,48,0,191,255,240,
         0,0,5,85,84,0,96,5,127,255,240,
         0,0,2,170,160,15,224,0,47,255,240,
         0,0,85,255,213,29,253,129,95,255,240,
         0,64,11,255,170,8,127,192,15,255,240,
         1,64,87,255,245,64,97,0,87,255,240,
         0,192,47,255,250,128,103,128,43,255,240,
         1,193,95,255,255,80,98,129,95,255,240,
         1,128,175,255,255,160,225,0,11,255,240,
         0,1,127,255,255,212,224,0,87,255,240,
         0,0,47,255,255,160,192,0,10,255,240,
         0,1,127,255,255,245,192,1,95,245,112,
         0,0,191,255,255,232,192,0,10,170,176,
         0,5,127,255,255,245,192,4,87,208,80,
         0,2,255,255,255,235,192,0,42,128,32,
         0,149,255,255,255,253,209,21,95,64,16,
         0,171,255,255,255,251,192,0,186,128,0,
         1,215,255,255,63,255,117,85,127,64,16,
         1,255,255,255,175,238,226,42,255,128,0,
         1,255,147,255,151,207,125,95,255,64,16,
         1,39,145,63,46,174,122,191,255,160,0,
         1,36,146,127,151,79,127,255,255,80,16,
         1,36,146,191,170,174,127,255,255,232,48,
         1,36,146,95,21,78,127,255,255,245,80,
         1,36,146,170,170,172,127,255,255,255,240,
         1,36,144,85,21,70,127,255,255,255,240,
         1,36,146,170,170,172,127,255,255,255,240,
         1,36,146,85,21,76,127,255,255,255,240,
         1,36,146,170,170,172,127,255,255,255,240,
         1,255,255,255,255,255,255,255,255,255,240,
         0,0,0,0,0,2,175,255,255,255,240,
         0,0,0,0,0,0,0,0,0,0,0
  },
  {88,48,0,64,33,0,0,0,0,0,0,0,0,
         0,160,66,0,0,0,0,0,0,0,0,
         0,144,66,0,0,0,0,0,0,0,0,
         1,48,2,2,0,0,0,0,0,0,0,
         0,41,0,2,0,0,0,0,0,0,0,
         0,136,2,4,0,0,0,0,0,0,0,
         0,10,4,4,0,0,0,0,0,0,0,
         0,18,4,0,0,0,0,0,0,0,0,
         0,98,8,8,0,0,0,0,0,0,0,
         0,0,0,8,0,0,0,0,0,0,0,
         0,0,32,8,0,0,0,0,0,0,0,
         0,64,64,16,0,0,0,0,0,0,0,
         0,160,128,0,0,0,0,0,0,0,0,
         1,40,128,32,0,0,0,0,0,0,0,
         1,20,0,32,0,0,0,0,0,0,0,
         0,4,128,64,0,0,0,62,224,0,0,
         0,68,128,64,0,0,0,193,16,0,0,
         0,72,3,0,0,0,7,0,136,0,0,
         0,16,28,0,0,0,8,128,136,0,0,
         0,16,64,0,0,0,8,4,4,0,0,
         0,32,128,0,0,0,8,8,4,0,0,
         0,65,0,0,0,0,8,8,4,0,0,
         0,1,0,0,0,0,4,8,8,0,0,
         0,16,0,0,0,0,11,8,8,0,0,
         0,168,0,0,0,0,16,68,148,0,0,
         1,72,0,0,0,0,16,128,66,0,0,
         0,8,0,0,0,0,16,128,66,0,0,
         0,8,0,0,0,0,8,64,130,0,0,
         0,144,0,0,0,0,8,8,2,0,0,
         1,32,0,0,0,0,4,76,132,0,0,
         0,32,0,0,0,0,3,33,8,0,0,
         0,32,0,0,0,0,0,146,48,0,0,
         0,64,0,0,0,0,0,115,192,0,0,
         0,0,0,0,0,0,0,18,0,0,0,
         0,0,0,0,0,0,0,18,0,0,0,
         0,0,0,0,0,0,0,18,0,0,0,
         0,0,0,0,0,0,0,18,0,0,0,
         0,0,0,0,0,0,0,17,0,0,0,
         0,2,1,0,64,1,0,17,0,0,0,
         0,10,1,128,80,1,64,17,0,0,0,
         0,44,3,0,32,2,64,17,0,0,0,
         0,0,0,0,0,0,0,0,0,0,0,
         0,8,0,2,0,0,64,0,16,0,0,
         0,16,0,2,0,0,64,0,8,0,0,
         0,16,0,4,0,0,32,0,8,0,0,
         0,32,0,4,0,0,32,0,4,0,0,
         0,32,0,4,0,0,32,0,4,0,0,
         0,64,0,8,0,0,16,0,2,0,0
  }
};

int firebBackgroundBitmap[2][530]=
{
  {88,48,170,170,176,0,0,0,2,191,255,255,240,
         174,251,240,0,0,0,5,127,255,255,240,
         191,206,128,0,0,0,0,174,255,255,240,
         248,0,128,0,0,0,1,95,255,255,240,
         248,1,192,0,0,0,0,171,255,255,240,
         176,0,128,0,0,0,0,87,255,255,240,
         112,1,64,0,0,0,0,2,175,255,240,
         240,0,128,0,0,0,0,21,255,255,240,
         208,0,0,0,0,0,0,0,171,255,240,
         225,0,0,0,0,0,0,1,87,255,240,
         117,0,0,0,0,0,0,0,42,255,240,
         235,0,0,0,0,0,0,0,23,255,240,
         63,0,0,0,0,0,0,0,10,191,240,
         126,0,0,0,0,0,0,0,5,255,240,
         12,0,0,0,0,0,0,0,2,175,240,
         76,0,0,0,0,0,0,0,5,255,240,
         20,0,0,0,0,0,0,0,0,191,240,
         12,0,0,0,0,0,0,0,1,127,240,
         84,0,0,0,0,0,0,0,0,175,240,
         44,0,0,0,0,0,0,0,1,127,240,
         88,64,0,0,0,0,0,0,0,171,240,
         109,64,0,0,0,0,0,0,0,87,240,
         90,192,0,0,0,0,0,0,0,42,160,
         47,192,0,0,0,0,0,0,0,85,240,
         31,128,0,0,0,0,0,0,0,10,160,
         152,0,0,0,0,0,0,0,0,5,80,
         8,0,0,0,0,0,0,0,0,0,32,
         152,0,0,0,0,0,0,0,0,0,80,
         8,0,0,0,0,0,0,0,0,0,0,
         8,0,0,0,0,0,0,0,0,0,0,
         56,0,0,0,0,0,0,0,0,0,0,
         16,128,0,0,0,0,0,0,0,0,0,
         58,128,0,0,0,0,0,0,0,0,0,
         181,128,0,0,0,0,0,0,0,0,0,
         127,128,0,0,0,0,0,0,0,0,0,
         127,0,0,0,0,0,0,0,0,0,0,
         248,0,0,0,0,0,0,0,0,0,0,
         88,0,0,0,0,0,0,0,0,0,0,
         184,0,0,0,0,0,0,0,0,0,0,
         24,0,0,0,0,0,0,0,0,0,0,
         168,0,0,0,0,0,0,0,0,0,0,
         88,0,0,0,0,0,0,0,0,0,0,
         40,0,0,0,0,0,0,0,0,0,0,
         72,0,0,0,0,0,0,0,0,0,0,
         40,0,0,0,0,0,0,0,0,0,0,
         255,255,255,255,255,255,255,7,255,255,240,
         0,0,0,0,0,2,175,127,255,255,240,
         0,0,0,0,0,0,95,255,255,255,240
  },
  {88,48,2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         255,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         255,128,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         255,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         255,128,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         255,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         0,128,0,0,0,0,0,0,0,0,0,
         255,128,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         2,0,0,0,0,0,0,0,0,0,0,
         3,255,255,255,255,255,255,255,255,255,240,
         4,8,0,2,0,0,64,0,16,0,0,
         8,16,0,2,0,0,64,0,8,0,0,
         16,16,0,4,0,0,32,0,8,0,0,
         32,32,0,4,0,0,32,0,4,0,0,
         64,32,0,4,0,0,32,0,4,0,0,
         128,64,0,8,0,0,16,0,2,0,0
  }
};

int firebAmbulanceBitmap[2][50]=
{
  {24,15,31,253,0,
         63,165,0,
         63,255,128,
         126,0,128,
         102,254,128,
         66,254,128,
         66,254,128,
         102,0,128,
         126,40,128,
         126,0,128,
         255,255,128,
         222,124,128,
         127,255,128,
         57,199,0,
         57,199,0,
         0,0,0
  },
  {24,16,0,96,0,
         0,144,0,
         31,248,0,
         32,4,0,
         79,154,0,
         72,146,0,
         79,159,0,
         64,0,128,
         128,192,64,
         129,224,192,
         129,224,192,
         128,192,64,
         152,12,64,
         231,243,192,
         36,18,0,
         24,12,0
  }
};

int firebAmbulanceShadowBitmap[2][50]=
{
  {24,16,0,0,0,
         63,253,128,
         63,255,128,
         63,208,128,
         63,254,128,
         63,254,128,
         63,254,128,
         63,208,128,
         63,248,128,
         63,208,128,
         63,255,128,
         63,252,128,
         63,255,128,
         63,255,128,
         0,0,0,
         0,0,0
  },
  {24,16,0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,4,0,
         7,12,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0
  }
};

int firebAmbulanceLightBitmap[2][50]=
{
  {24,16,0,0,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,62,0,
         0,62,0,
         0,62,0,
         0,0,0,
         0,40,0,
         0,0,0,
         0,63,0,
         0,60,0,
         0,0,0,
         0,0,0,
         0,0,0,
         0,0,0
  },
  {24,16,0,0,0,
         0,240,0,
         31,248,0,
         63,252,0,
         127,250,0,
         120,242,0,
         127,255,0,
         127,255,128,
         127,255,128,
         127,255,128,
         127,255,128,
         127,255,128,
         127,255,128,
         31,252,0,
         28,28,0,
         0,0,0
  }
};

int firebAmbulancePositions[2][2]=
{
  {66,30},
  {67,28}
};

int firebLivesBitmap[2][8]=
{
  {8,6, 252,
        204,
        132,
        132,
        204,
        252
  },
  {8,6,48,
       48,
       252,
       252,
       48,
       48
  }
};

int firebLivesLightBitmap[2][8]=
{
  {8,6,0,
       48,
       120,
       120,
       48,
       0
  },
  {8,6,0,
       0,
       0,
       0,
       0,
       0
  }
};

int firebLivesPositions[2][3][2]=
{
  {
    {66,17},
    {72,16},
    {78,15}
  },
  {
    {60,0},
    {67,0},
    {74,0}
  }
};

int firebPlayerBitmap[2][20]=
{
  {16,8,96,6,
        88,26,
        248,31,
        96,6,
        248,31,
        231,231,
        83,202,
        144,9,
        0,0
  },
  {16,9,224,28,
        224,28,
        224,28,
        64,8,
        96,24,
        95,232,
        64,8,
        64,8,
        160,20
  }
};

int firebPlayerPositions[2][3][2]=
{
  {
    {7,37},
    {28,37},
    {49,37}
  },
  {
    {14,32},
    {33,32},
    {51,32}
  }
};

int firebSurvivor0Bitmap[2][11]=
{
  {8,8,96,
       88,
       248,
       96,
       248,
       224,
       80,
       144,
       0
  },
  {8,9,112,
       112,
       112,
       32,
       112,
       168,
       32,
       32,
       80
  }
};

int firebSurvivor1Bitmap[2][10]=
{
  {8,8,56,
       108,
       60,
       240,
       120,
       96,
       208,
       16
  },
  {8,8,112,
       112,
       112,
       40,
       240,
       32,
       48,
       72
  }
};

int firebSurvivor2Bitmap[2][10]=
{
  {8,8,112,
       216,
       120,
       224,
       112,
       120,
       40,
       32
  },
  {8,8,4,
       14,
       31,
       110,
       20,
       40,
       200,
       64
  }
};

int firebSurvivor3Bitmap[2][7]=
{
  {8,5,6,
       214,
       61,
       255,
       52
  },
  {8,5,32,
       151,
       127,
       151,
       8
  }
};

int firebSurvivor4Bitmap[2][9]=
{
  {8,6,112,
       208,
       112,
       224,
       112,
       96,
       0
  },
  {8,7,224,
       224,
       224,
       64,
       112,
       64,
       112
  }
};

int firebSurvivorPositions[2][FIREB_SURVIVOR_STEPS][3]=
{
  {
    {8,8,1},
    {10,19,2},
    {8,29,6},
    {13,37,4},
    {14,26,7},
    {17,14,6},
    {24,5,2},
    {27,15,6},
    {32,26,2},
    {30,37,9},
    {35,28,6},
    {40,19,2},
    {45,12,1},
    {48,20,6},
    {50,29,7},
    {55,37,4},
    {57,29,1},
    {62,25,2},
    {67,24,1},
    {72,27,4}
  },
  {
    {13,6,0},
    {15,15,0},
    {17,24,0},
    {19,30,4},
    {21,21,1},
    {23,10,1},
    {28,6,1},
    {34,11,0},
    {36,20,0},
    {37,30,4},
    {40,21,1},
    {42,14,1},
    {46,11,1},
    {51,14,0},
    {54,23,0},
    {55,30,4},
    {59,21,1},
    {64,16,1},
    {69,19,0},
    {71,25,4}
  }
};

int firebSurvivorIdlePositions[2][3]=
{
  {5,4,11},
  {0,0,12}
};

int firebSurvivorKOPositions[2][3][2]=
{
  {
    {11,41},
    {32,41},
    {53,41}
  },
  {
    {16,43},
    {35,43},
    {53,43}
  }
};

// -----------------------------------------------------------------------------
// Real icon-glyph text, built as explicit int[] arrays (see header comment):
// icon 23 = the real reset/back icon, icon 21 = the real D-pad-arrow icon.
// -----------------------------------------------------------------------------

char firebTextMenu[8]= {23,58,32,77,101,110,117,0};                  // <23>: Menu
char firebTextContinue[12]= {21,58,32,67,111,110,116,105,110,117,101,0}; // <21>: Continue
char firebTextQuit[8]= {23,58,32,81,117,105,116,0};                  // <23>: Quit
char firebTextBack[8]= {23,58,32,66,97,99,107,0};                    // <23>: Back

// -----------------------------------------------------------------------------
// Game state
// -----------------------------------------------------------------------------

typedef struct
{
    int floor;   // 0/1/2 - which of the 3 windows this survivor jumped from
    int step;    // position along the real per-floor animation/timeline table
    int delay;   // real countdown before the fall animation actually starts
    bool bounced;
    bool dead;
    bool active; // this port's own real "slot in use" flag - see header comment
} FirebSurvivor;

FirebSurvivor firebSurvivors[FIREB_MAX_SURVIVORS];
bool firebOccupiedWindows[3];

int firebState;
int firebMenuIndex;
int firebHighscoreReturnState;

int firebScore;
int firebLives;
int firebPlayerPosition;
int firebMoveTick;
int firebSpawnDelay;
int firebNoOfSurvivors;
int firebIsClassic; // 0 = "new" graphics, 1 = "classic" graphics - indexes every fireb*[2]... table above

int firebHighscores[FIREB_HIGHSCORE_COUNT];
int firebMinHighscore;

// -----------------------------------------------------------------------------
// Highscores (EEPROM) - direct port of upstream's own loadHighscores()/
// saveHighscore() (credited there to "Crabator, by Rodot" - see header
// comment), minus the dropped name column.
// -----------------------------------------------------------------------------

void firebLoadHighscores()
{
    int j;
    for( j = 0; j < FIREB_HIGHSCORE_COUNT; j = j + 1 )
    {
        int v = eeprom_read_word( j * 2 );
        if( v == 0xFFFF )
          v = 0; // fresh/erased EEPROM cell - matches real upstream's own sentinel check
        firebHighscores[ j ] = v;
        firebMinHighscore = v; // real unconditional per-iteration overwrite - see header comment
    }
}

void firebSaveHighscore()
{
    firebHighscores[ FIREB_HIGHSCORE_COUNT - 1 ] = firebScore;

    int i;
    for( i = FIREB_HIGHSCORE_COUNT - 1; i > 0; i = i - 1 )
    {
        if( firebHighscores[ i - 1 ] < firebHighscores[ i ] )
        {
            int temp = firebHighscores[ i - 1 ];
            firebHighscores[ i - 1 ] = firebHighscores[ i ];
            firebHighscores[ i ] = temp;
            firebMinHighscore = firebHighscores[ i ];
        }
        else
          break;
    }

    for( i = 0; i < FIREB_HIGHSCORE_COUNT; i = i + 1 )
      eeprom_write_word( i * 2, firebHighscores[ i ] );
}

// -----------------------------------------------------------------------------
// initGame() - direct port, minus the dropped `survivorCount` (see header
// comment) and the real `gameState = STATE_MENU;` (this port's own
// `firebState` transition is driven by the state machine instead, from
// whichever state actually calls this).
// -----------------------------------------------------------------------------

void firebInitGame()
{
    gbPickRandomSeed();

    firebLives = 3;
    firebScore = 0;
    firebPlayerPosition = 1;
    firebSpawnDelay = 2;
    firebIsClassic = 0;

    int i;
    for( i = 0; i < FIREB_MAX_SURVIVORS; i = i + 1 )
    {
        firebSurvivors[ i ].active = false;
        firebSurvivors[ i ].dead = false;
        firebSurvivors[ i ].bounced = false;
        firebSurvivors[ i ].floor = 0;
        firebSurvivors[ i ].step = 0;
        firebSurvivors[ i ].delay = 0;
    }

    // Real upstream `survivors[0] = new Survivor(0, 3);` - one survivor
    // already falling from floor 0 the instant a fresh game begins.
    firebSurvivors[ 0 ].active = true;
    firebSurvivors[ 0 ].floor = 0;
    firebSurvivors[ 0 ].step = 0;
    firebSurvivors[ 0 ].delay = 3;
    firebNoOfSurvivors = 1;

    for( i = 0; i < 3; i = i + 1 )
      firebOccupiedWindows[ i ] = false;
    firebOccupiedWindows[ 0 ] = true;

    // firebMoveTick is deliberately NOT reset here - matching real
    // upstream's own initGame(), which never touches `moveTick` either.
    // It keeps whatever small leftover value a previous round left it at
    // (or the real dialect's own global zero-init on the very first
    // game), a harmless real quirk: gameLogic() just runs its very first
    // tick fractionally sooner or later, never more than a few frames.
}

void firebMovePlayer()
{
    // Four separate checks, not two OR'd conditions - see header comment
    // on the real upstream double-move quirk this preserves.
    if( gbPressed( BTN_LEFT ) )
    {
        if( firebPlayerPosition > 0 )
          firebPlayerPosition = firebPlayerPosition - 1;
    }
    if( gbPressed( BTN_A ) )
    {
        if( firebPlayerPosition > 0 )
          firebPlayerPosition = firebPlayerPosition - 1;
    }
    if( gbPressed( BTN_RIGHT ) )
    {
        if( firebPlayerPosition < 2 )
          firebPlayerPosition = firebPlayerPosition + 1;
    }
    if( gbPressed( BTN_B ) )
    {
        if( firebPlayerPosition < 2 )
          firebPlayerPosition = firebPlayerPosition + 1;
    }
}

// Direct port of upstream's own spawnSurvivor() - random(min,max) ->
// min + arand(max-min), preserving every real range quirk exactly.
void firebSpawnSurvivor()
{
    int mustSpawn;

    if( firebNoOfSurvivors != 0 )
    {
        if( firebScore <= ( 5 * 150 ) )
          mustSpawn = arand( 5 + 5 - ( firebScore / 150 ) );
        else
          mustSpawn = arand( 5 );
    }
    else
      mustSpawn = arand( 2 );

    if( mustSpawn == 0 )
    {
        int i;
        for( i = 0; i < FIREB_MAX_SURVIVORS; i = i + 1 )
        {
            if( !firebSurvivors[ i ].active )
            {
                int floorNo;
                int delayTicks;

                if( firebScore <= 300 )
                {
                    floorNo = 0;
                    delayTicks = 1 + arand( 5 );
                }
                else if( firebScore <= 600 )
                {
                    floorNo = arand( 2 );
                    delayTicks = 2 + arand( 4 );
                }
                else
                {
                    floorNo = arand( 3 );
                    delayTicks = 3 + arand( 3 );
                }

                while( firebOccupiedWindows[ floorNo ] )
                {
                    if( floorNo > 0 )
                      floorNo = floorNo - 1;
                    else
                      break;
                }

                if( floorNo < 0 )
                  break; // real, genuinely unreachable dead code - see header comment

                firebSurvivors[ i ].active = true;
                firebSurvivors[ i ].dead = false;
                firebSurvivors[ i ].bounced = false;
                firebSurvivors[ i ].floor = floorNo;
                if( floorNo == 0 )
                  firebSurvivors[ i ].step = 0;
                else if( floorNo == 1 )
                  firebSurvivors[ i ].step = 1;
                else
                  firebSurvivors[ i ].step = 2;
                firebSurvivors[ i ].delay = delayTicks;

                firebNoOfSurvivors = firebNoOfSurvivors + 1;
                firebSpawnDelay = 2;
                break;
            }
        }
    }
}

// Direct port of upstream's own moveSurvivors().
void firebMoveSurvivors()
{
    int i;
    for( i = 0; i < FIREB_MAX_SURVIVORS; i = i + 1 )
    {
        if( firebSurvivors[ i ].active )
        {
            if( firebSurvivors[ i ].dead )
            {
                firebSurvivors[ i ].active = false;
                firebNoOfSurvivors = firebNoOfSurvivors - 1;
                continue;
            }

            if( firebSurvivors[ i ].delay > 0 )
              firebSurvivors[ i ].delay = firebSurvivors[ i ].delay - 1;
            else
              firebSurvivors[ i ].step = firebSurvivors[ i ].step + 1;

            if( ( firebSurvivors[ i ].floor == 0 && firebSurvivors[ i ].step == 1 ) ||
                ( firebSurvivors[ i ].floor == 1 && firebSurvivors[ i ].step == 2 ) ||
                ( firebSurvivors[ i ].floor == 2 && firebSurvivors[ i ].step == 3 ) )
            {
                firebOccupiedWindows[ firebSurvivors[ i ].floor ] = false;
            }

            if( ( firebSurvivors[ i ].step == 4 ) ||
                ( firebSurvivors[ i ].step == 10 ) ||
                ( firebSurvivors[ i ].step == 16 ) )
            {
                if( firebSurvivors[ i ].bounced )
                {
                    firebSurvivors[ i ].bounced = false;
                    firebScore = firebScore + 1;
                }
                else
                {
                    firebSurvivors[ i ].dead = true;
                    gbPlayCancel();
                    firebLives = firebLives - 1;
                    if( firebLives <= 0 )
                      firebState = FIREB_STATE_GAMEOVER;
                    continue;
                }
            }

            if( firebSurvivors[ i ].step >= FIREB_SURVIVOR_STEPS )
            {
                firebSurvivors[ i ].active = false;
                firebNoOfSurvivors = firebNoOfSurvivors - 1;
                firebScore = firebScore + 10;
            }
        }
    }
}

// Direct port of upstream's own gameLogic().
void firebGameLogic()
{
    firebMoveTick = firebMoveTick - 1;

    if( firebMoveTick <= 0 )
    {
        firebMoveSurvivors();
        gbPlayTick();

        if( firebSpawnDelay > 0 )
          firebSpawnDelay = firebSpawnDelay - 1;

        if( firebScore <= ( 13 * 50 ) )
          firebMoveTick = 4 + ( 13 - ( firebScore / 50 ) );
        else
          firebMoveTick = 4;

        if( firebSpawnDelay <= 0 )
          firebSpawnSurvivor();
    }
}

// Direct port of upstream's own checkBounces() - see header comment on the
// real `active` guard this adds (provably behavior-identical here either
// way, added purely for consistency with every other survivors[] loop).
void firebCheckBounces()
{
    int i;
    for( i = 0; i < FIREB_MAX_SURVIVORS; i = i + 1 )
    {
        if( firebSurvivors[ i ].active && !firebSurvivors[ i ].bounced )
        {
            if( ( firebSurvivors[ i ].step == 3 && firebPlayerPosition == 0 ) ||
                ( firebSurvivors[ i ].step == 9 && firebPlayerPosition == 1 ) ||
                ( firebSurvivors[ i ].step == 15 && firebPlayerPosition == 2 ) )
            {
                firebSurvivors[ i ].bounced = true;
                gbPlayOK();
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Drawing - direct ports of upstream's own drawBackground()/drawScore()/
// drawLives()/drawAmbulance()/drawSurvivors()/drawPlayer(). gbDrawBitmap()
// only ever uses gbColor for "on" bits (the bg half of gbSetColorBg() only
// matters to gbDrawChar() - see gamebuinoShim.h's own header comment), so
// every real upstream `setColor(X, Y)` two-arg call here is ported as a
// plain single-color `gbSetColor(X)` except drawScore()'s own genuine
// opaque-text case (see header comment).
// -----------------------------------------------------------------------------

void firebDrawBackground()
{
    gbSetColor( GB_GRAY );
    gbDrawBitmap( 0, 0, firebSubBackgroundBitmap[ firebIsClassic ] );

    gbSetColor( GB_BLACK );
    gbDrawBitmap( 0, 0, firebBackgroundBitmap[ firebIsClassic ] );
}

void firebDrawScore()
{
    if( firebIsClassic == 0 )
    {
        gbFontSize = 2;
        gbSetColorBg( GB_WHITE, GB_BLACK ); // real opaque WHITE-on-BLACK score box

        if( firebScore <= 9 )
          gbCursorX = 76;
        else if( firebScore <= 99 )
          gbCursorX = 68;
        else if( firebScore <= 999 )
          gbCursorX = 60;
        else
          gbCursorX = 52;

        gbCursorY = 2;
        gbPrintNumber( firebScore );

        gbFontSize = 1;
        gbSetColorBg( GB_BLACK, GB_WHITE );
    }
    else
    {
        gbFontSize = 1;
        gbSetColor( GB_BLACK );

        if( firebScore <= 9 )
          gbCursorX = 53;
        else if( firebScore <= 99 )
          gbCursorX = 49;
        else if( firebScore <= 999 )
          gbCursorX = 45;
        else if( firebScore <= 9999 )
          gbCursorX = 41;
        else if( firebScore <= 99999 )
          gbCursorX = 37;
        else if( firebScore <= 999999 )
          gbCursorX = 33;
        else if( firebScore <= 9999999 )
          gbCursorX = 29;
        else
          gbCursorX = 25;

        gbCursorY = 1;
        gbPrintNumber( firebScore );
    }
}

// Direct port of upstream's own real switch-case-fallthrough (lives==3
// draws all 3 positions, lives==2 draws 2, lives==1 draws 1) expressed as
// plain `>=` checks - no switch statement in this dialect.
void firebDrawLives()
{
    gbSetColor( GB_WHITE );
    if( firebLives >= 3 )
      gbDrawBitmap( firebLivesPositions[ firebIsClassic ][ 2 ][ 0 ], firebLivesPositions[ firebIsClassic ][ 2 ][ 1 ], firebLivesLightBitmap[ firebIsClassic ] );
    if( firebLives >= 2 )
      gbDrawBitmap( firebLivesPositions[ firebIsClassic ][ 1 ][ 0 ], firebLivesPositions[ firebIsClassic ][ 1 ][ 1 ], firebLivesLightBitmap[ firebIsClassic ] );
    if( firebLives >= 1 )
      gbDrawBitmap( firebLivesPositions[ firebIsClassic ][ 0 ][ 0 ], firebLivesPositions[ firebIsClassic ][ 0 ][ 1 ], firebLivesLightBitmap[ firebIsClassic ] );

    gbSetColor( GB_BLACK );
    if( firebLives >= 3 )
      gbDrawBitmap( firebLivesPositions[ firebIsClassic ][ 2 ][ 0 ], firebLivesPositions[ firebIsClassic ][ 2 ][ 1 ], firebLivesBitmap[ firebIsClassic ] );
    if( firebLives >= 2 )
      gbDrawBitmap( firebLivesPositions[ firebIsClassic ][ 1 ][ 0 ], firebLivesPositions[ firebIsClassic ][ 1 ][ 1 ], firebLivesBitmap[ firebIsClassic ] );
    if( firebLives >= 1 )
      gbDrawBitmap( firebLivesPositions[ firebIsClassic ][ 0 ][ 0 ], firebLivesPositions[ firebIsClassic ][ 0 ][ 1 ], firebLivesBitmap[ firebIsClassic ] );
}

void firebDrawAmbulance()
{
    int posX = firebAmbulancePositions[ firebIsClassic ][ 0 ];
    int posY = firebAmbulancePositions[ firebIsClassic ][ 1 ];

    gbSetColor( GB_WHITE );
    gbDrawBitmap( posX, posY, firebAmbulanceLightBitmap[ firebIsClassic ] );
    gbSetColor( GB_GRAY );
    gbDrawBitmap( posX, posY, firebAmbulanceShadowBitmap[ firebIsClassic ] );
    gbSetColor( GB_BLACK );
    gbDrawBitmap( posX, posY, firebAmbulanceBitmap[ firebIsClassic ] );
}

void firebDrawSurvivors()
{
    gbSetColor( GB_BLACK );

    int i, posX, posY, mult, variant;
    for( i = 0; i < FIREB_MAX_SURVIVORS; i = i + 1 )
    {
        if( firebSurvivors[ i ].active )
        {
            if( firebSurvivors[ i ].dead )
            {
                if( firebSurvivors[ i ].step > 10 )
                {
                    posX = firebSurvivorKOPositions[ firebIsClassic ][ 2 ][ 0 ];
                    posY = firebSurvivorKOPositions[ firebIsClassic ][ 2 ][ 1 ];
                }
                else if( firebSurvivors[ i ].step > 4 )
                {
                    posX = firebSurvivorKOPositions[ firebIsClassic ][ 1 ][ 0 ];
                    posY = firebSurvivorKOPositions[ firebIsClassic ][ 1 ][ 1 ];
                }
                else
                {
                    posX = firebSurvivorKOPositions[ firebIsClassic ][ 0 ][ 0 ];
                    posY = firebSurvivorKOPositions[ firebIsClassic ][ 0 ][ 1 ];
                }

                gbDrawBitmap( posX, posY, firebSurvivor3Bitmap[ firebIsClassic ] );
            }
            else if( firebSurvivors[ i ].delay > 0 )
            {
                posX = firebSurvivorIdlePositions[ firebIsClassic ][ 0 ];
                posY = firebSurvivorIdlePositions[ firebIsClassic ][ 1 ];
                mult = firebSurvivorIdlePositions[ firebIsClassic ][ 2 ];

                gbDrawBitmap( posX, posY + firebSurvivors[ i ].floor * mult, firebSurvivor0Bitmap[ firebIsClassic ] );
            }
            else
            {
                posX = firebSurvivorPositions[ firebIsClassic ][ firebSurvivors[ i ].step ][ 0 ];
                posY = firebSurvivorPositions[ firebIsClassic ][ firebSurvivors[ i ].step ][ 1 ];
                variant = firebSurvivorPositions[ firebIsClassic ][ firebSurvivors[ i ].step ][ 2 ];

                if( variant == 0 )
                  gbDrawBitmap( posX, posY, firebSurvivor0Bitmap[ firebIsClassic ] );
                else if( variant == 1 )
                  gbDrawBitmap( posX, posY, firebSurvivor1Bitmap[ firebIsClassic ] );
                else if( variant == 2 )
                  gbDrawBitmap( posX, posY, firebSurvivor2Bitmap[ firebIsClassic ] );
                else if( variant == 3 )
                  gbDrawBitmap( posX, posY, firebSurvivor3Bitmap[ firebIsClassic ] );
                else if( variant == 4 )
                  gbDrawBitmap( posX, posY, firebSurvivor4Bitmap[ firebIsClassic ] );
                // 5-9: same 5 bitmaps, flipped horizontally (real upstream NOROT/FLIPH)
                else if( variant == 5 )
                  gbDrawBitmapRotated( posX, posY, firebSurvivor0Bitmap[ firebIsClassic ], 0, 1 );
                else if( variant == 6 )
                  gbDrawBitmapRotated( posX, posY, firebSurvivor1Bitmap[ firebIsClassic ], 0, 1 );
                else if( variant == 7 )
                  gbDrawBitmapRotated( posX, posY, firebSurvivor2Bitmap[ firebIsClassic ], 0, 1 );
                else if( variant == 8 )
                  gbDrawBitmapRotated( posX, posY, firebSurvivor3Bitmap[ firebIsClassic ], 0, 1 );
                else
                  gbDrawBitmapRotated( posX, posY, firebSurvivor4Bitmap[ firebIsClassic ], 0, 1 );
            }
        }
    }
}

void firebDrawPlayer()
{
    int posX = firebPlayerPositions[ firebIsClassic ][ firebPlayerPosition ][ 0 ];
    int posY = firebPlayerPositions[ firebIsClassic ][ firebPlayerPosition ][ 1 ];

    gbSetColor( GB_BLACK );
    gbDrawBitmap( posX, posY, firebPlayerBitmap[ firebIsClassic ] );
}

void firebDrawGameOver()
{
    gbSetColor( GB_WHITE );
    gbFillRect( 24, 20, 37, 7 );
    gbSetColor( GB_BLACK );
    gbCursorX = 25;
    gbCursorY = 21;
    gbPrintString( "GAME OVER" );

    if( firebScore > firebMinHighscore )
    {
        gbSetColor( GB_WHITE );
        gbFillRect( 14, 29, 57, 7 );
        gbSetColor( GB_BLACK );
        gbCursorX = 15;
        gbCursorY = 30;
        gbPrintString( "NEW HIGHSCORE!" );
    }

    gbSetColor( GB_WHITE );
    gbFillRect( 55, LCDHEIGHT - gbFontHeight - 1, gbFontWidth * 7, gbFontHeight );
    gbSetColor( GB_BLACK );
    gbCursorX = 56;
    gbCursorY = LCDHEIGHT - gbFontHeight;
    gbPrintString( firebTextMenu );
}

void firebDrawPaused()
{
    gbSetColor( GB_WHITE );
    gbFillRect( 28, 19, gbFontWidth * 6, gbFontHeight );
    gbSetColor( GB_BLACK );
    gbCursorX = 29;
    gbCursorY = 20;
    gbPrintString( "PAUSED" );

    gbSetColor( GB_WHITE );
    gbFillRect( 39, LCDHEIGHT - ( gbFontHeight * 2 ) - 1, gbFontWidth * 11, gbFontHeight );
    gbSetColor( GB_BLACK );
    gbCursorX = 40;
    gbCursorY = LCDHEIGHT - ( gbFontHeight * 2 );
    gbPrintString( firebTextContinue );

    gbSetColor( GB_WHITE );
    gbFillRect( 55, LCDHEIGHT - gbFontHeight - 1, gbFontWidth * 7, gbFontHeight );
    gbSetColor( GB_BLACK );
    gbCursorX = 56;
    gbCursorY = LCDHEIGHT - gbFontHeight;
    gbPrintString( firebTextQuit );
}

// -----------------------------------------------------------------------------
// State machine - see header comment for the full real blocking-loop ->
// state conversion story.
// -----------------------------------------------------------------------------

void firebBeginHighscores( int returnState )
{
    firebHighscoreReturnState = returnState;
    firebState = FIREB_STATE_HIGHSCORES;
}

// PLAYING / PAUSED / GAMEOVER share one real draw sequence, matching
// upstream's own real per-tick draw order in loop() exactly.
void firebUpdatePlayCommon()
{
    firebDrawBackground();
    firebDrawScore();
    firebDrawLives();
    firebDrawAmbulance();

    if( firebState == FIREB_STATE_PLAYING )
    {
        firebMovePlayer();
        firebGameLogic();
        firebCheckBounces();
    }

    firebDrawSurvivors();
    firebDrawPlayer();

    if( firebState == FIREB_STATE_PAUSED )
    {
        firebDrawPaused();
        if( gbPressed( BTN_A ) )
          firebState = FIREB_STATE_PLAYING;
    }

    if( firebState == FIREB_STATE_GAMEOVER )
      firebDrawGameOver();

    if( gbPressed( BTN_C ) )
    {
        if( firebState == FIREB_STATE_PLAYING )
          firebState = FIREB_STATE_PAUSED;
        else
        {
            // PAUSED or GAMEOVER, real upstream's own "quit to title" path
            if( firebScore > firebMinHighscore )
            {
                firebSaveHighscore();
                firebBeginHighscores( FIREB_STATE_TITLE );
            }
            else
              firebState = FIREB_STATE_TITLE;
        }
    }
}

void firebUpdateTitle()
{
    gbFontSize = 1;
    gbSetColor( GB_BLACK );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, firebTitleScreenBitmap );

    gbCursorX = 28;
    gbCursorY = 36;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        firebInitGame();
        firebState = FIREB_STATE_MENU;
    }
}

void firebUpdateMenu()
{
    gbFontSize = 1;
    gbSetColor( GB_BLACK );
    gbCursorX = 22;
    gbCursorY = 1;
    gbPrintString( "FIREBUINO!" );

    int i;
    for( i = 0; i < FIREB_MENU_LEN; i = i + 1 )
    {
        gbCursorY = 12 + i * 9;
        gbCursorX = 0;
        if( i == firebMenuIndex )
          gbPrintString( ">" );

        gbCursorX = 8;
        if( i == 0 )
          gbPrintString( "Play (new)" );
        else if( i == 1 )
          gbPrintString( "Play (classic)" );
        else if( i == 2 )
          gbPrintString( "High scores" );
        else
          gbPrintString( "About" );
    }

    if( gbRepeat( BTN_UP, 5 ) )
      firebMenuIndex = gbMax( 0, firebMenuIndex - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) )
      firebMenuIndex = gbMin( FIREB_MENU_LEN - 1, firebMenuIndex + 1 );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( firebMenuIndex == 0 )
        {
            firebIsClassic = 0;
            firebState = FIREB_STATE_PLAYING;
        }
        else if( firebMenuIndex == 1 )
        {
            firebIsClassic = 1;
            firebState = FIREB_STATE_PLAYING;
        }
        else if( firebMenuIndex == 2 )
          firebBeginHighscores( FIREB_STATE_MENU );
        else
          firebState = FIREB_STATE_ABOUT;
    }
    else if( gbPressed( BTN_B ) )
      firebState = FIREB_STATE_TITLE;
}

// Direct port of upstream's own drawHighScores() - minus the dropped name
// column (see header comment), and its own real blocking `while(true)`
// loop replaced by one shared state with a remembered return target.
void firebUpdateHighscores()
{
    gbFontSize = 1;
    gbSetColor( GB_BLACK );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

    gbCursorX = 20;
    gbCursorY = 3;
    gbPrintString( "HIGH SCORES" );

    int i, digits;
    for( i = 0; i < FIREB_HIGHSCORE_COUNT; i = i + 1 )
    {
        digits = 1;
        if( firebHighscores[ i ] > 9999 )
          digits = 5;
        else if( firebHighscores[ i ] > 999 )
          digits = 4;
        else if( firebHighscores[ i ] > 99 )
          digits = 3;
        else if( firebHighscores[ i ] > 9 )
          digits = 2;

        gbCursorX = LCDWIDTH - 6 - digits * gbFontWidth;
        gbCursorY = ( gbFontHeight * 2 ) + gbFontHeight * i;
        gbPrintNumber( firebHighscores[ i ] );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        firebState = firebHighscoreReturnState;
    }
}

// Direct port of upstream's own drawCredits().
void firebUpdateAbout()
{
    gbFontSize = 1;
    gbSetColor( GB_BLACK );

    gbCursorX = 24;
    gbCursorY = 6;
    gbPrintString( "Developer:" );
    gbCursorX = 28;
    gbCursorY = 14;
    gbPrintString( "LADBSoft" );
    gbCursorX = 2;
    gbCursorY = 26;
    gbPrintString( "Awesome new graphics:" );
    gbCursorX = 34;
    gbCursorY = 34;
    gbPrintString( "erico" );

    gbCursorX = 56;
    gbCursorY = LCDHEIGHT - gbFontHeight;
    gbPrintString( firebTextBack );

    if( gbPressed( BTN_C ) )
    {
        gbPlayOK();
        firebState = FIREB_STATE_MENU;
    }
}

void gameFirebuino_init()
{
    gbBegin();
    firebLoadHighscores();
    firebState = FIREB_STATE_TITLE;
}

void gameFirebuino_update()
{
    if( !gbUpdate() )
      return;

    if( firebState == FIREB_STATE_TITLE )
      firebUpdateTitle();
    else if( firebState == FIREB_STATE_MENU )
      firebUpdateMenu();
    else if( firebState == FIREB_STATE_HIGHSCORES )
      firebUpdateHighscores();
    else if( firebState == FIREB_STATE_ABOUT )
      firebUpdateAbout();
    else
      firebUpdatePlayCommon(); // PLAYING / PAUSED / GAMEOVER

    gbRenderFrame();
}
