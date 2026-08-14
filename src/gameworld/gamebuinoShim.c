#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include <stdlib.h>
#include <math.h>

// =============================================================================
// gamebuinoShim.c - see gamebuinoShim.h for the public API and its own
// rationale. Ported from the sibling gamebuino_classic_vircon32 project's
// own gamebuinoShim.c (proven there against 99 real games), converted from
// Vircon32's own C dialect back to standard C: `int[N] name = {...};` ->
// `int name[N] = {...};` array declarations (font tables, framebuffer,
// button-state arrays - data itself unchanged), and `gbPrintString()`/
// `gbPopup()`'s own `text` parameter (and `gbPrintNumber()`'s internal
// itoa() target buffer) -> `char*`/`char[]`, since this project's own game
// files pass real C string literals, not Vircon32's own `int[]`-per-
// character string convention. Every md_*() call site is retargeted from
// the Vircon32 build's own machineDependent.h onto this project's own SDL
// machineDependent.h (see that file for the exact signatures) - same
// single-voice md_playTone(freqHz, durationSeconds), same md_input*()
// button levels, same md_drawColumn()/md_drawColumnGray() column-byte
// addressing (this build draws each set bit as a real per-pixel filled rect
// directly onto a shared canvas rather than blitting a pre-baked GPU tile,
// but the col/page/value contract at this boundary is identical either
// way, so nothing above this line needed to change).
//
// gbDrawFastHLine()/gbDrawFastVLine()/gbDrawBitmap()/gbDrawBitmapRotated()
// are ported here as their real, plain per-pixel-gbDrawPixel() forms - the
// same shape real Gamebuino Classic hardware itself ships - rather than the
// sibling Vircon32 build's own hand-optimized versions (which hoist the
// destination page+bit computation out of the inner loop). That rewrite
// exists there specifically to dodge Vircon32's own documented flat
// ~10+2xargcount-instruction function-call overhead on a 15MHz CPU with a
// real per-frame instruction budget; a native x86/ARM build with a real
// SDL-backed canvas has no equivalent pressure (every one of these games
// already runs comfortably above 60fps unoptimized on any machine this
// port targets), so the simpler, more obviously-correct form is kept.
// =============================================================================

// -----------------------------------------------------------------------------
// Font tables - real Gamebuino Classic's own font5x7/font3x5/font3x3
// (utility/font5x7.c/font3x5.c/font3x3.c), ported verbatim (byte-for-byte
// identical data to the sibling Vircon32 build, which itself ported them
// verbatim from the real library source) - column-major, LSB-top, one real
// byte per table cell, indexed directly by ASCII 0-127 (including
// Gamebuino's own custom icon glyphs replacing the usual control-character
// range 0-31). Each font's own first two cells are its raw glyph
// width/height (5,7 / 3,5 / 3,3) - gbSetFont() reads them directly.
// -----------------------------------------------------------------------------

int gbFont5x7[ 642 ] = {
5, 7, 127, 65, 65, 65, 127, 62, 91, 79, 91, 62, 62, 107, 79, 107,
62, 24, 60, 120, 60, 24, 24, 60, 126, 60, 24, 28, 87, 125, 87, 28,
28, 94, 127, 94, 28, 0, 126, 67, 67, 126, 0, 126, 115, 115, 126, 0,
126, 127, 127, 126, 127, 65, 65, 65, 127, 48, 72, 58, 6, 14, 6, 41,
121, 41, 6, 127, 65, 65, 65, 127, 96, 112, 63, 2, 4, 42, 28, 54,
28, 42, 127, 62, 28, 8, 0, 0, 8, 28, 62, 127, 20, 34, 127, 34,
20, 60, 60, 60, 126, 255, 24, 66, 60, 129, 126, 62, 99, 117, 99, 62,
62, 97, 107, 99, 62, 62, 99, 107, 107, 62, 4, 2, 127, 2, 4, 16,
32, 127, 32, 16, 8, 8, 42, 28, 8, 8, 28, 42, 8, 8, 20, 62,
85, 65, 34, 99, 117, 105, 117, 99, 16, 24, 28, 24, 16, 4, 12, 28,
12, 4, 0, 0, 0, 0, 0, 0, 0, 95, 0, 0, 0, 7, 0, 7,
0, 20, 127, 20, 127, 20, 4, 42, 127, 42, 16, 35, 19, 8, 100, 98,
54, 73, 86, 32, 80, 0, 8, 7, 3, 0, 0, 28, 34, 65, 0, 0,
65, 34, 28, 0, 42, 28, 62, 28, 42, 8, 8, 62, 8, 8, 0, 224,
96, 0, 0, 8, 8, 8, 8, 8, 0, 96, 96, 0, 0, 96, 16, 8,
4, 3, 62, 81, 73, 69, 62, 0, 66, 127, 64, 0, 98, 81, 73, 73,
70, 33, 65, 73, 77, 51, 24, 20, 18, 127, 16, 39, 69, 69, 69, 57,
60, 74, 73, 73, 48, 65, 33, 17, 9, 7, 54, 73, 73, 73, 54, 6,
73, 73, 41, 30, 0, 0, 36, 0, 0, 0, 128, 100, 0, 0, 8, 20,
34, 65, 0, 20, 20, 20, 20, 20, 0, 65, 34, 20, 8, 2, 1, 89,
9, 6, 62, 65, 93, 89, 78, 124, 18, 17, 18, 124, 127, 73, 73, 73,
54, 62, 65, 65, 65, 34, 127, 65, 65, 34, 28, 127, 73, 73, 73, 65,
127, 9, 9, 9, 1, 62, 65, 65, 81, 50, 127, 8, 8, 8, 127, 0,
65, 127, 65, 0, 32, 65, 65, 63, 1, 127, 8, 20, 34, 65, 127, 64,
64, 64, 64, 127, 2, 12, 2, 127, 127, 2, 4, 8, 127, 62, 65, 65,
65, 62, 127, 9, 9, 9, 6, 62, 65, 97, 65, 190, 127, 9, 25, 41,
70, 38, 73, 73, 73, 50, 1, 1, 127, 1, 1, 63, 64, 64, 64, 63,
31, 32, 64, 32, 31, 63, 64, 56, 64, 63, 99, 20, 8, 20, 99, 3,
4, 120, 4, 3, 97, 81, 73, 69, 67, 0, 127, 65, 65, 0, 3, 4,
8, 16, 96, 0, 65, 65, 127, 0, 4, 2, 1, 2, 4, 128, 128, 128,
128, 128, 0, 3, 7, 8, 0, 32, 84, 84, 120, 64, 127, 40, 68, 68,
56, 56, 68, 68, 68, 40, 56, 68, 68, 40, 127, 56, 84, 84, 84, 24,
8, 126, 9, 9, 2, 24, 164, 164, 156, 120, 127, 8, 4, 4, 120, 0,
68, 125, 64, 0, 0, 96, 128, 132, 125, 127, 16, 16, 40, 68, 0, 65,
127, 64, 0, 124, 4, 120, 4, 120, 124, 8, 4, 4, 120, 56, 68, 68,
68, 56, 252, 24, 36, 36, 24, 24, 36, 36, 24, 252, 124, 8, 4, 4,
8, 8, 84, 84, 84, 32, 4, 63, 68, 68, 32, 60, 64, 64, 32, 124,
28, 32, 64, 32, 28, 60, 64, 32, 64, 60, 68, 40, 16, 40, 68, 76,
144, 144, 144, 124, 68, 100, 84, 76, 68, 0, 8, 54, 65, 0, 0, 0,
127, 0, 0, 0, 65, 54, 8, 0, 24, 4, 8, 16, 12, 127, 65, 65,
65, 127,
};

// Real Gamebuino Classic font3x5 data (utility/font3x5.c), byte-for-byte
// identical to the sibling gamebuino_classic_vircon32 build's own
// gbFont3x5 table.
int gbFont3x5[ 386 ] = {
3, 5,
63, 33, 63, 18, 8, 18, 10, 16, 10, 14, 28, 14, 12, 30, 12, 20,
26, 20, 22, 31, 22, 30, 19, 30, 30, 27, 30, 30, 31, 30, 63, 33,
63, 58, 47, 58, 23, 61, 23, 63, 33, 63, 24, 31, 2, 4, 10, 4,
31, 14, 4, 4, 14, 31, 10, 31, 10, 14, 14, 31, 4, 17, 14, 46,
37, 46, 47, 42, 46, 38, 41, 41, 2, 31, 2, 8, 31, 8, 21, 14,
4, 4, 14, 21, 14, 21, 21, 27, 21, 27, 4, 6, 4, 4, 12, 4,
0, 0, 0, 0, 23, 0, 3, 0, 3, 31, 10, 31, 22, 55, 26, 25,
4, 19, 10, 21, 58, 0, 3, 0, 0, 14, 17, 17, 14, 0, 10, 4,
10, 4, 14, 4, 0, 48, 0, 4, 4, 4, 0, 16, 0, 24, 4, 3,
31, 17, 31, 18, 31, 16, 29, 21, 23, 17, 21, 31, 7, 4, 31, 23,
21, 29, 31, 21, 29, 1, 1, 31, 31, 21, 31, 23, 21, 31, 0, 10,
0, 0, 50, 0, 4, 10, 17, 10, 10, 10, 17, 10, 4, 1, 21, 2,
14, 17, 23, 30, 5, 30, 31, 21, 10, 14, 17, 10, 31, 17, 14, 31,
21, 17, 31, 5, 1, 14, 17, 29, 31, 4, 31, 17, 31, 17, 8, 16,
15, 31, 4, 27, 31, 16, 16, 31, 6, 31, 30, 4, 15, 14, 17, 14,
31, 9, 6, 14, 17, 46, 31, 5, 26, 18, 21, 9, 1, 31, 1, 31,
16, 31, 15, 24, 15, 31, 12, 31, 27, 4, 27, 3, 28, 3, 25, 21,
19, 0, 31, 17, 3, 4, 24, 17, 31, 0, 2, 1, 2, 32, 32, 32,
0, 1, 2, 12, 18, 30, 31, 18, 12, 12, 18, 18, 12, 18, 31, 12,
26, 20, 4, 30, 5, 36, 42, 30, 31, 2, 28, 20, 29, 16, 32, 32,
29, 31, 8, 20, 17, 31, 16, 30, 4, 30, 30, 2, 28, 12, 18, 12,
62, 10, 4, 12, 18, 62, 30, 4, 2, 20, 22, 10, 2, 15, 18, 14,
16, 30, 14, 16, 14, 30, 8, 30, 18, 12, 18, 38, 40, 30, 50, 42,
38, 4, 30, 33, 0, 31, 0, 33, 30, 4, 1, 2, 1, 63, 33, 63,
};

// Real Gamebuino Classic font3x3 data (utility/font3x3.c), byte-for-byte
// identical to the sibling gamebuino_classic_vircon32 build's own
// gbFont3x3 table.
int gbFont3x3[ 386 ] = {
3, 3,
7, 5, 7, 5, 4, 5, 5, 4, 5, 3, 6, 3, 2, 7, 2, 2,
1, 2, 2, 3, 2, 14, 9, 14, 14, 13, 14, 14, 15, 14, 7, 5,
7, 5, 3, 7, 5, 15, 5, 7, 5, 7, 6, 7, 1, 2, 5, 2,
0, 7, 2, 2, 7, 0, 2, 7, 2, 0, 2, 7, 2, 0, 7, 6,
3, 6, 7, 7, 2, 2, 5, 5, 2, 1, 2, 2, 4, 2, 0, 5,
2, 2, 5, 0, 6, 7, 5, 5, 7, 5, 2, 3, 2, 2, 6, 2,
0, 0, 0, 0, 11, 0, 3, 0, 3, 7, 5, 7, 4, 7, 1, 5,
2, 5, 7, 7, 4, 0, 3, 0, 0, 7, 5, 5, 7, 0, 5, 0,
5, 2, 7, 2, 0, 12, 0, 2, 2, 2, 0, 4, 0, 4, 2, 1,
7, 5, 7, 5, 7, 4, 1, 7, 4, 5, 7, 2, 3, 2, 7, 4,
7, 1, 7, 6, 6, 1, 1, 7, 7, 7, 7, 3, 3, 7, 0, 5,
0, 0, 13, 0, 2, 5, 0, 5, 5, 5, 0, 5, 2, 1, 7, 3,
15, 9, 3, 6, 3, 6, 7, 7, 2, 2, 5, 5, 7, 7, 2, 7,
7, 5, 7, 3, 1, 7, 5, 6, 7, 2, 7, 5, 7, 5, 4, 4,
3, 7, 2, 5, 7, 4, 4, 7, 3, 7, 7, 1, 6, 7, 5, 7,
7, 3, 3, 7, 5, 3, 7, 3, 6, 4, 7, 1, 1, 7, 1, 7,
4, 7, 3, 4, 3, 7, 6, 7, 5, 2, 5, 1, 6, 1, 1, 7,
4, 0, 7, 5, 1, 2, 4, 5, 7, 0, 2, 1, 2, 8, 8, 8,
0, 3, 0, 6, 3, 6, 7, 7, 2, 2, 5, 5, 7, 5, 2, 7,
7, 5, 7, 3, 1, 7, 5, 6, 7, 2, 7, 5, 7, 5, 4, 4,
3, 7, 2, 5, 7, 4, 4, 7, 3, 7, 7, 1, 6, 7, 5, 7,
7, 3, 3, 7, 5, 3, 7, 3, 6, 4, 7, 1, 1, 7, 1, 7,
4, 7, 3, 4, 3, 7, 6, 7, 5, 2, 5, 1, 6, 1, 1, 7,
4, 2, 7, 5, 0, 7, 0, 5, 7, 2, 6, 2, 3, 7, 5, 7,
};

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

int gbMax( int a, int b ) { if( a > b ) return a; return b; }
int gbMin( int a, int b ) { if( a < b ) return a; return b; }
int gbAbsInt( int a ) { if( a < 0 ) return -a; return a; }
static float gbPow2( float e ) { return (float)exp( e * 0.69314718056 ); }

// -----------------------------------------------------------------------------
// Framebuffer + core drawing primitives
// -----------------------------------------------------------------------------

int gbFrameBuffer[ LCD_WIDTH * LCD_PAGES ];
int gbColor = 1;
bool gbRealGrayColor = false;
int gbGrayBuffer[ LCD_WIDTH * LCD_PAGES ];
bool gbAnyGrayDrawn = false;
int gbBgColor = 0;

void gbSetColor( int color )
{
    gbColor = color;
    gbBgColor = color;
}

void gbSetColorBg( int color, int bg )
{
    gbColor = color;
    gbBgColor = bg;
}

void gbDrawPixel( int x, int y )
{
    if( x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT ) return;

    int idx = x + ( y / 8 ) * LCD_WIDTH;
    int bit = 1 << ( y % 8 );

    if( gbColor == GB_INVERT )
    {
        gbFrameBuffer[ idx ] = gbFrameBuffer[ idx ] ^ bit;
    }
    else if( gbColor == GB_GRAY )
    {
        // Real Display::drawPixel()'s own checkerboard dither formula -
        // spatial (x&1)^(y&1) XORed against the low bit of the frame
        // counter, so the pattern also flips every other real tick.
        bool on = ( ( gbFrameCount & 1 ) != ( ( x & 1 ) ^ ( y & 1 ) ) );
        if( on )
          gbFrameBuffer[ idx ] = gbFrameBuffer[ idx ] | bit;
        else
          gbFrameBuffer[ idx ] = gbFrameBuffer[ idx ] & ( ~bit );

        if( gbRealGrayColor )
        {
            gbGrayBuffer[ idx ] = gbGrayBuffer[ idx ] | bit;
            gbAnyGrayDrawn = true;
        }
    }
    else
    {
        if( gbColor )
          gbFrameBuffer[ idx ] = gbFrameBuffer[ idx ] | bit;
        else
          gbFrameBuffer[ idx ] = gbFrameBuffer[ idx ] & ( ~bit );

        // Un-gray any pixel a previous GB_GRAY draw left here - see this
        // shim's own header comment on gbAnyGrayDrawn for why this is a
        // safe no-op (and costs nothing) until the first real gray draw
        // of a session ever happens.
        if( gbRealGrayColor && gbAnyGrayDrawn )
          gbGrayBuffer[ idx ] = gbGrayBuffer[ idx ] & ( ~bit );
    }
}

int gbGetPixel( int x, int y )
{
    if( x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT ) return 0;
    int idx = x + ( y / 8 ) * LCD_WIDTH;
    int bit = 1 << ( y % 8 );
    if( gbFrameBuffer[ idx ] & bit ) return 1;
    return 0;
}

void gbClear()
{
    int i;
    for( i = 0; i < LCD_WIDTH * LCD_PAGES; i++ )
    {
        gbFrameBuffer[ i ] = 0;
        gbGrayBuffer[ i ] = 0;
    }
    gbAnyGrayDrawn = false;
}

// Real Display::fillScreen() hardware bug, preserved deliberately: on real
// PCD8544 hardware this always fills solid BLACK regardless of what color
// was passed - confirmed directly against the real Display.cpp source, not
// assumed - so `color` here is a genuine, confirmed no-op.
void gbFillScreen( int color )
{
    int i;
    for( i = 0; i < LCD_WIDTH * LCD_PAGES; i++ )
      gbFrameBuffer[ i ] = 0xFF;
}

void gbDrawFastHLine( int x, int y, int w )
{
    int i;
    for( i = 0; i < w; i++ )
      gbDrawPixel( x + i, y );
}

void gbDrawFastVLine( int x, int y, int h )
{
    int i;
    for( i = 0; i < h; i++ )
      gbDrawPixel( x, y + i );
}

void gbDrawLine( int x0, int y0, int x1, int y1 )
{
    int dx = gbAbsInt( x1 - x0 );
    int dy = -gbAbsInt( y1 - y0 );
    int sx, sy, err, e2;

    if( x0 < x1 ) sx = 1; else sx = -1;
    if( y0 < y1 ) sy = 1; else sy = -1;
    err = dx + dy;

    while( true )
    {
        gbDrawPixel( x0, y0 );
        if( x0 == x1 && y0 == y1 ) break;
        e2 = 2 * err;
        if( e2 >= dy ) { err = err + dy; x0 = x0 + sx; }
        if( e2 <= dx ) { err = err + dx; y0 = y0 + sy; }
    }
}

void gbDrawRect( int x, int y, int w, int h )
{
    gbDrawFastHLine( x, y, w );
    gbDrawFastHLine( x, y + h - 1, w );
    gbDrawFastVLine( x, y, h );
    gbDrawFastVLine( x + w - 1, y, h );
}

void gbFillRect( int x, int y, int w, int h )
{
    int i;
    for( i = 0; i < w; i++ )
      gbDrawFastVLine( x + i, y, h );
}

void gbDrawCircle( int x0, int y0, int r )
{
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    gbDrawPixel( x0, y0 + r );
    gbDrawPixel( x0, y0 - r );
    gbDrawPixel( x0 + r, y0 );
    gbDrawPixel( x0 - r, y0 );

    while( x < y )
    {
        if( f >= 0 ) { y = y - 1; ddF_y = ddF_y + 2; f = f + ddF_y; }
        x = x + 1;
        ddF_x = ddF_x + 2;
        f = f + ddF_x;

        gbDrawPixel( x0 + x, y0 + y );
        gbDrawPixel( x0 - x, y0 + y );
        gbDrawPixel( x0 + x, y0 - y );
        gbDrawPixel( x0 - x, y0 - y );
        gbDrawPixel( x0 + y, y0 + x );
        gbDrawPixel( x0 - y, y0 + x );
        gbDrawPixel( x0 + y, y0 - x );
        gbDrawPixel( x0 - y, y0 - x );
    }
}

void gbFillCircle( int x0, int y0, int r )
{
    int x, y;
    for( y = -r; y <= r; y++ )
      for( x = -r; x <= r; x++ )
        if( x * x + y * y <= r * r )
          gbDrawPixel( x0 + x, y0 + y );
}

static void gbDrawCircleHelper( int x0, int y0, int r, int cornername )
{
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    while( x < y )
    {
        if( f >= 0 ) { y = y - 1; ddF_y = ddF_y + 2; f = f + ddF_y; }
        x = x + 1;
        ddF_x = ddF_x + 2;
        f = f + ddF_x;

        if( cornername & 0x4 ) { gbDrawPixel( x0 + x, y0 + y ); gbDrawPixel( x0 + y, y0 + x ); }
        if( cornername & 0x2 ) { gbDrawPixel( x0 + x, y0 - y ); gbDrawPixel( x0 + y, y0 - x ); }
        if( cornername & 0x8 ) { gbDrawPixel( x0 - y, y0 + x ); gbDrawPixel( x0 - x, y0 + y ); }
        if( cornername & 0x1 ) { gbDrawPixel( x0 - y, y0 - x ); gbDrawPixel( x0 - x, y0 - y ); }
    }
}

static void gbFillCircleHelper( int x0, int y0, int r, int corners, int delta )
{
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    while( x < y )
    {
        if( f >= 0 ) { y = y - 1; ddF_y = ddF_y + 2; f = f + ddF_y; }
        x = x + 1;
        ddF_x = ddF_x + 2;
        f = f + ddF_x;

        if( corners & 0x1 )
        {
            gbDrawFastVLine( x0 + x, y0 - y, 2 * y + 1 + delta );
            gbDrawFastVLine( x0 + y, y0 - x, 2 * x + 1 + delta );
        }
        if( corners & 0x2 )
        {
            gbDrawFastVLine( x0 - x, y0 - y, 2 * y + 1 + delta );
            gbDrawFastVLine( x0 - y, y0 - x, 2 * x + 1 + delta );
        }
    }
}

void gbDrawRoundRect( int x, int y, int w, int h, int r )
{
    gbDrawFastHLine( x + r, y, w - 2 * r );
    gbDrawFastHLine( x + r, y + h - 1, w - 2 * r );
    gbDrawFastVLine( x, y + r, h - 2 * r );
    gbDrawFastVLine( x + w - 1, y + r, h - 2 * r );

    gbDrawCircleHelper( x + r, y + r, r, 1 );
    gbDrawCircleHelper( x + w - r - 1, y + r, r, 2 );
    gbDrawCircleHelper( x + w - r - 1, y + h - r - 1, r, 4 );
    gbDrawCircleHelper( x + r, y + h - r - 1, r, 8 );
}

void gbFillRoundRect( int x, int y, int w, int h, int r )
{
    gbFillRect( x + r, y, w - 2 * r, h );
    gbFillCircleHelper( x + w - r - 1, y + r, r, 1, h - 2 * r - 1 );
    gbFillCircleHelper( x + r, y + r, r, 2, h - 2 * r - 1 );
}

static void gbSwapInt( int* a, int* b ) { int t = *a; *a = *b; *b = t; }

void gbDrawTriangle( int x0, int y0, int x1, int y1, int x2, int y2 )
{
    gbDrawLine( x0, y0, x1, y1 );
    gbDrawLine( x1, y1, x2, y2 );
    gbDrawLine( x2, y2, x0, y0 );
}

void gbFillTriangle( int x0, int y0, int x1, int y1, int x2, int y2 )
{
    int a, b, y, last;
    int dx01, dy01, dx02, dy02, dx12, dy12;
    int sa = 0, sb = 0;

    if( y0 > y1 ) { gbSwapInt( &y0, &y1 ); gbSwapInt( &x0, &x1 ); }
    if( y1 > y2 ) { gbSwapInt( &y2, &y1 ); gbSwapInt( &x2, &x1 ); }
    if( y0 > y1 ) { gbSwapInt( &y0, &y1 ); gbSwapInt( &x0, &x1 ); }

    if( y0 == y2 )
    {
        a = x0; b = x0;
        if( x1 < a ) a = x1; else if( x1 > b ) b = x1;
        if( x2 < a ) a = x2; else if( x2 > b ) b = x2;
        gbDrawFastHLine( a, y0, b - a + 1 );
        return;
    }

    dx01 = x1 - x0; dy01 = y1 - y0;
    dx02 = x2 - x0; dy02 = y2 - y0;
    dx12 = x2 - x1; dy12 = y2 - y1;

    if( y1 == y2 ) last = y1; else last = y1 - 1;

    for( y = y0; y <= last; y++ )
    {
        a = x0 + sa / dy01;
        b = x0 + sb / dy02;
        sa = sa + dx01;
        sb = sb + dx02;
        if( a > b ) gbSwapInt( &a, &b );
        gbDrawFastHLine( a, y, b - a + 1 );
    }

    sa = dx12 * ( y - y1 );
    sb = dx02 * ( y - y0 );
    for( ; y <= y2; y++ )
    {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa = sa + dx12;
        sb = sb + dx02;
        if( a > b ) gbSwapInt( &a, &b );
        gbDrawFastHLine( a, y, b - a + 1 );
    }
}

// -----------------------------------------------------------------------------
// Bitmaps - direct port of real Display::drawBitmap() (see this shim's own
// header comment / gbDrawBitmap()'s doc comment in gamebuinoShim.h for the
// real byte layout: bitmap[0]/[1] = width/height, then row-major
// ceil(width/8)-bytes-per-row MSB-first packed rows).
// -----------------------------------------------------------------------------

void gbDrawBitmap( int x, int y, int* bitmap )
{
    int w = bitmap[ 0 ];
    int h = bitmap[ 1 ];
    int byteWidth = ( w + 7 ) / 8;
    int row, col;

    for( row = 0; row < h; row++ )
    {
        for( col = 0; col < w; col++ )
        {
            int byteIndex = 2 + row * byteWidth + ( col / 8 );
            if( bitmap[ byteIndex ] & ( 0x80 >> ( col % 8 ) ) )
              gbDrawPixel( x + col, y + row );
        }
    }
}

// Direct port of real Display::drawBitmap(x,y,bitmap,rotation,flip) -
// preserves real hardware's own exact quirks (flip uses the bitmap's
// ORIGINAL pre-rotation width/height; vertical flip mirrors via `h-l`, not
// `h-l-1`, asymmetric with the horizontal case's own `w-k-1`) - see this
// function's own doc comment in gamebuinoShim.h.
void gbDrawBitmapRotated( int x, int y, int* bitmap, int rotation, int flip )
{
    int w = bitmap[ 0 ];
    int h = bitmap[ 1 ];
    int byteWidth = ( w + 7 ) / 8;
    int row, col, k, l;

    for( row = 0; row < h; row++ )
    {
        for( col = 0; col < w; col++ )
        {
            int byteIndex = 2 + row * byteWidth + ( col / 8 );
            bool set = ( bitmap[ byteIndex ] & ( 0x80 >> ( col % 8 ) ) ) != 0;
            if( !set ) continue;

            k = col;
            l = row;

            if( flip == 1 || flip == 3 ) k = w - k - 1;
            if( flip == 2 || flip == 3 ) l = h - l;

            if( rotation == 0 )
              gbDrawPixel( x + k, y + l );
            else if( rotation == 1 )
              gbDrawPixel( x + l, y + ( w - k - 1 ) );
            else if( rotation == 2 )
              gbDrawPixel( x + ( w - k - 1 ), y + ( h - l - 1 ) );
            else
              gbDrawPixel( x + ( h - l - 1 ), y + k );
        }
    }
}

// -----------------------------------------------------------------------------
// Text
// -----------------------------------------------------------------------------

int gbCursorX = 0, gbCursorY = 0, gbFontSize = 1, gbFontWidth = 4, gbFontHeight = 6;
int* gbFontPtr;

void gbSetFont( int* font )
{
    gbFontPtr = font;
    gbFontWidth = font[ 0 ] + 1;
    gbFontHeight = font[ 1 ] + 1;
}

static void gbDrawCharPixel( int x, int y, int col, int row )
{
    gbDrawPixel( x + col * gbFontSize, y + row * gbFontSize );
    if( gbFontSize == 2 )
    {
        gbDrawPixel( x + col * gbFontSize + 1, y + row * gbFontSize );
        gbDrawPixel( x + col * gbFontSize, y + row * gbFontSize + 1 );
        gbDrawPixel( x + col * gbFontSize + 1, y + row * gbFontSize + 1 );
    }
}

void gbDrawChar( int ch, int x, int y )
{
    int glyphW = gbFontPtr[ 0 ];
    int glyphH = gbFontPtr[ 1 ];
    int byteHeight = ( glyphH + 7 ) / 8;
    int col, bRow, bit, row;
    int savedColor = gbColor;

    for( col = 0; col < glyphW; col++ )
    {
        for( bRow = 0; bRow < byteHeight; bRow++ )
        {
            int b = gbFontPtr[ 2 + ch * glyphW * byteHeight + col * byteHeight + bRow ];
            for( bit = 0; bit < 8; bit++ )
            {
                row = bRow * 8 + bit;
                if( row >= glyphH ) break;

                if( b & ( 1 << bit ) )
                {
                    gbColor = savedColor;
                    gbDrawCharPixel( x, y, col, row );
                }
                else if( gbBgColor != savedColor )
                {
                    gbColor = gbBgColor;
                    gbDrawCharPixel( x, y, col, row );
                }
            }
        }
    }
    gbColor = savedColor;
}

void gbPrintString( char* text )
{
    int i, x, y;
    x = gbCursorX;
    y = gbCursorY;

    for( i = 0; text[ i ] != 0; i++ )
    {
        if( text[ i ] == '\n' )
        {
            // Real hardware's own Display::write() resets the cursor to the
            // left margin on '\n' (gbCursorX = 0), not back to whatever
            // gbCursorX happened to hold BEFORE this gbPrintString() call
            // started - a real, confirmed bug found via a direct user
            // report (Dark Tower's own action menu drawing garbled/off-
            // screen text after wrapping mid-item onto a new line, not
            // reproducible on the sibling Vircon32 build). This function
            // only writes gbCursorX/Y back to the real globals once, after
            // the whole string finishes (unlike the Vircon32 sibling's own
            // per-character-committed version), so re-reading the stale
            // global here silently carried the PREVIOUS line's own X
            // position forward instead of returning to 0, drifting the
            // cursor further right (eventually off-screen) with every
            // wrap. Fixed to the real, correct constant.
            x = 0;
            y = y + gbFontHeight * gbFontSize;
            continue;
        }

        gbDrawChar( (unsigned char)text[ i ], x, y );
        x = x + gbFontWidth * gbFontSize;
    }

    gbCursorX = x;
    gbCursorY = y;
}

void gbPrintNumber( int value )
{
    char numText[ 16 ];
    itoa( value, numText, 10 );
    gbPrintString( numText );
}

// Direct port of real Arduino Print::printFloat() - see this function's own
// doc comment in gamebuinoShim.h.
void gbPrintFloat( float value, int decimals )
{
    float rounding;
    int i, intPart, digit;
    float remainder;

    if( value < 0 )
    {
        gbPrintString( "-" );
        value = -value;
    }

    rounding = 0.5;
    for( i = 0; i < decimals; i = i + 1 )
      rounding = rounding / 10.0;
    value = value + rounding;

    intPart = (int)value;
    remainder = value - (float)intPart;
    gbPrintNumber( intPart );

    if( decimals > 0 )
      gbPrintString( "." );

    for( i = 0; i < decimals; i = i + 1 )
    {
        remainder = remainder * 10.0;
        digit = (int)remainder;
        gbPrintNumber( digit );
        remainder = remainder - digit;
    }
}

// -----------------------------------------------------------------------------
// Popup - direct port of real Gamebuino::popup()/updatePopup() (see this
// primitive's own doc comment in gamebuinoShim.h). gbUpdatePopup() is
// called automatically from gbRenderFrame() below, never by game code
// directly - matching real hardware's own automatic call from inside
// Gamebuino::update() itself.
// -----------------------------------------------------------------------------

char* gbPopupText;
int gbPopupTimeLeft = 0;

void gbPopup( char* text, int duration )
{
    gbPopupText = text;
    gbPopupTimeLeft = duration + 12;
}

void gbUpdatePopup()
{
    if( gbPopupTimeLeft > 0 )
    {
        int yOffset = 0;
        if( gbPopupTimeLeft < 12 )
          yOffset = 12 - gbPopupTimeLeft;

        gbFontSize = 1;
        gbSetColor( 0 ); // WHITE
        gbFillRoundRect( 0, LCD_HEIGHT - gbFontHeight + yOffset - 3, 84, gbFontHeight + 3, 3 );
        gbSetColor( 1 ); // BLACK
        gbDrawRoundRect( 0, LCD_HEIGHT - gbFontHeight + yOffset - 3, 84, gbFontHeight + 3, 3 );
        gbCursorX = 4;
        gbCursorY = LCD_HEIGHT - gbFontHeight + yOffset - 1;
        gbPrintString( gbPopupText );
        gbPopupTimeLeft = gbPopupTimeLeft - 1;
    }
}

// -----------------------------------------------------------------------------
// Buttons
// -----------------------------------------------------------------------------

int gbBtnHeld[ 7 ];
int gbBtnPrevHeld[ 7 ];

static int gbButtonLevel( int button )
{
    if( button == BTN_UP ) return md_inputUp();
    if( button == BTN_DOWN ) return md_inputDown();
    if( button == BTN_LEFT ) return md_inputLeft();
    if( button == BTN_RIGHT ) return md_inputRight();
    if( button == BTN_A ) return md_inputA();
    if( button == BTN_B ) return md_inputB();
    if( button == BTN_C ) return md_inputC();
    return 0;
}

static void gbUpdateButtons()
{
    int i;
    for( i = 0; i < 7; i++ )
    {
        gbBtnPrevHeld[ i ] = gbBtnHeld[ i ];
        if( gbButtonLevel( i ) )
          gbBtnHeld[ i ] = gbBtnHeld[ i ] + 1;
        else
          gbBtnHeld[ i ] = 0;
    }
}

bool gbPressed( int button ) { return gbBtnHeld[ button ] == 1; }
bool gbReleased( int button ) { return gbBtnHeld[ button ] == 0 && gbBtnPrevHeld[ button ] > 0; }
bool gbHeld( int button, int frames ) { return gbBtnHeld[ button ] >= frames; }
// Direct port of real Buttons::timeHeld() - a plain passthrough of the same
// per-tick hold counter gbHeld()/gbPressed() already maintain internally.
int gbTimeHeld( int button ) { return gbBtnHeld[ button ]; }

// Direct port of real Buttons::repeat(button, period) - see this function's
// own extensive doc comment in the sibling Vircon32 build's own
// gamebuinoShim.c for the full real-hardware-vs-earlier-shim-bug history;
// this port already starts from the corrected version. Real period<=1
// means "fire on every single held frame"; for period>1, real hardware's
// own exact modulo target is 1, not 0.
bool gbRepeat( int button, int period )
{
    if( period <= 1 )
      return gbBtnHeld[ button ] >= 1;

    return ( gbBtnHeld[ button ] % period ) == 1;
}

// -----------------------------------------------------------------------------
// Core / lifecycle
// -----------------------------------------------------------------------------

// 20, not 30 - matches real Gamebuino Classic's own out-of-the-box default
// (Gamebuino::begin() sets `timePerFrame = 50` directly, i.e. 1000/50 =
// 20fps) - see this shim's own header comment / gbBegin()'s doc comment in
// gamebuinoShim.h.
int gbFrameRateFps = 20;
int gbTickAccum = 0;

// The real engine tick rate gbUpdate()'s own accumulator sub-samples
// against - defaults to MD_FRAMES_PER_SECOND (60), matching every port's
// own historical behavior exactly (SDL2/SDL3 genuinely do tick their own
// dispatch loop at a fixed, reliable 60Hz, vsync/timer-locked - see each
// port's own main.c). NOT reset by gbBegin() - unlike gbFrameRateFps (a
// per-game, per-session request), this is a port-level fact about how
// often that port's own top-level dispatch loop actually calls into this
// shim at all, which doesn't change just because a new game launched.
//
// A port whose own real callback rate can't reliably hit 60Hz (Playdate's
// own hardware display refresh caps at a real 50fps ceiling - see
// src/playdate/main.c's own header comment) calls gbSetEngineFrameRate()
// once, at startup and again any time it retargets its own callback rate
// to match a game's current gbFrameRateFps request, so this accumulator's
// own math stays correct against whatever rate is actually driving it -
// see gbSetEngineFrameRate()'s own doc comment in gamebuinoShim.h.
int gbEngineFrameRate = MD_FRAMES_PER_SECOND;

// Real Gamebuino::frameCount - reset to 0 by gbBegin() (a real, considered
// adaptation for this project's own multi-game-per-session model - see
// gbBegin()'s own doc comment in gamebuinoShim.h).
int gbFrameCount = 0;

void gbBegin()
{
    gbFrameRateFps = 20;
    gbTickAccum = 0;
    gbFrameCount = 0;
    gbPopupTimeLeft = 0;
    gbClear();
    gbCursorX = 0;
    gbCursorY = 0;
    gbFontSize = 1;
    gbSetFont( gbFont3x5 ); // real Display::Display()'s own default font
    gbColor = 1;
}

// Real Gamebuino::setFrameRate(uint8_t fps) has no explicit clamp of its
// own beyond its parameter's real 8-bit range - but this shim's own engine
// tick rate (gbEngineFrameRate - see its own doc comment above) is fixed
// for the whole session by whichever port is actually running, so
// requesting anything above that can never mean anything more than
// requesting exactly that - clamped to [1, gbEngineFrameRate] for that
// reason, a shim-specific gate real hardware has no equivalent need for.
void gbSetFrameRate( int fps )
{
    if( fps < 1 ) fps = 1;
    if( fps > gbEngineFrameRate ) fps = gbEngineFrameRate;
    gbFrameRateFps = fps;
}

// A plain read accessor for gbFrameRateFps - the natural counterpart to
// gbSetFrameRate(), needed by a port that wants to react to a game's own
// current requested frame rate (see src/playdate/main.c's own use of this
// to keep pd->display->setRefreshRate() in sync with it) without needing
// its own separate, possibly-drifting copy of the same value.
int gbGetFrameRate()
{
    return gbFrameRateFps;
}

// Port-level primitive - NOT meant to be called by a game itself (no
// shipped game calls this, and none should). Retargets gbUpdate()'s own
// accumulator to sub-sample against `hz` instead of the default
// MD_FRAMES_PER_SECOND - see gbEngineFrameRate's own doc comment above for
// the full reasoning. gbTickAccum is reset to 0 on a change, so a rate
// change never leaves a stale accumulator value computed against the old
// reference rate around to produce one wrong/delayed tick immediately
// afterward.
void gbSetEngineFrameRate( int hz )
{
    if( hz < 1 ) hz = 1;
    gbEngineFrameRate = hz;
    gbTickAccum = 0;
}

void gbPickRandomSeed()
{
    // no-op - see gamebuinoShim.h's own header comment
}

// Whole-tick throttle to the configured frame rate (Bresenham-style
// accumulator) - see this function's own doc comment in gamebuinoShim.h.
bool gbUpdate()
{
    gbTickAccum = gbTickAccum + gbFrameRateFps;
    if( gbTickAccum < gbEngineFrameRate )
      return false;
    gbTickAccum = gbTickAccum - gbEngineFrameRate;

    gbUpdateButtons();
    gbClear();
    gbCursorX = 0;
    gbCursorY = 0;
    gbFrameCount = gbFrameCount + 1;

    // Real Gamebuino::update() resets color/bgcolor to (BLACK,WHITE) at the
    // tail of every single frame, via its own automatic displayBattery()
    // call - see this shim's own header comment / gbUpdate()'s doc comment
    // in the sibling Vircon32 build's own gamebuinoShim.c for the full
    // real-source citation. A game that wants transparent text still needs
    // to call gbSetColor(color) itself each frame it draws that way,
    // exactly like real hardware silently requires.
    gbColor = 1;
    gbBgColor = 0;

    return true;
}

// Streams the framebuffer to the real screen's backing surface via
// md_drawColumn() (and, for any real GB_GRAY content, a second targeted
// md_drawColumnGray() pass) - call once, at the very end of a game's own
// update function, after all of that tick's drawing is done.
//
// Deliberately does NOT call md_endFrame() itself - every port's own
// top-level per-real-frame loop (main.c on SDL2/SDL3, update() on
// Playdate) already calls md_endFrame() exactly once, unconditionally,
// after that real frame's dispatch is done, regardless of whether this
// particular tick's gbUpdate() gate actually ran a game logic tick at all
// (md_endFrame() itself is written to gracefully re-present the backing
// surface's already-persistent last content on a tick with nothing new to
// draw - see e.g. sdlBackend.c's own md_endFrame() doc comment). A second,
// real present from inside gbRenderFrame() itself would be a genuine
// duplicate - on a vsync-locked port this is not just wasted work but a
// second real vsync-blocking stall per logic tick, which compounds badly
// with the fixed-timestep accumulator driving that same outer loop: the
// extra measured wall-clock cost per outer iteration inflates the very
// elapsed-time sample that accumulator feeds on, queuing up even more
// catch-up ticks (and therefore even more duplicate presents) next
// iteration - a real, self-reinforcing slowdown, worse the more often a
// game's own requested frame rate makes this gate fire true.
void gbRenderFrame()
{
    gbUpdatePopup(); // real hardware's own automatic tail-of-update() call - see gbPopup()'s own doc comment
    md_beginFrame();
    int col, page, value;
    for( page = 0; page < LCD_PAGES; page++ )
      for( col = 0; col < LCD_WIDTH; col++ )
      {
        value = gbFrameBuffer[ col + page * LCD_WIDTH ] & 0xFF;
        if( value != 0 )
          md_drawColumn( col, page, value );
      }

    if( gbAnyGrayDrawn )
    {
        int grayValue;
        for( page = 0; page < LCD_PAGES; page++ )
          for( col = 0; col < LCD_WIDTH; col++ )
          {
            grayValue = gbGrayBuffer[ col + page * LCD_WIDTH ] & 0xFF;
            if( grayValue != 0 )
              md_drawColumnGray( col, page, grayValue );
          }
    }
}

// -----------------------------------------------------------------------------
// Sound - a small set of representative tones standing in for real
// Gamebuino's own tracker/pattern sound engine - see this shim's own header
// comment. playNote()'s own pitch->frequency formula matches real
// Sound.cpp's own noteToFrequency table (pitch 45 = A4/440Hz, 12
// semitones/octave, equal temperament).
// -----------------------------------------------------------------------------

void gbPlayNote( int pitch, int duration )
{
    float freq = 440.0 * gbPow2( ( (float)pitch - 45.0 ) / 12.0 );
    float durationSeconds = (float)duration / (float)gbFrameRateFps;
    md_playTone( freq, durationSeconds );
}

void gbPlayTick()
{
    md_playTone( 1800.0, 0.02 );
}

void gbPlayOK()
{
    md_playTone( 900.0, 0.05 );
    md_playTone( 1400.0, 0.08 );
}

void gbPlayCancel()
{
    md_playTone( 700.0, 0.05 );
    md_playTone( 400.0, 0.08 );
}

// -----------------------------------------------------------------------------
// Collision helpers - direct ports of Gamebuino::collide*()
// -----------------------------------------------------------------------------

bool gbCollidePointRect( int x1, int y1, int x2, int y2, int w, int h )
{
    return ( x1 >= x2 ) && ( x1 < x2 + w ) && ( y1 >= y2 ) && ( y1 < y2 + h );
}

bool gbCollideRectRect( int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2 )
{
    return ( x1 + w1 > x2 ) && ( x1 < x2 + w2 ) && ( y1 + h1 > y2 ) && ( y1 < y2 + h2 );
}

// Direct port of real Display::getBitmapPixel().
bool gbGetBitmapPixel( int* bitmap, int x, int y )
{
    int w = bitmap[ 0 ];
    int byteWidth = ( w + 7 ) / 8;
    return ( bitmap[ 2 + y * byteWidth + ( x / 8 ) ] & ( 0x80 >> ( x % 8 ) ) ) != 0;
}

// Direct port of real Gamebuino::collideBitmapBitmap() - same bounding-
// rect-reject-first optimization, same per-pixel AND-of-both-bitmaps
// overlap test over just the overlapping sub-rectangle.
bool gbCollideBitmapBitmap( int x1, int y1, int* b1, int x2, int y2, int* b2 )
{
    int w1 = b1[ 0 ];
    int h1 = b1[ 1 ];
    int w2 = b2[ 0 ];
    int h2 = b2[ 1 ];
    int xmin, ymin, xmax, ymax, x, y;

    if( !gbCollideRectRect( x1, y1, w1, h1, x2, y2, w2, h2 ) )
      return false;

    if( x1 >= x2 ) xmin = 0; else xmin = x2 - x1;
    if( y1 >= y2 ) ymin = 0; else ymin = y2 - y1;
    if( x1 + w1 >= x2 + w2 ) xmax = x2 + w2 - x1; else xmax = w1;
    if( y1 + h1 >= y2 + h2 ) ymax = y2 + h2 - y1; else ymax = h1;

    for( y = ymin; y < ymax; y++ )
    {
        for( x = xmin; x < xmax; x++ )
        {
            if( gbGetBitmapPixel( b1, x, y ) && gbGetBitmapPixel( b2, x1 + x - x2, y1 + y - y2 ) )
              return true;
        }
    }
    return false;
}
