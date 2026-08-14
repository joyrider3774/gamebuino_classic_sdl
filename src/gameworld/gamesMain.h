#ifndef GAMES_MAIN_H
#define GAMES_MAIN_H

// -----------------------------------------------------------------------------
// The narrow surface main.c (the "SDL platform backend" side) needs to reach
// into the "game world" side. This header itself never includes SDL.h or
// avrCompat.h, so it's safe for main.c to include alongside SDL3/SDL.h.
//
// Ported from the sibling Tinyjoypad_SDL project's own gamesMain.h/.c pair -
// same overall shape (menu<->game dispatch + quit-confirmation dialog +
// CLI-support surface), with three real, deliberate differences: real
// Button A (not "Fire"), and two more cross-cutting toggles this project's
// own real Gamebuino hardware model needs that TinyJoypad's never did
// (Button L pixel-grid overlay, Button R real-gray-color mode) - both
// handled here rather than staying backend-only the way this project's own
// mute toggle (Button Y) and TinyJoypad's own glow/CRT/pixel-grid toggles
// all do, since both L and R need to reach gameworld-side state
// (gamebuinoShim.c's own gbRealGrayColor - see machineDependent.h's own
// md_inputL()/md_inputR()/md_inputY() comment for the full reasoning).
// -----------------------------------------------------------------------------

#include <stdbool.h>

// Registers every ported game (addGames()) and initializes the menu - call
// once, before the main loop starts.
void gamesMain_init();

// Overrides the real-gray-color default gamesMain_init() itself just set
// (on) - main.c's own "-gray 0"/"-gray 1" CLI flag calls this once, right
// after gamesMain_init(), so metadata screenshot/thumbnail generation can
// deliberately capture with a different gray setting than the live app's
// own default without editing source. See gamesMain.c's own definition
// comment for the full reasoning.
void gamesMain_setRealGrayColor( bool on );

// Runs one frame of the menu<->game top-level dispatch (quit-confirm
// dialog, A-gate arming, pixel-grid/real-gray/mute toggles, menu
// selection, or the current game's own update()) - call once per real
// frame from main.c's own loop. Does NOT call md_updateAudio()/
// md_endFrame() itself - those stay in main.c, called unconditionally
// every frame regardless of what this function did (matches the sibling
// gamebuino_classic_vircon32 build's own main() shape).
void gamesMain_dispatchFrame();

// -----------------------------------------------------------------------------
// CLI-support surface (-list / -g <NAME> / -ms / .joy file handling) - main.c
// needs these to enumerate/launch games directly, bypassing the menu, without
// reaching into menu.c's own games[]/gameCount internals itself.
// -----------------------------------------------------------------------------

// How many games are registered (games[]/gameCount's own registration order,
// same order menu_getGame()/gamesMain_getGameTitle() index by - NOT the
// menu's alphabetized display order).
int gamesMain_getGameCount();

// Title of the game at registration index idx (as passed to addGame() in
// menuGameList.c - already all-uppercase there). NULL if idx is out of range.
char* gamesMain_getGameTitle( int idx );

// Case-insensitive exact-match lookup by title (matching the sibling
// project's own -g/.joy-file title-matching convention) - returns the
// registration index, or -1 if no game has that title.
int gamesMain_findGameByTitle( char* title );

// Launches game idx directly, bypassing the menu entirely (same effect as
// picking it from the menu and pressing Button A) - used by -g/.joy-file
// direct launch and by -ms's batch screenshot mode. No-op if idx is out of
// range.
void gamesMain_launchGameDirect( int idx );

// Marks the CURRENTLY running game as having been launched straight from
// the command line (-g/.joy-file), not picked from the menu - call once,
// right after the gamesMain_launchGameDirect() call that does that actual
// launch (NOT the one -ms's batch screenshot mode also uses - that path
// never reaches gamesMain_dispatchFrame()'s own interactive loop at all,
// so this would be meaningless there). Once set, pressing Start skips the
// quit-confirmation dialog entirely and quits the app directly instead of
// returning to the menu - there's no menu to return to in this mode,
// matching how the game was reached in the first place. Never cleared
// afterward - quitting is the only way out of this mode by design, so
// there's no scenario where it would need to be un-set.
void gamesMain_setLaunchedDirectly( bool direct );

// Draws a small "FPS NN.NN" readout (black backing rect + white BIOS-font
// text) in the screen's top-left corner - call once per real frame, after
// gamesMain_dispatchFrame(), when -fps is active.
void gamesMain_drawFpsOverlay( float fps );

#endif
