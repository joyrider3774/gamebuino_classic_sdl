#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// BigBlackBox (STUDIOCRAFTapps, "more games/BigBlackBox/BigBlackBox.ino",
// 2568 real source lines - correcting an earlier, since-invalidated audit
// pass's "5662 lines" estimate). Real, exact license text from the file's
// own header comment (quoted verbatim, not a standard open-source license
// name):
//
//   (C) Create by STUDIOCRAFTapps
//
//   You are free to modify this code as long as you keep this message.
//   Do not sell this game. Feel free to copy 'parts' of the game to
//   help you create your.
//
// A 13-level physics-platformer/puzzle game: squash-and-stretch jump
// physics (a real, hand-tuned "bounciness" curve - see BBB_BOUNCY_MATH1-4
// below), wall-jumping, teleporter pairs, key/locked-door pickups,
// piston-linked moving platforms/switches, patrolling enemies, conveyor
// belts, spikes and trampolines - all driven by one big per-tile collider-
// type dispatch table (`bbbColType[]`/`bbbPixelInCollider()`, a direct port
// of real upstream's `coltype[]`/`PixelInCollider()`). Progress (which
// levels are unlocked) persists across sessions via a real, single-byte
// EEPROM save.
//
// STRUCTURAL NOTE (already investigated/settled before this port started,
// not re-verified here): the repo also ships its own `lib_Gamebuino.cpp/h`/
// `lib_Display.cpp/h`/`lib_Buttons.cpp/h`/`lib_Sound.cpp/h`/`lib_font*.c`/
// `lib_settings.c` - confirmed to be the real, genuine LGPL Gamebuino
// Classic library, merely vendored locally under a `lib_` prefix, not a
// from-scratch reimplementation. None of those files were read or ported;
// only `BigBlackBox.ino` itself was read, with every real `gb.display.x()`/
// `gb.buttons.x()`/`gb.sound.x()`/`gb.x()`/`EEPROM.x()` call site
// mechanically rewritten to this project's own `gbY()`/`eeprom_*()` shim
// primitives, exactly like every other ported game.
//
// DIALECT REWRITES:
// - `byte`/`boolean`/`char` -> plain `int`/`bool`/`int` throughout (this
//   project's own established convention - see gameArtillery.c's own header
//   comment for the precedent). Every real velocity/position value upstream
//   declares as a narrow AVR type (`char VelocityX/VelocityY`, `byte
//   SquisheVertical/Horizontal`) only ever holds values the code itself
//   already clamps into a small, always-in-range window (-127..127 for
//   velocity; roughly 5..13 for squish) - confirmed by tracing every real
//   assignment site by hand - so this dialect's non-narrowing 32-bit `int`
//   produces bit-for-bit identical results to real AVR's narrow, wrapping
//   types here; nothing in this game relies on an actual AVR narrow-type
//   wraparound (unlike, say, real Skibuino's own EEPROM narrowing bug
//   documented in this project's own CLAUDE.md).
// - `B01111111`-style Arduino binary literals -> `0x`-prefixed hex, done by
//   a small one-off local script; every bitmap's real element count was
//   verified against its own real `{width,height,...}` header
//   (`2+ceil(width/8)*height`) before being pasted in below - all 63
//   sprite/UI bitmaps (52 real tile-ID sprites plus 11 extra UI/cinematic
//   sprites) and the 64x30 title `Logo` bitmap check out exactly.
// - Real `class EnnemieAI` (a private `PosX`/`PosY`/`Progress`/`GoRight`/
//   `Pre` + public accessor/`update()` method set) flattened into a plain
//   `struct BbbEnnemie { posX, posY, progress, goRight }` + free functions
//   taking an explicit array index (`bbbEnnemieUpdate(idx)`), the same
//   "flatten a real single-instance-per-object C++ class into plain C"
//   treatment already proven by gameCopter.c's own `struct CoptBatiment`
//   etc. Real upstream's own `new EnnemieAI[EnnemieCount]`/`new byte[...]`
//   dynamic per-level allocations (also used for `KeyX`/`KeyY`/`KeyGot`/
//   `LockerX`/`LockerY`/`LockerGot`) became fixed-size arrays
//   (`BBB_MAX_ENNEMIES`/`BBB_MAX_KEYS`/`BBB_MAX_LOCKERS` = 8 each) - the
//   real per-level maximums are only 3 enemies and 3 keys/lockers (Map8 and
//   Map2 respectively, confirmed by directly counting real tile-ID
//   occurrences in every one of the 13 real map arrays), so 8 is
//   comfortable headroom, matching this project's own "modest headroom"
//   convention elsewhere (MAX_GAMES, the thumbnail atlas, etc).
// - No `switch` statement used anywhere (this project's own established
//   "no switch statement proven to work" convention, per gameCastleDefence.c's
//   own header comment) - real upstream's own big `switch(GlobalColiderType)`
//   in `PixelInCollider()` became an if/else-if chain, case order and every
//   magic number preserved exactly.
// - Real `GetTPSer1X()`/`GetTPSer1Y()`/`GetTPSer2X()`/`GetTPSer2Y()` (trivial
//   one-line `pgm_read_byte()` wrapper accessors, needed on real hardware
//   only to reach PROGMEM/flash) were dropped - this dialect has one flat
//   address space (`pgm_read_byte(addr)` is already a plain `*(addr)` no-op
//   per avrCompat.h), so every one of their ~4 call sites now just indexes
//   `bbbTeleporterSerie1X[]`/etc directly. A trivial simplification with no
//   behavioral effect, not a design change.
// - Real `const byte* sprites[52]`/`const byte* coltype[52]` both used the
//   SAME pointer-array C++ type, but `coltype[52]` was actually initialized
//   with plain small integer literals (0-18), not real bitmap-data
//   pointers - a genuine sloppy-but-functional real upstream mistake (an
//   integer implicitly reinterpreted as a pointer value, then read back and
//   truncated to a `byte` at every real use site, which happens to
//   reproduce the original literal for values this small). Ported as a
//   plain `int[52] bbbColType` holding the exact same literal values real
//   upstream's own sloppy pointer array held - the actually-used semantic,
//   not upstream's own accidental type.
// - Real upstream's own genuinely dead/unused data was not ported (confirmed
//   by a project-wide grep of every use site before dropping anything):
//   `Back0`/`Back1` (both real draw call sites are inside a `/* ... */`
//   block comment - never actually drawn on real hardware either),
//   `coltypeSys[52]` (declared, never read anywhere), the commented-out
//   `Name[]` bitmap stub, and the global `bool Found` (every real write
//   site is itself inside a `//` line comment).
// - Real `gb.battery.show = false;` has no equivalent call site here - this
//   shim never draws a battery indicator of any kind (no `gbBattery*`
//   primitive exists at all), so the real call's own intent (don't show a
//   battery icon) already holds trivially; dropped rather than ported as a
//   no-op. Real upstream's own two `//gb.popup(...)` call sites (inside
//   `CheckForCollider()`) are themselves commented-out debug code on real
//   hardware too - nothing to port there either.
// - No float-literal `f` suffix anywhere (`12.5f`) - this dialect's lexer
//   rejects it outright ("bad floating point literal", confirmed directly
//   by a failed compile attempt during this port). Every real upstream
//   `f`-suffixed literal (`BouncyMath2 -0.0483870f`, etc) was ported with
//   the suffix simply dropped (`-0.0483870`) - this dialect's `float` is
//   already the only non-int numeric type, so no suffix is needed or
//   accepted.
// - `floor()`/`cos()` used exactly as real upstream does (this dialect's
//   own `math.h`, already globally included by `main.c`, provides both -
//   confirmed directly, not assumed). Real upstream's own `/8.0` (a
//   `double` literal) became `/8.0` throughout (no `double` distinct from
//   `float` in this dialect).
// - TITLE SCREEN: real upstream's own blocking `gb.titleScreen(F("v1.0"),
//   Logo)` (called once in `setup()`, and a SECOND time - a real, genuine
//   re-invocation of `gb.begin()`+`gb.titleScreen()` together - from
//   `loop()`'s own Mode==1 map-select screen whenever Button C is pressed)
//   became a `bbbTitleActive` bool state, dismissed by a genuine
//   `gbPressed(BTN_A)`, matching every other ported title screen in this
//   project (gamePong.c's own header comment has the original worked
//   example). Calling `gbBegin()` a second time mid-session (as real
//   upstream's own C-press handler does) would incorrectly reset this
//   shim's own frame counter/font/frame-rate state, which this project's
//   own multi-game-per-session cartridge model needs to stay intact across
//   an in-game "return to title" - so both real title-screen entry points
//   (initial launch, and the mid-game Button-C one) instead just set the
//   same `bbbTitleActive` flag, matching this project's own already-
//   established "blocking call -> explicit resumable state" precedent
//   without a second real `gbBegin()` call.
//
// SOUND: a direct, byte-for-byte port of real upstream's own `sfx(fxno,
// channel)`/`soundfx[8][8]` - `bbbSfx(fxno, channel)` issues the same 4
// `gbSoundCommand()` calls (volume/instrument/slide/arpeggio) against the
// same `bbbSoundFx[fxno][]` fields, in the same order, before a final
// `gbPlayNoteChannel(pitch, duration, channel)` - every one of upstream's
// own 11 real call sites was restored with its own real, literal channel
// argument (0-3) rather than always defaulting to channel 0.
//
// EEPROM: real upstream's own single-byte save (`byte LevelsUnlock`) is
// read once in `setup()` via a bare `EEPROM.read(EEPROM_SAVE_START+0)` -
// with NO fresh-cell check anywhere in the real source. Traced through what
// real upstream's own logic actually does with a genuinely fresh/erased
// cartridge's raw 255 sentinel: `LevelsUnlock` gates the map-select
// screen's own unlock check, `if(MapCursor <= LevelsUnlock)` - since
// `MapCursor` only ever ranges 0-12, a raw 255 sentinel makes this
// condition ALWAYS true, i.e. a genuinely fresh cartridge would show every
// single one of the 13 levels as already unlocked, not just the intended
// first one. Real upstream's own logic does NOT tolerate the raw 255
// sentinel safely (the same class of bug as this project's own already-
// documented Skibuino EEPROM narrowing bug, just inverted - over-permissive
// instead of save-blocking) - fixed by adding this project's own
// established explicit `==0xFF` fresh-cell check in `gameBigBlackBox_init()`,
// resetting to 0 (only the first level unlocked) exactly like every other
// EEPROM-consuming game in this project already does for its own first-ever
// save. `EEPROM.update(EEPROM_SAVE_START+0, LevelsUnlock)` -> a direct
// `eeprom_update_byte()` call, unchanged otherwise.
//
// REAL UPSTREAM QUIRKS/BUGS PRESERVED DELIBERATELY (none of them crash or
// make the port unplayable, so none were "fixed"):
// - **The "land" sound effect can never actually play.** Real `loop()`
//   resets `GroundedDown = false` THEN immediately checks
//   `if(last==false && GroundedDown==true) sfx(3,3);` three lines later,
//   before `CheckForCollider()` (which is the only thing that could set
//   `GroundedDown` back to true) has even run yet - so `GroundedDown` is
//   always still `false` at the moment of that check, making the condition
//   permanently unreachable. Preserved exactly (`bbbLast`/`bbbGroundedDown`
//   reset-then-check order matches real upstream line-for-line) - the
//   `land` entry in `bbbSoundFx[]` is real, correctly-portable data, it
//   just genuinely never fires on real hardware either.
// - **A real X-axis wall-collision double-flag bug.** Real
//   `CheckForCollider()`'s own second `for(xd...) if(ColX[xd]==1){...
//   GroundedRight=true;}` consequence loop sits OUTSIDE both the
//   `if(VelocityX>0)`/`if(VelocityX<0)` blocks above it (a real upstream
//   brace-placement mistake, confirmed by reading the raw real source
//   directly) - so colliding with a wall while moving in EITHER direction
//   sets BOTH `GroundedLeft` and `GroundedRight` true in the same tick, not
//   just the one that's actually correct for that collision. Traced the
//   real net effect by hand: it's harmless in practice, because the two
//   competing `if(pressed(A) && GroundedRight/GroundedLeft && ...)`
//   wall-jump branches in `loop()` are separate `if`s (not `else if`), and
//   the correct one (`GroundedLeft`, for a left-wall collision) always
//   runs SECOND and overwrites the incorrect one's `VelocityX` assignment -
//   the only observable artifact is the walljump sound effect firing
//   twice in the same tick instead of once. Ported with the exact same
//   brace placement (the consequence loop sits outside both `if`s here
//   too), preserving this quirk bit-for-bit rather than "fixing" it into
//   different, never-actually-shipped behavior.
// - **`EnnemieAI::update()`'s own real same-tick direction-reversal
//   quirk.** Real upstream checks `if(GoRight){...}` then, as a SEPARATE
//   (not `else if`) `if(!GoRight){...}` immediately after - so an enemy
//   that hits a wall and calls `ChangeDirection()` inside the first block
//   has its `GoRight` flag already flipped by the time the second `if`
//   evaluates, and can immediately move one step in the new direction
//   THIS SAME TICK (not just reverse facing with no movement). Ported with
//   the same two independent `if`s (not `else if`) in `bbbEnnemieUpdate()`.
// - **The `MapCursor-1>=0`/arrow-hiding real byte-wraparound near-miss.**
//   Real `byte MapCursor` underflowing past 0 wraps to 255 on real
//   hardware, making `MapCursor-1>=0` an always-true dead check (the left
//   arrow would never actually hide at MapCursor==0, and pressing LEFT
//   there would wrap MapCursor to 255 and read real out-of-bounds map-
//   preview data) - a real, if obscure, upstream bug. This dialect's own
//   non-wrapping `int` MapCursor makes this check behave as a genuine,
//   correct bounds check instead (the left arrow correctly hides at
//   cursor 0, and LEFT does nothing there) - the same already-documented,
//   accepted `avrCompat.h`-driven divergence category this project's own
//   CLAUDE.md describes generally, not a deliberate content change, and
//   not something worth deliberately un-fixing by hand-rolling a wraparound
//   (which would just reproduce a real out-of-bounds memory read, actively
//   undesirable here).
// - Real `getTile()` indexes the currently-loaded map via `MapCursor`, not
//   `CurrentLoadedMap` (a separate global) - confirmed harmless: the only
//   place `CurrentLoadedMap` is ever set to something other than
//   `MapCursor`'s own current value is the instant a level is chosen
//   (`CurrentLoadedMap = MapCursor;`), and `MapCursor` is never touched
//   again while actually playing (the only code that changes it lives in
//   the mutually-exclusive Mode==1 map-select branch) - so the two are
//   always equal throughout real gameplay. Ported literally
//   (`bbbGetTile()` also indexes via `bbbMapCursor`).
//
// Verified via a clean compile (`compile src/main.c -o obj/main.asm`, zero
// errors/warnings) - see this port's own final report for the full
// collision/EEPROM/shift-audit trace.

#define BBB_MAX_ENNEMIES 8
#define BBB_MAX_KEYS 8
#define BBB_MAX_LOCKERS 8
#define BBB_EEPROM_SAVE_START 16

#define BBB_BOUNCY_MATH1 0.0322580
#define BBB_BOUNCY_MATH2 -0.0483870
#define BBB_BOUNCY_MATH3 -0.0393700
#define BBB_BOUNCY_MATH4 0.0236220

// -----------------------------------------------------------------------------
//   Bitmap / level data (ported verbatim from real upstream - see this
//   file's own header comment for exactly how binary literals were
//   converted and byte counts verified)
// -----------------------------------------------------------------------------

// ---- Sprite bitmaps (tile IDs 0-51, sprites[] order) ----
int bbbEmpty[10]=
{
    8, 8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

int bbbBrick[10]=
{
    8, 8, 0xff, 0x21, 0xff, 0x88, 0xff, 0x21, 0xff, 0x88,
};

int bbbSharpBrick[10]=
{
    8, 8, 0xff, 0xc3, 0xa5, 0x99, 0x99, 0xa5, 0xc3, 0xff,
};

int bbbTowerBrick[10]=
{
    8, 8, 0xad, 0xb5, 0xad, 0xb5, 0xad, 0xb5, 0xad, 0xb5,
};

int bbbPiston[10]=
{
    8, 8, 0xff, 0x81, 0xdb, 0xdb, 0x3c, 0xe7, 0x81, 0xff,
};

int bbbHolder[10]=
{
    8, 8, 0xff, 0xa5, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0,
};

int bbbTeleporter0[10]=
{
    8, 8, 0xff, 0xd5, 0x6b, 0x35, 0x35, 0x6b, 0xd5, 0xff,
};

int bbbTeleporter1[10]=
{
    8, 8, 0xff, 0xab, 0xd6, 0xac, 0xac, 0xd6, 0xab, 0xff,
};

int bbbWall[10]=
{
    8, 8, 0xf0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xf0,
};

int bbbEnnemie[10]=
{
    8, 8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

int bbbGroundLeft[10]=
{
    8, 8, 0xff, 0xb9, 0xa6, 0xb9, 0xa6, 0xb9, 0xa6, 0xb9,
};

int bbbGroundRight[10]=
{
    8, 8, 0xff, 0x9d, 0x65, 0x9d, 0x65, 0x9d, 0x65, 0x9d,
};

int bbbGroundMiddle[10]=
{
    8, 8, 0xff, 0x99, 0x66, 0x99, 0x66, 0x99, 0x66, 0x99,
};

int bbbWindow[10]=
{
    8, 8, 0xff, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xff,
};

int bbbTable[10]=
{
    8, 8, 0xff, 0x81, 0xbd, 0xa5, 0xa5, 0xa5, 0xa5, 0xe7,
};

int bbbPistonExtension[10]=
{
    8, 8, 0xe7, 0x99, 0xe7, 0x99, 0xe7, 0x99, 0xe7, 0x99,
};

int bbbArrow1[10]=
{
    8, 8, 0xfe, 0x94, 0xb8, 0xfc, 0xbe, 0xdf, 0x8f, 0x6,
};

int bbbArrow2[10]=
{
    8, 8, 0x7f, 0x2f, 0x17, 0x2f, 0x5f, 0xbb, 0xf1, 0x60,
};

int bbbPlantPot[10]=
{
    8, 8, 0x0, 0x28, 0x14, 0x8, 0x10, 0x3c, 0x3c, 0x18,
};

int bbbKey[10]=
{
    8, 8, 0x0, 0x6, 0xf, 0xfd, 0xaf, 0x6, 0x0, 0x0,
};

int bbbLockedBlock[10]=
{
    8, 8, 0x7e, 0xdf, 0xa3, 0xa3, 0xf7, 0xb7, 0xf7, 0x7e,
};

int bbbSpike[10]=
{
    8, 8, 0x0, 0x22, 0x22, 0x55, 0x55, 0x88, 0xaa, 0xaa,
};

int bbbEnd[10]=
{
    8, 8, 0x0, 0x33, 0xcc, 0x0, 0x0, 0x0, 0x0, 0x0,
};

int bbbConveyerBeltR[10]=
{
    8, 8, 0x7e, 0xa3, 0xc5, 0x7e, 0x0, 0x0, 0x0, 0x0,
};

int bbbConveyerBeltL[10]=
{
    8, 8, 0x7e, 0xc5, 0xa3, 0x7e, 0x0, 0x0, 0x0, 0x0,
};

int bbbTowerBrickHat[10]=
{
    8, 8, 0x0, 0x0, 0x0, 0xff, 0x81, 0xff, 0xad, 0xb5,
};

int bbbTrampoline[10]=
{
    8, 8, 0xff, 0x24, 0x42, 0x81, 0x42, 0x24, 0xff, 0xff,
};

int bbbAntiPiston[10]=
{
    8, 8, 0xff, 0x81, 0xc3, 0x24, 0x3c, 0x42, 0x81, 0xc3,
};

int bbbMissing[10]=
{
    8, 8, 0xaa, 0x1, 0x80, 0x1, 0x80, 0x1, 0x80, 0x55,
};

int bbbPotion[10]=
{
    8, 8, 0x3c, 0x18, 0x18, 0x3c, 0x6e, 0xe7, 0xc3, 0x7e,
};

int bbbBubble[10]=
{
    8, 8, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x10, 0x8,
};

int bbbPipe0[10]=
{
    8, 8, 0x0, 0x0, 0xf, 0x10, 0x20, 0x23, 0x24, 0x24,
};

int bbbPipe1[10]=
{
    8, 8, 0x0, 0x0, 0xf0, 0x8, 0x4, 0xc4, 0x24, 0x24,
};

int bbbPipe2[10]=
{
    8, 8, 0x24, 0x24, 0x23, 0x20, 0x10, 0xf, 0x0, 0x0,
};

int bbbPipe3[10]=
{
    8, 8, 0x24, 0x24, 0xc4, 0x4, 0x8, 0xf0, 0x0, 0x0,
};

int bbbPipe4[10]=
{
    8, 8, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
};

int bbbPipe5[10]=
{
    8, 8, 0x0, 0x0, 0xff, 0x0, 0x0, 0xff, 0x0, 0x0,
};

int bbbStock0[10]=
{
    8, 8, 0x42, 0x81, 0x0, 0x99, 0x66, 0x0, 0xff, 0x0,
};

int bbbStock1[10]=
{
    8, 8, 0x0, 0xff, 0x0, 0x99, 0x66, 0x0, 0xff, 0x0,
};

int bbbStock2[10]=
{
    8, 8, 0x0, 0x7f, 0x80, 0x99, 0xe6, 0x80, 0x7f, 0x0,
};

int bbbStock3[10]=
{
    8, 8, 0x0, 0xfe, 0x1, 0x99, 0x67, 0x1, 0xfe, 0x0,
};

int bbbController0[10]=
{
    8, 8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xe0,
};

int bbbController1[10]=
{
    8, 8, 0xb0, 0xa8, 0xac, 0xaa, 0xa9, 0xff, 0x81, 0xff,
};

int bbbCrate[10]=
{
    8, 8, 0x0, 0x0, 0x7e, 0x42, 0x76, 0x6e, 0x42, 0x7e,
};

int bbbExperiment0[10]=
{
    8, 8, 0xf, 0x10, 0x13, 0x1c, 0x10, 0x10, 0x10, 0x17,
};

int bbbExperiment1[10]=
{
    8, 8, 0xf0, 0x8, 0x8, 0xc8, 0x38, 0x8, 0x8, 0xe8,
};

int bbbExperiment2[10]=
{
    8, 8, 0x13, 0x13, 0xf7, 0x7, 0x7, 0xf6, 0x10, 0xf,
};

int bbbExperiment3[10]=
{
    8, 8, 0x68, 0x68, 0xe8, 0xe8, 0xe8, 0x68, 0x8, 0xf0,
};

int bbbExperiment4[10]=
{
    8, 8, 0xf, 0x10, 0x13, 0x1c, 0x10, 0x17, 0x17, 0x15,
};

int bbbExperiment5[10]=
{
    8, 8, 0xfc, 0x2, 0x32, 0xce, 0x2, 0xfa, 0xfa, 0xba,
};

int bbbExperiment6[10]=
{
    8, 8, 0x15, 0x17, 0xf7, 0x7, 0x7, 0xf0, 0x10, 0x1f,
};

int bbbExperiment7[10]=
{
    8, 8, 0xba, 0xfa, 0xfa, 0xfa, 0xfa, 0x2, 0x2, 0xfc,
};

// ---- Extra sprites (not part of the tile set) ----
int bbbLogo[242]=
{
    64, 30, 0xAD, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xB5, 0x0, 0x0, 0x0, 0x77, 0x70, 0x0, 0x0, 0xAD, 0x0,
    0x0, 0x0, 0x52, 0x40, 0x0, 0x0, 0xB5, 0x0, 0x0, 0x0,
    0x62, 0x50, 0x0, 0x0, 0xAD, 0x0, 0x0, 0x0, 0x52, 0x50,
    0x0, 0x0, 0xB5, 0x0, 0x0, 0x0, 0x77, 0x70, 0x0, 0x0,
    0xAD, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xB5, 0x0,
    0x0, 0x0, 0x74, 0x27, 0x50, 0x0, 0xAD, 0x0, 0x0, 0x0,
    0x54, 0x54, 0x50, 0x0, 0xB5, 0x0, 0x0, 0x0, 0x64, 0x74,
    0x60, 0x0, 0xAD, 0x0, 0x0, 0x0, 0x54, 0x54, 0x50, 0x0,
    0xB5, 0x0, 0x0, 0x0, 0x77, 0x57, 0x50, 0x0, 0xAD, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xB5, 0x0, 0x0, 0x0,
    0x77, 0x50, 0x0, 0x0, 0xAD, 0x0, 0xF, 0xF0, 0x55, 0x50,
    0x0, 0x0, 0xB5, 0x0, 0xF, 0xF0, 0x65, 0x20, 0x20, 0x0,
    0xAD, 0x0, 0xD, 0xD0, 0x55, 0x50, 0x0, 0x0, 0xB5, 0x0,
    0xD, 0xD0, 0x77, 0x50, 0x40, 0x0, 0xAD, 0x0, 0xF, 0xF0,
    0x0, 0x0, 0x20, 0x0, 0xB5, 0x0, 0xF, 0xF0, 0xFF, 0x0,
    0xF0, 0x0, 0xAD, 0x0, 0xF, 0xF0, 0x81, 0x0, 0x60, 0x0,
    0xB5, 0x0, 0xF, 0xF0, 0xBD, 0x0, 0x60, 0x0, 0xFF, 0xFF,
    0xFF, 0xFF, 0xA5, 0x0, 0xF0, 0x0, 0xC3, 0xA6, 0x66, 0x65,
    0xA5, 0x1, 0xB8, 0x0, 0xA5, 0xB9, 0x99, 0x9D, 0xA5, 0x3,
    0x9C, 0x0, 0x99, 0xA6, 0x66, 0x65, 0xA5, 0x3, 0xC, 0x0,
    0x99, 0xB9, 0x99, 0x9D, 0xA5, 0x1, 0xF8, 0x0, 0xA5, 0xA6,
    0x66, 0x65, 0xA5, 0x0, 0x0, 0x0, 0xC3, 0xB9, 0x99, 0x9D,
    0xA5, 0x0, 0x0, 0x0, 0xFF, 0xA6, 0x66, 0x65, 0xE7, 0x0,
    0x0, 0x0,
};

int bbbExclPoint[10]=
{
    8, 8, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x0, 0x80,
};

int bbbWhitePotion[10]=
{
    8, 8, 0x3c, 0x18, 0x18, 0x6e, 0x85, 0xa1, 0x89, 0x7e,
};

int bbbPlayerSprite[10]=
{
    8, 8, 0xff, 0xff, 0xed, 0xed, 0xff, 0xff, 0xff, 0xff,
};

int bbbEArrow1[10]=
{
    8, 8, 0x0, 0x40, 0x60, 0x70, 0x70, 0x60, 0x40, 0x0,
};

int bbbEArrow2[10]=
{
    8, 8, 0x0, 0x2, 0x6, 0xe, 0xe, 0x6, 0x2, 0x0,
};

int bbbELocked[10]=
{
    8, 8, 0x0, 0x30, 0x48, 0x78, 0x78, 0x78, 0x0, 0x0,
};

int bbbEUnLocked[10]=
{
    8, 8, 0x0, 0x6, 0x9, 0x78, 0x78, 0x78, 0x0, 0x0,
};

int bbbEnnemieSprite[10]=
{
    8, 8, 0xff, 0x81, 0x93, 0x93, 0x81, 0x81, 0x81, 0xff,
};

int bbbEnnemieSprite1[10]=
{
    8, 8, 0xff, 0x81, 0xc9, 0xc9, 0x81, 0x81, 0x81, 0xff,
};
// ---- Map preview thumbnails (3x3, used on the map-select screen) ----
int bbbMap0Preview[9]=
{
    2, 0, 0,
    3, 0, 9,
    2, 1, 1,
};

int bbbMap1Preview[9]=
{
    2, 0, 0,
    3, 0, 2,
    0, 0, 3,
};

int bbbMap2Preview[9]=
{
    0, 0, 2,
    19, 0, 20,
    10, 12, 11,
};

int bbbMap3Preview[9]=
{
    0, 0, 0,
    23, 0, 9,
    2, 26, 2,
};

int bbbMap4Preview[9]=
{
    21, 0, 21,
    3, 0, 3,
    3, 26, 3,
};

int bbbMap5Preview[9]=
{
    2, 1, 2,
    15, 0, 28,
    27, 1, 4,
};

int bbbMap6Preview[9]=
{
    0, 0, 21,
    0, 0, 2,
    26, 0, 3,
};

int bbbMap7Preview[9]=
{
    0, 0, 0,
    0, 9, 0,
    21, 21, 21,
};

int bbbMap8Preview[9]=
{
    0, 0, 0,
    0, 9, 0,
    10, 12, 11,
};

int bbbMap9Preview[9]=
{
    0, 30, 0,
    0, 29, 0,
    0, 14, 0,
};

int bbbMap10Preview[9]=
{
    0, 35, 0,
    0, 35, 0,
    39, 37, 40,
};

int bbbMap11Preview[9]=
{
    35, 30, 0,
    33, 34, 0,
    23, 23, 23,
};

int bbbMap12Preview[9]=
{
    0, 48, 49,
    31, 50, 51,
    33, 32, 0,
};

// ---- Map tile data (row-major, bbbMapSizeX[i] x bbbMapSizeY[i] tiles used;
// Map7 alone has one extra unused padding row past its real MapSizeY[7]=22,
// harmless - see this file's own header comment) ----
// Map0: 13 x 9 tiles used (117 stored)
int bbbMap0[117]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 1, 2, 5, 5, 0, 0,
    0, 0, 0, 0, 0, 6, 3, 0, 0, 0, 0, 0, 4, 5, 2, 0, 0, 0, 2, 3,
    0, 25, 0, 16, 0, 15, 0, 0, 0, 0, 0, 3, 2, 0, 3, 0, 0, 0, 15, 18,
    0, 0, 0, 0, 3, 3, 0, 3, 0, 17, 0, 15, 14, 0, 0, 0, 0, 3, 3, 0,
    3, 0, 0, 0, 2, 5, 5, 0, 5, 5, 2, 3, 0, 3, 0, 9, 0, 3, 0, 9,
    0, 0, 0, 6, 2, 22, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2,
};

// Map1: 19 x 11 tiles used (209 stored)
int bbbMap1[209]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
    1, 2, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 0,
    0, 0, 0, 0, 0, 0, 0, 8, 0, 2, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0,
    0, 21, 0, 0, 0, 0, 8, 0, 3, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 5,
    5, 5, 5, 0, 0, 0, 0, 2, 0, 0, 5, 5, 5, 0, 3, 2, 0, 0, 0, 0,
    0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 0, 0, 0, 2, 0,
    0, 0, 3, 0, 0, 0, 0, 0, 0, 21, 0, 0, 3, 3, 0, 0, 0, 21, 21, 21,
    21, 3, 0, 0, 21, 0, 0, 0, 2, 0, 0, 3, 3, 0, 0, 0, 2, 2, 2, 2,
    2, 0, 5, 5, 5, 0, 0, 3, 0, 0, 3, 3, 0, 0, 0, 3, 0, 0, 0, 3,
    21, 21, 21, 21, 21, 21, 3, 22, 22, 3, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1,
    1, 1, 1, 1, 1, 2, 1, 1, 2,
};

// Map2: 16 x 12 tiles used (192 stored)
int bbbMap2[192]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
    0, 8, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2, 3, 0, 0, 0, 0, 8, 0, 0,
    0, 0, 0, 3, 0, 0, 0, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
    0, 0, 0, 3, 2, 5, 0, 0, 0, 0, 0, 21, 19, 0, 0, 3, 0, 0, 0, 3,
    3, 0, 0, 0, 9, 0, 21, 2, 5, 0, 0, 2, 0, 0, 0, 3, 3, 1, 1, 1,
    1, 1, 2, 5, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 19, 3, 7, 0, 0, 19, 0, 0, 0, 0, 0, 21, 0, 17,
    0, 2, 1, 2, 2, 1, 1, 1, 1, 2, 0, 5, 0, 2, 5, 0, 0, 3, 0, 3,
    3, 0, 20, 20, 20, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 3, 2, 22, 10, 12,
    11, 2, 10, 12, 11, 2, 21, 21, 21, 2, 0, 2,
};

// Map3: 21 x 19 tiles used (399 stored)
int bbbMap3[399]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 15, 0, 15, 15, 0, 15, 0, 15, 15,
    0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 15, 21, 21, 21, 21, 21, 15,
    0, 4, 0, 16, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1,
    2, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 2, 5, 3, 3, 0, 21, 0, 0, 21, 0, 25, 0, 0, 9, 0,
    0, 0, 21, 0, 0, 0, 0, 0, 3, 3, 0, 5, 5, 5, 5, 0, 3, 23, 23, 23,
    23, 23, 23, 3, 5, 0, 0, 0, 0, 3, 2, 15, 0, 0, 0, 0, 27, 2, 5, 5,
    5, 5, 5, 5, 2, 0, 0, 0, 0, 5, 2, 3, 5, 0, 0, 0, 0, 2, 21, 0,
    0, 0, 0, 0, 21, 0, 0, 0, 0, 0, 0, 3, 3, 4, 0, 0, 0, 0, 1, 2,
    1, 1, 1, 1, 1, 2, 0, 17, 0, 0, 9, 0, 3, 3, 5, 5, 5, 0, 5, 15,
    0, 0, 0, 0, 0, 0, 8, 0, 25, 21, 21, 21, 21, 3, 2, 0, 0, 0, 0, 2,
    0, 25, 0, 0, 0, 0, 19, 8, 0, 2, 1, 2, 5, 5, 2, 3, 0, 24, 24, 24,
    0, 0, 3, 24, 24, 24, 24, 24, 8, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0,
    0, 0, 0, 2, 5, 5, 5, 5, 5, 2, 0, 0, 18, 0, 25, 20, 3, 3, 0, 0,
    0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 14, 0, 3, 22, 3, 2, 1,
    1, 1, 1, 1, 26, 2, 10, 12, 12, 12, 11, 2, 26, 2, 10, 12, 12, 11, 2,
};

// Map4: 12 x 27 tiles used (324 stored)
int bbbMap4[324]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 2, 0,
    0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 3, 0, 0, 0,
    0, 0, 1, 1, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0,
    3, 0, 0, 0, 21, 0, 0, 0, 2, 1, 1, 2, 3, 22, 2, 0, 3, 0, 0, 0,
    0, 0, 0, 3, 3, 2, 2, 0, 3, 21, 0, 0, 21, 0, 0, 3, 3, 0, 0, 0,
    5, 5, 5, 5, 2, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 3,
    3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 2, 3, 0, 21, 21, 21, 21, 21, 21,
    3, 0, 0, 6, 3, 21, 2, 1, 1, 1, 1, 1, 1, 2, 21, 3, 3, 2, 2, 0,
    0, 0, 0, 0, 0, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
    3, 0, 0, 0, 21, 0, 0, 21, 0, 0, 0, 6, 3, 0, 0, 0, 2, 5, 5, 2,
    5, 5, 5, 2, 3, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 2, 0, 0, 0,
    0, 0, 0, 2, 1, 1, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
    3, 0, 0, 0, 21, 0, 0, 0, 0, 0, 0, 3, 3, 0, 21, 21, 3, 0, 0, 21,
    0, 0, 0, 3, 2, 1, 1, 1, 2, 1, 1, 2, 17, 0, 0, 3, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 3,
    3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 1, 1, 1, 2, 21, 21, 21,
    26, 21, 21, 2,
};

// Map5: 16 x 17 tiles used (272 stored)
int bbbMap5[272]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1,
    2, 3, 2, 4, 2, 3, 1, 1, 1, 1, 1, 2, 3, 0, 0, 0, 28, 3, 0, 0,
    0, 15, 0, 0, 0, 0, 0, 3, 3, 0, 8, 0, 28, 20, 0, 0, 0, 15, 0, 0,
    0, 0, 0, 3, 2, 0, 4, 1, 2, 3, 2, 27, 2, 3, 0, 0, 0, 0, 0, 3,
    3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 15, 15, 27, 28, 28, 3, 3, 16, 0, 0,
    0, 0, 0, 0, 0, 3, 0, 0, 15, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0,
    0, 3, 0, 0, 15, 0, 0, 3, 3, 0, 0, 21, 0, 21, 0, 0, 0, 3, 28, 28,
    4, 15, 15, 4, 3, 21, 21, 2, 15, 2, 0, 26, 0, 3, 0, 0, 28, 0, 0, 3,
    3, 5, 5, 5, 0, 5, 5, 5, 5, 3, 0, 0, 28, 0, 0, 3, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 3, 0, 0, 27, 28, 28, 3, 3, 0, 0, 0, 0, 18, 0, 0,
    0, 3, 0, 0, 15, 0, 0, 3, 3, 0, 25, 0, 0, 14, 0, 0, 0, 3, 0, 0,
    15, 0, 0, 3, 3, 0, 2, 5, 5, 2, 5, 20, 5, 2, 1, 1, 1, 2, 28, 3,
    3, 0, 0, 0, 19, 3, 22, 22, 22, 3, 19, 0, 0, 0, 0, 3, 2, 1, 1, 1,
    1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2,
};

// Map6: 21 x 12 tiles used (252 stored)
int bbbMap6[252]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 1, 1, 1, 2, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 21, 21, 0, 0, 0, 0, 0, 0,
    0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0, 0,
    0, 0, 0, 3, 3, 0, 0, 0, 21, 0, 0, 0, 0, 0, 0, 8, 17, 0, 0, 0,
    0, 0, 0, 0, 3, 3, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 8, 0, 21, 0,
    0, 21, 0, 0, 0, 3, 3, 0, 0, 0, 3, 0, 0, 0, 0, 21, 0, 0, 0, 2,
    0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 0, 3, 21, 21, 21, 21, 2, 0, 0, 0,
    3, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 3, 5, 5, 5, 5, 3, 0, 0,
    0, 3, 21, 21, 21, 21, 21, 0, 3, 3, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0,
    0, 0, 3, 5, 5, 5, 5, 2, 0, 3, 3, 0, 26, 0, 3, 0, 0, 0, 0, 3,
    0, 26, 0, 3, 0, 0, 0, 0, 3, 22, 3, 2, 10, 12, 11, 2, 0, 0, 0, 0,
    2, 10, 12, 11, 2, 0, 0, 0, 0, 2, 5, 2,
};

// Map7: 9 x 22 tiles used (207 stored)
int bbbMap7[207]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 4, 1, 1, 1, 1, 1, 2, 3, 0,
    0, 0, 0, 0, 0, 0, 3, 3, 28, 28, 28, 28, 28, 28, 28, 3, 3, 0, 0, 0,
    0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 21, 21, 3, 3, 0, 0, 0, 0, 0,
    5, 5, 3, 3, 21, 0, 0, 0, 0, 0, 0, 3, 3, 5, 21, 0, 0, 0, 0, 16,
    3, 3, 5, 5, 21, 0, 0, 0, 21, 3, 3, 5, 5, 5, 21, 0, 21, 5, 3, 3,
    5, 5, 5, 5, 0, 5, 5, 3, 3, 0, 0, 0, 0, 0, 2, 0, 3, 3, 0, 0,
    0, 0, 0, 3, 0, 3, 3, 0, 21, 21, 21, 21, 2, 0, 3, 3, 0, 5, 5, 5,
    5, 5, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0,
    0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 3, 3, 2, 0, 0, 0, 0, 0, 0, 3,
    3, 3, 0, 9, 0, 0, 0, 0, 3, 3, 2, 21, 21, 21, 21, 2, 22, 3, 2, 1,
    1, 1, 1, 1, 1, 1, 2,
};

// Map8: 19 x 13 tiles used (247 stored)
int bbbMap8[247]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 0,
    0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0,
    0, 0, 19, 3, 0, 0, 0, 20, 0, 0, 0, 0, 0, 25, 20, 3, 3, 0, 5, 5,
    5, 5, 5, 0, 0, 0, 25, 0, 0, 0, 0, 0, 3, 22, 3, 3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5, 5, 5, 2, 3, 0, 0, 0, 25, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 1, 1, 1, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 3, 0, 0, 0,
    25, 0, 9, 0, 25, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 5, 0, 0, 0, 3,
    5, 5, 5, 5, 0, 0, 0, 0, 0, 3, 3, 5, 5, 0, 0, 0, 0, 0, 3, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 9, 0, 0, 0, 0, 3, 19, 0,
    0, 9, 0, 0, 0, 0, 0, 3, 2, 10, 12, 11, 2, 10, 12, 11, 2, 10, 12, 11,
    2, 10, 12, 11, 2, 1, 2,
};

// Map9: 20 x 15 tiles used (300 stored)
int bbbMap9[300]=
{
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00,
    02, 04, 01, 02, 01, 01, 02, 01, 01, 01, 02, 01, 01, 01, 01, 02, 00, 35, 00, 00,
    03, 00, 00, 03, 00, 00, 03, 27, 00, 00, 03, 07, 00, 15, 00, 03, 00, 33, 36, 32,
    03, 00, 00, 03, 00, 00, 02, 00, 00, 00, 02, 05, 00, 15, 00, 03, 00, 30, 00, 35,
    03, 00, 00, 03, 17, 00, 28, 00, 00, 00, 00, 00, 05, 02, 00, 02, 00, 29, 00, 35,
    03, 00, 00, 27, 00, 00, 28, 00, 00, 00, 00, 00, 00, 03, 00, 03, 00, 14, 00, 35,
    03, 00, 00, 03, 00, 00, 02, 00, 00, 21, 00, 00, 00, 03, 00, 02, 01, 01, 02, 35,
    03, 00, 00, 02, 00, 00, 03, 05, 05, 02, 21, 00, 00, 02, 00, 00, 00, 00, 03, 35,
    03, 00, 00, 28, 00, 00, 03, 00, 00, 03, 05, 21, 00, 03, 05, 05, 05, 00, 03, 35,
    03, 00, 00, 28, 00, 00, 03, 32, 00, 03, 05, 05, 00, 03, 00, 00, 00, 00, 02, 35,
    03, 00, 00, 02, 21, 21, 02, 35, 00, 02, 00, 00, 00, 02, 22, 22, 22, 22, 03, 35,
    03, 00, 00, 03, 05, 05, 03, 35, 00, 03, 00, 00, 00, 03, 05, 05, 05, 05, 05, 35,
    03, 00, 00, 03, 00, 00, 03, 35, 00, 03, 00, 00, 00, 03, 00, 00, 31, 36, 36, 34,
    03, 21, 26, 03, 00, 00, 03, 33, 36, 03, 07, 00, 00, 03, 39, 38, 37, 38, 38, 40,
    02, 10, 12, 02, 00, 00, 02, 00, 00, 02, 01, 01, 01, 02, 01, 01, 01, 01, 01, 01,
};

// Map10: 19 x 19 tiles used (361 stored)
int bbbMap10[361]=
{
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 02,
    01, 01, 01, 01, 02, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 03, 00,
    00, 20, 20, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 02, 22, 02,
    01, 01, 02, 00, 00, 00, 00, 00, 00, 00, 00, 00, 30, 00, 19, 00, 00, 02, 00, 00,
    31, 32, 00, 00, 00, 00, 00, 00, 00, 00, 00, 33, 03, 05, 00, 00, 00, 00, 00, 35,
    35, 00, 00, 21, 00, 00, 00, 00, 00, 00, 00, 05, 00, 00, 36, 36, 36, 36, 34, 33,
    36, 36, 02, 00, 9, 00, 00, 02, 00, 00, 00, 00, 00, 00, 00, 39, 38, 38, 40, 00,
    00, 05, 05, 05, 05, 05, 05, 00, 00, 00, 00, 21, 00, 31, 36, 32, 00, 00, 00, 00,
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 02, 00, 35, 39, 37, 38, 38, 40, 00, 00,
    00, 00, 00, 00, 00, 00, 00, 00, 00, 03, 36, 34, 00, 00, 00, 00, 00, 00, 00, 00,
    00, 21, 41, 00, 00, 00, 21, 00, 03, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00,
    02, 42, 00, 9, 00, 02, 00, 03, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 03,
    05, 05, 05, 05, 03, 19, 03, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 03, 00,
    43, 00, 00, 03, 05, 02, 36, 36, 32, 00, 00, 00, 00, 26, 00, 00, 00, 03, 00, 14,
    00, 00, 03, 00, 00, 00, 39, 37, 40, 00, 00, 02, 01, 02, 00, 00, 02, 05, 05, 05,
    05, 02, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00,
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00,
    00, 00, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21,
};

// Map11: 19 x 10 tiles used (190 stored)
int bbbMap11[190]=
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
    0, 0, 0, 0, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 0,
    0, 0, 0, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 22, 8,
    21, 0, 33, 32, 0, 21, 0, 0, 0, 21, 0, 0, 0, 0, 0, 3, 3, 2, 1, 1,
    1, 2, 35, 0, 5, 0, 0, 2, 1, 8, 0, 0, 0, 0, 2, 3, 0, 0, 0, 0,
    0, 35, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0,
    35, 0, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 5, 0, 0, 0, 0, 33,
    36, 34, 21, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 9, 0, 0,
    0, 3, 0, 0, 21, 21, 0, 25, 0, 0, 3, 2, 23, 23, 23, 23, 23, 23, 23, 23,
    2, 23, 23, 23, 23, 23, 2, 21, 21, 2,
};

// Map12: 22 x 23 tiles used (506 stored)
int bbbMap12[506]=
{
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
    0, 4, 0, 2, 3, 0, 5, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 15, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 3, 19, 0, 25, 0, 9, 0, 0,
    8, 0, 0, 15, 0, 0, 0, 3, 2, 1/*1*/, 2, 1, 2, 0, 3, 5, 5, 5, 5, 5,
    5, 5, 8, 0, 0, 2, 28, 28, 28, 3, 0, 0, 0, 0, 3, 0, 3, 0, 0, 0,
    0, 0, 0, 0, 5, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0, 2, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0,
    0, 0, 0, 21, 0, 0, 0, 0, 0, 0, 0, 3, 0, 21, 0, 3, 0, 0, 0, 0,
    3, 0, 21, 0, 0, 3, 0, 0, 21, 21, 0, 25, 0, 3, 0, 2, 0, 3, 0, 0,
    0, 0, 3, 0, 2, 23, 23, 3, 23, 23, 23, 23, 23, 3, 5, 3, 0, 0, 0, 3,
    0, 0, 0, 0, 3, 21, 3, 0, 0, 3, 0, 43, 0, 0, 0, 3, 0, 3, 21, 0,
    21, 3, 2, 1/*1*/, 1, 1, 2, 5, 2, 1, 1, 2, 1, 1, 1, 1, 1, 2, 1, 2,
    2, 0, 2, 2, 3, 35, 0, 0, 35, 0, 0, 35, 0, 0, 0, 0, 0, 33, 32, 3,
    0, 0, 0, 0, 0, 3, 3, 35, 44, 45, 35, 48, 49, 35, 48, 49, 0, 0, 0, 31,
    34, 3, 0, 0, 0, 0, 0, 3, 3, 33, 46, 47, 33, 50, 51, 33, 50, 51, 0, 0,
    39, 37, 40, 3, 0, 21, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 3, 0, 2, 0, 0, 0, 3, 3, 1, 1, 1, 1, 1, 1, 1,
    2, 0, 0, 0, 0, 0, 30, 3, 0, 3, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 33, 3, 0, 3, 0, 0, 0, 3, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 5, 5, 5, 0, 0, 3, 0, 3, 21, 0, 21, 3, 3, 0,
    0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 43, 3, 0, 3, 5, 0, 5, 3,
    3, 0, 0, 29, 0, 0, 0, 41, 0, 0, 0, 0, 0, 0, 5, 2, 0, 3, 0, 0,
    0, 3, 3, 0, 0, 14, 0, 9, 2, 42, 0, 0, 0, 0, 0, 0, 0, 20, 0, 3,
    0, 26, 0, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    1, 2, 1, 1, 1, 2,
};

// ---- Per-level spawn position / map size tables ----
int bbbSpawnCoordX[13]=
{
    10, 2, 7, 9, 2, 7, 2, 4, 1, 2, 3, 1, 1,
};

int bbbSpawnCoordY[13]=
{
    5, 9, 7, 17, 25, 3, 10, 2, 9, 12, 15, 6, 3,
};

int bbbMapSizeX[13]=
{
    13, 19, 16, 21, 12, 16, 21, 9, 19, 20, 19, 19, 22,
};

int bbbMapSizeY[13]=
{
    9, 11, 12, 19, 27, 17, 12, 22, 13, 15, 19, 10, 23,
};

// ---- Teleporter pair tables ----
int bbbTeleporterSerie1X[4]=
{
    12, 0, 11, 10,
};

int bbbTeleporterSerie1Y[4]=
{
    7, 3, 15, 13,
};

int bbbTeleporterSerie2X[4]=
{
    12, 0, 11, 11,
};

int bbbTeleporterSerie2Y[4]=
{
    1, 8, 11, 2,
};
// sprites[] - tile ID -> bitmap pointer (matches sprite_order above 1:1)
int* bbbSprites[52]=
{
    bbbEmpty, bbbBrick, bbbSharpBrick, bbbTowerBrick,
    bbbPiston, bbbHolder, bbbTeleporter0, bbbTeleporter1,
    bbbWall, bbbEnnemie, bbbGroundLeft, bbbGroundRight,
    bbbGroundMiddle, bbbWindow, bbbTable, bbbPistonExtension,
    bbbArrow1, bbbArrow2, bbbPlantPot, bbbKey,
    bbbLockedBlock, bbbSpike, bbbEnd, bbbConveyerBeltR,
    bbbConveyerBeltL, bbbTowerBrickHat, bbbTrampoline, bbbAntiPiston,
    bbbMissing, bbbPotion, bbbBubble, bbbPipe0,
    bbbPipe1, bbbPipe2, bbbPipe3, bbbPipe4,
    bbbPipe5, bbbStock0, bbbStock1, bbbStock2,
    bbbStock3, bbbController0, bbbController1, bbbCrate,
    bbbExperiment0, bbbExperiment1, bbbExperiment2, bbbExperiment3,
    bbbExperiment4, bbbExperiment5, bbbExperiment6, bbbExperiment7,
};

// coltype[] - tile ID -> collider-type dispatch value (real upstream stored
// these as a sloppily-typed `const byte* coltype[52]` pointer array holding
// plain small integer literals, not real bitmap pointers - ported as a plain
// int array of the same literal values, which is all real upstream ever
// actually used them for).
int bbbColType[52]=
{
    0, 1, 1, 1, 9, 2, 3, 4, 5, 0, 1, 1, 1,
    0, 0, 6, 0, 0, 0, 7, 8, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int* bbbGetMap[13]=
{
    bbbMap0, bbbMap1, bbbMap2, bbbMap3, bbbMap4, bbbMap5, bbbMap6, bbbMap7, bbbMap8, bbbMap9, bbbMap10, bbbMap11, bbbMap12,
};

int* bbbGetMapsPreviews[13]=
{
    bbbMap0Preview, bbbMap1Preview, bbbMap2Preview, bbbMap3Preview, bbbMap4Preview, bbbMap5Preview, bbbMap6Preview, bbbMap7Preview, bbbMap8Preview, bbbMap9Preview, bbbMap10Preview, bbbMap11Preview, bbbMap12Preview,
};

// -----------------------------------------------------------------------------
//   Sound - real tracker soundfx table (see this file's own header comment)
// -----------------------------------------------------------------------------

// {waveform, pitch, pitchSlideTarget, pmt, vmt, volSlide, volume, length} -
// a direct, byte-for-byte port of real upstream's own `const int
// soundfx[8][8]` - every field is now used by bbbSfx() below.
int bbbSoundFx[8][8]=
{
    { 1, 27, 90, 2, 7, 7, 3, 7 },  // 0: jump
    { 1, 27, 112, 1, 1, 1, 6, 4 }, // 1: walljump
    { 0, 27, 57, 1, 1, 1, 6, 4 },  // 2: unlockdoor
    { 0, 9, 57, 1, 6, 8, 7, 8 },   // 3: land (see this file's own header comment - never actually fires)
    { 0, 46, 57, 1, 0, 18, 7, 47 },// 4: getkey
    { 0, 38, 79, 3, 6, 7, 7, 20 }, // 5: finish
    { 0, 30, 68, 3, 0, 0, 7, 5 },  // 6: click
    { 0, 30, 55, 1, 7, 0, 7, 15 }, // 7: death
};

// Direct port of real upstream's own `void sfx(int fxno, int channel)` -
// the same 4 gbSoundCommand() calls (volume/waveform/volume-slide/pitch-
// slide) plus a final gbPlayNoteChannel(), in the same order, against the
// same fields.
void bbbSfx( int fxno, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, bbbSoundFx[ fxno ][ 6 ], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, bbbSoundFx[ fxno ][ 0 ], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, bbbSoundFx[ fxno ][ 5 ], -bbbSoundFx[ fxno ][ 4 ], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, bbbSoundFx[ fxno ][ 3 ], bbbSoundFx[ fxno ][ 2 ] - 58, channel );
    gbPlayNoteChannel( bbbSoundFx[ fxno ][ 1 ], bbbSoundFx[ fxno ][ 7 ], channel );
}

// -----------------------------------------------------------------------------
//   Enemy struct (flattened from real class EnnemieAI - see this file's own
//   header comment)
// -----------------------------------------------------------------------------

typedef struct
{
    int posX;
    int posY;
    int progress;
    bool goRight;
} BbbEnnemie;

BbbEnnemie bbbEnnemies[BBB_MAX_ENNEMIES];

// -----------------------------------------------------------------------------
//   Game state (real upstream globals, bbb-prefixed - see this file's own
//   header comment for exactly what was dropped as genuinely dead)
// -----------------------------------------------------------------------------

int bbbCamX;
int bbbCamY;
int bbbFrame;

int bbbMode;

int bbbNbrOfLevel;
int bbbLevelsUnlock;
int bbbMapCursor;

int bbbKeysGot;

int bbbEnnemieCount;

int bbbCurrentLoadedMap;

int bbbScroll;

bool bbbLast;
bool bbbAC;
int bbbACT;
bool bbbPistonPressed;

int bbbTeleporterCount;

int bbbKeyX[BBB_MAX_KEYS];
int bbbKeyY[BBB_MAX_KEYS];
bool bbbKeyGot[BBB_MAX_KEYS];
int bbbKeyCount;

int bbbLockerX[BBB_MAX_LOCKERS];
int bbbLockerY[BBB_MAX_LOCKERS];
bool bbbLockerGot[BBB_MAX_LOCKERS];
int bbbLockerCount;

int bbbMapHeigth;
int bbbMapWidth;

bool bbbGoingRight;

bool bbbGroundedDown;
bool bbbGroundedRight;
bool bbbGroundedLeft;

bool bbbIsPlaying;

// New, this port's own explicit state - see this file's own header comment,
// TITLE SCREEN section.
bool bbbTitleActive;

int bbbCPosX;
int bbbCPosY;

float bbbPPosX;
float bbbPPosY;

int bbbSquisheVertical;
int bbbSquisheHorizontal;

int bbbVelocityX;
int bbbVelocityY;

// -----------------------------------------------------------------------------
//   Small helpers
// -----------------------------------------------------------------------------

int bbbClampInt( int minv, int maxv, int value )
{
    if( value < minv )
        return minv;
    else if( value >= maxv )
        return maxv;
    else
        return value;
}

int bbbGetTile( int x, int y )
{
    // Real upstream indexes via MapCursor, not CurrentLoadedMap - see this
    // file's own header comment for why that's harmless.
    return bbbGetMap[ bbbMapCursor ][ x + y * bbbMapWidth ];
}

int bbbGetPreviewTile( int x, int y )
{
    return bbbGetMapsPreviews[ bbbMapCursor ][ x + y * 3 ];
}

bool bbbInRay( int minV, int maxV, int value )
{
    return value <= maxV && value >= minV;
}

bool bbbInRange( int r, int v )
{
    return bbbInRay( r, r + 8, v + 2 ) || bbbInRay( r, r + 8, v + 6 );
}

// -----------------------------------------------------------------------------
//   Mode transitions (real upstream free functions - defined before
//   bbbPixelInCollider() below, which calls three of them, since this
//   dialect is compiled top-to-bottom with no reliable forward-declaration
//   mechanism - matches gameFiremen.c's own established precedent)
// -----------------------------------------------------------------------------

void bbbLoadCineStart()
{
    bbbFrame = 0;
    bbbCamX = 0;
    bbbCamY = 0;
    bbbMode = 4;
    bbbIsPlaying = false;
}

void bbbLoadCineEnd()
{
    bbbFrame = 0;
    bbbCamX = 0;
    bbbCamY = 0;
    bbbMode = 5;
    bbbIsPlaying = false;
}

void bbbSelectMap()
{
    bbbIsPlaying = false;
    bbbMode = 1;
}

void bbbDie()
{
    bbbIsPlaying = false;
    bbbMode = 2;
    bbbScroll = 0;
}

void bbbUnlockNext()
{
    if( bbbCurrentLoadedMap == bbbLevelsUnlock )
        bbbLevelsUnlock++;

    eeprom_update_byte( BBB_EEPROM_SAVE_START + 0, bbbLevelsUnlock );
    bbbMode = 3;
    bbbIsPlaying = false;
}

// -----------------------------------------------------------------------------
//   Collision (direct port of real PixelInCollider()/CheckForCollider())
// -----------------------------------------------------------------------------

// Real upstream's own `switch(GlobalColiderType)` ported as an if/else-if
// chain (this project's own established caution around this dialect's
// switch support) - case order and every magic number preserved exactly.
int bbbPixelInCollider( int pimx, int pimy, int picx, int picy )
{
    int collType = bbbColType[ bbbGetTile( pimx, pimy ) ];

    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;

    int value = 0;
    int a;
    int b;

    if( collType == 0 )
    {
        value = 1;
    }
    else if( collType == 1 )
    {
        value = 2;
    }
    else if( collType == 2 )
    {
        x1 = 1; y1 = 0; x2 = 6; y2 = 3;
    }
    else if( collType == 3 )
    {
        // Teleporter0
        for( a = 0; a < bbbTeleporterCount; a++ )
        {
            if( bbbTeleporterSerie1X[a] == pimx && bbbTeleporterSerie1Y[a] == pimy )
            {
                bbbPPosX = -( (float)bbbTeleporterSerie2X[a] * 8 ) + 13;
                bbbPPosY = -( (float)bbbTeleporterSerie2Y[a] * 8 ) + 4;
                bbbVelocityX = 63;
                bbbVelocityY = 40;
            }
            if( bbbTeleporterSerie2X[a] == pimx && bbbTeleporterSerie2Y[a] == pimy )
            {
                bbbPPosX = -( (float)bbbTeleporterSerie1X[a] * 8 ) + 13;
                bbbPPosY = -( (float)bbbTeleporterSerie1Y[a] * 8 ) + 4;
                bbbVelocityX = 63;
                bbbVelocityY = 40;
            }
        }
    }
    else if( collType == 4 )
    {
        for( a = 0; a < bbbTeleporterCount; a++ )
        {
            if( bbbTeleporterSerie1X[a] == pimx && bbbTeleporterSerie1Y[a] == pimy )
            {
                bbbPPosX = -( (float)bbbTeleporterSerie2X[a] * 8 ) - 5;
                bbbPPosY = -( (float)bbbTeleporterSerie2Y[a] * 8 ) + 4;
                bbbVelocityX = -63;
                bbbAC = true;
                bbbVelocityY = 40;
            }
            if( bbbTeleporterSerie2X[a] == pimx && bbbTeleporterSerie2Y[a] == pimy )
            {
                bbbPPosX = -( (float)bbbTeleporterSerie1X[a] * 8 ) - 5;
                bbbPPosY = -( (float)bbbTeleporterSerie1Y[a] * 8 ) + 4;
                bbbVelocityX = -63;
                bbbAC = true;
                bbbVelocityY = 40;
            }
        }
    }
    else if( collType == 5 )
    {
        x1 = 0; y1 = 0; x2 = 4; y2 = 8;
    }
    else if( collType == 6 )
    {
        if( bbbPistonPressed )
            value = 2;
        else
            value = 1;
    }
    else if( collType == 7 )
    {
        // The key
        value = 1;
        for( a = 0; a < bbbKeyCount; a++ )
        {
            if( bbbKeyX[a] == pimx && bbbKeyY[a] == pimy )
            {
                if( !bbbKeyGot[a] )
                {
                    bbbSfx( 4, 1 );
                    bbbKeysGot++;
                    bbbKeyGot[a] = true;
                    break;
                }
            }
        }
    }
    else if( collType == 8 )
    {
        // The locker
        if( bbbKeysGot > 0 )
        {
            for( a = 0; a < bbbLockerCount; a++ )
            {
                if( bbbLockerX[a] == pimx && bbbLockerY[a] == pimy )
                {
                    if( bbbLockerGot[a] == false )
                    {
                        bbbSfx( 2, 1 );
                        bbbLockerGot[a] = true;
                        bbbKeysGot--;
                        break;
                    }
                }
            }
        }
        for( b = 0; b < bbbLockerCount; b++ )
        {
            if( bbbLockerX[b] == pimx && bbbLockerY[b] == pimy )
            {
                if( bbbLockerGot[b] )
                    value = 1;
                else
                    value = 2;
                break;
            }
            else
            {
                value = 2;
            }
        }
    }
    else if( collType == 9 )
    {
        x1 = 0; y1 = 0; x2 = 7; y2 = 7;
        bbbPistonPressed = true;
    }
    else if( collType == 10 )
    {
        x1 = 2; y1 = 5; x2 = 7; y2 = 7;
        if( 7 - picx >= x1 && 7 - picx <= x2 && 7 - picy >= y1 && 7 - picy <= y2 )
        {
            bbbSfx( 7, 1 );
            bbbDie();
        }
    }
    else if( collType == 11 )
    {
        bbbSfx( 5, 2 );
        bbbUnlockNext();
    }
    else if( collType == 12 )
    {
        x1 = 0; y1 = 0; x2 = 7; y2 = 4;
        if( 7 - picx >= x1 && 7 - picx <= x2 && 7 - picy >= y1 && 7 - picy <= y2 )
        {
            bbbVelocityX = 30;
            bbbACT = 0;
        }
    }
    else if( collType == 13 )
    {
        x1 = 0; y1 = 0; x2 = 7; y2 = 4;
        if( 7 - picx >= x1 && 7 - picx <= x2 && 7 - picy >= y1 && 7 - picy <= y2 )
        {
            bbbVelocityY = -30;
            bbbACT = 1;
        }
    }
    else if( collType == 14 )
    {
        x1 = 0; y1 = 3; x2 = 7; y2 = 7;
    }
    else if( collType == 15 )
    {
        x1 = 0; y1 = 0; x2 = 7; y2 = 7;
        bbbVelocityY = 110;
        bbbACT = 2;
    }
    else if( collType == 16 )
    {
        x1 = 0; y1 = 0; x2 = 7; y2 = 7;
        bbbPistonPressed = false;
    }
    else if( collType == 17 )
    {
        if( !bbbPistonPressed )
            value = 2;
        else
            value = 1;
    }
    else if( collType == 18 )
    {
        bbbLoadCineEnd();
    }
    else
    {
        value = 1;
    }

    if( value == 0 )
    {
        if( 7 - picx >= x1 && 7 - picx <= x2 && 7 - picy >= y1 && 7 - picy <= y2 )
            value = 2;
        else
            value = 1;
    }
    return value - 1;
}

// Direct port of real CheckForCollider() - including its own real X-axis
// double-flag brace-placement bug, preserved exactly (see this file's own
// header comment).
void bbbCheckForCollider()
{
    if( bbbVelocityX != 0 )
    {
        int colX[8];
        int xd;

        if( bbbVelocityX > 0 )
        {
            for( xd = 0; xd < 8; xd++ )
            {
                colX[xd] = bbbPixelInCollider(
                    (int)( -( floor( ( bbbPPosX + 5.0 + 0 ) / 8.0 ) ) ),
                    (int)( -( floor( ( bbbPPosY - 3.0 + (float)xd ) / 8.0 ) ) ),
                    (int)( -( -( bbbPPosX + 5.0 + 0 ) - ( -( floor( ( bbbPPosX + 5.0 + 0 ) / 8.0 ) ) ) * 8.0 ) ),
                    (int)( -( -( bbbPPosY - 3.0 + (float)xd ) - ( -( floor( ( bbbPPosY - 3.0 + (float)xd ) / 8.0 ) ) ) * 8.0 ) )
                );
            }
            for( xd = 0; xd < 8; xd++ )
            {
                if( colX[xd] == 1 )
                {
                    bbbVelocityX = 0;
                    bbbGroundedLeft = true;
                }
            }
        }

        if( bbbVelocityX < 0 )
        {
            for( xd = 0; xd < 8; xd++ )
            {
                colX[xd] = bbbPixelInCollider(
                    (int)( -( floor( ( bbbPPosX - 4.0 + 0 ) / 8.0 ) ) ),
                    (int)( -( floor( ( bbbPPosY - 3.0 + (float)xd ) / 8.0 ) ) ),
                    (int)( -( -( bbbPPosX - 4.0 + 0 ) - ( -( floor( ( bbbPPosX - 4.0 + 0 ) / 8.0 ) ) ) * 8.0 ) ),
                    (int)( -( -( bbbPPosY - 3.0 + (float)xd ) - ( -( floor( ( bbbPPosY - 3.0 + (float)xd ) / 8.0 ) ) ) * 8.0 ) )
                );
            }
        }
        // Sits outside both if-blocks above - matches real upstream exactly
        // (see this file's own header comment).
        for( xd = 0; xd < 8; xd++ )
        {
            if( colX[xd] == 1 )
            {
                bbbVelocityX = 0;
                bbbGroundedRight = true;
            }
        }
    }

    if( bbbVelocityY != 0 )
    {
        int colY[8];
        int yd;

        if( bbbVelocityY > 0 )
        {
            for( yd = 0; yd < 8; yd++ )
            {
                colY[yd] = bbbPixelInCollider(
                    (int)( -( floor( ( bbbPPosX - 3.0 + (float)yd ) / 8.0 ) ) ),
                    (int)( -( floor( ( bbbPPosY + 5.0 + 0 ) / 8.0 ) ) ),
                    (int)( -( -( bbbPPosX - 3.0 + (float)yd ) - ( -( floor( ( bbbPPosX - 3.0 + (float)yd ) / 8.0 ) ) ) * 8.0 ) ),
                    (int)( -( -( bbbPPosY + 5.0 + 0 ) - ( -( floor( ( bbbPPosY + 5.0 + 0 ) / 8.0 ) ) ) * 8.0 ) )
                );
            }
            for( yd = 0; yd < 8; yd++ )
            {
                if( colY[yd] == 1 )
                    bbbVelocityY = 0;
            }
        }
        if( bbbVelocityY < 0 )
        {
            for( yd = 0; yd < 8; yd++ )
            {
                colY[yd] = bbbPixelInCollider(
                    (int)( -( floor( ( bbbPPosX - 3.0 + (float)yd ) / 8.0 ) ) ),
                    (int)( -( floor( ( bbbPPosY - 4.0 + 0 ) / 8.0 ) ) ),
                    (int)( -( -( bbbPPosX - 3.0 + (float)yd ) - ( -( floor( ( bbbPPosX - 3.0 + (float)yd ) / 8.0 ) ) ) * 8.0 ) ),
                    (int)( -( -( bbbPPosY - 4.0 + 0 ) - ( -( floor( ( bbbPPosY - 4.0 + 0 ) / 8.0 ) ) ) * 8.0 ) )
                );
            }
            for( yd = 0; yd < 8; yd++ )
            {
                if( colY[yd] == 1 )
                {
                    bbbVelocityY = 0;
                    bbbGroundedDown = true;
                }
            }
        }
    }

    int colG[6];
    int gd;
    for( gd = 0; gd < 6; gd++ )
    {
        colG[gd] = bbbPixelInCollider(
            (int)( -( floor( ( bbbPPosX - 2.0 + (float)gd ) / 8.0 ) ) ),
            (int)( -( floor( ( bbbPPosY - 3.0 + 0 ) / 8.0 ) ) ),
            (int)( -( -( bbbPPosX - 2.0 + (float)gd ) - ( -( floor( ( bbbPPosX - 2.0 + (float)gd ) / 8.0 ) ) ) * 8.0 ) ),
            (int)( -( -( bbbPPosY - 3.0 + 0 ) - ( -( floor( ( bbbPPosY - 3.0 + 0 ) / 8.0 ) ) ) * 8.0 ) )
        );
    }
    for( gd = 0; gd < 6; gd++ )
    {
        if( colG[gd] == 1 )
            bbbVelocityY = 30;
    }
}

// -----------------------------------------------------------------------------
//   Enemy AI (direct port of real class EnnemieAI::update() - see this
//   file's own header comment for the preserved same-tick reversal quirk)
// -----------------------------------------------------------------------------

bool bbbEnnemieSimplePixInColl( int pimx, int pimy )
{
    int t = bbbColType[ bbbGetTile( pimx, pimy ) ];

    if( t == 0 )
        return false;
    else if( t == 16 || t == 17 )
        return false;
    else if( t == 15 )
        return false;
    else
        return true;
}

void bbbEnnemieUpdate( int idx )
{
    if( bbbEnnemies[idx].progress > 2 )
    {
        bbbEnnemies[idx].progress = 0;
    }
    else
    {
        bbbEnnemies[idx].progress++;
        if( bbbEnnemies[idx].goRight )
        {
            if( !bbbEnnemieSimplePixInColl(
                    (int)floor( (float)( bbbEnnemies[idx].posX + 5 ) / 8.0 ),
                    (int)floor( (float)( bbbEnnemies[idx].posY + 3 ) / 8.0 ) ) )
            {
                bbbEnnemies[idx].posX++;
            }
            else
            {
                bbbEnnemies[idx].goRight = !bbbEnnemies[idx].goRight;
            }
        }
        if( !bbbEnnemies[idx].goRight )
        {
            if( !bbbEnnemieSimplePixInColl(
                    (int)floor( (float)( bbbEnnemies[idx].posX - 5 ) / 8.0 ),
                    (int)floor( (float)( bbbEnnemies[idx].posY + 3 ) / 8.0 ) ) )
            {
                bbbEnnemies[idx].posX--;
            }
            else
            {
                bbbEnnemies[idx].goRight = !bbbEnnemies[idx].goRight;
            }
        }
    }

    bool pre = false;
    int colY[8];
    int yd;
    for( yd = 0; yd < 8; yd++ )
    {
        colY[yd] = bbbEnnemieSimplePixInColl(
            (int)floor( (float)( bbbEnnemies[idx].posX - 3 + yd ) / 8.0 ),
            (int)floor( (float)( bbbEnnemies[idx].posY + 4 + 0 ) / 8.0 ) );
    }
    for( yd = 0; yd < 8; yd++ )
    {
        if( colY[yd] == 1 )
        {
            pre = true;
            break;
        }
    }
    if( !pre )
        bbbEnnemies[idx].posY++;
}

// -----------------------------------------------------------------------------
//   Drawing / camera
// -----------------------------------------------------------------------------

void bbbDrawPlayer()
{
    if( bbbGoingRight == true )
    {
        gbFillRect( (int)( bbbCPosX - ( bbbPPosX + (float)bbbSquisheHorizontal / 2.0 ) + 8 ),
                    (int)( bbbCPosY - ( bbbPPosY + (float)bbbSquisheVertical / 2.0 ) + 8 ),
                    bbbSquisheHorizontal, bbbSquisheVertical );
        gbSetColorBg( GB_WHITE, GB_WHITE );
        gbDrawFastVLine( (int)( bbbCPosX - ( bbbPPosX + (float)bbbSquisheHorizontal / 2.0 ) + 6 + bbbSquisheHorizontal ),
                          (int)( bbbCPosY - ( bbbPPosY + (float)bbbSquisheVertical / 2.0 ) + 10 ), 2 );
        gbDrawFastVLine( (int)( bbbCPosX - ( bbbPPosX + (float)bbbSquisheHorizontal / 2.0 ) + 3 + bbbSquisheHorizontal ),
                          (int)( bbbCPosY - ( bbbPPosY + (float)bbbSquisheVertical / 2.0 ) + 10 ), 2 );
        gbSetColorBg( GB_BLACK, GB_WHITE );
    }
    else
    {
        gbFillRect( (int)( bbbCPosX - ( bbbPPosX + (float)bbbSquisheHorizontal / 2.0 ) + 8 ),
                    (int)( bbbCPosY - ( bbbPPosY + (float)bbbSquisheVertical / 2.0 ) + 8 ),
                    bbbSquisheHorizontal, bbbSquisheVertical );
        gbSetColorBg( GB_WHITE, GB_WHITE );
        gbDrawFastVLine( (int)( bbbCPosX - ( bbbPPosX + (float)bbbSquisheHorizontal / 2.0 ) + 9 ),
                          (int)( bbbCPosY - ( bbbPPosY + (float)bbbSquisheVertical / 2.0 ) + 10 ), 2 );
        gbDrawFastVLine( (int)( bbbCPosX - ( bbbPPosX + (float)bbbSquisheHorizontal / 2.0 ) + 12 ),
                          (int)( bbbCPosY - ( bbbPPosY + (float)bbbSquisheVertical / 2.0 ) + 10 ), 2 );
        gbSetColorBg( GB_BLACK, GB_WHITE );
    }
}

void bbbClampCamera()
{
    if( bbbMapWidth < 11 )
    {
        bbbCPosX = ( LCDWIDTH - bbbMapWidth * 8 ) / 2;
    }
    else
    {
        bbbCPosX = bbbClampInt( -( 8 * bbbMapWidth - LCDWIDTH ), 0,
                                 (int)( bbbPPosX + (float)( LCDWIDTH / 2 ) - 8.0 ) );
    }
    bbbCPosY = bbbClampInt( -( 8 * bbbMapHeigth - LCDHEIGHT ), -8,
                             (int)( bbbPPosY + (float)( LCDHEIGHT / 2 ) - 8.0 ) );
}

// -----------------------------------------------------------------------------
//   Level loading (direct port of real PrepareMap())
// -----------------------------------------------------------------------------

void bbbPrepareMap()
{
    bbbPPosX = -(float)( 8 * bbbSpawnCoordX[ bbbCurrentLoadedMap ] ) + 4;
    bbbPPosY = -(float)( 8 * bbbSpawnCoordY[ bbbCurrentLoadedMap ] ) + 4;

    bbbVelocityX = 0;
    bbbVelocityY = 0;
    bbbPistonPressed = false;
    bbbIsPlaying = true;
    bbbMapWidth = bbbMapSizeX[ bbbCurrentLoadedMap ];
    bbbMapHeigth = bbbMapSizeY[ bbbCurrentLoadedMap ];

    bbbKeysGot = 0;

    int ennemieC = 0;
    bbbKeyCount = 0;
    bbbLockerCount = 0;

    int x, y;
    for( x = 0; x < bbbMapWidth; x++ )
    {
        for( y = 0; y < bbbMapHeigth; y++ )
        {
            if( bbbGetTile( x, y ) == 9 )
                ennemieC++;
            if( bbbGetTile( x, y ) == 19 )
                bbbKeyCount++;
            if( bbbGetTile( x, y ) == 20 )
                bbbLockerCount++;
        }
    }

    bbbEnnemieCount = ennemieC;

    int q;
    for( q = 0; q < bbbKeyCount; q++ )
        bbbKeyGot[q] = false;
    for( q = 0; q < bbbLockerCount; q++ )
        bbbLockerGot[q] = false;

    int i = 0;
    int a = 0;
    int b = 0;
    for( x = 0; x < bbbMapWidth; x++ )
    {
        for( y = 0; y < bbbMapHeigth; y++ )
        {
            if( bbbGetTile( x, y ) == 9 )
            {
                bbbEnnemies[i].posX = x * 8;
                bbbEnnemies[i].posY = y * 8;
                bbbEnnemies[i].progress = 0;
                bbbEnnemies[i].goRight = true;
                i++;
            }
            if( bbbGetTile( x, y ) == 19 )
            {
                bbbKeyX[a] = x;
                bbbKeyY[a] = y;
                a++;
            }
            if( bbbGetTile( x, y ) == 20 )
            {
                bbbLockerX[b] = x;
                bbbLockerY[b] = y;
                b++;
            }
        }
    }

    if( bbbCurrentLoadedMap == 0 )
        bbbLoadCineStart();
}

// -----------------------------------------------------------------------------
//   Title screen - see this file's own header comment, TITLE SCREEN section
// -----------------------------------------------------------------------------

void bbbUpdateTitle()
{
    gbDrawBitmap( 10, 0, bbbLogo );

    gbFontSize = 1;
    gbCursorX = 26;
    gbCursorY = 33;
    gbPrintString( "v1.0" );

    gbCursorX = 14;
    gbCursorY = 41;
    gbPrintString( "PRESS A TO START" );

    if( gbPressed( BTN_A ) )
        bbbTitleActive = false;
}

// -----------------------------------------------------------------------------
//   Gameplay tick (direct port of real loop()'s own `if(IsPlaying){...}`
//   branch)
// -----------------------------------------------------------------------------

void bbbUpdatePlaying()
{
    bbbGroundedDown = false;
    bbbGroundedRight = false;
    bbbGroundedLeft = false;

    // See this file's own header comment - this check can never actually be
    // true (GroundedDown was just reset to false immediately above), so the
    // "land" sound never fires, matching real upstream exactly.
    if( bbbLast == false && bbbGroundedDown == true )
        bbbSfx( 3, 3 );
    bbbLast = bbbGroundedDown;

    if( !( gbTimeHeld( BTN_RIGHT ) > 0 ) && !( gbTimeHeld( BTN_LEFT ) > 0 ) )
        bbbVelocityX = (int)( (float)bbbVelocityX * 0.6 );

    if( bbbVelocityY - 3 > -127 )
        bbbVelocityY = bbbVelocityY - 3;
    else
        bbbVelocityY = -127;

    if( gbTimeHeld( BTN_RIGHT ) > 0 )
    {
        if( bbbVelocityX - 4 > -30 )
            bbbVelocityX -= 4;
        bbbGoingRight = true;
    }
    if( gbTimeHeld( BTN_LEFT ) > 0 )
    {
        if( bbbVelocityX + 4 < 30 )
            bbbVelocityX += 4;
        bbbGoingRight = false;
    }

    bbbACT = 3;

    bbbCheckForCollider();

    if( gbPressed( BTN_A ) && bbbGroundedDown )
    {
        bbbSfx( 0, 0 );
        bbbVelocityY = 66;
    }

    if( gbPressed( BTN_A ) && bbbGroundedRight && !bbbGroundedDown )
    {
        bbbVelocityX = 60;
        bbbVelocityY = 56;
        bbbSfx( 1, 0 );
    }

    if( gbPressed( BTN_A ) && bbbGroundedLeft && !bbbGroundedDown )
    {
        bbbVelocityX = -60;
        bbbVelocityY = 56;
        bbbSfx( 1, 0 );
    }

    if( gbPressed( BTN_C ) )
    {
        bbbIsPlaying = false;
        bbbSelectMap();
    }

    if( bbbAC == true )
    {
        bbbVelocityX = -63;
        bbbAC = false;
    }
    if( bbbACT == 0 )
        bbbVelocityX = -23;
    else if( bbbACT == 1 )
        bbbVelocityX = 23;
    else if( bbbACT == 2 && gbPressed( BTN_A ) )
        bbbVelocityY = 110;

    bbbPPosX += (float)bbbVelocityX / 127.0 * 3.0;
    bbbPPosY += (float)bbbVelocityY / 127.0 * 3.0;

    if( bbbVelocityY > 0 )
    {
        if( bbbVelocityY > 62 )
        {
            bbbSquisheVertical = 5;
            bbbSquisheHorizontal = 10;
        }
        else
        {
            bbbSquisheVertical = (int)( 8.0 + ( (float)bbbVelocityY * BBB_BOUNCY_MATH2 ) );
            bbbSquisheHorizontal = (int)( 8.0 + ( (float)bbbVelocityY * BBB_BOUNCY_MATH1 ) );
        }
    }
    if( bbbVelocityY < 0 )
    {
        bbbSquisheVertical = (int)( 8.0 + ( (float)bbbVelocityY * BBB_BOUNCY_MATH3 ) );
        bbbSquisheHorizontal = (int)( 8.0 + ( (float)bbbVelocityY * BBB_BOUNCY_MATH4 ) );
    }
    if( bbbVelocityY == 0 )
    {
        bbbSquisheVertical = 8;
        bbbSquisheHorizontal = 8;
    }

    bbbDrawPlayer();
    bbbClampCamera();

    int c;
    for( c = 0; c < bbbEnnemieCount; c++ )
    {
        bbbEnnemieUpdate( c );
        if( bbbEnnemies[c].goRight )
            gbDrawBitmap( bbbCPosX + bbbEnnemies[c].posX - 4, bbbCPosY + bbbEnnemies[c].posY - 4, bbbEnnemieSprite );
        else
            gbDrawBitmap( bbbCPosX + bbbEnnemies[c].posX - 4, bbbCPosY + bbbEnnemies[c].posY - 4, bbbEnnemieSprite1 );

        if( bbbInRange( bbbEnnemies[c].posX - 8, (int)( -bbbPPosX ) ) &&
            bbbInRange( bbbEnnemies[c].posY - 8, (int)( -bbbPPosY ) ) )
        {
            bbbSfx( 7, 1 );
            bbbDie();
        }
    }

    int x, y;
    for( x = 0; x < bbbMapWidth; x++ )
    {
        for( y = 0; y < bbbMapHeigth; y++ )
        {
            int tile = bbbGetTile( x, y );

            if( tile == 15 )
            {
                if( bbbPistonPressed )
                    gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbSprites[15] );
                else
                    gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbMissing );
            }
            else if( tile == 19 )
            {
                int a;
                for( a = 0; a < bbbKeyCount; a++ )
                {
                    if( bbbKeyX[a] == x && bbbKeyY[a] == y )
                    {
                        if( bbbKeyGot[a] == true )
                            gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbEmpty );
                        else
                            gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbSprites[19] );
                        break;
                    }
                    else
                    {
                        if( a >= bbbKeyCount )
                            gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbSprites[19] );
                    }
                }
            }
            else if( tile == 20 )
            {
                int a;
                for( a = 0; a < bbbLockerCount; a++ )
                {
                    if( bbbLockerX[a] == x && bbbLockerY[a] == y )
                    {
                        if( bbbLockerGot[a] )
                            gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbEmpty );
                        else
                            gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbSprites[20] );
                        break;
                    }
                    else
                    {
                        if( a >= bbbLockerCount )
                            gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbSprites[20] );
                    }
                }
            }
            else if( tile == 28 )
            {
                if( !bbbPistonPressed )
                    gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbSprites[15] );
                else
                    gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbMissing );
            }
            else
            {
                gbDrawBitmap( bbbCPosX + x * 8, bbbCPosY + y * 8, bbbSprites[tile] );
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Non-playing modes (direct ports of real loop()'s own Mode==1..5
//   branches)
// -----------------------------------------------------------------------------

void bbbUpdateSelect()
{
    int x, y;
    for( x = 0; x < 3; x++ )
    {
        for( y = 0; y < 3; y++ )
        {
            if( bbbGetPreviewTile( x, y ) == 9 )
                gbDrawBitmap( 30 + x * 8, 12 + y * 8, bbbEnnemieSprite );
            else
                gbDrawBitmap( 30 + x * 8, 12 + y * 8, bbbSprites[ bbbGetPreviewTile( x, y ) ] );
        }
    }

    if( bbbMapCursor <= bbbLevelsUnlock )
    {
        gbDrawBitmap( 0, 0, bbbEUnLocked );
        if( gbPressed( BTN_A ) )
        {
            bbbCurrentLoadedMap = bbbMapCursor;
            bbbIsPlaying = true;
            bbbPrepareMap();
        }
    }
    else
    {
        gbDrawBitmap( 0, 0, bbbELocked );
    }

    if( bbbMapCursor - 1 >= 0 )
    {
        gbDrawBitmap( 0, 20, bbbEArrow2 );
        if( gbPressed( BTN_LEFT ) )
        {
            bbbSfx( 6, 2 );
            bbbMapCursor--;
        }
    }
    if( bbbMapCursor + 1 < bbbNbrOfLevel )
    {
        gbDrawBitmap( 76, 20, bbbEArrow1 );
        if( gbPressed( BTN_RIGHT ) )
        {
            bbbSfx( 6, 2 );
            bbbMapCursor++;
        }
    }

    if( gbPressed( BTN_C ) )
    {
        bbbIsPlaying = false;
        bbbTitleActive = true;
    }
}

void bbbUpdateBlackout()
{
    if( bbbScroll < 13 )
        bbbScroll++;

    gbFontSize = 2;
    gbCursorX = 2;
    gbCursorY = -10 + bbbScroll;
    gbPrintString( "Blackout!" );

    gbFontSize = 1;
    gbCursorX = 2;
    gbCursorY = (int)( 18.0 + (float)bbbScroll * 0.5 );
    gbPrintString( "Press A to continue" );

    if( gbPressed( BTN_A ) )
        bbbSelectMap();
}

void bbbUpdateGoodJob()
{
    if( bbbScroll < 13 )
        bbbScroll++;

    gbFontSize = 2;
    gbCursorX = 2;
    gbCursorY = -10 + bbbScroll;
    gbPrintString( "Good Job!" );

    gbFontSize = 1;
    gbCursorX = 2;
    gbCursorY = (int)( 18.0 + (float)bbbScroll * 0.5 );
    gbPrintString( "Press A to continue" );

    if( gbPressed( BTN_A ) )
        bbbSelectMap();
}

void bbbUpdateCineStart()
{
    if( gbPressed( BTN_A ) )
    {
        bbbIsPlaying = true;
        bbbFrame = 0;
    }

    int i;
    if( bbbFrame < 45 )
    {
        gbDrawBitmap( ( LCDWIDTH / 2 - 4 ) + bbbCamX, ( bbbFrame - 8 + bbbCamY ) - 8, bbbWhitePotion );
        gbDrawBitmap( ( LCDWIDTH / 2 - 4 ) + bbbCamX, ( LCDHEIGHT - 8 + bbbCamY ) - 8, bbbPlayerSprite );
        for( i = 0; i < 14; i++ )
        {
            if( i == 0 || i == 13 )
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbSharpBrick );
            else
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbBrick );
        }
    }
    else if( bbbFrame < 80 )
    {
        gbDrawBitmap( ( LCDWIDTH / 2 - 4 ) + bbbCamX, ( LCDHEIGHT - 8 + bbbCamY ) - 8, bbbEnnemieSprite );
        gbDrawBitmap( -8 + bbbCamX, ( LCDHEIGHT - 8 + bbbCamY ) - 8, bbbPlayerSprite );
        gbDrawBitmap( -8 + bbbCamX + 4, ( LCDHEIGHT - 8 + bbbCamY - 9 ) - 8, bbbExclPoint );
        for( i = 0; i < 14; i++ )
        {
            if( i == 0 || i == 13 )
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbSharpBrick );
            else
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbBrick );
        }
        gbDrawBitmap( -24 - 16 + bbbCamX, LCDHEIGHT - 8, bbbEnd );
        gbDrawBitmap( -24 - 8 + bbbCamX, LCDHEIGHT - 8, bbbEnd );
        bbbCamX++;
    }
    else if( bbbFrame < 110 )
    {
        gbDrawBitmap( ( LCDWIDTH / 2 - 4 ) + bbbCamX, ( LCDHEIGHT - 8 + bbbCamY ) - 8, bbbEnnemieSprite );
        gbDrawBitmapRotated( -8 + bbbCamX, ( LCDHEIGHT - 8 + bbbCamY ) - 8, bbbPlayerSprite, 0, 1 );
        for( i = 0; i < 14; i++ )
        {
            if( i == 0 || i == 13 )
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbSharpBrick );
            else
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbBrick );
        }
        gbDrawBitmap( -24 - 16 + bbbCamX, LCDHEIGHT - 8, bbbEnd );
        gbDrawBitmap( -24 - 8 + bbbCamX, LCDHEIGHT - 8, bbbEnd );
    }
    else
    {
        gbDrawBitmap( ( LCDWIDTH / 2 - 4 ) + bbbCamX, ( LCDHEIGHT - 8 + bbbCamY ) - 8, bbbEnnemieSprite );

        int px = -8 + bbbCamX - (int)( (float)( bbbFrame - 110 ) / (float)( 140 - 110 ) * 30.0 );
        float angleDeg = ( ( (float)bbbFrame - 110.0 ) / ( 140.0 - 110.0 ) ) * 180.0 - 90.0;
        float angleRad = angleDeg * 0.0174533;
        int py = ( LCDHEIGHT - 8 + bbbCamY ) - 8 + (int)( -( cos( angleRad ) * 16.0 ) );
        gbDrawBitmapRotated( px, py, bbbPlayerSprite, 0, 1 );

        for( i = 0; i < 14; i++ )
        {
            if( i == 0 || i == 13 )
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbSharpBrick );
            else
                gbDrawBitmap( -24 + ( i * 8 ) + bbbCamX, LCDHEIGHT - 8, bbbBrick );
        }
        gbDrawBitmap( -24 - 16 + bbbCamX, LCDHEIGHT - 8, bbbEnd );
        gbDrawBitmap( -24 - 8 + bbbCamX, LCDHEIGHT - 8, bbbEnd );
    }

    if( bbbFrame >= 150 )
    {
        bbbIsPlaying = true;
        bbbFrame = 0;
    }
    bbbFrame++;
}

void bbbUpdateCineEnd()
{
    int i;

    if( bbbFrame < 50 )
    {
        for( i = 0; i < 11; i++ )
        {
            if( i == 0 || i == 10 )
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbSharpBrick );
            else
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbBrick );
        }

        gbDrawBitmap( 38 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbTable );

        if( bbbFrame < 25 )
        {
            gbDrawBitmap( 38 + bbbCamX, LCDHEIGHT - 24 + bbbCamY, bbbPotion );
        }
        else
        {
            gbDrawBitmap( (int)( 38.0 + (float)bbbCamX + ( (float)( bbbFrame - 25 ) * 0.2 ) ),
                          (int)( (float)( LCDHEIGHT - 24 + bbbCamY ) - ( (float)( bbbFrame - 25 ) * 0.1 ) ),
                          bbbPotion );
        }

        gbDrawBitmap( (int)( 26.0 + (float)bbbCamX + ( (float)bbbFrame * 0.2 ) ),
                      (int)( (float)( LCDHEIGHT - 16 + bbbCamY ) - ( (float)bbbFrame * 0.2 ) ),
                      bbbPlayerSprite );
        gbDrawBitmapRotated( 50 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbEnnemieSprite, 0, 1 );
    }
    else if( bbbFrame < 90 )
    {
        for( i = 0; i < 11; i++ )
        {
            if( i == 0 || i == 10 )
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbSharpBrick );
            else
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbBrick );
        }

        gbDrawBitmap( 38 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbTable );

        gbDrawBitmap( (int)( 48.0 + (float)bbbCamX + ( (float)( bbbFrame - 90 ) * 0.075 ) ),
                      (int)( (float)( LCDHEIGHT - 16 + bbbCamY ) - ( (float)( bbbFrame - 90 ) * -0.2 ) ),
                      bbbPotion );
        gbDrawBitmap( (int)( 26.0 + (float)bbbCamX + ( (float)( bbbFrame - 90 ) * -0.2 ) ),
                      (int)( (float)( LCDHEIGHT - 14 + bbbCamY ) - ( (float)( bbbFrame - 90 ) * -0.2 ) ),
                      bbbPlayerSprite );
        gbDrawBitmapRotated( 50 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbEnnemieSprite, 0, 1 );
    }
    else if( bbbFrame >= 90 && bbbFrame < 94 )
    {
        gbSetColor( GB_BLACK );
        gbSetColor( GB_INVERT );
        gbFillRect( 0, 0, 84, 48 );

        for( i = 0; i < 11; i++ )
        {
            if( i == 0 || i == 10 )
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbSharpBrick );
            else
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbBrick );
        }
        gbDrawBitmap( 38 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbTable );
        gbDrawBitmap( 26 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbPlayerSprite );
        gbDrawBitmapRotated( 50 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbEnnemieSprite, 0, 1 );
        gbSetColor( GB_BLACK );
    }
    else
    {
        for( i = 0; i < 11; i++ )
        {
            if( i == 0 || i == 10 )
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbSharpBrick );
            else
                gbDrawBitmap( i * 8 + bbbCamX, LCDHEIGHT - 8 + bbbCamY, bbbBrick );
        }
        gbDrawBitmap( 38 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbTable );
        gbDrawBitmap( 26 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbPlayerSprite );
        gbDrawBitmapRotated( 50 + bbbCamX, LCDHEIGHT - 16 + bbbCamY, bbbPlayerSprite, 0, 1 );

        gbFontSize = 2;
        gbCursorX = 2;
        gbCursorY = -10 + 13;
        gbPrintString( "The end." );
    }

    if( bbbFrame >= 500 )
    {
        bbbFrame = 0;
        bbbSelectMap();
    }
    bbbFrame += 3;
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameBigBlackBox_init()
{
    gbBegin();
    gbSetFrameRate( 20 );

    bbbTitleActive = true;
    bbbMode = 0;
    bbbNbrOfLevel = 13;
    bbbIsPlaying = false;
    bbbMapCursor = 0;
    bbbCurrentLoadedMap = 0;
    bbbScroll = 0;
    bbbLast = false;
    bbbAC = false;
    bbbACT = 0;
    bbbPistonPressed = false;
    bbbTeleporterCount = 4;
    bbbGoingRight = true;
    bbbGroundedDown = false;
    bbbGroundedRight = false;
    bbbGroundedLeft = false;
    bbbMapHeigth = 9;
    bbbMapWidth = 13;
    bbbSquisheVertical = 8;
    bbbSquisheHorizontal = 8;
    bbbVelocityX = 0;
    bbbVelocityY = 0;
    bbbKeysGot = 0;
    bbbKeyCount = 0;
    bbbLockerCount = 0;
    bbbEnnemieCount = 0;
    bbbCamX = 0;
    bbbCamY = 0;
    bbbFrame = 0;
    bbbPPosX = 0;
    bbbPPosY = 0;
    bbbCPosX = 0;
    bbbCPosY = 0;

    // See this file's own header comment, EEPROM section, for exactly why
    // this fresh-cell check is needed (real upstream's own logic does not
    // tolerate the raw 255 sentinel safely).
    bbbLevelsUnlock = eeprom_read_byte( BBB_EEPROM_SAVE_START + 0 );
    if( bbbLevelsUnlock == 0xFF )
        bbbLevelsUnlock = 0;
}

void gameBigBlackBox_update()
{
    if( !gbUpdate() ) return;

    if( bbbTitleActive )
    {
        bbbUpdateTitle();
        gbRenderFrame();
        return;
    }

    if( bbbIsPlaying )
    {
        bbbUpdatePlaying();
    }
    else
    {
        if( bbbMode == 0 )
            bbbSelectMap();
        else if( bbbMode == 1 )
            bbbUpdateSelect();
        else if( bbbMode == 2 )
            bbbUpdateBlackout();
        else if( bbbMode == 3 )
            bbbUpdateGoodJob();
        else if( bbbMode == 4 )
            bbbUpdateCineStart();
        else if( bbbMode == 5 )
            bbbUpdateCineEnd();
    }

    gbRenderFrame();
}
