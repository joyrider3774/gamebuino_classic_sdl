// -----------------------------------------------------------------------------
// The "SDL platform backend" - implements every machineDependent.h function
// plus the sdlBackend.h platform-only extras main.c needs. Freely includes
// SDL.h (unlike the "game world" TU - see machineDependent.h's own comment
// for why the two sides are kept apart).
//
// This is the SDL2 port, ported from src/sdl3/sdlBackend.c (see that file's
// own comments for the full design rationale behind the video/input/audio
// choices below - only genuinely SDL2-vs-SDL3 API differences are called
// out here; everything else - the persistent gScreen canvas, the WHITE
// game / BLACK menu background polarity, the real gray-tint second render
// pass, the pixel-grid overlay drawn as real alpha-blended lines, the
// 16-voice square-wave audio mixer, the file-backed memory card, and the
// glow/CRT presentation-effect cycle (glowEffect.h/crtEffect.h, using the
// sibling Tinyjoypad_SDL project's own real SDL2 versions of both modules,
// not a hand-translation of the SDL3 ones) - carries over unchanged).
// -----------------------------------------------------------------------------

#include <SDL.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "machineDependent.h"
#include "sdlBackend.h"
#include "CInput.h"
#include "gamebuinoSDL2.h"
#include "glowEffect.h"
#include "crtEffect.h"

// =============================================================================
//   Shared platform state
// =============================================================================

static SDL_Window*   gWindow   = NULL;
static SDL_Renderer* gRenderer = NULL;
static CInput*       gInput    = NULL;
static bool          gQuit     = false;

static void sdlLog( const char* fmt, ... )
{
    char buf[ 1024 ];
    va_list args;
    va_start( args, fmt );
    SDL_vsnprintf( buf, sizeof( buf ), fmt, args );
    va_end( args );
    SDL_Log( "%s", buf );
}

// =============================================================================
//   VIDEO
// =============================================================================

// One persistent canvas at the real Vircon32 screen resolution (640x360,
// matching the sibling gamebuino_classic_vircon32 build's own real screen
// exactly) - NOT a separate small 84x48 "LCD" surface scaled up at present
// time. Same two real reasons as src/sdl3/sdlBackend.c's own identical
// design (see that file's own header comment in full): a single shared
// coordinate space lets md_drawSolidRect()/biosFont.h's menu text/game
// columns all draw directly at final screen coordinates with no separate
// scale-up step, and skipping a redraw (the quit dialog's own "game
// update() not called this frame" behavior) needs a truly persistent
// surface, not a renderer backbuffer that isn't guaranteed to retain its
// contents across presents.
//
// 84*7=588, 48*7=336 - centered in the 640x360 canvas with a 26px bar
// left/right and a 12px bar top/bottom, matching the sibling Vircon32
// build's own real TILE_SCALE/ORIGIN_X/ORIGIN_Y layout exactly.
#define SCREEN_LOGICAL_W MD_SCREEN_WIDTH
#define SCREEN_LOGICAL_H MD_SCREEN_HEIGHT
#define GAME_SCALE    7
#define GAME_ORIGIN_X 26
#define GAME_ORIGIN_Y 12

static SDL_Surface* gScreen        = NULL; // SCREEN_LOGICAL_W x H, RGBA32 - persistent
static SDL_Texture*  gScreenTexture = NULL; // streamed from gScreen every frame
static Uint32         gWhitePixel    = 0;
static Uint32         gBlackPixel    = 0;
static Uint32         gGrayPixel     = 0;
static Uint32         gRedPixel      = 0;

// Pixel-grid overlay - drawn directly as real alpha-blended grid lines
// onto the renderer's own backbuffer every frame, matching the sibling
// gamebuino_classic_vircon32 build's own identical "only while a game is
// actually running" gate (md_setInGame()). No longer has its own separate
// toggle button - see gEffectState's own declaration comment below.
static bool gPixelGridEnabled = false;
static bool gInGame           = false;

// Glow/CRT presentation effects - see src/sdl3/sdlBackend.c's own
// declaration comment for the full "one button, a real 3-bit counter,
// ALL 8 combinations of pixel-grid/glow/CRT" reasoning, unchanged here.
static int         gEffectState = 0; // bit0=pixel-grid, bit1=glow, bit2=CRT
static GlowEffect* gGlowEffect  = NULL;
static bool        gGlowEnabled = false;
static CrtEffect*  gCrtEffect   = NULL;
static bool        gCrtEnabled  = false;
static Uint32       gLastFrameTicks = 0; // for CrtEffect_Update()'s own real-time deltaTime - SDL2's SDL_GetTicks() returns Uint32, not SDL3's Uint64

// Set by md_setDialogShowing()/md_setFpsOverlayShowing() (gamesMain.c, once
// per real frame) - see machineDependent.h's own doc comments on both.
// md_endFrame() re-composites these two rects crisply on top of the
// pixel-grid overlay, the same "restore the clean pixels from the texture
// that already has them" trick src/sdl3's own md_endFrame() uses for the
// same reason.
static bool gDialogShowing = false;
static bool gFpsOverlayShowing = false;
static int  gFpsOverlayW = 0;
static int  gFpsOverlayH = 0;

void md_setDialogShowing( bool showing )
{
    gDialogShowing = showing;
}

void md_setFpsOverlayShowing( bool showing, int width, int height )
{
    gFpsOverlayShowing = showing;
    gFpsOverlayW = width;
    gFpsOverlayH = height;
}

// Reuses the same gQuit flag sdlBackend_pollEvents() sets on a real
// window-close/ButQuit event - sdlBackend_shouldQuit() (main.c's own loop
// condition) can't tell the two causes apart and doesn't need to.
void md_requestQuit()
{
    gQuit = true;
}

void md_initVideo()
{
    // SDL_CreateRGBSurfaceWithFormat, not SDL3's SDL_CreateSurface(w,h,fmt)
    // - SDL2's own legacy long-form surface constructor (the leading 0 is
    // an unused "depth override" left over from an even older API, ignored
    // whenever a real pixel format is also given).
    gScreen = SDL_CreateRGBSurfaceWithFormat( 0, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H, 32, SDL_PIXELFORMAT_RGBA32 );
    gScreenTexture = SDL_CreateTexture( gRenderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H );

    // SDL_ScaleModeNearest (mixed-case, no underscore) - SDL2's own enum
    // spelling, vs SDL3's SDL_SCALEMODE_NEAREST. SDL_SetTextureScaleMode()
    // itself has the identical name/signature in both.
    if( gScreenTexture )
      SDL_SetTextureScaleMode( gScreenTexture, SDL_ScaleModeNearest );

    // SDL_RenderSetLogicalSize(), not SDL3's SDL_SetRenderLogicalPresentation()
    // - SDL2 has no presentation-mode parameter (letterbox/stretch/overscan/
    // integer-scale), just this one call, which already produces the same
    // letterboxed, aspect-preserving fit this project needs by default.
    SDL_RenderSetLogicalSize( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H );

    if( gScreen )
    {
        // SDL_MapRGBA(surface->format, ...), not SDL3's SDL_MapSurfaceRGBA(surface, ...)
        // - SDL2's own legacy form maps against the format struct directly.
        gWhitePixel = SDL_MapRGBA( gScreen->format, 255, 255, 255, 255 );
        gBlackPixel = SDL_MapRGBA( gScreen->format, 0, 0, 0, 255 );
        gGrayPixel  = SDL_MapRGBA( gScreen->format, 128, 128, 128, 255 );
        gRedPixel   = SDL_MapRGBA( gScreen->format, 220, 40, 40, 255 );
    }

    // Same real constants src/sdl3/sdlBackend.c's own md_initVideo() uses.
    gGlowEffect = GlowEffect_Create( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H, 8 );
    gCrtEffect = CrtEffect_Create( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H,
        6, 2, 10.0f, 128, 128, 128, 45 );
    gLastFrameTicks = SDL_GetTicks();
}

void md_setInGame( bool inGame )
{
    gInGame = inGame;
}

// WHITE - matches real Gamebuino hardware's own reflective LCD (see this
// function's own doc comment in machineDependent.h).
void md_beginFrame()
{
    if( !gScreen )
      return;

    // SDL_FillRect, not SDL3's SDL_FillSurfaceRect - same signature/
    // semantics, SDL2's own original (never-renamed) name.
    SDL_FillRect( gScreen, NULL, gWhitePixel );
}

// BLACK - the opposite polarity, for the menu's own white-on-black BIOS
// text (see this function's own doc comment in machineDependent.h).
void md_beginMenuFrame()
{
    if( !gScreen )
      return;

    SDL_FillRect( gScreen, NULL, gBlackPixel );
}

// Shared by md_drawColumn()/md_drawColumnGray() below - both draw the same
// bit-run rectangles, just in a different fill color.
static void drawColumnRuns( int screenX, int screenY0, int value, Uint32 pixel )
{
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

        SDL_Rect r = { screenX, screenY0 + runStart * GAME_SCALE, GAME_SCALE, runLen * GAME_SCALE };
        SDL_FillRect( gScreen, &r, pixel );
    }
}

void md_drawColumn( int col, int page, int value )
{
    // avrCompat.h's uint8_t/etc are plain `int`s in this project too (see
    // that file's own header comment), so upstream shift/OR sprite-
    // compositing code that used to overflow harmlessly out of a real AVR
    // byte can leave stray high bits set - mask once here, the single
    // choke point every game/shim's column value funnels through.
    value &= 0xFF;

    if( value == 0 || !gScreen )
      return;

    int screenX = GAME_ORIGIN_X + col * GAME_SCALE;
    int screenY0 = GAME_ORIGIN_Y + page * 8 * GAME_SCALE;

    // A "set" bit is dark ink on this project's own real reflective LCD
    // (see md_beginFrame()'s own doc comment) - BLACK, not WHITE, unlike
    // the sibling Tinyjoypad_SDL project's own self-illuminating-OLED
    // equivalent.
    drawColumnRuns( screenX, screenY0, value, gBlackPixel );
}

// Second, targeted pass, only ever called by gbRenderFrame() when the
// runtime gbRealGrayColor toggle is on and this cell actually has real
// gray content (see machineDependent.h's own doc comment on this function,
// and gamebuinoShim.c's own gbGrayBuffer/gbAnyGrayDrawn doc comments) -
// paints a real, solid mid-gray directly over exactly those pixels,
// replacing whatever black/white the checkerboard-dither first pass left
// there. Unlike the sibling Vircon32 build (which needs a whole second
// pre-baked texture atlas for this, since its GPU has no per-pixel drawing
// at all), this is just the same bit-run rectangle fill as md_drawColumn()
// above, with a different color.
void md_drawColumnGray( int col, int page, int value )
{
    value &= 0xFF;

    if( value == 0 || !gScreen )
      return;

    int screenX = GAME_ORIGIN_X + col * GAME_SCALE;
    int screenY0 = GAME_ORIGIN_Y + page * 8 * GAME_SCALE;

    drawColumnRuns( screenX, screenY0, value, gGrayPixel );
}

void md_drawSolidRect( int x, int y, int w, int h, int color )
{
    if( !gScreen )
      return;

    Uint32 pixel = ( color == MD_COLOR_WHITE ) ? gWhitePixel : gBlackPixel;
    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect( gScreen, &rect, pixel );
}

static void drawColumnPixelRuns( int x, int y, int bits, int count, Uint32 pixel )
{
    if( !gScreen )
      return;

    int i = 0;
    while( i < count )
    {
        if( !( ( bits >> i ) & 1 ) )
        {
            i++;
            continue;
        }

        int runStart = i;
        while( i < count && ( ( bits >> i ) & 1 ) )
          i++;
        int runLen = i - runStart;

        SDL_Rect r = { x, y + runStart, 1, runLen };
        SDL_FillRect( gScreen, &r, pixel );
    }
}

void md_drawColumnPixels( int x, int y, int bits, int count )
{
    drawColumnPixelRuns( x, y, bits, count, gWhitePixel );
}

void md_drawColumnPixelsColor( int x, int y, int bits, int count, int color )
{
    Uint32 pixel = gWhitePixel;
    if( color == MD_COLOR_BLACK ) pixel = gBlackPixel;
    else if( color == MD_COLOR_RED ) pixel = gRedPixel;
    drawColumnPixelRuns( x, y, bits, count, pixel );
}

// Real alpha-blended grid lines at every GAME_SCALE-pixel boundary across
// the real 84x48 LCD area, matching the sibling gamebuino_classic_vircon32
// build's own pixel-grid overlay in spirit (same "one line per tile
// boundary" layout) but drawn directly rather than via a pre-baked texture
// asset - SDL has no texture-size ceiling to design a baked-atlas
// workaround around the way that build's own real 1024x1024 GPU limit
// forced (see that project's own CLAUDE.md).
static void drawPixelGridOverlay()
{
    SDL_SetRenderDrawBlendMode( gRenderer, SDL_BLENDMODE_BLEND );
    SDL_SetRenderDrawColor( gRenderer, 0, 0, 0, 60 );

    int left = GAME_ORIGIN_X;
    int top = GAME_ORIGIN_Y;
    int right = GAME_ORIGIN_X + LCD_WIDTH * GAME_SCALE;
    int bottom = GAME_ORIGIN_Y + LCD_HEIGHT * GAME_SCALE;

    // SDL_RenderDrawLine (plain int coordinates), not SDL3's SDL_RenderLine
    // (which takes float coordinates) - every coordinate here is already a
    // whole pixel position, so no float cast is needed at all.
    for( int col = 0; col <= LCD_WIDTH; col++ )
    {
        int x = GAME_ORIGIN_X + col * GAME_SCALE;
        SDL_RenderDrawLine( gRenderer, x, top, x, bottom );
    }
    for( int row = 0; row <= LCD_HEIGHT; row++ )
    {
        int y = GAME_ORIGIN_Y + row * GAME_SCALE;
        SDL_RenderDrawLine( gRenderer, left, y, right, y );
    }

    SDL_SetRenderDrawBlendMode( gRenderer, SDL_BLENDMODE_NONE );
}

void md_endFrame()
{
    if( !gScreen )
      return;

    // gScreen is persistent (see this section's own header comment) - even
    // on a frame where nothing actually redrew it (neither md_beginFrame()
    // nor md_beginMenuFrame() was called this frame - the quit-dialog's own
    // overlay-only frames), re-uploading+re-presenting its unchanged pixels
    // is correct, just a plain repeat of the last real frame's content.
    if( gScreenTexture )
    {
        SDL_UpdateTexture( gScreenTexture, NULL, gScreen->pixels, gScreen->pitch );

        SDL_SetRenderDrawColor( gRenderer, 0, 0, 0, 255 );
        SDL_RenderClear( gRenderer );
        // SDL_RenderCopy, not SDL3's SDL_RenderTexture - same signature
        // (renderer, texture, srcrect, dstrect), just SDL2's own original
        // (never-renamed) name.
        SDL_RenderCopy( gRenderer, gScreenTexture, NULL, NULL );

        // Only while actual gameplay is running (matching the sibling
        // Vircon32 build's own `currentGameIndex != -1` gate) - never on
        // the menu, and not drawn while the quit-confirmation dialog's own
        // box is up either (gInGame stays true for the dialog's whole
        // duration too, so the frozen game view behind/around it keeps
        // showing the grid - only the box itself is re-composited crisp
        // below).
        // Glow/CRT - see src/sdl3/sdlBackend.c's own identical comment for
        // the full reasoning (draw order, why neither can accumulate frame
        // over frame).
        Uint32 nowTicks = SDL_GetTicks();
        float deltaTime = (float)( nowTicks - gLastFrameTicks ) / 1000.0f;
        gLastFrameTicks = nowTicks;

        if( gInGame && gGlowEnabled )
          GlowEffect_Render( gRenderer, gScreen, gGlowEffect, 255, 255, 255, 140 );

        if( gInGame && gPixelGridEnabled )
          drawPixelGridOverlay();

        if( gInGame && gCrtEnabled )
        {
            CrtEffect_Update( gCrtEffect, deltaTime );
            CrtEffect_Render( gRenderer, gCrtEffect );
        }

        // Re-composite the dialog box's own rect crisply, ON TOP of the
        // grid overlay - gScreenTexture already holds its clean pixels
        // (drawConfirmQuitDialog() draws directly onto the same gScreen
        // every other draw call does), so this is just a second,
        // identically-sized/positioned blit of that one sub-rect.
        //
        // Plain SDL_Rect (int fields), not SDL3's SDL_FRect (float) -
        // SDL2's SDL_RenderCopy() takes int rects; no precision loss since
        // every value here is already a whole pixel position.
        if( gDialogShowing )
        {
            SDL_Rect dialogRect = { MD_DIALOG_X, MD_DIALOG_Y, MD_DIALOG_W, MD_DIALOG_H };
            SDL_RenderCopy( gRenderer, gScreenTexture, &dialogRect, &dialogRect );
        }

        // Same idea, for the "-fps" overlay's own top-left rect.
        if( gFpsOverlayShowing )
        {
            SDL_Rect fpsRect = { 0, 0, gFpsOverlayW, gFpsOverlayH };
            SDL_RenderCopy( gRenderer, gScreenTexture, &fpsRect, &fpsRect );
        }
    }

    SDL_RenderPresent( gRenderer );
}

// Real gameplay screenshots (generated via -ms, then cropped to the LCD
// area and resized to MD_THUMBNAIL_WIDTH x HEIGHT - see assets/thumbnails/'s
// own directory) - "thumb_00.bmp".."thumb_NN.bmp", indexed by registration
// order (the same order addGame() is called in menuGameList.c). The .bmp
// bytes themselves are compiled directly into the executable via
// thumbnailData.h (gThumbnailBlobs[]) - generated by tools/gen_thumbnails.py
// from assets/thumbnails/*.bmp, not read from disk at runtime.
#include "thumbnailData.h"

// Matches gameworld/menu.c's own MAX_GAMES.
#define THUMBNAIL_MAX_COUNT 112
static SDL_Surface* gThumbnails[ THUMBNAIL_MAX_COUNT ];
static int  gThumbnailCount = -1; // -1 = not yet probed

// Decodes gThumbnailBlobs[] in order, stopping at the first blob that
// fails to decode as a BMP (so a game with no thumbnail baked in yet is a
// silent no-op) - done once, lazily, on the first call from either public
// function below.
static void thumbnailsProbeIfNeeded()
{
    if( gThumbnailCount != -1 )
      return;

    gThumbnailCount = 0;
    for( int i = 0; i < gThumbnailBlobCount && i < THUMBNAIL_MAX_COUNT; i++ )
    {
        // SDL_RWops/SDL_RWFromConstMem/SDL_LoadBMP_RW, not SDL3's
        // SDL_IOStream/SDL_IOFromConstMem/SDL_LoadBMP_IO - SDL2's own
        // pre-IOStream-rename I/O abstraction. SDL_LoadBMP_RW()'s second
        // argument is a plain int "close it for me" flag (1), not SDL3's
        // bool.
        SDL_RWops* rw = SDL_RWFromConstMem( gThumbnailBlobs[ i ].data, gThumbnailBlobs[ i ].len );
        SDL_Surface* surf = SDL_LoadBMP_RW( rw, 1 );
        if( !surf )
          break;

        gThumbnails[ i ] = surf;
        gThumbnailCount = i + 1;
    }
}

int md_getThumbnailCount()
{
    thumbnailsProbeIfNeeded();
    return gThumbnailCount;
}

void md_drawGameThumbnail( int gameIndex, int x, int y )
{
    thumbnailsProbeIfNeeded();

    if( gameIndex < 0 || gameIndex >= gThumbnailCount || !gScreen )
      return;

    SDL_Rect dst = { x, y, MD_THUMBNAIL_WIDTH, MD_THUMBNAIL_HEIGHT };
    SDL_BlitSurface( gThumbnails[ gameIndex ], NULL, gScreen, &dst );
}

// =============================================================================
//   INPUT
// =============================================================================

// Raw held-frame counters, updated once per real frame in
// sdlBackend_pollEvents() - see machineDependent.h's own md_recentlyPressed()
// comment for why some ported games need the actual count, not just a
// level read.
static int gLeftFrames  = 0;
static int gRightFrames = 0;
static int gUpFrames    = 0;
static int gDownFrames  = 0;
static int gARawFrames  = 0; // pre-gate, see md_inputAFrames() below

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
// ported verbatim (same semantics, same reasoning) from the sibling
// gamebuino_classic_vircon32 build's own portVircon32.c. Armed once when a
// game is (re)launched from the menu, so the same press that confirmed the
// menu selection can't also be misread as the game's own first-frame input.
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

// gBSimFrames/gCSimFrames: simulated-press counters, mirroring gARawFrames
// above - needed because md_inputB()/md_inputC() (unlike md_inputUp()/
// Down()/Left()/Right(), which already read from a counter
// sdlBackend_pollEvents() would otherwise maintain) read
// gInput->Buttons.ButB/ButX directly, which -ms's batch screenshot mode
// never populates (it never calls sdlBackend_pollEvents() at all). OR'd
// with the real button read below so a real device press still works
// identically outside of -ms.
static int gBSimFrames = 0;
static int gCSimFrames = 0;

bool md_inputB() { return ( gInput && gInput->Buttons.ButB ) || gBSimFrames > 0; }

// See gamebuinoSDL2.h's own header comment for why real Gamebuino Button C
// is mapped onto CInput's ButX slot.
bool md_inputC() { return ( gInput && gInput->Buttons.ButX ) || gCSimFrames > 0; }

bool md_inputStart()
{
    // Either ButStart (gamepad Start, or the D keyboard alt-bind) or
    // ButBack (gamepad Back, or Escape) - matching the sibling
    // Tinyjoypad_SDL project's own dual-accept design, so a keyboard-only
    // player isn't forced to use one specific key.
    return gInput && ( gInput->Buttons.ButStart || gInput->Buttons.ButBack );
}

// Real-gray-color mode toggle (Button R) - see gamebuinoSDL2.h's own header
// comment for why this reads CInput's ButRB slot. A plain level read, not
// an edge check - gamesMain.c's own dispatch loop does its own edge
// detection against this (matching machineDependent.h's own contract).
bool md_inputR() { return gInput && gInput->Buttons.ButRB; }

void md_armInputAGate()
{
    gInputAGateActive = true;
}

// =============================================================================
//   AUDIO
// =============================================================================

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_AMPLITUDE   10000

static SDL_AudioDeviceID gAudioDevice  = 0;
static int   gFrameCounter = 0;

// A bank of independent voices, not a single shared tone - matches the
// sibling gamebuino_classic_vircon32 build's own real portVircon32.c
// md_playTone() design exactly (see that file's own header comment): real
// Gamebuino Classic hardware's own Sound engine defaults to NUM_CHANNELS=1,
// and every ported game's own call site is written assuming "this replaces
// whatever's currently sounding" - but that's a property of the ORIGINAL
// single-buzzer hardware, not something this function needs to enforce.
// Finding a free voice slot per call and mixing every active voice
// together lets two genuinely concurrent cues be heard at once instead of
// cutting each other off, with zero risk of ever sounding *wrong* for a
// game that only ever plays one tone at a time (the overwhelming majority).
#define AUDIO_MAX_VOICES 16
static bool  gToneActive[ AUDIO_MAX_VOICES ];
static float gToneFreq[ AUDIO_MAX_VOICES ];
static float gTonePhase[ AUDIO_MAX_VOICES ];
static int   gToneStopFrame[ AUDIO_MAX_VOICES ];

// Global mute (Button Y) - see machineDependent.h's own md_inputR() comment
// for why this, unlike the real-gray-color toggle, stays entirely
// backend-side: a pure audio-hardware volume control, matching the sibling
// Vircon32 build's own real `set_global_volume()` design, with no
// `gbMuted`-style flag anywhere in gamebuinoShim.c to reach.
static bool gMuted = false;

// Real analog output volume - see src/sdl3/sdlBackend.c's own identical
// declaration comment for the full reasoning.
static float gVolume = 1.0f;

// void(void*, Uint8*, int), not SDL3's void(void*, SDL_AudioStream*, int,
// int) - SDL2's classic audio callback fills the destination buffer
// directly in-place (no separate "put data into a stream object" call
// needed the way SDL3's SDL_PutAudioStreamData() works), so `stream` IS
// the buffer to fill and `len` is how many bytes it holds.
static void SDLCALL sdlAudioCallback( void* userdata, Uint8* stream, int len )
{
    (void)userdata;

    Sint16* buffer = (Sint16*)stream;
    int sampleCount = len / (int)sizeof( Sint16 );

    for( int i = 0; i < sampleCount; i++ )
    {
        // Square, not sine, per voice - matches real Gamebuino Classic
        // hardware's own genuine PWM-driven piezo speaker (a hard on/off
        // toggle, not a smooth waveform) far better than a pure sine would:
        // several games call gbPlayNote()/md_playTone() with very short
        // durations at low nominal frequencies, where a sine barely
        // completes a fraction of one cycle (near-silent), while a square
        // wave's hard edge is audible regardless of nominal frequency,
        // exactly like a real piezo buzzer being switched on for a moment.
        int mixed = 0;
        if( !gMuted )
        {
            for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
            {
                if( !gToneActive[ v ] || gToneFreq[ v ] <= 0.0f )
                  continue;

                mixed += (int)( ( gTonePhase[ v ] < (float)M_PI ? 1.0f : -1.0f )
                    * AUDIO_AMPLITUDE * gVolume );

                gTonePhase[ v ] += 2.0f * (float)M_PI * gToneFreq[ v ] / (float)AUDIO_SAMPLE_RATE;
                if( gTonePhase[ v ] >= 2.0f * (float)M_PI )
                  gTonePhase[ v ] -= 2.0f * (float)M_PI;
            }
        }

        // AUDIO_AMPLITUDE(10000) leaves enough headroom under Sint16's
        // +-32767 range for several simultaneous voices to sum cleanly - a
        // hard clamp rather than dynamic rescaling, since no game in this
        // cartridge plays more than 2-3 concurrent cues at once.
        if( mixed > 32767 ) mixed = 32767;
        if( mixed < -32768 ) mixed = -32768;
        buffer[ i ] = (Sint16)mixed;
    }
}

void md_initAudio()
{
    // SDL_InitSubSystem() returns 0 on success in SDL2 (a plain int), not
    // the bool SDL3 returns.
    if( SDL_InitSubSystem( SDL_INIT_AUDIO ) != 0 )
    {
        sdlLog( "Failed to init SDL audio subsystem: %s\n", SDL_GetError() );
        return;
    }

    // want/have, not a single spec + a returned stream object: SDL2's
    // SDL_OpenAudioDevice() negotiates against the device's own actual
    // capabilities and reports back what it really granted in `have` -
    // `want.samples` (a buffer size in SAMPLES, not bytes) has no SDL3
    // equivalent parameter at all, since SDL3's simplified
    // SDL_OpenAudioDeviceStream() picks a buffer size on its own.
    SDL_AudioSpec want, have;
    SDL_zero( want );
    want.freq     = AUDIO_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = sdlAudioCallback;

    gAudioDevice = SDL_OpenAudioDevice( NULL, 0, &want, &have, 0 );
    if( gAudioDevice == 0 )
    {
        sdlLog( "Failed to open audio device: %s\n", SDL_GetError() );
        return;
    }

    // SDL2 audio devices start PAUSED - unlike SDL3's SDL_OpenAudioDeviceStream
    // (which starts already-resumed, matching src/sdl3's own explicit
    // SDL_ResumeAudioDevice() call), SDL2's own device needs an explicit
    // "un-pause" instead. Do not skip this call - audio will be silent
    // otherwise.
    SDL_PauseAudioDevice( gAudioDevice, 0 );
}

void md_playTone( float freqHz, float durationSeconds )
{
    // A rest (freq<=0) is a genuine no-op here - it doesn't stop anything,
    // it just doesn't add a new voice, so whatever else is independently
    // playing on other voices continues unaffected.
    if( freqHz <= 0.0f )
      return;

    int slot = -1;
    for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
    {
        if( !gToneActive[ v ] )
        {
            slot = v;
            break;
        }
    }
    if( slot < 0 )
      slot = 0; // all voices busy - steal the first one rather than silently dropping the note

    gTonePhase[ slot ] = 0.0f;
    gToneFreq[ slot ] = freqHz;
    gToneActive[ slot ] = true;

    int durationFrames = (int)( durationSeconds * (float)MD_FRAMES_PER_SECOND );
    if( durationFrames < 1 )
      durationFrames = 1;

    // +1, not just gFrameCounter+durationFrames: main.c's own loop calls
    // gamesMain_dispatchFrame() (where every game's own gbPlayNote()/
    // md_playTone() call actually happens) BEFORE md_updateAudio() in the
    // very same real frame. Without this margin, a minimum-length
    // (1-frame) tone would get its gToneStopFrame set to
    // gFrameCounter+1, then md_updateAudio() (called moments later, same
    // iteration) increments gFrameCounter to exactly that value and
    // immediately expires it - cancelling the tone before the audio
    // callback thread has had virtually any real chance to render it, not
    // after a genuine frame of playback.
    gToneStopFrame[ slot ] = gFrameCounter + durationFrames + 1;
}

void md_stopTone()
{
    for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
    {
        gToneActive[ v ] = false;
        gToneStopFrame[ v ] = -1;
    }
}

void md_updateAudio()
{
    gFrameCounter++;

    for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
    {
        if( gToneActive[ v ] && gToneStopFrame[ v ] >= 0 && gFrameCounter >= gToneStopFrame[ v ] )
        {
            gToneActive[ v ] = false;
            gToneStopFrame[ v ] = -1;
        }
    }
}

int md_getFrameCounter()
{
    return gFrameCounter;
}

// =============================================================================
//   MEMORY CARD (backs eepromShim.h's persistent per-game EEPROM emulation)
// =============================================================================
// Backed by a real file - a dotfile in the user's home directory, matching
// the sibling Tinyjoypad_SDL project's own identical design (see that
// file's own header comment for the full HOME-vs-USERPROFILE-vs-"." fallback
// reasoning) - this project's own file name/card signature are the only
// real differences, so the two card files are never confused if a user
// happens to have both sibling projects installed. Deliberately the exact
// same file and on-disk format as src/sdl3's own identical implementation,
// so a player switching between the SDL3 and SDL2 builds on the same
// machine keeps the same saved high scores either way.
#define CARD_FILE_NAME ".gamebuino_classic_highscores"
#define CARD_SIGNATURE_TEXT "GAMEBUINOSDL01"
#define CARD_SIGNATURE_BYTES 32

static char gCardFilePath[ 1024 ];
static bool gCardPathResolved = false;

static void sdlBackendResolveCardFilePath()
{
    if( gCardPathResolved )
      return;
    gCardPathResolved = true;

    const char* homeDir = SDL_getenv( "HOME" );
#if defined( _WIN32 )
    if( homeDir == NULL || homeDir[ 0 ] == 0 )
      homeDir = SDL_getenv( "USERPROFILE" );
#endif
    if( homeDir == NULL || homeDir[ 0 ] == 0 )
      homeDir = ".";

    SDL_snprintf( gCardFilePath, sizeof( gCardFilePath ), "%s/%s", homeDir, CARD_FILE_NAME );
}

// Extends a shorter-than-needed file up to minSize with real zero bytes,
// rather than relying on fseek()-past-EOF-then-write's own implementation-
// defined gap-filling behavior.
static void sdlBackendEnsureFileSize( FILE* fp, long minSize )
{
    fseek( fp, 0, SEEK_END );
    long currentSize = ftell( fp );
    if( currentSize >= minSize )
      return;

    static const char zeroBuf[ 4096 ] = { 0 };
    long remaining = minSize - currentSize;
    while( remaining > 0 )
    {
        long chunk = remaining < (long)sizeof( zeroBuf ) ? remaining : (long)sizeof( zeroBuf );
        fwrite( zeroBuf, 1, (size_t)chunk, fp );
        remaining -= chunk;
    }
}

bool md_cardIsConnected()
{
    sdlBackendResolveCardFilePath();

    // "ab" (append) creates the file if missing without touching any
    // existing content otherwise - a real permissions/disk-full/etc
    // failure here correctly leaves eepromCardAvailable false upstream
    // (eepromShim.c), rather than silently pretending persistence works.
    FILE* fp = fopen( gCardFilePath, "ab" );
    if( !fp )
      return false;

    fclose( fp );
    return true;
}

bool md_cardHasOurSignature()
{
    char buf[ CARD_SIGNATURE_BYTES ];
    memset( buf, 0, sizeof( buf ) );

    FILE* fp = fopen( gCardFilePath, "rb" );
    if( !fp )
      return false;

    fread( buf, 1, sizeof( buf ), fp );
    fclose( fp );

    return strncmp( buf, CARD_SIGNATURE_TEXT, strlen( CARD_SIGNATURE_TEXT ) ) == 0;
}

void md_cardWriteSignature()
{
    char buf[ CARD_SIGNATURE_BYTES ];
    memset( buf, 0, sizeof( buf ) );
    strncpy( buf, CARD_SIGNATURE_TEXT, sizeof( buf ) - 1 );

    FILE* fp = fopen( gCardFilePath, "r+b" );
    if( !fp )
      return;

    sdlBackendEnsureFileSize( fp, (long)sizeof( buf ) );
    fseek( fp, 0, SEEK_SET );
    fwrite( buf, 1, sizeof( buf ), fp );
    fclose( fp );
}

void md_cardReadData( void* dest, int offsetBytes, int sizeBytes )
{
    // Zeroed first so a slot past the current real file length (never
    // written yet) reads back as real zeroed storage - eepromShim.c's own
    // "is this slot empty" check (magic != EEPROM_MAGIC) already handles
    // that correctly with no special-casing needed here.
    memset( dest, 0, (size_t)sizeBytes );

    FILE* fp = fopen( gCardFilePath, "rb" );
    if( !fp )
      return;

    fseek( fp, offsetBytes, SEEK_SET );
    fread( dest, 1, (size_t)sizeBytes, fp );
    fclose( fp );
}

void md_cardWriteData( void* src, int offsetBytes, int sizeBytes )
{
    FILE* fp = fopen( gCardFilePath, "r+b" );
    if( !fp )
      return;

    sdlBackendEnsureFileSize( fp, (long)( offsetBytes + sizeBytes ) );
    fseek( fp, offsetBytes, SEEK_SET );
    fwrite( src, 1, (size_t)sizeBytes, fp );
    fclose( fp );
}

// =============================================================================
//   sdlBackend.h - platform-only extras for main.c
// =============================================================================

static int  gConfigWindowW   = DEFAULT_WINDOW_WIDTH;
static int  gConfigWindowH   = DEFAULT_WINDOW_HEIGHT;
static bool gConfigFullscreen = false;
static bool gConfigVsync      = true;
static bool gConfigSoftwareRendering = false;

void sdlBackend_setWindowSize( int width, int height )
{
    if( width > 0 )
      gConfigWindowW = width;
    if( height > 0 )
      gConfigWindowH = height;
}

void sdlBackend_setFullscreen( bool fullscreen )
{
    gConfigFullscreen = fullscreen;
}

void sdlBackend_setVsync( bool enabled )
{
    gConfigVsync = enabled;
}

void sdlBackend_setSoftwareRendering( bool enabled )
{
    gConfigSoftwareRendering = enabled;
}

bool sdlBackend_init( int argc, char** argv )
{
    (void)argc;
    (void)argv;

    // SDL_Init() returns 0 on success in SDL2 (a plain int), not the bool
    // SDL3 returns - every SDL_Init()/SDL_InitSubSystem() check in this
    // file is `!= 0`, not SDL3's `!(...)`, for that reason.
    if( SDL_Init( SDL_INIT_VIDEO ) != 0 )
    {
        sdlLog( "Failed to init SDL video: %s\n", SDL_GetError() );
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_RESIZABLE;
    // SDL_WINDOW_FULLSCREEN_DESKTOP (borderless, matches the current
    // desktop video mode), not plain SDL_WINDOW_FULLSCREEN (a real
    // display-mode change) - matches what src/sdl3's own plain
    // SDL_WINDOW_FULLSCREEN actually does under SDL3's changed semantics,
    // keeping both ports' real behavior identical despite the different
    // flag name.
    if( gConfigFullscreen )
      windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    // 6-argument SDL_CreateWindow (title, x, y, w, h, flags), not SDL3's
    // 4-argument form (title, w, h, flags) - SDL2 never split window
    // position out of creation the way SDL3 did.
    gWindow = SDL_CreateWindow( "Gamebuino Classic for SDL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        gConfigWindowW, gConfigWindowH, windowFlags );

    if( !gWindow )
    {
        sdlLog( "Failed to create window: %s\n", SDL_GetError() );
        return false;
    }

    // (window, index, flags), not SDL3's (window, name) - index -1 means
    // "first driver matching flags" (SDL2 has no NULL-for-best-available
    // shorthand). SDL_RENDERER_SOFTWARE ("-s") instead of the default
    // _ACCELERATED - SDL2 has no NULL/"best available" driver-name
    // shorthand the way SDL3 does, so forcing software here means
    // swapping which one of these two mutually exclusive flags gets
    // requested, not passing a different name. SDL_RENDERER_PRESENTVSYNC
    // is folded into the flags here rather than a separate post-creation
    // call, since the runtime-toggleable SDL_RenderSetVSync() is a newer
    // (2.0.18+) SDL2 addition this project has no reason to depend on
    // when the flag does the same job at creation time.
    //
    // A vsync-locked SDL_RenderPresent() (called every real frame from
    // md_endFrame() below) can block for an extended, unpredictable
    // period - even effectively indefinitely - on a virtual/remote-
    // session display adapter that never delivers a real vblank signal to
    // an idle or occluded window (a real, externally-documented D3D-
    // present limitation, not an SDL bug). Investigated directly in this
    // project's own real build/sandbox environment: a `-ms` batch run
    // with vsync on (the default) crawled to a near-halt partway through,
    // while the identical run with vsync off (`-nd`) sailed through all
    // 99 games in seconds - and the SAME symptom (a "Not Responding"
    // process with near-zero accumulated CPU) was independently
    // reproduced against the unmodified, already-proven-working
    // src/sdl3 reference build too, which this SDL2 port's own
    // translation never touched. This is therefore a genuine environment/
    // display-driver limitation of this specific sandbox, affecting both
    // ports identically - not a bug introduced by the SDL3->SDL2 API
    // translation, and not something fixable from this file (there is no
    // portable way to put a timeout on a synchronous SDL_RenderPresent()
    // call without a watchdog thread, which risks papering over a real
    // future genuine deadlock and was deliberately not added here without
    // being able to verify it actually resolves anything in this specific
    // environment). A real display with a real compositor delivering real
    // vblank signals should never hit this at all. See CLAUDE.md/this
    // project's own test-running conventions for why every automated
    // capture/smoke-test run in this repo now always passes `-nd`.
    Uint32 rendererFlags = gConfigSoftwareRendering ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    if( gConfigVsync )
      rendererFlags |= SDL_RENDERER_PRESENTVSYNC;

    gRenderer = SDL_CreateRenderer( gWindow, -1, rendererFlags );
    if( !gRenderer )
    {
        sdlLog( "Failed to create renderer: %s\n", SDL_GetError() );
        return false;
    }

    gInput = CInput_Create();

    // SDL_GetRendererInfo(), not SDL3's SDL_GetRendererName() - SDL2 never
    // added a name-only shorthand, only the full info struct.
    SDL_RendererInfo rendererInfo;
    SDL_GetRendererInfo( gRenderer, &rendererInfo );
    sdlLog( "sdlBackend initialized: renderer=%s vsync=%s\n",
        rendererInfo.name, gConfigVsync ? "on" : "off" );

    return true;
}

bool sdlBackend_saveScreenshot( const char* path )
{
    if( !gScreen )
      return false;

    // Deliberately gScreen (the clean, pre-effects content), not whatever
    // the pixel-grid overlay currently looks like - -ms's screenshots
    // double as this project's own thumbnail source material, which should
    // stay crisp regardless of whatever a player happens to have the
    // pixel-grid overlay set to. SDL_SaveBMP() returns 0 on success in
    // SDL2 (a plain int), not the bool SDL3 returns.
    return SDL_SaveBMP( gScreen, path ) == 0;
}

void sdlBackend_simulateAFrame( bool pressed )
{
    updateHeldCounter( &gARawFrames, pressed );
}

void sdlBackend_simulateUpFrame( bool held ) { updateHeldCounter( &gUpFrames, held ); }
void sdlBackend_simulateDownFrame( bool held ) { updateHeldCounter( &gDownFrames, held ); }
void sdlBackend_simulateLeftFrame( bool held ) { updateHeldCounter( &gLeftFrames, held ); }
void sdlBackend_simulateRightFrame( bool held ) { updateHeldCounter( &gRightFrames, held ); }
void sdlBackend_simulateBFrame( bool pressed ) { updateHeldCounter( &gBSimFrames, pressed ); }
void sdlBackend_simulateCFrame( bool pressed ) { updateHeldCounter( &gCSimFrames, pressed ); }

void sdlBackend_shutdown()
{
    if( gAudioDevice )
    {
        SDL_CloseAudioDevice( gAudioDevice );
        gAudioDevice = 0;
    }

    if( gInput )
    {
        CInput_Destroy( gInput );
        gInput = NULL;
    }

    if( gScreenTexture )
    {
        SDL_DestroyTexture( gScreenTexture );
        gScreenTexture = NULL;
    }

    if( gScreen )
    {
        // SDL_FreeSurface, not SDL3's SDL_DestroySurface - SDL2's own
        // original (never-renamed) name.
        SDL_FreeSurface( gScreen );
        gScreen = NULL;
    }

    if( gGlowEffect )
    {
        GlowEffect_Destroy( gGlowEffect );
        gGlowEffect = NULL;
    }

    if( gCrtEffect )
    {
        CrtEffect_Destroy( gCrtEffect );
        gCrtEffect = NULL;
    }

    if( gRenderer )
    {
        SDL_DestroyRenderer( gRenderer );
        gRenderer = NULL;
    }

    if( gWindow )
    {
        SDL_DestroyWindow( gWindow );
        gWindow = NULL;
    }

    SDL_Quit();
}

void sdlBackend_pollEvents()
{
    if( !gInput )
      return;

    CInput_Update( gInput );

    if( gInput->Buttons.ButQuit )
      gQuit = true;

    bool left  = gInput->Buttons.ButLeft  || gInput->Buttons.ButDpadLeft;
    bool right = gInput->Buttons.ButRight || gInput->Buttons.ButDpadRight;
    bool up    = gInput->Buttons.ButUp    || gInput->Buttons.ButDpadUp;
    bool down  = gInput->Buttons.ButDown  || gInput->Buttons.ButDpadDown;
    bool a     = gInput->Buttons.ButA;

    updateHeldCounter( &gLeftFrames,  left );
    updateHeldCounter( &gRightFrames, right );
    updateHeldCounter( &gUpFrames,    up );
    updateHeldCounter( &gDownFrames,  down );
    updateHeldCounter( &gARawFrames,  a );

    // Button L1/G (effect cycle - pixel-grid/glow/CRT) - see
    // src/sdl3/sdlBackend.c's own identical comment for the full "real
    // 3-bit counter, all 8 combinations" reasoning. Only meaningful during
    // actual gameplay (matching drawPixelGridOverlay()'s own render gate
    // above), so the press is only even looked at then.
    if( gInGame && gInput->Buttons.ButLB && !gInput->PrevButtons.ButLB )
    {
        gEffectState = ( gEffectState + 1 ) % 8;
        gPixelGridEnabled = ( gEffectState & 1 ) != 0;
        gGlowEnabled      = ( gEffectState & 2 ) != 0;
        gCrtEnabled       = ( gEffectState & 4 ) != 0;
    }

    // Button L2/R2 (real volume down/up, ButLT/ButRT) - see
    // src/sdl3/sdlBackend.c's own identical comment for the full reasoning
    // (matches Tinyjoypad_SDL's own PageDown/PageUp step size exactly).
    if( gInput->Buttons.ButLT && !gInput->PrevButtons.ButLT )
    {
        gVolume -= 0.05f;
        if( gVolume < 0.0f )
          gVolume = 0.0f;
        sdlLog( "Volume: %d%%\n", (int)( gVolume * 100.0f + 0.5f ) );
    }
    if( gInput->Buttons.ButRT && !gInput->PrevButtons.ButRT )
    {
        gVolume += 0.05f;
        if( gVolume > 1.0f )
          gVolume = 1.0f;
        sdlLog( "Volume: %d%%\n", (int)( gVolume * 100.0f + 0.5f ) );
    }

    // Button Y (global mute toggle) - deliberately NOT gated to "a game is
    // running", matching the sibling Vircon32 build's own identical
    // design: the menu itself is silent today, but gating this to
    // gameplay-only would be a surprising inconsistency.
    if( gInput->Buttons.ButY && !gInput->PrevButtons.ButY )
    {
        gMuted = !gMuted;
        sdlLog( "Sound: %s\n", gMuted ? "muted" : "unmuted" );
    }

    // BUTTON_FULLSCREEN (F3) - a live toggle via SDL_SetWindowFullscreen(),
    // independent of the -f startup flag (sdlBackend_setFullscreen()/
    // gConfigFullscreen, read once at sdlBackend_init() time). Reuses
    // gConfigFullscreen itself as the current live state rather than a
    // separate variable, since nothing else reads it after init.
    // SDL_SetWindowFullscreen() takes a Uint32 flags value here (0 =
    // windowed, SDL_WINDOW_FULLSCREEN_DESKTOP = fullscreen), not SDL3's
    // plain bool - matches the same flag used at window-creation time
    // above.
    if( gInput->Buttons.ButFullscreen && !gInput->PrevButtons.ButFullscreen )
    {
        gConfigFullscreen = !gConfigFullscreen;
        SDL_SetWindowFullscreen( gWindow, gConfigFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0 );
    }
}

bool sdlBackend_shouldQuit()
{
    return gQuit;
}
