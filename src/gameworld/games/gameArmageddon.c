#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Armageddon (wuuff, GPLv3 - real LICENSE file confirmed,
// github.com/wuuff/armageddon). A Worms/Missile-Command-style artillery
// defense game: move a shared crosshair with the D-pad and fire from
// launcher one (Button A) or launcher two (Button B) to detonate incoming
// enemy missiles above your 6 cities before they land. Clearing every
// enemy missile in a wave starts a "BONUS POINTS" lull screen (unused
// ammo and any still-standing cities are cashed in for points one at a
// time) before the next, harder stage begins. Losing every city (the two
// launcher sites don't count) ends the game and offers a 5-entry
// highscore table. Upstream ships as three real `.ino` tabs compiled as
// one translation unit (AGEDDON.ino/highscores.ino/sound.ino) - merged
// into this one file exactly the same way, no functional change from that
// merge alone. No IR-link/two-cart networking code exists anywhere in any
// of the three files (confirmed by reading all of them in full) - genuinely
// single-player, as the porting-priority audit already expected.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(N)` became `arand(N)`
// (this dialect's own established RNG helper - upstream never uses the
// ranged `random(min,max)` form anywhere in this game). `gb.pickRandomSeed()`
// became `gbPickRandomSeed()`, a documented no-op. `gb.battery.show = false`
// (set twice upstream: once in setup(), once again in checkMenu() right
// after re-showing the title screen) was dropped outright both times,
// matching every other ported game's identical treatment of that real-
// hardware-only cosmetic flag. Upstream's own `switch(mode)` in loop()
// became an if/else-if chain (this dialect's exact `switch` support is
// unproven - same caution gamePong.c/gameAgaruino.c/gameConduit.c already
// took).
//
// ARRAY DECLARATION ORDER: every array below uses this dialect's own
// `TYPE[N] name` order, not C's `TYPE name[N]` (see gameConduit.c's own
// tile arrays for the established precedent). Every real PROGMEM bitmap
// byte became one plain `int` cell.
//
// FLATTENED PER-MISSILE TRACKING ARRAYS: upstream keeps each missile's
// state in a genuine 2D array - `uint8_t pDests[MAX_PMISSILES][2]`,
// `float pMissiles[MAX_PMISSILES][3]`, `uint8_t pDetonations[MAX_PMISSILES][4]`,
// `float eMissiles[MAX_EMISSILES][4]`. Every confirmed real 2D array
// anywhere else in this project (gameConduit.c's `condMap`/`condTiles`,
// gameAsterocks.c's `asterShipFrames`/`asterSoundFx`, gameLander.c's
// `landLandscape`/`landLandscapeTiles`) is always `int`/byte-valued - no
// ported game anywhere in this codebase has yet proven a 2D *float* array
// actually compiles in this dialect. Rather than being the first to find
// out the hard way, every one of upstream's 2D per-missile tables was
// flattened here into parallel same-length 1D arrays instead (e.g.
// `pMissiles[i][0]/[1]/[2]` became `armaPMissileX[i]`/`armaPMissileY[i]`/
// `armaPMissileLauncher[i]`) - purely a storage-layout change, zero
// behavioral difference, and it sidesteps the open question entirely.
//
// BLOCKING `gb.titleScreen(armageddon)` -> EXPLICIT STATE: upstream calls
// this real, blocking library function twice - once in setup() (the very
// first thing the player sees) and again from checkMenu() any time Button
// C is pressed during any of the other four modes (a genuine "pause to
// the title/logo screen" gesture, since mode itself is left completely
// unchanged underneath the blocking call and simply resumes once dismissed).
// Both call sites became one shared `ARMA_STATE_TITLE`, with `armaPrevState`
// recording whichever state was interrupted so Button A resumes exactly
// there - the same "blocking widget -> explicit resumable state" treatment
// as gamePong.c's own PONG_STATE_TITLE, just with an explicit resume target
// instead of always going to one fixed state, since upstream's own version
// can be triggered mid-game and must return to *that* game, not the menu.
// The real `Gamebuino::titleScreen(logo)` library function itself draws the
// passed-in logo bitmap at a fixed (0, 12) screen anchor - confirmed
// directly against the real Gamebuino.cpp source during gameFlappyBirdo.c's/
// gameUfoRace.c's own ports of that exact same real function - reused
// unchanged here (`armaUpdateTitle()` below) since armageddon's own real
// title bitmap is, like those two, exactly 64x36: at y=12 its own bottom
// row lands exactly on LCDHEIGHT (48), zero clipping. Real `titleScreen()`
// draws no text of its own at all - the "PRESS A" prompt above the logo is
// this port's own added UI affordance (every other ported game restoring a
// real `titleScreen()` call needed the exact same addition).
//
// REAL BITMAP ART RESTORED: all 5 real upstream bitmaps - the 64x36
// `armageddon` title/logo bitmap and the four real 8x8 `city`/`deadcity`/
// `launcher`/`deadlauncher` sprites - were extracted byte-for-byte via a
// small Python script that parsed the real .ino source directly (stripping
// `/* */` block comments first, since upstream keeps an old, unused,
// commented-out alternate `city[]` bitmap directly above the real active
// one - the script was careful to only ever match the real active
// (uncommented) array, matching upstream's own actual shipped choice) and
// converted every `B00000000`-style binary literal to `0x` hex, verifying
// each array's own real body byte count against its own declared
// width/height header before trusting it (64x36 -> 288 body bytes; every
// 8x8 sprite -> 8 body bytes) - not hand-transcribed. Drawn directly via
// `gbDrawBitmap()` at upstream's own exact real coordinates (`armaUpdateTitle()`
// for the logo; `armaDrawCities()` for the 4 city/launcher states, at each
// city's own real `i*10+2, 40` position). Checked for the mask/fill-under-
// bitmap bug class found in gameFlappyBirdo.c/gameParachute.c: it does NOT
// apply here - every one of upstream's `drawBitmap()` call sites in
// `drawCities()` is a single, complete, self-contained opaque sprite with
// no separate mask/fill array or GRAY fill drawn underneath it anywhere in
// the real source.
//
// SOUND - APPROXIMATED, DOCUMENTED (an already out-of-scope area, not a
// shim gap being newly flagged): upstream's own `sound.ino` calls a real
// low-level `gb.sound.command(CMD_VOLUME, ...)/command(CMD_SLIDE, ...)/
// command(CMD_ARPEGGIO, ...)/command(CMD_INSTRUMENT, ...)` sequence before
// every single `playNote()` call - a generic tracker-style command/
// arpeggio/instrument API this shim has no equivalent for at all (only
// one-shot `gbPlayNote`/`gbPlayTick`/`gbPlayOK`/`gbPlayCancel` exist - a
// documented scope limit, not something newly missing). `armaPlaySound()`
// below keeps upstream's own real per-effect pitch/duration pair from its
// `sounds[][5]` table (columns 0/1 = wave/pitch... actually column 1 =
// pitch, column 2 = duration - the two values `gbPlayNote(pitch,duration)`
// can actually express) and calls `gbPlayNote()` directly with them,
// dropping only the wave-shape/arpeggio columns this shim has no primitive
// for - the same "approximate with the closest one-shot primitive, keep
// whatever real parameters do carry over" treatment gameUfoRace.c's own
// sound already established, just carrying over pitch+duration instead of
// falling back to a single representative `gbPlayOK()`-style tone, since
// gbPlayNote() lets this port keep more real fidelity than that.
//
// HIGHSCORE NAME ENTRY - DROPPED, DOCUMENTED (matching an exact existing
// precedent, not a new gap): upstream's own `gb.getDefaultName(tmp_name)` +
// `gb.keyboard(tmp_name, 11)` (a real on-screen text-entry widget) has no
// equivalent anywhere in this shim - confirmed against gamebuinoShim.h's
// full real API surface, and already found and documented identically by
// gameUfoRace.c's own highscore table. Per-name storage was dropped
// entirely rather than faked with a placeholder string: `armaHighscores[]`
// is a plain scores-only table, and `armaDrawHighscores()` shows only the
// five real scores, right-aligned at upstream's own real screen position,
// with nothing invented in the name column upstream used to occupy.
//
// EEPROM - REAL BYTE-LEVEL PERSISTENCE, WITH TWO REAL UPSTREAM BUGS FOUND
// AND FIXED RATHER THAN PRESERVED (both explained in full below - this
// project's usual default is to preserve real bugs, but both of these are
// genuine, unintentional defects with no sane literal port target, not
// quirky-but-harmless real behavior worth keeping faithfully):
//
//   1) A REAL 5-BYTES-INTO-A-4-BYTE-VALUE OVERFLOW. Upstream's own
//      `loadHighscores()`/`saveHighscore()` read/write each entry as
//      `ENTRY_SIZE` (15) raw EEPROM bytes: the first `NAME_SIZE` (10) are
//      the name, and the real code then walks a raw `uint8_t* addr =
//      (uint8_t*)&highscores[entry]; addr += offset-NAME_SIZE;` pointer
//      across the *remaining* `offset` values 10..14 - that's 5 byte
//      writes into a `uint32_t`, which is only ever 4 bytes wide. On real
//      hardware this silently corrupts the low byte of the *next* entry's
//      own score (or walks straight past the end of the array on the very
//      last entry) every single time a highscore is saved - a genuine,
//      unintentional buffer-overflow bug, not a deliberate design choice.
//      This shim has no raw address-of-array-element pointer trick to even
//      attempt reproducing this literally in the first place (no classes,
//      no confirmed arbitrary pointer arithmetic across a flattened global
//      array here), and doing so on purpose would just be reintroducing
//      real memory corruption for no gameplay benefit - so `armaSaveHighscore()`/
//      `armaLoadHighscores()` below use a clean, correct 4-bytes-per-entry
//      layout instead, still via `eeprom_read_byte()`/`eeprom_write_byte()`
//      one byte at a time (matching upstream's own real byte-level
//      `EEPROM.read()`/`EEPROM.write()` calls exactly, per this task's own
//      instructions on matching upstream's real byte-width - upstream never
//      calls anything wider than a single byte at a time, so `eeprom_read_dword()`/
//      `eeprom_write_dword()` were deliberately NOT used even though they'd
//      have been more convenient), using the shim's own established real
//      big-endian byte order (matching `eepromShim.c`'s own `eeprom_read_dword()`/
//      `eeprom_write_dword()` formula, `(b0<<24)|(b1<<16)|(b2<<8)|b3`, MSB
//      first) purely for in-codebase consistency - fixed rather than
//      preserved, unlike this project's usual default, since the
//      alternative was actively harmful, not just quirky.
//
//   2) A REAL "PERMANENTLY LOCKED ON A FRESH CARTRIDGE" BUG. Nowhere in
//      `highscores.ino` is there any code to detect or initialize a truly
//      fresh EEPROM (unlike, say, gameUfoRace.c's own `initHighscore()`,
//      which at least has an inert `==0` dead-code check for this exact
//      situation). This project's own eepromShim.c deliberately reads
//      fresh/unwritten cells as 0xFF each, matching real AVR's own factory-
//      erased state (per this project's own CLAUDE.md) - so on a genuinely
//      fresh card, every `armaHighscores[]` entry would decode straight
//      from raw 0xFF bytes to the sentinel value 4294967295, and
//      `armaIsHighscore(score)` (`score > armaHighscores[NUM-1]`) could
//      then never return true, since a real game score can never reach 4
//      billion - permanently locking out the entire highscore feature,
//      since `armaSaveHighscore()` is only ever called from behind that
//      same check. This reads like a genuine gap in the real upstream code
//      (most likely never actually observed by its own author, if it was
//      only ever tested against an already-used EEPROM chip) rather than
//      deliberate design worth preserving faithfully. `armaLoadHighscores()`
//      below specifically detects an all-0xFF-fresh table and resets it to
//      a clean all-zero table instead, so the feature actually works the
//      first time in this emulator - documented here rather than silently
//      patched in, per this task's own "document uncertainty" instruction.
//
// TWO SMALLER REAL QUIRKS FOUND, HANDLED DIFFERENTLY ON PURPOSE:
//
//   - `stepPregame()`'s own `stage = 255; ...; nextStage();` is a
//     deliberate real trick (upstream's own comment literally says "Reset
//     to stage 0"): since `stage` is `uint8_t` on real hardware, incrementing
//     255 wraps around to 0 inside `nextStage()`'s own `stage++`. This
//     shim's `armaStage` is a plain unbounded `int` (matching every other
//     ported game's own `byte`/`uint8_t` -> `int` treatment), which would
//     NOT wrap the same way - `armaStage = -1;` is used instead right
//     before calling `armaNextStage()`, producing the exact same real
//     result (a fresh stage 0) without needing to fake an 8-bit overflow
//     this shim's own wider `int` doesn't have.
//   - `drawLull()`'s own `for( uint8_t i; i < 8; i++ )` loop never
//     initializes `i` at all - a genuine uninitialized-variable bug on real
//     hardware (undefined behavior in C, though AVR-GCC's own real codegen
//     most likely just happened to leave it reading 0 from a reused
//     register/stack slot in practice). Unlike the two EEPROM bugs above,
//     relying on genuinely uninitialized memory has no well-defined
//     behavior to even preserve in a *different* runtime - `armaUpdatePregame()`'s
//     ported equivalent (the cityCount tally inside `armaDrawLull()`)
//     explicitly initializes its own loop counter to 0, the overwhelmingly
//     likely real intended behavior, rather than reading whatever garbage
//     this shim's own runtime happens to leave behind.
//
// `#define MAX_CHANCE 50` is real upstream dead code - defined once, never
// referenced anywhere in any of the three files (the only place that could
// have used it, a `stage`-scaled `echance` formula, is itself commented out
// in `tryLaunchEnemy()`, leaving `echance` permanently fixed at its real
// initial value of 1 for the whole game) - dropped outright here rather
// than ported as dead weight, matching gameConduit.c's own identical
// treatment of its own dead `count` debug variable. `echance`'s own
// permanently-fixed value of 1 is, however, real load-bearing (if oddly
// undynamic) behavior and IS preserved exactly as upstream shipped it.
//
// Explicit `(int)` casts were added at every float-to-int drawing-call
// boundary this dialect requires but real C++ did implicitly (missile
// positions are tracked as `float` throughout, exactly like upstream, but
// `gbDrawLine()`/`gbDrawPixel()`/`gbDrawCircle()` all take `int` - the same
// treatment gameCatcher.c's own sprite-position casts already established).
//
// SHIM GAPS: none found beyond the two already-documented, deliberately
// out-of-scope areas above (the sound command/arpeggio/instrument API, and
// on-screen keyboard text entry) - every other primitive this port needed
// (`gbDrawBitmap`, `gbRepeat`, `gbDrawChar`'s underlying `gbPrintString`,
// `eeprom_read_byte`/`eeprom_write_byte`, `atan2`/`cos`/`sin`/`sqrt` via the
// globally-included `math.h`) already existed and worked as documented.

#define ARMA_TARGET_SPEED 3

#define ARMA_LAUNCHER_ONE 1
#define ARMA_LAUNCHER_TWO 2
#define ARMA_MAX_PMISSILES 10
#define ARMA_PSPEED 4
#define ARMA_PRADIUS 7
#define ARMA_EXPAND 0
#define ARMA_SHRINK 1

#define ARMA_MAX_EMISSILES 10

#define ARMA_NUM_HIGHSCORES 5

enum ArmaState
{
    ARMA_STATE_TITLE = 0,
    ARMA_STATE_PREGAME = 1,
    ARMA_STATE_GAME = 2,
    ARMA_STATE_LULL = 3,
    ARMA_STATE_DEAD = 4,
    ARMA_STATE_POSTDEAD = 5
};

int armaState;
int armaPrevState; // resume target once ARMA_STATE_TITLE (Button C pause, or the initial boot title) is dismissed

int armaCounter;
int armaFlash;
int armaStage;
int armaScore;

int armaLullMissiles;
int armaLullCities[8];
int armaCities[8]; // whether each city/launcher (index 2 and 5) is alive

int armaTargetX;
int armaTargetY;
int armaPammo[2];

// Player missiles - flattened from upstream's own 2D pDests/pMissiles/
// pDetonations arrays (see this file's own header comment on why).
int armaPDestX[ARMA_MAX_PMISSILES];
int armaPDestY[ARMA_MAX_PMISSILES];
float armaPMissileX[ARMA_MAX_PMISSILES];
float armaPMissileY[ARMA_MAX_PMISSILES];
int armaPMissileLauncher[ARMA_MAX_PMISSILES];
int armaPDetX[ARMA_MAX_PMISSILES];
int armaPDetY[ARMA_MAX_PMISSILES];
int armaPDetR[ARMA_MAX_PMISSILES];
int armaPDetState[ARMA_MAX_PMISSILES];

int armaEtotal;
int armaEchance;
float armaEspeed;
int armaEDest[ARMA_MAX_EMISSILES];
float armaEMissileX0[ARMA_MAX_EMISSILES]; // fixed spawn point (line start)
float armaEMissileY0[ARMA_MAX_EMISSILES];
float armaEMissileX[ARMA_MAX_EMISSILES]; // current falling position (line end)
float armaEMissileY[ARMA_MAX_EMISSILES];

int armaHighscores[ARMA_NUM_HIGHSCORES];

// Wave/pitch/duration/arpeggio-step-duration/arpeggio-step-size, copied
// verbatim from upstream's own real sound.ino `sounds[][5]` table - only
// columns 1 (pitch) and 2 (duration) have a real gbPlayNote() equivalent
// here (see this file's own header comment on the dropped command/
// arpeggio/instrument API).
int armaSounds[6][2] =
{
    {20, 5}, // player launch
    {25, 5}, // enemy launch
    {10, 5}, // detonating enemy missile
    {10, 2}, // score pips
    {2, 10}, // a city dies
    {20, 14}, // lose
};

#define ARMA_SOUND_PLAUNCH 0
#define ARMA_SOUND_ELAUNCH 1
#define ARMA_SOUND_DETONATE 2
#define ARMA_SOUND_SCORE 3
#define ARMA_SOUND_DEAD 4
#define ARMA_SOUND_LOSE 5

// -----------------------------------------------------------------------------
// Real upstream bitmaps - extracted byte-for-byte from the real .ino
// source (see this file's own header comment on how and how verified).
// -----------------------------------------------------------------------------

int armaTitleBitmap[290] =
{
64, 36, 0xFF, 0xF0, 0x0, 0x0, 0x0, 0x0, 0xF, 0xFF, 0xFC, 0xF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
0x3F, 0xE3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC7, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xF9, 0x77, 0x9E, 0xEE, 0xF9, 0xC2, 0x79, 0xF7, 0xBA, 0x77, 0xAE, 0x4E, 0xF6, 0xDE, 0xBA, 0xEB,
0x9A, 0x6B, 0x8E, 0x4D, 0x77, 0xC6, 0xDB, 0x5D, 0xAA, 0x63, 0xAE, 0xAC, 0x74, 0xDE, 0xDB, 0x5D,
0xB2, 0x6B, 0xA6, 0xED, 0x76, 0xDE, 0xBA, 0xEB, 0xBA, 0x49, 0xB6, 0xE9, 0x39, 0xC2, 0x79, 0xF7,
0xBA, 0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xDF, 0xFF, 0xE0, 0xFF, 0xFF, 0x7, 0xFF,
0xFB, 0xE3, 0xFE, 0x1F, 0x1F, 0xF8, 0xF8, 0x7F, 0xC7, 0xF8, 0xE1, 0xFF, 0xEF, 0xF7, 0xFF, 0x87,
0x1F, 0xFE, 0xF, 0xFF, 0xF7, 0xEF, 0xFF, 0xF0, 0x7F, 0xFF, 0xFF, 0xFF, 0xF7, 0xEF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xDF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xDF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFE, 0x3, 0xC0, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE1, 0xFB, 0xDF, 0x87, 0xFF,
0xFF, 0xFF, 0xFF, 0xDF, 0xF3, 0xCF, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xDF, 0xF3, 0xCF, 0xFB, 0xFF,
0xFF, 0xFF, 0xFF, 0xDF, 0xF3, 0xCF, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xE1, 0xF3, 0xCF, 0x87, 0xFF,
0xFF, 0xFF, 0xFF, 0xFE, 0x17, 0xE8, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x7, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE7, 0xE7, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xE7, 0xE7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF, 0xF7, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xEF, 0xF7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF, 0xF7, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xDF, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0xFD, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFE, 0x7F, 0xFE, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 0xBF, 0xFF,
0xFF,
};

int armaCityBitmap[10] =
{
    8, 8, 0x0, 0x0, 0x0, 0x0, 0x64, 0x6E, 0x7F, 0xFF,
};

int armaDeadCityBitmap[10] =
{
    8, 8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x62,
};

int armaLauncherBitmap[10] =
{
    8, 8, 0x18, 0x18, 0x18, 0x24, 0x24, 0x42, 0x42, 0x81,
};

int armaDeadLauncherBitmap[10] =
{
    8, 8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x66, 0x5A, 0x81,
};

// -----------------------------------------------------------------------------
// Sound
// -----------------------------------------------------------------------------

void armaPlaySound( int idx )
{
    gbPlayNote( armaSounds[ idx ][ 0 ], armaSounds[ idx ][ 1 ] );
}

// -----------------------------------------------------------------------------
// Highscores - see this file's own header comment for the two real
// upstream EEPROM bugs found and fixed here (not preserved).
// -----------------------------------------------------------------------------

void armaLoadHighscores()
{
    int entry, b0, b1, b2, b3, allFresh;

    allFresh = 1;
    for( entry = 0; entry < ARMA_NUM_HIGHSCORES; entry++ )
    {
        b0 = eeprom_read_byte( entry * 4 + 0 );
        b1 = eeprom_read_byte( entry * 4 + 1 );
        b2 = eeprom_read_byte( entry * 4 + 2 );
        b3 = eeprom_read_byte( entry * 4 + 3 );
        armaHighscores[ entry ] = ( b0 << 24 ) | ( b1 << 16 ) | ( b2 << 8 ) | b3;
        if( b0 != 255 || b1 != 255 || b2 != 255 || b3 != 255 )
          allFresh = 0;
    }

    // A genuinely fresh card decodes every entry from raw 0xFF bytes - see
    // this file's own header comment on why that's reset to a clean 0
    // here instead of preserved as upstream's own real (and, in practice,
    // permanently-broken) behavior.
    if( allFresh )
    {
        for( entry = 0; entry < ARMA_NUM_HIGHSCORES; entry++ )
          armaHighscores[ entry ] = 0;
    }
}

int armaIsHighscore( int score )
{
    if( score > armaHighscores[ ARMA_NUM_HIGHSCORES - 1 ] ) return 1;
    return 0;
}

// Direct port of upstream's own real saveHighscore() insertion/shift logic,
// minus every bit of name handling (see this file's own header comment on
// why) and using this shim's own corrected 4-bytes-per-entry EEPROM layout.
void armaSaveHighscore( int score )
{
    int found, tmpScore, entry, b0, b1, b2, b3;

    found = 0;
    for( entry = 0; entry < ARMA_NUM_HIGHSCORES; entry++ )
    {
        if( score > armaHighscores[ entry ] )
          found = 1;
        if( found )
        {
            tmpScore = armaHighscores[ entry ];
            armaHighscores[ entry ] = score;
            score = tmpScore;
        }
    }

    for( entry = 0; entry < ARMA_NUM_HIGHSCORES; entry++ )
    {
        b0 = ( armaHighscores[ entry ] >> 24 ) & 255;
        b1 = ( armaHighscores[ entry ] >> 16 ) & 255;
        b2 = ( armaHighscores[ entry ] >> 8 ) & 255;
        b3 = armaHighscores[ entry ] & 255;
        eeprom_write_byte( entry * 4 + 0, b0 );
        eeprom_write_byte( entry * 4 + 1, b1 );
        eeprom_write_byte( entry * 4 + 2, b2 );
        eeprom_write_byte( entry * 4 + 3, b3 );
    }
}

// Direct port of upstream's own real drawHighscores(), minus the name
// column (see this file's own header comment).
void armaDrawHighscores()
{
    int entry;

    gbSetColor( 1 );
    gbCursorX = 84 / 2 - 5 * 4;
    gbCursorY = 5;
    gbPrintString( "HIGHSCORES" );

    for( entry = 0; entry < ARMA_NUM_HIGHSCORES; entry++ )
    {
        gbCursorX = 84 - 5 * 6;
        gbCursorY = 12 + 5 * entry;
        if( armaHighscores[ entry ] < 100000 ) gbPrintNumber( 0 );
        if( armaHighscores[ entry ] < 10000 ) gbPrintNumber( 0 );
        if( armaHighscores[ entry ] < 1000 ) gbPrintNumber( 0 );
        if( armaHighscores[ entry ] < 100 ) gbPrintNumber( 0 );
        if( armaHighscores[ entry ] < 10 ) gbPrintNumber( 0 );
        gbPrintNumber( armaHighscores[ entry ] );
    }
}

// -----------------------------------------------------------------------------
// Stage/lull setup - direct ports of upstream's own nextStage()/nextLull().
// -----------------------------------------------------------------------------

void armaNextStage()
{
    int i;

    armaStage++;
    for( i = 0; i < 8; i++ )
      armaCities[ i ] = armaLullCities[ i ];
    armaCities[ 2 ] = 1;
    armaCities[ 5 ] = 1;
    armaPammo[ 0 ] = 10;
    armaPammo[ 1 ] = 10;
    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        armaPDestX[ i ] = 100;
        armaPDetX[ i ] = 100;
        armaEDest[ i ] = 100;
    }

    if( armaStage > 10 ) armaEtotal = 20;
    else armaEtotal = 10 + armaStage;

    if( armaStage > 18 ) armaEspeed = 2;
    else armaEspeed = 0.2 + ( armaStage * 0.1 );
}

void armaNextLull()
{
    int i;

    armaLullMissiles = 0;
    for( i = 0; i < 8; i++ )
      armaLullCities[ i ] = 0;
}

// -----------------------------------------------------------------------------
// Drawing - direct ports of upstream's own real draw*() functions.
// -----------------------------------------------------------------------------

void armaDrawScore()
{
    gbSetColor( 1 );
    gbCursorX = 84 / 2 - 4 * 3;
    gbCursorY = 0;

    if( armaScore < 100000 ) gbPrintNumber( 0 );
    if( armaScore < 10000 ) gbPrintNumber( 0 );
    if( armaScore < 1000 ) gbPrintNumber( 0 );
    if( armaScore < 100 ) gbPrintNumber( 0 );
    if( armaScore < 10 ) gbPrintNumber( 0 );
    gbPrintNumber( armaScore );
}

void armaDrawTargets()
{
    int i;

    gbSetColor( 1 );
    gbDrawFastHLine( armaTargetX - 1, armaTargetY, 3 );
    gbDrawFastVLine( armaTargetX, armaTargetY - 1, 3 );

    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        if( armaPDestX[ i ] <= 84 && armaPDetX[ i ] > 84 )
          gbDrawPixel( armaPDestX[ i ], armaPDestY[ i ] );
    }
}

void armaDrawCities()
{
    int i;

    gbSetColor( 1 );
    for( i = 0; i < 8; i++ )
    {
        if( i == 2 || i == 5 )
        {
            if( armaCities[ i ] ) gbDrawBitmap( i * 10 + 2, 40, armaLauncherBitmap );
            else gbDrawBitmap( i * 10 + 2, 40, armaDeadLauncherBitmap );
        }
        else
        {
            if( armaCities[ i ] ) gbDrawBitmap( i * 10 + 2, 40, armaCityBitmap );
            else gbDrawBitmap( i * 10 + 2, 40, armaDeadCityBitmap );
        }
    }
}

void armaDrawAmmo()
{
    int i, j, xcoord, ycoord;

    gbSetColor( 1 );
    for( i = 0; i < 2; i++ )
    {
        if( i == 0 ) xcoord = 25;
        else xcoord = 55;
        ycoord = 47;

        if( armaCities[ i * 3 + 2 ] ) // is this launcher alive?
        {
            for( j = 0; j < armaPammo[ i ]; j++ )
            {
                gbDrawPixel( xcoord, ycoord );
                if( xcoord % 2 == 0 )
                {
                    xcoord--;
                    ycoord--;
                }
                else xcoord++;
            }
        }
    }
}

void armaDrawMissiles()
{
    int i;

    gbSetColor( 1 );
    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        if( armaPDestX[ i ] <= 84 && armaPDetX[ i ] > 84 )
        {
            if( armaPMissileLauncher[ i ] == ARMA_LAUNCHER_ONE )
              gbDrawLine( 25, 40, (int)armaPMissileX[ i ], (int)armaPMissileY[ i ] );
            else
              gbDrawLine( 56, 40, (int)armaPMissileX[ i ], (int)armaPMissileY[ i ] );
        }
    }

    for( i = 0; i < ARMA_MAX_EMISSILES; i++ )
    {
        if( armaEDest[ i ] <= 84 )
          gbDrawLine( (int)armaEMissileX0[ i ], (int)armaEMissileY0[ i ], (int)armaEMissileX[ i ], (int)armaEMissileY[ i ] );
    }
}

void armaDrawDetonations()
{
    int i;

    gbSetColor( 1 );
    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        if( armaPDetX[ i ] <= 84 )
          gbDrawCircle( armaPDetX[ i ], armaPDetY[ i ], armaPDetR[ i ] );
    }
}

// -----------------------------------------------------------------------------
// Gameplay - direct ports of upstream's own real launchMissile()/
// tryLaunchEnemy()/stepMissiles()/stepDetonations()/stepCollision()/
// checkWin()/checkLose().
// -----------------------------------------------------------------------------

void armaLaunchMissile( int launcher )
{
    int i;

    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        if( armaPDestX[ i ] > 84 ) // free slot?
        {
            if( launcher == ARMA_LAUNCHER_ONE && armaCities[ 2 ] && armaPammo[ 0 ] )
            {
                armaPDestX[ i ] = armaTargetX;
                armaPDestY[ i ] = armaTargetY;
                armaPMissileX[ i ] = 25; // x-coord of left launcher
                armaPMissileLauncher[ i ] = ARMA_LAUNCHER_ONE;
                armaPammo[ 0 ]--;
                armaPlaySound( ARMA_SOUND_PLAUNCH );
            }
            else if( launcher == ARMA_LAUNCHER_TWO && armaCities[ 5 ] && armaPammo[ 1 ] )
            {
                armaPDestX[ i ] = armaTargetX;
                armaPDestY[ i ] = armaTargetY;
                armaPMissileX[ i ] = 56; // x-coord of right launcher
                armaPMissileLauncher[ i ] = ARMA_LAUNCHER_TWO;
                armaPammo[ 1 ]--;
                armaPlaySound( ARMA_SOUND_PLAUNCH );
            }
            armaPMissileY[ i ] = 40; // y-coord of both launchers
            break;
        }
    }
}

void armaTryLaunchEnemy()
{
    int someActive, i;

    someActive = 0;
    if( armaEtotal > 0 )
    {
        // If no enemy missile is active, always spawn one to avoid a long
        // pause with nothing incoming.
        for( i = 0; i < ARMA_MAX_EMISSILES; i++ )
        {
            if( armaEDest[ i ] <= 84 )
            {
                someActive = 1;
                break;
            }
        }

        if( !someActive || armaEchance >= arand( 100 ) ) // echance of 100
        {
            for( i = 0; i < ARMA_MAX_EMISSILES; i++ )
            {
                if( armaEDest[ i ] > 84 )
                {
                    armaEtotal--;
                    armaEDest[ i ] = arand( 8 ); // target one of the 8 city/launcher slots
                    armaEMissileX0[ i ] = arand( 84 ); // screen width
                    armaEMissileY0[ i ] = 0; // top of screen
                    armaEMissileX[ i ] = armaEMissileX0[ i ]; // start and end are the same at spawn
                    armaEMissileY[ i ] = 0;
                    armaPlaySound( ARMA_SOUND_ELAUNCH );
                    break; // only spawn one
                }
            }
        }
    }
}

void armaStepMissiles()
{
    int i;
    float dir;

    // Player missiles
    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        if( armaPDestX[ i ] <= 84 && armaPDetX[ i ] > 84 )
        {
            if( gbAbsInt( armaPDestX[ i ] - (int)armaPMissileX[ i ] ) < ARMA_PSPEED
                && gbAbsInt( armaPDestY[ i ] - (int)armaPMissileY[ i ] ) < ARMA_PSPEED )
            {
                armaPDetX[ i ] = armaPDestX[ i ];
                armaPDetY[ i ] = armaPDestY[ i ];
                armaPDetR[ i ] = 0; // start detonation at radius 0
                armaPDetState[ i ] = ARMA_EXPAND;
            }
            else
            {
                dir = atan2( armaPDestY[ i ] - armaPMissileY[ i ], armaPDestX[ i ] - armaPMissileX[ i ] );
                armaPMissileX[ i ] = armaPMissileX[ i ] + ARMA_PSPEED * cos( dir );
                armaPMissileY[ i ] = armaPMissileY[ i ] + ARMA_PSPEED * sin( dir );
            }
        }
    }

    // Enemy missiles
    for( i = 0; i < ARMA_MAX_EMISSILES; i++ )
    {
        if( armaEDest[ i ] <= 84 )
        {
            if( gbAbsInt( ( armaEDest[ i ] * 10 + 6 ) - (int)armaEMissileX[ i ] ) < ARMA_PSPEED
                && gbAbsInt( 44 - (int)armaEMissileY[ i ] ) < ARMA_PSPEED )
            {
                armaCities[ armaEDest[ i ] ] = 0; // destroy city/launcher

                if( armaEDest[ i ] == 2 ) armaPammo[ 0 ] = 0;
                if( armaEDest[ i ] == 5 ) armaPammo[ 1 ] = 0;

                armaEDest[ i ] = 100;
                armaPlaySound( ARMA_SOUND_DEAD );
            }
            else
            {
                dir = atan2( 44 - armaEMissileY[ i ], ( armaEDest[ i ] * 10 + 6 ) - armaEMissileX[ i ] );
                armaEMissileX[ i ] = armaEMissileX[ i ] + armaEspeed * cos( dir );
                armaEMissileY[ i ] = armaEMissileY[ i ] + armaEspeed * sin( dir );
            }
        }
    }
}

void armaStepDetonations()
{
    int i;

    if( armaCounter % 2 == 0 )
    {
        for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
        {
            if( armaPDetX[ i ] <= 84 )
            {
                if( armaPDetState[ i ] == ARMA_EXPAND )
                {
                    if( armaPDetR[ i ] < ARMA_PRADIUS ) armaPDetR[ i ]++;
                    else armaPDetState[ i ] = ARMA_SHRINK;
                }
                // Checked immediately (not "else") since it may have just
                // been set to SHRINK above - matches upstream's own real
                // comment/behavior exactly (no delay at full size).
                if( armaPDetState[ i ] == ARMA_SHRINK )
                {
                    if( armaPDetR[ i ] > 0 ) armaPDetR[ i ]--;
                    else
                    {
                        armaPDetX[ i ] = 100; // detonation complete, remove it
                        armaPDestX[ i ] = 100; // remove this destination
                    }
                }
            }
        }
    }
}

void armaStepCollision()
{
    int i, j;
    float dx, dy, dist;

    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        if( armaPDetX[ i ] <= 84 )
        {
            for( j = 0; j < ARMA_MAX_EMISSILES; j++ )
            {
                if( armaEDest[ j ] <= 84 )
                {
                    dx = armaEMissileX[ j ] - armaPDetX[ i ];
                    dy = armaEMissileY[ j ] - armaPDetY[ i ];
                    dist = sqrt( dx * dx + dy * dy );
                    if( (float)armaPDetR[ i ] >= dist )
                    {
                        armaEDest[ j ] = 100; // remove enemy missile
                        armaScore = armaScore + 25;
                        armaPlaySound( ARMA_SOUND_DETONATE );
                    }
                }
            }
        }
    }
}

void armaCheckWin()
{
    int i;

    if( armaEtotal == 0 )
    {
        for( i = 0; i < ARMA_MAX_EMISSILES; i++ )
        {
            if( armaEDest[ i ] <= 84 ) return; // an enemy missile is still in flight
        }
        // Every enemy missile destroyed and none remain to spawn.
        armaNextLull();
        armaState = ARMA_STATE_LULL;
    }
}

void armaCheckLose()
{
    int i;

    for( i = 0; i < 8; i++ )
    {
        if( i != 2 && i != 5 )
        {
            if( armaCities[ i ] ) return; // a city remains alive
        }
    }
    // Every city is dead - it is the end of days.
    armaCounter = 0;
    armaState = ARMA_STATE_DEAD;
    armaPlaySound( ARMA_SOUND_LOSE );
}

// -----------------------------------------------------------------------------
// Per-state update - direct ports of upstream's own real stepGame()/
// drawLull()+stepLull()/stepDead()/stepPregame(), plus the new
// armaUpdateTitle() (see this file's own header comment).
// -----------------------------------------------------------------------------

void armaUpdateGame()
{
    if( gbPressed( BTN_A ) ) armaLaunchMissile( ARMA_LAUNCHER_ONE );
    if( gbPressed( BTN_B ) ) armaLaunchMissile( ARMA_LAUNCHER_TWO );

    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        if( armaTargetX - ARMA_TARGET_SPEED > 0 ) armaTargetX = armaTargetX - ARMA_TARGET_SPEED;
        else armaTargetX = 0;
    }
    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        if( armaTargetX + ARMA_TARGET_SPEED < 84 ) armaTargetX = armaTargetX + ARMA_TARGET_SPEED;
        else armaTargetX = 84;
    }
    if( gbRepeat( BTN_UP, 1 ) )
    {
        if( armaTargetY - ARMA_TARGET_SPEED > 0 ) armaTargetY = armaTargetY - ARMA_TARGET_SPEED;
        else armaTargetY = 0;
    }
    if( gbRepeat( BTN_DOWN, 1 ) )
    {
        if( armaTargetY + ARMA_TARGET_SPEED < 48 ) armaTargetY = armaTargetY + ARMA_TARGET_SPEED;
        else armaTargetY = 48;
    }

    armaTryLaunchEnemy();
    armaStepMissiles();
    armaStepDetonations();
    armaStepCollision();

    armaDrawScore();
    armaDrawTargets();
    armaDrawCities();
    armaDrawAmmo();
    armaDrawMissiles();
    armaDrawDetonations();

    armaCheckWin();
    armaCheckLose();
}

void armaDrawLull()
{
    int i, cityCount;

    cityCount = 0;
    gbSetColor( 1 );

    gbCursorX = 84 / 2 - 4 * 6;
    gbCursorY = 48 / 2 - 5 * 3;
    gbPrintString( "BONUS POINTS" );

    gbCursorX = 84 / 2 - 4 * 8;
    gbCursorY = gbCursorY + 5 * 2;
    gbPrintNumber( armaLullMissiles );

    for( i = 0; i < armaLullMissiles; i++ )
      gbDrawPixel( 84 / 2 - 4 * 6 + i * 2, 48 / 2 - 3 );

    gbCursorX = 84 / 2 - 4 * 8;
    gbCursorY = gbCursorY + 5 * 2;
    for( i = 0; i < 8; i++ ) // fixed: upstream's own loop counter here is never initialized (see header comment)
      if( armaLullCities[ i ] ) cityCount++;
    gbPrintNumber( cityCount );

    for( i = 0; i < cityCount; i++ )
      gbDrawBitmap( 84 / 2 - 4 * 6 + i * 9, 48 / 2 + 2, armaCityBitmap );

    armaDrawScore();
    armaDrawCities();
    armaDrawAmmo();
}

void armaUpdateLull()
{
    int i;

    if( armaCounter % 4 == 0 && ( armaPammo[ 0 ] > 0 || armaPammo[ 1 ] > 0 ) )
    {
        armaLullMissiles++;
        armaScore = armaScore + 10;
        if( armaPammo[ 0 ] > 0 ) armaPammo[ 0 ]--;
        else armaPammo[ 1 ]--;
        armaPlaySound( ARMA_SOUND_SCORE );
    }

    // Once we have already iterated through the ammo above.
    if( armaCounter % 8 == 0 && armaPammo[ 0 ] == 0 && armaPammo[ 1 ] == 0 )
    {
        for( i = 0; i < 9; i++ )
        {
            if( i == 8 ) // iterated through every live city
            {
                armaNextStage();
                armaState = ARMA_STATE_GAME;
                return; // skips armaDrawLull() below this frame - matches upstream exactly
            }
            if( i != 2 && i != 5 && armaCities[ i ] != 0 ) // not a launcher, and still alive
            {
                armaLullCities[ i ] = 1;
                armaScore = armaScore + 100;
                armaCities[ i ] = 0;
                armaPlaySound( ARMA_SOUND_SCORE );
                break;
            }
        }
    }

    armaDrawLull();
}

void armaUpdateDead()
{
    gbSetColor( 1 );
    gbCursorX = 84 / 2 - 5 * 3;
    gbCursorY = 48 / 2 - 5;
    gbPrintString( "THE END" );

    if( armaState == ARMA_STATE_DEAD && armaCounter % 20 == 0 )
    {
        armaState = ARMA_STATE_POSTDEAD;
    }
    else if( armaState == ARMA_STATE_POSTDEAD )
    {
        if( armaCounter % 8 == 0 ) armaFlash = !armaFlash;

        if( armaFlash )
        {
            gbCursorX = 84 / 2 - 5 * 3;
            gbCursorY = 48 - 9;
            gbPrintString( "PRESS A" ); // upstream's own real "PRESS \x15" button-icon glyph -> plain text (see header comment)

            if( armaIsHighscore( armaScore ) )
            {
                gbCursorX = 84 / 2 - 5 * 5 - 2;
                gbCursorY = 48 - 15;
                gbPrintString( "NEW HIGHSCORE" );
            }
        }

        if( gbPressed( BTN_A ) )
        {
            if( armaIsHighscore( armaScore ) )
              armaSaveHighscore( armaScore ); // name entry dropped - see header comment
            armaScore = 0;
            armaState = ARMA_STATE_PREGAME;
        }
    }
}

void armaUpdatePregame()
{
    int i;

    armaDrawHighscores();

    if( armaCounter % 8 == 0 ) armaFlash = !armaFlash;
    if( armaFlash )
    {
        gbSetColor( 1 );
        gbCursorX = 84 / 2 - 5 * 3;
        gbCursorY = 48 - 9;
        gbPrintString( "PRESS A" ); // upstream's own real "PRESS \x15" button-icon glyph -> plain text (see header comment)
    }

    if( gbPressed( BTN_A ) )
    {
        armaStage = -1; // upstream's own real uint8_t stage=255 8-bit-wraparound-to-0 trick (see header comment)
        for( i = 0; i < 8; i++ )
          armaLullCities[ i ] = 1;
        armaNextStage(); // increments armaStage back to 0
        armaState = ARMA_STATE_GAME;
    }
}

// Restored real gb.titleScreen(armageddon) logo screen - see this file's
// own header comment on the confirmed real (0,12) anchor and the added
// "PRESS A" prompt text.
void armaUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( 0, 12, armaTitleBitmap );

    gbCursorX = 28;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      armaState = armaPrevState;
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void gameArmageddon_init()
{
    int i;

    gbBegin();
    armaLoadHighscores();
    gbPickRandomSeed();

    armaState = ARMA_STATE_TITLE;
    armaPrevState = ARMA_STATE_PREGAME;
    armaCounter = 0;
    armaFlash = 0;
    armaStage = 0;
    armaScore = 0;
    armaLullMissiles = 0;

    for( i = 0; i < 8; i++ )
    {
        armaCities[ i ] = 1;
        armaLullCities[ i ] = 0;
    }

    armaTargetX = 84 / 2;
    armaTargetY = 48 / 2;
    armaPammo[ 0 ] = 10;
    armaPammo[ 1 ] = 10;

    for( i = 0; i < ARMA_MAX_PMISSILES; i++ )
    {
        armaPDestX[ i ] = 100;
        armaPDestY[ i ] = 100;
        armaPMissileX[ i ] = 100;
        armaPMissileY[ i ] = 100;
        armaPMissileLauncher[ i ] = 0;
        armaPDetX[ i ] = 100;
        armaPDetY[ i ] = 100;
        armaPDetR[ i ] = 0;
        armaPDetState[ i ] = ARMA_EXPAND;
    }

    armaEtotal = 5;
    armaEchance = 1;
    armaEspeed = 0.2;
    for( i = 0; i < ARMA_MAX_EMISSILES; i++ )
    {
        armaEDest[ i ] = 100;
        armaEMissileX0[ i ] = 100;
        armaEMissileY0[ i ] = 100;
        armaEMissileX[ i ] = 100;
        armaEMissileY[ i ] = 100;
    }
}

void gameArmageddon_update()
{
    if( !gbUpdate() ) return;

    if( armaState == ARMA_STATE_TITLE ) armaUpdateTitle();
    else if( armaState == ARMA_STATE_PREGAME ) armaUpdatePregame();
    else if( armaState == ARMA_STATE_GAME ) armaUpdateGame();
    else if( armaState == ARMA_STATE_LULL ) armaUpdateLull();
    else armaUpdateDead(); // ARMA_STATE_DEAD or ARMA_STATE_POSTDEAD - matches upstream's own switch fallthrough

    // Direct port of upstream's own real checkMenu(): Button C, in any
    // state but the title screen itself, pauses to the real title/logo
    // screen (see this file's own header comment).
    if( armaState != ARMA_STATE_TITLE && gbPressed( BTN_C ) )
    {
        armaPrevState = armaState;
        armaState = ARMA_STATE_TITLE;
    }

    armaCounter++;

    gbRenderFrame();
}
