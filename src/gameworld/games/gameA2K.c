#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// A to K / A2K (Carlos Mari, carloslabs.com, Nov 2014, v1.0.b - Creative
// Commons BY 4.0, http://creativecommons.org/licenses/by/4.0/, recovered
// via direct download from carloslabs.com, no GitHub repo). A real 2048
// variant: slide the board with the D-pad, matching adjacent tiles of the
// same letter promote one of them to the next letter up ("A+A=B, B+B=C,
// ... J+J=K"), the game is won on reaching a K tile and lost the instant
// every one of the 16 board cells is filled (matching this game's own real
// readme_A2K.txt note that it deliberately differs from the usual 2048
// "no legal move left" lose condition - "don't let the board get full.
// Ever."). Upstream ships as a single `A2K.ino`, `#include`ing Gamebuino's
// own split per-module headers (`<Buttons.h>`/`<Display.h>`/`<Sound.h>`
// alongside `<Gamebuino.h>`) rather than the usual single include - the
// same real library either way, no different behavior to port.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the full reasoning). `byte` became
// plain `int` (avrCompat.h aliasing); `word` likewise, since this dialect
// has no distinct integer widths. `random(N)` became `arand(N)` (this
// dialect's own established RNG helper). `boolean` became `bool`. Upstream's
// own `GRID[GRID_X][GRID_Y]` 2D array is ported as a real `int[4][4]
// a2kGrid` array (this dialect's own `int name[N][M]` declaration order,
// already proven working by gameBlobAttack.c's own `blobField`/
// gameAsterocks.c's own `asterShipFrames` tables).
//
// TITLE SCREEN: upstream's own blocking `gb.titleScreen(F("\n>> A to K
// <<\n\nA 2048 clone\n\ncarloslabs.com"))` (called once from setup()) is
// converted into an explicit A2K_STATE_TITLE state, dismissed by a genuine
// `gbPressed(BTN_A)` - the same "blocking loop -> explicit resumable
// state" treatment used throughout this project (see gamePong.c's own
// header comment). `gb.pickRandomSeed()` runs right after the title screen
// is dismissed in real upstream `setup()` (after `gb.titleScreen()`
// returns, before the first `initGrid()`), so it's called from
// `a2kOnTitleDismiss()` here, matching that real ordering exactly - ported
// as `gbPickRandomSeed()`, a documented no-op. `gb.battery.show = false;`
// was dropped outright (purely cosmetic, no equivalent exists or is
// needed here, matching every other port's own treatment of this line).
//
// PERSISTENCE: real upstream sets `gb.display.persistence = true;` and
// only ever draws the grid lines once (in `initGrid()`), relying on that
// flag to keep them on screen across every later frame that only draws
// tiles/text on top without clearing first. This shim's own `gbUpdate()`
// always clears the framebuffer fresh every tick (matching real hardware's
// own default `persistence=false` behavior - see CLAUDE.md), so
// `persistence = true;` is dropped outright and `a2kDrawBoard()` instead
// redraws the grid lines, every tile, and the HUD text unconditionally on
// every single call - functionally identical to upstream's own real
// on-screen result (the grid lines never actually change position or
// visibility once drawn), and the same "just redraw everything every tick"
// simplification this project's own CLAUDE.md documents as safe for every
// game ported so far (see "Thumbnail atlas, quit-confirmation dialog...").
//
// THE 50ms POST-MOVE DELAY IS DROPPED, NOT JUST SHORTENED: real upstream's
// own `loop()` sets `bBusy = true` unconditionally at the very start of
// every `moveXxx()` call (even one that changes nothing on the board), then
// - still within that same `if(gb.update())` tick - does a genuine blocking
// `delay(DELAY_MILLIS)` (50ms), clears `bBusy`, spawns one new random tile,
// and redraws. Nothing is drawn to the display between the move and that
// delayed spawn, so the delay is a pure wall-clock stall with zero
// observable visual or gameplay effect once ported - this dialect has no
// blocking delay()-equivalent primitive at all (see gameSnakeClassic.c's
// own header comment for the established precedent of dropping a upstream
// `delay()` outright for exactly this reason), so `a2kUpdatePlay()` below
// performs the same move -> spawn -> redraw sequence within one real
// engine tick with no delay in between, matching what a player actually
// sees on real hardware (the pause is invisible either way).
//
// A GENUINE UPSTREAM QUIRK, PRESERVED: `moveXxx()` sets `bBusy = true`
// (and therefore triggers a fresh random tile spawn) even on a press that
// changes nothing on the board at all (e.g. pressing LEFT when every tile
// is already pushed as far left as it can go) - upstream never checks
// whether the move actually did anything before spawning. Preserved
// exactly here: `a2kMoveXxx()` always sets `a2kBusy = true`, so every
// single D-pad press spawns a new tile regardless of whether it moved
// anything, matching real hardware's own real (if arguably overly
// generous) behavior verbatim.
//
// A SECOND GENUINE UPSTREAM QUIRK, PRESERVED: winning (reaching a K tile)
// only sets `bGameWon = true` and shows the "YOU WON" overlay - it does
// NOT block further play the way `bGameOver` blocks movement. A player can
// keep sliding tiles after winning (and even merge two K tiles into an L,
// which is why `TILE_SCORE[]` below has a 12th entry for a letter beyond
// K that upstream's own win condition never actually requires reaching).
// Preserved exactly: `a2kGameWon` never gates the D-pad checks in
// `a2kUpdatePlay()`, only `a2kGameOver` does.
//
// `gb.changeGame()` (Button C, a real-hardware "switch cartridge on the SD
// card" OS feature) has no equivalent in this single-cartridge shared-menu
// model and is dropped outright - Button C simply does nothing mid-game
// here, matching gameSnakeAbc.c's/gameBlocksBuino.c's own established
// treatment of the same real upstream call. Button A mid-game is upstream's
// own real, genuine no-op too (`if(gb.buttons.pressed(BTN_A)){ //todo UNDO
// ?? }` - an unimplemented feature upstream itself never built, not
// something this port dropped).
//
// REAL BITMAP ART: upstream A2K has no bitmap/sprite art at all - every
// visual element is either a drawn line (`gbDrawFastHLine`/`gbDrawFastVLine`)
// or real font glyph (`gbDrawChar`/`gbPrintString`/`gbPrintNumber`), so
// there is nothing to restore here beyond the real font choices upstream
// itself uses (`font3x5` for the default/HUD text, `font5x7` for the grid
// letters and title/game-over/win headlines - ported as `gbFont3x5`/
// `gbFont5x7` respectively, matching every real `gb.display.setFont(...)`
// call site exactly).
//
// No missing shim primitives were found while porting this game - every
// primitive it needed (gbDrawFastHLine/VLine, gbDrawChar, gbPrintString/
// gbPrintNumber, gbSetFont/gbFont3x5/gbFont5x7, gbFillRect, arand()) already
// existed.

#define A2K_GRID_X 4
#define A2K_GRID_Y 4
#define A2K_POINTER_TEXT_X 8
#define A2K_POINTER_TEXT_Y 7
#define A2K_EMPTY_TILE 32 // ' ' - "blank space"
#define A2K_A_TILE 65     // 'A'
#define A2K_K_TILE 75     // 'K'
#define A2K_F_COUNT 4

// Real upstream grid_lines_x/y, grid_wide_x/y, grid_origin_x/y - never
// mutated after their own real initializers, so ported as plain constants
// rather than variables.
#define A2K_GRID_LINES 5
#define A2K_GRID_WIDE 10
#define A2K_GRID_ORIGIN 5

enum A2KState
{
    A2K_STATE_TITLE = 0,
    A2K_STATE_PLAY = 1
};

int a2kState;

int a2kGrid[A2K_GRID_X][A2K_GRID_Y];

int a2kScore;
int a2kMoves;
bool a2kBusy;
bool a2kGameOver;
bool a2kGameWon;

// Direct port of upstream's own real `word TILE_SCORE[12]` table, indexed
// by `letter - 'A'` (see a2kAddScore() below) - index 10 ('K') is the real
// win threshold, index 11 ('L') exists only because winning doesn't itself
// block further merges (see this file's own header comment).
int a2kTileScore[12] = { 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };

// -----------------------------------------------------------------------------
// Board mechanics - direct ports of upstream's own real functions
// -----------------------------------------------------------------------------

bool a2kIsEmptyTile()
{
    int x, y;
    for( y = 0; y < A2K_GRID_Y; y++ )
      for( x = 0; x < A2K_GRID_X; x++ )
        if( a2kGrid[ x ][ y ] == A2K_EMPTY_TILE )
          return true;
    return false;
}

// == upstream setRandomTile(numTiles) - places `numTiles` new tiles (each
// either 'A' or 'B', a genuine 50/50 pick per upstream's own `random(2)`)
// on random currently-empty cells.
void a2kSetRandomTile( int numTiles )
{
    int j;
    for( j = 0; j < numTiles; j++ )
    {
        bool ready = false;
        while( !ready )
        {
            int rx = arand( 4 );
            int ry = arand( 4 );
            int rz = arand( 2 );
            if( a2kGrid[ rx ][ ry ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ rx ][ ry ] = A2K_A_TILE + rz;
                ready = true;
            }
        }
    }
}

// == upstream addScore(bTile) - `bTile` is the NEW (already-promoted)
// letter value, matching every real call site below.
void a2kAddScore( int bTile )
{
    int ix = bTile - A2K_A_TILE;
    a2kScore = a2kScore + a2kTileScore[ ix ];
    if( bTile >= A2K_K_TILE )
      a2kGameWon = true;
}

// == upstream moveLeft() - four repeated left-compress-and-merge passes per
// row, matching upstream's own real `F_COUNT` loop exactly (each pass can
// only resolve one adjacent merge per row, so upstream repeats the whole
// scan 4 times to guarantee full compression across a 4-wide row).
void a2kMoveLeft()
{
    int fCount = 0;
    a2kBusy = true;
    while( fCount < A2K_F_COUNT )
    {
        fCount = fCount + 1;
        int y;
        for( y = 0; y < A2K_GRID_Y; y++ )
        {
            if( a2kGrid[ 0 ][ y ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ 0 ][ y ] = a2kGrid[ 1 ][ y ];
                a2kGrid[ 1 ][ y ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ 0 ][ y ] == a2kGrid[ 1 ][ y ] && a2kGrid[ 0 ][ y ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ 0 ][ y ] = 1 + a2kGrid[ 0 ][ y ];
                a2kAddScore( a2kGrid[ 0 ][ y ] );
                a2kGrid[ 1 ][ y ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ 1 ][ y ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ 1 ][ y ] = a2kGrid[ 2 ][ y ];
                a2kGrid[ 2 ][ y ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ 1 ][ y ] == a2kGrid[ 2 ][ y ] && a2kGrid[ 1 ][ y ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ 1 ][ y ] = 1 + a2kGrid[ 2 ][ y ];
                a2kAddScore( a2kGrid[ 1 ][ y ] );
                a2kGrid[ 2 ][ y ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ 2 ][ y ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ 2 ][ y ] = a2kGrid[ 3 ][ y ];
                a2kGrid[ 3 ][ y ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ 2 ][ y ] == a2kGrid[ 3 ][ y ] && a2kGrid[ 2 ][ y ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ 2 ][ y ] = 1 + a2kGrid[ 3 ][ y ];
                a2kAddScore( a2kGrid[ 2 ][ y ] );
                a2kGrid[ 3 ][ y ] = A2K_EMPTY_TILE;
            }
        }
    }
}

// == upstream moveRight() - the mirrored right-compress-and-merge pass.
void a2kMoveRight()
{
    int fCount = 0;
    a2kBusy = true;
    while( fCount < A2K_F_COUNT )
    {
        fCount = fCount + 1;
        int y;
        for( y = 0; y < A2K_GRID_Y; y++ )
        {
            if( a2kGrid[ 2 ][ y ] == a2kGrid[ 3 ][ y ] && a2kGrid[ 3 ][ y ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ 3 ][ y ] = 1 + a2kGrid[ 3 ][ y ];
                a2kAddScore( a2kGrid[ 3 ][ y ] );
                a2kGrid[ 2 ][ y ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ 3 ][ y ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ 3 ][ y ] = a2kGrid[ 2 ][ y ];
                a2kGrid[ 2 ][ y ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ 1 ][ y ] == a2kGrid[ 2 ][ y ] && a2kGrid[ 2 ][ y ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ 2 ][ y ] = 1 + a2kGrid[ 2 ][ y ];
                a2kAddScore( a2kGrid[ 2 ][ y ] );
                a2kGrid[ 1 ][ y ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ 2 ][ y ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ 2 ][ y ] = a2kGrid[ 1 ][ y ];
                a2kGrid[ 1 ][ y ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ 0 ][ y ] == a2kGrid[ 1 ][ y ] && a2kGrid[ 1 ][ y ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ 1 ][ y ] = 1 + a2kGrid[ 0 ][ y ];
                a2kAddScore( a2kGrid[ 1 ][ y ] );
                a2kGrid[ 0 ][ y ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ 1 ][ y ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ 1 ][ y ] = a2kGrid[ 0 ][ y ];
                a2kGrid[ 0 ][ y ] = A2K_EMPTY_TILE;
            }
        }
    }
}

// == upstream moveDown() - same shape as moveRight(), applied column-wise.
void a2kMoveDown()
{
    int fCount = 0;
    a2kBusy = true;
    while( fCount < A2K_F_COUNT )
    {
        fCount = fCount + 1;
        int x;
        for( x = 0; x < A2K_GRID_X; x++ )
        {
            if( a2kGrid[ x ][ 2 ] == a2kGrid[ x ][ 3 ] && a2kGrid[ x ][ 3 ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 3 ] = 1 + a2kGrid[ x ][ 3 ];
                a2kAddScore( a2kGrid[ x ][ 3 ] );
                a2kGrid[ x ][ 2 ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ x ][ 3 ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 3 ] = a2kGrid[ x ][ 2 ];
                a2kGrid[ x ][ 2 ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ x ][ 1 ] == a2kGrid[ x ][ 2 ] && a2kGrid[ x ][ 2 ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 2 ] = 1 + a2kGrid[ x ][ 2 ];
                a2kAddScore( a2kGrid[ x ][ 2 ] );
                a2kGrid[ x ][ 1 ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ x ][ 2 ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 2 ] = a2kGrid[ x ][ 1 ];
                a2kGrid[ x ][ 1 ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ x ][ 0 ] == a2kGrid[ x ][ 1 ] && a2kGrid[ x ][ 1 ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 1 ] = 1 + a2kGrid[ x ][ 0 ];
                a2kAddScore( a2kGrid[ x ][ 1 ] );
                a2kGrid[ x ][ 0 ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ x ][ 1 ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 1 ] = a2kGrid[ x ][ 0 ];
                a2kGrid[ x ][ 0 ] = A2K_EMPTY_TILE;
            }
        }
    }
}

// == upstream moveUp() - same shape as moveLeft(), applied column-wise.
void a2kMoveUp()
{
    int fCount = 0;
    a2kBusy = true;
    while( fCount < A2K_F_COUNT )
    {
        fCount = fCount + 1;
        int x;
        for( x = 0; x < A2K_GRID_X; x++ )
        {
            if( a2kGrid[ x ][ 0 ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 0 ] = a2kGrid[ x ][ 1 ];
                a2kGrid[ x ][ 1 ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ x ][ 0 ] == a2kGrid[ x ][ 1 ] && a2kGrid[ x ][ 0 ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 0 ] = 1 + a2kGrid[ x ][ 0 ];
                a2kAddScore( a2kGrid[ x ][ 0 ] );
                a2kGrid[ x ][ 1 ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ x ][ 1 ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 1 ] = a2kGrid[ x ][ 2 ];
                a2kGrid[ x ][ 2 ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ x ][ 1 ] == a2kGrid[ x ][ 2 ] && a2kGrid[ x ][ 1 ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 1 ] = 1 + a2kGrid[ x ][ 2 ];
                a2kAddScore( a2kGrid[ x ][ 1 ] );
                a2kGrid[ x ][ 2 ] = A2K_EMPTY_TILE;
            }

            if( a2kGrid[ x ][ 2 ] == A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 2 ] = a2kGrid[ x ][ 3 ];
                a2kGrid[ x ][ 3 ] = A2K_EMPTY_TILE;
            }
            if( a2kGrid[ x ][ 2 ] == a2kGrid[ x ][ 3 ] && a2kGrid[ x ][ 2 ] > A2K_EMPTY_TILE )
            {
                a2kGrid[ x ][ 2 ] = 1 + a2kGrid[ x ][ 3 ];
                a2kAddScore( a2kGrid[ x ][ 2 ] );
                a2kGrid[ x ][ 3 ] = A2K_EMPTY_TILE;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Drawing - direct ports of upstream's own real initGrid()'s own drawing
// half, renderGrid(), renderData(), gameWon(), gameOver()
// -----------------------------------------------------------------------------

// == upstream renderData() - the real HUD panel to the right of the board.
void a2kRenderData()
{
    int cursorXpos;

    gbSetFont( gbFont5x7 );
    gbCursorX = 49;
    gbCursorY = 1;
    gbPrintString( "A to K" );

    gbSetFont( gbFont3x5 );
    gbCursorX = 49;
    gbCursorY = 14;
    gbPrintString( "[ Score ]" );
    if( a2kScore >= 1000 ) cursorXpos = 58;
    else if( a2kScore >= 100 ) cursorXpos = 61;
    else cursorXpos = 64;
    gbCursorX = cursorXpos;
    gbCursorY = 22;
    gbPrintNumber( a2kScore );

    gbCursorX = 49;
    gbCursorY = 32;
    gbPrintString( "[ Moves ]" );
    if( a2kMoves >= 1000 ) cursorXpos = 58;
    else if( a2kMoves >= 100 ) cursorXpos = 61;
    else cursorXpos = 64;
    gbCursorX = cursorXpos;
    gbCursorY = 40;
    gbPrintNumber( a2kMoves );
}

// == upstream initGrid()'s own real grid-line drawing (only the X axis is
// computed upstream too, since the board is square - see this file's own
// header comment) + renderGrid()'s own per-cell letter loop + renderData().
void a2kDrawBoard()
{
    gbSetColor( 1 );
    gbClear();

    int gridLength = A2K_GRID_WIDE * ( A2K_GRID_LINES - 1 );
    int i;
    for( i = 0; i < A2K_GRID_LINES; i++ )
    {
        int gx = A2K_GRID_ORIGIN + i * A2K_GRID_WIDE;
        gbDrawFastVLine( gx, A2K_GRID_ORIGIN, gridLength );
        gbDrawFastHLine( A2K_GRID_ORIGIN, gx, gridLength );
    }

    gbSetFont( gbFont5x7 );
    int x, y;
    for( y = 0; y < A2K_GRID_Y; y++ )
      for( x = 0; x < A2K_GRID_X; x++ )
        gbDrawChar( a2kGrid[ x ][ y ], A2K_POINTER_TEXT_X + A2K_GRID_WIDE * x, A2K_POINTER_TEXT_Y + A2K_GRID_WIDE * y );

    a2kRenderData();
}

// == upstream gameWon().
void a2kDrawGameWon()
{
    gbSetColor( 1 );
    gbFillRect( 2, 5, 55, 32 );
    gbSetColor( 0 );
    gbFillRect( 3, 6, 53, 30 );
    gbSetColor( 1 );
    gbCursorX = 7;
    gbCursorY = 9;
    gbPrintString( "YOU WON A2K!\n\n   Press B to \n    restart" );
}

// == upstream gameOver().
void a2kDrawGameOver()
{
    gbSetColor( 1 );
    gbFillRect( 2, 5, 55, 32 );
    gbSetColor( 0 );
    gbFillRect( 3, 6, 53, 30 );
    gbSetColor( 1 );
    gbCursorX = 12;
    gbCursorY = 9;
    gbPrintString( "GAME OVER\n\n   Press B to \n    restart" );
}

// == upstream initGrid()'s own real state-reset half (the drawing half is
// folded into a2kDrawBoard() above, called unconditionally every tick - see
// this file's own header comment on why persistence=true is dropped).
void a2kResetGame()
{
    a2kMoves = 0;
    a2kScore = 0;
    a2kBusy = false;
    a2kGameOver = false;
    a2kGameWon = false;

    int x, y;
    for( y = 0; y < A2K_GRID_Y; y++ )
      for( x = 0; x < A2K_GRID_X; x++ )
        a2kGrid[ x ][ y ] = A2K_EMPTY_TILE;

    a2kSetRandomTile( 2 );
}

// -----------------------------------------------------------------------------
// States
// -----------------------------------------------------------------------------

// == upstream's own real post-titleScreen() code in setup() (pickRandomSeed()
// + initGrid() + setRandomTile(2), see this file's own header comment on why
// pickRandomSeed() belongs here rather than in gameA2K_init()).
void a2kOnTitleDismiss()
{
    gbPickRandomSeed();
    a2kResetGame();
    a2kState = A2K_STATE_PLAY;
}

void a2kUpdateTitle()
{
    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );
    gbCursorX = 14;
    gbCursorY = 4;
    gbPrintString( ">> A to K <<\n\nA 2048 clone\n\ncarloslabs.com" );
    gbCursorX = 22;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      a2kOnTitleDismiss();
}

// == upstream loop()'s own `if(gb.update()){ ... }` body.
void a2kUpdatePlay()
{
    a2kDrawBoard();

    if( gbPressed( BTN_C ) )
    {
        // real upstream: gb.changeGame() - see this file's own header comment.
    }
    if( gbPressed( BTN_A ) )
    {
        // real upstream: `//todo UNDO ??` - a genuine upstream no-op, not a
        // dropped feature (see this file's own header comment).
    }
    if( gbPressed( BTN_B ) )
    {
        a2kResetGame();
        a2kDrawBoard();
    }

    if( gbPressed( BTN_UP ) && !a2kBusy && !a2kGameOver )
    {
        a2kMoveUp();
        a2kMoves = a2kMoves + 1;
    }
    if( gbPressed( BTN_DOWN ) && !a2kBusy && !a2kGameOver )
    {
        a2kMoveDown();
        a2kMoves = a2kMoves + 1;
    }
    if( gbPressed( BTN_LEFT ) && !a2kBusy && !a2kGameOver )
    {
        a2kMoveLeft();
        a2kMoves = a2kMoves + 1;
    }
    if( gbPressed( BTN_RIGHT ) && !a2kBusy && !a2kGameOver )
    {
        a2kMoveRight();
        a2kMoves = a2kMoves + 1;
    }

    if( a2kBusy && !a2kGameOver )
    {
        // real upstream `delay(DELAY_MILLIS)` dropped here - see this
        // file's own header comment.
        a2kBusy = false;
        a2kSetRandomTile( 1 );
        a2kDrawBoard();
    }

    if( a2kGameOver )
      a2kDrawGameOver();

    a2kGameOver = !a2kIsEmptyTile();

    if( a2kGameWon )
      a2kDrawGameWon();
}

void gameA2K_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 );
    a2kState = A2K_STATE_TITLE;
}

void gameA2K_update()
{
    if( !gbUpdate() ) return;

    if( a2kState == A2K_STATE_TITLE ) a2kUpdateTitle();
    else a2kUpdatePlay();

    gbRenderFrame();
}
