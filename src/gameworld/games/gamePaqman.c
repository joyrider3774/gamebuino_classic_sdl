#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Paqman (Yoda Zhang / "yodasvideoarcade", license: none specified -
// yodasvideoarcade.com/gamebuino.php). A real Pac-Man clone: guide Paqman
// around a scrolling 32x33-tile maze eating dots and 4 power pills while 4
// ghosts hunt him using a genuine real "Scatter"/"Chase" mode timer and
// per-ghost individual targeting AI (a home-corner ghost that follows
// Paqman directly, one that leads 4 tiles ahead of him, one that uses a
// doubled-vector chase off ghost 1's own position, and one that flips
// between chasing and fleeing to its own corner based on distance) -
// eating a power pill lets Paqman eat frightened ghosts for a doubling
// bonus, sending them home as "eyes" to respawn; a bonus fruit appears
// twice per level once enough dots are eaten. The last of this author's
// five "yoda-*" games sharing one file-split convention (killrace.ino/
// lander.ino/invaders.ino/asterocks.ino/paqman.ino, each split further
// into standard.ino/specific.ino/nonstandard.ino/images.ino/sounds.ino
// tabs) - the other four are already ported in this project as
// gameKillrace.c/gameLander.c/gameInvaders.c/gameAsterocks.c; this file
// follows gameAsterocks.c's own established translation pattern directly
// (same state-machine shape, same boolean-arithmetic helper, same sound-fx
// table shape, same blocking-titleScreen-as-pause approximation).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `gb.display.cursorX =`/`cursorY =`
// ported unchanged (plain globals here too). `gb.display.print(...)` split
// into `gbPrintString()`/`gbPrintNumber()` (no overloading in this
// dialect). `random(N)` became `arand(N)` (this dialect's own established
// RNG helper); `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a
// documented no-op. `gb.battery.show = false;` was dropped outright,
// matching gamePong.c's own precedent (purely cosmetic on real hardware).
// Upstream's own `and`/`or` alternative operators became `&&`/`||`.
// Upstream's own `byte` fields (used throughout for lives/gamelevel/
// paqmanx/paqmany/most timers) became plain `int` - the same "every AVR
// fixed-width int type costs range/packing but nothing else once flattened
// to Vircon32's own single 32-bit int" reasoning already documented in
// gameAsterocks.c's own header comment applies unchanged. Upstream's own
// four globals `checkleft`/`checkright`/`checktop`/`checkbottom` are
// declared in paqman.ino but never referenced anywhere in any of the other
// five .ino tabs - genuinely vestigial, dropped outright (not ported as
// dead globals).
//
// Every global from this game uses a `paq`-prefixed name (checked against
// every other `src/games/*.c` file first - not already used; the game's
// own upstream `paqman*` variable names already started with "paq" so they
// keep their original spelling, just changed to a `paqmanX`-style
// dialect-appropriate case).
//
// ---- STATE MACHINE: upstream's own real `String gamestatus`, compared
// via `=="newgame"`/`=="running"`/etc (a real content-comparison operator
// overload C++'s String class provides, not available in this classless
// dialect) - replaced with a plain `int paqGameStatus` enum
// (PAQ_STATE_NEWGAME/NEWLEVEL/NEWLIFE/RUNNING/TITLE/GAMEOVER, plus one more
// added below), compared via `==` exactly like upstream's own String
// comparisons read. ----
//
// Upstream's own loop() body is a flat run of SEPARATE
// `if (gamestatus==X) { ... }` statements, in this exact order: newgame,
// newlevel, newlife, running, title, gameover - the exact same order and
// shape gameAsterocks.c's own loop() uses (same author, same convention).
// That file's own header comment already documents in detail exactly why
// this specific shape is load-bearing rather than a style choice (a real,
// deliberate same-tick CASCADE the very first time a game/level/life
// starts, since each `if` re-reads the just-updated variable within the
// same real gb.update() tick - see gameAsterocks.c for the full reasoning,
// which applies here identically since it's the same author's own loop()
// convention verbatim). Reproduced here the same way: five independent
// sequential `if`s in gamePaqman_update(), in upstream's own exact order,
// not an else-if chain.
//
// ---- The blocking gb.titleScreen() calls -> PAQ_STATE_SPLASH ----
// Upstream calls the real, blocking `gb.titleScreen(text, gamelogo)` at
// three real call sites, with the exact same two-caption inconsistency
// gameAsterocks.c's own header comment already documents for that game:
// once in setup() (caption "    Yoda's", WITH leading spaces), once from
// inside showtitle() when Button C is pressed (caption "Yoda's", no
// leading spaces), and once from inside checkbuttons() (called every tick
// during "running") when Button C is pressed mid-game (caption
// "    Yoda's", WITH leading spaces again) - preserved exactly via a
// `paqSplashShort` boolean flag rather than "fixed" to be consistent,
// following gameAsterocks.c's own identical precedent (`asterSplashShort`)
// for the identical upstream quirk. All three became one explicit
// PAQ_STATE_SPLASH state, dismissed by a genuine fresh `gbPressed(BTN_A)`
// (armed globally by `md_armInputAGate()`), always transitioning to
// PAQ_STATE_TITLE afterward - matching all three real call sites' own
// eventual real behavior. Real Display::titleScreen()'s own internal
// layout isn't available anywhere in this game's own .ino sources either,
// so this state's own screen reuses the exact same approximation
// gameAsterocks.c's own ASTER_STATE_SPLASH already established (the same
// logo-bitmap position, real caption text, added "PRESS A" hint line).
//
// ---- Mid-game Button C (pause-to-splash) approximation, reused from
// gameAsterocks.c's own identical precedent ----
// Upstream's own real Button-C-to-titleScreen check lives inside
// checkbuttons(), itself called partway through the "running" state's own
// per-tick sequence (6th of 18 calls: handleanimation, drawmaze, drawfruit,
// drawpaqman, drawready, checkbuttons, movepaqman, paqmanfulltile,
// determineshape, nextlevelcheck, drawdotspills, ghostmodetimer,
// settargettiles, handlepowerpilltimer, moveghosts, checkghostcollission,
// drawghosts, showscore) - meaning on real hardware the tick Button C is
// pressed, the first 5 calls already ran with the old game state, THEN the
// blocking call takes over, and only after it returns (gamestatus now
// "title") do the remaining 12 calls of that same tick run once more
// before the tick ends. This exact partial-tick nuance is inherent to a
// genuinely-blocking call and can't be preserved frame-for-frame in a
// non-blocking port regardless of where the check is placed (see
// gameAsterocks.c's own header comment, "Button C mid-game..." for the
// identical reasoning) - so this port reuses that exact same, already-
// proven "check first, return early" shape instead of inventing a new
// approximation: `gbPressed(BTN_C)` is checked at the very TOP of
// paqUpdateRunning() and returns immediately, skipping the rest of that
// tick's gameplay logic entirely. `paqCheckButtons()` below keeps only the
// four directional-input assignments (upstream's own real
// `gb.buttons.repeat(BTN_x,0)` idiom - ported as the literal
// `gbRepeat(BTN_x, 0)` upstream itself calls, matching this shim's own
// now-fixed `gbRepeat()` real "every held frame" semantics, not a worked-
// around substitute - see this project's own CLAUDE.md).
//
// ---- Boolean-arithmetic idiom -> paqB() helper, reused verbatim from
// gameAsterocks.c's own identical `asterB()` ----
// Upstream leans on the same "multiply a numeric formula by a boolean
// condition" idiom as this author's other games (e.g.
// `cursorX=40-2*(score>9)-2*(score>99)-2*(score>999);` in showscore()/
// showtitle()). Wrapped in a tiny local `paqB(bool)` helper (returns 1/0),
// the same dialect-safe stand-in gameAsterocks.c already established and
// proved compiles - not a fresh risk taken here.
//
// ---- Scratch globals folded into locals (behavior-preserving cleanup,
// not an upstream bug) ----
// Upstream's own `checkbyte`/`checkbit`/`checkval`/`checkvalue`/`paqpos`/
// `cpos`/`u1`/`u2` are all declared as top-level globals in paqman.ino, but
// every real use resets them before reading, function-call-locally, with
// nothing ever relying on a stale value surviving between calls (checked
// directly - no cross-function dependency exists on any of them). Declared
// as ordinary local variables inside paqPaqmanFullTile()/
// paqDrawDotsPills()/paqMoveGhosts() instead here, the same kind of
// cleanup gameAsterocks.c's own header comment documents doing for its own
// game's equivalent loose scratch globals.
//
// ---- `distance[4]`/float array avoided - a dialect-safety call, not a
// behavior change ----
// Upstream's own `moveghosts()` fills a real `float distance[4]` (one
// candidate direction's distance-to-goal per array slot) then scans it for
// the minimum. No other ported game in this codebase was found declaring a
// plain `float[N]` array (only `int[N]` arrays and arrays-of-struct
// containing float *fields* are proven so far - e.g. gameAgaruino.c's own
// `AgarBall[N]`) to confirm that exact declaration shape compiles here, so
// rather than risk it, this port restructures the same algorithm into a
// single running-minimum comparison (`float a`/`int b`, updated inside the
// same 4-iteration loop) instead of populating and re-scanning a stored
// array - mechanically equivalent to upstream's own
// `if (distance[u] <= a) { a = distance[u]; b = u; }` scan, just without
// ever materializing the intermediate array. `sqrt()` itself ports
// unchanged (already proven elsewhere in this project - e.g.
// gameKillrace.c/gameLander.c - `math.h` is included once, globally, by
// main.c ahead of every game file).
//
// ---- A theoretical (never observed) out-of-bounds read, preserved
// exactly as upstream wrote it - not exercised in real play ----
// Upstream's own direction-choice loop initializes `int a = 99; int b =
// -1;` then only ever assigns `b` when a candidate direction's own
// distance is `<= a`. If, hypothetically, all 4 candidate directions were
// simultaneously invalid (`checkvalue==1`, forcing `distance[u]=100`, which
// never satisfies `<=99`), `b` would stay `-1` and
// `ghostxdir[i]=checkxdir[b]`/`checkydir[b]` would read one element before
// each table - the exact same class of theoretical OOB read
// gameAsterocks.c's own header comment found and fixed for its own game's
// `ufo[-1]` case. This one is NOT fixed, and is preserved exactly as
// upstream wrote it, for a concrete reason that case didn't share: this
// game's real maze (`paqMazeScreen`) has no ghost-reachable dead ends, and
// the "no 180-degree reversal" rule is itself always waived in the one
// place a ghost could otherwise get stuck (the ghost-house doorway, `ghosty
// != 15 || (ghostx != 12 && ghostx != 15)`) - so at least one of the 4
// candidate directions is always legal at every real reachable tile,
// confirmed by reading `paqMazeScreen`'s own real wall layout directly, not
// just assumed. `b == -1` is therefore dead code on this specific maze, not
// a live, reachable bug the way the asterocks UFO case was - flagged here
// per this project's own "document uncertainty, preserve real bugs by
// default" policy rather than silently fixed or silently ignored.
//
// ---- Sound ----
// `playsoundfx(fxno, channel)` upstream builds a small per-channel
// FX-synth preset each call (waveform/instrument, volume slide, pitch/
// arpeggio slide `gb.sound.command(...)` calls, from the real
// `soundfx[8][8]` table) before playing one real note on top -
// `paqPlaySoundFx(fx, channel)` is a real, faithful port of this using
// this shim's own `gbSoundCommand()`/`gbPlayNoteChannel()` primitives (a
// direct port of real Sound.h's own per-channel tracker commands, the
// same established primitive gameAsterocks.c/gameKillrace.c/
// gameInvaders.c already use for this same author's other games).
// `paqSoundFx[8][8]` is a verbatim copy of upstream's own `soundfx[8][8]`
// table, and all 7 real `playsoundfx()` call sites now pass the same real
// channel number upstream itself uses at that exact site (0 or 1).
// Upstream's own direct (non-playsoundfx) calls - `gb.sound.playTick()` in
// handledeath(), `gb.sound.playOK()` on the title/game-over A/B presses -
// became `gbPlayTick()`/`gbPlayOK()` directly, unchanged.
//
// ---- Bitmaps - restored as real gbDrawBitmap() calls, not placeholders
// ----
// Every real PROGMEM bitmap byte in images.ino (plus the two large
// `dotscreen[]`/`mazescreen[]` bit-grid tables declared directly in
// paqman.ino itself) was mechanically converted from Arduino's
// `B00000000`-style binary literals to hex (`0x..`, this dialect has no
// binary-literal syntax) via a small local Python conversion script (not
// checked in - a one-off text transform, not a reusable asset generator
// the way tools/gen_column_atlas.py is) and cross-checked byte-count
// against each table's own declared width/height (208/15/1089 real bitmap
// bytes + 2-byte header each, for gamelogo/readylogo/mazeimage
// respectively; 5 bytes/frame x 20/12/8 frames for paqman/ghost/fruit;
// 132 bytes each for dotscreen/mazescreen) before being copied in verbatim
// as `paqGameLogoBitmap`/`paqReadyLogoBitmap`/`paqMazeBitmap`/
// `paqmanFrames`/`paqGhostFrames`/`paqFruitFrames`/`paqDotScreen`/
// `paqMazeScreen` below - every value in every table was verified to match
// the real source's own byte-for-byte binary pattern, not re-derived or
// approximated. `paqman[20][7]`/`ghost[12][7]`/`fruit[8][7]` keep their
// real upstream 2D-table shape (`int[20][7] paqmanFrames`, etc, indexed a
// whole row at a time straight into `gbDrawBitmap()`) rather than splitting
// into separately-named flat arrays - the same confirmed-safe pattern
// gameAsterocks.c's own header comment already established for its own
// `playership[20][9]`/`asterocks[12][22]`/`ufo[2][7]` tables.
//
// `dotscreen[]`/`mazescreen[]` are themselves already genuinely 1D real
// upstream byte arrays (each a 32x33-tile grid packed 8 tiles/byte,
// addressed by hand via `>>`/`%`/bitwise-AND throughout, not a real 2D C
// array at all) - so unlike the maze-grid case this project's own porting
// guidance specifically calls out for review, no flattening decision was
// actually needed here: upstream itself never declared a 2D array for
// either table, only ported verbatim as `int paqDotScreen[132]`/
// `int paqMazeScreen[132]`, indexed with the exact same real hand-rolled
// bit arithmetic upstream itself uses (`pos>>3` for the byte, `7-(pos%8)`
// for the bit, matching Gamebuino's own real MSB-first bitmap convention -
// deliberately NOT the same bit order as this shim's own LSB-first
// framebuffer, since these two tables were never rendered as bitmaps in
// the first place, only read as a pure collision/dot-state bitfield).
//
// No bitmap draw call site anywhere in this game's real source
// (drawpaqman/drawghosts/drawfruit/drawmaze/drawready, checked directly)
// is preceded by a separate mask/fill layer of the same shape - the
// mask-bleed bug class found twice in gameFlappyBirdo.c/once in
// gameParachute.c does not apply to this game at all, the same conclusion
// gameAsterocks.c's own header comment reached for its own game.
//
// EEPROM PERSISTENCE ADDED, BEYOND REAL UPSTREAM - no `EEPROM.read()`/
// `EEPROM.write()` calls exist anywhere in this game's real source, so
// `paqHighScore` was originally genuine in-session-only state. Added
// directly on request once an audit found this game displays a real
// highscore that never survives a cartridge reboot: `gamePaqman_init()`
// now loads it via `eeprom_read_word(0)`, with the same `==0xFFFF`
// fresh-EEPROM-cell reset check already established elsewhere in this
// project (see gameCrabator.c's/gameDescent.c's own identical check)
// rather than trusting a raw 65535 sentinel. Saved via
// `eeprom_write_word(0, paqHighScore)` at the exact point upstream's own
// real highscore-tracking line already updates it in memory - a one-shot
// write per new high score, not a per-frame write.
//
// ---- Shim gap check: none found ----
// Every real primitive this port needed already exists in the current
// gamebuinoShim.h/.c: gbBegin/gbSetFrameRate/gbUpdate/gbPickRandomSeed,
// gbPressed/gbRepeat (both already fixed per this project's own prior
// porting-batch bugfixes - used here via the literal upstream
// `gbRepeat(BTN_x, 0)` call), gbSetColor/gbCursorX/gbCursorY/
// gbPrintString/gbPrintNumber/gbDrawBitmap/gbFillRect/gbDrawRect/
// gbDrawFastHLine/gbDrawFastVLine/gbDrawPixel, gbPlayNote/gbPlayTick/
// gbPlayOK, and `arand()`/`sqrt()` from avrCompat.h/math.h. Nothing had to
// be worked around locally.

#define PAQ_STATE_SPLASH   0
#define PAQ_STATE_TITLE    1
#define PAQ_STATE_NEWGAME  2
#define PAQ_STATE_NEWLEVEL 3
#define PAQ_STATE_NEWLIFE  4
#define PAQ_STATE_RUNNING  5
#define PAQ_STATE_GAMEOVER 6

#define PAQ_GHOSTMODE_CHASE   0
#define PAQ_GHOSTMODE_SCATTER 1

int paqGameStatus;
bool paqSplashShort; // true: splash caption "Yoda's" (from the title screen's own C-press); false: "    Yoda's" (boot / mid-game pause) - see header comment

int paqScore;
int paqHighScore;
int paqLives;
int paqGameLevel;
int paqYeahTimer;
int paqDeadTimer;

int paqmanX;
int paqmanY;
int paqmanXAdd;
int paqmanYAdd;
int paqmanXDir;
int paqmanYDir;
int paqmanGhostXDir;
int paqmanGhostYDir;
int paqmanWantXDir;
int paqmanWantYDir;
int paqmanShape;
int paqmanAnimation;
int paqDotsToEat;
int paqLevelDots[132]; // mutable per-level copy of paqDotScreen, cleared to it in paqNewLevel()

int paqPowerPillTimer;
int paqGhostScore;
int paqWaitTime;

int paqGhostX[4];
int paqGhostY[4];
int paqGhostXAdd[4];
int paqGhostYAdd[4];
int paqGhostXDir[4];
int paqGhostYDir[4];
int paqGhostXGoal[4];
int paqGhostYGoal[4];
int paqGhostStatus[4]; // 0 = normal, 1 = eaten ("eyes", heading home)
int paqGhostNoMove;

int paqPosAdd[4]     = { -1, 1, -32, 32 };
int paqCheckXDir[4]  = { -1, 1, 0, 0 };
int paqCheckYDir[4]  = { 0, 0, -1, 1 };
int paqCheckOppX[4]  = { 1, -1, 0, 0 };
int paqCheckOppY[4]  = { 0, 0, 1, -1 };

int paqGhostMode;
int paqGhostModeTime;
int paqScreenYOffset;
int paqAnimationFrame;
int paqAnimationCounter;
int paqFruitShape;
int paqFruitVisible;
int paqBackgroundTimer;
int paqBackgroundSound;

// -----------------------------------------------------------------------
//   Real bitmaps/bit-grids (images.ino + paqman.ino's own dotscreen[]/
//   mazescreen[] tables, B-literals converted to hex) - see header
// -----------------------------------------------------------------------

// 32x33-tile dot/pill layout (1 = dot/pill present) - the per-level
// starting state, copied into paqLevelDots at the start of every level.
int paqDotScreen[132] =
{
    0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0,
    0x7f, 0xf9, 0xff, 0xe0,
    0x42, 0x9, 0x4, 0x20,
    0x42, 0x9, 0x4, 0x20,
    0x42, 0x9, 0x4, 0x20,
    0xff, 0xff, 0xff, 0xfe,
    0x42, 0x40, 0x24, 0x20,
    0x42, 0x40, 0x24, 0x20,
    0x7e, 0x79, 0xe7, 0xe0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x4, 0x0,
    0x7f, 0xf9, 0xff, 0xe0,
    0x42, 0x9, 0x4, 0x20,
    0x42, 0x9, 0x4, 0x20,
    0x73, 0xf9, 0xfc, 0xe0,
    0x12, 0x40, 0x24, 0x80,
    0x12, 0x40, 0x24, 0x80,
    0x7e, 0x79, 0xe7, 0xe0,
    0x40, 0x9, 0x0, 0x20,
    0x40, 0x9, 0x0, 0x20,
    0x7f, 0xff, 0xff, 0xe0,
    0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0,
};

// 32x33-tile wall/collision mask (1 = wall/blocked tile)
int paqMazeScreen[132] =
{
    0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,
    0x80, 0x6, 0x0, 0x1f,
    0xbd, 0xf6, 0xfb, 0xdf,
    0xbd, 0xf6, 0xfb, 0xdf,
    0xbd, 0xf6, 0xfb, 0xdf,
    0x80, 0x0, 0x0, 0x1f,
    0xbd, 0xbf, 0xdb, 0xdf,
    0xbd, 0xbf, 0xdb, 0xdf,
    0x81, 0x86, 0x18, 0x1f,
    0xfd, 0xf6, 0xfb, 0xff,
    0xfd, 0xf6, 0xfb, 0xff,
    0xfd, 0x80, 0x1b, 0xff,
    0xfd, 0xb9, 0xdb, 0xff,
    0xfd, 0xb9, 0xdb, 0xfe,
    0x0, 0x30, 0xc0, 0x7,
    0xfd, 0xbf, 0xdb, 0xff,
    0xfd, 0xbf, 0xdb, 0xff,
    0xfd, 0x80, 0x1b, 0xff,
    0xfd, 0xbf, 0xdb, 0xff,
    0xfd, 0xbf, 0xdb, 0xff,
    0x80, 0x6, 0x0, 0x1f,
    0xbd, 0xf6, 0xfb, 0xdf,
    0xbd, 0xf6, 0xfb, 0xdf,
    0x8c, 0x0, 0x3, 0x1f,
    0xed, 0xbf, 0xdb, 0x7f,
    0xed, 0xbf, 0xdb, 0x7f,
    0x81, 0x86, 0x18, 0x1f,
    0xbf, 0xf6, 0xff, 0xdf,
    0xbf, 0xf6, 0xff, 0xdf,
    0x80, 0x0, 0x0, 0x1f,
    0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,
};

int paqGameLogoBitmap[210] =
{
    64, 26,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xff, 0x4, 0x3, 0xe1, 0x0, 0x40, 0x81, 0xf,
    0xff, 0x84, 0x7, 0xf1, 0x80, 0xc0, 0x81, 0x8f,
    0xff, 0x8e, 0xf, 0xf9, 0xc1, 0xc1, 0xc1, 0xcf,
    0xf7, 0xce, 0xf, 0xf9, 0xe3, 0xc1, 0xc1, 0xef,
    0xff, 0xdf, 0x1f, 0xfd, 0xf7, 0xc3, 0xe1, 0xff,
    0xff, 0x5f, 0x1f, 0xfd, 0xff, 0xc3, 0xe1, 0xff,
    0xfe, 0x9f, 0x1f, 0xf5, 0xff, 0x43, 0xe1, 0xfd,
    0xe1, 0xbf, 0x9f, 0x75, 0xff, 0x47, 0xf1, 0xfd,
    0xef, 0x3b, 0x9f, 0xf5, 0xff, 0x47, 0x71, 0xfd,
    0xe8, 0x7f, 0x5f, 0xf5, 0xff, 0x4f, 0xe9, 0xfd,
    0xe8, 0x7f, 0x5f, 0xf5, 0xff, 0x4f, 0xe9, 0xfd,
    0xe8, 0x7f, 0x5f, 0xf5, 0xff, 0x4f, 0xe9, 0xfd,
    0xe8, 0xff, 0xaf, 0xe9, 0xff, 0x5f, 0xf5, 0xfd,
    0xe8, 0xff, 0xaf, 0xd1, 0xff, 0x5f, 0xf5, 0xfd,
    0xc8, 0xff, 0x26, 0x29, 0xfe, 0x5f, 0xe5, 0xf9,
    0xf8, 0xff, 0xe3, 0xdd, 0xff, 0xdf, 0xfd, 0xff,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xa6, 0xc6, 0x6a, 0xec, 0xe6, 0x6e, 0x66, 0xce,
    0xaa, 0xaa, 0x8a, 0x4a, 0x8a, 0xaa, 0x8a, 0xa8,
    0xaa, 0xae, 0xea, 0x4a, 0xca, 0xec, 0x8e, 0xac,
    0x4a, 0xaa, 0x2a, 0x4a, 0x8a, 0xaa, 0x8a, 0xa8,
    0x4e, 0xca, 0xe4, 0xec, 0xee, 0xaa, 0xea, 0xce,
};

int paqReadyLogoBitmap[17] =
{
    22, 5,
    0xee, 0x4c, 0xa4,
    0xa8, 0xea, 0xac,
    0xce, 0xaa, 0xe8,
    0xa8, 0xea, 0x40,
    0xae, 0xac, 0x48,
};

int paqmanFrames[20][7] =
{
    { 5, 5, 0x70, 0xf8, 0xc0, 0xf8, 0x70 },
    { 5, 5, 0x70, 0xf8, 0xf8, 0xf8, 0x70 },
    { 5, 5, 0x70, 0xf8, 0xc0, 0xf8, 0x70 },
    { 5, 5, 0x70, 0xe0, 0xc0, 0xe0, 0x70 },
    { 5, 5, 0x70, 0xf8, 0xd8, 0xd8, 0x50 },
    { 5, 5, 0x70, 0xf8, 0xf8, 0xf8, 0x70 },
    { 5, 5, 0x70, 0xf8, 0xd8, 0xd8, 0x50 },
    { 5, 5, 0x70, 0xf8, 0xd8, 0x88, 0x0 },
    { 5, 5, 0x70, 0xf8, 0x18, 0xf8, 0x70 },
    { 5, 5, 0x70, 0xf8, 0xf8, 0xf8, 0x70 },
    { 5, 5, 0x70, 0xf8, 0x18, 0xf8, 0x70 },
    { 5, 5, 0x70, 0x38, 0x18, 0x38, 0x70 },
    { 5, 5, 0x50, 0xd8, 0xd8, 0xf8, 0x70 },
    { 5, 5, 0x70, 0xf8, 0xf8, 0xf8, 0x70 },
    { 5, 5, 0x50, 0xd8, 0xd8, 0xf8, 0x70 },
    { 5, 5, 0x0, 0x88, 0xd8, 0xf8, 0x70 },
    { 5, 5, 0x0, 0x0, 0x88, 0xf8, 0x70 },
    { 5, 5, 0x0, 0x0, 0x20, 0x70, 0x70 },
    { 5, 5, 0x0, 0x0, 0x0, 0x20, 0x20 },
    { 5, 5, 0x88, 0x50, 0x0, 0x50, 0x88 },
};

int paqGhostFrames[12][7] =
{
    { 5, 5, 0x70, 0xf8, 0x58, 0xf8, 0xa8 },
    { 5, 5, 0x70, 0xf8, 0x58, 0xf8, 0x50 },
    { 5, 5, 0x70, 0xf8, 0xd0, 0xf8, 0xa8 },
    { 5, 5, 0x70, 0xf8, 0xd0, 0xf8, 0x50 },
    { 5, 5, 0x70, 0xa8, 0xf8, 0xf8, 0xa8 },
    { 5, 5, 0x70, 0xa8, 0xf8, 0xf8, 0x50 },
    { 5, 5, 0x70, 0xf8, 0xa8, 0xf8, 0xa8 },
    { 5, 5, 0x70, 0xf8, 0xa8, 0xf8, 0x50 },
    { 5, 5, 0x70, 0xf8, 0xf8, 0xf8, 0xa8 },
    { 5, 5, 0x0, 0x0, 0x0, 0x0, 0x0 },
    { 5, 5, 0x0, 0x50, 0x50, 0x0, 0x0 },
    { 5, 5, 0x0, 0x0, 0x0, 0x0, 0x0 },
};

int paqFruitFrames[8][7] =
{
    { 5, 5, 0x38, 0x48, 0xd0, 0xf8, 0x58 },
    { 5, 5, 0x70, 0xb0, 0xe8, 0x70, 0x20 },
    { 5, 5, 0x18, 0x70, 0xf8, 0xd8, 0x70 },
    { 5, 5, 0x50, 0xf8, 0xf8, 0xe8, 0x50 },
    { 5, 5, 0xf8, 0x70, 0xb8, 0xe8, 0x70 },
    { 5, 5, 0xa8, 0xf8, 0xd8, 0x70, 0x20 },
    { 5, 5, 0x20, 0x50, 0xb8, 0xb8, 0x70 },
    { 5, 5, 0x70, 0xf8, 0x50, 0x50, 0x70 },
};

int paqMazeBitmap[1091] =
{
    84, 99,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0,
    0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10,
    0x3f, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff, 0xff, 0xff, 0xff, 0xc0,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0xff, 0x1, 0xff, 0xc0, 0x90, 0x3f, 0xf8, 0xf, 0xf0, 0x20,
    0x41, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x8, 0x20,
    0x41, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x8, 0x20,
    0x41, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x8, 0x20,
    0x41, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x8, 0x20,
    0x41, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x8, 0x20,
    0x40, 0xff, 0x1, 0xff, 0xc0, 0x60, 0x3f, 0xf8, 0xf, 0xf0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0xff, 0x1, 0x80, 0xff, 0xff, 0xf0, 0x18, 0xf, 0xf0, 0x20,
    0x41, 0x0, 0x82, 0x41, 0x0, 0x0, 0x8, 0x24, 0x10, 0x8, 0x20,
    0x41, 0x0, 0x82, 0x41, 0x0, 0x0, 0x8, 0x24, 0x10, 0x8, 0x20,
    0x40, 0xff, 0x2, 0x40, 0xff, 0xf, 0xf0, 0x24, 0xf, 0xf0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x3f, 0xff, 0x2, 0x3f, 0xc0, 0x90, 0x3f, 0xc4, 0xf, 0xff, 0xc0,
    0x80, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x0, 0x10,
    0x7f, 0xfe, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x17, 0xff, 0xe0,
    0x0, 0x2, 0x82, 0x3f, 0xc0, 0x60, 0x3f, 0xc4, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x41, 0xfc, 0x3, 0xf8, 0x24, 0x14, 0x0, 0x0,
    0xff, 0xfe, 0x82, 0x41, 0x5, 0xfa, 0x8, 0x24, 0x17, 0xff, 0xf0,
    0x0, 0x0, 0x82, 0x41, 0x7c, 0x3, 0xe8, 0x24, 0x10, 0x0, 0x0,
    0xff, 0xff, 0x1, 0x81, 0x40, 0x0, 0x28, 0x18, 0xf, 0xff, 0xf0,
    0x0, 0x0, 0x0, 0x1, 0x40, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0x40, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0x40, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0x40, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0x40, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0,
    0xff, 0xff, 0x1, 0x81, 0x40, 0x0, 0x28, 0x18, 0xf, 0xff, 0xf0,
    0x0, 0x0, 0x82, 0x41, 0x7f, 0xff, 0xe8, 0x24, 0x10, 0x0, 0x0,
    0xff, 0xfe, 0x82, 0x41, 0x0, 0x0, 0x8, 0x24, 0x17, 0xff, 0xf0,
    0x0, 0x2, 0x82, 0x41, 0xff, 0xff, 0xf8, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0x0, 0x0, 0x0, 0x24, 0x14, 0x0, 0x0,
    0x0, 0x2, 0x82, 0x40, 0xff, 0xff, 0xf0, 0x24, 0x14, 0x0, 0x0,
    0x7f, 0xfe, 0x82, 0x41, 0x0, 0x0, 0x8, 0x24, 0x17, 0xff, 0xe0,
    0x80, 0x0, 0x82, 0x41, 0x0, 0x0, 0x8, 0x24, 0x10, 0x0, 0x10,
    0x3f, 0xff, 0x1, 0x80, 0xff, 0xf, 0xf0, 0x18, 0xf, 0xff, 0xc0,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0xff, 0x1, 0xff, 0xc0, 0x90, 0x3f, 0xf8, 0xf, 0xf0, 0x20,
    0x41, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x8, 0x20,
    0x41, 0x0, 0x82, 0x0, 0x20, 0x90, 0x40, 0x4, 0x10, 0x8, 0x20,
    0x40, 0xfc, 0x81, 0xff, 0xc0, 0x60, 0x3f, 0xf8, 0x13, 0xf0, 0x20,
    0x40, 0x4, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x0, 0x20,
    0x40, 0x4, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x0, 0x20,
    0x40, 0x4, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x0, 0x20,
    0x40, 0x4, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x0, 0x20,
    0x40, 0x4, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x0, 0x20,
    0x3e, 0x4, 0x81, 0x80, 0xff, 0xff, 0xf0, 0x18, 0x12, 0x7, 0xc0,
    0x1, 0x4, 0x82, 0x41, 0x0, 0x0, 0x8, 0x24, 0x12, 0x8, 0x0,
    0x1, 0x4, 0x82, 0x41, 0x0, 0x0, 0x8, 0x24, 0x12, 0x8, 0x0,
    0x3e, 0x3, 0x2, 0x40, 0xff, 0xf, 0xf0, 0x24, 0xc, 0x7, 0xc0,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x2, 0x40, 0x0, 0x90, 0x0, 0x24, 0x0, 0x0, 0x20,
    0x40, 0xff, 0xfc, 0x3f, 0xc0, 0x90, 0x3f, 0xc3, 0xff, 0xf0, 0x20,
    0x41, 0x0, 0x0, 0x0, 0x20, 0x90, 0x40, 0x0, 0x0, 0x8, 0x20,
    0x41, 0x0, 0x0, 0x0, 0x20, 0x90, 0x40, 0x0, 0x0, 0x8, 0x20,
    0x40, 0xff, 0xff, 0xff, 0xc0, 0x60, 0x3f, 0xff, 0xff, 0xf0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20,
    0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0,
    0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

// Real "FX Synth" preset table (sounds.ino), copied verbatim - see header.
int paqSoundFx[8][8] =
{
    { 0, 30, 59, 1, 7, 0, 2, 5 },   // 0 = background sound 1 - channel 0
    { 0, 35, 57, 1, 7, 0, 2, 5 },   // 1 = background sound 2 - channel 0
    { 0, 6, 62, 1, 0, 0, 2, 5 },    // 2 = frightened background sound - channel 0
    { 1, 11, 66, 1, 0, 0, 7, 3 },   // 3 = eat dots 1 - channel 1
    { 1, 44, 50, 1, 0, 0, 7, 3 },   // 4 = eat dots 2 - channel 1
    { 0, 0, 2, 1, 0, 0, 7, 5 },     // 5 = eat ghost - channel 1
    { 0, 0, 108, 1, 0, 0, 7, 5 },   // 6 = eat fruit - channel 1
    { 0, 54, 44, 1, 0, 0, 7, 50 },  // 7 = dead - channel 1
};

// -----------------------------------------------------------------------
//   Dialect-safety helper - see header comment ("Boolean-arithmetic idiom")
// -----------------------------------------------------------------------

int paqB( bool cond )
{
    if( cond ) return 1;
    return 0;
}

void paqPlaySoundFx( int fx, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, paqSoundFx[ fx ][ 6 ], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, paqSoundFx[ fx ][ 0 ], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, paqSoundFx[ fx ][ 5 ], -paqSoundFx[ fx ][ 4 ], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, paqSoundFx[ fx ][ 3 ], paqSoundFx[ fx ][ 2 ] - 58, channel );
    gbPlayNoteChannel( paqSoundFx[ fx ][ 1 ], paqSoundFx[ fx ][ 7 ], channel );
}

// -----------------------------------------------------------------------
//   State transitions (nonstandard.ino)
// -----------------------------------------------------------------------

void paqNewGame()
{
    paqScore = 0;
    paqLives = 3;
    paqGameLevel = 0;
    paqGameStatus = PAQ_STATE_NEWLEVEL;
}

void paqNewLevel()
{
    int i;
    for( i = 0; i < 132; i++ ) paqLevelDots[ i ] = paqDotScreen[ i ];
    paqDotsToEat = 244;
    paqFruitShape = paqGameLevel;
    if( paqFruitShape > 7 ) paqFruitShape = 7;
    paqGameStatus = PAQ_STATE_NEWLIFE;
}

void paqNewLife()
{
    paqPowerPillTimer = 0;
    paqYeahTimer = 0;
    paqDeadTimer = -1;
    paqmanX = 13;
    paqmanY = 24;
    paqmanXAdd = 2;
    paqmanYAdd = 0;
    paqmanXDir = 1;
    paqmanYDir = 0;
    paqmanGhostXDir = 1;
    paqmanGhostYDir = 1;
    paqmanShape = 12;
    paqmanAnimation = 3;
    paqFruitVisible = 0;
    paqWaitTime = 30;

    paqGhostX[ 0 ] = 13; paqGhostY[ 0 ] = 12; paqGhostXAdd[ 0 ] = 1; paqGhostYAdd[ 0 ] = 0; paqGhostXDir[ 0 ] = 1;  paqGhostYDir[ 0 ] = 0;
    paqGhostX[ 1 ] = 12; paqGhostY[ 1 ] = 15; paqGhostXAdd[ 1 ] = 0; paqGhostYAdd[ 1 ] = 0; paqGhostXDir[ 1 ] = 1;  paqGhostYDir[ 1 ] = 0;
    paqGhostX[ 2 ] = 13; paqGhostY[ 2 ] = 15; paqGhostXAdd[ 2 ] = 2; paqGhostYAdd[ 2 ] = 0; paqGhostXDir[ 2 ] = 1;  paqGhostYDir[ 2 ] = 0;
    paqGhostX[ 3 ] = 15; paqGhostY[ 3 ] = 15; paqGhostXAdd[ 3 ] = 0; paqGhostYAdd[ 3 ] = 0; paqGhostXDir[ 3 ] = -1; paqGhostYDir[ 3 ] = 0;

    paqGhostMode = PAQ_GHOSTMODE_CHASE;
    paqGhostModeTime = 1;
    paqGameStatus = PAQ_STATE_RUNNING;
}

void paqShowScore()
{
    if( paqScreenYOffset == -5 || paqScreenYOffset == 56 )
    {
        int i = 0;
        if( paqScreenYOffset == 56 ) i = 43;
        if( paqLives > 1 ) gbDrawBitmap( 0, i, paqmanFrames[ 3 ] );
        if( paqLives > 2 ) gbDrawBitmap( 6, i, paqmanFrames[ 3 ] );
        gbCursorX = 40 - 2 * paqB( paqScore > 9 ) - 2 * paqB( paqScore > 99 ) - 2 * paqB( paqScore > 999 );
        gbCursorY = i;
        gbPrintNumber( paqScore );
        gbCursorX = 72;
        gbPrintNumber( paqGameLevel + 1 );
    }
}

void paqNextLevelCheck()
{
    if( paqYeahTimer > 0 )
    {
        paqYeahTimer = paqYeahTimer - 1;
        if( paqYeahTimer == 0 )
        {
            paqGameLevel = paqGameLevel + 1;
            paqGameStatus = PAQ_STATE_NEWLEVEL;
        }
    }
}

void paqHandleDeath()
{
    paqDeadTimer = paqDeadTimer - 1;
    if( paqDeadTimer % 5 == 0 ) gbPlayTick();

    int i = 7 - paqDeadTimer / 10;
    gbDrawBitmap( paqmanX * 3 + paqmanXAdd - 1, paqmanY * 3 + paqmanYAdd - paqScreenYOffset - 1, paqmanFrames[ 12 + i ] );

    if( paqDeadTimer == 0 )
    {
        paqDeadTimer = -1;
        paqLives = paqLives - 1;
        if( paqLives == 0 ) paqGameStatus = PAQ_STATE_GAMEOVER;
        else paqGameStatus = PAQ_STATE_NEWLIFE;
    }
}

// -----------------------------------------------------------------------
//   Running-state gameplay (specific.ino)
// -----------------------------------------------------------------------

void paqHandleAnimation()
{
    paqAnimationCounter = ( paqAnimationCounter + 1 ) % 4;
    if( paqAnimationCounter == 0 ) paqAnimationFrame = ( paqAnimationFrame + 1 ) % 2;
}

void paqDrawMaze()
{
    paqScreenYOffset = ( ( paqmanY - 7 ) * 3 ) + paqmanYAdd;
    if( paqScreenYOffset < -5 ) paqScreenYOffset = -5; // < 0
    if( paqScreenYOffset > 56 ) paqScreenYOffset = 56; // > 51

    if( paqYeahTimer == 0 )
    {
        gbDrawBitmap( 0, -paqScreenYOffset, paqMazeBitmap );
    }
    else
    {
        if( paqAnimationFrame == 0 ) gbDrawBitmap( 0, -paqScreenYOffset, paqMazeBitmap );
    }
}

void paqDrawFruit()
{
    if( paqFruitVisible == 0 && ( paqDotsToEat == 174 || paqDotsToEat == 74 ) ) paqFruitVisible = 400;

    if( paqFruitVisible > 0 )
    {
        gbDrawBitmap( 41, 53 - paqScreenYOffset, paqFruitFrames[ paqFruitShape ] );
        paqFruitVisible = paqFruitVisible - 1;
        if( ( paqmanX == 13 || paqmanX == 14 ) && paqmanY == 18 )
        {
            // fruit eaten
            paqFruitVisible = 0;
            paqScore = paqScore + 100 * paqGameLevel;
            paqPlaySoundFx( 6, 1 ); // fruit eaten sound
        }
    }
}

void paqDrawPaqman()
{
    if( paqDeadTimer == -1 )
      gbDrawBitmap( paqmanX * 3 + paqmanXAdd - 1, paqmanY * 3 + paqmanYAdd - paqScreenYOffset - 1, paqmanFrames[ paqmanShape + paqmanAnimation ] );
    else
      paqHandleDeath();
}

void paqDrawReady()
{
    if( paqWaitTime > 0 )
    {
        if( paqAnimationFrame == 0 ) gbDrawBitmap( 32, 2, paqReadyLogoBitmap );
        paqWaitTime = paqWaitTime - 1;
    }
}

// Direction input only - Button C (pause-to-splash) is checked separately
// at the top of paqUpdateRunning() - see header comment.
void paqCheckButtons()
{
    if( gbRepeat( BTN_UP, 0 ) )
    {
        paqmanWantYDir = -1;
        paqmanWantXDir = 0;
    }
    if( gbRepeat( BTN_DOWN, 0 ) )
    {
        paqmanWantYDir = 1;
        paqmanWantXDir = 0;
    }
    if( gbRepeat( BTN_LEFT, 0 ) )
    {
        paqmanWantXDir = -1;
        paqmanWantYDir = 0;
    }
    if( gbRepeat( BTN_RIGHT, 0 ) )
    {
        paqmanWantXDir = 1;
        paqmanWantYDir = 0;
    }
}

void paqMovePaqman()
{
    if( paqWaitTime == 0 && paqYeahTimer == 0 && paqDeadTimer == -1 )
    {
        paqmanXAdd = paqmanXAdd + paqmanXDir;
        paqmanYAdd = paqmanYAdd + paqmanYDir;
        if( paqmanXDir != 0 || paqmanYDir != 0 ) paqmanAnimation = ( paqmanAnimation + 1 ) % 4;
    }
    if( paqmanXAdd >= 3 || paqmanXAdd <= -3 )
    {
        paqmanXAdd = 0;
        paqmanX = paqmanX + paqmanXDir;
    }
    if( paqmanYAdd >= 3 || paqmanYAdd <= -3 )
    {
        paqmanYAdd = 0;
        paqmanY = paqmanY + paqmanYDir;
    }
    // use tunnel
    if( paqmanX <= 0 && paqmanXDir == -1 ) paqmanX = 27;
    if( paqmanX >= 27 && paqmanXDir == 1 ) paqmanX = 0;
}

void paqPaqmanFullTile()
{
    if( paqmanXAdd == 0 && paqmanYAdd == 0 )
    {
        // check if dot eaten
        int pos = paqmanX + paqmanY * 32;
        int cbyte = pos >> 3;
        int cbit = 7 - ( pos % 8 );
        int cval = ( paqLevelDots[ cbyte ] >> cbit ) & 1;
        if( cval == 1 )
        {
            paqDotsToEat = paqDotsToEat - 1;
            if( paqDotsToEat % 2 == 0 ) paqPlaySoundFx( 3, 1 );
            else paqPlaySoundFx( 4, 1 );
            paqLevelDots[ cbyte ] = paqLevelDots[ cbyte ] - ( 1 << cbit );

            if( ( paqmanX == 1 || paqmanX == 26 ) && ( paqmanY == 4 || paqmanY == 24 ) )
            {
                // power pill eaten
                paqScore = paqScore + 50;
                paqPowerPillTimer = 180 - paqGameLevel * 15;
                paqGhostScore = 200;
            }
            else
            {
                // dot eaten
                paqScore = paqScore + 10;
            }
            if( paqDotsToEat == 0 ) paqYeahTimer = 60;
        }

        // check next tile
        pos = ( paqmanX + paqmanWantXDir ) + ( paqmanY + paqmanWantYDir ) * 32;
        cbyte = pos >> 3;
        cbit = 7 - ( pos % 8 );
        cval = ( paqMazeScreen[ cbyte ] >> cbit ) & 1;
        if( cval == 0 )
        {
            paqmanXDir = paqmanWantXDir;
            paqmanYDir = paqmanWantYDir;
            paqmanGhostXDir = paqmanXDir;
            paqmanGhostYDir = paqmanYDir;
        }

        pos = ( paqmanX + paqmanXDir ) + ( paqmanY + paqmanYDir ) * 32;
        cbyte = pos >> 3;
        cbit = 7 - ( pos % 8 );
        cval = ( paqMazeScreen[ cbyte ] >> cbit ) & 1;
        if( cval != 0 )
        {
            paqmanXDir = 0;
            paqmanYDir = 0;
        }
    }
}

void paqDetermineShape()
{
    if( paqmanXDir == -1 ) paqmanShape = 8;
    if( paqmanXDir == 1 )  paqmanShape = 0;
    if( paqmanYDir == 1 )  paqmanShape = 4;
    if( paqmanYDir == -1 ) paqmanShape = 12;
}

void paqDrawDotsPills()
{
    int drawp = 64;
    int cbyte, cbit;
    for( cbyte = 8; cbyte < 124; cbyte++ )
    {
        for( cbit = 7; cbit >= 0; cbit-- )
        {
            int cval = ( paqLevelDots[ cbyte ] >> cbit ) & 1;
            if( cval == 1 )
            {
                int drawx = drawp % 32;
                int drawy = drawp >> 5;
                int drawyp = drawy * 3 - paqScreenYOffset;
                if( ( drawx == 1 || drawx == 26 ) && ( drawy == 4 || drawy == 24 ) && drawyp >= 0 && drawyp < 48 )
                {
                    if( paqAnimationFrame == 1 )
                    {
                        gbDrawFastHLine( drawx * 3, drawyp + 1, 3 );
                        gbDrawFastVLine( drawx * 3 + 1, drawyp, 3 );
                    }
                }
                else
                {
                    gbDrawPixel( drawx * 3 + 1, drawyp + 1 );
                }
            }
            drawp = drawp + 1;
        }
    }
}

void paqGhostModeTimer()
{
    paqGhostModeTime = paqGhostModeTime - 1;
    if( paqGhostModeTime <= 0 )
    {
        if( paqGhostMode == PAQ_GHOSTMODE_CHASE )
        {
            paqGhostMode = PAQ_GHOSTMODE_SCATTER;
            paqGhostModeTime = 300 - paqGameLevel * 60;
        }
        else
        {
            paqGhostMode = PAQ_GHOSTMODE_CHASE;
            paqGhostModeTime = 600 + paqGameLevel * 150;
        }
    }
}

void paqSetTargetTiles()
{
    if( paqPowerPillTimer > 0 ) // power pill active -> random goals
    {
        paqGhostXGoal[ 0 ] = arand( 28 );
        paqGhostYGoal[ 0 ] = arand( 33 );
        paqGhostXGoal[ 1 ] = arand( 28 );
        paqGhostYGoal[ 1 ] = arand( 33 );
        paqGhostXGoal[ 2 ] = arand( 28 );
        paqGhostYGoal[ 2 ] = arand( 33 );
        paqGhostXGoal[ 3 ] = arand( 28 );
        paqGhostYGoal[ 3 ] = arand( 33 );
    }
    else
    {
        if( paqGhostMode == PAQ_GHOSTMODE_SCATTER ) // scatter -> go to corners
        {
            paqGhostXGoal[ 0 ] = 0;  paqGhostYGoal[ 0 ] = 0;
            paqGhostXGoal[ 1 ] = 27; paqGhostYGoal[ 1 ] = 0;
            paqGhostXGoal[ 2 ] = 0;  paqGhostYGoal[ 2 ] = 32;
            paqGhostXGoal[ 3 ] = 27; paqGhostYGoal[ 3 ] = 32;
        }
        else // chase -> follow paqman
        {
            paqGhostXGoal[ 0 ] = paqmanX; // ghost 1: target = paqman
            paqGhostYGoal[ 0 ] = paqmanY;
            paqGhostXGoal[ 1 ] = paqmanX + paqmanGhostXDir * 4; // ghost 2: 4 tiles in front of paqman
            paqGhostYGoal[ 1 ] = paqmanY + paqmanGhostYDir * 4;
            paqGhostXGoal[ 2 ] = paqGhostX[ 0 ] + ( ( paqmanX + paqmanGhostXDir * 2 ) - paqGhostX[ 0 ] ) * 2; // ghost 3: double vector, ghost 1 -> paqman
            paqGhostYGoal[ 2 ] = paqGhostY[ 0 ] + ( ( paqmanY + paqmanGhostYDir * 2 ) - paqGhostY[ 0 ] ) * 2;
            if( ( paqmanX - paqGhostX[ 3 ] ) * ( paqmanX - paqGhostX[ 3 ] ) + ( paqmanY - paqGhostY[ 3 ] ) * ( paqmanY - paqGhostY[ 3 ] ) < 8 )
            {
                // ghost 4: paqman if distance > 8, else own corner
                paqGhostXGoal[ 3 ] = 27;
                paqGhostYGoal[ 3 ] = 32;
            }
            else
            {
                paqGhostXGoal[ 3 ] = paqmanX;
                paqGhostYGoal[ 3 ] = paqmanY;
            }
        }
    }

    int i;
    for( i = 0; i < 4; i++ )
    {
        if( paqGhostStatus[ i ] == 1 ) // ghost eyes -> return home
        {
            paqGhostXGoal[ i ] = 13;
            paqGhostYGoal[ i ] = 15;
        }
    }
}

void paqDrawGhosts()
{
    if( paqDeadTimer == -1 || paqDeadTimer > 60 )
    {
        int i;
        for( i = 0; i < 4; i++ )
        {
            int u = 0;
            if( paqGhostXDir[ i ] == 1 )  u = 2;
            if( paqGhostYDir[ i ] == -1 ) u = 4;
            if( paqGhostYDir[ i ] == 1 )  u = 6;
            if( paqPowerPillTimer > 0 )   u = 8;
            if( paqGhostStatus[ i ] == 1 ) u = 10;
            gbDrawBitmap( paqGhostX[ i ] * 3 + paqGhostXAdd[ i ] - 1, paqGhostY[ i ] * 3 + paqGhostYAdd[ i ] - paqScreenYOffset - 1, paqGhostFrames[ u + paqAnimationFrame ] );
        }
    }
}

void paqCheckGhostCollision()
{
    int i;
    for( i = 0; i < 4; i++ )
    {
        if( paqmanX * 3 + paqmanXAdd <= paqGhostX[ i ] * 3 + paqGhostXAdd[ i ] + 1
            && paqmanX * 3 + paqmanXAdd >= paqGhostX[ i ] * 3 + paqGhostXAdd[ i ] - 1
            && paqmanY * 3 + paqmanYAdd <= paqGhostY[ i ] * 3 + paqGhostYAdd[ i ] + 1
            && paqmanY * 3 + paqmanYAdd >= paqGhostY[ i ] * 3 + paqGhostYAdd[ i ] - 1
            && paqGhostStatus[ i ] == 0 && paqDeadTimer == -1 )
        {
            // ghost eaten?
            if( paqPowerPillTimer > 0 )
            {
                paqScore = paqScore + paqGhostScore;
                paqGhostScore = paqGhostScore * 2;
                paqGhostStatus[ i ] = 1;
                paqPlaySoundFx( 5, 1 ); // ghost eaten sound
            }
            else // paqman eaten
            {
                paqDeadTimer = 70;
                paqPlaySoundFx( 7, 1 );
            }
        }
    }
}

void paqHandlePowerPillTimer()
{
    paqBackgroundTimer = ( paqBackgroundTimer + 1 ) % 5;
    if( paqPowerPillTimer > 0 )
    {
        paqPowerPillTimer = paqPowerPillTimer - 1;
        if( paqBackgroundTimer == 0 && paqDeadTimer == -1 && paqWaitTime == 0 && paqYeahTimer == 0 ) paqPlaySoundFx( 2, 0 );
    }
    else
    {
        if( paqBackgroundTimer == 0 && paqDeadTimer == -1 && paqWaitTime == 0 && paqYeahTimer == 0 )
        {
            paqPlaySoundFx( paqBackgroundSound, 0 );
            paqBackgroundSound = ( paqBackgroundSound + 1 ) % 2;
        }
    }
}

void paqMoveGhosts()
{
    if( paqGhostNoMove > 0 )
    {
        paqGhostNoMove = paqGhostNoMove - 1;
        if( paqWaitTime == 0 && paqYeahTimer == 0 && paqDeadTimer == -1 )
        {
            int i;
            for( i = 0; i < 4; i++ )
            {
                if( paqGhostStatus[ i ] == 1 )
                {
                    paqGhostXAdd[ i ] = 0;
                    paqGhostYAdd[ i ] = 0;
                    paqGhostX[ i ] = paqGhostX[ i ] + paqGhostXDir[ i ];
                    paqGhostY[ i ] = paqGhostY[ i ] + paqGhostYDir[ i ];
                }
                else
                {
                    paqGhostXAdd[ i ] = paqGhostXAdd[ i ] + paqGhostXDir[ i ];
                    paqGhostYAdd[ i ] = paqGhostYAdd[ i ] + paqGhostYDir[ i ];
                }
                if( paqGhostXAdd[ i ] >= 3 || paqGhostXAdd[ i ] <= -3 )
                {
                    paqGhostXAdd[ i ] = 0;
                    paqGhostX[ i ] = paqGhostX[ i ] + paqGhostXDir[ i ];
                }
                if( paqGhostYAdd[ i ] >= 3 || paqGhostYAdd[ i ] <= -3 )
                {
                    paqGhostYAdd[ i ] = 0;
                    paqGhostY[ i ] = paqGhostY[ i ] + paqGhostYDir[ i ];
                }
                // use tunnel
                if( paqGhostX[ i ] <= 0 && paqGhostXDir[ i ] == -1 ) paqGhostX[ i ] = 27;
                if( paqGhostX[ i ] >= 27 && paqGhostXDir[ i ] == 1 ) paqGhostX[ i ] = 0;
            }

            // if ghost at full tile position ...
            for( i = 0; i < 4; i++ )
            {
                if( paqGhostXAdd[ i ] == 0 && paqGhostYAdd[ i ] == 0 )
                {
                    int pos = paqGhostX[ i ] + ( paqGhostY[ i ] * 32 );

                    // check the four possible directions - see header
                    // comment ("distance[4]/float array avoided") for why
                    // this is a running-minimum scan rather than a stored
                    // array, and ("A theoretical...out-of-bounds read") for
                    // why `b` staying -1 is preserved unguarded, matching
                    // upstream's own real `int a=99; int b=-1;` scan.
                    float a = 99.0;
                    int b = -1;
                    int u;
                    for( u = 0; u < 4; u++ )
                    {
                        int cpos = pos + paqPosAdd[ u ];
                        int cbyte = cpos >> 3;
                        int cbit = 7 - ( cpos % 8 );
                        int cval = ( paqMazeScreen[ cbyte ] >> cbit ) & 1;

                        if( ( paqGhostX[ i ] == 13 || paqGhostX[ i ] == 14 ) && paqGhostY[ i ] == 12 && paqGhostStatus[ i ] != 1 && u == 3 )
                          cval = 1;
                        if( paqGhostXDir[ i ] == paqCheckOppX[ u ] && paqGhostYDir[ i ] == paqCheckOppY[ u ] && ( paqGhostY[ i ] != 15 || ( paqGhostX[ i ] != 12 && paqGhostX[ i ] != 15 ) ) )
                          cval = 1;

                        // distance from target point
                        int dx = paqGhostX[ i ] + paqCheckXDir[ u ];
                        dx = paqGhostXGoal[ i ] - dx;
                        int dy = paqGhostY[ i ] + paqCheckYDir[ u ];
                        dy = paqGhostYGoal[ i ] - dy;
                        float dist = sqrt( (float)( dx * dx + dy * dy ) );
                        if( cval == 1 ) dist = 100.0;

                        if( dist <= a )
                        {
                            a = dist;
                            b = u;
                        }
                    }

                    // choose direction with shortest distance
                    paqGhostXDir[ i ] = paqCheckXDir[ b ];
                    paqGhostYDir[ i ] = paqCheckYDir[ b ];

                } // end if at full tile position

                // ghost eyes reached home?
                if( paqGhostX[ i ] == 13 && paqGhostY[ i ] == 15 && paqGhostStatus[ i ] == 1 )
                  paqGhostStatus[ i ] = 0;

            } // next ghost
        } // end of move ghosts
    }
    else
    {
        paqGhostNoMove = paqGameLevel * 2 + 2;
    } // end of ghost no move
}

void paqUpdateRunning()
{
    // pause the game (mid-game Button C) -> splash screen. Checked first
    // and returns immediately - see header comment on this approximation
    // (reused from gameAsterocks.c's own identical treatment).
    if( gbPressed( BTN_C ) )
    {
        paqSplashShort = false;
        paqGameStatus = PAQ_STATE_SPLASH;
        return;
    }

    paqHandleAnimation();   // handle animation
    paqDrawMaze();          // draw maze
    paqDrawFruit();         // draw fruit
    paqDrawPaqman();        // draw paqman
    paqDrawReady();         // draw ready
    paqCheckButtons();      // check buttons and set next direction for paqman
    paqMovePaqman();        // move paqman
    paqPaqmanFullTile();    // if paqman at full tile position ...
    paqDetermineShape();    // determine paqman shape
    paqNextLevelCheck();    // level finished
    paqDrawDotsPills();     // draw dots & pills
    paqGhostModeTimer();    // ghost mode timer
    paqSetTargetTiles();    // set target tiles
    paqHandlePowerPillTimer(); // power pill timer
    paqMoveGhosts();        // move ghosts
    paqCheckGhostCollision(); // check collision with ghosts
    paqDrawGhosts();        // draw ghosts
    paqShowScore();         // show lives, score, level
}

// -----------------------------------------------------------------------
//   Title / splash / game over screens (standard.ino)
// -----------------------------------------------------------------------

void paqUpdateSplash()
{
    gbSetColor( 1 );
    gbCursorX = 20;
    gbCursorY = 2;
    if( paqSplashShort ) gbPrintString( "Yoda's" );
    else gbPrintString( "    Yoda's" );

    gbDrawBitmap( 10, 13, paqGameLogoBitmap );

    gbCursorX = 20;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      paqGameStatus = PAQ_STATE_TITLE;
}

void paqUpdateTitle()
{
    if( paqScore > paqHighScore )
    {
        paqHighScore = paqScore;
        eeprom_write_word( 0, paqHighScore );
    }

    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "  LAST         HIGH" );

    gbCursorX = 14 - 2 * paqB( paqScore > 9 ) - 2 * paqB( paqScore > 99 ) - 2 * paqB( paqScore > 999 );
    gbCursorY = 6;
    gbPrintNumber( paqScore );

    gbCursorX = 66 - 2 * paqB( paqHighScore > 9 ) - 2 * paqB( paqHighScore > 99 ) - 2 * paqB( paqHighScore > 999 );
    gbCursorY = 6;
    gbPrintNumber( paqHighScore );

    gbDrawBitmap( 10, 13, paqGameLogoBitmap );

    gbCursorX = 0;
    gbCursorY = 42;
    gbPrintString( " A: PLAY     C: QUIT" );

    if( gbPressed( BTN_A ) )
    {
        paqGameStatus = PAQ_STATE_NEWGAME;
        gbPlayOK();
    }
    if( gbPressed( BTN_C ) )
    {
        paqSplashShort = true;
        paqGameStatus = PAQ_STATE_SPLASH;
    }
}

void paqUpdateGameOver()
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
        paqGameStatus = PAQ_STATE_TITLE;
        gbPlayOK();
    }
}

// -----------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------

void gamePaqman_init()
{
    gbBegin();
    gbSetFrameRate( 30 ); // upstream's own explicit gb.setFrameRate(30) - overrides the real 20fps default, matching gameAsterocks.c's own identical override
    gbPickRandomSeed(); // no-op, see gamebuinoShim.h's own header comment
    paqGameStatus = PAQ_STATE_SPLASH;
    paqSplashShort = false;

    paqHighScore = eeprom_read_word( 0 );
    if( paqHighScore == 0xFFFF ) paqHighScore = 0;
}

void gamePaqman_update()
{
    if( !gbUpdate() ) return;

    // Deliberately independent sequential ifs, in upstream's own exact
    // order, not an else-if chain - see this file's own header comment
    // ("STATE MACHINE"/the cascade this shape produces) for why this
    // specific shape is load-bearing, not a style choice.
    if( paqGameStatus == PAQ_STATE_NEWGAME )  paqNewGame();
    if( paqGameStatus == PAQ_STATE_NEWLEVEL ) paqNewLevel();
    if( paqGameStatus == PAQ_STATE_NEWLIFE )  paqNewLife();
    if( paqGameStatus == PAQ_STATE_RUNNING )  paqUpdateRunning();
    if( paqGameStatus == PAQ_STATE_TITLE )    paqUpdateTitle();
    if( paqGameStatus == PAQ_STATE_GAMEOVER ) paqUpdateGameOver();
    if( paqGameStatus == PAQ_STATE_SPLASH )   paqUpdateSplash();

    gbRenderFrame();
}
