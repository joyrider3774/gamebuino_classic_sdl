#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Minesweeper (dirksteindorf, license: none specified in the real upstream
// repo - github.com/dirksteindorf/Gamebuino-Minesweeper). A faithful port
// of the classic Minesweeper: a 12x6 grid of covered cells, 12 mines
// scattered across it; Button A uncovers the cell under the cursor
// (auto-cascading through every connected run of zero-neighbour cells,
// exactly like every other Minesweeper), Button B toggles a flag on a
// still-covered cell, and the D-pad moves the cursor (wrapping at every
// edge). A small smiley reacts to what's happening (worried while A is
// held over a covered cell, sad once a mine is hit) and a live "flags
// placed / total mines" counter is drawn across the top of the screen.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every global symbol got a
// `mine`-prefixed name (this cartridge has no linker - every ported game
// shares one flat global namespace, same as every other file here).
// Upstream's own `byte` fields/locals (Arduino/AVR's own alias for
// uint8_t) became plain `int` throughout, and its own Arduino
// `B00000000`-style binary literals (used only in the two small icon
// bitmaps and the title logo, see below) were converted to `0x`-hex byte-
// for-byte, no bit reshuffling needed (this shim's own gbDrawBitmap() uses
// the exact same row-major/MSB-first layout real Display::drawBitmap()
// does, confirmed against gamebuinoShim.h's own header comment on that
// primitive). `random(N)`/`random(min,max)` became `arand(N)`/
// `min + arand(max-min)` (this dialect's own established RNG helper -
// see avrCompat.h and gameFlappyBirdo.c's own header comment for the exact
// ranged-random rewrite convention followed here). `gb.pickRandomSeed()`
// became `gbPickRandomSeed()`, a documented no-op, matching this whole
// project's own established precedent. `gb.battery.show = false;` was
// dropped outright (purely cosmetic on real hardware, no equivalent here).
//
// Upstream's own single-instance `minefield board[WIDTH][HEIGHT]` struct
// array is preserved as a genuine struct (`MineCell`, this dialect does
// support structs and struct arrays - see gameAgaruino.c's own `AgarBall`
// precedent), but flattened to a 1D array with manual `x*MINE_HEIGHT+y`
// indexing (`mineIdx()`) rather than a real `MineCell[W][H]` 2D
// declaration - gameConduit.c's own `int[6][8] condMap` proves plain 2D
// arrays work in this dialect, but a 2D array of a *struct* type together
// was never proven out anywhere in this project, so this port takes the
// safer, already-proven-elsewhere route instead (1D struct array + manual
// indexing, the same treatment gameMaze.c's own header comment describes
// choosing for its own bit-packed grids). Upstream's own tiny, single-
// instance `Cursor {x,y}` struct was flattened further still, straight to
// two plain ints (`mineCursorX`/`mineCursorY`) - there is only ever one
// cursor, so a whole extra struct type for it buys nothing here.
//
// Upstream's own blocking `gb.titleScreen(F(""), logo)` (called once from
// setup(), and effectively again any time the player presses Button C -
// upstream's own `process_player_input()`/WON-branch/LOST-branch each
// separately call `setup()` on a Button C press, which itself immediately
// re-shows the same blocking title screen) was converted into an explicit
// MINE_STATE_TITLE/MINE_STATE_PLAY state machine, matching the "blocking
// loop -> explicit resumable state" treatment used throughout this project
// (see gamePong.c's own header comment). Since all three of upstream's own
// Button-C-triggers `setup()` call sites do exactly the same thing, this
// port hoists that single check to the very top of `mineUpdatePlay()`
// instead of duplicating it three times - a pure de-duplication, matching
// gameTaquin.c's own header comment for the identical situation there (its
// own two `gb.titleScreen()` re-entry call sites). One small, harmless,
// documented behavior difference from hoisting so early: upstream would
// still draw one more frame of the board (WON/LOST branches) or the flag
// counter (RUNNING branch, since its own Button-C check lives *inside*
// `process_player_input()`, called after the counter is printed) on the
// exact tick Button C is pressed, before `setup()`'s own blocking title
// call takes over the physical screen; here that tick instead jumps
// straight to the title screen with nothing else drawn - invisible in
// practice since a real, blocking `titleScreen()` call would have
// immediately overwritten/flushed over that one extra frame anyway. Real
// Gamebuino's own `titleScreen()` waits specifically for Button A,
// matching Vircon32's own menu-select button - reused directly here as the
// dismiss gesture, same as every other ported game's own title screen.
// The real upstream `logo[]` bitmap (64x36 PROGMEM bytes, already `B`-
// binary in the real source) is restored here as a genuine
// `gbDrawBitmap()` call (`mineLogoBitmap`, converted byte-for-byte to hex -
// see this file's own header note above on why no bit reshuffling was
// needed) - centered horizontally exactly like gameMaze.c's own logo
// placement (`(LCDWIDTH-64)/2`), since real `titleScreen()`'s own fixed
// `x=0` placement is itself only correct in the context of real hardware's
// own additional boot-logo composition alongside it, which this shim has
// no equivalent of.
//
// Upstream's own `gb.popup(F("You lost. :("), 40)` / `gb.popup(F("You won!
// :)"), 40)` calls are implemented here as a small bordered message box
// (`mineDrawMessageBox()`) drawn *persistently* for as long as WON/LOST
// lasts, rather than as a transient fading overlay: this game's own
// `game_state` genuinely freezes at WON/LOST indefinitely until the player
// explicitly restarts (Button C) - upstream itself never clears
// `game_state` back to RUNNING on its own, the 40-frame popup timer was
// only ever cosmetic, not gating anything. A real `gbPopup(int* text, int
// duration)` shim primitive exists in gamebuinoShim.h/.c, but is
// deliberately not used here: its real 40+12-tick auto-dismiss would still
// make the message disappear on its own while `game_state` stays frozen
// indefinitely underneath, leaving a blank board with no restart hint -
// exactly the mismatch this file's own persistent-box design exists to
// avoid.
//
// A genuine, reachable upstream bug, found while checking `place_bombs()`
// closely and preserved (not fixed) here, per this project's own default
// of keeping real upstream behavior unless told otherwise: `place_bombs()`
// picks each mine's column via `random(1, WIDTH-2)` and row via
// `random(1, HEIGHT-2)`. Since Arduino's own ranged `random(min,max)` is
// max-*exclusive*, that only ever reaches columns 1..(WIDTH-3) and rows
// 1..(HEIGHT-3) - one short of the real last interior index in each axis
// (WIDTH-2 and HEIGHT-2 respectively, the same bound `init_board()`/the
// draw loop/the bounds check in `uncover_harmless_neighbours()` all treat
// as fully valid). The practical, visible effect: the rightmost column
// and the bottom row of the real 12x6 play field can *never* contain a
// mine, on real hardware and here alike - a genuine, if subtle, upstream
// gameplay quirk (a marginally-safer border a sufficiently attentive
// player could actually exploit), preserved exactly via
// `1 + arand(MINE_WIDTH - 3)` / `1 + arand(MINE_HEIGHT - 3)` below rather
// than "fixed" into a fairer distribution upstream never actually shipped.
//
// Another small, genuinely harmless upstream quirk, preserved verbatim
// rather than dropped as dead code: `NotFirstPress` gates
// `process_player_input()`'s own Button-A-*released* handling on a
// Button-A-*pressed* event having already been seen since the last
// `setup()` - almost exactly the same shape of bug this project's own
// `md_armInputAGate()` (wired into `portVircon32.c`'s own dispatch loop on
// every game launch - see this project's own CLAUDE.md "the menu-launch
// button bleeding into the game" writeup) was built specifically to
// prevent for every ported game uniformly. Because that global gate
// already suppresses the exact stray-release-with-no-matching-press
// scenario `NotFirstPress` guards against, this flag can never actually
// matter here - but it's real, genuinely harmless upstream code (not a
// typo or an off-by-one), so it's kept exactly as `mineNotFirstPress`
// rather than quietly deleted.
//
// `text[10]` (upstream's own ' '/'1'..'9' nearby-bomb-count-to-character
// lookup table) became a plain `int[10] mineDigitChars` of ASCII codes
// (32, then 49-57) - this dialect has no char literals in use anywhere
// else in this project (see gameSnakeAbc.c's own header comment/code for
// the same "just use the ASCII int" convention), so this follows suit
// rather than introducing the first one. Upstream's own `gb.display.
// drawChar(x, y, ch, size)` (used directly, not through `print()`, for the
// smiley/digit/mine-marker/flag glyphs) maps directly onto this shim's own
// `gbDrawChar(ch, x, y)` (real `size` comes from the global `gbFontSize`
// instead of a 4th parameter, matching every other size-aware primitive
// here). This file calls that real primitive directly, with no local
// wrapper needed.
//
// Checked specifically for the "bitmap drawn without its own underlying
// fill/mask layer first" bug class this project has already found twice
// (see gameFlappyBirdo.c's own header comment) - it does not apply here:
// upstream's own `flag[]` bitmap is drawn as the *only* thing for a
// FLAGGED cell (no fillRect/mask underneath it in the real source, and
// none added here either), and the `neutral[]` "worried face" bitmap is
// likewise the only draw call for its own on-screen slot. No real `GRAY`
// mask layer exists anywhere in this game's own real source to restore.

// Board geometry - upstream's own real `FONT`/`fontx`/`fonty`/`FIELD_*`/
// `WIDTH`/`HEIGHT`/`offset_*`/`BOMB_COUNT` constants, precomputed to plain
// literals here (matching gameMaze.c's own `MAZE_BIT_ARRAY_SIZE` precedent
// of hand-computing a derived constant rather than trusting arithmetic
// inside a `#define` - not proven necessary here, but not needed to prove
// out either way; the underlying real formulas are given in each comment
// so the numbers can be checked/rederived by hand).
#define MINE_FONT_W        3  // upstream's own `fontx` (font3x5's own real glyph width)
#define MINE_FONT_H        5  // upstream's own `fonty` (font3x5's own real glyph height)
#define MINE_FIELD_WIDTH   5  // fontx + 2
#define MINE_FIELD_HEIGHT  7  // fonty + 2

#define MINE_COLUMNS 12
#define MINE_ROWS     6
#define MINE_BOMB_COUNT 12 // ((COLUMNS + ROWS) / 2) + (ROWS / 2) = 9 + 3

#define MINE_WIDTH   14 // COLUMNS + 2 (one-cell border margin on every side)
#define MINE_HEIGHT   8 // ROWS + 2
#define MINE_BOARD_SIZE 112 // MINE_WIDTH * MINE_HEIGHT

#define MINE_OFFSET_X 21 // upstream's own real hardcoded value
#define MINE_OFFSET_Y  3 // (LCDHEIGHT - ROWS * FIELD_HEIGHT) / 2 = (48 - 42) / 2

enum MineCellState
{
    MINE_COVERED   = 0,
    MINE_UNCOVERED = 1,
    MINE_FLAGGED   = 2
};

enum MineGameState
{
    MINE_RUNNING = 0,
    MINE_WON     = 1,
    MINE_LOST    = 2
};

enum MineAppState
{
    MINE_STATE_TITLE = 0,
    MINE_STATE_PLAY  = 1
};

typedef struct
{
    bool isBomb;
    int nearbyBombs;
    int state;
} MineCell;

MineCell mineBoard[MINE_BOARD_SIZE];

int mineAppState;
int mineGameState;

int mineCursorX;
int mineCursorY;

int mineUncoveredFields;
int mineFlagCount;
bool mineFirstField;

bool mineADown;        // upstream's own `ADown` - true while Button A is held
bool mineNotFirstPress; // upstream's own `NotFirstPress` - see this file's own header comment

// ' ','1'..'9' as plain ASCII ints - see this file's own header comment.
int mineDigitChars[10]= { 32, 49, 50, 51, 52, 53, 54, 55, 56, 57 };

// Upstream's own real `flag[]` PROGMEM bitmap (8x7), B-binary converted to
// hex byte-for-byte (see this file's own header comment).
int mineFlagBitmap[9]= { 8, 7, 0x80, 0xC0, 0xE0, 0xA0, 0x80, 0x80, 0x00 };

// Upstream's own real `neutral[]` PROGMEM bitmap (8x7, the "worried face"
// shown while Button A is held over a still-covered cell).
int mineNeutralBitmap[9]= { 8, 7, 0x70, 0xF8, 0xA8, 0xF8, 0x88, 0xF8, 0x70 };

// Upstream's own real title-screen `logo[]` PROGMEM bitmap (64x36), B-
// binary converted to hex byte-for-byte, no bit reshuffling (see this
// file's own header comment).
int mineLogoBitmap[290]= { 64, 36,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1,
    0xB0, 0x18, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0xB8, 0x38, 0x0, 0x3F, 0xB4, 0x5D, 0xDD, 0xDD,
    0xBC, 0x78, 0x1, 0xBF, 0xA4, 0x51, 0x15, 0x15, 0xBE, 0xFB, 0x1, 0xB0, 0xA4, 0x51, 0x15, 0x15,
    0xBF, 0xFB, 0x61, 0xB0, 0x24, 0x51, 0x15, 0x15, 0xB7, 0xD8, 0x71, 0xB0, 0x24, 0x51, 0x15, 0x15,
    0xB3, 0x9B, 0x71, 0xB0, 0x24, 0x51, 0x1D, 0x1D, 0xB1, 0x1B, 0x79, 0xBC, 0x34, 0x59, 0x91, 0x99,
    0xB0, 0x1B, 0x7D, 0xB8, 0x14, 0x51, 0x11, 0x15, 0xB0, 0x1A, 0x2F, 0xB0, 0x14, 0x51, 0x11, 0x15,
    0xB0, 0x10, 0xA7, 0xB0, 0x15, 0x51, 0x11, 0x15, 0xB0, 0x4, 0x83, 0xB0, 0x15, 0x51, 0x11, 0x15,
    0xB0, 0x23, 0xD3, 0xBF, 0x95, 0x51, 0x11, 0x15, 0xB0, 0x17, 0xE1, 0xBF, 0xB2, 0x9D, 0xD1, 0xD5,
    0xB0, 0xF, 0xFC, 0x0, 0x0, 0x0, 0x0, 0x5, 0xB7, 0x8F, 0xF1, 0xFF, 0xFF, 0xFF, 0xFF, 0xF5,
    0xB0, 0x3F, 0xFC, 0x0, 0x0, 0x0, 0x0, 0x5, 0xB0, 0xF, 0xF0, 0x0, 0x20, 0x0, 0x4, 0x95,
    0xB0, 0x17, 0xE8, 0x0, 0x40, 0x80, 0x2, 0xA5, 0xB0, 0x23, 0xC4, 0x0, 0x41, 0x4, 0x0, 0x5,
    0xB0, 0x5, 0x20, 0x0, 0x82, 0x8, 0x26, 0xB5, 0xB0, 0x0, 0x80, 0x1, 0xC7, 0x1C, 0x70, 0x5,
    0xB0, 0x1, 0x0, 0x1, 0xC7, 0x1C, 0x72, 0xA5, 0xB0, 0x0, 0x80, 0x1, 0xC7, 0x1C, 0x74, 0x95,
    0xB0, 0x1, 0x0, 0xE0, 0x0, 0x0, 0x0, 0x1, 0xB0, 0x0, 0x81, 0xE0, 0x32, 0x85, 0xD1, 0x15,
    0xB0, 0x1, 0x1, 0xE0, 0x39, 0x5, 0x51, 0x9, 0xB0, 0x0, 0x81, 0x20, 0x31, 0x19, 0xDD, 0xC9,
    0xB0, 0x1, 0x0, 0x20, 0x0, 0x0, 0x0, 0x1, 0xB0, 0x0, 0x80, 0x20, 0x11, 0xD5, 0x9C, 0xDD,
    0xB0, 0x1, 0x0, 0x20, 0x39, 0x55, 0xC8, 0x89, 0xB0, 0x0, 0x80, 0x70, 0x11, 0x9D, 0x9D, 0x89,
    0xB0, 0x1, 0x0, 0xF8, 0x0, 0x0, 0x0, 0x1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// Flattens upstream's own real `board[x][y]` 2D indexing into this file's
// own 1D `mineBoard[]` (see this file's own header comment on why).
int mineIdx( int x, int y )
{
    return x * MINE_HEIGHT + y;
}

// Direct port of upstream's own init_board(), extended to zero the whole
// board (including the one-cell border margin upstream's own version
// leaves to C's automatic global zero-init) rather than just the interior
// - behaviorally identical (the border was always effectively
// false/0/COVERED anyway, since nothing else ever writes to it), just not
// relying on this dialect's own global-array zero-init guarantees.
void mineInitBoard()
{
    int i, j;
    for( i = 0; i < MINE_WIDTH; i = i + 1 )
    {
        for( j = 0; j < MINE_HEIGHT; j = j + 1 )
        {
            mineBoard[ mineIdx( i, j ) ].isBomb = false;
            mineBoard[ mineIdx( i, j ) ].nearbyBombs = 0;
            mineBoard[ mineIdx( i, j ) ].state = MINE_COVERED;
        }
    }
}

// Direct port of upstream's own place_bombs() - see this file's own header
// comment on the genuine, preserved `random(1,WIDTH-2)`/`random(1,HEIGHT-2)`
// off-by-one (the rightmost column/bottom row can never get a mine).
void minePlaceBombs()
{
    int i = 0;
    int x, y;

    while( i < MINE_BOMB_COUNT )
    {
        x = 1 + arand( MINE_WIDTH - 3 );
        y = 1 + arand( MINE_HEIGHT - 3 );

        if( !mineBoard[ mineIdx( x, y ) ].isBomb && !( x == mineCursorX + 1 && y == mineCursorY + 1 ) )
        {
            mineBoard[ mineIdx( x, y ) ].isBomb = true;
            i = i + 1;
        }
    }
}

// Direct port of upstream's own get_bomb_count().
int mineGetBombCount( int x, int y )
{
    int count = 0;

    if( mineBoard[ mineIdx( x, y - 1 ) ].isBomb ) count = count + 1;
    if( mineBoard[ mineIdx( x, y + 1 ) ].isBomb ) count = count + 1;

    if( mineBoard[ mineIdx( x - 1, y - 1 ) ].isBomb ) count = count + 1;
    if( mineBoard[ mineIdx( x - 1, y ) ].isBomb )     count = count + 1;
    if( mineBoard[ mineIdx( x - 1, y + 1 ) ].isBomb ) count = count + 1;

    if( mineBoard[ mineIdx( x + 1, y - 1 ) ].isBomb ) count = count + 1;
    if( mineBoard[ mineIdx( x + 1, y ) ].isBomb )     count = count + 1;
    if( mineBoard[ mineIdx( x + 1, y + 1 ) ].isBomb ) count = count + 1;

    return count;
}

// Direct port of upstream's own compute_bomb_hints().
void mineComputeBombHints()
{
    int i, j;
    for( i = 1; i < MINE_WIDTH - 1; i = i + 1 )
    {
        for( j = 1; j < MINE_HEIGHT - 1; j = j + 1 )
        {
            if( !mineBoard[ mineIdx( i, j ) ].isBomb )
              mineBoard[ mineIdx( i, j ) ].nearbyBombs = mineGetBombCount( i, j );
        }
    }
}

// Direct, unmodified port of upstream's own recursive
// uncover_harmless_neighbours() - this dialect does support recursion (see
// gameConduit.c's own condScanBlack()/condScanWhite() precedent).
void mineUncoverHarmless( int x, int y )
{
    if( x > 0 && y > 0 && x < MINE_WIDTH - 1 && y < MINE_HEIGHT - 1 )
    {
        if( mineBoard[ mineIdx( x, y ) ].state == MINE_COVERED )
        {
            if( !mineBoard[ mineIdx( x, y ) ].isBomb )
            {
                mineBoard[ mineIdx( x, y ) ].state = MINE_UNCOVERED;
                mineUncoveredFields = mineUncoveredFields + 1;

                if( mineBoard[ mineIdx( x, y ) ].nearbyBombs == 0 )
                {
                    mineUncoverHarmless( x + 1, y + 1 );
                    mineUncoverHarmless( x + 1, y );
                    mineUncoverHarmless( x + 1, y - 1 );

                    mineUncoverHarmless( x, y + 1 );
                    mineUncoverHarmless( x, y - 1 );

                    mineUncoverHarmless( x - 1, y + 1 );
                    mineUncoverHarmless( x - 1, y );
                    mineUncoverHarmless( x - 1, y - 1 );
                }
            }
        }
    }
}

void mineBeginTitle()
{
    mineAppState = MINE_STATE_TITLE;
}

// Direct port of the "new game" half of upstream's own setup() (the half
// that runs once the real blocking titleScreen() call has returned - see
// this file's own header comment).
void mineBeginPlay()
{
    mineAppState = MINE_STATE_PLAY;
    mineGameState = MINE_RUNNING;

    mineInitBoard();

    mineCursorX = 0;
    mineCursorY = 0;

    mineUncoveredFields = 0;
    mineFlagCount = 0;
    mineFirstField = true;
    mineNotFirstPress = false;
    mineADown = false;
}

void mineUpdateTitle()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 ); // upstream's own setup()-time setFont(font5x7) before titleScreen()
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, mineLogoBitmap );

    gbCursorX = 14;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      mineBeginPlay();
}

// Direct port of upstream's own draw_board() (see this file's own header
// comment on the ADown/smiley tracking embedded in it, called unmodified
// from every one of RUNNING/WON/LOST exactly like upstream does).
void mineDrawBoard()
{
    if( gbPressed( BTN_A ) )  mineADown = true;
    if( gbReleased( BTN_A ) ) mineADown = false;

    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );

    if( mineGameState == MINE_LOST )
      gbDrawChar(1, MINE_FIELD_WIDTH, MINE_FIELD_HEIGHT ); // upstream's own real sad-face icon glyph
    else if( mineADown )
    {
        if( mineBoard[ mineIdx( mineCursorX + 1, mineCursorY + 1 ) ].state == MINE_COVERED )
          gbDrawBitmap( MINE_FIELD_WIDTH, MINE_FIELD_HEIGHT, mineNeutralBitmap );
        else
          gbDrawChar(2, MINE_FIELD_WIDTH, MINE_FIELD_HEIGHT ); // upstream's own real neutral/happy-face icon glyph
    }
    else
      gbDrawChar(2, MINE_FIELD_WIDTH, MINE_FIELD_HEIGHT );

    gbSetFont( gbFont3x5 ); // upstream's own real default FONT

    int i, j;
    for( i = 1; i < MINE_WIDTH - 1; i = i + 1 )
    {
        for( j = 1; j < MINE_HEIGHT - 1; j = j + 1 )
        {
            int cellState = mineBoard[ mineIdx( i, j ) ].state;
            int px = MINE_OFFSET_X + MINE_FIELD_WIDTH * ( i - 1 ) + 1;
            int py = MINE_OFFSET_Y + MINE_FIELD_HEIGHT * ( j - 1 ) + 1;

            if( cellState == MINE_COVERED )
            {
                gbSetColor( 1 );
                gbFillRect( px, py, MINE_FONT_W, MINE_FONT_H );
            }
            else if( cellState == MINE_FLAGGED )
              gbDrawBitmap( px, py, mineFlagBitmap );
            else if( cellState == MINE_UNCOVERED )
            {
                if( mineBoard[ mineIdx( i, j ) ].isBomb )
                  gbDrawChar(42, px, py ); // '*'
                else
                  gbDrawChar(mineDigitChars[ mineBoard[ mineIdx( i, j ) ].nearbyBombs ], px, py );
            }
        }
    }

    gbSetColor( 1 );
    gbDrawRect( MINE_OFFSET_X - 1, MINE_OFFSET_Y - 1, MINE_FIELD_WIDTH * MINE_COLUMNS + 2, MINE_FIELD_HEIGHT * MINE_ROWS + 2 );
}

// Direct port of upstream's own draw_cursor().
void mineDrawCursor()
{
    gbSetColor( 1 );
    gbDrawRect( mineCursorX * MINE_FIELD_WIDTH + MINE_OFFSET_X - 1,
                mineCursorY * MINE_FIELD_HEIGHT + MINE_OFFSET_Y - 1,
                MINE_FIELD_WIDTH + 2,
                MINE_FIELD_HEIGHT + 2 );
}

// Persistent WON/LOST message box - see this file's own header comment on
// why this replaces upstream's own timed `gb.popup()` call.
void mineDrawMessageBox( char* text )
{
    int len = 0;
    while( text[ len ] != 0 ) len = len + 1;

    int boxW = 76;
    int boxH = 14;
    int boxX = ( LCDWIDTH - boxW ) / 2;
    int boxY = ( LCDHEIGHT - boxH ) / 2;

    gbSetColor( 0 );
    gbFillRect( boxX, boxY, boxW, boxH );
    gbSetColor( 1 );
    gbDrawRect( boxX, boxY, boxW, boxH );

    gbCursorX = boxX + ( boxW - len * gbFontWidth ) / 2;
    gbCursorY = boxY + ( boxH - gbFontHeight ) / 2;
    gbPrintString( text );
}

// Direct port of upstream's own process_player_input(), minus the Button C
// handling (hoisted up into mineUpdatePlay() - see this file's own header
// comment).
void mineProcessInput()
{
    int cx = mineCursorX + 1;
    int cy = mineCursorY + 1;

    if( gbPressed( BTN_A ) )
    {
        mineNotFirstPress = true;
        if( mineBoard[ mineIdx( cx, cy ) ].state == MINE_COVERED )
          gbPlayTick();
    }

    if( gbReleased( BTN_A ) && mineNotFirstPress )
    {
        if( mineFirstField )
        {
            mineFirstField = false;
            minePlaceBombs();
            mineComputeBombHints();
        }

        if( mineBoard[ mineIdx( cx, cy ) ].state == MINE_COVERED )
        {
            if( mineBoard[ mineIdx( cx, cy ) ].isBomb )
            {
                int x, y;
                for( x = 1; x < MINE_WIDTH - 1; x = x + 1 )
                {
                    for( y = 1; y < MINE_HEIGHT - 1; y = y + 1 )
                    {
                        if( mineBoard[ mineIdx( x, y ) ].isBomb )
                          mineBoard[ mineIdx( x, y ) ].state = MINE_UNCOVERED;
                    }
                }

                mineGameState = MINE_LOST;
                gbPlayCancel();
            }
            else
            {
                gbPlayOK();

                if( mineBoard[ mineIdx( cx, cy ) ].nearbyBombs == 0 )
                  mineUncoverHarmless( cx, cy );
                else
                {
                    mineBoard[ mineIdx( cx, cy ) ].state = MINE_UNCOVERED;
                    mineUncoveredFields = mineUncoveredFields + 1;
                }

                if( mineUncoveredFields == MINE_COLUMNS * MINE_ROWS - MINE_BOMB_COUNT )
                {
                    mineGameState = MINE_WON;
                    gbPlayOK();
                }
            }
        }
    }

    if( gbPressed( BTN_B ) )
    {
        if( mineFirstField )
        {
            mineFirstField = false;
            minePlaceBombs();
            mineComputeBombHints();
        }

        if( mineBoard[ mineIdx( cx, cy ) ].state == MINE_COVERED )
        {
            mineBoard[ mineIdx( cx, cy ) ].state = MINE_FLAGGED;
            mineFlagCount = mineFlagCount + 1;
            gbPlayOK();
        }
        else if( mineBoard[ mineIdx( cx, cy ) ].state == MINE_FLAGGED )
        {
            mineBoard[ mineIdx( cx, cy ) ].state = MINE_COVERED;
            mineFlagCount = mineFlagCount - 1;
            gbPlayOK();
        }
    }

    if( gbPressed( BTN_UP ) )
    {
        if( mineCursorY > 0 ) mineCursorY = mineCursorY - 1;
        else mineCursorY = MINE_ROWS - 1;
        gbPlayTick();
    }
    if( gbPressed( BTN_DOWN ) )
    {
        if( mineCursorY < MINE_ROWS - 1 ) mineCursorY = mineCursorY + 1;
        else mineCursorY = 0;
        gbPlayTick();
    }
    if( gbPressed( BTN_LEFT ) )
    {
        if( mineCursorX > 0 ) mineCursorX = mineCursorX - 1;
        else mineCursorX = MINE_COLUMNS - 1;
        gbPlayTick();
    }
    if( gbPressed( BTN_RIGHT ) )
    {
        if( mineCursorX < MINE_COLUMNS - 1 ) mineCursorX = mineCursorX + 1;
        else mineCursorX = 0;
        gbPlayTick();
    }
}

// Direct port of upstream's own loop() body (minus the three duplicated
// Button-C-triggered `setup()` calls, hoisted to one check here - see this
// file's own header comment).
void mineUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        mineBeginTitle();
        return;
    }

    // Upstream's own real flag-counter HUD line, printed unconditionally
    // at the very top of loop() every tick via `print("\n\n\n\n\n")` -
    // relies on this shim's own gbUpdate() already resetting
    // gbCursorX/gbCursorY to (0,0) every tick, exactly like real
    // hardware's own Display::update() does (see gamebuinoShim.c's own
    // gbUpdate()). Ported as a direct cursorY computation
    // (`5 * gbFontSize * gbFontHeight`, the exact same arithmetic
    // gbPrintString()'s own real '\n' handling would perform five times
    // over, starting from the (0,0) gbUpdate() just reset) rather than a
    // literal `"\n\n\n\n\n"` string - no other ported game in this project
    // has yet exercised a backslash-escape sequence inside a real string
    // literal (gameTaquin.c's own multi-line text, for one real example,
    // advances gbCursorY by hand between gbPrintString() calls for exactly
    // this reason), so this port sticks with that same already-proven
    // "manual cursor arithmetic" convention instead of being the first to
    // rely on untested string-escape support.
    gbSetFont( gbFont5x7 );
    gbCursorX = 0;
    gbCursorY = 5 * gbFontSize * gbFontHeight;
    if( MINE_BOMB_COUNT >= 10 )
      gbSetFont( gbFont3x5 );
    gbPrintNumber( mineFlagCount );
    gbPrintString( "/" );
    gbPrintNumber( MINE_BOMB_COUNT );
    gbSetFont( gbFont3x5 );

    if( mineGameState == MINE_RUNNING )
    {
        mineProcessInput();
        mineDrawBoard();
        mineDrawCursor();
    }
    else if( mineGameState == MINE_WON )
    {
        mineDrawBoard();
        mineDrawCursor();
        mineDrawMessageBox( "You won! :)" );
    }
    else // MINE_LOST
    {
        mineDrawBoard();
        mineDrawCursor();
        mineDrawMessageBox( "You lost. :(" );
    }
}

void gameMinesweeper_init()
{
    gbBegin();
    mineBeginTitle();
}

void gameMinesweeper_update()
{
    if( !gbUpdate() ) return;

    if( mineAppState == MINE_STATE_TITLE ) mineUpdateTitle();
    else mineUpdatePlay();

    gbRenderFrame();
}
