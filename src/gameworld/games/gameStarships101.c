#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// 101 Starships (Zoglu, license: none specified upstream - real source
// recovered directly from zoglu.net/site_files/101starships.zip, no GitHub
// repo). A vertically-scrolling space shooter: fly a small ship left/right/
// up/down, hold A to fire (two shots, up+down of the ship), release A to
// charge a five-shot super burst; destroy 101 real enemy waves (bezier-
// curve flight paths + a boss finale) across 5 real "STAGE" checkpoints
// without losing all 3 lives, then land a final rank (S down to D) on the
// score earned. Real upstream source is French-authored (function/variable
// names, comments) - every new comment in this port is written in English,
// but the real gameplay/formulas below are ported faithfully regardless of
// the source language.
//
// Real upstream multi-file consolidation: `_101starships.ino` (setup()/
// loop(), the real 101-entry/14-field enemy spawn table `ennemiSet[]`, the
// title-screen `logo` bitmap, and every music/SFX pattern table - see
// "SOUND" below), `_bitmaps.ino` (ship/shot/boss sprites), `affichages.ino`
// (HUD), `endscreen.ino`, `ennemis.ino`, `ennemis_tirs.ino`,
// `explosions.ino`, `perso.ino` (player), `tirs.ino` (player shots) - all
// merged into this one file, matching this project's own established
// multi-`.ino`-file consolidation precedent.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment). `byte`/`char` become plain `int`
// (avrCompat.h aliasing - real byte/char fields here always stay within
// safe ranges even as plain int, confirmed by tracing the bezier-curve
// coordinate math directly, not just assumed - see "REAL QUIRKS" #2/#3
// below for the two cases that needed real scrutiny). `PROGMEM`/
// `pgm_read_byte_near()` are dropped (no-ops in this shim, data kept as
// plain `int` arrays). `random(a,b)`/`random(n)` become `arand()`.
// Real Arduino `B00000000`-style binary literals across every bitmap table
// (`vaisseau`/`vaisseauS`/`vaisseauXS`/`vaisseau2`/`vaisseau3`/`vaisseau4`/
// `pt`/`shot`/`boss`/`logo`) and the entire 1414-entry `ennemiSet[]` table
// were converted to plain decimal via a one-off Python script (byte-for-
// byte, verified programmatically against the real source text - not
// hand-retyped), matching this project's own `gameShipwrek.c`-established
// precedent for large real PROGMEM tables.
//
// -----------------------------------------------------------------------
// STATE-MACHINE CONVERSION
// -----------------------------------------------------------------------
// Upstream's own real `setup()` calls a genuinely blocking
// `gb.titleScreen(F("   "), logo)` (a real, deliberately BLANK title
// string - three spaces - the logo bitmap alone carries the game's
// identity, plus the widget's own built-in "press A" hint) before
// `loop()` ever starts; the same real `initGame(byte retry)` function is
// also called again later, with `retry=1`, when the end screen's own
// "press A" retry is used (skipping the blocking title call that second
// time only). Converted here into two explicit states
// (`SHIPS_STATE_TITLE`/`SHIPS_STATE_GAME`), the same "blocking loop ->
// explicit resumable state" treatment `gamePong.c`/`gameShipwrek.c`
// already established. Since the only real difference `retry` ever made
// inside `initGame()` was whether the blocking title call ran first (every
// other line runs identically either way), the real reset logic is ported
// as a single parameterless `shipsResetGame()` - the TITLE state's own
// A-press handler calls it (mirroring `retry=0`), and the end screen's own
// "press A" retry handler calls it directly without visiting TITLE at all
// (mirroring `retry=1`). No hand-built title text is invented for the
// logo screen - upstream's own real title string is blank, and that
// choice is preserved exactly, unlike `gamePong.c`/`gameShipwrek.c` where
// upstream itself passed real title text to port.
//
// -----------------------------------------------------------------------
// SOUND - fully real, via the shared tracker/pattern/track engine
// -----------------------------------------------------------------------
// Every real upstream sound call site is now restored for real, via this
// shim's own real single-oscillator-per-channel tracker engine
// (`gbPlayPattern()`/`gbPlayTrack()`/`gbChangePatternSet()`) rather than
// approximated - no scope limit remains for this game's own sound.
//
// The continuous, real 2-channel background score
// (`gb.sound.playTrack()`/`changePatternSet()`, `track1`/`track2`
// (`shipsTrack1`/`shipsTrack2`), 24 real `p00`-`p23` pattern tables
// (`shipsPat00`-`shipsPat23`, wired up via `shipsPatternSet`)) now plays
// for real on channels 1/2, started from `shipsResetGame()` at the exact
// same point real `initGame()` does. Real upstream's own
// `!gb.sound.trackIsPlaying[1] && wait_end<10 && vies>0` check (loop()'s
// own "the track finished, since track1/track2 are 0xFFFF-terminated and
// don't loop on their own - restart it" trigger) is reproduced in
// `shipsUpdateGame()` by reading `gbTrackIsPlaying[1]` directly - a real,
// non-static `gamebuinoShim.c` global (the direct equivalent of real
// `Sound::trackIsPlaying[]`, itself a public member upstream reads the
// exact same way, not through an accessor). Unlike the sibling
// `gamebuino_classic_vircon32` build (a single translation unit, where
// `gbTrackIsPlaying` needs no forward declaration at all), this project
// compiles each game as its own separate translation unit against
// `gamebuinoShim.h`'s own public surface - `gbTrackIsPlaying` isn't part
// of that header's own extern list, so this file declares it itself
// (`extern bool gbTrackIsPlaying[];`, right next to where it's used) - the
// real global it points at is still `gamebuinoShim.c`'s own, nothing new
// was added there.
//
// The 6 named one-shot SFX (`player_shot`, `enn_shot`, `enn_hit`,
// `enn_destroy`, `player_destroy`, `player_super`) are real short tracker
// PATTERNS upstream, not simple `Sound::command()` envelopes - each now
// plays via a direct `gbPlayPattern(pattern, 0)` call
// (`shipsSfxShot()`/`shipsSfxEnemyShot()`/`shipsSfxHit()`/
// `shipsSfxDestroy()`/`shipsSfxSuperCharge()`/`shipsSfxPlayerDestroy()`
// below), with the real pattern data (`shipsSndPlayerShot` etc., copied
// word-for-word, not hand-transcribed - see "Sound data" below) rather
// than a representative single-note stand-in.
//
// Every other real sound call site (`gb.sound.playTick()`/`playOK()`/
// `playCancel()` - the phase-banner countdown ticks, pause/resume/quit,
// and the CONTINUE? prompt) already mapped 1:1 onto this shim's own
// identical `gbPlayTick()`/`gbPlayOK()`/`gbPlayCancel()` primitives with
// no approximation needed, unchanged by this pass. The two end-of-run
// "win"/"lose" jingles (`p20`/`p21` and `p22`/`p23`) now play for real via
// `gbStopTrack()`+`gbPlayPattern()` at the exact real `end_screen`
// transition point upstream fires them, replacing the earlier
// `gbPlayOK()`/`gbPlayCancel()` stinger stand-in.
//
// -----------------------------------------------------------------------
// REAL QUIRKS FOUND - preserved or normalized, and why
// -----------------------------------------------------------------------
// 1. Real `initPerso()` never resets `cadencetir`/`supershot`/
//    `supershot_nb` - only `x`/`y`/`repop`/`vies`/`destroy`/`wait_end`/
//    `wait_gameover` are touched, every single time `initGame()` runs
//    (fresh boot AND every retry/continue). This is a genuine, if minor,
//    real quirk: a player who retries or continues immediately after a
//    playthrough that ended with a partially-charged super shot starts
//    the next attempt with that same partial charge already banked.
//    Preserved verbatim here (`shipsInitPerso()` below matches real
//    `initPerso()` field-for-field) - only `gameStarships101_init()`
//    explicitly zeroes the whole player struct once, matching real
//    hardware's own one-time C++ static zero-initialization at program
//    start, since this shim's `shipsResetGame()` (unlike real hardware's
//    single long-running program) gets invoked repeatedly across a whole
//    cartridge session.
// 2. `boss_shots1`/`boss_shots2`/`boss_shots3` are real `byte` counters on
//    real hardware; ported here as plain `int` without reproducing 8-bit
//    wraparound. Checked, not just assumed safe: `boss_shots2`'s own gate
//    (`boss_shots2 >= 100 && y < 24 && y > 18`) is tied to the boss's own
//    short-period sine bob (`y = 21 + 16*sin(bossframe/6.0)`, oscillating
//    through the gate's range roughly every ~37 frames) - the gate fires
//    and resets the counter long before it could ever approach a real
//    byte's 256-wraparound point, so plain-`int` arithmetic here is
//    observably identical to the real 8-bit version, not just "probably
//    fine."
// 3. Bezier-curve coordinate fields (`Ennemi.x`/`.y`, real `byte`) use a
//    real "+100" coordinate-space shift baked into every one of
//    `ennemiSet[]`'s own control-point values specifically so a quadratic
//    Bezier's own convex-hull-bounded result never goes negative before
//    the later `x - 100` re-shift back to real screen space - traced
//    through directly (a quadratic Bezier's value is always a weighted
//    average of its 3 control points, so it can never leave their own
//    min/max range) rather than assumed safe, confirming plain `int`
//    fields need no special handling here despite real hardware using
//    byte/char types throughout this exact pipeline.
// 4. `newTir()`/`newTirEnnemi()`/`newExplosion()`'s real free-slot scan
//    (`while(arr[i].on==1) i++;`, with NO bound on `i` during the scan
//    itself - only clamped to slot 0 by a separate `if(i>=MAX) i=0;`
//    AFTER the scan) and the enemy-spawn loop's own `nextennemi` gap-read
//    (`ennemiSet[VARS_ENNEMIS*readennemi]` performed immediately after
//    `readennemi += 1`, reachable at exactly `readennemi==101`, one past
//    the table's real last valid row) are both genuine out-of-bounds
//    reads on real hardware - tolerated there as a harmless read of
//    whatever byte happens to sit next in flash/RAM, and in both cases
//    proven (not just assumed) to be truly inert: the free-slot scan's
//    own very next line already forces the index back to slot 0 once out
//    of range, and the spawn loop's own outer condition
//    (`readennemi < 101`) is already false by the time that garbage
//    gap-value would ever be read again, so it's discarded unread either
//    way. Both are guarded here (bounding the scan/read explicitly before
//    it can go out of range) since this target has no such tolerant
//    fallback for a genuine out-of-bounds array access - a safe,
//    zero-observable-difference normalization, not a behavior change.
// 5. The explosion animation's real `switch(frame)` case fallthrough
//    (case 0 falls into case 4 with no `break`; case 1 falls into case 3
//    with no `break`) is preserved verbatim - a real layered-detonation
//    effect (each of those two frames draws two concentric rings' worth
//    of pixels at once), not an oversight to "fix" with an added break.
// 6. The HUD's real super-shot charge bar
//    (`gb.display.fillRect(16,44,2*(supershot-10),3)`, called
//    unconditionally even while `supershot<10` makes the width negative)
//    relies on real `Display::fillRect()`'s own `int8_t` width parameter
//    and its own real per-pixel loop (`for(i=0;i<w;i++)`) simply never
//    executing for a negative `w` - confirmed directly against the real
//    Gamebuino-Classic source (`utility/Display.h`/`.cpp`), not assumed.
//    Ported as a direct, unguarded `gbFillRect()` call: this shim's own
//    `gbFillRect()`/`gbDrawFastHLine()` already have the identical
//    "loop doesn't execute for non-positive width" behavior.
// 7. The Button-C pause overlay is the one screen in this game where real
//    `gb.display.persistence=true` actually matters - every OTHER overlay
//    (the CONTINUE? prompt, the end screen) is drawn on top of a scene
//    that keeps naturally redrawing itself every tick anyway (upstream's
//    own function-call order alone already composites them correctly, no
//    persistence needed - traced through directly, not assumed). This
//    shim has no persistence primitive at all (`gbUpdate()` always clears
//    the framebuffer every tick, matching real hardware's own default
//    non-persistent behavior - see `gamebuinoShim.h`). Rather than freeze
//    the last real gameplay frame underneath the pause box, the paused
//    overlay here is drawn straight onto that tick's freshly-cleared
//    (white) background instead - a documented, cosmetic-only
//    simplification: the real dialog's own white boxes already cover
//    rows 0-41 of the LCD's 48, so the only visible difference is a blank
//    white margin in the small remaining strip instead of a frozen
//    starfield/ship there.
//
// -----------------------------------------------------------------------
// EEPROM
// -----------------------------------------------------------------------
// Real upstream stores a 2-byte LSB/MSB highscore at real EEPROM addresses
// 0/1 - ported directly via this shim's own `eeprom_read_byte()`/
// `eeprom_write_byte()` (matching `gameUfoRace.c`'s own established real
// EEPROM-consumer precedent), same addresses, same LSB/MSB packing. Real
// upstream's own `if(highscore>60000) highscore=0;` guard doubles as this
// shim's own real "fresh/unwritten card reads as 0xFF bytes" fallback
// (0xFF|0xFF00 = 65535 > 60000) with no extra handling needed - matching
// real hardware's own factory-erased EEPROM state exactly.

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------

#define SHIPS_MAX_TIRS 20
#define SHIPS_MAX_TIRS_ENNEMIS 35
#define SHIPS_MAX_ENNEMIS 8
#define SHIPS_MAX_EXPLOSIONS 3
#define SHIPS_VARS_ENNEMIS 14
#define SHIPS_STARS 12
#define SHIPS_PLAYER_SPD1 3
#define SHIPS_PLAYER_SPD2 3
#define SHIPS_TOTENNEMIS 101

// Field offsets within one 14-int row of shipsEnemySet[] (see real
// upstream's own comment: "frame d'apparition, vie, image, bezier(7),
// tirs(4)").
#define SHIPS_EOFF_GAP 0
#define SHIPS_EOFF_VIE 1
#define SHIPS_EOFF_IMAGE 2
#define SHIPS_EOFF_BEZX1 3
#define SHIPS_EOFF_BEZX2 4
#define SHIPS_EOFF_BEZX3 5
#define SHIPS_EOFF_BEZY1 6
#define SHIPS_EOFF_BEZY2 7
#define SHIPS_EOFF_BEZY3 8
#define SHIPS_EOFF_BEZTOT 9
#define SHIPS_EOFF_TIRNB 10
#define SHIPS_EOFF_TIRANG1 11
#define SHIPS_EOFF_TIRANG2 12
#define SHIPS_EOFF_TIRTOT 13

enum ShipsState
{
    SHIPS_STATE_TITLE = 0,
    SHIPS_STATE_GAME = 1
};

// -----------------------------------------------------------------------
// Structs - direct ports of real upstream's own Player/Tir/Ennemi/
// TirEnnemi/Explosion structs (_101starships.ino), field-for-field.
// -----------------------------------------------------------------------

typedef struct{
    int x, y;
    bool destroy;
    int cadencetir;
    int repop;
    int vies;
    int waitEnd;
    int waitGameover;
    int supershot;
    int supershotNb;
} ShipsPlayer;

typedef struct{
    bool on;
    int x, y;
    int xvit, yvit;
} ShipsTir;

typedef struct{
    int x1, x2, x3;
    int y1, y2, y3;
    int totFrames;
} ShipsBezier;

typedef struct{
    int nb;
    int ang1, ang2;
    int totFrames;
} ShipsEnemyTir;

typedef struct{
    bool on;
    int x, y;
    int vie;
    int image;
    ShipsBezier bez;
    int bezFrame;
    ShipsEnemyTir tir;
    int tirsFrame;
    bool justhit;
} ShipsEnnemi;

typedef struct{
    bool on;
    int x, y;
    int xvit, yvit;
} ShipsTirEnnemi;

typedef struct{
    int on;
    int x, y;
    int frame;
} ShipsExplosion;

// -----------------------------------------------------------------------
// Bitmaps - byte-for-byte converted from upstream's own real PROGMEM
// tables (_bitmaps.ino / the logo in _101starships.ino) via a one-off
// Python script (Arduino B-binary literals -> decimal, {width,height}
// header preserved) - see this file's own header comment.
// -----------------------------------------------------------------------

int shipsVaisseau[7] =
{
    8,5,192,112,72,112,192,
};

int shipsVaisseauS[7] =
{
    8,5,0,240,72,240,0,
};

int shipsVaisseauXs[7] =
{
    8,5,0,0,248,0,0,
};

int shipsVaisseau2[8] =
{
    8,6,252,48,120,120,48,252,
};

int shipsVaisseau3[8] =
{
    8,6,28,112,252,252,112,28,
};

int shipsVaisseau4[8] =
{
    8,6,24,252,56,56,252,24,
};

int shipsPt[3] =
{
    8,1,192,
};

int shipsShotBmp[5] =
{
    8,3,224,224,224,
};

int shipsBossBmp[28] =
{
    16,13,32,0,127,224,47,192,7,128,51,0,31,128,249,128,
    31,128,51,0,7,128,47,192,127,224,32,0,
};

int shipsLogo[242] =
{
    64,30,30,63,224,120,0,0,7,0,62,255,248,248,0,0,
    28,0,126,248,249,248,192,0,63,0,254,248,251,248,115,128,
    63,0,222,248,251,120,72,63,28,0,30,248,248,120,115,140,
    7,63,30,248,248,120,192,30,0,12,30,248,248,120,0,30,
    0,30,30,255,248,120,0,12,0,30,30,127,240,120,0,63,
    0,12,0,0,0,0,0,0,0,63,0,0,96,0,0,192,
    192,0,0,0,96,0,0,192,0,0,0,30,243,207,158,248,
    223,30,0,48,96,110,48,204,217,176,0,48,96,108,48,204,
    217,176,0,28,99,236,28,204,217,156,0,6,102,108,6,204,
    217,134,0,6,102,108,6,204,217,134,0,60,51,236,60,204,
    223,60,0,0,0,0,0,0,24,0,0,0,0,0,0,0,
    24,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,
    0,4,0,0,0,0,112,4,0,4,0,0,21,84,39,117,
    70,118,0,0,29,220,69,85,69,100,0,0,29,221,119,117,
    213,119,0,0,0,0,0,16,0,0,0,0,0,0,0,112,
    0,0,
};

// Real 101-entry / 14-field-per-entry enemy spawn table (real upstream's
// own comment: "frame d'apparition, vie, image, bezier(7), tirs(4)") -
// index 100 (the last row) is the real boss. See SHIPS_EOFF_* above for
// the per-field layout.
int shipsEnemySet[1414] =
{
    5,2,2,190,145,90,125,140,125,130,1,17,18,200,10,2,
    2,190,145,90,115,100,115,130,1,18,19,200,50,3,0,190,
    145,90,104,150,94,120,1,19,19,50,20,3,0,190,145,90,
    124,100,114,120,1,18,18,50,50,3,0,190,145,90,145,100,
    115,100,2,16,19,40,10,3,0,190,145,90,145,100,115,100,
    2,16,19,40,50,2,2,190,145,90,134,140,132,120,1,20,
    20,50,2,2,2,190,145,90,110,100,112,120,1,16,16,50,
    30,3,0,190,145,90,115,110,115,120,1,17,17,50,2,3,
    0,190,145,90,129,134,129,120,1,19,19,50,50,3,0,190,
    145,90,130,120,114,100,3,15,20,25,20,3,0,190,145,90,
    130,120,114,100,3,15,20,25,60,2,2,195,175,145,110,180,
    70,100,2,16,20,50,20,2,2,195,175,145,130,60,170,100,
    2,16,20,45,10,2,2,195,175,145,110,180,70,100,2,17,
    19,50,20,2,2,195,175,145,130,60,170,100,2,17,19,45,
    50,3,0,190,145,90,114,120,114,100,2,17,19,20,20,3,
    0,190,145,90,114,120,114,100,2,17,19,20,34,3,0,190,
    145,90,120,140,135,90,3,17,23,40,15,3,0,190,145,90,
    120,140,135,90,3,17,23,40,15,3,0,190,145,90,120,140,
    135,90,3,17,23,40,30,3,0,190,145,90,110,145,124,90,
    2,17,19,30,15,3,0,190,145,90,140,95,115,90,2,17,
    19,30,80,2,2,200,175,125,120,165,85,90,4,15,21,40,
    10,2,2,200,175,125,120,165,85,90,4,16,20,40,10,2,
    2,200,175,125,120,165,85,90,4,16,20,40,70,7,1,190,
    170,90,130,120,115,120,6,15,21,25,70,3,0,190,145,90,
    103,108,98,80,2,14,16,30,10,3,0,190,145,90,103,108,
    98,80,2,14,16,30,2,3,0,190,145,90,136,131,141,80,
    2,20,22,30,10,3,0,190,145,90,136,131,141,80,2,22,
    20,30,60,2,2,190,165,120,125,80,150,80,2,15,21,20,
    8,2,2,190,165,120,125,80,150,80,1,17,17,40,8,2,
    2,190,165,120,125,80,150,80,1,16,16,40,8,2,2,190,
    165,120,125,80,150,80,2,15,21,20,8,2,2,190,165,120,
    125,80,150,80,1,17,17,40,8,2,2,190,165,120,125,80,
    150,80,1,16,16,40,40,2,2,190,165,120,115,160,80,80,
    3,15,21,25,10,2,2,190,165,120,115,160,80,80,3,16,
    20,25,10,2,2,190,165,120,115,160,80,80,3,16,20,25,
    10,2,2,190,165,120,115,160,80,80,3,15,21,25,40,3,
    0,190,145,90,120,100,110,90,3,13,19,25,15,3,0,190,
    145,90,120,100,110,90,3,13,19,25,15,3,0,190,145,90,
    120,100,110,90,3,13,19,25,60,2,2,190,145,90,115,95,
    110,70,2,14,16,40,1,2,2,190,145,90,125,145,130,70,
    2,20,22,40,30,2,2,190,145,90,115,95,110,70,2,14,
    16,40,1,2,2,190,145,90,125,145,130,70,2,20,22,40,
    80,5,1,190,170,90,110,120,105,110,4,14,22,35,5,5,
    1,190,170,90,130,120,135,110,4,14,22,35,80,3,0,195,
    165,95,120,140,130,80,3,19,23,40,15,3,0,195,165,95,
    120,140,130,80,3,19,23,40,15,3,0,195,165,95,120,140,
    130,80,3,17,21,40,10,3,0,195,165,95,120,100,110,80,
    3,13,17,40,15,3,0,195,165,95,120,100,110,80,3,13,
    17,40,15,3,0,195,165,95,120,100,110,90,3,15,19,40,
    40,2,2,190,145,90,135,135,139,110,2,16,20,25,2,2,
    2,190,145,90,125,125,127,110,2,16,20,25,2,2,2,190,
    145,90,115,115,113,110,2,16,20,25,2,2,2,190,145,90,
    105,105,101,110,2,16,20,25,20,2,2,190,145,90,130,130,
    128,110,2,16,20,25,2,2,2,190,145,90,120,120,120,110,
    2,16,20,25,2,2,2,190,145,90,110,110,112,110,2,16,
    20,25,70,7,1,190,170,90,130,120,115,120,6,15,21,25,
    50,3,0,190,165,90,120,140,130,80,3,19,23,40,15,3,
    0,190,165,90,120,140,130,80,3,19,23,40,15,3,0,190,
    165,90,120,140,130,80,3,17,21,40,10,3,0,190,165,90,
    120,100,110,80,3,13,17,40,15,3,0,190,165,90,120,100,
    110,80,3,13,17,40,15,3,0,190,165,90,120,100,110,90,
    3,15,19,40,80,2,2,195,175,145,110,170,70,70,3,15,
    21,20,20,2,2,195,175,145,130,70,170,70,3,15,21,15,
    10,2,2,195,175,145,110,170,70,70,3,16,20,20,20,2,
    2,195,175,145,130,70,170,70,3,15,21,15,20,3,0,190,
    145,90,110,145,124,90,2,16,20,15,10,3,0,190,145,90,
    140,95,115,90,2,16,20,15,70,5,1,190,170,90,110,120,
    105,110,4,14,22,35,5,5,1,190,170,90,130,120,135,110,
    4,14,22,35,30,7,1,190,170,90,120,120,120,80,6,15,
    21,25,100,2,2,190,145,90,118,93,113,80,3,13,17,40,
    1,2,2,190,145,90,125,150,130,80,3,19,23,40,20,2,
    2,190,145,90,118,93,113,80,3,13,17,40,1,2,2,190,
    145,90,125,150,130,80,3,19,23,40,20,2,2,190,145,90,
    118,93,113,80,3,13,17,40,1,2,2,190,145,90,125,150,
    130,80,3,19,23,40,60,3,0,190,145,90,130,140,128,140,
    3,20,22,25,2,3,0,190,145,90,110,100,112,140,3,16,
    14,25,10,3,0,190,145,90,115,110,115,140,3,17,15,25,
    2,3,0,190,145,90,125,130,125,140,3,19,21,25,90,5,
    1,190,170,90,130,120,115,100,4,15,21,30,20,3,0,190,
    165,90,120,140,130,80,3,19,23,40,10,3,0,190,165,90,
    120,140,130,80,3,19,23,40,10,3,0,190,165,90,120,140,
    130,80,3,17,21,40,10,3,0,190,165,90,120,100,110,80,
    3,13,17,40,10,3,0,190,165,90,120,100,110,80,3,13,
    17,40,10,3,0,190,165,90,120,100,110,90,3,15,19,40,
    100,7,1,190,170,90,110,120,105,120,3,14,22,25,5,7,
    1,190,170,90,130,120,135,120,3,14,22,25,10,5,1,190,
    170,90,110,120,105,130,4,12,24,25,5,5,1,190,170,90,
    130,120,135,130,4,12,24,25,120,30,3,194,188,178,122,122,
    122,20,0,12,24,255,
};

// -----------------------------------------------------------------------
// Text - the two real "STAGE" banner messages (revealed with a real
// typewriter effect via shipsPrintTruncated() below, matching upstream's
// own real `String::substring(0, n)` technique) and a shared scratch
// buffer.
// -----------------------------------------------------------------------

char shipsStageMsg[10] = "STAGE  /5";
char shipsFinalStageMsg[12] = "FINAL STAGE";
char shipsScratchText[16];

// -----------------------------------------------------------------------
// Global state - real upstream's own module-level globals, `ships`-
// prefixed.
// -----------------------------------------------------------------------

int shipsState;

ShipsPlayer shipsPerso;
ShipsTir shipsTirs[SHIPS_MAX_TIRS];
ShipsEnnemi shipsEnnemis[SHIPS_MAX_ENNEMIS];
ShipsTirEnnemi shipsTirsEnnemis[SHIPS_MAX_TIRS_ENNEMIS];
ShipsExplosion shipsExplosions[SHIPS_MAX_EXPLOSIONS];

int shipsNbFrames;
bool shipsEndScreen;
bool shipsPause;
int shipsWaitEndScreen;
int shipsBonusEndScreen;
int shipsScore, shipsScoreAff;
int shipsShipsDestroyed;
int shipsStar[SHIPS_STARS * 2];
int shipsStarSpeed;
int shipsPhase, shipsCheckpoint, shipsPhaseDisplay;
bool shipsContinu;
int shipsNextEnnemi;
int shipsReadEnnemi;
bool shipsEndGame; // real upstream's own "EndGame" - true while any enemy or enemy shot is still active on screen (see header comment on updateTirsEnnemis()/updateEnnemis()'s own real interplay)
int shipsBossFrame;
int shipsBossShots1, shipsBossShots2, shipsBossShots3;
int shipsHighscore;
bool shipsScoreSaved;
bool shipsNewHighscore;
int shipsRankLetter;

// -----------------------------------------------------------------------
// Text helpers
// -----------------------------------------------------------------------

void shipsPrint( char* text, int x, int y )
{
    gbCursorX = x;
    gbCursorY = y;
    gbPrintString( text );
}

// Draws one real button-icon glyph (21/22/23 = A/B/C button icons in the
// real font's own low-ASCII icon range) then manually advances the cursor
// - the same proven-safe technique `gameGruniozerca.c` already established
// for inline icon glyphs, since this dialect's own string-literal octal-
// escape support (`"\25"`) was never proven out.
void shipsPrintIcon( int code )
{
    gbDrawChar( code, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
}

// Direct port of real upstream's own `String::substring(0, maxChars)`
// reveal technique (the "STAGE"/"FINAL STAGE" banners) - copies at most
// the first maxChars glyphs of a null-terminated message into a shared
// scratch buffer, then prints that.
void shipsPrintTruncated( char* text, int x, int y, int maxChars )
{
    int i = 0;
    while( text[ i ] != 0 && i < maxChars && i < 15 )
    {
        shipsScratchText[ i ] = text[ i ];
        i = i + 1;
    }
    shipsScratchText[ i ] = 0;
    gbCursorX = x;
    gbCursorY = y;
    gbPrintString( shipsScratchText );
}

// Direct port of real upstream's own `displayInt()` - zero-pads `value` on
// the left up to `fig` digits (gbPrintNumber() alone has no padding).
void shipsDisplayInt( int value, int tx, int ty, int fig )
{
    char buf[16];
    itoa( value, buf, 10 );
    int len = 0;
    while( buf[ len ] != 0 ) len = len + 1;
    int pad = fig - len;

    gbCursorX = tx;
    gbCursorY = ty;
    int i;
    for( i = 0; i < pad; i = i + 1 )
    {
        gbDrawChar( 48, gbCursorX, gbCursorY ); // '0'
        gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    }
    gbPrintString( buf );
}

// -----------------------------------------------------------------------
// Sound data - real upstream's own SFX/music tracker patterns and tracks
// (`_101starships.ino`), copied word-for-word via a script cross-checked
// against each array's own real declared element count (see this file's
// own header comment "SOUND"), not hand-transcribed.
// -----------------------------------------------------------------------

// Real one-shot SFX patterns (short tracker patterns, not
// Sound::command() envelopes).
int shipsSndEnnDestroy[ 5 ] = { 0x8045,0x8851,0x8241,0x538,0x0000 };
int shipsSndEnnShot[ 5 ] = { 0x8005,0x884D,0x81C1,0x250,0x0000 };
int shipsSndPlayerShot[ 5 ] = { 0x8005,0x8141,0x14C,0x154,0x0000 };
int shipsSndPlayerSuper[ 6 ] = { 0x8005,0x81C1,0x164,0x164,0x164,0x0000 };
int shipsSndEnnHit[ 4 ] = { 0x8005,0x81C1,0x108,0x0000 };
int shipsSndPlayerDestroy[ 5 ] = { 0x8045,0x8891,0x8241,0x608,0x0000 };

// Real background-music/jingle patterns (p00-p19 = looping 2-channel
// stage music, p20/p21 = win jingle, p22/p23 = lose jingle).
int shipsPat00[ 27 ] = { 0x8005,0x8101,0x410,0x2FC,0x210,0x2FC,0x210,0x4FC,0x20C,0x4FC,0x210,0x4FC,0x218,0x2FC,0x420,0x2FC,0x220,0x4FC,0x220,0x2FC,0x418,0x2FC,0x220,0x2FC,0x220,0x428,0x000 };
int shipsPat01[ 19 ] = { 0x8005,0x8101,0x12C,0x5FC,0x12C,0x1FC,0x12C,0x3FC,0x140,0x3FC,0x12C,0x5FC,0x12C,0x1FC,0x12C,0x3FC,0x140,0x3FC,0x0000 };
int shipsPat02[ 8 ] = { 0x8005,0x8101,0xC5C,0x250,0x258,0x85C,0x870,0x0000 };
int shipsPat03[ 15 ] = { 0x8005,0x8101,0xC40,0x23C,0x240,0x63C,0x640,0x448,0xC50,0x23C,0x240,0x648,0x650,0x458,0x0000 };
int shipsPat04[ 7 ] = { 0x8005,0x8101,0x106C,0x664,0x65C,0x458,0x0000 };
int shipsPat05[ 19 ] = { 0x8005,0x8101,0x13C,0x5FC,0x13C,0x1FC,0x13C,0x3FC,0x11C,0x3FC,0x13C,0x5FC,0x13C,0x1FC,0x13C,0x3FC,0x11C,0x3FC,0x0000 };
int shipsPat06[ 8 ] = { 0x8005,0x8101,0xC5C,0x258,0x264,0x86C,0x870,0x0000 };
int shipsPat07[ 19 ] = { 0x8005,0x8101,0x12C,0x5FC,0x12C,0x1FC,0x12C,0x3FC,0x140,0x3FC,0x13C,0x5FC,0x13C,0x1FC,0x13C,0x3FC,0x11C,0x3FC,0x0000 };
int shipsPat08[ 7 ] = { 0x8005,0x8101,0x106C,0x664,0x66C,0x458,0x0000 };
int shipsPat09[ 19 ] = { 0x8005,0x8101,0x13C,0x5FC,0x13C,0x1FC,0x13C,0x3FC,0x120,0x3FC,0x134,0x5FC,0x134,0x1FC,0x134,0x3FC,0x118,0x3FC,0x0000 };
int shipsPat10[ 8 ] = { 0x8005,0x8101,0xC5C,0x258,0x250,0x864,0x86C,0x0000 };
int shipsPat11[ 19 ] = { 0x8005,0x8101,0x12C,0x5FC,0x12C,0x1FC,0x12C,0x3FC,0x140,0x3FC,0x134,0x5FC,0x134,0x1FC,0x13C,0x3FC,0x1EC,0x3FC,0x0000 };
int shipsPat12[ 9 ] = { 0x8005,0x8101,0x870,0x46C,0x470,0x878,0x45C,0x48C,0x0000 };
int shipsPat13[ 19 ] = { 0x8005,0x8101,0x140,0x3FC,0x140,0x3FC,0x13C,0x3FC,0x13C,0x3FC,0x148,0x3FC,0x148,0x3FC,0x15C,0x3FC,0x13C,0x3FC,0x0000 };
int shipsPat14[ 9 ] = { 0x8005,0x8101,0x640,0x650,0x45C,0x648,0x658,0x464,0x0000 };
int shipsPat15[ 11 ] = { 0x8005,0x8101,0x410,0x2FC,0x210,0x8FC,0x418,0x2FC,0x23C,0x8FC,0x0000 };
int shipsPat16[ 9 ] = { 0x8005,0x8101,0x65C,0x66C,0x478,0x664,0x66C,0x458,0x0000 };
int shipsPat17[ 11 ] = { 0x8005,0x8101,0x42C,0x2FC,0x22C,0x8FC,0x434,0x2FC,0x234,0x8FC,0x0000 };
int shipsPat18[ 9 ] = { 0x8005,0x8101,0x65C,0x66C,0x478,0x66C,0x678,0x480,0x0000 };
int shipsPat19[ 11 ] = { 0x8005,0x8101,0x42C,0x2FC,0x22C,0x8FC,0x440,0x2FC,0x240,0x8FC,0x0000 };
int shipsPat20[ 11 ] = { 0x8241,0x8005,0x25C,0x264,0x440,0x848,0x240,0x250,0x458,0x85C,0x0000 };
int shipsPat21[ 15 ] = { 0x8241,0x8005,0x15C,0x5FC,0x15C,0x1FC,0x15C,0x7FC,0x140,0x5FC,0x140,0x1FC,0x140,0x7FC,0x0000 };
int shipsPat22[ 8 ] = { 0x8241,0x8005,0x28C,0x288,0x280,0x278,0x870,0x0000 };
int shipsPat23[ 12 ] = { 0x8241,0x8005,0x15C,0x1FC,0x158,0x1FC,0x150,0x1FC,0x148,0x1FC,0x840,0x0000 };

// Real patternSet[]/track1[]/track2[] - the pattern-ID lookup table
// shared by both music channels, and each channel's own real
// pattern-sequence track (a 0xFFFF-terminated list of pattern indices
// into shipsPatternSet, matching real Sound::updateTrack()'s own real
// word format exactly).
int* shipsPatternSet[ 24 ] = { shipsPat00,shipsPat01,shipsPat02,shipsPat03,shipsPat04,shipsPat05,shipsPat06,shipsPat07,shipsPat08,shipsPat09,shipsPat10,shipsPat11,shipsPat12,shipsPat13,shipsPat14,shipsPat15,shipsPat16,shipsPat17,shipsPat18,shipsPat19,shipsPat20,shipsPat21,shipsPat22,shipsPat23 };
int shipsTrack1[ 16 ] = { 0,2,4,6,8,2,4,10,12,14,16,14,18,14,16,0xFFFF };
int shipsTrack2[ 16 ] = { 3,1,5,7,9,1,5,11,13,15,17,15,19,15,17,0xFFFF };

// -----------------------------------------------------------------------
// Sound - one-shot SFX now play via the real gbPlayPattern() tracker
// engine, matching upstream's own real gb.sound.playPattern(X,0) call
// shape exactly (see this file's own header comment "SOUND").
// -----------------------------------------------------------------------

void shipsSfxShot() { gbPlayPattern( shipsSndPlayerShot, 0 ); }      // player_shot
void shipsSfxEnemyShot() { gbPlayPattern( shipsSndEnnShot, 0 ); }    // enn_shot
void shipsSfxHit() { gbPlayPattern( shipsSndEnnHit, 0 ); }           // enn_hit
void shipsSfxDestroy() { gbPlayPattern( shipsSndEnnDestroy, 0 ); }   // enn_destroy
void shipsSfxSuperCharge() { gbPlayPattern( shipsSndPlayerSuper, 0 ); } // player_super
void shipsSfxPlayerDestroy() { gbPlayPattern( shipsSndPlayerDestroy, 0 ); } // player_destroy

// -----------------------------------------------------------------------
// Stars - direct port of real upstream's own updateStars() (the real
// "+100" x-coordinate shift is kept verbatim for fidelity, though this
// dialect's plain `int` has no need of it - see header comment #3).
// -----------------------------------------------------------------------

void shipsUpdateStars( int dx )
{
    int j;
    for( j = 0; j < SHIPS_STARS; j = j + 1 )
    {
        shipsStar[ j * 2 ] = shipsStar[ j * 2 ] - dx;
        if( shipsStar[ j * 2 ] < 100 )
        {
            shipsStar[ j * 2 ] = shipsStar[ j * 2 ] + LCDWIDTH;
            shipsStar[ j * 2 + 1 ] = arand( LCDHEIGHT );
        }
        gbDrawPixel( shipsStar[ j * 2 ] - 100, shipsStar[ j * 2 + 1 ] );
    }
}

// -----------------------------------------------------------------------
// Explosions - direct port of real upstream's own newExplosion()/
// updateExplosions(), including the real case-0->4 and case-1->3 switch
// fallthroughs (see header comment #5).
// -----------------------------------------------------------------------

void shipsNewExplosion( int x, int y )
{
    int i = 0;
    while( i < SHIPS_MAX_EXPLOSIONS && shipsExplosions[ i ].on ) i = i + 1;
    if( i >= SHIPS_MAX_EXPLOSIONS ) i = 0;
    shipsExplosions[ i ].on = 1;
    shipsExplosions[ i ].x = x;
    shipsExplosions[ i ].y = y;
    shipsExplosions[ i ].frame = 0;
}

void shipsUpdateExplosions()
{
    int i;
    for( i = 0; i < SHIPS_MAX_EXPLOSIONS; i = i + 1 )
    {
        if( shipsExplosions[ i ].on )
        {
            int ex = shipsExplosions[ i ].x;
            int ey = shipsExplosions[ i ].y;

            switch( shipsExplosions[ i ].frame )
            {
            case 5:
                gbDrawPixel( ex, ey );
                gbDrawPixel( ex + 6, ey + 6 );
                gbDrawPixel( ex - 6, ey - 6 );
                gbDrawPixel( ex + 6, ey - 6 );
                gbDrawPixel( ex - 6, ey + 6 );
                break;

            case 0:
                gbDrawPixel( ex, ey );
                gbDrawPixel( ex, ey + 1 );
                gbDrawPixel( ex, ey - 1 );
                gbDrawPixel( ex + 1, ey );
                gbDrawPixel( ex - 1, ey );
                // falls through to case 4 - real upstream layered effect, see header comment #5
            case 4:
                gbDrawPixel( ex + 3, ey + 3 );
                gbDrawPixel( ex - 3, ey + 3 );
                gbDrawPixel( ex + 3, ey - 3 );
                gbDrawPixel( ex - 3, ey - 3 );
                gbDrawPixel( ex + 5, ey + 5 );
                gbDrawPixel( ex - 5, ey + 5 );
                gbDrawPixel( ex + 5, ey - 5 );
                gbDrawPixel( ex - 5, ey - 5 );
                gbDrawPixel( ex - 4, ey );
                gbDrawPixel( ex + 4, ey );
                gbDrawPixel( ex, ey + 4 );
                gbDrawPixel( ex, ey - 4 );
                break;

            case 1:
                gbDrawPixel( ex, ey + 1 );
                gbDrawPixel( ex - 1, ey + 1 );
                gbDrawPixel( ex + 1, ey + 1 );
                gbDrawPixel( ex, ey - 1 );
                gbDrawPixel( ex - 1, ey - 1 );
                gbDrawPixel( ex + 1, ey - 1 );
                gbDrawPixel( ex - 1, ey );
                gbDrawPixel( ex + 1, ey );
                gbDrawPixel( ex - 2, ey );
                gbDrawPixel( ex - 3, ey );
                gbDrawPixel( ex + 2, ey );
                gbDrawPixel( ex + 3, ey );
                gbDrawPixel( ex, ey + 2 );
                gbDrawPixel( ex, ey + 3 );
                gbDrawPixel( ex, ey - 2 );
                gbDrawPixel( ex, ey - 3 );
                gbDrawPixel( ex + 2, ey + 2 );
                gbDrawPixel( ex - 2, ey + 2 );
                gbDrawPixel( ex + 2, ey - 2 );
                gbDrawPixel( ex - 2, ey - 2 );
                // falls through to case 3 - real upstream layered effect, see header comment #5
            case 3:
                gbDrawPixel( ex - 5, ey );
                gbDrawPixel( ex + 5, ey );
                gbDrawPixel( ex, ey + 5 );
                gbDrawPixel( ex, ey - 5 );
                gbDrawPixel( ex + 4, ey + 4 );
                gbDrawPixel( ex - 4, ey + 4 );
                gbDrawPixel( ex + 4, ey - 4 );
                gbDrawPixel( ex - 4, ey - 4 );
                break;

            case 2:
                gbDrawPixel( ex, ey + 1 );
                gbDrawPixel( ex - 1, ey + 1 );
                gbDrawPixel( ex + 1, ey + 1 );
                gbDrawPixel( ex, ey - 1 );
                gbDrawPixel( ex - 1, ey - 1 );
                gbDrawPixel( ex + 1, ey - 1 );
                gbDrawPixel( ex - 1, ey );
                gbDrawPixel( ex + 1, ey );
                gbDrawPixel( ex - 2, ey );
                gbDrawPixel( ex - 4, ey );
                gbDrawPixel( ex + 2, ey );
                gbDrawPixel( ex + 4, ey );
                gbDrawPixel( ex, ey + 2 );
                gbDrawPixel( ex, ey + 4 );
                gbDrawPixel( ex, ey - 2 );
                gbDrawPixel( ex, ey - 4 );
                gbDrawPixel( ex + 3, ey + 3 );
                gbDrawPixel( ex - 3, ey + 3 );
                gbDrawPixel( ex + 3, ey - 3 );
                gbDrawPixel( ex - 3, ey - 3 );
                break;
            }

            shipsExplosions[ i ].frame = shipsExplosions[ i ].frame + 1;
            if( shipsExplosions[ i ].frame >= 6 ) shipsExplosions[ i ].on = 0;
        }
    }
}

// -----------------------------------------------------------------------
// Player shots - direct port of real upstream's own newTir()/updateTirs()
// (tirs.ino). Position is stored *10 (real upstream fixed-point sub-pixel
// precision), drawn at x/10,y/10.
// -----------------------------------------------------------------------

void shipsNewTir( int vx, int vy, int diffy )
{
    int i = 0;
    while( i < SHIPS_MAX_TIRS && shipsTirs[ i ].on ) i = i + 1; // bounded scan - see header comment #4
    if( i >= SHIPS_MAX_TIRS ) i = 0;
    shipsTirs[ i ].on = true;
    shipsTirs[ i ].x = ( shipsPerso.x - 1 ) * 10;
    shipsTirs[ i ].y = ( shipsPerso.y - diffy ) * 10 + vy;
    shipsTirs[ i ].xvit = vx;
    shipsTirs[ i ].yvit = vy;
    shipsSfxShot();
}

void shipsUpdateTirs()
{
    int i;
    for( i = 0; i < SHIPS_MAX_TIRS; i = i + 1 )
    {
        if( shipsTirs[ i ].on )
        {
            shipsTirs[ i ].x = shipsTirs[ i ].x + shipsTirs[ i ].xvit;
            shipsTirs[ i ].y = shipsTirs[ i ].y + shipsTirs[ i ].yvit;
            gbDrawFastHLine( shipsTirs[ i ].x / 10, shipsTirs[ i ].y / 10, 3 );
            if( shipsTirs[ i ].x / 10 > LCDWIDTH - 7 || shipsTirs[ i ].y / 10 < 0 || shipsTirs[ i ].y / 10 > LCDHEIGHT - 1 )
              shipsTirs[ i ].on = false;
        }
    }
}

// -----------------------------------------------------------------------
// Enemy shots - direct port of real upstream's own newTirEnnemi()/
// updateTirsEnnemis() (ennemis_tirs.ino).
// -----------------------------------------------------------------------

void shipsNewTirEnnemi( int x, int y, int vx, int vy )
{
    int i = 0;
    while( i < SHIPS_MAX_TIRS_ENNEMIS && shipsTirsEnnemis[ i ].on ) i = i + 1; // bounded scan - see header comment #4
    if( i >= SHIPS_MAX_TIRS_ENNEMIS ) i = 0;
    shipsTirsEnnemis[ i ].on = true;
    shipsTirsEnnemis[ i ].x = x * 10;
    shipsTirsEnnemis[ i ].y = y * 10;
    shipsTirsEnnemis[ i ].xvit = vx;
    shipsTirsEnnemis[ i ].yvit = vy;
}

void shipsUpdateTirsEnnemis()
{
    int i;
    for( i = 0; i < SHIPS_MAX_TIRS_ENNEMIS; i = i + 1 )
    {
        if( shipsTirsEnnemis[ i ].on )
        {
            shipsEndGame = true;
            shipsTirsEnnemis[ i ].x = shipsTirsEnnemis[ i ].x + shipsTirsEnnemis[ i ].xvit;
            shipsTirsEnnemis[ i ].y = shipsTirsEnnemis[ i ].y + shipsTirsEnnemis[ i ].yvit;
            gbDrawBitmap( shipsTirsEnnemis[ i ].x / 10 - 1, shipsTirsEnnemis[ i ].y / 10 - 1, shipsShotBmp );

            if( !shipsPerso.destroy )
            {
                int xdiff = shipsPerso.x - shipsTirsEnnemis[ i ].x / 10;
                int ydiff = shipsPerso.y - shipsTirsEnnemis[ i ].y / 10;
                if( xdiff <= 2 && xdiff >= -1 && ydiff <= 1 && ydiff >= -1 && shipsPerso.repop <= 0 )
                {
                    shipsPerso.destroy = true;
                    shipsSfxPlayerDestroy();
                }
            }

            if( shipsTirsEnnemis[ i ].x / 10 < -2 || shipsTirsEnnemis[ i ].x / 10 > LCDWIDTH + 2 ||
                shipsTirsEnnemis[ i ].y / 10 < -2 || shipsTirsEnnemis[ i ].y / 10 > LCDHEIGHT - 6 )
              shipsTirsEnnemis[ i ].on = false;
        }
    }
}

// -----------------------------------------------------------------------
// Enemy spawning - direct port of real upstream's own "Nouvel ennemi !"
// block at the tail of updateEnnemis() (ennemis.ino).
// -----------------------------------------------------------------------

void shipsSpawnNextEnnemi()
{
    while( shipsNbFrames >= shipsNextEnnemi && shipsReadEnnemi < SHIPS_TOTENNEMIS && shipsPerso.vies > 0 )
    {
        int i = 0;
        while( i < SHIPS_MAX_ENNEMIS && shipsEnnemis[ i ].on ) i = i + 1; // bounded scan - see header comment #4
        if( i >= SHIPS_MAX_ENNEMIS ) i = 0;

        int base = SHIPS_VARS_ENNEMIS * shipsReadEnnemi;

        shipsEnnemis[ i ].on = true;
        shipsEnnemis[ i ].x = 255; // placeholder - overwritten by the bezier formula before this enemy is ever drawn
        shipsEnnemis[ i ].y = 255;
        shipsEnnemis[ i ].bezFrame = 0;
        shipsEnnemis[ i ].tirsFrame = 0;

        shipsEnnemis[ i ].vie = shipsEnemySet[ base + SHIPS_EOFF_VIE ];
        shipsEnnemis[ i ].image = shipsEnemySet[ base + SHIPS_EOFF_IMAGE ];

        shipsEnnemis[ i ].bez.x1 = shipsEnemySet[ base + SHIPS_EOFF_BEZX1 ];
        shipsEnnemis[ i ].bez.x2 = shipsEnemySet[ base + SHIPS_EOFF_BEZX2 ];
        shipsEnnemis[ i ].bez.x3 = shipsEnemySet[ base + SHIPS_EOFF_BEZX3 ];
        shipsEnnemis[ i ].bez.y1 = shipsEnemySet[ base + SHIPS_EOFF_BEZY1 ];
        shipsEnnemis[ i ].bez.y2 = shipsEnemySet[ base + SHIPS_EOFF_BEZY2 ];
        shipsEnnemis[ i ].bez.y3 = shipsEnemySet[ base + SHIPS_EOFF_BEZY3 ];
        shipsEnnemis[ i ].bez.totFrames = shipsEnemySet[ base + SHIPS_EOFF_BEZTOT ];

        shipsEnnemis[ i ].tir.nb = shipsEnemySet[ base + SHIPS_EOFF_TIRNB ];
        shipsEnnemis[ i ].tir.ang1 = shipsEnemySet[ base + SHIPS_EOFF_TIRANG1 ];
        shipsEnnemis[ i ].tir.ang2 = shipsEnemySet[ base + SHIPS_EOFF_TIRANG2 ];
        shipsEnnemis[ i ].tir.totFrames = shipsEnemySet[ base + SHIPS_EOFF_TIRTOT ];

        shipsReadEnnemi = shipsReadEnnemi + 1;
        // guarded (see header comment #4) - reading past the table's real last row is genuinely never observed afterward
        if( shipsReadEnnemi < SHIPS_TOTENNEMIS )
          shipsNextEnnemi = shipsNextEnnemi + shipsEnemySet[ SHIPS_VARS_ENNEMIS * shipsReadEnnemi + SHIPS_EOFF_GAP ];
    }
}

// -----------------------------------------------------------------------
// Enemies - direct port of real upstream's own updateEnnemis() (the
// biggest single function here: bezier flight paths, per-enemy timed
// shots, the real boss's own bespoke firing patterns, and player-shot/
// player-body collision).
// -----------------------------------------------------------------------

void shipsUpdateEnnemis()
{
    if( shipsPerso.vies > 0 ) shipsEndGame = false;

    int i;
    for( i = 0; i < SHIPS_MAX_ENNEMIS; i = i + 1 )
    {
        if( shipsEnnemis[ i ].on )
        {
            shipsEndGame = true;

            // Bezier flight path
            shipsEnnemis[ i ].bezFrame = shipsEnnemis[ i ].bezFrame + 1;
            if( shipsEnnemis[ i ].image == 3 && shipsEnnemis[ i ].bezFrame > shipsEnnemis[ i ].bez.totFrames )
              shipsEnnemis[ i ].bezFrame = shipsEnnemis[ i ].bez.totFrames;

            float t = ( (float)shipsEnnemis[ i ].bezFrame ) / ( (float)shipsEnnemis[ i ].bez.totFrames );
            shipsEnnemis[ i ].x = (int)( ( 1.0 - t ) * ( 1.0 - t ) * shipsEnnemis[ i ].bez.x1 +
                                          2.0 * t * ( 1.0 - t ) * shipsEnnemis[ i ].bez.x2 +
                                          t * t * shipsEnnemis[ i ].bez.x3 );
            shipsEnnemis[ i ].y = (int)( ( 1.0 - t ) * ( 1.0 - t ) * shipsEnnemis[ i ].bez.y1 +
                                          2.0 * t * ( 1.0 - t ) * shipsEnnemis[ i ].bez.y2 +
                                          t * t * shipsEnnemis[ i ].bez.y3 );

            int enX = shipsEnnemis[ i ].x - 100;
            int enY = shipsEnnemis[ i ].y - 100;

            // Boss - real bespoke firing patterns (not the generic timed-shot mechanism below)
            if( t >= 1.0 && shipsEnnemis[ i ].image == 3 )
            {
                if( shipsBossShots3 == 0 ) shipsBossFrame = shipsBossFrame + 1;
                shipsBossShots1 = shipsBossShots1 + 1;
                shipsBossShots2 = shipsBossShots2 + 1;
                int tirV = 20;
                int tirVx = 0;
                int tirVy = 0;
                float tirAng = 0.0;
                enY = (int)( 21.0 + 16.0 * sin( shipsBossFrame / 6.0 ) );

                if( shipsBossShots1 >= 28 ) // top/bottom volleys
                {
                    if( shipsPerso.repop < 20 )
                    {
                        int p;
                        for( p = 0; p < 8; p = p + 1 )
                        {
                            if( enY < 21 ) tirAng = ( 215.0 - 13.0 * p ) * 0.0174;
                            else tirAng = ( 145.0 + 13.0 * p ) * 0.0174;
                            tirVx = (int)( tirV * cos( tirAng ) );
                            tirVy = (int)( tirV * sin( tirAng ) );
                            if( enY < 21 ) shipsNewTirEnnemi( enX - 4, enY + 4, tirVx, tirVy );
                            else shipsNewTirEnnemi( enX - 4, enY - 4, tirVx, tirVy );
                        }
                        shipsBossShots1 = shipsBossShots1 - 28;
                    }
                }
                if( shipsBossShots2 >= 100 && enY < 24 && enY > 18 ) shipsBossShots3 = 1;
                if( shipsBossShots3 > 0 )
                {
                    shipsBossShots1 = 0;
                    shipsBossShots2 = 0;
                    shipsBossShots3 = shipsBossShots3 + 1;
                    if( shipsBossShots3 % 9 == 0 && shipsPerso.repop < 20 )
                    {
                        int p;
                        for( p = 0; p < 9; p = p + 1 )
                        {
                            tirAng = ( 112.0 + 17.0 * p + 3.0 * ( shipsBossShots3 / 9 ) ) * 0.0174;
                            tirVx = (int)( tirV * cos( tirAng ) );
                            tirVy = (int)( tirV * sin( tirAng ) );
                            shipsNewTirEnnemi( enX - 3, enY, tirVx, tirVy );
                        }
                    }
                    if( shipsBossShots3 > 50 ) shipsBossShots3 = 0;
                }
            }

            if( !shipsEnnemis[ i ].justhit )
            {
                int minX = -2;
                int maxX = 3;
                int minY = -2;
                int maxY = 3;

                if( shipsEnnemis[ i ].image == 2 )
                {
                    gbDrawBitmap( enX - 2, enY - 2, shipsVaisseau4 );
                    if( gbCollideBitmapBitmap( enX - 2, enY - 2, shipsVaisseau4, shipsPerso.x, shipsPerso.y, shipsPt ) &&
                        !shipsPerso.destroy && shipsPerso.repop <= 0 )
                    {
                        shipsPerso.destroy = true;
                        shipsSfxPlayerDestroy();
                        shipsEnnemis[ i ].on = false;
                        shipsNewExplosion( enX, enY );
                    }
                }
                else if( shipsEnnemis[ i ].image == 1 )
                {
                    gbDrawBitmap( enX - 2, enY - 2, shipsVaisseau3 );
                    if( gbCollideBitmapBitmap( enX - 2, enY - 2, shipsVaisseau3, shipsPerso.x, shipsPerso.y, shipsPt ) &&
                        !shipsPerso.destroy && shipsPerso.repop <= 0 )
                    {
                        shipsPerso.destroy = true;
                        shipsSfxPlayerDestroy();
                        shipsEnnemis[ i ].on = false;
                        shipsNewExplosion( enX, enY );
                    }
                }
                else if( shipsEnnemis[ i ].image == 3 )
                {
                    gbDrawBitmap( enX - 5, enY - 6, shipsBossBmp );
                    minX = -2; maxX = 3; minY = -5; maxY = 5;
                    if( gbCollideBitmapBitmap( enX - 5, enY - 6, shipsBossBmp, shipsPerso.x, shipsPerso.y, shipsPt ) &&
                        !shipsPerso.destroy && shipsPerso.repop <= 0 )
                    {
                        shipsPerso.destroy = true;
                        shipsSfxPlayerDestroy();
                        // real upstream never sets the boss's own `on=0` or spawns an explosion on body-ram - only shots kill it
                    }
                }
                else
                {
                    gbDrawBitmap( enX - 2, enY - 2, shipsVaisseau2 );
                    if( gbCollideBitmapBitmap( enX - 2, enY - 2, shipsVaisseau2, shipsPerso.x, shipsPerso.y, shipsPt ) &&
                        !shipsPerso.destroy && shipsPerso.repop <= 0 )
                    {
                        shipsPerso.destroy = true;
                        shipsSfxPlayerDestroy();
                        shipsEnnemis[ i ].on = false;
                        shipsNewExplosion( enX, enY );
                    }
                }

                // Player-shot collision
                int m;
                for( m = 0; m < SHIPS_MAX_TIRS; m = m + 1 )
                {
                    if( shipsEnnemis[ i ].on && shipsTirs[ m ].on )
                    {
                        int tx = shipsTirs[ m ].x / 10;
                        int ty = shipsTirs[ m ].y / 10;
                        if( tx >= enX + minX - 2 && ty >= enY + minY - 1 && tx <= enX + maxX && ty <= enY + maxY + 1 )
                        {
                            if( shipsEnnemis[ i ].vie > 0 )
                            {
                                shipsEnnemis[ i ].vie = shipsEnnemis[ i ].vie - 1;
                                shipsScore = shipsScore + 50;
                            }
                            shipsTirs[ m ].on = false;
                            shipsEnnemis[ i ].justhit = true;
                            shipsSfxHit();
                        }
                    }
                }
            }
            else
              shipsEnnemis[ i ].justhit = false;

            // Generic per-enemy timed shots (the boss uses its own bespoke pattern above instead - tir.nb is 0 for it)
            shipsEnnemis[ i ].tirsFrame = shipsEnnemis[ i ].tirsFrame + 1;
            if( shipsEnnemis[ i ].tirsFrame >= shipsEnnemis[ i ].tir.totFrames )
            {
                shipsEnnemis[ i ].tirsFrame = 0;
                int tirV = 17;
                int tirVx = 0;
                int tirVy = 0;
                float tirAng = 0.0;
                float tirCoef = 0.0;
                int k;
                for( k = 0; k < shipsEnnemis[ i ].tir.nb; k = k + 1 )
                {
                    if( shipsEnnemis[ i ].tir.nb > 1 )
                      tirCoef = ( (float)k ) / ( (float)( shipsEnnemis[ i ].tir.nb - 1 ) );
                    else
                      tirCoef = 0.5;
                    tirAng = tirCoef * shipsEnnemis[ i ].tir.ang1 * 10.0 + ( 1.0 - tirCoef ) * shipsEnnemis[ i ].tir.ang2 * 10.0;
                    tirAng = tirAng * 0.0174;
                    tirVx = (int)( tirV * cos( tirAng ) );
                    tirVy = (int)( tirV * sin( tirAng ) );
                    if( shipsPerso.repop < 20 ) // enemy only fires once the player is ready
                    {
                        shipsNewTirEnnemi( enX, enY, tirVx, tirVy );
                        shipsSfxEnemyShot();
                    }
                }
            }

            if( shipsEnnemis[ i ].vie <= 0 )
            {
                shipsNewExplosion( enX, enY );
                shipsShipsDestroyed = shipsShipsDestroyed + 1;
                shipsSfxDestroy();
                if( shipsEnnemis[ i ].image == 1 ) shipsScore = shipsScore + 200;
                else if( shipsEnnemis[ i ].image == 3 ) shipsScore = shipsScore + 1000;
                else shipsScore = shipsScore + 100;
            }

            if( shipsEnnemis[ i ].vie <= 0 || ( t > 1.0 && shipsEnnemis[ i ].image != 3 ) )
              shipsEnnemis[ i ].on = false;
        }
    }

    shipsSpawnNextEnnemi();
}

// -----------------------------------------------------------------------
// Player - direct port of real upstream's own updatePerso()/initPerso()
// (perso.ino). See header comment #1 for why shipsInitPerso() deliberately
// leaves cadencetir/supershot/supershotNb untouched.
// -----------------------------------------------------------------------

void shipsInitPerso()
{
    shipsPerso.x = 10;
    shipsPerso.y = LCDHEIGHT / 2;
    shipsPerso.repop = 50;
    shipsPerso.vies = 3;
    shipsPerso.destroy = false;
    shipsPerso.waitEnd = 0;
    shipsPerso.waitGameover = 0;
}

void shipsUpdatePerso()
{
    bool btRight, btLeft, btUp, btDown;

    if( shipsPerso.waitEnd < 10 )
    {
        btRight = gbRepeat( BTN_RIGHT, 1 );
        btLeft = gbRepeat( BTN_LEFT, 1 );
        btUp = gbRepeat( BTN_UP, 1 );
        btDown = gbRepeat( BTN_DOWN, 1 );
    }
    else
    {
        // real upstream's own auto-pilot, taking over the controls once
        // the level is cleared, steering the ship toward its own fly-off
        // ramp position
        btRight = false;
        btLeft = false;
        btUp = false;
        btDown = false;
        if( shipsPerso.y > 23 ) btUp = true;
        if( shipsPerso.y < 20 ) btDown = true;
        if( shipsPerso.x > 11 ) btLeft = true;
        if( shipsPerso.x < 8 ) btRight = true;
        if( shipsPerso.x >= 8 && shipsPerso.x <= 11 ) shipsPerso.x = 10;
        if( shipsPerso.waitEnd == 10 ) // real upstream's own one-time stop, the instant the auto-pilot phase begins
        {
            gbStopTrack( 1 );
            gbStopTrack( 2 );
        }
    }

    if( !shipsPerso.destroy )
    {
        // Movement
        if( shipsPerso.waitEnd < 50 )
        {
            if( btRight && !btLeft )
            {
                if( btDown ) { shipsPerso.y = shipsPerso.y + SHIPS_PLAYER_SPD2; shipsPerso.x = shipsPerso.x + SHIPS_PLAYER_SPD2; }
                else if( btUp ) { shipsPerso.y = gbMax( 0, shipsPerso.y - SHIPS_PLAYER_SPD2 ); shipsPerso.x = shipsPerso.x + SHIPS_PLAYER_SPD2; }
                else if( !btDown ) shipsPerso.x = shipsPerso.x + SHIPS_PLAYER_SPD1;
            }
            if( btLeft && !btRight )
            {
                if( btDown ) { shipsPerso.y = shipsPerso.y + SHIPS_PLAYER_SPD2; shipsPerso.x = gbMax( 0, shipsPerso.x - SHIPS_PLAYER_SPD2 ); }
                else if( btUp ) { shipsPerso.y = gbMax( 0, shipsPerso.y - SHIPS_PLAYER_SPD2 ); shipsPerso.x = gbMax( 0, shipsPerso.x - SHIPS_PLAYER_SPD2 ); }
                else if( !btDown ) shipsPerso.x = gbMax( 0, shipsPerso.x - SHIPS_PLAYER_SPD1 );
            }
            if( ( !btLeft && !btRight ) || ( btLeft && btRight ) )
            {
                if( btDown ) shipsPerso.y = shipsPerso.y + SHIPS_PLAYER_SPD1;
                else if( btUp ) shipsPerso.y = gbMax( 0, shipsPerso.y - SHIPS_PLAYER_SPD1 );
            }

            if( shipsPerso.x < 2 ) shipsPerso.x = 2;
            if( shipsPerso.y < 2 ) shipsPerso.y = 2;
            if( shipsPerso.x > LCDWIDTH - 3 ) shipsPerso.x = LCDWIDTH - 3;
            if( shipsPerso.y > LCDHEIGHT - 9 ) shipsPerso.y = LCDHEIGHT - 9;
        }
        else
        {
            // Post-clear fly-off ramp
            float rampT = ( shipsPerso.waitEnd - 50.0 ) / 70.0;
            shipsPerso.x = gbMin( 100, (int)( 10.0 + 85.0 * rampT * rampT ) );
            if( shipsPerso.x > 11 && shipsPerso.waitEnd < 120 )
              shipsStarSpeed = shipsStarSpeed + 1 + ( shipsPerso.x - 11 ) / 10;
            if( shipsPerso.waitEnd >= 120 )
              shipsStarSpeed = gbMax( 0, 5 - ( shipsPerso.waitEnd - 120 ) / 5 );
        }

        if( shipsPerso.repop >= 20 )
        {
            shipsPerso.y = LCDHEIGHT / 2 - 3;
            float repop2 = ( ( (float)shipsPerso.repop - 30.0 ) / 15.0 ) * ( ( (float)shipsPerso.repop - 30.0 ) / 15.0 );
            shipsPerso.x = (int)( 25.0 - 30.0 * repop2 );
            if( shipsPerso.repop < 52 )
            {
                int j;
                for( j = 0; j < SHIPS_MAX_TIRS_ENNEMIS; j = j + 1 ) shipsTirsEnnemis[ j ].on = false;
            }
        }
        if( shipsPerso.repop > 0 )
        {
            shipsPerso.repop = shipsPerso.repop - 1;
            shipsPerso.waitEnd = 0;
        }

        // Draw the ship (blinking during respawn/end-sequence)
        int xorig = shipsPerso.x - 2;
        int yorig = shipsPerso.y - 2;
        if( shipsPerso.repop % 2 == 0 || ( shipsPerso.vies == 3 && !shipsContinu ) )
        {
            if( shipsPerso.repop < 20 && shipsPerso.waitEnd < 80 )
              gbDrawBitmap( xorig, yorig, shipsVaisseau );

            if( shipsPerso.repop >= 20 )
            {
                if( ( shipsPerso.repop - 20 ) % 8 < 2 )
                  gbDrawBitmap( xorig, yorig, shipsVaisseau );
                else if( ( shipsPerso.repop - 20 ) % 8 == 4 || ( shipsPerso.repop - 20 ) % 8 == 5 )
                  gbDrawBitmap( xorig, yorig, shipsVaisseauXs );
                else
                  gbDrawBitmap( xorig, yorig, shipsVaisseauS );
            }

            if( shipsPerso.waitEnd >= 80 )
            {
                if( ( shipsPerso.waitEnd - 80 ) % 8 < 2 )
                  gbDrawBitmap( xorig, yorig, shipsVaisseau );
                else if( ( shipsPerso.waitEnd - 80 ) % 8 == 4 || ( shipsPerso.waitEnd - 80 ) % 8 == 5 )
                  gbDrawBitmap( xorig, yorig, shipsVaisseauXs );
                else
                  gbDrawBitmap( xorig, yorig, shipsVaisseauS );
            }
        }

        // Super shot (charge/fire) - see this file's own header comment #1
        if( shipsPerso.repop > 0 )
        {
            shipsPerso.supershot = 0;
            shipsPerso.supershotNb = 0;
        }
        if( shipsPerso.supershot == 30 && gbRepeat( BTN_A, 1 ) && shipsPerso.waitEnd < 10 )
        {
            shipsPerso.supershotNb = 5;
            shipsPerso.supershot = 0;
        }
        if( shipsPerso.supershot == 29 ) shipsSfxSuperCharge();
        if( shipsPerso.supershotNb == 5 || shipsPerso.supershotNb == 3 || shipsPerso.supershotNb == 1 )
        {
            shipsNewTir( 45, -5, -1 );
            shipsNewTir( 45, 5, 1 );
            shipsNewTir( 55, 0, 0 );
            shipsNewTir( 50, 0, -1 );
            shipsNewTir( 50, 0, 1 );
        }
        if( shipsPerso.supershotNb > 0 )
        {
            shipsPerso.supershotNb = shipsPerso.supershotNb - 1;
            shipsPerso.supershot = 0;
        }

        // Normal shot / charge accumulation
        if( gbRepeat( BTN_A, 1 ) && shipsPerso.cadencetir == 0 && shipsPerso.repop < 20 && shipsPerso.supershotNb == 0 && shipsPerso.waitEnd < 10 )
        {
            shipsNewTir( 50, 0, -1 );
            shipsNewTir( 50, 0, 1 );
            shipsPerso.cadencetir = 6;
        }
        if( shipsPerso.cadencetir > 0 )
        {
            shipsPerso.cadencetir = shipsPerso.cadencetir - 1;
            shipsPerso.supershot = 0;
        }
        if( shipsPerso.cadencetir == 0 && shipsPerso.supershot < 30 )
          shipsPerso.supershot = shipsPerso.supershot + 1;
        if( shipsPerso.supershot >= 10 && shipsReadEnnemi < SHIPS_TOTENNEMIS )
          shipsScore = shipsScore + 5;
    }
    else
    {
        // Player was just destroyed
        shipsPerso.supershot = 0;
        if( shipsPerso.vies > 1 )
        {
            shipsPerso.repop = 60;
            shipsPerso.vies = shipsPerso.vies - 1;
            shipsPerso.destroy = false;
            shipsNewExplosion( shipsPerso.x, shipsPerso.y );
        }
        else
        {
            if( shipsPerso.vies != 0 )
            {
                shipsNewExplosion( shipsPerso.x, shipsPerso.y );
                shipsPerso.waitGameover = 0;
            }
            shipsPerso.vies = 0;
            gbStopTrack( 1 ); // real upstream's own stop, the instant the player's last life is lost
            gbStopTrack( 2 );
        }
    }

    if( shipsPerso.vies == 0 )
    {
        shipsPerso.waitGameover = shipsPerso.waitGameover + 1;

        if( shipsPerso.waitGameover >= 10 )
        {
            shipsPrint( "CONTINUE?", 22, 10 );
            shipsDisplayInt( gbMin( 9, ( 210 - shipsPerso.waitGameover ) / 20 ), 60, 10, 1 );

            gbCursorX = 20; gbCursorY = 29;
            shipsPrintIcon( 21 ); // A
            gbPrintString( "/" );
            shipsPrintIcon( 23 ); // C
            gbPrintString( " Continue" );

            gbCursorX = 24; gbCursorY = 36;
            shipsPrintIcon( 22 ); // B
            gbPrintString( " End game" );

            if( shipsPerso.waitGameover % 20 == 10 ) gbPlayTick();

            if( gbPressed( BTN_B ) )
            {
                shipsPerso.waitGameover = 230;
                gbPlayCancel();
            }

            if( gbPressed( BTN_C ) || gbPressed( BTN_A ) )
            {
                gbPlayOK();
                shipsPerso.vies = 3;
                shipsPerso.repop = 50;
                shipsScore = 0;
                shipsScoreAff = 0;
                shipsPerso.destroy = false;
                shipsContinu = true;
                shipsReadEnnemi = shipsCheckpoint;
                shipsNextEnnemi = shipsNbFrames + 50;
                shipsPhase = 0;
                shipsShipsDestroyed = 0;
                shipsEndGame = false;
                shipsBossFrame = 0;
                shipsBossShots1 = 0;
                shipsBossShots2 = 0;
                shipsBossShots3 = 0;

                int j;
                for( j = 0; j < SHIPS_MAX_ENNEMIS; j = j + 1 ) shipsEnnemis[ j ].on = false;
                for( j = 0; j < SHIPS_MAX_TIRS_ENNEMIS; j = j + 1 ) shipsTirsEnnemis[ j ].on = false;
                for( j = 0; j < SHIPS_STARS; j = j + 1 )
                {
                    shipsStar[ j * 2 ] = 100 + arand( LCDWIDTH / SHIPS_STARS + 3 ) + j * ( LCDWIDTH / SHIPS_STARS );
                    shipsStar[ j * 2 + 1 ] = arand( LCDHEIGHT );
                }
            }
        }
    }

    if( !shipsEndGame && shipsReadEnnemi >= SHIPS_TOTENNEMIS )
      shipsPerso.waitEnd = shipsPerso.waitEnd + 1;
}

// -----------------------------------------------------------------------
// HUD - direct port of real upstream's own updateAff() (affichages.ino).
// -----------------------------------------------------------------------

void shipsUpdateAff()
{
    gbSetColor( 0 ); // WHITE
    gbFillRect( 0, 42, LCDWIDTH, 6 );
    gbSetColor( 1 ); // BLACK

    if( shipsNbFrames > 40 )
    {
        gbFillRect( 16, 44, 2 * ( shipsPerso.supershot - 10 ), 3 ); // negative width while charge<10 draws nothing - see header comment #6

        if( shipsScore > shipsScoreAff + 50 ) shipsScoreAff = shipsScoreAff + 45;
        if( shipsScore > shipsScoreAff + 25 ) shipsScoreAff = shipsScoreAff + 25;
        if( shipsScore > shipsScoreAff ) shipsScoreAff = shipsScoreAff + gbMin( 5, shipsScore - shipsScoreAff );

        shipsDisplayInt( shipsScoreAff, 60, 43, 6 );

        gbCursorY = 43;
        gbCursorX = 8;
        gbPrintNumber( shipsPerso.vies );

        gbDrawBitmapRotated( 1, LCDHEIGHT - 8, shipsVaisseau, 1, 0 ); // ROTCCW, NOFLIP
    }
    else
    {
        shipsPrint( "HIGHSCORE:", 1, 43 );
        shipsDisplayInt( shipsHighscore, 60, 43, 6 );
    }
}

// -----------------------------------------------------------------------
// Reset / new game - direct port of real upstream's own initGame() minus
// the blocking title-screen call (see this file's own "STATE-MACHINE
// CONVERSION" header comment).
// -----------------------------------------------------------------------

void shipsResetGame()
{
    int lsb = eeprom_read_byte( 0 );
    int msb = eeprom_read_byte( 1 );
    shipsHighscore = ( lsb & 0x00FF ) + ( ( msb << 8 ) & 0xFF00 );
    if( shipsHighscore > 60000 ) shipsHighscore = 0;

    // Real upstream's own gb.sound.changePatternSet(patternSet,1/2) +
    // playTrack(track1,1)/playTrack(track2,2) - starts the real 2-channel
    // background score at the exact same point real initGame() does.
    gbChangePatternSet( shipsPatternSet, 1 );
    gbChangePatternSet( shipsPatternSet, 2 );
    gbPlayTrack( shipsTrack1, 1 );
    gbPlayTrack( shipsTrack2, 2 );

    gbPickRandomSeed(); // no-op, see gamebuinoShim.h's own header comment

    shipsInitPerso();
    shipsNbFrames = 0;
    shipsScore = 0;
    shipsScoreAff = 0;
    shipsShipsDestroyed = 0;
    shipsEndScreen = false;
    shipsPause = false;
    shipsEndGame = false;
    shipsBossFrame = 0;
    shipsBossShots1 = 0;
    shipsBossShots2 = 0;
    shipsBossShots3 = 0;
    shipsStarSpeed = 1;
    shipsPhase = 0;
    shipsCheckpoint = 0;
    shipsPhaseDisplay = 0;
    shipsContinu = false;
    shipsScoreSaved = false;
    shipsNewHighscore = false;

    int j;
    for( j = 0; j < SHIPS_MAX_TIRS; j = j + 1 ) shipsTirs[ j ].on = false;
    for( j = 0; j < SHIPS_MAX_ENNEMIS; j = j + 1 ) shipsEnnemis[ j ].on = false;
    for( j = 0; j < SHIPS_MAX_TIRS_ENNEMIS; j = j + 1 ) shipsTirsEnnemis[ j ].on = false;
    for( j = 0; j < SHIPS_MAX_EXPLOSIONS; j = j + 1 ) shipsExplosions[ j ].on = false;

    shipsReadEnnemi = 0;
    shipsNextEnnemi = shipsEnemySet[ SHIPS_VARS_ENNEMIS * shipsReadEnnemi + SHIPS_EOFF_GAP ];

    for( j = 0; j < SHIPS_STARS; j = j + 1 )
    {
        shipsStar[ j * 2 ] = 100 + arand( LCDWIDTH / SHIPS_STARS + 3 ) + j * ( LCDWIDTH / SHIPS_STARS );
        shipsStar[ j * 2 + 1 ] = arand( LCDHEIGHT );
    }
}

// -----------------------------------------------------------------------
// End screen - direct port of real upstream's own updateEndScreen()
// (endscreen.ino).
// -----------------------------------------------------------------------

void shipsUpdateEndScreen()
{
    gbFontSize = 2;
    if( shipsPerso.vies == 0 ) shipsPrint( "GAME OVER", 8, 0 );
    else shipsPrint( "COMPLETE!", 8, 0 );
    gbFontSize = 1;

    if( shipsWaitEndScreen == 0 )
    {
        shipsBonusEndScreen = shipsPerso.vies * 2000;
        if( shipsContinu ) shipsBonusEndScreen = 0;
    }

    if( shipsBonusEndScreen == 0 && !shipsScoreSaved )
    {
        shipsScoreSaved = true;
        if( shipsScore > shipsHighscore )
        {
            shipsHighscore = shipsScore;
            eeprom_write_byte( 0, shipsHighscore & 0x00FF );
            eeprom_write_byte( 1, ( shipsHighscore >> 8 ) & 0x00FF );
            shipsNewHighscore = true;
        }

        shipsRankLetter = 0;
        int top = 38000;
        while( shipsRankLetter < 10 && shipsScore < top )
        {
            shipsRankLetter = shipsRankLetter + 1;
            top = (int)( top * 0.75 );
        }
    }

    shipsPrint( "Ships destroyed :", 1, 14 );
    shipsDisplayInt( shipsShipsDestroyed, 72, 14, 3 );

    if( shipsPerso.vies > 0 )
    {
        shipsPrint( "Remaining lives :", 1, 20 );
        gbCursorX = 80;
        gbCursorY = 20;
        gbPrintNumber( shipsPerso.vies );
        if( shipsBonusEndScreen > 0 )
          shipsDisplayInt( shipsBonusEndScreen, 68, 26, 4 );
    }

    if( !shipsNewHighscore || shipsWaitEndScreen <= 20 )
      shipsPrint( "FINAL SCORE :", 1, 36 );
    else if( shipsWaitEndScreen % 10 < 7 )
      shipsPrint( "HIGHSCORE !", 1, 36 );

    shipsDisplayInt( shipsScore, 60, 36, 6 );

    if( shipsBonusEndScreen == 0 )
    {
        gbCursorX = 1;
        gbCursorY = 30;
        gbPrintString( "Rank :" );
        gbCursorX = 80;
        if( shipsRankLetter == 0 ) gbPrintString( "S" );
        else if( shipsRankLetter == 1 ) { gbCursorX = 76; gbPrintString( "A+" ); }
        else if( shipsRankLetter == 2 ) gbPrintString( "A" );
        else if( shipsRankLetter == 3 ) { gbCursorX = 76; gbPrintString( "A-" ); }
        else if( shipsRankLetter == 4 ) { gbCursorX = 76; gbPrintString( "B+" ); }
        else if( shipsRankLetter == 5 ) gbPrintString( "B" );
        else if( shipsRankLetter == 6 ) { gbCursorX = 76; gbPrintString( "B-" ); }
        else if( shipsRankLetter == 7 ) { gbCursorX = 76; gbPrintString( "C+" ); }
        else if( shipsRankLetter == 8 ) gbPrintString( "C" );
        else if( shipsRankLetter == 9 ) { gbCursorX = 76; gbPrintString( "C-" ); }
        else gbPrintString( "D" );
    }

    if( shipsBonusEndScreen == 0 && shipsWaitEndScreen > 20 )
    {
        if( ( shipsWaitEndScreen - 20 ) % 60 < 30 )
        {
            gbCursorX = 30; gbCursorY = 42;
            shipsPrintIcon( 21 ); // A
            gbPrintString( " Retry" );
        }
        else
        {
            gbCursorX = 12; gbCursorY = 42;
            shipsPrintIcon( 22 ); // B
            gbPrintString( "/" );
            shipsPrintIcon( 23 ); // C
            gbPrintString( " Title Screen" );
        }
    }

    if( shipsBonusEndScreen == 0 && shipsWaitEndScreen > 20 )
    {
        if( gbPressed( BTN_A ) )
        {
            gbPlayOK();
            shipsResetGame();
        }
        if( gbPressed( BTN_B ) || gbPressed( BTN_C ) )
        {
            gbPlayCancel();
            shipsState = SHIPS_STATE_TITLE;
        }
    }

    if( shipsBonusEndScreen > 0 && shipsWaitEndScreen > 20 )
    {
        int s = gbMin( shipsBonusEndScreen, 97 );
        shipsBonusEndScreen = shipsBonusEndScreen - s;
        gbPlayTick();
        shipsScore = shipsScore + s;
        shipsWaitEndScreen = 20;
    }

    shipsWaitEndScreen = shipsWaitEndScreen + 1;
    if( shipsWaitEndScreen > 80 ) shipsWaitEndScreen = shipsWaitEndScreen - 60;
}

// -----------------------------------------------------------------------
// Main gameplay tick - direct port of real upstream's own loop()'s own
// `if(!pause){ ... }` body (_101starships.ino).
// -----------------------------------------------------------------------

void shipsUpdateGameplayTick()
{
    shipsUpdateStars( shipsStarSpeed );
    if( shipsNbFrames < 15 ) shipsStarSpeed = 2;
    else shipsStarSpeed = 1;

    shipsUpdateEnnemis();
    shipsUpdateTirsEnnemis();
    shipsUpdatePerso();
    shipsUpdateTirs();
    shipsUpdateExplosions();
    shipsUpdateAff();
    shipsNbFrames = shipsNbFrames + 1;

    bool phaseThreshold =
        ( shipsPhase == 0 && shipsReadEnnemi >= 2 ) ||
        ( shipsPhase == 1 && shipsReadEnnemi >= 23 ) ||
        ( shipsPhase == 2 && shipsReadEnnemi >= 48 ) ||
        ( shipsPhase == 3 && shipsReadEnnemi >= 70 ) ||
        ( shipsPhase == 4 && shipsReadEnnemi >= 96 );

    if( ( shipsNbFrames >= shipsNextEnnemi - 20 ) && shipsPerso.vies > 0 && phaseThreshold )
    {
        shipsPhase = shipsPhase + 1;
        shipsPhaseDisplay = 40;
        shipsCheckpoint = shipsReadEnnemi;
    }

    if( shipsPhaseDisplay > 0 )
    {
        shipsPhaseDisplay = shipsPhaseDisplay - 1;
        int revealLen = ( 40 - shipsPhaseDisplay ) / 2;
        if( shipsPhase < 5 )
        {
            shipsPrintTruncated( shipsStageMsg, 48, 0, revealLen );
            if( shipsPhaseDisplay < 28 ) shipsDisplayInt( shipsPhase, 72, 0, 1 );
            if( shipsPhaseDisplay > 22 && shipsPhaseDisplay % 2 == 0 ) gbPlayTick();
        }
        else
        {
            shipsPrintTruncated( shipsFinalStageMsg, 40, 0, revealLen );
            if( shipsPhaseDisplay > 18 && shipsPhaseDisplay % 2 == 0 ) gbPlayTick();
        }
    }

    if( gbPressed( BTN_C ) && shipsPerso.vies > 0 && shipsPerso.waitEnd < 10 && shipsPerso.repop < 45 )
    {
        shipsPause = true;
        gbPlayCancel();
    }

    if( shipsPerso.vies > 0 && shipsPerso.waitEnd > 180 && !shipsEndScreen )
    {
        shipsEndScreen = true;
        shipsWaitEndScreen = 0;
        // Real upstream's own p20/p21 win jingle (stopTrack(1)/(2) then
        // playPattern(p20,1)/playPattern(p21,2)).
        gbStopTrack( 1 );
        gbStopTrack( 2 );
        gbPlayPattern( shipsPat20, 1 );
        gbPlayPattern( shipsPat21, 2 );
    }
    if( shipsPerso.vies == 0 && shipsPerso.waitGameover > 210 && !shipsEndScreen )
    {
        shipsEndScreen = true;
        shipsWaitEndScreen = 0;
        // Real upstream's own p22/p23 lose jingle (stopTrack(1)/(2) then
        // playPattern(p22,1)/playPattern(p23,2)).
        gbStopTrack( 1 );
        gbStopTrack( 2 );
        gbPlayPattern( shipsPat22, 1 );
        gbPlayPattern( shipsPat23, 2 );
    }
}

// -----------------------------------------------------------------------
// Pause overlay (Button C) - see this file's own header comment #7 for
// why this is drawn on a plain cleared background rather than a frozen
// gameplay frame.
// -----------------------------------------------------------------------

void shipsDrawPauseOverlay()
{
    gbSetColor( 1 ); // BLACK

    gbFontSize = 2;
    shipsPrint( "PAUSED", 16, 0 );
    gbFontSize = 1;

    shipsPrint( "Hint: stop shooting", 0, 12 );
    shipsPrint( "to charge super shot!", 0, 18 );

    gbCursorX = 20; gbCursorY = 29;
    shipsPrintIcon( 21 ); // A
    gbPrintString( "/" );
    shipsPrintIcon( 23 ); // C
    gbPrintString( " Continue" );

    gbCursorX = 16; gbCursorY = 36;
    shipsPrintIcon( 22 ); // B
    gbPrintString( " Title Screen" );

    if( gbPressed( BTN_C ) || gbPressed( BTN_A ) )
    {
        shipsPause = false;
        gbPlayOK();
    }
    if( gbPressed( BTN_B ) )
    {
        // Real upstream's own gb.sound.stopTrack(1)/stopTrack(2) before the
        // playCancel() stinger and the return to the title screen.
        gbStopTrack( 1 );
        gbStopTrack( 2 );
        gbPlayCancel();
        shipsState = SHIPS_STATE_TITLE;
        shipsPause = false;
    }
}

// -----------------------------------------------------------------------
// Title screen - hand-built (this shim has no gb.titleScreen() widget -
// see this file's own "STATE-MACHINE CONVERSION" header comment).
// -----------------------------------------------------------------------

void shipsUpdateTitle()
{
    gbSetColor( 1 ); // BLACK
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, shipsLogo );

    gbCursorX = 28;
    gbCursorY = 39;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        shipsResetGame();
        shipsState = SHIPS_STATE_GAME;
    }
}

// -----------------------------------------------------------------------
// Top-level dispatch - direct port of real upstream's own loop() body.
// -----------------------------------------------------------------------

// Real, non-static gamebuinoShim.c global (the direct equivalent of real
// Sound::trackIsPlaying[], itself a public member upstream reads the exact
// same way) - not part of gamebuinoShim.h's own public extern list, since
// this project compiles each game as its own separate translation unit;
// declared here directly rather than adding it to the shared shim header.
extern bool gbTrackIsPlaying[];

void shipsUpdateGame()
{
    if( !shipsEndScreen )
    {
        // Real upstream's own `!gb.sound.trackIsPlaying[1] && wait_end<10 &&
        // vies>0` check - track1/track2 are 0xFFFF-terminated (not looping),
        // so the background score naturally finishes; this re-triggers it,
        // giving a continuous loop, exactly matching real hardware. Runs
        // every tick regardless of pause state, matching real upstream's
        // own placement outside the `if(!pause)` branch below.
        if( !gbTrackIsPlaying[ 1 ] && shipsPerso.waitEnd < 10 && shipsPerso.vies > 0 )
        {
            gbPlayTrack( shipsTrack1, 1 );
            gbPlayTrack( shipsTrack2, 2 );
        }

        if( !shipsPause ) shipsUpdateGameplayTick();
        else shipsDrawPauseOverlay();
    }
    else
      shipsUpdateEndScreen();
}

// -----------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------

void gameStarships101_init()
{
    gbBegin();

    // Explicit one-time zero-init matching real hardware's own C++ static
    // zero-init at program start - shipsResetGame() itself (called on
    // every subsequent retry/continue) deliberately leaves cadencetir/
    // supershot/supershotNb untouched, see header comment #1.
    shipsPerso.x = 0;
    shipsPerso.y = 0;
    shipsPerso.destroy = false;
    shipsPerso.cadencetir = 0;
    shipsPerso.repop = 0;
    shipsPerso.vies = 0;
    shipsPerso.waitEnd = 0;
    shipsPerso.waitGameover = 0;
    shipsPerso.supershot = 0;
    shipsPerso.supershotNb = 0;

    shipsState = SHIPS_STATE_TITLE;
}

void gameStarships101_update()
{
    if( !gbUpdate() ) return;

    if( shipsState == SHIPS_STATE_TITLE ) shipsUpdateTitle();
    else shipsUpdateGame();

    gbRenderFrame();
}
