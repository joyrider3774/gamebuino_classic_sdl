#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Jezzball (RackhamLeNoir, GPLv3 - github.com/RackhamLeNoir/gamebuino-jezzball,
// real LICENSE file confirmed). Same author as this project's already-ported
// gameTaquin.c. A JezzBall/Qix-style game: a cursor walks around an
// otherwise-empty rectangular playfield full of bouncing balls; Button A
// fires a wall (a growing line, horizontal or vertical depending on Button
// B's toggle) from the cursor's position, splitting whatever board it's
// standing in into two; a wall that finishes growing without ever being hit
// by a ball permanently claims whichever of the two resulting halves ends up
// with no balls left in it (scored as claimed area), while a wall a ball
// touches mid-growth costs a life instead. Clearing 2300 (LEVELCLEAR)
// cumulative claimed-area units in a level advances to the next (one more
// ball, up to a real cap of 15); running out of lives ends the game.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamebuinoShim.h's own header comment). `random(N)` became `arand(N)`,
// `gb.pickRandomSeed()` became the documented `gbPickRandomSeed()` no-op,
// and `gb.battery.show = false;` was dropped outright - all matching this
// whole project's own established precedent (see gamePong.c's own header
// comment). Arduino's `constrain(v,lo,hi)` macro has no equivalent here (no
// ternary operator in this dialect either, so a `(a>b?a:b)`-style macro
// wouldn't have compiled) - replaced by a small local `jezzClamp()` helper
// built from the real `gbMax()`/`gbMin()` primitives, the same treatment
// gamePong.c/gameConduit.c already use for `max()`/`min()`. `numlength()`
// (upstream's own `floor(log10(number))+1`-based digit-count helper) was
// reimplemented via `itoa()` instead, matching gameTaquin.c's own
// `taqNumLength()` precedent exactly (this dialect has no readily available
// `log10()`, and itoa-based counting is simpler anyway) - the itoa version
// also naturally handles `number==0` correctly without upstream's own
// separate special case, since `score`/`level`/`percent` are all real
// non-negative counters here (upstream declares them all `unsigned int`),
// so no negative-number handling was needed either.
//
// FLATTENING FOUR REAL CLASSES (Ball/Board/Line/Cursor) INTO STRUCTS +
// INDEX-BASED SLICES: upstream's `Board` objects share ONE dynamically
// `malloc()`'d array of `Ball*` per level (`Board::ballsarray`, sized to
// that level's own ball count) - each `Board` just remembers a `_balls`
// pointer into some contiguous sub-range of that shared array plus its own
// `_nbballs` count, and `Board::split()` partitions a board's own sub-range
// in place (swapping elements) into two adjacent sub-ranges when a wall
// finishes. This dialect has no `malloc()`/`new` (confirmed: no other
// ported game in this project uses them - every dynamic-count entity table
// so far, e.g. gameCatcher.c's own `CatchSprite[CATCH_MAX_SPRITES]`, is a
// real fixed-size array) and no pointer-into-array aliasing trick to lean
// on either, so this port instead uses one fixed `JezzBall[JEZZ_MAXBALLS]`
// array (15 slots, matching upstream's own real `Ball::maxballs=15` cap)
// shared by every board, with each `JezzBoard` struct storing `ballStart`/
// `nbBalls` (an absolute index + count into that shared array) instead of a
// pointer - functionally identical to upstream's own pointer-into-shared-
// array scheme, just index-based. `jezzBoardSplit()` below reproduces
// upstream's own real in-place partition/swap loop index-for-index (see its
// own comment for the exact mapping). `JezzBoard[JEZZ_MAXBOARDS]` (15 slots
// too - upstream's own comment states directly "there cannot be more boards
// than balls", confirmed true by construction: a genuine second board only
// ever persists when a split leaves at least one ball on each side, so the
// total board count can never exceed the total ball count, itself capped at
// 15). `Line`/`Cursor` (upstream: one single global instance of each,
// singleton objects, never more than one at a time) became plain flat
// globals with no struct at all - one wall and one cursor exist at once,
// full stop.
//
// A GENUINE, UNREPRODUCIBLE UNDEFINED-BEHAVIOR BUG FOUND IN UPSTREAM'S OWN
// `Board::split()`, HANDLED BY IMPLEMENTING ITS CLEAR INTENT INSTEAD: real
// `Board::split()` is declared to return `Board*`, but NO code path in its
// real body (board.cpp) ever executes an explicit `return <value>;` - the
// only `return` at all is a bare `return;` on its very first line (itself
// only reachable if `!line.finished()`, which can never be true anyway,
// since `split()` is only ever called from loop() immediately after an
// explicit `if (line.finished())` check already passed - so this bare
// early return is dead code on top of being invalid for a non-void
// function). Every other path (the empty-new-board merge, the empty-
// original-board merge, and the genuine two-survivors case) falls off the
// end of the function entirely, an actual C++ undefined-behavior case (not
// a "real bug with defined, if surprising, behavior" like several other
// preserved-verbatim upstream quirks elsewhere in this project) - the
// returned pointer is simply whatever garbage avr-gcc happened to leave in
// its return register on real hardware, un-knowable without a real
// disassembly and not meaningfully "the same undefined behavior" on a
// different compiler/architecture even if one were done. Compounding this,
// loop()'s own caller does `Board *newboard = boards[...]->split(line); if
// (newboard) { boards[nbboards] = newboard; nbboards++; }` - genuinely
// double-booking the just-created board and incrementing `nbboards` a
// SECOND time relative to `split()`'s own already-complete internal
// bookkeeping (`nbboards++` already happens inside `split()`'s own real
// two-survivors branch) IF that indeterminate return value were ever
// observed as truthy. Since true UB has no "real" behavior this port could
// faithfully preserve byte-for-byte (unlike, say, an unsigned wraparound or
// an off-by-one, which have one single well-defined outcome on any
// compliant compiler), `jezzBoardSplit()` below instead implements
// `Board::split()`'s own CLEAR, unambiguous intent exactly once - partition
// the shared ball array, then either merge back (award area, keep one
// board) or keep both (award a smaller per-split bonus, increment the board
// count once) - and this port's own caller does NOT separately re-add a
// board or re-increment the count the way upstream's own loop() body
// would, since doing so would only reproduce a bug, not upstream's real,
// observable behavior.
//
// A GENUINE EEPROM ADDRESS TYPO, FOUND AND FIXED (not preserved):
// `get_highscore()`'s own magic-byte mismatch handler is meant to re-stamp
// EEPROM addresses 0/1/2 with `magic[0..2]` (42/12/28) via `for (j=0;j<3;
// j++) EEPROM.put(0, magic[j]);` - note the hardcoded `0` where `j` was
// clearly intended, confirmed directly against the real board.cpp/
// jezzball.ino source, not a transcription slip introduced here. This meant
// the "reset" path could NEVER actually succeed at restamping all three
// magic bytes correctly - it just overwrote address 0 three times in a row
// (ending at `magic[2]`=28) and left addresses 1/2 untouched, so the very
// next boot's own magic check mismatched AGAIN (address 0 now reads 28, not
// 42) and re-entered the same broken "reset" path forever. Real, practical
// consequence, worked through rather than just noted: on a genuinely fresh
// (or previously-corrupted) card - which, per this project's own
// eepromShim.c convention, is exactly the state any card starts in (fresh
// cells default to 255, matching real AVR EEPROM's own factory-erased
// state) - this meant Jezzball's own highscore could NEVER actually persist
// across a save/reload on real hardware either, every single boot
// re-detecting a "corrupted" stamp and resetting `highscore` to 0. Flagged
// as a genuine negative player-experience bug and fixed: `jezzGetHighscore()`
// now writes `eeprom_update_byte( j, jezzMagic[ j ] )`, restamping all three
// addresses correctly, so a saved high score now genuinely survives a
// reload.
// `EEPROM.get`/`EEPROM.put` (byte and 2-byte-`unsigned int` overloads) were
// ported to this shim's own `eeprom_read_byte()`/`eeprom_update_byte()`/
// `eeprom_read_word()`/`eeprom_write_word()` (this dialect has no classes to
// preserve the real `EEPROM` object's own dot-call syntax) - `get_
// highscore()`'s own declared-but-never-actually-returned `unsigned int`
// return value (itself ANOTHER real "falls off the end of a non-void
// function" case, this time genuinely harmless since its only real caller,
// `setup()`, calls it as a bare statement and never reads the result) was
// ported as a plain `void` function instead, matching the only way its
// result is ever actually used - not a behavior change, since nothing on
// real hardware ever reads that indeterminate value either.
//
// BLOCKING `gb.titleScreen(F(""), logo)` -> AN EXPLICIT, REUSED
// JEZZ_STATE_TITLE: upstream calls this exact same real blocking function
// (same empty name, same logo bitmap) from FOUR places - once in setup()
// (the initial boot splash) and once more from each of inputsgame()/
// inputsgameover()/inputslevelclear()'s own unconditional Button-C check
// (a genuine "return to the splash, then resume exactly where you left
// off" gesture, since none of those call sites touch any board/score/level
// state before or after the blocking call). Ported as ONE shared
// JEZZ_STATE_TITLE (matching gamePong.c's/gameTaquin.c's own "blocking
// titleScreen() -> explicit resumable state" treatment) reused for all four
// real entry points, with a `jezzFirstBoot` flag gating the ONE real
// difference between them: only the very first-ever dismissal (matching
// upstream's own setup(), which runs `gb.pickRandomSeed(); ...
// get_highscore(); preparelevel();` immediately AFTER its own title screen
// call returns) loads the highscore and prepares the first level - every
// later title visit (a mid-game Button-C pause) just resumes whatever state
// (playing / game-over / level-clear) was already active, exactly like
// upstream's own real board/score/level state surviving untouched across
// its own blocking call. Real `gb.titleScreen()`'s own internal caption/
// logo layout isn't available to read (it's a real library-internal
// function, only the logo bitmap and an empty name string were ever passed
// in) - so, matching this project's own established precedent for the same
// situation (see gameKillrace.c's own header comment), this port draws its
// own simple layout instead: the real logo bitmap (restored bit-for-bit
// below, not a placeholder) centered near the top, plus a plain "PRESS A"
// prompt underneath, dismissed by a genuine fresh `gbPressed(BTN_A)` -
// Vircon32's own menu-select button, matching every other ported title
// screen in this project.
//
// A REAL EXECUTION-ORDER NUANCE FROM THE SAME BLOCKING-CALL CONVERSION:
// upstream's own loop() normal-play branch runs `updategame(); inputsgame();
// drawgame();` in that exact order every tick - so on the exact tick Button
// C is pressed, `updategame()` (ball movement, wall growth/collision, wall-
// finish splitting) has ALREADY run for that tick before inputsgame()'s own
// blocking titleScreen() call takes over; only that one tick's own
// `drawgame()` call is what real hardware defers until AFTER the title
// screen is later dismissed (an artifact of a blocking call being
// interleavable mid-tick that a non-blocking engine has no way to
// reproduce - see gameKillrace.c's own header comment for this exact same
// situation and the same "return immediately, skip the rest of this tick's
// draw" resolution used here too). `jezzUpdatePlay()`'s own normal-play
// branch below preserves the real part of this ordering (ball movement/
// wall growth genuinely still happens on a tick where C gets pressed) while
// skipping only the now-meaningless "draw, then immediately overwrite with
// a title screen" artifact.
//
// REAL BITMAP ART RESTORED: the 64x30 title/menu logo (`jezzLogoBitmap`)
// was converted byte-for-byte from its real Arduino `B00000000`-style
// binary literals (`logo[]` in jezzball.ino) into this dialect's own
// `int[N] name = { width, height, 0x.., ... }` shape `gbDrawBitmap()`
// expects, via a small script that parsed jezzball.ino's own literal
// source text directly (not hand-transcribed) and verified the resulting
// byte count (240 = ceil(64/8) x 30) against the real declared width/
// height before trusting it - the same discipline gameFlappyBirdo.c's own
// header comment describes. No separate mask/fill layer exists for it
// upstream (a single self-contained outline bitmap, confirmed directly in
// jezzball.ino - no GRAY-body/BLACK-outline layering to restore), so its
// one `gbDrawBitmap()` call is a single direct call with no background-
// bleed risk to guard against.
//
// TWO REAL GAMEBUINO ICON GLYPHS RESTORED AS REAL ASCII, NOT SUBSTITUTED
// TEXT: upstream's own octal escapes `"\03"` (a life/heart icon, printed
// once per remaining life), `"\17"` (a decorative icon bracketing a new
// high score), and `"\25"` (the same D-pad-arrow "press to continue" icon
// gameTaquin.c's own `taqRestartText` already restores) are real Gamebuino
// custom low-ASCII glyphs (decimal 3/15/21 respectively) this shim's real
// font tables already cover in full (ASCII 0-127) - drawn directly via
// `gbDrawChar()`/`gbPrintString()` with their real decimal codes, exactly
// like real hardware, rather than substituted with plain text the way this
// project's font-table gap used to force before real fonts existed.
//
// A REAL, PRESERVED SINGLETON-OBJECT QUIRK: upstream's own `Cursor cursor;`
// is a single global C++ object, constructed exactly ONCE, ever (at
// program start, before setup() even runs) - `preparelevel()` (called at
// initial boot, and again on every level-up/restart) never touches it.
// This means the cursor's own on-screen position and current wall
// orientation genuinely persist across level transitions and full-game
// restarts on real hardware, never recentered. Ported the same way: this
// port's own `jezzCursorX`/`Y`/`Horizontal` globals are initialized exactly
// once, in `gameJezzball_init()`, and never touched again by
// `jezzPrepareLevel()`/`jezzClearLevel()` - matching real hardware exactly,
// not "fixed" into a more intuitive per-level recenter real hardware never
// actually does either.
//
// Upstream draws the cursor with `gb.display.setColor(INVERT)` (a real
// XOR-style compositing mode - the cursor line inverts whatever's already
// on screen underneath it, so it stays visible over both the black
// "claimed" background and the white "still in play" board interior),
// ported directly as `gbSetColor(GB_INVERT)`.
//
// SOUND: upstream's own Ball class calls `gb.sound.playTick()` on every
// wall bounce, ball-vs-ball bounce, and ball-vs-wall-under-construction
// collision - the only sound call anywhere in this game's own source
// (confirmed directly - no `playNote`/`playOK`/`playCancel` anywhere in
// jezzball.ino/ball.cpp/board.cpp/line.cpp/cursor.cpp) - ported as a direct,
// unmodified `gbPlayTick()` at every one of those same real call sites.

#define JEZZ_BOARDWIDTH  ( LCDWIDTH - 22 )
#define JEZZ_BOARDHEIGHT LCDHEIGHT
#define JEZZ_LEVELCLEAR  2300
#define JEZZ_MAXBALLS    15 // real upstream Ball::maxballs
#define JEZZ_MAXBOARDS   15 // "there cannot be more boards than balls" - see header comment
#define JEZZ_BALLSIZE    2  // real upstream Ball::ballsize
#define JEZZ_CURSORSIZE  3  // real upstream Cursor::cursorsize

enum JezzState
{
    JEZZ_STATE_TITLE = 0,
    JEZZ_STATE_PLAY  = 1
};

enum JezzLineState
{
    JEZZ_LINE_IDLE       = 0,
    JEZZ_LINE_EXPANDING  = 1
};

typedef struct
{
    int x;
    int y;
    int vx;
    int vy;
} JezzBall;

typedef struct
{
    int x;
    int y;
    int w;
    int h;
    int ballStart; // absolute index into jezzBalls[] where this board's own slice begins
    int nbBalls;   // count of balls in this board's own slice
} JezzBoard;

JezzBall jezzBalls[JEZZ_MAXBALLS];
int jezzTotalBalls; // real upstream Board::totalballs - balls allocated this level

JezzBoard jezzBoards[JEZZ_MAXBOARDS];
int jezzNbBoards;

int jezzLineState;
int jezzLineX, jezzLineY, jezzLineL;
bool jezzLineHorizontal;
int jezzLineBoard;

int jezzCursorX, jezzCursorY;
bool jezzCursorHorizontal;

int jezzHighscore = 0;
int jezzScore = 0;
int jezzLives = 5;
int jezzLevel = 0;
int jezzLevelScore = 0;

int jezzState;
bool jezzFirstBoot;

int jezzMagic[3] = { 42, 12, 28 };

// Real upstream logo[] (64x30, PROGMEM) - see this file's own header
// comment for how this was extracted/converted, byte-for-byte, from the
// real B-literal source.
int jezzLogoBitmap[242] =
{
    64, 30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x4F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xCF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xCE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xCE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x31, 0xCE, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF1,
    0xF1, 0xCE, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xF3,
    0xF1, 0xCE, 0x00, 0x00, 0x00, 0x00, 0x1E, 0xF6,
    0x71, 0xCE, 0x00, 0x00, 0x00, 0x00, 0x1C, 0xE6,
    0x71, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xE7,
    0x73, 0x80, 0x00, 0x00, 0x00, 0x01, 0x9F, 0xF7,
    0x50, 0x00, 0x00, 0x00, 0x00, 0x07, 0x9C, 0xF7,
    0xC0, 0x00, 0x00, 0x00, 0x03, 0x1E, 0x3C, 0xE3,
    0x80, 0x00, 0x00, 0xC0, 0x0F, 0x7E, 0x3F, 0xC0,
    0x00, 0x00, 0x03, 0xC0, 0x3E, 0x0C, 0x3F, 0x00,
    0x00, 0x00, 0x0F, 0x9C, 0xFE, 0x0F, 0xB8, 0x00,
    0x00, 0x00, 0x3F, 0x3E, 0x1C, 0x1F, 0x80, 0x00,
    0x00, 0x00, 0x7E, 0x73, 0x18, 0x3C, 0x00, 0x00,
    0x00, 0x00, 0xFE, 0x7F, 0x1F, 0x60, 0x00, 0x00,
    0x00, 0x00, 0xCE, 0xFC, 0x3E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0E, 0xF0, 0x70, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0E, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1E, 0x7C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

// No `constrain()` macro and no ternary operator in this dialect - see
// this file's own header comment.
int jezzClamp( int v, int lo, int hi )
{
    return gbMax( lo, gbMin( v, hi ) );
}

// Direct port of upstream's own numlength() via itoa() - see this file's
// own header comment (matches gameTaquin.c's own taqNumLength() exactly).
int jezzNumLength( int number )
{
    char buf[16];
    int len = 0;
    itoa( number, buf, 10 );
    while( buf[ len ] != 0 )
      len = len + 1;
    return len;
}

void jezzAwardArea( int area )
{
    jezzScore = jezzScore + area;
    jezzLevelScore = jezzLevelScore + area;
}

// -----------------------------------------------------------------------------
// Ball - direct port of ball.cpp's own Ball class
// -----------------------------------------------------------------------------

void jezzBallInit( int index, int x, int y )
{
    jezzBalls[ index ].x = x;
    jezzBalls[ index ].y = y;
    if( arand( 2 ) )
      jezzBalls[ index ].vx = 1;
    else
      jezzBalls[ index ].vx = -1;
    if( arand( 2 ) )
      jezzBalls[ index ].vy = 1;
    else
      jezzBalls[ index ].vy = -1;
}

// Direct port of upstream's own Ball::move().
void jezzBallMove( int idx, int gamex, int gamey, int gamew, int gameh )
{
    if( ( jezzBalls[ idx ].x == gamex + gamew - JEZZ_BALLSIZE && jezzBalls[ idx ].vx > 0 ) ||
        ( jezzBalls[ idx ].x == gamex && jezzBalls[ idx ].vx < 0 ) )
    {
        jezzBalls[ idx ].vx = -jezzBalls[ idx ].vx;
        gbPlayTick();
    }
    else
      jezzBalls[ idx ].x = jezzBalls[ idx ].x + jezzBalls[ idx ].vx;

    if( ( jezzBalls[ idx ].y == gamey + gameh - JEZZ_BALLSIZE && jezzBalls[ idx ].vy > 0 ) ||
        ( jezzBalls[ idx ].y == gamey && jezzBalls[ idx ].vy < 0 ) )
    {
        jezzBalls[ idx ].vy = -jezzBalls[ idx ].vy;
        gbPlayTick();
    }
    else
      jezzBalls[ idx ].y = jezzBalls[ idx ].y + jezzBalls[ idx ].vy;
}

// Direct port of upstream's own Ball::collide(Ball&).
void jezzBallCollideBall( int a, int b )
{
    if( !gbCollideRectRect( jezzBalls[ a ].x, jezzBalls[ a ].y, JEZZ_BALLSIZE, JEZZ_BALLSIZE,
                             jezzBalls[ b ].x, jezzBalls[ b ].y, JEZZ_BALLSIZE, JEZZ_BALLSIZE ) )
      return;

    // ball on the right
    if( jezzBalls[ a ].vx > 0 && jezzBalls[ b ].x == jezzBalls[ a ].x + JEZZ_BALLSIZE - 1 )
    {
        jezzBalls[ a ].vx = -1;
        jezzBalls[ b ].vx = 1;
    }
    // ball on the left
    else if( jezzBalls[ a ].vx < 0 && jezzBalls[ a ].x == jezzBalls[ b ].x + JEZZ_BALLSIZE - 1 )
    {
        jezzBalls[ a ].vx = 1;
        jezzBalls[ b ].vx = -1;
    }

    // ball on the top
    if( jezzBalls[ a ].vy > 0 && jezzBalls[ b ].y == jezzBalls[ a ].y + JEZZ_BALLSIZE - 1 )
    {
        jezzBalls[ a ].vy = -1;
        jezzBalls[ b ].vy = 1;
    }
    // ball on the bottom
    else if( jezzBalls[ a ].vy < 0 && jezzBalls[ a ].y == jezzBalls[ b ].y + JEZZ_BALLSIZE - 1 )
    {
        jezzBalls[ a ].vy = 1;
        jezzBalls[ b ].vy = -1;
    }

    gbPlayTick();
}

// -----------------------------------------------------------------------------
// Line - direct port of line.cpp's own Line class
// -----------------------------------------------------------------------------

void jezzLineStart( int x, int y, bool horizontal, int board )
{
    jezzLineX = x;
    jezzLineY = y;
    jezzLineHorizontal = horizontal;
    jezzLineL = 0;
    jezzLineBoard = board;
    jezzLineState = JEZZ_LINE_EXPANDING;
}

// Direct port of upstream's own Line::collision(Ball&).
bool jezzLineCollision( int ballIdx )
{
    int minV, maxV, pos, length;

    if( jezzLineHorizontal )
    {
        minV = jezzBoards[ jezzLineBoard ].x;
        maxV = jezzBoards[ jezzLineBoard ].x + jezzBoards[ jezzLineBoard ].w - 1;
        pos = jezzClamp( jezzLineX - jezzLineL, minV, maxV );
        length = jezzClamp( 2 * jezzLineL + 1, 0, maxV - minV );
        return gbCollideRectRect( pos, jezzLineY, length, 1,
                                   jezzBalls[ ballIdx ].x, jezzBalls[ ballIdx ].y, JEZZ_BALLSIZE, JEZZ_BALLSIZE );
    }

    minV = jezzBoards[ jezzLineBoard ].y;
    maxV = jezzBoards[ jezzLineBoard ].y + jezzBoards[ jezzLineBoard ].h - 1;
    pos = jezzClamp( jezzLineY - jezzLineL, minV, maxV );
    length = jezzClamp( 2 * jezzLineL + 1, 0, maxV - minV );
    return gbCollideRectRect( jezzLineX, pos, 1, length,
                               jezzBalls[ ballIdx ].x, jezzBalls[ ballIdx ].y, JEZZ_BALLSIZE, JEZZ_BALLSIZE );
}

// Direct port of upstream's own Ball::collide(Line&).
bool jezzBallCollideLine( int ballIdx )
{
    if( !jezzLineCollision( ballIdx ) )
      return false;

    if( jezzLineHorizontal )
      jezzBalls[ ballIdx ].vy = -jezzBalls[ ballIdx ].vy;
    else
      jezzBalls[ ballIdx ].vx = -jezzBalls[ ballIdx ].vx;

    gbPlayTick();
    return true;
}

// Direct port of upstream's own Line::finished().
bool jezzLineFinished()
{
    int bx = jezzBoards[ jezzLineBoard ].x;
    int by = jezzBoards[ jezzLineBoard ].y;
    int bw = jezzBoards[ jezzLineBoard ].w;
    int bh = jezzBoards[ jezzLineBoard ].h;

    if( jezzLineHorizontal )
      return ( jezzLineX - jezzLineL <= bx ) && ( jezzLineX + jezzLineL >= bx + bw );

    return ( jezzLineY - jezzLineL <= by ) && ( jezzLineY + jezzLineL >= by + bh );
}

// Direct port of upstream's own Line::draw().
void jezzLineDraw()
{
    int minV, maxV, pos, length;

    if( jezzLineState == JEZZ_LINE_IDLE )
      return;

    if( jezzLineHorizontal )
    {
        minV = jezzBoards[ jezzLineBoard ].x;
        maxV = jezzBoards[ jezzLineBoard ].x + jezzBoards[ jezzLineBoard ].w - 1;
        pos = jezzClamp( jezzLineX - jezzLineL, minV, maxV );
        length = jezzClamp( 2 * jezzLineL + 1, 0, maxV - minV );
        gbDrawFastHLine( pos, jezzLineY, length );
    }
    else
    {
        minV = jezzBoards[ jezzLineBoard ].y;
        maxV = jezzBoards[ jezzLineBoard ].y + jezzBoards[ jezzLineBoard ].h - 1;
        pos = jezzClamp( jezzLineY - jezzLineL, minV, maxV );
        length = jezzClamp( 2 * jezzLineL + 1, 0, maxV - minV );
        gbDrawFastVLine( jezzLineX, pos, length );
    }
}

// -----------------------------------------------------------------------------
// Cursor - direct port of cursor.cpp's own Cursor class
// -----------------------------------------------------------------------------

void jezzCursorUp()
{
    jezzCursorY = jezzClamp( jezzCursorY - 1, 0, JEZZ_BOARDHEIGHT - 1 );
}

void jezzCursorDown()
{
    jezzCursorY = jezzClamp( jezzCursorY + 1, 0, JEZZ_BOARDHEIGHT - 1 );
}

void jezzCursorLeft()
{
    jezzCursorX = jezzClamp( jezzCursorX - 1, 0, JEZZ_BOARDWIDTH - 1 );
}

void jezzCursorRight()
{
    jezzCursorX = jezzClamp( jezzCursorX + 1, 0, JEZZ_BOARDWIDTH - 1 );
}

void jezzCursorRotate()
{
    jezzCursorHorizontal = !jezzCursorHorizontal;
}

void jezzCursorDraw()
{
    if( jezzCursorHorizontal )
      gbDrawFastHLine( jezzCursorX - ( JEZZ_CURSORSIZE / 2 ), jezzCursorY, JEZZ_CURSORSIZE );
    else
      gbDrawFastVLine( jezzCursorX, jezzCursorY - ( JEZZ_CURSORSIZE / 2 ), JEZZ_CURSORSIZE );
}

// -----------------------------------------------------------------------------
// Board - direct port of board.cpp's own Board class (see this file's own
// header comment for the shared-ball-array indexing scheme)
// -----------------------------------------------------------------------------

// Direct port of upstream's own Board::initBalls().
void jezzInitBalls( int boardIndex, int nb )
{
    int i;
    jezzTotalBalls = nb;
    jezzBoards[ boardIndex ].ballStart = 0;
    jezzBoards[ boardIndex ].nbBalls = nb;

    for( i = 0; i < nb; i = i + 1 )
      jezzBallInit( i, arand( jezzBoards[ boardIndex ].w - JEZZ_BALLSIZE ), arand( jezzBoards[ boardIndex ].h - JEZZ_BALLSIZE ) );
}

// Direct port of upstream's own Board::moveBalls().
void jezzMoveBalls( int bi )
{
    int i, j;
    int start = jezzBoards[ bi ].ballStart;
    int nb = jezzBoards[ bi ].nbBalls;

    for( i = 0; i < nb; i = i + 1 )
    {
        jezzBallMove( start + i, jezzBoards[ bi ].x, jezzBoards[ bi ].y, jezzBoards[ bi ].w, jezzBoards[ bi ].h );

        for( j = i + 1; j < nb; j = j + 1 )
          jezzBallCollideBall( start + i, start + j );
    }
}

// Direct port of upstream's own Board::split() - see this file's own header
// comment for the real UB bug found in upstream's own version and why this
// implements its clear intent exactly once instead.
void jezzBoardSplit( int bi )
{
    int tempnbballs = jezzBoards[ bi ].nbBalls;
    int oldBallStart = jezzBoards[ bi ].ballStart;
    int newIndex = jezzNbBoards; // tentative new-board slot, only "kept" if both sides survive
    int candidateStart = oldBallStart + tempnbballs; // mirrors upstream's own `_balls + _nbballs`
    int stayCount = 0;
    int newCount = 0;
    int i, j, tmpX, tmpY, tmpVX, tmpVY;

    if( jezzLineHorizontal )
    {
        jezzBoards[ newIndex ].x = jezzBoards[ bi ].x;
        jezzBoards[ newIndex ].y = jezzLineY + 1;
        jezzBoards[ newIndex ].w = jezzBoards[ bi ].w;
        jezzBoards[ newIndex ].h = jezzBoards[ bi ].h + jezzBoards[ bi ].y - jezzLineY - 1;
        jezzBoards[ bi ].h = jezzLineY - jezzBoards[ bi ].y;
    }
    else
    {
        jezzBoards[ newIndex ].x = jezzLineX + 1;
        jezzBoards[ newIndex ].y = jezzBoards[ bi ].y;
        jezzBoards[ newIndex ].w = jezzBoards[ bi ].w + jezzBoards[ bi ].x - jezzLineX - 1;
        jezzBoards[ newIndex ].h = jezzBoards[ bi ].h;
        jezzBoards[ bi ].w = jezzLineX - jezzBoards[ bi ].x;
    }

    // Real upstream partition loop: classify each ball in bi's own original
    // [oldBallStart, oldBallStart+tempnbballs) slice as staying in bi's own
    // (now-shrunk) rectangle or moving to the new board, swapping in place
    // to keep "stays" at the front and "moves" at the back of the same
    // shared range - see this file's own header comment for the full index
    // mapping against upstream's own pointer arithmetic.
    while( stayCount + newCount < tempnbballs )
    {
        i = oldBallStart + stayCount;

        if( gbCollideRectRect( jezzBoards[ bi ].x, jezzBoards[ bi ].y, jezzBoards[ bi ].w, jezzBoards[ bi ].h,
                                jezzBalls[ i ].x, jezzBalls[ i ].y, JEZZ_BALLSIZE, JEZZ_BALLSIZE ) )
        {
            stayCount = stayCount + 1;
        }
        else
        {
            j = oldBallStart + tempnbballs - newCount - 1;

            tmpX = jezzBalls[ i ].x;   tmpY = jezzBalls[ i ].y;
            tmpVX = jezzBalls[ i ].vx; tmpVY = jezzBalls[ i ].vy;
            jezzBalls[ i ].x = jezzBalls[ j ].x;   jezzBalls[ i ].y = jezzBalls[ j ].y;
            jezzBalls[ i ].vx = jezzBalls[ j ].vx; jezzBalls[ i ].vy = jezzBalls[ j ].vy;
            jezzBalls[ j ].x = tmpX;   jezzBalls[ j ].y = tmpY;
            jezzBalls[ j ].vx = tmpVX; jezzBalls[ j ].vy = tmpVY;

            candidateStart = candidateStart - 1;
            newCount = newCount + 1;
        }
    }

    jezzBoards[ bi ].nbBalls = stayCount;
    jezzBoards[ newIndex ].nbBalls = newCount;
    jezzBoards[ newIndex ].ballStart = candidateStart;

    // Check for empty boards - direct port of upstream's own real
    // three-way merge logic and its own real per-branch area-award
    // formulas (see this file's own header comment for why this replaces
    // upstream's own UB-tainted bookkeeping with its clear intent).
    if( newCount == 0 )
    {
        if( jezzLineHorizontal )
          jezzAwardArea( jezzBoards[ newIndex ].w * ( jezzBoards[ newIndex ].h + 1 ) );
        else
          jezzAwardArea( ( jezzBoards[ newIndex ].w + 1 ) * jezzBoards[ newIndex ].h );
        // bi itself already holds its own (unchanged) tempnbballs balls - nothing else to do
    }
    else if( stayCount == 0 )
    {
        if( jezzLineHorizontal )
          jezzAwardArea( jezzBoards[ bi ].w * ( jezzBoards[ bi ].h + 1 ) );
        else
          jezzAwardArea( ( jezzBoards[ bi ].w + 1 ) * jezzBoards[ bi ].h );

        jezzBoards[ bi ].x = jezzBoards[ newIndex ].x;
        jezzBoards[ bi ].y = jezzBoards[ newIndex ].y;
        jezzBoards[ bi ].w = jezzBoards[ newIndex ].w;
        jezzBoards[ bi ].h = jezzBoards[ newIndex ].h;
        jezzBoards[ bi ].ballStart = jezzBoards[ newIndex ].ballStart;
        jezzBoards[ bi ].nbBalls = jezzBoards[ newIndex ].nbBalls;
    }
    else
    {
        if( jezzLineHorizontal )
          jezzAwardArea( jezzBoards[ bi ].w );
        else
          jezzAwardArea( jezzBoards[ bi ].h );

        jezzNbBoards = jezzNbBoards + 1;
    }
}

void jezzBoardDraw( int bi )
{
    gbFillRect( jezzBoards[ bi ].x, jezzBoards[ bi ].y, jezzBoards[ bi ].w, jezzBoards[ bi ].h );
}

void jezzBoardDrawBalls( int bi )
{
    int i;
    for( i = 0; i < jezzBoards[ bi ].nbBalls; i = i + 1 )
      gbFillRect( jezzBalls[ jezzBoards[ bi ].ballStart + i ].x, jezzBalls[ jezzBoards[ bi ].ballStart + i ].y, JEZZ_BALLSIZE, JEZZ_BALLSIZE );
}

// -----------------------------------------------------------------------------
// Level management - direct port of upstream's own preparelevel()/clearlevel()
// -----------------------------------------------------------------------------

void jezzClearLevel()
{
    jezzNbBoards = 0;
    jezzTotalBalls = 0;
    jezzLineState = JEZZ_LINE_IDLE;
}

void jezzPrepareLevel()
{
    int numballs;

    jezzLevel = jezzLevel + 1;
    jezzLevelScore = 0;

    numballs = jezzClamp( jezzLevel, 1, JEZZ_MAXBALLS );

    jezzNbBoards = 1;
    jezzBoards[ 0 ].x = 0;
    jezzBoards[ 0 ].y = 0;
    jezzBoards[ 0 ].w = JEZZ_BOARDWIDTH;
    jezzBoards[ 0 ].h = JEZZ_BOARDHEIGHT;
    jezzInitBalls( 0, numballs );
    jezzLineState = JEZZ_LINE_IDLE;
}

// -----------------------------------------------------------------------------
// EEPROM highscore - see this file's own header comment about the real,
// verbatim-preserved address typo in upstream's own reset path.
// -----------------------------------------------------------------------------

void jezzGetHighscore()
{
    int i, j, temp;

    for( i = 0; i < 3; i = i + 1 )
    {
        temp = eeprom_read_byte( i );
        if( jezzMagic[ i ] != temp )
        {
            // Fixed here, not preserved - see this file's own header
            // comment for the real upstream bug this replaces (a
            // hardcoded 0 instead of `j`, which meant the "reset" path
            // could never actually re-stamp all three magic bytes, so the
            // high score could never persist across a save/reload).
            for( j = 0; j < 3; j = j + 1 )
              eeprom_update_byte( j, jezzMagic[ j ] );
            jezzHighscore = 0;
            return;
        }
    }

    jezzHighscore = eeprom_read_word( 3 );
}

void jezzSetHighscore()
{
    eeprom_write_word( 3, jezzHighscore );
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void jezzBeginTitle()
{
    jezzState = JEZZ_STATE_TITLE;
}

void jezzBeginPlay()
{
    jezzState = JEZZ_STATE_PLAY;
}

// -----------------------------------------------------------------------------
// Drawing - direct port of upstream's own drawgame()/drawgameover()/drawlevelclear()
// -----------------------------------------------------------------------------

// Direct port of upstream's own drawgame(). Text color is never explicitly
// (re)set to BLACK by upstream either - the last real setColor() call left
// standing from a prior frame's own drawing already left it there (this
// shim's own gbColor is a persistent global exactly like real hardware's
// own Display::color member, only ever touched by an explicit setColor()
// call - see gamebuinoShim.c's own gbBegin(), which sets it to BLACK=1
// exactly once at startup and never again on its own).
void jezzDrawGame()
{
    int i, percent;

    gbFillScreen( 1 ); // BLACK - unclaimed screen background

    // sidebar
    gbSetColor( 0 ); // WHITE
    gbFillRect( LCDWIDTH - 21, 0, 21, LCDHEIGHT );
    gbSetColor( 1 ); // BLACK
    gbSetFont( gbFont3x5 );

    gbCursorX = LCDWIDTH - 20;
    gbCursorY = 1;
    gbPrintString( "Level" );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbCursorX = LCDWIDTH - 4 * jezzNumLength( jezzLevel );
    gbPrintNumber( jezzLevel );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbCursorX = LCDWIDTH - 20;
    gbPrintString( "Score" );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbCursorX = LCDWIDTH - 4 * jezzNumLength( jezzScore );
    gbPrintNumber( jezzScore );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    // lives, one real heart-icon glyph (ASCII 3) per remaining life
    gbCursorX = LCDWIDTH - 20;
    for( i = 0; i < jezzLives; i = i + 1 )
    {
        gbDrawChar( 3, gbCursorX, gbCursorY );
        gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    }

    percent = jezzLevelScore * 100 / ( JEZZ_BOARDWIDTH * JEZZ_BOARDHEIGHT );
    gbCursorX = LCDWIDTH - 4 * ( jezzNumLength( percent ) + 1 );
    gbCursorY = 31;
    gbPrintNumber( percent );
    gbPrintString( "%" );

    // boards
    gbSetColor( 0 ); // WHITE
    for( i = 0; i < jezzNbBoards; i = i + 1 )
      jezzBoardDraw( i );

    // line
    gbSetColor( 1 ); // BLACK
    jezzLineDraw();

    // cursor - real upstream setColor(INVERT), restored for real:
    // GB_INVERT was added to gamebuinoShim.h/.c specifically because this
    // file's own first draft had to substitute BLACK here (see this file's
    // own header comment for the full story) - the cursor now genuinely
    // toggles whatever's already on screen, staying visible over both
    // claimed (black) and unclaimed (white) territory exactly like real
    // hardware.
    gbSetColor( GB_INVERT );
    jezzCursorDraw();

    // balls
    gbSetColor( 1 ); // BLACK
    for( i = 0; i < jezzNbBoards; i = i + 1 )
      jezzBoardDrawBalls( i );
}

// Direct port of upstream's own drawgameover().
void jezzDrawGameOver()
{
    gbSetFont( gbFont5x7 );
    gbCursorX = ( LCDWIDTH - 53 ) / 2;
    gbCursorY = 10;
    gbPrintString( "Game Over" );

    gbSetFont( gbFont3x5 );
    if( jezzScore > jezzHighscore )
    {
        gbCursorX = ( LCDWIDTH - 59 ) / 2;
        gbCursorY = 30;
        gbPrintString( "High score" );
        gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

        gbCursorX = ( LCDWIDTH - ( 6 * ( jezzNumLength( jezzScore ) + 2 ) - 1 ) ) / 2;
        gbDrawChar( 15, gbCursorX, gbCursorY ); // real "\17" icon
        gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
        gbPrintNumber( jezzScore );
        gbDrawChar( 15, gbCursorX, gbCursorY ); // real "\17" icon
    }
    else
    {
        gbCursorX = ( LCDWIDTH - 29 ) / 2;
        gbCursorY = 30;
        gbPrintString( "Score" );
        gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

        gbCursorX = ( LCDWIDTH - ( 6 * jezzNumLength( jezzScore ) - 1 ) ) / 2;
        gbPrintNumber( jezzScore );
    }
}

// Direct port of upstream's own drawlevelclear().
void jezzDrawLevelClear()
{
    gbSetFont( gbFont5x7 );
    gbCursorX = ( LCDWIDTH - 29 ) / 2;
    gbCursorY = 5;
    gbPrintString( "Level" );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbCursorX = ( LCDWIDTH - ( 6 * jezzNumLength( jezzLevel ) - 1 ) ) / 2;
    gbPrintNumber( jezzLevel );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;

    gbCursorX = ( LCDWIDTH - 41 ) / 2;
    gbPrintString( "cleared" );

    gbSetFont( gbFont3x5 );
    gbCursorX = ( LCDWIDTH - 41 ) / 2;
    gbCursorY = 35;
    gbPrintString( "Press " );
    gbDrawChar( 21, gbCursorX, gbCursorY ); // real "\25" D-pad-arrow icon
}

// -----------------------------------------------------------------------------
// Input / update - direct port of upstream's own inputsgame()/
// inputsgameover()/inputslevelclear()/updategame()/loop()
// -----------------------------------------------------------------------------

// Direct port of upstream's own updategame().
void jezzUpdateGame()
{
    int i, nb, start;

    for( i = 0; i < jezzNbBoards; i = i + 1 )
      jezzMoveBalls( i );

    if( jezzLineState == JEZZ_LINE_EXPANDING )
    {
        jezzLineL = jezzLineL + 1; // line.grow()

        // check collision with a ball
        nb = jezzBoards[ jezzLineBoard ].nbBalls;
        start = jezzBoards[ jezzLineBoard ].ballStart;
        for( i = 0; i < nb; i = i + 1 )
        {
            if( jezzBallCollideLine( start + i ) )
            {
                jezzLives = jezzLives - 1;
                jezzLineState = JEZZ_LINE_IDLE;
                return;
            }
        }

        if( jezzLineFinished() )
        {
            jezzBoardSplit( jezzLineBoard );
            jezzLineState = JEZZ_LINE_IDLE;
        }
    }
}

// Direct port of upstream's own inputsgame(). Returns true the instant
// Button C is pressed (matching upstream's own real gb.titleScreen() call
// site there) so the caller can skip this tick's own drawgame() call - see
// this file's own header comment on the real execution-order nuance this
// preserves/can't preserve.
bool jezzInputsGame()
{
    int i;

    if( gbPressed( BTN_A ) && jezzLineState == JEZZ_LINE_IDLE )
    {
        for( i = 0; i < jezzNbBoards; i = i + 1 )
        {
            if( gbCollidePointRect( jezzCursorX, jezzCursorY, jezzBoards[ i ].x, jezzBoards[ i ].y, jezzBoards[ i ].w, jezzBoards[ i ].h ) )
            {
                jezzLineStart( jezzCursorX, jezzCursorY, jezzCursorHorizontal, i );
                break;
            }
        }
    }

    if( gbPressed( BTN_B ) )
      jezzCursorRotate();

    if( gbPressed( BTN_C ) )
    {
        jezzBeginTitle();
        return true;
    }

    if( gbRepeat( BTN_UP, 1 ) )
      jezzCursorUp();
    else if( gbRepeat( BTN_DOWN, 1 ) )
      jezzCursorDown();
    if( gbRepeat( BTN_LEFT, 1 ) )
      jezzCursorLeft();
    else if( gbRepeat( BTN_RIGHT, 1 ) )
      jezzCursorRight();

    return false;
}

// Direct port of upstream's own inputsgameover().
void jezzInputsGameOver()
{
    if( gbPressed( BTN_A ) )
    {
        if( jezzScore > jezzHighscore )
        {
            jezzHighscore = jezzScore;
            jezzSetHighscore();
        }
        jezzScore = 0;
        jezzLevel = 0;
        jezzLives = 5;
        jezzClearLevel();
        jezzPrepareLevel();
    }

    if( gbPressed( BTN_C ) )
      jezzBeginTitle();
}

// Direct port of upstream's own inputslevelclear().
void jezzInputsLevelClear()
{
    if( gbPressed( BTN_A ) )
    {
        jezzClearLevel();
        jezzPrepareLevel();
    }

    if( gbPressed( BTN_C ) )
      jezzBeginTitle();
}

void jezzUpdateTitle()
{
    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );

    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, jezzLogoBitmap );

    gbCursorX = ( LCDWIDTH - 7 * gbFontWidth ) / 2;
    gbCursorY = 38;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        if( jezzFirstBoot )
        {
            jezzFirstBoot = false;
            jezzGetHighscore();
            jezzPrepareLevel();
        }
        jezzBeginPlay();
    }
}

// Direct port of upstream's own loop() body's normal-play dispatch (the
// `if (gb.update())` part itself lives in gameJezzball_update() below,
// matching every other game's own split here).
void jezzUpdatePlay()
{
    if( jezzLives <= 0 )
    {
        jezzDrawGameOver();
        jezzInputsGameOver();
    }
    else if( jezzLevelScore >= JEZZ_LEVELCLEAR )
    {
        jezzDrawLevelClear();
        jezzInputsLevelClear();
    }
    else
    {
        jezzUpdateGame();
        if( jezzInputsGame() ) return; // Button C pressed - skip this tick's own draw, see header comment
        jezzDrawGame();
    }
}

void gameJezzball_init()
{
    gbBegin();
    gbPickRandomSeed();

    jezzHighscore = 0;
    jezzScore = 0;
    jezzLives = 5;
    jezzLevel = 0;
    jezzLevelScore = 0;
    jezzNbBoards = 0;
    jezzTotalBalls = 0;
    jezzLineState = JEZZ_LINE_IDLE;
    jezzFirstBoot = true;

    // real upstream Cursor() constructor - only ever run once, see this
    // file's own header comment
    jezzCursorX = JEZZ_BOARDWIDTH / 2 - JEZZ_CURSORSIZE + 1;
    jezzCursorY = JEZZ_BOARDHEIGHT / 2;
    jezzCursorHorizontal = true;

    jezzBeginTitle();
}

void gameJezzball_update()
{
    if( !gbUpdate() ) return;

    if( jezzState == JEZZ_STATE_TITLE ) jezzUpdateTitle();
    else jezzUpdatePlay();

    gbRenderFrame();
}
