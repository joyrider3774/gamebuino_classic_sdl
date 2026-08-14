#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Taquin (RackhamLeNoir, GPLv3 - github.com/RackhamLeNoir/gamebuino-taquin).
// A classic 15-puzzle: slide 15 numbered tiles around a 4x4 grid (one cell
// always empty) using the D-pad until they're back in order. The board
// starts pre-shuffled by 200 random legal moves rather than a random
// deal, guaranteeing every deal is solvable (a random 15-puzzle deal is
// only solvable half the time - shuffling via legal moves sidesteps that
// entirely, same trick real sliding-puzzle implementations commonly use).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Upstream's own bare globals
// (`values`, `emptypos`, `moves`) and functions (`makemove`, `init_game`,
// `gamewon`, `drawgame`, `drawwin`, `inputsgame`, `inputswin`) were all
// given a `taq`-prefixed name, since Vircon32 has no linker and every game
// in this single compiled cartridge shares one flat global namespace.
// Upstream's own `switch` statement in `makemove()` became an if/else-if
// chain instead, matching the style already established by gamePong.c/
// gameAgaruino.c (neither uses `switch`, and this dialect's exact support
// for it is untested here, so this port doesn't risk being the first).
//
// Upstream's own blocking `gb.titleScreen(F("Taquin"))` - called once in
// setup() before the very first shuffle, and again from inside both
// inputsgame() and inputswin() as a genuine "pause the game" gesture on a
// Button C press - was converted into an explicit TAQ_STATE_TITLE state,
// matching the "blocking loop -> explicit resumable state" treatment used
// throughout this project (see gamePong.c's own header comment). Since
// upstream's own two C-press call sites both did exactly the same thing
// (unconditionally show the title screen), this port hoists that single
// check to the top of taqUpdatePlay() once, rather than duplicating it in
// both taqInputsGame() and taqInputsWin() - a pure de-duplication, not a
// behavior change: pressing C during play always pauses to the title
// either way. Board state (the tile grid and move counter) is left
// untouched by pausing/resuming, exactly like upstream's own blocking
// call resuming back into the same loop() iteration it interrupted.
// Real Gamebuino's own titleScreen() waits specifically for Button A,
// matching Vircon32's own menu-select button - reused directly here as
// the dismiss gesture, same as gamePong.c.
//
// `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a documented no-op
// (see gamePong.c's own header comment for why - this whole project's
// established precedent for every upstream `randomSeed()`-style call).
// Upstream's own shuffle used `random(4)`; ported as `arand(4)` (this
// dialect's own established RNG helper - see avrCompat.h). `gb.battery.
// show = true;` was dropped outright, matching gamePong.c's own treatment
// of the same real-hardware-only battery indicator.
//
// `taqDrawGame()`/`taqDrawWin()` now call the shim's own real gbSetFont(),
// restoring upstream's own exact real font5x7/font3x5 choices and cursor
// formulas verbatim - tile numbers centered in their own 14x12 cell by
// upstream's own real formula, the move counter in font3x5 at its own real
// (57,5) anchor, and the win screen's own real "You WON!"/"in N moves"/
// "Press <arrow icon> to restart" text (the icon restored as a real glyph -
// see taqRestartText above) at upstream's own real centered positions. This
// shim originally drew everything with one fixed 8x8 glyph table wide
// enough to force real compromises before gbSetFont()/real fonts existed:
// two-digit tile numbers visibly overlapped their own cell border, "moves"
// was shortened to "MOV" to fit a narrower column, and the restart line's
// own icon glyph (unsupported outside ASCII 32-127) was replaced with
// plain "PRESS A" text - none of that is needed anymore now that this file
// draws with the same real font sizes upstream does.
//
// A genuine upstream quirk, faithfully preserved: pressing a D-pad
// direction that's blocked by the grid edge (e.g. Up while the empty
// cell is already on the top row) still increments the move counter even
// though `taqMakeMove()` itself is a no-op for that press - this is
// exactly how upstream's own inputsgame() is written (moves++ always
// follows a button press, never gated on whether the move actually did
// anything), so a player can inflate their own move count for free by
// bumping a wall. Left exactly as upstream wrote it rather than "fixing"
// a genuinely visible, longstanding piece of this game's own behavior.
//
// Upstream's own gamewon() only checks that values[0..14] each hold their
// own index - it never separately checks that the empty cell sits at
// index 15. This happens to still be correct in practice (a solved
// values[0..14] can only occur when the empty cell has actually been
// carried around to index 15 by the moves that produced it), so this is
// not a bug to fix, just a slightly indirect check - ported verbatim as
// taqGameWon() below.

#define TAQ_MAXSHUFFLES 200

#define TAQ_UP    0
#define TAQ_DOWN  1
#define TAQ_LEFT  2
#define TAQ_RIGHT 3

#define TAQ_CELLWIDTH  14
#define TAQ_CELLHEIGHT 12

enum TaqState
{
    TAQ_STATE_TITLE = 0,
    TAQ_STATE_PLAY  = 1
};

int taqState;

int taqValues[16];
int taqEmptyPos;
int taqMoves;

// Real upstream's own win-screen line, `"Press \25 to restart"` - `\25` is
// an octal escape (021 = ASCII 21), one of real Gamebuino's own custom
// low-ASCII icon glyphs (a D-pad arrow, the same range used by
// gamebuinoShim.c's own font tables) rather than a printable character, so
// it can't be written as a plain quoted string literal - built as an
// explicit int array instead, restoring the real icon now that this
// shim's fonts actually cover ASCII 0-31.
char taqRestartText[19]=
{
    80, 114, 101, 115, 115, 32, // "Press "
    21,                         // real D-pad-arrow icon glyph
    32, 116, 111, 32, 114, 101, 115, 116, 97, 114, 116, // " to restart"
    0
};

// Direct port of upstream's own numlength() - real hardware's own version
// uses floor(log10()), reimplemented here via itoa() since this dialect
// has no log10() readily available and this is simpler anyway.
int taqNumLength( int number )
{
    char buf[16];
    int len = 0;
    itoa( number, buf, 10 );
    while( buf[ len ] != 0 )
      len = len + 1;
    return len;
}

// Direct port of upstream's own makemove() - moves the empty cell one
// step in `direction` by sliding the adjacent tile into it, no-op if that
// would walk the empty cell off the edge of the grid.
void taqMakeMove( int direction )
{
    if( direction == TAQ_UP )
    {
        if( taqEmptyPos < 12 )
        {
            taqValues[ taqEmptyPos ] = taqValues[ taqEmptyPos + 4 ];
            taqEmptyPos = taqEmptyPos + 4;
        }
    }
    else if( direction == TAQ_DOWN )
    {
        if( taqEmptyPos > 3 )
        {
            taqValues[ taqEmptyPos ] = taqValues[ taqEmptyPos - 4 ];
            taqEmptyPos = taqEmptyPos - 4;
        }
    }
    else if( direction == TAQ_LEFT )
    {
        if( taqEmptyPos % 4 != 3 )
        {
            taqValues[ taqEmptyPos ] = taqValues[ taqEmptyPos + 1 ];
            taqEmptyPos = taqEmptyPos + 1;
        }
    }
    else if( direction == TAQ_RIGHT )
    {
        if( taqEmptyPos % 4 != 0 )
        {
            taqValues[ taqEmptyPos ] = taqValues[ taqEmptyPos - 1 ];
            taqEmptyPos = taqEmptyPos - 1;
        }
    }
}

// Direct port of upstream's own init_game(): resets the grid to solved
// order, then scrambles it with 200 random legal moves (see this file's
// own header comment for why that guarantees a solvable deal).
void taqInitGame()
{
    int i;
    for( i = 0; i < 16; i++ )
      taqValues[ i ] = i;
    taqEmptyPos = 15;
    taqMoves = 0;

    for( i = 0; i < TAQ_MAXSHUFFLES; i++ )
      taqMakeMove( arand( 4 ) );
}

// Direct port of upstream's own gamewon() (see this file's own header
// comment about its own slightly-indirect empty-cell check).
bool taqGameWon()
{
    int i;
    for( i = 0; i < 15; i++ )
      if( taqValues[ i ] != i )
        return false;

    return true;
}

void taqBeginTitle()
{
    taqState = TAQ_STATE_TITLE;
}

void taqBeginPlay()
{
    taqState = TAQ_STATE_PLAY;
}

void taqUpdateTitle()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbCursorX = 18;
    gbCursorY = 16;
    gbPrintString( "TAQUIN" );
    gbCursorX = 14;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      taqBeginPlay();
}

// Direct port of upstream's own inputsgame(), minus the Button C handling
// (hoisted up into taqUpdatePlay() - see this file's own header comment).
void taqInputsGame()
{
    if( gbPressed( BTN_UP ) )
    {
        taqMakeMove( TAQ_UP );
        taqMoves = taqMoves + 1;
    }
    else if( gbPressed( BTN_DOWN ) )
    {
        taqMakeMove( TAQ_DOWN );
        taqMoves = taqMoves + 1;
    }

    if( gbPressed( BTN_LEFT ) )
    {
        taqMakeMove( TAQ_LEFT );
        taqMoves = taqMoves + 1;
    }
    else if( gbPressed( BTN_RIGHT ) )
    {
        taqMakeMove( TAQ_RIGHT );
        taqMoves = taqMoves + 1;
    }
}

// Direct port of upstream's own drawgame() - draws the 4x4 grid of tile
// cells (always) plus each tile's own number (skipped for whichever cell
// is currently the empty one), then the move counter to the right of the
// grid (see this file's own header comment for the layout adaptation).
void taqDrawGame()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbSetFont( gbFont5x7 ); // real upstream's own tile-number font

    int i, j;
    for( i = 0; i < 4; i++ )
    {
        for( j = 0; j < 4; j++ )
        {
            if( 4 * i + j != taqEmptyPos )
            {
                // Real upstream's own exact centering formula - practical
                // now that this shim's real font5x7 matches upstream's own
                // real glyph metrics (this used to be hand-tuned fudge
                // numbers for the old fixed 8x8 shim font, which centered
                // noticeably worse and overlapped two-digit tiles' own
                // cell border - see this file's own header comment).
                if( taqValues[ 4 * i + j ] < 9 )
                  gbCursorX = j * TAQ_CELLWIDTH + ( TAQ_CELLWIDTH - 5 ) / 2 + 1;
                else
                  gbCursorX = j * TAQ_CELLWIDTH + ( TAQ_CELLWIDTH - 11 ) / 2;

                gbCursorY = i * TAQ_CELLHEIGHT + ( TAQ_CELLHEIGHT - 7 ) / 2 + 1;
                gbPrintNumber( taqValues[ 4 * i + j ] + 1 );
            }

            gbDrawRect( j * TAQ_CELLWIDTH, i * TAQ_CELLHEIGHT, TAQ_CELLWIDTH, TAQ_CELLHEIGHT );
        }
    }

    // Real upstream's own move counter - font3x5, real (57,5) anchor with
    // a real println() advance to the "moves" label's own line beneath it
    // (this shim's own gbPrintString() has no println() equivalent, so the
    // advance is done explicitly here, matching what println() would do).
    gbSetFont( gbFont3x5 );
    gbCursorX = 4 * TAQ_CELLWIDTH + 1;
    gbCursorY = 5;
    gbPrintNumber( taqMoves );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 4 * TAQ_CELLWIDTH + 1;
    gbPrintString( "moves" );
}

// Direct port of upstream's own inputswin(), minus the Button C handling
// (hoisted up into taqUpdatePlay() - see this file's own header comment).
void taqInputsWin()
{
    if( gbPressed( BTN_A ) )
      taqInitGame();
}

// Direct port of upstream's own real drawwin(), cursor positions/fonts and
// all - including the real "Press <arrow icon> to restart" line (see this
// file's own header comment on taqRestartText).
void taqDrawWin()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbSetFont( gbFont5x7 );

    gbCursorX = ( LCDWIDTH - 8 * 6 ) / 2;
    gbCursorY = ( LCDHEIGHT - 5 * 8 ) / 2;
    gbPrintString( "You WON!" );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbCursorX = ( LCDWIDTH - ( 3 + taqNumLength( taqMoves ) ) * 6 ) / 2;
    gbPrintString( "in " );
    gbPrintNumber( taqMoves );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbCursorX = ( LCDWIDTH - 5 * 6 ) / 2;
    gbPrintString( "moves" );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight; // upstream's own trailing println("") blank line
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbSetFont( gbFont3x5 );
    gbCursorX = ( LCDWIDTH - 18 * 4 ) / 2;
    gbPrintString( taqRestartText );
}

// Direct port of upstream's own loop() body (the `if (gb.update())` part
// itself lives in gameTaquin_update() below, matching every other game's
// own split here) - dispatches to the win screen or the live board
// depending on taqGameWon(), exactly like upstream re-checks it every
// single tick rather than latching a separate "won" state.
void taqUpdatePlay()
{
    // pause the game if C is pressed (see this file's own header comment
    // about hoisting this check out of taqInputsGame()/taqInputsWin())
    if( gbPressed( BTN_C ) )
    {
        taqBeginTitle();
        return;
    }

    if( taqGameWon() )
    {
        taqInputsWin();
        taqDrawWin();
    }
    else
    {
        taqInputsGame();
        taqDrawGame();
    }
}

void gameTaquin_init()
{
    gbBegin();
    taqInitGame();
    taqBeginTitle();
}

void gameTaquin_update()
{
    if( !gbUpdate() ) return;

    if( taqState == TAQ_STATE_TITLE ) taqUpdateTitle();
    else taqUpdatePlay();

    gbRenderFrame();
}
