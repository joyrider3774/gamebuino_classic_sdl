#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Super Space Shooter (msevilgenius, license: none specified -
// github.com/msevilgenius/Gamebuino-SuperSpaceShooter). A side-scrolling
// shoot-'em-up: move a ship around a fixed x-column near the left edge,
// fire east with Button A, dodge/pop enemy bullets fired in up to 8
// compass directions. Real upstream is a genuine prototype, not a
// finished game (see its own todo.txt and README.md: "Proper enemy
// spawning" and "Better system for finding empty space in array" are
// both still-open upstream TODOs) - Button B is upstream's own real,
// documented "temporary way to create enemies for testing" (main.cpp's
// own comment, verbatim), and it is the ONLY way any enemy ever appears
// in this real game; there is no automatic spawner anywhere in upstream
// source. Preserved exactly as-is, not a shortcut this port invented.
//
// STRUCTURAL NOTE: unlike every other game ported into this cartridge so
// far, real upstream here has no `.ino` file at all - it is pure C++
// (main.cpp plus six real classes, each its own .cpp/.h pair: Bullet,
// BulletManager, Enemy, EnemyManager, Player, EffectsManager). This
// dialect has no classes/methods (see gamebuinoShim.h's own header
// comment), so every class became a plain data-only struct plus free
// functions that take an explicit array index (or, for the single-
// instance Player, plain globals with no index at all - matching this
// project's own "flatten a real single-instance C++ library into plain C
// globals/functions" precedent, here applied to a *game's* own classes
// rather than just the Gamebuino API itself):
//   Bullet[64]        -> struct SssBullet;  SssBullet[64] sssBullets;
//   Enemy[10]         -> struct SssEnemy;   SssEnemy[10] sssEnemies;
//   EffectsManager's
//     own effect[8]/point[8] -> struct SssEffect/SssStar, SssEffect[8]
//     sssEffects; SssStar[8] sssStars (both were private nested typedef
//     structs upstream - promoted to real top-level named struct types,
//     since this dialect requires a named `struct Name {...};` rather
//     than an anonymous nested typedef, per VIRCON32_C_DIALECT.md's own
//     "typedef struct {...} Name;` is rejected outright" finding).
//   Player            -> plain globals (sssPlayerX/Y/etc) - only one
//     ever exists, so an index/array would add nothing.
// `HitBox` (a real plain data struct upstream, no methods) ports directly
// as a named `struct SssHitBox {...};` (this dialect's own C++-style
// struct declaration - the tag name is the type name at every use site
// afterward, no repeated `struct` keyword needed), but every function
// that upstream returned one from (`getCollisionBox()`) became `void
// ...GetCollisionBox(..., SssHitBox* out)` - a 4-field/4-word struct
// cannot be returned by value
// in this dialect (VIRCON32_C_DIALECT.md #4: "functions cannot return
// values of size > 1"), so every call site takes the real out-pointer
// form instead (matching this project's own established
// gameDescent.c-precedent for the identical constraint).
//
// A genuine 3-way circular dependency falls out of the flattening
// (Bullet's own testCollision() needs Player's/EnemyManager's own
// functions; Enemy's own shoot() needs BulletManager's own
// createBullet(); Player's own hit() needs the shared reset()) - real
// upstream resolved this the C++ way, with forward class declarations
// and `extern` global instances across separate translation units. This
// single-translation-unit dialect resolves the same shape of problem the
// same way plain C always has: forward function prototypes at the top of
// this file for the handful of functions that get called before their
// own definition (a pattern already proven working elsewhere in this
// cartridge - see gameSkibuino.c's own forward-declared `skiThinkAll()`
// etc).
//
// DIALECT REWRITES: every real `gb.x.y(...)` call site was mechanically
// rewritten to a plain `gbY(...)` function call (see gamePong.c's own
// header comment). `int8_t`/`uint8_t`/`uint16_t`/`byte` all become plain
// `int` (avrCompat.h aliasing/this dialect's own single 32-bit int type -
// no narrower integer width exists here anyway). `random(N)` became
// `arand(N)`; `random(a,b)` (upstream's own real ranged form, used once
// for enemy-spawn Y position) became `a + arand(b - a)`, matching this
// project's own established real-Arduino-semantics-preserving convention
// (`[a,b)`, upper-exclusive) rather than assumed. `max()`/`min()` (Arduino
// macros) became `gbMax()`/`gbMin()`. `PROGMEM`/`pgm_read_byte()`/
// `pgm_read_word()` are dropped outright - already no-ops in this shim,
// and every real PROGMEM byte table (7 real sprite bitmaps, 3 movement
// tables, 3 shooting-pattern tables, 8 explosion-animation frames, and
// the 64x28 real title-screen logo) is copied verbatim as a plain
// `int[]` global, with every real AVR `B`-binary-literal byte (e.g.
// `B01111101`) converted to its decimal value (verified via a small
// Python conversion script cross-checked against the real source's own
// byte count per array, not hand-transcribed, to rule out a transcription
// mistake on ~280 total literal bytes) - `gbDrawBitmap()`'s own real
// format (`bitmap[0]`=width, `[1]`=height, then row-major MSB-first
// packed bytes) matches upstream's real `Display::drawBitmap()` format
// exactly, so every sprite byte carries over unchanged, only the literal
// syntax changes.
//
// The real `enemy_types[]` PROGMEM pointer table (an array of pointers
// selecting sprite/movement/shooting data by `ENEMY_TYPE`) is ported as
// three small lookup functions (`sssEnemySpriteTable()`/
// `sssEnemyMovementTable()`/`sssEnemyShootingTable()`, one `if` per real
// non-DEAD type) rather than a genuine pointer-to-array table, since this
// game only ever has 3 real playable enemy types (BASIC/SPINNER/WEAVER) -
// simpler and exactly as correct as a real table lookup for this small a
// set. The real `dead_sprite`/`dead_movement`/`dead_shooting` PROGMEM
// arrays (all `{0,0}`/`{0}`-style placeholders, real upstream's own
// `enemy_types[DEAD*3 + ...]` slots) are NOT ported at all - traced
// through and confirmed to have zero observable effect: real
// `EnemyManager::update()` only ever calls `Enemy::draw()` on an enemy
// that was already confirmed non-dead *before* `Enemy::update()` ran that
// same tick, so the one real case where a freshly-DEAD enemy's own
// `draw()` still fires this tick (an enemy whose `x` just crossed `-2`)
// reads `dead_sprite` = a real, genuine `{width=0, height=0}` bitmap -
// which draws literally nothing. This port reproduces the exact same
// *visible* outcome by having `sssEnemiesUpdate()` simply skip the draw
// call once an enemy goes dead this same tick, rather than porting a
// zero-size bitmap purely to draw nothing with it.
//
// UPSTREAM BUGS/QUIRKS - preserved as genuine, load-bearing original
// behavior (traced through each one, not assumed harmless):
// - `BulletManager::createBullet()`/`EnemyManager::createEnemy()`/
//   `EffectsManager::createEffect()` all use the exact same real
//   "not terribly efficient" linear scan upstream's own comments admit
//   to: if every slot in a fixed-size array is already alive, the *last*
//   slot is silently reused/overwritten anyway (`i < MAX-1` stops the
//   scan one short of the end, then that final index is used
//   unconditionally) - a real, harmless-at-this-game's-real-scale bug,
//   reproduced exactly rather than "fixed" into a genuinely different
//   (never-shipped) full/reject behavior.
// - `Player::hit()`'s own `if(health<0) reset();` means the player
//   survives exactly 7 hits (health starts at 6, decrements to -1 before
//   a reset fires) - an odd-looking off-by-one against a nominal "6
//   health", but genuine, deliberate upstream design (its own 3-heart,
//   3-pip-each HUD is built around exactly this range), preserved as-is.
// - Real `if(abtn||gb.buttons.pressed(BTN_A)){ player.shoot(); }` (with
//   `abtn` a manually edge-tracked "is A currently held" boolean upstream
//   builds itself, via its own `updateButtons()`) is traced through and
//   found to be exactly equivalent to "call shoot() every tick A is held"
//   - `abtn` is already true on the very same tick `pressed(BTN_A)` first
//   fires (both set from the identical edge), so the `||` never adds a
//   tick the plain level-check didn't already cover. Ported directly as
//   `gbHeld(BTN_A, 1)` (this shim's own real "currently held" query, ==
//   `gbBtnHeld[button] >= 1`) - a documented, traced-through
//   simplification, not a behavior change (`Player::shoot()`'s own
//   internal `last_bullet_time > 5` cooldown is what actually gates real
//   fire rate either way). The same reasoning/primitive applies to
//   upstream's own manually-tracked `upbtn`/`dnbtn`/`lfbtn`/`rtbtn`
//   (movement is also called every tick a direction is held) - all four
//   ported as `gbHeld(BTN_x, 1)` directly, with no local edge-tracking
//   state needed at all.
// - Real `EffectsManager::update()`'s own `switch` has a real, easy-to-
//   miss fallthrough: even when an explosion's `age` exceeds 3 and its
//   `type` is reset to `E_NONE` this same tick, control still falls
//   through to the shared `effects[i].age++` at the bottom of the `for`
//   body (the `break` only exits the `switch`, not the loop) - a real,
//   harmless extra increment on an already-dead effect slot, reproduced
//   exactly via an if/else that still increments `age` on both the
//   "aged out" and "still animating" branches (see `sssEffectsUpdate()`).
//
// Real upstream's own blocking `gb.titleScreen(F(" SSS by msevilgenius"),
// logo)` (called once at boot, and again on every Button C press mid-
// game, immediately followed in both cases by a real `reset()` call once
// it returns) became an explicit `SSS_STATE_TITLE`/`SSS_STATE_PLAY` state
// machine, matching gamePong.c's own established treatment of this exact
// upstream pattern - `sssReset()` is called at the same real point
// upstream's own blocking call would have returned (the moment Button A
// dismisses the title), not on the Button C press itself, preserving the
// real order of operations (old game state stays frozen, untouched, for
// however long the title screen is shown, then gets cleared right before
// play resumes). The real title-screen logo (64x28) and upstream's own
// literal title text are drawn directly by `sssUpdateTitle()`, since this
// shim has no generic built-in title-screen widget of its own (every
// ported game implements its own title state - see gamePong.c). Real
// `gb.display.setFont(font3x5)` (called by upstream's own `drawScore()`,
// every single frame) is set once in `gameSuperSpaceShooter_init()`
// instead: `gbFont3x5` is already this shim's own real default font, so
// upstream's own per-frame call is a no-op after the very first tick -
// traced through and confirmed safe to call once, not every frame
// (nothing else in this game ever calls `gbSetFont()`, so the font can
// never actually change out from under it). `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op; `gb.battery.show = false`
// (real hardware's own battery-icon cosmetic setting, no shim equivalent)
// was dropped outright, matching this project's established norm for
// this class of setting.

typedef struct
{
    int x;
    int y;
    int w;
    int h;
} SssHitBox;

typedef struct
{
    int x;
    int y;
    int speed;
    int direction; // SSS_DIR_* below - SSS_DIR_NONE (0) means dead
    int source;    // SSS_SRC_PLAYER / SSS_SRC_ENEMY
} SssBullet;

typedef struct
{
    int x;
    int y;
    int type; // SSS_* enemy type below - SSS_DEAD (0) means dead
    int shootFrame;
    int lastBulletTime;
} SssEnemy;

typedef struct
{
    int type; // SSS_E_* below - SSS_E_NONE (0) means dead
    int x;
    int y;
    int age;
} SssEffect;

typedef struct
{
    int x;
    int y;
} SssStar;

#define SSS_MAX_BULLETS 64
#define SSS_BULLET_SIZE 3
#define SSS_DIAG_BULLET_SIZE 2
#define SSS_MAX_ENEMIES 10
#define SSS_MAX_EFFECTS 8
#define SSS_MAX_STARS 8

#define SSS_SRC_PLAYER 0
#define SSS_SRC_ENEMY 1

// Real DIRECTION enum order (globals.h): D_NONE, DIR_N, DIR_NE, DIR_E,
// DIR_SE, DIR_S, DIR_SW, DIR_W, DIR_NW.
#define SSS_DIR_NONE 0
#define SSS_DIR_N 1
#define SSS_DIR_NE 2
#define SSS_DIR_E 3
#define SSS_DIR_SE 4
#define SSS_DIR_S 5
#define SSS_DIR_SW 6
#define SSS_DIR_W 7
#define SSS_DIR_NW 8

// Real ENEMY_TYPE enum order: DEAD, BASIC, SPINNER, WEAVER.
#define SSS_DEAD 0
#define SSS_BASIC 1
#define SSS_SPINNER 2
#define SSS_WEAVER 3

// Real EFFECT_TYPE enum order: E_NONE, EXPLOSION_SMALL, EXPLOSION_LARGE,
// FLASH. FLASH is real upstream dead code - declared in the enum but
// never actually created anywhere in real EffectsManager.cpp/Enemy.cpp/
// Bullet.cpp/Player.cpp - kept here only for real enum-value parity, and
// (like upstream) never triggered.
#define SSS_E_NONE 0
#define SSS_EXPLOSION_SMALL 1
#define SSS_EXPLOSION_LARGE 2
#define SSS_FLASH 3

#define SSS_STATE_TITLE 0
#define SSS_STATE_PLAY 1

SssBullet sssBullets[64];
SssEnemy sssEnemies[10];
SssEffect sssEffects[8];
SssStar sssStars[8];

int sssScore;
int sssStarTimer;
int sssState;

int sssPlayerX;
int sssPlayerY;
int sssPlayerBulletSpeed;
int sssPlayerHealth;
int sssPlayerLastBulletTime;
int sssPlayerAbilities;

// -----------------------------------------------------------------------------
// Real bitmap/data tables (Enemy.h/EnemyTypes.h/Player.cpp/
// EffectsManager.cpp/main.cpp's own `logo`), copied verbatim - every real
// AVR `B`-binary-literal byte converted to decimal (see this file's own
// header comment).
// -----------------------------------------------------------------------------

int sssLogoBitmap[226]= {
    64, 28, 0, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0,
    0, 68, 72, 0, 0, 0, 0, 0, 0, 40, 65, 19, 231, 220, 0, 0,
    0, 0, 121, 18, 36, 84, 0, 16, 0, 40, 9, 18, 39, 208, 0, 40,
    0, 68, 9, 18, 36, 16, 0, 16, 0, 0, 73, 18, 36, 80, 0, 0,
    0, 0, 121, 243, 231, 208, 0, 0, 1, 240, 0, 2, 60, 0, 0, 0,
    2, 64, 0, 2, 36, 0, 0, 0, 0, 240, 0, 2, 32, 249, 227, 207,
    129, 224, 62, 0, 60, 136, 34, 72, 128, 240, 24, 0, 4, 137, 226, 15,
    130, 64, 28, 0, 4, 137, 34, 8, 1, 240, 31, 156, 36, 137, 34, 72,
    128, 0, 28, 0, 60, 249, 227, 207, 128, 0, 24, 0, 0, 128, 0, 0,
    0, 0, 62, 0, 0, 128, 0, 0, 0, 0, 0, 0, 60, 128, 0, 4,
    0, 0, 0, 0, 36, 128, 0, 4, 0, 0, 0, 0, 32, 249, 243, 239,
    125, 192, 0, 0, 60, 137, 18, 36, 69, 64, 0, 32, 4, 137, 18, 36,
    125, 0, 0, 0, 4, 137, 18, 36, 65, 0, 0, 0, 36, 137, 18, 36,
    69, 0, 32, 0, 60, 137, 243, 231, 125, 0, 80, 0, 0, 0, 0, 0,
    0, 0
};

int sssBasicSpriteBitmap[9]= { 7, 7, 125, 145, 61, 121, 61, 145, 125 };
int sssBasicMovement[3]= { 1, -1, 0 };
int sssBasicShooting[3]= { 1, 64, 16 };

int sssSpinnerSpriteBitmap[9]= { 7, 7, 1, 57, 255, 171, 255, 57, 1 };
int sssSpinnerMovement[3]= { 1, -1, 0 };
int sssSpinnerShooting[17]= { 8, 128, 4, 64, 4, 32, 4, 16, 4, 8, 4, 4, 4, 2, 4, 1, 4 };

int sssWeaverSpriteBitmap[9]= { 7, 7, 49, 67, 229, 253, 229, 67, 49 };
int sssWeaverMovement[33]= {
    16, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1,
    1
};
int sssWeaverShooting[3]= { 1, 64, 24 };

int sssShipSpriteBitmap[9]= { 7, 7, 249, 97, 113, 255, 113, 97, 249 };
int sssHeart2Bitmap[7]= { 5, 5, 87, 255, 255, 119, 39 };
int sssHeart1Bitmap[7]= { 5, 5, 87, 239, 207, 119, 39 };
int sssHeart0Bitmap[7]= { 5, 5, 87, 175, 143, 87, 39 };

int sssSmallExplosion0[5]= { 5, 3, 0, 0, 32 };
int sssSmallExplosion1[6]= { 5, 4, 0, 16, 64, 32 };
int sssSmallExplosion2[7]= { 5, 5, 64, 8, 128, 128, 32 };
int sssSmallExplosion3[6]= { 5, 4, 32, 0, 8, 128 };

int sssLargeExplosion0[7]= { 8, 5, 0, 0, 0, 8, 12 };
int sssLargeExplosion1[9]= { 8, 7, 0, 16, 36, 18, 72, 44, 16 };
int sssLargeExplosion2[10]= { 8, 8, 16, 50, 0, 34, 128, 0, 197, 34 };
int sssLargeExplosion3[10]= { 8, 4, 64, 1, 128, 0, 8, 0, 0, 65 };

// -----------------------------------------------------------------------------
// Forward declarations - see this file's own header comment on the real
// 3-way circular dependency this resolves (Bullet <-> Player <-> Enemy).
// -----------------------------------------------------------------------------

void sssPlayerGetCollisionBox( SssHitBox* hb );
void sssPlayerHit();
bool sssEnemiesTestShot( int x1, int y1, int x2, int y2, int x3, int y3 );
void sssCreateEffect( int type, int x, int y );
void sssCreateBullet( int x, int y, int dir, int speed, int source );
void sssReset();

// -----------------------------------------------------------------------------
// EffectsManager -> sssEffects*()/sssStars*() (no dependency on any other
// section, so it comes first).
// -----------------------------------------------------------------------------

void sssEffectsInit()
{
    int i;
    for( i = 0; i < SSS_MAX_EFFECTS; i++ )
      sssEffects[ i ].type = SSS_E_NONE;
    sssStarTimer = 0;
}

void sssInitEffect( int id, int type, int x, int y )
{
    sssEffects[ id ].type = type;
    sssEffects[ id ].x = x;
    sssEffects[ id ].y = y;
    sssEffects[ id ].age = 0;
}

void sssCreateEffect( int type, int x, int y )
{
    // real upstream linear scan - see this file's own header comment on
    // the real "silently reuse the last slot once full" quirk
    int i = 0;
    while( sssEffects[ i ].type != SSS_E_NONE && i < SSS_MAX_EFFECTS - 1 )
      i++;
    sssInitEffect( i, type, x, y );
}

int* sssSmallExplosionFrame( int age )
{
    if( age == 0 ) return sssSmallExplosion0;
    if( age == 1 ) return sssSmallExplosion1;
    if( age == 2 ) return sssSmallExplosion2;
    return sssSmallExplosion3;
}

int* sssLargeExplosionFrame( int age )
{
    if( age == 0 ) return sssLargeExplosion0;
    if( age == 1 ) return sssLargeExplosion1;
    if( age == 2 ) return sssLargeExplosion2;
    return sssLargeExplosion3;
}

void sssDrawStarField()
{
    int i;
    for( i = 0; i < SSS_MAX_STARS; i++ )
    {
        sssStars[ i ].x -= ( gbFrameCount % 2 );
        if( sssStars[ i ].x >= 0 )
        {
            gbDrawPixel( sssStars[ i ].x, sssStars[ i ].y );
        }
        else if( sssStarTimer < 1 )
        {
            sssStars[ i ].x = LCDWIDTH;
            sssStars[ i ].y = arand( LCDHEIGHT );
            sssStarTimer = 16 + arand( 64 - 16 ); // real random(16,64)
        }
    }
    sssStarTimer--;
}

void sssEffectsUpdate()
{
    int i;
    for( i = 0; i < SSS_MAX_EFFECTS; i++ )
    {
        if( sssEffects[ i ].type == SSS_EXPLOSION_SMALL )
        {
            if( sssEffects[ i ].age > 3 )
            {
                sssEffects[ i ].type = SSS_E_NONE;
            }
            else
            {
                // real upstream draws with random flip AND rotation so
                // every explosion doesn't look identical
                gbDrawBitmapRotated( sssEffects[ i ].x - 2, sssEffects[ i ].y - 2,
                    sssSmallExplosionFrame( sssEffects[ i ].age ), arand( 4 ), arand( 4 ) );
                sssEffects[ i ].x -= ( gbFrameCount % 2 ); // moves with the scrolling background
            }
            sssEffects[ i ].age++; // real upstream fallthrough - see header comment
        }
        else if( sssEffects[ i ].type == SSS_EXPLOSION_LARGE )
        {
            if( sssEffects[ i ].age > 3 )
            {
                sssEffects[ i ].type = SSS_E_NONE;
            }
            else
            {
                gbDrawBitmapRotated( sssEffects[ i ].x - 3, sssEffects[ i ].y - 3,
                    sssLargeExplosionFrame( sssEffects[ i ].age ), arand( 4 ), arand( 4 ) );
                sssEffects[ i ].x -= ( gbFrameCount % 2 );
            }
            sssEffects[ i ].age++;
        }
        // SSS_E_NONE (or the real never-triggered SSS_FLASH): nothing to
        // update, matching real upstream's own `continue` (skips age++)
    }
    sssDrawStarField();
}

// -----------------------------------------------------------------------------
// Bullet + BulletManager -> sssBullet*()/sssBullets*()/sssCreateBullet()
// -----------------------------------------------------------------------------

bool sssBulletIsDead( int idx )
{
    return sssBullets[ idx ].direction == SSS_DIR_NONE;
}

void sssBulletInit( int idx, int x, int y, int dir, int speed, int source )
{
    sssBullets[ idx ].x = x;
    sssBullets[ idx ].y = y;
    sssBullets[ idx ].direction = dir;
    sssBullets[ idx ].speed = speed;
    sssBullets[ idx ].source = source;
}

void sssBulletTestCollision( int idx, int x1, int y1, int x2, int y2, int x3, int y3 )
{
    SssHitBox hb;
    if( sssBullets[ idx ].source == SSS_SRC_ENEMY )
    {
        sssPlayerGetCollisionBox( &hb );
        if( gbCollidePointRect( x1, y1, hb.x, hb.y, hb.w, hb.h ) ||
            gbCollidePointRect( x2, y2, hb.x, hb.y, hb.w, hb.h ) ||
            gbCollidePointRect( x3, y3, hb.x, hb.y, hb.w, hb.h ) )
        {
            sssPlayerHit();
            sssBullets[ idx ].direction = SSS_DIR_NONE;
            sssCreateEffect( SSS_EXPLOSION_SMALL, sssBullets[ idx ].x, sssBullets[ idx ].y );
        }
    }
    else if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
    {
        if( sssEnemiesTestShot( x1, y1, x2, y2, x3, y3 ) )
        {
            sssBullets[ idx ].direction = SSS_DIR_NONE;
            sssCreateEffect( SSS_EXPLOSION_SMALL, sssBullets[ idx ].x, sssBullets[ idx ].y );
        }
    }
}

void sssBulletUpdateN( int idx )
{
    sssBullets[ idx ].y -= sssBullets[ idx ].speed;
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawFastVLine( x, y, SSS_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x - 1, y + SSS_BULLET_SIZE - 1, x + 1, y + SSS_BULLET_SIZE - 1 );
    sssBulletTestCollision( idx, x, y, x, y + 1, x, y + 2 );
}

void sssBulletUpdateNE( int idx )
{
    if( gbFrameCount % 3 ) // twice every 3 frames
    {
        sssBullets[ idx ].y -= sssBullets[ idx ].speed;
        sssBullets[ idx ].x += sssBullets[ idx ].speed;
    }
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawLine( x, y, x - SSS_DIAG_BULLET_SIZE, y + SSS_DIAG_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x - SSS_BULLET_SIZE, y + 1, x - 1, y + SSS_BULLET_SIZE );
    sssBulletTestCollision( idx, x, y, x - 1, y + 1, x - 2, y + 2 );
}

void sssBulletUpdateE( int idx )
{
    sssBullets[ idx ].x += sssBullets[ idx ].speed;
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawFastHLine( x - ( SSS_BULLET_SIZE - 1 ), y, SSS_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x - ( SSS_BULLET_SIZE - 1 ), y - 1, x - ( SSS_BULLET_SIZE - 1 ), y + 1 );
    sssBulletTestCollision( idx, x, y, x - 1, y, x - 2, y );
}

void sssBulletUpdateSE( int idx )
{
    if( gbFrameCount % 3 )
    {
        sssBullets[ idx ].y += sssBullets[ idx ].speed;
        sssBullets[ idx ].x += sssBullets[ idx ].speed;
    }
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawLine( x, y, x - SSS_DIAG_BULLET_SIZE, y - SSS_DIAG_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x - SSS_BULLET_SIZE, y - 1, x - 1, y - SSS_BULLET_SIZE );
    sssBulletTestCollision( idx, x, y, x - 1, y - 1, x - 2, y - 2 );
}

void sssBulletUpdateS( int idx )
{
    sssBullets[ idx ].y += sssBullets[ idx ].speed;
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawFastVLine( x, y - ( SSS_BULLET_SIZE - 1 ), SSS_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x - 1, y - ( SSS_BULLET_SIZE - 1 ), x + 1, y - ( SSS_BULLET_SIZE - 1 ) );
    sssBulletTestCollision( idx, x, y, x, y - 1, x, y - 2 );
}

void sssBulletUpdateSW( int idx )
{
    if( gbFrameCount % 3 )
    {
        sssBullets[ idx ].y += sssBullets[ idx ].speed;
        sssBullets[ idx ].x -= sssBullets[ idx ].speed;
    }
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawLine( x, y, x + SSS_DIAG_BULLET_SIZE, y - SSS_DIAG_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x + SSS_BULLET_SIZE, y - 1, x + 1, y - SSS_BULLET_SIZE );
    sssBulletTestCollision( idx, x, y, x + 1, y - 1, x + 2, y - 2 );
}

void sssBulletUpdateW( int idx )
{
    sssBullets[ idx ].x -= sssBullets[ idx ].speed;
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawFastHLine( x, y, SSS_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x + SSS_BULLET_SIZE - 1, y - 1, x + SSS_BULLET_SIZE - 1, y + 1 );
    sssBulletTestCollision( idx, x, y, x - 1, y, x - 2, y );
}

void sssBulletUpdateNW( int idx )
{
    if( gbFrameCount % 3 )
    {
        sssBullets[ idx ].y -= sssBullets[ idx ].speed;
        sssBullets[ idx ].x -= sssBullets[ idx ].speed;
    }
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    gbDrawLine( x, y, x + SSS_DIAG_BULLET_SIZE, y + SSS_DIAG_BULLET_SIZE );
    if( sssBullets[ idx ].source == SSS_SRC_PLAYER )
      gbDrawLine( x + SSS_BULLET_SIZE, y + 1, x + 1, y + SSS_BULLET_SIZE );
    sssBulletTestCollision( idx, x, y, x + 1, y + 1, x + 2, y + 2 );
}

void sssBulletUpdate( int idx )
{
    int x = sssBullets[ idx ].x;
    int y = sssBullets[ idx ].y;
    // real upstream "destroy bullets off screen" bounds check
    if( x < 0 || x > LCDWIDTH || y < 0 || y > LCDHEIGHT )
    {
        sssBullets[ idx ].direction = SSS_DIR_NONE;
        return;
    }

    if( sssBullets[ idx ].direction == SSS_DIR_N ) sssBulletUpdateN( idx );
    else if( sssBullets[ idx ].direction == SSS_DIR_NE ) sssBulletUpdateNE( idx );
    else if( sssBullets[ idx ].direction == SSS_DIR_E ) sssBulletUpdateE( idx );
    else if( sssBullets[ idx ].direction == SSS_DIR_SE ) sssBulletUpdateSE( idx );
    else if( sssBullets[ idx ].direction == SSS_DIR_S ) sssBulletUpdateS( idx );
    else if( sssBullets[ idx ].direction == SSS_DIR_SW ) sssBulletUpdateSW( idx );
    else if( sssBullets[ idx ].direction == SSS_DIR_W ) sssBulletUpdateW( idx );
    else if( sssBullets[ idx ].direction == SSS_DIR_NW ) sssBulletUpdateNW( idx );
    // SSS_DIR_NONE: nothing to do, matching real upstream's own `case D_NONE: return;`
}

void sssBulletsReset()
{
    int i;
    for( i = 0; i < SSS_MAX_BULLETS; i++ )
    {
        if( !sssBulletIsDead( i ) )
          sssBulletInit( i, 0, 0, SSS_DIR_NONE, 0, 0 );
    }
}

void sssCreateBullet( int x, int y, int dir, int speed, int source )
{
    // real upstream linear scan - see this file's own header comment on
    // the real "silently reuse the last slot once full" quirk
    int i = 0;
    while( !sssBulletIsDead( i ) && i < SSS_MAX_BULLETS - 1 )
      i++;
    sssBulletInit( i, x, y, dir, speed, source );
}

void sssBulletsUpdateAndDraw()
{
    int i;
    for( i = 0; i < SSS_MAX_BULLETS; i++ )
    {
        if( !sssBulletIsDead( i ) )
          sssBulletUpdate( i );
    }
}

// -----------------------------------------------------------------------------
// Enemy + EnemyManager -> sssEnemy*()/sssEnemies*()/sssCreateEnemy()
// -----------------------------------------------------------------------------

int* sssEnemySpriteTable( int type )
{
    if( type == SSS_SPINNER ) return sssSpinnerSpriteBitmap;
    if( type == SSS_WEAVER ) return sssWeaverSpriteBitmap;
    return sssBasicSpriteBitmap; // SSS_BASIC (SSS_DEAD is never drawn - see header comment)
}

int* sssEnemyMovementTable( int type )
{
    if( type == SSS_SPINNER ) return sssSpinnerMovement;
    if( type == SSS_WEAVER ) return sssWeaverMovement;
    return sssBasicMovement;
}

int* sssEnemyShootingTable( int type )
{
    if( type == SSS_SPINNER ) return sssSpinnerShooting;
    if( type == SSS_WEAVER ) return sssWeaverShooting;
    return sssBasicShooting;
}

bool sssEnemyIsDead( int idx )
{
    return sssEnemies[ idx ].type == SSS_DEAD;
}

void sssEnemyInit( int idx, int x, int y, int type )
{
    sssEnemies[ idx ].x = x;
    sssEnemies[ idx ].y = y;
    sssEnemies[ idx ].type = type;
    sssEnemies[ idx ].lastBulletTime = 0;
    sssEnemies[ idx ].shootFrame = 0;
}

void sssEnemyDraw( int idx )
{
    int* sprite = sssEnemySpriteTable( sssEnemies[ idx ].type );
    gbDrawBitmap( sssEnemies[ idx ].x - 3, sssEnemies[ idx ].y - 3, sprite );
}

void sssEnemyGetCollisionBox( int idx, SssHitBox* hb )
{
    hb->x = sssEnemies[ idx ].x - 3;
    if( sssEnemies[ idx ].type == SSS_SPINNER ) hb->y = sssEnemies[ idx ].y - 2;
    else hb->y = sssEnemies[ idx ].y - 3;
    hb->w = 7;
    if( sssEnemies[ idx ].type == SSS_SPINNER ) hb->h = 5;
    else hb->h = 7;
}

void sssEnemyHit( int idx )
{
    sssEnemies[ idx ].type = SSS_DEAD;
    sssScore += 10;
    sssCreateEffect( SSS_EXPLOSION_LARGE, sssEnemies[ idx ].x, sssEnemies[ idx ].y );
}

void sssEnemyMove( int idx )
{
    int* ref = sssEnemyMovementTable( sssEnemies[ idx ].type );
    int frame = gbFrameCount % ref[ 0 ];
    sssEnemies[ idx ].x += ref[ frame * 2 + 1 ];
    sssEnemies[ idx ].y += ref[ frame * 2 + 2 ];
}

void sssEnemyShoot( int idx )
{
    int* ref = sssEnemyShootingTable( sssEnemies[ idx ].type );
    int numFrames = ref[ 0 ];
    int shootFrame = sssEnemies[ idx ].shootFrame;
    // real upstream's own "horrible way of finding the data for the
    // previous frame, which has to wrap around negatively" (its own
    // comment, verbatim) - ported with the same modulo-wrap formula
    int prevFrame = ( shootFrame + ( numFrames - 1 ) ) % numFrames;
    if( sssEnemies[ idx ].lastBulletTime < ref[ ( prevFrame + 1 ) * 2 ] )
      return;

    int directions = ref[ shootFrame * 2 + 1 ];
    int x = sssEnemies[ idx ].x;
    int y = sssEnemies[ idx ].y;
    // each bit is a compass direction, MSB first: NW W SW S SE E NE N
    if( directions & 128 ) sssCreateBullet( x, y, SSS_DIR_NW, 2, SSS_SRC_ENEMY );
    if( directions & 64 )  sssCreateBullet( x, y, SSS_DIR_W, 2, SSS_SRC_ENEMY );
    if( directions & 32 )  sssCreateBullet( x, y, SSS_DIR_SW, 2, SSS_SRC_ENEMY );
    if( directions & 16 )  sssCreateBullet( x, y, SSS_DIR_S, 2, SSS_SRC_ENEMY );
    if( directions & 8 )   sssCreateBullet( x, y, SSS_DIR_SE, 2, SSS_SRC_ENEMY );
    if( directions & 4 )   sssCreateBullet( x, y, SSS_DIR_E, 2, SSS_SRC_ENEMY );
    if( directions & 2 )   sssCreateBullet( x, y, SSS_DIR_NE, 2, SSS_SRC_ENEMY );
    if( directions & 1 )   sssCreateBullet( x, y, SSS_DIR_N, 2, SSS_SRC_ENEMY );

    sssEnemies[ idx ].shootFrame = ( shootFrame + 1 ) % numFrames;
    sssEnemies[ idx ].lastBulletTime = 0;
}

void sssEnemyUpdate( int idx )
{
    if( sssEnemies[ idx ].x < -2 )
    {
        sssEnemies[ idx ].type = SSS_DEAD;
        return;
    }
    sssEnemies[ idx ].lastBulletTime++;
    sssEnemyMove( idx );
    sssEnemyShoot( idx );
}

void sssEnemiesReset()
{
    int i;
    for( i = 0; i < SSS_MAX_ENEMIES; i++ )
    {
        if( !sssEnemyIsDead( i ) )
          sssEnemyInit( i, 0, 0, SSS_DEAD );
    }
}

void sssEnemiesUpdate()
{
    int i;
    for( i = 0; i < SSS_MAX_ENEMIES; i++ )
    {
        if( !sssEnemyIsDead( i ) )
        {
            sssEnemyUpdate( i );
            // real upstream calls draw() unconditionally after update()
            // even if update() just killed it - traced through and
            // confirmed a no-visible-op in that exact case (see header
            // comment on the real, unported dead_sprite={0,0}), so this
            // port skips the now-redundant draw call directly
            if( !sssEnemyIsDead( i ) )
              sssEnemyDraw( i );
        }
    }
}

void sssCreateEnemy( int x, int y, int type )
{
    // real upstream linear scan - see this file's own header comment on
    // the real "silently reuse the last slot once full" quirk
    int i = 0;
    while( !sssEnemyIsDead( i ) && i < SSS_MAX_ENEMIES - 1 )
      i++;
    sssEnemyInit( i, x, y, type );
}

bool sssEnemiesTestShot( int x1, int y1, int x2, int y2, int x3, int y3 )
{
    SssHitBox hb;
    int i;
    for( i = 0; i < SSS_MAX_ENEMIES; i++ )
    {
        if( !sssEnemyIsDead( i ) )
        {
            sssEnemyGetCollisionBox( i, &hb );
            if( gbCollidePointRect( x3, y3, hb.x, hb.y, hb.w, hb.h ) ||
                gbCollidePointRect( x2, y2, hb.x, hb.y, hb.w, hb.h ) ||
                gbCollidePointRect( x1, y1, hb.x, hb.y, hb.w, hb.h ) )
            {
                sssEnemyHit( i );
                return true;
            }
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Player -> sssPlayer*() (a single real instance - plain globals, no index)
// -----------------------------------------------------------------------------

void sssPlayerBegin()
{
    sssPlayerX = 10;
    sssPlayerY = LCDHEIGHT / 2;
    sssPlayerBulletSpeed = 2;
    sssPlayerAbilities = 0;
    sssPlayerLastBulletTime = 5;
    sssPlayerHealth = 6;
}

int* sssHeartBitmapForLevel( int level ) // level is already clamped to 0..2 by the caller
{
    if( level <= 0 ) return sssHeart0Bitmap;
    if( level == 1 ) return sssHeart1Bitmap;
    return sssHeart2Bitmap;
}

void sssPlayerDraw()
{
    if( sssPlayerLastBulletTime < 200 ) sssPlayerLastBulletTime++; // piggy-backs on a function run every frame, matching upstream
    gbDrawBitmap( sssPlayerX - 5, sssPlayerY - 3, sssShipSpriteBitmap );

    // three-heart HUD, each heart showing one of 3 real pip levels -
    // real upstream's own exact gbMax/gbMin clamp formulas, unchanged
    int h = sssPlayerHealth;
    gbDrawBitmap( 11, LCDHEIGHT - 6, sssHeartBitmapForLevel( gbMax( 0, h - 4 ) ) );
    gbDrawBitmap( 6, LCDHEIGHT - 6, sssHeartBitmapForLevel( gbMax( 0, gbMin( h - 2, 2 ) ) ) );
    gbDrawBitmap( 1, LCDHEIGHT - 6, sssHeartBitmapForLevel( gbMax( 0, gbMin( h, 2 ) ) ) );
}

void sssPlayerShoot()
{
    if( sssPlayerLastBulletTime > 5 ) // don't let the player shoot too often
    {
        if( sssPlayerAbilities == 0 )
        {
            sssCreateBullet( sssPlayerX + 1, sssPlayerY, SSS_DIR_E, sssPlayerBulletSpeed, SSS_SRC_PLAYER );
            sssPlayerLastBulletTime = 0;
        }
    }
}

void sssPlayerMoveUp()
{
    if( sssPlayerY > 2 ) sssPlayerY--;
}

void sssPlayerMoveDown()
{
    if( sssPlayerY < LCDHEIGHT - 3 ) sssPlayerY++;
}

void sssPlayerMoveLeft()
{
    if( sssPlayerX >= 5 ) sssPlayerX--;
}

void sssPlayerMoveRight()
{
    if( sssPlayerX < LCDWIDTH - 10 ) sssPlayerX++;
}

void sssPlayerGetCollisionBox( SssHitBox* hb )
{
    hb->x = sssPlayerX - 2;
    hb->y = sssPlayerY - 1;
    hb->w = 3;
    hb->h = 3;
}

// called when the player is hit by an enemy bullet
void sssPlayerHit()
{
    gbPlayCancel();
    sssPlayerHealth--;
    if( sssPlayerHealth < 0 )
      sssReset(); // real upstream's own "temporary" full reset-on-death
}

// -----------------------------------------------------------------------------
// Top-level game state (main.cpp's own setup()/loop()/reset()/drawScore())
// -----------------------------------------------------------------------------

void sssReset()
{
    sssScore = 0;
    sssPlayerBegin();
    sssBulletsReset();
    sssEnemiesReset();
    sssEffectsInit();
}

void sssDrawScore()
{
    gbCursorX = 1;
    gbCursorY = 1;
    gbPrintNumber( sssScore );
}

void sssBeginTitle()
{
    sssState = SSS_STATE_TITLE;
}

void sssBeginPlay()
{
    sssState = SSS_STATE_PLAY;
}

void sssUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, sssLogoBitmap );
    gbCursorX = 1;
    gbCursorY = 32;
    gbPrintString( " SSS by msevilgenius" );
    gbCursorX = 1;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        sssReset();
        sssBeginPlay();
    }
}

void sssUpdatePlay()
{
    // pause the game (real upstream: Button C re-shows the blocking title
    // screen, then resets once it's dismissed - see this file's own
    // header comment)
    if( gbPressed( BTN_C ) )
    {
        sssBeginTitle();
        return;
    }

    // player movement - held continuously, not edge-triggered (see this
    // file's own header comment on gbHeld() replacing upstream's own
    // manually edge-tracked upbtn/dnbtn/lfbtn/rtbtn)
    if( gbHeld( BTN_UP, 1 ) ) sssPlayerMoveUp();
    if( gbHeld( BTN_DOWN, 1 ) ) sssPlayerMoveDown();
    if( gbHeld( BTN_LEFT, 1 ) ) sssPlayerMoveLeft();
    if( gbHeld( BTN_RIGHT, 1 ) ) sssPlayerMoveRight();

    // player shooting - see this file's own header comment on gbHeld()
    // replacing upstream's own `abtn||pressed(BTN_A)` check
    if( gbHeld( BTN_A, 1 ) ) sssPlayerShoot();

    // real upstream's own "temporary way to create enemies for testing"
    // (main.cpp's own comment, verbatim) - the only way any enemy ever
    // appears in this real game, see this file's own header comment
    if( gbPressed( BTN_B ) )
      sssCreateEnemy( LCDWIDTH + 3, 3 + arand( LCDHEIGHT - 6 ), 1 + arand( 3 ) );

    // updating and rendering
    sssEnemiesUpdate();
    sssBulletsUpdateAndDraw();
    sssEffectsUpdate();
    sssPlayerDraw();
    sssDrawScore();
}

void gameSuperSpaceShooter_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 ); // real upstream's own drawScore()-time setFont(font3x5) - see this file's own header comment on why this is set once here rather than every frame
    sssBeginTitle();
}

void gameSuperSpaceShooter_update()
{
    if( !gbUpdate() ) return;

    if( sssState == SSS_STATE_TITLE ) sssUpdateTitle();
    else sssUpdatePlay();

    gbRenderFrame();
}
