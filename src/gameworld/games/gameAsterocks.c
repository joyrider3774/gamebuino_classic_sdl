#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Asterocks (Yoda Zhang / "yodasvideoarcade", license: none specified -
// yodasvideoarcade.com/gamebuino.php). A real Asteroids clone: rotate and
// thrust a small ship around a wrapping playfield, shoot/split drifting
// rocks into smaller pieces, dodge (or shoot) a UFO that spawns once the
// score climbs and shoots back, survive on 3 lives across escalating
// levels, with a periodic bonus life every 10000 points. One of 5
// "yoda-*" games sharing this author's own file-split convention
// (asterocks.ino/standard.ino/specific.ino/nonstandard.ino/images.ino/
// sounds.ino) - only this one game (asterocks) was read/ported here.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment). `gb.display.cursorX =`/
// `cursorY =` ported unchanged (plain globals here too). `gb.display.
// print(...)` split into `gbPrintString()`/`gbPrintNumber()` (no
// overloading in this dialect). `random(N)` became `arand(N)` (this
// dialect's own established RNG helper); `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op. `gb.battery.show = false;`
// was dropped outright, matching gamePong.c's own precedent (purely
// cosmetic on real hardware). Upstream's C++ `and`/`or` alternative
// operators became `&&`/`||`. Real `byte` fields (upstream used `byte`
// throughout for lives/gamelevel/most speed and type fields) became
// plain `int` - unlike `uint8_t`/`int16_t`/etc, `byte` itself isn't one
// of avrCompat.h's own aliased AVR fixed-width types, but the exact same
// "every AVR fixed-width int type costs range/packing but nothing else
// once flattened to Vircon32's own single 32-bit int" reasoning documented
// there applies unchanged.
//
// Every global from this game uses an `aster`-prefixed name (checked
// against every other `src/games/*.c` file first - not already used).
//
// ---- Bitmaps - restored as real gbDrawBitmap() calls, not placeholders ----
// Every real PROGMEM bitmap byte in images.ino was converted from
// Arduino's `B00000000`-style binary literals to hex (`0x..`, this
// dialect has no binary-literal syntax) and copied verbatim, one plain
// int per original byte: `asterLogoBitmap` (the 64x26 splash/title logo),
// `asterShipFrames` (20 real 7x7 frames - indices 0-15 are the ship's own
// 16 rotation headings, indices 15-19 double up as a 5-frame death/
// explosion animation, exactly as upstream's own `playership[20][9]`
// table and `handledeath()`'s own `i = 19 - deadtimer/10` index formula
// use it - frame 15 is shared between "pointing up-left" and "first
// explosion frame", not a mistake, just upstream reusing one table for
// two purposes), `asterRockFrames` (12 real 10x10 frames - upstream's
// own 3-tier size system: types 0-3 = big, 4-7 = medium, 8-11 = small,
// selected by the exact same `asterocktype>3`/`>7` comparisons used
// throughout for both collision-box sizing and split/score-bonus logic),
// `asterUfoFrames` (2 real 7x5 frames - the two UFO types), and
// `asterBulletBitmap` (one real 3x3 frame, shared by both player and UFO
// shots). No bitmap here is preceded by a separate GRAY mask/fill layer
// anywhere in the real source (checked every drawBitmap call site in
// images.ino/specific.ino/standard.ino/nonstandard.ino directly) - the
// mask-bleed bug class found twice in gameFlappyBirdo.c does not apply to
// this game at all.
//
// Upstream keeps these as genuine 2D PROGMEM tables (`playership[20][9]`,
// `asterocks[12][22]`, `ufo[2][7]`) and indexes a whole row at a time
// (`playership[playershiprotation]`) directly into `drawBitmap()`. This
// port keeps that exact same shape (`int[20][9] asterShipFrames`, etc.)
// rather than splitting into 20/12/2 separately-named flat arrays -
// confirmed against VIRCON32_C_DIALECT.md section 4 ("arrays decay to
// pointers as parameters, as usual") that indexing one row out of a 2D
// array and passing it to a function expecting `int*` is a supported,
// documented pattern in this dialect, not something that needed
// discovering by trial and error.
//
// ---- Sound ----
// `playsoundfx(fxno, channel)` upstream builds a small "FX Synth" preset
// each call via `gb.sound.command(...)` (waveform/instrument, volume
// slide, pitch/arpeggio slide) before `gb.sound.playNote(pitch, duration,
// channel)` - `asterPlaySoundFx(fx, channel)` is a real, faithful port of
// this using this shim's own `gbSoundCommand()`/`gbPlayNoteChannel()`
// primitives (a direct port of real Sound.h's own per-channel tracker
// commands - see gamebuinoShim.h's own Sound section). `asterSoundFx[10][8]`
// is a verbatim copy of upstream's own `soundfx[10][8]` table (all 10 rows/
// 8 columns checked against the real `sounds.ino` byte-for-byte), and every
// one of upstream's 11 real `playsoundfx(fx, channel)` call sites now
// passes the same real channel number upstream itself uses at that exact
// site.
//
// ---- The blocking gb.titleScreen() calls -> ASTER_STATE_SPLASH ----
// Upstream calls the real, blocking `gb.titleScreen(text, gamelogo)` at
// three real call sites: once in setup() (the boot splash), once from
// inside showtitle() when Button C is pressed (using caption "Yoda's",
// no leading spaces), and once from inside handleplayership() when Button
// C is pressed mid-game (using caption "    Yoda's", WITH leading spaces -
// a trivial upstream inconsistency between the two C-press call sites,
// preserved exactly via a boolean flag rather than "fixed" to match,
// since it's harmless either way). All three became one explicit
// ASTER_STATE_SPLASH state (matching gamePong.c's own worked "blocking
// loop -> explicit resumable state" pattern), dismissed by a genuine
// fresh `gbPressed(BTN_A)` (armed globally by `md_armInputAGate()`),
// always transitioning to ASTER_STATE_TITLE afterward - matching all
// three real call sites' own eventual real behavior (setup()'s own
// splash sets gamestatus="title" directly after the blocking call
// returns; showtitle()'s own C-press leaves gamestatus untouched, but it
// was already "title" going in; handleplayership()'s own C-press sets
// gamestatus="title" directly too). Real Display::titleScreen()'s own
// internal layout isn't available anywhere in this game's own .ino
// sources (it lives inside the real, closed Gamebuino library itself,
// only its two arguments - a caption string and a bitmap - are visible
// here), so this state's own screen is an approximation: the same logo
// bitmap at the same (10,13) position showtitle() itself already uses for
// the exact same asset, the real caption text, and an added "PRESS A"
// hint line (upstream's own real titleScreen() almost certainly shows
// some real dismiss-prompt of its own; this is the closest documented
// stand-in without inventing new layout from nothing).
//
// Upstream's own real gamestatus string state machine ("newgame"/
// "newlevel"/"newlife"/"running"/"title"/"gameover") became an int enum,
// dispatched in gameAsterocks_update() using the exact same relative
// order loop() itself checks them in (newgame, newlevel, newlife,
// running, title, gameover - splash appended last, its own position
// doesn't interact with anything below). This exact order matters and
// was preserved deliberately, not just for readability: loop() uses
// independent sequential `if`s, not an if/else-if chain, so on the real
// hardware a genuine same-tick CASCADE happens every time a new game/
// level/life starts - newgame() flips status to "newlevel", and since
// the very next `if (gamestatus=="newlevel")` check in the same loop()
// call re-reads the now-updated variable, newlevel() runs too, then
// newlife(), then the "running" block itself, ALL within the one same
// real gb.update() tick - so no blank/cleared frame is ever visible
// between pressing A on the title screen and seeing real gameplay.
// Every OTHER transition in the source (title->newgame, running->title,
// title->splash, gameover->title, splash->title) instead moves to a
// state whose own `if` check already ran earlier in loop()'s fixed order
// this same tick, so real hardware shows one genuinely blank/cleared
// frame at each of those specific transitions before the new state's own
// first real draw call happens the following tick. This port reproduces
// BOTH behaviors exactly, for real, simply by writing five independent
// sequential `if`s below in loop()'s own exact order (not an else-if
// chain) rather than the usual single-state-per-tick dispatch pattern
// gamePong.c/gameAgaruino.c use - a deliberate, load-bearing deviation
// from that usual pattern, not an oversight (dialect rule 4 only bans
// `switch`, it doesn't require every dispatcher to be mutually exclusive).
//
// Button C mid-game (pause-to-splash) is checked at the very TOP of
// asterUpdateRunning() and returns immediately, skipping the rest of that
// tick's gameplay logic - matching gamePong.c's own established
// approximation for exactly this "blocking call used as a pause gesture"
// shape (upstream's own version has the check at the very END of
// handleplayership() instead, with the real blocking call meaning the
// REST of that tick's other handler calls - shots/rocks/ufo/etc - still
// run to completion afterward on real hardware before the screen ever
// actually goes blocking; that exact partial-tick nuance is inherent to a
// genuinely-blocking call and can't be preserved frame-for-frame in a
// non-blocking port regardless of where the check is placed, so this
// port uses the same simple, already-proven "check first, return early"
// shape instead of inventing a new approximation).
//
// ---- gbRepeat(btn, 0) bug found and fixed in the shim itself ----
// Upstream's own ship thrust/hyperspace/rotate-left/rotate-right controls
// all call `gb.buttons.repeat(BTN_x, 0)`, clearly intending "true every
// single tick the button is held" (the standard idiom for a continuously-
// held action, and the only sensible reading for thrust/rotation in an
// Asteroids clone - real `Buttons::repeat()` treats period 0 the same as
// period 1: fire on every held frame, confirmed directly against the real
// `Buttons.cpp` source). This shim's own `gbRepeat(button, period)`
// (gamebuinoShim.c) used to return true only on the very FIRST tick a
// button was pressed whenever `period <= 0` - the opposite of a continuous
// repeat, and a real, if narrow, divergence from real hardware. Found via
// this exact file's own ship controls needing it; fixed centrally in
// gamebuinoShim.c (gbRepeat() now matches real hardware's own period<=1
// "fire every held frame" behavior exactly), so the call sites below use
// the real, literal `gbRepeat(BTN_x, 0)` upstream itself calls, not a
// worked-around substitute.
//
// ---- Boolean-arithmetic idiom -> asterB() helper ----
// Upstream leans heavily on C's classic "multiply a numeric formula by a
// boolean condition" idiom instead of a real if/else or ternary (e.g.
// `left = 8 + 16*(asterocktype[i]>3) + 8*(asterocktype[i]>7);`,
// `score = score + 200 + 800*(ufotype==2);`). This dialect has no
// ternary operator, and no other ported game in this codebase was found
// using a raw `bool` operand directly inside int arithmetic (`int *
// bool`) to confirm it type-checks here, so rather than risk a compile
// failure this port added a tiny local `asterB(bool)` helper (returns
// 1/0) and wrapped every such condition in it - a mechanical, minimal-
// diff, dialect-safe stand-in for the exact same formulas, not a
// behavior change.
//
// ---- Two real bugs found, and fixed (not preserved) - both are genuine
// memory-safety hazards, not the harmless logic/visibility quirks this
// project otherwise preserves by default (compare gameAgaruino.c's own
// header comment, which preserves a similar-looking but harmless
// upstream oddity) ----
// 1. `newlevel()`'s own `asterocksonscreen = 4 + gamelevel*2; if
//    (asterocksonscreen>64) asterocksonscreen=64;` clamps against 64, but
//    every parallel `asterock*[32]` array (position/speed/type) upstream
//    itself declares is only 32 elements long - so from gamelevel 14
//    onward (4+14*2=32) up through ~gamelevel 29, upstream's own real
//    source writes past the end of every one of those arrays, a genuine
//    buffer overflow that would corrupt real AVR SRAM too (upstream's
//    own bug, not introduced by this port). Fixed here by clamping
//    `asterRocksOnScreen` to `ASTER_MAX_ROCKS` (32, the real declared
//    array size) instead of upstream's own mismatched 64. On real
//    hardware this silently corrupts nearby global RAM; ported literally
//    to a shared-cartridge environment with many other games' own
//    globals living in the same flat address space, the exact same bug
//    could corrupt THEIR state too, not just this game's own - a real
//    cross-game safety concern distinct in kind from a cosmetic gameplay
//    oddity, and worth fixing rather than preserving.
// 2. `moveufo()` sets `ufotype=0` the instant the UFO drifts off the left/
//    right edge, then unconditionally draws `ufo[ufotype-1]` (i.e.
//    `ufo[-1]`) on that exact same tick, in upstream's own real source.
//    On real AVR hardware this reads one PROGMEM byte before the `ufo[]`
//    table in flash - a contained, read-only, merely-cosmetic glitch (one
//    frame of garbled UFO pixels). Ported literally, `asterUfoFrames[-1]`
//    would instead read whatever RAM word happens to sit immediately
//    before this array (most likely another live global, not a fixed
//    flash byte) and hand it to `gbDrawBitmap()` as a completely
//    unvalidated width/height - a real risk of a much larger out-of-
//    bounds read than upstream's own contained flash glitch, potentially
//    reading (and rendering "bytes" from) far outside this game's own
//    memory. Fixed by re-checking `asterUfoType != 0` immediately before
//    that specific draw call, skipping it for the one tick the UFO type
//    just became 0 - the UFO simply disappears a frame earlier than
//    upstream's own glitch-frame, with no other behavior change.
//
// ---- Minor, behavior-preserving cleanups (not upstream bugs) ----
// Upstream's own `i`/`u` loop-index variables are reused loosely as
// globals across many top-level functions (always reset to 0 immediately
// before use in every case) - made local to each function instead here,
// including dropping one genuinely vestigial `i=++i;` at the very end of
// handleplayership()'s own ship-visible block, which has no observable
// effect on real hardware either (every consumer of `i` resets it to 0
// before ever reading it again). `destroyed`/`left`/`right` (per-rock
// scratch globals inside handlerocks(), likewise always reset before
// each use) became locals inside that same loop body for the same
// reason. The `float x, y;` scratch pair handlerocks() uses for a rock's
// per-tick velocity offset are declared local `int` here instead - both
// operands are always exact integers in every real call (asterRockXSpeed/
// YSpeed are themselves plain ints), so upstream's own float storage was
// never anything but a superfluous type choice for this particular use.
//
// Every other real formula/constant is preserved exactly as upstream
// wrote it, including several odd-looking but genuine, harmless upstream
// quirks: `newlevel()`'s own initial rock speed uses `rockxadd[random(4)]`
// (only the first 4 of the 12-entry table) for X but `rockyadd[random(12)]`
// (the full table) for Y - an asymmetric initial-velocity distribution,
// not a typo worth normalizing; the position-wrap thresholds (664, 376,
// -56, -80, 384) are kept as upstream's own literal sub-pixel (`*8`
// fixed-point) tuning constants rather than re-derived from
// `LCDWIDTH*8`/`LCDHEIGHT*8` programmatically, since they don't actually
// match those exactly either (664 vs 84*8=672, 376 vs 48*8=384 - real,
// deliberately-tuned upstream slack, not rounding error).
//
// EEPROM PERSISTENCE ADDED, BEYOND REAL UPSTREAM - no `EEPROM.read()`/
// `EEPROM.write()` calls exist anywhere in this game's real source, so
// `asterHighScore` was originally genuine in-session-only state (reset
// every relaunch). Added directly on request once an audit found this
// game displays a real highscore that never survives a cartridge reboot:
// `gameAsterocks_init()` now loads it via `eeprom_read_word(0)`, with the
// same `==0xFFFF` fresh-EEPROM-cell reset check already established
// elsewhere in this project (see gameCrabator.c's/gameDescent.c's own
// identical check) rather than trusting a raw 65535 sentinel. Saved via
// `eeprom_write_word(0, asterHighScore)` at the exact point upstream's
// own `asterUpdateTitle()` already updates `asterHighScore` in memory -
// naturally a one-shot write per new high score, not a per-frame write,
// since the `>` guard becomes false again the instant the save happens.

#define ASTER_STATE_SPLASH   0
#define ASTER_STATE_TITLE    1
#define ASTER_STATE_NEWGAME  2
#define ASTER_STATE_NEWLEVEL 3
#define ASTER_STATE_NEWLIFE  4
#define ASTER_STATE_RUNNING  5
#define ASTER_STATE_GAMEOVER 6

#define ASTER_MAX_ROCKS 32

int asterGameStatus;
bool asterSplashShort; // true: splash caption "Yoda's" (from the title screen's own C-press); false: "    Yoda's" (boot / mid-game pause)

int asterScore;
int asterHighScore;
int asterLives;
int asterGameLevel;
int asterYeahTimer;
int asterDeadTimer;

int asterPlayerShipX;
int asterPlayerShipY;
int asterPlayerShipRotation;
int asterPlayerShipXSpeed;
int asterPlayerShipYSpeed;
int asterPlayerShipVisible;
int asterPlayerShipAppear;

int asterXAdd[16]= { 0, 4, 8, 8, 8, 8, 8, 4, 0, -4, -8, -8, -8, -8, -8, -4 };
int asterYAdd[16]= { -8, -8, -8, -4, 0, 4, 8, 8, 8, 8, 8, 4, 0, -4, -8, -8 };
int asterRockXAdd[12]= { 4, 4, -4, -4, 8, 8, 8, 8, -8, -8, -8, -8 };
int asterRockYAdd[12]= { -8, 8, -8, 8, -8, -4, 4, 8, 8, 4, -4, -8 };

int asterPlayerShotX[4];
int asterPlayerShotY[4];
int asterPlayerShotXSpeed[4];
int asterPlayerShotYSpeed[4];
int asterPlayerShotCounter[4];
int asterPlayerShots;

int asterBonusScore;
int asterSoundSpeed;
int asterSoundValue;
int asterSoundCounter;

int asterRockX[ASTER_MAX_ROCKS];
int asterRockY[ASTER_MAX_ROCKS];
int asterRockXSpeed[ASTER_MAX_ROCKS];
int asterRockYSpeed[ASTER_MAX_ROCKS];
int asterRockType[ASTER_MAX_ROCKS];
int asterRocksOnScreen;

int asterUfoX;
int asterUfoY;
int asterUfoType;
int asterUfoXr;
int asterUfoYr;
int asterUfoShotX;
int asterUfoShotY;
int asterUfoShotXr;
int asterUfoShotYr;

// -----------------------------------------------------------------------
//   Real bitmaps (images.ino, B-literals converted to hex) - see header
// -----------------------------------------------------------------------

int asterLogoBitmap[210]=
{
    64, 26,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x38, 0x3d, 0xf7, 0xdf, 0x7, 0x7, 0x77, 0x1e,
    0x7c, 0x7d, 0xf7, 0xdf, 0x8f, 0x8f, 0x77, 0x3e,
    0xfe, 0xfd, 0xf7, 0xdf, 0xdf, 0xdf, 0x77, 0x7e,
    0xfe, 0xfd, 0xf7, 0xdf, 0xdf, 0xdf, 0x77, 0x7e,
    0xee, 0xe0, 0xe7, 0x1d, 0xdd, 0xdc, 0x77, 0x70,
    0xee, 0xe0, 0xe7, 0x1d, 0xdd, 0xdc, 0x7e, 0x70,
    0xee, 0xe0, 0xe7, 0x9d, 0xdd, 0xdc, 0x7c, 0x70,
    0xfe, 0xf8, 0xe7, 0x9f, 0x9d, 0xdc, 0x78, 0x7c,
    0xfe, 0x7c, 0xe7, 0x9f, 0x1d, 0xdc, 0x7c, 0x3e,
    0xfe, 0x3e, 0xe7, 0x1f, 0x9d, 0xdc, 0x7e, 0x1f,
    0xee, 0xe, 0xe7, 0x1d, 0xdd, 0xdc, 0x77, 0x7,
    0xee, 0xe, 0xe7, 0x1d, 0xdd, 0xdc, 0x77, 0x7,
    0xee, 0xfe, 0xe7, 0xdd, 0xdf, 0xdf, 0x77, 0x7f,
    0xee, 0xfe, 0xe7, 0xdd, 0xdf, 0xdf, 0x77, 0x7f,
    0xee, 0xfc, 0xe7, 0xdd, 0xcf, 0x8f, 0x77, 0x7e,
    0xee, 0xf8, 0xe7, 0xdd, 0xc7, 0x7, 0x77, 0x7c,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xa6, 0xc6, 0x6a, 0xec, 0xe6, 0x6e, 0x66, 0xce,
    0xaa, 0xaa, 0x8a, 0x4a, 0x8a, 0xaa, 0x8a, 0xa8,
    0xaa, 0xae, 0xea, 0x4a, 0xca, 0xec, 0x8e, 0xac,
    0x4a, 0xaa, 0x2a, 0x4a, 0x8a, 0xaa, 0x8a, 0xa8,
    0x4e, 0xca, 0xe4, 0xec, 0xee, 0xaa, 0xea, 0xce,
};

int asterShipFrames[20][9]=
{
    { 7, 7, 0x10, 0x10, 0x28, 0x28, 0x44, 0x7c, 0x0 },
    { 7, 7, 0xc, 0x14, 0x24, 0x44, 0x68, 0x18, 0x0 },
    { 7, 7, 0x6, 0x1a, 0x64, 0x44, 0x28, 0x18, 0x0 },
    { 7, 7, 0x0, 0x1e, 0x62, 0x44, 0x28, 0x30, 0x0 },
    { 7, 7, 0x0, 0x60, 0x58, 0x46, 0x58, 0x60, 0x0 },
    { 7, 7, 0x0, 0x30, 0x28, 0x44, 0x62, 0x1e, 0x0 },
    { 7, 7, 0x0, 0x18, 0x28, 0x44, 0x64, 0x1a, 0x6 },
    { 7, 7, 0x0, 0x18, 0x68, 0x44, 0x24, 0x14, 0xc },
    { 7, 7, 0x0, 0x7c, 0x44, 0x28, 0x28, 0x10, 0x10 },
    { 7, 7, 0x0, 0x30, 0x2c, 0x44, 0x48, 0x50, 0x60 },
    { 7, 7, 0x0, 0x30, 0x28, 0x44, 0x4c, 0xb0, 0xc0 },
    { 7, 7, 0x0, 0x18, 0x28, 0x44, 0x8c, 0xf0, 0x0 },
    { 7, 7, 0x0, 0xc, 0x34, 0xc4, 0x34, 0xc, 0x0 },
    { 7, 7, 0x0, 0xf0, 0x8c, 0x44, 0x28, 0x18, 0x0 },
    { 7, 7, 0xc0, 0xb0, 0x4c, 0x44, 0x28, 0x30, 0x0 },
    { 7, 7, 0x60, 0x50, 0x48, 0x44, 0x2c, 0x30, 0x0 },
    { 7, 7, 0x0, 0x20, 0x28, 0x44, 0x4c, 0x38, 0x8 },
    { 7, 7, 0x0, 0x40, 0x4c, 0x82, 0x8, 0x48, 0x30 },
    { 7, 7, 0x40, 0x84, 0x2, 0x0, 0x8, 0x48, 0x20 },
    { 7, 7, 0x80, 0x2, 0x0, 0x0, 0x0, 0x4, 0x40 },
};

int asterRockFrames[12][22]=
{
    { 10, 10, 0x36, 0x0, 0x49, 0x0, 0x80, 0x80, 0x40, 0x40, 0x20, 0x40, 0x40, 0x40, 0x80, 0x40, 0x80, 0x80, 0x41, 0x0, 0x3e, 0x0 },
    { 10, 10, 0x11, 0x0, 0x2a, 0x80, 0x44, 0x40, 0x80, 0x80, 0x80, 0x40, 0x80, 0x40, 0x80, 0x40, 0x40, 0x80, 0x21, 0x0, 0x1e, 0x0 },
    { 10, 10, 0x1e, 0x0, 0x21, 0x0, 0x40, 0x80, 0x80, 0x40, 0x80, 0x40, 0x40, 0x40, 0x40, 0x40, 0x88, 0x80, 0x55, 0x0, 0x22, 0x0 },
    { 10, 10, 0x1f, 0x0, 0x20, 0x80, 0x40, 0x40, 0x80, 0x40, 0x80, 0x80, 0x81, 0x0, 0x80, 0x80, 0x40, 0x40, 0x24, 0x80, 0x1b, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0x1c, 0x0, 0x22, 0x0, 0x11, 0x0, 0x21, 0x0, 0x2a, 0x0, 0x14, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0x16, 0x0, 0x29, 0x0, 0x22, 0x0, 0x21, 0x0, 0x12, 0x0, 0xc, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0xc, 0x0, 0x12, 0x0, 0x21, 0x0, 0x11, 0x0, 0x25, 0x0, 0x1a, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0xc, 0x0, 0x12, 0x0, 0x21, 0x0, 0x22, 0x0, 0x29, 0x0, 0x16, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0xa, 0x0, 0x12, 0x0, 0xc, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x14, 0x0, 0x12, 0x0, 0xc, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc, 0x0, 0x12, 0x0, 0xa, 0x0, 0x4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 10, 10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc, 0x0, 0x12, 0x0, 0x14, 0x0, 0x8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
};

int asterUfoFrames[2][7]=
{
    { 7, 5, 0x10, 0x28, 0xfe, 0x44, 0x38 },
    { 7, 5, 0x20, 0xd8, 0x70, 0x0, 0x0 },
};

int asterBulletBitmap[5]= { 3, 3, 0x40, 0xe0, 0x40 };

// Real "FX Synth" preset table (sounds.ino), copied verbatim - see header.
int asterSoundFx[10][8]=
{
    { 0, 36, 57, 1, 1, 1, 5, 6 },   // 0 = shoot - channel 0
    { 1, 15, 57, 1, 1, 2, 7, 8 },   // 1 = hit big rock / player ship - channel 2
    { 1, 20, 57, 1, 1, 2, 7, 8 },   // 2 = hit medium rock - channel 2
    { 1, 25, 57, 1, 1, 2, 7, 8 },   // 3 = hit small rock / ufo - channel 2
    { 0, 0, 2, 1, 0, 0, 4, 5 },     // 4 = ufo sound - channel 3
    { 0, 58, 0, 0, 0, 0, 7, 8 },    // 5 = bonus life - channel 3
    { 0, 0, 0, 0, 2, 1, 7, 3 },     // 6 = background 1 - channel 1
    { 0, 2, 0, 0, 2, 1, 7, 3 },     // 7 = background 2 - channel 1
    { 1, 12, 58, 0, 0, 0, 7, 2 },   // 8 = thrust - channel 0
    { 0, 40, 57, 1, 1, 1, 6, 6 },   // 9 = ufoshot - channel 0
};

// -----------------------------------------------------------------------
//   Dialect-safety helper - see header comment ("Boolean-arithmetic idiom")
// -----------------------------------------------------------------------

int asterB( bool cond )
{
    if( cond ) return 1;
    return 0;
}

void asterPlaySoundFx( int fx, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, asterSoundFx[ fx ][ 6 ], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, asterSoundFx[ fx ][ 0 ], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, asterSoundFx[ fx ][ 5 ], -asterSoundFx[ fx ][ 4 ], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, asterSoundFx[ fx ][ 3 ], asterSoundFx[ fx ][ 2 ] - 58, channel );
    gbPlayNoteChannel( asterSoundFx[ fx ][ 1 ], asterSoundFx[ fx ][ 7 ], channel );
}

// -----------------------------------------------------------------------
//   State transitions (nonstandard.ino)
// -----------------------------------------------------------------------

void asterNewGame()
{
    asterScore = 0;
    asterLives = 3;
    asterGameLevel = 0;
    asterUfoType = 0;
    asterUfoShotX = -1;
    asterPlayerShipRotation = 0;
    asterBonusScore = 10000;
    asterGameStatus = ASTER_STATE_NEWLEVEL;
}

void asterNewLevel()
{
    asterRocksOnScreen = 4 + asterGameLevel * 2;
    // upstream clamped to 64 here; fixed to the real declared array size
    // (32) instead - see header comment, "Two real bugs found, and fixed".
    if( asterRocksOnScreen > ASTER_MAX_ROCKS ) asterRocksOnScreen = ASTER_MAX_ROCKS;

    int i;
    for( i = 0; i < asterRocksOnScreen; i++ )
    {
        int x = arand( 320 );
        if( x > 160 ) x = x + 448;
        int y = arand( 128 );
        asterRockX[ i ] = x;
        asterRockY[ i ] = y;
        asterRockType[ i ] = arand( 4 );
        // upstream's own asymmetric table use (X: only the first 4 of the
        // 12-entry table, Y: the full table) - preserved, see header.
        asterRockXSpeed[ i ] = asterRockXAdd[ arand( 4 ) ] / 4 + 8;
        asterRockYSpeed[ i ] = asterRockYAdd[ arand( 12 ) ] / 4 + 8;
    }

    asterPlayerShipVisible = 2;
    asterGameStatus = ASTER_STATE_NEWLIFE;
    asterSoundSpeed = 40;
    asterSoundValue = 0;
}

void asterNewLife()
{
    asterYeahTimer = 0;
    asterDeadTimer = -1;
    asterPlayerShipX = 320;
    asterPlayerShipY = 176;
    asterPlayerShipXSpeed = 0;
    asterPlayerShipYSpeed = 0;
    if( asterPlayerShipVisible == 1 ) // after dying
    {
        asterPlayerShipVisible = 0;
        asterPlayerShipRotation = 0;
    }
    else // after finishing a level
    {
        asterPlayerShipVisible = 1;
    }
    asterGameStatus = ASTER_STATE_RUNNING;
}

void asterShowScore()
{
    int i = 1;
    while( asterLives > i )
    {
        gbDrawBitmap( i * 6 - 6, 0, asterShipFrames[ 0 ] );
        i = i + 1;
    }

    gbCursorX = 40 - 2 * asterB( asterScore > 9 ) - 2 * asterB( asterScore > 99 ) - 2 * asterB( asterScore > 999 ) - 2 * asterB( asterScore > 9999 ) - 2 * asterB( asterScore > 99999 );
    gbCursorY = 0;
    gbPrintNumber( asterScore );

    gbCursorX = 76;
    gbPrintNumber( asterGameLevel + 1 );
}

void asterNextLevelCheck()
{
    if( asterYeahTimer > 0 )
    {
        asterYeahTimer = asterYeahTimer - 1;
        if( asterYeahTimer == 0 )
        {
            asterGameLevel = asterGameLevel + 1;
            asterGameStatus = ASTER_STATE_NEWLEVEL;
        }
    }
}

void asterHandleDeath()
{
    asterDeadTimer = asterDeadTimer - 1;
    if( asterDeadTimer % 5 == 0 ) asterPlaySoundFx( 1, 0 );

    int i = 19 - asterDeadTimer / 10;
    gbDrawBitmap( asterPlayerShipX / 8, asterPlayerShipY / 8, asterShipFrames[ i ] );

    if( asterDeadTimer == 0 )
    {
        asterDeadTimer = -1;
        asterLives = asterLives - 1;
        if( asterLives == 0 ) asterGameStatus = ASTER_STATE_GAMEOVER;
        else asterGameStatus = ASTER_STATE_NEWLIFE;
    }
}

// -----------------------------------------------------------------------
//   Running-state gameplay (specific.ino)
// -----------------------------------------------------------------------

void asterHandlePlayerShip()
{
    if( asterPlayerShipVisible == 1 )
    {
        if( asterDeadTimer == -1 )
        {
            asterPlayerShipX = asterPlayerShipX + asterPlayerShipXSpeed;
            asterPlayerShipY = asterPlayerShipY + asterPlayerShipYSpeed;

            gbDrawBitmap( asterPlayerShipX / 8, asterPlayerShipY / 8, asterShipFrames[ asterPlayerShipRotation ] );

            if( gbRepeat( BTN_B, 0 ) ) // thrust
            {
                asterPlaySoundFx( 8, 0 );
                asterPlayerShipXSpeed = asterPlayerShipXSpeed + asterXAdd[ asterPlayerShipRotation ] / 8;
                asterPlayerShipYSpeed = asterPlayerShipYSpeed + asterYAdd[ asterPlayerShipRotation ] / 8;
                if( asterPlayerShipXSpeed > 8 ) asterPlayerShipXSpeed = 8;
                if( asterPlayerShipXSpeed < -8 ) asterPlayerShipXSpeed = -8;
                if( asterPlayerShipYSpeed > 8 ) asterPlayerShipYSpeed = 8;
                if( asterPlayerShipYSpeed < -8 ) asterPlayerShipYSpeed = -8;
            }
            if( gbRepeat( BTN_DOWN, 0 ) ) // hyperspace
            {
                asterPlayerShipX = arand( 592 ) + 40;
                asterPlayerShipY = arand( 304 ) + 40;
                asterPlayerShipXSpeed = 0;
                asterPlayerShipYSpeed = 0;
            }
            if( gbRepeat( BTN_LEFT, 0 ) ) // rotate left
            {
                asterPlayerShipRotation--;
                if( asterPlayerShipRotation < 0 ) asterPlayerShipRotation = asterPlayerShipRotation + 16;
            }
            if( gbRepeat( BTN_RIGHT, 0 ) ) // rotate right
            {
                asterPlayerShipRotation++;
                asterPlayerShipRotation = asterPlayerShipRotation % 16;
            }
            if( gbPressed( BTN_A ) && asterPlayerShots < 4 ) // release shot
            {
                asterPlayerShotXSpeed[ asterPlayerShots ] = asterXAdd[ asterPlayerShipRotation ];
                asterPlayerShotYSpeed[ asterPlayerShots ] = asterYAdd[ asterPlayerShipRotation ];
                asterPlayerShotX[ asterPlayerShots ] = asterPlayerShipX + 16 + asterPlayerShotXSpeed[ asterPlayerShots ];
                asterPlayerShotY[ asterPlayerShots ] = asterPlayerShipY + 16 + asterPlayerShotYSpeed[ asterPlayerShots ];
                asterPlayerShotCounter[ asterPlayerShots ] = 0;
                asterPlayerShots = asterPlayerShots + 1;
                asterPlaySoundFx( 0, 0 );
            }
        }
        else // death
        {
            asterHandleDeath();
        }
    }

    // ship off screen -> appear at the opposite side
    if( asterPlayerShipX < -56 ) asterPlayerShipX = asterPlayerShipX + 664;
    if( asterPlayerShipX > 664 ) asterPlayerShipX = asterPlayerShipX - 664;
    if( asterPlayerShipY < -56 ) asterPlayerShipY = asterPlayerShipY + 376;
    if( asterPlayerShipY > 376 ) asterPlayerShipY = asterPlayerShipY - 376;

    if( asterPlayerShipAppear == 1 ) asterPlayerShipVisible = 1;
}

void asterHandlePlayerShots()
{
    int i = 0;
    while( i < asterPlayerShots )
    {
        asterPlayerShotX[ i ] = asterPlayerShotX[ i ] + asterPlayerShotXSpeed[ i ];
        asterPlayerShotY[ i ] = asterPlayerShotY[ i ] + asterPlayerShotYSpeed[ i ];
        if( asterPlayerShotX[ i ] < 0 ) asterPlayerShotX[ i ] = asterPlayerShotX[ i ] + 664;
        if( asterPlayerShotX[ i ] > 664 ) asterPlayerShotX[ i ] = asterPlayerShotX[ i ] - 664;
        if( asterPlayerShotY[ i ] < 0 ) asterPlayerShotY[ i ] = asterPlayerShotY[ i ] + 376;
        if( asterPlayerShotY[ i ] > 376 ) asterPlayerShotY[ i ] = asterPlayerShotY[ i ] - 376;
        asterPlayerShotCounter[ i ] = asterPlayerShotCounter[ i ] + 1;

        gbDrawBitmap( asterPlayerShotX[ i ] / 8, asterPlayerShotY[ i ] / 8, asterBulletBitmap );

        if( asterPlayerShotCounter[ i ] > 30 ) // remove shot (swap with last)
        {
            asterPlayerShotX[ i ] = asterPlayerShotX[ asterPlayerShots - 1 ];
            asterPlayerShotY[ i ] = asterPlayerShotY[ asterPlayerShots - 1 ];
            asterPlayerShotXSpeed[ i ] = asterPlayerShotXSpeed[ asterPlayerShots - 1 ];
            asterPlayerShotYSpeed[ i ] = asterPlayerShotYSpeed[ asterPlayerShots - 1 ];
            asterPlayerShotCounter[ i ] = asterPlayerShotCounter[ asterPlayerShots - 1 ];
            asterPlayerShots = asterPlayerShots - 1;
        }
        i = i + 1;
    }
}

void asterHandleRocks()
{
    asterPlayerShipAppear = 1;
    int i = 0;
    while( i < asterRocksOnScreen )
    {
        int vx = asterRockXSpeed[ i ] - 8;
        int vy = asterRockYSpeed[ i ] - 8;
        asterRockX[ i ] = asterRockX[ i ] + vx;
        asterRockY[ i ] = asterRockY[ i ] + vy;
        if( asterRockX[ i ] < -80 ) asterRockX[ i ] = 664;
        if( asterRockX[ i ] > 664 ) asterRockX[ i ] = -80;
        if( asterRockY[ i ] < -80 ) asterRockY[ i ] = 376;
        if( asterRockY[ i ] > 376 ) asterRockY[ i ] = -80;

        gbDrawBitmap( asterRockX[ i ] / 8, asterRockY[ i ] / 8, asterRockFrames[ asterRockType[ i ] ] );

        // collision offset depending on rock size (3-tier: big/medium/small)
        int left = 8 + 16 * asterB( asterRockType[ i ] > 3 ) + 8 * asterB( asterRockType[ i ] > 7 );
        int right = 72 - 16 * asterB( asterRockType[ i ] > 3 ) - 8 * asterB( asterRockType[ i ] > 7 );

        int destroyed = 0;

        // collision with player ship
        if( ( asterPlayerShipX + 8 < asterRockX[ i ] + right ) && ( asterPlayerShipX + 40 > asterRockX[ i ] + left ) && ( asterPlayerShipY + 8 < asterRockY[ i ] + right ) && ( asterPlayerShipY + 40 > asterRockY[ i ] + left ) && asterPlayerShipVisible == 1 && asterDeadTimer == -1 )
        {
            asterDeadTimer = 40;
            destroyed = 1;
        }

        // collision with ufo
        if( ( asterRockX[ i ] + left < asterUfoX + 32 + 16 * asterB( asterUfoType == 1 ) ) && ( asterRockX[ i ] + right > asterUfoX ) && ( asterRockY[ i ] + left < asterUfoY + 16 + 16 * asterB( asterUfoType == 1 ) ) && ( asterRockY[ i ] + right > asterUfoY ) && asterUfoType != 0 )
        {
            asterScore = asterScore + 200 + 800 * asterB( asterUfoType == 2 );
            destroyed = 1;
            asterUfoType = 0;
        }

        // collision with ufoshot
        if( ( asterUfoShotX < asterRockX[ i ] + right ) && ( asterUfoShotX + 16 > asterRockX[ i ] + left ) && ( asterUfoShotY < asterRockY[ i ] + right ) && ( asterUfoShotY + 16 > asterRockY[ i ] + left ) && asterUfoShotX > -1 )
        {
            destroyed = 1;
            asterUfoShotX = -1;
        }

        // collision with playershots
        int u = 0;
        while( u < asterPlayerShots )
        {
            if( ( asterPlayerShotX[ u ] < asterRockX[ i ] + right ) && ( asterPlayerShotX[ u ] + 16 > asterRockX[ i ] + left ) && ( asterPlayerShotY[ u ] < asterRockY[ i ] + right ) && ( asterPlayerShotY[ u ] + 16 > asterRockY[ i ] + left ) && asterPlayerShotCounter[ u ] < 50 )
            {
                destroyed = 1;
                asterPlayerShotCounter[ u ] = 50;
            }
            u = u + 1;
        }

        // split or remove rock
        if( destroyed == 1 )
        {
            asterSoundSpeed = asterSoundSpeed - 2 * asterB( asterSoundSpeed > 5 );
            asterPlaySoundFx( 1 + asterB( asterRockType[ i ] > 3 ) + asterB( asterRockType[ i ] > 7 ), 2 );
            asterScore = asterScore + 20 + 30 * asterB( asterRockType[ i ] > 3 ) + 50 * asterB( asterRockType[ i ] > 7 );

            if( asterRockType[ i ] < 8 ) // big or medium rock -> split
            {
                asterRockX[ asterRocksOnScreen ] = asterRockX[ i ] + 16;
                asterRockY[ asterRocksOnScreen ] = asterRockY[ i ] + 16;
                asterRockX[ i ] = asterRockX[ i ] + 16;
                asterRockY[ i ] = asterRockY[ i ] + 16;
                asterRockType[ asterRocksOnScreen ] = 4 + arand( 4 ) + 4 * asterB( asterRockType[ i ] >= 4 );
                asterRockType[ i ] = 4 + arand( 4 ) + 4 * asterB( asterRockType[ i ] >= 4 );
                asterRockXSpeed[ asterRocksOnScreen ] = asterRockXAdd[ arand( 12 ) ] / 4 + 8;
                asterRockYSpeed[ asterRocksOnScreen ] = asterRockYAdd[ arand( 12 ) ] / 4 + 8;
                asterRockXSpeed[ i ] = asterRockXAdd[ arand( 12 ) ] / 2 + 8;
                asterRockYSpeed[ i ] = asterRockYAdd[ arand( 12 ) ] / 2 + 8;
                if( asterRockXSpeed[ i ] == asterRockXSpeed[ asterRocksOnScreen ] && asterRockYSpeed[ i ] == asterRockYSpeed[ asterRocksOnScreen ] )
                {
                    asterRockXSpeed[ i ] = -asterRockXSpeed[ i ];
                    asterRockYSpeed[ i ] = -asterRockYSpeed[ i ];
                }
                if( asterRockType[ asterRocksOnScreen ] > 7 || arand( 2 ) == 1 )
                {
                    asterRockXSpeed[ asterRocksOnScreen ] = asterRockXAdd[ arand( 12 ) ] / 2 + 8;
                    asterRockYSpeed[ asterRocksOnScreen ] = asterRockYAdd[ arand( 12 ) ] / 2 + 8;
                }
                if( asterRockType[ i ] > 7 || arand( 2 ) == 1 )
                {
                    asterRockXSpeed[ i ] = asterRockXAdd[ arand( 12 ) ] / 2 + 8;
                    asterRockYSpeed[ i ] = asterRockYAdd[ arand( 12 ) ] / 2 + 8;
                }
                if( asterRocksOnScreen < ASTER_MAX_ROCKS ) asterRocksOnScreen = asterRocksOnScreen + 1;
            }
            else // small rock -> remove
            {
                asterRocksOnScreen = asterRocksOnScreen - 1;
                asterRockX[ i ] = asterRockX[ asterRocksOnScreen ];
                asterRockY[ i ] = asterRockY[ asterRocksOnScreen ];
                asterRockType[ i ] = asterRockType[ asterRocksOnScreen ];
                asterRockXSpeed[ i ] = asterRockXSpeed[ asterRocksOnScreen ];
                asterRockYSpeed[ i ] = asterRockYSpeed[ asterRocksOnScreen ];
                if( asterRocksOnScreen == 0 ) asterYeahTimer = 60; // all rocks removed -> next level
            }
        }

        // can the playership appear? (no rock in the inside square)
        if( ( asterRockX[ i ] + right > 184 && asterRockX[ i ] + left < 488 ) && ( asterRockY[ i ] + right > 72 && asterRockY[ i ] + left < 312 ) )
          asterPlayerShipAppear = 0;

        i = i + 1;
    }
}

void asterUfoAppears()
{
    if( asterRocksOnScreen < 8 && asterScore > 500 && asterUfoType == 0 && arand( 250 ) < 2 )
    {
        asterUfoType = 1 + asterB( arand( 4 + asterGameLevel ) > 2 ); // which ufo?
        asterUfoX = -56;
        asterUfoXr = 3;
        if( arand( 2 ) == 0 )
        {
            asterUfoX = 672;
            asterUfoXr = -3;
        }
        asterUfoY = arand( 320 ) + 32;
        asterUfoYr = ( arand( 3 ) - 1 ) * 3;
    }
}

void asterMoveUfo()
{
    if( asterUfoType != 0 )
    {
        asterUfoX = asterUfoX + asterUfoXr;
        asterUfoY = asterUfoY + asterUfoYr;
        if( asterUfoX % 5 == 0 ) asterPlaySoundFx( 4, 3 );
        if( asterUfoY < -40 ) asterUfoY = 384;
        if( asterUfoY > 384 ) asterUfoY = -40;
        if( arand( 50 ) < 2 ) asterUfoYr = ( arand( 3 ) - 1 ) * 3; // change direction
        if( asterUfoX < -56 || asterUfoX > 672 ) asterUfoType = 0;

        // re-checked before drawing - see header comment, "Two real bugs
        // found, and fixed" #2 (upstream's own ufo[-1] out-of-bounds read)
        if( asterUfoType != 0 )
          gbDrawBitmap( asterUfoX / 8, asterUfoY / 8, asterUfoFrames[ asterUfoType - 1 ] );

        // collision playership & ufo
        if( ( asterUfoX < asterPlayerShipX + 40 ) && ( asterUfoX + 32 + 16 * asterB( asterUfoType == 1 ) > asterPlayerShipX + 8 ) && ( asterUfoY < asterPlayerShipY + 40 ) && ( asterUfoY + 16 + 16 * asterB( asterUfoType == 1 ) > asterPlayerShipY + 8 ) && asterUfoType != 0 && asterPlayerShipVisible == 1 && asterDeadTimer == -1 )
        {
            asterUfoType = 0;
            asterDeadTimer = 40;
            asterPlaySoundFx( 1, 2 );
        }
    }
}

void asterPlayerShotUfoCollision()
{
    int u = 0;
    while( u < asterPlayerShots )
    {
        if( ( asterPlayerShotX[ u ] < asterUfoX + 32 + 16 * asterB( asterUfoType == 1 ) ) && ( asterPlayerShotX[ u ] + 16 > asterUfoX ) && ( asterPlayerShotY[ u ] < asterUfoY + 16 + 16 * asterB( asterUfoType == 1 ) ) && ( asterPlayerShotY[ u ] + 16 > asterUfoY ) && asterPlayerShotCounter[ u ] < 50 && asterUfoType != 0 )
        {
            asterPlaySoundFx( 3, 0 );
            asterScore = asterScore + 200 + 800 * asterB( asterUfoType == 2 );
            asterUfoType = 0;
            asterPlayerShotCounter[ u ] = 50;
        }
        u = u + 1;
    }
}

void asterUfoShotRelease()
{
    if( asterUfoType != 0 && asterUfoShotX == -1 && asterUfoX > 40 && asterUfoX < 608 )
    {
        asterPlaySoundFx( 9, 0 );
        asterUfoShotX = asterUfoX + 24;
        asterUfoShotY = asterUfoY + 16;
        asterUfoShotXr = ( arand( 3 ) - 1 ) * 8;
        asterUfoShotYr = ( arand( 3 ) - 1 ) * 8;
        if( asterUfoType == 2 ) // the "smart" ufo aims at the player
        {
            asterUfoShotXr = -8 + 16 * asterB( asterUfoX < asterPlayerShipX );
            asterUfoShotYr = -8 + 16 * asterB( asterUfoY < asterPlayerShipY );
        }
        if( asterUfoShotXr == 0 && asterUfoShotYr == 0 ) asterUfoShotXr = 8;
    }
}

void asterMoveUfoShot()
{
    if( asterUfoShotX != -1 )
    {
        asterUfoShotX = asterUfoShotX + asterUfoShotXr;
        asterUfoShotY = asterUfoShotY + asterUfoShotYr;
        gbDrawBitmap( asterUfoShotX / 8, asterUfoShotY / 8, asterBulletBitmap );
        if( asterUfoShotX < 0 || asterUfoShotX > 664 || asterUfoShotY < 0 || asterUfoShotY > 376 )
          asterUfoShotX = -1;

        // collision ufoshot & player
        if( ( asterUfoShotX < asterPlayerShipX + 40 ) && ( asterUfoShotX + 16 > asterPlayerShipX + 8 ) && ( asterUfoShotY < asterPlayerShipY + 40 ) && ( asterUfoShotY + 16 > asterPlayerShipY + 8 ) && asterUfoShotX > -1 && asterPlayerShipVisible == 1 && asterDeadTimer == -1 )
        {
            asterUfoShotX = -1;
            asterDeadTimer = 40;
            asterPlaySoundFx( 1, 2 );
        }
    }
}

void asterCheckBonusLife()
{
    if( asterScore >= asterBonusScore )
    {
        asterPlaySoundFx( 5, 3 );
        asterLives = asterLives + 1;
        asterBonusScore = asterBonusScore + 10000;
    }
}

void asterBackgroundSound()
{
    asterSoundCounter = asterSoundCounter + 1;
    if( asterSoundCounter > asterSoundSpeed )
    {
        asterSoundCounter = 0;
        asterSoundValue = ( asterSoundValue + 1 ) % 2;
        asterPlaySoundFx( asterSoundValue + 6, 1 );
    }
}

void asterUpdateRunning()
{
    // pause the game (mid-game Button C) -> splash screen. Checked first
    // and returns immediately - see header comment on this approximation.
    if( gbPressed( BTN_C ) )
    {
        asterSplashShort = false;
        asterGameStatus = ASTER_STATE_SPLASH;
        return;
    }

    asterHandlePlayerShip();
    asterHandlePlayerShots();
    asterHandleRocks();
    asterNextLevelCheck();
    asterUfoAppears();
    asterMoveUfo();
    asterPlayerShotUfoCollision();
    asterUfoShotRelease();
    asterMoveUfoShot();
    asterCheckBonusLife();
    asterShowScore();
    asterBackgroundSound();
}

// -----------------------------------------------------------------------
//   Title / splash / game over screens (standard.ino)
// -----------------------------------------------------------------------

void asterUpdateSplash()
{
    gbSetColor( 1 );
    gbCursorX = 20;
    gbCursorY = 2;
    if( asterSplashShort ) gbPrintString( "Yoda's" );
    else gbPrintString( "    Yoda's" );

    gbDrawBitmap( 10, 13, asterLogoBitmap );

    gbCursorX = 20;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      asterGameStatus = ASTER_STATE_TITLE;
}

void asterUpdateTitle()
{
    if( asterScore > asterHighScore )
    {
        asterHighScore = asterScore;
        eeprom_write_word( 0, asterHighScore );
    }

    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "  LAST         HIGH" );

    gbCursorX = 14 - 2 * asterB( asterScore > 9 ) - 2 * asterB( asterScore > 99 ) - 2 * asterB( asterScore > 999 );
    gbCursorY = 6;
    gbPrintNumber( asterScore );

    gbCursorX = 66 - 2 * asterB( asterHighScore > 9 ) - 2 * asterB( asterHighScore > 99 ) - 2 * asterB( asterHighScore > 999 );
    gbCursorY = 6;
    gbPrintNumber( asterHighScore );

    gbDrawBitmap( 10, 13, asterLogoBitmap );

    gbCursorX = 0;
    gbCursorY = 42;
    gbPrintString( " A: PLAY     C: QUIT" );

    if( gbPressed( BTN_A ) )
    {
        asterGameStatus = ASTER_STATE_NEWGAME;
        gbPlayOK();
    }
    if( gbPressed( BTN_C ) )
    {
        asterSplashShort = true;
        asterGameStatus = ASTER_STATE_SPLASH;
    }
}

void asterUpdateGameOver()
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
        asterGameStatus = ASTER_STATE_TITLE;
        gbPlayOK();
    }
}

// -----------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------

void gameAsterocks_init()
{
    gbBegin();
    gbSetFrameRate( 25 ); // upstream's own explicit gb.setFrameRate(25) - overrides the real 20fps default, unlike most games ported so far
    gbPickRandomSeed(); // no-op, see gamebuinoShim.h's own header comment
    asterGameStatus = ASTER_STATE_SPLASH;
    asterSplashShort = false;

    asterHighScore = eeprom_read_word( 0 );
    if( asterHighScore == 0xFFFF ) asterHighScore = 0;
}

void gameAsterocks_update()
{
    if( !gbUpdate() ) return;

    // Deliberately independent sequential ifs, in upstream's own exact
    // order, not an else-if chain - see this file's own header comment
    // ("Upstream's own real gamestatus string state machine...") for why
    // this specific shape is load-bearing, not a style choice.
    if( asterGameStatus == ASTER_STATE_NEWGAME )  asterNewGame();
    if( asterGameStatus == ASTER_STATE_NEWLEVEL ) asterNewLevel();
    if( asterGameStatus == ASTER_STATE_NEWLIFE )  asterNewLife();
    if( asterGameStatus == ASTER_STATE_RUNNING )  asterUpdateRunning();
    if( asterGameStatus == ASTER_STATE_TITLE )    asterUpdateTitle();
    if( asterGameStatus == ASTER_STATE_GAMEOVER ) asterUpdateGameOver();
    if( asterGameStatus == ASTER_STATE_SPLASH )   asterUpdateSplash();

    gbRenderFrame();
}
