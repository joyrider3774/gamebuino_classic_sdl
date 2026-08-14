#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Agaruino (ogbaba, GPLv3 - github.com/ogbaba/Agaruino). A real agar.io
// clone for Gamebuino Classic: steer a blob around a 100x100 world eating
// smaller blobs to grow, avoiding bigger ones that can eat you right back.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameAgaruino.c (which already did the real upstream-Arduino-
// to-Vircon32-dialect porting work - gb.x.y() flattening, random()->
// arand(), the upstream `byte est_ia`-as-boolean idiom -> plain `bool`,
// French-named globals kept with an `agar` prefix, the real vy-clamp typo
// normalized rather than reproduced, the real invisible-player-blob quirk
// preserved as upstream wrote it, and the real float-display bug fixed via
// gbPrintFloat() - see that file's own header comment for the full
// original write-up of all of the above, none of which needed revisiting
// here) - converting Vircon32's own C dialect back to standard C:
//  - `struct AgarBall { ... };` -> `typedef struct { ... } AgarBall;`, so
//    `AgarBall* ball`-style pointer parameters and plain `AgarBall x;`
//    declarations both work without a `struct` keyword at every use site,
//    exactly as this file's own function signatures already assumed.
//  - `AgarBall[AGAR_PLAYER_COUNT] agarBalls;` -> `AgarBall
//    agarBalls[AGAR_PLAYER_COUNT];` (array-declaration order).
// No other change was needed - every text string here is already a plain
// C string literal, and every other primitive call site already matches
// this project's own gamebuinoShim.h signatures unchanged.

typedef struct
{
    bool isAI;
    bool isFood;   // controls respawn size after being eaten: 1 -> small, 0 -> medium
    float size;
    float vx, vy;
    float x, y;
} AgarBall;

#define AGAR_MAP_W 100
#define AGAR_MAP_H 100
#define AGAR_MAX_SPEED 1.2
#define AGAR_PLAYER_COUNT 16

// Solo play: the human is always element 0 of agarBalls[].
int agarPlayerIndex = 0;
AgarBall agarBalls[ AGAR_PLAYER_COUNT ];

enum AgarState
{
    AGAR_STATE_MENU = 0,
    AGAR_STATE_PLAY = 1
};

int agarState;
bool agarWon;

void agarBeginMenu()
{
    agarState = AGAR_STATE_MENU;
}

void agarBeginPlay()
{
    // Scatter every ball across the map. Every slot but the player's own
    // becomes, 50/50, either a small stationary-ish "food" blob or a
    // medium AI-controlled hunter; the player always starts medium-sized.
    int i;
    for( i = 0; i < AGAR_PLAYER_COUNT; i++ )
    {
        agarBalls[ i ].x = arand( AGAR_MAP_W );
        agarBalls[ i ].y = arand( AGAR_MAP_H );
        agarBalls[ i ].vx = 0;
        agarBalls[ i ].vy = 0;

        if( i != agarPlayerIndex )
        {
            if( arand( 2 ) )
            {
                agarBalls[ i ].isFood = true;
                agarBalls[ i ].isAI = false;
                agarBalls[ i ].size = 1;
            }
            else
            {
                agarBalls[ i ].isAI = true;
                agarBalls[ i ].isFood = false;
                agarBalls[ i ].size = 2;
            }
        }
        else
        {
            agarBalls[ agarPlayerIndex ].isAI = false;
            agarBalls[ agarPlayerIndex ].isFood = false;
            agarBalls[ agarPlayerIndex ].size = 2;
        }
    }

    agarState = AGAR_STATE_PLAY;
    agarWon = false;
}

void agarUpdateMenu()
{
    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "AGARUINO !" );

    gbCursorX = 2;
    gbCursorY = 14;
    gbPrintString( "A LANCER" );

    if( agarWon )
    {
        gbCursorX = 2;
        gbCursorY = 26;
        gbPrintString( "GAGNE !" );
    }

    if( gbPressed( BTN_A ) )
      agarBeginPlay();
}

void agarDraw()
{
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Taille : " );
    gbPrintFloat( agarBalls[ agarPlayerIndex ].size, 2 );

    int i;
    for( i = 0; i < AGAR_PLAYER_COUNT; i++ )
    {
        // Preserved exactly as upstream wrote it (see this file's own
        // header comment) - this is an "is at least one edge outside the
        // viewport" test, not "is on screen", so the player's own ball
        // (always dead-center relative to itself) never satisfies any of
        // these four conditions and is therefore never actually drawn,
        // matching real hardware's own original behavior.
        if( ( agarBalls[ i ].x - agarBalls[ i ].size < agarBalls[ agarPlayerIndex ].x - LCDWIDTH / 2 )
            || ( agarBalls[ i ].x + agarBalls[ i ].size > agarBalls[ agarPlayerIndex ].x + LCDWIDTH / 2 )
            || ( agarBalls[ i ].y - agarBalls[ i ].size < agarBalls[ agarPlayerIndex ].y - LCDHEIGHT / 2 )
            || ( agarBalls[ i ].y + agarBalls[ i ].size < agarBalls[ agarPlayerIndex ].y + LCDHEIGHT / 2 ) )
        {
            gbFillCircle
            (
                (int)( agarBalls[ i ].x - agarBalls[ agarPlayerIndex ].x + LCDWIDTH / 2 ),
                (int)( agarBalls[ i ].y - agarBalls[ agarPlayerIndex ].y + LCDHEIGHT / 2 ),
                (int)agarBalls[ i ].size
            );
        }
    }
}

// Adds (ax, ay) to a ball's own velocity, then clamps both axes to a
// per-ball max speed that shrinks as the ball grows (a bigger blob moves
// sluggishly) - normalized from upstream's own min()/max() calls (see
// this file's own header comment for the one-character typo found there).
void agarAccelerate( AgarBall* ball, float ax, float ay )
{
    float vmax = AGAR_MAX_SPEED - ball->size / 20.0;

    ball->vx = ball->vx + ax;
    if( ball->vx > vmax ) ball->vx = vmax;
    if( ball->vx < -vmax ) ball->vx = -vmax;

    ball->vy = ball->vy + ay;
    if( ball->vy > vmax ) ball->vy = vmax;
    if( ball->vy < -vmax ) ball->vy = -vmax;
}

void agarHandleInput()
{
    if( gbRepeat( BTN_DOWN, 10 ) )
      agarAccelerate( &agarBalls[ agarPlayerIndex ], 0, 0.5 );
    if( gbRepeat( BTN_UP, 10 ) )
      agarAccelerate( &agarBalls[ agarPlayerIndex ], 0, -0.5 );
    if( gbRepeat( BTN_RIGHT, 10 ) )
      agarAccelerate( &agarBalls[ agarPlayerIndex ], 0.5, 0 );
    if( gbRepeat( BTN_LEFT, 10 ) )
      agarAccelerate( &agarBalls[ agarPlayerIndex ], -0.5, 0 );

    if( gbPressed( BTN_C ) )
      agarBeginMenu();
}

// predator eats prey if (and only if) it's genuinely bigger - prey
// respawns at a random spot, at its own "resting" size (small for a food
// blob, medium for a hunter, matching agarBeginPlay()'s own initial sizes)
void agarEat( AgarBall* predator, AgarBall* prey )
{
    if( predator->size > prey->size )
    {
        predator->size = predator->size + prey->size / 4;

        prey->x = arand( AGAR_MAP_W );
        prey->y = arand( AGAR_MAP_H );
        if( prey->isFood )
          prey->size = 1;
        else
          prey->size = 2;

        gbPlayOK();
    }
}

void agarHandleEating()
{
    int i, j;
    for( i = 0; i < AGAR_PLAYER_COUNT; i++ )
    {
        for( j = 0; j < AGAR_PLAYER_COUNT; j++ )
        {
            if( i == j ) continue;

            if( gbCollidePointRect
                (
                    (int)agarBalls[ j ].x, (int)agarBalls[ j ].y,
                    (int)( agarBalls[ i ].x - agarBalls[ i ].size ),
                    (int)( agarBalls[ i ].y - agarBalls[ i ].size ),
                    (int)( agarBalls[ i ].size * 2 ), (int)( agarBalls[ i ].size * 2 )
                )
                && ( agarBalls[ i ].size > agarBalls[ j ].size ) )
              agarEat( &agarBalls[ i ], &agarBalls[ j ] );
        }
    }
}

void agarMoveAI( AgarBall* ball )
{
    if( arand( 10 ) == 0 )
      agarAccelerate( ball, (float)arand( 2 ) - 0.5, (float)arand( 2 ) - 0.5 );
}

void agarHandleMovement()
{
    int i;
    for( i = 0; i < AGAR_PLAYER_COUNT; i++ )
    {
        if( agarBalls[ i ].isAI )
          agarMoveAI( &agarBalls[ i ] );

        agarBalls[ i ].x = agarBalls[ i ].x + agarBalls[ i ].vx;
        agarBalls[ i ].y = agarBalls[ i ].y + agarBalls[ i ].vy;

        if( agarBalls[ i ].x > AGAR_MAP_W ) agarBalls[ i ].x = AGAR_MAP_W;
        if( agarBalls[ i ].x < 0 ) agarBalls[ i ].x = 0;
        if( agarBalls[ i ].y > AGAR_MAP_H ) agarBalls[ i ].y = AGAR_MAP_H;
        if( agarBalls[ i ].y < 0 ) agarBalls[ i ].y = 0;
    }
}

// Anyone reaching this size ends the round - not necessarily the player:
// an AI hunter can win it too, matching upstream's own generic "someone
// won" message (see agarUpdateMenu()'s own "GAGNE !" line) rather than
// tracking which specific ball crossed the threshold.
void agarCheckWin()
{
    int i;
    for( i = 0; i < AGAR_PLAYER_COUNT; i++ )
    {
        if( agarBalls[ i ].size > 20 )
        {
            agarBeginMenu();
            agarWon = true;
        }
    }
}

void agarUpdatePlay()
{
    agarHandleInput();
    agarHandleMovement();
    agarHandleEating();
    agarCheckWin();
    agarDraw();
}

void gameAgaruino_init()
{
    gbBegin();
    agarBeginMenu();
    agarWon = false;
}

void gameAgaruino_update()
{
    if( !gbUpdate() ) return;

    if( agarState == AGAR_STATE_MENU ) agarUpdateMenu();
    else agarUpdatePlay();

    gbRenderFrame();
}
