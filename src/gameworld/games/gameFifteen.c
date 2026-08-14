#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Fifteen (Tnxec2, no license specified - github.com/Tnxec2/fifteen; the
// staged repo carries no LICENSE file of any kind, only a Readme.md, a
// .gitignore, the SD-card .HEX/.INF artifacts and the sketch itself -
// confirmed by listing the real directory directly, not assumed). The
// classic sliding-tile puzzle: numbered tiles in a square grid with one
// empty cell, pushed around with the D-pad until they read 1..N-1 in order
// with the gap last. Three real board sizes (3x3 "easy", 4x4 "normal",
// 5x5 "hard") picked from the game's own level screen, plus a real EEPROM
// save that survives a power cycle.
//
// This is a genuinely separate codebase from this cartridge's already
// shipped TAQUIN (RackhamLeNoir), which is the same puzzle concept written
// independently - different author, different board sizes, different
// shuffle/solvability handling, different save behavior. Both ship, the
// same way this cartridge ships two Snake games, two 2048 variants, two
// Asteroids clones and two Breakout clones.
//
// Source: one real Arduino sketch, `src/fifteen/fifteen.ino` (450 lines,
// no other tabs), read in full before porting.
//
// DIALECT REWRITES (this project's standing conventions): every real
// `gb.x.y(...)` call site becomes a plain `gbY(...)` call (this dialect has
// no classes/methods - see gamePong.c's own header comment); array
// declarations use this dialect's required `TYPE[N] name` order; upstream's
// own `Gamestate` struct (the persisted save record: an id string, the
// board and the board dimension) is flattened into named globals
// (`ftnStateBuffer`/`ftnDimension`), matching the same treatment
// gameGemgem.c/gameMasterKebab.c already use for an identical real
// `struct`-copied-to-EEPROM pattern. `random(0,n)` becomes `arand(n)`;
// `gb.pickRandomSeed()` becomes `gbPickRandomSeed()`, a documented no-op;
// `gb.battery.show = false` is dropped (a real-hardware-only battery
// indicator, same treatment as gamePong.c). Global naming prefix: `ftn`.
//
// FONT: upstream never calls `setFont()` or touches `fontSize`, so every
// string and tile number is drawn in real hardware's own default font3x5 at
// size 1 - which is exactly what `gbBegin()` selects, so no font call
// appears in this port either.
//
// BITMAP: the real 64x30 `logo` PROGMEM table was converted from Arduino
// `B01010101` binary literals to plain hex by a one-off script (this dialect
// has no binary-literal syntax), its value count checked against its own
// real `{64,30}` header first - 240 row bytes, exact. No art was redrawn or
// approximated.
//
// EEPROM SAVE (the real feature worth porting carefully): upstream persists
// a whole `Gamestate` record - an 18-byte id string, the 25-entry board as
// AVR 16-bit ints, and the 16-bit board dimension - by copying the struct
// byte-for-byte over `EEPROM.write(x, ((uint8_t*)&gameState)[x])`, 70 bytes
// in total, and validates it by reading the first 18 bytes back and
// `strcmp_P`-ing them against its own literal `"FIFTEEN GAMEBUINO"`. That id
// string IS this game's fresh-cell sentinel, and it is a sound one: a
// factory-erased AVR EEPROM (and this project's own eepromShim.c, which
// deliberately matches that) reads 0xFF in every cell, mismatching the id's
// first character immediately, so `isValidGame()` correctly reports "no save
// here" and the game offers only a fresh start. Ported exactly, with the
// struct's implicit byte layout written out as explicit named addresses
// (FTN_EEP_ID / _BUFFER / _DIMENSION) instead of a `sizeof`-driven raw
// struct copy: this dialect's `sizeof` counts 32-bit words rather than
// bytes, and one conceptual AVR EEPROM byte address is one eepromShim cell,
// so a literal `((uint8_t*)&struct)[x]` copy would not mean the same thing
// here (see eepromShim.h's own header comment on that one-cell-per-byte
// model). The footprint is the same 70 cells upstream uses, well inside this
// shim's real 1024-cell ATmega328 address space. `eeprom_update_byte()` is
// used rather than `eeprom_write_byte()`: semantically identical (it only
// skips a write whose value is already stored) but it avoids re-running
// eepromShim.c's full-slot checksum for cells that never change after the
// first stamp - the whole 18-cell id string in particular. The resulting
// card contents are what upstream's own `EEPROM.write()` loop produces.
// The board/dimension cells are recomposed on load through `ftnNarrowS16()`,
// re-applying real AVR's own signed-16-bit narrowing that this dialect's
// always-32-bit `int` would otherwise skip (the same explicit-narrowing
// idiom gameGemgem.c/gameUnderTheTower.c already use). In practice every
// stored value here is 0..24, and the fresh-cell case can never reach this
// path at all because the id check gates it - the helper is correctness
// insurance, not a live fix.
//
// The two real save sites are ported unchanged: `newSave()` (a fresh
// zeroed record with dimension 3, stamped when "New Game" is chosen) and
// `saveGame()` (Button C during play).
//
// BLOCKING CALLS -> EXPLICIT STATES (matching gamePong.c's worked "blocking
// loop -> explicit resumable state" pattern used throughout this project):
// upstream has three blocking constructs - `gb.titleScreen(logo)`,
// `gameMenu()`'s own `while(1) { if (gb.update()) {...} }` load/new menu,
// and `chooseMap()`'s identical loop. All three become explicit states
// (FTN_STATE_TITLE / _MENU / _CHOOSEMAP / _PLAY). Upstream's real call
// graph is `titleMenu(init)` -> title screen, then `gameMenu()` when
// `init` is true or `chooseMap()` when it is false, and both of those can
// themselves call `titleMenu(false)` again - so which state follows the
// title screen, and which follows the level picker, are carried by
// `ftnAfterTitle`/`ftnAfterChooseMap`, reproducing all four real call
// sites exactly:
//   * `setup()`'s own `titleMenu(true)`      -> title, then the menu.
//   * the menu's own "New Game" choice       -> newSave(), then the level
//     picker, then play.
//   * play's own Button C handler,
//     `saveGame(); titleMenu(false);`        -> title, level picker, play.
//   * the menu's own Button B/C "exit",
//     `titleMenu(false);`                    -> title, level picker, and
//     then BACK INTO THE MENU, because upstream's own `while(1)` menu loop
//     simply resumes after that call returns. A genuinely odd real
//     behavior - "exit" from the menu walks you through the level picker,
//     initializes a board you then cannot see, and drops you back in the
//     same menu - preserved exactly rather than tidied into a quit, since
//     it is what real hardware does. Note the menu's own selection
//     highlight is a function-local in upstream that survives that round
//     trip, so `ftnMenuNewGame` is likewise only reset in
//     gameFifteen_init(), not on re-entering the menu state.
// The real `Gamebuino::titleScreen(logo)` layout lives inside the closed
// library, not in this game's sources, so that state is the same
// approximation every other port here uses: the real logo bitmap centered,
// plus an added "PRESS A" hint, dismissed by a genuine fresh
// `gbPressed(BTN_A)`.
//
// A SINGLE BLANK TICK ON STATE CHANGE: pressing C during play switches
// state and returns without drawing, so that one 50ms tick renders blank
// before the title paints. The same shape gameTaquin.c's Button-C pause and
// gameGemgem.c's own state switches already ship with - this project's
// established precedent, not a defect specific to this file.
//
// POPUPS: upstream uses two independent popup mechanisms and this port keeps
// both, because they genuinely look different on real hardware. The sketch
// defines its own local `popup()`/`updatePopup()` pair drawn at the TOP of
// the screen (used for "New Game" and "You won!"), while `saveGame()` and
// the menu's own "Game restored" use the real library's `gb.popup()`, which
// slides in from the BOTTOM (this shim's `gbPopup()`, drawn automatically by
// gbRenderFrame()). The local one is ported as `ftnPopup()`/
// `ftnUpdatePopup()`, called from exactly the place in the draw order
// upstream calls it (last, on top of the board).
//
// PRESERVED REAL UPSTREAM QUIRKS (kept as shipped, not fixed - this
// project's default):
//
// 1) `isSolvable()` does not implement the real 15-puzzle solvability rule.
//    It counts inversions over indices 0..gridSize-2 only (the last cell is
//    never examined) and treats the empty cell as an ordinary value 0
//    rather than excluding it, and it ignores the blank's own row entirely
//    (which the real parity rule needs for even-width boards). So the
//    "swap the first two tiles to make it solvable" correction it applies
//    is computed from the wrong parity and a deal can genuinely come out
//    unsolvable. Preserved verbatim: it is this game's real, shipped
//    shuffle behavior, and Button B reshuffles at any time, so an
//    unsolvable deal is never a dead end.
// 2) The local popup's own `uint8_t yOffset = popupTimeLeft - 12;` for its
//    last 12 ticks underflows a real AVR `uint8_t` to 244..255, so the box
//    is drawn far below the 48px screen and is simply invisible for that
//    whole tail - it does NOT slide anywhere, despite the arithmetic
//    clearly having been written expecting a signed slide. This dialect's
//    `int` never narrows, so the wrap is re-applied explicitly with `& 255`
//    (see ftnUpdatePopup()) to reproduce real hardware's invisible tail
//    rather than an upward slide that never happens on a real Gamebuino.
// 3) The "You won!" popup is re-armed on every single tick while the board
//    is solved, so it never expires and never fades - it just sits at the
//    top of the screen until the player starts a new game. Preserved.
// 4) `chooseMap()`'s two range clamps sit OUTSIDE their own `if
//    (gb.buttons.pressed(...))` bodies (upstream wrote the two-line
//    if/statement pairs without braces), so both clamps run on every tick
//    rather than only after a press. Harmless for the wrap-around they were
//    written for, and load-bearing in one real case: a dimension of 0 - the
//    struct's own zero-initialized starting value - is snapped straight to
//    5 by the unconditional `< SIZE_MIN` clamp on the picker's very first
//    tick, after that first tick has already drawn a level screen with no
//    name on it. Reproduced exactly, clamps outside the presses and all.
// 5) `saveGame()` returns early when the board is already solved, so
//    pressing C on a won board deliberately keeps whatever save was there
//    before. Preserved.
// 6) The menu's own "Load saved" choice with no valid save present does
//    nothing at all and still drops into play, on a board whose dimension
//    is still 0 - real hardware shows an empty bordered rectangle you
//    cannot move anything on (every direction is out of bounds against a
//    dimension of 0) until Button C or B is pressed. Preserved, with the
//    one Vircon32-specific guard described below.
//
// THE ONE REAL BEHAVIOR CHANGE, and why it is required here: quirk 6's
// zero dimension reaches `drawField()`'s own `LCDWIDTH / dimension` and
// `LCDHEIGHT / dimension`. Real AVR silently returns garbage from an
// integer division by zero and carries on; Vircon32 hard-traps the CPU
// (VIRCON32_C_DIALECT.md section 17.3), which would crash the emulator
// outright the first time that reachable state occurs. `ftnDrawField()`
// therefore clamps the divisor to a minimum of 1. This is genuinely
// invisible: in that state the board is all zeros and the dimension is 0,
// so the grid-line loop (`i < dimension`) never runs and every tile print
// is skipped, leaving exactly the same lone border rectangle real hardware
// draws - only the crash is removed. Every other division by the dimension
// in this game (`findEmptyCell()`'s own `i / dimension` and `i % dimension`)
// is already unreachable at zero, because its loop bound is `gridSize`,
// which is always `dimension * dimension` at that point and therefore 0
// too; the same holds for `shuffle()`'s `arand(gridSize)`.
//
// SHIM GAPS: none found. Every primitive this port needs (gbDrawBitmap,
// gbDrawRect/gbFillRect/gbDrawLine, gbPrintString/gbPrintNumber, gbPressed,
// gbPlayOK, gbPopup, eeprom_read_byte/eeprom_update_byte, arand) already
// exists and is used directly. The only text this game prints that a plain
// string literal cannot hold is its icon glyphs - `F("\20")`/`F("\21")`,
// octal 020/021, i.e. ASCII 16/17, real Gamebuino low-ASCII glyphs rather
// than printable characters - built as explicit int arrays exactly like
// gameTaquin.c/gameSimonbuino.c/gameGemgem.c already do for the same gap.

#define FTN_SIZE_MIN     3
#define FTN_SIZE_DEFAULT 3
#define FTN_SIZE_MAX     5

#define FTN_CELLS ( FTN_SIZE_MAX * FTN_SIZE_MAX )

#define FTN_STATE_TITLE     0
#define FTN_STATE_MENU      1
#define FTN_STATE_CHOOSEMAP 2
#define FTN_STATE_PLAY      3

// EEPROM layout - upstream's own `Gamestate` field order, written out as
// explicit one-cell-per-conceptual-AVR-byte addresses (see this file's own
// header comment on the EEPROM save mechanism). The board and the dimension
// are real AVR 16-bit ints, two cells each, little-endian.
#define FTN_EEP_ID        0
#define FTN_EEP_ID_LEN    18
#define FTN_EEP_BUFFER    18
#define FTN_EEP_DIMENSION 68

// upstream's own `const char GAME_ID[] PROGMEM = "FIFTEEN GAMEBUINO";`
// (17 characters + terminator = the same 18 cells FTN_EEP_ID_LEN reserves)
char ftnGameIdText[18]= "FIFTEEN GAMEBUINO";

// upstream's own `F("\20")` menu selection cursor - octal 020 is ASCII 16,
// a real Gamebuino low-ASCII icon glyph rather than a printable character
char ftnMenuCursorText[2]= { 16, 0 };

// upstream's own `F("\21 New Game \20")` - ASCII 17 and 16 are real
// Gamebuino low-ASCII icon glyphs bracketing the text
char ftnNewGameText[13]=
{
    17,                             // real icon glyph
    32, 78, 101, 119, 32,           // " New "
    71, 97, 109, 101, 32,           // "Game "
    16,                             // real icon glyph
    0
};

// upstream's own `F("\21 You won! \20")`
char ftnYouWonText[13]=
{
    17,                             // real icon glyph
    32, 89, 111, 117, 32,           // " You "
    119, 111, 110, 33, 32,          // "won! "
    16,                             // real icon glyph
    0
};

// upstream's own `F("\21 Select level \20")`
char ftnSelectLevelText[17]=
{
    17,                             // real icon glyph
    32, 83, 101, 108, 101, 99, 116, 32,   // " Select "
    108, 101, 118, 101, 108, 32,          // "level "
    16,                             // real icon glyph
    0
};

// upstream's own 64x30 `logo` PROGMEM table
int ftnLogoBitmap[242]=
{
    64, 30,
    0x00, 0x00, 0x1F, 0xFF, 0xFF, 0xF0, 0x00, 0x00,
    0x72, 0x00, 0x21, 0x02, 0x04, 0x08, 0x0E, 0x40,
    0x40, 0x70, 0x49, 0x32, 0x64, 0xC4, 0x08, 0x0E,
    0x42, 0x40, 0x41, 0x02, 0x02, 0x04, 0x08, 0x48,
    0x62, 0x60, 0xFF, 0xFF, 0xFF, 0xFE, 0x0C, 0x4C,
    0x42, 0x40, 0x82, 0x02, 0x02, 0x02, 0x08, 0x48,
    0x42, 0x41, 0x32, 0x02, 0x32, 0x29, 0x08, 0x48,
    0x00, 0x01, 0x32, 0x02, 0x31, 0x29, 0x00, 0x00,
    0x00, 0x02, 0x32, 0x02, 0x31, 0x14, 0x80, 0x00,
    0x43, 0x82, 0x02, 0x02, 0x01, 0x00, 0x81, 0x0E,
    0x42, 0x83, 0xFF, 0xFF, 0xFF, 0xFF, 0x81, 0x0A,
    0x63, 0x84, 0x04, 0x02, 0x00, 0x80, 0x41, 0x8E,
    0x42, 0x05, 0xC4, 0x32, 0x08, 0x93, 0x41, 0x08,
    0x73, 0x89, 0x44, 0x42, 0x38, 0x91, 0x21, 0xCE,
    0x00, 0x09, 0x84, 0x72, 0x18, 0x8B, 0x20, 0x00,
    0x00, 0x10, 0x84, 0x71, 0x38, 0x4B, 0x10, 0x00,
    0x70, 0x10, 0x08, 0x01, 0x00, 0x40, 0x10, 0x0E,
    0x50, 0x20, 0x08, 0x01, 0x00, 0x40, 0x08, 0x0A,
    0x70, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x0E,
    0x40, 0x20, 0x08, 0x01, 0x00, 0x20, 0x08, 0x08,
    0x70, 0x40, 0x08, 0x01, 0x00, 0x20, 0x04, 0x0E,
    0x00, 0x44, 0x10, 0xE1, 0x11, 0x22, 0x74, 0x00,
    0x00, 0x8C, 0x10, 0xA1, 0x31, 0x26, 0x52, 0x00,
    0x60, 0x84, 0x10, 0xE1, 0x13, 0x12, 0x52, 0x0C,
    0x51, 0x04, 0x10, 0xA1, 0x17, 0x92, 0x51, 0x0A,
    0x51, 0x0E, 0x10, 0xE1, 0x39, 0x17, 0x71, 0x0A,
    0x52, 0x00, 0x10, 0x01, 0x00, 0x10, 0x00, 0x8A,
    0x52, 0x00, 0x20, 0x01, 0x00, 0x08, 0x00, 0x8A,
    0x02, 0x00, 0x20, 0x01, 0x00, 0x08, 0x00, 0x80,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80
};

int ftnState;
int ftnAfterTitle;     // which state the title screen hands over to
int ftnAfterChooseMap; // which state the level picker hands over to
// How many level-picker invocations are stacked underneath the current one.
// Pressing B/C inside upstream's own chooseMap() calls titleMenu(false),
// which calls chooseMap() again while the first one's while(1) loop is
// still on the stack - so confirming a size with A there returns into the
// PREVIOUS picker rather than starting the game, and only the outermost
// one hands over to ftnAfterChooseMap. This counter carries that real
// nesting, which a plain state machine would otherwise flatten away.
int ftnChooseMapDepth;
bool ftnMenuNewGame;   // the menu's own selection highlight

// upstream's own live board (`unsigned char buffer[SIZE_MAX*SIZE_MAX]`) and
// its own derived cell count
int ftnBuffer[FTN_CELLS];
int ftnGridSize;

bool ftnGameWin;

int ftnEmptyIndex;
int ftnEmptyRow;
int ftnEmptyCol;

// upstream's own `Gamestate` record, flattened into named globals (its own
// `id` field lives in ftnGameIdText above, which is a constant)
int ftnStateBuffer[FTN_CELLS];
int ftnDimension;

// upstream's own local popup (drawn at the TOP of the screen, distinct from
// the real library's own bottom-sliding gb.popup())
char* ftnPopupText;
int ftnPopupTimeLeft;

// -----------------------------------------------------------------------------
// Text helpers
// -----------------------------------------------------------------------------

int ftnStrLen( char* text )
{
    int len = 0;
    while( text[ len ] != 0 )
      len = len + 1;
    return len;
}

// Direct port of upstream's own printCentered() - sets only the horizontal
// cursor (every caller sets cursorY itself) and prints.
void ftnPrintCentered( char* text )
{
    gbCursorX = ( LCDWIDTH / 2 ) - ( ftnStrLen( text ) * gbFontSize * gbFontWidth / 2 );
    gbPrintString( text );
}

void ftnPopup( char* text, int duration )
{
    ftnPopupText = text;
    ftnPopupTimeLeft = duration + 12;
}

// Direct port of upstream's own updatePopup().
void ftnUpdatePopup()
{
    if( ftnPopupTimeLeft != 0 )
    {
        int yOffset = 0;

        // Real AVR `uint8_t yOffset = popupTimeLeft - 12;` underflows to
        // 244..255 here, putting the box far below a 48px screen - so the
        // popup is simply invisible for its last 12 ticks instead of
        // sliding anywhere (see this file's own header comment, preserved
        // quirk 2). This dialect's int never narrows, so the wrap is
        // re-applied explicitly.
        if( ftnPopupTimeLeft < 12 )
          yOffset = ( ftnPopupTimeLeft - 12 ) & 255;

        int width = ftnStrLen( ftnPopupText ) * gbFontSize * gbFontWidth;
        gbFontSize = 1;
        gbSetColor( GB_BLACK );
        gbDrawRect( LCDWIDTH / 2 - width / 2 - 2, yOffset - 1, width + 2, gbFontHeight + 2 );
        gbSetColor( GB_WHITE );
        gbFillRect( LCDWIDTH / 2 - width / 2 - 1, yOffset - 1, width + 1, gbFontHeight + 1 );
        gbSetColor( GB_BLACK );
        gbCursorY = yOffset;
        ftnPrintCentered( ftnPopupText );
        ftnPopupTimeLeft = ftnPopupTimeLeft - 1;
    }
}

// -----------------------------------------------------------------------------
// The real EEPROM save record
// -----------------------------------------------------------------------------

// Re-applies real AVR's own signed 16-bit `int` narrowing to a value
// recomposed from two persisted EEPROM bytes - see this file's own header
// comment on the EEPROM save mechanism.
int ftnNarrowS16( int value )
{
    value = value & 0xFFFF;
    if( value >= 32768 )
      value = value - 65536;
    return value;
}

// Direct port of upstream's own writeEeprom() - the same 70 conceptual AVR
// bytes its own raw `((uint8_t*)&gameState)[x]` struct copy writes, in the
// same field order (see this file's own header comment on why
// eeprom_update_byte() is used here).
void ftnWriteEeprom()
{
    int i;

    for( i = 0; i < FTN_EEP_ID_LEN; i++ )
      eeprom_update_byte( FTN_EEP_ID + i, ftnGameIdText[ i ] & 0xFF );

    for( i = 0; i < FTN_CELLS; i++ )
    {
        eeprom_update_byte( FTN_EEP_BUFFER + i * 2,     ftnStateBuffer[ i ] & 0xFF );
        eeprom_update_byte( FTN_EEP_BUFFER + i * 2 + 1, ( ftnStateBuffer[ i ] >> 8 ) & 0xFF );
    }

    eeprom_update_byte( FTN_EEP_DIMENSION,     ftnDimension & 0xFF );
    eeprom_update_byte( FTN_EEP_DIMENSION + 1, ( ftnDimension >> 8 ) & 0xFF );
}

// Direct port of upstream's own isValidGame() - the real fresh-cell gate
// (see this file's own header comment on the EEPROM mechanism). Upstream
// reads the id bytes into the struct and strcmp_P's them; this compares the
// stored cells against the id string directly, which is the same test
// without needing a scratch buffer, and cannot read past the id field the
// way a string compare against unterminated 0xFF cells theoretically could.
bool ftnIsValidGame()
{
    int i;
    for( i = 0; i < FTN_EEP_ID_LEN; i++ )
      if( eeprom_read_byte( FTN_EEP_ID + i ) != ftnGameIdText[ i ] )
        return false;

    return true;
}

// Direct port of upstream's own newSave() - a fresh, zeroed record at the
// default board size.
void ftnNewSave()
{
    int i;
    for( i = 0; i < FTN_CELLS; i++ )
      ftnStateBuffer[ i ] = 0;

    ftnDimension = FTN_SIZE_DEFAULT;
    ftnWriteEeprom();
}

// -----------------------------------------------------------------------------
// Board logic
// -----------------------------------------------------------------------------

// Direct port of upstream's own isSolved() - the tiles read 1..gridSize-1 in
// order (the empty cell is wherever the tiles left it, which for a genuinely
// ordered board can only be the last cell).
bool ftnIsSolved()
{
    int count = 1;
    int i;

    for( i = 0; i < ftnGridSize - 1; i++ )
    {
        if( ftnBuffer[ i ] != count )
          return false;

        count = count + 1;
    }

    return true;
}

// Direct port of upstream's own findEmptyCell(). Its loop bound is
// gridSize, which is always dimension*dimension at every call site, so the
// two divisions here are unreachable when the dimension is 0 (see this
// file's own header comment).
void ftnFindEmptyCell()
{
    int i;
    for( i = 0; i < ftnGridSize; i++ )
      if( ftnBuffer[ i ] == 0 )
      {
          ftnEmptyIndex = i;
          ftnEmptyRow = i / ftnDimension;
          ftnEmptyCol = i % ftnDimension;
      }
}

// Direct port of upstream's own shuffle() - one pass of random swaps.
void ftnShuffle()
{
    int n, temp, i;

    for( i = 0; i < ftnGridSize; i++ )
    {
        n = arand( ftnGridSize );
        temp = ftnBuffer[ n ];
        ftnBuffer[ n ] = ftnBuffer[ i ];
        ftnBuffer[ i ] = temp;
    }
}

// Direct port of upstream's own isSolvable() - see this file's own header
// comment, preserved quirk 1: this is NOT the real 15-puzzle parity rule,
// and it is ported exactly as written rather than corrected.
bool ftnIsSolvable()
{
    int kDisorder = 0;
    int i, j;

    for( i = 1; i < ftnGridSize - 1; i++ )
      for( j = i - 1; j >= 0; j-- )
        if( ftnBuffer[ j ] > ftnBuffer[ i ] )
          kDisorder = kDisorder + 1;

    return !( kDisorder % 2 );
}

// Direct port of upstream's own initBoard().
void ftnInitBoard()
{
    int i;

    ftnGameWin = false;
    ftnGridSize = ftnDimension * ftnDimension;
    gbPickRandomSeed();

    for( i = 0; i < FTN_CELLS; i++ )
    {
        if( i < ftnGridSize - 1 ) ftnBuffer[ i ] = i + 1;
        else ftnBuffer[ i ] = 0;
    }

    ftnShuffle();

    if( !ftnIsSolvable() ) // upstream's own "make puzzle solvable" swap
    {
        int temp = ftnBuffer[ 0 ];
        ftnBuffer[ 0 ] = ftnBuffer[ 1 ];
        ftnBuffer[ 1 ] = temp;
    }

    ftnFindEmptyCell();
    gbPlayOK();
}

// Direct port of upstream's own restoreBoard().
void ftnRestoreBoard()
{
    ftnGameWin = ftnIsSolved();
    ftnGridSize = ftnDimension * ftnDimension;
    ftnFindEmptyCell();
    gbPlayOK();
}

// Direct port of upstream's own restoreGame() - reads the whole persisted
// record back, copies the saved board into the live one, then rebuilds the
// derived state from it.
void ftnRestoreGame()
{
    int i;

    for( i = 0; i < FTN_CELLS; i++ )
    {
        ftnStateBuffer[ i ] = ftnNarrowS16( eeprom_read_byte( FTN_EEP_BUFFER + i * 2 )
                                          | ( eeprom_read_byte( FTN_EEP_BUFFER + i * 2 + 1 ) << 8 ) );
        ftnBuffer[ i ] = ftnStateBuffer[ i ];
    }

    ftnDimension = ftnNarrowS16( eeprom_read_byte( FTN_EEP_DIMENSION )
                               | ( eeprom_read_byte( FTN_EEP_DIMENSION + 1 ) << 8 ) );

    ftnRestoreBoard();
}

// Direct port of upstream's own saveGame() - Button C during play. Note the
// early return on a solved board (see this file's own header comment,
// preserved quirk 5).
void ftnSaveGame()
{
    int i;

    if( ftnGameWin ) return;

    for( i = 0; i < FTN_CELLS; i++ )
      ftnStateBuffer[ i ] = ftnBuffer[ i ];

    ftnWriteEeprom();
    gbPlayOK();
    gbPopup( "Game saved.", 40 );
}

// -----------------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------------

// Direct port of upstream's own drawField() - the outer border, the grid
// lines, then every non-empty tile's own number. The divisor clamp is this
// port's one real behavior change, and it is genuinely invisible - see this
// file's own header comment for the full reasoning.
void ftnDrawField()
{
    int divisor = ftnDimension;
    if( divisor < 1 ) divisor = 1;

    int cellWidth  = LCDWIDTH / divisor;
    int cellHeight = LCDHEIGHT / divisor;
    int i;

    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

    for( i = 1; i < ftnDimension; i++ )
    {
        gbDrawLine( 0, cellHeight * i, LCDWIDTH, cellHeight * i );
        gbDrawLine( cellWidth * i, 0, cellWidth * i, LCDHEIGHT );
    }

    int row = 0;
    int col = 0;

    for( i = 0; i < ftnGridSize; i++ )
    {
        if( ftnBuffer[ i ] > 0 )
        {
            gbCursorX = col * cellWidth + 5;
            gbCursorY = row * cellHeight + 4;
            // upstream prints with println(); the trailing line advance has
            // no effect here because every tile sets its own cursor first
            gbPrintNumber( ftnBuffer[ i ] );
        }

        col = col + 1;
        if( col >= ftnDimension )
        {
            col = 0;
            row = row + 1;
        }
    }
}

// -----------------------------------------------------------------------------
// States (upstream's own blocking titleScreen()/gameMenu()/chooseMap()/loop())
// -----------------------------------------------------------------------------

void ftnBeginTitle( int afterTitle, int afterChooseMap )
{
    ftnAfterTitle = afterTitle;
    ftnAfterChooseMap = afterChooseMap;
    ftnState = FTN_STATE_TITLE;
}

// Stand-in for the real, closed gb.titleScreen(logo) - see this file's own
// header comment on the title screen.
void ftnUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;

    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, ftnLogoBitmap );

    gbCursorY = 38;
    ftnPrintCentered( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbPickRandomSeed(); // upstream's own titleMenu() call, a no-op here
        ftnState = ftnAfterTitle;
    }
}

// Direct port of the body of upstream's own gameMenu() while(1) loop.
void ftnUpdateMenu()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;

    gbCursorY = 0;
    ftnPrintCentered( "Fifteen" );

    gbCursorX = 20;
    gbCursorY = 10;
    gbPrintString( "Load saved" );
    gbCursorX = 20;
    gbCursorY = 20;
    gbPrintString( "New Game" );

    if( ftnMenuNewGame ) gbCursorY = 20;
    else gbCursorY = 10;
    gbCursorX = 10;
    gbPrintString( ftnMenuCursorText );

    gbCursorY = 40;
    ftnPrintCentered( "[c,b] exit, [a] start" );

    if( gbPressed( BTN_A ) )
    {
        if( ftnMenuNewGame )
        {
            ftnNewSave();
            ftnAfterChooseMap = FTN_STATE_PLAY;
            ftnState = FTN_STATE_CHOOSEMAP;
        }
        else
        {
            // upstream does nothing at all when there is no valid save,
            // and still falls through into play (see this file's own
            // header comment, preserved quirk 6)
            if( ftnIsValidGame() )
            {
                ftnRestoreGame();
                gbPopup( "Game restored", 40 );
            }

            ftnState = FTN_STATE_PLAY;
        }

        return;
    }

    if( gbPressed( BTN_UP ) || gbPressed( BTN_DOWN ) )
      ftnMenuNewGame = !ftnMenuNewGame;

    if( gbPressed( BTN_C ) || gbPressed( BTN_B ) )
    {
        // upstream's own titleMenu(false) from inside the menu loop: title
        // screen, then the level picker, and then back into this same menu
        // (see this file's own header comment)
        ftnBeginTitle( FTN_STATE_CHOOSEMAP, FTN_STATE_MENU );
    }
}

// Direct port of the body of upstream's own chooseMap() while(1) loop -
// including its own two unconditional range clamps (see this file's own
// header comment, preserved quirk 4).
void ftnUpdateChooseMap()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;

    gbCursorY = LCDHEIGHT - 11;
    ftnPrintCentered( ftnSelectLevelText );

    if( ftnDimension == 3 )
    {
        gbCursorY = LCDHEIGHT / 2 - 11;
        ftnPrintCentered( "easy" );
        gbCursorY = LCDHEIGHT / 2 - 2;
        ftnPrintCentered( "3 x 3" );
    }
    else if( ftnDimension == 4 )
    {
        gbCursorY = LCDHEIGHT / 2 - 11;
        ftnPrintCentered( "normal" );
        gbCursorY = LCDHEIGHT / 2 - 2;
        ftnPrintCentered( "4 x 4" );
    }
    else if( ftnDimension == 5 )
    {
        gbCursorY = LCDHEIGHT / 2 - 11;
        ftnPrintCentered( "hard" );
        gbCursorY = LCDHEIGHT / 2 - 2;
        ftnPrintCentered( "5 x 5" );
    }

    if( gbPressed( BTN_A ) )
    {
        ftnInitBoard();

        // upstream returns into whichever picker invocation called this
        // one, if any (see ftnChooseMapDepth above)
        if( ftnChooseMapDepth > 0 ) ftnChooseMapDepth = ftnChooseMapDepth - 1;
        else ftnState = ftnAfterChooseMap;

        return;
    }

    if( gbPressed( BTN_RIGHT ) )
      ftnDimension = ftnDimension + 1;

    if( ftnDimension > FTN_SIZE_MAX ) ftnDimension = FTN_SIZE_MIN;

    if( gbPressed( BTN_LEFT ) )
      ftnDimension = ftnDimension - 1;

    if( ftnDimension < FTN_SIZE_MIN ) ftnDimension = FTN_SIZE_MAX;

    if( gbPressed( BTN_C ) || gbPressed( BTN_B ) )
    {
        // upstream's own titleMenu(false) from inside the picker loop:
        // title screen, then a NESTED picker on top of this one
        ftnChooseMapDepth = ftnChooseMapDepth + 1;
        ftnBeginTitle( FTN_STATE_CHOOSEMAP, ftnAfterChooseMap );
    }
}

// Direct port of the body of upstream's own loop().
void ftnUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        ftnSaveGame();
        // upstream's own titleMenu(false): title screen, then the level
        // picker, then straight back into play
        ftnBeginTitle( FTN_STATE_CHOOSEMAP, FTN_STATE_PLAY );
        return;
    }

    if( gbPressed( BTN_B ) )
    {
        ftnInitBoard();
        ftnPopup( ftnNewGameText, 20 );
    }

    if( ftnGameWin )
    {
        // re-armed every tick while solved, so it never fades (see this
        // file's own header comment, preserved quirk 3)
        ftnPopup( ftnYouWonText, 20 );
    }
    else
    {
        // Each direction slides the neighbouring tile that way, i.e. it
        // moves the EMPTY cell the opposite way - upstream's own mapping.
        if( gbPressed( BTN_RIGHT ) )
        {
            int newCol = ftnEmptyCol - 1;
            if( newCol >= 0 )
            {
                int temp = ftnBuffer[ ftnEmptyIndex ];
                ftnBuffer[ ftnEmptyIndex ] = ftnBuffer[ ftnEmptyRow * ftnDimension + newCol ];
                ftnEmptyIndex = ftnEmptyRow * ftnDimension + newCol;
                ftnEmptyCol = newCol;
                ftnBuffer[ ftnEmptyIndex ] = temp;
            }
        }
        else if( gbPressed( BTN_LEFT ) )
        {
            int newCol = ftnEmptyCol + 1;
            if( newCol < ftnDimension )
            {
                int temp = ftnBuffer[ ftnEmptyIndex ];
                ftnBuffer[ ftnEmptyIndex ] = ftnBuffer[ ftnEmptyRow * ftnDimension + newCol ];
                ftnEmptyIndex = ftnEmptyRow * ftnDimension + newCol;
                ftnEmptyCol = newCol;
                ftnBuffer[ ftnEmptyIndex ] = temp;
            }
        }
        else if( gbPressed( BTN_DOWN ) )
        {
            int newRow = ftnEmptyRow - 1;
            if( newRow >= 0 )
            {
                int temp = ftnBuffer[ ftnEmptyIndex ];
                ftnBuffer[ ftnEmptyIndex ] = ftnBuffer[ newRow * ftnDimension + ftnEmptyCol ];
                ftnEmptyIndex = newRow * ftnDimension + ftnEmptyCol;
                ftnEmptyRow = newRow;
                ftnBuffer[ ftnEmptyIndex ] = temp;
            }
        }
        else if( gbPressed( BTN_UP ) )
        {
            int newRow = ftnEmptyRow + 1;
            if( newRow < ftnDimension )
            {
                int temp = ftnBuffer[ ftnEmptyIndex ];
                ftnBuffer[ ftnEmptyIndex ] = ftnBuffer[ newRow * ftnDimension + ftnEmptyCol ];
                ftnEmptyIndex = newRow * ftnDimension + ftnEmptyCol;
                ftnEmptyRow = newRow;
                ftnBuffer[ ftnEmptyIndex ] = temp;
            }
        }

        if( ftnIsSolved() )
          ftnGameWin = true;
    }

    ftnDrawField();
    ftnUpdatePopup();
}

// == upstream's own setup(): gb.begin(), the dropped battery indicator, and
// titleMenu(true) - the title screen first, then the load/new menu.
void gameFifteen_init()
{
    gbBegin();

    // One cartridge session runs many games in sequence, so every global
    // this game reads before its own initBoard()/restoreGame() ever runs is
    // reset explicitly here rather than inheriting whatever the previous
    // launch left behind (real hardware boots straight into one game, where
    // these are the C globals' own zero/default initial values).
    int i;
    for( i = 0; i < FTN_CELLS; i++ )
    {
        ftnBuffer[ i ] = 0;
        ftnStateBuffer[ i ] = 0;
    }

    ftnGridSize = FTN_SIZE_DEFAULT * FTN_SIZE_DEFAULT;
    ftnDimension = 0; // upstream's own zero-initialized Gamestate.dimension
    ftnGameWin = false;
    ftnEmptyIndex = 0;
    ftnEmptyRow = 0;
    ftnEmptyCol = 0;
    ftnMenuNewGame = false;
    ftnChooseMapDepth = 0;
    ftnPopupText = "";
    ftnPopupTimeLeft = 0;

    ftnBeginTitle( FTN_STATE_MENU, FTN_STATE_PLAY );
}

void gameFifteen_update()
{
    if( !gbUpdate() ) return;

    if( ftnState == FTN_STATE_TITLE )          ftnUpdateTitle();
    else if( ftnState == FTN_STATE_MENU )      ftnUpdateMenu();
    else if( ftnState == FTN_STATE_CHOOSEMAP ) ftnUpdateChooseMap();
    else                                       ftnUpdatePlay();

    gbRenderFrame();
}
