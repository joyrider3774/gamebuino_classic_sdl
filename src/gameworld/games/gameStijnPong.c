#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Stijn's Pong (StijnCaerts-Gamebuino/Pong, Stijn Caerts, MIT). Reading the
// real upstream source directly settled one thing first: `movePlayer2()`
// only ever tracks the ball's own midpoint automatically, with no button
// read anywhere for the right paddle - a real single-player-vs-CPU-
// opponent Pong (the same shape as this catalog's already-shipped
// `gamePong.c`/"Pong Solo"), not the genuine local-2-player game its own
// staging task description assumed from the folder name alone.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment). Upstream's own `Player`/`Ball` classes
// (real member functions `up()`/`down()`/`draw()`) were flattened into
// plain `spong`-prefixed globals and free functions, matching this
// project's own established "flatten a single-instance C++ class into
// plain C" treatment. `gb.collideRectRect()` -> `gbCollideRectRect()`
// directly - a real, already-shipped shim primitive.
//
// BLOCKING `gb.titleScreen(F("Pong"))` -> EXPLICIT STATE: upstream calls
// this real blocking widget twice - once from `setup()` (real boot) and
// again from `loop()` any time Button C is pressed mid-game (a real
// "pause" gesture, using the exact same "Pong" text both times, unlike
// `gamePongLocalMultiplayer.c`'s own sibling port, which shows a distinct
// "PAUSED" variant on its own second call) - ported as two states
// (`SPONG_STATE_TITLE`/`SPONG_STATE_PAUSE`) sharing one draw function,
// using this project's own established "blocking widget -> explicit
// resumable state" treatment (see gamePong.c's own header comment). A
// fresh Button A press resumes play from either one with no reset of any
// kind, matching upstream's own real modal-pause behavior exactly (paddle/
// ball/score state is never touched by either real `titleScreen()` call).
//
// A real, genuine upstream bug preserved verbatim, not "fixed": real
// `resetBall()` sets `ball.y = (LCDWIDTH - ball.dim) / 2` - `LCDWIDTH`,
// not `LCDHEIGHT` - so every single scoring reset actually re-centers the
// ball on the screen's own *width* (39) rather than its height's real
// midpoint (21), landing it well below true vertical center on this
// 84x48 screen every time. Reproduced exactly via the same literal
// `LCDWIDTH` reference below, not silently corrected to `LCDHEIGHT`.
//
// `gb.battery.show = false;` was dropped outright, matching this
// project's own established no-equivalent precedent (see gamePong.c's own
// header comment) - purely cosmetic on real hardware, nothing to port.

#define SPONG_STATE_TITLE 0
#define SPONG_STATE_PLAYING 1
#define SPONG_STATE_PAUSE 2

int spongState;

int spongPlayerScore;
int spongPlayerW;
int spongPlayerH;
int spongPlayerX;
int spongPlayerY;

int spongOpponentScore;
int spongOpponentW;
int spongOpponentH;
int spongOpponentX;
int spongOpponentY;

int spongBallDim;
int spongBallX;
int spongBallY;
int spongBallVx;
int spongBallVy;

void spongResetPositions()
{
    spongPlayerW = 3;
    spongPlayerH = 16;
    spongPlayerX = 0;
    spongPlayerY = ( LCDHEIGHT - spongPlayerH ) / 2;

    spongOpponentW = 3;
    spongOpponentH = 16;
    spongOpponentX = LCDWIDTH - spongOpponentW;
    spongOpponentY = ( LCDHEIGHT - spongOpponentH ) / 2;

    spongBallDim = 6;
    spongBallX = ( LCDWIDTH - spongBallDim ) / 2;
    spongBallY = ( LCDHEIGHT - spongBallDim ) / 2;
    spongBallVx = 3;
    spongBallVy = 3;
}

void spongResetBall()
{
    spongBallX = ( LCDWIDTH - spongBallDim ) / 2;
    // Real upstream bug, preserved verbatim - see this file's own header
    // comment: LCDWIDTH here, not LCDHEIGHT.
    spongBallY = ( LCDWIDTH - spongBallDim ) / 2;
    spongBallVx = -spongBallVx;
}

void spongDrawTitle( char* text )
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbCursorX = 4;
    gbCursorY = 20;
    gbPrintString( text );
    gbCursorX = 4;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );
}

void spongUpdateTitle()
{
    spongDrawTitle( "PONG" );
    if( gbPressed( BTN_A ) )
      spongState = SPONG_STATE_PLAYING;
}

void spongUpdatePause()
{
    spongDrawTitle( "PONG" );
    if( gbPressed( BTN_A ) )
      spongState = SPONG_STATE_PLAYING;
}

void spongDraw()
{
    gbSetFont( gbFont3x5 );
    gbFontSize = 2;
    gbSetColor( 1 );
    gbCursorX = 15;
    gbCursorY = 16;
    gbPrintNumber( spongPlayerScore );

    gbCursorX = 57;
    gbCursorY = 16;
    gbPrintNumber( spongOpponentScore );
    gbSetFont( gbFont5x7 );

    gbFillRect( spongPlayerX, spongPlayerY, spongPlayerW, spongPlayerH );
    gbFillRect( spongOpponentX, spongOpponentY, spongOpponentW, spongOpponentH );
    gbFillRect( spongBallX, spongBallY, spongBallDim, spongBallDim );
}

void spongMovePlayer()
{
    if( gbRepeat( BTN_UP, 1 ) )
    {
        if( spongPlayerY > 0 )
          spongPlayerY = spongPlayerY - 2;
    }
    if( gbRepeat( BTN_DOWN, 1 ) )
    {
        if( spongPlayerY + spongPlayerH < LCDHEIGHT )
          spongPlayerY = spongPlayerY + 2;
    }
}

void spongMoveOpponent()
{
    // follow midpoint of the ball - a real, literal port of upstream's own
    // CPU opponent, not a genuine second player.
    if( ( ( spongBallY + spongBallDim ) / 2 ) < ( ( spongOpponentY + spongOpponentH ) / 2 ) )
    {
        if( spongOpponentY > 0 )
          spongOpponentY = spongOpponentY - 2;
    }
    else if( ( ( spongBallY + spongBallDim ) / 2 ) > ( ( spongOpponentY + spongOpponentH ) / 2 ) )
    {
        if( spongOpponentY + spongOpponentH < LCDHEIGHT )
          spongOpponentY = spongOpponentY + 2;
    }
}

void spongMoveBall()
{
    spongBallX = spongBallX + spongBallVx;
    spongBallY = spongBallY + spongBallVy;

    if( spongBallY < 0 )
    {
        spongBallY = 0;
        spongBallVy = -spongBallVy;
        gbPlayTick();
    }
    else if( ( spongBallY + spongBallDim ) > LCDHEIGHT )
    {
        spongBallY = LCDHEIGHT - spongBallDim;
        spongBallVy = -spongBallVy;
        gbPlayTick();
    }

    if( gbCollideRectRect( spongBallX, spongBallY, spongBallDim, spongBallDim, spongPlayerX, spongPlayerY, spongPlayerW, spongPlayerH ) )
    {
        spongBallX = spongPlayerX + spongPlayerW;
        spongBallVx = -spongBallVx;
        gbPlayTick();
    }
    else if( gbCollideRectRect( spongBallX, spongBallY, spongBallDim, spongBallDim, spongOpponentX, spongOpponentY, spongOpponentW, spongOpponentH ) )
    {
        spongBallX = spongOpponentX - spongBallDim;
        spongBallVx = -spongBallVx;
        gbPlayTick();
    }

    if( spongBallX < 0 )
    {
        spongOpponentScore = spongOpponentScore + 1;
        gbPlayOK();
        spongResetBall();
    }
    else if( ( spongBallX + spongBallDim ) > LCDWIDTH )
    {
        spongPlayerScore = spongPlayerScore + 1;
        gbPlayOK();
        spongResetBall();
    }

    if( spongPlayerScore == 10 || spongOpponentScore == 10 )
    {
        spongPlayerScore = 0;
        spongOpponentScore = 0;
    }
}

void spongUpdatePlaying()
{
    if( gbPressed( BTN_C ) )
    {
        spongState = SPONG_STATE_PAUSE;
        return;
    }

    spongMovePlayer();
    spongMoveOpponent();
    spongMoveBall();

    spongDraw();
}

void gameStijnPong_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 );
    gbPickRandomSeed();

    spongState = SPONG_STATE_TITLE;
    spongPlayerScore = 0;
    spongOpponentScore = 0;
    spongResetPositions();
}

void gameStijnPong_update()
{
    if( !gbUpdate() ) return;

    if( spongState == SPONG_STATE_PLAYING )
      spongUpdatePlaying();
    else if( spongState == SPONG_STATE_TITLE )
      spongUpdateTitle();
    else
      spongUpdatePause();

    gbRenderFrame();
}
