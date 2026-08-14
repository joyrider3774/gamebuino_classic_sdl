#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Invaders (Yoda Zhang / "yodasvideoarcade", License: None specified -
// yodasvideoarcade.com/gamebuino.php). A real Space Invaders clone: move
// the ship left/right, shoot the descending invader grid before it reaches
// the bottom, a UFO saucer occasionally flies across the top for bonus
// points, and 4 bunkers erode as they're hit by either side's shots.
// Upstream ships as 6 real `.ino` tabs sharing one translation unit
// (invaders/standard/specific/nonstandard/images/sounds) - all 6 were read
// in full before writing this port.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `gb.display.cursorX =`/
// `cursorY =` ported unchanged (plain globals here too). `random(2)` became
// `arand(2)` (this dialect's own established RNG helper). `gb.pickRandom
// Seed()` became `gbPickRandomSeed()`, a documented no-op. `gb.battery.
// show=false;` was dropped outright - purely cosmetic on real hardware,
// matching every other port's own treatment of the same call. Upstream's
// own `String gamestatus` (compared with `==` against `"title"`/
// `"running"`/etc, real Arduino String objects with real heap churn every
// tick) became a plain `int invState` compared against `InvState` enum
// constants - the same "flatten a real single-instance library into plain
// C globals/functions" treatment this whole project uses, just applied to
// a state variable instead of a class instance this time.
//
// `gb.setFrameRate(30)` is a GENUINE explicit upstream call (unlike Pong's
// own missing call, which turned out to be a real bug in that port's own
// history - see this project's own CLAUDE.md) - ported literally as
// `gbSetFrameRate(30)`, a real, intentionally-chosen 30fps rate, not
// something needing correction.
//
// STATE MACHINE - upstream's own real `loop()` shape, preserved exactly:
// `if (gamestatus=="newgame"){newgame();}` / `if (gamestatus=="newlevel")
// {newlevel();}` / `if (gamestatus=="running"){...}` / `if (gamestatus==
// "title"){showtitle();}` / `if (gamestatus=="gameover"){...}` are FIVE
// SEPARATE consecutive `if` statements upstream, not an if/else-if chain -
// meaning pressing A on the title screen falls through newgame->newlevel->
// running ALL WITHIN THE SAME real tick (newgame() sets gamestatus=
// "newlevel", which the very next `if` immediately notices and acts on,
// same for newlevel()->"running"), so a fresh game's very first frame is
// already a fully-drawn, playing level - no blank setup frames. Likewise a
// GAMEOVER transition triggered mid-tick by invInvaderLogic()/
// invHandleDeath() (partway through the running block) is immediately
// noticed by the gameover check later in that SAME chain, so the "GAME
// OVER" box appears on the very same tick a game genuinely ends. Ported
// here as five separate `if` statements in `gameInvaders_update()` in the
// same order, specifically NOT else-if, to reproduce this real same-tick
// fallthrough behavior exactly - not an oversight.
//
// A GENUINE DELIBERATE DEVIATION, clearly bounded: upstream's own
// `checkbuttons()` (called first inside the running block) can itself call
// the real BLOCKING `gb.titleScreen(...)` on a Button-C press, which
// freezes real hardware inside its own internal update loop for as many
// real frames as it takes the player to press A again - only once THAT
// call finally returns does the rest of the running block's own remaining
// draw calls (drawplayership() etc) execute, painting one more gameplay
// frame directly on top of the splash's own last-drawn pixels before the
// NEXT real tick finally flips to the "title" scoreboard screen (since the
// buffer is only cleared once per real gb.update() cycle, and the
// blocking call's own internal loop already consumed a whole one on its
// last iteration). The net effect on real hardware is one harmless,
// literally-unobservable-at-20fps glitch frame. Rather than reproduce that
// buffer-overlap artifact bit-for-bit, `invCheckButtons()`'s own C-press
// branch here just sets `invState = INV_STATE_SPLASH` and `invUpdateRunning
// ()` returns immediately right after calling it if state is no longer
// RUNNING - matching the clean "blocking loop -> explicit resumable state"
// treatment gamePong.c/gameSimonbuino.c/every other port here already
// uses for the exact same real gb.titleScreen()-mid-play pattern. This
// early return is added ONLY for this one case (a genuine real BLOCKING
// call upstream) - it is deliberately NOT added after the mid-sequence
// GAMEOVER transitions above, since those are just plain variable
// assignments on real hardware (not blocking calls), so the rest of that
// tick's own drawing genuinely does keep running past them there, exactly
// like upstream.
//
// SPLASH SCREEN (real `gb.titleScreen(name, gamelogo)`, called at boot,
// again on a mid-game Button-C press, and a third time from the custom
// "title" scoreboard screen's own Button-C press) - reproduces the real
// `Gamebuino::titleScreen()` source's own actual logo placement, read
// directly from the real library (`more games/Gamebuino-Classic/
// Gamebuino.cpp`): the passed name string prints at cursor (0,12), then
// the passed logo bitmap draws at `(0, 12+logoOffset)` where `logoOffset`
// is the active font's own real `fontHeight` if the name string is
// non-empty, else 0. This game's default font is never changed from real
// hardware's own default (font3x5, fontHeight=6) anywhere in the real
// source, and all 3 real call sites pass a non-empty name - so the offset
// is always a constant 6, landing the logo at a fixed `(0, 18)`, computed
// once here rather than re-derived at runtime. The real function's own
// blinking A-button-icon hint, B-button mute-toggle readout, and C-button
// "flash the SD-card loader" gesture are all dropped, matching this
// project's own established simplification for every other ported
// `titleScreen()` recreation (see gameSimonbuino.c/gameFlappyBirdo.c) - no
// extra "PRESS A" stand-in text was added here either, since (unlike
// those two games) this game's own real name string already occupies the
// equivalent line.
//
// A PRESERVED REAL UPSTREAM INCONSISTENCY: two of the three real
// `gb.titleScreen(...)` call sites (`setup()`, and the Button-C handler
// inside the RUNNING state's own `checkbuttons()`) pass `"    Yoda's"`
// (4 leading spaces); the third (the scoreboard "title" screen's own
// Button-C handler) passes plain `"Yoda's"` with none - almost certainly
// an unintentional inconsistency in the original source rather than
// meaningful design, but real hardware genuinely would show this exact
// difference, so it's preserved via an `invSplashIndented` flag set by
// whichever of the 3 call sites triggers the splash, rather than silently
// unifying the two texts.
//
// A SHARED-VARIABLE QUIRK, PRESERVED BY CALL ORDER: `invadershotframe`
// (real `invInvaderShotFrame` here) is BOTH the invader-bomb sprite's own
// blink-animation frame counter AND the player ship's own death-flash
// frame selector (`playership[1+invadershotframe]`) - the same variable,
// reused for two unrelated sprites. Upstream's own real `loop()` reads it
// for the death flash (`drawplayership()`) BEFORE updating it for the bomb
// animation (`invadershot()`, later in the same tick) - so the death flash
// always shows the PREVIOUS tick's toggle value. Reproduced automatically
// here just by calling `invDrawPlayerShip()` before `invInvaderShot()` in
// the exact same order inside `invUpdateRunning()`, with no special-casing
// needed.
//
// A PRESERVED POTENTIAL DOUBLE-COLLISION QUIRK: `invInvaderShot()`'s own
// per-shot loop checks bunker collision, then player-ship collision, then
// player-shot collision, unconditionally in sequence with no `continue`/
// early-exit after a shot is consumed by an earlier check in that same
// pass (upstream never added one either) - so in the narrow edge case
// where a consumed shot's reset position (`-1`, `-1`) coincidentally still
// satisfies a LATER check's own box test in the same iteration (e.g. the
// player-ship check when `shipx==0`, making its own box include x=0), a
// single real shot could theoretically be double-processed within one
// tick. Preserved exactly as upstream wrote it, matching this project's
// own "preserve real upstream behavior/bugs by default" norm - the window
// is narrow enough it was very likely never once observed on real
// hardware either.
//
// REAL BITMAP ART RESTORED. Every real `const byte PROGMEM NAME[] = {...}`
// array in `images.ino` (gamelogo, invader[8], playership[3], bunker[5],
// bomb[2], saucer[2]) was decoded via a small one-off Python script reading
// the real `.ino` source directly, converting every `B00000000`-style
// Arduino binary literal to a plain int and re-rendering each frame's own
// bits back into an ASCII preview to confirm the conversion against the
// expected sprite shapes before trusting it (same verification discipline
// as gameFlappyBirdo.c's own restoration pass) - not hand-transcribed.
// Every real PROGMEM byte became one plain `int` cell in a
// `int[N] name = { width, height, byte0, byte1, ... }` array (this
// dialect's own `int[N] name` array-declaration order, not C's
// `int name[N]`), exactly the shape `gbDrawBitmap()` expects.
// CHECKED FOR THE FILL/MASK-UNDERNEATH BUG CLASS (found twice in
// gameFlappyBirdo.c this project's own history): every real
// `gb.display.drawBitmap(...)` call site in this game's own source was
// checked directly, and NONE of them are preceded by a `setColor(GRAY)`/
// mask-bitmap pass or a plain `fillRect()` - every sprite here (invaders,
// ship, bunkers, bombs, saucer) is a single, self-contained `drawBitmap()`
// call straight onto the plain white background. The only real
// `setColor(0)`+`fillRect()` pair in this whole game is `showgameover()`'s
// own opaque white text-box backing (a UI box, not a sprite) - ported
// verbatim in `invShowGameOver()`.
//
// BITMAP FRAME SELECTION: upstream indexes each sprite's own real 2D
// PROGMEM array with a single flat index (`invader[invaders[i]+
// invaderframe[i]]` picks one of 8 rows this way; `playership[1+
// invadershotframe]`, `bunker[bunkers[i]]`, `bomb[frame]`, and
// `saucer[saucers]` all do the same over their own smaller arrays). Ported
// here as small `invDrawXBitmap(x, y, idx)` if/else-chain helpers
// selecting among separately-declared named 1D arrays (`invBmpInvader0`..
// `7` etc), rather than declaring one real 2D array and passing a single-
// indexed row (which decays to a pointer) straight into `gbDrawBitmap()`.
// The latter isn't known to fail here - gameSimonbuino.c's own
// `simonSoundFx[fx][i]` proves 2D-array element reads work fine in this
// dialect - but passing a 2D array's own inner row specifically as a
// decayed `int*` bitmap pointer into `gbDrawBitmap()` has no proven
// precedent anywhere in this codebase, and this project's own established
// convention (see the `switch`-statement avoidance elsewhere in this file
// and throughout the project) is to not be the first port to gamble on an
// unproven construct when an already-proven one (if/else over named 1D
// arrays, used by every other ported game's own bitmap-frame-selection
// code) does the same job.
//
// `invInvaderX`/`Y`/`Invaders`/`InvaderFrame` are sized `[40]` here, not
// upstream's own declared `[55]` - real dead headroom, confirmed directly:
// nothing in any of the 6 real upstream files ever indexes past 39.
//
// `invInvaderLogic()`'s own real `do { ... } while (invaders[ctr]==-1);`
// (advance the invader-cycle index, wrapping and reversing direction past
// 39, skipping already-dead slots) was rewritten as a plain `while` loop
// with an explicit `found` flag instead - not because `do/while` is known
// to fail here, simply because it's untested in this codebase, matching
// this project's own general policy of preferring an already-proven
// looping construct (`while`, used elsewhere in this project) over an
// unproven one when the rewrite is this mechanical.
//
// SOUND: `soundfx[8][8]` copied verbatim into `invSoundFx` (matching
// gameSimonbuino.c's own `simonSoundFx[5][8]` precedent for a 2D sound-
// parameter table). Real `playsoundfx(fxno, channel)` drove a small
// hand-rolled tracker effect per call (`gb.sound.command(...)` for
// waveform/volume-slide/pitch-slide, then a `playNote()`) - this shim has
// no tracker/pattern engine at all (see gamebuinoShim.h's own header
// comment), so `invPlaySfx()` only forwards the pitch/duration pair
// (`invSoundFx[fxno][1]`/`[7]`) to `gbPlayNote()`; the channel parameter
// is kept (for exact call-site parity with upstream) but unused, and the
// waveform/slide shaping is dropped outright, matching gameFlappyBirdo.c's
// own `flapPlaySfx()` precedent for the exact same simplification.
//
// The digit-count cursor-offset formula upstream uses twice (`14-2*
// (score>9)-2*(score>99)-2*(score>999)`, relying on C++'s own implicit
// bool-to-int promotion under multiplication) was rewritten here as a
// small `invDigitCount()` helper instead of a literal port - not because
// this dialect's own `bool` (confirmed 1/0-valued) is known to reject that
// arithmetic, simply because no other file in this project has yet tested
// multiplying a bool by an int, and the helper is exactly as short while
// only relying on already-proven `if`/`return` arithmetic.
//
// Upstream never calls `gb.display.setFont(...)` anywhere in any of its 6
// files - real hardware's own default font (font3x5) is used for every
// screen this game ever draws, unchanged for the whole game. This port
// therefore never calls `gbSetFont()` either (gbBegin() already selects
// `gbFont3x5` by default) - every real cursor-position/text layout number
// upstream uses ported completely unchanged, with no narrower/rescaled
// substitute font needed at all (unlike several earlier ports in this
// project, made before real fonts existed here).
//
// EEPROM PERSISTENCE ADDED, BEYOND REAL UPSTREAM - no `EEPROM.read()`/
// `EEPROM.write()` calls exist anywhere in this game's real source, so
// `invHighscore` was originally genuine in-session-only state, explicitly
// reset to 0 by upstream's own real `setup()` every single launch. Added
// directly on request once an audit found this game displays a real
// highscore that never survives a cartridge reboot: `gameInvaders_init()`'s
// own real `invHighscore = 0;` line is now a real `eeprom_read_word(0)`
// load instead (with the same `==0xFFFF` fresh-EEPROM-cell reset check
// already established elsewhere in this project, e.g. gameCrabator.c/
// gameDescent.c, rather than trusting a raw 65535 sentinel) - the very
// same effective "0 on a genuinely fresh card" outcome upstream's own
// hardcoded `=0` produced, just no longer discarding a real earlier save.
// Saved via `eeprom_write_word(0, invHighscore)` at the exact point
// upstream's own real highscore-tracking line already updates it in
// memory - a one-shot write per new high score, not a per-frame write.

#define INV_STATE_SPLASH 0
#define INV_STATE_TITLE 1
#define INV_STATE_NEWGAME 2
#define INV_STATE_NEWLEVEL 3
#define INV_STATE_RUNNING 4
#define INV_STATE_GAMEOVER 5

int invState;
bool invSplashIndented;

int invScore = 0;
int invHighscore = 0;
int invLives;
int invGameLevel;
int invShipX;

int invInvaderX[40];
int invInvaderY[40];
int invInvaders[40];
int invInvaderFrame[40];
int invInvaderAnz;
int invInvaderCtr;
int invInvaderSound;
int invCheckDir;
int invNextXDir;
int invNextYDir;
int invInvaderXr;
int invInvaderYr;

int invInvaderShotX[4];
int invInvaderShotY[4];
int invInvaderShotFrame;
int invInvaderShots;
int invInvaderShotTimer;

int invBunkers[4];

int invShotX;
int invShotY;

int invYeahTimer;
int invInfoShow;
int invDeadCounter;

int invSaucerX;
int invSaucerDir;
int invSaucers;
int invSaucerTimer;
int invSaucerWait;

// -----------------------------------------------------------------------------
// Real upstream sprite bitmaps (see this file's own header comment for the
// decode/verification process). Every entry is { width, height, byte0, ... }.
// -----------------------------------------------------------------------------

int invBmpLogo[210]= { 64, 26,
    255, 255, 255, 255, 255, 255, 255, 255,
    0, 0, 0, 0, 0, 0, 0, 0,
    59, 157, 220, 113, 241, 251, 225, 240,
    59, 157, 220, 249, 249, 251, 243, 248,
    59, 157, 220, 249, 249, 251, 251, 248,
    59, 221, 221, 253, 253, 195, 187, 184,
    59, 221, 221, 253, 221, 195, 187, 184,
    59, 221, 221, 221, 221, 195, 187, 128,
    59, 253, 221, 221, 221, 243, 187, 192,
    59, 253, 221, 221, 221, 243, 241, 224,
    59, 253, 221, 253, 221, 243, 224, 240,
    59, 253, 221, 253, 221, 195, 240, 120,
    59, 189, 221, 253, 221, 195, 184, 56,
    59, 189, 221, 221, 221, 195, 187, 184,
    59, 189, 253, 221, 221, 195, 187, 184,
    59, 156, 249, 221, 249, 251, 187, 248,
    59, 156, 113, 221, 249, 251, 187, 248,
    59, 156, 33, 221, 241, 251, 185, 240,
    0, 0, 0, 0, 0, 0, 0, 0,
    255, 255, 255, 255, 255, 255, 255, 255,
    0, 0, 0, 0, 0, 0, 0, 0,
    166, 198, 106, 236, 230, 110, 102, 206,
    170, 170, 138, 74, 138, 170, 138, 168,
    170, 174, 234, 74, 202, 236, 142, 172,
    74, 170, 42, 74, 138, 170, 138, 168,
    78, 202, 228, 236, 238, 170, 234, 206, };

int invBmpInvader0[7]= { 7, 5, 56, 214, 254, 40, 198 };
int invBmpInvader1[7]= { 7, 5, 56, 214, 254, 68, 40 };
int invBmpInvader2[7]= { 7, 5, 16, 186, 214, 124, 40 };
int invBmpInvader3[7]= { 7, 5, 56, 84, 254, 146, 40 };
int invBmpInvader4[7]= { 7, 5, 56, 84, 124, 68, 40 };
int invBmpInvader5[7]= { 7, 5, 56, 84, 124, 40, 84 };
int invBmpInvader6[7]= { 7, 5, 84, 130, 84, 130, 84 };
int invBmpInvader7[7]= { 7, 5, 84, 130, 84, 130, 84 };

int invBmpShip0[6]= { 7, 4, 16, 124, 254, 254 };
int invBmpShip1[6]= { 7, 4, 138, 64, 4, 146 };
int invBmpShip2[6]= { 7, 4, 146, 4, 64, 138 };

int invBmpBunker0[7]= { 8, 5, 126, 255, 255, 231, 195 };
int invBmpBunker1[7]= { 8, 5, 126, 219, 255, 165, 195 };
int invBmpBunker2[7]= { 8, 5, 110, 219, 118, 165, 195 };
int invBmpBunker3[7]= { 8, 5, 102, 217, 86, 165, 66 };
int invBmpBunker4[7]= { 8, 5, 34, 137, 82, 165, 66 };

int invBmpBomb0[6]= { 2, 4, 128, 64, 128, 64 };
int invBmpBomb1[6]= { 2, 4, 64, 128, 64, 128 };

int invBmpSaucer0[10]= { 11, 4, 31, 0, 106, 192, 255, 224, 100, 192 };
int invBmpSaucer1[10]= { 11, 4, 93, 192, 85, 64, 85, 64, 93, 192 };

// Real soundfx[8][8] table, copied verbatim (see this file's own header
// comment - only fields [1] (pitch) and [7] (duration) of each row are
// actually used by invPlaySfx() below).
int invSoundFx[8][8]= {
    { 1, 57, 57, 1, 1, 1, 5, 6 },  // 0 = shoot
    { 0, 0, 68, 1, 0, 0, 7, 4 },   // 1 = invader hit
    { 1, 15, 57, 1, 1, 2, 7, 15 }, // 2 = player hit
    { 0, 10, 60, 1, 0, 0, 7, 6 },  // 3 = saucer
    { 0, 5, 58, 0, 1, 5, 5, 2 },   // 4 = invaders 1
    { 0, 4, 58, 0, 1, 5, 5, 2 },   // 5 = invaders 2
    { 0, 2, 58, 0, 1, 5, 5, 2 },   // 6 = invaders 3
    { 0, 1, 58, 0, 1, 5, 5, 2 },   // 7 = invaders 4
};

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

int invDigitCount( int n )
{
    if( n > 999 ) return 4;
    if( n > 99 ) return 3;
    if( n > 9 ) return 2;
    return 1;
}

void invPlaySfx( int fxno, int channel )
{
    channel = channel; // unused - this shim's gbPlayNote() has no channel parameter
    gbPlayNote( invSoundFx[ fxno ][ 1 ], invSoundFx[ fxno ][ 7 ] );
}

void invDrawInvaderBitmap( int x, int y, int idx )
{
    if( idx == 0 ) gbDrawBitmap( x, y, invBmpInvader0 );
    else if( idx == 1 ) gbDrawBitmap( x, y, invBmpInvader1 );
    else if( idx == 2 ) gbDrawBitmap( x, y, invBmpInvader2 );
    else if( idx == 3 ) gbDrawBitmap( x, y, invBmpInvader3 );
    else if( idx == 4 ) gbDrawBitmap( x, y, invBmpInvader4 );
    else if( idx == 5 ) gbDrawBitmap( x, y, invBmpInvader5 );
    else if( idx == 6 ) gbDrawBitmap( x, y, invBmpInvader6 );
    else gbDrawBitmap( x, y, invBmpInvader7 );
}

void invDrawShipBitmap( int x, int y, int idx )
{
    if( idx == 0 ) gbDrawBitmap( x, y, invBmpShip0 );
    else if( idx == 1 ) gbDrawBitmap( x, y, invBmpShip1 );
    else gbDrawBitmap( x, y, invBmpShip2 );
}

void invDrawBunkerBitmap( int x, int y, int idx )
{
    if( idx == 0 ) gbDrawBitmap( x, y, invBmpBunker0 );
    else if( idx == 1 ) gbDrawBitmap( x, y, invBmpBunker1 );
    else if( idx == 2 ) gbDrawBitmap( x, y, invBmpBunker2 );
    else if( idx == 3 ) gbDrawBitmap( x, y, invBmpBunker3 );
    else gbDrawBitmap( x, y, invBmpBunker4 );
}

void invDrawBombBitmap( int x, int y, int idx )
{
    if( idx == 0 ) gbDrawBitmap( x, y, invBmpBomb0 );
    else gbDrawBitmap( x, y, invBmpBomb1 );
}

void invDrawSaucerBitmap( int x, int y, int idx )
{
    if( idx == 0 ) gbDrawBitmap( x, y, invBmpSaucer0 );
    else gbDrawBitmap( x, y, invBmpSaucer1 );
}

// -----------------------------------------------------------------------------
// State transitions
// -----------------------------------------------------------------------------

void invBeginSplash( bool indented )
{
    invSplashIndented = indented;
    invState = INV_STATE_SPLASH;
}

// == upstream newgame()
void invNewGame()
{
    invScore = 0;
    invLives = 3;
    invGameLevel = 0;
    invShipX = 40;
    invShotX = -1;
    invShotY = -1;
    invDeadCounter = -1;
    invSaucers = -1;

    int i;
    for( i = 0; i < 4; i++ )
    {
        invInvaderShotX[ i ] = -1;
        invInvaderShotY[ i ] = -1;
    }

    invState = INV_STATE_NEWLEVEL;
}

// == upstream newlevel()
void invNewLevel()
{
    invInvaderAnz = 40;
    invInvaderCtr = 39;
    invInvaderXr = 2;
    invInvaderYr = 0;
    invCheckDir = 0;
    invNextXDir = 2;
    invNextYDir = 0;
    invYeahTimer = 0;
    invInvaderShotTimer = 60;
    invSaucerTimer = 240;

    int down = invGameLevel * 2;
    if( invGameLevel > 8 ) down = 16;

    int i;
    for( i = 0; i < 8; i++ )
    {
        invInvaderX[ i ] = 10 + i * 8;
        invInvaderX[ i + 8 ] = 10 + i * 8;
        invInvaderX[ i + 16 ] = 10 + i * 8;
        invInvaderX[ i + 24 ] = 10 + i * 8;
        invInvaderX[ i + 32 ] = 10 + i * 8;
        invInvaderY[ i ] = 0 + down;
        invInvaderY[ i + 8 ] = 6 + down;
        invInvaderY[ i + 16 ] = 12 + down;
        invInvaderY[ i + 24 ] = 18 + down;
        invInvaderY[ i + 32 ] = 24 + down;
        invInvaders[ i ] = 4;
        invInvaders[ i + 8 ] = 2;
        invInvaders[ i + 16 ] = 2;
        invInvaders[ i + 24 ] = 0;
        invInvaders[ i + 32 ] = 0;
        invInvaderFrame[ i ] = 0;
        invInvaderFrame[ i + 8 ] = 0;
        invInvaderFrame[ i + 16 ] = 0;
        invInvaderFrame[ i + 24 ] = 0;
        invInvaderFrame[ i + 32 ] = 0;
    }

    for( i = 0; i < 4; i++ )
    {
        invBunkers[ i ] = 0;
        if( invGameLevel > 5 ) invBunkers[ i ] = -1;
    }

    invState = INV_STATE_RUNNING;
}

// -----------------------------------------------------------------------------
// Running - input / player
// -----------------------------------------------------------------------------

// == upstream checkbuttons()
void invCheckButtons()
{
    if( gbRepeat( BTN_LEFT, 1 ) && invShipX > 0 && invDeadCounter == -1 )
      invShipX = invShipX - 1;
    if( gbRepeat( BTN_RIGHT, 1 ) && invShipX < 78 && invDeadCounter == -1 )
      invShipX = invShipX + 1;

    // real blocking gb.titleScreen() call - see header comment for why this
    // is the one case that gets an early return in invUpdateRunning().
    if( gbPressed( BTN_C ) )
      invBeginSplash( true ); // "    Yoda's" (4 leading spaces)

    if( gbPressed( BTN_A ) && invShotX == -1 && invDeadCounter == -1 )
    {
        invShotX = invShipX + 3;
        invShotY = 41;
        invPlaySfx( 0, 0 );
    }
}

// == upstream drawplayership() + handledeath()
void invHandleDeath()
{
    invDeadCounter = invDeadCounter - 1;
    if( invDeadCounter == 0 )
    {
        invDeadCounter = -1;
        invLives = invLives - 1;
        invShipX = 0;
        if( invLives == 0 )
          invState = INV_STATE_GAMEOVER;
    }
}

void invDrawPlayerShip()
{
    if( invDeadCounter == -1 )
      invDrawShipBitmap( invShipX, 44, 0 );
    else
    {
        invDrawShipBitmap( invShipX, 44, 1 + invInvaderShotFrame );
        invHandleDeath();
    }
}

// == upstream drawplayershot()
void invDrawPlayerShot()
{
    if( invShotX != -1 )
    {
        invShotY = invShotY - 2;
        gbDrawLine( invShotX, invShotY, invShotX, invShotY + 2 );
        if( invShotY < 0 )
        {
            invShotX = -1;
            invShotY = -1;
        }
    }
}

// -----------------------------------------------------------------------------
// Running - invaders
// -----------------------------------------------------------------------------

// == upstream invaderlogic() - the real `do { ... } while(...)` advance-and-
// skip-dead-slots loop is rewritten as a plain `while` (see header comment).
void invInvaderLogic()
{
    if( invInvaderAnz > 0 )
    {
        invCheckDir = 0;

        bool found = false;
        while( !found )
        {
            invInvaderCtr = invInvaderCtr + 1;
            if( invInvaderCtr > 39 )
            {
                invInvaderCtr = 0;
                invCheckDir = 1;
                invInvaderSound = ( invInvaderSound + 1 ) % 4;
                invPlaySfx( invInvaderSound + 4, 1 );
            }
            if( invInvaders[ invInvaderCtr ] != -1 ) found = true;
        }

        // change direction?
        if( invCheckDir == 1 )
        {
            if( invNextYDir != 0 )
            {
                invInvaderXr = 0;
                invInvaderYr = 2;
            }
            else
            {
                invInvaderXr = invNextXDir;
                invInvaderYr = 0;
            }
            invCheckDir = 0;
        }

        // change invader position
        invInvaderX[ invInvaderCtr ] = invInvaderX[ invInvaderCtr ] + invInvaderXr;
        invInvaderY[ invInvaderCtr ] = invInvaderY[ invInvaderCtr ] + invInvaderYr;

        // determine bunker removal if invaders are too low
        if( invInvaderY[ invInvaderCtr ] > 34 )
        {
            int i;
            for( i = 0; i < 4; i++ )
              invBunkers[ i ] = -1;
        }

        // determine game over if invaders reach bottom
        if( invInvaderY[ invInvaderCtr ] > 40 )
          invState = INV_STATE_GAMEOVER;

        // determine screen border hit -> go down, then change direction
        if( invInvaderX[ invInvaderCtr ] > 75 && invInvaderXr > 0 )
        {
            invNextXDir = -2;
            invNextYDir = 2;
        }
        if( invInvaderX[ invInvaderCtr ] < 2 && invInvaderXr < 0 )
        {
            invNextXDir = 2;
            invNextYDir = 2;
        }
        if( invInvaderYr != 0 )
          invNextYDir = 0;

        // change invader shape
        invInvaderFrame[ invInvaderCtr ] = ( invInvaderFrame[ invInvaderCtr ] + 1 ) % 2;

        // remove killed invader
        if( invInvaders[ invInvaderCtr ] == 6 )
        {
            invInvaders[ invInvaderCtr ] = -1;
            invInvaderAnz = invInvaderAnz - 1;
        }

        // release invader shot
        if( invInvaderShotTimer <= 0 && invInvaderShots < invGameLevel + 1 && invInvaderShots < 4 && invInvaderY[ invInvaderCtr ] < 40 )
        {
            invInvaderShotTimer = 40 - invGameLevel * 10;
            invInvaderShots = invInvaderShots + 1;
            int flag = 0;
            int u;
            for( u = 0; u < 4; u++ )
            {
                if( flag == 0 && invInvaderShotX[ u ] == -1 )
                {
                    invInvaderShotX[ u ] = invInvaderX[ invInvaderCtr ] + 1;
                    invInvaderShotY[ u ] = invInvaderY[ invInvaderCtr ];
                    flag = 1;
                }
            }
        }
    }
}

// == upstream drawinvaders()
void invDrawInvaders()
{
    invInfoShow = 1;

    int i;
    for( i = 0; i < 40; i++ )
    {
        if( invInvaders[ i ] != -1 )
        {
            invDrawInvaderBitmap( invInvaderX[ i ], invInvaderY[ i ], invInvaders[ i ] + invInvaderFrame[ i ] );
            if( invInvaderY[ i ] < 5 )
              invInfoShow = 0;
        }

        int checkl = invInvaderX[ i ];
        int checkr = invInvaderX[ i ] + 6;
        int checkt = invInvaderY[ i ];
        int checkb = invInvaderY[ i ] + 4;
        if( invInvaders[ i ] == 4 )
        {
            checkl = checkl + 1;
            checkr = checkr - 1;
        }
        if( invInvaders[ i ] != -1 && invInvaders[ i ] != 6 && invShotX >= checkl && invShotX <= checkr && invShotY + 2 >= checkt && invShotY <= checkb )
        {
            invScore = invScore + invInvaders[ i ] * 10 + 10;
            invInvaders[ i ] = 6;
            invShotX = -1;
            invShotY = -1;
            invPlaySfx( 1, 0 );
        }
    }
}

// == upstream invadershot() - see header comment on the preserved
// double-collision-per-tick edge case.
void invInvaderShot()
{
    invInvaderShotTimer = invInvaderShotTimer - 1;
    invInvaderShotFrame = ( invInvaderShotFrame + 1 ) % 2;

    int i;
    for( i = 0; i < 4; i++ )
    {
        if( invInvaderShotX[ i ] != -1 )
        {
            invInvaderShotY[ i ] = invInvaderShotY[ i ] + 1;
            invDrawBombBitmap( invInvaderShotX[ i ], invInvaderShotY[ i ], invInvaderShotFrame );

            // invadershot & bunker
            int u;
            for( u = 0; u < 4; u++ )
            {
                int checkl = 11 + u * 18;
                int checkr = 11 + u * 18 + 7;
                int checkt = 39;
                int checkb = 43;
                if( invBunkers[ u ] != -1 && invInvaderShotX[ i ] + 1 >= checkl && invInvaderShotX[ i ] <= checkr && invInvaderShotY[ i ] + 3 >= checkt && invInvaderShotY[ i ] <= checkb )
                {
                    invBunkers[ u ] = invBunkers[ u ] + 1;
                    if( invBunkers[ u ] > 4 ) invBunkers[ u ] = -1;
                    invInvaderShotX[ i ] = -1;
                    invInvaderShotY[ i ] = -1;
                    invInvaderShots = invInvaderShots - 1;
                }
            }

            // invadershot & player
            int shipCheckl = invShipX;
            int shipCheckr = invShipX + 6;
            int shipCheckt = 44;
            int shipCheckb = 47;
            if( invDeadCounter == -1 && invInvaderShotX[ i ] + 1 >= shipCheckl && invInvaderShotX[ i ] <= shipCheckr && invInvaderShotY[ i ] + 3 >= shipCheckt && invInvaderShotY[ i ] <= shipCheckb )
            {
                invDeadCounter = 60;
                invPlaySfx( 2, 2 );
            }

            // invadershot & playershot
            int shotCheckl = invInvaderShotX[ i ];
            int shotCheckr = invInvaderShotX[ i ] + 1;
            int shotCheckt = invInvaderShotY[ i ];
            int shotCheckb = invInvaderShotY[ i ] + 3;
            if( invShotX >= shotCheckl && invShotX <= shotCheckr && invShotY + 2 >= shotCheckt && invShotY <= shotCheckb )
            {
                invShotX = -1;
                invShotY = -1;
                invInvaderShotX[ i ] = -1;
                invInvaderShotY[ i ] = -1;
                invInvaderShots = invInvaderShots - 1;
            }

            // invadershot off bottom of screen?
            if( invInvaderShotY[ i ] > 47 )
            {
                invInvaderShotX[ i ] = -1;
                invInvaderShotY[ i ] = -1;
                invInvaderShots = invInvaderShots - 1;
            }
        }
    }
}

// == upstream drawbunkers()
void invDrawBunkers()
{
    int i;
    for( i = 0; i < 4; i++ )
    {
        int checkl = 11 + i * 18;
        int checkr = 11 + i * 18 + 7;
        int checkt = 39;
        int checkb = 43;
        if( invBunkers[ i ] != -1 && invShotX >= checkl && invShotX <= checkr && invShotY + 2 >= checkt && invShotY <= checkb )
        {
            invBunkers[ i ] = invBunkers[ i ] + 1;
            invShotX = -1;
            invShotY = -1;
            if( invBunkers[ i ] > 4 ) invBunkers[ i ] = -1;
        }

        if( invBunkers[ i ] != -1 )
          invDrawBunkerBitmap( 11 + i * 18, 39, invBunkers[ i ] );
    }
}

// == upstream saucerappears()
void invSaucerAppears()
{
    invSaucerTimer = invSaucerTimer - 1;
    if( invSaucerTimer <= 0 )
    {
        invSaucerTimer = 240;
        if( invInfoShow == 1 && invSaucers == -1 )
        {
            invSaucers = 0;
            int i = arand( 2 );
            if( i == 0 )
            {
                invSaucerX = 0;
                invSaucerDir = 1;
            }
            else
            {
                invSaucerX = 73;
                invSaucerDir = -1;
            }
        }
    }
}

// == upstream movesaucer()
void invMoveSaucer()
{
    if( invSaucers == 0 )
    {
        invSaucerX = invSaucerX + invSaucerDir;
        if( invSaucerX <= 0 || invSaucerX >= 73 )
          invSaucers = -1;
        if( invSaucerX % 5 == 0 )
          invPlaySfx( 3, 0 );

        int checkl = invSaucerX;
        int checkr = invSaucerX + 10;
        int checkt = 0;
        int checkb = 3;
        if( invShotX >= checkl && invShotX <= checkr && invShotY + 2 >= checkt && invShotY <= checkb )
        {
            invScore = invScore + 100;
            invSaucers = 1;
            invShotX = -1;
            invShotY = -1;
            invSaucerWait = 30;
            invPlaySfx( 1, 0 );
        }
    }
}

// == upstream drawsaucer()
void invDrawSaucer()
{
    if( invSaucers != -1 )
    {
        invDrawSaucerBitmap( invSaucerX, 0, invSaucers );
        if( invSaucers == 1 )
        {
            invSaucerWait = invSaucerWait - 1;
            if( invSaucerWait <= 0 )
              invSaucers = -1;
        }
    }
}

// == upstream nextlevelcheck()
void invNextLevelCheck()
{
    if( invInvaderAnz == 0 )
    {
        invYeahTimer = invYeahTimer + 1;
        if( invYeahTimer >= 90 )
        {
            invGameLevel = invGameLevel + 1;
            invState = INV_STATE_NEWLEVEL;
        }
    }
}

// == upstream showscore()
void invShowScore()
{
    if( invInfoShow == 1 && invSaucers == -1 )
    {
        if( invLives > 1 ) invDrawShipBitmap( 0, 0, 0 );
        if( invLives > 2 ) invDrawShipBitmap( 9, 0, 0 );

        gbCursorX = 42 - 2 * ( invDigitCount( invScore ) - 1 );
        gbCursorY = 0;
        gbPrintNumber( invScore );

        gbCursorX = 72;
        gbPrintNumber( invGameLevel + 1 );
    }
}

// == upstream loop()'s own "running" branch
void invUpdateRunning()
{
    invCheckButtons();
    if( invState != INV_STATE_RUNNING ) return; // real blocking titleScreen() - see header comment

    invDrawPlayerShip();
    invDrawPlayerShot();
    invInvaderLogic();
    invDrawInvaders();
    invInvaderShot();
    invNextLevelCheck();
    invDrawBunkers();
    invSaucerAppears();
    invMoveSaucer();
    invDrawSaucer();
    invShowScore();
}

// -----------------------------------------------------------------------------
// Splash / title / game over screens
// -----------------------------------------------------------------------------

// == real Gamebuino::titleScreen(name, gamelogo) - see header comment for
// the real (0, 12+logoOffset) placement this reproduces.
void invUpdateSplash()
{
    gbCursorX = 0;
    gbCursorY = 12;
    if( invSplashIndented )
      gbPrintString( "    Yoda's" );
    else
      gbPrintString( "Yoda's" );
    gbDrawBitmap( 0, 18, invBmpLogo );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        invState = INV_STATE_TITLE;
    }
}

// == upstream showtitle()
void invUpdateTitle()
{
    if( invScore > invHighscore )
    {
        invHighscore = invScore;
        eeprom_write_word( 0, invHighscore );
    }

    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "  LAST         HIGH" );

    gbCursorX = 14 - 2 * ( invDigitCount( invScore ) - 1 );
    gbCursorY = 6;
    gbPrintNumber( invScore );

    gbCursorX = 66 - 2 * ( invDigitCount( invHighscore ) - 1 );
    gbCursorY = 6;
    gbPrintNumber( invHighscore );

    gbDrawBitmap( 10, 13, invBmpLogo );

    gbCursorX = 0;
    gbCursorY = 42;
    gbPrintString( " A: PLAY     C: QUIT" );

    if( gbPressed( BTN_A ) )
    {
        invState = INV_STATE_NEWGAME;
        gbPlayOK();
    }
    if( gbPressed( BTN_C ) )
      invBeginSplash( false ); // plain "Yoda's" - see header comment
}

// == upstream showgameover()
void invShowGameOver()
{
    gbSetColor( 0 );
    gbFillRect( 22, 16, 39, 9 );
    gbSetColor( 1 );
    gbCursorX = 24;
    gbCursorY = 18;
    gbPrintString( "GAME OVER" );
    gbDrawRect( 22, 16, 39, 9 );
    gbCursorX = 4;
    gbCursorY = 42;
    gbPrintString( "PRESS B TO CONTINUE" );

    if( gbPressed( BTN_B ) )
    {
        invState = INV_STATE_TITLE;
        gbPlayOK();
    }
}

// == upstream loop()'s own "gameover" branch: freeze-frame the invader grid
// exactly as it stood, then draw the message box on top.
void invUpdateGameOver()
{
    int i;
    for( i = 0; i < 40; i++ )
    {
        if( invInvaders[ i ] != -1 )
          invDrawInvaderBitmap( invInvaderX[ i ], invInvaderY[ i ], invInvaders[ i ] + invInvaderFrame[ i ] );
    }
    invShowGameOver();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameInvaders_init()
{
    gbBegin();
    gbSetFrameRate( 30 ); // real, explicit upstream rate - see header comment
    invHighscore = eeprom_read_word( 0 );
    if( invHighscore == 0xFFFF ) invHighscore = 0;
    invBeginSplash( true ); // "    Yoda's" - real setup()'s own initial splash
}

void gameInvaders_update()
{
    if( !gbUpdate() ) return;

    // Five separate `if`s, not else-if - see header comment on the real
    // same-tick newgame->newlevel->running (and mid-tick ->gameover)
    // fallthrough this reproduces.
    if( invState == INV_STATE_NEWGAME ) invNewGame();
    if( invState == INV_STATE_NEWLEVEL ) invNewLevel();
    if( invState == INV_STATE_RUNNING ) invUpdateRunning();
    if( invState == INV_STATE_SPLASH ) invUpdateSplash();
    if( invState == INV_STATE_TITLE ) invUpdateTitle();
    if( invState == INV_STATE_GAMEOVER ) invUpdateGameOver();

    gbRenderFrame();
}
