#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Lander (Yoda Zhang / "yodasvideoarcade", License: None specified -
// yodasvideoarcade.com/gamebuino.php). One of five "yoda-*" games this
// author wrote sharing the exact same multi-tab .ino file-split
// convention (killrace/lander/invaders/asterocks/paqman - lander.ino/
// standard.ino/specific.ino/nonstandard.ino/images.ino/sounds.ino all
// compiled as one translation unit, exactly like a single real .ino
// sketch). A real Lunar-Lander clone: fly a small ship down onto a
// scrolling, randomly-tiled landscape using two lateral thrusters (D-pad
// Left/Right) and one main thruster (Button A) against constant gravity,
// touching down gently and exactly on a marked landing pad to refuel and
// score points before running out of fuel or crashing into the terrain.
// 10 built-in levels, then a harder "gravity=1" mode once they're all
// cleared once (levels repeat, `screen = gamelevel % (maxlevel+1)`, but
// gravity now pulls down twice as often).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (see gamePong.c's own header comment for
// why - this dialect has no classes/methods). `random(4)` became
// `arand(4)` (this dialect's own established RNG helper).
// `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a documented no-op.
// `gb.battery.show = false;` was dropped outright, matching every other
// port's treatment of that real-hardware-only cosmetic indicator.
// Upstream's own `and`/`or` (C++'s alternative boolean-operator spellings,
// used throughout every .ino tab here) were mechanically rewritten to
// `&&`/`||` - this dialect's own real support for the word-spelled
// alternative tokens is unconfirmed, so this port doesn't risk being the
// first to rely on them (same discipline gamePong.c/gameAgaruino.c already
// apply to `switch`). No `switch` statement existed upstream to begin
// with. Upstream's own real `String gamestatus;` compared via
// `gamestatus=="somename"` (a real Arduino `String` object with a real
// `operator==`) has no equivalent here at all (no classes/operator
// overloading) - replaced with a plain `int landState` + a `LAND_STATE_*`
// enum, one value per distinct string upstream used
// ("title"/"newgame"/"selectlevel"/"newlevel"/"newlife"/"running"/
// "gameover") plus one more state this port had to invent of its own
// (LAND_STATE_SPLASH - see "blocking titleScreen()" below).
//
// REAL BITMAP ART RESTORED. Every real `const byte PROGMEM name[]` array
// in images.ino (gamelogo, landscapetiles, spaceship, thrust, levels) was
// copied byte-for-byte (mechanically converting every Arduino
// `B00000000`-style binary literal to `0x`-prefixed hex via a small
// script reading the real .ino source directly, not hand-transcribed) into
// this dialect's own `int[N] name = {...}`/`int[N][M] name = {...}` array-
// declaration order (`TYPE[N] name`, not C's `TYPE name[N]` - see
// gameConduit.c's own `condTiles`/`condInitialMap` for the same established
// 2D-array precedent), one plain `int` per real PROGMEM byte, exactly the
// `{width, height, byte0, byte1, ...}` shape `gbDrawBitmap()` expects - no
// other conversion needed, every one of these tables already carried its
// own real width/height header per entry:
//   - `landGameLogo` (64x26, `{64,26,...}`) - the splash/title-screen logo,
//     drawn via `gbDrawBitmap(10,13,...)` at upstream's own exact real
//     anchor (both `showtitle()`'s own custom title/score screen and this
//     port's own reconstructed splash screen - see below - share this same
//     real (10,13) anchor, a deliberate visual-consistency choice this
//     port made since the splash has no anchor of its own to restore: the
//     real bitmap-drawing half of `gb.titleScreen()` isn't in this
//     upstream source at all, it's inside the real closed-source
//     `Gamebuino.cpp`).
//   - `landLandscapeTiles` (58 tiles, each 4x4, `int[58][6]`) - every
//     possible "which neighbor cells are empty" terrain-edge variant,
//     drawn tile-by-tile in `landDrawLandscape()` exactly like upstream's
//     own `drawlandscape()`.
//   - `landSpaceship` (6 frames, each 6x6, `int[6][8]`) - frame 0 = idle/
//     falling, frame 1 = any-thruster-lit (upstream lights the same single
//     "engines on" sprite for every thrust direction, not a per-thruster
//     sprite - preserved exactly), frames 2-5 = the 4-frame death
//     explosion sequence (four copies of the current frame drawn at the
//     four diagonal corners expanding outward from the ship's last
//     position, `landDrawSpaceship()`'s own dead-branch).
//   - `landThrust` (4x3, flat `int[5]`) - the small flame icon drawn next
//     to the ship on any thrust press, at upstream's own exact real
//     per-direction offsets (`shipx/10+6` for Left, `shipx/10-4` for
//     Right, `shipx/10+1,shipy/10+5` for the main thruster).
//   - `landLevels` (10 levels, each 21x12, `int[10][38]`) - each level's
//     starting terrain silhouette. No mask/fill layer exists for any of
//     these tables upstream (checked deliberately - see the header-comment
//     precedent in gameFlappyBirdo.c's own writeup on this exact bug
//     class): every sprite here is a fully self-contained outline
//     (landscape tiles are solid black already; the ship/thrust icons are
//     small enough that "off" bits being transparent reads correctly
//     against the plain white background) - nothing was skipped.
//
// A REAL, LOAD-BEARING USE OF THE GENUINE CPU-WRITABLE FRAMEBUFFER:
// `landNewLevel()` (`newlevel()` upstream) draws each level's real bitmap
// straight into the display buffer, then reads real pixels back out one
// cell at a time via `gbGetPixel()` to decide which of the 58 terrain
// tiles' edge-variants belongs at each of the 21x12 landscape cells (a
// solid cell's own 8 neighbor cells determine which "this edge is exposed"
// tile variant renders there, matching upstream's own real if-chain
// exactly, including that every check is a bare `if` rather than an
// `else if` - later matches deliberately override earlier ones for cells
// where multiple neighbor edges are exposed at once, e.g. a lone peak gets
// overridden all the way down to its own final specific tile - preserving
// this exact override order was essential, not just copying the
// individual conditions). This is the exact real technique CLAUDE.md's own
// "Source platform facts" section calls out Gamebuino Classic's hardware
// for supporting (a genuine random-access-readable framebuffer, unlike
// every TinyJoypad-lineage byte-stream-only driver) - it works here
// unmodified because this shim's own `gbDrawBitmap()`/`gbGetPixel()` are
// both real, proven ports of the real `Display::drawBitmap()`/
// `getPixel()`. Two of the eight neighbor reads (`l6`/`l8`) are computed
// but never actually used by any of the tile-selection conditions -
// preserved exactly as upstream wrote them (a real, harmless dead-read in
// the original source, not a porting artifact).
//
// STATE MACHINE / "blocking titleScreen()" TREATMENT: this game is
// unusual versus every other port in this project so far in that it has
// TWO distinct real title-screen concepts layered on top of each other,
// not one:
//   1. `showtitle()` - upstream's own CUSTOM title/high-score screen (its
//      own real drawing code, not a library call), shown whenever
//      `gamestatus=="title"`. Ported directly as LAND_STATE_TITLE /
//      `landShowTitle()` - no state-machine conversion needed here at all,
//      it was already a real polled-every-tick screen upstream, exactly
//      like every other game's own already-interactive screens.
//   2. The REAL blocking `gb.titleScreen(text, gamelogo)` library call -
//      called three separate times upstream: once in `setup()` (shown
//      once at boot, before `gamestatus` is even assigned for the first
//      time), once from `checkbuttons()` on a Button C press during
//      active gameplay (a "pause to the big splash" gesture), and once
//      more from `showtitle()` itself on a Button C press (a "show the
//      splash again from the title screen" gesture). All three converted
//      into one shared LAND_STATE_SPLASH / `landUpdateSplash()`, matching
//      the "blocking loop -> explicit resumable state" treatment used
//      throughout this project (see gamePong.c's own header comment) -
//      dismissed by a genuine fresh `gbPressed(BTN_A)`, matching real
//      `Gamebuino::titleScreen()`'s own real wait-for-Button-A semantics.
//      Real hardware boots directly into this blocking splash before
//      `gamestatus` is ever set, so `gameLander_init()` starts in
//      LAND_STATE_SPLASH too, matching that real boot order exactly.
//      A REAL UPSTREAM QUIRK PRESERVED: the boot call and the mid-game
//      Button-C call both pass `F("    Yoda's")` (four leading spaces),
//      while `showtitle()`'s own Button-C call passes `F("Yoda's")` (no
//      leading spaces) - a real, literal difference in the two source
//      files (lander.ino vs standard.ino), almost certainly the author's
//      own manual attempt at centering two differently-sized strings
//      rather than a genuine bug, but preserved exactly either way per
//      this project's own "preserve real upstream behavior/bugs by
//      default" norm: `landSplashPadded` (set at each of the three real
//      call sites to match which literal string upstream used there)
//      picks which of the two literal strings `landUpdateSplash()` prints.
//   Where upstream's own real blocking call would have frozen the whole
//   MCU for as long as the player left the splash on screen, this port's
//   non-blocking version instead lets `gbUpdate()` keep ticking every
//   real frame while LAND_STATE_SPLASH is showing - a real, necessary
//   behavior change every "blocking call -> state" conversion in this
//   project already makes (see gamePong.c's own header comment) - the
//   only games-side consequence here is that the death/gravity/fuel timers
//   are effectively paused while the splash is up (matching Pong's own
//   established precedent of an early `return` skipping the rest of that
//   tick's play logic on the same real "pause" gesture) rather than
//   upstream's own real single-loop-iteration same-tick cascade all the
//   way through a still-running game world during the block. This
//   project's own real sequential (non-`else`) `if(gamestatus==...)` chain
//   in `loop()` was otherwise preserved exactly as its own real cascading
//   dispatch in `gameLander_update()` (e.g. `landNewGame()` sets
//   `landState` to LAND_STATE_SELECTLEVEL, and since the next `if` in the
//   same tick re-reads the now-changed `landState`, `landSelectLevel()`
//   genuinely runs in that SAME tick too, exactly matching upstream's own
//   real behavior of never seeing a blank frame between `newgame`->
//   `selectlevel`->...->`running` cascades) - LAND_STATE_SPLASH's own
//   check was appended at the very end of that dispatch chain (a state
//   that never had a named `gamestatus` string of its own upstream to
//   begin with, so there's no "correct" original position for it) so that
//   a same-tick transition into it (from either `landCheckButtons()` or
//   `landShowTitle()`) still results in the splash being the last, and
//   therefore visible, thing drawn that tick - a reasonable, deliberately
//   simplified analog of upstream's own real "whatever the blocking call
//   leaves on screen when it finally returns is what's visible" behavior.
//
// A REAL SECOND UPSTREAM BUG FOUND AND PRESERVED: `drawspaceship()`'s own
// dead-ship branch (drawing the 4-way explosion) ends with its own real,
// literal call to `handledeath();` - and `loop()`'s own running-state
// block ALSO calls `handledeath();` again, unconditionally, right after
// `drawspaceship()` returns. That means `handledeath()` genuinely runs
// TWICE per real game tick for the entire duration of a death animation -
// `deadcounter` (the 30-tick countdown driving both the explosion-frame
// index and the "SHIPS LEFT" readout) actually ticks down by 2 every real
// frame, not 1, so the whole death sequence plays back roughly twice as
// fast as the literal "30" constant would otherwise suggest, and the
// "SHIPS LEFT" box gets cleared-and-redrawn twice a frame (harmless, just
// wasted cycles). This reads as a genuine copy-paste duplication bug, not
// intentional design - but since it's real, observable gameplay-timing
// behavior (not just an internal implementation detail invisible to a
// player), this port preserves it exactly: `landDrawSpaceship()` calls
// `landHandleDeath()` itself in its own dead-branch, in addition to
// `gameLander_update()`'s own separate unconditional call in the running-
// state block, exactly mirroring upstream's real double-call structure.
//
// BYTE-WRAPAROUND CLAMP TRICKS, translated to plain int comparisons: real
// `shipxspeed`/`shipyspeed` are `byte` (uint8_t) upstream. Decrementing
// them below 0 (`shipxspeed=--shipxspeed;` when already 0, or
// `shipyspeed=shipyspeed-2;` when already 0 or 1) wraps around to near 255
// on real 8-bit hardware - upstream deliberately exploits that wraparound,
// immediately following each decrement with `if (shipxspeed==255)
// shipxspeed=0;` / `if (shipyspeed>250) shipyspeed=0;`, a real "clamp at
// zero via unsigned underflow" idiom that only works because each
// decrement is a small, fixed amount (1 or 2) - the wrapped value always
// lands safely inside the specific checked band. This shim's own
// `landShipXSpeed`/`landShipYSpeed` are plain (non-wrapping) `int`s like
// every other port in this project, so the literal wraparound never
// happens here - this port instead uses a plain `if (landShipXSpeed < 0)
// landShipXSpeed = 0;` / `if (landShipYSpeed < 0) landShipYSpeed = 0;`
// after each decrement, which produces the exact same OBSERVABLE clamp-at-
// zero result upstream's own trick was actually going for, without
// depending on this shim's `int`s ever wrapping the way a real AVR `byte`
// does.
//
// A REAL, INTENTIONAL ASYMMETRY PRESERVED: `checkbuttons()` reads Left/
// Right thrust via `gb.buttons.repeat(BTN_LEFT/RIGHT, 1)` (fires every
// single tick the button stays held - continuous fine lateral control),
// but reads the main thruster via `gb.buttons.repeat(BTN_A, 0)` (real
// `Buttons::repeat()`'s own period-0 case fires only once per physical
// press, not on hold - confirmed directly against this shim's own
// `gbRepeat()`, which returns `false` outright once `period<=0` after the
// initial press). Ported as literal `gbRepeat(BTN_LEFT,1)`/
// `gbRepeat(BTN_RIGHT,1)`/`gbRepeat(BTN_A,0)` calls - main-thruster taps
// therefore genuinely only add one discrete downward-speed-reducing boost
// per press (must be re-pressed for another), while the lateral thrusters
// accelerate continuously while held. This is real, intentional upstream
// design (a lunar-lander "tap the main engine for a controlled boost,
// hold the side thrusters to steer" feel), not a porting artifact.
//
// SOUND: real `playsoundfx(fxno,channel)` builds a small per-channel
// FX-synth preset each call (waveform/instrument, volume slide, pitch/
// arpeggio slide `gb.sound.command(...)` calls, from the real
// `soundfx[][]` table) before playing one real note on top -
// `landPlaySoundFx(fxNo,channel)` is a real, faithful port of this using
// this shim's own `gbSoundCommand()`/`gbPlayNoteChannel()` primitives (a
// direct port of real Sound.h's own per-channel tracker commands). All 12
// real `playsoundfx()` call sites already passed the same real channel
// number upstream itself uses at that exact site (0 or 1), so no call
// site needed to change.
// `checkpickup()` is a genuine empty stub upstream (an apparently
// unfinished/leftover fuel-pickup feature, never actually implemented in
// this source) - ported as a literal no-op function, call site preserved,
// to keep this port's own function-per-function structure matching
// upstream 1:1.
//
// Digit-count-dependent cursor-shifting (`showtitle()`'s own
// `14-2*(score>9)-2*(score>99)-2*(score>999)` and `showscore()`'s own
// `81-4*(score>9)-...-4*(score>9999)`, both right-aligning a growing
// number by shifting its start position left one glyph-width per extra
// digit) relies on C's own "a comparison is an int 0 or 1" implicit
// conversion for direct multiplication - unproven in this dialect (no
// existing port relies on multiplying a bare comparison result), so this
// port plays it safe with an explicit `landDigitShift(value, perDigit,
// tierCount)` helper (an if-chain, not arithmetic-on-a-comparison)
// instead of risking being the first to depend on that conversion. A REAL
// UPSTREAM INCONSISTENCY preserved exactly by keeping the two call sites'
// own real `tierCount` different: `showtitle()`'s own LAST/HIGH readout
// only ever checks up to 3 extra-digit tiers (`>999`, i.e. genuinely
// misaligns a real 5-digit-or-higher score/highscore, never accounting
// for a 5th digit), while `showscore()`'s own in-game score readout checks
// a 4th tier (`>9999`) that `showtitle()` never did - a real, if minor,
// upstream inconsistency between the two screens' own real formulas, not
// a porting slip.
//
// Neither `gb.display.setFont()` nor `fontSize` is ever touched anywhere
// in this real source - every screen in this game was always meant to
// render with real hardware's own plain default font (font3x5), so this
// port never calls `gbSetFont()` either, leaving this shim's own real
// `gbBegin()`-time default (font3x5, size 1) in effect for the whole game,
// exactly matching real hardware.
//
// EEPROM PERSISTENCE ADDED, BEYOND REAL UPSTREAM - no `EEPROM.read()`/
// `EEPROM.write()` calls exist anywhere in this game's real source, so
// `landHighScore` was originally genuine in-session-only state, explicitly
// reset to 0 by upstream's own real `setup()` every single launch. Added
// directly on request once an audit found this game displays a real
// highscore that never survives a cartridge reboot: `gameLander_init()`'s
// own real `landHighScore = 0;` line is now a real `eeprom_read_word(0)`
// load instead (with the same `==0xFFFF` fresh-EEPROM-cell reset check
// already established elsewhere in this project, e.g. gameCrabator.c/
// gameDescent.c, rather than trusting a raw 65535 sentinel) - the very
// same effective "0 on a genuinely fresh card" outcome upstream's own
// hardcoded `=0` produced, just no longer discarding a real earlier save.
// Saved via `eeprom_write_word(0, landHighScore)` at the exact point
// upstream's own real highscore-tracking line already updates it in
// memory - a one-shot write per new high score, not a per-frame write.

int landScore = 0;
int landHighScore = 0;
int landLives = 0;
int landGameLevel = 0;
int landScreen = 0;
int landMaxLevel = 9;
int landShipX = 0;
int landShipY = 0;
int landShipXSpeed = 0;
int landShipYSpeed = 0;
int landFuel = 0;
int landLanded = 0;
int landGravityCounter = 0;
int landGravity = 0;
int landLandscape[21][12];
int landGoalX[10]= {9,8,11,15,12,14,11,18,14,13};
int landGoalY[10]= {11,11,11,9,11,5,11,11,4,3};
int landYeahTimer = 0;
int landDeadCounter = 0;
int landSplashPadded = 1; // which of the two real splash strings to print - see header comment

enum LandState
{
    LAND_STATE_SPLASH = 0,
    LAND_STATE_TITLE = 1,
    LAND_STATE_NEWGAME = 2,
    LAND_STATE_SELECTLEVEL = 3,
    LAND_STATE_NEWLEVEL = 4,
    LAND_STATE_NEWLIFE = 5,
    LAND_STATE_RUNNING = 6,
    LAND_STATE_GAMEOVER = 7
};

int landState;

// Real title/splash logo, 64x26 - see header comment.
int landGameLogo[210]=
{
  64,26,
 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x30,0x00,0x80,0xC0,0x9F,0x87,0xF0,0xF8,
 0x78,0x00,0x81,0xE0,0xB3,0xCF,0x81,0xEC,
 0x78,0x01,0xC1,0xE0,0xA1,0xCF,0x03,0xC6,
 0x78,0x01,0xC1,0xF0,0xA1,0xEF,0x03,0xC2,
 0x78,0x03,0xE1,0xF0,0xA1,0xEF,0x03,0xC2,
 0x78,0x03,0xE1,0x78,0xA1,0xEF,0x03,0xC2,
 0x78,0x02,0xE1,0x78,0xA1,0xEF,0x03,0xC2,
 0x78,0x06,0xF1,0x3C,0xA1,0xEF,0xE3,0xC6,
 0x78,0x04,0xF1,0x3C,0xA1,0xEF,0x03,0xFC,
 0x78,0x04,0x79,0x1E,0xA1,0xEF,0x03,0xC6,
 0x78,0x0F,0xF9,0x1E,0xA1,0xEF,0x03,0xC2,
 0x78,0x08,0x79,0x0F,0xA1,0xEF,0x03,0xC2,
 0x78,0x08,0x3D,0x0F,0xA1,0xEF,0x03,0xC2,
 0x78,0x18,0x3D,0x07,0xA1,0xCF,0x03,0xC2,
 0x7C,0x10,0x3D,0x07,0xB3,0xCF,0x83,0xC2,
 0x3F,0xD0,0x19,0x03,0x1F,0x87,0xF9,0x82,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0xA6,0xC6,0x6A,0xEC,0xE6,0x6E,0x66,0xCE,
 0xAA,0xAA,0x8A,0x4A,0x8A,0xAA,0x8A,0xA8,
 0xAA,0xAE,0xEA,0x4A,0xCA,0xEC,0x8E,0xAC,
 0x4A,0xAA,0x2A,0x4A,0x8A,0xAA,0x8A,0xA8,
 0x4E,0xCA,0xE4,0xEC,0xEE,0xAA,0xEA,0xCE,
};

// 58 real 4x4 terrain-edge tiles - see header comment.
int landLandscapeTiles[58][6]=
{
  {4,4, 0xF0,0xF0,0xF0,0xF0}, // full
  {4,4, 0xF0,0x70,0x70,0xF0}, // 4 x links frei
  {4,4, 0xF0,0x30,0xF0,0xF0},
  {4,4, 0xF0,0xF0,0x70,0xF0},
  {4,4, 0xF0,0x70,0x30,0xF0},
  {4,4, 0xF0,0xE0,0xE0,0xF0}, // 4 x rechts frei
  {4,4, 0xF0,0xC0,0xF0,0xF0},
  {4,4, 0xF0,0xF0,0xE0,0xF0},
  {4,4, 0xF0,0xE0,0xC0,0xF0},
  {4,4, 0x90,0xF0,0xF0,0xF0}, // 4 x oben frei
  {4,4, 0xD0,0xD0,0xF0,0xF0},
  {4,4, 0xB0,0xF0,0xF0,0xF0},
  {4,4, 0x90,0xB0,0xF0,0xF0},
  {4,4, 0xF0,0xF0,0xF0,0x90}, // 4 x unten frei
  {4,4, 0xF0,0xF0,0xB0,0xB0},
  {4,4, 0xF0,0xF0,0xF0,0xD0},
  {4,4, 0xF0,0xF0,0xD0,0x90},
  {4,4, 0x10,0x70,0xF0,0xF0}, // 4 x links oben frei
  {4,4, 0x10,0x10,0x70,0xF0},
  {4,4, 0x10,0x70,0x30,0xF0},
  {4,4, 0x10,0x50,0x70,0xF0},
  {4,4, 0xC0,0xE0,0xE0,0xF0}, // 4 x rechts oben frei
  {4,4, 0x80,0xC0,0xC0,0xF0},
  {4,4, 0x80,0xA0,0xE0,0xF0},
  {4,4, 0x80,0xE0,0xC0,0xF0},
  {4,4, 0xF0,0x70,0x70,0x30}, // 4 x links unten frei
  {4,4, 0xF0,0x30,0x30,0x10},
  {4,4, 0xF0,0x70,0x50,0x10},
  {4,4, 0xF0,0x30,0x70,0x10},
  {4,4, 0xF0,0xE0,0xE0,0xC0}, // 4 x rechts unten frei
  {4,4, 0xF0,0xC0,0xC0,0x80},
  {4,4, 0xF0,0xE0,0xA0,0x80},
  {4,4, 0xF0,0xC0,0xE0,0x80},
  {4,4, 0x10,0x30,0xF0,0x30}, // 4 x spitze links
  {4,4, 0x30,0xF0,0x30,0x10},
  {4,4, 0x10,0x30,0x70,0x30},
  {4,4, 0x30,0x70,0x30,0x10},
  {4,4, 0xC0,0xF0,0xC0,0x80}, // 4 x spitze rechts
  {4,4, 0x80,0xC0,0xF0,0xC0},
  {4,4, 0xC0,0xE0,0xC0,0x80},
  {4,4, 0x80,0xC0,0xE0,0xC0},
  {4,4, 0x40,0x40,0xE0,0xF0}, // 4 x spitze oben
  {4,4, 0x20,0x20,0x70,0xF0},
  {4,4, 0x00,0x40,0xE0,0xF0},
  {4,4, 0x00,0x20,0x70,0xF0},
  {4,4, 0xF0,0xE0,0x40,0x40}, // 4 x spitze unten
  {4,4, 0xF0,0x70,0x20,0x20},
  {4,4, 0xF0,0xE0,0x40,0x00},
  {4,4, 0xF0,0x70,0x20,0x00},
  {4,4, 0xD0,0xF0,0xF0,0xB0}, // 4 x horizontale
  {4,4, 0xB0,0xF0,0xF0,0xD0},
  {4,4, 0x90,0xD0,0xF0,0xB0},
  {4,4, 0xD0,0xF0,0xB0,0x90},
  {4,4, 0xF0,0x70,0xE0,0xF0}, // 4 x vertikale
  {4,4, 0xF0,0xE0,0x70,0xF0},
  {4,4, 0xF0,0x30,0x60,0xF0},
  {4,4, 0xF0,0x60,0xC0,0xF0},
  {4,4, 0x60,0xD0,0xB0,0x60}, // fuel
};

// 6 real 6x6 ship frames (idle/thrust/4-frame death explosion) - see header comment.
int landSpaceship[6][8]=
{
  {6,6, 0x48,0x30,0x78,0x30,0x48,0x00},
  {6,6, 0x48,0x30,0x78,0x30,0x48,0x84},
  {6,6, 0x68,0x94,0x44,0x88,0xA4,0x58},
  {6,6, 0x10,0x44,0x90,0x24,0x88,0x20},
  {6,6, 0x84,0x20,0x08,0x40,0x10,0x84},
  {6,6, 0x10,0x00,0x80,0x04,0x00,0x20},
};

// Real 4x3 thrust-flame icon - see header comment.
int landThrust[5]= {4,3, 0x60,0xF0,0x60};

// 10 real 21x12 level layouts - see header comment.
int landLevels[10][38]=
{
  {21,12, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x08, 0x40,0x00,0x18, 0xE0,0x00,0x78, 0xE0,0x00,0x38, 0xC0,0x00,0x38, 0xE0,0x00,0x78, 0xF1,0x04,0xF8, 0xFF,0xFF,0xF8},
  {21,12, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x80, 0x00,0x01,0x80, 0x90,0x01,0xC8, 0xFC,0x03,0xC8, 0xFE,0x07,0xD8, 0xF0,0x0F,0xF8, 0xE0,0x07,0xF8, 0xC0,0x03,0xF8, 0xF8,0x07,0xF8, 0xFF,0xFF,0xF8},
  {21,12, 0x00,0x3F,0xF8, 0x00,0x09,0xF8, 0x00,0x00,0xF8, 0x00,0x00,0xB8, 0x02,0x00,0x38, 0x76,0x00,0x18, 0xFF,0x00,0x38, 0xFF,0xC0,0xB8, 0xFF,0x81,0xF8, 0xFF,0x01,0xF8, 0xFF,0x80,0xF8, 0xFF,0xFF,0xF8},
  {21,12, 0x07,0xFF,0xF8, 0x03,0xFF,0xF8, 0x03,0xFD,0x78, 0x01,0x78,0x58, 0x80,0x50,0x18, 0x80,0x00,0x08, 0x80,0x00,0x18, 0xD0,0x00,0x08, 0xF4,0x00,0x18, 0xFD,0x03,0xF8, 0xFF,0x27,0xF8, 0xFF,0xFF,0xF8},
  {21,12, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x08, 0x80,0x00,0x18, 0xC8,0x00,0x08, 0xFE,0xD8,0x08, 0xFF,0xFC,0x08, 0xFF,0xF0,0x08, 0xFF,0xA0,0x08, 0xFF,0x00,0x18, 0xFF,0x80,0xF8, 0xFF,0xFF,0xF8},
  {21,12, 0x03,0x78,0x98, 0x03,0xD0,0x08, 0x01,0xC0,0x18, 0x80,0x80,0x08, 0x80,0x00,0x08, 0x80,0x07,0xC0, 0xC0,0x0F,0xE8, 0xE0,0x1F,0xE8, 0xC0,0xF8,0x78, 0xE0,0x73,0x38, 0x7D,0xE7,0x98, 0x3F,0x8F,0xC8},
  {21,12, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x45,0x00, 0x81,0xFF,0x80, 0xC7,0xFF,0xC0, 0xE3,0xFF,0xE0, 0xC1,0xFF,0xC0, 0xF0,0x93,0x80, 0xF8,0x00,0x00, 0xFC,0x00,0x00, 0xFE,0x00,0x08, 0xFE,0xFF,0xF8},
  {21,12, 0x07,0xC0,0x38, 0x07,0xE0,0x18, 0x07,0x80,0x18, 0x03,0x00,0x08, 0x03,0x80,0x00, 0x03,0x04,0x00, 0x81,0x04,0x00, 0xC0,0x0C,0x00, 0xE0,0x0E,0x00, 0xF0,0x1E,0x00, 0xE0,0xFF,0x80, 0xFF,0xFF,0xF8},
  {21,12, 0x00,0xF0,0x38, 0x00,0x60,0x08, 0x38,0xC0,0x08, 0xE0,0xC0,0x00, 0xC1,0x83,0xC0, 0x81,0x87,0x40, 0x83,0x0F,0xE0, 0x02,0x1B,0xE0, 0x80,0x0F,0xB0, 0x90,0x1F,0xF0, 0xD0,0x3E,0xF0, 0xFF,0xFF,0xF8},
  {21,12, 0x01,0xE0,0x38, 0x00,0xF0,0x18, 0x48,0x78,0x08, 0xFC,0x3F,0x88, 0xFC,0x3F,0xC0, 0xF8,0x7F,0x80, 0xF0,0x7F,0x00, 0xF0,0xF6,0x00, 0xD0,0x40,0x08, 0x90,0x00,0x18, 0x80,0x00,0x78, 0xFF,0xFF,0xF8},
};

// Real per-effect [waveform, pitch, ?, ?, ?, ?, volume, duration] table,
// copied verbatim - see header comment.
int landSoundFx[5][8]=
{
  {1,17,53,0,7,0,7,3},  // 0 = thrust (channel 0)
  {1,26,41,1,1,3,7,20}, // 1 = crash (channel 1)
  {0,0,42,1,1,2,7,20},  // 2 = landing success (channel 1)
  {0,54,0,0,0,0,7,1},   // 3 = fuel low (channel 1)
  {0,0,65,1,1,1,7,5},   // 4 = pick up fuel (channel 1)
};

void landPlaySoundFx( int fxNo, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, landSoundFx[ fxNo ][ 6 ], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, landSoundFx[ fxNo ][ 0 ], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, landSoundFx[ fxNo ][ 5 ], -landSoundFx[ fxNo ][ 4 ], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, landSoundFx[ fxNo ][ 3 ], landSoundFx[ fxNo ][ 2 ] - 58, channel );
    gbPlayNoteChannel( landSoundFx[ fxNo ][ 1 ], landSoundFx[ fxNo ][ 7 ], channel );
}

// Right-aligns a growing number by shifting its start position left by
// `perDigit` pixels per extra digit beyond the first, up to `tierCount`
// extra tiers - see header comment on why this is an explicit if-chain
// rather than arithmetic on a bare comparison, and on the two real call
// sites' own different real `tierCount`.
int landDigitShift( int v, int perDigit, int tierCount )
{
    int shift = 0;
    if( ( tierCount >= 1 ) && ( v > 9 ) ) shift = shift + perDigit;
    if( ( tierCount >= 2 ) && ( v > 99 ) ) shift = shift + perDigit;
    if( ( tierCount >= 3 ) && ( v > 999 ) ) shift = shift + perDigit;
    if( ( tierCount >= 4 ) && ( v > 9999 ) ) shift = shift + perDigit;
    return shift;
}

void landCheckPickup()
{
    // real upstream stub - see header comment
}

void landCheckButtons()
{
    if( gbRepeat( BTN_LEFT, 1 ) && ( landDeadCounter == -1 ) && ( landYeahTimer == 0 ) )
    {
        landShipXSpeed = landShipXSpeed - 1;
        landFuel = landFuel - 1;
        landPlaySoundFx( 0, 0 );
        if( landFuel < 50 ) landPlaySoundFx( 3, 1 );
        gbDrawBitmap( landShipX / 10 + 6, landShipY / 10 + 1, landThrust );
        if( landShipXSpeed < 0 ) landShipXSpeed = 0;
    }
    if( gbRepeat( BTN_RIGHT, 1 ) && ( landDeadCounter == -1 ) && ( landYeahTimer == 0 ) )
    {
        landShipXSpeed = landShipXSpeed + 1;
        landFuel = landFuel - 1;
        landPlaySoundFx( 0, 0 );
        if( landFuel < 50 ) landPlaySoundFx( 3, 1 );
        gbDrawBitmap( landShipX / 10 - 4, landShipY / 10 + 1, landThrust );
        if( landShipXSpeed > 20 ) landShipXSpeed = 20;
    }
    if( gbRepeat( BTN_A, 0 ) && ( landDeadCounter == -1 ) && ( landYeahTimer == 0 ) )
    {
        landShipYSpeed = landShipYSpeed - 2;
        landFuel = landFuel - 1;
        landPlaySoundFx( 0, 0 );
        if( landFuel < 50 ) landPlaySoundFx( 3, 1 );
        gbDrawBitmap( landShipX / 10 + 1, landShipY / 10 + 5, landThrust );
        if( landShipYSpeed < 0 ) landShipYSpeed = 0;
    }

    if( gbPressed( BTN_C ) )
    {
        landState = LAND_STATE_SPLASH;
        landSplashPadded = 1;
        return;
    }

    if( landFuel < 0 ) landFuel = 0;
    landGravityCounter = landGravityCounter + 1;
    if( landGravityCounter > ( 2 - landGravity ) )
    {
        landShipYSpeed = landShipYSpeed + 1;
        landGravityCounter = 0;
        if( landShipYSpeed > 20 ) landShipYSpeed = 20;
    }
}

void landDrawLandscape()
{
    int x, y;
    for( y = 0; y < 12; y = y + 1 )
    {
        for( x = 0; x < 21; x = x + 1 )
        {
            if( landLandscape[ x ][ y ] != 0 )
              gbDrawBitmap( x * 4, y * 4, landLandscapeTiles[ landLandscape[ x ][ y ] - 1 ] );
        }
    }
}

void landHandleDeath()
{
    if( landDeadCounter != -1 )
    {
        landDeadCounter = landDeadCounter - 1;
        gbSetColor( 0 );
        gbFillRect( 17, 19, 50, 7 );
        gbSetColor( 1 );
        gbCursorX = 18;
        gbCursorY = 20;
        gbPrintNumber( landLives - 1 );
        gbCursorX = 26;
        gbPrintString( "SHIPS LEFT" );
        if( landDeadCounter == 0 )
        {
            landDeadCounter = -1;
            landLives = landLives - 1;
            if( landLives == 0 ) landState = LAND_STATE_GAMEOVER;
            else landState = LAND_STATE_NEWLIFE;
        }
    }
}

void landDrawSpaceship()
{
    if( ( landDeadCounter == -1 ) && ( landYeahTimer == 0 ) )
    {
        landShipX = landShipX + ( landShipXSpeed - 10 );
        landShipY = landShipY + ( landShipYSpeed - 10 );
        if( landShipX > 780 ) landShipX = 780;
        if( landShipX < 0 ) landShipX = 0;
        if( landShipY < 0 ) landShipY = 0;
    }

    if( landDeadCounter == -1 )
    {
        if( ( ( landShipYSpeed >= 11 ) && ( landShipYSpeed <= 12 ) && ( landShipXSpeed > 8 ) && ( landShipXSpeed < 12 ) ) || ( landYeahTimer != 0 ) )
        {
            gbDrawBitmap( landShipX / 10, landShipY / 10, landSpaceship[ 1 ] );
            // check if landed
            if( ( landShipX / 10 >= landGoalX[ landScreen ] * 4 ) && ( landShipX / 10 + 5 <= landGoalX[ landScreen ] * 4 + 11 ) && ( landShipY / 10 + 6 >= landGoalY[ landScreen ] * 4 ) && ( landYeahTimer == 0 ) )
              landLanded = 1;
        }
        else
        {
            gbDrawBitmap( landShipX / 10, landShipY / 10, landSpaceship[ 0 ] );
        }
    }
    else
    {
        gbDrawBitmap( landShipX / 10 - 10 + landDeadCounter / 4, landShipY / 10 - 10 + landDeadCounter / 4, landSpaceship[ 5 - landDeadCounter / 10 ] );
        gbDrawBitmap( landShipX / 10 - 10 + landDeadCounter / 4, landShipY / 10 + 10 - landDeadCounter / 4, landSpaceship[ 5 - landDeadCounter / 10 ] );
        gbDrawBitmap( landShipX / 10 + 10 - landDeadCounter / 4, landShipY / 10 - 10 + landDeadCounter / 4, landSpaceship[ 5 - landDeadCounter / 10 ] );
        gbDrawBitmap( landShipX / 10 + 10 - landDeadCounter / 4, landShipY / 10 + 10 - landDeadCounter / 4, landSpaceship[ 5 - landDeadCounter / 10 ] );
        landHandleDeath(); // real upstream double-call quirk - see header comment
    }

    // draw landing platform
    gbSetColor( 0 );
    gbDrawFastHLine( landGoalX[ landScreen ] * 4, landGoalY[ landScreen ] * 4 + 1, 12 );
    gbSetColor( 1 );
    gbDrawFastHLine( landGoalX[ landScreen ] * 4, landGoalY[ landScreen ] * 4, 12 );
}

void landSelectLevel()
{
    gbCursorY = 18;
    gbCursorX = 10;
    gbPrintString( "SELECT LEVEL:" );
    gbCursorX = 66;
    gbPrintNumber( landGameLevel + 1 );
    gbCursorY = 36;
    gbCursorX = 2;
    gbPrintString( "LEFT/RIGHT TO SELECT" );
    gbCursorX = 18;
    gbCursorY = 42;
    gbPrintString( "B TO CONFIRM" );
    if( gbRepeat( BTN_LEFT, 2 ) && ( landGameLevel > 0 ) )
    {
        landGameLevel = landGameLevel - 1;
        landPlaySoundFx( 4, 1 );
    }
    if( gbRepeat( BTN_RIGHT, 2 ) && ( landGameLevel < landMaxLevel ) )
    {
        landGameLevel = landGameLevel + 1;
        landPlaySoundFx( 4, 1 );
    }
    if( gbRepeat( BTN_B, 1 ) )
    {
        landState = LAND_STATE_NEWLEVEL;
        landPlaySoundFx( 4, 1 );
    }
}

void landCheckCollision()
{
    if( ( landDeadCounter == -1 ) && ( landYeahTimer == 0 ) )
    {
        int x = landShipX / 10;
        int y = landShipY / 10;
        int l0 = gbGetPixel( x + 1, y ) + gbGetPixel( x + 2, y + 1 ) + gbGetPixel( x + 3, y + 1 ) + gbGetPixel( x + 4, y );
        int l1 = gbGetPixel( x + 1, y + 4 ) + gbGetPixel( x + 2, y + 3 ) + gbGetPixel( x + 3, y + 3 ) + gbGetPixel( x + 4, y + 4 );
        int l2 = gbGetPixel( x + 1, y + 2 ) + gbGetPixel( x + 4, y + 2 );
        int l3 = 0;
        if( ( landShipYSpeed >= 11 ) && ( landShipYSpeed <= 12 ) && ( landShipXSpeed > 8 ) && ( landShipXSpeed < 12 ) )
          l3 = gbGetPixel( x, y + 5 ) + gbGetPixel( x + 5, y + 5 );
        if( ( l0 + l1 + l2 + l3 ) != 0 )
        {
            landDeadCounter = 30;
            landPlaySoundFx( 1, 1 );
        }
    }
}

void landNewGame()
{
    landScore = 0;
    landLives = 3;
    landGravity = 0;
    landState = LAND_STATE_SELECTLEVEL;
}

void landNewLevel()
{
    // create landscape array from level bitmap
    landScreen = landGameLevel % ( landMaxLevel + 1 );
    if( landGameLevel > landMaxLevel ) landGravity = 1;

    gbSetColor( 0 );
    gbFillRect( 0, 0, 21, 12 );
    gbSetColor( 1 );
    gbDrawBitmap( 0, 0, landLevels[ landScreen ] );

    int x, y;
    for( y = 0; y < 12; y = y + 1 )
    {
        for( x = 0; x < 21; x = x + 1 )
        {
            int l0 = gbGetPixel( x, y );
            int l1 = 1;
            if( ( x > 0 ) && ( y > 0 ) ) l1 = gbGetPixel( x - 1, y - 1 );
            int l2 = 1;
            if( y > 0 ) l2 = gbGetPixel( x, y - 1 );
            int l3 = 1;
            if( ( x < 20 ) && ( y > 0 ) ) l3 = gbGetPixel( x + 1, y - 1 );
            int l4 = 1;
            if( x > 0 ) l4 = gbGetPixel( x - 1, y );
            int l5 = 1;
            if( x < 20 ) l5 = gbGetPixel( x + 1, y );
            int l6 = 1;
            if( ( x > 0 ) && ( y < 11 ) ) l6 = gbGetPixel( x - 1, y + 1 );
            int l7 = 1;
            if( y < 11 ) l7 = gbGetPixel( x, y + 1 );
            int l8 = 1;
            if( ( x < 20 ) && ( y < 11 ) ) l8 = gbGetPixel( x + 1, y + 1 );

            int l = 0;
            if( l0 == 1 )
            {
                l = 1;
                if( l4 == 0 ) l = 2 + arand( 4 );
                if( l5 == 0 ) l = 6 + arand( 4 );
                if( l2 == 0 ) l = 10 + arand( 4 );
                if( l7 == 0 ) l = 14 + arand( 4 );
                if( ( l2 == 0 ) && ( l4 == 0 ) ) l = 18 + arand( 4 );
                if( ( l2 == 0 ) && ( l5 == 0 ) ) l = 22 + arand( 4 );
                if( ( l4 == 0 ) && ( l7 == 0 ) ) l = 26 + arand( 4 );
                if( ( l5 == 0 ) && ( l7 == 0 ) ) l = 30 + arand( 4 );
                if( ( l2 == 0 ) && ( l7 == 0 ) ) l = 50 + arand( 4 );
                if( ( l4 == 0 ) && ( l5 == 0 ) ) l = 54 + arand( 4 );
                if( ( l2 == 0 ) && ( l4 == 0 ) && ( l7 == 0 ) ) l = 34 + arand( 4 );
                if( ( l2 == 0 ) && ( l5 == 0 ) && ( l7 == 0 ) ) l = 38 + arand( 4 );
                if( ( l2 == 0 ) && ( l4 == 0 ) && ( l5 == 0 ) ) l = 42 + arand( 4 );
                if( ( l4 == 0 ) && ( l5 == 0 ) && ( l7 == 0 ) ) l = 46 + arand( 4 );
                if( ( l2 == 0 ) && ( l4 == 0 ) && ( l5 == 0 ) && ( l7 == 0 ) ) l = 58;
            }
            landLandscape[ x ][ y ] = l;
        }
    }

    gbSetColor( 0 );
    gbFillRect( 0, 0, 21, 12 );
    gbSetColor( 1 );
    landState = LAND_STATE_NEWLIFE;
}

void landNewLife()
{
    landShipX = 8;
    landShipY = 0;
    landShipXSpeed = 10;
    landShipYSpeed = 10;
    landDeadCounter = -1;
    landLanded = 0;
    landYeahTimer = 0;
    landFuel = 500;
    landState = LAND_STATE_RUNNING;
}

void landShowScore()
{
    if( landShipY / 10 > 10 )
    {
        gbSetColor( 0 );
        gbFillRect( 0, 0, 84, 6 );
        gbSetColor( 1 );
        gbCursorY = 0;
        gbCursorX = 81 - landDigitShift( landScore, 4, 4 );
        gbPrintNumber( landScore );
        gbCursorX = 0;
        gbPrintString( "F" );
        gbDrawFastHLine( 4, 2, landFuel / 10 );
    }
}

void landNextLevelCheck()
{
    // increment timer after landed
    if( landLanded == 1 )
    {
        landYeahTimer = landYeahTimer + 1;
        if( landFuel > 9 )
        {
            landFuel = landFuel - 10;
            landScore = landScore + 10;
            if( ( landScore % 30 ) == 0 ) landPlaySoundFx( 3, 1 );
        }
        gbSetColor( 0 );
        gbFillRect( 5, 17, 74, 7 );
        gbSetColor( 1 );
        gbCursorX = 6;
        gbCursorY = 18;
        gbPrintString( "READY FOR LEVEL" );
        gbCursorX = 70;
        gbPrintNumber( landGameLevel + 2 );
        if( landYeahTimer >= 50 )
        {
            landGameLevel = landGameLevel + 1;
            landState = LAND_STATE_NEWLEVEL;
            landPlaySoundFx( 2, 1 );
        }
    }
}

void landShowGameOver()
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
        landState = LAND_STATE_TITLE;
        gbPlayOK();
    }
}

void landShowTitle()
{
    if( landScore > landHighScore )
    {
        landHighScore = landScore;
        eeprom_write_word( 0, landHighScore );
    }
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "  LAST         HIGH" );
    gbCursorX = 14 - landDigitShift( landScore, 2, 3 );
    gbCursorY = 6;
    gbPrintNumber( landScore );
    gbCursorX = 66 - landDigitShift( landHighScore, 2, 3 );
    gbCursorY = 6;
    gbPrintNumber( landHighScore );
    gbDrawBitmap( 10, 13, landGameLogo );
    gbCursorX = 0;
    gbCursorY = 42;
    gbPrintString( " A: PLAY     C: QUIT" );
    if( gbPressed( BTN_A ) )
    {
        landState = LAND_STATE_NEWGAME;
        gbPlayOK();
    }
    if( gbPressed( BTN_C ) )
    {
        landState = LAND_STATE_SPLASH;
        landSplashPadded = 0;
    }
}

// Non-blocking stand-in for the real blocking `gb.titleScreen(text,
// gamelogo)` library call - see header comment for why one shared state
// covers all three real call sites, and for `landSplashPadded`'s own
// purpose.
void landUpdateSplash()
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 4;
    if( landSplashPadded == 1 ) gbPrintString( "    Yoda's" );
    else gbPrintString( "Yoda's" );
    gbDrawBitmap( 10, 13, landGameLogo );
    gbCursorX = 14;
    gbCursorY = 42;
    gbPrintString( "PRESS A" );
    if( gbPressed( BTN_A ) )
      landState = LAND_STATE_TITLE;
}

void gameLander_init()
{
    gbBegin();
    gbSetFrameRate( 20 );
    gbPickRandomSeed();
    landHighScore = eeprom_read_word( 0 );
    if( landHighScore == 0xFFFF ) landHighScore = 0;
    landGameLevel = 0;
    landSplashPadded = 1;
    landState = LAND_STATE_SPLASH; // real hardware boots straight into the blocking splash too
}

void gameLander_update()
{
    if( !gbUpdate() ) return;

    // Sequential (non-`else`) dispatch, matching upstream's own real
    // `if(gamestatus==...)` chain in `loop()` exactly, including its own
    // real same-tick cascades - see header comment.
    if( landState == LAND_STATE_NEWGAME ) landNewGame();
    if( landState == LAND_STATE_SELECTLEVEL ) landSelectLevel();
    if( landState == LAND_STATE_NEWLEVEL ) landNewLevel();
    if( landState == LAND_STATE_NEWLIFE ) landNewLife();
    if( landState == LAND_STATE_RUNNING )
    {
        landCheckButtons();  // check buttons and change ship direction
        landDrawLandscape(); // draw the landscape
        landCheckCollision(); // check collision with landscape
        landDrawSpaceship(); // draw ship, fuel and landing platforms
        landCheckPickup();   // check fuel pickup and landing
        landNextLevelCheck(); // next level?
        landHandleDeath();   // handle deathcounter
        landShowScore();     // show lives, score, level
    }
    if( landState == LAND_STATE_TITLE ) landShowTitle();
    if( landState == LAND_STATE_GAMEOVER ) landShowGameOver();
    if( landState == LAND_STATE_SPLASH ) landUpdateSplash();

    gbRenderFrame();
}
