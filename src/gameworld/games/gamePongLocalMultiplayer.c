#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Gamebuino-PongLocalMultiplayer (qubist, none specified). A genuine
// local hot-seat 2-player Pong: Player 1 (D-pad Up/Down) vs. Player 2/
// "oponent" [sic, real upstream spelling, preserved in this port's own
// comments but not in any user-visible text] (Button B up / Button A
// down) - no AI, no networking of any kind, so nothing to drop or
// redesign at all, unlike this catalog's other local-2P games.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment). `gb.collideRectRect()` ->
// `gbCollideRectRect()` directly.
//
// BLOCKING `gb.titleScreen(...)` -> EXPLICIT STATE: upstream calls this
// real blocking function twice - once in `setup()` (real boot) and again
// from `loop()` any time Button C is pressed mid-game (a real "pause"
// gesture - upstream's own title text literally becomes "Pong - PAUSED"
// on this second call, with `fontSize` left at 2 afterward as a real,
// if odd, upstream side effect that persists into whatever draws next).
// Ported as two states (`PONGLM_STATE_TITLE`/`PONGLM_STATE_PAUSE`) using
// this project's own established "blocking widget -> explicit resumable
// state" treatment (see gamePong.c's own header comment) - a fresh
// Button A press resumes play from either one, matching upstream's own
// real `titleScreen()` contract (Button A dismisses).
//
// DRAW CALLS MOVED INSIDE THE UPDATE GATE: upstream's own real score/
// ball/paddle draw calls sit OUTSIDE the `if (gb.update())` block, so
// real hardware redraws the same unchanged frame on every raw `loop()`
// iteration, not just on real logic ticks - harmless there (an idempotent
// redraw of identical state costs nothing meaningful on real hardware,
// which has no separate "commit to GPU" step to gate). This shim's own
// `gbUpdate()` is what performs the real per-tick clear/throttle
// `gameXxx_update()` must be gated behind (`if (!gbUpdate()) return;` -
// a real, project-wide requirement found the hard way porting
// SpinSpinSpinbuino), so this port's own draw calls simply moved inside
// that same gate instead - functionally identical output, since nothing
// upstream ever relied on drawing happening *between* real logic ticks.
//
// Real `boolean paused` is unused dead state in real upstream too (set
// to `false` at global scope, never read or written anywhere in
// `loop()`) - not ported, since it does nothing upstream either.

#define PONGLM_STATE_TITLE 0
#define PONGLM_STATE_PLAYING 1
#define PONGLM_STATE_PAUSE 2

int pongLMState;

int pongLMPlayerScore;
int pongLMPlayerH;
int pongLMPlayerW;
int pongLMPlayerX;
int pongLMPlayerY;
int pongLMPlayerVy;

int pongLMOpponentScore;
int pongLMOpponentH;
int pongLMOpponentW;
int pongLMOpponentX;
int pongLMOpponentY;
int pongLMOpponentVy;

int pongLMBallSize;
int pongLMBallX;
int pongLMBallY;
int pongLMBallVx;
int pongLMBallVy;

void pongLMResetPositions()
{
    pongLMPlayerH = 16;
    pongLMPlayerW = 3;
    pongLMPlayerX = 0;
    pongLMPlayerY = ( LCDHEIGHT - pongLMPlayerH ) / 2;
    pongLMPlayerVy = 2;

    pongLMOpponentH = 16;
    pongLMOpponentW = 3;
    pongLMOpponentX = LCDWIDTH - pongLMOpponentW;
    pongLMOpponentY = ( LCDHEIGHT - pongLMOpponentH ) / 2;
    pongLMOpponentVy = 2;

    pongLMBallSize = 6;
    pongLMBallX = LCDWIDTH - pongLMBallSize - pongLMOpponentW - 1;
    pongLMBallY = ( LCDHEIGHT - pongLMBallSize ) / 2;
    pongLMBallVx = 3;
    pongLMBallVy = 3;
}

void pongLMDrawTitle( char* text )
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

void pongLMUpdateTitle()
{
    pongLMDrawTitle( "PONG 2-PLAYER" );
    if( gbPressed( BTN_A ) )
      pongLMState = PONGLM_STATE_PLAYING;
}

void pongLMUpdatePause()
{
    pongLMDrawTitle( "PONG - PAUSED" );
    if( gbPressed( BTN_A ) )
      pongLMState = PONGLM_STATE_PLAYING;
}

void pongLMDraw()
{
    gbSetFont( gbFont3x5 );
    gbFontSize = 2;
    gbSetColor( 1 );
    gbCursorX = 15;
    gbCursorY = 16;
    gbPrintNumber( pongLMPlayerScore );

    gbCursorX = 57;
    gbCursorY = 16;
    gbPrintNumber( pongLMOpponentScore );
    gbSetFont( gbFont5x7 );

    gbFillRect( pongLMBallX, pongLMBallY, pongLMBallSize, pongLMBallSize );
    gbFillRect( pongLMPlayerX, pongLMPlayerY, pongLMPlayerW, pongLMPlayerH );
    gbFillRect( pongLMOpponentX, pongLMOpponentY, pongLMOpponentW, pongLMOpponentH );
}

void pongLMUpdatePlaying()
{
    if( gbPressed( BTN_C ) )
    {
        pongLMState = PONGLM_STATE_PAUSE;
        return;
    }

    if( gbRepeat( BTN_UP, 1 ) )
      pongLMPlayerY = gbMax( 0, pongLMPlayerY - pongLMPlayerVy );
    if( gbRepeat( BTN_DOWN, 1 ) )
      pongLMPlayerY = gbMin( LCDHEIGHT - pongLMPlayerH, pongLMPlayerY + pongLMPlayerVy );

    pongLMBallX = pongLMBallX + pongLMBallVx;
    pongLMBallY = pongLMBallY + pongLMBallVy;

    if( pongLMBallY < 0 )
    {
        pongLMBallY = 0;
        pongLMBallVy = -pongLMBallVy;
        gbPlayTick();
    }
    if( ( pongLMBallY + pongLMBallSize ) > LCDHEIGHT )
    {
        pongLMBallY = LCDHEIGHT - pongLMBallSize;
        pongLMBallVy = -pongLMBallVy;
        gbPlayTick();
    }
    if( gbCollideRectRect( pongLMBallX, pongLMBallY, pongLMBallSize, pongLMBallSize, pongLMPlayerX, pongLMPlayerY, pongLMPlayerW, pongLMPlayerH ) )
    {
        pongLMBallX = pongLMPlayerX + pongLMPlayerW;
        pongLMBallVx = -pongLMBallVx;
        gbPlayTick();
    }
    if( gbCollideRectRect( pongLMBallX, pongLMBallY, pongLMBallSize, pongLMBallSize, pongLMOpponentX, pongLMOpponentY, pongLMOpponentW, pongLMOpponentH ) )
    {
        pongLMBallX = pongLMOpponentX - pongLMBallSize;
        pongLMBallVx = -pongLMBallVx;
        gbPlayTick();
    }
    if( pongLMBallX < 0 )
    {
        pongLMOpponentScore = pongLMOpponentScore + 1;
        gbPlayCancel();
        pongLMBallX = 0 + pongLMPlayerW + 16;
        pongLMBallVx = gbAbsInt( pongLMBallVx );
        pongLMBallY = arand( LCDHEIGHT - pongLMBallSize );
    }
    if( ( pongLMBallX + pongLMBallSize ) > LCDWIDTH )
    {
        pongLMPlayerScore = pongLMPlayerScore + 1;
        gbPlayCancel();
        pongLMBallX = LCDWIDTH - pongLMBallSize - pongLMOpponentW - 16;
        pongLMBallVx = -gbAbsInt( pongLMBallVx );
        pongLMBallY = arand( LCDHEIGHT - pongLMBallSize );
    }
    if( pongLMPlayerScore == 10 || pongLMOpponentScore == 10 )
    {
        pongLMPlayerScore = 0;
        pongLMOpponentScore = 0;
    }

    if( gbRepeat( BTN_B, 1 ) )
      pongLMOpponentY = gbMax( 0, pongLMOpponentY - pongLMOpponentVy );
    if( gbRepeat( BTN_A, 1 ) )
      pongLMOpponentY = gbMin( LCDHEIGHT - pongLMOpponentH, pongLMOpponentY + pongLMOpponentVy );

    pongLMDraw();
}

void gamePongLocalMultiplayer_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 );
    gbPickRandomSeed();

    pongLMState = PONGLM_STATE_TITLE;
    pongLMPlayerScore = 0;
    pongLMOpponentScore = 0;
    pongLMResetPositions();
}

void gamePongLocalMultiplayer_update()
{
    if( !gbUpdate() ) return;

    if( pongLMState == PONGLM_STATE_PLAYING )
      pongLMUpdatePlaying();
    else if( pongLMState == PONGLM_STATE_TITLE )
      pongLMUpdateTitle();
    else
      pongLMUpdatePause();

    gbRenderFrame();
}
