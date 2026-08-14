#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Snake Classic (real original author Ripper121; this port's own real
// upstream source is Tnxec2's own fork of it, license: None specified -
// github.com/Tnxec2/snake-gamebuino-classic). A real, complete Snake clone
// for Gamebuino Classic - move the snake around the 84x48 screen (wrapping
// at every edge, not dying on them), eating one food item at a time (a
// "mouse" or an "apple", worth 2 or 1 points respectively) to grow; running
// into your own body ends the round. Confirmed a direct fork, not just a
// similar game, by diffing Tnxec2's own real source directly against
// Ripper121's own real original (recovered separately and compared
// byte-for-byte) - every sprite/struct/function body is identical;
// Tnxec2's own real changes are a relative left/right-turn steering scheme
// (replacing Ripper121's own absolute-direction buttons), Button
// A/B for pause instead of Button C (C instead returns to the title
// screen), and an added popup/pause-banner UI polish - Tnxec2's own fork
// even still shows Ripper121's own real, unchanged
// `gb.titleScreen(F("Snake by Ripper121"), logo)` string, the detail that
// gave the real provenance away.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(min, max)` became
// `arand(max - min)` (this dialect's own established RNG helper).
// Upstream's own bare globals/types (`snake`, `food`, `direction`, `TOP`/
// `RIGHT`/`BOTTOM`/`LEFT`, `Snake[]`, `Food[]`, `makesnake()`, etc) were all
// given an `snkc`-prefixed name, since Vircon32 has no linker and every game
// in this single compiled cartridge shares one flat global namespace -
// important here specifically because a second, separate Snake port
// (SnakeAbcBuino, prefix `sabc`) is planned for this same cartridge too.
//
// Upstream's own blocking `gb.titleScreen(F("Snake by Ripper121"), logo)`
// (shown once from setup(), and re-entered as a "pause and show the title
// again" gesture whenever Button C is pressed mid-game) was converted into
// an explicit SNKC_STATE_TITLE state, matching gamePong.c's/
// gameAgaruino.c's own "blocking loop -> explicit resumable state"
// treatment; the real 64x30 splash-logo bitmap is drawn via this shim's own
// `gbDrawBitmap()` primitive, alongside the "PRESS A" prompt. Upstream's own
// blocking "Game Over" screen (drawn directly to the display, followed by a
// hardcoded `delay(3000)` before silently reinitializing and resuming) was
// similarly converted into an explicit SNKC_STATE_GAMEOVER state that now
// waits for a genuine fresh Button A press to restart, rather than an
// unskippable 3-second wall-clock delay - matching this dialect's complete
// lack of any `delay()`-equivalent blocking primitive in the first place.
//
// Upstream's own head/body/food sprites (headU/headD/headL/headR, bodyH/
// bodyV, mouse, apple - each a small hand-drawn PROGMEM bitmap, blitted via
// `gb.display.drawBitmap()`) plus its own 64x30 title-screen `logo` bitmap
// are all drawn using this shim's own `gbDrawBitmap()` primitive. Upstream's
// own bitmap arrays use Arduino's
// `B00010000`-style binary literal syntax, not valid in this dialect - each
// byte below was mechanically converted to decimal (via a small script
// parsing the real .ino source directly, not hand arithmetic) and
// spot-checked by hand against its real upstream `B...` literal (e.g.
// `B00100000` = 32, `B01111111` = 127 - both confirmed bit-by-bit).
// Upstream draws every sprite at its own segment's raw `x`/`y` with no
// centering adjustment even though the sprites are not all exactly
// SNKC_SEG_SIZE(4) square (heads are 5x3/3x5, body is 3x3) - preserved
// here exactly the same way (draw at the segment's own x/y, unadjusted)
// rather than inventing new centering upstream never had. Upstream's own
// body-sprite selection is also simpler than a first glance at `bodyH`/
// `bodyV`'s names suggests: `drawSnake()` picks the body sprite from the
// snake's single current *global* `direction` (TOP/BOTTOM -> bodyV,
// LEFT/RIGHT -> bodyH) for every non-head segment, not per-segment
// neighbor-relative orientation - and `bodyH`/`bodyV`'s own real pixel
// data is actually byte-for-byte identical (both the same plus-shaped 3x3
// glyph), a genuine upstream quirk preserved rather than "fixed" into
// something visually different from real hardware. Upstream's
// own unused `popup()`/`updatePopup()`/`printCentered(char*)` functions
// (defined but never actually called anywhere in the real source) were
// dropped as dead code; the "PAUSE" banner (the one real on-screen use of
// `printCentered()`) was kept, ported as a small local `snkcCenterX()`
// helper instead. Upstream never calls into `gb.sound` at all (no note/
// tick/OK/cancel calls anywhere in the real source) - this port stays
// silent too, faithfully matching that rather than inventing sound cues
// upstream never had. Upstream also never persists a high score via EEPROM
// (no `EEPROM.*` calls anywhere in the real source) - so none was added
// here either, per this project's own "don't invent gameplay behavior not
// in the upstream source" rule.
//
// Three real upstream quirks found while reading the source closely, all
// preserved deliberately rather than "fixed":
// - **Only Left/Right steer - Up/Down do nothing.** `direction` is a single
//   TOP/RIGHT/BOTTOM/LEFT value that only ever rotates one step at a time,
//   and only in response to BTN_LEFT (rotate counter-clockwise through the
//   cycle) / BTN_RIGHT (rotate clockwise) - BTN_UP/BTN_DOWN are never read
//   for movement at all. Surprising for a Snake clone (most use all four
//   d-pad directions directly) but this is genuinely how the real game
//   plays on real hardware, not a porting mistake - preserved exactly
//   (SNKC_DIR_TOP/RIGHT/BOTTOM/LEFT below, rotated the same way).
// - **The starting position's axes are crossed.** `makesnake()`'s own calc
//   computes `calcH` from LCDHEIGHT and assigns it to the head's X, and
//   `calcW` from LCDWIDTH and assigns it to the head's Y - backwards from
//   what "start centered" would normally compute. Since LCDWIDTH(84) !=
//   LCDHEIGHT(48) this is NOT a no-impact swap (unlike e.g. Agaruino's own
//   vy/vx typo, preserved for the same "genuinely how it plays" reason) -
//   it produces a real, visibly off-center starting position (using this
//   shim's own 4px segment size: x=24, y=40, i.e. close to the bottom edge
//   rather than screen center). Kept exactly as upstream wrote it below in
//   `snkcMakeSnake()`.
// - Upstream's own `w`/`h` fields on the snake's head segment are always
//   equal (both hardcoded to 4) and its own movement/wrap code inconsistently
//   mixes which of the two it reads on which axis (e.g. wrapping X against
//   `Snake[0].h` rather than `Snake[0].w`) - genuinely a no-impact mixup
//   since the two values never actually differ, so this port drops the
//   separate w/h fields entirely and uses one shared `SNKC_SEG_SIZE`
//   constant throughout instead (matching Agaruino's own precedent for
//   silently normalizing an obvious no-impact typo rather than faithfully
//   reproducing which specific field name was misread where).

#define SNKC_SEG_SIZE 4
#define SNKC_MAX_LENGTH 200
#define SNKC_START_LENGTH 2
#define SNKC_TICKS_PER_STEP 4 // 200ms upstream step delay / 50ms per tick at this shim's default 20fps

#define SNKC_DIR_TOP    0
#define SNKC_DIR_RIGHT  1
#define SNKC_DIR_BOTTOM 2
#define SNKC_DIR_LEFT   3

#define SNKC_FOOD_TYPES 2
#define SNKC_FOOD_MOUSE 0
#define SNKC_FOOD_APPLE 1

// Real upstream sprite bitmaps, copied byte-for-byte from SNAKE.ino's own
// `B`-binary-literal PROGMEM arrays and converted to decimal (see this
// file's own header comment). Each is shaped { width, height, byte0, ... },
// one plain int per original byte, matching gbDrawBitmap()'s documented
// format exactly.
int snkcLogoBitmap[242]= { 64, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 0, 0, 0, 8, 0, 112, 0, 120, 0, 60, 28, 56, 17, 240, 0, 248, 56, 124, 124, 120, 59, 240, 1, 252, 248, 124, 252, 248, 127, 240, 7, 252, 252, 253, 252, 252, 127, 192, 15, 248, 252, 249, 254, 125, 255, 128, 31, 225, 254, 251, 254, 61, 254, 0, 31, 129, 254, 251, 222, 255, 238, 0, 31, 1, 254, 251, 223, 255, 222, 224, 62, 30, 239, 123, 207, 255, 143, 224, 63, 255, 239, 255, 255, 255, 207, 128, 63, 255, 231, 255, 255, 191, 231, 184, 31, 31, 227, 247, 255, 191, 247, 240, 0, 62, 225, 247, 135, 190, 243, 240, 0, 255, 225, 207, 131, 30, 99, 192, 3, 255, 224, 31, 0, 0, 67, 128, 31, 251, 0, 6, 0, 0, 0, 0, 31, 248, 0, 0, 0, 0, 0, 0, 15, 224, 0, 0, 3, 252, 240, 0, 15, 0, 0, 0, 31, 255, 248, 0, 4, 0, 15, 255, 255, 223, 248, 0, 0, 1, 255, 255, 255, 223, 120, 0, 0, 7, 255, 255, 255, 255, 240, 0, 0, 31, 255, 255, 255, 255, 255, 0, 0, 63, 255, 255, 255, 255, 255, 192, 0, 255, 252, 0, 127, 255, 255, 224, 0, 255, 0, 0, 7, 223, 255, 224, 1, 254, 63, 240, 3, 235, 255, 224, 1, 255, 255, 255, 241, 251, 253, 96 };

int snkcHeadLBitmap[5]= { 5, 3, 32, 88, 240 };
int snkcHeadRBitmap[5]= { 5, 3, 32, 208, 120 };
int snkcHeadUBitmap[7]= { 3, 5, 128, 192, 160, 192, 64 };
int snkcHeadDBitmap[7]= { 3, 5, 64, 192, 160, 192, 128 };

// bodyH/bodyV are genuinely byte-for-byte identical in the real upstream
// source (both the same plus-shaped 3x3 glyph) - see this file's own
// header comment for why that's preserved rather than "fixed".
int snkcBodyHBitmap[5]= { 3, 3, 64, 224, 64 };
int snkcBodyVBitmap[5]= { 3, 3, 64, 224, 64 };

int snkcMouseBitmap[6]= { 8, 4, 12, 154, 190, 127 };
int snkcAppleBitmap[10]= { 8, 8, 14, 16, 126, 153, 129, 129, 102, 24 };

enum SnkcState
{
    SNKC_STATE_TITLE = 0,
    SNKC_STATE_PLAY = 1,
    SNKC_STATE_GAMEOVER = 2
};

typedef struct
{
    int x;
    int y;
} SnkcSegment;

typedef struct
{
    int x;
    int y;
    int w;
    int h;
    int points;
    int type;
    bool exist;
} SnkcFood;

int snkcState;
int snkcDirection;
int snkcLength;
bool snkcPaused;
int snkcStepCounter;

SnkcSegment snkcSnake[SNKC_MAX_LENGTH];
SnkcFood snkcFood[SNKC_FOOD_TYPES];

// Assumes fontSize 1 (8px-wide glyphs) - every screen that uses this keeps
// gbFontSize at its own default of 1 the whole time.
int snkcCenterX( int textLen )
{
    return ( LCDWIDTH - textLen * 8 ) / 2;
}

void snkcWrapPlayfield()
{
    if( snkcSnake[ 0 ].x < 0 )
      snkcSnake[ 0 ].x = LCDWIDTH - SNKC_SEG_SIZE;
    if( snkcSnake[ 0 ].y < 0 )
      snkcSnake[ 0 ].y = LCDHEIGHT - SNKC_SEG_SIZE;
    if( snkcSnake[ 0 ].x > LCDWIDTH - SNKC_SEG_SIZE )
      snkcSnake[ 0 ].x = 0;
    if( snkcSnake[ 0 ].y > LCDHEIGHT - SNKC_SEG_SIZE )
      snkcSnake[ 0 ].y = 0;
}

// True if the given box overlaps any snake segment from the head up to
// (but not including) the current tail - used both to keep new food off
// the snake's own body, and (reused exactly as upstream does) to detect
// the head having just moved onto a food item.
bool snkcIsPartOfSnake( int x, int y, int w, int h )
{
    int i;
    for( i = 0; i < snkcLength - 1; i++ )
    {
        if( gbCollideRectRect( snkcSnake[ i ].x, snkcSnake[ i ].y, SNKC_SEG_SIZE, SNKC_SEG_SIZE, x, y, w, h ) )
          return true;
    }
    return false;
}

// True if the head has collided with any other body segment - game over.
bool snkcIsSnakePartOfSnake()
{
    int i;
    for( i = 1; i < snkcLength; i++ )
    {
        if( gbCollideRectRect( snkcSnake[ 0 ].x, snkcSnake[ 0 ].y, SNKC_SEG_SIZE, SNKC_SEG_SIZE, snkcSnake[ i ].x, snkcSnake[ i ].y, SNKC_SEG_SIZE, SNKC_SEG_SIZE ) )
          return true;
    }
    return false;
}

void snkcMakeFood()
{
    int i;
    for( i = 0; i < SNKC_FOOD_TYPES; i++ )
      snkcFood[ i ].exist = false;

    int randomType = arand( SNKC_FOOD_TYPES );
    if( randomType == SNKC_FOOD_MOUSE )
    {
        snkcFood[ randomType ].w = 8;
        snkcFood[ randomType ].h = 4;
        snkcFood[ randomType ].points = 2;
    }
    if( randomType == SNKC_FOOD_APPLE )
    {
        snkcFood[ randomType ].w = 8;
        snkcFood[ randomType ].h = 8;
        snkcFood[ randomType ].points = 1;
    }

    int x = arand( LCDWIDTH - snkcFood[ randomType ].w );
    int y = arand( LCDHEIGHT - snkcFood[ randomType ].h );
    while( snkcIsPartOfSnake( x, y, snkcFood[ randomType ].w, snkcFood[ randomType ].h ) )
    {
        x = arand( LCDWIDTH - snkcFood[ randomType ].w );
        y = arand( LCDHEIGHT - snkcFood[ randomType ].h );
    }

    snkcFood[ randomType ].x = x;
    snkcFood[ randomType ].y = y;
    snkcFood[ randomType ].type = randomType;
    snkcFood[ randomType ].exist = true;
}

// Starting position's axes are deliberately crossed - see this file's own
// header comment for why that's preserved rather than fixed.
void snkcMakeSnake()
{
    snkcDirection = SNKC_DIR_TOP;
    snkcLength = SNKC_START_LENGTH;

    int calcH = ( LCDHEIGHT / SNKC_SEG_SIZE ) / 2;
    int calcW = ( LCDWIDTH / SNKC_SEG_SIZE ) / 2;
    int startX = calcH * SNKC_SEG_SIZE;
    int startY = calcW * SNKC_SEG_SIZE;

    int i;
    for( i = 0; i < SNKC_MAX_LENGTH; i++ )
    {
        if( i == 0 )
        {
            snkcSnake[ i ].x = startX;
            snkcSnake[ i ].y = startY;
        }
        else
        {
            snkcSnake[ i ].x = 0 - SNKC_SEG_SIZE;
            snkcSnake[ i ].y = 0 - SNKC_SEG_SIZE;
        }
    }
}

void snkcDrawScore()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintNumber( snkcLength - SNKC_START_LENGTH );
}

// Real head/body sprite art, matching upstream's own drawSnake() exactly:
// the head (segment 0) picks its sprite from the snake's current direction,
// every other segment picks bodyV for TOP/BOTTOM travel or bodyH for
// LEFT/RIGHT travel (upstream keys this off the single global `direction`,
// not each segment's own neighbor-relative orientation - see this file's
// own header comment). Drawn at each segment's own raw x/y, unadjusted,
// exactly like upstream (its sprites aren't all exactly SNKC_SEG_SIZE
// square either, and it never centers them).
void snkcDrawSnake()
{
    gbSetColor( 1 );
    int i;
    for( i = 0; i < snkcLength; i++ )
    {
        if( snkcDirection == SNKC_DIR_TOP )
        {
            if( i == 0 ) gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcHeadUBitmap );
            else gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcBodyVBitmap );
        }
        else if( snkcDirection == SNKC_DIR_RIGHT )
        {
            if( i == 0 ) gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcHeadRBitmap );
            else gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcBodyHBitmap );
        }
        else if( snkcDirection == SNKC_DIR_BOTTOM )
        {
            if( i == 0 ) gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcHeadDBitmap );
            else gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcBodyVBitmap );
        }
        else if( snkcDirection == SNKC_DIR_LEFT )
        {
            if( i == 0 ) gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcHeadLBitmap );
            else gbDrawBitmap( snkcSnake[ i ].x, snkcSnake[ i ].y, snkcBodyHBitmap );
        }
    }
}

// Real food sprite art - the "mouse" and "apple" bitmaps, matching
// upstream's own drawFood() exactly.
void snkcDrawFood()
{
    gbSetColor( 1 );
    int i;
    for( i = 0; i < SNKC_FOOD_TYPES; i++ )
    {
        if( snkcFood[ i ].exist )
        {
            if( snkcFood[ i ].type == SNKC_FOOD_MOUSE )
              gbDrawBitmap( snkcFood[ i ].x, snkcFood[ i ].y, snkcMouseBitmap );
            if( snkcFood[ i ].type == SNKC_FOOD_APPLE )
              gbDrawBitmap( snkcFood[ i ].x, snkcFood[ i ].y, snkcAppleBitmap );
        }
    }
}

void snkcNextStep()
{
    int i;
    for( i = snkcLength - 1; i > 0; i-- )
    {
        snkcSnake[ i ].x = snkcSnake[ i - 1 ].x;
        snkcSnake[ i ].y = snkcSnake[ i - 1 ].y;
    }

    if( snkcDirection == SNKC_DIR_TOP )
      snkcSnake[ 0 ].y = snkcSnake[ 0 ].y - SNKC_SEG_SIZE;
    else if( snkcDirection == SNKC_DIR_RIGHT )
      snkcSnake[ 0 ].x = snkcSnake[ 0 ].x + SNKC_SEG_SIZE;
    else if( snkcDirection == SNKC_DIR_BOTTOM )
      snkcSnake[ 0 ].y = snkcSnake[ 0 ].y + SNKC_SEG_SIZE;
    else if( snkcDirection == SNKC_DIR_LEFT )
      snkcSnake[ 0 ].x = snkcSnake[ 0 ].x - SNKC_SEG_SIZE;

    snkcWrapPlayfield();

    int f;
    for( f = 0; f < SNKC_FOOD_TYPES; f++ )
    {
        if( snkcFood[ f ].exist && snkcIsPartOfSnake( snkcFood[ f ].x, snkcFood[ f ].y, snkcFood[ f ].w, snkcFood[ f ].h ) )
        {
            snkcFood[ f ].exist = false;
            snkcLength = snkcLength + snkcFood[ f ].points;
            if( snkcLength > SNKC_MAX_LENGTH )
              snkcLength = SNKC_MAX_LENGTH;
            if( snkcLength < SNKC_MAX_LENGTH )
              snkcMakeFood();
        }
    }

    if( snkcIsSnakePartOfSnake() )
      snkcState = SNKC_STATE_GAMEOVER;
}

void snkcBeginTitle()
{
    snkcState = SNKC_STATE_TITLE;
}

void snkcBeginPlay()
{
    snkcStepCounter = 0;
    snkcPaused = false;
    snkcMakeSnake();
    snkcMakeFood();
    snkcState = SNKC_STATE_PLAY;
}

// Real upstream title logo (64x30), restored via gbDrawBitmap() - see this
// file's own header comment. Centered horizontally ((84-64)/2 = 10); the
// "PRESS A" prompt sits below it rather than overlapping.
void snkcUpdateTitle()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbDrawBitmap( 10, 2, snkcLogoBitmap );

    gbCursorX = snkcCenterX( 7 );
    gbCursorY = 36;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      snkcBeginPlay();
}

void snkcUpdateGameOver()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbCursorX = snkcCenterX( 9 );
    gbCursorY = 8;
    gbPrintString( "GAME OVER" );

    gbCursorX = snkcCenterX( 5 );
    gbCursorY = 22;
    gbPrintString( "SCORE" );

    gbCursorX = 36;
    gbCursorY = 34;
    gbPrintNumber( snkcLength - SNKC_START_LENGTH );

    if( gbPressed( BTN_A ) )
      snkcBeginPlay();
}

void snkcUpdatePlay()
{
    // BTN_C pauses-and-shows-the-title-screen, matching upstream's own
    // mainMenu()-on-C behavior exactly.
    if( gbPressed( BTN_C ) )
    {
        snkcBeginTitle();
        return;
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
      snkcPaused = !snkcPaused;

    if( !snkcPaused )
    {
        // Only Left/Right steer (rotating the current direction) - Up/Down
        // are never read here. See this file's own header comment for why
        // that's faithful to upstream, not a porting omission.
        if( gbPressed( BTN_LEFT ) )
        {
            snkcDirection = snkcDirection - 1;
            if( snkcDirection < 0 )
              snkcDirection = SNKC_DIR_LEFT;
        }
        if( gbPressed( BTN_RIGHT ) )
        {
            snkcDirection = snkcDirection + 1;
            if( snkcDirection > SNKC_DIR_LEFT )
              snkcDirection = SNKC_DIR_TOP;
        }

        snkcStepCounter = snkcStepCounter + 1;
        if( snkcStepCounter >= SNKC_TICKS_PER_STEP )
        {
            snkcStepCounter = 0;
            snkcNextStep();
            if( snkcState == SNKC_STATE_GAMEOVER )
              return;
        }
    }

    snkcDrawScore();
    snkcDrawSnake();
    snkcDrawFood();

    if( snkcPaused )
    {
        gbSetColor( 0 );
        gbFillRect( 0, 0, LCDWIDTH, 9 );
        gbSetColor( 1 );
        gbCursorX = snkcCenterX( 5 );
        gbCursorY = 1;
        gbPrintString( "PAUSE" );
    }
}

void gameSnakeClassic_init()
{
    gbBegin();
    snkcBeginTitle();
}

void gameSnakeClassic_update()
{
    if( !gbUpdate() ) return;

    if( snkcState == SNKC_STATE_TITLE ) snkcUpdateTitle();
    else if( snkcState == SNKC_STATE_GAMEOVER ) snkcUpdateGameOver();
    else snkcUpdatePlay();

    gbRenderFrame();
}
