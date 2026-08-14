#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Shipwrek (yawn-g, license: none specified upstream - github.com/yawn-g/
// shipwrek). A two-player, single-cartridge Battleship: each player places
// 5 boats (Cruiser..Carrier) on their own hidden 9x9 grid, then players
// alternate turns firing at the other's grid until every one of an
// opponent's boats is sunk. Picked from `more games/` per this project's
// own DISCOVERED_GAMES.md porting-priority audit, specifically as a real,
// expected EEPROM consumer (see the EEPROM section below for why that
// expectation did NOT pan out once the real source was read).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment - this
// dialect has no classes/methods). Every global got a `ship`-prefixed name
// (this cartridge has no linker). Upstream's own `switch(b){case 4: ...}`
// bitmap/text dispatches (draw_boat(), sunk_popup()) became if/else-if
// chains (no switch statement proven to work in this dialect - see this
// project's own CLAUDE.md). Every real `B00000000`-style Arduino binary
// literal across all 13 PROGMEM bitmap tables (carrier/submarine/cruiser/
// battle_ship/destroyer/logo/clouds/cloudsW/clouds2/game_text/
// game_outline/over_text/over_outline) was mechanically converted to `0x`
// hex via a one-off script (byte-for-byte, verified against the source -
// not retyped by hand) - every byte became a plain `int` cell, matching
// this project's own established convention. `random(15,70)` (Arduino's
// ranged random, exclusive upper bound) became `arand(55) + 15`.
// `gb.pickRandomSeed()` -> `gbPickRandomSeed()` (documented no-op).
// `Serial.begin(9600)` and `gb.battery.show = false` were both dropped
// outright (real-hardware-only debug/cosmetic concerns with no equivalent
// here - same treatment gamePong.c already gave `gb.battery.show`).
// Upstream's own `char`/`byte` arrays all became plain `int` (this dialect
// has no byte-width types - see this project's own CLAUDE.md dialect
// rules); array declarations use this dialect's own `int[N] name` order
// throughout, never `int name[N]`.
//
// Upstream's own boolean-flip idiom for a 0/1 value x (`-x + 1`, used for
// `boat_rot = -boat_rot + 1;`, `shots[-p+1]`, `-p+1`, `sunk(-p+1, ...)`,
// `p_name[-p+1]` etc, all over the source) is rewritten everywhere here as
// the equivalent `1 - x` - purely a readability substitution, not a
// behavior change (every one of these values is always exactly 0 or 1).
//
// -----------------------------------------------------------------------
// STATE-MACHINE CONVERSION (the big structural rewrite)
// -----------------------------------------------------------------------
// Upstream's real loop() is one giant nest of blocking `while`/`for` loops
// around `if (gb.update()) { ... }` (placement: `for(p) { while(b>=0) {
// if(gb.update()){...} } }`; then `while(playing) { for(p) { for(steps) {
// while(waiting){if(gb.update()){...}} } while(waiting_for_shot){
// if(gb.update()){...}} } }`) - converted here into an explicit state
// machine (`SHIP_STATE_TITLE/PLACE/ANIM/SHOOT`), matching the same
// "blocking loop -> explicit resumable state" treatment gamePong.c's own
// header comment documents for `gb.titleScreen()`. Every real local
// variable that used to live on the call stack across loop iterations
// (`p`, `b`, `cur_x`, `cur_y`, `boat_rot`, `steps`, the per-turn anim
// counters) became a persistent global instead (`shipP`/`shipB`/
// `shipCurX`/`shipCurY`/`shipBoatRot`/`shipAnimSteps`/...), advanced once
// per real `gbUpdate()` tick by button presses exactly the way upstream's
// own nested `while(gb.update())` loops were themselves advanced once per
// real Arduino `loop()` call - the two are behaviorally identical, just
// with the call stack traded for global state. The always-true `while (b
// >= 0)` loop condition upstream relies on (byte `b` never actually goes
// negative - termination is entirely via an internal `break`) needed no
// special handling here: it's just an explicit `if (shipB == 0) { ...
// transition ...; return; } else shipB = shipB - 1;` instead.
//
// Two subtleties were preserved deliberately rather than simplified away,
// because they produce genuine, reachable real-hardware behavior:
//
// 1. Upstream's own `title_screen()` (played from BOTH the initial
//    `setup()` AND a mid-game Button-C press) is a real BLOCKING call that
//    shows the splash and waits for A, then returns control to the exact
//    same point in the exact same nested loop it was called from - i.e. on
//    real hardware, Button C mid-game is a "pause and show the splash,
//    resume exactly where you left off" gesture, NOT a quit/reset. Ported
//    faithfully via `shipReturnState` (remembers which of PLACE/ANIM/SHOOT
//    to resume into) rather than "fixing" it into an actual quit - the
//    shared cartridge-level Start-button quit-confirmation dialog
//    (wired centrally in portVircon32.c) is the real way back to the menu;
//    this game's own Button C never touches that at all, exactly like
//    upstream.
// 2. Upstream's own game-over handling has NO explicit "you win, press
//    anything to restart" screen at all: `playing` only flips false deep
//    inside `update_game_over_anim()` once its own frame counter exceeds
//    41 (rendering the sliding "GAME OVER" banner for ~2.1 real seconds at
//    20fps) - checked ONLY at the top of the per-player `for` loop's next
//    iteration. Since a human needs 2-3 real Button-A presses to click
//    through the anim phase's own 3 steps, it's genuinely possible (a fast
//    player) for the OTHER player to still get one more full turn before
//    the game notices `playing == false` and Arduino's own `loop()`
//    re-invocation naturally restarts everything via a fresh
//    `reset_game()` call - straight back to boat placement, no title
//    screen shown. This emergent, timing-dependent behavior is reproduced
//    here as-is (not hard-coded into "game over -> immediate restart"):
//    `shipUpdateAnimPhase()`'s own step-3 transition checks `shipPlaying`
//    at exactly the same logical point upstream does (after this player's
//    own anim+shoot turn fully resolves, before deciding whether to hand
//    the next player a turn or restart), using the same real per-frame
//    `shipGameOverAnimFc` accumulation and the same real button-press-gated
//    step advancement - so the same "bonus turn if you clicked through
//    fast enough" quirk can occur here too, exactly as on real hardware.
//
// -----------------------------------------------------------------------
// REAL EEPROM CHECK - genuinely came back negative for this game
// -----------------------------------------------------------------------
// This game was picked specifically because it ships a real
// `shipwreck.ino.eep` file upstream, expected to be this project's first
// real EEPROM consumer. Checked directly rather than assumed: grepping the
// entire upstream source (shipwreck.ino/functions.ino/todo.ino - the
// latter two are effectively empty, a blank line and a comment-only to-do
// list respectively) for "EEPROM" found ZERO matches - this game never
// calls `EEPROM.read()`/`write()`/`update()` anywhere. The shipped `.eep`
// file itself was inspected too: it's exactly `:00000001FF` - a single
// Intel-HEX "end of file" record with no data records at all, i.e. the
// default empty stub the Arduino/avr-gcc toolchain writes for EVERY sketch
// regardless of whether it touches EEPROM, not real save data. So despite
// this project's own CLAUDE.md describing shipwrek as "a natural first
// EEPROM consumer," the real source has nothing to wire up - the
// `eeprom_*()` primitives (`eepromShim.h`) are correctly left uncalled
// here, matching what upstream actually does (nothing), not what its
// filename made it look like it might do. No high score or placement
// layout is persisted; every game starts fresh from the title screen.
//
// -----------------------------------------------------------------------
// SHIM PRIMITIVES USED
// -----------------------------------------------------------------------
// - **`gb.popup(text, duration)`** - this game leans on it heavily (9 real
//   call sites: "Can't overlap boats", "Already shot there!", "HIT!!!",
//   "Miss...", and one "<Boat> sunk!" per boat type) - not a cosmetic
//   nice-to-have here, a real, load-bearing part of the game's feedback
//   loop. Every call site here calls `gbPopup(text, duration)` directly
//   (the exact same real upstream durations); `gbPopup()` in
//   `gamebuinoShim.h`/`.c` is a direct port of real `Gamebuino::popup()`/
//   `updatePopup()` and auto-draws itself, real-hardware-accurate
//   slide-in animation included, on top of everything else already drawn
//   that frame.
// - **`gb.display.println(...)` (and the bare `gb.display.println()`
//   line-break form) has no direct one-call equivalent** - only
//   `gbPrintString()`/`gbPrintNumber()` (no auto-newline) exist. Handled
//   locally with a tiny `shipNewline()` helper (advances `gbCursorY` by
//   one real font cell and resets `gbCursorX` to 0, mirroring
//   `gbPrintString()`'s own internal `'\n'` handling exactly) - used by
//   `shipPrintInZone()`/`shipPrintInZoneWithNumber()` and directly
//   wherever upstream calls bare `println()`. This is a trivial, low-risk
//   local helper (not a design decision worth debating), but is flagged
//   here anyway since `println()` is a real, named upstream API this shim
//   doesn't expose as a single call.
//
// Neither of the above rises to "large missing subsystem" (the tracker/
// pattern sound engine and an on-screen keyboard are this project's own
// already-documented, deliberate scope limits) - both are small.
//
// -----------------------------------------------------------------------
// OTHER REAL QUIRKS FOUND, PRESERVED (not "fixed") - fidelity to upstream
// -----------------------------------------------------------------------
// - Real single-arg `GRAY` (the shim's own `GB_GRAY`, a dithered checkerboard
//   color) is used exactly where upstream uses it: both cloud layers' own
//   back/shadow pass, own-boat overlays in `draw_boats()`, sunk-own-boat
//   reveal in `draw_shots()`, the boat's own waterline-mask band, and the
//   SHOOT-phase aim-reticle crosshair.
// - `shipUpdateClouds()`'s own real 3-layer bitmap technique (a GRAY
//   "shadow" cloud layer, THEN a WHITE mask cloud silhouette, THEN a BLACK
//   outline on top, all at the same x position) is exactly the "fill/mask
//   before an outline bitmap" pattern this port's own instructions
//   specifically warn about - all three layers were restored and preserved
//   in upstream's own exact order; dropping the WHITE mask layer would let
//   whatever's already drawn underneath bleed through the cloud silhouette.
// - `shipUpdateBoat()`'s own real `drawBitmap(10, ..., logo)` call has NO
//   explicit `setColor()` of its own immediately before it - it relies
//   entirely on whatever color the immediately-preceding
//   `shipUpdateClouds()` call left set (which always ends by explicitly
//   setting BLACK right before its own final two bitmap draws) - a real,
//   deterministic (not ambiguous) implicit-color-carryover quirk, since
//   this port calls the two functions back-to-back in the same fixed
//   order upstream does. Preserved exactly rather than "helpfully" adding
//   a redundant `gbSetColor()` upstream itself never has.
// - `shipUpdateSinkAnim()` runs unconditionally every frame (not gated by
//   `shipSinking`, only its OWN internal body is gated) directly after
//   `shipUpdateBoat()` already drew the same `shipLogo` bitmap once this
//   same frame at a slightly different y (before vs. after that frame's
//   own possible `shipBoatAnimY` increment) - upstream's own real
//   `update_boat()` + `update_sink_anim()` pairing does this too, so once
//   sinking starts, a handful of frames genuinely draw the ship bitmap
//   TWICE (at two adjacent y offsets) before the waterline-mask fillRect
//   papers back over both. A real, minor upstream double-draw quirk,
//   preserved as-is rather than de-duplicated.
// - `sfx()` is a direct, byte-for-byte port of upstream's own real
//   `sfx(fxno,channel)` - `shipSfx()` calls the shim's own real
//   tracker-engine primitives (`gbSoundCommand()` for volume/waveform/
//   slide/arpeggio, `gbPlayNoteChannel()` for the note itself), matching
//   upstream's own exact call order/argument shape one-for-one.
//   `shipSoundFx[2][8]` is upstream's own real `soundfx[2][8]` table
//   verified byte-for-byte. All 3 real call sites (sunk, hit, launch) are
//   restored with upstream's own exact fxno/channel arguments (channel
//   always 0, matching upstream).
// - `draw_shots()`'s own inner-block `byte x, y; bool dir;` locals (used
//   right inside a function whose OUTER loop already uses `x`/`y` as loop
//   variables) were renamed to `bx`/`by`/`bdir` here defensively, purely
//   to avoid depending on an unverified corner case of this dialect's own
//   block-scoping/shadowing rules - not an upstream behavior change.
// - The many `uselessN` globals (1 through 36, all dead - never read
//   anywhere) and the unused `sinking_anim_start`/`arrow_step_duration`
//   locals were dropped outright, matching gameConduit.c's own precedent
//   for dropping a real, provably-dead upstream debug leftover
//   (`arrow_step_duration`'s single value, 10, is inlined directly at its
//   one real use site instead).
// - `p_name[2][9]`/`boat_name[5][9]` (real mutable/fixed name tables) were
//   ported as small `shipPName(p)`/`shipBoatNameFor(b)` helper functions
//   returning a fixed string literal per index via if/else, rather than a
//   literal 2D string array - this game never actually renames players
//   (no on-screen keyboard - an already-documented scope limit), so
//   nothing is lost, and it sidesteps ever needing to confirm this
//   dialect's own nested string-array initializer syntax against a case
//   this project hadn't already proven elsewhere.

enum ShipState
{
    SHIP_STATE_TITLE = 0,
    SHIP_STATE_PLACE = 1,
    SHIP_STATE_ANIM = 2,
    SHIP_STATE_SHOOT = 3
};

int shipState;
int shipReturnState; // -1 = "never played yet" sentinel, else the PLACE/ANIM/SHOOT phase to resume into after a Button-C pause

int shipP;        // current active player (0/1) across PLACE/ANIM/SHOOT
int shipB;        // current boat index during PLACE (4 down to 0)
int shipCurX;      // grid cursor - placement cursor during PLACE, aim cursor during SHOOT
int shipCurY;
int shipBoatRot;   // 0/1 orientation flag during PLACE
int shipAnimSteps; // 0/1/2 during ANIM

bool shipPlaying;
bool shipDisplayEnemyShots;
bool shipGameOver;
int shipNbShots[2];
int shipLastCurX[2];
int shipLastCurY[2];

int shipGameOverAnimFc;
int shipCloudsX;
int shipClouds2X;
int shipBoatAnimFc;
int shipBoatAnimY;
bool shipBoatAnimFloatY;
bool shipSinking;
int shipArrowFc;
bool shipPopupBlocker;
int shipAnimFc;
int shipExplosionX;

char shipPressA[4]; // {21, 16 or ' ', ' ' or 16, 0} - the blinking "press A" arrow indicator

// boat positions x, y, dir (255 for x means not placed)
int shipBoatPos[2][5][3];

// shots map: 255 = not shot at, 254 = miss, 0-4 = hit boat number
int shipShots[2][9][9];

// {waveform, pitch, pmd/pmt-ish, vmt, vmd, vol-slide, volume, length} - real upstream soundfx[2][8] table, byte-for-byte.
int shipSoundFx[2][8] =
{
    {1,0,57,1,2,9,7,20}, // launch
    {1,4,58,0,1,4,7,17}, // explode
};

// -----------------------------------------------------------------------
// Bitmaps - byte-for-byte converted from upstream's own real PROGMEM
// tables (Arduino B-binary literals -> 0x hex, one int per byte, {width,
// height} header preserved) - see this file's own header comment.
// -----------------------------------------------------------------------

int shipCarrier[18] =
{
    32,4,
    0xFF,0xFF,0xFF,0x0,
    0xFF,0xFF,0xFF,0xC0,
    0xFF,0xFF,0xFF,0xC0,
    0xFF,0xFF,0xFF,0x0,
};

int shipSubmarine[10] =
{
    16,4,
    0x7F,0x80,
    0xFF,0xE0,
    0xFF,0xE0,
    0x7F,0x80,
};

int shipCruiser[6] =
{
    8,4,
    0x78,
    0x7C,
    0x7C,
    0x78,
};

int shipBattleShip[14] =
{
    24,4,
    0x7F,0xFF,0xE0,
    0xFF,0xFF,0xF8,
    0xFF,0xFF,0xF8,
    0x7F,0xFF,0xE0,
};

int shipDestroyer[10] =
{
    16,4,
    0x7F,0xFC,
    0xFF,0xFF,
    0xFF,0xFF,
    0x7F,0xFC,
};

int shipLogo[242] =
{
    64,30,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x4,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x3,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x4,0x0,0x0,0x0,0x0,
    0x0,0x0,0x1,0x6,0x0,0x0,0x0,0x0,
    0x0,0x0,0x7,0xB,0x80,0x0,0x0,0x0,
    0x0,0x0,0x7,0x7D,0xE,0x0,0x0,0x0,
    0x0,0x2,0x8F,0xFF,0xF8,0x12,0x31,0xFF,
    0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFC,
    0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8,
    0xF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
    0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
    0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
};

int shipClouds[112] =
{
    88,10,
    0x40,0x0,0x1,0x0,0x8,0x0,0x0,0x0,0x0,0xC0,0x8,
    0x40,0x0,0x0,0x80,0x4,0x0,0x0,0x0,0x1,0x30,0x30,
    0x20,0x0,0x0,0x78,0x7C,0x0,0x0,0x0,0x1,0xF,0xC0,
    0x10,0x0,0x0,0x47,0x82,0x0,0x0,0x0,0x2,0x0,0x0,
    0xC,0x0,0x1,0x80,0x1,0x80,0x0,0x0,0xC,0x0,0x0,
    0x3,0x80,0xE,0x0,0x0,0x40,0x0,0x0,0x10,0x0,0x0,
    0x0,0x7F,0xF0,0x0,0x0,0x38,0x0,0x0,0xE0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x7,0x80,0xF,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x7F,0xF0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
};

int shipCloudsw[112] =
{
    88,10,
    0x3F,0xFF,0xFE,0xFF,0xF7,0xFF,0xFF,0xFF,0xFF,0x3F,0xF0,
    0x3F,0xFF,0xFF,0x7F,0xFB,0xFF,0xFF,0xFF,0xFE,0xF,0xC0,
    0x1F,0xFF,0xFF,0x87,0x83,0xFF,0xFF,0xFF,0xFE,0x0,0x0,
    0xF,0xFF,0xFF,0x80,0x1,0xFF,0xFF,0xFF,0xFC,0x0,0x0,
    0x3,0xFF,0xFE,0x0,0x0,0x7F,0xFF,0xFF,0xF0,0x0,0x0,
    0x0,0x7F,0xF0,0x0,0x0,0x3F,0xFF,0xFF,0xE0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x7,0xFF,0xFF,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x7F,0xF0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
};

int shipClouds2[156] =
{
    88,14,
    0x21,0x0,0x1,0x0,0x20,0x0,0x20,0x0,0x80,0x0,0x4,
    0x21,0x0,0x1,0x0,0x40,0x0,0x40,0x0,0x80,0x0,0x4,
    0x10,0x80,0x0,0x80,0x40,0x0,0x40,0x0,0x80,0x0,0x2,
    0x8,0x40,0x0,0x40,0x40,0x0,0x40,0x0,0x40,0x0,0x1,
    0x87,0xB0,0x0,0x30,0x20,0x0,0x20,0x0,0x40,0x0,0x10,
    0x40,0xFF,0x0,0x3E,0x10,0x0,0x20,0x0,0x20,0x0,0x28,
    0x3F,0x50,0xFF,0xE1,0xF8,0x0,0x10,0x0,0x18,0x0,0xC7,
    0x0,0x20,0xCC,0x40,0x7,0x0,0x7C,0x0,0x36,0x3,0xC0,
    0x0,0x30,0x70,0x40,0x1C,0xFF,0xA3,0x0,0xC1,0xFC,0x20,
    0x0,0x4F,0x8C,0x40,0x20,0x4,0x20,0xFF,0x1,0x0,0x20,
    0x0,0x40,0x3,0xE0,0x10,0x4,0x18,0x31,0x83,0xC0,0xC0,
    0xE3,0x80,0x0,0x1C,0x3C,0x1B,0x37,0xC0,0x7C,0x3F,0x3C,
    0x1C,0x0,0x0,0x3,0xC3,0xE0,0xC0,0x0,0x0,0x0,0x3,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
};

int shipGameText[79] =
{
    56,11,
    0xF,0xC0,0x60,0x7C,0x1F,0x7F,0xC0,
    0x38,0xC0,0xE0,0x3C,0x3C,0x38,0xC0,
    0x60,0x40,0xF0,0x3C,0x3C,0x38,0x40,
    0x60,0x41,0xF0,0x2C,0x7C,0x30,0x0,
    0xE0,0x1,0x30,0x2E,0x58,0x33,0x0,
    0xE0,0x2,0x30,0x6E,0x98,0x7E,0x0,
    0xC7,0xE7,0xF0,0x6E,0x98,0x72,0x0,
    0xE1,0x84,0x38,0x47,0x38,0x70,0x0,
    0xE1,0x8C,0x38,0x47,0x38,0x60,0x80,
    0x73,0x98,0x38,0xC6,0x38,0xE1,0x80,
    0x3F,0x3C,0x7D,0xE6,0xFD,0xFF,0x80,
};

int shipGameOutline[93] =
{
    56,13,
    0xF,0xF0,0x78,0x7F,0x1F,0xFF,0xF0,
    0x38,0x10,0xC8,0x41,0x30,0x40,0x10,
    0x63,0x90,0x8C,0x61,0x21,0xE3,0x90,
    0x4E,0xD1,0x84,0x21,0x61,0x22,0xD0,
    0xC8,0x51,0x4,0x29,0xC1,0x27,0xF0,
    0x88,0x73,0x64,0x68,0xD3,0x66,0x40,
    0x8F,0xFE,0xE4,0x48,0xB2,0x40,0xC0,
    0x9C,0xC,0x6,0x48,0xB2,0x46,0x80,
    0x8F,0x3D,0xE2,0x5C,0x62,0x47,0xE0,
    0x8F,0x39,0x22,0xD4,0x62,0xCD,0xA0,
    0xC6,0x33,0x63,0x9C,0xE3,0x8F,0x20,
    0x60,0x61,0x41,0xC,0x81,0x0,0x20,
    0x3F,0xFF,0x7F,0xFF,0xFF,0xFF,0xE0,
};

int shipOverText[68] =
{
    48,11,
    0xF,0x8F,0x8F,0x7F,0xCF,0xF0,
    0x31,0xC7,0x6,0x38,0xC7,0x38,
    0x70,0xE7,0xC,0x38,0x47,0x38,
    0x60,0xE7,0x8,0x30,0x6,0x38,
    0xE0,0xE3,0x18,0x33,0x6,0x30,
    0xE0,0xE3,0x30,0x7E,0xF,0xE0,
    0xE0,0xE3,0x30,0x72,0xE,0xC0,
    0xE0,0xC3,0xE0,0x70,0xC,0xE0,
    0xE1,0xC3,0xC0,0x60,0x8C,0x60,
    0x63,0x81,0xC0,0xE1,0x9C,0x70,
    0x3E,0x1,0x81,0xFF,0xBE,0x3C,
};

int shipOverOutline[80] =
{
    48,13,
    0xF,0xEF,0xEF,0xFF,0xFF,0xFC,
    0x38,0x38,0x28,0x40,0x18,0x6,
    0x67,0x1C,0x6C,0xE3,0x9C,0x62,
    0x45,0x8C,0x49,0xA2,0xD4,0x62,
    0xCC,0x8C,0x5B,0x27,0xF4,0xE2,
    0x88,0x8E,0x72,0x66,0x4C,0xE6,
    0x88,0x8A,0x66,0x40,0xC8,0xC,
    0x88,0x8A,0x64,0x46,0x88,0x98,
    0x89,0x9A,0xC,0x47,0xE9,0x88,
    0x8B,0x12,0x18,0xCD,0xB9,0xCC,
    0xCE,0x33,0x11,0x8F,0x31,0xC6,
    0x60,0xE1,0x31,0x0,0x20,0xE2,
    0x3F,0x81,0xE1,0xFF,0xFF,0xBE,
};

// -----------------------------------------------------------------------
// Small name-table helpers (see this file's own header comment on why
// these are plain if/else functions rather than a 2D string array)
// -----------------------------------------------------------------------

char* shipPName( int p )
{
    if( p == 0 ) return "Player1";
    return "Player2";
}

char* shipBoatNameFor( int b )
{
    if( b == 0 ) return "Cruiser";
    if( b == 1 ) return "Submarin";
    if( b == 2 ) return "Destroyr";
    if( b == 3 ) return "Bat.Ship";
    return "Carrier";
}

// -----------------------------------------------------------------------
// Board logic - direct ports of upstream's own check_pos()/sunk()
// -----------------------------------------------------------------------

int shipCheckPos( int p, int curX, int curY )
{
    int output = 255;
    int b;
    for( b = 0; b < 5; b = b + 1 )
    {
        int bx = shipBoatPos[ p ][ b ][ 0 ];
        int by = shipBoatPos[ p ][ b ][ 1 ];
        int dir = shipBoatPos[ p ][ b ][ 2 ];
        if( ( curX >= bx && curX < bx + b + 1 && curY == by && dir == 1 ) ||
            ( curY >= by && curY < by + b + 1 && curX == bx && dir == 0 ) )
          output = b;
    }
    return output;
}

int shipSunk( int p, int b )
{
    int output = 1;
    int x = shipBoatPos[ p ][ b ][ 0 ];
    int y = shipBoatPos[ p ][ b ][ 1 ];
    int dir = shipBoatPos[ p ][ b ][ 2 ];
    int i;
    for( i = 0; i <= b; i = i + 1 )
    {
        if( dir ) { if( i > 0 ) x = x + 1; }
        else { if( i > 0 ) y = y + 1; }
        if( shipShots[ 1 - p ][ x ][ y ] != b ) output = 0;
    }
    return output;
}

// -----------------------------------------------------------------------
// Board / boat / shot drawing - direct ports of upstream's own
// draw_board()/draw_boat()/draw_boats()/draw_shots()
// -----------------------------------------------------------------------

void shipDrawBoard()
{
    gbFillScreen( 1 ); // BLACK
    gbSetColor( 0 );   // WHITE - text zone background
    gbFillRect( 49, 7, 34, 40 );
    gbCursorY = 1;
    gbDrawFastHLine( 1, 0, 46 );
    gbDrawFastVLine( 47, 1, 46 );
    gbDrawFastHLine( 1, 47, 46 );
    gbDrawFastVLine( 0, 1, 46 );
    int x, y;
    for( y = 0; y < 10; y = y + 1 )
      for( x = 0; x < 10; x = x + 1 )
        gbDrawPixel( x * 5 + 1, y * 5 + 1 );
}

void shipDrawBoat( int x, int y, int b, int dir, int c )
{
    gbSetColor( c );
    if( dir )
    {
        if( b == 4 ) gbDrawBitmap( x * 5 + 1, y * 5 + 2, shipCarrier );
        else if( b == 3 ) gbDrawBitmap( x * 5 + 1, y * 5 + 2, shipBattleShip );
        else if( b == 2 ) gbDrawBitmap( x * 5 + 1, y * 5 + 2, shipDestroyer );
        else if( b == 1 ) gbDrawBitmap( x * 5 + 1, y * 5 + 2, shipSubmarine );
        else gbDrawBitmap( x * 5 + 1, y * 5 + 2, shipCruiser );
    }
    else
    {
        if( b == 4 ) gbDrawBitmapRotated( x * 5 + 2, y * 5 + 1, shipCarrier, 3, 0 );
        else if( b == 3 ) gbDrawBitmapRotated( x * 5 + 2, y * 5 + 1, shipBattleShip, 3, 0 );
        else if( b == 2 ) gbDrawBitmapRotated( x * 5 + 2, y * 5 + 1, shipDestroyer, 3, 0 );
        else if( b == 1 ) gbDrawBitmapRotated( x * 5 + 2, y * 5 + 1, shipSubmarine, 3, 0 );
        else gbDrawBitmapRotated( x * 5 + 2, y * 5 + 1, shipCruiser, 3, 0 );
    }
}

void shipDrawBoats( int p )
{
    int b;
    for( b = 0; b < 5; b = b + 1 )
    {
        int x = shipBoatPos[ p ][ b ][ 0 ];
        int y = shipBoatPos[ p ][ b ][ 1 ];
        int dir = shipBoatPos[ p ][ b ][ 2 ];
        if( x < 255 ) shipDrawBoat( x, y, b, dir, GB_GRAY );
    }
}

void shipDrawShots( int p )
{
    if( shipDisplayEnemyShots )
    {
        shipDrawBoats( p );
        p = 1 - p;
    }
    int x, y;
    for( y = 0; y < 9; y = y + 1 )
    {
        for( x = 0; x < 9; x = x + 1 )
        {
            int shot = shipShots[ p ][ x ][ y ];
            if( shot >= 0 && shot < 5 )
            {
                if( !shipSunk( 1 - p, shot ) )
                {
                    gbSetColor( 0 ); // WHITE
                    gbFillRect( x * 5 + 2, y * 5 + 2, 4, 4 );
                }
                else
                {
                    int bx = shipBoatPos[ 1 - p ][ shot ][ 0 ];
                    int by = shipBoatPos[ 1 - p ][ shot ][ 1 ];
                    int bdir = shipBoatPos[ 1 - p ][ shot ][ 2 ];
                    if( shipDisplayEnemyShots ) shipDrawBoat( bx, by, shot, bdir, 0 ); // WHITE
                    else shipDrawBoat( bx, by, shot, bdir, GB_GRAY );
                }
            }
            else if( shot == 254 )
            {
                gbSetColor( 0 ); // WHITE
                gbFillRect( x * 5 + 3, y * 5 + 3, 2, 2 );
            }
        }
    }
}

void shipResetGame()
{
    shipGameOver = false;
    shipGameOverAnimFc = 0;

    int p, b, x, y;
    for( p = 0; p < 2; p = p + 1 )
    {
        for( b = 0; b < 5; b = b + 1 )
        {
            shipBoatPos[ p ][ b ][ 0 ] = 255;
            shipBoatPos[ p ][ b ][ 1 ] = 0;
            shipBoatPos[ p ][ b ][ 2 ] = 0;
        }
        for( x = 0; x < 9; x = x + 1 )
          for( y = 0; y < 9; y = y + 1 )
            shipShots[ p ][ x ][ y ] = 255;
        shipNbShots[ p ] = 0;
        shipLastCurX[ p ] = 4;
        shipLastCurY[ p ] = 4;
    }
}

// A direct, byte-for-byte port of upstream's own real sfx(fxno,channel).
void shipSfx( int fxno, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, shipSoundFx[ fxno ][ 6 ], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, shipSoundFx[ fxno ][ 0 ], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, shipSoundFx[ fxno ][ 5 ], -shipSoundFx[ fxno ][ 4 ], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, shipSoundFx[ fxno ][ 3 ], shipSoundFx[ fxno ][ 2 ] - 58, channel );
    gbPlayNoteChannel( shipSoundFx[ fxno ][ 1 ], shipSoundFx[ fxno ][ 7 ], channel );
}

// -----------------------------------------------------------------------
// Text helpers (see this file's own header comment on the missing
// println()-equivalent primitive)
// -----------------------------------------------------------------------

void shipNewline()
{
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

void shipPrintInZone( char* t )
{
    gbCursorX = 50;
    gbPrintString( t );
    shipNewline();
}

void shipPrintInZoneWithNumber( char* t, int n )
{
    gbCursorX = 50;
    gbPrintString( t );
    gbPrintNumber( n );
    shipNewline();
}

// -----------------------------------------------------------------------
// Animations - direct ports of upstream's own update_clouds()/
// update_boat()/update_sink_anim()/update_arrow()/update_hit_anim()/
// sunk_popup()/draw_anim_text()/update_game_over_anim()
// -----------------------------------------------------------------------

void shipUpdateClouds()
{
    shipCloudsX = shipCloudsX - 1;
    if( shipCloudsX < 1 ) shipCloudsX = 88;

    if( shipCloudsX % 2 == 0 )
    {
        shipClouds2X = shipClouds2X - 1;
        if( shipClouds2X < 1 ) shipClouds2X = 88;
    }

    gbSetColor( 1 ); // GRAY substituted BLACK - back "shadow" cloud layer
    gbDrawBitmap( shipClouds2X, 0, shipClouds2 );
    gbDrawBitmap( shipClouds2X - 88, 0, shipClouds2 );
    gbSetColor( 0 ); // WHITE mask - front cloud silhouette
    gbDrawBitmap( shipCloudsX, 0, shipCloudsw );
    gbDrawBitmap( shipCloudsX - 88, 0, shipCloudsw );
    gbSetColor( 1 ); // BLACK outline - front cloud
    gbDrawBitmap( shipCloudsX, 0, shipClouds );
    gbDrawBitmap( shipCloudsX - 88, 0, shipClouds );
}

void shipUpdateBoat()
{
    if( shipBoatAnimFc > 14 && !shipSinking )
    {
        shipBoatAnimFloatY = !shipBoatAnimFloatY;
        shipBoatAnimFc = 0;
    }

    // draws in whatever color shipUpdateClouds() last left set (BLACK) -
    // a real, deterministic upstream implicit-color-carryover quirk, see
    // this file's own header comment - preserved exactly, no extra
    // gbSetColor() added here.
    gbDrawBitmap( 10, 22 + shipBoatAnimFloatY + shipBoatAnimY, shipLogo );
    gbSetColor( GB_GRAY );
    gbFillRect( 0, 35, 84, 14 );

    shipBoatAnimFc = shipBoatAnimFc + 1;
}

void shipUpdateSinkAnim()
{
    if( shipSinking )
    {
        if( shipBoatAnimFc > 4 )
        {
            shipBoatAnimFc = 0;
            if( shipBoatAnimY < 10 ) shipBoatAnimY = shipBoatAnimY + 1;
        }
        gbSetColor( 1 ); // BLACK
        gbDrawBitmap( 10, 22 + shipBoatAnimFloatY + shipBoatAnimY, shipLogo );
        gbSetColor( GB_GRAY );
        gbFillRect( 0, 35, 84, 14 );
    }
    shipBoatAnimFc = shipBoatAnimFc + 1;
}

void shipUpdateArrow()
{
    if( shipArrowFc >= 10 )
    {
        shipArrowFc = 0;
        if( shipPressA[ 1 ] == 16 )
        {
            shipPressA[ 1 ] = 32; // ' '
            shipPressA[ 2 ] = 16;
        }
        else
        {
            shipPressA[ 1 ] = 16;
            shipPressA[ 2 ] = 32; // ' '
        }
    }
    else
      shipArrowFc = shipArrowFc + 1;

    gbSetColor( 0 ); // WHITE
    gbCursorX = 70;
    gbCursorY = 41;
    gbPrintString( shipPressA );
}

void shipUpdateHitAnim()
{
    if( shipAnimFc == 0 ) shipExplosionX = arand( 55 ) + 15;
    if( shipAnimFc < 15 )
    {
        gbSetColor( 0 ); // WHITE
        gbFillCircle( shipExplosionX, 31, shipAnimFc );
        gbSetColor( 1 ); // BLACK
        gbDrawCircle( shipExplosionX, 31, shipAnimFc );
        shipAnimFc = shipAnimFc + 1;
    }
}

void shipSunkPopup( int b )
{
    if( shipSinking )
    {
        if( !shipPopupBlocker )
        {
            shipPopupBlocker = true;
            if( b == 0 ) gbPopup( "Cruiser sunk!", 15 );
            else if( b == 1 ) gbPopup( "Submarine sunk!", 15 );
            else if( b == 2 ) gbPopup( "Destroyer sunk!", 15 );
            else if( b == 3 ) gbPopup( "Battleship sunk!", 15 );
            else if( b == 4 ) gbPopup( "Carrier sunk!", 15 );
            shipSfx( 1, 0 );
        }
    }
}

void shipDrawAnimText( int p, int steps )
{
    if( !shipGameOver )
    {
        gbSetColor( 1 ); // BLACK
        gbCursorX = 14;
        gbCursorY = 16;
        if( steps == 2 )
        {
            gbPrintString( shipPName( p ) );
            gbPrintString( "'s " );
            gbPrintString( "turn!" );
        }
        else
        {
            gbPrintString( shipPName( 1 - p ) );
            gbPrintString( "'s " );
            gbPrintString( "shot:" );
            shipNewline();
        }
    }
}

void shipUpdateGameOverAnim( int p )
{
    int gx, ox;

    if( shipGameOverAnimFc > 41 ) shipPlaying = false;

    if( shipGameOverAnimFc <= 17 )
    {
        gx = 84 - shipGameOverAnimFc * 4;
        ox = -50 + shipGameOverAnimFc * 4;
    }
    else
    {
        gx = 16;
        ox = 18;
    }

    gbSetColor( 0 ); // WHITE
    gbDrawBitmap( gx - 1, 7, shipGameOutline );
    gbDrawBitmap( ox - 1, 22, shipOverOutline );
    gbSetColor( 1 ); // BLACK
    gbDrawBitmap( gx, 8, shipGameText );
    gbDrawBitmap( ox, 23, shipOverText );

    gbSetColor( 0 ); // WHITE
    gbCursorX = 2;
    gbCursorY = 41;
    gbPrintString( shipPName( p ) );
    gbPrintString( " wins!" );

    shipGameOverAnimFc = shipGameOverAnimFc + 1;
}

// -----------------------------------------------------------------------
// Phase transitions
// -----------------------------------------------------------------------

void shipBeginPlace( int p )
{
    shipP = p;
    shipB = 4;
    shipCurX = 4;
    shipCurY = 4;
    shipBoatRot = 0;
    shipState = SHIP_STATE_PLACE;
}

void shipBeginAnim( int p )
{
    shipP = p;
    shipPopupBlocker = false;
    shipSinking = false;
    shipBoatAnimY = 0;
    shipBoatAnimFc = 0;
    shipAnimFc = 0;
    if( shipNbShots[ 1 - p ] == 0 ) shipAnimSteps = 2;
    else shipAnimSteps = 0;
    shipState = SHIP_STATE_ANIM;
}

void shipBeginShoot( int p )
{
    shipP = p;
    shipCurX = shipLastCurX[ p ];
    shipCurY = shipLastCurY[ p ];
    shipState = SHIP_STATE_SHOOT;
}

void shipPauseToTitle()
{
    shipReturnState = shipState;
    shipState = SHIP_STATE_TITLE;
    gbPlayCancel();
}

// -----------------------------------------------------------------------
// Phase updates
// -----------------------------------------------------------------------

// Upstream's own real `gb.titleScreen(F("Shipwreck"), logo)` is a built-in
// blocking widget with no equivalent here - hand-built following this
// project's own established title-screen pattern (see gamePong.c's own
// header comment), reusing the same real `logo` bitmap upstream itself
// reuses for both the splash AND the in-game floating-boat animation.
void shipUpdateTitle()
{
    gbSetColor( 1 ); // BLACK
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, shipLogo );

    gbCursorX = 24;
    gbCursorY = 34;
    gbPrintString( "SHIPWRECK" );
    gbCursorX = 28;
    gbCursorY = 41;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        if( shipReturnState == -1 )
        {
            shipResetGame();
            shipBeginPlace( 0 );
        }
        else
          shipState = shipReturnState;
    }
}

void shipUpdatePlace()
{
    shipDrawBoard();
    shipDrawBoats( shipP );

    gbSetColor( 0 ); // WHITE - header name line (drawn on the still-black strip above the white zone, see draw_board()'s own cursorY=1)
    shipPrintInZone( shipPName( shipP ) );
    gbSetColor( 1 ); // BLACK
    shipPrintInZone( "Place" );
    shipPrintInZone( "your" );
    shipPrintInZone( shipBoatNameFor( shipB ) );
    shipNewline();
    shipPrintInZone( "A: Place" );
    shipPrintInZone( "B: Rotat" );

    if( gbPressed( BTN_B ) )
    {
        shipBoatRot = 1 - shipBoatRot;
        if( shipCurX + shipB > 8 && shipBoatRot == 1 ) shipCurX = 8 - shipB;
        if( shipCurY + shipB > 8 && shipBoatRot == 0 ) shipCurY = 8 - shipB;
    }

    if( gbPressed( BTN_A ) )
    {
        int possible = 1;
        int i;
        for( i = 0; i < shipB + 1; i = i + 1 )
        {
            int xOff = 0;
            int yOff = 0;
            if( shipBoatRot ) xOff = i;
            else yOff = i;
            if( shipCheckPos( shipP, shipCurX + xOff, shipCurY + yOff ) < 255 )
            {
                possible = 0;
                break;
            }
        }
        if( possible )
        {
            shipBoatPos[ shipP ][ shipB ][ 0 ] = shipCurX;
            shipBoatPos[ shipP ][ shipB ][ 1 ] = shipCurY;
            shipBoatPos[ shipP ][ shipB ][ 2 ] = shipBoatRot;
            gbPlayOK();
            if( shipB == 0 )
            {
                if( shipP == 0 )
                  shipBeginPlace( 1 );
                else
                {
                    shipPlaying = true;
                    shipBeginAnim( 0 );
                }
                return;
            }
            shipB = shipB - 1;
        }
        else
        {
            gbPlayCancel();
            gbPopup( "Can't overlap boats", 20 );
        }
    }

    if( gbPressed( BTN_UP ) ) { if( shipCurY > 0 ) shipCurY = shipCurY - 1; else gbPlayCancel(); }
    if( gbPressed( BTN_DOWN ) ) { if( shipCurY < 8 - shipB * ( 1 - shipBoatRot ) ) shipCurY = shipCurY + 1; else gbPlayCancel(); }
    if( gbPressed( BTN_LEFT ) ) { if( shipCurX > 0 ) shipCurX = shipCurX - 1; else gbPlayCancel(); }
    if( gbPressed( BTN_RIGHT ) ) { if( shipCurX < 8 - shipB * shipBoatRot ) shipCurX = shipCurX + 1; else gbPlayCancel(); }

    if( gbPressed( BTN_C ) ) { shipPauseToTitle(); return; }

    gbSetColor( 0 ); // WHITE
    int k;
    for( k = 0; k <= shipB; k = k + 1 )
    {
        if( shipBoatRot ) gbDrawRect( ( shipCurX + k ) * 5 + 3, shipCurY * 5 + 3, 2, 2 );
        else gbDrawRect( shipCurX * 5 + 3, ( shipCurY + k ) * 5 + 3, 2, 2 );
    }
}

void shipUpdateAnimPhase()
{
    int p = shipP;

    shipUpdateClouds();
    shipUpdateBoat();

    if( shipAnimSteps == 1 )
    {
        if( shipNbShots[ 1 - p ] >= 1 )
        {
            int checkShot = shipCheckPos( p, shipLastCurX[ 1 - p ], shipLastCurY[ 1 - p ] );
            if( checkShot < 255 )
            {
                if( shipSunk( p, checkShot ) )
                {
                    shipSinking = true;
                    shipSunkPopup( checkShot );
                    shipUpdateHitAnim();

                    shipGameOver = true;
                    int i;
                    for( i = 0; i < 5; i = i + 1 )
                      if( !shipSunk( p, i ) ) shipGameOver = false;
                    if( shipGameOver ) shipPlaying = false;
                }
                else
                {
                    shipUpdateHitAnim();
                    if( !shipPopupBlocker )
                    {
                        shipSfx( 1, 0 );
                        gbPopup( "HIT!!!", 10 );
                        shipPopupBlocker = true;
                    }
                }
            }
            else
            {
                if( !shipPopupBlocker )
                {
                    gbPlayCancel();
                    gbPopup( "Miss...", 10 );
                    shipPopupBlocker = true;
                }
            }
        }
    }

    shipDrawAnimText( p, shipAnimSteps );
    shipUpdateSinkAnim();
    shipUpdateArrow();
    if( shipGameOver ) shipUpdateGameOverAnim( 1 - p );

    if( gbPressed( BTN_A ) )
    {
        if( shipAnimSteps >= 2 )
        {
            if( shipGameOver && !shipPlaying )
            {
                shipResetGame();
                shipBeginPlace( 0 );
            }
            else if( shipGameOver )
              shipBeginAnim( 1 - p );
            else
              shipBeginShoot( p );
        }
        else
        {
            shipAnimSteps = shipAnimSteps + 1;
            shipPopupBlocker = false;
        }
    }
    if( gbPressed( BTN_C ) ) shipPauseToTitle();
}

void shipUpdateShoot()
{
    shipDrawBoard();
    shipDrawShots( shipP );

    if( gbPressed( BTN_UP ) ) { if( shipCurY > 0 ) shipCurY = shipCurY - 1; else gbPlayCancel(); }
    if( gbPressed( BTN_DOWN ) ) { if( shipCurY < 8 ) shipCurY = shipCurY + 1; else gbPlayCancel(); }
    if( gbPressed( BTN_LEFT ) ) { if( shipCurX > 0 ) shipCurX = shipCurX - 1; else gbPlayCancel(); }
    if( gbPressed( BTN_RIGHT ) ) { if( shipCurX < 8 ) shipCurX = shipCurX + 1; else gbPlayCancel(); }

    if( gbPressed( BTN_A ) && !shipDisplayEnemyShots )
    {
        if( shipShots[ shipP ][ shipCurX ][ shipCurY ] != 255 )
        {
            gbPlayCancel();
            gbPopup( "Already shot there!", 20 );
        }
        else
        {
            shipSfx( 0, 0 ); // launch sound - upstream's own commented-out gb.sound.playTick() right after this stays uncalled, matching real shipped behavior
            shipNbShots[ shipP ] = shipNbShots[ shipP ] + 1;
            int target = shipCheckPos( 1 - shipP, shipCurX, shipCurY );
            if( target < 255 ) shipShots[ shipP ][ shipCurX ][ shipCurY ] = target;
            else shipShots[ shipP ][ shipCurX ][ shipCurY ] = 254;
            shipLastCurX[ shipP ] = shipCurX;
            shipLastCurY[ shipP ] = shipCurY;
            shipBeginAnim( 1 - shipP );
            return;
        }
    }

    if( gbPressed( BTN_B ) ) shipDisplayEnemyShots = true;
    if( gbReleased( BTN_B ) ) shipDisplayEnemyShots = false;

    if( gbPressed( BTN_C ) ) { shipPauseToTitle(); return; }

    // draw aim (must be drawn before the text zone - see upstream's own comment)
    if( !shipDisplayEnemyShots )
    {
        gbSetColor( GB_GRAY );
        gbDrawFastVLine( shipCurX * 5 + 3, shipCurY * 5 + 3 - 6, 13 );
        gbDrawFastHLine( shipCurX * 5 + 3 - 6, shipCurY * 5 + 3, 13 );
        gbSetColor( 0 ); // WHITE
        gbDrawCircle( shipCurX * 5 + 3, shipCurY * 5 + 3, 7 );
        gbDrawRect( shipCurX * 5 + 3, shipCurY * 5 + 3, 2, 2 );
    }

    // text (must be drawn after aim)
    gbSetColor( 1 ); // BLACK
    gbFillRect( 49, 0, 34, 7 );
    gbSetColor( 0 ); // WHITE
    gbFillRect( 49, 8, 34, 40 );
    shipPrintInZone( shipPName( shipP ) );
    gbCursorY = gbCursorY + 6;
    gbSetColor( 1 ); // BLACK
    shipPrintInZoneWithNumber( "Shot ", shipNbShots[ shipP ] + 1 );
    gbCursorY = gbCursorY + 6;
    if( !shipDisplayEnemyShots ) shipPrintInZone( "A: Shoot" );
    else shipNewline();
    shipPrintInZone( "B: Toggl" );
}

// -----------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------

void gameShipwrek_init()
{
    gbBegin();

    shipCloudsX = 88;
    shipClouds2X = 88;
    shipDisplayEnemyShots = false;
    shipArrowFc = 0;
    shipPressA[ 0 ] = 21;
    shipPressA[ 1 ] = 16;
    shipPressA[ 2 ] = 32; // ' '
    shipPressA[ 3 ] = 0;

    shipReturnState = -1;
    shipState = SHIP_STATE_TITLE;

    gbPickRandomSeed(); // no-op, see gamebuinoShim.h's own header comment
}

void gameShipwrek_update()
{
    if( !gbUpdate() ) return;

    if( shipState == SHIP_STATE_TITLE ) shipUpdateTitle();
    else if( shipState == SHIP_STATE_PLACE ) shipUpdatePlace();
    else if( shipState == SHIP_STATE_ANIM ) shipUpdateAnimPhase();
    else shipUpdateShoot();

    // real gb.popup()'s own overlay is drawn automatically by
    // gbRenderFrame() below - see header comment.
    gbRenderFrame();
}
