#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// CRAZYCAR (Baptiste Pouget, GPLv3 - confirmed via the repo's own
// LICENSE.txt - github.com/baptistepouget/CRAZYCAR-Gamebuino). A top-down
// endless car dodger: the car is pinned to a fixed X near the left edge of
// a 3-lane road and only moves up/down, while trees scroll by in the
// verges and obstacles (traffic cone / nails / an animal / a pedestrian)
// scroll toward the car from the right - dodge them for as long as
// possible, one point per obstacle survived, game over on any hit.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(N)` became `arand(N)`
// (this project's own established RNG helper). Upstream's own bare globals
// (`voitureX/Y`, `obs`, `dec`, `vitesse`, `score`, `perdu`, `debut`) and
// functions (`dVoiture`, `dDecor`, `dObstacles`, `afficherMenu`) were all
// given a `ccar`-prefixed name, since Vircon32 has no linker and every game
// in this single compiled cartridge shares one flat global namespace.
// Upstream's own C++ default member initializers on its `Obstacle`/`Decor`
// structs (`int x = 0;` etc, not legal plain-C struct syntax) were dropped -
// every field is instead given its real starting value explicitly by
// `ccarInitRoad()`/`ccarBeginMenu()` below (see gameAgaruino.c's own
// `AgarBall` struct for the same established precedent). Upstream's own
// `switch` statements (in `dObstacles()`/`afficherMenu()`) became if/else-if
// chains instead, matching the style already established by gamePong.c/
// gameAgaruino.c/gameTaquin.c (this dialect's exact support for `switch` is
// untested there, so this port doesn't risk being the first either).
//
// **Bitmaps**: unlike gameShufflepuck.c's own decision to drop its (large,
// purely decorative, ~88x48) upstream bitmaps outright, this game's six
// sprites (the 16x8 car, and five 8x8 obstacle/tree sprites) are small,
// few, and directly gameplay-relevant, so real upstream `B00000111`-style
// PROGMEM byte literals were mechanically converted into this shim's own
// real bitmap format (see the bitmap tables' own comment below) and drawn
// with the real `gbDrawBitmap()` primitive - this reproduces the original
// pixel art exactly, not a procedural stand-in. Collision against the car
// uses real `gb.collideBitmapBitmap()` (`gbCollideBitmapBitmap()`),
// genuine pixel-perfect collision matching real hardware exactly, not a
// bounding-box approximation.
//
// Real `gb.titleScreen(F("Crazycar"))` (shown once at boot, and again -
// via `afficherMenu()`'s own two "cancel" cases - whenever the difficulty
// menu is backed out of) and real `gb.menu(menu, 4)` (a blocking built-in
// Easy/Hard/Extreme/Exit picker widget with no shim equivalent at all)
// were both converted into an explicit CCAR_STATE_TITLE/MENU/PLAY/GAMEOVER
// state machine, matching gamePong.c's own established "blocking call ->
// explicit resumable state" treatment. Since there's no real on-hardware
// widget left to port pixel-for-pixel, the difficulty menu here is this
// port's own simple up/down + A-to-select / B-to-cancel list with a ">"
// cursor marker - functionally equivalent (pick Easy/Hard/Extreme to
// start, or back out to the title) rather than a literal translation.
//
// A genuine upstream quirk, preserved exactly rather than "fixed":
// `afficherMenu()` only ever resets the car's Y position, each obstacle's
// Y position (back to the starting lane), and the score - it never resets
// any obstacle's X position or any tree's scroll position. Those are only
// ever initialized once, in `setup()`. Practically: after a game over,
// restarting keeps whatever obstacle X positions and tree-scroll offsets
// were on screen at the moment of the crash, rather than presenting a
// freshly reset road - ported here as `ccarInitRoad()` (called once, from
// `gameCrazyCar_init()`) vs. `ccarBeginMenu()` (called every time the
// difficulty menu is (re)entered, resetting only Y positions and score),
// deliberately mirroring that exact split.
//
// Upstream's own real game-over text (`"Your score is: "` + the score +
// `println()` + `"Press C"`) is restored verbatim in `ccarUpdateGameOver()`
// now that gbSetFont()/real fonts exist - it used to be shortened to a
// "SCORE" label + the number on its own line + "PRESS C" because this
// shim's old fixed 8x8 font made the real string too wide for 84px; not
// needed anymore at upstream's own real font3x5 size. Upstream's own
// inconsistent title-screen casing ("Crazycar" at boot, "CRAZYCAR" on
// every subsequent return to it) was normalized to the all-caps menu title
// "CRAZYCAR" everywhere, a purely cosmetic no-op difference.
//
// `gb.pickRandomSeed()` was dropped outright (this project's established
// no-op precedent for every upstream `randomSeed()`-style call - see
// gamePong.c's own header comment); upstream never calls it here anyway.
// There is no EEPROM/high-score persistence upstream, so none was added.

typedef struct
{
    int x;
    int y;
    int kind; // 0=cone, 1=nails, 2=animal, 3=pedestrian - matches upstream's own nBitmap 0..3
} CcarObstacle;
CcarObstacle ccarObstacles[3];

typedef struct
{
    int x;
    int y;
} CcarTree;
CcarTree ccarDecor[24];

// Real upstream PROGMEM byte literals (B00000111 etc), mechanically
// converted into this shim's own real standard bitmap format
// ({width,height,packed-row-bytes...} - see gbDrawBitmap()'s own doc
// comment in gamebuinoShim.h), the same format gbCollideBitmapBitmap()
// reads for pixel-perfect collision. The 8-wide sprites' own row values
// convert to the standard format completely unchanged (a single MSB-first
// byte per row is already exactly what one of these int values already
// was); only the 16-wide car sprite's own rows needed splitting into 2
// real bytes each (high byte = row>>8, low byte = row&0xFF) to fit the
// standard multi-byte-row layout.
int ccarCarBitmap[18]= { 16, 8, 7,240, 11,208, 11,254, 255,255, 255,255, 255,255, 206,39, 120,60 };
int ccarConeBitmap[10]= { 8, 8, 0, 24, 60, 36, 126, 66, 255, 255 };
int ccarNailsBitmap[10]= { 8, 8, 0, 24, 60, 6, 15, 48, 120, 0 };
int ccarAnimalBitmap[10]= { 8, 8, 2, 3, 127, 254, 126, 68, 68, 0 };
int ccarPedestrianBitmap[10]= { 8, 8, 90, 90, 126, 24, 24, 60, 36, 108 };
int ccarTreeBitmap[10]= { 8, 8, 60, 126, 126, 126, 60, 24, 24, 60 };

#define CCAR_LIMIT_TOP 12    // upstream LIMITEROUTEB - car's own top road edge
#define CCAR_LIMIT_BOTTOM 36 // upstream LIMITEROUTEH - car's own bottom road edge
#define CCAR_LIMIT_MID 24    // upstream LIMITEROUTEM - dashed centerline
#define CCAR_NUM_DASHES 4    // upstream NOMBREPOINTILLES
#define CCAR_DASH_LEN 17     // upstream taillePointille (LCDWIDTH/4 - 4, a fixed constant upstream too)
#define CCAR_CAR_X 8

enum CcarState
{
    CCAR_STATE_TITLE = 0,
    CCAR_STATE_MENU = 1,
    CCAR_STATE_PLAY = 2,
    CCAR_STATE_GAMEOVER = 3
};

int ccarState;
int ccarCarY;
int ccarSpeed;      // upstream vitesse - doubles as car move speed AND road scroll speed, matching upstream's own dual use of one variable
int ccarScore;
int ccarDashGap;    // upstream espacePointilles - animates every frame, never reset by ccarBeginMenu() (matches upstream)
int ccarMenuIndex;  // 0=Easy, 1=Hard, 2=Extreme, 3=Exit

// Upstream: setup()'s own one-time obstacle/tree/road init - only ever
// called once (see this file's own header comment on the ccarBeginMenu()
// split below).
void ccarInitRoad()
{
    int i;

    ccarObstacles[0].x = -8;
    ccarObstacles[1].x = 30;
    ccarObstacles[2].x = 60;
    ccarObstacles[0].y = 14;
    ccarObstacles[1].y = 14;
    ccarObstacles[2].y = 14;
    ccarObstacles[0].kind = 0;
    ccarObstacles[1].kind = 0;
    ccarObstacles[2].kind = 0;

    for( i = 0; i < 12; i++ )
    {
        ccarDecor[i].x = i * 8;
        ccarDecor[i + 12].x = i * 8;
        ccarDecor[i].y = 37;
        ccarDecor[i + 12].y = 1;
    }

    ccarDashGap = 4;
    ccarCarY = 28;
    ccarScore = 0;
}

void ccarBeginTitle()
{
    ccarState = CCAR_STATE_TITLE;
}

// Upstream: afficherMenu()'s own pre-menu reset - only the car's Y
// position, each obstacle's Y position and the score (NOT obstacle X or
// tree scroll positions - see this file's own header comment).
void ccarBeginMenu()
{
    ccarState = CCAR_STATE_MENU;
    ccarCarY = 28;
    ccarObstacles[0].y = 14;
    ccarObstacles[1].y = 14;
    ccarObstacles[2].y = 14;
    ccarScore = 0;
}

void ccarBeginPlay()
{
    ccarState = CCAR_STATE_PLAY;
}

void ccarBeginGameOver()
{
    ccarState = CCAR_STATE_GAMEOVER;
}

void ccarUpdateTitle()
{
    gbSetColor( 1 );
    gbFontSize = 1;

    gbCursorX = 10;
    gbCursorY = 16;
    gbPrintString( "CRAZYCAR" );

    gbCursorX = 14;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      ccarBeginMenu();
}

void ccarUpdateMenu()
{
    gbSetColor( 1 );
    gbFontSize = 1;

    gbCursorX = 8;
    gbCursorY = 2;
    if( ccarMenuIndex == 0 ) gbPrintString( ">EASY" );
    else gbPrintString( " EASY" );

    gbCursorX = 8;
    gbCursorY = 12;
    if( ccarMenuIndex == 1 ) gbPrintString( ">HARD" );
    else gbPrintString( " HARD" );

    gbCursorX = 8;
    gbCursorY = 22;
    if( ccarMenuIndex == 2 ) gbPrintString( ">EXTREME" );
    else gbPrintString( " EXTREME" );

    gbCursorX = 8;
    gbCursorY = 32;
    if( ccarMenuIndex == 3 ) gbPrintString( ">EXIT" );
    else gbPrintString( " EXIT" );

    if( gbPressed( BTN_DOWN ) )
    {
        ccarMenuIndex = ccarMenuIndex + 1;
        if( ccarMenuIndex > 3 ) ccarMenuIndex = 0;
        gbPlayTick();
    }
    if( gbPressed( BTN_UP ) )
    {
        ccarMenuIndex = ccarMenuIndex - 1;
        if( ccarMenuIndex < 0 ) ccarMenuIndex = 3;
        gbPlayTick();
    }
    // upstream: gb.menu()'s own cancel (Button B) return value (-1) re-shows
    // the title screen - ported the same way here.
    if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        ccarBeginTitle();
        return;
    }
    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( ccarMenuIndex == 0 )
        {
            ccarSpeed = 2;
            ccarBeginPlay();
        }
        else if( ccarMenuIndex == 1 )
        {
            ccarSpeed = 3;
            ccarBeginPlay();
        }
        else if( ccarMenuIndex == 2 )
        {
            ccarSpeed = 4;
            ccarBeginPlay();
        }
        else
        {
            // upstream: "Exit" also re-shows the title screen
            ccarBeginTitle();
        }
    }
}

// Upstream dDecor(): draws the road edges, scrolls the two rows of
// roadside trees (wrapping each once it scrolls fully off the left edge),
// and animates the dashed centerline.
void ccarDrawRoad()
{
    int i;

    gbSetColor( 1 );
    gbDrawLine( 0, CCAR_LIMIT_BOTTOM, LCDWIDTH, CCAR_LIMIT_BOTTOM );
    gbDrawLine( 0, CCAR_LIMIT_TOP, LCDWIDTH, CCAR_LIMIT_TOP );

    for( i = 0; i < 12; i++ )
    {
        if( ccarDecor[i].x < -8 ) ccarDecor[i].x = ccarDecor[i].x + 96;
        if( ccarDecor[i + 12].x < -8 ) ccarDecor[i + 12].x = ccarDecor[i + 12].x + 96;

        ccarDecor[i].x = ccarDecor[i].x - ccarSpeed;
        gbDrawBitmap( ccarDecor[i].x, ccarDecor[i].y, ccarTreeBitmap );

        ccarDecor[i + 12].x = ccarDecor[i + 12].x - ccarSpeed;
        gbDrawBitmap( ccarDecor[i + 12].x, ccarDecor[i + 12].y, ccarTreeBitmap );
    }

    for( i = 0; i < CCAR_NUM_DASHES + 1; i++ )
    {
        gbDrawLine
        (
            ccarDashGap + i * ( CCAR_DASH_LEN + 8 ), CCAR_LIMIT_MID,
            ccarDashGap + i * ( CCAR_DASH_LEN + 8 ) + CCAR_DASH_LEN, CCAR_LIMIT_MID
        );
    }

    ccarDashGap = ccarDashGap - ccarSpeed;
    if( ccarDashGap < -CCAR_DASH_LEN - 4 ) ccarDashGap = 2;
}

// Upstream dVoiture(): moves the car within the road's own vertical
// bounds, then draws it.
void ccarUpdateCar()
{
    if( gbRepeat( BTN_DOWN, 1 ) && ( ( ccarCarY + 8 ) < CCAR_LIMIT_BOTTOM ) )
      ccarCarY = ccarCarY + ccarSpeed;
    if( gbRepeat( BTN_UP, 1 ) && ( ccarCarY > CCAR_LIMIT_TOP ) )
      ccarCarY = ccarCarY - ccarSpeed;

    gbSetColor( 1 );
    gbDrawBitmap( CCAR_CAR_X, ccarCarY, ccarCarBitmap );
}

// Upstream dObstacles(): scrolls the 3 obstacles, draws each by its own
// kind, checks collision against the car (bounding-box approximation - see
// this file's own header comment), and respawns any obstacle that's
// scrolled fully off the left edge (scoring a point).
void ccarUpdateObstacles()
{
    int i;
    int* kindBitmap;

    for( i = 0; i < 3; i++ )
    {
        ccarObstacles[i].x = ccarObstacles[i].x - ccarSpeed;

        if( ccarObstacles[i].kind == 0 ) kindBitmap = ccarConeBitmap;
        else if( ccarObstacles[i].kind == 1 ) kindBitmap = ccarNailsBitmap;
        else if( ccarObstacles[i].kind == 2 ) kindBitmap = ccarAnimalBitmap;
        else kindBitmap = ccarPedestrianBitmap;

        gbSetColor( 1 );
        gbDrawBitmap( ccarObstacles[i].x, ccarObstacles[i].y, kindBitmap );

        // Real gb.collideBitmapBitmap() - genuine pixel-perfect collision.
        if( gbCollideBitmapBitmap( ccarObstacles[i].x, ccarObstacles[i].y, kindBitmap, CCAR_CAR_X, ccarCarY, ccarCarBitmap ) )
          ccarBeginGameOver();

        if( ccarObstacles[i].x <= -8 )
        {
            ccarScore = ccarScore + 1;
            ccarObstacles[i].x = 84;
            ccarObstacles[i].y = arand( 2 ) * 12 + 14;
            ccarObstacles[i].kind = arand( 4 );
        }
    }
}

void ccarUpdatePlay()
{
    ccarDrawRoad();
    ccarUpdateCar();
    ccarUpdateObstacles();
}

// Direct port of upstream's own real game-over text now that gbSetFont()/
// real fonts make it fit as-written: "Your score is: " and the score share
// one real line (upstream's own `print()`+`println()` pair, no line break
// between them), "Press C" on the line below - upstream never sets
// cursorX/cursorY or setFont() anywhere in this game, so both start at
// their own real defaults ((0,0), font3x5) the first (and only) time this
// screen is ever drawn per round.
void ccarUpdateGameOver()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbSetFont( gbFont3x5 );

    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Your score is: " );
    gbPrintNumber( ccarScore );

    gbCursorX = 0;
    gbCursorY = 6;
    gbPrintString( "Press C" );

    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        ccarBeginMenu();
    }
}

void gameCrazyCar_init()
{
    gbBegin();
    ccarInitRoad();
    ccarSpeed = 2;
    ccarMenuIndex = 0;
    ccarBeginTitle();
}

void gameCrazyCar_update()
{
    if( !gbUpdate() ) return;

    if( ccarState == CCAR_STATE_TITLE ) ccarUpdateTitle();
    else if( ccarState == CCAR_STATE_MENU ) ccarUpdateMenu();
    else if( ccarState == CCAR_STATE_PLAY ) ccarUpdatePlay();
    else ccarUpdateGameOver();

    gbRenderFrame();
}
