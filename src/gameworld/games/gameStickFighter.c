#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// StickFighter (Clement83, license none specified - the same author as
// this project's own already-shipped Copter/GlaciGlaca/CrazyTown, confirmed
// directly via the staged copy's own .git/config remote URL,
// github.com/Clement83/StickFighter). Art by Quirby64. A real one-on-one
// stick-figure fighting game: move/jump/duck/punch/kick, land a real 3-hit
// "AYOUKEN" fireball combo (Down, Forward, A), knock the opponent's life bar
// to zero to win a round, first to 3 round wins takes the match.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameStickFighter.c (which already did the real upstream-
// Arduino-to-Vircon32-dialect porting work - the multiplayer drop, the
// blocking titleScreen()->explicit-state conversion, the struct-by-value
// parameter rewrite, the sound/particle-system port, every real preserved
// upstream quirk - see the rest of this header comment below for the full
// original write-up, none of which needed revisiting here) - converting
// Vircon32's own C dialect back to standard C:
//  - `int[N] name;`/`int[N] name = {...};` -> `int name[N];`/`int
//    name[N] = {...};` (array-declaration order) for every real bitmap
//    table (genuinely numeric byte data, not text, so it stays `int[]`),
//    including the multi-dimensional `int[3][8] sfgtSoundFx` table
//    (-> `int sfgtSoundFx[3][8]`, manually diffed by eye rather than via
//    the array-count diff script).
//  - `int*[4] sfgtTabCpt`/`int*[2] sfgtMenuTab` -> `int* sfgtTabCpt[4]`/
//    `int* sfgtMenuTab[2]` (array-of-pointer declaration order).
//  - `struct SfgtAnimSprite { ... };`/`struct SfgtAyouken { ... };`/
//    `struct SfgtFighter { ... };`/`struct SfgtPart { ... };` -> `typedef
//    struct { ... } SfgtAnimSprite;` etc, so every existing `SfgtFighter*`
//    pointer parameter and plain `SfgtFighter sfgtPlayer1;`-style
//    declaration works without a `struct` keyword at any use site, exactly
//    as this file's own function signatures already assumed. Member arrays
//    inside those structs got the same array-declaration-order fix:
//    `SfgtAnimSprite[SFGT_NB_STATE] sprites;` -> `SfgtAnimSprite
//    sprites[SFGT_NB_STATE];`, `int[SFGT_NB_MOVE_SAVE] combo;` -> `int
//    combo[SFGT_NB_MOVE_SAVE];`, and `SfgtPart[SFGT_PART_MAX]
//    sfgtParticles;` -> `SfgtPart sfgtParticles[SFGT_PART_MAX];`.
//  - `int[2] sfgtArrowGlyphText = { 16, 0 };` (the real "\20" icon-glyph
//    text passed straight to `gbPrintString()`) -> `char
//    sfgtArrowGlyphText[2] = { 16, 0 };`, matching this shim's own real
//    `gbPrintString(char* text)` signature and this project's own
//    established text-array convention (see gameSenet.c's own identical
//    icon-glyph-array treatment).
// Every real bitmap/sound data table's own byte values were copied
// directly from the source file, not retyped from memory, and diff-
// verified byte-for-byte against it afterward (see this project's own
// CLAUDE.md) - the one 2D table (`sfgtSoundFx`) was verified by direct
// eye-diff instead, since the verification script does not handle
// multi-dimensional arrays.
//
// UPSTREAM IS 8 REAL `.ino` TABS PLUS `part.h` - all read in full before
// writing this port: `StickFighter.ino` (globals/state machine/loop()),
// `Player.ino` (fighter physics/attacks/the real solo AI), `arena.ino`
// (round timer/KO-banner/background), `mainMenu.ino` (the 3-item bitmap
// picker), `finalScreen.ino` (match-end win/lose screen),
// `titleScreen.ino` (boot logo + the real attract-mode credits screen),
// `part.ino`/`part.h` (a tiny hit-spark particle system by "valden"), and
// `master.ino`/`slave.ino` (the real two-cartridge `Wire.h` I2C multiplayer
// protocol) - read completely specifically to confirm neither one defines
// any shared helper the kept files also call; both are pure `Wire.h`
// read/write marshalling with no such overlap.
//
// MULTIPLAYER DROPPED ENTIRELY, PER THIS PROJECT'S OWN ESTABLISHED "DROP
// THE HARDWARE-SPECIFIC MODE, KEEP THE HARDWARE-INDEPENDENT ONE" PRECEDENT
// (gameBRally.c's own accelerometer-vs-digital-button fallback,
// gameSenet.c's own I2C-multiplayer removal): there is exactly one emulated
// cartridge here, so real `Wire.h` two-cartridge play can never run. NOT
// PORTED AT ALL: `master.ino`/`slave.ino` in full (`setupMaster`/
// `setupSlave`/`updateMaster`/`updateSlave`/`masterRead`/`masterWrite`/
// `requestEvent`/`receiveEvent`), `mainMenu.ino`'s own `multiPlayerMenu[2]`
// Host/Join array and `StickFighter.ino`'s own real `stateGame==1`
// ("multiplayer menu", `gb.menu(multiPlayerMenu,2)`) branch, and
// `Player.ino`'s own `movePlayerSlave()`/`updatePlayerSalve()` plus the
// `bt_up`/`bt_down`/`bt_left`/`bt_right`/`bt_a`/`bt_b` "fake buttons for
// slave" globals - none of it has any real caller left once the Host/Join
// menu entry is gone. `mainMenu.ino`'s own real 3-item `tabSpriteMenu[3] =
// {menuSolo, menuMulti, menuOption}` picker drops `menuMulti`, becoming a
// 2-item `sfgtMenuTab[2] = {sfgtMenuSoloBitmap, sfgtMenuOptionBitmap}`
// (`SFGT_INDEX_MAX_ITEM_MENU` 2 -> 1); the carousel draw formula itself
// (`sfgtDrawMainMenu()`) is upstream's own real modulo-style "left/center/
// right" formula, unmodified - it already produces the correct result for
// a 2-item menu with no special-casing needed (both side-preview slots
// naturally land on the one non-focused item).
//
// `isMaster`/`isOnePlayer`/`isPaused`/`disconnected`/`slave_updated` -
// REMOVED AS VARIABLES, NOT JUST LEFT UNUSED. Once the multiplayer menu
// entry is gone, `isOnePlayer` is always true and `isMaster` is always
// irrelevant, so every real upstream branch that tested them collapses to
// its one now-permanent outcome and is deleted outright, per this
// project's own "delete the dead side, don't leave an unreachable branch"
// rule: `if(isMaster && !isOnePlayer) updateMaster();` (dead, both real
// call sites in `loop()`), `if(!isMaster && !isOnePlayer){
// updatePlayerSalve(); updateSlave(); }` (dead), `if(!isPaused ||
// isOnePlayer || !isMaster)` (always true - the whole body always runs
// unconditionally now), `if(isMaster || isOnePlayer)` (always true),
// `if(isOnePlayer) moveIAPlayer(&Player2,&Player1);` (always runs - kept,
// unconditionally, as `sfgtMoveIAPlayer()`'s own permanent call site -
// Player2 is fully AI-controlled in every remaining game mode).
// `Player.ino`'s own `gestionAttack()`/`updatePlayer()` never reference
// these flags at all, so they needed no changes here. `finalScreen.ino`'s
// own real win-banner test, `((isMaster||isOnePlayer) &&
// Player1.cptVictory==3) || (!isMaster && !isOnePlayer &&
// Player2.cptVictory==3)`, reduces the same way to plain
// `sfgtPlayer1.cptVictory==3` (the second clause needs `!isOnePlayer`,
// permanently false) - the human player is always Player1 in every
// remaining mode, so "did the human win" is just "did Player1 reach 3".
//
// `stateGame==2` ("OPTION" PER UPSTREAM'S OWN HEADER COMMENT) IS A REAL
// AI-VS-AI ATTRACT/CREDITS SCREEN, NOT AN OPTIONS MENU - confirmed by
// reading the full branch directly rather than trusting the comment:
// `loop()`'s own `else if(stateGame==2)` calls `moveIAPlayer(&Player2,
// &Player1); moveIAPlayer(&Player1,&Player2); updatePlayer(); updateArena();
// drawPlayer(); drawArena(); credit();` - both fighters are AI-driven and
// `credit()` scrolls a real "Design by Quirby64 / Programme by Clement"
// credits block over the fight in progress. It has no real multiplayer
// dependency at all, so it's ported here too, reachable exactly like
// upstream: selecting the menu's remaining "option" (now index 1) entry.
// `updatePlayer()`'s own real `if(stateGame!=2) movePlayer(&Player1);`
// guard is preserved unmodified - it's what keeps human input from ever
// reaching Player1 while this demo runs.
//
// BLOCKING `gb.titleScreen(bitmap)` -> EXPLICIT STATE, matching this
// project's own established treatment (gamePong.c's `PONG_STATE_TITLE`,
// gameArmageddon.c's `ARMA_STATE_TITLE`): a new `SFGT_GAME_TITLE` state
// (not a real upstream `stateGame` value - upstream's title is a genuine
// blocking library call outside its own state machine entirely) shows the
// real `sfStartMenu` logo at real hardware's own fixed `(0,12)` anchor
// (confirmed directly against the real `Gamebuino::titleScreen()` source
// during this project's earlier `gameArmageddon.c`/`gameFlappyBirdo.c`/
// `gameUfoRace.c` ports of that same real function) plus this port's own
// added "PRESS A" prompt (real `titleScreen()` draws no text of its own -
// every other ported game restoring it needed the same addition).
// Upstream's own real Button-C "quit to title" gesture
// (`if(gb.buttons.pressed(BTN_C)) goTitleScreen();`, checked unconditionally
// every tick before the `stateGame` dispatch) is preserved as a genuine
// mid-game reset to this same title state from any other state. One small,
// documented timing deviation: real `goTitleScreen()` calls the blocking
// `gb.titleScreen()` (waits for A right there) and only then calls
// `initGame()` - by the time `loop()` reaches its own `if(stateGame==3)`
// check the same tick, the menu is already showing. This port's own
// non-blocking version instead shows the title for at least one real
// tick and only advances to the menu on a later tick's fresh A-press -
// imperceptible in practice, the same one-or-two-frame state-machine
// deviation already accepted for every other ported game's own
// de-blocked `titleScreen()`/`gb.menu()` call.
//
// REAL BITMAP ART, RESTORED FROM THE FIRST PASS: all 42 real upstream
// PROGMEM bitmaps (every fighter animation frame for both P1 and the
// mirrored P2 palette, the fireball/AYOUKEN sprites, the arena background,
// the "3,2,1,FIGHT!"/"TIME UP"/KO banners, both menu-carousel bitmaps, all
// 4 win/lose/final-screen bitmaps, and the title logo) were extracted
// byte-for-byte via a small script that parsed each real
// `const byte NAME[] PROGMEM = {...}` array directly out of the real `.ino`
// sources and counted its own real element count automatically (rather
// than hand-counting ~40 arrays, several hundreds of bytes long) - every
// array already matches this shim's own `gbDrawBitmap()`/
// `gbDrawBitmapRotated()` format exactly as shipped (`{width, height,
// row-major MSB-first packed bytes...}` - the same real Gamebuino bitmap
// layout, needing no reformatting, only the hex bytes copied verbatim and
// the declaration syntax converted to this dialect's own `int[N] name =
// {...};`). Every real `gb.display.drawBitmap(x,y,bmp,0,dir)` call site
// (used throughout for both fighters' own left/right mirroring) is a
// direct `gbDrawBitmapRotated(x,y,bmp,0,dir)` call - real upstream's own
// `NOFLIP`/`FLIPH` values (0/1) already match this shim's own `flip`
// parameter numbering exactly (`SFGT_NOFLIP 0`/`SFGT_FLIPH 1` below), so
// `player->dir` is passed straight through with no translation needed, the
// same real numeric coincidence gameFlappyBirdo.c/gameSimonbuino.c already
// relied on for their own restored rotated-bitmap art.
//
// DIALECT NOTE - STRUCT-BY-VALUE PARAMETERS REWRITTEN TO POINTERS. Real
// upstream passes a whole `Figther` by value twice
// (`playerIsAttack(Figther player)`, called as `playerIsAttack(Player1)`/
// `playerIsAttack(*player)`) - this dialect rejects passing (or returning)
// any struct/union larger than one word (confirmed directly against this
// project's own toolchain: "functions cannot pass arguments of size > 1"),
// and `SfgtFighter` is many words wide. `sfgtPlayerIsAttack()` here takes
// `SfgtFighter*` instead, with every real call site changed from
// `playerIsAttack(Player1)`/`playerIsAttack(*player)` to
// `sfgtPlayerIsAttack(&sfgtPlayer1)`/`sfgtPlayerIsAttack(player)` (`player`
// is already a pointer at every one of those call sites) - a pure calling-
// convention change, zero behavioral difference, since the function's own
// body only ever reads two fields.
//
// SOUND - ONE-SHOT-TONE APPROXIMATION, an already-established scope limit
// (see this project's own CLAUDE.md, and this same author's own already-
// shipped gameCopter.c for the identical treatment): real
// `playsoundfx(fxno, channel)` drives real `gb.sound.command()` calls to
// shape volume/waveform/volume-slide/pitch-slide *before*
// `gb.sound.playNote()` - this shim only implements one-shot tones
// (`gbPlayNote`/`gbPlayTick`/`gbPlayOK`/`gbPlayCancel`), no tracker/command
// system. `sfgtPlaysoundfx(fxno)` calls `gbPlayNote()` directly with the
// real table's own pitch/duration values only (`sfgtSoundFx[fxno][1]`/
// `[7]`), dropping the volume/waveform/slide shaping and the per-call
// `channel` parameter (always 0 upstream anyway - this shim has no
// multi-channel concept). Real upstream's own third sound effect (index 2,
// "mort selon jerom" - a death/KO stinger) is preserved and used exactly
// where upstream calls it (`gestionAttack()`, the instant a defeated
// fighter's life first reaches 0).
//
// BYTE WRAPAROUND, NOT REPRODUCED - a whole-project simplification already
// documented once for the whole codebase (see gameCastleDefence.c's own
// header comment for the canonical write-up), not specific to this game.
// One real, visible consequence here: upstream's own attract-mode
// `offsetCredit` is a real `byte`, decremented every tick `credit()` runs
// with no clamp of any kind - on real hardware this wraps from 0 back to
// 255 every 256 ticks, making the credits block loop indefinitely while
// the attract-mode demo plays. `sfgtOffsetCredit` here is a plain `int`
// (this project's own established `byte -> int` conversion), so it just
// keeps decrementing without ever wrapping - the credits scroll up once
// and then stay off-screen for the rest of that attract-mode session
// instead of looping. `sfgtOffsetCredit` is still reset to its real
// initial value of 50 every time the title screen is (re)entered
// (matching real `goTitleScreen()`'s own real assignment), so a fresh
// attract-mode session (reached via Button C, or the very first boot)
// always shows the credits at least once.
//
// PARTICLE SYSTEM (`part.ino`/`part.h`, credited upstream to "valden") -
// a small, genuinely single-player-compatible hit-spark effect (10 slots,
// simple gravity/velocity/lifetime), spawned by `gestionAttack()` on every
// successful hit. Ported directly: the real `partDraw(Gamebuino &gbu, ...)`
// signature takes a C++ reference to the single real `gb` instance purely
// so it can call `gbu.display.drawPixel(...)` - this dialect has no
// references and no `gb` object to pass in the first place (see this
// shim's own header comment), so `sfgtPartDraw()` drops that parameter
// entirely and calls `gbDrawPixel()` directly, keeping only the real
// `xOffset`/`yOffset` parameters (always called here as `sfgtPartDraw(0,
// 0)`, matching every real upstream call site).
//
// Every other real `byte`/`boolean` field became plain `int`/`bool`
// (this project's own established convention); `random(a,b)` became this
// project's own established `a + arand(b-a)` conversion (matching real
// Arduino `random(min,max)`'s own half-open `[min,max)` range exactly -
// e.g. `punchFigther()`'s own real `random(3,5)` becomes `3 + arand(2)`,
// still only ever producing 3 or 4); `pgm_read_byte(bmp)`/
// `pgm_read_byte(bmp+1)` (reading a bitmap's own real width/height header
// bytes back out, in `changeBoundPlayer()`) became plain `bmp[0]`/`bmp[1]`
// indexing, since PROGMEM is a real no-op here and every bitmap is already
// a plain `int[]`. `abs(float)` (real Arduino's own generic ternary-macro
// `abs()`, called on a float difference in `moveIAPlayer()`'s own real
// `dist` calculation) became `(int)fabs(...)` (`math.h`'s `fabs()`,
// already proven working directly by this same author's own gameCopter.c/
// gameCrazyTown.c/gameHexagon.c ports). No ternary operator exists in this
// dialect, so every real `a?b:c` expression upstream used (`(pAttack->
// isJump)? 0 : 12`-style offsets, the ayouken sprite-frame pick, the menu
// carousel's own wraparound index picks) became an explicit if/else
// assigning a local variable first, then using that variable - the exact
// same, zero-behavioral-difference rewrite already used by every other
// ported game with a `?:` in its own upstream source.
//
// `Player1.oldLife`/`int8_t oldLife` - kept as a real, genuinely UNUSED
// upstream struct field (assigned once at init, never read anywhere in the
// real source - confirmed directly, not assumed) purely for 1:1 structural
// fidelity against the real `Figther` struct layout; harmless either way.
//
// No `setFont()`/`setColor()`/`GRAY`/`INVERT` call exists anywhere in the
// real upstream source (confirmed directly by grepping every `.ino` file) -
// this port never calls `gbSetFont()`/`gbSetColor()` either, so every draw
// stays on this shim's own real default font (`gbFont3x5`) and default
// color (`GB_BLACK`), exactly matching real hardware's own untouched
// defaults for this particular game.

// -----------------------------------------------------------------------------
//   Constants
// -----------------------------------------------------------------------------

#define SFGT_NB_STATE 11
#define SFGT_NB_SPRITE_STATE 2
#define SFGT_TIME_ATTACK 2
#define SFGT_SPEED_RUN 3
#define SFGT_GROUND_Y 42
#define SFGT_TIME_DEF 2
#define SFGT_NB_MOVE_SAVE 4
#define SFGT_TIME_LIVE_AYOUKEN 15
#define SFGT_VITT_AYOUKEN 3
#define SFGT_DAMAGE_AYOUKEN 10
#define SFGT_TIME_FALL 5
#define SFGT_CPT_COMBAT_INIT 30
#define SFGT_SEUIL_MIN_MOVE 0.1

// Matches this shim's own gbDrawBitmapRotated() flip parameter numbering
// exactly (0=none, 1=horizontal) - see this file's own header comment.
#define SFGT_NOFLIP 0
#define SFGT_FLIPH 1

// Real upstream currentState values (StickFighter.ino's own comment):
// IDL:0, run:1, kick:2, punchLeft:3, punchRight:4, duck:5, duckKick:6,
// jump:7, jumpKick:8, dead:9, fire(ayouken pose):10.
#define SFGT_ST_IDLE 0
#define SFGT_ST_RUN 1
#define SFGT_ST_KICK 2
#define SFGT_ST_PUNCH_LEFT 3
#define SFGT_ST_PUNCH_RIGHT 4
#define SFGT_ST_DUCK 5
#define SFGT_ST_DUCK_KICK 6
#define SFGT_ST_JUMP 7
#define SFGT_ST_JUMP_KICK 8
#define SFGT_ST_DEAD 9
#define SFGT_ST_FIRE 10

// Real upstream stateGame values (StickFighter.ino's own real comment,
// minus the dropped "1 multiplayer menu" entry) plus this port's own
// SFGT_GAME_TITLE addition (see this file's own header comment).
#define SFGT_GAME_PLAY 0
#define SFGT_GAME_ATTRACT 2
#define SFGT_GAME_MENU 3
#define SFGT_GAME_FINAL 4
#define SFGT_GAME_TITLE 5

#define SFGT_INDEX_MAX_ITEM_MENU 1

#define SFGT_PART_MAX 10
#define SFGT_PART_GRAV_X 0
#define SFGT_PART_GRAV_Y 6
#define SFGT_PART_LIFETIME 20

// -----------------------------------------------------------------------------
//   Real bitmap art (see this file's own header comment)
// -----------------------------------------------------------------------------

int sfgtIdle1Bitmap[14] =
{
    8, 12, 0x78, 0x78, 0x78, 0x20, 0x70, 0xA8, 0xEC, 0x20, 0x20, 0x50, 0x48, 0x6C
};

int sfgtIdle2Bitmap[14] =
{
    8, 12, 0x0, 0x78, 0x78, 0x78, 0x20, 0x70, 0xA8, 0xEC, 0x20, 0x50, 0x48, 0x6C
};

int sfgtRun1Bitmap[14] =
{
    8, 12, 0x78, 0x78, 0x78, 0x70, 0xA8, 0xEC, 0x20, 0x20, 0x24, 0xDC, 0x80, 0x0
};

int sfgtRun2Bitmap[14] =
{
    8, 12, 0x78, 0x78, 0x78, 0x20, 0x70, 0xA8, 0xEC, 0x20, 0x20, 0x20, 0x20, 0x30
};

int sfgtKick1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x78, 0x0, 0x78, 0x0, 0x30, 0x0, 0x7C, 0x0, 0xA0, 0x0, 0xE0, 0x10, 0x20, 0xF0, 0x3F, 0x0, 0x40, 0x0, 0x80, 0x0, 0xC0, 0x0
};

int sfgtPunchLeft1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x78, 0x0, 0x78, 0x0, 0x20, 0x0, 0x7F, 0xF0, 0xA0, 0x0, 0xE0, 0x0, 0x20, 0x0, 0x20, 0x0, 0x50, 0x0, 0x48, 0x0, 0x6C, 0x0
};

int sfgtPunchRight1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x78, 0x0, 0x78, 0x0, 0x20, 0x0, 0x60, 0x0, 0xFF, 0x80, 0x2C, 0x0, 0x20, 0x0, 0x20, 0x0, 0x50, 0x0, 0x48, 0x0, 0x6C, 0x0
};

int sfgtDuck1Bitmap[8] =
{
    8, 6, 0x78, 0x78, 0x20, 0xFC, 0x48, 0x6C
};

int sfgtDuckKick1Bitmap[14] =
{
    16, 6, 0x78, 0x0, 0x78, 0x0, 0x20, 0x0, 0xFC, 0x0, 0x48, 0x0, 0x47, 0xC0
};

int sfgtJump1Bitmap[14] =
{
    8, 12, 0x78, 0x78, 0x78, 0x24, 0x24, 0x78, 0xA0, 0xA0, 0x38, 0x44, 0x84, 0x0
};

int sfgtJumpKick1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x78, 0x0, 0x78, 0x0, 0x24, 0x0, 0x24, 0x0, 0x78, 0x0, 0xA0, 0x0, 0xA0, 0x0, 0x3F, 0xE0, 0x40, 0x0, 0x80, 0x0, 0x0, 0x0
};

int sfgtDead1Bitmap[10] =
{
    16, 4, 0xE0, 0x0, 0xE0, 0x0, 0xEF, 0x80, 0xFC, 0x70
};

int sfgtP2Idle1Bitmap[14] =
{
    8, 12, 0x78, 0x58, 0x78, 0x20, 0x70, 0xA8, 0xEC, 0x20, 0x20, 0x50, 0x48, 0x6C
};

int sfgtP2Idle2Bitmap[14] =
{
    8, 12, 0x0, 0x58, 0x78, 0x78, 0x20, 0x70, 0xA8, 0xEC, 0x20, 0x50, 0x48, 0x6C
};

int sfgtP2Run1Bitmap[14] =
{
    8, 12, 0x78, 0x58, 0x78, 0x70, 0xA8, 0xEC, 0x20, 0x20, 0x24, 0xDC, 0x80, 0x0
};

int sfgtP2Run2Bitmap[14] =
{
    8, 12, 0x78, 0x58, 0x78, 0x20, 0x70, 0xA8, 0xEC, 0x20, 0x20, 0x20, 0x20, 0x30
};

int sfgtP2JumpKick1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x58, 0x0, 0x78, 0x0, 0x24, 0x0, 0x24, 0x0, 0x78, 0x0, 0xA0, 0x0, 0xA0, 0x0, 0x3F, 0xE0, 0x40, 0x0, 0x80, 0x0, 0x0, 0x0
};

int sfgtP2PunchLeft1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x58, 0x0, 0x78, 0x0, 0x20, 0x0, 0x7F, 0xF0, 0xA0, 0x0, 0xE0, 0x0, 0x20, 0x0, 0x20, 0x0, 0x50, 0x0, 0x48, 0x0, 0x6C, 0x0
};

int sfgtP2PunchRight1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x58, 0x0, 0x78, 0x0, 0x20, 0x0, 0x60, 0x0, 0xFF, 0x80, 0x2C, 0x0, 0x20, 0x0, 0x20, 0x0, 0x50, 0x0, 0x48, 0x0, 0x6C, 0x0
};

int sfgtP2Duck1Bitmap[8] =
{
    8, 6, 0x78, 0x58, 0x20, 0xFC, 0x48, 0x6C
};

int sfgtP2DuckKick1Bitmap[14] =
{
    16, 6, 0x78, 0x0, 0x58, 0x0, 0x20, 0x0, 0xFC, 0x0, 0x48, 0x0, 0x47, 0xC0
};

int sfgtP2Jump1Bitmap[14] =
{
    8, 12, 0x78, 0x58, 0x78, 0x24, 0x24, 0x78, 0xA0, 0xA0, 0x38, 0x44, 0x84, 0x0
};

int sfgtP2Kick1Bitmap[26] =
{
    16, 12, 0x78, 0x0, 0x58, 0x0, 0x78, 0x0, 0x30, 0x0, 0x7C, 0x0, 0xA0, 0x0, 0xE0, 0x10, 0x20, 0xF0, 0x3F, 0x0, 0x40, 0x0, 0x80, 0x0, 0xC0, 0x0
};

int sfgtP2Dead1Bitmap[10] =
{
    16, 4, 0xE0, 0x0, 0xE0, 0x0, 0xAF, 0x80, 0xFC, 0x70
};

int sfgtFire1Bitmap[15] =
{
    8, 13, 0x0, 0xF0, 0xF0, 0xF0, 0x48, 0x78, 0x40, 0x78, 0x48, 0x40, 0xA0, 0xA0, 0xF0
};

int sfgtFireBall1Bitmap[8] =
{
    8, 6, 0xF8, 0x7C, 0xCC, 0xCC, 0x7C, 0xF8
};

int sfgtFireBall2Bitmap[8] =
{
    8, 6, 0x38, 0xC4, 0xB4, 0xB4, 0xC4, 0x38
};

int sfgtAreneBitmap[530] =
{
    88, 48, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x62, 0x7, 0xFF, 0xFF, 0xFC, 0x3, 0xFF, 0xFF, 0xFE, 0xC, 0xE0, 0x56, 0x4, 0x4, 0x4, 0x4, 0x2, 0x2, 0x2, 0x2, 0xA, 0x20, 0x62, 0x4, 0x4, 0x4, 0x4, 0x2, 0x2, 0x2, 0x2, 0xC, 0xE0, 0x42, 0x4, 0x4, 0x4, 0x4, 0x2, 0x2, 0x2, 0x2, 0x8, 0x80, 0x47, 0x7, 0xFF, 0xFF, 0xFC, 0x3, 0xFF, 0xFF, 0xFE, 0x8, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x31, 0x8C, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x18, 0xC0, 0x4A, 0x52, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0xA5, 0x20, 0x4A, 0x52, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0xA5, 0x20, 0x31, 0x8C, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x18, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0xD5, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x50, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0
};

int sfgtCptBold1Bitmap[14] =
{
    8, 12, 0x18, 0x28, 0x48, 0xA8, 0xE8, 0xE8, 0x28, 0x28, 0xEC, 0x84, 0xFC, 0xFC
};

int sfgtCptBold2Bitmap[14] =
{
    8, 12, 0xFC, 0x84, 0xF4, 0xF4, 0xF4, 0x84, 0xBC, 0xBC, 0xBC, 0x84, 0xFC, 0xFC
};

int sfgtCptBold3Bitmap[14] =
{
    8, 12, 0xFC, 0x84, 0xF4, 0xF4, 0xF4, 0x84, 0xF4, 0xF4, 0xF4, 0x84, 0xFC, 0xFC
};

int sfgtFightBitmap[50] =
{
    32, 12, 0xFD, 0xF7, 0xEF, 0xDF, 0x85, 0x14, 0x2B, 0x51, 0xBD, 0xB5, 0xEB, 0x5B, 0xBD, 0xB5, 0xEB, 0x5B, 0xBC, 0xA5, 0xB, 0x4A, 0x84, 0xA5, 0xE8, 0x4A, 0xBC, 0xA5, 0x2B, 0x4A, 0xBC, 0xA5, 0xAB, 0x4A, 0xA0, 0xA5, 0xAB, 0x4A, 0xA1, 0x14, 0x2B, 0x4A, 0xE1, 0xF7, 0xEF, 0xCE, 0xE1, 0xF7, 0xEF, 0xCE
};

int sfgtTimeUpBitmap[74] =
{
    48, 12, 0xFB, 0xEF, 0xDF, 0x80, 0x7E, 0xFC, 0x8A, 0x28, 0x50, 0x80, 0x5A, 0x84, 0xDB, 0x68, 0x57, 0x80, 0x5A, 0xB4, 0xDB, 0x68, 0x57, 0x80, 0x5A, 0xB4, 0x51, 0x4B, 0x57, 0x80, 0x5A, 0x84, 0x51, 0x4B, 0x50, 0x80, 0x5A, 0xBC, 0x51, 0x4B, 0x57, 0x80, 0x5A, 0xBC, 0x51, 0x4B, 0x57, 0x80, 0x5A, 0xA0, 0x51, 0x4B, 0x57, 0x80, 0x5A, 0xA0, 0x52, 0x2B, 0x50, 0x80, 0x42, 0xA0, 0x73, 0xEF, 0xDF, 0x80, 0x7E, 0xE0, 0x73, 0xEF, 0xDF, 0x80, 0x7E, 0xE0
};

int sfgtP1koBitmap[62] =
{
    40, 12, 0xF0, 0x30, 0x7, 0xEF, 0xC0, 0x88, 0x50, 0x5, 0xA8, 0x40, 0xB4, 0x90, 0x5, 0xAB, 0x40, 0xB5, 0x50, 0x5, 0xAB, 0x40, 0xB5, 0xD0, 0x5, 0xAB, 0x40, 0x8D, 0xD0, 0x4, 0x6B, 0x40, 0xB8, 0x50, 0x5, 0xAB, 0x40, 0xB0, 0x50, 0x5, 0xAB, 0x40, 0xA1, 0xD8, 0x5, 0xAB, 0x40, 0xA1, 0x8, 0x5, 0xA8, 0x40, 0xE1, 0xF8, 0x7, 0xEF, 0xC0, 0xE1, 0xF8, 0x7, 0xEF, 0xC0
};

int sfgtP2koBitmap[62] =
{
    40, 12, 0xF1, 0xF8, 0x7, 0xEF, 0xC0, 0x89, 0x8, 0x5, 0xA8, 0x40, 0xB5, 0xE8, 0x5, 0xAB, 0x40, 0xB5, 0xE8, 0x5, 0xAB, 0x40, 0xB5, 0xE8, 0x5, 0xAB, 0x40, 0x8D, 0x8, 0x4, 0x6B, 0x40, 0xB9, 0x78, 0x5, 0xAB, 0x40, 0xB1, 0x78, 0x5, 0xAB, 0x40, 0xA1, 0x78, 0x5, 0xAB, 0x40, 0xA1, 0x8, 0x5, 0xA8, 0x40, 0xE1, 0xF8, 0x7, 0xEF, 0xC0, 0xE1, 0xF8, 0x7, 0xEF, 0xC0
};

int sfgtMenuOptionBitmap[242] =
{
    48, 40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x10, 0x93, 0x3B, 0x92, 0x98, 0x0, 0x10, 0xAA, 0x91, 0x2A, 0xA0, 0x0, 0x10, 0xAB, 0x11, 0x2B, 0x90, 0x0, 0x10, 0xAA, 0x11, 0x2B, 0x88, 0x0, 0x10, 0x92, 0x13, 0x92, 0xB0, 0x0, 0x10, 0x80, 0x0, 0x0, 0x0, 0x1C, 0x10, 0x80, 0x0, 0x0, 0x0, 0x1C, 0x10, 0x80, 0x0, 0x0, 0x7, 0x3E, 0x70, 0x80, 0x0, 0x0, 0x7, 0xFF, 0xF0, 0x80, 0x0, 0x0, 0x7, 0xFF, 0xF0, 0x80, 0x0, 0x0, 0x3, 0xBE, 0xF0, 0x80, 0x0, 0x0, 0x3, 0xFF, 0xF0, 0x80, 0x3, 0x8E, 0x7, 0xFF, 0xF0, 0x80, 0x3, 0x8E, 0x1F, 0xE3, 0xF0, 0x80, 0x3, 0xFE, 0x1F, 0xE3, 0xF0, 0x80, 0x3, 0xFE, 0x1F, 0xE3, 0xF0, 0x80, 0x7, 0xFF, 0x7, 0xFF, 0xF0, 0x80, 0x7F, 0xFF, 0xF3, 0xFF, 0xF0, 0x80, 0x7D, 0xFD, 0xF3, 0xBE, 0xF0, 0x80, 0x7F, 0xFF, 0xF7, 0xFF, 0xF0, 0x8E, 0x1F, 0x8F, 0xC7, 0xFF, 0xF0, 0x9E, 0x1F, 0x8F, 0xC7, 0x3E, 0x70, 0xFE, 0x7F, 0xFF, 0xF0, 0x1C, 0x10, 0xFC, 0x7F, 0xFF, 0xF0, 0x1C, 0x10, 0xFE, 0x7D, 0xFD, 0xF0, 0x0, 0x10, 0xFF, 0xF, 0xFF, 0x80, 0x0, 0x10, 0xDF, 0x7, 0xFF, 0x0, 0x0, 0x10, 0xFF, 0xF3, 0xFE, 0x0, 0x0, 0x10, 0xFF, 0xF3, 0xFE, 0x0, 0x0, 0x10, 0xFF, 0xF3, 0x8E, 0x0, 0x0, 0x10, 0xFF, 0x83, 0x8E, 0x0, 0x0, 0x10, 0xDF, 0x80, 0x0, 0x0, 0x0, 0x10, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x10, 0xFF, 0xC0, 0x0, 0x0, 0x0, 0x10, 0xFF, 0xC0, 0x0, 0x0, 0x0, 0x10, 0xFF, 0xC0, 0x0, 0x0, 0x0, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0
};

int sfgtMenuSoloBitmap[242] =
{
    48, 40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x10, 0x80, 0x0, 0x0, 0x0, 0x0, 0x10, 0xA9, 0x80, 0x1, 0xC0, 0x0, 0x10, 0xAA, 0x0, 0x7, 0xE0, 0x0, 0x10, 0xA9, 0x0, 0x1F, 0xF0, 0x0, 0x10, 0xA8, 0x80, 0x7F, 0xD8, 0x0, 0x10, 0x93, 0x20, 0xFF, 0xC8, 0x0, 0x10, 0x80, 0x0, 0xFF, 0xC8, 0x0, 0x10, 0x9B, 0x0, 0xFF, 0xC8, 0x0, 0x10, 0xA2, 0x81, 0x7F, 0xF0, 0x0, 0x10, 0xA3, 0x1, 0x7F, 0x80, 0x0, 0x10, 0xAA, 0x81, 0x3F, 0x80, 0x0, 0x10, 0x9B, 0x1, 0x1F, 0x0, 0x0, 0x10, 0x80, 0x0, 0xE3, 0x60, 0x0, 0x10, 0x80, 0x0, 0x3, 0xE0, 0x0, 0x10, 0x80, 0x0, 0xF, 0xF0, 0x0, 0x10, 0x80, 0x0, 0xF, 0xF1, 0xE0, 0x10, 0x80, 0x0, 0x7, 0xE3, 0x78, 0x10, 0x80, 0x0, 0x7, 0x3, 0xEC, 0x10, 0x80, 0x0, 0x0, 0x3, 0x1C, 0x10, 0x80, 0x0, 0x0, 0x63, 0x9C, 0xD0, 0x80, 0x0, 0x0, 0x73, 0xFD, 0xD0, 0x80, 0x0, 0x0, 0x39, 0xFB, 0x90, 0x81, 0xF0, 0x0, 0x1C, 0xF7, 0x10, 0x80, 0x7C, 0x0, 0xE, 0xCE, 0x10, 0x81, 0xF0, 0x0, 0x7, 0xFC, 0x10, 0x80, 0xE0, 0x0, 0x3, 0xF0, 0x10, 0x80, 0x60, 0x0, 0x1, 0xC0, 0x10, 0x80, 0x40, 0x0, 0x1, 0x80, 0x10, 0x80, 0x0, 0x0, 0x1, 0x80, 0x10, 0x9F, 0xFF, 0xC0, 0x1, 0x80, 0x10, 0xA0, 0x0, 0x20, 0x1, 0x80, 0x10, 0xA1, 0xFC, 0x20, 0x3, 0x0, 0x10, 0xA5, 0xE4, 0x20, 0xF, 0x80, 0x10, 0xAD, 0xF5, 0xA0, 0x1E, 0xC0, 0x10, 0xA5, 0xFC, 0x20, 0x38, 0xC0, 0x10, 0xA1, 0xFC, 0x20, 0x70, 0xC0, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0
};

int sfgtFinalScreenBitmap[322] =
{
    64, 40, 0x0, 0x0, 0x0, 0x0, 0xC0, 0x0, 0x40, 0x80, 0x0, 0x0, 0x0, 0x0, 0xA0, 0x0, 0xE0, 0x80, 0x0, 0x0, 0x0, 0x0, 0xCC, 0xE6, 0x46, 0xC0, 0x0, 0x0, 0x0, 0x0, 0xA8, 0xEA, 0x48, 0xA0, 0x18, 0x0, 0x0, 0x0, 0xAE, 0xAE, 0x46, 0xA0, 0x3C, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7A, 0x0, 0x0, 0x0, 0xE0, 0x0, 0x0, 0x0, 0xFC, 0x0, 0x0, 0x0, 0xE0, 0x0, 0x0, 0x0, 0xF8, 0x0, 0x0, 0x0, 0xAC, 0xCA, 0x0, 0x0, 0xB4, 0x0, 0x0, 0x0, 0xA8, 0xAA, 0x0, 0x0, 0x4E, 0xF0, 0x0, 0x0, 0xAE, 0xAE, 0x0, 0x0, 0x1C, 0xF2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xA, 0xF4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x28, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xB0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x3, 0xF0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x3, 0xD0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x3, 0xD0, 0x0, 0x0, 0x0, 0x50, 0x0, 0x0, 0x1, 0x20, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x3, 0xC0, 0x0, 0x0, 0x1, 0xB0, 0x0, 0x0, 0x5, 0x0, 0x0, 0x0, 0xF, 0xFF, 0xFC, 0x0, 0x5, 0x0, 0x0, 0x0, 0x8, 0x0, 0x4, 0x0, 0x5, 0x0, 0x0, 0x0, 0x9, 0x1, 0x6, 0x0, 0x5, 0x0, 0x0, 0x0, 0xB, 0x1, 0x6, 0x0, 0x2, 0x80, 0x0, 0x0, 0x9, 0x1B, 0x86, 0x0, 0x2, 0x80, 0x0, 0x0, 0x9, 0x11, 0x6, 0x0, 0x2, 0xC0, 0x0, 0x0, 0xB, 0xB1, 0x6, 0xF, 0xFF, 0xFC, 0x0, 0x0, 0x8, 0x0, 0x6, 0x8, 0x0, 0x4, 0x0, 0x0, 0xB, 0xB0, 0x6, 0x1B, 0x80, 0x84, 0x0, 0x0, 0xB, 0x90, 0x6, 0x18, 0x80, 0x84, 0x0, 0x0, 0xA, 0x18, 0x6, 0x1B, 0xB1, 0x84, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1A, 0x2A, 0x84, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1B, 0xA9, 0x84, 0x0, 0x0, 0x8, 0x0, 0x6, 0x18, 0x0, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1B, 0xA0, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1B, 0xA0, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1A, 0x20, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x18, 0x0, 0x4, 0x0, 0x0
};

int sfgtFinalScreenLooseBitmap[17] =
{
    24, 5, 0x64, 0xCC, 0xA0, 0x8A, 0xAA, 0xA0, 0x4A, 0xCC, 0x40, 0x2A, 0xAA, 0x40, 0xC4, 0xAA, 0x40
};

int sfgtFinalScreenWinBitmap[42] =
{
    64, 5, 0x64, 0xA6, 0xC4, 0xEA, 0x84, 0xEE, 0x4A, 0x60, 0x8A, 0xA8, 0xAA, 0x4A, 0x8A, 0x44, 0xAA, 0x80, 0x8A, 0xE8, 0xCE, 0x4A, 0x8E, 0x44, 0xAE, 0x40, 0x8A, 0xEA, 0xAA, 0x4A, 0x8A, 0x44, 0xAE, 0x20, 0x64, 0xA6, 0xAA, 0x4E, 0xEA, 0x4E, 0x4A, 0xC0
};

int sfgtFinalScreenP1Bitmap[322] =
{
    64, 40, 0x0, 0x0, 0x0, 0x0, 0xC0, 0x0, 0x40, 0x80, 0x0, 0x0, 0x0, 0x0, 0xA0, 0x0, 0xE0, 0x80, 0x0, 0x0, 0x0, 0x0, 0xCC, 0xE6, 0x46, 0xC0, 0x0, 0x0, 0x0, 0x0, 0xA8, 0xEA, 0x48, 0xA0, 0x18, 0x0, 0x0, 0x0, 0xAE, 0xAE, 0x46, 0xA0, 0x3C, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7A, 0x0, 0x0, 0x0, 0xE0, 0x0, 0x0, 0x0, 0xFC, 0x0, 0x0, 0x0, 0xE0, 0x0, 0x0, 0x0, 0xF8, 0x0, 0x0, 0x0, 0xAC, 0xCA, 0x0, 0x0, 0xB4, 0x0, 0x0, 0x0, 0xA8, 0xAA, 0x0, 0x0, 0x4E, 0xF0, 0x0, 0x0, 0xAE, 0xAE, 0x0, 0x0, 0x1C, 0xF2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xA, 0xF4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x28, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xB0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x3, 0xF0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x3, 0xD0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x3, 0xD0, 0x0, 0x0, 0x0, 0x50, 0x0, 0x0, 0x1, 0x20, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x3, 0xC0, 0x0, 0x0, 0x1, 0xB0, 0x0, 0x0, 0x5, 0x0, 0x0, 0x0, 0xF, 0xFF, 0xFC, 0x0, 0x5, 0x0, 0x0, 0x0, 0x8, 0x0, 0x4, 0x0, 0x5, 0x0, 0x0, 0x0, 0x9, 0x1, 0x6, 0x0, 0x5, 0x0, 0x0, 0x0, 0xB, 0x1, 0x6, 0x0, 0x2, 0x80, 0x0, 0x0, 0x9, 0x1B, 0x86, 0x0, 0x2, 0x80, 0x0, 0x0, 0x9, 0x11, 0x6, 0x0, 0x2, 0xC0, 0x0, 0x0, 0xB, 0xB1, 0x6, 0xF, 0xFF, 0xFC, 0x0, 0x0, 0x8, 0x0, 0x6, 0x8, 0x0, 0x4, 0x0, 0x0, 0xB, 0xA0, 0x6, 0x1B, 0x80, 0x84, 0x0, 0x0, 0xB, 0xA0, 0x6, 0x18, 0x80, 0x84, 0x0, 0x0, 0xA, 0x20, 0x6, 0x1B, 0xB1, 0x84, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1A, 0x2A, 0x84, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1B, 0xA9, 0x84, 0x0, 0x0, 0x8, 0x0, 0x6, 0x18, 0x0, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1B, 0xB0, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1B, 0x90, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x1A, 0x18, 0x4, 0x0, 0x0, 0x8, 0x0, 0x6, 0x18, 0x0, 0x4, 0x0, 0x0
};

int sfgtStartMenuBitmap[242] =
{
    64, 30, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF8, 0x0, 0x6, 0x73, 0x0, 0x0, 0x0, 0x3, 0x4, 0x6, 0x19, 0x5C, 0x80, 0x0, 0x0, 0x4, 0xC, 0x3A, 0x26, 0x53, 0x0, 0x0, 0x0, 0x8, 0x73, 0xA6, 0x48, 0x4C, 0x0, 0x0, 0x0, 0x8, 0x8C, 0xB4, 0x4E, 0xA8, 0x0, 0x0, 0x0, 0x4, 0x51, 0x94, 0x9E, 0xB4, 0x0, 0x0, 0x0, 0x2, 0x52, 0x16, 0xFC, 0xEE, 0x0, 0x0, 0x0, 0x2, 0x5A, 0x3E, 0x70, 0xE7, 0x0, 0x0, 0x0, 0xC, 0xCB, 0x38, 0x0, 0x0, 0x0, 0x0, 0x0, 0x31, 0x8F, 0x0, 0x7, 0xFF, 0xFF, 0x0, 0x0, 0x47, 0xC, 0x7, 0xF8, 0x0, 0x2, 0x0, 0x0, 0x3E, 0x3, 0x88, 0x7, 0xFC, 0xFC, 0xFC, 0x0, 0x18, 0x3C, 0xCF, 0xF8, 0x5, 0x0, 0x43, 0x0, 0x7, 0xC3, 0x0, 0x7, 0x75, 0x7F, 0x40, 0x80, 0x38, 0xC, 0x1, 0xF5, 0x55, 0x41, 0x4C, 0x40, 0x40, 0x30, 0x6, 0x15, 0x55, 0x5F, 0x4A, 0x40, 0x4C, 0x40, 0x68, 0x65, 0x55, 0x41, 0x4C, 0x40, 0x74, 0x4E, 0xA9, 0x85, 0x55, 0x5F, 0x40, 0x80, 0x44, 0x72, 0xAA, 0x5, 0xD5, 0x5F, 0x44, 0x80, 0x4, 0xE, 0xAA, 0x74, 0x15, 0x41, 0x4A, 0x40, 0x8, 0x70, 0xAA, 0x95, 0xD5, 0x7F, 0x49, 0x20, 0x4, 0x40, 0xAB, 0xB5, 0x57, 0x7F, 0x58, 0xF0, 0x4, 0x40, 0xA4, 0x27, 0x73, 0x0, 0xF0, 0x70, 0x4, 0x40, 0xA3, 0xE3, 0x30, 0x1, 0xE0, 0x20, 0x3, 0x20, 0xA0, 0xE0, 0x0, 0x0, 0xC0, 0x0, 0x3, 0xE0, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0xE0, 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};

int* sfgtTabCpt[4] = { sfgtFightBitmap, sfgtCptBold1Bitmap, sfgtCptBold2Bitmap, sfgtCptBold3Bitmap };
int* sfgtMenuTab[2] = { sfgtMenuSoloBitmap, sfgtMenuOptionBitmap };

// Real "\20" (octal) icon glyph upstream prints on the final screen (ASCII
// 16) - not a printable character a quoted string literal can hold
// directly, built as an explicit char[] instead (matching this project's
// own established convention for embedded icon glyphs, e.g.
// gameSimonbuino.c's own win-message array), and matching this shim's own
// real `gbPrintString(char* text)` signature.
char sfgtArrowGlyphText[2] = { 16, 0 };

// -----------------------------------------------------------------------------
//   Fighter data model - direct port of real StickFighter.ino's own
//   animSprite/Ayouken/Figther structs.
// -----------------------------------------------------------------------------

typedef struct
{
    int* sprite1;
    int* sprite2;
} SfgtAnimSprite;

typedef struct
{
    int posX, posY;
    int timeLive;
    int dir;
    int* sprite1;
    int* sprite2;
} SfgtAyouken;

typedef struct
{
    int currentState; // see the SFGT_ST_* constants above
    int currentSprite; // 0 or 1 - which of the pair to draw
    float posX, posY;
    float vx, vy;
    int height, width;
    SfgtAnimSprite sprites[SFGT_NB_STATE];
    int life;
    int oldLife; // real, unused upstream field - see this file's own header comment
    int cadance; // sprite animation speed
    int damage; // damage of the current punch
    int dir; // SFGT_NOFLIP -> or SFGT_FLIPH <-
    int timeAttack, timeNextAttack;
    bool isJump;
    int cptVictory;
    int isDef;
    SfgtAyouken ayouken;
    int combo[SFGT_NB_MOVE_SAVE];
    int timeFall;
} SfgtFighter;

SfgtFighter sfgtPlayer1 =
{
    SFGT_ST_IDLE, 0, 8.0, 29.0, 0.0, 0.0, 13, 8,
    {
        { sfgtIdle1Bitmap, sfgtIdle2Bitmap },
        { sfgtRun1Bitmap, sfgtRun2Bitmap },
        { sfgtKick1Bitmap, sfgtKick1Bitmap },
        { sfgtPunchLeft1Bitmap, sfgtPunchLeft1Bitmap },
        { sfgtPunchRight1Bitmap, sfgtPunchRight1Bitmap },
        { sfgtDuck1Bitmap, sfgtDuck1Bitmap },
        { sfgtDuckKick1Bitmap, sfgtDuckKick1Bitmap },
        { sfgtJump1Bitmap, sfgtJump1Bitmap },
        { sfgtJumpKick1Bitmap, sfgtJumpKick1Bitmap },
        { sfgtDead1Bitmap, sfgtDead1Bitmap },
        { sfgtFire1Bitmap, sfgtFire1Bitmap }
    },
    100, 100, 2, 5, SFGT_NOFLIP, 4, 8, false, 0, 0,
    { 0, 0, 0, SFGT_NOFLIP, sfgtFireBall1Bitmap, sfgtFireBall2Bitmap },
    { 0, 0, 0, 0 }, 0
};

SfgtFighter sfgtPlayer2 =
{
    SFGT_ST_IDLE, 0, 68.0, 29.0, 0.0, 0.0, 13, 8,
    {
        { sfgtP2Idle1Bitmap, sfgtP2Idle2Bitmap },
        { sfgtP2Run1Bitmap, sfgtP2Run2Bitmap },
        { sfgtP2Kick1Bitmap, sfgtP2Kick1Bitmap },
        { sfgtP2PunchLeft1Bitmap, sfgtP2PunchLeft1Bitmap },
        { sfgtP2PunchRight1Bitmap, sfgtP2PunchRight1Bitmap },
        { sfgtP2Duck1Bitmap, sfgtP2Duck1Bitmap },
        { sfgtP2DuckKick1Bitmap, sfgtP2DuckKick1Bitmap },
        { sfgtP2Jump1Bitmap, sfgtP2Jump1Bitmap },
        { sfgtP2JumpKick1Bitmap, sfgtP2JumpKick1Bitmap },
        { sfgtP2Dead1Bitmap, sfgtP2Dead1Bitmap },
        { sfgtFire1Bitmap, sfgtFire1Bitmap }
    },
    100, 100, 2, 5, SFGT_FLIPH, 4, 8, false, 0, 0,
    { 0, 0, 0, SFGT_FLIPH, sfgtFireBall1Bitmap, sfgtFireBall2Bitmap },
    { 0, 0, 0, 0 }, 0
};

// Real StickFighter.ino soundfx table (kick/punch/death) - see this file's
// own header comment on the one-shot-tone approximation this uses.
int sfgtSoundFx[3][8] =
{
    { 0, 22, 54, 1, 7, 0, 7, 10 }, // kick
    { 0, 22, 48, 1, 7, 5, 7, 10 }, // punch
    { 0, 24, 55, 1, 0, 0, 7, 7 }   // death
};

// -----------------------------------------------------------------------------
//   Particle system (part.ino/part.h, credited upstream to "valden") -
//   real single-player-compatible hit-spark effect. Position/velocity are
//   tracked at 10x scale for sub-pixel precision, matching real upstream.
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y;
    int vx, vy;
    int lifetime; // 0 = dead
} SfgtPart;

SfgtPart sfgtParticles[SFGT_PART_MAX];

// -----------------------------------------------------------------------------
//   Other real StickFighter.ino globals
// -----------------------------------------------------------------------------

int sfgtCptCombat = 0;
int sfgtStateFight = 0;
int sfgtXoffsetCptGras = 0;
int sfgtYoffsetTimeUp = 0;
int sfgtCptTechArena = 0;
int sfgtStateGame = SFGT_GAME_TITLE;

int sfgtFocusItem = 0; // 0 solo, 1 option
int sfgtTimeMinAffichageMenu = 2;

bool sfgtChoiceMenu = true;
int sfgtOffsetCredit = 0;

bool sfgtStopGame = false;

// Real upstream comment kept verbatim ("incrase for dificulte IA") -
// AI difficulty-scaling constant used throughout sfgtMoveIAPlayer() below.
int sfgtDiffculty = 15;

// -----------------------------------------------------------------------------
//   Prototypes
// -----------------------------------------------------------------------------

void sfgtInitPlayer( bool isStartGame );
void sfgtUpdPlayer( SfgtFighter* player, SfgtFighter* other );
void sfgtDrwPlayer( SfgtFighter* player );
void sfgtMovePlayer( SfgtFighter* player );
void sfgtChangeBoundPlayer( SfgtFighter* player );
bool sfgtPlayerIsAttack( SfgtFighter* player );
void sfgtGestionAttack( SfgtFighter* pAttack, SfgtFighter* pDef );
bool sfgtAddToCombo( SfgtFighter* player, int moveTouch );
void sfgtPlayerFall( int chance, SfgtFighter* pDef );
void sfgtUpdatePlayer();
void sfgtDrawPlayer();
int sfgtLifeTopixel( int life );
void sfgtLeftFigther( SfgtFighter* player );
void sfgtRightFigther( SfgtFighter* player );
void sfgtHighFigther( SfgtFighter* player );
void sfgtBottomFigther( SfgtFighter* player );
void sfgtPunchFigther( SfgtFighter* player );
void sfgtKickFigther( SfgtFighter* player );
void sfgtMoveIAPlayer( SfgtFighter* player, SfgtFighter* human );
void sfgtPlaysoundfx( int fxno );

void sfgtInitArena();
void sfgtUpdateArena();
void sfgtDrawArena();
void sfgtRestartCombat();

void sfgtInitMainMenu();
void sfgtUpdateMainMenu();
void sfgtDrawMainMenu();

void sfgtInitFinalScreen();
void sfgtUpdateFinalScreen();
void sfgtDrawFinalScreen();

void sfgtGoTitleScreen();
void sfgtUpdateTitle();
void sfgtCredit();

void sfgtPartInit();
void sfgtPartUpdate();
void sfgtPartCreate( int x, int y, int ranX, int ranY, int numbers );
void sfgtPartDraw( int xOffset, int yOffset );

void sfgtInitGame();

// -----------------------------------------------------------------------------
//   Player.ino port
// -----------------------------------------------------------------------------

void sfgtInitPlayer( bool isStartGame )
{
    sfgtPlayer1.currentState = SFGT_ST_IDLE;
    sfgtPlayer1.currentSprite = 0;
    sfgtPlayer1.posX = 8.0;
    sfgtPlayer1.posY = 42.0;
    sfgtPlayer1.vx = 0.0;
    sfgtPlayer1.vy = 0.0;
    sfgtPlayer1.height = 12;
    sfgtPlayer1.width = 8;
    sfgtPlayer1.life = 100;
    sfgtPlayer1.oldLife = 100;
    sfgtPlayer1.cadance = 2;
    sfgtPlayer1.damage = 5;
    sfgtPlayer1.dir = SFGT_NOFLIP;
    sfgtPlayer1.timeAttack = SFGT_TIME_ATTACK;
    sfgtPlayer1.isJump = false;

    sfgtPlayer2.currentState = SFGT_ST_IDLE;
    sfgtPlayer2.currentSprite = 0;
    sfgtPlayer2.posX = 68.0;
    sfgtPlayer2.posY = 42.0;
    sfgtPlayer2.vx = 0.0;
    sfgtPlayer2.vy = 0.0;
    sfgtPlayer2.height = 12;
    sfgtPlayer2.width = 8;
    sfgtPlayer2.life = 100;
    sfgtPlayer2.oldLife = 100;
    sfgtPlayer2.cadance = 2;
    sfgtPlayer2.damage = 5;
    sfgtPlayer2.dir = SFGT_FLIPH;
    sfgtPlayer2.timeAttack = SFGT_TIME_ATTACK;
    sfgtPlayer2.isJump = false;

    if( isStartGame )
    {
        sfgtPlayer1.cptVictory = 0;
        sfgtPlayer2.cptVictory = 0;
    }
    sfgtStopGame = true;
}

void sfgtUpdatePlayer()
{
    if( sfgtStateFight == 0 )
    {
        // "3, 2, 1, FIGHT!" - no player logic during the countdown
    }
    else if( sfgtStateFight == 1 )
    {
        // Fight - Player1 is human-controlled unless the attract-mode demo
        // is running (matches real upstream's own `if(stateGame != 2)`
        // guard exactly - see this file's own header comment).
        if( sfgtStateGame != SFGT_GAME_ATTRACT )
        {
            sfgtMovePlayer( &sfgtPlayer1 );
        }
    }
    else if( sfgtStateFight == 2 || sfgtStateFight == 3 )
    {
        // Fighter KO / Time UP
        if( sfgtStopGame )
        {
            if( sfgtPlayer1.life != sfgtPlayer2.life )
            {
                if( sfgtPlayer1.life > sfgtPlayer2.life )
                {
                    sfgtPlayer1.cptVictory = sfgtPlayer1.cptVictory + 1;
                }
                else
                {
                    sfgtPlayer2.cptVictory = sfgtPlayer2.cptVictory + 1;
                }
            }
            sfgtStopGame = false;
        }
    }
    else if( sfgtStateFight == 4 )
    {
        // End of the KO screen
        if( sfgtPlayer1.cptVictory == 3 || sfgtPlayer2.cptVictory == 3 )
        {
            if( sfgtStateGame == SFGT_GAME_ATTRACT )
            {
                sfgtPlayer1.cptVictory = 0;
                sfgtPlayer2.cptVictory = 0;
                sfgtRestartCombat();
            }
            else
            {
                sfgtStateGame = SFGT_GAME_FINAL;
            }
        }
        else
        {
            sfgtRestartCombat();
        }
    }

    sfgtUpdPlayer( &sfgtPlayer1, &sfgtPlayer2 );
    sfgtUpdPlayer( &sfgtPlayer2, &sfgtPlayer1 );

    // Update facing direction
    if( sfgtPlayer1.posX < sfgtPlayer2.posX )
    {
        sfgtPlayer1.dir = SFGT_NOFLIP;
        sfgtPlayer2.dir = SFGT_FLIPH;
    }
    else
    {
        sfgtPlayer1.dir = SFGT_FLIPH;
        sfgtPlayer2.dir = SFGT_NOFLIP;
    }

    if( sfgtPlayerIsAttack( &sfgtPlayer1 ) )
    {
        sfgtGestionAttack( &sfgtPlayer1, &sfgtPlayer2 );
    }
    if( sfgtPlayerIsAttack( &sfgtPlayer2 ) )
    {
        sfgtGestionAttack( &sfgtPlayer2, &sfgtPlayer1 );
    }
}

void sfgtGestionAttack( SfgtFighter* pAttack, SfgtFighter* pDef )
{
    int damage = 0;

    if( pAttack->currentState == SFGT_ST_PUNCH_LEFT || pAttack->currentState == SFGT_ST_PUNCH_RIGHT )
    {
        if( gbCollideRectRect( (int)( pAttack->posX - 4 ), (int)( pAttack->posY - 14 ), 14, 3,
            (int)pDef->posX, (int)( pDef->posY - pDef->height ), 6, pDef->height ) )
        {
            damage = 5;
        }
    }
    else if( pAttack->currentState == SFGT_ST_KICK || pAttack->currentState == SFGT_ST_JUMP_KICK )
    {
        int offsetY = 12;
        if( pAttack->isJump )
        {
            offsetY = 0;
        }
        if( gbCollideRectRect( (int)( pAttack->posX - 6 ), (int)( pAttack->posY - offsetY ), 18, 3,
            (int)pDef->posX, (int)( pDef->posY - pDef->height ), 6, pDef->height ) )
        {
            damage = 8;
            pAttack->timeAttack = pAttack->timeAttack - 1;
            if( pAttack->currentState == SFGT_ST_JUMP_KICK )
            {
                sfgtPlayerFall( 2, pDef );
            }
        }
    }
    else if( pAttack->currentState == SFGT_ST_DUCK_KICK )
    {
        if( gbCollideRectRect( (int)( pAttack->posX - 6 ), (int)( pAttack->posY - 3 ), 18, 3,
            (int)pDef->posX, (int)( pDef->posY - pDef->height ), 6, pDef->height ) )
        {
            damage = 3;
            sfgtPlayerFall( 2, pDef );
        }
    }

    if( pAttack->ayouken.timeLive > 0 )
    {
        if( gbCollideRectRect( pAttack->ayouken.posX, pAttack->ayouken.posY, 6, 6,
            (int)pDef->posX, (int)( pDef->posY - pDef->height ), 6, pDef->height ) )
        {
            damage = SFGT_DAMAGE_AYOUKEN;
            if( pAttack->ayouken.timeLive > 0 )
            {
                if( pAttack->ayouken.timeLive > 2 )
                {
                    pAttack->ayouken.timeLive = pAttack->ayouken.timeLive - 2;
                }
                else
                {
                    pAttack->ayouken.timeLive = pAttack->ayouken.timeLive - 1;
                }
            }
            sfgtPlayerFall( 3, pDef );
        }
        if( pDef->ayouken.timeLive > 0 )
        {
            if( gbCollideRectRect( pAttack->ayouken.posX, pAttack->ayouken.posY, 6, 6,
                pDef->ayouken.posX, pDef->ayouken.posY, 6, 6 ) )
            {
                if( pAttack->ayouken.timeLive > 0 )
                {
                    if( pAttack->ayouken.timeLive > 2 )
                    {
                        pAttack->ayouken.timeLive = pAttack->ayouken.timeLive - 2;
                    }
                    else
                    {
                        pAttack->ayouken.timeLive = pAttack->ayouken.timeLive - 1;
                    }
                    if( pDef->ayouken.timeLive > 2 )
                    {
                        pDef->ayouken.timeLive = pDef->ayouken.timeLive - 2;
                    }
                    else
                    {
                        pDef->ayouken.timeLive = pDef->ayouken.timeLive - 1;
                    }
                }
            }
        }
    }

    if( damage )
    {
        sfgtPartCreate( (int)pDef->posX, (int)( pDef->posY - 10 ), 10, 10, 5 );
        if( pDef->isDef > 0 )
        {
            pAttack->timeNextAttack = pAttack->timeNextAttack + 10;
            damage = damage / 2;
        }

        pDef->life = pDef->life - damage;
        if( pDef->life < 0 )
        {
            pDef->life = 0;
            sfgtPlaysoundfx( 2 );
        }

        if( pDef->dir == SFGT_NOFLIP )
        {
            if( pDef->life > 0 )
            {
                pDef->vx = -SFGT_SPEED_RUN;
            }
            else
            {
                pDef->vx = -SFGT_SPEED_RUN * 6;
                pDef->vy = -SFGT_SPEED_RUN * 6;
            }
        }
        else
        {
            if( pDef->life > 0 )
            {
                pDef->vx = SFGT_SPEED_RUN;
            }
            else
            {
                pDef->vx = SFGT_SPEED_RUN * 6;
                pDef->vy = -SFGT_SPEED_RUN * 6;
            }
        }
    }
}

void sfgtPlayerFall( int chance, SfgtFighter* pDef )
{
    if( arand( chance ) == 0 )
    {
        pDef->currentState = SFGT_ST_DEAD;
        pDef->timeFall = SFGT_TIME_FALL;
        sfgtChangeBoundPlayer( pDef );
    }
}

int sfgtLifeTopixel( int life )
{
    return (int)( life * 0.23 );
}

void sfgtDrwPlayer( SfgtFighter* player )
{
    if( player->ayouken.timeLive > 0 )
    {
        int* ayoukenSprite;
        if( player->ayouken.timeLive > ( SFGT_TIME_LIVE_AYOUKEN / 3 ) )
        {
            ayoukenSprite = player->ayouken.sprite1;
        }
        else
        {
            ayoukenSprite = player->ayouken.sprite2;
        }
        gbDrawBitmapRotated( player->ayouken.posX, player->ayouken.posY, ayoukenSprite, 0, player->ayouken.dir );
    }

    int px;
    if( player->dir == SFGT_NOFLIP )
    {
        px = (int)player->posX;
    }
    else
    {
        px = (int)( player->posX - ( player->width - 6 ) );
    }

    int* sprite;
    if( player->currentSprite == 0 )
    {
        sprite = player->sprites[ player->currentState ].sprite1;
    }
    else
    {
        sprite = player->sprites[ player->currentState ].sprite2;
    }

    gbDrawBitmapRotated( px, (int)( player->posY - player->height ), sprite, 0, player->dir );
}

void sfgtDrawPlayer()
{
    sfgtDrwPlayer( &sfgtPlayer1 );
    sfgtDrwPlayer( &sfgtPlayer2 );

    gbFillRect( 14, 2, sfgtLifeTopixel( sfgtPlayer1.life ), 3 );

    int offsetPx = sfgtLifeTopixel( sfgtPlayer2.life );
    gbFillRect( 47 + ( 23 - offsetPx ), 2, offsetPx, 3 );

    int i;
    for( i = 0; i < sfgtPlayer1.cptVictory; i = i + 1 )
    {
        gbFillRect( i * 5 + 2, 8, 2, 2 );
    }

    for( i = 0; i < sfgtPlayer2.cptVictory; i = i + 1 )
    {
        gbFillRect( 80 - ( i * 5 ), 8, 2, 2 );
    }
}

void sfgtUpdPlayer( SfgtFighter* player, SfgtFighter* other )
{
    if( gbFrameCount % player->cadance == 0 )
    {
        player->currentSprite = player->currentSprite + 1;
        player->currentSprite = player->currentSprite % SFGT_NB_SPRITE_STATE;
    }

    if( player->timeNextAttack > 0 )
    {
        player->timeNextAttack = player->timeNextAttack - 1;
    }
    if( player->timeFall > 0 )
    {
        player->timeFall = player->timeFall - 1;
    }

    if( player->ayouken.timeLive > 0 )
    {
        if( player->ayouken.dir == SFGT_NOFLIP )
        {
            player->ayouken.posX = player->ayouken.posX + SFGT_VITT_AYOUKEN;
        }
        else
        {
            player->ayouken.posX = player->ayouken.posX - SFGT_VITT_AYOUKEN;
        }
        player->ayouken.timeLive = player->ayouken.timeLive - 1;
    }
    if( player->timeAttack > 0 )
    {
        player->timeAttack = player->timeAttack - 1;
    }

    if( player->isDef > 0 )
    {
        player->isDef = player->isDef - 1;
    }

    if( player->life > 0 )
    {
        if( player->timeAttack == 0 && player->currentState != SFGT_ST_IDLE && player->currentState != SFGT_ST_DUCK
            && player->currentState != SFGT_ST_DEAD && player->timeFall == 0 )
        {
            if( player->currentState == SFGT_ST_DUCK_KICK )
            {
                player->currentState = SFGT_ST_DUCK;
            }
            else if( player->isJump )
            {
                player->currentState = SFGT_ST_JUMP;
            }
            else if( player->vx < SFGT_SEUIL_MIN_MOVE && player->vx > -SFGT_SEUIL_MIN_MOVE )
            {
                player->currentState = SFGT_ST_IDLE;
            }
            else
            {
                player->currentState = SFGT_ST_RUN;
            }

            sfgtChangeBoundPlayer( player );
        }
    }
    else
    {
        player->currentState = SFGT_ST_DEAD;
        sfgtChangeBoundPlayer( player );
    }

    if( ( ( player->vx > 0 && player->dir == SFGT_NOFLIP ) || ( player->vx < 0 && player->dir != SFGT_NOFLIP ) )
        && gbCollideRectRect( (int)player->posX, (int)( player->posY - player->height ), 8, player->height,
            (int)other->posX, (int)( other->posY - other->height ), 8, other->height ) )
    {
        player->vx = 0;
    }

    if( !player->isJump )
    {
        if( player->vx != 0 )
        {
            player->posX = player->posX + player->vx;
            player->vx = player->vx * 0.4; // rapid deceleration
        }
        else if( player->vx < SFGT_SEUIL_MIN_MOVE && player->vx > -SFGT_SEUIL_MIN_MOVE )
        {
            player->vx = 0;
        }
    }
    else
    {
        if( player->vy < -1.2 )
        {
            player->vy = player->vy * 0.9;
            player->posY = player->posY + player->vy;
        }
        else
        {
            if( player->vy < 0 )
            {
                player->vy = -player->vy;
            }
            player->vy = player->vy * 1.3;
            player->posY = player->posY + player->vy;
        }
        player->posX = player->posX + player->vx;
        player->vx = player->vx * 0.9; // rapid deceleration

        if( player->posY >= SFGT_GROUND_Y )
        {
            player->posY = SFGT_GROUND_Y;
            player->isJump = false;
            player->vy = 0;
            player->vx = 0;
            player->timeAttack = 0;
        }
    }

    if( player->posX < 0 )
    {
        player->posX = 0;
    }
    else if( ( player->posX + player->width ) > 84 )
    {
        player->posX = 84 - player->width;
    }
}

void sfgtMovePlayer( SfgtFighter* player )
{
    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        sfgtRightFigther( player );
    }
    else if( gbRepeat( BTN_LEFT, 1 ) )
    {
        sfgtLeftFigther( player );
    }
    if( gbPressed( BTN_UP ) )
    {
        sfgtHighFigther( player );
    }
    else if( gbRepeat( BTN_DOWN, 1 ) )
    {
        sfgtBottomFigther( player );
    }

    if( gbPressed( BTN_A ) )
    {
        sfgtPunchFigther( player );
    }
    else if( gbPressed( BTN_B ) )
    {
        sfgtKickFigther( player );
    }
}

void sfgtMoveIAPlayer( SfgtFighter* player, SfgtFighter* human )
{
    if( sfgtStateFight == 1 )
    {
        int isFireBall = 0;
        int isCrunch = 0;
        int fuiteStrategique = 0;
        int dist = (int)fabs( player->posX - human->posX );

        if( human->currentState == SFGT_ST_DUCK || human->currentState == SFGT_ST_DUCK_KICK || human->currentState == SFGT_ST_DEAD )
        {
            isCrunch = 5;
        }
        int diffLife = player->life - human->life;
        if( human->ayouken.timeLive > 0 )
        {
            isFireBall = 5;
        }
        if( isCrunch > 0 && diffLife > 0 )
        {
            fuiteStrategique = 10;
        }

        int rdm = arand( 100 );

        // Attack?
        if( rdm <= ( 20 - isCrunch + diffLife - dist ) )
        {
            sfgtPunchFigther( player );
        }
        else if( rdm > 65 && rdm < 90 - dist + sfgtDiffculty )
        {
            sfgtKickFigther( player );
        }
        else if( rdm > 45 && rdm < ( 48 + isCrunch + fuiteStrategique - diffLife + isFireBall ) )
        {
            sfgtBottomFigther( player );
            if( player->dir == SFGT_NOFLIP )
            {
                sfgtRightFigther( player );
            }
            else
            {
                sfgtLeftFigther( player );
            }
            sfgtPunchFigther( player );
        }

        // Move
        if( rdm > ( 95 - fuiteStrategique + diffLife - sfgtDiffculty ) )
        {
            // backward
            if( player->dir != SFGT_NOFLIP )
            {
                sfgtRightFigther( player );
            }
            else
            {
                sfgtLeftFigther( player );
            }
        }
        else if( rdm <= ( 40 + isCrunch + diffLife - fuiteStrategique + sfgtDiffculty ) )
        {
            if( player->dir == SFGT_NOFLIP )
            {
                sfgtRightFigther( player );
            }
            else
            {
                sfgtLeftFigther( player );
            }
        }
        if( rdm > 19 && rdm < 25 + isFireBall )
        {
            sfgtHighFigther( player );
        }
        else if( rdm > ( 39 - isCrunch + diffLife ) && rdm < 45 + isCrunch )
        {
            sfgtBottomFigther( player );
        }
    }
}

// IDL:0, run:1, kick:2, punchLeft:3, punchRight:4, duck:5, duckKick:6,
// jump:7, jumpKick:8, dead:9
void sfgtLeftFigther( SfgtFighter* player )
{
    if( player->life == 0 )
        return;
    int moveTouch;
    if( player->dir == SFGT_NOFLIP )
    {
        moveTouch = 4;
    }
    else
    {
        moveTouch = 3;
    }
    if( sfgtAddToCombo( player, moveTouch ) )
        return;
    if( !player->isJump )
    {
        if( player->dir == SFGT_NOFLIP )
        {
            // recoil
            player->isDef = SFGT_TIME_DEF;

            if( player->timeNextAttack > 0 )
                return;
            player->vx = -SFGT_SPEED_RUN / 2;
        }
        else
        {
            if( player->timeNextAttack > 0 )
                return;
            // advance
            player->vx = -SFGT_SPEED_RUN;
        }

        if( !sfgtPlayerIsAttack( player ) )
            player->currentState = SFGT_ST_RUN;
    }
    else
    {
        if( player->timeNextAttack > 0 )
            return;
        if( player->vx > -SFGT_SPEED_RUN )
            player->vx = player->vx - 0.1;
    }

    sfgtChangeBoundPlayer( player );
}

void sfgtRightFigther( SfgtFighter* player )
{
    if( player->life == 0 )
        return;
    int moveTouch;
    if( player->dir == SFGT_NOFLIP )
    {
        moveTouch = 3;
    }
    else
    {
        moveTouch = 4;
    }
    if( sfgtAddToCombo( player, moveTouch ) )
        return;
    if( !player->isJump )
    {
        if( player->dir == SFGT_FLIPH )
        {
            // recoil
            player->isDef = SFGT_TIME_DEF;

            if( player->timeNextAttack > 0 )
                return;
            player->vx = SFGT_SPEED_RUN / 2;
        }
        else
        {
            if( player->timeNextAttack > 0 )
                return;
            // advance
            player->vx = SFGT_SPEED_RUN;
        }
        if( !sfgtPlayerIsAttack( player ) )
            player->currentState = SFGT_ST_RUN;
    }
    else
    {
        if( player->timeNextAttack > 0 )
            return;
        if( player->vx < SFGT_SPEED_RUN )
            player->vx = player->vx + 0.1;
    }
    sfgtChangeBoundPlayer( player );
}

void sfgtHighFigther( SfgtFighter* player )
{
    if( player->timeNextAttack > 0 )
        return;
    if( player->life == 0 )
        return;
    if( sfgtAddToCombo( player, 1 ) )
        return;
    if( !player->isJump )
    {
        player->currentState = SFGT_ST_JUMP;
        sfgtChangeBoundPlayer( player );
        player->isJump = true;
        player->vy = -SFGT_SPEED_RUN;
    }
}

void sfgtBottomFigther( SfgtFighter* player )
{
    if( player->life == 0 )
        return;
    if( player->timeNextAttack > 0 )
        return;
    if( sfgtAddToCombo( player, 2 ) )
        return;
    player->currentState = SFGT_ST_DUCK;
    sfgtChangeBoundPlayer( player );
}

void sfgtPunchFigther( SfgtFighter* player )
{
    if( player->life == 0 )
        return;
    if( player->timeNextAttack > 0 )
        return;
    sfgtPlaysoundfx( 1 );

    if( sfgtAddToCombo( player, 5 ) )
        return;
    if( player->currentState == SFGT_ST_DUCK )
    {
        player->currentState = SFGT_ST_DUCK_KICK;
        player->timeAttack = SFGT_TIME_ATTACK;
    }
    else if( !player->isJump )
    {
        player->currentState = 3 + arand( 2 ); // punchLeft(3) or punchRight(4)
        player->timeAttack = SFGT_TIME_ATTACK;
    }
    else
    {
        player->currentState = SFGT_ST_JUMP_KICK;
        player->timeAttack = SFGT_TIME_ATTACK * 4;
    }
    player->timeNextAttack = SFGT_TIME_ATTACK + 1; // punches are fast
    sfgtChangeBoundPlayer( player );
}

void sfgtKickFigther( SfgtFighter* player )
{
    if( player->life == 0 )
        return;
    if( player->timeNextAttack > 0 )
        return;
    sfgtPlaysoundfx( 0 );
    if( sfgtAddToCombo( player, 6 ) )
        return;
    if( player->currentState == SFGT_ST_DUCK )
    {
        player->currentState = SFGT_ST_DUCK_KICK;
        player->timeAttack = SFGT_TIME_ATTACK;
    }
    else if( !player->isJump )
    {
        player->timeAttack = SFGT_TIME_ATTACK;
        player->currentState = SFGT_ST_KICK;
    }
    else
    {
        player->timeAttack = SFGT_TIME_ATTACK * 4;
        player->currentState = SFGT_ST_KICK;
    }
    player->timeNextAttack = SFGT_TIME_ATTACK + 3; // kicks are slow
    sfgtChangeBoundPlayer( player );
}

void sfgtChangeBoundPlayer( SfgtFighter* player )
{
    player->width = player->sprites[ player->currentState ].sprite1[ 0 ];
    player->height = player->sprites[ player->currentState ].sprite1[ 1 ];
}

bool sfgtPlayerIsAttack( SfgtFighter* player )
{
    return ( player->timeAttack > 0 || player->ayouken.timeLive > 0 );
}

// moveTouch: 1=>up, 2=>down, 3=>forward, 4=>backward, 5=>A, 6=>B
bool sfgtAddToCombo( SfgtFighter* player, int moveTouch )
{
    if( player->combo[ 0 ] == moveTouch )
        return false;
    int i;
    for( i = SFGT_NB_MOVE_SAVE - 1; i > 0; i = i - 1 )
    {
        player->combo[ i ] = player->combo[ i - 1 ];
    }
    player->combo[ 0 ] = moveTouch;

    if( player->ayouken.timeLive == 0 && player->combo[ 0 ] == 5 && player->combo[ 1 ] == 3 && player->combo[ 2 ] == 2 )
    {
        player->currentState = SFGT_ST_FIRE;
        player->ayouken.timeLive = SFGT_TIME_LIVE_AYOUKEN;
        player->ayouken.posX = (int)player->posX;
        player->ayouken.posY = (int)( player->posY - 8 );
        player->ayouken.dir = player->dir;
        player->timeNextAttack = SFGT_TIME_ATTACK + 5; // fireballs are very slow
        return true;
    }

    return false;
}

void sfgtPlaysoundfx( int fxno )
{
    gbPlayNote( sfgtSoundFx[ fxno ][ 1 ], sfgtSoundFx[ fxno ][ 7 ] );
}

// -----------------------------------------------------------------------------
//   arena.ino port
// -----------------------------------------------------------------------------

void sfgtInitArena()
{
    sfgtCptCombat = SFGT_CPT_COMBAT_INIT;
    sfgtCptTechArena = 80;
    sfgtStateFight = 0;
    sfgtXoffsetCptGras = 0;
    sfgtYoffsetTimeUp = 0;
}

void sfgtUpdateArena()
{
    if( sfgtCptTechArena > 0 )
        sfgtCptTechArena = sfgtCptTechArena - 1;
    if( sfgtStateFight == 0 )
    {
        if( sfgtCptTechArena == 19 )
        {
            sfgtXoffsetCptGras = 13;
        }
        else if( sfgtCptTechArena == 0 )
        {
            sfgtStateFight = 1;
        }
    }
    if( sfgtStateFight == 1 && sfgtCptCombat == 0 )
    {
        sfgtStateFight = 3;
        sfgtCptTechArena = 60;
    }

    if( ( sfgtPlayer1.life == 0 || sfgtPlayer2.life == 0 ) && sfgtStateFight != 2 )
    {
        sfgtStateFight = 2;
        sfgtCptTechArena = 60;
    }

    if( sfgtStateFight == 1 )
    {
        if( gbFrameCount % 20 == 0 )
            sfgtCptCombat = sfgtCptCombat - 1;
    }

    if( sfgtStateFight == 3 || sfgtStateFight == 2 )
    {
        if( sfgtCptTechArena >= 30 )
        {
            sfgtYoffsetTimeUp = sfgtCptTechArena - 30;
        }
        if( sfgtCptTechArena == 0 )
        {
            sfgtStateFight = 4;
        }
    }
}

void sfgtDrawArena()
{
    gbDrawBitmap( 0, 0, sfgtAreneBitmap );

    if( sfgtStateFight == 0 )
    {
        gbDrawBitmap( 39 - sfgtXoffsetCptGras, 18, sfgtTabCpt[ sfgtCptTechArena / 20 ] );
    }
    else if( sfgtStateFight == 2 )
    {
        if( sfgtPlayer1.life != sfgtPlayer2.life )
        {
            int* koBitmap;
            if( sfgtPlayer1.life == 0 )
            {
                koBitmap = sfgtP1koBitmap;
            }
            else
            {
                koBitmap = sfgtP2koBitmap;
            }
            gbDrawBitmap( 29, 18 + sfgtYoffsetTimeUp, koBitmap );
        }
    }
    else if( sfgtStateFight == 3 )
    {
        gbDrawBitmap( 21, 18 + sfgtYoffsetTimeUp, sfgtTimeUpBitmap );
    }

    int dizaine = sfgtCptCombat / 10;
    int unite = sfgtCptCombat - ( dizaine * 10 );

    gbCursorX = 39;
    gbCursorY = 1;
    gbPrintNumber( dizaine );
    gbCursorX = 42;
    gbPrintNumber( unite );
}

void sfgtRestartCombat()
{
    sfgtInitPlayer( false );
    sfgtInitArena();
}

// -----------------------------------------------------------------------------
//   mainMenu.ino port (menuMulti dropped - see this file's own header
//   comment)
// -----------------------------------------------------------------------------

void sfgtInitMainMenu()
{
    sfgtFocusItem = 0; // 0 solo, 1 option
    sfgtTimeMinAffichageMenu = 2;
}

void sfgtUpdateMainMenu()
{
    if( sfgtTimeMinAffichageMenu > 0 )
        sfgtTimeMinAffichageMenu = sfgtTimeMinAffichageMenu - 1;
    if( gbPressed( BTN_LEFT ) )
    {
        if( sfgtFocusItem == 0 )
        {
            sfgtFocusItem = SFGT_INDEX_MAX_ITEM_MENU;
        }
        else
        {
            sfgtFocusItem = sfgtFocusItem - 1;
        }
    }
    else if( gbPressed( BTN_RIGHT ) )
    {
        if( sfgtFocusItem == SFGT_INDEX_MAX_ITEM_MENU )
        {
            sfgtFocusItem = 0;
        }
        else
        {
            sfgtFocusItem = sfgtFocusItem + 1;
        }
    }
    else if( gbPressed( BTN_A ) && sfgtTimeMinAffichageMenu == 0 )
    {
        if( sfgtFocusItem == 0 )
        {
            sfgtStateGame = SFGT_GAME_PLAY;
        }
        else
        {
            sfgtStateGame = SFGT_GAME_ATTRACT;
        }
    }
}

void sfgtDrawMainMenu()
{
    int leftIdx;
    if( sfgtFocusItem == 0 )
    {
        leftIdx = SFGT_INDEX_MAX_ITEM_MENU;
    }
    else
    {
        leftIdx = sfgtFocusItem - 1;
    }

    int rightIdx;
    if( sfgtFocusItem == SFGT_INDEX_MAX_ITEM_MENU )
    {
        rightIdx = 0;
    }
    else
    {
        rightIdx = sfgtFocusItem + 1;
    }

    gbDrawBitmap( -24, 8, sfgtMenuTab[ leftIdx ] );
    gbDrawBitmap( 22, 2, sfgtMenuTab[ sfgtFocusItem ] );
    gbDrawBitmap( 68, 8, sfgtMenuTab[ rightIdx ] );
}

// -----------------------------------------------------------------------------
//   finalScreen.ino port
// -----------------------------------------------------------------------------

void sfgtInitFinalScreen()
{
    sfgtChoiceMenu = true;
}

void sfgtUpdateFinalScreen()
{
    if( gbPressed( BTN_UP ) || gbPressed( BTN_DOWN ) )
    {
        sfgtChoiceMenu = !sfgtChoiceMenu;
    }
    if( gbPressed( BTN_A ) )
    {
        sfgtInitPlayer( true );
        sfgtInitArena();
        if( sfgtChoiceMenu )
        {
            sfgtStateGame = SFGT_GAME_PLAY;
        }
        else
        {
            sfgtStateGame = SFGT_GAME_MENU;
        }
    }
}

void sfgtDrawFinalScreen()
{
    if( sfgtPlayer1.cptVictory == 3 )
    {
        gbDrawBitmap( 12, 8, sfgtFinalScreenP1Bitmap );
    }
    else
    {
        gbDrawBitmap( 12, 8, sfgtFinalScreenBitmap );
    }

    // The human player is always Player1 in every remaining mode (see this
    // file's own header comment on the real isMaster/isOnePlayer removal).
    if( sfgtPlayer1.cptVictory == 3 )
    {
        gbDrawBitmap( 10, 1, sfgtFinalScreenWinBitmap );
    }
    else
    {
        gbDrawBitmap( 10, 1, sfgtFinalScreenLooseBitmap );
    }

    gbCursorX = 40;
    if( sfgtChoiceMenu )
    {
        gbCursorY = 8;
    }
    else
    {
        gbCursorY = 14;
    }
    gbPrintString( sfgtArrowGlyphText );
}

// -----------------------------------------------------------------------------
//   titleScreen.ino port (real blocking gb.titleScreen() -> explicit
//   state - see this file's own header comment)
// -----------------------------------------------------------------------------

void sfgtGoTitleScreen()
{
    sfgtStateGame = SFGT_GAME_TITLE;
    sfgtOffsetCredit = 50;
}

void sfgtUpdateTitle()
{
    gbCursorX = 20;
    gbCursorY = 1;
    gbPrintString( "PRESS A" );
    gbDrawBitmap( 0, 12, sfgtStartMenuBitmap );

    if( gbPressed( BTN_A ) )
    {
        sfgtInitGame();
    }
}

void sfgtCredit()
{
    gbCursorX = 0;
    gbCursorY = sfgtOffsetCredit;
    gbPrintString( "Design by :\n     Quirby64" );
    gbCursorX = 0;
    gbCursorY = sfgtOffsetCredit + 50;
    gbPrintString( "Programme by :\n     Clement" );
    sfgtOffsetCredit = sfgtOffsetCredit - 1;
}

// -----------------------------------------------------------------------------
//   part.ino / part.h port
// -----------------------------------------------------------------------------

void sfgtPartUpdate()
{
    int i;
    for( i = 0; i < SFGT_PART_MAX; i = i + 1 )
    {
        if( sfgtParticles[ i ].lifetime > 0 )
        {
            sfgtParticles[ i ].x = sfgtParticles[ i ].x + sfgtParticles[ i ].vx;
            sfgtParticles[ i ].y = sfgtParticles[ i ].y + sfgtParticles[ i ].vy;

            sfgtParticles[ i ].vy = sfgtParticles[ i ].vy + SFGT_PART_GRAV_Y;
            sfgtParticles[ i ].vx = sfgtParticles[ i ].vx + SFGT_PART_GRAV_X;

            sfgtParticles[ i ].lifetime = sfgtParticles[ i ].lifetime - 1;

            if( sfgtParticles[ i ].x < -60 || sfgtParticles[ i ].x > 900 || sfgtParticles[ i ].y < -50 || sfgtParticles[ i ].y > 950 )
                sfgtParticles[ i ].lifetime = 0;
        }
    }
}

void sfgtPartCreate( int x, int y, int ranX, int ranY, int numbers )
{
    int i = 0;
    int j = 0;
    while( j < numbers && i < SFGT_PART_MAX )
    {
        if( sfgtParticles[ i ].lifetime == 0 )
        {
            j = j + 1;

            sfgtParticles[ i ].x = x * 10;
            sfgtParticles[ i ].y = y * 10;

            sfgtParticles[ i ].vx = -ranX + arand( ranX * 2 );
            sfgtParticles[ i ].vy = -ranY + arand( ranY * 2 );

            sfgtParticles[ i ].lifetime = SFGT_PART_LIFETIME;
        }
        i = i + 1;
    }
}

void sfgtPartInit()
{
    int i;
    for( i = 0; i < SFGT_PART_MAX; i = i + 1 )
    {
        sfgtParticles[ i ].lifetime = 0;
    }
}

void sfgtPartDraw( int xOffset, int yOffset )
{
    int i;
    for( i = 0; i < SFGT_PART_MAX; i = i + 1 )
    {
        if( sfgtParticles[ i ].lifetime != 0 )
        {
            gbDrawPixel( sfgtParticles[ i ].x / 10 + xOffset, sfgtParticles[ i ].y / 10 + yOffset );
        }
    }
}

// -----------------------------------------------------------------------------
//   StickFighter.ino port - top-level state machine
// -----------------------------------------------------------------------------

void sfgtInitGame()
{
    sfgtStateGame = SFGT_GAME_MENU;
    sfgtInitMainMenu();
    sfgtInitPlayer( true );
    sfgtInitArena();
    sfgtInitFinalScreen();
    sfgtPartInit();
}

void gameStickFighter_init()
{
    gbBegin();
    sfgtGoTitleScreen();
}

void gameStickFighter_update()
{
    if( !gbUpdate() )
        return;

    if( gbPressed( BTN_C ) )
    {
        sfgtGoTitleScreen();
    }

    if( sfgtStateGame == SFGT_GAME_TITLE )
    {
        sfgtUpdateTitle();
    }
    else if( sfgtStateGame == SFGT_GAME_MENU )
    {
        sfgtUpdateMainMenu();
        sfgtDrawMainMenu();
    }
    else if( sfgtStateGame == SFGT_GAME_FINAL )
    {
        sfgtUpdateFinalScreen();
        sfgtDrawFinalScreen();
    }
    else if( sfgtStateGame == SFGT_GAME_ATTRACT )
    {
        sfgtMoveIAPlayer( &sfgtPlayer2, &sfgtPlayer1 );
        sfgtMoveIAPlayer( &sfgtPlayer1, &sfgtPlayer2 );
        sfgtUpdatePlayer();
        sfgtUpdateArena();
        sfgtDrawPlayer();
        sfgtDrawArena();
        sfgtCredit();
    }
    else // SFGT_GAME_PLAY
    {
        sfgtUpdatePlayer();
        sfgtUpdateArena();
        sfgtMoveIAPlayer( &sfgtPlayer2, &sfgtPlayer1 );
        sfgtPartUpdate();
        sfgtDrawPlayer();
        sfgtDrawArena();
        sfgtPartDraw( 0, 0 );
    }

    gbRenderFrame();
}
