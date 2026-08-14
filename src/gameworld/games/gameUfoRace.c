#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// UFO-Race (Rodot, license: none specified - github.com/Rodot/UFO-Race). A
// top-down racing game: pilot a flying-saucer-shaped "car" (drawn as a
// simple circle with a heading line, not an actual sprite - see
// ufoDrawPlayer() below) around a 32x32-tile track full of road/sand/ice
// surfaces, one-way "power" arrow tiles, bouncy/solid obstacle blocks and a
// single start/finish tile, trying to complete a lap in the fewest possible
// engine ticks. A small on-screen minimap in the corner always shows the
// local neighborhood of open track around the player. Real per-lap times
// are kept in a 5-entry highscore table, genuinely persisted to Vircon32's
// memory card via this shim's own eepromShim.h/.c - see "EEPROM" below for
// why this is a notable first for this project, not just another game.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(N)`/`random(a,b)` became
// `arand(N)`/`a+arand(b-a)` (this dialect's own established RNG helper).
// `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a documented no-op.
// Upstream's own `byte` fields/locals all became plain `int` (no `byte`
// alias exists anywhere in this project - unlike gameAgaruino.c's own
// `byte`-as-boolean case, nothing here is genuinely boolean, so `int` is
// the right substitute throughout, not `bool`). Upstream's own two
// `switch` statements (the friction/accelerator tile lookups in
// `updatePlayer()`) became if/else-if chains - this dialect's exact
// `switch` support is unproven, matching the same caution gamePong.c/
// gameAgaruino.c/gameTaquin.c already took. `cos()`/`sin()` port unchanged:
// `math.h` (already included once, globally, by main.c ahead of every
// game file) provides real `sin`/`cos`, so upstream's own float trig
// physics needed zero rewriting beyond adding explicit `(int)` casts at
// float-to-int call/assignment boundaries this dialect requires but real
// C++ did implicitly (e.g. `getTile(player.x/16, player.y/16)`'s own
// implicit narrowing to a `byte` parameter).
//
// REAL BITMAP ART RESTORED: every one of upstream's 16 real 16x16 tile
// sprites (`road`, `power_right/down/left/up`, `sand`, `ice`, `start`,
// `block_bouncer`, `block_single`, `block_right/down/left/up/horizontal/
// vertical`) plus the 64x36 title-screen `logo` were extracted byte-for-
// byte from the real `const byte NAME[] PROGMEM = {...}` arrays in
// upstream's own world.ino/titleScreen.ino via a small script that parsed
// the real .ino source directly and counted each array's own real element
// count in Python (34 ints per 16x16 tile = 2 header + 32 body bytes, 290
// for the 64x36 logo = 2 header + 288 body bytes) rather than
// hand-transcribing or guessing - into plain `int[N] name = { width,
// height, byte0, byte1, ... }` arrays below, the exact format
// `gbDrawBitmap()` expects, no bit-repacking needed. Every real
// `gb.display.drawBitmap(...)` call site has a direct `gbDrawBitmap()`
// counterpart: `ufoDrawWorld()` draws each track tile directly (checked
// carefully for the "upstream draws a mask/fill first" bug class found
// twice in gameFlappyBirdo.c - it does NOT apply here: every one of these
// 16 tile sprites is a complete, self-contained opaque tile texture drawn
// directly onto a screen `gbUpdate()` has just freshly cleared, with
// nothing else underneath it to bleed through and no separate upstream
// mask array declared for any of them), and `ufoUpdateTitle()` draws the
// real `logo` bitmap at (0, 12) - the exact real anchor
// `Gamebuino::titleScreen()` itself draws a passed-in logo at, already
// confirmed directly against the real Gamebuino.cpp source during
// gameFlappyBirdo.c's own port (that fact is a property of the one shared
// real library function every `gb.titleScreen(logo)` call site uses, not
// something specific to that game, so reusing it here is a confirmed real
// value, not a guess). At 64x36 anchored at y=12 the logo's own bottom row
// lands exactly on LCDHEIGHT (48) with zero clipping.
//
// A GENUINE UPSTREAM QUIRK, VERIFIED BIT-BY-BIT, PRESERVED: `world[]`
// (the packed 32x32 tile grid, 2 tiles per byte) is declared with an
// explicit real size of `WORLD_W*WORLD_H/2` = 512 bytes, but upstream's own
// initializer literal only actually supplies 256 of them (16 rows) - real
// C (and this dialect) zero-fills the remaining declared elements of a
// partially-initialized global array, so real hardware's own rows 16-31 of
// the nominal 32-row world are silently *all* tile id 0 ("road"), never
// actually laid out by upstream at all. Decoding the real row-15 payload
// bit-by-bit (a Python script, not by eye) confirms this is harmless in
// practice: every nibble in that row is >=8 (a solid wall), so the
// designed track is already fully enclosed and the player can never
// actually reach the always-road rows 16-31 to notice they're blank -
// preserved exactly as upstream shipped it (`ufoWorld` below is written
// out to its full real 512-int length, real values then explicit 0s,
// rather than relying on this dialect's own partial-initializer semantics
// matching C's, to remove any doubt).
//
// A SECOND GENUINE UPSTREAM QUIRK, PRESERVED: `getTile()` (used by
// `updatePlayer()`'s own collision/friction/accelerator checks) has no
// bounds guard at all on its `x`/`y` inputs, unlike `drawMap()`'s own
// separately-guarded minimap version - reading past the real declared
// `world[]`/`ufoWorld` extent would be undefined on real AVR hardware
// (whatever's next in flash) and would read an unrelated global's value
// here (Vircon32 does not hardware-trap a plain out-of-bounds array read -
// see VIRCON32_C_DIALECT.md section 17.3's list of what *does* hard-trap,
// which is division/modulo-by-zero, `sqrt` of a negative, and
// `atan2(0,0)`, not plain memory reads). Verified this is inert in
// practice rather than just assumed: the track's own solid wall ring (rows
// 0 and 15, and the left/right border columns, all nibble values >=8 per
// the same bit-decode above) keeps the player's own tile-quantized position
// within bounds at all times, so `ufoGetTile()` below is a direct,
// unguarded port with no defensive clamp added.
//
// A THIRD GENUINE UPSTREAM QUIRK, PRESERVED: `drawMap()`'s own minimap
// culling test reads `tile_x > WORLD_W` / `tile_y > WORLD_H` (should
// arguably be `>=`, since valid tile indices only run 0..31) - an
// off-by-one that lets `getTile(32, tile_y)` execute for the minimap's own
// rightmost/bottommost edge case, reading one tile into the next packed
// row (or, for the single combination `tile_x==32 && tile_y==31`, exactly
// one int past the end of `ufoWorld`). Ported verbatim (`> UFO_WORLD_W`/
// `> UFO_WORLD_H` below, not `>=`) rather than "fixed" - this is a cosmetic
// minimap-only edge case (at most one wrong-looking pixel in the corner of
// an 18x18 overlay), not a crash risk on this platform (see the previous
// paragraph), and preserving real upstream boundary bugs by default is
// this project's own established norm.
//
// A FOURTH GENUINE UPSTREAM QUIRK, PRESERVED (but functionally inert):
// `drawWorld()`'s own two nested loops swap `WORLD_W`/`WORLD_H` between the
// `y` bound (`min(WORLD_W, ...)`) and the `x` bound (`min(WORLD_H, ...)`) -
// backwards from what the variable names suggest. Ported exactly as
// `gbMin(UFO_WORLD_W, ...)`/`gbMin(UFO_WORLD_H, ...)` in the same swapped
// positions below. Harmless: `WORLD_W`/`WORLD_H` (and this port's own
// `UFO_WORLD_W`/`UFO_WORLD_H`) are both 32, so the swap has zero actual
// effect - noted here so it doesn't look like a fresh porting mistake.
// `max()`/`min()` (Arduino macros, unavailable here, and no ternary
// operator either) became `gbMax()`/`gbMin()` (this project's own
// established real-function stand-ins).
//
// STATE MACHINE: upstream's own real control flow is a nest of blocking
// calls - `setup()` shows a blocking `gb.titleScreen(logo)` once, then
// `loop()` repeatedly calls `drawMenu()`, itself a blocking
// `gb.menu(pauseMenu, 4)` (a real library-built-in list widget, not
// hand-drawn by this game's own source at all - see "THE PAUSE MENU'S OWN
// LAYOUT IS A REASONABLE APPROXIMATION" below) dispatching via `switch` to
// `play()` (itself a blocking `while(1)` loop, exited only by a Button C
// press), `drawHighScores()` (blocking until A/B/C), or a small blocking
// System Info loop (exited by C) - with `default` (any unhandled menu
// result, including the real "Main Menu" entry, which upstream's own
// `switch` never gives its own explicit `case`) returning to the title
// screen. All of this was flattened into one explicit `UfoState` enum
// (TITLE/MENU/PLAY/HIGHSCORES/NEWHIGHSCORE/SYSINFO) dispatched from
// `gameUfoRace_update()`, matching the "blocking loop -> explicit
// resumable state" treatment used throughout this project (see
// gamePong.c's own header comment) - Button A dismisses the title screen
// (matching real `titleScreen()`'s own real dismiss button, and this
// engine's own menu-select button), Button C returns from PLAY to MENU
// (matching upstream's own real `play()` exit) and from MENU to TITLE
// (matching upstream's own real menu-cancel-or-"Main Menu" `default` case),
// and from SYSINFO back to MENU (matching upstream's own real dismiss
// button there too).
//
// THE PAUSE MENU'S OWN LAYOUT IS A REASONABLE APPROXIMATION, NOT A PORT:
// `gb.menu(pauseMenu, PAUSEMENULENGTH)` is a real, generic, built-in list
// widget implemented *inside* the Gamebuino Classic library itself (like
// `gb.titleScreen()`) - its own real pixel-level rendering isn't present
// anywhere in this game's own source to port, only the four real item
// strings it's called with (`"Play"`, `"High scores"`, `"System Info"`,
// `"Main Menu"`) and their real order, both kept exactly as upstream wrote
// them in `ufoUpdateMenu()` below. The actual on-screen layout (a simple
// UP/DOWN-navigated vertical list with a ">" cursor, confirmed via A) is
// this port's own reasonable custom rendering of that real menu, the same
// treatment gameFlappyBirdo.c's own difficulty-select screen already
// established as this project's norm for a real-but-library-internal
// upstream UI element.
//
// EEPROM - A REAL FIRST FOR THIS PROJECT: unlike every game shipped here so
// far (this project's own CLAUDE.md had flagged the still-unported
// `shipwrek` as the expected first real EEPROM consumer), UFO-Race's own
// highscore.ino genuinely calls real `EEPROM.read()`/`EEPROM.write()` -
// making this game, not shipwrek, the actual first real functional
// consumer of eepromShim.h/.c's `eeprom_read_byte()`/`eeprom_write_byte()`
// in this project. `ufoInitHighscore()`/`ufoApplyNewHighscore()` below are
// direct ports of upstream's own `initHighscore()`/the EEPROM-writing tail
// of `saveHighscore()`, keeping the real per-entry 2-byte LSB/MSB packing
// (`eeprom_write_byte(i*2, score & 0xFF)`, `eeprom_write_byte(i*2+1,
// (score>>8) & 0xFF)`) and the real bubble-sort-on-insert - but at a
// different, simpler byte layout than upstream's own real 12-bytes-per-
// entry one (10 name bytes + 2 score bytes), since names aren't stored at
// all here (see "NAME ENTRY - DROPPED, DOCUMENTED" below): just 2 bytes
// per entry, 10 bytes total across 5 entries. This isn't meant to be
// byte-compatible with a real cartridge's own EEPROM dump (a different
// game entirely on Vircon32's side, with its own independent memory-card
// slot anyway - see eepromShim.h's own header comment) - only upstream's
// real *behavior* (persisted, sorted, lowest-time-wins lap times) needs to
// survive a reboot, not a bit-for-bit-identical layout.
//
// A GENUINE UPSTREAM DEAD-CODE CHECK, PRESERVED AS-IS: `initHighscore()`'s
// own `highscore[i] = (highscore[i]==0) ? 9999 : highscore[i];` only ever
// matches a truly-zero stored value - but this shim's own EEPROM (matching
// the real ATmega328's own documented factory-erased state, per this
// project's own CLAUDE.md EEPROM section) reads fresh/unwritten cells as
// 0xFF, not 0x00, so a fresh table here decodes to 65535 per entry (LSB
// 255 + MSB 255<<8), never 0 - this `==0` check can never actually fire
// with this shim's own realistic defaults. Ported verbatim below (as a
// harmless, documented no-op) rather than silently dropped or "fixed"
// into a real sentinel check, matching this project's own established
// norm of preserving inert upstream logic rather than second-guessing it.
//
// The functional (not bit-for-bit) outcome matches real hardware too, by
// a different route worth spelling out precisely (see gameSkibuino.c's
// own header comment for a game where this same class of composition
// genuinely does NOT match real hardware, and had to be fixed): real
// `int highscore[NUM_HIGHSCORE]` is a genuine 16-bit signed AVR type, so
// the same fresh-EEPROM composition narrows to **-1** there, not 65535,
// at the point that intermediate `unsigned int` arithmetic gets assigned
// back into the signed `highscore[]` element. But real `saveHighscore()`
// takes its own `score` parameter as `unsigned int`, and comparing
// `unsigned int` against a signed `int` in C promotes the signed side to
// unsigned first - so real `score < highscore[NUM_HIGHSCORE-1]` actually
// evaluates as `score < 65535` on real hardware too (the stored -1
// reinterpreted back to unsigned for the comparison), identical to this
// dialect's own un-truncated +65535. Net effect: an entry no one has
// beaten yet DISPLAYS as -1 on real hardware vs 65535 here (a real,
// cosmetic-only difference in `gb.display.print()`'s own signed-vs-
// unsigned formatting - this shim's own `gbPrintNumber()` has no separate
// signed/unsigned overload to diverge in the first place), but the actual
// "is this a new highscore" decision every real cartridge and this port
// both make is the same, not a porting regression.
//
// FIXED FOR DISPLAY, NOT FOR COMPARISON: `ufoHighscore[i]` itself is
// deliberately left storing +65535 for a fresh entry (not real hardware's
// own -1) - this dialect has no unsigned type at all to launder a stored
// -1 back to 65535 the way real hardware's own mixed signed/unsigned
// comparison does, so storing -1 here would make `ufoApplyNewHighscore()`'s
// own `<` check permanently fail instead (the same real regression found
// and fixed in gameSkibuino.c). `ufoPrintHighscoreValue()` below instead
// special-cases only the on-screen print of that one exact sentinel value
// (65535 -> printed as -1), leaving the stored value and every comparison
// against it untouched - real hardware's own on-screen appearance is
// matched exactly with zero risk to the save logic.
//
// NAME ENTRY - DROPPED, DOCUMENTED: upstream's own `gb.getDefaultName(...)`
// + `gb.keyboard(...)` (a real on-screen text-entry widget for naming a new
// highscore) has no equivalent anywhere in this shim (no keyboard/text-
// input primitive exists at all - confirmed against gamebuinoShim.h's full
// real API surface). Per-name storage was dropped entirely rather than
// faked with a fixed placeholder string: `ufoHighscore[]` is a plain
// times-only table (matching this project's own established precedent for
// a feature with no shim equivalent, e.g. gameFlappyBirdo.c's dropped FX
// synth waveform shaping) - `ufoUpdateHighscores()` below shows only the
// five real times, right-aligned exactly like upstream's own real
// `LCDWIDTH-4*fontWidth` formula, with nothing in the name column upstream
// used to occupy (no invented replacement text was added there, to avoid
// presenting invented content as if it were real upstream layout).
//
// SOUND - APPROXIMATED, DOCUMENTED: upstream's own `highscore_sound[]` is a
// real multi-note `PROGMEM` pattern played via `gb.sound.playPattern(...)`
// - this shim has no pattern/track engine at all (only one-shot tones -
// see gamebuinoShim.h's own header comment), so entering the new-highscore
// dialog instead plays one representative `gbPlayOK()` tone, the same
// "approximate with the closest one-shot primitive, document the
// simplification" treatment gameFlappyBirdo.c already established for its
// own dropped FX synth shaping. Every other real sound call ports 1:1:
// `playOK()`/`playCancel()`/`playTick()` -> `gbPlayOK()`/`gbPlayCancel()`/
// `gbPlayTick()`.
//
// `gb.popup(F("NEW LAP!"), 20)` (a real non-blocking overlay upstream
// shows for 20 real ticks while gameplay keeps running underneath it,
// distinct from every *other* blocking dialog in this game) is ported
// directly as `gbPopup("NEW LAP!", 20)`, which auto-draws itself on top of
// everything else already drawn that frame.
//
// SYSTEM INFO - HONEST PLACEHOLDER, NOT FABRICATED DATA: upstream's own
// System Info screen reads real hardware telemetry this shim has no
// equivalent for at all (`gb.battery.voltage/level`, `gb.backlight.
// ambientLight/backlightValue`, `gb.sound.getVolume()`/`volumeMax` - none
// of these exist anywhere in gamebuinoShim.h). Rather than inventing
// plausible-looking fake numbers for them, `ufoUpdateSysInfo()` below
// keeps the real menu entry (matching upstream's own real item) but shows
// a short, honest "no sensors emulated here" message instead - the same
// spirit as this project's own established norm of dropping a feature
// outright rather than faking data for it (e.g. GRAY, the FX synth), just
// applied to a whole screen instead of one visual element.
//
// `gb.battery.show = false;` (real setup()-time cosmetic battery-icon
// suppression) was dropped outright, matching gamePong.c's own identical
// treatment of the same real-hardware-only feature.

// -----------------------------------------------------------------------------
// Real upstream tile sprites (16x16) and title logo (64x36) - byte-for-byte
// from upstream's own world.ino/titleScreen.ino (see this file's own header
// comment on how these were extracted and verified).
// -----------------------------------------------------------------------------

int ufoSpriteRoad[34]= {
    16, 16, 0x0, 0x0, 0x20, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x8, 0x4, 0x0,
    0x0, 0x0,
};

int ufoSpritePowerRight[34]= {
    16, 16, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc0, 0x0, 0xa0, 0x0, 0x90, 0x3f, 0x88, 0x20, 0x4,
    0x20, 0x2, 0x20, 0x2, 0x20, 0x4, 0x3f, 0x88, 0x0, 0x90, 0x0, 0xa0, 0x0, 0xc0, 0x0, 0x0,
    0x0, 0x0,
};

int ufoSpritePowerDown[34]= {
    16, 16, 0x0, 0x0, 0x0, 0x0, 0x7, 0xe0, 0x4, 0x20, 0x4, 0x20, 0x4, 0x20, 0x4, 0x20,
    0x4, 0x20, 0x3c, 0x3c, 0x20, 0x4, 0x10, 0x8, 0x8, 0x10, 0x4, 0x20, 0x2, 0x40, 0x1, 0x80,
    0x0, 0x0,
};

int ufoSpritePowerLeft[34]= {
    16, 16, 0x0, 0x0, 0x0, 0x0, 0x3, 0x0, 0x5, 0x0, 0x9, 0x0, 0x11, 0xfc, 0x20, 0x4,
    0x40, 0x4, 0x40, 0x4, 0x20, 0x4, 0x11, 0xfc, 0x9, 0x0, 0x5, 0x0, 0x3, 0x0, 0x0, 0x0,
    0x0, 0x0,
};

int ufoSpritePowerUp[34]= {
    16, 16, 0x0, 0x0, 0x1, 0x80, 0x2, 0x40, 0x4, 0x20, 0x8, 0x10, 0x10, 0x8, 0x20, 0x4,
    0x3c, 0x3c, 0x4, 0x20, 0x4, 0x20, 0x4, 0x20, 0x4, 0x20, 0x4, 0x20, 0x7, 0xe0, 0x0, 0x0,
    0x0, 0x0,
};

int ufoSpriteSand[34]= {
    16, 16, 0x0, 0x20, 0x20, 0x2, 0x4, 0x80, 0x80, 0x8, 0x0, 0x0, 0x10, 0x44, 0x2, 0x0,
    0x40, 0x22, 0x0, 0x0, 0x8, 0x88, 0x0, 0x1, 0x42, 0x0, 0x0, 0x40, 0x0, 0x8, 0x84, 0x0,
    0x10, 0x80,
};

int ufoSpriteIce[34]= {
    16, 16, 0x0, 0x0, 0x4, 0x10, 0x8, 0x20, 0x10, 0x40, 0x20, 0x80, 0x41, 0x0, 0x80, 0x8,
    0x0, 0x11, 0x0, 0x22, 0x4, 0x40, 0x8, 0x80, 0x10, 0x0, 0x20, 0x10, 0x0, 0x20, 0x0, 0x40,
    0x0, 0x80,
};

int ufoSpriteStart[34]= {
    16, 16, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x19, 0x98, 0x19, 0x98, 0x6, 0x60, 0x6, 0x60,
    0x19, 0x98, 0x19, 0x98, 0x6, 0x60, 0x6, 0x60, 0x19, 0x98, 0x19, 0x98, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0,
};

int ufoSpriteBlockBouncer[34]= {
    16, 16, 0xaa, 0xaa, 0x0, 0x1, 0x80, 0x0, 0xf, 0xf1, 0x9f, 0xf8, 0x1c, 0x39, 0x99, 0x98,
    0x1a, 0x59, 0x9a, 0x58, 0x19, 0x99, 0x9c, 0x38, 0x1f, 0xf9, 0x8f, 0xf0, 0x0, 0x1, 0x80, 0x0,
    0x55, 0x55,
};

int ufoSpriteBlockSingle[34]= {
    16, 16, 0xaa, 0xaa, 0x0, 0x1, 0x80, 0x0, 0xf, 0xf1, 0x9f, 0xf8, 0x1c, 0x39, 0x98, 0x18,
    0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x9c, 0x38, 0x1f, 0xf9, 0x8f, 0xf0, 0x0, 0x1, 0x80, 0x0,
    0x55, 0x55,
};

int ufoSpriteBlockRight[34]= {
    16, 16, 0xaa, 0xaa, 0x0, 0x1, 0x0, 0x0, 0xff, 0xf1, 0xff, 0xf8, 0x0, 0x39, 0x0, 0x18,
    0x0, 0x19, 0x0, 0x18, 0x0, 0x19, 0x0, 0x38, 0xff, 0xf9, 0xff, 0xf0, 0x0, 0x1, 0x0, 0x0,
    0x55, 0x55,
};

int ufoSpriteBlockDown[34]= {
    16, 16, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18,
    0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x9c, 0x38, 0x1f, 0xf9, 0x8f, 0xf0, 0x0, 0x1, 0x80, 0x0,
    0x55, 0x55,
};

int ufoSpriteBlockLeft[34]= {
    16, 16, 0xaa, 0xaa, 0x0, 0x0, 0x80, 0x0, 0xf, 0xff, 0x9f, 0xff, 0x1c, 0x0, 0x98, 0x0,
    0x18, 0x0, 0x98, 0x0, 0x18, 0x0, 0x9c, 0x0, 0x1f, 0xff, 0x8f, 0xff, 0x0, 0x0, 0x80, 0x0,
    0x55, 0x55,
};

int ufoSpriteBlockUp[34]= {
    16, 16, 0xaa, 0xaa, 0x0, 0x1, 0x80, 0x0, 0xf, 0xf1, 0x9f, 0xf8, 0x1c, 0x39, 0x98, 0x18,
    0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18,
    0x18, 0x19,
};

int ufoSpriteBlockHorizontal[34]= {
    16, 16, 0xaa, 0xaa, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0,
    0x55, 0x55,
};

int ufoSpriteBlockVertical[34]= {
    16, 16, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18,
    0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18, 0x18, 0x19, 0x98, 0x18,
    0x18, 0x19,
};

int ufoLogoBitmap[290]= {
    64, 36, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x0, 0x0, 0x0, 0x0, 0x30,
    0x40, 0x1, 0x80, 0x0, 0x0, 0x0, 0x0, 0x60, 0xe0, 0x1, 0x81, 0x9b, 0xe7, 0x80, 0x0, 0xc1,
    0xf0, 0x1, 0x81, 0x9b, 0xef, 0xc0, 0x1, 0xc1, 0xf8, 0x1, 0x81, 0x9b, 0xc, 0xc0, 0x3, 0xfd,
    0xf0, 0x1, 0x81, 0x9b, 0xc, 0xc0, 0x3, 0xfc, 0xe4, 0x1, 0x81, 0x9b, 0xc, 0xc0, 0x7, 0xf8,
    0x40, 0x1, 0x81, 0x9b, 0xec, 0xc0, 0xf, 0xf0, 0x0, 0x1, 0x81, 0x9b, 0xec, 0xc0, 0x18, 0x18,
    0xc2, 0x1, 0x81, 0x9b, 0xc, 0xc0, 0x38, 0x3c, 0xe6, 0x1, 0x81, 0x9b, 0xc, 0xc0, 0x30, 0x7f,
    0xe7, 0x1, 0x81, 0x9b, 0xc, 0xc0, 0x7f, 0xfd, 0xff, 0x1, 0x81, 0x9b, 0xc, 0xc0, 0xff, 0x3d,
    0xff, 0x81, 0x81, 0x9b, 0xc, 0xc1, 0xfe, 0x18, 0xcf, 0x81, 0x81, 0xfb, 0xf, 0xc1, 0xfc, 0x0,
    0xce, 0x41, 0x80, 0xf3, 0x7, 0x83, 0x4, 0x10, 0x6, 0x21, 0x80, 0x0, 0x0, 0x6, 0xe, 0x38,
    0x84, 0x1f, 0x80, 0x0, 0x0, 0x0, 0xf, 0x79, 0x84, 0x3f, 0x9f, 0x6, 0xf, 0x3e, 0x1f, 0xfb,
    0xce, 0x3f, 0x9f, 0x86, 0x1f, 0xbe, 0x1f, 0x7f, 0xdf, 0x7f, 0x99, 0x8f, 0x19, 0xb0, 0xe, 0x7b,
    0xff, 0x1, 0x99, 0x8f, 0x19, 0xb0, 0x6, 0x33, 0xde, 0x1, 0x99, 0x8f, 0x19, 0xb0, 0x4, 0x21,
    0x8e, 0x1, 0x9f, 0x8f, 0x18, 0x30, 0x2, 0x1, 0x4, 0x11, 0x9f, 0x9f, 0x99, 0xbe, 0x2, 0x60,
    0x7, 0xe1, 0x9f, 0x1f, 0x9b, 0x3e, 0x0, 0x73, 0x8f, 0xc1, 0x9b, 0x19, 0x98, 0x30, 0x1, 0xf7,
    0xdf, 0xc1, 0x9b, 0x19, 0x99, 0xb0, 0x0, 0xff, 0xff, 0x81, 0x9b, 0x1f, 0x99, 0xb0, 0x0, 0xf7,
    0xc0, 0x1, 0x9b, 0xbf, 0xd9, 0xb0, 0x0, 0xe3, 0x80, 0x1, 0x99, 0xb0, 0xdf, 0xbe, 0x0, 0x61,
    0x4, 0x1, 0x99, 0xb0, 0xcf, 0x3e, 0x0, 0x0, 0x0, 0x1, 0x80, 0x0, 0x0, 0x0, 0x65, 0x33,
    0xb3, 0xb9, 0x80, 0x1, 0x80, 0x0, 0x72, 0x3a, 0xaa, 0x91, 0xff, 0xff, 0xff, 0xff, 0x72, 0x2b,
    0xb3, 0x93,
};

#define UFO_NUM_SPRITES 16

int* ufoSprites[UFO_NUM_SPRITES]=
{
    ufoSpriteRoad,             // 0
    ufoSpritePowerRight,       // 1
    ufoSpritePowerDown,        // 2
    ufoSpritePowerLeft,        // 3
    ufoSpritePowerUp,          // 4
    ufoSpriteSand,             // 5
    ufoSpriteIce,              // 6
    ufoSpriteStart,            // 7
    ufoSpriteBlockBouncer,     // 8
    ufoSpriteBlockSingle,      // 9
    ufoSpriteBlockRight,       // A
    ufoSpriteBlockDown,        // B
    ufoSpriteBlockLeft,        // C
    ufoSpriteBlockUp,          // D
    ufoSpriteBlockHorizontal,  // E
    ufoSpriteBlockVertical,    // F
};

// -----------------------------------------------------------------------------
// Real upstream packed tile map (32x32, 2 tiles/byte) - see this file's own
// header comment on the real 256-real-values/256-zero-fill split found while
// extracting this verbatim from world.ino.
// -----------------------------------------------------------------------------

#define UFO_WORLD_W 32
#define UFO_WORLD_H 32

int ufoWorld[512]= {
    0xDC, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xAD,
    0xF0, 0x01, 0x71, 0x00, 0x00, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x0D, 0xD0, 0x00, 0x00, 0x05, 0x5F,
    0xF0, 0x0C, 0xEE, 0xEE, 0xEE, 0xEA, 0x00, 0xF0, 0x0D, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x5F,
    0xF0, 0x00, 0xD5, 0x55, 0x00, 0x5D, 0x00, 0xF0, 0x0F, 0xD0, 0x0F, 0xF0, 0x0D, 0x50, 0x00, 0x0F,
    0xF0, 0x80, 0xF5, 0x00, 0x00, 0x0F, 0x00, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xCA, 0x80, 0x0F,
    0xF0, 0x00, 0xF0, 0x00, 0xD0, 0x0B, 0x00, 0xF0, 0x4F, 0xF0, 0x0F, 0xF0, 0x0F, 0x55, 0x00, 0x0F,
    0xF0, 0x80, 0xF0, 0xD0, 0xF5, 0x00, 0x05, 0xF4, 0x4F, 0xF0, 0x0B, 0xB0, 0x0F, 0x50, 0x00, 0x5F,
    0xF0, 0x00, 0xF0, 0xB0, 0xF5, 0x55, 0x55, 0xF4, 0x6F, 0xF0, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x5F,
    0xF0, 0x80, 0xF0, 0x00, 0xBC, 0xEE, 0xEA, 0xB6, 0x6F, 0xF0, 0x00, 0x00, 0x0F, 0x00, 0x8C, 0xAF,
    0xF0, 0x00, 0xF5, 0x00, 0x00, 0x00, 0x66, 0x66, 0x6F, 0xBC, 0xEE, 0xEE, 0xAF, 0x50, 0x00, 0x5F,
    0xF0, 0x40, 0xB5, 0x55, 0x00, 0x06, 0x66, 0x66, 0x6F, 0x00, 0x00, 0x00, 0x0B, 0x55, 0x00, 0x0F,
    0xF5, 0x00, 0x0C, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xAB, 0x00, 0x00, 0x00, 0xCE, 0xEA, 0x00, 0x0F,
    0xF5, 0x50, 0x00, 0xCE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEA, 0x00, 0x5F,
    0xF5, 0x55, 0x00, 0x00, 0x00, 0x00, 0x55, 0xCE, 0xEA, 0x55, 0x08, 0x88, 0x88, 0x00, 0x00, 0x5F,
    0xF5, 0x55, 0x50, 0x00, 0x00, 0x00, 0x00, 0x03, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x5F,
    0xBC, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xA8, 0x88, 0x88, 0xCE, 0xEE, 0xAB,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

// Real upstream `"\25:Save \27:Exit"` prompt from drawNewHighscore() - \25
// (octal, ASCII 21) and \27 (octal, ASCII 23) are real Gamebuino icon
// glyphs from the same low-ASCII custom-icon range gameTaquin.c's own
// `taqRestartText` already restored (see that file's own header comment);
// their real pictograms aren't independently verified here, but they are
// preserved as the real glyph codes upstream used rather than dropped or
// guessed at, matching this project's established norm for this glyph
// range - built as an explicit int array since a plain quoted string
// literal can't hold a non-printable low-ASCII code.
char ufoSaveExitText[14]=
{
    21, 58, 83, 97, 118, 101, 32, // real icon glyph, ":Save "
    23, 58, 69, 120, 105, 116,    // real icon glyph, ":Exit"
    0
};

typedef struct
{
    float x, y, v, vx, vy, angle;
    int radius;
} UfoPlayer;

enum UfoState
{
    UFO_STATE_TITLE = 0,
    UFO_STATE_MENU = 1,
    UFO_STATE_PLAY = 2,
    UFO_STATE_HIGHSCORES = 3,
    UFO_STATE_NEWHIGHSCORE = 4,
    UFO_STATE_SYSINFO = 5
};

#define UFO_MENU_COUNT 4
#define UFO_NUM_HIGHSCORE 5

UfoPlayer ufoPlayer;

int ufoState;
int ufoMenuIndex;

// Real upstream globals - never reset by ufoBeginPlay() (see that
// function's own comment for why: matching upstream's own initGame(),
// which never touches camera_x/camera_y either).
int ufoCameraX;
int ufoCameraY;

int ufoTime;
bool ufoCountingTime;

int ufoHighscore[UFO_NUM_HIGHSCORE];
int ufoPendingTime;          // the lap time awaiting a save/cancel decision in UFO_STATE_NEWHIGHSCORE
int ufoHighscoreReturnState; // which state UFO_STATE_HIGHSCORES should return to once dismissed

// Direct port of upstream's own getTile() - see this file's own header
// comment on why this stays deliberately unguarded, matching upstream.
int ufoGetTile( int x, int y )
{
    if( ( x & 1 ) != 0 ) // odd
      return ufoWorld[ y * ( UFO_WORLD_W / 2 ) + x / 2 ] & 0xF;
    else // even
      return ( ufoWorld[ y * ( UFO_WORLD_W / 2 ) + x / 2 ] >> 4 );
}

// DISPLAY-ONLY cosmetic fix, not a functional one - see this file's own
// header comment for the full derivation: a freshly-erased, never-beaten
// entry is genuinely stored as 65535 here (needed for `ufoApplyNewHighscore()`'s
// own `<` comparison to keep working correctly - this dialect has no
// unsigned type to launder a stored -1 back the way real hardware's own
// mixed signed/unsigned comparison does), but real hardware's own
// `gb.display.print()` of the exact same fresh entry shows -1, since the
// raw stored AVR value really is -1 there. Printing 65535 here would be a
// real, avoidable cosmetic mismatch even though the underlying comparison
// logic is already correct - this helper prints -1 for that one specific
// sentinel value and the real number for every other (i.e. every real,
// already-saved) entry, matching real hardware's own on-screen appearance
// exactly without touching the stored value itself or its own comparison.
void ufoPrintHighscoreValue( int value )
{
    if( value == 65535 )
      gbPrintNumber( -1 );
    else
      gbPrintNumber( value );
}

// Direct port of upstream's own initHighscore() - see this file's own
// header comment on the simpler 2-bytes-per-entry EEPROM layout (no name
// storage) and the preserved-but-inert `==0` dead-code check.
void ufoInitHighscore()
{
    int i;
    for( i = 0; i < UFO_NUM_HIGHSCORE; i++ )
    {
        int lsb = eeprom_read_byte( i * 2 );
        int msb = eeprom_read_byte( i * 2 + 1 );
        ufoHighscore[ i ] = ( lsb & 0xFF ) + ( ( msb << 8 ) & 0xFF00 );
        if( ufoHighscore[ i ] == 0 )
          ufoHighscore[ i ] = 9999; // see header comment - dead in practice with this shim's own real 0xFF-fresh EEPROM
    }
}

// Direct port of upstream's own EEPROM-writing tail of saveHighscore() (the
// bubble-sort-on-insert plus the per-entry byte write) - the name-shuffling
// half of upstream's own version is dropped along with name storage itself
// (see header comment).
void ufoApplyNewHighscore( int score )
{
    ufoHighscore[ UFO_NUM_HIGHSCORE - 1 ] = score;

    int i;
    for( i = UFO_NUM_HIGHSCORE - 1; i > 0; i-- )
    {
        if( ufoHighscore[ i - 1 ] > ufoHighscore[ i ] )
        {
            int temp = ufoHighscore[ i - 1 ];
            ufoHighscore[ i - 1 ] = ufoHighscore[ i ];
            ufoHighscore[ i ] = temp;
        }
        else
          break;
    }

    for( i = 0; i < UFO_NUM_HIGHSCORE; i++ )
    {
        eeprom_write_byte( i * 2, ufoHighscore[ i ] & 0xFF );
        eeprom_write_byte( i * 2 + 1, ( ufoHighscore[ i ] >> 8 ) & 0xFF );
    }
}

// Direct port of upstream's own initPlayer().
void ufoInitPlayer()
{
    ufoPlayer.radius = 3;
    ufoPlayer.x = 20;
    ufoPlayer.y = 20;
    ufoPlayer.v = 0;
    ufoPlayer.vx = 0;
    ufoPlayer.vy = 0;
    ufoPlayer.angle = 0;
}

// Direct port of upstream's own initTime().
void ufoInitTime()
{
    ufoTime = 0;
    ufoCountingTime = false;
}

void ufoBeginTitle()
{
    ufoState = UFO_STATE_TITLE;
    gbSetFont( gbFont5x7 );
}

void ufoBeginMenu()
{
    ufoState = UFO_STATE_MENU;
    ufoMenuIndex = 0;
    gbSetFont( gbFont5x7 );
}

// == upstream initGame() + play()'s own real font3x5 switch. Deliberately
// does NOT touch ufoCameraX/ufoCameraY - matching upstream's own initGame()
// exactly, which never resets camera_x/camera_y either, so the camera
// smoothly re-converges onto the reset player position over the next few
// ticks instead of snapping - a real upstream behavior, not a missed reset.
void ufoBeginPlay()
{
    ufoInitPlayer();
    ufoInitTime();
    gbSetFont( gbFont3x5 );
    ufoState = UFO_STATE_PLAY;
}

void ufoBeginHighscores( int returnState )
{
    ufoHighscoreReturnState = returnState;
    gbSetFont( gbFont5x7 );
    ufoState = UFO_STATE_HIGHSCORES;
}

// See header comment on SOUND for why this plays one representative
// gbPlayOK() tone rather than upstream's own real highscore_sound[] pattern.
void ufoBeginNewHighscore( int score )
{
    ufoPendingTime = score;
    gbSetFont( gbFont5x7 );
    ufoState = UFO_STATE_NEWHIGHSCORE;
    gbPlayOK();
}

void ufoBeginSysInfo()
{
    gbSetFont( gbFont5x7 );
    ufoState = UFO_STATE_SYSINFO;
}

// == upstream drawTitleScreen()'s own real gb.titleScreen(logo) call -
// dismissed by a genuine fresh Button A press (this engine's own
// menu-select button, matching real titleScreen()'s own real dismiss
// button - see gamePong.c's own header comment for why).
void ufoUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( 0, 12, ufoLogoBitmap );
    gbCursorX = 21;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      ufoBeginMenu();
}

// == upstream drawMenu()'s own real gb.menu(pauseMenu, 4) - see header
// comment on why this screen's own pixel layout is a reasonable custom
// approximation of a real library-internal widget, while the four real
// item strings/order are upstream's own.
void ufoUpdateMenu()
{
    gbSetColor( 1 );
    gbCursorX = 18;
    gbCursorY = 0;
    gbPrintString( "UFO-RACE" );

    int i;
    for( i = 0; i < UFO_MENU_COUNT; i++ )
    {
        gbCursorX = 10;
        gbCursorY = 16 + i * 8;
        if( i == ufoMenuIndex )
          gbPrintString( ">" );

        gbCursorX = 16;
        gbCursorY = 16 + i * 8;
        if( i == 0 )
          gbPrintString( "PLAY" );
        else if( i == 1 )
          gbPrintString( "HIGH SCORES" );
        else if( i == 2 )
          gbPrintString( "SYSTEM INFO" );
        else
          gbPrintString( "MAIN MENU" );
    }

    if( gbPressed( BTN_UP ) )
    {
        gbPlayTick();
        ufoMenuIndex = ufoMenuIndex - 1;
        if( ufoMenuIndex < 0 )
          ufoMenuIndex = UFO_MENU_COUNT - 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        gbPlayTick();
        ufoMenuIndex = ufoMenuIndex + 1;
        if( ufoMenuIndex > UFO_MENU_COUNT - 1 )
          ufoMenuIndex = 0;
    }

    // real `default:` case (any unhandled gb.menu() result, including the
    // real "Main Menu" entry, which upstream's own switch never gives its
    // own explicit case) - see header comment.
    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        ufoBeginTitle();
    }

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( ufoMenuIndex == 0 )
          ufoBeginPlay();
        else if( ufoMenuIndex == 1 )
          ufoBeginHighscores( UFO_STATE_MENU );
        else if( ufoMenuIndex == 2 )
          ufoBeginSysInfo();
        else
          ufoBeginTitle();
    }
}

// Direct port of upstream's own real drawHighScores() (minus the dropped
// name column - see header comment), including the real wobble effect on
// the header text.
void ufoUpdateHighscores()
{
    gbSetColor( 1 );
    gbCursorX = 9 + arand( 2 );
    gbCursorY = 0 + arand( 2 );
    gbPrintString( "BEST TIMES" );

    int thisScore;
    for( thisScore = 0; thisScore < UFO_NUM_HIGHSCORE; thisScore++ )
    {
        gbCursorX = LCDWIDTH - 4 * gbFontWidth;
        gbCursorY = gbFontHeight + gbFontHeight * thisScore;
        ufoPrintHighscoreValue( ufoHighscore[ thisScore ] );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        if( ufoHighscoreReturnState == UFO_STATE_PLAY )
          ufoBeginPlay();
        else
          ufoBeginMenu();
    }
}

// Direct port of upstream's own real drawNewHighscore(), including the real
// wobble effect and the real multi-line "\n"-driven print sequence (see
// header comment on gbPrintString()'s own real '\n' support making this
// practical as a direct port rather than a manually-recomputed layout).
void ufoUpdateNewHighscore()
{
    gbSetColor( 1 );
    gbCursorX = 2 + arand( 2 );
    gbCursorY = 0 + arand( 2 );
    gbPrintString( "NEW HIGHSCORE" );

    gbCursorX = 0;
    gbCursorY = 12;
    gbPrintString( "Your time " );
    gbPrintNumber( ufoPendingTime );
    gbPrintString( "\nBest      " );
    ufoPrintHighscoreValue( ufoHighscore[ 0 ] );
    gbPrintString( "\nWorst     " );
    ufoPrintHighscoreValue( ufoHighscore[ UFO_NUM_HIGHSCORE - 1 ] );

    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( ufoSaveExitText );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        ufoApplyNewHighscore( ufoPendingTime );
        ufoBeginHighscores( UFO_STATE_PLAY );
    }
    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        ufoBeginPlay();
    }
}

// See header comment on SYSTEM INFO for why this is an honest placeholder
// rather than fabricated sensor data.
void ufoUpdateSysInfo()
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "SYSTEM INFO" );
    gbCursorX = 0;
    gbCursorY = 16;
    gbPrintString( "No sensors" );
    gbCursorX = 0;
    gbCursorY = 24;
    gbPrintString( "emulated here" );
    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( "C: Back" );

    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        ufoBeginMenu();
    }
}

// Direct port of upstream's own real updatePlayer() - see header comment
// for the switch->if/else-if rewrites and the two preserved real bugs
// (unguarded getTile(), and the WORLD_W/WORLD_H swap that doesn't apply to
// this particular function).
void ufoUpdatePlayer()
{
    if( gbRepeat( BTN_RIGHT, 1 ) )
      ufoPlayer.angle = ufoPlayer.angle + 0.31415 / 2;
    if( gbRepeat( BTN_LEFT, 1 ) )
      ufoPlayer.angle = ufoPlayer.angle - 0.31415 / 2;
    if( gbRepeat( BTN_A, 1 ) )
      ufoPlayer.v = ufoPlayer.v + 0.02;
    if( gbRepeat( BTN_B, 1 ) )
    {
        ufoPlayer.v = ufoPlayer.v * 0.8;
        ufoPlayer.vx = ufoPlayer.vx * 0.8;
        ufoPlayer.vy = ufoPlayer.vy * 0.8;
    }

    int currentTile = ufoGetTile( (int)( ufoPlayer.x / 16 ), (int)( ufoPlayer.y / 16 ) );

    // friction (upstream switch -> if/else-if, matching gamePong.c/
    // gameTaquin.c's own established caution around this dialect's
    // unproven switch support)
    if( currentTile == 0 ) // road
    {
        ufoPlayer.v = ufoPlayer.v * 0.95;
        ufoPlayer.vx = ufoPlayer.vx * 0.9;
        ufoPlayer.vy = ufoPlayer.vy * 0.9;
    }
    else if( currentTile == 5 ) // sand
    {
        ufoPlayer.v = ufoPlayer.v * 0.9;
        ufoPlayer.vx = ufoPlayer.vx * 0.8;
        ufoPlayer.vy = ufoPlayer.vy * 0.8;
    }
    else if( currentTile == 6 ) // ice
    {
        ufoPlayer.v = ufoPlayer.v * 0.7;
        ufoPlayer.vx = ufoPlayer.vx * 1;
        ufoPlayer.vy = ufoPlayer.vy * 1;
    }
    else // real upstream default: same as road
    {
        ufoPlayer.v = ufoPlayer.v * 0.95;
        ufoPlayer.vx = ufoPlayer.vx * 0.9;
        ufoPlayer.vy = ufoPlayer.vy * 0.9;
    }

    // accelerator tiles (upstream switch's own real case-1/case-7
    // fallthrough became an `||` below)
    if( currentTile == 1 || currentTile == 7 ) // right / start (avoids stopping dead on the start tile)
      ufoPlayer.vx = ufoPlayer.vx + 1;
    else if( currentTile == 2 ) // down
      ufoPlayer.vy = ufoPlayer.vy + 1;
    else if( currentTile == 3 ) // left
      ufoPlayer.vx = ufoPlayer.vx - 1;
    else if( currentTile == 4 ) // up
      ufoPlayer.vy = ufoPlayer.vy - 1;

    ufoPlayer.vx = ufoPlayer.vx + cos( ufoPlayer.angle ) * ufoPlayer.v;
    ufoPlayer.vy = ufoPlayer.vy + sin( ufoPlayer.angle ) * ufoPlayer.v;

    // collision on the x axis
    ufoPlayer.x = ufoPlayer.x + ufoPlayer.vx;
    currentTile = ufoGetTile( (int)( ufoPlayer.x / 16 ), (int)( ufoPlayer.y / 16 ) );
    if( currentTile >= 8 )
    {
        ufoPlayer.x = ufoPlayer.x - ufoPlayer.vx;
        if( currentTile == 8 ) // bouncer
        {
            if( ufoPlayer.vx >= 0 )
              ufoPlayer.vx = -6;
            else
              ufoPlayer.vx = 6;
            gbPlayOK();
        }
        else // regular block
        {
            ufoPlayer.vx = ufoPlayer.vx * -0.5;
            gbPlayTick();
        }
        ufoPlayer.v = ufoPlayer.v * 0.5;
    }

    // collision on the y axis
    ufoPlayer.y = ufoPlayer.y + ufoPlayer.vy;
    currentTile = ufoGetTile( (int)( ufoPlayer.x / 16 ), (int)( ufoPlayer.y / 16 ) );
    if( currentTile >= 8 )
    {
        ufoPlayer.y = ufoPlayer.y - ufoPlayer.vy;
        if( currentTile == 8 ) // bouncer
        {
            if( ufoPlayer.vy >= 0 )
              ufoPlayer.vy = -6;
            else
              ufoPlayer.vy = 6;
            gbPlayOK();
        }
        else // regular block
        {
            ufoPlayer.vy = ufoPlayer.vy * -0.5;
            gbPlayTick();
        }
        ufoPlayer.v = ufoPlayer.v * 0.5;
    }

    // camera smoothing - real upstream int arithmetic (a truncating
    // integer division every tick), preserved exactly rather than
    // "upgraded" to float smoothing.
    int cameraXTarget = (int)( ufoPlayer.x + cos( ufoPlayer.angle ) * ufoPlayer.v * 64 - LCDWIDTH / 2 );
    int cameraYTarget = (int)( ufoPlayer.y + sin( ufoPlayer.angle ) * ufoPlayer.v * 64 - LCDHEIGHT / 2 );
    ufoCameraX = ( ufoCameraX * 3 + cameraXTarget ) / 4;
    ufoCameraY = ( ufoCameraY * 3 + cameraYTarget ) / 4;
}

// Direct port of upstream's own real drawWorld() - see header comment on
// the preserved (but inert) WORLD_W/WORLD_H loop-bound swap.
void ufoDrawWorld()
{
    gbSetColor( 1 );

    int yLo = gbMax( 0, ufoCameraY / 16 );
    int yHi = gbMin( UFO_WORLD_W, ( ufoCameraY + LCDHEIGHT ) / 16 + 1 ); // real upstream swap - see header comment
    int xLo = gbMax( 0, ufoCameraX / 16 );
    int xHi = gbMin( UFO_WORLD_H, ( ufoCameraX + LCDWIDTH ) / 16 + 1 ); // real upstream swap - see header comment

    int x, y;
    for( y = yLo; y < yHi; y++ )
    {
        for( x = xLo; x < xHi; x++ )
        {
            int spriteID = ufoGetTile( x, y );
            int xScreen = x * 16 - ufoCameraX;
            int yScreen = y * 16 - ufoCameraY;
            gbDrawBitmap( xScreen, yScreen, ufoSprites[ spriteID ] );
        }
    }
}

// Direct port of upstream's own real drawMap() (the corner minimap) - see
// header comment on the preserved `> UFO_WORLD_W`/`> UFO_WORLD_H` off-by-one
// and on relying on the previous tick's own drawPlayer() to have left the
// draw color at BLACK (matching upstream's own identical reliance on
// cross-call color state, no redundant setColor added here).
void ufoDrawMap()
{
    gbFillRect( 0, 0, 18, 18 );
    gbSetColor( 0 );

    int x, y;
    for( y = 0; y < 16; y++ )
    {
        for( x = 0; x < 16; x++ )
        {
            int tileX = x + (int)( ufoPlayer.x / 16 ) - 8;
            int tileY = y + (int)( ufoPlayer.y / 16 ) - 8;
            if( tileX < 0 || tileX > UFO_WORLD_W || tileY < 0 || tileY > UFO_WORLD_H )
              continue;
            if( ufoGetTile( tileX, tileY ) < 8 )
              gbDrawPixel( x + 1, y + 1 );
        }
    }

    gbSetColor( 1 );
    gbDrawPixel( 9, 9 );
}

// Direct port of upstream's own real drawTime().
void ufoDrawTimeHud()
{
    gbCursorX = 0;
    gbCursorY = LCDHEIGHT - gbFontHeight;
    gbSetColorBg( 1, 0 ); // real setColor(BLACK, WHITE) - opaque text background
    gbPrintNumber( ufoTime );
}

// Direct port of upstream's own real drawPlayer().
void ufoDrawPlayer()
{
    int xScreen = (int)ufoPlayer.x - ufoCameraX;
    int yScreen = (int)ufoPlayer.y - ufoCameraY;
    if( !( xScreen < -16 || xScreen > LCDWIDTH || yScreen < -16 || yScreen > LCDHEIGHT ) )
    {
        gbFillCircle( xScreen, yScreen, ufoPlayer.radius );
        gbSetColor( 0 );
        gbDrawLine( xScreen, yScreen, xScreen + (int)( cos( ufoPlayer.angle ) * 4 ), yScreen + (int)( sin( ufoPlayer.angle ) * 4 ) );
        gbSetColor( 1 );
    }
}

// == upstream updateTime() - see header comment for the full reasoning
// behind converting its own nested blocking dialogs into explicit state
// transitions. Only ever called while ufoState==UFO_STATE_PLAY.
void ufoUpdateTimeCheckpoint()
{
    int tile = ufoGetTile( (int)( ufoPlayer.x / 16 ), (int)( ufoPlayer.y / 16 ) );

    if( tile == 7 )
    {
        if( !ufoCountingTime )
        {
            ufoCountingTime = true;
            ufoTime = 0;
        }
        else
        {
            if( ufoTime > 100 )
            {
                if( ufoTime < ufoHighscore[ UFO_NUM_HIGHSCORE - 1 ] )
                  ufoBeginNewHighscore( ufoTime );
                else
                {
                    ufoBeginPlay(); // real initTime()+initPlayer() reset
                    gbPopup( "NEW LAP!", 20 );
                }
                return;
            }
        }
    }

    if( ufoCountingTime )
      ufoTime = ufoTime + 1;
}

// == upstream play()'s own real `while(1){ if(gb.update()){...} }` body.
void ufoUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        ufoBeginMenu();
        return;
    }

    ufoUpdatePlayer();
    ufoUpdateTimeCheckpoint();

    // a lap just completed and transitioned to a dialog/reset this same
    // tick - see ufoUpdateTimeCheckpoint()'s own header comment.
    if( ufoState != UFO_STATE_PLAY )
      return;

    ufoDrawWorld();
    ufoDrawMap();
    ufoDrawTimeHud();
    ufoDrawPlayer();
    // real gb.popup()'s own overlay (the "NEW LAP!" notification) is drawn
    // automatically by gbRenderFrame() below - see header comment.
}

void gameUfoRace_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 ); // real upstream setup()'s own `gb.display.setFont(font5x7)`
    ufoInitHighscore();
    ufoCameraX = 0;
    ufoCameraY = 0;
    ufoBeginTitle();
}

void gameUfoRace_update()
{
    if( !gbUpdate() ) return;

    if( ufoState == UFO_STATE_TITLE ) ufoUpdateTitle();
    else if( ufoState == UFO_STATE_MENU ) ufoUpdateMenu();
    else if( ufoState == UFO_STATE_PLAY ) ufoUpdatePlay();
    else if( ufoState == UFO_STATE_HIGHSCORES ) ufoUpdateHighscores();
    else if( ufoState == UFO_STATE_NEWHIGHSCORE ) ufoUpdateNewHighscore();
    else ufoUpdateSysInfo();

    gbRenderFrame();
}
