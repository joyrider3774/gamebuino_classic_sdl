#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Blob-Attack (LudumDareDevelopment / "TEAM Arg", license: none specified -
// github.com/LudumDareDevelopment/Blob-Attack). A Puyo-Puyo/Columns-style
// falling-blob puzzle on the real 8x8 Gamebuino Classic playfield: a 2-cell
// "domino" piece (one always-present center blob plus a second blob on one
// of its four edges, rotatable through all four orientations) falls one row
// every 25 frames; landing locks it into the grid, and any 4-or-more blobs
// of the same color/shape connected in a pack/column/row are cleared for
// points (plus a growing chain bonus for consecutive clears within the same
// drop). A real, complete game (menu with HELP/INFO/PLAY, a pause screen, a
// game-over screen with EEPROM high-score persistence) - not just a static
// screen.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(1, 4)`/`random(1, 6)` became
// `arand(3) + 1`/`arand(5) + 1` (this dialect's own established RNG helper -
// `random(min, max)` -> `arand(max - min) + min`). Every global got a
// `blob`-prefixed name (this cartridge has no linker - every game shares one
// flat global namespace). Upstream's `byte` fields (there is no `byte` type
// at all in this dialect's own avrCompat.h - only a real `int`/`float`/
// `bool`/`void`) all became plain `int` (or `bool` for the two genuine
// true/false fields, `canMoveBlobsDown`/`giveExtraScore` - a better match
// than upstream's own Arduino `boolean`-as-byte idiom, same treatment
// gameAgaruino.c already used). `field`/`fieldFlags` (`int field[8][8]`)
// became `int[8][8] blobField`/`blobFieldFlags` (this dialect's own
// `TYPE[N][M] name` array-declaration order, already proven in
// gameConduit.c/gameLander.c/etc). The 6 small blob-tile bitmaps
// (`blobs_bitmap[]`, an array of pointers to 6 separate `byte[]` arrays
// upstream) were consolidated into one `int[6][8] blobTileBitmaps` 2D table
// instead (each row is one complete `{width,height,bytes...}` bitmap,
// row-indexed and passed straight to `gbDrawBitmap()`/`gbDrawBitmapRotated()`
// - the same "table of bitmaps, not array of pointers" convention
// gameLander.c's own `landLandscapeTiles`/`landLandscape[x][y]-1` lookup
// already established, since a real `int*[N]` pointer-array declaration has
// no precedent anywhere else in this project). Upstream's own
// `switch(which_blobs)`/`switch(selector)`/`switch(state)` (real Gamebuino
// games commonly use `switch`, unlike this project's own convention - see
// this project's own CLAUDE.md dialect rules) all became if/else-if chains.
//
// Upstream's own blocking `gb.titleScreen(F("Blob Attack"))` - called once
// in `setup()`, and again from inside the STATE_PLAYING case as a genuine
// "freeze gameplay and show the title" gesture when Button C is pressed -
// was converted into an explicit `BLOB_STATE_TITLE` state (matching every
// other port's own "blocking widget -> explicit resumable state" treatment,
// see gamePong.c's own header comment), dismissed by a genuine
// `gbPressed(BTN_A)` and returning to whichever state requested it
// (`blobTitleReturnState` - `BLOB_STATE_MAIN_MENU` at boot,
// `BLOB_STATE_PLAYING` when triggered mid-game by Button C). The Button C
// check itself was moved to the very top of `blobUpdatePlaying()` (upstream
// checks it in the middle of the case, after `DrawField()`/the drop timer)
// with an early return - the same reordering gamePong.c's own
// `pongUpdatePlay()` already uses for its own analogous Button C/
// `titleScreen()` pause gesture, since this shim has no way to genuinely
// freeze mid-frame the way a real blocking call can. `gb.pickRandomSeed()`
// became `gbPickRandomSeed()`, a documented no-op.
//
// Upstream's own setup()-time pre-init (`FillBlobPit()`/`CreateCurrentBlobs()`/
// `removeFlag()`/`InitPlayfield()`/resetting `canMoveBlobsDown`/
// `giveExtraScore`/`scorePlayer`/`extraScoreForChain`, all called once before
// `state = STATE_MAIN_MENU`) is genuinely dead code on real hardware too:
// `STATE_MAIN_MENU`'s own case body unconditionally re-runs every one of
// those same reset calls itself, on every single frame it's active
// (including the very first one), so whatever setup() did gets immediately
// overwritten before a player can ever see it. Dropped here without any
// behavioral change - `gameBlobAttack_init()` only keeps the parts that
// actually matter once (reading the EEPROM high score, `blobSelector = 0`).
// `BLOB_WAITING` (upstream's own unused `#define ... 4`) and `BlobNumbers`
// (an upstream global declared but never once read or written anywhere in
// the real source) were both dropped outright as genuine dead code, not
// silently - both confirmed unused by a full-file search before dropping.
//
// A real missing-return-path bug found in `IsOneBlobDropPossible()`: when
// its outer `if` condition is true but its inner `for` loop never matches,
// upstream's own C++ function falls off the end without returning anything
// at all (undefined behavior on real AVR - whatever garbage was left in the
// return register). This dialect requires every path to return a value, so
// an explicit `return false;` was added for that exact fall-through case in
// `blobIsOneDropPossible()` below - "no blob drop was possible" is both the
// most likely real-world compiled outcome for this exact code shape and the
// least surprising interpretation, not a silent behavior change.
//
// A real, subtle "looks unsafe but isn't" quirk found while auditing every
// `blobField[][]`/`blobFieldFlags[][]` index arithmetic in
// `blobFourInPack()`/`blobFourInColumn()`/`blobFourInRow()` by hand against
// the real 8x8 grid bounds (ported verbatim below, with no defensive clamps
// added): `blobAboveIsSame()`/`blobLeftIsSame()` use an off-by-one `- 1 > 0`
// bounds check internally (should arguably be `>= 0`), which looks like a
// real bug in isolation (row/column 1 never counts as "same as above/left"
// of row/column 0) - but every single `column+2`/`column-1`/`row-3`-style
// write in the three match-detection functions is, in every real call path,
// already guarded by exactly this same off-by-one check having returned
// true first. The two "bugs" cancel out into a genuinely safe real program;
// confirmed by hand, case by case, before deciding no bounds-check needed
// adding here. `blobIsMovePossible()`'s own similar-looking
// out-of-bounds-vs-occupied check was audited the same way and is likewise
// always safe (an occupied, off-grid cell always returns `false` before
// `blobIsTileFree()` is ever called with an out-of-range coordinate; an
// unoccupied one skips that call entirely via the same `temp != 0` guard).
//
// A real behavioral difference from actual hardware, found and documented
// rather than silently reproduced or silently "fixed": upstream's own
// `byte selector` is an *unsigned* 8-bit field, so `selector--` at
// `selector == 0` wraps to 255 on real AVR hardware - and since
// `if (selector < 0)` can never be true for an unsigned byte, that specific
// clamp line is real dead code on real hardware, while the very next
// `if (selector > 2) selector = 2;` line *does* catch the wrapped 255,
// so real Gamebuino hardware's actual observed behavior is "pressing UP at
// the topmost menu entry wraps around to the bottommost one" - almost
// certainly not what the two clamp lines were meant to do. This dialect has
// no unsigned 8-bit type at all (see avrCompat.h's own header comment) -
// `blobSelector` is a plain signed `int` below, so its own `< 0` clamp
// actually works, giving the obviously-intended "stop at the top entry"
// behavior instead of real hardware's wraparound quirk. Documented here
// rather than hand-simulating the wraparound just to match an accidental
// real-hardware artifact.
//
// Upstream's own `gb.display.println(...)` call chains (each screen sets
// `cursorX`/`cursorY` once, then calls `println()` two or three times in a
// row, relying on real `Print::println()`'s own implicit "newline + return
// to column 0" behavior between each) were consolidated into a single
// `gbPrintString("line one\nline two\n...")` call per screen (this shim's
// own `gbPrintString()` already special-cases `'\n'` exactly the same way -
// advance `gbCursorY` by one line, reset `gbCursorX` to 0 - see
// gameUfoRace.c's own `"\nBest      "` precedent) - same real screen output,
// fewer call sites.
//
// A real upstream cursor-position bug in the pause screen, FOUND AND FIXED
// (not preserved): `gb.display.println("B to play")` is called with no
// `cursorX`/`cursorY` reset beforehand at all, unlike every other text draw
// in this file - it silently inherited whatever cursor position the last
// frame's own drawing left behind (in practice, wherever
// `blobDrawBlobs(..., BLOB_NEXT)`'s own "Next" label last positioned it,
// near the top-right of the screen), landing nowhere near the pause icon
// it's presumably meant to label. Fixed in `blobUpdatePause()`: the label
// is now explicitly centered directly below the pause icon.
//
// A real one-byte-EEPROM high-score bug, FOUND AND FIXED (not preserved):
// upstream's own `unsigned long highScore = EEPROM.read(0);` (setup()) and
// `EEPROM.write(0, scorePlayer)` (game-over) both operate through real
// AVR `EEPROM.read()`/`.write()`, which only ever handles a single byte -
// assigning/passing a wider `unsigned long` silently truncates to its own
// low 8 bits on the way in or out, so any real score over 255 reads back
// wrapped modulo 256 after a restart. Fixed by using `eeprom_read_word()`/
// `eeprom_write_word()` instead of the byte-narrow primitives, with this
// project's own established fresh-cell (`==65535`) sentinel check on load -
// `blobHighScore` now persists and reloads the real, full score
// unconditionally. This is this project's first real EEPROM consumer
// (every prior EEPROM-using port reads/writes 16-bit-or-wider scores
// byte-split across two addresses instead - Blob-Attack's own upstream
// source is the first one that actually only ever used a single truncated
// byte, on real hardware too).
//
// Sound: `gb.sound.playTick()` -> `gbPlayTick()` directly. The two-note
// "clear" jingle in `removeGroups()` (`playNote(440,2,0); delay(100);
// playNote(1047,2,0);`) dropped its `delay(100)` outright - this dialect has
// no blocking-delay primitive at all (see gameSnakeClassic.c's own header
// comment for the same conclusion reached independently there) and this
// shim's sound is one-shot/single-channel besides, so the two short (2-tick)
// notes are now just fired back-to-back with no gap; a documented, sensible
// approximation of upstream's own already-minor victory chime, not a scope
// gap to flag.
//
// No missing-shim-primitive workarounds were needed for this port -
// `gbDrawBitmap()`/`gbDrawBitmapRotated()`/`gbDrawChar()`/`gbRepeat()` all
// already exist and behave as documented (this file doesn't even need
// `gbRepeat()` - upstream only ever used `buttons.pressed()`, a plain
// edge-triggered press, for every single input in this game).

#define BLOB_PLAYFIELD_WIDTH  8
#define BLOB_PLAYFIELD_HEIGHT 8
#define BLOB_PIXELS 6

#define BLOB_FREE 0
#define BLOB_ZERO_X 0
#define BLOB_ZERO_Y 0

#define BLOB_CURRENT 0
#define BLOB_NEXT    2

#define BLOB_TILES_IN_BLOBS 3

#define BLOB_NO_FLAG 0
#define BLOB_FLAG    1

enum BlobState
{
    BLOB_STATE_TITLE     = 0,
    BLOB_STATE_MAIN_MENU = 1,
    BLOB_STATE_PLAYING   = 2,
    BLOB_STATE_HELP      = 3,
    BLOB_STATE_INFO      = 4,
    BLOB_STATE_PAUSE     = 5,
    BLOB_STATE_GAME_OVER = 6
};

int blobState;
int blobTitleReturnState;
int blobSelector;

int blobField[BLOB_PLAYFIELD_WIDTH][BLOB_PLAYFIELD_HEIGHT];
int blobFieldFlags[BLOB_PLAYFIELD_WIDTH][BLOB_PLAYFIELD_HEIGHT];

int blobCurrentBlobs[9]=
{
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
};

int blobRandomPit[6];
int blobXY[2]; // X and Y coordinates of the falling piece's own 3x3 local grid origin

bool blobCanMoveDown;
bool blobGiveExtraScore;

int blobScore;
int blobExtraScoreForChain;
int blobHighScore;

// Real Display::drawBitmap() format ({width,height} header then
// ceil(width/8) bytes/row, row-major MSB-first) - one row per blob tile,
// index 0 (BLOB_FREE) is an all-zero/invisible tile, matching upstream's
// own blob00_bitmap exactly. Upstream's own `blob00inverted_bitmap` was not
// ported - it's real, genuine dead code upstream too (only ever referenced
// from a commented-out line inside `moveBlobsDown()`).
int blobTileBitmaps[6][8]=
{
    { 8, 6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // BLOB_FREE - blank/invisible
    { 8, 6, 0x0C, 0x1A, 0x13, 0x1A, 0x0C, 0x00 },
    { 8, 6, 0x06, 0x1D, 0x17, 0x1D, 0x06, 0x00 },
    { 8, 6, 0x0C, 0x17, 0x1E, 0x17, 0x0C, 0x00 },
    { 8, 6, 0x19, 0x13, 0x11, 0x13, 0x19, 0x00 },
    { 8, 6, 0x1F, 0x1D, 0x1F, 0x1D, 0x1F, 0x00 },
};

// menu_bitmap.h's own real B-binary literals, mechanically converted to hex
// via a small script parsing the real upstream header directly (not hand
// arithmetic) - byte counts cross-checked against each bitmap's own
// {width,height} header (176/260/15 data bytes respectively) before use.
int blobSplash1Bitmap[178]=
{
    64, 22,
    0x00, 0x7F, 0xE0, 0x00, 0x00, 0x1F, 0xC0, 0x00,
    0x07, 0xA2, 0xBC, 0x00, 0x00, 0xF5, 0x78, 0x00,
    0x08, 0x00, 0x8B, 0x80, 0x06, 0x80, 0x05, 0x80,
    0x1C, 0x00, 0x48, 0xC0, 0x0D, 0x60, 0x02, 0xC0,
    0x20, 0x02, 0xA5, 0x60, 0x02, 0x80, 0x01, 0x60,
    0x60, 0x0B, 0xA8, 0x28, 0x28, 0xD6, 0xA9, 0x58,
    0x60, 0x5A, 0xB5, 0x58, 0x78, 0x76, 0xAA, 0xAC,
    0x41, 0x6D, 0xDB, 0xFC, 0x50, 0xBF, 0xD5, 0x5C,
    0xD3, 0x77, 0xBC, 0x1C, 0xD0, 0xD3, 0xD6, 0xBE,
    0xAA, 0xBD, 0xF7, 0x16, 0xD8, 0x71, 0xFA, 0xB6,
    0xDF, 0xFF, 0x9D, 0x1E, 0xDC, 0x01, 0xEE, 0xB7,
    0xD0, 0xE9, 0x8A, 0x36, 0xCB, 0x87, 0x0F, 0xBB,
    0xB0, 0x69, 0xC0, 0x3A, 0x77, 0xFF, 0x03, 0xDF,
    0xD8, 0x61, 0xC0, 0xCA, 0x6A, 0xFB, 0x1D, 0xAF,
    0xAE, 0x07, 0xFD, 0x06, 0x2A, 0xBB, 0x8E, 0x37,
    0xAB, 0xFF, 0x6F, 0xC6, 0x1D, 0xDF, 0xC0, 0x12,
    0x56, 0xFF, 0xF8, 0x04, 0x0E, 0xF6, 0xE0, 0x1E,
    0x32, 0xBF, 0xE8, 0x18, 0x03, 0x75, 0x5F, 0xDC,
    0x19, 0x55, 0x7A, 0x30, 0x01, 0xFF, 0xC0, 0x58,
    0x0D, 0x6F, 0xC0, 0xE0, 0x00, 0x75, 0x22, 0xB0,
    0x03, 0xFB, 0xDF, 0x80, 0x00, 0x1F, 0xF7, 0x80,
    0x00, 0x3F, 0xF8, 0x00, 0x00, 0x01, 0xFE, 0x00,
};

int blobSplash2Bitmap[262]=
{
    80, 26,
    0x7F, 0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0F, 0xCF, 0x80, 0x00, 0x03, 0xE0, 0x00, 0x00,
    0x00, 0x00, 0x07, 0xCF, 0x80, 0x00, 0x03, 0xE0, 0x00, 0x00,
    0x00, 0xF8, 0x07, 0xDF, 0x80, 0x00, 0x07, 0xE0, 0x00, 0x00,
    0x01, 0xF8, 0x07, 0x9F, 0x00, 0x00, 0x07, 0xC0, 0x00, 0x00,
    0x01, 0xF0, 0x0F, 0x1F, 0x00, 0x00, 0x07, 0xC0, 0x00, 0x00,
    0x01, 0xF0, 0x1E, 0x3F, 0x00, 0x00, 0x0F, 0xC0, 0x00, 0x00,
    0x03, 0xF0, 0x78, 0x3E, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x00,
    0x03, 0xE1, 0xF0, 0x3E, 0x07, 0xFF, 0xFF, 0x8F, 0xFF, 0xC0,
    0x03, 0xE1, 0xF8, 0x7E, 0x1F, 0xFF, 0xFF, 0x9F, 0xFF, 0x80,
    0x07, 0xE0, 0xF8, 0x7C, 0x3E, 0x38, 0x1F, 0x1F, 0x00, 0x00,
    0x07, 0xC0, 0xF8, 0x7C, 0x7E, 0x7C, 0x1F, 0x1F, 0x00, 0x00,
    0x07, 0xC0, 0xF8, 0xFC, 0x7C, 0x7C, 0x3F, 0x0E, 0x00, 0x00,
    0x0F, 0x81, 0xF0, 0xF8, 0x7C, 0x7C, 0x3E, 0x0E, 0x00, 0x00,
    0x0F, 0x87, 0xF0, 0xF8, 0x7C, 0xF8, 0x3E, 0x3C, 0x00, 0x00,
    0x0F, 0xFF, 0xC0, 0xFF, 0xFF, 0xF0, 0x3F, 0xF0, 0x00, 0x00,
    0x1F, 0xFE, 0x00, 0xFF, 0x8F, 0xE0, 0x1F, 0xC0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x83, 0xFD, 0xFE, 0x78, 0x1E, 0x0E, 0xF0, 0x00, 0x00,
    0x07, 0x87, 0xFB, 0xFC, 0x78, 0x3F, 0x8E, 0xC0, 0x00, 0x00,
    0x06, 0x85, 0xFA, 0xFC, 0x68, 0x78, 0x8F, 0xC0, 0x00, 0x00,
    0x0E, 0xC1, 0xD0, 0xE8, 0xEC, 0x78, 0x0F, 0xC0, 0x00, 0x00,
    0x0F, 0xC0, 0xC0, 0x60, 0xFC, 0x7C, 0x0F, 0xE0, 0x00, 0x00,
    0x0F, 0xE0, 0xC0, 0x60, 0xFE, 0x7F, 0x8E, 0xE0, 0x00, 0x00,
    0x0E, 0x60, 0xC0, 0x60, 0xE6, 0x3F, 0x8E, 0x70, 0x00, 0x00,
    0x1E, 0xC0, 0xC0, 0x61, 0xEC, 0x1E, 0x0F, 0x78, 0x00, 0x00,
};

int blobPauseBitmap[17]=
{
    24, 5,
    0xEE, 0xAE, 0xE0,
    0xAA, 0xA8, 0x80,
    0xEE, 0xAE, 0xE0,
    0x8A, 0xA2, 0x80,
    0x8A, 0xEE, 0xE0,
};

void blobDrawField()
{
    int x, y;
    for( y = 0; y < BLOB_PLAYFIELD_HEIGHT; y = y + 1 )
    {
        for( x = 0; x < BLOB_PLAYFIELD_WIDTH; x = x + 1 )
        {
            // ROTCCW, NOFLIP - matches upstream's own exact drawBitmap() call
            gbDrawBitmapRotated( x * BLOB_PIXELS, y * BLOB_PIXELS, blobTileBitmaps[ blobField[ x ][ y ] ], 1, 0 );
        }
    }
}

void blobInitPlayfield()
{
    int x, y;
    for( x = 0; x < BLOB_PLAYFIELD_WIDTH; x = x + 1 )
      for( y = 0; y < BLOB_PLAYFIELD_HEIGHT; y = y + 1 )
        blobField[ x ][ y ] = BLOB_FREE;
}

void blobFillPit()
{
    int x;
    for( x = 0; x < 6; x = x + 1 )
      blobRandomPit[ x ] = arand( 3 ) + 1; // real random(1, 4) -> 1..3
}

void blobCreateCurrentBlobs()
{
    blobXY[0] = 2; // player X
    blobXY[1] = 0; // player Y

    int i;
    for( i = 0; i < 9; i = i + 1 )
      blobCurrentBlobs[ i ] = 0;

    blobCurrentBlobs[1] = blobRandomPit[0];
    blobCurrentBlobs[4] = blobRandomPit[1];

    for( i = 0; i < 4; i = i + 1 )
      blobRandomPit[ i ] = blobRandomPit[ i + 2 ];

    blobRandomPit[4] = arand( 5 ) + 1; // real random(1, 6) -> 1..5
    blobRandomPit[5] = arand( 5 ) + 1;
}

void blobDrawBlobs( int drawX, int drawY, int whichBlobs )
{
    if( whichBlobs == BLOB_CURRENT )
    {
        int drawPointer = 0;
        int y, x;
        for( y = drawY; y < drawY + 18; y = y + BLOB_PIXELS )
        {
            for( x = drawX; x < drawX + 18; x = x + BLOB_PIXELS )
            {
                int temp = blobCurrentBlobs[ drawPointer ];
                if( temp > 0 )
                  gbDrawBitmapRotated( x, y, blobTileBitmaps[ temp ], 1, 0 ); // ROTCCW, NOFLIP
                drawPointer = drawPointer + 1;
            }
        }
    }
    else if( whichBlobs == BLOB_NEXT )
    {
        gbCursorX = LCDWIDTH - 21;
        gbCursorY = 0;
        gbPrintString( "Next" );
        gbDrawBitmapRotated( drawX, drawY + 8, blobTileBitmaps[ blobRandomPit[0] ], 1, 0 );
        gbDrawBitmapRotated( drawX, drawY + 8 + 8, blobTileBitmaps[ blobRandomPit[1] ], 1, 0 );
    }
}

bool blobAboveIsSame( int arrayX, int arrayY )
{
    if( ( arrayY - 1 > 0 ) && ( blobField[ arrayX ][ arrayY ] == blobField[ arrayX ][ arrayY - 1 ] ) ) return true;
    else return false;
}

bool blobUnderIsSame( int arrayX, int arrayY )
{
    if( ( arrayY + 1 < BLOB_PLAYFIELD_HEIGHT ) && ( blobField[ arrayX ][ arrayY ] == blobField[ arrayX ][ arrayY + 1 ] ) ) return true;
    else return false;
}

bool blobRightIsSame( int arrayX, int arrayY )
{
    if( ( arrayX + 1 < BLOB_PLAYFIELD_WIDTH ) && ( blobField[ arrayX ][ arrayY ] == blobField[ arrayX + 1 ][ arrayY ] ) ) return true;
    else return false;
}

bool blobLeftIsSame( int arrayX, int arrayY )
{
    if( ( arrayX - 1 > 0 ) && ( blobField[ arrayX ][ arrayY ] == blobField[ arrayX - 1 ][ arrayY ] ) ) return true;
    else return false;
}

bool blobIsTileFree( int arrayX, int arrayY )
{
    if( blobField[ arrayX ][ arrayY ] == BLOB_FREE ) return true;
    else return false;
}

// See this file's own header comment - every out-of-bounds-looking access
// below was hand-audited against the real 8x8 grid and confirmed always
// safe, given the exact same call-site guards upstream used. Ported
// verbatim, no extra clamps added.
bool blobIsMovePossible( int arrayX, int arrayY )
{
    int drawPointer = 0;
    int y, x;
    for( y = arrayY; y < arrayY + BLOB_TILES_IN_BLOBS; y = y + 1 )
    {
        for( x = arrayX; x < arrayX + BLOB_TILES_IN_BLOBS; x = x + 1 )
        {
            if( x < 0 || x > BLOB_PLAYFIELD_WIDTH - 1 || y > BLOB_PLAYFIELD_HEIGHT - 1 )
            {
                int edgeTemp = blobCurrentBlobs[ drawPointer ];
                if( edgeTemp != 0 ) return false;
            }

            int temp = blobCurrentBlobs[ drawPointer ];
            if( ( temp != 0 ) && ( blobIsTileFree( x, y ) == false ) ) return false;
            drawPointer = drawPointer + 1;
        }
    }
    return true;
}

void blobRemoveFlag()
{
    int x, y;
    for( x = 0; x < BLOB_PLAYFIELD_WIDTH; x = x + 1 )
      for( y = 0; y < BLOB_PLAYFIELD_HEIGHT; y = y + 1 )
        blobFieldFlags[ x ][ y ] = BLOB_NO_FLAG;
}

void blobFourInPack()
{
    int column, row;
    for( column = 0; column < BLOB_PLAYFIELD_WIDTH; column = column + 1 )
    {
        for( row = BLOB_PLAYFIELD_HEIGHT - 1; row > 0; row = row - 1 )
        {
            if( !blobIsTileFree( column, row ) )
            {
                if( blobAboveIsSame( column, row ) && blobRightIsSame( column, row ) && blobAboveIsSame( column + 1, row ) )
                {
                    blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column ][ row - 1 ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row - 1 ] = BLOB_FLAG;
                }
                if( blobRightIsSame( column, row ) && blobAboveIsSame( column + 1, row ) && blobRightIsSame( column + 1, row - 1 ) )
                {
                    blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row - 1 ] = BLOB_FLAG;
                    blobFieldFlags[ column + 2 ][ row - 1 ] = BLOB_FLAG;
                }
                if( blobRightIsSame( column, row ) && blobUnderIsSame( column + 1, row ) && blobRightIsSame( column + 1, row + 1 ) )
                {
                    blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row + 1 ] = BLOB_FLAG;
                    blobFieldFlags[ column + 2 ][ row + 1 ] = BLOB_FLAG;
                }
                if( blobAboveIsSame( column, row ) && blobRightIsSame( column, row - 1 ) && blobAboveIsSame( column + 1, row - 1 ) )
                {
                    blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column ][ row - 1 ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row - 1 ] = BLOB_FLAG;
                    blobFieldFlags[ column + 1 ][ row - 2 ] = BLOB_FLAG;
                }
                if( blobAboveIsSame( column, row ) && blobLeftIsSame( column, row - 1 ) && blobAboveIsSame( column - 1, row - 1 ) )
                {
                    blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                    blobFieldFlags[ column ][ row - 1 ] = BLOB_FLAG;
                    blobFieldFlags[ column - 1 ][ row - 1 ] = BLOB_FLAG;
                    blobFieldFlags[ column - 1 ][ row - 2 ] = BLOB_FLAG;
                }
            }
        }
    }
}

void blobFourInColumn()
{
    int column, row, temp;
    for( column = 0; column < BLOB_PLAYFIELD_WIDTH; column = column + 1 )
    {
        for( row = BLOB_PLAYFIELD_HEIGHT - 1; row > 0; row = row - 1 )
        {
            if( !blobIsTileFree( column, row ) )
            {
                if( blobAboveIsSame( column, row ) && blobAboveIsSame( column, row - 1 ) )
                {
                    if( blobAboveIsSame( column, row - 2 ) )
                    {
                        blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                        blobFieldFlags[ column ][ row - 1 ] = BLOB_FLAG;
                        blobFieldFlags[ column ][ row - 2 ] = BLOB_FLAG;
                        blobFieldFlags[ column ][ row - 3 ] = BLOB_FLAG;
                    }
                    for( temp = 0; temp < 3; temp = temp + 1 )
                    {
                        if( blobRightIsSame( column, row - temp ) )
                        {
                            blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column ][ row - 1 ] = BLOB_FLAG;
                            blobFieldFlags[ column ][ row - 2 ] = BLOB_FLAG;
                            blobFieldFlags[ column + 1 ][ row - temp ] = BLOB_FLAG;
                        }
                    }
                    for( temp = 0; temp < 3; temp = temp + 1 )
                    {
                        if( blobLeftIsSame( column, row - temp ) )
                        {
                            blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column ][ row - 1 ] = BLOB_FLAG;
                            blobFieldFlags[ column ][ row - 2 ] = BLOB_FLAG;
                            blobFieldFlags[ column - 1 ][ row - temp ] = BLOB_FLAG;
                        }
                    }
                }
            }
        }
    }
}

void blobFourInRow()
{
    int column, row, temp;
    for( column = 0; column < BLOB_PLAYFIELD_WIDTH; column = column + 1 )
    {
        for( row = BLOB_PLAYFIELD_HEIGHT - 1; row > 0; row = row - 1 )
        {
            if( !blobIsTileFree( column, row ) )
            {
                if( blobRightIsSame( column, row ) && blobRightIsSame( column + 1, row ) )
                {
                    if( blobRightIsSame( column + 2, row ) )
                    {
                        blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                        blobFieldFlags[ column + 1 ][ row ] = BLOB_FLAG;
                        blobFieldFlags[ column + 2 ][ row ] = BLOB_FLAG;
                        blobFieldFlags[ column + 3 ][ row ] = BLOB_FLAG;
                    }
                    for( temp = 0; temp < 3; temp = temp + 1 )
                    {
                        if( blobAboveIsSame( column + temp, row ) )
                        {
                            blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column + 1 ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column + 2 ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column + temp ][ row - 1 ] = BLOB_FLAG;
                        }
                    }
                    for( temp = 0; temp < 3; temp = temp + 1 )
                    {
                        if( blobUnderIsSame( column + temp, row ) )
                        {
                            blobFieldFlags[ column ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column + 1 ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column + 2 ][ row ] = BLOB_FLAG;
                            blobFieldFlags[ column + temp ][ row + 1 ] = BLOB_FLAG;
                        }
                    }
                }
            }
        }
    }
}

bool blobIsOnlyOne()
{
    int temp = 0;
    int i;
    for( i = 0; i < 9; i = i + 1 )
      if( blobCurrentBlobs[ i ] != 0 ) temp = temp + 1;

    if( temp < 2 ) return true;
    else return false;
}

void blobRotateRight()
{
    if( !blobIsOnlyOne() )
    {
        int temp = blobCurrentBlobs[1];
        blobCurrentBlobs[1] = blobCurrentBlobs[3];
        blobCurrentBlobs[3] = blobCurrentBlobs[7];
        blobCurrentBlobs[7] = blobCurrentBlobs[5];
        blobCurrentBlobs[5] = temp;
        gbPlayTick();
    }
    if( !blobIsMovePossible( blobXY[0], blobXY[1] ) )
    {
        int temp = blobCurrentBlobs[1];
        blobCurrentBlobs[1] = blobCurrentBlobs[5];
        blobCurrentBlobs[5] = blobCurrentBlobs[7];
        blobCurrentBlobs[7] = blobCurrentBlobs[3];
        blobCurrentBlobs[3] = temp;
    }
}

void blobRotateLeft()
{
    if( !blobIsOnlyOne() )
    {
        int temp = blobCurrentBlobs[1];
        blobCurrentBlobs[1] = blobCurrentBlobs[5];
        blobCurrentBlobs[5] = blobCurrentBlobs[7];
        blobCurrentBlobs[7] = blobCurrentBlobs[3];
        blobCurrentBlobs[3] = temp;
        gbPlayTick();
    }
    if( !blobIsMovePossible( blobXY[0], blobXY[1] ) )
    {
        int temp = blobCurrentBlobs[1];
        blobCurrentBlobs[1] = blobCurrentBlobs[3];
        blobCurrentBlobs[3] = blobCurrentBlobs[7];
        blobCurrentBlobs[7] = blobCurrentBlobs[5];
        blobCurrentBlobs[5] = temp;
    }
}

void blobMoveRight()
{
    if( !blobIsOnlyOne() && blobIsMovePossible( blobXY[0] + 1, blobXY[1] ) )
      blobXY[0] = blobXY[0] + 1;
}

void blobMoveLeft()
{
    if( !blobIsOnlyOne() && blobIsMovePossible( blobXY[0] - 1, blobXY[1] ) )
      blobXY[0] = blobXY[0] - 1;
}

void blobDrawCurrent()
{
    blobDrawBlobs( ( blobXY[0] * BLOB_PIXELS ) + BLOB_ZERO_X, ( blobXY[1] * BLOB_PIXELS ) + BLOB_ZERO_Y, BLOB_CURRENT );
}

void blobMoveDown()
{
    blobCanMoveDown = false;
    int column, row;
    for( column = 0; column < BLOB_PLAYFIELD_WIDTH; column = column + 1 )
    {
        for( row = BLOB_PLAYFIELD_HEIGHT - 1; row > 0; row = row - 1 )
        {
            if( blobIsTileFree( column, row ) )
            {
                if( !blobIsTileFree( column, row - 1 ) )
                {
                    blobField[ column ][ row ] = blobField[ column ][ row - 1 ];
                    blobField[ column ][ row - 1 ] = BLOB_FREE;
                    blobDrawField(); // real upstream redundancy, preserved - the whole field is
                                      // redrawn on every single tile move-down, then this one
                                      // tile is drawn again right after (see next line)
                    gbDrawBitmap( column * BLOB_PIXELS, row * BLOB_PIXELS, blobTileBitmaps[ blobField[ column ][ row ] ] ); // NOROT, NOFLIP
                    blobCanMoveDown = true;
                }
            }
        }
    }
}

// See this file's own header comment - upstream's real `IsOneBlobDropPossible()`
// falls off the end with no return value at all when the outer condition is
// true but the inner loop never matches; `return false;` added explicitly.
bool blobIsOneDropPossible( int arrayX, int arrayY )
{
    if( ( blobCurrentBlobs[1] == 0 ) && ( blobCurrentBlobs[7] == 0 ) )
    {
        int temp;
        for( temp = 3; temp < 6; temp = temp + 1 )
        {
            if( ( blobCurrentBlobs[ temp ] != 0 ) && ( blobIsTileFree( arrayX, arrayY ) == false ) )
              return true;
        }
        return false;
    }
    else return false;
}

void blobStore( int arrayX, int arrayY )
{
    int drawPointer = 0;
    int y, x;
    for( y = arrayY; y < arrayY + BLOB_TILES_IN_BLOBS; y = y + 1 )
    {
        for( x = arrayX; x < arrayX + BLOB_TILES_IN_BLOBS; x = x + 1 )
        {
            if( blobCurrentBlobs[ drawPointer ] != 0 )
              blobField[ x ][ y ] = blobCurrentBlobs[ drawPointer ];
            drawPointer = drawPointer + 1;
        }
    }
}

void blobRemoveGroups()
{
    int x, y;
    for( x = 0; x < BLOB_PLAYFIELD_WIDTH; x = x + 1 )
    {
        for( y = 0; y < BLOB_PLAYFIELD_HEIGHT; y = y + 1 )
        {
            if( blobFieldFlags[ x ][ y ] == BLOB_FLAG )
            {
                blobGiveExtraScore = true;
                blobField[ x ][ y ] = BLOB_FREE;
                blobScore = blobScore + 50;
            }
        }
    }

    if( blobGiveExtraScore == true )
    {
        blobScore = blobScore + blobExtraScoreForChain;
        blobExtraScoreForChain = blobExtraScoreForChain + 500;
        // real upstream `delay(100)` between these two notes dropped - see
        // this file's own header comment (no blocking-delay primitive here)
        gbPlayNote( 440, 2 );
        gbPlayNote( 1047, 2 );
    }
    blobGiveExtraScore = false;
    blobRemoveFlag();
}

void blobDeletePossible()
{
    while( blobCanMoveDown )
    {
        blobFourInPack();
        blobFourInColumn();
        blobFourInRow();
        blobRemoveGroups();
        blobMoveDown();
    }
    blobCanMoveDown = true;
}

void blobStoreOne( int arrayX, int arrayY )
{
    // if the blob is not on the floor
    if( arrayY < BLOB_PLAYFIELD_HEIGHT - 2 )
    {
        int x;
        for( x = 0; x < BLOB_TILES_IN_BLOBS; x = x + 1 )
        {
            if( ( !blobIsTileFree( arrayX + x, arrayY + 2 ) ) && ( !blobIsOnlyOne() ) && ( blobCurrentBlobs[ 3 + x ] != 0 ) )
            {
                blobField[ arrayX + x ][ arrayY + 1 ] = blobCurrentBlobs[ 3 + x ];
                blobCurrentBlobs[ 3 + x ] = 0;
            }
        }
    }
}

void blobDrop()
{
    if( blobIsOneDropPossible( blobXY[0], blobXY[1] + 1 ) )
      blobStoreOne( blobXY[0], blobXY[1] ); // real upstream passes blobXY[1] here, not +1 - preserved verbatim

    // move down is no longer possible because the field is full, the game is over
    if( ( blobXY[1] == 0 ) && !blobIsTileFree( blobXY[0] + 1, 0 ) )
      blobState = BLOB_STATE_GAME_OVER;

    if( blobIsMovePossible( blobXY[0], blobXY[1] + 1 ) )
    {
        blobXY[1] = blobXY[1] + 1;
        gbPlayTick();
    }
    else if( blobState != BLOB_STATE_GAME_OVER )
    {
        blobStore( blobXY[0], blobXY[1] );
        blobScore = blobScore + 10;
        blobDeletePossible();
        blobCreateCurrentBlobs();
    }
}

void blobBeginTitle( int returnState )
{
    blobTitleReturnState = returnState;
    blobState = BLOB_STATE_TITLE;
}

void blobUpdateTitle()
{
    gbCursorX = 20;
    gbCursorY = 16;
    gbPrintString( "BLOB ATTACK" );
    gbCursorX = 28;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      blobState = blobTitleReturnState;
}

void blobUpdateMainMenu()
{
    // real upstream re-runs this entire reset every single frame this state
    // is active, not just on entry - see this file's own header comment for
    // why that makes gameBlobAttack_init()'s own setup()-time pre-init dead
    // code, and why this is preserved here exactly as upstream wrote it
    blobFillPit();
    blobCreateCurrentBlobs();
    blobRemoveFlag();
    blobInitPlayfield();
    blobCanMoveDown = true;
    blobGiveExtraScore = false;
    blobScore = 0;
    blobExtraScoreForChain = 0;

    gbDrawBitmap( 0, 26, blobSplash1Bitmap );
    gbDrawBitmap( 0, 0, blobSplash2Bitmap );

    gbCursorX = LCDWIDTH - 16;
    gbCursorY = 13;
    gbPrintString( "HELP" );
    gbCursorX = LCDWIDTH - 16;
    gbCursorY = 21;
    gbPrintString( "INFO" );
    gbCursorX = LCDWIDTH - 16;
    gbCursorY = 29;
    gbPrintString( "PLAY" );

    if( gbPressed( BTN_DOWN ) ) blobSelector = blobSelector + 1;
    if( gbPressed( BTN_UP ) ) blobSelector = blobSelector - 1;
    if( blobSelector < 0 ) blobSelector = 0;
    if( blobSelector > 2 ) blobSelector = 2;

    if( blobSelector == 0 )
    {
        if( gbPressed( BTN_A ) ) blobState = BLOB_STATE_HELP;
    }
    else if( blobSelector == 1 )
    {
        if( gbPressed( BTN_A ) ) blobState = BLOB_STATE_INFO;
    }
    else
    {
        if( gbPressed( BTN_A ) ) blobState = BLOB_STATE_PLAYING;
    }

    gbDrawLine( LCDWIDTH - 16, 11 + ( blobSelector * 8 ), LCDWIDTH, 11 + ( blobSelector * 8 ) );
}

void blobUpdatePlaying()
{
    // upstream checks Button C in the middle of this case (after DrawField()/
    // the drop timer) then blocks inside gb.titleScreen(); moved to the top
    // with an early return instead - see this file's own header comment
    if( gbPressed( BTN_C ) )
    {
        blobBeginTitle( BLOB_STATE_PLAYING );
        return;
    }

    blobDrawField();

    if( gbFrameCount % 25 == 0 ) blobDrop();
    if( gbPressed( BTN_A ) ) blobRotateLeft();
    if( gbPressed( BTN_B ) ) blobRotateRight();
    if( gbPressed( BTN_RIGHT ) ) blobMoveRight();
    if( gbPressed( BTN_LEFT ) ) blobMoveLeft();
    if( gbPressed( BTN_DOWN ) ) blobDrop();
    if( gbPressed( BTN_UP ) ) blobState = BLOB_STATE_PAUSE;

    blobDrawBlobs( LCDWIDTH - 20, 0, BLOB_NEXT );
    blobDrawCurrent();
}

void blobUpdateHelp()
{
    if( gbPressed( BTN_B ) )
      blobState = BLOB_STATE_MAIN_MENU;
    else
    {
        gbCursorX = 0;
        gbCursorY = 0;
        gbPrintString( "Visit\nhttp://www.team-arg.\norg/BLBA-manual.html\nB to go back" );
    }
}

void blobUpdateInfo()
{
    if( gbPressed( BTN_B ) )
      blobState = BLOB_STATE_MAIN_MENU;
    else
    {
        gbCursorX = 0;
        gbCursorY = 0;
        gbPrintString( "A game made by\nTEAM Arg\nB to go back" );
    }
}

void blobUpdatePause()
{
    if( gbPressed( BTN_B ) )
      blobState = BLOB_STATE_PLAYING;
    else
    {
        gbDrawBitmap( LCDWIDTH / 2 - 9, LCDHEIGHT / 2 - 3, blobPauseBitmap );
        // Fixed here, not preserved - see this file's own header comment
        // for the real upstream cursor-position bug this replaces (no
        // cursorX/cursorY set at all, so the label landed wherever the
        // previous frame's own drawing left the cursor, nowhere near the
        // pause icon it's meant to label). Centered directly below the
        // 24x5 icon, which is itself drawn at (LCDWIDTH/2-9, LCDHEIGHT/2-3).
        gbCursorX = LCDWIDTH / 2 - 18;
        gbCursorY = LCDHEIGHT / 2 + 5;
        gbPrintString( "B to play" );
        gbCursorX = 0;
        gbCursorY = LCDHEIGHT - 5;
        gbPrintString( "Score: " );
        gbPrintNumber( blobScore );
    }
}

void blobUpdateGameOver()
{
    if( gbPressed( BTN_A ) )
      blobState = BLOB_STATE_MAIN_MENU;
    else
    {
        gbDrawBitmap( 0, 26, blobSplash1Bitmap );

        gbCursorX = LCDWIDTH / 2 - 26;
        gbCursorY = 0;
        gbPrintString( "Game Over" );

        gbCursorX = LCDWIDTH / 2 - 21;
        gbCursorY = 7;
        gbPrintString( "Press A" );

        gbCursorX = 0;
        gbCursorY = 14;
        gbPrintString( "Score " );
        gbPrintNumber( blobScore );

        if( blobScore > blobHighScore )
        {
            // Fixed here, not preserved - see this file's own header
            // comment for the real one-byte-EEPROM truncation bug this
            // replaces (any score over 255 read back wrapped after a
            // restart). Saved as a full word instead, with the project's
            // own established fresh-cell sentinel check on load below.
            eeprom_write_word( 0, blobScore );
            blobHighScore = blobScore;
        }

        gbCursorX = 0;
        gbCursorY = 21;
        gbPrintString( "HI-SCORE: " );
        gbPrintNumber( blobHighScore );
    }
}

void gameBlobAttack_init()
{
    gbBegin();
    gbPickRandomSeed();

    blobSelector = 0;

    // Fixed here, not preserved - see this file's own header comment for
    // the real single-byte-EEPROM truncation bug this replaces. Read back
    // as a full word, with an explicit fresh-cell (0xFFFF) sentinel check
    // matching this project's own established EEPROM convention.
    blobHighScore = eeprom_read_word( 0 );
    if( blobHighScore == 65535 ) blobHighScore = 0;

    blobBeginTitle( BLOB_STATE_MAIN_MENU );
}

void gameBlobAttack_update()
{
    if( !gbUpdate() ) return;

    if( blobState == BLOB_STATE_TITLE ) blobUpdateTitle();
    else if( blobState == BLOB_STATE_MAIN_MENU ) blobUpdateMainMenu();
    else if( blobState == BLOB_STATE_PLAYING ) blobUpdatePlaying();
    else if( blobState == BLOB_STATE_HELP ) blobUpdateHelp();
    else if( blobState == BLOB_STATE_INFO ) blobUpdateInfo();
    else if( blobState == BLOB_STATE_PAUSE ) blobUpdatePause();
    else blobUpdateGameOver();

    gbRenderFrame();
}
