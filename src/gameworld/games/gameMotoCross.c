#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// MotoCross (Clement83, none specified). A small, unfinished-feeling
// side-scrolling motocross tech demo: the rider stays fixed at the left
// edge of the screen (`player1.x` is genuinely never assigned anywhere
// upstream past its own struct-initializer 0) while the world - a long
// procedurally-scattered strip of ramps ("tremplin") and bumps ("bosse") -
// scrolls underneath at the rider's own current speed. Button A
// accelerates, Button B brakes, Button RIGHT leans forward (transferring
// upward jump velocity into forward speed while airborne), Button LEFT
// cycles the rider's own wheelie pose. There is no scoring, no lap/finish
// line logic, and no game-over path of any kind - `GAME_OVER`/
// `MOTO_STATE_END_TRACK` are both real, defined-but-dead upstream states,
// reproduced here exactly as inert for the same reason (never actually
// reachable in the real, as-shipped source either).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment). `gb.collideBitmapBitmap()` ->
// `gbCollideBitmapBitmap()` directly - a real, already-shipped shim
// primitive.
//
// REAL BITMAP ART: all 8 real upstream sprites actually used in-game
// (`MotoFinish`/`moto1`/`motoCabre`/`motoRoueAv`/`motoTomber` - the
// rider's 5 real poses; `bosse`/`tramplin` - the two obstacle types;
// `bottesFoins` - the decorative hay-bale prop) were already stored as
// real `0x`-hex byte literals upstream (no `B01111111`-style binary
// literals needing conversion) - copied byte-for-byte into `int[]`
// arrays with their own real width/height headers, every array's real
// total element count verified by computing `2 + ceil(width/8)*height`
// against the actual literal count rather than hand-trusted. Upstream's
// own `spriteSheet[]` (a 200x188 sheet, `motoCross.ino`'s own single
// largest array) is genuinely never referenced by any `drawBitmap()` call
// anywhere in any of the 3 real upstream `.ino` files - real dead asset
// data, not ported here either.
//
// BLOCKING `gb.titleScreen(TitleScreen)` -> EXPLICIT STATE: upstream's
// own real `goTitleScreen()` (called once from `setup()`'s own
// `initPrograme()`, and again from `loop()` any time Button C is
// pressed) blocks on this real bitmap-titlescreen widget - ported as
// `MOTOX_STATE_TITLE`, this project's own established "blocking widget ->
// explicit resumable state" treatment (see gamePong.c's own header
// comment). Real upstream's own `initPlayer()`/`initElements()` calls
// only ever run ONCE, right after the very first (`setup()`-time) title
// dismiss, never again on any later Button-C-triggered title screen (the
// real `gameState` variable is untouched by `goTitleScreen()`, so
// gameplay resumes exactly where it left off once a later title screen is
// dismissed) - matched here by calling the equivalent resets once, from
// this port's own `gameMotoCross_init()`, and never re-calling them from
// `motoxUpdateTitle()` on dismiss (the same precedent already established
// by `gamePongLocalMultiplayer.c`'s own `PONGLM_STATE_TITLE`/
// `PONGLM_STATE_PAUSE` split). Real upstream's own trivial single-tick
// `MAIN_MENU -> IN_GAME` pass-through state is skipped entirely here
// (nothing is ever drawn or read while it's active) - a fresh Button A
// press on the title screen goes straight to `MOTOX_STATE_PLAYING`,
// matching every other bitmap-title game in this catalog's own "one
// A-press, no intermediate state" precedent (`gameSavePrincesse.c`,
// `gameArtillery.c`).
//
// TWO REAL UPSTREAM LOGIC BUGS, PRESERVED EXACTLY, NOT "FIXED" - both in
// `updatePlayer()`'s own wheelie-state cycling:
//   1. The Button-RIGHT (forward-lean) branch tests
//      `player1.state == MOTO_STATE_ROUE_AR` twice in a row
//      (`if(...) {...} else if(...) {...}` with the identical condition
//      on both arms) - the second arm is genuine, real dead code, never
//      reachable on real hardware either. Reproduced literally below.
//   2. The Button-LEFT (wheelie-cycle) branch's final `else if` is
//      missing its own `player1.state ==` comparison entirely -
//      `else if(MOTO_STATE_ROUE_AR)` tests the bare macro value (1, a
//      nonzero constant) instead of comparing it to the current state, so
//      that branch is unconditionally true whenever the first two arms
//      fail to match. Real, load-bearing behavior on real hardware: while
//      Button LEFT is held, any state other than ROUE_AV or NORMAL
//      (including ROUE_AR itself) falls straight through to
//      `MOTO_STATE_KO` (the "fallen off" pose) every 10th frame. Ported
//      as the literal bare-int condition `if( MOTOX_MOTO_ROUE_AR )` below
//      - a nonzero int constant is a valid, always-true condition in this
//      dialect exactly like in real C, so the real bug reproduces
//      byte-for-byte without any special-casing.
//
// Real upstream's own `drawPlayer()` calls `gb.display.print(player1.vx)`
// unconditionally every frame, with no `setCursor()` of its own - since
// this shim's own `gbUpdate()` resets the cursor to (0,0) every real tick
// (unlike real hardware, which only zeroes it once at boot), this always
// prints in the top-left corner, a real, undocumented debug speed readout
// left in the shipped game. FIXED, NOT PRESERVED-AS-A-LIMITATION: this
// game's own port originally had no float-print primitive to call at all
// (`gbPrintNumber()` only ever took `int`), so it was ported as
// `gbPrintNumber( (int)motoxPlayerVx )` - the truncated integer part,
// missing the fractional digit real hardware would have shown. Fixed once
// `gbPrintFloat()` was promoted to the shared shim (`gamebuinoShim.h`/
// `.c`, a direct port of real Arduino's own `Print::printFloat()`
// algorithm) after `gameAgaruino.c` was independently found to need the
// exact same primitive for its own real "Taille : " float readout - this
// file's own call site now prints the genuine, real fractional speed
// value exactly like real hardware does.
//
// Converting Vircon32's own C dialect back to standard C: every
// `TYPE[N] name;`/`TYPE[N] name = {...};` declaration (including the
// float-array level-element tables and the numeric bitmap arrays below)
// was rewritten to standard C array-declaration order,
// `TYPE name[N];`/`TYPE name[N] = {...};`. No structs are used anywhere
// in this file. Every bitmap data array below was copied byte-for-byte
// from the source, not retyped from memory.

#define MOTOX_STATE_TITLE 0
#define MOTOX_STATE_PLAYING 1

#define MOTOX_MOTO_NORMAL 0
#define MOTOX_MOTO_ROUE_AR 1
#define MOTOX_MOTO_ROUE_AV 2
#define MOTOX_MOTO_END_TRACK 3
#define MOTOX_MOTO_KO 4

#define MOTOX_ACC 0.15
#define MOTOX_ENGINE_BREAK 0.01
#define MOTOX_BREAK 0.2
#define MOTOX_MAX_VITT 5
#define MOTOX_GRAVITY 0.2
#define MOTOX_TREMPLIN_VY 0.18
#define MOTOX_SOL_Y 27
#define MOTOX_FROTTEMENT_OBSTACLE 0.95
#define MOTOX_TRANSFER_FORCE 0.01

#define MOTOX_NB_ELEMENT_INGAME 40

int motoxMotoFinishBitmap[74] = {
    24, 24, 0x1, 0x0, 0x0, 0x1, 0xC0, 0x0, 0x79, 0xC2, 0x0, 0xFD, 0x4, 0xE0, 0xFF, 0xA5,
    0xB0, 0x63, 0xBF, 0x18, 0x6B, 0x3F, 0x8, 0x7E, 0xCF, 0xC8, 0x3F, 0x9D, 0x18, 0x7F, 0xFC, 0xB0,
    0x7F, 0xF8, 0xE0, 0x7F, 0xFC, 0x0, 0x3F, 0xFE, 0x0, 0x1F, 0x7E, 0x0, 0xF, 0x3C, 0x0, 0x3,
    0x78, 0x0, 0xD, 0xF0, 0x0, 0x19, 0xB0, 0x0, 0x13, 0xB8, 0x0, 0x2, 0x28, 0x0, 0x2, 0x68,
    0x0, 0x3, 0x18, 0x0, 0x1, 0xB0, 0x0, 0x0, 0xE0, 0x0
};

int motoxMoto1Bitmap[65] = {
    24, 21, 0x0, 0xF0, 0x0, 0x1, 0xF8, 0x0, 0x1, 0xFC, 0x0, 0x1, 0x80, 0x0, 0x1, 0xE8,
    0x0, 0x1, 0xFC, 0x0, 0x3, 0xE0, 0x0, 0x7, 0xC0, 0x0, 0x7, 0xFA, 0x0, 0x7, 0xFE, 0x0,
    0x7, 0x86, 0x0, 0x3F, 0xDF, 0xC0, 0x7, 0xFE, 0x20, 0x3B, 0xFF, 0xC0, 0x6F, 0xFB, 0x60, 0xC6,
    0xF7, 0x30, 0x97, 0xF5, 0x90, 0x9F, 0xE4, 0x90, 0xC7, 0x76, 0x30, 0x6C, 0x23, 0x60, 0x38, 0x1,
    0xC0
};

int motoxMotoCabreBitmap[71] = {
    24, 23, 0x1E, 0x0, 0x0, 0x3F, 0x0, 0x0, 0x3F, 0x80, 0x0, 0x30, 0x0, 0x0, 0x3D, 0x0,
    0x0, 0x3F, 0xA0, 0x0, 0x3E, 0x24, 0x0, 0x3F, 0xFC, 0x0, 0x3F, 0xB3, 0x80, 0x3C, 0x7E, 0xC0,
    0x3F, 0x7C, 0x60, 0x1F, 0xFC, 0x20, 0x1F, 0xEF, 0x20, 0x1F, 0xCC, 0x60, 0xFF, 0xC6, 0xC0, 0x3B,
    0xE3, 0x80, 0x7F, 0xF0, 0x0, 0xDE, 0xE0, 0x0, 0x86, 0x0, 0x0, 0x9E, 0x0, 0x0, 0xC6, 0x0,
    0x0, 0x6C, 0x0, 0x0, 0x38, 0x0, 0x0
};

int motoxMotoRoueAvBitmap[71] = {
    24, 23, 0x0, 0x3C, 0x0, 0x0, 0x7E, 0x0, 0x0, 0x7F, 0x0, 0x0, 0xE0, 0x0, 0x1, 0xFA,
    0x0, 0x1, 0xFF, 0x0, 0x3, 0xE0, 0x0, 0x33, 0xF0, 0x0, 0x1F, 0xF8, 0x0, 0x7, 0xFC, 0x0,
    0x3F, 0xFE, 0x80, 0x6F, 0xF3, 0x0, 0xCF, 0xFF, 0x0, 0x97, 0xFF, 0x80, 0x9F, 0xFE, 0x40, 0xC7,
    0xFF, 0x80, 0x6C, 0xC6, 0xC0, 0x38, 0xEE, 0x60, 0x0, 0xA, 0x20, 0x0, 0xB, 0x20, 0x0, 0xC,
    0x60, 0x0, 0x6, 0xC0, 0x0, 0x3, 0x80
};

int motoxMotoTomberBitmap[35] = {
    24, 11, 0x0, 0x0, 0xF8, 0x0, 0x1, 0xEC, 0x0, 0x1, 0xCC, 0x1, 0x87, 0xDC, 0x7F, 0xFF,
    0xF8, 0x7F, 0xFF, 0xF8, 0xC7, 0xFD, 0x38, 0x9F, 0xFB, 0x70, 0xFF, 0x7B, 0xF0, 0xFF, 0x3B, 0xE0,
    0x7E, 0x1, 0xC0
};

int motoxBosseBitmap[14] = {
    24, 4, 0x0, 0x1E, 0x0, 0x38, 0x3F, 0x0, 0x7C, 0x7F, 0x80, 0xFF, 0xFF, 0xF8
};

int motoxTramplinBitmap[47] = {
    24, 15, 0x0, 0x0, 0x8, 0x0, 0x0, 0x38, 0x0, 0x0, 0x78, 0x0, 0x1, 0xF8, 0x0, 0x3,
    0xF8, 0x0, 0x7, 0xF8, 0x0, 0xF, 0xF8, 0x0, 0x3F, 0xF8, 0x0, 0xFF, 0xF8, 0x1, 0xFF, 0xF8,
    0x7, 0xFF, 0xF8, 0xF, 0xFF, 0xF8, 0x3F, 0xFF, 0xF8, 0xFF, 0xFF, 0xF8, 0xFF, 0xFF, 0xF8
};

int motoxBottesFoinsBitmap[10] = {
    8, 8, 0x18, 0x38, 0xF8, 0x78, 0x38, 0x18, 0x8, 0x8
};

int motoxTitleScreenBitmap[290] = {
    64, 36, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0xD, 0x99, 0xE6, 0x0, 0x0, 0x0, 0x0, 0x0, 0xA, 0xA4, 0x89,
    0x0, 0x0, 0x0, 0x0, 0x0, 0xA, 0xA4, 0x89, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0xA4, 0x89,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x98, 0x86, 0x0, 0x0, 0x0, 0x40, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x70, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1E, 0x70, 0x80, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x3F, 0x41, 0x38, 0x1, 0xDC, 0x67, 0xBC, 0x0, 0x3F, 0xE9, 0x6C, 0x2, 0x12, 0x94,
    0xA4, 0x0, 0x18, 0xEF, 0xC6, 0x2, 0x1E, 0x92, 0x10, 0x0, 0x1A, 0xCF, 0xC2, 0x2, 0x14, 0x91,
    0x8C, 0x0, 0x1F, 0xB3, 0xF2, 0x1, 0xD2, 0x67, 0xBC, 0x0, 0xF, 0xE7, 0x46, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x1F, 0xFF, 0x2C, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1F, 0xFE, 0x38, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x1F, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF, 0xFF, 0x80, 0x0, 0x0, 0x1F,
    0xF8, 0x0, 0x7, 0xDF, 0x80, 0x0, 0x0, 0x70, 0x7, 0x0, 0x3, 0xCF, 0x0, 0x0, 0x1, 0xC0,
    0x1, 0xE0, 0x0, 0xDE, 0x0, 0x0, 0xF, 0x0, 0x0, 0x38, 0x3, 0x7C, 0x0, 0x0, 0x38, 0x0,
    0x0, 0xE, 0x6, 0x6C, 0x0, 0x0, 0xE0, 0x0, 0x0, 0x3, 0x4, 0xEE, 0x0, 0x1, 0x80, 0x0,
    0x0, 0x1, 0x0, 0x8A, 0x0, 0x1, 0x0, 0x0, 0xF8, 0x0, 0x0, 0x9A, 0x0, 0x2, 0x0, 0x7,
    0xF, 0xF8, 0x0, 0xC6, 0x0, 0x6, 0x0, 0x18, 0x0, 0xC, 0x0, 0x6C, 0x0, 0xC, 0x0, 0x60,
    0x0, 0x6, 0x0, 0x38, 0x0, 0x30, 0x1, 0xC0, 0x0, 0x3, 0x0, 0x0, 0x0, 0x60, 0x7, 0x0,
    0x0, 0x1, 0x0, 0x0, 0x0, 0xC0, 0xC, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x0, 0x18, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x6, 0x0, 0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x40, 0x0,
    0x0, 0x0
};

int motoxState;

int motoxPlayerState;
float motoxPlayerVx;
float motoxPlayerVy;
float motoxPlayerX;
float motoxPlayerY;

float motoxElemPosX[MOTOX_NB_ELEMENT_INGAME];
int motoxElemType[MOTOX_NB_ELEMENT_INGAME];

float motoxBottesPosX;

// Real upstream `player1.sprites[NB_MOTO_STATE]` (an array of bitmap
// pointers stored inside the player struct) - ported as a small if/else
// lookup function instead of a global array of pointers, matching this
// project's own established caution around pointer-typed struct/array
// globals (see gameSolitaire.c's own header comment).
int* motoxGetSprite( int state )
{
    if( state == MOTOX_MOTO_NORMAL )    return motoxMoto1Bitmap;
    if( state == MOTOX_MOTO_ROUE_AR )   return motoxMotoCabreBitmap;
    if( state == MOTOX_MOTO_ROUE_AV )   return motoxMotoRoueAvBitmap;
    if( state == MOTOX_MOTO_END_TRACK ) return motoxMotoFinishBitmap;
    return motoxMotoTomberBitmap; // MOTOX_MOTO_KO
}

void motoxInitPlayer()
{
    motoxPlayerState = MOTOX_MOTO_NORMAL;
    motoxPlayerVx = 0;
    motoxPlayerVy = 0;
    motoxPlayerX = 0;
    motoxPlayerY = MOTOX_SOL_Y;
}

void motoxInitElements()
{
    int i;
    for( i = 0; i < MOTOX_NB_ELEMENT_INGAME; i++ )
    {
        motoxElemPosX[ i ] = ( 88 + arand( 250 - 88 ) ) * ( i + 1 );
        motoxElemType[ i ] = ( (int)motoxElemPosX[ i ] ) % 2;
    }

    motoxBottesPosX = 50;
}

void motoxUpdatePlayer()
{
    if( gbRepeat( BTN_A, 1 ) )
      motoxPlayerVx = motoxPlayerVx + MOTOX_ACC;
    else
      motoxPlayerVx = motoxPlayerVx - MOTOX_ENGINE_BREAK;

    if( gbRepeat( BTN_B, 1 ) )
      motoxPlayerVx = motoxPlayerVx - MOTOX_BREAK;

    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        if( motoxPlayerVy < 0 )
        {
            float temp = motoxPlayerVy - ( motoxPlayerVy * MOTOX_TRANSFER_FORCE );
            motoxPlayerVx = motoxPlayerVx + temp;
            motoxPlayerVy = motoxPlayerVy - temp;
            if( motoxPlayerState == MOTOX_MOTO_ROUE_AR )
              motoxPlayerState = MOTOX_MOTO_NORMAL;
            else if( motoxPlayerState == MOTOX_MOTO_ROUE_AR ) // real upstream dead branch - see this file's own header comment
              motoxPlayerState = MOTOX_MOTO_ROUE_AV;
        }
    }

    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        if( gbFrameCount % 10 == 0 )
        {
            if( motoxPlayerState == MOTOX_MOTO_ROUE_AV )
              motoxPlayerState = MOTOX_MOTO_NORMAL;
            else if( motoxPlayerState == MOTOX_MOTO_NORMAL )
              motoxPlayerState = MOTOX_MOTO_ROUE_AR;
            else if( MOTOX_MOTO_ROUE_AR ) // real upstream bug: missing "motoxPlayerState ==" - see this file's own header comment; always true
              motoxPlayerState = MOTOX_MOTO_KO;
        }
    }

    if( motoxPlayerVx > MOTOX_MAX_VITT )
      motoxPlayerVx = MOTOX_MAX_VITT;
    else if( motoxPlayerVx < 0.0 )
      motoxPlayerVx = 0;

    int i;
    for( i = 0; i < MOTOX_NB_ELEMENT_INGAME; i++ )
    {
        int elemY;
        int* elemBitmap;
        if( motoxElemType[ i ] == 0 )
        {
            elemY = 33;
            elemBitmap = motoxTramplinBitmap;
        }
        else
        {
            elemY = 44;
            elemBitmap = motoxBosseBitmap;
        }

        if( gbCollideBitmapBitmap( (int)motoxElemPosX[ i ], elemY, elemBitmap, (int)motoxPlayerX, (int)motoxPlayerY, motoxGetSprite( motoxPlayerState ) ) )
        {
            while( gbCollideBitmapBitmap( (int)motoxElemPosX[ i ], elemY, elemBitmap, (int)motoxPlayerX, (int)motoxPlayerY, motoxGetSprite( motoxPlayerState ) ) )
              motoxPlayerY = motoxPlayerY - 1; // don't let the rider sink into the ground/obstacle

            motoxPlayerVx = motoxPlayerVx * MOTOX_FROTTEMENT_OBSTACLE;
            motoxPlayerVy = 0;
            motoxPlayerVy = motoxPlayerVy - ( MOTOX_TREMPLIN_VY * motoxPlayerVx );
            motoxPlayerState = MOTOX_MOTO_ROUE_AR;
            break;
        }
    }

    motoxPlayerY = motoxPlayerY + motoxPlayerVy;

    if( motoxPlayerY < MOTOX_SOL_Y )
    {
        motoxPlayerVy = motoxPlayerVy + MOTOX_GRAVITY;
    }
    else if( motoxPlayerY > MOTOX_SOL_Y )
    {
        motoxPlayerY = MOTOX_SOL_Y;
        motoxPlayerVy = 0;
        motoxPlayerState = MOTOX_MOTO_NORMAL;
    }
}

void motoxDrawPlayer()
{
    gbPrintFloat( motoxPlayerVx, 2 ); // real upstream debug speed readout - see this file's own header comment
    gbDrawBitmap( (int)motoxPlayerX, (int)motoxPlayerY, motoxGetSprite( motoxPlayerState ) );
}

// Real upstream `elements[i].vx` is assigned from `player1.vx` and then
// immediately consumed within the very same tick, never read again
// afterward - ported as a plain local instead of a redundant parallel
// array, functionally identical.
void motoxUpdateElements()
{
    int i;
    for( i = 0; i < MOTOX_NB_ELEMENT_INGAME; i++ )
      motoxElemPosX[ i ] = motoxElemPosX[ i ] - motoxPlayerVx;

    float bottesVx = motoxPlayerVx / 1.5;
    motoxBottesPosX = motoxBottesPosX - bottesVx;
    if( motoxBottesPosX < -16 )
      motoxBottesPosX = motoxBottesPosX + ( 88 + arand( 150 - 88 ) );
}

void motoxDrawElements()
{
    int i;
    for( i = 0; i < MOTOX_NB_ELEMENT_INGAME; i++ )
    {
        if( motoxElemPosX[ i ] > -16.0 && motoxElemPosX[ i ] < 84.0 )
        {
            if( motoxElemType[ i ] == 0 )
              gbDrawBitmap( (int)motoxElemPosX[ i ], 33, motoxTramplinBitmap );
            else
              gbDrawBitmap( (int)motoxElemPosX[ i ], 44, motoxBosseBitmap );
        }
    }

    gbSetColor( GB_GRAY );
    gbDrawBitmap( (int)motoxBottesPosX, 40, motoxBottesFoinsBitmap );
    gbSetColor( GB_BLACK );
}

void motoxDrawWorld()
{
    gbDrawLine( 0, 47, 84, 47 );
}

void motoxUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 10, 1, motoxTitleScreenBitmap );
    gbCursorX = 26;
    gbCursorY = 41;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      motoxState = MOTOX_STATE_PLAYING;
}

void motoxUpdatePlaying()
{
    if( gbPressed( BTN_C ) )
    {
        motoxState = MOTOX_STATE_TITLE;
        return;
    }

    // Real upstream's own updateWorld() is genuinely empty (a no-op stub)
    // - not called here at all, zero behavioral difference.
    motoxUpdatePlayer();
    motoxUpdateElements();

    motoxDrawWorld();
    motoxDrawElements();
    motoxDrawPlayer();
}

void gameMotoCross_init()
{
    gbBegin();
    gbPickRandomSeed();

    motoxState = MOTOX_STATE_TITLE;
    motoxInitPlayer();
    motoxInitElements();
}

void gameMotoCross_update()
{
    if( !gbUpdate() ) return;

    if( motoxState == MOTOX_STATE_PLAYING )
      motoxUpdatePlaying();
    else
      motoxUpdateTitle();

    gbRenderFrame();
}
