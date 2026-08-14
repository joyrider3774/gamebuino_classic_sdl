#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// AsteroidRipper (ripper121, license: none specified - recovered via
// direct download, no live GitHub repo; the author's current
// gamebuino.com profile has no matching Classic-era upload for this
// title). A real Asteroids clone: rotate the ship in 8 fixed headings,
// thrust forward/backward, shoot up to 4 bullets at once, and split big
// rocks into medium ones and medium rocks into small ones as they're
// destroyed, clearing 3 big + 9 medium + 27 small rocks (39 total) to
// advance a level (each level raising every rock tier's own base speed by
// 0.1). 3 lives, any rock touching the ship costs one (also destroying/
// splitting that rock, exactly like a bullet hit) - lose the last life and
// it's game over. Named "AsteroidRipper" rather than plain "Asteroids"
// since this cartridge already ships an unrelated Yoda Zhang game of that
// genre under the `aster`-prefixed gameAsterocks.c - a different game
// entirely, not a naming collision to resolve, just two distinct real
// Asteroids clones sharing a cartridge, given each a distinct prefix
// (`astr`, not `aster`).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment - this
// dialect has no classes/methods). Every global/function/struct tag uses
// an `astr`/`Astr`-prefixed name (checked against every other
// `src/games/*.c` file first - this cartridge has no linker, so every game
// shares one flat global namespace). `byte` fields became plain `int`
// (this dialect's `int` is already a full 32-bit word - see
// gameAsterocks.c's own header comment for why this loses nothing).
// `random(min, max)` (Arduino's ranged form, upstream's own real
// initGame() big-asteroid spawn quadrants) became `astrRandRange(lo, hi)`,
// a thin `lo + arand(hi - lo)` wrapper matching Arduino's own real
// `min..max-1` semantics exactly via this dialect's own established
// `arand(n)` RNG helper (avrCompat.h); plain `random(N)` sites became
// `arand(N)` directly. `gb.pickRandomSeed()`-equivalent seeding
// (`randomSeed(gb.battery.voltage + gb.backlight.ambientLight)`) was
// dropped outright - this shim's RNG isn't manually seedable the same way
// (matching this whole project's own established precedent for every
// other upstream `randomSeed()` call), and `gb.battery.show = false;` was
// dropped too, matching gamePong.c's own precedent (purely cosmetic on
// real hardware).
//
// ---- Bitmaps - restored as real gbDrawBitmap() calls, not placeholders ----
// Every real `static unsigned char PROGMEM NAME[]` array in the upstream
// `images` block was copied verbatim into a plain `int astrXxxBitmap[N]`
// array below (this dialect's own `int name[N]` array-declaration order),
// with every Arduino `B00000000`-style binary literal converted to hex
// (`0x..`, no binary-literal syntax in this dialect) via a small script
// parsing the real .ino source directly (not hand arithmetic) - every
// array's own declared width/height header was cross-checked against its
// real byte count (`ceil(width/8)*height`) and matches exactly for all 12
// bitmaps: `astrLogoBitmap` (the real 64x28 boot-splash logo passed to
// upstream's own `gb.begin(F(".by Ripper121"), Logo_black)`),
// `astrBigAsteroidBitmap`/`astrMediumAsteroidBitmap`/
// `astrSmallAsteroidBitmap` (8x8/6x6/4x4, one shared sprite per tier - the
// real source draws every rock of a given tier identically regardless of
// its own random velocity), and the 8 real 5x5 `astrShipXxxBitmap` frames
// (one per compass heading N/NE/E/SE/S/SW/W/NW) upstream's own
// `drawShip(rotation)` selects between via a `switch`. No bitmap here is
// preceded by a separate GRAY mask/fill layer anywhere in the real source
// (every `gb.display.drawBitmap(...)` call site in asteroid.ino draws
// straight onto the background with no `setColor(GRAY)`+mask-bitmap pair
// or fillRect first) - the mask-bleed bug class documented in
// gameFlappyBirdo.c's own header comment does not apply to this game.
// Upstream never calls `gb.display.setColor(...)` anywhere at all, so this
// port makes no `gbSetColor()` calls either - `gbBegin()` already resets
// `gbColor` to real hardware's own default (BLACK), matching upstream's
// implicit reliance on that same real default.
//
// ---- The non-blocking gb.begin(title, logo) boot splash -> ASTR_STATE_SPLASH ----
// Upstream's own `gb.begin(F(".by Ripper121"), Logo_black)` (setup()'s
// only call before `initGame()`) is real hardware's own built-in,
// non-blocking, hardware-timed boot branding display - not a genuine
// blocking `gb.titleScreen()`/`gb.menu()`-style button-wait call the way
// several other ports in this project convert into an explicit state
// (gamePong.c/gameBlockdude.c/gameAsterocks.c). Since this shim's own
// `gbBegin()` has no splash mechanism of its own and the real boot-splash's
// own internal timing/layout isn't available to read here, this port
// approximates it as one explicit `ASTR_STATE_SPLASH` state shown once at
// launch, drawing the real logo bitmap plus a "PRESS A" prompt, dismissed
// by a genuine fresh `gbPressed(BTN_A)` - the same "closest documented
// stand-in without inventing new blocking-wait behavior the real call
// doesn't have" reasoning already used for gameAsterocks.c's own
// `gb.titleScreen()` approximation, chosen here specifically so the boot
// logo's own real bitmap art (a genuine, game-specific asset, not generic
// Gamebuino chrome) still gets shown rather than silently dropped the way
// gameFiremen.c's own header comment documents for *generic* boot-splash
// branding. Never re-entered mid-game (upstream itself never re-shows it
// either - Button C here only ever toggles pause, unlike several other
// ported games' own Button-C "return to splash" gesture).
//
// ---- Real upstream bugs/quirks found while reading asteroid.ino - preserved exactly ----
// - `drawBullet(rotation, i)`'s own rotation->velocity table does NOT
//   match `drawShip(rotation)`'s own table for the same rotation index
//   (e.g. rotation 0: the ship moves south, `vy = +v`, while a bullet
//   fired at rotation 0 moves west, `vx = -v`) - a genuine inconsistency
//   between the ship's own heading and the direction its bullets actually
//   fly, real and load-bearing (every bullet in this game flies at a
//   fixed 45-degree offset from wherever the ship is actually pointing),
//   preserved exactly rather than "corrected" to match `drawShip()`'s own
//   table.
// - `drawBullet()` itself moves a bullet with its X/Y axes transposed
//   relative to every other moving entity in this game (ship/asteroids
//   all do the ordinary `x += vx; y += vy`) - real upstream code reads
//   `Bullet[i].y += Bullet[i].vx; Bullet[i].x += Bullet[i].vy;`, i.e. the
//   `vx` component is actually applied to `y` and vice versa. A genuine,
//   observable upstream bug (bullets trace a diagonally-mirrored path
//   relative to what their own `vx`/`vy` fields alone would suggest),
//   preserved exactly, not fixed.
// - The real 8-direction velocity table shared by all three asteroid
//   tiers' own init (`switch (random(0, 8))`) has only 7 truly distinct
//   headings: roll 0 and roll 7 both produce identical `(+v, +v)`
//   velocity. Preserved exactly (`astrSetAsteroidVelocity()` below mirrors
//   this - roll 7 is not rebalanced to a genuinely distinct 8th
//   direction).
// - Level-complete can override a same-tick game-over: `AsteroidExist`
//   (this port's own `astrAsteroidExist`) is only checked once, AFTER all
//   three asteroid-tier collision loops finish for the tick - so if the
//   hit that costs the player's last life is also the hit that destroys
//   the very last on-screen rock, upstream's own level-complete branch
//   still runs that same tick and unconditionally sets `GameOver = false`
//   again, overriding the death that just happened a few lines earlier in
//   the very same `gb.update()` call - the player sees "Next Level"
//   instead of "Game Over". Preserved exactly by running this port's own
//   three tier-update functions, then its own level-complete check, then
//   only finalizing the ASTR_STATE_GAMEOVER transition after both, in
//   that same real order (see `gameAsteroidRipper_update()`'s own dispatch
//   below).
// - `gb.display.setTextSize(0)` (the real HUD score/lives line's own only
//   text-size call in the whole source) has no verifiable real-hardware
//   meaning available to check here (the real `Display.cpp` this staged
//   copy of asteroid.ino shipped alongside isn't itself part of this
//   game's own recovered source, and no other Gamebuino Classic game
//   ported into this cartridge so far has ever called `setTextSize(0)`
//   either) - this shim's own `gbFontSize` only supports 1 (native) or 2
//   (doubled) in any case. Normalized to the default size (1), which
//   needed no code of its own at all: `gbBegin()` already leaves
//   `gbFontSize` at 1.
// - Pausing (Button C) used to freeze the ENTIRE screen, not just gameplay
//   logic: `gbUpdate()` clears the framebuffer every real engine tick
//   unconditionally (matching real hardware's own default
//   `persistence=false` behavior - see this project's own CLAUDE.md), and
//   real upstream draws literally nothing at all - no "PAUSED" banner, no
//   HUD - while its own `paused` flag is set (confirmed by reading
//   `loop()` directly: the entire score/ship/bullet/asteroid draw path
//   lives inside `if (!paused)`). FIXED HERE, NOT PRESERVED: a plain
//   centered "PAUSED" label is now drawn whenever `astrPaused` is true, so
//   pausing no longer looks indistinguishable from a frozen/crashed game.
// - Both the "Next Level" and "Game Over" screens were originally drawn
//   directly to the display followed by a hardcoded `delay(3000)` before
//   silently reinitializing - this dialect has no `delay()`-equivalent
//   blocking primitive at all. Converted into two explicit states,
//   `ASTR_STATE_NEXTLEVEL`/`ASTR_STATE_GAMEOVER`, each now waiting for a
//   genuine fresh Button A press before reinitializing and resuming,
//   matching gameSnakeClassic.c's own established precedent for this
//   exact upstream pattern (its own real Game Over screen had the same
//   `delay(3000)`-then-reinit shape).
//
// `gb.collideRectRect(...)` ported directly to this shim's own
// `gbCollideRectRect(...)` (an exact 1:1 primitive - no shim gap here).
// This game has no real `EEPROM.read()`/`write()` calls anywhere in its
// source (no high score is ever saved upstream), so no EEPROM shim usage
// was needed.
//
// Upstream keeps three parallel same-shaped arrays (`BigAsteroid[3]`,
// `MediumAsteroid[9]`, `SmallAsteroid[27]`) plus a `Bullet[4]` array and one
// `Ship` struct; this port keeps that same shape (`AstrAsteroid[N]` per
// tier, `AstrBullet astrBullets[4]`, one `AstrShip astrShip`) rather than
// merging the three asteroid tiers into one array with a size tag, staying
// a close structural mirror of the real source. The repeated "spawn 3
// smaller rocks at this position" logic (identical in both the
// ship-collision and bullet-collision branches, for both the
// big-splits-into-medium and medium-splits-into-small cases) was factored
// into two small shared helpers, `astrSpawnMediumFromBig()`/
// `astrSpawnSmallFromMedium()` - a real deduplication, not a behavior
// change (both call sites were already byte-for-byte identical in
// upstream). Likewise `astrSetAsteroidVelocity()` factors out the
// identical 8-direction `switch` upstream repeats verbatim for all three
// tiers' own init.

#define ASTR_MAX_BULLETS 4
#define ASTR_MAX_BIG 3
#define ASTR_MAX_MEDIUM 9 // ASTR_MAX_BIG * 3, matching upstream's own #define maxMediumAsteroid
#define ASTR_MAX_SMALL 27 // ASTR_MAX_MEDIUM * 3, matching upstream's own #define maxSmallAsteroid

#define ASTR_STATE_SPLASH 0
#define ASTR_STATE_PLAY 1
#define ASTR_STATE_NEXTLEVEL 2
#define ASTR_STATE_GAMEOVER 3

// ---- Bitmaps (real upstream art, B-literal bytes converted to hex - see header comment) ----

int astrLogoBitmap[226] =
{
    64, 28,
    0x10, 0x02, 0x00, 0x00, 0x08, 0x10, 0x00, 0x00,
    0x28, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x28, 0x77, 0x9C, 0x73, 0x88, 0xF0, 0x00, 0x00,
    0x44, 0x82, 0x22, 0x44, 0x49, 0x10, 0x00, 0x00,
    0x7C, 0x62, 0x3E, 0x44, 0x49, 0x10, 0x00, 0x00,
    0x44, 0x12, 0x20, 0x44, 0x49, 0x10, 0x00, 0x00,
    0x82, 0xE1, 0x9E, 0x43, 0x88, 0xF0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x70, 0x00,
    0x01, 0xF0, 0x03, 0x00, 0x00, 0x01, 0x88, 0x00,
    0x02, 0x10, 0x05, 0x00, 0x00, 0x01, 0x08, 0x00,
    0x04, 0x08, 0x0E, 0x80, 0x30, 0x01, 0x08, 0x00,
    0x04, 0x08, 0x01, 0x80, 0x50, 0x00, 0x88, 0x00,
    0x04, 0x08, 0x00, 0x00, 0x48, 0x00, 0xF0, 0x00,
    0x04, 0x10, 0x00, 0x20, 0x38, 0x00, 0x00, 0x00,
    0x02, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0xE0, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x02, 0x00, 0x07, 0x00, 0x00,
    0x00, 0x00, 0x0A, 0x03, 0x00, 0x18, 0x80, 0x00,
    0x00, 0x00, 0x09, 0x05, 0x00, 0x10, 0x80, 0x00,
    0x00, 0x00, 0x07, 0x04, 0x80, 0x10, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x80, 0x08, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

int astrBigAsteroidBitmap[10] =
{
    8, 8,
    0x3C,
    0x43,
    0x81,
    0x81,
    0x81,
    0x81,
    0xC6,
    0x38,
};

int astrMediumAsteroidBitmap[8] =
{
    6, 6,
    0x38,
    0xC4,
    0x84,
    0x84,
    0x44,
    0x78,
};

int astrSmallAsteroidBitmap[6] =
{
    4, 4,
    0x60,
    0x90,
    0xD0,
    0x30,
};

int astrShipNBitmap[7] =
{
    5, 5,
    0x20,
    0x20,
    0x50,
    0x50,
    0xF8,
};

int astrShipNEBitmap[7] =
{
    5, 5,
    0x18,
    0xE8,
    0x50,
    0x30,
    0x10,
};

int astrShipEBitmap[7] =
{
    5, 5,
    0x80,
    0xE0,
    0x98,
    0xE0,
    0x80,
};

int astrShipSEBitmap[7] =
{
    5, 5,
    0x10,
    0x30,
    0x50,
    0xE8,
    0x18,
};

int astrShipSBitmap[7] =
{
    5, 5,
    0xF8,
    0x50,
    0x50,
    0x20,
    0x20,
};

int astrShipSWBitmap[7] =
{
    5, 5,
    0x40,
    0x60,
    0x50,
    0xB8,
    0xC0,
};

int astrShipWBitmap[7] =
{
    5, 5,
    0x08,
    0x38,
    0xC8,
    0x38,
    0x08,
};

int astrShipNWBitmap[7] =
{
    5, 5,
    0xC0,
    0xB8,
    0x50,
    0x60,
    0x40,
};

// ---- Structs ----

typedef struct{
    float x;
    float y;
    int w;
    int h;
    int rotation;
    float v;
    float vx;
    float vy;
    int score;
} AstrShip;

typedef struct{
    int x;
    int y;
    int rotation;
    int v;
    int vx;
    int vy;
    bool exist;
} AstrBullet;

typedef struct{
    float x;
    float y;
    int w;
    int h;
    float v;
    float vx;
    float vy;
    bool exist;
} AstrAsteroid;

// ---- Globals ----

int astrState;
bool astrPaused;
bool astrGameOver;
float astrLevelSpeed;
int astrLevel;
int astrLives;
int astrAsteroidExist;

AstrShip astrShip;
AstrBullet astrBullets[4];
AstrAsteroid astrBigAsteroids[3];
AstrAsteroid astrMediumAsteroids[9];
AstrAsteroid astrSmallAsteroids[27];

// ---- Small helpers ----

// Matches Arduino's real ranged random(lo, hi) (returns lo..hi-1) via this
// dialect's own established arand(n) helper (returns 0..n-1).
int astrRandRange( int lo, int hi )
{
    return lo + arand( hi - lo );
}

// The real 8-direction velocity table upstream repeats verbatim for all
// three asteroid tiers' own init - see header comment for the real
// roll-0/roll-7-collide quirk this preserves exactly.
void astrSetAsteroidVelocity( AstrAsteroid* a )
{
    int dir;
    dir = arand( 8 );

    if( dir == 0 ) { a->vx = a->v; a->vy = a->v; }
    else if( dir == 1 ) { a->vx = -a->v; a->vy = a->v; }
    else if( dir == 2 ) { a->vy = 0; a->vx = -a->v; }
    else if( dir == 3 ) { a->vx = -a->v; a->vy = -a->v; }
    else if( dir == 4 ) { a->vy = -a->v; a->vx = 0; }
    else if( dir == 5 ) { a->vx = a->v; a->vy = -a->v; }
    else if( dir == 6 ) { a->vx = a->v; a->vy = 0; }
    else { a->vx = a->v; a->vy = a->v; } // dir == 7 - real upstream duplicate of dir 0
}

void astrSpawnMediumFromBig( float x, float y )
{
    int k, l;
    for( k = 0; k < 3; k++ )
      for( l = 0; l < ASTR_MAX_MEDIUM; l++ )
        if( !astrMediumAsteroids[ l ].exist )
        {
            astrMediumAsteroids[ l ].exist = true;
            astrMediumAsteroids[ l ].x = x;
            astrMediumAsteroids[ l ].y = y;
            break;
        }
}

void astrSpawnSmallFromMedium( float x, float y )
{
    int k, l;
    for( k = 0; k < 3; k++ )
      for( l = 0; l < ASTR_MAX_SMALL; l++ )
        if( !astrSmallAsteroids[ l ].exist )
        {
            astrSmallAsteroids[ l ].exist = true;
            astrSmallAsteroids[ l ].x = x;
            astrSmallAsteroids[ l ].y = y;
            break;
        }
}

// ---- Init ----

void astrInitGame()
{
    int i;

    for( i = 0; i < ASTR_MAX_BULLETS; i++ )
    {
        astrBullets[ i ].x = LCDWIDTH / 2;
        astrBullets[ i ].y = LCDHEIGHT / 2;
        astrBullets[ i ].v = 2;
        astrBullets[ i ].vx = 0;
        astrBullets[ i ].vy = 0;
        astrBullets[ i ].exist = false;
    }

    astrShip.x = LCDWIDTH / 2;
    astrShip.y = LCDHEIGHT / 2;
    astrShip.w = 5;
    astrShip.h = 5;
    astrShip.rotation = 0;
    astrShip.v = 0;
    astrShip.vx = 0;
    astrShip.vy = 0;
    astrShip.score = 0;

    astrAsteroidExist = 0;

    for( i = 0; i < ASTR_MAX_BIG; i++ )
    {
        if( arand( 2 ) == 0 )
        {
            astrBigAsteroids[ i ].x = astrRandRange( 0, ( LCDWIDTH / 2 ) - astrShip.w * 2 );
            astrBigAsteroids[ i ].y = astrRandRange( 0, ( LCDHEIGHT / 2 ) - astrShip.h * 2 );
        }
        else
        {
            astrBigAsteroids[ i ].x = astrRandRange( ( LCDWIDTH / 2 ) + astrShip.w * 2, LCDWIDTH );
            astrBigAsteroids[ i ].y = astrRandRange( ( LCDHEIGHT / 2 ) + astrShip.h * 2, LCDHEIGHT );
        }
        astrBigAsteroids[ i ].w = 8;
        astrBigAsteroids[ i ].h = 8;
        astrBigAsteroids[ i ].v = 0.1 + astrLevelSpeed;
        astrSetAsteroidVelocity( &astrBigAsteroids[ i ] );
        astrBigAsteroids[ i ].exist = true;
    }

    for( i = 0; i < ASTR_MAX_MEDIUM; i++ )
    {
        astrMediumAsteroids[ i ].x = LCDWIDTH / 2;
        astrMediumAsteroids[ i ].y = LCDHEIGHT / 2;
        astrMediumAsteroids[ i ].w = 6;
        astrMediumAsteroids[ i ].h = 6;
        astrMediumAsteroids[ i ].v = 0.2 + astrLevelSpeed;
        astrSetAsteroidVelocity( &astrMediumAsteroids[ i ] );
        astrMediumAsteroids[ i ].exist = false;
    }

    for( i = 0; i < ASTR_MAX_SMALL; i++ )
    {
        astrSmallAsteroids[ i ].x = LCDWIDTH / 2;
        astrSmallAsteroids[ i ].y = LCDHEIGHT / 2;
        astrSmallAsteroids[ i ].w = 4;
        astrSmallAsteroids[ i ].h = 4;
        astrSmallAsteroids[ i ].v = 0.3 + astrLevelSpeed;
        astrSetAsteroidVelocity( &astrSmallAsteroids[ i ] );
        astrSmallAsteroids[ i ].exist = false;
    }
}

// ---- Draw helpers that also drive next-frame velocity (matches upstream exactly - see header comment) ----

void astrDrawShip( int rotation )
{
    if( rotation == 0 ) { astrShip.vx = 0; astrShip.vy = astrShip.v; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipNBitmap ); }
    else if( rotation == 1 ) { astrShip.vx = -astrShip.v; astrShip.vy = astrShip.v; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipNEBitmap ); }
    else if( rotation == 2 ) { astrShip.vy = 0; astrShip.vx = -astrShip.v; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipEBitmap ); }
    else if( rotation == 3 ) { astrShip.vx = -astrShip.v; astrShip.vy = -astrShip.v; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipSEBitmap ); }
    else if( rotation == 4 ) { astrShip.vy = -astrShip.v; astrShip.vx = 0; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipSBitmap ); }
    else if( rotation == 5 ) { astrShip.vx = astrShip.v; astrShip.vy = -astrShip.v; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipSWBitmap ); }
    else if( rotation == 6 ) { astrShip.vx = astrShip.v; astrShip.vy = 0; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipWBitmap ); }
    else { astrShip.vx = astrShip.v; astrShip.vy = astrShip.v; gbDrawBitmap( (int)astrShip.x, (int)astrShip.y, astrShipNWBitmap ); } // rotation == 7
}

// Real upstream bug preserved exactly here: vx/vy are swapped onto y/x
// (see header comment) - this is NOT a typo in this port, it mirrors
// asteroid.ino's own real `drawBullet()` line for line.
void astrDrawBullet( int rotation, int i )
{
    if( rotation == 0 ) { astrBullets[ i ].vx = -astrBullets[ i ].v; astrBullets[ i ].vy = 0; }
    else if( rotation == 1 ) { astrBullets[ i ].vx = -astrBullets[ i ].v; astrBullets[ i ].vy = astrBullets[ i ].v; }
    else if( rotation == 2 ) { astrBullets[ i ].vy = astrBullets[ i ].v; astrBullets[ i ].vx = 0; }
    else if( rotation == 3 ) { astrBullets[ i ].vx = astrBullets[ i ].v; astrBullets[ i ].vy = astrBullets[ i ].v; }
    else if( rotation == 4 ) { astrBullets[ i ].vy = 0; astrBullets[ i ].vx = astrBullets[ i ].v; }
    else if( rotation == 5 ) { astrBullets[ i ].vx = astrBullets[ i ].v; astrBullets[ i ].vy = -astrBullets[ i ].v; }
    else if( rotation == 6 ) { astrBullets[ i ].vx = 0; astrBullets[ i ].vy = -astrBullets[ i ].v; }
    else { astrBullets[ i ].vx = -astrBullets[ i ].v; astrBullets[ i ].vy = -astrBullets[ i ].v; } // rotation == 7

    gbDrawPixel( astrBullets[ i ].x, astrBullets[ i ].y );
}

// ---- Per-tier update (move, wrap, collide, split/score, draw) ----

void astrUpdateSmallAsteroids()
{
    int i, j;
    for( i = 0; i < ASTR_MAX_SMALL; i++ )
    {
        if( !astrSmallAsteroids[ i ].exist ) continue;

        astrAsteroidExist = astrAsteroidExist + 1;
        astrSmallAsteroids[ i ].x = astrSmallAsteroids[ i ].x + astrSmallAsteroids[ i ].vx;
        astrSmallAsteroids[ i ].y = astrSmallAsteroids[ i ].y + astrSmallAsteroids[ i ].vy;

        if( astrSmallAsteroids[ i ].x < 0 ) astrSmallAsteroids[ i ].x = LCDWIDTH;
        if( astrSmallAsteroids[ i ].y < 0 ) astrSmallAsteroids[ i ].y = LCDHEIGHT;
        if( astrSmallAsteroids[ i ].x > LCDWIDTH ) astrSmallAsteroids[ i ].x = 0;
        if( astrSmallAsteroids[ i ].y > LCDHEIGHT ) astrSmallAsteroids[ i ].y = 0;

        if( gbCollideRectRect( (int)astrShip.x, (int)astrShip.y, astrShip.w, astrShip.h, (int)astrSmallAsteroids[ i ].x, (int)astrSmallAsteroids[ i ].y, astrSmallAsteroids[ i ].w, astrSmallAsteroids[ i ].h ) )
        {
            astrShip.score = astrShip.score + 1;
            astrLives = astrLives - 1;
            if( astrLives <= 0 ) astrGameOver = true;
            astrSmallAsteroids[ i ].exist = false;
            gbPlayCancel();
            break;
        }

        for( j = 0; j < ASTR_MAX_BULLETS; j++ )
          if( astrBullets[ j ].exist && gbCollideRectRect( astrBullets[ j ].x, astrBullets[ j ].y, 1, 1, (int)astrSmallAsteroids[ i ].x, (int)astrSmallAsteroids[ i ].y, astrSmallAsteroids[ i ].w, astrSmallAsteroids[ i ].h ) )
          {
              astrShip.score = astrShip.score + 1;
              astrBullets[ j ].exist = false;
              astrSmallAsteroids[ i ].exist = false;
              gbPlayTick();
          }

        gbDrawBitmap( (int)astrSmallAsteroids[ i ].x, (int)astrSmallAsteroids[ i ].y, astrSmallAsteroidBitmap );
    }
}

void astrUpdateMediumAsteroids()
{
    int i, j;
    for( i = 0; i < ASTR_MAX_MEDIUM; i++ )
    {
        if( !astrMediumAsteroids[ i ].exist ) continue;

        astrAsteroidExist = astrAsteroidExist + 1;
        astrMediumAsteroids[ i ].x = astrMediumAsteroids[ i ].x + astrMediumAsteroids[ i ].vx;
        astrMediumAsteroids[ i ].y = astrMediumAsteroids[ i ].y + astrMediumAsteroids[ i ].vy;

        if( astrMediumAsteroids[ i ].x < 0 ) astrMediumAsteroids[ i ].x = LCDWIDTH;
        if( astrMediumAsteroids[ i ].y < 0 ) astrMediumAsteroids[ i ].y = LCDHEIGHT;
        if( astrMediumAsteroids[ i ].x > LCDWIDTH ) astrMediumAsteroids[ i ].x = 0;
        if( astrMediumAsteroids[ i ].y > LCDHEIGHT ) astrMediumAsteroids[ i ].y = 0;

        if( gbCollideRectRect( (int)astrShip.x, (int)astrShip.y, astrShip.w, astrShip.h, (int)astrMediumAsteroids[ i ].x, (int)astrMediumAsteroids[ i ].y, astrMediumAsteroids[ i ].w, astrMediumAsteroids[ i ].h ) )
        {
            astrShip.score = astrShip.score + 1;
            astrLives = astrLives - 1;
            if( astrLives <= 0 ) astrGameOver = true;
            astrMediumAsteroids[ i ].exist = false;
            astrSpawnSmallFromMedium( astrMediumAsteroids[ i ].x, astrMediumAsteroids[ i ].y );
            gbPlayCancel();
            break;
        }

        for( j = 0; j < ASTR_MAX_BULLETS; j++ )
          if( astrBullets[ j ].exist && gbCollideRectRect( astrBullets[ j ].x, astrBullets[ j ].y, 1, 1, (int)astrMediumAsteroids[ i ].x, (int)astrMediumAsteroids[ i ].y, astrMediumAsteroids[ i ].w, astrMediumAsteroids[ i ].h ) )
          {
              astrShip.score = astrShip.score + 1;
              astrBullets[ j ].exist = false;
              astrMediumAsteroids[ i ].exist = false;
              astrSpawnSmallFromMedium( astrMediumAsteroids[ i ].x, astrMediumAsteroids[ i ].y );
              gbPlayTick();
          }

        gbDrawBitmap( (int)astrMediumAsteroids[ i ].x, (int)astrMediumAsteroids[ i ].y, astrMediumAsteroidBitmap );
    }
}

void astrUpdateBigAsteroids()
{
    int i, j;
    for( i = 0; i < ASTR_MAX_BIG; i++ )
    {
        if( !astrBigAsteroids[ i ].exist ) continue;

        astrAsteroidExist = astrAsteroidExist + 1;
        astrBigAsteroids[ i ].x = astrBigAsteroids[ i ].x + astrBigAsteroids[ i ].vx;
        astrBigAsteroids[ i ].y = astrBigAsteroids[ i ].y + astrBigAsteroids[ i ].vy;

        if( astrBigAsteroids[ i ].x < 0 ) astrBigAsteroids[ i ].x = LCDWIDTH;
        if( astrBigAsteroids[ i ].y < 0 ) astrBigAsteroids[ i ].y = LCDHEIGHT;
        if( astrBigAsteroids[ i ].x > LCDWIDTH ) astrBigAsteroids[ i ].x = 0;
        if( astrBigAsteroids[ i ].y > LCDHEIGHT ) astrBigAsteroids[ i ].y = 0;

        if( gbCollideRectRect( (int)astrShip.x, (int)astrShip.y, astrShip.w, astrShip.h, (int)astrBigAsteroids[ i ].x, (int)astrBigAsteroids[ i ].y, astrBigAsteroids[ i ].w, astrBigAsteroids[ i ].h ) )
        {
            astrShip.score = astrShip.score + 1;
            astrLives = astrLives - 1;
            if( astrLives <= 0 ) astrGameOver = true;
            astrBigAsteroids[ i ].exist = false;
            astrSpawnMediumFromBig( astrBigAsteroids[ i ].x, astrBigAsteroids[ i ].y );
            gbPlayCancel();
            break;
        }

        for( j = 0; j < ASTR_MAX_BULLETS; j++ )
          if( astrBullets[ j ].exist && gbCollideRectRect( astrBullets[ j ].x, astrBullets[ j ].y, 1, 1, (int)astrBigAsteroids[ i ].x, (int)astrBigAsteroids[ i ].y, astrBigAsteroids[ i ].w, astrBigAsteroids[ i ].h ) )
          {
              astrShip.score = astrShip.score + 1;
              astrBullets[ j ].exist = false;
              astrBigAsteroids[ i ].exist = false;
              astrSpawnMediumFromBig( astrBigAsteroids[ i ].x, astrBigAsteroids[ i ].y );
              gbPlayTick();
          }

        gbDrawBitmap( (int)astrBigAsteroids[ i ].x, (int)astrBigAsteroids[ i ].y, astrBigAsteroidBitmap );
    }
}

// ---- States ----

void astrUpdateSplash()
{
    gbDrawBitmap( 10, 4, astrLogoBitmap );
    gbCursorX = 24;
    gbCursorY = 38;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      astrState = ASTR_STATE_PLAY;
}

void astrUpdatePlay()
{
    // gb.display.setTextSize(0) has no verifiable real equivalent - see
    // header comment. Score / total-rock-count / lives HUD, at upstream's
    // own exact cursor positions.
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintNumber( astrShip.score );
    gbCursorX = 8;
    gbCursorY = 0;
    gbPrintString( "/" );
    gbCursorX = 12;
    gbCursorY = 0;
    gbPrintNumber( ASTR_MAX_BIG + ASTR_MAX_MEDIUM + ASTR_MAX_SMALL );
    gbCursorX = 81;
    gbCursorY = 0;
    gbPrintNumber( astrLives );

    // rotate the ship - one fixed 45-degree step per fresh press, not a
    // held-repeat (matches upstream's own literal gbPressed()-shaped call)
    if( gbPressed( BTN_LEFT ) )
      astrShip.rotation = astrShip.rotation - 1;
    if( gbPressed( BTN_RIGHT ) )
      astrShip.rotation = astrShip.rotation + 1;
    if( astrShip.rotation > 7 ) astrShip.rotation = 0;
    if( astrShip.rotation < 0 ) astrShip.rotation = 7;

    // thrust forward/backward - also a single step per fresh press, not a
    // held-repeat, matching upstream exactly (a real, if coarse, control feel)
    if( gbPressed( BTN_UP ) )
      astrShip.v = astrShip.v - 0.1;
    if( gbPressed( BTN_DOWN ) )
      astrShip.v = astrShip.v + 0.1;
    if( astrShip.v > 1.8 ) astrShip.v = 1.8;
    if( astrShip.v < -1.8 ) astrShip.v = -1.8;

    astrShip.x = astrShip.x + astrShip.vx;
    astrShip.y = astrShip.y + astrShip.vy;

    // wraparound at every edge
    if( astrShip.x < 0 ) astrShip.x = LCDWIDTH;
    if( astrShip.y < 0 ) astrShip.y = LCDHEIGHT;
    if( astrShip.x > LCDWIDTH ) astrShip.x = 0;
    if( astrShip.y > LCDHEIGHT ) astrShip.y = 0;

    // fire a bullet
    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        int i;
        for( i = 0; i < ASTR_MAX_BULLETS; i++ )
          if( !astrBullets[ i ].exist )
          {
              astrBullets[ i ].x = (int)( astrShip.x + astrShip.w / 2 );
              astrBullets[ i ].y = (int)( astrShip.y + astrShip.h / 2 );
              astrBullets[ i ].rotation = astrShip.rotation;
              astrBullets[ i ].exist = true;
              break;
          }
    }

    // move / destroy / draw every bullet
    int i;
    for( i = 0; i < ASTR_MAX_BULLETS; i++ )
    {
        if( astrBullets[ i ].x < 0 || astrBullets[ i ].y < 0 || astrBullets[ i ].x > LCDWIDTH || astrBullets[ i ].y > LCDHEIGHT )
        {
            astrBullets[ i ].x = LCDWIDTH / 2;
            astrBullets[ i ].y = LCDHEIGHT / 2;
            astrBullets[ i ].vx = 0;
            astrBullets[ i ].vy = 0;
            astrBullets[ i ].exist = false;
            gbPlayTick();
        }

        if( astrBullets[ i ].exist )
        {
            // real upstream axis-transposed move - see header comment
            astrBullets[ i ].y = astrBullets[ i ].y + astrBullets[ i ].vx;
            astrBullets[ i ].x = astrBullets[ i ].x + astrBullets[ i ].vy;
            astrDrawBullet( astrBullets[ i ].rotation, i );
        }
    }

    astrAsteroidExist = 0;
    astrUpdateSmallAsteroids();
    astrUpdateMediumAsteroids();
    astrUpdateBigAsteroids();

    // level-complete check happens AFTER all three tiers, exactly like
    // upstream - see header comment for the real same-tick
    // level-complete-overrides-game-over quirk this preserves
    if( astrAsteroidExist <= 0 )
    {
        astrGameOver = false;
        astrLevelSpeed = astrLevelSpeed + 0.1;
        astrShip.score = 0;
        astrLevel = astrLevel + 1;
        astrState = ASTR_STATE_NEXTLEVEL;
    }

    astrDrawShip( astrShip.rotation );

    if( astrState == ASTR_STATE_PLAY && astrGameOver )
      astrState = ASTR_STATE_GAMEOVER;
}

void astrUpdateNextLevel()
{
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Next Level:" );
    gbCursorX = 0;
    gbCursorY = 6;
    gbPrintNumber( astrLevel );
    gbCursorX = 0;
    gbCursorY = 12;
    gbPrintString( "!!!Higher Speed!!!" );

    if( gbPressed( BTN_A ) )
    {
        astrInitGame();
        astrState = ASTR_STATE_PLAY;
    }
}

void astrUpdateGameOver()
{
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Game OVER" );

    if( gbPressed( BTN_A ) )
    {
        astrGameOver = false;
        astrLevelSpeed = 0.0;
        astrLevel = 0;
        astrLives = 3;
        astrShip.score = 0;
        astrInitGame();
        astrState = ASTR_STATE_PLAY;
    }
}

// ---- Entry points ----

void gameAsteroidRipper_init()
{
    gbBegin();
    astrLevelSpeed = 0.0;
    astrLevel = 0;
    astrLives = 3;
    astrPaused = false;
    astrGameOver = false;
    astrState = ASTR_STATE_SPLASH;
    astrInitGame();
}

void gameAsteroidRipper_update()
{
    if( !gbUpdate() ) return;

    // pause toggle only applies mid-game, matching real upstream's own
    // exact reachable window (real hardware's `delay(3000)` inside the
    // Next Level / Game Over screens blocks button polling entirely, so
    // Button C was never actually reachable during those on real hardware
    // either - see header comment on why those screens are now their own
    // A-press-gated states rather than a blocking delay)
    if( astrState == ASTR_STATE_PLAY && gbPressed( BTN_C ) )
      astrPaused = !astrPaused;

    if( !astrPaused )
    {
        if( astrState == ASTR_STATE_SPLASH ) astrUpdateSplash();
        else if( astrState == ASTR_STATE_NEXTLEVEL ) astrUpdateNextLevel();
        else if( astrState == ASTR_STATE_GAMEOVER ) astrUpdateGameOver();
        else astrUpdatePlay();
    }
    else
    {
        // Fixed here, not preserved - see this file's own header comment
        // for the real upstream behavior this replaces (pausing froze the
        // entire screen with no "PAUSED" banner or HUD at all, giving no
        // visual indication the game was paused rather than frozen/
        // crashed). A plain centered label is now drawn while paused.
        gbSetColor( 1 );
        gbCursorX = ( LCDWIDTH - 36 ) / 2;
        gbCursorY = LCDHEIGHT / 2 - 3;
        gbPrintString( "PAUSED" );
    }

    gbRenderFrame();
}
