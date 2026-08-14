#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Skibuino (Mike Del Pozzo, GPLv3 - real LICENSE file confirmed -
// github.com/delpozzo/skibuino). A skiing game: ski continuously downhill
// through a scrolling 192x256 map, steering left/right around trees and
// logs, with a temporary jump (clears logs, not trees) and a power-slide
// speed boost; distance skied ("meters traveled") is the score, and the
// farthest run ever survived is saved as a high score across sessions.
// Hitting an obstacle (without jumping over a log) ends the run with a
// random one-line death quip and a short game-over animation before
// restarting. A genuinely novel genre for this cartridge - no other ported
// game here is a skiing/downhill-runner game.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every global/function got a
// `ski`-prefixed name (this single compiled cartridge shares one flat
// global namespace across every game - confirmed unused by grepping the
// whole `src/` tree before picking it). `random(N)` became `arand(N)`;
// upstream's own two-argument `random(LCDHEIGHT, MAPHEIGHT-LCDHEIGHT-16)`
// (used for every obstacle's own spawn Y) became `LCDHEIGHT + arand(
// (MAPHEIGHT-LCDHEIGHT-16) - LCDHEIGHT)` (this dialect's own established
// `min + arand(max-min)` ranged-random idiom), factored into one
// `skiRandObstacleY()` helper since all three obstacle spawners
// (trees/logs) use the exact same real range. `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op (this project's own established
// precedent for every upstream `randomSeed()`-style call).
//
// STATE MACHINE: upstream has TWO real blocking calls converted here, both
// via this project's own established "blocking loop -> explicit resumable
// state" treatment (see gamePong.c's own header comment for the pattern):
//   - `setup()`'s own blocking `gb.titleScreen(gameLogo)` (shown once at
//     boot) became `SKI_STATE_TITLE`/`skiUpdateTitle()`, dismissed by a
//     genuine `gbPressed(BTN_A)` exactly like every other ported title
//     screen here.
//   - `pauseGame()`'s own blocking `while(1) { if(gb.update()) {...} }`
//     loop (entered from inside `playerThink()` on a Button-C press)
//     became `SKI_STATE_PAUSE`/`skiUpdatePause()`, with upstream's own
//     internal `highScoreResetFlag` boolean preserved verbatim as
//     `skiHighScoreResetFlag` to pick between its two real sub-screens
//     (the main P A U S E D menu, and the "...are you sure?" high-score-
//     reset confirmation) - both ported as direct, near-verbatim
//     transcriptions of upstream's own real text/layout/button checks.
//     ONE DELIBERATE, DOCUMENTED SIMPLIFICATION: real `pauseGame()` is
//     called *from the middle of* `playerThink()` and, being a genuinely
//     blocking call, execution resumes at the very next line once it
//     returns - so on real hardware, pressing C mid-tick still finishes
//     that same tick's own jump-countdown/x-clamp/scroll-refresh/
//     `metersTraveled+=yspeed` tail logic (using pre-pause state) the
//     instant the player resumes. A non-blocking state machine can't
//     preserve that without unnaturally restructuring `playerThink()`
//     itself into two halves - so, matching every other blocking-call
//     conversion in this project, `skiPlayerThink()` instead just
//     `return`s immediately after entering `SKI_STATE_PAUSE`, skipping
//     that one tick's tail logic outright (it resumes fresh, from the top
//     of `skiPlayerThink()`, on the tick play actually resumes). `skiMeters
//     Traveled` under-counts by at most one `yspeed` per pause - imperceptible
//     over a run that's normally hundreds of meters long. `skiUpdatePlay()`
//     itself also checks `skiState` right after calling `skiThinkAll()` and
//     skips `skiUpdateCamera()`/`skiDrawAll()` for that tick if a pause was
//     just entered mid-think - avoiding a half-finished gameplay frame
//     being rendered underneath the very first pause-menu tick.
//   - Real `pauseGame()`'s own "A: Title Screen" option calls the real
//     blocking `setup()` again in full (re-running `gb.titleScreen()` from
//     scratch) - ported as transitioning back to `SKI_STATE_TITLE`
//     (`skiClearEntities(); skiBeginTitle();`), which - via the exact same
//     title-dismiss handler used at boot - re-runs `skiInitEntities()`/
//     `skiInitCamera()`/`skiStartGame()` once Button A is pressed again,
//     the same real call sequence `setup()` performs. Real upstream's own
//     `setup()`/`startGame()` never call `saveHighScore()` on this path
//     (only `restartGame()` - reached exclusively via a real crash/game-
//     over, or the high-score-reset confirm - ever does), so quitting to
//     the title screen from the pause menu mid-run would silently discard a
//     genuinely-higher-than-recorded `metersTraveled` without ever writing
//     it to EEPROM. FIXED HERE, NOT PRESERVED: the same highscore check
//     `skiRestartGame()` already performs now also runs on this "A: Title
//     Screen" path, in `skiUpdatePause()`, before the run's own state is
//     cleared.
//
// ENTITY SYSTEM: upstream's own generic `Entity` struct/array (`EntityList
// [MAXENTS]`, `spawnEntity()`/`freeEntity()`/`thinkAll()`/`drawAll()`) is
// ported closely, but with three real adaptations:
//   1. `void(*think)(struct ENTITY_S*)` (a real per-entity function
//      pointer, upstream's own dispatch for player/tree/log/game-over
//      behavior) became a plain `int think` dispatch id
//      (`SKI_THINK_PLAYER`/`_GAMEOVER`/`_TREE`/`_LOG`), matched by an
//      explicit `if`/`else if` chain in `skiThinkAll()`. This dialect's
//      own reference doc (`VIRCON32_C_DIALECT.md`) documents function
//      pointers (even as struct fields) as supported by the current
//      compiler - but matching this project's own established "don't be
//      the first file betting on an unproven-here pattern" caution
//      (gameCatcher.c's own header comment gives the identical reasoning
//      for the identical situation - a real `void(*draw_fun)(void*)`
//      struct field in ITS OWN upstream source), this file plays it safe
//      the same way.
//   2. `const byte *sprite[MAXFRAMES]` (4 real bitmap pointers per entity)
//      became four plain `int sprite0..sprite3` fields (one of the
//      `SKI_BMP_*` ids below, or `SKI_BMP_NONE`), for the same "avoid an
//      unproven-here pointer-as-struct-field pattern" reason - dispatched
//      to the real bitmap array via an explicit `if`/`else if` chain in
//      `skiDrawAll()`/`skiGetSpriteWH()`, mirroring gameCatcher.c's own
//      identical `flags_bm[]`-avoidance treatment.
//   3. `byte flag[MAXFLAGS]` (4 general-purpose per-entity flag slots)
//      became four plain `flag0..flag3` fields rather than an array field
//      - no other file in this project has yet exercised an array *inside*
//      a struct (every existing struct here only has scalar members, see
//      e.g. gameCatcher.c's own `CatchSprite`/gameCrazyCar.c's own
//      `CcarObstacle`), so this sticks to that same already-proven
//      scalar-fields-only shape rather than being the first to bet on an
//      untested one, for a cost of only 8 extra named fields total (4
//      sprites + 4 flags) across the whole file. Upstream's own
//      `PLAYERFLAGS` enum (`JUMP=0, JUMPCTR=1, GAMEOVER=2, PLAYERTXT=3`)
//      maps directly: `flag0`=JUMP, `flag1`=JUMPCTR, `flag2`=GAMEOVER,
//      `flag3`=PLAYERTXT.
//   `Entity *player` (a real pointer to the player's own live entity) became
//   a plain `int skiPlayerIndex`, for the same "avoid an untested pointer-
//   into-array-element pattern" reason (no other file here takes the
//   address of an array element either) - true to upstream anyway, since
//   `spawnEntity()` always returns the first free slot and the player is
//   always spawned first, immediately after a full `clearEntities()`, so
//   the player is provably always entity index 0 for the whole game.
//   `switch` statements (upstream's own random-sprite pickers in
//   `spawnLargeTree()`/`spawnSmallTree()`/`spawnLog()`, and the random
//   death-quip picker in `thinkGameOver()`) became `if`/`else if` chains -
//   no `switch` has been proven to work in this dialect yet (see
//   gamePong.c's own header comment for the same established caution).
//   One small defensive addition beyond a literal port: `skiInitEntities()`
//   explicitly zeroes each entity's own `type` field (upstream leaves it
//   uninitialized until first spawned, relying on real AVR's own
//   zero-initialized-BSS globals defaulting every never-spawned slot's
//   `type` to 0/PLAYER, which is why `clearObstacles()`'s own real
//   `EntityList[i].type == OBSTACLE` check - with no accompanying `.used`
//   guard - is harmless there) - added here purely so the same guarantee
//   holds even across this cartridge's own game-to-game relaunches within
//   one long-running process, not because upstream's own check was
//   observed to misbehave.
//
// COLLISION - A REAL BUG, FOUND AND FIXED: this file used to claim real
// Gamebuino Classic's own `gb.collideBitmapBitmap()` is itself nothing
// more than a `collideRectRect()` test against each bitmap's own real
// width/height header bytes (no genuine per-pixel comparison at all
// despite the name) - a claim explicitly flagged at the time as
// "recalled-not-re-read-this-session", not actually verified. It was
// wrong: reading the real library source directly (`more games/
// Gamebuino-Classic/Gamebuino.cpp`) shows `Gamebuino::collideBitmapBitmap()`
// genuinely does real per-overlap-pixel testing via `display.
// getBitmapPixel()` on both bitmaps - `collideRectRect()` is only used
// first, as an early-exit bounding-box pre-check before the real per-pixel
// loop, not a replacement for it (see gameDescent.c's own
// `gbCollideBitmapBitmap()` for the same real source confirming this,
// already promoted to the shared shim). `skiCollideBitmapBitmap()` below
// now calls the real `gbCollideBitmapBitmap()` primitive directly (via a
// new local `skiGetSpriteBitmap()` lookup returning each sprite id's own
// real bitmap pointer, mirroring `skiGetSpriteWH()`'s/`skiDrawAll()`'s own
// identical `if`/`else if` dispatch structure) instead of approximating
// with rectangles - real upstream's own function signature takes `int8_t`
// x/y parameters (an 8-bit range too small for this game's own real
// 192x256 map coordinates) - whether that silently truncates on genuine
// hardware is NOT something this port re-verified this session (documented
// uncertainty, not asserted as a bug); moot either way here, since this
// dialect's own full-range 32-bit `int` is used throughout with no
// narrowing step at all.
//
// SOUND: a faithful, byte-for-byte port of real `playSound(const int *snd,
// byte channel)` - `skiPlaySound(int* snd)` drives the same 5 real
// `gb.sound.command(...)` calls (note volume, instrument select, volume
// slide, pitch slide via the arpeggio command, tremolo) in upstream's own
// exact order before `gbPlayNoteChannel()`, always on channel 0 (upstream's
// own `channel` parameter is a literal `0` at every real call site). The 5
// real envelope tables from upstream's own `Sound.h` (`skiSndJump`/
// `skiSndSlide`/`skiSndCrash`/`skiSndResume`/`skiSndPause`) are copied
// verbatim (10 entries each: volume, instrument ID, volume-slide step
// duration/depth, pitch-slide step duration/depth, tremolo step
// duration/depth, pitch, duration).
//
// EEPROM: upstream's own `getHighScore()`/`saveHighScore()` read/write two
// raw EEPROM bytes directly (address 0 = low byte, address 1 = high byte -
// upstream's own local variable names for these, confusingly, are `two`
// and `one` respectively, preserved here only as a comment, not as
// deliberately-confusing identifiers) - ported through this shim's own
// `eeprom_read_byte()`/`eeprom_update_byte()` at the exact same two
// addresses, matching upstream's own exact byte order (not this project's
// own `eeprom_read_word()`/`eeprom_write_word()` helpers, since those pack
// the *opposite* byte order - high byte first - which would still work
// correctly for this game alone since it owns both the read and write
// ends exclusively, but diverges from a literal upstream transcription for
// no benefit).
//
// A REAL SIGNED-32-BIT-vs-SIGNED-16-BIT MISMATCH, FOUND AND FIXED (not
// just a preserved cosmetic quirk): this shim's own fresh/never-written
// EEPROM cells default to 255 (0xFF), matching real AVR EEPROM's own
// actual factory-erased state - so on a genuinely fresh card, both bytes
// read back 255. Upstream's own real `int getHighScore(){ long two =
// EEPROM.read(0); long one = EEPROM.read(1); return ((two<<0)&0xFFFFFF) +
// ((one<<8)&0xFFFFFFFF); }` computes this same 255+65280=65535 as an
// intermediate `unsigned long` - but its *return type* is a genuine
// 16-bit signed AVR `int`, so real hardware silently narrows 65535 down to
// its own low 16 bits at the `return` statement, reinterpreted as the
// signed value **-1**, not 65535. `highScore`/`metersTraveled` are both
// plain signed `int` upstream too (confirmed directly in `Skibuino.ino`),
// with no unsigned operand anywhere in `if(metersTraveled > highScore)` to
// launder the sign back the way e.g. gameUfoRace.c's own `unsigned int
// score` parameter does for its own structurally-identical composition -
// so on real hardware, a truly fresh cartridge's first real distance
// (any non-negative value) always beats -1, and the game's first-ever
// highscore save always succeeds. This dialect's own `int` is always a
// full 32-bit word and never narrows at a return/assignment boundary the
// way AVR's real 16-bit `int` does, so the identical formula here stays
// +65535 instead - an "impossibly high" sentinel no real distance could
// ever beat, which would silently and permanently block a fresh
// cartridge's very first highscore save (a real, save-breaking regression,
// not merely a cosmetic HUD-display difference). Fixed in
// `skiGetHighScore()` below by detecting the genuinely-fresh case directly
// (both raw bytes == 255) and returning a plain 0 instead of the composed
// 65535 - matching real hardware's own actual functional outcome (any
// real score beats the fresh sentinel), not its literal internal bit
// pattern. `gameCrabator.c`/`gameDescentIntoHell.c`'s own real upstream
// `(highscore[i]==0xFFFF) ? 0 : highscore[i]` check achieves the same
// effect for their own structurally-similar per-entry highscore tables -
// this game's own upstream never had an equivalent check to restore, so
// this fix is new, not a restoration of dropped upstream logic.
//
// REAL BITMAP ART RESTORED - every one of upstream's own real
// `const byte NAME[] PROGMEM = {...}` bitmaps (the 72x48 `gameLogo` splash,
// the 4 real 8x10 player sprites, the 4 real 16x16 large trees, the 4 real
// 8x8 small trees, and the 4 real 8x4 logs) is reproduced below as a real
// `int[N] skiXxxBitmap = { width, height, byte0, byte1, ... }` array (this
// dialect's own `int[N] name` array-declaration order, not C's `int
// name[N]`) - not one was replaced with a geometric placeholder. Every
// upstream `B00000000`-style Arduino binary literal (432 of them, just in
// `gameLogo` alone) was converted to hex mechanically via a small one-off
// Python script reading Sprite.h directly (not hand-transcribed - too
// error-prone at this size) and double-checked by recomputing each
// bitmap's own expected `ceil(width/8)*height` byte count against what the
// script actually emitted (every one of the 17 real bitmaps matched
// exactly). Every sprite here is a single, fully self-contained outline
// bitmap - none of them are backed by a separate solid GRAY/fillRect mask
// layer upstream draws first (confirmed by reading `Sprite.h` in full, the
// same real bug class gameFlappyBirdo.c's/gameParachute.c's own header
// comments document finding and fixing elsewhere in this project) - so
// there is no fill-under-bitmap layer to restore here, and no bleed-
// through risk to guard against.

#define SKI_MAXENTS 32
#define SKI_MAPWIDTH 192
#define SKI_MAPHEIGHT 256
// A REAL UPSTREAM MACRO-EXPANSION BUG, DELIBERATELY PRESERVED - DO NOT
// "FIX" THE MISSING OUTER PARENS: real upstream's own `#define
// CAMERAYOFFSET (LCDHEIGHT/2) + 10` has no parens around the whole
// expression, only around `LCDHEIGHT/2` - so its one real real call site,
// `cameraY = player->y - CAMERAYOFFSET;`, textually expands to `player->y
// - (LCDHEIGHT/2) + 10`, which parses as `(player->y - 24) + 10`, i.e.
// `player->y - 14`, NOT `player->y - 34` the way a fully-parenthesized
// macro would. Confirmed as real, load-bearing shipped behavior (not a
// theoretical parse quirk) via a direct side-by-side screenshot of real
// hardware, provided directly by the user: the player sprite sits
// noticeably higher on screen (around 30-40% down) than this port's own
// earlier, fully-parenthesized `(LCDHEIGHT/2 + 10)` version produced
// (`player->y - 34`, ~70% down - a real, visible, previously-unnoticed
// divergence from real hardware). Reproduced here with the same real
// missing parens, matching upstream's own real macro text exactly rather
// than hardcoding the resulting number, so this stays correct if
// `LCDHEIGHT` itself is ever revisited.
#define SKI_CAMERAYOFFSET LCDHEIGHT/2 + 10
// The 4 macros below are also matched exactly against real upstream's own
// text (`CAMERAXOFFSET LCDWIDTH/2`, `XSTARTPOS (LCDWIDTH/2) - 4`,
// `YSTARTPOS (LCDHEIGHT/2) - 5`, `SCROLLPOS MAPHEIGHT - LCDHEIGHT + 8`,
// `XLIMITR MAPWIDTH - 8`) after a project-wide sweep prompted by the real
// `SKI_CAMERAYOFFSET` bug just above - a systematic check found none of
// these 4 are ever used in a context where the missing parens would
// actually change a value (all either the leftmost operand of their own
// +/- chain, a lone function argument, or a real upstream `-=` rewritten
// as `x = x - MACRO`, all unaffected by the macro's own internal
// grouping) - matched exactly anyway, on request, rather than relying on
// that per-site analysis holding forever as this file changes.
#define SKI_CAMERAXOFFSET LCDWIDTH/2
#define SKI_XSTARTPOS (LCDWIDTH/2) - 4
#define SKI_YSTARTPOS (LCDHEIGHT/2) - 5
#define SKI_SCROLLPOS SKI_MAPHEIGHT - LCDHEIGHT + 8
#define SKI_XLIMITR SKI_MAPWIDTH - 8
#define SKI_XLIMITL 0
#define SKI_JUMPDURATION 10
#define SKI_JUMPCOOLDOWN 5
#define SKI_GAMEOVERTIMER 16

#define SKI_TYPE_PLAYER 0
#define SKI_TYPE_OBSTACLE 1

// real NOROT/ROTCCW/ROT180/ROTCW and NOFLIP/FLIPH/FLIPV/FLIPVH constants
#define SKI_NOROT 0
#define SKI_ROT180 2
#define SKI_NOFLIP 0
#define SKI_FLIPH 1

#define SKI_THINK_NONE 0
#define SKI_THINK_PLAYER 1
#define SKI_THINK_GAMEOVER 2
#define SKI_THINK_TREE 3
#define SKI_THINK_LOG 4

#define SKI_BMP_NONE -1
#define SKI_BMP_PLAYER_NORMAL 0
#define SKI_BMP_PLAYER_ANGLE 1
#define SKI_BMP_PLAYER_JUMP 2
#define SKI_BMP_PLAYER_ANGLE2 3
#define SKI_BMP_LARGETREE1 4
#define SKI_BMP_LARGETREE2 5
#define SKI_BMP_LARGETREE3 6
#define SKI_BMP_LARGETREE4 7
#define SKI_BMP_SMALLTREE1 8
#define SKI_BMP_SMALLTREE2 9
#define SKI_BMP_SMALLTREE3 10
#define SKI_BMP_SMALLTREE4 11
#define SKI_BMP_LOG1 12
#define SKI_BMP_LOG2 13
#define SKI_BMP_LOG3 14
#define SKI_BMP_LOG4 15

#define SKI_STATE_TITLE 0
#define SKI_STATE_PLAY 1
#define SKI_STATE_PAUSE 2

// -----------------------------------------------------------------------------
// Real bitmap art - see this file's own header comment for the conversion
// method (a small one-off script, not hand transcription).
// -----------------------------------------------------------------------------

int skiGameLogoBitmap[434] =
{
    72,48,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x3f,0xc0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3f,0xc0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x2,0x0,0x0,0x0,0x92,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0x1,0x31,
    0x0,0x6,0x0,0x0,0x0,0x0,0x0,0x1,0xff,0x0,0xf,0x0,0x0,0x8,0x0,0x0,
    0x1,0x7d,0x0,0x16,0x80,0x0,0xc,0x0,0x0,0x0,0x7c,0x0,0x7f,0xe0,0x0,0x16,
    0x0,0x0,0x0,0xce,0x0,0x3e,0x40,0x0,0x2d,0x0,0x1,0x0,0x82,0x0,0x5f,0xe0,
    0x0,0x5f,0x80,0xff,0x0,0x82,0x0,0xe7,0xb0,0x0,0xc,0x0,0xff,0x0,0x82,0x0,
    0x7e,0x60,0x0,0xc,0x0,0x20,0x0,0x82,0x0,0x9f,0xf0,0x0,0x1e,0x0,0x0,0x0,
    0x0,0x1,0xee,0xc8,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff,0xf0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0xff,0xf0,0x0,0x0,0x0,0x0,0x0,0x0,0x3,0x6f,0x3c,0x0,
    0x0,0x0,0x0,0x1,0x0,0x0,0x6,0x0,0x0,0x0,0x0,0x0,0x3,0xfe,0x0,0x6,
    0x0,0x0,0x0,0x0,0x0,0x3,0xfe,0x0,0xf,0x0,0x0,0x0,0x0,0x0,0x0,0xc,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xc,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x4,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x6,0x0,0x0,0xc1,0x8c,0x0,
    0x60,0x0,0x0,0xd,0x0,0x1c,0xc1,0x8c,0x0,0x60,0x0,0x18,0xd,0x0,0x32,0xc0,
    0xc,0x0,0x0,0x0,0x18,0x3f,0xc0,0x30,0xd3,0x8f,0x36,0xe3,0xe7,0x18,0x1f,0x80,
    0x1c,0xe1,0x8d,0xb6,0x63,0x6d,0x98,0x2f,0x40,0x6,0xf1,0x8d,0xb6,0x63,0x6d,0x98,
    0x74,0xe0,0x26,0xd1,0x8d,0xb6,0x63,0x6d,0x80,0x3f,0xc0,0x1c,0xdf,0xef,0x3f,0xfb,
    0x67,0x18,0x4f,0x20,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf6,0xf0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x7f,0xe0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0xb6,0xd8,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
};

int skiPlayerNormalBitmap[12] =
{
    8,10,
    0x4a,0x99,0xff,0xbd,0x3c,0x66,0x42,0x42,0x42,0x42,
};

int skiPlayerAngleBitmap[12] =
{
    8,10,
    0x4a,0x99,0xff,0xbd,0x3c,0x66,0x84,0x84,0x42,0x21,
};

int skiPlayerAngle2Bitmap[12] =
{
    8,10,
    0x8,0x18,0xff,0xbd,0xbd,0x66,0x44,0x24,0x12,0x9,
};

int skiPlayerJumpBitmap[12] =
{
    8,10,
    0x10,0x18,0xff,0xbd,0xbd,0x24,0xa5,0x42,0x81,0x0,
};

int skiLargeTree1Bitmap[34] =
{
    16,16,
    0x0,0x80,0x1,0x80,0x3,0xc0,0x7,0xe0,0x1f,0xf8,0xf,0xf0,0x1f,0xf8,0x3f,0xfc,
    0x1f,0xf8,0x3f,0xfc,0x7f,0xfe,0x3f,0xfc,0xff,0xff,0x1,0x80,0x1,0x80,0x3,0xc0,
};

int skiLargeTree2Bitmap[34] =
{
    16,16,
    0x1,0x0,0x3,0xc0,0x7,0xe0,0x3,0xf0,0x7,0xe0,0xf,0xf0,0xf,0xf8,0x1f,0xf0,
    0xf,0xe0,0x1f,0xf0,0x3f,0xf8,0x1f,0xf8,0x3f,0xfc,0x1,0x80,0x1,0x80,0x3,0xc0,
};

int skiLargeTree3Bitmap[34] =
{
    16,16,
    0x1,0x80,0x3,0xc0,0x5,0xa0,0x3,0xc0,0x7,0xa0,0x9,0xd0,0xf,0xf8,0x1d,0xd0,
    0x3,0xe0,0x19,0xb4,0x2f,0xe8,0x17,0xf8,0x3d,0xe4,0x1,0x80,0x1,0x80,0x3,0xc0,
};

int skiLargeTree4Bitmap[34] =
{
    16,16,
    0x1,0x0,0x1,0x80,0x3,0xc0,0x5,0xa0,0x1f,0xf8,0xf,0x90,0x17,0xf8,0x39,0xec,
    0x1f,0x98,0x27,0xfc,0x7b,0xb2,0x3f,0xfc,0xdb,0xcf,0x1,0x80,0x1,0x80,0x3,0xc0,
};

int skiSmallTree1Bitmap[10] =
{
    8,8,
    0x8,0x18,0x3c,0x7e,0xff,0x18,0x18,0x3c,
};

int skiSmallTree2Bitmap[10] =
{
    8,8,
    0x10,0x18,0x2c,0x5a,0xbf,0x18,0x18,0x3c,
};

int skiSmallTree3Bitmap[10] =
{
    8,8,
    0x8,0x18,0x3c,0x7e,0xff,0x7e,0x18,0x3c,
};

int skiSmallTree4Bitmap[10] =
{
    8,8,
    0x10,0x18,0x3c,0x72,0xdb,0x7e,0x18,0x3c,
};

int skiLog1Bitmap[6] =
{
    8,4,
    0x10,0xff,0xff,0x2,
};

int skiLog2Bitmap[6] =
{
    8,4,
    0x1,0xff,0xff,0x20,
};

int skiLog3Bitmap[6] =
{
    8,4,
    0x0,0xff,0xff,0x8,
};

int skiLog4Bitmap[6] =
{
    8,4,
    0x40,0xff,0xff,0x2,
};

// -----------------------------------------------------------------------------
// Entity system - see this file's own header comment for the real
// function-pointer/array-field/bitmap-pointer adaptations made here.
// -----------------------------------------------------------------------------

typedef struct
{
    int frame;
    int sprite0, sprite1, sprite2, sprite3;
    int rotation;
    int flip;
    int x, y;
    int type;
    int xspeed;
    int yspeed;
    int flag0, flag1, flag2, flag3; // JUMP, JUMPCTR, GAMEOVER, PLAYERTXT
    bool used;
    int think; // SKI_THINK_* dispatch id
} SkiEntity;

SkiEntity skiEntities[SKI_MAXENTS];
int skiPlayerIndex;

int skiCameraX, skiCameraY;
int skiMetersTraveled;
int skiHighScore;
bool skiHighScoreResetFlag;
int skiState;

// -----------------------------------------------------------------------------
// Score / EEPROM
// -----------------------------------------------------------------------------

int skiGetHighScore()
{
    // upstream's own local names for these two bytes are "two" (addr 0,
    // low byte) and "one" (addr 1, high byte) - see this file's own header
    // comment.
    int lo = eeprom_read_byte( 0 );
    int hi = eeprom_read_byte( 1 );

    // A genuinely fresh EEPROM (both bytes still 0xFF) - see this file's
    // own header comment for the full real-hardware-vs-this-dialect
    // derivation. Returning a plain 0 here (rather than the composed
    // 65535) matches real hardware's own actual functional outcome: any
    // real distance beats it.
    if( lo == 255 && hi == 255 )
      return 0;

    return lo + ( hi << 8 );
}

void skiSaveHighScore( int score )
{
    int lo = score & 0xFF;
    int hi = ( score >> 8 ) & 0xFF;
    eeprom_update_byte( 0, lo );
    eeprom_update_byte( 1, hi );
}

void skiDrawScore()
{
    gbPrintNumber( skiMetersTraveled );
    gbPrintString( "M" );
    if( skiMetersTraveled > skiHighScore )
      gbPrintString( "   New Best!" );
    else
    {
        gbPrintString( "    Best: " );
        gbPrintNumber( skiHighScore );
        gbPrintString( "M" );
    }
}

// -----------------------------------------------------------------------------
// Sound - real Sound.h envelope tables (Volume, Instrument ID, Volume Slide
// Step Duration/Depth, Pitch Slide Step Duration/Depth, Tremolo Step
// Duration/Depth, Pitch, Duration), copied verbatim.
// -----------------------------------------------------------------------------

int skiSndJump[10]   = { 4, 0, 1, -1, 1,  2, 0, 0, 23, 4 };
int skiSndSlide[10]  = { 4, 1, 1, -1, 1, -2, 0, 0, 16, 4 };
int skiSndCrash[10]  = { 6, 1, 1, -1, 0,  0, 0, 0,  0, 6 };
int skiSndResume[10] = { 1, 0, 0,  0, 4, -5, 0, 0, 60, 6 };
int skiSndPause[10]  = { 1, 0, 0,  0, 4,  5, 0, 0, 55, 6 };

// Direct port of real `playSound(const int *snd, byte channel)`.
void skiPlaySound( int* snd )
{
    gbSoundCommand( GB_CMD_VOLUME, snd[ 0 ], 0, 0 );
    gbSoundCommand( GB_CMD_INSTRUMENT, snd[ 1 ], 0, 0 );
    gbSoundCommand( GB_CMD_SLIDE, snd[ 2 ], snd[ 3 ], 0 );
    gbSoundCommand( GB_CMD_ARPEGGIO, snd[ 4 ], snd[ 5 ], 0 );
    gbSoundCommand( GB_CMD_TREMOLO, snd[ 6 ], snd[ 7 ], 0 );
    gbPlayNoteChannel( snd[ 8 ], snd[ 9 ], 0 );
}

// -----------------------------------------------------------------------------
// Entity management
// -----------------------------------------------------------------------------

void skiInitEntities()
{
    int i;
    for( i = 0; i < SKI_MAXENTS; i++ )
    {
        skiEntities[ i ].used = false;
        skiEntities[ i ].rotation = SKI_NOROT;
        skiEntities[ i ].flip = SKI_NOFLIP;
        skiEntities[ i ].think = SKI_THINK_NONE;
        skiEntities[ i ].type = SKI_TYPE_PLAYER; // see header comment - defensive default, not a behavior fix
        skiEntities[ i ].sprite0 = SKI_BMP_NONE;
        skiEntities[ i ].sprite1 = SKI_BMP_NONE;
        skiEntities[ i ].sprite2 = SKI_BMP_NONE;
        skiEntities[ i ].sprite3 = SKI_BMP_NONE;
        skiEntities[ i ].flag0 = 0;
        skiEntities[ i ].flag1 = 0;
        skiEntities[ i ].flag2 = 0;
        skiEntities[ i ].flag3 = 0;
    }
}

int skiSpawnEntity()
{
    int i;
    for( i = 0; i < SKI_MAXENTS; i++ )
    {
        if( !skiEntities[ i ].used )
        {
            skiEntities[ i ].used = true;
            return i;
        }
    }
    return -1;
}

void skiFreeEntity( int idx )
{
    skiEntities[ idx ].used = false;
    skiEntities[ idx ].think = SKI_THINK_NONE;
    skiEntities[ idx ].sprite0 = SKI_BMP_NONE;
    skiEntities[ idx ].sprite1 = SKI_BMP_NONE;
    skiEntities[ idx ].sprite2 = SKI_BMP_NONE;
    skiEntities[ idx ].sprite3 = SKI_BMP_NONE;
}

void skiClearEntities()
{
    int i;
    for( i = 0; i < SKI_MAXENTS; i++ )
      skiFreeEntity( i );
}

void skiClearObstacles()
{
    // No `.used` guard here, matching upstream's own real `clearObstacles()`
    // exactly - see this file's own header comment on why that's safe
    // (every never-spawned slot's own `type` is explicitly defaulted to
    // SKI_TYPE_PLAYER by skiInitEntities() above).
    int i;
    for( i = 0; i < SKI_MAXENTS; i++ )
      if( skiEntities[ i ].type == SKI_TYPE_OBSTACLE )
        skiFreeEntity( i );
}

int skiEntitySpriteId( int idx )
{
    int frame = skiEntities[ idx ].frame;
    if( frame == 0 ) return skiEntities[ idx ].sprite0;
    if( frame == 1 ) return skiEntities[ idx ].sprite1;
    if( frame == 2 ) return skiEntities[ idx ].sprite2;
    return skiEntities[ idx ].sprite3;
}

void skiGetSpriteWH( int spriteId, int* outW, int* outH )
{
    if( spriteId == SKI_BMP_PLAYER_NORMAL || spriteId == SKI_BMP_PLAYER_ANGLE ||
        spriteId == SKI_BMP_PLAYER_JUMP || spriteId == SKI_BMP_PLAYER_ANGLE2 )
    {
        *outW = 8;
        *outH = 10;
        return;
    }
    if( spriteId >= SKI_BMP_LARGETREE1 && spriteId <= SKI_BMP_LARGETREE4 )
    {
        *outW = 16;
        *outH = 16;
        return;
    }
    if( spriteId >= SKI_BMP_SMALLTREE1 && spriteId <= SKI_BMP_SMALLTREE4 )
    {
        *outW = 8;
        *outH = 8;
        return;
    }
    if( spriteId >= SKI_BMP_LOG1 && spriteId <= SKI_BMP_LOG4 )
    {
        *outW = 8;
        *outH = 4;
        return;
    }
    *outW = 0;
    *outH = 0;
}

// Real bitmap pointer for a given sprite id - mirrors skiGetSpriteWH()'s
// own if/else-if structure (and skiDrawAll()'s own identical dispatch)
// exactly, for skiCollideBitmapBitmap() below to use with the real
// gbCollideBitmapBitmap() primitive.
int* skiGetSpriteBitmap( int spriteId )
{
    if( spriteId == SKI_BMP_PLAYER_NORMAL ) return skiPlayerNormalBitmap;
    if( spriteId == SKI_BMP_PLAYER_ANGLE ) return skiPlayerAngleBitmap;
    if( spriteId == SKI_BMP_PLAYER_JUMP ) return skiPlayerJumpBitmap;
    if( spriteId == SKI_BMP_PLAYER_ANGLE2 ) return skiPlayerAngle2Bitmap;
    if( spriteId == SKI_BMP_LARGETREE1 ) return skiLargeTree1Bitmap;
    if( spriteId == SKI_BMP_LARGETREE2 ) return skiLargeTree2Bitmap;
    if( spriteId == SKI_BMP_LARGETREE3 ) return skiLargeTree3Bitmap;
    if( spriteId == SKI_BMP_LARGETREE4 ) return skiLargeTree4Bitmap;
    if( spriteId == SKI_BMP_SMALLTREE1 ) return skiSmallTree1Bitmap;
    if( spriteId == SKI_BMP_SMALLTREE2 ) return skiSmallTree2Bitmap;
    if( spriteId == SKI_BMP_SMALLTREE3 ) return skiSmallTree3Bitmap;
    if( spriteId == SKI_BMP_SMALLTREE4 ) return skiSmallTree4Bitmap;
    if( spriteId == SKI_BMP_LOG1 ) return skiLog1Bitmap;
    if( spriteId == SKI_BMP_LOG2 ) return skiLog2Bitmap;
    if( spriteId == SKI_BMP_LOG3 ) return skiLog3Bitmap;
    return skiLog4Bitmap;
}

// Real per-pixel equivalent of upstream's own `gb.collideBitmapBitmap()` -
// see this file's own header comment for the real correction (this used
// to compose the shim's own `gbCollideRectRect()` instead, based on a
// since-disproven claim that real hardware's own collideBitmapBitmap()
// is itself just a rect-rect test).
bool skiCollideBitmapBitmap( int a, int b )
{
    return gbCollideBitmapBitmap( skiEntities[ a ].x, skiEntities[ a ].y, skiGetSpriteBitmap( skiEntitySpriteId( a ) ),
                                   skiEntities[ b ].x, skiEntities[ b ].y, skiGetSpriteBitmap( skiEntitySpriteId( b ) ) );
}

void skiThinkAll();
void skiPlayerThink( int self );
void skiThinkGameOver( int self );
void skiTreeThink( int self );
void skiLogThink( int self );

void skiThinkAll()
{
    int i;
    for( i = 0; i < SKI_MAXENTS; i++ )
    {
        if( skiEntities[ i ].used && skiEntities[ i ].think != SKI_THINK_NONE )
        {
            if( skiEntities[ i ].think == SKI_THINK_PLAYER ) skiPlayerThink( i );
            else if( skiEntities[ i ].think == SKI_THINK_GAMEOVER ) skiThinkGameOver( i );
            else if( skiEntities[ i ].think == SKI_THINK_TREE ) skiTreeThink( i );
            else if( skiEntities[ i ].think == SKI_THINK_LOG ) skiLogThink( i );
        }
    }
}

void skiDrawAll()
{
    int i, spriteId, px, py;
    for( i = 0; i < SKI_MAXENTS; i++ )
    {
        if( skiEntities[ i ].used )
        {
            spriteId = skiEntitySpriteId( i );
            if( spriteId != SKI_BMP_NONE )
            {
                gbSetColor( 1 );
                px = skiEntities[ i ].x - skiCameraX;
                py = skiEntities[ i ].y - skiCameraY;

                if( spriteId == SKI_BMP_PLAYER_NORMAL )
                  gbDrawBitmapRotated( px, py, skiPlayerNormalBitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_PLAYER_ANGLE )
                  gbDrawBitmapRotated( px, py, skiPlayerAngleBitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_PLAYER_JUMP )
                  gbDrawBitmapRotated( px, py, skiPlayerJumpBitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_PLAYER_ANGLE2 )
                  gbDrawBitmapRotated( px, py, skiPlayerAngle2Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_LARGETREE1 )
                  gbDrawBitmapRotated( px, py, skiLargeTree1Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_LARGETREE2 )
                  gbDrawBitmapRotated( px, py, skiLargeTree2Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_LARGETREE3 )
                  gbDrawBitmapRotated( px, py, skiLargeTree3Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_LARGETREE4 )
                  gbDrawBitmapRotated( px, py, skiLargeTree4Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_SMALLTREE1 )
                  gbDrawBitmapRotated( px, py, skiSmallTree1Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_SMALLTREE2 )
                  gbDrawBitmapRotated( px, py, skiSmallTree2Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_SMALLTREE3 )
                  gbDrawBitmapRotated( px, py, skiSmallTree3Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_SMALLTREE4 )
                  gbDrawBitmapRotated( px, py, skiSmallTree4Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_LOG1 )
                  gbDrawBitmapRotated( px, py, skiLog1Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_LOG2 )
                  gbDrawBitmapRotated( px, py, skiLog2Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else if( spriteId == SKI_BMP_LOG3 )
                  gbDrawBitmapRotated( px, py, skiLog3Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
                else
                  gbDrawBitmapRotated( px, py, skiLog4Bitmap, skiEntities[ i ].rotation, skiEntities[ i ].flip );
            }
        }
    }
}

void skiInitCamera()
{
    skiCameraX = 0;
    skiCameraY = 0;
}

void skiUpdateCamera()
{
    if( !skiEntities[ skiPlayerIndex ].used ) return;

    skiCameraX = skiEntities[ skiPlayerIndex ].x - SKI_CAMERAXOFFSET;
    if( skiCameraX < 0 ) skiCameraX = 0;
    if( skiCameraX > ( SKI_MAPWIDTH - LCDWIDTH ) ) skiCameraX = SKI_MAPWIDTH - LCDWIDTH;

    skiCameraY = skiEntities[ skiPlayerIndex ].y - SKI_CAMERAYOFFSET;
    if( skiCameraY < 0 ) skiCameraY = 0;
    if( skiCameraY > ( SKI_MAPHEIGHT - LCDHEIGHT ) ) skiCameraY = SKI_MAPHEIGHT - LCDHEIGHT;
}

// -----------------------------------------------------------------------------
// Player
// -----------------------------------------------------------------------------

int skiSpawnPlayer( int x, int y )
{
    int self = skiSpawnEntity();
    if( self == -1 ) return -1;

    skiEntities[ self ].think = SKI_THINK_PLAYER;
    skiEntities[ self ].x = x;
    skiEntities[ self ].y = y;
    skiEntities[ self ].type = SKI_TYPE_PLAYER;
    skiEntities[ self ].xspeed = 0;
    skiEntities[ self ].yspeed = 2;
    skiEntities[ self ].sprite0 = SKI_BMP_PLAYER_NORMAL;
    skiEntities[ self ].sprite1 = SKI_BMP_PLAYER_ANGLE;
    skiEntities[ self ].sprite2 = SKI_BMP_PLAYER_JUMP;
    skiEntities[ self ].sprite3 = SKI_BMP_PLAYER_ANGLE2;
    skiEntities[ self ].frame = 0;
    skiEntities[ self ].flip = SKI_NOFLIP;
    skiEntities[ self ].rotation = SKI_NOROT;
    skiEntities[ self ].flag0 = 0; // JUMP
    skiEntities[ self ].flag1 = 0; // JUMPCTR
    skiEntities[ self ].flag2 = SKI_GAMEOVERTIMER; // GAMEOVER
    skiEntities[ self ].flag3 = 0; // PLAYERTXT

    return self;
}

void skiBeginPause();
void skiSpawnObstacles();

// Direct port of upstream's own real `playerThink()` - see this file's own
// header comment on the one deliberate simplification (the Button-C pause
// path returns immediately rather than resuming this same tick's tail
// logic once unpaused).
void skiPlayerThink( int self )
{
    skiEntities[ self ].y = skiEntities[ self ].y + skiEntities[ self ].yspeed;

    skiEntities[ self ].frame = 0;
    skiEntities[ self ].flip = SKI_NOFLIP;

    // D-Pad Right - Turn Right
    if( gbRepeat( BTN_RIGHT, 1 ) && !skiEntities[ self ].flag0 )
    {
        if( skiEntities[ self ].xspeed == 4 ) skiEntities[ self ].frame = 3;
        else skiEntities[ self ].frame = 1;
        skiEntities[ self ].flip = SKI_NOFLIP;
        skiEntities[ self ].x = skiEntities[ self ].x + skiEntities[ self ].xspeed;
    }

    // D-Pad Left - Turn Left
    if( gbRepeat( BTN_LEFT, 1 ) && !skiEntities[ self ].flag0 )
    {
        if( skiEntities[ self ].xspeed == 4 ) skiEntities[ self ].frame = 3;
        else skiEntities[ self ].frame = 1;
        skiEntities[ self ].flip = SKI_FLIPH;
        skiEntities[ self ].x = skiEntities[ self ].x - skiEntities[ self ].xspeed;
    }

    // Button B - Jump
    if( gbPressed( BTN_B ) )
      skiPlaySound( skiSndJump );
    if( gbRepeat( BTN_B, 1 ) )
    {
        if( !skiEntities[ self ].flag1 )
        {
            skiEntities[ self ].flag1 = SKI_JUMPCOOLDOWN;
            skiEntities[ self ].flag0 = SKI_JUMPDURATION;
        }
    }
    else
      skiEntities[ self ].flag0 = 0;

    // Button A - Power Slide
    if( gbPressed( BTN_A ) )
      skiPlaySound( skiSndSlide );
    if( gbRepeat( BTN_A, 1 ) && !skiEntities[ self ].flag0 )
      skiEntities[ self ].xspeed = 4;
    else
      skiEntities[ self ].xspeed = 2;

    // Button C - Pause
    if( gbPressed( BTN_C ) )
    {
        skiPlaySound( skiSndPause );
        skiBeginPause();
        return; // see this file's own header comment on the blocking-call simplification
    }

    // Jumping logic
    if( skiEntities[ self ].flag0 )
    {
        skiEntities[ self ].flag0 = skiEntities[ self ].flag0 - 1;
        skiEntities[ self ].frame = 2;
    }
    else
      skiEntities[ self ].flag0 = 0;

    if( skiEntities[ self ].flag1 )
      skiEntities[ self ].flag1 = skiEntities[ self ].flag1 - 1;

    // Keep player within map bounds
    if( skiEntities[ self ].x < SKI_XLIMITL ) skiEntities[ self ].x = SKI_XLIMITL;
    if( skiEntities[ self ].x > SKI_XLIMITR ) skiEntities[ self ].x = SKI_XLIMITR;

    // Refresh obstacles and warp player back to top
    if( skiEntities[ self ].y > SKI_SCROLLPOS )
    {
        skiClearObstacles();
        skiEntities[ self ].y = SKI_YSTARTPOS;
        skiSpawnObstacles();
    }

    // Update meters traveled and draw score text
    skiMetersTraveled = skiMetersTraveled + skiEntities[ self ].yspeed;
    skiDrawScore();
}

void skiRestartGame();

// Direct port of upstream's own real `thinkGameOver()` (switch -> if/else
// chain, see this file's own header comment).
void skiThinkGameOver( int self )
{
    skiEntities[ self ].flag2 = skiEntities[ self ].flag2 - 1;
    skiEntities[ self ].frame = 2;
    skiEntities[ self ].rotation = SKI_ROT180;

    skiDrawScore();

    gbCursorY = skiEntities[ self ].y - skiCameraY - 5;
    gbCursorX = skiEntities[ self ].x - skiCameraX;

    if( skiEntities[ self ].flag3 == 0 )
    {
        skiPlaySound( skiSndCrash );
        skiEntities[ self ].flag3 = arand( 9 ) + 1;
    }

    if( skiEntities[ self ].flag3 == 1 ) gbPrintString( "Ooof!" );
    else if( skiEntities[ self ].flag3 == 2 ) gbPrintString( "Medic!" );
    else if( skiEntities[ self ].flag3 == 3 ) gbPrintString( "Ouch." );
    else if( skiEntities[ self ].flag3 == 4 ) gbPrintString( "Doh!" );
    else if( skiEntities[ self ].flag3 == 5 ) gbPrintString( "My legs!" );
    else if( skiEntities[ self ].flag3 == 6 ) gbPrintString( "My back!" );
    else if( skiEntities[ self ].flag3 == 7 ) gbPrintString( "...help" );
    else if( skiEntities[ self ].flag3 == 8 ) gbPrintString( "Why me?" );
    else if( skiEntities[ self ].flag3 == 9 ) gbPrintString( "My neck!" );

    if( skiEntities[ self ].flag2 <= 0 )
      skiRestartGame();
}

// -----------------------------------------------------------------------------
// Obstacles
// -----------------------------------------------------------------------------

int skiRandObstacleY()
{
    // upstream's own real ranged `random(LCDHEIGHT, MAPHEIGHT-LCDHEIGHT-16)`
    // -> `min + arand(max-min)` (this dialect's own established idiom).
    return LCDHEIGHT + arand( ( SKI_MAPHEIGHT - LCDHEIGHT - 16 ) - LCDHEIGHT );
}

int skiSpawnLargeTree( int x, int y )
{
    int self = skiSpawnEntity();
    if( self == -1 ) return -1;

    skiEntities[ self ].think = SKI_THINK_TREE;
    skiEntities[ self ].x = x;
    skiEntities[ self ].y = y;
    skiEntities[ self ].type = SKI_TYPE_OBSTACLE;
    skiEntities[ self ].frame = 0;
    skiEntities[ self ].flip = SKI_NOFLIP;
    skiEntities[ self ].rotation = SKI_NOROT;
    skiEntities[ self ].flag0 = 0;

    int r = arand( 4 );
    if( r == 0 ) skiEntities[ self ].sprite0 = SKI_BMP_LARGETREE1;
    else if( r == 1 ) skiEntities[ self ].sprite0 = SKI_BMP_LARGETREE2;
    else if( r == 2 ) skiEntities[ self ].sprite0 = SKI_BMP_LARGETREE3;
    else skiEntities[ self ].sprite0 = SKI_BMP_LARGETREE4;

    return self;
}

int skiSpawnSmallTree( int x, int y )
{
    int self = skiSpawnEntity();
    if( self == -1 ) return -1;

    skiEntities[ self ].think = SKI_THINK_TREE;
    skiEntities[ self ].x = x;
    skiEntities[ self ].y = y;
    skiEntities[ self ].type = SKI_TYPE_OBSTACLE;
    skiEntities[ self ].frame = 0;
    skiEntities[ self ].flip = SKI_NOFLIP;
    skiEntities[ self ].rotation = SKI_NOROT;
    skiEntities[ self ].flag0 = 0;

    int r = arand( 4 );
    if( r == 0 ) skiEntities[ self ].sprite0 = SKI_BMP_SMALLTREE1;
    else if( r == 1 ) skiEntities[ self ].sprite0 = SKI_BMP_SMALLTREE2;
    else if( r == 2 ) skiEntities[ self ].sprite0 = SKI_BMP_SMALLTREE3;
    else skiEntities[ self ].sprite0 = SKI_BMP_SMALLTREE4;

    return self;
}

void skiTreeThink( int self )
{
    if( skiCollideBitmapBitmap( skiPlayerIndex, self ) )
      skiEntities[ skiPlayerIndex ].think = SKI_THINK_GAMEOVER;
}

int skiSpawnLog( int x, int y )
{
    int self = skiSpawnEntity();
    if( self == -1 ) return -1;

    skiEntities[ self ].think = SKI_THINK_LOG;
    skiEntities[ self ].x = x;
    skiEntities[ self ].y = y;
    skiEntities[ self ].type = SKI_TYPE_OBSTACLE;
    skiEntities[ self ].frame = 0;
    skiEntities[ self ].flip = SKI_NOFLIP;
    skiEntities[ self ].rotation = SKI_NOROT;
    skiEntities[ self ].flag0 = 0;

    int r = arand( 4 );
    if( r == 0 ) skiEntities[ self ].sprite0 = SKI_BMP_LOG1;
    else if( r == 1 ) skiEntities[ self ].sprite0 = SKI_BMP_LOG2;
    else if( r == 2 ) skiEntities[ self ].sprite0 = SKI_BMP_LOG3;
    else skiEntities[ self ].sprite0 = SKI_BMP_LOG4;

    return self;
}

void skiLogThink( int self )
{
    if( skiEntities[ skiPlayerIndex ].flag0 ) return; // JUMP - clears logs

    if( skiCollideBitmapBitmap( skiPlayerIndex, self ) )
      skiEntities[ skiPlayerIndex ].think = SKI_THINK_GAMEOVER;
}

void skiSpawnObstacles()
{
    int i;
    for( i = 0; i < 10; i++ )
    {
        skiSpawnLargeTree( arand( SKI_MAPWIDTH ), skiRandObstacleY() );
        skiSpawnSmallTree( arand( SKI_MAPWIDTH ), skiRandObstacleY() );
    }
    for( i = 0; i < 10; i++ )
      skiSpawnLog( arand( SKI_MAPWIDTH ), skiRandObstacleY() );
}

// -----------------------------------------------------------------------------
// Game/state flow
// -----------------------------------------------------------------------------

void skiStartGame()
{
    skiHighScore = skiGetHighScore();
    skiMetersTraveled = 0;
    skiPlayerIndex = skiSpawnPlayer( SKI_XSTARTPOS, SKI_YSTARTPOS );
    skiSpawnObstacles();
}

void skiRestartGame()
{
    if( skiMetersTraveled > skiHighScore )
      skiSaveHighScore( skiMetersTraveled );

    skiClearEntities();
    skiStartGame();
}

void skiBeginTitle()
{
    skiState = SKI_STATE_TITLE;
}

// Real upstream `setup()`'s own post-titleScreen tail
// (`initEntities();initCamera();startGame();`) - run once, the moment the
// title screen is genuinely dismissed (at boot, or after returning to the
// title screen from the pause menu - see this file's own header comment).
void skiBeginPlayFresh()
{
    skiInitEntities();
    skiInitCamera();
    skiStartGame();
    skiState = SKI_STATE_PLAY;
}

void skiBeginPause()
{
    skiHighScoreResetFlag = false;
    skiState = SKI_STATE_PAUSE;
}

void skiResumePlay()
{
    skiState = SKI_STATE_PLAY;
}

void skiUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( ( LCDWIDTH - 72 ) / 2, 0, skiGameLogoBitmap );

    gbCursorX = 28;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      skiBeginPlayFresh();
}

// Direct port of upstream's own real `loop()` body.
void skiUpdatePlay()
{
    skiThinkAll();
    if( skiState != SKI_STATE_PLAY ) return; // a pause was entered mid-think - see this file's own header comment
    skiUpdateCamera();
    skiDrawAll();
}

// Direct, near-verbatim port of upstream's own real `pauseGame()` (its own
// blocking `while(1)` loop replaced by one dispatch per real tick - see
// this file's own header comment).
void skiUpdatePause()
{
    skiDrawScore();

    if( skiHighScoreResetFlag )
    {
        gbCursorX = 12;
        gbCursorY = 18;
        gbPrintString( "RESET HIGH SCORE\n   ...are you sure?\n" );

        gbCursorY = 35;
        gbPrintString( "A+B: Confirm\n  C: Cancel\n" );

        // Buttons A + B - Reset High Score
        if( gbRepeat( BTN_A, 1 ) && gbRepeat( BTN_B, 1 ) )
        {
            skiHighScoreResetFlag = true;
            skiSaveHighScore( 0 );
            skiMetersTraveled = 0;
            skiRestartGame();
            skiResumePlay();
            return;
        }

        // Button C - Cancel
        if( gbPressed( BTN_C ) )
          skiHighScoreResetFlag = false;
    }
    else
    {
        gbCursorX = 20;
        gbCursorY = 18;
        gbPrintString( "P A U S E D\n" );

        gbCursorY = 30;
        gbPrintString( "A: Title Screen\nB: Reset High Score\nC: Resume Game\n" );

        // Button A - Return to Title Screen
        // Fixed: real upstream's own "A: Title Screen" handler calls only
        // restartGame() (unreachable from here) and never saveHighScore() -
        // see this file's own header comment - so quitting to the title
        // screen mid-run silently discarded a genuinely higher distance.
        // The same highscore check skiRestartGame() already performs is run
        // here too, before the run's own state is cleared.
        if( gbPressed( BTN_A ) )
        {
            if( skiMetersTraveled > skiHighScore )
              skiSaveHighScore( skiMetersTraveled );

            skiClearEntities();
            skiBeginTitle();
            return;
        }

        // Button B - Reset High Score
        if( gbPressed( BTN_B ) )
          skiHighScoreResetFlag = true;

        // Button C - Resume Game
        if( gbPressed( BTN_C ) )
        {
            skiPlaySound( skiSndResume );
            skiResumePlay();
            return;
        }
    }
}

void gameSkibuino_init()
{
    gbBegin();
    gbPickRandomSeed();
    skiBeginTitle();
}

void gameSkibuino_update()
{
    if( !gbUpdate() ) return;

    if( skiState == SKI_STATE_TITLE ) skiUpdateTitle();
    else if( skiState == SKI_STATE_PLAY ) skiUpdatePlay();
    else skiUpdatePause();

    gbRenderFrame();
}
