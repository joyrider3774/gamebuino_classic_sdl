#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Xonix (Tnxec2 / Nikolas Maletschkin, license: None specified - confirmed
// directly against the real repo, github.com/Tnxec2/xonix-gamebuino: there
// is no LICENSE/COPYING file anywhere in it and neither `XONIX.ino` nor
// `PrintUtils.ino` carries a license header of its own; the author name
// comes from the repo's own real commit authorship). A real Xonix clone for
// Gamebuino Classic - steer a cursor out of the already-claimed border into
// the open field, drawing a trail behind it, and get back to claimed
// territory to enclose (and claim) everything the trail cut off. Claim 75%
// of the board to advance a level; touching your own trail, or letting a
// roaming enemy touch either you or your trail, costs one of three lives.
// Each level adds one more enemy (and refunds one life, up to 3); clearing
// the level that already has all 10 enemies wins the game outright.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every global got an `xnx`-prefixed
// name, since this cartridge has no linker and every ported game shares one
// flat global namespace. Upstream's own `coords` struct (a plain x/y int
// pair, used for the player, the player's velocity, and both enemy arrays)
// is flattened into separate scalar/array globals - the same "flatten a
// small aggregate into plain named globals/parallel arrays" treatment used
// throughout this project (see gamePunkt.c/gameSnake5110.c) - which also
// sidesteps upstream's own `enemys[i] = {-1, -1}` brace-initializer
// assignments, a form this dialect has no equivalent for. Upstream's own
// `B00000000`-style Arduino binary bitmap literals (the 64x30 title logo,
// the 5x4 heart, the 2x2 trail dot) were converted to hex byte-for-byte by
// script, no bit reshuffling needed - this shim's own gbDrawBitmap() reads
// the exact same row-major/MSB-first layout real Display::drawBitmap() does.
// `random(a, b)` became `a + arand(b - a)` and `random(n)` became `arand(n)`
// (this project's own established ranged-random rewrite). The two `? :`
// ternaries upstream uses (the enemy's random initial velocity signs, and
// the player cell's own draw color) became explicit if/else - this dialect
// has no ternary operator at all.
//
// `PrintUtils.ino` IS this author's own reusable helper file, shared
// verbatim with his other Gamebuino titles - confirmed by direct comparison
// against `snake-gamebuino-classic`, already shipped in this cartridge as
// `gameSnakeClassic.c`, whose own header comment describes the identical
// `popup()`/`updatePopup()`/`printCentered()` trio. Xonix uses exactly one
// piece of it for real: the `printCentered(const __FlashStringHelper*)`
// overload, ported here as `xnxPrintCentered()` (with a local
// `xnxStrLen()` standing in for `strlen_PF()`, which only exists on AVR to
// reach flash). The rest is dead code and is dropped: the `char*`
// `printCentered()` overload is never called at all, and while
// `updatePopup()` IS called once per tick, `popup()` never is anywhere in
// the game, so `popupTimeLeft` is permanently 0 and the popup box can never
// appear on real hardware either. (This shim does have a real `gbPopup()`
// primitive, but wiring it in here would invent a UI element the real game
// never shows.)
//
// TIMING: upstream throttles player/enemy movement on raw Arduino
// `millis()` (`currentTime - prevTime >= delayTime`, delayTime 200ms) on top
// of `gb.update()`'s own frame throttle. Vircon32 has no wall-clock
// primitive, so `xnxMillis()` derives a millisecond clock from
// `gbFrameCount * 50` (the real 20fps default tick period - see
// gamebuinoShim.h's own gbBegin() comment), matching gameCruiser.c's own
// already-established `cruiMillis()` precedent. The comparison itself is
// left in upstream's exact original shape rather than rewritten into a tick
// count, so the real 200ms threshold stays visible at the call site: with
// the real 20fps default upstream itself never overrides (it never calls
// `gb.setFrameRate()`), this works out to one move every 4 ticks, exactly
// like real hardware. `xnxPrevTime` is deliberately NOT reset by
// `xnxInitGame()` - upstream's own `prevTime` is a plain global its
// `initGame()` never touches either.
//
// BLOCKING TITLE SCREEN -> EXPLICIT STATE: upstream's own real
// `gb.titleScreen(F("Xonix by TnxEc2"), logo)` (shown once from `setup()`,
// and re-entered whenever Button C is pressed mid-game) is a blocking call,
// converted here into an explicit XNX_STATE_TITLE state, matching the
// treatment used throughout this project (see gamePong.c/gameLightsOutAD.c).
// The real 64x30 logo bitmap is drawn via gbDrawBitmap(), centered
// horizontally (real hardware's own fixed x=0 placement only makes sense
// alongside its own boot-logo furniture, which this shim has no equivalent
// for), with upstream's own real title string and a "PRESS A" prompt under
// it. Upstream continues straight into the same tick's drawing after
// `titleScreen()` returns (its `initGame()` call happens inline, then
// execution falls through to `drawField()`), and both directions of the
// state switch here preserve that "no dropped frame" shape exactly: the A
// press that leaves the title screen draws the fresh board in the very same
// tick, and the C press that returns to it draws the title screen in the
// very same tick.
//
// No EEPROM/highscore exists upstream at all (confirmed by grep across both
// real source files - there is no score concept in this game beyond the
// live enemy count and the claimed-area percentage, neither of which is
// persisted), so none was invented here, per this project's own "don't
// invent a highscore concept real upstream never had" precedent. Upstream
// never calls `setFont()` either, so this port inherits this shim's own real
// `gbFont3x5` default exactly like upstream inherits real hardware's -
// which matters, since both `drawScore()`'s own score position and the
// status banner's own height are computed from the live `fontHeight`.
// `Serial.begin(9600)` (never written to afterwards) and
// `gb.battery.show = false` (a real-hardware-only cosmetic indicator) are
// both dropped outright, matching every other port's own treatment.
//
// REAL UPSTREAM QUIRKS PRESERVED DELIBERATELY (none of these is a porting
// mistake - each is exactly how the real game behaves on real hardware):
//
// - **The bucket-fill's own off-by-one neighbour guards.** `claimBoard()`'s
//   flood fill tests `row > 1`/`col > 1` before looking at the cell above/
//   left, where `row > 0`/`col > 0` is what the symmetric `row <
//   BOARDHEIGHT - 1`/`col < BOARDWIDTH - 1` guards on the other two sides
//   imply. Harmless in practice, and provably so rather than just
//   apparently: row 0 and column 0 are set CELL_CLAIMED by `clearBoard()`
//   and nothing ever unsets them, so the skipped reads could never have
//   found the CELL_EMPTY the test is looking for anyway. Reproduced
//   verbatim rather than "corrected".
//
// - **The duplicated claim condition in `move()`.** The identical
//   `oldPlayerStat != CELL_CLAIMED && board[player] == CELL_CLAIMED` test is
//   written out twice in a row, once to call `claimBoard()` and once to zero
//   the player's velocity. `claimBoard()` never changes the player's own
//   cell away from CELL_CLAIMED, so the second test always agrees with the
//   first - kept as two separate statements exactly as upstream wrote them.
//
// - **`oldPlayerStat` is sampled before the trail is marked.** `move()`
//   reads the player's current cell into `oldPlayerStat` first, and only
//   then overwrites a CELL_EMPTY cell with CELL_LINE - so a player standing
//   on open field records EMPTY, not LINE. This is load-bearing (it is what
//   makes "was outside, now inside" detect a completed loop), not an
//   ordering accident, and is preserved exactly.
//
// - **Enemies can slip diagonally into claimed territory.** An enemy bounces
//   by testing only its two orthogonal neighbours, then moves on both axes
//   at once - so when both orthogonal cells are open but the diagonal one is
//   claimed, it steps right onto the claimed cell. It bounces straight back
//   out the following tick (from inside solid claimed ground both tests fire
//   and both velocities flip), so this shows up as an enemy briefly clipping
//   the corner of a claimed region. Left exactly as upstream has it.
//
// NO OUT-OF-BOUNDS RISK FROM THE ENEMY BOUNCE READS, checked rather than
// assumed: `board[enemys[i].x + enemys_velocity[i].x][enemys[i].y]` looks
// like a classic unguarded neighbour read (this platform would hand back an
// arbitrary adjacent global rather than real AVR's harmless flash garbage,
// and `claimBoard()`'s own `board[enemy] = CELL_EMPTY` is a genuine WRITE),
// but the board's outermost ring is CELL_CLAIMED from `clearBoard()` onward
// and is never unclaimed by anything, which pins every enemy inside
// [1, BOARDWIDTH-2] x [1, BOARDHEIGHT-2] permanently: reaching column 0
// would require passing the bounce test against column 0 itself, which is
// always claimed, and the diagonal-entry case above lands on a cell whose
// own orthogonal test already proved it is not on the ring. Enemies also
// start well inside (`random(10, BOARDWIDTH - 10)`) and never take a zero
// velocity. So every index stays in range by construction and no defensive
// clamp was added - the neighbour reads are ported literally.

// Upstream's own real constants. BOARDWIDTH/BOARDHEIGHT/LEVELCLEARRATE are
// reproduced with upstream's own exact parenthesization (see this project's
// own real Skibuino macro bug for why that is checked rather than tidied):
// 84 - 7 leaves a 7px-wide status strip on the right, /2 gives a 38x24 cell
// board, and 38 * 24 / 100 * 75 is 675 claimed cells to clear a level (the
// integer division truncating first, upstream's own real behaviour).
#define XNX_CELL_EMPTY 0
#define XNX_CELL_CLAIMED 1
#define XNX_CELL_LINE 2
#define XNX_CELL_CAN_CLAIM 3

#define XNX_CELLSIZE 2
#define XNX_BOARDWIDTH ((LCDWIDTH - 7) / XNX_CELLSIZE)
#define XNX_BOARDHEIGHT (LCDHEIGHT / XNX_CELLSIZE)
#define XNX_LEVELCLEARRATE (XNX_BOARDWIDTH * XNX_BOARDHEIGHT / 100 * 75)

#define XNX_MAX_ENEMYS 10

// Upstream's own real `delayTime` - the movement period in milliseconds.
#define XNX_DELAYTIME 200

#define XNX_STATE_TITLE 0
#define XNX_STATE_PLAY 1

// Upstream's own real `logo` PROGMEM bitmap (64x30) - the title screen art.
int xnxLogoBitmap[242]= {
    64, 30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8,
    0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x7D, 0x08,
    0x01, 0xE6, 0x00, 0x00, 0x00, 0x3E, 0x3D, 0x08,
    0x07, 0xC2, 0x00, 0x00, 0x0F, 0x0E, 0x1E, 0x08,
    0x01, 0xF2, 0x00, 0x01, 0xF3, 0x8E, 0x1E, 0x08,
    0x00, 0x74, 0x01, 0xE0, 0xE3, 0x8E, 0x0F, 0x08,
    0x00, 0x3C, 0x03, 0x38, 0xE3, 0x8E, 0x17, 0x08,
    0x00, 0x3C, 0x03, 0x38, 0xE3, 0x8E, 0x17, 0xC8,
    0x00, 0x1C, 0x07, 0x38, 0xE3, 0x8E, 0x33, 0x88,
    0x00, 0x1F, 0x07, 0x38, 0xE3, 0xCF, 0x80, 0x08,
    0x00, 0x2F, 0x07, 0x18, 0xFB, 0xE0, 0x00, 0x08,
    0x00, 0x63, 0xC1, 0x98, 0xF0, 0x00, 0x00, 0x08,
    0x00, 0x41, 0xE0, 0xF0, 0x00, 0x02, 0x22, 0x08,
    0x00, 0x41, 0xF0, 0x00, 0x00, 0x01, 0x24, 0x08,
    0x00, 0xE3, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x08,
    0x01, 0x80, 0x00, 0x00, 0x00, 0x01, 0x88, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8F, 0xF8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x88, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x24, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x22, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Upstream's own real `heart` bitmap (5x4) - one life, drawn in the right
// status strip.
int xnxHeartBitmap[6]= { 5, 4, 0xD8, 0xF8, 0x70, 0x20 };

// Upstream's own real `line` bitmap (2x2) - one cell of the player's trail,
// a checkerboard dot rather than a solid block so the trail reads as
// distinct from claimed ground.
int xnxLineBitmap[4]= { 2, 2, 0x80, 0x40 };

int xnxState;

bool xnxGameOver;
bool xnxWon;
bool xnxPaused;
bool xnxLevelup;

int xnxPlayerX;
int xnxPlayerY;
int xnxPlayerVX;
int xnxPlayerVY;
int xnxHealth;

int xnxEnemyX[XNX_MAX_ENEMYS];
int xnxEnemyY[XNX_MAX_ENEMYS];
int xnxEnemyVX[XNX_MAX_ENEMYS];
int xnxEnemyVY[XNX_MAX_ENEMYS];
int xnxEnemysCount;

int xnxBoard[XNX_BOARDWIDTH][XNX_BOARDHEIGHT];

// Upstream's own real `prevTime` - a raw millis() snapshot of the last move.
int xnxPrevTime;

// Stands in for real Arduino millis() - see this file's own header comment
// on TIMING. 50ms per real logic tick at this shim's own 20fps default,
// matching real Gamebuino Classic hardware.
int xnxMillis()
{
    return gbFrameCount * 50;
}

// PrintUtils.ino's own real strlen_PF() stand-in - this dialect's strings
// are plain 0-terminated int[] arrays with no separate flash address space.
int xnxStrLen( char* text )
{
    int n = 0;
    while( text[ n ] != 0 )
      n = n + 1;
    return n;
}

// Direct port of PrintUtils.ino's own real printCentered() - note it only
// ever sets the cursor's X, leaving whatever cursorY the caller already
// set (the status banner sets it to 0 itself).
void xnxPrintCentered( char* text )
{
    gbCursorX = ( LCDWIDTH / 2 ) - ( xnxStrLen( text ) * gbFontSize * gbFontWidth / 2 );
    gbPrintString( text );
}

// Direct port of upstream's own real initPlayer() - back to the top edge,
// mid-board, stationary.
void xnxInitPlayer()
{
    xnxPlayerX = XNX_BOARDWIDTH / 2;
    xnxPlayerY = 0;
    xnxPlayerVX = 0;
    xnxPlayerVY = 0;
}

// Direct port of upstream's own real clearBoard() - a one-cell-thick claimed
// ring around an otherwise empty field. This ring is what keeps every later
// unguarded neighbour read in range (see this file's own header comment).
void xnxClearBoard()
{
    int row, col;

    for( row = 0; row < XNX_BOARDHEIGHT; row = row + 1 )
    {
        xnxBoard[ 0 ][ row ] = XNX_CELL_CLAIMED;
        xnxBoard[ XNX_BOARDWIDTH - 1 ][ row ] = XNX_CELL_CLAIMED;

        for( col = 1; col < XNX_BOARDWIDTH - 1; col = col + 1 )
        {
            if( row == 0 || row == XNX_BOARDHEIGHT - 1 )
              xnxBoard[ col ][ row ] = XNX_CELL_CLAIMED;
            else
              xnxBoard[ col ][ row ] = XNX_CELL_EMPTY;
        }
    }
}

// Direct port of upstream's own real initEnemys(). Slots past the live
// enemy count are parked at (-1,-1) with a zero velocity exactly like
// upstream - never read, since every loop over enemies stops at
// xnxEnemysCount.
void xnxInitEnemys()
{
    int i;

    for( i = 0; i < XNX_MAX_ENEMYS; i = i + 1 )
    {
        if( i >= xnxEnemysCount )
        {
            xnxEnemyX[ i ] = -1;
            xnxEnemyY[ i ] = -1;
            xnxEnemyVX[ i ] = 0;
            xnxEnemyVY[ i ] = 0;
        }
        else
        {
            xnxEnemyX[ i ] = 10 + arand( XNX_BOARDWIDTH - 10 - 10 );
            xnxEnemyY[ i ] = 10 + arand( XNX_BOARDHEIGHT - 10 - 10 );

            xnxEnemyVX[ i ] = 1;
            if( arand( 40 ) > 20 )
              xnxEnemyVX[ i ] = -1;

            xnxEnemyVY[ i ] = 1;
            if( arand( 40 ) > 20 )
              xnxEnemyVY[ i ] = -1;
        }
    }
}

// Direct port of upstream's own real initGame(). Note it does NOT reset the
// movement timer - see this file's own header comment on TIMING.
void xnxInitGame()
{
    gbPickRandomSeed(); // no-op, see gamebuinoShim.h's own header comment

    xnxPaused = false;
    xnxGameOver = false;
    xnxWon = false;
    xnxHealth = 3;
    xnxEnemysCount = 1;

    xnxInitPlayer();
    xnxClearBoard();
    xnxInitEnemys();
    gbPlayOK();
}

// Direct port of upstream's own real checkInput(). A direction is only
// accepted while standing on claimed ground, or when it is not an immediate
// reversal - so the player can never double back onto their own fresh trail
// cell, but is free to turn around the instant they are back on safe ground.
void xnxCheckInput()
{
    if( gbPressed( BTN_RIGHT ) )
    {
        if( xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_CLAIMED || xnxPlayerVX != -1 )
        {
            xnxPlayerVX = 1;
            xnxPlayerVY = 0;
        }
    }
    else if( gbPressed( BTN_LEFT ) )
    {
        if( xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_CLAIMED || xnxPlayerVX != 1 )
        {
            xnxPlayerVX = -1;
            xnxPlayerVY = 0;
        }
    }
    else if( gbPressed( BTN_DOWN ) )
    {
        if( xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_CLAIMED || xnxPlayerVY != -1 )
        {
            xnxPlayerVY = 1;
            xnxPlayerVX = 0;
        }
    }
    else if( gbPressed( BTN_UP ) )
    {
        if( xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_CLAIMED || xnxPlayerVY != 1 )
        {
            xnxPlayerVY = -1;
            xnxPlayerVX = 0;
        }
    }
}

// Direct port of upstream's own real clearLine() - wipes the trail after a
// death, leaving the field as it was before the run started.
void xnxClearLine()
{
    int row, col;

    for( row = 0; row < XNX_BOARDHEIGHT; row = row + 1 )
      for( col = 0; col < XNX_BOARDWIDTH; col = col + 1 )
      {
          if( xnxBoard[ col ][ row ] == XNX_CELL_LINE )
            xnxBoard[ col ][ row ] = XNX_CELL_EMPTY;
      }
}

// Direct port of upstream's own real levelUp(). Clearing the level that
// already carries all 10 enemies wins outright; otherwise the board resets
// with one more enemy, one refunded life (capped at 3), and the game
// deliberately left paused behind the "Level Up" banner until Button B.
void xnxLevelUp()
{
    if( xnxEnemysCount >= XNX_MAX_ENEMYS )
    {
        gbPlayOK();
        xnxWon = true;
        return;
    }

    xnxInitPlayer();
    xnxClearBoard();
    xnxEnemysCount = xnxEnemysCount + 1;
    if( xnxHealth < 3 )
      xnxHealth = xnxHealth + 1;
    xnxInitEnemys();
    gbPlayOK();
    xnxLevelup = true;
    xnxPaused = true;
}

// Direct port of upstream's own real claimBoard(): the finished trail
// becomes claimed, every remaining open cell is provisionally marked
// claimable, then a bucket fill starting from the enemies' own cells melts
// the claimable marks back to open ground anywhere an enemy can still reach
// - whatever survives the fill is the enclosed area and gets claimed. See
// this file's own header comment for the `row > 1`/`col > 1` guard quirk
// reproduced verbatim inside the fill.
void xnxClaimBoard()
{
    int row, col, i;

    gbPlayTick();

    // claim line
    for( row = 0; row < XNX_BOARDHEIGHT; row = row + 1 )
      for( col = 0; col < XNX_BOARDWIDTH; col = col + 1 )
      {
          if( xnxBoard[ col ][ row ] == XNX_CELL_LINE )
            xnxBoard[ col ][ row ] = XNX_CELL_CLAIMED;
          else if( xnxBoard[ col ][ row ] == XNX_CELL_EMPTY )
            xnxBoard[ col ][ row ] = XNX_CELL_CAN_CLAIM;
      }

    // clear enemy cells for bucket-fill
    for( i = 0; i < xnxEnemysCount; i = i + 1 )
      xnxBoard[ xnxEnemyX[ i ] ][ xnxEnemyY[ i ] ] = XNX_CELL_EMPTY;

    // Bucket-fill
    bool changed = true;
    while( changed )
    {
        changed = false;

        for( row = 0; row < XNX_BOARDHEIGHT; row = row + 1 )
          for( col = 0; col < XNX_BOARDWIDTH; col = col + 1 )
          {
              if( xnxBoard[ col ][ row ] != XNX_CELL_CAN_CLAIM )
                continue;

              if( ( row > 1 && xnxBoard[ col ][ row - 1 ] == XNX_CELL_EMPTY ) ||
                  ( row < XNX_BOARDHEIGHT - 1 && xnxBoard[ col ][ row + 1 ] == XNX_CELL_EMPTY ) ||
                  ( col > 1 && xnxBoard[ col - 1 ][ row ] == XNX_CELL_EMPTY ) ||
                  ( col < XNX_BOARDWIDTH - 1 && xnxBoard[ col + 1 ][ row ] == XNX_CELL_EMPTY ) )
              {
                  if( xnxBoard[ col ][ row ] == XNX_CELL_CAN_CLAIM )
                    xnxBoard[ col ][ row ] = XNX_CELL_EMPTY;
                  changed = true;
              }
          }
    }

    // claim
    int score = 0;
    for( row = 0; row < XNX_BOARDHEIGHT; row = row + 1 )
      for( col = 0; col < XNX_BOARDWIDTH; col = col + 1 )
      {
          if( xnxBoard[ col ][ row ] == XNX_CELL_CAN_CLAIM )
            xnxBoard[ col ][ row ] = XNX_CELL_CLAIMED;
          if( xnxBoard[ col ][ row ] == XNX_CELL_CLAIMED )
            score = score + 1;
      }

    if( score >= XNX_LEVELCLEARRATE )
      xnxLevelUp();
}

// Direct port of upstream's own real move() - one movement step for the
// player and every live enemy, plus the death and claim checks. See this
// file's own header comment for the three real quirks preserved here
// (`oldPlayerStat` sampled before the trail mark, the duplicated claim
// condition, and the enemies' diagonal corner-clipping).
void xnxMove()
{
    int i;
    int oldPlayerStat = xnxBoard[ xnxPlayerX ][ xnxPlayerY ];

    if( xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_EMPTY )
      xnxBoard[ xnxPlayerX ][ xnxPlayerY ] = XNX_CELL_LINE;

    // move player
    if( xnxPlayerVX == 1 && xnxPlayerX < XNX_BOARDWIDTH - 1 )
      xnxPlayerX = xnxPlayerX + 1;
    else if( xnxPlayerVX == -1 && xnxPlayerX > 0 )
      xnxPlayerX = xnxPlayerX - 1;

    if( xnxPlayerVY == 1 && xnxPlayerY < XNX_BOARDHEIGHT - 1 )
      xnxPlayerY = xnxPlayerY + 1;
    else if( xnxPlayerVY == -1 && xnxPlayerY > 0 )
      xnxPlayerY = xnxPlayerY - 1;

    bool playerAttacked = false;

    // move enemys
    for( i = 0; i < xnxEnemysCount; i = i + 1 )
    {
        if( xnxBoard[ xnxEnemyX[ i ] + xnxEnemyVX[ i ] ][ xnxEnemyY[ i ] ] == XNX_CELL_CLAIMED )
          xnxEnemyVX[ i ] = -xnxEnemyVX[ i ];
        if( xnxBoard[ xnxEnemyX[ i ] ][ xnxEnemyY[ i ] + xnxEnemyVY[ i ] ] == XNX_CELL_CLAIMED )
          xnxEnemyVY[ i ] = -xnxEnemyVY[ i ];

        xnxEnemyX[ i ] = xnxEnemyX[ i ] + xnxEnemyVX[ i ];
        xnxEnemyY[ i ] = xnxEnemyY[ i ] + xnxEnemyVY[ i ];

        if( xnxPlayerX == xnxEnemyX[ i ] && xnxPlayerY == xnxEnemyY[ i ] )
          playerAttacked = true;
        if( xnxBoard[ xnxEnemyX[ i ] ][ xnxEnemyY[ i ] ] == XNX_CELL_LINE )
          playerAttacked = true;
    }

    if( playerAttacked || xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_LINE )
    {
        xnxInitPlayer();
        xnxClearLine();
        gbPlayCancel();
        xnxHealth = xnxHealth - 1;
        if( xnxHealth <= 0 )
          xnxGameOver = true;
    }

    if( !xnxGameOver && ( xnxPlayerVX != 0 || xnxPlayerVY != 0 ) )
    {
        if( oldPlayerStat != XNX_CELL_CLAIMED && xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_CLAIMED )
          xnxClaimBoard();

        if( oldPlayerStat != XNX_CELL_CLAIMED && xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_CLAIMED )
        {
            xnxPlayerVX = 0;
            xnxPlayerVY = 0;
        }
    }
}

// Direct port of upstream's own real drawField(). The player is drawn BLACK
// while out on open ground and WHITE while standing on claimed ground, so it
// stays visible against either background (upstream's own ternary, rewritten
// as if/else).
void xnxDrawField()
{
    int row, col, i;

    // draw board
    gbSetColor( GB_BLACK );
    for( row = 0; row < XNX_BOARDHEIGHT; row = row + 1 )
      for( col = 0; col < XNX_BOARDWIDTH; col = col + 1 )
      {
          if( xnxBoard[ col ][ row ] == XNX_CELL_CLAIMED )
            gbFillRect( col * XNX_CELLSIZE, row * XNX_CELLSIZE, XNX_CELLSIZE, XNX_CELLSIZE );

          if( xnxBoard[ col ][ row ] == XNX_CELL_LINE )
            gbDrawBitmap( col * XNX_CELLSIZE, row * XNX_CELLSIZE, xnxLineBitmap );
      }

    // draw player
    if( xnxBoard[ xnxPlayerX ][ xnxPlayerY ] == XNX_CELL_EMPTY )
      gbSetColor( GB_BLACK );
    else
      gbSetColor( GB_WHITE );
    gbFillRect( xnxPlayerX * XNX_CELLSIZE, xnxPlayerY * XNX_CELLSIZE, XNX_CELLSIZE, XNX_CELLSIZE );

    // draw enemys
    gbSetColor( GB_BLACK );
    for( i = 0; i < xnxEnemysCount; i = i + 1 )
      gbFillRect( xnxEnemyX[ i ] * XNX_CELLSIZE, xnxEnemyY[ i ] * XNX_CELLSIZE, XNX_CELLSIZE, XNX_CELLSIZE );
}

// Direct port of upstream's own real drawScore() - the 8px-wide strip to the
// right of the board, filled solid black, carrying one heart per remaining
// life and the current enemy count at the bottom. Both the hearts and the
// number are drawn WHITE (upstream sets that color once, before the hearts,
// and never changes it back before printing).
void xnxDrawScore()
{
    int i;

    gbSetColor( GB_BLACK );
    gbFillRect( XNX_BOARDWIDTH * XNX_CELLSIZE, 0, LCDWIDTH - XNX_BOARDWIDTH * XNX_CELLSIZE, LCDHEIGHT );

    gbSetColor( GB_WHITE );
    for( i = 0; i < xnxHealth; i = i + 1 )
      gbDrawBitmap( LCDWIDTH - 7, 2 + ( i * 5 ), xnxHeartBitmap );

    gbFontSize = 1;
    gbCursorX = LCDWIDTH - 7;
    gbCursorY = LCDHEIGHT - gbFontHeight - 2;
    gbPrintNumber( xnxEnemysCount );
}

// Stands in for upstream's own real blocking gb.titleScreen(F("Xonix by
// TnxEc2"), logo) call - see this file's own header comment.
void xnxDrawTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, xnxLogoBitmap );

    gbCursorY = 34;
    xnxPrintCentered( "Xonix by TnxEc2" );

    gbCursorY = 41;
    xnxPrintCentered( "PRESS A" );
}

// Direct port of upstream's own real loop() body, minus the title-screen
// branch (hoisted into gameXonix_update() below so the state switch can
// happen without losing a frame - see this file's own header comment).
void xnxUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        // upstream: titleMenu(false) - a blocking gb.titleScreen() call that
        // takes over the display immediately, so the title is drawn in this
        // very tick here too.
        xnxState = XNX_STATE_TITLE;
        xnxDrawTitle();
        return;
    }

    if( !xnxGameOver && !xnxWon )
    {
        if( gbPressed( BTN_B ) )
        {
            xnxPaused = !xnxPaused;
            xnxLevelup = false;
        }

        if( !xnxPaused )
        {
            xnxCheckInput();

            int currentTime = xnxMillis();
            if( currentTime - xnxPrevTime >= XNX_DELAYTIME )
            {
                xnxMove();
                xnxPrevTime = currentTime;
            }
        }
    }
    else
    {
        if( gbPressed( BTN_A ) )
          xnxInitGame();
    }

    xnxDrawField();
    xnxDrawScore();

    if( xnxPaused || xnxLevelup || xnxGameOver || xnxWon )
    {
        gbCursorY = 0;
        gbSetColor( GB_WHITE );
        gbFillRect( 0, 0, LCDWIDTH, gbFontHeight + 1 );
        gbSetColor( GB_BLACK );

        if( xnxWon ) xnxPrintCentered( " You Won! " );
        else if( xnxGameOver ) xnxPrintCentered( " Game over " );
        else if( xnxLevelup ) xnxPrintCentered( " Level Up " );
        else if( xnxPaused ) xnxPrintCentered( " PAUSE " );
    }
}

void gameXonix_init()
{
    gbBegin();

    xnxPrevTime = 0; // upstream's own real global initializer, at boot only

    // upstream: setup() calls titleMenu(true), whose own blocking
    // titleScreen() runs FIRST and only calls initGame() once it returns -
    // so the board is genuinely not built until the title screen is
    // dismissed, and initGame()'s own gb.sound.playOK() is heard exactly
    // once, at that moment, not at boot. gameXonix_update()'s own
    // XNX_STATE_TITLE branch below is that same call site.
    xnxState = XNX_STATE_TITLE;
}

void gameXonix_update()
{
    if( !gbUpdate() ) return;

    if( xnxState == XNX_STATE_TITLE )
    {
        if( gbPressed( BTN_A ) )
        {
            // upstream's own titleScreen() returns here and execution falls
            // straight through into the same tick's gameplay/draw code.
            xnxInitGame();
            xnxState = XNX_STATE_PLAY;
        }
        else
        {
            xnxDrawTitle();
            gbRenderFrame();
            return;
        }
    }

    xnxUpdatePlay();
    gbRenderFrame();
}
