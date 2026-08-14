#ifndef MACHINE_DEPENDENT_H
#define MACHINE_DEPENDENT_H

// -----------------------------------------------------------------------------
// The per-port interface every Gamebuino Classic compatibility shim
// (gamebuinoShim.h) is built on top of - ported from the sibling
// tinyjoypad_vircon32/Tinyjoypad_SDL projects' own machineDependent.h
// (see Tinyjoypad_SDL/CLAUDE.md's "Translation-unit boundary" section for
// the full rationale on why this must stay a SEPARATE translation unit from
// the "game world" side), adapted for this project's own real hardware
// facts instead of TinyJoypad's: an 84x48 PCD8544 (Nokia 5110) display, 6
// real 8-pixel-tall "pages" (48/8) instead of TinyJoypad's 128x64/8, and 7
// real discrete digital buttons (Up/Down/Left/Right/A/B/C) plus this
// project's own Start/L/R/Y cross-cutting toggles (quit-confirmation
// dialog, pixel-grid overlay, real-gray-color mode, global mute - see
// gamebuino_classic_vircon32/CLAUDE.md for why each of those exists) rather
// than TinyJoypad's simpler D-pad+Fire+Fire2+Start.
//
// Bodies live in each port's own sdlBackend.c ("SDL platform backend" - the
// only files allowed to #include SDL.h, since SDL headers pull in
// <stdint.h>, which would hard-conflict with avrCompat.h's own
// uint8_t-aliased-to-int typedefs if both were ever visible in the same
// translation unit) or, for the Playdate port, main.c directly.
//
// Real Gamebuino Classic hardware streams the PCD8544 one byte at a time,
// one hardware "page" at a time - each byte packs 8 vertical pixels of a
// single column (bit 0 = top, bit 7 = bottom), addressed exactly like an
// SSD1306. There are only 256 possible byte values; the Vircon32 build
// pre-baked a 256-tile texture atlas to blit from (its GPU has no
// CPU-writable framebuffer) - this SDL build needs no such trick.
// md_drawColumn() draws each set pixel as a real GAME_SCALE x GAME_SCALE
// filled rect directly onto one shared "real screen" canvas (see each
// port's own sdlBackend.c VIDEO section comment for why this is one
// persistent canvas, not a separate small framebuffer scaled up at present
// time) - the same canvas md_drawSolidRect() and biosFont.h's menu/dialog
// text both draw onto directly at native 1:1 scale.
// -----------------------------------------------------------------------------

#include <stdbool.h>
#include <stddef.h>

// Real Gamebuino Classic PCD8544 resolution - see
// gamebuino_classic_vircon32/CLAUDE.md's own "Source platform facts"
// section (confirmed directly against the real Gamebuino-Classic library
// source, not assumed from documentation).
#define LCD_WIDTH  84
#define LCD_HEIGHT 48
#define LCD_PAGES  6

// Vircon32's own real screen resolution (its `screen_width`/`screen_height`
// BIOS constants) - kept as this port's own menu/dialog coordinate space
// too, matching the sibling Tinyjoypad_SDL project's own identical choice
// (reuses its own already-extracted biosFont.h verbatim - see that file's
// own header comment). Games never need these (they only ever draw through
// gbXxx() primitives, addressed in LCD_WIDTH/HEIGHT-space) - only the menu
// (menu.c) and the quit-confirmation dialog (gamesMain.c) do.
#define MD_SCREEN_WIDTH  640
#define MD_SCREEN_HEIGHT 360

// =============================================================================
//   VIDEO
// =============================================================================

void md_initVideo();

// Call once per real frame (from gamesMain_dispatchFrame()) with whether a
// game is actually running right now (currentGameIndex != -1) vs. the menu
// being shown - lets each platform side gate its own presentation-only
// effects (pixel-grid overlay - see sdlBackend.c) the same way the sibling
// Tinyjoypad_SDL project's own SDL backends gate glow/CRT/pixel-grid on
// this identical signal: those effects are about making actual gameplay
// look like a specific kind of display, so they'd look wrong applied to
// the plain BIOS-font menu screen too.
void md_setInGame( bool inGame );

// Real 640x360 screen-space rect of the quit-confirmation dialog box (see
// gamesMain.c's own drawConfirmQuitDialog()) - shared here, not just a
// local inside that function, so the platform side (see
// md_setDialogShowing() below) can know exactly which sub-rect to keep
// crisp/effect-free without duplicating (and risking drifting out of sync
// with) the same 4 numbers a second time.
#define MD_DIALOG_X 160
#define MD_DIALOG_Y 110
#define MD_DIALOG_W 320
#define MD_DIALOG_H 140

// Call once per real frame (from gamesMain_dispatchFrame(), alongside
// md_setInGame()) with whether the quit-confirmation dialog is showing
// right now. Unlike md_setInGame() (which stays true for the dialog's own
// entire duration too - it's still real gameplay, just paused), this is
// its own separate signal: the SDL ports' own pixel-grid overlay (gated on
// md_setInGame()) is meant to make the frozen game screen behind/around
// the dialog keep looking like actual gameplay while it's up, but the
// dialog box itself (drawn at MD_DIALOG_X/Y/W/H, above) should stay crisp
// and unaffected by it.
void md_setDialogShowing( bool showing );

// Call once per real frame that the "-fps" overlay is actually being drawn
// (gamesMain_drawFpsOverlay(), each backend's own showFps branch), with
// the exact pixel rect it just drew (always screen-space (0,0), but width
// varies with the digit count of the current FPS text). Same reasoning as
// md_setDialogShowing(): the debug FPS readout should stay crisp and
// legible, not pixel-gridded along with everything else. Pass
// showing=false (width/height ignored) once "-fps" isn't in effect.
void md_setFpsOverlayShowing( bool showing, int width, int height );

// Requests the app quit at the top of the next real frame - the game-world
// side's own equivalent of the platform side's window-close/F4 handling.
// Real Gamebuino hardware never needed a "game world asks the platform to
// exit" concept at all (no real OS process to quit), so this is a
// genuinely new, SDL-platform-specific addition - see gamesMain.c's own
// Start-button handling for its one real call site (a game launched via
// -g/.joy skips the quit-confirmation dialog entirely and quits directly
// instead, matching the sibling Tinyjoypad_SDL project's own identical
// design).
void md_requestQuit();

// clears the screen to WHITE - called once at the start of every game
// frame, before that frame's md_drawColumn() calls. WHITE, not BLACK,
// matches real Gamebuino hardware's own reflective LCD (a "set" pixel is
// dark ink on a light background - see gamebuinoShim.c's own
// gbClear()/md_beginFrame() precedent in the Vircon32 build for the real
// color-inversion bug this project already found and fixed once, doing
// this the other way around).
void md_beginFrame();

// Clears the screen to BLACK - called once by menu.c at the start of every
// menu frame, before that frame's biosDrawText() (white BIOS-font text)
// calls. A genuinely different primitive from md_beginFrame() above, not
// an oversight or a duplicate: the menu is real full-color chrome with the
// OPPOSITE background polarity from a game's own reflective-LCD content
// (white ink on a black background, not the other way around) - matching
// the sibling gamebuino_classic_vircon32 build's own menu.c, which likewise
// calls a separate, direct `clear_screen(color_black)` rather than reusing
// portVircon32.c's own `md_beginFrame()` (its own real
// `clear_screen(color_white)`) for this. gamesMain.c's own quit-
// confirmation dialog deliberately calls NEITHER of these two - it draws
// its box directly on top of whatever's already on screen (see
// drawConfirmQuitDialog()'s own comment), since it's a genuine overlay, not
// a fresh frame.
void md_beginMenuFrame();

// col: 0..83 (LCD x). page: 0..5 (LCD y / 8). value: the raw PCD8544
// column byte (0-255) - a value of 0 means "all 8 pixels off" (i.e. all
// white) and is a real no-op, since the frame was already cleared to white
// by md_beginFrame().
void md_drawColumn( int col, int page, int value );

// Second, targeted pass used only for GB_GRAY content (see gamebuinoShim.c's
// own gbRenderFrame()/gbGrayBuffer doc comments) - draws the exact same 8
// pixels as md_drawColumn() would, but tinted a real, solid mid-gray instead
// of black, and ONLY when gbRealGrayColor is on (gbRenderFrame() only ever
// calls this once gbAnyGrayDrawn is true, which itself only ever becomes
// true while that toggle is on). The sibling Vircon32 build needed a whole
// second pre-baked texture atlas for this because its GPU has no per-pixel
// drawing at all; this SDL build can just draw real per-pixel gray rects
// directly onto the same shared canvas md_drawColumn() already draws onto -
// same col/page/value addressing, no atlas involved.
void md_drawColumnGray( int col, int page, int value );

// waits for vsync (wraps time.h's end_frame())
void md_endFrame();

// Draws a solid-color filled rectangle with its top-left corner at (x, y)
// - used for the quit-confirmation dialog's box (see gamesMain.c's own
// dispatch loop), not something individual games call. This project is
// monochrome throughout (matching the real PCD8544 every game was authored
// for), so `color` is just one of the two constants below rather than a
// real RGB value.
#define MD_COLOR_BLACK 0
#define MD_COLOR_WHITE 1
void md_drawSolidRect( int x, int y, int w, int h, int color );

// Sets up to 32 vertical white pixels in one column, starting at absolute
// pixel row y (NOT page-aligned like md_drawColumn() - bit0 is the pixel
// at row y, bit1 at y+1, etc, for `count` bits). Used by biosFont.h's menu
// text blitter, whose glyphs are 20px tall and don't line up with the 8px
// PCD8544 "page" every game's own md_drawColumn() calls assume - games
// themselves never call this. count is a plain 20-30-ish for a text glyph
// column - well under 32, so a single int always holds the whole run.
void md_drawColumnPixels( int x, int y, int bits, int count );

// A real RGB tint, not one of MD_COLOR_BLACK/WHITE above - unlike every
// game's own strictly-monochrome LCD content, the menu itself is real
// full-color chrome (matching the sibling gamebuino_classic_vircon32
// build's own menu.c, which tints its identical BIOS-font text via a real
// `set_multiply_color(color_red)` call for the exact same reason: flagging
// a `markUnfinished()` game's own list/info text - see menu.c's own
// drawing code). Same bit-run addressing as md_drawColumnPixels() above,
// just with an explicit color instead of always white - kept as a
// separate primitive (rather than adding a color parameter to
// md_drawColumnPixels() itself) so biosFont.h's own biosDrawChar()/
// biosDrawText() stay a byte-for-byte match against the sibling
// Tinyjoypad_SDL project's own identical file (see that file's own header
// comment / the user's own direct request to reuse that project's exact
// menu font) - biosDrawCharColor()/biosDrawTextColor() (added on top, in
// this project's own biosFont.h) are the only callers.
#define MD_COLOR_RED 2
void md_drawColumnPixelsColor( int x, int y, int bits, int count, int color );

// Pixel size of a game's menu thumbnail - shared here so callers (menu.c)
// can lay out around it (e.g. centering it vertically) without duplicating
// the actual asset dimensions.
#define MD_THUMBNAIL_WIDTH  256
#define MD_THUMBNAIL_HEIGHT 128

// How many games have a pre-baked gameplay thumbnail. The menu uses this
// to skip drawing a thumbnail for any game index at or past it (e.g. a
// newly-added game before a thumbnail exists for it), rather than
// assuming every menu entry has one.
int md_getThumbnailCount();

// Draws gameIndex's pre-baked gameplay screenshot (MD_THUMBNAIL_WIDTH x
// MD_THUMBNAIL_HEIGHT) with its top-left corner at (x, y). No-op if
// gameIndex is out of range - callers should still gate on
// md_getThumbnailCount() first rather than relying on this no-op alone,
// since drawing nothing there is a silent no-op, not an error.
void md_drawGameThumbnail( int gameIndex, int x, int y );

// =============================================================================
//   INPUT
// =============================================================================

bool md_inputUp();
bool md_inputDown();
bool md_inputLeft();
bool md_inputRight();

// A level read like the rest, EXCEPT immediately after md_armInputAGate()
// is called: from then until the physical button is actually released,
// this always reports false - see md_armInputAGate()'s own comment.
bool md_inputA();

bool md_inputB();
bool md_inputC();
bool md_inputStart();

// Real-gray-color rendering mode toggle (Button R) - real Gamebuino Classic
// hardware has no equivalent (see gamebuino_classic_vircon32/CLAUDE.md's
// own "Real GRAY color..." section for why the Vircon32 build itself grew
// this). Read directly by gamesMain_dispatchFrame() every frame (unlike the
// sibling Tinyjoypad_SDL project's own glow/CRT/pixel-grid toggles, which
// stay entirely inside each SDL backend) specifically because this one
// flips gameworld-side state (gamebuinoShim.c's own `gbRealGrayColor`) that
// the platform backend has no business reaching into directly. This
// project's own pixel-grid overlay (Button L) and global mute (Button Y)
// toggles, by contrast, need no gameworld-side awareness at all - a pixel-
// grid overlay is a pure presentation effect, and muting is a pure audio-
// hardware volume control (matching the sibling Vircon32 build's own real
// `set_global_volume()` design, with no `gbMuted`-style flag anywhere in
// gamebuinoShim.c to reach) - so both of those stay entirely backend-side,
// exactly like the sibling Tinyjoypad_SDL project's own equivalents.
bool md_inputR();

// Call once, right when a game is (re)launched from the menu, to suppress
// md_inputA() until the confirm press that launched it is physically
// released - otherwise that same press can bleed into the game's very
// first frame and be misread as the player's own input (the exact real
// bug this project's own history calls "the menu-launch button bleeding
// into the game" - see gamebuino_classic_vircon32/CLAUDE.md).
void md_armInputAGate();

// Raw held-frame counters: positive N means "held for N real frames"
// (N==1 the instant it was pressed), negative N means "released N real
// frames ago". See Tinyjoypad_SDL/CLAUDE.md's own machineDependent.h
// comment on md_recentlyPressed() below for the full "why" - some ported
// games throttle their own logic to run once every few real frames, and a
// plain "== 1" edge check misses a tap that started and ended within a
// single skipped frame.
int md_inputUpFrames();
int md_inputDownFrames();
int md_inputLeftFrames();
int md_inputRightFrames();
int md_inputAFrames();

// True if a button's raw held-frame counter shows it became newly pressed
// at any point within the last `window` real frames (inclusive) - the safe
// replacement for a plain "== 1" edge check in any game whose logic only
// ticks once every `window` real frames. window == 1 (an unthrottled game,
// ticking every real frame) reduces this to exactly the traditional
// single-frame edge check.
#define md_recentlyPressed(framesValue, window) ( (framesValue) >= 1 && (framesValue) <= (window) )

// =============================================================================
//   TIMING
// =============================================================================

// Vircon32's own BIOS exposes get_frame_counter()/frames_per_second as real
// built-ins (time.h) - gamebuinoShim.c's own gbFrameCount uses this. No SDL
// equivalent exists, so this backend provides its own: a plain incrementing
// counter, advanced once per real frame by md_updateAudio() (matching the
// Vircon32 build's own frame-counted md_playTone()/md_stopTone() duration
// tracking, which already needed the same idea).
#define MD_FRAMES_PER_SECOND 60
int md_getFrameCounter();

// =============================================================================
//   AUDIO
// =============================================================================

void md_initAudio();

// Starts playing freqHz for durationSeconds, replacing whatever tone is
// currently sounding - matches this project's own real machineDependent.h
// exactly (single-voice, no channel parameter): real Gamebuino Classic
// hardware's own Sound engine has a genuine `NUM_CHANNELS` config knob, but
// it defaults to 1 (see gamebuino_classic_vircon32/CLAUDE.md's own "Source
// platform facts" section, confirmed directly against the real library
// source) - every ported game's own ...Shim.c call site already only ever
// expects one tone active at a time, matching this single-voice interface
// with no adaptation needed. freqHz <= 0 is treated as silence (used by
// one-shot "stop this channel" calls). Does not block: it returns
// immediately and the tone is stopped automatically by md_updateAudio()
// once its duration elapses, so gameplay/animation keeps running during a
// sound effect instead of freezing for it.
void md_playTone( float freqHz, float durationSeconds );

// stops the current tone immediately (no fade) - used when leaving a game
// (returning to the menu) so no audio survives into the next screen
void md_stopTone();

// advances the scheduled auto-stop - call exactly once per frame,
// regardless of which game (if any) is running
void md_updateAudio();

// A real, continuously-retunable sustained tone, for gamebuinoShim.c's own
// tracker/pattern engine - a real note's pitch/volume can change smoothly
// while it's still sounding, driven by a real instrument envelope/slide/
// arpeggio/tremolo effect, unlike md_playTone()'s own fire-and-forget,
// fixed-duration one-shot model (which isn't a fit for that). Returns the
// backend's own internal voice slot now playing freqHz at the given 0..1
// volume, or -1 if every voice is already busy; pass that same value to
// md_trackerVoiceRetune()/md_trackerVoiceStop() for the rest of that
// note's life. Shares the same underlying voice pool as md_playTone().
int md_trackerVoiceStart( float freqHz, float volume );

// Retunes an already-started tracker voice in place - no new attack, no
// click, matching real hardware's own continuously-updated oscillator.
void md_trackerVoiceRetune( int channel, float freqHz, float volume );

// Ends a tracker voice for good (the note itself has finished, not just a
// mid-note retune).
void md_trackerVoiceStop( int channel );

// =============================================================================
//   MEMORY CARD (backs eepromShim.h's persistent per-game EEPROM emulation)
// =============================================================================
// Ported from the sibling tinyjoypad_vircon32/Tinyjoypad_SDL projects' own
// machineDependent.h (thin wrappers there around Vircon32's real memory-
// card hardware) - here each port backs these with a genuine file instead
// (see each port's own sdlBackend.c/main.c for the real read/write
// implementation). offsetBytes/sizeBytes are real bytes, not Vircon32
// ints/words - this project's own eepromShim.c stores its on-disk slot
// data as real bytes, so byte offsets are the natural unit here.

bool md_cardIsConnected();

// true only if the connected card's own signature matches this project's
// fixed signature (see eepromShim.c) - a save file written by an unrelated
// program, or a blank/missing file, both read as false here rather than
// risking a misread of foreign data.
bool md_cardHasOurSignature();

// stamps this project's fixed signature onto the connected card - called
// once, the first time anything is ever written to a fresh/foreign card.
void md_cardWriteSignature();

void md_cardReadData( void* dest, int offsetBytes, int sizeBytes );
void md_cardWriteData( void* src, int offsetBytes, int sizeBytes );

#endif
