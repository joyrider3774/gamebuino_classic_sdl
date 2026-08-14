#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// DeathMaze (msevilgenius, license: none specified - recovered via direct
// download, no live GitHub repo for this specific game; the same author's
// Gamebuino-SuperSpaceShooter is already ported into the sibling
// gamebuino_classic_vircon32 cartridge as gameSuperSpaceShooter.c). A tiny
// one-way maze runner: the player only ever moves right (plus up/down to
// dodge into gaps), racing to cross a randomly-generated 16-column wall
// maze before a real-time-decaying score bottoms out, with a real
// EEPROM-backed high score.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameDeathMaze.c (which already did the real upstream-Arduino-
// to-Vircon32-dialect porting work and documents every real upstream quirk
// preserved/normalized in its own header comment - see that file for the
// full write-up, none of which needed revisiting here) - converting
// Vircon32's own C dialect back to standard C:
//  - `int[DMAZE_HEIGHT] dmazeMap;` -> `int dmazeMap[DMAZE_HEIGHT];` and
//    `int[226] dmazeLogoBitmap = {...};` -> `int
//    dmazeLogoBitmap[226] = {...};` (array-declaration order only - the
//    226-entry logo bitmap's own real byte data was copied directly from
//    the source file, not retyped from memory, and diff-verified
//    byte-for-byte against it afterward).
// No other change was needed - every text string here is already a plain
// C string literal, `eeprom_read_byte()`/`eeprom_write_byte()` already
// match this project's own eepromShim.h signatures unchanged, and this
// project's own eepromSelectGame() (gamesMain.c) already resolves the
// right on-disk slot before this game's own init() ever runs.

#define DMAZE_WIDTH 16
#define DMAZE_HEIGHT 18
#define DMAZE_PENALTY 500

#define DMAZE_STATE_TITLE 0
#define DMAZE_STATE_PLAY 1
#define DMAZE_STATE_PAUSED 2

int dmazeState = DMAZE_STATE_TITLE;

int dmazePlayerX = 0;
int dmazePlayerY = 0;

int dmazeScore = 10000;
int dmazeBestScore = 0;
bool dmazeFinished = false;

int dmazeMoving = 0;

int dmazeMap[ DMAZE_HEIGHT ];

// -----------------------------------------------------------------------------
// Real upstream title-screen logo (64x28), shown via the real, blocking
// `gb.titleScreen(F(" msevilgenius's"), logo)` - see this file's own header
// comment for the byte-format/conversion notes.
// -----------------------------------------------------------------------------
int dmazeLogoBitmap[ 226 ] = {
    64, 28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    63, 15, 193, 31, 236, 49, 255, 252, 49, 140, 3, 131, 12, 49, 85, 84,
    48, 204, 2, 131, 12, 49, 21, 84, 48, 204, 6, 195, 12, 49, 80, 84,
    48, 207, 134, 195, 15, 241, 85, 84, 48, 204, 6, 195, 12, 49, 21, 84,
    48, 204, 15, 227, 12, 49, 69, 84, 48, 204, 12, 99, 12, 49, 85, 68,
    49, 140, 12, 99, 12, 49, 85, 20, 63, 15, 216, 51, 12, 49, 255, 252,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 224, 112, 64, 252, 252, 0,
    0, 0, 224, 112, 224, 12, 192, 0, 0, 0, 240, 240, 160, 24, 192, 0,
    0, 0, 208, 177, 176, 24, 192, 0, 0, 0, 208, 177, 176, 48, 248, 0,
    0, 0, 217, 49, 176, 48, 192, 0, 0, 0, 201, 51, 248, 96, 192, 0,
    0, 0, 207, 51, 24, 224, 192, 0, 0, 0, 198, 51, 24, 192, 192, 0,
    0, 0, 198, 54, 13, 252, 252, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// == real drawMaze() ==
void dmazeDrawMaze()
{
    gbSetColor( GB_BLACK );
    gbDrawRect( 0, 0, 76, 40 );
    gbDrawRect( 1, 1, 74, 38 );

    for( int y = 0; y < DMAZE_HEIGHT; y = y + 1 )
    {
        for( int x = 0; x < DMAZE_WIDTH; x = x + 1 )
        {
            if( ( dmazeMap[ y ] >> ( DMAZE_WIDTH - ( x + 1 ) ) ) & 1 )
              gbDrawRect( ( x + 1 ) * 4, ( y + 1 ) * 2, 2, 2 );
        }
    }
}

// == real drawScores() ==
void dmazeDrawScores()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 3;
    gbCursorY = LCDHEIGHT - 6;
    gbPrintString( "SCORE:" );
    gbPrintNumber( dmazeScore );
    gbPrintString( " BEST:" );
    gbPrintNumber( dmazeBestScore );
}

// == real drawPlayer(dontFlash) ==
void dmazeDrawPlayer( bool dontFlash )
{
    int x = dmazePlayerX * 2 + 2;
    int y = dmazePlayerY * 2 + 2;

    if( dontFlash || ( ( gbFrameCount / 2 ) % 2 ) )
    {
        gbSetColor( GB_BLACK );
        gbDrawRect( x, y, 2, 2 );
    }
}

// == real testClear(x,y) ==
bool dmazeTestClear( int x, int y )
{
    return !( ( dmazeMap[ y ] >> ( DMAZE_WIDTH - ( x + 1 ) ) ) & 1 );
}

// == real randomiseMaze() ==
void dmazeRandomiseMaze()
{
    // clear the map (make all the walls solid with no gaps)
    for( int i = 0; i < DMAZE_HEIGHT; i = i + 1 )
      dmazeMap[ i ] = 0xFFFF;

    // work across adding gaps as we go
    for( int i = DMAZE_WIDTH - 1; i >= 0; i = i - 1 )
    {
        int noOfGaps;

        // how many gaps in the column?
        int roll = arand( 10 );
        if( roll == 9 )
          noOfGaps = 3;
        else if( roll == 8 || roll == 7 )
          noOfGaps = 2;
        else
          noOfGaps = 1;

        // real upstream's own comment, verbatim: "excuse my crazy method
        // for this. the first line is a little confusing even to me. It
        // makes a number like: 1111111111011111 with the 0 corresponding
        // to the column we are modifying, then in the loop we bitwise AND
        // that with a row in the maze to create a gap in the wall"
        int row = ~( 1 << i );
        for( int j = 0; j < noOfGaps; j = j + 1 )
        {
            int gapRow = arand( DMAZE_HEIGHT );
            dmazeMap[ gapRow ] = dmazeMap[ gapRow ] & row;
        }
    }
}

// == real readBest() ==
int dmazeReadBest()
{
    int a = eeprom_read_byte( 0 );
    int b = eeprom_read_byte( 1 );
    int written = eeprom_read_byte( 2 );

    // does the eeprom contain a score?
    if( written != 42 )
      return 0;

    return b | ( a << 8 );
}

// == real writeBest(best) ==
void dmazeWriteBest( int best )
{
    eeprom_write_byte( 0, ( best >> 8 ) & 0xFF );
    eeprom_write_byte( 1, best & 0xFF );
    // flag for testing if score exists
    eeprom_write_byte( 2, 42 );
}

// == real reset() ==
void dmazeReset()
{
    dmazeRandomiseMaze();
    dmazePlayerX = 0;
    dmazePlayerY = DMAZE_HEIGHT / 2;
    dmazeScore = 10000;
    dmazeBestScore = dmazeReadBest();
    dmazeFinished = false;
}

void dmazeBeginPlay()
{
    dmazeState = DMAZE_STATE_PLAY;
}

void dmazeBeginPaused()
{
    dmazeState = DMAZE_STATE_PAUSED;
}

// Shared real titleScreen(F(" msevilgenius's"), logo) layout, reused by
// both real call sites - see this file's own header comment for why the
// anchors match gameSuperSpaceShooter.c's own already-proven real 64x28
// title-screen recreation exactly (same author, same real logo size).
void dmazeDrawTitleScreen()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, dmazeLogoBitmap );
    gbCursorX = 1;
    gbCursorY = 32;
    gbPrintString( " msevilgenius's" );
    gbCursorX = 1;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );
}

// == real setup()'s own gb.titleScreen(...) call, boot case - dismissing
// runs the real reset() that followed it in setup() ==
void dmazeUpdateTitle()
{
    dmazeDrawTitleScreen();

    if( gbPressed( BTN_A ) )
    {
        dmazeReset();
        dmazeBeginPlay();
    }
}

// == real loop()'s own mid-game gb.titleScreen(...) call (Button C) - a
// real pause screen, dismissing resumes play with no reset at all ==
void dmazeUpdatePaused()
{
    dmazeDrawTitleScreen();

    if( gbPressed( BTN_A ) )
      dmazeBeginPlay();
}

void dmazeUpdatePlay()
{
    if( dmazeMoving )
      dmazeMoving = dmazeMoving - 1;

    // have we finished the maze?
    if( !dmazeFinished && dmazePlayerX >= DMAZE_WIDTH * 2 )
    {
        dmazeFinished = true;
        if( dmazeScore > dmazeBestScore )
        {
            dmazeBestScore = dmazeScore;
            dmazeWriteBest( dmazeBestScore );
        }
    }

    // C to return to (pause at) the title screen - real upstream's own
    // blocking titleScreen() call means nothing below this runs on the
    // same tick the real call would have taken over
    if( gbPressed( BTN_C ) )
    {
        dmazeBeginPaused();
        return;
    }

    // B to reset anytime
    if( gbPressed( BTN_B ) )
      dmazeReset();

    // press a button to reset when finished
    if( dmazeFinished && gbPressed( BTN_A ) )
      dmazeReset();

    if( dmazeScore > -10000 && !dmazeFinished )
      dmazeScore = dmazeScore - 5; // decrease score every frame

    // movement (only if we haven't got to the end)
    if( !dmazeFinished )
    {
        if( gbPressed( BTN_UP ) )
        {
            dmazeMoving = 6;
            if( dmazePlayerY > 0 && ( !( dmazePlayerX % 2 ) || dmazeTestClear( ( dmazePlayerX - 1 ) / 2, dmazePlayerY - 1 ) ) )
              dmazePlayerY = dmazePlayerY - 1;
            else
              dmazeScore = dmazeScore - DMAZE_PENALTY;
        }
        if( gbPressed( BTN_DOWN ) )
        {
            dmazeMoving = 6;
            if( dmazePlayerY + 1 < DMAZE_HEIGHT && ( !( dmazePlayerX % 2 ) || dmazeTestClear( ( dmazePlayerX - 1 ) / 2, dmazePlayerY + 1 ) ) )
              dmazePlayerY = dmazePlayerY + 1;
            else
              dmazeScore = dmazeScore - DMAZE_PENALTY;
        }
        if( gbPressed( BTN_RIGHT ) )
        {
            dmazeMoving = 6;
            if( ( dmazePlayerX % 2 ) || dmazeTestClear( dmazePlayerX / 2, dmazePlayerY ) )
              dmazePlayerX = dmazePlayerX + 1;
            else
              dmazeScore = dmazeScore - DMAZE_PENALTY;
        }
    }

    // drawing functions
    dmazeDrawMaze();
    dmazeDrawScores();
    dmazeDrawPlayer( dmazeMoving || dmazeFinished );
}

void gameDeathMaze_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 ); // matches real upstream's own gb.display.setFont(font3x5) - already this shim's own default too
    gbPickRandomSeed(); // documented no-op - see gamebuinoShim.h
    dmazeState = DMAZE_STATE_TITLE;
}

void gameDeathMaze_update()
{
    if( !gbUpdate() ) return;

    if( dmazeState == DMAZE_STATE_TITLE )
      dmazeUpdateTitle();
    else if( dmazeState == DMAZE_STATE_PAUSED )
      dmazeUpdatePaused();
    else
      dmazeUpdatePlay();

    gbRenderFrame();
}
