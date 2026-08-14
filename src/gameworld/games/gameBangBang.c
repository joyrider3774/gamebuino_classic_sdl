#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Bang! Bang! (RackhamLeNoir, GPLv3 -
// github.com/RackhamLeNoir/gamebuino-bangbang). A port of the classic DOS
// "Bang! Bang!" artillery-duel game: aim a cannon (angle + force) and fire
// a bullet along a parabolic trajectory across procedurally generated
// hilly terrain at a second, static cannon.
//
// STRUCTURAL NOTE: real upstream is a genuine C++ multi-file sketch
// (bangbang.ino plus three real classes, each its own .cpp/.h pair:
// Cannon, Bullet, Terrain). This dialect has no classes/methods (see
// gamebuinoShim.h's own header comment), so every class became a plain
// data-only struct plus free functions that take an explicit pointer -
// the same "flatten a real single-instance C++ library into plain C
// globals/functions" treatment already proven for gameSuperSpaceShooter.c/
// gameSolitaire.c:
//   Cannon   -> struct BbangCannon;  BbangCannon bbangCannons[2]
//               (index 0 = cannon1/the player's own cannon, index 1 =
//               cannon2/the static target - matching upstream's own
//               `Cannon cannon1(...)`/`Cannon cannon2(...)` globals).
//   Bullet   -> struct BbangBullet;  a single global bbangBullet, matching
//               upstream's own single global `Bullet bullet;` (only ever
//               one bullet in flight at a time). `Bullet::_c` (a
//               `Cannon*`) ports directly as `BbangBullet::c`, a real
//               `BbangCannon*` pointer field - this dialect supports
//               pointers to named struct types natively.
//   Terrain  -> a single global `int bbangTerrain[LCDWIDTH]` array plus
//               free functions - matching this project's own established
//               "one real instance -> plain globals, no struct needed"
//               precedent (e.g. gameSuperSpaceShooter.c's own Player).
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` call (see gamePong.c's own header comment). `uint8_t`/`byte`/
// `unsigned int`/`long int` all became plain `int` (avrCompat.h aliasing -
// this dialect's own single 32-bit int type has no narrower width anyway).
// `random(a,b)` (Arduino's own ranged, upper-exclusive random) became
// `a + arand(b-a)`, matching gameSuperSpaceShooter.c's own established
// convention. `constrain(v,lo,hi)` (an Arduino macro, unavailable here) is
// inlined directly as `gbMax(lo, gbMin(hi, v))` (no ternary operator in
// this dialect - see VIRCON32_C_DIALECT.md). `PI` (an Arduino macro) is
// replaced with a local `BBANG_PI` constant, matching gameHexagon.c's own
// established precedent for avoiding a bet on math.h's own undocumented-
// in-this-project `pi` symbol. There is no bitmap art anywhere in real
// upstream (every visual - cannon, bullet, terrain, aim indicator - is
// drawn with primitive shapes: circles, lines, rects, a triangle), so this
// port needed no bitmap restoration work at all.
//
// GB_GRAY is restored exactly where upstream calls `setColor(GRAY)`
// (Cannon::draw()'s own aiming-indicator triangle) - not substituted with
// BLACK/WHITE.
//
// TITLE SCREEN: real upstream's own blocking `gb.titleScreen(F("Bang!
// Bang!"))` (setup()) became an explicit BBANG_SCREEN_TITLE state shown at
// boot, dismissed by a genuine Button A press - matching the "blocking
// loop -> explicit resumable state" treatment used throughout this
// project (e.g. gamePong.c/gameTaquin.c).
//
// A REAL, CONFIRMED UPSTREAM COPY-PASTE BUG, FOUND AND FIXED (not
// preserved): both `inputsgame()` and `inputswin()` call
// `gb.titleScreen(F("Taquin"))` on a Button C press - not
// `F("Bang! Bang!")`. RackhamLeNoir is also the real author of
// gamebuino-taquin (already ported here as gameTaquin.c), and this is a
// genuine leftover from that other project's own source, not a deliberate
// crossover reference. Real `titleScreen()` is blocking and unconditional
// about its own name argument - on real hardware, pressing C mid-game
// genuinely showed a screen reading "Taquin", not "Bang! Bang!", then
// resumed play exactly where it left off once A was pressed again. Flagged
// as a genuine, misleading negative player-experience bug and fixed:
// `bbangUpdatePause()` now prints "BANG! BANG!" instead.
//
// TWO MORE REAL UPSTREAM BUGS, CONFIRMED AND PRESERVED (both a real
// "always-false unsigned comparison" pattern, verified by tracing rather
// than assumed):
// - `Terrain::get()`/`Cannon::getY()` both return real `uint8_t` (always
//   >= 0 in real C++), so upstream's own `if (ty < 0 && ...)` (updategame,
//   guarding "bullet fell below the terrain's own valid range without a
//   collision") and `if (cannon2.getY() < 0 || ...)` (the cannon2-sunk-
//   below-the-map fallback in the win check) are both genuinely
//   unreachable dead branches in real upstream - not something this port
//   introduced. Ported literally with a plain (signed) `int` comparison
//   rather than deleted outright, so this file stays a faithful line-for-
//   line match of upstream's own real control flow for anyone diffing
//   against it - and the branches stay just as unreachable here, since
//   `bbangTerrainGenerate()`'s own `gbMax()`/`gbMin()` clamps (the direct
//   port of upstream's own `constrain(..., 0, LCDHEIGHT-1)` calls) mean
//   terrain height (and therefore cannon Y) can never actually go negative
//   either way.
// - `Terrain::collision()` computes `terrain[bx-1]` unconditionally
//   (`bx` is real `uint8_t`, so `bx == 0` would wrap to 255 and read/write
//   far outside the real 84-entry array - undefined behavior on real
//   hardware too, not a porting-introduced risk). Confirmed unreachable in
//   real play by tracing the bullet's own x position end-to-end: it is
//   only ever seeded from `cannon1`'s fixed x (10, matching upstream's own
//   `Cannon cannon1(10, 0, 45, 35, true)`) via `bbangBulletShoot()`, and
//   `bbangBulletMove()` only ever increments it - so `bx` is always in
//   `[10, LCDWIDTH-1]` in every real game, never 0. Ported with no added
//   bounds guard, matching upstream's own real code exactly.
//
// A REAL, CONFIRMED UNFINISHED-UPSTREAM DESIGN, PRESERVED AS-IS: despite
// `gamestate` having real `PLAYER2AIMING`/`PLAYER2SHOOTING` values defined
// (and `Cannon cannon2`'s own `dir_right=false` constructor argument
// clearly intended for a second, independently-aimable player), no real
// code path anywhere in upstream ever transitions `gamestate` to either of
// those two values - `inputsgame()` only ever handles
// `PLAYER1AIMING`/(implicitly)`PLAYER1SHOOTING`, and `updategame()` only
// ever checks `PLAYER1SHOOTING`. This is confirmed, not assumed, by
// reading every real assignment to `gamestate` in the source: only 0
// (`PLAYER1AIMING`, in `init_game()`), 1 (`PLAYER1SHOOTING`), and 4
// (`END`) are ever written anywhere. So real upstream "Bang! Bang!" is
// genuinely a single-player game today - cannon2 is a static target that
// only ever repositions itself (sinking as terrain erodes beneath it),
// never aims or fires back - not the local 2-player duel its own name and
// unused enum values suggest it was heading toward. Ported exactly as
// shipped: this port's own `bbangGameState` keeps the same 5 real values
// (`BBANG_PLAYER1AIMING`/`BBANG_PLAYER1SHOOTING`/`BBANG_PLAYER2AIMING`/
// `BBANG_PLAYER2SHOOTING`/`BBANG_END`) for a literal match against
// upstream, with the middle two intentionally as dead as they are in the
// real source - not gameplay this port could invent without exceeding its
// own real source of truth. Marked unfinished in the cartridge's own menu
// (red list text, info "Player 2 never shoots") on direct request, since
// this is a real, permanent gap rather than something to fix - there's no
// real upstream player-2 logic anywhere to restore.
//
// Global naming prefix: `bbang`.

#define BBANG_PI 3.14159265

#define BBANG_CANNONLENGTH 5

#define BBANG_ANGLEMIN 0
#define BBANG_ANGLEMAX 85
#define BBANG_ANGLEDELTA 1

#define BBANG_FORCEMIN 20
#define BBANG_FORCEMAX 120
#define BBANG_FORCEDELTA 2

#define BBANG_PLAYER1AIMING   0
#define BBANG_PLAYER1SHOOTING 1
#define BBANG_PLAYER2AIMING   2
#define BBANG_PLAYER2SHOOTING 3
#define BBANG_END             4

typedef struct{
    int x;
    int y;
    int angle;
    int force;
    bool dirRight;
} BbangCannon;

typedef struct{
    int x;
    int y;
    BbangCannon* c;
} BbangBullet;

enum BbangScreen
{
    BBANG_SCREEN_TITLE = 0,
    BBANG_SCREEN_GAME  = 1,
    BBANG_SCREEN_PAUSE = 2
};

int bbangScreen;
int bbangGameState;

BbangCannon bbangCannons[2];
BbangBullet bbangBullet;
int bbangTerrain[LCDWIDTH];

// Real "Press <arrow icon> to continue" text (drawwin()) - built as an
// explicit int array[] since it needs a real non-printable icon glyph
// (ASCII 21, the same real D-pad-arrow glyph gameTaquin.c's own win screen
// uses) a quoted string literal can't hold directly.
char bbangContinueText[20] =
{
    80, 114, 101, 115, 115, 32, // "Press "
    21,                          // real D-pad-arrow icon glyph
    32, 116, 111, 32, 99, 111, 110, 116, 105, 110, 117, 101, // " to continue"
    0
};

// -----------------------------------------------------------------------
//   Cannon (real Cannon.cpp)
// -----------------------------------------------------------------------

float bbangRadOfDeg( int a )
{
    return ( BBANG_PI * a ) / 180.0;
}

void bbangCannonInit( BbangCannon* c, int x, int y, int angle, int force, bool dirRight )
{
    c->x = x;
    c->y = y;
    c->angle = angle;
    c->force = force;
    c->dirRight = dirRight;
}

void bbangCannonMove( BbangCannon* c, int ypos )
{
    c->y = ypos;
}

void bbangCannonUp( BbangCannon* c )
{
    c->angle = gbMax( BBANG_ANGLEMIN, gbMin( BBANG_ANGLEMAX, c->angle + BBANG_ANGLEDELTA ) );
}

void bbangCannonDown( BbangCannon* c )
{
    c->angle = gbMax( BBANG_ANGLEMIN, gbMin( BBANG_ANGLEMAX, c->angle - BBANG_ANGLEDELTA ) );
}

void bbangCannonLonger( BbangCannon* c )
{
    c->force = gbMax( BBANG_FORCEMIN, gbMin( BBANG_FORCEMAX, c->force + BBANG_FORCEDELTA ) );
}

void bbangCannonShorter( BbangCannon* c )
{
    c->force = gbMax( BBANG_FORCEMIN, gbMin( BBANG_FORCEMAX, c->force - BBANG_FORCEDELTA ) );
}

// Direct port of real Cannon::draw() - the force bar is only ever drawn
// for a right-facing cannon (upstream's own real `if (_dir_right)` guard,
// inside the already-`aiming`-gated block), which in practice means only
// cannon1 (index 0) ever shows it, since cannon2 never reaches an
// `aiming` state at all (see this file's own header comment).
void bbangCannonDraw( BbangCannon* c, bool aiming )
{
    float barrelDx;
    float barrelDy;

    gbSetColor( GB_BLACK );
    gbDrawCircle( c->x, LCDHEIGHT - ( c->y + 2 ), 2 );

    barrelDx = BBANG_CANNONLENGTH * cos( bbangRadOfDeg( c->angle ) );
    barrelDy = BBANG_CANNONLENGTH * sin( bbangRadOfDeg( c->angle ) );

    if( c->dirRight )
    {
        gbDrawLine( c->x - 2, LCDHEIGHT - c->y, c->x, LCDHEIGHT - ( c->y + 2 ) );
        gbDrawLine( c->x, LCDHEIGHT - ( c->y + 2 ),
            c->x + (int)barrelDx, LCDHEIGHT - ( c->y + 2 + (int)barrelDy ) );
    }
    else
    {
        gbDrawLine( c->x + 2, LCDHEIGHT - c->y, c->x, LCDHEIGHT - ( c->y + 2 ) );
        gbDrawLine( c->x, LCDHEIGHT - ( c->y + 2 ),
            c->x - (int)barrelDx, LCDHEIGHT - ( c->y + 2 + (int)barrelDy ) );
    }

    if( aiming )
    {
        int forcebar;

        gbSetColor( GB_GRAY );
        gbDrawTriangle( c->x - 1, LCDHEIGHT - ( c->y - 3 ), c->x, LCDHEIGHT - ( c->y - 2 ), c->x + 1, LCDHEIGHT - ( c->y - 3 ) );

        if( c->dirRight )
        {
            gbSetColor( GB_BLACK );
            gbDrawFastVLine( c->x - 6, LCDHEIGHT - ( c->y + 6 ), 6 );
            gbSetColor( GB_WHITE );
            gbFillRect( c->x - 5, LCDHEIGHT - ( c->y + 6 ), 2, 6 );
            gbSetColor( GB_BLACK );
            forcebar = 5 * ( c->force - BBANG_FORCEMIN ) / ( BBANG_FORCEMAX - BBANG_FORCEMIN ) + 1;
            gbFillRect( c->x - 5, LCDHEIGHT - ( c->y + forcebar ), 2, forcebar );
        }
    }
}

// -----------------------------------------------------------------------
//   Terrain (real Terrain.cpp)
// -----------------------------------------------------------------------

void bbangTerrainInterpolate( int lo, int hi )
{
    int diff;
    int l;
    int i;

    diff = bbangTerrain[ hi ] - bbangTerrain[ lo ];
    l = hi - lo;

    for( i = lo; i < hi; i = i + 1 )
        bbangTerrain[ i ] = bbangTerrain[ lo ] + diff * ( i - lo ) / l;
}

// Direct port of real Terrain::generate() - a real midpoint-displacement
// heightmap, refined over successively smaller segments (`s` from 2 up to
// LCDWIDTH-2) with the per-segment random displacement `d` decaying by a
// real `pow(2, -r)` factor every single inner iteration (confirmed by
// tracing: `d` decays below 1.0 - making every further displacement a real
// no-op via `arand()`'s own `n <= 0` guard, see avrCompat.h - within the
// first few dozen of this loop's real ~3300 total iterations; this is how
// the real algorithm behaves on real hardware too, not a porting
// difference).
void bbangTerrainGenerate()
{
    int s;
    float d;
    float r;
    int segmentsize;
    int i;
    int dInt;

    bbangTerrain[ 0 ] = 5 + arand( 15 );
    bbangTerrain[ LCDWIDTH - 1 ] = 5 + arand( 15 );
    bbangTerrainInterpolate( 0, LCDWIDTH - 1 );

    s = 2;
    d = 20.0;
    r = 0.2;

    while( s < LCDWIDTH - 1 )
    {
        segmentsize = LCDWIDTH / s;

        for( i = 0; i < s - 1; i = i + 1 )
        {
            dInt = (int)d;
            bbangTerrain[ segmentsize * ( i + 1 ) ] = gbMax( 0, gbMin( LCDHEIGHT - 1,
                bbangTerrain[ segmentsize * ( i + 1 ) ] + ( -dInt + arand( 2 * dInt ) ) ) );
            bbangTerrainInterpolate( segmentsize * i, segmentsize * ( i + 1 ) );
            d = d * pow( 2.0, -r );
        }
        bbangTerrainInterpolate( LCDWIDTH - segmentsize, LCDWIDTH - 1 );
        s = s + 1;
    }
}

int bbangTerrainGet( int pos )
{
    return bbangTerrain[ pos ];
}

void bbangTerrainDraw()
{
    int i;

    gbSetColor( GB_BLACK );
    for( i = 0; i < LCDWIDTH; i = i + 1 )
        gbDrawFastVLine( i, LCDHEIGHT - bbangTerrain[ i ], bbangTerrain[ i ] );
}

// Direct port of real Terrain::collision() - see this file's own header
// comment for why `bx - 1` never actually goes out of bounds in real play.
bool bbangTerrainCollision( BbangBullet* b )
{
    int bx;
    int by;

    bx = gbMax( 0, gbMin( LCDWIDTH - 1, b->x ) );
    by = gbMax( 0, gbMin( LCDHEIGHT - 1, b->y ) );

    if( by <= bbangTerrain[ bx ] )
    {
        bbangTerrain[ bx - 1 ] = gbMax( 0, gbMin( LCDHEIGHT, bbangTerrain[ bx - 1 ] - 1 ) );
        bbangTerrain[ bx ] = gbMax( 0, gbMin( LCDHEIGHT, bbangTerrain[ bx ] - 2 ) );
        if( bx < LCDWIDTH - 1 )
            bbangTerrain[ bx + 1 ] = gbMax( 0, gbMin( LCDHEIGHT, bbangTerrain[ bx + 1 ] - 1 ) );
        b->y = bbangTerrain[ bx ];
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
//   Bullet (real Bullet.cpp)
// -----------------------------------------------------------------------

// Direct port of real Bullet::trajectory() - the return value is genuinely
// narrowed from a float expression to a real (signed) integer y position,
// exactly like upstream's own `long int` return type narrowing a float
// expression on its way out.
int bbangBulletTrajectory( BbangBullet* b, int posx )
{
    float x;
    float t;
    float f;

    x = posx - b->c->x;
    t = tan( bbangRadOfDeg( b->c->angle ) );
    f = 5.0 * (float)b->c->force * (float)b->c->force / 100.0;

    return (int)( t * x - ( 1.0 + t * t ) * x * x / f + b->c->y + 2 );
}

void bbangBulletShoot( BbangBullet* b, BbangCannon* c )
{
    b->c = c;
    b->x = c->x;
    b->y = bbangBulletTrajectory( b, b->x );
}

bool bbangBulletMove( BbangBullet* b )
{
    b->x = b->x + 1;
    if( b->x >= LCDWIDTH )
      return false;
    b->y = bbangBulletTrajectory( b, b->x );
    return true;
}

bool bbangBulletOnCannon( BbangBullet* b, BbangCannon* c )
{
    int dx;
    int dy;

    dx = c->x - b->x;
    dy = c->y - b->y;
    return dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2;
}

void bbangBulletDraw( BbangBullet* b )
{
    gbSetColor( GB_BLACK );
    if( b->x < LCDWIDTH && b->y > 0 && b->y < LCDHEIGHT )
      gbFillRect( b->x, LCDHEIGHT - b->y, 2, 2 );
}

// -----------------------------------------------------------------------
//   Game screens (real bangbang.ino)
// -----------------------------------------------------------------------

void bbangInitGame()
{
    bbangGameState = BBANG_PLAYER1AIMING;

    bbangTerrainGenerate();

    bbangCannonMove( &bbangCannons[ 0 ], bbangTerrainGet( 10 ) + 1 );
    bbangCannonMove( &bbangCannons[ 1 ], bbangTerrainGet( LCDWIDTH - 10 ) + 1 );
}

void bbangBeginTitle()
{
    bbangScreen = BBANG_SCREEN_TITLE;
}

void bbangUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;
    gbCursorX = ( LCDWIDTH - 11 * gbFontWidth ) / 2;
    gbCursorY = 16;
    gbPrintString( "BANG! BANG!" );
    gbCursorX = ( LCDWIDTH - 7 * gbFontWidth ) / 2;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        bbangScreen = BBANG_SCREEN_GAME;
        bbangInitGame();
    }
}

// Fixed here, not preserved - see this file's own header comment for the
// real upstream copy-paste bug this replaces (this screen used to print
// "Taquin", not "Bang! Bang!", a genuine leftover from the same author's
// other game).
void bbangUpdatePause()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;
    gbCursorX = ( LCDWIDTH - 11 * gbFontWidth ) / 2;
    gbCursorY = 16;
    gbPrintString( "BANG! BANG!" );
    gbCursorX = ( LCDWIDTH - 7 * gbFontWidth ) / 2;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      bbangScreen = BBANG_SCREEN_GAME;
}

// Direct port of real drawgame().
void bbangDrawGame()
{
    bbangTerrainDraw();
    bbangCannonDraw( &bbangCannons[ 0 ], bbangGameState == BBANG_PLAYER1AIMING );
    bbangCannonDraw( &bbangCannons[ 1 ], bbangGameState == BBANG_PLAYER2AIMING );
    bbangBulletDraw( &bbangBullet );
}

// Direct port of real drawwin().
void bbangDrawWin()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 1;
    gbSetFont( gbFont5x7 );
    gbCursorX = ( LCDWIDTH - 6 * 8 ) / 2;
    gbCursorY = 12;
    gbPrintString( "You win!" );

    gbSetFont( gbFont3x5 );
    gbCursorX = ( LCDWIDTH - 4 * 19 ) / 2;
    gbCursorY = 35;
    gbPrintString( bbangContinueText );

    gbSetFont( gbFont3x5 ); // restore this shim's own real default for every screen that follows
}

// Direct port of real inputsgame(), minus the Button C pause handling
// (hoisted up into bbangUpdateGame() - matching gamePong.c's own
// established treatment for exactly this situation).
void bbangInputsGame()
{
    if( bbangGameState == BBANG_PLAYER1AIMING )
    {
        if( gbPressed( BTN_A ) )
        {
            bbangGameState = BBANG_PLAYER1SHOOTING;
            bbangBulletShoot( &bbangBullet, &bbangCannons[ 0 ] );
        }

        if( gbRepeat( BTN_UP, 1 ) )
          bbangCannonUp( &bbangCannons[ 0 ] );
        else if( gbRepeat( BTN_DOWN, 1 ) )
          bbangCannonDown( &bbangCannons[ 0 ] );
        if( gbRepeat( BTN_LEFT, 1 ) )
          bbangCannonShorter( &bbangCannons[ 0 ] );
        else if( gbRepeat( BTN_RIGHT, 1 ) )
          bbangCannonLonger( &bbangCannons[ 0 ] );
    }
}

// Direct port of real inputswin(), minus the Button C pause handling.
void bbangInputsWin()
{
    if( gbPressed( BTN_A ) )
      bbangInitGame();
}

// Direct port of real updategame() - only ever runs while a bullet is
// actually in flight (`BBANG_PLAYER1SHOOTING`). See this file's own header
// comment for the two real, confirmed-unreachable `< 0` branches ported
// literally below.
void bbangUpdateGameLogic()
{
    int ty;

    if( bbangGameState == BBANG_PLAYER1SHOOTING )
    {
        if( !bbangBulletMove( &bbangBullet ) )
        {
            bbangGameState = BBANG_PLAYER1AIMING;
            return;
        }

        ty = bbangTerrainGet( bbangBullet.x );
        if( ty < 0 && bbangBullet.y <= 0 )
        {
            bbangGameState = BBANG_PLAYER1AIMING;
            return;
        }
        else if( bbangBullet.y < ty )
        {
            bbangTerrainCollision( &bbangBullet );
            bbangCannonMove( &bbangCannons[ 0 ], bbangTerrainGet( 10 ) + 1 );
            bbangCannonMove( &bbangCannons[ 1 ], bbangTerrainGet( LCDWIDTH - 10 ) + 1 );

            if( bbangCannons[ 1 ].y < 0 || bbangBulletOnCannon( &bbangBullet, &bbangCannons[ 1 ] ) )
            {
                bbangGameState = BBANG_END;
                return;
            }
            bbangGameState = BBANG_PLAYER1AIMING;
        }
    }
}

void bbangUpdateGame()
{
    if( gbPressed( BTN_C ) )
    {
        bbangScreen = BBANG_SCREEN_PAUSE;
        return;
    }

    if( bbangGameState == BBANG_END )
    {
        bbangInputsWin();
        bbangDrawWin();
    }
    else
    {
        bbangUpdateGameLogic();
        bbangInputsGame();
        bbangDrawGame();
    }
}

void gameBangBang_init()
{
    gbBegin();

    bbangCannonInit( &bbangCannons[ 0 ], 10, 0, 45, 35, true );
    bbangCannonInit( &bbangCannons[ 1 ], LCDWIDTH - 10, 0, 45, 35, false );

    bbangBeginTitle();
}

void gameBangBang_update()
{
    if( !gbUpdate() ) return;

    if( bbangScreen == BBANG_SCREEN_TITLE ) bbangUpdateTitle();
    else if( bbangScreen == BBANG_SCREEN_PAUSE ) bbangUpdatePause();
    else bbangUpdateGame();

    gbRenderFrame();
}
