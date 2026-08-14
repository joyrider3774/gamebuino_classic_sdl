#ifndef GAMEBUINO_SHIM_H
#define GAMEBUINO_SHIM_H

#include <stdbool.h>

// -----------------------------------------------------------------------------
// Reproduces the real Gamebuino Classic library's own API surface
// (Gamebuino.h/utility/Display.h/Buttons.h/Sound.h) on top of
// machineDependent.h - ported from the sibling gamebuino_classic_vircon32
// project's own identically-named file (see that file's own header comment
// for the full "flatten a real single-instance C++ library into plain C
// globals/functions" rationale, already proven there against 99 real
// games), converted from Vircon32's own dialect back to standard C the same
// mechanical way the sibling Tinyjoypad_SDL project converted
// tinyJoypadShim.h/obonoCoreShim.h (see that project's own CLAUDE.md
// "Dialect conversion" section for the general recipe): `int[N] name` ->
// `int name[N]` array declarations, and the one genuine text-string
// parameter (gbPrintString()'s/gbPopup()'s own `text`) -> `char*`, since
// Vircon32 strings are `int[]` (one 32-bit word per character) and a raw
// `int*` receiving a real C string literal would reinterpret its bytes as
// garbage ints. Bitmap/font data tables (gbFont5x7/3x5/3x3, and every
// game's own sprite arrays) stay `int[]`/`int*` - genuinely numeric byte
// tables, not text, so no dialect-conversion recipe item applies to them
// beyond the array-declaration-order fix.
//
// This dialect has no classes/methods/operator overloading - real `gb.
// display.fillRect(...)`/`gb.buttons.pressed(...)`/`gb.sound.playTick()`
// call syntax cannot be preserved literally in the Vircon32 source this was
// ported from either. Every ported game's own `gb.x.y(...)` call sites were
// already mechanically rewritten to a plain `gbY(...)` function call there
// (there is only ever one `gb` instance in any real cartridge anyway, so
// flattening loses nothing) - see gamePong.c's own header comment in the
// sibling project for a worked example of the exact rewrites this needed.
//
// Real hardware has a genuine 84x48 PCD8544 (Nokia 5110) CPU-writable
// framebuffer (Display::_displayBuffer[]) - ported as a plain in-RAM
// int[LCD_WIDTH*LCD_PAGES] framebuffer here, matching the sibling Vircon32
// build's own identical choice.
//
// Text rendering ports Gamebuino's own real font3x5/font5x7/font3x3 bitmap
// fonts directly (see this file's own Font tables section in
// gamebuinoShim.c) - real setFont()/print() semantics, including real
// per-font inter-char/line spacing.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//   Configuration
// -----------------------------------------------------------------------------
// By default, GB_GRAY renders exactly like real hardware: a checkerboard
// dither that flickers with the frame counter (see GB_GRAY's own doc
// comment below). `gbRealGrayColor` is a runtime toggle (default off),
// bound to Button R and read directly by gamesMain.c's own dispatch loop
// (see that file's own comment on why this needs gameworld-side awareness,
// unlike this project's own pixel-grid overlay/mute toggles, which stay
// entirely inside each SDL backend) - that renders GB_GRAY as a real,
// solid, flat gray color instead. A deliberate visual enhancement beyond
// real hardware, not a compatibility requirement, ported from the sibling
// Vircon32 build's own identical feature.
extern bool gbRealGrayColor;

#define LCDWIDTH LCD_WIDTH
#define LCDHEIGHT LCD_HEIGHT

// Button IDs - real hardware has these wired to specific digital pins
// (see the real library's own utility/settings.c), irrelevant here since
// every gbXxx() input function goes straight through machineDependent.h's
// own md_input*() calls instead of reading a raw pin.
#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_A     4
#define BTN_B     5
#define BTN_C     6

// -----------------------------------------------------------------------------
//   Core / lifecycle
// -----------------------------------------------------------------------------

// Call once from a game's own init(). Frame rate defaults to 20 (matching
// real Gamebuino Classic's own out-of-the-box default - `Gamebuino::begin()`
// sets `timePerFrame = 50` directly, i.e. 1000/50 = 20fps - confirmed
// directly in the real Gamebuino.cpp, not assumed; easy to mix up with the
// later META library's own different 25fps default) - call
// gbSetFrameRate() afterward if a game needs a different rate.
void gbBegin();

void gbSetFrameRate( int fps );

// Plain read accessor for the current requested frame rate (defaults to
// 20 - see gbBegin()'s own doc comment - until/unless a game calls
// gbSetFrameRate() itself). Not needed by SDL2/SDL3 (their own fixed-60Hz
// dispatch loop never needs to know it), but used by the Playdate port's
// own main.c to keep pd->display->setRefreshRate() in sync with whatever
// rate the currently-running game actually wants - see that file's own
// comment for the full reasoning.
int gbGetFrameRate();

// Port-level primitive - retargets gbUpdate()'s own internal accumulator
// to sub-sample against `hz` real engine ticks/second instead of the
// default MD_FRAMES_PER_SECOND (60). NOT meant to be called by a game
// itself - only by a port's own machine-dependent layer, and only a port
// whose own real dispatch-callback rate genuinely isn't a fixed 60Hz
// (SDL2/SDL3 never call this at all, leaving the default in place; the
// Playdate port calls it once at startup and again any time it retargets
// its own pd->display->setRefreshRate() to a different value, so the two
// always stay in lock-step - see src/playdate/main.c's own header comment
// for the full "why" this exists at all).
void gbSetEngineFrameRate( int hz );

// Real gb.update() both throttles to the configured frame rate AND clears+
// redraws the display buffer's own "changed since last frame" bookkeeping -
// ported as a plain whole-tick throttle (see gbUpdate()'s own definition):
// returns true on the one real engine tick that should actually run this
// frame's worth of game logic. A ported game's own loop()-equivalent should
// be structured as `if( gbUpdate() ) { ... }`, exactly mirroring upstream's
// own `if (gb.update()) { ... }` shape.
bool gbUpdate();

// Streams the current framebuffer to the real screen via md_drawColumn() -
// every ported game's own `_update()` function MUST call this exactly once,
// as its very last statement, on every tick `gbUpdate()` returns true (see
// gamePong.c's own `gameXxx_update()` for the exact shape every other
// shipped game already follows: `if( !gbUpdate() ) return; ...draw...;
// gbRenderFrame();`). Forgetting this call is a real, easy-to-make mistake
// with a deceptively unhelpful symptom - every draw call still runs and
// writes into the framebuffer correctly, but nothing ever reaches the
// screen, so the game silently renders as a permanently blank display with
// no error of any kind.
void gbRenderFrame();

// Real Gamebuino::frameCount - a real, public tick counter, incremented
// once per real logic tick by gbUpdate() itself (see its own definition).
// Reset to 0 by gbBegin() (a real, considered adaptation for this
// project's own multi-game-per-session cartridge model - see gbBegin()'s
// own comment). Useful directly for animation/blink pacing (`frameCount %
// N`-style checks).
extern int gbFrameCount;

void gbPickRandomSeed(); // no-op - see this file's own header comment

// -----------------------------------------------------------------------------
//   Buttons
// -----------------------------------------------------------------------------

bool gbPressed( int button );  // true on the exact tick the button transitions to held
bool gbReleased( int button ); // true on the exact tick the button transitions to released
bool gbHeld( int button, int frames ); // true if held for at least `frames` real ticks
// Real Buttons::timeHeld() - the actual real tick count a button has been
// held, for games that need the number itself (variable jump height, a
// charge-up mechanic) rather than just a threshold check.
int gbTimeHeld( int button );
// true once immediately on press, then true again every `period` ticks
// while still held - matches real Buttons::repeat()'s own auto-repeat feel
bool gbRepeat( int button, int period );

// -----------------------------------------------------------------------------
//   Display
// -----------------------------------------------------------------------------

void gbClear();
// `color` is a real, confirmed no-op - real Display::fillScreen() always
// fills solid BLACK regardless of what color is passed (a genuine real
// hardware bug - see gamebuinoShim.c's own header comment on this
// function). Kept as a parameter anyway so call sites stay a literal
// match for real `gb.display.fillScreen(...)`.
void gbFillScreen( int color );

// Real hardware's own real color constants (Display.h: WHITE=0, BLACK=1,
// INVERT=2, GRAY=3) - GB_ prefixed since a bare `WHITE`/`BLACK` could
// collide with some ported game's own identically-named local otherwise.
// GB_GRAY is a real, genuine draw color too (real Display.h's own GRAY=3,
// enabled by default on real hardware via ENABLE_GRAYSCALE) - a checkerboard
// dither that flickers with the frame counter, not a flat fill (see
// gbSetColor()'s own doc comment below).
#define GB_WHITE 0
#define GB_BLACK 1
#define GB_INVERT 2
#define GB_GRAY 3

// Direct port of real Display::setColor(color) - color is one of
// GB_WHITE/GB_BLACK/GB_INVERT/GB_GRAY above. GB_INVERT toggles whatever's
// already on screen at each drawn pixel (a real XOR against the
// framebuffer, not a fixed color). GB_GRAY is a real checkerboard dither,
// ported directly from real `Display::drawPixel()`'s own formula: each
// pixel draws BLACK or WHITE depending on `(x&1)^(y&1)` XORed against the
// low bit of `gbFrameCount`, so the pattern also flips every other real
// tick - a genuine two-pixel-period spatial dither that also flickers over
// time, exactly like real hardware's own default (non-optional) GRAY
// behavior, not a flat gray fill (this display has no such thing - it's
// 1-bit). gbDrawPixel()/gbFillRect()/gbDrawFastHLine()/gbDrawFastVLine()/
// gbDrawBitmap()/gbDrawBitmapRotated()/gbDrawChar() (and everything built
// on top of them - circles, triangles, rounded rects, gbDrawLine()) all
// support both GB_INVERT and GB_GRAY, matching real hardware, where both
// are real `color` values read by the same low-level `drawPixel()` every
// other primitive is built on - gbDrawChar()'s own separate `gbBgColor`
// mechanism is unaffected, matching real hardware's own drawChar()
// likewise never checking for INVERT/GRAY itself (it just calls
// drawPixel()/fillRect(), which already do).
void gbSetColor( int color );

// Direct port of real Display::setColor(color, bg) (the two-argument
// overload) - only gbDrawChar() ever reads the background half of this
// (see its own header comment in gamebuinoShim.c): with `bg` different
// from `color`, a printed glyph's own "off" pixels are drawn in `bg`
// (a real opaque text background) instead of staying transparent.
void gbSetColorBg( int color, int bg );
void gbDrawPixel( int x, int y );
int gbGetPixel( int x, int y );
void gbDrawLine( int x0, int y0, int x1, int y1 );
void gbDrawFastHLine( int x, int y, int w );
void gbDrawFastVLine( int x, int y, int h );
void gbDrawRect( int x, int y, int w, int h );
void gbFillRect( int x, int y, int w, int h );
void gbDrawCircle( int x0, int y0, int r );
void gbFillCircle( int x0, int y0, int r );

// Direct ports of real Display::drawRoundRect()/fillRoundRect()/
// drawTriangle()/fillTriangle() (utility/Display.cpp).
void gbDrawRoundRect( int x, int y, int w, int h, int r );
void gbFillRoundRect( int x, int y, int w, int h, int r );
void gbDrawTriangle( int x0, int y0, int x1, int y1, int x2, int y2 );
void gbFillTriangle( int x0, int y0, int x1, int y1, int x2, int y2 );

// Real Gamebuino Classic's own Display::drawBitmap(), confirmed directly
// against the real Display.cpp source rather than assumed. `bitmap[0]`/
// `[1]` are the real width/height header bytes, then `ceil(width/8)` bytes
// per row (row-major, MSB-first) - this is a DIFFERENT byte layout from
// this shim's own `gbFrameBuffer[]` (column-page, LSB=top - see this
// file's own header comment) - the two must not be confused. Every real
// PROGMEM byte becomes one plain int cell here (matching every other byte
// table in this project, e.g. gbFont5x7/gbFont3x5). Only "on" bits are drawn, in
// whatever color gbSetColor() last set - "off" bits are fully transparent,
// leaving whatever's already on screen untouched, exactly like real
// hardware's own bitmap compositing.
void gbDrawBitmap( int x, int y, int* bitmap );

// rotation: 0=none, 1=CCW, 2=180, 3=CW (matches real NOROT/ROTCCW/ROT180/
// ROTCW). flip: 0=none, 1=horizontal, 2=vertical, 3=both (matches real
// NOFLIP/FLIPH/FLIPV/FLIPVH). Ported bit-for-bit from real
// Display::drawBitmap(x,y,bitmap,rotation,flip) - including that
// function's own real quirks, confirmed directly in the real source: flip
// is applied using the bitmap's ORIGINAL (pre-rotation) width/height
// rather than the rotated shape's own effective dimensions, and vertical
// flip mirrors via `h - l` (not `h - l - 1`, asymmetric with the
// horizontal case's own `w - k - 1`) - preserved exactly so a game relying
// on either behaves identically to real hardware, not "fixed" into
// different, never-actually-shipped behavior.
void gbDrawBitmapRotated( int x, int y, int* bitmap, int rotation, int flip );

// Text - real Gamebuino bitmap fonts (see this file's own header comment
// and gamebuinoShim.c's own Font tables section). gbFontWidth/gbFontHeight
// are the real per-glyph cell size (raw glyph size + 1, real inter-char/
// line spacing baked in exactly like real Display::setFont()) - read-only,
// kept up to date by gbSetFont(). gbFontSize is the real integer size
// multiplier (1 = native, 2 = each glyph pixel doubled - the two sizes
// Gamebuino Classic games actually use in practice).
extern int gbCursorX, gbCursorY, gbFontSize, gbFontWidth, gbFontHeight;
extern int gbFont5x7[ 642 ]; // real hardware's own larger font - {5,7} raw cell, ASCII 0-127
extern int gbFont3x5[ 386 ]; // real hardware's own default font - {3,5} raw cell, ASCII 0-127
extern int gbFont3x3[ 386 ]; // real hardware's own smallest font - {3,3} raw cell, ASCII 0-127
void gbSetFont( int* font ); // font: one of gbFont5x7/gbFont3x5/gbFont3x3 above

// Vircon32 strings are `int[]` (one word per character) - this port's own
// games instead use real, plain `char*`/`char[N]` C strings throughout, the
// same dialect-conversion fix the sibling Tinyjoypad_SDL project already
// applied project-wide (see that project's own CLAUDE.md "Dialect
// conversion" section item 3).
void gbPrintString( char* text );
void gbPrintNumber( int value );

// Direct port of real Arduino Print::printFloat() - real hardware's own
// default float-printing behavior whenever a game calls
// `gb.display.print()`/`println()` on a real `float` value directly, not
// through an explicit `String`/formatting call. Real algorithm: round by
// adding half a unit in the last requested decimal place, print the (now-
// rounded) integer part, then extract each decimal digit from the
// remainder in turn. `decimals` matches what a game's own real call site
// passed to `println(value, decimals)` - real Arduino's own implicit
// default when no explicit decimals argument is given (a plain
// `print(floatValue)`) is 2, matching this shim's own `gbPrintNumber()`
// convention of "the caller always states an explicit value".
void gbPrintFloat( float value, int decimals );

// Direct port of real Gamebuino::popup()/updatePopup() (Gamebuino.cpp) - a
// small auto-dismissing bordered text box that slides in from the bottom
// edge over its final ~12 ticks, drawn on top of everything else already
// drawn that frame. Real hardware calls updatePopup() itself automatically
// at the tail of every real Gamebuino::update() (right before the
// framebuffer is sent to the display) - this shim reproduces that by
// calling it automatically from gbRenderFrame() itself, so a game only
// ever needs to call `gbPopup(text, duration)` and nothing else, matching
// real hardware's own one-call contract exactly.
void gbPopup( char* text, int duration );

// Draws one real glyph (ASCII 0-127) at (x,y) directly, in the currently
// selected font/size/color - the primitive gbPrintString() itself calls
// per character. Real Display::drawChar(x,y,c,size) takes size as its own
// 4th parameter; this shim instead reads the global gbFontSize, matching
// every other size-aware primitive here (gbPrintString/gbPrintNumber
// included) - set it before calling if you need size 2. Useful directly
// (not just via gbPrintString()) for a single non-text glyph, e.g. a
// game's own icon/digit HUD elements drawn one character at a time rather
// than as a string.
void gbDrawChar( int ch, int x, int y );

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------
// pitch matches real Gamebuino's own MIDI-style note numbers (0 = C, 12 =
// one octave up, etc, A4=440Hz at pitch 45 relative to a C0 base - the same
// convention real Sound.cpp's own noteToFrequency table uses) so upstream
// playNote(pitch, duration, channel) call sites port with the pitch value
// unchanged. duration is in real Gamebuino "ticks" (1 tick = 1 real update()
// frame on real hardware) - converted to seconds against the shim's own
// configured frame rate.

void gbPlayNote( int pitch, int duration );
void gbPlayTick();
void gbPlayOK();
void gbPlayCancel();

// -----------------------------------------------------------------------------
//   Collision helpers - direct ports of Gamebuino::collide*()
// -----------------------------------------------------------------------------

bool gbCollidePointRect( int x1, int y1, int x2, int y2, int w, int h );
bool gbCollideRectRect( int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2 );

// Direct ports of real `Display::getBitmapPixel()`/`Gamebuino::
// collideBitmapBitmap()` (read from utility/Display.cpp/Gamebuino.cpp).
// `bitmap` uses this shim's own real bitmap format (bitmap[0]=width,
// bitmap[1]=height, then packed row bytes - the same format gbDrawBitmap()
// itself reads), so these two are genuine drop-in ports, not
// approximations: same bounding-rect-reject-first optimization as real
// hardware, same per-pixel AND-of-both-bitmaps overlap test.
bool gbGetBitmapPixel( int* bitmap, int x, int y );
bool gbCollideBitmapBitmap( int x1, int y1, int* b1, int x2, int y2, int* b2 );

// -----------------------------------------------------------------------------
//   Small Arduino-macro stand-ins upstream game code commonly relies on
// -----------------------------------------------------------------------------
// No ternary operator in the Vircon32 dialect this was ported from - real
// functions instead of a `(a>b?a:b)` macro, kept as-is (valid, unchanged
// standard C - no reason to "restore" a ternary here).

int gbMax( int a, int b );
int gbMin( int a, int b );

// Real Arduino `abs()` stand-in - used internally by gbDrawLine()'s own
// Bresenham implementation, and available directly to any game that needs
// a plain integer absolute value.
int gbAbsInt( int a );

#endif
