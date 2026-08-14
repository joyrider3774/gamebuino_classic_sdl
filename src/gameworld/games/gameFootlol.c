#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// FOOTLOL (FOOTLOL-Gamebuino, Baptiste Pouget, GPLv3). A genuine local
// hot-seat 2-player football/soccer game: 3-vs-3, top-down, physics-based
// (players and the ball collide and bounce off each other with real
// elastic-ish momentum exchange) - both teams share the SAME physical
// button set, turn-by-turn (a board-game-style hotseat design, not split
// controls and not networked), so - like gameBomber.c/gameStickFighter.c/
// gameTron.c before it - nothing needed to be redesigned or dropped for
// this to work as a single-cartridge port.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment). Real `Joueur Joueurs[NJOUEURS]`/
// `Balle balle` (structs-of-floats-with-in-struct member initializers,
// a real C++-only feature) were flattened to plain parallel arrays
// (`flolJx`/`flolJy`/`flolJvx`/`flolJvy`/`flolJequipe`) plus plain float
// globals for the ball, matching this project's own established
// "one real instance -> plain globals/parallel arrays" precedent.
//
// BLOCKING `gb.titleScreen(F("Footuino"))` -> EXPLICIT STATE: real
// upstream's own displayed title text is genuinely "Footuino", not
// "Footlol" (the repo/directory name is the only place "Footlol" appears
// at all) - preserved verbatim, not "corrected" to match the repo name.
// Called once at real boot (`setup()`) and again from `loop()` whenever
// Button C is pressed mid-game (upstream's own real "give up"/restart
// gesture, which also resets the score to 0-0 once the title is
// dismissed again). Ported as `FLOL_STATE_TITLE`/`FLOL_STATE_PLAYING`
// using this project's own established "blocking widget -> explicit
// resumable state" treatment (see gamePong.c's own header comment) - a
// fresh Button A press both resets play (`flolReset()`, this port's own
// name for real upstream `initialiser()`) and the score, matching
// upstream's own real `titleScreen(); initialiser(); ptsB=ptsW=0;` call
// chain exactly (including firing that same reset once at the very first
// launch, since `gameFootlol_init()` already primed initial positions
// before the title state is even shown, exactly mirroring how real
// `setup()` calls `initialiser()` BEFORE its own blocking
// `gb.titleScreen()` call - both platforms end up with valid positions
// already sitting behind the title screen the whole time it's shown).
//
// REAL, PERSISTENT-ACROSS-RESETS GLOBALS, PRESERVED EXACTLY: real
// upstream's own `quiJoue` ("who's playing", default 'w') and `curseur`
// (default 3) are declared at global scope with their own real default
// initializers and are NEVER touched by `initialiser()` itself (only by
// the selection/aim phases below) - so, unlike the ball/player positions
// and the score, whose team's turn it is and which player is currently
// selected both genuinely survive a goal, a Button-C restart, and even a
// fresh title-screen dismissal, all real upstream behavior. Ported the
// same way: `flolQuiJoue`/`flolCurseur` are plain globals with their own
// real default initializers, never written by `flolReset()`.
//
// A REAL, CONFIRMED-BY-TRACING-THE-ACTUAL-INDICES NAMING INVERSION,
// PRESERVED AS-IS: `initialiser()` assigns `equipe='w'` to `Joueurs[0..2]`
// and `equipe='b'` to `Joueurs[3..5]`, but `phaseTir()`'s own selection
// cursor logic picks from `Joueurs[0..2]` (the 'w'-team players) whenever
// `quiJoue=='b'`, and from `Joueurs[3..5]` (the 'b'-team players) whenever
// `quiJoue=='w'` - i.e. `quiJoue` does NOT mean "this team's players are
// now selectable", it means the opposite. Internally self-consistent
// (every one of `curseur`'s own default/wrap values agrees with this
// exact inversion, and `initialiser()`'s own `Nsel` starting angle -
// PI for 'w', 0 for 'b' - lines up with facing the correct goal under
// this same inverted reading) so it is NOT a functional bug to "fix",
// just a real, easy-to-misread upstream naming choice - reproduced by
// mechanical translation of the exact same comparisons/ranges, not
// re-derived from what the names alone would suggest.
//
// A REAL UPSTREAM BUG, DROPPED RATHER THAN PORTED AS DEAD WEIGHT (matching
// this project's own established `echance`/gameArmageddon.c precedent):
// `phaseJeu()`'s own two wall-collision blocks (`if ((Joueurs[i].x -
// RJOUEUR) < TERRAING) { if (...) {...} else { initPosJ[i]; } }`, and the
// mirrored right-wall block) both reference `initPosJ` - a `Pos[]` ARRAY,
// not the real `initPosJ(int)` FUNCTION of the same base name a few lines
// above it - as a bare, side-effect-free expression statement. This is
// real upstream dead code: the array-index expression is computed and
// immediately discarded, so the intended "snap the player back to their
// starting position" behavior this `else` branch was clearly meant to
// have never actually happens on real hardware either. Worth noting this
// is very likely inert in practice regardless: the real upstream
// condition guarding entry into this `else` branch in the first place
// (`(Joueurs[i].y > CAGESH) || (Joueurs[i].y < LCDHEIGHT - CAGESH)`, an
// OR rather than the ball-collision code's own equivalent AND-shaped
// exclusion test) is true for nearly every on-screen `y`, so the
// bounce-back branch above it already handles almost every real
// collision anyway. Since the real `initPosJ(int)` function this dead
// statement was clearly meant to call is (as a direct consequence) never
// genuinely invoked from anywhere in the real program either, it was not
// ported at all, together with the real `posInitJ[]` position-memory
// array that existed solely to feed it - both dropped as one unit, not
// two separate gaps.
//
// A REAL DIALECT-FORCED SUBSTITUTION, NOT A DISCRETIONARY "FIX":
// `phaseJeu()`'s own two collision-response blocks (player-vs-player,
// player-vs-ball) both compute an impact angle as real upstream's own
// hand-rolled `float N = atan(dy/dx); if(dx<0){ N += PI; }` - a classic
// naive two-argument-arctangent reconstruction. This dialect has no plain
// `atan()` at all (only `atan2(y,x)` - confirmed directly against the
// real shipped math.h), and - more importantly - VIRCON32_C_DIALECT.md
// confirms plain integer OR float division by zero genuinely hard-traps
// this CPU, unlike real AVR float division (which just produces
// Infinity/NaN and keeps running). Two players (or a player and the ball)
// can very plausibly collide while sharing the exact same X coordinate
// (`dx==0`), which upstream's own `dy/dx` would hit on every single such
// collision - a real, reachable crash on this platform that real hardware
// never risked. Fixed by computing the angle as `atan2(dy,dx)` directly
// (no division at all) - mathematically the *correct*, full-range version
// of exactly what upstream's own hand-rolled reconstruction was already
// trying to approximate (and, as a side effect, no longer reproduces
// upstream's own minor own quadrant bug where the `+PI` correction is
// applied unconditionally for `dx<0` even when `dy<0`, where it should
// have been `-PI`) - with one small defensive addition real hardware
// didn't need either: `atan2(0,0)` is itself a real, separate hard-trap
// on this dialect (confirmed in VIRCON32_C_DIALECT.md), reachable only in
// the vanishingly-rare case of two colliding objects sharing the exact
// same (x,y), so both collision blocks below skip their own response
// entirely (a plain guard, not a workaround for anything upstream itself
// handles specially) whenever `dx==0 && dy==0`.
//
// A REAL UPSTREAM QUIRK, PRESERVED EXACTLY, NOT "CORRECTED" TO LOOK
// SYMMETRIC: both collision-response blocks compute their own `Vin` value
// by mixing the CURRENT object's own `vx` with the OTHER object's own
// `vy` (`Joueurs[i].vx * cos(N) + Joueurs[j].vy * sin(N)` for the
// player-vs-player block; `Joueurs[i].vx * cos(N) + balle.vy * sin(N)`
// for the player-vs-ball block) rather than both components coming from
// the same object the way the following `Vjn` line does
// (`Joueurs[j].vx...+Joueurs[j].vy...`/`balle.vx...+balle.vy...`, both
// components from the SAME object there). Confirmed this odd asymmetry is
// real, present in both blocks identically (so it reads as a deliberate,
// if unusual, real design choice rather than an isolated typo), and
// reproduced by literal translation rather than "balanced" to match the
// `Vjn` line's own shape.
//
// Real bitmap art: none - every visual (goal posts, center line, player/
// ball circles, the aim line, the score text) is drawn with primitive
// shapes and text, so this port needed no bitmap restoration work at all.

#define FLOL_NJOUEURS 6
#define FLOL_TERRAING 12
#define FLOL_CAGESH 14
#define FLOL_RBALLE 2
#define FLOL_RJOUEUR 3
#define FLOL_FRICTION 0.95
#define FLOL_LFRICTION 0.8
#define FLOL_PI 3.14159265

#define FLOL_TEAM_WHITE 0 // upstream 'w'
#define FLOL_TEAM_BLACK 1 // upstream 'b'

#define FLOL_PHASE_SELECT 0 // upstream 's' - picking which player kicks next
#define FLOL_PHASE_PLAY   1 // upstream 'j' - ball/players physically moving
#define FLOL_PHASE_AIM    2 // upstream 't' - aiming this turn's kick direction/power

#define FLOL_J1X (LCDWIDTH/2-10)
#define FLOL_J1Y (LCDHEIGHT/2-8)
#define FLOL_J2X (LCDWIDTH/2-10)
#define FLOL_J2Y (LCDHEIGHT/2+8)
#define FLOL_J3X (LCDWIDTH/2-20)
#define FLOL_J3Y (LCDHEIGHT/2)
#define FLOL_J4X (LCDWIDTH/2+10)
#define FLOL_J4Y (LCDHEIGHT/2-8)
#define FLOL_J5X (LCDWIDTH/2+10)
#define FLOL_J5Y (LCDHEIGHT/2+8)
#define FLOL_J6X (LCDWIDTH/2+20)
#define FLOL_J6Y (LCDHEIGHT/2)

#define FLOL_STATE_TITLE 0
#define FLOL_STATE_PLAYING 1

int flolState;

int flolPtsB = 0;
int flolPtsW = 0;

// Real upstream globals that are NEVER reset by initialiser()/flolReset() -
// see this file's own header comment.
int flolCompteur = 0;
float flolNsel = FLOL_PI;
float flolDsel = 0;
int flolPhase = FLOL_PHASE_SELECT;
int flolPhaseJFinie = 1;
int flolCurseur = 3;
int flolQuiJoue = FLOL_TEAM_WHITE;

float flolBallX;
float flolBallY;
float flolBallVx;
float flolBallVy;

float flolJx[FLOL_NJOUEURS];
float flolJy[FLOL_NJOUEURS];
float flolJvx[FLOL_NJOUEURS];
float flolJvy[FLOL_NJOUEURS];
int flolJequipe[FLOL_NJOUEURS];

void flolResetV()
{
    int i;

    for( i = 0; i < FLOL_NJOUEURS; i++ )
    {
        flolJvx[ i ] = 0;
        flolJvy[ i ] = 0;
    }
    flolBallVx = 0;
    flolBallVy = 0;
}

// Real upstream `initialiser()` - resets ball/player positions, velocities
// and this turn's aim defaults, but deliberately leaves `flolPhase`/
// `flolPhaseJFinie`/`flolCompteur`/`flolCurseur`/`flolQuiJoue`/the score
// untouched, matching real upstream exactly (see this file's own header
// comment).
void flolReset()
{
    flolBallX = LCDWIDTH / 2;
    flolBallY = LCDHEIGHT / 2;

    flolJx[ 0 ] = FLOL_J1X; flolJy[ 0 ] = FLOL_J1Y; flolJequipe[ 0 ] = FLOL_TEAM_WHITE;
    flolJx[ 1 ] = FLOL_J2X; flolJy[ 1 ] = FLOL_J2Y; flolJequipe[ 1 ] = FLOL_TEAM_WHITE;
    flolJx[ 2 ] = FLOL_J3X; flolJy[ 2 ] = FLOL_J3Y; flolJequipe[ 2 ] = FLOL_TEAM_WHITE;
    flolJx[ 3 ] = FLOL_J4X; flolJy[ 3 ] = FLOL_J4Y; flolJequipe[ 3 ] = FLOL_TEAM_BLACK;
    flolJx[ 4 ] = FLOL_J5X; flolJy[ 4 ] = FLOL_J5Y; flolJequipe[ 4 ] = FLOL_TEAM_BLACK;
    flolJx[ 5 ] = FLOL_J6X; flolJy[ 5 ] = FLOL_J6Y; flolJequipe[ 5 ] = FLOL_TEAM_BLACK;

    flolResetV();

    if( flolQuiJoue == FLOL_TEAM_WHITE )
      flolNsel = FLOL_PI;
    else
      flolNsel = 0;
    flolDsel = 6;
}

void flolDrawDecor()
{
    // Left goal.
    gbDrawLine( FLOL_TERRAING, 0, FLOL_TERRAING, FLOL_CAGESH );
    gbDrawLine( 0, FLOL_CAGESH, FLOL_TERRAING, FLOL_CAGESH );
    gbDrawLine( FLOL_TERRAING, LCDHEIGHT - FLOL_CAGESH, FLOL_TERRAING, LCDHEIGHT );
    gbDrawLine( 0, LCDHEIGHT - FLOL_CAGESH, FLOL_TERRAING, LCDHEIGHT - FLOL_CAGESH );
    // Right goal.
    gbDrawLine( LCDWIDTH - FLOL_TERRAING, 0, LCDWIDTH - FLOL_TERRAING, FLOL_CAGESH );
    gbDrawLine( LCDWIDTH, FLOL_CAGESH, LCDWIDTH - FLOL_TERRAING, FLOL_CAGESH );
    gbDrawLine( LCDWIDTH - FLOL_TERRAING, LCDHEIGHT - FLOL_CAGESH, LCDWIDTH - FLOL_TERRAING, LCDHEIGHT );
    gbDrawLine( LCDWIDTH, LCDHEIGHT - FLOL_CAGESH, LCDWIDTH - FLOL_TERRAING, LCDHEIGHT - FLOL_CAGESH );
    // Center line.
    gbDrawLine( LCDWIDTH / 2, 0, LCDWIDTH / 2, LCDHEIGHT );

    // Score labels + numbers.
    gbFontSize = 2;
    gbDrawChar( 66, FLOL_TERRAING / 2 - 2, LCDHEIGHT - 12 ); // 'B'
    gbDrawChar( 87, LCDWIDTH - FLOL_TERRAING / 2 - 2, LCDHEIGHT - 12 ); // 'W'
    gbFontSize = 1;

    gbCursorX = FLOL_TERRAING / 2 - 4;
    gbCursorY = 4;
    gbPrintNumber( flolPtsB );

    gbCursorX = LCDWIDTH - FLOL_TERRAING / 2 - 4;
    gbCursorY = 4;
    gbPrintNumber( flolPtsW );
}

void flolDrawBall()
{
    gbFillCircle( (int)flolBallX, (int)flolBallY, FLOL_RBALLE );
}

void flolDrawPlayers()
{
    int i;

    for( i = 0; i < FLOL_NJOUEURS; i++ )
    {
        if( flolJequipe[ i ] == FLOL_TEAM_WHITE )
          gbFillCircle( (int)flolJx[ i ], (int)flolJy[ i ], FLOL_RJOUEUR );
        else
          gbDrawCircle( (int)flolJx[ i ], (int)flolJy[ i ], FLOL_RJOUEUR );
    }
}

// Real upstream `phaseTir()` - the "which player kicks next" selection
// phase. See this file's own header comment for the real, confirmed
// `flolQuiJoue`-selects-the-opposite-team's-players naming inversion this
// reproduces exactly.
void flolUpdateSelect()
{
    flolCompteur = 0;

    if( flolQuiJoue == FLOL_TEAM_BLACK )
    {
        if( gbPressed( BTN_RIGHT ) )
        {
            flolCurseur += 1;
            if( flolCurseur >= 3 )
              flolCurseur = 0;
            gbPlayTick();
        }
        if( gbPressed( BTN_LEFT ) )
        {
            flolCurseur -= 1;
            if( flolCurseur < 0 )
              flolCurseur = 2;
            gbPlayTick();
        }
    }
    if( flolQuiJoue == FLOL_TEAM_WHITE )
    {
        if( gbPressed( BTN_RIGHT ) )
        {
            flolCurseur += 1;
            if( flolCurseur >= FLOL_NJOUEURS )
              flolCurseur = 3;
            gbPlayTick();
        }
        if( gbPressed( BTN_LEFT ) )
        {
            flolCurseur -= 1;
            if( flolCurseur < 3 )
              flolCurseur = 5;
            gbPlayTick();
        }
    }

    gbDrawCircle( (int)flolJx[ flolCurseur ], (int)flolJy[ flolCurseur ], FLOL_RJOUEUR + 1 );

    if( gbPressed( BTN_A ) )
      flolPhase = FLOL_PHASE_AIM;
}

// Real upstream `phaseTir2()` - aiming this turn's kick direction/power.
void flolUpdateAim()
{
    float posCx;
    float posCy;

    if( gbRepeat( BTN_RIGHT, 1 ) )
      flolNsel += FLOL_PI / 64;
    if( gbRepeat( BTN_LEFT, 1 ) )
      flolNsel -= FLOL_PI / 64;
    if( gbRepeat( BTN_UP, 1 ) )
      flolDsel += 1;
    if( gbRepeat( BTN_DOWN, 1 ) )
      flolDsel -= 1;
    if( flolDsel > 20 )
      flolDsel -= 1;
    if( flolDsel < 5 )
      flolDsel += 1;

    posCx = flolJx[ flolCurseur ] + flolDsel * cos( flolNsel );
    posCy = flolJy[ flolCurseur ] + flolDsel * sin( flolNsel );
    gbDrawLine( (int)flolJx[ flolCurseur ], (int)flolJy[ flolCurseur ], (int)posCx, (int)posCy );
    flolJvx[ flolCurseur ] = ( flolDsel * cos( flolNsel ) ) / 6;
    flolJvy[ flolCurseur ] = ( flolDsel * sin( flolNsel ) ) / 6;

    if( gbPressed( BTN_B ) )
    {
        flolPhase = FLOL_PHASE_PLAY;
        if( flolQuiJoue == FLOL_TEAM_WHITE )
        {
            flolQuiJoue = FLOL_TEAM_BLACK;
            flolCurseur = 0;
        }
        else
        {
            flolQuiJoue = FLOL_TEAM_WHITE;
            flolCurseur = 3;
        }
        flolNsel += FLOL_PI;
    }
    // Real upstream also has a Button-A check right here that just
    // reassigns `phase = 't'` - already the active phase at this point in
    // real `phaseTir2()`, so this is a real, functionally inert
    // self-reassignment on real hardware too, not something dropped by
    // this port. Reproduced literally for fidelity.
    if( gbPressed( BTN_A ) )
      flolPhase = FLOL_PHASE_AIM;
}

// Real upstream `phaseJeu()` - the physics tick: ball/player movement,
// friction, wall/goal collisions, and player-vs-player/player-vs-ball
// elastic-ish collision response. See this file's own header comment for
// the real dialect-forced atan2() substitution and the real "mixed
// vx/vy" collision-response quirk, both preserved/adapted deliberately,
// not by accident.
void flolUpdatePlay()
{
    int i;
    int j;
    float dx;
    float dy;
    float N;
    float Vin;
    float Vinx;
    float Viny;
    float Vjn;
    float Vjnx;
    float Vjny;

    flolCompteur += 1;
    flolPhaseJFinie = 0;
    Vin = 0; Vinx = 0; Viny = 0; Vjn = 0; Vjnx = 0; Vjny = 0;

    flolBallX += flolBallVx;
    flolBallY += flolBallVy;

    // FRICTION - BALL
    if( ( flolBallVx < -FLOL_LFRICTION ) || ( flolBallVx > FLOL_LFRICTION ) || ( flolBallVy < -FLOL_LFRICTION ) || ( flolBallVy > FLOL_LFRICTION ) )
      flolPhaseJFinie += 1;

    flolBallVx *= FLOL_FRICTION;
    flolBallVy *= FLOL_FRICTION;

    // BALL COLLISION - LEFT
    if( ( flolBallX - FLOL_RBALLE ) < FLOL_TERRAING )
    {
        if( ( flolBallY < FLOL_CAGESH ) || ( flolBallY > LCDHEIGHT - FLOL_CAGESH ) )
        {
            flolBallX += 1;
        }
        else
        {
            flolReset();
            flolPtsW += 1;
            gbPlayOK();
        }
        flolBallVx *= -1;
    }
    // BALL COLLISION - RIGHT
    if( ( flolBallX + FLOL_RBALLE ) > ( LCDWIDTH - FLOL_TERRAING ) )
    {
        if( ( flolBallY < FLOL_CAGESH ) || ( flolBallY > LCDHEIGHT - FLOL_CAGESH ) )
        {
            flolBallX -= 1;
        }
        else
        {
            flolReset();
            flolPtsB += 1;
            gbPlayOK();
        }
        flolBallVx *= -1;
    }
    if( ( flolBallY + FLOL_RBALLE ) > LCDHEIGHT )
    {
        flolBallY -= 1;
        flolBallVy *= -1;
    }
    if( ( flolBallY - FLOL_RBALLE ) < 0 )
    {
        flolBallY += 1;
        flolBallVy *= -1;
    }

    for( i = 0; i < FLOL_NJOUEURS; i++ )
    {
        // FRICTION - PLAYER
        if( ( flolJvx[ i ] < -FLOL_LFRICTION ) || ( flolJvx[ i ] > FLOL_LFRICTION ) || ( flolJvy[ i ] < -FLOL_LFRICTION ) || ( flolJvy[ i ] > FLOL_LFRICTION ) )
          flolPhaseJFinie += 1;

        flolJvx[ i ] *= FLOL_FRICTION;
        flolJvy[ i ] *= FLOL_FRICTION;

        // PLAYER COLLISION - LEFT WALL
        if( ( flolJx[ i ] - FLOL_RJOUEUR ) < FLOL_TERRAING )
        {
            if( ( flolJy[ i ] > FLOL_CAGESH ) || ( flolJy[ i ] < LCDHEIGHT - FLOL_CAGESH ) )
            {
                flolJx[ i ] += 1;
                flolJvx[ i ] *= -1;
            }
            // else: real upstream dead code here, dropped - see this
            // file's own header comment.
        }
        // PLAYER COLLISION - RIGHT WALL
        if( ( flolJx[ i ] + FLOL_RJOUEUR ) > ( LCDWIDTH - FLOL_TERRAING ) )
        {
            if( ( flolJy[ i ] > FLOL_CAGESH ) || ( flolJy[ i ] < LCDHEIGHT - FLOL_CAGESH ) )
            {
                flolJx[ i ] -= 1;
                flolJvx[ i ] *= -1;
            }
            // else: real upstream dead code here, dropped - see this
            // file's own header comment.
        }
        if( ( flolJy[ i ] + FLOL_RJOUEUR ) > LCDHEIGHT )
        {
            flolJy[ i ] -= 1;
            flolJvy[ i ] *= -1;
        }
        if( ( flolJy[ i ] - FLOL_RJOUEUR ) < 0 )
        {
            flolJy[ i ] += 1;
            flolJvy[ i ] *= -1;
        }

        flolJx[ i ] += flolJvx[ i ];
        flolJy[ i ] += flolJvy[ i ];

        // PLAYER-VS-PLAYER COLLISION - real upstream's own odd
        // `if(j==i){j++;} if(j>=NJOUEURS){break;}` skip-self pattern,
        // reproduced literally.
        for( j = 0; j < FLOL_NJOUEURS; j++ )
        {
            if( j == i )
              j++;
            if( j >= FLOL_NJOUEURS )
              break;

            dx = flolJx[ i ] - flolJx[ j ];
            dy = flolJy[ i ] - flolJy[ j ];
            if( ( dx * dx + dy * dy ) < ( ( FLOL_RJOUEUR * 2 ) * ( FLOL_RJOUEUR * 2 ) ) )
            {
                if( !( dx == 0 && dy == 0 ) )
                {
                    N = atan2( dy, dx );

                    // PLAYER I
                    Vin = flolJvx[ i ] * cos( N ) + flolJvy[ j ] * sin( N );
                    Vinx = Vin * cos( N );
                    Viny = Vin * sin( N );
                    // PLAYER J
                    Vjn = flolJvx[ j ] * cos( N ) + flolJvy[ j ] * sin( N );
                    Vjnx = Vjn * cos( N );
                    Vjny = Vjn * sin( N );

                    flolJvx[ i ] += -Vinx + Vjnx;
                    flolJvy[ i ] += -Viny + Vjny;

                    flolJvx[ j ] += -Vjnx + Vinx;
                    flolJvy[ j ] += -Vjny + Viny;

                    // Avoid getting stuck overlapping.
                    flolJx[ i ] = flolJx[ j ] + cos( N ) * ( FLOL_RJOUEUR + FLOL_RJOUEUR );
                    flolJy[ i ] = flolJy[ j ] + sin( N ) * ( FLOL_RJOUEUR + FLOL_RJOUEUR );
                }
            }
        }

        // PLAYER-VS-BALL COLLISION.
        dx = flolJx[ i ] - flolBallX;
        dy = flolJy[ i ] - flolBallY;
        if( ( dx * dx + dy * dy ) < ( ( FLOL_RJOUEUR + FLOL_RBALLE ) * ( FLOL_RJOUEUR + FLOL_RBALLE ) ) )
        {
            if( !( dx == 0 && dy == 0 ) )
            {
                N = atan2( dy, dx );

                // PLAYER I
                Vin = flolJvx[ i ] * cos( N ) + flolBallVy * sin( N );
                Vinx = Vin * cos( N );
                Viny = Vin * sin( N );
                // BALL
                Vjn = flolBallVx * cos( N ) + flolBallVy * sin( N );
                Vjnx = Vjn * cos( N );
                Vjny = Vjn * sin( N );

                flolJvx[ i ] += -Vinx + Vjnx;
                flolJvy[ i ] += -Viny + Vjny;

                flolBallVx += -Vjnx + Vinx;
                flolBallVy += -Vjny + Viny;

                // Avoid getting stuck overlapping.
                flolJx[ i ] = flolBallX + cos( N ) * ( FLOL_RJOUEUR + FLOL_RBALLE );
                flolJy[ i ] = flolBallY + sin( N ) * ( FLOL_RJOUEUR + FLOL_RBALLE );
            }
        }
    }
}

// Real upstream `gPhases()` - three plain sequential `if` checks (NOT
// else-if): the settle-timeout check can flip `flolPhase` from PLAY to
// SELECT, and the dispatch below it must still fire THIS SAME TICK once
// it does, exactly matching real upstream's own same-tick fallthrough.
void flolPhases()
{
    if( ( flolPhaseJFinie == 0 ) && ( flolCompteur > 60 ) )
    {
        flolResetV();
        flolPhase = FLOL_PHASE_SELECT;
    }
    if( flolPhase == FLOL_PHASE_PLAY )
      flolUpdatePlay();
    if( flolPhase == FLOL_PHASE_SELECT )
      flolUpdateSelect();
    if( flolPhase == FLOL_PHASE_AIM )
      flolUpdateAim();
}

void flolUpdateTitle()
{
    gbSetColor( 1 );
    gbCursorX = 4;
    gbCursorY = 16;
    gbPrintString( "Footuino" );
    gbCursorX = 4;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        flolReset();
        flolPtsB = 0;
        flolPtsW = 0;
        flolState = FLOL_STATE_PLAYING;
    }
}

void flolUpdatePlaying()
{
    flolDrawDecor();
    flolDrawBall();
    flolDrawPlayers();
    flolPhases();

    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        flolState = FLOL_STATE_TITLE;
    }
}

void gameFootlol_init()
{
    gbBegin();
    flolState = FLOL_STATE_TITLE;
    flolReset();
}

void gameFootlol_update()
{
    if( !gbUpdate() ) return;

    if( flolState == FLOL_STATE_PLAYING )
      flolUpdatePlaying();
    else
      flolUpdateTitle();

    gbRenderFrame();
}
