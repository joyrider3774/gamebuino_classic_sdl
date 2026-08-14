#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Lights Out AD (author: 94k, license: WTFPL - both per this project's own
// staging metadata; the real upstream `lightsOutAD.ino`/`Readme.txt` never
// embed an author name or license header of their own beyond the generic
// WTFPL boilerplate block at the very top of the .ino, and the original
// git:// repo link is dead, so this game was recovered via a direct file
// download rather than a live clone). A real Lights Out puzzle variant with
// four distinct rule sets and a fully configurable board size:
//
// - Classic: pressing a cell toggles it and its 4 orthogonal neighbours -
//   turn every light off to win.
// - Alt. (Alternative): identical to Classic except the pressed cell itself
//   never toggles, only its neighbours do.
// - Threeway: cells cycle through 3 states (off/dim/on) instead of 2, both
//   the pressed cell and its neighbours advance one step each press - win
//   once every cell is back to the "off" state.
// - SuperPos (Super Position): identical 3-state cycling to Threeway, but
//   the middle state counts as a valid win state alongside "off" (only the
//   literal "on" state, value 1, blocks a win) - some starting boards can
//   only be solved by leaving certain cells in that superposition state.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every global symbol got a
// `lout`-prefixed name (this cartridge has no linker - every ported game
// shares one flat global namespace). Upstream's own `random(min,max)` calls
// became `arand(n)`/`min + arand(max-min)` (this project's own established
// ranged-random rewrite convention - see gameFlappyBirdo.c's own header
// comment), and its own `B`-binary bitmap literals (the `field[3][6]` cell
// icons and the `PX00`/`PX01`/`PX10`/`PX11` macros used to build the
// `screenL[]` title logo) were converted to `0x`-hex byte-for-byte, no bit
// reshuffling needed (this shim's own gbDrawBitmap() uses the exact same
// row-major/MSB-first layout real Display::drawBitmap() does).
//
// BOARD STORAGE: upstream's own `newBoard()`/`introBoard()` each `malloc()`
// a `char*` board sized exactly to the requested `x*y` (up to 21x12=252
// cells) and `free()` it once done. This dialect's `malloc`/`free` do work
// (see VIRCON32_C_DIALECT.md), but a single fixed-size `int loutBoard[252]`
// global covers every possible board size with no dynamic allocation at
// all - the same "safer, already-proven-elsewhere route" gameMinesweeper.c's
// own header comment describes choosing for its own board storage. Only the
// first `width*height` cells are ever read or written by any function below
// (indexed as `y*width+x`, matching upstream's own real indexing exactly),
// so stale data left over past that range from a previous, larger game is
// never observed.
//
// BLOCKING LOOPS -> EXPLICIT STATE MACHINE: upstream's own real control flow
// is four nested blocking loops - `introBoard()` (a real animated reveal of
// the title logo, run once from `setup()`), the real blocking
// `gb.titleScreen(...)` call right after it, `loop()`'s own outer
// `while(1)` settings menu, and `game()`'s own inner `while(true)` play
// loop (itself calling the blocking `won()` loop on a win) - converted here
// into six explicit `LoutAppState` values (`LOUT_APP_INTRO_REVEAL`/
// `_INTRO_LINGER`/`_TITLE`/`_MENU`/`_PLAY`/`_WON`), matching the "blocking
// loop -> explicit resumable state" treatment used throughout this project
// (see gamePong.c's own header comment; gameMinesweeper.c/game2048.c for
// the closest real precedent of a title-screen + persistent-settings-menu
// shape).
//
// THE INTRO REVEAL'S OWN REAL EARLY-EXIT QUIRK IS PRESERVED EXACTLY:
// upstream's own reveal loop draws the board, THEN (every 3rd tick) applies
// one queued toggle, THEN checks for a Button-A press to `break` - in that
// order. A press on the exact tick a toggle also fires still lets that
// toggle apply before breaking (this port's own `loutUpdateIntroReveal()`
// performs the same three steps in the same order, so an A-press mid-reveal
// behaves identically). Breaking out of the reveal early still runs the
// full 3-tick "let it linger" phase afterward - upstream never lets Button A
// skip that part, and neither does this port.
//
// UPSTREAM'S OWN REAL "CHANGE GAME" SUBSTITUTION: the settings menu's own
// Button C branch calls real `gb.changeGame()` (a real-hardware "switch
// cartridge slot" OS feature with no equivalent in this single-cartridge
// shared-menu model) - substituted with a re-entry into the title screen
// state instead, the same substitution gameGruniozerca.c's/gameCrazyTown.c's
// own header comments already established for their own upstream Exit/
// changeGame options.
//
// SOUND MUTE HAS NO DIRECT SHIM EQUIVALENT: upstream's own settings-menu
// "Sound" toggle reads/writes real hardware volume directly
// (`gb.sound.getVolume()`/`gb.sound.setVolume(...)`), which this shim has no
// primitive for (no `gbSetVolume()`/`gbGetVolume()` - the cartridge's own
// global mute is a separate, orthogonal Button-Y toggle in `portVircon32.c`,
// not a per-game setting). Reimplemented the same way gameFlappyBirdo.c's
// own header comment describes for its own upstream mute toggle: a local
// `loutSoundOn` bool gates a small `loutPlayTick()`/`loutPlayOK()`/
// `loutPlayCancel()` wrapper trio used everywhere upstream calls
// `gb.sound.playTick()`/`playOK()`/`playCancel()` from the settings menu
// onward. `loutSoundOn` starts `true` (upstream's own initial value depends
// on real hardware's already-saved volume state, which has no equivalent
// here - defaulting to on is the closest real behavior). A genuine, subtle
// upstream quirk this reproduces exactly: `gb.sound.playTick()` is called
// once, unconditionally, right when Button A is pressed on the settings
// menu - only afterward does the Sound row's own handler flip the volume.
// So toggling sound OFF still plays that tick's own click (queued while
// still unmuted), but toggling sound ON plays it silently (still muted at
// the moment the click was queued) - reproduced here by calling
// `loutPlayTick()` (which reads the *current*, not-yet-flipped
// `loutSoundOn`) before flipping the bool, in the same order upstream calls
// `gb.sound.playTick()` before its own volume-setting ternary. The intro
// reveal's own `gb.sound.playTick()` calls are NOT gated by this at all -
// they run before the settings menu (and its `sound` variable) even exist
// in upstream's own real program flow, so they're always unconditional
// here too, exactly like real hardware.
//
// `gb.battery.show = false/true` (a real-hardware-only cosmetic battery
// indicator, hidden while playing an oversized board so it doesn't overlap
// the play field) is dropped outright, matching every other port's own
// treatment of that field (see gameCatcher.c's own header comment).
// `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a documented no-op
// (this project's own established precedent for every upstream call to it).
//
// No real font is ever selected upstream (`setFont()` is never called), so
// this port relies on this shim's own real `gbFont3x5` default exactly like
// upstream relies on real hardware's own default - see gamebuinoShim.c's
// own `gbBegin()`. No real `setColor()` call exists upstream either (real
// hardware's own default draw color, BLACK, is exactly what every draw call
// here needs), so this port makes no `gbSetColor()` calls at all, matching
// upstream precisely rather than adding a redundant call upstream never
// makes.

// Maximum board dimensions and rule count - upstream's own real `XMAX`/
// `YMAX`/`RULES` constants. 21*4=84 and 12*4=48 exactly fill the real
// 84x48 LCD when a full-size board is chosen (matching upstream's own real
// hardcoded intro-reveal board size of 21x12 exactly).
#define LOUT_XMAX 21
#define LOUT_YMAX 12
#define LOUT_RULES 4
#define LOUT_BOARD_MAX 252 // LOUT_XMAX * LOUT_YMAX

enum LoutAppState
{
    LOUT_APP_INTRO_REVEAL = 0,
    LOUT_APP_INTRO_LINGER = 1,
    LOUT_APP_TITLE        = 2,
    LOUT_APP_MENU         = 3,
    LOUT_APP_PLAY         = 4,
    LOUT_APP_WON          = 5
};

// Upstream's own real `field[3][6]` PROGMEM bitmap table (three 4x4 cell
// icons: off, on, superposition) - B-binary converted to hex byte-for-byte,
// split into three named bitmaps plus a real `int*[3]` array-of-pointers,
// the same already-proven pattern used project-wide for small bitmap sets
// (e.g. gameMinesweeper.c's own `mineFlagBitmap`/`mineNeutralBitmap`).
int loutField0Bitmap[6] = { 4, 4, 0x00, 0x60, 0x60, 0x00 };
int loutField1Bitmap[6] = { 4, 4, 0x00, 0x00, 0x00, 0x00 };
int loutField2Bitmap[6] = { 4, 4, 0x60, 0x90, 0x90, 0x60 };
int* loutFieldBitmaps[3] = { loutField0Bitmap, loutField1Bitmap, loutField2Bitmap };

// Upstream's own real `screenL[]` PROGMEM title-logo bitmap (56x26), built
// upstream from `PX00`/`PX01`/`PX10`/`PX11`/`EMPTYL` macros - expanded here
// to plain hex bytes (PX00=0x00, PX01=0x06, PX10=0x60, PX11=0x66, EMPTYL=7
// zero bytes, matching upstream's own real macro definitions exactly).
int loutScreenLBitmap[184] = {
    56, 26,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x66, 0x00, 0x60, 0x00, 0x60, 0x06,
    0x00, 0x66, 0x00, 0x60, 0x00, 0x60, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x06, 0x00, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x66, 0x60, 0x66, 0x60, 0x66, 0x00,
    0x00, 0x66, 0x60, 0x66, 0x60, 0x66, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x60, 0x00, 0x60, 0x60, 0x60,
    0x00, 0x00, 0x60, 0x00, 0x60, 0x60, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x66, 0x00, 0x00, 0x60, 0x60, 0x06,
    0x00, 0x66, 0x00, 0x00, 0x60, 0x60, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Upstream's own real `logo[5][14]` board-configuration table used only by
// `introBoard()` - flattened to a plain 1D array (`row*14+col` indexing,
// matching every other flattened 2D grid in this project, e.g.
// gameMinesweeper.c's own `mineIdx()`).
int loutLogoPattern[70] = {
    1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0,
    1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1,
    1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0
};

int loutAppState;

// Settings-menu persistent state - upstream's own real `x`/`y`/`pos`/
// `rule`/`sound` locals, declared once at the very top of `loop()` (which
// itself runs exactly once, forever) so they persist across every game
// played in the same session - ported as real globals, initialized once in
// gameLightsOutAD_init() and never reset by loutBeginMenu() itself, for the
// exact same persistence.
int loutWidth;
int loutHeight;
int loutMenuPos;
int loutRule;
bool loutSoundOn;

// Active game state - upstream's own real `game()`/`won()` locals.
int loutBoard[LOUT_BOARD_MAX];
int loutBoardW;
int loutBoardH;
int loutBoardRule;
int loutPX;
int loutPY;
int loutHOff;
int loutVOff;
int loutStartT;
int loutToggles;
int loutWonTime;

// Intro-reveal state - upstream's own real `introBoard()` locals.
int loutIntroBoard[LOUT_BOARD_MAX];
int loutIntroTogX[10];
int loutIntroTogY[10];
int loutIntroStep;
int loutIntroH;
int loutIntroLinger;

// See this file's own header comment on the settings-menu "Sound" toggle -
// these three wrappers are used everywhere upstream calls
// `gb.sound.playTick()`/`playOK()`/`playCancel()` from the settings menu
// onward (NOT inside the intro reveal, which runs before `loutSoundOn`
// exists in upstream's own real program flow either).
void loutPlayTick()
{
    if( loutSoundOn )
      gbPlayTick();
}

void loutPlayOK()
{
    if( loutSoundOn )
      gbPlayOK();
}

void loutPlayCancel()
{
    if( loutSoundOn )
      gbPlayCancel();
}

// Upstream's own real `ruleSet[]`/`toggleS[]` string tables - ported as
// if/else-if chains rather than an `int*[N]` array of string-literal
// pointers (no precedent anywhere in this project for an array of
// string-literal pointers, unlike the already-proven `int*[N]`
// array-of-*bitmap*-pointers pattern used above - see
// gameGlaciGlaca.c's own header comment for the identical situation there).
void loutPrintRuleName( int rule )
{
    if( rule == 0 ) gbPrintString( "Classic" );
    else if( rule == 1 ) gbPrintString( "Alt." );
    else if( rule == 2 ) gbPrintString( "Threeway" );
    else gbPrintString( "SuperPos" );
}

void loutPrintSoundState( bool on )
{
    if( on ) gbPrintString( "On" );
    else gbPrintString( "Off" );
}

// Direct port of upstream's own real toggle() - flips a cell and its 4
// orthogonal neighbours. `rule<2` (Classic/Alt.) is a real 2-state XOR
// toggle, only self-toggling for Classic (`rule==0`); `rule>=2`
// (Threeway/SuperPos) is a real 3-state cycle that always advances the
// pressed cell too.
void loutToggle( int* board, int w, int h, int px, int py, int rule )
{
    if( rule < 2 )
    {
        if( px > 0 ) board[ py * w + px - 1 ] = board[ py * w + px - 1 ] ^ 1;
        if( px < w - 1 ) board[ py * w + px + 1 ] = board[ py * w + px + 1 ] ^ 1;
        if( py > 0 ) board[ ( py - 1 ) * w + px ] = board[ ( py - 1 ) * w + px ] ^ 1;
        if( py < h - 1 ) board[ ( py + 1 ) * w + px ] = board[ ( py + 1 ) * w + px ] ^ 1;
        if( rule == 0 ) board[ py * w + px ] = board[ py * w + px ] ^ 1;
    }
    else
    {
        if( px > 0 ) board[ py * w + px - 1 ] = ( board[ py * w + px - 1 ] + 1 ) % 3;
        if( px < w - 1 ) board[ py * w + px + 1 ] = ( board[ py * w + px + 1 ] + 1 ) % 3;
        if( py > 0 ) board[ ( py - 1 ) * w + px ] = ( board[ ( py - 1 ) * w + px ] + 1 ) % 3;
        if( py < h - 1 ) board[ ( py + 1 ) * w + px ] = ( board[ ( py + 1 ) * w + px ] + 1 ) % 3;
        board[ py * w + px ] = ( board[ py * w + px ] + 1 ) % 3;
    }
}

// Direct port of upstream's own real draw() - draws the board border, then
// every cell's real icon bitmap.
void loutDrawBoard( int* board, int w, int h, int hOff, int vOff )
{
    gbDrawFastVLine( hOff - 1, vOff - 1, h * 4 + 2 );
    gbDrawFastHLine( hOff - 1, vOff - 1, w * 4 + 2 );
    gbDrawFastVLine( hOff + w * 4, vOff, h * 4 + 1 );
    gbDrawFastHLine( hOff, vOff + h * 4, w * 4 );

    int i, k;
    for( i = 0; i < h; i = i + 1 )
    {
        for( k = 0; k < w; k = k + 1 )
          gbDrawBitmap( hOff + k * 4, vOff + i * 4, loutFieldBitmaps[ board[ i * w + k ] ] );
    }
}

void loutBeginMenu()
{
    loutAppState = LOUT_APP_MENU;
}

void loutBeginTitle()
{
    loutAppState = LOUT_APP_TITLE;
}

// Direct port of upstream's own real newBoard() - fills the board, seeds
// SuperPos's own extra superposition cells, then scrambles it with a real
// 1-in-3 chance of toggling each cell (a real, load-bearing upstream
// property: this scramble pass always uses a real, valid sequence of
// toggle() calls, so every generated board is provably solvable by
// construction - undoing the exact same sequence of presses always solves
// it, even though the RNG never checks solvability explicitly).
void loutNewBoard( int w, int h, int rule )
{
    int i;
    for( i = 0; i < w * h; i = i + 1 )
      loutBoard[ i ] = 0;

    if( rule == 3 )
    {
        for( i = 0; i < w * h; i = i + 1 )
        {
            if( arand( 3 ) == 1 )
              loutBoard[ i ] = 2;
        }
    }

    int j, k;
    for( j = 0; j < h; j = j + 1 )
    {
        for( k = 0; k < w; k = k + 1 )
        {
            if( arand( 3 ) == 1 )
              loutToggle( loutBoard, w, h, k, j, rule );
        }
    }
}

// Direct port of upstream's own real win check half of won() - Classic/
// Alt./Threeway require every cell to be fully "off" (0); SuperPos also
// accepts the superposition state (2), only the literal "on" state (1)
// blocks a win.
bool loutCheckWon()
{
    int i;
    for( i = 0; i < loutBoardW * loutBoardH; i = i + 1 )
    {
        if( loutBoardRule < 3 )
        {
            if( loutBoard[ i ] != 0 ) return false;
        }
        else
        {
            if( loutBoard[ i ] == 1 ) return false;
        }
    }
    return true;
}

// Upstream's own real won() plays gb.sound.playOK() once immediately on
// entry, before its own blocking display loop even starts - reproduced
// here as the transition into LOUT_APP_WON. Real upstream also computes
// `time = gb.frameCount - startT` here, once, before that loop begins -
// not inside it - so the displayed time is a real snapshot, not a running
// clock. `loutWonTime` captures that same snapshot at the same point.
void loutBeginWon()
{
    loutAppState = LOUT_APP_WON;
    loutWonTime = gbFrameCount - loutStartT;
    loutPlayOK();
}

// FIXED, NOT PRESERVED - see this file's own header comment: real
// upstream's own `time = gb.frameCount - startT` sits once, before its
// blocking display loop begins, not inside it - a real static snapshot,
// not a running clock. An earlier pass here recomputed it fresh every
// tick instead (since this port's own per-tick state-machine architecture
// calls this function every frame), making the displayed time keep
// counting up for as long as the player lingered on this screen - a real,
// live-reported divergence from real hardware, not a preserved quirk.
// Fixed by printing the snapshot `loutBeginWon()` already took.
void loutUpdateWon()
{
    int time = loutWonTime;

    gbCursorX = 6;
    gbCursorY = 5;
    gbPrintString( "You won!" );

    gbCursorX = 6;
    gbCursorY = gbCursorY + 8;
    gbPrintNumber( loutBoardW );
    gbPrintString( "x" );
    gbPrintNumber( loutBoardH );
    gbPrintString( " " );
    loutPrintRuleName( loutBoardRule );

    gbCursorX = 6;
    gbCursorY = gbCursorY + 8;
    gbPrintString( "Time: " );
    gbPrintNumber( time / 20 );
    gbPrintString( "." );
    gbPrintNumber( time % 20 * 5 );
    gbPrintString( " s" );

    gbCursorX = 6;
    gbCursorY = gbCursorY + 8;
    gbPrintString( "Toggles: " );
    gbPrintNumber( loutToggles );

    gbCursorX = 6;
    gbCursorY = gbCursorY + 8;
    gbPrintString( "Press A to exit." );

    if( gbPressed( BTN_A ) )
    {
        loutPlayOK(); // upstream's own real second playOK() call, after the win screen's own loop breaks
        loutBeginMenu();
    }
}

// Direct port of the settings-menu "Play" option's own real body: generates
// a fresh board, then checks for the (rare but real) case where the random
// scramble already happens to be solved - upstream's own real game() checks
// this once, before the board is ever shown, and returns immediately
// without ever entering the play loop if so.
void loutBeginGame( int w, int h, int rule )
{
    loutBoardW = w;
    loutBoardH = h;
    loutBoardRule = rule;
    loutPX = 0;
    loutPY = 0;
    loutHOff = ( LOUT_XMAX - w ) * 2;
    loutVOff = ( LOUT_YMAX - h ) * 2;

    loutNewBoard( w, h, rule );

    loutStartT = gbFrameCount;
    loutToggles = 0;

    if( loutCheckWon() )
      loutBeginWon();
    else
      loutAppState = LOUT_APP_PLAY;
}

// Direct port of upstream's own real game()'s per-tick play loop body -
// same real if/else-if button priority order as upstream (B resets the
// cursor's row, then A toggles, then the 4 movement directions, then C
// quits back to the settings menu).
void loutUpdatePlay()
{
    loutDrawBoard( loutBoard, loutBoardW, loutBoardH, loutHOff, loutVOff );

    gbDrawPixel( loutHOff + loutPX * 4, loutVOff + loutPY * 4 );
    gbDrawPixel( loutHOff + loutPX * 4 + 3, loutVOff + loutPY * 4 );
    gbDrawPixel( loutHOff + loutPX * 4, loutVOff + loutPY * 4 + 3 );
    gbDrawPixel( loutHOff + loutPX * 4 + 3, loutVOff + loutPY * 4 + 3 );

    if( gbPressed( BTN_B ) )
      loutPY = 0;
    else if( gbPressed( BTN_A ) )
    {
        loutToggle( loutBoard, loutBoardW, loutBoardH, loutPX, loutPY, loutBoardRule );
        loutToggles = loutToggles + 1;
        loutPlayTick();

        if( loutCheckWon() )
          loutBeginWon();
    }
    else if( gbPressed( BTN_UP ) )
    {
        if( loutPY <= 0 ) loutPY = loutBoardH - 1;
        else loutPY = loutPY - 1;
    }
    else if( gbPressed( BTN_DOWN ) )
    {
        if( loutPY >= loutBoardH - 1 ) loutPY = 0;
        else loutPY = loutPY + 1;
    }
    else if( gbPressed( BTN_LEFT ) )
    {
        if( loutPX <= 0 ) loutPX = loutBoardW - 1;
        else loutPX = loutPX - 1;
    }
    else if( gbPressed( BTN_RIGHT ) )
    {
        if( loutPX >= loutBoardW - 1 ) loutPX = 0;
        else loutPX = loutPX + 1;
    }
    else if( gbPressed( BTN_C ) )
    {
        loutPlayCancel();
        loutBeginMenu();
    }
}

// Direct port of upstream's own real loop() body - the persistent settings
// menu (Play/Width/Height/Rules/Sound), same real if/else-if input
// priority order as upstream (A confirms, then LEFT/RIGHT repeat-adjust the
// selected row, then UP/DOWN move the cursor, then C - upstream's own real
// `gb.changeGame()` - re-enters the title screen instead, see this file's
// own header comment).
void loutUpdateMenu()
{
    gbDrawCircle( 6, 7 + loutMenuPos * 8, 2 );

    gbCursorX = 12;
    gbCursorY = 5;
    gbPrintString( "Play" );

    gbCursorX = 12;
    gbCursorY = gbCursorY + 8;
    gbPrintString( "Width:  " );
    gbPrintNumber( loutWidth );

    gbCursorX = 12;
    gbCursorY = gbCursorY + 8;
    gbPrintString( "Height: " );
    gbPrintNumber( loutHeight );

    gbCursorX = 12;
    gbCursorY = gbCursorY + 8;
    gbPrintString( "Rules:  " );
    loutPrintRuleName( loutRule );

    gbCursorX = 12;
    gbCursorY = gbCursorY + 8;
    gbPrintString( "Sound:  " );
    loutPrintSoundState( loutSoundOn );

    if( gbPressed( BTN_A ) )
    {
        loutPlayTick(); // see this file's own header comment on the real mute-click ordering quirk this preserves
        if( loutMenuPos == 0 )
          loutBeginGame( loutWidth, loutHeight, loutRule );
        else if( loutMenuPos == 4 )
          loutSoundOn = !loutSoundOn;
    }
    else if( gbRepeat( BTN_LEFT, 4 ) )
    {
        if( loutMenuPos == 1 )
        {
            if( loutWidth == 1 ) loutWidth = LOUT_XMAX;
            else loutWidth = loutWidth - 1;
            loutPlayTick();
        }
        else if( loutMenuPos == 2 )
        {
            if( loutHeight == 1 ) loutHeight = LOUT_YMAX;
            else loutHeight = loutHeight - 1;
            loutPlayTick();
        }
        else if( loutMenuPos == 3 )
        {
            if( loutRule == 0 ) loutRule = LOUT_RULES - 1;
            else loutRule = loutRule - 1;
            loutPlayTick();
        }
    }
    else if( gbRepeat( BTN_RIGHT, 4 ) )
    {
        if( loutMenuPos == 1 )
        {
            loutWidth = gbMax( 1, ( loutWidth + 1 ) % ( LOUT_XMAX + 1 ) );
            loutPlayTick();
        }
        else if( loutMenuPos == 2 )
        {
            loutHeight = gbMax( 1, ( loutHeight + 1 ) % ( LOUT_YMAX + 1 ) );
            loutPlayTick();
        }
        else if( loutMenuPos == 3 )
        {
            loutRule = ( loutRule + 1 ) % LOUT_RULES;
            loutPlayTick();
        }
    }
    else if( gbPressed( BTN_UP ) )
    {
        loutPlayTick();
        if( loutMenuPos == 0 ) loutMenuPos = 4;
        else loutMenuPos = loutMenuPos - 1;
    }
    else if( gbPressed( BTN_DOWN ) )
    {
        loutPlayTick();
        loutMenuPos = ( loutMenuPos + 1 ) % 5;
    }
    else if( gbPressed( BTN_C ) )
      loutBeginTitle(); // upstream: gb.changeGame() - see this file's own header comment
}

// Direct port of upstream's own real introBoard() setup half - builds the
// title-logo reveal board (top/right non-logo areas lit, the logo pattern
// embedded in the middle) and picks 10 random near-logo toggle points to
// reveal one at a time.
void loutBeginIntro()
{
    int i, h, k;

    for( i = 0; i < 126; i = i + 1 )
      loutIntroBoard[ i ] = 1;

    for( h = 0; h < 5; h = h + 1 )
    {
        for( k = 0; k < 14; k = k + 1 )
        {
            loutIntroBoard[ i ] = loutLogoPattern[ h * 14 + k ];
            i = i + 1;
        }
        for( k = 0; k < 7; k = k + 1 )
        {
            loutIntroBoard[ i ] = 1;
            i = i + 1;
        }
    }

    while( i < LOUT_BOARD_MAX )
    {
        loutIntroBoard[ i ] = 1;
        i = i + 1;
    }

    for( i = 0; i < 10; i = i + 1 )
    {
        int x = arand( 14 );
        int y = 5 + arand( 7 );
        loutToggle( loutIntroBoard, LOUT_XMAX, LOUT_YMAX, x, y, 0 );
        loutIntroTogX[ i ] = x;
        loutIntroTogY[ i ] = y;
    }

    loutIntroStep = 0;
    loutIntroH = 0;
    loutAppState = LOUT_APP_INTRO_REVEAL;
}

// See this file's own header comment on the exact real per-tick ordering
// this preserves (draw, then maybe apply a queued toggle, then check for a
// dismiss press).
void loutUpdateIntroReveal()
{
    loutDrawBoard( loutIntroBoard, LOUT_XMAX, LOUT_YMAX, 0, 0 );

    loutIntroH = loutIntroH + 1;
    if( loutIntroH > 2 )
    {
        loutIntroH = 0;
        loutToggle( loutIntroBoard, LOUT_XMAX, LOUT_YMAX, loutIntroTogX[ loutIntroStep ], loutIntroTogY[ loutIntroStep ], 0 );
        gbPlayTick(); // unconditional - see this file's own header comment
        loutIntroStep = loutIntroStep + 1;
    }

    if( gbPressed( BTN_A ) || loutIntroStep >= 10 )
    {
        loutIntroLinger = 0;
        loutAppState = LOUT_APP_INTRO_LINGER;
    }
}

// Direct port of upstream's own real "let it linger for a while" loop - 3
// more ticks of the fully (or early-exited) revealed board before the real
// title screen takes over.
void loutUpdateIntroLinger()
{
    loutDrawBoard( loutIntroBoard, LOUT_XMAX, LOUT_YMAX, 0, 0 );

    loutIntroLinger = loutIntroLinger + 1;
    if( loutIntroLinger >= 3 )
      loutBeginTitle();
}

// Direct port of upstream's own real `gb.titleScreen(F("Lights Out AD by"),
// screenL)` call - real hardware's own titleScreen() draws the bitmap, the
// given text, and a "press A" prompt, then blocks for Button A. Restored as
// an explicit state (see gameMinesweeper.c/game2048.c's own header comments
// for the identical situation) with the logo centered horizontally (real
// hardware's own fixed x=0 placement only makes sense alongside its own
// boot-logo furniture, which this shim has no equivalent for).
void loutUpdateTitle()
{
    gbDrawBitmap( ( LCDWIDTH - 56 ) / 2, 2, loutScreenLBitmap );

    gbCursorX = ( LCDWIDTH - 17 * gbFontWidth ) / 2;
    gbCursorY = 30;
    gbPrintString( "Lights Out AD by" );

    gbCursorX = ( LCDWIDTH - 7 * gbFontWidth ) / 2;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      loutBeginMenu();
}

void gameLightsOutAD_init()
{
    gbBegin();
    gbPickRandomSeed(); // no-op, see gamebuinoShim.h's own header comment

    loutWidth = 5;
    loutHeight = 5;
    loutMenuPos = 0;
    loutRule = 0;
    loutSoundOn = true;

    loutBeginIntro();
}

void gameLightsOutAD_update()
{
    if( !gbUpdate() ) return;

    if( loutAppState == LOUT_APP_INTRO_REVEAL ) loutUpdateIntroReveal();
    else if( loutAppState == LOUT_APP_INTRO_LINGER ) loutUpdateIntroLinger();
    else if( loutAppState == LOUT_APP_TITLE ) loutUpdateTitle();
    else if( loutAppState == LOUT_APP_MENU ) loutUpdateMenu();
    else if( loutAppState == LOUT_APP_PLAY ) loutUpdatePlay();
    else loutUpdateWon();

    gbRenderFrame();
}
