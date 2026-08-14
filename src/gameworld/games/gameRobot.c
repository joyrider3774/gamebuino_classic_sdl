#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// =============================================================================
// Robot (Frakasss, license: none specified -
// https://github.com/Frakasss/Robot). A real single-screen-scrolling run-
// and-gun platformer: walk/jump through 5 side-scrolling "worlds" (levels
// 1/5/9/13/17 in the real source's own runningLevel numbering), each ending
// in a boss fight (levels 2-3, 6-7, 10-11, 14-15, 18-19) against a big
// three-part robot boss, shooting 7 real enemy types (larva/ufo/robot/
// rocket/tesla-tower/jumper/ghost) along the way. Every real upstream file
// (Robot.ino, Player.ino, Bullet.ino, Ennemy.ino, Level.ino, Sound.ino,
// Sprites.ino) was read in full before writing this port. Genuinely plain
// .ino/C source throughout - no C++ classes anywhere in this game (verified
// directly), just a handful of plain structs (Player/Bullet/LandscapePlan/
// Ennemy) and free functions, the same shape as every other ported game.
//
// DIALECT REWRITES:
// - Every real `gb.x.y(...)` call site mechanically rewritten to a plain
//   `gbY(...)` call (see gamePong.c's own header comment for why - this
//   dialect has no classes/methods). `gb.display.cursorX =`/`cursorY =`
//   ported unchanged (plain globals here too, `gbCursorX`/`gbCursorY`).
// - Real `and`/`or` (C++ alternative operator tokens, valid in the Arduino
//   IDE's own C++ compiler) rewritten to `&&`/`||` throughout - this
//   dialect only recognizes the symbolic forms.
// - Upstream's real `Player`/`Bullet`/`LandscapePlan`/`Ennemy` structs are
//   ported as named `typedef struct{ ... } RoboXxx;` types (this dialect rejects
//   anonymous `typedef struct{...} Name;`) with real struct-array globals
//   (`RoboEnnemy[15] roboEnnemies;` etc) - proven directly in this project
//   already by gameBomber.c's own `BombBombe[BOMB_NB_BOMBE]
//   bombMasterBombe;`/`BombPlayer*`-parameter pattern, reused here
//   unchanged rather than flattening into parallel arrays.
// - Real `void Init(String tmp)` (branching on `tmp=="Next"`/`tmp=="Prev"`)
//   was split into two real functions, `roboInitNext()`/`roboInitPrev()`,
//   each doing its own real `runningLevel` adjustment before calling a
//   shared `roboInitCommon()` for the rest of the real body - this dialect
//   has no meaningful String/String-equality support, and the two branches
//   were always mutually exclusive anyway (never both true in one real
//   call), so this loses nothing.
// - Scratch loop/temp variables upstream declares as file-scope globals
//   (`for_x`/`for_y`/`check01`/`check02`/`check03`, real AVR `byte`/`int`
//   scratch reused across many functions) are plain LOCAL variables here
//   instead - confirmed by direct inspection that none of them are ever
//   read by a different function than the one that last wrote them, so
//   this is a pure, behavior-preserving cleanup, not a risk.
// - Real 2D PROGMEM sprite tables (`playerSprite[4][10]`, `ennemy01[4][12]`,
//   `landscape[10][14]`, etc) are ported as real 2D `int[R][C]` arrays with
//   a dynamic row index passed straight into `gbDrawBitmap()`
//   (`roboLandscape[ idx ]`) - already proven safe and working in this
//   project by gameArtillery.c's own `artUnitsBitmaps[ artPlayers[i].team
//   ]` call site, so no per-frame if/else dispatch helper was needed here
//   (unlike gameInvaders.c's own more cautious approach, written before
//   that precedent existed). Every real `Bxxxxxxxx` Arduino binary literal
//   was converted to plain decimal by a small one-off Python script reading
//   Sprites.ino directly (not hand-transcribed) - two of upstream's own
//   real sprite tables (`ennemy04b[2][10]`, `boom[2][24]`, `landscape[10]
//   [14]`, `background[1][18]`) declare a wider per-row byte count than
//   some of their own rows actually list explicit values for (real C's own
//   implicit trailing-zero-fill for a short initializer list) - the
//   conversion script reproduced this exactly by zero-padding each such
//   row out to its own real declared width, rather than only emitting the
//   explicitly-listed bytes, since `ennemy04b[1]` (an 8x8 rocket-exhaust
//   frame only given 4 of its own real needed 8 data bytes) is genuinely
//   drawn on screen (`ennemy_draw()`'s own rocket case, `counter%2==1`) and
//   would read past a too-short array otherwise.
//
// A REAL BUG PRESERVED, NOT FIXED: the tesla-tower enemy (type 4) reuses
// its own `x_min`/`x_max` fields as a second, unrelated (x,y) coordinate
// pair in exactly two places - `ennemy_draw()`'s own tower-pole sprite
// (`gb.display.drawBitmap(ennemies[i].x_min-player.x_world, ennemies[i].
// x_max, ennemy05)` - `x_max` used as a Y coordinate) and `bullet_
// EnnemyCollision()`'s own tesla hit-test (`gb.collideRectRect(...,
// ennemies[i].x_min, ennemies[i].x_max, 5, 9)`, which - unlike every other
// enemy type's own hit-test - never subtracts `player.x_world` at all, so
// it compares a bullet's real WORLD x-coordinate directly against a
// STATIC level-relative bound). Both read unmistakably like a real
// upstream copy-paste mistake (every other enemy type keys its own
// draw/hit-test off `.x`/`.y`, never `.x_min`/`.x_max`), not a deliberate
// design - but real Vircon32-hardware behavior for both is well-defined
// (clipped off-screen draws, a hit-test box that rarely lines up with the
// tower's own visible sprite), so neither risks a crash. Ported bit-for-
// bit unmodified, per this project's own "preserve real upstream bugs
// unless they crash/hang/are unplayable" norm - a real player will see the
// tesla tower's own pole graphic misplaced and find it unusually hard to
// hit with a bullet, exactly matching real hardware.
//
// TWO MORE REAL UPSTREAM QUIRKS PRESERVED DELIBERATELY:
// - `player_checkDeath()` (life<=0 or a fall off the bottom of the screen)
//   is only ever called during the two real platforming states
//   (`runningLevel%4==1`) - NEVER during a boss fight (`%4==3`, see
//   `Robot.ino`'s own real `loop()` switch, case 3's own statement list -
//   there is genuinely no `player_checkDeath()` call in it at all). A
//   player's own `life` can go arbitrarily negative during a long, careless
//   boss fight with zero real consequence beyond a wrong-looking (or, once
//   negative, empty/zero-width - `gbFillRect()` with a negative width is a
//   confirmed real no-op here, not a crash) health bar - real hardware has
//   the exact same permissiveness (a genuine "you cannot lose a boss
//   fight" design, not a porting gap).
// - `map_Init()`'s own real levels 5/9/13/17 wave data never sets
//   `ennemies[1..12].anim` at all (only level 1's own 7-enemy wave, and
//   every boss encounter's single `ennemies[0]`, explicitly zero it) -
//   preserved exactly (no implicit reset added), matching real hardware's
//   own identical reliance on whatever `anim` value a previous level left
//   behind in that same array slot (zero-valued the very first time any
//   of those levels is ever reached in a session, matching a fresh global
//   array's own real zero-initialized state on both platforms).
// - Real upstream's own `Sound.ino` defines a real 6-entry `soundfx[6][8]`
//   effects table (with comments literally reading "thrust"/"crash"/
//   "landing success"/"fuel low"/"pick up fuel" - almost certainly copied
//   from a completely different, Lander-style project template) but this
//   game's own real source only ever calls `sound_play()` ONCE in its
//   entirety (the jump sound, `sound_play(5)` in `Player.ino`) - every
//   other entry is real, genuine dead data. Ported faithfully as-is (the
//   full table is kept for fidelity, but no extra call sites were
//   invented) - this shim has no tracker/pattern engine (see
//   `gamebuinoShim.h`'s own header comment), so `roboSoundPlay()`
//   approximates the one real call with a single `gbPlayNote(pitch,
//   duration)` using the table's own real pitch/duration columns
//   (indices [1]/[7]) exactly, matching gameInvaders.c's own established
//   `invPlaySfx()` precedent for the identical real simplification.
//
// STATE MACHINE: real upstream's own `loop()` switches on `runningLevel%4`
// (0=level-intro title card, 1=platforming, 2=boss-approach walk-in,
// 3=boss fight) every real tick - ported here as `roboRunningLevel` (kept
// as the real, literal level-index integer, not just its `%4` remainder,
// since several real functions - `Init()`'s own outer switch, `ennemy_
// init()`, `map_Init()` - all key off the real, exact value, not just its
// remainder) plus a real dispatch `switch(roboRunningLevel % 4)` each tick,
// matching upstream's own shape line-for-line.
//
// TWO REAL BLOCKING-CALL SITES, both handled the same established way this
// project always translates a real blocking `gb.titleScreen()` call (see
// gameInvaders.c's own header comment for the identical pattern, "a real
// BLOCKING call upstream... an early return... matching the clean
// 'blocking loop -> explicit resumable state' treatment... every other
// port here already uses"):
// - Real `setup()`'s own `gb.titleScreen(gamelogo)` (a real one-argument
//   overload - just a bitmap, no name text) becomes `roboShowingSplash`,
//   dismissed by a genuine fresh Button-A press (gated via `roboAGate` -
//   the same cross-game "menu-launch button bleeds into the very first
//   tick" gate every other ported title screen here already needs, since
//   the very same physical A-press that confirmed this game's own menu
//   entry is still being held on this game's own first real tick).
// - Real `fnctn_checkbuttons()`'s own mid-game `if(gb.buttons.pressed(
//   BTN_C)){gb.titleScreen();}` (a real ZERO-argument call - no name, no
//   logo, matching real `Gamebuino::titleScreen()`'s own default
//   parameters) becomes `roboFrozen`: pressing C sets it and
//   `roboFnctnCheckButtons()` returns `false`, which the platforming/boss-
//   fight update functions check right after calling it (matching
//   gameInvaders.c's own `if( invState != INV_STATE_RUNNING ) return;`)
//   to skip the rest of that tick's own drawing, exactly like real
//   hardware skips the rest of that tick's own statements while blocked
//   inside the real call. A blank freeze screen (plus a small "PRESS A"
//   hint text - real hardware's own equivalent blinking-icon hint is
//   dropped, matching gameInvaders.c's own identical, already-established
//   simplification) is shown every tick `roboFrozen` stays true, dismissed
//   by a genuine fresh Button-A press. ONE deliberate, bounded difference
//   from real hardware, inherent to this "resume on a LATER tick" model
//   rather than real hardware's own "resume mid-statement, same tick"
//   blocking call: on real hardware, the very A-press that dismisses this
//   screen is immediately re-read by `fnctn_checkbuttons()`'s own later
//   `gb.buttons.pressed(BTN_A)` fire-bullet check IN THE SAME TICK (both
//   checks see the identical already-latched button state, since no new
//   `gb.update()` runs in between) - a real, if minor, "unfreezing also
//   fires a bullet" quirk. This port's own gameplay only resumes on the
//   NEXT engine tick after the unfreezing press, by which point that
//   physical press has very likely already been released (or, if still
//   held, no longer reads as a fresh `gbPressed()` edge, since that edge
//   was already consumed once) - so the bullet does NOT also fire here.
//   A strictly more forgiving outcome for the player, and an inherent,
//   accepted consequence of this project's own already-established
//   "blocking call -> resumable state, resumed on a later tick"
//   translation, not something invented specifically for this port.
//
// EEPROM: real upstream Robot has no highscore/save concept of any kind
// (no `EEPROM.h` include, no persistent score anywhere in the real
// source) - none was added here either, matching this project's own norm
// of only wiring up persistence a real game actually calls for.
// =============================================================================

#define ROBO_NOROT 0
#define ROBO_ROTCCW 1
#define ROBO_ROT180 2
#define ROBO_ROTCW 3
#define ROBO_NOFLIP 0
#define ROBO_FLIPH 1
#define ROBO_FLIPV 2
#define ROBO_FLIPVH 3

// -----------------------------------------------------------------------------
// Structs - direct ports of the real Robot.ino Player/Bullet/LandscapePlan/
// Ennemy structs (named, not anonymous - this dialect rejects `typedef
// struct{...} Name;`).
// -----------------------------------------------------------------------------

typedef struct{
    int x_screen, y_screen, x_world;
    int jumpStatus, fall, pos, dir;
    int lives, life, cpt;
    int score;
} RoboPlayer;

typedef struct{
    int y_screen, x_world, dir;
} RoboBullet;

typedef struct{
    int x_landscape, y_landscape, type_landscape;
} RoboLandscapePlan;

typedef struct{
    int x, y, counter, x_min, x_max, type, life, dir, anim;
} RoboEnnemy;

// -----------------------------------------------------------------------------
// Globals - direct ports of Robot.ino's own real globals.
// -----------------------------------------------------------------------------

int roboLevelLength;
int roboRunningLevel;
int roboTileNumber;
int roboEnnemiesNumber;

RoboPlayer roboPlayer;
RoboBullet roboBullet[3];
RoboLandscapePlan roboLandscapePlan[100];
RoboEnnemy roboEnnemies[15];

// Real setup()'s own blocking `gb.titleScreen(gamelogo)` - see header
// comment.
bool roboShowingSplash;
bool roboAGate; // suppresses a stale menu-launch A-press, same as every other ported title screen

// Real mid-game `gb.titleScreen()` (Button C) - see header comment.
bool roboFrozen;

// Forward declarations - roboPlayerCheckDeath()/roboPlayerCheckLevelEnd()
// (defined well before these in the Player section) and roboEnnemyMove()'s
// own boss-death branch (Ennemy section) all call these; both are only
// actually defined near the very end of this file (Init/state-transition
// section) - this dialect allows forward declarations for exactly this
// kind of ordering (confirmed in VIRCON32_C_DIALECT.md).
void roboInitNext();
void roboInitPrev();


// -----------------------------------------------------------------------------
// Sprites - see this file's own header comment for how these were derived.
// { width, height, row-major MSB-first packed bytes } - the exact format
// gbDrawBitmap()/gbDrawBitmapRotated() expect.
// -----------------------------------------------------------------------------

int roboGamelogo[282] = { 64, 35, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 160, 24, 0, 0, 0, 0, 0, 0, 64, 24, 0, 0, 0, 0, 0, 0, 64, 32, 0, 0, 0, 0, 0, 0, 62, 64, 0, 0, 0, 0, 0, 0, 255, 128, 0, 0, 0, 0, 0, 1, 255, 192, 6, 96, 0, 0, 0, 3, 255, 224, 15, 240, 0, 0, 0, 3, 224, 16, 31, 248, 0, 0, 0, 3, 204, 208, 29, 184, 112, 24, 0, 3, 213, 80, 28, 56, 211, 204, 0, 3, 217, 144, 56, 29, 246, 94, 0, 3, 224, 16, 56, 29, 133, 214, 0, 3, 255, 240, 56, 28, 247, 158, 0, 1, 255, 224, 0, 0, 3, 128, 0, 0, 255, 192, 0, 0, 3, 128, 0, 3, 254, 0, 0, 0, 31, 0, 0, 15, 255, 143, 192, 7, 220, 0, 0, 31, 127, 247, 160, 14, 192, 0, 0, 61, 255, 239, 16, 12, 199, 0, 0, 123, 255, 239, 16, 31, 143, 178, 0, 127, 248, 239, 16, 24, 217, 182, 0, 119, 240, 119, 160, 49, 219, 166, 0, 124, 96, 15, 192, 63, 143, 60, 0, 63, 176, 0, 0, 0, 0, 12, 0, 17, 188, 0, 0, 0, 0, 24, 0, 62, 126, 0, 0, 0, 0, 48, 0, 255, 255, 0, 0, 7, 255, 224, 1, 255, 255, 128, 0, 0, 0, 0, 1, 248, 15, 128, 0, 0, 0, 0, 3, 224, 3, 128, 0, 0, 0, 0, 28, 224, 3, 224, 0, 0, 0, 0, 63, 64, 7, 252, 0, 0, 0, 0, 127, 128, 15, 254, 0, 0, 0, 0, 127, 0, 15, 254, 0, 0, 0, 0 };
int roboPlayerSprite[4][10] =
{
    { 5, 8, 72, 112, 168, 248, 112, 112, 80, 80 },
    { 5, 8, 72, 112, 168, 248, 112, 112, 80, 64 },
    { 5, 8, 72, 112, 168, 248, 112, 112, 80, 16 },
    { 5, 8, 0, 0, 0, 72, 112, 168, 248, 112 },
};
int roboEnnemy01[4][12] =
{
    { 11, 5, 20, 0, 62, 0, 119, 0, 190, 128, 162, 128 },
    { 11, 5, 10, 0, 31, 0, 123, 128, 191, 64, 1, 64 },
    { 11, 5, 10, 0, 31, 0, 123, 192, 191, 160, 160, 160 },
    { 11, 5, 20, 0, 62, 0, 119, 128, 191, 64, 160, 0 },
};
int roboEnnemy02[7] = { 8, 5, 56, 68, 84, 254, 124 };
int roboEnnemy02b[2][3] =
{
    { 6, 1, 84 },
    { 6, 1, 40 },
};
int roboEnnemy03[6][10] =
{
    { 5, 8, 112, 136, 168, 112, 112, 112, 80, 80 },
    { 5, 8, 112, 136, 168, 112, 112, 112, 80, 16 },
    { 5, 8, 112, 136, 168, 112, 112, 112, 16, 16 },
    { 5, 8, 112, 136, 168, 112, 112, 112, 32, 32 },
    { 5, 8, 112, 136, 168, 112, 112, 112, 64, 64 },
    { 5, 8, 112, 136, 168, 112, 112, 112, 80, 64 },
};
int roboEnnemy04[9] = { 7, 7, 56, 68, 84, 124, 254, 238, 56 };
int roboEnnemy04b[2][10] =
{
    { 5, 4, 136, 136, 80, 32, 0, 0, 0, 0 },
    { 8, 8, 80, 32, 0, 0, 0, 0, 0, 0 },
};
int roboEnnemy05[11] = { 5, 9, 112, 232, 248, 112, 32, 112, 168, 112, 32 };
int roboEnnemy05b[4] = { 4, 2, 224, 112 };
int roboEnnemy06[9] = { 8, 7, 40, 170, 254, 84, 124, 254, 130 };
int roboEnnemy07[8] = { 5, 6, 112, 136, 216, 136, 136, 248 };
int roboEnnemyBossHead[22] = { 16, 10, 2, 0, 2, 0, 63, 224, 127, 240, 127, 240, 127, 240, 64, 48, 100, 240, 127, 240, 51, 48 };
int roboEnnemyBossBody[38] = { 16, 18, 0, 48, 0, 48, 76, 240, 127, 240, 127, 240, 127, 240, 15, 3, 127, 245, 255, 250, 255, 252, 255, 242, 255, 242, 255, 252, 255, 242, 255, 254, 255, 242, 255, 252, 127, 240 };
int roboEnnemyBossFeet[11] = { 6, 9, 120, 252, 204, 204, 252, 220, 252, 236, 252 };
int roboBackground[1][18] =
{
    { 16, 7, 28, 0, 99, 0, 128, 188, 128, 66, 136, 1, 64, 1, 63, 254, 0, 0 },
};
int roboLandscape[10][14] =
{
    { 24, 4, 255, 255, 255, 255, 255, 255, 170, 170, 170, 85, 85, 85 },
    { 24, 4, 255, 255, 255, 40, 162, 138, 69, 20, 81, 255, 255, 255 },
    { 24, 4, 255, 255, 255, 36, 146, 73, 73, 36, 146, 255, 255, 255 },
    { 24, 4, 255, 255, 255, 255, 255, 255, 73, 36, 146, 182, 219, 109 },
    { 24, 4, 255, 255, 255, 187, 187, 187, 255, 255, 255, 255, 255, 255 },
    { 8, 8, 255, 187, 255, 255, 255, 187, 255, 255, 0, 0, 0, 0 },
    { 8, 8, 15, 11, 15, 15, 255, 187, 255, 255, 0, 0, 0, 0 },
    { 8, 8, 240, 176, 240, 240, 255, 187, 255, 255, 0, 0, 0, 0 },
    { 8, 4, 126, 207, 191, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 4, 4, 240, 176, 240, 240, 0, 0, 0, 0, 0, 0, 0, 0 },
};
int roboHeart[7] = { 5, 5, 216, 248, 248, 112, 32 };
int roboBoom[2][24] =
{
    { 7, 7, 40, 84, 186, 124, 186, 84, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 11, 11, 10, 0, 64, 64, 21, 0, 42, 128, 159, 32, 46, 128, 159, 32, 42, 128, 21, 0, 64, 64, 10, 0 },
};


// -----------------------------------------------------------------------------
// Sound - see this file's own header comment. Only soundfx[5] is ever
// actually used by real upstream (the jump sound) - the rest of this real
// table is genuine dead data, kept for fidelity.
// -----------------------------------------------------------------------------

int roboSoundfx[48] = {
    1, 17, 53, 0, 7, 0, 2, 3,
    1, 17, 53, 0, 7, 0, 10, 3,
    1, 26, 41, 1, 1, 3, 7, 20,
    0, 0, 42, 1, 1, 2, 7, 20,
    0, 54, 0, 0, 0, 0, 7, 1,
    0, 0, 65, 1, 1, 1, 7, 5,
};

void roboSoundPlay( int fxno )
{
    int pitch = roboSoundfx[ fxno * 8 + 1 ];
    int duration = roboSoundfx[ fxno * 8 + 7 ];
    gbPlayNote( pitch, duration );
}

// -----------------------------------------------------------------------------
// Player (Player.ino)
// -----------------------------------------------------------------------------

void roboPlayerInit()
{
    roboPlayer.x_screen = 30;
    roboPlayer.y_screen = 30;
    roboPlayer.x_world = 0;
    roboPlayer.jumpStatus = 0;
    roboPlayer.fall = 0;
    roboPlayer.pos = 0;
    roboPlayer.dir = 1;
    roboPlayer.cpt = 0;
}

void roboPlayerSetAnimation()
{
    if( roboPlayer.pos != 3 )
    {
        if( gbPressed( BTN_B ) )
          roboPlayer.pos = 0;

        if( gbRepeat( BTN_RIGHT, 0 ) || gbRepeat( BTN_LEFT, 0 ) )
        {
            if( roboPlayer.pos == 2 )
              roboPlayer.pos = 1;
            else
              roboPlayer.pos = 2;
        }
    }
}

void roboPlayerJump()
{
    if( gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 1 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 1 ) == 0 )
      roboPlayer.jumpStatus = 5;

    if( roboPlayer.jumpStatus == 5 && gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 2 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 2 ) == 0 )
      roboPlayer.jumpStatus = 6;

    if( roboPlayer.jumpStatus == 6 && gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 4 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 4 ) == 0
     && gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 3 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 3 ) == 0 )
      roboPlayer.jumpStatus = 7;

    if( roboPlayer.jumpStatus == 7 && gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 6 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 6 ) == 0
     && gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 5 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 5 ) == 0 )
      roboPlayer.jumpStatus = 8;
}

void roboPlayerMove( int dist, int dir )
{
    int check01 = 0;
    int check02 = 0;
    int check03 = 0;
    int forY, forX;

    if( roboPlayer.pos == 3 ) check01 = 3;

    if( roboPlayer.jumpStatus > 4 )
    {
        if( roboPlayer.jumpStatus == 8 ) check01 = check01 - 6;
        else if( roboPlayer.jumpStatus == 7 ) check01 = check01 - 4;
        else if( roboPlayer.jumpStatus == 6 ) check01 = check01 - 2;
        else if( roboPlayer.jumpStatus == 5 ) check01 = check01 - 1;
    }

    if( dir == 2 && roboPlayer.x_screen < 30 ) check02 = 0;
    if( dir == 0 && roboPlayer.x_world > 1 ) check02 = 1;

    if( dir == 2 && roboPlayer.x_screen >= 30 && roboPlayer.x_world + 30 < roboLevelLength - 44 ) check02 = 1;
    if( dir == 0 && roboPlayer.x_screen >= 32 && roboPlayer.x_world + 32 < roboLevelLength - 44 ) check02 = 1;

    if( dir == 2 && roboPlayer.x_world + 30 >= roboLevelLength - 50 ) check02 = 2;
    if( dir == 0 && roboPlayer.x_screen > 30 ) check02 = 0;

    if( dir == 0 && roboPlayer.x_world > roboLevelLength - 110 ) check02 = 0;

    forY = 0;
    if( roboPlayer.y_screen + 1 + check01 > 0 && roboPlayer.y_screen + 1 + check01 < 60 )
      forY = roboPlayer.y_screen + 1 + check01;
    if( roboPlayer.y_screen + 8 + roboPlayer.fall > 0 && roboPlayer.y_screen + 8 + roboPlayer.fall < 60 )
    {
        for( forX = forY; forX < roboPlayer.y_screen + 8; forX = forX + 1 )
        {
            if( gbGetPixel( roboPlayer.x_screen + ( dist * ( dir - 1 ) ) + ( dir * 2 ) - ( dir - 1 ), forX ) == 1 ) check03 = 1;
            if( gbGetPixel( roboPlayer.x_screen + ( dist * ( dir - 1 ) ) + ( dir * 2 ), forX ) == 1 ) check03 = 1;
        }
    }

    if( check03 == 0 && roboPlayer.x_screen + ( dist * ( dir - 1 ) ) >= 0 && roboPlayer.x_world < roboLevelLength )
    {
        if( check02 == 0 ) roboPlayer.x_screen = roboPlayer.x_screen + dist * ( dir - 1 );
        else if( check02 == 1 ) roboPlayer.x_world = roboPlayer.x_world + dist * ( dir - 1 );
        else if( check02 == 2 ) roboPlayer.x_screen = roboPlayer.x_screen + dist * ( dir - 1 );
    }
    else
    {
        if( dist == 2 ) roboPlayerMove( 1, dir );
    }
}

void roboFnctnSetBullet()
{
    int forY;
    if( roboPlayer.pos == 3 ) forY = 5;
    else forY = 2;

    if( roboBullet[ 0 ].dir == 10 )
    {
        roboBullet[ 0 ].y_screen = roboPlayer.y_screen + forY;
        roboBullet[ 0 ].x_world = roboPlayer.x_world + roboPlayer.x_screen + ( 3 * roboPlayer.dir );
        roboBullet[ 0 ].dir = roboPlayer.dir + roboPlayer.dir;
    }
    else if( roboBullet[ 1 ].dir == 10 )
    {
        roboBullet[ 1 ].y_screen = roboPlayer.y_screen + forY;
        roboBullet[ 1 ].x_world = roboPlayer.x_world + roboPlayer.x_screen + ( 3 * roboPlayer.dir );
        roboBullet[ 1 ].dir = roboPlayer.dir + roboPlayer.dir;
    }
    else if( roboBullet[ 2 ].dir == 10 )
    {
        roboBullet[ 2 ].y_screen = roboPlayer.y_screen + forY;
        roboBullet[ 2 ].x_world = roboPlayer.x_world + roboPlayer.x_screen + ( 3 * roboPlayer.dir );
        roboBullet[ 2 ].dir = roboPlayer.dir + roboPlayer.dir;
    }
}

// Real fnctn_checkbuttons() - returns false the instant this tick hits the
// real blocking mid-game titleScreen() call (Button C), matching
// gameInvaders.c's own established "blocking call -> early return" shape
// (see this file's own header comment).
bool roboFnctnCheckButtons()
{
    roboPlayerSetAnimation();

    if( gbPressed( BTN_C ) )
    {
        roboFrozen = true;
        return false;
    }

    if( roboPlayer.cpt < 15 )
    {
        if( gbRepeat( BTN_DOWN, 0 ) )
          roboPlayer.pos = 3;
        else
        {
            if( roboPlayer.pos == 3 )
            {
                if( gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen ) == 1 || gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen ) == 1 )
                {
                    // real upstream empty branch - stay crouched, something is overhead
                }
                else
                  roboPlayer.pos = 0;
            }
        }

        if( gbPressed( BTN_B ) )
        {
            if( roboPlayer.pos != 3 )
            {
                if( roboPlayer.jumpStatus == 0 )
                {
                    roboPlayerJump();
                    roboSoundPlay( 5 );
                }
            }
        }

        if( gbRepeat( BTN_RIGHT, 0 ) )
        {
            if( roboPlayer.dir == 0 )
              roboPlayer.dir = 1;
            else
            {
                if( roboPlayer.pos == 3 ) roboPlayerMove( 1, 2 );
                else roboPlayerMove( 2, 2 );
            }
        }
        else if( gbRepeat( BTN_LEFT, 0 ) )
        {
            if( roboPlayer.dir == 1 )
              roboPlayer.dir = 0;
            else
            {
                if( roboPlayer.pos == 3 ) roboPlayerMove( 1, 0 );
                else roboPlayerMove( 2, 0 );
            }
        }
        else
        {
            if( roboPlayer.pos != 3 ) roboPlayer.pos = 0;
        }

        if( gbPressed( BTN_A ) )
          roboFnctnSetBullet();
    }
    else
    {
        if( roboPlayer.dir == 1 ) roboPlayerMove( 1, 2 );
        else roboPlayerMove( 1, 0 );
    }

    return true;
}

void roboPlayerCheckEnnemyCollision()
{
    int forX;
    int tmp;

    if( roboPlayer.cpt > 0 ) roboPlayer.cpt = roboPlayer.cpt - 1;

    if( roboPlayer.cpt < 5 )
    {
        for( forX = 0; forX < roboEnnemiesNumber; forX = forX + 1 )
        {
            if( roboEnnemies[ forX ].life > 0 )
            {
                tmp = 0;
                if( roboPlayer.pos == 3 ) tmp = 3;

                if( roboEnnemies[ forX ].type == 0 ) // larva
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 2, roboEnnemies[ forX ].y + 1, 7, 4 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                        if( roboPlayer.x_screen < roboEnnemies[ forX ].x - roboPlayer.x_world + 6 )
                        {
                            roboPlayer.dir = 0; roboPlayerMove( 2, 0 );
                        }
                        else
                        {
                            roboPlayer.dir = 1; roboPlayerMove( 2, 2 );
                        }
                    }
                }
                else if( roboEnnemies[ forX ].type == 1 ) // ufo
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 1, 5, 5 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                        if( roboEnnemies[ forX ].dir == 0 )
                        {
                            roboPlayer.dir = 0; roboPlayerMove( 2, 0 );
                        }
                        else
                        {
                            roboPlayer.dir = 1; roboPlayerMove( 2, 2 );
                        }
                    }
                }
                else if( roboEnnemies[ forX ].type == 2 ) // robot2
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 1, 3, 5 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.dir == 1 ) { roboPlayer.dir = 0; roboPlayerMove( 2, 0 ); }
                        else { roboPlayer.dir = 1; roboPlayerMove( 2, 2 ); }
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                    }
                }
                else if( roboEnnemies[ forX ].type == 3 ) // rocket
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 1, 5, 5 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                        if( roboEnnemies[ forX ].dir == 0 )
                        {
                            roboPlayer.dir = 0; roboPlayerMove( 2, 0 );
                        }
                        else
                        {
                            roboPlayer.dir = 1; roboPlayerMove( 2, 2 );
                        }
                    }
                }
                else if( roboEnnemies[ forX ].type == 4 ) // tesla tower
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 1, 3, 2 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                        if( roboEnnemies[ forX ].dir == 0 )
                        {
                            roboPlayer.dir = 0; roboPlayerMove( 2, 0 );
                        }
                        else
                        {
                            roboPlayer.dir = 1; roboPlayerMove( 2, 2 );
                        }
                    }
                }
                else if( roboEnnemies[ forX ].type == 5 ) // jumper
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 1, 5, 6 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                        if( roboEnnemies[ forX ].dir == 0 )
                        {
                            roboPlayer.dir = 0; roboPlayerMove( 2, 0 );
                        }
                        else
                        {
                            roboPlayer.dir = 1; roboPlayerMove( 2, 2 );
                        }
                    }
                }
                else if( roboEnnemies[ forX ].type == 6 ) // ghost
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 1, 3, 4 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                        if( roboEnnemies[ forX ].dir == 0 )
                        {
                            roboPlayer.dir = 0; roboPlayerMove( 2, 0 );
                        }
                        else
                        {
                            roboPlayer.dir = 1; roboPlayerMove( 2, 2 );
                        }
                    }
                }
                else if( roboEnnemies[ forX ].type == 10 ) // boss
                {
                    if( gbCollideRectRect( roboPlayer.x_screen + 1, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 3, 10, 11 )
                     || gbCollideRectRect( roboPlayer.x_screen + 2, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world + 0, roboEnnemies[ forX ].y + 14, 12, 11 )
                     || gbCollideRectRect( roboPlayer.x_screen + 2, roboPlayer.y_screen + 1 + tmp, 3, 6, roboEnnemies[ forX ].x - roboPlayer.x_world - 1, roboEnnemies[ forX ].y + 25, 14, 7 ) )
                    {
                        roboPlayerJump();
                        if( roboPlayer.cpt == 0 ) roboPlayer.life = roboPlayer.life - 1;
                        roboPlayer.cpt = 20;
                        if( roboEnnemies[ forX ].dir == 0 )
                        {
                            roboPlayer.dir = 0; roboPlayerMove( 2, 0 );
                        }
                        else
                        {
                            roboPlayer.dir = 1; roboPlayerMove( 2, 2 );
                        }
                    }
                }
            }
            else
            {
                if( roboEnnemies[ forX ].counter > 0 )
                  roboEnnemies[ forX ].counter = roboEnnemies[ forX ].counter - 1;
            }
        }
    }
}

// Real player_checkDeath() - only ever called during the platforming
// state (see this file's own header comment).
void roboPlayerCheckDeath()
{
    if( ( roboPlayer.y_screen > 48 && roboPlayer.y_screen < 100 ) || ( roboPlayer.life == 0 ) )
      roboInitPrev();
}

void roboPlayerCheckLevelEnd()
{
    if( roboPlayer.x_screen + roboPlayer.x_world > roboLevelLength - 54 )
      roboInitNext();
}

void roboPlayerCheckJump()
{
    if( roboPlayer.jumpStatus == 8 )
      roboPlayer.y_screen = roboPlayer.y_screen - 6;
    else if( roboPlayer.jumpStatus == 7 )
    {
        if( gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 4 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 4 ) == 0 && gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 3 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 3 ) == 0 )
          roboPlayer.y_screen = roboPlayer.y_screen - 4;
        else
          roboPlayer.jumpStatus = 6;
    }
    else if( roboPlayer.jumpStatus == 6 )
    {
        if( gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 2 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 2 ) == 0 )
          roboPlayer.y_screen = roboPlayer.y_screen - 2;
        else
          roboPlayer.jumpStatus = 5;
    }
    else if( roboPlayer.jumpStatus == 5 )
    {
        if( gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen - 1 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen - 1 ) == 0 )
          roboPlayer.y_screen = roboPlayer.y_screen - 1;
        else
          roboPlayer.jumpStatus = 4;
    }
    if( roboPlayer.jumpStatus > 1 ) roboPlayer.jumpStatus = roboPlayer.jumpStatus - 1;
}

void roboPlayerCheckVerticalPos()
{
    int forX;

    if( roboPlayer.fall == 0 && roboPlayer.pos == 3 && gbGetPixel( roboPlayer.x_screen + 2, roboPlayer.y_screen + 7 ) == 1 )
      roboPlayer.y_screen = roboPlayer.y_screen - 1;

    if( gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen + 8 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen + 8 ) == 0 )
    {
        roboPlayer.y_screen = roboPlayer.y_screen + 1;
        if( roboPlayer.fall < 4 ) roboPlayer.fall = roboPlayer.fall + 1;
        for( forX = 0; forX < roboPlayer.fall; forX = forX + 1 )
        {
            if( gbGetPixel( roboPlayer.x_screen, roboPlayer.y_screen + 8 ) == 0 && gbGetPixel( roboPlayer.x_screen + 4, roboPlayer.y_screen + 8 ) == 0 )
              roboPlayer.y_screen = roboPlayer.y_screen + 1;
        }
    }
    else
    {
        roboPlayer.fall = 0;
        roboPlayer.jumpStatus = 0;
    }
}

void roboPlayerDrawHud()
{
    gbCursorX = 6;
    gbPrintString( "S" );
    gbPrintNumber( roboPlayer.score );

    gbDrawBitmap( 35, 0, roboHeart );
    gbDrawRect( 42, 0, 14, 5 );
    gbFillRect( 43, 1, roboPlayer.life * 2, 3 );

    gbDrawBitmap( 69, -3, roboPlayerSprite[ 3 ] );
    gbCursorX = 75;
    gbPrintNumber( roboPlayer.lives );
}

void roboPlayerDraw()
{
    if( roboPlayer.cpt % 2 == 0 )
    {
        if( roboPlayer.dir == 0 )
          gbDrawBitmapRotated( roboPlayer.x_screen, roboPlayer.y_screen, roboPlayerSprite[ roboPlayer.pos ], ROBO_NOROT, ROBO_FLIPH );
        else
          gbDrawBitmapRotated( roboPlayer.x_screen, roboPlayer.y_screen, roboPlayerSprite[ roboPlayer.pos ], ROBO_NOROT, ROBO_NOFLIP );
    }
}

// -----------------------------------------------------------------------------
// Bullet (Bullet.ino)
// -----------------------------------------------------------------------------

void roboBulletInit()
{
    roboBullet[ 0 ].y_screen = 0; roboBullet[ 0 ].x_world = 0; roboBullet[ 0 ].dir = 10;
    roboBullet[ 1 ].y_screen = 0; roboBullet[ 1 ].x_world = 0; roboBullet[ 1 ].dir = 10;
    roboBullet[ 2 ].y_screen = 0; roboBullet[ 2 ].x_world = 0; roboBullet[ 2 ].dir = 10;
}

void roboBulletEnnemyCollision( int tmp )
{
    int forY;
    for( forY = 0; forY < roboEnnemiesNumber; forY = forY + 1 )
    {
        if( roboEnnemies[ forY ].life > 0 && roboEnnemies[ forY ].anim == 0 )
        {
            if( roboEnnemies[ forY ].type == 0 ) // larva
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x, roboEnnemies[ forY ].y, 11, 5 ) )
                {
                    roboEnnemies[ forY ].life = roboEnnemies[ forY ].life - 1;
                    roboEnnemies[ forY ].anim = 10;
                    if( roboEnnemies[ forY ].life == 0 ) roboPlayer.score = roboPlayer.score + 25;
                }
            }
            else if( roboEnnemies[ forY ].type == 1 ) // ufo
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x, roboEnnemies[ forY ].y, 7, 5 ) )
                {
                    roboEnnemies[ forY ].life = roboEnnemies[ forY ].life - 1;
                    roboEnnemies[ forY ].anim = 10;
                    if( roboEnnemies[ forY ].life == 0 ) roboPlayer.score = roboPlayer.score + 50;
                }
            }
            else if( roboEnnemies[ forY ].type == 2 ) // robot
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x, roboEnnemies[ forY ].y, 5, 8 ) )
                {
                    roboEnnemies[ forY ].life = roboEnnemies[ forY ].life - 1;
                    roboEnnemies[ forY ].anim = 10;
                    if( roboEnnemies[ forY ].life == 0 ) roboPlayer.score = roboPlayer.score + 30;
                }
            }
            else if( roboEnnemies[ forY ].type == 3 ) // rocket
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x - 1, roboEnnemies[ forY ].y, 9, 7 ) )
                {
                    roboEnnemies[ forY ].life = roboEnnemies[ forY ].life - 1;
                    roboEnnemies[ forY ].anim = 10;
                    if( roboEnnemies[ forY ].life == 0 ) roboPlayer.score = roboPlayer.score + 30;
                }
            }
            else if( roboEnnemies[ forY ].type == 4 ) // tesla - real upstream bug: x_min/x_max reused as a static (x,y), no player.x_world subtracted (see header comment)
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x_min, roboEnnemies[ forY ].x_max, 5, 9 ) )
                {
                    roboEnnemies[ forY ].life = roboEnnemies[ forY ].life - 1;
                    roboEnnemies[ forY ].anim = 10;
                    if( roboEnnemies[ forY ].life == 0 ) roboPlayer.score = roboPlayer.score + 25;
                }
            }
            else if( roboEnnemies[ forY ].type == 5 ) // jumper
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x, roboEnnemies[ forY ].y, 7, 7 ) )
                {
                    roboEnnemies[ forY ].life = roboEnnemies[ forY ].life - 1;
                    roboEnnemies[ forY ].anim = 10;
                    if( roboEnnemies[ forY ].life == 0 ) roboPlayer.score = roboPlayer.score + 50;
                }
            }
            else if( roboEnnemies[ forY ].type == 6 ) // ghost - knocked back, not damaged
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x, roboEnnemies[ forY ].y, 5, 7 ) )
                {
                    if( roboBullet[ tmp ].dir == 0 ) roboEnnemies[ forY ].x = roboEnnemies[ forY ].x - 5;
                    else roboEnnemies[ forY ].x = roboEnnemies[ forY ].x + 5;
                    roboEnnemies[ forY ].anim = 10;
                }
            }
            else if( roboEnnemies[ forY ].type == 10 ) // boss
            {
                if( gbCollideRectRect( roboBullet[ tmp ].x_world, roboBullet[ tmp ].y_screen, 2, 2, roboEnnemies[ forY ].x - 2 + ( gbAbsInt( ( roboEnnemies[ forY ].dir - 2 ) / 2 ) * 13 ), roboEnnemies[ forY ].y + 14, 5, 12 ) )
                {
                    roboEnnemies[ forY ].life = roboEnnemies[ forY ].life - 1;
                    roboEnnemies[ forY ].anim = 20;
                    if( roboEnnemies[ forY ].life == 0 ) roboPlayer.score = roboPlayer.score + 150;
                }
            }
        }
    }
}

void roboBulletMove()
{
    int forX;
    for( forX = 0; forX < 3; forX = forX + 1 )
    {
        if( roboBullet[ forX ].dir != 10 )
        {
            roboBullet[ forX ].x_world = roboBullet[ forX ].x_world + ( 3 * ( roboBullet[ forX ].dir - 1 ) );

            if( roboBullet[ forX ].x_world - roboPlayer.x_world < 0 || roboBullet[ forX ].x_world - roboPlayer.x_world > 84 )
              roboBullet[ forX ].dir = 10;

            if( gbGetPixel( roboBullet[ forX ].x_world - roboPlayer.x_world, roboBullet[ forX ].y_screen ) || gbGetPixel( roboBullet[ forX ].x_world - roboPlayer.x_world + 1, roboBullet[ forX ].y_screen ) )
            {
                roboBulletEnnemyCollision( forX );
                roboBullet[ forX ].dir = 10;
            }
        }
    }
}

void roboBulletDraw()
{
    int forX;
    for( forX = 0; forX < 3; forX = forX + 1 )
    {
        if( roboBullet[ forX ].dir != 10 )
          gbFillRect( roboBullet[ forX ].x_world - roboPlayer.x_world, roboBullet[ forX ].y_screen, 2, 2 );
    }
}

// -----------------------------------------------------------------------------
// Ennemy (Ennemy.ino)
// -----------------------------------------------------------------------------

void roboEnnemyInit()
{
    if( roboRunningLevel == 1 )
    {
    roboEnnemies[0].x = 80;
    roboEnnemies[0].y = 39;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 20;
    roboEnnemies[0].x_max = 90;
    roboEnnemies[0].type = 0;
    roboEnnemies[0].life = 3;
    roboEnnemies[0].dir = 2;
    roboEnnemies[0].anim = 0;
    roboEnnemies[1].x = 100;
    roboEnnemies[1].y = 16;
    roboEnnemies[1].counter = 0;
    roboEnnemies[1].x_min = 100;
    roboEnnemies[1].x_max = 170;
    roboEnnemies[1].type = 1;
    roboEnnemies[1].life = 1;
    roboEnnemies[1].dir = 2;
    roboEnnemies[1].anim = 0;
    roboEnnemies[2].x = 180;
    roboEnnemies[2].y = 36;
    roboEnnemies[2].counter = 0;
    roboEnnemies[2].x_min = 180;
    roboEnnemies[2].x_max = 230;
    roboEnnemies[2].type = 2;
    roboEnnemies[2].life = 1;
    roboEnnemies[2].dir = 2;
    roboEnnemies[2].anim = 0;
    roboEnnemies[3].x = 240;
    roboEnnemies[3].y = 26;
    roboEnnemies[3].counter = 0;
    roboEnnemies[3].x_min = 10;
    roboEnnemies[3].x_max = 60;
    roboEnnemies[3].type = 3;
    roboEnnemies[3].life = 1;
    roboEnnemies[3].dir = 0;
    roboEnnemies[3].anim = 0;
    roboEnnemies[4].x = 290;
    roboEnnemies[4].y = 36;
    roboEnnemies[4].counter = 0;
    roboEnnemies[4].x_min = 300;
    roboEnnemies[4].x_max = 35;
    roboEnnemies[4].type = 4;
    roboEnnemies[4].life = 1;
    roboEnnemies[4].dir = 0;
    roboEnnemies[4].anim = 0;
    roboEnnemies[5].x = 460;
    roboEnnemies[5].y = 37;
    roboEnnemies[5].counter = 0;
    roboEnnemies[5].x_min = 460;
    roboEnnemies[5].x_max = 530;
    roboEnnemies[5].type = 5;
    roboEnnemies[5].life = 1;
    roboEnnemies[5].dir = 0;
    roboEnnemies[5].anim = 0;
    roboEnnemies[6].x = 540;
    roboEnnemies[6].y = 16;
    roboEnnemies[6].counter = 0;
    roboEnnemies[6].x_min = 540;
    roboEnnemies[6].x_max = 610;
    roboEnnemies[6].type = 6;
    roboEnnemies[6].life = 1;
    roboEnnemies[6].dir = 0;
    roboEnnemies[6].anim = 0;
    }
    else if( roboRunningLevel == 2 )
    {
    roboEnnemies[0].x = 100;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 65;
    roboEnnemies[0].x_max = 48;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 5;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 3 )
    {
    roboEnnemies[0].x = 65;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 13;
    roboEnnemies[0].x_max = 62;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 5;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 5 || roboRunningLevel == 9 || roboRunningLevel == 13 || roboRunningLevel == 17 )
    {
        // real upstream: cases 5/9/13/17 are byte-identical wave layouts
        // (verified directly) - and, unlike case 1's own wave, never set
        // .anim for ennemies[1..12] at all (see this file's own header
        // comment) - preserved exactly, not defaulted to 0 here.
    roboEnnemies[0].x = 20;
    roboEnnemies[0].y = 39;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 20;
    roboEnnemies[0].x_max = 96;
    roboEnnemies[0].type = 0;
    roboEnnemies[0].life = 1;
    roboEnnemies[0].dir = 2;
    roboEnnemies[1].x = 108;
    roboEnnemies[1].y = 16;
    roboEnnemies[1].counter = 0;
    roboEnnemies[1].x_min = 108;
    roboEnnemies[1].x_max = 160;
    roboEnnemies[1].type = 1;
    roboEnnemies[1].life = 1;
    roboEnnemies[1].dir = 2;
    roboEnnemies[2].x = 168;
    roboEnnemies[2].y = 39;
    roboEnnemies[2].counter = 0;
    roboEnnemies[2].x_min = 168;
    roboEnnemies[2].x_max = 228;
    roboEnnemies[2].type = 0;
    roboEnnemies[2].life = 1;
    roboEnnemies[2].dir = 2;
    roboEnnemies[3].x = 240;
    roboEnnemies[3].y = 27;
    roboEnnemies[3].counter = 0;
    roboEnnemies[3].x_min = 240;
    roboEnnemies[3].x_max = 292;
    roboEnnemies[3].type = 0;
    roboEnnemies[3].life = 1;
    roboEnnemies[3].dir = 2;
    roboEnnemies[4].x = 352;
    roboEnnemies[4].y = 16;
    roboEnnemies[4].counter = 0;
    roboEnnemies[4].x_min = 352;
    roboEnnemies[4].x_max = 408;
    roboEnnemies[4].type = 1;
    roboEnnemies[4].life = 1;
    roboEnnemies[4].dir = 2;
    roboEnnemies[5].x = 376;
    roboEnnemies[5].y = 36;
    roboEnnemies[5].counter = 0;
    roboEnnemies[5].x_min = 376;
    roboEnnemies[5].x_max = 432;
    roboEnnemies[5].type = 2;
    roboEnnemies[5].life = 1;
    roboEnnemies[5].dir = 2;
    roboEnnemies[6].x = 424;
    roboEnnemies[6].y = 24;
    roboEnnemies[6].counter = 0;
    roboEnnemies[6].x_min = 424;
    roboEnnemies[6].x_max = 468;
    roboEnnemies[6].type = 2;
    roboEnnemies[6].life = 1;
    roboEnnemies[6].dir = 2;
    roboEnnemies[7].x = 432;
    roboEnnemies[7].y = 36;
    roboEnnemies[7].counter = 0;
    roboEnnemies[7].x_min = 432;
    roboEnnemies[7].x_max = 488;
    roboEnnemies[7].type = 2;
    roboEnnemies[7].life = 1;
    roboEnnemies[7].dir = 2;
    roboEnnemies[8].x = 436;
    roboEnnemies[8].y = 12;
    roboEnnemies[8].counter = 0;
    roboEnnemies[8].x_min = 436;
    roboEnnemies[8].x_max = 456;
    roboEnnemies[8].type = 2;
    roboEnnemies[8].life = 1;
    roboEnnemies[8].dir = 2;
    roboEnnemies[9].x = 516;
    roboEnnemies[9].y = 39;
    roboEnnemies[9].counter = 0;
    roboEnnemies[9].x_min = 516;
    roboEnnemies[9].x_max = 552;
    roboEnnemies[9].type = 0;
    roboEnnemies[9].life = 1;
    roboEnnemies[9].dir = 2;
    roboEnnemies[10].x = 600;
    roboEnnemies[10].y = 27;
    roboEnnemies[10].counter = 0;
    roboEnnemies[10].x_min = 600;
    roboEnnemies[10].x_max = 648;
    roboEnnemies[10].type = 0;
    roboEnnemies[10].life = 1;
    roboEnnemies[10].dir = 2;
    roboEnnemies[11].x = 680;
    roboEnnemies[11].y = 12;
    roboEnnemies[11].counter = 0;
    roboEnnemies[11].x_min = 680;
    roboEnnemies[11].x_max = 784;
    roboEnnemies[11].type = 1;
    roboEnnemies[11].life = 1;
    roboEnnemies[11].dir = 2;
    roboEnnemies[12].x = 748;
    roboEnnemies[12].y = 36;
    roboEnnemies[12].counter = 0;
    roboEnnemies[12].x_min = 748;
    roboEnnemies[12].x_max = 784;
    roboEnnemies[12].type = 2;
    roboEnnemies[12].life = 1;
    roboEnnemies[12].dir = 2;
    }
    else if( roboRunningLevel == 6 )
    {
    roboEnnemies[0].x = 100;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 65;
    roboEnnemies[0].x_max = 48;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 1;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 7 )
    {
    roboEnnemies[0].x = 65;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 13;
    roboEnnemies[0].x_max = 62;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 5;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 10 )
    {
    roboEnnemies[0].x = 100;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 65;
    roboEnnemies[0].x_max = 48;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 1;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 11 )
    {
    roboEnnemies[0].x = 65;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 13;
    roboEnnemies[0].x_max = 62;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 5;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 14 )
    {
    roboEnnemies[0].x = 100;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 65;
    roboEnnemies[0].x_max = 48;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 1;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 15 )
    {
    roboEnnemies[0].x = 65;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 13;
    roboEnnemies[0].x_max = 62;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 5;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 18 )
    {
    roboEnnemies[0].x = 100;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 65;
    roboEnnemies[0].x_max = 48;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 1;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
    else if( roboRunningLevel == 19 )
    {
    roboEnnemies[0].x = 65;
    roboEnnemies[0].y = 12;
    roboEnnemies[0].counter = 0;
    roboEnnemies[0].x_min = 13;
    roboEnnemies[0].x_max = 62;
    roboEnnemies[0].type = 10;
    roboEnnemies[0].life = 5;
    roboEnnemies[0].dir = 0;
    roboEnnemies[0].anim = 0;
    }
}

// Real ennemy_move() - upstream's own per-type animation/movement.
void roboEnnemyMove()
{
    int forX;
    for( forX = 0; forX < roboEnnemiesNumber; forX = forX + 1 )
    {
        if( roboEnnemies[ forX ].life > 0 && roboEnnemies[ forX ].anim == 0 )
        {
            if( roboEnnemies[ forX ].type == 0 ) // larva
            {
                roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 8;
                if( ( roboEnnemies[ forX ].counter / 2 == 1 || roboEnnemies[ forX ].counter / 2 == 2 ) && roboEnnemies[ forX ].counter % 2 == 0 )
                  roboEnnemies[ forX ].x = roboEnnemies[ forX ].x + roboEnnemies[ forX ].dir - 1;
                if( roboEnnemies[ forX ].x < roboEnnemies[ forX ].x_min ) roboEnnemies[ forX ].dir = 2;
                if( roboEnnemies[ forX ].x > roboEnnemies[ forX ].x_max ) roboEnnemies[ forX ].dir = 0;
            }
            else if( roboEnnemies[ forX ].type == 1 ) // ufo
            {
                roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 12;
                roboEnnemies[ forX ].x = roboEnnemies[ forX ].x + roboEnnemies[ forX ].dir - 1;
                if( roboEnnemies[ forX ].counter < 6 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 1;
                if( roboEnnemies[ forX ].counter >= 6 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 1;
                if( roboEnnemies[ forX ].x < roboEnnemies[ forX ].x_min ) roboEnnemies[ forX ].dir = 2;
                if( roboEnnemies[ forX ].x > roboEnnemies[ forX ].x_max ) roboEnnemies[ forX ].dir = 0;
            }
            else if( roboEnnemies[ forX ].type == 2 ) // robot2
            {
                roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 6;
                if( roboEnnemies[ forX ].counter == 3 || roboEnnemies[ forX ].counter == 4 )
                  roboEnnemies[ forX ].x = roboEnnemies[ forX ].x + roboEnnemies[ forX ].dir - 1;
                if( roboEnnemies[ forX ].x < roboEnnemies[ forX ].x_min ) roboEnnemies[ forX ].dir = 2;
                if( roboEnnemies[ forX ].x > roboEnnemies[ forX ].x_max ) roboEnnemies[ forX ].dir = 0;
            }
            else if( roboEnnemies[ forX ].type == 3 ) // rocket
            {
                roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 3;
                roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + roboEnnemies[ forX ].dir - 1;
                if( roboEnnemies[ forX ].y < roboEnnemies[ forX ].x_min ) roboEnnemies[ forX ].dir = 2;
                if( roboEnnemies[ forX ].y > roboEnnemies[ forX ].x_max ) roboEnnemies[ forX ].dir = 0;
            }
            else if( roboEnnemies[ forX ].type == 4 ) // tesla tower
            {
                if( roboEnnemies[ forX ].x > roboPlayer.x_world + roboPlayer.x_screen - 30 && roboEnnemies[ forX ].x < roboPlayer.x_world + roboPlayer.x_screen + 30 )
                {
                    roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 2;
                    roboEnnemies[ forX ].x = roboEnnemies[ forX ].x + roboEnnemies[ forX ].dir - 1;
                }
                else
                {
                    roboEnnemies[ forX ].x = roboEnnemies[ forX ].x_min;
                    if( roboEnnemies[ forX ].x_min - roboPlayer.x_world > roboPlayer.x_screen ) roboEnnemies[ forX ].dir = 0;
                    else roboEnnemies[ forX ].dir = 2;
                }
            }
            else if( roboEnnemies[ forX ].type == 5 ) // jumper
            {
                roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 19;
                if( roboEnnemies[ forX ].counter > 4 )
                  roboEnnemies[ forX ].x = roboEnnemies[ forX ].x + roboEnnemies[ forX ].dir - 1;

                if( roboEnnemies[ forX ].counter == 5 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 3;
                else if( roboEnnemies[ forX ].counter == 6 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 2;
                else if( roboEnnemies[ forX ].counter == 7 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 2;
                else if( roboEnnemies[ forX ].counter == 8 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 1;
                else if( roboEnnemies[ forX ].counter == 9 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 1;
                else if( roboEnnemies[ forX ].counter == 13 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 1;
                else if( roboEnnemies[ forX ].counter == 14 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 1;
                else if( roboEnnemies[ forX ].counter == 15 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 2;
                else if( roboEnnemies[ forX ].counter == 16 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 2;
                else if( roboEnnemies[ forX ].counter == 17 ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 3;

                if( roboEnnemies[ forX ].x < roboEnnemies[ forX ].x_min ) roboEnnemies[ forX ].dir = 2;
                if( roboEnnemies[ forX ].x > roboEnnemies[ forX ].x_max ) roboEnnemies[ forX ].dir = 0;
            }
            else if( roboEnnemies[ forX ].type == 6 ) // ghost
            {
                roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 6;
                if( roboEnnemies[ forX ].x - roboPlayer.x_world > roboPlayer.x_screen && roboEnnemies[ forX ].x - roboPlayer.x_world - roboPlayer.x_screen > 10 && roboEnnemies[ forX ].x - roboPlayer.x_world - roboPlayer.x_screen < 35 )
                {
                    if( roboEnnemies[ forX ].y < roboPlayer.y_screen ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 1;
                    if( roboEnnemies[ forX ].y > roboPlayer.y_screen ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 1;
                    roboEnnemies[ forX ].x = roboEnnemies[ forX ].x - 1;
                }
                if( roboEnnemies[ forX ].x - roboPlayer.x_world < roboPlayer.x_screen && roboPlayer.x_screen - ( roboEnnemies[ forX ].x - roboPlayer.x_world ) > 10 && roboPlayer.x_screen - ( roboEnnemies[ forX ].x - roboPlayer.x_world ) < 35 )
                {
                    if( roboEnnemies[ forX ].y < roboPlayer.y_screen ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y + 1;
                    if( roboEnnemies[ forX ].y > roboPlayer.y_screen ) roboEnnemies[ forX ].y = roboEnnemies[ forX ].y - 1;
                    roboEnnemies[ forX ].x = roboEnnemies[ forX ].x + 1;
                }
            }
            else if( roboEnnemies[ forX ].type == 10 ) // boss
            {
                roboEnnemies[ forX ].counter = ( roboEnnemies[ forX ].counter + 1 ) % 18;
                if( roboEnnemies[ forX ].counter % roboEnnemies[ forX ].life == 0 )
                  roboEnnemies[ forX ].x = roboEnnemies[ forX ].x + roboEnnemies[ forX ].dir - 1;

                if( roboEnnemies[ forX ].x < roboEnnemies[ forX ].x_min ) roboEnnemies[ forX ].dir = 2;
                if( roboEnnemies[ forX ].x > roboEnnemies[ forX ].x_max ) roboEnnemies[ forX ].dir = 0;
            }
        }
        else
        {
            if( roboEnnemies[ forX ].anim > 0 )
              roboEnnemies[ forX ].anim = roboEnnemies[ forX ].anim - 1;
            else
            {
                if( roboEnnemies[ forX ].type == 10 )
                  roboInitNext();
            }
        }
    }
}

// Real ennemy_draw() - two branches per enemy: alive (its own animated
// sprite), or dead-but-still-exploding (the shared "boom" frames).
void roboEnnemyDraw()
{
    int forX;
    for( forX = 0; forX < roboEnnemiesNumber; forX = forX + 1 )
    {
        if( roboEnnemies[ forX ].life > 0 )
        {
            if( roboEnnemies[ forX ].x < roboPlayer.x_world + 90 && roboEnnemies[ forX ].x > roboPlayer.x_world - 10 )
            {
                if( roboEnnemies[ forX ].anim % 3 == 0 )
                {
                    if( roboEnnemies[ forX ].type == 0 ) // larva
                    {
                        if( roboEnnemies[ forX ].dir == 0 )
                          gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy01[ roboEnnemies[ forX ].counter / 2 ], ROBO_NOROT, ROBO_NOFLIP );
                        else
                          gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy01[ roboEnnemies[ forX ].counter / 2 ], ROBO_NOROT, ROBO_FLIPH );
                    }
                    else if( roboEnnemies[ forX ].type == 1 ) // ufo
                    {
                        gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy02 );
                        gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y + 6, roboEnnemy02b[ roboEnnemies[ forX ].counter % 2 ] );
                    }
                    else if( roboEnnemies[ forX ].type == 2 ) // robot
                    {
                        if( roboEnnemies[ forX ].dir == 0 )
                          gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy03[ roboEnnemies[ forX ].counter ], ROBO_NOROT, ROBO_FLIPH );
                        else
                          gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy03[ roboEnnemies[ forX ].counter ], ROBO_NOROT, ROBO_NOFLIP );
                    }
                    else if( roboEnnemies[ forX ].type == 3 ) // rocket
                    {
                        gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy04 );
                        if( roboEnnemies[ forX ].dir == 0 )
                          gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world + 1, roboEnnemies[ forX ].y + 7, roboEnnemy04b[ roboEnnemies[ forX ].counter % 2 ] );
                    }
                    else if( roboEnnemies[ forX ].type == 4 ) // tesla - real upstream bug: x_min/x_max reused as (x,y) for the pole (see header comment)
                    {
                        gbDrawBitmap( roboEnnemies[ forX ].x_min - roboPlayer.x_world, roboEnnemies[ forX ].x_max, roboEnnemy05 );
                        if( roboEnnemies[ forX ].counter == 0 )
                          gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy05b );
                        else
                          gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy05b, ROBO_NOROT, ROBO_FLIPVH );
                    }
                    else if( roboEnnemies[ forX ].type == 5 ) // jumper
                      gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy06 );
                    else if( roboEnnemies[ forX ].type == 6 ) // ghost
                    {
                        gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y, roboEnnemy07, ROBO_NOROT, ROBO_FLIPH );
                        gbSetColor( GB_INVERT );
                        gbDrawFastVLine( roboEnnemies[ forX ].x - roboPlayer.x_world + 1 + ( roboEnnemies[ forX ].counter / 2 ), roboEnnemies[ forX ].y + 4, 2 );
                        gbSetColor( GB_BLACK );
                    }
                    else if( roboEnnemies[ forX ].type == 10 ) // boss
                    {
                        if( roboEnnemies[ forX ].dir == 0 )
                        {
                            gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y - 2 + ( roboEnnemies[ forX ].counter / 4 ) % 4, roboEnnemyBossHead );
                            gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world, roboEnnemies[ forX ].y + 8, roboEnnemyBossBody );
                        }
                        else
                        {
                            gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world - 3, roboEnnemies[ forX ].y - 2 + ( roboEnnemies[ forX ].counter / 4 ) % 4, roboEnnemyBossHead, ROBO_NOROT, ROBO_FLIPH );
                            gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world - 3, roboEnnemies[ forX ].y + 8, roboEnnemyBossBody, ROBO_NOROT, ROBO_FLIPH );
                        }
                        gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world - 2, roboEnnemies[ forX ].y + 26, roboEnnemyBossFeet, ROBO_ROTCCW, ROBO_NOFLIP );
                        gbDrawBitmapRotated( roboEnnemies[ forX ].x - roboPlayer.x_world + 9, roboEnnemies[ forX ].y + 26, roboEnnemyBossFeet, ROBO_ROTCCW, ROBO_FLIPH );
                    }
                }
            }
        }
        else
        {
            if( roboEnnemies[ forX ].x < roboPlayer.x_world + 90 && roboEnnemies[ forX ].x > roboPlayer.x_world - 10 )
            {
                if( roboEnnemies[ forX ].type == 4 ) // tesla - same x_min/x_max-as-(x,y) reuse as the alive-draw case above
                {
                    if( roboEnnemies[ forX ].anim > 0 )
                      gbDrawBitmap( roboEnnemies[ forX ].x_min - roboPlayer.x_world - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ), roboEnnemies[ forX ].x_max - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ), roboBoom[ roboEnnemies[ forX ].anim % 2 ] );
                }
                else if( roboEnnemies[ forX ].type == 10 ) // boss - four simultaneous explosion puffs
                {
                    if( roboEnnemies[ forX ].anim > 0 )
                    {
                        gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ) + 2, roboEnnemies[ forX ].y - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ), roboBoom[ roboEnnemies[ forX ].anim % 2 ] );
                        gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ) + 2, roboEnnemies[ forX ].y - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ) + 10, roboBoom[ roboEnnemies[ forX ].anim % 2 ] );
                        gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ) - 4, roboEnnemies[ forX ].y - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ) + 22, roboBoom[ roboEnnemies[ forX ].anim % 2 ] );
                        gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ) + 8, roboEnnemies[ forX ].y - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ) + 22, roboBoom[ roboEnnemies[ forX ].anim % 2 ] );
                    }
                }
                else // every other type shares the same single-puff explosion draw
                {
                    if( roboEnnemies[ forX ].anim > 0 )
                      gbDrawBitmap( roboEnnemies[ forX ].x - roboPlayer.x_world - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ), roboEnnemies[ forX ].y - ( 2 * ( roboEnnemies[ forX ].anim % 2 ) ), roboBoom[ roboEnnemies[ forX ].anim % 2 ] );
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
// Level (Level.ino)
// -----------------------------------------------------------------------------

void roboLevelDrawLandscape()
{
    int forX, forY;
    for( forX = 0; forX < roboTileNumber; forX = forX + 1 )
    {
        if( ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world > -25 && ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world < 92 )
        {
            if( roboLandscapePlan[ forX ].type_landscape == 10 )
              gbDrawLine( ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world, roboLandscapePlan[ forX ].y_landscape * 4, ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world, 48 );
            else if( roboLandscapePlan[ forX ].type_landscape == 11 )
              gbDrawLine( ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world - 1, roboLandscapePlan[ forX ].y_landscape * 4, ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world - 1, 48 );
            else if( roboLandscapePlan[ forX ].type_landscape == 100 )
            {
                for( forY = 0; forY < 6; forY = forY + 1 )
                  gbDrawBitmap( ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world + ( forY * 24 ), 44, roboLandscape[ 4 ] );
            }
            else
              gbDrawBitmapRotated( ( roboLandscapePlan[ forX ].x_landscape * 4 ) - roboPlayer.x_world, roboLandscapePlan[ forX ].y_landscape * 4, roboLandscape[ roboLandscapePlan[ forX ].type_landscape ], ROBO_NOROT, ROBO_NOFLIP );
        }
    }
}

void roboDrawBossArena()
{
    int forX;
    for( forX = 0; forX < 6; forX = forX + 1 )
      gbDrawBitmap( forX * 24, 44, roboLandscape[ 4 ] );

    for( forX = 0; forX < 12; forX = forX + 1 )
    {
        gbDrawBitmapRotated( -4, 48 - roboLevelLength + ( forX * 4 ), roboLandscape[ 8 ], ROBO_NOROT, ROBO_FLIPH );
        gbDrawBitmap( 80, 48 - roboLevelLength + ( forX * 4 ), roboLandscape[ 8 ] );
    }
}

void roboLevelDrawIntroCard()
{
    gbDrawBitmap( 7, 12, roboBackground[ 0 ] );
    gbDrawBitmapRotated( 19, 28, roboEnnemy01[ 2 ], ROBO_NOROT, ROBO_NOFLIP );

    gbDrawBitmapRotated( 0, 33, roboLandscape[ 0 ], ROBO_NOROT, ROBO_NOFLIP );
    gbDrawBitmapRotated( 24, 33, roboLandscape[ 0 ], ROBO_NOROT, ROBO_NOFLIP );
    gbDrawBitmapRotated( 48, 33, roboLandscape[ 0 ], ROBO_NOROT, ROBO_NOFLIP );
    gbDrawBitmapRotated( 72, 33, roboLandscape[ 0 ], ROBO_NOROT, ROBO_NOFLIP );

    gbDrawBitmapRotated( 47, 25, roboLandscape[ 6 ], ROBO_NOROT, ROBO_NOFLIP );
    gbDrawBitmapRotated( 55, 21, roboLandscape[ 9 ], ROBO_NOROT, ROBO_NOFLIP );
    gbDrawBitmapRotated( 55, 25, roboLandscape[ 9 ], ROBO_NOROT, ROBO_NOFLIP );
    gbDrawBitmapRotated( 55, 29, roboLandscape[ 9 ], ROBO_NOROT, ROBO_NOFLIP );
    gbDrawBitmapRotated( 59, 25, roboLandscape[ 7 ], ROBO_NOROT, ROBO_NOFLIP );
}

// Real level_drawIntro() - the "Level N" title card shown between worlds
// (runningLevel%4==0). All 5 real cases (0/4/8/12/16, "Level 1".."Level 5")
// draw the exact same real background art, differing only in the printed
// number - ported as one shared roboLevelDrawIntroCard() helper plus the
// real per-case text, matching upstream's own real (if repetitive) source
// line-for-line rather than inventing a different shape.
void roboLevelDrawIntro()
{
    gbFillRect( 0, 0, 84, 11 );
    gbFillRect( 0, 37, 84, 11 );
    gbSetColor( GB_WHITE );
    gbFillRect( 27, 2, 30, 7 );
    gbSetColor( GB_BLACK );
    gbCursorX = 28;
    gbCursorY = 3;

    if( roboRunningLevel == 0 )
    {
        gbPrintString( "Level 1" );
        roboLevelDrawIntroCard();
    }
    else if( roboRunningLevel == 4 )
    {
        gbPrintString( "Level 2" );
        roboLevelDrawIntroCard();
    }
    else if( roboRunningLevel == 8 )
    {
        gbPrintString( "Level 3" );
        roboLevelDrawIntroCard();
    }
    else if( roboRunningLevel == 12 )
    {
        gbPrintString( "Level 4" );
        roboLevelDrawIntroCard();
    }
    else if( roboRunningLevel == 16 )
    {
        gbPrintString( "Level 5" );
        roboLevelDrawIntroCard();
    }
}

void roboLevelDrawBackground()
{
    int forX;
    for( forX = 0; forX < roboLevelLength / 50; forX = forX + 1 )
    {
        if( forX * 50 - roboPlayer.x_world > -16 && forX * 50 - roboPlayer.x_world < 92 )
          gbDrawBitmap( forX * 50 - roboPlayer.x_world, 6, roboBackground[ 0 ] );
    }
}

void roboMapInitLevel1()
{
    roboLandscapePlan[0].x_landscape = 0;
    roboLandscapePlan[0].y_landscape = 11;
    roboLandscapePlan[0].type_landscape = 0;
    roboLandscapePlan[1].x_landscape = 6;
    roboLandscapePlan[1].y_landscape = 11;
    roboLandscapePlan[1].type_landscape = 0;
    roboLandscapePlan[2].x_landscape = 12;
    roboLandscapePlan[2].y_landscape = 11;
    roboLandscapePlan[2].type_landscape = 0;
    roboLandscapePlan[3].x_landscape = 18;
    roboLandscapePlan[3].y_landscape = 11;
    roboLandscapePlan[3].type_landscape = 0;
    roboLandscapePlan[4].x_landscape = 24;
    roboLandscapePlan[4].y_landscape = 11;
    roboLandscapePlan[4].type_landscape = 0;
    roboLandscapePlan[5].x_landscape = 30;
    roboLandscapePlan[5].y_landscape = 11;
    roboLandscapePlan[5].type_landscape = 0;
    roboLandscapePlan[6].x_landscape = 36;
    roboLandscapePlan[6].y_landscape = 11;
    roboLandscapePlan[6].type_landscape = 0;
    roboLandscapePlan[7].x_landscape = 42;
    roboLandscapePlan[7].y_landscape = 11;
    roboLandscapePlan[7].type_landscape = 0;
    roboLandscapePlan[8].x_landscape = 48;
    roboLandscapePlan[8].y_landscape = 11;
    roboLandscapePlan[8].type_landscape = 0;
    roboLandscapePlan[9].x_landscape = 54;
    roboLandscapePlan[9].y_landscape = 11;
    roboLandscapePlan[9].type_landscape = 0;
    roboLandscapePlan[10].x_landscape = 60;
    roboLandscapePlan[10].y_landscape = 11;
    roboLandscapePlan[10].type_landscape = 0;
    roboLandscapePlan[11].x_landscape = 66;
    roboLandscapePlan[11].y_landscape = 11;
    roboLandscapePlan[11].type_landscape = 0;
    roboLandscapePlan[12].x_landscape = 72;
    roboLandscapePlan[12].y_landscape = 11;
    roboLandscapePlan[12].type_landscape = 0;
    roboLandscapePlan[13].x_landscape = 78;
    roboLandscapePlan[13].y_landscape = 11;
    roboLandscapePlan[13].type_landscape = 0;
    roboLandscapePlan[14].x_landscape = 84;
    roboLandscapePlan[14].y_landscape = 11;
    roboLandscapePlan[14].type_landscape = 0;
    roboLandscapePlan[15].x_landscape = 90;
    roboLandscapePlan[15].y_landscape = 11;
    roboLandscapePlan[15].type_landscape = 0;
    roboLandscapePlan[16].x_landscape = 96;
    roboLandscapePlan[16].y_landscape = 11;
    roboLandscapePlan[16].type_landscape = 0;
    roboLandscapePlan[17].x_landscape = 102;
    roboLandscapePlan[17].y_landscape = 11;
    roboLandscapePlan[17].type_landscape = 0;
    roboLandscapePlan[18].x_landscape = 108;
    roboLandscapePlan[18].y_landscape = 11;
    roboLandscapePlan[18].type_landscape = 0;
    roboLandscapePlan[19].x_landscape = 114;
    roboLandscapePlan[19].y_landscape = 11;
    roboLandscapePlan[19].type_landscape = 0;
    roboLandscapePlan[20].x_landscape = 120;
    roboLandscapePlan[20].y_landscape = 11;
    roboLandscapePlan[20].type_landscape = 0;
    roboLandscapePlan[21].x_landscape = 126;
    roboLandscapePlan[21].y_landscape = 11;
    roboLandscapePlan[21].type_landscape = 0;
    roboLandscapePlan[22].x_landscape = 132;
    roboLandscapePlan[22].y_landscape = 11;
    roboLandscapePlan[22].type_landscape = 0;
    roboLandscapePlan[23].x_landscape = 138;
    roboLandscapePlan[23].y_landscape = 11;
    roboLandscapePlan[23].type_landscape = 0;
    roboLandscapePlan[24].x_landscape = 144;
    roboLandscapePlan[24].y_landscape = 11;
    roboLandscapePlan[24].type_landscape = 0;
    roboLandscapePlan[25].x_landscape = 150;
    roboLandscapePlan[25].y_landscape = 11;
    roboLandscapePlan[25].type_landscape = 0;
    roboLandscapePlan[26].x_landscape = 156;
    roboLandscapePlan[26].y_landscape = 11;
    roboLandscapePlan[26].type_landscape = 0;
    roboLandscapePlan[27].x_landscape = 162;
    roboLandscapePlan[27].y_landscape = 11;
    roboLandscapePlan[27].type_landscape = 0;
    roboLandscapePlan[28].x_landscape = 168;
    roboLandscapePlan[28].y_landscape = 11;
    roboLandscapePlan[28].type_landscape = 0;
    roboLandscapePlan[29].x_landscape = 174;
    roboLandscapePlan[29].y_landscape = 11;
    roboLandscapePlan[29].type_landscape = 0;
    roboLandscapePlan[30].x_landscape = 180;
    roboLandscapePlan[30].y_landscape = 11;
    roboLandscapePlan[30].type_landscape = 0;
    roboLandscapePlan[31].x_landscape = 186;
    roboLandscapePlan[31].y_landscape = 11;
    roboLandscapePlan[31].type_landscape = 0;
    roboLandscapePlan[32].x_landscape = 192;
    roboLandscapePlan[32].y_landscape = 11;
    roboLandscapePlan[32].type_landscape = 0;
    roboLandscapePlan[33].x_landscape = 198;
    roboLandscapePlan[33].y_landscape = 11;
    roboLandscapePlan[33].type_landscape = 0;
    roboLandscapePlan[34].x_landscape = 204;
    roboLandscapePlan[34].y_landscape = 11;
    roboLandscapePlan[34].type_landscape = 0;
    roboLandscapePlan[35].x_landscape = 210;
    roboLandscapePlan[35].y_landscape = 11;
    roboLandscapePlan[35].type_landscape = 0;
    roboLandscapePlan[36].x_landscape = 216;
    roboLandscapePlan[36].y_landscape = 11;
    roboLandscapePlan[36].type_landscape = 0;
    roboLandscapePlan[37].x_landscape = 222;
    roboLandscapePlan[37].y_landscape = 11;
    roboLandscapePlan[37].type_landscape = 0;
    roboLandscapePlan[38].x_landscape = 228;
    roboLandscapePlan[38].y_landscape = 11;
    roboLandscapePlan[38].type_landscape = 0;
    roboLandscapePlan[39].x_landscape = 234;
    roboLandscapePlan[39].y_landscape = 11;
    roboLandscapePlan[39].type_landscape = 0;
    roboLandscapePlan[40].x_landscape = 240;
    roboLandscapePlan[40].y_landscape = 11;
    roboLandscapePlan[40].type_landscape = 0;
    roboLandscapePlan[41].x_landscape = 246;
    roboLandscapePlan[41].y_landscape = 11;
    roboLandscapePlan[41].type_landscape = 0;
    roboLandscapePlan[42].x_landscape = 252;
    roboLandscapePlan[42].y_landscape = 0;
    roboLandscapePlan[42].type_landscape = 100;
}

// Real level 5's own real map (a genuine, unique layout - not shared with
// 9/13/17, unlike ennemy_init()'s own wave data - see this file's own
// header comment: it uses landscape tile type 3 for its grass segments,
// where levels 9/13/17 use type 0, for 19 real tiles - verified directly,
// not assumed).
void roboMapInitLevel5()
{
    roboLandscapePlan[0].x_landscape = 0;
    roboLandscapePlan[0].y_landscape = 11;
    roboLandscapePlan[0].type_landscape = 3;
    roboLandscapePlan[1].x_landscape = 3;
    roboLandscapePlan[1].y_landscape = 9;
    roboLandscapePlan[1].type_landscape = 5;
    roboLandscapePlan[2].x_landscape = 6;
    roboLandscapePlan[2].y_landscape = 11;
    roboLandscapePlan[2].type_landscape = 3;
    roboLandscapePlan[3].x_landscape = 12;
    roboLandscapePlan[3].y_landscape = 11;
    roboLandscapePlan[3].type_landscape = 3;
    roboLandscapePlan[4].x_landscape = 18;
    roboLandscapePlan[4].y_landscape = 11;
    roboLandscapePlan[4].type_landscape = 3;
    roboLandscapePlan[5].x_landscape = 24;
    roboLandscapePlan[5].y_landscape = 11;
    roboLandscapePlan[5].type_landscape = 3;
    roboLandscapePlan[6].x_landscape = 27;
    roboLandscapePlan[6].y_landscape = 9;
    roboLandscapePlan[6].type_landscape = 5;
    roboLandscapePlan[7].x_landscape = 30;
    roboLandscapePlan[7].y_landscape = 11;
    roboLandscapePlan[7].type_landscape = 3;
    roboLandscapePlan[8].x_landscape = 33;
    roboLandscapePlan[8].y_landscape = 8;
    roboLandscapePlan[8].type_landscape = 5;
    roboLandscapePlan[9].x_landscape = 33;
    roboLandscapePlan[9].y_landscape = 9;
    roboLandscapePlan[9].type_landscape = 5;
    roboLandscapePlan[10].x_landscape = 36;
    roboLandscapePlan[10].y_landscape = 11;
    roboLandscapePlan[10].type_landscape = 3;
    roboLandscapePlan[11].x_landscape = 40;
    roboLandscapePlan[11].y_landscape = 9;
    roboLandscapePlan[11].type_landscape = 5;
    roboLandscapePlan[12].x_landscape = 42;
    roboLandscapePlan[12].y_landscape = 11;
    roboLandscapePlan[12].type_landscape = 3;
    roboLandscapePlan[13].x_landscape = 48;
    roboLandscapePlan[13].y_landscape = 11;
    roboLandscapePlan[13].type_landscape = 3;
    roboLandscapePlan[14].x_landscape = 54;
    roboLandscapePlan[14].y_landscape = 11;
    roboLandscapePlan[14].type_landscape = 3;
    roboLandscapePlan[15].x_landscape = 60;
    roboLandscapePlan[15].y_landscape = 8;
    roboLandscapePlan[15].type_landscape = 3;
    roboLandscapePlan[16].x_landscape = 60;
    roboLandscapePlan[16].y_landscape = 8;
    roboLandscapePlan[16].type_landscape = 10;
    roboLandscapePlan[17].x_landscape = 66;
    roboLandscapePlan[17].y_landscape = 8;
    roboLandscapePlan[17].type_landscape = 3;
    roboLandscapePlan[18].x_landscape = 72;
    roboLandscapePlan[18].y_landscape = 8;
    roboLandscapePlan[18].type_landscape = 3;
    roboLandscapePlan[19].x_landscape = 75;
    roboLandscapePlan[19].y_landscape = 5;
    roboLandscapePlan[19].type_landscape = 5;
    roboLandscapePlan[20].x_landscape = 75;
    roboLandscapePlan[20].y_landscape = 6;
    roboLandscapePlan[20].type_landscape = 5;
    roboLandscapePlan[21].x_landscape = 78;
    roboLandscapePlan[21].y_landscape = 8;
    roboLandscapePlan[21].type_landscape = 11;
    roboLandscapePlan[22].x_landscape = 78;
    roboLandscapePlan[22].y_landscape = 11;
    roboLandscapePlan[22].type_landscape = 3;
    roboLandscapePlan[23].x_landscape = 81;
    roboLandscapePlan[23].y_landscape = 4;
    roboLandscapePlan[23].type_landscape = 1;
    roboLandscapePlan[24].x_landscape = 87;
    roboLandscapePlan[24].y_landscape = 11;
    roboLandscapePlan[24].type_landscape = 3;
    roboLandscapePlan[25].x_landscape = 92;
    roboLandscapePlan[25].y_landscape = 9;
    roboLandscapePlan[25].type_landscape = 5;
    roboLandscapePlan[26].x_landscape = 93;
    roboLandscapePlan[26].y_landscape = 11;
    roboLandscapePlan[26].type_landscape = 3;
    roboLandscapePlan[27].x_landscape = 99;
    roboLandscapePlan[27].y_landscape = 11;
    roboLandscapePlan[27].type_landscape = 3;
    roboLandscapePlan[28].x_landscape = 105;
    roboLandscapePlan[28].y_landscape = 11;
    roboLandscapePlan[28].type_landscape = 3;
    roboLandscapePlan[29].x_landscape = 106;
    roboLandscapePlan[29].y_landscape = 8;
    roboLandscapePlan[29].type_landscape = 1;
    roboLandscapePlan[30].x_landscape = 109;
    roboLandscapePlan[30].y_landscape = 5;
    roboLandscapePlan[30].type_landscape = 1;
    roboLandscapePlan[31].x_landscape = 111;
    roboLandscapePlan[31].y_landscape = 11;
    roboLandscapePlan[31].type_landscape = 3;
    roboLandscapePlan[32].x_landscape = 112;
    roboLandscapePlan[32].y_landscape = 8;
    roboLandscapePlan[32].type_landscape = 1;
    roboLandscapePlan[33].x_landscape = 117;
    roboLandscapePlan[33].y_landscape = 11;
    roboLandscapePlan[33].type_landscape = 0;
    roboLandscapePlan[34].x_landscape = 126;
    roboLandscapePlan[34].y_landscape = 11;
    roboLandscapePlan[34].type_landscape = 0;
    roboLandscapePlan[35].x_landscape = 127;
    roboLandscapePlan[35].y_landscape = 9;
    roboLandscapePlan[35].type_landscape = 5;
    roboLandscapePlan[36].x_landscape = 132;
    roboLandscapePlan[36].y_landscape = 11;
    roboLandscapePlan[36].type_landscape = 0;
    roboLandscapePlan[37].x_landscape = 138;
    roboLandscapePlan[37].y_landscape = 11;
    roboLandscapePlan[37].type_landscape = 0;
    roboLandscapePlan[38].x_landscape = 141;
    roboLandscapePlan[38].y_landscape = 9;
    roboLandscapePlan[38].type_landscape = 5;
    roboLandscapePlan[39].x_landscape = 144;
    roboLandscapePlan[39].y_landscape = 11;
    roboLandscapePlan[39].type_landscape = 0;
    roboLandscapePlan[40].x_landscape = 145;
    roboLandscapePlan[40].y_landscape = 7;
    roboLandscapePlan[40].type_landscape = 5;
    roboLandscapePlan[41].x_landscape = 145;
    roboLandscapePlan[41].y_landscape = 8;
    roboLandscapePlan[41].type_landscape = 5;
    roboLandscapePlan[42].x_landscape = 150;
    roboLandscapePlan[42].y_landscape = 8;
    roboLandscapePlan[42].type_landscape = 0;
    roboLandscapePlan[43].x_landscape = 150;
    roboLandscapePlan[43].y_landscape = 8;
    roboLandscapePlan[43].type_landscape = 10;
    roboLandscapePlan[44].x_landscape = 155;
    roboLandscapePlan[44].y_landscape = 4;
    roboLandscapePlan[44].type_landscape = 1;
    roboLandscapePlan[45].x_landscape = 156;
    roboLandscapePlan[45].y_landscape = 8;
    roboLandscapePlan[45].type_landscape = 0;
    roboLandscapePlan[46].x_landscape = 162;
    roboLandscapePlan[46].y_landscape = 8;
    roboLandscapePlan[46].type_landscape = 0;
    roboLandscapePlan[47].x_landscape = 165;
    roboLandscapePlan[47].y_landscape = 5;
    roboLandscapePlan[47].type_landscape = 5;
    roboLandscapePlan[48].x_landscape = 165;
    roboLandscapePlan[48].y_landscape = 6;
    roboLandscapePlan[48].type_landscape = 5;
    roboLandscapePlan[49].x_landscape = 168;
    roboLandscapePlan[49].y_landscape = 8;
    roboLandscapePlan[49].type_landscape = 11;
    roboLandscapePlan[50].x_landscape = 168;
    roboLandscapePlan[50].y_landscape = 11;
    roboLandscapePlan[50].type_landscape = 0;
    roboLandscapePlan[51].x_landscape = 173;
    roboLandscapePlan[51].y_landscape = 9;
    roboLandscapePlan[51].type_landscape = 5;
    roboLandscapePlan[52].x_landscape = 174;
    roboLandscapePlan[52].y_landscape = 11;
    roboLandscapePlan[52].type_landscape = 0;
    roboLandscapePlan[53].x_landscape = 179;
    roboLandscapePlan[53].y_landscape = 8;
    roboLandscapePlan[53].type_landscape = 5;
    roboLandscapePlan[54].x_landscape = 179;
    roboLandscapePlan[54].y_landscape = 9;
    roboLandscapePlan[54].type_landscape = 5;
    roboLandscapePlan[55].x_landscape = 180;
    roboLandscapePlan[55].y_landscape = 11;
    roboLandscapePlan[55].type_landscape = 0;
    roboLandscapePlan[56].x_landscape = 185;
    roboLandscapePlan[56].y_landscape = 9;
    roboLandscapePlan[56].type_landscape = 5;
    roboLandscapePlan[57].x_landscape = 186;
    roboLandscapePlan[57].y_landscape = 11;
    roboLandscapePlan[57].type_landscape = 0;
    roboLandscapePlan[58].x_landscape = 192;
    roboLandscapePlan[58].y_landscape = 11;
    roboLandscapePlan[58].type_landscape = 0;
    roboLandscapePlan[59].x_landscape = 197;
    roboLandscapePlan[59].y_landscape = 9;
    roboLandscapePlan[59].type_landscape = 6;
    roboLandscapePlan[60].x_landscape = 198;
    roboLandscapePlan[60].y_landscape = 11;
    roboLandscapePlan[60].type_landscape = 0;
    roboLandscapePlan[61].x_landscape = 199;
    roboLandscapePlan[61].y_landscape = 7;
    roboLandscapePlan[61].type_landscape = 6;
    roboLandscapePlan[62].x_landscape = 199;
    roboLandscapePlan[62].y_landscape = 9;
    roboLandscapePlan[62].type_landscape = 5;
    roboLandscapePlan[63].x_landscape = 201;
    roboLandscapePlan[63].y_landscape = 10;
    roboLandscapePlan[63].type_landscape = 4;
    roboLandscapePlan[64].x_landscape = 203;
    roboLandscapePlan[64].y_landscape = 1;
    roboLandscapePlan[64].type_landscape = 7;
    roboLandscapePlan[65].x_landscape = 203;
    roboLandscapePlan[65].y_landscape = 3;
    roboLandscapePlan[65].type_landscape = 5;
    roboLandscapePlan[66].x_landscape = 203;
    roboLandscapePlan[66].y_landscape = 5;
    roboLandscapePlan[66].type_landscape = 5;
    roboLandscapePlan[67].x_landscape = 203;
    roboLandscapePlan[67].y_landscape = 7;
    roboLandscapePlan[67].type_landscape = 5;
    roboLandscapePlan[68].x_landscape = 204;
    roboLandscapePlan[68].y_landscape = 11;
    roboLandscapePlan[68].type_landscape = 0;
    roboLandscapePlan[69].x_landscape = 205;
    roboLandscapePlan[69].y_landscape = 6;
    roboLandscapePlan[69].type_landscape = 5;
    roboLandscapePlan[70].x_landscape = 207;
    roboLandscapePlan[70].y_landscape = 5;
    roboLandscapePlan[70].type_landscape = 7;
    roboLandscapePlan[71].x_landscape = 207;
    roboLandscapePlan[71].y_landscape = 7;
    roboLandscapePlan[71].type_landscape = 5;
    roboLandscapePlan[72].x_landscape = 207;
    roboLandscapePlan[72].y_landscape = 10;
    roboLandscapePlan[72].type_landscape = 4;
    roboLandscapePlan[73].x_landscape = 209;
    roboLandscapePlan[73].y_landscape = 7;
    roboLandscapePlan[73].type_landscape = 7;
    roboLandscapePlan[74].x_landscape = 210;
    roboLandscapePlan[74].y_landscape = 11;
    roboLandscapePlan[74].type_landscape = 0;
    roboLandscapePlan[75].x_landscape = 219;
    roboLandscapePlan[75].y_landscape = 11;
    roboLandscapePlan[75].type_landscape = 4;
    roboLandscapePlan[76].x_landscape = 222;
    roboLandscapePlan[76].y_landscape = 8;
    roboLandscapePlan[76].type_landscape = 9;
    roboLandscapePlan[77].x_landscape = 225;
    roboLandscapePlan[77].y_landscape = 7;
    roboLandscapePlan[77].type_landscape = 9;
    roboLandscapePlan[78].x_landscape = 225;
    roboLandscapePlan[78].y_landscape = 11;
    roboLandscapePlan[78].type_landscape = 4;
    roboLandscapePlan[79].x_landscape = 228;
    roboLandscapePlan[79].y_landscape = 6;
    roboLandscapePlan[79].type_landscape = 9;
    roboLandscapePlan[80].x_landscape = 231;
    roboLandscapePlan[80].y_landscape = 11;
    roboLandscapePlan[80].type_landscape = 4;
    roboLandscapePlan[81].x_landscape = 232;
    roboLandscapePlan[81].y_landscape = 6;
    roboLandscapePlan[81].type_landscape = 9;
    roboLandscapePlan[82].x_landscape = 235;
    roboLandscapePlan[82].y_landscape = 5;
    roboLandscapePlan[82].type_landscape = 9;
    roboLandscapePlan[83].x_landscape = 237;
    roboLandscapePlan[83].y_landscape = 11;
    roboLandscapePlan[83].type_landscape = 4;
    roboLandscapePlan[84].x_landscape = 238;
    roboLandscapePlan[84].y_landscape = 4;
    roboLandscapePlan[84].type_landscape = 9;
    roboLandscapePlan[85].x_landscape = 241;
    roboLandscapePlan[85].y_landscape = 3;
    roboLandscapePlan[85].type_landscape = 5;
    roboLandscapePlan[86].x_landscape = 241;
    roboLandscapePlan[86].y_landscape = 5;
    roboLandscapePlan[86].type_landscape = 5;
    roboLandscapePlan[87].x_landscape = 241;
    roboLandscapePlan[87].y_landscape = 7;
    roboLandscapePlan[87].type_landscape = 5;
}

// Real levels 9/13/17 (verified byte-identical to each other, unlike
// level 5 - see this file's own header comment).
void roboMapInitLevel9()
{
    roboLandscapePlan[0].x_landscape = 0;
    roboLandscapePlan[0].y_landscape = 11;
    roboLandscapePlan[0].type_landscape = 0;
    roboLandscapePlan[1].x_landscape = 3;
    roboLandscapePlan[1].y_landscape = 9;
    roboLandscapePlan[1].type_landscape = 5;
    roboLandscapePlan[2].x_landscape = 6;
    roboLandscapePlan[2].y_landscape = 11;
    roboLandscapePlan[2].type_landscape = 0;
    roboLandscapePlan[3].x_landscape = 12;
    roboLandscapePlan[3].y_landscape = 11;
    roboLandscapePlan[3].type_landscape = 0;
    roboLandscapePlan[4].x_landscape = 18;
    roboLandscapePlan[4].y_landscape = 11;
    roboLandscapePlan[4].type_landscape = 0;
    roboLandscapePlan[5].x_landscape = 24;
    roboLandscapePlan[5].y_landscape = 11;
    roboLandscapePlan[5].type_landscape = 0;
    roboLandscapePlan[6].x_landscape = 27;
    roboLandscapePlan[6].y_landscape = 9;
    roboLandscapePlan[6].type_landscape = 5;
    roboLandscapePlan[7].x_landscape = 30;
    roboLandscapePlan[7].y_landscape = 11;
    roboLandscapePlan[7].type_landscape = 0;
    roboLandscapePlan[8].x_landscape = 33;
    roboLandscapePlan[8].y_landscape = 8;
    roboLandscapePlan[8].type_landscape = 5;
    roboLandscapePlan[9].x_landscape = 33;
    roboLandscapePlan[9].y_landscape = 9;
    roboLandscapePlan[9].type_landscape = 5;
    roboLandscapePlan[10].x_landscape = 36;
    roboLandscapePlan[10].y_landscape = 11;
    roboLandscapePlan[10].type_landscape = 0;
    roboLandscapePlan[11].x_landscape = 40;
    roboLandscapePlan[11].y_landscape = 9;
    roboLandscapePlan[11].type_landscape = 5;
    roboLandscapePlan[12].x_landscape = 42;
    roboLandscapePlan[12].y_landscape = 11;
    roboLandscapePlan[12].type_landscape = 0;
    roboLandscapePlan[13].x_landscape = 48;
    roboLandscapePlan[13].y_landscape = 11;
    roboLandscapePlan[13].type_landscape = 0;
    roboLandscapePlan[14].x_landscape = 54;
    roboLandscapePlan[14].y_landscape = 11;
    roboLandscapePlan[14].type_landscape = 0;
    roboLandscapePlan[15].x_landscape = 60;
    roboLandscapePlan[15].y_landscape = 8;
    roboLandscapePlan[15].type_landscape = 0;
    roboLandscapePlan[16].x_landscape = 60;
    roboLandscapePlan[16].y_landscape = 8;
    roboLandscapePlan[16].type_landscape = 10;
    roboLandscapePlan[17].x_landscape = 66;
    roboLandscapePlan[17].y_landscape = 8;
    roboLandscapePlan[17].type_landscape = 0;
    roboLandscapePlan[18].x_landscape = 72;
    roboLandscapePlan[18].y_landscape = 8;
    roboLandscapePlan[18].type_landscape = 0;
    roboLandscapePlan[19].x_landscape = 75;
    roboLandscapePlan[19].y_landscape = 5;
    roboLandscapePlan[19].type_landscape = 5;
    roboLandscapePlan[20].x_landscape = 75;
    roboLandscapePlan[20].y_landscape = 6;
    roboLandscapePlan[20].type_landscape = 5;
    roboLandscapePlan[21].x_landscape = 78;
    roboLandscapePlan[21].y_landscape = 8;
    roboLandscapePlan[21].type_landscape = 11;
    roboLandscapePlan[22].x_landscape = 78;
    roboLandscapePlan[22].y_landscape = 11;
    roboLandscapePlan[22].type_landscape = 0;
    roboLandscapePlan[23].x_landscape = 81;
    roboLandscapePlan[23].y_landscape = 4;
    roboLandscapePlan[23].type_landscape = 1;
    roboLandscapePlan[24].x_landscape = 87;
    roboLandscapePlan[24].y_landscape = 11;
    roboLandscapePlan[24].type_landscape = 0;
    roboLandscapePlan[25].x_landscape = 92;
    roboLandscapePlan[25].y_landscape = 9;
    roboLandscapePlan[25].type_landscape = 5;
    roboLandscapePlan[26].x_landscape = 93;
    roboLandscapePlan[26].y_landscape = 11;
    roboLandscapePlan[26].type_landscape = 0;
    roboLandscapePlan[27].x_landscape = 99;
    roboLandscapePlan[27].y_landscape = 11;
    roboLandscapePlan[27].type_landscape = 0;
    roboLandscapePlan[28].x_landscape = 105;
    roboLandscapePlan[28].y_landscape = 11;
    roboLandscapePlan[28].type_landscape = 0;
    roboLandscapePlan[29].x_landscape = 106;
    roboLandscapePlan[29].y_landscape = 8;
    roboLandscapePlan[29].type_landscape = 1;
    roboLandscapePlan[30].x_landscape = 109;
    roboLandscapePlan[30].y_landscape = 5;
    roboLandscapePlan[30].type_landscape = 1;
    roboLandscapePlan[31].x_landscape = 111;
    roboLandscapePlan[31].y_landscape = 11;
    roboLandscapePlan[31].type_landscape = 0;
    roboLandscapePlan[32].x_landscape = 112;
    roboLandscapePlan[32].y_landscape = 8;
    roboLandscapePlan[32].type_landscape = 1;
    roboLandscapePlan[33].x_landscape = 117;
    roboLandscapePlan[33].y_landscape = 11;
    roboLandscapePlan[33].type_landscape = 0;
    roboLandscapePlan[34].x_landscape = 126;
    roboLandscapePlan[34].y_landscape = 11;
    roboLandscapePlan[34].type_landscape = 0;
    roboLandscapePlan[35].x_landscape = 127;
    roboLandscapePlan[35].y_landscape = 9;
    roboLandscapePlan[35].type_landscape = 5;
    roboLandscapePlan[36].x_landscape = 132;
    roboLandscapePlan[36].y_landscape = 11;
    roboLandscapePlan[36].type_landscape = 0;
    roboLandscapePlan[37].x_landscape = 138;
    roboLandscapePlan[37].y_landscape = 11;
    roboLandscapePlan[37].type_landscape = 0;
    roboLandscapePlan[38].x_landscape = 141;
    roboLandscapePlan[38].y_landscape = 9;
    roboLandscapePlan[38].type_landscape = 5;
    roboLandscapePlan[39].x_landscape = 144;
    roboLandscapePlan[39].y_landscape = 11;
    roboLandscapePlan[39].type_landscape = 0;
    roboLandscapePlan[40].x_landscape = 145;
    roboLandscapePlan[40].y_landscape = 7;
    roboLandscapePlan[40].type_landscape = 5;
    roboLandscapePlan[41].x_landscape = 145;
    roboLandscapePlan[41].y_landscape = 8;
    roboLandscapePlan[41].type_landscape = 5;
    roboLandscapePlan[42].x_landscape = 150;
    roboLandscapePlan[42].y_landscape = 8;
    roboLandscapePlan[42].type_landscape = 0;
    roboLandscapePlan[43].x_landscape = 150;
    roboLandscapePlan[43].y_landscape = 8;
    roboLandscapePlan[43].type_landscape = 10;
    roboLandscapePlan[44].x_landscape = 155;
    roboLandscapePlan[44].y_landscape = 4;
    roboLandscapePlan[44].type_landscape = 1;
    roboLandscapePlan[45].x_landscape = 156;
    roboLandscapePlan[45].y_landscape = 8;
    roboLandscapePlan[45].type_landscape = 0;
    roboLandscapePlan[46].x_landscape = 162;
    roboLandscapePlan[46].y_landscape = 8;
    roboLandscapePlan[46].type_landscape = 0;
    roboLandscapePlan[47].x_landscape = 165;
    roboLandscapePlan[47].y_landscape = 5;
    roboLandscapePlan[47].type_landscape = 5;
    roboLandscapePlan[48].x_landscape = 165;
    roboLandscapePlan[48].y_landscape = 6;
    roboLandscapePlan[48].type_landscape = 5;
    roboLandscapePlan[49].x_landscape = 168;
    roboLandscapePlan[49].y_landscape = 8;
    roboLandscapePlan[49].type_landscape = 11;
    roboLandscapePlan[50].x_landscape = 168;
    roboLandscapePlan[50].y_landscape = 11;
    roboLandscapePlan[50].type_landscape = 0;
    roboLandscapePlan[51].x_landscape = 173;
    roboLandscapePlan[51].y_landscape = 9;
    roboLandscapePlan[51].type_landscape = 5;
    roboLandscapePlan[52].x_landscape = 174;
    roboLandscapePlan[52].y_landscape = 11;
    roboLandscapePlan[52].type_landscape = 0;
    roboLandscapePlan[53].x_landscape = 179;
    roboLandscapePlan[53].y_landscape = 8;
    roboLandscapePlan[53].type_landscape = 5;
    roboLandscapePlan[54].x_landscape = 179;
    roboLandscapePlan[54].y_landscape = 9;
    roboLandscapePlan[54].type_landscape = 5;
    roboLandscapePlan[55].x_landscape = 180;
    roboLandscapePlan[55].y_landscape = 11;
    roboLandscapePlan[55].type_landscape = 0;
    roboLandscapePlan[56].x_landscape = 185;
    roboLandscapePlan[56].y_landscape = 9;
    roboLandscapePlan[56].type_landscape = 5;
    roboLandscapePlan[57].x_landscape = 186;
    roboLandscapePlan[57].y_landscape = 11;
    roboLandscapePlan[57].type_landscape = 0;
    roboLandscapePlan[58].x_landscape = 192;
    roboLandscapePlan[58].y_landscape = 11;
    roboLandscapePlan[58].type_landscape = 0;
    roboLandscapePlan[59].x_landscape = 197;
    roboLandscapePlan[59].y_landscape = 9;
    roboLandscapePlan[59].type_landscape = 6;
    roboLandscapePlan[60].x_landscape = 198;
    roboLandscapePlan[60].y_landscape = 11;
    roboLandscapePlan[60].type_landscape = 0;
    roboLandscapePlan[61].x_landscape = 199;
    roboLandscapePlan[61].y_landscape = 7;
    roboLandscapePlan[61].type_landscape = 6;
    roboLandscapePlan[62].x_landscape = 199;
    roboLandscapePlan[62].y_landscape = 9;
    roboLandscapePlan[62].type_landscape = 5;
    roboLandscapePlan[63].x_landscape = 201;
    roboLandscapePlan[63].y_landscape = 10;
    roboLandscapePlan[63].type_landscape = 4;
    roboLandscapePlan[64].x_landscape = 203;
    roboLandscapePlan[64].y_landscape = 1;
    roboLandscapePlan[64].type_landscape = 7;
    roboLandscapePlan[65].x_landscape = 203;
    roboLandscapePlan[65].y_landscape = 3;
    roboLandscapePlan[65].type_landscape = 5;
    roboLandscapePlan[66].x_landscape = 203;
    roboLandscapePlan[66].y_landscape = 5;
    roboLandscapePlan[66].type_landscape = 5;
    roboLandscapePlan[67].x_landscape = 203;
    roboLandscapePlan[67].y_landscape = 7;
    roboLandscapePlan[67].type_landscape = 5;
    roboLandscapePlan[68].x_landscape = 204;
    roboLandscapePlan[68].y_landscape = 11;
    roboLandscapePlan[68].type_landscape = 0;
    roboLandscapePlan[69].x_landscape = 205;
    roboLandscapePlan[69].y_landscape = 6;
    roboLandscapePlan[69].type_landscape = 5;
    roboLandscapePlan[70].x_landscape = 207;
    roboLandscapePlan[70].y_landscape = 5;
    roboLandscapePlan[70].type_landscape = 7;
    roboLandscapePlan[71].x_landscape = 207;
    roboLandscapePlan[71].y_landscape = 7;
    roboLandscapePlan[71].type_landscape = 5;
    roboLandscapePlan[72].x_landscape = 207;
    roboLandscapePlan[72].y_landscape = 10;
    roboLandscapePlan[72].type_landscape = 4;
    roboLandscapePlan[73].x_landscape = 209;
    roboLandscapePlan[73].y_landscape = 7;
    roboLandscapePlan[73].type_landscape = 7;
    roboLandscapePlan[74].x_landscape = 210;
    roboLandscapePlan[74].y_landscape = 11;
    roboLandscapePlan[74].type_landscape = 0;
    roboLandscapePlan[75].x_landscape = 219;
    roboLandscapePlan[75].y_landscape = 11;
    roboLandscapePlan[75].type_landscape = 4;
    roboLandscapePlan[76].x_landscape = 222;
    roboLandscapePlan[76].y_landscape = 8;
    roboLandscapePlan[76].type_landscape = 9;
    roboLandscapePlan[77].x_landscape = 225;
    roboLandscapePlan[77].y_landscape = 7;
    roboLandscapePlan[77].type_landscape = 9;
    roboLandscapePlan[78].x_landscape = 225;
    roboLandscapePlan[78].y_landscape = 11;
    roboLandscapePlan[78].type_landscape = 4;
    roboLandscapePlan[79].x_landscape = 228;
    roboLandscapePlan[79].y_landscape = 6;
    roboLandscapePlan[79].type_landscape = 9;
    roboLandscapePlan[80].x_landscape = 231;
    roboLandscapePlan[80].y_landscape = 11;
    roboLandscapePlan[80].type_landscape = 4;
    roboLandscapePlan[81].x_landscape = 232;
    roboLandscapePlan[81].y_landscape = 6;
    roboLandscapePlan[81].type_landscape = 9;
    roboLandscapePlan[82].x_landscape = 235;
    roboLandscapePlan[82].y_landscape = 5;
    roboLandscapePlan[82].type_landscape = 9;
    roboLandscapePlan[83].x_landscape = 237;
    roboLandscapePlan[83].y_landscape = 11;
    roboLandscapePlan[83].type_landscape = 4;
    roboLandscapePlan[84].x_landscape = 238;
    roboLandscapePlan[84].y_landscape = 4;
    roboLandscapePlan[84].type_landscape = 9;
    roboLandscapePlan[85].x_landscape = 241;
    roboLandscapePlan[85].y_landscape = 3;
    roboLandscapePlan[85].type_landscape = 5;
    roboLandscapePlan[86].x_landscape = 241;
    roboLandscapePlan[86].y_landscape = 5;
    roboLandscapePlan[86].type_landscape = 5;
    roboLandscapePlan[87].x_landscape = 241;
    roboLandscapePlan[87].y_landscape = 7;
    roboLandscapePlan[87].type_landscape = 5;
}

void roboMapInit()
{
    if( roboRunningLevel == 1 ) roboMapInitLevel1();
    else if( roboRunningLevel == 5 ) roboMapInitLevel5();
    else if( roboRunningLevel == 9 || roboRunningLevel == 13 || roboRunningLevel == 17 ) roboMapInitLevel9();
}


// -----------------------------------------------------------------------------
// Init() - real upstream's own real level-transition dispatcher, split
// into roboInitNext()/roboInitPrev() (see this file's own header comment
// for why).
// -----------------------------------------------------------------------------

void roboInitCommon()
{
    if( roboRunningLevel % 4 == 1 ) roboPlayerInit();
    else
    {
        roboPlayer.x_world = 0;
        roboPlayer.pos = 0;
    }
    roboBulletInit();
    roboMapInit();
    roboEnnemyInit();

    if( roboRunningLevel == 1 ) { roboLevelLength = 1100; roboTileNumber = 43; roboEnnemiesNumber = 7; }
    else if( roboRunningLevel == 2 ) { roboLevelLength = 0; roboEnnemiesNumber = 1; }
    else if( roboRunningLevel == 5 ) { roboLevelLength = 967; roboTileNumber = 88; roboEnnemiesNumber = 13; }
    else if( roboRunningLevel == 6 ) { roboLevelLength = 0; roboEnnemiesNumber = 1; }
    else if( roboRunningLevel == 9 ) { roboLevelLength = 967; roboTileNumber = 88; roboEnnemiesNumber = 13; }
    else if( roboRunningLevel == 10 ) { roboLevelLength = 0; roboEnnemiesNumber = 1; }
    else if( roboRunningLevel == 13 ) { roboLevelLength = 967; roboTileNumber = 88; roboEnnemiesNumber = 13; }
    else if( roboRunningLevel == 14 ) { roboLevelLength = 0; roboEnnemiesNumber = 1; }
    else if( roboRunningLevel == 17 ) { roboLevelLength = 967; roboTileNumber = 88; roboEnnemiesNumber = 13; }
    else if( roboRunningLevel == 18 ) { roboLevelLength = 0; roboEnnemiesNumber = 1; }
}

void roboInitNext()
{
    roboRunningLevel = roboRunningLevel + 1;
    roboInitCommon();
}

void roboInitPrev()
{
    roboRunningLevel = roboRunningLevel - 1;
    roboPlayer.lives = roboPlayer.lives - 1;
    roboPlayer.life = 6;
    roboInitCommon();
}

// -----------------------------------------------------------------------------
// State dispatch - real Robot.ino's own loop() switch(runningLevel%4),
// split into one function per real case.
// -----------------------------------------------------------------------------

// real runningLevel%4==0 - "Level N" title card + player_goButton()
void roboUpdateIntro()
{
    roboLevelDrawIntro();
    if( gbPressed( BTN_B ) )
      roboInitNext();
}

// real runningLevel%4==1 - platforming
void roboUpdatePlatform()
{
    roboLevelDrawLandscape();
    roboPlayerCheckJump();
    if( roboPlayer.jumpStatus < 4 )
      roboPlayerCheckVerticalPos();

    if( !roboFnctnCheckButtons() ) return; // real blocking mid-game titleScreen() - see header comment

    gbClear();
    roboLevelDrawLandscape();
    roboPlayerDraw();
    roboEnnemyDraw();
    roboPlayerCheckEnnemyCollision();
    roboPlayerCheckDeath();
    roboPlayerCheckLevelEnd();
    roboEnnemyMove();
    roboBulletMove();
    roboBulletDraw();
    roboPlayerDrawHud();
    roboLevelDrawBackground();
}

// real runningLevel%4==2 - boss approach walk-in
void roboUpdateBossApproach()
{
    roboDrawBossArena();
    roboPlayerDraw();
    roboEnnemyDraw();
    roboPlayerCheckVerticalPos();
    if( roboEnnemies[ 0 ].x > roboEnnemies[ 0 ].x_min )
      roboEnnemyMove();
    else
    {
        if( roboLevelLength < 48 ) roboLevelLength = roboLevelLength + 1;
        else roboInitNext();
    }
}

// real runningLevel%4==3 - boss fight (no player_checkDeath()/
// player_checkLevelEnd() here at all - see header comment)
void roboUpdateBossFight()
{
    roboDrawBossArena();
    roboPlayerCheckJump();
    if( roboPlayer.jumpStatus < 4 )
      roboPlayerCheckVerticalPos();

    if( !roboFnctnCheckButtons() ) return; // real blocking mid-game titleScreen() - see header comment

    gbClear();
    roboDrawBossArena();
    roboPlayerDraw();
    roboEnnemyDraw();
    roboPlayerCheckEnnemyCollision();
    roboEnnemyMove();
    roboBulletMove();
    roboBulletDraw();
    roboPlayerDrawHud();
}

// real setup()'s own blocking gb.titleScreen(gamelogo) - a real ONE-
// argument overload (bitmap only, no name text) - see header comment.
void roboUpdateSplash()
{
    gbDrawBitmap( 0, 12, roboGamelogo );

    if( roboAGate )
    {
        if( !gbHeld( BTN_A, 1 ) ) roboAGate = false;
        return;
    }
    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        roboShowingSplash = false;
    }
}

// real mid-game gb.titleScreen() (Button C, zero-argument overload) - see
// header comment.
void roboUpdateFreeze()
{
    gbCursorX = 0;
    gbCursorY = 20;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        roboFrozen = false;
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameRobot_init()
{
    gbBegin();

    roboRunningLevel = 0;
    roboPlayer.lives = 5;
    roboPlayer.life = 6;
    roboPlayer.score = 0;

    roboShowingSplash = true;
    roboAGate = true; // suppress the still-held menu-launch A press
    roboFrozen = false;
}

void gameRobot_update()
{
    if( !gbUpdate() ) return;

    if( roboShowingSplash )
    {
        roboUpdateSplash();
        gbRenderFrame();
        return;
    }

    if( roboFrozen )
    {
        roboUpdateFreeze();
        gbRenderFrame();
        return;
    }

    int dispatch = roboRunningLevel % 4;
    if( dispatch == 0 ) roboUpdateIntro();
    else if( dispatch == 1 ) roboUpdatePlatform();
    else if( dispatch == 2 ) roboUpdateBossApproach();
    else if( dispatch == 3 ) roboUpdateBossFight();

    gbRenderFrame();
}
