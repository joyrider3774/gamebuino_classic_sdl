#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Pong Solo (Aurelien Rodot, LGPLv3 - the official Gamebuino Classic
// library's own bundled "2.Intermediate/Pong" example sketch,
// github.com/Gamebuino/Gamebuino-Classic). The first game ported to this
// project, picked specifically to prove the whole pipeline (gamebuinoShim,
// gamesMain, menu, the SDL3 backend) against a small, complete, real game
// before porting anything larger - the same "prove the shim on 2-3 simple
// games first" discipline the sibling tinyjoypad_vircon32/Tinyjoypad_SDL
// projects used for their own shims.
//
// A single-player Pong: move a paddle up/down, a simple AI opponent
// tracks the ball, first to 10 points resets the score.
//
// Copied byte-for-byte unmodified from the sibling gamebuino_classic_
// vircon32 build's own gamePong.c - real proof that this dialect-
// conversion recipe's own "no `int[]`-text/no comma-separated struct
// globals used here" carve-out (see CLAUDE.md's "Dialect conversion"
// section) is not just theoretical: this file happens to use plain string
// literals (already valid `char*` in standard C, and already valid as
// this dialect's own `int[]` string convention in Vircon32) and no struct-
// typed globals at all, so it compiles as real standard C completely
// unchanged. See that file's own original header comment (preserved in
// git history of the sibling project) for the full original upstream-
// Arduino-to-Vircon32-dialect porting notes (gb.x.y() flattening,
// gb.display.print() splitting into gbPrintNumber()/gbPrintString(),
// max()/min() -> gbMax()/gbMin(), random() -> arand(), the blocking
// gb.titleScreen() -> explicit PONG_STATE_TITLE state) - none of those
// needed any further changes for this build.

int pongPlayerScore = 0;
int pongPlayerH = 16;
int pongPlayerW = 3;
int pongPlayerX = 0;
int pongPlayerY;
int pongPlayerVy = 2;

int pongOpponentScore = 0;
int pongOpponentH = 16;
int pongOpponentW = 3;
int pongOpponentX;
int pongOpponentY;
int pongOpponentVy = 2;

int pongBallSize = 6;
int pongBallX;
int pongBallY;
int pongBallVx = 3;
int pongBallVy = 3;

enum PongState
{
    PONG_STATE_TITLE = 0,
    PONG_STATE_PLAY = 1
};

int pongState;

void pongResetPositions()
{
    pongPlayerY = ( LCDHEIGHT - pongPlayerH ) / 2;
    pongOpponentX = LCDWIDTH - pongOpponentW;
    pongOpponentY = ( LCDHEIGHT - pongOpponentH ) / 2;
    pongBallSize = 6;
    pongBallX = LCDWIDTH - pongBallSize - pongOpponentW - 1;
    pongBallY = ( LCDHEIGHT - pongBallSize ) / 2;
    pongBallVx = 3;
    pongBallVy = 3;
}

void pongBeginTitle()
{
    pongState = PONG_STATE_TITLE;
}

void pongBeginPlay()
{
    pongState = PONG_STATE_PLAY;
    gbFontSize = 2;
}

void pongUpdateTitle()
{
    gbSetColor( 1 );
    gbCursorX = 15;
    gbCursorY = 16;
    gbFontSize = 1;
    gbPrintString( "PONG SOLO" );
    gbCursorX = 8;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      pongBeginPlay();
}

void pongUpdatePlay()
{
    // pause the game if C is pressed
    if( gbPressed( BTN_C ) )
    {
        pongBeginTitle();
        return;
    }

    // move the player
    if( gbRepeat( BTN_UP, 1 ) )
      pongPlayerY = gbMax( 0, pongPlayerY - pongPlayerVy );
    if( gbRepeat( BTN_DOWN, 1 ) )
      pongPlayerY = gbMin( LCDHEIGHT - pongPlayerH, pongPlayerY + pongPlayerVy );

    // move the ball
    pongBallX = pongBallX + pongBallVx;
    pongBallY = pongBallY + pongBallVy;

    // collision with the top border
    if( pongBallY < 0 )
    {
        pongBallY = 0;
        pongBallVy = -pongBallVy;
        gbPlayTick();
    }
    // collision with the bottom border
    if( ( pongBallY + pongBallSize ) > LCDHEIGHT )
    {
        pongBallY = LCDHEIGHT - pongBallSize;
        pongBallVy = -pongBallVy;
        gbPlayTick();
    }
    // collision with the player
    if( gbCollideRectRect( pongBallX, pongBallY, pongBallSize, pongBallSize, pongPlayerX, pongPlayerY, pongPlayerW, pongPlayerH ) )
    {
        pongBallX = pongPlayerX + pongPlayerW;
        pongBallVx = -pongBallVx;
        gbPlayTick();
    }
    // collision with the opponent
    if( gbCollideRectRect( pongBallX, pongBallY, pongBallSize, pongBallSize, pongOpponentX, pongOpponentY, pongOpponentW, pongOpponentH ) )
    {
        pongBallX = pongOpponentX - pongBallSize;
        pongBallVx = -pongBallVx;
        gbPlayTick();
    }
    // collision with the left side
    if( pongBallX < 0 )
    {
        pongOpponentScore = pongOpponentScore + 1;
        gbPlayCancel();
        pongBallX = LCDWIDTH - pongBallSize - pongOpponentW - 1;
        pongBallVx = gbAbsInt( pongBallVx );
        pongBallY = arand( LCDHEIGHT - pongBallSize );
    }
    // collision with the right side
    if( ( pongBallX + pongBallSize ) > LCDWIDTH )
    {
        pongPlayerScore = pongPlayerScore + 1;
        gbPlayOK();
        pongBallX = LCDWIDTH - pongBallSize - pongOpponentW - 16;
        pongBallVx = gbAbsInt( pongBallVx );
        pongBallY = arand( LCDHEIGHT - pongBallSize );
    }
    // reset score when 10 is reached
    if( ( pongPlayerScore == 10 ) || ( pongOpponentScore == 10 ) )
    {
        pongPlayerScore = 0;
        pongOpponentScore = 0;
    }

    // move the opponent
    if( ( pongOpponentY + ( pongOpponentH / 2 ) ) < ( pongBallY + ( pongBallSize / 2 ) ) )
    {
        pongOpponentY = pongOpponentY + pongOpponentVy;
        pongOpponentY = gbMin( LCDHEIGHT - pongOpponentH, pongOpponentY );
    }
    else
    {
        pongOpponentY = pongOpponentY - pongOpponentVy;
        pongOpponentY = gbMax( 0, pongOpponentY );
    }

    // draw the score
    gbFontSize = 2;
    gbCursorX = 15;
    gbCursorY = 16;
    gbPrintNumber( pongPlayerScore );

    gbCursorX = 57;
    gbCursorY = 16;
    gbPrintNumber( pongOpponentScore );

    // draw the ball / player / opponent
    gbSetColor( 1 );
    gbFillRect( pongBallX, pongBallY, pongBallSize, pongBallSize );
    gbFillRect( pongPlayerX, pongPlayerY, pongPlayerW, pongPlayerH );
    gbFillRect( pongOpponentX, pongOpponentY, pongOpponentW, pongOpponentH );
}

void gamePong_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 ); // real upstream's own setup()-time `gb.display.setFont(font5x7)` - stays set for the whole game, title screen included
    pongResetPositions();
    pongBeginTitle();
}

void gamePong_update()
{
    if( !gbUpdate() ) return;

    if( pongState == PONG_STATE_TITLE ) pongUpdateTitle();
    else pongUpdatePlay();

    gbRenderFrame();
}
