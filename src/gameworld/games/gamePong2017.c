#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Pong 2017 / "Pong Revisited" (yawn-g, https://github.com/yawn-g/pong-2017,
// no license file / license specified in the repo). A single-player Pong
// against a simple ball-tracking AI opponent, with a real per-player
// "tricks" power-up menu (Button B opens/closes it, Left/Right pick a
// trick, Button A activates the highlighted one). Ported from the real
// upstream source: pong-2017.ino (setup()/loop()), Ball.ino, Player.ino,
// functions.ino (drawBackground()). RectObstacle.ino was read too but is
// genuinely dead code upstream - `class RectObstacle` is declared and its
// constructor/smoothMove() are defined, but no RectObstacle instance is
// ever created anywhere in the real game (confirmed by grep across every
// real .ino file) - so nothing from it is ported, there is nothing to
// port. The unrelated other/test-i2c/ subfolder (a standalone I2C hardware
// test sketch, not part of the real game) was not read for porting
// purposes. Upstream's own stray `#include <Wire.h>` and forward-declared-
// but-never-defined-or-called `void masterWrite();` are real, confirmed
// dead vestiges (neither `masterWrite` nor `Wire.` is ever actually used
// anywhere in the real game code) and were simply dropped.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` call (this dialect has no classes - see gamebuinoShim.h's own
// header comment). Upstream's `Player`/`Ball` C++ classes (each with a
// constructor and a handful of methods, `Player player[2];` a global
// array of class instances) were flattened into plain globals and
// parallel `int[2]`/`int[2][5]` arrays indexed by player id (0 = human,
// 1 = AI) - the same "flatten a real single-instance C++ class into plain
// C globals/parallel arrays" treatment already established throughout
// this project (see gamePunkt.c's own header comment for the original
// precedent). Upstream's blocking `gb.titleScreen(F("PONG 2017"))` - called
// once in setup(), and again from inside loop() as a genuine "pause the
// game" gesture on Button C, exactly like gamePong.c's own already-shipped
// precedent - became an explicit PONG17_STATE_TITLE state (shown at boot
// and re-entered on a C press mid-game, dismissed by a fresh Button A
// press, matching real `titleScreen()`'s own real dismiss button). Real
// `gb.titleScreen()` draws its own title-box UI this shim has no generic
// equivalent for; a plain "PONG 2017" / "PRESS A" text screen was
// hand-built instead, the same simplification gamePong.c's own port
// already established. Neither the title state nor the Button-C pause
// resets any game state (ball/pad position, active tricks) - genuinely
// matching real upstream, whose own blocking `titleScreen()` call is
// simply interposed into the same running `loop()`, nothing is ever
// re-initialized by it.
//
// Ball/paddle velocities are ported as plain `int`, not `float`: every
// real upstream velocity field (`Ball::xSpeed`/`ySpeed`, `Player::xSpeed`/
// `ySpeed`) is declared `float` but is, in the entire real source, only
// ever assigned the literal values 1, -1, 2 or -2 (`ySpeed` in particular
// is set once, to exactly 2, in `Player::Player()`, and never reassigned
// anywhere) - so representing them as `int` reproduces the real values
// bit-for-bit, it is not an approximation.
//
// Upstream's `tricks[5]` per-player trick loadout (which real trick ID
// occupies each of the 5 menu slots) is assigned identically for both
// players in real `Player::initPlayer()` (`{EXPAND_PAD, BORDERS,
// EXTRA_PAD, FREEZE, INVISIBALL}` regardless of `id`), so this port uses
// one shared `pong17TrickIds[5]` array instead of a genuinely-duplicated
// per-player copy - a faithful simplification, not a behavior change,
// since the two real per-player copies could never actually diverge.
// Likewise `Player::tricksMenuOn`/`tricksCursor` are only ever read or
// written for `player[0]` anywhere in the real source (the trick menu is
// human-player-only - the real AI opponent, `player[1]`, never opens a
// menu or ever gets any `trickOn[]` flag set by anything), so this port
// keeps them as plain scalars (`pong17TricksMenuOn`/`pong17TricksCursor`)
// rather than a genuinely-unused-half `[2]` array - again a faithful
// simplification of real, confirmed-always-player-0-only state, not new
// behavior. `Player::trickOn[5]`/private `trickFC[5]` (per-slot active
// flag / frame counter) DO stay real `[2][5]` arrays, since real upstream
// calls `updateTricks()` for both players every tick even though only
// player 0's flags are ever actually set true in practice.
//
// Three real upstream `Player`/global fields are genuinely dead code -
// confirmed by grepping every real .ino file for each name - and are
// intentionally NOT ported at all (there is nothing observable to
// preserve): `Player::health` (assigned once from the real `HEALTH` macro
// in the constructor, never read anywhere after that - the on-screen
// "life gauge" bars in `drawBackground()` are drawn at a fixed, constant
// width matching the `HEALTH` macro's own literal value, not scaled by
// this field at all), `Player::points` (never assigned or read anywhere),
// `Player::selectedTrick` (only ever assigned, in `loop()`'s own Button-A
// handler, never read anywhere - upstream's own comment on this field's
// declaration literally says "peut-être à supprimer", French for "maybe
// to delete", i.e. the original author already suspected this themself),
// and the global `nbRounds` (declared and initialized, never read
// anywhere). `RectObstacle` (see above) is the fourth and largest such
// case.
//
// **A real, confirmed color-state bug in real upstream's own
// `drawBackground()`, preserved exactly, not fixed**: the function calls
// `gb.display.setColor(BLACK)` once for the top/bottom border lines, then
// `gb.display.setColor(GRAY)` for the center net - and never sets the
// color back to BLACK afterward. Every subsequent draw in that same
// function (both player-name strings, both life-gauge bars, both rounds-
// won bars, and the active-trick-icon HUD row) is therefore drawn in
// GRAY, not BLACK, on real hardware - almost certainly an unintentional
// upstream bug (forgetting to reset the color), but a real, load-bearing
// part of what the actual shipped game looks like. Preserved by simply
// never calling `gbSetColor()` at any of those real call sites either,
// exactly mirroring upstream - `gbSetColor()`/`gbColor` persist across
// calls here exactly like real `Display::color` does, so the same "bug"
// reproduces naturally from a literal, in-order translation, with no
// special-casing needed. This color leak also reaches past
// `drawBackground()` into `Player::updateTricksMenu()` (which likewise
// never sets its own color) - real upstream's `Ball::draw()` sets BLACK
// right before it, but only inside its own `if (ballVisible)` guard, so
// whenever the INVISIBALL trick is active (ball hidden), the trick-menu
// cursor/icon row draws in GRAY instead of BLACK too. `Player::drawPad()`
// is the one real exception - it explicitly sets BLACK itself every call,
// so the paddles are always solid black regardless. This port reproduces
// every one of these calls in the exact same order upstream does, so the
// same call-order-dependent color state naturally falls out here too.
//
// CONFIRMED, NOT JUST THEORIZED: unlike the superficially-similar color-
// leak bug once suspected in gameSuperCrateBuino.c (see that file's own
// header comment for the full, still-unresolved investigation - live
// testing there directly contradicted the source-level trace), this one
// was independently confirmed live by the user directly, via a real
// screenshot from an actual emulator running real upstream's own
// unmodified source - the net, dashed round-indicator lines, and both
// player-name labels ("YOU"/the opponent name) all genuinely render in a
// dithered GRAY, not solid BLACK, exactly matching this port's own
// preserved behavior. Left preserved, not fixed.
//
// **A second real, confirmed bug preserved exactly**: real
// `Player::updateTricks()` unconditionally runs `trickFC[t]++` after its
// own if/else, even along the branch that just reset `trickFC[t] = 0`
// (because the trick's duration had just elapsed) - so a trick's frame
// counter actually ends each activation at 1, not 0. The first time any
// given trick slot is activated this is invisible (a global array starts
// zero-initialized either way), but every activation after the first
// starts counting from 1, making that trick's real effective duration one
// frame shorter than its own `trickDuration[]` entry from the second use
// onward. Reproduced verbatim (`pong17TrickFC[id][t]++` runs
// unconditionally after the if/else, exactly like upstream).
//
// **A real upstream gap, not a bug, preserved as-is**: of the 5 real
// trick IDs actually placed into a menu slot (`EXPAND_PAD`/`BORDERS`/
// `EXTRA_PAD`/`FREEZE`/`INVISIBALL`), real `Player::updateTricks()` only
// actually implements an effect for 2 of them (`EXPAND_PAD` widens the
// paddle to 11px while active; `INVISIBALL` hides the ball while active).
// `BORDERS`/`EXTRA_PAD`/`FREEZE` have a real menu icon, a real
// `trickDuration[]` entry, and get a real, correctly-timed on/off cycle
// (`trickOn[t]`, the active-trick HUD icon, the frame-counter expiry) -
// but the real `switch` in `updateTricks()` simply has no `case` for any
// of them, so selecting one visibly does nothing beyond showing its own
// icon in the HUD row for its duration. A sixth defined trick ID,
// `SHRINK_PAD`, isn't even placed into any menu slot by real
// `initPlayer()`, so it's entirely unreachable in play. This reads as a
// genuinely unfinished feature in the real shipped game, not a porting
// mistake - preserved exactly as shipped, not completed.
//
// Real `Player::padTop()` calls `floor(padHeight/2)` - a real no-op double
// truncation upstream, since `padHeight/2` (already `byte / int`,
// integer division) is truncated before `floor()` ever sees it. Ported as
// plain integer division (`pong17PadHeight[id] / 2`), identical result,
// no `floor()` call needed.
//
// Upstream's own obscure boolean-toggle idiom for Button B
// (`tricksMenuOn = -tricksMenuOn + 1;`, exploiting `-true == -1`/
// `-false == 0`) was rewritten as a plain `if/else` toggle - mechanically
// identical output, just not relying on this dialect's bool-to-int
// arithmetic promotion behaving the same way real AVR C++ does (never
// verified either way, and there's no reason to depend on it here).
//
// This game has no genuine highscore/best-round/win-tracking concept at
// all to persist: there is no `#include <EEPROM.h>` anywhere upstream, no
// score/round value is ever actually incremented or compared against a
// win condition in any of the real source files, and `roundsWon` (drawn
// as the on-screen "rounds won" bar) is a static value both players are
// simply initialized to (5) and never modified again - so no EEPROM
// wiring was added here, matching this project's own established "don't
// invent a highscore concept real upstream never had" precedent.
//
// Real `gb.display.setFont(font3x3)` is called once in `setup()` and
// never changed - ported directly as one `gbSetFont(gbFont3x3)` call in
// `gamePong2017_init()`. All of the trick-icon characters this game
// prints (`'X'`=88 EXPAND_PAD, `'='`=61 BORDERS, `'C'`=67 EXTRA_PAD,
// `'F'`=70 FREEZE, `'*'`=42 INVISIBALL) are ordinary printable ASCII, so
// they need no special int[]-array workaround, unlike the real Gamebuino
// icon-glyph range (ASCII 0-31) other ports have needed that treatment
// for - only the trick-menu's own small "^" cursor glyph (real `char(25)`,
// ASCII 25, in the icon-glyph range) needs a direct `gbDrawChar()` call
// rather than a plain string literal.

#define PONG17_PAD_HEIGHT 7
#define PONG17_PAD_SPEED 2
#define PONG17_TRICKS_PER_PLAYER 5
#define PONG17_BALL_WIDTH 2

// Trick IDs - real upstream's own #defines, ASCII code doubling as the
// on-screen icon character for each one.
#define PONG17_TRICK_INVISIBALL 42  // *
#define PONG17_TRICK_BORDERS 61     // =
#define PONG17_TRICK_EXTRA_PAD 67   // C
#define PONG17_TRICK_FREEZE 70      // F
#define PONG17_TRICK_SHRINK_PAD 72  // H (real, but never placed into any menu slot - unreachable, see header comment)
#define PONG17_TRICK_EXPAND_PAD 88  // X

// Real upstream's own trickDuration[128] table, ported verbatim - indexed
// directly by trick ID (ASCII code), not by menu slot.
int pong17TrickDuration[128] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 0-9
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 10-19
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 20-29
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 30-39
    10, 10, 25, 10, 10, 10, 10, 10, 10, 10, // 40-49
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 50-59
    10, 60, 10, 10, 10, 10, 10, 60, 10, 10, // 60-69
    20, 10, 60, 10, 10, 10, 10, 10, 10, 10, // 70-79
    10, 10, 10, 10, 10, 10, 10, 10, 60, 10, // 80-89
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 90-99
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 100-109
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, // 110-119
    10, 10, 10, 10, 10, 10, 10, 10           // 120-127
};

// Shared per-slot trick loadout (identical for both players upstream -
// see header comment).
int pong17TrickIds[5] = {
    PONG17_TRICK_EXPAND_PAD,
    PONG17_TRICK_BORDERS,
    PONG17_TRICK_EXTRA_PAD,
    PONG17_TRICK_FREEZE,
    PONG17_TRICK_INVISIBALL
};

// Real Gamebuino font3x3 icon glyph ASCII 25 - a small up-arrow used as
// the trick-menu's own cursor indicator.
#define PONG17_ICON_CURSOR 25

// Ball state (id-less - only one ball).
int pong17BallX = 41;
int pong17BallY = 23;
int pong17BallXSpeed = 1;
int pong17BallYSpeed = 1;
bool pong17BallVisible = true;

// Real upstream global toggle - declared, never actually set true
// anywhere in the real source, so `drawBackground()`'s own alternate
// "hide health bar near the top" layout is real but permanently
// unreachable. Preserved as a real (inert) global for fidelity.
bool pong17HideHealth = false;

// Per-player state, indexed by player id: 0 = human (left paddle), 1 = AI
// (right paddle, tracks the ball's y every tick).
int pong17X[2];
int pong17Y[2];
int pong17PadWidth[2];
int pong17PadHeight[2];
int pong17RoundsWon[2];
int pong17RoundsBarDir[2];
int pong17RoundsBarX[2];
int pong17TrickOn[2][5];
int pong17TrickFC[2][5];

// Human-player-only trick-menu state (see header comment).
bool pong17TricksMenuOn = false;
int pong17TricksCursor = 2;

enum Pong17State
{
    PONG17_STATE_TITLE = 0,
    PONG17_STATE_PLAY = 1
};

int pong17State;

void pong17InitPlayer( int id )
{
    pong17Y[id] = 23;
    pong17PadHeight[id] = PONG17_PAD_HEIGHT;
    pong17PadWidth[id] = 2;
    pong17RoundsWon[id] = 5;

    if( id == 0 )
    {
        pong17X[id] = 0;
        pong17RoundsBarX[id] = 36;
        pong17RoundsBarDir[id] = -1;
    }
    else
    {
        pong17X[id] = 82;
        pong17RoundsBarX[id] = 44;
        pong17RoundsBarDir[id] = 1;
    }
}

int pong17PadTop( int id )
{
    return pong17Y[id] - ( pong17PadHeight[id] / 2 );
}

void pong17DrawPad( int id )
{
    gbSetColor( GB_BLACK );
    gbFillRect( pong17X[id], pong17PadTop( id ), pong17PadWidth[id], pong17PadHeight[id] );
}

void pong17MoveUp( int id )
{
    if( pong17PadTop( id ) > 1 )
      pong17Y[id] = pong17Y[id] - PONG17_PAD_SPEED;
}

void pong17MoveDown( int id )
{
    if( ( pong17PadTop( id ) + pong17PadHeight[id] ) < ( LCDHEIGHT - 2 ) )
      pong17Y[id] = pong17Y[id] + PONG17_PAD_SPEED;
}

// Real Player::updateTricksMenu() - human player (id 0) only, real
// upstream never calls this for the AI.
void pong17UpdateTricksMenu()
{
    if( pong17TricksMenuOn )
    {
        // real upstream's "^" cursor glyph, real println() newline landing
        // exactly at y=43 (39 + font3x3's own real per-glyph height of 4) -
        // the same y the active-trick HUD row below already uses.
        gbDrawChar( PONG17_ICON_CURSOR, 10 + pong17TricksCursor * 4, 39 );
        gbCursorX = 10;
        gbCursorY = 43;
        gbPrintString( "X=CF*" );
    }
}

// Real Player::updateTricks() - called every tick for both players.
void pong17UpdateTricks( int id )
{
    int t;
    for( t = 0; t < PONG17_TRICKS_PER_PLAYER; t++ )
    {
        if( pong17TrickOn[id][t] )
        {
            int trickId = pong17TrickIds[t];

            if( pong17TrickFC[id][t] == pong17TrickDuration[trickId] )
            {
                // duration elapsed - restore state
                if( trickId == PONG17_TRICK_EXPAND_PAD )
                  pong17PadHeight[id] = PONG17_PAD_HEIGHT;
                if( trickId == PONG17_TRICK_INVISIBALL )
                  pong17BallVisible = true;

                pong17TrickOn[id][t] = false;
                pong17TrickFC[id][t] = 0;
            }
            else
            {
                // duration not yet elapsed - apply effect
                if( trickId == PONG17_TRICK_EXPAND_PAD )
                  pong17PadHeight[id] = 11;
                if( trickId == PONG17_TRICK_INVISIBALL )
                  pong17BallVisible = false;
            }

            // real upstream's own unconditional increment - see header
            // comment on the real "ends at 1, not 0" bug this preserves.
            pong17TrickFC[id][t]++;
        }
    }
}

void pong17BallDraw()
{
    if( pong17BallVisible )
    {
        gbSetColor( GB_BLACK );
        gbFillRect( pong17BallX, pong17BallY, PONG17_BALL_WIDTH, PONG17_BALL_WIDTH );
    }
}

void pong17BallMove()
{
    if( pong17BallXSpeed > 0 )
    {
        if( pong17BallX < ( LCDWIDTH - 2 ) )
          pong17BallX = pong17BallX + pong17BallXSpeed;
        else
          pong17BallXSpeed = -pong17BallXSpeed;
    }
    if( pong17BallXSpeed < 0 )
    {
        if( pong17BallX > 0 )
          pong17BallX = pong17BallX + pong17BallXSpeed;
        else
          pong17BallXSpeed = -pong17BallXSpeed;
    }
    if( pong17BallYSpeed > 0 )
    {
        if( pong17BallY < ( LCDHEIGHT - 2 ) )
          pong17BallY = pong17BallY + pong17BallYSpeed;
        else
          pong17BallYSpeed = -pong17BallYSpeed;
    }
    if( pong17BallYSpeed < 0 )
    {
        if( pong17BallY > 1 )
          pong17BallY = pong17BallY + pong17BallYSpeed;
        else
          pong17BallYSpeed = -pong17BallYSpeed;
    }
}

// Real Ball::padCollide() - no position correction on hit (upstream's own
// re-collide-move() call is commented out), and only the x velocity
// reflects (not y) - both real, preserved exactly.
void pong17BallPadCollide()
{
    int p;
    for( p = 0; p < 2; p++ )
    {
        if( gbCollideRectRect( pong17BallX, pong17BallY, PONG17_BALL_WIDTH, PONG17_BALL_WIDTH,
            pong17X[p], pong17PadTop( p ), pong17PadWidth[p], pong17PadHeight[p] ) )
        {
            gbPlayTick();
            pong17BallXSpeed = -pong17BallXSpeed;
        }
    }
}

// Real functions.ino's own drawBackground() - see header comment for the
// real, preserved color-leak bug this reproduces by simply never calling
// gbSetColor() at any of the same call sites upstream doesn't either.
void pong17DrawBackground()
{
    int yOffset;
    if( ( pong17BallY < 8 ) && pong17HideHealth )
      yOffset = pong17BallY;
    else
      yOffset = 8;

    gbClear();

    // top & bottom borders
    gbSetColor( GB_BLACK );
    gbDrawFastHLine( 0, 0, LCDWIDTH );
    gbDrawFastHLine( 0, 47, LCDWIDTH );

    // net
    gbSetColor( GB_GRAY );
    gbFillRect( 41, 1, 2, 46 );

    // names (drawn in GRAY - see header comment)
    gbCursorX = 29;
    gbCursorY = 2 - ( 8 - yOffset );
    gbPrintString( "YQN" );
    gbCursorX = 44;
    gbPrintString( "RLF" );

    // life gauges (static width, matching real HEALTH=27 - see header
    // comment on the real dead Player::health field)
    gbFillRect( 1, 2 - ( 8 - yOffset ), 27, 3 );
    gbDrawPixel( 0, 3 - ( 8 - yOffset ) );
    gbFillRect( 56, 2 - ( 8 - yOffset ), 27, 3 );
    gbDrawPixel( 83, 3 - ( 8 - yOffset ) );

    // rounds won
    int p;
    for( p = 0; p < 2; p++ )
    {
        int i;
        for( i = 0; i < pong17RoundsWon[p]; i++ )
        {
            int dir = pong17RoundsBarDir[p];
            int x = pong17RoundsBarX[p] + i * 4 * dir;
            gbDrawFastHLine( x, 6 - ( 8 - yOffset ), 3 );
        }
    }

    // active tricks HUD row
    for( p = 0; p < 2; p++ )
    {
        int i;
        for( i = 0; i < PONG17_TRICKS_PER_PLAYER; i++ )
        {
            if( pong17TrickOn[p][i] )
              gbDrawChar( pong17TrickIds[i], 10 + i * 4, 43 );
        }
    }
}

void pong17BeginTitle()
{
    pong17State = PONG17_STATE_TITLE;
}

void pong17BeginPlay()
{
    pong17State = PONG17_STATE_PLAY;
}

void pong17UpdateTitleState()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 24;
    gbCursorY = 18;
    gbPrintString( "PONG 2017" );
    gbCursorX = 28;
    gbCursorY = 28;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      pong17BeginPlay();
}

void pong17UpdatePlayState()
{
    pong17DrawBackground();

    pong17UpdateTricks( 0 );
    pong17UpdateTricks( 1 );

    pong17BallMove();
    pong17BallPadCollide();
    pong17BallDraw();

    pong17UpdateTricksMenu();

    pong17DrawPad( 0 );
    pong17DrawPad( 1 );

    // move the AI (player 1) - simple ball-y tracking, no smoothing
    if( pong17BallY > pong17Y[1] )
      pong17MoveDown( 1 );
    if( pong17BallY < pong17Y[1] )
      pong17MoveUp( 1 );

    if( gbRepeat( BTN_UP, 1 ) )
      pong17MoveUp( 0 );
    if( gbRepeat( BTN_DOWN, 1 ) )
      pong17MoveDown( 0 );

    if( gbPressed( BTN_LEFT ) && pong17TricksMenuOn )
    {
        if( pong17TricksCursor > 0 )
          pong17TricksCursor--;
        else
          pong17TricksCursor = PONG17_TRICKS_PER_PLAYER - 1;
    }
    if( gbPressed( BTN_RIGHT ) && pong17TricksMenuOn )
    {
        if( pong17TricksCursor < ( PONG17_TRICKS_PER_PLAYER - 1 ) )
          pong17TricksCursor++;
        else
          pong17TricksCursor = 0;
    }

    if( gbPressed( BTN_B ) )
    {
        if( pong17TricksMenuOn )
          pong17TricksMenuOn = false;
        else
          pong17TricksMenuOn = true;
    }

    if( gbPressed( BTN_A ) && pong17TricksMenuOn )
    {
        gbPlayOK();
        pong17TricksMenuOn = false;
        pong17TrickOn[0][pong17TricksCursor] = true;
    }

    if( gbPressed( BTN_C ) )
      pong17BeginTitle();
}

void gamePong2017_init()
{
    gbBegin();
    gbSetFont( gbFont3x3 ); // real upstream's own setup()-time gb.display.setFont(font3x3) - stays set for the whole game

    pong17InitPlayer( 0 );
    pong17InitPlayer( 1 );

    pong17BeginTitle();
}

void gamePong2017_update()
{
    if( !gbUpdate() ) return;

    if( pong17State == PONG17_STATE_TITLE )
      pong17UpdateTitleState();
    else
      pong17UpdatePlayState();

    gbRenderFrame();
}
