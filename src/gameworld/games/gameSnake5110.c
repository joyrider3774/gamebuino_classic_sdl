#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Gamebuino Classic Snake 5110 (Lady Awesome & MakerSquirrel, CC-BY-SA 2018
// per the source file's own header comment - though the repo's own
// top-level LICENSE file is GPLv3, a real, unresolved conflict between the
// two not resolved here; original Highscore code by Rodot). A real Snake
// game with two real distinct modes: a faithful classic mode (solid outer
// walls, single food type, dying on any wall/self touch) and a "new" mode
// with a real, distinctive mechanic this catalog has nothing else like -
// the outer wall is made of removable segments that get torn down one at a
// time as the snake eats special "wall" prey, letting the snake wrap
// through the resulting gaps to the opposite edge, alongside two more
// special prey types (grow instantly by 3, or shrink by 4). Confirmed via
// direct diff to be a genuinely different codebase from this project's
// already-shipped Snake Classic (Ripper121/Tnxec2's single-file procedural
// port) - class-based (Coordinate/Snake structs across separate .h/.cpp
// files), different author, different mechanics entirely, not a
// duplicate.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment). Upstream's own `Coordinate` class (an
// x/y pair with real member functions for arena bounds/pixel conversion/
// movement) and `Snake` struct (a fixed `Coordinate[220]` array + a size
// counter, with real member functions for add/shrink/move/random-free-pos)
// were both flattened into plain `snk`-prefixed globals and free functions,
// matching this project's own established "flatten a single-instance C++
// class into plain C globals" treatment (see gamePunkt.c's own header
// comment for the general reasoning) - the snake's own coordinate array
// became two parallel `int[]` arrays (`snkSnakeX`/`snkSnakeY`), the same
// "parallel arrays for many small game objects" idiom this project already
// uses elsewhere (gamePunkt.c's own enemy arrays). Sized 221, not 220 (real
// `g_arenaSize`, 20x11): upstream's own real `moveCoordinates()` loop
// writes into `m_snakeCoordinates[size]` - one slot past the last valid
// index - on its very first iteration whenever growth pushes `size` up to
// its own real maximum of 220, a genuine (if practically unreachable -
// filling the entire 220-cell arena would end the game via collision long
// before this could happen) upstream off-by-one; one extra slot of
// headroom here costs nothing and avoids leaning on this dialect's own
// array bounds behavior matching that access at all.
//
// TWO REAL SIMPLIFICATIONS, both matching an already-established precedent
// elsewhere in this project rather than being new judgment calls:
// - **`gb.menu(mainMenu, MAINMENU_LENGTH)`** (a real, generic Gamebuino
//   list-selection widget) has no equivalent in this shim - hand-rolled as
//   `snkUpdateMenu()` instead, the same "bespoke replacement for upstream's
//   own binary gb.menu() widget" treatment `gameConduit.c`'s own
//   `condUpdateMenu()` already established for the identical real gap. Its
//   own "Start Classic" label is shown as "Classic Mode" (same meaning,
//   one real font5x7 char shorter) - the literal upstream text overflows
//   LCDWIDTH by a few pixels at this cursorX, the same real "shorten to
//   fit this dialect's own font metrics" adjustment several other ported
//   games' own menus already needed.
// - **Highscore name entry** (`gb.getDefaultName()`/`gb.keyboard()`, a
//   real on-screen letter-by-letter name entry keyboard) - dropped
//   outright, matching `gameArmageddon.c`'s own already-documented
//   "HIGHSCORE NAME ENTRY - DROPPED, DOCUMENTED" precedent (no keyboard
//   widget exists in this shim at all). This port's own highscore tables
//   persist scores only, not names - `snkSaveHighscore()`/
//   `snkDrawHighscores()` below are simplified accordingly (real
//   upstream's own per-entry EEPROM layout interleaves 10 name bytes with
//   each 2-byte score; this port's own layout is just 5 consecutive
//   2-byte scores per mode, a genuinely different EEPROM layout since
//   there's no name data left to interleave with).
//
// BLOCKING WHILE(true) LOOPS -> EXPLICIT STATES: upstream structures
// `gameLoop()`/`setDifficulty()`/`showCredits()`/`drawHighScores()`/
// `showScore()` each as their own real blocking `while(true) { if
// (!gb.update()) continue; ... }` loop, called from `loop()`'s own
// menu dispatch and returned from on a button press - the "blocking loop
// -> explicit resumable state" treatment used throughout this project
// (see gamePong.c's own header comment) turns each of these into one
// state in a single flat `snkState` machine
// (`SNK_STATE_TITLE`/`MENU`/`PLAYING`/`DIFFICULTY`/`HIGHSCORE`/`CREDITS`/
// `GAMEOVER_SCORE`), with `gameSnake5110_update()` dispatching to exactly
// one `snkUpdateXxx()` per real tick. Real `showHighscore(true);
// showHighscore(false);` (the menu's own "Show Highscore" option, two
// real sequential blocking calls, classic table shown first) became one
// `SNK_STATE_HIGHSCORE` shown twice in a row via `snkHighscoreShowBoth`
// (set only from the menu's own path, not from `gameOver()`'s own single-
// table `showHighscore(g_isClassicMode)` call, which shows just the
// mode that was actually being played).
//
// REAL UPSTREAM QUIRKS PRESERVED, NOT "FIXED" (this project's own norm -
// preserve a real bug unless told otherwise):
// 1. `Coordinate::setOffBounds()` upstream reads `{ m_x = -2; m_y -2; }` -
//    a real typo (missing `=`) that makes the real `m_y` assignment a
//    no-op discarded expression, only `m_x` actually changes. Harmless in
//    practice (`isInArena()` already returns false from `m_x=-2` alone
//    regardless of `m_y`), but ported bit-for-bit anyway - `snkSetOffBounds()`
//    below only ever sets the X coordinate too.
// 2. `deleteRandomWallElement()`'s own real "last wall on this axis"
//    special case (`if (g_xWallsRemaining==0 && g_yWallsRemaining==1) { for
//    (...) { if (!g_yWallsRemoved[i]) continue; g_yWallsRemoved[i]=true;
//    g_yWallsRemaining=0; return; } }` and the mirrored X case) has its
//    own real inverted condition - it only actually fires its body on an
//    ALREADY-removed wall slot (a guaranteed no-op re-assignment), so
//    `g_yWallsRemaining`/`g_xWallsRemaining` gets reset to 0 without the
//    real final wall segment on that axis ever actually being marked
//    removed. A real, functional gameplay quirk (New mode's outer wall
//    can never be fully cleared on either axis - one segment per axis
//    always remains standing), reproduced exactly in `snkDeleteRandomWallElement()`
//    below, not corrected into "working" behavior upstream never shipped.
//
// Real `byte`/`int8_t`/`int16_t`/`uint8_t` fields all become plain `int`
// (this dialect has no narrower native types - see gameMaze.c's own header
// comment on why "just use int" costs nothing here); every value here
// stays safely within 32-bit range (raster coordinates 0-20, scores well
// under a few thousand), so no narrow-int-wraparound risk exists to
// preserve or guard against either way.
//
// FRAME COUNTER / RANDOM: `gb.pickRandomSeed()` -> `gbPickRandomSeed()`
// (a documented no-op); `random(a,b)` -> `a + arand(b-a)`, this dialect's
// own established ranged-random idiom.
//
// REAL BITMAP ART RESTORED: the real 64x36 `Snake5110Logo` title bitmap
// was extracted byte-for-byte from the real source (every `0x`-hex literal
// already matched this dialect's own syntax directly, no binary-literal
// conversion needed this time) and is drawn via `gbDrawBitmap()` at
// upstream's own real `titleScreen()` anchor (0,12) - see gamePong.c's/
// gameArmageddon.c's own header comments for why (0,12) is titleScreen()'s
// own real fixed logo position and why a 36px-tall logo lands exactly on
// LCDHEIGHT (48) with zero clipping.
//
// SOUND - APPROXIMATED, DOCUMENTED (an already out-of-scope area, not a
// new shim gap): upstream's own two `gb.sound.playNote(pitch, duration,
// 0)` calls (eating prey, dying) keep their real pitch/duration pair via
// `gbPlayNote()` directly - the same "approximate with the closest one-
// shot primitive, keep whatever real parameters do carry over" treatment
// gameUfoRace.c's/gameArmageddon.c's own sound already established.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameSnake5110.c (which already did the real upstream-Arduino-
// to-Vircon32-dialect porting work and documents every real upstream quirk
// preserved/normalized in its own header comment above - none of which
// needed revisiting here) - converting Vircon32's own C dialect back to
// standard C:
//  - `int[290] snakeLogoBitmap` (array-declaration-order only) is now
//    `int snakeLogoBitmap[290]` (the 290-entry logo bitmap's own real byte
//    data was copied directly from the source file, not retyped from
//    memory, and diff-verified byte-for-byte against it afterward).
//  - `int[221] snkSnakeX;` / `int[221] snkSnakeY;` -> `int snkSnakeX[221];` /
//    `int snkSnakeY[221];` (array-declaration order only, for the two
//    flattened Snake-coordinate parallel arrays).
//  - `int[21] snkXWallsRemoved;` / `int[12] snkYWallsRemoved;` ->
//    `int snkXWallsRemoved[21];` / `int snkYWallsRemoved[12];`.
//  - `int[5] snkHighscoreNew;` / `int[5] snkHighscoreClassic;` ->
//    `int snkHighscoreNew[5];` / `int snkHighscoreClassic[5];`.
// No struct conversions were needed - the Vircon32 source has no `struct`
// declarations left in it at all (Coordinate/Snake were already flattened
// into plain globals/parallel arrays during the original Arduino->Vircon32
// port, per this file's own header comment above). Every text string here
// is already a plain C string literal, `eeprom_read_word()`/
// `eeprom_write_word()` already match this project's own eepromShim.h
// signatures unchanged, and this project's own eepromSelectGame()
// (gamesMain.c) already resolves the right on-disk slot before this game's
// own init() ever runs.

#define SNK_RASTER_X 20
#define SNK_RASTER_Y 11
#define SNK_MAX_LEVEL 9
#define SNK_ARENA_SIZE 220
#define SNK_HIGHSCORE_COUNT 5

#define SNK_UP 0
#define SNK_RIGHT 1
#define SNK_DOWN 2
#define SNK_LEFT 3
#define SNK_PAUSE -1

#define SNK_STATE_TITLE 0
#define SNK_STATE_MENU 1
#define SNK_STATE_PLAYING 2
#define SNK_STATE_DIFFICULTY 3
#define SNK_STATE_HIGHSCORE 4
#define SNK_STATE_CREDITS 5
#define SNK_STATE_GAMEOVER_SCORE 6

int snakeLogoBitmap[ 290 ] = { 64, 36,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xFE, 0x47, 0xF2, 0xF, 0xE1, 0xC0, 0x8F, 0xE4,
    0xFE, 0xA7, 0xF5, 0xF, 0xE1, 0xC1, 0x4F, 0xEA,
    0xFE, 0x47, 0xF2, 0xF, 0xE1, 0xC0, 0x8F, 0xE4,
    0xE0, 0x7, 0x70, 0x3E, 0xF9, 0xC0, 0xE, 0x0,
    0xE0, 0x7, 0x77, 0x3E, 0xF9, 0xDF, 0xCE, 0x0,
    0xE0, 0x7, 0x77, 0x3E, 0xF9, 0xDF, 0xCE, 0x0,
    0xE0, 0x7, 0x77, 0x38, 0x39, 0xDF, 0xCE, 0x0,
    0xE0, 0x7, 0x77, 0x3F, 0xB9, 0xFC, 0xF, 0xFE,
    0xE0, 0x7, 0x77, 0x3F, 0xB9, 0xFC, 0xF, 0xFE,
    0xFF, 0xE7, 0x77, 0x3F, 0xB9, 0xFC, 0xF, 0xFE,
    0xFF, 0xE7, 0x77, 0x3, 0xB8, 0x0, 0x0, 0xE,
    0xFF, 0xE7, 0x77, 0x3F, 0xB9, 0xFC, 0xF, 0xFE,
    0x0, 0xE7, 0x77, 0x3F, 0xB9, 0xFC, 0xF, 0xFE,
    0x0, 0xE7, 0x77, 0x3F, 0xB9, 0xFC, 0xF, 0xFE,
    0x0, 0xE7, 0x77, 0x38, 0x39, 0xDF, 0xCE, 0x0,
    0x0, 0xE7, 0x77, 0x38, 0x39, 0xDF, 0xCE, 0x0,
    0x0, 0xE7, 0x77, 0x38, 0x39, 0xDF, 0xCE, 0x0,
    0x0, 0xE7, 0x77, 0x0, 0x38, 0x1, 0xCE, 0x0,
    0xFF, 0xE7, 0x7F, 0x10, 0x38, 0x81, 0xCF, 0xFE,
    0xFF, 0xE7, 0x7F, 0x28, 0x39, 0x41, 0xCF, 0xFE,
    0xFF, 0xE7, 0x7F, 0x10, 0x38, 0x81, 0xCF, 0xFE,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF2, 0x4E,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x86, 0xD1,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xE2, 0x51,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x51,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x51,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xE2, 0x4E,
};

// ---- flattened Snake/Coordinate state ----

int snkSnakeX[ 221 ];
int snkSnakeY[ 221 ];
int snkSnakeSize;

int snkHeadX, snkHeadY;
int snkNomX, snkNomY;
int snkGrowNomX, snkGrowNomY;
int snkShrinkNomX, snkShrinkNomY;
int snkWallNomX, snkWallNomY;

int snkNom;
int snkGrowNom;
int snkScore;
int snkGameLevel;
int snkDelayCounter;
int snkLastButtonPressed;
int snkLastTimeButtonPressed;
int snkLevelModuloHelper;
int snkIsClassicMode;

int snkXWallsRemoved[ 21 ];
int snkYWallsRemoved[ 12 ];
int snkXWallsRemaining;
int snkYWallsRemaining;

int snkHighscoreNew[ 5 ];
int snkHighscoreClassic[ 5 ];

int snkState;
int snkMenuIndex;
int snkHighscoreMode;      // which table SNK_STATE_HIGHSCORE is currently showing
int snkHighscoreShowBoth;  // 1 when reached from the menu's own "Show Highscore" (classic, then new); 0 from game over (current mode only)
int snkGotHighscore;
int snkWallCrash;

// ---- small helpers ----

int snkXPixel( int x ) { return x * 4 + 2; }
int snkYPixel( int y ) { return y * 4 + 2; }

int snkIsInArena( int x, int y )
{
    return ( x > -1 && x < SNK_RASTER_X && y > -1 && y < SNK_RASTER_Y );
}

void snkSetOffBounds( int* x, int* y )
{
    y = y; // unused - real upstream's own setOffBounds() typo, see header comment: only x is ever actually set
    *x = -2;
}

int snkIsXWallRemoved( int val )
{
    if( val < 0 || val > SNK_RASTER_X ) return 0;
    return snkXWallsRemoved[ val ];
}

int snkIsYWallRemoved( int val )
{
    if( val < 0 || val > SNK_RASTER_Y ) return 0;
    return snkYWallsRemoved[ val ];
}

int snkIsOutOfBounds( int x, int y )
{
    if( snkIsInArena( x, y ) ) return 0;
    if( snkIsClassicMode ) return 1;
    if( x < -1 || x > SNK_RASTER_X ) return 1;
    if( y < -1 || y > SNK_RASTER_Y ) return 1;
    if( snkIsXWallRemoved( x ) || snkIsYWallRemoved( y ) ) return 0;
    return 1;
}

// ---- Snake array operations ----

void snkAddCoordinate( int x, int y )
{
    if( !snkIsInArena( x, y ) || snkSnakeSize >= SNK_ARENA_SIZE ) return;
    snkSnakeX[ snkSnakeSize ] = x;
    snkSnakeY[ snkSnakeSize ] = y;
    snkSnakeSize++;
}

void snkResetSnake()
{
    int i;
    for( i = 0; i < snkSnakeSize; i++ )
    {
        snkSnakeX[ i ] = 0;
        snkSnakeY[ i ] = 0;
    }
    snkSnakeSize = 0;
}

int snkIsPartOfSnake( int x, int y, int checkHead )
{
    int index = 1;
    if( checkHead ) index = 0;
    for( ; index < snkSnakeSize; index++ )
    {
        if( snkSnakeX[ index ] == x && snkSnakeY[ index ] == y )
          return 1;
    }
    return 0;
}

void snkMoveCoordinates( int newHeadX, int newHeadY )
{
    int index;
    for( index = snkSnakeSize; index > 0; index-- )
    {
        snkSnakeX[ index ] = snkSnakeX[ index - 1 ];
        snkSnakeY[ index ] = snkSnakeY[ index - 1 ];
    }
    snkSnakeX[ 0 ] = newHeadX;
    snkSnakeY[ 0 ] = newHeadY;
}

void snkGetRandomFreePos( int* outX, int* outY )
{
    int nx, ny, inUse;
    inUse = 1;
    while( inUse )
    {
        nx = arand( SNK_RASTER_X );
        ny = arand( SNK_RASTER_Y );
        inUse = snkIsPartOfSnake( nx, ny, 1 );
        if( !inUse && snkIsInArena( snkNomX, snkNomY ) )
          inUse = ( nx == snkNomX && ny == snkNomY );
        if( !inUse && snkIsInArena( snkGrowNomX, snkGrowNomY ) )
          inUse = ( nx == snkGrowNomX && ny == snkGrowNomY );
        if( !inUse && snkIsInArena( snkShrinkNomX, snkShrinkNomY ) )
          inUse = ( nx == snkShrinkNomX && ny == snkShrinkNomY );
        if( !inUse && snkIsInArena( snkWallNomX, snkWallNomY ) )
          inUse = ( nx == snkWallNomX && ny == snkWallNomY );
    }
    *outX = nx;
    *outY = ny;
}

// ---- New-mode removable walls ----

void snkDeleteRandomWallElement()
{
    int i, rngX, rngY, removedX, removedY, inUse;

    if( snkXWallsRemaining == 0 && snkYWallsRemaining == 0 )
      return;

    if( snkXWallsRemaining == 0 && snkYWallsRemaining == 1 )
    {
        for( i = 0; i < SNK_RASTER_Y + 1; i++ )
        {
            if( !snkYWallsRemoved[ i ] ) continue;
            snkYWallsRemoved[ i ] = 1;
            snkYWallsRemaining = 0;
            return;
        }
    }
    if( snkXWallsRemaining == 1 && snkYWallsRemaining == 0 )
    {
        for( i = 0; i < SNK_RASTER_X + 1; i++ )
        {
            if( !snkXWallsRemoved[ i ] ) continue;
            snkXWallsRemoved[ i ] = 1;
            snkXWallsRemaining = 0;
            return;
        }
    }

    inUse = 1;
    while( inUse )
    {
        rngX = arand( SNK_RASTER_X + 1 );
        rngY = arand( SNK_RASTER_Y + 1 );
        removedY = snkYWallsRemoved[ rngY ];
        removedX = snkXWallsRemoved[ rngX ];
        if( !removedY )
        {
            inUse = 0;
            snkYWallsRemoved[ rngY ] = 1;
            snkYWallsRemaining--;
        }
        else if( !removedX )
        {
            inUse = 0;
            snkXWallsRemoved[ rngX ] = 1;
            snkXWallsRemaining--;
        }
    }
}

// ---- EEPROM (scores only - see header comment) ----

void snkLoadHighscores()
{
    int i, v;
    for( i = 0; i < SNK_HIGHSCORE_COUNT; i++ )
    {
        v = eeprom_read_word( i * 2 );
        if( v == 0xFFFF ) v = 0;
        snkHighscoreNew[ i ] = v;
        v = eeprom_read_word( 10 + i * 2 );
        if( v == 0xFFFF ) v = 0;
        snkHighscoreClassic[ i ] = v;
    }
}

void snkSaveHighscore( int useClassicMode )
{
    int* scores = snkHighscoreNew;
    int base = 0;
    int i;
    if( useClassicMode ) { scores = snkHighscoreClassic; base = 10; }

    scores[ SNK_HIGHSCORE_COUNT - 1 ] = snkScore;
    for( i = SNK_HIGHSCORE_COUNT - 1; i > 0; i-- )
    {
        int tmp;
        if( scores[ i - 1 ] >= scores[ i ] ) break;
        tmp = scores[ i - 1 ];
        scores[ i - 1 ] = scores[ i ];
        scores[ i ] = tmp;
    }
    for( i = 0; i < SNK_HIGHSCORE_COUNT; i++ )
      eeprom_write_word( base + i * 2, scores[ i ] );
}

// ---- init ----

void snkInitGame( int useClassicMode )
{
    snkIsClassicMode = useClassicMode;
    gbPickRandomSeed();

    if( useClassicMode )
    {
        snkResetSnake();
        snkHeadX = 8; snkHeadY = SNK_RASTER_Y - 1;
        snkAddCoordinate( snkHeadX, snkHeadY );
        snkAddCoordinate( 7, SNK_RASTER_Y - 1 );
        snkAddCoordinate( 6, SNK_RASTER_Y - 1 );
        snkAddCoordinate( 5, SNK_RASTER_Y - 1 );
        snkAddCoordinate( 4, SNK_RASTER_Y - 1 );
        snkAddCoordinate( 3, SNK_RASTER_Y - 1 );
        snkAddCoordinate( 2, SNK_RASTER_Y - 1 );
        snkAddCoordinate( 1, SNK_RASTER_Y - 1 );
        snkAddCoordinate( 0, SNK_RASTER_Y - 1 );
        snkNomX = SNK_RASTER_X / 2;
        snkNomY = SNK_RASTER_Y / 2;
        snkLastButtonPressed = SNK_RIGHT;
    }
    else
    {
        int i;
        snkResetSnake();
        snkHeadX = SNK_RASTER_X / 2; snkHeadY = SNK_RASTER_Y / 2;
        snkAddCoordinate( snkHeadX, snkHeadY );
        snkLastButtonPressed = SNK_PAUSE;
        snkGetRandomFreePos( &snkNomX, &snkNomY );
        for( i = 0; i <= SNK_RASTER_X; i++ ) snkXWallsRemoved[ i ] = 0;
        for( i = 0; i <= SNK_RASTER_Y; i++ ) snkYWallsRemoved[ i ] = 0;
        snkXWallsRemaining = SNK_RASTER_X + 1;
        snkYWallsRemaining = SNK_RASTER_Y + 1;
        snkSetOffBounds( &snkShrinkNomX, &snkShrinkNomY );
        snkSetOffBounds( &snkGrowNomX, &snkGrowNomY );
        snkSetOffBounds( &snkWallNomX, &snkWallNomY );
    }
    snkNom = 0;
    snkGrowNom = 0;
    snkScore = 0;
    snkLevelModuloHelper = SNK_MAX_LEVEL + 1 - snkGameLevel;
    snkDelayCounter = 0;
    snkLastTimeButtonPressed = SNK_PAUSE;
}

// ---- drawing ----

void snkDrawArena()
{
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );
    if( snkIsClassicMode ) return;

    gbSetColorBg( 0, 0 );
    if( snkXWallsRemoved[ 0 ] )
    {
        gbFillRect( 0, 0, 5, 2 );
        gbFillRect( 0, LCDHEIGHT - 1, 5, 2 );
    }
    if( snkYWallsRemoved[ 0 ] )
    {
        gbFillRect( 0, 0, 2, 5 );
        gbFillRect( LCDWIDTH - 1, 0, 2, 5 );
    }
    int i;
    for( i = 1; i < SNK_RASTER_X; i++ )
    {
        if( !snkXWallsRemoved[ i ] ) continue;
        gbFillRect( i * 4 + 1, 0, 5, 2 );
        gbFillRect( i * 4 + 1, LCDHEIGHT - 1, 5, 2 );
    }
    for( i = 1; i < SNK_RASTER_Y; i++ )
    {
        if( !snkYWallsRemoved[ i ] ) continue;
        gbFillRect( 0, i * 4 + 1, 2, 5 );
        gbFillRect( LCDWIDTH - 1, i * 4 + 1, 2, 5 );
    }
    gbSetColorBg( 1, 0 );
    if( snkLastButtonPressed == SNK_PAUSE )
    {
        gbCursorX = 2;
        gbCursorY = 2;
        gbPrintNumber( snkScore );
    }
}

void snkDrawSnake( int direction, int timeChanged )
{
    if( timeChanged )
    {
        int hasMoved;
        int wasOut = snkIsOutOfBounds( snkHeadX, snkHeadY );
        int wasIn = snkIsInArena( snkHeadX, snkHeadY );
        int nx = snkHeadX;
        int ny = snkHeadY;

        hasMoved = 1;
        if( direction == SNK_PAUSE ) hasMoved = 0;
        else if( direction == SNK_UP ) ny -= 1;
        else if( direction == SNK_RIGHT ) nx += 1;
        else if( direction == SNK_DOWN ) ny += 1;
        else if( direction == SNK_LEFT ) nx -= 1;
        else hasMoved = 0;

        if( hasMoved )
        {
            if( !snkIsClassicMode && !wasIn && !wasOut )
            {
                if( direction == SNK_RIGHT && nx > SNK_RASTER_X - 1 ) nx = -1;
                else if( direction == SNK_LEFT && nx < 1 ) nx = SNK_RASTER_X;
                else if( direction == SNK_UP && ny < 1 ) ny = SNK_RASTER_Y;
                else if( direction == SNK_DOWN && ny > SNK_RASTER_Y - 1 ) ny = -1;
            }
            snkHeadX = nx;
            snkHeadY = ny;

            if( snkNom || snkGrowNom > 0 )
            {
                snkAddCoordinate( 0, 0 ); // dummy - overwritten by the shift below, matches real upstream
                if( snkGrowNom > 0 ) snkGrowNom--;
                snkNom = 0;
            }
            snkMoveCoordinates( snkHeadX, snkHeadY );
        }
    }

    int size = snkSnakeSize;
    int index;
    for( index = 0; index < size; index++ )
    {
        int xp = snkXPixel( snkSnakeX[ index ] );
        int yp = snkYPixel( snkSnakeY[ index ] );
        gbDrawFastHLine( xp, yp, 3 );
        gbDrawFastHLine( xp, yp + 1, 3 );
        gbDrawFastHLine( xp, yp + 2, 3 );

        if( index > 0 )
        {
            int deltaX = snkSnakeX[ index ] - snkSnakeX[ index - 1 ];
            int deltaY = snkSnakeY[ index ] - snkSnakeY[ index - 1 ];
            if( gbAbsInt( deltaX ) < 2 && gbAbsInt( deltaY ) < 2 )
            {
                gbDrawFastHLine( xp - deltaX, yp - deltaY, 3 );
                gbDrawFastHLine( xp - deltaX, yp - deltaY + 1, 3 );
                gbDrawFastHLine( xp - deltaX, yp - deltaY + 2, 3 );
            }
        }
    }
}

void snkDrawPrey()
{
    gbDrawCircle( snkXPixel( snkNomX ) + 1, snkYPixel( snkNomY ) + 1, 1 );
    if( snkIsClassicMode ) return;
    if( snkIsInArena( snkShrinkNomX, snkShrinkNomY ) )
      gbDrawFastHLine( snkXPixel( snkShrinkNomX ), snkYPixel( snkShrinkNomY ) + 1, 3 );
    if( snkIsInArena( snkGrowNomX, snkGrowNomY ) )
    {
        gbDrawFastHLine( snkXPixel( snkGrowNomX ), snkYPixel( snkGrowNomY ) + 1, 3 );
        gbDrawFastVLine( snkXPixel( snkGrowNomX ) + 1, snkYPixel( snkGrowNomY ), 3 );
    }
    if( snkIsInArena( snkWallNomX, snkWallNomY ) )
      gbDrawRect( snkXPixel( snkWallNomX ), snkYPixel( snkWallNomY ), 3, 3 );
}

void snkSetNewNomPos()
{
    snkSetOffBounds( &snkNomX, &snkNomY );
    snkGetRandomFreePos( &snkNomX, &snkNomY );
    if( snkIsClassicMode ) return;

    snkSetOffBounds( &snkShrinkNomX, &snkShrinkNomY );
    snkSetOffBounds( &snkGrowNomX, &snkGrowNomY );
    snkSetOffBounds( &snkWallNomX, &snkWallNomY );
    if( snkSnakeSize < 8 ) return;

    int rngVal = arand( 38 );
    if( rngVal == 4 || rngVal == 2 || rngVal == 3 || rngVal == 33 )
      snkGetRandomFreePos( &snkShrinkNomX, &snkShrinkNomY );
    else if( rngVal == 23 || rngVal == 5 || rngVal == 7 || rngVal == 29 )
      snkGetRandomFreePos( &snkGrowNomX, &snkGrowNomY );
    else if( rngVal == 13 || rngVal == 37 || rngVal == 1 )
      snkGetRandomFreePos( &snkWallNomX, &snkWallNomY );
}

int snkSnakeHasPrey()
{
    int shrinkNom = 0;
    int wallNom = 0;

    if( snkNomX == snkHeadX && snkNomY == snkHeadY )
    {
        snkNom = 1;
        snkScore += snkGameLevel;
    }
    else if( !snkIsClassicMode && snkShrinkNomX == snkHeadX && snkShrinkNomY == snkHeadY )
    {
        shrinkNom = 1;
        snkSnakeSize -= 4;
        snkScore -= 4;
    }
    else if( !snkIsClassicMode && snkGrowNomX == snkHeadX && snkGrowNomY == snkHeadY )
    {
        snkGrowNom += 3;
        snkScore += 3 * snkGameLevel;
    }
    else if( !snkIsClassicMode && snkWallNomX == snkHeadX && snkWallNomY == snkHeadY )
    {
        wallNom = 1;
        snkScore += snkGameLevel;
        snkDeleteRandomWallElement();
    }

    if( snkNom || shrinkNom || wallNom || snkGrowNom == 3 )
    {
        gbPlayNote( 54, 1 );
        return 1;
    }
    return 0;
}

int snkWallCollision() { return snkIsOutOfBounds( snkHeadX, snkHeadY ); }
int snkSnakeBite() { return snkIsPartOfSnake( snkHeadX, snkHeadY, 0 ); }

// ---- state: TITLE ----

void snkUpdateTitle()
{
    gbDrawBitmap( 0, 12, snakeLogoBitmap );
    gbSetColor( 1 );
    gbCursorX = 6;
    gbCursorY = 0;
    gbSetFont( gbFont5x7 );
    gbPrintString( "PRESS A" );
    gbSetFont( gbFont3x5 );

    if( gbPressed( BTN_A ) )
      snkState = SNK_STATE_MENU;
}

// ---- state: MENU (hand-rolled gb.menu() replacement) ----

void snkUpdateMenu()
{
    gbSetFont( gbFont5x7 );
    gbSetColor( 1 );

    gbCursorX = 4; gbCursorY = 2;
    if( snkMenuIndex == 0 ) gbPrintString( ">Start Game" ); else gbPrintString( " Start Game" );
    gbCursorX = 4; gbCursorY = 10;
    if( snkMenuIndex == 1 ) gbPrintString( ">Classic Mode" ); else gbPrintString( " Classic Mode" );
    gbCursorX = 4; gbCursorY = 18;
    if( snkMenuIndex == 2 ) gbPrintString( ">Highscore" ); else gbPrintString( " Highscore" );
    gbCursorX = 4; gbCursorY = 26;
    if( snkMenuIndex == 3 ) gbPrintString( ">Difficulty" ); else gbPrintString( " Difficulty" );
    gbCursorX = 4; gbCursorY = 34;
    if( snkMenuIndex == 4 ) gbPrintString( ">Titlescreen" ); else gbPrintString( " Titlescreen" );
    gbCursorX = 4; gbCursorY = 42;
    if( snkMenuIndex == 5 ) gbPrintString( ">Credits" ); else gbPrintString( " Credits" );

    if( gbPressed( BTN_UP ) && snkMenuIndex > 0 ) snkMenuIndex--;
    if( gbPressed( BTN_DOWN ) && snkMenuIndex < 5 ) snkMenuIndex++;

    if( gbPressed( BTN_A ) )
    {
        gbSetFont( gbFont3x5 );
        if( snkMenuIndex == 0 )
        {
            snkInitGame( 0 );
            snkState = SNK_STATE_PLAYING;
        }
        else if( snkMenuIndex == 1 )
        {
            snkInitGame( 1 );
            snkState = SNK_STATE_PLAYING;
        }
        else if( snkMenuIndex == 2 )
        {
            snkHighscoreMode = 1;
            snkHighscoreShowBoth = 1;
            snkState = SNK_STATE_HIGHSCORE;
        }
        else if( snkMenuIndex == 3 )
        {
            snkState = SNK_STATE_DIFFICULTY;
        }
        else if( snkMenuIndex == 4 )
        {
            snkState = SNK_STATE_TITLE;
        }
        else
        {
            snkState = SNK_STATE_CREDITS;
        }
    }
}

// ---- state: DIFFICULTY ----

void snkUpdateDifficulty()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Difficulty: " );
    gbPrintNumber( snkGameLevel );

    int xPos = 8;
    int yPos = 40;
    int i;
    for( i = 0; i < SNK_MAX_LEVEL; i++ )
    {
        int currentHeight = ( i + 1 ) * 4 + 1;
        gbDrawFastVLine( xPos + 5, yPos, currentHeight + 1 );
        gbDrawFastHLine( xPos, yPos + currentHeight + 1, 6 );
        if( i < snkGameLevel )
          gbFillRect( xPos, yPos, 4, currentHeight );
        xPos += 8;
        yPos -= 4;
    }

    if( gbPressed( BTN_C ) || gbPressed( BTN_A ) )
    {
        gbSetFont( gbFont3x5 );
        snkState = SNK_STATE_MENU;
        return;
    }
    if( snkGameLevel > 1 && ( gbRepeat( BTN_LEFT, 2 ) || gbRepeat( BTN_DOWN, 2 ) ) )
      snkGameLevel--;
    if( snkGameLevel < SNK_MAX_LEVEL && ( gbRepeat( BTN_RIGHT, 2 ) || gbRepeat( BTN_UP, 2 ) ) )
      snkGameLevel++;
}

// ---- state: HIGHSCORE ----

void snkUpdateHighscore()
{
    int* scores = snkHighscoreNew;
    if( snkHighscoreMode ) scores = snkHighscoreClassic;

    gbSetColor( 1 );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

    gbCursorX = 20;
    if( snkHighscoreMode ) gbCursorX = 4;
    gbCursorY = 3;
    if( snkHighscoreMode ) gbPrintString( "CLASSIC HIGH SCORES" ); else gbPrintString( "HIGH SCORES" );

    int i;
    for( i = 0; i < SNK_HIGHSCORE_COUNT; i++ )
    {
        gbCursorX = 6;
        gbCursorY = ( gbFontHeight * 2 ) + gbFontHeight * i;
        if( scores[ i ] == 0 )
          gbPrintString( "-" );
        else
          gbPrintString( "SNAKE" );

        if( scores[ i ] > 9999 ) gbCursorX = LCDWIDTH - 6 - 5 * gbFontWidth;
        else if( scores[ i ] > 999 ) gbCursorX = LCDWIDTH - 6 - 4 * gbFontWidth;
        else if( scores[ i ] > 99 ) gbCursorX = LCDWIDTH - 6 - 3 * gbFontWidth;
        else if( scores[ i ] > 9 ) gbCursorX = LCDWIDTH - 6 - 2 * gbFontWidth;
        else gbCursorX = LCDWIDTH - 6 - gbFontWidth;
        gbPrintNumber( scores[ i ] );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        if( snkHighscoreShowBoth && snkHighscoreMode == 1 )
        {
            snkHighscoreMode = 0; // classic shown first, now show the new-mode table
        }
        else
        {
            snkState = SNK_STATE_MENU;
        }
    }
}

// ---- state: CREDITS ----

void snkUpdateCredits()
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "v2.0 Oct12\nCC-BY-SA 2018\nLady Awesome\nMakerSquirrel\n  HighScore code\n  by R0d0t" );

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        snkState = SNK_STATE_MENU;
    }
}

// ---- state: GAMEOVER_SCORE ----

void snkUpdateGameOverScore()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbCursorY = 3;
    gbCursorX = 14;
    gbPrintString( "GAME OVER!" );
    gbCursorX = 12;
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight * 2;
    gbPrintString( "YOUR SCORE:" );
    gbCursorX = 30;
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbPrintNumber( snkScore );
    if( snkGotHighscore )
    {
        gbCursorX = 0;
        gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
        gbPrintString( "NEW HIGHSCORE!" );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbSetFont( gbFont3x5 );
        if( snkGotHighscore )
          snkSaveHighscore( snkIsClassicMode );
        snkHighscoreMode = snkIsClassicMode;
        snkHighscoreShowBoth = 0;
        snkState = SNK_STATE_HIGHSCORE;
    }
}

// ---- state: PLAYING ----

void snkGameOver( int wallCrash )
{
    gbPlayNote( 0, 3 );
    snkWallCrash = wallCrash;
    if( snkIsClassicMode )
      snkGotHighscore = ( snkScore > snkHighscoreClassic[ SNK_HIGHSCORE_COUNT - 1 ] );
    else
      snkGotHighscore = ( snkScore > snkHighscoreNew[ SNK_HIGHSCORE_COUNT - 1 ] );

    if( wallCrash ) gbPopup( "Wallcrash!", 9 ); else gbPopup( "Snakebite!", 9 );
    snkState = SNK_STATE_GAMEOVER_SCORE;
}

// Direction to use for this tick: real hardware picks the pressed direction
// outright in classic mode, but in new mode ignores a press that would
// turn the snake directly backwards onto itself (keeping whatever
// direction was already in effect instead) - see this file's own header
// comment on upstream's real per-direction ternary chain this replaces.
int snkPickDirection( int pressedDir, int oppositeDir )
{
    if( snkIsClassicMode ) return pressedDir;
    if( snkLastTimeButtonPressed == oppositeDir ) return snkLastTimeButtonPressed;
    return pressedDir;
}

void snkUpdatePlaying()
{
    if( gbPressed( BTN_C ) )
    {
        snkState = SNK_STATE_MENU;
        return;
    }

    if( snkWallCollision() ) { snkGameOver( 1 ); return; }
    if( snkSnakeBite() ) { snkGameOver( 0 ); return; }

    snkDelayCounter++;
    int timeChanged = ( ( snkDelayCounter % snkLevelModuloHelper ) == 0 );
    snkDrawArena();

    if( gbRepeat( BTN_UP, 1 ) )
      snkLastButtonPressed = snkPickDirection( SNK_UP, SNK_DOWN );
    else if( gbRepeat( BTN_RIGHT, 1 ) )
      snkLastButtonPressed = snkPickDirection( SNK_RIGHT, SNK_LEFT );
    else if( gbRepeat( BTN_DOWN, 1 ) )
      snkLastButtonPressed = snkPickDirection( SNK_DOWN, SNK_UP );
    else if( gbRepeat( BTN_LEFT, 1 ) )
      snkLastButtonPressed = snkPickDirection( SNK_LEFT, SNK_RIGHT );
    else if( gbRepeat( BTN_B, 1 ) || gbRepeat( BTN_A, 1 ) )
    {
        snkLastButtonPressed = SNK_PAUSE;
        if( snkLastTimeButtonPressed != snkLastButtonPressed )
          gbPopup( "Pause", 9 );
    }

    if( timeChanged )
      snkLastTimeButtonPressed = snkLastButtonPressed;

    snkDrawSnake( snkLastButtonPressed, timeChanged );
    if( snkLastButtonPressed != SNK_PAUSE && timeChanged && snkSnakeHasPrey() )
      snkSetNewNomPos();
    snkDrawPrey();
}

// ---- entry points ----

void gameSnake5110_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 );

    snkState = SNK_STATE_TITLE;
    snkMenuIndex = 0;
    snkGameLevel = 1;
    snkSnakeSize = 0;
    snkHighscoreMode = 0;
    snkHighscoreShowBoth = 0;
    snkGotHighscore = 0;

    snkLoadHighscores();
}

void gameSnake5110_update()
{
    if( !gbUpdate() ) return;

    if( snkState == SNK_STATE_PLAYING )
      snkUpdatePlaying();
    else if( snkState == SNK_STATE_MENU )
      snkUpdateMenu();
    else if( snkState == SNK_STATE_TITLE )
      snkUpdateTitle();
    else if( snkState == SNK_STATE_DIFFICULTY )
      snkUpdateDifficulty();
    else if( snkState == SNK_STATE_HIGHSCORE )
      snkUpdateHighscore();
    else if( snkState == SNK_STATE_CREDITS )
      snkUpdateCredits();
    else
      snkUpdateGameOverScore();

    gbRenderFrame();
}
