// -----------------------------------------------------------------------------
// The "SDL platform backend" - implements every machineDependent.h function
// plus the sdlBackend.h platform-only extras main.c needs. Freely includes
// SDL.h (unlike the "game world" TU - see machineDependent.h's own comment
// for why the two sides are kept apart).
//
// Ported from the sibling Tinyjoypad_SDL project's own sdlBackend.c - same
// overall shape (one persistent full-screen-resolution SDL_Surface, a
// software square-wave multi-voice audio mixer, a file-backed memory card),
// with this project's own real facts substituted throughout: 84x48/6-page
// LCD (not 128x64/8-page), 7 real buttons plus Start/L/R/Y (not Fire/
// Fire2/Start), WHITE game background (not BLACK - see md_beginFrame()'s
// own doc comment in machineDependent.h), a real second gray-tinted render
// pass (md_drawColumnGray()), and a real BLACK menu background under WHITE
// BIOS text (md_beginMenuFrame() - the opposite polarity from a game's own
// content, see that function's own doc comment). This project's own
// pixel-grid overlay is drawn directly as real alpha-blended grid lines
// rather than a pre-baked texture asset, since SDL has no texture-size
// ceiling to design around (unlike the Vircon32 build's own
// tools/gen_pixelgrid.py-baked PNG) - kept as its own separate Button L
// toggle rather than folded into the glow/CRT cycle below, unlike
// Tinyjoypad_SDL's own single-button 5-state cycle (which bundles pixel-
// grid together with glow/CRT) - this project's pixel-grid toggle already
// shipped on its own dedicated button before glow/CRT were ever added, so
// merging it into a new combined cycle would have been a real, unasked-for
// behavior change to already-tested UX. Glow (`glowEffect.h`) and CRT
// scanlines (`crtEffect.h`) are copied verbatim from Tinyjoypad_SDL (both
// self-contained, GPU-backed, no project-specific coupling - see each
// file's own header comment) and driven by their own separate 3-state
// cycle (none -> glow -> CRT -> none) on Button R2/G, per direct request.
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "machineDependent.h"
#include "sdlBackend.h"
#include "CInput.h"
#include "gamebuinoSDL3.h"
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
// time. Same two real reasons as the sibling Tinyjoypad_SDL project's own
// identical design (see that file's own header comment in full): a single
// shared coordinate space lets md_drawSolidRect()/biosFont.h's menu text/
// game columns all draw directly at final screen coordinates with no
// separate scale-up step, and skipping a redraw (the quit dialog's own
// "game update() not called this frame" behavior) needs a truly persistent
// surface, not a renderer backbuffer that isn't guaranteed to retain its
// contents across presents.
//
// 84*7=588, 48*7=336 - centered in the 640x360 canvas with a 26px bar
// left/right and a 12px bar top/bottom, matching the sibling Vircon32
// build's own real TILE_SCALE/ORIGIN_X/ORIGIN_Y layout exactly (not
// Tinyjoypad_SDL's own different 128x64/5x/20px layout).
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

// Only the pixel-grid overlay is ported from the sibling Tinyjoypad_SDL
// project's own richer glow/CRT/pixel-grid effect set (see this file's own
// header comment for why) - drawn directly as real alpha-blended grid
// lines onto the renderer's own backbuffer every frame, matching the
// sibling gamebuino_classic_vircon32 build's own identical "only while a
// game is actually running" gate (md_setInGame()).
static bool gPixelGridEnabled = false;
static bool gInGame           = false;

// Glow/CRT presentation effects (glowEffect.h/crtEffect.h, copied verbatim
// from Tinyjoypad_SDL) - Button L1/G steps a real 3-bit counter
// (gEffectState) through all 8 combinations of pixel-grid/glow/CRT, per
// direct request - a genuine superset of Tinyjoypad_SDL's own curated
// 5-state cycle (which never combines pixel-grid with CRT, for instance).
// gPixelGridEnabled/gGlowEnabled/gCrtEnabled are always kept in lockstep
// with gEffectState's own 3 bits, recomputed from it on every press rather
// than tracked independently - there is no longer a separate direct
// pixel-grid-only toggle (see gamebuinoSDL3.h's own header comment for
// why: pixel-grid moved from its own dedicated Button L slot into this
// combined cycle, freeing that slot for L2/R2 real volume control).
static int         gEffectState = 0; // bit0=pixel-grid, bit1=glow, bit2=CRT
static GlowEffect* gGlowEffect  = NULL;
static bool        gGlowEnabled = false;
static CrtEffect*  gCrtEffect   = NULL;
static bool        gCrtEnabled  = false;
static Uint64      gLastFrameTicks = 0; // for CrtEffect_Update()'s own real-time deltaTime

// Set by md_setDialogShowing()/md_setFpsOverlayShowing() (gamesMain.c, once
// per real frame) - see machineDependent.h's own doc comments on both.
// md_endFrame() re-composites these two rects crisply on top of the
// pixel-grid overlay, the same "restore the clean pixels from the texture
// that already has them" trick the sibling Tinyjoypad_SDL project's own
// md_endFrame() uses for the same reason.
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
    gScreen = SDL_CreateSurface( SCREEN_LOGICAL_W, SCREEN_LOGICAL_H, SDL_PIXELFORMAT_RGBA32 );
    gScreenTexture = SDL_CreateTexture( gRenderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H );

    if( gScreenTexture )
      SDL_SetTextureScaleMode( gScreenTexture, SDL_SCALEMODE_NEAREST );

    SDL_SetRenderLogicalPresentation( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H,
        SDL_LOGICAL_PRESENTATION_LETTERBOX );

    if( gScreen )
    {
        gWhitePixel = SDL_MapSurfaceRGBA( gScreen, 255, 255, 255, 255 );
        gBlackPixel = SDL_MapSurfaceRGBA( gScreen, 0, 0, 0, 255 );
        gGrayPixel  = SDL_MapSurfaceRGBA( gScreen, 128, 128, 128, 255 );
        gRedPixel   = SDL_MapSurfaceRGBA( gScreen, 220, 40, 40, 255 );
    }

    // Same real constants Tinyjoypad_SDL's own sdlBackend.c uses for both
    // effects - screen-space post-process parameters, nothing here depends
    // on this project's own different GAME_SCALE/LCD size.
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

    SDL_FillSurfaceRect( gScreen, NULL, gWhitePixel );
}

// BLACK - the opposite polarity, for the menu's own white-on-black BIOS
// text (see this function's own doc comment in machineDependent.h).
void md_beginMenuFrame()
{
    if( !gScreen )
      return;

    SDL_FillSurfaceRect( gScreen, NULL, gBlackPixel );
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
        SDL_FillSurfaceRect( gScreen, &r, pixel );
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
    SDL_FillSurfaceRect( gScreen, &rect, pixel );
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
        SDL_FillSurfaceRect( gScreen, &r, pixel );
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

    for( int col = 0; col <= LCD_WIDTH; col++ )
    {
        int x = GAME_ORIGIN_X + col * GAME_SCALE;
        SDL_RenderLine( gRenderer, (float)x, (float)top, (float)x, (float)bottom );
    }
    for( int row = 0; row <= LCD_HEIGHT; row++ )
    {
        int y = GAME_ORIGIN_Y + row * GAME_SCALE;
        SDL_RenderLine( gRenderer, (float)left, (float)y, (float)right, (float)y );
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
        SDL_RenderTexture( gRenderer, gScreenTexture, NULL, NULL );

        // Only while actual gameplay is running (matching the sibling
        // Vircon32 build's own `currentGameIndex != -1` gate) - never on
        // the menu, and not drawn while the quit-confirmation dialog's own
        // box is up either (gInGame stays true for the dialog's whole
        // duration too, so the frozen game view behind/around it keeps
        // showing the grid - only the box itself is re-composited crisp
        // below).
        // Glow/CRT (glowEffect.h/crtEffect.h) - drawn straight onto the
        // backbuffer just cleared and filled above, same reasoning as the
        // pixel-grid overlay just below: SDL_RenderClear() wipes this every
        // frame regardless of what either effect drew last time, so neither
        // can accumulate frame over frame. Glow first, then pixel-grid,
        // matching Tinyjoypad_SDL's own exact draw order (glow is a
        // brightness layer meant to sit under a display-type overlay, not
        // on top of it).
        Uint64 nowTicks = SDL_GetTicks();
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
        if( gDialogShowing )
        {
            SDL_FRect dialogRect = { (float)MD_DIALOG_X, (float)MD_DIALOG_Y, (float)MD_DIALOG_W, (float)MD_DIALOG_H };
            SDL_RenderTexture( gRenderer, gScreenTexture, &dialogRect, &dialogRect );
        }

        // Same idea, for the "-fps" overlay's own top-left rect.
        if( gFpsOverlayShowing )
        {
            SDL_FRect fpsRect = { 0.0f, 0.0f, (float)gFpsOverlayW, (float)gFpsOverlayH };
            SDL_RenderTexture( gRenderer, gScreenTexture, &fpsRect, &fpsRect );
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
        SDL_IOStream* io = SDL_IOFromConstMem( gThumbnailBlobs[ i ].data, gThumbnailBlobs[ i ].len );
        SDL_Surface* surf = SDL_LoadBMP_IO( io, true );
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

// See gamebuinoSDL3.h's own header comment for why real Gamebuino Button C
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

// Real-gray-color mode toggle (Button R) - see gamebuinoSDL3.h's own header
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

static SDL_AudioStream* gAudioStream   = NULL;
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
// Per-voice volume (0..1) - a one-shot md_playTone() voice always plays at
// full volume (1.0), matching this backend's own original behavior before
// this field existed; gamebuinoShim.c's own tracker engine (md_trackerVoice
// Start()/Retune()) is what actually varies this, for a real note's own
// volume-slide/tremolo/instrument-step dynamics.
static float gToneVolume[ AUDIO_MAX_VOICES ];

// Global mute (Button Y) - see machineDependent.h's own md_inputR() comment
// for why this, unlike the real-gray-color toggle, stays entirely
// backend-side: a pure audio-hardware volume control, matching the sibling
// Vircon32 build's own real `set_global_volume()` design, with no
// `gbMuted`-style flag anywhere in gamebuinoShim.c to reach.
static bool gMuted = false;

// Real analog output volume (0.0-1.0), stepped by 0.05f/press on
// ButLT/ButRT (L2/R2, sdlBackend_pollEvents()) - see gamebuinoSDL3.h's own
// header comment. Independent of gMuted (mute is a full silence override,
// matching the sibling Vircon32 build's own real set_global_volume()
// design; this is a genuine gain control on top of that, unmuted-state-
// only, matching Tinyjoypad_SDL's own identical gVolume/gMuted split).
static float gVolume = 1.0f;

static void SDLCALL sdlAudioCallback( void* userdata, SDL_AudioStream* stream,
    int additionalAmount, int totalAmount )
{
    (void)userdata;
    (void)totalAmount;

    if( additionalAmount <= 0 )
      return;

    Uint8* raw = (Uint8*)SDL_stack_alloc( Uint8, additionalAmount );
    if( !raw )
      return;

    Sint16* buffer = (Sint16*)raw;
    int sampleCount = additionalAmount / (int)sizeof( Sint16 );

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
                    * AUDIO_AMPLITUDE * gVolume * gToneVolume[ v ] );

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

    SDL_PutAudioStreamData( stream, raw, additionalAmount );
    SDL_stack_free( raw );
}

void md_initAudio()
{
    if( !SDL_InitSubSystem( SDL_INIT_AUDIO ) )
    {
        sdlLog( "Failed to init SDL audio subsystem: %s\n", SDL_GetError() );
        return;
    }

    SDL_AudioSpec spec = { 0 };
    spec.freq     = AUDIO_SAMPLE_RATE;
    spec.format   = SDL_AUDIO_S16;
    spec.channels = 1;

    gAudioStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec, sdlAudioCallback, NULL );

    if( !gAudioStream )
    {
        sdlLog( "Failed to open audio stream: %s\n", SDL_GetError() );
        return;
    }

    gAudioDevice = SDL_GetAudioStreamDevice( gAudioStream );
    if( gAudioDevice )
      SDL_ResumeAudioDevice( gAudioDevice );
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
    gToneVolume[ slot ] = 1.0f;
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

// See machineDependent.h's own doc comment for the full rationale (a
// continuously-retunable voice, for gamebuinoShim.c's own tracker/pattern
// engine, distinct from md_playTone()'s own fixed-duration one-shot pool
// above - though both share the same underlying AUDIO_MAX_VOICES bank).
// A tracker voice never auto-expires by frame count (gToneStopFrame stays
// -1, the same "not scheduled to auto-stop" sentinel md_stopTone() already
// uses) - its lifetime is managed explicitly by gamebuinoShim.c's own
// gbStopNoteChannel(), matching real hardware's own note-duration-driven
// (not wall-clock-driven) lifetime exactly.
int md_trackerVoiceStart( float freqHz, float volume )
{
    if( freqHz <= 0.0f )
      return -1;

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
    gToneVolume[ slot ] = volume;
    gToneActive[ slot ] = true;
    gToneStopFrame[ slot ] = -1;

    return slot;
}

// Retunes an already-started tracker voice in place - no phase reset, no
// click, matching real hardware's own continuously-updated oscillator
// exactly: gTonePhase[] keeps advancing across the frequency change in the
// audio callback, it's never touched here.
void md_trackerVoiceRetune( int channel, float freqHz, float volume )
{
    if( channel < 0 || channel >= AUDIO_MAX_VOICES || freqHz <= 0.0f )
      return;

    gToneFreq[ channel ] = freqHz;
    gToneVolume[ channel ] = volume;
}

void md_trackerVoiceStop( int channel )
{
    if( channel < 0 || channel >= AUDIO_MAX_VOICES )
      return;

    gToneActive[ channel ] = false;
    gToneStopFrame[ channel ] = -1;
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
// happens to have both sibling projects installed.
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

    if( !SDL_Init( SDL_INIT_VIDEO ) )
    {
        sdlLog( "Failed to init SDL video: %s\n", SDL_GetError() );
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_RESIZABLE;
    if( gConfigFullscreen )
      windowFlags |= SDL_WINDOW_FULLSCREEN;

    gWindow = SDL_CreateWindow( "Gamebuino Classic for SDL", gConfigWindowW,
        gConfigWindowH, windowFlags );

    if( !gWindow )
    {
        sdlLog( "Failed to create window: %s\n", SDL_GetError() );
        return false;
    }

    // NULL = let SDL auto-pick its own best-available driver (typically
    // hardware-accelerated); SDL_SOFTWARE_RENDERER ("software", a real
    // SDL3-defined driver name) forces its built-in CPU rasterizer instead
    // - "-s" on the command line, see main.c.
    gRenderer = SDL_CreateRenderer( gWindow,
        gConfigSoftwareRendering ? SDL_SOFTWARE_RENDERER : NULL );
    if( !gRenderer )
    {
        sdlLog( "Failed to create renderer: %s\n", SDL_GetError() );
        return false;
    }

    SDL_SetRenderVSync( gRenderer, gConfigVsync ? 1 : 0 );

    gInput = CInput_Create();

    sdlLog( "sdlBackend initialized: renderer=%s vsync=%s\n",
        SDL_GetRendererName( gRenderer ), gConfigVsync ? "on" : "off" );

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
    // pixel-grid overlay set to.
    return SDL_SaveBMP( gScreen, path );
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
        SDL_DestroySurface( gScreen );
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

    // Button L1/G (effect cycle - pixel-grid/glow/CRT, see gamebuinoSDL3.h's
    // own header comment for why this reads CInput's ButLB slot) - per
    // direct request, a real 3-bit counter enumerating ALL 8 combinations
    // of the three effects (pixel-grid alone was this project's own
    // original, already-shipped toggle; glow/CRT are the two ported from
    // Tinyjoypad_SDL) rather than Tinyjoypad_SDL's own curated 5-state
    // subset (which skips several combinations, e.g. pixel-grid+CRT
    // together) - one press always advances to the next of the 8, wrapping
    // back to "all off" after the 8th. Only meaningful during actual
    // gameplay (matching drawPixelGridOverlay()'s own render gate above),
    // so the press is only even looked at then.
    if( gInGame && gInput->Buttons.ButLB && !gInput->PrevButtons.ButLB )
    {
        gEffectState = ( gEffectState + 1 ) % 8;
        gPixelGridEnabled = ( gEffectState & 1 ) != 0;
        gGlowEnabled      = ( gEffectState & 2 ) != 0;
        gCrtEnabled       = ( gEffectState & 4 ) != 0;
    }

    // Button L2/R2 (real volume down/up, ButLT/ButRT) - per direct request,
    // matching Tinyjoypad_SDL's own PageDown/PageUp step size (0.05f per
    // press) and behavior exactly, just remapped onto the two real analog
    // shoulder triggers instead of that project's own ButLB/ButRB (both
    // already spoken for here - see gamebuinoSDL3.h's own header comment).
    // Deliberately NOT gated to gInGame, matching the mute toggle just
    // below - volume is a hardware-level concern, not a gameplay one.
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
    if( gInput->Buttons.ButFullscreen && !gInput->PrevButtons.ButFullscreen )
    {
        gConfigFullscreen = !gConfigFullscreen;
        SDL_SetWindowFullscreen( gWindow, gConfigFullscreen );
    }
}

bool sdlBackend_shouldQuit()
{
    return gQuit;
}
