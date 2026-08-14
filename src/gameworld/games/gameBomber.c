#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Bomber (Clement - code, Quirby64 & Clement - art, license: none specified,
// github.com/Clement83/Bomber) - a real Bomberman-style clone: place bombs on
// a soft-block maze to kill two AI monsters before they kill you, across 5
// real distinct maze layouts.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameBomber.c (which already did the real upstream-Arduino-to-
// Vircon32-dialect porting work - the real multiplayer/I2C-master-slave
// removal, the menu collapse to a direct title-screen start, the real
// off-by-one game-over-check fix, the dead-code drops, the real bounds-
// safety addition in bombGetTile(), the sound approximation, and every
// other real preserved/normalized upstream quirk - see that file's own
// header comment below, kept verbatim, for the full original write-up,
// none of which needed revisiting here) - converting Vircon32's own C
// dialect back to standard C:
//  - `int[N] name;`/`int[N] name = {...};` -> `int name[N];`/`int
//    name[N] = {...};` (array-declaration order) for every numeric bitmap/
//    maze/sound-fx table (bombMiniBomberBitmap/bombMiniMonsterBitmap/
//    bombMiniExplodeBitmap/bombMiniCoupeBitmap/bombTitleScreenImage/
//    bombGameOverScreenBitmap/bombTableauScoreBitmap/bombMazeData1..5/
//    bombWinner/bombMaze), including the one 2D table
//    (`int[2][8] bombSoundFx` -> `int bombSoundFx[2][8]`) - genuinely
//    numeric byte data throughout, not text, so everything stays `int[]`.
//  - `struct BombPlayer { ... };`/`struct BombBombe { ... };` ->
//    `typedef struct { ... } BombPlayer;`/`typedef struct { ... }
//    BombBombe;`, so `BombPlayer*`/`BombBombe*`-style pointer parameters
//    and plain `BombPlayer x;`/`BombBombe x;` declarations both work
//    without a `struct` keyword at every use site.
//  - `BombBombe[BOMB_NB_BOMBE] bombMasterBombe;`/
//    `BombBombe[BOMB_NB_BOMBE] bombMonstreBombe;` ->
//    `BombBombe bombMasterBombe[BOMB_NB_BOMBE];`/
//    `BombBombe bombMonstreBombe[BOMB_NB_BOMBE];` (array-declaration
//    order, applied after the struct already had its own typedef name).
//  - `int[4] cheminPossible;` (a local inside bombUpdateMonstre()) ->
//    `int cheminPossible[4];`.
// No text-string array needed conversion - every string this game prints
// ("Press any key", "PRESS A") is already a plain C string literal, valid
// as-is in both dialects.
//
// ---------------------------------------------------------------------
// Everything below this point is the original gamebuino_classic_vircon32
// header comment, preserved verbatim (it documents real preserved
// upstream bugs/design decisions that still apply unchanged).
// ---------------------------------------------------------------------
//
// MULTIPLAYER REMOVED, PER THIS PROJECT'S OWN ESTABLISHED PRECEDENT: real
// upstream is 8 real `.ino` tabs - `Bomber`/`Bombe`/`Maze`/`Player`/
// `gameOverScreen`/`titleScreen` (kept, read in full) plus `master`/`slave`
// (dropped, also read in full to confirm neither one defines a shared helper
// any kept file still calls - both are pure `Wire.h` I2C protocol code with
// no such helper). Real upstream ships a genuine two-cartridge IR-link-style
// I2C master/slave multiplayer mode alongside a genuine, already-complete
// single-player mode against two real AI opponents (`monstre1`/`monstre2`) -
// there is only one emulated cartridge here, so the multiplayer mode is
// dropped entirely and only the single-player mode is ported, matching the
// exact "drop the real hardware/mode-specific option, keep the hardware-
// independent one" treatment already used for B-Rally's own accelerometer
// fallback and Senet's own I2C-multiplayer removal (see gameBRally.c/
// gameSenet.c). Concretely dropped: `setupMaster()`/`setupSlave()`/
// `updateMaster()`/`updateSlave()`/`masterRead()`/`masterWrite()`/
// `requestEvent()`/`receiveEvent()`/`updateNetwork()`/`updateSlavePlayer()`
// and every `Wire.h`/I2C-only `#define` (`SLAVE_PAUSED`/`I_AM_MASTER`/
// `STATE_GAME`/`PLAYER1_X`.../`BOMBE_NETWORK`/`BOMBE_DIST_EXP`/`BOMBE_X`/
// `BOMBE_Y`/`BT_UP`.../`SLAVE_DATA_BUFFER_LENGTH`) - none of them has any
// real caller left once the I2C menu option is gone. `slavePlayer` (the real
// second human player) is removed entirely too - every draw/collision/
// win-loss reference to it is gone, not left as a dead branch - and its own
// `MiniBomberP2` sprite is unused and dropped for the same reason (upstream
// itself only ever draws `MiniBomberP2` for `slavePlayer`, never for either
// AI monster). `slaveBombe[]` is removed the same way; only `masterBombe[]`/
// `monstreBombe[]` remain.
//
// MENU - COLLAPSED TO A DIRECT TITLE-SCREEN START, DOCUMENTED CHOICE: real
// upstream shows two separate real blocking screens in sequence - a genuine
// `gb.titleScreen(titleScreenImage)` widget (dismissed by any button), then
// a real `gb.menu(menu,3)` widget offering "Sigle player"/"Host (master)"/
// "Join (slave)" (note upstream's own real display-string typo, "Sigle
// player" - a plain UI string with zero gameplay effect, normalized to
// "Single player" would be if the menu still existed at all - moot here
// since the picker itself is gone, see below). Both real widgets
// (`gb.titleScreen()`/`gb.menu()`) have no shim equivalent (confirmed
// against the shim's own full API surface) and would need a hand-rolled
// UP/DOWN navigable replacement the way `gameSenet.c`'s own main menu does -
// but with Host/Join removed per the scope limit above, only one real choice
// would remain, and showing a picker with a single option to pick would look
// broken. Per this task's own explicit guidance, this port instead skips
// straight from the title screen into single-player game start on one
// A-press, matching Pong Solo's own established "one A-press dismisses the
// title screen, goes straight into gameplay" precedent (`gamePong.c`) -
// `bombUpdateTitle()` below draws the real title bitmap and starts the real
// single-player flow (`case 0` of upstream's own menu switch: `paused =
// false; isMaster = true; isSingle = true; stateGame = 1;`) the instant A is
// pressed, with no intermediate picker at all.
//
// `isMaster`/`isSingle`/`disconnected`/`paused` are all removed as plain
// dead state once multiplayer is gone: `isMaster`/`isSingle` are always true
// in the kept code path (every real branch that checks them - `if(isMaster)`
// in the main loop, `if(!isSingle)` guards around slave-only work - collapses
// to its single always-taken side); `disconnected` has no real single-player
// reader at all; and `paused` is traced through every real assignment site
// and found to be write-only-once-then-dead in single-player mode too - it
// starts `true`, is set `false` exactly once when "Sigle player" is chosen,
// and is only ever set back to `true` from inside the now-deleted
// `masterRead()`/`updateSlave()` network code, so once the picker collapses
// to "always single player", it is provably always `false` for the entire
// time any code that checks it (`updatePlayer()`/`updateMonstre()`) can ever
// run - removed from both, not left as an always-false guard.
//
// The single-player AI (`updateMonstres()`/`MonsterCanDropBombe()`/
// `chercherCheminPossible()`/`evaluateCase()`/`incraseLevelDanger()`) is
// ported fully and unmodified - a genuine tile-scoring heuristic that picks
// the least-dangerous of 4 neighboring tiles to move to (falling back to
// "stay put" if every neighbor scores as dangerous) and separately decides
// whether to drop a bomb (blocked by a pending bomb already ticking down
// under its own feet, by any of its 4 neighboring tiles scoring as
// dangerous, otherwise a 1-in-30 random urge to bomb regardless, or a
// deliberate bomb next to a real soft-block tile). `incraseLevelDanger()`'s
// own upstream name (a genuine typo for "increase") is normalized to
// `bombIncreaseLevelDanger()` here - purely an internal, never-externally-
// visible identifier spelling fix with zero gameplay effect, the same kind
// of harmless-typo normalization already established for Agaruino's own
// `joueurs`/`joueur` fix (see gameAgaruino.c's own header comment).
//
// A genuine PRE-EXISTING UPSTREAM BUG, FOUND AND FIXED (not preserved):
// `ScoreScreen()`'s own real continuation check is `if(currentLevel<NB_MAZE)
// stateGame=1; else stateGame=50;` - but `currentLevel` already equals
// `NB_MAZE-1` (4) once the 5th and final real maze (Maze5) finishes, so this
// check is off by one and lets a 6th "level" start. `loadMazeByNumero()`'s
// own switch has no `case 5`, so nothing loads for it (the maze array is
// silently left holding whatever partially-destroyed state Maze5 was last
// in) - and once that 6th round ends, its own `winner[currentLevel]` write
// becomes `winner[5]`, one past the end of the real `winner[NB_MAZE]`
// (`NB_MAZE`=5) array: a genuine out-of-bounds write on real hardware too,
// not something introduced by this port. Faithfully reproducing "whatever
// real AVR SRAM byte happened to sit right after `winner[]`" is both
// impossible and pointless here - this dialect's globals share nothing with
// real hardware's own memory layout, so the same write would just corrupt
// an unrelated Vircon32 global with no relationship whatsoever to what real
// hardware corrupts. Fixed by changing the check to `currentLevel<NB_MAZE-1`,
// matching the code's own obvious intent (exactly 5 real mazes, then the
// real final game-over screen) - see `bombScoreScreen()` below.
//
// A genuine dead upstream function, DROPPED (not ported): `initScore()` is
// defined but has zero real callers anywhere in upstream (confirmed by
// grepping every `.ino` file) - `winner[]` was always relying on plain
// static zero-initialization on real hardware's own single power-on. This
// port instead calls a ported `bombInitScore()` once from `gameBomber_init()`
// - a genuine, considered adaptation (not a preserved-as-is port of dead
// code) for the same reason `gbFrameCount`/`gbPopupTimeLeft` are reset on
// every launch elsewhere in this project: one cartridge session here can
// launch this game many times, and a stale `bombWinner[]` from an earlier
// playthrough in the same session would otherwise leak into a fresh one.
//
// A genuine dead upstream function, DROPPED (not ported): `caseHavePlayer()`
// is declared, defined (always `return false;`), and never called anywhere
// in any real `.ino` file - not a multiplayer-only casualty, just genuinely
// unreachable code on real hardware too.
//
// Screen shake (`shakeScreen()`/`ExplosionBombe()`'s own `shake_magnitude`/
// `shake_timeLeft`): the real camera-position jitter (`cameraX`/`cameraY`)
// is ported and fully preserved - a real, visible gameplay effect. Real
// upstream also flickers the LCD's own physical backlight brightness in the
// same function (`gb.backlight.set(...)`) - dropped outright, since
// Vircon32's own screen is not a backlit LCD and has no equivalent concept;
// this is a real hardware-specific cosmetic effect with nothing to port to,
// not a shim gap.
//
// A genuine, considered bounds-safety addition, not upstream behavior:
// `evaluateCase()`'s own AI heuristic probes tiles up to `DIST_EXPLOSION-1`
// (2) cells away from a monster's current position in all 4 directions,
// which can legally go one cell past the real maze's own solid outer wall
// ring for a monster standing right next to it (every real maze border is
// exactly one wall-tile thick). On real hardware this narrows a negative
// `char` offset into an unsigned `byte` parameter, wrapping to a large
// positive value and reading (undefined, garbage) memory well past the end
// of the real, fixed-size `Maze[]` array - a genuine pre-existing upstream
// memory-safety bug, not a deliberate feature. This dialect's `int`-only
// arithmetic does not reproduce the same byte-wraparound at all (an
// out-of-range probe here would instead read a small negative or
// past-the-end plain array index), so `bombGetTile()` below adds an explicit
// bounds check that treats any out-of-range coordinate as a solid wall
// (tile value 1) - a safe, sensible extension of the real maze border's own
// obvious design intent (every real maze already surrounds the play area
// with a solid wall ring at exactly these coordinates), not new gameplay
// behavior.
//
// SOUND - APPROXIMATED, DOCUMENTED (an already-established scope limit, not
// a new shim gap): upstream's own `playsoundfx()` issues a real low-level
// `gb.sound.command(CMD_VOLUME,...)/command(CMD_WAVE,...)/command(CMD_SLIDE,
// ...)/command(CMD_ARPEGGIO,...)` sequence before every `gb.sound.playNote()`
// call - a generic tracker-style command/arpeggio/instrument API this shim
// has no equivalent for (only one-shot `gbPlayNote`/`gbPlayTick`/`gbPlayOK`/
// `gbPlayCancel` exist), the same gap already documented in gameArmageddon.c.
// `bombPlaySound()` below keeps upstream's own real per-effect pitch/
// duration pair from its `soundfx[][8]` table (column 1 = pitch, column 7 =
// duration - the two values `gbPlayNote(pitch,duration)` can actually
// express) and calls `gbPlayNote()` directly with them, dropping only the
// volume/waveform/slide columns this shim has no primitive for, matching
// `gameArmageddon.c`'s own `armaPlaySound()` treatment exactly. The real
// `channel` parameter (always 0 in every real call site) is dropped too,
// since `gbPlayNote()` itself has no channel parameter.
//
// Real bitmap art (`MiniBomber`/`MiniMonster`/`MiniExplode`/`MiniCoupe`/
// `titleScreenImage`/`GameOverScreen`/`TableauScore`) is copied verbatim
// (real PROGMEM byte tables, hex literals copied directly) and drawn via
// `gbDrawBitmap()` from the very first pass - none of it is a self-contained-
// bitmap-without-a-mask situation (every one of these upstream bitmaps is
// drawn solo, with no separate fill/mask layer underneath it in real
// upstream source, so there is no FlappyBirdo-style bleed-through risk
// here). Upstream never calls `gb.display.setFont()` at all, so all text
// here inherits this shim's own real `gbFont3x5` default, matching real
// hardware's own same default.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods). `byte`/
// `int8_t`/`uint8_t` upstream types all collapse to plain `int` (this
// dialect's only integer type). `random(a,b)` (Arduino's exclusive-upper-
// bound ranged random) becomes `a + arand(b-a)`; `random(n)` becomes
// `arand(n)`. Upstream's own real by-value `Player`/`Bombe` struct
// parameters (`drawPlayer(Player play,...)`, `MonsterCanDropBombe(Player
// monster)`) are converted to pointer parameters throughout, since both
// structs are several words wide and this dialect only allows passing/
// returning a struct by value when it is exactly one word (see
// VIRCON32_C_DIALECT.md section 4) - `bombDrawPlayer()`'s own `isP1`
// parameter is dropped too, once `slavePlayer` (the only real caller that
// ever passed it `false` for a non-monster) is gone: the sole remaining
// human player always draws the real `MiniBomber` sprite, which
// `play->isMonster` alone is now enough to decide.
//
// Function/global naming: every global/function this game introduces uses
// a `bomb` prefix (this dialect has one flat global namespace shared with
// every other ported game).

typedef struct
{
    int x, y, xt, yt, nextBombe;
    bool isAlive, isMonster;
} BombPlayer;

typedef struct
{
    int x, y;
    int timer, distExplos;
    bool isAlive, explose;
} BombBombe;

#define BOMB_NB_BOMBE 6
#define BOMB_TIMER_BOMBE 80
#define BOMB_DIST_EXPLOSION 3
#define BOMB_NB_FRAME_EXPLOSION 18

#define BOMB_WIDTH_BLOCK 4
#define BOMB_HEIGHT_BLOCK 4
#define BOMB_WIDTH_MAZE 21
#define BOMB_HEIGHT_MAZE 12
#define BOMB_NB_MAZE 5

// Real upstream state numbers, kept as literal values for a direct 1:1 read
// against Bomber.ino's own `loop()` - state 20 (real upstream's own "main
// menu" state) is repurposed here as the hand-rolled title/start screen (see
// this file's own header comment on the menu collapse); state 12 (declared
// in upstream's own state-dispatch `else if`, but never assigned by any
// real code path in any of the kept `.ino` files) is dropped entirely as
// genuinely dead.
#define BOMB_STATE_TITLE        20
#define BOMB_STATE_START         1
#define BOMB_STATE_PLAY          0
#define BOMB_STATE_ENDGAME_ANIM 10
#define BOMB_STATE_SCORE        11
#define BOMB_STATE_GAMEOVER     50
#define BOMB_STATE_BACK_TO_TITLE 51

// -----------------------------------------------------------------------------
// Real bitmap art - copied verbatim from Player.ino/Maze.ino/gameOverScreen.ino/
// titleScreen.ino (bitmap[0]/[1] are the real width/height header bytes).
// -----------------------------------------------------------------------------

int bombMiniBomberBitmap[7] = {
    8, 5, 0x70, 0x88, 0xD8, 0xA8, 0xD8,
};

int bombMiniMonsterBitmap[7] = {
    8, 5, 0x70, 0xA8, 0xF8, 0xF8, 0x70,
};

int bombMiniExplodeBitmap[6] = {
    8, 4, 0x20, 0xC0, 0x30, 0xA0,
};

int bombMiniCoupeBitmap[7] = {
    8, 5, 0x70, 0xF8, 0x70, 0x20, 0x70,
};

int bombTitleScreenImage[290] = {
    64, 36, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0x0,
    0x0, 0x0, 0x18, 0x0, 0x0, 0x0, 0xFF, 0x0, 0x0, 0x0, 0x18, 0x0, 0x0, 0x0, 0xC0, 0xC3, 0xE6, 0x0, 0x18, 0x0,
    0x0, 0x0, 0xC0, 0xC6, 0x7A, 0x0, 0x18, 0x0, 0x0, 0x0, 0xC0, 0xCE, 0x54, 0xF3, 0x19, 0xE0, 0xFC, 0x67, 0xC0, 0xD9,
    0xCC, 0xF3, 0x19, 0xE0, 0xFC, 0x67, 0xFF, 0x11, 0xC6, 0xCC, 0xDE, 0x1B, 0x3, 0x78, 0xFF, 0x13, 0xFE, 0xCC, 0xDE, 0x1B,
    0x3, 0x78, 0xC0, 0xDF, 0xFE, 0xCC, 0xD8, 0x1B, 0xFF, 0x60, 0xC0, 0xDF, 0xFE, 0xCC, 0xD8, 0x1B, 0xFF, 0x60, 0xC0, 0xCF,
    0xFC, 0xCC, 0xDE, 0x1B, 0x0, 0x60, 0xC0, 0xC7, 0xF8, 0xCC, 0xDE, 0x1B, 0x0, 0x60, 0xFF, 0x3, 0xF0, 0xCC, 0xD9, 0xE0,
    0xFC, 0x60, 0xFF, 0x1, 0xE0, 0xCC, 0xD9, 0xE0, 0xFC, 0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x60,
    0x0, 0x0, 0x0, 0x8, 0x80, 0x4, 0x20, 0x90, 0x0, 0x0, 0x4, 0x44, 0x4F, 0x88, 0x23, 0xFC, 0x0, 0x0, 0xA, 0x22,
    0xB9, 0xD0, 0x24, 0x2, 0x0, 0x0, 0x11, 0x11, 0x9, 0x40, 0x8, 0x1, 0x0, 0x0, 0x24, 0xAA, 0x21, 0x30, 0x29, 0xF9,
    0x0, 0x0, 0x49, 0x44, 0x51, 0x18, 0xA, 0x95, 0x0, 0x0, 0x92, 0x0, 0x89, 0xF8, 0xA, 0x95, 0x0, 0x0, 0x49, 0x44,
    0x50, 0xF8, 0x9, 0xF9, 0x0, 0x0, 0x24, 0xAA, 0x20, 0xF8, 0x4, 0x2, 0x0, 0x0, 0x11, 0x13, 0x0, 0xF0, 0xF, 0xFF,
    0x0, 0x0, 0xA, 0x24, 0x8F, 0xE0, 0x13, 0xC, 0x80, 0x0, 0x4, 0x49, 0x53, 0xC8, 0x12, 0x94, 0x80, 0x0, 0x0, 0x12,
    0x20, 0x4, 0xF, 0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0xF2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x9C,
    0x80, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0xB2, 0x9D, 0x1D, 0xDD, 0x9D, 0x0, 0x7F, 0xFF, 0xB9, 0x11, 0x11, 0xD1,
    0x49, 0xFE, 0x0, 0x0, 0xB1, 0x1D, 0xDD, 0x5D, 0x49, 0x0,
};

int bombGameOverScreenBitmap[299] = {
    88, 27, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x11, 0x0, 0x0, 0x0, 0x8, 0x80, 0x0, 0x30, 0x18, 0xC,
    0xFF, 0x22, 0x22, 0x0, 0x4, 0x44, 0x4F, 0x80, 0x38, 0x1C, 0xC, 0xC1, 0xD4, 0x45, 0x0, 0xA, 0x22, 0xB8, 0x40, 0x68,
    0x1C, 0x1C, 0xC0, 0x8, 0x88, 0x80, 0x11, 0x11, 0x0, 0x20, 0x4C, 0x1E, 0x14, 0xC0, 0x45, 0x52, 0x40, 0x24, 0xAA, 0x20,
    0x20, 0x4C, 0x1A, 0x24, 0xC0, 0xA2, 0x29, 0x20, 0x49, 0x44, 0x50, 0x0, 0xC4, 0x1A, 0x24, 0xFD, 0x10, 0x4, 0x90, 0x92,
    0x0, 0x88, 0x0, 0x86, 0x1B, 0x24, 0xC0, 0xA2, 0x29, 0x20, 0x49, 0x44, 0x51, 0xE1, 0xFE, 0x19, 0x44, 0xC0, 0x45, 0x52,
    0x40, 0x24, 0xAA, 0x20, 0x21, 0x82, 0x19, 0x44, 0xC0, 0xC, 0x88, 0x80, 0x11, 0x13, 0x0, 0x21, 0x3, 0x19, 0xC4, 0xC0,
    0x12, 0x45, 0x0, 0xA, 0x24, 0x80, 0x23, 0x1, 0x18, 0x84, 0xC0, 0xA9, 0x22, 0x0, 0x4, 0x49, 0x5E, 0x62, 0x1, 0x98,
    0x84, 0xFF, 0x44, 0x80, 0x0, 0x0, 0x12, 0x21, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x80, 0x4,
    0x0, 0x0, 0x0, 0x0, 0x11, 0x0, 0x0, 0x4, 0x44, 0x4F, 0x89, 0x0, 0xDF, 0xE3, 0xF9, 0xA2, 0x22, 0x0, 0xA, 0x22,
    0xB9, 0xD1, 0x80, 0x90, 0x2, 0xA, 0x54, 0x45, 0x0, 0x11, 0x11, 0x9, 0x40, 0x81, 0x90, 0x2, 0x4, 0x8, 0x88, 0x80,
    0x24, 0xAA, 0x21, 0x30, 0xC1, 0x90, 0x2, 0x2, 0x45, 0x52, 0x40, 0x49, 0x44, 0x51, 0x18, 0x41, 0x10, 0x2, 0x2, 0xA2,
    0x29, 0x20, 0x92, 0x0, 0x89, 0xF8, 0x43, 0x1F, 0xE2, 0xD, 0x10, 0x4, 0x90, 0x49, 0x44, 0x50, 0xF8, 0x62, 0x10, 0x3,
    0xF8, 0xA2, 0x29, 0x20, 0x24, 0xAA, 0x20, 0xF8, 0x26, 0x10, 0x2, 0x30, 0x45, 0x52, 0x40, 0x11, 0x13, 0x0, 0xF0, 0x34,
    0x10, 0x2, 0x18, 0xC, 0x88, 0x80, 0xA, 0x24, 0x8F, 0xE0, 0x14, 0x10, 0x2, 0xF, 0x12, 0x45, 0x0, 0x4, 0x49, 0x53,
    0xC8, 0x18, 0x10, 0x2, 0x4, 0xA9, 0x22, 0x0, 0x0, 0x12, 0x20, 0x4, 0x8, 0x1F, 0xE2, 0x2, 0x44, 0x80, 0x0,
};

int bombTableauScoreBitmap[530] = {
    88, 48, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0xC0, 0x9F, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7F, 0x20, 0x90, 0x20, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x81, 0x20, 0x60, 0x17, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0x0, 0xC0, 0x20, 0xF6, 0xC,
    0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0xE0, 0x80, 0x21, 0x55, 0x14, 0x97, 0x5D, 0x55, 0xD5, 0x26, 0x65, 0x50, 0x80, 0x21,
    0x54, 0xA5, 0x91, 0x45, 0x55, 0x15, 0x2, 0x25, 0x50, 0x80, 0x20, 0xF4, 0x44, 0x97, 0x4D, 0x75, 0xD6, 0x62, 0x25, 0xE0,
    0x80, 0x10, 0x24, 0xA4, 0x94, 0x45, 0x14, 0x55, 0x22, 0x24, 0x81, 0x0, 0xF, 0xC5, 0x15, 0xD7, 0x5D, 0x15, 0xD5, 0x77,
    0x74, 0x7E, 0x0, 0xC, 0xC6, 0xC, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x66, 0x0, 0xC, 0xC7, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFC, 0x66, 0x0, 0xB, 0x44, 0x4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x5A, 0x0, 0x4, 0x84, 0xE4, 0x10,
    0x41, 0x4, 0x10, 0x0, 0x4, 0x24, 0x0, 0x7, 0x5, 0x14, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x1C, 0x0, 0x0, 0x5,
    0xB4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x0, 0x0, 0x80, 0xD, 0x54, 0x10, 0x41, 0x4, 0x10, 0x0, 0x6, 0x0, 0x20,
    0x47, 0x95, 0xB4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0x1E, 0x40, 0x2F, 0xC4, 0x4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4,
    0xBF, 0x0, 0xB, 0xE7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x2F, 0x80, 0x33, 0xF4, 0x4, 0x10, 0x41, 0x4, 0x10,
    0x0, 0x4, 0xCF, 0xC0, 0x63, 0xF4, 0xE4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0x8F, 0xC0, 0x7E, 0x35, 0xB4, 0x10, 0x41,
    0x4, 0x10, 0x0, 0x5, 0xF8, 0xC0, 0x40, 0x35, 0x14, 0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0x0, 0xC0, 0x40, 0x25, 0x54,
    0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0x0, 0x80, 0x72, 0x24, 0xA4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0xC8, 0x80, 0x25,
    0x14, 0x4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x94, 0x40, 0x28, 0x8F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xA2,
    0x20, 0x45, 0x14, 0x4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0x14, 0x40, 0xA2, 0x24, 0xE4, 0x10, 0x41, 0x4, 0x10, 0x0,
    0x6, 0x88, 0x80, 0x10, 0x55, 0x54, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x41, 0x40, 0x28, 0xCD, 0xF4, 0x10, 0x41, 0x4,
    0x10, 0x0, 0x4, 0xA3, 0x20, 0x45, 0x25, 0xF4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0x14, 0x80, 0x88, 0x94, 0xE4, 0x10,
    0x41, 0x4, 0x10, 0x0, 0x6, 0x22, 0x40, 0x10, 0x4C, 0x4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x41, 0x20, 0x28, 0xA7,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xA2, 0x80, 0x45, 0x14, 0x4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x5, 0x14, 0x40,
    0x8, 0x84, 0xE4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x22, 0x0, 0x15, 0x45, 0x54, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4,
    0x55, 0x0, 0x22, 0x25, 0xF4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x88, 0x80, 0x48, 0x95, 0xF4, 0x10, 0x41, 0x4, 0x10,
    0x0, 0x5, 0x22, 0x40, 0x25, 0x24, 0xE4, 0x10, 0x41, 0x4, 0x10, 0x0, 0x4, 0x94, 0x80, 0x12, 0x44, 0x4, 0x10, 0x41,
    0x4, 0x10, 0x0, 0x4, 0x49, 0x0, 0x8, 0x87, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x22, 0x0, 0x5, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x14, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

// -----------------------------------------------------------------------------
// Real maze layouts - copied verbatim from Maze.ino's own Maze1..Maze5 tables
// (tile values: 0=floor, 1=solid wall, 2=soft/breakable block).
// -----------------------------------------------------------------------------

int bombMazeData1[252] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 0, 2, 1, 2, 1, 0, 0, 0, 0, 0, 1, 2, 2, 2, 2, 2, 1, 2, 0, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 1,
    1, 2, 2, 1, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1, 2, 0, 1, 2, 2, 2, 1,
    1, 2, 1, 0, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 1,
    1, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 1, 2, 2, 1,
    1, 2, 2, 1, 2, 0, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 1, 2, 2, 2, 1,
    1, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 0, 2, 1, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0, 0, 2, 2, 1, 2, 0, 1,
    1, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

int bombMazeData2[252] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 0, 1, 2, 1, 2, 1, 2, 0, 2, 0, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 1,
    1, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 0, 1, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1,
    1, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

int bombMazeData3[252] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 0, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 0, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 2, 2, 2, 1, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 1, 2, 2, 2, 1,
    1, 2, 2, 2, 2, 0, 2, 2, 2, 1, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 2, 2, 2, 1, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 1, 2, 2, 2, 1,
    1, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 1,
    1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1,
    1, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

int bombMazeData4[252] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 1,
    1, 0, 1, 2, 2, 2, 0, 2, 2, 0, 0, 0, 2, 2, 1, 2, 2, 2, 1, 0, 1,
    1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1,
    1, 1, 2, 2, 1, 2, 1, 2, 2, 1, 1, 1, 2, 2, 2, 2, 1, 2, 2, 1, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 2, 2, 2, 1, 2, 1, 2, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 2, 2, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 1, 2, 2, 1, 2, 1, 2, 2, 1, 1, 1, 2, 2, 2, 2, 1, 2, 2, 2, 1,
    1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1,
    1, 0, 1, 2, 2, 2, 1, 2, 2, 0, 0, 0, 2, 2, 1, 2, 2, 2, 1, 0, 1,
    1, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

int bombMazeData5[252] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 0, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1,
    1, 2, 0, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1,
    1, 2, 1, 0, 0, 0, 1, 2, 0, 2, 1, 2, 0, 2, 1, 2, 0, 2, 1, 2, 1,
    1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1,
    1, 2, 0, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1,
    1, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 0, 2, 0, 2, 0, 2, 1, 2, 1,
    1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1,
    1, 0, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1,
    1, 0, 0, 0, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

// Real upstream `soundfx[2][8]` table - column 1 is note pitch, column 7 is
// note duration (see this file's own header comment on sound approximation).
int bombSoundFx[2][8] = {
    { 1, 4, 113, 10, 7, 19, 7, 52 },
    { 0, 19, 9, 1, 7, 15, 7, 4 },
};

void bombPlaySound( int fxno )
{
    gbPlayNote( bombSoundFx[ fxno ][ 1 ], bombSoundFx[ fxno ][ 7 ] );
}

// -----------------------------------------------------------------------------
// Game state
// -----------------------------------------------------------------------------

BombPlayer bombMasterPlayer;
BombPlayer bombMonstre1;
BombPlayer bombMonstre2;

BombBombe bombMasterBombe[BOMB_NB_BOMBE];
BombBombe bombMonstreBombe[BOMB_NB_BOMBE];

int bombWinner[BOMB_NB_MAZE];

int bombMaze[BOMB_HEIGHT_MAZE*BOMB_WIDTH_MAZE];

int bombDistExplosion;

int bombShakeMagnitude;
int bombShakeTimeLeft;
int bombCameraX, bombCameraY;

int bombStateGame;
int bombCurrentLevel;
int bombTimerAnimEndGame;

// -----------------------------------------------------------------------------
// Maze (Maze.ino)
// -----------------------------------------------------------------------------

// Real `getTile()`/`setTile()`, extended with an explicit bounds check - see
// this file's own header comment on the real out-of-bounds AI probe bug this
// guards against. Any out-of-range coordinate reads as a solid wall (1),
// matching the real maze's own border design.
int bombGetTile( int x, int y )
{
    if( x < 0 || x >= BOMB_WIDTH_MAZE || y < 0 || y >= BOMB_HEIGHT_MAZE )
      return 1;
    return bombMaze[ ( y * BOMB_WIDTH_MAZE ) + x ];
}

void bombSetTile( int x, int y, int value )
{
    if( x < 0 || x >= BOMB_WIDTH_MAZE || y < 0 || y >= BOMB_HEIGHT_MAZE )
      return;
    bombMaze[ ( y * BOMB_WIDTH_MAZE ) + x ] = value;
}

void bombLoadMaze( int* maze )
{
    int x, y;
    for( y = 0; y < BOMB_HEIGHT_MAZE; y++ )
      for( x = 0; x < BOMB_WIDTH_MAZE; x++ )
        bombMaze[ ( y * BOMB_WIDTH_MAZE ) + x ] = maze[ ( y * BOMB_WIDTH_MAZE ) + x ];
}

void bombLoadMazeByNumero( int numero )
{
    if( numero == 0 )
      bombLoadMaze( bombMazeData1 );
    else if( numero == 1 )
      bombLoadMaze( bombMazeData2 );
    else if( numero == 2 )
      bombLoadMaze( bombMazeData3 );
    else if( numero == 3 )
      bombLoadMaze( bombMazeData4 );
    else if( numero == 4 )
      bombLoadMaze( bombMazeData5 );
}

bool bombCaseHaveBombe( int x, int y )
{
    int i;
    for( i = 0; i < BOMB_NB_BOMBE; i++ )
    {
        if( bombMasterBombe[ i ].isAlive && ( bombMasterBombe[ i ].x / 4 ) == x && ( bombMasterBombe[ i ].y / 4 ) == y )
          return true;
        if( bombMonstreBombe[ i ].isAlive && ( bombMonstreBombe[ i ].x / 4 ) == x && ( bombMonstreBombe[ i ].y / 4 ) == y )
          return true;
    }
    return false;
}

// Real camera-shake position jitter only - see this file's own header
// comment on why the real backlight-flicker half of this function is
// dropped.
void bombShakeScreen()
{
    if( bombShakeTimeLeft )
    {
        bombShakeTimeLeft--;
        bombCameraX += arand( ( 2 * bombShakeMagnitude ) + 1 ) - bombShakeMagnitude;
        bombCameraY += arand( ( 2 * bombShakeMagnitude ) + 1 ) - bombShakeMagnitude;
    }
    else
    {
        bombCameraX = 0;
        bombCameraY = 0;
    }
}

void bombDrawMaze()
{
    int x, y, spriteID;

    bombShakeScreen();

    for( y = 0; y < BOMB_HEIGHT_MAZE; y++ )
    {
        for( x = 0; x < BOMB_WIDTH_MAZE; x++ )
        {
            spriteID = bombGetTile( x, y );
            if( spriteID == 0 )
              continue;
            else if( spriteID == 1 )
              gbFillRect( ( x * BOMB_WIDTH_BLOCK ) - bombCameraX, ( y * BOMB_HEIGHT_BLOCK ) - bombCameraY, BOMB_WIDTH_BLOCK, BOMB_HEIGHT_BLOCK );
            else if( spriteID > 10 )
            {
                gbDrawBitmap( x * BOMB_WIDTH_BLOCK, y * BOMB_HEIGHT_BLOCK, bombMiniExplodeBitmap );
                spriteID--;
                if( spriteID == 10 )
                  spriteID = 0;
                bombSetTile( x, y, spriteID );
            }
            else
              gbDrawRect( ( x * BOMB_WIDTH_BLOCK ) + 1, ( y * BOMB_HEIGHT_BLOCK ) + 1, BOMB_WIDTH_BLOCK / 2, BOMB_HEIGHT_BLOCK / 2 );
        }
    }
}

// -----------------------------------------------------------------------------
// Bombs (Bombe.ino)
// -----------------------------------------------------------------------------

void bombDropBombe( int x, int y, BombBombe* bombeArray )
{
    int cpt;

    cpt = 0;
    do
    {
        if( bombeArray[ cpt ].isAlive == false )
        {
            bombeArray[ cpt ].timer = BOMB_TIMER_BOMBE;
            bombeArray[ cpt ].distExplos = bombDistExplosion;
            bombeArray[ cpt ].isAlive = true;
            bombeArray[ cpt ].x = x;
            bombeArray[ cpt ].y = y;
            bombPlaySound( 1 );
            break;
        }
        cpt++;
    }
    while( cpt < BOMB_NB_BOMBE );
}

void bombDrawBombes()
{
    int i;
    for( i = 0; i < BOMB_NB_BOMBE; i++ )
    {
        if( bombMasterBombe[ i ].isAlive && ( bombMasterBombe[ i ].timer % 20 ) > 5 )
          gbFillCircle( bombMasterBombe[ i ].x + 2, bombMasterBombe[ i ].y + 2, 1 );
        if( bombMonstreBombe[ i ].isAlive && ( bombMonstreBombe[ i ].timer % 20 ) > 5 )
          gbFillCircle( bombMonstreBombe[ i ].x + 2, bombMonstreBombe[ i ].y + 2, 1 );
    }
}

bool bombSetTuileExplosion( int tuileX, int tuileY )
{
    int tuileId;

    tuileId = bombGetTile( tuileX, tuileY );
    if( tuileId != 1 )
      bombSetTile( tuileX, tuileY, BOMB_NB_FRAME_EXPLOSION );

    if( tuileId == 1 || tuileId == 2 )
      return true;
    return false;
}

void bombExplosionBombe( BombBombe* laBombe )
{
    int decalageX, decalageY, tuileX, tuileY;

    bombPlaySound( 0 );
    bombShakeMagnitude = 2;
    bombShakeTimeLeft = 3;
    laBombe->explose = true;

    for( decalageX = 0; decalageX < laBombe->distExplos; decalageX++ )
    {
        tuileX = ( laBombe->x / 4 ) + decalageX;
        tuileY = laBombe->y / 4;
        if( bombSetTuileExplosion( tuileX, tuileY ) )
          break;
    }
    for( decalageX = 0; decalageX < laBombe->distExplos; decalageX++ )
    {
        tuileX = ( laBombe->x / 4 ) - decalageX;
        tuileY = laBombe->y / 4;
        if( bombSetTuileExplosion( tuileX, tuileY ) )
          break;
    }
    for( decalageY = 0; decalageY < laBombe->distExplos; decalageY++ )
    {
        tuileX = laBombe->x / 4;
        tuileY = ( laBombe->y / 4 ) + decalageY;
        if( bombSetTuileExplosion( tuileX, tuileY ) )
          break;
    }
    for( decalageY = 0; decalageY < laBombe->distExplos; decalageY++ )
    {
        tuileX = laBombe->x / 4;
        tuileY = ( laBombe->y / 4 ) - decalageY;
        if( bombSetTuileExplosion( tuileX, tuileY ) )
          break;
    }
}

void bombUpdateBombes()
{
    int i;
    for( i = 0; i < BOMB_NB_BOMBE; i++ )
    {
        if( bombMasterBombe[ i ].isAlive )
        {
            if( bombMasterBombe[ i ].timer == 0 || bombGetTile( bombMasterBombe[ i ].x / 4, bombMasterBombe[ i ].y / 4 ) > 2 )
            {
                bombMasterBombe[ i ].timer = 0;
                bombMasterBombe[ i ].isAlive = false;
                bombExplosionBombe( &bombMasterBombe[ i ] );
            }
            bombMasterBombe[ i ].timer--;
        }
        if( bombMonstreBombe[ i ].isAlive )
        {
            if( bombMonstreBombe[ i ].timer == 0 || bombGetTile( bombMonstreBombe[ i ].x / 4, bombMonstreBombe[ i ].y / 4 ) > 2 )
            {
                bombMonstreBombe[ i ].timer = 0;
                bombMonstreBombe[ i ].isAlive = false;
                bombExplosionBombe( &bombMonstreBombe[ i ] );
            }
            bombMonstreBombe[ i ].timer--;
        }
    }
}

// -----------------------------------------------------------------------------
// Players + AI (Player.ino)
// -----------------------------------------------------------------------------

void bombP1StartPos()
{
    bombMasterPlayer.x = 4;
    bombMasterPlayer.y = 4;
    bombMasterPlayer.xt = 1;
    bombMasterPlayer.yt = 1;
    bombMasterPlayer.isAlive = true;
    bombMasterPlayer.isMonster = false;
}

void bombM1StartPos()
{
    bombMonstre1.x = 4;
    bombMonstre1.y = 40;
    bombMonstre1.xt = 1;
    bombMonstre1.yt = 10;
    bombMonstre1.isAlive = true;
    bombMonstre1.isMonster = true;
}

void bombM2StartPos()
{
    bombMonstre2.x = 76;
    bombMonstre2.y = 4;
    bombMonstre2.xt = 19;
    bombMonstre2.yt = 1;
    bombMonstre2.isAlive = true;
    bombMonstre2.isMonster = true;
}

void bombDrawPlayer( BombPlayer* play )
{
    if( !play->isAlive )
      return;

    if( !play->isMonster )
      gbDrawBitmap( play->x, play->y, bombMiniBomberBitmap );
    else
      gbDrawBitmap( play->x, play->y, bombMiniMonsterBitmap );
}

void bombDrawPlayers()
{
    bombDrawPlayer( &bombMasterPlayer );
    bombDrawPlayer( &bombMonstre1 );
    bombDrawPlayer( &bombMonstre2 );
}

bool bombTileIsOk( int x, int y )
{
    int tile;

    if( bombCaseHaveBombe( x, y ) )
      return false;

    tile = bombGetTile( x, y );
    return tile == 0 || tile > 10;
}

bool bombTileIsOkMonster( int x, int y )
{
    return bombGetTile( x, y ) == 0;
}

// Real `incraseLevelDanger()` (a genuine upstream spelling typo, normalized
// here - see this file's own header comment). Returns a "safety score" for
// one probed tile: below 10 is safe, above 50 is a real, imminent bomb
// danger (proportional to how close the probed tile sits to a live bomb).
int bombIncreaseLevelDanger( int tuileX, int tuileY, int decalage )
{
    if( bombCaseHaveBombe( tuileX, tuileY ) )
      return ( ( BOMB_DIST_EXPLOSION - decalage ) + 50 ) + arand( 10 );
    return arand( 10 );
}

int bombEvaluateCase( int x, int y )
{
    int levelDanger, decalageX, decalageY, tuileX, tuileY, tileId;

    levelDanger = 0;

    for( decalageX = 0; decalageX < BOMB_DIST_EXPLOSION; decalageX++ )
    {
        tuileX = x + decalageX;
        tuileY = y;
        tileId = bombGetTile( tuileX, tuileY );
        if( tileId == 1 || tileId == 2 )
          break;
        levelDanger += bombIncreaseLevelDanger( tuileX, tuileY, decalageX );
    }
    for( decalageX = 0; decalageX < BOMB_DIST_EXPLOSION; decalageX++ )
    {
        tuileX = x - decalageX;
        tuileY = y;
        tileId = bombGetTile( tuileX, tuileY );
        if( tileId == 1 || tileId == 2 )
          break;
        levelDanger += bombIncreaseLevelDanger( tuileX, tuileY, decalageX );
    }
    for( decalageY = 0; decalageY < BOMB_DIST_EXPLOSION; decalageY++ )
    {
        tuileX = x;
        tuileY = y + decalageY;
        tileId = bombGetTile( tuileX, tuileY );
        if( tileId == 1 || tileId == 2 )
          break;
        levelDanger += bombIncreaseLevelDanger( tuileX, tuileY, decalageY );
    }
    for( decalageY = 0; decalageY < BOMB_DIST_EXPLOSION; decalageY++ )
    {
        tuileX = x;
        tuileY = y - decalageY;
        tileId = bombGetTile( tuileX, tuileY );
        if( tileId == 1 || tileId == 2 )
          break;
        levelDanger += bombIncreaseLevelDanger( tuileX, tuileY, decalageY );
    }

    return levelDanger;
}

void bombChercherCheminPossible( int* cheminPossible, BombPlayer* monstre )
{
    int i;

    for( i = 0; i < 4; i++ )
      cheminPossible[ i ] = 9999;

    if( bombTileIsOkMonster( monstre->xt, monstre->yt + 1 ) )
      cheminPossible[ 0 ] = bombEvaluateCase( monstre->xt, monstre->yt + 1 );
    if( bombTileIsOkMonster( monstre->xt + 1, monstre->yt ) )
      cheminPossible[ 1 ] = bombEvaluateCase( monstre->xt + 1, monstre->yt );
    if( bombTileIsOkMonster( monstre->xt, monstre->yt - 1 ) )
      cheminPossible[ 2 ] = bombEvaluateCase( monstre->xt, monstre->yt - 1 );
    if( bombTileIsOkMonster( monstre->xt - 1, monstre->yt ) )
      cheminPossible[ 3 ] = bombEvaluateCase( monstre->xt - 1, monstre->yt );
}

bool bombMonsterCanDropBombe( BombPlayer* monster )
{
    int tile;

    if( monster->nextBombe > 0 )
      return false;

    if( bombCaseHaveBombe( monster->xt, monster->yt ) )
      return false;

    if( bombEvaluateCase( monster->xt - 1, monster->yt ) > 50 )
      return false;
    if( bombEvaluateCase( monster->xt + 1, monster->yt ) > 50 )
      return false;
    if( bombEvaluateCase( monster->xt, monster->yt - 1 ) > 50 )
      return false;
    if( bombEvaluateCase( monster->xt, monster->yt + 1 ) > 50 )
      return false;

    if( arand( 30 ) == 0 )
      return true;

    tile = bombGetTile( monster->xt - 1, monster->yt );
    if( tile == 2 )
      return true;
    tile = bombGetTile( monster->xt + 1, monster->yt );
    if( tile == 2 )
      return true;
    tile = bombGetTile( monster->xt, monster->yt - 1 );
    if( tile == 2 )
      return true;
    tile = bombGetTile( monster->xt, monster->yt + 1 );
    if( tile == 2 )
      return true;

    return false;
}

void bombUpdateMonstre( BombPlayer* monstre )
{
    int cheminPossible[4];
    int choix, oldValue, tmpValue, i;

    if( !monstre->isAlive )
      return;

    // Only re-plan once the monster has fully arrived at its current target
    // tile - matches real upstream's own `x==xt*4 && y==yt*4` gate exactly.
    if( monstre->x != ( monstre->xt * 4 ) || monstre->y != ( monstre->yt * 4 ) )
      return;

    bombChercherCheminPossible( cheminPossible, monstre );

    choix = 150;
    oldValue = 9999;
    for( i = 0; i < 4; i++ )
    {
        if( cheminPossible[ i ] < oldValue )
        {
            oldValue = cheminPossible[ i ];
            choix = i;
        }
    }

    // Every neighboring tile scores as a real, imminent danger - the
    // monster's own current tile gets one more check before deciding
    // whether to sit still instead of walking into a worse spot.
    if( oldValue > 50 )
    {
        tmpValue = bombEvaluateCase( monstre->xt, monstre->yt );
        if( tmpValue < 50 )
          return;
    }

    if( choix == 0 )
    {
        if( bombTileIsOkMonster( monstre->xt, monstre->yt + 1 ) )
          monstre->yt++;
    }
    else if( choix == 1 )
    {
        if( bombTileIsOkMonster( monstre->xt + 1, monstre->yt ) )
          monstre->xt++;
    }
    else if( choix == 2 )
    {
        if( bombTileIsOkMonster( monstre->xt, monstre->yt - 1 ) )
          monstre->yt--;
    }
    else if( choix == 3 )
    {
        if( bombTileIsOkMonster( monstre->xt - 1, monstre->yt ) )
          monstre->xt--;
    }

    if( bombMonsterCanDropBombe( monstre ) )
    {
        bombDropBombe( monstre->xt * 4, monstre->yt * 4, bombMonstreBombe );
        monstre->nextBombe = BOMB_NB_FRAME_EXPLOSION;
    }
    else if( monstre->nextBombe > 0 )
      monstre->nextBombe--;
}

void bombUpdateMonstres()
{
    bombUpdateMonstre( &bombMonstre1 );
    bombUpdateMonstre( &bombMonstre2 );
}

void bombUpdatePlayerAll( BombPlayer* play )
{
    bool isMove;

    if( !play->isAlive )
      return;

    isMove = ( play->x != play->xt * 4 ) || ( play->y != play->yt * 4 );

    if( isMove )
    {
        if( play->x != play->xt * 4 )
        {
            if( play->xt * 4 > play->x )
              play->x++;
            else
              play->x--;
        }
        else if( play->y != play->yt * 4 )
        {
            if( play->yt * 4 > play->y )
              play->y++;
            else
              play->y--;
        }
    }

    if( bombGetTile( play->xt, play->yt ) > 10 )
      play->isAlive = false;
}

void bombPressUp( BombPlayer* play )
{
    bool isMove;
    isMove = ( play->x != play->xt * 4 ) || ( play->y != play->yt * 4 );
    if( !isMove )
      if( bombTileIsOk( play->xt, play->yt - 1 ) )
        play->yt--;
}

void bombPressDown( BombPlayer* play )
{
    bool isMove;
    isMove = ( play->x != play->xt * 4 ) || ( play->y != play->yt * 4 );
    if( !isMove )
      if( bombTileIsOk( play->xt, play->yt + 1 ) )
        play->yt++;
}

void bombPressLeft( BombPlayer* play )
{
    bool isMove;
    isMove = ( play->x != play->xt * 4 ) || ( play->y != play->yt * 4 );
    if( !isMove )
      if( bombTileIsOk( play->xt - 1, play->yt ) )
        play->xt--;
}

void bombPressRight( BombPlayer* play )
{
    bool isMove;
    isMove = ( play->x != play->xt * 4 ) || ( play->y != play->yt * 4 );
    if( !isMove )
      if( bombTileIsOk( play->xt + 1, play->yt ) )
        play->xt++;
}

// Real upstream branches on `isMaster` to pick which bomb array a dropped
// bomb belongs to - always `masterBombe` now that `slavePlayer`/the network
// branch is gone (this function's only real caller is the human player).
void bombPressA( BombPlayer* play )
{
    if( !bombCaseHaveBombe( play->xt, play->yt ) )
      bombDropBombe( play->xt * 4, play->yt * 4, bombMasterBombe );
}

void bombUpdatePlayer( BombPlayer* play )
{
    if( !play->isAlive )
      return;

    if( gbRepeat( BTN_RIGHT, 1 ) )
      bombPressRight( play );
    else if( gbRepeat( BTN_LEFT, 1 ) )
      bombPressLeft( play );
    else if( gbRepeat( BTN_UP, 1 ) )
      bombPressUp( play );
    else if( gbRepeat( BTN_DOWN, 1 ) )
      bombPressDown( play );

    if( gbPressed( BTN_A ) )
      bombPressA( play );
}

// -----------------------------------------------------------------------------
// Score/game-over screens (gameOverScreen.ino)
// -----------------------------------------------------------------------------

void bombGameOverScreen()
{
    gbDrawBitmap( 0, 0, bombGameOverScreenBitmap );

    gbCursorX = 10;
    gbCursorY = 42;
    gbPrintString( "Press any key" );

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
      bombStateGame = BOMB_STATE_BACK_TO_TITLE;
}

void bombScoreScreen()
{
    int i;

    gbDrawBitmap( 0, 0, bombTableauScoreBitmap );

    for( i = 0; i < BOMB_NB_MAZE; i++ )
    {
        if( bombWinner[ i ] == 0 )
          continue;
        gbDrawBitmap( 22 + ( 6 * i ), 6 + ( 8 * bombWinner[ i ] ), bombMiniCoupeBitmap );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
    {
        // Fixed real off-by-one - see this file's own header comment.
        if( bombCurrentLevel < BOMB_NB_MAZE - 1 )
          bombStateGame = BOMB_STATE_START;
        else
          bombStateGame = BOMB_STATE_GAMEOVER;
    }

    if( gbPressed( BTN_C ) )
      bombStateGame = BOMB_STATE_GAMEOVER;
}

void bombEndGame()
{
    bombDrawMaze();
    bombDrawPlayers();
    bombDrawBombes();

    gbSetColor( GB_WHITE );
    gbFillRect( 0, 0, bombTimerAnimEndGame * 2, LCDHEIGHT );
    gbFillRect( LCDWIDTH - bombTimerAnimEndGame * 2, 0, bombTimerAnimEndGame * 2, LCDHEIGHT );
    gbSetColorBg( GB_BLACK, GB_WHITE );
    gbCursorX = 12;
    gbCursorY = 1;
    // Real upstream's own "GAME OVER!" print call is commented out in the
    // real source too - no text is meant to appear here, just the real
    // white wipe-in animation over the frozen final gameplay frame.
    bombTimerAnimEndGame++;
    if( bombTimerAnimEndGame == ( LCDWIDTH / 4 ) + 10 )
      bombStateGame = BOMB_STATE_SCORE;
}

// -----------------------------------------------------------------------------
// Title/reset (titleScreen.ino) - see this file's own header comment on the
// real menu collapse.
// -----------------------------------------------------------------------------

void bombInitScore()
{
    int i;
    for( i = 0; i < BOMB_NB_MAZE; i++ )
      bombWinner[ i ] = 0;
}

void bombResetGame()
{
    bombCurrentLevel = -1;
    bombStateGame = BOMB_STATE_TITLE;
}

void bombInitGame()
{
    int i;

    bombTimerAnimEndGame = 0;
    bombP1StartPos();
    bombM1StartPos();
    bombM2StartPos();
    bombDistExplosion = BOMB_DIST_EXPLOSION;

    for( i = 0; i < BOMB_NB_BOMBE; i++ )
    {
        bombMasterBombe[ i ].isAlive = false;
        bombMonstreBombe[ i ].isAlive = false;
    }
}

void bombGoTitleScreen()
{
    bombResetGame();
    bombInitGame();
}

void bombUpdateTitle()
{
    gbDrawBitmap( 0, 0, bombTitleScreenImage );

    gbCursorX = 30;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      bombStateGame = BOMB_STATE_START;
}

// -----------------------------------------------------------------------------
// Top-level dispatch (Bomber.ino's own loop(), network branches removed)
// -----------------------------------------------------------------------------

void gameBomber_init()
{
    gbBegin();
    bombInitScore();
    bombGoTitleScreen();
}

void gameBomber_update()
{
    if( !gbUpdate() ) return;

    if( bombStateGame == BOMB_STATE_TITLE )
    {
        bombUpdateTitle();
    }
    else if( bombStateGame == BOMB_STATE_PLAY )
    {
        if( gbPressed( BTN_C ) )
          bombStateGame = BOMB_STATE_BACK_TO_TITLE;

        bombUpdatePlayer( &bombMasterPlayer );
        bombUpdateMonstres();
        bombUpdatePlayerAll( &bombMasterPlayer );
        bombUpdatePlayerAll( &bombMonstre1 );
        bombUpdatePlayerAll( &bombMonstre2 );
        bombUpdateBombes();

        bombDrawMaze();
        bombDrawPlayers();
        bombDrawBombes();

        // Real win/loss condition re-derived using only masterPlayer/
        // monstre1/monstre2 - see this file's own header comment (upstream's
        // own real `slavePlayer` win branch was already permanently dead in
        // single-player mode on real hardware too, since `isSingle` is
        // always true there).
        if( !bombMasterPlayer.isAlive || ( !bombMonstre1.isAlive && !bombMonstre2.isAlive ) )
        {
            if( bombMasterPlayer.isAlive )
              bombWinner[ bombCurrentLevel ] = 1;
            else if( bombMonstre1.isAlive && bombMonstre2.isAlive )
              bombWinner[ bombCurrentLevel ] = 3 + arand( 2 );
            else if( bombMonstre1.isAlive )
              bombWinner[ bombCurrentLevel ] = 3;
            else if( bombMonstre2.isAlive )
              bombWinner[ bombCurrentLevel ] = 4;

            bombStateGame = BOMB_STATE_ENDGAME_ANIM;
        }
    }
    else if( bombStateGame == BOMB_STATE_ENDGAME_ANIM )
    {
        bombEndGame();
    }
    else if( bombStateGame == BOMB_STATE_SCORE )
    {
        bombScoreScreen();
    }
    else if( bombStateGame == BOMB_STATE_START )
    {
        bombInitGame();
        bombStateGame = BOMB_STATE_PLAY;
        bombCurrentLevel++;
        bombLoadMazeByNumero( bombCurrentLevel );
    }
    else if( bombStateGame == BOMB_STATE_GAMEOVER )
    {
        bombGameOverScreen();
    }
    else if( bombStateGame == BOMB_STATE_BACK_TO_TITLE )
    {
        bombGoTitleScreen();
    }

    gbRenderFrame();
}
