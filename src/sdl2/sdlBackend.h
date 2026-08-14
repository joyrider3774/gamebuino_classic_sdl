#ifndef SDL_BACKEND_H
#define SDL_BACKEND_H

// -----------------------------------------------------------------------------
// Platform-only extras that main.c needs beyond machineDependent.h's own
// contract - main.c and sdlBackend.c are both part of the "SDL platform
// backend" half of this project (see machineDependent.h's own comment on
// the TU split), so both are free to #include SDL.h directly and share
// declarations here. The "game world" side (avrCompat.h, the shims, every
// games/*.c file, menu.c) never sees this header.
//
// Identical to src/sdl3/sdlBackend.h - this header itself never includes
// SDL.h and declares no SDL-typed parameter, so nothing here needed to
// change for the SDL2 port at all.
// -----------------------------------------------------------------------------

#include <stdbool.h>

// Config setters - call any of these BEFORE sdlBackend_init() to override
// its defaults. sdlBackend_init() itself still takes argc/argv (used only
// for logging), so CLI parsing/ownership stays in main.c rather than
// duplicated here.
void sdlBackend_setWindowSize( int width, int height );
void sdlBackend_setFullscreen( bool fullscreen );
// Vsync-locked pacing (the default) vs uncapped ("-nd", run as fast as
// possible).
void sdlBackend_setVsync( bool enabled );
// Forces SDL's built-in software renderer ("-s") instead of the default
// auto-picked (typically hardware-accelerated) one.
void sdlBackend_setSoftwareRendering( bool enabled );

// Creates the window/renderer/framebuffer surface, opens a gamepad (if any)
// via CInput, and initializes audio. Returns false if SDL init failed.
bool sdlBackend_init( int argc, char** argv );

void sdlBackend_shutdown();

// Pumps SDL's OS event queue and refreshes CInput's button state - call
// once per real frame, before reading any md_input*() function.
void sdlBackend_pollEvents();

// True once the window has been closed / quit requested (F4, window X).
bool sdlBackend_shouldQuit();

// Saves the current screen contents (post md_endFrame()'s own gScreen
// state) as a BMP - used by -ms's batch screenshot mode. Returns false on
// failure (e.g. video not initialized yet).
bool sdlBackend_saveScreenshot( const char* path );

// Feeds one simulated frame of Button A's raw held-frame counter, bypassing
// real device polling entirely - used only by -ms's batch screenshot mode
// to script a "tap A once" gesture per game (one frame released, one frame
// pressed, one frame released again, then a plain run of idle frames)
// without needing a real keyboard/gamepad press.
void sdlBackend_simulateAFrame( bool pressed );

// Same idea as sdlBackend_simulateAFrame(), for Up - some games need a
// direction genuinely HELD across many frames to reach a real "still
// alive, actively playing" state for -ms rather than a single tap.
void sdlBackend_simulateUpFrame( bool held );

// Same idea as sdlBackend_simulateUpFrame(), for Down/Left/Right - some
// -ms screenshot scripts need to navigate a menu (Down to move a cursor)
// or hold a direction other than Up to reach real gameplay - e.g. a maze
// game whose player only advances along a rightward corridor.
void sdlBackend_simulateDownFrame( bool held );
void sdlBackend_simulateLeftFrame( bool held );
void sdlBackend_simulateRightFrame( bool held );

// Same tap-gesture idea as sdlBackend_simulateAFrame(), for Button B/C -
// some -ms screenshot scripts need a genuine B or C press (not just A) to
// advance past a real "B: Rotate"/"B: Deal"/"B TO CONFIRM"-style prompt,
// or to dismiss a real title/prologue chain that upstream itself gates on
// a button other than A.
void sdlBackend_simulateBFrame( bool pressed );
void sdlBackend_simulateCFrame( bool pressed );

#endif
