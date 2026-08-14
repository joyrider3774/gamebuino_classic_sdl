#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Tron (Clement83, license unspecified upstream - same author as this
// cartridge's own already-shipped Bomber/Copter/GlaciGlaca/CrazyTown,
// confirmed directly via the staged copy's own .git/config remote URL,
// github.com/Clement83/Tron). A real light-cycle (Tron-style) trail game:
// two riders leave a permanent wall behind them as they move, and crash
// the instant they hit a wall, their own trail, or the opponent's trail.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameTron.c (which already did the real upstream-Arduino-to-
// Vircon32-dialect porting work - see that file's own much longer header
// comment, copied below verbatim, for the full original write-up of the
// multiplayer removal, the AI/persistence/bitmap work, etc, none of which
// needed revisiting here) - converting Vircon32's own C dialect back to
// standard C:
//  - `struct TronSnake { ... };` -> `typedef struct { ... } TronSnake;`,
//    so `TronSnake* play`-style pointer parameters and plain
//    `TronSnake tronP1;` declarations both work without a `struct`
//    keyword at every use site, exactly as this file's own function
//    signatures already assumed.
//  - Every `TYPE[N] name` / `TYPE[N] name = {...}` array declaration
//    (`tronMotoBitmap`, `tronLooserBitmap`, `tronWinnerBitmap`,
//    `tronAllDir`, `tronTrail`) -> `TYPE name[N]` (array-declaration
//    order) - plain standard-C array syntax.
// No text-string array conversions were needed - every string in this
// file (menu labels, "PRESS A", etc) was already a plain C string
// literal, not an int[]-encoded one.
//
// The two-separate-lines `TronSnake tronP1;` / `TronSnake tronP2;`
// declaration (rather than the more natural `TronSnake tronP1, tronP2;`)
// is kept exactly as the original file has it, comment included below -
// that comma-separated-struct-global memory-corruption bug was a real,
// empirically-confirmed defect specific to the Vircon32 dialect/compiler
// itself, not something standard C has any equivalent hazard for (a
// normal C compiler has no such bug with `Foo a, b;` for struct types).
// Carried over unchanged purely as faithful historical documentation, not
// because standard C needs the same workaround.
//
// UPSTREAM IS 4 REAL FILES - all read in full before writing the original
// port. `Tron.ino` holds the real `Snake` struct, the real menu/state
// machine (`gameMode`: gameMenu/game/gameOver), and the real difficulty
// constants (`FACILE`=7/`MOYEN`=15/`DIFFICILE`=25 - these are real
// frame-rate values, not scores: a higher rate means more logic ticks per
// second at the same fixed 2px-per-tick step, i.e. genuinely faster, so
// DIFFICILE really is the hardest). `player.ino` holds `drwPlayer()`/
// `updPlayer()`/`human()`/`ia()`/`movePlayerSlave()`/`updatePlayer()`/
// `drawPlayer()`. `master.ino`/`slave.ino` hold ONLY the real two-
// cartridge `Wire.h` I2C protocol (`setupMaster`/`updateMaster`/
// `masterRead`/`masterWrite`, `setupSlave`/`updateSlave`/`requestEvent`/
// `receiveEvent`) - read in full to confirm neither file defines any
// shared helper the kept single-player path also calls; both are pure
// network glue with no such thing, so both are dropped in their entirety,
// per this task's own scope.
//
// MULTIPLAYER DROPPED ENTIRELY, PER THIS PROJECT'S OWN ESTABLISHED
// "drop the real hardware/mode-specific option, keep the real
// hardware-independent one" precedent (see gameBRally.c's own accelerometer
// fallback and gameSenet.c's own I2C-mode removal for two earlier examples
// of the exact same treatment in this cartridge). Real upstream's own
// `multiPlayerMenu` (`gb.menu(multiPlayerMenu,3)`, entries "1 player"/
// "Host (master)"/"Join (slave)") is gone along with the Host/Join
// branches: `setupMaster()`/`setupSlave()`/`updateMaster()`/`updateSlave()`/
// `masterRead()`/`masterWrite()`/`requestEvent()`/`receiveEvent()`/
// `movePlayerSlave()` are not ported - none of them has any real caller
// left once the multiplayer menu entries are gone. `isMaster`/`isOnePlayer`/
// `isPaused`/`disconnected`/the `bt_*` fake-slave-button globals are all
// dropped too rather than kept as permanently-unreachable dead branches -
// `p2` (a real `Snake` struct, exactly like `p1`) simply becomes
// AI-only, matching how upstream's own single-player mode already uses it
// (`isOnePlayer==true` always calls `ia(&p2)`, never a network path).
//
// SINCE ONLY ONE REAL CHOICE SURVIVES ONCE HOST/JOIN ARE GONE, THE
// MULTIPLAYER PICKER SCREEN ITSELF IS SKIPPED - a deliberate choice (this
// task's own instructions leave it to per-game judgement), matching every
// other single-player-only game already shipped in this cartridge (Pong
// Solo's own one-A-press title dismissal). Concretely: real upstream flow
// is `titleScreen(moto) -> gb.menu(multiPlayerMenu,3) [pick "1 player"] ->
// initDifficulty() [gb.menu(difficultyMenu,3)] -> initGame() -> game`. This
// port's flow is `TRON_STATE_TITLE -> TRON_STATE_DIFFICULTY ->
// TRON_STATE_GAME`, i.e. the picker step alone is removed and the real
// difficulty menu (still a genuine, still-reachable real choice) is kept
// exactly - real upstream's own `initDifficulty()` is called
// unconditionally from the "1 player" branch regardless of mode, so
// nothing about it depended on the picker still existing.
//
// THE REAL AI (`ia()`, `player.ino`) IS PORTED COMPLETELY AND UNMODIFIED -
// no simplification, no invented behavior. It is a genuine, if simple,
// random-walk collision-avoidance routine: a small (1-in-40 per tick)
// unprompted chance to pick a fresh random direction, then up to 10 real
// retries (`forceSortie`) picking a new random direction any time the
// pixel directly ahead is already occupied, checked via the exact same
// `getPixel()`-against-the-drawn-framebuffer mechanic the human player's
// own real collision detection (`updPlayer()`) is built on - this is not
// an approximation of the real AI, `tronIa()` below is a direct,
// line-for-line port of upstream's own real `ia()` body.
//
// A REAL SHIM GAP THIS PORT HAD TO WORK AROUND LOCALLY, NOT FIXED IN THE
// SHARED SHIM: this game's entire mechanic depends on real
// `display.persistence = true` (set once in upstream's own real
// `initGame()`) - the drawn trail IS the collision surface, checked
// directly via `getPixel()`, and it must never be erased except by a
// fresh `initGame()` call. This shim's own `gbUpdate()` unconditionally
// clears the framebuffer every single tick (matching every other game
// shipped in this cartridge, all of which redraw their whole scene from
// scratch every tick and never needed real persistence - see
// gamebuinoShim.c's own `gbUpdate()`), so a literal port would erase the
// entire trail one tick after it was drawn, breaking the whole game.
// Fixed LOCALLY (this game is the first, and so far only, one in this
// cartridge whose core mechanic needs true persistence, so this is a
// one-off per-game workaround, not something to promote into the shared
// shim): `tronTrail[]` is a local, plain-int bitplane addressed
// exactly like the shim's own internal `gbFrameBuffer[]`
// (`x + (y/8)*LCDWIDTH`, bit `y%8`) that this file maintains itself,
// completely untouched by `gbClear()`. Every `TRON_STATE_GAME` tick
// redraws the real border walls fresh (`tronDrawWalls()` - cheap, 8 fast
// line calls) and re-blits every previously-marked trail pixel from
// `tronTrail[]` back onto the real framebuffer (`tronRenderTrail()`)
// before drawing this tick's own new rider positions - visually and
// functionally identical to real hardware's own persistent framebuffer,
// just rebuilt every tick instead of genuinely never cleared.
// `tronBlocked()` (the real collision predicate every `getPixel()==BLACK`/
// `==WHITE` check in upstream becomes) reads directly from this same
// `tronTrail[]` plus a plain arithmetic border-geometry check, so it is
// exactly as accurate as a real `getPixel()` read against upstream's own
// persistent buffer would have been.
//
// A REAL BY-VALUE-STRUCT REWRITE (VIRCON32_C_DIALECT.md section 4/15.2,
// applicable to the original Vircon32 port this one was itself ported
// from): upstream's own `drwPlayer(Snake play)` takes the whole struct BY
// VALUE (4 real int fields = 4 words, over that dialect's real 1-word
// function boundary limit) - `play.dir`/`play.life` are never actually
// read inside its body, only `play.x`/`play.y`, so `tronDrawPlayerAt(int
// x, int y)` below takes just the two scalars that are actually used
// rather than a pointer-to-struct out-pointer dance that would be pure
// ceremony here. Kept as-is in this standard-C port too, even though
// standard C would have allowed passing the struct by value directly -
// no reason to diverge from an already-correct, already-verified shape.
//
// A REAL, PROVABLY-EQUIVALENT SIMPLIFICATION: upstream's own real
// `drwPlayer()` draws its 2x2 rider block with a nested `drawPixel()`
// loop, with a `//gb.display.fillRect(play.x,play.y,VITTESSE_SNAKE,
// VITTESSE_SNAKE);` line commented out directly above it - upstream's own
// author already confirmed the two are equivalent for this exact shape,
// this port just uses the equivalent `gbFillRect()` call directly (one
// call instead of four `gbDrawPixel()` calls), not a behavior change.
//
// A REAL UPSTREAM QUIRK PRESERVED EXACTLY, NOT NORMALIZED: pressing Button
// C shows the title screen (`goTitleScreen()`) from ANYWHERE in real
// upstream's own outer `loop()`, checked unconditionally before the state
// switch - but because `gb.menu()` (the difficulty picker) is itself a
// nested BLOCKING call, that outer C-check can only ever actually be
// reached in real hardware while control is in the non-blocking `game`/
// `gameOver` states, never while a real blocking menu is up. This port's
// own `TRON_STATE_DIFFICULTY` is a genuine per-tick (non-blocking) state
// rather than a blocking call, so it WOULD wrongly start responding to
// Button C if the check were applied unconditionally - `gameTron_update()`
// below only checks Button C during `TRON_STATE_GAME`/`TRON_STATE_GAMEOVER`,
// matching real hardware's own actual reachable behavior exactly rather
// than the wider reachability a non-blocking rewrite would otherwise
// accidentally introduce. `tronReturnState` records which of those two
// states to resume once the interrupting title screen is dismissed,
// mirroring upstream's own real `state` variable staying completely
// untouched by `goTitleScreen()` itself.
//
// GAMEOVER'S OWN "BACK TO MENU" (Button B) TARGET, ADAPTED: real upstream
// sets `state = gameMenu` on Button B from the gameOver screen, which
// (with the picker still in place) would re-show the mode picker then the
// difficulty picker. With the picker gone, this port sends Button B
// straight to `TRON_STATE_DIFFICULTY` instead - the next real, still-
// meaningful interactive step on the surviving single-player path, and
// exactly where real upstream's own `state = gameMenu` would have ended
// up landing anyway once "1 player" is the only choice. Button A instead
// restarts immediately at the SAME difficulty (`tronInitGame()` + the
// current `tronDifficulter`'s own frame rate), a direct, unmodified port
// of upstream's own real `case gameOver: ... if(pressed(BTN_A)) {
// initGame(); gb.setFrameRate(difficulter); state=game; }`.
//
// Real bitmaps (`moto`/`looser`/`winner`, `player.ino`'s own top-level
// `const byte ... PROGMEM` tables) are copied byte-for-byte - already in
// this shim's own `gbDrawBitmap()` format (width, height, then packed
// row-major MSB-first bytes), confirmed directly against each array's own
// declared width/height before copying. `moto` (80x44) is drawn at the
// real `Gamebuino::titleScreen()` anchor `(0,12)` (confirmed by reading
// the real `Gamebuino.cpp` directly - matching this cartridge's own
// already-established "PRESS A" title-screen convention used by every
// other ported game, in place of the generic Gamebuino splash
// logo/icon-row real `titleScreen()` also draws, which is generic library
// chrome unrelated to this specific game and is dropped the same way
// every other port in this cartridge already drops it) - real hardware
// clips its bottom 8 rows off-screen at that anchor (12+44=56 > the real
// 48px LCD height), a genuine preserved upstream quirk, not a mistake
// introduced here. `looser`/`winner` (88x48, 4px wider than the real
// 84px LCD) are drawn at `(0,0)` exactly like real upstream's own
// `gb.display.drawBitmap(0,0, looser/winner)` and are likewise clipped on
// their real right edge - preserved the same way.
//
// `random(0,40)`/`random(0,4)` -> `arand(40)`/`arand(4)` (this project's
// own established RNG helper, avrCompat.h). `byte`/`uint8_t`/`int8_t`
// fields become plain `int` (word-sized here regardless). The real `Dir`
// enum (`up`/`down`/`left`/`right`) becomes `TRON_DIR_*` `#define`
// constants rather than an enum-typed struct field, matching this
// cartridge's own established convention for direction fields elsewhere
// (e.g. gameSnakeClassic.c's own `SNKC_DIR_*`). `gb.pickRandomSeed()` ->
// `gbPickRandomSeed()`, a documented no-op (kept as a call site for
// fidelity, matching this project's own established precedent for every
// other upstream `randomSeed()`-style call). `gb.battery.show = false` is
// dropped outright (purely cosmetic on real hardware, no equivalent
// system here).

int tronMotoBitmap[442] =
{
80, 44, 0x0, 0x0, 0x7, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xFF,
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
0x0, 0x0, 0x1F, 0xFF, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1F, 0xFF, 0xF0, 0x0,
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7F, 0xFF, 0xFE, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
0x7F, 0xFF, 0xFE, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0xFF, 0xF1, 0xE0, 0x0, 0x0,
0x0, 0x0, 0x0, 0x0, 0x1, 0xFF, 0xF1, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xFF,
0xF, 0xFF, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xFF, 0xF, 0xFF, 0xC0, 0x0, 0x0, 0x0,
0x0, 0x0, 0x1F, 0xFC, 0x3F, 0xE1, 0xFC, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1F, 0xFC, 0x3F, 0xE1,
0xFC, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1F, 0xF0, 0xFF, 0xFE, 0x3F, 0xFF, 0xE0, 0x0, 0x0, 0x0,
0x1F, 0xF0, 0xFF, 0xFE, 0x3F, 0xFF, 0xE0, 0x0, 0x0, 0x0, 0x1F, 0xF0, 0xFF, 0xFE, 0x3F, 0xFF,
0xE0, 0x0, 0x0, 0xFF, 0x80, 0x0, 0xFC, 0x7F, 0x8F, 0x3F, 0xF8, 0x0, 0x0, 0xFF, 0x80, 0x0,
0xFC, 0x7F, 0x8F, 0x3F, 0xF8, 0x0, 0x7, 0xFF, 0xE0, 0xF, 0xF0, 0x7, 0x8C, 0xFF, 0xFF, 0x0,
0x7, 0xFF, 0xE0, 0xF, 0xF0, 0x7, 0x8C, 0xFF, 0xFF, 0x0, 0x1F, 0xFF, 0xF8, 0x7F, 0xC3, 0xE7,
0x83, 0xFF, 0xFF, 0xC0, 0x1F, 0xFF, 0xF8, 0x7F, 0xC3, 0xE7, 0x83, 0xFF, 0xFF, 0xC0, 0x7F, 0xC0,
0xE7, 0xFC, 0xF, 0xE7, 0x8F, 0xF0, 0x7, 0xF0, 0x7F, 0xC0, 0xE7, 0xFC, 0xF, 0xE7, 0x8F, 0xF0,
0x7, 0xF0, 0xFE, 0x0, 0x0, 0x0, 0xFF, 0xE7, 0x80, 0x0, 0x7, 0xF0, 0xFE, 0x0, 0x0, 0x0,
0xFF, 0xE7, 0x80, 0x0, 0x7, 0xF0, 0xFE, 0x0, 0x0, 0x0, 0xFF, 0xE7, 0x80, 0x0, 0x7, 0xF0,
0xF8, 0x0, 0x7, 0x8F, 0xFF, 0xE7, 0xFF, 0xC0, 0x0, 0xF0, 0xF8, 0x0, 0x7, 0x9F, 0xFF, 0xE7,
0xFF, 0xC0, 0x0, 0xF0, 0xF8, 0x0, 0x7, 0x9F, 0xFF, 0xF9, 0xFF, 0xC0, 0x0, 0xF0, 0xF8, 0x0,
0x7, 0x9F, 0xFF, 0xF9, 0xFF, 0xC0, 0x0, 0xF0, 0xF8, 0x0, 0x7, 0x9F, 0xFF, 0xFE, 0x3, 0xC0,
0x1, 0xF0, 0xF8, 0x0, 0x7, 0x9F, 0xFF, 0xFE, 0x3, 0xC0, 0x1, 0xF0, 0xFE, 0x0, 0x3F, 0x9F,
0xFF, 0xFF, 0xF3, 0x0, 0x7, 0xF0, 0xFE, 0x0, 0x3F, 0x9F, 0xFF, 0xFF, 0xF3, 0x0, 0x7, 0xF0,
0x7F, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x30, 0x7, 0xF0, 0x7F, 0x80, 0xFF, 0xFF, 0xFF, 0xFF,
0xF0, 0x30, 0x7, 0xF0, 0x7F, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x30, 0x7, 0xF0, 0x1F, 0xFF,
0xFF, 0xFF, 0x0, 0x0, 0x3, 0xFF, 0xFF, 0xC0, 0x1F, 0xFF, 0xFF, 0xFF, 0x0, 0x0, 0x3, 0xFF,
0xFF, 0xC0, 0x7, 0xFF, 0xF0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xFE, 0x0, 0x7, 0xFF, 0xF0, 0x0,
0x0, 0x0, 0x0, 0xFF, 0xFE, 0x0, 0x1, 0xFF, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x3F, 0xF8, 0x0,
0x1, 0xFF, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x3F, 0xF8, 0x0,
};

int tronLooserBitmap[530] =
{
88, 48, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0xF0, 0xF0, 0x1F, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0xF0, 0xE0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0x0, 0x70, 0xE2, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0, 0x70, 0xE2, 0x1F,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x8, 0x70, 0xE0, 0x1F, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF,
0xFF, 0xFF, 0x8, 0x70, 0xF0, 0x1F, 0xFF, 0xFF, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0x0, 0x70, 0xEE,
0x1F, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x0, 0x70, 0xE0, 0x1F, 0xFD, 0xFF, 0xFE, 0xFF,
0xFF, 0xFF, 0xFF, 0x80, 0xF0, 0xE0, 0x3F, 0xFC, 0x7F, 0xFF, 0x7F, 0xFF, 0xFE, 0xFF, 0xE3, 0xF0,
0xF0, 0x7F, 0xFF, 0x3F, 0xFF, 0x7F, 0xFF, 0xFC, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF,
0x7F, 0xFF, 0xFD, 0xFF, 0xFF, 0xF0, 0xF0, 0x7F, 0xFF, 0xDF, 0xFF, 0x7F, 0xFF, 0xFB, 0xFF, 0xC,
0x70, 0xE0, 0x3F, 0xFF, 0xE3, 0xFD, 0xFF, 0xBF, 0xF7, 0xFF, 0x88, 0x70, 0xE0, 0x3F, 0xFF, 0xFB,
0xFC, 0xBF, 0x37, 0xEF, 0xFF, 0x80, 0xF0, 0xFC, 0x3F, 0xFF, 0xF9, 0xFE, 0xBF, 0x37, 0xEF, 0xFF,
0x80, 0xF0, 0xF0, 0x3F, 0xFF, 0xFE, 0xFA, 0x7F, 0x27, 0xDF, 0xFF, 0xC0, 0xF0, 0xE6, 0x3F, 0xFF,
0xFE, 0x78, 0x7F, 0xF, 0x1F, 0xFF, 0xC1, 0xF0, 0xE0, 0x3F, 0xFF, 0xFF, 0xBC, 0x2E, 0xD, 0xFF,
0xFF, 0xE1, 0xF0, 0xF0, 0x3F, 0xFF, 0xFF, 0xCC, 0x2E, 0x8, 0xFF, 0xFF, 0xE3, 0xF0, 0xFF, 0xFF,
0xFF, 0xFF, 0xF4, 0x6, 0x11, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xDF, 0xF8, 0xC5, 0x7,
0xFF, 0xFF, 0xFF, 0xF0, 0xC2, 0x71, 0xFF, 0xE1, 0xBE, 0xE9, 0x8, 0x70, 0xFF, 0xE3, 0xF0, 0xC0,
0x1, 0xFF, 0xF0, 0x0, 0xE9, 0x0, 0x0, 0xFF, 0x80, 0xF0, 0xC2, 0x10, 0xFF, 0xF8, 0x3, 0xFB,
0xC0, 0x3, 0xFF, 0x8C, 0xF0, 0xC2, 0x10, 0xFF, 0xFE, 0xC, 0xFF, 0xFE, 0x1F, 0xFF, 0x80, 0xF0,
0xC2, 0x10, 0xFF, 0xFF, 0x87, 0xFF, 0xF8, 0x1F, 0xFF, 0x87, 0xF0, 0xC2, 0x10, 0xFF, 0xFF, 0xE1,
0x3F, 0xF0, 0xFF, 0xFF, 0x83, 0xF0, 0xC2, 0x10, 0xFF, 0xFF, 0xF1, 0xFF, 0xF1, 0xFF, 0xFF, 0xC0,
0xF0, 0xC2, 0x10, 0xFF, 0xFF, 0xC0, 0x3F, 0xE0, 0x7F, 0xFF, 0xE1, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF,
0x84, 0x9F, 0xE0, 0x3F, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x37, 0xE2, 0x1F, 0xFF,
0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xF1, 0xB7, 0xE1, 0xFF, 0xFF, 0xC2, 0x70, 0xF8, 0xFF, 0xFF,
0xFF, 0xA7, 0x64, 0xC2, 0x7F, 0xFF, 0xC0, 0x30, 0xE0, 0x3F, 0xFF, 0xFF, 0xF5, 0x40, 0xE, 0x3F,
0xFF, 0xC0, 0x30, 0xE3, 0x3F, 0xFF, 0xFF, 0xF7, 0x0, 0x7, 0xFF, 0xFF, 0xC2, 0x70, 0xE0, 0x3F,
0xFF, 0xFF, 0xC6, 0x1, 0xC, 0xFF, 0xFF, 0xC3, 0xF0, 0xE1, 0xFF, 0xFF, 0xFF, 0xCE, 0x1, 0xE,
0xFF, 0xFF, 0xC3, 0xF0, 0xE0, 0xFF, 0xFF, 0xFF, 0x38, 0x42, 0xA7, 0x3F, 0xFF, 0xC3, 0xF0, 0xF0,
0x3F, 0xFF, 0xFE, 0x7C, 0x5B, 0x87, 0xCF, 0xFF, 0xC3, 0xF0, 0xF8, 0x7F, 0xFF, 0xFD, 0xF8, 0xDF,
0xF7, 0xF3, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x8F, 0xF7, 0xF3, 0xFF, 0xFF, 0xF0,
0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xEF, 0xFB, 0xFC, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xEF, 0xFF, 0xFE, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF,
0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xDF, 0xFF, 0xFF, 0xFF,
0xFF, 0xF0,
};

int tronWinnerBitmap[530] =
{
88, 48, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xF8, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF,
0xFC, 0x77, 0x1F, 0xC6, 0x78, 0xCF, 0x87, 0x8D, 0xFF, 0xF0, 0xFF, 0xFE, 0x22, 0x30, 0xC0, 0x38,
0x7, 0x3, 0x80, 0xFF, 0xF0, 0xFF, 0xFE, 0x0, 0x30, 0xC6, 0x38, 0xC6, 0x33, 0x80, 0xFF, 0xF0,
0xFF, 0xFF, 0x0, 0x30, 0xC6, 0x38, 0xC6, 0x3, 0x8C, 0xFF, 0xF0, 0xFF, 0xFF, 0x0, 0x70, 0xC6,
0x38, 0xC6, 0x1F, 0x8F, 0xFF, 0xF0, 0xFF, 0xFF, 0x0, 0x70, 0xC6, 0x38, 0xC6, 0xF, 0x8F, 0xFF,
0xF0, 0xFF, 0xFF, 0x8, 0xF0, 0xC6, 0x38, 0xC7, 0x3, 0x8F, 0xFF, 0xF0, 0xFF, 0xFF, 0x9C, 0xF0,
0xC6, 0x38, 0xC7, 0x87, 0x8F, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC7, 0xFF, 0xF0,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBB, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xB5, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD9, 0xFF,
0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC2, 0xCF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFE, 0x3F, 0x3F, 0xFF, 0xF0, 0xFF, 0xFF, 0x80, 0x0, 0x0, 0x3F, 0xF9, 0xFF, 0xA7,
0xFF, 0xF0, 0xFF, 0xFE, 0x0, 0x0, 0x0, 0x7F, 0xF7, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xF0, 0x0,
0x0, 0x0, 0x7F, 0xF7, 0xCF, 0xDF, 0xFF, 0xF0, 0xFF, 0x80, 0x0, 0x0, 0x0, 0xC3, 0xF9, 0xDF,
0xEF, 0xE1, 0xF0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x9C, 0xFF, 0xC0, 0xF7, 0xCE, 0x70, 0x0, 0x0,
0x0, 0x0, 0x3, 0x7F, 0x70, 0xFF, 0x99, 0xBF, 0xB0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x7F, 0x2F,
0xFF, 0xEE, 0x3F, 0x90, 0x0, 0x0, 0x0, 0x0, 0x2, 0xFF, 0xEF, 0xFA, 0x37, 0x9F, 0xD0, 0x0,
0x0, 0x0, 0x0, 0x2, 0xFE, 0xF, 0xDB, 0x57, 0x3F, 0xD0, 0x0, 0x0, 0x0, 0x0, 0x2, 0xFD,
0x90, 0x2B, 0x5F, 0x7F, 0xD0, 0x0, 0x0, 0x0, 0x0, 0x2, 0xFF, 0xBF, 0xDF, 0xFB, 0x7F, 0xD0,
0x0, 0x0, 0x0, 0x0, 0x2, 0xFF, 0xBF, 0xFF, 0xFB, 0x7F, 0x90, 0x0, 0x0, 0x0, 0x0, 0x3,
0x7F, 0x7F, 0xFF, 0xFB, 0xBF, 0xB0, 0xFF, 0xC0, 0x0, 0x0, 0x1, 0x3C, 0xFF, 0xFF, 0xFB, 0x9E,
0x70, 0xFF, 0xF8, 0x0, 0x0, 0x0, 0xC1, 0xFF, 0xFF, 0xFF, 0xE0, 0xF0, 0xFF, 0xFF, 0x0, 0x0,
0x0, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xF0,
};

#define TRON_STEP 2 // real VITTESSE_SNAKE

#define TRON_DIR_UP    0
#define TRON_DIR_DOWN  1
#define TRON_DIR_LEFT  2
#define TRON_DIR_RIGHT 3

int tronAllDir[4] = { TRON_DIR_UP, TRON_DIR_DOWN, TRON_DIR_LEFT, TRON_DIR_RIGHT };

#define TRON_FACILE    7  // real FACILE
#define TRON_MOYEN     15 // real MOYEN
#define TRON_DIFFICILE 25 // real DIFFICILE

enum TronState
{
    TRON_STATE_TITLE = 0,
    TRON_STATE_DIFFICULTY = 1,
    TRON_STATE_GAME = 2,
    TRON_STATE_GAMEOVER = 3
};

typedef struct
{
    int x, y, life, dir;
} TronSnake;

// Declared on two SEPARATE lines rather than the more natural
// `TronSnake tronP1, tronP2;` - the original Vircon32-dialect port this
// file was itself ported from found a real, empirically-confirmed dialect
// bug there (a comma-separated multi-variable global declaration of a
// STRUCT type corrupted memory - writes through the second variable's own
// fields landed in unrelated global state, visibly eating into
// gbFrameBuffer's own wall pixels), specific to that compiler, not to
// standard C (a normal C compiler has no such bug with `Foo a, b;` for
// struct types). Kept on two separate lines here anyway, purely as
// faithful historical documentation of that finding - a comma-separated
// declaration would be entirely safe in standard C, but there's no reason
// to "clean up" a working, already-verified declaration either.
TronSnake tronP1;
TronSnake tronP2;

int tronState;
// Which state to resume once a Button-C-triggered title interrupt (during
// TRON_STATE_GAME/TRON_STATE_GAMEOVER only - see header comment) is
// dismissed. Doubles as the very first boot's own real "where does the
// initial title screen lead" target (TRON_STATE_DIFFICULTY), since that
// value can never otherwise be reached here (Button C is never checked
// while TRON_STATE_DIFFICULTY is active).
int tronReturnState;

int tronDifficultyIndex; // 0=Easy/FACILE, 1=Hard/MOYEN, 2=Insane/DIFFICILE - real upstream difficultyMenu order
int tronDifficulter; // real `difficulter` - the actual frame rate the chosen difficulty runs gameplay at

// Local persistent trail bitplane - see this file's own header comment on
// why this shim's own auto-clearing gbUpdate() needs a per-game workaround
// here. Addressed exactly like gbFrameBuffer itself: idx = x + (y/8)*84,
// bit = 1<<(y%8). 84*6 = 504 words (LCDWIDTH x LCD_PAGES).
int tronTrail[504];

void tronTrailClear()
{
    int i;
    for( i = 0; i < 504; i = i + 1 )
      tronTrail[ i ] = 0;
}

void tronTrailMark( int x, int y )
{
    if( x < 0 || x >= LCDWIDTH || y < 0 || y >= LCDHEIGHT )
      return;

    int idx = x + ( y / 8 ) * LCDWIDTH;
    tronTrail[ idx ] = tronTrail[ idx ] | ( 1 << ( y % 8 ) );
}

int tronTrailHit( int x, int y )
{
    if( x < 0 || x >= LCDWIDTH || y < 0 || y >= LCDHEIGHT )
      return 0;

    int idx = x + ( y / 8 ) * LCDWIDTH;
    return ( tronTrail[ idx ] >> ( y % 8 ) ) & 1;
}

// Real border geometry from upstream's own initGame(): drawFastVLine(0/1
// and 82/83, 0, 48) + drawFastHLine(0, 0/1 and 46/47, 84) - a solid 2px
// wall on all four sides.
int tronIsWall( int x, int y )
{
    return ( x < 2 || x >= 82 || y < 2 || y >= 46 );
}

// The real collision predicate every `getPixel(x,y)==BLACK`/`==WHITE`
// check in upstream's own updPlayer()/ia() becomes: 1 = blocked (a real
// wall or trail pixel, upstream's own BLACK), 0 = free (upstream's own
// WHITE).
int tronBlocked( int x, int y )
{
    if( tronIsWall( x, y ) )
      return 1;

    return tronTrailHit( x, y );
}

void tronDrawWalls()
{
    gbSetColor( 1 );
    gbDrawFastVLine( 0, 0, 48 );
    gbDrawFastVLine( 1, 0, 48 );
    gbDrawFastVLine( 82, 0, 48 );
    gbDrawFastVLine( 83, 0, 48 );
    gbDrawFastHLine( 0, 0, 84 );
    gbDrawFastHLine( 0, 1, 84 );
    gbDrawFastHLine( 0, 46, 84 );
    gbDrawFastHLine( 0, 47, 84 );
}

void tronRenderTrail()
{
    gbSetColor( 1 );
    int x, y;
    for( y = 0; y < LCDHEIGHT; y = y + 1 )
      for( x = 0; x < LCDWIDTH; x = x + 1 )
        if( tronTrailHit( x, y ) )
          gbDrawPixel( x, y );
}

// Real drwPlayer(Snake play) - see header comment on the by-value struct
// rewrite. Draws the current 2x2 rider block AND marks it into the
// persistent trail bitplane so future ticks' tronRenderTrail() keeps
// showing it, exactly matching real hardware's own persistent framebuffer.
void tronDrawPlayerAt( int x, int y )
{
    gbSetColor( 1 );
    gbFillRect( x, y, TRON_STEP, TRON_STEP );

    int i, j;
    for( i = 0; i < TRON_STEP; i = i + 1 )
      for( j = 0; j < TRON_STEP; j = j + 1 )
        tronTrailMark( x + i, y + j );
}

// Real updPlayer(Snake *play) - direct port.
void tronUpdPlayer( TronSnake* play )
{
    if( play->dir == TRON_DIR_UP )
      play->y = play->y - TRON_STEP;
    else if( play->dir == TRON_DIR_DOWN )
      play->y = play->y + TRON_STEP;
    else if( play->dir == TRON_DIR_LEFT )
      play->x = play->x - TRON_STEP;
    else if( play->dir == TRON_DIR_RIGHT )
      play->x = play->x + TRON_STEP;

    if( tronBlocked( play->x, play->y ) )
      play->life = 0;
}

// Real human(Snake *play) - direct port.
void tronHuman( TronSnake* play )
{
    if( gbPressed( BTN_UP ) )
      play->dir = TRON_DIR_UP;
    else if( gbPressed( BTN_DOWN ) )
      play->dir = TRON_DIR_DOWN;
    else if( gbPressed( BTN_LEFT ) )
      play->dir = TRON_DIR_LEFT;
    else if( gbPressed( BTN_RIGHT ) )
      play->dir = TRON_DIR_RIGHT;
}

// Real ia(Snake *play) - direct, unmodified port (see header comment).
// The real do-while's own `forceSortie++<10` (checked, then incremented)
// is reproduced with the increment moved to the end of the loop body -
// still exactly 10 real retries maximum, same as upstream.
void tronIa( TronSnake* play )
{
    int isOk = 0;
    int forceSortie = 0;

    if( arand( 40 ) == 0 )
      play->dir = tronAllDir[ arand( 4 ) ];

    do
    {
        int nextX = play->x;
        int nextY = play->y;
        if( play->dir == TRON_DIR_UP )
          nextY = nextY - TRON_STEP;
        else if( play->dir == TRON_DIR_DOWN )
          nextY = nextY + TRON_STEP;
        else if( play->dir == TRON_DIR_LEFT )
          nextX = nextX - TRON_STEP;
        else if( play->dir == TRON_DIR_RIGHT )
          nextX = nextX + TRON_STEP;

        if( tronBlocked( nextX, nextY ) == 0 )
          isOk = 1;
        else
          play->dir = tronAllDir[ arand( 4 ) ];

        forceSortie = forceSortie + 1;
    } while( isOk == 0 && forceSortie < 10 );
}

// Real initGame() - direct port, minus the real fake-slave-button resets
// (bt_up/bt_down/bt_left/bt_right/bt_a/bt_b - multiplayer-only, dropped).
void tronInitGame()
{
    gbPickRandomSeed();
    tronTrailClear();

    tronP1.x = 10;
    tronP1.y = 10;
    tronP1.dir = TRON_DIR_RIGHT;
    tronP1.life = 1;

    tronP2.x = 74;
    tronP2.y = 38;
    tronP2.dir = TRON_DIR_LEFT;
    tronP2.life = 1;
}

void tronUpdateTitle()
{
    gbSetColor( 1 );
    gbFontSize = 1;
    gbCursorX = 14;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );
    gbDrawBitmap( 0, 12, tronMotoBitmap );

    if( gbPressed( BTN_A ) )
    {
        // Real initPrograme()'s own gb.setFrameRate(15) only ever runs
        // once, right after the very first real title-screen dismissal -
        // TRON_STATE_DIFFICULTY can only be tronReturnState's value on
        // that exact first dismissal (see this variable's own doc
        // comment), so this check reproduces that real one-time placement
        // exactly rather than re-applying it on every later Button-C
        // title interrupt too.
        if( tronReturnState == TRON_STATE_DIFFICULTY )
          gbSetFrameRate( 15 );

        tronState = tronReturnState;
    }
}

// Real initDifficulty()'s own gb.menu(difficultyMenu,3) - a hand-rolled
// replacement for that real binary library widget (out of this shim's own
// scope, same as every other ported game's own hand-rolled gb.menu()
// screen - see gameConduit.c's own condUpdateMenu() for this cartridge's
// established convention, including its own leading "*" selection marker,
// reused here). Real upstream's own difficultyMenu strings ("Easy"/"Hard"/
// "Insane") are kept verbatim even though they don't literally match
// FACILE/MOYEN/DIFFICILE's own French variable names ("Medium" labelled
// "Hard", "Difficult" labelled "Insane") - a real upstream naming quirk,
// preserved rather than "corrected" into a translation upstream itself
// never used.
void tronUpdateDifficulty()
{
    if( gbPressed( BTN_UP ) )
    {
        tronDifficultyIndex = tronDifficultyIndex - 1;
        if( tronDifficultyIndex < 0 )
          tronDifficultyIndex = 2;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        tronDifficultyIndex = tronDifficultyIndex + 1;
        if( tronDifficultyIndex > 2 )
          tronDifficultyIndex = 0;
    }

    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "DIFFICULTY" );

    gbSetFont( gbFont3x5 );
    gbCursorX = 2;
    gbCursorY = 12;
    gbPrintString( "UP/DOWN choose, A ok" );

    int i;
    for( i = 0; i < 3; i = i + 1 )
    {
        gbCursorY = 24 + i * 8;
        gbCursorX = 0;
        if( i == tronDifficultyIndex )
          gbPrintString( "*" );

        gbCursorX = 8;
        if( i == 0 )
          gbPrintString( "EASY" );
        else if( i == 1 )
          gbPrintString( "HARD" );
        else
          gbPrintString( "INSANE" );
    }

    if( gbPressed( BTN_A ) )
    {
        if( tronDifficultyIndex == 0 )
          tronDifficulter = TRON_FACILE;
        else if( tronDifficultyIndex == 1 )
          tronDifficulter = TRON_MOYEN;
        else
          tronDifficulter = TRON_DIFFICILE;

        gbSetFrameRate( tronDifficulter );
        tronInitGame();
        tronState = TRON_STATE_GAME;
    }
}

// Real case game: - human()/ia()/updatePlayer() (the real `if(!isPaused)`
// guard is dropped: isPaused can never become true anywhere in this
// single-player-only port, it was only ever set by the removed
// multiplayer paths) then drawPlayer(), then the real life==0 check.
void tronUpdateGame()
{
    tronHuman( &tronP1 );
    tronIa( &tronP2 );
    tronUpdPlayer( &tronP1 );
    tronUpdPlayer( &tronP2 );

    tronDrawWalls();
    tronRenderTrail();
    tronDrawPlayerAt( tronP1.x, tronP1.y );
    tronDrawPlayerAt( tronP2.x, tronP2.y );

    if( tronP1.life == 0 || tronP2.life == 0 )
    {
        tronState = TRON_STATE_GAMEOVER;
        gbSetFrameRate( 20 ); // real gb.setFrameRate(20);//Normal frameRate
    }
}

// Real case gameOver: - `p1.life==0 && (isOnePlayer||isMaster)` simplifies
// to a plain `p1.life==0` check since isOnePlayer is always true in this
// single-player-only port (p1 is always the real human player here).
void tronUpdateGameOver()
{
    gbSetColor( 1 );
    if( tronP1.life == 0 )
      gbDrawBitmap( 0, 0, tronLooserBitmap );
    else
      gbDrawBitmap( 0, 0, tronWinnerBitmap );

    if( gbPressed( BTN_B ) )
      tronState = TRON_STATE_DIFFICULTY; // see header comment on this adaptation

    if( gbPressed( BTN_A ) )
    {
        gbSetFrameRate( tronDifficulter );
        tronInitGame();
        tronState = TRON_STATE_GAME;
    }
}

void gameTron_init()
{
    gbBegin();
    tronState = TRON_STATE_TITLE;
    tronReturnState = TRON_STATE_DIFFICULTY;
    tronDifficultyIndex = 0;
    tronDifficulter = TRON_MOYEN; // real difficulter's own initial value before any real choice is made
}

void gameTron_update()
{
    if( !gbUpdate() )
      return;

    // Real Button-C "show title screen" check - only reachable in real
    // hardware during game/gameOver (see header comment).
    if( ( tronState == TRON_STATE_GAME || tronState == TRON_STATE_GAMEOVER ) && gbPressed( BTN_C ) )
    {
        tronReturnState = tronState;
        tronState = TRON_STATE_TITLE;
    }

    if( tronState == TRON_STATE_TITLE )
      tronUpdateTitle();
    else if( tronState == TRON_STATE_DIFFICULTY )
      tronUpdateDifficulty();
    else if( tronState == TRON_STATE_GAME )
      tronUpdateGame();
    else if( tronState == TRON_STATE_GAMEOVER )
      tronUpdateGameOver();

    gbRenderFrame();
}
