#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Spin Spin Spinbuino! (Charly Piva "Zoglu" / Margot Piva "Isil",
// zoglu.net - license none specified). A real-time dexterity/avoidance
// game: the player controls a spinning two-headed baton (constantly
// rotating on its own, direction reversible) that must be steered by the
// D-pad through 8 real hand-designed tile-mask levels toward a goal
// marker without letting either tip of the baton touch a wall, while a
// stopwatch times the run against a real per-level target time. Real
// upstream source: 10 real .ino tabs (spin/menu/niveaux/level_design/
// Joueur/collisions/explosions/affichages/bitmaps/credits.ino, 1560 lines
// total), real #include <Gamebuino.h> + <EEPROM.h>. Recovered via direct
// download (zoglu.net/trucs/spin_full.zip, the wiki's own listed link,
// still live) - a real wiki-listed game that was staged early but fell
// through the cracks of this project's own tier-triage process until a
// direct "status report on missing games" audit found it (see
// `more games/DISCOVERED_GAMES.md`'s own "Two more real staged
// directories..." section).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment for the full reasoning). Upstream's
// own single-instance `Joueur` struct was flattened into plain
// `spin`-prefixed globals (`spinX`/`spinY`/`spinXDiff`/`spinAngle`/etc)
// rather than ported as an actual struct - this project has no proven
// precedent for a struct with this many scalar fields, and flattening
// costs nothing for a genuinely single-instance object (the same
// treatment gamePunkt.c's own header comment already establishes for
// "many small objects" via parallel arrays - here there's only ever one
// object, so plain globals are the natural equivalent). Real `byte`/
// `char`/`unsigned int`/`boolean` fields all become plain `int` (this
// dialect has no narrower native types - see gameMaze.c's own header
// comment on why "just use int" costs nothing here); every real value in
// this game stays safely within 32-bit range throughout (level
// coordinates top out at 256, `x_full`/`y_full` at `x*5`/`y*5` <= 1280,
// nowhere near any width where AVR's native 16-bit `int` would have
// wrapped differently than this dialect's own always-32-bit `int` -
// checked directly, not assumed, per this project's own established
// narrow-int audit discipline).
//
// LEVEL DATA: upstream's own 8 real hand-designed levels (`niveau_0`..
// `niveau_7`, each a real 32-row x 8-byte, MSB-first tile-mask bitmap -
// one bit = one 4x4px solid block, decoded directly by `spinAfficheDecor()`
// below) plus their own real per-level settings arrays (start/goal
// position, spring count, spring positions/facings) were extracted
// byte-for-byte from `level_design.ino` via a small Python script (every
// `B01111111`-style Arduino binary literal converted straight to `0x7F`
// hex - this dialect has no such literal syntax - not hand-transcribed),
// stored as plain `int[256]` per-level data arrays and `int[]` settings
// arrays, verified against upstream's own real byte counts (every level
// body is exactly 256 bytes; every settings array's own real length,
// 5 to 35 bytes, matched exactly). Upstream's own real commented-out
// *earlier* draft level data (`level_design.ino`'s own large `/* ... */`
// block near the top of the file, an alternate `niveau_0`/`niveau_1` pair
// never actually compiled into any real shipped build) was correctly
// skipped, not accidentally included.
//
// `current_level`/`current_settings` (real `prog_uchar*` pointers,
// PROGMEM being a real no-op in this dialect per `avrCompat.h`) became
// plain `int*` globals (`spinCurrentLevel`/`spinCurrentSettings`),
// assigned directly from whichever `spinLevelNData`/`spinLevelNSettings`
// array `spinInitNiveau()` selects via a plain switch (upstream's own
// real `niveaux[]`/`niveaux_settings[]` array-of-pointers indirection was
// dropped - upstream's own `initNiveau()` already switches on the level
// number before ever touching that array, so the array-of-pointers layer
// was pure redundancy to begin with, not a real behavioral difference).
// `pgm_read_byte_near(current_level + idx)` became a small bounds-checked
// helper, `spinLevelByte()`, rather than a bare `spinCurrentLevel[idx]`
// read - a deliberate, defensive addition, not a real upstream behavior:
// `spinAfficheDecor()`'s own real scrolling-tile-lookup formula (ported
// unchanged from upstream) can compute an index past the real 256-byte
// level array's own end whenever the player is near a level's outer edge
// (upstream's own real comment-free code relies on real AVR flash simply
// returning whatever adjacent PROGMEM data happens to sit there, rendered
// harmlessly as "some other decorative block" and then immediately
// painted over by this same function's own explicit solid-black
// out-of-level-bounds border fill a few lines later - every real level's
// own solid outer wall ring keeps the player far enough from this in
// practice that it's cosmetically invisible on real hardware). This
// dialect's own arrays have no real adjacent-memory guarantee to lean on
// the same way, so `spinLevelByte()` simply returns 0 (an empty/background
// block) for any out-of-[0,255] index instead - functionally identical to
// upstream's own real on-screen result (still immediately overpainted by
// the same border fill either way), just without relying on undefined
// out-of-bounds memory contents to get there.
//
// BLOCKING `gb.titleScreen(F("Spin Spin Spinbuino!"))` -> EXPLICIT STATE:
// upstream's own real `initGame()` (which also reloads every real
// EEPROM-backed level-complete/highscore value and re-derives
// `unlocked_level`) is called both from `setup()` (real boot) and again
// from `updateMenu()` any time Button B or C is pressed on the level-
// select menu - a genuine "return to the title/logo screen" gesture, not
// a hard reset (menu position/highscores are all reloaded from the same
// real EEPROM values that were just saved). Ported as one shared
// `SPIN_MODE_TITLE` state (the "blocking widget -> explicit resumable
// state" treatment used throughout this project, see gamePong.c's own
// header comment) - `spinUpdateTitle()` draws the real title text plus a
// "PRESS A" prompt (matching every other ported game's own titleScreen()
// restoration; real `Gamebuino::titleScreen(title)` draws quite a bit
// more - the real boot logo, A/B/C button-icon hints - none of which
// exists in this shim, the same already-established simplification every
// other title-screen port here uses) and re-runs the same real EEPROM-
// reload/`spinInitMenu()` sequence on a fresh A-press.
// `gb.battery.show = false;` was dropped outright (no battery indicator
// exists in this shim at all - purely cosmetic on real hardware, matching
// every other port's own precedent).
//
// FRAME COUNTER: `afficheArrivee()`'s own real `gb.frameCount % 4` goal-
// marker animation cycle uses this shim's own `gbFrameCount` global
// directly (incremented once per real logic tick inside `gbUpdate()`,
// matching real hardware's own placement) - no local counter needed.
//
// REAL EEPROM PERSISTENCE - genuinely ported, not invented: upstream
// really does call `EEPROM.read()`/`EEPROM.write()` for each of the 8
// real per-level "complete" flags (address `i`, one byte) and 8 real
// per-level highscore timer values (address `32+i*2`, a real two-byte
// LSB/MSB-composed word) - ported via this project's own
// `eeprom_read_byte()`/`eeprom_write_byte()`/`eeprom_read_word()`/
// `eeprom_write_word()` at the exact same real addresses, rather than
// upstream's own manual LSB/MSB composition (this shim's own
// `eeprom_read_word()`/`eeprom_write_word()` already do exactly that
// composition internally - see gamePunkt.c's own header comment on this
// same shim primitive). Upstream's own real
// `if(highscores[i] > 2000) highscores[i] = 0;` fresh-value clamp
// (checked directly against this project's own established EEPROM
// int-narrowing audit - see CLAUDE.md's own "A project-wide EEPROM
// audit" section) already safely handles a genuinely fresh EEPROM cell
// here without any extra fix needed: a freshly-erased word composes to
// 65535 on both real hardware and this dialect alike (this dialect's
// `int` never narrows the way AVR's real 16-bit `int` would, but 65535
// stays well outside the same `> 2000` bound either way) - a real,
// already-safe "Category 1" case per that audit's own classification, not
// a new gap.
//
// REAL UPSTREAM QUIRKS PRESERVED, NOT "FIXED":
//
// 1. Real fallthrough in `drawExplosion()` (`explosions.ino`): `case 0`
//    has no `break` (falls into `case 4`'s own extra dots), and `case 1`
//    has no `break` either (falls into `case 3`'s own extra dots) - `case
//    2`/`case 3`/`case 4`/`case 5` all do `break` normally. A real,
//    deliberate-looking "denser explosion frame" effect (frames 0 and 1
//    draw more dots than frames 2/3/4/5 alone would), reproduced exactly
//    via the same real fallthrough in `spinDrawExplosion()` below, not
//    "corrected" into one dot-set per frame.
// 2. Real icon-glyph text strings (`"\25Next \26Retry \27Menu"` and
//    similar - octal escapes 025/026/027 = ASCII 21/22/23, real
//    Gamebuino font5x7 D-pad/button icon glyphs, not printable
//    characters) can't be held in a plain quoted string literal in this
//    dialect (confirmed unsupported outside ASCII 32-127, the same
//    finding gameTaquin.c's/gameSimonbuino.c's own header comments
//    already document) - ported as explicit 0-terminated `char[]` arrays
//    of literal decimal character codes instead (`spinFinPrompt`/
//    `spinRetryPrompt`/`spinPausePrompt`/`spinUnlockYes`/`spinUnlockNo`
//    below), the same treatment those two files already established.
//
// SOUND - GENUINELY RESTORED, not approximated: upstream's own single
// non-one-shot sound call, `gb.sound.playPattern(destroy, 0)` on a wall
// hit, is ported directly via the shim's own real tracker-engine
// `gbPlayPattern()` primitive against `spinDestroyPattern` (the real,
// verbatim `destroy[]` PROGMEM word array from spin.ino - 4 note/command
// words plus the real 0x0000 pattern terminator, matching upstream's own
// declared 5-word length exactly). No `gbChangePatternSet()`/
// `gbChangeInstrumentSet()` call is needed on channel 0 for this: the
// pattern array is played directly rather than looked up by ID, and an
// untouched channel already carries the real default square/noise
// instrument pair. `gb.sound.playOK()`/`playCancel()`/`playTick()`
// (Joueur.ino/menu.ino/credits.ino) are already real one-shot primitives
// with a direct `gbPlayOK()`/`gbPlayCancel()`/`gbPlayTick()` equivalent,
// so those call sites needed no change.
//
// gb.pickRandomSeed()/random() are never called anywhere in this real
// game (it's entirely deterministic - hand-designed levels, no
// procedural content) - no `arand()` usage needed anywhere in this port.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameSpinSpinSpinbuino.c (which already did the real
// upstream-Arduino-to-Vircon32-dialect porting work documented above) -
// converting Vircon32's own C dialect back to standard C:
//  - Every `TYPE[N] name = {...};` / `TYPE[N] name;` array declaration
//    (all 8 real level-data tables, all 8 settings tables, both goal
//    bitmaps, all 4 spring bitmaps, all 8 level-select icon bitmaps, and
//    the plain state arrays `spinAnimRessorts`/`spinComplete`/
//    `spinHighscores`/`spinTarget`) -> standard `TYPE name[N] = {...};` /
//    `TYPE name[N];` declaration order. Every numeric data table's real
//    byte content was copied directly from the source file (never
//    retyped from memory) and diff-verified byte-for-byte afterward.
//  - The 5 real icon-glyph button-prompt "strings"
//    (`spinFinPrompt`/`spinRetryPrompt`/`spinPausePrompt`/
//    `spinUnlockYes`/`spinUnlockNo`), each a Vircon32-dialect
//    `int[N]`-of-decimal-character-codes (needed there because a plain
//    quoted string literal can't hold non-printable icon-glyph codes -
//    see quirk 2 above), became standard `char name[N] = {...};` arrays
//    of the identical decimal values - this dialect's `char` type and
//    `gbPrintString( char* )` signature make the `int[]` workaround
//    unnecessary, but the non-printable byte values themselves still
//    can't be written as a quoted string literal, so the explicit decimal
//    initializer-list form was kept unchanged.
//  - `int* spinRessortBitmap(...)` / `int* spinLevelIconBitmap(...)` /
//    `int* spinCurrentLevel` / `int* spinCurrentSettings` return/pointer
//    types are unchanged - these point at real numeric bitmap/level/
//    settings data tables, not text, so they stay `int*` per this
//    project's own established "data stays int[], text becomes char[]"
//    rule.
//  - `void spinPrintLine( int* text )` -> `void spinPrintLine( char* text )`
//    (its own real parameter is always one of the plain C string literals
//    used throughout `spinUpdateCredits()`, e.g. "Do you want to").
//  - Added a standard 4-include block (`avrCompat.h`/`machineDependent.h`/
//    `gamebuinoShim.h`/`eepromShim.h`) in place of Vircon32's own
//    no-explicit-include convention; `avrCompat.h` already pulls in
//    `<math.h>` (needed here for `cos()`/`sin()`), matching every other
//    already-ported game that uses trig.
// No other change was made - every real EEPROM call site
// (`eeprom_read_byte()`/`eeprom_write_byte()`/`eeprom_read_word()`/
// `eeprom_write_word()`) already matches this project's own eepromShim.h
// signatures unchanged, and this project's own eepromSelectGame()
// (gamesMain.c) already resolves the right on-disk slot before this
// game's own init() ever runs.

#define SPIN_TAILLE_JOUEUR 2
#define SPIN_TAILLE_BATON 10
#define SPIN_JOUEUR_ECRAN_X 42
#define SPIN_JOUEUR_ECRAN_Y 24
#define SPIN_VITESSE_DROITE 10
#define SPIN_NOMBRE_NIVEAUX 8

#define SPIN_MODE_TITLE 0
#define SPIN_MODE_MENU 1
#define SPIN_MODE_NIVEAU 2
#define SPIN_MODE_CREDITS 3

// niveau_0 - 256 bytes (32 rows x 8 bytes)
int spinLevel0Data[256] =
{
    0x80,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x0,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x3F,0x80,0x7F,0xFF,0xFF,0xFF,0xFF,
    0x80,0x7F,0x80,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0x80,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xE0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0x7F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3,0xFF,0x80,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3,0x80,0x0,0x7F,0xFF,0xFF,
    0xFF,0xC0,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xC0,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xC0,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xC0,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xC0,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xE0,0x3,0x80,0x0,0x7F,0xFF,0xFF,
    0xFF,0xF0,0x3,0xFF,0x80,0xFF,0xFF,0xFF,
};

// niveau_1 - 256 bytes (32 rows x 8 bytes)
int spinLevel1Data[256] =
{
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0x1,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0x1,0xFF,0xFF,
    0xFF,0xF0,0x0,0x3,0xF8,0x1,0xFF,0xFF,
    0xFF,0xE0,0x0,0x1,0xF8,0x1,0xFF,0xFF,
    0xFF,0xE0,0x0,0x1,0xF8,0x1,0xFF,0xFF,
    0xFF,0xE0,0x0,0x1,0xF8,0x1,0xFF,0xFF,
    0xFF,0xE0,0x0,0x1,0xF8,0x1,0xFF,0xFF,
    0xFF,0xE0,0x0,0x1,0xF8,0x1,0xFF,0xFF,
    0xFF,0xE0,0x7F,0x81,0xF8,0x1,0xFF,0xFF,
    0xFF,0xE0,0x7F,0x81,0xFC,0x3,0xFF,0xFF,
    0xFF,0xE0,0x7F,0x81,0xFC,0x3,0xFF,0xFF,
    0xFF,0xE0,0x7F,0x81,0xFC,0x3,0xFF,0xFF,
    0xFF,0xE0,0x7F,0x81,0xFC,0x3,0xFF,0xFF,
    0xFF,0xE0,0x7F,0x81,0xFC,0x3,0xFF,0xFF,
    0xFF,0xE0,0x7F,0x81,0xFC,0x3,0xFF,0xFF,
    0xFF,0xC0,0x3F,0x80,0x0,0x3,0xFF,0xFF,
    0xFF,0xC0,0x3F,0x80,0x0,0x3,0xFF,0xFF,
    0xFF,0xC0,0x3F,0x80,0x0,0x3,0xFF,0xFF,
    0xFF,0xC0,0x3F,0x80,0x0,0x3,0xFF,0xFF,
    0xFF,0xC0,0x3F,0x80,0x0,0x3,0xFF,0xFF,
    0xFF,0xC0,0x3F,0x80,0x0,0x3,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xC0,0x0,0x7,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

// niveau_2 - 256 bytes (32 rows x 8 bytes)
int spinLevel2Data[256] =
{
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x30,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xF0,0xF0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xF0,0xF0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xF0,0xF0,0xF,0xFF,0xFF,0xFF,0xFF,
    0x0,0xC0,0x3E,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x3E,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x3E,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x20,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x20,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x20,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x20,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0xC0,0x20,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x81,0xFF,0xE0,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x81,0xFF,0xE0,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xE0,0x1F,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

// niveau_3 - 256 bytes (32 rows x 8 bytes)
int spinLevel3Data[256] =
{
    0x0,0xF8,0x0,0x0,0x0,0x3F,0x0,0x0,
    0x0,0xF8,0x1F,0x0,0x0,0x3F,0x0,0x0,
    0x0,0x0,0x1F,0x0,0x0,0x0,0x3F,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x3F,0x0,
    0x0,0x0,0x0,0x3,0xE0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x3,0xE0,0x0,0x0,0x0,
    0x0,0x1F,0x0,0x0,0x1,0xF8,0x0,0x0,
    0x0,0x1F,0x3,0xE0,0x1,0xF8,0x0,0x0,
    0x0,0x0,0x3,0xE0,0x0,0x0,0x0,0x0,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

// niveau_4 - 256 bytes (32 rows x 8 bytes)
int spinLevel4Data[256] =
{
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x0,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x1F,0xE0,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x20,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x20,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x20,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x20,0x7F,0xFF,0xFF,
    0xFF,0xFF,0x0,0x0,0x20,0x7F,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xF8,0x20,0x7F,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xF8,0x20,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x18,0x20,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x0,0x20,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x0,0x20,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x0,0x20,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x0,0x20,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x0,0x20,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x1F,0xE0,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x1F,0xE0,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x1F,0xE0,0xF,0xFF,0xFF,
    0xFF,0xF0,0x0,0x1F,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

// niveau_5 - 256 bytes (32 rows x 8 bytes)
int spinLevel5Data[256] =
{
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x7,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x7,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x7,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x3C,0x1,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x3C,0x1,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x3C,0x1,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x3C,0x1,0xFF,0xFF,0xFF,0xFF,
    0x0,0x0,0x3C,0x1,0xFF,0xFF,0xFF,0xFF,
    0x3,0xF8,0x3C,0x1,0xFF,0xFF,0xFF,0xFF,
    0x3,0xF8,0x3C,0x1,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x4,0x1,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x4,0x1,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x6,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x6,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x6,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x6,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x1E,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC0,0x1E,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFC,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFC,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFC,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFC,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFC,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
};

// niveau_6 - 256 bytes (32 rows x 8 bytes)
int spinLevel6Data[256] =
{
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFE,0x7,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFE,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFE,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFE,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFE,0x0,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFE,0x7,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0x0,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0x0,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0x0,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0x0,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0x0,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFE,0x0,0xFE,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xF8,0xF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

// niveau_7 - 256 bytes (32 rows x 8 bytes)
int spinLevel7Data[256] =
{
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x0,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x0,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x0,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x0,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xF,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xF0,0xF,0x7,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xF0,0xF,0x7,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xF0,0xF,0x7,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xF0,0x1,0x7,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xF0,0x1,0x7,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xF0,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xC1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x81,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x81,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0xC1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0x1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0x0,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0x0,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0x0,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0xFC,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0xFC,0xF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

// niveau_0_settings - 5 bytes
int spinLevel0Settings[5] = { 22, 22, 144, 110, 0 };
// niveau_1_settings - 5 bytes
int spinLevel1Settings[5] = { 56, 84, 168, 24, 0 };
// niveau_2_settings - 5 bytes
int spinLevel2Settings[5] = { 16, 116, 88, 88, 0 };
// niveau_3_settings - 5 bytes
int spinLevel3Settings[5] = { 14, 18, 242, 18, 0 };
// niveau_4_settings - 35 bytes
int spinLevel4Settings[35] = { 84, 76, 158, 74, 10, 48, 56, 3, 48, 64, 3, 48, 72, 3, 48, 80, 3, 48, 88, 3, 64, 8, 3, 64, 16, 3, 64, 24, 3, 64, 32, 3, 64, 40, 3 };
// niveau_5_settings - 23 bytes
int spinLevel5Settings[23] = { 106, 66, 18, 18, 6, 0, 68, 0, 8, 68, 0, 16, 68, 0, 76, 76, 1, 76, 84, 1, 76, 92, 1 };
// niveau_6_settings - 14 bytes
int spinLevel6Settings[14] = { 110, 58, 72, 20, 3, 148, 76, 3, 148, 84, 3, 148, 92, 3 };
// niveau_7_settings - 23 bytes
int spinLevel7Settings[23] = { 18, 114, 74, 64, 6, 56, 120, 0, 64, 120, 0, 72, 120, 0, 16, 56, 3, 16, 64, 3, 16, 72, 3 };
int spinGoalBitmap[24] = { 16, 11, 0xE, 0x0, 0x3E, 0x0, 0x70, 0x0, 0x6E, 0x0, 0x1B, 0x0, 0x11, 0x0, 0x1B, 0x0, 0xE, 0xC0, 0x1, 0xC0, 0xF, 0x80, 0xE, 0x0 };
int spinGoal2Bitmap[24] = { 16, 11, 0xE, 0x0, 0xF, 0x80, 0x1, 0xC0, 0xE, 0xC0, 0x1B, 0x0, 0x11, 0x0, 0x1B, 0x0, 0x6E, 0x0, 0x70, 0x0, 0x3E, 0x0, 0xE, 0x0 };
int spinRessort0Bitmap[10] = { 8, 8, 0x7E, 0xFF, 0xFF, 0xFF, 0x42, 0x7E, 0x42, 0x7E };
int spinRessort1Bitmap[10] = { 8, 8, 0x0, 0x7E, 0xFF, 0xFF, 0xFF, 0x7E, 0x42, 0x7E };
int spinRessort2Bitmap[10] = { 8, 8, 0x0, 0x0, 0x7E, 0xFF, 0xFF, 0xFF, 0x7E, 0x7E };
int spinRessort3Bitmap[10] = { 8, 8, 0x0, 0x0, 0x0, 0x7E, 0xFF, 0xFF, 0xFF, 0x7E };
int spinLevelIcon1Bitmap[10] = { 8, 8, 0x0, 0x0, 0x3C, 0xC3, 0x81, 0xC3, 0x3C, 0x0 };
int spinLevelIcon2Bitmap[10] = { 8, 8, 0x0, 0x0, 0x3C, 0xFF, 0xFF, 0xFF, 0x3C, 0x0 };
int spinLevelIcon3Bitmap[10] = { 8, 8, 0x0, 0x18, 0xE7, 0x81, 0x66, 0x5A, 0x66, 0x0 };
int spinLevelIcon4Bitmap[10] = { 8, 8, 0x0, 0x18, 0xFF, 0xFF, 0x7E, 0x7E, 0x66, 0x0 };
int spinLevelIcon5Bitmap[10] = { 8, 8, 0x1E, 0x12, 0x12, 0x12, 0x12, 0x1E, 0x12, 0x1E };
int spinLevelIcon6Bitmap[10] = { 8, 8, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E };
int spinLevelIcon7Bitmap[10] = { 8, 8, 0x3C, 0x42, 0xA5, 0xA5, 0x81, 0x99, 0x42, 0x3C };
int spinLevelIcon8Bitmap[10] = { 8, 8, 0x3C, 0x7E, 0xDB, 0xDB, 0xFF, 0xE7, 0x7E, 0x3C };
char spinFinPrompt[19] = { 21, 78, 101, 120, 116, 32, 22, 82, 101, 116, 114, 121, 32, 23, 77, 101, 110, 117, 0 };
char spinRetryPrompt[15] = { 21, 47, 22, 82, 101, 116, 114, 121, 32, 23, 77, 101, 110, 117, 0 };
char spinPausePrompt[22] = { 21, 77, 101, 110, 117, 32, 22, 82, 101, 116, 114, 121, 32, 23, 85, 110, 112, 97, 117, 115, 101, 0 };
char spinUnlockYes[16] = { 21, 76, 101, 116, 39, 115, 32, 100, 111, 32, 116, 104, 105, 115, 33, 0 };
char spinUnlockNo[9] = { 22, 78, 111, 32, 119, 97, 121, 33, 0 };

// Real `destroy[]` pattern (spin.ino) - a tiny 4-note, 0-terminated
// tracker pattern played on a wall hit via real `gb.sound.playPattern(
// destroy, 0)`. Copied verbatim from upstream's own real word values
// (`0x8045,0x8891,0x8241,0x0608,0x0000` - the trailing 0x0000 is the real
// pattern terminator), not re-derived - channel 0 needs no
// gbChangePatternSet()/gbChangeInstrumentSet() call first, since
// gbPlayPattern() takes this array directly rather than looking it up by
// ID, and a channel with no instrument set of its own already gets the
// real default square/noise pair.
int spinDestroyPattern[5] = { 0x8045, 0x8891, 0x8241, 0x0608, 0x0000 };

// ---- end of generated data section ----

// ---- flattened Joueur state ----
int spinX, spinY;
int spinXDiff, spinYDiff;
int spinXFull, spinYFull;
int spinAngle;
int spinAngleLastStart;
int spinHit;
int spinReverse;
int spinTaille;
int spinCircleX, spinCircleY;
int spinTimer, spinHighscore;
int spinXGoal, spinYGoal;
int spinGoal;
int spinMaxRessorts;
int spinStart;
int spinLastDirection;
int spinPause;
int spinPauseOff;

int spinAnimRessorts[32];

int spinMode;
int* spinCurrentLevel;
int* spinCurrentSettings;
int spinCurrentNumLevel;
int spinComplete[8];
int spinUnlockedLevel;
int spinHighscores[8];
int spinTarget[8];
int spinMenuFrames;
int spinGameComplete;

// ---- small helpers ----

int spinLevelByte( int idx )
{
    if( idx < 0 || idx >= 256 )
      return 0;

    return spinCurrentLevel[ idx ];
}

int spinRoundF( float f )
{
    if( f >= 0.0 )
      return (int)( f + 0.5 );

    return (int)( f - 0.5 );
}

// ---- level select / EEPROM ----

void spinCalcUnlockedLevel()
{
    int i;
    int lo = gbMax( 1, spinUnlockedLevel );

    for( i = lo; i < SPIN_NOMBRE_NIVEAUX; i++ )
    {
        if( spinComplete[ i - 1 ] == 1 )
          spinUnlockedLevel = i;
    }
}

void spinInitMenu( int lvl )
{
    int i;

    spinCalcUnlockedLevel();
    spinCurrentNumLevel = lvl;
    spinGameComplete = 1;

    for( i = 0; i < SPIN_NOMBRE_NIVEAUX; i++ )
    {
        if( spinComplete[ i ] != 1 )
          spinGameComplete = 0;

        eeprom_write_byte( i, spinComplete[ i ] );
        eeprom_write_word( 32 + i * 2, spinHighscores[ i ] );
    }
}

void spinLoadEeprom()
{
    int i;

    for( i = 0; i < SPIN_NOMBRE_NIVEAUX; i++ )
    {
        spinComplete[ i ] = eeprom_read_byte( i );
        spinHighscores[ i ] = eeprom_read_word( 32 + i * 2 );
        if( spinHighscores[ i ] > 2000 || spinHighscores[ i ] < 0 )
          spinHighscores[ i ] = 0;
    }
}

void spinInitNiveau( int lvl )
{
    spinCurrentNumLevel = lvl;

    switch( spinCurrentNumLevel )
    {
        case 1: spinCurrentLevel = spinLevel1Data; spinCurrentSettings = spinLevel1Settings; break;
        case 2: spinCurrentLevel = spinLevel2Data; spinCurrentSettings = spinLevel2Settings; break;
        case 3: spinCurrentLevel = spinLevel3Data; spinCurrentSettings = spinLevel3Settings; break;
        case 4: spinCurrentLevel = spinLevel4Data; spinCurrentSettings = spinLevel4Settings; break;
        case 5: spinCurrentLevel = spinLevel5Data; spinCurrentSettings = spinLevel5Settings; break;
        case 6: spinCurrentLevel = spinLevel6Data; spinCurrentSettings = spinLevel6Settings; break;
        case 7: spinCurrentLevel = spinLevel7Data; spinCurrentSettings = spinLevel7Settings; break;
        default: spinCurrentLevel = spinLevel0Data; spinCurrentSettings = spinLevel0Settings; break;
    }

    spinX = spinCurrentSettings[ 0 ];
    spinY = spinCurrentSettings[ 1 ];
    spinXGoal = spinCurrentSettings[ 2 ];
    spinYGoal = spinCurrentSettings[ 3 ];
    spinXFull = spinX * 5;
    spinYFull = spinY * 5;
    spinAngle = ( spinAngleLastStart + 140 ) % 180;
    spinHit = 0;
    spinReverse = 0;
    spinTaille = SPIN_TAILLE_BATON;
    spinTimer = 0;
    spinGoal = 0;
    spinHighscore = spinHighscores[ spinCurrentNumLevel ];
    spinMaxRessorts = spinCurrentSettings[ 4 ];
    spinStart = 0;
    spinLastDirection = -1;
    spinPause = 0;
    spinPauseOff = 0;

    for( int i = 0; i < 32; i++ )
      spinAnimRessorts[ i ] = 0;
}

// ---- collisions ----

int spinTestCollisionsJoueur( int offset, int firstCollision, int activerRessorts )
{
    int testX1, testX2, testY1, testY2;
    float angleRad = ( ( spinAngle + offset + 180 ) % 180 ) * 0.0174;
    int i;

    for( i = spinTaille; i >= 1; i -= 2 )
    {
        testX1 = SPIN_JOUEUR_ECRAN_X + (int)( i * cos( angleRad ) );
        testX2 = SPIN_JOUEUR_ECRAN_X - (int)( i * cos( angleRad ) );
        testY1 = SPIN_JOUEUR_ECRAN_Y + (int)( i * sin( angleRad ) );
        testY2 = SPIN_JOUEUR_ECRAN_Y - (int)( i * sin( angleRad ) );

        if( gbGetPixel( testX1, testY1 ) || gbGetPixel( testX2, testY2 ) )
        {
            if( firstCollision )
            {
                if( gbGetPixel( testX1, testY1 ) )
                {
                    spinCircleX = testX1;
                    spinCircleY = testY1;
                }
                else
                {
                    spinCircleX = testX2;
                    spinCircleY = testY2;
                }
            }

            if( activerRessorts )
            {
                int collX, collY, j;

                if( gbGetPixel( testX1, testY1 ) )
                {
                    collX = testX1 + spinX - SPIN_JOUEUR_ECRAN_X;
                    collY = testY1 + spinY - SPIN_JOUEUR_ECRAN_Y;
                }
                else
                {
                    collX = testX2 + spinX - SPIN_JOUEUR_ECRAN_X;
                    collY = testY2 + spinY - SPIN_JOUEUR_ECRAN_Y;
                }

                for( j = 0; j < spinMaxRessorts; j++ )
                {
                    if( spinCurrentSettings[ 5 + j * 3 ] - collX > -8 && spinCurrentSettings[ 5 + j * 3 ] - collX <= 0 &&
                        spinCurrentSettings[ 6 + j * 3 ] - collY > -8 && spinCurrentSettings[ 6 + j * 3 ] - collY <= 0 )
                    {
                        spinAnimRessorts[ j ] = 1;
                    }
                }
            }

            return 1;
        }
    }

    return 0;
}

void spinTestCollisionsProcheJoueur()
{
    int ok = 0;
    int offsetCollisions = -1;

    while( !ok && offsetCollisions > -90 )
    {
        if( offsetCollisions > 0 )
          offsetCollisions *= -1;
        else
          offsetCollisions *= -3;

        if( !spinTestCollisionsJoueur( offsetCollisions * ( 1 - 2 * spinReverse ), 0, 0 ) )
          ok = 1;
    }

    spinAngle = ( spinAngle + offsetCollisions * ( 1 - 2 * spinReverse ) + 180 ) % 180;
    if( offsetCollisions < 0 )
      spinReverse = 1 - spinReverse;
}

void spinCollisionsJoueur( int springs )
{
    if( springs )
    {
        if( spinTestCollisionsJoueur( 0, 0, 1 ) )
          spinTestCollisionsProcheJoueur();
    }
    else
    {
        if( spinTestCollisionsJoueur( 0, 1, 0 ) )
        {
            spinHit = 1;
            gbPlayPattern( spinDestroyPattern, 0 );
        }

        if( spinHit > 0 )
          spinTestCollisionsProcheJoueur();
    }
}

int spinVerifCollisionRessorts()
{
    int ret = 0;
    int i;

    for( i = 0; i < spinMaxRessorts; i++ )
    {
        if( spinCurrentSettings[ 5 + i * 3 ] - ( spinXFull / 5 ) <= 2 && spinCurrentSettings[ 5 + i * 3 ] - ( spinXFull / 5 ) > -10 &&
            spinCurrentSettings[ 6 + i * 3 ] - ( spinYFull / 5 ) <= 2 && spinCurrentSettings[ 6 + i * 3 ] - ( spinYFull / 5 ) > -10 )
        {
            ret = 1;
        }
    }

    return ret;
}

// ---- player update/draw ----

void spinUpdateJoueur()
{
    spinAngle = ( spinAngle + 3 * ( 1 - 2 * spinReverse ) + 180 ) % 180;

    if( spinHit > 0 )
    {
        spinAngle = ( spinAngle + 18 * ( 1 - 2 * spinReverse ) + 180 ) % 180;
        if( spinHit < 200 )
          spinHit++;
        if( spinTaille >= 0 )
          spinTaille--;

        if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
        {
            spinInitNiveau( spinCurrentNumLevel );
            gbPlayOK();
        }
        if( gbPressed( BTN_C ) )
        {
            spinMode = SPIN_MODE_MENU;
            spinMenuFrames = 0;
            spinInitMenu( spinCurrentNumLevel );
            gbPlayCancel();
        }
    }

    if( spinGoal )
    {
        spinAngle = ( spinAngle + 9 * ( 1 - 2 * spinReverse ) + 180 ) % 180;
        if( spinGoal < 200 )
          spinGoal++;

        if( spinX > spinXGoal )
          spinX -= gbMin( 2, spinX - spinXGoal );
        if( spinX < spinXGoal )
          spinX += gbMin( 2, spinXGoal - spinX );
        if( spinY > spinYGoal )
          spinY -= gbMin( 2, spinY - spinYGoal );
        if( spinY < spinYGoal )
          spinY += gbMin( 2, spinYGoal - spinY );
    }

    if( spinHit == 0 && !spinGoal )
    {
        if( spinStart )
          spinTimer = gbMin( 2000, spinTimer + 1 );

        spinXDiff = 0;
        spinYDiff = 0;

        if( ( !gbRepeat( BTN_UP, 1 ) && spinLastDirection == 3 ) || ( !gbRepeat( BTN_DOWN, 1 ) && spinLastDirection == 2 ) )
        {
            if( gbRepeat( BTN_RIGHT, 1 ) && !gbRepeat( BTN_LEFT, 1 ) ) spinLastDirection = 0;
            if( !gbRepeat( BTN_RIGHT, 1 ) && gbRepeat( BTN_LEFT, 1 ) ) spinLastDirection = 1;
        }
        if( ( !gbRepeat( BTN_RIGHT, 1 ) && spinLastDirection == 0 ) || ( !gbRepeat( BTN_LEFT, 1 ) && spinLastDirection == 1 ) )
        {
            if( gbRepeat( BTN_UP, 1 ) && !gbRepeat( BTN_DOWN, 1 ) ) spinLastDirection = 3;
            if( !gbRepeat( BTN_UP, 1 ) && gbRepeat( BTN_DOWN, 1 ) ) spinLastDirection = 2;
        }

        if( gbPressed( BTN_RIGHT ) ) spinLastDirection = 0;
        if( gbPressed( BTN_LEFT ) )  spinLastDirection = 1;
        if( gbPressed( BTN_DOWN ) )  spinLastDirection = 2;
        if( gbPressed( BTN_UP ) )    spinLastDirection = 3;

        if( gbRepeat( BTN_RIGHT, 1 ) && spinLastDirection == 0 ) spinXDiff = SPIN_VITESSE_DROITE;
        if( gbRepeat( BTN_LEFT, 1 ) && spinLastDirection == 1 )  spinXDiff = -SPIN_VITESSE_DROITE;
        if( gbRepeat( BTN_DOWN, 1 ) && spinLastDirection == 2 )  spinYDiff = SPIN_VITESSE_DROITE;
        if( gbRepeat( BTN_UP, 1 ) && spinLastDirection == 3 )    spinYDiff = -SPIN_VITESSE_DROITE;

        if( !spinStart && ( spinXDiff != 0 || spinYDiff != 0 ) )
        {
            spinStart = 1;
            spinAngleLastStart = spinAngle;
        }

        spinXFull += spinXDiff;
        while( spinVerifCollisionRessorts() && spinXDiff != 0 )
          spinXFull -= spinXDiff / 4;
        spinYFull += spinYDiff;
        while( spinVerifCollisionRessorts() && spinYDiff != 0 )
          spinYFull -= spinYDiff / 4;

        spinX = spinXFull / 5;
        spinY = spinYFull / 5;

        if( spinGoal == 0 && spinHit == 0 && gbAbsInt( spinX - spinXGoal ) < 9 && gbAbsInt( spinY - spinYGoal ) < 9 )
          spinGoal = 1;
    }
}

void spinDrawExplosion( int frame, int x, int y )
{
    switch( frame )
    {
        case 5:
        {
            gbDrawPixel( x, y );
            gbDrawPixel( x + 6, y + 6 );
            gbDrawPixel( x - 6, y - 6 );
            gbDrawPixel( x + 6, y - 6 );
            gbDrawPixel( x - 6, y + 6 );
        }
        break;

        case 0:
        {
            gbDrawPixel( x, y );
            gbDrawPixel( x, y + 1 );
            gbDrawPixel( x, y - 1 );
            gbDrawPixel( x + 1, y );
            gbDrawPixel( x - 1, y );
        }
        case 4:
        {
            gbDrawPixel( x + 3, y + 3 );
            gbDrawPixel( x - 3, y + 3 );
            gbDrawPixel( x + 3, y - 3 );
            gbDrawPixel( x - 3, y - 3 );

            gbDrawPixel( x + 5, y + 5 );
            gbDrawPixel( x - 5, y + 5 );
            gbDrawPixel( x + 5, y - 5 );
            gbDrawPixel( x - 5, y - 5 );

            gbDrawPixel( x - 4, y );
            gbDrawPixel( x + 4, y );

            gbDrawPixel( x, y + 4 );
            gbDrawPixel( x, y - 4 );
        }
        break;

        case 1:
        {
            gbDrawPixel( x, y + 1 );
            gbDrawPixel( x - 1, y + 1 );
            gbDrawPixel( x + 1, y + 1 );
            gbDrawPixel( x, y - 1 );
            gbDrawPixel( x - 1, y - 1 );
            gbDrawPixel( x + 1, y - 1 );
            gbDrawPixel( x - 1, y );
            gbDrawPixel( x + 1, y );

            gbDrawPixel( x - 2, y );
            gbDrawPixel( x - 3, y );
            gbDrawPixel( x + 2, y );
            gbDrawPixel( x + 3, y );

            gbDrawPixel( x, y + 2 );
            gbDrawPixel( x, y + 3 );
            gbDrawPixel( x, y - 2 );
            gbDrawPixel( x, y - 3 );

            gbDrawPixel( x + 2, y + 2 );
            gbDrawPixel( x - 2, y + 2 );
            gbDrawPixel( x + 2, y - 2 );
            gbDrawPixel( x - 2, y - 2 );
        }
        case 3:
        {
            gbDrawPixel( x - 5, y );
            gbDrawPixel( x + 5, y );

            gbDrawPixel( x, y + 5 );
            gbDrawPixel( x, y - 5 );

            gbDrawPixel( x + 4, y + 4 );
            gbDrawPixel( x - 4, y + 4 );
            gbDrawPixel( x + 4, y - 4 );
            gbDrawPixel( x - 4, y - 4 );
        }
        break;

        case 2:
        {
            gbDrawPixel( x, y + 1 );
            gbDrawPixel( x - 1, y + 1 );
            gbDrawPixel( x + 1, y + 1 );
            gbDrawPixel( x, y - 1 );
            gbDrawPixel( x - 1, y - 1 );
            gbDrawPixel( x + 1, y - 1 );
            gbDrawPixel( x - 1, y );
            gbDrawPixel( x + 1, y );

            gbDrawPixel( x - 2, y );
            gbDrawPixel( x - 4, y );
            gbDrawPixel( x + 2, y );
            gbDrawPixel( x + 4, y );

            gbDrawPixel( x, y + 2 );
            gbDrawPixel( x, y + 4 );
            gbDrawPixel( x, y - 2 );
            gbDrawPixel( x, y - 4 );

            gbDrawPixel( x + 3, y + 3 );
            gbDrawPixel( x - 3, y + 3 );
            gbDrawPixel( x + 3, y - 3 );
            gbDrawPixel( x - 3, y - 3 );
        }
        break;
    }
}

void spinAfficheJoueur()
{
    float angleRad = spinAngle * 0.0174;
    float x0 = SPIN_JOUEUR_ECRAN_X + cos( ( spinAngle + 90 ) * 0.0174 );
    float y0 = SPIN_JOUEUR_ECRAN_Y + sin( ( spinAngle + 90 ) * 0.0174 );
    float x1 = SPIN_JOUEUR_ECRAN_X + cos( ( spinAngle - 90 ) * 0.0174 );
    float y1 = SPIN_JOUEUR_ECRAN_Y + sin( ( spinAngle - 90 ) * 0.0174 );
    int x2, y2, x3, y3, x4, y4, x5, y5;

    gbFillCircle( SPIN_JOUEUR_ECRAN_X, SPIN_JOUEUR_ECRAN_Y, SPIN_TAILLE_JOUEUR * spinTaille / SPIN_TAILLE_BATON );

    x2 = spinRoundF( x0 + spinTaille * cos( angleRad ) );
    y2 = spinRoundF( y0 + spinTaille * sin( angleRad ) );
    x3 = spinRoundF( x1 + spinTaille * cos( angleRad ) );
    y3 = spinRoundF( y1 + spinTaille * sin( angleRad ) );
    x4 = spinRoundF( x0 - spinTaille * cos( angleRad ) );
    y4 = spinRoundF( y0 - spinTaille * sin( angleRad ) );
    x5 = spinRoundF( x1 - spinTaille * cos( angleRad ) );
    y5 = spinRoundF( y1 - spinTaille * sin( angleRad ) );

    gbDrawLine( x2, y2, x3, y3 );
    gbDrawLine( x3, y3, x5, y5 );
    gbDrawLine( x4, y4, x5, y5 );
    gbDrawLine( x4, y4, x2, y2 );

    if( spinHit > 0 )
    {
        if( SPIN_TAILLE_BATON - spinTaille < 6 )
          gbDrawCircle( spinCircleX, spinCircleY, 2 * ( SPIN_TAILLE_BATON - spinTaille ) );

        if( spinTaille < 6 )
          spinDrawExplosion( 5 - spinTaille, SPIN_JOUEUR_ECRAN_X, SPIN_JOUEUR_ECRAN_Y );
    }
}

// ---- decor / world drawing ----

void spinAfficheDecor()
{
    int ca, decor;
    int decoraff;
    int drawX, drawY;
    int oX = ( spinX - SPIN_JOUEUR_ECRAN_X ) / 32;
    int oY = ( spinY - SPIN_JOUEUR_ECRAN_Y ) / 4;
    int skiplastX = ( ( oX + 3 ) * 32 > spinX + SPIN_JOUEUR_ECRAN_X );
    int o = oY * 8 + oX % 8;
    int i, j;

    for( i = 0; i < 56; i++ )
    {
        ca = o + i % 4 + ( i / 4 ) * 8;
        decor = spinLevelByte( ca );
        drawY = 4 * ( ca / 8 ) - ( spinY - SPIN_JOUEUR_ECRAN_Y );

        for( j = 0; j < 8; j++ )
        {
            decoraff = ( decor >> j ) & 1;
            drawX = 4 * ( ( ca % 8 ) * 8 + 7 - j ) - ( spinX - SPIN_JOUEUR_ECRAN_X );
            if( drawX > 88 )
              decoraff = 0;
            if( decoraff )
              gbFillRect( drawX, drawY, 4, 4 );
        }

        if( i % 4 == 2 && skiplastX )
          i++;
    }

    if( spinX - SPIN_JOUEUR_ECRAN_X < 0 )
      gbFillRect( 0, 0, SPIN_JOUEUR_ECRAN_X - spinX, LCDHEIGHT );
    if( spinY - SPIN_JOUEUR_ECRAN_Y < 0 )
      gbFillRect( 0, 0, LCDWIDTH, SPIN_JOUEUR_ECRAN_Y - spinY );
    if( 128 - spinY - SPIN_JOUEUR_ECRAN_Y < 0 )
      gbFillRect( 0, LCDHEIGHT + ( 128 - spinY - SPIN_JOUEUR_ECRAN_Y ), LCDWIDTH, spinY + SPIN_JOUEUR_ECRAN_Y - 128 );
    if( 256 - spinX - SPIN_JOUEUR_ECRAN_X < 0 )
      gbFillRect( LCDWIDTH + ( 256 - spinX - SPIN_JOUEUR_ECRAN_X ), 0, spinX + SPIN_JOUEUR_ECRAN_X - 256, LCDHEIGHT );
}

void spinAfficheArrivee()
{
    switch( gbFrameCount % 4 )
    {
        case 0:
          gbDrawBitmapRotated( SPIN_JOUEUR_ECRAN_X - 5 - ( spinX - spinXGoal ), SPIN_JOUEUR_ECRAN_Y - 5 - ( spinY - spinYGoal ), spinGoalBitmap, 0, 0 );
          break;
        case 1:
          gbDrawBitmapRotated( SPIN_JOUEUR_ECRAN_X - 5 - ( spinX - spinXGoal ), SPIN_JOUEUR_ECRAN_Y - 5 - ( spinY - spinYGoal ), spinGoal2Bitmap, 0, 0 );
          break;
        case 2:
          gbDrawBitmapRotated( SPIN_JOUEUR_ECRAN_X - 5 - ( spinX - spinXGoal ), SPIN_JOUEUR_ECRAN_Y - 10 - ( spinY - spinYGoal ), spinGoalBitmap, 1, 0 );
          break;
        default:
          gbDrawBitmapRotated( SPIN_JOUEUR_ECRAN_X - 5 - ( spinX - spinXGoal ), SPIN_JOUEUR_ECRAN_Y - 10 - ( spinY - spinYGoal ), spinGoal2Bitmap, 1, 0 );
          break;
    }
}

int* spinRessortBitmap( int frame )
{
    if( frame == 4 ) return spinRessort1Bitmap;
    if( frame == 2 ) return spinRessort2Bitmap;
    if( frame != 0 ) return spinRessort3Bitmap;
    return spinRessort0Bitmap;
}

void spinAfficheRessorts()
{
    int i;

    for( i = 0; i < spinMaxRessorts; i++ )
    {
        int sens = spinCurrentSettings[ 7 + i * 3 ];
        gbDrawBitmapRotated(
            SPIN_JOUEUR_ECRAN_X - ( spinX - spinCurrentSettings[ 5 + i * 3 ] ),
            SPIN_JOUEUR_ECRAN_Y - ( spinY - spinCurrentSettings[ 6 + i * 3 ] ),
            spinRessortBitmap( spinAnimRessorts[ i ] ), sens, 0 );
    }

    for( i = 0; i < 32; i++ )
    {
        if( spinAnimRessorts[ i ] > 0 )
          spinAnimRessorts[ i ]++;
        if( spinAnimRessorts[ i ] > 4 )
          spinAnimRessorts[ i ] = 0;
    }
}

// ---- HUD ----

void spinAfficheTimer( int frames, int posX, int posY )
{
    gbSetColor( 0 );
    gbFillRect( posX - 1, posY, 21, 6 );
    gbSetColor( 1 );
    gbCursorY = posY;
    gbCursorX = posX;

    if( frames >= 2000 )
    {
        gbPrintString( "99\"99" );
    }
    else
    {
        gbPrintNumber( frames / 200 % 10 );
        gbCursorX = posX + 16;
        gbPrintNumber( ( frames % 2 ) * 5 );
        gbCursorX = posX + 12;
        gbPrintNumber( frames * 5 / 10 % 10 );
        gbCursorX = posX + 8;
        gbPrintString( "\"" );
        gbCursorX = posX + 4;
        gbPrintNumber( frames / 20 % 10 );
    }
}

void spinAfficheFin()
{
    if( spinHighscore > 20 && spinHighscore < 2000 )
    {
        gbSetColor( 0 );
        gbFillRect( 0, 0, LCDWIDTH, 12 );
        gbSetColor( 1 );
        spinAfficheTimer( spinHighscore, 64, 6 );
        gbCursorX = 0;

        if( spinTimer < spinHighscore )
        {
            gbCursorY = 0;
            gbPrintString( "New record!" );
            gbCursorX = 0;
            gbCursorY = 6;
            gbPrintString( "Previous record:" );
        }
        else
        {
            gbCursorY = 0;
            gbPrintString( "Time:" );
            gbCursorX = 0;
            gbCursorY = 6;
            gbPrintString( "Your record:" );
        }
    }
    else
    {
        gbSetColor( 0 );
        gbFillRect( 0, 0, LCDWIDTH, 6 );
        gbSetColor( 1 );
        gbCursorX = 0;
        gbCursorY = 0;
        gbPrintString( "Time:" );
    }

    gbSetColor( 0 );
    gbFillRect( 0, LCDHEIGHT - 6, LCDWIDTH, 6 );
    gbSetColor( 1 );
    gbCursorX = 6;
    gbCursorY = LCDHEIGHT - 6;
    gbPrintString( spinFinPrompt );
}

void spinAfficheRetry()
{
    gbSetColor( 0 );
    gbFillRect( 0, LCDHEIGHT - 6, LCDWIDTH, 6 );
    gbSetColor( 1 );
    gbCursorX = 14;
    gbCursorY = LCDHEIGHT - 6;
    gbPrintString( spinRetryPrompt );
}

void spinAffichePause()
{
    gbSetColor( 0 );
    gbFillRect( 0, LCDHEIGHT - 6, LCDWIDTH, 6 );
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = LCDHEIGHT - 6;
    gbPrintString( spinPausePrompt );
}

// ---- top-level NIVEAU (in-level) tick ----

void spinUpdateNiveau()
{
    if( !spinPause )
      spinUpdateJoueur();

    spinAfficheDecor();

    if( !spinPause && spinHit == 0 && spinGoal == 0 )
      spinCollisionsJoueur( 0 );

    spinAfficheRessorts();

    if( !spinPause && spinHit == 0 && spinGoal == 0 )
      spinCollisionsJoueur( 1 );

    spinAfficheArrivee();

    if( spinTaille >= 0 )
      spinAfficheJoueur();
    if( spinGoal > 20 )
      spinAfficheFin();
    if( spinTaille < 6 )
      spinAfficheRetry();

    spinAfficheTimer( spinTimer, 64, 0 );

    // pause handling
    if( spinPause )
    {
        spinAffichePause();
        if( gbPressed( BTN_C ) )
        {
            spinPause = 0;
            spinPauseOff = 1;
            gbPlayOK();
        }
        if( gbPressed( BTN_B ) )
        {
            spinInitNiveau( spinCurrentNumLevel );
            gbPlayCancel();
        }
        if( gbPressed( BTN_A ) )
        {
            spinMode = SPIN_MODE_MENU;
            spinMenuFrames = 0;
            spinInitMenu( spinCurrentNumLevel );
            gbPlayCancel();
        }
    }
    if( spinHit == 0 && spinGoal == 0 && !spinPause && !spinPauseOff && gbPressed( BTN_C ) )
    {
        spinPause = 1;
        gbPlayCancel();
    }
    spinPauseOff = 0;

    // end-of-level handling
    if( spinGoal > 20 )
    {
        spinComplete[ spinCurrentNumLevel ] = 1;
        if( spinTimer < spinHighscores[ spinCurrentNumLevel ] || spinHighscore <= 20 || spinHighscore >= 2000 )
          spinHighscores[ spinCurrentNumLevel ] = spinTimer;

        if( gbPressed( BTN_B ) )
        {
            spinInitNiveau( spinCurrentNumLevel );
            gbPlayCancel();
        }
        if( ( gbPressed( BTN_A ) && spinCurrentNumLevel >= SPIN_NOMBRE_NIVEAUX - 1 ) || gbPressed( BTN_C ) )
        {
            spinMode = SPIN_MODE_MENU;
            spinMenuFrames = 0;
            spinInitMenu( spinCurrentNumLevel );
            gbPlayCancel();
        }
        if( gbPressed( BTN_A ) && spinCurrentNumLevel < SPIN_NOMBRE_NIVEAUX - 1 )
        {
            spinInitNiveau( spinCurrentNumLevel + 1 );
            gbPlayOK();
        }
    }
}

// ---- title ----

void spinUpdateTitle()
{
    gbSetColor( 1 );
    gbCursorX = 6;
    gbCursorY = 16;
    gbFontSize = 1;
    gbPrintString( "SPIN SPIN SPINBUINO!" );
    gbCursorX = 30;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        spinLoadEeprom();
        spinInitMenu( spinUnlockedLevel );
        spinMode = SPIN_MODE_MENU;
        spinMenuFrames = 0;
    }
}

// ---- menu ----

void spinNomNiveau()
{
    gbCursorX = 0;
    gbCursorY = 0;

    switch( spinCurrentNumLevel )
    {
        case -2: gbPrintString( "Unlock/Erase records" ); break;
        case -1: gbPrintString( "Credits" ); break;
        case 0:  gbPrintString( "1A: Start spinning!" ); break;
        case 1:  gbPrintString( "1B: Zig Zag" ); break;
        case 2:  gbPrintString( "1C: Narrow passages" ); break;
        case 3:  gbPrintString( "1D: Hurdles" ); break;
        case 4:  gbPrintString( "2A: Springs" ); break;
        case 5:  gbPrintString( "2B: Boing Boing" ); break;
        case 6:  gbPrintString( "2C: Right or left?" ); break;
        case 7:  gbPrintString( "2D: Patience" ); break;
    }
}

int* spinLevelIconBitmap( int which )
{
    switch( which )
    {
        case 1: return spinLevelIcon1Bitmap;
        case 2: return spinLevelIcon2Bitmap;
        case 3: return spinLevelIcon3Bitmap;
        case 4: return spinLevelIcon4Bitmap;
        case 5: return spinLevelIcon5Bitmap;
        case 6: return spinLevelIcon6Bitmap;
        case 7: return spinLevelIcon7Bitmap;
        default: return spinLevelIcon8Bitmap;
    }
}

void spinUpdateMenu()
{
    int drawX, drawY;
    int drawOldX = 0;
    int drawOldY = 0;
    int i;

    spinNomNiveau();
    spinMenuFrames++;
    if( spinMenuFrames >= 160 )
      spinMenuFrames = 32;
    if( spinMenuFrames >= 2 && spinMenuFrames < 32 )
      spinMenuFrames = 32;

    if( gbPressed( BTN_C ) || gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        spinMode = SPIN_MODE_TITLE;
        return;
    }

    for( i = 0; i < 6; i++ )
    {
        int niveauDessine = spinCurrentNumLevel + i - 2;
        drawX = 20 * i - 10;
        drawY = 20 + 5 * ( niveauDessine % 2 ); // overridden below when niveauDessine<0 - real upstream formula, unmodified
        if( niveauDessine < 0 )
          drawY = 27;

        if( niveauDessine >= -2 && niveauDessine <= spinUnlockedLevel && niveauDessine < SPIN_NOMBRE_NIVEAUX )
        {
            if( niveauDessine >= -1 && drawOldY != 0 )
            {
                // punches a WHITE gap at the node, then restores BLACK -
                // gbColor is a persistent global, not reset by gbClear(),
                // so leaving it at WHITE here would silently blank every
                // later draw this tick (the icon right below, and both
                // spinNomNiveau()/"Your record:" text prints) as well as
                // spinNomNiveau()'s own prints on every subsequent tick
                // until something else happens to set it back to BLACK.
                gbDrawLine( drawOldX + 7, drawOldY + 4, drawX + 4, drawY + 4 );
                gbSetColor( 0 );
                gbFillRect( drawX + 1, drawY + 1, 6, 6 );
                gbSetColor( 1 );
            }

            if( niveauDessine >= 0 )
            {
                if( spinHighscores[ niveauDessine ] > spinTarget[ niveauDessine ] || spinHighscores[ niveauDessine ] <= 20 || spinHighscores[ niveauDessine ] >= 2000 )
                {
                    if( i == 2 && spinMenuFrames % 16 < 10 ) gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 2 ) );
                    else gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 1 ) );
                }
                else
                {
                    if( i == 2 && spinMenuFrames % 16 < 10 ) gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 4 ) );
                    else gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 3 ) );
                }
            }
            if( niveauDessine == -1 )
            {
                if( i == 2 && spinMenuFrames % 16 < 10 ) gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 8 ) );
                else gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 7 ) );
            }
            if( niveauDessine == -2 )
            {
                if( i == 2 && spinMenuFrames % 16 < 10 ) gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 6 ) );
                else gbDrawBitmap( drawX, drawY, spinLevelIconBitmap( 5 ) );
            }
        }

        drawOldX = drawX;
        drawOldY = drawY;

        if( i == 2 && niveauDessine >= 0 && spinMenuFrames >= 32 )
        {
            gbCursorX = 0;
            gbCursorY = 6;
            if( spinComplete[ niveauDessine ] == 0 || spinHighscores[ niveauDessine ] <= 20 )
            {
                gbPrintString( "Go for it !" );
            }
            else
            {
                if( spinMenuFrames % 64 >= 32 )
                {
                    gbPrintString( "Your record:" );
                    spinAfficheTimer( spinHighscores[ niveauDessine ], 64, 6 );
                }
                else
                {
                    gbPrintString( "Target time:" );
                    spinAfficheTimer( spinTarget[ niveauDessine ], 64, 6 );
                }
            }
        }
    }

    if( spinGameComplete == 1 )
    {
        gbCursorX = 4;
        gbCursorY = LCDHEIGHT - 6;
        gbPrintString( "All levels complete!" );
    }

    if( gbPressed( BTN_LEFT ) && spinCurrentNumLevel > -2 )
    {
        spinCurrentNumLevel--;
        spinMenuFrames = 32;
        gbPlayTick();
    }
    if( gbPressed( BTN_RIGHT ) && spinCurrentNumLevel < spinUnlockedLevel && spinCurrentNumLevel < SPIN_NOMBRE_NIVEAUX - 1 )
    {
        spinCurrentNumLevel++;
        spinMenuFrames = 32;
        gbPlayTick();
    }
    spinCurrentNumLevel = gbMin( spinCurrentNumLevel, SPIN_NOMBRE_NIVEAUX - 1 );

    if( gbPressed( BTN_A ) && spinCurrentNumLevel >= 0 && spinMenuFrames > 5 )
    {
        spinMode = SPIN_MODE_NIVEAU;
        spinAngleLastStart = 10;
        spinInitNiveau( spinCurrentNumLevel );
        gbPlayOK();
    }
    if( gbPressed( BTN_A ) && spinCurrentNumLevel < 0 && spinMenuFrames > 5 )
    {
        spinMode = SPIN_MODE_CREDITS;
        gbPlayOK();
    }
}

// ---- credits / unlock-all screen ----

// Direct port of real Display::println() itself (print, then advance to a
// new line and reset cursorX to 0) - used below wherever upstream calls
// real println() so each real explicit cursorX/cursorY override that
// follows lands exactly where upstream put it, rather than guessing at
// println()'s own real line-height advance by hand.
void spinPrintLine( char* text )
{
    gbPrintString( text );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

void spinUpdateCredits()
{
    if( spinCurrentNumLevel == -2 )
    {
        gbCursorX = 0;
        gbCursorY = 0;
        spinPrintLine( "Do you want to" );
        spinPrintLine( "unlock ALL levels?" );
        gbCursorY = 16;
        spinPrintLine( "This will also erase" );
        spinPrintLine( "your records." );
        gbCursorY = 31;
        gbCursorX = 14;
        gbPrintString( spinUnlockYes );
        gbCursorY = 38;
        gbCursorX = 14;
        gbPrintString( spinUnlockNo );

        if( gbPressed( BTN_A ) )
        {
            int i;
            for( i = 0; i < SPIN_NOMBRE_NIVEAUX; i++ )
            {
                spinHighscores[ i ] = 0;
                spinComplete[ i ] = 0;
            }
            spinUnlockedLevel = SPIN_NOMBRE_NIVEAUX;
        }
    }
    else
    {
        gbCursorX = 0;
        gbCursorY = 0;
        spinPrintLine( "Spin Spin Spinbuino !" );
        gbCursorY = 8;
        spinPrintLine( "Created by:" );
        gbCursorX = 8;
        spinPrintLine( "Charly Piva \"Zoglu\"" );
        gbCursorX = 8;
        spinPrintLine( "Margot Piva  \"Isil\"" );
        gbCursorX = 32;
        spinPrintLine( "www.zoglu.net" );
        gbCursorY = 34;
        gbCursorX = 0;
        spinPrintLine( "Thanks to:" );
        gbCursorX = 16;
        spinPrintLine( "Rodot and Myndale" );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        spinMode = SPIN_MODE_MENU;
        spinMenuFrames = 0;
        spinInitMenu( spinCurrentNumLevel );
        gbPlayCancel();
    }
}

// ---- entry points ----

void gameSpinSpinSpinbuino_init()
{
    int i;

    gbBegin();

    spinMode = SPIN_MODE_TITLE;
    spinMenuFrames = 0;
    spinCurrentNumLevel = 0;
    spinUnlockedLevel = 0;
    spinAngleLastStart = 0;
    spinAngle = 0;
    spinReverse = 0;
    spinGameComplete = 0;

    for( i = 0; i < 8; i++ )
      spinTarget[ i ] = 0;
    spinTarget[ 0 ] = 140;
    spinTarget[ 1 ] = 140;
    spinTarget[ 2 ] = 160;
    spinTarget[ 3 ] = 280;
    spinTarget[ 4 ] = 240;
    spinTarget[ 5 ] = 220;
    spinTarget[ 6 ] = 160;
    spinTarget[ 7 ] = 300;

    spinLoadEeprom();
    spinInitMenu( spinUnlockedLevel );
    // spinInitMenu() above already snapshots spinUnlockedLevel into
    // spinCurrentNumLevel and re-derives spinUnlockedLevel/spinGameComplete
    // from the freshly-loaded spinComplete[] - matching real upstream's
    // own initGame()->initMenu(unlocked_level) call chain exactly.
}

void gameSpinSpinSpinbuino_update()
{
    if( !gbUpdate() ) return;

    if( spinMode == SPIN_MODE_NIVEAU )
      spinUpdateNiveau();
    else if( spinMode == SPIN_MODE_MENU )
      spinUpdateMenu();
    else if( spinMode == SPIN_MODE_TITLE )
      spinUpdateTitle();
    else
      spinUpdateCredits();

    gbRenderFrame();
}
