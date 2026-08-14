#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Aimbuino ("Bsktuino", Baptiste Pouget, GPLv3 - github.com/ogbaba/Aimbuino,
// a real 2016 school project, "ISN Project 2016, Lorgues"). A basketball-
// style aiming game: aim a launch angle/power at a small target ("Cible")
// that respawns at a random position, then release Button A to fire a ball
// along a real gravity-arc trajectory; a hit scores a point and respawns
// both ball and target, a miss (ball leaves the screen) ends the run in
// Free Mode but is free in Challenge Mode, which instead runs against a
// real 60-second countdown. A small 3-entry top score table is kept for
// each mode.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment). Real upstream's own `Balle`/`Save`/
// `Score` structs (each real field given a real inline default-member
// initializer, a C++-only construct this dialect's own plain-C structs
// don't support) were flattened to plain `aimb`-prefixed globals instead -
// matching this project's own established "flatten a real single-instance
// struct into plain C globals" precedent - with defaults assigned
// explicitly in `gameAimbuino_init()`/`aimbInitJeu()` instead of inline.
//
// BLOCKING `gb.titleScreen(F("AimBuino"))` (called once from real `setup()`,
// and again for real upstream's own "Exit" menu entry) -> two explicit,
// resumable states (`AIMB_STATE_TITLE`, dismissed by a fresh Button A press
// exactly like every other ported title screen in this project) and
// `AIMB_STATE_MENU` for upstream's own real `gb.menu(menu,4)` widget (no
// equivalent primitive in this shim - see gamebuinoShim.h's own real API
// surface), hand-rolled the same way gameConduit.c's own `condUpdateMenu()`
// already established for this project (an up/down cursor list + Button A
// to confirm). Selecting "Exit" returns to `AIMB_STATE_TITLE` exactly like
// real upstream's own `case 3` (re-show the title screen, `gameMode` itself
// stays 'm' so real hardware's own next loop() iteration shows the menu
// again once A is pressed) - functionally identical here since re-entering
// `AIMB_STATE_MENU` needs the same fresh Button A press either way.
//
// NAME ENTRY - DROPPED, DOCUMENTED: real upstream's own `gb.keyboard(...)`
// (an on-screen text-entry widget used to name a new top-3 entry) has no
// equivalent in this shim (confirmed against gamebuinoShim.h's full real API
// surface - no keyboard/text-input primitive exists at all) - matching this
// project's own established `gameUfoRace.c` precedent, per-entry name
// storage was dropped entirely rather than faked with a placeholder string;
// `aimbLbChallenge[3]`/`aimbLbFree[3]` are plain scores-only tables (real
// upstream's own `Score.pseudo`/`Score.mode` fields have no port here at
// all, since `mode` was only ever used to label which table an entry
// belonged to - now implicit in which of the two arrays holds it).
//
// A REAL, PRESERVED UPSTREAM BUG: real upstream's own aim-angle reset,
// `NViser = (1/4)*PI;` (identical in both the global initializer and
// `initJeu()`), computes `(1/4)` as a genuine C integer division of two
// int literals - 0, not 0.25 - so the whole expression is always exactly
// `0.0`, not the quarter-turn the "(1/4)" clearly intends. Every fresh aim
// therefore starts pointing due right (angle 0) rather than up-and-right at
// 45 degrees. This is real, directly player-visible gameplay behavior (not
// an internal-only slip), so it's reproduced bit-for-bit here
// (`aimbAimAngle = ( 1 / 4 ) * AIMB_PI;`, relying on this dialect's own
// identical int/int truncation rule) rather than "fixed" to what was
// probably intended.
//
// FLOAT COUNTDOWN DISPLAY - SIMPLIFIED, DOCUMENTED: real upstream's own
// `challenge()` prints a genuine float (`60.0 - (tpsF/20.0)`, real Arduino
// `Print::print(float)` showing two decimal digits) - this shim has no
// float-printing primitive at all (only `gbPrintNumber(int)` - see
// gamebuinoShim.h), so the remaining time is shown as a plain whole-second
// countdown (`(AIMB_CHALLENGE_TICKS - tpsF) / 20`) instead of upstream's
// own two-decimal display; the real 60-second/1200-tick deadline itself
// (`60*20`, this game's own real 20fps default - it never calls
// `gb.setFrameRate()`) is unchanged.
//
// `Cible[]` (the 8x8 target bitmap, real upstream `B01111110`-style binary
// literals) was converted to hex byte-for-byte (`B01111110` -> `0x7E` etc)
// into `aimbTargetBitmap`, drawn with this shim's own real `gbDrawBitmap()`
// primitive exactly like upstream's own `gb.display.drawBitmap(...)` call.
//
// EEPROM: real upstream's own `restoreData()`/`saveData()` raw-byte-copy
// the entire `Save` struct (`sizeof(save)` bytes, via `EEPROM.read/write` in
// a loop) - ported using this project's own established
// `eeprom_read_word()`/`eeprom_write_word()` + explicit `==0xFFFF`
// fresh-cell check pattern instead (see this project's own CLAUDE.md EEPROM
// audit section for exactly why a raw 0xFFFF sentinel can't be trusted as a
// real score) - 3 words for `aimbLbChallenge` at addresses 0/2/4, 3 more
// for `aimbLbFree` at 6/8/10. `aimbHandleLoss()` reloads from EEPROM right
// before evaluating the just-finished run's score (matching real upstream's
// own `restoreData()` call at the exact moment `perdu` becomes true) and
// only writes back if the run's score actually earned a new table entry -
// matching real upstream's own real `saveData()` call site, which likewise
// only ever runs inside that same conditional.
//
// Real upstream never calls `gb.setFont()` anywhere, so it runs on real
// hardware's own actual default font - this shim's own `gbBegin()` defaults
// to the identical real font (`gbFont3x5`), so no `gbSetFont()` call was
// needed here either.

#define AIMB_STATE_TITLE 0
#define AIMB_STATE_MENU 1
#define AIMB_STATE_PLAYING 2
#define AIMB_STATE_LEADERBOARD 3

#define AIMB_MODE_AIM 0
#define AIMB_MODE_PHYS 1

#define AIMB_PLAY_FREE 0
#define AIMB_PLAY_CHALLENGE 1

#define AIMB_MENU_LEN 4

#define AIMB_PI 3.14159265

#define AIMB_IBALLEX 6
#define AIMB_IBALLEY 38

// real upstream's own literal `60*20` - 60 real seconds at this game's own
// real 20fps default (it never calls gb.setFrameRate()).
#define AIMB_CHALLENGE_TICKS 1200

int aimbTargetBitmap[10] = { 8, 8, 0x18, 0x7E, 0x66, 0xDB, 0xDB, 0x66, 0x7E, 0x18 };

int aimbState;
int aimbMenuIndex;

// A real, found-via-live-testing bug: this game chains THREE separate
// Button-A-driven state transitions in a row (title->menu, menu->playing,
// and inside playing, aimbViser()'s own gbRepeat(BTN_A,1)/gbReleased(BTN_A)
// aim-charge/launch gesture) - a single physical A-press held across a
// state transition is still "held" the instant the new state's own update
// function starts checking BTN_A again that same tick, so the exact same
// press that dismissed the title screen could also immediately confirm a
// menu item, and the exact same press that confirmed a menu item could
// immediately register as the start of an aim-charge in gameplay. Unlike
// the cartridge-level "launch button bleeds into the game" bug this
// project's own md_armInputAGate() already fixes once per game launch,
// this needed a *local*, reusable gate applied at every one of Aimbuino's
// own internal Button-A-driven transitions: `aimbConsumeA()` only reports
// a fresh press once Button A has actually been observed released at
// least once since the last transition armed it.
int aimbAGated;

int aimbConsumeA()
{
    if( aimbAGated )
    {
        if( !gbHeld( BTN_A, 1 ) )
          aimbAGated = 0;
        return 0;
    }
    return gbPressed( BTN_A );
}

int aimbPlayMode;
int aimbSubMode;
int aimbLost;
int aimbScore;
int aimbHighScore;
int aimbChallengeStartFrame;

float aimbBallX;
float aimbBallY;
int aimbBallR;

int aimbTargetX;
int aimbTargetY;

float aimbAimAngle;
float aimbAimForce;
int aimbForceDir;
float aimbT;

int aimbLbChallenge[3];
int aimbLbFree[3];

int aimbInsertScore( int* table, int score )
{
    int i;
    for( i = 0; i < 3; i++ )
    {
        if( table[i] < score )
        {
            int j;
            for( j = 2; j > i; j = j - 1 )
              table[j] = table[j - 1];
            table[i] = score;
            return 1;
        }
    }
    return 0;
}

void aimbLoadLeaderboard()
{
    int i;
    for( i = 0; i < 3; i++ )
    {
        int v = eeprom_read_word( i * 2 );
        if( v == 0xFFFF )
          v = 0;
        aimbLbChallenge[i] = v;
    }
    for( i = 0; i < 3; i++ )
    {
        int v = eeprom_read_word( 6 + i * 2 );
        if( v == 0xFFFF )
          v = 0;
        aimbLbFree[i] = v;
    }
}

void aimbSaveLeaderboard()
{
    int i;
    for( i = 0; i < 3; i++ )
      eeprom_write_word( i * 2, aimbLbChallenge[i] );
    for( i = 0; i < 3; i++ )
      eeprom_write_word( 6 + i * 2, aimbLbFree[i] );
}

void aimbInitJeu()
{
    aimbBallX = AIMB_IBALLEX;
    aimbBallY = AIMB_IBALLEY;
    aimbBallR = 3;
    aimbAimAngle = ( 1 / 4 ) * AIMB_PI; // real, preserved upstream bug - see header comment
    aimbAimForce = 5;
    aimbT = 0;
    aimbSubMode = AIMB_MODE_AIM;
    aimbTargetX = 24 + arand( LCDWIDTH - 12 - 24 );
    aimbTargetY = 6 + arand( LCDHEIGHT - 8 - 6 );
}

void aimbDecor()
{
    gbDrawCircle( (int)aimbBallX, (int)aimbBallY, aimbBallR );

    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Score: " );
    gbPrintNumber( aimbScore );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;

    if( aimbPlayMode == AIMB_PLAY_CHALLENGE )
      aimbHighScore = aimbLbChallenge[0];
    else
      aimbHighScore = aimbLbFree[0];

    gbPrintString( "High Score: " );
    gbPrintNumber( aimbHighScore );

    gbDrawBitmap( aimbTargetX, aimbTargetY, aimbTargetBitmap );
}

void aimbViser()
{
    float x = aimbBallX + aimbAimForce * cos( aimbAimAngle );
    float y = aimbBallY + aimbAimForce * sin( aimbAimAngle );
    gbDrawLine( (int)aimbBallX, (int)aimbBallY, (int)x, (int)y );

    if( gbRepeat( BTN_RIGHT, 1 ) )
      aimbAimAngle = aimbAimAngle + AIMB_PI / 64;
    if( gbRepeat( BTN_LEFT, 1 ) )
      aimbAimAngle = aimbAimAngle - AIMB_PI / 64;

    // the still-held A-press that just confirmed the menu selection to get
    // here must not also be read as the start of a charge or an immediate
    // launch - gated the same way as the title/menu transitions above, via
    // the shared aimbAGated flag (armed by aimbUpdateMenu() on entry).
    if( aimbAGated )
    {
        if( !gbHeld( BTN_A, 1 ) )
          aimbAGated = 0;
    }
    else
    {
        if( gbRepeat( BTN_A, 1 ) )
          aimbAimForce = aimbAimForce + aimbForceDir;

        if( gbReleased( BTN_A ) )
        {
            aimbAimForce = aimbAimForce * 2;
            aimbT = 0;
            aimbSubMode = AIMB_MODE_PHYS;
            gbPlayTick();
        }
    }

    if( aimbAimForce > 17 || aimbAimForce < 5 )
      aimbForceDir = -aimbForceDir;
}

void aimbPhysique()
{
    aimbT = aimbT + 0.1;
    aimbBallX = aimbAimForce * cos( aimbAimAngle ) * aimbT + AIMB_IBALLEX;
    aimbBallY = 0.5 * 10 * aimbT * aimbT + aimbAimForce * sin( aimbAimAngle ) * aimbT + AIMB_IBALLEY;

    if( aimbBallY > LCDHEIGHT || aimbBallX > LCDWIDTH || aimbBallX < 0 )
    {
        gbPlayCancel();
        aimbInitJeu();
        if( aimbPlayMode == AIMB_PLAY_FREE )
          aimbLost = 1;
    }

    // real upstream runs this unconditionally right after the out-of-bounds
    // reset above too (no early return there), so on an out-of-bounds tick
    // this tests the just-reset ball against the just-re-randomized target -
    // preserved exactly, not special-cased away.
    float dx = (float)( aimbTargetX + 4 ) - aimbBallX;
    float dy = (float)( aimbTargetY + 4 ) - aimbBallY;
    int d = (int)( dx * dx + dy * dy );
    int hitR = aimbBallR + 4;
    if( d < hitR * hitR )
    {
        gbPlayOK();
        aimbScore = aimbScore + 1;
        aimbInitJeu();
    }
}

void aimbChallenge()
{
    int tpsF = gbFrameCount - aimbChallengeStartFrame;
    if( tpsF > AIMB_CHALLENGE_TICKS )
    {
        aimbLost = 1;
        return;
    }

    int secondsLeft = ( AIMB_CHALLENGE_TICKS - tpsF ) / 20;
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
    gbPrintNumber( secondsLeft );
    gbPrintString( "s" );
}

void aimbUpdateTitle()
{
    gbCursorX = 4;
    gbCursorY = 16;
    gbPrintString( "AIMBUINO" );
    gbCursorX = 4;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( aimbConsumeA() )
    {
        aimbState = AIMB_STATE_MENU;
        aimbAGated = 1;
    }
}

void aimbUpdateMenu()
{
    gbCursorX = 2;
    gbCursorY = 1;
    gbPrintString( "AIMBUINO" );

    int i;
    for( i = 0; i < AIMB_MENU_LEN; i++ )
    {
        gbCursorY = 14 + i * 8;
        gbCursorX = 0;
        if( i == aimbMenuIndex )
          gbPrintString( "*" );

        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "FREE MODE" );
        else if( i == 1 ) gbPrintString( "CHALLENGE" );
        else if( i == 2 ) gbPrintString( "LEADERBOARD" );
        else gbPrintString( "EXIT" );
    }

    if( gbRepeat( BTN_UP, 5 ) )
      aimbMenuIndex = gbMax( 0, aimbMenuIndex - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) )
      aimbMenuIndex = gbMin( AIMB_MENU_LEN - 1, aimbMenuIndex + 1 );

    if( aimbConsumeA() )
    {
        gbPlayOK();
        aimbAGated = 1;
        if( aimbMenuIndex == 0 )
        {
            aimbScore = 0;
            aimbPlayMode = AIMB_PLAY_FREE;
            aimbInitJeu();
            aimbState = AIMB_STATE_PLAYING;
        }
        else if( aimbMenuIndex == 1 )
        {
            aimbScore = 0;
            aimbPlayMode = AIMB_PLAY_CHALLENGE;
            aimbChallengeStartFrame = gbFrameCount;
            aimbInitJeu();
            aimbState = AIMB_STATE_PLAYING;
        }
        else if( aimbMenuIndex == 2 )
        {
            aimbState = AIMB_STATE_LEADERBOARD;
        }
        else
        {
            aimbState = AIMB_STATE_TITLE;
        }
    }
}

void aimbUpdatePlaying()
{
    if( gbPressed( BTN_C ) )
    {
        aimbState = AIMB_STATE_MENU;
        return;
    }

    aimbDecor();

    if( aimbSubMode == AIMB_MODE_AIM )
      aimbViser();
    else
      aimbPhysique();

    if( aimbPlayMode == AIMB_PLAY_CHALLENGE )
      aimbChallenge();
}

void aimbHandleLoss()
{
    aimbInitJeu();
    aimbLoadLeaderboard();

    int inserted;
    if( aimbPlayMode == AIMB_PLAY_CHALLENGE )
      inserted = aimbInsertScore( aimbLbChallenge, aimbScore );
    else
      inserted = aimbInsertScore( aimbLbFree, aimbScore );

    if( inserted )
      aimbSaveLeaderboard();

    aimbLost = 0;
    aimbState = AIMB_STATE_LEADERBOARD;
}

void aimbUpdateLeaderboard()
{
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "LEADERBOARD" );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
    gbPrintString( "CHALLENGE/FREE" );

    int i;
    for( i = 0; i < 3; i++ )
    {
        gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
        gbCursorX = 0;
        gbPrintNumber( aimbLbChallenge[i] );
        gbCursorX = LCDWIDTH / 2;
        gbPrintNumber( aimbLbFree[i] );
    }

    if( gbPressed( BTN_C ) )
      aimbState = AIMB_STATE_MENU;
}

void gameAimbuino_init()
{
    gbBegin();
    gbPickRandomSeed();

    aimbState = AIMB_STATE_TITLE;
    aimbMenuIndex = 0;
    aimbScore = 0;
    aimbPlayMode = AIMB_PLAY_FREE;
    aimbForceDir = 1;
    aimbLost = 0;

    aimbLoadLeaderboard();
    aimbInitJeu();
}

void gameAimbuino_update()
{
    if( !gbUpdate() ) return;

    if( aimbState == AIMB_STATE_TITLE )
      aimbUpdateTitle();
    else if( aimbState == AIMB_STATE_MENU )
      aimbUpdateMenu();
    else if( aimbState == AIMB_STATE_PLAYING )
      aimbUpdatePlaying();
    else
      aimbUpdateLeaderboard();

    if( aimbLost )
      aimbHandleLoss();

    gbRenderFrame();
}
