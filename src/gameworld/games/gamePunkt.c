#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Punkt (Andy O'Neill, MIT License - github.com/aoneill01/gamebuino-punkt).
// A minimalist dodge/collect arcade game: a small plus-shaped "dot" player
// wanders the board collecting a blinking 2x2 target dot; every collect
// grows the score by one AND spawns one more small square "enemy" dot that
// perpetually slides back and forth (either horizontally or vertically)
// across the whole board, easing in/out near each end rather than moving
// at constant speed. Touch any enemy and it's game over. The same author's
// gameMaze.c (already ported to this project) is a useful style reference,
// though this is a wholly different game/logic base.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the full reasoning). Upstream's own
// `Player`/`Enemy`/`Target` classes (player.h/.cpp, enemy.h/.cpp,
// target.h/.cpp) were flattened into plain global state + free functions
// with a `punkt`-prefixed name each (`punktPlayerX/Y`, `punktEnemyX/Y[]`,
// `punktTargetX/Y`, etc) - `Enemy`/`Target` are logically "many instances"
// upstream (an `Enemy enemies[MAX_ENEMIES]` array and a single `Target`),
// ported here as parallel int arrays (`punktEnemyX[]`/`punktEnemyY[]`/
// `punktEnemyHorizontal[]`/`punktEnemyInitTime[]`) indexed the same way
// upstream indexes `enemies[i]`, rather than as an actual struct array
// (no evidence any other port in this project needed struct arrays, and
// parallel arrays are already this project's own established idiom for
// "many small game objects", e.g. gameMaze.c's own per-cell bit arrays).
// Real `byte`/`unsigned long` fields become plain `int` throughout (this
// dialect has no such types - see gameMaze.c's own header comment on why
// "just use int" costs nothing here). `random(N)` became `arand(N)`;
// upstream's own two-argument `random(min, max)` (in Target::
// moveToNewLocation()) became `min + arand(max - min)`, this dialect's
// own established ranged-random idiom. `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op.
//
// STATE MACHINE, replacing upstream's own MODE_PLAYING/MODE_GAME_OVER/
// MODE_TITLE_SCREEN/MODE_MAIN_MENU/MODE_INSTRUCTIONS: upstream's own
// reset() calls a genuinely BLOCKING `gb.titleScreen(F(""), logoLarge)`
// (once from setup(), and again any time Button C is pressed, at any
// point in the game, via a top-of-loop() `if
// (gb.buttons.pressed(BTN_C)) reset();` check) - converted to an explicit
// PUNKT_STATE_TITLE, dismissed by a genuine fresh Button A press (the
// "blocking loop -> explicit resumable state" treatment used throughout
// this project, see gamePong.c's own header comment). Real titleScreen()
// itself actually draws a lot more than just the passed-in logo (the real
// Gamebuino boot logo, plus A/B/C button-icon hints for "start", "toggle
// volume", and "flash new game from SD" - confirmed by reading the real
// Gamebuino.cpp directly) - none of that is reproduced here, matching
// every other title-screen port in this project's own already-established
// simplification (just the passed logo bitmap plus a plain "PRESS A"
// prompt; this shim has no boot logo, no SD-card game-flashing, and
// global mute is already its own separate Button-Y convention project-
// wide, not a per-titleScreen Button-B toggle). Button C's own "reset to
// title from anywhere, at any time" behavior is preserved as a single
// top-level check in gamePunkt_update() (mirroring upstream's own
// top-of-loop() placement exactly, rather than duplicating the check in
// every one of the 5 state handlers) - re-entering PUNKT_STATE_TITLE this
// way, rather than an instant same-tick passthrough to the main menu the
// way real hardware's single-tick MODE_TITLE_SCREEN case does, exactly
// matching gameMaze.c's own already-established precedent for this same
// class of upstream "blocking mid-game titleScreen() used as a restart
// gesture" (see that file's own header comment).
//
// Upstream's reset() also explicitly calls `gb.setFrameRate(30)` - unlike
// most games ported to this project so far, Punkt does NOT rely on real
// hardware's own 20fps default, so this port calls `gbSetFrameRate(30)`
// once in gamePunkt_init() to match, a genuine intentional choice by this
// game's own author, not a mistake to normalize away.
// `gb.battery.show = false;` was dropped outright (no battery indicator
// exists in this shim at all - purely cosmetic on real hardware, matching
// every other port's own precedent).
//
// FRAME COUNTER: upstream leans on real `gb.frameCount` (an ever-
// incrementing unsigned long exposed directly by real Gamebuino hardware)
// as its own time base for the enemy movement easing curves, the main-menu
// slide-in animation, and the instructions-screen phase timer. Ported here
// via the shim's own `gbFrameCount` global (incremented once per real
// logic tick inside gbUpdate(), matching real hardware's own placement).
//
// REAL EEPROM PERSISTENCE - genuinely ported, not invented: upstream
// really does call `EEPROM.read()`/`EEPROM.write()`/`EEPROM.update()` for
// a persisted high score, so this port genuinely uses this project's own
// `eeprom_read_byte()`/`eeprom_write_byte()`/`eeprom_update_byte()`
// (gameFiremen.c is the only other ported game so far that needed this).
// Punkt's own real upstream uses its own hand-rolled magic-sentinel
// convention to detect a fresh EEPROM (`EEPROM.read(0) == 42`, writing 42
// to address 0 and 0 to address 1 the first time), a genuinely different
// convention from gameFiremen.c's own `eeprom_read_byte(0)==0xff` real-
// factory-erased-cell check - preserved exactly as Punkt's own upstream
// wrote it rather than normalized to match Firemen's own different (but
// equally real) convention.
//
// REAL UPSTREAM QUIRKS FOUND AND HANDLED:
//
// 1. `randomSeed(1)` inside doMainMenu()'s own Button-B ("instructions")
//    handler, specifically so the instructions-screen demo's target/enemy
//    positions look identical every single time (upstream's own comment:
//    "So instructions always look the same."). This dialect exposes no
//    seedable-RNG primitive anywhere (no `srand()`/seed hook in
//    avrCompat.h, matching `gbPickRandomSeed()` itself already being a
//    documented no-op) - there is no way to reproduce this determinism
//    here. Accepted as a real, minor, purely-cosmetic limitation: this
//    port's own instructions demo shows genuinely random positions each
//    time it's opened, instead of always the same ones.
//
// 2. Real byte-wraparound diagonal-movement throttle: `Player::
//    isTimeToMoveDiagonally()` adds DIAG_DELTA=91 (≈128/√2) to a `byte`
//    field every call and checks whether its high bit (0x80) just flipped
//    - real hardware's own `byte` type wraps this addition at 256 for
//    free. This dialect has no byte type (see above), so
//    `punktIsTimeToMoveDiagonally()` explicitly masks with `& 0xFF` after
//    every add to reproduce the exact same real wraparound bit-for-bit,
//    preserving the exact real "diagonal moves land roughly 91/128 of the
//    time, approximating true 1/√2 diagonal speed" algorithm.
//
// 3. Real uninitialized-member quirk on `player = Player();`/`target =
//    Target();` (upstream's own reset-via-reassignment idiom, used in
//    initializeGame() and doMainMenu()'s instructions setup): neither
//    class's constructor ever explicitly initializes `_diagonalTimer`/
//    `_haloTimer`, so after the very first, globally-zero-initialized
//    instance, every subsequent reassignment copies in whatever
//    indeterminate stack garbage the temporary object's own un-initialized
//    member happened to hold - a real, if basically harmless (it only
//    changes each animation's exact starting phase, not its behavior),
//    upstream implementation artifact. This dialect has no concept of
//    "uninitialized stack garbage" to faithfully reproduce, so
//    `punktPlayerReset()`/game-init explicitly zero these fields instead -
//    the same "normalize a meaningless implementation artifact rather than
//    fake an unreproducible one" call gameAgaruino.c/gameMaze.c's own
//    header comments already made for similar no-gameplay-impact quirks.
//
// 4. `Enemy::isHit()`/`Target::isHit()`/the game-over box's own AABB
//    overlap tests are all the exact same real formula real Gamebuino's
//    own `Gamebuino::collideRectRect()` implements
//    (`gbCollideRectRect()` here) - substituted for that shim helper
//    directly at both real call sites instead of reproducing the same
//    four-comparison expression by hand three separate times.
//
// 5. Target's real halo ring uses real `GRAY` (`Display::setColor(GRAY)`),
//    ported directly as `gbSetColor(GB_GRAY)`.
//
// 6. doGameOver()'s own real, minor cosmetic bug, preserved verbatim
//    (project norm: preserve real upstream bugs unless told otherwise):
//    the game-over box's own left edge is computed as
//    `(BOARD_WIDTH - ((4*9)+1)) >> 1`, but the "Game Over" text's own
//    cursorX right next to it is computed with different paren grouping,
//    `(BOARD_WIDTH - (4*9) + 1) >> 1` - these are NOT the same formula
//    (37 subtracted vs 36 subtracted, then +1 outside vs inside the
//    subtraction), landing the text 1px right of the box's own true left
//    edge instead of perfectly centered inside it. Kept exactly as
//    upstream wrote it below, not "fixed" into different, never-actually-
//    shipped behavior.
//
// TEXT/FONT: upstream calls `gb.display.setFont(font3x5)` (real hardware's
// own default anyway) for all body text, and `gb.display.setFont(font5x7)`
// only for the two blinking menu-selector icon glyphs on the main menu -
// ported as real `gbSetFont(gbFont3x5)`/`gbSetFont(gbFont5x7)` calls at
// the exact same real call sites. Those two icon glyphs are real
// Gamebuino font5x7 octal-escape char literals (`'\25'`/`'\26'`, decimal
// 21/22 - low-range icon glyphs, not printable ASCII); ported via a small
// `punktPrintChar()` helper (this shim's own `gbPrintString()` needs a
// 0-terminated int array, not a bare int - the exact same pattern
// gameSnakeAbc.c's own `sabcPrintChar()` already established), passing
// plain decimal literals (21/22) rather than char-literal syntax, matching
// that same file's own established "don't rely on unproven char-literal-
// in-numeric-context support in this dialect" convention.
//
// Upstream's own `do { target.moveToNewLocation(); } while
// (target.isHit(...)); ` (re-roll the target's spawn point until it isn't
// already sitting on the player) has no proven do-while precedent
// anywhere else in this project, so it's ported as an equivalent
// call-once-then-while-retry instead (identical real behavior, using only
// this dialect's already-proven `while` construct).

#define PUNKT_GUTTER_WIDTH 9
// Matches real upstream's own exact macro text (constants.h: `#define
// BOARD_WIDTH LCDWIDTH - GUTTER_WIDTH`, no outer parens) rather than a
// fully-parenthesized "cleanup" - see gameSkibuino.c's own header comment
// for why this project now checks every upstream #define's real
// parenthesization (or lack thereof) explicitly, after a real bug was
// found there from exactly this kind of silent "improvement". Verified
// safe either way at every real usage site in this file (every one has
// PUNKT_BOARD_WIDTH/PUNKT_BOARD_HEIGHT as the leftmost operand of a
// +/- chain, or as a bare `x = x - MACRO`-style rewrite of a real
// upstream `-=`, both of which are unaffected by the macro's own internal
// grouping) - matched exactly anyway, on request, rather than relying on
// that per-site analysis.
#define PUNKT_BOARD_WIDTH LCDWIDTH - PUNKT_GUTTER_WIDTH
#define PUNKT_BOARD_HEIGHT LCDHEIGHT

#define PUNKT_MAX_ENEMIES 50

#define PUNKT_PLAYER_SIZE 4
#define PUNKT_DIAG_DELTA 91

#define PUNKT_ENEMY_SIZE 2
#define PUNKT_SLOW_DISTANCE 16
#define PUNKT_SLOW_TIME 64
#define PUNKT_HORIZ_CONST_TIME PUNKT_BOARD_WIDTH - PUNKT_ENEMY_SIZE - 2 * PUNKT_SLOW_DISTANCE
#define PUNKT_VERT_CONST_TIME PUNKT_BOARD_HEIGHT - PUNKT_ENEMY_SIZE - 2 * PUNKT_SLOW_DISTANCE

#define PUNKT_TARGET_SIZE 2

#define PUNKT_TITLE_X ( ( LCDWIDTH - ( 5 * 6 ) ) >> 1 )

// Real upstream `logo[] PROGMEM` (32x11, shown sliding in on the main
// menu) - copied byte-for-byte (already plain 0x-hex literals upstream,
// no Arduino B-binary literals, so no bit-conversion was needed). Header
// cells 0/1 are width/height (matching gbDrawBitmap()'s own documented
// format), followed by ceil(32/8)*11 = 44 row-major, MSB-first data bytes.
int punktLogoBitmap[46]= { 32, 11,
    0x0, 0x0, 0x73, 0x80,
    0x0, 0x0, 0x52, 0x80,
    0x67, 0xFF, 0xDE, 0xC0,
    0xF4, 0x54, 0x54, 0x40,
    0xF5, 0x55, 0x4E, 0xC0,
    0x65, 0x55, 0x56, 0x80,
    0x5, 0x55, 0x56, 0x80,
    0x4, 0x45, 0x56, 0x80,
    0x5, 0xFF, 0xFF, 0x80,
    0x5, 0x0, 0x0, 0x0,
    0x7, 0x0, 0x0, 0x0
};

// Real upstream `logoLarge[] PROGMEM` (64x29, the real title-screen splash
// passed to the blocking `gb.titleScreen(F(""), logoLarge)`) - copied
// byte-for-byte the same way. Header cells 0/1 are width/height, followed
// by ceil(64/8)*29 = 232 row-major, MSB-first data bytes.
int punktLogoLargeBitmap[234]= { 64, 29,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0xFC, 0x3F, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0xFC, 0x3F, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0xCC, 0x33, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0xCC, 0x33, 0x0,
    0x0, 0xF0, 0xFF, 0xFF, 0xFF, 0xCF, 0xF3, 0xC0,
    0x0, 0xF0, 0xFF, 0xFF, 0xFF, 0xCF, 0xF3, 0xC0,
    0x3, 0xFC, 0xC0, 0xCC, 0xC0, 0xCC, 0xC0, 0xC0,
    0x3, 0xFC, 0xC0, 0xCC, 0xC0, 0xCC, 0xC0, 0xC0,
    0x3, 0xFC, 0xCC, 0xCC, 0xCC, 0xC3, 0xF3, 0xC0,
    0x3, 0xFC, 0xCC, 0xCC, 0xCC, 0xC3, 0xF3, 0xC0,
    0x0, 0xF0, 0xCC, 0xCC, 0xCC, 0xCC, 0xF3, 0x0,
    0x0, 0xF0, 0xCC, 0xCC, 0xCC, 0xCC, 0xF3, 0x0,
    0x0, 0x0, 0xCC, 0xCC, 0xCC, 0xCC, 0xF3, 0x0,
    0x0, 0x0, 0xCC, 0xCC, 0xCC, 0xCC, 0xF3, 0x0,
    0x0, 0x0, 0xC0, 0xC0, 0xCC, 0xCC, 0xF3, 0x0,
    0x0, 0x0, 0xC0, 0xC0, 0xCC, 0xCC, 0xF3, 0x0,
    0x0, 0x0, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0,
    0x0, 0x0, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0,
    0x0, 0x0, 0xCC, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xCC, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xFC, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xFC, 0x0, 0x0, 0x0, 0x0, 0x0
};

int punktReferenceTime;

int punktScore;
int punktHighScore;

int punktPlayerX;
int punktPlayerY;
int punktPlayerDiagonalTimer;

int punktTargetX;
int punktTargetY;
int punktTargetHaloTimer;

int punktEnemyX[PUNKT_MAX_ENEMIES];
int punktEnemyY[PUNKT_MAX_ENEMIES];
int punktEnemyHorizontal[PUNKT_MAX_ENEMIES];
int punktEnemyInitTime[PUNKT_MAX_ENEMIES];
int punktEnemyCount;

enum PunktState
{
    PUNKT_STATE_TITLE = 0,
    PUNKT_STATE_MAIN_MENU = 1,
    PUNKT_STATE_PLAYING = 2,
    PUNKT_STATE_GAME_OVER = 3,
    PUNKT_STATE_INSTRUCTIONS = 4
};

int punktState;

// Prints a single character at the current cursor position - this shim's
// own gbPrintString() needs a 0-terminated int array, not a bare int (see
// this file's own header comment).
void punktPrintChar( int ch )
{
    char buf[2];
    buf[ 0 ] = ch;
    buf[ 1 ] = 0;
    gbPrintString( buf );
}

// -----------------------------------------------------------------------
//   Player
// -----------------------------------------------------------------------

void punktPlayerReset()
{
    punktPlayerX = ( PUNKT_BOARD_WIDTH - PUNKT_PLAYER_SIZE ) >> 1;
    punktPlayerY = ( PUNKT_BOARD_HEIGHT - PUNKT_PLAYER_SIZE ) >> 1;
    // See this file's own header comment on normalizing away a real
    // uninitialized-member quirk here (upstream never explicitly resets
    // this field, relying on stack-garbage after the first reset).
    punktPlayerDiagonalTimer = 0;
}

// Real 1/sqrt(2) ≈ 91/128 diagonal-speed throttle: if the high-order bit
// flips when adding DIAG_DELTA to a byte-wrapping counter, move diagonally
// this tick. See this file's own header comment on the explicit `& 0xFF`
// masking needed to reproduce real hardware's own free byte wraparound.
bool punktIsTimeToMoveDiagonally()
{
    int before = punktPlayerDiagonalTimer;
    punktPlayerDiagonalTimer = ( punktPlayerDiagonalTimer + PUNKT_DIAG_DELTA ) & 0xFF;
    return ( before & 0x80 ) != ( punktPlayerDiagonalTimer & 0x80 );
}

void punktPlayerUpdate()
{
    if( gbRepeat( BTN_UP, 1 ) )
    {
        if( gbRepeat( BTN_LEFT, 1 ) )
        {
            if( punktIsTimeToMoveDiagonally() )
            {
                if( punktPlayerY > 0 ) punktPlayerY = punktPlayerY - 1;
                if( punktPlayerX > 0 ) punktPlayerX = punktPlayerX - 1;
            }
        }
        else if( gbRepeat( BTN_RIGHT, 1 ) )
        {
            if( punktIsTimeToMoveDiagonally() )
            {
                if( punktPlayerY > 0 ) punktPlayerY = punktPlayerY - 1;
                if( punktPlayerX < PUNKT_BOARD_WIDTH - PUNKT_PLAYER_SIZE ) punktPlayerX = punktPlayerX + 1;
            }
        }
        else
        {
            if( punktPlayerY > 0 ) punktPlayerY = punktPlayerY - 1;
        }
    }
    else if( gbRepeat( BTN_DOWN, 1 ) )
    {
        if( gbRepeat( BTN_LEFT, 1 ) )
        {
            if( punktIsTimeToMoveDiagonally() )
            {
                if( punktPlayerY < PUNKT_BOARD_HEIGHT - PUNKT_PLAYER_SIZE ) punktPlayerY = punktPlayerY + 1;
                if( punktPlayerX > 0 ) punktPlayerX = punktPlayerX - 1;
            }
        }
        else if( gbRepeat( BTN_RIGHT, 1 ) )
        {
            if( punktIsTimeToMoveDiagonally() )
            {
                if( punktPlayerY < PUNKT_BOARD_HEIGHT - PUNKT_PLAYER_SIZE ) punktPlayerY = punktPlayerY + 1;
                if( punktPlayerX < PUNKT_BOARD_WIDTH - PUNKT_PLAYER_SIZE ) punktPlayerX = punktPlayerX + 1;
            }
        }
        else
        {
            if( punktPlayerY < PUNKT_BOARD_HEIGHT - PUNKT_PLAYER_SIZE ) punktPlayerY = punktPlayerY + 1;
        }
    }
    else if( gbRepeat( BTN_LEFT, 1 ) )
    {
        if( punktPlayerX > 0 ) punktPlayerX = punktPlayerX - 1;
    }
    else if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        if( punktPlayerX < PUNKT_BOARD_WIDTH - PUNKT_PLAYER_SIZE ) punktPlayerX = punktPlayerX + 1;
    }
}

// Real upstream draws a plus/cross shape (two overlapping thin rects) with
// setColor(BLACK, BLACK) (the two-arg overload also sets the background,
// harmless here since a fillRect never reads it) - ported verbatim via
// gbSetColorBg().
void punktPlayerDraw()
{
    gbSetColorBg( 1, 1 );
    gbFillRect( punktPlayerX + 1, punktPlayerY, 2, PUNKT_PLAYER_SIZE );
    gbFillRect( punktPlayerX, punktPlayerY + 1, PUNKT_PLAYER_SIZE, 2 );
}

// -----------------------------------------------------------------------
//   Enemies (parallel arrays - see this file's own header comment)
// -----------------------------------------------------------------------

void punktEnemyUpdate( int idx )
{
    int curTime = gbFrameCount;
    int t;

    if( punktEnemyHorizontal[ idx ] )
    {
        int period = PUNKT_HORIZ_CONST_TIME + PUNKT_HORIZ_CONST_TIME + PUNKT_SLOW_TIME + PUNKT_SLOW_TIME;
        t = ( curTime - punktEnemyInitTime[ idx ] ) % period;

        if( t < PUNKT_HORIZ_CONST_TIME )
        {
            punktEnemyX[ idx ] = PUNKT_SLOW_DISTANCE + t;
        }
        else if( t < PUNKT_HORIZ_CONST_TIME + PUNKT_SLOW_TIME )
        {
            t = t - PUNKT_HORIZ_CONST_TIME;
            punktEnemyX[ idx ] = -1 * ( ( t * t ) >> 6 ) + t + PUNKT_SLOW_DISTANCE + PUNKT_HORIZ_CONST_TIME;
        }
        else if( t < PUNKT_HORIZ_CONST_TIME + PUNKT_HORIZ_CONST_TIME + PUNKT_SLOW_TIME )
        {
            t = t - ( PUNKT_HORIZ_CONST_TIME + PUNKT_SLOW_TIME );
            punktEnemyX[ idx ] = PUNKT_SLOW_DISTANCE + PUNKT_HORIZ_CONST_TIME - t;
        }
        else
        {
            t = t - ( PUNKT_HORIZ_CONST_TIME + PUNKT_HORIZ_CONST_TIME + PUNKT_SLOW_TIME );
            punktEnemyX[ idx ] = ( ( t * t ) >> 6 ) - t + PUNKT_SLOW_DISTANCE;
        }
    }
    else
    {
        int period = PUNKT_VERT_CONST_TIME + PUNKT_VERT_CONST_TIME + PUNKT_SLOW_TIME + PUNKT_SLOW_TIME;
        t = ( curTime - punktEnemyInitTime[ idx ] ) % period;

        if( t < PUNKT_VERT_CONST_TIME )
        {
            punktEnemyY[ idx ] = PUNKT_SLOW_DISTANCE + t;
        }
        else if( t < PUNKT_VERT_CONST_TIME + PUNKT_SLOW_TIME )
        {
            t = t - PUNKT_VERT_CONST_TIME;
            punktEnemyY[ idx ] = -1 * ( ( t * t ) >> 6 ) + t + PUNKT_SLOW_DISTANCE + PUNKT_VERT_CONST_TIME;
        }
        else if( t < PUNKT_VERT_CONST_TIME + PUNKT_VERT_CONST_TIME + PUNKT_SLOW_TIME )
        {
            t = t - ( PUNKT_VERT_CONST_TIME + PUNKT_SLOW_TIME );
            punktEnemyY[ idx ] = PUNKT_SLOW_DISTANCE + PUNKT_VERT_CONST_TIME - t;
        }
        else
        {
            t = t - ( PUNKT_VERT_CONST_TIME + PUNKT_VERT_CONST_TIME + PUNKT_SLOW_TIME );
            punktEnemyY[ idx ] = ( ( t * t ) >> 6 ) - t + PUNKT_SLOW_DISTANCE;
        }
    }
}

void punktEnemyDraw( int idx )
{
    gbSetColor( 1 );
    gbFillRect( punktEnemyX[ idx ], punktEnemyY[ idx ], PUNKT_ENEMY_SIZE, PUNKT_ENEMY_SIZE );
}

void punktEnemySpawn( int idx, int playerX, int playerY )
{
    int curTime = gbFrameCount;

    if( arand( PUNKT_BOARD_WIDTH + PUNKT_BOARD_HEIGHT ) < PUNKT_BOARD_HEIGHT )
      punktEnemyHorizontal[ idx ] = 1;
    else
      punktEnemyHorizontal[ idx ] = 0;

    if( punktEnemyHorizontal[ idx ] )
    {
        punktEnemyY[ idx ] = arand( PUNKT_BOARD_HEIGHT - PUNKT_ENEMY_SIZE );
        punktEnemyInitTime[ idx ] = curTime - ( PUNKT_HORIZ_CONST_TIME + ( PUNKT_SLOW_TIME >> 1 ) );
        // Make sure it doesn't spawn close to the player
        if( playerX > ( PUNKT_BOARD_WIDTH >> 1 ) )
          punktEnemyInitTime[ idx ] = punktEnemyInitTime[ idx ] - ( PUNKT_HORIZ_CONST_TIME + PUNKT_SLOW_TIME );
    }
    else
    {
        punktEnemyX[ idx ] = arand( PUNKT_BOARD_WIDTH - PUNKT_ENEMY_SIZE );
        punktEnemyInitTime[ idx ] = curTime - ( PUNKT_VERT_CONST_TIME + ( PUNKT_SLOW_TIME >> 1 ) );
        // Make sure it doesn't spawn close to the player
        if( playerY > ( PUNKT_BOARD_HEIGHT >> 1 ) )
          punktEnemyInitTime[ idx ] = punktEnemyInitTime[ idx ] - ( PUNKT_VERT_CONST_TIME + PUNKT_SLOW_TIME );
    }

    punktEnemyUpdate( idx );
}

// -----------------------------------------------------------------------
//   Target
// -----------------------------------------------------------------------

void punktTargetMoveToNewLocation()
{
    // real `random(2, BOARD_WIDTH - TARGET_SIZE - 2)` -> `min + arand(max - min)`
    punktTargetX = 2 + arand( ( PUNKT_BOARD_WIDTH - PUNKT_TARGET_SIZE - 2 ) - 2 );
    punktTargetY = 2 + arand( ( PUNKT_BOARD_HEIGHT - PUNKT_TARGET_SIZE - 2 ) - 2 );
}

void punktTargetDraw()
{
    gbSetColor( 1 );
    gbFillRect( punktTargetX, punktTargetY, PUNKT_TARGET_SIZE, PUNKT_TARGET_SIZE );

    punktTargetHaloTimer = ( punktTargetHaloTimer + 1 ) & 0xFF;
    if( ( punktTargetHaloTimer & 0x08 ) != 0 )
    {
        gbSetColor( GB_GRAY );
        gbDrawFastHLine( punktTargetX - 1, punktTargetY - 2, 4 );
        gbDrawFastHLine( punktTargetX - 1, punktTargetY + 3, 4 );
        gbDrawFastVLine( punktTargetX - 2, punktTargetY - 1, 4 );
        gbDrawFastVLine( punktTargetX + 3, punktTargetY - 1, 4 );
    }
}

// -----------------------------------------------------------------------
//   HUD / board
// -----------------------------------------------------------------------

void punktDrawScore()
{
    gbSetFont( gbFont3x5 );
    gbCursorX = PUNKT_BOARD_WIDTH + 1;
    gbCursorY = 1;
    if( punktScore < 10 ) punktPrintChar( 48 ); // '0'
    gbPrintNumber( punktScore );
}

void punktDrawHighScore()
{
    gbSetFont( gbFont3x5 );
    gbCursorX = PUNKT_BOARD_WIDTH + 1;
    gbCursorY = PUNKT_BOARD_HEIGHT - 12;
    gbPrintString( "hi" );
    gbCursorX = PUNKT_BOARD_WIDTH + 1;
    gbCursorY = PUNKT_BOARD_HEIGHT - 6;
    if( punktHighScore < 10 ) punktPrintChar( 48 ); // '0'
    gbPrintNumber( punktHighScore );
}

void punktDrawGutter()
{
    gbSetColor( 1 );
    gbFillRect( LCDWIDTH - PUNKT_GUTTER_WIDTH, 0, PUNKT_GUTTER_WIDTH, PUNKT_BOARD_HEIGHT );

    gbSetColor( 0 );
    punktDrawScore();
    punktDrawHighScore();
}

void punktDrawBoard()
{
    punktTargetDraw();
    punktPlayerDraw();

    int i;
    for( i = 0; i < punktEnemyCount; i++ )
      punktEnemyDraw( i );

    punktDrawGutter();
}

// -----------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------

void punktBeginTitle()
{
    punktState = PUNKT_STATE_TITLE;
}

void punktBeginMainMenu()
{
    punktReferenceTime = gbFrameCount;
    punktState = PUNKT_STATE_MAIN_MENU;
}

void punktInitializeGame()
{
    gbPickRandomSeed();
    punktEnemyCount = 0;
    punktScore = 0;
    punktPlayerReset();
    punktTargetMoveToNewLocation();
    // See this file's own header comment on normalizing away a real
    // uninitialized-member quirk here.
    punktTargetHaloTimer = 0;
    punktState = PUNKT_STATE_PLAYING;
    gbPlayOK();
}

void punktUpdateTitle()
{
    // Explicitly reset font state on entry (real titleScreen() itself
    // explicitly forces `display.fontSize = 1` every time it's entered) -
    // guards against a leftover gbFont5x7 from a previous main-menu visit.
    gbSetFont( gbFont3x5 );

    gbSetColor( 1 );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, punktLogoLargeBitmap );

    gbCursorX = 28;
    gbCursorY = 36;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      punktBeginMainMenu();
}

void punktUpdateMainMenu()
{
    if( gbPressed( BTN_A ) )
    {
        punktInitializeGame();
        return;
    }
    else if( gbPressed( BTN_B ) )
    {
        punktReferenceTime = gbFrameCount;
        punktState = PUNKT_STATE_INSTRUCTIONS;
        // Real upstream calls randomSeed(1) here so the demo always looks
        // identical - no equivalent exists in this dialect (see this
        // file's own header comment) so this demo's positions vary run to
        // run here instead.
        punktTargetMoveToNewLocation();
        punktEnemySpawn( 0, 0, 0 );
        punktEnemySpawn( 1, 0, 0 );
        punktPlayerReset();
        return;
    }

    int t = gbFrameCount - punktReferenceTime;

    gbSetColor( 1 );
    int y;
    if( t < ( 22 << 1 ) ) y = -11 + ( t >> 1 );
    else y = 11;
    gbDrawBitmap( PUNKT_TITLE_X, y, punktLogoBitmap );
    gbFillRect( 0, 0, PUNKT_GUTTER_WIDTH, PUNKT_BOARD_HEIGHT );
    gbFillRect( LCDWIDTH - PUNKT_GUTTER_WIDTH, 0, PUNKT_GUTTER_WIDTH, PUNKT_BOARD_HEIGHT );

    if( t > ( 22 << 1 ) )
    {
        gbSetFont( gbFont3x5 );
        gbCursorY = 25;
        gbCursorX = PUNKT_TITLE_X - 5;
        gbPrintString( "start" );
        gbCursorY = 33;
        gbCursorX = PUNKT_TITLE_X - 5;
        gbPrintString( "instructions" );

        if( ( t & 0x08 ) != 0 )
        {
            gbSetFont( gbFont5x7 );
            gbCursorY = 24;
            gbCursorX = PUNKT_TITLE_X - 11;
            punktPrintChar( 21 ); // '\25' - real font5x7 selector-arrow icon glyph
            gbCursorY = 32;
            gbCursorX = PUNKT_TITLE_X - 11;
            punktPrintChar( 22 ); // '\26' - real font5x7 selector-arrow icon glyph
        }
    }
}

void punktUpdatePlaying()
{
    punktPlayerUpdate();

    int i;
    for( i = 0; i < punktEnemyCount; i++ )
    {
        punktEnemyUpdate( i );
        if( gbCollideRectRect( punktPlayerX, punktPlayerY, 4, 4, punktEnemyX[ i ], punktEnemyY[ i ], PUNKT_ENEMY_SIZE, PUNKT_ENEMY_SIZE ) )
        {
            gbPlayCancel();
            punktState = PUNKT_STATE_GAME_OVER;
        }
    }

    if( gbCollideRectRect( punktPlayerX, punktPlayerY, 4, 4, punktTargetX, punktTargetY, PUNKT_TARGET_SIZE, PUNKT_TARGET_SIZE ) )
    {
        gbPlayOK();
        punktTargetMoveToNewLocation();
        while( gbCollideRectRect( punktPlayerX, punktPlayerY, 4, 4, punktTargetX, punktTargetY, PUNKT_TARGET_SIZE, PUNKT_TARGET_SIZE ) )
          punktTargetMoveToNewLocation();

        if( punktEnemyCount < PUNKT_MAX_ENEMIES )
        {
            punktEnemySpawn( punktEnemyCount, punktPlayerX, punktPlayerY );
            punktEnemyCount = punktEnemyCount + 1;
        }

        if( punktScore < 100 ) punktScore = punktScore + 1;
        if( punktScore > punktHighScore ) punktHighScore = punktScore;
    }

    punktDrawBoard();
}

void punktUpdateGameOver()
{
    punktDrawBoard();

    gbSetColor( 1 );
    if( ( gbFrameCount & 0x08 ) != 0 ) punktDrawScore();

    int boxW = ( 4 * 9 ) + 1;
    int boxX = ( PUNKT_BOARD_WIDTH - boxW ) >> 1;
    int boxY = ( PUNKT_BOARD_HEIGHT - 7 ) >> 1;
    gbFillRect( boxX, boxY, boxW, 7 );

    gbSetFont( gbFont3x5 );
    gbSetColor( 0 );
    // Preserved verbatim from real upstream - see this file's own header
    // comment on this real, minor cosmetic off-by-formula quirk (this
    // genuinely differs from boxX's own formula just above).
    gbCursorX = ( PUNKT_BOARD_WIDTH - ( 4 * 9 ) + 1 ) >> 1;
    gbCursorY = boxY + 1;
    gbPrintString( "Game Over" );

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
      punktBeginMainMenu();

    // Real EEPROM.update(1, highScore) - genuinely persisted, see this
    // file's own header comment.
    eeprom_update_byte( 1, punktHighScore );
}

void punktUpdateInstructions()
{
    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
    {
        punktReferenceTime = gbFrameCount;
        punktState = PUNKT_STATE_MAIN_MENU;
        gbPickRandomSeed();
        return;
    }

    int t = gbFrameCount - punktReferenceTime;

    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );

    if( t < 100 )
    {
        gbCursorX = 31;
        gbCursorY = 14;
        gbPrintString( "you" );
        punktPlayerDraw();
    }
    else if( t < 250 )
    {
        gbCursorX = 10;
        gbCursorY = 14;
        gbPrintString( "collect these" );
        punktTargetDraw();
    }
    else if( t < 400 )
    {
        gbCursorX = 14;
        gbCursorY = 14;
        gbPrintString( "avoid these" );
        punktEnemyUpdate( 0 );
        punktEnemyUpdate( 1 );
        punktEnemyDraw( 0 );
        punktEnemyDraw( 1 );
    }
    else
    {
        punktReferenceTime = gbFrameCount;
        punktState = PUNKT_STATE_MAIN_MENU;
        gbPickRandomSeed();
    }
}

void gamePunkt_init()
{
    gbBegin();
    // Real upstream reset() explicitly overrides the frame rate - see this
    // file's own header comment.
    gbSetFrameRate( 30 );

    punktReferenceTime = 0;
    punktScore = 0;
    punktHighScore = 0;
    punktEnemyCount = 0;

    // Real magic-byte fresh-EEPROM check (address 0 == 42) - see this
    // file's own header comment on why this differs from gameFiremen.c's
    // own 0xff convention.
    if( eeprom_read_byte( 0 ) == 42 )
    {
        punktHighScore = eeprom_read_byte( 1 );
    }
    else
    {
        eeprom_write_byte( 0, 42 );
        eeprom_update_byte( 1, 0 );
    }

    punktBeginTitle();
}

void gamePunkt_update()
{
    if( !gbUpdate() ) return;


    // Real top-of-loop() `if (gb.buttons.pressed(BTN_C)) reset();` check -
    // applies uniformly across every state, see this file's own header
    // comment.
    if( gbPressed( BTN_C ) )
    {
        punktBeginTitle();
    }
    else if( punktState == PUNKT_STATE_TITLE )
    {
        punktUpdateTitle();
    }
    else if( punktState == PUNKT_STATE_MAIN_MENU )
    {
        punktUpdateMainMenu();
    }
    else if( punktState == PUNKT_STATE_PLAYING )
    {
        punktUpdatePlaying();
    }
    else if( punktState == PUNKT_STATE_GAME_OVER )
    {
        punktUpdateGameOver();
    }
    else
    {
        punktUpdateInstructions();
    }

    gbRenderFrame();
}
