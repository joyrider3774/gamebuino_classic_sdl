#ifndef MENU_H
#define MENU_H

// A real, previously-latent header-hygiene bug, found via a real macOS/
// Clang build failure ("unknown type name 'bool'"): this header uses
// `bool` below without including <stdbool.h> itself, instead silently
// relying on whichever caller happens to #include it doing so first (in
// practice, machineDependent.h, transitively) - worked by accident under
// MinGW/Linux GCC's own laxer handling here, since every real caller of
// this header so far also happens to include machineDependent.h
// somewhere in the same translation unit, but menu.c's own real include
// order puts machineDependent.h AFTER menu.h, so that transitive
// availability was never actually guaranteed - Clang correctly rejected
// it. Every header should include what it directly uses, not rely on
// inclusion order - fixed here rather than reordering any caller's own
// #include list.
#include <stdbool.h>

// Game-select menu. Ported from the sibling gamebuino_classic_vircon32
// build's own menu.c/menu.h (itself modeled on the tinyjoypad_vircon32/
// Tinyjoypad_SDL projects' own identical-shaped menu) - draws with the real
// Vircon32 BIOS font (biosFont.h, copied verbatim from the sibling
// Tinyjoypad_SDL project per direct user request) rather than a custom
// glyph renderer.
//
// Two dialect fixes versus the Vircon32 build this was ported from:
//  - GameFunc is a real standard-C function-pointer typedef
//    (`typedef void (*GameFunc)( void );`) - Vircon32's own dialect instead
//    writes this as `typedef void(void) GameFunc;` with the `*` added back
//    at each *use* site; here the `*` lives in the typedef itself, so a use
//    site just declares `GameFunc init` with no extra star.
//  - `title`/`author`/`info` are `char*` here, not `int*` - Vircon32
//    strings are `int[]` (one word per character), standard C strings are
//    `char[]`.

typedef void (*GameFunc)( void );

typedef struct
{
    char* title;
    // Original game's author/credit (e.g. "AURELIEN RODOT") - shown as
    // "BY <author>" under the menu's thumbnail screenshot.
    char* author;
    // Optional (pass NULL if not needed) - a second line shown directly
    // below the author credit. Two real, independent uses share this one
    // field: a porter credit continuation for a real combined "original
    // author / porter" attribution too long for one line, or a short
    // reason for a game flagged via markUnfinished() below (e.g. "Ball can
    // get stuck"). Drawn in white normally, red when this game is
    // unfinished - see menu_update()'s own drawing code.
    char* info;
    GameFunc init;
    GameFunc update;
    // Optional (pass NULL if not needed) - called once when this game
    // resumes after being fully frozen for the quit-confirmation dialog
    // (see gamesMain.c's own dispatch loop). Most games redraw their whole
    // screen unconditionally every update() call, so freezing/resuming
    // them is transparent - but a game that skips its own redraw entirely
    // on frames where nothing changed (a dirty-flag optimization) needs
    // this hook to force that flag back to true, or its next real update()
    // could also skip drawing and leave the dialog's pixels on screen
    // instead of the game's own content. NULL is correct (and is all any
    // Gamebuino game needs today) for any game whose own update() always
    // redraws unconditionally rather than skipping frames where nothing
    // changed - see gamebuinoShim.h's own gbUpdate()/gbRenderFrame() doc
    // comments for why that's true of every game here by construction.
    GameFunc onResume;
    // Still fully registered/playable/thumbnailed like any other game
    // (see markUnfinished() below) - just drawn with reddish list text as
    // a visual "known incomplete" warning. Defaults to false in addGame().
    bool unfinished;
} Game;

extern int gameCount;

// Returns the index this game was registered at (or -1 if MAX_GAMES was
// already reached) - pass straight into markUnfinished() below if needed.
int addGame( char* title, char* author, char* info, GameFunc init, GameFunc update, GameFunc onResume );

// Flags a registered game as unfinished - still shown, selectable, and
// playable exactly like any other game (registration index and thumbnail
// mapping untouched), just drawn with reddish list text - see the real
// use case in menuGameList.c.
void markUnfinished( int index );

Game* menu_getGame( int index );
void menu_init();

// draws the menu and handles its own navigation input; returns the game
// just chosen (Button A pressed on it) this frame, or -1 if none was
int menu_update();

#endif
