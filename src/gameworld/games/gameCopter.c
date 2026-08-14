#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Copter (Clement83, no license specified - github.com/Clement83/Copter). A
// single-player helicopter side-scroller: fly a chopper over a scrolling
// skyline, shoot enemy vehicles (jeeps/halftracks/tanks) with a
// machine-gun-style burst, dodge their return fire, and land on heliports
// to drop off civilians rescued from the ground below. Confirmed distinct
// from - and NOT sharing any code with - `CopterStrike`'s own separate
// multiplayer codebase elsewhere in `more games/`; this is a genuinely
// different, real single-player game by a different author.
//
// GLOSSARY (upstream identifiers are French; kept as the basis for this
// port's own `copt`-prefixed names rather than translated outright, same
// precedent as gameAgaruino.c's own French-name policy): `bat`/Batiment =
// building/vehicle target, `resc`/Rescape = rescapee (civilian to save),
// `mitraille` = machine-gun fire, `enrayer` = jammed (gun overheat),
// `etat` = state, `cptVictoire`/`cptDeath` = save count / civilian-death
// count, `vieRestant` = lives remaining, `ex`/`exEnn` = enemy explosion.
//
// DIALECT REWRITES (see gamePong.c's own header comment for the general
// "gb.x.y(...) -> gbY(...)" flattening rationale, not repeated per call site
// here):
// - Every real `gb.display.*`/`gb.buttons.*`/`gb.sound.*` call site
//   mechanically rewritten to a flat `gbX(...)` call.
// - `random(N)`/`random(min,max)` -> `arand(N)`/`min + arand(max-min)`.
// - `PROGMEM`/`const byte foo[] PROGMEM = {...}` bitmaps ported as plain
//   `int[N] coptFoo = {...}` arrays (one word per real byte, matching every
//   other bitmap table in this project) - all real sprite/background/title
//   art restored via genuine `gbDrawBitmap()`/a custom rotation helper (see
//   "drawBitmapAngle" below), no placeholders. Upstream's own `Balles`
//   bitmap (an unused debug pixel pattern, referenced only from a line that
//   is itself commented out in the real source: `//gb.display.drawBitmap(
//   0,0,Balles);`) was dropped - genuinely dead data, never drawn on real
//   hardware either.
// - Real `struct`s (`Batiment`/`Missile`/`Rescape`) ported as real named
//   `struct CoptBatiment`/`CoptMissile`/`CoptRescape` types with real
//   `Type[N] name;` array globals and whole-struct assignment (e.g.
//   `coptBat[i] = coptBatInit[i];` below) - proven directly in this project
//   already by gameAgaruino.c's own `struct AgarBall`/`AgarBall[N]
//   agarBalls;`, not a fresh risk. Upstream's own `Copter` struct fields
//   `angle`, `dir`, and `dirSprite` are declared but never actually read or
//   written anywhere in the real source (confirmed by grepping every real
//   `.ino` tab) - genuinely dead fields, dropped rather than ported as inert
//   globals.
// - No `switch` in this dialect - `initGame()`'s own building-type ->
//   starting-life `switch` became an if/else-if chain (`coptInitGame()`
//   below).
// - No `B10000000`-style Arduino binary literals - `drawBitmapAngle()`'s own
//   mask/bitmap bit test became `0x80 >> (i % 8)`.
// - Array declarations throughout are `TYPE[N] name`, verified against
//   every declaration in this file.
// - `gb.pickRandomSeed()` has no call site in this particular upstream
//   game (never called) - nothing to port here.
//
// TITLE SCREEN: upstream's own blocking `gb.titleScreen(title)` (called once
// in `setup()`, and again from `loop()` whenever Button C is pressed mid-
// game, each time re-running `initGame()` right after it returns) became an
// explicit `COPT_STATE_TITLE`/`COPT_STATE_PLAY` state machine, the same
// "blocking call -> explicit resumable state" treatment gamePong.c's own
// header comment documents in full. `coptUpdateTitle()` draws the real
// title bitmap (`titre.ino`'s own `title[]`, 64x36, centered at (10,2)) plus
// a "PRESS A" prompt (this port's own UI text, matching every other ported
// title screen's own convention - real Gamebuino's own titleScreen() prints
// its own such prompt internally, not shown in this game's own source),
// dismissed by a genuine `gbPressed(BTN_A)`, at which point `coptInitGame()`
// runs - mirroring upstream's exact "titleScreen() returns, then
// initGame()" order. One small, deliberate timing simplification: upstream
// calls `gb.setFrameRate(FRAMERATE)` (41) only *after* `goTitleScreen()`
// returns from its very first, `setup()`-time call - meaning the real
// cartridge's very first title-screen wait alone runs at real hardware's
// 20fps default, and every frame after that (every future title screen
// included) runs at 41fps. This port instead sets 41fps once in
// `gameCopter_init()`, before ever drawing the first title screen, so
// *every* title screen (including the very first) runs at the same 41fps
// as gameplay - imperceptible in practice (only affects title-screen input
// polling smoothness for a few frames on the very first boot, never
// gameplay itself) and considerably simpler than modeling a one-time
// frame-rate switch inside the state machine.
//
// SOUND: a direct, byte-for-byte port of upstream's own real
// `playsoundfx(fxno,channel)` - `coptPlaySoundFx()` calls the shim's own
// real tracker-engine primitives (`gbSoundCommand()` for volume/
// instrument/slide/arpeggio, `gbPlayNoteChannel()` for the note itself),
// matching upstream's own exact call order/argument shape one-for-one.
// `coptSoundFx[3][8]` is upstream's own real `soundfx[3][8]` table verified
// byte-for-byte. All 3 real call sites (Ennemies.ino's own building-
// destroyed explosion, Player.ino's own machine-gun burst, Rescaper.ino's
// own civilian-pickup chime) are restored with upstream's own exact
// fxno/channel arguments (channel always 0, matching upstream).
//
// REAL UPSTREAM QUIRKS FOUND, AND WHAT WAS DONE ABOUT EACH:
//
// 1. THE CAMERA/DRAWING COORDINATE SPLIT (preserved exactly - real,
//    load-bearing gameplay, not a bug). Every world object (buildings,
//    missiles, rescapees) is drawn at `screenX = worldX - player.x`, while
//    the player's own helicopter sprite is drawn at a SEPARATE screen
//    position, `player.offsetCam` (animated between 20 and 65 depending on
//    which way it's facing/turning) - the two are only made consistent by
//    collision checks that explicitly add `offsetCam` back in
//    (`player.x + player.offsetCam`) when comparing against a world
//    position. This is a deliberate "look-ahead camera" effect (turning to
//    aim right slides the copter's own on-screen icon toward the LEFT edge,
//    revealing more world ahead of it, while `player.x` itself keeps
//    advancing at the same rate so the world doesn't visibly jump) -
//    ported verbatim, every `dist = worldX - player.x` / `player.x +
//    player.offsetCam` site kept exactly as upstream wrote it.
// 2. TWO "ANTI INTEGER-NEGATIVE-BUG" SENTINEL BUILDING ENTRIES (preserved
//    exactly, upstream's own words: "ennemie anti bug de l'integer
//    negatif"). The real building table's first and last rows are
//    `{60,500,48,...}` / `{60,6000,48,...}` - `type=60` still satisfies
//    `type>49` in `coptUpdateBatiment()`/`coptDrawEnnemies()` just like any
//    real attack building (no special-case branch exists anywhere for
//    `type==60`), so these two act as genuine (if minor) extra enemies deep
//    into the level rather than truly inert padding - kept precisely as
//    upstream shipped them, not "cleaned up".
// 3. HELIPORT LIFE OVERWRITTEN ON EVERY (RE)START (preserved exactly). The
//    building table's own heliport rows list `life=10`, but
//    `coptInitGame()`'s own real if/else-if chain (ported 1:1 from
//    upstream's `switch`) only special-cases types 50-54 and falls through
//    to `life=127` for anything else - including heliports (`type==0`) and
//    the two sentinel rows above. The table's own `life=10` for heliports
//    is therefore always dead-on-arrival, overwritten before play ever
//    starts, every single time. Confirmed intentional-enough to preserve
//    rather than "fix": heliports are never fired upon in this game's own
//    logic (they only match the separate `type==0` branch, which does
//    landing/rescue-dropoff, never damage), so their own `life` field is
//    inert either way - preserved for byte-for-byte behavioral fidelity,
//    not because it's load-bearing.
// 4. PARTIAL STATE RESET ON A BUTTON-C MID-GAME RESTART (preserved
//    exactly). Upstream's own `initGame()` - called every time, both on the
//    very first boot and every subsequent Button-C-triggered restart -
//    only resets: player hp/x/etat/offsetCam, cptVictoire/cptDeath/
//    vieRestant, every building's own `life` (not `posX`/`posY`), every
//    missile's own `timerAlive` (not damage/velocity/position), and every
//    rescapee's full state. Left untouched across a restart: player vx/vy/
//    angleSprite/mitraille/isEnrayer/nbClient/timeMitraille/timeRegenere,
//    the crash timer, the double-click timer, and - notably - every
//    building's own current `posX`/`posY` (so a type-50 jeep that had
//    drifted sideways while chasing the player, or any building mid-way
//    through its own "rise up from below the horizon" animation, stays
//    exactly where it was into the next attempt). This is ported exactly
//    as-is via `coptInitGame()` below, called with nothing extra whenever
//    the title screen's own "PRESS A" is pressed again mid-cartridge-
//    session. See point 5 for the one place this project's own menu
//    structure required something upstream never needed.
// 5. A GENUINE ADDITION (NOT UPSTREAM): full state zeroing specifically in
//    `gameCopter_init()`, separate from `coptInitGame()`. Real hardware
//    only ever runs `setup()` once per physical power-on, so upstream never
//    needed to worry about any of the fields point 4 lists surviving
//    between "sessions" - there is only ever one. This project's own menu
//    can relaunch the same compiled game object repeatedly within one
//    running Vircon32 session, so without an explicit full reset, a value
//    like a leftover building `posX` drift or a stale crash timer from a
//    *previous* playthrough could silently carry into a freshly-launched
//    one. `gameCopter_init()` (called once per real menu launch) therefore
//    explicitly zeroes every field `coptInitGame()` itself does not touch,
//    and additionally restores the entire building table from a pristine
//    `coptBatInit[]` copy (whole-struct assignment - see gameAgaruino.c's
//    own already-proven struct-array precedent) before calling
//    `coptInitGame()` - together genuinely reproducing real hardware's own
//    true cold-boot state on every fresh launch, while leaving the
//    Button-C mid-session restart path (which never calls
//    `gameCopter_init()` again) exactly as upstream quirk-for-quirk.
//
// TWO VIRCON32-SPECIFIC DEFENSIVE ADDITIONS (both purely to prevent a real
// engine-level crash on this specific target that upstream's own AVR target
// cannot suffer the same way - neither changes any visible gameplay in the
// normal, reachable play range):
// - `coptDrawWorld()`'s own background-tile index (`((int)player.x/42)%8`,
//   then `+i`, then `%8` again) is real signed-int arithmetic here, unlike
//   upstream's own `uint8_t index`, which would silently *wrap* a
//   momentarily-negative value into some other small positive number
//   (itself already a real, if famously obscure, upstream edge case if the
//   player ever flies backward past world x=0 - `avrCompat.h`'s own header
//   comment already documents this general class of behavior difference
//   from aliasing every AVR fixed-width type to plain `int`). On real AVR
//   hardware the worst case is a glitched/wrong background tile for a
//   frame. On Vircon32, a raw negative array index into `coptLeFond[]`
//   would read whatever earlier global happens to sit at that address and
//   hand it to `gbDrawBitmap()` as a bitmap pointer - a wild pointer
//   dereference, not just a cosmetic glitch. A one-line `if (index < 0)
//   index += 8;` guard was added purely to keep the index in its own
//   intended `[0,7]` range; it is a no-op for every index value the
//   original formula ever actually produces in the forward-flight case.
// - `coptDrawMitraille()`'s own `sqrt(hypo^2 - hauteur^2)` is mathematically
//   never negative for any angle this game's own callers ever pass in
//   (verified by hand: `hypo = hauteur / cos(angle)` with `hauteur` a
//   non-negative int and `|angle|` bounded well under pi/2 by
//   `angleSprite`'s own real `[-0.6,0.6]` clamp, so `hypo >= hauteur`
//   always) - but `sqrt()` of a negative **hard-traps the Vircon32 CPU**
//   outright (VIRCON32_C_DIALECT.md section 17.3), unlike a desktop FPU's
//   silent `NaN`, so a defensive `if (diffSq < 0) diffSq = 0;` clamp was
//   added immediately before the `sqrt()` call as cheap insurance against
//   float-rounding error near the boundary, not because the unclamped
//   formula is actually reachable with a real negative value today.
//
// FRAME COUNTER AND FLOAT MATH: this file reads the real, shared
// `gbFrameCount` global (`gamebuinoShim.h`/`.c`, incremented once per real
// logic tick inside `gbUpdate()`, matching real hardware's own placement)
// directly for enemy fire cadence, the muzzle-flash line every-3rd-frame
// flicker, the rescapee walk-cycle animation, the "GO" blinking prompt, and
// similar timing, rather than keeping its own local frame counter. Its
// sub-pixel-precision physics likewise calls real `fmax()`/`fmin()`/
// `fabs()` directly from this dialect's own `math.h` (confirmed directly in
// the sibling tinyjoypad_vircon32 project's own VIRCON32_C_DIALECT.md, and
// already used directly by `gameCrazyTown.c`/`gameHexagon.c`), backed by
// real single-cycle `FMAX`/`FMIN`/`FABS` opcodes, rather than
// `gamebuinoShim.h`'s own int-only `gbMax()`/`gbMin()`/`gbAbsInt()`.

int coptVille1[242] =
{
    48, 40, 0x0, 0x0, 0x0, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5, 0x80, 0x0, 0x6, 0x0,
    0x0, 0x9, 0x40, 0x0, 0xB, 0x0, 0x3, 0x11, 0x20, 0x0, 0x12, 0x80, 0x5, 0xA1, 0x20, 0x0,
    0x22, 0x40, 0x9, 0x41, 0x20, 0x0, 0x42, 0x40, 0x11, 0x25, 0x68, 0x0, 0x42, 0x40, 0x21, 0x19,
    0x3C, 0x0, 0x4A, 0xC0, 0x41, 0x11, 0x2A, 0x0, 0x52, 0x40, 0x45, 0x51, 0x29, 0x0, 0x42, 0x40,
    0x49, 0x31, 0x29, 0x0, 0x42, 0x40, 0x51, 0x15, 0x69, 0x0, 0x4A, 0xC0, 0x41, 0x19, 0x29, 0x0,
    0x52, 0x40, 0x45, 0x51, 0x2B, 0x0, 0x42, 0x40, 0x49, 0x31, 0x29, 0x0, 0x42, 0x43, 0x51, 0x11,
    0x29, 0x0, 0x4A, 0xC5, 0xC1, 0x15, 0x69, 0x0, 0x52, 0x49, 0x45, 0x59, 0x2B, 0x0, 0x42, 0x51,
    0x29, 0x31, 0x29, 0x0, 0x42, 0x61, 0x11, 0x11, 0x29, 0x0, 0x4A, 0xC5, 0x51, 0x11, 0x29, 0x0,
    0x52, 0x4D, 0x75, 0x55, 0x6B, 0x0, 0x42, 0x59, 0x19, 0x39, 0x29, 0x0, 0x42, 0x41, 0x11, 0x11,
    0x29, 0x0, 0x4A, 0xC1, 0x11, 0x11, 0x29, 0x0, 0x52, 0x45, 0x55, 0x51, 0x2B, 0x0, 0x42, 0x4D,
    0x79, 0x35, 0x69, 0x0, 0x42, 0x59, 0x11, 0x19, 0x29, 0x0, 0x4A, 0xC1, 0x11, 0x11, 0x29, 0x0,
    0x52, 0x41, 0x15, 0x51, 0x2B, 0x0, 0x42, 0x45, 0x59, 0x31, 0x29, 0x0, 0x42, 0x4D, 0x71, 0x15,
    0x69, 0x0, 0x4A, 0xD9, 0x11, 0x19, 0x29, 0x0, 0x52, 0x41, 0x15, 0x51, 0x2B, 0x0, 0x42, 0x41,
    0x19, 0x31, 0x29, 0x0, 0x42, 0x5D, 0x51, 0x15, 0x69, 0x0, 0x42, 0x5D, 0x71, 0x19, 0x2B, 0x0,
    0x5A, 0x5D, 0x15, 0x51, 0x2B, 0x0, 0x5A, 0x5D, 0x15, 0x51, 0x2B, 0x0, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptVille2[242] =
{
    48, 40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0xB0,
    0x0, 0x0, 0x0, 0x0, 0x1, 0x28, 0x0, 0x1, 0x80, 0x0, 0x2, 0x24, 0x0, 0x2, 0xC0, 0x0,
    0x4, 0x24, 0x0, 0x4, 0xA0, 0x0, 0x8, 0xAC, 0x0, 0x8, 0x90, 0x0, 0x9, 0xA4, 0x0, 0x10,
    0x90, 0x0, 0xB, 0x24, 0x0, 0x22, 0x90, 0x0, 0x8, 0x24, 0x0, 0x44, 0x9C, 0x0, 0x8, 0xAC,
    0x0, 0x40, 0x96, 0x0, 0x9, 0xA4, 0x0, 0x48, 0x95, 0x0, 0xB, 0x24, 0x0, 0x50, 0x94, 0x80,
    0x8, 0x24, 0x0, 0x42, 0x96, 0x80, 0x8, 0xAC, 0x0, 0x44, 0x95, 0x80, 0x9, 0xA4, 0x0, 0x40,
    0x94, 0x80, 0xB, 0x24, 0xC0, 0x48, 0x94, 0x80, 0x8, 0x25, 0x60, 0x50, 0x96, 0x80, 0x8, 0xAE,
    0x53, 0x42, 0x95, 0x80, 0x9, 0xA4, 0x4D, 0xC4, 0x94, 0x80, 0xA, 0xE8, 0x49, 0x40, 0x94, 0x80,
    0xC, 0xB1, 0x51, 0x28, 0x96, 0x80, 0x8, 0x92, 0x55, 0x70, 0x95, 0x80, 0x10, 0x88, 0x59, 0x22,
    0x94, 0x80, 0x22, 0xA8, 0x51, 0x24, 0x94, 0x80, 0x26, 0xB9, 0x51, 0x20, 0x96, 0x80, 0x2C, 0x8A,
    0x55, 0x68, 0x95, 0x80, 0x20, 0x88, 0x59, 0x30, 0x94, 0x80, 0x20, 0x88, 0x51, 0x22, 0x94, 0x80,
    0x22, 0xA9, 0x51, 0x24, 0x96, 0x80, 0x26, 0xBA, 0x55, 0x60, 0x95, 0x80, 0x2C, 0x88, 0x59, 0x28,
    0x94, 0x80, 0x20, 0x88, 0x51, 0x30, 0x94, 0x80, 0x20, 0x89, 0x51, 0x22, 0x96, 0x80, 0x22, 0xAA,
    0x55, 0x64, 0x95, 0x80, 0x26, 0xB8, 0x59, 0x20, 0x94, 0x80, 0x2C, 0x88, 0x51, 0x28, 0x94, 0x80,
    0x20, 0x8B, 0x51, 0x30, 0x94, 0x80, 0x20, 0x8B, 0x51, 0x20, 0x94, 0x80, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptVille3[242] =
{
    48, 40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x80, 0x0, 0x0, 0x0,
    0x0, 0x3, 0x40, 0x0, 0x0, 0x60, 0x0, 0x5, 0x20, 0x0, 0x0, 0xD0, 0x0, 0x9, 0x10, 0x0,
    0x1, 0x48, 0x0, 0x9, 0x8, 0x0, 0x2, 0x44, 0x0, 0xD, 0x44, 0x0, 0x2, 0x42, 0x0, 0x9,
    0x64, 0x0, 0x2, 0x51, 0x0, 0x9, 0x34, 0x0, 0xE, 0x48, 0x80, 0x9, 0x4, 0x0, 0x1A, 0x40,
    0x80, 0xD, 0x44, 0x0, 0x2A, 0x44, 0x80, 0x9, 0x64, 0x0, 0x4A, 0x42, 0x80, 0x9, 0x34, 0x0,
    0x5A, 0x50, 0x80, 0x9, 0x4, 0x0, 0x6A, 0x48, 0x80, 0xD, 0x44, 0x0, 0x4A, 0x40, 0x80, 0x9,
    0x64, 0x0, 0x4A, 0x44, 0x80, 0xC9, 0x34, 0x0, 0x5A, 0x42, 0x81, 0xA9, 0x4, 0x0, 0x6A, 0x50,
    0xB2, 0x9D, 0x44, 0x0, 0x4A, 0x48, 0xEC, 0x89, 0x64, 0x0, 0x4A, 0x40, 0xA4, 0x85, 0xD4, 0x0,
    0x5A, 0x45, 0x22, 0xA3, 0x4C, 0x0, 0x6A, 0x43, 0xAA, 0x92, 0x44, 0x0, 0x4A, 0x51, 0x26, 0x84,
    0x42, 0x0, 0x4A, 0x49, 0x22, 0x85, 0x51, 0x0, 0x5A, 0x41, 0x22, 0xA7, 0x59, 0x0, 0x6A, 0x45,
    0xAA, 0x94, 0x4D, 0x0, 0x4A, 0x43, 0x26, 0x84, 0x41, 0x0, 0x4A, 0x51, 0x22, 0x84, 0x41, 0x0,
    0x5A, 0x49, 0x22, 0xA5, 0x51, 0x0, 0x6A, 0x41, 0xAA, 0x97, 0x59, 0x0, 0x4A, 0x45, 0x26, 0x84,
    0x4D, 0x0, 0x4A, 0x43, 0x22, 0x84, 0x41, 0x0, 0x5A, 0x51, 0x22, 0xA4, 0x41, 0x0, 0x6A, 0x49,
    0xAA, 0x95, 0x51, 0x0, 0x4A, 0x41, 0x26, 0x87, 0x59, 0x0, 0x4A, 0x45, 0x22, 0x84, 0x4D, 0x0,
    0x4A, 0x43, 0x22, 0xB4, 0x41, 0x0, 0x4A, 0x41, 0x22, 0xB4, 0x41, 0x0, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptVille4[242] =
{
    48, 40, 0x0, 0x30, 0x0, 0x0, 0x0, 0x0, 0x0, 0x68, 0x0, 0x0, 0x0, 0x0, 0x0, 0xA4,
    0x0, 0x0, 0x18, 0x0, 0x1, 0x22, 0x30, 0x0, 0x34, 0x0, 0x1, 0x21, 0x68, 0x0, 0x52, 0x0,
    0x1, 0x20, 0xA4, 0x0, 0x91, 0x0, 0x5, 0xA9, 0x22, 0x0, 0x90, 0x80, 0xF, 0x26, 0x21, 0x0,
    0x90, 0x80, 0x15, 0x22, 0x20, 0x80, 0xD4, 0x80, 0x25, 0x22, 0xA8, 0x80, 0x92, 0x80, 0x25, 0x23,
    0x24, 0x80, 0x90, 0x80, 0x25, 0xAA, 0x22, 0x80, 0x90, 0x80, 0x25, 0x26, 0x20, 0x80, 0xD4, 0x80,
    0x35, 0x22, 0xA8, 0x80, 0x92, 0x80, 0x25, 0x23, 0x24, 0x80, 0x90, 0x80, 0x25, 0x22, 0x22, 0xB0,
    0x90, 0x80, 0x25, 0xAA, 0x20, 0xE8, 0xD4, 0x80, 0x35, 0x26, 0xA8, 0xA4, 0x92, 0x80, 0x25, 0x23,
    0x25, 0x22, 0x90, 0x80, 0x25, 0x22, 0x22, 0x21, 0x90, 0x80, 0x25, 0x22, 0x22, 0xA8, 0xD4, 0x80,
    0x35, 0xAA, 0xAB, 0xAC, 0x92, 0x80, 0x25, 0x27, 0x26, 0x26, 0x90, 0x80, 0x25, 0x22, 0x22, 0x20,
    0x90, 0x80, 0x25, 0x22, 0x22, 0x20, 0xD4, 0x80, 0x35, 0x22, 0xAA, 0xA8, 0x92, 0x80, 0x25, 0xAB,
    0x27, 0xAC, 0x90, 0x80, 0x25, 0x26, 0x22, 0x26, 0x90, 0x80, 0x25, 0x22, 0x22, 0x20, 0xD4, 0x80,
    0x35, 0x22, 0xAA, 0x20, 0x92, 0x80, 0x25, 0x23, 0x26, 0xA8, 0x90, 0x80, 0x25, 0xAA, 0x23, 0xAC,
    0x90, 0x80, 0x25, 0x26, 0x22, 0x26, 0xD4, 0x80, 0x35, 0x22, 0xAA, 0x20, 0x92, 0x80, 0x25, 0x23,
    0x26, 0x20, 0x90, 0x80, 0x25, 0xAA, 0x22, 0xAE, 0x90, 0x80, 0x35, 0x26, 0x23, 0xAE, 0x90, 0x80,
    0x35, 0x22, 0xAA, 0x2E, 0x96, 0x80, 0x35, 0x22, 0xAA, 0x2E, 0x96, 0x80, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptVille5[242] =
{
    48, 40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x80, 0x0, 0x0, 0xC, 0x0, 0x2, 0xC0, 0x0,
    0x0, 0x14, 0x0, 0x4, 0xA0, 0x0, 0xE, 0x14, 0x0, 0x8, 0x90, 0x0, 0xB, 0x24, 0x0, 0x10,
    0x90, 0x0, 0xB, 0xE4, 0x0, 0x22, 0x90, 0x0, 0x8, 0x24, 0x0, 0x44, 0x9C, 0x0, 0x8, 0xAC,
    0x0, 0x40, 0x96, 0x0, 0x9, 0xA4, 0x0, 0x48, 0x95, 0x0, 0xB, 0x24, 0x0, 0x78, 0x94, 0x80,
    0x8, 0x24, 0x0, 0xA, 0x96, 0x80, 0x8, 0xAC, 0x0, 0x4, 0x95, 0x80, 0x9, 0xA4, 0x0, 0x2,
    0x94, 0x80, 0xB, 0x24, 0xC0, 0x2, 0x94, 0x80, 0x8, 0x25, 0x60, 0x1E, 0x96, 0x80, 0x8, 0xAE,
    0x50, 0x32, 0x95, 0x80, 0x9, 0xA4, 0x58, 0x24, 0x94, 0x80, 0xA, 0xE8, 0x5C, 0x20, 0x94, 0x80,
    0xC, 0xB1, 0x52, 0x68, 0x96, 0x80, 0x8, 0x92, 0x56, 0xB0, 0x95, 0x80, 0x10, 0x88, 0x59, 0xA2,
    0x94, 0x80, 0x22, 0xA8, 0x50, 0xA4, 0x94, 0x80, 0x26, 0xB9, 0x51, 0x20, 0x96, 0x80, 0x2C, 0x8A,
    0x55, 0x68, 0x95, 0x80, 0x20, 0x88, 0x59, 0x30, 0x94, 0x80, 0x20, 0x88, 0x51, 0x22, 0x94, 0x80,
    0x22, 0xA9, 0x51, 0x24, 0x96, 0x80, 0x26, 0xBA, 0x55, 0x60, 0x95, 0x80, 0x2C, 0x88, 0x59, 0x28,
    0x94, 0x80, 0x20, 0x88, 0x51, 0x30, 0x94, 0x80, 0x20, 0x89, 0x51, 0x22, 0x96, 0x80, 0x22, 0xAA,
    0x55, 0x64, 0x95, 0x80, 0x26, 0xB8, 0x59, 0x20, 0x94, 0x80, 0x2C, 0x88, 0x51, 0x28, 0x94, 0x80,
    0x20, 0x8B, 0x51, 0x30, 0x94, 0x80, 0x20, 0x8B, 0x51, 0x20, 0x94, 0x80, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptVille6[242] =
{
    48, 40, 0x0, 0x0, 0x0, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5, 0x80, 0x0, 0x6, 0x0,
    0x0, 0x9, 0x40, 0x0, 0xB, 0x0, 0x3, 0x11, 0x20, 0x0, 0x12, 0x80, 0x5, 0xA1, 0x20, 0x0,
    0x22, 0x40, 0x9, 0x41, 0x60, 0x0, 0x42, 0x40, 0x9, 0x23, 0x80, 0x0, 0x42, 0xC0, 0x9, 0x1A,
    0x0, 0x0, 0x4B, 0x80, 0x5, 0x12, 0x0, 0x0, 0x52, 0x0, 0x5, 0x53, 0xC0, 0x0, 0x42, 0x0,
    0x65, 0x31, 0x60, 0x0, 0x42, 0x0, 0x55, 0x15, 0x20, 0x0, 0x4B, 0x80, 0x59, 0x19, 0x20, 0x0,
    0x52, 0xC0, 0x45, 0x51, 0x20, 0x0, 0x42, 0x40, 0x49, 0x31, 0x30, 0x0, 0x42, 0x40, 0x51, 0x11,
    0x28, 0x0, 0x4A, 0xC0, 0x41, 0x15, 0x64, 0x0, 0x52, 0x40, 0x45, 0x59, 0x22, 0x0, 0x42, 0x58,
    0x69, 0x31, 0x21, 0x0, 0x42, 0x64, 0x51, 0x11, 0x29, 0x0, 0x4A, 0xC6, 0x91, 0x11, 0x25, 0x0,
    0x52, 0x41, 0x15, 0x55, 0x63, 0x0, 0x42, 0x59, 0x19, 0x39, 0x21, 0x0, 0x42, 0x41, 0x11, 0x11,
    0x29, 0x0, 0x4A, 0xC1, 0x11, 0x11, 0x25, 0x0, 0x52, 0x45, 0x55, 0x51, 0x23, 0x0, 0x42, 0x4D,
    0x79, 0x35, 0x61, 0x0, 0x42, 0x59, 0x11, 0x19, 0x29, 0x0, 0x4A, 0xC1, 0x11, 0x11, 0x25, 0x0,
    0x52, 0x41, 0x15, 0x51, 0x23, 0x0, 0x42, 0x45, 0x59, 0x31, 0x21, 0x0, 0x42, 0x4D, 0x71, 0x15,
    0x69, 0x0, 0x4A, 0xD9, 0x11, 0x19, 0x25, 0x0, 0x52, 0x41, 0x15, 0x51, 0x23, 0x0, 0x42, 0x41,
    0x19, 0x31, 0x21, 0x0, 0x42, 0x5D, 0x51, 0x15, 0x61, 0x0, 0x42, 0x5D, 0x71, 0x19, 0x23, 0x0,
    0x5A, 0x5D, 0x15, 0x51, 0x23, 0x0, 0x5A, 0x5D, 0x15, 0x51, 0x23, 0x0, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptVille7[242] =
{
    48, 40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x80, 0x0, 0x0, 0x0,
    0x0, 0x3, 0x40, 0x0, 0x0, 0x60, 0x0, 0x5, 0x20, 0x0, 0x0, 0xD0, 0x0, 0x9, 0x10, 0x0,
    0x1, 0x48, 0x0, 0x9, 0x8, 0x0, 0x2, 0x48, 0x0, 0xD, 0x44, 0x0, 0x2, 0x50, 0x0, 0x9,
    0x64, 0x0, 0x2, 0x51, 0x0, 0x9, 0x34, 0x0, 0x2, 0x4A, 0x0, 0x9, 0x4, 0x0, 0x2, 0x44,
    0x0, 0xD, 0x44, 0x0, 0x2, 0x44, 0x0, 0x9, 0x64, 0x0, 0x2, 0x42, 0x0, 0x9, 0x34, 0x0,
    0x2, 0x51, 0x80, 0x9, 0x4, 0x0, 0x2, 0x48, 0x80, 0xD, 0x44, 0x0, 0x2, 0x40, 0x80, 0x9,
    0x64, 0x0, 0x2, 0x44, 0x80, 0x9, 0x34, 0x0, 0x2, 0x42, 0x80, 0x9, 0x4, 0x0, 0x2, 0x50,
    0xBE, 0xD, 0x4, 0x0, 0x2, 0x48, 0xE4, 0x9, 0x24, 0x0, 0x2, 0x40, 0xA9, 0x9, 0x15, 0x0,
    0x2, 0x45, 0x2A, 0xD, 0x4F, 0x0, 0x2, 0x43, 0xAC, 0x1F, 0x2D, 0x0, 0x2, 0x51, 0x24, 0x25,
    0x11, 0x0, 0x2, 0x49, 0x23, 0x44, 0xA1, 0x0, 0x2, 0x41, 0x22, 0xA4, 0x61, 0x0, 0x2, 0x45,
    0xAA, 0x94, 0x41, 0x0, 0x2, 0x43, 0x26, 0x84, 0x41, 0x0, 0x2, 0x51, 0x22, 0x84, 0x41, 0x0,
    0x2, 0x49, 0x22, 0xA5, 0x51, 0x0, 0x12, 0x41, 0xAA, 0x97, 0x59, 0x0, 0x4A, 0x45, 0x26, 0x84,
    0x4D, 0x0, 0x66, 0x43, 0x22, 0x84, 0x41, 0x0, 0x5E, 0x51, 0x22, 0xA4, 0x41, 0x0, 0x4A, 0x49,
    0xAA, 0x95, 0x51, 0x0, 0x4A, 0x41, 0x26, 0x87, 0x59, 0x0, 0x4A, 0x45, 0x22, 0x84, 0x4D, 0x0,
    0x4A, 0x43, 0x22, 0xB4, 0x41, 0x0, 0x4A, 0x41, 0x22, 0xB4, 0x41, 0x0, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptVille8[242] =
{
    48, 40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x70, 0x0, 0x0, 0x0, 0x0, 0x0, 0x98, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x54, 0x0, 0x0, 0x0, 0x0, 0x0, 0x32, 0x0, 0x0, 0x0, 0x0, 0x1, 0x12,
    0x0, 0x0, 0x0, 0x3, 0x8, 0xB2, 0x0, 0x0, 0x0, 0x5, 0x94, 0x52, 0x0, 0x0, 0x0, 0x9,
    0x63, 0x93, 0x80, 0x0, 0x0, 0x11, 0x20, 0x13, 0x40, 0x0, 0x0, 0x91, 0x10, 0x15, 0x60, 0x0,
    0x0, 0xD1, 0x8, 0x99, 0x10, 0x0, 0x0, 0xF5, 0x49, 0x91, 0x20, 0x0, 0x1, 0x99, 0x2B, 0x21,
    0x40, 0x0, 0x2, 0x91, 0x8, 0x25, 0x80, 0x0, 0x4, 0x91, 0x8, 0x29, 0x42, 0x0, 0x0, 0x95,
    0x48, 0xA1, 0x27, 0x0, 0x0, 0x59, 0x29, 0xA1, 0x2A, 0x80, 0x0, 0x51, 0xB, 0x25, 0x32, 0x80,
    0x0, 0xD1, 0x8, 0x29, 0x22, 0x80, 0x4, 0x95, 0x48, 0x21, 0x2A, 0x80, 0x2, 0x99, 0x28, 0xA1,
    0x32, 0x80, 0x1, 0xB1, 0x9, 0x99, 0x22, 0x80, 0x0, 0x91, 0xB, 0x15, 0x22, 0x80, 0x20, 0x95,
    0x48, 0x13, 0x2A, 0x80, 0x30, 0x99, 0x28, 0x13, 0x32, 0x80, 0x28, 0x51, 0x8, 0x95, 0x22, 0x80,
    0x24, 0x51, 0x9, 0x95, 0x22, 0x80, 0x22, 0x95, 0x4B, 0x39, 0x2A, 0x80, 0x21, 0x19, 0x28, 0x21,
    0x32, 0x80, 0x25, 0x51, 0x8, 0x25, 0x22, 0x80, 0x29, 0x31, 0x8, 0xA9, 0x22, 0x80, 0x21, 0x15,
    0x49, 0xA1, 0x2A, 0x80, 0x21, 0x19, 0x2B, 0x21, 0x32, 0x80, 0x25, 0x51, 0x8, 0x25, 0x22, 0x80,
    0x29, 0x31, 0x8, 0x29, 0x22, 0x80, 0x21, 0x11, 0x8, 0x21, 0x22, 0x80, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC0,
};

int coptChar1[18] =
{
    16, 8, 0x1, 0x0, 0x0, 0xC0, 0x3, 0xF0, 0xFE, 0x10, 0x2, 0x10, 0xF, 0xF8, 0x1A, 0x2C,
    0xF, 0xF8,
};

int coptChar1C[18] =
{
    16, 8, 0x0, 0x0, 0x6, 0x0, 0x9, 0x70, 0x11, 0xD0, 0x23, 0x10, 0x47, 0xF8, 0x1A, 0x2C,
    0x3F, 0xF8,
};

int coptChar2[32] =
{
    24, 10, 0x0, 0x3C, 0x0, 0x7F, 0xC2, 0x0, 0x0, 0x82, 0x0, 0xF, 0x83, 0x80, 0x12, 0xFD,
    0x40, 0x21, 0x2, 0x20, 0x20, 0x0, 0x20, 0x1F, 0xFF, 0xC0, 0xA, 0xAA, 0x80, 0x7, 0xFF, 0x0,
};

int coptChar2C[32] =
{
    24, 10, 0x0, 0x0, 0x0, 0x1F, 0x0, 0x0, 0x21, 0x0, 0x0, 0x4F, 0x85, 0x80, 0x53, 0x4B,
    0x40, 0x21, 0x32, 0x20, 0x20, 0x84, 0x20, 0x1F, 0xFF, 0xC0, 0xA, 0xAA, 0x80, 0x7, 0xFF, 0x0,
};

int coptHalftrack[32] =
{
    24, 10, 0x0, 0xA, 0x80, 0x0, 0x5, 0x40, 0x1, 0xFA, 0xA0, 0x1, 0x9, 0x50, 0x7F, 0x8,
    0xA8, 0xC0, 0xF7, 0xF8, 0x7C, 0x0, 0x18, 0x57, 0xFF, 0xF0, 0x28, 0x2A, 0xA8, 0x10, 0x1F, 0xF0,
};

int coptHalftrack1c[32] =
{
    24, 10, 0x1, 0x0, 0x80, 0x62, 0x80, 0x40, 0x5C, 0x40, 0xA0, 0x88, 0x20, 0x50, 0x48, 0x40,
    0x28, 0x27, 0x80, 0xF8, 0x3C, 0x83, 0x18, 0x16, 0x7F, 0xF0, 0x2A, 0x2A, 0xA8, 0x11, 0x5F, 0xF0,
};

int coptHalftrack2[32] =
{
    24, 10, 0x7, 0xEF, 0xF8, 0x0, 0x5A, 0xA8, 0x1, 0xFD, 0x58, 0x1, 0xA, 0xA8, 0x7F, 0xD,
    0x58, 0xC0, 0xF7, 0xF8, 0x7C, 0x0, 0x18, 0x57, 0xFF, 0xF0, 0x28, 0x2A, 0xA8, 0x10, 0x1F, 0xF0,
};

int coptHalftrack2C[32] =
{
    24, 10, 0x8, 0x0, 0x78, 0x4, 0x20, 0xA8, 0x2, 0x51, 0x58, 0x1, 0x9A, 0xA8, 0xF, 0x2D,
    0x58, 0x70, 0xF7, 0xF8, 0x40, 0x0, 0x18, 0xDD, 0xFF, 0xF0, 0x66, 0x2A, 0xA8, 0x44, 0x1F, 0xF0,
};

int coptJeep[22] =
{
    16, 10, 0x20, 0x0, 0x50, 0x0, 0xA0, 0x0, 0x40, 0x40, 0x44, 0x40, 0xFC, 0x78, 0xFF, 0xF8,
    0xAF, 0x78, 0x50, 0xA0, 0x20, 0x40,
};

int coptJeepC[22] =
{
    16, 10, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0xD, 0x0, 0x12, 0x80, 0x25, 0x0, 0xEC, 0xE0,
    0xFF, 0xF0, 0xAF, 0x78, 0x70, 0xE0,
};

int coptHeliPort[26] =
{
    24, 8, 0x80, 0x18, 0xC0, 0xC0, 0xC, 0x60, 0x60, 0x6, 0x30, 0x30, 0x8F, 0x18, 0x18, 0xD9,
    0xC, 0xC, 0x70, 0x6, 0x6, 0x30, 0x3, 0x3, 0x18, 0x1,
};

int coptExplosion1[50] =
{
    16, 24, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x40, 0x2, 0x66, 0x66, 0x3F, 0xFC, 0x2F, 0xF4, 0x37, 0xEC, 0x7E, 0x7E, 0x7C, 0x3E,
    0xFC, 0x3F,
};

int coptExplosion2[50] =
{
    16, 24, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x3, 0xC0, 0xC, 0x30, 0x1F, 0xF8, 0x1F, 0xF8, 0xE, 0x70, 0x7, 0xE0,
    0xC3, 0xC3, 0x43, 0xC2, 0x67, 0xE6, 0x3F, 0xFC, 0x33, 0xCC, 0x21, 0x84, 0x72, 0x4E, 0x7C, 0x3E,
    0xFD, 0xBF,
};

int coptExplosion3[50] =
{
    16, 24, 0x3, 0xC0, 0x5, 0xA0, 0x1B, 0xD8, 0x27, 0xE4, 0x4F, 0xF2, 0xDD, 0xBB, 0xB9, 0x9D,
    0xB7, 0xED, 0xCB, 0xD3, 0x73, 0xCE, 0x3, 0xC0, 0x3, 0xC0, 0x3, 0xC0, 0x3, 0xC0, 0x3, 0xC0,
    0x3, 0xC3, 0x83, 0xC2, 0x43, 0xC6, 0x6B, 0xD4, 0x37, 0xEC, 0x21, 0x84, 0x73, 0xCE, 0x7D, 0xBE,
    0xFC, 0x3F,
};

int coptGo[53] =
{
    24, 17, 0x0, 0x1, 0x0, 0x0, 0x1, 0x80, 0x0, 0x1, 0xC0, 0x0, 0x1, 0xE0, 0x0, 0x1,
    0xF0, 0xFF, 0xFF, 0xF8, 0xFF, 0xFF, 0xFC, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFC, 0xFF, 0xFF, 0xF8, 0x0, 0x1, 0xF0, 0x0, 0x1, 0xE0, 0x0, 0x1, 0xC0, 0x0,
    0x1, 0x80, 0x0, 0x1, 0x0,
};

int coptCharEx1[10] =
{
    8, 8, 0x0, 0x0, 0x24, 0x18, 0x18, 0x24, 0x0, 0x0,
};

int coptCharEx2[10] =
{
    8, 8, 0x91, 0x42, 0x18, 0x25, 0xA4, 0x18, 0x42, 0x91,
};

int coptHelicoFace[35] =
{
    24, 11, 0xFF, 0xFF, 0x80, 0x0, 0x80, 0x0, 0x3, 0xE0, 0x0, 0xE, 0x38, 0x0, 0x8, 0x8,
    0x0, 0xC, 0x98, 0x0, 0xE, 0xB8, 0x0, 0x7, 0xF0, 0x0, 0x3, 0xE0, 0x0, 0x4, 0x10, 0x0,
    0xC, 0x18, 0x0,
};

int coptHelicoDroite[42] =
{
    32, 10, 0x7, 0xFF, 0xFF, 0xC0, 0x20, 0x1, 0x0, 0x0, 0x70, 0x1F, 0xE0, 0x0, 0xD8, 0x78,
    0xF8, 0x0, 0xAF, 0xFC, 0x4C, 0x0, 0xD8, 0xFF, 0xE6, 0x0, 0x70, 0xF, 0xFF, 0x0, 0x20, 0x7,
    0xFF, 0x80, 0x0, 0x1, 0x84, 0x20, 0x0, 0xF, 0xFF, 0xC0,
};

int coptHelicoGauche[42] =
{
    32, 10, 0x7F, 0xFF, 0xFC, 0x0, 0x0, 0x10, 0x0, 0x80, 0x0, 0xFF, 0x1, 0xC0, 0x3, 0xE3,
    0xC3, 0x60, 0x6, 0x47, 0xFE, 0xA0, 0xC, 0xFF, 0xE3, 0x60, 0x1F, 0xFE, 0x1, 0xC0, 0x3F, 0xFC,
    0x0, 0x80, 0x84, 0x30, 0x0, 0x0, 0x7F, 0xFE, 0x0, 0x0,
};

int coptHelicoDroiteMask[42] =
{
    32, 10, 0x7, 0xFF, 0xFF, 0xC0, 0x20, 0x1, 0x0, 0x0, 0x70, 0x1F, 0xE0, 0x0, 0xF8, 0x7F,
    0xF8, 0x0, 0xFF, 0xFF, 0xFC, 0x0, 0xF8, 0xFF, 0xFE, 0x0, 0x70, 0xF, 0xFF, 0x0, 0x20, 0x7,
    0xFF, 0x80, 0x0, 0x1, 0x84, 0x20, 0x0, 0xF, 0xFF, 0xC0,
};

int coptHelicoFaceMask[35] =
{
    24, 11, 0xFF, 0xFF, 0x80, 0x0, 0x80, 0x0, 0x3, 0xE0, 0x0, 0xF, 0xF8, 0x0, 0xF, 0xF8,
    0x0, 0xF, 0xF8, 0x0, 0xF, 0xF8, 0x0, 0x7, 0xF0, 0x0, 0x3, 0xE0, 0x0, 0x4, 0x10, 0x0,
    0xC, 0x18, 0x0,
};

int coptHelicoGaucheMask[42] =
{
    32, 10, 0x7F, 0xFF, 0xFC, 0x0, 0x0, 0x10, 0x0, 0x80, 0x0, 0xFF, 0x1, 0xC0, 0x3, 0xFF,
    0xC3, 0xE0, 0x7, 0xFF, 0xFF, 0xE0, 0xF, 0xFF, 0xE3, 0xE0, 0x1F, 0xFE, 0x1, 0xC0, 0x3F, 0xFC,
    0x0, 0x80, 0x84, 0x30, 0x0, 0x0, 0x7F, 0xFE, 0x0, 0x0,
};

int coptHelicoCracheMask[50] =
{
    16, 24, 0x3F, 0xFC, 0x3F, 0xFE, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x73, 0xCE, 0x7, 0xE0, 0xF, 0xF0, 0x1F, 0xF8, 0x1F, 0xF8, 0xF, 0xF0, 0x7, 0xE0, 0x3, 0xC0,
    0x3, 0xC0, 0x3, 0xC0, 0x3, 0xC0, 0x3, 0xC0, 0x1B, 0xD8, 0x3F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFE,
    0xFF, 0xFF,
};

int coptHelicoExplodeMask[35] =
{
    24, 11, 0x89, 0xC8, 0x80, 0x47, 0xFB, 0x0, 0x3F, 0xFE, 0x0, 0x3F, 0xFF, 0x0, 0xBF, 0xFF,
    0x0, 0x5F, 0xFE, 0x0, 0x3F, 0xFD, 0x0, 0x1F, 0xFC, 0x80, 0x2F, 0xFC, 0x0, 0x47, 0xF4, 0x0,
    0x89, 0xC2, 0x0,
};

int coptHelicoCrache[50] =
{
    16, 24, 0x3F, 0xFC, 0x30, 0xE, 0xE0, 0x6, 0xC0, 0x3, 0x80, 0x1, 0x80, 0x1, 0x9E, 0x79,
    0x72, 0x4E, 0x6, 0x60, 0xA, 0x50, 0x12, 0x48, 0x12, 0x48, 0xA, 0x50, 0x7, 0xE0, 0x2, 0x40,
    0x2, 0x40, 0x2, 0x40, 0x2, 0x40, 0x2, 0x40, 0x1A, 0x58, 0x26, 0x64, 0x42, 0x42, 0x46, 0x62,
    0x84, 0x21,
};

int coptHelicoExplode[35] =
{
    24, 11, 0x88, 0x48, 0x80, 0x44, 0x91, 0x0, 0x23, 0xE2, 0x0, 0x1F, 0x3C, 0x0, 0x88, 0x8C,
    0x0, 0x4C, 0xDA, 0x0, 0x2E, 0xB9, 0x0, 0x13, 0xF0, 0x80, 0x23, 0xE8, 0x0, 0x44, 0x84, 0x0,
    0x88, 0x42, 0x0,
};

int coptMan1[7] =
{
    8, 5, 0x40, 0xA0, 0x40, 0x40, 0xA0,
};

int coptMan2[7] =
{
    8, 5, 0x40, 0x0, 0xE0, 0x40, 0xA0,
};

int coptTitle[290] =
{
    64, 36, 0x0, 0xB, 0x29, 0xD1, 0xDD, 0xD9, 0xD0, 0x0, 0xFF, 0xFB, 0x91, 0x11, 0x1D, 0x14,
    0x9F, 0xFF, 0x0, 0xB, 0x11, 0xDD, 0xD5, 0xD4, 0x90, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1,
    0xA0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x90, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4,
    0x90, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0xA0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4,
    0xA2, 0x0, 0x4, 0x0, 0x0, 0x0, 0x0, 0x4, 0x94, 0x0, 0x6, 0x0, 0xF, 0xFF, 0xFF, 0x84,
    0x88, 0x0, 0x7, 0x80, 0x40, 0x2, 0x0, 0x4, 0x88, 0x0, 0xC, 0x80, 0xE0, 0x3F, 0xC0, 0x4,
    0x84, 0x0, 0x14, 0x81, 0xB0, 0xF1, 0xF0, 0x4, 0xA3, 0x0, 0x24, 0x81, 0x5F, 0xF8, 0x98, 0x4,
    0x91, 0x0, 0x4, 0x81, 0xB1, 0xFF, 0xCC, 0x4, 0x81, 0x0, 0x2, 0x80, 0xE0, 0x1F, 0xFE, 0x4,
    0x89, 0x0, 0x2, 0x80, 0x40, 0xF, 0xFF, 0x4, 0x85, 0x0, 0x6, 0x80, 0x0, 0x3, 0x8, 0x44,
    0xA1, 0x7C, 0x24, 0x80, 0x0, 0x1F, 0xFF, 0x84, 0x91, 0xC8, 0x14, 0x80, 0x10, 0x0, 0x0, 0x4,
    0x81, 0x52, 0xD, 0x80, 0x8, 0x0, 0x0, 0x4, 0x8A, 0x54, 0x4, 0x8F, 0xD0, 0x0, 0x0, 0x4,
    0x87, 0x58, 0x4, 0x90, 0x20, 0x4, 0x0, 0x4, 0xA2, 0x48, 0x84, 0xA0, 0x50, 0x8, 0x0, 0x4,
    0x92, 0x46, 0x42, 0xB0, 0x89, 0x90, 0x0, 0x4, 0x82, 0x45, 0x22, 0xA8, 0x19, 0xE0, 0x0, 0x4,
    0x8B, 0x55, 0x14, 0xA4, 0x29, 0x70, 0x0, 0x4, 0x86, 0x4D, 0x8, 0xA3, 0xCB, 0x18, 0x0, 0x4,
    0xA2, 0x45, 0x2A, 0xA2, 0x4A, 0x8, 0x0, 0x4, 0x92, 0x45, 0x49, 0xA2, 0x4A, 0x8, 0x60, 0x24,
    0x83, 0x55, 0x8, 0xA2, 0x4A, 0x48, 0x20, 0x94, 0x8A, 0x4D, 0x8, 0xA2, 0x4E, 0x6C, 0x20, 0xCC,
    0x86, 0x45, 0x2A, 0xA2, 0x4C, 0x3, 0x20, 0xBC, 0xA2, 0x45, 0x49, 0xA2, 0x49, 0x11, 0xF8, 0x94,
    0x93, 0x55, 0x8, 0xA2, 0x49, 0x0, 0xCC, 0x94, 0x82, 0x4D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
};

// -----------------------------------------------------------------------------
// Structs (real named-struct types + real Type[N] arrays, both proven
// already in this project by gameAgaruino.c's own struct AgarBall).
// -----------------------------------------------------------------------------

typedef struct
{
    int type;   // 0 = heliport, 1-49 = decorative-only, >49 = attack target
    int posX, posY;
    int height;
    int* sprite;
    int* spriteDamage;
    int life;
    int cadance;
    int damage;
} CoptBatiment;

typedef struct
{
    int timerAlive;
    int damage;
    int isGravity;
    float x, y;
    float vx, vy;
} CoptMissile;

typedef struct
{
    int etat; // 0 dead, 1 safe (dropped off), 2 in copter, 3 alive on the ground
    int x, tx, y;
} CoptRescape;

// -----------------------------------------------------------------------------
// Constants - COPT_-prefixed even though these are plain #defines (not
// globals), since this dialect's single translation unit means every game's
// own #define macros share one flat preprocessor namespace too.
// -----------------------------------------------------------------------------

#define COPT_NB_EXPLOSION_ENNEMI 3
#define COPT_MAX_LIFE 10
#define COPT_FRAMERATE 41
#define COPT_TIME_TO_REGENERE ( COPT_FRAMERATE * 2 )
#define COPT_TIME_TO_ENRAYE COPT_FRAMERATE
#define COPT_NB_RESC 33
#define COPT_NB_MAX_RESC_IN_COPTER 8
#define COPT_NB_LIFE 2
#define COPT_GRAVITE 0.025
#define COPT_TIME_ALIVE_BOULET 120
#define COPT_TIME_ALIVE_MITRAILLE 60
#define COPT_V_MISSILE10 200
#define COPT_V_MISSILE 2
#define COPT_NB_MISSIBLE 10
#define COPT_NB_BAT_LVL 31
#define COPT_NB_FOND 8
#define COPT_ANGLE_ROT 0.05
#define COPT_MAX_VELOCITY 4
#define COPT_DOUBLE_CLIC 10
#define COPT_TEMP_CRASH 80
#define COPT_STATE_TITLE 0
#define COPT_STATE_PLAY 1

// -----------------------------------------------------------------------------
// Sound - see this file's own header comment on why this is an
// approximation (this shim only has one-shot tones, no volume/waveform/
// slide shaping).
// -----------------------------------------------------------------------------

int coptSoundFx[3][8] =
{
    { 1, 4, 113, 10, 7, 19, 7, 52 },  // Explosion
    { 1, 36, 12, 1, 7, 0, 7, 10 },    // Mitraille (machine-gun fire)
    { 0, 25, 1, 1, 7, 0, 7, 9 },      // Rescue pickup
};

void coptPlaySoundFx( int fxno, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, coptSoundFx[ fxno ][ 6 ], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, coptSoundFx[ fxno ][ 0 ], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, coptSoundFx[ fxno ][ 5 ], -coptSoundFx[ fxno ][ 4 ], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, coptSoundFx[ fxno ][ 3 ], coptSoundFx[ fxno ][ 2 ] - 58, channel );
    gbPlayNoteChannel( coptSoundFx[ fxno ][ 1 ], coptSoundFx[ fxno ][ 7 ], channel );
}


// -----------------------------------------------------------------------------
// Game state
// -----------------------------------------------------------------------------

// Pristine copy of the real building/vehicle table, restored into coptBat[]
// on every fresh menu launch (gameCopter_init()) - see this file's own
// header comment, quirk 5, for why this project's own menu structure needs
// this where real hardware never did.
// Field order matches struct CoptBatiment exactly: type, posX, posY,
// height, sprite, spriteDamage, life, cadance, damage. posY is NOT
// uniformly 48 - most entries start well below the real ground line (48)
// and rise into place over time via coptUpdateBatiment()'s own
// `if (posY>48) posY--` climb, a real, load-bearing part of the level
// design (an earlier draft of this port mistakenly flattened every row's
// posY to 48, silently deleting every building's own rise-in animation -
// caught by re-checking this table character-for-character against the
// real upstream source before shipping).
CoptBatiment coptBatInit[COPT_NB_BAT_LVL] =
{
    { 60, 500, 48, 10, coptJeep, coptJeepC, 127, 2, COPT_MAX_LIFE },     // anti integer-negative-bug sentinel (see header comment)
    { 0, 1010, 48, 8, coptHeliPort, coptHeliPort, 10, 0, 0 },
    { 50, 1200, 70, 10, coptJeep, coptJeepC, 5, 5, 2 },
    { 51, 1110, 80, 10, coptHalftrack, coptHalftrack1c, 15, 10, 3 },
    { 53, 1150, 60, 8, coptChar1, coptChar1C, 25, 15, 6 },
    { 54, 1350, 70, 10, coptChar2, coptChar2C, 35, 20, 7 },
    { 53, 1410, 90, 8, coptChar1, coptChar1C, 25, 15, 6 },
    { 50, 1500, 80, 10, coptJeep, coptJeepC, 5, 5, 2 },
    { 52, 1620, 60, 10, coptHalftrack2, coptHalftrack2C, 15, 10, 3 },
    { 51, 1700, 70, 10, coptHalftrack, coptHalftrack1c, 15, 10, 4 },
    { 50, 1800, 60, 10, coptJeep, coptJeepC, 5, 5, 2 },
    { 54, 1950, 90, 10, coptChar2, coptChar2C, 40, 20, 7 },
    { 52, 2200, 60, 10, coptHalftrack2, coptHalftrack2C, 15, 10, 4 },
    { 52, 2315, 120, 10, coptHalftrack2, coptHalftrack2C, 15, 10, 4 },
    { 51, 2450, 60, 10, coptHalftrack, coptHalftrack1c, 15, 10, 3 },
    { 53, 2575, 90, 8, coptChar1, coptChar1C, 25, 15, 6 },
    { 54, 2650, 80, 10, coptChar2, coptChar2C, 35, 20, 7 },
    { 53, 2780, 70, 8, coptChar1, coptChar1C, 25, 15, 6 },
    { 50, 2800, 120, 10, coptJeep, coptJeepC, 5, 5, 2 },
    { 52, 2850, 80, 10, coptHalftrack2, coptHalftrack2C, 15, 10, 4 },
    { 51, 2880, 55, 10, coptHalftrack, coptHalftrack1c, 15, 10, 3 },
    { 50, 2900, 75, 10, coptJeep, coptJeepC, 5, 5, 2 },
    { 54, 2960, 60, 10, coptChar2, coptChar2C, 40, 20, 7 },
    { 52, 3000, 65, 10, coptHalftrack2, coptHalftrack2C, 15, 10, 4 },
    { 53, 3075, 85, 8, coptChar1, coptChar1C, 25, 15, 6 },
    { 54, 3150, 90, 10, coptChar2, coptChar2C, 35, 20, 7 },
    { 53, 3100, 70, 8, coptChar1, coptChar1C, 25, 15, 6 },
    { 52, 3180, 60, 10, coptHalftrack2, coptHalftrack2C, 15, 10, 4 },
    { 51, 3200, 100, 10, coptHalftrack, coptHalftrack1c, 15, 10, 3 },
    { 0, 3250, 48, 8, coptHeliPort, coptHeliPort, 10, 0, 0 },
    { 60, 6000, 48, 10, coptJeep, coptJeepC, 127, 2, COPT_MAX_LIFE },    // anti integer-negative-bug sentinel (see header comment)
};

CoptBatiment coptBat[COPT_NB_BAT_LVL]; // mutable gameplay copy, populated from coptBatInit every gameCopter_init()

// Backgrounds - real upstream `leFond[]` pointer table, order preserved exactly.
int* coptLeFond[COPT_NB_FOND] = { coptVille1, coptVille5, coptVille3, coptVille7, coptVille2, coptVille8, coptVille6, coptVille4 };

CoptMissile coptMissiles[COPT_NB_MISSIBLE];
CoptRescape coptResc[COPT_NB_RESC];

// Player state (a single instance upstream - kept as plain globals rather
// than wrapped in a struct, matching gamePong.c's own precedent for
// single-instance entities).
float coptPlayerVx, coptPlayerVy;
float coptPlayerX, coptPlayerY;
float coptPlayerEtat;         // aim direction: <0.5 aiming left, 0.5-2.5 centered, >2.5 aiming right (real upstream comment's own labels are inconsistent with the actual conditions below - only the numeric thresholds matter and are preserved exactly)
float coptPlayerAngleSprite;  // real-time tilt used by coptDrawBitmapAngle()
bool coptPlayerMitraille;     // currently firing
bool coptPlayerIsEnrayer;     // gun jammed (overheated)
int coptPlayerImpact;         // screen x of the gun's own current ground impact point
int coptPlayerHp;
int coptPlayerNbClient;       // civilians currently aboard
int coptPlayerOffsetCam;      // real upstream's own camera/drawing-position split - see header comment
int coptPlayerTimeMitraille;
int coptPlayerTimeRegenere;

int coptCptVictoire; // civilians successfully dropped off
int coptCptDeath;    // civilians killed (crashed with, or shot)
int coptVieRestant;  // lives remaining

int coptCrashTimer;   // upstream: cptCrash
int coptTimeToReclic; // upstream: timeToReclclic (double-tap window)

int coptExState;  // upstream: exEnn.etat - >= COPT_NB_EXPLOSION_ENNEMI means "no explosion playing"
int coptExFreq;    // upstream: exEnn.frequence
int coptExPosX;    // upstream: exEnn.posX

int coptState;

// -----------------------------------------------------------------------------
// HUD helpers (hud.ino)
// -----------------------------------------------------------------------------

int coptGetLifeHud()
{
    return ( coptPlayerHp * 10 ) / COPT_MAX_LIFE;
}

int coptGetLifeMittraille()
{
    return ( coptPlayerTimeMitraille * 10 ) / COPT_TIME_TO_ENRAYE;
}

// -----------------------------------------------------------------------------
// Player draw/update (Player.ino)
// -----------------------------------------------------------------------------

// Direct port of upstream's own custom per-pixel rotate+mask blit - this is
// NOT the same "mask/fill-under-bitmap" pattern flagged elsewhere in this
// project (gameFlappyBirdo.c/gameParachute.c): upstream's own function
// already correctly composites both layers itself, pixel by pixel (a "mask"
// on bit decides whether to touch the destination pixel at all; the
// corresponding "bitmap" bit then picks BLACK vs WHITE for it, so a WHITE
// pixel genuinely erases whatever was drawn underneath) - `gbDrawBitmapRotated()`
// alone could not reproduce this (it only ever draws "on" bits in a single
// fixed color), so this custom routine is kept, ported to operate on this
// shim's own `int*` bitmap format (`[0]`=width,`[1]`=height, ceil(width/8)
// bytes/row) instead of `pgm_read_byte()`.
void coptDrawBitmapAngle( int x, int y, int* bitmap, int* mask, float angle )
{
    int w = bitmap[ 0 ];
    int h = bitmap[ 1 ];
    int centerX = w / 2;
    int centerY = h / 2;
    int byteWidth = ( w + 7 ) / 8;
    float cosA = cos( angle );
    float sinA = sin( angle );
    int i, j, desX, desY;

    for( j = 0; j < h; j++ )
    {
        for( i = 0; i < w; i++ )
        {
            if( mask[ 2 + j * byteWidth + i / 8 ] & ( 0x80 >> ( i % 8 ) ) )
            {
                if( bitmap[ 2 + j * byteWidth + i / 8 ] & ( 0x80 >> ( i % 8 ) ) )
                  gbSetColor( 1 ); // BLACK
                else
                  gbSetColor( 0 ); // WHITE

                desX = (int)( ( (float)( i - centerX ) ) * cosA - ( (float)( j - centerY ) ) * sinA );
                desY = (int)( ( (float)( i - centerX ) ) * sinA + ( (float)( j - centerY ) ) * cosA );
                gbDrawPixel( x + desX, y + desY );
            }
        }
    }

    gbSetColor( 1 );
}

void coptDrawMitraille( float angle, int mult )
{
    if( !coptPlayerMitraille ) return;

    int hauteur = 48 - (int)coptPlayerY;
    int hypo = (int)( (float)hauteur / cos( angle ) );
    int diffSq = hypo * hypo - hauteur * hauteur;
    if( diffSq < 0 ) diffSq = 0; // Vircon32-specific defensive clamp - see this file's own header comment
    int dist = coptPlayerOffsetCam + (int)( sqrt( (float)diffSq ) * (float)mult );
    coptPlayerImpact = dist;

    int i, taille;
    for( i = -1; i < 2; i++ )
    {
        taille = 1 + arand( 5 );
        gbDrawLine( dist + ( i * 2 ), 46, dist + ( i * 2 ), 46 - taille );
    }

    int offset = 0;
    if( coptPlayerEtat < 0.5 ) offset = -10;
    else if( coptPlayerEtat > 2.5 ) offset = 2;
    else offset = -5;

    if( ( gbFrameCount % 3 ) == 0 )
      gbDrawLine( coptPlayerOffsetCam + offset, (int)coptPlayerY, ( dist - 1 ) + arand( 3 ), 46 );
}

void coptDrawPlayer()
{
    if( coptPlayerHp > 0 )
    {
        if( coptPlayerEtat < 0.5 )
        {
            coptDrawBitmapAngle( coptPlayerOffsetCam, (int)coptPlayerY, coptHelicoGauche, coptHelicoGaucheMask, coptPlayerAngleSprite );
            coptDrawMitraille( 0.90 + coptPlayerAngleSprite, -1 );
        }
        else if( coptPlayerEtat > 2.5 )
        {
            coptDrawBitmapAngle( coptPlayerOffsetCam, (int)coptPlayerY, coptHelicoDroite, coptHelicoDroiteMask, coptPlayerAngleSprite );
            coptDrawMitraille( 0.90 - coptPlayerAngleSprite, 1 );
        }
        else
        {
            coptDrawBitmapAngle( coptPlayerOffsetCam, (int)coptPlayerY, coptHelicoFace, coptHelicoFaceMask, coptPlayerAngleSprite );
            coptDrawMitraille( 0, 1 );
        }
    }
    else if( coptPlayerY < 42 )
      coptDrawBitmapAngle( coptPlayerOffsetCam, (int)coptPlayerY, coptHelicoExplode, coptHelicoExplodeMask, coptPlayerAngleSprite );
    else
      coptDrawBitmapAngle( coptPlayerOffsetCam, 34, coptHelicoCrache, coptHelicoCracheMask, 0 );
}

void coptUpdatePlayer()
{
    if( coptTimeToReclic > 0 )
      coptTimeToReclic--;

    if( coptPlayerHp > 0 )
    {
        if( gbPressed( BTN_RIGHT ) )
        {
            if( coptTimeToReclic > 0 )
            {
                coptPlayerEtat = coptPlayerEtat + 1.5;
                coptTimeToReclic = 0;
            }
            else
              coptTimeToReclic = COPT_DOUBLE_CLIC;
        }

        if( gbPressed( BTN_LEFT ) )
        {
            if( coptTimeToReclic > 0 )
            {
                coptPlayerEtat = coptPlayerEtat - 1.5;
                coptTimeToReclic = 0;
            }
            else
              coptTimeToReclic = COPT_DOUBLE_CLIC;
        }

        if( gbRepeat( BTN_RIGHT, 1 ) )
        {
            coptPlayerAngleSprite = fmin( coptPlayerAngleSprite + COPT_ANGLE_ROT, 0.6 );
            coptPlayerVx = fmin( coptPlayerVx + 0.18, COPT_MAX_VELOCITY );
            coptPlayerVy = fmax( coptPlayerVy - 0.05, -COPT_MAX_VELOCITY );
        }
        if( gbRepeat( BTN_LEFT, 1 ) )
        {
            coptPlayerAngleSprite = fmax( coptPlayerAngleSprite - COPT_ANGLE_ROT, -0.60 );
            coptPlayerVx = fmax( coptPlayerVx - 0.18, -COPT_MAX_VELOCITY );
            coptPlayerVy = fmax( coptPlayerVy - 0.05, -COPT_MAX_VELOCITY );
        }
        if( gbRepeat( BTN_UP, 1 ) )
          coptPlayerVy = fmax( coptPlayerVy - 0.15, -COPT_MAX_VELOCITY );
        if( gbRepeat( BTN_DOWN, 1 ) )
          coptPlayerVy = fmin( coptPlayerVy + 0.2, COPT_MAX_VELOCITY );

        if( !coptPlayerIsEnrayer && gbRepeat( BTN_A, 1 ) )
        {
            coptPlaySoundFx( 1, 0 );
            coptPlayerMitraille = true;
            coptPlayerTimeMitraille++;
            if( coptPlayerTimeMitraille > COPT_TIME_TO_ENRAYE )
            {
                coptPlayerTimeMitraille = COPT_TIME_TO_ENRAYE;
                coptPlayerIsEnrayer = true;
            }
        }
        else
        {
            coptPlayerMitraille = false;
            int enrayerMod = 1;
            if( coptPlayerIsEnrayer ) enrayerMod = 2;
            if( coptPlayerTimeMitraille > 0 && ( gbFrameCount % enrayerMod ) == 0 )
              coptPlayerTimeMitraille--;
            else if( coptPlayerTimeMitraille == 0 )
              coptPlayerIsEnrayer = false;
        }

        if( coptPlayerTimeRegenere == 0 && coptPlayerHp < COPT_MAX_LIFE )
        {
            if( ( gbFrameCount % 3 ) == 0 )
              coptPlayerHp++;
        }
        else if( coptPlayerTimeRegenere > 0 )
          coptPlayerTimeRegenere--;

        // upstream's own BTN_B handler body is `//player.hp = 0;` - a real
        // no-op left commented out in the actual shipped source, so there is
        // nothing to port here either.

        if( ( coptPlayerVx > 0.5 && coptPlayerEtat < 3 ) || coptPlayerEtat < -0.1 )
          coptPlayerEtat = coptPlayerEtat + 0.05;
        else if( ( coptPlayerVx < -0.5 && coptPlayerEtat > 0 ) || coptPlayerEtat > 3.1 )
          coptPlayerEtat = coptPlayerEtat - 0.05;

        if( coptPlayerEtat < 0.5 )
        {
            if( coptPlayerOffsetCam < 65 )
            {
                coptPlayerOffsetCam++;
                coptPlayerX--;
            }
        }
        else if( coptPlayerEtat > 2.5 )
        {
            if( coptPlayerOffsetCam > 20 )
            {
                coptPlayerOffsetCam--;
                coptPlayerX++;
            }
        }
        else
        {
            if( coptPlayerOffsetCam > 42 )
            {
                coptPlayerOffsetCam--;
                coptPlayerX++;
            }
            else if( coptPlayerOffsetCam < 42 )
            {
                coptPlayerOffsetCam++;
                coptPlayerX--;
            }
        }

        coptPlayerAngleSprite = coptPlayerAngleSprite * 0.9;
        coptPlayerVx = coptPlayerVx * 0.95;
        coptPlayerVy = fmin( coptPlayerVy + 0.05, COPT_MAX_VELOCITY );

        coptPlayerX = coptPlayerX + coptPlayerVx;
        coptPlayerY = coptPlayerY + coptPlayerVy;

        if( coptPlayerY > 42 )
        {
            if( fabs( coptPlayerVy ) > 0.5 )
            {
                coptPlayerY = coptPlayerY - coptPlayerVy;
                coptPlayerVy = -0.2 * coptPlayerVy;
            }
            else
            {
                coptPlayerY = 42;
                coptPlayerVy = 0;
            }
        }
        if( coptPlayerY < 10 )
        {
            coptPlayerY = 10;
            coptPlayerVy = 0;
        }
    }
    else
    {
        coptPlayerMitraille = false;
        if( coptCrashTimer <= 0 )
          coptCrashTimer = COPT_TEMP_CRASH;
        coptCrashTimer--;
        if( coptCrashTimer < 1 )
        {
            coptVieRestant--;
            coptPlayerAngleSprite = 0;
            coptPlayerHp = COPT_MAX_LIFE;
        }
        if( coptPlayerNbClient > 0 )
        {
            int j;
            for( j = 0; j < COPT_NB_RESC; j++ )
            {
                if( coptResc[ j ].etat == 2 )
                {
                    coptResc[ j ].etat = 0;
                    coptPlayerNbClient--;
                    coptCptDeath++;
                }
            }
        }

        if( coptPlayerY < 42 )
        {
            coptPlayerAngleSprite = coptPlayerAngleSprite + COPT_ANGLE_ROT * 4;
            coptPlayerVy = fmin( coptPlayerVy + 0.3, COPT_MAX_VELOCITY );
            coptPlayerVx = coptPlayerVx * 0.96;
            coptPlayerX = coptPlayerX + coptPlayerVx;
            coptPlayerY = coptPlayerY + coptPlayerVy;
        }
    }
}

// -----------------------------------------------------------------------------
// World background (Copter.ino's own drawWorld())
// -----------------------------------------------------------------------------

void coptDrawWorld()
{
    gbSetColor( GB_GRAY );

    int i, index, indexImageX;
    for( i = 0; i < 3; i++ )
    {
        index = ( ( ( (int)coptPlayerX / 42 ) % 8 ) + i ) % 8;
        if( index < 0 ) index = index + 8; // Vircon32-specific defensive guard - see this file's own header comment
        indexImageX = ( 42 * i ) - ( (int)coptPlayerX % 42 );
        gbDrawBitmap( indexImageX, 0, coptLeFond[ index ] );
    }

    gbSetColor( 1 );
    gbDrawFastHLine( 0, 39, 84 );
}

// -----------------------------------------------------------------------------
// Buildings / enemies (Ennemies.ino)
// -----------------------------------------------------------------------------

void coptUpdateBatiment()
{
    int i, j, x;
    for( i = 0; i < COPT_NB_BAT_LVL; i++ )
    {
        if( coptBat[ i ].type == 0 )
        {
            if( coptPlayerVx < 0.2 && coptPlayerY > 40
                && gbAbsInt( ( (int)coptPlayerX + coptPlayerOffsetCam ) - ( coptBat[ i ].posX + 12 ) ) < 20
                && coptPlayerNbClient > 0 && ( gbFrameCount % 20 ) == 0 )
            {
                for( j = 0; j < COPT_NB_RESC; j++ )
                {
                    if( coptResc[ j ].etat == 2 )
                    {
                        coptResc[ j ].etat = 1;
                        coptPlayerNbClient--;
                        coptCptVictoire++;
                        break;
                    }
                }
            }
        }
        else if( coptBat[ i ].type > 49 && coptBat[ i ].life > 0
                 && gbAbsInt( ( (int)coptPlayerX + coptPlayerOffsetCam ) - coptBat[ i ].posX ) < 88 )
        {
            if( coptBat[ i ].posY > 48 && ( gbFrameCount % 5 ) == 0 )
              coptBat[ i ].posY--;

            if( coptPlayerMitraille )
            {
                int posXBat = coptBat[ i ].posX - (int)coptPlayerX;
                if( coptPlayerImpact > posXBat && coptPlayerImpact < ( posXBat + 15 ) )
                  coptBat[ i ].life--;

                if( coptBat[ i ].life <= 0 )
                {
                    coptPlaySoundFx( 0, 0 );
                    coptExState = 0;
                    coptExPosX = coptBat[ i ].posX;
                }
            }

            if( coptBat[ i ].type == 50
                && gbAbsInt( ( (int)coptPlayerX + coptPlayerOffsetCam ) - coptBat[ i ].posX ) > 15
                && ( gbFrameCount % 3 ) == 0 )
            {
                if( ( (int)coptPlayerX + coptPlayerOffsetCam ) > coptBat[ i ].posX )
                  coptBat[ i ].posX++;
                else
                  coptBat[ i ].posX--;
            }

            if( coptBat[ i ].posY < 52 && ( gbFrameCount % coptBat[ i ].cadance ) == 0 )
            {
                for( x = 0; x < COPT_NB_MISSIBLE; x++ )
                {
                    if( coptMissiles[ x ].timerAlive == 0 )
                    {
                        coptMissiles[ x ].vy = -( (float)arand( COPT_V_MISSILE10 ) ) / 100.0;
                        coptMissiles[ x ].vx = COPT_V_MISSILE + coptMissiles[ x ].vy;

                        if( ( gbFrameCount % 2 ) == 0 )
                          coptMissiles[ x ].vx = -coptMissiles[ x ].vx;

                        coptMissiles[ x ].x = coptBat[ i ].posX + 10;
                        coptMissiles[ x ].y = 48 - coptBat[ i ].height;

                        if( coptBat[ i ].type == 53 || coptBat[ i ].type == 54 )
                        {
                            coptMissiles[ x ].timerAlive = COPT_TIME_ALIVE_BOULET;
                            coptMissiles[ x ].isGravity = 1;
                        }
                        else
                        {
                            coptMissiles[ x ].timerAlive = COPT_TIME_ALIVE_MITRAILLE;
                            coptMissiles[ x ].isGravity = 0;
                        }

                        coptMissiles[ x ].damage = coptBat[ i ].damage;
                        break;
                    }
                }
            }
        }
    }
}

void coptDrawEnnemies()
{
    int i, dist;
    for( i = 0; i < COPT_NB_BAT_LVL; i++ )
    {
        dist = coptBat[ i ].posX - (int)coptPlayerX;
        if( dist > 88 || dist < -80 )
          continue;

        if( coptBat[ i ].life > 0 )
          gbDrawBitmap( dist, coptBat[ i ].posY - coptBat[ i ].height, coptBat[ i ].sprite );
        else
          gbDrawBitmap( dist, coptBat[ i ].posY - coptBat[ i ].height, coptBat[ i ].spriteDamage );
    }
}

void coptUpdateExplosion()
{
    if( coptExState < COPT_NB_EXPLOSION_ENNEMI )
    {
        if( ( gbFrameCount % coptExFreq ) == 0 )
          coptExState++;
    }
}

void coptDrawExplosion()
{
    if( coptExState < COPT_NB_EXPLOSION_ENNEMI )
    {
        int dist = coptExPosX - (int)coptPlayerX;
        if( coptExState == 0 ) gbDrawBitmap( dist, 24, coptExplosion1 );
        if( coptExState == 1 ) gbDrawBitmap( dist, 24, coptExplosion2 );
        if( coptExState == 2 ) gbDrawBitmap( dist, 24, coptExplosion3 );
    }
}

// -----------------------------------------------------------------------------
// Missiles (missile.ino)
// -----------------------------------------------------------------------------

void coptUpdateMissile()
{
    int i;
    for( i = 0; i < COPT_NB_MISSIBLE; i++ )
    {
        if( coptMissiles[ i ].timerAlive > 0 )
        {
            if( coptMissiles[ i ].isGravity == 1 )
            {
                coptMissiles[ i ].vy = coptMissiles[ i ].vy + COPT_GRAVITE;
                if( coptMissiles[ i ].y > 40 && coptMissiles[ i ].timerAlive > 10 )
                  coptMissiles[ i ].timerAlive = 10;
            }

            coptMissiles[ i ].x = coptMissiles[ i ].x + coptMissiles[ i ].vx;
            coptMissiles[ i ].y = coptMissiles[ i ].y + coptMissiles[ i ].vy;

            if( coptMissiles[ i ].y < -10
                || fabs( coptMissiles[ i ].x - ( coptPlayerX + coptPlayerOffsetCam ) ) > 100 )
              coptMissiles[ i ].timerAlive = 0;
            else if( gbCollidePointRect( (int)coptMissiles[ i ].x, (int)coptMissiles[ i ].y,
                                          (int)coptPlayerX + ( coptPlayerOffsetCam - 11 ), (int)coptPlayerY, 22, 8 ) )
            {
                if( coptPlayerHp > 0 )
                {
                    coptPlayerHp = coptPlayerHp - coptMissiles[ i ].damage;
                    coptPlayerTimeRegenere = COPT_TIME_TO_REGENERE;
                    if( coptPlayerAngleSprite < 0 )
                      coptPlayerAngleSprite = coptPlayerAngleSprite + 0.2;
                    else
                      coptPlayerAngleSprite = coptPlayerAngleSprite - 0.2;

                    if( coptPlayerHp < 0 )
                      coptPlayerHp = 0;
                }
                coptMissiles[ i ].timerAlive = 0;
            }

            coptMissiles[ i ].timerAlive--;
            if( coptMissiles[ i ].timerAlive < 0 )
              coptMissiles[ i ].timerAlive = 0;
        }
    }
}

void coptDrawMissile()
{
    int i, dist;
    for( i = 0; i < COPT_NB_MISSIBLE; i++ )
    {
        if( coptMissiles[ i ].timerAlive > 0 )
        {
            dist = (int)coptMissiles[ i ].x - (int)coptPlayerX;
            if( gbAbsInt( dist ) > 88 )
              continue;

            if( coptMissiles[ i ].isGravity == 1 && coptMissiles[ i ].timerAlive < 10 )
            {
                if( coptMissiles[ i ].timerAlive > 5 )
                  gbDrawBitmap( dist - 4, (int)coptMissiles[ i ].y - 4, coptCharEx1 );
                else
                  gbDrawBitmap( dist - 4, (int)coptMissiles[ i ].y - 4, coptCharEx2 );
            }
            else
              gbFillCircle( dist, (int)coptMissiles[ i ].y, 1 );
        }
    }
}

// -----------------------------------------------------------------------------
// Rescapees (Rescaper.ino)
// -----------------------------------------------------------------------------

void coptDrawRescaper()
{
    int i;
    for( i = 0; i < COPT_NB_RESC; i++ )
    {
        if( coptResc[ i ].etat > 2 && gbAbsInt( coptResc[ i ].x - (int)coptPlayerX ) < 100 )
        {
            if( ( gbFrameCount % 6 ) > 2 )
              gbDrawBitmap( coptResc[ i ].x - ( (int)coptPlayerX - coptPlayerOffsetCam ), coptResc[ i ].y, coptMan1 );
            else
              gbDrawBitmap( coptResc[ i ].x - ( (int)coptPlayerX - coptPlayerOffsetCam ), coptResc[ i ].y - 1, coptMan2 );
        }
    }
}

void coptUpdateRescaper()
{
    int i;
    for( i = 0; i < COPT_NB_RESC; i++ )
    {
        if( coptResc[ i ].etat > 2 && gbAbsInt( coptResc[ i ].x - ( (int)coptPlayerX - coptPlayerOffsetCam ) ) < 100 )
        {
            if( coptPlayerMitraille )
            {
                int posX = coptResc[ i ].x - ( (int)coptPlayerX - coptPlayerOffsetCam );
                if( coptPlayerImpact > posX && coptPlayerImpact < ( posX + 3 ) )
                {
                    coptCptDeath++;
                    coptResc[ i ].etat = 0;
                }
            }

            if( coptResc[ i ].etat > 2 )
            {
                if( coptPlayerVx < 0.2 && coptPlayerY > 40 )
                {
                    coptResc[ i ].tx = (int)coptPlayerX - 5 + arand( 10 );

                    if( coptPlayerNbClient < COPT_NB_MAX_RESC_IN_COPTER )
                    {
                        if( gbAbsInt( coptResc[ i ].x - (int)coptPlayerX ) < 5 )
                        {
                            coptPlaySoundFx( 2, 0 );
                            coptPlayerNbClient++;
                            coptResc[ i ].etat = 2;
                        }
                    }
                }

                if( coptResc[ i ].tx == coptResc[ i ].x )
                {
                    coptResc[ i ].tx = coptResc[ i ].x - 10 + arand( 20 );
                    if( coptResc[ i ].tx < 100 )
                      coptResc[ i ].tx = 110 + arand( 10 );
                }
                if( coptResc[ i ].tx > coptResc[ i ].x )
                  coptResc[ i ].x++;
                else
                  coptResc[ i ].x--;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// HUD (hud.ino)
// -----------------------------------------------------------------------------

void coptDrawHud()
{
    if( coptPlayerX < 800 )
    {
        if( ( gbFrameCount % 20 ) < 15 )
          gbDrawBitmap( 10, 20, coptGo );
    }

    gbSetColor( 1 );
    gbSetFont( gbFont3x3 );

    // upstream prints real low-range Gamebuino icon glyphs (font3x3 codes
    // 31/2/3 - a plane/passenger-style icon set, see gamebuinoShim.h's own
    // header comment on the real font tables) via F("\37")-style octal
    // escapes inside a Print string. This dialect's own string-literal
    // lexer is not documented to support octal escapes at all
    // (VIRCON32_C_DIALECT.md's only charset guidance is "keep source
    // ASCII"), so each icon glyph is drawn directly via gbDrawChar()
    // instead - documented as existing for exactly this "single non-text
    // glyph" case - with gbCursorX advanced by hand to land the HUD numbers
    // exactly where upstream's own Print-based auto-advance would.
    int cx = 0;
    gbDrawChar( 31, cx, 0 );
    cx = cx + gbFontWidth;
    gbCursorX = cx;
    gbCursorY = 0;
    gbPrintNumber( coptPlayerNbClient );
    cx = gbCursorX + gbFontWidth; // upstream's own literal " " space
    gbDrawChar( 2, cx, 0 );
    cx = cx + gbFontWidth;
    gbCursorX = cx;
    gbCursorY = 0;
    gbPrintNumber( coptCptVictoire );
    gbCursorX = gbCursorX + gbFontWidth;
    gbPrintString( "!" ); // upstream's own "\41" is octal 41 = ASCII 33 = a real '!' character, not a custom icon
    gbPrintNumber( coptCptDeath );

    gbSetFont( gbFont3x5 );

    gbDrawRect( 40, 0, 12, 3 );
    gbDrawFastHLine( 41, 1, coptGetLifeHud() );

    gbDrawRect( 54, 0, 12, 3 );
    gbDrawFastHLine( 55, 1, coptGetLifeMittraille() );

    int i;
    for( i = 0; i <= coptVieRestant; i++ )
      gbDrawChar( 3, ( 80 - i * 4 ) + gbFontWidth, 0 ); // upstream's own " \03" (a space then the icon)
}

// -----------------------------------------------------------------------------
// End-game report (endGame.ino)
// -----------------------------------------------------------------------------

void coptWinScreen()
{
    int nbKill = 0;
    int nbTot = 0;
    int i;
    for( i = 0; i < COPT_NB_BAT_LVL; i++ )
    {
        if( coptBat[ i ].type > 49 )
        {
            nbTot++;
            if( coptBat[ i ].life <= 0 )
              nbKill++;
        }
    }

    // upstream never sets a font in winScreen() itself, relying on whatever
    // was last active (always font3x5 in practice - see this file's own
    // header comment on why font5x7 is never reached by any real code path
    // here); set explicitly for defensive determinism, not a behavior change.
    gbSetFont( gbFont3x5 );
    gbSetColor( 1 );

    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Save : " );
    gbPrintNumber( coptCptVictoire );

    gbCursorX = 0;
    gbCursorY = gbCursorY + gbFontHeight;
    gbPrintString( "Death : " );
    gbPrintNumber( coptCptDeath );

    gbCursorX = 0;
    gbCursorY = gbCursorY + gbFontHeight;
    gbPrintString( "Ennemie kill : " );
    gbPrintNumber( nbKill );
    gbPrintString( "/" );
    gbPrintNumber( nbTot );

    gbCursorX = 0;
    gbCursorY = gbCursorY + gbFontHeight;
    gbPrintString( "press c button" );
}

// -----------------------------------------------------------------------------
// Title / init / dispatch (titre.ino + Copter.ino's own setup()/loop())
// -----------------------------------------------------------------------------

void coptInitGame()
{
    int i;
    for( i = 0; i < COPT_NB_BAT_LVL; i++ )
    {
        if( coptBat[ i ].type == 50 ) coptBat[ i ].life = 5;
        else if( coptBat[ i ].type == 51 ) coptBat[ i ].life = 15;
        else if( coptBat[ i ].type == 52 ) coptBat[ i ].life = 15;
        else if( coptBat[ i ].type == 53 ) coptBat[ i ].life = 25;
        else if( coptBat[ i ].type == 54 ) coptBat[ i ].life = 35;
        else coptBat[ i ].life = 127; // also matches heliports/sentinels - see this file's own header comment, quirk 3
    }

    coptVieRestant = COPT_NB_LIFE;
    coptPlayerHp = COPT_MAX_LIFE;
    coptPlayerX = 1000;
    coptCptVictoire = 0;
    coptCptDeath = 0;
    coptPlayerEtat = 3;
    coptPlayerOffsetCam = 42;

    for( i = 0; i < COPT_NB_RESC; i++ )
    {
        coptResc[ i ].etat = 3;
        coptResc[ i ].x = 1100 + arand( 2000 );
        coptResc[ i ].y = 39 + arand( 5 );
        coptResc[ i ].tx = coptResc[ i ].x;
    }

    for( i = 0; i < COPT_NB_MISSIBLE; i++ )
      coptMissiles[ i ].timerAlive = 0;
}

void coptUpdateTitle()
{
    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );
    gbFontSize = 1;
    gbCursorX = 28;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );
    gbDrawBitmap( 10, 2, coptTitle );

    if( gbPressed( BTN_A ) )
    {
        coptInitGame();
        coptState = COPT_STATE_PLAY;
    }
}

void coptUpdatePlayState()
{
    if( gbPressed( BTN_C ) )
    {
        coptState = COPT_STATE_TITLE;
        return;
    }

    if( ( coptCptVictoire + coptCptDeath ) < COPT_NB_RESC && coptVieRestant > 0 )
    {
        coptUpdatePlayer();
        coptUpdateRescaper();
        coptUpdateBatiment();
        coptUpdateMissile();
        coptUpdateExplosion();
        coptDrawWorld();
        coptDrawMissile();
        coptDrawEnnemies();
        coptDrawPlayer();
        coptDrawRescaper();
        coptDrawHud();
    }
    else
      coptWinScreen();
}

void gameCopter_init()
{
    gbBegin();
    gbSetFrameRate( COPT_FRAMERATE );

    // Full reset to real hardware's own true cold-boot state - see this
    // file's own header comment, quirk 5, for why this project's own menu
    // (unlike real hardware) needs this done explicitly here.
    coptPlayerVx = 0;
    coptPlayerVy = 0;
    coptPlayerAngleSprite = 0;
    coptPlayerMitraille = false;
    coptPlayerIsEnrayer = false;
    coptPlayerImpact = 0;
    coptPlayerNbClient = 0;
    coptPlayerTimeMitraille = 0;
    coptPlayerTimeRegenere = 0;
    coptCrashTimer = 0;
    coptTimeToReclic = 0;
    coptExState = COPT_NB_EXPLOSION_ENNEMI + 1;
    coptExFreq = 5;
    coptExPosX = 0;

    int i;
    for( i = 0; i < COPT_NB_BAT_LVL; i++ )
      coptBat[ i ] = coptBatInit[ i ];
    for( i = 0; i < COPT_NB_MISSIBLE; i++ )
    {
        coptMissiles[ i ].timerAlive = 0;
        coptMissiles[ i ].damage = 0;
        coptMissiles[ i ].isGravity = 0;
        coptMissiles[ i ].x = 0;
        coptMissiles[ i ].y = 0;
        coptMissiles[ i ].vx = 0;
        coptMissiles[ i ].vy = 0;
    }
    for( i = 0; i < COPT_NB_RESC; i++ )
    {
        coptResc[ i ].etat = 0;
        coptResc[ i ].x = 0;
        coptResc[ i ].tx = 0;
        coptResc[ i ].y = 0;
    }

    coptInitGame();
    coptState = COPT_STATE_TITLE;
}

void gameCopter_update()
{
    if( !gbUpdate() ) return;


    if( coptState == COPT_STATE_TITLE ) coptUpdateTitle();
    else coptUpdatePlayState();

    gbRenderFrame();
}
