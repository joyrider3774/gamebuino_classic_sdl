// -----------------------------------------------------------------------------
// Entry point + top-level frame loop. The actual menu<->game dispatch logic
// (quit-confirm dialog, A-gate arming, real-gray toggle, menu selection)
// lives in gamesMain.c's own gamesMain_dispatchFrame() - modeled on the
// sibling gamebuino_classic_vircon32 build's own portVircon32.c main().
//
// This file reuses the sibling Tinyjoypad_SDL project's own command-line-
// parameter surface and FPS-display concept directly (see printHelp()
// below for the exact flags kept) - with this project's own real Button A
// (not "Fire") and ".gbu" stub-file naming (not ".joy").
//
// md_endFrame() is called unconditionally, exactly once, at the bottom of
// every real frame - the sole real present call in this whole file.
// gamebuinoShim.c's own gbUpdate()/gbRenderFrame() (via the fixed
// 20fps-by-default accumulator) decide for themselves whether this
// particular real tick actually draws anything new into the backing
// surface, but never call md_endFrame() themselves - see gbRenderFrame()'s
// own doc comment in gamebuinoShim.c for why a second present from there
// would be a real, measurable bug on a vsync-locked port, not just
// redundant work.
// -----------------------------------------------------------------------------

#if defined _WIN32 || defined __CYGWIN__
    #include <windows.h>
#endif

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "machineDependent.h"
#include "sdlBackend.h"
#include "gamesMain.h"

static void printHelp( char* exeName )
{
    printf( "Usage: %s [options] [file.gbu]\n", exeName );
    printf( "  -w <WIDTH>     Window width (default %d)\n", 640 );
    printf( "  -h <HEIGHT>    Window height (default %d)\n", 360 );
    printf( "  -f             Start in fullscreen\n" );
    printf( "  -ns            No sound (skip audio init)\n" );
    printf( "  -fps           Show fps counter overlay\n" );
    printf( "  -nd            No delay (uncapped framerate, vsync off)\n" );
    printf( "  -s             Force software rendering (no GPU acceleration)\n" );
    printf( "  -list          List all game names, then exit\n" );
    printf( "  -g <NAME>      Launch a specific game directly by title\n" );
    printf( "  -ms            Batch-capture a screenshot of every game (./<TITLE>.bmp), then exit\n" );
    printf( "  -gray <0|1>    Override the real-gray-color default (on) - useful when\n" );
    printf( "                 regenerating metadata screenshots/thumbnails with -ms\n" );
    printf( "  -gbu           Write a .gbu stub file for every game, then exit\n" );
    printf( "  file.gbu       Launch the game named by this file directly\n" );
    printf( "  -?/-help       Show this help\n" );
}

// Writes one plain-text ".gbu" stub file per registered game (just the
// title) - lets an external ROM-list frontend have a per-game file to
// point at. Needs gamesMain_init() (so games[]/gameCount are populated)
// but no SDL/window at all, same as -list below.
static void writeGbuFiles()
{
    gamesMain_init();

    for( int i = 0; i < gamesMain_getGameCount(); i++ )
    {
        char* title = gamesMain_getGameTitle( i );
        if( !title || strlen( title ) == 0 )
          continue;

        char filename[ 512 ];
        snprintf( filename, sizeof( filename ), "./%s.gbu", title );

        FILE* f = fopen( filename, "w" );
        if( f )
        {
            fwrite( title, sizeof( char ), strlen( title ), f );
            fclose( f );
        }
    }
}

static void listGames()
{
    gamesMain_init();

    for( int i = 0; i < gamesMain_getGameCount(); i++ )
      printf( "%d. %s\n", i + 1, gamesMain_getGameTitle( i ) );
}

// One frame of a screenshot script's simulated input - held direction
// state (Up/Down/Left/Right) plus one real dispatch/audio/present cycle.
// Button state (A/B/C) is set separately via screenshotButtonFrame() below
// and persists across calls exactly like a real held/released button
// would, since sdlBackend's own simulate*Frame() setters are just plain
// state, not edge-triggered.
static void screenshotHeldDirectionFrame( bool holdUp, bool holdDown, bool holdLeft, bool holdRight )
{
    sdlBackend_simulateUpFrame( holdUp );
    sdlBackend_simulateDownFrame( holdDown );
    sdlBackend_simulateLeftFrame( holdLeft );
    sdlBackend_simulateRightFrame( holdRight );
    gamesMain_dispatchFrame();
    md_updateAudio();
    md_endFrame();
}

// Bare per-frame stepper, with no direction input of its own - used by
// screenshotWait()/a ScreenshotScript.custom callback, which drive every
// simulated button/direction themselves between calls.
static void screenshotStepFrame()
{
    gamesMain_dispatchFrame();
    md_updateAudio();
    md_endFrame();
}

// Which button screenshotScriptFor()'s own generic tap loop presses - most
// games confirm/dismiss a menu or title screen with A, but a few (e.g. a
// game whose own real prologue is dismissed with Button B only) need a
// different one.
#define SS_BUTTON_A 0
#define SS_BUTTON_B 1
#define SS_BUTTON_C 2

static void screenshotButtonFrame( int button, bool pressed )
{
    if( button == SS_BUTTON_B )      sdlBackend_simulateBFrame( pressed );
    else if( button == SS_BUTTON_C ) sdlBackend_simulateCFrame( pressed );
    else                              sdlBackend_simulateAFrame( pressed );
}

// How many consecutive real frames a simulated press/release phase holds
// for, instead of an original single-real-frame pulse. A GENUINE, CONFIRMED
// bug was found and root-caused here (not a hypothetical): gbUpdate()
// throttles to a fixed frame rate via a Bresenham-style accumulator
// (gbTickAccum in gamebuinoShim.c) that only actually runs a game's own
// logic tick - the only point gbPressed()'s own edge-detector ever samples
// input - once every (MD_FRAMES_PER_SECOND / gbFrameRateFps) real frames,
// deterministically phase-locked to the exact real-frame count since that
// game's own gbBegin() call. A single-real-frame press pulse can therefore
// land EXACTLY on a real frame the accumulator never fires a tick on -
// confirmed directly via a temporary debug build that captured a
// screenshot after every single tap: a game stuck on its own title screen
// after 12 taps with a 1-frame pulse dismissed it on the very FIRST tap
// once the press was widened past the divisor. 8 real frames comfortably
// exceeds the tick divisor of every frame rate any game in this project
// currently configures (the slowest, Elventure's own 10fps, has just a
// 6-frame divisor; the real Gamebuino default of 20fps has a 3-frame
// divisor) - guaranteeing every simulated press/release genuinely overlaps
// at least one real logic tick regardless of phase, with a SINGLE pulse
// (not several repeated taps, the earlier workaround this superseded).
#define SS_PRESS_FRAMES   8
#define SS_RELEASE_FRAMES 8

static void screenshotHoldFrames( int frames, bool holdUp, bool holdDown, bool holdLeft, bool holdRight )
{
    for( int j = 0; j < frames; j++ )
      screenshotHeldDirectionFrame( holdUp, holdDown, holdLeft, holdRight );
}

// One full press-and-release edge of a single button (A/B/C), each phase
// held long enough (see SS_PRESS_FRAMES/SS_RELEASE_FRAMES above) to
// guarantee a real gbUpdate() logic tick actually samples it - the atomic
// gesture every custom per-game screenshot sequence below is built from.
static void screenshotButtonPulse( int button )
{
    screenshotButtonFrame( button, false );
    screenshotHoldFrames( SS_RELEASE_FRAMES, false, false, false, false );
    screenshotButtonFrame( button, true );
    screenshotHoldFrames( SS_PRESS_FRAMES, false, false, false, false );
    screenshotButtonFrame( button, false );
    screenshotHoldFrames( SS_RELEASE_FRAMES, false, false, false, false );
}

static void screenshotTapButton( int button )
{
    screenshotButtonPulse( button );
}

// One full press-and-release edge of a single held direction (Up/Down/
// Left/Right) - for menu-navigation sequences that move a cursor rather
// than confirm/dismiss anything (e.g. moving off a menu's own default
// selection before confirming with A). Same widened-pulse reasoning as
// screenshotButtonPulse() above - gbPressed(BTN_UP/DOWN/LEFT/RIGHT) is
// sampled by the exact same throttled tick.
static void screenshotTapDir( bool up, bool down, bool left, bool right )
{
    screenshotHoldFrames( SS_RELEASE_FRAMES, false, false, false, false );
    screenshotHoldFrames( SS_PRESS_FRAMES, up, down, left, right );
    screenshotHoldFrames( SS_RELEASE_FRAMES, false, false, false, false );
}

// Plain real-frame wait with no input of its own - for letting a real
// upstream animation/auto-cascade/timeout run its course between two
// scripted presses.
static void screenshotWait( int frames )
{
    screenshotHoldFrames( frames, false, false, false, false );
}

// A fully custom per-game input sequence, called INSTEAD of
// screenshotScriptFor()'s own generic tap loop (see runBatchScreenshots())
// for games whose own real state chain needs more than "tap one button N
// times, optionally holding one direction throughout" can express - a mix
// of different buttons and/or directions in a specific order.
typedef void (*ScreenshotCustomFn)( void );

// The sibling gamebuino_classic_vircon32 build's own menu-thumbnail capture
// pass (see that project's own CLAUDE.md) found that no single generic
// button sequence reaches real gameplay for every game - most need a
// couple of A taps through a title/menu screen first, several need a more
// specific sequence. This table is that same per-game tuning, added here
// as each game gets ported and its own real capture sequence worked out
// (see main.c's own header comment in the Vircon32 build for the same
// "empirically tuned, checked against the actual screenshot" discipline).
// Default (title not listed) is 4 A taps ~1.5s apart, no held direction, no
// extra wait after the last tap.
typedef struct
{
    int  tapCount;
    int  gapFrames;       // real frames waited after each tap
    int  finalWaitFrames; // extra real frames waited after the last tap
    bool holdUp;          // held through every gap/final-wait frame
    bool holdDown;
    bool holdLeft;
    bool holdRight;
    int  tapButton;            // SS_BUTTON_A/B/C - which button the generic tap loop above presses
    ScreenshotCustomFn custom; // if non-NULL, replaces the generic tap loop entirely
} ScreenshotScript;

#define SS_DEFAULT_TAPS  4
#define SS_DEFAULT_GAP   90

// -----------------------------------------------------------------------------
// Custom per-game capture sequences (used via ScreenshotScript.custom for
// games whose real state chain the generic tap loop can't express) - see
// each function's own comment for the real state-machine trace behind it.
// -----------------------------------------------------------------------------

// LANDER: SPLASH -[A]-> TITLE -[A]-> NEWGAME (auto-cascades same tick into
// SELECTLEVEL) -[B, "B TO CONFIRM"]-> NEWLEVEL/NEWLIFE (both auto-cascade
// same tick) -> RUNNING. Real gameplay needs a genuine Button B, not A, to
// leave SELECTLEVEL - the generic A-only tap loop can never pass it.
static void screenshotCustomLander()
{
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 30 ); // SPLASH -> TITLE
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 30 ); // TITLE -> NEWGAME -> SELECTLEVEL (default level 0)
    screenshotTapButton( SS_BUTTON_B ); screenshotWait( 60 ); // SELECTLEVEL -> NEWLEVEL -> NEWLIFE -> RUNNING
}

// VIDEO POKER: TITLE -[A]-> PLAY (a betting screen, cards face-down) -
// [B, "B:Deal"]-> cards actually dealt/revealed. A real Button B, not A.
static void screenshotCustomVideoPoker()
{
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 30 ); // TITLE -> PLAY (betting screen)
    screenshotTapButton( SS_BUTTON_B ); screenshotWait( 60 ); // Deal - cards actually revealed
}

// BLOB ATTACK: TITLE -[A]-> MAIN_MENU (blobSelector defaults to 0 = HELP,
// not PLAY) -[Down x2]-> selector=2=PLAY -[A]-> PLAYING. Needs two genuine
// Down taps the generic loop (Button A only) can't send.
static void screenshotCustomBlobAttack()
{
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 30 );        // TITLE -> MAIN_MENU (selector=HELP)
    screenshotTapDir( false, true, false, false ); screenshotWait( 15 ); // selector -> INFO
    screenshotTapDir( false, true, false, false ); screenshotWait( 15 ); // selector -> PLAY
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 60 );        // confirm -> PLAYING
}

// CRAZYTOWN: TITLE -[A]-> MENU ("Continue" selected, but Continue is a
// no-op on a fresh game since townTimeLeft==0) -[Down]-> "3 min play"
// -[A]-> real play. Needs a genuine Down tap first.
static void screenshotCustomCrazyTown()
{
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 30 );        // TITLE -> MENU (Continue highlighted)
    screenshotTapDir( false, true, false, false ); screenshotWait( 15 ); // Continue -> "3 min play"
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 60 );        // start the 3-minute game
}

// ARTILLERY: TITLE -[A]-> SELECT_MAP (artGameLevel defaults to 0, which is
// actually the OPTIONS/Settings slot, not a real level) -[Right]-> level 1
// -[A]-> NEW_LEVEL (auto-cascades to SELECT_UNIT, which itself auto-
// advances to real RUNNING gameplay after a real ~20-tick unit-select
// marker animation - no further input needed once level 1 is chosen).
static void screenshotCustomArtillery()
{
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 30 );         // TITLE -> SELECT_MAP (level 0 = Settings)
    screenshotTapDir( false, false, false, true ); screenshotWait( 15 ); // level 0 -> level 1 (a real playable map)
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 100 );        // choose level 1 -> NEW_LEVEL -> SELECT_UNIT -> (~20 ticks) -> RUNNING
}

// CASTLE DEFENCE: TITLE -[A]-> WEAPON_SELECT (cdefMainSub==0) -[A]-> pick
// main weapon (cdefWeaponPoint defaults to 0, so this just re-picks the
// same Rifle) -[Right]-> move the point off the main weapon's own index
// (the sub-weapon pick requires cdefMainWeapon != cdefWeaponPoint, or it's
// silently ignored) -[A]-> pick sub weapon -[A]-> confirm -> READY (auto-
// advances to real PLAY after a fixed ~80-tick "Ready..?"/"GO!!" countdown).
static void screenshotCustomCastleDefence()
{
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 30 );         // TITLE -> WEAPON_SELECT
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 20 );         // pick main weapon (point 0 = Rifle)
    screenshotTapDir( false, false, false, true ); screenshotWait( 15 ); // move point off the main weapon's own index
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 20 );         // pick sub weapon
    screenshotTapButton( SS_BUTTON_A ); screenshotWait( 260 );        // confirm -> READY -> (~80 ticks) -> PLAY
}

// B-RALLY: TITLE -[A]-> brallyLevelStart() immediately begins a real
// ~60-tick (180 real frame) countdown, then RUNNING - but the car's own
// speed (BTN_A, held, is the accelerator during RUNNING) decays back to 0
// between scripted taps, so a plain tap-based script would still show a
// stationary "Spd:0" car once RUNNING starts. Holds Button A continuously
// for a stretch once the countdown has cleared, so the captured frame
// shows genuine motion/nonzero speed, not the parked start-line frame.
// Every stage below is trimmed to close to its own real minimum (not a
// generous safety margin) per a direct, live "takes too long on the
// press-A screen" report - the confirm gap only needs to clear the title
// screen, not linger there.
static void screenshotCustomBRally()
{
    screenshotTapButton( SS_BUTTON_A ); // TITLE -> COUNTDOWN (immediate, no menu)
    screenshotWait( 15 );

    screenshotWait( 190 ); // clear the ~180-real-frame countdown (A is ignored during COUNTDOWN anyway)

    sdlBackend_simulateAFrame( true ); // hold the accelerator once RUNNING - real motion/speed, not a parked car
    screenshotWait( 60 );
    sdlBackend_simulateAFrame( false );
}

// DEATHMAZE: the default script's first A tap already reaches
// DMAZE_STATE_PLAY (dmazeScore visibly counting down proves it), but the
// player sprite itself starts at the maze's own fixed left-edge spawn cell
// and only flashes into visibility while genuinely moving
// (dmazeDrawPlayer()'s own dontFlash argument is `dmazeMoving ||
// dmazeFinished`) - with no movement input at all it never leaves that
// cell, and even once moved, a 2x2 player square drawn over this game's
// own dense wall pattern can still land pixel-for-pixel on top of a wall
// segment and be visually indistinguishable from it. arand() is a
// documented no-op in this shim (see gamebuinoShim.h's own header
// comment), so dmazeRandomiseMaze() generates the exact same maze layout
// on every run - traced directly against the real generation algorithm: a
// full row-by-row sweep (Up to row 0, then Down across the entire column)
// is guaranteed to cross at least one real gap (every column has >=1) and
// produce genuine, visible rightward movement - the closest this game's
// own real geometry allows a scripted capture to get.
static void screenshotCustomDeathMaze()
{
    screenshotTapButton( SS_BUTTON_A ); // dismiss DMAZE_STATE_TITLE
    screenshotWait( 15 );

    screenshotTapDir( false, false, false, true ); // try Right at the spawn row first
    screenshotWait( 4 );
    for( int i = 0; i < 9; i++ )
    {
        screenshotTapDir( true, false, false, false ); // Up one row
        screenshotWait( 4 );
        screenshotTapDir( false, false, false, true ); // try Right at this row
        screenshotWait( 4 );
    }
    for( int i = 0; i < 17; i++ )
    {
        screenshotTapDir( false, true, false, false ); // Down one row
        screenshotWait( 4 );
        screenshotTapDir( false, false, false, true ); // try Right at this row
        screenshotWait( 4 );
    }
    // end on a Right attempt so dmazeMoving is fresh (guaranteed solid,
    // non-flashed player render) regardless of which row above actually
    // succeeded.
    screenshotTapDir( false, false, false, true );
    screenshotWait( 2 );
}

// STAR HONOR dismisses its own real title loop AND both prologue text
// pages with Button B, not A (starGetInput()'s own real starBButton latch,
// gated by starNewButtonInputAllowed the same way this shim gates A
// elsewhere). Several distinct B taps (not a hold) walk
// STAR_STATE_TITLELOOP -> STAR_STATE_PROLOGUE (2 text pages) ->
// STAR_STATE_MAP, landing on the real overworld map with the ship/planets
// visible.
static void screenshotCustomStarHonor()
{
    for( int i = 0; i < 10; i++ )
    {
        screenshotTapButton( SS_BUTTON_B );
        screenshotWait( 15 );
    }
    screenshotWait( 30 );
}

// FIFTEEN: TITLE -[A]-> MENU (cursor defaults to "Load saved", which
// upstream deliberately leaves a no-op with no valid save present) -
// [Down] moves the cursor to "New Game" instead -[A]-> CHOOSEMAP (defaults
// to the smallest 3x3 size) -[A]-> PLAY, a real shuffled tile board.
static void screenshotCustomFifteen()
{
    screenshotTapButton( SS_BUTTON_A ); // dismiss FTN_STATE_TITLE
    screenshotWait( 15 );
    screenshotTapDir( false, true, false, false ); // select "New Game"
    screenshotWait( 10 );
    screenshotTapButton( SS_BUTTON_A ); // confirm New Game -> size picker
    screenshotWait( 15 );
    screenshotTapButton( SS_BUTTON_A ); // confirm default 3x3 size -> PLAY
    screenshotWait( 30 );
}

static ScreenshotScript screenshotScriptFor( char* title )
{
    ScreenshotScript s = { SS_DEFAULT_TAPS, SS_DEFAULT_GAP, 0, false, false, false, false, SS_BUTTON_A, NULL };

    // CRAZYCAR/CONDUIT/FLAPPY BIRDO/UFO RACE/BLOCKDUDE/GRUNIOZERCA/
    // ARMAGEDDON/CRABATOR/SUPER CRATE BUINO/FIREBUINO all share the
    // exact same real shape: TITLE -[A]-> MENU (or an equivalent first
    // sub-screen) -[A, default/first menu entry]-> real PLAY - genuinely 2
    // sequential Button-A presses, no direction/other-button input needed
    // (each game's own default menu-cursor position already lands on
    // "Play"/"Easy"/etc). tapCount=2 is now enough (was tapCount=6 before
    // screenshotButtonPulse()'s own wide-pulse fix - see that function's
    // own doc comment for why a single wide pulse per tap already
    // guarantees a real sampled tick, with no repeated-tap workaround
    // needed anymore). MINESWEEPER needs the same 2 presses for a related
    // but distinct reason: the FIRST dismisses its own title screen into
    // real PLAY, and the SECOND is a genuine press+release of Button A on
    // the covered top-left cell, uncovering it.
    if( SDL_strcmp( title, "CRAZYCAR" ) == 0 ||
        SDL_strcmp( title, "CONDUIT" ) == 0 ||
        SDL_strcmp( title, "FLAPPY BIRDO" ) == 0 ||
        SDL_strcmp( title, "UFO RACE" ) == 0 ||
        SDL_strcmp( title, "BLOCKDUDE" ) == 0 ||
        SDL_strcmp( title, "GRUNIOZERCA" ) == 0 ||
        SDL_strcmp( title, "ARMAGEDDON" ) == 0 ||
        SDL_strcmp( title, "CRABATOR" ) == 0 ||
        SDL_strcmp( title, "MARUINO" ) == 0 ||
        SDL_strcmp( title, "SUPER CRATE BUINO" ) == 0 ||
        SDL_strcmp( title, "FIREBUINO" ) == 0 ||
        SDL_strcmp( title, "MINESWEEPER" ) == 0 ||
        SDL_strcmp( title, "PUNKT" ) == 0 || // TITLE-[A]->MAIN_MENU-[A]->PLAYING (punktUpdateMainMenu()'s own A-press branch calls punktInitializeGame() directly, no separate menu-item selection needed)
        SDL_strcmp( title, "ASTEROCKS" ) == 0 ) // SPLASH-[A]->TITLE-[A]->NEWGAME, same 2-press shape
    {
        s.tapCount = 2;
        s.gapFrames = 30;
    }
    // SMASH AND CRASH needs a real 3rd sequential Button-A press
    // (TITLE->MENU->MAPSELECT->PLAY, each one a genuine "> Survival"/
    // default-map confirm).
    else if( SDL_strcmp( title, "SMASH AND CRASH" ) == 0 )
    {
        s.tapCount = 3;
        s.gapFrames = 30;
    }
    // GLACIGLACA needs 2 real Button-A presses (TITLE->DIFFICULTY,
    // DIFFICULTY->PREPARE_TEMPS, default Facile) - PREPARE_TEMPS then
    // auto-cascades into DAYINTRO, which itself auto-advances into real
    // MAGASIN (shop) gameplay after its own fixed 80-tick (240 real frame)
    // timeout with no further input needed at all - a generous
    // finalWaitFrames clears that timeout instead of a 3rd scripted press.
    else if( SDL_strcmp( title, "GLACIGLACA" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 30;
        s.finalWaitFrames = 260;
    }
    // DESCENT INTO HELL needs only 1 registered Button-A press (dismissing
    // its own TITLE screen) - but that press starts a real, fixed 20-tick,
    // 10fps intro animation (descentUpdateIntro(), gbSetFrameRate(10))
    // before real dungeon-crawl PLAY begins (confirmed by tracing
    // descentPlayer.heal's own health-icon HUD loop against the actual
    // captured pixels - a real, if RNG-timing-sensitive, PLAY frame, not a
    // menu/title). finalWaitFrames is deliberately kept short (just past
    // the empirically-confirmed intro->PLAY transition, not a large
    // safety margin) - once PLAY begins, this game's own wandering
    // monsters can genuinely kill an unmoving, unscripted player, and
    // (since no srand() call exists anywhere in this project - see
    // CLAUDE.md - every arand() draw is a deterministic function of
    // exactly how many ticks have already run) waiting substantially
    // longer was confirmed empirically to cycle through a real death ->
    // GAMEOVER_MSG -> back toward INTRO's own first bitmap, not toward
    // more/better gameplay. Captured shortly after PLAY begins instead,
    // matching the sibling Tinyjoypad_SDL project's own established
    // "capture shortly after the run begins, while still alive" fallback
    // for this exact class of unscriptable-survival game.
    else if( SDL_strcmp( title, "DESCENT INTO HELL" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 30;
        s.finalWaitFrames = 160;
    }
    // SHIPWREK's own real PLACE (ship-placement) screen is already
    // genuine interactive gameplay (not a menu/title), reached with a
    // single Button-A press - a second guaranteed press places the first
    // boat (the Carrier) at its own default valid position, so the
    // captured board shows a real placed boat instead of a still-
    // completely-empty grid.
    else if( SDL_strcmp( title, "SHIPWREK" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 30;
    }
    // SOLITAIRE's own real chain (TITLE -> a hand-rolled "gb.menu()"
    // NEW EASY GAME/NEW HARD GAME/GAME STATISTICS replacement, defaulting
    // to NEW EASY GAME) is exactly 2 real A-confirms deep.
    else if( SDL_strcmp( title, "SOLITAIRE" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // LIGHTS OUT AD's own real chain (INTRO_REVEAL -> INTRO_LINGER (auto)
    // -> TITLE -> a persistent settings menu defaulting to "Play") is at
    // most 3 real A-confirms deep.
    else if( SDL_strcmp( title, "LIGHTS OUT AD" ) == 0 )
    {
        s.tapCount = 3;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // SENET's own real chain (TITLE -> MAIN_MENU, defaulting to "Play /
    // Resume" -> GAME_SETUP -> PLAYING) is 3 real A-confirms deep.
    else if( SDL_strcmp( title, "SENET" ) == 0 )
    {
        s.tapCount = 3;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // STICKFIGHTER's own real chain (TITLE -> MENU, a 2-item solo/option
    // carousel defaulting to "solo" -> PLAY) is 2 real A-confirms deep.
    else if( SDL_strcmp( title, "STICKFIGHTER" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // SPIN SPIN SPINBUINO's own real chain (TITLE -> MENU, a level picker
    // defaulting to level 1A -> NIVEAU) is 2 real A-confirms deep.
    else if( SDL_strcmp( title, "SPIN SPIN SPINBUINO!" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // SNAKE 5110's own real chain (TITLE -> MENU, defaulting to "Start
    // Game" -> PLAYING) is 2 real A-confirms deep.
    else if( SDL_strcmp( title, "SNAKE 5110" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // AIMBUINO's own real chain (TITLE -> MENU, defaulting to "FREE MODE"
    // -> PLAYING) is exactly 2 real A-confirms deep, gated by
    // aimbConsumeA()'s own real debounce (only reports a fresh press once
    // Button A has genuinely been observed released since the gate was
    // last armed) - already satisfied by screenshotButtonPulse()'s own
    // real release phase between taps. Deliberately exactly 2, not more: a
    // 3rd A tap once already PLAYING is read as a real aim-charge-then-
    // release gesture (aimbViser()), confirmed via a direct screenshot to
    // sometimes register as a miss, which aimbHandleLoss() sends straight
    // to AIMB_STATE_LEADERBOARD - not a menu-navigation bug, a genuine
    // extra shot this script shouldn't be taking at all.
    else if( SDL_strcmp( title, "AIMBUINO" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // DARKSHMUP's own real chain (TITLE -> SHIPSELECT, a ship-description
    // carousel -> PLAY) is 2 real A-confirms deep.
    else if( SDL_strcmp( title, "DARKSHMUP" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // PETITMONSTRE's own real opening is several pages of blocking story
    // text (real upstream's own nested `while(true){if(gb.update()){...}}`
    // dialogue loops, each dismissed with its own A press) before the
    // player ever gains control - a generous tap count clears every page.
    else if( SDL_strcmp( title, "PETITMONSTRE" ) == 0 )
    {
        s.tapCount = 6;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // UNDER THE TOWER opens on a real, several-page scripted dialogue
    // event (uttDisplayDialogue()/uttStepDialogue(), each page dismissed
    // with its own A press) the instant UTT_STATE_WORLD begins, before the
    // player ever gains control - a generous tap count clears every page.
    else if( SDL_strcmp( title, "UNDER THE TOWER" ) == 0 )
    {
        s.tapCount = 8;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // COPTERSTRIKE's own real chain (TITLE -> SELECTMAP, a 4-mission/
    // Easy-Normal-Hard picker defaulting to Desert Strike/Easy -> GAME) is
    // 2 real A-confirms deep.
    else if( SDL_strcmp( title, "COPTERSTRIKE" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // GEMGEM's own real chain (TITLE -> MENU, defaulting to "New Game" ->
    // PLAYING) is 2 real A-confirms deep - PLAYING itself opens with a
    // real, self-dismissing "New game" popup (gemPopup(), a real 52-tick
    // lifespan at this game's own real fps=20 default, ~156 real frames)
    // overlaid on the board, so a generous final wait lets it clear on its
    // own before the capture, matching what a player would actually see
    // once done reading it.
    else if( SDL_strcmp( title, "GEMGEM" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 20;
        s.finalWaitFrames = 200;
    }
    // BREAKOUT RIPPER's own real chain is a single A tap
    // (BRKO_STATE_TITLE -> BRKO_STATE_PLAY directly), but the paddle only
    // reads Left/Right (no held-direction field this script sets for it)
    // and stays fixed wherever it starts - too long a wait reliably
    // outlives the ball falling straight past the stationary paddle,
    // landing on BRKO_STATE_MESSAGE's own real "Game Over!" screen instead
    // (confirmed via a direct capture). One tap, then just enough wait to
    // render a real PLAYING frame with the ball/paddle/bricks all visible,
    // short enough the paddle hasn't been missed yet - the same "capture
    // shortly after the run begins" shape as BREAKOUT's own already-solved
    // precedent in the sibling Tinyjoypad_SDL project's own
    // screenshotScriptFor().
    else if( SDL_strcmp( title, "BREAKOUT RIPPER" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 10;
    }
    // BOMBER's own real chain is a single A tap (BOMB_STATE_TITLE ->
    // BOMB_STATE_START, which auto-advances to BOMB_STATE_PLAY the very
    // next tick with no further input needed at all) - a 2nd/3rd/4th A tap
    // is read as a real in-game bomb-drop, confirmed via a direct capture
    // to occasionally walk the player all the way to BOMB_STATE_GAMEOVER
    // -> BOMB_STATE_BACK_TO_TITLE, a real one-tick-blank transition state
    // (bombGoTitleScreen() draws nothing itself, relying on the very next
    // tick's own BOMB_STATE_TITLE draw) - landing the capture on a
    // genuinely blank white frame. One tap only.
    else if( SDL_strcmp( title, "BOMBER" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 20;
        s.finalWaitFrames = 30;
    }
    // TRON's own real chain (TITLE -> DIFFICULTY -> GAME) is 2 real
    // A-confirms deep, but both riders move forward automatically every
    // tick with no player input needed to keep moving - too long a total
    // budget reliably outlives a real crash into a wall/trail, landing on
    // TRON_STATE_GAMEOVER's own "game over" screen instead of live
    // gameplay (confirmed via a direct capture). Two taps, then only a
    // short wait - captures shortly after the run begins, while both
    // riders are still genuinely alive.
    else if( SDL_strcmp( title, "TRON" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 10;
        s.finalWaitFrames = 15;
    }
    // The following all need a genuinely different button/direction
    // somewhere in their own real title/menu chain that the generic
    // Button-A-only tap loop can never send - see each custom function's
    // own doc comment above for the exact real state-machine transitions
    // it drives.
    else if( SDL_strcmp( title, "LANDER" ) == 0 )
      s.custom = screenshotCustomLander;
    else if( SDL_strcmp( title, "VIDEO POKER" ) == 0 )
      s.custom = screenshotCustomVideoPoker;
    else if( SDL_strcmp( title, "BLOB ATTACK" ) == 0 )
      s.custom = screenshotCustomBlobAttack;
    else if( SDL_strcmp( title, "CRAZYTOWN" ) == 0 )
      s.custom = screenshotCustomCrazyTown;
    else if( SDL_strcmp( title, "ARTILLERY" ) == 0 )
      s.custom = screenshotCustomArtillery;
    else if( SDL_strcmp( title, "CASTLE DEFENCE" ) == 0 )
      s.custom = screenshotCustomCastleDefence;
    else if( SDL_strcmp( title, "B-RALLY" ) == 0 )
      s.custom = screenshotCustomBRally;
    else if( SDL_strcmp( title, "DEATHMAZE" ) == 0 )
      s.custom = screenshotCustomDeathMaze;
    else if( SDL_strcmp( title, "STAR HONOR" ) == 0 )
      s.custom = screenshotCustomStarHonor;
    else if( SDL_strcmp( title, "FIFTEEN" ) == 0 )
      s.custom = screenshotCustomFifteen;

    return s;
}

// Runs each game's own screenshotScriptFor() tap sequence, then saves the
// resulting screen as a BMP - via sdlBackend_simulateAFrame()/
// simulateUpFrame() instead of a real device press. Needs a real window/
// renderer already initialized (gScreen only exists after md_initVideo()),
// unlike -list/-gbu above.
static void runBatchScreenshots()
{
    // Menu screenshot first, before any game's own launch overwrites
    // currentGameIndex (gamesMain_init() already left it at -1, showing
    // the menu's own default first-page/first-selection state).
    for( int j = 0; j < 5; j++ )
      screenshotHeldDirectionFrame( false, false, false, false );
    sdlBackend_saveScreenshot( "./menu.bmp" );
    printf( "Captured ./menu.bmp\n" );

    for( int i = 0; i < gamesMain_getGameCount(); i++ )
    {
        gamesMain_launchGameDirect( i );

        char* title = gamesMain_getGameTitle( i );
        ScreenshotScript script = screenshotScriptFor( title ? title : "" );

        if( script.custom )
        {
            script.custom();
        }
        else
        {
            for( int tap = 0; tap < script.tapCount; tap++ )
            {
                screenshotButtonPulse( script.tapButton );

                for( int j = 0; j < script.gapFrames; j++ )
                  screenshotHeldDirectionFrame( script.holdUp, script.holdDown, script.holdLeft, script.holdRight );
            }

            for( int j = 0; j < script.finalWaitFrames; j++ )
              screenshotHeldDirectionFrame( script.holdUp, script.holdDown, script.holdLeft, script.holdRight );
        }

        char filename[ 512 ];
        snprintf( filename, sizeof( filename ), "./%s.bmp", title ? title : "game" );
        sdlBackend_saveScreenshot( filename );
        printf( "Captured %s\n", filename );
    }
}

int main( int argc, char** argv )
{
    // Attach to a potential console when launched with -mwindows so output
    // is visible in a cmd/msys prompt, but stays invisible when launched
    // from Explorer/a shortcut. First checks GetStdHandle(STD_OUTPUT_HANDLE)
    // - a -mwindows GUI-subsystem process genuinely has no stdout handle at
    // all when launched with no redirection, but when the shell DID
    // explicitly redirect stdout (`> file`, a pipe), STD_OUTPUT_HANDLE is
    // already a real, valid handle to that file/pipe regardless of
    // subsystem type, and unconditionally freopen("CON",...)-ing over it
    // would silently discard that redirection.
#if defined _WIN32 || defined __CYGWIN__
    if( GetStdHandle( STD_OUTPUT_HANDLE ) == NULL && AttachConsole( (DWORD)-1 ) )
    {
        freopen( "CON", "w", stderr );
        freopen( "CON", "w", stdout );
    }
#endif

    bool fullscreen  = false;
    bool noAudioInit = false;
    bool showFps     = false;
    bool noDelay     = false;
    bool makeScreenshots = false;
    bool softwareRendering = false;
    int  windowWidth  = 0; // 0 = keep sdlBackend's own default
    int  windowHeight = 0;
    char startGameTitle[ 100 ] = { 0 };
    int  grayOverride = -1; // -1 = keep gamesMain_init()'s own real-gray-color default (on); 0/1 = force off/on

    // First pass: detect a positional ".gbu" file argument (its filename,
    // minus path and extension, IS the game title to launch) and every
    // other flag.
    for( int i = 1; i < argc; i++ )
    {
        char* ext = strrchr( argv[ i ], '.' );
        if( ext != NULL && SDL_strcasecmp( ext, ".gbu" ) == 0 )
        {
            // Last path separator of either flavor - scanned by hand
            // (rather than comparing two strrchr() results, one of which
            // may be NULL, with a relational operator) since a mixed-
            // separator path is possible on Windows.
            char* nameStart = argv[ i ];
            for( char* p = argv[ i ]; p < ext; p++ )
              if( *p == '/' || *p == '\\' )
                nameStart = p + 1;

            memset( startGameTitle, 0, sizeof( startGameTitle ) );
            size_t len = (size_t)( ext - nameStart );
            if( len >= sizeof( startGameTitle ) )
              len = sizeof( startGameTitle ) - 1;
            memcpy( startGameTitle, nameStart, len );
        }

        if( SDL_strcmp( argv[ i ], "-gbu" ) == 0 )
        {
            writeGbuFiles();
            return 0;
        }

        if( SDL_strcmp( argv[ i ], "-list" ) == 0 )
        {
            listGames();
            return 0;
        }

        if( SDL_strcmp( argv[ i ], "-?" ) == 0 || SDL_strcmp( argv[ i ], "--?" ) == 0 ||
            SDL_strcmp( argv[ i ], "/?" ) == 0 || SDL_strcmp( argv[ i ], "-help" ) == 0 ||
            SDL_strcmp( argv[ i ], "--help" ) == 0 )
        {
            printHelp( argv[ 0 ] );
            return 0;
        }

        if( SDL_strcmp( argv[ i ], "-f" ) == 0 )
          fullscreen = true;

        if( SDL_strcmp( argv[ i ], "-ns" ) == 0 )
          noAudioInit = true;

        if( SDL_strcmp( argv[ i ], "-fps" ) == 0 )
          showFps = true;

        if( SDL_strcmp( argv[ i ], "-nd" ) == 0 )
          noDelay = true;

        if( SDL_strcmp( argv[ i ], "-s" ) == 0 )
          softwareRendering = true;

        if( SDL_strcmp( argv[ i ], "-ms" ) == 0 )
          makeScreenshots = true;

        if( SDL_strcmp( argv[ i ], "-w" ) == 0 && i + 1 < argc )
          windowWidth = SDL_atoi( argv[ i + 1 ] );

        if( SDL_strcmp( argv[ i ], "-h" ) == 0 && i + 1 < argc )
          windowHeight = SDL_atoi( argv[ i + 1 ] );

        if( SDL_strcmp( argv[ i ], "-g" ) == 0 && i + 1 < argc )
        {
            memset( startGameTitle, 0, sizeof( startGameTitle ) );
            strncpy( startGameTitle, argv[ i + 1 ], sizeof( startGameTitle ) - 1 );
        }

        if( SDL_strcmp( argv[ i ], "-gray" ) == 0 && i + 1 < argc )
          grayOverride = SDL_atoi( argv[ i + 1 ] ) ? 1 : 0;
    }

    sdlBackend_setWindowSize( windowWidth, windowHeight );
    sdlBackend_setFullscreen( fullscreen );
    sdlBackend_setVsync( !noDelay );
    sdlBackend_setSoftwareRendering( softwareRendering );

    if( !sdlBackend_init( argc, argv ) )
    {
        SDL_Log( "sdlBackend_init failed, aborting.\n" );
        return 1;
    }

    md_initVideo();

    if( !noAudioInit )
      md_initAudio();

    gamesMain_init();

    if( grayOverride != -1 )
      gamesMain_setRealGrayColor( grayOverride != 0 );

    if( makeScreenshots )
    {
        runBatchScreenshots();
        sdlBackend_shutdown();
        return 0;
    }

    if( startGameTitle[ 0 ] != 0 )
    {
        int idx = gamesMain_findGameByTitle( startGameTitle );
        if( idx != -1 )
        {
            gamesMain_launchGameDirect( idx );
            gamesMain_setLaunchedDirectly( true );
        }
        else
          SDL_Log( "No game titled '%s' found - showing the menu instead.\n", startGameTitle );
    }

    Uint64 perfFreq       = SDL_GetPerformanceFrequency();
    Uint64 fpsWindowStart = SDL_GetPerformanceCounter();
    int    fpsFrameCount  = 0;
    float  avgFps         = 0.0f;

    // showFps never toggles at runtime (no keybind for it), so this
    // one-time "off" call is all that's needed to keep the platform side's
    // own effect-exemption state correct for the whole process lifetime
    // when "-fps" wasn't passed at all.
    if( !showFps )
      md_setFpsOverlayShowing( false, 0, 0 );

    // Fixed-60Hz logic accumulator - every game's own timing (gbUpdate()'s
    // own frame-rate accumulator, and audio's gFrameCounter-based tone-
    // duration scheduling, see sdlBackend.c's own md_updateAudio()) assumes
    // exactly MD_FRAMES_PER_SECOND==60 gamesMain_dispatchFrame() calls
    // happen per real second - true only by accident on an old fixed-60Hz
    // display. With vsync on, SDL_RenderPresent() (called once per real
    // loop iteration, inside md_endFrame() below) paces this whole loop to
    // the display's own ACTUAL refresh rate, whatever that is - a
    // 100Hz/144Hz/etc monitor would otherwise call dispatchFrame()
    // 100/144/etc times per real second, running every game proportionally
    // too fast. Decoupled by accumulating real elapsed wall-clock time
    // (SDL_GetPerformanceCounter(), immune to the display's own refresh
    // rate) and only running a logic tick once a full 1/60s has
    // accumulated - a real fixed-timestep game loop, standard shape.
    //
    // elapsed is clamped to a few ticks' worth before accumulating to avoid
    // a spiral of death after a real stall (window drag, alt-tab, a
    // debugger breakpoint).
    const double logicDt = 1.0 / (double)MD_FRAMES_PER_SECOND;
    const double maxAccumulatedSeconds = logicDt * 5.0;
    double logicAccumulator = 0.0;
    Uint64 lastTicks = SDL_GetPerformanceCounter();

    while( !sdlBackend_shouldQuit() )
    {
        sdlBackend_pollEvents();

        if( sdlBackend_shouldQuit() )
          break;

        Uint64 nowTicks = SDL_GetPerformanceCounter();
        double elapsedSeconds = (double)( nowTicks - lastTicks ) / (double)perfFreq;
        lastTicks = nowTicks;
        if( elapsedSeconds > maxAccumulatedSeconds )
          elapsedSeconds = maxAccumulatedSeconds;
        logicAccumulator += elapsedSeconds;

        while( logicAccumulator >= logicDt )
        {
            gamesMain_dispatchFrame();
            md_updateAudio();

            logicAccumulator -= logicDt;
        }

        if( showFps )
        {
            fpsFrameCount++;
            Uint64 now = SDL_GetPerformanceCounter();
            double elapsed = (double)( now - fpsWindowStart ) / (double)perfFreq;
            if( elapsed >= 1.0 )
            {
                avgFps = (float)( (double)fpsFrameCount / elapsed );
                fpsFrameCount = 0;
                fpsWindowStart = now;
            }
            gamesMain_drawFpsOverlay( avgFps );
        }

        md_endFrame();
    }

    md_stopTone();
    sdlBackend_shutdown();

    return 0;
}
