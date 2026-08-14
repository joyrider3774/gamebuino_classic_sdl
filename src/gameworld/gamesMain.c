// -----------------------------------------------------------------------------
// Top-level "game world" glue: the quit-confirmation dialog, the real-gray-
// color toggle (Button R), and the menu<->game dispatch
// (gamesMain_dispatchFrame()) main.c calls once per real frame, plus the
// small CLI-support surface (gamesMain.h).
//
// Ported from the sibling Tinyjoypad_SDL project's own gamesMain.c (see
// that file's own header comment for the "why a separate file at all"
// rationale - the quit dialog only ever calls game-world-safe drawing
// functions, so it lives here rather than in main.c) and cross-checked
// against the sibling gamebuino_classic_vircon32 build's own portVircon32.c
// (whose main() owns the equivalent dispatch loop directly, since that
// project has no separate SDL-platform-backend split at all) - real Button
// A (not "Fire"), and the pixel-grid/real-gray/mute toggles that build's
// own main() also owns are split three ways here: pixel-grid and mute stay
// entirely backend-side (see machineDependent.h's own note on why), and
// only the real-gray-color toggle (Button R) is handled here, since it's
// the one that needs to flip gameworld-side state.
//
// Must never #include SDL.h or anything that pulls in <stdint.h> (see
// machineDependent.h's own header comment for why).
// -----------------------------------------------------------------------------

#include "avrCompat.h"
#include "biosFont.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "menu.h"
#include "menuGameList.h"
#include "eepromShim.h"

// -----------------------------------------------------------------------------
// Quit-confirmation dialog - ported from the sibling gamebuino_classic_
// vircon32 build's own portVircon32.c (drawConfirmQuitDialog(), plus
// main()'s own confirmingQuit/confirmSelection state and edge-detection).
// -----------------------------------------------------------------------------

bool confirmingQuit = false;
int confirmSelection = 0; // 0 = NO (default - the safer choice), 1 = YES
bool prevConfirmLeft = false;
bool prevConfirmRight = false;
bool prevConfirmA = false;
bool prevStart = false;

int currentGameIndex = -1;

// Set once by gamesMain_setLaunchedDirectly() when the current game was
// reached via -g/.joy-file instead of the menu - see that function's own
// header comment (gamesMain.h) for why Start then skips the confirm
// dialog and quits directly, and why this is never cleared back to false.
bool gLaunchedDirectly = false;

void gamesMain_setLaunchedDirectly( bool direct )
{
    gLaunchedDirectly = direct;
}

void drawConfirmQuitDialog()
{
    int boxX = MD_DIALOG_X, boxY = MD_DIALOG_Y, boxW = MD_DIALOG_W, boxH = MD_DIALOG_H;
    int borderThickness = 6;

    md_drawSolidRect( boxX, boxY, boxW, boxH, MD_COLOR_WHITE );
    md_drawSolidRect
    (
        boxX + borderThickness, boxY + borderThickness,
        boxW - ( borderThickness * 2 ), boxH - ( borderThickness * 2 ),
        MD_COLOR_BLACK
    );

    biosDrawText( "CONFIRM", boxX + 125, boxY + 20 );
    biosDrawText( "QUIT TO MENU?", boxX + 95, boxY + 55 );

    int yesX = boxX + 100;
    int noX = boxX + 210;
    int optionsY = boxY + 95;

    if( confirmSelection == 1 )
      biosDrawText( ">", yesX - 15, optionsY );
    else
      biosDrawText( ">", noX - 15, optionsY );

    biosDrawText( "YES", yesX, optionsY );
    biosDrawText( "NO", noX, optionsY );
}

// -----------------------------------------------------------------------------
// Real-gray-color toggle (Button R) - see machineDependent.h's own
// md_inputR() comment for why this is the one cross-cutting toggle handled
// here rather than entirely backend-side. Global, not gated to gameplay-
// only (matching the sibling Vircon32 build's own identical choice) -
// harmless to flip on the menu even though the menu itself never draws
// GB_GRAY, and it means the setting is already correct the instant a game
// that does use it starts.
// -----------------------------------------------------------------------------

bool prevGrayButton = false;

// The whole top-level menu<->game dispatch, called once per real frame by
// main.c - matches the sibling gamebuino_classic_vircon32 build's own
// main() loop body exactly (state transitions only; md_updateAudio()/
// md_endFrame() stay in main.c itself, called unconditionally afterward
// every frame regardless of which branch below ran).
void gamesMain_dispatchFrame()
{
    bool start = md_inputStart();
    bool justStarted = ( start && !prevStart );
    prevStart = start;

    if( confirmingQuit )
    {
        bool left = md_inputLeft();
        bool right = md_inputRight();
        bool a = md_inputA();
        bool justLeft = ( left && !prevConfirmLeft );
        bool justRight = ( right && !prevConfirmRight );
        bool justA = ( a && !prevConfirmA );
        prevConfirmLeft = left;
        prevConfirmRight = right;
        prevConfirmA = a;

        if( justLeft || justRight )
          confirmSelection = 1 - confirmSelection;

        if( justA )
        {
            // The same physical press that just confirmed this dialog must
            // not also register as a fresh press once we're back in the
            // menu (instantly launching whatever's highlighted) or back in
            // gameplay (an unwanted in-game action the instant it resumes)
            // - md_inputA() is the single shared gate every gbPressed(BTN_A)
            // read goes through, so arming it here covers both destinations.
            md_armInputAGate();
            if( confirmSelection == 1 )
            {
                md_stopTone();
                currentGameIndex = -1;
                menu_init();
            }
            else if( menu_getGame( currentGameIndex )->onResume != NULL )
              menu_getGame( currentGameIndex )->onResume();
            confirmingQuit = false;
        }
        else if( justStarted )
        {
            // pressing Start again cancels, same as selecting NO
            if( menu_getGame( currentGameIndex )->onResume != NULL )
              menu_getGame( currentGameIndex )->onResume();
            confirmingQuit = false;
        }

        drawConfirmQuitDialog();
    }
    else if( currentGameIndex != -1 && justStarted && gLaunchedDirectly )
    {
        // No menu to return to in this mode (see
        // gamesMain_setLaunchedDirectly()'s own comment) - matches the
        // sibling Tinyjoypad_SDL project's own identical -g/.joy-file
        // behavior: quits directly, no confirmation dialog at all.
        md_requestQuit();
    }
    else if( currentGameIndex != -1 && justStarted )
    {
        confirmingQuit = true;
        confirmSelection = 0;
        // Arm against whatever Left/Right/A happen to already be held at
        // this exact moment, the same reasoning as md_armInputAGate() -
        // otherwise a leftover press from gameplay could immediately
        // register as a dialog input.
        prevConfirmLeft = md_inputLeft();
        prevConfirmRight = md_inputRight();
        prevConfirmA = md_inputA();
        drawConfirmQuitDialog();
    }
    else if( currentGameIndex == -1 )
    {
        int chosen = menu_update();
        if( chosen != -1 )
        {
            currentGameIndex = chosen;
            md_armInputAGate();

            // Clear to white once, immediately on selection and before the
            // chosen game's own init() runs any of its own code - some
            // games' init() doesn't necessarily draw a full frame of its
            // own right away, and this project's own persistent screen
            // canvas behaves exactly like real Gamebuino VRAM does when
            // nothing redraws it - without this, the last menu frame would
            // otherwise still be sitting on screen for that one gap tick
            // instead of a clean transition.
            md_beginFrame();

            // Resolve/load this game's own persistent EEPROM slot (looked
            // up by its title, not by chosen/registration index - see
            // eepromShim.c) before init() runs, since a game's own init()
            // is what actually calls eeprom_read_byte()/etc to load its
            // saved high score.
            eepromSelectGame( menu_getGame( chosen )->title );

            menu_getGame( chosen )->init();
        }
    }
    else
    {
        menu_getGame( currentGameIndex )->update();
    }

    // Button R toggles real-solid-gray-color rendering globally - see
    // prevGrayButton's own comment above. `gbRealGrayColor` itself
    // (gamebuinoShim.h/.c) is what every drawing primitive/gbRenderFrame()
    // actually reads every frame - this is just the button edge-detect
    // that flips it.
    bool grayButton = md_inputR();
    if( grayButton && !prevGrayButton )
      gbRealGrayColor = !gbRealGrayColor;
    prevGrayButton = grayButton;

    md_setInGame( currentGameIndex != -1 );
    md_setDialogShowing( confirmingQuit );
}

void gamesMain_init()
{
    addGames();
    menu_init();

    // Real solid-gray GB_GRAY rendering, on by default here - a deliberate
    // choice for the two ports that actually call this function (SDL2/
    // SDL3, both genuine true-color displays where "solid gray" and the
    // real hardware's own checkerboard-flicker dither are two visibly
    // different, equally legitimate looks), overriding gamebuinoShim.c's
    // own real-hardware-matching default of off. Still a live, per-session
    // Button R toggle from here - this only sets the starting state.
    // Playdate's own main.c never calls gamesMain_init() at all (see that
    // file's own header comment on why it has its own from-scratch
    // dispatch loop), so this default has no effect there - correct, since
    // a strictly 1-bit panel has no analog "solid gray" to begin with, and
    // that port's own real "grayscale," the checkerboard dither, is
    // already unconditionally on regardless of this flag.
    gbRealGrayColor = true;
}

// Lets each SDL port's own CLI ("-gray 0"/"-gray 1") override the default
// gamesMain_init() just set, without main.c needing to reach into
// gamebuinoShim.c's own gbRealGrayColor global directly (main.c only
// talks to the game-world side through gamesMain.h/machineDependent.h's
// own public surface, never a shim global by name). Added specifically to
// make regenerating this project's own metadata screenshots/thumbnails
// with a deliberately different gray setting than the live app's own
// default a real, repeatable one-flag command instead of a temporary
// source edit + rebuild + revert cycle each time.
void gamesMain_setRealGrayColor( bool on )
{
    gbRealGrayColor = on;
}

// -----------------------------------------------------------------------------
// CLI-support surface - see gamesMain.h for what each of these is for.
// -----------------------------------------------------------------------------

int gamesMain_getGameCount()
{
    return gameCount;
}

char* gamesMain_getGameTitle( int idx )
{
    if( idx < 0 || idx >= gameCount )
      return NULL;

    return menu_getGame( idx )->title;
}

static int gamesMainStrcmpNoCase( char* a, char* b )
{
    while( *a && *b )
    {
        int ca = *a, cb = *b;
        if( ca >= 'a' && ca <= 'z' ) ca -= 32;
        if( cb >= 'a' && cb <= 'z' ) cb -= 32;
        if( ca != cb )
          return ca - cb;
        a++;
        b++;
    }
    return *a - *b;
}

int gamesMain_findGameByTitle( char* title )
{
    for( int i = 0; i < gameCount; i++ )
      if( gamesMainStrcmpNoCase( menu_getGame( i )->title, title ) == 0 )
        return i;

    return -1;
}

void gamesMain_launchGameDirect( int idx )
{
    if( idx < 0 || idx >= gameCount )
      return;

    currentGameIndex = idx;
    md_armInputAGate();

    // Same reasoning as gamesMain_dispatchFrame()'s own menu-selection
    // branch above - matters even more here, since this same function
    // also backs -ms's batch screenshot mode, which launches every game
    // back-to-back into the same persistent screen canvas: without this, a
    // game whose own init() doesn't draw a full frame immediately would
    // have its very first screenshot capture the PREVIOUS game's leftover
    // frame instead of a clean start.
    md_beginFrame();

    // Same reasoning as gamesMain_dispatchFrame()'s own menu-selection
    // branch above - matters here too, since this same function also
    // backs -ms's batch screenshot mode, which launches every game back-
    // to-back: without this, a game whose own init() reads a saved high
    // score would still see whatever the PREVIOUS game's own slot left in
    // currentSlot instead of its own.
    eepromSelectGame( menu_getGame( idx )->title );

    menu_getGame( idx )->init();
}

void gamesMain_drawFpsOverlay( float fps )
{
    int whole = (int)fps;
    int frac = (int)( ( fps - (float)whole ) * 100.0f + 0.5f );
    if( frac >= 100 )
    {
        frac -= 100;
        whole++;
    }

    char wholeText[ 8 ];
    char fracText[ 8 ];
    itoa( whole, wholeText, 10 );
    itoa( frac, fracText, 10 );

    char fpsText[ 24 ];
    strcpy( fpsText, "FPS " );
    strcat( fpsText, wholeText );
    strcat( fpsText, "." );
    if( frac < 10 )
      strcat( fpsText, "0" );
    strcat( fpsText, fracText );

    int textW = biosTextWidth( fpsText );
    int rectW = textW + 8;
    int rectH = BIOS_FONT_CHAR_H + 4;
    md_drawSolidRect( 0, 0, rectW, rectH, MD_COLOR_BLACK );
    biosDrawText( fpsText, 4, 2 );

    // See machineDependent.h's own md_setFpsOverlayShowing() comment - this
    // is what keeps the readout itself crisp/unblurred on top of the SDL
    // ports' own presentation effects, the same way md_setDialogShowing()
    // already does for the quit-confirmation dialog box.
    md_setFpsOverlayShowing( true, rectW, rectH );
}
