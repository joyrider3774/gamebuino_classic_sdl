#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Breakout by Ripper121 (ripper121, license none specified - recovered via
// direct download, no live GitHub repo; the author's current
// gamebuino.com profile has no matching Classic-era upload). A real,
// single-file Breakout clone: move a paddle left/right to keep a ball in
// play and clear a grid of bricks, gaining one point per brick. Every
// cleared board grows the brick grid by one row (up to a real cap) and
// starts a fresh level with the same score; missing the ball once ends
// the game outright - there is no lives system at all in real upstream,
// preserved here exactly (a genuine, load-bearing design choice, not an
// omission).
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the full reasoning).
// `random(0, 2)` became `arand(2)` (this dialect's own established RNG
// helper). `byte` fields became plain `int`/`bool` (avrCompat.h aliasing/
// this dialect's own real bool type). Real `B00000000`-style binary
// literals in the three PROGMEM bitmaps were converted to hex verbatim
// (same bit pattern, different literal syntax only). Upstream's own
// `BrickStruct Brick[BrickRows][BrickColsMax]` 2D struct-array grid is
// flattened to a 1D `BrkoBrick[]` array with manual `brkoBrickIdx(row,col)`
// indexing instead of a real 2D declaration - the same safer, already-
// proven-elsewhere route gameMinesweeper.c's own header comment describes
// choosing (a 2D array of a *struct* type together was never proven out
// anywhere in this project, unlike plain-int 2D arrays or 1D struct
// arrays, both already proven).
//
// Real upstream quirk audit:
// - Upstream's own inline comments mislabel which screen edge each ball-
//   wall collision check belongs to (`Ball.x < 0` is commented "collision
//   with the top", `Ball.y < 0` is commented "collision with the left",
//   etc) - purely a comment slip with zero effect on the actual bounce
//   logic, so this port's own comments describe the real edge each check
//   tests instead of reproducing the mislabeling.
// - Upstream has no lives/multi-ball system: touching the bottom edge
//   once is an immediate game over (score and level both reset to 0,
//   brick columns reset to the starting 4). Preserved exactly - a real,
//   deliberate design choice for this particular clone, not a bug.
// - Winning a full board does NOT reset the score - only a loss does.
//   Once the brick-column count reaches its real cap (11), every
//   subsequent full clear re-shows "You win the game!" and immediately
//   re-fills the same 11-column board, so score keeps climbing
//   indefinitely at the hardest difficulty. Preserved exactly.
// - Upstream's own brick explosion plays 5 real bitmap frames
//   (explode2/1/0/1/2) via 5 sequential `drawBitmap()` + `display.update()`
//   + `delay(40)` calls, genuinely blocking the whole MCU (and therefore
//   button input) for ~200ms. Vircon32 has no mid-function blocking
//   display-push/delay primitive at all (a game's own update() function
//   only ever produces one visible frame per call), so this is ported as
//   an explicit multi-tick animation state instead (`brkoExploding`) -
//   the same "blocking loop -> explicit resumable state" treatment used
//   throughout this project (see gamePong.c's own title-screen conversion,
//   gameSimonbuino.c's own note-timing state). Each of the 5 frames is
//   held for exactly one real logic tick (this shim's own default 20fps,
//   50ms/tick, close to upstream's real 40ms/frame), with paddle/ball
//   movement and the pause toggle genuinely frozen for the animation's
//   whole duration - matching real hardware's own de-facto input freeze
//   during its blocking `delay()` calls. Upstream's own nested collision
//   loop can genuinely destroy more than one brick in a single tick (the
//   ball clipping the real 1px gap between two adjacent bricks) - this is
//   preserved by collecting every brick touched in one tick into a small
//   pending list (`BRKO_MAX_EXPLOSIONS` slots, generous headroom for a
//   case that in practice destroys at most 2-4 bricks at once) and
//   animating all of them in lockstep rather than only ever supporting a
//   single explosion at a time.
// - Upstream's own message screens ("Next level!"/"You win the game!"/
//   "Game Over!" plus the current level/score) are shown via
//   `display.update()` followed by a genuine blocking `delay(5000)` before
//   the next level (or a fresh game) is actually initialized. Ported as
//   another explicit tick-counted state (`BRKO_STATE_MESSAGE`,
//   `BRKO_MESSAGE_TICKS` = 100 real logic ticks = 5 real seconds at this
//   shim's default 20fps) rather than a blocking call - the message text
//   itself, and exactly when Level/BrickCols/score get mutated relative to
//   it, are reproduced line-for-line from upstream's own `loop()`.
//
// Real upstream `gb.begin(F("Breakout by Ripper121"), logo)` shows its own
// 64x28 boot-logo bitmap once before gameplay starts; this shim's
// `gbBegin()` has no boot-splash equivalent of its own (see
// gamebuinoShim.h). Following gamePunkt.c's own established precedent for
// exactly this situation, the real logo bitmap is restored on an explicit,
// dismissible `BRKO_STATE_TITLE` screen (real upstream has no in-game
// title/menu screen at all - this is a deliberate adaptation of the boot
// splash into the same idiom every other ported game already uses for its
// own title screen, not invented gameplay).

typedef struct{
    int x;
    int y;
    int w;
    int h;
    bool exist;
} BrkoBrick;

// BrkoBrickRows: real upstream `#define BrickRows (LCDWIDTH/(BrickSpaceX+
// BrickW))-1` = (84/(1+5))-1 = 13 - a fixed function of the real 84px-wide
// LCD, so it is safe to bake in as a literal here. BrkoBrickColsMax is
// upstream's own real `BrickColsMax` cap (the brick grid's real growable
// dimension, one real column of bricks added per level up to this cap).
#define BRKO_BRICK_ROWS 13
#define BRKO_BRICK_COLS_MAX 11
#define BRKO_BRICK_W 5
#define BRKO_BRICK_H 2
#define BRKO_BRICK_SPACE_X 1
#define BRKO_BRICK_SPACE_Y 1
#define BRKO_MAX_EXPLOSIONS 8
#define BRKO_EXPLODE_FRAME_COUNT 5
#define BRKO_EXPLODE_TICKS_PER_FRAME 1
#define BRKO_MESSAGE_TICKS 100

enum BrkoState
{
    BRKO_STATE_TITLE = 0,
    BRKO_STATE_PLAY = 1,
    BRKO_STATE_MESSAGE = 2
};

enum BrkoMsgKind
{
    BRKO_MSG_NEXTLEVEL = 0,
    BRKO_MSG_WIN = 1,
    BRKO_MSG_GAMEOVER = 2
};

int brkoState;

// Player paddle
int brkoPlayerX;
int brkoPlayerY;
int brkoPlayerW = 10;
int brkoPlayerH = 2;
int brkoPlayerVx = 2;
int brkoPlayerScore = 0;

// Ball
int brkoBallX;
int brkoBallY;
int brkoBallVx;
int brkoBallVy;
int brkoBallV;
int brkoBallSize = 3;

bool brkoPaused = false;
bool brkoLost = false;

// Bricks - flattened 1D grid, index = row * BRKO_BRICK_COLS_MAX + col
BrkoBrick brkoBrick[BRKO_BRICK_ROWS*BRKO_BRICK_COLS_MAX];
int brkoBrickCols = 4;
int brkoBrickCount = 0;
int brkoDestBrickCount = 0;
int brkoLevel = 1;

// Explosion animation (see this file's own header comment)
bool brkoExploding = false;
int brkoExplodeFrame = 0;
int brkoExplodeTimer = 0;
int brkoExplodeCount = 0;
int brkoExplodeX[BRKO_MAX_EXPLOSIONS];
int brkoExplodeY[BRKO_MAX_EXPLOSIONS];

// Message screen (see this file's own header comment)
int brkoMessageTimer = 0;
int brkoMsgKind;
int brkoMsgLevel;
int brkoMsgScore;

// Real upstream `logo[]` (64x28 PROGMEM bitmap, shown once via real
// `gb.begin(F("Breakout by Ripper121"), logo)`) - real binary literals
// converted to hex verbatim, same bit pattern.
int brkoLogoBitmap[226] = { 64, 28,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xF, 0xE0, 0x0, 0x0, 0x60, 0x0, 0x1, 0x0,
    0xF, 0xE0, 0x0, 0x0, 0x60, 0x0, 0x3, 0x0,
    0xC, 0x23, 0x4E, 0x1C, 0x66, 0x71, 0xBF, 0xC0,
    0xF, 0xE3, 0xBB, 0x16, 0x6C, 0xFD, 0xBB, 0xC0,
    0xF, 0xE3, 0x3E, 0xE, 0x78, 0xDD, 0xBB, 0x0,
    0xC, 0x23, 0x38, 0x36, 0x7C, 0xDD, 0xBB, 0x0,
    0xF, 0xE3, 0x1E, 0x3E, 0x6C, 0x79, 0xFB, 0xC0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0xFC, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0xFE, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0xFE, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x4, 0xFE, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0xFC, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x3F, 0xFF, 0xFF, 0xF0, 0x0, 0x0,
    0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFE, 0x0, 0x0,
    0x0, 0x1, 0xFF, 0xFF, 0xFF, 0xFE, 0x0, 0x0,
    0x0, 0x1, 0xFF, 0xFF, 0xFF, 0xFE, 0x0, 0x0, };

// Real upstream `explode0[]`/`explode1[]`/`explode2[]` (8x8 PROGMEM
// bitmaps, the 5-frame flicker sequence played explode2-1-0-1-2, sparsest
// to densest and back).
int brkoExplode0Bitmap[10] = { 8, 8,
    0x10, 0x41, 0x4, 0x90, 0x4, 0x41, 0x8, 0xA4, };
int brkoExplode1Bitmap[10] = { 8, 8,
    0x0, 0x41, 0x4, 0x10, 0x4, 0x40, 0x8, 0x0, };
int brkoExplode2Bitmap[10] = { 8, 8,
    0x0, 0x0, 0x0, 0x10, 0x4, 0x40, 0x8, 0x0, };

int brkoBrickIdx( int row, int col )
{
    return row * BRKO_BRICK_COLS_MAX + col;
}

// Real upstream `Brick Init`/`Player Init`/`Ball Init` (the tail of
// `loop()`'s own `if (GameReset)` block) - lays out the current
// `brkoBrickCols`-wide grid and resets the paddle/ball to their real
// starting positions/velocities. Does NOT touch score/level/brickCols
// themselves - the caller decides those first (matching upstream's own
// real mutation order in the win/loss branches).
void brkoInitLevel()
{
    int row;
    int col;
    int idx;

    brkoBrickCount = 0;
    for( row = 0; row < BRKO_BRICK_ROWS; row = row + 1 )
    {
        for( col = 0; col < brkoBrickCols; col = col + 1 )
        {
            idx = brkoBrickIdx( row, col );
            brkoBrick[idx].x = ( LCDWIDTH / ( BRKO_BRICK_SPACE_X + BRKO_BRICK_W ) ) / 4 + row * ( BRKO_BRICK_W + BRKO_BRICK_SPACE_X );
            brkoBrick[idx].y = 5 + BRKO_BRICK_SPACE_Y + col * ( BRKO_BRICK_H + BRKO_BRICK_SPACE_Y );
            brkoBrick[idx].w = BRKO_BRICK_W;
            brkoBrick[idx].h = BRKO_BRICK_H;
            brkoBrick[idx].exist = true;
            brkoBrickCount = brkoBrickCount + 1;
        }
    }

    brkoPlayerW = 10;
    brkoPlayerH = 2;
    brkoPlayerX = ( LCDWIDTH - brkoPlayerW ) / 2;
    brkoPlayerVx = 2;
    brkoPlayerY = LCDHEIGHT - brkoPlayerH;

    brkoBallSize = 3;
    brkoBallX = ( LCDWIDTH - brkoPlayerW ) / 2;
    brkoBallY = LCDHEIGHT - brkoPlayerH - brkoBallSize;
    brkoBallV = 1;
    brkoBallVx = -brkoBallV;
    brkoBallVy = -brkoBallV;
    // real upstream `if (random(0, 2) == 1) Ball.vx = -Ball.vx;`
    if( arand( 2 ) == 1 )
      brkoBallVx = -brkoBallVx;

    brkoExploding = false;
    brkoExplodeCount = 0;
    brkoPaused = false;
}

void brkoDrawTitle()
{
    gbFontSize = 1;
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 4, brkoLogoBitmap );
    gbCursorX = 6;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      brkoState = BRKO_STATE_PLAY;
}

// Real upstream message text (`if (GameReset) { if (GameWin) {...} else if
// (!GameFirstRun) {...} }`) - line-for-line the same three templates,
// drawn every tick this state is active (this shim's own `gbUpdate()`
// clears+redraws the framebuffer every logic tick, unlike real hardware's
// own one-shot `display.update()` push before the real blocking `delay()`).
void brkoDrawMessage()
{
    gbFontSize = 1;
    if( brkoMsgKind == BRKO_MSG_NEXTLEVEL )
      gbPrintString( "Next level!\n" );
    else if( brkoMsgKind == BRKO_MSG_WIN )
      gbPrintString( "You win the game!\n" );
    else
      gbPrintString( "Game Over!\n" );

    gbPrintString( "Level:\n" );
    gbPrintNumber( brkoMsgLevel );
    gbPrintString( "\nScore:\n" );
    gbPrintNumber( brkoMsgScore );
}

// Real upstream `GameWin` branch: `Level++`; if the brick-column cap
// hasn't been reached yet, grow it by one and show "Next level!" with the
// new Level; otherwise leave it at the cap and show "You win the game!" -
// either way the level reinit that follows re-fills the (possibly grown)
// board. Score is NOT reset here - only a loss resets it.
void brkoEnterMessageWin()
{
    brkoLevel = brkoLevel + 1;
    if( brkoBrickCols < BRKO_BRICK_COLS_MAX )
    {
        brkoBrickCols = brkoBrickCols + 1;
        brkoMsgKind = BRKO_MSG_NEXTLEVEL;
    }
    else
    {
        brkoMsgKind = BRKO_MSG_WIN;
    }
    brkoMsgLevel = brkoLevel;
    brkoMsgScore = brkoPlayerScore;
    gbPlayOK();
    brkoMessageTimer = BRKO_MESSAGE_TICKS;
    brkoState = BRKO_STATE_MESSAGE;
}

// Real upstream `else if (!GameFirstRun)` branch: shows "Game Over!" with
// the score/level the player just lost with, THEN resets brick columns/
// score/level/destroyed-count back to their real starting values (matching
// upstream's own exact mutation order - the message itself is captured
// before the reset, so the player still sees what they actually reached).
void brkoEnterMessageLoss()
{
    brkoMsgKind = BRKO_MSG_GAMEOVER;
    brkoMsgLevel = brkoLevel;
    brkoMsgScore = brkoPlayerScore;
    gbPlayCancel();
    brkoBrickCols = 4;
    brkoPlayerScore = 0;
    brkoLevel = 0;
    brkoDestBrickCount = 0;
    brkoMessageTimer = BRKO_MESSAGE_TICKS;
    brkoState = BRKO_STATE_MESSAGE;
}

void brkoUpdateMessage()
{
    brkoDrawMessage();
    brkoMessageTimer = brkoMessageTimer - 1;
    if( brkoMessageTimer <= 0 )
    {
        brkoInitLevel();
        brkoState = BRKO_STATE_PLAY;
    }
}

// Draws every pending explosion at the current animation frame - called
// both the tick a new hit is detected (frame 0, matching upstream's own
// first `drawBitmap()` call before its first `delay()`) and every tick
// afterward while `brkoExploding` stays true.
void brkoDrawExplosionFrame()
{
    int i;
    int* bmp;

    if( ( brkoExplodeFrame == 0 ) || ( brkoExplodeFrame == 4 ) )
      bmp = brkoExplode2Bitmap;
    else if( ( brkoExplodeFrame == 1 ) || ( brkoExplodeFrame == 3 ) )
      bmp = brkoExplode1Bitmap;
    else
      bmp = brkoExplode0Bitmap;

    for( i = 0; i < brkoExplodeCount; i = i + 1 )
      gbDrawBitmap( brkoExplodeX[i], brkoExplodeY[i], bmp );
}

void gameBreakoutRipper_init()
{
    gbBegin();
    brkoLevel = 1;
    brkoBrickCols = 4;
    brkoPlayerScore = 0;
    brkoDestBrickCount = 0;
    brkoLost = false;
    brkoInitLevel();
    brkoState = BRKO_STATE_TITLE;
}

void gameBreakoutRipper_update()
{
    int row;
    int col;
    int idx;
    int ballOnPlayer;

    if( !gbUpdate() ) return;

    if( brkoState == BRKO_STATE_TITLE )
    {
        brkoDrawTitle();
        gbRenderFrame();
        return;
    }

    if( brkoState == BRKO_STATE_MESSAGE )
    {
        brkoUpdateMessage();
        gbRenderFrame();
        return;
    }

    // BRKO_STATE_PLAY below - a direct port of real upstream's own
    // `if (gb.update()) { ... }` body.

    // pause the game if C is pressed - real hardware's own blocking
    // explosion delay() also blocks button reads, so the toggle is
    // likewise unavailable for the whole explosion animation here.
    if( !brkoExploding )
    {
        if( gbPressed( BTN_C ) )
          brkoPaused = !brkoPaused;
    }

    if( !brkoExploding && !brkoPaused )
    {
        // move the player
        if( gbRepeat( BTN_LEFT, 1 ) )
          brkoPlayerX = gbMax( 0, brkoPlayerX - brkoPlayerVx );
        if( gbRepeat( BTN_RIGHT, 1 ) )
          brkoPlayerX = gbMin( LCDWIDTH - brkoPlayerW, brkoPlayerX + brkoPlayerVx );

        // move the ball
        brkoBallX = brkoBallX + brkoBallVx;
        brkoBallY = brkoBallY + brkoBallVy;

        // collision with the left edge
        if( brkoBallX < 0 )
        {
            brkoBallX = 0;
            brkoBallVx = -brkoBallVx;
            gbPlayTick();
        }
        // collision with the top edge
        if( brkoBallY < 0 )
        {
            brkoBallY = 0;
            brkoBallVy = -brkoBallVy;
            gbPlayTick();
        }
        // collision with the right edge
        if( ( brkoBallX + brkoBallSize ) > LCDWIDTH )
        {
            brkoBallX = LCDWIDTH - brkoBallSize;
            brkoBallVx = -brkoBallVx;
            gbPlayTick();
        }
        // collision with the bottom edge - ball lost, real upstream has no
        // lives system at all: this ends the current game outright
        if( ( brkoBallY + brkoBallSize ) > LCDHEIGHT )
        {
            brkoBallY = LCDHEIGHT - brkoBallSize;
            brkoBallVy = -brkoBallVy;
            brkoLost = true;
            gbPlayTick();
        }
        // collision with the paddle
        if( gbCollideRectRect( brkoBallX, brkoBallY, brkoBallSize, brkoBallSize, brkoPlayerX, brkoPlayerY, brkoPlayerW, brkoPlayerH ) )
        {
            ballOnPlayer = ( brkoBallX + ( brkoBallSize / 2 ) - brkoPlayerX ) + 1;
            if( ballOnPlayer > ( brkoPlayerW / 2 ) )
              brkoBallVx = brkoBallV;
            else
              brkoBallVx = -brkoBallV;
            brkoBallVy = -brkoBallVy;
            gbPlayTick();
        }
    }

    // draw the ball / paddle / remaining bricks - drawn every tick
    // regardless of paused/exploding, matching upstream's own draw calls
    // sitting outside the `if (!paused)` guard.
    gbFillRect( brkoBallX, brkoBallY, brkoBallSize, brkoBallSize );
    gbFillRect( brkoPlayerX, brkoPlayerY, brkoPlayerW, brkoPlayerH );
    for( row = 0; row < BRKO_BRICK_ROWS; row = row + 1 )
    {
        for( col = 0; col < brkoBrickCols; col = col + 1 )
        {
            idx = brkoBrickIdx( row, col );
            if( brkoBrick[idx].exist )
              gbFillRect( brkoBrick[idx].x, brkoBrick[idx].y, brkoBrick[idx].w, brkoBrick[idx].h );
        }
    }

    if( brkoExploding )
    {
        brkoDrawExplosionFrame();
        brkoExplodeTimer = brkoExplodeTimer + 1;
        if( brkoExplodeTimer >= BRKO_EXPLODE_TICKS_PER_FRAME )
        {
            brkoExplodeTimer = 0;
            brkoExplodeFrame = brkoExplodeFrame + 1;
            if( brkoExplodeFrame >= BRKO_EXPLODE_FRAME_COUNT )
            {
                brkoExploding = false;
                brkoExplodeCount = 0;
                gbPlayTick(); // real upstream's own trailing playTick() after the 5-frame flicker
            }
        }
    }
    else if( !brkoPaused )
    {
        // collision with the bricks - collect every brick touched this
        // tick (the ball can legitimately clip two adjacent bricks across
        // their real 1px gap) into the pending explosion list
        brkoExplodeCount = 0;
        for( row = 0; row < BRKO_BRICK_ROWS; row = row + 1 )
        {
            for( col = 0; col < brkoBrickCols; col = col + 1 )
            {
                idx = brkoBrickIdx( row, col );
                if( brkoBrick[idx].exist && gbCollideRectRect( brkoBallX, brkoBallY, brkoBallSize, brkoBallSize, brkoBrick[idx].x, brkoBrick[idx].y, brkoBrick[idx].w, brkoBrick[idx].h ) )
                {
                    brkoPlayerScore = brkoPlayerScore + 1;
                    brkoDestBrickCount = brkoDestBrickCount + 1;
                    brkoBrick[idx].exist = false;
                    brkoBallVy = -brkoBallVy;
                    if( brkoExplodeCount < BRKO_MAX_EXPLOSIONS )
                    {
                        brkoExplodeX[brkoExplodeCount] = brkoBallX - brkoBallSize;
                        brkoExplodeY[brkoExplodeCount] = brkoBallY - brkoBallSize;
                        brkoExplodeCount = brkoExplodeCount + 1;
                    }
                }
            }
        }

        if( brkoExplodeCount > 0 )
        {
            gbPlayCancel();
            brkoExploding = true;
            brkoExplodeFrame = 0;
            brkoExplodeTimer = 0;
            brkoDrawExplosionFrame();
        }
    }

    if( brkoLost )
    {
        brkoLost = false;
        brkoEnterMessageLoss();
    }
    else if( ( !brkoExploding ) && ( brkoDestBrickCount >= brkoBrickCount ) )
    {
        brkoDestBrickCount = 0;
        brkoEnterMessageWin();
    }

    gbRenderFrame();
}
