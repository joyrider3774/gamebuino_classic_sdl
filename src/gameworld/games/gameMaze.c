#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Maze (Andy O'Neill, MIT License - github.com/aoneill01/gamebuino-maze).
// A real-time, animated maze crawler: walk cell-to-cell from the top-left
// corner to the bottom-right exit; reaching it shows a "Good job!" popup and
// immediately generates a fresh, bigger maze (growing by 10 cells each axis,
// capped at 50x50) so the game just keeps going. The maze itself is built
// with a randomized Prim's-algorithm-style wall-knockdown: starting from a
// single visited cell, repeatedly pick a random unvisited "candidate" cell
// adjacent to the visited region and knock down the wall to a random already-
// visited neighbor, until every cell has been visited.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(N)` became `arand(N)` (this
// dialect's own established RNG helper). Upstream's own `byte` fields/locals
// (Arduino/AVR's own alias for uint8_t) became plain `int` throughout - this
// dialect has no such type, and every other ported game in this project
// already takes the same "just use int" approach (see avrCompat.h's own
// header comment on why aliasing every AVR fixed-width type to plain int
// costs nothing here). Upstream's own `switch(random(4))` direction picker in
// generateMaze() was rewritten as an if/else-if chain (no switch statement
// used anywhere else in this project's own ported games, and this dialect's
// exact switch-statement support was never proven out on those other ports
// either) - the exact same random-retry-until-a-valid-direction-is-found
// algorithm is otherwise preserved unchanged.
//
// Upstream's own blocking `gb.titleScreen(F(""), logo)` (called once from
// setup(), and again - synchronously, mid-loop() - whenever Button C is
// pressed during play, i.e. "hold C to restart") was converted into an
// explicit MAZE_STATE_TITLE/MAZE_STATE_PLAY state machine, matching the same
// "blocking loop -> explicit resumable state" treatment gamePong.c's own
// header comment documents (real Gamebuino's own titleScreen() waits for
// Button A, matching Vircon32's own menu-select button, reused directly here
// as the dismiss gesture too). The upstream logo bitmap passed alongside that
// call (a real 64x36 PROGMEM byte array, `logo[]` in the real Maze.ino) is
// restored via `gbDrawBitmap()`, copied verbatim into `mazeLogoBitmap` below
// (the real bytes were already plain 0x-hex literals upstream, not Arduino
// B-binary literals, so no bit-conversion was needed, just a direct copy -
// verified byte-for-byte against the real source). Real `titleScreen()`
// draws the logo at a fixed `x=0` (see the real Gamebuino.cpp's own
// implementation) since real hardware's own logic centers the whole
// composition - including its own Gamebuino boot logo above/beside it -
// rather than the game's logo alone; this shim has no such boot-logo
// composition, so the bitmap is instead explicitly centered horizontally
// here (`x = (LCDWIDTH - width) / 2`) and placed above the "PRESS A" prompt.
//
// `gb.popup(F("     Good job!"), 60)` (a real, small, transient on-screen
// notification - `Gamebuino::popup()`) is ported directly as
// `gbPopup("     Good job!", 60)`, which auto-draws itself on top of
// everything else already drawn that frame, exactly matching real
// hardware's own non-blocking "gameplay continues immediately underneath
// the popup" behavior.
// `gb.battery.show = false;` was dropped outright (no battery indicator
// exists here at all - purely cosmetic on real hardware). `gb.pickRandomSeed()`
// became `gbPickRandomSeed()`, a documented no-op, matching this whole
// project's own established precedent for every other upstream
// `randomSeed()`-style call.
//
// One genuine upstream typo, silently normalized (see gameAgaruino.c's own
// header comment for the established precedent of doing this for an obvious,
// no-impact typo rather than faithfully reproducing it): generateMaze()'s own
// final line called `gridSet(cells, posX, posY, true)`, passing (row=posX,
// col=posY) - every other call site in the real source consistently passes
// (row, col) as (posY, posX) instead. This one is a no-op bug either way
// since posX and posY have both just been reset to 0 on the very same line,
// so swapping them changes nothing - normalized below to
// `mazeGridSet(mazeCells, mazePosY, mazePosX, true)` for consistency with
// every other call site in this file instead of reproducing a typo that
// would otherwise just look like a fresh mistake in this port.

#define MAZE_MAX_WIDTH 50
#define MAZE_MAX_HEIGHT 50
#define MAZE_MAX_CANDIDATES 30

// (MAZE_MAX_HEIGHT * MAZE_MAX_WIDTH - 1) / 8 + 1, precomputed - one bit per
// cell, packed 8-per-int exactly like upstream's own byte arrays.
#define MAZE_BIT_ARRAY_SIZE 313

// The real upstream title-screen logo (`logo[] PROGMEM` in the real
// Maze.ino, by Andy O'Neill) - a 64x36 bitmap, copied byte-for-byte (see
// this file's own header comment). Header cells 0/1 are width/height
// (matching gbDrawBitmap()'s own documented format), followed by
// ceil(64/8)*36 = 288 row-major, MSB-first data bytes.
int mazeLogoBitmap[290]= { 64, 36,
    0x6F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x6F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x68, 0x82, 0x0, 0xA0, 0x0, 0x2A, 0x20, 0x3,
    0x6E, 0xEE, 0xBE, 0xBE, 0xFB, 0xEA, 0xEB, 0xFF,
    0x60, 0x8, 0x8A, 0x0, 0x2A, 0x20, 0xA, 0x8B,
    0x7B, 0xAE, 0xFB, 0xAF, 0xAF, 0xAE, 0xEE, 0xBB,
    0x60, 0xA0, 0x2A, 0xA2, 0xA, 0x8, 0x20, 0xB,
    0x7B, 0xBE, 0xEA, 0xEF, 0xFA, 0xEF, 0xAE, 0xEB,
    0x6A, 0xA0, 0x0, 0x28, 0x28, 0xA2, 0xA2, 0x83,
    0x6A, 0xFF, 0xBA, 0xFB, 0xEE, 0xBE, 0xFF, 0xBB,
    0x60, 0x2, 0xAA, 0x0, 0x2, 0x80, 0x28, 0xB,
    0x7B, 0xFE, 0xEF, 0xAE, 0xFA, 0xBA, 0xAE, 0xFB,
    0x60, 0x80, 0x0, 0xA8, 0x82, 0x8A, 0x82, 0x2B,
    0x7B, 0xBE, 0xAB, 0xFF, 0xFF, 0xFB, 0xBF, 0xEF,
    0x60, 0x8, 0xAB, 0xFF, 0xFF, 0xFA, 0x0, 0xB,
    0x7A, 0xEE, 0xBB, 0x77, 0xFF, 0xFB, 0xAE, 0xEB,
    0x62, 0x22, 0x8B, 0x24, 0xE3, 0xB8, 0xA2, 0x23,
    0x7A, 0xEA, 0xFF, 0x56, 0x7B, 0x5E, 0xEE, 0xAB,
    0x62, 0x2A, 0x83, 0x55, 0x77, 0x38, 0x8A, 0xAB,
    0x7B, 0xFF, 0xFB, 0x74, 0x63, 0x9B, 0xBB, 0xAB,
    0x60, 0x0, 0xA3, 0xFF, 0xFF, 0xF8, 0x80, 0xAB,
    0x7B, 0xFA, 0xAF, 0xFF, 0xFF, 0xFB, 0xFA, 0xFB,
    0x60, 0x22, 0x20, 0xA0, 0x2A, 0x20, 0xA, 0x2B,
    0x6F, 0xBB, 0xFF, 0xBF, 0xAB, 0xBF, 0xBA, 0xEB,
    0x62, 0x2A, 0x0, 0x0, 0x2A, 0x80, 0xAA, 0x23,
    0x6A, 0xAE, 0xFB, 0xBE, 0xEA, 0xEF, 0xEF, 0xFF,
    0x6A, 0x80, 0xA, 0x8, 0x0, 0x2, 0x88, 0x3,
    0x6F, 0xEE, 0xBF, 0xFA, 0xEE, 0xEE, 0xBE, 0xAB,
    0x60, 0x22, 0x88, 0xA, 0x82, 0x80, 0x0, 0xAB,
    0x7B, 0xEB, 0xBB, 0xFB, 0xAB, 0xEE, 0xEF, 0xAF,
    0x68, 0x88, 0x80, 0xA, 0xA8, 0x22, 0x22, 0xA3,
    0x6E, 0xFE, 0xEA, 0xEE, 0xFB, 0xBF, 0xAA, 0xAF,
    0x60, 0x2, 0x8A, 0x20, 0x22, 0x0, 0xA8, 0xA3,
    0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB,
    0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};

int mazeWidth;
int mazeHeight;
int mazeHWalls[MAZE_BIT_ARRAY_SIZE];
int mazeVWalls[MAZE_BIT_ARRAY_SIZE];
int mazeCells[MAZE_BIT_ARRAY_SIZE];

int mazeCandidates[MAZE_MAX_CANDIDATES];
int mazeCandidateCount;

int mazePosX;
int mazePosY;

int mazeMovingX;
int mazeMovingY;

enum MazeState
{
    MAZE_STATE_TITLE = 0,
    MAZE_STATE_PLAY = 1
};

int mazeState;

bool mazeIsSet( int* grid, int row, int col )
{
    int offset = row * mazeWidth + col;
    return ( grid[ offset / 8 ] & ( 1 << ( 7 - ( offset % 8 ) ) ) ) != 0;
}

void mazeGridSet( int* grid, int row, int col, bool on )
{
    int offset = row * mazeWidth + col;
    if( on )
      grid[ offset / 8 ] = grid[ offset / 8 ] | ( 1 << ( 7 - ( offset % 8 ) ) );
    else
      grid[ offset / 8 ] = grid[ offset / 8 ] & ~( 1 << ( 7 - ( offset % 8 ) ) );
}

void mazeAddCandidate( int val )
{
    if( mazeCandidateCount >= MAZE_MAX_CANDIDATES ) return;

    int i;
    for( i = 0; i < mazeCandidateCount; i++ )
    {
        if( mazeCandidates[ i ] == val ) return;
    }

    mazeCandidates[ mazeCandidateCount ] = val;
    mazeCandidateCount = mazeCandidateCount + 1;
}

int mazeRemoveCandidate( int index )
{
    int tmp = mazeCandidates[ index ];
    mazeCandidates[ index ] = mazeCandidates[ mazeCandidateCount - 1 ];
    mazeCandidateCount = mazeCandidateCount - 1;
    return tmp;
}

void mazeMoveToCell( int row, int col )
{
    mazeGridSet( mazeCells, row, col, false );

    if( col < mazeWidth - 2 && mazeIsSet( mazeCells, row, col + 1 ) )
      mazeAddCandidate( ( row << 8 ) + ( col + 1 ) );
    if( col > 0 && mazeIsSet( mazeCells, row, col - 1 ) )
      mazeAddCandidate( ( row << 8 ) + ( col - 1 ) );
    if( row < mazeHeight - 2 && mazeIsSet( mazeCells, row + 1, col ) )
      mazeAddCandidate( ( ( row + 1 ) << 8 ) + col );
    if( row > 0 && mazeIsSet( mazeCells, row - 1, col ) )
      mazeAddCandidate( ( ( row - 1 ) << 8 ) + col );
}

void mazeGenerateMaze( int w, int h )
{
    mazeWidth = w;
    mazeHeight = h;

    int i;
    for( i = 0; i < MAZE_BIT_ARRAY_SIZE; i++ ) mazeHWalls[ i ] = 255;
    for( i = 0; i < MAZE_BIT_ARRAY_SIZE; i++ ) mazeVWalls[ i ] = 255;
    for( i = 0; i < MAZE_BIT_ARRAY_SIZE; i++ ) mazeCells[ i ] = 255;

    mazeCandidateCount = 0;
    mazeMoveToCell( 0, 0 );

    while( mazeCandidateCount > 0 )
    {
        int randIndex = arand( mazeCandidateCount );
        int choice = mazeRemoveCandidate( randIndex );
        int row = ( choice >> 8 ) & 255;
        int col = choice & 255;

        bool foundWall = false;
        while( !foundWall )
        {
            int dir = arand( 4 );

            if( dir == 0 )
            {
                if( col > 0 && !mazeIsSet( mazeCells, row, col - 1 ) )
                {
                    mazeGridSet( mazeVWalls, row, col, false );
                    foundWall = true;
                }
            }
            else if( dir == 1 )
            {
                if( col < mazeWidth - 2 && !mazeIsSet( mazeCells, row, col + 1 ) )
                {
                    mazeGridSet( mazeVWalls, row, col + 1, false );
                    foundWall = true;
                }
            }
            else if( dir == 2 )
            {
                if( row > 0 && !mazeIsSet( mazeCells, row - 1, col ) )
                {
                    mazeGridSet( mazeHWalls, row, col, false );
                    foundWall = true;
                }
            }
            else if( dir == 3 )
            {
                if( row < mazeHeight - 2 && !mazeIsSet( mazeCells, row + 1, col ) )
                {
                    mazeGridSet( mazeHWalls, row + 1, col, false );
                    foundWall = true;
                }
            }
        }

        mazeMoveToCell( row, col );

        if( mazeCandidateCount == 0 )
        {
            int r;
            int c;
            for( r = 0; r < mazeHeight - 1; r++ )
            {
                for( c = 0; c < mazeWidth - 1; c++ )
                {
                    if( !mazeIsSet( mazeCells, r, c ) ) mazeMoveToCell( r, c );
                }
            }
        }
    }

    mazeGridSet( mazeHWalls, 0, 0, false );
    mazeGridSet( mazeHWalls, mazeHeight - 1, mazeWidth - 2, false );
    mazePosX = 0;
    mazePosY = 0;
    mazeMovingX = 0;
    mazeMovingY = 0;
    // See this file's own header comment on the (row, col) argument order
    // typo silently normalized here.
    mazeGridSet( mazeCells, mazePosY, mazePosX, true );
}

void mazeHandleMovement()
{
    if( gbRepeat( BTN_UP, 1 ) && mazePosY > 0 && !mazeIsSet( mazeHWalls, mazePosY, mazePosX ) )
    {
        if( mazeIsSet( mazeCells, mazePosY - 1, mazePosX ) ) mazeGridSet( mazeCells, mazePosY, mazePosX, false );
        mazePosY = mazePosY - 1;
        mazeMovingY = -6;
        mazeGridSet( mazeCells, mazePosY, mazePosX, true );
    }
    else if( gbRepeat( BTN_DOWN, 1 ) && mazePosY < mazeHeight - 2 && !mazeIsSet( mazeHWalls, mazePosY + 1, mazePosX ) )
    {
        if( mazeIsSet( mazeCells, mazePosY + 1, mazePosX ) ) mazeGridSet( mazeCells, mazePosY, mazePosX, false );
        mazePosY = mazePosY + 1;
        mazeMovingY = 6;
        mazeGridSet( mazeCells, mazePosY, mazePosX, true );
    }
    else if( gbRepeat( BTN_LEFT, 1 ) && mazePosX > 0 && !mazeIsSet( mazeVWalls, mazePosY, mazePosX ) )
    {
        if( mazeIsSet( mazeCells, mazePosY, mazePosX - 1 ) ) mazeGridSet( mazeCells, mazePosY, mazePosX, false );
        mazePosX = mazePosX - 1;
        mazeMovingX = -6;
        mazeGridSet( mazeCells, mazePosY, mazePosX, true );
    }
    else if( gbRepeat( BTN_RIGHT, 1 ) && mazePosX < mazeWidth - 2 && !mazeIsSet( mazeVWalls, mazePosY, mazePosX + 1 ) )
    {
        if( mazeIsSet( mazeCells, mazePosY, mazePosX + 1 ) ) mazeGridSet( mazeCells, mazePosY, mazePosX, false );
        mazePosX = mazePosX + 1;
        mazeMovingX = 6;
        mazeGridSet( mazeCells, mazePosY, mazePosX, true );
    }
}

void mazeDrawMaze()
{
    gbSetColor( 1 );

    int rowStart = 0;
    if( mazePosY >= 6 ) rowStart = mazePosY - 6;
    int colStart = 0;
    if( mazePosX >= 9 ) colStart = mazePosX - 9;

    int row;
    int col;
    for( row = rowStart; row < mazeHeight && row < mazePosY + 6; row++ )
    {
        for( col = colStart; col < mazeWidth && col < mazePosX + 9; col++ )
        {
            int baseX = ( col - mazePosX ) * 6 + 38 + mazeMovingX;
            int baseY = ( row - mazePosY ) * 6 + 20 + mazeMovingY;

            if( col < mazeWidth - 1 )
            {
                if( mazeIsSet( mazeHWalls, row, col ) )
                  gbDrawFastHLine( baseX, baseY, 7 );

                if( row < mazeHeight - 1 )
                {
                    if( mazeIsSet( mazeCells, row, col ) )
                    {
                        gbFillRect( baseX + 2, baseY + 2, 3, 3 );
                        if( row > 0 && mazeIsSet( mazeCells, row - 1, col ) && !mazeIsSet( mazeHWalls, row, col ) )
                          gbFillRect( baseX + 2, baseY - 1, 3, 3 );
                        if( col > 0 && mazeIsSet( mazeCells, row, col - 1 ) && !mazeIsSet( mazeVWalls, row, col ) )
                          gbFillRect( baseX - 1, baseY + 2, 3, 3 );
                    }
                }
            }

            if( row < mazeHeight - 1 )
            {
                if( mazeIsSet( mazeVWalls, row, col ) )
                  gbDrawFastVLine( baseX, baseY, 7 );
            }
        }
    }

    if( mazeMovingX > 0 )
    {
        if( mazePosX > 0 && mazeIsSet( mazeCells, mazePosY, mazePosX - 1 ) )
        {
            gbSetColor( 0 );
            gbFillRect( 43, 22, mazeMovingX, 3 );
        }
        else
        {
            gbSetColor( 1 );
            gbFillRect( 40, 22, mazeMovingX, 3 );
        }
    }
    if( mazeMovingX < 0 )
    {
        if( mazePosX < mazeWidth - 2 && mazeIsSet( mazeCells, mazePosY, mazePosX + 1 ) )
        {
            gbSetColor( 0 );
            gbFillRect( 40 + mazeMovingX, 22, -mazeMovingX, 3 );
        }
        else
        {
            gbSetColor( 1 );
            gbFillRect( 43 + mazeMovingX, 22, -mazeMovingX, 3 );
        }
    }

    if( mazeMovingY > 0 )
    {
        if( mazePosY > 0 && mazeIsSet( mazeCells, mazePosY - 1, mazePosX ) )
        {
            gbSetColor( 0 );
            gbFillRect( 40, 25, 3, mazeMovingY );
        }
        else
        {
            gbSetColor( 1 );
            gbFillRect( 40, 22, 3, mazeMovingY );
        }
    }
    if( mazeMovingY < 0 )
    {
        if( mazePosY < mazeHeight - 2 && mazeIsSet( mazeCells, mazePosY + 1, mazePosX ) )
        {
            gbSetColor( 0 );
            gbFillRect( 40, 22 + mazeMovingY, 3, -mazeMovingY );
        }
        else
        {
            gbSetColor( 1 );
            gbFillRect( 40, 25 + mazeMovingY, 3, -mazeMovingY );
        }
    }
}

void mazeBeginTitle()
{
    mazeState = MAZE_STATE_TITLE;
}

void mazeBeginPlay()
{
    gbPickRandomSeed();
    mazeGenerateMaze( 10, 10 );
    mazeState = MAZE_STATE_PLAY;
}

void mazeUpdateTitle()
{
    gbSetColor( 1 );

    // Real logo bitmap is 64x36 - centered horizontally on the 84px-wide
    // display ((84 - 64) / 2 = 10), placed near the top so "PRESS A" still
    // fits comfortably below it (see this file's own header comment on why
    // this differs from real titleScreen()'s own fixed x=0 placement).
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, mazeLogoBitmap );

    gbFontSize = 1;
    gbCursorX = 14;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      mazeBeginPlay();
}

void mazeUpdatePlay()
{
    // Button C fully restarts (a fresh random 10x10 maze once the title
    // screen is dismissed again) - see this file's own header comment on
    // why the real blocking gb.titleScreen()/reset() combo became an
    // explicit state re-entry here instead.
    if( gbPressed( BTN_C ) )
    {
        mazeBeginTitle();
        return;
    }

    if( mazeMovingX == 0 && mazeMovingY == 0 )
    {
        if( mazePosX == mazeWidth - 2 && mazePosY == mazeHeight - 2 )
        {
            gbPopup( "     Good job!", 60 );
            gbPlayOK();

            int newW = mazeWidth + 10;
            if( newW > MAZE_MAX_WIDTH ) newW = MAZE_MAX_WIDTH;
            int newH = mazeHeight + 10;
            if( newH > MAZE_MAX_HEIGHT ) newH = MAZE_MAX_HEIGHT;
            mazeGenerateMaze( newW, newH );
        }

        mazeHandleMovement();
    }

    mazeDrawMaze();
    // real gb.popup()'s own overlay is drawn automatically by
    // gbRenderFrame() below - see header comment.

    if( mazeMovingX < 0 ) mazeMovingX = mazeMovingX + 1;
    if( mazeMovingY < 0 ) mazeMovingY = mazeMovingY + 1;
    if( mazeMovingX > 0 ) mazeMovingX = mazeMovingX - 1;
    if( mazeMovingY > 0 ) mazeMovingY = mazeMovingY - 1;
}

void gameMaze_init()
{
    gbBegin();
    mazeBeginTitle();
}

void gameMaze_update()
{
    if( !gbUpdate() ) return;

    if( mazeState == MAZE_STATE_TITLE ) mazeUpdateTitle();
    else mazeUpdatePlay();

    gbRenderFrame();
}
