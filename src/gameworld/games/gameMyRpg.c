#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// MyRPG (Frakasss, license: none specified -
// github.com/Frakasss/MyRPG). A small top-down overworld walker: an 84x48
// LCD-sized 3x3 grid of outdoor maps (country/village/cemetery/mountain
// tilesets), each map built from a compact bit-packed tile layout, two
// enterable buildings (a house and a shop) with their own separate
// full-screen interior scenes, and simple 8x8 sprite-pixel collision
// against whatever's already drawn on screen that same tick (no separate
// tile/solidity map at all - real upstream reads the just-drawn
// framebuffer back with getPixel() to decide whether a step is blocked).
// Upstream has no title screen at all (`gb.titleScreen(...)` is commented
// out in real `setup()`) and never reads Button A, so this port has no
// title-screen state either - gameplay starts immediately, matching
// upstream exactly.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (see gamePong.c's own header comment for
// why - this dialect has no classes/methods). `gb.battery.show = false;`
// was dropped outright (purely cosmetic on real hardware, already-
// established precedent from every other port here). Real upstream's
// `Player`/`MapSpecial` structs were ported directly as named
// `MyrpgPlayer`/`MyrpgMapSpecial` structs (anonymous `typedef struct{...}
// Name;` is rejected by this dialect - see VIRCON32_C_DIALECT.md). The
// `Player` struct's own real `checkCollision` field is never actually
// read or written anywhere in real upstream source (`MyRpg.ino`/
// `Map.ino`/`Player.ino`) - a genuinely dead field, dropped here rather
// than carried forward as unused clutter. The real `speeed` field-name
// typo (three e's, `Player.ino`) is a pure internal identifier with zero
// gameplay effect, so this port just spells it `speed` rather than
// reproducing the typo - unlike the real, behavior-affecting upstream
// quirks below, which ARE preserved exactly.
//
// Real upstream's loop-shared globals used purely as short-lived loop
// counters (`i`/`j`/`k`/`w`/`h`/`byteWidth`/`check01` in real
// `Map.ino`/`Player.ino`) were converted to genuine C locals inside each
// function that uses them - a straightforward, non-behavioral cleanup
// (nothing outside those functions ever reads them across ticks), not a
// gameplay change.
//
// Real upstream bitmap arrays use Arduino's own `B01111000`-style binary
// byte literals (no such syntax in this dialect) - every one was
// mechanically converted to hex byte values (verified against real
// upstream's own declared element counts, `2 + ceil(width/8)*height` for
// every bitmap here, before trusting the conversion). `pgm_read_byte()`/
// `PROGMEM` are dropped outright (this dialect's `avrCompat.h` already
// makes both real no-ops project-wide - every byte lives in plain RAM/ROM
// as a normal `int[]` array here, same as every other ported game's own
// bitmap tables).
//
// **Two real, deliberately-preserved upstream quirks, both confirmed by
// reading `Map.ino`'s own `output_map()` directly rather than assumed
// symmetric**: moving through the map grid to the LEFT or DOWN leaves
// `map_previous`/`myrpgMapPrevious` stale after the slide transition
// finishes (real upstream's own `//map_previous=map_current;` is
// commented out in exactly those two of the four slide-direction
// branches, not the other two) - a real, asymmetric upstream bug in the
// original code, not a porting mistake, preserved verbatim rather than
// "fixed" into a symmetric version real hardware never actually shipped.
// It has no crash/hang/unplayable consequence (mapZip/maps stay correctly
// indexed by the always-accurate `map_current` regardless), so it's
// preserved rather than platform-forced-fixed, per this project's own
// "preserve real upstream quirks unless they'd crash/hang/be genuinely
// unplayable here" standard.
//
// The house-entry/exit sequence's own real vignette-style black-border
// wipe (`output_transition()`'s own real unconditional
// fillRect(BLACK)+fillRect(WHITE) pair, drawn every transition tick
// *before* that tick's own map/house redraw layers on top of it) is
// ported call-for-call in the same order - the black border thickens
// toward the center as `animTransition` grows, then the actual scene
// still shows through the shrinking white window until the border fully
// closes, exactly matching real upstream's own draw sequence.
//
// No real shim primitive gap was hit porting this game - every real
// upstream call (`drawBitmap`, `fillRect`, `setColor`, `drawPixel`,
// `getPixel`, `buttons.repeat`) already has a direct `gbXxx()`
// equivalent.

// -----------------------------------------------------------------------------
//   Bitmaps (real upstream Map.ino/Player.ino byte tables, B-literals
//   converted to hex - see this file's own header comment above)
// -----------------------------------------------------------------------------

int myrpgTree[62] =
{
    24, 20, 0x0, 0x0, 0x0, 0x3, 0xf8, 0x0, 0xe, 0xee, 0x0, 0x1f, 0x1f, 0x0, 0x2f, 0xfe, 0x80,
    0x77, 0xfd, 0xc0, 0xb8, 0xe3, 0xa0, 0xdf, 0x1f, 0x60, 0xef, 0xfe, 0xe0, 0xf1, 0xf1, 0xe0,
    0xbe, 0xf, 0xa0, 0xdf, 0xff, 0x60, 0x6f, 0xfe, 0xc0, 0x31, 0xf1, 0x80, 0x1e, 0xf, 0x0,
    0x7, 0xfc, 0x0, 0x7, 0xfc, 0x0, 0xf, 0xfe, 0x0, 0x1f, 0xff, 0x0, 0x1f, 0xbf, 0x0,
};

int myrpgFenceH[22] =
{
    16, 10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x63, 0x0, 0x94, 0x80, 0xff, 0x80, 0xff, 0x80, 0xf7,
    0x80, 0x0, 0x0, 0x0, 0x0,
};

int myrpgFenceV[22] =
{
    16, 10, 0xc, 0x0, 0x12, 0x0, 0x1e, 0x0, 0x1e, 0x0, 0x1e, 0x0, 0xc, 0x0, 0x12, 0x0, 0x1e,
    0x0, 0x1e, 0x0, 0x1e, 0x0,
};

int myrpgBush[22] =
{
    16, 10, 0x0, 0x0, 0x1c, 0x0, 0x6a, 0x0, 0xd5, 0x0, 0xaa, 0x80, 0xd5, 0x80, 0xaa, 0x80,
    0xd5, 0x80, 0x7f, 0x0, 0x1c, 0x0,
};

int myrpgHouse[62] =
{
    24, 20, 0x0, 0x0, 0x0, 0x1f, 0xff, 0x80, 0x2f, 0xff, 0x40, 0x6f, 0xff, 0x60, 0x6f, 0xff,
    0x60, 0xef, 0xff, 0x70, 0xff, 0xff, 0xf0, 0xe0, 0x0, 0x70, 0xdf, 0xff, 0xb0, 0xdf, 0xff,
    0xb0, 0xbf, 0xff, 0xd0, 0xbf, 0xff, 0xd0, 0xff, 0xff, 0xf0, 0x40, 0x0, 0x20, 0x5d, 0xfb,
    0xa0, 0x55, 0xa, 0xa0, 0x55, 0xa, 0xa0, 0x5d, 0xb, 0xa0, 0x41, 0x8, 0x20, 0x7f, 0xf, 0xe0,
};

int myrpgChurch[62] =
{
    24, 20, 0x0, 0x60, 0x0, 0x0, 0xf0, 0x0, 0x1, 0xf8, 0x0, 0x3, 0xfc, 0x0, 0x7, 0xfe, 0x0,
    0xf, 0xff, 0x0, 0x4, 0x2, 0x0, 0x4, 0x62, 0x0, 0x4, 0x92, 0x0, 0x1c, 0x93, 0x80, 0x3c,
    0x63, 0xc0, 0x7c, 0x3, 0xe0, 0xfc, 0x3, 0xf0, 0xfc, 0xf3, 0xf0, 0x45, 0xa, 0x20, 0x55,
    0xa, 0xa0, 0x55, 0xa, 0xa0, 0x45, 0xa, 0x20, 0x7d, 0xb, 0xe0, 0x7, 0xe, 0x0,
};

int myrpgBushDead[22] =
{
    16, 10, 0x0, 0x0, 0x23, 0x0, 0xb6, 0x40, 0x9c, 0xc0, 0xcd, 0x80, 0x6f, 0x80, 0x3f, 0x0,
    0x1c, 0x0, 0xe, 0x0, 0x1f, 0x0,
};

int myrpgGrave[22] =
{
    16, 10, 0x3e, 0x0, 0x49, 0x0, 0x5d, 0x0, 0x49, 0x0, 0x49, 0x0, 0x41, 0x0, 0x7f, 0x0, 0x55,
    0x0, 0x2a, 0x0, 0x55, 0x0,
};

int myrpgRock[22] =
{
    16, 10, 0x0, 0x0, 0x30, 0x0, 0x78, 0x0, 0x5e, 0x0, 0xcf, 0x0, 0x87, 0x80, 0x97, 0xc0, 0xa7,
    0xc0, 0x7f, 0xc0, 0x0, 0x0,
};

int myrpgHome02[530] =
{
    84, 48, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xe0, 0x0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x70, 0xd0, 0x0, 0xfc, 0x0, 0x38, 0x0, 0x0, 0x0, 0x0, 0x0, 0xb0, 0xc8, 0xfc,
    0x84, 0x0, 0x38, 0x0, 0x0, 0x0, 0x0, 0x1, 0x30, 0xc7, 0x3, 0x87, 0xff, 0xef, 0xff, 0xff,
    0xff, 0xff, 0xfe, 0x30, 0xc5, 0x2, 0xfc, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0xc5,
    0x2, 0x84, 0x0, 0x28, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0xc5, 0xfe, 0xb4, 0x0, 0x38, 0x0,
    0x0, 0xf, 0x78, 0x2, 0x30, 0xc5, 0xaa, 0xcc, 0x0, 0x0, 0x0, 0x0, 0x9, 0x48, 0x2, 0x30,
    0xc5, 0x56, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3f, 0xfe, 0x2, 0x30, 0xc5, 0xaa, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x40, 0x1, 0x2, 0x30, 0xc5, 0x56, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x82, 0x30,
    0xc5, 0xaa, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x82, 0x30, 0xc5, 0x56, 0x0, 0x0, 0x38,
    0x0, 0x0, 0x80, 0x0, 0x82, 0x30, 0xc5, 0xfe, 0x0, 0x0, 0x38, 0x0, 0x0, 0x8f, 0x78, 0x82,
    0x30, 0xc5, 0xfe, 0x0, 0x0, 0x38, 0x0, 0x0, 0xc9, 0x49, 0x82, 0x30, 0xc5, 0x86, 0x0, 0x0,
    0x38, 0x0, 0x0, 0xf9, 0xcf, 0x82, 0x30, 0xc4, 0x0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x79, 0xcf,
    0x2, 0x30, 0xc4, 0x0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x6f, 0x7b, 0x2, 0x30, 0xff, 0xff, 0xff,
    0xff, 0xf8, 0x0, 0x0, 0x69, 0x4b, 0x2, 0x30, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x0, 0x0, 0x9,
    0x48, 0x2, 0x30, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0xe0, 0x0,
    0x0, 0x0, 0x38, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0xd3, 0xc0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x0,
    0x0, 0x2, 0x30, 0xcc, 0x27, 0xdf, 0xdf, 0x38, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0xc4, 0x28,
    0x2d, 0xa0, 0xf8, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0xc7, 0xf8, 0x20, 0x20, 0xb8, 0x0, 0x0,
    0x20, 0x0, 0x3, 0x30, 0xc4, 0x28, 0x2d, 0xa0, 0xa8, 0x0, 0x0, 0x50, 0x0, 0x7, 0x30, 0xc4,
    0x2f, 0xff, 0xff, 0xa8, 0x0, 0x0, 0x5e, 0x0, 0xf, 0x30, 0xc4, 0x69, 0x27, 0x24, 0xa8, 0x0,
    0x0, 0x51, 0x0, 0xf, 0x30, 0xc4, 0x29, 0x28, 0xa4, 0xb8, 0x0, 0x0, 0x5d, 0x0, 0xf, 0x30,
    0xc4, 0x2f, 0xff, 0xff, 0x80, 0x0, 0x0, 0x53, 0x0, 0x3f, 0x30, 0xc7, 0xe0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x52, 0x0, 0x2f, 0x30, 0xc4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x52, 0x0, 0x2f, 0x30,
    0xc4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x72, 0x0, 0x2f, 0x30, 0xc4, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x52, 0x0, 0x2e, 0x30, 0xc4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5e, 0x0, 0x2a, 0x30,
    0xc4, 0x0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x41, 0x0, 0x22, 0x30, 0xc4, 0x0, 0x0, 0x0, 0x38,
    0x0, 0x0, 0x41, 0x0, 0x3e, 0x30, 0xc4, 0x0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x7f, 0x0, 0x22,
    0x30, 0xc4, 0x0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0xc7, 0xff, 0xff, 0xff,
    0xf8, 0x7, 0xff, 0xff, 0xff, 0xfe, 0x30, 0xc8, 0x0, 0x0, 0x0, 0x38, 0x4, 0x0, 0x0, 0x0,
    0x1, 0x30, 0xd0, 0x0, 0x0, 0x0, 0x38, 0x4, 0x0, 0x0, 0x0, 0x0, 0xb0, 0xe0, 0x0, 0x0, 0x0,
    0x38, 0x4, 0x0, 0x0, 0x0, 0x0, 0x70, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x7, 0xff, 0xff, 0xff,
    0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x7, 0xff, 0xff, 0xff, 0xff, 0xf0,
};

int myrpgShop[530] =
{
    84, 48, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x30, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc0, 0x28, 0x36, 0xdb, 0xde, 0xf7, 0x8a, 0x28, 0xaa,
    0x31, 0x81, 0x40, 0x24, 0x1c, 0x73, 0xde, 0x63, 0x1b, 0x6c, 0xee, 0x7b, 0xc2, 0x40, 0x22,
    0x1c, 0x72, 0x52, 0x0, 0x0, 0x0, 0xee, 0x0, 0x4, 0x40, 0x21, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x8, 0x40, 0x20, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x40,
    0x20, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x20, 0x8f, 0xff, 0xff, 0xfc,
    0x3, 0xff, 0xff, 0xff, 0x10, 0x40, 0x20, 0x8a, 0xaa, 0xaa, 0xac, 0x2, 0xaa, 0xaa, 0xab,
    0x10, 0x40, 0x20, 0x8d, 0x55, 0x55, 0x54, 0x3, 0x55, 0x55, 0x55, 0x10, 0x40, 0x20, 0x8f,
    0xff, 0xff, 0xfc, 0x3, 0xff, 0xff, 0xff, 0x10, 0x40, 0x20, 0x88, 0x42, 0x10, 0x84, 0x2,
    0x0, 0x0, 0x1, 0x10, 0x40, 0x20, 0x89, 0x42, 0x50, 0x94, 0x3, 0xb5, 0xb5, 0xb5, 0x10, 0x40,
    0x20, 0x8a, 0x4a, 0x92, 0xa4, 0x3, 0xff, 0xff, 0xff, 0x10, 0x40, 0x20, 0x88, 0x52, 0x14,
    0x84, 0x2, 0x0, 0x0, 0x1, 0x10, 0x40, 0x20, 0x88, 0x42, 0x10, 0x84, 0x2, 0xd6, 0xb5, 0xad,
    0x10, 0x40, 0x20, 0x8f, 0xff, 0xff, 0xfc, 0x3, 0xff, 0xff, 0xff, 0x10, 0x40, 0x20, 0x80,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x20, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x10, 0x40, 0x20, 0x8f, 0xff, 0xff, 0xfc, 0x3, 0xff, 0xff, 0xff, 0x10, 0x40, 0x20,
    0x8a, 0xaa, 0xaa, 0xac, 0x2, 0xaa, 0xaa, 0xab, 0x10, 0x40, 0x20, 0x8d, 0x55, 0x55, 0x54,
    0x3, 0x55, 0x55, 0x55, 0x10, 0x40, 0x20, 0x8f, 0xff, 0xff, 0xfc, 0x3, 0xff, 0xff, 0xff,
    0x10, 0x40, 0x20, 0x88, 0x0, 0x0, 0x4, 0x2, 0x10, 0x84, 0x21, 0x10, 0x40, 0x20, 0x8e, 0xd6,
    0xd6, 0xd4, 0x2, 0x50, 0x94, 0x25, 0x10, 0x40, 0x20, 0x8f, 0xff, 0xff, 0xfc, 0x2, 0x92,
    0xa4, 0xa9, 0x10, 0x40, 0x20, 0x88, 0x0, 0x0, 0x4, 0x2, 0x14, 0x85, 0x21, 0x10, 0x40, 0x20,
    0x8b, 0x5a, 0xd6, 0xb4, 0x2, 0x10, 0x84, 0x21, 0x10, 0x40, 0x20, 0x8f, 0xff, 0xff, 0xfc,
    0x3, 0xff, 0xff, 0xff, 0x10, 0x40, 0x20, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10,
    0x40, 0x20, 0x80, 0x1, 0xe0, 0x3c, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x20, 0x80, 0x3, 0xf0,
    0x24, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x20, 0x80, 0x7, 0x10, 0x7c, 0x0, 0x0, 0x0, 0x0,
    0x10, 0x40, 0x20, 0x80, 0x6, 0xb0, 0xa4, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x20, 0x80, 0x6,
    0x17, 0xc4, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x20, 0x80, 0x5, 0xe4, 0x84, 0x0, 0x0, 0x0,
    0x0, 0x10, 0x40, 0x20, 0x80, 0x1, 0xe4, 0x84, 0x0, 0x0, 0x0, 0x0, 0x10, 0x40, 0x20, 0x80,
    0x1, 0xe7, 0xfc, 0x0, 0x49, 0x24, 0x0, 0x10, 0x40, 0x20, 0x80, 0x1, 0x25, 0x54, 0x0, 0x6d,
    0xb6, 0x0, 0x10, 0x40, 0x20, 0x80, 0x0, 0x7, 0xfc, 0x0, 0x49, 0x24, 0x0, 0x10, 0x40, 0x20,
    0xff, 0xff, 0xff, 0xfc, 0x3, 0xff, 0xff, 0xff, 0xf0, 0x40, 0x21, 0x0, 0x0, 0x0, 0x4, 0x2,
    0x49, 0x24, 0x18, 0xc8, 0x40, 0x22, 0x0, 0x0, 0x0, 0x4, 0x2, 0x49, 0x24, 0x25, 0x24, 0x40,
    0x24, 0x0, 0x0, 0x0, 0x4, 0x2, 0x24, 0x92, 0x8, 0x42, 0x40, 0x28, 0x0, 0x0, 0x0, 0x4, 0x2,
    0x24, 0x92, 0x8, 0x41, 0x40, 0x30, 0x0, 0x0, 0x0, 0x4, 0x2, 0x0, 0x0, 0x0, 0x0, 0xc0, 0x3f,
    0xff, 0xff, 0xff, 0xfc, 0x3, 0xff, 0xff, 0xff, 0xff, 0xc0,
};

int myrpgM000[22] =
{
    32, 5, 0x80, 0x5, 0x0, 0x3a, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x80, 0x0, 0x80, 0x0, 0x0, 0x0,
    0x0, 0x11, 0x0, 0x0,
};

int myrpgM001[22] =
{
    32, 5, 0x10, 0x42, 0x0, 0xa5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0,
    0x0, 0xe0, 0x0, 0x0,
};

int myrpgM002[22] =
{
    32, 5, 0x2a, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x1, 0x0, 0x0,
};

int myrpgM003[22] =
{
    32, 5, 0x0, 0x80, 0x0, 0x51, 0x4, 0x0, 0x80, 0x0, 0x10, 0x80, 0x0, 0x0, 0x0, 0x0, 0x80,
    0x0, 0x0, 0x80, 0x0, 0x0,
};

int myrpgM004[22] =
{
    32, 5, 0x40, 0x80, 0x0, 0x0, 0x2, 0x8, 0x0, 0x4, 0x0, 0x4, 0x8, 0x0, 0x0, 0x60, 0x0, 0x4,
    0x0, 0x0, 0x0, 0x0,
};

int myrpgM005[22] =
{
    32, 5, 0x0, 0x1, 0x0, 0x0, 0x82, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0,
};

int myrpgM006[22] =
{
    32, 5, 0x0, 0x0, 0x80, 0x0, 0x0, 0x2a, 0x81, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x2a, 0x81,
    0x0, 0x0, 0x0, 0x80, 0x7e,
};

int myrpgM007[22] =
{
    32, 5, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x20, 0x88, 0x0, 0x20, 0x0, 0x0, 0x4, 0x0, 0x0, 0x81,
    0x0, 0x31, 0x80, 0x48,
};

int myrpgM008[22] =
{
    32, 5, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x90, 0x0, 0x0, 0x0, 0x80, 0x1, 0x82, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x3c,
};

int myrpgPlayerSprite[9] =
{
    8, 7, 0x78, 0xfc, 0x84, 0x84, 0x78, 0x78, 0x78,
};

int myrpgPlayerMask[6] =
{
    8, 4, 0x78, 0xfc, 0xfc, 0xfc,
};

int myrpgPlayerLegs00[3] =
{
    8, 1, 0x48,
};

int myrpgPlayerLegs01[3] =
{
    8, 1, 0x30,
};

int myrpgPlayerLegs02[3] =
{
    8, 1, 0x8,
};

int myrpgPlayerLegs03[3] =
{
    8, 1, 0x40,
};

// -----------------------------------------------------------------------------
//   Bitmap-pointer tables (real upstream `const byte* NAME[]={...}` arrays)
// -----------------------------------------------------------------------------

int* myrpgHouseBitmaps[2] = { myrpgHome02, myrpgShop };

int* myrpgMapCountry[4]  = { myrpgTree, myrpgBush, myrpgFenceV, myrpgFenceH };
int* myrpgMapVillage[4]  = { myrpgHouse, myrpgBush, myrpgFenceV, myrpgFenceH };
int* myrpgMapCemetery[4] = { myrpgChurch, myrpgGrave, myrpgFenceV, myrpgFenceH };
int* myrpgMapMountain[4] = { myrpgTree, myrpgBush, myrpgBushDead, myrpgRock };

// Real upstream `maps[9]` - which tileset category (0=country, 1=village,
// 2=cemetery, 3=mountain) each of the 9 outdoor maps (a 3x3 grid, row
// major) uses.
int myrpgMaps[9] = { 0, 0, 0, 0, 1, 0, 2, 3, 3 };

int* myrpgMapZip[9] =
{
    myrpgM000, myrpgM001, myrpgM002,
    myrpgM003, myrpgM004, myrpgM005,
    myrpgM006, myrpgM007, myrpgM008,
};

int* myrpgPlayerLegsH[3] = { myrpgPlayerLegs00, myrpgPlayerLegs01, myrpgPlayerLegs01 };
int* myrpgPlayerLegsV[3] = { myrpgPlayerLegs00, myrpgPlayerLegs02, myrpgPlayerLegs03 };

// -----------------------------------------------------------------------------
//   State
// -----------------------------------------------------------------------------

// Real upstream `gamestatus` values, preserved as plain numbers to match
// upstream's own literal switch/if-else checks directly:
//   0 = normal outdoor exploration
//   1 = sliding between two adjacent outdoor maps
//   2 = entering a house - vignette closing in over the outdoor map
//   3 = entering a house - vignette opening onto the house interior
//   4 = inside a house
//   5 = exiting a house - vignette closing in over the house interior
//   6 = exiting a house - vignette opening onto the outdoor map
int myrpgGamestatus;

int myrpgMapCurrent;
int myrpgMapPrevious;
int myrpgHouseCurrent;
int myrpgVertCurrent;
int myrpgVertPrevious;
int myrpgHorizCurrent;
int myrpgHorizPrevious;
// Real upstream `dir` - which of the 4 slide directions gamestatus==1 is
// currently animating (0=right, 1=left, 2=down, 3=up - matches upstream's
// own real assignments at each edge-of-map crossing, see myrpgMovePlayer()).
int myrpgDir;
int myrpgSlidespeed;
// Real upstream `anim` - a free-running 0..5 counter driving the player's
// own walk-cycle leg sprite (myrpgAnim/2 indexes the 3-frame leg tables).
int myrpgAnim;
int myrpgAnimTransition;

typedef struct{
    int x;
    int y;
    // Real upstream `player.dir` - which way the player is facing/walking:
    // 0=right, 1=left, 2=down, 3=up.
    int dir;
    int speed;
} MyrpgPlayer;
MyrpgPlayer myrpgPlayer;

typedef struct{
    // Real upstream field name `sprite` - despite the name, this is
    // actually the outdoor map index (0-8) this house/shop sits on, not a
    // graphic sprite id. Preserved verbatim (including the name) for a
    // direct match against upstream source.
    int sprite;
    int x;
    int y;
    int typ;
} MyrpgMapSpecial;
MyrpgMapSpecial myrpgMapSpecial[2];

// -----------------------------------------------------------------------------
//   Logic
// -----------------------------------------------------------------------------

void myrpgInitHouses()
{
    myrpgMapSpecial[ 0 ].sprite = 4; myrpgMapSpecial[ 0 ].x = 16; myrpgMapSpecial[ 0 ].y = 13; myrpgMapSpecial[ 0 ].typ = 0;
    myrpgMapSpecial[ 1 ].sprite = 4; myrpgMapSpecial[ 1 ].x = 66; myrpgMapSpecial[ 1 ].y = 23; myrpgMapSpecial[ 1 ].typ = 1;
}

// Direct port of real upstream drawMap() (Map.ino) - bitmap already uses
// this shim's own real {width,height,rows...} format (bitmap[0]=w,
// bitmap[1]=h, bitmap[2+] = packed row bytes), the same format real
// upstream's own pgm_read_byte(bitmap)/pgm_read_byte(bitmap+1)/bitmap+=2
// dance reads directly from PROGMEM. Each set bit at (column i, row j)
// draws one 10x10 tile sprite from whichever category table `spriteSet`
// (an outdoor map index 0-8, looked up in myrpgMaps[]) selects, with the
// tile's own sprite picked by i/8 (0-3) - real upstream's own compact
// "4 categories x 8 columns x 5 rows" bit-packed encoding.
void myrpgDrawMap( int* bitmap, int spriteSet, int horiz, int vert )
{
    int w = bitmap[ 0 ];
    int h = bitmap[ 1 ];
    int byteWidth = ( w + 7 ) / 8;
    int j;
    int i;

    for( j = 0; j < h; j = j + 1 )
    {
        for( i = 0; i < w; i = i + 1 )
        {
            if( bitmap[ 2 + j * byteWidth + i / 8 ] & ( 0x80 >> ( i % 8 ) ) )
            {
                int drawX = ( ( i % 8 ) * 10 ) + horiz;
                int drawY = ( ( j * 10 ) - 1 ) + vert;

                if( myrpgMaps[ spriteSet ] == 0 )
                {
                    gbDrawBitmap( drawX, drawY, myrpgMapCountry[ i / 8 ] );
                }
                else if( myrpgMaps[ spriteSet ] == 1 )
                {
                    gbDrawBitmap( drawX, drawY, myrpgMapVillage[ i / 8 ] );
                }
                else if( myrpgMaps[ spriteSet ] == 2 )
                {
                    gbDrawBitmap( drawX, drawY, myrpgMapCemetery[ i / 8 ] );
                }
                else if( myrpgMaps[ spriteSet ] == 3 )
                {
                    gbDrawBitmap( drawX, drawY, myrpgMapMountain[ i / 8 ] );
                }
            }
        }
    }
}

// Direct port of real upstream output_map() (Map.ino).
void myrpgOutputMap()
{
    if( myrpgGamestatus == 0 )
    {
        myrpgDrawMap( myrpgMapZip[ myrpgMapCurrent ], myrpgMapCurrent, 0, 0 );
    }
    else if( myrpgGamestatus == 1 )
    {
        myrpgDrawMap( myrpgMapZip[ myrpgMapPrevious ], myrpgMapPrevious, myrpgHorizPrevious, myrpgVertPrevious );
        myrpgDrawMap( myrpgMapZip[ myrpgMapCurrent ], myrpgMapCurrent, myrpgHorizCurrent, myrpgVertCurrent );

        if( myrpgDir == 0 )
        {
            if( myrpgHorizCurrent - myrpgSlidespeed > 0 )
            {
                myrpgHorizCurrent = myrpgHorizCurrent - myrpgSlidespeed;
                myrpgHorizPrevious = myrpgHorizPrevious - myrpgSlidespeed;
            }
            else
            {
                myrpgMapPrevious = myrpgMapCurrent;
                myrpgHorizCurrent = 0;
                myrpgHorizPrevious = 0;
            }
        }
        else if( myrpgDir == 1 )
        {
            if( myrpgHorizCurrent + myrpgSlidespeed < 0 )
            {
                myrpgHorizCurrent = myrpgHorizCurrent + myrpgSlidespeed;
                myrpgHorizPrevious = myrpgHorizPrevious + myrpgSlidespeed;
            }
            else
            {
                // Real upstream leaves myrpgMapPrevious stale here (its own
                // `//map_previous=map_current;` is commented out) - a real,
                // preserved upstream quirk, see this file's own header
                // comment.
                myrpgHorizCurrent = 0;
                myrpgHorizPrevious = 0;
            }
        }
        else if( myrpgDir == 2 )
        {
            if( myrpgVertCurrent - myrpgSlidespeed > 0 )
            {
                myrpgVertCurrent = myrpgVertCurrent - myrpgSlidespeed;
                myrpgVertPrevious = myrpgVertPrevious - myrpgSlidespeed;
            }
            else
            {
                // Same preserved upstream quirk as myrpgDir==1 above.
                myrpgVertCurrent = 0;
                myrpgVertPrevious = 0;
            }
        }
        else if( myrpgDir == 3 )
        {
            if( myrpgVertCurrent + myrpgSlidespeed < 0 )
            {
                myrpgVertCurrent = myrpgVertCurrent + myrpgSlidespeed;
                myrpgVertPrevious = myrpgVertPrevious + myrpgSlidespeed;
            }
            else
            {
                myrpgMapPrevious = myrpgMapCurrent;
                myrpgVertCurrent = 0;
                myrpgVertPrevious = 0;
            }
        }
    }
    else if( myrpgGamestatus == 4 )
    {
        gbDrawBitmap( 0, 0, myrpgHouseBitmaps[ myrpgHouseCurrent ] );
    }
}

// Direct port of real upstream output_transition() (Map.ino) - the real
// vignette-style black-border wipe used when entering/leaving a house.
void myrpgOutputTransition()
{
    if( myrpgGamestatus == 2 || myrpgGamestatus == 3 || myrpgGamestatus == 5 || myrpgGamestatus == 6 )
    {
        gbSetColor( GB_BLACK );
        gbFillRect( 0, 0, LCDWIDTH, LCDHEIGHT );
        gbSetColor( GB_WHITE );
        gbFillRect( myrpgAnimTransition, myrpgAnimTransition, LCDWIDTH - ( 2 * myrpgAnimTransition ), LCDHEIGHT - ( 2 * myrpgAnimTransition ) );
        gbSetColor( GB_BLACK );
    }

    if( myrpgGamestatus == 2 )
    {
        myrpgDrawMap( myrpgMapZip[ myrpgMapCurrent ], myrpgMapCurrent, 0, 0 );
        if( myrpgAnimTransition + 6 > 24 )
        {
            myrpgGamestatus = 3;
            myrpgPlayer.x = 39;
            myrpgPlayer.y = 42;
            myrpgPlayer.dir = 3;
        }
        else
        {
            myrpgAnimTransition = myrpgAnimTransition + 6;
        }
    }
    else if( myrpgGamestatus == 3 )
    {
        gbDrawBitmap( 0, 0, myrpgHouseBitmaps[ myrpgHouseCurrent ] );
        if( myrpgAnimTransition - 6 < 0 )
        {
            myrpgGamestatus = 4;
        }
        else
        {
            myrpgAnimTransition = myrpgAnimTransition - 6;
        }
    }
    else if( myrpgGamestatus == 5 )
    {
        gbDrawBitmap( 0, 0, myrpgHouseBitmaps[ myrpgHouseCurrent ] );
        if( myrpgAnimTransition + 6 > 24 )
        {
            myrpgGamestatus = 6;
        }
        else
        {
            myrpgAnimTransition = myrpgAnimTransition + 6;
        }
    }
    else if( myrpgGamestatus == 6 )
    {
        myrpgDrawMap( myrpgMapZip[ myrpgMapCurrent ], myrpgMapCurrent, 0, 0 );
        if( myrpgAnimTransition - 6 < 0 )
        {
            myrpgGamestatus = 0;
        }
        else
        {
            myrpgAnimTransition = myrpgAnimTransition - 6;
        }
    }
}

// Direct ports of real upstream move_right()/move_left()/move_up()/
// move_down() (Player.ino) - real, single-pixel-row-strip collision
// against whatever is already drawn in the framebuffer THIS tick (the map
// is always drawn before this runs, matching real upstream's own
// output_map() -> fctnt_movePlayer() call order, preserved exactly in
// gameMyRpg_update() below), not a separate solidity/tile map at all.
void myrpgMoveRight()
{
    int collide = 0;
    int i;
    for( i = myrpgPlayer.y + 5; i < myrpgPlayer.y + 7; i = i + 1 )
    {
        if( gbGetPixel( myrpgPlayer.x + 4 + myrpgPlayer.speed, i ) ) { collide = 1; }
    }
    if( collide == 0 ) { myrpgPlayer.x = myrpgPlayer.x + myrpgPlayer.speed; }
}

void myrpgMoveLeft()
{
    int collide = 0;
    int i;
    for( i = myrpgPlayer.y + 5; i < myrpgPlayer.y + 7; i = i + 1 )
    {
        if( gbGetPixel( myrpgPlayer.x + 1 - myrpgPlayer.speed, i ) ) { collide = 1; }
    }
    if( collide == 0 ) { myrpgPlayer.x = myrpgPlayer.x - myrpgPlayer.speed; }
}

void myrpgMoveUp()
{
    int collide = 0;
    int i;
    for( i = myrpgPlayer.x + 1; i < myrpgPlayer.x + 5; i = i + 1 )
    {
        if( gbGetPixel( i, myrpgPlayer.y + 4 ) ) { collide = 1; }
    }
    if( collide == 0 ) { myrpgPlayer.y = myrpgPlayer.y - myrpgPlayer.speed; }
}

void myrpgMoveDown()
{
    int collide = 0;
    int i;
    for( i = myrpgPlayer.x + 1; i < myrpgPlayer.x + 5; i = i + 1 )
    {
        if( gbGetPixel( i, myrpgPlayer.y + 8 ) ) { collide = 1; }
    }
    if( collide == 0 ) { myrpgPlayer.y = myrpgPlayer.y + myrpgPlayer.speed; }
}

// Direct port of real upstream fctnt_movePlayer() (Player.ino).
void myrpgMovePlayer()
{
    if( myrpgGamestatus == 0 )
    {
        if( gbRepeat( BTN_B, 1 ) )
        {
            myrpgPlayer.speed = 2;
        }
        else
        {
            myrpgPlayer.speed = 1;
        }

        if( gbRepeat( BTN_RIGHT, 1 ) )
        {
            myrpgPlayer.dir = 0;
            if( myrpgPlayer.x <= 78 )
            {
                myrpgMoveRight();
            }
            else
            {
                if( ( myrpgMapCurrent / 3 ) == ( ( myrpgMapCurrent + 1 ) / 3 ) )
                {
                    myrpgMapCurrent = myrpgMapCurrent + 1;
                    myrpgDir = 0;
                    myrpgVertCurrent = 0;
                    myrpgVertPrevious = 0;
                    myrpgHorizCurrent = 84;
                    myrpgHorizPrevious = 0;
                    myrpgGamestatus = 1;
                }
            }
        }

        if( gbRepeat( BTN_LEFT, 1 ) )
        {
            myrpgPlayer.dir = 1;
            if( myrpgPlayer.x > 0 )
            {
                myrpgMoveLeft();
            }
            else
            {
                if( ( myrpgMapCurrent / 3 ) == ( ( myrpgMapCurrent - 1 ) / 3 ) && ( myrpgMapCurrent - 1 >= 0 ) )
                {
                    myrpgMapCurrent = myrpgMapCurrent - 1;
                    myrpgDir = 1;
                    myrpgVertCurrent = 0;
                    myrpgVertPrevious = 0;
                    myrpgHorizCurrent = -84;
                    myrpgHorizPrevious = 0;
                    myrpgGamestatus = 1;
                }
            }
        }

        if( gbRepeat( BTN_UP, 1 ) )
        {
            myrpgPlayer.dir = 3;
            if( myrpgPlayer.y > 0 )
            {
                myrpgMoveUp();
            }
            else
            {
                if( ( myrpgMapCurrent - 3 ) >= 0 )
                {
                    myrpgMapCurrent = myrpgMapCurrent - 3;
                    myrpgDir = 3;
                    myrpgVertCurrent = -48;
                    myrpgVertPrevious = 0;
                    myrpgHorizCurrent = 0;
                    myrpgHorizPrevious = 0;
                    myrpgGamestatus = 1;
                }
            }
        }

        if( gbRepeat( BTN_DOWN, 1 ) )
        {
            myrpgPlayer.dir = 2;
            if( myrpgPlayer.y < 42 )
            {
                myrpgMoveDown();
            }
            else
            {
                if( ( myrpgMapCurrent + 3 ) < 9 )
                {
                    myrpgMapCurrent = myrpgMapCurrent + 3;
                    myrpgDir = 2;
                    myrpgVertCurrent = 48;
                    myrpgVertPrevious = 0;
                    myrpgHorizCurrent = 0;
                    myrpgHorizPrevious = 0;
                    myrpgGamestatus = 1;
                }
            }
        }
    }
    else if( myrpgGamestatus == 1 )
    {
        if( myrpgDir == 0 )
        {
            if( myrpgPlayer.x - myrpgSlidespeed > 0 )
            {
                myrpgPlayer.x = myrpgPlayer.x - myrpgSlidespeed;
            }
            else
            {
                myrpgPlayer.x = 0;
                myrpgGamestatus = 0;
                myrpgMapPrevious = myrpgMapCurrent;
            }
        }
        else if( myrpgDir == 1 )
        {
            if( myrpgPlayer.x + myrpgSlidespeed < 78 )
            {
                myrpgPlayer.x = myrpgPlayer.x + myrpgSlidespeed;
            }
            else
            {
                myrpgPlayer.x = 78;
                myrpgGamestatus = 0;
                myrpgMapPrevious = myrpgMapCurrent;
            }
        }
        else if( myrpgDir == 2 )
        {
            if( myrpgPlayer.y - myrpgSlidespeed > 0 )
            {
                myrpgPlayer.y = myrpgPlayer.y - myrpgSlidespeed;
            }
            else
            {
                myrpgPlayer.y = 0;
                myrpgGamestatus = 0;
                myrpgMapPrevious = myrpgMapCurrent;
            }
        }
        else if( myrpgDir == 3 )
        {
            if( myrpgPlayer.y + myrpgSlidespeed < 42 )
            {
                myrpgPlayer.y = myrpgPlayer.y + myrpgSlidespeed;
            }
            else
            {
                myrpgPlayer.y = 42;
                myrpgGamestatus = 0;
                myrpgMapPrevious = myrpgMapCurrent;
            }
        }
    }
    else if( myrpgGamestatus == 4 )
    {
        myrpgPlayer.speed = 1;
        if( gbRepeat( BTN_RIGHT, 1 ) ) { myrpgPlayer.dir = 0; myrpgMoveRight(); }
        if( gbRepeat( BTN_LEFT, 1 ) )  { myrpgPlayer.dir = 1; myrpgMoveLeft(); }
        if( gbRepeat( BTN_UP, 1 ) )    { myrpgPlayer.dir = 3; myrpgMoveUp(); }
        if( gbRepeat( BTN_DOWN, 1 ) )  { myrpgPlayer.dir = 2; myrpgMoveDown(); }
    }
}

// Direct port of real upstream output_player() (Player.ino) - a WHITE
// solid mask drawn first (so the player sprite is never blended with
// whatever's underneath), then the BLACK player sprite, then a couple of
// hand-placed "eye" pixels and the current walk-cycle leg frame, all keyed
// off myrpgPlayer.dir.
void myrpgOutputPlayer()
{
    if( myrpgGamestatus != 2 && myrpgGamestatus != 3 && myrpgGamestatus != 5 && myrpgGamestatus != 6 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y, myrpgPlayerMask );
        gbSetColor( GB_BLACK );
        gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y, myrpgPlayerSprite );

        if( myrpgPlayer.dir == 0 )
        {
            gbDrawPixel( myrpgPlayer.x + 2, myrpgPlayer.y + 2 );
            gbDrawPixel( myrpgPlayer.x + 4, myrpgPlayer.y + 2 );
            gbDrawPixel( myrpgPlayer.x + 6, myrpgPlayer.y + 1 );
            if( gbRepeat( BTN_RIGHT, 1 ) )
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsH[ myrpgAnim / 2 ] );
            }
            else
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsH[ 0 ] );
            }
        }
        else if( myrpgPlayer.dir == 1 )
        {
            gbDrawPixel( myrpgPlayer.x + 1, myrpgPlayer.y + 2 );
            gbDrawPixel( myrpgPlayer.x + 3, myrpgPlayer.y + 2 );
            gbDrawPixel( myrpgPlayer.x - 1, myrpgPlayer.y + 1 );
            if( gbRepeat( BTN_LEFT, 1 ) )
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsH[ myrpgAnim / 2 ] );
            }
            else
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsH[ 0 ] );
            }
        }
        else if( myrpgPlayer.dir == 2 )
        {
            gbDrawPixel( myrpgPlayer.x + 1, myrpgPlayer.y + 2 );
            gbDrawPixel( myrpgPlayer.x + 4, myrpgPlayer.y + 2 );
            if( gbRepeat( BTN_DOWN, 1 ) )
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsV[ myrpgAnim / 2 ] );
            }
            else
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsV[ 0 ] );
            }
        }
        else if( myrpgPlayer.dir == 3 )
        {
            if( gbRepeat( BTN_UP, 1 ) )
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsV[ myrpgAnim / 2 ] );
            }
            else
            {
                gbDrawBitmap( myrpgPlayer.x, myrpgPlayer.y + 7, myrpgPlayerLegsV[ 0 ] );
            }
        }
    }
}

// Direct port of real upstream fctnt_anim() (MyRpg.ino).
void myrpgUpdateAnim()
{
    myrpgAnim = ( myrpgAnim + 1 ) % 6;
}

// Direct port of real upstream fctnt_checkIn() (Map.ino) - entering either
// house's real 5x2px doorway trigger rect switches into the entering-house
// transition.
void myrpgCheckIn()
{
    if( myrpgGamestatus == 0 )
    {
        int i;
        for( i = 0; i < 2; i = i + 1 )
        {
            if( myrpgMapSpecial[ i ].sprite == myrpgMapCurrent )
            {
                if( myrpgPlayer.x > myrpgMapSpecial[ i ].x && myrpgPlayer.x < myrpgMapSpecial[ i ].x + 5 &&
                    myrpgPlayer.y < myrpgMapSpecial[ i ].y && myrpgPlayer.y > myrpgMapSpecial[ i ].y - 2 )
                {
                    myrpgGamestatus = 2;
                    myrpgAnimTransition = 0;
                    myrpgHouseCurrent = myrpgMapSpecial[ i ].typ;
                    myrpgMapPrevious = i;
                }
            }
        }
    }
}

// Direct port of real upstream fctnt_checkOut() (Map.ino) - walking off
// the bottom edge of a house interior switches into the exiting-house
// transition, restoring the player back at the doorway they entered.
void myrpgCheckOut()
{
    if( myrpgGamestatus == 4 )
    {
        if( myrpgPlayer.y > 42 )
        {
            myrpgAnimTransition = 0;
            myrpgPlayer.x = myrpgMapSpecial[ myrpgMapPrevious ].x;
            myrpgPlayer.y = myrpgMapSpecial[ myrpgMapPrevious ].y;
            myrpgPlayer.dir = 2;
            myrpgGamestatus = 5;
            myrpgMapPrevious = myrpgMapCurrent;
        }
    }
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

// Real upstream never calls gb.titleScreen() at all (it's commented out in
// real setup()) and never reads Button A anywhere - so this port has no
// title-screen state either; gameplay starts immediately, exactly like
// upstream.
void gameMyRpg_init()
{
    gbBegin();

    myrpgGamestatus = 0;
    myrpgMapCurrent = 0;
    myrpgMapPrevious = 0;
    myrpgVertCurrent = 0;
    myrpgVertPrevious = 0;
    myrpgHouseCurrent = 0;
    myrpgHorizCurrent = 0;
    myrpgHorizPrevious = 0;
    myrpgDir = 0;
    myrpgSlidespeed = 12;
    myrpgPlayer.x = 24;
    myrpgPlayer.y = 20;
    myrpgPlayer.dir = 0;
    myrpgPlayer.speed = 1;
    myrpgAnim = 0;
    myrpgAnimTransition = 0;

    myrpgInitHouses();
}

// Direct port of real upstream loop()'s own `if(gb.update()){...}` body -
// same call order as real upstream (output_map/output_transition/
// fctnt_movePlayer/output_player/fctnt_anim/fctnt_checkIn/fctnt_checkOut),
// which matters here: myrpgMovePlayer()'s own collision checks read back
// pixels myrpgOutputMap() just drew THIS tick via gbGetPixel().
void gameMyRpg_update()
{
    if( !gbUpdate() ) return;

    myrpgOutputMap();
    myrpgOutputTransition();
    myrpgMovePlayer();
    myrpgOutputPlayer();
    myrpgUpdateAnim();
    myrpgCheckIn();
    myrpgCheckOut();

    gbRenderFrame();
}
