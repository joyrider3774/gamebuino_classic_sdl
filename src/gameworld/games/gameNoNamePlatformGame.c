#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// NoNamePlatformGame (Frakasss, no license specified -
// github.com/Frakasss/NoNamePlatformGame). A tiny endless side-scrolling
// platformer: walk left/right across a scrolling landscape (a ground
// strip plus one recurring elevated block every 50 world-pixels),
// jumping with Button B and crouching with Down - no real win/lose
// condition at all, just a walking-and-jumping toy (confirmed by reading
// every real .ino file completely - Levels.ino is genuinely empty, and
// there is no scoring, no enemies, no death state anywhere in the real
// source).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment). The real `Player` struct's
// fields were flattened to plain `nnpg`-prefixed globals
// (`player.x_screen` -> `nnpgXScreen`, etc - this project's own
// established "flatten a real single-instance struct into plain globals"
// treatment).
//
// REAL BITMAP ART: `gamelogo[]`/`playerSprite[2][10]`/`background[1][18]`
// (Sprites.ino) are real Arduino `B01111111`-style binary literals -
// converted to `0x` hex byte-for-byte, same row-major layout real
// upstream already used (one row per LCD scanline, `ceil(width/8)` bytes
// per row, MSB = leftmost pixel - confirmed directly against
// `gbDrawBitmap()`'s own real decode loop in gamebuinoShim.c, not
// assumed), so no transposition was needed, only a base conversion.
// Element counts verified against each array's own real
// `{width,height,...}` header (38 total for the 24x12 gamelogo, 10 per
// 5x8 playerSprite pose, 18 for the 8x16 background tile) rather than
// hand-transcribed.
//
// BLOCKING `gb.titleScreen(gamelogo)` -> EXPLICIT STATE: real upstream
// calls this once, blocking, in `setup()` (before `loop()` ever runs),
// and again - with NO arguments at all, a distinct real overload - from
// `fnctn_checkbuttons()` any time Button C is pressed mid-game (a real
// "show the splash again" pause gesture, not a menu/quit). Both become
// one shared `NNPG_STATE_TITLE` (this project's own established
// "blocking widget -> explicit resumable state" treatment, e.g.
// gamePong.c/gameArtillery.c's own single re-used title state) - the
// no-argument mid-game call is treated as re-showing the same real logo
// screen rather than inventing a second, blank title state, since real
// upstream's own player state is never reset by either call
// (`fnctn_initPlayer()` runs exactly once, in `setup()`, before the very
// first title screen - not after either dismissal). Ported the same way:
// `nnpgInitPlayer()` is called exactly once, from
// `gameNoNamePlatformGame_init()`, never again from the title state or
// from a Button-C press mid-game.
//
// SOUND: `nnpgPlaySoundFx()` is a direct, byte-for-byte port of real
// upstream's own `outpt_soundfx(byte fxno)` - the same 4
// `gbSoundCommand()` calls (volume/instrument/slide/arpeggio, always
// channel 0, matching upstream's own hardcoded `0` argument throughout)
// plus a final `gbPlayNoteChannel()`, against the existing
// `nnpgSoundTable[][]` table.
//
// DROPPED, NO EQUIVALENT: `gb.battery.show = false;` (a real-hardware-
// only battery-icon display setting, matching this project's own
// established "purely cosmetic, dropped outright" precedent for this
// exact call).
//
// PRESERVED REAL UPSTREAM QUIRKS (kept exactly as shipped, matching this
// project's own default per CLAUDE.md):
// 1) `for_y`/`check01`/`check02` (NoNamePlatformGame.ino) are genuine
//    dead globals - declared, never read or written anywhere in the real
//    source (grep-confirmed across every .ino file) - not ported at all.
// 2) `outpt_displayPlayer()`'s real "blink" test also compares
//    `player.eyes==90`/`==91`, but `fnctn_playerEyes()` only ever cycles
//    `eyes` through `0..79` (`(eyes+1)%80`) - those two branches are
//    genuinely unreachable dead code in real upstream too. Ported
//    verbatim below (`nnpgDisplayPlayer()` still checks `nnpgEyes==90`/
//    `==91`), not removed or "fixed".
// 3) The real per-frame draw order calls `outpt_drawLandscape()` TWICE -
//    once immediately (using whatever color was left set from the
//    previous frame) purely so `fnctn_checkbuttons()`'s own real
//    `getPixel()` collision checks have real landscape pixels to read,
//    then again (in BLACK, after an explicit WHITE `fillRect()` erase)
//    for the actual visible frame. Ported with the identical real
//    double-draw shape (`nnpgUpdatePlaying()` below) rather than
//    simplified to a single draw, since the collision checks in
//    `nnpgCheckButtons()` genuinely depend on the first draw already
//    being on-screen when they run - this shim's own `gbUpdate()` already
//    clears the framebuffer once per tick (matching real hardware's own
//    default `persistence=false` auto-clear), so the buffer is correctly
//    blank before this first draw happens, exactly like real hardware.

int nnpgGamelogoBitmap[38] = {
    24, 12,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x24,
    0x00, 0x00, 0x38,
    0x00, 0x00, 0x54,
    0x00, 0x00, 0x7C,
    0x00, 0x00, 0x38,
    0x00, 0x00, 0x38,
    0x00, 0x00, 0x28,
    0x00, 0x00, 0x28,
};

int nnpgPlayerSprite[2][10] = {
    { 5, 8, 0x48, 0x70, 0xA8, 0xF8, 0x70, 0x70, 0x50, 0x50 }, // normal
    { 5, 8, 0x00, 0x00, 0x00, 0x48, 0x70, 0xA8, 0xF8, 0x70 }, // crouch
};

int nnpgBackgroundBitmap[1][18] = {
    { 8, 16, 0x10, 0x10, 0x28, 0x28, 0x44, 0x44, 0x92, 0x82, 0x8A, 0xA2, 0x82, 0x44, 0x38, 0x10, 0x10, 0x10 },
};

// Real upstream sound-effect table (Sounds.ino), already plain decimal -
// copied verbatim. Column 1 = pitch, column 7 = duration (see the header
// comment's own "SOUND" section for why only those two columns are used).
int nnpgSoundTable[6][8] = {
    { 1, 17, 53, 0, 7, 0, 2, 3 },
    { 1, 17, 53, 0, 7, 0, 10, 3 },
    { 1, 26, 41, 1, 1, 3, 7, 20 },
    { 0, 0, 42, 1, 1, 2, 7, 20 },
    { 0, 54, 0, 0, 0, 0, 7, 1 },
    { 0, 0, 65, 1, 1, 1, 7, 5 },
};

#define NNPG_STATE_TITLE 0
#define NNPG_STATE_PLAYING 1

int nnpgState;

int nnpgLevelLength;
int nnpgXScreen;
int nnpgYScreen;
int nnpgXWorld;
int nnpgJumpStatus;
int nnpgFall;
int nnpgCrouch;
int nnpgWalking;
int nnpgDir;
int nnpgEyes;

// Direct port of real upstream's own `void outpt_soundfx(byte fxno)` -
// always channel 0, matching every one of that function's own real
// gb.sound.command()/playNote() calls exactly.
void nnpgPlaySoundFx( int fxno )
{
    gbSoundCommand( GB_CMD_VOLUME, nnpgSoundTable[ fxno ][ 6 ], 0, 0 );
    gbSoundCommand( GB_CMD_INSTRUMENT, nnpgSoundTable[ fxno ][ 0 ], 0, 0 );
    gbSoundCommand( GB_CMD_SLIDE, nnpgSoundTable[ fxno ][ 5 ], -nnpgSoundTable[ fxno ][ 4 ], 0 );
    gbSoundCommand( GB_CMD_ARPEGGIO, nnpgSoundTable[ fxno ][ 3 ], nnpgSoundTable[ fxno ][ 2 ] - 58, 0 );
    gbPlayNoteChannel( nnpgSoundTable[ fxno ][ 1 ], nnpgSoundTable[ fxno ][ 7 ], 0 );
}

// Direct port of real `fnctn_initPlayer()`.
void nnpgInitPlayer()
{
    nnpgXScreen = 0;
    nnpgYScreen = 30;
    nnpgXWorld = 40;
    nnpgJumpStatus = 0;
    nnpgFall = 0;
    nnpgCrouch = 0;
    nnpgWalking = 0;
    nnpgDir = 1;
    nnpgEyes = 0;
}

// Direct port of real `fnctn_playerEyes()`.
void nnpgPlayerEyes()
{
    nnpgEyes = ( nnpgEyes + 1 ) % 80;
}

// Direct port of real `fnctn_checkJump()`.
void nnpgCheckJump()
{
    if( nnpgJumpStatus == 8 ) nnpgYScreen = nnpgYScreen - 6;
    else if( nnpgJumpStatus == 7 ) nnpgYScreen = nnpgYScreen - 4;
    else if( nnpgJumpStatus == 6 ) nnpgYScreen = nnpgYScreen - 2;
    else if( nnpgJumpStatus == 5 ) nnpgYScreen = nnpgYScreen - 1;

    if( nnpgJumpStatus > 0 ) nnpgJumpStatus = nnpgJumpStatus - 1;
}

// Direct port of real `fnctn_checkPlayerPos()`.
void nnpgCheckPlayerPos()
{
    int k;
    if( gbGetPixel( nnpgXScreen, nnpgYScreen + 8 ) == 0 && gbGetPixel( nnpgXScreen + 4, nnpgYScreen + 8 ) == 0 )
    {
        nnpgYScreen = nnpgYScreen + 1;
        if( nnpgFall < 4 ) nnpgFall = nnpgFall + 1;
        for( k = 0; k < nnpgFall; k = k + 1 )
        {
            if( gbGetPixel( nnpgXScreen, nnpgYScreen + 8 ) == 0 && gbGetPixel( nnpgXScreen + 4, nnpgYScreen + 8 ) == 0 )
              nnpgYScreen = nnpgYScreen + 1;
        }
    }
    else
    {
        nnpgFall = 0;
    }
}

// Direct port of real `fnctn_checkbuttons()`, minus the real BTN_C
// "show title screen" branch - that one is handled by
// `nnpgUpdatePlaying()` itself before this function is ever called (see
// the header comment's own "BLOCKING titleScreen()" section).
void nnpgCheckButtons()
{
    if( gbPressed( BTN_B ) )
    {
        if( nnpgJumpStatus == 0 && nnpgCrouch == 0 )
        {
            nnpgPlaySoundFx( 5 );
            nnpgJumpStatus = 8;
            nnpgWalking = 0;
        }
    }

    if( gbRepeat( BTN_RIGHT, 0 ) )
    {
        if( nnpgDir == 0 )
        {
            nnpgDir = 1;
        }
        else
        {
            if( gbGetPixel( nnpgXScreen + 5, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen + 5, nnpgYScreen + 7 ) == 1 )
            {
                // real upstream: blocked, do nothing
            }
            else
            {
                if( nnpgXScreen < 84 )
                {
                    if( nnpgWalking != 1 ) nnpgWalking = 1;
                    else nnpgWalking = 2;

                    if( nnpgXScreen < 40 )
                    {
                        if( nnpgCrouch == 1 ) nnpgXScreen = nnpgXScreen + 1;
                        else
                        {
                            if( gbGetPixel( nnpgXScreen + 6, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen + 6, nnpgYScreen + 7 ) == 1 )
                              nnpgXScreen = nnpgXScreen + 1;
                            else
                              nnpgXScreen = nnpgXScreen + 2;
                        }
                    }
                    else
                    {
                        if( nnpgXWorld < nnpgLevelLength - 40 )
                        {
                            if( nnpgCrouch == 1 ) nnpgXWorld = nnpgXWorld + 1;
                            else
                            {
                                if( gbGetPixel( nnpgXScreen + 6, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen + 6, nnpgYScreen + 7 ) == 1 )
                                  nnpgXWorld = nnpgXWorld + 1;
                                else
                                  nnpgXWorld = nnpgXWorld + 2;
                            }
                        }
                        else
                        {
                            if( nnpgCrouch == 1 ) nnpgXScreen = nnpgXScreen + 1;
                            else
                            {
                                if( gbGetPixel( nnpgXScreen + 6, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen + 6, nnpgYScreen + 7 ) == 1 )
                                  nnpgXScreen = nnpgXScreen + 1;
                                else
                                  nnpgXScreen = nnpgXScreen + 2;
                            }
                        }
                    }
                }
            }
        }
    }
    else if( gbRepeat( BTN_LEFT, 0 ) )
    {
        if( nnpgDir == 1 )
        {
            nnpgDir = 0;
        }
        else
        {
            if( gbGetPixel( nnpgXScreen - 1, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen - 1, nnpgYScreen + 7 ) == 1 )
            {
                // real upstream: blocked, do nothing
            }
            else
            {
                if( nnpgXScreen > 0 )
                {
                    if( nnpgWalking != 1 ) nnpgWalking = 1;
                    else nnpgWalking = 2;

                    if( nnpgXScreen > 40 )
                    {
                        if( nnpgCrouch == 1 ) nnpgXScreen = nnpgXScreen - 1;
                        else
                        {
                            if( gbGetPixel( nnpgXScreen - 2, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen - 2, nnpgYScreen + 7 ) == 1 )
                              nnpgXScreen = nnpgXScreen - 1;
                            else
                              nnpgXScreen = nnpgXScreen - 2;
                        }
                    }
                    else
                    {
                        if( nnpgXWorld > 40 )
                        {
                            if( nnpgCrouch == 1 ) nnpgXWorld = nnpgXWorld - 1;
                            else
                            {
                                if( gbGetPixel( nnpgXScreen - 2, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen - 2, nnpgYScreen + 7 ) == 1 )
                                  nnpgXWorld = nnpgXWorld - 1;
                                else
                                  nnpgXWorld = nnpgXWorld - 2;
                            }
                        }
                        else
                        {
                            if( nnpgCrouch == 1 ) nnpgXScreen = nnpgXScreen - 1;
                            else
                            {
                                if( gbGetPixel( nnpgXScreen - 2, nnpgYScreen + ( 5 * nnpgCrouch ) ) == 1 || gbGetPixel( nnpgXScreen - 2, nnpgYScreen + 7 ) == 1 )
                                  nnpgXScreen = nnpgXScreen - 1;
                                else
                                  nnpgXScreen = nnpgXScreen - 2;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        nnpgWalking = 0;
    }

    if( gbRepeat( BTN_DOWN, 0 ) )
    {
        nnpgCrouch = 1;
    }
    else
    {
        if( nnpgCrouch == 1 && ( gbGetPixel( nnpgXScreen, nnpgYScreen ) == 1 || gbGetPixel( nnpgXScreen + 4, nnpgYScreen ) == 1 ) )
        {
            // real upstream: still stuck under something, stay crouched
        }
        else
        {
            nnpgCrouch = 0;
        }
    }
}

// Direct port of real `outpt_displayPlayer()`.
void nnpgDisplayPlayer()
{
    if( nnpgDir == 0 )
      gbDrawBitmapRotated( nnpgXScreen - 1, nnpgYScreen, nnpgPlayerSprite[ nnpgCrouch ], 0, 1 ); // NOROT, FLIPH
    else
      gbDrawBitmap( nnpgXScreen, nnpgYScreen, nnpgPlayerSprite[ nnpgCrouch ] ); // NOROT, NOFLIP

    if( nnpgWalking != 0 && nnpgFall == 0 )
    {
        gbSetColor( GB_INVERT );
        gbDrawPixel( nnpgXScreen + ( nnpgWalking * 2 ) - 1, nnpgYScreen + 7 );
        gbSetColor( GB_BLACK );
    }

    // See header comment #2 - eyes==90/91 are real, genuinely unreachable
    // upstream dead code (nnpgEyes only ever cycles 0..79), preserved.
    if( nnpgEyes == 0 || nnpgEyes == 1 || nnpgEyes == 40 || nnpgEyes == 41 || nnpgEyes == 45 || nnpgEyes == 46 || nnpgEyes == 90 || nnpgEyes == 91 )
      gbDrawFastHLine( nnpgXScreen + 1, nnpgYScreen + 2 + ( nnpgCrouch * 3 ), 3 );
}

// Direct port of real `outpt_drawLandscape()`.
void nnpgDrawLandscape()
{
    gbDrawFastHLine( 0, 45, 84 );
    gbDrawFastHLine( 0, 46, 84 );
    gbDrawFastHLine( 0, 47, 84 );
    gbDrawFastHLine( 0, 48, 84 );

    if( 100 - nnpgXWorld > -8 && 100 - nnpgXWorld < 92 )
      gbFillRect( 100 - nnpgXWorld, 35, 8, 8 );
}

// Direct port of real `outpt_drawBackground()`.
void nnpgDrawBackground()
{
    int i;
    for( i = 0; i < nnpgLevelLength / 50; i = i + 1 )
    {
        if( i * 50 - nnpgXWorld > -8 && i * 50 - nnpgXWorld < 92 )
          gbDrawBitmap( i * 50 - nnpgXWorld, 29, nnpgBackgroundBitmap[ 0 ] );
    }
}

// See the header comment's own "BLOCKING titleScreen()" section - this
// draws the real logo bitmap plus a "PRESS A" prompt, matching real
// upstream's own `gb.titleScreen(gamelogo)` widget behavior.
void nnpgUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 30, 10, nnpgGamelogoBitmap );
    gbCursorX = 28;
    gbCursorY = 30;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      nnpgState = NNPG_STATE_PLAYING;
}

// Direct port of real `loop()`'s own `if(gb.update()){...}` body - see the
// header comment's own quirk #3 for why `nnpgDrawLandscape()` is called
// twice.
void nnpgUpdatePlaying()
{
    if( gbPressed( BTN_C ) )
    {
        nnpgState = NNPG_STATE_TITLE;
        return;
    }

    nnpgDrawLandscape();
    nnpgCheckButtons();
    gbSetColor( GB_WHITE );
    gbFillRect( 0, 0, 84, 48 );
    gbSetColor( GB_BLACK );
    nnpgDrawLandscape();
    nnpgDisplayPlayer();
    nnpgPlayerEyes();

    nnpgCheckJump();
    if( nnpgJumpStatus < 4 )
      nnpgCheckPlayerPos();

    nnpgDrawBackground();
}

void gameNoNamePlatformGame_init()
{
    gbBegin();
    nnpgLevelLength = 1000;
    nnpgInitPlayer();
    nnpgState = NNPG_STATE_TITLE;
}

void gameNoNamePlatformGame_update()
{
    if( !gbUpdate() ) return;

    if( nnpgState == NNPG_STATE_PLAYING )
      nnpgUpdatePlaying();
    else
      nnpgUpdateTitle();

    gbRenderFrame();
}
