#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// CrazyTown (Clement83/Clement Quintard, http://quintard.me, license: none
// specified - github.com/Clement83/CrazyTown; taxi graphics credited
// upstream to "Quirby64"). A top-down taxi-driving game: steer a taxi
// (rotate + accelerate/brake, not a D-pad walker) around an open 128x128-
// tile world, pick up "clients" (French for "customers"/passengers - a
// single-player game, nothing networked) scattered around the map, and
// drive each one to a randomly-chosen destination before a countdown timer
// (selectable 3/5/10 real minutes) runs out. A compass needle on the HUD
// always points toward the current passenger's drop-off point (or toward
// nothing once no one is aboard). A 5-entry EEPROM-backed high-score table
// persists across sessions.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the general pattern).
// `random(N)`/`random(a,b)` became `arand(N)`/`a+arand(b-a)` (this
// dialect's own established RNG helper). `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op. `pow(x,2)` was simplified to
// `x*x` and `sqrt(pow(v,2))` to real `fabs(v)` (math.h provides `fabs`
// directly per this dialect's own documented math.h surface) - both
// mathematically identical to what upstream wrote, just without an
// unneeded `pow`/`sqrt` call. Upstream's own `Ufo` struct (x,y,v,vx,vy,
// angle,byte radius) ported as `struct TownPlayer` with `radius` widened
// to `int` (no `byte` alias exists in this project) - `radius` is a real,
// harmless piece of dead upstream data: assigned once in `initPlayer()`
// and never actually read anywhere else in the whole source (drawing uses
// the taxi bitmap's own fixed size, not the radius), kept only for
// structural fidelity.
//
// STATE MACHINE: upstream's own real control flow is entirely blocking
// calls - `setup()` shows a blocking `gb.titleScreen(logo)` once, then
// `loop()` repeatedly calls `drawMenu()`, itself a blocking `gb.menu(
// pauseMenu, 7)` (a real library-built-in list widget, not hand-drawn by
// this game's own source - see "THE PAUSE MENU IS A REASONABLE
// APPROXIMATION" below) dispatching via `switch` to `play()` (a blocking
// `while(1)` loop, exited by Button C or by the timer reaching 0),
// `drawMiniMap()` (a second blocking `while(1)` loop), or `drawHighScores()`
// (blocking until A/B/C) - with `default` (any unhandled `gb.menu()`
// result, including the real "Main Menu" entry, which upstream's own
// `switch` never gives its own explicit `case`) returning to the title
// screen. All of this was flattened into one explicit `TownState` enum
// (TITLE/MENU/PLAY/GAMEOVER/HIGHSCORES/MINIMAP) dispatched from
// `gameCrazyTown_update()`, matching the "blocking loop -> explicit
// resumable state" treatment used throughout this project (see
// gamePong.c's own header comment and gameUfoRace.c's own near-identical
// conversion). Button A dismisses the title screen (matching real
// `titleScreen()`'s own real dismiss button, and this engine's own
// menu-select button), Button C returns from PLAY to MENU (matching
// upstream's own real `play()` pause behavior) and from MENU to TITLE
// (matching upstream's own real menu-cancel/"Main Menu" `default` case),
// and A/B/C all dismiss HIGHSCORES and MINIMAP back to MENU (matching
// upstream's own real dismiss buttons in each case).
//
// THE PAUSE MENU IS A REASONABLE APPROXIMATION, NOT A PORT: `gb.menu(
// pauseMenu, PAUSEMENULENGTH)` is a real, generic, built-in list widget
// implemented *inside* the Gamebuino Classic library itself (like
// `gb.titleScreen()`) - its own real pixel-level rendering isn't present
// anywhere in this game's own source to port, only the seven real item
// strings it's called with ("Continue", "3 min play", "5 min play",
// "10 min play", "Map", "High scores", "Main Menu") and their real order,
// both kept exactly as upstream wrote them in `townUpdateMenu()` below.
// The actual on-screen layout (a compact UP/DOWN-navigated vertical list
// with a ">" cursor) is this port's own reasonable custom rendering of
// that real menu, the same treatment gameUfoRace.c's/gameFlappyBirdo.c's
// own library-internal-widget screens already established as this
// project's norm - font3x5 is forced explicitly here (this port's own
// deliberate choice so all 7 items fit the 48px-tall screen), unlike the
// PLAY/GAMEOVER/HUD screens below, which are genuine literal ports of real
// upstream drawing code and therefore preserve upstream's own real
// setFont() call sites (and lack thereof) exactly - see the per-function
// comments below for which is which.
//
// TITLE SCREEN: `gb.titleScreen(logo)` also draws a second, real built-in
// `gamebuinoLogo` boot-branding bitmap (confirmed directly in the real
// Gamebuino.cpp source) underneath every game's own custom logo - already
// an established, project-wide simplification to drop (gamePong.c and
// gameUfoRace.c's own title screens never reproduce it either, being
// purely cosmetic real-hardware branding), not a fresh oversight here.
// `townUpdateTitle()` below draws only CrazyTown's own real 64x36 `logo`
// bitmap at the real anchor `Gamebuino::titleScreen()` itself uses (y=12
// when no name string is passed, confirmed directly against the real
// source - lands the logo's own bottom row exactly on LCDHEIGHT=48) plus
// a "PRESS A" prompt, reusing gameUfoRace.c's own already-verified exact
// prompt position.
//
// REAL BITMAP ART RESTORED: Client1 (8x8, a passenger icon), GrandTaxi
// (24x9, the player's own taxi), the 64x36 title `logo`, and all 7 real
// 16x16 world tile sprites (Eau/Fleure/Foret/Pavillon/RoofTop/Usine/
// Usine2) were extracted byte-for-byte from upstream's own real
// `const byte NAME[] PROGMEM = {...}` arrays via a small script that
// parsed the real .ino source directly and cross-checked each array's own
// element count against its declared width/height (e.g. 34 ints per 16x16
// tile = 2 header + 32 body bytes; 2048 for the full 128x128 `world[]`
// bitmask = exactly WORLD_W*WORLD_H/8, confirmed fully populated with no
// partial-initializer zero-fill quirk this time, unlike gameUfoRace.c's
// own `world[]`) rather than hand-transcribed or guessed - into plain
// `int[N] name = { width, height, byte0, byte1, ... }` arrays below, the
// exact format `gbDrawBitmap()` expects. Checked the real "does upstream
// draw a separate fill/mask layer underneath this bitmap first"
// bleed-through bug class (found twice already in gameFlappyBirdo.c) for
// every one of these: it does NOT apply to any of them - Client1 and
// every world tile are complete, self-contained opaque tile textures drawn
// directly onto a screen `gbUpdate()` has just freshly cleared (matching
// gameUfoRace.c's own identical conclusion for its own tile sprites), and
// GrandTaxi is drawn one `gbDrawPixel()` at a time with no separate fill
// layer for the same reason to omit.
//
// GRANDTAXI NEEDS GENUINE CONTINUOUS-ANGLE ROTATION, NOT `gbDrawBitmapRotated()`:
// upstream's own `drawBitmapAngle()` (AffichageHelper.ino) is a completely
// custom per-pixel rotation helper - NOT a call into real `Display::
// drawBitmap(x,y,bitmap,rotation,flip)`'s own fixed 4-way NOROT/ROTCCW/
// ROT180/ROTCW rotation - it rotates the taxi sprite by the player's own
// real, continuous `float angle` (via `sin`/`cos`), something the real
// Display class itself never supported at all (upstream re-implements this
// from scratch by calling `gb.display.drawPixel()` directly per set bit,
// exactly like this port's own `townDrawBitmapAngle()` does). This is NOT
// a shim gap - there is no real-hardware primitive this could have called
// into instead, upstream wrote its own workaround too - so
// `townDrawBitmapAngle()` below is a direct, bit-for-bit port of that real
// per-pixel trig-rotation loop (`B10000000` -> `0x80`, `pgm_read_byte`
// reads replaced with direct `int[]` indexing per this shim's own
// established convention for baked bitmap data), not a new invention.
//
// TWO REAL SHIM PRIMITIVES THIS GAME RELIES ON (see also the final report
// to the user):
//
// 1. Real `Display::fillRoundRect()`/`drawRoundRect()` (used by
//    `drawHud()`'s own real "TAXI" status badge, a 21x7 box with a real
//    corner radius of 3) are ported directly as `gbFillRoundRect()`/
//    `gbDrawRoundRect()` in `gamebuinoShim.h`/`.c`, so both call sites here
//    simply call `gbFillRoundRect(27,0,21,7,3)`/`gbDrawRoundRect(27,0,21,7,3)`
//    directly, matching upstream's own real radius exactly.
// 2. Real `Display::setColor(INVERT)` (a genuine third draw mode, alongside
//    WHITE/BLACK, that XORs each drawn pixel against whatever is already on
//    screen) is ported directly as the `GB_INVERT` color constant in
//    `gamebuinoShim.h`/`.c` (every drawing primitive's color branch XORs the
//    target bit when `gbColor == GB_INVERT`). Upstream's own one real call
//    site (`drawHud()`, the "TAXI" badge label while a passenger is aboard)
//    does `setColor(BLACK); fillRoundRect(...); setColor(INVERT);` then
//    prints "TAXI" - ported here with `GB_INVERT` used directly at that same
//    call site, matching upstream's own call exactly.
//
// PLATFORM-REQUIRED DIV-BY-ZERO GUARDS (not upstream bugs, not behavior
// changes): `VIRCON32_C_DIALECT.md` documents that division or modulo by
// zero (integer OR float) hard-traps this CPU, unlike real AVR hardware
// where a float division by zero silently yields Infinity/NaN. Two of
// upstream's own real formulas divide by a value that's virtually always
// nonzero in practice but isn't provably so: `upgradeScore()`'s own
// `distClient*100/distNext` (distNext is a real point-to-point distance,
// only exactly 0 if a client's freshly-randomized destination happens to
// land on the player's exact current float position) and `drawHud()`'s
// own `(AC/AB)` compass-needle scale (AB is the live distance to the
// current destination, which shrinks toward 0 as the player approaches
// it and could in principle hit exactly 0.0 on some frame before the
// drop-off threshold check fires). Both get a tiny explicit floor
// (`townDistNext`/`ab` bumped up to a small nonzero value) immediately
// before the division - functionally inert in every realistic play
// session, added purely so a real, if astronomically unlikely, exact
// coincidence can't hard-crash the whole game on this platform the way it
// never could have on real hardware.
//
// A REAL BUG, FOUND AND FIXED (not preserved): `updatePlayer()`'s own
// `distTotal += 8 * abs(player.v<0)` (and the identical `distClient +=
// 8 * abs(player.v<0)` right after it) compares `player.v` against 0 and
// takes `abs()` of the resulting 0/1 boolean, instead of the clearly-
// intended `abs(player.v)`. Since `player.v` (the taxi's own scalar speed)
// is only ever increased by a positive `+0.02` or scaled down by positive
// multipliers throughout this entire game - it is never actually assigned
// a negative value anywhere - `player.v<0` is always false, so this line
// always adds exactly 0: `distTotal`/`distClient` never accumulated any
// real distance at all. This wasn't just cosmetic (`distTotal` only feeds
// the "Dist:" game-over readout, which was therefore always "000000") -
// `distClient` also feeds directly into `upgradeScore()`'s own scoring
// formula (`diff = (distClient*100/distNext)-100`), so with `distClient`
// permanently stuck at 0, `diff` was always exactly -100, `max(2,diff)`
// always resolved to the floor value 2, and every single drop-off
// unconditionally awarded `distNext/2` points - upstream's own intended
// "efficiency bonus/penalty for how directly you drove" scoring mechanic
// never actually engaged. Flagged as a genuine negative player-experience
// bug and fixed, unlike this project's usual "preserve real upstream
// scoring behavior" norm for this class of bug (see gameAgaruino.c's own
// header comment on that norm) - `townUpdatePlayer()` now uses the
// clearly-intended `fabs(townPlayer.v)` directly.
//
// FRICTION ONLY APPLIES ON TILE 0 ("road") - NOT A BUG, JUST HOW UPSTREAM
// WROTE IT: `updatePlayer()`'s own real `switch(currentTile){ case 0: ...
// }` has no other case and no `default` at all - ported as a bare `if`
// with no `else`, meaning literally zero friction is ever applied while
// driving over any non-road tile (forest/water/building/etc - only a
// direct collision with an actual obstacle tile, id 1, ever slows the taxi
// down off-road). Noted explicitly so this doesn't look like a missed
// translation of additional upstream friction cases that were simply never
// there.
//
// `GetSpriteById()`'s real `switch` maps case 5 to `Usine2` and case 6 to
// `Usine` - the reverse of their own declaration order in `world.ino`
// (`Usine` is declared, then `Usine2`) - ported verbatim via
// `townSprites[]`'s own element order (index 5 = Usine2, index 6 = Usine)
// rather than "corrected", since it's real upstream's own asset-index
// mapping, not a porting slip. Unlike gameUfoRace.c's own `drawWorld()`,
// this game's real loop bounds (`y` checked against `WORLD_H`, `x` against
// `WORLD_W`) are NOT swapped - ported straightforwardly with no quirk to
// preserve, noted here only so its absence isn't mistaken for a missed
// translation.
//
// EEPROM / HIGH SCORES: upstream's own real per-entry EEPROM layout is 12
// bytes (10 real name-character bytes, written via a real on-screen
// `gb.getDefaultName()` + `gb.keyboard()` text-entry widget, plus a 2-byte
// LSB/MSB score) - `gb.keyboard()`/`gb.getDefaultName()` have no
// equivalent anywhere in this shim (no text-input widget exists at all;
// already an established, documented scope limit, not a fresh gap - see
// gameUfoRace.c's own identical "NAME ENTRY - DROPPED, DOCUMENTED"
// section). Per that same established precedent, name storage is dropped
// entirely rather than faked with a placeholder string: `townHighscore[]`
// is a plain 5-entry scores-only table, 2 bytes/entry (10 bytes total),
// read/written via manual `eeprom_read_byte()`/`eeprom_write_byte()` calls
// at address `i*2`/`i*2+1` preserving upstream's own real LSB-then-MSB
// byte order exactly (`eeprom_read_word()`/`eeprom_write_word()` were
// deliberately NOT used here - this shim's own versions split a 16-bit
// value MSB-then-LSB across `address`/`address+1`, the opposite order from
// upstream's own real LSB-then-MSB split, so using them would silently
// flip the byte order relative to what upstream actually wrote; manual
// per-byte calls in the exact real order sidestep the question entirely -
// matching gameUfoRace.c's own identical reasoning and identical simpler
// layout for the exact same reason). `townUpdateHighscores()` accordingly
// shows only the five real scores (matching upstream's own real
// `LCDWIDTH-4*fontWidth` right-aligned column formula and real header-text
// wobble effect) with no invented replacement text in the dropped name
// column, exactly like gameUfoRace.c's own precedent. Upstream's own
// `initHighscore()` has one harmless, fully inert line -
// `highscore[thisScore] = /*(highscore[thisScore]==9999) ? 0 : */
// highscore[thisScore];` - a self-assignment with the only real logic
// already commented out by upstream itself; it does nothing at all, so no
// translation of it exists below. `drawHighScores()`'s own real
// `gb.display.textWrap = false;` is dropped as a pure no-op here (already
// an established scope limit - `gbPrintString()` never wraps at all
// regardless of this flag, see gameSnakeAbc.c's own identical precedent).
// `gb.sound.playOK()`/`playCancel()`/`playTick()` ported 1:1 to
// `gbPlayOK()`/`gbPlayCancel()`/`gbPlayTick()`; the real multi-note
// `highscore_sound[]` PROGMEM pattern (`Sound::playPattern()`) that
// upstream never actually calls anywhere in this particular game's own
// source (it's declared in highscore.ino but dead - no real call site
// exists) needed no substitution at all.
//
// `gb.battery.show = false;` (real setup()-time cosmetic battery-icon
// suppression, `initGame()`'s own real call) was dropped outright, matching
// gamePong.c's/gameUfoRace.c's own identical treatment of the same
// real-hardware-only feature.

// -----------------------------------------------------------------------------
// Real upstream bitmaps - byte-for-byte, see this file's own header comment
// on how these were extracted and verified.
// -----------------------------------------------------------------------------

int townClient1Bitmap[10] =
{
    8, 8, 0x38, 0xB9, 0x92, 0x7C, 0x10, 0x10, 0x28, 0x44,
};

int townGrandTaxiBitmap[29] =
{
    24, 9, 0xFF, 0xFF, 0xA0, 0xC0, 0x0, 0x60, 0xCD, 0xB5, 0xE0, 0x96, 0xC, 0x60, 0x96, 0x4C,
    0x40, 0x96, 0xC, 0x60, 0xCD, 0xB5, 0xE0, 0xC0, 0x0, 0x60, 0xFF, 0xFF, 0xA0,
};

int townLogoBitmap[290] =
{
    64, 36, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xC0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x7F, 0xC3, 0x6, 0xC, 0xFF, 0x7, 0x81, 0x2, 0x6, 0x7, 0x3, 0x8, 0xC0, 0x18,
    0x61, 0x2, 0x6, 0x5, 0x81, 0x18, 0xC0, 0x10, 0x31, 0x2, 0x6, 0x4, 0x81, 0xB0, 0xC0, 0x30,
    0x11, 0x2, 0x6, 0xC, 0x80, 0xE0, 0xC0, 0x20, 0x11, 0x2, 0x6, 0x8, 0xC0, 0xE0, 0xC0, 0x20,
    0x11, 0x2, 0x6, 0x18, 0x40, 0xE0, 0xFE, 0x20, 0x11, 0x2, 0x6, 0x1F, 0xE1, 0xB0, 0xC0, 0x20,
    0x11, 0x2, 0x6, 0x10, 0x61, 0x10, 0xC0, 0x30, 0x11, 0x2, 0x6, 0x30, 0x23, 0x18, 0xC0, 0x10,
    0x31, 0x2, 0x6, 0x20, 0x36, 0xC, 0xC0, 0x18, 0x61, 0x86, 0x6, 0x60, 0x1C, 0x4, 0xC0, 0x7,
    0x80, 0x78, 0x1C, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xF0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x1D, 0xD8, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x78, 0xD4, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x7, 0x40, 0x76, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFE, 0x41, 0x80, 0x0, 0x0,
    0x0, 0x0, 0x0, 0xB2, 0x70, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x1, 0xBA, 0x7A, 0x60, 0x0, 0x6,
    0x50, 0x0, 0x3, 0xFF, 0x4F, 0xFF, 0x0, 0x7, 0x20, 0x0, 0xE, 0x1, 0xF8, 0x0, 0xC0, 0x6,
    0x20, 0x0, 0x18, 0x82, 0x4, 0x0, 0x40, 0x0, 0x0, 0x0, 0x30, 0x3, 0x84, 0x1, 0xC0, 0x0,
    0x0, 0x0, 0x20, 0x82, 0x5, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x6F, 0xFE, 0x6, 0xF6, 0x0, 0x0,
    0x0, 0x0, 0xCF, 0xFE, 0x7, 0xFA, 0xE, 0x8E, 0xEE, 0xCE, 0xF3, 0x33, 0xFD, 0x98, 0x8, 0x88,
    0xE8, 0xA4, 0x3, 0x30, 0x1, 0x98, 0xE, 0xEE, 0xAE, 0xA4, 0x3, 0xF0, 0x1, 0xF8, 0x0, 0x0,
    0x0, 0x0, 0x1, 0xE0, 0x0, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0,
};

int townSpriteEau[34] =
{
    16, 16, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xF7, 0xFA, 0xEB, 0xFF, 0xFF, 0xDF, 0xFF, 0xAF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xF5, 0xFF, 0xFF, 0xDF, 0xFF, 0xAF, 0xDF, 0xFF, 0xAF, 0xFF,
    0xFF, 0xFF,
};

int townSpriteFleure[34] =
{
    16, 16, 0x0, 0x0, 0x50, 0x0, 0x20, 0x28, 0x50, 0x10, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0,
    0x1, 0x40, 0x0, 0x80, 0x1, 0x40, 0x0, 0x0, 0x0, 0x14, 0x28, 0x8, 0x10, 0x14, 0x28, 0x0,
    0x0, 0x0,
};

int townSpriteForet[34] =
{
    16, 16, 0x0, 0x0, 0x10, 0x0, 0x6D, 0x0, 0x54, 0x8, 0xAA, 0x14, 0x54, 0x8, 0x6C, 0x20,
    0x10, 0x0, 0x0, 0x10, 0x0, 0x6C, 0x4, 0x54, 0x10, 0xAA, 0x28, 0x54, 0x10, 0x6C, 0x0, 0x10,
    0x0, 0x0,
};

int townSpritePavillon[34] =
{
    16, 16, 0x0, 0x0, 0x7F, 0xFE, 0x6B, 0x56, 0x57, 0xAA, 0x7F, 0xFE, 0x6B, 0x56, 0x57, 0xAA,
    0x7F, 0xFE, 0x6B, 0x56, 0x57, 0xAA, 0x7F, 0xFE, 0x40, 0x2, 0x40, 0x2, 0x40, 0x2, 0x7F, 0xFE,
    0x0, 0x0,
};

int townSpriteRoofTop[34] =
{
    16, 16, 0x0, 0x0, 0x7F, 0xFE, 0x60, 0x6, 0x5F, 0xFA, 0x50, 0xA, 0x52, 0xA, 0x50, 0x4A,
    0x50, 0xA, 0x50, 0xA, 0x56, 0x2A, 0x56, 0xA, 0x50, 0xA, 0x5F, 0xFA, 0x60, 0x6, 0x7F, 0xFE,
    0x0, 0x0,
};

int townSpriteUsine[34] =
{
    16, 16, 0x0, 0x0, 0x38, 0x0, 0x7D, 0xFE, 0x7D, 0x2, 0x7D, 0x2, 0x39, 0xFE, 0x7F, 0x2,
    0x40, 0x2, 0x40, 0x2, 0x40, 0x2, 0x7F, 0xFE, 0x2, 0x0, 0x25, 0x3C, 0x52, 0x3C, 0x20, 0x0,
    0x0, 0x0,
};

int townSpriteUsine2[34] =
{
    16, 16, 0x0, 0x20, 0x0, 0x50, 0x38, 0x20, 0x47, 0xFE, 0x55, 0x2, 0x44, 0x82, 0x38, 0xF2,
    0x21, 0xA, 0x12, 0x66, 0xA, 0x96, 0x6, 0x96, 0x76, 0x66, 0x75, 0xA, 0x4, 0xF2, 0x7, 0xFE,
    0x0, 0x0,
};

// Real upstream's own switch: case 0..6 -> Eau/Fleure/Foret/Pavillon/
// RoofTop/Usine2/Usine (note indices 5/6 map to Usine2 then Usine - the
// REVERSE of their own declaration order above - see header comment).
int* townSprites[7] =
{
    townSpriteEau, townSpriteFleure, townSpriteForet, townSpritePavillon,
    townSpriteRoofTop, townSpriteUsine2, townSpriteUsine,
};

// x1,y1,x2,y2,tile - real upstream zone table (tile-space rectangles that
// override the default "Foret" tile appearance for tiles inside them).
#define TOWN_NB_ZONE 10
int townZones[50] =
{
    19, 0, 118, 31, 3, 56, 68, 64, 74, 0, 0, 32, 32, 62, 0,
    47, 94, 86, 116, 0, 42, 35, 93, 57, 5, 14, 62, 113, 93, 6,
    98, 35, 110, 44, 3, 111, 49, 119, 60, 3, 47, 94, 127, 127, 4,
    14, 116, 30, 123, 3,
};

// Real upstream 128x128 packed 1-bit-per-tile world bitmask (1 = obstacle,
// 0 = drivable). Fully populated (2048 = WORLD_W*WORLD_H/8 real values, no
// partial-initializer zero-fill quirk this time).
#define TOWN_WORLD_W 128
#define TOWN_WORLD_H 128
int townWorld[2048] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x80, 0x0, 0x1F, 0xFF, 0x80, 0x0, 0x7F, 0xFF, 0xE0, 0x0, 0x0, 0x7, 0xE0, 0xFF, 0xFF, 0xFF,
    0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xEE, 0x0, 0x0, 0xFF,
    0x8C, 0xFF, 0xC0, 0x0, 0x1F, 0xFE, 0x0, 0x0, 0x7, 0x7B, 0xDD, 0xE6, 0xF, 0x0, 0x7E, 0xFF,
    0x98, 0xFF, 0x8F, 0xE7, 0x80, 0x3E, 0x7F, 0xF3, 0xE7, 0x7B, 0xDD, 0xE6, 0x6F, 0x7F, 0x7E, 0xFF,
    0x91, 0xFF, 0x1F, 0xE7, 0x80, 0x30, 0x7F, 0xF3, 0xE7, 0x0, 0x1, 0xE6, 0xE0, 0x7F, 0x7E, 0xFF,
    0xB3, 0xFE, 0x3F, 0xE7, 0xFF, 0x30, 0x7F, 0xF3, 0xE7, 0x7B, 0xDD, 0xE6, 0x7F, 0xFF, 0x7E, 0xFF,
    0xB3, 0xFC, 0x7F, 0xE7, 0xFF, 0x33, 0x7F, 0xF3, 0xE7, 0x7B, 0xDD, 0xE0, 0x3F, 0xFF, 0x7C, 0xFF,
    0x93, 0xF8, 0xE0, 0x7, 0xFF, 0x33, 0x7F, 0xF3, 0xE7, 0x7B, 0xDD, 0xE7, 0x80, 0xC1, 0x1, 0xFF,
    0x83, 0xF1, 0xC7, 0xE7, 0xC0, 0x33, 0x7F, 0xF3, 0xE7, 0x0, 0x1, 0xE7, 0xBE, 0xDD, 0x7F, 0xFF,
    0xF3, 0xE3, 0x8F, 0xE7, 0xDF, 0x33, 0x60, 0x23, 0xE7, 0xFF, 0xFF, 0xE7, 0xBE, 0xD9, 0x7F, 0xFF,
    0xF3, 0xE7, 0x1F, 0xE7, 0xDF, 0x33, 0x6F, 0x83, 0xE7, 0x0, 0x0, 0x60, 0x3E, 0xD9, 0x7F, 0xF,
    0xF3, 0xE6, 0x3F, 0xE7, 0xDF, 0x33, 0x6F, 0x87, 0xE7, 0x0, 0x0, 0x67, 0xFE, 0xDF, 0x7E, 0x7,
    0xF3, 0xE4, 0x0, 0x3, 0xDF, 0x33, 0x6F, 0xBF, 0xE7, 0x3F, 0x9E, 0x67, 0xFE, 0xCE, 0x7C, 0x7,
    0xE0, 0xE0, 0xDB, 0xD8, 0x1F, 0x33, 0x6F, 0xBF, 0xE7, 0x3F, 0x1E, 0x60, 0x3E, 0xEC, 0xF8, 0x7,
    0xE0, 0xE1, 0xDB, 0xDB, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x3E, 0x3E, 0x67, 0x80, 0x1, 0xF0, 0x47,
    0xE4, 0xE7, 0xD9, 0x9B, 0xFF, 0x3, 0xF8, 0x0, 0x0, 0x7C, 0x7E, 0x67, 0xBF, 0xFF, 0xE0, 0xC7,
    0xE0, 0xE7, 0xD9, 0x9B, 0xFF, 0xCF, 0xF9, 0xFF, 0xFF, 0xF8, 0xFE, 0x60, 0x3F, 0xFF, 0xC1, 0xC7,
    0xE0, 0xE7, 0xDD, 0xBB, 0xFF, 0xCE, 0x1, 0xFF, 0xC0, 0x1, 0xFE, 0x67, 0xFF, 0xFF, 0x83, 0xC7,
    0xF3, 0xE7, 0xDD, 0xBB, 0xFC, 0x0, 0xF9, 0xFF, 0xC0, 0x3, 0xFE, 0x67, 0xFF, 0xFF, 0x7, 0xC7,
    0xF3, 0xE7, 0xC0, 0x3, 0xF8, 0xF, 0xF9, 0xFF, 0xCF, 0xFF, 0xFE, 0x7, 0xFF, 0xFE, 0xF, 0xC7,
    0xF3, 0xE7, 0xFC, 0xFF, 0xF0, 0xFF, 0xF8, 0x0, 0xF, 0xFF, 0xFE, 0x3, 0x0, 0x0, 0x1F, 0xC7,
    0xF3, 0xE7, 0xFC, 0xFF, 0xF3, 0xFF, 0xF8, 0x0, 0xF, 0xFF, 0xFF, 0xD0, 0x0, 0x0, 0x3F, 0xC7,
    0xF3, 0xE7, 0xFC, 0xFF, 0xF3, 0xFF, 0xF9, 0xFF, 0xE7, 0xFF, 0xFF, 0xD0, 0x38, 0x0, 0x7F, 0xC7,
    0xF0, 0x7, 0xFC, 0xFF, 0xF3, 0xFF, 0xF9, 0xFF, 0xF3, 0xFF, 0xFF, 0xDC, 0xF8, 0xFF, 0xFF, 0xC7,
    0xF3, 0xE7, 0xFC, 0xFF, 0xF2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0xF9, 0xFF, 0xFF, 0xC7,
    0x83, 0xE3, 0xFC, 0xFF, 0xF2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0xF1, 0xFF, 0xFF, 0xC7,
    0x83, 0xF3, 0xFC, 0xFF, 0xF2, 0x7F, 0xFF, 0xFE, 0xFC, 0xFC, 0xFF, 0xE4, 0xE3, 0xFF, 0xFF, 0xC7,
    0x9F, 0xF1, 0xFC, 0xFF, 0xF2, 0x7F, 0xF0, 0x0, 0xFC, 0xFC, 0xFF, 0xE4, 0xC7, 0xFF, 0xFF, 0xC7,
    0x9F, 0xF8, 0xFC, 0xFF, 0xF2, 0x7F, 0xF7, 0xFF, 0x8C, 0xFC, 0xE0, 0x64, 0xF, 0xFF, 0xFF, 0xC7,
    0x9F, 0xFC, 0x7C, 0xFF, 0xF2, 0x7F, 0xF7, 0xFF, 0xAC, 0x0, 0x0, 0x64, 0x1F, 0xFF, 0xFF, 0xC7,
    0x9F, 0xFE, 0x0, 0x0, 0x2, 0x0, 0x7, 0x1, 0xA4, 0x0, 0x6, 0x64, 0xFF, 0xFF, 0xFF, 0xC7,
    0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0xB7, 0xEF, 0xFE, 0x64, 0xF8, 0x1, 0xFF, 0xC7,
    0x80, 0x0, 0x7, 0xFF, 0x30, 0x7F, 0x70, 0x39, 0xB0, 0x0, 0x2, 0x64, 0xF0, 0x0, 0x7F, 0xC7,
    0xDF, 0xF7, 0xC7, 0xFF, 0x36, 0x41, 0x7F, 0x39, 0x9F, 0xBF, 0xFA, 0x4, 0xE3, 0xFC, 0x7F, 0xC7,
    0xC0, 0x7, 0x8F, 0xFF, 0x36, 0x5D, 0x0, 0x38, 0x1C, 0x3F, 0x8A, 0x4, 0xC7, 0xFE, 0x7F, 0xC7,
    0xDF, 0x9F, 0x1F, 0xFF, 0x36, 0x5D, 0x0, 0x38, 0x1, 0x0, 0x22, 0x60, 0xCF, 0xFE, 0x7F, 0xC7,
    0xD0, 0xBE, 0x3F, 0xFF, 0x36, 0x5D, 0x3F, 0x3F, 0xFB, 0xEF, 0xBE, 0x60, 0xCF, 0xFE, 0x7F, 0xC7,
    0xD6, 0xBC, 0x7F, 0xFF, 0x32, 0x51, 0x3E, 0x0, 0xFA, 0x2F, 0xBE, 0x64, 0xC, 0x6, 0x7F, 0xC7,
    0xD6, 0x38, 0xFF, 0xFF, 0x30, 0x11, 0x3E, 0x7E, 0xF8, 0xAF, 0xBE, 0x64, 0xD, 0x50, 0x7F, 0xC7,
    0xD7, 0xF1, 0xFF, 0xFF, 0x3A, 0x51, 0x3E, 0x7E, 0xFD, 0xAF, 0x80, 0x64, 0xCD, 0x56, 0x7F, 0x87,
    0xC7, 0xE3, 0xFF, 0xFF, 0x3A, 0x5F, 0x3E, 0x70, 0x1, 0x8F, 0x80, 0x64, 0xC5, 0x56, 0x3F, 0x7,
    0xD7, 0xE3, 0xFF, 0xFF, 0x3A, 0x5F, 0x3E, 0x70, 0x0, 0x0, 0x33, 0xE4, 0xE5, 0x57, 0x1E, 0x7,
    0xD7, 0xE3, 0xFF, 0xFF, 0xA, 0x40, 0x3E, 0x73, 0xFF, 0xFF, 0xF3, 0xE4, 0xE0, 0x3, 0x84, 0xF,
    0xC7, 0xE3, 0xFF, 0xFF, 0x2, 0x40, 0x3E, 0x3, 0xFF, 0xE0, 0x0, 0x4, 0xE0, 0xFB, 0xC0, 0x3F,
    0xEF, 0xE3, 0xFF, 0xFF, 0x3E, 0x7F, 0xBE, 0x3, 0xFF, 0xE0, 0x0, 0x4, 0xF0, 0xFB, 0xE0, 0x3F,
    0xC1, 0xE3, 0xFF, 0xFF, 0x3E, 0x7F, 0xBF, 0xF0, 0x7, 0xE7, 0xEE, 0xFC, 0xFC, 0x38, 0x7C, 0x1F,
    0xDD, 0xE3, 0xFF, 0xFF, 0x3E, 0x7F, 0xBF, 0xFF, 0xF7, 0xE7, 0xEE, 0x0, 0xFE, 0xF, 0x3E, 0x1F,
    0xC1, 0xE3, 0xFF, 0xFF, 0x3E, 0x60, 0x3F, 0xFF, 0xF7, 0xE7, 0xE, 0xFC, 0xFF, 0x87, 0x9F, 0x1F,
    0xFF, 0xE3, 0xFF, 0xFF, 0x3E, 0x6F, 0xFF, 0xFF, 0xF3, 0xE7, 0x7E, 0xC, 0xFF, 0xE1, 0xDF, 0x1F,
    0xC3, 0xC3, 0xFF, 0xFF, 0x20, 0x6F, 0xE0, 0x0, 0xFB, 0xE7, 0x0, 0xAC, 0x1, 0xF1, 0xDF, 0xF,
    0xDB, 0x87, 0xFF, 0xFF, 0x4, 0x6F, 0xEF, 0xFE, 0xFB, 0xE7, 0x7F, 0xA0, 0x1, 0xF9, 0x83, 0x7,
    0xC3, 0x8F, 0xFF, 0xFF, 0x8E, 0x6F, 0xEF, 0xE0, 0x3, 0xE7, 0x7F, 0xBD, 0xF8, 0xF9, 0xBB, 0x83,
    0xFB, 0x8F, 0xFF, 0xFF, 0x9E, 0x60, 0x0, 0xF, 0xCF, 0xE4, 0x0, 0x1D, 0xFC, 0x79, 0x83, 0xC3,
    0xFB, 0x87, 0xFF, 0xFF, 0x9E, 0x7F, 0xEF, 0xFF, 0x8F, 0xE5, 0xF7, 0xD9, 0xFC, 0x78, 0x3B, 0xE3,
    0xC3, 0xC3, 0xFF, 0xFF, 0x9E, 0x7F, 0xEF, 0xFF, 0x9F, 0xE5, 0xF7, 0xDB, 0xFE, 0x79, 0x83, 0xE3,
    0xDF, 0xE3, 0xFF, 0xFF, 0x1E, 0x7F, 0xEF, 0xFF, 0x3F, 0xE5, 0xF7, 0xDB, 0xFE, 0x79, 0xBB, 0xE3,
    0xDF, 0xE3, 0xFF, 0xFF, 0x2, 0x7F, 0xEF, 0xFF, 0x3F, 0xE4, 0xF0, 0x1B, 0xFE, 0x79, 0x83, 0xC3,
    0xDF, 0xE3, 0xFF, 0xFF, 0x3A, 0x0, 0x0, 0x0, 0x0, 0x6, 0xF7, 0xFB, 0xFE, 0x79, 0xBB, 0x83,
    0xC1, 0xE3, 0xFF, 0xFF, 0x3A, 0x0, 0x0, 0x0, 0x0, 0x6, 0xF7, 0xF9, 0xFE, 0x79, 0x83, 0x7,
    0xD8, 0xE3, 0xFF, 0xFF, 0x3B, 0xF3, 0xFF, 0xFF, 0xEF, 0xFE, 0x3, 0xFD, 0xFE, 0x79, 0xDE, 0xF,
    0xDC, 0x63, 0xFF, 0xFF, 0x38, 0x1, 0xFE, 0x0, 0xEF, 0xC6, 0xF0, 0x4, 0x0, 0x79, 0xDE, 0x1F,
    0xDE, 0x63, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xE0, 0x0, 0x79, 0xDE, 0x3F,
    0x8F, 0x62, 0x0, 0x0, 0x1F, 0xFC, 0x0, 0xFF, 0x0, 0x10, 0xFF, 0xFF, 0xFE, 0x78, 0x1E, 0x3F,
    0x87, 0x62, 0x7F, 0xE7, 0x8F, 0xFF, 0xFF, 0xFF, 0x3F, 0x79, 0xFF, 0xFF, 0xFE, 0x79, 0xFE, 0x3F,
    0xA3, 0x62, 0xFF, 0xE7, 0xC7, 0xFF, 0xC0, 0xFF, 0x3C, 0x79, 0xFF, 0xFF, 0xFE, 0x79, 0xFE, 0x3F,
    0xBB, 0x62, 0xFF, 0xE7, 0xE3, 0xFF, 0x9E, 0xFF, 0x30, 0xF9, 0xFF, 0xFF, 0xFE, 0x79, 0xFE, 0x3F,
    0xBB, 0x62, 0xFF, 0xE1, 0xF0, 0x0, 0x3E, 0xFF, 0x37, 0xF9, 0xFF, 0xFF, 0xFE, 0x79, 0xFE, 0x3F,
    0xA3, 0x62, 0xFF, 0xE0, 0xFF, 0xFF, 0x9E, 0x0, 0x0, 0x0, 0x0, 0x7F, 0xFE, 0x79, 0xFE, 0x3F,
    0xA7, 0x62, 0xFF, 0xE4, 0x3F, 0xFF, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x7F, 0xFE, 0x79, 0xFE, 0x3F,
    0x8F, 0x62, 0xFF, 0xC7, 0xF, 0xFF, 0xFC, 0xFF, 0x37, 0xF9, 0xDE, 0x7F, 0xFE, 0x79, 0xFE, 0x3F,
    0x8F, 0x62, 0xFF, 0xCF, 0x83, 0xFF, 0xFC, 0xFF, 0x30, 0x79, 0xDE, 0x7F, 0xFE, 0x70, 0xE, 0x3F,
    0xDE, 0x62, 0x7F, 0xCF, 0xE0, 0x7F, 0xFC, 0xFF, 0x3F, 0x79, 0xDE, 0x7F, 0xFC, 0x0, 0xE, 0x3F,
    0xDC, 0x62, 0x3, 0xCF, 0xF8, 0x3F, 0xFC, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xCE, 0x3F,
    0xD8, 0xE3, 0xF3, 0xCF, 0xFF, 0x1F, 0xFC, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x9F, 0xCE, 0x1F,
    0xD1, 0xE3, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3F, 0x79, 0xFE, 0x7F, 0xFF, 0x9F, 0xCE, 0x1F,
    0xC3, 0xE3, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3F, 0x79, 0x2, 0x7F, 0xFF, 0x9F, 0xCF, 0xF,
    0xC7, 0xE3, 0x83, 0xD7, 0x9E, 0xBC, 0xFC, 0xFF, 0x3C, 0x78, 0x0, 0x7F, 0xFF, 0x9F, 0xCF, 0x8F,
    0xCF, 0xC2, 0x7, 0xD7, 0x9E, 0xBC, 0xFC, 0xFF, 0x38, 0xF8, 0x78, 0x7, 0xFF, 0x9F, 0xCF, 0x8F,
    0xCF, 0x0, 0x27, 0xD7, 0x9E, 0xBC, 0xFC, 0xFF, 0x3B, 0xF9, 0xFE, 0x3, 0xFF, 0x9F, 0x87, 0x8F,
    0xCE, 0x0, 0xE7, 0xD7, 0x9E, 0xBC, 0xFC, 0x0, 0x3B, 0xF9, 0xFE, 0x71, 0xFF, 0x9F, 0x3, 0x87,
    0xCC, 0x43, 0xE7, 0xD7, 0x9E, 0xBC, 0x0, 0x0, 0x0, 0x0, 0x0, 0x78, 0xFF, 0x1E, 0x31, 0xC7,
    0xC8, 0xE3, 0xE7, 0xD7, 0x9E, 0xBC, 0xFF, 0x5F, 0x0, 0x0, 0x0, 0x7C, 0x7F, 0x0, 0x78, 0x7,
    0xC1, 0xE3, 0xE7, 0xD7, 0x9E, 0xBC, 0xFF, 0x5F, 0x9F, 0x7B, 0xB7, 0x3E, 0x3F, 0x0, 0x78, 0x7,
    0xC3, 0xE3, 0xE7, 0xD7, 0x9E, 0xBC, 0xFF, 0x5F, 0x9F, 0x7B, 0xB7, 0x9F, 0x1F, 0x3E, 0x31, 0xC7,
    0xC7, 0xE3, 0xE7, 0xD7, 0x9E, 0xBC, 0xFF, 0x5F, 0x9F, 0x7B, 0xB7, 0x8F, 0x9F, 0x3F, 0x3, 0xC7,
    0xCF, 0xE3, 0xE7, 0xD7, 0x9E, 0xBC, 0xFF, 0x5F, 0x9E, 0x7B, 0xB7, 0xC3, 0x9E, 0x3F, 0x87, 0xC7,
    0xCF, 0xE3, 0xE7, 0xD7, 0x9E, 0xBC, 0xFF, 0x5F, 0x9E, 0xFB, 0xB7, 0xF3, 0x9C, 0x1F, 0xCF, 0xC7,
    0xCF, 0xE3, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1E, 0xFB, 0xB7, 0xF8, 0x1, 0xDF, 0xCF, 0xC7,
    0xCF, 0xE3, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0xFF, 0xDF, 0xCF, 0xC7,
    0xC0, 0x63, 0xE6, 0xDB, 0x1B, 0x6D, 0xB6, 0xDB, 0x1F, 0xFF, 0xFF, 0x9F, 0xFF, 0x0, 0x0, 0x7,
    0xCC, 0x63, 0xE7, 0xFF, 0x0, 0x0, 0x0, 0x3, 0x81, 0xFF, 0xFF, 0x9F, 0xFE, 0x0, 0x0, 0xF,
    0xCC, 0x63, 0xE7, 0xFF, 0x3F, 0xFF, 0xFF, 0xFF, 0xC0, 0x7F, 0xFF, 0x87, 0xFC, 0x0, 0x0, 0x1F,
    0xCF, 0xE3, 0xC3, 0xFE, 0x3F, 0xFF, 0xFF, 0xFF, 0xF8, 0x3F, 0xFF, 0xC3, 0xF8, 0x3F, 0xFF, 0xFF,
    0xCF, 0xE3, 0x81, 0xFC, 0x7F, 0xFF, 0xFF, 0xFF, 0xFE, 0x3F, 0xFF, 0xF1, 0xF0, 0x7F, 0xFF, 0xFF,
    0xCF, 0xE3, 0x18, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0xFF, 0xF8, 0xF0, 0xF8, 0x0, 0x1,
    0xCF, 0xE0, 0x3C, 0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFC, 0x1, 0xE1, 0xFF, 0x9D,
    0xC0, 0x60, 0x3C, 0x1, 0xFF, 0xFF, 0xFF, 0xFE, 0x1F, 0x9F, 0xFF, 0xFE, 0x1, 0xEF, 0xFF, 0xDD,
    0xCC, 0x63, 0x18, 0xE3, 0xFF, 0xFF, 0xFF, 0xFE, 0xF, 0x9F, 0x3, 0xFF, 0xF1, 0xEF, 0x80, 0x1D,
    0xCC, 0x63, 0x81, 0xE7, 0xFF, 0xFF, 0xFF, 0xFE, 0xCF, 0x9E, 0x3, 0xFF, 0xE1, 0xEF, 0x9F, 0xFD,
    0xCF, 0xE3, 0xC3, 0xC3, 0xFF, 0xFF, 0xFF, 0xFE, 0x8F, 0x9E, 0x33, 0xFF, 0xC1, 0xE0, 0x3F, 0xF9,
    0xCF, 0xE3, 0xE7, 0x0, 0x0, 0x0, 0xF, 0xFE, 0x80, 0x0, 0x33, 0xFF, 0x83, 0xEF, 0x80, 0x1,
    0xCF, 0xE3, 0xE6, 0x0, 0x0, 0x0, 0x7, 0xFE, 0x9F, 0x80, 0x3, 0xFF, 0x7, 0xEF, 0xBC, 0xFB,
    0xC7, 0xE3, 0xE4, 0x0, 0x0, 0x0, 0x7, 0xFE, 0x1F, 0x9F, 0x7, 0xFE, 0x7, 0xEF, 0xBC, 0xFB,
    0xE3, 0xE3, 0xE0, 0x3C, 0xFF, 0xFF, 0x83, 0xFF, 0xFF, 0x9F, 0xFF, 0xFC, 0x7, 0xE0, 0x3C, 0x1B,
    0xF1, 0xE3, 0xE0, 0x7C, 0x7F, 0xFF, 0xC1, 0xFF, 0xFF, 0x9F, 0xFF, 0xF8, 0x7, 0xFD, 0xFC, 0xB,
    0xF9, 0xE1, 0xE0, 0xFE, 0x3F, 0xE1, 0xE0, 0xFF, 0xFF, 0x9F, 0xFF, 0xF0, 0x61, 0xFD, 0xF8, 0xCB,
    0xF9, 0xE0, 0x1, 0xFF, 0x1F, 0xED, 0xF0, 0x7F, 0xFF, 0x9F, 0xFF, 0xE0, 0xF0, 0xFD, 0xE0, 0xC3,
    0xF9, 0xF0, 0x3, 0xFF, 0x83, 0xED, 0xF8, 0x1F, 0xFF, 0x9F, 0xFF, 0xC1, 0xF8, 0x3D, 0x87, 0xC7,
    0xF9, 0xF8, 0x7, 0xFF, 0xC0, 0xED, 0xFC, 0x3, 0xFF, 0x9F, 0xFE, 0x3, 0xCE, 0xD, 0xF, 0xCF,
    0xF9, 0xFC, 0xF, 0xFF, 0xF8, 0x0, 0x1E, 0x0, 0x7F, 0x9F, 0xFC, 0x7, 0xC7, 0x84, 0x3F, 0x8F,
    0xF9, 0xFF, 0xFF, 0x80, 0xFE, 0x6F, 0xDF, 0x0, 0x0, 0x0, 0x0, 0xF, 0xD3, 0xE0, 0xBF, 0x1F,
    0xF9, 0xFF, 0xFF, 0x3E, 0xFE, 0x6F, 0xDF, 0xE0, 0x0, 0x0, 0x0, 0x3F, 0x83, 0xE3, 0xBE, 0x3F,
    0xF9, 0xFF, 0xFC, 0x7E, 0xFE, 0x60, 0x0, 0xFC, 0x0, 0x0, 0x1, 0xFE, 0x7, 0xE7, 0xBC, 0x7F,
    0xF9, 0xFF, 0xF9, 0xFE, 0xFE, 0x7F, 0xFE, 0xFF, 0xFF, 0x9F, 0xFF, 0xF8, 0x63, 0xE7, 0x3C, 0xFF,
    0xF9, 0xFC, 0x3, 0xC0, 0x0, 0x0, 0x2, 0xFF, 0xFF, 0x9F, 0xFF, 0xE1, 0xF3, 0xE7, 0x3C, 0xFF,
    0xF9, 0xFD, 0xFF, 0x80, 0x0, 0xF, 0x7A, 0xFF, 0xFF, 0x9F, 0xF8, 0x7, 0xFB, 0xE7, 0x7C, 0xFF,
    0xF8, 0x7D, 0xFF, 0x9E, 0xFF, 0xCF, 0x78, 0x1, 0xFF, 0x9F, 0xFB, 0xFF, 0xF1, 0xE7, 0x78, 0x7F,
    0xF3, 0x1D, 0xFF, 0x9E, 0xFF, 0xCF, 0x3, 0xFD, 0xFF, 0x9F, 0xF1, 0xFF, 0xE0, 0xC6, 0x78, 0x7F,
    0xF7, 0xC0, 0x7F, 0x9E, 0xFF, 0xCF, 0xDF, 0xFC, 0x0, 0x0, 0x4, 0x0, 0x4, 0x6, 0x7B, 0x7F,
    0xE7, 0xFF, 0x0, 0x1E, 0xFF, 0xC0, 0x0, 0x0, 0x68, 0x0, 0x0, 0x4, 0x4, 0xE, 0xFB, 0x7F,
    0xEF, 0xFF, 0x0, 0x1E, 0x7F, 0x0, 0x0, 0x0, 0x0, 0x2F, 0xB1, 0xF1, 0xE0, 0xBE, 0xFB, 0x7F,
    0xEF, 0xFE, 0x7B, 0xFF, 0x7F, 0x7E, 0x1F, 0xFC, 0x3F, 0x2F, 0x9F, 0xFB, 0xF1, 0xBE, 0xFB, 0x7F,
    0xE7, 0xFE, 0xF9, 0xFF, 0x0, 0x7F, 0x87, 0xFC, 0x0, 0x2F, 0xD0, 0x3, 0xFB, 0xBE, 0x3, 0x7F,
    0xF0, 0x7C, 0xFC, 0xFF, 0xFB, 0xFF, 0xC1, 0xFF, 0x7F, 0xE0, 0xD0, 0x1, 0xFB, 0xBE, 0xFF, 0x7F,
    0xFF, 0x1, 0xFE, 0x0, 0x3, 0xFF, 0xF0, 0x0, 0x0, 0x0, 0x3, 0xF8, 0x0, 0x0, 0xFF, 0x7F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x0, 0x0, 0xE, 0x7, 0xF8, 0x0, 0x0, 0x0, 0x7F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define TOWN_NB_CLIENT 4
#define TOWN_DIST_RECUP_DEPOS 16
#define TOWN_DEMI_PI 0.157
#define TOWN_NUM_HIGHSCORE 5
#define TOWN_PAUSE_MENU_COUNT 7
#define TOWN_GAMEOVER_FRAMES 40

enum TownState
{
    TOWN_STATE_TITLE = 0,
    TOWN_STATE_MENU = 1,
    TOWN_STATE_PLAY = 2,
    TOWN_STATE_GAMEOVER = 3,
    TOWN_STATE_HIGHSCORES = 4,
    TOWN_STATE_MINIMAP = 5
};

typedef struct
{
    float x, y, v, vx, vy, angle;
    int radius; // real upstream field, assigned once, never actually read anywhere - see header comment
} TownPlayer;

TownPlayer townPlayer;
int townState;
int townMenuIndex;

// destination of the current client
int townXDest;
int townYDest;
int townNumClient;
int townTime;
int townTimeLeft;
int townScoreTotal;
int townNbClient;
float townDistTotal; // total distance for the run
float townDistNext;  // beeline distance to the current client's destination
float townDistClient; // distance actually driven toward the client
bool townCountingTime;

int townCameraX;
int townCameraY;
int townSavedCameraX; // real upstream old_x/old_y - saved/restored around the minimap's own reuse of camera_x/camera_y as tile-space pan coordinates
int townSavedCameraY;

int townClients[8]; // x1,y1,x2,y2,... (TOWN_NB_CLIENT*2 - written as a plain literal since this dialect's own array-size brackets aren't proven to fold a macro expression) - relies on this dialect's global zero-init, matching real upstream's own uninitialized global array

int townHighscore[TOWN_NUM_HIGHSCORE];

int townGameOverFrame;
float townScoreCpt;

int townMiniMapFrame;

// -----------------------------------------------------------------------------
// Forward declarations - this game's own state machine has real mutual
// cycles (MENU launches PLAY/GAMEOVER-reachable states and is itself
// re-entered from all of them; TITLE/HIGHSCORES/MINIMAP all return to
// MENU) so no purely-linear top-to-bottom definition order can avoid every
// call-before-definition site. `VIRCON32_C_DIALECT.md` documents that
// "forward/partial declarations are allowed for ordering" - matching
// gameSkibuino.c's own identical, already-established use of exactly this
// pattern for the same reason (see its own forward-declared `skiThinkAll()`
// etc).
// -----------------------------------------------------------------------------

void townUpgradeScore();
void townBeginMenu();
void townBeginHighscores();
void townBeginTitle();
void townInitGame( int tempMax );
void townBeginPlay();

// -----------------------------------------------------------------------------
// Small local helpers
// -----------------------------------------------------------------------------

void townDisplayText( char* s, int x, int y )
{
    gbCursorX = x;
    gbCursorY = y;
    gbPrintString( s );
}

// Prints `value` continuing from the current cursor position, left-padded
// with '0' glyphs (drawn explicitly at 48='0') to at least `fig` digits -
// direct port of upstream's own real `displayInt(l, fig)` (the cursor-
// continuing overload), built on real `itoa()` exactly like this shim's
// own `gbPrintNumber()` does internally.
void townPrintPaddedHere( int value, int fig )
{
    gbFontSize = 1; // real upstream forces fontSize=1 on every displayInt() call
    char buf[16];
    int len = 0;
    itoa( value, buf, 10 );
    while( buf[ len ] != 0 )
      len = len + 1;

    int pad = fig - len;
    int i;
    for( i = 0; i < pad; i++ )
    {
        gbDrawChar( 48, gbCursorX, gbCursorY ); // 48 = '0'
        gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    }
    gbPrintString( buf );
}

// Direct port of upstream's own real `displayInt(l, Tx, Ty, fig)` (the
// explicit-cursor overload).
void townPrintPaddedAt( int value, int x, int y, int fig )
{
    gbCursorX = x;
    gbCursorY = y;
    townPrintPaddedHere( value, fig );
}

// Direct, bit-for-bit port of upstream's own real per-pixel continuous-
// angle bitmap rotator (AffichageHelper.ino's own `drawBitmapAngle()`) -
// see header comment on why this is NOT a `gbDrawBitmapRotated()` call
// (that primitive only supports 4 fixed 90-degree rotations; this game
// needs a real, continuous float angle). `bitmap[]` uses the same
// `{width,height,bytes...}` layout as `gbDrawBitmap()`.
void townDrawBitmapAngle( int x, int y, int* bitmap, float angle )
{
    int w = bitmap[ 0 ];
    int h = bitmap[ 1 ];
    int centerX = w / 2;
    int centerY = h / 2;
    int byteWidth = ( w + 7 ) / 8;
    float sinA = sin( angle );
    float cosA = cos( angle );

    int i, j;
    for( j = 0; j < h; j++ )
    {
        for( i = 0; i < w; i++ )
        {
            int byteVal = bitmap[ 2 + j * byteWidth + i / 8 ];
            if( byteVal & ( 0x80 >> ( i % 8 ) ) ) // 0x80 = real upstream B10000000
            {
                int desX = (int)( ( i - centerX ) * cosA - ( j - centerY ) * sinA );
                int desY = (int)( ( i - centerX ) * sinA + ( j - centerY ) * cosA );
                gbDrawPixel( x + desX, y + desY );
            }
        }
    }
}

// Direct port of upstream's own real `getTile()` - `B00000001` -> `1`.
int townGetTile( int x, int y )
{
    return ( townWorld[ y * 16 + x / 8 ] >> ( 7 - ( x % 8 ) ) ) & 1;
}

// -----------------------------------------------------------------------------
// High scores - see header comment on the dropped name-entry widget and
// the manual LSB/MSB byte order.
// -----------------------------------------------------------------------------

void townInitHighscore()
{
    int i;
    for( i = 0; i < TOWN_NUM_HIGHSCORE; i++ )
    {
        int lsb = eeprom_read_byte( i * 2 );
        int msb = eeprom_read_byte( i * 2 + 1 );
        townHighscore[ i ] = ( lsb & 0xFF ) + ( ( msb << 8 ) & 0xFF00 );
        if( townHighscore[ i ] == 0xFFFF ) townHighscore[ i ] = 0;
    }
}

bool townHaveNewHighscore()
{
    return townScoreTotal > townHighscore[ TOWN_NUM_HIGHSCORE - 1 ];
}

// Direct port of upstream's own real bubble-sort-on-insert (descending -
// index 0 is the best score, matching real `haveNewHightScore()`'s own
// comparison against the worst/last entry).
void townApplyHighscore( int score )
{
    townHighscore[ TOWN_NUM_HIGHSCORE - 1 ] = score;

    int i;
    for( i = TOWN_NUM_HIGHSCORE - 1; i > 0; i-- )
    {
        if( townHighscore[ i - 1 ] < townHighscore[ i ] )
        {
            int temp = townHighscore[ i - 1 ];
            townHighscore[ i - 1 ] = townHighscore[ i ];
            townHighscore[ i ] = temp;
        }
        else
          break;
    }

    for( i = 0; i < TOWN_NUM_HIGHSCORE; i++ )
    {
        eeprom_write_byte( i * 2, townHighscore[ i ] & 0xFF );
        eeprom_write_byte( i * 2 + 1, ( townHighscore[ i ] >> 8 ) & 0xFF );
    }
}

// -----------------------------------------------------------------------------
// Time
// -----------------------------------------------------------------------------

void townStopTime()
{
    townCountingTime = false;
}

void townResetTime()
{
    townTime = 0;
    townStopTime();
}

void townInitTime( int tempMax )
{
    townResetTime();
    townTimeLeft = tempMax * 20 * 60;
}

void townUpdateTime()
{
    if( townCountingTime )
      townTime = townTime + 1;
    townTimeLeft = townTimeLeft - 1;
}

// -----------------------------------------------------------------------------
// Player
// -----------------------------------------------------------------------------

void townInitPlayer()
{
    townPlayer.radius = 4;
    townPlayer.x = 20;
    townPlayer.y = 20;
    townPlayer.v = 0;
    townPlayer.vx = 0;
    townPlayer.vy = 0;
    townPlayer.angle = 0;
    townNumClient = -1;
}

// Direct port of upstream's own real `updatePlayer()` - see header comment
// for the preserved `distTotal`/`distClient` bug and the friction-only-on-
// road real behavior.
void townUpdatePlayer()
{
    if( gbRepeat( BTN_RIGHT, 1 ) )
      townPlayer.angle = townPlayer.angle + TOWN_DEMI_PI;
    if( gbRepeat( BTN_LEFT, 1 ) )
      townPlayer.angle = townPlayer.angle - TOWN_DEMI_PI;
    if( gbRepeat( BTN_A, 1 ) )
      townPlayer.v = townPlayer.v + 0.02;
    if( gbRepeat( BTN_B, 1 ) )
    {
        townPlayer.v = townPlayer.v * 0.8;
        townPlayer.vx = townPlayer.vx * 0.8;
        townPlayer.vy = townPlayer.vy * 0.8;
    }

    int currentTile = townGetTile( (int)( townPlayer.x / 16 ), (int)( townPlayer.y / 16 ) );

    // real upstream switch has only case 0 (road) and no default - see
    // header comment, ported as a bare `if` with no `else`.
    if( currentTile == 0 )
    {
        townPlayer.v = townPlayer.v * 0.95;
        townPlayer.vx = townPlayer.vx * 0.9;
        townPlayer.vy = townPlayer.vy * 0.9;
    }

    townPlayer.vx = townPlayer.vx + cos( townPlayer.angle ) * townPlayer.v;
    townPlayer.vy = townPlayer.vy + sin( townPlayer.angle ) * townPlayer.v;

    // collision on the x axis
    townPlayer.x = townPlayer.x + townPlayer.vx;
    currentTile = townGetTile( (int)( townPlayer.x / 16 ), (int)( townPlayer.y / 16 ) );
    if( currentTile == 1 )
    {
        townPlayer.x = townPlayer.x - townPlayer.vx;
        townPlayer.vx = townPlayer.vx * -0.5;
        gbPlayTick();
        townPlayer.v = townPlayer.v * 0.5;
    }

    // collision on the y axis
    townPlayer.y = townPlayer.y + townPlayer.vy;
    currentTile = townGetTile( (int)( townPlayer.x / 16 ), (int)( townPlayer.y / 16 ) );
    if( currentTile == 1 )
    {
        townPlayer.y = townPlayer.y - townPlayer.vy;
        townPlayer.vy = townPlayer.vy * -0.5;
        gbPlayTick();
        townPlayer.v = townPlayer.v * 0.5;
    }

    // FIXED, NOT PRESERVED (see header comment for the full real upstream
    // bug this replaces): real upstream writes `abs(player.v<0)` instead of
    // the clearly-intended `abs(player.v)`, so distTotal/distClient never
    // accumulated any real distance at all - the "Dist:" game-over readout
    // was always "000000" and the drive-efficiency scoring bonus/penalty
    // never actually engaged (always resolving to its floor value). Fixed
    // to the clearly-intended `fabs(townPlayer.v)`.
    townDistTotal = townDistTotal + 8 * fabs( townPlayer.v );
    if( townNumClient > -1 )
      townDistClient = townDistClient + 8 * fabs( townPlayer.v );

    int cameraXTarget = (int)( townPlayer.x + cos( townPlayer.angle ) * townPlayer.v * 64 - LCDWIDTH / 2 );
    int cameraYTarget = (int)( townPlayer.y + sin( townPlayer.angle ) * townPlayer.v * 64 - LCDHEIGHT / 2 );
    townCameraX = ( townCameraX * 3 + cameraXTarget ) / 4;
    townCameraY = ( townCameraY * 3 + cameraYTarget ) / 4;
}

// Direct port of upstream's own real `drawPlayer()` - relies on
// `townDrawWorld()` (called immediately before this every PLAY tick) to
// have left the draw color at BLACK, exactly like upstream relies on the
// same real call ordering (`drawWorld()`'s own explicit `setColor(1)`
// call), no redundant setColor added here.
void townDrawPlayer()
{
    int xScreen = (int)townPlayer.x - townCameraX;
    int yScreen = (int)townPlayer.y - townCameraY;
    if( !( xScreen < -16 || xScreen > LCDWIDTH || yScreen < -16 || yScreen > LCDHEIGHT ) )
      townDrawBitmapAngle( xScreen, yScreen, townGrandTaxiBitmap, townPlayer.angle );
}

// -----------------------------------------------------------------------------
// Clients (passengers)
// -----------------------------------------------------------------------------

// Direct port of upstream's own real `updateClient()`.
void townUpdateClient()
{
    int i;
    for( i = 0; i < TOWN_NB_CLIENT; i++ )
    {
        if( townNumClient == i )
          continue; // client is in the taxi - not displayed/updated here

        int x = townClients[ i * 2 ];
        int y = townClients[ i * 2 + 1 ];
        int camXCenter = (int)townPlayer.x;
        int camYCenter = (int)townPlayer.y;
        int dx = x - camXCenter;
        int dy = y - camYCenter;
        float dist = sqrt( (float)( dx * dx + dy * dy ) );

        if( dist > 150 )
        {
            int spriteID = 1;
            do
            {
                townClients[ i * 2 ] = camXCenter + ( -LCDWIDTH * 2 + arand( LCDWIDTH * 4 ) );
                townClients[ i * 2 + 1 ] = camYCenter + ( -LCDHEIGHT * 2 + arand( LCDHEIGHT * 4 ) );
                spriteID = townGetTile( townClients[ i * 2 ] / 16, townClients[ i * 2 + 1 ] / 16 );
            }
            while( spriteID != 0 );
        }
        else if( townNumClient == -1 && dist < TOWN_DIST_RECUP_DEPOS && fabs( townPlayer.v ) < 0.1 )
        {
            // pick up this client
            townNumClient = i;
            townResetTime();
            townCountingTime = true;
            gbPlayOK();

            int spriteID2 = 1;
            do
            {
                townXDest = 100 + arand( 1900 );
                townYDest = 100 + arand( 1900 );
                spriteID2 = townGetTile( townXDest / 16, townYDest / 16 );
            }
            while( spriteID2 != 0 );

            int db = townXDest - (int)townPlayer.x;
            int ad = townYDest - (int)townPlayer.y;
            townDistNext = sqrt( (float)( db * db + ad * ad ) );
        }
    }

    if( townNumClient > -1 )
    {
        int db = townXDest - (int)townPlayer.x;
        int ad = townYDest - (int)townPlayer.y;
        float ab = sqrt( (float)( db * db + ad * ad ) );
        if( ab < TOWN_DIST_RECUP_DEPOS && fabs( townPlayer.v ) < 0.1 )
        {
            townNbClient = townNbClient + 1;
            townNumClient = -1;
            townStopTime();
            townXDest = -20;
            townYDest = -20;
            gbPlayOK();
            townUpgradeScore();
        }
    }
}

// Direct port of upstream's own real `upgradeScore()` - see header comment
// on the platform-required div-by-zero guard and the preserved bug's real
// effect on this formula (diff is always -100 in practice, so this always
// awards `distNext/2`).
void townUpgradeScore()
{
    if( townDistNext <= 0 )
      townDistNext = 1; // platform-required zero-guard - see header comment

    int diff = (int)( ( townDistClient * 100 ) / townDistNext ) - 100;
    int denom = gbMax( 2, diff );
    townScoreTotal = townScoreTotal + (int)( townDistNext / denom );
    townDistNext = 0;
    townDistClient = 0;
}

// Direct port of upstream's own real `DrawClient()`.
void townDrawClients()
{
    int i;
    for( i = 0; i < TOWN_NB_CLIENT; i++ )
    {
        if( townNumClient == i )
          continue;

        int x = townClients[ i * 2 ];
        int y = townClients[ i * 2 + 1 ];
        int xScreen = x - townCameraX;
        int yScreen = y - townCameraY;
        if( !( xScreen < -16 || xScreen > LCDWIDTH || yScreen < -16 || yScreen > LCDHEIGHT ) )
          gbDrawBitmap( xScreen, yScreen, townClient1Bitmap );
    }

    int xScreen = townXDest - townCameraX;
    int yScreen = townYDest - townCameraY;
    if( !( xScreen < -16 || xScreen > LCDWIDTH || yScreen < -16 || yScreen > LCDHEIGHT ) )
    {
        gbDrawCircle( xScreen, yScreen, 4 );
        gbDrawCircle( xScreen, yScreen, 1 );
    }
}

// -----------------------------------------------------------------------------
// World
// -----------------------------------------------------------------------------

// Direct port of upstream's own real `drawWorld()` - real loop bounds are
// NOT swapped here (see header comment).
void townDrawWorld()
{
    gbSetColor( 1 );

    int yLo = gbMax( 0, townCameraY / 16 );
    int yHi = gbMin( TOWN_WORLD_H, ( townCameraY + LCDHEIGHT ) / 16 + 1 );
    int xLo = gbMax( 0, townCameraX / 16 );
    int xHi = gbMin( TOWN_WORLD_W, ( townCameraX + LCDWIDTH ) / 16 + 1 );

    int x, y;
    for( y = yLo; y < yHi; y++ )
    {
        for( x = xLo; x < xHi; x++ )
        {
            int spriteID = townGetTile( x, y );
            if( spriteID == 0 )
              continue;

            int numSprite = 2; // default: Foret

            int i;
            for( i = 0; i < TOWN_NB_ZONE; i++ )
            {
                int decalage = i * 5;
                int x1 = townZones[ decalage ];
                int y1 = townZones[ decalage + 1 ];
                int x2 = townZones[ decalage + 2 ];
                int y2 = townZones[ decalage + 3 ];
                if( gbCollidePointRect( x, y, x1, y1, x2 - x1, y2 - y1 ) )
                {
                    numSprite = townZones[ decalage + 4 ];
                    break;
                }
            }

            int xScreen = x * 16 - townCameraX;
            int yScreen = y * 16 - townCameraY;
            gbDrawBitmap( xScreen, yScreen, townSprites[ numSprite ] );
        }
    }
}

// -----------------------------------------------------------------------------
// HUD - direct port of upstream's own real `drawHud()` (uses the real
// rounded-rect and INVERT primitives; see header comment).
// -----------------------------------------------------------------------------

void townDrawHud()
{
    gbSetColor( 0 ); // WHITE
    gbFillCircle( 4, 43, 4 );
    gbSetColor( 1 ); // BLACK
    gbDrawCircle( 4, 43, 4 );

    gbSetColorBg( 1, 0 ); // real setColor(BLACK, WHITE)
    gbSetFont( gbFont3x3 );
    townPrintPaddedAt( townTimeLeft, 0, 0, 5 );
    townPrintPaddedAt( townTime, 65, 0, 5 );
    townPrintPaddedAt( townNbClient, 69, 45, 4 );

    if( townNumClient > -1 )
    {
        int ac = 4;
        int db = townXDest - (int)townPlayer.x;
        int ad = townYDest - (int)townPlayer.y;
        float ab = sqrt( (float)( db * db + ad * ad ) );
        if( ab < 0.01 )
          ab = 0.01; // platform-required zero-guard - see header comment
        float ec = ( (float)ac / ab ) * db;
        float ae = ( (float)ac / ab ) * ad;
        gbDrawLine( 4, 43, 4 + (int)ec, 43 + (int)ae );

        gbSetColor( 1 ); // BLACK
        gbFillRoundRect( 27, 0, 21, 7, 3 );
        // real upstream: setColor(INVERT) - the badge background is
        // guaranteed freshly solid BLACK from the fill just above, so
        // GB_INVERT's XOR here matches upstream's own call exactly.
        gbSetColor( GB_INVERT );
    }
    else
    {
        gbSetColor( 0 ); // WHITE
        gbFillRoundRect( 27, 0, 21, 7, 3 );
        gbSetColor( 1 ); // BLACK
        gbDrawRoundRect( 27, 0, 21, 7, 3 );
    }
    townDisplayText( "TAXI", 30, 2 );

    gbSetFont( gbFont3x5 );
    gbSetColor( 1 );
}

// -----------------------------------------------------------------------------
// Mini-map - direct port of upstream's own real `drawMiniMap()`. Real
// upstream temporarily repurposes the same camera_x/camera_y globals as
// TILE-space pan coordinates for this screen, saving/restoring the real
// pixel-space camera around it - ported the same way here.
// -----------------------------------------------------------------------------

void townBeginMiniMap()
{
    townSavedCameraX = townCameraX;
    townSavedCameraY = townCameraY;
    townCameraY = (int)( townPlayer.y / 16 );
    townCameraX = (int)( townPlayer.x / 16 );
    townMiniMapFrame = 0;
    townState = TOWN_STATE_MINIMAP;
}

void townUpdateMiniMap()
{
    int yHi = gbMin( TOWN_WORLD_H, townCameraY + LCDHEIGHT );
    int xHi = gbMin( TOWN_WORLD_W, townCameraX + LCDWIDTH );

    int x, y;
    for( y = townCameraY; y < yHi; y++ )
    {
        for( x = townCameraX; x < xHi; x++ )
        {
            if( townGetTile( x, y ) == 0 )
              continue;
            gbDrawPixel( x - townCameraX, y - townCameraY );
        }
    }

    if( townMiniMapFrame % 20 > 9 )
      gbDrawPixel( (int)( townPlayer.x / 16 ) - townCameraX, (int)( townPlayer.y / 16 ) - townCameraY );
    if( townMiniMapFrame % 20 > 9 )
      gbDrawPixel( townXDest / 16 - townCameraX, townYDest / 16 - townCameraY );

    if( gbRepeat( BTN_LEFT, 1 ) && townCameraX > 0 )
      townCameraX = townCameraX - 1;
    if( gbRepeat( BTN_RIGHT, 1 ) && townCameraX < ( TOWN_WORLD_W - LCDWIDTH ) )
      townCameraX = townCameraX + 1;
    if( gbRepeat( BTN_UP, 1 ) && townCameraY > 0 )
      townCameraY = townCameraY - 1;
    if( gbRepeat( BTN_DOWN, 1 ) && townCameraY < ( TOWN_WORLD_H - LCDHEIGHT ) )
      townCameraY = townCameraY + 1;

    if( gbPressed( BTN_A ) || gbPressed( BTN_C ) || gbPressed( BTN_B ) )
    {
        townCameraX = townSavedCameraX;
        townCameraY = townSavedCameraY;
        townBeginMenu();
        return;
    }

    townMiniMapFrame = townMiniMapFrame + 1;
}

// -----------------------------------------------------------------------------
// Game-over screen - direct port of upstream's own real `GameOverScreen()`.
// -----------------------------------------------------------------------------

void townBeginGameOver()
{
    townState = TOWN_STATE_GAMEOVER;
    townGameOverFrame = TOWN_GAMEOVER_FRAMES;
    townScoreCpt = 0;
}

void townUpdateGameOver()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbPrintString( "Game Over\n" );
    gbSetFont( gbFont3x5 );
    gbPrintString( "Clients:" );
    townPrintPaddedHere( townNbClient, 4 );
    gbPrintString( "\n" );
    gbPrintString( "Dist:" );
    townPrintPaddedHere( (int)townDistTotal, 6 );
    gbPrintString( "\n" );
    gbPrintString( "Score:" );
    townPrintPaddedHere( (int)townScoreCpt, 8 );

    if( townGameOverFrame > 0 )
    {
        townGameOverFrame = townGameOverFrame - 1;
        townScoreCpt = townScoreCpt + (float)townScoreTotal / TOWN_GAMEOVER_FRAMES;
    }
    else
    {
        townScoreCpt = townScoreTotal;

        if( townHaveNewHighscore() )
        {
            gbCursorX = 20 + arand( 2 );
            gbCursorY = 30 + arand( 2 );
            gbPrintString( "New records!\n" );
        }

        if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
        {
            gbPlayOK();
            if( townHaveNewHighscore() )
            {
                townApplyHighscore( townScoreTotal );
                townBeginHighscores();
            }
            else
              townBeginMenu();
        }
    }
}

// -----------------------------------------------------------------------------
// High scores screen - direct port of upstream's own real `drawHighScores()`
// minus the dropped name column (see header comment). No explicit font is
// set here, matching upstream's own real lack of a setFont() call in this
// function - it inherits whatever font was last active.
// -----------------------------------------------------------------------------

void townBeginHighscores()
{
    townState = TOWN_STATE_HIGHSCORES;
}

void townUpdateHighscores()
{
    gbSetColor( 1 );
    gbCursorX = 9 + arand( 2 );
    gbCursorY = 0 + arand( 2 );
    gbPrintString( "BEST SCORE\n" );

    int thisScore;
    for( thisScore = 0; thisScore < TOWN_NUM_HIGHSCORE; thisScore++ )
    {
        gbCursorX = LCDWIDTH - 4 * gbFontWidth;
        gbCursorY = gbFontHeight + gbFontHeight * thisScore;
        gbPrintNumber( townHighscore[ thisScore ] );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        townBeginMenu();
    }
}

// -----------------------------------------------------------------------------
// Pause menu - a reasonable approximation of the real built-in gb.menu()
// widget, see header comment.
// -----------------------------------------------------------------------------

void townBeginMenu()
{
    townState = TOWN_STATE_MENU;
    townMenuIndex = 0;
    gbSetFont( gbFont3x5 ); // this port's own deliberate choice - see header comment
}

void townUpdateMenu()
{
    gbSetColor( 1 );

    int i;
    for( i = 0; i < TOWN_PAUSE_MENU_COUNT; i++ )
    {
        gbCursorX = 2;
        gbCursorY = i * 6;
        if( i == townMenuIndex )
          gbPrintString( ">" );

        gbCursorX = 8;
        gbCursorY = i * 6;
        if( i == 0 ) gbPrintString( "Continue" );
        else if( i == 1 ) gbPrintString( "3 min play" );
        else if( i == 2 ) gbPrintString( "5 min play" );
        else if( i == 3 ) gbPrintString( "10 min play" );
        else if( i == 4 ) gbPrintString( "Map" );
        else if( i == 5 ) gbPrintString( "High scores" );
        else gbPrintString( "Main Menu" );
    }

    if( gbPressed( BTN_UP ) )
    {
        gbPlayTick();
        townMenuIndex = townMenuIndex - 1;
        if( townMenuIndex < 0 )
          townMenuIndex = TOWN_PAUSE_MENU_COUNT - 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        gbPlayTick();
        townMenuIndex = townMenuIndex + 1;
        if( townMenuIndex > TOWN_PAUSE_MENU_COUNT - 1 )
          townMenuIndex = 0;
    }

    // real `default:` case (any unhandled gb.menu() result, including the
    // real "Main Menu" entry, which upstream's own switch never gives its
    // own explicit case) - see header comment.
    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        townBeginTitle();
        return;
    }

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( townMenuIndex == 1 )
        {
            townInitGame( 3 );
            townBeginPlay();
        }
        else if( townMenuIndex == 2 )
        {
            townInitGame( 5 );
            townBeginPlay();
        }
        else if( townMenuIndex == 3 )
        {
            townInitGame( 10 );
            townBeginPlay();
        }
        else if( townMenuIndex == 0 )
        {
            if( townTimeLeft > 0 )
              townBeginPlay();
        }
        else if( townMenuIndex == 4 )
          townBeginMiniMap();
        else if( townMenuIndex == 5 )
          townBeginHighscores();
        else
          townBeginTitle();
    }
}

// -----------------------------------------------------------------------------
// Title screen - see header comment on the dropped built-in boot-logo
// overlay (an already-established, project-wide simplification).
// -----------------------------------------------------------------------------

void townBeginTitle()
{
    townState = TOWN_STATE_TITLE;
    gbSetFont( gbFont5x7 );
}

void townUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( 0, 12, townLogoBitmap );
    gbCursorX = 21;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbPickRandomSeed();
        townBeginMenu();
    }
}

// -----------------------------------------------------------------------------
// Play - direct port of upstream's own real `initGame()`/`play()`.
// -----------------------------------------------------------------------------

void townInitGame( int tempMax )
{
    townInitPlayer();
    townDistTotal = 0;
    townDistNext = 0;
    townDistClient = 0;
    townNbClient = 0;
    townScoreTotal = 0;
    townInitTime( tempMax );
}

void townBeginPlay()
{
    gbSetFont( gbFont3x5 ); // real upstream's own `play()`-top `gb.display.setFont(font3x5)` - runs on every entry, fresh or Continue
    townState = TOWN_STATE_PLAY;
}

void townUpdatePlay()
{
    // pause the game if C is pressed - font5x7 restore is moot here since
    // townBeginMenu() immediately forces its own font anyway.
    if( gbPressed( BTN_C ) )
    {
        townBeginMenu();
        return;
    }

    if( townTimeLeft == 0 )
    {
        townBeginGameOver();
        return;
    }

    townUpdatePlayer();
    townUpdateTime();
    townUpdateClient();
    townDrawWorld();
    townDrawPlayer();
    townDrawClients();
    townDrawHud();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameCrazyTown_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 ); // real upstream setup()'s own gb.display.setFont(font5x7)
    townInitHighscore();
    townXDest = 100; // real upstream setup()'s own initial placeholder, never read before the first real pickup randomizes it
    townYDest = 100;
    townBeginTitle();
}

void gameCrazyTown_update()
{
    if( !gbUpdate() ) return;

    if( townState == TOWN_STATE_TITLE ) townUpdateTitle();
    else if( townState == TOWN_STATE_MENU ) townUpdateMenu();
    else if( townState == TOWN_STATE_PLAY ) townUpdatePlay();
    else if( townState == TOWN_STATE_GAMEOVER ) townUpdateGameOver();
    else if( townState == TOWN_STATE_HIGHSCORES ) townUpdateHighscores();
    else townUpdateMiniMap();

    gbRenderFrame();
}
