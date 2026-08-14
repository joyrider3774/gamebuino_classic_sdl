// -----------------------------------------------------------------------------
// The Playdate port. Structured after the same-shape sibling Tinyjoypad_SDL
// project's own src/playdate/main.c (single-file eventHandler()+update()-
// callback shape, matching the official SDK's own Examples/Hello World
// convention too) - NOT split into a separate "backend" + "main" pair the
// way src/sdl3/ and src/sdl2/ are, since Playdate has no CLI/window/event-
// queue surface for a second file to own. See that project's own file (and
// its own CLAUDE.md's "The Playdate port" section) for the full worked
// precedent this port's own design decisions below are adapted from.
//
// Reuses ../gameworld/ (all 99 games, both shims, menuGameList.c's own
// addGames() registration table) completely unmodified - same as every
// other port. Does NOT reuse gameworld/menu.c's own menu_update() or
// gamesMain.c's own gamesMain_dispatchFrame()/drawConfirmQuitDialog(), both
// of which assume a real 640x360 canvas and the BIOS font (biosFont.h) -
// Playdate's screen is a fixed 400x240 1-bit panel, an entirely different
// scale/color model that doesn't fit that design at all. This file's own
// menuUpdate() below is a from-scratch, Playdate-native replacement
// (Playdate's own system font via pd->graphics->drawText(), a paginated
// numbered list, real gameplay thumbnails) - it still walks the exact same
// menu.h Game table (menu_getGame()/gameCount, populated by the unmodified
// menuGameList.c's addGames()), just rendered and navigated differently.
// Same reasoning for the quit-confirmation dialog: this port has no
// equivalent UI at all, matching the sibling project's own precedent
// exactly - holding A+B+Up+Right together returns to the menu immediately,
// no YES/NO prompt (see update()'s own comment below).
//
// Explicitly out of scope, matching the sibling project's own identical
// descope: every CLI flag the SDL ports' own main.c parses (no shell to
// launch from), and the glow/CRT presentation effects (meaningless on a
// fixed 1-bit panel - though see the PIXEL GRID section below for the one
// presentation effect this port DOES keep).
//
// GB_GRAY content, unlike the SDL ports' own arbitrary-RGB conversion
// machinery, doesn't need one at all - this project's own drawing
// primitives already only ever produce pure black/white/a real GB_GRAY
// dither, and this port's own md_drawColumn()/md_drawColumnGray() draw
// every one of this shim's logical colors (BLACK, and GB_GRAY's own real,
// native `kColorGrey` checkerboard) through the real Playdate SDK's own
// LCDColor/LCDPattern mechanism directly (see md_drawColumn()'s own
// comment below for the full "any color can be a pattern" design and the
// real pd_api_gfx.h citations behind it) - genuinely no manual dither/
// conversion code of this port's own at all, just native GPU-tiled fills.
// -----------------------------------------------------------------------------

#include "pd_api.h"
#include <string.h>

#include "machineDependent.h"
#include "menu.h"
#include "menuGameList.h"
#include "eepromShim.h"
#include "gamebuinoShim.h" // gbGetFrameRate()/gbSetEngineFrameRate() - see the FRAME-RATE SYNC section below

static PlaydateAPI* pd;

// =============================================================================
//   VIDEO
// =============================================================================

// 84*4=336, 48*4=192 - the best clean-multiplier fit under the real 400x240
// panel (GAME_SCALE=3 would waste more border space; GAME_SCALE=5 would be
// 420x240, overflowing the panel's own real width outright). Centered with a
// comfortable 32px/24px border on every side - matches this file's own
// header comment in the original task brief, verified by eye once real
// gameplay was on screen (see this port's own verification screenshots).
//
// Manual per-draw-call multiply in md_drawColumn(), NOT
// pd->display->setScale()/setOffset() - same reasoning as the sibling
// Tinyjoypad_SDL project's own identical choice (see that project's own
// main.c header comment for the full "why not setScale()" investigation:
// its own legal values are limited to 1/2/4/8, and even a legal one doesn't
// transform draw-call coordinates the way SDL's logical-presentation
// feature does - it re-samples a small top-left region of the always-
// 400x240 framebuffer instead). Manual scaling has no such restriction, so
// it hits this port's own actual best fit (336x192) directly.
#define GAME_SCALE    4
#define GAME_ORIGIN_X 32 // (400 - 84*4) / 2
#define GAME_ORIGIN_Y 24 // (240 - 48*4) / 2

// Nothing to do here - unlike SDL (which needs a window/renderer/streaming
// texture set up before any draw call is legal), Playdate's graphics
// context always exists from process start, and this port never touches
// pd->display's own scale/offset state at all (see GAME_SCALE's own comment
// above), so there's no per-mode display transform to set up either.
void md_initVideo() {}

// Present only to satisfy machineDependent.h's own contract (gameworld's
// menu.c/gamesMain.c still reference this symbol even though this port's
// own dispatch loop below never calls into either of those files) - nothing
// to actually track, since GAME_SCALE's own fixed origin/scale never
// changes between menu and gameplay.
void md_setInGame( bool inGame ) { (void)inGame; }

// No quit-confirmation dialog on this port at all (see this file's own
// header comment - A+B+Up+Right held returns to the menu immediately, with
// no dialog to keep effect-free in the first place), so there's nothing for
// this signal to actually drive here - still needs a real definition to
// satisfy machineDependent.h's own contract (gamesMain.c's own
// drawConfirmQuitDialog(), compiled as part of this port's own shared
// ../gameworld build regardless of whether anything here calls it, still
// references this symbol).
void md_setDialogShowing( bool showing ) { (void)showing; }

// Same reasoning as md_setDialogShowing() above - no "-fps" flag on this
// port (no CLI at all), but gamesMain.c's own gamesMain_drawFpsOverlay()
// still references this symbol.
void md_setFpsOverlayShowing( bool showing, int width, int height ) { (void)showing; (void)width; (void)height; }

// No -g/.gbu-file direct launch on this port (no CLI/shell), so
// gamesMain_setLaunchedDirectly() is never called here - real Playdate
// hardware has no "quit the app" concept anyway (the physical Menu button/
// A+B+Up+Right chord both return to THIS port's own menu, they don't exit)
// - a genuine no-op, not a stand-in for some equivalent this port doesn't
// have yet. Still needs a real definition - gamesMain.c references it.
void md_requestQuit() {}

// WHITE background / BLACK "on" bit - matches real Gamebuino Classic
// hardware's own genuine reflective PCD8544 LCD exactly (a "set" pixel is
// dark ink on a light background, not a lit pixel on a dark screen),
// cross-checked directly against src/sdl3/sdlBackend.c's own
// md_beginFrame()/md_drawColumn() (gWhitePixel fill / gBlackPixel "on" bit)
// as the real ground truth to match - the OPPOSITE polarity choice from the
// sibling Tinyjoypad_SDL project's own Playdate port, which deliberately
// matches TinyJoypad's own self-illuminating OLED instead (kColorBlack
// background / kColorWhite "on" bit) - because THAT project's own source
// hardware has the opposite real polarity from this one. Clears the WHOLE
// physical panel (game content is only ever drawn in the GAME_ORIGIN_X/Y-
// offset region - everything outside that stays this background color,
// acting as the border), matching every SDL port's own md_beginFrame()
// clearing its whole persistent canvas including its own letterbox border.
void md_beginFrame()
{
    pd->graphics->clear( kColorWhite );
}

// A genuinely different clear than md_beginFrame() above - never actually
// called by this port (menu.c's own menu_update(), the only real caller
// anywhere in the game world, is never invoked here - see this file's own
// header comment for why this port has its own from-scratch menu instead),
// but still needs a real definition since menu.c (compiled as part of this
// port's own shared ../gameworld build) references it.
void md_beginMenuFrame()
{
    pd->graphics->clear( kColorBlack );
}

// A real, native Playdate LCDPattern for GB_GRAY content - confirmed
// directly against the real installed SDK (pd_api_gfx.h, SDK 2.7.2):
// `typedef uint8_t LCDPattern[16]; // 8x8 pattern: 8 rows image data, 8
// rows mask`. The 8 bitmap rows alternate 0b10101010/0b01010101 - the same
// checkerboard bit pattern this shim's own GB_GRAY dither already uses
// everywhere else (gbDrawPixel()'s own spatial `(x&1)^(y&1)` component,
// gamebuinoShim.c) - with a fully-opaque 0xFF mask on every row so all 64
// pixels of the tile actually participate (a mask bit of 0 would instead
// leave that pixel transparent, showing whatever was drawn underneath).
// Used by casting it directly to LCDColor at each draw call site
// (`(LCDColor)kColorGrey` below - the array decays to a pointer to its own
// first byte, matching LCDColor's own documented "either an LCDSolidColor,
// or a pointer to LCDPattern" contract exactly - pd_api_gfx.h's own
// `LCDOpaquePattern` convenience macro builds the identical 16-byte shape
// from 8 arguments, not used here since this pattern's own full 16 bytes
// are simple/explicit enough to just write out directly). No separate
// pattern-registration call exists or is needed in the C API (unlike the
// Lua-only `gfx.setPattern()`) - any function taking an LCDColor parameter
// (fillRect, drawLine, setColor, ...) accepts a cast pattern pointer
// exactly like a solid color.
//
// The GPU tiles a pattern-valued LCDColor by real, absolute SCREEN pixel
// coordinates (row/col mod 8), not by each individual draw call's own
// local (x,y) origin - confirmed by there being no per-call phase
// parameter anywhere in the C fillRect()/drawLine()/etc signatures (the
// Lua-only `setPattern(pattern,[x,y])`'s own optional phase-shift argument
// only makes sense to offer at all if the default, no-argument case is
// itself anchored to one single fixed, shared origin) - so many small
// per-run fillRect() calls using this same pattern (see
// drawRunOfPixels()'s own doc comment below) tile seamlessly into one
// continuous checkerboard across the whole screen, with no visible seams
// at each individual 4px-wide/tall run's own edges.
static LCDPattern kColorGrey = {
    // Bitmap
    0b10101010,
    0b01010101,
    0b10101010,
    0b01010101,
    0b10101010,
    0b01010101,
    0b10101010,
    0b01010101,

    // Mask
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
};

// Shared by md_drawColumn()/md_drawColumnGray() below - "any of this
// shim's logical draw colors can be a pattern" is a real, general
// mechanism on this port, not something bolted on only for gray: this one
// helper scans `value`'s own 8 bits for consecutive runs (matching every
// other port's own already-proven "batch a run of set bits into one fill
// call instead of one call per individual pixel" performance discipline -
// this dialect's own flat per-call overhead concern doesn't apply to a
// native C build the way it does to the sibling Vircon32 project's own
// ISA, but batching still means fewer GPU draw calls per frame here too)
// and draws each run with whatever `color` the caller passes - a genuine
// LCDColor, which may be a plain LCDSolidColor (kColorBlack, for ordinary
// BLACK/WHITE content) or a cast pattern pointer (kColorGrey, for GB_GRAY
// content) - fillRect() itself neither knows nor cares which.
static void drawRunOfPixels( int col, int page, int value, LCDColor color )
{
    int x  = GAME_ORIGIN_X + col * GAME_SCALE;
    int y0 = GAME_ORIGIN_Y + page * 8 * GAME_SCALE;

    int i = 0;
    while( i < 8 )
    {
        if( !( ( value >> i ) & 1 ) )
        {
            i++;
            continue;
        }

        int runStart = i;
        while( i < 8 && ( ( value >> i ) & 1 ) )
          i++;
        int runLen = i - runStart;

        pd->graphics->fillRect( x, y0 + runStart * GAME_SCALE, GAME_SCALE, runLen * GAME_SCALE, color );
    }
}

void md_drawColumn( int col, int page, int value )
{
    // Same byte-truncation risk as every other port (avrCompat.h's
    // uint8_t/etc are plain `int`s here too) - mask once here, the single
    // choke point every game/shim's column value funnels through.
    value &= 0xFF;

    if( value == 0 )
      return;

    // kColorBlack for "on" bits (dark ink on this project's own real
    // reflective LCD) - see this function's own header-comment-adjacent
    // md_beginFrame() doc comment for the full polarity reasoning.
    drawRunOfPixels( col, page, value, kColorBlack );
}

// The real, targeted second pass gbRenderFrame() (gamebuinoShim.c) runs
// for GB_GRAY content once gbAnyGrayDrawn is true - which only ever
// happens with gbRealGrayColor also true (see gbDrawPixel()'s own GB_GRAY
// branch), which this port's own init() below sets at startup,
// unconditionally (matching SDL2/SDL3's own identical default - see
// gamesMain.c). Draws each of THIS pass's own "on" bits (col/page/value
// here is gbGrayBuffer's own content - exactly the subset of pixels the
// current frame drew as GB_GRAY specifically, not gbFrameBuffer's own
// already-drawn black/white content, which the FIRST pass, md_drawColumn()
// above, already painted moments earlier in the same real frame) with the
// real, native kColorGrey pattern instead of a plain solid block - a
// genuine, static (non-flickering) 1-bit checkerboard dither, GPU-tiled at
// the real device-pixel level (finer-grained than the coarse, one-
// logical-Gamebuino-pixel-at-a-time checkerboard gbDrawPixel()'s own
// fallback dither produces before any of this ever runs), overpainting
// whichever transient flicker-approximation pixels md_drawColumn() left
// there a moment ago with this port's own real "what does GB_GRAY actually
// look like on a genuine 1-bit panel" answer.
void md_drawColumnGray( int col, int page, int value )
{
    value &= 0xFF;

    if( value == 0 )
      return;

    drawRunOfPixels( col, page, value, (LCDColor)kColorGrey );
}

// Both unused on this port (no BIOS-font menu, no quit-confirmation dialog
// - see this file's own header comment) - kept as real, callable no-op
// definitions purely to satisfy machineDependent.h's own contract
// (biosFont.h's md_drawColumnPixels()/md_drawColumnPixelsColor() calls and
// gamesMain.c's drawConfirmQuitDialog()'s md_drawSolidRect() calls, all
// compiled as part of this port's own shared ../gameworld build, still need
// *something* to link against even though this port's own dispatch loop
// below never invokes gamesMain.c's dispatch function that would call any
// of them).
void md_drawSolidRect( int x, int y, int w, int h, int color )
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
}

void md_drawColumnPixels( int x, int y, int bits, int count )
{
    (void)x; (void)y; (void)bits; (void)count;
}

void md_drawColumnPixelsColor( int x, int y, int bits, int count, int color )
{
    (void)x; (void)y; (void)bits; (void)count; (void)color;
}

// No thumbnails via THIS symbol on this port (gameworld/menu.c's own
// md_getThumbnailCount()/md_drawGameThumbnail() contract - never called
// here, since this port has its own from-scratch menu with its own
// separate thumbnail loading/drawing below in the MENU section) - 0
// unconditionally, matching the documented "no thumbnail yet" contract.
int md_getThumbnailCount() { return 0; }

void md_drawGameThumbnail( int gameIndex, int x, int y )
{
    (void)gameIndex; (void)x; (void)y;
}

// =============================================================================
//   PIXEL GRID EFFECT - the one SDL-port presentation effect this port keeps
//   (see this file's own header comment for why glow/CRT aren't built here
//   at all - meaningless on a fixed 1-bit panel). A crisp on/off outline,
//   cheap and legible even at 1-bit, gated behind Playdate's own system
//   menu instead of a physical button (Button L has no Playdate hardware
//   equivalent - see the INPUT section below).
// =============================================================================

// A static "LCD pixel grid" overlay - a thin opaque black outline drawn
// around every source pixel's own boundary, so each of the real 84x48
// PCD8544 pixels reads as its own distinct visible cell once scaled up by
// GAME_SCALE, instead of blending into one smooth block. Pre-baked ONCE
// into an LCDBitmap at init() time, then just drawn unchanged every frame
// it's enabled, rather than re-drawing GAME_SCALE lines by hand each frame.
static LCDBitmap* gPixelGridBitmap  = NULL;
static bool       gPixelGridEnabled = false; // default OFF - see the system-menu checkmark item below

static void pixelGridEffectInit()
{
    // Sized to exactly the scaled game canvas (GAME_SCALE*84 x
    // GAME_SCALE*48 = 336x192), not the full 400x240 panel - there's
    // nothing to grid-line outside the actual game content area.
    // kColorClear background: only the grid lines themselves should be
    // opaque, so drawBitmap() below leaves every other pixel of the
    // already-drawn game content showing through untouched.
    gPixelGridBitmap = pd->graphics->newBitmap( GAME_SCALE * LCD_WIDTH, GAME_SCALE * LCD_HEIGHT, kColorClear );
    if( !gPixelGridBitmap )
      return;

    pd->graphics->pushContext( gPixelGridBitmap );

    for( int x = 0; x < GAME_SCALE * LCD_WIDTH; x += GAME_SCALE )
      pd->graphics->fillRect( x, 0, 1, GAME_SCALE * LCD_HEIGHT, kColorBlack );

    for( int y = 0; y < GAME_SCALE * LCD_HEIGHT; y += GAME_SCALE )
      pd->graphics->fillRect( 0, y, GAME_SCALE * LCD_WIDTH, 1, kColorBlack );

    pd->graphics->popContext();
}

// Called once per real frame during actual gameplay only (see update()
// below) - draws the pre-baked grid bitmap at the same GAME_ORIGIN_X/Y
// offset md_drawColumn() itself draws through, so it lines up exactly with
// the scaled game content underneath.
static void pixelGridEffectRender()
{
    if( !gPixelGridEnabled || !gPixelGridBitmap )
      return;

    pd->graphics->drawBitmap( gPixelGridBitmap, GAME_ORIGIN_X, GAME_ORIGIN_Y, kBitmapUnflipped );
}

// Playdate's own update-callback model presents automatically once
// update() (below) returns 1 - no manual "present"/vsync-wait call exists
// or is needed, unlike SDL's SDL_RenderPresent()/vsync-locked loop.
void md_endFrame()
{
#ifndef NDEBUG
    pd->system->drawFPS( 0, 0 );
#endif
}

// =============================================================================
//   INPUT
// =============================================================================

// Raw held-frame counters, updated once per real frame in update() below -
// same shape/contract as every other port's own copy (see src/sdl3/
// sdlBackend.c's identical helper for the full reasoning).
static int gLeftFrames  = 0;
static int gRightFrames = 0;
static int gUpFrames    = 0;
static int gDownFrames  = 0;
static int gARawFrames  = 0; // pre-gate, see md_inputAFrames() below
static bool gButB = false;   // level, not a counter - matches machineDependent.h's own md_inputB() contract

static void updateHeldCounter( int* counter, bool held )
{
    if( held )
      *counter = ( *counter > 0 ) ? ( *counter + 1 ) : 1;
    else
      *counter = ( *counter < 0 ) ? ( *counter - 1 ) : -1;
}

int md_inputLeftFrames()  { return gLeftFrames;  }
int md_inputRightFrames() { return gRightFrames; }
int md_inputUpFrames()    { return gUpFrames;    }
int md_inputDownFrames()  { return gDownFrames;  }

bool md_inputLeft()  { return md_inputLeftFrames()  > 0; }
bool md_inputRight() { return md_inputRightFrames() > 0; }
bool md_inputUp()    { return md_inputUpFrames()    > 0; }
bool md_inputDown()  { return md_inputDownFrames()  > 0; }

// Suppresses md_inputA() until the physical button is actually released -
// same contract/reasoning as every other port (ported from the sibling
// gamebuino_classic_vircon32 build's own portVircon32.c originally). Armed
// once when a game is (re)launched from the menu, so the same A press that
// confirmed the menu selection can't also be misread as the game's own
// first-frame input.
static bool gInputAGateActive = false;

int md_inputAFrames()
{
    if( gInputAGateActive )
    {
        if( gARawFrames <= 0 )
          gInputAGateActive = false;

        return -3600;
    }

    return gARawFrames;
}

bool md_inputA() { return md_inputAFrames() > 0; }

bool md_inputB() { return gButB; }

// Real Gamebuino Button C has no physical Playdate button - per direct
// requirement, exposed as a Playdate system-menu item instead (see
// buttonCMenuCallback() below), reporting a single momentary EDGE for
// exactly one read, not a held level: gCPending is set true once by the
// menu-item callback, then consumed (read AND cleared) the very next time
// this is called. gbUpdateButtons() (gamebuinoShim.c) is the only real
// caller anywhere in the game world - it samples every button's own level
// exactly once per real logic tick, so "true for exactly one read" here
// produces exactly the same one-tick gbBtnHeld[]==1 pulse a real momentary
// press would (matching md_armInputAGate()'s own identical "single edge,
// not held" contract, just consumed on read instead of gated on release).
static bool gCPending = false;

bool md_inputC()
{
    if( gCPending )
    {
        gCPending = false;
        return true;
    }
    return false;
}

// No Start-button concept on this port: there is no quit-confirmation
// dialog to trigger (see this file's own header comment and update()'s own
// A+B+Up+Right gesture) - gamesMain.c's own dispatch (the only real caller
// of md_inputStart() anywhere in the game world) isn't used by this port,
// but still needs a real definition to satisfy machineDependent.h's own
// contract.
bool md_inputStart() { return false; }

// Real-gray-color mode toggle (Button R) - per direct requirement, this
// port never wires ANY input to this at all (see gbRealGrayColor's own
// "never touched" contract, and md_drawColumnGray()'s own doc comment for
// why GB_GRAY content renders correctly here without it). gamesMain.c's own
// dispatch (never called by this port) is the only real reader of this
// symbol anywhere in the game world, but it still needs a real definition
// to link.
bool md_inputR() { return false; }

void md_armInputAGate()
{
    gInputAGateActive = true;
}

// =============================================================================
//   AUDIO
// =============================================================================

// Real Playdate hardware caps out at 50fps for full-screen refreshes (a
// hardware/panel limit, not a tunable setting) - requested via
// pd->display->setRefreshRate() in init() below.
#define PLAYDATE_REFRESH_RATE 50

// One synth, not a pool - real Gamebuino Classic hardware's own Sound
// engine defaults to NUM_CHANNELS=1 (see machineDependent.h's own
// md_playTone() doc comment), matching every other port's own single-voice
// choice.
static PDSynth* gSynth = NULL;
static int gFrameCounter = 0;

void md_initAudio()
{
    gSynth = pd->sound->synth->newSynth();
    // Square, not Playdate's own default sine - matches every other port's
    // own square-wave choice (see src/sdl3/sdlBackend.c's sdlAudioCallback()
    // for the full reasoning: matches real Gamebuino Classic hardware's own
    // genuine PWM-driven piezo speaker, a hard on/off toggle rather than a
    // smooth waveform). Playdate's synth engine supports this directly as a
    // built-in waveform - no manual oscillator/sample-buffer code needed at
    // all, unlike SDL's own raw-callback approach.
    pd->sound->synth->setWaveform( gSynth, kWaveformSquare );
}

void md_playTone( float freqHz, float durationSeconds )
{
    if( freqHz <= 0.0f )
    {
        // freq <= 0 is a deliberate silent "rest" (matches every other
        // port's own contract) - stop whatever's currently sounding, start
        // nothing new.
        pd->sound->synth->noteOff( gSynth, 0 );
        return;
    }

    // when=0 ("now", no scheduling) - Playdate's own synth handles the
    // note's automatic stop after `len` (durationSeconds) itself, unlike
    // every SDL port's own manual gFrameCounter/gToneStopFrame bookkeeping
    // in md_updateAudio() - genuinely no equivalent needed here at all,
    // since the SDK's own audio engine already tracks real elapsed time
    // sample-accurately. This sidesteps the whole class of problem
    // gameworld's own md_getFrameCounter() exists to help solve elsewhere -
    // confirmed directly (grep) that nothing in ../gameworld actually calls
    // md_getFrameCounter() at all, unlike the sibling Tinyjoypad_SDL
    // project's own obonoCoreShim.c, which reads it directly for note-
    // sequencer timing and needed a real fractional-accumulator fix for its
    // own 60-vs-50fps mismatch as a result - no such fix is needed here.
    pd->sound->synth->playNote( gSynth, freqHz, 0.5f, durationSeconds, 0 );
}

void md_stopTone()
{
    pd->sound->synth->noteOff( gSynth, 0 );
}

// md_getFrameCounter() is declared by machineDependent.h and referenced by
// nothing in ../gameworld (confirmed directly via a project-wide grep
// before writing this) - a plain per-real-callback increment is enough for
// interface completeness, with no fractional-rate compensation needed (see
// md_playTone()'s own comment above for why).
void md_updateAudio()
{
    gFrameCounter++;
}

int md_getFrameCounter() { return gFrameCounter; }

// =============================================================================
//   MEMORY CARD (backs eepromShim.h's persistent per-game EEPROM emulation)
// =============================================================================
// A real implementation - saves into this game's own private per-cartridge
// data folder via pd->file (kFileWrite/kFileReadData), the same real
// Playdate persistence mechanism the sibling Tinyjoypad_SDL project's own
// Playdate port already uses for its own high-score save. No home-directory
// concept applies here the way it does on the SDL ports - a plain relative
// filename with no leading slash is automatically written into this game's
// own sandboxed Data/<bundleid> folder.
//
// Playdate's own file API has no true random-access read-modify-write the
// way stdio's "r+b" mode gives the SDL ports (kFileWrite always creates
// fresh/truncates) - so instead of seeking within an open handle, every
// read/write here works against one whole in-memory copy of the file: read
// pulls the entire file in, write patches the in-memory copy at the given
// offset (growing/zero-filling it as needed) then writes the WHOLE thing
// back out in one kFileWrite pass. The file tops out around 132KB
// (eepromShim.c's own EEPROM_MAX_SLOTS=128 * ~1KB+40 bytes/slot) and this
// only ever runs once per game launch (a read) or on a genuine new high
// score (a write), never per-frame, so the extra whole-file I/O each time
// isn't a real cost.
#define CARD_FILE_NAME "highscores.dat"
#define CARD_SIGNATURE_TEXT "GAMEBUINOPLAYDATE01"
#define CARD_SIGNATURE_BYTES 32

// Reads the whole file into a pd->system->realloc()'d buffer - the file API
// has no direct handle-based "get size" call, so this just grows a buffer
// in chunks until read() stops returning data. *outBuf is NULL and *outLen
// is 0 (a legitimate "no file yet" result, not an error the caller needs to
// special-case) if the file doesn't exist.
static int cardReadWholeFile( unsigned char** outBuf, int* outLen )
{
    *outBuf = NULL;
    *outLen = 0;

    SDFile* fp = pd->file->open( CARD_FILE_NAME, kFileReadData );
    if( !fp )
      return 0;

    int cap = 4096;
    unsigned char* buf = pd->system->realloc( NULL, (size_t)cap );
    int len = 0;
    for( ;; )
    {
        if( len + 4096 > cap )
        {
            cap *= 2;
            buf = pd->system->realloc( buf, (size_t)cap );
        }
        int n = pd->file->read( fp, buf + len, 4096 );
        if( n <= 0 )
          break;
        len += n;
    }
    pd->file->close( fp );

    *outBuf = buf;
    *outLen = len;
    return 1;
}

bool md_cardIsConnected()
{
    // Always true - Playdate's own sandboxed per-game data folder always
    // exists and is always writable, unlike a real removable memory card.
    return true;
}

bool md_cardHasOurSignature()
{
    unsigned char* buf;
    int len;
    if( !cardReadWholeFile( &buf, &len ) )
      return false;

    bool matches = ( len >= (int)strlen( CARD_SIGNATURE_TEXT ) )
                 && ( memcmp( buf, CARD_SIGNATURE_TEXT, strlen( CARD_SIGNATURE_TEXT ) ) == 0 );

    pd->system->realloc( buf, 0 );
    return matches;
}

void md_cardWriteSignature()
{
    unsigned char* buf;
    int len;
    cardReadWholeFile( &buf, &len ); // buf==NULL/len==0 (fresh file) is fine

    if( len < CARD_SIGNATURE_BYTES )
    {
        unsigned char* grown = pd->system->realloc( buf, (size_t)CARD_SIGNATURE_BYTES );
        memset( grown + len, 0, (size_t)( CARD_SIGNATURE_BYTES - len ) );
        buf = grown;
        len = CARD_SIGNATURE_BYTES;
    }

    memset( buf, 0, CARD_SIGNATURE_BYTES );
    memcpy( buf, CARD_SIGNATURE_TEXT, strlen( CARD_SIGNATURE_TEXT ) );

    SDFile* fp = pd->file->open( CARD_FILE_NAME, kFileWrite );
    if( fp )
    {
        pd->file->write( fp, buf, (unsigned int)len );
        pd->file->close( fp );
    }

    pd->system->realloc( buf, 0 );
}

void md_cardReadData( void* dest, int offsetBytes, int sizeBytes )
{
    memset( dest, 0, (size_t)sizeBytes );

    unsigned char* buf;
    int len;
    if( !cardReadWholeFile( &buf, &len ) )
      return;

    int avail = len - offsetBytes;
    if( avail > 0 )
    {
        int copyLen = avail < sizeBytes ? avail : sizeBytes;
        memcpy( dest, buf + offsetBytes, (size_t)copyLen );
    }

    pd->system->realloc( buf, 0 );
}

void md_cardWriteData( void* src, int offsetBytes, int sizeBytes )
{
    unsigned char* buf;
    int len;
    cardReadWholeFile( &buf, &len ); // buf==NULL/len==0 (fresh file) is fine

    int neededLen = offsetBytes + sizeBytes;
    if( len < neededLen )
    {
        unsigned char* grown = pd->system->realloc( buf, (size_t)neededLen );
        memset( grown + len, 0, (size_t)( neededLen - len ) );
        buf = grown;
        len = neededLen;
    }

    memcpy( buf + offsetBytes, src, (size_t)sizeBytes );

    SDFile* fp = pd->file->open( CARD_FILE_NAME, kFileWrite );
    if( fp )
    {
        pd->file->write( fp, buf, (unsigned int)len );
        pd->file->close( fp );
    }

    pd->system->realloc( buf, 0 );
}

// =============================================================================
//   MENU - Playdate-native (see this file's own header comment for why
//   gameworld/menu.c's own menu_update() isn't used here)
// =============================================================================

static LCDFont* gMenuFont = NULL;
static int gMenuRowHeight = 16;
static int gMenuSelection = 0; // a DISPLAY position (0..gameCount-1, alphabetized) - see gDisplayOrder[] below
static bool gPrevMenuUp = false;
static bool gPrevMenuDown = false;
static bool gPrevMenuLeft = false;
static bool gPrevMenuRight = false;

// games[] itself (menu.h's own Game table, populated by menuGameList.c's
// unmodified addGames()) stays in registration order - that's also what
// thumbnails/thumb_NN.png (below) are keyed by, since they were generated
// in that same order (see src/playdate/tools/gen_thumbnails.py). Alphabetical
// sorting is purely a *display* concern, kept in this separate indirection
// array instead - gDisplayOrder[i] holds the registration index shown at
// display position i. Re-implements gameworld/menu.c's own identical
// displayOrder[]/menu_buildDisplayOrder() locally rather than exposing that
// header-private array through a new cross-port accessor.
#define MENU_MAX_GAMES 112
static int gDisplayOrder[ MENU_MAX_GAMES ];

static LCDBitmap* gThumbnails[ MENU_MAX_GAMES ];
static int gThumbnailCount = 0;

#define MENU_TITLE_Y      2
#define MENU_HINT_Y       16
#define MENU_PAGE_Y       28
#define MENU_LIST_TOP     42
#define MENU_LIST_BOTTOM  236
#define MENU_LEFT_MARGIN  12
#define MENU_THUMB_X      220
#define MENU_THUMB_W      168
#define MENU_THUMB_H      96
#define MENU_THUMB_AUTHOR_GAP 4

// General decimal-append helper (row-number prefix, page indicator) - this
// file is part of the "platform" side, which never includes avrCompat.h
// (avrCompat.h's own itoa() isn't available here), same reasoning as every
// other port's own from-scratch menu.
static void appendInt( char* buf, int n )
{
    char digits[ 12 ];
    int count = 0;
    if( n == 0 )
    {
        digits[ count++ ] = '0';
    }
    else
    {
        while( n > 0 )
        {
            digits[ count++ ] = '0' + ( n % 10 );
            n /= 10;
        }
    }

    int len = (int)strlen( buf );
    while( count > 0 )
      buf[ len++ ] = digits[ --count ];
    buf[ len ] = 0;
}

// "NN. " row-number prefix, always 2 digits (this project's own 99 games
// never need a 3rd) - zero-padded.
static void appendZeroPadded2( char* buf, int n )
{
    int len = (int)strlen( buf );
    buf[ len ]     = '0' + ( ( n / 10 ) % 10 );
    buf[ len + 1 ] = '0' + ( n % 10 );
    buf[ len + 2 ] = 0;
}

// Selection sort on gDisplayOrder (by title) - gameCount is a small handful
// of entries (99 today), so O(n^2) costs nothing measurable here. Direct
// re-implementation of gameworld/menu.c's own menu_buildDisplayOrder(),
// since that function's own displayOrder[] array is file-private.
static void menuBuildDisplayOrder()
{
    for( int i = 0; i < gameCount; i++ )
      gDisplayOrder[ i ] = i;

    for( int i = 0; i < gameCount - 1; i++ )
    {
        int best = i;
        for( int j = i + 1; j < gameCount; j++ )
          if( strcmp( menu_getGame( gDisplayOrder[ j ] )->title,
                      menu_getGame( gDisplayOrder[ best ] )->title ) < 0 )
            best = j;

        if( best != i )
        {
            int tmp = gDisplayOrder[ i ];
            gDisplayOrder[ i ] = gDisplayOrder[ best ];
            gDisplayOrder[ best ] = tmp;
        }
    }
}

// thumbnails/thumb_00.png .. thumb_98.png (src/playdate/Source/thumbnails/,
// generated by tools/gen_thumbnails.py from this project's own real,
// already-captured metadata/screenshots/*.bmp gameplay screenshots) -
// loaded once here, referenced by path with no extension (Playdate's own
// `pdc` build tool converts a Source/*.png into its own bundled bitmap
// format at build time). Probed sequentially, stopping at the first
// missing index - registration-order-keyed, matching every other port's
// own thumbnail convention, not display order.
static void menuLoadThumbnails()
{
    gThumbnailCount = 0;
    for( int i = 0; i < MENU_MAX_GAMES; i++ )
    {
        char* path = NULL;
        pd->system->formatString( &path, "thumbnails/thumb_%02d", i );
        const char* err = NULL;
        LCDBitmap* bmp = pd->graphics->loadBitmap( path, &err );
        pd->system->realloc( path, 0 ); // frees

        if( !bmp )
          break;

        gThumbnails[ i ] = bmp;
        gThumbnailCount = i + 1;
    }
}

static void menuInit()
{
    const char* err = NULL;
    // A compact built-in system font - every Playdate ships every
    // /System/Fonts/* font already, so this port needs no embedded font
    // asset of its own (matching the sibling Tinyjoypad_SDL project's own
    // identical choice, made for the identical reason).
    gMenuFont = pd->graphics->loadFont( "/System/Fonts/Roobert-10-Bold.pft", &err );
    if( gMenuFont )
      gMenuRowHeight = pd->graphics->getFontHeight( gMenuFont ) + 4;

    pd->graphics->setFont( gMenuFont );

    menuBuildDisplayOrder();
    menuLoadThumbnails();
}

static void menuDrawCentered( const char* text, int y )
{
    size_t len = strlen( text );
    int w = pd->graphics->getTextWidth( gMenuFont, text, len, kASCIIEncoding, 0 );
    pd->graphics->drawText( text, len, kASCIIEncoding, ( 400 - w ) / 2, y );
}

// Draws `text` at (x,y) with a filled black box behind it and the glyphs
// themselves inverted (white-on-black) - this port's own real, simple
// stand-in for gameworld/menu.c's own MD_COLOR_RED text tint (see menu.h's
// own Game.unfinished doc comment): Playdate has no true color to draw a
// "red" warning in, so a bold white-on-black highlight box serves the same
// "known incomplete" visual-flag purpose instead, kept deliberately simple
// per the original task brief.
static void menuDrawUnfinished( const char* text, int x, int y )
{
    size_t len = strlen( text );
    int w = pd->graphics->getTextWidth( gMenuFont, text, len, kASCIIEncoding, 0 );
    pd->graphics->fillRect( x - 2, y - 1, w + 4, gMenuRowHeight - 2, kColorBlack );
    pd->graphics->setDrawMode( kDrawModeInverted );
    pd->graphics->drawText( text, len, kASCIIEncoding, x, y );
    pd->graphics->setDrawMode( kDrawModeCopy );
}

// Runs one frame of the menu's own navigation+render (Up/Down move the
// selection, Left/Right jump a whole page, A launches it) and returns the
// chosen game's registration index (see menu.h's own note on
// menu_getGame()'s indexing) once A is pressed, or -1 otherwise.
static int menuUpdate()
{
    bool up    = md_inputUp();
    bool down  = md_inputDown();
    bool left  = md_inputLeft();
    bool right = md_inputRight();
    bool a     = md_inputA();

    int visibleRows = ( MENU_LIST_BOTTOM - MENU_LIST_TOP ) / gMenuRowHeight;
    if( visibleRows < 1 )
      visibleRows = 1;
    int totalPages = ( gameCount + visibleRows - 1 ) / visibleRows;

    if( down && !gPrevMenuDown )
    {
        gMenuSelection++;
        if( gMenuSelection >= gameCount )
          gMenuSelection = 0;
    }
    if( up && !gPrevMenuUp )
    {
        gMenuSelection--;
        if( gMenuSelection < 0 )
          gMenuSelection = gameCount - 1;
    }

    // LEFT/RIGHT jump a whole page at a time (wrapping past the last/first
    // page) - matches gameworld/menu.c's own identical LEFT/RIGHT handling.
    if( right && !gPrevMenuRight )
    {
        int currentPage = gMenuSelection / visibleRows;
        currentPage++;
        if( currentPage >= totalPages )
          currentPage = 0;
        gMenuSelection = currentPage * visibleRows;
        if( gMenuSelection >= gameCount )
          gMenuSelection = gameCount - 1;
    }
    if( left && !gPrevMenuLeft )
    {
        int currentPage = gMenuSelection / visibleRows;
        currentPage--;
        if( currentPage < 0 )
          currentPage = totalPages - 1;
        gMenuSelection = currentPage * visibleRows;
        if( gMenuSelection >= gameCount )
          gMenuSelection = gameCount - 1;
    }

    gPrevMenuUp = up;
    gPrevMenuDown = down;
    gPrevMenuLeft = left;
    gPrevMenuRight = right;

    pd->graphics->clear( kColorWhite );
    menuDrawCentered( "GAMEBUINO CLASSIC", MENU_TITLE_Y );
    menuDrawCentered( "UP/DOWN SELECT  LEFT/RIGHT PAGE  A PLAY", MENU_HINT_Y );

    int currentPage = gMenuSelection / visibleRows;

    if( totalPages > 1 )
    {
        char pageLabel[ 32 ];
        pageLabel[ 0 ] = 0;
        strcat( pageLabel, "PAGE " );
        appendInt( pageLabel, currentPage + 1 );
        strcat( pageLabel, "/" );
        appendInt( pageLabel, totalPages );
        menuDrawCentered( pageLabel, MENU_PAGE_Y );
    }

    int startIndex = currentPage * visibleRows;

    int y = MENU_LIST_TOP;
    for( int row = 0; row < visibleRows; row++ )
    {
        int pos = startIndex + row;
        if( pos >= gameCount )
          break;

        int gameIdx = gDisplayOrder[ pos ];
        Game* game = menu_getGame( gameIdx );

        char label[ 64 ];
        label[ 0 ] = 0;
        strcat( label, ( pos == gMenuSelection ) ? "> " : "  " );
        appendZeroPadded2( label, pos + 1 );
        strcat( label, ". " );
        strcat( label, game->title );

        if( game->unfinished )
          menuDrawUnfinished( label, MENU_LEFT_MARGIN, y );
        else
          pd->graphics->drawText( label, strlen( label ), kASCIIEncoding, MENU_LEFT_MARGIN, y );

        y += gMenuRowHeight;
    }

    // Real gameplay thumbnail + "BY <author>" (plus an optional second info
    // line - see menu.h's own Game.info doc comment) of the currently-
    // selected game, matching gameworld/menu.c's own identical feature -
    // switches immediately whenever the selection moves. Indexed through
    // gDisplayOrder[] like the row label above - the thumbnail set is keyed
    // by registration index, not by alphabetical display position. Centered
    // vertically (as a group with the author/info lines below it) within
    // the list area, matching gameworld/menu.c's own identical blockY
    // centering formula.
    int selectedGameIdx = gDisplayOrder[ gMenuSelection ];
    Game* selectedGame = menu_getGame( selectedGameIdx );

    if( selectedGameIdx < gThumbnailCount )
    {
        bool hasInfo = selectedGame->info != NULL;
        int lineCount = hasInfo ? 2 : 1;
        int blockHeight = MENU_THUMB_H + MENU_THUMB_AUTHOR_GAP + gMenuRowHeight * lineCount;
        int blockY = MENU_LIST_TOP + ( ( MENU_LIST_BOTTOM - MENU_LIST_TOP ) - blockHeight ) / 2;

        pd->graphics->drawBitmap( gThumbnails[ selectedGameIdx ], MENU_THUMB_X, blockY, kBitmapUnflipped );

        char authorText[ 48 ];
        strcpy( authorText, "BY " );
        strcat( authorText, selectedGame->author );
        int authorW = pd->graphics->getTextWidth( gMenuFont, authorText, strlen( authorText ), kASCIIEncoding, 0 );
        int authorX = MENU_THUMB_X + ( MENU_THUMB_W - authorW ) / 2;
        int authorY = blockY + MENU_THUMB_H + MENU_THUMB_AUTHOR_GAP;
        pd->graphics->drawText( authorText, strlen( authorText ), kASCIIEncoding, authorX, authorY );

        if( hasInfo )
        {
            int infoW = pd->graphics->getTextWidth( gMenuFont, selectedGame->info, strlen( selectedGame->info ), kASCIIEncoding, 0 );
            int infoX = MENU_THUMB_X + ( MENU_THUMB_W - infoW ) / 2;
            int infoY = authorY + gMenuRowHeight;

            if( selectedGame->unfinished )
              menuDrawUnfinished( selectedGame->info, infoX, infoY );
            else
              pd->graphics->drawText( selectedGame->info, strlen( selectedGame->info ), kASCIIEncoding, infoX, infoY );
        }
    }

    if( a )
      return selectedGameIdx;

    return -1;
}

// =============================================================================
//   TOP-LEVEL DISPATCH
// =============================================================================

static int gCurrentGameIndex = -1;

// Playdate's own system menu (opened by the player's own physical Menu
// button during gameplay, or the Simulator's own Esc key) holds up to 3
// custom entries - confirmed directly against the real SDK docs ("Inside
// Playdate with C.html": "...up to three menu item[s]..."), not assumed,
// and re-confirmed empirically in the Simulator (a 4th
// pd->system->addMenuItem() call was tried once, and the real SDK visibly
// caps the system menu's own custom section at 3 entries - the 4th simply
// never appears in the rendered menu, exactly matching the docs). Three real
// actions need exposing this way on this port - "Return to Menu" (see
// below), Button C (no physical Playdate button at all), and the pixel-grid
// toggle (Button L on every other port, which Playdate hardware doesn't
// have either) - fitting the cap exactly, with no item needing to be
// dropped.
//
// No global-mute item here: real Playdate hardware has its own system-level
// volume control (the headphone-jack volume, exposed as the native
// "Volume" entry already sitting in this same system menu, screenshot-
// confirmed sitting right below this port's own 3 custom items) - this
// port doesn't need to duplicate that with a second, redundant mute toggle
// of its own the way the SDL ports' own keyboard-bound mute keybind does
// (those ports have no such native system-level equivalent to defer to).
//
// "Menu" - the equivalent of what Start plus the quit-confirmation dialog
// does on the SDL ports, just reached through the system menu instead of a
// dedicated button (this port has no spare physical button for it, and per
// this file's own header comment, no YES/NO confirmation dialog either -
// it returns directly, no prompt, matching the existing A+B+Up+Right
// chord's own identical directness). Added SECOND, right after Button C
// (see addGameSystemMenuItems() below for the exact real order) - the
// chord itself is kept as a second, faster way to do the same thing
// without leaving the system menu open at all - both paths call this same
// returnToMenu(), so there's exactly one place that resets
// gCurrentGameIndex/tears down the menu items.
static PDMenuItem* gPixelGridMenuItem = NULL;

// =============================================================================
//   FRAME-RATE SYNC - keeps pd->display's own real refresh rate (which, per
//   the Playdate SDK, directly controls how often update() below is even
//   CALLED at all) and gbUpdate()'s own internal accumulator
//   (gbSetEngineFrameRate() - see gamebuinoShim.c's own doc comment) both
//   matched to whatever frame rate the currently-running game has actually
//   requested (gbGetFrameRate(), defaulting to the real Gamebuino 20fps
//   default via gbBegin() until/unless that game calls gbSetFrameRate()
//   itself, mid-game or otherwise).
//
//   This is a genuine architectural difference from the SDL2/SDL3 ports,
//   not a port of anything they do: those tick their own dispatch loop at a
//   fixed, reliable 60Hz (matching gamebuinoShim.c's own MD_FRAMES_PER_SECOND
//   default exactly, vsync/timer-locked) and let gbUpdate()'s own
//   accumulator sub-sample down to whatever rate a game wants - genuinely
//   correct there, since a desktop loop really can hit 60Hz reliably. Real
//   Playdate hardware physically cannot exceed 50 real full-screen
//   refreshes/second (PLAYDATE_REFRESH_RATE, above) - simply leaving the
//   accumulator's own reference rate at the shared default of 60 while this
//   port's own real callback rate is capped at 50 would silently run every
//   requested game frame rate 60/50=1.2x too SLOW (the callback itself
//   already can't keep up with 60 real ticks/sec, so the accumulator would
//   sub-sample an already-slow stream even further) - the exact class of
//   mismatch this shim's own gbEngineFrameRate indirection exists to let a
//   port correct for. Retargeting pd->display's own real refresh rate
//   directly to match a game's request (up to the real 50fps ceiling) AND
//   keeping the accumulator's own reference in lock-step via
//   gbSetEngineFrameRate() means a 20fps game's own update() genuinely
//   fires at a real 20Hz with zero sub-sampling loss - Playdate's own
//   hardware-level vsync timing does the pacing directly, instead of a
//   manual accumulator trying to approximate it.
// =============================================================================

// The frame rate this port has already synced pd->display's own refresh
// rate + gbEngineFrameRate to - starts at an impossible sentinel (0, no
// real game ever requests 0fps) so the very first sync after any game
// launch always performs a real sync at least once, never skipped by
// comparing against a stale leftover value from a previous session.
static int gSyncedEngineFps = 0;

static void syncEngineFrameRateToGame()
{
    int fps = gbGetFrameRate();
    if( fps > PLAYDATE_REFRESH_RATE ) fps = PLAYDATE_REFRESH_RATE; // real hardware ceiling
    if( fps < 1 ) fps = 1;

    if( fps == gSyncedEngineFps )
      return; // already matches - avoid a redundant SDK/shim call every single tick

    gSyncedEngineFps = fps;
    pd->display->setRefreshRate( (float)fps );
    gbSetEngineFrameRate( fps );
}

static void returnToMenu()
{
    md_stopTone();
    gCurrentGameIndex = -1;
    pd->system->removeAllMenuItems();
    gPixelGridMenuItem = NULL;

    // Back to a fixed, comfortably-responsive rate for the hand-rolled menu
    // itself (menuUpdate() never goes through gbUpdate() at all, so
    // gbEngineFrameRate doesn't matter here - only pd->display's own real
    // refresh rate, which controls how often update()/menuUpdate() itself
    // gets called, does). gSyncedEngineFps is deliberately NOT reset here -
    // the next game launched forces a real re-sync unconditionally anyway
    // (see update()'s own launch branch), so leaving it alone costs
    // nothing and avoids a redundant extra setRefreshRate() call on the
    // common case where the next game launched happens to want the same
    // rate as this one just did.
    pd->display->setRefreshRate( PLAYDATE_REFRESH_RATE );
}

// Wraps returnToMenu() to match PDMenuItemCallbackFunction's own
// (void (*)(void* userdata)) signature - returnToMenu() itself takes no
// arguments and is also called directly from update()'s own A+B+Up+Right
// chord handling below, so it can't take a userdata parameter of its own.
static void returnToMenuMenuCallback( void* userdata )
{
    (void)userdata;
    returnToMenu();
}

// Fires the instant the player selects "Button C" from the system menu -
// addMenuItem() (unlike addCheckmarkMenuItem()) invokes its callback
// immediately on selection and hides the system menu right away (per the
// SDK's own documented behavior), so gCPending is set here and consumed by
// md_inputC() on the very next real logic tick that samples it - see that
// function's own doc comment for the full "single edge, not held" reasoning.
static void buttonCMenuCallback( void* userdata )
{
    (void)userdata;
    gCPending = true;
}

// Fires when the player toggles the checkmark from Playdate's own system
// menu - reads the value the OS already flipped back out, rather than
// tracking a second, independently-toggled copy of it here that could
// drift out of sync with what the checkmark itself is showing. The NULL
// guard is load-bearing, not defensive boilerplate - matches the sibling
// Tinyjoypad_SDL project's own identical, crash-report-confirmed reasoning
// for its own pixel-grid checkmark callback (see that project's own
// main.c): toggling a checkmark while the system menu is open defers this
// callback until the menu actually closes, and selecting a plain
// addMenuItem() entry in the same session (there is none on this port
// besides "Button C", but the ordering risk is identical in kind) closes
// the menu and could otherwise run after returnToMenu() has already freed
// every PDMenuItem via removeAllMenuItems().
static void pixelGridMenuCallback( void* userdata )
{
    (void)userdata;
    if( gPixelGridMenuItem == NULL )
      return;
    gPixelGridEnabled = pd->system->getMenuItemValue( gPixelGridMenuItem ) != 0;

    // Same problem menu.h's own onResume hook exists to solve elsewhere - a
    // game that skips its own redraw on frames where nothing changed won't
    // naturally paint over the grid lines this toggle just added/removed.
    // Every Gamebuino game here redraws unconditionally every real logic
    // tick though (gbUpdate()'s own gbClear() + full recomposite - see
    // gamebuino_classic_vircon32/CLAUDE.md's own "onResume" precedent for
    // why no Gamebuino game here actually needs this hook in practice), so
    // this is a defensive no-op call for architectural parity with the
    // sibling project rather than something ever observed to matter.
    if( gCurrentGameIndex != -1 )
    {
        GameFunc onResume = menu_getGame( gCurrentGameIndex )->onResume;
        if( onResume != NULL )
          onResume();
    }
}

static void addGameSystemMenuItems()
{
    // Button C first, then "Menu", then the Pixel Grid checkmark - see
    // gPixelGridMenuItem's own doc comment above for why this port has
    // these 3 items at all.
    pd->system->addMenuItem( "Button C", buttonCMenuCallback, NULL );
    pd->system->addMenuItem( "Menu", returnToMenuMenuCallback, NULL );
    gPixelGridMenuItem = pd->system->addCheckmarkMenuItem(
        "Pixel Grid", gPixelGridEnabled ? 1 : 0, pixelGridMenuCallback, NULL );
}

static int update( void* userdata )
{
    (void)userdata;

    PDButtons current, pushed, released;
    (void)pushed; (void)released;
    pd->system->getButtonState( &current, &pushed, &released );

    updateHeldCounter( &gLeftFrames,  ( current & kButtonLeft )  != 0 );
    updateHeldCounter( &gRightFrames, ( current & kButtonRight ) != 0 );
    updateHeldCounter( &gUpFrames,    ( current & kButtonUp )    != 0 );
    updateHeldCounter( &gDownFrames,  ( current & kButtonDown )  != 0 );
    updateHeldCounter( &gARawFrames,  ( current & kButtonA )     != 0 );
    gButB = ( current & kButtonB ) != 0;

    md_setInGame( gCurrentGameIndex != -1 );

    if( gCurrentGameIndex == -1 )
    {
        int chosen = menuUpdate();
        if( chosen != -1 )
        {
            gCurrentGameIndex = chosen;
            md_armInputAGate();

            // Clear to white once, immediately on selection and before the
            // chosen game's own init() runs any of its own code - matches
            // every other port's own gamesMain.c identical fix: some games'
            // own init() doesn't necessarily draw a full frame right away,
            // and this port's own screen behaves exactly like real
            // Gamebuino VRAM does when nothing redraws it.
            md_beginFrame();

            // Resolves/loads this game's own persistent EEPROM slot (looked
            // up by title - see eepromShim.c) before init() runs, since a
            // game's own init() is what actually calls eeprom_read_byte()/
            // etc to load its saved high score.
            eepromSelectGame( menu_getGame( chosen )->title );

            // Reset the accumulator's own reference rate to the real
            // hardware ceiling BEFORE this game's own init() runs - every
            // game's own init() begins with gbBegin() (which always resets
            // gbFrameRateFps to the real 20fps default) and MAY immediately
            // call gbSetFrameRate() itself right after, whose own clamp
            // (gamebuinoShim.c) measures against gbEngineFrameRate's own
            // CURRENT value - if that were still left over from a
            // DIFFERENT, previously-launched game's own already-synced
            // rate (e.g. a prior 20fps game), a new game requesting
            // something faster would be wrongly clamped down to that
            // stale leftover value instead of the real 50fps ceiling.
            // gSyncedEngineFps is invalidated too, so the real sync call
            // below always runs for real even if this game's own final
            // requested rate happens to numerically match whatever the
            // previous game had already synced to.
            gbSetEngineFrameRate( PLAYDATE_REFRESH_RATE );
            gSyncedEngineFps = -1;

            menu_getGame( chosen )->init();
            addGameSystemMenuItems();

            // Now that init() has fully settled this game's own real
            // requested frame rate, match this port's own actual display
            // refresh rate (and gbUpdate()'s own accumulator reference) to
            // it for real - see syncEngineFrameRateToGame()'s own doc
            // comment above.
            syncEngineFrameRateToGame();
        }
    }
    else
    {
        // No quit-CONFIRMATION dialog on this port (see this file's own
        // header comment) - A+B+Up+Right held together returns to the menu
        // immediately, matching the sibling Tinyjoypad_SDL project's own
        // identical chorded gesture - an unlikely-to-happen-by-accident
        // combo none of these 99 games' own real controls use
        // simultaneously. Checked before this frame's game update() runs
        // (not after), so the exit frame doesn't render one extra frame of
        // gameplay first. Same destination as the system menu's own
        // "Menu" item (see gPixelGridMenuItem's own doc comment above for
        // the system menu's own full real 3-item contents/ordering) -
        // both paths call this same returnToMenu().
        if( ( current & kButtonA ) && ( current & kButtonB ) &&
            ( current & kButtonUp ) && ( current & kButtonRight ) )
        {
            returnToMenu();
        }
        else
        {
            // Cheap (a single int comparison unless something actually
            // changed) - catches a game calling gbSetFrameRate() mid-game,
            // not just at launch. See this function's own doc comment
            // above for the full "why" this needs to happen at all.
            syncEngineFrameRateToGame();
            menu_getGame( gCurrentGameIndex )->update();
            pixelGridEffectRender();
        }
    }

    md_updateAudio();
    md_endFrame();

    return 1;
}

static void init()
{
    // Real Playdate hardware caps out at 50fps for full-screen refreshes (a
    // hardware/panel limit, not a tunable setting).
    pd->display->setRefreshRate( PLAYDATE_REFRESH_RATE );

    // Always on for this port, matching SDL2/SDL3's own identical default
    // (set in their own shared gamesMain.c, which this port never calls
    // into, hence setting it here directly instead) - GB_GRAY content now
    // routes through md_drawColumnGray()'s own real, native kColorGrey
    // pattern fill (see that function's own doc comment above) rather than
    // the plain per-pixel flicker-dither fallback every drawing primitive
    // still computes into gbFrameBuffer[] regardless (gbDrawPixel()'s own
    // GB_GRAY branch always sets/clears the transient "on" bit the same
    // way whether gbRealGrayColor is true or not - this flag only decides
    // whether gbGrayBuffer[]/gbAnyGrayDrawn additionally track which of
    // those bits are GB_GRAY specifically, enabling gbRenderFrame()'s own
    // second pass at all). Not reset per-game by gbBegin() (a genuine
    // session-wide setting, exactly like every other port's own identical
    // default), so this needs setting only once, here.
    gbRealGrayColor = true;

    md_initVideo();
    md_initAudio();
    pixelGridEffectInit();

    addGames();
    menuInit();

    pd->system->setUpdateCallback( update, NULL );
}

#ifdef _WINDLL
__declspec( dllexport )
#endif
int eventHandler( PlaydateAPI* _pd, PDSystemEvent event, uint32_t arg )
{
    (void)arg;

    if( event == kEventInit )
    {
        pd = _pd;
        init();
    }

    return 0;
}
