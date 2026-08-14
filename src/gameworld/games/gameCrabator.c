#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Crabator (Rodot, license: none specified upstream - github.com/Rodot/
// Crabator). A top-down twin-stick-style shooter on a toroidal (wrapping)
// 16x12 tile world: kill crabs for cash, spend $5 at a roaming crate to
// upgrade through 5 weapons (.357/P90/AK47/RPG/MG42), survive an
// ever-growing crab population (a periodic tougher "boss" crab spawns every
// few kills, the interval shrinking over time) for as long as possible.
// Upstream's own readme describes it plainly: "kill crabs to make money to
// buy weapons to kill crabs to make money, etc."
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment - this
// dialect has no classes/methods). Every global/function got a `crab`-
// prefixed name (this cartridge has one flat namespace across 43 other
// already-ported games). `byte`/`char`/`boolean` all became plain `int`/
// `bool` (no byte-width types in this dialect); `int16_t`/`uint8_t`
// parameters became plain `int`. `random(N)` -> `arand(N)`; `random(a,b)`
// (Arduino's own exclusive-upper-bound ranged form) -> `a + arand(b - a)`.
// `PROGMEM`/`pgm_read_byte()` are dropped outright (already no-ops in this
// shim - see avrCompat.h). `switch(dir)`/`switch(gb.menu(...))` both became
// if/else-if chains (this port's own established caution around this
// dialect's switch support, even though several already-shipped games do
// use `switch` successfully - matching gameShipwrek.c/gameUfoRace.c's own
// choice here). Real C++ reference parameters (`int&`, `byte&` in
// `screenCoord()`/`moveXYDS()`) have no equivalent in this dialect -
// `screenCoord()` became an int* out-parameter pair (the same b2Vec2-style
// "out-pointer" idiom this project's own VIRCON32_C_DIALECT.md documents,
// already proven here via gameSkibuino.c's own `skiGetSpriteWH()`);
// `moveXYDS()`'s `x`/`y` out-params became a `crabMoveXYDS(int* x, int* y,
// int dir, int speed)` signature, with every array-element call site
// (mobs/bullets) using a local copy-in/copy-out pair rather than taking a
// direct `&array[i]` address, purely as a defensive, unnecessary-risk-
// avoidance choice (address-of a plain scalar, e.g. `&crabPlayerX`, is used
// directly since that's a proven-safe pattern already).
//
// MULTI-TAB CONSOLIDATION: upstream ships 7 real .ino tabs (Crabator.ino,
// bullets.ino, crate.ino, mobs.ino, pause_menu.ino, play.ino, sprites.ino,
// world.ino - Arduino concatenates them alphabetically into one real
// translation unit, exactly like every other multi-tab game already ported
// here). Reordered here (world -> splash/bullets helpers -> mobs -> bullets/
// shooting -> crate -> score/EEPROM -> state machine) purely so every
// function is defined before its first real call site in this dialect's
// single top-to-bottom translation unit (no forward prototypes needed) -
// `mobs.ino`'s own `damageMob()` calls `bullets.ino`'s own `setSplash()`,
// for instance, so the bullets/splash helpers had to move earlier. This is
// a pure reordering, not a behavior change.
//
// REAL BITMAP ART RESTORED VERBATIM: all 10 real PROGMEM bitmaps from
// sprites.ino (the 64x36 `logo`, the 16x12-bit `world` tile bitmask, the
// 8x8 `tiles`/`mobSprite`/`bossSprite`/`playerSprite`/`splashSprite`/
// `crateSprite`/`fullHeart`/`halfHeart`/`emptyHeart`) were converted from
// upstream's own real `B########`-style Arduino binary literals to this
// project's own `0x` hex convention via a one-off Python script that
// parsed sprites.ino directly and converted every literal byte-for-byte
// (not hand-transcribed or guessed), matching this project's own
// established precedent (gameShipwrek.c/gameUfoRace.c). Every real
// `gb.display.drawBitmap(...)` call site has a direct `gbDrawBitmap()`/
// `gbDrawBitmapRotated()` counterpart - checked for the "upstream draws a
// mask/fill layer first" bleed-through bug class this project's own
// history warns about, and it does NOT apply here: every one of these 10
// sprites is a complete, self-contained tile/icon drawn onto a screen
// `gbUpdate()` has just freshly cleared, with nothing else underneath to
// bleed through.
//
// STATE MACHINE CONVERSION: upstream's real control flow is a nest of
// blocking calls - `setup()` shows a real blocking `gb.titleScreen(logo)`
// once, then calls `initGame()`; `loop()` (called repeatedly by the Arduino
// runtime) immediately calls `pause()` (a real blocking `gb.menu(...)`-
// driven pause menu, NOT gameplay), which dispatches via `switch` to
// `play()` (itself a blocking `while(true)` loop, exited only by a real
// Button-C press), `displayHighScores()`, or a small blocking System Info
// loop. All of this was flattened into an explicit `CrabState` enum
// (TITLE/MENU/PLAY/HIGHSCORES/SYSINFO) dispatched from
// `gameCrabator_update()`, matching the "blocking loop -> explicit
// resumable state" treatment used throughout this project (see
// gamePong.c's own header comment). `CRAB_STATE_PLAY` itself has 4 real
// sub-phases (`CrabPlayPhase`) mirroring upstream's own real internal
// structure exactly: `CRAB_PLAY_INTRO` (`play()`'s own real "LET'S GO!"
// countdown, `i<10`, fontSize 2, run once every real `play()` call),
// `CRAB_PLAY_NORMAL` (the real main `while(true){ if(gb.update()){...} }`
// body), `CRAB_PLAY_GAMEOVER_ANIM` (the real white-wipe-bars-closing-in
// `while(1)` block, `timer` 0 to `LCDWIDTH/4+10`), and
// `CRAB_PLAY_GAMEOVER_STATS` (the real final "GAME OVER!"/"NEW HIGHSCORE"
// + stats `while(1)` block, waiting for Button A).
//
// TWO GENUINE, SUBTLE UPSTREAM QUIRKS PRESERVED IN THE STATE MACHINE:
// 1. Upstream's real `pause()` switch has an asymmetry in how its cases
//    exit: case 0 ("Play") and case 2/3/4 all `break` (looping back to
//    re-show the SAME pause menu once `play()`/the sub-screen returns),
//    but case 1 ("Restart") ends with a real `return;` - exiting `pause()`
//    entirely, so control falls through to `loop()`'s own trailing
//    `gb.titleScreen(logo);` call (a real blocking title-screen wait for a
//    fresh A-press) BEFORE the pause menu is ever seen again. So pressing
//    Button C mid-game after choosing "Play" returns straight to the pause
//    menu, but after choosing "Restart" it detours through the title splash
//    first. Ported via `crabPlayCReturnsTo` (set to `CRAB_STATE_MENU` for
//    "Play", `CRAB_STATE_TITLE` for "Restart" - and `CRAB_STATE_TITLE` is
//    ALWAYS dismissed by a fresh A-press straight back into
//    `CRAB_STATE_MENU`, matching every real path that shows the title
//    splash mid-session: the initial boot, this "Restart" quirk, the
//    "Main Menu" pause item, and Button C cancelling the pause menu itself
//    - real upstream's own `default: return;` case in the same switch,
//    interpreted here as this dialect's own explicit Button-C-cancels
//    gesture since no real `gb.menu()` cancel semantics could be confirmed
//    directly).
// 2. "Play" (case 0) never calls `initGame()` - it just resumes whatever
//    game state already exists - while "Restart" (case 1) always calls
//    `initGame()` first. Both call `play()`, and `play()`'s own real
//    "LET'S GO!" intro + `gb.popup("\x15:shoot \x16:run", 60)` hint run
//    ONCE at the very top of every real `play()` call, unconditionally -
//    but a death-triggered in-place restart (the real `initGame(); break;`
//    at the end of the game-over stats screen, which breaks only the inner
//    `for(thisMob...)` loop, never actually returning from `play()` itself)
//    stays inside the SAME `play()` call and therefore never replays the
//    intro. Ported via `crabMenuSelectPlay()`/`crabMenuSelectRestart()`
//    (both set `crabPlayPhase = CRAB_PLAY_INTRO`) vs. the game-over-stats
//    accept handler and `crabDismissHighscores()`'s own "continue" branch
//    (both set `crabPlayPhase = CRAB_PLAY_NORMAL` directly, skipping the
//    intro).
//
// EEPROM - A REAL HIGHSCORE CONSUMER: upstream's own `loadHighscore()`/
// `saveHighscore()` genuinely call `EEPROM.read()`/`EEPROM.write()` (a real
// top-5 table, RANKMAX=5) - ported via this shim's own `eeprom_read_byte()`/
// `eeprom_write_byte()` (2 bytes/entry, LSB then MSB, 5 entries - simpler
// than upstream's own real 12-bytes/entry layout, which also packs a
// 10-character name per entry; only upstream's real *behavior*, a
// persisted, sorted score table, needs to survive a reboot here, not a
// bit-for-bit-identical EEPROM layout, matching this project's own
// established norm). Upstream's own real fresh-EEPROM sentinel handling -
// `highscore[i] = (highscore[i]==0xFFFF) ? 0 : highscore[i];` - already
// matches this shim's own real fresh-EEPROM behavior (unwritten cells read
// as 0xFF, so a fresh entry decodes to exactly 0xFFFF) with zero adaptation
// needed, unlike gameUfoRace.c's own different lower-is-better 9999
// sentinel choice for a lap-time table.
//
// NAME ENTRY - DROPPED, DOCUMENTED: upstream's own `gb.getDefaultName(...)`
// + `gb.keyboard(...)` (a real on-screen text-entry widget, called from
// `saveHighscore()` right before it shows the highscore table) has no
// equivalent anywhere in this shim (no on-screen-keyboard primitive exists
// at all) - dropped entirely, matching gameUfoRace.c's/gameShipwrek.c's own
// identical precedent: `crabHighscore[]` is a plain numeric-only table, no
// name column at all, rather than a fabricated placeholder name.
//
// SOUND - REAL, FULL RESTORATION: upstream's own real `Sound::
// playPattern()` calls (the per-weapon fire sound - `magnum_sound`/
// `p90_sound`/`ak47_sound`/`rpg_sound`/`mg42_sound` plus the P90's own
// `p90_alternative_sound` - and `blast_sound`, `mob_death_sound`,
// `player_damage_sound`, `power_up`) are ported verbatim onto this shim's
// real pattern-engine primitive (`gbPlayPattern()`), not approximated -
// every real upstream tracker-envelope hex/decimal value was copied
// byte-for-byte from `Crabator.ino` into a `crab*Sound` array here (each
// array's own element count, including its real 0x0000 terminator, was
// checked against upstream's own literal array before trusting it), and
// all 7 real upstream `playPattern()` call sites (weapon fire, the P90
// alternate waveform, x2 blast, mob death, player damage, power-up) are
// restored - none dropped. `weapons_sounds[]` (a real PROGMEM array of
// PROGMEM pointers upstream) is ported as an if/else lookup function,
// `crabWeaponSoundFor()`, returning the matching pattern array directly -
// the same treatment `crabWeaponName()` above already uses for the
// analogous `weapon_name[]` array-of-string-pointers, since a genuine
// `int*[N]` array-of-array-pointers remains unverified in this dialect
// (see that function's own comment). Every real call uses channel 0,
// matching upstream exactly - upstream never calls `changePatternSet()`/
// `changeInstrumentSet()` anywhere in this game, so channel 0 keeps this
// shim's own real default square+noise instrument pair throughout, same
// as real hardware's own default. Upstream's own real per-tick throttle
// for the two automatic weapons (`if(((currentWeapon==1)||(currentWeapon
// ==4))&&(gb.frameCount%2)){ /* skip */ } else { playPattern(...); }` -
// silences the fire sound on every other real tick for P90/MG42, to
// avoid a continuous-beep effect) and the P90-specific
// `p90_alternative_sound` cancel-every-other-shot waveform swap
// (`if(currentWeapon==1){ if(random()%2) playPattern(p90_alternative_
// sound,0); }`, ported as `arand(2)`) are both restored exactly.
//
// SYSTEM INFO - PARTIALLY HONEST, PARTIALLY REAL: upstream's own System
// Info pause-menu screen reads real hardware telemetry this shim has no
// equivalent for at all (`gb.battery.voltage/level`, `gb.backlight.
// ambientLight/backlightValue`, `gb.sound.getVolume()/volumeMax`) - shown
// here as a short, honest "No sensors emulated here" placeholder instead
// of fabricated numbers, matching gameUfoRace.c's own established
// precedent for the same situation. Unlike that game, though, upstream's
// own System Info screen ALSO prints two real, available-here values
// (`activeMobs`/`NUMMOBS`, `kills`) - those two lines are genuine, live
// data in this port too, not placeholders.
//
// OTHER REAL QUIRKS FOUND, PRESERVED (not "fixed") - fidelity to upstream:
// - `crabBlastBullet` (upstream `blast_bullet`) is a single shared global
//   set UNCONDITIONALLY by every real call to `shoot()` (any weapon, not
//   just the RPG) - so if a different weapon is fired while an earlier RPG
//   round is still in flight, that RPG's own eventual AOE explosion damage/
//   recoil (`damageMob(thisMob, blast_bullet)` inside `explode()`) can end
//   up using the LATER weapon's own damage/recoil stats instead of the
//   RPG's own. A genuine, if obscure, real upstream quirk (not a porting
//   mistake), preserved verbatim rather than tracking a per-explosion
//   weapon id upstream itself never bothered to.
// - The life-hearts drawing loop (`for(i=0; i<=playerLifeMax/2; i+=1)`)
//   always burns one throwaway iteration at `i==0` that draws entirely
//   off-screen (`LCDWIDTH - 0*9 + 2 = 86 > LCDWIDTH`) before the 3 real
//   visible heart icons at `i==1,2,3` - preserved exactly (not "fixed" into
//   starting at `i==1`), a real, harmless upstream artifact.
// - `crabResetGameState()` (upstream `initGame()`) never resets
//   `playerDir`/`playerSpeed`/`shake_magnitude` - only the exact fields
//   upstream's own real `initGame()` resets are reset here too.
// - `cameraY = playerY - LCDHEIGHT/2 + playerW/2;` reuses `playerW` (not
//   `playerH`) for the Y-axis offset in upstream's own real `initGame()` -
//   inert here since `playerW == playerH == 6`, but ported literally
//   (`CRAB_PLAYER_W`, not `CRAB_PLAYER_H`) rather than "corrected."
// - Upstream's own real `byteWidth = (WORLD_H + 7) / 8` sizes a per-row
//   byte stride off the map's HEIGHT constant where it should logically
//   depend on its WIDTH (16 columns = 2 bytes/row) - numerically identical
//   here only because `(12+7)/8` also truncates to 2, so this is a real but
//   totally inert upstream mismatch for these exact dimensions. Ported as a
//   fixed `CRAB_BYTE_WIDTH` constant (not the misleading formula itself,
//   which would wrongly suggest the map's real per-row byte stride tracks
//   its height).
// - `explode()`'s own real `random(-4,4)` (Arduino's exclusive-upper-bound
//   ranged form, giving the asymmetric range [-4,3], not a symmetric
//   [-4,4]) is ported as the equivalent `-4 + arand(8)` - preserving the
//   real asymmetry rather than "rounding it out" to a symmetric range.
// - Upstream's own `assignArray()` (declared in Crabator.ino) is never
//   called anywhere in the source actually read for this port - dropped
//   outright as real, provably-dead code, matching this project's own
//   established norm (e.g. gameShipwrek.c's own dropped `uselessN`
//   globals).
// - `shakeScreen()`'s own real backlight-flicker line (`gb.backlight.
//   set(...)`) was dropped outright - no backlight primitive exists
//   anywhere in this shim (a real-hardware-only cosmetic feature, the same
//   treatment `gb.battery.show` already got in gamePong.c).

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------

#define CRAB_WORLD_W 16
#define CRAB_WORLD_H 12
#define CRAB_BYTE_WIDTH 2 // see header comment - upstream's own real (WORLD_H+7)/8, numerically 2 for these exact dimensions

#define CRAB_PLAYER_W 6
#define CRAB_PLAYER_H 6
#define CRAB_PLAYER_LIFE_MAX 6

#define CRAB_NUM_MOBS 16
#define CRAB_INIT_NUM_MOBS 4
#define CRAB_MOBS_RATE 6   // every N kills, one more mob slot becomes active
#define CRAB_BOSS_FREQ_INIT 16
#define CRAB_BOSS_RATE 1
#define CRAB_MOB_MAX_LIFE 10
#define CRAB_BOSS_MAX_LIFE 100
#define CRAB_BOSS_SIZE 6
#define CRAB_MOB_SIZE 4

#define CRAB_NUM_SPLASH 16
#define CRAB_NUM_BULLETS 10
#define CRAB_NUM_WEAPONS 5
#define CRAB_RANK_MAX 5

enum CrabState
{
    CRAB_STATE_TITLE = 0,
    CRAB_STATE_MENU = 1,
    CRAB_STATE_PLAY = 2,
    CRAB_STATE_HIGHSCORES = 3,
    CRAB_STATE_SYSINFO = 4
};

enum CrabPlayPhase
{
    CRAB_PLAY_INTRO = 0,
    CRAB_PLAY_NORMAL = 1,
    CRAB_PLAY_GAMEOVER_ANIM = 2,
    CRAB_PLAY_GAMEOVER_STATS = 3
};

enum CrabHighscoreReturn
{
    CRAB_HS_RETURN_MENU = 0,
    CRAB_HS_RETURN_PLAY_FRESH = 1,   // -> a fresh play() session (Restart path) - phase INTRO
    CRAB_HS_RETURN_PLAY_CONTINUE = 2 // -> continuing the same play() session (death path) - phase NORMAL
};

#define CRAB_MENU_COUNT 5

// -----------------------------------------------------------------------
// Real upstream bitmaps - byte-for-byte converted from sprites.ino's own
// real B-binary literals (see header comment on how). {width,height}
// header preserved exactly, matching gbDrawBitmap()'s own expected format.
// -----------------------------------------------------------------------

int crabLogo[290]=
{
    64, 36,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x80,0x0,0x0,0x0,0x0,0x1,0x41,0x41,
    0x80,0x0,0x1,0x80,0x6,0x4,0x14,0x11,
    0x80,0x0,0x1,0x80,0x6,0x0,0x80,0x81,
    0x80,0x0,0x1,0x80,0x6,0x0,0x0,0x41,
    0x8F,0x3F,0x79,0xB9,0xEF,0x9E,0x7E,0x11,
    0x99,0xBB,0xCD,0xFB,0x36,0x33,0x76,0x21,
    0x99,0xB3,0xD,0x98,0x36,0x33,0x66,0x45,
    0x98,0x30,0x7D,0x99,0xF6,0x33,0x60,0x3,
    0x98,0x30,0xCD,0x9B,0x36,0x33,0x61,0x41,
    0x98,0x30,0xCD,0x9B,0x36,0x33,0x64,0x15,
    0x99,0xB0,0xDD,0x9B,0x76,0x33,0x60,0x89,
    0xCF,0x30,0xFD,0xF3,0xF3,0x9E,0x60,0x41,
    0x80,0x0,0x0,0x0,0x0,0x0,0x5,0x15,
    0xA0,0x0,0x0,0x0,0x0,0x20,0x80,0xA1,
    0xC0,0x0,0x18,0xC,0x0,0x0,0xF,0xF5,
    0x90,0x0,0x24,0x52,0x80,0xF0,0x8,0x13,
    0xD0,0x2,0xAC,0x56,0x81,0xF8,0xF,0xF1,
    0x99,0x43,0x34,0xDA,0xE1,0xF8,0xA,0x35,
    0x95,0x42,0x25,0x52,0x81,0xF8,0xC,0x59,
    0xDD,0xC2,0x19,0xCC,0x61,0x68,0xF,0xF1,
    0x80,0x40,0x0,0x0,0x0,0xF0,0x8,0x15,
    0x80,0x80,0x0,0x0,0x0,0x0,0xF,0xF1,
    0xC0,0x0,0x0,0x0,0x6,0x0,0x0,0x5,
    0x80,0x0,0x0,0x0,0xF,0x0,0x0,0x3,
    0xC0,0x0,0x0,0x0,0x6,0x0,0x0,0x1,
    0x90,0x0,0x0,0x0,0x9,0x0,0x0,0x5,
    0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x9,
    0xC1,0x40,0x0,0x0,0x0,0x0,0x0,0x1,
    0x93,0xD0,0x0,0x0,0x0,0x0,0x0,0x5,
    0xA3,0xE0,0x0,0x0,0x0,0x6,0x0,0x1,
    0x83,0xE0,0x5,0x45,0x40,0xF,0x5,0x45,
    0x83,0xD0,0x2,0x2,0x0,0x6,0x2,0x3,
    0x81,0x40,0x1,0x41,0x40,0x9,0x1,0x41,
    0x80,0x0,0x4,0x14,0x10,0x0,0x4,0x15,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

int crabWorld[24]=
{
    0xCF,0xCF, 0x80,0x40, 0x01,0xFC, 0x39,0xE0,
    0x0C,0x06, 0x87,0x1F, 0xC3,0xC1, 0x81,0x01,
    0x3C,0x33, 0x07,0xE0, 0x84,0x09, 0xC1,0xCF,
};

int crabTileBitmap[10]= { 8,8, 0x54,0x20,0x14,0x41,0x88,0x04,0x51,0x0A };
int crabMobSprite[10]= { 8,8, 0x00,0x00,0x18,0x3C,0x18,0x24,0x00,0x00 };
int crabBossSprite[10]= { 8,8, 0x00,0x24,0x18,0x7E,0x3C,0x7E,0x3C,0x00 };
int crabPlayerSprite[10]= { 8,8, 0x00,0x3C,0x5A,0x7E,0x7E,0x7E,0x3C,0x00 };
int crabSplashSprite[10]= { 8,8, 0x00,0x08,0x00,0x18,0x38,0xB4,0x00,0x10 };
int crabCrateSprite[10]= { 8,8, 0xFF,0x81,0xFF,0xA3,0xC5,0xFF,0x81,0xFF };
int crabFullHeart[10]= { 8,8, 0x6C,0xFE,0xFE,0x7C,0x38,0x10,0x00,0x00 };
int crabHalfHeart[10]= { 8,8, 0x00,0x0C,0x1C,0x18,0x10,0x00,0x00,0x00 };
int crabEmptyHeart[10]= { 8,8, 0x6C,0x92,0x82,0x44,0x28,0x10,0x00,0x00 };

// Real icon-glyph hint strings (\x15/\x21 and \x16/\x22 are real Gamebuino
// low-ASCII icon glyphs, from the same custom-icon range gameTaquin.c's own
// "restart" arrow already restored) - built as explicit int[] arrays since
// a quoted string literal can't hold a non-printable code directly (see
// gameSimonbuino.c's own header comment on the identical need).
char crabShootRunHint[14]= { 21,58,115,104,111,111,116,32, 22,58,114,117,110, 0 }; // "\x15:shoot \x16:run"
char crabAcceptHint[9]= { 21,58,97,99,99,101,112,116, 0 };                      // "\x15:accept"

// -----------------------------------------------------------------------
// Weapon tables - direct port of Crabator.ino's own real per-weapon arrays
// (index 0..4 = .357/P90/AK47/RPG/MG42, matching upstream exactly).
// -----------------------------------------------------------------------

int crabWeaponSize[CRAB_NUM_WEAPONS]= { 2, 1, 2, 3, 2 };
int crabWeaponDamage[CRAB_NUM_WEAPONS]= { 10, 2, 3, 5, 4 };
int crabWeaponRate[CRAB_NUM_WEAPONS]= { 30, 1, 2, 30, 1 };
int crabWeaponSpeed[CRAB_NUM_WEAPONS]= { 4, 5, 3, 2, 5 };
int crabWeaponSpread[CRAB_NUM_WEAPONS]= { 1, 2, 1, 0, 2 };
int crabWeaponEnemyRecoil[CRAB_NUM_WEAPONS]= { 3, 2, 3, 0, 3 };
int crabWeaponPlayerRecoil[CRAB_NUM_WEAPONS]= { 0, 0, 1, 3, 3 };
int crabWeaponAmmo[CRAB_NUM_WEAPONS]= { 9999, 500, 300, 20, 150 };

// Real upstream `weapon_name[]` (a PROGMEM array of PROGMEM string
// pointers) ported as an if/else lookup rather than an `int*[N]` array of
// string-literal pointers - that exact pattern (as opposed to the already-
// proven `int*[N]` array-of-*bitmap*-pointers, e.g. gameUfoRace.c's own
// `ufoSprites`) remains unverified in this dialect per gameGlaciGlaca.c's
// own header comment - matching gameShipwrek.c's own identical
// `shipBoatNameFor()`/`shipPName()` precedent.
char* crabWeaponName( int i )
{
    if( i == 0 ) return ".357";
    if( i == 1 ) return "P90";
    if( i == 2 ) return "AK47";
    if( i == 3 ) return "RPG";
    return "MG42";
}

// -----------------------------------------------------------------------
// Sound pattern data - direct, byte-for-byte port of Crabator.ino's own
// real tracker-envelope arrays (see gamebuinoShim.h's own Sound section
// for gbPlayPattern()'s real format - a plain 0-terminated uint16 command/
// note array). Every element, including the real 0x0000 terminator, was
// checked against upstream's own literal array's own declared element
// count before trusting it.
// -----------------------------------------------------------------------

int crabMagnumSound[6]          = { 0x0045, 0x7049, 0x017C, 0x784D, 0x042C, 0x0000 };
int crabP90Sound[3]             = { 0x0045, 0x0154, 0x0000 };
int crabP90AlternativeSound[3]  = { 0x0045, 0x014C, 0x0000 };
int crabAk47Sound[3]            = { 0x0045, 0x012C, 0x0000 };
int crabMg42Sound[7]            = { 0x0045, 0x0140, 0x8141, 0x7849, 0x788D, 0x052C, 0x0000 };
int crabRpgSound[4]             = { 0x0045, 0x8101, 0x7F30, 0x0000 };
int crabBlastSound[5]           = { 0x0045, 0x7849, 0x784D, 0x0A28, 0x0000 };
int crabPowerUpSound[17]        = { 0x0005, 0x0140, 0x0150, 0x015C, 0x0170, 0x0180, 0x016C, 0x0154,
                                     0x0160, 0x0174, 0x0184, 0x014C, 0x015C, 0x0168, 0x017C, 0x018C, 0x0000 };
int crabMobDeathSound[3]        = { 0x0045, 0x0184, 0x0000 };
int crabPlayerDamageSound[3]    = { 0x0045, 0x0564, 0x0000 };

// -----------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------

int crabPlayerSpeed;
int crabPlayerX;
int crabPlayerY;
int crabPlayerLife;
int crabPlayerDir;
int crabCameraX;
int crabCameraY;
int crabShakeMagnitude;
int crabShakeTimeLeft;

int crabMobsX[CRAB_NUM_MOBS];
int crabMobsY[CRAB_NUM_MOBS];
int crabMobsDir[CRAB_NUM_MOBS];
int crabMobsLife[CRAB_NUM_MOBS];
int crabMobsSize[CRAB_NUM_MOBS];
int crabBossNextSpawn;
int crabBossFreq;
int crabActiveMobs;

bool crabSplashActive[CRAB_NUM_SPLASH];
int crabSplashX[CRAB_NUM_SPLASH];
int crabSplashY[CRAB_NUM_SPLASH];
int crabSplashDir[CRAB_NUM_SPLASH];

int crabBulletsX[CRAB_NUM_BULLETS];
int crabBulletsY[CRAB_NUM_BULLETS];
int crabBulletsDir[CRAB_NUM_BULLETS];
bool crabBulletsActive[CRAB_NUM_BULLETS];
int crabBulletsWeapon[CRAB_NUM_BULLETS];

int crabBlastX;
int crabBlastY;
int crabBlastLifespan;
int crabBlastBullet; // see header comment - a single shared "last fired bullet" id, a real preserved upstream quirk

int crabCurrentWeapon;
int crabNextShot;
int crabAmmo;

int crabCrateX;
int crabCrateY;

int crabScore;
int crabKills;
int crabHighscore[CRAB_RANK_MAX];

int crabScreenX; // player's own last-computed on-screen position, reused unchanged across GAMEOVER_ANIM/STATS ticks (see header comment)
int crabScreenY;

int crabState;
int crabMenuIndex;
int crabPlayPhase;
int crabPlayIntroCounter;
int crabGameOverTimer;
int crabPlayCReturnsTo;   // CRAB_STATE_MENU or CRAB_STATE_TITLE - see header comment quirk 1
int crabHighscoreReturn;

// -----------------------------------------------------------------------
// Small math/movement helpers - direct ports of Crabator.ino's own real
// wrap()/distanceBetween()/moveXYDS()/screenCoord().
// -----------------------------------------------------------------------

int crabWrap( int i, int imax )
{
    return ( imax + i ) % imax;
}

int crabDistanceBetween( int pos1, int pos2, int worldSize )
{
    int dist = gbAbsInt( pos1 - pos2 );
    if( dist < worldSize / 2 ) return dist;
    return worldSize - dist;
}

// Real upstream `moveXYDS(int &x, int &y, byte &dir, char speed)` - `dir`
// is only ever READ inside the real function body, never written, so it
// stays a plain by-value `int` parameter here (see header comment).
void crabMoveXYDS( int* x, int* y, int dir, int speed )
{
    if( dir == 0 ) *y = *y - speed;
    else if( dir == 1 ) *x = *x - speed;
    else if( dir == 2 ) *y = *y + speed;
    else if( dir == 3 ) *x = *x + speed;

    *x = crabWrap( *x, CRAB_WORLD_W * 8 );
    *y = crabWrap( *y, CRAB_WORLD_H * 8 );
}

// Real upstream `screenCoord(int absoluteX, int absoluteY, int &x, int &y)`
// - real ternaries rewritten as if/else; the missing lower-bound check
// (only `x > LCDWIDTH || y > LCDHEIGHT` is tested, never `< 0`) is a real,
// load-bearing part of upstream's own off-screen culling contract (several
// callers intentionally draw at a small negative offset, e.g. `x-2,y-2`)
// and is preserved exactly.
bool crabScreenCoord( int absoluteX, int absoluteY, int* outX, int* outY )
{
    int x = absoluteX - crabCameraX + 8;
    if( x >= 0 ) x = x % ( CRAB_WORLD_W * 8 );
    else x = CRAB_WORLD_W * 8 + x % ( CRAB_WORLD_W * 8 );
    x = x - 8;

    int y = absoluteY - crabCameraY + 8;
    if( y >= 0 ) y = y % ( CRAB_WORLD_H * 8 );
    else y = CRAB_WORLD_H * 8 + y % ( CRAB_WORLD_H * 8 );
    y = y - 8;

    *outX = x;
    *outY = y;

    if( x > LCDWIDTH || y > LCDHEIGHT ) return false;
    return true;
}

// -----------------------------------------------------------------------
// World - direct ports of world.ino's own real getTile()/drawWorld()/
// collideWorld() (all three callers only ever pass non-negative i/j/x/y in
// practice - see header comment - so no defensive clamp is added, matching
// upstream's own unguarded real design).
// -----------------------------------------------------------------------

bool crabGetTile( int i, int j )
{
    int idx = ( j % CRAB_WORLD_H ) * CRAB_BYTE_WIDTH + ( i % CRAB_WORLD_W ) / 8;
    if( crabWorld[ idx ] & ( 0x80 >> ( i % 8 ) ) ) return true;
    return false;
}

void crabDrawWorld( int x, int y )
{
    x = crabWrap( x, CRAB_WORLD_W * 8 );
    y = crabWrap( y, CRAB_WORLD_H * 8 );

    int i, j;
    for( j = y / 8; j < ( LCDHEIGHT / 8 + y / 8 + 1 ); j = j + 1 )
    {
        for( i = x / 8; i < ( LCDWIDTH / 8 + x / 8 + 1 ); i = i + 1 )
        {
            if( crabGetTile( i, j ) )
              gbDrawBitmap( i * 8 - x, j * 8 - y, crabTileBitmap );
        }
    }
}

bool crabCollideWorld( int x, int y, int w, int h )
{
    if( crabGetTile( x / 8, y / 8 ) ) return true;
    if( crabGetTile( ( x + w - 1 ) / 8, y / 8 ) ) return true;
    if( crabGetTile( ( x + w - 1 ) / 8, ( y + h - 1 ) / 8 ) ) return true;
    if( crabGetTile( x / 8, ( y + h - 1 ) / 8 ) ) return true;
    return false;
}

// -----------------------------------------------------------------------
// Splash effects (death "poof" marks) - direct ports of bullets.ino's own
// real setSplash()/drawSplashes(), moved ahead of the mob functions below
// since damageMob() calls setSplash() (see header comment on reordering).
// -----------------------------------------------------------------------

void crabSetSplash( int x, int y )
{
    int thisSplash;
    for( thisSplash = 0; thisSplash < CRAB_NUM_SPLASH; thisSplash = thisSplash + 1 )
    {
        if( !crabSplashActive[ thisSplash ] )
        {
            crabSplashActive[ thisSplash ] = true;
            crabSplashX[ thisSplash ] = x;
            crabSplashY[ thisSplash ] = y;
            crabSplashDir[ thisSplash ] = arand( 5 );
            break;
        }
    }
}

void crabDrawSplashes()
{
    int thisSplash;
    for( thisSplash = 0; thisSplash < CRAB_NUM_SPLASH; thisSplash = thisSplash + 1 )
    {
        if( crabSplashActive[ thisSplash ] )
        {
            int x, y;
            if( crabScreenCoord( crabSplashX[ thisSplash ], crabSplashY[ thisSplash ], &x, &y ) )
              gbDrawBitmapRotated( x - 2, y - 2, crabSplashSprite, crabSplashDir[ thisSplash ], 0 );
            else
              crabSplashActive[ thisSplash ] = false;
        }
    }
}

// -----------------------------------------------------------------------
// Mobs - direct ports of mobs.ino's own real spawnMob()/spawnMobs()/
// moveMobs()/checkMobCollisions()/collideOtherMobs()/drawMobs()/
// damageMob().
// -----------------------------------------------------------------------

bool crabCheckMobCollisions( int thisMob );
bool crabSpawnMob( int thisMob );

bool crabCollideOtherMobs( int thisMob )
{
    int otherMob;
    for( otherMob = 0; otherMob < crabActiveMobs; otherMob = otherMob + 1 )
    {
        if( thisMob == otherMob ) continue;
        if( gbCollideRectRect( crabMobsX[ thisMob ], crabMobsY[ thisMob ], crabMobsSize[ thisMob ], crabMobsSize[ thisMob ],
                                crabMobsX[ otherMob ], crabMobsY[ otherMob ], crabMobsSize[ otherMob ], crabMobsSize[ otherMob ] ) )
          return true;
    }
    return false;
}

bool crabCheckMobCollisions( int thisMob )
{
    if( crabCollideWorld( crabMobsX[ thisMob ], crabMobsY[ thisMob ], crabMobsSize[ thisMob ], crabMobsSize[ thisMob ] ) )
      return true;
    if( crabCollideOtherMobs( thisMob ) )
      return true;
    return false;
}

bool crabSpawnMob( int thisMob )
{
    crabMobsSize[ thisMob ] = CRAB_MOB_SIZE;
    crabMobsLife[ thisMob ] = CRAB_MOB_MAX_LIFE;
    if( !crabBossNextSpawn )
    {
        crabBossFreq = gbMax( crabBossFreq - CRAB_BOSS_RATE, 1 );
        crabBossNextSpawn = crabBossFreq;
        crabMobsSize[ thisMob ] = CRAB_BOSS_SIZE;
        crabMobsLife[ thisMob ] = CRAB_BOSS_MAX_LIFE;
    }

    bool okay = false;
    while( !okay )
    {
        crabMobsX[ thisMob ] = arand( CRAB_WORLD_W * 2 ) * 4;
        crabMobsY[ thisMob ] = arand( CRAB_WORLD_H * 2 ) * 4;
        okay = true;

        if( crabCheckMobCollisions( thisMob ) ) { okay = false; continue; }
        if( crabWrap( crabMobsX[ thisMob ] - crabCameraX, CRAB_WORLD_W * 8 ) < LCDWIDTH ) { okay = false; continue; }
        if( crabWrap( crabMobsY[ thisMob ] - crabCameraY, CRAB_WORLD_H * 8 ) < LCDHEIGHT ) { okay = false; continue; }
    }

    crabMobsDir[ thisMob ] = arand( 4 );
    return true;
}

// Upstream's own real return value is always true (no path inside
// spawnMob() ever returns false) - preserved as a harmless, inert
// early-return check anyway, matching this project's own norm of
// preserving dead-but-harmless upstream logic rather than simplifying it
// away (see gameUfoRace.c's own identical `initHighscore()` precedent).
bool crabSpawnMobs()
{
    int thisMob;
    for( thisMob = 0; thisMob < crabActiveMobs; thisMob = thisMob + 1 )
    {
        crabMobsX[ thisMob ] = 9999;
        crabMobsY[ thisMob ] = 9999;
    }
    for( thisMob = 0; thisMob < crabActiveMobs; thisMob = thisMob + 1 )
    {
        if( !crabSpawnMob( thisMob ) ) return false;
    }
    return true;
}

void crabMoveMobs()
{
    int thisMob;
    for( thisMob = 0; thisMob < crabActiveMobs; thisMob = thisMob + 1 )
    {
        int x = crabWrap( crabMobsX[ thisMob ] - crabCameraX, CRAB_WORLD_W * 8 );
        int y = crabWrap( crabMobsY[ thisMob ] - crabCameraY, CRAB_WORLD_H * 8 );

        if( crabDistanceBetween( crabMobsX[ thisMob ], crabPlayerX, CRAB_WORLD_W * 8 ) < ( LCDWIDTH + 32 ) &&
            crabDistanceBetween( crabMobsY[ thisMob ], crabPlayerY, CRAB_WORLD_H * 8 ) < ( LCDHEIGHT + 32 ) )
        {
            int mx = crabMobsX[ thisMob ];
            int my = crabMobsY[ thisMob ];
            crabMoveXYDS( &mx, &my, crabMobsDir[ thisMob ], 1 );
            crabMobsX[ thisMob ] = mx;
            crabMobsY[ thisMob ] = my;

            if( crabCheckMobCollisions( thisMob ) )
            {
                mx = crabMobsX[ thisMob ];
                my = crabMobsY[ thisMob ];
                crabMoveXYDS( &mx, &my, crabMobsDir[ thisMob ], -1 );
                crabMobsX[ thisMob ] = mx;
                crabMobsY[ thisMob ] = my;
                crabMobsDir[ thisMob ] = arand( 4 );
                continue;
            }

            if( arand( 32 ) == 0 )
            {
                crabMobsDir[ thisMob ] = arand( 4 );
                continue;
            }

            if( arand( 16 ) == 0 )
            {
                if( arand( 2 ) )
                {
                    if( ( LCDWIDTH / 2 - x ) > 0 ) crabMobsDir[ thisMob ] = 3;
                    else crabMobsDir[ thisMob ] = 1;
                }
                else
                {
                    if( ( LCDHEIGHT / 2 - y ) > 0 ) crabMobsDir[ thisMob ] = 2;
                    else crabMobsDir[ thisMob ] = 0;
                }
            }
        }
    }
}

void crabDrawMobs()
{
    int thisMob;
    for( thisMob = 0; thisMob < crabActiveMobs; thisMob = thisMob + 1 )
    {
        int x, y;
        if( crabScreenCoord( crabMobsX[ thisMob ], crabMobsY[ thisMob ], &x, &y ) )
        {
            if( crabMobsSize[ thisMob ] != CRAB_BOSS_SIZE )
              gbDrawBitmapRotated( x - 2, y - 2, crabMobSprite, crabMobsDir[ thisMob ], 0 );
            else
              gbDrawBitmapRotated( x - 1, y - 1, crabBossSprite, crabMobsDir[ thisMob ], 0 );
        }
    }
}

// -----------------------------------------------------------------------
// Sound helpers - see header comment on the real Sound::playPattern()
// tracker envelopes, now ported verbatim onto gbPlayPattern().
// -----------------------------------------------------------------------

// Real upstream `weapons_sounds[currentWeapon]` (a PROGMEM array of
// PROGMEM pattern pointers) - see this file's own header comment for why
// this is an if/else lookup rather than an `int*[N]` array of array
// pointers.
int* crabWeaponSoundFor( int weapon )
{
    if( weapon == 0 ) return crabMagnumSound;
    if( weapon == 1 ) return crabP90Sound;
    if( weapon == 2 ) return crabAk47Sound;
    if( weapon == 3 ) return crabRpgSound;
    return crabMg42Sound; // weapon == 4
}

void crabPlayWeaponSound( int weapon ) { gbPlayPattern( crabWeaponSoundFor( weapon ), 0 ); }
void crabPlayBlastSound() { gbPlayPattern( crabBlastSound, 0 ); }
void crabPlayMobDeathSound() { gbPlayPattern( crabMobDeathSound, 0 ); }
void crabPlayDamageSound() { gbPlayPattern( crabPlayerDamageSound, 0 ); }
void crabPlayPowerUpSound() { gbPlayPattern( crabPowerUpSound, 0 ); }

// damageMob() - direct port of mobs.ino's own real function. Placed after
// the sound helpers (needs crabPlayMobDeathSound()) and crabSetSplash()
// (needs the splash helpers above) - see header comment on reordering.
void crabDamageMob( int thisMob, int thisBullet )
{
    crabMobsLife[ thisMob ] = crabMobsLife[ thisMob ] - crabWeaponDamage[ crabBulletsWeapon[ thisBullet ] ];

    int recoil = crabWeaponEnemyRecoil[ crabBulletsWeapon[ thisBullet ] ];
    if( crabMobsSize[ thisMob ] == CRAB_BOSS_SIZE ) recoil = recoil / 4;

    int mx = crabMobsX[ thisMob ];
    int my = crabMobsY[ thisMob ];
    crabMoveXYDS( &mx, &my, crabBulletsDir[ thisBullet ], recoil );
    crabMobsX[ thisMob ] = mx;
    crabMobsY[ thisMob ] = my;

    if( crabCheckMobCollisions( thisMob ) )
    {
        mx = crabMobsX[ thisMob ];
        my = crabMobsY[ thisMob ];
        crabMoveXYDS( &mx, &my, crabBulletsDir[ thisBullet ], -recoil );
        crabMobsX[ thisMob ] = mx;
        crabMobsY[ thisMob ] = my;
    }
    crabMobsDir[ thisMob ] = ( crabBulletsDir[ thisBullet ] + 2 ) % 4;

    if( crabMobsLife[ thisMob ] <= 0 )
    {
        crabScore = crabScore + 1;
        crabKills = crabKills + 1;
        crabBossNextSpawn = crabBossNextSpawn - 1;
        if( crabBulletsWeapon[ thisBullet ] != 3 ) crabPlayMobDeathSound();
        if( crabMobsSize[ thisMob ] == CRAB_BOSS_SIZE ) crabScore = crabScore + 4;

        crabSetSplash( crabMobsX[ thisMob ], crabMobsY[ thisMob ] );

        int x, y;
        if( crabScreenCoord( crabMobsX[ thisMob ], crabMobsY[ thisMob ], &x, &y ) )
          gbFillRect( x - 1, y - 1, crabMobsSize[ thisMob ] + 1, crabMobsSize[ thisMob ] + 1 );

        crabSpawnMob( thisMob );
        if( crabActiveMobs < CRAB_NUM_MOBS )
        {
            if( crabActiveMobs < ( crabKills / CRAB_MOBS_RATE ) + CRAB_INIT_NUM_MOBS )
            {
                crabActiveMobs = crabActiveMobs + 1;
                crabSpawnMob( crabActiveMobs - 1 );
            }
        }
    }
}

// -----------------------------------------------------------------------
// Bullets/shooting - direct ports of bullets.ino's own real shoot()/
// moveBullets()/explode()/drawBullets()/drawAmmoOverlay().
// -----------------------------------------------------------------------

void crabShoot()
{
    if( crabAmmo )
    {
        if( crabNextShot == 0 )
        {
            int thisBullet;
            for( thisBullet = 0; thisBullet < CRAB_NUM_BULLETS; thisBullet = thisBullet + 1 )
            {
                if( !crabBulletsActive[ thisBullet ] )
                {
                    crabBulletsActive[ thisBullet ] = true;
                    crabBulletsWeapon[ thisBullet ] = crabCurrentWeapon;

                    crabNextShot = crabWeaponRate[ crabCurrentWeapon ];
                    crabAmmo = crabAmmo - 1;

                    int spreadMax = crabWeaponSpread[ crabCurrentWeapon ];
                    int spreadMin;
                    if( crabWeaponSize[ crabCurrentWeapon ] % 2 == 0 ) spreadMin = -spreadMax;
                    else spreadMin = -spreadMax - 1;

                    crabBulletsX[ thisBullet ] = crabPlayerX + CRAB_PLAYER_W / 2 + ( spreadMin + arand( spreadMax + 1 - spreadMin ) ) - crabWeaponSize[ crabCurrentWeapon ] / 2;
                    crabBulletsY[ thisBullet ] = crabPlayerY + CRAB_PLAYER_H / 2 + ( spreadMin + arand( spreadMax + 1 - spreadMin ) ) - crabWeaponSize[ crabCurrentWeapon ] / 2;

                    crabBulletsDir[ thisBullet ] = crabPlayerDir;
                    crabBlastBullet = thisBullet; // see header comment - a real shared-global quirk, set for every weapon

                    if( ( crabCurrentWeapon == 1 || crabCurrentWeapon == 4 ) && ( gbFrameCount % 2 == 1 ) )
                    {
                        // real upstream throttle - silently skip the fire sound this tick
                    }
                    else
                      crabPlayWeaponSound( crabCurrentWeapon );

                    if( crabCurrentWeapon == 1 ) // P90 - cancel every other shot's buzz with an alternate waveform
                    {
                        if( arand( 2 ) ) gbPlayPattern( crabP90AlternativeSound, 0 );
                    }

                    int recoil = crabWeaponPlayerRecoil[ crabCurrentWeapon ];
                    crabMoveXYDS( &crabPlayerX, &crabPlayerY, crabPlayerDir, -recoil );
                    int i;
                    for( i = 0; i < recoil; i = i + 1 )
                    {
                        if( crabCollideWorld( crabPlayerX, crabPlayerY, CRAB_PLAYER_W, CRAB_PLAYER_H ) )
                          crabMoveXYDS( &crabPlayerX, &crabPlayerY, crabPlayerDir, 1 );
                        else
                          break;
                    }

                    if( crabCurrentWeapon == 4 )
                    {
                        crabShakeMagnitude = 1;
                        crabShakeTimeLeft = 2;
                    }
                    break;
                }
            }
        }
    }
    else
    {
        crabCurrentWeapon = gbMax( 0, crabCurrentWeapon - 1 );
        crabAmmo = crabWeaponAmmo[ crabCurrentWeapon ];
        crabNextShot = 20;
        gbPopup( "Out of ammo!", 30 );
    }
}

void crabMoveBullets()
{
    int thisBullet;
    for( thisBullet = 0; thisBullet < CRAB_NUM_BULLETS; thisBullet = thisBullet + 1 )
    {
        if( crabBulletsActive[ thisBullet ] )
        {
            int s = crabWeaponSize[ crabBulletsWeapon[ thisBullet ] ];
            int bx = crabBulletsX[ thisBullet ];
            int by = crabBulletsY[ thisBullet ];
            crabMoveXYDS( &bx, &by, crabBulletsDir[ thisBullet ], crabWeaponSpeed[ crabBulletsWeapon[ thisBullet ] ] );
            crabBulletsX[ thisBullet ] = bx;
            crabBulletsY[ thisBullet ] = by;

            if( crabCollideWorld( crabBulletsX[ thisBullet ], crabBulletsY[ thisBullet ], s, s ) )
            {
                crabBulletsActive[ thisBullet ] = false;
                if( crabBulletsWeapon[ thisBullet ] == 3 )
                {
                    crabBlastX = crabBulletsX[ thisBullet ];
                    crabBlastY = crabBulletsY[ thisBullet ];
                    crabBlastLifespan = 8;
                    crabPlayBlastSound();
                }
                continue;
            }

            int thisMob;
            for( thisMob = 0; thisMob < crabActiveMobs; thisMob = thisMob + 1 )
            {
                if( gbCollideRectRect( crabBulletsX[ thisBullet ], crabBulletsY[ thisBullet ], s, s,
                                        crabMobsX[ thisMob ], crabMobsY[ thisMob ], crabMobsSize[ thisMob ], crabMobsSize[ thisMob ] ) )
                {
                    if( crabBulletsWeapon[ thisBullet ] == 3 )
                    {
                        crabBlastX = crabBulletsX[ thisBullet ];
                        crabBlastY = crabBulletsY[ thisBullet ];
                        crabBlastLifespan = 8;
                        crabPlayBlastSound();
                    }
                    else
                      crabDamageMob( thisMob, thisBullet );

                    crabBulletsActive[ thisBullet ] = false;
                    break;
                }
            }
        }
    }
}

void crabExplode()
{
    if( crabBlastLifespan )
    {
        crabBlastLifespan = crabBlastLifespan - 1;
        crabShakeMagnitude = 4;
        crabShakeTimeLeft = 2;

        int s = 10 + arand( 6 );
        int x = crabBlastX + ( -4 + arand( 8 ) ) - s / 2; // real upstream random(-4,4) - see header comment on the preserved asymmetric range
        int y = crabBlastY + ( -4 + arand( 8 ) ) - s / 2;

        int thisMob;
        for( thisMob = 0; thisMob < crabActiveMobs; thisMob = thisMob + 1 )
        {
            if( gbCollideRectRect( crabMobsX[ thisMob ], crabMobsY[ thisMob ], crabMobsSize[ thisMob ], crabMobsSize[ thisMob ], x, y, s, s ) )
              crabDamageMob( thisMob, crabBlastBullet );
        }

        int xScreen, yScreen;
        if( crabScreenCoord( x, y, &xScreen, &yScreen ) )
        {
            gbSetColor( GB_INVERT );
            gbFillRect( xScreen, yScreen, s, s );
            gbSetColor( GB_BLACK );
        }
    }
}

void crabDrawBullets()
{
    int thisBullet;
    for( thisBullet = 0; thisBullet < CRAB_NUM_BULLETS; thisBullet = thisBullet + 1 )
    {
        if( crabBulletsActive[ thisBullet ] )
        {
            int x, y;
            if( crabScreenCoord( crabBulletsX[ thisBullet ], crabBulletsY[ thisBullet ], &x, &y ) )
            {
                int s = crabWeaponSize[ crabBulletsWeapon[ thisBullet ] ];
                if( s == 1 ) gbDrawPixel( x, y );
                else gbFillRect( x, y, s, s );
            }
        }
    }
}

void crabDrawAmmoOverlay()
{
    if( crabAmmo )
    {
        gbCursorX = 0;
        gbCursorY = LCDHEIGHT - gbFontHeight;
        gbPrintString( crabWeaponName( crabCurrentWeapon ) );

        if( crabNextShot > 2 )
          gbFillRect( -2, LCDHEIGHT - 2, crabNextShot, 2 );

        if( crabCurrentWeapon > 0 )
        {
            int xOffset = 0;
            if( crabAmmo < 100 ) xOffset = xOffset + gbFontWidth;
            if( crabAmmo < 10 ) xOffset = xOffset + gbFontWidth;
            gbCursorX = LCDWIDTH - 3 * gbFontWidth + xOffset;
            gbCursorY = LCDHEIGHT - gbFontHeight;
            gbPrintNumber( crabAmmo );
        }
        else
        {
            gbCursorX = LCDWIDTH - 3 * gbFontWidth;
            gbCursorY = LCDHEIGHT - gbFontHeight;
            gbPrintString( "inf" );
        }
    }
}

// -----------------------------------------------------------------------
// Crate (weapon upgrade pickup) - direct ports of crate.ino's own real
// spawnCrate()/collideCrate()/drawCrate().
// -----------------------------------------------------------------------

void crabSpawnCrate()
{
    bool okay = false;
    while( !okay )
    {
        crabCrateX = arand( CRAB_WORLD_W ) * 8;
        crabCrateY = arand( CRAB_WORLD_H ) * 8;
        okay = true;

        if( crabCollideWorld( crabCrateX, crabCrateY, 8, 8 ) ) okay = false;

        int x, y;
        if( crabScreenCoord( crabCrateX, crabCrateY, &x, &y ) ) okay = false;
    }
}

void crabCollideCrate()
{
    if( gbCollideRectRect( crabCrateX + 2, crabCrateY + 2, 4, 4, crabPlayerX, crabPlayerY, CRAB_PLAYER_W, CRAB_PLAYER_H ) )
    {
        if( crabScore < 5 )
        {
            gbPopup( "Earn $5 first", 30 );
            return;
        }
        if( crabCurrentWeapon < CRAB_NUM_WEAPONS - 1 )
        {
            gbPopup( "Upgraded !", 30 );
            crabPlayPowerUpSound();
        }
        else
          gbPopup( "Refilled !", 30 );

        crabScore = crabScore - 5;
        crabSpawnCrate();
        crabCurrentWeapon = gbMin( CRAB_NUM_WEAPONS - 1, crabCurrentWeapon + 1 );
        crabAmmo = crabWeaponAmmo[ crabCurrentWeapon ];
        crabPlayerLife = gbMin( crabPlayerLife + 1, CRAB_PLAYER_LIFE_MAX );
    }
}

void crabDrawCrate()
{
    int x, y;
    if( crabScreenCoord( crabCrateX, crabCrateY, &x, &y ) )
      gbDrawBitmap( x, y, crabCrateSprite );
}

// -----------------------------------------------------------------------
// Score / camera-shake / EEPROM highscore - direct ports of play.ino's own
// real displayScore()/shakeScreen()/loadHighscore()/(the EEPROM half of)
// saveHighscore().
// -----------------------------------------------------------------------

void crabDisplayScore()
{
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "$" );
    gbPrintNumber( crabScore );
}

// See header comment - the real backlight-flicker line has no equivalent
// and is dropped; the camera-shake offset itself is a direct port.
void crabShakeScreen()
{
    if( crabShakeTimeLeft )
    {
        crabShakeTimeLeft = crabShakeTimeLeft - 1;
        crabCameraX = crabCameraX + ( -crabShakeMagnitude + arand( 2 * crabShakeMagnitude + 1 ) );
        crabCameraY = crabCameraY + ( -crabShakeMagnitude + arand( 2 * crabShakeMagnitude + 1 ) );
    }
}

void crabLoadHighscore()
{
    int i;
    for( i = 0; i < CRAB_RANK_MAX; i = i + 1 )
    {
        int lsb = eeprom_read_byte( i * 2 );
        int msb = eeprom_read_byte( i * 2 + 1 );
        crabHighscore[ i ] = ( lsb & 0xFF ) + ( ( msb << 8 ) & 0xFF00 );
        if( crabHighscore[ i ] == 0xFFFF ) crabHighscore[ i ] = 0;
    }
}

void crabInsertHighscore( int score )
{
    crabHighscore[ CRAB_RANK_MAX - 1 ] = score;

    int i;
    for( i = CRAB_RANK_MAX - 1; i > 0; i = i - 1 )
    {
        if( crabHighscore[ i - 1 ] < crabHighscore[ i ] )
        {
            int temp = crabHighscore[ i - 1 ];
            crabHighscore[ i - 1 ] = crabHighscore[ i ];
            crabHighscore[ i ] = temp;
        }
        else break;
    }

    for( i = 0; i < CRAB_RANK_MAX; i = i + 1 )
    {
        eeprom_write_byte( i * 2, crabHighscore[ i ] & 0xFF );
        eeprom_write_byte( i * 2 + 1, ( crabHighscore[ i ] >> 8 ) & 0xFF );
    }
}

// -----------------------------------------------------------------------
// Reset - the non-EEPROM half of play.ino's own real initGame() (the
// EEPROM-save half is threaded through the state machine below instead -
// see header comment quirk 2 and crabMenuSelectRestart()/
// crabStatsAcceptOrHighscore() below).
// -----------------------------------------------------------------------

void crabResetGameState()
{
    crabScore = 0;
    crabKills = 0;
    crabCurrentWeapon = 0;
    crabAmmo = 9999;
    crabNextShot = 0;
    crabShakeTimeLeft = 0;
    crabPlayerLife = CRAB_PLAYER_LIFE_MAX;
    crabBossFreq = CRAB_BOSS_FREQ_INIT;
    crabBossNextSpawn = crabBossFreq;
    crabActiveMobs = CRAB_INIT_NUM_MOBS;

    bool mobsOk = false;
    while( !mobsOk )
    {
        bool posOk = false;
        while( !posOk )
        {
            crabPlayerX = arand( CRAB_WORLD_W ) * 8;
            crabPlayerY = arand( CRAB_WORLD_H ) * 8;
            posOk = !crabCollideWorld( crabPlayerX, crabPlayerY, CRAB_PLAYER_W, CRAB_PLAYER_H );
        }
        crabCameraX = crabPlayerX - LCDWIDTH / 2 + CRAB_PLAYER_W / 2;
        crabCameraY = crabPlayerY - LCDHEIGHT / 2 + CRAB_PLAYER_W / 2; // real upstream reuses playerW here too - see header comment
        mobsOk = crabSpawnMobs();
    }

    crabSpawnCrate();

    int i;
    for( i = 0; i < CRAB_NUM_BULLETS; i = i + 1 ) crabBulletsActive[ i ] = false;
    for( i = 0; i < CRAB_NUM_SPLASH; i = i + 1 ) crabSplashActive[ i ] = false;
    crabBlastLifespan = 0;
}

// -----------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------

void crabUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 0, 12, crabLogo ); // real confirmed Gamebuino::titleScreen() anchor for a 64x36 logo - see gameUfoRace.c's own header comment
    gbCursorX = 21;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        gbSetFont( gbFont5x7 );
        crabState = CRAB_STATE_MENU;
    }
}

void crabDismissHighscores()
{
    gbPlayOK();
    if( crabHighscoreReturn == CRAB_HS_RETURN_MENU )
      crabState = CRAB_STATE_MENU;
    else if( crabHighscoreReturn == CRAB_HS_RETURN_PLAY_FRESH )
    {
        crabResetGameState();
        crabPlayCReturnsTo = CRAB_STATE_TITLE;
        crabPlayPhase = CRAB_PLAY_INTRO;
        crabPlayIntroCounter = 0;
        gbSetFont( gbFont3x5 );
        crabState = CRAB_STATE_PLAY;
    }
    else
    {
        crabResetGameState();
        crabPlayPhase = CRAB_PLAY_NORMAL;
        crabState = CRAB_STATE_PLAY;
    }
}

void crabUpdateHighscores()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 9 + arand( 2 );
    gbCursorY = 0 + arand( 2 );
    gbPrintString( "HIGH SCORES" );

    int thisScore;
    for( thisScore = 0; thisScore < CRAB_RANK_MAX; thisScore = thisScore + 1 )
    {
        gbCursorX = LCDWIDTH - 3 * gbFontWidth;
        gbCursorY = gbFontHeight + gbFontHeight * thisScore;
        gbPrintNumber( crabHighscore[ thisScore ] );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
      crabDismissHighscores();
}

void crabUpdateSysInfo()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 0; gbCursorY = 0;
    gbPrintString( "SYSTEM INFO" );
    gbCursorX = 0; gbCursorY = 7;
    gbPrintString( "No sensors" );
    gbCursorX = 0; gbCursorY = 14;
    gbPrintString( "emulated here" );
    gbCursorX = 0; gbCursorY = 21;
    gbPrintString( "Mobs:" );
    gbPrintNumber( crabActiveMobs );
    gbPrintString( "/" );
    gbPrintNumber( CRAB_NUM_MOBS );
    gbCursorX = 0; gbCursorY = 28;
    gbPrintString( "Killed:" );
    gbPrintNumber( crabKills );
    gbCursorX = 0; gbCursorY = 35;
    gbPrintString( "C: Back" );

    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        gbSetFont( gbFont5x7 );
        crabState = CRAB_STATE_MENU;
    }
}

void crabMenuSelectPlay()
{
    gbPlayOK();
    crabPlayCReturnsTo = CRAB_STATE_MENU;
    crabPlayPhase = CRAB_PLAY_INTRO;
    crabPlayIntroCounter = 0;
    gbSetFont( gbFont3x5 );
    crabState = CRAB_STATE_PLAY;
}

void crabMenuSelectRestart()
{
    gbPlayOK();
    if( crabScore > crabHighscore[ CRAB_RANK_MAX - 1 ] )
    {
        crabInsertHighscore( crabScore );
        crabHighscoreReturn = CRAB_HS_RETURN_PLAY_FRESH;
        gbSetFont( gbFont5x7 );
        crabState = CRAB_STATE_HIGHSCORES;
    }
    else
    {
        crabResetGameState();
        crabPlayCReturnsTo = CRAB_STATE_TITLE;
        crabPlayPhase = CRAB_PLAY_INTRO;
        crabPlayIntroCounter = 0;
        gbSetFont( gbFont3x5 );
        crabState = CRAB_STATE_PLAY;
    }
}

void crabUpdateMenu()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 18;
    gbCursorY = 0;
    gbPrintString( "CRABATOR" );

    int i;
    for( i = 0; i < CRAB_MENU_COUNT; i = i + 1 )
    {
        gbCursorX = 2;
        gbCursorY = 8 + i * 8;
        if( i == crabMenuIndex ) gbPrintString( ">" );

        gbCursorX = 8;
        gbCursorY = 8 + i * 8;
        if( i == 0 ) gbPrintString( "Play" );
        else if( i == 1 ) gbPrintString( "Restart" );
        else if( i == 2 ) gbPrintString( "High scores" );
        else if( i == 3 ) gbPrintString( "System Info" );
        else gbPrintString( "Main Menu" );
    }

    if( gbPressed( BTN_UP ) )
    {
        gbPlayTick();
        crabMenuIndex = crabMenuIndex - 1;
        if( crabMenuIndex < 0 ) crabMenuIndex = CRAB_MENU_COUNT - 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        gbPlayTick();
        crabMenuIndex = crabMenuIndex + 1;
        if( crabMenuIndex > CRAB_MENU_COUNT - 1 ) crabMenuIndex = 0;
    }

    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        crabState = CRAB_STATE_TITLE;
    }

    if( gbPressed( BTN_A ) )
    {
        if( crabMenuIndex == 0 ) crabMenuSelectPlay();
        else if( crabMenuIndex == 1 ) crabMenuSelectRestart();
        else if( crabMenuIndex == 2 )
        {
            gbPlayOK();
            crabHighscoreReturn = CRAB_HS_RETURN_MENU;
            crabState = CRAB_STATE_HIGHSCORES;
        }
        else if( crabMenuIndex == 3 )
        {
            gbPlayOK();
            gbSetFont( gbFont3x5 );
            crabState = CRAB_STATE_SYSINFO;
        }
        else
        {
            gbPlayOK();
            crabState = CRAB_STATE_TITLE;
        }
    }
}

void crabUpdatePlayIntro()
{
    gbSetColor( GB_BLACK );
    gbFontSize = 2;
    gbCursorX = 6;
    gbCursorY = 16;
    gbPrintString( "LET'S GO!" );
    crabPlayIntroCounter = crabPlayIntroCounter + 1;

    if( crabPlayIntroCounter >= 10 )
    {
        gbFontSize = 1;
        gbPopup( crabShootRunHint, 60 );
        crabPlayPhase = CRAB_PLAY_NORMAL;
    }
}

void crabUpdatePlayNormal()
{
    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        gbSetFont( gbFont5x7 );
        crabState = crabPlayCReturnsTo;
        return;
    }

    bool moved = false;
    if( gbRepeat( BTN_RIGHT, 1 ) ) { crabPlayerDir = 3; moved = true; }
    else if( gbRepeat( BTN_LEFT, 1 ) ) { crabPlayerDir = 1; moved = true; }
    if( gbRepeat( BTN_DOWN, 1 ) ) { crabPlayerDir = 2; moved = true; }
    else if( gbRepeat( BTN_UP, 1 ) ) { crabPlayerDir = 0; moved = true; }

    if( moved )
    {
        crabMoveXYDS( &crabPlayerX, &crabPlayerY, crabPlayerDir, crabPlayerSpeed );
        if( crabCollideWorld( crabPlayerX, crabPlayerY, CRAB_PLAYER_W, CRAB_PLAYER_H ) )
          crabMoveXYDS( &crabPlayerX, &crabPlayerY, crabPlayerDir, -crabPlayerSpeed );
    }

    crabCameraX = crabPlayerX + CRAB_PLAYER_W / 2 - LCDWIDTH / 2;
    crabCameraY = crabPlayerY + CRAB_PLAYER_H / 2 - LCDHEIGHT / 2;
    crabShakeScreen();
    gbSetColor( GB_BLACK );
    crabDrawWorld( crabCameraX, crabCameraY );

    crabScreenCoord( crabPlayerX, crabPlayerY, &crabScreenX, &crabScreenY );
    gbDrawBitmapRotated( crabScreenX - 1, crabScreenY - 1, crabPlayerSprite, crabPlayerDir, 0 );

    crabMoveMobs();
    crabDrawMobs();

    if( crabNextShot ) crabNextShot = crabNextShot - 1;
    if( gbRepeat( BTN_A, 1 ) && !gbRepeat( BTN_B, 1 ) ) crabShoot();
    if( gbRepeat( BTN_B, 1 ) ) crabPlayerSpeed = 2;
    else crabPlayerSpeed = 1;

    crabMoveBullets();
    crabDrawBullets();
    crabExplode();
    crabDrawSplashes();
    crabCollideCrate();
    crabDrawCrate();

    int i;
    for( i = 0; i <= CRAB_PLAYER_LIFE_MAX / 2; i = i + 1 )
    {
        if( ( i * 2 ) <= crabPlayerLife )
          gbDrawBitmap( LCDWIDTH - i * 9 + 2, 0, crabFullHeart );
        else
        {
            gbSetColor( GB_WHITE );
            gbDrawBitmap( LCDWIDTH - i * 9 + 2, 0, crabFullHeart );
            gbSetColorBg( GB_BLACK, GB_WHITE ); // real two-arg call preserved verbatim - inert for a bitmap draw, see header comment on gbDrawBitmap() ignoring bg
            gbDrawBitmap( LCDWIDTH - i * 9 + 2, 0, crabEmptyHeart );
        }
    }

    if( !crabPlayerLife )
    {
        if( ( gbFrameCount % 2 ) == 0 ) { crabShakeMagnitude = 2; crabShakeTimeLeft = 1; }
    }
    else if( crabPlayerLife == 1 )
    {
        crabShakeMagnitude = 1;
        crabShakeTimeLeft = 1;
    }

    if( crabPlayerLife % 2 )
      gbDrawBitmap( LCDWIDTH - ( crabPlayerLife / 2 + 1 ) * 9 + 2, 0, crabHalfHeart );

    crabDrawAmmoOverlay();
    crabDisplayScore();

    int thisMob;
    for( thisMob = 0; thisMob < crabActiveMobs; thisMob = thisMob + 1 )
    {
        if( gbCollideRectRect( crabMobsX[ thisMob ], crabMobsY[ thisMob ], crabMobsSize[ thisMob ], crabMobsSize[ thisMob ],
                                crabPlayerX, crabPlayerY, CRAB_PLAYER_W, CRAB_PLAYER_H ) )
        {
            crabPlayerLife = crabPlayerLife - 1;
            crabShakeMagnitude = 2;
            crabShakeTimeLeft = 4;
            if( crabMobsSize[ thisMob ] == CRAB_BOSS_SIZE )
            {
                crabPlayerLife = crabPlayerLife - 1;
                crabShakeMagnitude = 3;
                crabShakeTimeLeft = 4;
            }
            crabPlayDamageSound();
            crabSpawnMob( thisMob );

            if( crabPlayerLife < 0 )
            {
                crabPlayPhase = CRAB_PLAY_GAMEOVER_ANIM;
                crabGameOverTimer = 0;
                return; // real upstream never runs anything else this tick once this triggers - see header comment
            }
        }
    }
}

void crabUpdatePlayGameOverAnim()
{
    crabDrawMobs();
    crabDrawBullets();
    crabDrawSplashes();
    crabDrawCrate();
    crabDrawAmmoOverlay();
    crabDisplayScore();
    gbSetColor( GB_BLACK );
    crabDrawWorld( crabCameraX, crabCameraY );
    gbDrawBitmapRotated( crabScreenX - 1, crabScreenY - 1, crabPlayerSprite, crabPlayerDir, 0 );

    gbSetColor( GB_WHITE );
    gbFillRect( 0, 0, crabGameOverTimer * 2, LCDHEIGHT );
    gbFillRect( LCDWIDTH - crabGameOverTimer * 2, 0, crabGameOverTimer * 2, LCDHEIGHT );
    gbSetColorBg( GB_BLACK, GB_WHITE );
    gbCursorX = 12;
    gbCursorY = 1;
    gbPrintString( "GAME OVER!" );

    crabGameOverTimer = crabGameOverTimer + 1;
    if( crabGameOverTimer == ( LCDWIDTH / 4 + 10 ) )
      crabPlayPhase = CRAB_PLAY_GAMEOVER_STATS;
}

void crabStatsAccept()
{
    gbPlayOK();
    if( crabScore > crabHighscore[ CRAB_RANK_MAX - 1 ] )
    {
        crabInsertHighscore( crabScore );
        crabHighscoreReturn = CRAB_HS_RETURN_PLAY_CONTINUE;
        gbSetFont( gbFont5x7 );
        crabState = CRAB_STATE_HIGHSCORES;
    }
    else
    {
        crabResetGameState();
        crabPlayPhase = CRAB_PLAY_NORMAL;
    }
}

// Note: no gbSetColor()/gbSetColorBg() call anywhere in this function - it
// deliberately inherits the (BLACK,WHITE) state left behind by
// crabUpdatePlayGameOverAnim()'s own final text draw, matching upstream's
// own real implicit color-carryover exactly (see header comment - the same
// class of quirk gameShipwrek.c's own header comment documents).
void crabUpdatePlayGameOverStats()
{
    if( crabScore > crabHighscore[ CRAB_RANK_MAX - 1 ] )
    {
        gbCursorX = 2 + arand( 2 );
        gbCursorY = 0 + arand( 2 );
        gbPrintString( "NEW HIGHSCORE" );
    }
    else
    {
        gbCursorX = 12;
        gbCursorY = 1;
        gbPrintString( "GAME OVER!" );
    }

    gbCursorX = 0;
    gbCursorY = 12;
    gbPrintString( "You made $" );
    gbPrintNumber( crabScore );
    gbPrintString( "\nby killing\n" );
    gbPrintNumber( crabKills );
    gbPrintString( " crabs." );

    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( crabAcceptHint );

    if( gbPressed( BTN_A ) ) crabStatsAccept();
}

void crabUpdatePlay()
{
    if( crabPlayPhase == CRAB_PLAY_INTRO ) crabUpdatePlayIntro();
    else if( crabPlayPhase == CRAB_PLAY_NORMAL ) crabUpdatePlayNormal();
    else if( crabPlayPhase == CRAB_PLAY_GAMEOVER_ANIM ) crabUpdatePlayGameOverAnim();
    else crabUpdatePlayGameOverStats();
}

// -----------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------

void gameCrabator_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 ); // real upstream setup()'s own gb.display.setFont(font5x7)
    crabLoadHighscore();
    crabResetGameState(); // real upstream setup()'s own initGame() - the highscore-save check inside real initGame() is always false here (score==0 at boot), so it's skipped rather than reproduced as a no-op
    crabState = CRAB_STATE_TITLE;
    crabMenuIndex = 0;
    gbPickRandomSeed(); // no-op, see gamebuinoShim.h's own header comment
}

void gameCrabator_update()
{
    if( !gbUpdate() ) return;

    if( crabState == CRAB_STATE_TITLE ) crabUpdateTitle();
    else if( crabState == CRAB_STATE_MENU ) crabUpdateMenu();
    else if( crabState == CRAB_STATE_SYSINFO ) crabUpdateSysInfo();
    else if( crabState == CRAB_STATE_HIGHSCORES ) crabUpdateHighscores();
    else crabUpdatePlay();

    // real gb.popup()'s own overlay is drawn automatically by
    // gbRenderFrame() below - see gamebuinoShim.h's own header comment.
    gbRenderFrame();
}
