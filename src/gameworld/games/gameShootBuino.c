#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// ShootBuino (frthery, no license specified - github.com/frthery/ShootBuino).
// A vertical Space-Invaders-style shooter: a spaceship confined to a narrow
// central lane fires up at two-color invader waves that spawn in random
// formations and drift downward, with a chain-combo scoring system (killing
// same-color invaders back-to-back builds a combo bonus; switching color
// resets it) and a per-level difficulty ramp (invader/bullet fall speed and
// spawn variety both increase with `game_level`, 1-9).
//
// Real upstream multi-tab consolidation: `shootBuino.ino` (setup/loop, game
// state, field/score/menu screens), `player.ino` (player movement/shooting/
// collision), `invader.ino` (invader spawning/movement/collision),
// `highscore.ino` (top-5 score/chain tables), `sounds.ino` (sound-effect
// dispatch) and `sprites.ino` (real PROGMEM bitmap tables) are all merged
// here, matching this project's own established multi-tab-.ino consolidation
// precedent (gameConduit.c/gameSimonbuino.c). Every `gb.x.y(...)` call site
// is mechanically rewritten to a plain `gbY(...)` call (this dialect has no
// classes/methods - see gamebuinoShim.h's own header comment), `byte`/
// `short`/`boolean` become plain `int`/`bool` (avrCompat.h has no alias for
// `boolean`/`byte`/`short`), `random(n)` becomes `arand(n)`, and ranged
// `random(lo,hi)` becomes the small `sbuinoRandRange()` helper below (this
// shim's own `arand()` only covers the single-argument `[0,n)` form).
//
// Real bitmap art (`logo`/`score_0..9`/`spaceship`/`invader_0`/`invader_1`/
// `invader_explose_0`/`invader_explose_1`, all real row-major MSB-first
// PROGMEM tables in the real sprites.ino) is restored verbatim via
// `gbDrawBitmap()`, with every `B00000000`-style binary literal converted to
// hex (this dialect has no binary-literal syntax). No font is restored
// beyond this shim's own real `gbFont3x5` default, since upstream never
// calls `gb.display.setFont()` at all.
//
// Real upstream's own blocking `gb.titleScreen(logo)` (called once from
// `setup()`) became an explicit `sbuinoTitleShown` gate, matching this
// project's own established "blocking widget -> explicit resumable state"
// treatment (see gamePong.c's own header comment) - dismissed by a genuine
// `gbPressed(BTN_A)`. The real 64x36 logo is centered horizontally
// (`x=(LCDWIDTH-64)/2`) rather than upstream's real hardware-only `x=0`
// (real `titleScreen()` composes the logo alongside Gamebuino's own boot
// furniture this shim has no equivalent for) - the same substitution
// gameMaze.c's own header comment already documents for the identical
// situation (and, not by coincidence, the identical 64x36 logo size).
//
// Real upstream's own `GameMenu()` third option (Button C = real
// `gb.changeGame()`, a real-hardware "switch cartridge slot" OS feature with
// no equivalent in this single-cartridge shared-menu model) re-shows the
// title screen and forces a fresh restart instead, matching
// gameGruniozerca.c's/gameCrazyCar.c's own established "Exit" substitution.
//
// SAVE_EEPROM is `#define`d 0 in the real upstream `shootBuino.ino`, and
// every real `EEPROM.read()`/`EEPROM.write()` call in the real
// `highscore.ino` sits inside an `#if SAVE_EEPROM` block, including the
// "HIGHSCORE SAVED!" popup - none of that code is part of the real compiled
// game at all, so the top-5 score/chain tables were originally genuine
// in-RAM-only state here too, matching real upstream's own actually-shipped
// behavior exactly.
//
// EEPROM PERSISTENCE ADDED, BEYOND REAL UPSTREAM'S OWN SHIPPED BEHAVIOR -
// added directly on request once an audit found this game displays two
// real top-5 tables that never survived a cartridge reboot. Real
// upstream's own dormant, never-compiled `#if SAVE_EEPROM` code already
// spells out an intended layout, reused directly here rather than
// invented fresh: `highscore[i]` at address `i*2` (0,2,4,6,8),
// `highchain[i]` at address `NUM_HIGHSCORE*2 + i*2` (10,12,14,16,18).
// One real deviation from that dormant code's own real formula, though:
// its own real fresh-cell check is `highscore[thisScore]==0`, which -
// exactly like `gameUfoRace.c`'s own already-documented identical
// dead-check bug - can never actually fire against a genuinely fresh
// EEPROM (composes to 65535, not 0, on real hardware and here alike).
// Since this dormant code was never actually compiled/tested upstream
// either, there is no real "restore it exactly" obligation the way there
// would be for shipped behavior - used this project's own established,
// already-proven-safe `==0xFFFF` check instead (see gameCrabator.c's/
// gameDescent.c's own identical check) for both tables.
// `sbuinoSaveHighscore()` below takes a new `eepromBase` parameter (0 for
// the score table, `SBUINO_NUM_HIGHSCORE*2` for the chain table, matching
// real upstream's own real `eeprom_index` parameter of the same real
// function) and, matching real upstream's own real save loop exactly,
// rewrites all 5 entries of whichever table changed, not just the one
// new entry - correct, since inserting a new score can shift every
// entry below it down one rank.
//
// Real upstream's own `PlaySoundFx()` drives the tracker directly via
// `gb.sound.command()` (per-channel waveform/volume-slide/pitch-slide
// parameters) - only ever invoked upstream for the game-over cue (the
// invader-hit dispatch call is commented out in the real `sounds.ino`, so
// invader hits are genuinely silent on real hardware too). This shim only
// ports representative one-shot tones (see gamebuinoShim.h's own header
// comment and this project's own open "Sound" question) with no
// `command()`-level primitive at all, so the game-over cue uses this
// project's own established closest substitute for a "you lost" cue,
// `gbPlayCancel()`, instead of reproducing the exact custom waveform.
//
// Three real, verified upstream bugs. The first two below were found and
// fixed (not preserved) once flagged as genuine negative player-experience
// bugs; the third (the unused bullet slots) is harmless and stays
// preserved:
//   - FIXED, NOT PRESERVED - was: `player_life` (`sbuinoPlayerLife` here)
//     is only ever initialized once, via its own global initializer - real
//     `InitGame()` never reset it on a new game. Combined with the
//     AVR-integer-promotion guard `(player_life - 1) >= 0` (preserved here
//     by using a plain `int`, which promotes identically), once the
//     player died once in a session, every subsequent game (menu-restart
//     or the auto-restart after viewing highscores) started with life
//     already at 0 and died again on the very first hit, permanently, for
//     the rest of the cartridge session. Fixed in `sbuinoInitGame()`:
//     `sbuinoPlayerLife` is now explicitly reset to
//     `sbuinoPlayerLifeDefault` on every new game.
//   - FIXED, NOT PRESERVED - was: `MoveInvadersBullets()` drew each
//     invader bullet via a real `fillRect(x,y,1,-2)` - a NEGATIVE height.
//     Both real `Display::fillRect()`/`drawFastVLine()` and this shim's
//     own `gbFillRect()`/`gbDrawFastVLine()` only ever loop from `y` up to
//     (exclusive) `y+h`, so a negative `h` made the loop bounds empty and
//     drew nothing at all - invader bullets were genuinely invisible even
//     though they could still hit the player. Fixed to draw a real 2px
//     vertical bullet ending at its own logical Y position.
//   - `InvaderShoot()`/`MoveInvadersBullets()` both loop `NUMBULLETS` (10)
//     slots even though `invaders_bullets_x/y/active` are all sized
//     `NUMINVBULLETS` (12) - the last 2 of 12 allocated invader-bullet slots
//     are permanently unused. Harmless (only lowers the real concurrent
//     invader-bullet cap from 12 to 10) - preserved exactly.
//
// One real upstream bug normalized rather than preserved:
// `DrawGraphicalScore()` builds its 6 score digits via `sprintf(buf,
// "%06i", game_score)` into a `char buf[6]` (too small even for the digits
// alone, let alone a null terminator) and then reads `buf[6]` for the last
// digit - one index past the end of a 6-byte array. This dialect has no
// `sprintf()`/stdio at all (so a literal mechanical translation isn't even
// possible), and the real indexing is a plain off-by-one typo (`buf[5]` was
// clearly intended), not a load-bearing design choice - the same
// "typo normalized away, not reproduced as a fresh mistake in this port"
// treatment gameAgaruino.c's own header comment already established for an
// analogous case. Reimplemented as direct digit extraction
// (`(score/10^n)%10`), which also sidesteps the real `levels[game_level]`
// one-past-the-end read in `UpdateGameScore()` when `game_level` is already
// at `GAME_LEVEL_MAX` (harmless on real AVR - the level-up it would gate is
// already blocked by the separate `game_level < GAME_LEVEL_MAX` check
// regardless of what garbage value the OOB read returns - but not worth
// risking on this different architecture's own memory layout when the fix
// is a one-line bounds guard with a proven-identical outcome).
//
// Real, confirmed-dead upstream code, not ported (each verified by grepping
// every real source file for any call site - none exists): `ShowFrame()`/
// `ShowDebug()` (debug helpers, never called from `loop()` or anywhere
// else), `GamePause()` (defined but never invoked from the real `loop()`),
// `PlayerMegaBomb()`/`DrawMegaBomb()`/`has_megabomb` (the mega-bomb feature
// is fully commented out at every real call site), `DrawPlayerLife()` (its
// own real call in `DrawScore()` is commented out), and the unused
// `field_h`/`field_w`/`field_play` constants.

int sbuinoLogoBitmap[290] = { 64, 36, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x04, 0x7C, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x04, 0x44, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x04, 0x7C, 0x00, 0x40, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x90, 0x04, 0x7C, 0x00, 0x40, 0x00, 0x00, 0x01, 0xF8, 0x04, 0x20, 0x00, 0x40, 0x00, 0x04, 0x23, 0x6C, 0x04, 0x7C, 0x00, 0x40, 0x00, 0x02, 0x43, 0xFC, 0x04, 0x00, 0x00, 0x40, 0x00, 0x05, 0xA3, 0xFC, 0x04, 0x5C, 0x00, 0x40, 0x10, 0x8A, 0x53, 0x0C, 0x04, 0x00, 0x00, 0x40, 0x09, 0x08, 0x11, 0x98, 0x04, 0x7C, 0x00, 0x40, 0x1F, 0x8B, 0xD0, 0x00, 0x04, 0x04, 0x00, 0x40, 0x36, 0xCC, 0x30, 0x00, 0x04, 0x7C, 0x00, 0x40, 0x3F, 0xC6, 0x60, 0x40, 0x04, 0x00, 0x00, 0x40, 0x3F, 0xC0, 0x00, 0x40, 0x04, 0x28, 0x00, 0x40, 0x30, 0xC0, 0x00, 0x40, 0x04, 0x54, 0x00, 0x40, 0x19, 0x80, 0x00, 0x00, 0x04, 0x7C, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x40, 0x04, 0x00, 0x80, 0x00, 0x05, 0xF0, 0x00, 0x40, 0x04, 0x00, 0x80, 0x00, 0x05, 0x00, 0x00, 0x40, 0x04, 0x00, 0x80, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x05, 0xF0, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x05, 0x10, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x05, 0xF0, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x01, 0x80, 0x00, 0x05, 0xF0, 0x00, 0x40, 0x04, 0x01, 0x80, 0x00, 0x05, 0x10, 0x00, 0x40, 0x04, 0x05, 0xA0, 0x00, 0x05, 0xF0, 0x00, 0x40, 0x04, 0x05, 0xA0, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x07, 0xE0, 0x40, 0x05, 0xF0, 0x00, 0x40, 0x00, 0x0F, 0xF0, 0x40, 0x04, 0x40, 0x00, 0x40, 0x00, 0x0D, 0xB0, 0x40, 0x05, 0xF0, 0x00, 0x40, 0x00, 0x08, 0x10, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x05, 0x70, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x05, 0xD0, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00 };

int sbuinoScore0Bitmap[10] = { 8, 8, 0x00, 0x00, 0xEF, 0x81, 0x81, 0xEF, 0x00, 0x00 };
int sbuinoScore1Bitmap[10] = { 8, 8, 0x00, 0x00, 0xEF, 0xC0, 0x60, 0x30, 0x00, 0x00 };
int sbuinoScore2Bitmap[10] = { 8, 8, 0x00, 0x00, 0xE1, 0x91, 0x10, 0x8F, 0x00, 0x00 };
int sbuinoScore3Bitmap[10] = { 8, 8, 0x00, 0x00, 0xEF, 0x91, 0x00, 0x91, 0x00, 0x00 };
int sbuinoScore4Bitmap[10] = { 8, 8, 0x00, 0x00, 0xEF, 0x10, 0x10, 0xE0, 0x00, 0x00 };
int sbuinoScore5Bitmap[10] = { 8, 8, 0x00, 0x00, 0x8F, 0x91, 0x10, 0xE1, 0x00, 0x00 };
int sbuinoScore6Bitmap[10] = { 8, 8, 0x00, 0x00, 0x8F, 0x91, 0x10, 0xEF, 0x00, 0x00 };
int sbuinoScore7Bitmap[10] = { 8, 8, 0x00, 0x00, 0xEF, 0x80, 0x00, 0x80, 0x00, 0x00 };
int sbuinoScore8Bitmap[10] = { 8, 8, 0x00, 0x00, 0xEF, 0x91, 0x10, 0xEF, 0x00, 0x00 };
int sbuinoScore9Bitmap[10] = { 8, 8, 0x00, 0x00, 0xEF, 0x90, 0x10, 0xE0, 0x00, 0x00 };

int sbuinoSpaceshipBitmap[10] = { 8, 8, 0x18, 0x18, 0x5A, 0x5A, 0x7E, 0xFF, 0xDB, 0x81 };

int sbuinoInvader0Bitmap[10] = { 8, 8, 0x42, 0x24, 0x5A, 0xA5, 0x81, 0xBD, 0xC3, 0x66 };
int sbuinoInvaderExplose0Bitmap[10] = { 8, 8, 0x42, 0x00, 0x5A, 0x25, 0x80, 0xA5, 0x83, 0x64 };
int sbuinoInvader1Bitmap[10] = { 8, 8, 0x42, 0x24, 0x7E, 0xDB, 0xFF, 0xFF, 0xC3, 0x66 };
int sbuinoInvaderExplose1Bitmap[10] = { 8, 8, 0x42, 0x00, 0x66, 0xDB, 0x99, 0xE7, 0xC3, 0x00 };

// Icon-glyph footer strings - built as explicit int arrays[] rather than
// plain quoted string literals, matching this project's own established
// precedent (gameTaquin.c/gameUfoRace.c/gameStarships101.c) for real
// Gamebuino low-ASCII custom icon glyphs (21/22 = the real A/B button-icon
// glyphs) that a plain string literal can't hold. Real upstream text:
// "\x16:accept" and "\x15:accept \x16:cancel".
char sbuinoTextAcceptB[9] = { 22, 58, 97, 99, 99, 101, 112, 116, 0 }; // "<B icon>:accept"
char sbuinoTextMenuFooter[18] = { 21, 58, 97, 99, 99, 101, 112, 116, 32, 22, 58, 99, 97, 110, 99, 101, 108, 0 }; // "<A icon>:accept <B icon>:cancel"

#define SBUINO_PLAYER_W 8
#define SBUINO_PLAYER_H 8
#define SBUINO_NUM_BULLETS 10
#define SBUINO_NUM_INVADERS 10
#define SBUINO_NUM_INV_BULLETS 12
#define SBUINO_GAME_LEVEL_MAX 9
#define SBUINO_NUM_HIGHSCORE 5
#define SBUINO_FIELD_X1 15
#define SBUINO_FIELD_X2 69

// -----------------------------------------------------------------------------
// Game state - direct port of shootBuino.ino's own globals
// -----------------------------------------------------------------------------

bool sbuinoTitleShown = false; // real upstream setup()-time gb.titleScreen(logo) - see header comment
bool sbuinoInitialize = false;
bool sbuinoInitializeHighscore = false;
bool sbuinoGameMenu = false;
bool sbuinoGameHighscore = false;
bool sbuinoGameShowHighscore = true;
bool sbuinoGameOver = false;

int sbuinoGameForceLevel = 0; // real upstream dead-debug hook - never set anywhere else, kept for structural fidelity
int sbuinoGameLevel = 1;
int sbuinoGameMenuLevel = 1;

int sbuinoLevels[9] = { 0, 1000, 2000, 4000, 6000, 8000, 10000, 15000, 20000 };

int sbuinoGameDelai = 11;
int sbuinoGameScore = 0;
int sbuinoChainCombo = 0;
int sbuinoMaxChainCombo = 0;

int sbuinoAnimDefaultCounter = 5;
int sbuinoAnimCounter = 5;

// Player - real upstream `player_x`/`player_y` are float, but every real
// delta applied to them (`player_vx`/`player_vy`) is exactly 2, so the
// value is always integral at every real frame boundary either way; using
// plain `int` here is a proven zero-effect simplification that also avoids
// needing an explicit cast at every gbMin()/gbMax()/gbDrawBitmap() call
// site (those take `int` parameters).
int sbuinoPlayerH = SBUINO_PLAYER_H;
int sbuinoPlayerW = SBUINO_PLAYER_W;
int sbuinoPlayerX;
int sbuinoPlayerY;
int sbuinoPlayerVx = 2;
int sbuinoPlayerVy = 2;
int sbuinoPlayerLifeDefault = 10;
int sbuinoPlayerLife = 10; // matches sbuinoPlayerLifeDefault's own value (standard C forbids a non-constant global initializer) - reset on every new game in sbuinoInitGame() - see header comment
bool sbuinoPlayerExplosed = false;

int sbuinoBulletsX[10];
int sbuinoBulletsY[10];
float sbuinoBulletsVy = 2.5;
bool sbuinoBulletsActive[10];
int sbuinoLastInvaderShoot = -1;

// Invaders
int sbuinoInvaderH = 8;
int sbuinoInvaderW = 8;
bool sbuinoInvadersLevelup = false;
int sbuinoInvadersX[10];
int sbuinoInvadersY[10];
float sbuinoInvadersV = 0.2;
float sbuinoInvadersDefaultVy = 1.0;
float sbuinoInvadersVy = 1.0;
int sbuinoInvadersDefaultShootDelai = 5;
int sbuinoInvadersShootDelai = 5;
bool sbuinoInvadersActive[10];
int sbuinoInvadersExplosed[10][4]; // [type,x,y,animation]
int sbuinoInvadersType[10];
int sbuinoNbActiveInvaders = 0;

int sbuinoInvBulletsX[12];
int sbuinoInvBulletsY[12];
float sbuinoInvBulletsV = 0.25;
float sbuinoInvBulletsDefaultVy = 1.0;
float sbuinoInvBulletsVy = 1.0;
bool sbuinoInvBulletsActive[12];

// Highscore (RAM only - SAVE_EEPROM is 0 upstream, see header comment)
int sbuinoHighscoreNewIndex = -1;
int sbuinoHighchainNewIndex = -1;
int sbuinoHighscore[5] = { 0, 0, 0, 0, 0 };
int sbuinoHighchain[5] = { 0, 0, 0, 0, 0 };

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

// Arduino's ranged random(min,max) returns a value in [min,max) - this
// shim's own arand(n) only covers the single-argument [0,n) form.
int sbuinoRandRange( int lo, int hi )
{
    return lo + arand( hi - lo );
}

int sbuinoGetGameSpeed()
{
    if( sbuinoGameLevel == 1 )
      return sbuinoGameDelai;
    else
      return sbuinoGameDelai - sbuinoGameLevel;
}

int sbuinoGetInvaderShootDelai()
{
    if( sbuinoGameLevel <= 5 )
      return sbuinoGetGameSpeed() * sbuinoInvadersShootDelai;
    else
      return 30;
}

void sbuinoUpdateGameScore( int value, bool combo )
{
    int bonus = 0;

    if( combo )
    {
        sbuinoChainCombo = sbuinoChainCombo + 1;
    }
    else
    {
        if( sbuinoChainCombo > sbuinoMaxChainCombo )
          sbuinoMaxChainCombo = sbuinoChainCombo;

        bonus = sbuinoChainCombo * 10;
        sbuinoChainCombo = 0;
    }

    sbuinoGameScore = sbuinoGameScore + bonus + value;

    // Guarded by `sbuinoGameLevel < SBUINO_GAME_LEVEL_MAX` before indexing
    // sbuinoLevels[] - see header comment on the real upstream
    // `levels[game_level]` one-past-the-end read this avoids.
    if( sbuinoGameLevel < SBUINO_GAME_LEVEL_MAX && sbuinoGameScore > sbuinoLevels[ sbuinoGameLevel ] )
    {
        sbuinoInvadersLevelup = true;
        sbuinoGameLevel = sbuinoGameLevel + 1;
    }
}

// -----------------------------------------------------------------------------
// Sound
// -----------------------------------------------------------------------------

void sbuinoPlaySoundFxPlayerShoot()
{
    gbPlayTick();
}

void sbuinoPlaySoundFxInvaderHit()
{
    // Real upstream call here is commented out - invader hits are genuinely
    // silent on real hardware too.
}

void sbuinoPlaySoundFxGameOver()
{
    // See header comment - no gb.sound.command() equivalent exists in this
    // shim, so this uses the project's own established closest one-shot
    // substitute for a "you lost" cue.
    gbPlayCancel();
}

// -----------------------------------------------------------------------------
// Highscore
// -----------------------------------------------------------------------------

void sbuinoInitHighscore()
{
    int i;

    for( i = 0; i < SBUINO_NUM_HIGHSCORE; i = i + 1 )
    {
        sbuinoHighscore[ i ] = eeprom_read_word( i * 2 );
        if( sbuinoHighscore[ i ] == 0xFFFF ) sbuinoHighscore[ i ] = 0;
    }

    for( i = 0; i < SBUINO_NUM_HIGHSCORE; i = i + 1 )
    {
        sbuinoHighchain[ i ] = eeprom_read_word( SBUINO_NUM_HIGHSCORE * 2 + i * 2 );
        if( sbuinoHighchain[ i ] == 0xFFFF ) sbuinoHighchain[ i ] = 0;
    }

    sbuinoInitializeHighscore = true;
}

void sbuinoSaveHighscore( int* scores, int* indexOut, int score, int eepromBase )
{
    int tmpCurrentScore = 0;
    int tmpLastScore = 0;
    int i;

    *indexOut = -1;

    if( score > scores[ SBUINO_NUM_HIGHSCORE - 1 ] )
    {
        for( i = 0; i < SBUINO_NUM_HIGHSCORE; i = i + 1 )
        {
            if( *indexOut == -1 && scores[ i ] <= score )
            {
                tmpLastScore = scores[ i ];
                scores[ i ] = score;
                *indexOut = i;
            }
            else if( scores[ i ] <= score )
            {
                tmpCurrentScore = scores[ i ];
                scores[ i ] = tmpLastScore;
                tmpLastScore = tmpCurrentScore;
            }
        }

        for( i = 0; i < SBUINO_NUM_HIGHSCORE; i = i + 1 )
          eeprom_write_word( eepromBase + i * 2, scores[ i ] );
    }
}

void sbuinoSaveAllHighscore( int score, int chain )
{
    sbuinoSaveHighscore( sbuinoHighscore, &sbuinoHighscoreNewIndex, score, 0 );
    sbuinoSaveHighscore( sbuinoHighchain, &sbuinoHighchainNewIndex, chain, SBUINO_NUM_HIGHSCORE * 2 );
}

void sbuinoPrintRank( int rank1based )
{
    gbPrintNumber( rank1based );
    if( rank1based == 1 ) gbPrintString( "ST" );
    else if( rank1based == 2 ) gbPrintString( "ND" );
    else if( rank1based == 3 ) gbPrintString( "RD" );
    else gbPrintString( "TH" );
}

void sbuinoDrawHighscoreUpdate()
{
    int i;

    gbSetColor( 1 );
    gbCursorX = 16;
    gbCursorY = 1;
    if( sbuinoGameShowHighscore )
      gbPrintString( "-HIGH SCORES-" );
    else
      gbPrintString( "-HIGH CHAINS-" );

    for( i = 0; i < SBUINO_NUM_HIGHSCORE; i = i + 1 )
    {
        gbCursorX = 3;
        gbCursorY = 8 + ( i * 6 );

        sbuinoPrintRank( i + 1 );
        gbPrintString( " - " );

        if( sbuinoGameShowHighscore )
        {
            gbPrintNumber( sbuinoHighscore[ i ] );
            if( sbuinoHighscoreNewIndex == i )
              gbPrintString( "(NEW)" );
        }
        else
        {
            gbPrintNumber( sbuinoHighchain[ i ] );
            if( sbuinoHighchainNewIndex == i )
              gbPrintString( "(NEW)" );
        }
    }

    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( sbuinoTextAcceptB );

    if( gbPressed( BTN_B ) )
    {
        gbPlayOK();

        if( sbuinoGameShowHighscore )
        {
            sbuinoHighscoreNewIndex = -1;
            sbuinoGameShowHighscore = false;
        }
        else
        {
            sbuinoHighchainNewIndex = -1;
            sbuinoGameHighscore = false;
        }
    }
}

// -----------------------------------------------------------------------------
// Player
// -----------------------------------------------------------------------------

void sbuinoDrawExplosionPixel( int x, int y )
{
    if( x >= SBUINO_FIELD_X1 && x <= SBUINO_FIELD_X2 )
      gbDrawPixel( x, y );
}

void sbuinoDrawExplosion( int x, int y, int animationCounter )
{
    int d3 = ( 10 - animationCounter ) / 3;
    int d2 = ( 10 - animationCounter ) / 2;

    sbuinoDrawExplosionPixel( x + d3, y );
    sbuinoDrawExplosionPixel( x - d3, y );
    sbuinoDrawExplosionPixel( x, y + d3 );
    sbuinoDrawExplosionPixel( x, y - d3 );
    sbuinoDrawExplosionPixel( x + d2, y + d3 );
    sbuinoDrawExplosionPixel( x + d3, y + d2 );
    sbuinoDrawExplosionPixel( x - d2, y - d3 );
    sbuinoDrawExplosionPixel( x - d3, y - d2 );
}

void sbuinoDrawPlayerExplosion()
{
    if( sbuinoAnimCounter > 0 )
    {
        sbuinoDrawExplosion( sbuinoPlayerX, sbuinoPlayerY, sbuinoAnimCounter );
        sbuinoAnimCounter = sbuinoAnimCounter - 1;
    }
    else
    {
        sbuinoGameOver = true;
    }
}

void sbuinoDrawPlayer()
{
    if( !sbuinoPlayerExplosed )
      gbDrawBitmap( sbuinoPlayerX, sbuinoPlayerY, sbuinoSpaceshipBitmap );
    else
      sbuinoDrawPlayerExplosion();
}

void sbuinoPlayerShoot()
{
    int i;

    for( i = 0; i < SBUINO_NUM_BULLETS; i = i + 1 )
    {
        if( !sbuinoBulletsActive[ i ] )
        {
            sbuinoBulletsX[ i ] = sbuinoPlayerX + ( sbuinoPlayerW / 2 );
            sbuinoBulletsY[ i ] = sbuinoPlayerY;

            sbuinoPlaySoundFxPlayerShoot();

            sbuinoBulletsActive[ i ] = true;
            break;
        }
    }
}

void sbuinoMovePlayer()
{
    if( gbRepeat( BTN_RIGHT, 1 ) )
      sbuinoPlayerX = gbMin( SBUINO_FIELD_X2 - sbuinoPlayerW, sbuinoPlayerX + sbuinoPlayerVx );
    if( gbRepeat( BTN_LEFT, 1 ) )
      sbuinoPlayerX = gbMax( SBUINO_FIELD_X1 + 1, sbuinoPlayerX - sbuinoPlayerVx );
    if( gbRepeat( BTN_UP, 1 ) )
      sbuinoPlayerY = gbMax( 1, sbuinoPlayerY - sbuinoPlayerVy );
    if( gbRepeat( BTN_DOWN, 1 ) )
      sbuinoPlayerY = gbMin( LCDHEIGHT - sbuinoPlayerH, sbuinoPlayerY + sbuinoPlayerVy );
    if( gbRepeat( BTN_A, 5 ) )
      sbuinoPlayerShoot();
}

bool sbuinoCheckPlayerCollision( int x, int y )
{
    bool collision = false;
    int x1 = sbuinoPlayerX + 2;
    int x2 = sbuinoPlayerX + sbuinoPlayerW - 2;
    int y1 = sbuinoPlayerY;
    int y2 = sbuinoPlayerY + sbuinoPlayerH;

    if( !sbuinoPlayerExplosed && x >= x1 && x <= x2 && y <= y2 && y >= y1 )
    {
        collision = true;

        if( ( sbuinoPlayerLife - 1 ) >= 0 )
          sbuinoPlayerLife = sbuinoPlayerLife - 1;

        if( sbuinoPlayerLife == 0 )
        {
            sbuinoPlayerExplosed = true;
            sbuinoPlaySoundFxGameOver();
        }

        sbuinoUpdateGameScore( 0, false );
    }

    return collision;
}

// -----------------------------------------------------------------------------
// Invaders
// -----------------------------------------------------------------------------

void sbuinoInvaderExplosed( int index, int x, int y, int type )
{
    sbuinoInvadersExplosed[ index ][ 0 ] = type;
    sbuinoInvadersExplosed[ index ][ 1 ] = x;
    sbuinoInvadersExplosed[ index ][ 2 ] = y;
    sbuinoInvadersExplosed[ index ][ 3 ] = sbuinoAnimCounter;
}

bool sbuinoCheckInvaderCollision( int x, int y )
{
    bool collision = false;
    int x1, x2, y1, y2;
    int i;

    for( i = 0; i < SBUINO_NUM_INVADERS; i = i + 1 )
    {
        if( sbuinoInvadersActive[ i ] )
        {
            x1 = sbuinoInvadersX[ i ];
            x2 = sbuinoInvadersX[ i ] + sbuinoInvaderW;
            y1 = sbuinoInvadersY[ i ];
            y2 = sbuinoInvadersY[ i ] + sbuinoInvaderH;

            if( x >= x1 && x <= x2 && y <= y2 && y >= y1 )
            {
                sbuinoInvadersActive[ i ] = false;
                sbuinoNbActiveInvaders = sbuinoNbActiveInvaders - 1;

                sbuinoPlaySoundFxInvaderHit();
                sbuinoInvaderExplosed( i, sbuinoInvadersX[ i ], sbuinoInvadersY[ i ], sbuinoInvadersType[ i ] );

                if( sbuinoInvadersType[ i ] != sbuinoLastInvaderShoot )
                  sbuinoUpdateGameScore( 10, true );
                else
                  sbuinoUpdateGameScore( 10, false );

                sbuinoLastInvaderShoot = sbuinoInvadersType[ i ];

                collision = true;
                break;
            }
        }
    }

    return collision;
}

void sbuinoMoveBullets()
{
    int i;

    for( i = 0; i < SBUINO_NUM_BULLETS; i = i + 1 )
    {
        if( sbuinoBulletsActive[ i ] )
        {
            // Real upstream `bullets_y` is an int array but is decremented
            // by the float `bullets_vy` (2.5) every frame - the implicit
            // float->int truncation on every store means the real per-frame
            // step is actually a consistent -3 (y-2.5 always lands on a
            // ".5" value once y is itself an integer, and C truncates
            // toward zero), not a smooth -2.5. Preserved exactly via an
            // explicit cast (this dialect requires it), matching real
            // upstream's own actual compiled behavior, not the "intended"
            // 2.5px/frame the source's own naming suggests.
            sbuinoBulletsY[ i ] = (int)( (float)sbuinoBulletsY[ i ] - sbuinoBulletsVy );

            gbFillRect( sbuinoBulletsX[ i ], sbuinoBulletsY[ i ], 1, 3 );

            if( sbuinoCheckInvaderCollision( sbuinoBulletsX[ i ], sbuinoBulletsY[ i ] ) )
              sbuinoBulletsActive[ i ] = false;

            if( sbuinoBulletsY[ i ] <= 0 )
            {
                sbuinoBulletsActive[ i ] = false;
                sbuinoBulletsX[ i ] = 0;
                sbuinoBulletsY[ i ] = 0;
            }
        }
    }
}

void sbuinoDrawInvader( int x, int y, int type )
{
    if( type == 1 )
      gbDrawBitmap( x, y, sbuinoInvader1Bitmap );
    else
      gbDrawBitmap( x, y, sbuinoInvader0Bitmap );
}

void sbuinoDrawInvaderExplosion( int index )
{
    if( sbuinoInvadersExplosed[ index ][ 0 ] == 1 )
      gbDrawBitmap( sbuinoInvadersExplosed[ index ][ 1 ], sbuinoInvadersExplosed[ index ][ 2 ], sbuinoInvaderExplose1Bitmap );
    else
      gbDrawBitmap( sbuinoInvadersExplosed[ index ][ 1 ], sbuinoInvadersExplosed[ index ][ 2 ], sbuinoInvaderExplose0Bitmap );

    sbuinoInvadersExplosed[ index ][ 3 ] = sbuinoInvadersExplosed[ index ][ 3 ] - 1;
}

void sbuinoDrawInvaders()
{
    int i;

    for( i = 0; i < SBUINO_NUM_INVADERS; i = i + 1 )
    {
        if( sbuinoInvadersActive[ i ] )
        {
            sbuinoDrawInvader( sbuinoInvadersX[ i ], sbuinoInvadersY[ i ], sbuinoInvadersType[ i ] );

            // Real upstream `break`s the whole loop after the first
            // collision found this frame - any remaining active invaders
            // simply aren't drawn/checked this one frame (a harmless,
            // purely cosmetic single-frame skip, preserved exactly).
            if( sbuinoCheckPlayerCollision( sbuinoInvadersX[ i ] + ( sbuinoInvaderW / 2 ), sbuinoInvadersY[ i ] + ( sbuinoInvaderH / 2 ) ) )
              break;
        }
        else if( sbuinoInvadersExplosed[ i ][ 3 ] > 0 )
        {
            sbuinoDrawInvaderExplosion( i );
        }
    }
}

void sbuinoLaunchInvader( int index, int number, int posX, int posY, bool reverse, bool align, bool color )
{
    if( !reverse )
      sbuinoInvadersX[ index ] = posX + ( number * sbuinoInvaderW );
    else
      sbuinoInvadersX[ index ] = posX - ( number * sbuinoInvaderW );

    if( !align )
      sbuinoInvadersY[ index ] = posY + ( number * -5 );
    else
      sbuinoInvadersY[ index ] = posY;

    sbuinoInvadersActive[ index ] = true;
    if( color )
      sbuinoInvadersType[ index ] = 1;
    else
      sbuinoInvadersType[ index ] = 0;
    sbuinoNbActiveInvaders = sbuinoNbActiveInvaders + 1;
}

// Real upstream's own InvaderShoot() - note it loops SBUINO_NUM_BULLETS
// (10), not SBUINO_NUM_INV_BULLETS (12) - see header comment.
void sbuinoInvaderShoot( int invaderPosX, int invaderPosY )
{
    int speed = sbuinoGetGameSpeed();
    int delai = sbuinoGetInvaderShootDelai();

    if( speed == 0 || ( ( gbFrameCount % delai ) == 0 ) )
    {
        int i;

        for( i = 0; i < SBUINO_NUM_BULLETS; i = i + 1 )
        {
            if( !sbuinoInvBulletsActive[ i ] )
            {
                sbuinoInvBulletsX[ i ] = invaderPosX + ( sbuinoInvaderW / 2 );
                sbuinoInvBulletsY[ i ] = ( invaderPosY + ( sbuinoInvaderH / 2 ) ) + 3;
                sbuinoInvBulletsActive[ i ] = true;
                break;
            }
        }
    }
}

void sbuinoMoveInvaders()
{
    int posX = 0;
    int posY = 0;
    int type = 0;
    int reverse = arand( 2 );
    int align = arand( 2 );
    bool color = false;
    int speed;
    int lo, hi;
    int i;

    if( sbuinoGameLevel <= 2 )
      type = arand( 3 );
    else if( sbuinoGameLevel <= 5 )
      type = arand( 4 );
    else if( sbuinoGameLevel < 8 )
      type = arand( 5 );
    else if( sbuinoGameLevel >= 8 )
      type = arand( 6 );

    if( sbuinoLastInvaderShoot == 1 )
      color = true;
    else
      color = false;

    speed = sbuinoGetGameSpeed();
    if( speed == 0 || ( ( gbFrameCount % speed ) == 0 ) )
    {
        for( i = 0; i < SBUINO_NUM_INVADERS; i = i + 1 )
        {
            if( sbuinoInvadersActive[ i ] )
            {
                sbuinoInvadersY[ i ] = (int)( (float)sbuinoInvadersY[ i ] + sbuinoInvadersVy );

                if( sbuinoInvadersY[ i ] > LCDHEIGHT )
                {
                    sbuinoInvadersX[ i ] = 0;
                    sbuinoInvadersY[ i ] = 0;
                    sbuinoInvadersActive[ i ] = false;
                    sbuinoNbActiveInvaders = sbuinoNbActiveInvaders - 1;
                }
                else
                {
                    sbuinoInvaderShoot( sbuinoInvadersX[ i ], sbuinoInvadersY[ i ] );
                }
            }
        }

        if( sbuinoNbActiveInvaders == 0 )
        {
            if( sbuinoInvadersLevelup )
            {
                sbuinoInvadersVy = sbuinoInvadersDefaultVy + ( (float)sbuinoGameLevel * sbuinoInvadersV );
                sbuinoInvBulletsVy = sbuinoInvBulletsDefaultVy + ( (float)sbuinoGameLevel * sbuinoInvBulletsV );

                sbuinoInvadersLevelup = false;
            }

            if( type == 1 )
            {
                if( reverse == 0 ) { lo = SBUINO_FIELD_X1; hi = SBUINO_FIELD_X2 - ( sbuinoInvaderW * 2 ); }
                else { lo = SBUINO_FIELD_X1 + ( sbuinoInvaderW * 2 ); hi = SBUINO_FIELD_X2 - sbuinoInvaderW; }
                posX = sbuinoRandRange( lo, hi );

                sbuinoLaunchInvader( 0, 0, posX, posY, ( reverse == 1 ), ( align == 1 ), color );
                sbuinoLaunchInvader( 1, 1, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );
            }
            else if( type == 2 )
            {
                if( reverse == 0 ) { lo = SBUINO_FIELD_X1; hi = SBUINO_FIELD_X2 - ( sbuinoInvaderW * 3 ); }
                else { lo = SBUINO_FIELD_X1 + ( sbuinoInvaderW * 3 ); hi = SBUINO_FIELD_X2 - sbuinoInvaderW; }
                posX = sbuinoRandRange( lo, hi );

                sbuinoLaunchInvader( 0, 0, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );
                sbuinoLaunchInvader( 1, 1, posX, posY, ( reverse == 1 ), ( align == 1 ), color );
                sbuinoLaunchInvader( 2, 2, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );
            }
            else if( type == 3 )
            {
                if( reverse == 0 ) { lo = SBUINO_FIELD_X1; hi = SBUINO_FIELD_X2 - ( sbuinoInvaderW * 4 ); }
                else { lo = SBUINO_FIELD_X1 + ( sbuinoInvaderW * 4 ); hi = SBUINO_FIELD_X2 - sbuinoInvaderW; }
                posX = sbuinoRandRange( lo, hi );

                sbuinoLaunchInvader( 0, 0, posX, posY, ( reverse == 1 ), ( align == 1 ), color );
                sbuinoLaunchInvader( 1, 1, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );
                sbuinoLaunchInvader( 2, 2, posX, posY, ( reverse == 1 ), ( align == 1 ), color );
                sbuinoLaunchInvader( 3, 3, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );
            }
            else if( type == 4 )
            {
                posX = SBUINO_FIELD_X1 + 2;
                sbuinoLaunchInvader( 0, 0, posX, posY, false, ( align == 1 ), color );
                sbuinoLaunchInvader( 1, 1, posX, posY, false, ( align == 1 ), !color );
                sbuinoLaunchInvader( 2, 2, posX, posY, false, ( align == 1 ), color );

                posX = SBUINO_FIELD_X2 - ( sbuinoInvaderW * 3 ) - 1;
                sbuinoLaunchInvader( 3, 0, posX, posY, false, ( align == 1 ), !color );
                sbuinoLaunchInvader( 4, 1, posX, posY, false, ( align == 1 ), color );
                sbuinoLaunchInvader( 5, 2, posX, posY, false, ( align == 1 ), !color );
            }
            else if( type == 5 )
            {
                if( reverse == 0 ) { lo = SBUINO_FIELD_X1; hi = SBUINO_FIELD_X2 - ( ( sbuinoInvaderW * 3 ) + ( sbuinoInvaderW / 2 ) ); }
                else { lo = SBUINO_FIELD_X1 + ( ( sbuinoInvaderW * 3 ) + ( sbuinoInvaderW / 2 ) ); hi = SBUINO_FIELD_X2 - sbuinoInvaderW; }
                posX = sbuinoRandRange( lo, hi );

                sbuinoLaunchInvader( 0, 0, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );
                sbuinoLaunchInvader( 1, 1, posX, posY, ( reverse == 1 ), ( align == 1 ), color );
                sbuinoLaunchInvader( 2, 2, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );

                if( reverse == 0 )
                  posX = posX + ( sbuinoInvaderW / 2 );
                else
                  posX = posX - ( sbuinoInvaderW / 2 );

                if( align == 1 )
                  posY = posY - ( sbuinoInvaderH + 2 );
                else
                  posY = posY - ( sbuinoInvaderH + ( sbuinoInvaderH / 2 ) + 1 );

                sbuinoLaunchInvader( 3, 0, posX, posY, ( reverse == 1 ), ( align == 1 ), color );
                sbuinoLaunchInvader( 4, 1, posX, posY, ( reverse == 1 ), ( align == 1 ), !color );
                sbuinoLaunchInvader( 5, 2, posX, posY, ( reverse == 1 ), ( align == 1 ), color );
            }
            else
            {
                posX = sbuinoRandRange( SBUINO_FIELD_X1, SBUINO_FIELD_X2 - sbuinoInvaderW );
                sbuinoLaunchInvader( 0, 0, posX, posY, false, false, !color );
            }
        }
    }
}

void sbuinoMoveInvadersBullets()
{
    int i;

    // Loops SBUINO_NUM_BULLETS (10), not SBUINO_NUM_INV_BULLETS (12) -
    // matching sbuinoInvaderShoot()'s own real upstream bound (see header
    // comment).
    for( i = 0; i < SBUINO_NUM_BULLETS; i = i + 1 )
    {
        if( sbuinoInvBulletsActive[ i ] )
        {
            sbuinoInvBulletsY[ i ] = (int)( (float)sbuinoInvBulletsY[ i ] + sbuinoInvBulletsVy );

            // Fixed here, not preserved - see this file's own header
            // comment for the real upstream bug this replaces
            // (`fillRect(...,1,-2)` - a negative height, which drew
            // nothing at all, making every invader bullet completely
            // invisible even though it could still hit the player). Drawn
            // as a 2px-tall vertical bullet ending at its own logical Y
            // position (the same "extend 2px upward from Y" shape the
            // negative height was clearly meant to produce), rather than
            // just flipping the sign, which would instead draw downward
            // from Y and shift the visible bullet off its real position.
            gbFillRect( sbuinoInvBulletsX[ i ], sbuinoInvBulletsY[ i ] - 2, 1, 2 );

            sbuinoCheckPlayerCollision( sbuinoInvBulletsX[ i ], sbuinoInvBulletsY[ i ] );

            if( sbuinoInvBulletsY[ i ] >= LCDHEIGHT )
            {
                sbuinoInvBulletsActive[ i ] = false;
                sbuinoInvBulletsX[ i ] = 0;
                sbuinoInvBulletsY[ i ] = 0;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Field / score
// -----------------------------------------------------------------------------

void sbuinoDrawField()
{
    gbDrawLine( SBUINO_FIELD_X1, 0, SBUINO_FIELD_X1, LCDHEIGHT );
    gbDrawLine( SBUINO_FIELD_X2, 0, SBUINO_FIELD_X2, LCDHEIGHT );
}

void sbuinoDrawDigit( int index, int value )
{
    if( value == 1 ) gbDrawBitmap( 6, index * 5, sbuinoScore1Bitmap );
    else if( value == 2 ) gbDrawBitmap( 6, index * 5, sbuinoScore2Bitmap );
    else if( value == 3 ) gbDrawBitmap( 6, index * 5, sbuinoScore3Bitmap );
    else if( value == 4 ) gbDrawBitmap( 6, index * 5, sbuinoScore4Bitmap );
    else if( value == 5 ) gbDrawBitmap( 6, index * 5, sbuinoScore5Bitmap );
    else if( value == 6 ) gbDrawBitmap( 6, index * 5, sbuinoScore6Bitmap );
    else if( value == 7 ) gbDrawBitmap( 6, index * 5, sbuinoScore7Bitmap );
    else if( value == 8 ) gbDrawBitmap( 6, index * 5, sbuinoScore8Bitmap );
    else if( value == 9 ) gbDrawBitmap( 6, index * 5, sbuinoScore9Bitmap );
    else gbDrawBitmap( 6, index * 5, sbuinoScore0Bitmap );
}

// Real upstream builds these 6 digits via sprintf("%06i") into a too-small
// buffer and then indexes one past its end for the last digit - see header
// comment. Reimplemented as plain digit extraction (this dialect has no
// sprintf() anyway), which also fixes the real off-by-one typo.
void sbuinoDrawGraphicalScore()
{
    int d1 = ( sbuinoGameScore / 100000 ) % 10;
    int d2 = ( sbuinoGameScore / 10000 ) % 10;
    int d3 = ( sbuinoGameScore / 1000 ) % 10;
    int d4 = ( sbuinoGameScore / 100 ) % 10;
    int d5 = ( sbuinoGameScore / 10 ) % 10;
    int d6 = sbuinoGameScore % 10;

    sbuinoDrawDigit( 0, d6 );
    sbuinoDrawDigit( 1, d5 );
    sbuinoDrawDigit( 2, d4 );
    sbuinoDrawDigit( 3, d3 );
    sbuinoDrawDigit( 4, d2 );
    sbuinoDrawDigit( 5, d1 );
}

void sbuinoDrawChain()
{
    gbCursorX = 72;
    gbCursorY = 5;
    gbPrintString( "ch." );
    gbCursorX = 72;
    gbCursorY = 13;
    gbPrintNumber( sbuinoChainCombo );

    if( sbuinoLastInvaderShoot == 0 )
      gbDrawBitmap( 72, 20, sbuinoInvader0Bitmap );
    else if( sbuinoLastInvaderShoot == 1 )
      gbDrawBitmap( 72, 20, sbuinoInvader1Bitmap );

    gbCursorX = 72;
    gbCursorY = 40;
    if( sbuinoGameLevel < SBUINO_GAME_LEVEL_MAX )
    {
        gbPrintString( "S." );
        gbPrintNumber( sbuinoGameLevel );
    }
    else
    {
        gbPrintString( "Max" );
    }
}

void sbuinoDrawScore()
{
    sbuinoDrawGraphicalScore();
    sbuinoDrawChain();
}

// -----------------------------------------------------------------------------
// Game flow
// -----------------------------------------------------------------------------

void sbuinoInitGame()
{
    int i;

    if( !sbuinoInitializeHighscore )
      sbuinoInitHighscore();

    for( i = 0; i < SBUINO_NUM_BULLETS; i = i + 1 )
    {
        sbuinoBulletsActive[ i ] = false;
        sbuinoBulletsX[ i ] = 0;
        sbuinoBulletsY[ i ] = 0;
    }

    for( i = 0; i < SBUINO_NUM_INVADERS; i = i + 1 )
    {
        sbuinoInvadersActive[ i ] = false;
        sbuinoInvadersType[ i ] = 0;
        sbuinoInvadersX[ i ] = 0;
        sbuinoInvadersY[ i ] = 0;
        sbuinoInvadersExplosed[ i ][ 0 ] = 0;
        sbuinoInvadersExplosed[ i ][ 1 ] = 0;
        sbuinoInvadersExplosed[ i ][ 2 ] = 0;
    }
    sbuinoInvadersVy = sbuinoInvadersDefaultVy;
    sbuinoInvadersLevelup = false;
    sbuinoInvadersShootDelai = sbuinoInvadersDefaultShootDelai;

    sbuinoNbActiveInvaders = 0;

    for( i = 0; i < SBUINO_NUM_INV_BULLETS; i = i + 1 )
    {
        sbuinoInvBulletsActive[ i ] = false;
        sbuinoInvBulletsX[ i ] = 0;
        sbuinoInvBulletsY[ i ] = 0;
    }
    sbuinoInvBulletsVy = sbuinoInvBulletsDefaultVy;

    // Fixed here, not preserved - see this file's own header comment for
    // the real upstream bug this replaces (player_life was only ever
    // initialized once, never reset on a new game - combined with the
    // AVR-integer-promotion guard, once the player died once in a
    // session, every subsequent restart started with life already at 0
    // and died again on the very first hit, permanently, for the rest of
    // the session).
    sbuinoPlayerLife = sbuinoPlayerLifeDefault;
    sbuinoPlayerH = SBUINO_PLAYER_H;
    sbuinoPlayerW = SBUINO_PLAYER_W;
    sbuinoPlayerX = ( LCDWIDTH / 2 ) - ( SBUINO_PLAYER_W / 2 );
    sbuinoPlayerY = LCDHEIGHT - sbuinoPlayerH;
    sbuinoPlayerExplosed = false;

    sbuinoLastInvaderShoot = -1;
    sbuinoMaxChainCombo = 0;
    sbuinoGameScore = 0;
    sbuinoChainCombo = 0;

    if( sbuinoGameForceLevel > 0 )
      sbuinoGameLevel = sbuinoGameForceLevel;
    else if( sbuinoGameMenuLevel != 1 )
      sbuinoGameLevel = sbuinoGameMenuLevel;
    else
      sbuinoGameLevel = 1;

    sbuinoInvadersLevelup = true;
    sbuinoGameMenuLevel = sbuinoGameLevel;
    sbuinoGameShowHighscore = true;

    sbuinoAnimCounter = sbuinoAnimDefaultCounter;

    sbuinoGameOver = false;

    sbuinoInitialize = true;
}

void sbuinoPlay()
{
    if( !sbuinoPlayerExplosed )
      sbuinoMovePlayer();

    sbuinoMoveInvaders();
    sbuinoMoveInvadersBullets();
    sbuinoMoveBullets();

    sbuinoDrawField();
    sbuinoDrawPlayer();
    sbuinoDrawInvaders();

    sbuinoDrawScore();
}

void sbuinoGameOverUpdate()
{
    gbCursorX = 22;
    gbCursorY = 1;
    gbPrintString( "!GAME OVER!" );

    gbCursorX = 0;
    gbCursorY = 10;
    gbPrintString( "Score: " );
    gbPrintNumber( sbuinoGameScore );

    gbCursorX = 0;
    gbCursorY = 20;
    gbPrintString( "Max. Chain: " );
    gbPrintNumber( sbuinoMaxChainCombo );

    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( sbuinoTextAcceptB );

    if( gbPressed( BTN_B ) )
    {
        gbPlayOK();

        sbuinoSaveAllHighscore( sbuinoGameScore, sbuinoMaxChainCombo );

        sbuinoGameHighscore = true;
        sbuinoInitialize = false;
        sbuinoGameOver = false;
    }
}

void sbuinoGameMenuUpdate()
{
    gbCursorX = 5;
    gbCursorY = 1;
    gbPrintString( "-CHOOSE GAME LEVEL-" );

    gbFillTriangle( 30, 10, 25, 15, 35, 15 );
    gbFillTriangle( 30, 28, 25, 23, 35, 23 );

    gbCursorX = 0;
    gbCursorY = 17;
    gbPrintString( "LEVEL: " );
    gbPrintNumber( sbuinoGameMenuLevel );

    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( sbuinoTextMenuFooter );

    if( gbPressed( BTN_UP ) )
    {
        if( ( sbuinoGameMenuLevel + 1 ) <= SBUINO_GAME_LEVEL_MAX )
          sbuinoGameMenuLevel = sbuinoGameMenuLevel + 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        if( ( sbuinoGameMenuLevel - 1 ) >= 1 )
          sbuinoGameMenuLevel = sbuinoGameMenuLevel - 1;
    }
    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();

        sbuinoGameLevel = sbuinoGameMenuLevel;
        sbuinoInitialize = false;
        sbuinoGameMenu = false;
    }
    if( gbPressed( BTN_B ) )
    {
        gbPlayOK();

        sbuinoGameMenuLevel = sbuinoGameLevel;
        sbuinoGameMenu = false;
    }
    if( gbPressed( BTN_C ) )
    {
        gbPlayOK();

        // upstream: gb.changeGame() - see header comment.
        sbuinoInitialize = false;
        sbuinoGameMenu = false;
        sbuinoTitleShown = false;
    }
}

void sbuinoUpdateTitle()
{
    // Real logo bitmap is 64x36 - centered horizontally on the 84px-wide
    // display ((84-64)/2 = 10), placed near the top so "PRESS A" still fits
    // below it - see header comment on why this differs from real
    // titleScreen()'s own fixed x=0 placement.
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, sbuinoLogoBitmap );

    gbFontSize = 1;
    gbCursorX = 14;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      sbuinoTitleShown = true;
}

void gameShootBuino_init()
{
    gbBegin();
    gbSetFrameRate( 20 ); // matches real upstream's own game_frame_rate=20 (also this shim's own real default)
    gbSetColor( 1 );
}

void gameShootBuino_update()
{
    if( !gbUpdate() ) return;

    if( !sbuinoTitleShown )
    {
        sbuinoUpdateTitle();
        gbRenderFrame();
        return;
    }

    if( !sbuinoInitialize )
      sbuinoInitGame();

    if( !sbuinoGameOver && !sbuinoGameMenu && !sbuinoGameHighscore )
    {
        if( gbPressed( BTN_C ) )
        {
            gbPlayCancel();
            sbuinoGameMenu = true;
        }
        sbuinoPlay();
    }
    else if( sbuinoGameMenu )
      sbuinoGameMenuUpdate();
    else if( sbuinoGameHighscore )
      sbuinoDrawHighscoreUpdate();
    else if( sbuinoGameOver )
      sbuinoGameOverUpdate();

    gbRenderFrame();
}
