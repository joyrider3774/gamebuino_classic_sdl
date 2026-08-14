#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Castle Defence (kh9282, no license specified -
// github.com/kh9282/CastleDefence). A top-down tower-defense-flavored
// shooter: monsters climb five fixed lanes toward a castle at the top of
// the screen; the player walks a horizontal strip near the bottom and
// shoots them with one of three weapons (rifle/shotgun/sniper, chosen as
// a main+sub pair at the start of a run) before they reach the castle
// wall. Every 15 kills levels up (raising monster HP/speed and, every 5th
// level, spawning a boss "king" that must be killed to unlock a shop
// where coins earned from kills buy permanent damage/ammo/ability
// upgrades). Source is 5 real upstream `.ino` tabs sharing one real
// Arduino translation unit (Castle.ino/attack.ino/image.ino/monster.ino/
// shop.ino, confirmed no other `.ino` files exist in the staged
// directory) - all read in full before porting.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment for the full reasoning). Upstream's
// own `byte`/`boolean` types became plain `int`/`bool` (this project's own
// established avrCompat.h convention - see that file's own header comment
// on why aliasing every AVR fixed-width type costs only range, not
// correctness, for the vast majority of code). Upstream's own C++ structs
// (`Gun`, `Monster`) ported directly as real Vircon32 structs
// (`CdefGun[3] cdefGuns`, `CdefMonster[5] cdefMon`) - struct arrays are
// proven to compile in this dialect (gameFlappyBirdo.c's own `FlapPipe[3]
// flapPipes`, gameAgaruino.c's own `AgarBall` struct, etc.), and every
// array declaration everywhere in this file uses this dialect's own
// required `TYPE[N] name` order, never `TYPE name[N]`. No `switch`
// statement is used anywhere (every real upstream `switch(weapon)`/
// `switch(Me_HP)`/`switch(mon_num)`/etc became an if/else-if chain,
// matching this project's own "no switch statement proven to work"
// convention). Every real Arduino `B00000000`-style binary literal (only
// present in `image.ino`'s own `Rifle[]`/`Shotgun[]`/`SniperRifle[]`/
// `rifle_img[]`/`shotgun_img[]`/`sniper_img[]`/`motion_slow[]` bitmaps -
// every other bitmap table upstream already used plain `0x` hex) was
// converted to `0x` hex by a small one-off local script, byte-count
// verified against each bitmap's own real `{width,height,...}` header
// before being pasted in below - every other real PROGMEM bitmap byte
// here is copied verbatim, unchanged. Every real `random(N)`/
// `random(min,max)` call became `arand(N)`/`min + arand(max-min)` (this
// project's own established RNG-conversion formula), including upstream's
// own real range bugs this produces byte-for-byte (see "Preserved real
// upstream bugs" below). `gb.pickRandomSeed()` has no call site in this
// particular upstream source (it's never called) - not ported, nothing to
// no-op. Global naming prefix: `cdef` (verified unused by every other game
// shipped or concurrently in flight in this project).
//
// REAL 2D BITMAP TABLES (`fence[][18]`/`monster[][20]`/`king[][40]`/
// `king_skil[][40]`/`motion_stun[][38]`) ported as real Vircon32 2D
// `int[N][M]` tables (`cdefFenceBitmaps`/`cdefMonsterBitmaps`/
// `cdefKingBitmaps`/`cdefKingSkilBitmaps`/`cdefMotionStunBitmaps`),
// exactly like this project's own already-proven `gameLander.c`
// (`landSpaceship[6][8]`)/`gameAsterocks.c` (`asterShipFrames[20][9]`)
// precedent - `cdefKingBitmaps[i]` decays to the `int*` `gbDrawBitmap()`
// expects, exactly like those two files' own established call pattern.
// `motion_stun[][38]` upstream is genuinely a 3-entry table whose real
// LAST entry is `{}` (an empty C initializer, i.e. every one of its 38
// bytes real-C-zero-fills to 0) - real `stunmotion_time[]` cycling 0,1,2
// really does index into that empty entry one frame in three (see
// `Motion_stun()`'s own real `<=1` cycle test), so `cdefMotionStunBitmaps`
// keeps a real explicit all-zero third row (`{0,0,0,...}`, width=0/
// height=0) rather than silently dropping it - `gbDrawBitmap()` on a 0x0
// bitmap is a real, harmless no-op, reproducing upstream's own actual
// "blank flicker frame" every third stun-animation tick exactly.
//
// BLOCKING-LOOP -> STATE MACHINE, upstream's own real nested
// `while(weapon_select){if(gb.update()){...}}` / `while(!readyGo){...}` /
// blocking `GameOver()`/`display_highScore()`/`displaymessage()`-modal
// while(true) loops all became explicit states (`CDEF_STATE_TITLE`/
// `_WEAPON_SELECT`/`_READY`/`_PLAY`/`_SHOP_MESSAGE`/`_GAMEOVER`, plus
// `CDEF_STATE_HIGHSCORE` with a `cdefHighscoreReturn` field remembering
// which of the two real call sites - weapon-select's own UP-button peek,
// or the post-game-over flow - should resume once it's dismissed),
// matching the "blocking loop -> explicit resumable state" treatment
// this project uses throughout (see gamePong.c's own header comment).
// Upstream's real `gb.titleScreen(F("Castle Defence!"))` (called with no
// logo bitmap, at 3 real call sites: setup(), weapon-select's own
// Button-C "restart", and GameOver()'s own ending) is now one shared
// `cdefBeginTitle()`/`cdefUpdateTitle()` pair, dismissed by a genuine
// fresh `gbPressed(BTN_A)` exactly like every other titleScreen()
// conversion in this project - `cdefInitGame()` (the direct port of
// upstream's own real `initGame()`) runs on that same A-press, matching
// upstream's own real "titleScreen() blocks, THEN initGame() runs" call
// order at all 3 real sites. `load_Highscore()` itself only has one real
// call site (setup(), once ever) and is ported the same way, called once
// from `gameCastleDefence_init()`, not tied to every title re-visit.
//
// A REAL CONTROL-FLOW-ABANDONMENT QUIRK, preserved on purpose: upstream's
// own `GameOver()` and `displaymessage()`'s own buy-modal are both
// genuinely blocking calls made from partway through the main per-tick
// draw sequence (`displayHP()`'s own `case 0: GameOver();` and
// `display_Shop()`'s own per-item `displaymessage()` call) - meaning
// real hardware never executes the REST of that tick's own remaining
// upstream code (shot-bar/ammo/level-up/monster-drop/the other 8 shop
// item boxes/boss-kill-check/player-draw/movement/attack/reload/weapon-
// change) once either blocking path is entered, for that tick or any
// future tick until the blocking call itself returns. Reproduced exactly
// here: `cdefUpdatePlay()` explicitly checks `cdefState` immediately
// after calling `cdefDisplayHp()` and after calling `cdefDisplayShop()`
// and returns early the instant either one has switched state to
// `CDEF_STATE_GAMEOVER`/`CDEF_STATE_SHOP_MESSAGE`, so the remainder of
// that tick's own draw/input code is genuinely skipped, matching real
// hardware's own abandoned call stack rather than "helpfully" letting
// the rest of the tick run anyway.
//
// PRESERVED REAL UPSTREAM BUGS (kept exactly as shipped, not fixed - this
// project's own default per CLAUDE.md, matching the precedent already set
// by e.g. gameAgaruino.c's own preserved backwards on-screen-culling
// test):
//
// 1) FIXED, NOT PRESERVED - was: `displayHP()`'s own real `switch(Me_HP)
//    {case 7:...case 0:GameOver();}` had NO case for `Me_HP==1` (drawn HP
//    bar segment silently vanished for exactly 1 HP) and, critically, NO
//    default/fallback for any negative value either - and a boss hit
//    subtracts 2 in one step (`Me_HP -= 2`), so a player at exactly 1 HP
//    hit by the boss dropped straight to -1, skipping over the exact
//    `case 0` match GameOver() needed to ever trigger - a real potential
//    "HP goes permanently negative, game never ends" soft-lock. Fixed in
//    `cdefDisplayHp()`: a case for HP==1 now draws a final sliver, and the
//    game-over check is `<= 0` instead of `== 0`.
// 2) FIXED, NOT PRESERVED - was: `GameOver()`'s own real end-of-round slide
//    animation (`if(camera_y) camera_y++; else if(camera_y<=0){...show
//    GAME OVER text/scoring...}`) only ever reached its "else" branch (the
//    one that actually shows the GAME OVER text and reads Button B) once
//    `camera_y` counted UP to exactly 0 - which only happens if `camera_y`
//    was negative or already 0 at the moment of death. If the player had
//    been climbing (camera_y is incremented positive while moving up
//    beyond the starting view - a completely normal, easily reached play
//    pattern), `camera_y` at death was already positive, and incrementing
//    a positive number only moved it further from 0 - so the GAME OVER
//    text (and the only Button-B check that can exit this state) could
//    permanently never appear. Fixed in `cdefUpdateGameOver()`: camera_y
//    now moves toward 0 from either side.
// 3) FIXED, NOT PRESERVED - was: the real Level-up floor guard
//    `if(!mon_Ramdom_MAX <= 40) mon_Ramdom_MAX -= 10;` is a real operator-
//    precedence bug: `!` binds tighter than `<=`, so `!mon_Ramdom_MAX` (0
//    or 1) compared `<= 40` was ALWAYS true regardless of
//    `mon_Ramdom_MAX`'s actual value - the intended "stop decreasing once
//    low enough" floor never applied, and `mon_Ramdom_MAX -= 10` really
//    ran unconditionally every time `Level % 3 == 0`, shrinking monster
//    toughness variance unboundedly instead of leveling off. Fixed in the
//    level-up block in `cdefUpdatePlay()`: the decrement is now genuinely
//    gated on `cdefMonRandomMax > 40`.
// 4) FIXED, NOT PRESERVED - was: `shop.ino`'s own `displaymessage()`, for
//    `weapons==0` (rifle) only, checked `if(type==2){...life-recovery
//    text/purchase...}` as a SEPARATE, unconditional statement AFTER the
//    `if(owned){...} else {println(" Impossible!");}` block - not nested
//    inside an else-if chain the way the shotgun/sniper cases
//    (`weapons==1`/`2`) both are. Since the life-recovery item (row 3,
//    column 1 of the shop) is reachable regardless of whether the player
//    actually owns the rifle, a player who didn't pick the rifle saw BOTH
//    "Impossible!" (from the ownership check) AND the real life-recovery
//    text/purchase option stacked on the same screen. Fixed in
//    `cdefUpdateShopMessage()`'s display code: `type==2` is now checked
//    first and shown on its own, with "Impossible!" only shown for the two
//    real rifle upgrades when the rifle isn't owned - the buy-on-A-press
//    logic already correctly allowed life recovery regardless of
//    ownership and needed no change.
// 5) Every upstream `random(min,max)` real range quirk survives this
//    project's own established `min + arand(max-min)` conversion exactly
//    (matching real Arduino `random(min,max)`'s own `[min,max)`
//    half-open range) - e.g. `displayHP()`'s own critical-HP shake jitter
//    (`random(-1,+1)`) really only ever produces -1 or 0, never +1, on
//    real hardware too; ported as `-1 + arand(2)` (never `+1`), not
//    "fixed" into a symmetric ±1 jitter.
// 6) BYTE WRAPAROUND, NOT REPRODUCED (a whole-project simplification, not
//    specific to this game - noted once here rather than at every
//    individual occurrence): several real upstream fields this game
//    mutates with a signed delta were declared `byte` (real unsigned
//    8-bit AVR arithmetic, e.g. `shake_x`/`shake_y`, `mon_Ramdom_MAX`,
//    `Boss_message`) - on real hardware, e.g. `shake_x += (a negative
//    random value)` from a 0 baseline would wrap to a huge value near
//    255 instead of going negative. This project's own established
//    avrCompat.h convention aliases every AVR fixed-width type straight
//    to a full-range `int` project-wide (see that file's own header
//    comment) specifically because reproducing every individual byte-
//    wraparound site case-by-case was judged not worth the fragility -
//    so this specific wraparound quirk does not reproduce here, unlike
//    the bugs 1-5 above (which are all real *logic* bugs, not artifacts
//    of AVR's own integer width, and so are preserved).
//
// A REAL BUG THAT DOES **NOT** REPRODUCE HERE, BY DESIGN, NOT OVERSIGHT:
// upstream's own weapon-select screen calls the real blocking
// `display_highScore()` synchronously from inside its own per-tick body
// on an UP press, and - because real hardware's own nested `while(true)`
// loops share one continuous button-sample state within a single call
// stack - the SAME physical A/B/C press used to dismiss that highscore
// screen can still read as "just pressed" an instant later back in
// weapon-select's own remaining per-tick code path (never re-sampled by a
// fresh `gb.update()` in between) - the exact same class of bug this
// project's own `md_armInputAGate()` exists to prevent elsewhere (see
// this project's own CLAUDE.md). This port's own state-machine
// architecture (one state's update function runs per real engine tick,
// full stop - `CDEF_STATE_HIGHSCORE` returning to
// `CDEF_STATE_WEAPON_SELECT` takes effect on the FOLLOWING tick, which
// calls `gbUpdate()` fresh and re-samples buttons for real) structurally
// cannot reproduce this one, the same way gamePong.c's/gameMaze.c's own
// title/play transitions already don't - not something to fix, since it
// was never introduced in the first place.
//
// SHIM GAPS - ALREADY-DOCUMENTED, NOT NEW (established precedent exists
// in other ported games; not flagged as a fresh finding):
// - `gb.popup(text, 10)` (real transient overlay, used 3 times in
//   `displaymessage()`'s own buy logic for "Upgrade complete!"/"Have no
//   money."/"Your Life is full.") is provided directly by `gbPopup()` in
//   `gamebuinoShim.h`/`.c` (a direct port of real `Gamebuino::popup()`/
//   `updatePopup()`), which auto-draws itself on top of everything else
//   already drawn that frame, regardless of screen/state - all three call
//   sites call `gbPopup(text, 10)` directly (the same real upstream
//   duration).
// - `gb.getDefaultName()`/`gb.keyboard()` (real on-screen name entry for
//   a new high score) has no shim equivalent (see gameUfoRace.c's own
//   header comment) - per-name storage was dropped entirely rather than
//   faked with a placeholder string, matching that file's own established
//   precedent: `cdefHighscores[5]` is a plain scores-only table (no name
//   column at all, upstream's own `name[][]`/`NAME_LENGTH` not ported).
// - `gb.battery.show = false;` was dropped outright (purely cosmetic on
//   real hardware, no equivalent needed - matching every other port's own
//   treatment).
// - `gb.display.textWrap = false;` (set once, in `display_highScore()`)
//   was dropped as a genuine no-op rather than a gap: this shim's own
//   `gbPrintString()` never auto-wraps at the screen edge in the first
//   place (only an explicit `\n` moves to a new line - see
//   gamebuinoShim.c's own header comment), so upstream's own request to
//   disable wrapping is already exactly this shim's only real behavior.
// - Real low-ASCII icon glyphs upstream embeds inline via octal escapes
//   (`"please button\26"`, `"Reload \35"`, `"DANGER\01"`,
//   `"\25:Buy        \26:cancel"` - decimal 22/29/1/21+22) are restored
//   via direct `gbDrawChar(code, x, y)` calls at the right point in each
//   string (`cdefDrawIcon()` below) rather than embedded as raw control
//   bytes inside a string literal, matching this project's own
//   established gameHexagon.c/gameJezzball.c/gamePunkt.c precedent for
//   exactly this situation.
// No other shim gap was found - `gbDrawBitmap()`/`gbDrawChar()`/
// `gbRepeat()`/`gbCollidePointRect()`/real `gbFont5x7`/`gbFont3x5`/
// `eeprom_read_word()`/`eeprom_write_word()` all exist and are used
// directly as documented, with no local workaround needed for any of
// them.
//
// EEPROM: upstream genuinely calls real `EEPROM.read()`/`EEPROM.write()`
// in `save_Highscore()`/`load_Highscore()` for a real 5-entry high-score
// table, so this port genuinely uses this project's own eepromShim.h
// (`eeprom_read_word()`/`eeprom_write_word()`, 2 bytes per score, 5
// scores = addresses 0-9 of the shim's own 1024-byte-per-game slot) -
// not invented persistence upstream doesn't have. Upstream's own real
// fresh-EEPROM guard (`Highscores[i] == 0xFFFF ? 0 : Highscores[i]`,
// since real factory-erased AVR EEPROM - and this shim's own fresh
// cells, see eepromShim.h's own header comment - both read as 0xFF per
// byte) is preserved exactly in `cdefLoadHighscore()` below.
//
// CURSOR-CASCADE NOTE: this shim's own `gbUpdate()` resets
// `gbCursorX`/`gbCursorY` to (0,0) at the start of every real engine tick
// (a shim-specific convenience documented directly in gamebuinoShim.c,
// unlike real hardware's own persistent cursor) - so upstream's own
// several `println()` cascades that never explicitly set a starting
// cursor position (e.g. `displayHP()`'s own "Lv"/level/"$"/coin block)
// resolve to the exact same deterministic on-screen position every
// single tick here, rather than inheriting whatever an unrelated
// previous frame happened to leave the cursor at on real hardware - a
// cleaner, still fully upstream-literal (upstream's own source never
// specifies a position for that block either) reproduction. A small
// local `cdefPrintln()`/`cdefPrintlnNum()` pair (print, then advance
// `gbCursorY` by one real font line and reset `gbCursorX` to 0) stands in
// for upstream's own real `Print::println()` throughout, since this
// shim's own `gbPrintString()` only auto-advances on an explicit `\n`
// inside the string it's given, not automatically after every call.
//
// Dead/unused code NOT ported (confirmed genuinely inert, not a missed
// feature): upstream's own top-level `byte screen;` is declared and
// assigned `screen = 0;` in `initGame()` but never read anywhere in any
// of the 5 real `.ino` files - matching gameLander.c's own "confirmed
// dead" precedent, it's simply not ported here at all.

// -----------------------------------------------------------------------------
// Real upstream bitmaps - copied byte-for-byte from image.ino's own real
// `const byte NAME[] PROGMEM = { width, height, ... }` arrays (every real
// PROGMEM byte becomes one plain int cell, matching every other bitmap
// table in this project). The handful that used real Arduino
// `B00000000`-style binary literals (Rifle/Shotgun/SniperRifle/
// rifle_img/shotgun_img/sniper_img/motion_slow) were converted to `0x`
// hex - byte-count-verified against each one's own real header before
// being pasted in, no bits altered.
// -----------------------------------------------------------------------------

int cdefSelectBitmap[386]= {
    64,48,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x80,0x0,0x0,0x0,0x0,0x0,0x7,0x0,0x80,0x2,0x0,0x0,
    0x0,0x0,0x8,0x0,0x80,0x2,0x1,0xC0,0x0,0x0,0x8,0x18,0x8C,0x37,0x82,0x29,
    0x48,0x0,0xC,0x24,0x92,0x4A,0x2,0x9,0x48,0x0,0x6,0x3C,0x9E,0x42,0x2,0xE9,
    0x68,0x0,0x1,0x20,0x90,0x42,0x2,0x29,0x58,0x0,0x1,0x24,0x92,0x4A,0x2,0x29,
    0x48,0x0,0xE,0x18,0x8C,0x31,0x81,0xC6,0x48,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x20,0x0,0x0,0x0,0x3F,
    0xE0,0x0,0x7F,0xE0,0xF,0xFE,0x0,0x6,0x0,0x0,0x7F,0xE0,0x3F,0xF8,0x0,0xFF,
    0xFF,0x0,0xFF,0x0,0x3F,0xF8,0x3,0xFF,0xF8,0x0,0xCE,0x0,0x38,0x0,0x3,0xFF,
    0x0,0x0,0xCE,0x0,0x30,0x0,0x3,0xC0,0x0,0x0,0x83,0x0,0x30,0x0,0x3,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x38,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x6C,0xE2,0x4A,0x5D,0x26,0x0,0x0,0x0,0x44,0x95,0x6A,0x89,0xA8,
    0x0,0x0,0x0,0x6C,0xE7,0x5B,0x9,0x6B,0x0,0x0,0x0,0x6C,0x95,0x4A,0x89,0x29,
    0x0,0x0,0x0,0x38,0x95,0x4A,0x5D,0x26,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x38,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x44,0xEA,0xEE,0x0,
    0x0,0x0,0x0,0x0,0x5C,0x8A,0x44,0x0,0x0,0x0,0x0,0x0,0x5C,0xE4,0x44,0x0,
    0x0,0x0,0x0,0x0,0x44,0x8A,0x44,0x0,0x0,0x0,0x0,0x0,0x38,0xEA,0xE4,0x0,
    0x0,0x0,
};

int cdefPointBitmap[9]= {
    8,7,0x20,0x70,0xF8,0x70,0x70,0x70,0x70,
};

int cdefCastleBitmap[146]= {
    72,16,0xFA,0x40,0x40,0x40,0x40,0x40,0x40,0x85,0xF0,0xFB,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFD,0xF0,0xFA,0x4,0x4,0x4,0x4,0x4,0x4,0xD,0xF0,0xFA,0x4,0x4,
    0x4,0x4,0x4,0x4,0xD,0xF0,0x2,0x4,0x4,0x4,0x4,0x4,0x4,0xC,0x0,0x3,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFC,0x0,0x4,0x40,0x40,0x40,0x40,0x40,0x40,0x82,
    0x0,0xF8,0x40,0x40,0x40,0x40,0x40,0x40,0x81,0xF0,0x40,0x40,0x40,0x40,0x40,0x40,
    0x40,0x80,0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xF0,
};

int cdefShotBitmap[425]= {
    72,47,0x0,0x0,0x0,0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x20,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x10,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x8,0x0,0x8,0x0,0x0,0x0,0x0,0x0,0x0,0x8,0x0,0x10,0x0,0x0,0x0,
    0x0,0x0,0x0,0x8,0x0,0x20,0x0,0x0,0x0,0x0,0x0,0x0,0x10,0x0,0x40,0x0,
    0x0,0x0,0x0,0x0,0x0,0x9,0xF0,0x80,0x0,0x0,0x0,0x0,0x8,0x0,0x9,0xB1,
    0x0,0x0,0x2,0x0,0x0,0x4,0x0,0x5,0x91,0x0,0x0,0x4,0x40,0x0,0x1,0x0,
    0x5,0xE2,0x0,0x0,0x18,0x37,0x0,0x0,0xC0,0xD,0x22,0x0,0x0,0xE0,0x0,0xC0,
    0x0,0x68,0xF,0x61,0x0,0x1,0x0,0x0,0x38,0x0,0x2E,0x2,0xC8,0x80,0x6,0x0,
    0x0,0x7,0xE0,0x1F,0x87,0xC8,0x40,0x38,0x0,0x0,0x0,0x18,0xF,0xC3,0x90,0x20,
    0xC0,0x0,0x0,0x0,0xC,0xD,0xC3,0x90,0x21,0x0,0x0,0x0,0x0,0x3,0x84,0xF7,
    0x60,0x1E,0x0,0x0,0x0,0x0,0x0,0x61,0x77,0xE0,0x60,0x0,0x0,0x0,0x0,0x0,
    0x11,0x37,0xE0,0xC0,0x0,0x0,0x0,0x0,0x0,0xD,0xBB,0xFF,0x6,0x0,0x0,0x0,
    0x0,0xC,0x3,0x78,0x78,0x78,0x0,0x0,0x0,0x0,0x3,0x81,0x0,0x70,0xC0,0x0,
    0x0,0x0,0x0,0x0,0x7E,0x1,0xFE,0xC0,0x0,0x0,0x0,0x0,0x0,0x33,0x0,0xFF,
    0xD0,0x0,0x0,0x0,0x0,0x1,0xFF,0x80,0x7F,0x2E,0x0,0x0,0x0,0x0,0x1,0x8F,
    0xC0,0x75,0x0,0x0,0x0,0x0,0x0,0x3,0xC7,0x0,0x47,0x0,0x0,0x0,0x0,0x0,
    0x1F,0xF,0x80,0x41,0xE0,0x0,0x0,0x0,0x0,0x20,0x6F,0xF3,0x20,0x18,0x0,0x0,
    0x0,0x0,0x3,0x3,0xFB,0x58,0x4,0x0,0x0,0x0,0x0,0x1C,0x3,0x7F,0x84,0x1,
    0x0,0x0,0x0,0x0,0x24,0x3,0x79,0x2,0x0,0x80,0x0,0x0,0x1,0xC2,0x4,0xFD,
    0x91,0x80,0x0,0x0,0x0,0xE,0x1,0x8,0xF1,0xD8,0x60,0x0,0x0,0x0,0x30,0x0,
    0x89,0xF1,0xC8,0x30,0x0,0x0,0x1,0xC0,0x0,0x41,0x30,0xF8,0x8,0x0,0x0,0xE,
    0x0,0x0,0x23,0xF0,0x7C,0x2,0x0,0x0,0x10,0x0,0x0,0x62,0xF8,0x3A,0x1,0x80,
    0x0,0xE0,0x0,0x0,0xC6,0x44,0xE,0x0,0x78,0x0,0x0,0x0,0x1,0x7,0xC4,0x3,
    0x0,0x6,0xC0,0x0,0x0,0x2,0x0,0x42,0x5,0x80,0x0,0x38,0x0,0x0,0x4,0x0,
    0x40,0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x80,0x0,0x20,0x0,0x0,0x0,0x0,
    0x0,0x0,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x0,0x0,
};

int cdefHpBarBitmap[27]= {
    8,25,0x60,0x90,0x90,0x90,0xF0,0x90,0x90,0x90,0xF0,0x90,0x90,0x90,0xF0,0x90,
    0x90,0x90,0xF0,0x90,0x90,0x90,0xF0,0x90,0x90,0x90,0x60,
};

int cdefShopBitmap[434]= {
    72,48,0x7,0x0,0x3,0x80,0x0,0x7,0xFF,0xF8,0x0,0x8,0xA5,0x24,0x52,0x67,
    0x9,0x0,0x4,0x0,0x8,0x25,0x24,0x12,0x94,0x8B,0x80,0x4,0x0,0xB,0xA5,0xA3,
    0x9E,0x94,0x8B,0x0,0x4,0x0,0x8,0xA5,0x60,0x52,0x97,0x8,0x80,0x4,0x0,0x8,
    0xA5,0x24,0x52,0x94,0xB,0x80,0x4,0x0,0x7,0x19,0x23,0x92,0x64,0x9,0x0,0x4,
    0x0,0x0,0x0,0x0,0x0,0x0,0x8,0x0,0x4,0x0,0x0,0x0,0x0,0x0,0x0,0x7,
    0xFF,0xF8,0x0,0x0,0x0,0x80,0x0,0x8,0x0,0x0,0x8,0x0,0x0,0x1,0xC0,0x0,
    0x1C,0x0,0x0,0x1C,0x0,0x0,0x3,0xE0,0x0,0x3E,0x0,0x0,0x3E,0x0,0x0,0x1,
    0xC0,0x0,0x1C,0x0,0x0,0x1C,0x0,0x0,0x5,0xC0,0x0,0x1C,0x7,0xFC,0x1C,0x0,
    0xF,0xFD,0xC1,0xFF,0xDC,0x0,0xC0,0x1C,0x0,0xF,0xFC,0x7,0xFF,0x0,0x1F,0xFF,
    0xE0,0x0,0x1F,0xE0,0x7,0xFF,0x0,0x7F,0xFF,0x0,0x0,0x19,0xC0,0x7,0x0,0x0,
    0x7F,0xE0,0x0,0x0,0x19,0xC0,0x6,0x0,0x0,0x78,0x0,0x0,0x0,0x10,0x60,0x6,
    0x0,0x0,0x60,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x80,0x0,0x8,0x0,0x0,0x8,0x0,0x0,0x1,0xC0,0x0,0x1C,0x0,0x0,0x1C,
    0x0,0xF,0xC3,0xE1,0xF8,0x3E,0x3,0xF0,0x3E,0x0,0xA,0x21,0xC1,0x44,0x1C,0x2,
    0x88,0x1C,0x0,0xF,0xC1,0xC1,0xF8,0x1C,0x3,0xF0,0x1C,0x0,0x0,0x1,0xC0,0x0,
    0x1C,0x0,0x0,0x1C,0x0,0x1,0xF8,0x0,0x3F,0x0,0x0,0x7E,0x0,0x0,0x1,0x44,
    0x0,0x28,0x80,0x0,0x51,0x0,0x0,0x1,0xF8,0x0,0x3F,0x0,0x0,0x7E,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x1,0x40,0x0,0x8,0x0,0x40,0x8,0x0,0x3,0x13,0xE0,0x0,0x1C,
    0x1,0xB0,0x1C,0x0,0x3,0xBB,0xE0,0x10,0x3E,0x0,0xE0,0x3E,0x0,0x7,0x1D,0xC0,
    0x28,0x1C,0x8,0xA2,0x1C,0x0,0xE,0xAE,0x80,0x28,0x1C,0x36,0xD,0x9C,0x0,0xD,
    0x54,0x0,0x44,0x1C,0x1C,0x7,0x1C,0x0,0x4,0xA0,0x0,0x44,0x0,0x14,0x45,0x0,
    0x0,0x1,0x50,0x0,0x44,0x0,0x1,0xB0,0x0,0x0,0x2,0xA8,0x0,0x38,0x0,0x0,
    0xE0,0x0,0x0,0x1,0x10,0x0,0x0,0x0,0x0,0xA0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x3,0x84,0xBA,0xB8,0x77,0x31,0xDC,0x10,0x0,0x4,0x44,
    0xA2,0x90,0x82,0x4A,0x10,0x8,0x0,0x5,0xC6,0xB9,0x10,0x62,0x7A,0xDC,0xFC,0x0,
    0x4,0x45,0xA2,0x90,0x12,0x4A,0x50,0x8,0x0,0x3,0x84,0xBA,0x90,0xE2,0x49,0x9C,
    0x10,0x0,
};

int cdefFenceBitmaps[2][18]=
{
    { 8,16,0x70,0x88,0x98,0xE8,0x88,0x88,0x88,0x88,0x9C,0xA2,0xA6,0xBA,0xA2,0x62,0x22,0x22 },
    { 8,16,0x72,0x8A,0x9A,0xEA,0x8A,0x8C,0x88,0x88,0x9C,0xA2,0xA6,0xBA,0xA2,0x62,0x22,0x22 },
};

int cdefMonsterBitmaps[2][20]=
{
    { 16,9,0x1E,0x0,0x21,0x0,0x40,0x80,0x40,0x80,0x40,0x80,0x40,0xC0,0x80,0x40,0x70,0x80,0xF,0x0 },
    { 16,9,0x0,0x0,0x1E,0x0,0x21,0x0,0x40,0x80,0x40,0x80,0x40,0x80,0x80,0x40,0x47,0x80,0x38,0x0 },
};

int cdefKingBitmaps[2][40]=
{
    { 16,19,0x21,0x84,0x32,0x4C,0x2C,0x34,0x21,0x84,0x2A,0x54,0x21,0x84,0x1F,0xF8,0x20,0x4,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x1,0x80,0x1,0x80,0x1,0x7E,0x2,0x1,0xFC },
    { 16,19,0x0,0x0,0x21,0x84,0x32,0x4C,0x2C,0x34,0x21,0x84,0x2A,0x54,0x21,0x84,0x1F,0xF8,0x20,0x4,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x2,0x80,0x1,0x80,0x1,0x80,0x6,0x40,0x78,0x3F,0x80 },
};

int cdefKingSkilBitmaps[2][40]=
{
    { 16,19,0x0,0x0,0x21,0x84,0x32,0x4C,0x2C,0x34,0x21,0x84,0x2A,0x54,0x21,0x84,0x1F,0xF8,0x20,0x4,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x2,0x40,0x2,0x80,0x1,0x80,0x1,0x80,0x1,0x7F,0xFE,0x0,0x0 },
    { 16,19,0x0,0x0,0x0,0x0,0x0,0x0,0x21,0x84,0x32,0x4C,0x2C,0x34,0x21,0x84,0x2A,0x54,0x21,0x84,0x1F,0xF8,0x20,0x4,0x40,0x2,0x40,0x2,0x40,0x2,0x80,0x1,0x80,0x1,0x83,0xC1,0x7C,0x3E,0x0,0x0 },
};

// 3rd real entry is genuinely `{}` upstream (all-zero, a 0x0 no-op bitmap
// real hardware really draws every 3rd stun-animation tick) - see this
// file's own header comment.
int cdefMotionStunBitmaps[3][38]=
{
    { 24,12,0x0,0x40,0x0,0x1,0xB0,0x0,0x0,0xE0,0x0,0x0,0xA0,0x0,0x20,0x0,0x80,0xD8,0x3,0x60,0x70,0x1,0xC0,0x50,0x1,0x40,0x0,0x40,0x0,0x1,0xB0,0x0,0x0,0xE0,0x0,0x0,0xA0,0x0 },
    { 24,12,0x0,0x0,0x0,0x4,0x4,0x0,0x1B,0x1B,0x0,0xE,0xE,0x0,0xA,0xA,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x4,0x4,0x0,0x1B,0x1B,0x0,0xE,0xE,0x0,0xA,0xA,0x0,0x0,0x0,0x0 },
    { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
};

// Real Arduino `B00000000`-style binary literals converted to `0x` hex
// (byte-count-verified against each real {width,height,...} header).
int cdefRifleBitmap[20]= {
    9,9,
    0x8,0x0,
    0x8,0x0,
    0x8,0x0,
    0x0,0x0,
    0xeb,0x80,
    0x0,0x0,
    0x8,0x0,
    0x8,0x0,
    0x8,0x0,
};

int cdefShotgunBitmap[11]= {
    8,9,
    0x0,
    0x0,
    0x0,
    0x8,
    0x1c,
    0x8,
    0x0,
    0x0,
    0x0,
};

int cdefSniperRifleBitmap[248]= {
    48,41,
    0x0,0x0,0xff,0x80,0x0,0x0,
    0x0,0x7,0x1c,0x70,0x0,0x0,
    0x0,0x18,0x1c,0xc,0x0,0x0,
    0x0,0x60,0x1c,0x3,0x0,0x0,
    0x0,0x80,0x1c,0x0,0x80,0x0,
    0x1,0x0,0x8,0x0,0x40,0x0,
    0x2,0x0,0x8,0x0,0x20,0x0,
    0x4,0x0,0x8,0x0,0x10,0x0,
    0x8,0x0,0x8,0x0,0x8,0x0,
    0x10,0x0,0x8,0x0,0x4,0x0,
    0x10,0x0,0x8,0x0,0x4,0x0,
    0x20,0x0,0x8,0x0,0x2,0x0,
    0x20,0x0,0x8,0x0,0x2,0x0,
    0x40,0x0,0x8,0x0,0x1,0x0,
    0x40,0x0,0x8,0x0,0x1,0x0,
    0x40,0x0,0x8,0x0,0x1,0x0,
    0x80,0x0,0x8,0x0,0x0,0x80,
    0x80,0x0,0x8,0x0,0x0,0x80,
    0x80,0x0,0x8,0x0,0x0,0x80,
    0xf8,0x0,0x0,0x0,0xf,0x80,
    0xff,0xff,0xeb,0xff,0xff,0x80,
    0xf8,0x0,0x0,0x0,0xf,0x80,
    0x80,0x0,0x8,0x0,0x0,0x80,
    0x80,0x0,0x8,0x0,0x0,0x80,
    0x80,0x0,0x8,0x0,0x0,0x80,
    0x40,0x0,0x8,0x0,0x1,0x0,
    0x40,0x0,0x8,0x0,0x1,0x0,
    0x40,0x0,0x8,0x0,0x1,0x0,
    0x20,0x0,0x8,0x0,0x2,0x0,
    0x20,0x0,0x8,0x0,0x2,0x0,
    0x10,0x0,0x8,0x0,0x4,0x0,
    0x10,0x0,0x8,0x0,0x4,0x0,
    0x8,0x0,0x8,0x0,0x8,0x0,
    0x4,0x0,0x8,0x0,0x10,0x0,
    0x2,0x0,0x8,0x0,0x20,0x0,
    0x1,0x0,0x8,0x0,0x40,0x0,
    0x0,0x80,0x1c,0x0,0x80,0x0,
    0x0,0x60,0x1c,0x3,0x0,0x0,
    0x0,0x18,0x1c,0xc,0x0,0x0,
    0x0,0x7,0x1c,0x70,0x0,0x0,
    0x0,0x0,0xff,0x80,0x0,0x0,
};

int cdefRifleImgBitmap[7]= {
    8,5,
    0x2,
    0x7f,
    0xfe,
    0xcc,
    0x86,
};

int cdefShotgunImgBitmap[6]= {
    8,4,
    0x7f,
    0xfe,
    0xc0,
    0x80,
};

int cdefSniperImgBitmap[8]= {
    8,6,
    0x3e,
    0x8,
    0x7f,
    0xfe,
    0xc0,
    0x80,
};

int cdefMotionSlowBitmap[9]= {
    8,7,
    0x20,
    0x50,
    0x50,
    0x88,
    0x88,
    0x88,
    0x70,
};

// -----------------------------------------------------------------------------
// State machine / structs / globals
// -----------------------------------------------------------------------------

enum CdefState
{
    CDEF_STATE_TITLE = 0,
    CDEF_STATE_WEAPON_SELECT = 1,
    CDEF_STATE_HIGHSCORE = 2,
    CDEF_STATE_READY = 3,
    CDEF_STATE_PLAY = 4,
    CDEF_STATE_SHOP_MESSAGE = 5,
    CDEF_STATE_GAMEOVER = 6
};

#define CDEF_RANKMAX 5

typedef struct
{
    int power;
    int shotDelayTime;
    int ammo;
    int ammoMax;
    bool shotState;   // true = has ammo/can shoot, false = empty/reloading
    bool shotDelay;   // true = ready, false = mid burst/reload-delay
    bool reloadShow;  // blink flag for the low-ammo ammo readout
} CdefGun;

typedef struct
{
    int monX;
    int monY;
    int monHp;
    int monSpeed;
    int monMovement;
    int monSpeedMax;
    int slowTime;
    int stunTime;
    bool monMove;    // animation frame toggle
    bool monState;   // true = alive
    bool slowState;  // shotgun special ability active
    bool stunState;  // sniper special ability active
} CdefMonster;

int cdefState;

// system / camera
int cdefWorldY;
int cdefCameraY;
int cdefShakeX;
int cdefShakeY;
bool cdefShakeState;
int cdefShakeTimeLeft;
int cdefShakeMagnitude;
int cdefReadyCnt;

// weapon-select screen
int cdefMainSub;
int cdefWeaponPoint;
bool cdefWeaponChange;

// level-up banner
int cdefLevelUpX;
int cdefLevel;
bool cdefLeveling;

// shop
bool cdefShopState;
int cdefScroll;
int cdefShopMsgWeapon;
int cdefShopMsgType;

// highscores (names dropped - no shim keyboard, see header comment)
int cdefHighscores[5];
int cdefHighscoreReturn;

// player
int cdefPlayerX;
int cdefPlayerY;
int cdefWeapon;
int cdefMainWeapon;
int cdefSubWeapon;
int cdefMeHp;
int cdefSlowUp;
int cdefStunUp;
int cdefKill;
int cdefCoin;
int cdefReloadingDelay;
bool cdefReloading;

CdefGun cdefGuns[3];

// monsters / waves
int cdefMonRandomMax;
int cdefMonHpUp;
int cdefMonSpeedUp;
int cdefMonMax;
int cdefDefaultSpeed;
bool cdefBoss;
bool cdefBossing;
bool cdefBossDie;
int cdefBossMessage;
int cdefMonDropCnt; // upstream's own `monster_drop` counter - distinct from the cdefMonsterDrop() function below
bool cdefDroping;
int cdefKingDefaultHp;
int cdefMonsterDefaultHp;

CdefMonster cdefMon[5];

int cdefSlowmotionTime[5];
int cdefStunmotionTime[5];

// -----------------------------------------------------------------------------
// Small print helpers - see this file's own header comment (cursor-cascade
// note) for why these exist instead of a real println().
// -----------------------------------------------------------------------------

void cdefPrintln( char* text )
{
    gbPrintString( text );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

void cdefPrintlnNum( int value )
{
    gbPrintNumber( value );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

// Draws one real low-ASCII icon glyph at the current cursor, then advances
// gbCursorX exactly like gbPrintString() does per character - see this
// file's own header comment on why icons are drawn this way rather than
// embedded as raw bytes inside a string literal.
void cdefDrawIcon( int code )
{
    gbDrawChar( code, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
}

// -----------------------------------------------------------------------------
// EEPROM highscores (real upstream EEPROM usage - see header comment)
// -----------------------------------------------------------------------------

void cdefLoadHighscore()
{
    int i;
    for( i = 0; i < CDEF_RANKMAX; i = i + 1 )
    {
        cdefHighscores[ i ] = eeprom_read_word( i * 2 );
        if( cdefHighscores[ i ] == 0xFFFF ) cdefHighscores[ i ] = 0;
    }
}

void cdefSaveHighscore()
{
    int i;
    cdefHighscores[ CDEF_RANKMAX - 1 ] = cdefCoin;
    for( i = CDEF_RANKMAX - 1; i > 0; i = i - 1 )
    {
        if( cdefHighscores[ i - 1 ] < cdefHighscores[ i ] )
        {
            int tmp = cdefHighscores[ i - 1 ];
            cdefHighscores[ i - 1 ] = cdefHighscores[ i ];
            cdefHighscores[ i ] = tmp;
        }
        else break;
    }
    for( i = 0; i < CDEF_RANKMAX; i = i + 1 )
      eeprom_write_word( i * 2, cdefHighscores[ i ] );
}

// -----------------------------------------------------------------------------
// Monster helpers (monster.ino)
// -----------------------------------------------------------------------------

void cdefMonsterDie( int j )
{
    cdefMon[ j ].monState = false;
    if( cdefBoss && j == 2 ) cdefBossDie = true;
    cdefMon[ j ].slowState = false;
    cdefMon[ j ].stunState = false;
    cdefMon[ j ].monSpeed = cdefDefaultSpeed;
    cdefMon[ j ].slowTime = 0;
    cdefMon[ j ].stunTime = 0;
    cdefMon[ j ].monY = 87;
    cdefMonMax = cdefMonMax - 1;
    cdefKill = cdefKill + 1;
}

void cdefAllKill()
{
    if( cdefBossDie && cdefMonMax <= 0 )
    {
        cdefBoss = false;
        cdefShopState = true;
        cdefWeapon = 0;
        cdefBossDie = false;
    }
}

void cdefMonsterShake()
{
    gbPlayCancel();
    cdefShakeTimeLeft = 2;
    cdefShakeMagnitude = 1;
    cdefShakeState = true;
}

void cdefMonSetting( int monNum )
{
    if( cdefMon[ monNum ].monState == false )
    {
        if( monNum == 0 ) cdefMon[ monNum ].monX = 15;
        else if( monNum == 1 ) cdefMon[ monNum ].monX = 26;
        else if( monNum == 2 ) { if( cdefBoss ) cdefMon[ monNum ].monX = 34; else cdefMon[ monNum ].monX = 37; }
        else if( monNum == 3 ) cdefMon[ monNum ].monX = 48;
        else if( monNum == 4 ) cdefMon[ monNum ].monX = 59;

        cdefMon[ monNum ].monY = 96;
        if( cdefBoss ) cdefMon[ monNum ].monHp = 30 * cdefMonHpUp; else cdefMon[ monNum ].monHp = 10 + cdefMonHpUp;
        cdefMon[ monNum ].monSpeed = 3 + cdefMonSpeedUp;
        if( cdefBoss ) cdefMon[ monNum ].monSpeedMax = 18; else cdefMon[ monNum ].monSpeedMax = 12;
        cdefMon[ monNum ].monState = true;
        cdefMonMax = cdefMonMax + 1;
    }
}

void cdefMonsterDraw( int z )
{
    int idx;
    if( cdefMon[ z ].monMove ) idx = 1; else idx = 0;
    gbDrawBitmap( cdefMon[ z ].monX, cdefMon[ z ].monY + cdefCameraY, cdefMonsterBitmaps[ idx ] );
    cdefMon[ z ].monMovement = cdefMon[ z ].monMovement + cdefMon[ z ].monSpeed;
    if( cdefMon[ z ].monMovement > cdefMon[ z ].monSpeedMax )
    {
        cdefMon[ z ].monY = cdefMon[ z ].monY - 1;
        cdefMon[ z ].monMovement = 0;
        if( cdefMon[ z ].monMove ) cdefMon[ z ].monMove = false; else cdefMon[ z ].monMove = true;
    }
}

void cdefMonsterSlow( int k )
{
    int cur = cdefMon[ k ].slowTime;
    cdefMon[ k ].slowTime = cur + 1;
    if( cdefBoss && k == 2 )
    {
        if( cur >= ( 70 + cdefSlowUp ) / 2 )
        {
            if( !cdefMon[ k ].stunState ) cdefMon[ k ].monSpeed = cdefDefaultSpeed;
            cdefMon[ k ].slowTime = 0;
            cdefMon[ k ].slowState = false;
        }
    }
    else if( cur >= 70 + cdefSlowUp )
    {
        if( !cdefMon[ k ].stunState ) cdefMon[ k ].monSpeed = cdefDefaultSpeed;
        cdefMon[ k ].slowTime = 0;
        cdefMon[ k ].slowState = false;
    }
}

void cdefMonsterStun( int g )
{
    int cur = cdefMon[ g ].stunTime;
    cdefMon[ g ].stunTime = cur + 1;
    if( cdefBoss && g == 2 )
    {
        if( cur >= ( 50 + cdefStunUp ) / 2 )
        {
            if( !cdefMon[ g ].slowState ) cdefMon[ g ].monSpeed = cdefDefaultSpeed;
            cdefMon[ g ].stunTime = 0;
            cdefMon[ g ].stunState = false;
        }
    }
    else if( cur >= 50 + cdefStunUp )
    {
        if( !cdefMon[ g ].slowState ) cdefMon[ g ].monSpeed = cdefDefaultSpeed;
        cdefMon[ g ].stunTime = 0;
        cdefMon[ g ].stunState = false;
    }
}

void cdefMotionSlow( int a )
{
    if( cdefBoss && a == 2 ) gbDrawBitmap( cdefMon[ a ].monX + 11, cdefMon[ a ].monY + cdefCameraY + cdefSlowmotionTime[ a ], cdefMotionSlowBitmap );
    else gbDrawBitmap( cdefMon[ a ].monX + 5, cdefMon[ a ].monY + cdefCameraY + cdefSlowmotionTime[ a ], cdefMotionSlowBitmap );
    if( cdefSlowmotionTime[ a ] <= 3 ) cdefSlowmotionTime[ a ] = cdefSlowmotionTime[ a ] + 1;
    else cdefSlowmotionTime[ a ] = 0;
}

void cdefMotionStun( int b )
{
    if( cdefBoss && b == 2 ) gbDrawBitmap( cdefMon[ b ].monX - 1, cdefMon[ b ].monY + cdefCameraY - 4, cdefMotionStunBitmaps[ cdefStunmotionTime[ b ] ] );
    else gbDrawBitmap( cdefMon[ b ].monX - 4, cdefMon[ b ].monY + cdefCameraY - 7, cdefMotionStunBitmaps[ cdefStunmotionTime[ b ] ] );
    if( cdefStunmotionTime[ b ] <= 1 ) cdefStunmotionTime[ b ] = cdefStunmotionTime[ b ] + 1;
    else cdefStunmotionTime[ b ] = 0;
}

void cdefDisplayMonHp( int u )
{
    gbDrawRect( cdefMon[ u ].monX, cdefMon[ u ].monY - 4 + cdefCameraY, 10, 3 );
    if( (float)cdefMonsterDefaultHp * 0.9 <= (float)cdefMon[ u ].monHp ) gbFillRect( cdefMon[ u ].monX + 1, cdefMon[ u ].monY - 3 + cdefCameraY, 8, 1 );
    else if( (float)cdefMonsterDefaultHp * 0.8 <= (float)cdefMon[ u ].monHp ) gbFillRect( cdefMon[ u ].monX + 1, cdefMon[ u ].monY - 3 + cdefCameraY, 6, 1 );
    else if( (float)cdefMonsterDefaultHp * 0.5 <= (float)cdefMon[ u ].monHp ) gbFillRect( cdefMon[ u ].monX + 1, cdefMon[ u ].monY - 3 + cdefCameraY, 4, 1 );
    else if( (float)cdefMonsterDefaultHp * 0.2 <= (float)cdefMon[ u ].monHp ) gbFillRect( cdefMon[ u ].monX + 1, cdefMon[ u ].monY - 3 + cdefCameraY, 2, 1 );
    else gbFillRect( cdefMon[ u ].monX + 1, cdefMon[ u ].monY - 3 + cdefCameraY, 0, 1 );
}

void cdefMonsterDrop()
{
    if( cdefDroping )
    {
        if( cdefMon[ 2 ].monY == 72 )
        {
            if( cdefMon[ 0 ].monState == false )
            {
                cdefMon[ 0 ].monX = 15;
                cdefMon[ 0 ].monY = cdefMon[ 2 ].monY + 7;
                cdefMon[ 0 ].monHp = 10 + cdefMonHpUp;
                cdefMon[ 0 ].monSpeed = 3 + cdefMonSpeedUp;
                cdefMon[ 0 ].monSpeedMax = 12;
            }
            if( cdefMon[ 4 ].monState == false )
            {
                cdefMon[ 4 ].monX = 59;
                cdefMon[ 4 ].monY = cdefMon[ 2 ].monY + 7;
                cdefMon[ 4 ].monHp = 10 + cdefMonHpUp;
                cdefMon[ 4 ].monSpeed = 3 + cdefMonSpeedUp;
                cdefMon[ 4 ].monSpeedMax = 12;
            }
            cdefMon[ 0 ].monState = true;
            cdefMon[ 4 ].monState = true;
            cdefMonMax = cdefMonMax + 2;
            cdefDroping = false;
        }
        if( cdefMon[ 2 ].monY == 47 )
        {
            if( cdefMon[ 1 ].monState == false )
            {
                cdefMon[ 1 ].monX = 23;
                cdefMon[ 1 ].monY = cdefMon[ 2 ].monY + 7;
                cdefMon[ 1 ].monHp = 10 + cdefMonHpUp;
                cdefMon[ 1 ].monSpeed = 3 + cdefMonSpeedUp;
                cdefMon[ 1 ].monSpeedMax = 12;
            }
            if( cdefMon[ 3 ].monState == false )
            {
                cdefMon[ 3 ].monX = 51;
                cdefMon[ 3 ].monY = cdefMon[ 2 ].monY + 7;
                cdefMon[ 3 ].monHp = 10 + cdefMonHpUp;
                cdefMon[ 3 ].monSpeed = 3 + cdefMonSpeedUp;
                cdefMon[ 3 ].monSpeedMax = 12;
            }
            cdefMon[ 1 ].monState = true;
            cdefMon[ 3 ].monState = true;
            cdefMonMax = cdefMonMax + 2;
            cdefDroping = false;
        }
    }
}

void cdefDisplayMonster()
{
    int monNum;

    if( cdefBoss )
    {
        if( cdefMon[ 2 ].monState == false && !cdefBossing && !cdefBossDie )
        {
            monNum = 2;
            cdefMonSetting( monNum );
        }
    }
    else if( cdefMonMax <= 5 && !cdefBoss && !cdefBossing )
    {
        int monMake = 1 + arand( cdefMonRandomMax - 1 );
        if( monMake == cdefMonRandomMax / 2 - 20 ) { monNum = 0; cdefMonSetting( monNum ); }
        else if( monMake == cdefMonRandomMax / 2 - 10 ) { monNum = 1; cdefMonSetting( monNum ); }
        else if( monMake == cdefMonRandomMax / 2 ) { monNum = 2; cdefMonSetting( monNum ); }
        else if( monMake == cdefMonRandomMax / 2 + 10 ) { monNum = 3; cdefMonSetting( monNum ); }
        else if( monMake == cdefMonRandomMax / 2 + 20 ) { monNum = 4; cdefMonSetting( monNum ); }
    }

    if( cdefMon[ 0 ].monState ) cdefMonsterDraw( 0 );
    if( cdefMon[ 1 ].monState ) cdefMonsterDraw( 1 );
    if( cdefMon[ 2 ].monState )
    {
        if( cdefBoss && !cdefBossing )
        {
            if( cdefMon[ 2 ].monY == 72 || cdefMon[ 2 ].monY == 47 )
            {
                int idx;
                if( cdefMon[ 2 ].monMove ) idx = 1; else idx = 0;
                gbDrawBitmap( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY + cdefCameraY, cdefKingSkilBitmaps[ idx ] );
                if( !cdefMon[ 2 ].stunState )
                {
                    int curDrop = cdefMonDropCnt;
                    cdefMonDropCnt = cdefMonDropCnt + 1;
                    if( curDrop == 70 ) { cdefMonDropCnt = 0; cdefMon[ 2 ].monY = cdefMon[ 2 ].monY - 1; }
                    else if( curDrop == 30 ) cdefDroping = true;
                }
                if( cdefMon[ 2 ].monMove ) cdefMon[ 2 ].monMove = false; else cdefMon[ 2 ].monMove = true;
            }
            else
            {
                int idx2;
                if( cdefMon[ 2 ].monMove ) idx2 = 1; else idx2 = 0;
                gbDrawBitmap( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY + cdefCameraY, cdefKingBitmaps[ idx2 ] );
                cdefMon[ 2 ].monMovement = cdefMon[ 2 ].monMovement + cdefMon[ 2 ].monSpeed;
                if( cdefMon[ 2 ].monMovement > cdefMon[ 2 ].monSpeedMax )
                {
                    cdefMon[ 2 ].monY = cdefMon[ 2 ].monY - 1;
                    cdefMon[ 2 ].monMovement = 0;
                    if( cdefMon[ 2 ].monMove ) cdefMon[ 2 ].monMove = false; else cdefMon[ 2 ].monMove = true;
                }
            }
        }
        else cdefMonsterDraw( 2 );
    }
    if( cdefMon[ 3 ].monState ) cdefMonsterDraw( 3 );
    if( cdefMon[ 4 ].monState ) cdefMonsterDraw( 4 );

    // monster reaches the castle wall (row 15)
    if( cdefMon[ 0 ].monY == 15 ) { cdefMonsterDie( 0 ); cdefMeHp = cdefMeHp - 1; cdefMonsterShake(); }
    if( cdefMon[ 1 ].monY == 15 ) { cdefMonsterDie( 1 ); cdefMeHp = cdefMeHp - 1; cdefMonsterShake(); }
    if( cdefMon[ 2 ].monY == 15 ) { cdefMonsterDie( 2 ); if( cdefBoss ) cdefMeHp = cdefMeHp - 2; else cdefMeHp = cdefMeHp - 1; cdefMonsterShake(); }
    if( cdefMon[ 3 ].monY == 15 ) { cdefMonsterDie( 3 ); cdefMeHp = cdefMeHp - 1; cdefMonsterShake(); }
    if( cdefMon[ 4 ].monY == 15 ) { cdefMonsterDie( 4 ); cdefMeHp = cdefMeHp - 1; cdefMonsterShake(); }

    // abnormal-state timers
    if( cdefMon[ 0 ].slowState ) cdefMonsterSlow( 0 );
    if( cdefMon[ 1 ].slowState ) cdefMonsterSlow( 1 );
    if( cdefMon[ 2 ].slowState ) cdefMonsterSlow( 2 );
    if( cdefMon[ 3 ].slowState ) cdefMonsterSlow( 3 );
    if( cdefMon[ 4 ].slowState ) cdefMonsterSlow( 4 );

    if( cdefMon[ 0 ].stunState ) cdefMonsterStun( 0 );
    if( cdefMon[ 1 ].stunState ) cdefMonsterStun( 1 );
    if( cdefMon[ 2 ].stunState ) cdefMonsterStun( 2 );
    if( cdefMon[ 3 ].stunState ) cdefMonsterStun( 3 );
    if( cdefMon[ 4 ].stunState ) cdefMonsterStun( 4 );

    // slow-motion FX overlay
    if( cdefMon[ 0 ].slowState && cdefMon[ 0 ].monState ) cdefMotionSlow( 0 ); else cdefSlowmotionTime[ 0 ] = 0;
    if( cdefMon[ 1 ].slowState && cdefMon[ 1 ].monState ) cdefMotionSlow( 1 ); else cdefSlowmotionTime[ 1 ] = 0;
    if( cdefMon[ 2 ].slowState && cdefMon[ 2 ].monState ) cdefMotionSlow( 2 ); else cdefSlowmotionTime[ 2 ] = 0;
    if( cdefMon[ 3 ].slowState && cdefMon[ 3 ].monState ) cdefMotionSlow( 3 ); else cdefSlowmotionTime[ 3 ] = 0;
    if( cdefMon[ 4 ].slowState && cdefMon[ 4 ].monState ) cdefMotionSlow( 4 ); else cdefSlowmotionTime[ 4 ] = 0;

    // stun-motion FX overlay
    if( cdefMon[ 0 ].stunState && cdefMon[ 0 ].monState ) cdefMotionStun( 0 ); else cdefStunmotionTime[ 0 ] = 0;
    if( cdefMon[ 1 ].stunState && cdefMon[ 1 ].monState ) cdefMotionStun( 1 ); else cdefStunmotionTime[ 1 ] = 0;
    if( cdefMon[ 2 ].stunState && cdefMon[ 2 ].monState ) cdefMotionStun( 2 ); else cdefStunmotionTime[ 2 ] = 0;
    if( cdefMon[ 3 ].stunState && cdefMon[ 3 ].monState ) cdefMotionStun( 3 ); else cdefStunmotionTime[ 3 ] = 0;
    if( cdefMon[ 4 ].stunState && cdefMon[ 4 ].monState ) cdefMotionStun( 4 ); else cdefStunmotionTime[ 4 ] = 0;

    // HP bars
    if( cdefMon[ 0 ].monState ) cdefDisplayMonHp( 0 );
    if( cdefMon[ 1 ].monState ) cdefDisplayMonHp( 1 );
    if( cdefBoss && cdefMon[ 2 ].monState )
    {
        gbDrawRect( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY - 5 + cdefCameraY, 16, 3 );
        if( (float)cdefKingDefaultHp * 0.9 <= (float)cdefMon[ 2 ].monHp ) gbFillRect( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY - 4 + cdefCameraY, 16, 1 );
        else if( (float)cdefKingDefaultHp * 0.8 <= (float)cdefMon[ 2 ].monHp ) gbFillRect( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY - 4 + cdefCameraY, 13, 1 );
        else if( (float)cdefKingDefaultHp * 0.6 <= (float)cdefMon[ 2 ].monHp ) gbFillRect( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY - 4 + cdefCameraY, 10, 1 );
        else if( (float)cdefKingDefaultHp * 0.4 <= (float)cdefMon[ 2 ].monHp ) gbFillRect( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY - 4 + cdefCameraY, 7, 1 );
        else if( (float)cdefKingDefaultHp * 0.2 <= (float)cdefMon[ 2 ].monHp ) gbFillRect( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY - 4 + cdefCameraY, 4, 1 );
        else gbFillRect( cdefMon[ 2 ].monX, cdefMon[ 2 ].monY - 4 + cdefCameraY, 1, 1 );
    }
    else if( cdefMon[ 2 ].monState ) cdefDisplayMonHp( 2 );
    if( cdefMon[ 3 ].monState ) cdefDisplayMonHp( 3 );
    if( cdefMon[ 4 ].monState ) cdefDisplayMonHp( 4 );
}

// -----------------------------------------------------------------------------
// Attack (attack.ino)
// -----------------------------------------------------------------------------

void cdefAttacking( int m )
{
    cdefMon[ m ].monHp = cdefMon[ m ].monHp - cdefGuns[ cdefWeapon ].power;
    if( cdefWeapon == 1 && ( cdefMon[ m ].monHp + cdefMonHpUp ) >= 1 ) { cdefMon[ m ].slowTime = 0; cdefMon[ m ].monSpeed = 1; cdefMon[ m ].slowState = true; }
    if( cdefWeapon == 2 && ( cdefMon[ m ].monHp + cdefMonHpUp ) >= 1 ) { cdefMon[ m ].stunTime = 0; cdefMon[ m ].monSpeed = 0; cdefMon[ m ].stunState = true; }
    if( cdefMon[ m ].monHp <= 0 )
    {
        cdefMonsterDie( m );
        if( cdefBoss ) cdefCoin = cdefCoin + 10; else cdefCoin = cdefCoin + 2;
    }
}

void cdefAttack()
{
    if( cdefGuns[ cdefWeapon ].shotState && cdefWeapon == 0 )
    {
        gbPlayOK();
        cdefGuns[ cdefWeapon ].ammo = cdefGuns[ cdefWeapon ].ammo - 1;
        if( cdefGuns[ cdefWeapon ].ammo == 0 ) cdefGuns[ cdefWeapon ].shotState = false;

        if( cdefBoss )
        {
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 2 ].monX, cdefMon[ 2 ].monY + cdefCameraY, 18, 16 ) && cdefMon[ 2 ].monState ) cdefAttacking( 2 );
        }
        else if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 2 ].monX, cdefMon[ 2 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 2 ].monState ) cdefAttacking( 2 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 0 ].monX, cdefMon[ 0 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 0 ].monState ) cdefAttacking( 0 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 1 ].monX, cdefMon[ 1 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 1 ].monState ) cdefAttacking( 1 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 3 ].monX, cdefMon[ 3 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 3 ].monState ) cdefAttacking( 3 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 4 ].monX, cdefMon[ 4 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 4 ].monState ) cdefAttacking( 4 );
    }
    else if( cdefGuns[ cdefWeapon ].shotState && cdefGuns[ cdefWeapon ].shotDelay )
    {
        if( cdefWeapon == 2 ) gbDrawBitmap( cdefPlayerX - 32, cdefPlayerY - 20, cdefShotBitmap );
        cdefShakeTimeLeft = 2;
        if( cdefWeapon == 1 ) cdefShakeMagnitude = 2; else cdefShakeMagnitude = 4;
        cdefShakeState = true;
        gbPlayOK();
        cdefGuns[ cdefWeapon ].ammo = cdefGuns[ cdefWeapon ].ammo - 1;
        if( cdefGuns[ cdefWeapon ].ammo == 0 ) cdefGuns[ cdefWeapon ].shotState = false;

        if( cdefBoss )
        {
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 2 ].monX, cdefMon[ 2 ].monY + cdefCameraY, 18, 16 ) && cdefMon[ 2 ].monState ) cdefAttacking( 2 );
        }
        else if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 2 ].monX, cdefMon[ 2 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 2 ].monState ) cdefAttacking( 2 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 0 ].monX, cdefMon[ 0 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 0 ].monState ) cdefAttacking( 0 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 1 ].monX, cdefMon[ 1 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 1 ].monState ) cdefAttacking( 1 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 3 ].monX, cdefMon[ 3 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 3 ].monState ) cdefAttacking( 3 );
        if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, cdefMon[ 4 ].monX, cdefMon[ 4 ].monY + cdefCameraY, 10, 9 ) && cdefMon[ 4 ].monState ) cdefAttacking( 4 );

        cdefGuns[ cdefWeapon ].shotDelay = false;
    }
}

// -----------------------------------------------------------------------------
// Background / shake (Castle.ino)
// -----------------------------------------------------------------------------

void cdefDisplayBackground()
{
    gbDrawFastVLine( 8 + cdefShakeX, 0 + cdefShakeY, 47 );
    gbDrawFastVLine( 75 + cdefShakeX, 0 + cdefShakeY, 47 );

    gbDrawBitmap( 8 + cdefShakeX, 0 + cdefCameraY + cdefShakeY, cdefCastleBitmap );

    gbDrawBitmap( 8 + cdefShakeX, 16 + cdefCameraY + cdefShakeY, cdefFenceBitmaps[ 0 ] );
    gbDrawBitmap( 8 + cdefShakeX, 32 + cdefCameraY + cdefShakeY, cdefFenceBitmaps[ 1 ] );
    gbDrawBitmap( 69 + cdefShakeX, 16 + cdefCameraY + cdefShakeY, cdefFenceBitmaps[ 0 ] );
    gbDrawBitmap( 69 + cdefShakeX, 32 + cdefCameraY + cdefShakeY, cdefFenceBitmaps[ 1 ] );
    gbDrawBitmap( 69 + cdefShakeX, 48 + cdefCameraY + cdefShakeY, cdefFenceBitmaps[ 1 ] );
}

void cdefShakeScreen()
{
    if( cdefShakeState )
    {
        if( cdefShakeTimeLeft > 0 )
        {
            cdefShakeTimeLeft = cdefShakeTimeLeft - 1;
            cdefShakeX = cdefShakeX + ( -cdefShakeMagnitude + arand( 2 * cdefShakeMagnitude + 1 ) );
            cdefShakeY = cdefShakeY + ( -cdefShakeMagnitude + arand( 2 * cdefShakeMagnitude + 1 ) );
            cdefDisplayBackground();
        }
        else
        {
            cdefShakeX = 0;
            cdefShakeY = 0;
            cdefDisplayBackground();
            cdefShakeState = false;
        }
    }
}

// -----------------------------------------------------------------------------
// HUD (Castle.ino)
// -----------------------------------------------------------------------------

void cdefBeginGameOver()
{
    cdefState = CDEF_STATE_GAMEOVER;
}

void cdefDisplayHp()
{
    int hpShakeX = 0;
    int hpShakeY = 0;
    if( cdefMeHp <= 2 )
    {
        hpShakeX = hpShakeX + ( -1 + arand( 2 ) );
        hpShakeY = hpShakeY + ( -1 + arand( 2 ) );
    }
    gbDrawBitmap( 79 + hpShakeX, 1 + hpShakeY, cdefHpBarBitmap );

    // Fixed here, not preserved - see this file's own header comment (item
    // 1) for the real upstream bug this replaces: no case for exactly 1 HP
    // (the bar segment silently vanished) and no fallback for a negative
    // HP value at all, so a player at 1 HP hit by the boss's 2-damage
    // attack dropped straight to -1, skipping the exact `==0` match
    // GameOver() needed to ever trigger - a real permanent soft-lock (HP
    // stuck negative, game never ending). A case for HP==1 now draws a
    // final 1px sliver, and the game-over check is `<= 0` instead of
    // `== 0` so any negative HP value still ends the game correctly.
    if( cdefMeHp == 7 ) gbFillRect( 80, 2, 2, 23 );
    else if( cdefMeHp == 6 ) gbFillRect( 80, 6, 2, 19 );
    else if( cdefMeHp == 5 ) gbFillRect( 80, 10, 2, 15 );
    else if( cdefMeHp == 4 ) gbFillRect( 80, 14, 2, 11 );
    else if( cdefMeHp == 3 ) gbFillRect( 80 + hpShakeX, 18 + hpShakeY, 2, 7 );
    else if( cdefMeHp == 2 ) gbFillRect( 80 + hpShakeX, 22 + hpShakeY, 2, 3 );
    else if( cdefMeHp == 1 ) gbFillRect( 80 + hpShakeX, 24 + hpShakeY, 2, 1 );
    else if( cdefMeHp <= 0 ) { cdefBeginGameOver(); return; }

    cdefPrintln( "Lv" );
    cdefPrintlnNum( cdefLevel );
    cdefPrintln( "" );
    cdefPrintln( "$" );
    gbPrintNumber( cdefCoin );
}

void cdefDisplayShotBar()
{
    if( !cdefGuns[ cdefWeapon ].shotDelay && cdefWeapon >= 1 )
    {
        if( cdefGuns[ cdefWeapon ].shotDelayTime >= 28 )
        {
            gbFillRect( 0, 43, cdefGuns[ cdefWeapon ].shotDelayTime - 28, 1 );
            gbFillRect( 0, 44, 7, 4 );
        }
        else if( cdefGuns[ cdefWeapon ].shotDelayTime >= 21 )
        {
            gbFillRect( 0, 44, cdefGuns[ cdefWeapon ].shotDelayTime - 21, 1 );
            gbFillRect( 0, 45, 7, 3 );
        }
        else if( cdefGuns[ cdefWeapon ].shotDelayTime >= 14 )
        {
            gbFillRect( 0, 45, cdefGuns[ cdefWeapon ].shotDelayTime - 14, 1 );
            gbFillRect( 0, 46, 7, 2 );
        }
        else if( cdefGuns[ cdefWeapon ].shotDelayTime >= 7 )
        {
            gbFillRect( 0, 46, cdefGuns[ cdefWeapon ].shotDelayTime - 7, 1 );
            gbFillRect( 0, 47, 7, 1 );
        }
        else gbFillRect( 0, 47, cdefGuns[ cdefWeapon ].shotDelayTime, 1 );

        int before = cdefGuns[ cdefWeapon ].shotDelayTime;
        cdefGuns[ cdefWeapon ].shotDelayTime = before - 1;
        if( before <= 0 )
        {
            if( cdefWeapon == 1 ) cdefGuns[ cdefWeapon ].shotDelayTime = 21;
            else if( cdefWeapon == 2 ) cdefGuns[ cdefWeapon ].shotDelayTime = 35;
            cdefGuns[ cdefWeapon ].shotDelay = true;
        }
    }
}

void cdefDisplayAmmo()
{
    if( cdefWeapon == 0 ) gbDrawBitmap( 76, 37, cdefRifleImgBitmap );
    else if( cdefWeapon == 1 ) gbDrawBitmap( 76, 38, cdefShotgunImgBitmap );
    else if( cdefWeapon == 2 ) gbDrawBitmap( 76, 36, cdefSniperImgBitmap );

    gbCursorX = 77;
    gbCursorY = 43;
    if( cdefWeapon == 0 )
    {
        if( cdefGuns[ 0 ].ammo <= ( cdefGuns[ 0 ].ammoMax / 2 + 10 ) )
        {
            if( cdefGuns[ 0 ].reloadShow ) { gbPrintNumber( cdefGuns[ 0 ].ammo ); cdefGuns[ 0 ].reloadShow = false; }
            else cdefGuns[ 0 ].reloadShow = true;
        }
        else gbPrintNumber( cdefGuns[ 0 ].ammo );
    }
    else if( cdefWeapon == 1 )
    {
        if( cdefGuns[ 1 ].ammo <= ( cdefGuns[ 1 ].ammoMax / 2 + 5 ) )
        {
            if( cdefGuns[ 1 ].reloadShow ) { gbPrintNumber( cdefGuns[ 1 ].ammo ); cdefGuns[ 1 ].reloadShow = false; }
            else cdefGuns[ 1 ].reloadShow = true;
        }
        else gbPrintNumber( cdefGuns[ 1 ].ammo );
    }
    else if( cdefWeapon == 2 )
    {
        if( cdefGuns[ 2 ].ammo <= ( cdefGuns[ 2 ].ammoMax / 2 + 2 ) )
        {
            if( cdefGuns[ 2 ].reloadShow ) { gbPrintNumber( cdefGuns[ 2 ].ammo ); cdefGuns[ 2 ].reloadShow = false; }
            else cdefGuns[ 2 ].reloadShow = true;
        }
        else gbPrintNumber( cdefGuns[ 2 ].ammo );
    }

    if( cdefReloading == false )
    {
        gbCursorX = ( LCDWIDTH / 4 ) - 2;
        gbCursorY = ( LCDHEIGHT / 3 ) + 2;
        gbSetFont( gbFont5x7 );
        gbPrintString( "Reload " );
        cdefDrawIcon( 29 ); // real octal \35
        gbSetFont( gbFont3x5 );

        if( cdefWeapon == 0 )
        {
            int before = cdefReloadingDelay;
            cdefReloadingDelay = before + 1;
            if( before >= 1 )
            {
                gbPlayOK();
                int a = cdefGuns[ 0 ].ammo;
                cdefGuns[ 0 ].ammo = a + 1;
                if( a < cdefGuns[ 0 ].ammoMax + 19 ) cdefReloadingDelay = 0;
                else { cdefReloading = true; cdefGuns[ 0 ].shotState = true; }
            }
        }
        else if( cdefWeapon == 1 )
        {
            int before = cdefReloadingDelay;
            cdefReloadingDelay = before + 1;
            if( before >= 3 )
            {
                gbPlayOK();
                int a = cdefGuns[ 1 ].ammo;
                cdefGuns[ 1 ].ammo = a + 1;
                if( a < cdefGuns[ 1 ].ammoMax + 9 ) cdefReloadingDelay = 0;
                else { cdefReloading = true; cdefGuns[ 1 ].shotState = true; }
            }
        }
        else if( cdefWeapon == 2 )
        {
            int before = cdefReloadingDelay;
            cdefReloadingDelay = before + 1;
            if( before >= 6 )
            {
                gbPlayOK();
                int a = cdefGuns[ 2 ].ammo;
                cdefGuns[ 2 ].ammo = a + 1;
                if( a < cdefGuns[ 2 ].ammoMax + 3 ) cdefReloadingDelay = 0;
                else { cdefReloading = true; cdefGuns[ 2 ].shotState = true; }
            }
        }
    }
}

void cdefLevelUp()
{
    // "LEVEL UP!"/"DANGER" draw with a real opaque WHITE text background
    // (gbUpdate()'s own per-frame default - see its own header comment) so
    // they stay legible over the active gameplay art behind them.
    if( cdefLeveling )
    {
        cdefLevelUpX = cdefLevelUpX - 2;
        gbCursorX = cdefLevelUpX;
        gbCursorY = 0;
        gbSetFont( gbFont5x7 );
        gbPrintString( "LEVEL UP!" );
        gbSetFont( gbFont3x5 );
        if( cdefLevelUpX == -50 ) { cdefLeveling = false; cdefLevelUpX = LCDWIDTH; }
    }

    if( cdefBossing )
    {
        gbCursorX = 22 + arand( 2 );
        gbCursorY = 10 + arand( 2 );
        int curMsg = cdefBossMessage;
        cdefBossMessage = curMsg + 1;
        if( curMsg <= 70 )
        {
            gbSetFont( gbFont5x7 );
            gbPrintString( "DANGER" );
            cdefDrawIcon( 1 ); // real octal \01
            gbSetFont( gbFont3x5 );
        }
        if( cdefMonMax == 0 && cdefBossMessage >= 70 ) { cdefBoss = true; cdefBossMessage = 0; cdefBossing = false; }
    }
}

// -----------------------------------------------------------------------------
// Shop (shop.ino)
// -----------------------------------------------------------------------------

void cdefDisplayMessage( int weapons, int type )
{
    // This price label draws with a real opaque WHITE text background
    // (gbUpdate()'s own per-frame default - see its own header comment) so
    // it stays legible over the shop's own busy staircase bitmap art.
    gbCursorX = 60;
    gbCursorY = 2;
    if( weapons == 0 ) { if( type != 2 ) cdefPrintln( "50" ); else cdefPrintln( "80" ); }
    else if( weapons == 1 ) { if( type != 2 ) cdefPrintln( "60" ); else cdefPrintln( "80" ); }
    else if( weapons == 2 )
    {
        if( type == 0 ) cdefPrintln( "70" );
        else if( type == 1 ) cdefPrintln( "60" );
        else if( type == 2 ) cdefPrintln( "80" );
    }

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        cdefShopMsgWeapon = weapons;
        cdefShopMsgType = type;
        cdefState = CDEF_STATE_SHOP_MESSAGE;
    }
}

void cdefDisplayShop()
{
    if( cdefShopState )
    {
        if( cdefScroll <= 0 )
        {
            gbDrawBitmap( 9, cdefScroll, cdefShopBitmap );

            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 12, 9, 16, 11 ) ) { cdefDisplayMessage( 0, 0 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 30, 9, 19, 11 ) ) { cdefDisplayMessage( 1, 0 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 50, 9, 22, 11 ) ) { cdefDisplayMessage( 2, 0 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }

            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 12, 21, 16, 11 ) ) { cdefDisplayMessage( 0, 1 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 30, 21, 19, 11 ) ) { cdefDisplayMessage( 1, 1 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 50, 21, 22, 11 ) ) { cdefDisplayMessage( 2, 1 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }

            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 12, 32, 16, 10 ) ) { cdefDisplayMessage( 0, 2 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 30, 32, 19, 10 ) ) { cdefDisplayMessage( 1, 2 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }
            if( gbCollidePointRect( cdefPlayerX + 4, cdefPlayerY + 4, 50, 32, 22, 10 ) ) { cdefDisplayMessage( 2, 2 ); if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return; }

            if( gbPressed( BTN_C ) ) { gbPlayCancel(); cdefShopState = false; }
        }
        else
        {
            gbDrawBitmap( 9, cdefScroll, cdefShopBitmap );
            cdefScroll = cdefScroll - 1;
        }
    }
}

// The real upgrade-info + buy modal (shop.ino's own displaymessage() inner
// `while(1)`).
void cdefUpdateShopMessage()
{
    gbCursorX = 5;
    gbCursorY = 5;
    gbSetFont( gbFont5x7 );

    int weapons = cdefShopMsgWeapon;
    int type = cdefShopMsgType;
    bool owned = ( weapons == cdefMainWeapon || weapons == cdefSubWeapon );

    // Fixed here, not preserved - see this file's own header comment (item
    // 4) for the real upstream bug this replaces: the life-recovery text
    // (type==2) used to be printed as a separate, unconditional statement
    // after the ownership if/else instead of being nested into it the way
    // weapons==1/2 both are - so a player who doesn't own the rifle saw
    // BOTH "Impossible!" and the real life-recovery text/purchase option
    // stacked on the same screen. Life recovery is genuinely purchasable
    // without owning the rifle (a separate, deliberate item - confirmed by
    // the Buy/Cancel icon and the A-press purchase logic below, both of
    // which already correctly allow it regardless of `owned`), so it's now
    // checked first and shown on its own, with "Impossible!" only shown for
    // the two real rifle upgrades (type 0/1) when the rifle isn't owned.
    if( weapons == 0 )
    {
        if( type == 2 ) { cdefPrintln( " Life" ); cdefPrintln( " Recovery(+1)" ); gbSetFont( gbFont3x5 ); gbPrintString( "  Your Life : " ); cdefPrintlnNum( cdefMeHp ); }
        else if( owned )
        {
            if( type == 0 ) { cdefPrintln( "Rifle Damage" ); cdefPrintln( " Upgrade(+1)" ); gbSetFont( gbFont3x5 ); gbPrintString( "  Your Damage : " ); cdefPrintlnNum( cdefGuns[ 0 ].power ); }
            else if( type == 1 ) { cdefPrintln( "Rifle Ammo" ); cdefPrintln( " Upgrade(+2)" ); gbSetFont( gbFont3x5 ); gbPrintString( "  Your Ammo : " ); cdefPrintlnNum( cdefGuns[ 0 ].ammoMax + 20 ); }
        }
        else cdefPrintln( " Impossible!" );
    }
    else if( weapons == 1 )
    {
        if( owned )
        {
            if( type == 0 ) { cdefPrintln( "ShotgunDamage" ); cdefPrintln( " Upgrade(+2)" ); gbSetFont( gbFont3x5 ); gbPrintString( "  Your Damage : " ); cdefPrintlnNum( cdefGuns[ 1 ].power ); }
            else if( type == 1 ) { cdefPrintln( "Shotgun Ammo" ); cdefPrintln( " Upgrade(+2)" ); gbSetFont( gbFont3x5 ); gbPrintString( "  Your Ammo : " ); cdefPrintlnNum( cdefGuns[ 1 ].ammoMax + 10 ); }
            else if( type == 2 ) { cdefPrintln( "Slow time" ); cdefPrintln( " Upgrade(+10)" ); gbSetFont( gbFont3x5 ); gbPrintString( " Your slow time : " ); cdefPrintlnNum( 70 + cdefSlowUp ); }
        }
        else cdefPrintln( " Impossible!" );
    }
    else if( weapons == 2 )
    {
        if( owned )
        {
            if( type == 0 ) { cdefPrintln( "Sniper Damage" ); cdefPrintln( " Upgrade(+4)" ); gbSetFont( gbFont3x5 ); gbPrintString( "  Your Damage : " ); cdefPrintlnNum( cdefGuns[ 2 ].power ); }
            else if( type == 1 ) { cdefPrintln( "Sniper Ammo" ); cdefPrintln( " Upgrade(+2)" ); gbSetFont( gbFont3x5 ); gbPrintString( "  Your Ammo : " ); cdefPrintlnNum( cdefGuns[ 2 ].ammoMax + 4 ); }
            else if( type == 2 ) { cdefPrintln( "Stun time" ); cdefPrintln( " Upgrade(+10)" ); gbSetFont( gbFont3x5 ); gbPrintString( " Your stun time : " ); cdefPrintlnNum( 50 + cdefStunUp ); }
        }
        else cdefPrintln( " Impossible!" );
    }

    gbSetFont( gbFont3x5 );
    cdefPrintln( "" );
    if( owned || ( weapons == 0 && type == 2 ) )
    {
        cdefDrawIcon( 21 ); // real octal \25
        gbPrintString( ":Buy        " );
        cdefDrawIcon( 22 ); // real octal \26
        gbPrintString( ":cancel" );
        gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
        gbCursorX = 0;
    }

    if( gbPressed( BTN_A ) )
    {
        if( weapons == 0 )
        {
            if( owned )
            {
                if( type == 0 ) { if( cdefCoin >= 50 ) { gbPlayTick(); cdefCoin = cdefCoin - 50; cdefGuns[ 0 ].power = cdefGuns[ 0 ].power + 1; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
                else if( type == 1 ) { if( cdefCoin >= 50 ) { gbPlayTick(); cdefCoin = cdefCoin - 50; cdefGuns[ 0 ].ammoMax = cdefGuns[ 0 ].ammoMax + 2; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
            }
            if( type == 2 )
            {
                if( cdefCoin >= 80 )
                {
                    gbPlayTick();
                    if( cdefMeHp == 7 ) { gbPopup( " Your Life is full.", 10 ); }
                    else { cdefMeHp = cdefMeHp + 1; cdefCoin = cdefCoin - 80; gbPopup( " Upgrade complete!", 10 ); }
                }
                else { gbPopup( "   Have no money.", 10 ); }
            }
        }
        else if( weapons == 1 )
        {
            if( owned )
            {
                if( type == 0 ) { if( cdefCoin >= 60 ) { gbPlayTick(); cdefCoin = cdefCoin - 60; cdefGuns[ 1 ].power = cdefGuns[ 1 ].power + 2; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
                else if( type == 1 ) { if( cdefCoin >= 60 ) { gbPlayTick(); cdefCoin = cdefCoin - 60; cdefGuns[ 1 ].ammoMax = cdefGuns[ 1 ].ammoMax + 2; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
                else if( type == 2 ) { if( cdefCoin >= 80 ) { gbPlayTick(); cdefCoin = cdefCoin - 80; cdefSlowUp = cdefSlowUp + 10; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
            }
        }
        else if( weapons == 2 )
        {
            if( owned )
            {
                if( type == 0 ) { if( cdefCoin >= 70 ) { gbPlayTick(); cdefCoin = cdefCoin - 70; cdefGuns[ 2 ].power = cdefGuns[ 2 ].power + 4; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
                else if( type == 1 ) { if( cdefCoin >= 60 ) { gbPlayTick(); cdefCoin = cdefCoin - 60; cdefGuns[ 2 ].ammoMax = cdefGuns[ 2 ].ammoMax + 2; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
                else if( type == 2 ) { if( cdefCoin >= 80 ) { gbPlayTick(); cdefCoin = cdefCoin - 80; cdefStunUp = cdefStunUp + 10; gbPopup( " Upgrade complete!", 10 ); } else { gbPopup( "   Have no money.", 10 ); } }
            }
        }
    }
    else if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        cdefMonMax = 0;
        cdefState = CDEF_STATE_PLAY;
        return;
    }
    // real gb.popup()'s own overlay is drawn automatically by
    // gbRenderFrame() - see header comment.
}

// -----------------------------------------------------------------------------
// Game over / highscores (Castle.ino)
// -----------------------------------------------------------------------------

void cdefUpdateGameOver()
{
    cdefDisplayBackground();

    // Fixed here, not preserved - see this file's own header comment (item
    // 2) for the real upstream bug this replaces: incrementing
    // unconditionally only ever converges to 0 if camera_y was already
    // <= 0 at the moment of death - a player who had been climbing (a
    // completely normal play pattern) has a positive camera_y at death,
    // which incrementing only pushes further away, so the "GAME OVER" text
    // (and the only Button-B check that can exit this state) could
    // permanently never appear. Moves toward 0 from either side instead.
    if( cdefCameraY < 0 )
      cdefCameraY = cdefCameraY + 1;
    else if( cdefCameraY > 0 )
      cdefCameraY = cdefCameraY - 1;
    else
    {
        // "GAME OVER"/"please button"/"Your Socre :"/"NEW HIGHT SCORE" draw
        // with a real opaque WHITE text background (gbUpdate()'s own
        // per-frame default - see its own header comment) so they stay
        // legible over the castle-wall/fence bitmap art behind them.
        gbCursorX = 16;
        gbCursorY = 20;
        gbSetFont( gbFont5x7 );
        cdefPrintln( "GAME OVER" );
        gbSetFont( gbFont3x5 );
        cdefPrintln( "" );
        gbCursorX = 0;
        gbSetFont( gbFont5x7 );
        gbPrintString( "please button" );
        cdefDrawIcon( 22 ); // real octal \26
        gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
        gbCursorX = 0;
        gbSetFont( gbFont3x5 );
        gbCursorX = 11;
        gbPrintString( " Your Socre :" ); // real upstream typo, preserved
        cdefPrintlnNum( cdefCoin );
        gbCursorX = 12 + arand( 2 );
        gbCursorY = 0 + arand( 2 );
        if( cdefCoin > cdefHighscores[ CDEF_RANKMAX - 1 ] )
        {
            cdefPrintln( "NEW HIGHT SCORE" ); // real upstream typo, preserved
            if( gbPressed( BTN_B ) )
            {
                gbSetFont( gbFont5x7 );
                cdefSaveHighscore();
                gbSetFont( gbFont3x5 );
                cdefHighscoreReturn = CDEF_STATE_TITLE;
                cdefState = CDEF_STATE_HIGHSCORE;
            }
        }
        else if( gbPressed( BTN_B ) )
        {
            cdefHighscoreReturn = CDEF_STATE_TITLE;
            cdefState = CDEF_STATE_HIGHSCORE;
        }
    }
}

void cdefUpdateHighscore()
{
    gbCursorX = 3 + arand( 2 );
    gbCursorY = 0 + arand( 2 );
    gbSetFont( gbFont5x7 );
    cdefPrintln( "HIGH SCORES" );

    gbCursorX = 0;
    gbCursorY = gbFontHeight;
    int i;
    for( i = 0; i < CDEF_RANKMAX; i = i + 1 )
    {
        if( cdefHighscores[ i ] == 0 ) gbPrintString( "-" );
        // real name column dropped - no shim keyboard, see header comment
        gbCursorX = LCDWIDTH - 3 * gbFontWidth;
        gbCursorY = gbFontHeight + gbFontHeight * i;
        cdefPrintlnNum( cdefHighscores[ i ] );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        gbSetFont( gbFont3x5 );
        cdefState = cdefHighscoreReturn;
    }
}

// -----------------------------------------------------------------------------
// Init / title / weapon select / ready / play (Castle.ino)
// -----------------------------------------------------------------------------

void cdefInitGame()
{
    cdefWorldY = LCDHEIGHT;
    cdefCameraY = 0;
    cdefShakeX = 0;
    cdefShakeY = 0;
    cdefShakeState = false;

    cdefLevelUpX = LCDWIDTH;
    cdefLevel = 1;
    cdefLeveling = false;

    cdefShopState = false;
    cdefScroll = LCDHEIGHT;

    cdefMainSub = 0;
    cdefWeaponPoint = 0;
    cdefWeaponChange = false;
    cdefReadyCnt = 0;

    cdefPlayerX = LCDWIDTH / 2;
    cdefPlayerY = LCDHEIGHT / 2;
    cdefMeHp = 7;
    cdefSlowUp = 0;
    cdefStunUp = 0;
    cdefKill = 0;
    cdefCoin = 0;
    cdefReloadingDelay = 0;
    cdefReloading = true;

    cdefGuns[ 0 ].power = 2; cdefGuns[ 1 ].power = 7; cdefGuns[ 2 ].power = 12;
    cdefGuns[ 0 ].shotDelayTime = 0; cdefGuns[ 1 ].shotDelayTime = 21; cdefGuns[ 2 ].shotDelayTime = 35;
    cdefGuns[ 0 ].ammo = 20; cdefGuns[ 1 ].ammo = 10; cdefGuns[ 2 ].ammo = 4;

    int i;
    for( i = 0; i <= 2; i = i + 1 )
    {
        cdefGuns[ i ].ammoMax = 0;
        cdefGuns[ i ].shotState = true;
        cdefGuns[ i ].shotDelay = true;
        cdefGuns[ i ].reloadShow = true;
    }

    cdefMonRandomMax = 200;
    cdefMonHpUp = 0;
    cdefMonSpeedUp = 0;
    cdefMonMax = 0;
    cdefDefaultSpeed = 3;
    cdefBoss = false;
    cdefBossing = false;
    cdefBossDie = false;
    cdefBossMessage = 0;
    cdefMonDropCnt = 0;
    cdefDroping = false;
    cdefKingDefaultHp = cdefMonHpUp * 30;
    cdefMonsterDefaultHp = cdefMonHpUp + 10;

    for( i = 0; i <= 4; i = i + 1 )
    {
        cdefMon[ i ].monMovement = 0;
        cdefMon[ i ].slowTime = 0;
        cdefMon[ i ].stunTime = 0;
        cdefMon[ i ].monMove = false;
        cdefMon[ i ].monState = false;
        cdefMon[ i ].slowState = false;
        cdefMon[ i ].stunState = false;
    }
}

void cdefBeginTitle()
{
    cdefState = CDEF_STATE_TITLE;
}

// Reduced custom title screen - upstream's own real gb.titleScreen() call
// for this game passes no logo bitmap, so nothing is lost by not drawing
// one (matching gamePong.c's/gameFirem.c's own established simplification
// for a plain-text-only titleScreen() call).
void cdefUpdateTitle()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbCursorX = 4;
    gbCursorY = 14;
    gbPrintString( "CASTLE" );
    gbCursorX = 4;
    gbCursorY = 23;
    gbPrintString( "DEFENCE!" );
    gbSetFont( gbFont3x5 );
    gbCursorX = 8;
    gbCursorY = 38;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        cdefInitGame();
        cdefState = CDEF_STATE_WEAPON_SELECT;
    }
}

void cdefUpdateWeaponSelect()
{
    gbDrawBitmap( 15, 0, cdefSelectBitmap );
    if( cdefWeaponPoint == 0 ) gbDrawBitmap( 18, 27, cdefPointBitmap );
    else if( cdefWeaponPoint == 1 ) gbDrawBitmap( 36, 27, cdefPointBitmap );
    else if( cdefWeaponPoint == 2 ) gbDrawBitmap( 57, 27, cdefPointBitmap );

    if( cdefMainSub == 1 )
    {
        if( cdefMainWeapon == 0 ) gbDrawBitmap( 76, 37, cdefRifleImgBitmap );
        else if( cdefMainWeapon == 1 ) gbDrawBitmap( 76, 37, cdefShotgunImgBitmap );
        else if( cdefMainWeapon == 2 ) gbDrawBitmap( 76, 37, cdefSniperImgBitmap );
    }
    if( cdefMainSub == 2 )
    {
        if( cdefMainWeapon == 0 ) gbDrawBitmap( 76, 37, cdefRifleImgBitmap );
        else if( cdefMainWeapon == 1 ) gbDrawBitmap( 76, 37, cdefShotgunImgBitmap );
        else if( cdefMainWeapon == 2 ) gbDrawBitmap( 76, 37, cdefSniperImgBitmap );

        if( cdefSubWeapon == 0 ) gbDrawBitmap( 76, 31, cdefRifleImgBitmap );
        else if( cdefSubWeapon == 1 ) gbDrawBitmap( 76, 31, cdefShotgunImgBitmap );
        else if( cdefSubWeapon == 2 ) gbDrawBitmap( 76, 31, cdefSniperImgBitmap );
    }

    if( gbPressed( BTN_UP ) )
    {
        cdefHighscoreReturn = CDEF_STATE_WEAPON_SELECT;
        cdefState = CDEF_STATE_HIGHSCORE;
        return;
    }
    if( gbRepeat( BTN_LEFT, 5 ) )
    {
        if( cdefWeaponPoint != 0 ) { gbPlayTick(); cdefWeaponPoint = cdefWeaponPoint - 1; }
    }
    if( gbRepeat( BTN_RIGHT, 5 ) )
    {
        if( cdefWeaponPoint != 2 ) { gbPlayTick(); cdefWeaponPoint = cdefWeaponPoint + 1; }
    }
    if( gbPressed( BTN_A ) )
    {
        if( cdefMainSub == 0 ) { gbPlayOK(); cdefMainWeapon = cdefWeaponPoint; cdefMainSub = cdefMainSub + 1; }
        else if( cdefMainSub == 1 && cdefMainWeapon != cdefWeaponPoint ) { gbPlayOK(); cdefSubWeapon = cdefWeaponPoint; cdefMainSub = cdefMainSub + 1; }
        else if( cdefMainSub == 2 )
        {
            gbPlayOK();
            cdefWeapon = cdefMainWeapon;
            cdefState = CDEF_STATE_READY;
            return;
        }
    }
    if( gbPressed( BTN_B ) )
    {
        if( cdefMainSub != 0 ) { gbPlayCancel(); cdefMainSub = cdefMainSub - 1; }
    }
    if( gbPressed( BTN_C ) ) cdefBeginTitle();
}

void cdefUpdateReady()
{
    cdefDisplayBackground();
    cdefDisplayHp();
    if( cdefState == CDEF_STATE_GAMEOVER ) return; // Me_HP starts at 7 so unreachable here in practice, mirrors displayHP()'s own possible early-exit uniformly
    cdefDisplayAmmo();

    // "Ready..?"/"GO!!" draw with a real opaque WHITE text background
    // (gbUpdate()'s own per-frame default - see its own header comment) so
    // they stay legible over the castle-wall bitmap art behind them.
    gbCursorX = 26;
    gbCursorY = 11;
    if( ( !( cdefReadyCnt % 10 == 0 ) ) && cdefReadyCnt <= 49 )
      cdefPrintln( "Ready..?" );
    else if( cdefReadyCnt >= 50 )
    {
        gbSetFont( gbFont5x7 );
        gbCursorX = 26 + arand( 2 );
        gbCursorY = 11 + arand( 2 );
        cdefPrintln( " GO!! " );
        gbSetFont( gbFont3x5 );
    }
    if( cdefReadyCnt >= 80 )
    {
        cdefState = CDEF_STATE_PLAY;
        cdefReadyCnt = 0;
        return;
    }
    cdefReadyCnt = cdefReadyCnt + 1;
}

void cdefUpdatePlay()
{
    if( cdefShakeState ) cdefShakeScreen();
    else if( !cdefShopState ) cdefDisplayBackground();
    if( !cdefShopState ) cdefDisplayMonster();

    cdefDisplayHp();
    if( cdefState == CDEF_STATE_GAMEOVER ) return;

    cdefDisplayShotBar();
    cdefDisplayAmmo();
    cdefLevelUp();
    cdefMonsterDrop();
    cdefDisplayShop();
    if( cdefState == CDEF_STATE_SHOP_MESSAGE ) return;
    if( cdefBoss ) cdefAllKill();

    if( cdefKill == cdefLevel * 15 )
    {
        cdefLeveling = true;
        cdefLevel = cdefLevel + 1;
        if( cdefLevel % 5 == 0 ) cdefBossing = true;
        if( cdefLevel % 3 == 0 )
        {
            cdefMonSpeedUp = cdefMonSpeedUp + 1;
            cdefDefaultSpeed = cdefDefaultSpeed + 1;
            // Fixed here, not preserved - see this file's own header
            // comment (item 3) for the real upstream operator-precedence
            // bug this replaces (`!` binds tighter than `<=`, so the
            // intended "stop decreasing once low enough" floor guard was
            // actually always true, and the toughness range shrank
            // unboundedly forever instead of leveling off at 40).
            if( cdefMonRandomMax > 40 )
              cdefMonRandomMax = cdefMonRandomMax - 10;
        }
        if( cdefLevel % 2 == 0 )
        {
            cdefMonHpUp = cdefMonHpUp + 2;
            cdefKingDefaultHp = cdefMonHpUp * 30;
            cdefMonsterDefaultHp = cdefMonHpUp + 10;
        }
    }

    // display me
    if( cdefWeapon == 0 ) gbDrawBitmap( cdefPlayerX, cdefPlayerY, cdefRifleBitmap );
    else if( cdefWeapon == 1 ) gbDrawBitmap( cdefPlayerX, cdefPlayerY, cdefShotgunBitmap );
    else if( cdefWeapon == 2 ) gbDrawBitmap( cdefPlayerX - 16, cdefPlayerY - 16, cdefSniperRifleBitmap );

    // move
    if( gbRepeat( BTN_UP, 1 ) )
    {
        if( cdefPlayerY > -4 )
        {
            if( cdefWeapon == 0 ) cdefPlayerY = cdefPlayerY - 1;
            cdefPlayerY = cdefPlayerY - 1;
        }
        else
        {
            cdefPlayerY = -4;
            if( cdefWorldY > LCDHEIGHT )
            {
                if( cdefWeapon == 0 ) { cdefWorldY = cdefWorldY - 2; cdefCameraY = cdefCameraY + 2; }
                else { cdefWorldY = cdefWorldY - 1; cdefCameraY = cdefCameraY + 1; }
            }
            else { cdefCameraY = 0; cdefWorldY = 48; }
        }
    }
    if( gbRepeat( BTN_DOWN, 1 ) )
    {
        if( cdefPlayerY < LCDHEIGHT - 5 )
        {
            if( cdefWeapon == 0 ) cdefPlayerY = cdefPlayerY + 1;
            cdefPlayerY = cdefPlayerY + 1;
        }
        else
        {
            cdefPlayerY = LCDHEIGHT - 5;
            if( cdefWorldY < ( LCDHEIGHT * 2 ) )
            {
                if( cdefWeapon == 0 ) { cdefWorldY = cdefWorldY + 2; cdefCameraY = cdefCameraY - 2; }
                else { cdefWorldY = cdefWorldY + 1; cdefCameraY = cdefCameraY - 1; }
            }
            else { cdefCameraY = -48; cdefWorldY = 96; }
        }
    }
    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        if( cdefPlayerX > 5 )
        {
            if( cdefWeapon == 0 ) cdefPlayerX = cdefPlayerX - 1;
            cdefPlayerX = cdefPlayerX - 1;
        }
        else cdefPlayerX = 5;
    }
    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        if( cdefPlayerX < LCDWIDTH - 14 )
        {
            if( cdefWeapon == 0 ) cdefPlayerX = cdefPlayerX + 1;
            cdefPlayerX = cdefPlayerX + 1;
        }
        else cdefPlayerX = LCDWIDTH - 14;
    }

    // attack
    if( !cdefShopState )
    {
        if( cdefWeapon == 0 )
        {
            if( gbRepeat( BTN_A, 4 ) ) { cdefGuns[ 0 ].shotDelay = false; cdefAttack(); }
            else if( gbReleased( BTN_A ) ) cdefGuns[ 0 ].shotDelay = true;
        }
        else if( gbPressed( BTN_A ) ) cdefAttack();
    }

    // reload
    if( gbPressed( BTN_B ) )
    {
        if( !cdefShopState )
        {
            if( cdefWeapon == 0 ) { if( cdefGuns[ 0 ].ammo <= ( cdefGuns[ 0 ].ammoMax / 2 + 10 ) && cdefGuns[ 0 ].shotDelay ) { cdefReloading = false; cdefGuns[ 0 ].shotState = false; } }
            else if( cdefWeapon == 1 ) { if( cdefGuns[ 1 ].ammo <= ( cdefGuns[ 1 ].ammoMax / 2 + 5 ) && cdefGuns[ 1 ].shotDelay ) { cdefReloading = false; cdefGuns[ 1 ].shotState = false; } }
            else if( cdefWeapon == 2 ) { if( cdefGuns[ 2 ].ammo <= ( cdefGuns[ 2 ].ammoMax / 2 + 2 ) && cdefGuns[ 2 ].shotDelay ) { cdefReloading = false; cdefGuns[ 2 ].shotState = false; } }
        }
    }

    // weapon change
    if( gbPressed( BTN_C ) )
    {
        if( !cdefShopState )
        {
            if( cdefReloading )
            {
                gbPlayTick();
                if( !cdefWeaponChange ) { cdefWeapon = cdefSubWeapon; cdefWeaponChange = true; }
                else { cdefWeapon = cdefMainWeapon; cdefWeaponChange = false; }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameCastleDefence_init()
{
    gbBegin();
    cdefLoadHighscore();
    cdefBeginTitle();
}

void gameCastleDefence_update()
{
    if( !gbUpdate() ) return;

    if( cdefState == CDEF_STATE_TITLE ) cdefUpdateTitle();
    else if( cdefState == CDEF_STATE_WEAPON_SELECT ) cdefUpdateWeaponSelect();
    else if( cdefState == CDEF_STATE_HIGHSCORE ) cdefUpdateHighscore();
    else if( cdefState == CDEF_STATE_READY ) cdefUpdateReady();
    else if( cdefState == CDEF_STATE_PLAY ) cdefUpdatePlay();
    else if( cdefState == CDEF_STATE_SHOP_MESSAGE ) cdefUpdateShopMessage();
    else if( cdefState == CDEF_STATE_GAMEOVER ) cdefUpdateGameOver();

    gbRenderFrame();
}
