#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Aerial-Assault (SkylarHylar, license: none specified -
// github.com/SkylarHylar/Strike-Down). A real Joust clone.
//
// THE REPO NAME IS NOT THE GAME NAME: the GitHub repository is called
// "Strike-Down" and its Arduino sketch folder/entry file are
// `StrikeDown/StrikeDown.ino`, but the repo's own README.md says, in full,
// "# Aerial-Assault / A Joust Clone for the Gamebuino!" - so the real title
// is Aerial-Assault, and that is what this game is registered under in
// menuGameList.c. The sketch itself never prints its own name anywhere (its
// only title-screen text is a single space), so the README is the only real
// naming evidence either way, and it is unambiguous.
//
// LICENSE: none. Confirmed by listing every file in the staged repository
// directly (`.gitattributes`, `.gitignore`, `README.md`, the prebuilt
// `STRIKE.elf`/`STRIKE.hex`, and `StrikeDown/`'s own three `.ino` tabs) -
// there is no LICENSE/COPYING file of any kind, and a grep for
// "licen"/"copyright"/"author" across all three source files and the README
// returns nothing at all. Registered as "None specified", matching this
// cartridge's own treatment of every other unlicensed port.
//
// Source: a real, standard multi-tab Arduino sketch, all three tabs read in
// full before porting - `StrikeDown.ino` (setup + the real `gb.menu()`
// driven loop: Survival / Status / Controls / Options / Change Game),
// `player.ino` (the entire game, one blocking `play()` function), and
// `sprites.ino` (13 PROGMEM bitmaps).
//
// -----------------------------------------------------------------------------
// The real mechanic, as this game actually implements it
// -----------------------------------------------------------------------------
// A fixed (non-scrolling) 84x48 arena of platforms. The player flaps upward
// one pixel at a time (Button A or B, each press subtracting 1 from the
// gravity accumulator) and walks/drifts left-right with a real -2..+2
// velocity, wrapping around both screen edges. Three enemy types share the
// arena, each spawned in a random count per stage:
//   * FLOATER - drifts horizontally at a constant 1px/tick, jittering
//     between two adjacent rows of its own randomly chosen "line" height.
//   * BOUNCER - accelerates toward the player in both axes (velocity capped
//     at 1px/tick each way) and, uniquely, does NOT kill on a level hit: it
//     knocks the player back and takes recoil itself.
//   * HUNTER - tracks the player's height with a gravity accumulator and
//     closes horizontally at a flat 1px/tick. Kills on a level hit.
// Plus a PTERODACTYL, a real timed hazard (it appears once `dactyltime`
// counts down to 0 and then flies across the screen forever, oscillating
// one pixel per tick toward the player's height, wrapping at both edges).
//
// The Joust rule itself, as actually written here, is a plain height
// comparison at collision time - NOT the arcade original's relative-height-
// of-the-mounts rule, and not a velocity comparison:
//   * player Y <= enemy Y - 1  ->  the player wins; the enemy is knocked
//     down-right by (+2,+2) and turns into an EGG, which then falls under
//     gravity until it lands on a platform.
//   * otherwise               ->  the player dies (loses a life), except
//     against the bouncer, which only shoves.
//   * player Y <= dactyl Y - 3 (a deeper 3px margin, not 1) -> the
//     pterodactyl is merely reset (`dactyltime = 450`), never killed.
// Touching a downed egg collects it: that enemy's remaining count drops by
// one and it respawns at the current spawn point. Clearing every enemy
// advances the stage, which progressively REMOVES platforms (the two
// bridges go at stage 3, the middle platform at 6, the left ledge at 9, the
// right ledge at 11) - the game's only real difficulty curve.
//
// NO HIGHSCORE, NO EEPROM: confirmed by grep across all three real source
// files - there is no `EEPROM.h` include, no `EEPROM.read`/`.write`, and no
// score variable of any kind. The only progress readout is the live stage
// number, shown during each stage's own rest period. Nothing was invented
// here, matching this project's own "don't invent a highscore concept real
// upstream never had" precedent.
//
// -----------------------------------------------------------------------------
// Dialect rewrites (this project's standing conventions)
// -----------------------------------------------------------------------------
// Every real `gb.x.y(...)` call site became a plain `gbY(...)` call (this
// dialect has no classes/methods - see gamePong.c's own header comment).
// `boolean` became `bool`. Array declarations use this dialect's required
// `TYPE[N] name` order. `random(min,max)` (Arduino's own EXCLUSIVE upper
// bound) became `min + arand(max - min)`; `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op. `gb.battery.show = ...` (a real
// hardware battery indicator, toggled in four places here) was dropped
// outright, matching gamePong.c. Global naming prefix: `aer`.
//
// All 13 real PROGMEM bitmaps are restored verbatim below, every
// `B00000000`-style Arduino binary literal mechanically converted to hex by
// a one-off script (this dialect has no binary-literal syntax) with each
// table's own byte count checked against its real `{width,height}` header
// first - all 13 exact, nothing redrawn or approximated.
//
// Real icon glyphs `\25`/`\26`/`\27` (octal - ASCII 21/22/23, the real
// Gamebuino A/B/C button icons) cannot be embedded in a plain quoted string
// literal here, so each line using one is an explicit `int[]` array - the
// established precedent from gameTaquin.c/gameSmash.c.
//
// SOUND: upstream plays exactly two real notes (a flap and a death), each
// on real channel 1, each followed by 2-3 raw `gb.sound.command(...)`
// tracker effects (a flap's own volume/instrument/tremolo slides; a
// death's own instrument/pitch-slide) - a real, faithful port of both via
// this shim's own `gbSoundCommand()`/`gbPlayNoteChannel()` primitives (a
// direct port of real Sound.h's own per-channel tracker commands),
// preserving upstream's own real, literal call order (the note plays
// first, the command() slides are issued right after it, not before).
// The 4 identical real death-sound sites (a repeated
// `playNote(1,28,1)`+2`command()`s+`alive=false;`+`lives--` block) are
// still consolidated into one `aerKillPlayer()` helper, called from all 4
// real sites.
//
// BLOCKING WIDGETS -> EXPLICIT STATES (gamePong.c's own established
// treatment, and gameSmash.c's precedent for this exact menu shape):
//   * `gb.titleScreen(F(" "), Title)` (setup) and `gb.titleScreen(Title)`
//     (the menu's own cancel path) -> AER_STATE_TITLE. Real
//     `Gamebuino::titleScreen()` prints the passed name at cursor (0,12)
//     and draws the logo at `(0, 12 + logoOffset)`, where `logoOffset` is
//     the active font's own real height when a name string is passed and 0
//     when it is not (the formula this project already established by
//     reading the real Gamebuino.cpp - see gameInvaders.c/gameSmash.c). The
//     two real call sites therefore differ: setup passes `" "` with font5x7
//     active (height 8) -> logo at (0,20), and the cancel path passes no
//     name at all -> logo at (0,12). Both are reproduced, carried by
//     `aerTitleLogoY`. The name string `" "` is a single invisible space,
//     so a "PRESS A" hint (this project's own established stand-in for the
//     real blinking A-icon prompt) fills that line instead. NOTE the 64x30
//     logo genuinely overflows the bottom of the screen by 2 rows in the
//     setup case (20+30 = 50 > 48) - real hardware clips it exactly the
//     same way, so it is left alone rather than nudged up.
//   * `gb.menu(menu, 5)` -> AER_STATE_MENU, a hand-rolled 5-item list with
//     a ">" cursor (gameDigger.c/gameSmash.c precedent - this shim has no
//     generic menu widget by design). Real `Gamebuino::menu()`'s own button
//     semantics are reproduced: A confirms (plays OK, returns the item),
//     B or C cancels (plays Cancel, returns -1 -> the title screen), and
//     the highlight resets to item 0 on every fresh entry, exactly like the
//     real widget's own `activeItem = 0`. Its slide-in animation is not
//     reproduced (cosmetic, no gameplay effect).
//   * `play()`'s own `while(true){ if(gb.update()){...} }`, and the three
//     `while(1)` screens behind menu items 1/2/3 -> AER_STATE_PLAY /
//     _STATUS / _CONTROLS / _OPTIONS. `pause` and the `lives <= 0` game
//     over are kept as upstream's own flags inside the play state rather
//     than split into further states, since upstream itself treats them as
//     sub-modes of one screen.
//   * `gb.changeGame()` (menu item 4 - a real "flash a different game off
//     the SD card" OS feature) has no Vircon32 equivalent, the same class
//     of real-hardware-only call already dropped for gamePirates' own
//     `load_game()`. This cartridge's real equivalent gesture is the global
//     Start-button quit dialog handled by portVircon32.c. The menu entry is
//     kept (so the real 5-item menu still reads as it does on hardware) and
//     selecting it returns to the title screen - the closest available
//     outcome, matching gameSmash.c's own identical decision.
//
// `gb.getCpuLoad()`/`gb.getFreeRam()` (the whole point of the Status
// screen) are real hardware introspection with no meaning on a virtual
// console. The screen is kept so the real menu still has five working
// entries, showing literal "N/A" placeholders rather than invented numbers
// - exactly what gameSmash.c already does for the same two calls.
//
// The real `gb.display.print(F("    Loading...."))` printed once before
// `play()` starts, with no delay before the next `gb.update()` overwrites
// it, was dropped - purely cosmetic, and whether it is ever visible for
// even one frame depends on real SPI/display-flip timing this project
// cannot reproduce (gameSmash.c set this precedent for the identical line).
//
// -----------------------------------------------------------------------------
// Real upstream bugs and quirks
// -----------------------------------------------------------------------------
// 1) **`case 2:` (Controls) has no `break;`** on real hardware, so backing
//    out of the Controls screen with Button B does not return to the menu -
//    it falls straight through into `case 3:` (Options) instead. Ported
//    faithfully at first, but fixed on direct user request once live
//    testing found it a genuine navigation annoyance, not just a curiosity:
//    `aerUpdateControls()`'s own B handler now calls `aerBeginMenu()`
//    directly, matching every other sub-screen's own B-handler shape
//    (`aerUpdateStatus()` right above it). The only quirk in this list that
//    is NOT reproduced bit-for-bit - every other one below still is.
//
// 2) **Four separate `wait == 0 || 10`-style conditions are always true.**
//    `(wait == 0 || 10)`, `(wait == 0 || 5 || 10 || 15)` parse as
//    `(wait == 0) || 10` etc - a bare non-zero constant as the right
//    operand, so the whole clause is unconditionally true and the intended
//    "only on these frames" throttle never happens at all. This is not
//    cosmetic: it is why the pterodactyl chases the player's height at a
//    full 1px per tick, why the floater moves every single tick rather than
//    every fifth, and why the floater's own row jitters every tick. All
//    four sites are written here as the unconditional code they really are,
//    each with the original condition quoted in a comment.
//
// 3) **`floaterleft = random(0,1)` can only ever produce 0.** Arduino's
//    `random(min,max)` has an exclusive upper bound, so this is a constant
//    `false` - the floater's entire leftward-movement branch (and its
//    NOFLIP sprite direction) is real, permanently dead code on hardware
//    too. Ported as `arand(1)`, which is 0 for exactly the same reason.
//
// 4) **`spawn = random(0,2)` never produces 2**, so the third spawn point
//    (76,11) is likewise unreachable. Kept anyway, byte for byte.
//
// 5) **The spawn point used when an egg is collected is one tick stale.**
//    Each egg-collect block re-rolls `spawn` and THEN reads `spawnx`/
//    `spawny` - but those two are only recomputed from `spawn` at the top
//    of the next tick, so the enemy actually respawns at the PREVIOUS
//    roll's coordinates. Preserved by keeping the exact statement order.
//
// 6) **The egg-collect tests run against every enemy every tick, whether
//    or not that enemy is currently an egg** - they test the egg bitmap at
//    the live enemy's own position. So a single collision can both kill the
//    player (via the enemy test) and consume/respawn that enemy (via the
//    egg test) on the same tick. Preserved by keeping the same test order.
//
// 7) **The lives readout is drawn inverted and one heart is misplaced.**
//    `if(lives < 1) drawBitmap(18,0,life);` - a heart at the TOP of the
//    screen, and each condition is `<` rather than `>=`, so hearts appear
//    as lives are LOST, not as they remain. Copied verbatim.
//
// 8) **The Pterodactyl option does nothing.** The Options screen toggles a
//    real `pterodactyl` boolean that `play()` never reads even once
//    (confirmed by grep across all three files) - the pterodactyl always
//    spawns regardless. The option is kept and still toggles the variable,
//    exactly as on hardware.
//
// 9) **The Options screen's UP/DOWN are inverted** (UP increments the
//    option index, DOWN decrements). Kept.
//
// 10) **Enemy/bridge sprites are drawn flipped but collided unflipped.**
//    Every `drawBitmap(..., NOROT, someflip)` has a matching
//    `collideBitmapBitmap(...)` with no flip argument at all - real
//    `Gamebuino::collideBitmapBitmap()` has no such parameter, so real
//    hardware has this same mismatch. It is genuinely visible on the
//    bridge, whose second row is `0xFF,0x00` (solid on the left half only)
//    but which is drawn FLIPH at (0,40): the drawn solid half and the
//    collidable solid half are opposite. Preserved - this shim's
//    `gbCollideBitmapBitmap()` is a direct port of the real one and behaves
//    identically.
//
// 11) **A stage-gated platform still swallows the else-if chain.** The
//    player's landing chain tests `platform2`/`platform3` collision
//    unconditionally and only acts on the result `if(stage < 6)` etc - so
//    once a platform has been removed, occupying the space where it used to
//    be still consumes the chain and skips the final `else` branch, leaving
//    the player neither grounded nor accumulating gravity (a real invisible
//    "hover pocket"). Preserved.
//
// 12) **The hunter's landing chain disagrees with the player's.** Its
//    right-hand `platform3` case is gated on `stage < 6` where the player's
//    is `stage < 11`, and both its `platform3` cases snap to y=11 from a
//    `< 13` test where the player uses y=9 from `< 11`. Copied verbatim,
//    inconsistencies included.
//
// -----------------------------------------------------------------------------
// SHIM GAPS: none. Every primitive needed already exists and is used
// directly - gbDrawBitmap/gbDrawBitmapRotated, gbCollideBitmapBitmap,
// gbDrawRect, gbPrintString/gbPrintNumber, gbPressed/gbRepeat, gbSetFont,
// gbPlayNote/gbPlayOK/gbPlayCancel, gbSetFrameRate, arand. The only real
// upstream calls without an equivalent (`gb.menu`, `gb.titleScreen`,
// `gb.changeGame`, `gb.getCpuLoad`, `gb.getFreeRam`, `gb.battery.show`,
// `gb.sound.command`) are each an already-established, already-documented
// project-wide scope decision, not a new gap.
// -----------------------------------------------------------------------------

#define AER_STATE_TITLE    0
#define AER_STATE_MENU     1
#define AER_STATE_PLAY     2
#define AER_STATE_STATUS   3
#define AER_STATE_CONTROLS 4
#define AER_STATE_OPTIONS  5

#define AER_MENULENGTH 5

// -----------------------------------------------------------------------------
// Real bitmaps (sprites.ino) - see this file's own header comment for the
// B-literal -> hex conversion and its per-table byte-count check.
// -----------------------------------------------------------------------------

int aerPlayerStandBitmap[10]=
{
    8, 8,
    0xCC, 0xF4, 0x3F, 0x3C, 0x18, 0x24, 0xC6, 0xC6
};

int aerPlayerFlyBitmap[7]=
{
    8, 5,
    0xCC, 0xF4, 0x3F, 0x3C, 0x3C
};

int aerLifeBitmap[6]=
{
    8, 4,
    0xCB, 0xC3, 0xE7, 0xDB
};

int aerPlatformBitmap[58]=
{
    56, 8,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

int aerPlatform2Bitmap[14]=
{
    24, 4,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};

int aerPlatform3Bitmap[10]=
{
    16, 4,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF
};

// second row is solid on the LEFT half only - see preserved quirk 10
int aerBridgeBitmap[6]=
{
    16, 2,
    0xFF, 0xFF,
    0xFF, 0x00
};

int aerTitleBitmap[242]=
{
    64, 30,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFC, 0xE0, 0x3F, 0x01, 0xF0, 0x0F, 0x3F, 0x81,
    0xE0, 0xE0, 0x3F, 0x01, 0xF0, 0x0F, 0x37, 0x81,
    0xC3, 0xF9, 0xFE, 0x79, 0xFC, 0xFE, 0x67, 0x3F,
    0x8F, 0xF9, 0xFE, 0x39, 0xFC, 0xFE, 0x0F, 0x3F,
    0x9F, 0xF3, 0xFC, 0x03, 0xF9, 0xFC, 0x1E, 0x07,
    0x87, 0xF3, 0xFC, 0x83, 0xF9, 0xFC, 0x7E, 0x07,
    0xC3, 0xE7, 0xF9, 0xCF, 0xF3, 0xF8, 0x3C, 0xFF,
    0xF3, 0xE7, 0xF9, 0xCF, 0xF3, 0xF9, 0x1C, 0xFF,
    0x03, 0xCF, 0xF3, 0xCF, 0x00, 0xF3, 0x88, 0x07,
    0x0F, 0xCF, 0xF3, 0xCF, 0x00, 0xF3, 0xC8, 0x07,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xF1, 0xFC, 0x3E, 0x7E, 0x7C, 0x73, 0xFF,
    0xFF, 0xF0, 0xF9, 0x9E, 0x7E, 0x7C, 0x73, 0xFF,
    0xFF, 0xE4, 0xF3, 0x9C, 0xFC, 0xF8, 0x67, 0xFF,
    0xFF, 0xE4, 0xF3, 0x9C, 0xFC, 0xF8, 0x67, 0xFF,
    0xFF, 0xCC, 0xE7, 0x39, 0x99, 0xF2, 0x4F, 0xFF,
    0xFF, 0xCE, 0xE7, 0x39, 0x99, 0xF2, 0x4F, 0xFF,
    0xFF, 0x9C, 0xCE, 0x73, 0x33, 0xE6, 0x1F, 0xFF,
    0xFF, 0x90, 0xCE, 0x73, 0x33, 0xE6, 0x1F, 0xFF,
    0xFF, 0x03, 0xE4, 0xE0, 0x07, 0xCE, 0x7F, 0xFF,
    0xFF, 0x0F, 0xF1, 0xE0, 0x07, 0xCF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

int aerPointBitmap[10]=
{
    8, 8,
    0x01, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x01
};

int aerDactylBitmap[10]=
{
    16, 4,
    0x0F, 0x3C,
    0x33, 0xFF,
    0x1F, 0x3C,
    0x30, 0x0C
};

int aerFloaterBitmap[7]=
{
    8, 5,
    0xC8, 0xCC, 0x7F, 0x3E, 0x1C
};

int aerBouncerBitmap[6]=
{
    8, 4,
    0x08, 0xCC, 0xFF, 0x3E
};

int aerHunterBitmap[7]=
{
    8, 5,
    0xE6, 0xE6, 0x3F, 0x1E, 0x07
};

int aerEggBitmap[6]=
{
    8, 4,
    0x0C, 0x3E, 0x3E, 0x1C
};

// -----------------------------------------------------------------------------
// Real text lines containing a Gamebuino button-icon glyph (ASCII 21/22/23),
// which a plain quoted string literal cannot hold - see this file's own
// header comment.
// -----------------------------------------------------------------------------

// upstream's own `println("\25 Jump")` - ASCII 21 is the real A-button icon
char aerCtrlAText[8]= { 21, 32, 74, 117, 109, 112, 10, 0 };
// upstream's own `println("\26 Jump")` - ASCII 22 is the real B-button icon
char aerCtrlBText[8]= { 22, 32, 74, 117, 109, 112, 10, 0 };
// upstream's own `println("\27 Pause")` - ASCII 23 is the real C-button icon
char aerCtrlCText[9]= { 23, 32, 80, 97, 117, 115, 101, 10, 0 };
// upstream's own `println("Press \26")`
char aerPressBText[8]= { 80, 114, 101, 115, 115, 32, 22, 0 };
// upstream's own `println("\26 to resume")`
char aerResumeText[12]= { 22, 32, 116, 111, 32, 114, 101, 115, 117, 109, 101, 0 };

// -----------------------------------------------------------------------------
// Real upstream globals (StrikeDown.ino), one per line - this dialect shares
// the full type across a comma-separated declaration list, and comma-declared
// aggregate globals are a known real hazard here (see gameTron.c's own
// header comment), so this file never uses that form at all.
// -----------------------------------------------------------------------------

int aerPlayerX;
int aerPlayerY;
int aerPlayerXv;
int aerPlayerFlip;   // 0 = NOFLIP, 1 = FLIPH
int aerPlayerGrav;
bool aerGround;      // upstream's own `int ground = true`

int aerFloaterX;
int aerFloaterY;
int aerFloaterLine;
int aerFloaterXv;
bool aerFloaterLeft; // always false - see preserved quirk 3
int aerFloaterFlip;
int aerFloaterGrav;
bool aerFloatEgg;

int aerBouncerX;
int aerBouncerY;
int aerBouncerGrav;
int aerBouncerXv;
int aerBouncerFlip;
bool aerBounceEgg;

int aerHunterX;
int aerHunterY;
int aerHunterGrav;
int aerHunterXv;
int aerHunterFlip;
bool aerHuntEgg;

int aerBouncers;
int aerHunters;
int aerFloaters;

int aerDactylX;
int aerDactylY;

int aerWait;
int aerStage;
int aerDactylTime;
int aerEnemies;
int aerSpawn;
int aerSpawnX;
int aerSpawnY;

int aerSelect;       // the Options screen's own highlighted option

int aerRest;         // per-stage breather countdown ("Stage N" is shown while > 0)

bool aerPause;

bool aerPterodactyl; // a real, never-read option - see preserved quirk 8
int aerCount;        // the Lives option (1/3/5)
int aerFps;          // the Speed option (15/30/45)

int aerLives;
bool aerAlive;

// Port-only state (upstream's own blocking widgets - see header comment)
int aerState;
int aerMenuIndex;
int aerTitleLogoY;   // 20 from setup()'s own named call, 12 from the menu's cancel path

// -----------------------------------------------------------------------------
// Collision helper
// -----------------------------------------------------------------------------

// Every real player-collision line in upstream reads
// `collideBitmapBitmap(playerx, playery, playerfly, X, Y, B) ||
//  collideBitmapBitmap(playerx, playery, playerstand, X, Y, B) == true`
// - `==` binds tighter than `||`, so that is plainly "either player sprite
// overlaps B", which is what this returns. Both sprites are always tested
// regardless of which one is currently drawn, exactly like upstream.
bool aerPlayerHits( int x, int y, int* bitmap )
{
    if( gbCollideBitmapBitmap( aerPlayerX, aerPlayerY, aerPlayerFlyBitmap, x, y, bitmap ) )
      return true;

    return gbCollideBitmapBitmap( aerPlayerX, aerPlayerY, aerPlayerStandBitmap, x, y, bitmap );
}

// The real, identical `playNote(1,28,1)` + 2 `command()`s + `alive = false;`
// + `lives--` sequence upstream repeats at four separate death sites -
// including upstream's own real, literal call order (the note plays
// first, the two command() slides are issued right after it, not before).
void aerKillPlayer()
{
    gbPlayNoteChannel( 1, 28, 1 );
    gbSoundCommand( GB_CMD_INSTRUMENT, 1, 0, 1 );
    gbSoundCommand( GB_CMD_SLIDE, 7, -2, 1 );
    aerAlive = false;
    aerLives = aerLives - 1;
}

// -----------------------------------------------------------------------------
// State transitions
// -----------------------------------------------------------------------------

void aerBeginTitle( int logoY )
{
    aerState = AER_STATE_TITLE;
    aerTitleLogoY = logoY;
    gbSetFont( gbFont5x7 );
}

void aerBeginMenu()
{
    aerState = AER_STATE_MENU;
    aerMenuIndex = 0; // real gb.menu()'s own fresh `activeItem = 0` per call
    gbSetFont( gbFont5x7 );
}

// == the whole of upstream's own `case 0:` reset block.
void aerBeginPlay()
{
    gbPickRandomSeed(); // no-op - see gamebuinoShim.h's own header comment

    aerPlayerX = 38;
    aerPlayerY = 33;
    aerPlayerXv = 0;
    aerPlayerFlip = 0;
    aerPlayerGrav = 1;

    aerFloaterX = 20;
    aerFloaterY = 20;
    aerFloaterLine = 4 + arand( 7 ); // random(4,11)
    aerFloaterXv = 0;
    aerFloaterLeft = false;
    aerFloaterFlip = 0;
    aerFloaterGrav = 0;
    aerFloatEgg = false;

    aerBouncerX = 20;
    aerBouncerY = 20;
    aerBouncerGrav = 0;
    aerBouncerXv = 0;
    aerBouncerFlip = 0;
    aerBounceEgg = false;

    aerHunterX = 20;
    aerHunterY = 20;
    aerHunterGrav = 0;
    aerHunterXv = 0;
    aerHunterFlip = 0;
    aerHuntEgg = false;

    aerBouncers = arand( 5 ); // random(0,5)
    aerHunters = arand( 5 );
    aerFloaters = arand( 5 );

    aerEnemies = aerBouncers + aerHunters + aerFloaters;

    aerDactylX = 84;
    aerDactylY = 20;

    aerWait = 0;
    aerStage = 1;
    aerSpawn = arand( 2 ); // random(0,2) - never 2, see preserved quirk 4
    aerSpawnX = 0;
    aerSpawnY = 0;

    aerRest = 150;
    aerDactylTime = 450;

    gbSetFrameRate( aerFps );
    aerLives = aerCount;

    aerAlive = true;
    aerPause = false;
    gbSetFont( gbFont3x5 );

    aerState = AER_STATE_PLAY;
}

// -----------------------------------------------------------------------------
// Title (real gb.titleScreen - see this file's own header comment)
// -----------------------------------------------------------------------------

void aerUpdateTitle()
{
    gbSetColor( GB_BLACK );

    // upstream's own name string is a single invisible space; the real
    // blinking A-icon prompt is replaced by this project's own established
    // "PRESS A" stand-in
    gbCursorX = 21;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    gbDrawBitmap( 0, aerTitleLogoY, aerTitleBitmap );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        aerBeginMenu();
    }
}

// -----------------------------------------------------------------------------
// Menu (real gb.menu(Survival/Status/Controls/Options/Change Game))
// -----------------------------------------------------------------------------

char* aerMenuItemText( int idx )
{
    if( idx == 0 ) return "Survival";
    if( idx == 1 ) return "Status";
    if( idx == 2 ) return "Controls";
    if( idx == 3 ) return "Options";
    return "Change Game"; // idx == 4
}

void aerUpdateMenu()
{
    int i;
    int y;

    gbSetColor( GB_BLACK );

    for( i = 0; i < AER_MENULENGTH; i = i + 1 )
    {
        y = 2 + i * 9;

        gbCursorX = 8;
        gbCursorY = y;
        gbPrintString( aerMenuItemText( i ) );

        if( i == aerMenuIndex )
        {
            gbCursorX = 2;
            gbCursorY = y;
            gbPrintString( ">" );
        }
    }

    // real gb.menu(): B or C cancels and returns -1, which upstream's own
    // `case -1:` handles by showing the logo-only title screen
    if( gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        aerBeginTitle( 12 );
        return;
    }

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();

        if( aerMenuIndex == 0 ) aerBeginPlay();
        else if( aerMenuIndex == 1 ) aerState = AER_STATE_STATUS;
        else if( aerMenuIndex == 2 ) aerState = AER_STATE_CONTROLS;
        else if( aerMenuIndex == 3 ) aerState = AER_STATE_OPTIONS;
        else aerBeginTitle( 12 ); // real gb.changeGame() - see header comment

        return;
    }

    if( gbRepeat( BTN_DOWN, 4 ) )
    {
        aerMenuIndex = aerMenuIndex + 1;
        if( aerMenuIndex >= AER_MENULENGTH ) aerMenuIndex = 0;
        gbPlayTick();
    }

    if( gbRepeat( BTN_UP, 4 ) )
    {
        aerMenuIndex = aerMenuIndex - 1;
        if( aerMenuIndex < 0 ) aerMenuIndex = AER_MENULENGTH - 1;
        gbPlayTick();
    }
}

// -----------------------------------------------------------------------------
// Status (real getCpuLoad()/getFreeRam() - "N/A" here, see header comment)
// -----------------------------------------------------------------------------

void aerUpdateStatus()
{
    if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        aerBeginMenu();
        return;
    }

    gbSetColor( GB_BLACK );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "CPU:N/A%\n" );
    gbCursorX = 2;
    gbPrintString( "\n" );
    gbCursorX = 2;
    gbPrintString( "Free RAM: N/A\n" );
    gbCursorX = 2;
    gbPrintString( "\n" );
    gbCursorX = 2;
    gbPrintString( aerPressBText );
}

// -----------------------------------------------------------------------------
// Controls
// -----------------------------------------------------------------------------
// Real upstream's own `case 2:` (Controls) has no `break;`, so on real
// hardware pressing B here falls straight through into `case 3:` (Options)
// instead of back to the menu - preserved quirk 1 in this file's own header
// comment documents this precisely. Fixed here on direct request: B now
// returns to the menu, matching every other sub-screen's own B-handler
// (aerUpdateStatus() above is the same shape) rather than the real
// hardware's own missing-break bug.
void aerUpdateControls()
{
    if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        aerBeginMenu();
        return;
    }

    gbSetColor( GB_BLACK );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );

    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( aerCtrlAText );
    gbCursorX = 2;
    gbPrintString( "\n" );
    gbCursorX = 2;
    gbPrintString( aerCtrlBText );
    gbCursorX = 2;
    gbPrintString( "\n" );
    gbCursorX = 2;
    gbPrintString( aerCtrlCText );
}

// -----------------------------------------------------------------------------
// Options
// -----------------------------------------------------------------------------

void aerUpdateOptions()
{
    if( gbPressed( BTN_B ) )
    {
        gbPlayCancel();
        aerBeginMenu();
        return;
    }

    // UP increments and DOWN decrements - see preserved quirk 9
    if( gbRepeat( BTN_UP, 15 ) )
    {
        aerSelect = aerSelect + 1;
        if( aerSelect == 3 ) aerSelect = 0;
    }
    if( gbRepeat( BTN_DOWN, 15 ) )
    {
        aerSelect = aerSelect - 1;
        if( aerSelect == -1 ) aerSelect = 2;
    }

    gbSetColor( GB_BLACK );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );
    gbCursorY = 10;

    if( aerSelect == 0 )
    {
        gbCursorX = 10;
        gbPrintString( "Pterodactyl\n" );
        gbCursorX = 2;
        gbPrintString( " \n" );
        gbCursorX = 34;
        if( aerPterodactyl ) gbPrintString( "YES\n" );
        if( !aerPterodactyl ) gbPrintString( "NO!\n" );

        if( gbRepeat( BTN_A, 15 ) )
        {
            // a real option that nothing ever reads - see preserved quirk 8
            if( aerPterodactyl ) aerPterodactyl = false;
            else aerPterodactyl = true;
        }
    }

    if( aerSelect == 1 )
    {
        gbDrawBitmap( 2, 28, aerPointBitmap );
        gbDrawBitmapRotated( 74, 28, aerPointBitmap, 0, 1 ); // NOROT, FLIPH
        gbCursorX = 26;
        gbPrintString( "Lives\n" );
        gbCursorX = 2;
        gbPrintString( " \n" );
        gbCursorX = 24;
        if( aerCount == 5 ) gbPrintString( "Five..\n" );
        if( aerCount == 3 ) gbPrintString( "Three?\n" );
        if( aerCount == 1 ) gbPrintString( "One!!!\n" );

        if( gbRepeat( BTN_LEFT, 15 ) )
        {
            aerCount = aerCount - 2;
            if( aerCount <= -1 ) aerCount = 5;
        }
        if( gbRepeat( BTN_RIGHT, 15 ) )
        {
            aerCount = aerCount + 2;
            if( aerCount >= 7 ) aerCount = 1;
        }
    }

    if( aerSelect == 2 )
    {
        gbDrawBitmap( 2, 28, aerPointBitmap );
        gbDrawBitmapRotated( 74, 28, aerPointBitmap, 0, 1 ); // NOROT, FLIPH
        gbCursorX = 26;
        gbPrintString( "Speed\n" );
        gbCursorX = 2;
        gbPrintString( " \n" );
        gbCursorX = 20;
        if( aerFps == 45 ) gbPrintString( "Fast!!!\n" );
        if( aerFps == 30 ) gbPrintString( "Regular\n" );
        if( aerFps == 15 ) gbPrintString( "Slow...\n" );

        if( gbRepeat( BTN_LEFT, 15 ) )
        {
            aerFps = aerFps - 15;
            if( aerFps <= 0 ) aerFps = 45;
        }
        if( gbRepeat( BTN_RIGHT, 15 ) )
        {
            aerFps = aerFps + 15;
            if( aerFps >= 60 ) aerFps = 15;
        }
    }
}

// -----------------------------------------------------------------------------
// Gameplay (the body of upstream's own play() while(true) loop)
// -----------------------------------------------------------------------------

void aerUpdateRunning()
{
    // enemy counter
    aerEnemies = aerBouncers + aerHunters + aerFloaters;
    if( aerEnemies == 0 )
    {
        aerBouncers = 1 + arand( 4 ); // random(1,5)
        aerHunters = arand( 5 );      // random(0,5)
        aerFloaters = 1 + arand( 4 ); // random(1,5)
        aerRest = 150;
        aerDactylTime = 450;
        aerStage = aerStage + 1;
    }

    // Random Spawn (spawn is only ever 0 or 1 - see preserved quirk 4)
    if( aerSpawn == 0 ) { aerSpawnX = 8;  aerSpawnY = 11; }
    if( aerSpawn == 1 ) { aerSpawnX = 40; aerSpawnY = 19; }
    if( aerSpawn == 2 ) { aerSpawnX = 76; aerSpawnY = 11; }

    // Rest variable (so the player can have a break)
    if( aerRest > 0 )
    {
        aerRest = aerRest - 1;
        gbCursorX = 30;
        gbCursorY = 14;
        gbPrintString( "Stage " );
        gbPrintNumber( aerStage );
    }

    // wait counter (for timing)
    aerWait = aerWait + 1;
    if( aerWait == 20 ) aerWait = 0;

    // Pterodactyl
    if( aerDactylTime > 0 && aerRest == 0 )
    {
        aerDactylTime = aerDactylTime - 1;
        aerDactylX = 84;
    }
    if( aerDactylTime == 0 )
    {
        gbDrawBitmap( aerDactylX, aerDactylY, aerDactylBitmap );
        aerDactylX = aerDactylX - 1;
        if( aerDactylX <= -15 ) aerDactylX = 91;
        if( aerDactylX >= 92 ) aerDactylX = -14;

        // upstream: `if((dactyly < playery) && (wait == 0 || 10))` /
        // `else if(wait == 0 || 10)` - both `wait` clauses are
        // unconditionally true (see preserved quirk 2), so this is simply
        // "step one pixel toward the player's height, every tick"
        if( aerDactylY < aerPlayerY ) aerDactylY = aerDactylY + 1;
        else aerDactylY = aerDactylY - 1;
    }

    // Floater
    if( aerFloaterX <= -7 ) aerFloaterX = 83;
    if( aerFloaterX >= 84 ) aerFloaterX = -6;
    if( !aerFloatEgg )
    {
        if( aerFloaters > 0 && aerRest == 0 )
        {
            aerFloaterGrav = 0;
            gbDrawBitmapRotated( aerFloaterX, aerFloaterY, aerFloaterBitmap, 0, aerFloaterFlip );

            // upstream's own `if(wait == 0 || 5 || 10 || 15)` guards below
            // are unconditionally true - see preserved quirk 2
            if( aerFloaterLeft ) // never true - see preserved quirk 3
            {
                aerFloaterXv = -1;
                aerFloaterX = aerFloaterX + aerFloaterXv;
                aerFloaterFlip = 0; // NOFLIP
            }
            if( !aerFloaterLeft )
            {
                aerFloaterXv = 1;
                aerFloaterX = aerFloaterX + aerFloaterXv;
                aerFloaterFlip = 1; // FLIPH
            }

            // random(floaterline - 1, floaterline + 1) - so exactly two
            // possible rows, floaterline-1 or floaterline
            aerFloaterY = ( aerFloaterLine - 1 ) + arand( 2 );
        }
    }
    if( aerFloatEgg )
    {
        aerFloaterY = aerFloaterY + aerFloaterGrav;
        aerFloaterX = aerFloaterX + aerFloaterXv;
        gbDrawBitmapRotated( aerFloaterX, aerFloaterY, aerEggBitmap, 0, aerFloaterFlip );
        aerFloaterGrav = 1;
    }

    // Bouncer
    if( aerBouncerX <= -7 ) aerBouncerX = 83;
    if( aerBouncerX >= 84 ) aerBouncerX = -6;
    if( !aerBounceEgg )
    {
        if( aerBouncers > 0 && aerRest == 0 )
        {
            aerBouncerY = aerBouncerY + aerBouncerGrav;
            aerBouncerX = aerBouncerX + aerBouncerXv;
            gbDrawBitmapRotated( aerBouncerX, aerBouncerY, aerBouncerBitmap, 0, aerBouncerFlip );

            if( aerBouncerY < aerPlayerY && aerWait == 0 && aerBouncerGrav < 1 )
              aerBouncerGrav = aerBouncerGrav + 1;
            if( aerBouncerY == aerPlayerY )
              aerBouncerGrav = 0;
            if( aerBouncerY > aerPlayerY && aerWait == 0 && aerBouncerGrav > -1 )
              aerBouncerGrav = aerBouncerGrav - 1;
            if( aerBouncerX < aerPlayerX && aerWait == 0 && aerBouncerXv < 1 )
            {
                aerBouncerXv = aerBouncerXv + 1;
                aerBouncerFlip = 1; // FLIPH
            }
            if( aerBouncerX > aerPlayerX && aerWait == 0 && aerBouncerXv > -1 )
            {
                aerBouncerXv = aerBouncerXv - 1;
                aerBouncerFlip = 0; // NOFLIP
            }
        }
    }
    if( aerBounceEgg )
    {
        aerBouncerY = aerBouncerY + aerBouncerGrav;
        aerBouncerX = aerBouncerX + aerBouncerXv;
        gbDrawBitmapRotated( aerBouncerX, aerBouncerY, aerEggBitmap, 0, aerBouncerFlip );
        aerBouncerGrav = 1;
    }

    // Hunter
    if( aerHunterX <= -7 ) aerHunterX = 83;
    if( aerHunterX >= 84 ) aerHunterX = -6;
    if( !aerHuntEgg )
    {
        if( aerHunters > 0 && aerRest == 0 )
        {
            aerHunterY = aerHunterY + aerHunterGrav;
            aerHunterX = aerHunterX + aerHunterXv;
            gbDrawBitmapRotated( aerHunterX, aerHunterY, aerHunterBitmap, 0, aerHunterFlip );

            if( aerAlive )
            {
                if( aerHunterY == aerPlayerY )
                  aerHunterGrav = 0;
                if( aerHunterY < aerPlayerY && aerWait == 10 && aerHunterGrav < 1 )
                  aerHunterGrav = aerHunterGrav + 1;
                if( aerHunterY > aerPlayerY && aerWait == 10 && aerHunterGrav > -1 )
                  aerHunterGrav = aerHunterGrav - 1;
                if( aerHunterX < aerPlayerX )
                {
                    aerHunterX = aerHunterX + 1;
                    aerHunterFlip = 1; // FLIPH
                }
                if( aerHunterX > aerPlayerX )
                {
                    aerHunterX = aerHunterX - 1;
                    aerHunterFlip = 0; // NOFLIP
                }
            }
        }
    }
    if( aerHuntEgg )
    {
        aerHunterY = aerHunterY + aerHunterGrav;
        aerHunterX = aerHunterX + aerHunterXv;
        gbDrawBitmapRotated( aerHunterX, aerHunterY, aerEggBitmap, 0, aerHunterFlip );
        aerHunterGrav = 1;
    }

    // Player
    aerPlayerY = aerPlayerY + aerPlayerGrav;
    aerPlayerX = aerPlayerX + aerPlayerXv;
    if( aerGround && aerAlive )
      gbDrawBitmapRotated( aerPlayerX, aerPlayerY, aerPlayerStandBitmap, 0, aerPlayerFlip );
    if( !aerGround && aerAlive )
      gbDrawBitmapRotated( aerPlayerX, aerPlayerY, aerPlayerFlyBitmap, 0, aerPlayerFlip );

    if( gbRepeat( BTN_LEFT, 15 ) )
    {
        aerPlayerXv = aerPlayerXv - 1;
        aerPlayerFlip = 0; // NOFLIP
    }
    if( gbRepeat( BTN_RIGHT, 15 ) )
    {
        aerPlayerXv = aerPlayerXv + 1;
        aerPlayerFlip = 1; // FLIPH
    }
    if( aerPlayerXv >= 3 ) aerPlayerXv = 2;
    if( aerPlayerXv <= -3 ) aerPlayerXv = -2;
    if( aerPlayerX <= -7 ) aerPlayerX = 83;
    if( aerPlayerX >= 84 ) aerPlayerX = -6;

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
    {
        // Real upstream call order: the note plays first, the 3 command()
        // slides are issued right after it - see header comment.
        gbPlayNoteChannel( 5, 8, 1 );
        gbSoundCommand( GB_CMD_VOLUME, 4, 0, 1 );
        gbSoundCommand( GB_CMD_INSTRUMENT, 1, 0, 1 );
        gbSoundCommand( GB_CMD_TREMOLO, 8, 3, 1 );
        aerPlayerY = aerPlayerY - 1;
        aerPlayerGrav = aerPlayerGrav - 1;
    }
    if( gbRepeat( BTN_C, 20 ) )
      aerPause = true;

    // Death Code
    if( aerPlayerY >= 50 )
      aerKillPlayer();

    if( aerPlayerHits( aerDactylX, aerDactylY, aerDactylBitmap ) )
    {
        if( aerDactylTime == 0 )
        {
            if( aerPlayerY <= aerDactylY - 3 )
              aerDactylTime = 450;
            else if( aerAlive )
            {
                aerDactylTime = 450;
                aerKillPlayer();
            }
        }
    }
    if( aerPlayerHits( aerFloaterX, aerFloaterY, aerFloaterBitmap ) )
    {
        if( aerFloaters > 0 )
        {
            if( aerPlayerY <= aerFloaterY - 1 )
            {
                aerFloaterX = aerFloaterX + 2;
                aerFloaterY = aerFloaterY + 2;
                aerFloatEgg = true;
            }
            else if( aerAlive && aerRest == 0 )
              aerKillPlayer();
        }
    }
    if( aerPlayerHits( aerBouncerX, aerBouncerY, aerBouncerBitmap ) )
    {
        if( aerBouncers > 0 )
        {
            if( aerPlayerY <= aerBouncerY - 1 )
            {
                aerBouncerX = aerBouncerX + 2;
                aerBouncerY = aerBouncerY + 2;
                aerBounceEgg = true;
            }
            // the bouncer is the one enemy that never kills - it shoves
            else if( aerBouncerFlip == 1 && aerAlive && aerRest == 0 )
            {
                aerPlayerXv = 1;
                aerBouncerXv = -1;
                aerPlayerFlip = 1; // FLIPH
            }
            else if( aerAlive && aerRest == 0 )
            {
                aerPlayerXv = -1;
                aerBouncerXv = 1;
                aerPlayerFlip = 0; // NOFLIP
            }
        }
    }
    if( aerPlayerHits( aerHunterX, aerHunterY, aerHunterBitmap ) )
    {
        if( aerHunters > 0 )
        {
            if( aerPlayerY <= aerHunterY - 1 )
            {
                aerHunterX = aerHunterX + 2;
                aerHunterY = aerHunterY + 2;
                aerHuntEgg = true;
            }
            else if( aerAlive && aerRest == 0 )
              aerKillPlayer();
        }
    }

    // Egg pickups. These test the egg bitmap at each enemy's own live
    // position whether or not that enemy is currently an egg, and the
    // spawn coordinates they read are one tick stale - see preserved
    // quirks 5 and 6.
    if( aerPlayerHits( aerFloaterX, aerFloaterY, aerEggBitmap ) )
    {
        if( aerFloaters > 0 )
        {
            aerSpawn = arand( 2 );            // random(0,2)
            aerFloaterLine = 4 + arand( 7 );  // random(4,11)
            aerFloaterX = -10;
            aerFloaterY = 0;
            aerFloaterXv = 0;
            aerFloaters = aerFloaters - 1;
            aerFloaterLeft = arand( 1 );      // random(0,1) - always 0/false
        }
        aerFloatEgg = false;
    }
    if( aerPlayerHits( aerBouncerX, aerBouncerY, aerEggBitmap ) )
    {
        if( aerBouncers > 0 )
        {
            aerSpawn = arand( 2 );
            aerBouncerX = aerSpawnX;
            aerBouncerY = aerSpawnY;
            aerBouncerXv = 0;
            aerBouncerGrav = 0;
            aerBouncers = aerBouncers - 1;
        }
        aerBounceEgg = false;
    }
    if( aerPlayerHits( aerHunterX, aerHunterY, aerEggBitmap ) )
    {
        if( aerHunters > 0 )
        {
            aerSpawn = arand( 2 );
            aerHunterX = aerSpawnX;
            aerHunterY = aerSpawnY;
            aerHunterXv = 0;
            aerHunterGrav = 0;
            aerHunters = aerHunters - 1;
        }
        aerHuntEgg = false;
    }

    if( !aerAlive )
    {
        aerPlayerX = 38;
        aerPlayerY = 33;
        aerPlayerXv = 0;
        aerPlayerFlip = 0;
        aerPlayerGrav = 1;
        if( aerWait == 9 ) aerAlive = true;
    }

    // Drawing - the arena, thinning out as the stage number climbs
    gbDrawBitmap( 14, 40, aerPlatformBitmap );
    if( aerStage < 3 )
    {
        gbDrawBitmapRotated( 0, 40, aerBridgeBitmap, 0, 1 ); // NOROT, FLIPH
        gbDrawBitmap( 70, 40, aerBridgeBitmap );
    }
    if( aerStage < 6 )
      gbDrawBitmap( 30, 26, aerPlatform2Bitmap );
    if( aerStage < 9 )
      gbDrawBitmap( 0, 16, aerPlatform3Bitmap );
    if( aerStage < 11 )
      gbDrawBitmap( 68, 16, aerPlatform3Bitmap );

    // The lives readout, inverted and one heart adrift - see quirk 7
    if( aerLives < 1 ) gbDrawBitmap( 18, 0, aerLifeBitmap );
    if( aerLives < 2 ) gbDrawBitmap( 24, 42, aerLifeBitmap );
    if( aerLives < 3 ) gbDrawBitmap( 30, 42, aerLifeBitmap );
    if( aerLives < 4 ) gbDrawBitmap( 36, 42, aerLifeBitmap );

    // Player Collision - a single else-if chain, so a stage-removed
    // platform still swallows it (see preserved quirk 11)
    if( aerPlayerHits( 14, 40, aerPlatformBitmap ) )
    {
        if( aerPlayerY < 35 )
        {
            aerPlayerY = 33;
            aerPlayerGrav = 0;
            aerGround = true;
        }
    }
    else if( aerPlayerHits( 30, 26, aerPlatform2Bitmap ) )
    {
        if( aerStage < 6 )
        {
            if( aerPlayerY < 21 )
            {
                aerPlayerY = 19;
                aerGround = true;
            }
            else aerPlayerY = aerPlayerY + 1;

            aerPlayerGrav = 0;
        }
    }
    else if( aerPlayerHits( 0, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 9 )
        {
            if( aerPlayerY < 11 )
            {
                aerPlayerY = 9;
                aerGround = true;
            }
            else aerPlayerY = aerPlayerY + 1;

            aerPlayerGrav = 0;
        }
    }
    else if( aerPlayerHits( 68, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 11 )
        {
            if( aerPlayerY < 11 )
            {
                aerPlayerY = 9;
                aerGround = true;
            }
            else aerPlayerY = aerPlayerY + 1;

            aerPlayerGrav = 0;
        }
    }
    else if( aerPlayerHits( 0, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerPlayerY = 33;
            aerPlayerGrav = 0;
            aerGround = true;
        }
    }
    else if( aerPlayerHits( 70, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerPlayerY = 33;
            aerPlayerGrav = 0;
            aerGround = true;
        }
    }
    else if( aerPlayerY <= -1 )
    {
        aerPlayerY = 0;
        aerPlayerGrav = 0;
    }
    else
    {
        aerGround = false;
        if( aerWait == 10 )
          aerPlayerGrav = aerPlayerGrav + 1;
    }

    // Hunter Collision - note the two real disagreements with the player's
    // own chain above (see preserved quirk 12)
    if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerHunterBitmap, 14, 40, aerPlatformBitmap ) )
    {
        if( aerHunterY < 37 )
        {
            aerHunterY = 35;
            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerHunterBitmap, 30, 26, aerPlatform2Bitmap ) )
    {
        if( aerStage < 6 )
        {
            if( aerHunterY < 21 ) aerHunterY = 19;
            else aerHunterY = aerHunterY + 1;

            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerHunterBitmap, 0, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 9 )
        {
            if( aerHunterY < 13 ) aerHunterY = 11;
            else aerHunterY = aerHunterY + 1;

            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerHunterBitmap, 68, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 6 ) // upstream really does test 6 here, not 11
        {
            if( aerHunterY < 13 ) aerHunterY = 11;
            else aerHunterY = aerHunterY + 1;

            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerHunterBitmap, 0, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerHunterY = 35;
            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerHunterBitmap, 70, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerHunterY = 35;
            aerHunterGrav = 0;
        }
    }
    else if( aerHunterY <= -1 )
    {
        aerHunterY = 0;
        aerHunterGrav = 0;
    }

    // Floater Egg landing
    if( gbCollideBitmapBitmap( aerFloaterX, aerFloaterY, aerEggBitmap, 14, 40, aerPlatformBitmap ) )
    {
        aerFloaterY = 36;
        aerFloaterXv = 0;
        aerFloaterGrav = 0;
    }
    else if( gbCollideBitmapBitmap( aerFloaterX, aerFloaterY, aerEggBitmap, 30, 26, aerPlatform2Bitmap ) )
    {
        if( aerStage < 6 )
        {
            aerFloaterY = 22;
            aerFloaterXv = 0;
            aerFloaterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerFloaterX, aerFloaterY, aerEggBitmap, 0, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 9 )
        {
            aerFloaterY = 12;
            aerFloaterXv = 0;
            aerFloaterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerFloaterX, aerFloaterY, aerEggBitmap, 68, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 11 )
        {
            aerFloaterY = 12;
            aerFloaterXv = 0;
            aerFloaterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerFloaterX, aerFloaterY, aerEggBitmap, 0, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerFloaterY = 36;
            aerFloaterGrav = 0;
            aerFloaterXv = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerFloaterX, aerFloaterY, aerEggBitmap, 70, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerFloaterY = 36;
            aerFloaterGrav = 0;
            aerFloaterXv = 0;
        }
    }

    // Bouncer Egg landing
    if( gbCollideBitmapBitmap( aerBouncerX, aerBouncerY, aerEggBitmap, 14, 40, aerPlatformBitmap ) )
    {
        aerBouncerY = 36;
        aerBouncerXv = 0;
        aerBouncerGrav = 0;
    }
    else if( gbCollideBitmapBitmap( aerBouncerX, aerBouncerY, aerEggBitmap, 30, 26, aerPlatform2Bitmap ) )
    {
        if( aerStage < 6 )
        {
            aerBouncerY = 22;
            aerBouncerXv = 0;
            aerBouncerGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerBouncerX, aerBouncerY, aerEggBitmap, 0, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 9 )
        {
            aerBouncerY = 12;
            aerBouncerXv = 0;
            aerBouncerGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerBouncerX, aerBouncerY, aerEggBitmap, 68, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 11 )
        {
            aerBouncerY = 12;
            aerBouncerXv = 0;
            aerBouncerGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerBouncerX, aerBouncerY, aerEggBitmap, 0, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerBouncerY = 36;
            aerBouncerGrav = 0;
            aerBouncerXv = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerBouncerX, aerBouncerY, aerEggBitmap, 70, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerBouncerY = 36;
            aerBouncerGrav = 0;
            aerBouncerXv = 0;
        }
    }

    // Hunter Egg landing
    if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerEggBitmap, 14, 40, aerPlatformBitmap ) )
    {
        aerHunterY = 36;
        aerHunterXv = 0;
        aerHunterGrav = 0;
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerEggBitmap, 30, 26, aerPlatform2Bitmap ) )
    {
        if( aerStage < 6 )
        {
            aerHunterY = 22;
            aerHunterXv = 0;
            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerEggBitmap, 0, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 9 )
        {
            aerHunterY = 12;
            aerHunterXv = 0;
            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerEggBitmap, 68, 16, aerPlatform3Bitmap ) )
    {
        if( aerStage < 11 )
        {
            aerHunterY = 12;
            aerHunterXv = 0;
            aerHunterGrav = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerEggBitmap, 0, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerHunterY = 36;
            aerHunterGrav = 0;
            aerHunterXv = 0;
        }
    }
    else if( gbCollideBitmapBitmap( aerHunterX, aerHunterY, aerEggBitmap, 70, 40, aerBridgeBitmap ) )
    {
        if( aerStage < 3 )
        {
            aerHunterY = 36;
            aerHunterGrav = 0;
            aerHunterXv = 0;
        }
    }
}

// == the whole body of upstream's own play() loop: the gameplay block, then
// the pause overlay, then the game over screen - all three gated exactly as
// upstream gates them, and all three able to run on the same tick.
void aerUpdatePlay()
{
    if( aerLives > 0 && !aerPause )
      aerUpdateRunning();

    if( aerPause )
    {
        gbSetFont( gbFont5x7 );
        gbSetColor( GB_BLACK );
        gbCursorX = 24;
        gbCursorY = 4;
        gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );
        gbPrintString( "PAUSED\n" );
        gbCursorX = 8;
        gbPrintNumber( aerLives );
        gbPrintString( " lives\n" );
        gbCursorX = 8;
        gbPrintString( " \n" );
        gbCursorX = 8;
        gbPrintString( aerResumeText );

        if( gbPressed( BTN_B ) )
        {
            gbSetFont( gbFont3x5 );
            aerPause = false;
        }
    }

    if( aerLives <= 0 )
    {
        gbSetFrameRate( 30 );
        gbSetFont( gbFont5x7 );
        gbSetColor( GB_BLACK );
        gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );
        gbCursorY = 2;
        gbCursorX = 16;
        gbPrintString( "You died!\n" );
        gbCursorX = 8;
        gbPrintString( "\n" );
        gbCursorX = 20;
        gbCursorY = 38;
        gbPrintString( aerPressBText );

        if( gbPressed( BTN_B ) )
        {
            // upstream's own three stacked `break;`s, only the first of
            // which is reachable - it leaves play() and returns to the menu
            gbPlayCancel();
            aerBeginMenu();
        }
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

// == upstream's own setup(): gb.begin(), setFont(font5x7), the title screen,
// the dropped battery indicator, and setFrameRate(30).
void gameAerialAssault_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 );
    gbSetColor( GB_BLACK );
    gbSetFrameRate( 30 );

    // One cartridge session runs many games in sequence, so every real
    // upstream file-scope initializer is re-applied here rather than
    // inheriting whatever a previous launch left behind (on real hardware
    // these are simply the globals' own load-time values). The four the
    // player can actually change - select/pterodactyl/count/fps - are the
    // real reason this matters: they persist across rounds within a
    // session, exactly like upstream, but must start fresh per launch.
    aerPlayerX = 38;
    aerPlayerY = 36;
    aerPlayerXv = 0;
    aerPlayerFlip = 0;
    aerPlayerGrav = 0;
    aerGround = true;

    aerFloaterX = 20;
    aerFloaterY = 20;
    aerFloaterLine = 0;
    aerFloaterXv = 0;
    aerFloaterLeft = false;
    aerFloaterFlip = 0;
    aerFloaterGrav = 0;
    aerFloatEgg = false;

    aerBouncerX = 20;
    aerBouncerY = 20;
    aerBouncerGrav = 0;
    aerBouncerXv = 0;
    aerBouncerFlip = 0;
    aerBounceEgg = false;

    aerHunterX = 20;
    aerHunterY = 20;
    aerHunterGrav = 0;
    aerHunterXv = 0;
    aerHunterFlip = 0;
    aerHuntEgg = false;

    aerBouncers = 0;
    aerHunters = 0;
    aerFloaters = 0;

    aerDactylX = 84;
    aerDactylY = 20;

    aerWait = 0;
    aerStage = 0;
    aerDactylTime = 450;
    aerEnemies = 0;
    aerSpawn = arand( 2 );
    aerSpawnX = 0;
    aerSpawnY = 0;

    aerSelect = 0;
    aerRest = 150;
    aerPause = false;

    aerPterodactyl = true;
    aerCount = 3;
    aerFps = 30;

    aerLives = 0;
    aerAlive = true;

    // setup()'s own gb.titleScreen(F(" "), Title): a named (if invisible)
    // title, so the logo sits one font5x7 line lower than the nameless
    // call the menu's cancel path makes - see this file's own header comment
    aerBeginTitle( 20 );
}

void gameAerialAssault_update()
{
    if( !gbUpdate() ) return;

    if( aerState == AER_STATE_TITLE )         aerUpdateTitle();
    else if( aerState == AER_STATE_MENU )     aerUpdateMenu();
    else if( aerState == AER_STATE_STATUS )   aerUpdateStatus();
    else if( aerState == AER_STATE_CONTROLS ) aerUpdateControls();
    else if( aerState == AER_STATE_OPTIONS )  aerUpdateOptions();
    else                                      aerUpdatePlay();

    gbRenderFrame();
}
