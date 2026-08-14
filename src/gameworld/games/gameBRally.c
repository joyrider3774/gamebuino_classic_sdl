#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// B-Rally (scmar, MIT license - github.com/scmar/B-Rally). A pseudo-3D
// "Pole Position"-style rally racer: steer a car down a scrolling,
// perspective-projected road full of curves/hills, dodge up to 10
// AI opponent cars plus left/right/lane road signs, and reach the finish
// as high up the field as possible across 10 named tracks (Easy1..
// Hardest2) of rising difficulty. Real upstream source spans six .ino
// files (`B-Rally.ino`, `adxl345.ino`, `running.ino`, `render.ino`,
// `pause.ino`, `eeprom.ino`) plus `Bitmaps.h`/`menu.h` - all consolidated
// here into one file, matching this project's own established multi-file
// upstream-consolidation precedent (e.g. gameShipwrek.c, gameCastleDefence.c).
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods). `byte`
// became plain `int`; `pgm_read_byte()`/PROGMEM are dropped outright (already
// no-ops in this shim) with the underlying byte tables copied verbatim as
// plain `int[]` arrays, hex/decimal literals kept as-is and the handful of
// Arduino-only `B10101010`-style binary literals converted to their exact
// decimal value (no bit pattern changed). `random()` is never called
// anywhere in real upstream (every track/opponent layout is fully
// deterministic upstream too - no RNG use to port).
//
// REAL BITMAP ART RESTORED FROM THE VERY FIRST PASS: the real "scenic" car
// sprite set (`scenic_l_0/l2_0/s_0/r2_0/r_0` + their `_1` animation frame and
// `_m` collision mask, matching real upstream's own actually-*shipped*
// `cars_0[]`/`cars_1[]`/`cars_m[]` tables in Bitmaps.h - the OTHER,
// differently-shaped "general car" sprite set (`car_l_0` etc) is commented
// out in real upstream's own Bitmaps.h and never actually used by any real
// cartridge, so it is correctly NOT ported here either - dead, unshipped
// art, not a fidelity loss), all 10 real opponent car sprites (`car1`..
// `car10`), the real scrolling hills background (`hills_new`, a 128-column
// x 3-page strip), the real road signs (`sign_left/right/lanes/finish`),
// the real bouncing "GAME OVER" letter sprites (`A/E/G/M/O/R/V`), and the
// real 3-frame countdown flag sprites (`sprite1/2/3`) were all extracted
// byte-for-byte from the real Bitmaps.h `const byte NAME[] PROGMEM = {...}`
// tables (Arduino `B`-binary literals converted to decimal, hex literals
// copied unchanged) via a small script that parsed the real header
// directly rather than hand-transcribing.
//
// A GENUINE UPSTREAM RENDERING TECHNIQUE, PORTED AS A DIFFERENT (but
// pixel-behavior-equivalent) IMPLEMENTATION: real upstream draws every
// frame by poking `gb.display.getBuffer()` directly byte-by-byte for
// speed (`renderHills()`/`renderFloor()`/`renderCar()` in render.ino).
// This shim has no exposed raw-framebuffer pointer (by design - see
// gamebuinoShim.h's own real drawing-primitive surface), so each of those
// three was reimplemented here using only the existing `gbDrawPixel()`/
// `gbSetColor()`/`gbFillRect()` primitives, chosen per real technique:
//   - `renderFloor()`'s own real checkerboard dither
//     (`mask1=B10101010`, flipped by `frameCount%2`, complemented for
//     alternating columns) is EXACTLY the same per-pixel formula this
//     shim's own real `GB_GRAY` color already implements
//     (`(x&1)^(y&1)` XORed against the low bit of `gbFrameCount`) - so
//     `brallyRenderFloor()` below is simply `gbSetColor(GB_GRAY)` +
//     one `gbFillRect()` call, not an approximation but the identical
//     dither math already proven elsewhere in this project.
//   - `renderHills()`'s own real per-byte background blit is ported as a
//     per-bit `gbDrawPixel()` loop over the real `hills_new` column data
//     (up to 84*24 = 2016 pixel draws once per real 20fps logic tick -
//     comfortably inside this platform's own real per-frame instruction
//     budget, see this project's own CLAUDE.md performance-pass writeup).
//   - `renderCar()`'s own real "AND mask, then OR ink" byte composite is
//     ported as an equivalent per-bit test (`brallyRenderCarFrame()`
//     below): wherever the real mask bit is 0 (opaque), the pixel is
//     force-drawn BLACK or WHITE depending on the matching ink bit -
//     wherever the mask bit is 1 (transparent), the pixel is left
//     untouched entirely, the same real "skip fully-transparent pixels"
//     shortcut the real AND/OR byte trick gives for free.
// `renderScaledSprite16()` (the real nearest-neighbor 16x16-to-NxN sprite
// downscaler used for opponents/signs/countdown/game-over letters, driven
// by real upstream's own `factors[]` per-scale-level index tables) is
// ported as `brallyRenderScaledSprite16()` using this shim's own real
// `gbGetBitmapPixel()` (a direct, matching-format port of real
// `Display::getBitmapPixel()`) to test each source bit instead of manual
// PROGMEM pointer/shift arithmetic - same real algorithm, same real
// `factors[]` tables (ported verbatim below as `brallyFactors`), just
// reading bits through an existing shim primitive instead of a raw
// pointer. No new shim primitive was needed for any of this.
//
// A REAL, DELIBERATE CONTROL-SCHEME "REDESIGN" THAT TURNED OUT TO ALREADY
// EXIST UPSTREAM: real upstream reads a real ADXL345 accelerometer over
// I2C (`adxl345.ino`) for tilt-based steering, gated the whole time behind
// `ACCAvail` (set true only if `Wire.beginTransmission(ACC)` actually
// finds a chip at boot). There is no such sensor here, so `ACCAvail` is
// simply never set true (the whole real `#ifdef WIREON` accelerometer
// branch, `adxl345.ino`, `calibrate()`, and the accelerometer-only
// `CALIBRATE` gamestate/`pausemenu2` "Calibrate Accel" menu entry are all
// dropped outright, not adapted). This is NOT a from-scratch control
// redesign, because real upstream's own `running()` already has a
// complete, independent, real digital-button steering path used on any
// real cartridge lacking the accelerometer chip (`ACCAvail==false`):
// Button LEFT/RIGHT steer (soft turn), Button B while turning sharpens it
// into a hard turn (`dir` 2->1, 4->5) while also braking, Button A
// accelerates, and releasing LEFT/RIGHT/A re-centers the wheel (`dir=3`).
// This port simply always takes that exact, already-real fallback path
// verbatim (the same `if (... ) && !ACCAvail` guards upstream itself
// already has - here the `!ACCAvail` half is always true, so those
// clauses collapse to their plain form) - real, upstream-authored digital
// controls, not an invented mapping.
//
// A REAL SIMPLIFICATION, CLEARLY DOCUMENTED: real upstream reads all 10
// track layouts (curve/hill/lane/sign data, one 4-byte record per distance
// unit) from a `B-RALLY.DAT` file on a real SD card via `petit_fatfs`/
// `Wire.h` SPI calls (`getNextSegment()`/`openTrackFile()` in B-Rally.ino).
// There is no SD card or filesystem on this platform, so the real per-track
// CSV-derived binary data is unavailable - but real upstream's OWN source
// already ships a real (if `#if 0`-commented-out, pre-petitfatfs)
// procedural fallback for exactly this situation: a `getNextSegment()`
// variant built entirely from `sin(dist/N)` curve/hill terms plus a
// look-ahead threshold test to place left/right curve-warning signs, left
// in B-Rally.ino's own source as a real, dead "Mockup-Track for before
// petitfatfs" comment block. `brallyGetNextSegment()` below is a direct,
// working port of that exact real formula (same curve/hill sine
// construction, same "signs appear when the curve bends more than 4 units
// within a 20-unit lookahead, gated every 5th distance unit" logic) -
// extended with a small per-track difficulty scale (steadily shorter sine
// periods and larger amplitudes as `brallyCurTrack` increases, see
// `brallyLevelStart()`) so the 10 named tracks still feel like a real
// difficulty progression despite generating their curves rather than
// reading them from a file. One deliberate departure from the dead
// upstream comment itself: that code reads the *global* `dist` directly
// inside `getNextSegment()`, which - had it ever shipped as-is on real
// hardware - would have generated the exact same segment content
// repeatedly whenever more than one segment is generated in a single tick
// (since `dist` is already fully advanced before any of that tick's
// `getNextSegment()` calls happen); real upstream never exercised this
// dead branch on any actual cartridge, so this isn't a preserved-quirk
// case - `brallyGetNextSegment()` instead advances its own dedicated
// `brallyGenPos` counter by exactly 1 per call, giving a correctly
// monotonic, gap-free curve on this platform. Every track is single-lap
// here (`brallyLapLength == brallyTotalLength` always) - real per-track
// lap counts also lived only in the unavailable file data.
//
// A REAL, PRESERVED UPSTREAM QUIRK: `restart()` (Pause -> Restart) seeds
// `opponents[i].d = i` (opponents bunched near the start) and does NOT
// re-run `segments[20-i].opp = i+1` the way `levelStart()` does - a real,
// literal difference between the two real upstream functions, not a
// porting slip. Ported verbatim as two distinct functions
// (`brallyLevelStart()`/`brallyRestart()`) rather than merged into one -
// harmless in practice on real hardware and here alike, since
// `updateOpps()` unconditionally re-derives which segment (if any) an
// opponent occupies every single tick regardless of this initial seeding.
//
// GENUINE UPSTREAM DEAD CODE, CONFIRMED AND NOT PORTED: the `signs==4`
// tunnel case and both real `renderTunnel()` overloads are unreachable in
// real upstream - the `tunnel` sprite is a real empty `{0,0}` bitmap, no
// segment generator (file-based or the dead mock one) ever assigns
// `signs=4`, and the one live-code call site for the 6-argument
// `renderTunnel()` is itself commented out in real `running.ino`. Neither
// is ported here.
//
// EEPROM: a direct, real port of upstream's own `eeprom.ino` via this
// shim's `eeprom_read_byte()`/`eeprom_write_byte()` (real address range
// 0-1023) - same real 3-byte magic-token first-run check
// (`EEPROMTOKEN1/2/3`), same real one-time full-region zero-fill on first
// run (`brallyClearEeprom()`), same real per-track 4-byte record (best
// position + 2-byte best time in ticks) at `track*4+6..8`. Unlike some
// other already-ported games' own EEPROM sections, upstream's own
// zero-fill here is load-bearing, not dead: `trySaveEeprom()` treats a
// stored value of exactly 0 as "no result yet" for a track, so the
// one-time full clear (rather than trusting this shim's own fresh-EEPROM
// 0xFF default) is genuinely required for that check to behave correctly,
// and is preserved exactly as upstream wrote it.
//
// A REAL, DOCUMENTED COSMETIC SIMPLIFICATION: `selectTrack()`'s own real
// hint line uses three real Gamebuino icon glyphs (`\25\26\27`); this port
// prints a plain-text equivalent ("A:go B(hold):clear C:back") instead -
// purely cosmetic, no gameplay difference, and the 84px-wide screen at
// font3x5 has no room for upstream's own full real line length anyway
// (each real track row/header line was similarly trimmed to fit the same
// real 4px-per-glyph budget). Everything else in `selectTrack()`
// (per-track best time/position readout, UP/DOWN cursor, held-B clears,
// A loads, C returns to the race) is a direct, unmodified port.
//
// `LCDWIDTH`/`LCDHEIGHT` here are the exact same values (84/48) real
// upstream's own `Display.h` defines them as, so every real pixel-space
// formula in `render.ino`/`running.ino` (perspective projection, road
// width, HUD/menu layout) ports with zero coordinate rescaling.

typedef struct
{
    int x;
    int y;
    int height;
    int lanes;
    int signs;
    int opp;
} BRallySegment;

typedef struct
{
    int* sprite;
    int x;
    int z;
    int y;
    bool renderWhite;
} BRallyObst;

typedef struct
{
    int* sprite;
    int d;
    int x;
    float s;
    int r;
} BRallyOpp;

#define BRALLY_DISTANCE 70
#define BRALLY_MAXSPEED 150
#define BRALLY_ROADWIDTH 30
#define BRALLY_MAXOBST 20
#define BRALLY_HALFH (LCDHEIGHT/2)
#define BRALLY_HALFW (LCDWIDTH/2)

#define BRALLY_STATE_TITLE 0
#define BRALLY_STATE_COUNTDOWN 1
#define BRALLY_STATE_RUNNING 2
#define BRALLY_STATE_PAUSE 3
#define BRALLY_STATE_GAMEOVER 4
#define BRALLY_STATE_SELECT 5

#define BRALLY_EETOKEN1 178 // real EEPROMTOKEN1 = 0xb2
#define BRALLY_EETOKEN2 172 // real EEPROMTOKEN2 = 0xac
#define BRALLY_EETOKEN3 226 // real EEPROMTOKEN3 = 0xe2

BRallySegment brallySegments[70];
BRallyObst brallyObst[20];
BRallyOpp brallyOpponents[10];

int brallyMinY;
int brallyMinHeight;
int brallyNumObst;
int brallyDx, brallyDy;
int brallyPlayerX;
int brallyDist;
int brallyGenPos;
int brallyS = 2;
int brallySp = 2;
int brallyLastL, brallyLastR, brallyL, brallyR;
int brallyDir = 3;
int brallyHillX;
int brallyOffX;
int brallyPos;
int brallyFc;
int brallyGameState;
int brallyCountdown;
int brallyCurTrack = 0;
int brallyMaxTrack = 0;
int brallyTotalTrack = 10;
int brallyNextTrack;
int brallyTotalLength = 10000;
int brallyLapLength = 10000;
int brallySelectedTrack;
int brallyPauseCursor = 0;

// per-track procedural curve tuning, computed once in brallyLevelStart()
// (see this file's own header comment on the real, dead upstream
// "Mockup-Track" fallback this is adapted from)
float brallyPeriodA, brallyPeriodB, brallyPeriodC;
float brallyAmpX1, brallyAmpX2, brallyAmpY;

// -----------------------------------------------------------------------
//   Real bitmap art (Bitmaps.h) - verbatim byte tables, PROGMEM dropped
// -----------------------------------------------------------------------

int brallyScenicL0[48]= { 0, 0, 0, 96, 1, 248, 1, 252, 3, 94, 6, 190, 13, 126, 126, 188, 249, 93, 224, 45, 193, 227, 193, 241, 193, 249, 193, 121, 65, 124, 100, 120, 104, 124, 116, 126, 60, 126, 52, 60, 58, 56, 24, 176, 4, 0, 0, 0 };
int brallyScenicL1[48]= { 0, 0, 0, 96, 1, 232, 1, 220, 3, 190, 5, 126, 10, 190, 125, 124, 248, 189, 194, 93, 193, 225, 193, 241, 193, 249, 193, 121, 65, 120, 96, 124, 108, 124, 124, 126, 54, 126, 56, 120, 58, 56, 24, 176, 4, 0, 0, 0 };
int brallyScenicLM[48]= { 255, 255, 255, 159, 254, 7, 254, 3, 252, 1, 248, 1, 240, 1, 128, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 128, 1, 128, 1, 128, 1, 192, 1, 192, 3, 192, 3, 224, 15, 251, 255, 255, 255 };

int brallyScenicL20[48]= { 0, 0, 0, 0, 3, 80, 4, 188, 125, 94, 240, 190, 224, 70, 192, 249, 192, 253, 224, 189, 232, 189, 224, 62, 100, 63, 104, 62, 117, 62, 106, 60, 116, 62, 122, 60, 48, 60, 58, 184, 49, 240, 8, 0, 0, 0, 0, 0 };
int brallyScenicL21[48]= { 0, 0, 0, 0, 2, 176, 5, 92, 126, 190, 241, 30, 192, 230, 192, 249, 224, 253, 192, 189, 224, 190, 228, 61, 96, 62, 116, 62, 105, 62, 116, 62, 122, 60, 116, 62, 58, 60, 48, 188, 50, 240, 4, 0, 0, 0, 0, 0 };
int brallyScenicL2M[48]= { 255, 255, 255, 255, 252, 15, 248, 3, 128, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 128, 1, 128, 1, 128, 1, 128, 1, 128, 1, 192, 1, 192, 3, 192, 15, 243, 255, 255, 255, 255, 255 };

int brallyScenicS0[48]= { 0, 0, 0, 0, 28, 0, 235, 192, 209, 240, 195, 124, 213, 124, 232, 125, 220, 124, 216, 125, 216, 124, 202, 253, 192, 124, 220, 253, 200, 124, 208, 253, 193, 124, 201, 124, 193, 240, 225, 192, 28, 0, 0, 0, 0, 0, 0, 0 };
int brallyScenicS1[48]= { 0, 0, 0, 0, 28, 0, 245, 192, 225, 176, 213, 248, 226, 126, 220, 124, 216, 125, 204, 124, 216, 125, 218, 124, 200, 253, 216, 124, 216, 253, 200, 124, 192, 126, 193, 248, 193, 176, 225, 192, 28, 0, 0, 0, 0, 0, 0, 0 };
int brallyScenicSM[48]= { 255, 255, 255, 255, 227, 255, 0, 63, 0, 15, 0, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 3, 0, 15, 0, 63, 227, 255, 255, 255, 255, 255, 255, 255 };

int brallyScenicR0[48]= { 0, 0, 4, 0, 24, 176, 58, 56, 52, 60, 60, 126, 116, 126, 104, 124, 100, 120, 65, 124, 193, 121, 193, 249, 193, 241, 193, 227, 224, 45, 249, 93, 126, 188, 13, 126, 6, 190, 3, 94, 1, 252, 1, 248, 0, 96, 0, 0 };
int brallyScenicR1[48]= { 0, 0, 4, 0, 24, 176, 58, 56, 56, 120, 54, 126, 124, 126, 108, 124, 96, 124, 65, 120, 193, 121, 193, 249, 193, 241, 193, 225, 194, 93, 248, 189, 125, 124, 10, 190, 5, 126, 3, 190, 1, 220, 1, 232, 0, 96, 0, 0 };
int brallyScenicRM[48]= { 255, 255, 251, 255, 224, 15, 192, 3, 192, 3, 192, 1, 128, 1, 128, 1, 128, 1, 128, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 240, 1, 248, 1, 252, 1, 254, 3, 254, 7, 255, 159, 255, 255 };

int brallyScenicR20[48]= { 0, 0, 0, 0, 8, 0, 49, 240, 58, 184, 48, 60, 122, 60, 116, 62, 106, 60, 117, 62, 104, 62, 100, 63, 224, 62, 232, 189, 224, 189, 192, 253, 192, 249, 224, 70, 240, 190, 125, 94, 4, 188, 3, 80, 0, 0, 0, 0 };
int brallyScenicR21[48]= { 0, 0, 0, 0, 4, 0, 50, 240, 48, 188, 58, 60, 116, 62, 122, 60, 116, 62, 105, 62, 116, 62, 96, 62, 228, 61, 224, 190, 192, 189, 224, 253, 192, 249, 192, 230, 241, 30, 126, 190, 5, 92, 2, 176, 0, 0, 0, 0 };
int brallyScenicR2M[48]= { 255, 255, 255, 255, 243, 255, 192, 15, 192, 3, 192, 1, 128, 1, 128, 1, 128, 1, 128, 1, 128, 1, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 128, 1, 248, 3, 252, 15, 255, 255, 255, 255 };

int* brallyCars0[6]= { brallyScenicS0, brallyScenicL0, brallyScenicL20, brallyScenicS0, brallyScenicR20, brallyScenicR0 };
int* brallyCars1[6]= { brallyScenicS1, brallyScenicL1, brallyScenicL21, brallyScenicS1, brallyScenicR21, brallyScenicR1 };
int* brallyCarsM[6]= { brallyScenicSM, brallyScenicLM, brallyScenicL2M, brallyScenicSM, brallyScenicR2M, brallyScenicRM };

int brallyCar1[34]= { 16, 16, 0, 128, 1, 128, 2, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar2[34]= { 16, 16, 1, 128, 2, 64, 0, 64, 0, 128, 1, 0, 2, 0, 3, 192, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar3[34]= { 16, 16, 1, 128, 2, 64, 0, 64, 0, 128, 0, 64, 2, 64, 1, 128, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar4[34]= { 16, 16, 0, 128, 1, 0, 1, 0, 2, 0, 2, 128, 3, 192, 0, 128, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar5[34]= { 16, 16, 3, 192, 2, 0, 3, 128, 0, 64, 0, 64, 2, 64, 1, 128, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar6[34]= { 16, 16, 1, 128, 2, 64, 2, 0, 3, 128, 2, 64, 2, 64, 1, 128, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar7[34]= { 16, 16, 3, 192, 0, 64, 0, 64, 0, 128, 1, 0, 1, 0, 1, 0, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar8[34]= { 16, 16, 3, 192, 4, 32, 4, 32, 3, 192, 4, 32, 4, 32, 3, 192, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar9[34]= { 16, 16, 1, 192, 2, 32, 2, 32, 1, 224, 0, 32, 2, 32, 1, 192, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };
int brallyCar10[34]= { 16, 16, 2, 96, 6, 144, 10, 144, 2, 144, 2, 144, 2, 144, 2, 96, 0, 0, 15, 240, 184, 29, 112, 14, 255, 255, 255, 255, 255, 255, 120, 30, 112, 14 };

int* brallyOppSprite[10]= { brallyCar1, brallyCar2, brallyCar3, brallyCar4, brallyCar5, brallyCar6, brallyCar7, brallyCar8, brallyCar9, brallyCar10 };

// Real background: 128 columns x 3 pages (rows 0-23), column-major, one
// byte per page - the same "column, page" addressing this shim's own
// gbFrameBuffer[] uses. Real upstream reads this 3-byte group in REVERSE
// order per column (page0 <- byte+2, page1 <- byte+1, page2 <- byte+0,
// see render.ino's own `renderHills()`) - preserved exactly below.
int brallyHillsBitmap[384]= { 255, 192, 0, 255, 224, 0, 255, 224, 0, 255, 240, 0, 255, 240, 0, 255, 248, 0, 255, 248, 0, 255, 252, 0, 255, 252, 0, 255, 254, 0, 255, 254, 0, 255, 255, 0, 255, 255, 192, 255, 254, 112, 255, 254, 24, 255, 254, 24, 255, 254, 12, 255, 254, 48, 255, 252, 192, 255, 252, 192, 255, 253, 128, 255, 243, 0, 255, 236, 0, 255, 236, 0, 255, 184, 0, 254, 96, 0, 253, 192, 0, 253, 192, 0, 251, 0, 0, 247, 128, 0, 239, 128, 0, 239, 128, 0, 255, 192, 0, 255, 192, 0, 255, 224, 0, 255, 224, 0, 255, 224, 0, 255, 240, 0, 255, 232, 0, 255, 228, 0, 255, 228, 0, 255, 196, 0, 255, 216, 0, 255, 176, 0, 255, 176, 0, 255, 96, 0, 254, 192, 0, 253, 128, 0, 253, 128, 0, 243, 0, 0, 230, 0, 0, 239, 0, 0, 239, 0, 0, 255, 128, 0, 255, 128, 0, 255, 192, 0, 255, 192, 0, 255, 192, 0, 255, 224, 0, 255, 240, 0, 255, 240, 0, 255, 240, 0, 255, 248, 0, 255, 252, 0, 255, 254, 0, 255, 254, 0, 255, 253, 128, 255, 252, 64, 255, 248, 96, 255, 248, 96, 255, 193, 128, 254, 62, 0, 255, 224, 0, 255, 224, 0, 255, 192, 0, 255, 224, 0, 255, 240, 0, 255, 216, 0, 255, 216, 0, 255, 176, 0, 255, 96, 0, 254, 192, 0, 255, 128, 0, 255, 0, 0, 254, 0, 0, 252, 0, 0, 248, 0, 0, 248, 0, 0, 248, 0, 0, 248, 0, 0, 255, 0, 0, 253, 0, 0, 255, 128, 0, 255, 128, 32, 248, 0, 160, 254, 0, 32, 254, 0, 64, 248, 0, 96, 252, 0, 16, 252, 0, 160, 255, 224, 64, 254, 160, 32, 255, 192, 64, 252, 0, 168, 255, 160, 0, 252, 16, 16, 255, 192, 0, 255, 196, 0, 248, 0, 0, 255, 242, 128, 250, 176, 64, 255, 192, 128, 255, 192, 64, 248, 0, 192, 255, 0, 32, 253, 0, 128, 255, 0, 64, 248, 0, 128, 252, 0, 32, 252, 0, 128, 254, 1, 96, 254, 0, 0, 254, 0, 0, 254, 0, 0, 255, 0, 0, 255, 128, 0, 255, 192, 0, 255, 192, 0 };

int brallySignLeft[34]= { 16, 16, 127, 254, 255, 255, 224, 7, 192, 51, 192, 243, 195, 195, 207, 3, 220, 3, 220, 3, 207, 3, 195, 195, 192, 243, 192, 51, 224, 3, 255, 255, 127, 254 };
int brallySignRight[34]= { 16, 16, 127, 254, 255, 255, 192, 7, 204, 3, 207, 3, 195, 195, 192, 243, 192, 59, 192, 59, 192, 243, 195, 195, 207, 3, 204, 3, 224, 7, 255, 255, 127, 254 };
int brallySignLanes[34]= { 16, 16, 127, 254, 255, 255, 224, 7, 198, 99, 198, 99, 198, 99, 198, 99, 206, 115, 204, 51, 216, 27, 216, 27, 216, 27, 192, 3, 224, 7, 255, 255, 127, 254 };
int brallySignFinish[34]= { 16, 16, 240, 240, 240, 240, 240, 240, 240, 240, 15, 15, 15, 15, 15, 15, 15, 15, 240, 240, 240, 240, 240, 240, 240, 240, 15, 15, 15, 15, 15, 15, 15, 15 };

int brallyLetterA[34]= { 16, 16, 0, 0, 3, 224, 7, 224, 7, 224, 7, 240, 15, 240, 15, 112, 14, 120, 30, 120, 31, 248, 31, 252, 63, 252, 60, 60, 60, 62, 124, 30, 0, 0 };
int brallyLetterE[34]= { 16, 16, 0, 0, 15, 240, 15, 240, 15, 240, 15, 240, 15, 0, 15, 0, 15, 240, 15, 240, 15, 240, 15, 0, 15, 0, 15, 248, 15, 248, 15, 248, 0, 0 };
int brallyLetterG[34]= { 16, 16, 0, 0, 3, 248, 15, 248, 31, 248, 63, 8, 62, 0, 60, 0, 60, 252, 60, 252, 60, 252, 62, 60, 63, 60, 31, 252, 15, 252, 3, 240, 0, 0 };
int brallyLetterM[34]= { 16, 16, 0, 0, 252, 63, 252, 63, 254, 63, 238, 63, 238, 127, 238, 127, 239, 127, 239, 111, 231, 239, 231, 239, 231, 239, 231, 207, 227, 207, 227, 207, 0, 0 };
int brallyLetterO[34]= { 16, 16, 0, 0, 7, 224, 15, 248, 31, 252, 62, 124, 60, 60, 60, 62, 124, 30, 124, 30, 60, 62, 60, 60, 63, 252, 31, 248, 15, 240, 3, 192, 0, 0 };
int brallyLetterR[34]= { 16, 16, 0, 0, 31, 224, 31, 248, 31, 248, 30, 124, 30, 120, 30, 120, 31, 240, 31, 224, 31, 240, 30, 248, 30, 120, 30, 120, 30, 124, 30, 60, 0, 0 };
int brallyLetterV[34]= { 16, 16, 0, 0, 60, 30, 60, 60, 60, 60, 30, 60, 30, 56, 30, 120, 14, 120, 15, 112, 15, 112, 7, 240, 7, 224, 7, 224, 7, 224, 3, 192, 0, 0 };

int* brallyGameOverLetters[8]= { brallyLetterG, brallyLetterA, brallyLetterM, brallyLetterE, brallyLetterO, brallyLetterV, brallyLetterE, brallyLetterR };

int brallySprite1[34]= { 16, 16, 255, 255, 128, 1, 128, 193, 135, 193, 135, 193, 133, 193, 129, 193, 129, 193, 129, 193, 129, 193, 129, 193, 129, 193, 129, 193, 128, 1, 128, 1, 255, 255 };
int brallySprite2[34]= { 16, 16, 255, 255, 128, 1, 135, 193, 135, 225, 132, 225, 128, 225, 129, 225, 131, 225, 135, 193, 135, 129, 142, 1, 143, 225, 143, 225, 128, 1, 128, 1, 255, 255 };
int brallySprite3[34]= { 16, 16, 255, 255, 128, 1, 135, 225, 135, 241, 128, 113, 128, 113, 131, 225, 131, 225, 128, 113, 128, 113, 128, 113, 135, 241, 135, 193, 128, 1, 128, 1, 255, 255 };

int* brallyCountdownSprites[3]= { brallySprite1, brallySprite2, brallySprite3 };

// Real per-scale-level nearest-neighbor sampling tables (Bitmaps.h
// `factors[]`) - `brallyFactors[f-1][i]` maps the i-th row/column of a
// scaled (16-f)x(16-f) sprite to its real source row/column (0-15).
int brallyF1[16]= { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
int brallyF2[15]= { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15 };
int brallyF3[14]= { 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13, 14, 15 };
int brallyF4[13]= { 0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 13, 14, 15 };
int brallyF5[12]= { 0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 14, 15 };
int brallyF6[11]= { 0, 1, 3, 4, 6, 7, 9, 11, 12, 14, 15 };
int brallyF7[10]= { 0, 2, 3, 5, 6, 8, 10, 12, 13, 15 };
int brallyF8[9]= { 0, 1, 3, 5, 7, 9, 11, 13, 15 };
int brallyF9[8]= { 1, 3, 5, 7, 9, 11, 13, 15 };
int brallyF10[7]= { 0, 3, 5, 7, 11, 14, 15 };
int brallyF11[5]= { 1, 4, 9, 12, 15 };
int brallyF12[4]= { 1, 5, 11, 15 };
int brallyF13[3]= { 1, 8, 15 };
int brallyF14[2]= { 6, 13 };
int brallyF15[1]= { 9 };

int* brallyFactors[15]= { brallyF1, brallyF2, brallyF3, brallyF4, brallyF5, brallyF6, brallyF7, brallyF8, brallyF9, brallyF10, brallyF11, brallyF12, brallyF13, brallyF14, brallyF15 };

// Real per-distance sprite-scale lookup (Bitmaps.h `zLut[65]`) - maps a
// road segment index (0-64) to its real on-screen sprite scale factor.
int brallyZLut[65]= { 0, 1, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15 };

// -----------------------------------------------------------------------
//   Track names (menu.h) - a function returning a string-literal pointer,
//   the same real, already-proven pattern gameShipwrek.c's own
//   shipPName()/shipBoatNameFor() use (no precedent anywhere in this
//   project for a global int*[N] array of string-literal pointers).
// -----------------------------------------------------------------------

char* brallyTrackName( int idx )
{
    if( idx == 0 ) return "Easy1";
    if( idx == 1 ) return "Easy2";
    if( idx == 2 ) return "Medium1";
    if( idx == 3 ) return "Medium2";
    if( idx == 4 ) return "Hard1";
    if( idx == 5 ) return "Hard2";
    if( idx == 6 ) return "Harder1";
    if( idx == 7 ) return "Harder2";
    if( idx == 8 ) return "Hardest1";
    return "Hardest2";
}

void brallyNewline()
{
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

// -----------------------------------------------------------------------
//   EEPROM - direct port of eeprom.ino
// -----------------------------------------------------------------------

void brallyClearEeprom()
{
    eeprom_write_byte( 0, BRALLY_EETOKEN1 );
    eeprom_write_byte( 1, BRALLY_EETOKEN2 );
    eeprom_write_byte( 2, BRALLY_EETOKEN3 );
    int i;
    for( i = 3; i < 1024; i = i + 1 )
    {
        if( eeprom_read_byte( i ) != 0 )
          eeprom_write_byte( i, 0 );
    }
}

void brallyLoadEeprom()
{
    if( eeprom_read_byte( 0 ) != BRALLY_EETOKEN1 || eeprom_read_byte( 1 ) != BRALLY_EETOKEN2 || eeprom_read_byte( 2 ) != BRALLY_EETOKEN3 )
    {
        brallyClearEeprom();
        brallyCurTrack = 0;
        brallyMaxTrack = 0;
        return;
    }
    brallyCurTrack = eeprom_read_byte( 4 );
    brallyMaxTrack = eeprom_read_byte( 5 );
}

void brallyTrySaveEeprom( int curTrack, int pos, int fc )
{
    if( eeprom_read_byte( 0 ) != BRALLY_EETOKEN1 || eeprom_read_byte( 1 ) != BRALLY_EETOKEN2 || eeprom_read_byte( 2 ) != BRALLY_EETOKEN3 )
    {
        brallyClearEeprom();
        return;
    }
    int sec = fc / 20;
    if( eeprom_read_byte( curTrack * 4 + 6 ) > 0 )
    {
        if( eeprom_read_byte( curTrack * 4 + 6 ) > pos )
          eeprom_write_byte( curTrack * 4 + 6, pos );
        int oldSec = eeprom_read_byte( curTrack * 4 + 7 ) * 256 + eeprom_read_byte( curTrack * 4 + 8 );
        if( oldSec > sec )
        {
            eeprom_write_byte( curTrack * 4 + 7, sec / 256 );
            eeprom_write_byte( curTrack * 4 + 8, sec % 256 );
        }
    }
    else
    {
        eeprom_write_byte( curTrack * 4 + 6, pos );
        eeprom_write_byte( curTrack * 4 + 7, sec / 256 );
        eeprom_write_byte( curTrack * 4 + 8, sec % 256 );
    }
    brallyNextTrack = curTrack;
    if( pos < 4 )
    {
        if( curTrack < brallyTotalTrack - 1 ) brallyNextTrack = brallyNextTrack + 1;
        if( brallyNextTrack > brallyMaxTrack ) brallyMaxTrack = brallyNextTrack;
    }
    if( eeprom_read_byte( 4 ) != brallyNextTrack ) eeprom_write_byte( 4, brallyNextTrack );
    if( eeprom_read_byte( 5 ) != brallyMaxTrack ) eeprom_write_byte( 5, brallyMaxTrack );
}

// -----------------------------------------------------------------------
//   Procedural track generation (replaces real petit_fatfs file reads -
//   see this file's own header comment)
// -----------------------------------------------------------------------

void brallyGetNextSegment( BRallySegment* out )
{
    float d = (float)brallyGenPos;
    float curve = brallyAmpX1 * sin( d / brallyPeriodA ) + brallyAmpX2 * sin( d / brallyPeriodB );
    float curveAhead = brallyAmpX1 * sin( ( d + 20.0 ) / brallyPeriodA ) + brallyAmpX2 * sin( ( d + 20.0 ) / brallyPeriodB );

    out->x = (int)curve;
    out->y = (int)( brallyAmpY * sin( d / brallyPeriodC ) );
    out->lanes = 1;
    out->height = 0;
    out->signs = 0;
    out->opp = 0;

    if( ( brallyGenPos % 5 ) == 0 )
    {
        if( curveAhead < -4.0 ) out->signs = 1;
        else if( curveAhead > 4.0 ) out->signs = 2;
    }
    // real finish-line marker (signs==5, a real live upstream case in
    // running.ino) wired to this port's own known total track length,
    // since there is no file-driven finish flag to read here
    if( brallyGenPos >= brallyTotalLength - 2 && brallyGenPos <= brallyTotalLength + 2 )
      out->signs = 5;

    brallyGenPos = brallyGenPos + 1;
}

void brallyLevelStart()
{
    brallyPeriodA = 150.0 - (float)brallyCurTrack * 8.0;
    if( brallyPeriodA < 60.0 ) brallyPeriodA = 60.0;
    brallyPeriodB = 70.0 - (float)brallyCurTrack * 3.0;
    if( brallyPeriodB < 25.0 ) brallyPeriodB = 25.0;
    brallyPeriodC = 130.0 - (float)brallyCurTrack * 6.0;
    if( brallyPeriodC < 50.0 ) brallyPeriodC = 50.0;
    brallyAmpX1 = 5.5 + (float)brallyCurTrack * 0.4;
    brallyAmpX2 = 1.0 + (float)brallyCurTrack * 0.2;
    brallyAmpY = 3.5 + (float)brallyCurTrack * 0.3;

    brallyTotalLength = 4000 + brallyCurTrack * 500;
    brallyLapLength = brallyTotalLength;

    brallyDist = 0;
    brallyGenPos = 0;
    brallyHillX = 0;
    brallyPlayerX = 0;
    brallySp = 0;

    int i;
    for( i = 0; i < BRALLY_DISTANCE; i = i + 1 )
    {
        brallyGetNextSegment( &brallySegments[ i ] );
        brallyDist = brallyDist + 1;
    }

    for( i = 0; i < 10; i = i + 1 )
    {
        brallyOpponents[ i ].d = 11 - i;
        brallyOpponents[ i ].sprite = brallyOppSprite[ i ];
        brallyOpponents[ i ].x = ( i % 2 ) * BRALLY_ROADWIDTH - BRALLY_ROADWIDTH / 2;
        brallyOpponents[ i ].s = 2.0 - ( (float)i ) / 10.0;
        brallyOpponents[ i ].r = 0;
        brallySegments[ 20 - i ].opp = i + 1;
    }
    brallyPos = 11;
    brallyFc = 0;
    brallyCountdown = 60;
    brallyGameState = BRALLY_STATE_COUNTDOWN;
    gbPopup( brallyTrackName( brallyCurTrack ), 60 );
}

void brallyRestart()
{
    brallySp = 0;
    brallyDist = 0;
    brallyGenPos = 0;
    brallyPlayerX = 0;
    int i;
    for( i = 0; i < BRALLY_DISTANCE; i = i + 1 )
    {
        brallyGetNextSegment( &brallySegments[ i ] );
        brallyDist = brallyDist + 1;
    }
    for( i = 0; i < 10; i = i + 1 )
    {
        brallyOpponents[ i ].d = i;
        brallyOpponents[ i ].sprite = brallyOppSprite[ i ];
    }
    brallyPos = 11;
    brallyFc = 0;
    brallyGameState = BRALLY_STATE_COUNTDOWN;
}

void brallyLoadLevel( int lvl )
{
    if( lvl > brallyMaxTrack )
    {
        gbPopup( "Track locked", 60 );
        return;
    }
    brallyCurTrack = lvl;
    brallyLevelStart();
}

// -----------------------------------------------------------------------
//   Rendering (render.ino) - see this file's own header comment for how
//   each real raw-framebuffer technique maps onto this shim's own
//   drawing primitives.
// -----------------------------------------------------------------------

void brallyRenderHills( int xOff )
{
    // real upstream also force-fills the not-yet-drawn road area below
    // the hills silhouette solid black (two explicit 0xFF page writes)
    gbSetColor( GB_BLACK );
    gbFillRect( 0, 24, LCDWIDTH, 16 );

    int i;
    for( i = 0; i < LCDWIDTH; i = i + 1 )
    {
        int xo = ( xOff + i ) % 128;
        if( xo < 0 ) xo = xo + 128;
        int base = xo * 3;
        int b0 = brallyHillsBitmap[ base + 2 ];
        int b1 = brallyHillsBitmap[ base + 1 ];
        int b2 = brallyHillsBitmap[ base ];
        int bit;
        for( bit = 0; bit < 8; bit = bit + 1 )
        {
            if( ( ( b0 >> bit ) & 1 ) != 0 ) gbDrawPixel( i, bit );
            if( ( ( b1 >> bit ) & 1 ) != 0 ) gbDrawPixel( i, 8 + bit );
            if( ( ( b2 >> bit ) & 1 ) != 0 ) gbDrawPixel( i, 16 + bit );
        }
    }
}

void brallyRenderFloor( int minheight )
{
    int mh = minheight;
    if( mh < 0 ) mh = 0;
    if( mh > LCDHEIGHT ) mh = LCDHEIGHT;
    gbSetColor( GB_GRAY );
    gbFillRect( 0, mh, LCDWIDTH, LCDHEIGHT - mh );
}

void brallyRenderCarFrame( int* mask, int* frame, int baseX, int baseY )
{
    int col, bit;
    for( col = 0; col < 24; col = col + 1 )
    {
        int px = baseX + col;
        if( px < 0 || px >= LCDWIDTH ) continue;

        int maskBottom = mask[ 2 * col ];
        int maskTop = mask[ 2 * col + 1 ];
        int frameBottom = frame[ 2 * col ];
        int frameTop = frame[ 2 * col + 1 ];

        for( bit = 0; bit < 8; bit = bit + 1 )
        {
            if( ( ( maskTop >> bit ) & 1 ) == 0 )
            {
                if( ( ( frameTop >> bit ) & 1 ) != 0 ) gbSetColor( GB_BLACK ); else gbSetColor( GB_WHITE );
                gbDrawPixel( px, baseY + bit );
            }
            if( ( ( maskBottom >> bit ) & 1 ) == 0 )
            {
                if( ( ( frameBottom >> bit ) & 1 ) != 0 ) gbSetColor( GB_BLACK ); else gbSetColor( GB_WHITE );
                gbDrawPixel( px, baseY + 8 + bit );
            }
        }
    }
}

void brallyRenderCar( int off, int dir )
{
    int* activeFrame;
    if( ( gbFrameCount % 2 ) != 0 ) activeFrame = brallyCars1[ dir ]; else activeFrame = brallyCars0[ dir ];
    int baseX = ( LCDWIDTH / 2 ) - 12 + off;
    int baseY = 32; // real upstream anchor: page 4 (row 32), see header comment
    brallyRenderCarFrame( brallyCarsM[ dir ], activeFrame, baseX, baseY );
}

void brallyRenderScaledSprite16( int xPos, int yPos, int f, bool renderWhite, int* sprite )
{
    if( f < 1 || f > 15 ) return;
    if( xPos > LCDWIDTH || yPos > LCDHEIGHT ) return;

    int size = 16 - f;
    int* factorRow = brallyFactors[ f - 1 ];

    int row, col;
    for( row = 0; row < size; row = row + 1 )
    {
        int srcY = factorRow[ row ];
        int py = yPos + row;
        if( py < 0 || py >= LCDHEIGHT ) continue;

        for( col = 0; col < size; col = col + 1 )
        {
            int px = xPos + col;
            if( px < 0 || px >= LCDWIDTH ) continue;
            int srcX = factorRow[ col ];

            if( gbGetBitmapPixel( sprite, srcX, srcY ) )
            {
                gbSetColor( GB_BLACK );
                gbDrawPixel( px, py );
            }
            else if( renderWhite )
            {
                gbSetColor( GB_WHITE );
                gbDrawPixel( px, py );
            }
        }
    }
}

void brallyDrawSingleLine( int left, int right, int y, int color )
{
    if( left > 0 && left < 127 )
    {
        if( color == 1 ) gbSetColor( GB_BLACK ); else gbSetColor( GB_WHITE );
        gbDrawPixel( left - 1, y );
        gbDrawPixel( left, y );
    }
    if( left < 127 && right > -127 )
    {
        gbSetColor( GB_WHITE );
        gbDrawFastHLine( left + 1, y, right - left - 1 );
    }
    if( right < LCDWIDTH )
    {
        if( color == 1 ) gbSetColor( GB_BLACK ); else gbSetColor( GB_WHITE );
        gbDrawPixel( right, y );
        gbDrawPixel( right + 1, y );
    }
}

void brallyAddObst( int z, int x, int y, int* sprite, bool renderWhite )
{
    if( brallyNumObst > BRALLY_MAXOBST ) return;
    brallyObst[ brallyNumObst ].z = z;
    brallyObst[ brallyNumObst ].x = x;
    brallyObst[ brallyNumObst ].y = y;
    brallyObst[ brallyNumObst ].sprite = sprite;
    brallyObst[ brallyNumObst ].renderWhite = renderWhite;
    brallyNumObst = brallyNumObst + 1;
}

void brallyRenderObst()
{
    int i;
    for( i = brallyNumObst; i > 0; i = i - 1 )
      brallyRenderScaledSprite16( brallyObst[ i - 1 ].x, brallyObst[ i - 1 ].y, brallyObst[ i - 1 ].z, brallyObst[ i - 1 ].renderWhite, brallyObst[ i - 1 ].sprite );
}

void brallyRenderHud()
{
    gbCursorX = 0;
    gbCursorY = 0;
    gbSetColor( GB_BLACK );
    gbPrintString( "Spd:" );
    gbPrintNumber( brallySp );
    gbPrintString( " Pos:" );
    gbPrintNumber( brallyPos );
    gbPrintString( " L:" );
    gbPrintNumber( ( brallyDist - BRALLY_DISTANCE ) / brallyLapLength + 1 );
    gbPrintString( "/" );
    gbPrintNumber( brallyTotalLength / brallyLapLength );
}

// -----------------------------------------------------------------------
//   Opponent AI / collision (running.ino)
// -----------------------------------------------------------------------

void brallyUpdateOpps()
{
    brallyPos = 11;
    int posOnLap = ( brallyDist - BRALLY_DISTANCE ) % brallyLapLength;
    int i;
    for( i = 0; i < 10; i = i + 1 )
    {
        if( brallyGameState == BRALLY_STATE_RUNNING && brallyOpponents[ i ].d < brallyTotalLength + i + 20 )
          brallyOpponents[ i ].r = brallyOpponents[ i ].r + (int)( brallyOpponents[ i ].s * 24.0 );
        if( brallyOpponents[ i ].r > 24 )
        {
            brallyOpponents[ i ].d = brallyOpponents[ i ].d + brallyOpponents[ i ].r / 24;
            brallyOpponents[ i ].r = brallyOpponents[ i ].r % 24;
        }
        if( brallyOpponents[ i ].d < ( brallyDist - BRALLY_DISTANCE ) ) brallyPos = brallyPos - 1;

        int dmod = brallyOpponents[ i ].d % brallyLapLength;
        if( dmod < posOnLap + BRALLY_DISTANCE && dmod > posOnLap )
        {
            int segIdx = dmod - posOnLap;
            brallyOpponents[ i ].s = 2.0 - ( (float)gbAbsInt( brallySegments[ segIdx ].x ) ) / 7.0;
            brallySegments[ segIdx ].opp = i + 1;
        }
        else
        {
            brallyOpponents[ i ].s = 2.0 - ( (float)i ) / 10.0;
            if( brallyOpponents[ i ].d + 200 < brallyDist ) brallyOpponents[ i ].s = brallyOpponents[ i ].s + 1.0;
        }
    }
}

void brallyCheckCollision()
{
    int posOnLap = ( brallyDist - BRALLY_DISTANCE ) % brallyLapLength;
    int i;
    for( i = 0; i < 10; i = i + 1 )
    {
        int dmod = brallyOpponents[ i ].d % brallyLapLength;
        if( dmod > posOnLap && dmod < ( posOnLap + brallyS ) )
        {
            if( gbAbsInt( brallyPlayerX + brallyOpponents[ i ].x ) < 15 )
            {
                brallySp = (int)( brallyOpponents[ i ].s * 20.0 );
                brallyS = (int)( brallyOpponents[ i ].s * 5.0 / 6.0 );
            }
        }
    }
}

// -----------------------------------------------------------------------
//   Main states (B-Rally.ino / running.ino / pause.ino)
// -----------------------------------------------------------------------

void brallyRunningTick()
{
    brallyFc = brallyFc + 1;

    if( brallyDist > brallyTotalLength + BRALLY_DISTANCE && brallyGameState == BRALLY_STATE_RUNNING )
    {
        brallyGameState = BRALLY_STATE_GAMEOVER;
        brallyTrySaveEeprom( brallyCurTrack, brallyPos, brallyFc );
    }

    // player input - real upstream's own digital-button fallback path
    // (see this file's own header comment - no accelerometer here)
    if( gbRepeat( BTN_LEFT, 1 ) && brallyDir != 1 ) brallyDir = 2;
    if( gbRepeat( BTN_RIGHT, 1 ) && brallyDir != 5 ) brallyDir = 4;
    if( gbRepeat( BTN_B, 1 ) )
    {
        if( brallySp > 3 ) brallySp = brallySp - 4; else brallySp = 0;
        if( brallyDir == 2 ) brallyDir = 1;
        if( brallyDir == 4 ) brallyDir = 5;
    }
    if( gbRepeat( BTN_A, 1 ) && brallyGameState != BRALLY_STATE_COUNTDOWN )
    {
        if( brallySp < BRALLY_MAXSPEED ) brallySp = brallySp + ( ( BRALLY_MAXSPEED - brallySp + 128 ) / 128 );
    }
    else
    {
        if( brallySp > 0 ) brallySp = brallySp - 1;
    }
    if( gbReleased( BTN_RIGHT ) || gbReleased( BTN_LEFT ) || gbReleased( BTN_A ) ) brallyDir = 3;

    if( brallyDir == 1 ) { if( brallyPlayerX < 80 ) brallyPlayerX = brallyPlayerX + brallyS + 10; }
    else if( brallyDir == 2 ) { if( brallyPlayerX < 80 ) brallyPlayerX = brallyPlayerX + brallyS + 5; }
    else if( brallyDir == 4 ) { if( brallyPlayerX > -80 ) brallyPlayerX = brallyPlayerX - brallyS - 5; }
    else if( brallyDir == 5 ) { if( brallyPlayerX > -80 ) brallyPlayerX = brallyPlayerX - brallyS - 10; }

    if( gbPressed( BTN_C ) )
    {
        brallyGameState = BRALLY_STATE_PAUSE;
        return;
    }

    if( brallyPlayerX < -( BRALLY_ROADWIDTH * brallySegments[ 0 ].lanes ) || brallyPlayerX > ( BRALLY_ROADWIDTH * brallySegments[ 0 ].lanes ) )
    {
        if( brallySp > 20 ) brallySp = brallySp / 2;
        if( brallySp > 0 )
        {
            if( brallyOffX == 1 ) brallyOffX = -1; else brallyOffX = 1;
        }
    }

    brallyMinY = 99;
    brallyMinHeight = 99;
    brallyDx = 0;
    brallyDy = 0;
    brallyS = brallySp / 24;
    brallyCheckCollision();
    brallyDist = brallyDist + brallyS;
    brallyNumObst = 0;

    int i;
    for( i = 0; i < BRALLY_DISTANCE - brallyS; i = i + 1 )
      brallySegments[ i ] = brallySegments[ i + brallyS ];
    for( i = brallyS; i > 0; i = i - 1 )
      brallyGetNextSegment( &brallySegments[ BRALLY_DISTANCE - i ] );

    brallyPlayerX = brallyPlayerX + ( brallySegments[ 0 ].x * brallyS );

    int hillDelta = 0;
    if( brallyS > 1 ) hillDelta = brallySegments[ 0 ].x / 2;
    brallyHillX = brallyHillX + hillDelta;

    brallyRenderHills( brallyHillX + brallyOffX );
    brallyRenderHud();

    int z;
    for( z = 1; z < BRALLY_DISTANCE; z = z + 1 )
    {
        brallyDy = brallyDy + brallySegments[ z ].y;
        brallySegments[ z ].height = BRALLY_HALFH + ( BRALLY_HALFH / z ) + ( 5 * brallyDy / ( z + 4 ) );
        if( brallySegments[ z ].height < brallyMinHeight ) brallyMinHeight = brallySegments[ z ].height;
    }

    brallyRenderFloor( brallyMinHeight );

    brallyUpdateOpps();
    brallyLastL = 0;
    brallyLastR = LCDWIDTH;

    for( z = 1; z < BRALLY_DISTANCE; z = z + 1 )
    {
        int y = brallySegments[ z ].height;
        int w = ( BRALLY_ROADWIDTH * brallySegments[ z ].lanes ) / z;
        brallyDx = brallyDx + brallySegments[ z ].x;
        int t = BRALLY_HALFW + brallyDx + brallyPlayerX / z;
        brallyL = t - w;
        brallyR = t + w;

        if( y < brallyMinY && y > 5 )
        {
            int diffy = brallyMinY - y;
            int di;
            for( di = 0; di < diffy; di = di + 1 )
            {
                int ll = brallyL - brallyL * di / diffy + brallyLastL * di / diffy;
                int rr = brallyR - brallyR * di / diffy + brallyLastR * di / diffy;
                brallyDrawSingleLine( ll, rr, di + y, ( ( brallyDist + z ) / 3 ) % 3 );
            }
            brallyLastL = brallyL;
            brallyLastR = brallyR;
            brallyMinY = y;
        }

        if( brallyNumObst < BRALLY_MAXOBST && z < 64 )
        {
            int zt = brallyZLut[ z ];
            int zt16 = 16 - zt;
            int b = y - zt16;

            if( brallySegments[ z ].signs == 5 )
            {
                int k;
                for( k = 0; k < 4; k = k + 1 )
                {
                    if( t > 0 && ( t + zt ) < LCDWIDTH )
                      brallyAddObst( zt, t - zt16 / 2 + ( k - 2 ) * zt16, y - zt, brallySignFinish, true );
                }
            }
            else if( brallySegments[ z ].signs == 2 )
            {
                if( ( brallyR + zt ) < LCDWIDTH && brallyR > 0 )
                  brallyAddObst( zt, brallyR, b, brallySignLeft, true );
            }
            else if( brallySegments[ z ].signs == 1 )
            {
                if( brallyL > zt16 && brallyL < LCDWIDTH )
                  brallyAddObst( zt, brallyL - zt16, b, brallySignRight, true );
            }
            else if( brallySegments[ z ].signs == 3 )
            {
                if( brallyL > zt16 && brallyL < LCDWIDTH )
                  brallyAddObst( zt, brallyL - zt16, b, brallySignLanes, true );
                if( ( brallyR + zt ) < LCDWIDTH && brallyR > 0 )
                  brallyAddObst( zt, brallyR, b, brallySignLanes, true );
            }

            if( brallySegments[ z ].opp != 0 && brallyNumObst < BRALLY_MAXOBST )
            {
                if( t > 0 && ( t + zt ) < LCDWIDTH )
                {
                    int oppIdx = brallySegments[ z ].opp - 1;
                    brallyAddObst( zt, t - zt16 / 2 + brallyOpponents[ oppIdx ].x / z, b, brallyOpponents[ oppIdx ].sprite, false );
                }
                brallySegments[ z ].opp = 0;
            }
        }
    }

    brallyRenderObst();
    brallyRenderCar( brallyOffX, brallyDir );
    gbPlayNote( brallySp % 32, 1 );

    if( brallyGameState == BRALLY_STATE_COUNTDOWN )
    {
        brallyCountdown = brallyCountdown - 1;
        if( brallyCountdown == 0 )
        {
            brallyGameState = BRALLY_STATE_RUNNING;
        }
        else
        {
            brallyRenderScaledSprite16( LCDWIDTH / 2 - 8 + ( brallyCountdown % 20 ) / 2, LCDHEIGHT / 4 - 8 + ( brallyCountdown % 20 ) / 2, brallyCountdown % 20, true, brallyCountdownSprites[ brallyCountdown / 20 ] );
            brallyFc = brallyFc - 1;
        }
    }
}

void brallyGameOverTick()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 0;
    gbCursorY = 0;
    if( brallyPos < 4 )
    {
        gbPrintString( "Congrats!" );
        brallyNewline();
    }
    gbPrintString( "You reached " );
    brallyNewline();
    gbPrintNumber( brallyPos );
    gbPrintString( ". Place" );
    brallyNewline();
    gbPrintString( "at track " );
    gbPrintString( brallyTrackName( brallyCurTrack ) );
    brallyNewline();
    gbPrintString( "Press A" );

    int i;
    for( i = 0; i < 8; i = i + 1 )
    {
        float s1 = (float)( gbFrameCount + i * 8 );
        int gx = (int)( sin( s1 / 30.0 ) * 34.0 + 34.0 );
        int gy = (int)( 16.0 - cos( s1 / 20.0 ) * 16.0 );
        int gf = (int)( cos( s1 / 20.0 ) * 7.0 + 8.0 );
        brallyRenderScaledSprite16( gx, gy, gf, false, brallyGameOverLetters[ 7 - i ] );
    }

    if( gbPressed( BTN_A ) )
    {
        brallyCurTrack = brallyNextTrack;
        brallyLoadLevel( brallyCurTrack );
    }
}

void brallyPauseMenuUpdate()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 10;
    gbCursorY = 2;
    gbPrintString( "PAUSED" );

    int i;
    int y = 12;
    for( i = 0; i < 4; i = i + 1 )
    {
        gbCursorX = 6;
        gbCursorY = y;
        if( i == brallyPauseCursor ) gbPrintString( ">" ); else gbPrintString( " " );
        gbCursorX = 12;
        gbCursorY = y;
        if( i == 0 ) gbPrintString( "Resume" );
        else if( i == 1 ) gbPrintString( "Restart" );
        else if( i == 2 ) gbPrintString( "Select track" );
        else gbPrintString( "Exit" );
        y = y + gbFontSize * gbFontHeight;
    }

    if( gbPressed( BTN_DOWN ) ) brallyPauseCursor = ( brallyPauseCursor + 1 ) % 4;
    if( gbPressed( BTN_UP ) )
    {
        brallyPauseCursor = brallyPauseCursor - 1;
        if( brallyPauseCursor < 0 ) brallyPauseCursor = 3;
    }

    if( gbPressed( BTN_A ) )
    {
        if( brallyPauseCursor == 0 ) brallyGameState = BRALLY_STATE_RUNNING;
        else if( brallyPauseCursor == 1 ) brallyRestart();
        else if( brallyPauseCursor == 2 ) { brallySelectedTrack = 0; brallyGameState = BRALLY_STATE_SELECT; }
        else brallyGameState = BRALLY_STATE_TITLE;
    }
}

void brallySelectTrackUpdate()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Track   Time Pos" );

    int offsetTrack = 0;
    if( brallySelectedTrack > 5 ) offsetTrack = brallySelectedTrack - 5;

    int i;
    int y = gbFontSize * gbFontHeight;
    for( i = offsetTrack; i < 6 + offsetTrack; i = i + 1 )
    {
        gbCursorX = 0;
        gbCursorY = y;
        if( brallySelectedTrack == i ) gbPrintString( ">" ); else gbPrintString( " " );
        gbPrintString( brallyTrackName( i ) );
        gbCursorX = 44;
        gbCursorY = y;
        int sec = eeprom_read_byte( i * 4 + 7 ) * 256 + eeprom_read_byte( i * 4 + 8 );
        if( sec > 0 )
        {
            int mm = sec / 60;
            int ss = sec % 60;
            if( mm < 10 ) gbPrintString( "0" );
            gbPrintNumber( mm );
            gbPrintString( ":" );
            if( ss < 10 ) gbPrintString( "0" );
            gbPrintNumber( ss );
            gbPrintString( " " );
            gbPrintNumber( eeprom_read_byte( i * 4 + 6 ) );
        }
        else
        {
            gbPrintString( "--:-- --" );
        }
        y = y + gbFontSize * gbFontHeight;
    }

    gbCursorX = 0;
    gbCursorY = y;
    gbPrintString( "A:go B:clr C:back" );

    if( gbPressed( BTN_C ) ) brallyGameState = BRALLY_STATE_RUNNING;
    if( gbPressed( BTN_DOWN ) ) { if( brallySelectedTrack < brallyTotalTrack - 1 ) brallySelectedTrack = brallySelectedTrack + 1; }
    if( gbPressed( BTN_UP ) ) { if( brallySelectedTrack > 0 ) brallySelectedTrack = brallySelectedTrack - 1; }
    if( gbHeld( BTN_B, 40 ) ) brallyClearEeprom();
    if( gbPressed( BTN_A ) ) brallyLoadLevel( brallySelectedTrack );
}

void brallyTitleTick()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 20;
    gbCursorY = 16;
    gbPrintString( "B-RALLY" );
    gbCursorX = 14;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        brallyLoadEeprom();
        brallyLevelStart();
    }
}

void gameBRally_init()
{
    gbBegin();
    brallyGameState = BRALLY_STATE_TITLE;
}

void gameBRally_update()
{
    if( !gbUpdate() ) return;

    if( brallyGameState == BRALLY_STATE_TITLE ) brallyTitleTick();
    else if( brallyGameState == BRALLY_STATE_RUNNING ) brallyRunningTick();
    else if( brallyGameState == BRALLY_STATE_COUNTDOWN ) brallyRunningTick();
    else if( brallyGameState == BRALLY_STATE_PAUSE ) brallyPauseMenuUpdate();
    else if( brallyGameState == BRALLY_STATE_GAMEOVER ) brallyGameOverTick();
    else brallySelectTrackUpdate();

    gbRenderFrame();
}
