#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// GemGem (Tnxec2, no license specified - github.com/Tnxec2/gemgem-gamebuino,
// the repo carries no LICENSE file of any kind, only a Readme.md; confirmed
// by listing the real repo contents directly, not assumed). A Bejeweled-
// style match-3: a 9x5 board of 7 gem types, a D-pad cursor, Button A marks
// a gem and then swaps it with an orthogonally adjacent marked neighbour.
// A swap that produces no run of three or more is undone automatically;
// runs are erased, the gems above fall into the gap, new gems drop in at
// the top row, and the whole erase/fall cycle repeats until the board is
// stable. 10 points per triplet plus 10 per extra gem in the run. The game
// ends when no single swap anywhere on the board could produce a match.
//
// Source: 4 real upstream `.ino` tabs sharing one real Arduino translation
// unit (`GEMGEM.ino`/`Process.ino`/`State.ino`/`PrintUtils.ino` - confirmed
// no other source files exist in the staged directory; the repo's own
// nested `gemgem-gamebuino/` subfolder is a byte-identical duplicate of its
// own parent, verified with a recursive diff, an upload artifact rather
// than a second variant, so it was ignored). All four read in full before
// porting.
//
// DIALECT REWRITES (this project's own standing conventions): every real
// `gb.x.y(...)` call site became a plain `gbY(...)` call (this dialect has
// no classes/methods - see gamePong.c's own header comment for the full
// reasoning); upstream's own `switch` statements (`nextstep()`'s own step
// dispatch, `drawField()`'s own gem-image selection) became if/else-if
// chains; every array declaration uses this dialect's own required
// `TYPE[N] name` order; upstream's own `coords` struct (a plain x/y pair,
// used for `cursor`/`marked`/`switched`/`hint`) was flattened into pairs of
// named `int` globals (`gemCursorX`/`gemCursorY`/...), and upstream's own
// `Gamestate` struct (the persisted save record) into named globals
// (`gemHighscore`/`gemScore`/`gemSavedBoard`), matching the same "flatten a
// struct into named globals" treatment gameMasterKebab.c's own port already
// established for an identical real `struct`-copied-to-EEPROM pattern. The
// ternary in `saveGame()` (`gameOver ? 0 : board[row][col]`) became an
// if/else (no ternary in this dialect). Global naming prefix: `gem`
// (checked clear of every other game shipped in this cartridge -
// game2048.c's own bare `g` prefix is a distinct identifier space, nothing
// there is named `gem*`).
//
// BITMAPS: the real `logo` (64x30) and `gem1`-`gem7` (8x7 each) PROGMEM
// tables were converted from Arduino `B01010101` binary literals to plain
// `0x` hex by a one-off script (this dialect has no binary-literal syntax),
// with every table's own value count verified against its own real
// `{width,height,...}` header before being pasted in - all 8 exact. No art
// was redrawn, approximated, or dropped.
//
// TIMING: upstream's own gem-cascade pacing is the one real `millis()`
// consumer (`process()`: `if (currentTime - prevTime >= delayTime)` with
// `delayTime = 200`). Vircon32 has no wall-clock primitive to read (the
// same gap gameCatcher.c's/gameSnakeAbc.c's own header comments already
// document), so it became a plain tick countdown: upstream never calls
// `gb.setFrameRate()` at all, so it runs at real Gamebuino Classic's own
// 20fps default (50ms per tick, and this shim's own gbUpdate() throttle
// honors that exactly), making 200ms exactly 4 ticks - no rounding.
// `startProcessing()`'s own real `prevTime = 0` (which makes the very
// first `process()` call step immediately, since any real `millis()`
// reading already exceeds the 200ms threshold against a zero baseline) is
// reproduced by arming the countdown at 0, so the first tick after a swap
// steps at once and every following step is a full 4 ticks later.
//
// EEPROM SAVE/PAUSE (the real feature this game is unusual for having, and
// the reason it was worth porting carefully): upstream persists a whole
// `Gamestate` record - a 20-byte id string, a 2-byte highscore, a 2-byte
// score and the full 45-cell board - by memcpy'ing the struct byte-by-byte
// over `EEPROM.write(x, ((uint8_t*)&gameState)[x])`, and validates it on
// boot by reading the first 20 bytes back and `strcmp_P`-ing them against
// its own literal `"GEMGEM GAMEBUINO V1"` id string. That id string IS this
// game's own fresh-cell sentinel, and it is a genuinely sound one: a real
// factory-erased AVR EEPROM (and this project's own eepromShim.c, which
// deliberately matches it) reads 0xFF in every cell, which mismatches the
// id's own first character immediately, so `isValidGame()` correctly
// reports "no save here" and `newSave()` stamps a fresh zeroed record.
// Ported exactly, just with the struct's own implicit byte layout written
// out as explicit named addresses (`GEM_EEP_ID`/`_HIGHSCORE`/`_SCORE`/
// `_BOARD`) instead of relying on a `sizeof`-driven raw struct copy - this
// dialect's `sizeof` counts 32-bit words, not bytes, and one conceptual AVR
// EEPROM byte address is one eepromShim cell, so a literal
// `((uint8_t*)&struct)[x]` copy would not mean the same thing here (see
// eepromShim.h's own header comment on that one-cell-per-byte model). The
// total footprint is the same 69 cells upstream uses, well inside this
// shim's own real 1024-cell ATmega328 address space. Three real save sites,
// all ported: `newSave()` (fresh stamp when validation fails),
// `saveGame()` (Button C during play - saves board+score+highscore, or a
// zeroed board if the game is already over), and `saveNewHighScore()` (on
// game over with a new best - persists the highscore and deliberately
// zeroes the saved board, upstream's own way of retiring a finished game).
// `eeprom_update_byte()` is used rather than `eeprom_write_byte()`: it is
// semantically identical (it only skips a write whose value is already
// there) but avoids re-running eepromShim.c's own full-slot checksum for
// every one of the 69 cells on every save, most of which - the whole 20-cell
// id string in particular - never change after the first stamp. Purely a
// Vircon32 write-cost consideration; the resulting card contents are
// byte-for-byte what upstream's own `EEPROM.write()` loop produces.
//
// REAL AVR NARROW-INT ROUND-TRIP: the persisted highscore/score are real
// signed 16-bit AVR `int`s stored as two bytes each. This dialect's `int`
// is always 32-bit and never narrows, so `gemNarrowS16()` re-applies the
// real narrowing when loading (the same explicit-narrowing helper idiom
// gameUnderTheTower.c's own `uttNarrowS8()` already established) - keeping
// the load path's own arithmetic identical to real hardware's rather than
// silently widening a value real hardware would have wrapped. In practice
// no reachable score gets anywhere near 32767 (10 points per match), and
// the fresh-cell 65535 case can never reach this path at all, since the id
// check gates it - the helper is correctness insurance, not a live fix.
//
// BLOCKING CALLS -> EXPLICIT STATES (matching gamePong.c's own worked
// "blocking loop -> explicit resumable state" pattern used throughout this
// project): upstream has two blocking constructs, `gb.titleScreen(...)` and
// `gameMenu()`'s own `while(1) { if (gb.update()) {...} }` new-game/load
// menu. Both became explicit states (`GEM_STATE_TITLE`/`GEM_STATE_MENU`/
// `GEM_STATE_PLAY`). `titleMenu(firstRun)`'s own two real behaviors are
// carried by `gemTitleReturn`, reproducing all three real call sites
// exactly:
//   * `setup()`'s own `titleMenu(true)`   -> title, then the menu
//     (GEM_TITLE_TO_MENU).
//   * `loop()`'s own Button-C handler, `saveGame(); titleMenu(false);`
//     -> title, then `initGame()`, then straight into play
//     (GEM_TITLE_TO_PLAY). Note this really does start a brand new game
//     after saving, not resume the saved one - upstream's own behavior,
//     preserved.
//   * `gameMenu()`'s own Button-B/C handler, `titleMenu(false);`
//     -> title, then `initGame()`, then back into the menu it was called
//     from, because upstream's own `while(1)` menu loop simply resumes
//     after that call returns (GEM_TITLE_TO_MENU_AFTER_INIT). A genuinely
//     odd real behavior - "exit" from the menu initializes a game you then
//     cannot see, and leaves you in the same menu - preserved exactly
//     rather than tidied into a quit, since it is what real hardware does.
// The real `Gamebuino::titleScreen(text, logo)` layout lives inside the
// closed library, not in this game's own sources, so this state's own
// screen is the same approximation every other port here uses (the real
// logo bitmap, the real caption text, and an added "PRESS A" hint),
// dismissed by a genuine fresh `gbPressed(BTN_A)`.
//
// A SINGLE BLANK TICK ON STATE CHANGE: switching state mid-tick (Button C
// during play, or dismissing the title) returns without drawing, so that
// one 50ms tick renders blank before the destination state paints. Exactly
// the same shape gameTaquin.c's own Button-C pause already ships with -
// this project's established precedent, not a defect specific to this file.
//
// POPUPS: upstream has TWO independent popup mechanisms and this port keeps
// both, because they genuinely look different on real hardware.
// `PrintUtils.ino` defines a local `popup()`/`updatePopup()` pair drawn at
// the TOP of the screen (used for "New game"/"Game restored" from the
// menu), while `saveGame()` calls the real library's own `gb.popup()`,
// which slides in from the BOTTOM (this shim's own `gbPopup()`, drawn
// automatically by gbRenderFrame()). The local one is ported as
// `gemPopup()`/`gemUpdatePopup()`, called from exactly the same place in
// the draw order upstream calls it (after the cursor, before the pause /
// game-over dialogs, so those still cover it).
//
// PRESERVED REAL UPSTREAM QUIRKS (kept as shipped, not fixed - this
// project's own default):
//
// 1) The local popup's own `uint8_t yOffset = popupTimeLeft - 12;` for its
//    last 12 ticks underflows a real AVR `uint8_t` to 244..255, so the box
//    is drawn far below the 48px screen and is simply invisible for that
//    whole tail - it does NOT slide anywhere, despite the arithmetic
//    clearly having been written expecting a signed slide-up. This
//    dialect's `int` never narrows, so the wrap is re-applied explicitly
//    with `& 255` (see gemUpdatePopup()) to reproduce real hardware's own
//    invisible tail rather than an upward slide that never happens on a
//    real Gamebuino.
// 2) `restoreGame()` ("Load saved") calls `initVars()` but never resets the
//    score, so restoring a save keeps whatever score the record carries -
//    including after `saveNewHighScore()` deliberately zeroed the saved
//    board, in which case "Load saved" deals a fresh random board while
//    keeping the previous run's final score. Preserved.
// 3) `canMakeMove()` sets `showHint = false` the moment it finds any
//    playable move (upstream even flags it `// TODO - set to false for
//    production`), so the hint coordinates it just computed are hidden
//    again immediately; only Button B (pause) turns the readout back on.
//    Preserved verbatim.
// 4) `eraseGems()` scores per matched origin cell using a `count` that
//    starts at 1 and is incremented only for cells not already marked, so
//    an intersecting horizontal+vertical run can score its shared gems
//    differently depending on scan order, and a single erase pass can award
//    several overlapping bonuses. Ported line-for-line rather than
//    normalized - it is this game's own real scoring behavior.
// 5) `drawField()` relies on the display color still being BLACK from the
//    previous tick when it draws the top score bar (it never sets a color
//    before that first `fillRect`). True on real hardware and true here (a
//    tick always ends with BLACK selected), so no color call was inserted
//    that upstream does not have.
//
// DROPPED: `gb.battery.show = false` (real-hardware-only battery indicator,
// same treatment as gamePong.c). `gb.pickRandomSeed()` -> `gbPickRandomSeed()`,
// a documented no-op. Every `random(1, NUMGEMIMAGES+1)` became
// `1 + arand(GEM_NUMGEMIMAGES)` (this project's own established RNG
// conversion). Sound is upstream's own two real one-shot calls
// (`playOK()`/`playCancel()`), which this shim implements directly - no
// approximation was needed for this game at all.
//
// SHIM GAPS: none found. Every primitive this port needs (gbDrawBitmap,
// gbFillRect/gbDrawRect/gbDrawFastHLine/gbDrawFastVLine/gbDrawPixel,
// gbPrintString/gbPrintNumber, gbPressed, gbPlayOK/gbPlayCancel, gbPopup,
// eeprom_read_byte/eeprom_update_byte, arand, itoa/strlen) already exists.
// The only text this game prints that a plain string literal cannot hold is
// the menu's own `F("\20")` selection cursor (octal 020 = ASCII 16, one of
// real Gamebuino's own custom low-ASCII icon glyphs), built as an explicit
// int array exactly like gameTaquin.c's/gameSimonbuino.c's own header
// comments already established for the same real gap.

#define GEM_BOARDWIDTH   9
#define GEM_BOARDHEIGHT  5
#define GEM_GEMWIDTH     8
#define GEM_GEMHEIGHT    7
#define GEM_CURSORWIDTH  9
#define GEM_CURSORHEIGHT 8
#define GEM_NUMGEMIMAGES 7

#define GEM_STEP_ERASE  0
#define GEM_STEP_ERASED 1
#define GEM_STEP_MOVE   2
#define GEM_STEP_DONE   3

// upstream's own `delayTime = 200` ms at real Gamebuino Classic's own 20fps
// default (50ms/tick) - see this file's own header comment on TIMING
#define GEM_STEP_TICKS 4

enum GemState
{
    GEM_STATE_TITLE = 0,
    GEM_STATE_MENU  = 1,
    GEM_STATE_PLAY  = 2
};

// What the title screen does once Button A dismisses it - see this file's
// own header comment on upstream's own three real titleMenu() call sites.
#define GEM_TITLE_TO_MENU            0
#define GEM_TITLE_TO_PLAY            1
#define GEM_TITLE_TO_MENU_AFTER_INIT 2

// EEPROM layout - upstream's own `Gamestate` struct field order, written
// out as explicit one-cell-per-conceptual-AVR-byte addresses (see this
// file's own header comment on the EEPROM save/pause mechanism).
#define GEM_EEP_ID        0
#define GEM_EEP_ID_LEN    20
#define GEM_EEP_HIGHSCORE 20
#define GEM_EEP_SCORE     22
#define GEM_EEP_BOARD     24

// upstream's own `const char GAME_ID[] PROGMEM = "GEMGEM GAMEBUINO V1";`
// (19 characters + terminator = the same 20 cells GEM_EEP_ID_LEN reserves)
char gemGameIdText[20]= "GEMGEM GAMEBUINO V1";

// upstream's own `gb.display.print(F("\20"))` menu selection cursor - octal
// 020 is ASCII 16, a real Gamebuino low-ASCII icon glyph rather than a
// printable character, so it is built as an explicit int array
char gemMenuCursorText[2]= { 16, 0 };

int gemLogoBitmap[242]=
{
    64, 30,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x88, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x08, 0x40, 0x01, 0x00, 0x00, 0xC0, 0x00,
    0x02, 0x08, 0x20, 0x3E, 0x00, 0xEC, 0x60, 0x00,
    0x04, 0x14, 0x10, 0x62, 0x1C, 0x77, 0x60, 0x00,
    0x0C, 0x22, 0x30, 0xC2, 0x36, 0x66, 0x6F, 0x00,
    0x0A, 0x49, 0x51, 0x80, 0x66, 0x66, 0x60, 0x00,
    0x09, 0x80, 0x91, 0x81, 0x7C, 0x66, 0x60, 0x00,
    0x09, 0x00, 0x91, 0x9F, 0x30, 0x66, 0x70, 0x00,
    0x09, 0x00, 0x90, 0xC2, 0x31, 0x66, 0x00, 0x00,
    0x09, 0x00, 0x90, 0x66, 0x1E, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x90, 0x3E, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x02, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x02, 0x90, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x09, 0x06, 0x90, 0x01, 0xF0, 0x07, 0x63, 0x00,
    0x09, 0x06, 0x90, 0x03, 0x10, 0xE3, 0xBB, 0x00,
    0x0A, 0x8C, 0x90, 0x06, 0x11, 0xB3, 0x33, 0x00,
    0x0C, 0x49, 0x50, 0x0C, 0x03, 0x33, 0x33, 0x00,
    0x04, 0x22, 0x30, 0x0C, 0x0B, 0xE3, 0x33, 0x00,
    0x02, 0x14, 0x20, 0x0C, 0xF9, 0x83, 0x33, 0x80,
    0x01, 0x08, 0x40, 0x06, 0x11, 0x8B, 0x30, 0x00,
    0x00, 0x88, 0x80, 0x03, 0x30, 0xF0, 0x00, 0x00,
    0x00, 0x49, 0x00, 0x01, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

int gemGem1Bitmap[9]= { 8, 7, 0x81, 0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x81 };
int gemGem2Bitmap[9]= { 8, 7, 0x81, 0x18, 0x18, 0x18, 0x18, 0x18, 0x81 };
int gemGem3Bitmap[9]= { 8, 7, 0x81, 0x18, 0x3C, 0x7E, 0x3C, 0x18, 0x81 };
int gemGem4Bitmap[9]= { 8, 7, 0x81, 0x6E, 0x7A, 0x76, 0x3C, 0x18, 0x81 };
int gemGem5Bitmap[9]= { 8, 7, 0x81, 0x24, 0x18, 0x18, 0x18, 0x24, 0x81 };
int gemGem6Bitmap[9]= { 8, 7, 0x81, 0x3C, 0x7E, 0x7A, 0x66, 0x3C, 0x81 };
int gemGem7Bitmap[9]= { 8, 7, 0x81, 0x00, 0x7E, 0x7A, 0x7E, 0x00, 0x81 };

// Direct port of upstream's own oneOffPatterns[8][3][2] (Process.ino) - the
// eight ways three gems can sit one swap away from a triplet, borrowed by
// upstream from inventwithpython.com's own pygame Gemgem chapter.
int gemOneOffPatterns[8][3][2]=
{
    { { 0, 1 }, { 1, 0 }, { 2, 0 } },
    { { 0, 1 }, { 1, 1 }, { 2, 0 } },
    { { 0, 0 }, { 1, 1 }, { 2, 0 } },
    { { 0, 1 }, { 1, 0 }, { 2, 1 } },
    { { 0, 0 }, { 1, 0 }, { 2, 1 } },
    { { 0, 0 }, { 1, 1 }, { 2, 1 } },
    { { 0, 0 }, { 0, 2 }, { 0, 3 } },
    { { 0, 0 }, { 0, 1 }, { 0, 3 } }
};

int gemState;
int gemTitleReturn;
bool gemMenuNewGame;

bool gemGameOver;
bool gemShowGameOverDialog;
bool gemPaused;

int gemStepTimer; // upstream's own prevTime/delayTime pair, as a tick countdown

int gemHintX, gemHintY;
bool gemShowHint;

int gemBoard[5][9];
bool gemCanErased[5][9];

int gemCursorX, gemCursorY;
int gemMarkedX, gemMarkedY;     // coords of the marked gem
int gemSwitchedX, gemSwitchedY; // coords of the marked gem, for undo
bool gemProcessing;             // true while a real erase/fall cascade is running
bool gemGameStarted;
int gemStep;
int gemErasedCount;
int gemScoreAdd;

// upstream's own `Gamestate` record, flattened into named globals
int gemHighscore;
int gemScore;
int gemSavedBoard[5][9];

// upstream's own local popup (PrintUtils.ino) - drawn at the TOP of the
// screen, distinct from the real library's own bottom-sliding gb.popup()
char* gemPopupText;
int gemPopupTimeLeft;

// -----------------------------------------------------------------------------
// PrintUtils.ino
// -----------------------------------------------------------------------------

int gemStrLen( char* text )
{
    int len = 0;
    while( text[ len ] != 0 )
      len = len + 1;
    return len;
}

// Direct port of upstream's own printCentered() - sets only the horizontal
// cursor (the caller always sets cursorY itself) and prints.
void gemPrintCentered( char* text )
{
    gbCursorX = ( LCDWIDTH / 2 ) - ( gemStrLen( text ) * gbFontSize * gbFontWidth / 2 );
    gbPrintString( text );
}

void gemPopup( char* text, int duration )
{
    gemPopupText = text;
    gemPopupTimeLeft = duration + 12;
}

void gemUpdatePopup()
{
    if( gemPopupTimeLeft != 0 )
    {
        int yOffset = 0;

        // Real AVR `uint8_t yOffset = popupTimeLeft - 12;` underflows to
        // 244..255 here, putting the box far off the bottom of a 48px
        // screen - so the popup is simply invisible for its last 12 ticks
        // instead of sliding anywhere (see this file's own header comment,
        // preserved quirk 1). This dialect's int never narrows, so the wrap
        // is re-applied explicitly.
        if( gemPopupTimeLeft < 12 )
          yOffset = ( gemPopupTimeLeft - 12 ) & 255;

        int width = gemStrLen( gemPopupText ) * gbFontSize * gbFontWidth;
        gbFontSize = 1;
        gbSetColor( GB_BLACK );
        gbDrawRect( LCDWIDTH / 2 - width / 2 - 2, yOffset - 1, width + 2, gbFontHeight + 2 );
        gbSetColor( GB_WHITE );
        gbFillRect( LCDWIDTH / 2 - width / 2 - 1, yOffset - 1, width + 1, gbFontHeight + 1 );
        gbSetColor( GB_BLACK );
        gbCursorY = yOffset;
        gemPrintCentered( gemPopupText );
        gemPopupTimeLeft = gemPopupTimeLeft - 1;
    }
}

// -----------------------------------------------------------------------------
// State.ino - the real EEPROM save/restore mechanism
// -----------------------------------------------------------------------------

// Re-applies real AVR's own signed 16-bit `int` narrowing to a value
// recomposed from two persisted EEPROM bytes - see this file's own header
// comment on the narrow-int round trip.
int gemNarrowS16( int value )
{
    value = value & 0xFFFF;
    if( value >= 32768 )
      value = value - 65536;
    return value;
}

// Direct port of upstream's own writeEeprom() - the same 69 conceptual AVR
// bytes its own raw `((uint8_t*)&gameState)[x]` struct copy writes, in the
// same field order (see this file's own header comment on why
// eeprom_update_byte() is used here).
void gemWriteEeprom()
{
    int i, row, col;

    for( i = 0; i < GEM_EEP_ID_LEN; i++ )
      eeprom_update_byte( GEM_EEP_ID + i, gemGameIdText[ i ] & 0xFF );

    eeprom_update_byte( GEM_EEP_HIGHSCORE,     gemHighscore & 0xFF );
    eeprom_update_byte( GEM_EEP_HIGHSCORE + 1, ( gemHighscore >> 8 ) & 0xFF );
    eeprom_update_byte( GEM_EEP_SCORE,         gemScore & 0xFF );
    eeprom_update_byte( GEM_EEP_SCORE + 1,     ( gemScore >> 8 ) & 0xFF );

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
        eeprom_update_byte( GEM_EEP_BOARD + row * GEM_BOARDWIDTH + col,
                            gemSavedBoard[ row ][ col ] & 0xFF );
}

// Direct port of upstream's own loadGame().
void gemLoadGame()
{
    int row, col;

    gemHighscore = gemNarrowS16( eeprom_read_byte( GEM_EEP_HIGHSCORE )
                               | ( eeprom_read_byte( GEM_EEP_HIGHSCORE + 1 ) << 8 ) );
    gemScore     = gemNarrowS16( eeprom_read_byte( GEM_EEP_SCORE )
                               | ( eeprom_read_byte( GEM_EEP_SCORE + 1 ) << 8 ) );

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
        gemSavedBoard[ row ][ col ] = eeprom_read_byte( GEM_EEP_BOARD + row * GEM_BOARDWIDTH + col ) & 0xFF;
}

// Direct port of upstream's own isValidGame() - the real fresh-cell gate
// (see this file's own header comment on the EEPROM mechanism). Upstream
// reads the id bytes into the struct and strcmp_P's them; this compares the
// stored cells against the id string directly, which is the same test
// without needing a scratch buffer, and cannot read past the id field the
// way a string compare against unterminated 0xFF cells theoretically could.
bool gemIsValidGame()
{
    int i;
    for( i = 0; i < GEM_EEP_ID_LEN; i++ )
      if( eeprom_read_byte( GEM_EEP_ID + i ) != gemGameIdText[ i ] )
        return false;

    return true;
}

// Direct port of upstream's own newSave().
void gemNewSave()
{
    int row, col;

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
        gemSavedBoard[ row ][ col ] = 0;

    gemScore = 0;
    gemHighscore = 0;
    gemWriteEeprom();
}

// Direct port of upstream's own saveNewHighScore() - deliberately zeroes
// the saved board (retiring the finished game) while keeping the score and
// the freshly-raised highscore.
void gemSaveNewHighScore()
{
    int row, col;

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
        gemSavedBoard[ row ][ col ] = 0;

    gemWriteEeprom();
}

// Direct port of upstream's own saveGame() - Button C during play.
void gemSaveGame()
{
    int row, col;

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
      {
          if( gemGameOver ) gemSavedBoard[ row ][ col ] = 0;
          else gemSavedBoard[ row ][ col ] = gemBoard[ row ][ col ];
      }

    gemWriteEeprom();
    gbPlayOK();
    gbPopup( "Game saved.", 40 );
}

// Direct port of upstream's own initVars().
void gemInitVars()
{
    gemStepTimer = 0;
    gemPaused = false;
    gemGameOver = false;
    gemShowHint = false;
    gemGameStarted = false;

    gemCursorX = GEM_BOARDWIDTH / 2;
    gemCursorY = GEM_BOARDHEIGHT / 2;
    gemMarkedX = -1;
    gemMarkedY = -1;
    gemSwitchedX = -1;
    gemSwitchedY = -1;
}

// -----------------------------------------------------------------------------
// Process.ino - the erase/fall cascade
// -----------------------------------------------------------------------------

// Direct port of upstream's own startProcessing() - prevTime = 0 means
// "step on the very next tick" (see this file's own header comment on
// TIMING).
void gemStartProcessing()
{
    gemProcessing = true;
    gemStep = GEM_STEP_ERASE;
    gemStepTimer = 0;
}

void gemFillGems()
{
    int row, col;

    gbPickRandomSeed();
    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
      {
          gemBoard[ row ][ col ] = 1 + arand( GEM_NUMGEMIMAGES );
          gemCanErased[ row ][ col ] = false;
      }
}

void gemRestoreGems()
{
    int row, col;

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
      {
          gemBoard[ row ][ col ] = gemSavedBoard[ row ][ col ];
          gemCanErased[ row ][ col ] = false;
      }
}

// Direct port of upstream's own restoreGame() - note it never resets the
// score (see this file's own header comment, preserved quirk 2).
void gemRestoreGame()
{
    gemInitVars();
    if( gemSavedBoard[ 0 ][ 0 ] == 0 ) gemFillGems();
    else gemRestoreGems();
    gemStartProcessing();
    gbPlayOK();
}

// Direct port of upstream's own initGame().
void gemInitGame()
{
    gemInitVars();
    gemScore = 0;
    gemFillGems();
    gemStartProcessing();
    gbPlayOK();
}

// Direct port of upstream's own canMakeMove() - returns true (and records
// the hint coordinates) as soon as any single swap anywhere on the board
// could produce a triplet. The second half of each test is the same pattern
// with its own two coordinate components swapped, i.e. the transposed
// (vertical vs. horizontal) case.
bool gemCanMakeMove()
{
    int x, y, p;

    gemHintX = -1;
    gemHintY = -1;

    for( x = 0; x < GEM_BOARDWIDTH; x++ )
      for( y = 0; y < GEM_BOARDHEIGHT; y++ )
        for( p = 0; p < 8; p++ )
        {
            if(
                (    y + gemOneOffPatterns[ p ][ 0 ][ 1 ] < GEM_BOARDHEIGHT
                  && x + gemOneOffPatterns[ p ][ 0 ][ 0 ] < GEM_BOARDWIDTH
                  && y + gemOneOffPatterns[ p ][ 1 ][ 1 ] < GEM_BOARDHEIGHT
                  && x + gemOneOffPatterns[ p ][ 1 ][ 0 ] < GEM_BOARDWIDTH
                  && y + gemOneOffPatterns[ p ][ 2 ][ 1 ] < GEM_BOARDHEIGHT
                  && x + gemOneOffPatterns[ p ][ 2 ][ 0 ] < GEM_BOARDWIDTH
                  && gemBoard[ y + gemOneOffPatterns[ p ][ 0 ][ 1 ] ][ x + gemOneOffPatterns[ p ][ 0 ][ 0 ] ]
                  == gemBoard[ y + gemOneOffPatterns[ p ][ 1 ][ 1 ] ][ x + gemOneOffPatterns[ p ][ 1 ][ 0 ] ]
                  && gemBoard[ y + gemOneOffPatterns[ p ][ 0 ][ 1 ] ][ x + gemOneOffPatterns[ p ][ 0 ][ 0 ] ]
                  == gemBoard[ y + gemOneOffPatterns[ p ][ 2 ][ 1 ] ][ x + gemOneOffPatterns[ p ][ 2 ][ 0 ] ]
                )
             || (    y + gemOneOffPatterns[ p ][ 0 ][ 0 ] < GEM_BOARDHEIGHT
                  && x + gemOneOffPatterns[ p ][ 0 ][ 1 ] < GEM_BOARDWIDTH
                  && y + gemOneOffPatterns[ p ][ 1 ][ 0 ] < GEM_BOARDHEIGHT
                  && x + gemOneOffPatterns[ p ][ 1 ][ 1 ] < GEM_BOARDWIDTH
                  && y + gemOneOffPatterns[ p ][ 2 ][ 0 ] < GEM_BOARDHEIGHT
                  && x + gemOneOffPatterns[ p ][ 2 ][ 1 ] < GEM_BOARDWIDTH
                  && gemBoard[ y + gemOneOffPatterns[ p ][ 0 ][ 0 ] ][ x + gemOneOffPatterns[ p ][ 0 ][ 1 ] ]
                  == gemBoard[ y + gemOneOffPatterns[ p ][ 1 ][ 0 ] ][ x + gemOneOffPatterns[ p ][ 1 ][ 1 ] ]
                  && gemBoard[ y + gemOneOffPatterns[ p ][ 0 ][ 0 ] ][ x + gemOneOffPatterns[ p ][ 0 ][ 1 ] ]
                  == gemBoard[ y + gemOneOffPatterns[ p ][ 2 ][ 0 ] ][ x + gemOneOffPatterns[ p ][ 2 ][ 1 ] ]
                )
              )
            {
                gemHintX = x;
                gemHintY = y;
                gemShowHint = false; // upstream's own real "TODO" line - see preserved quirk 3
                return true;
            }
        }

    return false;
}

// Direct port of upstream's own undoSwitch().
void gemUndoSwitch()
{
    if( gemSwitchedX >= 0 )
    {
        int temp = gemBoard[ gemSwitchedY ][ gemSwitchedX ];
        gemBoard[ gemSwitchedY ][ gemSwitchedX ] = gemBoard[ gemCursorY ][ gemCursorX ];
        gemBoard[ gemCursorY ][ gemCursorX ] = temp;
    }
}

// Direct port of upstream's own moveDown() - one step of gravity per call
// (plus a fresh random gem in every empty top-row cell), returning true
// while anything is still falling.
bool gemMoveDown()
{
    bool result = false;
    int row, col;

    for( col = 0; col < GEM_BOARDWIDTH; col++ )
      if( gemBoard[ 0 ][ col ] == 0 )
        gemBoard[ 0 ][ col ] = 1 + arand( GEM_NUMGEMIMAGES );

    for( row = GEM_BOARDHEIGHT - 1; row > 0; row-- )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
        if( gemBoard[ row ][ col ] == 0 && gemBoard[ row - 1 ][ col ] > 0 )
        {
            gemBoard[ row ][ col ] = gemBoard[ row - 1 ][ col ];
            gemBoard[ row - 1 ][ col ] = 0;
            result = true;
        }

    return result;
}

// Direct port of upstream's own eraseGems() - marks every gem in every run
// of 3 or more, scores them, then clears them all (see this file's own
// header comment, preserved quirk 4, on the scoring's own real quirks).
void gemEraseGems()
{
    int row, col;

    gemErasedCount = 0;
    gemScoreAdd = 0;
    gemShowHint = false;

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
      {
          if(
              ( col + 2 < GEM_BOARDWIDTH
                && gemBoard[ row ][ col ] == gemBoard[ row ][ col + 1 ]
                && gemBoard[ row ][ col ] == gemBoard[ row ][ col + 2 ] )
           || ( row + 2 < GEM_BOARDHEIGHT
                && gemBoard[ row ][ col ] == gemBoard[ row + 1 ][ col ]
                && gemBoard[ row ][ col ] == gemBoard[ row + 2 ][ col ] )
            )
          {
              gemCanErased[ row ][ col ] = true;
              int count = 1;
              int offset = col + 1;

              if( col + 2 < GEM_BOARDWIDTH
                  && gemBoard[ row ][ col ] == gemBoard[ row ][ col + 1 ]
                  && gemBoard[ row ][ col ] == gemBoard[ row ][ col + 2 ] )
              {
                  while( offset < GEM_BOARDWIDTH && gemBoard[ row ][ offset ] == gemBoard[ row ][ col ] )
                  {
                      if( !gemCanErased[ row ][ offset ] )
                      {
                          gemCanErased[ row ][ offset ] = true;
                          count = count + 1;
                      }
                      offset = offset + 1;
                  }
              }

              if( row + 2 < GEM_BOARDHEIGHT
                  && gemBoard[ row ][ col ] == gemBoard[ row + 1 ][ col ]
                  && gemBoard[ row ][ col ] == gemBoard[ row + 2 ][ col ] )
              {
                  offset = row + 1;
                  while( offset < GEM_BOARDHEIGHT && gemBoard[ offset ][ col ] == gemBoard[ row ][ col ] )
                  {
                      if( !gemCanErased[ offset ][ col ] )
                      {
                          gemCanErased[ offset ][ col ] = true;
                          count = count + 1;
                      }
                      offset = offset + 1;
                  }
              }

              // every 3rd match gets 10 points + 10 points per extra gem
              gemScoreAdd = gemScoreAdd + ( 10 + ( count - 3 ) * 10 );
          }
      }

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
      for( col = 0; col < GEM_BOARDWIDTH; col++ )
        if( gemCanErased[ row ][ col ] )
        {
            gemBoard[ row ][ col ] = 0;
            gemErasedCount = gemErasedCount + 1;
            gemCanErased[ row ][ col ] = false;
        }
}

// Direct port of upstream's own isNeighbor().
bool gemIsNeighbor()
{
    if( gemMarkedX < 0 ) return false;

    if( gemCursorX == gemMarkedX )
    {
        if( gemCursorY - 1 == gemMarkedY ) return true;
        if( gemCursorY + 1 == gemMarkedY ) return true;
    }
    if( gemCursorY == gemMarkedY )
    {
        if( gemCursorX - 1 == gemMarkedX ) return true;
        if( gemCursorX + 1 == gemMarkedX ) return true;
    }
    return false;
}

// Direct port of upstream's own switchGems() - swaps with the already-marked
// neighbour if there is one, otherwise just marks the gem under the cursor.
void gemSwitchGems()
{
    if( gemIsNeighbor() )
    {
        int temp = gemBoard[ gemCursorY ][ gemCursorX ];
        gemBoard[ gemCursorY ][ gemCursorX ] = gemBoard[ gemMarkedY ][ gemMarkedX ];
        gemBoard[ gemMarkedY ][ gemMarkedX ] = temp;

        gemSwitchedX = gemMarkedX;
        gemSwitchedY = gemMarkedY;
        gemStartProcessing();

        gemMarkedX = -1;
        gemMarkedY = -1;
        return;
    }

    gemMarkedX = gemCursorX;
    gemMarkedY = gemCursorY;
}

// Direct port of upstream's own nextstep() (its own `switch` became an
// if/else-if chain - the final `else` is upstream's own `default:`, i.e.
// STEP_DONE).
void gemNextStep()
{
    if( gemStep == GEM_STEP_ERASE )
    {
        gemEraseGems();
        gemStep = GEM_STEP_ERASED;
    }
    else if( gemStep == GEM_STEP_ERASED )
    {
        if( gemErasedCount > 0 )
        {
            gbPlayOK();
            gemStep = GEM_STEP_MOVE;
            if( gemGameStarted ) gemScore = gemScore + gemScoreAdd;
        }
        else
        {
            gbPlayCancel();
            gemUndoSwitch();
            gemStep = GEM_STEP_DONE;
        }
        gemSwitchedX = -1;
        gemSwitchedY = -1;
    }
    else if( gemStep == GEM_STEP_MOVE )
    {
        if( !gemMoveDown() ) gemStep = GEM_STEP_ERASE;
    }
    else
    {
        gemGameOver = !gemCanMakeMove();
        gemShowGameOverDialog = gemGameOver;
        if( gemGameOver && gemScore > gemHighscore )
        {
            gemHighscore = gemScore;
            gemSaveNewHighScore();
        }
        gemProcessing = false; // end of gems move
        if( !gemGameStarted ) gemGameStarted = true;
    }
}

// Direct port of upstream's own process() - its own millis()/delayTime gate
// as a tick countdown (see this file's own header comment on TIMING).
void gemProcess()
{
    gemStepTimer = gemStepTimer - 1;
    if( gemStepTimer <= 0 )
    {
        gemNextStep();
        gemStepTimer = GEM_STEP_TICKS;
    }
}

// -----------------------------------------------------------------------------
// GEMGEM.ino - input and drawing
// -----------------------------------------------------------------------------

// Direct port of upstream's own checkInput().
void gemCheckInput()
{
    if( gbPressed( BTN_A ) )
      gemSwitchGems();

    if( gbPressed( BTN_RIGHT ) )
    {
        gemCursorX = gemCursorX + 1;
        if( gemCursorX >= GEM_BOARDWIDTH ) gemCursorX = GEM_BOARDWIDTH - 1;
    }
    else if( gbPressed( BTN_LEFT ) )
    {
        gemCursorX = gemCursorX - 1;
        if( gemCursorX < 0 ) gemCursorX = 0;
    }
    else if( gbPressed( BTN_DOWN ) )
    {
        gemCursorY = gemCursorY + 1;
        if( gemCursorY >= GEM_BOARDHEIGHT ) gemCursorY = GEM_BOARDHEIGHT - 1;
    }
    else if( gbPressed( BTN_UP ) )
    {
        gemCursorY = gemCursorY - 1;
        if( gemCursorY < 0 ) gemCursorY = 0;
    }
}

// Direct port of upstream's own drawField() - the score bar, the board
// grid, and every gem (the marked one drawn in white on a filled black
// cell). The leading fillRect deliberately inherits the ambient BLACK
// color, exactly like upstream (see preserved quirk 5).
void gemDrawField()
{
    int row, col, x, y;

    gbFillRect( 1, 0, LCDWIDTH - 2, 7 );

    gbDrawFastHLine( 1, 7, LCDWIDTH - 2 );
    gbDrawFastHLine( 1, 15, LCDWIDTH - 2 );
    gbDrawFastHLine( 1, 23, LCDWIDTH - 2 );
    gbDrawFastHLine( 1, 31, LCDWIDTH - 2 );
    gbDrawFastHLine( 1, 39, LCDWIDTH - 2 );
    gbDrawFastHLine( 1, 47, LCDWIDTH - 2 );

    gbDrawFastVLine( 1, 7, 40 );
    gbDrawFastVLine( 10, 7, 40 );
    gbDrawFastVLine( 19, 7, 40 );
    gbDrawFastVLine( 28, 7, 40 );
    gbDrawFastVLine( 37, 7, 40 );
    gbDrawFastVLine( 46, 7, 40 );
    gbDrawFastVLine( 55, 7, 40 );
    gbDrawFastVLine( 64, 7, 40 );
    gbDrawFastVLine( 73, 7, 40 );
    gbDrawFastVLine( 82, 7, 40 );

    for( row = 0; row < GEM_BOARDHEIGHT; row++ )
    {
        y = 8 + row * ( GEM_GEMHEIGHT + 1 );
        for( col = 0; col < GEM_BOARDWIDTH; col++ )
        {
            x = 2 + col * ( GEM_GEMWIDTH + 1 );
            if( gemMarkedX == col && gemMarkedY == row )
            {
                gbSetColor( GB_BLACK );
                gbFillRect( x, y, GEM_GEMWIDTH, GEM_GEMHEIGHT );
                gbSetColor( GB_WHITE );
            }
            else
              gbSetColor( GB_BLACK );

            if( gemBoard[ row ][ col ] == 1 )      gbDrawBitmap( x, y, gemGem1Bitmap );
            else if( gemBoard[ row ][ col ] == 2 ) gbDrawBitmap( x, y, gemGem2Bitmap );
            else if( gemBoard[ row ][ col ] == 3 ) gbDrawBitmap( x, y, gemGem3Bitmap );
            else if( gemBoard[ row ][ col ] == 4 ) gbDrawBitmap( x, y, gemGem4Bitmap );
            else if( gemBoard[ row ][ col ] == 5 ) gbDrawBitmap( x, y, gemGem5Bitmap );
            else if( gemBoard[ row ][ col ] == 6 ) gbDrawBitmap( x, y, gemGem6Bitmap );
            else if( gemBoard[ row ][ col ] == 7 ) gbDrawBitmap( x, y, gemGem7Bitmap );
        }
    }
}

// Direct port of upstream's own drawScore() - the live score at the left of
// the top bar, and either the hint coordinates (while paused) or the
// highscore at the right, both in white on the black bar.
void gemDrawScore()
{
    char scoreText[16];

    gbSetColor( GB_WHITE );
    gbFontSize = 1;

    gbCursorY = 1;
    gbCursorX = 2;
    gbPrintNumber( gemScore );

    if( gemShowHint )
    {
        gbCursorX = LCDWIDTH - 3 * gbFontSize * gbFontWidth - 2;
        gbPrintNumber( gemHintX );
        gbPrintString( ":" );
        gbPrintNumber( gemHintY );
    }
    else
    {
        itoa( gemHighscore, scoreText, 10 );
        gbCursorX = LCDWIDTH - gemStrLen( scoreText ) * gbFontSize * gbFontWidth - 2;
        gbPrintString( scoreText );
    }

    gbSetColor( GB_BLACK );
}

// Direct port of upstream's own drawCursor() - a white box around the
// current cell with black notch pixels punched into each of its four sides.
void gemDrawCursor()
{
    int x = 1 + gemCursorX * GEM_CURSORWIDTH;
    int y = 7 + gemCursorY * GEM_CURSORHEIGHT;

    gbSetColor( GB_WHITE );
    gbDrawFastHLine( x + 1, y, GEM_CURSORWIDTH - 1 );                    // top
    gbDrawFastHLine( x + 1, y + GEM_CURSORHEIGHT, GEM_CURSORWIDTH - 1 ); // bottom
    gbDrawFastVLine( x, y + 1, GEM_CURSORHEIGHT - 1 );                   // left
    gbDrawFastVLine( x + GEM_CURSORWIDTH, y + 1, GEM_CURSORHEIGHT - 1 ); // right

    gbSetColor( GB_BLACK );
    gbDrawPixel( x + 3, y );
    gbDrawPixel( x + 5, y );
    gbDrawPixel( x + 3, y + GEM_CURSORHEIGHT );
    gbDrawPixel( x + 5, y + GEM_CURSORHEIGHT );
    gbDrawPixel( x, y + 3 );
    gbDrawPixel( x, y + 5 );
    gbDrawPixel( x + GEM_CURSORWIDTH, y + 3 );
    gbDrawPixel( x + GEM_CURSORWIDTH, y + 5 );
}

// Direct port of upstream's own drawGameOverDialog().
void gemDrawGameOverDialog()
{
    int top = LCDHEIGHT / 2 - ( ( gbFontHeight + 1 ) * 4 / 2 ) - 1;

    gbSetColor( GB_WHITE );
    gbFillRect( 0, top, LCDWIDTH, ( gbFontHeight + 1 ) * 4 + 2 );
    gbSetColor( GB_BLACK );

    gbCursorY = top + 1;
    gemPrintCentered( " GAME OVER " );

    gbCursorX = 10;
    gbCursorY = gbCursorY + gbFontHeight + 1;
    gbPrintString( "Score: " );
    gbPrintNumber( gemScore );

    gbCursorX = 10;
    gbCursorY = gbCursorY + gbFontHeight + 1;
    gbPrintString( "Highscore: " );
    gbPrintNumber( gemHighscore );

    gbCursorY = gbCursorY + gbFontHeight + 1;
    gemPrintCentered( "a: board, b: new" );
}

// Direct port of upstream's own drawPauseDialog().
void gemDrawPauseDialog()
{
    gbCursorY = LCDHEIGHT / 2 - gbFontHeight / 2;
    gbSetColor( GB_WHITE );
    gbFillRect( 0, LCDHEIGHT / 2 - gbFontHeight / 2 - 1, LCDWIDTH, gbFontHeight + 2 );
    gbSetColor( GB_BLACK );
    gemPrintCentered( " PAUSE " );
}

// -----------------------------------------------------------------------------
// States (upstream's own blocking titleScreen()/gameMenu()/loop())
// -----------------------------------------------------------------------------

void gemBeginTitle( int returnTarget )
{
    gemTitleReturn = returnTarget;
    gemState = GEM_STATE_TITLE;
}

// == upstream's own gameMenu() entry code (everything before its own
// while(1)): validate the saved record, load it or stamp a fresh one.
void gemBeginMenu()
{
    gemMenuNewGame = true;

    if( gemIsValidGame() ) gemLoadGame();
    else gemNewSave();

    gemState = GEM_STATE_MENU;
}

// Stand-in for the real, closed gb.titleScreen(F("Gem-Gem by TnxEc2"), logo)
// - see this file's own header comment on the title screen.
void gemUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;

    gbDrawBitmap( 10, 2, gemLogoBitmap );

    gbCursorY = 34;
    gemPrintCentered( "Gem-Gem by TnxEc2" );
    gbCursorY = 41;
    gemPrintCentered( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        if( gemTitleReturn == GEM_TITLE_TO_MENU )
          gemBeginMenu();
        else
        {
            // both remaining call sites run initGame() right after the
            // blocking title screen returns
            gemInitGame();
            if( gemTitleReturn == GEM_TITLE_TO_PLAY ) gemState = GEM_STATE_PLAY;
            else gemState = GEM_STATE_MENU; // resumes upstream's own still-running menu loop
        }
    }
}

// Direct port of the body of upstream's own gameMenu() while(1) loop.
void gemUpdateMenu()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;

    gbCursorY = 0;
    gemPrintCentered( "GemGem" );

    gbCursorX = 10;
    gbCursorY = 10;
    gbPrintString( "High Score: " );
    gbPrintNumber( gemHighscore );

    gbCursorX = 20;
    gbCursorY = 20;
    gbPrintString( "New Game" );
    gbCursorX = 20;
    gbCursorY = 30;
    gbPrintString( "Load saved" );

    if( gemMenuNewGame ) gbCursorY = 20;
    else gbCursorY = 30;
    gbCursorX = 10;
    gbPrintString( gemMenuCursorText );

    gbCursorY = 40;
    gemPrintCentered( "[c,b] exit, [a] start" );

    if( gbPressed( BTN_A ) )
    {
        if( gemMenuNewGame )
        {
            gemInitGame();
            gemPopup( "New game", 40 );
        }
        else
        {
            gemRestoreGame();
            gemPopup( "Game restored", 40 );
        }
        gemState = GEM_STATE_PLAY;
        return;
    }

    if( gbPressed( BTN_UP ) || gbPressed( BTN_DOWN ) )
      gemMenuNewGame = !gemMenuNewGame;

    if( gbPressed( BTN_C ) || gbPressed( BTN_B ) )
      gemBeginTitle( GEM_TITLE_TO_MENU_AFTER_INIT );
}

// Direct port of the body of upstream's own loop().
void gemUpdatePlay()
{
    if( !gemProcessing && gbPressed( BTN_C ) )
    {
        gemSaveGame();
        gemBeginTitle( GEM_TITLE_TO_PLAY );
        return;
    }

    if( !gemGameOver )
    {
        if( gbPressed( BTN_B ) )
        {
            gemPaused = !gemPaused;
            gemShowHint = gemPaused;

            // Real hardware's own millis() keeps running while paused, so
            // a cascade paused mid-step always resumes with its 200ms
            // window already long expired and steps again immediately.
            // This port's own tick countdown simply stops instead, so it
            // is re-armed to 0 here to reproduce that same instant resume.
            if( !gemPaused ) gemStepTimer = 0;
        }
        if( !gemPaused )
        {
            if( gemProcessing ) gemProcess();
            else gemCheckInput();
        }
    }
    else
    {
        if( gbPressed( BTN_A ) )
          gemShowGameOverDialog = !gemShowGameOverDialog;
        else if( gbPressed( BTN_B ) )
          gemInitGame();
    }

    gemDrawField();
    gemDrawScore();
    if( !gemProcessing && !gemGameOver ) gemDrawCursor();
    gemUpdatePopup();
    if( gemPaused ) gemDrawPauseDialog();
    if( gemGameOver && gemShowGameOverDialog ) gemDrawGameOverDialog();
}

// == upstream's own setup(): gb.begin(), the dropped battery indicator, and
// titleMenu(true) - title screen first, then the new-game/load menu.
void gameGemgem_init()
{
    gbBegin();

    // One cartridge session runs many games in sequence, so every global
    // this game reads before its own initGame()/restoreGame() ever runs is
    // reset explicitly here rather than inheriting whatever the previous
    // launch left behind (real hardware boots straight into one game and
    // never needed this).
    gemPopupText = "";
    gemPopupTimeLeft = 0;
    gemProcessing = false;
    gemShowGameOverDialog = false;
    gemScore = 0;
    gemHighscore = 0;
    gemInitVars();

    gemBeginTitle( GEM_TITLE_TO_MENU );
}

void gameGemgem_update()
{
    if( !gbUpdate() ) return;

    if( gemState == GEM_STATE_TITLE )     gemUpdateTitle();
    else if( gemState == GEM_STATE_MENU ) gemUpdateMenu();
    else                                  gemUpdatePlay();

    gbRenderFrame();
}
