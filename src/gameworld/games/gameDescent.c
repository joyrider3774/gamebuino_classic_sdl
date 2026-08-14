#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// DescentIntoHeel / "Descent into Hell" (etienne72230, license: none
// specified - github.com/etienne72230/DescentIntoHeel). A small roguelike
// dungeon crawler: walk a 5x5 grid of rooms (one screen each), shoot
// monsters that spawn the first time a room is entered, collect dropped
// rate/damage/heal power-ups, find the down-stairs room on each level to
// descend (score +100 per level), and survive - health hits 0 -> Game
// Over -> a 5-entry high-score table. A pause-style in-game Map screen
// (Button C) shows the whole explored dungeon, current stats, and can
// jump to the high-score table or force-restart. Originally 8 real Arduino
// `.ino` tabs sharing one translation unit (`DescentIntoHell.ino` -
// setup()/loop()/drawGame()/GameOver()/leveldown(), `Donjon.ino` - maze
// generation, `Hightscore.ino` [sic, upstream's own real spelling] -
// EEPROM load/save, `Intro.ino` - the boot logo + 2-frame animated intro,
// `Item.ino`/`Monster.ino`/`Shoot.ino` - the three "actor" systems,
// `Map.ino` - the pause map) - flattened here into one file with a
// `descent`-prefixed name on every real global/function (this dialect's
// single flat namespace, shared by every game in the cartridge).
//
// DIALECT REWRITES: every real `gb.x.y(...)` call site was mechanically
// rewritten to a plain `gbY(...)` function call (see gamePong.c's own
// header comment - this dialect has no classes/methods/operator
// overloading). `and`/`or` (real, valid C++ alternative operator tokens
// upstream uses throughout instead of `&&`/`||`) were rewritten to
// `&&`/`||`. `random(N)`/`random(a,b)` became `arand(N)`/`a+arand(b-a)`
// (this project's own established RNG helper - every range was individually
// re-derived from upstream's real Arduino semantics, `[min,max)`, not
// assumed - see the "random(-1,1)" and "type-2 wander" notes below for two
// real ranges this mattered for). `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op. `gb.battery.show=false` and
// `gb.display.textWrap=false` (both real hardware-only cosmetic settings
// with no shim equivalent - a battery icon and an auto-wrap flag that would
// never actually trigger here anyway, given how short every printed string
// in this game is) were dropped outright, matching this project's
// established norm for this class of setting. Upstream's own two real
// `switch` statements (`Item.ino`'s `updateItem()`, `Monster.ino`'s
// `updateMonster()`) became if/else-if chains, matching the standing
// caution (gamePong.c/gameAgaruino.c/gameTaquin.c/gameCrazyCar.c/etc) that
// this dialect's exact `switch` support is unproven. The real `Donjon[8][25]`
// dungeon-state grid and `monstertype[NUM_MONSTER][3]` stat table are kept
// as genuine 2D arrays (`int[8][25] descentDonjon`, `int[NUM_MONSTER][3]
// descentMonsterType`, both `TYPE[N][M] name` declarations, read/written
// with real `arr[row][col]` syntax) rather than flattened to 1D - confirmed
// safe first against gameConduit.c's own `condMap`/gameAsterocks.c's own
// sprite-frame tables, both genuine runtime-mutable 2D arrays already
// proven working in this dialect, so no flattening was needed here (Item's
// upstream `Config`/`confshoot`/`confmonster`/`confitem` structs likewise
// port directly as plain C structs, e.g. `struct DescentPlayer {...};
// DescentPlayer descentPlayer;` - already plain data-only structs upstream,
// no real class/method content to flatten - and a struct *array* was
// confirmed working the same way via gameCrazyCar.c's own `CcarObstacle[3]
// ccarObstacles;`).
//
// THREE REAL SHIM PRIMITIVES THIS GAME RELIES ON:
//
// (1) `gb.collideBitmapBitmap()` (real pixel-perfect bitmap-vs-bitmap
// collision) is the single most-used real primitive in this specific game
// (9 real call sites across DescentIntoHell.ino/Item.ino/Monster.ino/
// Shoot.ino - every monster/player/item/bullet hit-test), provided by
// `gbCollideBitmapBitmap()`/`gbGetBitmapPixel()` in `gamebuinoShim.h`/`.c`,
// a direct, bit-for-bit port of real `Gamebuino::collideBitmapBitmap()`/
// `Display::getBitmapPixel()` (read straight from
// `more games/Gamebuino-Classic/Gamebuino.cpp`/`utility/Display.cpp`: same
// bounding-box pre-check via `gbCollideRectRect()`, same per-overlap-pixel
// AND test) - every call site here uses these real shim primitives
// directly.
//
// (2) `gbAbsInt()` is declared in gamebuinoShim.h (used internally by
// `gbDrawLine()`, and directly by gamePong.c), and every integer-abs call
// site here uses the real `gbAbsInt()` directly. The float half,
// `fabs()` (needed for `Monster.ino`'s own type-3 "chase" AI, which calls
// Arduino's generic `abs()` on a real `float` subtraction - `monster.x`/
// `monster.y` are floats), is not a shim primitive at all: this dialect's
// own `math.h` already provides real `fabs()` (confirmed via the sibling
// tinyjoypad_vircon32 project's own VIRCON32_C_DIALECT.md, already used
// directly by gameCrazyTown.c/gameHexagon.c/gameCopter.c), so this file
// calls `fabs()` directly too.
//
// (3) `GB_INVERT` is a real, working third draw color in gamebuinoShim.h,
// supported by `gbDrawBitmap()` (which XORs for `GB_INVERT`, confirmed
// directly in gamebuinoShim.c). This game genuinely needs correct
// bitmap-level INVERT, confirmed by reading the real source line-by-line:
// `drawGame()` sets `setColor(INVERT)` once and, with NO further
// `setColor()` call in between, draws the health icons AND all 4 door + 4
// barrier + 4 stairs bitmaps in that same real INVERT mode (easy to miss
// on a first pass, since only the health-icon loop is visually separated
// from the door-drawing block by a blank line in the source) - genuinely
// meaningful, not cosmetic: an INVERT-drawn door sprite carves a real
// opening-shaped notch out of the terrain bitmap's own already-solid wall
// line underneath it (a plain BLACK-on-BLACK door draw would be entirely
// invisible there). Two more real INVERT sites, found the same way: the
// player's own invulnerability flash (`DescentIntoHell.ino`'s main loop)
// and the monster hit-flash (`Shoot.ino`'s `updateShoot()`). All three
// sites here just call `gbSetColor(GB_INVERT); gbDrawBitmap(...);`
// directly.
//
// REAL UPSTREAM BUGS FOUND - PRESERVED OR DROPPED, WITH REASONING:
//
// - `leveldown()`'s own `monstertype[NUM_MONSTER][0]+=1;` (clearly meant to
// toughen every monster type's HP every 2 levels) is a genuine
// out-of-bounds array write even on real hardware: `NUM_MONSTER` is 5, and
// `monstertype[NUM_MONSTER][3]`'s valid rows are 0-4, so this line (run 5
// times, always at the same out-of-bounds `[5][0]` cell) never actually
// reaches any real monster type's own stats - confirmed inert by reading
// the real declaration directly. This project has an established precedent
// of preserving real out-of-bounds *reads* verbatim (e.g. gameUfoRace.c's
// own unguarded `ufoGetTile()`) since those are provably harmless on this
// engine (a plain garbage-value read, no side effect elsewhere) - but an
// out-of-bounds *write* carries no such guarantee on a different
// architecture, for a line that provably changes nothing real either way.
// Dropped outright rather than reproduced, called out here instead of
// silently vanishing.
// - `initMonster()`'s own `do{...}while(place_ok=false);` (assignment, not
// comparison) is a genuine real logic bug: the loop's own condition always
// evaluated falsy regardless of `place_ok`'s real value, so the body always
// ran exactly once and an overlapping monster spawn was never actually
// retried - monsters could spawn stacked on top of each other. FIXED, NOT
// PRESERVED: the loop below now genuinely retries (`while( placeOk ==
// false )`), matching the clearly-intended real comparison rather than the
// typo'd assignment.
// - `Item.ino`'s own `initItem()` has a real dead `alea==6` branch: Arduino's
// own `random(0,6)` (this port's own `arand(6)`) is exclusive of 6, so it
// can never actually produce 6 on real hardware either. Preserved verbatim
// as an inert branch, the same treatment gameUfoRace.c's own header comment
// gives its preserved dead `highscore[i]==0` EEPROM check.
// - `Shoot.ino`'s own `initShoot()` has four bare `shoot[i].orientation==0;`
// -style statements (a comparison, not an assignment - each one sits
// directly under a line that already assigns the same value) - a provable
// no-op, discarding an unused comparison result. Dropped outright.
// - `Monster.ino`'s own type-4 movement has two bare `int(monster[i].x);
// int(monster[i].y);` statements (C++-style function-cast syntax, result
// discarded, x/y never reassigned) - another provable no-op, and not even
// valid syntax as a bare statement in this C dialect. Dropped outright.
// - A genuinely subtle, confirmed, PRESERVED color-state bug: the main
// loop's own player-invulnerability flash sets `setColor(INVERT)`
// unconditionally whenever `player.invul>0`, but only resets it back to
// `setColor(BLACK)` on the alternating tick that actually redraws the
// player sprite with it (`invul%2==1`) - so on every *other* invul tick,
// the very next real draw call, `drawShoot()`, runs with the color left at
// INVERT, meaning that tick's own bullets render as a real XOR toggle
// instead of solid BLACK. `drawGame()`'s own trailing `setColor(BLACK)`
// resets it again at the very start of the *next* frame, so the bleed is
// confined to that one tick's own bullets - preserved exactly (see
// `descentUpdatePlay()` below).
// - Map()'s own Button-B "restart" calls upstream's real `initGame()`
// directly (not `GameOver()`), and `initGame()` never resets `finish`/`fin`
// (unlike `leveldown()`/`GameOver()`, which both explicitly do) - so
// `initDonjon()`'s own two generation while-loops silently no-op (both
// flags are already `true`, left over from the very first real dungeon
// generation this session). Net real effect: this "restart" reshows the
// title screen and resets the player/level/shots, but keeps reusing
// whatever dungeon layout (room connections/doors/visited flags) was
// already on the grid - and, since `playintro` is also left untouched
// (already `false` mid-game), skips the Intro() animation too, unlike a
// real `GameOver()` which explicitly resets both. Preserved exactly as
// `descentMapRestart()`.
// - `initMonster()`'s own real `random(-1,1)` (a type-1 monster's initial
// per-axis velocity) only ever returns -1 or 0, never +1 (Arduino's own
// `random(min,max)` is exclusive of `max`) - confirmed and preserved
// (`-1 + arand(2)`): a type-1 monster can only ever *spawn* drifting
// up/left, though its own bounce-off-the-walls physics can still flip it
// positive afterward.
// - The type-2 "wander" monster's own two real target-repick call sites use
// different ranges: `initMonster()` itself picks `random(8,75)`/
// `random(8,35)`, while `updateMonster()`'s own on-arrival retarget instead
// picks `random(8,72)`/`random(4,36)` - a real, confirmed upstream
// inconsistency (not a typo this port can safely guess the "intended"
// value for), preserved exactly as two different ranges rather than
// unified.
// - Real upstream only ever calls `setFont(font5x7)` once, inside
// `saveHighscore()` (immediately before the now-dropped keyboard widget -
// see EEPROM note below) - so the high-score table renders in the bigger
// font5x7 only when reached via a real Game Over, and in whatever font was
// already active (always font3x5 in practice, since nothing else in this
// game ever switches fonts) when peeked at mid-game via the Map screen's
// own Button A. Preserved exactly via `descentBeginHighscore(fromGameOver)`'s
// own conditional `gbSetFont()` call.
// - The Game-Over-to-state-machine conversion drops one genuinely harmless
// extra frame real hardware would draw: upstream's own main loop calls
// `drawGame()`/`drawItem()`/etc unconditionally right after the
// `if(player.heal<=0){...}` block, so on real hardware those still run once
// more - using the freshly-*reset* post-GameOver state - right after the
// entire death -> highscore -> title -> dismiss round trip completes and
// control unwinds back up through the nested blocking calls. Not
// reproduced here (an early `return` is used instead - see
// `descentUpdatePlay()`), since it's cosmetically invisible: that stray
// frame is immediately overwritten by the very next real PLAY tick's own
// identical draw anyway.
//
// EEPROM: `Hightscore.ino` genuinely calls real `EEPROM.read()`/
// `EEPROM.write()` (confirmed directly) - wired to this shim's own real
// `eeprom_read_byte()`/`eeprom_write_byte()`. The name-entry half
// (`gb.getDefaultName()`+`gb.keyboard()`, a real on-screen text-entry
// widget) has no shim equivalent at all and is dropped entirely - a
// documented, already-established scope limit (see gameUfoRace.c's own
// header comment, "NAME ENTRY - DROPPED, DOCUMENTED"), not flagged as a new
// gap. `descentLoadHighscore()`/`descentApplyNewHighscore()` keep upstream's
// own real bubble-sort-on-insert and its own real
// `highscore[i]==0xFFFF -> 0` fresh-EEPROM sentinel check (genuinely
// meaningful here, unlike gameUfoRace.c's own inert `==0` check, since this
// shim's own EEPROM really does read fresh/unwritten cells as 0xFF per
// byte, matching real AVR - see eepromShim.h's own header comment) - but at
// a simpler 2-bytes-per-entry layout (score only, no names), matching
// gameUfoRace.c's own already-established precedent for a name-less
// EEPROM table; this isn't meant to be byte-compatible with a real
// cartridge's own EEPROM dump anyway (a distinct memory-card slot, keyed by
// this game's own menu title).
//
// STATE MACHINE: every one of upstream's real blocking calls became an
// explicit, resumable state (matching gamePong.c's own established
// "blocking call -> explicit state" treatment): `gb.titleScreen(name,logo)`
// -> `DESCENT_STATE_TITLE` (dismissed by a genuine `gbPressed(BTN_A)`, this
// engine's own menu-select button, matching real titleScreen()'s own real
// dismiss button); `Intro()`'s own real 20-tick, 2-frame, 10fps boot
// animation -> `DESCENT_STATE_INTRO`; `Map()`'s own real blocking
// `while(true)` pause screen -> `DESCENT_STATE_MAP`; the Game-Over message's
// own real blocking `while(true)` -> `DESCENT_STATE_GAMEOVER_MSG`; and
// `displayHighScores()`'s own real blocking `while(true)` ->
// `DESCENT_STATE_HIGHSCORE`, shared by both of its own real call sites
// (post-Game-Over, and Map's own Button-A "peek" mid-game) and
// distinguished by `descentHighscoreFromGameOver` (which of the two
// controls both the font-switch quirk above and where dismissing it
// returns to). Real `gb.popup(F("Level Down"),30)` (a genuine non-blocking
// overlay drawn by real hardware's own `gb.update()` on top of whatever
// gameplay keeps running underneath it) became a small non-blocking
// `descentPopupTimer`/`descentDrawPopup()` pair, the same treatment
// gameUfoRace.c's own `ufoPopupTimer` already established for the same
// real upstream feature - since no real per-pixel layout for it exists
// anywhere in this game's own source to port (a library-internal widget),
// its own box/text layout here is this port's own reasonable
// approximation, matching that same precedent.
//
// BITMAPS: all 28 real PROGMEM bitmaps (the boot logo, the 88x48 terrain,
// 4 player-facing sprites, 4 doors, 2 barriers, 4 stairs, 2 map icons, 5
// monsters, 3 items, 1 bullet, and the 2 intro-animation frames) were
// extracted byte-for-byte from the real source, verified with a small
// Python script that recomputed each one's own expected element count
// (`2 + ceil(width/8)*height`) against its real literal's actual length
// before transcribing - zero truncation/miscount. `\27`/`\25`/`\26` (real
// octal escapes for Gamebuino's own custom icon glyphs, ASCII 23/21/22)
// are built as explicit `int[]` arrays for the Map/Game-Over hint text,
// matching gameTaquin.c's/gameUfoRace.c's own established precedent for
// this exact glyph range - including upstream's own real "Hight Score"
// spelling (not "High"), preserved verbatim like every other real upstream
// display string this project ports.

#define MAX_SHOOT 30
#define MAX_MONSTER 5
#define NUM_MONSTER 5
#define RANKMAX 5

// -----------------------------------------------------------------------------
// Real upstream bitmaps (byte-for-byte, see header comment)
// -----------------------------------------------------------------------------

int descentLogoBitmap[242]= {
    0x40, 0x1e, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x7f, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4,
    0x40, 0x44, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xdf, 0xfc, 0x1, 0xff, 0x0, 0x0, 0x0, 0x1,
    0x11, 0x10, 0x1, 0x11, 0x0, 0x0, 0x0, 0x1, 0xff, 0xf0, 0x1, 0xff, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x44, 0x0, 0xff, 0xff, 0xf0, 0x0, 0x0, 0x0, 0x7f, 0x0, 0x8f, 0xff, 0xf0,
    0x0, 0x0, 0x0, 0x11, 0x0, 0xbf, 0xff, 0xf0, 0x0, 0x0, 0x0, 0x1f, 0x0, 0xff, 0xff, 0xf0,
    0x7, 0xc0, 0x0, 0x0, 0xf, 0x0, 0x0, 0xf, 0x4, 0x40, 0x0, 0x0, 0x9, 0x0, 0xf0, 0xf,
    0x1f, 0xc0, 0x0, 0x0, 0xb, 0x1, 0xf8, 0xf, 0x11, 0x0, 0x0, 0x0, 0xb, 0x3, 0xfc, 0xf,
    0x1f, 0x0, 0x0, 0x0, 0xb, 0xe, 0x67, 0xf, 0x0, 0x0, 0x3, 0x80, 0xf, 0xd, 0x9b, 0xf,
    0x0, 0x0, 0x3, 0x80, 0xf, 0xe, 0x97, 0xf, 0x0, 0x0, 0x3, 0x0, 0xf, 0xf, 0xff, 0xf,
    0x0, 0x0, 0x0, 0x0, 0xf0, 0xf, 0xf, 0x0, 0xf0, 0x0, 0x0, 0x0, 0x90, 0xe, 0x97, 0x0,
    0xf0, 0x0, 0x3, 0xf0, 0xb0, 0xf, 0x9f, 0x0, 0xf0, 0x0, 0x2, 0x60, 0xb0, 0xf, 0x6f, 0x0,
    0xf0, 0x0, 0x3, 0x80, 0xbf, 0xe, 0x7, 0xf, 0xf0, 0x0, 0x3, 0x80, 0xbf, 0xd, 0xfb, 0xf,
    0xf0, 0x0, 0x3, 0x80, 0xff, 0x8, 0x1, 0xf, 0xf0, 0x0, 0xf8, 0x6f, 0xff, 0x7, 0xfe, 0xf,
    0xff, 0xff, 0x1c, 0x70, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1c, 0x70, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0,
};

int descentTerrainBitmap[530]= {
    0x58, 0x30, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x20, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x40, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x8, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x0, 0x4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x2, 0x0, 0x3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x0, 0x2,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4,
    0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0,
    0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4,
    0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x4, 0x0, 0x3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x0,
    0x4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x8, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x1, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x20, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x40, 0x40, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x10,
};

int descentPlayerUpBitmap[8]= {
    0x8, 0x6, 0x20, 0x70, 0x70, 0x88, 0x70, 0xd8,
};

int descentPlayerRightBitmap[8]= {
    0x8, 0x6, 0x20, 0x50, 0x70, 0x48, 0x70, 0xd8,
};

int descentPlayerDownBitmap[8]= {
    0x8, 0x6, 0x20, 0x50, 0x70, 0x88, 0x70, 0xd8,
};

int descentPlayerLeftBitmap[8]= {
    0x8, 0x6, 0x20, 0x50, 0x70, 0x90, 0x70, 0xd8,
};

int descentDoorUpBitmap[14]= {
    0x10, 0x6, 0x3f, 0x0, 0x7f, 0x80, 0x61, 0x80, 0xc0, 0xc0, 0xc0, 0xc0, 0x3f, 0x0,
};

int descentDoorRightBitmap[12]= {
    0x8, 0xa, 0x60, 0x78, 0x9c, 0x8c, 0x8c, 0x8c, 0x8c, 0x9c, 0x78, 0x60,
};

int descentDoorDownBitmap[14]= {
    0x10, 0x6, 0x3f, 0x0, 0xc0, 0xc0, 0xc0, 0xc0, 0x61, 0x80, 0x7f, 0x80, 0x3f, 0x0,
};

int descentDoorLeftBitmap[12]= {
    0x8, 0xa, 0x18, 0x78, 0xe4, 0xc4, 0xc4, 0xc4, 0xc4, 0xe4, 0x78, 0x18,
};

int descentBarrierHBitmap[5]= {
    0x8, 0x3, 0xfc, 0x48, 0xfc,
};

int descentBarrierVBitmap[8]= {
    0x8, 0x6, 0xa0, 0xe0, 0xa0, 0xa0, 0xe0, 0xa0,
};

int descentStairsUpBitmap[16]= {
    0x10, 0x7, 0x3f, 0xc0, 0x7f, 0xe0, 0x66, 0x60, 0x6f, 0x60, 0xcf, 0x30, 0xcf, 0x30, 0x10, 0x80,
};

int descentStairsRightBitmap[14]= {
    0x8, 0xc, 0x60, 0x7c, 0x1c, 0x86, 0x76, 0x7e, 0x7e, 0x76, 0x86, 0x1c, 0x7c, 0x60,
};

int descentStairsDownBitmap[16]= {
    0x10, 0x7, 0x10, 0x80, 0xcf, 0x30, 0xcf, 0x30, 0x6f, 0x60, 0x66, 0x60, 0x7f, 0xe0, 0x3f, 0xc0,
};

int descentStairsLeftBitmap[14]= {
    0x8, 0xc, 0xc, 0x7c, 0x70, 0xc2, 0xdc, 0xfc, 0xfc, 0xdc, 0xc2, 0x70, 0x7c, 0xc,
};

int descentMapExitBitmap[9]= {
    0x8, 0x7, 0x0, 0x38, 0x20, 0x30, 0x20, 0x38, 0x0,
};

int descentMapPlayerBitmap[9]= {
    0x8, 0x7, 0x0, 0x10, 0x0, 0x38, 0x10, 0x28, 0x0,
};

int descentMonster1Bitmap[8]= {
    0x8, 0x6, 0x70, 0xa8, 0xf8, 0x70, 0xd8, 0x88,
};

int descentMonster2Bitmap[7]= {
    0x8, 0x5, 0x30, 0xd0, 0xa8, 0x58, 0x60,
};

int descentMonster3Bitmap[7]= {
    0x8, 0x5, 0xcc, 0x30, 0x78, 0x30, 0x48,
};

int descentMonster4Bitmap[9]= {
    0x8, 0x7, 0x20, 0x70, 0x50, 0x88, 0xf8, 0xa8, 0x50,
};

int descentMonster5Bitmap[10]= {
    0x8, 0x8, 0x66, 0x99, 0x5a, 0x0, 0x3c, 0x5a, 0x18, 0x24,
};

int descentItemRateBitmap[4]= {
    0x8, 0x2, 0x78, 0xa0,
};

int descentItemDamageBitmap[6]= {
    0x8, 0x4, 0x40, 0xa0, 0xe0, 0xe0,
};

int descentItemHealBitmap[6]= {
    0x8, 0x4, 0xd8, 0xf8, 0x70, 0x20,
};

int descentBulletBitmap[4]= {
    0x8, 0x2, 0xc0, 0xc0,
};

int descentIntro1Bitmap[530]= {
    0x58, 0x30, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
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
    0x0, 0x0, 0x0, 0x1, 0xf8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0xf8,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x6, 0x6, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x6, 0x6, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x6,
    0x0, 0x1f, 0x86, 0x6, 0x1f, 0x87, 0xf8, 0x0, 0x0, 0x0, 0x6, 0x0, 0x1f, 0x86, 0x6, 0x1f,
    0x87, 0xf8, 0x0, 0x0, 0x0, 0x6, 0x0, 0x0, 0x66, 0x6, 0x60, 0x66, 0x6, 0x0, 0x0, 0x0,
    0x6, 0x0, 0x0, 0x66, 0x6, 0x60, 0x66, 0x6, 0x0, 0x0, 0x0, 0x6, 0x0, 0x1f, 0xe6, 0x6,
    0x60, 0x66, 0x6, 0x0, 0x0, 0x0, 0x6, 0x0, 0x1f, 0xe6, 0x6, 0x60, 0x66, 0x6, 0x0, 0x0,
    0x0, 0x6, 0x6, 0x60, 0x61, 0xfe, 0x60, 0x66, 0x6, 0x0, 0x0, 0x0, 0x6, 0x6, 0x60, 0x61,
    0xfe, 0x60, 0x66, 0x6, 0x0, 0x0, 0x0, 0x1, 0xf8, 0x1f, 0xe0, 0x6, 0x1f, 0x86, 0x6, 0x0,
    0x0, 0x0, 0x1, 0xf8, 0x1f, 0xe0, 0x6, 0x1f, 0x86, 0x6, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x7, 0xf8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0xf8, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0,
};

int descentIntro23Bitmap[530]= {
    0x58, 0x30, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
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
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1f, 0xe0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x1f, 0xe0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x60, 0x18, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x60, 0x18, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x67, 0x98, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x67, 0x98, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x67, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x67, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x67, 0xf8,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x67, 0xf8, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x60, 0x18, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x60,
    0x18, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1f, 0xe0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x1f, 0xe0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
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
    0x0, 0x0,
};

int* descentPlayerSprites[4]= {
    descentPlayerUpBitmap, descentPlayerRightBitmap, descentPlayerDownBitmap, descentPlayerLeftBitmap
};

int* descentMonsterSprites[NUM_MONSTER]= {
    descentMonster1Bitmap, descentMonster2Bitmap, descentMonster3Bitmap, descentMonster4Bitmap, descentMonster5Bitmap
};

int* descentItemSprites[3]= {
    descentItemRateBitmap, descentItemDamageBitmap, descentItemHealBitmap
};

// Real octal-escape icon-glyph text (see header comment) - upstream's own
// real "Hight Score" spelling preserved verbatim.
char descentHightScoreText[14]= { 23, 32, 72, 105, 103, 104, 116, 32, 83, 99, 111, 114, 101, 0 };
char descentMapReturnText[9]= { 23, 32, 114, 101, 116, 117, 114, 110, 0 };
char descentMapScoreText[8]= { 21, 32, 83, 99, 111, 114, 101, 0 };
char descentMapQuitText[7]= { 22, 32, 81, 117, 105, 116, 0 };

// -----------------------------------------------------------------------------
// Real upstream data (Donjon.ino/DescentIntoHell.ino) - dungeon grid kept as
// a genuine 2D array (see header comment on why flattening wasn't needed).
// [row 0]=cluster id (Kruskal-style union-find during generation), [1]=room
// x coord, [2]=room y coord, [3..6]=door state (0=none,1=door,2=stairs) for
// top/right/bottom/left, [7]=room visited flag. Column index = room number
// (0-24, a 5x5 grid).
// -----------------------------------------------------------------------------

int descentDonjon[8][25]=
{
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 },
    { 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4 },
    { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

// Real `monstertype[NUM_MONSTER][3]` - [heal, damage, wait_time_under_step]
// per monster type.
int descentMonsterType[NUM_MONSTER][3]=
{
    { 2, 1, 3 },
    { 1, 1, 1 },
    { 2, 2, 2 },
    { 3, 2, 5 },
    { 5, 2, 1 },
};

// -----------------------------------------------------------------------------
// Structs (flattened from upstream's own plain, already-class-free C
// structs - see header comment). Real upstream declares monster[8]/item[8]
// but every loop bound in this game is really MAX_MONSTER (5) - indices 5-7
// of both arrays are simply never touched, an oversized-but-harmless real
// upstream declaration, preserved as-is rather than "corrected" to 5.
// -----------------------------------------------------------------------------

typedef struct
{
    int x;
    int y;
    int vx;
    int vy;
    int donjonroom;
    int orientation;
    int heal;
    int damage;
    int rate;
    int score;
    int invul;
} DescentPlayer;
DescentPlayer descentPlayer;

typedef struct
{
    bool on;
    int x;
    int y;
    int vx;
    int vy;
    int orientation;
} DescentShoot;
DescentShoot descentShoot[MAX_SHOOT];

typedef struct
{
    bool on;
    int type;
    float x;
    float y;
    int vx;
    int vy;
    int heal;
    int damage;
    int time_step;
    int orientation;
} DescentMonster;
DescentMonster descentMonster[8];

typedef struct
{
    bool on;
    int type;
    int x;
    int y;
} DescentItem;
DescentItem descentItem[8];

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

int descentAlea;
int descentCote;
int descentRando;
int descentOk;
int descentDonjonLevel;
bool descentFinish;
bool descentFin;
float descentShootDelay;
int descentWait;
int descentImage;          // Intro.ino's own real frame counter
bool descentPlayIntro;     // real upstream `playintro`
bool descentHighscoreFromGameOver;
int descentPopupTimer;     // non-blocking real gb.popup() approximation - see header comment
int descentHighscore[RANKMAX];

enum DescentState
{
    DESCENT_STATE_TITLE = 0,
    DESCENT_STATE_INTRO = 1,
    DESCENT_STATE_PLAY = 2,
    DESCENT_STATE_MAP = 3,
    DESCENT_STATE_GAMEOVER_MSG = 4,
    DESCENT_STATE_HIGHSCORE = 5
};
int descentState;

// -----------------------------------------------------------------------------
// Donjon.ino - real dungeon generation, ported directly (2D array syntax
// confirmed working - see header comment). First while-loop: a real
// Kruskal-style random-edge union-find maze generator (`descentDonjon[0][n]`
// is room n's own cluster id; two rooms are connected until every room
// shares cluster id 0). Second while-loop: places the down-stairs in a real
// dead-end room (exactly one door) other than the start room (12).
// -----------------------------------------------------------------------------

void descentInitDonjon()
{
    while( descentFinish == false )
    {
        descentAlea = arand( 24 );
        descentCote = arand( 2 );
        if( descentDonjon[ 1 ][ descentAlea ] == 4 )
          descentCote = 1;
        if( descentDonjon[ 2 ][ descentAlea ] == 4 )
          descentCote = 0;
        if( descentDonjon[ 2 ][ descentAlea ] == 4 && descentDonjon[ 1 ][ descentAlea ] == 4 )
          descentCote = 2;

        if( descentCote == 0 && descentDonjon[ 0 ][ descentAlea ] != descentDonjon[ 0 ][ descentAlea + 1 ] )
        {
            int i;
            for( i = 0; i < 25; i++ )
              if( descentDonjon[ 0 ][ descentAlea + 1 ] == descentDonjon[ 0 ][ i ] && i != ( descentAlea + 1 ) )
                descentDonjon[ 0 ][ i ] = descentDonjon[ 0 ][ descentAlea ];
            descentDonjon[ 0 ][ descentAlea + 1 ] = descentDonjon[ 0 ][ descentAlea ];
            descentDonjon[ 4 ][ descentAlea ] = 1;
            descentDonjon[ 6 ][ descentAlea + 1 ] = 1;
        }
        if( descentCote == 1 && descentDonjon[ 0 ][ descentAlea ] != descentDonjon[ 0 ][ descentAlea + 5 ] )
        {
            int i;
            for( i = 0; i < 25; i++ )
              if( descentDonjon[ 0 ][ descentAlea + 5 ] == descentDonjon[ 0 ][ i ] && i != ( descentAlea + 5 ) )
                descentDonjon[ 0 ][ i ] = descentDonjon[ 0 ][ descentAlea ];
            descentDonjon[ 0 ][ descentAlea + 5 ] = descentDonjon[ 0 ][ descentAlea ];
            descentDonjon[ 5 ][ descentAlea ] = 1;
            descentDonjon[ 3 ][ descentAlea + 5 ] = 1;
        }

        descentOk = 0;
        int i;
        for( i = 0; i < 25; i++ )
          if( descentDonjon[ 0 ][ 0 ] == descentDonjon[ 0 ][ i ] )
            descentOk = descentOk + 1;
        if( descentOk == 25 )
          descentFinish = true;
    }

    while( descentFin == false )
    {
        descentOk = 0;
        descentAlea = arand( 25 );
        int i;
        for( i = 3; i < 7; i++ )
          if( descentDonjon[ i ][ descentAlea ] == 1 && descentAlea != 12 )
            descentOk = descentOk + 1;

        if( descentOk == 1 )
        {
            while( descentFinish == true )
            {
                descentRando = 3 + arand( 4 );
                if( descentDonjon[ descentRando ][ descentAlea ] == 0 )
                {
                    descentDonjon[ descentRando ][ descentAlea ] = 2;
                    descentFin = true;
                    descentFinish = false;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Item.ino
// -----------------------------------------------------------------------------

void descentInitItem( int x, int y )
{
    descentAlea = arand( 6 );
    if( descentAlea == 1 )
    {
        int i;
        for( i = 0; i < MAX_MONSTER; i++ )
        {
            if( descentItem[ i ].on == false )
            {
                descentItem[ i ].on = true;
                descentAlea = arand( 6 );
                if( descentAlea == 3 || descentAlea == 4 || descentAlea == 5 )
                  descentAlea = 0;
                else if( descentAlea == 6 ) // real dead code - arand(6) never returns 6, see header comment
                  descentAlea = 1;
                if( descentPlayer.heal == 5 && descentAlea == 2 )
                  descentAlea = 0;
                descentItem[ i ].type = descentAlea;
                descentItem[ i ].x = x;
                descentItem[ i ].y = y;
                break;
            }
        }
    }
}

void descentUpdateItem()
{
    int i;
    for( i = 0; i < MAX_MONSTER; i++ )
    {
        if( gbCollideBitmapBitmap( descentItem[ i ].x, descentItem[ i ].y, descentItemSprites[ descentItem[ i ].type ], descentPlayer.x, descentPlayer.y, descentPlayerSprites[ descentPlayer.orientation ] ) && descentItem[ i ].on == true )
        {
            gbPlayOK();
            if( descentItem[ i ].type == 0 )
            {
                descentPlayer.rate = descentPlayer.rate + 1;
                descentItem[ i ].on = false;
            }
            else if( descentItem[ i ].type == 1 )
            {
                descentPlayer.damage = descentPlayer.damage + 1;
                descentItem[ i ].on = false;
            }
            else if( descentItem[ i ].type == 2 )
            {
                descentPlayer.heal = descentPlayer.heal + 1;
                descentItem[ i ].on = false;
            }
        }
    }
}

void descentDrawItem()
{
    int i;
    for( i = 0; i < MAX_MONSTER; i++ )
      if( descentItem[ i ].on == true )
        gbDrawBitmap( descentItem[ i ].x, descentItem[ i ].y, descentItemSprites[ descentItem[ i ].type ] );
}

// -----------------------------------------------------------------------------
// Monster.ino
// -----------------------------------------------------------------------------

void descentInitMonster()
{
    descentPlayer.score = descentPlayer.score + 2;
    descentAlea = arand( MAX_MONSTER );

    int j;
    for( j = 0; j < descentAlea; j++ )
    {
        int i;
        for( i = 0; i < MAX_MONSTER; i++ )
        {
            if( descentMonster[ i ].on == false )
            {
                descentMonster[ i ].on = true;
                descentMonster[ i ].type = arand( NUM_MONSTER );

                bool placeOk;
                do
                {
                    placeOk = true;
                    descentMonster[ i ].x = 8 + arand( 67 );
                    descentMonster[ i ].y = 8 + arand( 27 );
                    descentMonster[ i ].vx = 1;
                    descentMonster[ i ].vy = 1;
                    int k;
                    for( k = 0; k < MAX_MONSTER; k++ )
                      if( k != i && descentMonster[ k ].on == true && gbCollideBitmapBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ], (int)descentMonster[ k ].x, (int)descentMonster[ k ].y, descentMonsterSprites[ descentMonster[ k ].type ] ) )
                        placeOk = false;
                }
                while( placeOk == false ); // fixed, not preserved - see header comment for the real `while(place_ok=false)` assignment-typo bug this replaces (an overlapping monster spawn was never actually retried)

                descentMonster[ i ].heal = descentMonsterType[ descentMonster[ i ].type ][ 0 ];
                descentMonster[ i ].damage = descentMonsterType[ descentMonster[ i ].type ][ 1 ];
                descentMonster[ i ].time_step = descentMonsterType[ descentMonster[ i ].type ][ 2 ];

                if( descentMonster[ i ].type == 1 )
                {
                    do
                    {
                        descentMonster[ i ].vx = -1 + arand( 2 ); // real random(-1,1) - only ever -1 or 0, see header comment
                        descentMonster[ i ].vy = -1 + arand( 2 );
                    }
                    while( descentMonster[ i ].vx == 0 && descentMonster[ i ].vy == 0 );
                }
                else if( descentMonster[ i ].type == 2 )
                {
                    descentAlea = arand( 2 );
                    if( descentAlea == 0 )
                    {
                        descentMonster[ i ].vx = 8 + arand( 67 );
                        descentMonster[ i ].vy = (int)descentMonster[ i ].y;
                    }
                    else
                    {
                        descentMonster[ i ].vy = 8 + arand( 27 );
                        descentMonster[ i ].vx = (int)descentMonster[ i ].x;
                    }
                }
                descentWait = 20;
                break;
            }
        }
    }
}

void descentUpdateMonster()
{
    if( descentWait != 0 )
      descentWait = descentWait - 1;

    int i;
    for( i = 0; i < MAX_MONSTER; i++ )
    {
        if( descentMonster[ i ].on == true )
        {
            if( descentMonster[ i ].time_step > 0 )
              descentMonster[ i ].time_step = descentMonster[ i ].time_step - 1;

            if( gbCollideBitmapBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ], descentPlayer.x, descentPlayer.y, descentPlayerSprites[ descentPlayer.orientation ] ) )
            {
                // touching the player - upstream's own real empty branch (no movement while touching), preserved
            }
            else if( descentMonster[ i ].time_step == 0 )
            {
                if( descentMonster[ i ].type == 0 )
                {
                    if( descentMonster[ i ].x > descentPlayer.x ) descentMonster[ i ].x = descentMonster[ i ].x - 1;
                    else descentMonster[ i ].x = descentMonster[ i ].x + 1;
                    if( descentMonster[ i ].y > descentPlayer.y ) descentMonster[ i ].y = descentMonster[ i ].y - 1;
                    else descentMonster[ i ].y = descentMonster[ i ].y + 1;
                }
                else if( descentMonster[ i ].type == 1 )
                {
                    descentMonster[ i ].x = descentMonster[ i ].x + descentMonster[ i ].vx;
                    descentMonster[ i ].y = descentMonster[ i ].y + descentMonster[ i ].vy;
                    if( descentMonster[ i ].y < 4 ) descentMonster[ i ].vy = -descentMonster[ i ].vy;
                    if( descentMonster[ i ].y > 36 ) descentMonster[ i ].vy = -descentMonster[ i ].vy;
                    if( descentMonster[ i ].x < 7 ) descentMonster[ i ].vx = -descentMonster[ i ].vx;
                    if( descentMonster[ i ].x > 72 ) descentMonster[ i ].vx = -descentMonster[ i ].vx;
                }
                else if( descentMonster[ i ].type == 2 )
                {
                    int j;
                    if( descentMonster[ i ].vx > descentMonster[ i ].x )
                    {
                        descentMonster[ i ].x = descentMonster[ i ].x + 1;
                        for( j = 0; j < MAX_MONSTER; j++ )
                          if( j != i && descentMonster[ j ].on == true && gbCollideBitmapBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ], (int)descentMonster[ j ].x, (int)descentMonster[ j ].y, descentMonsterSprites[ descentMonster[ j ].type ] ) )
                            descentMonster[ i ].x = descentMonster[ i ].x - 1;
                    }
                    if( descentMonster[ i ].vx < descentMonster[ i ].x )
                    {
                        descentMonster[ i ].x = descentMonster[ i ].x - 1;
                        for( j = 0; j < MAX_MONSTER; j++ )
                          if( j != i && descentMonster[ j ].on == true && gbCollideBitmapBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ], (int)descentMonster[ j ].x, (int)descentMonster[ j ].y, descentMonsterSprites[ descentMonster[ j ].type ] ) )
                            descentMonster[ i ].x = descentMonster[ i ].x + 1;
                    }
                    if( descentMonster[ i ].vy < descentMonster[ i ].y )
                    {
                        descentMonster[ i ].y = descentMonster[ i ].y - 1;
                        for( j = 0; j < MAX_MONSTER; j++ )
                          if( j != i && descentMonster[ j ].on == true && gbCollideBitmapBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ], (int)descentMonster[ j ].x, (int)descentMonster[ j ].y, descentMonsterSprites[ descentMonster[ j ].type ] ) )
                            descentMonster[ i ].y = descentMonster[ i ].y + 1;
                    }
                    if( descentMonster[ i ].vy > descentMonster[ i ].y )
                    {
                        descentMonster[ i ].y = descentMonster[ i ].y + 1;
                        for( j = 0; j < MAX_MONSTER; j++ )
                          if( j != i && descentMonster[ j ].on == true && gbCollideBitmapBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ], (int)descentMonster[ j ].x, (int)descentMonster[ j ].y, descentMonsterSprites[ descentMonster[ j ].type ] ) )
                            descentMonster[ i ].y = descentMonster[ i ].y - 1;
                    }
                    if( descentMonster[ i ].vx == descentMonster[ i ].x && descentMonster[ i ].vy == descentMonster[ i ].y )
                    {
                        descentAlea = arand( 2 );
                        if( descentAlea == 0 )
                        {
                            descentMonster[ i ].vx = 8 + arand( 64 ); // real random(8,72) - different range than initMonster()'s own, see header comment
                            descentMonster[ i ].vy = (int)descentMonster[ i ].y;
                        }
                        else
                        {
                            descentMonster[ i ].vy = 4 + arand( 32 ); // real random(4,36) - different range than initMonster()'s own, see header comment
                            descentMonster[ i ].vx = (int)descentMonster[ i ].x;
                        }
                    }
                }
                else if( descentMonster[ i ].type == 3 )
                {
                    if( fabs( descentMonster[ i ].x - descentPlayer.x ) > fabs( descentMonster[ i ].y - descentPlayer.y ) )
                    {
                        if( descentMonster[ i ].x > descentPlayer.x ) descentMonster[ i ].x = descentMonster[ i ].x - 1;
                        else descentMonster[ i ].x = descentMonster[ i ].x + 1;
                    }
                    else
                    {
                        if( descentMonster[ i ].y > descentPlayer.y ) descentMonster[ i ].y = descentMonster[ i ].y - 1;
                        else descentMonster[ i ].y = descentMonster[ i ].y + 1;
                    }
                }
                else if( descentMonster[ i ].type == 4 )
                {
                    if( descentMonster[ i ].x < 7 )
                    {
                        descentMonster[ i ].x = 7;
                        descentMonster[ i ].vy = arand( 361 );
                    }
                    else if( descentMonster[ i ].x > 69 )
                    {
                        descentMonster[ i ].x = 69;
                        descentMonster[ i ].vy = arand( 361 );
                    }
                    else if( descentMonster[ i ].y < 4 )
                    {
                        descentMonster[ i ].y = 4;
                        descentMonster[ i ].vy = arand( 361 );
                    }
                    else if( descentMonster[ i ].y > 33 )
                    {
                        descentMonster[ i ].y = 33;
                        descentMonster[ i ].vy = arand( 361 );
                    }
                    descentMonster[ i ].vx = 1;
                    descentMonster[ i ].x = descentMonster[ i ].x + descentMonster[ i ].vx * cos( descentMonster[ i ].vy );
                    descentMonster[ i ].y = descentMonster[ i ].y + descentMonster[ i ].vx * sin( descentMonster[ i ].vy );
                    // real `int(monster[i].x); int(monster[i].y);` dropped here - see header comment (provable no-op)
                }
                descentMonster[ i ].time_step = descentMonsterType[ descentMonster[ i ].type ][ 2 ];
            }

            if( descentMonster[ i ].heal <= 0 )
            {
                descentMonster[ i ].on = false;
                descentPlayer.score = descentPlayer.score + descentMonsterType[ descentMonster[ i ].type ][ 2 ];
                descentInitItem( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y );
            }
        }
    }
}

void descentDrawMonster()
{
    int i;
    for( i = 0; i < MAX_MONSTER; i++ )
      if( descentMonster[ i ].on == true )
        gbDrawBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ] );
}

// -----------------------------------------------------------------------------
// Shoot.ino
// -----------------------------------------------------------------------------

void descentInitShoot()
{
    gbPlayTick();
    int i;
    for( i = 0; i < MAX_SHOOT; i++ )
    {
        if( descentShoot[ i ].on == false )
        {
            descentShoot[ i ].on = true;
            descentShoot[ i ].vx = 3;
            descentShoot[ i ].vy = 3;
            descentShoot[ i ].orientation = descentPlayer.orientation;
            if( descentShoot[ i ].orientation == 0 )
            {
                descentShoot[ i ].x = descentPlayer.x + 1;
                descentShoot[ i ].y = descentPlayer.y - 2;
            }
            else if( descentShoot[ i ].orientation == 1 )
            {
                descentShoot[ i ].x = descentPlayer.x + 5;
                descentShoot[ i ].y = descentPlayer.y + 2;
            }
            else if( descentShoot[ i ].orientation == 2 )
            {
                descentShoot[ i ].x = descentPlayer.x + 1;
                descentShoot[ i ].y = descentPlayer.y + 6;
            }
            else if( descentShoot[ i ].orientation == 3 )
            {
                descentShoot[ i ].x = descentPlayer.x - 2;
                descentShoot[ i ].y = descentPlayer.y + 2;
            }
            break;
        }
    }
}

void descentUpdateShoot()
{
    int i, j;
    for( i = 0; i < MAX_SHOOT; i++ )
    {
        if( descentShoot[ i ].on == true )
        {
            if( descentShoot[ i ].orientation == 0 ) descentShoot[ i ].y = descentShoot[ i ].y - descentShoot[ i ].vy;
            else if( descentShoot[ i ].orientation == 1 ) descentShoot[ i ].x = descentShoot[ i ].x + descentShoot[ i ].vx;
            else if( descentShoot[ i ].orientation == 2 ) descentShoot[ i ].y = descentShoot[ i ].y + descentShoot[ i ].vy;
            else if( descentShoot[ i ].orientation == 3 ) descentShoot[ i ].x = descentShoot[ i ].x - descentShoot[ i ].vx;

            if( descentShoot[ i ].x > 78 || descentShoot[ i ].x < 6 || descentShoot[ i ].y > 41 || descentShoot[ i ].y < 2 )
              descentShoot[ i ].on = false;

            for( j = 0; j < MAX_MONSTER; j++ )
            {
                if( descentMonster[ j ].on == true )
                {
                    if( gbCollideBitmapBitmap( (int)descentMonster[ j ].x, (int)descentMonster[ j ].y, descentMonsterSprites[ descentMonster[ j ].type ], descentShoot[ i ].x, descentShoot[ i ].y, descentBulletBitmap ) )
                    {
                        gbSetColor( GB_INVERT );
                        gbDrawBitmap( (int)descentMonster[ j ].x, (int)descentMonster[ j ].y, descentMonsterSprites[ descentMonster[ j ].type ] );
                        gbSetColor( GB_BLACK );
                        descentMonster[ j ].heal = descentMonster[ j ].heal - descentPlayer.damage;
                        descentShoot[ i ].on = false;
                    }
                }
            }
        }
    }
}

void descentDrawShoot()
{
    int i;
    for( i = 0; i < MAX_SHOOT; i++ )
      if( descentShoot[ i ].on == true )
        gbDrawBitmap( descentShoot[ i ].x, descentShoot[ i ].y, descentBulletBitmap );
}

// -----------------------------------------------------------------------------
// Hightscore.ino - real EEPROM.read()/write() calls, wired to this shim's
// own real eeprom_read_byte()/eeprom_write_byte() - see header comment
// (name storage dropped, a documented pre-established scope limit).
// -----------------------------------------------------------------------------

void descentLoadHighscore()
{
    int i;
    for( i = 0; i < RANKMAX; i++ )
    {
        int lsb = eeprom_read_byte( i * 2 );
        int msb = eeprom_read_byte( i * 2 + 1 );
        descentHighscore[ i ] = ( lsb & 0xFF ) + ( ( msb << 8 ) & 0xFF00 );
        if( descentHighscore[ i ] == 0xFFFF ) // real fresh-EEPROM sentinel, genuinely meaningful here - see header comment
          descentHighscore[ i ] = 0;
    }
}

void descentApplyNewHighscore( int score )
{
    descentHighscore[ RANKMAX - 1 ] = score;

    int i;
    for( i = RANKMAX - 1; i > 0; i-- )
    {
        if( descentHighscore[ i - 1 ] < descentHighscore[ i ] )
        {
            int temp = descentHighscore[ i - 1 ];
            descentHighscore[ i - 1 ] = descentHighscore[ i ];
            descentHighscore[ i ] = temp;
        }
        else break;
    }

    for( i = 0; i < RANKMAX; i++ )
    {
        eeprom_write_byte( i * 2, descentHighscore[ i ] & 0xFF );
        eeprom_write_byte( i * 2 + 1, ( descentHighscore[ i ] >> 8 ) & 0xFF );
    }
}

// -----------------------------------------------------------------------------
// DescentIntoHell.ino - init / level-down / draw / state machine
// -----------------------------------------------------------------------------

void descentInitPlayer()
{
    descentPlayer.x = 40;
    descentPlayer.y = 21;
    descentPlayer.vx = 2;
    descentPlayer.vy = 2;
    descentPlayer.donjonroom = 12;
    descentPlayer.orientation = 1;
    descentPlayer.heal = 5;
    descentPlayer.damage = 1;
    descentPlayer.rate = 1;
    descentPlayer.score = 0;
    descentPlayer.invul = 0;
}

// == upstream's real initGame(), minus gb.titleScreen() (moved to the
// explicit state machine - see header comment) and gb.battery.show=false
// (dropped, no shim equivalent, purely cosmetic on real hardware).
void descentInitGame()
{
    gbPickRandomSeed();
    gbFontSize = 1;
    int i;
    for( i = 0; i < MAX_SHOOT; i++ )
    {
        descentShoot[ i ].on = false;
        descentShoot[ i ].vx = 2;
        descentShoot[ i ].vy = 2;
    }
    descentInitDonjon();
    descentInitPlayer();
    descentDonjonLevel = 1;
}

void descentLevelDown()
{
    gbPickRandomSeed();
    gbPlayOK();
    descentDonjonLevel = descentDonjonLevel + 1;
    descentPlayer.x = 42;
    descentPlayer.y = 24;
    descentPlayer.vx = 2;
    descentPlayer.vy = 2;
    descentPlayer.donjonroom = 12;
    descentPlayer.orientation = 1;
    descentPlayer.score = descentPlayer.score + 100;

    int i;
    for( i = 0; i < MAX_SHOOT; i++ )
      descentShoot[ i ].on = false;

    // Real upstream `if(donjonlevel%2==1){ for(...) monstertype[NUM_MONSTER][0]+=1; }`
    // is dropped here - a genuine, confirmed out-of-bounds array write even
    // on real hardware that never actually reaches any monster type's own
    // stats either way - see header comment.

    for( i = 0; i < 25; i++ )
    {
        int j;
        for( j = 3; j < 7; j++ )
        {
            descentDonjon[ j ][ i ] = 0;
            descentDonjon[ 0 ][ i ] = i;
            descentDonjon[ 7 ][ i ] = 0;
        }
    }
    descentDonjon[ 7 ][ 12 ] = 1;
    descentOk = 0;
    descentFinish = false;
    descentFin = false;
    descentInitDonjon();
    descentPopupTimer = 30; // real gb.popup(F("Level Down"), 30)
}

// == upstream's own real drawGame() - see header comment (gap 3) on why
// EVERY draw below this point (health icons, doors, barriers, stairs) uses
// real INVERT mode, not just the health icons.
void descentDrawGame()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 0, 0, descentTerrainBitmap );

    gbSetColor( GB_INVERT );
    int i;
    for( i = 0; i < descentPlayer.heal; i++ )
      gbDrawBitmap( 4 + i * 7, 0, descentItemHealBitmap );

    if( descentDonjon[ 3 ][ descentPlayer.donjonroom ] == 1 )
    {
        gbDrawBitmap( 37, 1, descentDoorUpBitmap );
        if( descentOk == 1 )
          gbDrawBitmap( 39, 4, descentBarrierHBitmap );
    }
    if( descentDonjon[ 4 ][ descentPlayer.donjonroom ] == 1 )
    {
        gbDrawBitmap( 77, 19, descentDoorRightBitmap );
        if( descentOk == 1 )
          gbDrawBitmap( 77, 21, descentBarrierVBitmap );
    }
    if( descentDonjon[ 5 ][ descentPlayer.donjonroom ] == 1 )
    {
        gbDrawBitmap( 37, 41, descentDoorDownBitmap );
        if( descentOk == 1 )
          gbDrawBitmap( 39, 41, descentBarrierHBitmap );
    }
    if( descentDonjon[ 6 ][ descentPlayer.donjonroom ] == 1 )
    {
        gbDrawBitmap( 1, 19, descentDoorLeftBitmap );
        if( descentOk == 1 )
          gbDrawBitmap( 4, 21, descentBarrierVBitmap );
    }
    if( descentDonjon[ 3 ][ descentPlayer.donjonroom ] == 2 )
      gbDrawBitmap( 36, 0, descentStairsUpBitmap );
    if( descentDonjon[ 4 ][ descentPlayer.donjonroom ] == 2 )
      gbDrawBitmap( 77, 18, descentStairsRightBitmap );
    if( descentDonjon[ 5 ][ descentPlayer.donjonroom ] == 2 )
      gbDrawBitmap( 36, 41, descentStairsDownBitmap );
    if( descentDonjon[ 6 ][ descentPlayer.donjonroom ] == 2 )
      gbDrawBitmap( 0, 18, descentStairsLeftBitmap );

    gbSetColor( GB_BLACK );
}

void descentDrawPopup()
{
    gbSetColor( GB_BLACK );
    gbFillRect( 20, 18, 44, 10 );
    gbSetColor( GB_WHITE );
    gbCursorX = 23;
    gbCursorY = 20;
    gbFontSize = 1;
    gbPrintString( "Level Down" );
    gbSetColor( GB_BLACK );
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void descentBeginTitle()
{
    descentState = DESCENT_STATE_TITLE;
}

// == real gb.titleScreen(F("Descent into Hell"), logo) - see header comment.
void descentUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 21;
    gbCursorY = 0;
    gbPrintString( "PRESS A" );
    gbCursorX = 0;
    gbCursorY = 12;
    gbPrintString( "Descent into Hell" );
    gbDrawBitmap( 0, 18, descentLogoBitmap );

    if( gbPressed( BTN_A ) )
    {
        if( descentPlayIntro )
        {
            descentState = DESCENT_STATE_INTRO;
            descentImage = 0;
            gbSetFrameRate( 10 );
        }
        else
          descentState = DESCENT_STATE_PLAY;
    }
}

// == real Intro.ino's own Intro() - a 20-tick, 2-frame, 10fps boot
// animation, then switches to 20fps and enters gameplay.
void descentUpdateIntro()
{
    if( descentImage < 10 )
      gbDrawBitmap( 0, 0, descentIntro1Bitmap );
    else
      gbDrawBitmap( 0, 0, descentIntro23Bitmap );

    if( descentImage >= 20 )
    {
        descentPlayIntro = false;
        gbSetFrameRate( 20 );
        descentState = DESCENT_STATE_PLAY;
    }

    descentImage = descentImage + 1;
}

// fromGameOver selects between real GameOver()'s own saveHighscore() path
// (font5x7, returns to a fully reset title screen) and Map()'s own
// mid-game "peek" path (font left as-is, returns to Map) - see header
// comment. Defined here, before its own two real call sites
// (descentUpdateMap()/descentUpdateGameOverMsg() below), matching this
// project's established "define every begin*() helper before its own
// first caller" discipline (this dialect's support for calling a function
// defined later in the same file is unconfirmed - see gameUfoRace.c's own
// identical ordering).
void descentBeginHighscore( bool fromGameOver )
{
    descentHighscoreFromGameOver = fromGameOver;
    if( fromGameOver )
      gbSetFont( gbFont5x7 ); // real upstream setFont(font5x7), right before the (dropped) keyboard widget
    descentState = DESCENT_STATE_HIGHSCORE;
}

// == real loop()'s own main gameplay branch (playintro==false).
void descentUpdatePlay()
{
    descentOk = 0;
    int i;
    for( i = 0; i < MAX_MONSTER; i++ )
    {
        if( descentMonster[ i ].on == true )
        {
            if( gbCollideBitmapBitmap( (int)descentMonster[ i ].x, (int)descentMonster[ i ].y, descentMonsterSprites[ descentMonster[ i ].type ], descentPlayer.x, descentPlayer.y, descentPlayerSprites[ descentPlayer.orientation ] ) )
            {
                if( descentPlayer.invul <= 0 && descentWait == 0 )
                {
                    descentPlayer.heal = descentPlayer.heal - descentMonster[ i ].damage;
                    descentPlayer.invul = 20;
                }
            }
            descentOk = 1;
        }
        if( descentItem[ i ].on == true )
          descentOk = 1;
    }

    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        descentPlayer.x = descentPlayer.x + descentPlayer.vx;
        if( gbRepeat( BTN_A, 1 ) ) {}
        else descentPlayer.orientation = 1;
    }
    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        descentPlayer.x = descentPlayer.x - descentPlayer.vx;
        if( gbRepeat( BTN_A, 1 ) ) {}
        else descentPlayer.orientation = 3;
    }
    if( gbRepeat( BTN_DOWN, 1 ) )
    {
        descentPlayer.y = descentPlayer.y + descentPlayer.vy;
        if( gbRepeat( BTN_A, 1 ) ) {}
        else descentPlayer.orientation = 2;
    }
    if( gbRepeat( BTN_UP, 1 ) )
    {
        descentPlayer.y = descentPlayer.y - descentPlayer.vy;
        if( gbRepeat( BTN_A, 1 ) ) {}
        else descentPlayer.orientation = 0;
    }
    if( gbRepeat( BTN_A, 1 ) )
    {
        if( descentShootDelay <= 0 )
        {
            descentShootDelay = 15 - ( descentPlayer.rate * 10 / 50 );
            descentInitShoot();
        }
    }
    if( descentShootDelay > 0 )
      descentShootDelay = descentShootDelay - 1;

    if( gbPressed( BTN_C ) )
    {
        // == real Map() - a nested blocking call on real hardware that fully
        // takes over rendering until dismissed - converted to an explicit
        // state, so bail out of this tick's own remaining PLAY logic now.
        descentState = DESCENT_STATE_MAP;
        return;
    }

    if( descentPlayer.y < 2 && descentPlayer.x > 37 && descentPlayer.x < 42 )
    {
        if( descentDonjon[ 3 ][ descentPlayer.donjonroom ] == 1 && descentOk == 0 )
        {
            descentPlayer.donjonroom = descentPlayer.donjonroom - 5;
            for( i = 0; i < MAX_MONSTER; i++ )
              descentItem[ i ].on = false;
            if( descentDonjon[ 7 ][ descentPlayer.donjonroom ] == 0 )
              descentInitMonster();
            descentDonjon[ 7 ][ descentPlayer.donjonroom ] = 1;
            descentPlayer.y = 35;
            for( i = 0; i < MAX_SHOOT; i++ )
              descentShoot[ i ].on = false;
        }
        else if( descentDonjon[ 3 ][ descentPlayer.donjonroom ] == 2 && descentOk == 0 )
          descentLevelDown();
    }
    if( descentPlayer.y > 35 && descentPlayer.x > 37 && descentPlayer.x < 42 )
    {
        if( descentDonjon[ 5 ][ descentPlayer.donjonroom ] == 1 && descentOk == 0 )
        {
            descentPlayer.donjonroom = descentPlayer.donjonroom + 5;
            if( descentDonjon[ 7 ][ descentPlayer.donjonroom ] == 0 )
              descentInitMonster();
            descentDonjon[ 7 ][ descentPlayer.donjonroom ] = 1;
            descentPlayer.y = 7;
            for( i = 0; i < MAX_SHOOT; i++ )
              descentShoot[ i ].on = false;
        }
        else if( descentDonjon[ 5 ][ descentPlayer.donjonroom ] == 2 && descentOk == 0 )
          descentLevelDown();
    }
    if( descentPlayer.x < 7 && descentPlayer.y < 23 && descentPlayer.y > 19 )
    {
        if( descentDonjon[ 6 ][ descentPlayer.donjonroom ] == 1 && descentOk == 0 )
        {
            descentPlayer.donjonroom = descentPlayer.donjonroom - 1;
            if( descentDonjon[ 7 ][ descentPlayer.donjonroom ] == 0 )
              descentInitMonster();
            descentDonjon[ 7 ][ descentPlayer.donjonroom ] = 1;
            descentPlayer.x = 72;
            for( i = 0; i < MAX_SHOOT; i++ )
              descentShoot[ i ].on = false;
        }
        else if( descentDonjon[ 6 ][ descentPlayer.donjonroom ] == 2 && descentOk == 0 )
          descentLevelDown();
    }
    if( descentPlayer.x > 72 && descentPlayer.y < 23 && descentPlayer.y > 19 )
    {
        if( descentDonjon[ 4 ][ descentPlayer.donjonroom ] == 1 && descentOk == 0 )
        {
            descentPlayer.donjonroom = descentPlayer.donjonroom + 1;
            if( descentDonjon[ 7 ][ descentPlayer.donjonroom ] == 0 )
              descentInitMonster();
            descentDonjon[ 7 ][ descentPlayer.donjonroom ] = 1;
            descentPlayer.x = 7;
            for( i = 0; i < MAX_SHOOT; i++ )
              descentShoot[ i ].on = false;
        }
        else if( descentDonjon[ 4 ][ descentPlayer.donjonroom ] == 2 && descentOk == 0 )
          descentLevelDown();
    }

    if( descentPlayer.y < 2 ) descentPlayer.y = 2;
    if( descentPlayer.y > 35 ) descentPlayer.y = 35;
    if( descentPlayer.x < 7 ) descentPlayer.x = 7;
    if( descentPlayer.x > 72 ) descentPlayer.x = 72;

    if( descentPlayer.heal <= 0 )
    {
        // == real blocking Game-Over while(true) loop -> explicit state (see
        // header comment on the one harmless extra frame this drops).
        descentState = DESCENT_STATE_GAMEOVER_MSG;
        return;
    }

    descentDrawGame();
    descentDrawItem();
    descentDrawMonster();
    gbDrawBitmap( descentPlayer.x, descentPlayer.y, descentPlayerSprites[ descentPlayer.orientation ] );
    if( descentPlayer.invul > 0 )
    {
        descentPlayer.invul = descentPlayer.invul - 1;
        gbSetColor( GB_INVERT );
        if( descentPlayer.invul % 2 == 1 )
        {
            gbDrawBitmap( descentPlayer.x, descentPlayer.y, descentPlayerSprites[ descentPlayer.orientation ] );
            gbSetColor( GB_BLACK );
        }
        // NOTE: on an EVEN invul tick, the color is deliberately left at
        // GB_INVERT here (see header comment) - descentDrawShoot() below
        // then draws this tick's own bullets in real XOR mode instead of
        // solid BLACK, a genuine preserved upstream bug.
    }
    descentDrawShoot();
    descentUpdateShoot();
    descentUpdateItem();
    descentUpdateMonster();

    if( descentPopupTimer > 0 )
    {
        descentPopupTimer = descentPopupTimer - 1;
        descentDrawPopup();
    }
}

// == real Map()'s own Button-B "restart" (calls initGame() directly, not
// GameOver()) - see header comment on the real finish/fin/playintro quirks
// this preserves.
void descentMapRestart()
{
    descentInitGame();
    descentBeginTitle();
}

// == real Map() - a blocking while(true) pause/status screen on real
// hardware, converted to an explicit state (see header comment).
void descentUpdateMap()
{
    descentOk = 0;
    int i;
    for( i = 0; i < 25; i++ )
    {
        if( descentDonjon[ 7 ][ i ] == 1 )
        {
            gbSetColor( GB_BLACK );
            gbFillRect( descentDonjon[ 1 ][ i ] * 9 + 1, descentDonjon[ 2 ][ i ] * 9, 9, 9 );
            gbSetColor( GB_WHITE );
            if( descentDonjon[ 3 ][ i ] == 1 )
              gbFillRect( descentDonjon[ 1 ][ i ] * 9 + 4, descentDonjon[ 2 ][ i ] * 9, 3, 3 );
            else if( descentDonjon[ 3 ][ i ] == 2 )
              descentOk = 1;
            if( descentDonjon[ 4 ][ i ] == 1 )
              gbFillRect( descentDonjon[ 1 ][ i ] * 9 + 7, descentDonjon[ 2 ][ i ] * 9 + 3, 3, 3 );
            else if( descentDonjon[ 4 ][ i ] == 2 )
              descentOk = 1;
            if( descentDonjon[ 5 ][ i ] == 1 )
              gbFillRect( descentDonjon[ 1 ][ i ] * 9 + 4, descentDonjon[ 2 ][ i ] * 9 + 6, 3, 3 );
            else if( descentDonjon[ 5 ][ i ] == 2 )
              descentOk = 1;
            if( descentDonjon[ 6 ][ i ] == 1 )
              gbFillRect( descentDonjon[ 1 ][ i ] * 9 + 1, descentDonjon[ 2 ][ i ] * 9 + 3, 3, 3 );
            else if( descentDonjon[ 6 ][ i ] == 2 )
              descentOk = 1;
            gbFillRect( descentDonjon[ 1 ][ i ] * 9 + 2, descentDonjon[ 2 ][ i ] * 9 + 1, 7, 7 );
            gbSetColor( GB_BLACK );
            if( descentOk == 1 )
            {
                gbDrawBitmap( descentDonjon[ 1 ][ i ] * 9 + 2, descentDonjon[ 2 ][ i ] * 9 + 1, descentMapExitBitmap );
                descentOk = 0;
            }
            if( i == descentPlayer.donjonroom )
            {
                gbSetColor( GB_WHITE );
                gbFillRect( descentDonjon[ 1 ][ i ] * 9 + 2, descentDonjon[ 2 ][ i ] * 9 + 1, 7, 7 );
                gbSetColor( GB_BLACK );
                gbDrawBitmap( descentDonjon[ 1 ][ i ] * 9 + 2, descentDonjon[ 2 ][ i ] * 9 + 1, descentMapPlayerBitmap );
            }
        }
    }

    gbCursorY = 0;
    gbCursorX = 50;
    gbFontSize = 1;
    gbPrintString( "Level" );
    gbCursorX = 75;
    gbPrintNumber( descentDonjonLevel );
    gbCursorY = 6;
    gbCursorX = 47;
    gbPrintString( "S" );
    gbCursorX = 55;
    gbPrintNumber( descentPlayer.score );
    gbDrawBitmap( 47, 14, descentItemRateBitmap );
    gbCursorY = 12;
    gbCursorX = 55;
    gbPrintNumber( descentPlayer.rate );
    gbDrawBitmap( 47, 19, descentItemDamageBitmap );
    gbCursorY = 18;
    gbCursorX = 55;
    gbPrintNumber( descentPlayer.damage );
    gbCursorY = 26;
    gbCursorX = 47;
    gbPrintString( descentMapReturnText );
    gbCursorY = 33;
    gbCursorX = 47;
    gbPrintString( descentMapScoreText );
    gbCursorY = 40;
    gbCursorX = 47;
    gbPrintString( descentMapQuitText );

    if( gbPressed( BTN_C ) )
    {
        gbSetColor( GB_BLACK );
        descentState = DESCENT_STATE_PLAY;
    }
    else if( gbPressed( BTN_A ) )
    {
        gbSetColor( GB_BLACK );
        descentHighscoreFromGameOver = false;
        descentBeginHighscore( false );
    }
    else if( gbPressed( BTN_B ) )
      descentMapRestart();
}

void descentUpdateGameOverMsg()
{
    gbSetColor( GB_BLACK );
    gbCursorY = 10;
    gbCursorX = 0;
    gbFontSize = 2;
    gbPrintString( "Game Over !" );
    gbCursorY = 32;
    gbFontSize = 1;
    gbCursorX = 16;
    gbPrintString( descentHightScoreText );

    if( gbPressed( BTN_C ) )
    {
        // == real GameOver()'s own real saveHighscore() call - the
        // name-entry half is dropped, see header comment.
        descentApplyNewHighscore( descentPlayer.score );
        descentBeginHighscore( true );
    }
}

// == real GameOver()'s own post-saveHighscore() reset, then a fresh
// initGame() - see header comment on the real finish/fin quirk this
// (correctly) DOES reset, unlike descentMapRestart() above.
void descentFinishGameOver()
{
    descentOk = 0;
    descentFinish = false;
    descentFin = false;
    int i;
    for( i = 0; i < 25; i++ )
    {
        descentDonjon[ 0 ][ i ] = i;
        descentDonjon[ 3 ][ i ] = 0;
        descentDonjon[ 4 ][ i ] = 0;
        descentDonjon[ 5 ][ i ] = 0;
        descentDonjon[ 6 ][ i ] = 0;
        descentDonjon[ 7 ][ i ] = 0;
    }
    descentDonjon[ 7 ][ 12 ] = 1;
    for( i = 0; i < MAX_MONSTER; i++ )
      descentMonster[ i ].on = false;
    for( i = 0; i < MAX_MONSTER; i++ )
      descentItem[ i ].on = false;

    descentPlayIntro = true;
    gbSetFont( gbFont3x5 );
    gbFontSize = 1;

    descentInitGame();
    descentBeginTitle();
}

void descentUpdateHighscore()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 10;
    gbCursorY = 1;
    gbPrintString( "HIGH SCORES" );
    gbCursorX = 0;
    gbCursorY = gbFontHeight;

    int thisScore;
    for( thisScore = 0; thisScore < RANKMAX; thisScore++ )
    {
        gbCursorY = gbFontHeight + gbFontHeight * thisScore;
        gbCursorX = 0;
        if( descentHighscore[ thisScore ] == 0 )
          gbPrintString( "-" );
        gbCursorX = LCDWIDTH - 3 * gbFontWidth;
        gbPrintNumber( descentHighscore[ thisScore ] );
    }

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        if( descentHighscoreFromGameOver )
          descentFinishGameOver();
        else
          descentState = DESCENT_STATE_MAP;
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameDescent_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 ); // real hardware default - this game never switches font except transiently for the highscore screen, see header comment
    descentLoadHighscore();

    descentFinish = false;
    descentFin = false;
    descentWait = 0;
    descentShootDelay = 0;
    descentPlayIntro = true;

    descentInitGame();
    descentBeginTitle();
}

void gameDescent_update()
{
    if( !gbUpdate() ) return;

    if( descentState == DESCENT_STATE_TITLE ) descentUpdateTitle();
    else if( descentState == DESCENT_STATE_INTRO ) descentUpdateIntro();
    else if( descentState == DESCENT_STATE_PLAY ) descentUpdatePlay();
    else if( descentState == DESCENT_STATE_MAP ) descentUpdateMap();
    else if( descentState == DESCENT_STATE_GAMEOVER_MSG ) descentUpdateGameOverMsg();
    else descentUpdateHighscore();

    gbRenderFrame();
}
