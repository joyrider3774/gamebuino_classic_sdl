#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// DarkShmup (Clement83 - github.com/Clement83/DarkShmup, license: none
// specified). Confirmed the real, genuine repo directly (not the earlier,
// entirely fabricated "Frakasss/DarkShmup" this project has already caught
// once before): `git remote -v` on the staged clone shows
// `origin https://github.com/Clement83/DarkShmup.git`, and its own git log
// shows a real commit by `Clement83 <clement@quintard.me>` - matching the
// same author already credited elsewhere in this cartridge for Copter/
// GlaciGlaca/CrazyTown/Bomber/StickFighter/Tron. No LICENSE file exists in
// the repo, matching the "none specified" license given for this port.
//
// A vertical-scrolling shoot-'em-up with a real, distinctive "dark world"
// mechanic: enemies belong to one of two dimensions (TypeDark 0/1), only
// one of which is visible/hittable at a time, and Button B swaps which
// dimension is active - the player's own ship always stays visible in
// both. Real upstream ships 3 selectable player skins (a slow heavy
// twin-cannon ship, a balanced ship, and a small fast single-shot ship),
// picked on a real ship-select screen shown once after the title screen.
//
// STRUCTURAL NOTE: like gameSuperSpaceShooter.c, real upstream here has no
// gameplay logic in its own class files at all - StarShip.h/StarShipPlayer.h/
// Bullet.h/Explosion.h are all genuinely pure, method-free data-only C++
// classes (their own matching .cpp files are each just a single
// `#include "X.h"` line, confirmed by reading all four directly), with
// every real behavior living in DarkShmup.ino/PlayerHelper.ino/
// StarShipHelper.ino/DefinitionPattern.ino as free functions taking a
// pointer/reference. This made the flattening simpler than
// gameSuperSpaceShooter.c's own (no real method bodies to relocate, only
// class->struct + pointer-taking-function->index-taking-function
// mechanical rewrites):
//   StarShip[18]        -> struct DshmupShip;    DshmupShip[18] dshmupEnemies;
//   Bullet[15] (x2)      -> struct DshmupBullet;  DshmupBullet[15] dshmupEnemyBullets;
//                                                  DshmupBullet[15] dshmupPlayerBullets;
//   Explosion[5]         -> struct DshmupExplosion; DshmupExplosion[5] dshmupExplosions;
//   StarShipPlayer        -> plain globals (dshmupPlayerPosX/Y/etc) - only
//     one ever exists, matching gameSuperSpaceShooter.c's own identical
//     treatment of its single-instance Player.
// Two real struct fields were found to be genuinely dead (never read or
// assigned anywhere in the real source, confirmed via a full grep sweep)
// and were dropped rather than ported: `Bullet::TypeDark`/`Bullet::Skin`
// (declared, never touched for any real bullet instance) and
// `StarShipPlayer::TypeDark` (declared, never touched for the real player
// instance either - only `StarShip::TypeDark`, the enemy-dimension flag,
// is ever actually used).
//
// DIALECT REWRITES: every real `gb.x.y(...)` call site became a plain
// `gbY(...)` call (see gamePong.c's own header comment). `int8_t`/
// `uint8_t`/`byte` all become plain `int` (avrCompat.h aliasing - no
// narrower integer type exists in this dialect anyway). Real upstream's
// own bitmap byte tables were ALREADY written as plain `0x..` hex
// literals (not AVR `B`-binary literals), so no literal-format conversion
// was needed at all here (unlike most other ports in this cartridge) -
// every byte was copied verbatim, with byte counts verified by script
// against each bitmap's own declared width/height
// (`2 + ceil(width/8)*height`) rather than trusted by eye. `random(a,b)`
// became `a + arand(b-a)` (this project's own established real-Arduino-
// semantics-preserving convention, `[a,b)` upper-exclusive);
// `random(0,n)`-shaped calls simplify to plain `arand(n)`. Real upstream's
// own `switch` statements with no `default:` case (`GetSpriteEnnemi()`/
// `GetSpritePlayer()`) gained a safe fallback `return` purely to satisfy
// this dialect's own requirement that every code path return a value -
// never actually reached, since every real call site already only ever
// passes a value the switch already handles.
//
// UPSTREAM BUGS/QUIRKS - preserved as genuine, load-bearing original
// behavior (traced through each one directly against the real source, not
// assumed):
// - **No death/game-over handling exists anywhere in real upstream** -
//   `player.Life` is decremented on every enemy-bullet hit with no `<= 0`
//   check anywhere in the whole codebase (confirmed via a full grep sweep
//   for every real `player.Life`/`(&player)->Life` reference), so a real
//   cartridge just keeps playing forever regardless of health. This
//   project's own porting-priority audit had already flagged
//   `gameSuperSpaceShooter.c` as "a real prototype, not a finished game" -
//   DarkShmup is the same story, preserved the same way (no invented
//   game-over screen).
// - **`StarShipPlayer::Life` is a real `uint8_t`** (StarShipPlayer.h) -
//   since nothing ever stops it decrementing below 0, real AVR hardware
//   narrows every `Life -= Dmg` assignment modulo 256, so health actually
//   *wraps back up* to a large positive number instead of going negative
//   (e.g. 0 - 1 narrows to 255) - meaning real hardware's own "no death"
//   behavior above is even more literally true: the player cyclically
//   "heals" back up after passing 0 rather than sitting at an
//   ever-more-negative number. This dialect's `int` never narrows (see
//   VIRCON32_C_DIALECT.md), so the one real decrement site
//   (`dshmupUpdateAndDrawBulletEnnemies()`) explicitly masks with `& 255`
//   to reproduce the real uint8_t wraparound bit-for-bit, rather than
//   silently drifting into unbounded negative numbers on the HUD - a
//   direct application of this project's own established EEPROM-audit-era
//   narrow-int scrutiny to a live gameplay field, not just persisted data.
//   `StarShip::Life` (the enemy health field) is real `int8_t`, but was
//   checked and left as plain `int`: it only ever accumulates a handful of
//   small hits (max concurrent player bullets is 15, each dealing at most
//   2 damage) before the per-tick `Life <= 0` death check removes the
//   enemy, nowhere near int8_t's own -128..127 wraparound range.
// - **`Score`/`OldScore` are real `unsigned int`** (16-bit on AVR,
//   0..65535) - `dshmupScore`/`dshmupOldScore` explicitly mask with
//   `& 65535` after every increment to reproduce real hardware's own
//   16-bit wraparound, reachable in practice given the game's own real
//   "plays forever" design (~720 enemy kills at +91/kill).
// - **`updatePosVaisseauEnnemie()`'s own real `switch` fallthrough bug**:
//   `case 2:` (the boss "Tfi" ship, spawned by `PopPatternBoss1()`) has no
//   `break;`, so it falls through into `case 3:`'s own body too - meaning
//   a real skin-2 enemy gets BOTH `UpdateTFightStarShip()` AND
//   `UpdateBugsStarShip()` applied to it every single tick, not just the
//   one its skin nominally selects. Preserved exactly in
//   `dshmupUpdatePosVaisseauEnnemie()`.
// - **`tasseTabEnnemie()`'s own real "velocity left behind" bug**: the
//   real array-compaction loop (used to remove a dead enemy by shifting
//   every later element one slot down) copies `TypeDark`/`PosX`/`PosY`/
//   `Life`/`Skin`/`FrameLife`/`NbFrameLife` field-by-field, but never
//   copies `VX`/`VY` - so after compaction, the enemy that moved down a
//   slot keeps whatever velocity was already sitting in that slot from a
//   *previous* occupant, not its own real velocity, until its own
//   movement AI happens to overwrite it. Preserved exactly in
//   `dshmupTasseTabEnnemie()` (only the same 7 fields are copied).
// - **`PopPattern3()`'s own real inverted break condition** - its loop
//   guard is `if(nbEnemisAlive<=NBMAX_ENNEMI) break;`, which is true on
//   essentially every real call (this function is only ever invoked from
//   `itsTimeToPop()`, itself only called once `nbEnemisAlive` has already
//   dropped to <= 4, always comfortably under the 18-enemy cap) - so the
//   loop breaks on its very first iteration almost every time, and
//   `PopPattern3()` is, in real practice, dead weight: roughly a quarter
//   of all real "time to spawn more enemies" events
//   (`itsTimeToPop()`'s own `random(0,4)` picks uniformly among 4
//   patterns) silently spawn nothing at all. Preserved exactly in
//   `dshmupPopPattern3()`, including the inverted condition.
// - **Bullet-pool/explosion-pool exhaustion is a real, silent no-op**
//   (`PlayerFire()`/`addBullet()`/`addExplosion()`'s own real
//   `while(...IsAlive && posB < MAX) posB++; if(posB < MAX) { ... }`
//   shape) - once a pool is full, the newest shot/explosion is simply
//   dropped, never overwriting an existing live entry. This is a
//   genuinely different (and safer) pattern than
//   `gameSuperSpaceShooter.c`'s own real "silently reuse the last slot
//   once full" upstream quirk documented in that file's own header
//   comment - both are real, correctly-transcribed upstream behaviors,
//   just from two different real games.
//
// DEAD CODE, CONFIRMED VIA A FULL GREP SWEEP AND NOT PORTED: `EtoileDeco`/
// `Munition`/`infinie` (DarkShmup.ino) and `BonusMunition`/`Upgrade`/`Vie`
// (BonusHelper.ino) are six real bitmap byte arrays that are declared but
// never once drawn or otherwise referenced anywhere in the real source -
// apparently laid groundwork for a bonus/ammo system (`NbMunition = 1000;`,
// also declared and never read or written anywhere else) that was never
// actually wired up in the shipped game. `displayText()` (DarkShmup.ino)
// is likewise declared but never called anywhere. None of these seven
// were ported, matching this project's own established "don't invent a
// feature real upstream never actually wired up" precedent (e.g.
// `gameParachute.c`'s own never-displayed `highscore` field).
//
// `gb.getFreeRam()`/`gb.getCpuLoad()` (both drawn every frame in the real
// `updateAndDrawHud()`, top corners of the HUD) have no meaningful
// Vircon32 equivalent - dropped outright, matching this project's already-
// established `gb.battery.show = false` precedent for this class of
// real-hardware-only cosmetic debug readout. `gb.battery.show = false`
// itself (set in both `ChoixVaisseau()` and `drawWorld()`) was dropped the
// same way.
//
// A genuine, documented judgment call: real `ChoixVaisseau()`'s own
// `gb.display.print(GetResumePlayer(...))` call (drawing each ship's
// description text on the ship-select screen) never explicitly resets
// `cursorX`/`cursorY` before each frame's own print call, and this
// isolated workspace has no access to real `Display::write()`'s own exact
// bottom-of-screen cursor-wrap source to verify whether that omission is
// actually harmless on real hardware (e.g. an implicit wrap-to-top) or a
// real, live bug (the multi-line paragraph's own cursor drifting further
// down-screen every single frame, since nothing ever resets it, eventually
// printing off-screen). Rather than guess, this port explicitly resets the
// cursor to a fixed position before printing the description every frame
// - a small, deliberate deviation from a literal transcription, chosen to
// guarantee a legible, stable result. Real upstream's own `textWrap = true`
// (no shim equivalent - this shim has no automatic word-wrap) is
// approximated the same way every other paragraph of upstream Gamebuino
// text in this cartridge has been: the three real ship-description
// strings are manually broken into short lines with embedded `\n`s at
// sensible word boundaries, preserving upstream's own real (typo-included)
// French text verbatim rather than correcting it.
//
// Confirmed via a full grep sweep of every real `.ino`/`.cpp`/`.h` file:
// real upstream DarkShmup has **no sound calls of any kind** (no
// `gb.sound.*` anywhere) and **no EEPROM/highscore concept at all** (no
// `EEPROM.h` include, no save/load call, `Score` is a pure in-session
// value with nothing to persist) - so this port adds neither sound effects
// nor EEPROM persistence, matching real upstream exactly rather than
// inventing either.

typedef struct{
    int TypeDark; // 0 = light world, 1 = dark world
    int Skin;
    int PosX;
    int PosY;
    int VX;
    int VY;
    int Life;
    int NbFrameLife;
    int FrameLife;
} DshmupShip;

typedef struct{
    int PosX;
    int PosY;
    int Dmg;
    bool IsAlive;
} DshmupBullet;

typedef struct{
    int PosX;
    int PosY;
    int Temps;
    int TempsMax;
    bool IsAlive;
} DshmupExplosion;

#define DSHMUP_NBMAX_ENNEMI 18
#define DSHMUP_NBMIN_ENNEMI_SCREEN 4
#define DSHMUP_NBMAX_ENNEMI_BULLET 15
#define DSHMUP_NBMAX_PLAYER_BULLET 15
#define DSHMUP_NBMAX_EXPLOSION 5
#define DSHMUP_NB_SKIN_PLAYER 3
#define DSHMUP_VITESSE_SHIP 1
#define DSHMUP_OFFSET_DEF 17

#define DSHMUP_STATE_TITLE 0
#define DSHMUP_STATE_SHIPSELECT 1
#define DSHMUP_STATE_PLAY 2

DshmupShip dshmupEnemies[18];
DshmupBullet dshmupEnemyBullets[15];
DshmupBullet dshmupPlayerBullets[15];
DshmupExplosion dshmupExplosions[5];

int dshmupNbEnemisAlive;
int dshmupScore;
int dshmupOldScore;
int dshmupIsInDarkWorld; // 0/1, matches real StarShip::TypeDark's own int range

int dshmupPlayerSkin;
int dshmupPlayerPosX;
int dshmupPlayerPosY;
int dshmupPlayerLife;
int dshmupPlayerSpeedX;
int dshmupPlayerMaxSpeedX;
int dshmupPlayerPuissanceTire;

int dshmupState;
int dshmupShipSelectOffset;

// -----------------------------------------------------------------------------
// Real bitmap data (DarkShmup.ino/PlayerHelper.ino/StarShipHelper.ino),
// copied verbatim - already real 0x.. hex literals upstream, no literal-
// format conversion needed. Byte counts verified by script against each
// bitmap's own declared width/height before transcription.
// -----------------------------------------------------------------------------

int dshmupTitleScreenBitmap[250] = {
    64, 31, 0x1, 0xFF, 0xFF, 0xFE, 0x0, 0x78, 0x0, 0x10, 0x1, 0xFF, 0xFF, 0xFE, 0x3C, 0x3,
    0x80, 0x70, 0x3, 0xFF, 0xFF, 0xC0, 0x0, 0x0, 0x3, 0xF0, 0x7F, 0xFF, 0xFF, 0x80, 0x0, 0x0,
    0xF, 0xF0, 0x7F, 0xFF, 0xFF, 0x0, 0x0, 0x0, 0x3F, 0xF0, 0x7F, 0xFF, 0xFE, 0x0, 0x3, 0x0,
    0xF3, 0xF0, 0x1F, 0xFF, 0xFC, 0x0, 0x3, 0x83, 0xE3, 0xF0, 0xF, 0xFF, 0xF8, 0x0, 0x0, 0xCF,
    0xCF, 0xF0, 0xF, 0xFF, 0xF8, 0x0, 0x0, 0x5F, 0x9F, 0xF0, 0xF, 0xFF, 0xF8, 0x0, 0x0, 0xCF,
    0x3F, 0xF0, 0xF, 0xFF, 0xF8, 0x0, 0x3, 0xE6, 0x7F, 0xF0, 0x1F, 0xFF, 0xFC, 0x0, 0xF, 0xE6,
    0x7F, 0xF0, 0x7F, 0xFF, 0x1E, 0x0, 0x3C, 0xE0, 0x73, 0xF0, 0x7F, 0xFE, 0xF, 0xC0, 0xFC,
    0xE0, 0x73, 0xF0, 0x7F, 0xFE, 0xF, 0xE3, 0xFC, 0x0, 0x3, 0xF0, 0x7F, 0xFE, 0xF, 0xEF, 0xFC,
    0x0, 0x3, 0xF0, 0x7F, 0xFE, 0xF, 0xFF, 0xFC, 0x19, 0x83, 0xF0, 0x7F, 0xFF, 0x1F, 0xFF,
    0xFC, 0x19, 0x83, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFC, 0xC0, 0x33, 0xF0, 0xF, 0xFF, 0xFF,
    0xFF, 0xFC, 0xC0, 0x33, 0xF0, 0xF, 0xFF, 0xF7, 0xFF, 0xFC, 0xFF, 0xF3, 0xF0, 0xF, 0xFF,
    0xC7, 0xFF, 0xFC, 0xFF, 0xF3, 0xF0, 0xF, 0xFF, 0x7, 0xFF, 0xFC, 0xFF, 0xF3, 0xF0, 0x1F,
    0xFC, 0x3, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x7F, 0xF0, 0x1, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
    0x7F, 0xC0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x7F, 0x0, 0x0, 0x7F, 0xFF, 0xFF, 0xFF,
    0xF0, 0x2, 0x0, 0x0, 0x3F, 0xFF, 0xFF, 0xFF, 0xF0, 0x4, 0x0, 0x0, 0x1, 0xC7, 0xFC, 0x7F,
    0xF0, 0xC, 0x0, 0x0, 0x1, 0xFF, 0x1F, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xF0
};

int dshmupFmBitmap[22] = {
    16, 10, 0x80, 0x40, 0x8C, 0x40, 0xCC, 0xC0, 0xFF, 0xC0, 0xF3, 0xC0, 0xFF, 0xC0, 0xFF, 0xC0,
    0xFF, 0xC0, 0x7F, 0x80, 0x4C, 0x80
};

int dshmupRapideBitmap[22] = {
    16, 10, 0xC, 0x0, 0xC, 0x0, 0x8C, 0x40, 0x92, 0x40, 0x8C, 0x40, 0xFF, 0xC0, 0xFF, 0xC0,
    0x8C, 0x40, 0x12, 0x0, 0x21, 0x0
};

int dshmupSmallBitmap[11] = { 8, 9, 0x10, 0x10, 0x10, 0x28, 0xBA, 0xFE, 0x10, 0x10, 0x28 };

int dshmupHfightBitmap[6] = { 8, 4, 0x90, 0x90, 0xF0, 0x90 };
int dshmupRaiderBitmap[8] = { 8, 6, 0xB4, 0xFC, 0x84, 0xFC, 0x48, 0x30 };
int dshmupTfiBitmap[9] = { 8, 7, 0x81, 0x99, 0xBD, 0xE7, 0xE7, 0xBD, 0x99 };
int dshmupBugsBitmap[10] = { 8, 8, 0x82, 0x44, 0x28, 0xBA, 0xFE, 0xD6, 0xBA, 0x82 };
int dshmupEtoileBitmap[20] = {
    16, 9, 0x52, 0x80, 0x21, 0x0, 0x92, 0x40, 0x4C, 0x80, 0x3F, 0x0, 0x4C, 0x80, 0x92, 0x40,
    0x21, 0x0, 0x52, 0x80
};

// Real ship-select description text - upstream's own verbatim (typo-
// included) French text, manually wrapped into short lines (this shim has
// no automatic textWrap - see this file's own header comment).
char dshmupTxtFm[96] = "Vaisseau lour et\nlent capable de\ndetruire tous les\nennemis";
char dshmupTxtRapide[96] = "Vaisseau polivalent.\nUn bon copromie\nentre vitesse et\npuissance de feux";
char dshmupTxtSmall[96] = "Vaisseau petit et\nmaniable. Peux\nesquiver facilement\nles tires ennemie";

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

// Real upstream displayInt(long l, byte Tx, byte Ty, byte fig): prints
// `l` zero-padded to at least `fig` digits (never truncated if `l` itself
// already has more digits than `fig`) at (Tx,Ty) in whatever font is
// currently selected.
void dshmupPrintPadded( int value, int x, int y, int digits )
{
    int v = value;
    if( v < 0 ) v = 0;

    int count = 1;
    int t = v;
    while( t >= 10 )
    {
        t = t / 10;
        count++;
    }

    int total = count;
    if( digits > total ) total = digits;
    if( total > 11 ) total = 11; // real Score/Life never remotely approach this

    char buf[12];
    int vv = v;
    int i;
    for( i = total - 1; i >= 0; i-- )
    {
        buf[i] = 48 + ( vv % 10 ); // '0' == 48
        vv = vv / 10;
    }
    buf[total] = 0;

    gbCursorX = x;
    gbCursorY = y;
    gbPrintString( buf );
}

int* dshmupGetSpriteEnnemi( int num )
{
    if( num == 0 ) return dshmupHfightBitmap;
    if( num == 1 ) return dshmupRaiderBitmap;
    if( num == 2 ) return dshmupTfiBitmap;
    if( num == 3 ) return dshmupBugsBitmap;
    if( num == 4 ) return dshmupEtoileBitmap;
    return dshmupHfightBitmap; // unreachable - see header comment
}

int* dshmupGetSpritePlayer( int num )
{
    if( num == 0 ) return dshmupFmBitmap;
    if( num == 1 ) return dshmupRapideBitmap;
    if( num == 2 ) return dshmupSmallBitmap;
    return dshmupFmBitmap; // unreachable - see header comment
}

char* dshmupGetResumePlayer( int num )
{
    if( num == 1 ) return dshmupTxtRapide;
    if( num == 2 ) return dshmupTxtSmall;
    return dshmupTxtFm;
}

// -----------------------------------------------------------------------------
// Bullets / explosions (BulletHelper-equivalent free functions)
// -----------------------------------------------------------------------------

void dshmupAddBullet( int enemyIdx )
{
    int posB = 0;
    while( dshmupEnemyBullets[posB].IsAlive && posB < DSHMUP_NBMAX_ENNEMI_BULLET )
      posB++;

    if( posB < DSHMUP_NBMAX_ENNEMI_BULLET )
    {
        dshmupEnemyBullets[posB].Dmg = 1;
        dshmupEnemyBullets[posB].IsAlive = true;
        dshmupEnemyBullets[posB].PosX = dshmupEnemies[enemyIdx].PosX;
        dshmupEnemyBullets[posB].PosY = dshmupEnemies[enemyIdx].PosY;
    }
}

void dshmupAddExplosion( int posX, int posY )
{
    int posB = 0;
    while( dshmupExplosions[posB].IsAlive && posB < DSHMUP_NBMAX_EXPLOSION )
      posB++;

    if( posB < DSHMUP_NBMAX_EXPLOSION )
    {
        dshmupExplosions[posB].Temps = 0;
        dshmupExplosions[posB].TempsMax = 10;
        dshmupExplosions[posB].IsAlive = true;
        dshmupExplosions[posB].PosX = posX;
        dshmupExplosions[posB].PosY = posY;
    }
}

void dshmupPlayerFire()
{
    int posB = 0;
    while( dshmupPlayerBullets[posB].IsAlive && posB < DSHMUP_NBMAX_PLAYER_BULLET )
      posB++;

    if( posB < DSHMUP_NBMAX_PLAYER_BULLET )
    {
        dshmupPlayerBullets[posB].Dmg = dshmupPlayerPuissanceTire;
        dshmupPlayerBullets[posB].IsAlive = true;
        dshmupPlayerBullets[posB].PosY = dshmupPlayerPosY;

        if( dshmupPlayerSkin == 0 )
        {
            dshmupPlayerBullets[posB].PosX = dshmupPlayerPosX;

            // real upstream's own dual-bullet skin-0 spread: re-scans for
            // a second free slot starting from the same posB index
            // (already alive after the assignment above, so the scan
            // immediately advances past it)
            while( dshmupPlayerBullets[posB].IsAlive && posB < DSHMUP_NBMAX_PLAYER_BULLET )
              posB++;

            if( posB < DSHMUP_NBMAX_PLAYER_BULLET )
            {
                dshmupPlayerBullets[posB].Dmg = dshmupPlayerPuissanceTire;
                dshmupPlayerBullets[posB].IsAlive = true;
                dshmupPlayerBullets[posB].PosX = dshmupPlayerPosX + 10;
                dshmupPlayerBullets[posB].PosY = dshmupPlayerPosY;
            }
        }
        else if( dshmupPlayerSkin == 1 )
        {
            dshmupPlayerBullets[posB].PosX = dshmupPlayerPosX + 5;
        }
        else if( dshmupPlayerSkin == 2 )
        {
            dshmupPlayerBullets[posB].PosX = dshmupPlayerPosX + 3;
        }
    }
}

// -----------------------------------------------------------------------------
// Enemy collision / AI (StarShipHelper.ino)
// -----------------------------------------------------------------------------

bool dshmupTestColision( int bx, int by, int idx )
{
    int w = 0;
    int h = 0;
    int skin = dshmupEnemies[idx].Skin;

    if( skin == 0 )
    {
        w = 4; h = 4;
    }
    else if( skin == 1 )
    {
        w = 6; h = 6;
    }
    else if( skin == 2 || skin == 3 )
    {
        // real upstream's own case 2 falls through into case 3 - both
        // assign the identical w=8,h=7, so this combined branch is
        // functionally identical
        w = 8; h = 7;
    }
    else if( skin == 4 )
    {
        w = 10; h = 9;
    }

    return gbCollidePointRect( bx, by, dshmupEnemies[idx].PosX, dshmupEnemies[idx].PosY, w, h );
}

bool dshmupTestColisionPlayer( int bx, int by )
{
    int w = 0;
    int h = 0;

    if( dshmupPlayerSkin == 0 || dshmupPlayerSkin == 1 )
    {
        w = 10; h = 10;
    }
    else if( dshmupPlayerSkin == 2 )
    {
        w = 7; h = 9;
    }

    return gbCollidePointRect( bx, by, dshmupPlayerPosX, dshmupPlayerPosY, w, h );
}

void dshmupUpdateBugsStarShip( int idx )
{
    if( dshmupEnemies[idx].PosY > 0 )
    {
        dshmupEnemies[idx].NbFrameLife++;
    }
    else if( dshmupEnemies[idx].PosY < 0 && dshmupEnemies[idx].NbFrameLife == 0 )
    {
        dshmupEnemies[idx].VY = DSHMUP_VITESSE_SHIP;
        dshmupEnemies[idx].VX = DSHMUP_VITESSE_SHIP;
    }

    if( arand( 100 ) == 0 && dshmupEnemies[idx].NbFrameLife > 0 )
      dshmupAddBullet( idx );

    if( dshmupEnemies[idx].NbFrameLife > dshmupEnemies[idx].FrameLife )
    {
        dshmupEnemies[idx].VY = DSHMUP_VITESSE_SHIP;
        if( dshmupEnemies[idx].PosX > 44 )
          dshmupEnemies[idx].VX = DSHMUP_VITESSE_SHIP;
        else
          dshmupEnemies[idx].VX = -DSHMUP_VITESSE_SHIP;
    }
    else
    {
        if( dshmupEnemies[idx].NbFrameLife > 5 && dshmupEnemies[idx].NbFrameLife % 5 == 0 )
          dshmupEnemies[idx].VY = -dshmupEnemies[idx].VY;
        if( dshmupEnemies[idx].PosX < 0 || dshmupEnemies[idx].PosX > LCDWIDTH - 16 )
          dshmupEnemies[idx].VX = -dshmupEnemies[idx].VX;
    }
}

void dshmupUpdateTFightStarShip( int idx )
{
    if( dshmupEnemies[idx].PosY > 5 )
    {
        dshmupEnemies[idx].NbFrameLife++;
        dshmupEnemies[idx].VY = 0;
    }
    else if( dshmupEnemies[idx].PosY < 0 && dshmupEnemies[idx].NbFrameLife == 0 )
    {
        dshmupEnemies[idx].VY = DSHMUP_VITESSE_SHIP;
    }

    if( arand( 50 ) == 0 )
      dshmupAddBullet( idx );

    if( dshmupEnemies[idx].NbFrameLife > dshmupEnemies[idx].FrameLife )
    {
        dshmupEnemies[idx].VY = DSHMUP_VITESSE_SHIP;
        if( dshmupEnemies[idx].PosX > 44 )
          dshmupEnemies[idx].VX = DSHMUP_VITESSE_SHIP;
        else
          dshmupEnemies[idx].VX = -DSHMUP_VITESSE_SHIP;
    }
    else
    {
        if( dshmupEnemies[idx].NbFrameLife > 0 && dshmupEnemies[idx].NbFrameLife % 20 == 0 )
        {
            if( dshmupEnemies[idx].VX == 0 )
              dshmupEnemies[idx].VX = DSHMUP_VITESSE_SHIP;
            dshmupEnemies[idx].VX = -dshmupEnemies[idx].VX;
        }
    }
}

// See this file's own header comment: real upstream's own `case 2:` has
// no `break;` and falls through into `case 3:`'s body too - preserved
// exactly (skin 2 gets BOTH update functions applied every tick).
void dshmupUpdatePosVaisseauEnnemie( int idx )
{
    int skin = dshmupEnemies[idx].Skin;

    if( skin == 2 )
    {
        dshmupUpdateTFightStarShip( idx );
        dshmupUpdateBugsStarShip( idx );
    }
    else
    {
        dshmupUpdateBugsStarShip( idx ); // skins 0, 1, 3, 4
    }
}

void dshmupDrawStarShip( int idx )
{
    gbDrawBitmap( dshmupEnemies[idx].PosX, dshmupEnemies[idx].PosY, dshmupGetSpriteEnnemi( dshmupEnemies[idx].Skin ) );
}

// See this file's own header comment: real upstream never copies VX/VY
// during this compaction - preserved exactly.
void dshmupTasseTabEnnemie( int index )
{
    int i;
    for( i = index; i < dshmupNbEnemisAlive - 1; i++ )
    {
        dshmupEnemies[i].TypeDark = dshmupEnemies[i + 1].TypeDark;
        dshmupEnemies[i].PosX = dshmupEnemies[i + 1].PosX;
        dshmupEnemies[i].PosY = dshmupEnemies[i + 1].PosY;
        dshmupEnemies[i].Life = dshmupEnemies[i + 1].Life;
        dshmupEnemies[i].Skin = dshmupEnemies[i + 1].Skin;
        dshmupEnemies[i].FrameLife = dshmupEnemies[i + 1].FrameLife;
        dshmupEnemies[i].NbFrameLife = dshmupEnemies[i + 1].NbFrameLife;
    }
}

// -----------------------------------------------------------------------------
// Enemy spawn patterns (DefinitionPattern.ino)
// -----------------------------------------------------------------------------

void dshmupPopPattern1()
{
    if( dshmupNbEnemisAlive + 4 <= DSHMUP_NBMAX_ENNEMI )
    {
        int b = dshmupNbEnemisAlive;
        dshmupEnemies[b].TypeDark = 0; dshmupEnemies[b].PosX = 10; dshmupEnemies[b].PosY = -10;
        dshmupEnemies[b].Life = 1; dshmupEnemies[b].Skin = 0; dshmupEnemies[b].FrameLife = 120; dshmupEnemies[b].NbFrameLife = 0;

        dshmupEnemies[b + 1].TypeDark = 1; dshmupEnemies[b + 1].PosX = 30; dshmupEnemies[b + 1].PosY = -6;
        dshmupEnemies[b + 1].Life = 1; dshmupEnemies[b + 1].Skin = 3; dshmupEnemies[b + 1].FrameLife = 120; dshmupEnemies[b + 1].NbFrameLife = 0;

        dshmupEnemies[b + 2].TypeDark = 1; dshmupEnemies[b + 2].PosX = 20; dshmupEnemies[b + 2].PosY = -6;
        dshmupEnemies[b + 2].Life = 1; dshmupEnemies[b + 2].Skin = 3; dshmupEnemies[b + 2].FrameLife = 120; dshmupEnemies[b + 2].NbFrameLife = 0;

        dshmupEnemies[b + 3].TypeDark = 0; dshmupEnemies[b + 3].PosX = 40; dshmupEnemies[b + 3].PosY = -10;
        dshmupEnemies[b + 3].Life = 1; dshmupEnemies[b + 3].Skin = 0; dshmupEnemies[b + 3].FrameLife = 120; dshmupEnemies[b + 3].NbFrameLife = 0;

        dshmupNbEnemisAlive += 4;
    }
}

void dshmupPopPattern2()
{
    if( dshmupNbEnemisAlive + 6 <= DSHMUP_NBMAX_ENNEMI )
    {
        int b = dshmupNbEnemisAlive;
        dshmupEnemies[b].TypeDark = 1; dshmupEnemies[b].PosX = 0; dshmupEnemies[b].PosY = -10;
        dshmupEnemies[b].Life = 5; dshmupEnemies[b].Skin = 3; dshmupEnemies[b].FrameLife = 120; dshmupEnemies[b].NbFrameLife = 0;

        dshmupEnemies[b + 1].TypeDark = 1; dshmupEnemies[b + 1].PosX = 10; dshmupEnemies[b + 1].PosY = -6;
        dshmupEnemies[b + 1].Life = 5; dshmupEnemies[b + 1].Skin = 3; dshmupEnemies[b + 1].FrameLife = 120; dshmupEnemies[b + 1].NbFrameLife = 0;

        dshmupEnemies[b + 2].TypeDark = 1; dshmupEnemies[b + 2].PosX = 20; dshmupEnemies[b + 2].PosY = -10;
        dshmupEnemies[b + 2].Life = 5; dshmupEnemies[b + 2].Skin = 3; dshmupEnemies[b + 2].FrameLife = 120; dshmupEnemies[b + 2].NbFrameLife = 0;

        dshmupEnemies[b + 3].TypeDark = 0; dshmupEnemies[b + 3].PosX = 0; dshmupEnemies[b + 3].PosY = -20;
        dshmupEnemies[b + 3].Life = 3; dshmupEnemies[b + 3].Skin = 1; dshmupEnemies[b + 3].FrameLife = 120; dshmupEnemies[b + 3].NbFrameLife = 0;

        dshmupEnemies[b + 4].TypeDark = 0; dshmupEnemies[b + 4].PosX = 10; dshmupEnemies[b + 4].PosY = -16;
        dshmupEnemies[b + 4].Life = 3; dshmupEnemies[b + 4].Skin = 1; dshmupEnemies[b + 4].FrameLife = 120; dshmupEnemies[b + 4].NbFrameLife = 0;

        dshmupEnemies[b + 5].TypeDark = 0; dshmupEnemies[b + 5].PosX = 20; dshmupEnemies[b + 5].PosY = -20;
        dshmupEnemies[b + 5].Life = 3; dshmupEnemies[b + 5].Skin = 1; dshmupEnemies[b + 5].FrameLife = 120; dshmupEnemies[b + 5].NbFrameLife = 0;

        dshmupNbEnemisAlive += 6;
    }
}

void dshmupPopPatternBoss1()
{
    if( dshmupNbEnemisAlive + 3 <= DSHMUP_NBMAX_ENNEMI )
    {
        int b = dshmupNbEnemisAlive;
        dshmupEnemies[b].TypeDark = 0; dshmupEnemies[b].PosX = 10; dshmupEnemies[b].PosY = -6;
        dshmupEnemies[b].Life = 3; dshmupEnemies[b].Skin = 2; dshmupEnemies[b].FrameLife = 220; dshmupEnemies[b].NbFrameLife = 0;

        dshmupEnemies[b + 1].TypeDark = 0; dshmupEnemies[b + 1].PosX = 40; dshmupEnemies[b + 1].PosY = -6;
        dshmupEnemies[b + 1].Life = 3; dshmupEnemies[b + 1].Skin = 2; dshmupEnemies[b + 1].FrameLife = 220; dshmupEnemies[b + 1].NbFrameLife = 0;

        dshmupEnemies[b + 2].TypeDark = 1; dshmupEnemies[b + 2].PosX = 25; dshmupEnemies[b + 2].PosY = -10;
        dshmupEnemies[b + 2].Life = 30; dshmupEnemies[b + 2].Skin = 4; dshmupEnemies[b + 2].FrameLife = 520; dshmupEnemies[b + 2].NbFrameLife = 0;

        dshmupNbEnemisAlive += 3;
    }
}

void dshmupPopPattern3()
{
    int i;
    for( i = 0; i < 16; i++ )
    {
        // real upstream logic-inversion bug: this condition is virtually
        // always true (nbEnemisAlive is basically never > NBMAX_ENNEMI),
        // so the loop breaks on its very first iteration almost every
        // time - see this file's own header comment.
        if( dshmupNbEnemisAlive <= DSHMUP_NBMAX_ENNEMI )
          break;

        dshmupEnemies[dshmupNbEnemisAlive].TypeDark = i % 2;
        dshmupEnemies[dshmupNbEnemisAlive].PosX = 6 * i + 6;
        dshmupEnemies[dshmupNbEnemisAlive].PosY = -15 + arand( 10 );
        dshmupEnemies[dshmupNbEnemisAlive].Life = 1;
        dshmupEnemies[dshmupNbEnemisAlive].Skin = 0;
        dshmupEnemies[dshmupNbEnemisAlive].FrameLife = 90;
        dshmupEnemies[dshmupNbEnemisAlive].NbFrameLife = 0;
        dshmupNbEnemisAlive++;
    }
}

void dshmupItsTimeToPop()
{
    int r = arand( 4 );
    if( r == 0 ) dshmupPopPattern1();
    else if( r == 1 ) dshmupPopPattern2();
    else if( r == 2 ) dshmupPopPatternBoss1();
    else dshmupPopPattern3();
}

// -----------------------------------------------------------------------------
// Player (PlayerHelper.ino)
// -----------------------------------------------------------------------------

void dshmupInitPlayerByType()
{
    dshmupPlayerPosY = 36;
    dshmupPlayerPosX = 15;
    dshmupPlayerSpeedX = 0;

    if( dshmupPlayerSkin == 0 )
    {
        dshmupPlayerLife = 15;
        dshmupPlayerMaxSpeedX = 2;
        dshmupPlayerPuissanceTire = 2;
    }
    else if( dshmupPlayerSkin == 1 )
    {
        dshmupPlayerLife = 10;
        dshmupPlayerMaxSpeedX = 3;
        dshmupPlayerPuissanceTire = 2;
    }
    else if( dshmupPlayerSkin == 2 )
    {
        dshmupPlayerLife = 8;
        dshmupPlayerMaxSpeedX = 4;
        dshmupPlayerPuissanceTire = 1;
    }
}

// -----------------------------------------------------------------------------
// Top-level world update/draw (DarkShmup.ino's own updateWorld()/drawWorld())
// -----------------------------------------------------------------------------

void dshmupUpdateWorld()
{
    if( dshmupNbEnemisAlive <= DSHMUP_NBMIN_ENNEMI_SCREEN )
      dshmupItsTimeToPop();

    int i = 0;
    while( i < dshmupNbEnemisAlive )
    {
        dshmupUpdatePosVaisseauEnnemie( i );
        dshmupEnemies[i].PosX += dshmupEnemies[i].VX;
        dshmupEnemies[i].PosY += dshmupEnemies[i].VY;

        if( dshmupEnemies[i].PosY > 60 || dshmupEnemies[i].Life <= 0 )
        {
            dshmupEnemies[i].Life = 0;
            dshmupAddExplosion( dshmupEnemies[i].PosX, dshmupEnemies[i].PosY );
            dshmupTasseTabEnnemie( i );
            dshmupNbEnemisAlive--;
        }
        else
        {
            i++;
        }
    }

    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        if( dshmupPlayerSpeedX > -dshmupPlayerMaxSpeedX )
          dshmupPlayerSpeedX--;
    }
    else if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        if( dshmupPlayerSpeedX < dshmupPlayerMaxSpeedX )
          dshmupPlayerSpeedX++;
    }
    else
    {
        dshmupPlayerSpeedX = 0;
    }

    if( gbPressed( BTN_A ) )
      dshmupPlayerFire();

    dshmupPlayerPosX += dshmupPlayerSpeedX;
    if( dshmupPlayerPosX >= LCDWIDTH - 16 || dshmupPlayerPosX <= 0 )
    {
        dshmupPlayerPosX -= dshmupPlayerSpeedX;
        dshmupPlayerSpeedX = 0;
    }
}

void dshmupUpdateAndDrawHud()
{
    gbSetFont( gbFont3x3 );

    if( dshmupOldScore < dshmupScore ) dshmupOldScore = ( dshmupOldScore + 7 ) & 65535;
    if( dshmupOldScore > dshmupScore ) dshmupOldScore = dshmupScore;

    dshmupPrintPadded( dshmupOldScore, 10, 45, 7 );
    dshmupPrintPadded( dshmupPlayerLife, 50, 45, 2 );

    gbSetFont( gbFont3x5 );
}

void dshmupUpdateAndDrawBullet()
{
    gbSetColor( GB_INVERT );

    int i;
    for( i = 0; i < DSHMUP_NBMAX_PLAYER_BULLET; i++ )
    {
        if( dshmupPlayerBullets[i].IsAlive )
        {
            dshmupPlayerBullets[i].PosY -= 2;
            if( dshmupPlayerBullets[i].PosY < 0 )
            {
                dshmupPlayerBullets[i].IsAlive = false;
            }
            else
            {
                int cptEnnemi;
                for( cptEnnemi = 0; cptEnnemi < dshmupNbEnemisAlive; cptEnnemi++ )
                {
                    if( dshmupEnemies[cptEnnemi].TypeDark == dshmupIsInDarkWorld )
                    {
                        if( dshmupTestColision( dshmupPlayerBullets[i].PosX, dshmupPlayerBullets[i].PosY, cptEnnemi ) )
                        {
                            dshmupEnemies[cptEnnemi].Life -= dshmupPlayerBullets[i].Dmg;
                            dshmupPlayerBullets[i].IsAlive = false;
                            dshmupScore = ( dshmupScore + 91 ) & 65535; // "pour faire un score a la con qui monte vite" - real upstream comment, verbatim intent preserved
                        }
                    }
                }
            }

            gbDrawPixel( dshmupPlayerBullets[i].PosX, dshmupPlayerBullets[i].PosY );
        }
    }
}

void dshmupUpdateAndDrawBulletEnnemies()
{
    gbSetColor( GB_INVERT );

    int i;
    for( i = 0; i < DSHMUP_NBMAX_ENNEMI_BULLET; i++ )
    {
        if( dshmupEnemyBullets[i].IsAlive )
        {
            dshmupEnemyBullets[i].PosY += 2;
            if( dshmupEnemyBullets[i].PosY > 50 )
            {
                dshmupEnemyBullets[i].IsAlive = false;
            }
            else if( dshmupTestColisionPlayer( dshmupEnemyBullets[i].PosX, dshmupEnemyBullets[i].PosY ) )
            {
                dshmupAddExplosion( dshmupEnemyBullets[i].PosX, dshmupEnemyBullets[i].PosY );
                // real StarShipPlayer::Life is uint8_t - see this file's
                // own header comment on why this masks with & 255
                dshmupPlayerLife = ( dshmupPlayerLife - dshmupEnemyBullets[i].Dmg ) & 255;
                dshmupEnemyBullets[i].IsAlive = false;
            }

            gbDrawPixel( dshmupEnemyBullets[i].PosX, dshmupEnemyBullets[i].PosY );
            gbDrawLine( dshmupEnemyBullets[i].PosX, dshmupEnemyBullets[i].PosY, dshmupEnemyBullets[i].PosX, dshmupEnemyBullets[i].PosY - 3 );
        }
    }
}

void dshmupDrawExplosion()
{
    int i;
    for( i = 0; i < DSHMUP_NBMAX_EXPLOSION; i++ )
    {
        if( dshmupExplosions[i].IsAlive )
        {
            if( dshmupExplosions[i].Temps < dshmupExplosions[i].TempsMax )
            {
                int x;
                for( x = 0; x < 10; x++ )
                {
                    int posx = dshmupExplosions[i].PosX - dshmupExplosions[i].Temps + arand( 2 * dshmupExplosions[i].Temps );
                    int posy = dshmupExplosions[i].PosY - dshmupExplosions[i].Temps + arand( 2 * dshmupExplosions[i].Temps );
                    gbDrawPixel( posx, posy );
                }
                dshmupExplosions[i].Temps++;
            }
            else
            {
                dshmupExplosions[i].IsAlive = false;
            }
        }
    }
}

void dshmupDrawWorld()
{
    if( dshmupIsInDarkWorld )
    {
        gbFillScreen( GB_BLACK ); // real gbFillScreen()'s own color arg is a confirmed no-op - always fills BLACK, matching real hardware
        gbSetColor( GB_WHITE );
    }
    else
    {
        gbSetColor( GB_BLACK );
    }

    int i;
    for( i = 0; i < dshmupNbEnemisAlive; i++ )
    {
        if( dshmupEnemies[i].TypeDark == dshmupIsInDarkWorld && dshmupEnemies[i].Life > 0 )
          dshmupDrawStarShip( i );
    }

    if( gbPressed( BTN_B ) )
    {
        if( dshmupIsInDarkWorld ) dshmupIsInDarkWorld = 0;
        else dshmupIsInDarkWorld = 1;
    }

    gbDrawBitmap( dshmupPlayerPosX, dshmupPlayerPosY, dshmupGetSpritePlayer( dshmupPlayerSkin ) );

    dshmupUpdateAndDrawHud();
    dshmupUpdateAndDrawBulletEnnemies();
    dshmupUpdateAndDrawBullet();
    dshmupDrawExplosion();
}

// -----------------------------------------------------------------------------
// State machine (real upstream's own blocking gb.titleScreen()/ChoixVaisseau()
// while(true) loops, flattened into explicit states - see gamePong.c's own
// established treatment of this exact upstream pattern)
// -----------------------------------------------------------------------------

void dshmupUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 8, dshmupTitleScreenBitmap );
    gbCursorX = 8;
    gbCursorY = 41;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      dshmupState = DSHMUP_STATE_SHIPSELECT;
}

void dshmupUpdateShipSelect()
{
    if( gbPressed( BTN_RIGHT ) )
    {
        dshmupPlayerSkin++;
        if( dshmupPlayerSkin >= DSHMUP_NB_SKIN_PLAYER )
          dshmupPlayerSkin = 0;
        dshmupShipSelectOffset = DSHMUP_OFFSET_DEF;
    }
    if( gbPressed( BTN_LEFT ) )
    {
        if( dshmupPlayerSkin == 0 )
          dshmupPlayerSkin = DSHMUP_NB_SKIN_PLAYER - 1;
        else
          dshmupPlayerSkin--;
        dshmupShipSelectOffset = DSHMUP_OFFSET_DEF;
    }
    if( dshmupShipSelectOffset > 0 )
      dshmupShipSelectOffset--;

    gbSetColor( GB_BLACK );
    // real upstream never resets cursorX/cursorY before this print call -
    // see this file's own header comment on why this port deliberately
    // does, rather than risk an unverifiable scroll/wrap artifact
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( dshmupGetResumePlayer( dshmupPlayerSkin ) );
    gbDrawBitmap( dshmupPlayerPosX - dshmupShipSelectOffset, dshmupPlayerPosY, dshmupGetSpritePlayer( dshmupPlayerSkin ) );

    if( gbPressed( BTN_A ) )
    {
        dshmupInitPlayerByType();
        dshmupState = DSHMUP_STATE_PLAY;
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameDarkShmup_init()
{
    gbBegin();

    dshmupState = DSHMUP_STATE_TITLE;
    dshmupIsInDarkWorld = 0;
    dshmupNbEnemisAlive = 0;
    dshmupScore = 0;
    dshmupOldScore = 0;
    dshmupPlayerLife = 10;
    dshmupPlayerSkin = 0;
    dshmupPlayerPosY = 36;
    dshmupPlayerPosX = 15;
    dshmupPlayerSpeedX = 0;
    dshmupShipSelectOffset = DSHMUP_OFFSET_DEF;

    // Real upstream's own setup() only ever runs once per real power-on,
    // so its own bullet/enemy/explosion pools start genuinely fresh every
    // time. This cartridge's own menu allows re-entering the same game
    // multiple times in one session, unlike a real single boot - explicitly
    // clear every pool here so a relaunch is never contaminated by a
    // previous playthrough's own leftover state.
    int i;
    for( i = 0; i < DSHMUP_NBMAX_ENNEMI; i++ )
      dshmupEnemies[i].Life = 0;
    for( i = 0; i < DSHMUP_NBMAX_ENNEMI_BULLET; i++ )
      dshmupEnemyBullets[i].IsAlive = false;
    for( i = 0; i < DSHMUP_NBMAX_PLAYER_BULLET; i++ )
      dshmupPlayerBullets[i].IsAlive = false;
    for( i = 0; i < DSHMUP_NBMAX_EXPLOSION; i++ )
      dshmupExplosions[i].IsAlive = false;
}

void gameDarkShmup_update()
{
    if( !gbUpdate() ) return;

    if( dshmupState == DSHMUP_STATE_TITLE )
    {
        dshmupUpdateTitle();
    }
    else if( dshmupState == DSHMUP_STATE_SHIPSELECT )
    {
        dshmupUpdateShipSelect();
    }
    else
    {
        dshmupUpdateWorld();
        dshmupDrawWorld();
    }

    gbRenderFrame();
}
