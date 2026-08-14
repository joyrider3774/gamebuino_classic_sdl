#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Conduit (adekto, MIT - github.com/adekto/conduit). A pipe-connection
// sliding puzzle: an 8x6 grid of tiles where the leftmost/rightmost
// columns hold four fixed "plug" endpoints (a black pair and a white
// pair) and the interior 6x6 block of pipe tiles can be shifted a whole
// row/column at a time (wrapping around) - the goal is to slide the
// interior until every plug is actually connected all the way through to
// its own matching-color partner, tested via a real recursive
// flood-trace along the pipe graph, not just visually. Includes a small
// built-in level editor (place/erase tiles with a brush) and a "reset
// stats" option. Picked from `more games/` per this project's own
// DISCOVERED_GAMES.md porting-priority audit.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (see gamePong.c's own header comment for
// why - this dialect has no classes/methods). Every global symbol got a
// `cond`-prefixed name (this cartridge has no linker - see this project's
// own CLAUDE.md). Upstream's own direction constants (`byte u=1,d=2,l=3,
// r=4;`) became `#define COND_DIR_*`.
//
// Upstream's own blocking `gb.menu(menu, MENULENGTH)` (a real, built-in
// Gamebuino Classic list-menu widget with no equivalent here) was
// rewritten as an explicit `COND_STATE_MENU` state with its own tiny
// hand-rolled up/down/A list, matching this project's own established
// "blocking widget -> explicit state" treatment (see gamePong.c's own
// PONG_STATE_TITLE for the same idea applied to `gb.titleScreen()`).
// Upstream's own three `gamestate` values (1=menu, 2=play, 3=edit) became
// `COND_STATE_MENU/PLAY/EDIT`, alongside a 4th, `COND_STATE_TITLE`, for
// upstream's own separate real title screen (see below).
//
// Upstream's own `gb.display.drawBitmap(x, y, tile[n], NOROT, NOFLIP)`
// puzzle-tile draws use a bespoke local `condDrawTilePx()` helper instead
// of this shim's own general `gbDrawBitmap()` primitive, since upstream's
// own real tile bitmap bytes (`condTiles[][8]`, copied verbatim from
// upstream's own real `tile[][10]` PROGMEM table, just dropping the
// leading width/height=8,8 header every entry shared) are a fixed 8x8
// format with no width/height header of their own - `condDrawTilePx()`
// calls `gbDrawPixel()` bit-by-bit directly against those raw bytes. This
// works out pixel-for-pixel identical to upstream's own real bitmap
// format: both this shim's framebuffer (`gbFrameBuffer[]`, see
// gamebuinoShim.c) and real Gamebuino's own bitmap byte layout pack 8
// vertical pixels per byte, LSB = top - the exact same convention, so no
// bit-reversal or reshuffling was needed, just a straight per-bit copy.
// The upstream 64x30 `title[]` splash bitmap (shown once via
// `gb.titleScreen(F(""), title)`) is drawn as a real, separate
// COND_STATE_TITLE screen (condTitleBitmap/condUpdateTitle() below) via
// the real `gbDrawBitmap()` primitive - copied verbatim from upstream's
// own real `0x`-hex byte literals (already valid syntax in this dialect,
// no B-binary conversion needed).
//
// Real `GRAY` (a dithered checkerboard shade the real PCD8544 driver
// fakes since the LCD itself is strictly monochrome) has no equivalent in
// this shim (`gbSetColor()` only takes 0/1 - see gamebuinoShim.h) - every
// grid-overlay draw call that asked for GRAY was ported using plain black
// (1) instead; a purely cosmetic simplification (the overlay is still
// fully functional as a grid aid, just a bit bolder than upstream's own
// lighter grid dots/lines).
//
// A genuine upstream quirk found while reading `scanBlack()`/`scanWhite()`
// closely, and preserved (not silently dropped) here: their `byte x, y`
// parameters are unsigned 8-bit on real AVR, so `x-1` at `x==0` (or `y-1`
// at `y==0`) silently underflows to 255 - which upstream's own very next
// line (`if (x > 7 || y > 5) return false;`) then catches as "out of
// bounds" anyway, relying on that underflow as an implicit bounds check
// rather than an explicit one. This is a real, reachable case here too:
// `condSwapUp()`/`condSwapDown()` rotate a whole column through all 6
// rows (0-5) including the very top/bottom row, so a vertical pipe
// segment genuinely can land on row 0 (or 5) and then get scanned
// "further up" (or "down") right off the edge. Since this port's own
// `condScanBlack()`/`condScanWhite()` use plain (signed, non-wrapping)
// `int` coordinates instead of AVR's wrapping `byte`, that same implicit
// trick would instead read a genuinely negative array index - so an
// explicit `x < 0 || y < 0` check was added alongside the original
// `x > 7 || y > 5` one, reproducing the exact same "stop the scan"
// outcome without relying on unsigned wraparound.
//
// Upstream's own `count` global (incremented once per recursive scan
// call, reset to 0 at the top of `pathfind()`) is a leftover debug
// counter - read nowhere, written nowhere else, with zero effect on any
// real gameplay outcome - dropped outright here rather than ported as
// dead weight.
//
// Upstream's own `gb.sound.playOK()`/`playCancel()` calls on the play
// screen's A/B presses were already commented out in the real source
// (`//gb.sound.playOK();` / `//gb.sound.playCancel();`) - left uncalled
// here too, matching upstream's own actual shipped behavior rather than
// "fixing" it by un-commenting sound upstream itself never enabled.
// `gb.sound.playTick()` on an actual tile swap was kept (`gbPlayTick()`).
//
// `condStatsDraw()` calls the shim's own real gbSetFont(), matching
// upstream's own exact `StatsDraw()` cursor positions/font choices verbatim
// (points at (64,43) in font3x5 - real hardware's own default font, "WIN"/
// "GAME OVER" at (64,10)/(30,10) and the move counter at (64,20), all in the
// larger font5x7). `condUpdateMenu()`'s own PLAY GAME/EDIT LVL/RESET list
// below is a different, hand-rolled screen with no equivalent real layout
// to restore (upstream used its own binary `gb.menu()` widget there, out of
// this shim's scope - see that function's own comment), so it keeps its own
// bespoke spacing/shortened "RESET" label as-is. No EEPROM persistence is
// used - upstream never touches EEPROM either.

#define COND_DIR_U 1
#define COND_DIR_D 2
#define COND_DIR_L 3
#define COND_DIR_R 4

#define COND_MENU_LEN 3

// The real upstream title splash (a 64x30 bitmap shown via the blocking
// `gb.titleScreen(F(""), title)` call - see this file's own header comment
// on why that's now a genuine separate COND_STATE_TITLE screen). Restored
// via the project's own gbDrawBitmap() primitive, added after this port was
// first written - copied verbatim from upstream's own real `0x`-hex byte
// literals (already valid syntax in this dialect, no B-binary conversion
// needed), unlike condTiles[][8] below (a bespoke fixed-size format this
// file's own condDrawTilePx() reads directly, with no width/height header -
// not the same layout as gbDrawBitmap expects, so left as its own thing).
int condTitleBitmap[242]=
{
64,30,0x84,0xE0,0x0,0x10,0xA0,0x0,0x42,0x80,0x97,0x1F,0x0,0x10,0x20,0x0,0x40,0x80,0x91,0x50,0x80,0xC,0xC0,0x0,0x33,0x0,0x8C,0x2,0x40,0x8,0x4E,0x7,0x21,0x0,0x40,0x1,0x20,0x9,0x71,0xF8,0xE5,0x0,0x21,0x10,0xA0,0x9,0x15,0xA,0x85,0x0,0x1F,0x1C,0x20,0x8,0xC0,0x60,0x9,0x0,0x0,0xE4,0x20,0x4,0x0,0x0,0x2,0x0,0x0,0x6,0x60,0x2,0x11,0x8,0x84,0x0,0x0,0x8,0x10,0x1,0xF1,0xF8,0xF8,0x0,0x0,0x8,0x50,0x0,0xE,0x7,0x0,0x0,0x0,0x8,0x10,0x0,0x0,0x0,0x0,0x0,0x0,0x6,0x60,0x0,0x0,0x0,0x0,0x0,0xC0,0xE4,0x20,0x0,0x0,0x0,0x0,0x0,0x3F,0x1C,0xA0,0x0,0x0,0x0,0x0,0x0,0xA1,0x50,0xA0,0x0,0x0,0x0,0x0,0x0,0xC,0x1,0x20,0x0,0x7,0x0,0x7,0x0,0x0,0x0,0x40,0x0,0x5,0x0,0x5,0x0,0x21,0x10,0x80,0x0,0x5,0x0,0x7,0xC,0x3F,0x1F,0x0,0x0,0x5,0x0,0x0,0x14,0xC0,0xE0,0x0,0x0,0x7,0x0,0x0,0x14,0x3E,0x1F,0x8E,0xF0,0x77,0x38,0xE7,0x37,0x7E,0x3F,0x4B,0xE8,0xBF,0x28,0xA5,0x3F,0xF0,0x79,0xAE,0x29,0xE7,0x38,0xA5,0x1C,0xE0,0x70,0xAE,0x29,0xC7,0x38,0xA5,0x1C,0xE0,0x70,0xEE,0x39,0xC7,0x38,0xE7,0x1C,0xE0,0x70,0xEE,0x39,0xC7,0x38,0xE7,0x1C,0xF0,0x79,0xEE,0x39,0xC7,0x38,0xE7,0x1C,0x7E,0x3F,0xCE,0x38,0xFF,0x3F,0xE7,0x1F,0x3E,0x1F,0x8E,0x38,0x77,0x1E,0xE7,0xF,
};

enum CondState
{
    COND_STATE_TITLE = 0,
    COND_STATE_MENU = 1,
    COND_STATE_PLAY = 2,
    COND_STATE_EDIT = 3
};

int condState;
int condMenuIndex;

int condSelectX = 4;
int condSelectY = 3;
int condPoints;
int condPointGain;
int condPointLose;
int condMoves;
int condGrid;
int condBrush;
bool condScroll;
bool condWinState;

// Pipe-tile bitmaps - copied verbatim from upstream's own real
// `tile[][10]` PROGMEM table (dropping the shared width/height=8,8 header
// every entry had), 8 bytes per tile (one byte per column, LSB = top
// pixel - see this file's own header comment). Tile 0 doubles as the
// selection-cursor icon (a hollow box), matching upstream's own reuse of
// the same slot for both purposes.
int condTiles[23][8]=
{
{0xE7,0x81,0x81,0x0,0x0,0x81,0x81,0xE7},
{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18},
{0x0,0x0,0x0,0xFF,0xFF,0x0,0x0,0x0},
{0x0,0x0,0x0,0x1F,0x1F,0x18,0x18,0x18},
{0x0,0x0,0x0,0xF8,0xF8,0x18,0x18,0x18},
{0x18,0x18,0x18,0xF8,0xF8,0x0,0x0,0x0},
{0x18,0x18,0x18,0x1F,0x1F,0x0,0x0,0x0},
{0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24},
{0x0,0x0,0xFF,0x0,0x0,0xFF,0x0,0x0},
{0x0,0x0,0x1F,0x30,0x20,0x27,0x24,0x24},
{0x0,0x0,0xF8,0xC,0x4,0xE4,0x24,0x24},
{0x24,0x24,0xE4,0x4,0xC,0xF8,0x0,0x0},
{0x24,0x24,0x27,0x20,0x30,0x1F,0x0,0x0},
{0x18,0x18,0x1F,0xF8,0xF8,0x27,0x24,0x24},
{0x18,0x18,0xF8,0x1F,0x1F,0xE4,0x24,0x24},
{0x24,0x24,0xE4,0x1F,0x1F,0xF8,0x18,0x18},
{0x24,0x24,0x27,0xF8,0xF8,0x1F,0x18,0x18},
{0x24,0x24,0x24,0xFF,0xFF,0x24,0x24,0x24},
{0x18,0x18,0xFF,0x18,0x18,0xFF,0x18,0x18},
{0x0,0x18,0xE4,0x2,0x2,0xE4,0x18,0x0},
{0x0,0x18,0x27,0x40,0x40,0x27,0x18,0x0},
{0x0,0x0,0x18,0x3F,0x3F,0x18,0x0,0x0},
{0x0,0x0,0x18,0xFC,0xFC,0x18,0x0,0x0},
};

// The puzzle's own starting layout - copied verbatim from upstream's own
// real `mapdata[][8]` initializer ([y][x] order, matching upstream
// exactly). Columns 0 and 7 hold the four fixed plug endpoints (values
// 19-22) and are never touched by any swap - only the interior columns
// 1-6 (any row) and any full column 1-6 (all 6 rows) ever rotate.
int condInitialMap[6][8]=
{
{20,0,0,8,0,3,4,22},
{0,0,7,0,0,6,2,0},
{0,7,2,10,0,3,5,0},
{0,0,3,17,0,0,5,0},
{21,0,2,7,8,8,0,0},
{0,5,0,0,8,8,12,19},
};

int condMap[6][8];

void condResetMap()
{
    int x, y;
    for( y = 0; y < 6; y++ )
      for( x = 0; x < 8; x++ )
        condMap[ y ][ x ] = condInitialMap[ y ][ x ];
}

void condBeginTitle()
{
    condState = COND_STATE_TITLE;
}

void condBeginMenu()
{
    condState = COND_STATE_MENU;
}

void condBeginPlay()
{
    condState = COND_STATE_PLAY;
    condScroll = false;
}

void condBeginEdit()
{
    condState = COND_STATE_EDIT;
}

void condResetStats()
{
    condPoints = 10000;
    condPointGain = 1000;
    condPointLose = 5;
    condMoves = 0;
    condBrush = 0;
}

// Rotates row condSelectY's own interior columns (1-6) one step left/right,
// and column condSelectX's own full height (rows 0-5) one step up/down -
// direct ports of upstream's own swapLeft()/swapRight()/swapUp()/swapDown().
void condSwapLeft()
{
    int hold = condMap[ condSelectY ][ 1 ];
    int x;
    for( x = 1; x < 6; x++ )
      condMap[ condSelectY ][ x ] = condMap[ condSelectY ][ x + 1 ];
    condMap[ condSelectY ][ 6 ] = hold;
}

void condSwapRight()
{
    int hold = condMap[ condSelectY ][ 6 ];
    int x;
    for( x = 6; x > 1; x-- )
      condMap[ condSelectY ][ x ] = condMap[ condSelectY ][ x - 1 ];
    condMap[ condSelectY ][ 1 ] = hold;
}

void condSwapUp()
{
    int hold = condMap[ 0 ][ condSelectX ];
    int y;
    for( y = 0; y < 5; y++ )
      condMap[ y ][ condSelectX ] = condMap[ y + 1 ][ condSelectX ];
    condMap[ 5 ][ condSelectX ] = hold;
}

void condSwapDown()
{
    int hold = condMap[ 5 ][ condSelectX ];
    int y;
    for( y = 5; y > 0; y-- )
      condMap[ y ][ condSelectX ] = condMap[ y - 1 ][ condSelectX ];
    condMap[ 0 ][ condSelectX ] = hold;
}

// Recursive flood-trace along the black pipe network - direct port of
// upstream's own scanBlack() (see this file's own header comment for the
// explicit x<0/y<0 bounds-check quirk added here).
bool condScanBlack( int x, int y, int dir )
{
    if( x < 0 || x > 7 || y < 0 || y > 5 ) return false;
    if( condMap[ y ][ x ] == 0 ) return false;
    if( condMap[ y ][ x ] == 21 && dir == COND_DIR_L ) return true;
    if( condMap[ y ][ x ] == 22 && dir == COND_DIR_R ) return true;

    if( condMap[ y ][ x ] == 1 || condMap[ y ][ x ] == 18 )
    {
        if( dir == COND_DIR_U ) return condScanBlack( x, y - 1, COND_DIR_U );
        if( dir == COND_DIR_D ) return condScanBlack( x, y + 1, COND_DIR_D );
    }
    if( condMap[ y ][ x ] == 2 || condMap[ y ][ x ] == 17 )
    {
        if( dir == COND_DIR_L ) return condScanBlack( x - 1, y, COND_DIR_L );
        if( dir == COND_DIR_R ) return condScanBlack( x + 1, y, COND_DIR_R );
    }
    if( condMap[ y ][ x ] == 3 || condMap[ y ][ x ] == 15 )
    {
        if( dir == COND_DIR_L ) return condScanBlack( x, y + 1, COND_DIR_D );
        if( dir == COND_DIR_U ) return condScanBlack( x + 1, y, COND_DIR_R );
    }
    if( condMap[ y ][ x ] == 4 || condMap[ y ][ x ] == 16 )
    {
        if( dir == COND_DIR_R ) return condScanBlack( x, y + 1, COND_DIR_D );
        if( dir == COND_DIR_U ) return condScanBlack( x - 1, y, COND_DIR_L );
    }
    if( condMap[ y ][ x ] == 5 || condMap[ y ][ x ] == 13 )
    {
        if( dir == COND_DIR_R ) return condScanBlack( x, y - 1, COND_DIR_U );
        if( dir == COND_DIR_D ) return condScanBlack( x - 1, y, COND_DIR_L );
    }
    if( condMap[ y ][ x ] == 6 || condMap[ y ][ x ] == 14 )
    {
        if( dir == COND_DIR_L ) return condScanBlack( x, y - 1, COND_DIR_U );
        if( dir == COND_DIR_D ) return condScanBlack( x + 1, y, COND_DIR_R );
    }
    return false;
}

// Same idea, white pipe network - direct port of upstream's own scanWhite().
bool condScanWhite( int x, int y, int dir )
{
    if( x < 0 || x > 7 || y < 0 || y > 5 ) return false;
    if( condMap[ y ][ x ] == 0 ) return false;
    if( condMap[ y ][ x ] == 19 && dir == COND_DIR_R ) return true;
    if( condMap[ y ][ x ] == 20 && dir == COND_DIR_L ) return true;

    if( condMap[ y ][ x ] == 7 || condMap[ y ][ x ] == 17 )
    {
        if( dir == COND_DIR_U ) return condScanWhite( x, y - 1, COND_DIR_U );
        if( dir == COND_DIR_D ) return condScanWhite( x, y + 1, COND_DIR_D );
    }
    if( condMap[ y ][ x ] == 8 || condMap[ y ][ x ] == 18 )
    {
        if( dir == COND_DIR_L ) return condScanWhite( x - 1, y, COND_DIR_L );
        if( dir == COND_DIR_R ) return condScanWhite( x + 1, y, COND_DIR_R );
    }
    if( condMap[ y ][ x ] == 9 || condMap[ y ][ x ] == 13 )
    {
        if( dir == COND_DIR_L ) return condScanWhite( x, y + 1, COND_DIR_D );
        if( dir == COND_DIR_U ) return condScanWhite( x + 1, y, COND_DIR_R );
    }
    if( condMap[ y ][ x ] == 10 || condMap[ y ][ x ] == 14 )
    {
        if( dir == COND_DIR_R ) return condScanWhite( x, y + 1, COND_DIR_D );
        if( dir == COND_DIR_U ) return condScanWhite( x - 1, y, COND_DIR_L );
    }
    if( condMap[ y ][ x ] == 11 || condMap[ y ][ x ] == 15 )
    {
        if( dir == COND_DIR_R ) return condScanWhite( x, y - 1, COND_DIR_U );
        if( dir == COND_DIR_D ) return condScanWhite( x - 1, y, COND_DIR_L );
    }
    if( condMap[ y ][ x ] == 12 || condMap[ y ][ x ] == 16 )
    {
        if( dir == COND_DIR_L ) return condScanWhite( x, y - 1, COND_DIR_U );
        if( dir == COND_DIR_D ) return condScanWhite( x + 1, y, COND_DIR_R );
    }
    return false;
}

// Checks every plug endpoint is actually connected through to its own
// matching-color partner - direct port of upstream's own pathfind().
void condPathfind()
{
    condWinState = true;

    int x, y;
    for( x = 0; x < 8; x++ )
    {
        for( y = 0; y < 6; y++ )
        {
            if( condMap[ y ][ x ] > 18 )
            {
                if( condMap[ y ][ x ] == 22 )
                  if( !condScanBlack( x - 1, y, COND_DIR_L ) ) condWinState = false;
                if( condMap[ y ][ x ] == 21 )
                  if( !condScanBlack( x + 1, y, COND_DIR_R ) ) condWinState = false;
                if( condMap[ y ][ x ] == 19 )
                  if( !condScanWhite( x - 1, y, COND_DIR_L ) ) condWinState = false;
                if( condMap[ y ][ x ] == 20 )
                  if( !condScanWhite( x + 1, y, COND_DIR_R ) ) condWinState = false;
            }
        }
    }

    if( condWinState )
      condPoints = condPoints + condPointGain + condPointLose;
}

// Draws one 8x8 tile bitmap at a raw pixel position (not necessarily
// grid-aligned - the edit screen's own brush preview is drawn at upstream's
// own literal (63,10), overlapping the grid's own rightmost column, exactly
// matching upstream's own real drawBitmap(63,10,...) call).
void condDrawTilePx( int px, int py, int tileIdx )
{
    int col, row, byteVal;
    gbSetColor( 1 );
    for( col = 0; col < 8; col++ )
    {
        byteVal = condTiles[ tileIdx ][ col ];
        for( row = 0; row < 8; row++ )
          if( ( ( byteVal >> row ) & 1 ) == 1 )
            gbDrawPixel( px + col, py + row );
    }
}

// Draws the whole 8x6 playfield plus the optional grid overlay and the
// selection cursor - direct port of upstream's own GameDraw().
void condGameDraw()
{
    int x, y;
    for( x = 0; x < 8; x++ )
    {
        for( y = 0; y < 6; y++ )
        {
            if( condGrid == 1 )
            {
                gbDrawPixel( x * 8, y * 8 );
            }
            else if( condGrid == 2 )
            {
                gbSetColor( GB_GRAY );
                gbDrawFastHLine( x * 8 + 1, y * 8, 6 );
                gbDrawFastVLine( x * 8, y * 8 + 1, 6 );
                gbSetColor( 1 );
            }
            else if( condGrid == 3 )
            {
                gbSetColor( GB_GRAY );
                gbDrawFastVLine( x * 8, y * 8, 8 );
                gbDrawFastHLine( x * 8, y * 8, 8 );
                gbSetColor( 1 );
            }
            else if( condGrid == 4 )
            {
                gbSetColor( GB_GRAY );
                gbDrawPixel( x * 8, y * 8 );
                gbSetColor( 1 );
            }

            if( condMap[ y ][ x ] > 0 )
              condDrawTilePx( x * 8, y * 8, condMap[ y ][ x ] );
        }
    }

    condDrawTilePx( condSelectX * 8, condSelectY * 8, 0 );
}

// Draws the score/move counters and the win/game-over banner - direct port
// of upstream's own real StatsDraw(), cursor positions/fonts and all (see
// this file's own header comment).
void condStatsDraw()
{
    gbFontSize = 1;

    gbCursorX = 64;
    gbCursorY = 43;
    gbSetFont( gbFont3x5 );
    gbPrintNumber( condPoints );

    if( condWinState )
    {
        gbCursorX = 64;
        gbCursorY = 10;
        gbSetFont( gbFont5x7 );
        gbPrintString( "WIN" );
    }
    if( condPoints < 1 )
    {
        gbCursorX = 30;
        gbCursorY = 10;
        gbSetFont( gbFont5x7 );
        gbPrintString( "GAME OVER" );
    }

    gbCursorX = 64;
    gbCursorY = 20;
    gbSetFont( gbFont5x7 );
    gbPrintNumber( condMoves );
}

// The real title splash upstream showed via its own blocking
// `gb.titleScreen(F(""), title)` call, now a genuine separate state (see
// this file's own header comment on condTitleBitmap) - dismissed by a
// fresh Button A press, matching every other ported game's own title
// screen in this project (see gamePong.c's own header comment).
void condUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, condTitleBitmap );

    gbFontSize = 1;
    gbSetFont( gbFont3x5 ); // guard against condStatsDraw() leaving font5x7 selected from a previous round
    gbCursorX = 14;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      condBeginMenu();
}

// Hand-rolled replacement for upstream's own blocking `gb.menu(menu,
// MENULENGTH)` widget (see this file's own header comment) - three
// options, matching upstream's own MENULENGTH=3 list exactly (play game /
// edit level / reset stats, "level"/"stats" shortened for this shim's
// wider 8x8 font - see this file's own header comment).
void condUpdateMenu()
{
    gbFontSize = 1;
    gbSetFont( gbFont3x5 ); // guard against condStatsDraw() leaving font5x7 selected from a previous round
    gbCursorX = 2;
    gbCursorY = 1;
    gbPrintString( "CONDUIT" );

    int i;
    for( i = 0; i < COND_MENU_LEN; i++ )
    {
        gbCursorY = 16 + i * 10;
        gbCursorX = 0;
        if( i == condMenuIndex )
          gbPrintString( "*" );

        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "PLAY GAME" );
        if( i == 1 ) gbPrintString( "EDIT LVL" );
        if( i == 2 ) gbPrintString( "RESET" );
    }

    if( gbRepeat( BTN_UP, 5 ) )
      condMenuIndex = gbMax( 0, condMenuIndex - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) )
      condMenuIndex = gbMin( COND_MENU_LEN - 1, condMenuIndex + 1 );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( condMenuIndex == 0 ) condBeginPlay();
        else if( condMenuIndex == 1 ) condBeginEdit();
        else condResetStats();
    }
}

// Direct port of upstream's own GameTick().
void condUpdatePlay()
{
    if( gbRepeat( BTN_RIGHT, 5 ) )
    {
        if( condScroll )
        {
            gbPlayTick();
            condSwapRight();
            condPathfind();
            condPoints = condPoints - condPointLose;
            condMoves = condMoves + 1;
        }
        else if( condSelectX < 6 )
          condSelectX = condSelectX + 1;
    }
    if( gbRepeat( BTN_LEFT, 5 ) )
    {
        if( condScroll )
        {
            gbPlayTick();
            condSwapLeft();
            condPathfind();
            condPoints = condPoints - condPointLose;
            condMoves = condMoves + 1;
        }
        else if( condSelectX > 1 )
          condSelectX = condSelectX - 1;
    }
    if( gbRepeat( BTN_DOWN, 5 ) )
    {
        if( condScroll )
        {
            gbPlayTick();
            condSwapDown();
            condPathfind();
            condPoints = condPoints - condPointLose;
            condMoves = condMoves + 1;
        }
        else if( condSelectY < 5 )
          condSelectY = condSelectY + 1;
    }
    if( gbRepeat( BTN_UP, 5 ) )
    {
        if( condScroll )
        {
            gbPlayTick();
            condSwapUp();
            condPathfind();
            condPoints = condPoints - condPointLose;
            condMoves = condMoves + 1;
        }
        else if( condSelectY > 0 )
          condSelectY = condSelectY - 1;
    }

    // upstream's own gb.sound.playOK() here was already commented out in
    // the real source - see this file's own header comment.
    if( gbPressed( BTN_A ) )
    {
        if( !condWinState )
          condScroll = true;
    }
    if( gbReleased( BTN_A ) )
      condScroll = false;

    // upstream's own gb.sound.playCancel() here was already commented out
    // in the real source too - see this file's own header comment.
    if( gbPressed( BTN_B ) )
    {
        condGrid = condGrid + 1;
        if( condGrid > 4 ) condGrid = 0;
    }
    if( gbPressed( BTN_C ) )
      condBeginMenu();
}

// Direct port of upstream's own EditTick().
void condUpdateEdit()
{
    if( gbRepeat( BTN_RIGHT, 5 ) )
      if( condSelectX < 7 )
        condSelectX = condSelectX + 1;
    if( gbRepeat( BTN_LEFT, 5 ) )
      if( condSelectX > 0 )
        condSelectX = condSelectX - 1;
    if( gbRepeat( BTN_DOWN, 5 ) )
      if( condSelectY < 5 )
        condSelectY = condSelectY + 1;
    if( gbRepeat( BTN_UP, 5 ) )
      if( condSelectY > 0 )
        condSelectY = condSelectY - 1;

    if( gbRepeat( BTN_A, 10 ) )
      condMap[ condSelectY ][ condSelectX ] = 0;
    if( gbPressed( BTN_A ) )
      condMap[ condSelectY ][ condSelectX ] = condBrush;

    if( gbPressed( BTN_B ) )
    {
        condBrush = condBrush + 1;
        if( condBrush == 23 ) condBrush = 0;
    }
    if( gbPressed( BTN_C ) )
      condBeginMenu();

    condDrawTilePx( 63, 10, condBrush );
}

void gameConduit_init()
{
    gbBegin();

    condResetMap();
    condState = COND_STATE_TITLE;
    condMenuIndex = 0;
    condSelectX = 4;
    condSelectY = 3;
    condPoints = 10000;
    condPointGain = 1000;
    condPointLose = 5;
    condMoves = 0;
    condGrid = 0;
    condBrush = 0;
    condScroll = false;
    condWinState = false;
}

void gameConduit_update()
{
    if( !gbUpdate() ) return;

    if( condState == COND_STATE_TITLE )
      condUpdateTitle();
    else if( condState == COND_STATE_MENU )
      condUpdateMenu();
    else if( condState == COND_STATE_PLAY )
    {
        condUpdatePlay();
        condGameDraw();
        condStatsDraw();
    }
    else
    {
        condUpdateEdit();
        condGameDraw();
    }

    gbRenderFrame();
}
