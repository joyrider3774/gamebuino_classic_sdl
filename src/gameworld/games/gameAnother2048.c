#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// another2048 (grafMakulaDer2te, github.com/grafMakulaDer2te/another2048,
// v0.1.11 / Dec 2023 - License: NONE SPECIFIED, confirmed by reading the
// real repo directly: no LICENSE/COPYING file anywhere, and no license
// header in README.md, change.log or the single `another2048.ino` source
// file itself). A real, complete port of the author's own first Gamebuino
// project: a 4x4 sliding-tile 2048 played entirely with the D-pad, B to
// restart.
//
// A GENUINELY DIFFERENT GAME FROM THIS CARTRIDGE'S ALREADY-SHIPPED "2048"
// (game2048.c, Josiah Winslow, prefix `g`) - a completely separate,
// unrelated codebase, shipped side by side the same way this cartridge
// already ships two unrelated Snake games (gameSnakeClassic.c /
// gameSnake5110.c) and two unrelated Asteroids clones (gameAsterocks.c /
// gameAsteroidRipper.c). Both are 4x4 2048 boards, but essentially
// everything past that differs:
//   - PRESENTATION: this game has no tile-face sprites at all - it draws a
//     plain 5-line-by-5-line grid of `fillRect()` rules and prints each
//     tile's own decimal value as font3x5 text inside its cell.
//     game2048.c instead blits one of 18 pre-drawn tile-face bitmaps per
//     cell and animates every freshly spawned tile over 3 real ticks.
//   - SCORING: score counts SPAWNS, not merges - it starts at 4 (the two
//     starting "2" tiles) and grows by exactly the face value of every
//     newly spawned tile (2, or 4 on a 10% roll). Merging tiles is worth
//     nothing at all here. Standard 2048 (and game2048.c) instead score
//     the merged value.
//   - OPENING POSITION: fixed, not random - always exactly two "2" tiles
//     at board row 2, columns 0 and 1 (`gameMatrix_init` upstream, ported
//     verbatim as an2kGameMatrixInit below). game2048.c spawns two random
//     tiles instead.
//   - WIN CONDITION: 16384, not 2048 (upstream's own real check is
//     `> maxNumber` with `maxNumber` 8192 - see an2kCheckVictory()), and
//     it exists purely so the numbers stop overflowing their own cells,
//     per this game's own README.
//   - LOSE CONDITION: there is none. Upstream's own README states this
//     directly ("at the moment there is no game-over screen. If you can't
//     move anymore then you have lost"). Preserved exactly - a stuck board
//     simply stops responding to direction presses and the player restarts
//     with B.
//   - MERGE RULE: a tile CAN merge more than once in a single move here
//     (see the CHAIN MERGES note below) - a real behavioral difference
//     from standard 2048, preserved deliberately.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the full reasoning). `int16_t`/
// `int8_t`/`uint8_t`/`long`/`boolean` all became plain `int`/`bool`
// (avrCompat.h aliasing - this dialect has exactly one 32-bit integer
// width). Upstream's own `switch(dir)` became an if/else-if chain (this
// dialect has no `switch` - see gameStarHonor.c's own header comment) and
// its `(randGenFor4 == 0) ? 4 : 2` ternary became an explicit if/else (no
// ternary operator in this dialect). Upstream's own `enum directions {
// dowwn, lefft, uup, righht }` (the doubled letters are upstream's own
// real spellings, presumably dodging Arduino macro collisions) is ported
// as a prefixed enum with the same ordinal values. `gb.pickRandomSeed()`
// became `gbPickRandomSeed()`, a documented no-op; `gb.battery.show =
// false;` was dropped outright (purely cosmetic on real hardware,
// no equivalent exists or is needed here) - both matching every other port
// in this project. The seven `#define BTN_*_PIN`/`#define SCR_*` pin
// constants and `#define BuzzerPin` at the top of the real sketch are
// dropped: they are real hardware pin numbers, never referenced anywhere
// in the sketch's own code (and each is written with a stray trailing
// semicolon inside the macro body upstream, so they could not have been
// used in an expression even if something had tried).
//
// TITLE SCREEN: upstream's own blocking `gb.titleScreen(F(""),
// TITLESCREEN)` - called once from setup(), and again (still blocking,
// mid-loop) whenever Button B is pressed to restart - is converted into an
// explicit AN2K_STATE_TITLE state dismissed by a genuine `gbPressed(BTN_A)`,
// the same "blocking widget -> explicit resumable state" treatment used
// throughout this project (see gamePong.c's own header comment). The real
// `Gamebuino::titleScreen()` library function draws its logo bitmap at a
// fixed (0,12) screen anchor - confirmed directly against the real
// Gamebuino.cpp source during gameFlappyBirdo.c's/gameUfoRace.c's/
// gameArmageddon.c's own ports of that same real function, and reused
// unchanged here. This game's own logo is 48x32, so at (0,12) it occupies
// y 12..43 and x 0..47 with zero clipping (left-aligned, exactly as real
// hardware draws it - not re-centered). Real `titleScreen()` draws no text
// of its own beyond the name string it is passed, and upstream passes a
// literal empty one (`#define initScreenText ""`, with the game's real name
// commented out right beside it in the real source) - so the "PRESS A"
// prompt drawn here is this port's own added UI affordance, exactly as
// every other port restoring a real `titleScreen()` call has needed.
//
// RESTART ORDERING PRESERVED: upstream's own Button B branch calls the
// blocking `titleScreen()` FIRST and only resets `score`/`gameMatrix`
// afterward, once the title screen returns - so the old board is still in
// memory (though not on screen) for as long as the title is up. Reproduced
// exactly via an2kRestartPending: B enters AN2K_STATE_TITLE, and the reset
// runs at the moment Button A dismisses it, not at the moment B is pressed.
//
// THE 100ms millis() DEBOUNCE -> A 2-TICK DEBOUNCE: upstream runs its whole
// input block OUTSIDE `if(gb.update())`, i.e. on every single free-spinning
// `loop()` iteration (thousands per second on real hardware), while
// `gb.buttons.pressed()` only refreshes once per real 20fps frame - so
// without the debounce a single press would fire hundreds of moves. Its own
// real `debounceNextMillis = millis() + 100` therefore does double duty:
// it collapses a press to one move, and it additionally locks out any
// further move for the rest of that 100ms window. Here the input block runs
// once per real logic tick instead (this platform calls a game's update
// function once per engine frame, with no free-spinning inner loop), where
// `gbPressed()` is already a true one-tick edge - so the first job is
// handled for free, but the second is real, observable behavior worth
// keeping: at real hardware's own 20fps default (50ms/tick), a 100ms
// lockout starting mid-tick N blocks tick N+1 outright and expires in time
// for tick N+2. Ported as exactly that: an2kDebounceUntilFrame =
// gbFrameCount + 2, so a direction press landing on the tick immediately
// after a move is genuinely dropped, matching real hardware rather than
// silently making the game twice as responsive as the real thing.
//
// THE RANDOM-PLACEMENT COUNTERS ARE REPLACED WITH arand(), DELIBERATELY:
// upstream has no real RNG call anywhere. Instead it increments two plain
// counters (`randGen`, and `randGenFor4` kept modulo 10) once per free-
// spinning `loop()` iteration, and samples them at spawn time -
// `positive_modulo(randGen, emptyFilds)` picks which empty cell gets the
// new tile, and `randGenFor4 == 0` decides a 10% chance of spawning a 4
// instead of a 2 (the real 10% figure is upstream's own, stated in its own
// change.log entry 0.1.5). On real hardware, with many thousands of loop
// iterations elapsing between two player moves, both counters are
// effectively uniform noise at the moment they're read - which is exactly
// what those two formulas are standing in for. Reproducing the counters
// literally here would NOT reproduce that: this port's own "loop()" runs at
// the fixed 60Hz engine frame rate, and moves can only ever happen on a
// 20fps logic tick, so `randGen` would advance by an exact multiple of 3
// between any two spawns - freezing `randGen % 3` at one constant value for
// an entire session, and making the cell choice fully deterministic
// whenever the board has exactly 3 (or 6, 9, 12, 15) empty cells left,
// precisely the tight late-game boards where placement matters most. So
// both counters are dropped and replaced with `arand(emptyFilds)` and
// `arand(10) == 0` (this dialect's own established RNG helper - see
// avrCompat.h), which reproduce real hardware's own OBSERVABLE behavior
// (uniform empty-cell choice, 10% chance of a 4) rather than its exact
// counter arithmetic. Upstream's own `positive_modulo()` helper existed
// only to keep those two counters non-negative and is dropped with them.
//
// CHAIN MERGES IN A SINGLE MOVE - A REAL UPSTREAM QUIRK, PRESERVED:
// an2kMoveMatrix() is a literal, line-for-line port of upstream's own
// `moveMatrix()`, including the fact that it does not mark a merged tile as
// spent for the rest of the move the way standard 2048 does. Traced by hand
// on a real column before deciding to keep it: a DOWN move on the column
// [4,2,2,0] (top to bottom) gives [0,0,0,8] here - the two 2s merge into a
// 4, and that brand-new 4 immediately merges again with the pre-existing 4
// above it in the same move. Standard 2048 (and this cartridge's own
// game2048.c) would produce [0,0,4,4]. Preserved exactly - it is real,
// shipped gameplay, and it is a large part of why this game reaches high
// tile values much faster than a conventional 2048 does.
//
// TWO REAL COSMETIC GRID QUIRKS, PRESERVED: upstream's own grid loop draws
// its 5 horizontal rules 72px wide starting at x=5 (so they end at x=76)
// but places its 5 vertical rules at x = 5,23,41,59,77 - so the rightmost
// vertical rule sits one pixel past where the horizontal rules stop, and
// the four corner joins on that edge are open. The same is true one axis
// over: the vertical rules are 36px tall from y=2 (ending at y=37) while
// the bottom horizontal rule sits at y=38. Both reproduced exactly, since
// both come straight from upstream's own real `4 * gridXstepWidth` /
// `4 * gridYstepWidth` extents.
//
// A REAL "NO WAY BACK" STATE AFTER WINNING, PRESERVED: upstream gates its
// ENTIRE input block on `!victory`, Button B's own restart branch included -
// so once the "Victory!" box appears (a real 16384 tile), nothing on the
// real hardware can restart the game short of a power cycle. Kept exactly
// as upstream wrote it rather than "fixed", because unlike this project's
// own genuine soft-lock exceptions (see gamePetitMonstre.c/
// gameUnderTheTower.c), there is a real, always-available way out here: this
// cartridge's own global Start-button quit-confirmation dialog returns to
// the shared game menu from any state, and relaunching runs
// gameAnother2048_init() fresh - the exact equivalent of the power cycle a
// real Gamebuino owner would do.
//
// A REAL COLOR-STATE LEAK, PRESERVED: upstream's own victory branch ends on
// `setColor(BLACK, WHITE)` (not the plain single-argument `setColor(BLACK)`
// that real Display's own constructor default uses), so from the first
// victory frame onward every later text draw - the tile numbers and the
// score line included - renders with a real opaque WHITE background instead
// of transparent. Reproduced literally via gbSetColorBg(), same as
// gamePong2017.c's own preserved GRAY leak. gameAnother2048_init() calls
// `gbSetColor(GB_BLACK)` (which sets color and background alike, exactly
// like real `Display::Display()`'s own `setColor(BLACK)`) so a freshly
// launched game always starts from real hardware's own default state rather
// than inheriting whatever background color the previously played game in
// this cartridge happened to leave behind.
//
// SOUND: upstream's only sound call is a single real `gb.sound.playTick()`
// on every accepted move - ported directly as `gbPlayTick()`. No
// approximation needed, and no shim gap.
//
// EEPROM: upstream has none - no `#include <EEPROM.h>`, no `EEPROM.read()`/
// `.write()` call anywhere in the real source (confirmed by direct grep),
// and no highscore concept of any kind. None was invented, matching this
// project's own "don't invent a highscore real upstream never had"
// precedent.
//
// NO SHIM GAPS: every primitive this port needs already exists
// (gbDrawBitmap, gbFillRect, gbPrintString/gbPrintNumber, gbSetFont with
// the real gbFont3x5/gbFont5x7 tables, gbSetColor/gbSetColorBg, gbPressed,
// gbPlayTick, gbFrameCount, arand). Nothing in gamebuinoShim.h/.c or any
// other shared file was changed.

// -----------------------------------------------------------------------------
// Layout constants - upstream's own real #define values, verbatim (prefixed
// for this cartridge's single flat namespace). Every one of these is a
// single literal token upstream, so none of them can suffer the
// unparenthesized-multi-term-macro hazard gameSkibuino.c's own header
// comment documents.
// -----------------------------------------------------------------------------

#define AN2K_NUMBER_START_X 7
#define AN2K_NUMBER_START_Y 4
#define AN2K_NUMBER_X_STEP_WIDTH 18
#define AN2K_NUMBER_Y_STEP_WIDTH 9

#define AN2K_GRID_LINE_WIDTH 1
#define AN2K_GRID_START_X 5
#define AN2K_GRID_START_Y 2
#define AN2K_GRID_X_STEP_WIDTH 18
#define AN2K_GRID_Y_STEP_WIDTH 9

#define AN2K_SCORE_X 5
#define AN2K_SCORE_Y 40

#define AN2K_MAX_NUMBER 8192
#define AN2K_VICTORY_RECT_X 2
#define AN2K_VICTORY_RECT_Y 13
#define AN2K_VICTORY_RECT_WIDTH 80
#define AN2K_VICTORY_RECT_HEIGHT 14
#define AN2K_VICTORY_TEXT_X 19
#define AN2K_VICTORY_TEXT_Y 16

// Upstream's own real `#define debounceDelay 100` (milliseconds), expressed
// in real 20fps logic ticks instead - see this file's own header comment on
// the debounce for the full derivation.
#define AN2K_DEBOUNCE_TICKS 2

// Upstream's own `enum directions { dowwn, lefft, uup, righht }` - same
// ordinal values, upstream's own doubled-letter spellings normalized away
// since nothing here needs to dodge an Arduino macro name.
enum An2kDirection
{
    AN2K_DIR_DOWN = 0,
    AN2K_DIR_LEFT = 1,
    AN2K_DIR_UP = 2,
    AN2K_DIR_RIGHT = 3
};

enum An2kState
{
    AN2K_STATE_TITLE = 0,
    AN2K_STATE_PLAY = 1
};

// -----------------------------------------------------------------------------
// Real upstream art: the 48x32 TITLESCREEN logo bitmap, extracted byte-for-
// byte from the real .ino via a small script (every `B00000000`-style
// Arduino binary literal converted to plain hex - this dialect has no such
// literal syntax) and verified against its own declared 48x32 header
// (6 bytes per row x 32 rows = 192 body bytes) before being trusted.
// gbDrawBitmap()'s own expected format exactly: width, height, then packed
// MSB-first row bytes.
// -----------------------------------------------------------------------------

int an2kTitleBitmap[194]=
{
    48, 32,
    0x00, 0x01, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x41, 0xFF, 0x0F, 0xF8,
    0x00, 0x03, 0xC1, 0xFF, 0x0F, 0xF8,
    0x00, 0x02, 0x41, 0xFF, 0x0F, 0xF8,
    0x00, 0x00, 0x00, 0x07, 0x0E, 0x38,
    0x00, 0x02, 0x40, 0x07, 0x0E, 0x38,
    0x00, 0x03, 0x40, 0x07, 0x0E, 0x38,
    0x00, 0x02, 0xC1, 0xFF, 0x0E, 0x38,
    0x00, 0x02, 0x41, 0xFF, 0x0E, 0x38,
    0x00, 0x00, 0x01, 0xFF, 0x0E, 0x38,
    0x00, 0x01, 0x81, 0xC0, 0x0E, 0x38,
    0x00, 0x02, 0x41, 0xC0, 0x0E, 0x38,
    0x00, 0x02, 0x41, 0xC0, 0x0E, 0x38,
    0x00, 0x01, 0x81, 0xFF, 0x0F, 0xF8,
    0x00, 0x00, 0x01, 0xFF, 0x0F, 0xF8,
    0x00, 0x03, 0xC1, 0xFF, 0x0F, 0xF8,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x01, 0xC7, 0x0F, 0xF8,
    0x00, 0x01, 0x01, 0xC7, 0x0F, 0xF8,
    0x00, 0x00, 0x01, 0xC7, 0x0F, 0xF8,
    0x00, 0x03, 0xC1, 0xC7, 0x0E, 0x38,
    0x00, 0x02, 0x01, 0xC7, 0x0E, 0x38,
    0x00, 0x03, 0x01, 0xC7, 0x0E, 0x38,
    0x00, 0x02, 0x01, 0xFF, 0x0F, 0xF8,
    0x00, 0x03, 0xC1, 0xFF, 0x0F, 0xF8,
    0x00, 0x00, 0x01, 0xFF, 0x0F, 0xF8,
    0x00, 0x03, 0x80, 0x07, 0x0E, 0x38,
    0x00, 0x02, 0x40, 0x07, 0x0E, 0x38,
    0x00, 0x03, 0x80, 0x07, 0x0E, 0x38,
    0x00, 0x03, 0x00, 0x07, 0x0F, 0xF8,
    0x00, 0x02, 0xC0, 0x07, 0x0F, 0xF8,
    0x00, 0x00, 0x00, 0x07, 0x0F, 0xF8
};

// -----------------------------------------------------------------------------
// State - upstream's own globals, one for one.
// -----------------------------------------------------------------------------

// Upstream's own real `gameMatrix_init` opening position: two "2" tiles at
// row 2, columns 0 and 1. Indexed [y][x], exactly like upstream.
int an2kGameMatrixInit[4][4]=
{
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 2, 2, 0, 0 },
    { 0, 0, 0, 0 }
};

int an2kGameMatrix[4][4];
int an2kOldMatrix[4][4];

int an2kScore = 4;
bool an2kVictory = false;

int an2kState = AN2K_STATE_TITLE;
int an2kDebounceUntilFrame = 0;
// Upstream's Button B branch shows the title screen first and only resets
// the board once it is dismissed - see this file's own header comment.
bool an2kRestartPending = false;

// -----------------------------------------------------------------------------
// Board helpers - direct ports of upstream's own same-named functions.
// -----------------------------------------------------------------------------

void an2kCleanGameMatrix()
{
    int ix;
    int iy;

    for( ix = 0; ix < 4; ix++ )
      for( iy = 0; iy < 4; iy++ )
        an2kGameMatrix[ iy ][ ix ] = an2kGameMatrixInit[ iy ][ ix ];
}

void an2kCopyOldMatrix()
{
    int ix;
    int iy;

    for( ix = 0; ix < 4; ix++ )
      for( iy = 0; iy < 4; iy++ )
        an2kOldMatrix[ iy ][ ix ] = an2kGameMatrix[ iy ][ ix ];
}

// True when the board is UNCHANGED since the last an2kCopyOldMatrix() -
// upstream's own (slightly counter-intuitively named) `checkOldMatrix()`,
// used as `if(!checkOldMatrix()) randomPlaceNumber();` i.e. "only spawn a
// new tile if the move actually changed something".
bool an2kCheckOldMatrix()
{
    int ix;
    int iy;

    for( ix = 0; ix < 4; ix++ )
      for( iy = 0; iy < 4; iy++ )
        if( an2kOldMatrix[ iy ][ ix ] != an2kGameMatrix[ iy ][ ix ] )
          return false;

    return true;
}

bool an2kCheckVictory()
{
    int ix;
    int iy;

    for( ix = 0; ix < 4; ix++ )
      for( iy = 0; iy < 4; iy++ )
        if( an2kGameMatrix[ iy ][ ix ] > AN2K_MAX_NUMBER )
          return true;

    return false;
}

// Direct port of upstream's own randomPlaceNumber(), with its counter-based
// pseudo-randomness replaced by arand() - see this file's own header comment
// for the full reasoning. Scan order (ix outer, iy inner) is upstream's own,
// preserved because it defines which empty cell a given index selects.
int an2kRandomPlaceNumber()
{
    int ix;
    int iy;
    int emptyFilds = 0;
    int randPos;

    // first count number of empty fields
    for( ix = 0; ix < 4; ix++ )
      for( iy = 0; iy < 4; iy++ )
        if( an2kGameMatrix[ iy ][ ix ] == 0 )
          emptyFilds++;

    if( emptyFilds == 0 ) return 0; // break if no empty field lefft

    // get random position
    randPos = arand( emptyFilds );

    // place 2 (or, on a 10% roll, 4) at that random position
    for( ix = 0; ix < 4; ix++ )
      for( iy = 0; iy < 4; iy++ )
        if( an2kGameMatrix[ iy ][ ix ] == 0 )
        {
            if( randPos == 0 )
            {
                if( arand( 10 ) == 0 ) an2kGameMatrix[ iy ][ ix ] = 4;
                else an2kGameMatrix[ iy ][ ix ] = 2;

                an2kScore = an2kScore + an2kGameMatrix[ iy ][ ix ];
            }
            randPos--;
        }

    return emptyFilds;
}

// Line-for-line port of upstream's own moveMatrix(), switch flattened to an
// if/else-if chain. Every arithmetic detail (including the chain-merge
// behavior and the harmless self-assignments/zero-writes the second `if`'s
// own else-branch performs on already-empty cells) is upstream's own - see
// this file's own header comment.
void an2kMoveMatrix( int dir )
{
    int ix;
    int iy;
    int nextt;

    if( dir == AN2K_DIR_DOWN )
    {
        for( ix = 0; ix < 4; ix++ )
        {
            for( iy = 3; iy > -1; iy-- ) // go from dowwn to uup
            {
                if( !( iy == 3 ) ) // skip if last
                {
                    nextt = iy + 1;
                    while( ( an2kGameMatrix[ nextt ][ ix ] == 0 ) && ( nextt < 3 ) ) nextt++;

                    if( an2kGameMatrix[ nextt ][ ix ] == 0 )
                    {
                        an2kGameMatrix[ nextt ][ ix ] = an2kGameMatrix[ iy ][ ix ];
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    if( an2kGameMatrix[ nextt ][ ix ] == an2kGameMatrix[ iy ][ ix ] )
                    {
                        an2kGameMatrix[ nextt ][ ix ] = an2kGameMatrix[ nextt ][ ix ] * 2;
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    else
                    {
                        an2kGameMatrix[ nextt - 1 ][ ix ] = an2kGameMatrix[ iy ][ ix ];
                        if( !( ( nextt - 1 ) == iy ) ) an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                }
            }
        }
    }
    else if( dir == AN2K_DIR_UP )
    {
        for( ix = 0; ix < 4; ix++ )
        {
            for( iy = 0; iy < 4; iy++ ) // go from uup to dowwn
            {
                if( !( iy == 0 ) ) // skip if first
                {
                    nextt = iy - 1;
                    while( ( an2kGameMatrix[ nextt ][ ix ] == 0 ) && ( nextt > 0 ) ) nextt--;

                    if( an2kGameMatrix[ nextt ][ ix ] == 0 )
                    {
                        an2kGameMatrix[ nextt ][ ix ] = an2kGameMatrix[ iy ][ ix ];
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    if( an2kGameMatrix[ nextt ][ ix ] == an2kGameMatrix[ iy ][ ix ] )
                    {
                        an2kGameMatrix[ nextt ][ ix ] = an2kGameMatrix[ nextt ][ ix ] * 2;
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    else
                    {
                        an2kGameMatrix[ nextt + 1 ][ ix ] = an2kGameMatrix[ iy ][ ix ];
                        if( !( ( nextt + 1 ) == iy ) ) an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                }
            }
        }
    }
    else if( dir == AN2K_DIR_RIGHT )
    {
        for( ix = 3; ix > -1; ix-- ) // go from righht to lefft
        {
            for( iy = 3; iy > -1; iy-- )
            {
                if( !( ix == 3 ) ) // skip if last
                {
                    nextt = ix + 1;
                    while( ( an2kGameMatrix[ iy ][ nextt ] == 0 ) && ( nextt < 3 ) ) nextt++;

                    if( an2kGameMatrix[ iy ][ nextt ] == 0 )
                    {
                        an2kGameMatrix[ iy ][ nextt ] = an2kGameMatrix[ iy ][ ix ];
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    if( an2kGameMatrix[ iy ][ nextt ] == an2kGameMatrix[ iy ][ ix ] )
                    {
                        an2kGameMatrix[ iy ][ nextt ] = an2kGameMatrix[ iy ][ nextt ] * 2;
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    else
                    {
                        an2kGameMatrix[ iy ][ nextt - 1 ] = an2kGameMatrix[ iy ][ ix ];
                        if( !( ( nextt - 1 ) == ix ) ) an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                }
            }
        }
    }
    else if( dir == AN2K_DIR_LEFT )
    {
        for( ix = 0; ix < 4; ix++ ) // go from lefft to righht
        {
            for( iy = 3; iy > -1; iy-- )
            {
                if( !( ix == 0 ) ) // skip if last
                {
                    nextt = ix - 1;
                    while( ( an2kGameMatrix[ iy ][ nextt ] == 0 ) && ( nextt > 0 ) ) nextt--;

                    if( an2kGameMatrix[ iy ][ nextt ] == 0 )
                    {
                        an2kGameMatrix[ iy ][ nextt ] = an2kGameMatrix[ iy ][ ix ];
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    if( an2kGameMatrix[ iy ][ nextt ] == an2kGameMatrix[ iy ][ ix ] )
                    {
                        an2kGameMatrix[ iy ][ nextt ] = an2kGameMatrix[ iy ][ nextt ] * 2;
                        an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                    else
                    {
                        an2kGameMatrix[ iy ][ nextt + 1 ] = an2kGameMatrix[ iy ][ ix ];
                        if( !( ( nextt + 1 ) == ix ) ) an2kGameMatrix[ iy ][ ix ] = 0;
                    }
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Drawing - direct port of upstream's own drawGameMatrix().
// -----------------------------------------------------------------------------

void an2kDrawGameMatrix()
{
    int ix;
    int iy;
    int it;

    gbSetFont( gbFont3x5 ); // upstream's own `#define numberFont font3x5`

    // draw numbers
    for( ix = 0; ix < 4; ix++ )
    {
        for( iy = 0; iy < 4; iy++ )
        {
            gbCursorX = AN2K_NUMBER_START_X + ( AN2K_NUMBER_X_STEP_WIDTH * ix );
            gbCursorY = AN2K_NUMBER_START_Y + ( AN2K_NUMBER_Y_STEP_WIDTH * iy );
            if( an2kGameMatrix[ iy ][ ix ] > 0 ) gbPrintNumber( an2kGameMatrix[ iy ][ ix ] );
        }
    }

    // draw grid (drawn AFTER the numbers upstream, so a rule can clip a
    // wide 4- or 5-digit tile value - preserved, it is real upstream
    // draw order)
    for( it = 0; it < 5; it++ )
    {
        gbFillRect( AN2K_GRID_START_X, AN2K_GRID_START_Y + ( it * AN2K_GRID_Y_STEP_WIDTH ), 4 * AN2K_GRID_X_STEP_WIDTH, AN2K_GRID_LINE_WIDTH ); // x lines
        gbFillRect( AN2K_GRID_START_X + ( it * AN2K_GRID_X_STEP_WIDTH ), AN2K_GRID_START_Y, AN2K_GRID_LINE_WIDTH, 4 * AN2K_GRID_Y_STEP_WIDTH ); // y lines
    }

    // draw score - upstream builds `"score: " + String(score)` as one
    // Arduino String; split into a literal plus gbPrintNumber() here (this
    // dialect has no String class and no operator overloading), which
    // leaves the cursor advancing identically either way
    gbCursorX = AN2K_SCORE_X;
    gbCursorY = AN2K_SCORE_Y;
    gbPrintString( "score: " );
    gbPrintNumber( an2kScore );

    // victory screen
    if( an2kVictory )
    {
        gbSetColorBg( GB_BLACK, GB_WHITE );
        gbFillRect( AN2K_VICTORY_RECT_X, AN2K_VICTORY_RECT_Y, AN2K_VICTORY_RECT_WIDTH, AN2K_VICTORY_RECT_HEIGHT );
        gbSetColorBg( GB_WHITE, GB_BLACK );
        gbSetFont( gbFont5x7 ); // upstream's own `#define victoryTextFont font5x7`
        gbCursorX = AN2K_VICTORY_TEXT_X;
        gbCursorY = AN2K_VICTORY_TEXT_Y;
        gbPrintString( "Victory!" );
        gbSetColorBg( GB_BLACK, GB_WHITE );
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void an2kUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbSetFont( gbFont5x7 ); // upstream's own `#define initScreenFont font5x7`
    gbDrawBitmap( 0, 12, an2kTitleBitmap );

    gbCursorX = 28;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbPickRandomSeed(); // upstream calls this right after every titleScreen() return

        if( an2kRestartPending )
        {
            an2kRestartPending = false;
            an2kScore = 4;
            an2kCleanGameMatrix();
        }

        an2kState = AN2K_STATE_PLAY;
    }
}

void an2kUpdatePlay()
{
    bool btnPressed = false;

    if( ( gbFrameCount >= an2kDebounceUntilFrame ) && !an2kVictory )
    {
        an2kCopyOldMatrix();

        if( gbPressed( BTN_DOWN ) )
        {
            an2kMoveMatrix( AN2K_DIR_DOWN );
            btnPressed = true;
        }
        if( gbPressed( BTN_UP ) )
        {
            an2kMoveMatrix( AN2K_DIR_UP );
            btnPressed = true;
        }
        if( gbPressed( BTN_LEFT ) )
        {
            an2kMoveMatrix( AN2K_DIR_LEFT );
            btnPressed = true;
        }
        if( gbPressed( BTN_RIGHT ) )
        {
            an2kMoveMatrix( AN2K_DIR_RIGHT );
            btnPressed = true;
        }
        if( gbPressed( BTN_B ) )
        {
            // restart - the board/score reset happens once the title screen
            // is dismissed, matching upstream's own blocking-call ordering
            an2kRestartPending = true;
            an2kState = AN2K_STATE_TITLE;
            return;
        }

        if( btnPressed )
        {
            an2kVictory = an2kCheckVictory();
            an2kDebounceUntilFrame = gbFrameCount + AN2K_DEBOUNCE_TICKS;
            gbPlayTick();
            if( !an2kCheckOldMatrix() ) an2kRandomPlaceNumber();
        }
    }

    an2kDrawGameMatrix();
}

void gameAnother2048_init()
{
    gbBegin();
    // real Display::Display()'s own default - sets color AND background
    // alike, so a launch never inherits another game's leaked background
    gbSetColor( GB_BLACK );
    gbSetFont( gbFont5x7 ); // upstream's own setup()-time setFont(initScreenFont)

    an2kCleanGameMatrix();
    an2kScore = 4;
    an2kVictory = false;
    an2kDebounceUntilFrame = 0;
    an2kRestartPending = false;
    an2kState = AN2K_STATE_TITLE;
}

void gameAnother2048_update()
{
    if( !gbUpdate() ) return;

    if( an2kState == AN2K_STATE_PLAY ) an2kUpdatePlay();
    // re-checked, not chained with `else`: a Button B restart inside
    // an2kUpdatePlay() switches to the title state mid-tick, and this draws
    // it on that same tick instead of leaving one blank frame behind
    if( an2kState == AN2K_STATE_TITLE ) an2kUpdateTitle();

    gbRenderFrame();
}
