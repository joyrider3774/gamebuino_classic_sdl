#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Artillery (Frakasss, no license specified -
// github.com/Frakasss/Artillery). Read completely (Artillery.ino/
// function.ino/images.ino/output.ino/sounds.ino) before porting, per this
// project's own methodology - confirms this is NOT a simple 1v1 duel: it's
// a real local, hot-seat, turn-based Worms-style artillery battle with 2-4
// TEAMS of 1-4 units each (all configured together via a real in-game
// Settings/Options screen before a match starts), any number of those teams
// optionally AI-controlled (`nbCpuTeam`, 0 up to nbTeam-1) - upstream's own
// real `Player.isIA` flag is decided per-unit from its team index
// (`team >= nbTeam - nbCpuTeam`), not from a fixed "player vs CPU" design.
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` call (this dialect has no classes/methods - see gamePong.c's
// own header comment); `byte`/`boolean` became plain `int` throughout
// (matching gameCastleDefence.c's own established convention - `dir`
// specifically needs this, since upstream uses it arithmetically,
// `(dir*2)-1`, not just as a flag); `random(0,N)` became `arand(N)`; no
// `switch` statement is used anywhere (every real upstream `switch` became
// an if/else-if chain, matching this project's own "no switch statement
// proven to work" convention). Global naming prefix: `art`.
//
// REAL BITMAP DATA: `gamelogo[]`/`landscapetiles[12][6]`/`units[4][6]`/
// `levels[26][38]`/`options[]` (images.ino) are all real Arduino
// `B00000000`-style binary literals - converted to `0x` hex by a small
// one-off local Python script (byte-count verified against each table's
// own real `{width,height,...}` header: 315 data bytes for the 72x35
// gamelogo, 26*36=936 for the 21x12 levels, 30 for the 24x10 options
// icon), matching gameCastleDefence.c's own established precedent for this
// exact situation. `trajParamX[]`/`trajParamY[]` and `soundfx[6][8]`
// (sounds.ino) were already plain decimal, copied verbatim.
//
// DEAD CODE, CONFIRMED AND NOT PORTED (matching gameCastleDefence.c's own
// "screen" precedent for exactly this situation - grep-confirmed against
// the real source, not assumed):
// - `byte landscapeZip[21][12];` (Artillery.ino) is declared but never
//   read or written anywhere else in the real source.
// - The top-level `byte nbAliveTeam;` global (Artillery.ino) is write-only
//   (set once, `nbAliveTeam = nbTeam;`, in the NEW_LEVEL case) and never
//   read - the real end-of-round alive-team count instead comes from a
//   completely separate LOCAL `byte nbAliveTeam = 0;` that shadows it
//   inside `fnctn_gameOver()`. Only the real local one is ported here (as
//   `aliveTeams`, a plain function-local in `artGameOver()`).
// - `outpt_title()` is defined but never called anywhere - every real
//   `gb.titleScreen(gamelogo)` call site invokes that directly, not through
//   this wrapper.
//
// TITLE SCREEN: real `gb.titleScreen(gamelogo)` is called at 3 real sites -
// once blocking in `main_initGame()` (setup-time), and twice more as a
// genuine "show the splash" gesture (Button C, from both the map-select and
// options screens - it does NOT return to any top-level menu, it just shows
// the logo until A is pressed again, then resumes exactly where it was).
// Converted into one shared `ART_TITLE` state plus an `artTitleReturnState`
// field remembering which of the two mid-game screens to resume (matching
// this project's own "blocking loop -> explicit resumable state" treatment,
// e.g. gamePong.c/gameCastleDefence.c). Real `main_initGame()`'s own
// config-default-setting code (upstream places it textually AFTER its own
// blocking `titleScreen()` call) instead runs immediately in
// `gameArtillery_init()`, before the title is ever shown - behaviorally
// identical, since nothing reads those config vars while the title screen
// is up either way.
//
// SOUND: `artPlaySoundFx()` is a direct, byte-for-byte port of real
// upstream's own `outpt_soundfx(byte fxno)` - the same 4
// `gbSoundCommand()` calls (volume/instrument/slide/arpeggio, always
// channel 0, matching upstream's own hardcoded `0` argument throughout)
// plus a final `gbPlayNoteChannel()`, against the existing `artSoundFx[][]`
// table.
//
// PRESERVED REAL UPSTREAM BUGS (kept exactly as shipped, matching this
// project's own default per CLAUDE.md):
// 1) `fnctn_checkPlayerPos()`'s real "get unstuck" test -
//    `if(getPixel(x+2,y+3)==1 || getPixel(x+1,y+3)==1 && dead==0)` - is a
//    genuine operator-precedence trap (`&&` binds tighter than `||`, so
//    this is really `A || (B && C)`): the x+2 column check pushes ANY
//    player up regardless of `dead`, while only the x+1 column check is
//    gated on `dead==0`. Ported with the same real precedence
//    (`artCheckPlayerPos()` below), not "cleaned up" into `(A||B)&&C`.
// 2) `fnctn_ia()`'s real target-search simulation computes `rocket.x`
//    using `trajParamY[...]` (not `trajParamX[...]`) for the HORIZONTAL
//    offset, at both places it builds a candidate shot - a likely copy-
//    paste slip from the real firing code in `fnctn_checkbuttons()`
//    (which correctly uses `trajParamX` there). Ported exactly
//    (`artAiThink()` below uses `artTrajParamY` for `artRocket.x` too) -
//    "fixing" this would give the AI a genuinely different (arguably
//    better) simulated aim than upstream ever shipped.
// 3) The AI's own angle search loop (`for(fct_countr2=8;fct_countr2>0;
//    fct_countr2--)`) never tries index 0 - the AI never considers the
//    maximum-elevation shot a human player can select. Ported with the
//    same real loop bound.
//
// DELIBERATE SAFETY ADAPTATIONS (not preserved bugs - real AVR byte
// wraparound papering over an out-of-bounds risk that this project's own
// avrCompat.h convention does not reproduce, see gameCastleDefence.c's own
// header comment on this exact convention):
// - Real `Rocket.y` is an unsigned `byte` upstream; a steep upward shot
//   that pushes it "negative" really wraps to a large positive value on
//   real hardware, which eventually satisfies the search loop's own
//   `rocket.y>48` exit test. This shim's `artRocket.y` is a full-range
//   `int` (no wraparound), so `artAiThink()`'s own inner `while` loop adds
//   an explicit `artRocket.y < 0` exit check upstream never needed - without
//   it, a steep-upward simulated shot could loop forever instead of
//   terminating.
// - Every `landscape[x][y]` read/write whose index derives from a live
//   rocket position (`artCheckCollision()`, `artRebuildMap()`,
//   `artAiThink()`) goes through two small bounds-safe helpers,
//   `artLandscapeGetOr()`/`artLandscapeSet()`, instead of a raw array
//   access - real hardware's own equivalent out-of-range index either gets
//   caught by the same byte-wraparound or reads/writes adjacent real AVR
//   RAM (harmless there); this dialect has no such safety net; an actual
//   out-of-bounds array access here is undefined and could silently
//   corrupt an unrelated global. `artLandscapeSet()` also fixes a genuine
//   off-by-one in upstream's OWN already-present defensive bound checks
//   inside `fnctn_rebuildMap()` (`<=21`/`<=12`, where the real valid index
//   range for a `[21][12]` array is `<=20`/`<=11`) - the original author
//   clearly intended to guard exactly this case and simply miscounted, so
//   the corrected bound serves that same, obviously-intended purpose
//   rather than reproducing the typo and risking real memory corruption.
//
// No real `setColor(GRAY)`/`setColor(INVERT with a bitmap)`/`setFont()`/
// `EEPROM.*` call exists anywhere in the real source (grep-confirmed
// directly) - `GB_INVERT` IS used (real explosion-flash animation in
// `outpt_boom()`), ported directly as `GB_INVERT`; no GRAY, no custom font
// (inherits this shim's own real `gbFont3x5` default, matching every other
// ported game that never calls `setFont()` upstream either), no EEPROM
// (no save data upstream).

// -----------------------------------------------------------------------------
// Real upstream bitmaps (images.ino), B-binary converted to 0x hex.
// -----------------------------------------------------------------------------

int artGamelogoBitmap[317]= {
    72,35,
    0x0,0x0,0x0,0x82,0x54,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x5B,0x15,0xB5,0x0,
    0x0,0x0,0x0,0x0,0x1,0xD2,0x55,0x25,0x0,0x0,0x0,0x0,0x0,0x1,0x51,0x54,
    0xA3,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x0,0x0,0xF,
    0xFF,0xFF,0xFA,0x60,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x14,0x0,0x0,0x0,0x1,0x28,0x0,0x0,0x0,0x15,0x5A,0x4C,0x14,0xC2,0xA3,0x60,
    0x0,0x0,0x1D,0x55,0x2A,0x14,0x83,0xAA,0x50,0x0,0x0,0x15,0x95,0x6A,0x9,0x82,
    0xA9,0x50,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0xFF,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x3,0xFF,0xE0,0x0,0xE,
    0x0,0x0,0x0,0x0,0x7,0xFF,0xE0,0x0,0x31,0x80,0x0,0x0,0x0,0xF,0xFF,0xF0,
    0x0,0x40,0x40,0x0,0x0,0x0,0xF,0xFF,0xF0,0x0,0x8F,0x20,0x0,0x0,0x0,0xF,
    0xFF,0xF0,0x0,0x90,0xA0,0x0,0x0,0x0,0xF,0xFF,0xE0,0x1,0x3F,0xD0,0x0,0x0,
    0x0,0xF,0xFF,0xC0,0x1,0x3F,0xD0,0x0,0x0,0x0,0x2,0x7F,0x80,0x1,0x65,0x50,
    0x0,0x0,0x0,0xF,0xFE,0x0,0x3,0xE0,0x50,0x0,0x0,0x0,0xF,0xFC,0x0,0x5,
    0x66,0x90,0x0,0x0,0x0,0xF,0xFE,0x0,0x6,0xB0,0xA0,0x0,0x0,0x0,0xA,0x8F,
    0x80,0x5,0x5F,0x60,0x0,0x0,0x0,0x0,0x7,0xC0,0x3,0xAA,0xA0,0x0,0x0,0x0,
    0x0,0x3F,0xF0,0x4,0x75,0x60,0x0,0x1,0xF0,0x1,0xFF,0xF8,0x5,0xF,0xC0,0x10,
    0x1,0x1B,0xC1,0xFF,0xF8,0x8,0xC3,0xFF,0xF8,0x0,0xF,0xFC,0xFE,0xF8,0x8,0x37,
    0xFC,0xF8,0x0,0x7F,0xFC,0xFD,0xE8,0xA,0xF,0xFF,0xF0,0x0,0x7,0xBF,0x67,0xD8,
    0x11,0x80,0x3F,0xF0,0x2,0x1E,0x53,0xFF,0x78,0x10,0x78,0x14,0x80,0x3,0xF3,0xEF,
    0xFD,0xF0,0x20,0x7,0xF3,0x0,0x0,0x0,0xFF,0xF7,0xF0,
};

int artLandscapeTiles[12][6]=
{
    { 4, 4, 0xF0, 0xF0, 0xF0, 0xF0 },
    { 4, 4, 0xE0, 0xF0, 0xF0, 0xF0 },
    { 4, 4, 0x60, 0xF0, 0xF0, 0xF0 },
    { 4, 4, 0x70, 0xF0, 0xF0, 0xE0 },
    { 4, 4, 0x60, 0xF0, 0xF0, 0x70 },
    { 4, 4, 0x60, 0xF0, 0xF0, 0x60 },
    { 4, 4, 0x0, 0x0, 0x0, 0x0 },
    { 4, 4, 0x10, 0x0, 0x0, 0x0 },
    { 4, 4, 0x90, 0x0, 0x0, 0x0 },
    { 4, 4, 0x80, 0x0, 0x0, 0x10 },
    { 4, 4, 0x90, 0x0, 0x0, 0x80 },
    { 4, 4, 0x90, 0x0, 0x0, 0x90 },
};

int artUnitsBitmaps[4][6]=
{
    { 4, 4, 0x60, 0xF0, 0x60, 0x90 },
    { 4, 4, 0x90, 0xF0, 0x60, 0x90 },
    { 4, 4, 0xF0, 0xF0, 0x60, 0x90 },
    { 4, 4, 0x60, 0x60, 0x60, 0x90 },
};

int artLevels[26][38]=
{
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xC0,0x0,0x18,0xF0,0x0,0x38,0xF8,0x0,0x78,0xC1,0xFC,0x78,0xFB,0x6,0xF8,0xFC,0x1,0xF8,0xF8,0x0,0xF8,0xF8,0x0,0xF8,0xF8,0x0,0xF8},
    {21,12,0x0,0x0,0x0,0x30,0x0,0x0,0x44,0xD3,0x0,0x52,0xAA,0x0,0x76,0xA9,0x0,0x0,0x0,0x0,0x18,0x0,0x0,0x1D,0x56,0x30,0x15,0x55,0x50,0x1D,0x95,0x60,0x0,0x0,0x0,0xAA,0xAA,0xA8},
    {21,12,0x0,0x7F,0xF8,0x0,0x13,0xF8,0x0,0x1,0xF8,0x0,0x1,0x38,0x4,0x0,0x38,0x6C,0x0,0x18,0xFE,0x0,0x38,0xFF,0x80,0xB8,0xFF,0x1,0xF8,0xFE,0x1,0xF8,0xFF,0x0,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x0,0x1,0x3,0x0,0x1,0x96,0x0,0x0,0xBC,0x10,0x0,0xF8,0x10,0x80,0x70,0x10,0x80,0x78,0x38,0xC0,0x78,0x78,0xE4,0x78,0x78,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x45,0x0,0x81,0xFF,0x80,0xC7,0xFF,0xC0,0xE3,0xFF,0xE0,0xC1,0xFF,0xC0,0xF0,0x93,0x80,0xF8,0x0,0x0,0xFC,0x0,0x0,0xFE,0x0,0x0,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x99,0xF0,0x0,0x65,0xD8,0x0,0xF0,0x7E,0x0,0x48,0xCF,0x0,0x40,0xF,0x80,0x40,0x17,0xC0,0x50,0x3,0x60,0xE0,0x6,0x38,0xFF,0xFF,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x6,0xA,0x0,0xF,0x84,0x0,0x7,0x0,0xF0,0x0,0x0,0x10,0x20,0x0,0x70,0x70,0x0,0xF0,0xF8,0x1,0x90,0x20,0x2,0x90,0xFC,0x4,0x90,0xF8,0xFF,0xF8,0xF3,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x1,0xC0,0x0,0x3,0xE0,0x0,0x2,0xA0,0x2,0x3,0xE0,0x6,0x1,0xC0,0x45,0x80,0x80,0x47,0x83,0xE0,0x3D,0xC,0x80,0x3C,0xF1,0xC0,0x24,0x1,0x40,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x80,0x0,0x8,0xC0,0x0,0x98,0x80,0xDB,0xF8,0x81,0xFF,0xF8,0x80,0x7F,0xF8,0x80,0x2F,0xF8,0xC0,0x7,0xF8,0xF8,0xF,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x7,0xC0,0x38,0x7,0xE0,0x18,0x7,0x80,0x18,0x3,0x0,0x8,0x3,0x80,0x0,0x3,0x4,0x0,0x81,0x4,0x0,0xC0,0xC,0x0,0xE0,0xE,0x0,0xF0,0x1E,0x0,0xE0,0xFF,0x80,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x8,0x40,0x0,0x18,0xE0,0x0,0x78,0xE0,0x0,0x38,0xC0,0x0,0x38,0xE0,0x0,0x78,0xF1,0x9,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0xC0,0x60,0xE0,0x0,0x0,0x0,0xE,0x0,0x7,0x0,0x0,0x0,0x60,0xC0,0x30,0x0,0x30,0x0,0x7,0x0,0x1E,0x0,0x0,0x0,0x1C,0x18,0xE0,0x0,0x0},
    {21,12,0x0,0x0,0x0,0xFF,0x9F,0xF8,0xFF,0xCF,0xF8,0x80,0xE6,0x8,0xEF,0xF3,0xD8,0xC0,0x70,0x8,0xFD,0xE4,0xF8,0x80,0xCE,0x78,0xDF,0x9F,0x38,0xC0,0x3F,0xB8,0xFF,0xFF,0x88,0xFF,0xFF,0xB8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xF0,0x0,0x0,0xF8,0x0,0x0,0xF8,0x0,0x0,0xFC,0x0,0x18,0xFC,0x0,0x78,0xFC,0xF,0xF8,0xFE,0x1F,0xF8,0xFF,0xFF,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x70,0x1F,0x0,0x30,0x28,0x80,0x70,0x28,0x40,0x30,0x7F,0xF0,0x70,0xFF,0xF8,0x30,0xCF,0x38,0x10,0x4F,0x30,0x10,0x30,0xC0,0x10,0xFF,0xFF,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0xF,0xFF,0xF8,0x80,0x8,0x0,0xF0,0xFF,0x0,0xCF,0xE4,0x80,0x4F,0xE6,0x40,0x30,0xFF,0xE0,0x0,0x7F,0xC0,0x0,0x11,0x10,0x0,0x7F,0xE0,0x0,0x0,0x0},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0xC0,0x0,0x0,0xE0,0x0,0x7,0xFC,0x0,0x1,0xFE,0x0,0x2,0xAB,0xF0,0x1F,0xFF,0xE0,0x1F,0xFF,0xC0,0x1F,0xFF,0x80,0xAF,0xFF,0xA8,0x55,0x55,0x50},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x80,0x10,0x0,0xC0,0x18,0x0,0xC0,0x8,0x8,0xC0,0x1C,0x38,0xC2,0x1C,0x78,0xC6,0x1C,0x78,0xE6,0x1C,0x78,0xF6,0x1C,0x78,0xFF,0x3F,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x1,0xE0,0x38,0x0,0xF0,0x18,0x48,0x78,0x8,0xFC,0x3F,0x88,0xFC,0x3F,0xC0,0xF8,0x7F,0x80,0xF0,0x7F,0x0,0xF0,0xF6,0x0,0xD0,0x40,0x8,0x90,0x0,0x18,0x80,0x0,0x78,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x40,0x0,0x1,0xC0,0x90,0x1,0xE8,0xFC,0x3,0xE8,0xFE,0x7,0xF8,0xF0,0xF,0xF8,0xE0,0x7,0xF8,0xC0,0x3,0xF8,0xF8,0x7,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x20,0x0,0x0,0x20,0x0,0x0,0x20,0x0,0x0,0x70,0x0,0x0,0x50,0x0,0x44,0x51,0x10,0xEE,0xFB,0xB8,0xEE,0xDB,0xB8,0x45,0x8D,0x10,0xFF,0xFF,0xF8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x2,0x80,0x0,0xAB,0x80,0x0,0x71,0x0,0x0,0x71,0x0,0x0,0x73,0x0,0x0,0x26,0x0,0x0,0x7C,0x0,0x0,0xF8,0x0,0x0,0xF0,0x0,0x0,0x70,0x0,0x0,0xF0,0x0,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xA8,0x0,0x0,0x50,0x0,0x1F,0xFF,0xC0,0x1F,0xFF,0xC0,0xA,0x8A,0x80,0xFA,0x8A,0xF8,0xAA,0x8A,0xA8,0xAA,0x8A,0xA8,0xAA,0x8A,0xA8,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x0,0x0,0x0,0x18,0x8,0x20,0x3C,0x4,0x40,0x7E,0xF,0xE0,0xDB,0x1B,0xB0,0xFF,0x3F,0xF8,0x24,0x2F,0xE8,0x5A,0x28,0x28,0xA5,0x6,0xC0,0x0,0x0,0x0,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x0,0x1F,0x0,0x0,0x3F,0x83,0xC0,0x7B,0x7,0xE0,0x7E,0xF,0xF0,0x7C,0xC,0x90,0x7E,0xD,0xB0,0x7F,0xF,0xF0,0x3F,0x8F,0xF0,0x1F,0xA,0x50,0x0,0x0,0x0,0xFF,0xFF,0xF8},
    {21,12,0x0,0x0,0x38,0x0,0x0,0x10,0x40,0x11,0x38,0x30,0x4,0x0,0x7A,0xAA,0x0,0x30,0x4,0x0,0x40,0x11,0x70,0x0,0x0,0x20,0x0,0x8,0x70,0x80,0x1C,0x0,0xC0,0x3E,0x8,0xFF,0xFF,0x78},
};

int artOptionsBitmap[32]= {
    24,10,
    0x30,0x90,0x0,0x78,0xF0,0x0,0x30,0x60,0xC0,0x48,0x90,0x20,0x0,0x0,0x40,0x0,
    0x0,0x0,0x78,0x60,0x40,0x78,0x60,0x0,0x30,0x60,0x0,0x48,0x90,0x0,
};

// -----------------------------------------------------------------------------
// Real upstream trajectory table (sounds.ino has soundfx[][]; this one lives
// in images.ino) and sound effect table (sounds.ino) - already plain
// decimal upstream, copied verbatim.
// -----------------------------------------------------------------------------

int artTrajParamX[9]= { 0, 10, 20, 30, 30, 30, 20, 10, 0 };
int artTrajParamY[9]= { -30, -30, -20, -10, 0, 10, 20, 30, 30 };

int artSoundFx[6][8]= {
    { 1, 17, 53, 0, 7, 0, 2, 3 },
    { 1, 17, 53, 0, 7, 0, 10, 3 },
    { 1, 26, 41, 1, 1, 3, 7, 20 },
    { 0, 0, 42, 1, 1, 2, 7, 20 },
    { 0, 54, 0, 0, 0, 0, 7, 1 },
    { 0, 0, 65, 1, 1, 1, 7, 5 },
};

// -----------------------------------------------------------------------------
// State machine / structs / globals
// -----------------------------------------------------------------------------

enum ArtState
{
    ART_TITLE       = 0,
    ART_SELECT_MAP  = 1,
    ART_NEW_LEVEL   = 2,
    ART_PAUSE       = 3,
    ART_RUNNING     = 4,
    ART_ANIMFIRE    = 5,
    ART_BOOM        = 6,
    ART_GAMEOVER    = 7,
    ART_OPTIONS     = 8,
    ART_WAIT        = 9,
    ART_SELECT_UNIT = 10,
    ART_DAMAGE      = 11
};

#define ART_ROT_NONE 0
#define ART_ROT_CCW  1
#define ART_ROT_180  2
#define ART_ROT_CW   3

typedef struct
{
    int x;
    int y;
    int dir;    // 0/1 - used arithmetically upstream (`(dir*2)-1`), not a plain flag
    int dead;   // 0/1
    int team;
    int fall;
    int isIA;   // 0/1
    int life;
    int timer;
} ArtPlayer;

typedef struct
{
    int nbAlive;
    int lastPlayer;
} ArtTeam;

typedef struct
{
    int x;
    int y;
    int xTraj;
    int yTraj;
} ArtRocket;

typedef struct
{
    int angle;
    int power;
    int targetLocked;
} ArtIa;

ArtPlayer artPlayers[16];
ArtTeam artTeams[4];
ArtRocket artRocket;
ArtIa artIa;

int artGameStatus;
int artTitleReturnState;

int artNbAvailableLevel;
int artGameLevel;
int artScreen;
int artLandscape[21][12];
int artSetting;
int artConstTimer;
int artRandomVal;
int artOutCountr3;

int artCurrentTeam;
int artCurrentPlayer;
int artJumpStatus;
int artNbPlayer;
int artNbTeam;
int artNbCpuTeam;
int artUnitLife;

int artPower;
int artAngle;
int artTimer;
int artGravity;

// Scratch fields for artUnzip() - a direct port of real upstream's own
// `PxChecks` struct fields (used purely as temporaries by whichever
// function just filled them in, `artBuildLandscape()` or
// `artRebuildMap()`), ported as plain globals rather than a one-instance
// struct since nothing here needs struct semantics.
int artL0, artL1, artL2, artL3, artL4, artL5, artL6, artL7, artL8;

// -----------------------------------------------------------------------------
// Bounds-safe landscape accessors - see this file's own header comment
// ("Deliberate safety adaptations") for why these exist instead of a raw
// `artLandscape[x][y]` access at every call site that derives an index from
// a live rocket position.
// -----------------------------------------------------------------------------

int artLandscapeGetOr( int x, int y, int fallback )
{
    if( x < 0 || x > 20 || y < 0 || y > 11 ) return fallback;
    return artLandscape[ x ][ y ];
}

void artLandscapeSet( int x, int y, int v )
{
    if( x < 0 || x > 20 || y < 0 || y > 11 ) return;
    artLandscape[ x ][ y ] = v;
}

// -----------------------------------------------------------------------------
// Sound (see this file's own header comment - "SOUND")
// -----------------------------------------------------------------------------

// Direct port of real upstream's own `void outpt_soundfx(byte fxno)` -
// always channel 0, matching every one of that function's own real
// gb.sound.command()/playNote() calls exactly.
void artPlaySoundFx( int fxno )
{
    gbSoundCommand( GB_CMD_VOLUME, artSoundFx[ fxno ][ 6 ], 0, 0 );
    gbSoundCommand( GB_CMD_INSTRUMENT, artSoundFx[ fxno ][ 0 ], 0, 0 );
    gbSoundCommand( GB_CMD_SLIDE, artSoundFx[ fxno ][ 5 ], -artSoundFx[ fxno ][ 4 ], 0 );
    gbSoundCommand( GB_CMD_ARPEGGIO, artSoundFx[ fxno ][ 3 ], artSoundFx[ fxno ][ 2 ] - 58, 0 );
    gbPlayNoteChannel( artSoundFx[ fxno ][ 1 ], artSoundFx[ fxno ][ 7 ], 0 );
}

// -----------------------------------------------------------------------------
// Landscape build/rebuild (function.ino's own LANDSCAPE RELATED FUNCTIONS)
// -----------------------------------------------------------------------------

// Direct port of real `fnctn_unzip()` - classifies one landscape cell from
// artL0..artL8 (the 3x3 neighbourhood already filled in by whichever caller
// needs it) into a real tile index (`/10`) + rotation (`%10`) pair, exactly
// matching real upstream's own two-digit encoding.
void artUnzip( int x, int y )
{
    if( artL0 < 60 )
    {
        artLandscape[ x ][ y ] = 0;

        if( artL2 >= 60 && artL3 >= 60 && artL5 >= 60 ) artLandscape[ x ][ y ] = 10;
        if( artL4 >= 60 && artL6 >= 60 && artL7 >= 60 ) artLandscape[ x ][ y ] = 11;
        if( artL5 >= 60 && artL7 >= 60 && artL8 >= 60 ) artLandscape[ x ][ y ] = 12;
        if( artL1 >= 60 && artL2 >= 60 && artL4 >= 60 ) artLandscape[ x ][ y ] = 13;

        if( artL1 >= 60 && artL2 >= 60 && artL3 >= 60 && artL4 >= 60 && artL5 >= 60 ) artLandscape[ x ][ y ] = 20;
        if( artL4 >= 60 && artL5 >= 60 && artL6 >= 60 && artL7 >= 60 && artL8 >= 60 ) artLandscape[ x ][ y ] = 21;
        if( artL2 >= 60 && artL3 >= 60 && artL5 >= 60 && artL7 >= 60 && artL8 >= 60 ) artLandscape[ x ][ y ] = 22;
        if( artL1 >= 60 && artL2 >= 60 && artL4 >= 60 && artL6 >= 60 && artL7 >= 60 ) artLandscape[ x ][ y ] = 23;

        if( artL1 >= 60 && artL2 >= 60 && artL3 >= 60 && artL4 >= 60 && artL5 >= 60 && artL6 >= 60 && artL7 >= 60 ) artLandscape[ x ][ y ] = 40;
        if( artL2 >= 60 && artL3 >= 60 && artL4 >= 60 && artL5 >= 60 && artL6 >= 60 && artL7 >= 60 && artL8 >= 60 ) artLandscape[ x ][ y ] = 41;
        if( artL1 >= 60 && artL2 >= 60 && artL3 >= 60 && artL4 >= 60 && artL5 >= 60 && artL7 >= 60 && artL8 >= 60 ) artLandscape[ x ][ y ] = 42;
        if( artL1 >= 60 && artL2 >= 60 && artL4 >= 60 && artL5 >= 60 && artL6 >= 60 && artL7 >= 60 && artL8 >= 60 ) artLandscape[ x ][ y ] = 43;

        if( artL1 >= 60 && artL2 >= 60 && artL3 >= 60 && artL4 >= 60 && artL5 >= 60 && artL6 >= 60 && artL7 >= 60 && artL8 >= 60 ) artLandscape[ x ][ y ] = 50;
    }
    else
    {
        artLandscape[ x ][ y ] = 60;

        if( artL2 < 60 && artL5 < 60 ) artLandscape[ x ][ y ] = 70;
        if( artL4 < 60 && artL7 < 60 ) artLandscape[ x ][ y ] = 71;
        if( artL5 < 60 && artL7 < 60 ) artLandscape[ x ][ y ] = 72;
        if( artL2 < 60 && artL4 < 60 ) artLandscape[ x ][ y ] = 73;

        if( artL2 < 60 && artL4 < 60 && artL5 < 60 ) artLandscape[ x ][ y ] = 80;
        if( artL4 < 60 && artL5 < 60 && artL7 < 60 ) artLandscape[ x ][ y ] = 81;
        if( artL2 < 60 && artL5 < 60 && artL7 < 60 ) artLandscape[ x ][ y ] = 82;
        if( artL2 < 60 && artL4 < 60 && artL7 < 60 ) artLandscape[ x ][ y ] = 83;

        if( artL2 < 60 && artL4 < 60 && artL5 < 60 && artL7 < 60 ) artLandscape[ x ][ y ] = 110;
    }
}

// Direct port of real `fnctn_buildLandscape()` - reads the real level
// bitmap that `artNewLevel()` has just drawn onto the framebuffer at (1,1)
// (this shim's own genuine CPU-writable framebuffer, matching real
// hardware's own `Display::getPixel()`/`drawPixel()`) and classifies every
// one of the 21x12 landscape cells from those real pixels.
void artBuildLandscape()
{
    int cx, cy;
    for( cy = 1; cy < 13; cy = cy + 1 )
    {
        for( cx = 1; cx < 22; cx = cx + 1 )
        {
            if( gbGetPixel( cx,     cy     ) ) artL0 = 0; else artL0 = 60;
            if( gbGetPixel( cx - 1, cy - 1 ) ) artL1 = 0; else artL1 = 60;
            if( gbGetPixel( cx,     cy - 1 ) ) artL2 = 0; else artL2 = 60;
            if( gbGetPixel( cx + 1, cy - 1 ) ) artL3 = 0; else artL3 = 60;
            if( gbGetPixel( cx - 1, cy     ) ) artL4 = 0; else artL4 = 60;
            if( gbGetPixel( cx + 1, cy     ) ) artL5 = 0; else artL5 = 60;
            if( gbGetPixel( cx - 1, cy + 1 ) ) artL6 = 0; else artL6 = 60;
            if( gbGetPixel( cx,     cy + 1 ) ) artL7 = 0; else artL7 = 60;
            if( gbGetPixel( cx + 1, cy + 1 ) ) artL8 = 0; else artL8 = 60;
            artUnzip( cx - 1, cy - 1 );
        }
    }
}

// Direct port of real `fnctn_rebuildMap()` - carves the single landscape
// cell nearest the rocket's real impact point (upstream's own real
// direction-dependent quadrant choice, preserved exactly), then re-runs the
// full 21x12 classification pass using the already-classified
// `artLandscape[][]` values as input this time (not raw framebuffer
// pixels) - matching real upstream's own two different `pixelCheck` fill
// strategies for the initial build vs a rebuild. Out-of-range neighbours in
// THIS pass are real upstream `0` (treated as solid - the map border acts
// walled-off), not 60 - see `artLandscapeGetOr()`'s own call sites below.
void artRebuildMap()
{
    int cx, cy;

    if( artRocket.xTraj >= 0 )
    {
        if( artRocket.yTraj >= 0 )
        {
            if( artLandscapeGetOr( artRocket.x / 4 - 1, artRocket.y / 4, 60 ) < 60 && artLandscapeGetOr( artRocket.x / 4, artRocket.y / 4 - 1, 60 ) < 60 )
            {
                if( gbAbsInt( artRocket.xTraj ) > gbAbsInt( artRocket.yTraj ) / 2 ) artLandscapeSet( artRocket.x / 4 - 1, artRocket.y / 4, 60 );
                else artLandscapeSet( artRocket.x / 4, artRocket.y / 4 - 1, 60 );
            }
            else artLandscapeSet( artRocket.x / 4, artRocket.y / 4, 60 );
        }
        else
        {
            if( artLandscapeGetOr( artRocket.x / 4 - 1, artRocket.y / 4, 60 ) < 60 && artLandscapeGetOr( artRocket.x / 4, artRocket.y / 4 + 1, 60 ) < 60 )
            {
                if( gbAbsInt( artRocket.xTraj ) > gbAbsInt( artRocket.yTraj ) / 2 ) artLandscapeSet( artRocket.x / 4 - 1, artRocket.y / 4, 60 );
                else artLandscapeSet( artRocket.x / 4, artRocket.y / 4 + 1, 60 );
            }
            else artLandscapeSet( artRocket.x / 4, artRocket.y / 4, 60 );
        }
    }
    else
    {
        if( artRocket.yTraj >= 0 )
        {
            if( artLandscapeGetOr( artRocket.x / 4 + 1, artRocket.y / 4, 60 ) < 60 && artLandscapeGetOr( artRocket.x / 4, artRocket.y / 4 - 1, 60 ) < 60 )
            {
                if( gbAbsInt( artRocket.xTraj ) > gbAbsInt( artRocket.yTraj ) / 2 ) artLandscapeSet( artRocket.x / 4 + 1, artRocket.y / 4, 60 );
                else artLandscapeSet( artRocket.x / 4, artRocket.y / 4 - 1, 60 );
            }
            else artLandscapeSet( artRocket.x / 4, artRocket.y / 4, 60 );
        }
        else
        {
            if( artLandscapeGetOr( artRocket.x / 4 + 1, artRocket.y / 4, 60 ) < 60 && artLandscapeGetOr( artRocket.x / 4, artRocket.y / 4 + 1, 60 ) < 60 )
            {
                if( gbAbsInt( artRocket.xTraj ) > gbAbsInt( artRocket.yTraj ) / 2 ) artLandscapeSet( artRocket.x / 4 + 1, artRocket.y / 4, 60 );
                else artLandscapeSet( artRocket.x / 4, artRocket.y / 4 + 1, 60 );
            }
            else artLandscapeSet( artRocket.x / 4, artRocket.y / 4, 60 );
        }
    }

    for( cy = 0; cy < 12; cy = cy + 1 )
    {
        for( cx = 0; cx < 21; cx = cx + 1 )
        {
            artL0 = artLandscape[ cx ][ cy ];
            artL1 = artLandscapeGetOr( cx - 1, cy - 1, 0 );
            artL2 = artLandscapeGetOr( cx,     cy - 1, 0 );
            artL3 = artLandscapeGetOr( cx + 1, cy - 1, 0 );
            artL4 = artLandscapeGetOr( cx - 1, cy,     0 );
            artL5 = artLandscapeGetOr( cx + 1, cy,     0 );
            artL6 = artLandscapeGetOr( cx - 1, cy + 1, 0 );
            artL7 = artLandscapeGetOr( cx,     cy + 1, 0 );
            artL8 = artLandscapeGetOr( cx + 1, cy + 1, 0 );
            artUnzip( cx, cy );
        }
    }
}

// Direct port of real `fnctn_definePlayer()` - a pseudo-shuffle (upstream's
// own real, non-Fisher-Yates permutation formula, preserved exactly) picks
// a random landscape column per unit, then scans up/down from the map's
// own top/bottom edge (alternating by parity) for the first real
// sky-then-ground pixel transition, poking a single corrective pixel
// directly into the framebuffer if no valid surface is found - all via
// this shim's own genuine `gbGetPixel()`/`gbDrawPixel()`, matching real
// upstream's own `getPixel()`/`drawPixel()` exactly.
void artDefinePlayer()
{
    int i, tmp, tmp2, check;
    int randm[21];

    for( i = 0; i < 21; i = i + 1 ) randm[ i ] = ( i * 4 ) % 21;

    tmp = arand( 21 );
    for( i = 0; i < 21 - tmp; i = i + 1 )
    {
        tmp2 = randm[ i ];
        randm[ i ] = randm[ i + tmp ];
        randm[ i + tmp ] = tmp2;
    }

    for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
    {
        artPlayers[ i ].dead = 0;
        artPlayers[ i ].life = artUnitLife;
        artPlayers[ i ].team = i / artNbPlayer;
        artPlayers[ i ].fall = 0;
        artPlayers[ i ].isIA = 0;
        if( artPlayers[ i ].team >= artNbTeam - artNbCpuTeam ) artPlayers[ i ].isIA = 1;
        artPlayers[ i ].timer = 0;

        artPlayers[ i ].x = randm[ i ] * 4;

        if( randm[ i ] % 2 == 0 )
        {
            artPlayers[ i ].y = 0;
            check = 0;
            while( check == 0 && artPlayers[ i ].y < 11 )
            {
                if( gbGetPixel( randm[ i ] + 1, artPlayers[ i ].y + 1 ) == 0 && gbGetPixel( randm[ i ] + 1, artPlayers[ i ].y + 1 + 1 ) == 1 )
                {
                    artPlayers[ i ].y = artPlayers[ i ].y * 4;
                    check = 1;
                }
                else artPlayers[ i ].y = artPlayers[ i ].y + 1;
            }
            if( check == 0 )
            {
                artPlayers[ i ].y = ( randm[ i ] % 11 ) * 4;
                if( gbGetPixel( randm[ i ] + 1, ( randm[ i ] % 11 ) + 1 ) == 0 )
                {
                    gbSetColor( GB_BLACK );
                    gbDrawPixel( randm[ i ] + 1, ( ( randm[ i ] % 11 ) + 1 ) + 1 );
                }
                else
                {
                    gbSetColor( GB_WHITE );
                    gbDrawPixel( randm[ i ] + 1, ( randm[ i ] % 11 ) + 1 );
                    gbSetColor( GB_BLACK );
                }
            }
        }
        else
        {
            artPlayers[ i ].y = 10;
            check = 0;
            while( check == 0 && artPlayers[ i ].y > 0 )
            {
                if( gbGetPixel( randm[ i ] + 1, artPlayers[ i ].y + 1 ) == 0 && gbGetPixel( randm[ i ] + 1, artPlayers[ i ].y + 1 + 1 ) == 1 )
                {
                    artPlayers[ i ].y = artPlayers[ i ].y * 4;
                    check = 1;
                }
                else artPlayers[ i ].y = artPlayers[ i ].y - 1;
            }
            if( check == 0 )
            {
                artPlayers[ i ].y = ( randm[ i ] % 11 ) * 4;
                if( gbGetPixel( randm[ i ] + 1, ( randm[ i ] % 11 ) + 1 ) == 0 )
                {
                    gbSetColor( GB_BLACK );
                    gbDrawPixel( randm[ i ] + 1, ( ( randm[ i ] % 11 ) + 1 ) + 1 );
                }
                else
                {
                    gbSetColor( GB_WHITE );
                    gbDrawPixel( randm[ i ] + 1, ( randm[ i ] % 11 ) + 1 );
                    gbSetColor( GB_BLACK );
                }
            }
        }

        if( artPlayers[ i ].x > 40 ) artPlayers[ i ].dir = 0;
        else artPlayers[ i ].dir = 1;
    }
}

// Direct port of real `fnctn_newlevel()`.
void artNewLevel()
{
    gbSetColor( GB_WHITE );
    gbFillRect( 0, 0, 21, 12 );
    gbSetColor( GB_BLACK );
    gbDrawRect( 0, 0, 23, 14 );
    gbDrawBitmap( 1, 1, artLevels[ artScreen ] );

    artDefinePlayer();
    artBuildLandscape();

    gbSetColor( GB_WHITE );
    gbFillRect( 0, 0, 23, 14 );
    gbSetColor( GB_BLACK );
}

// -----------------------------------------------------------------------------
// Player-related functions (function.ino's own PLAYER RELATED FUNCTIONS)
// -----------------------------------------------------------------------------

void artCheckJump()
{
    if( artJumpStatus == 6 ) artPlayers[ artCurrentPlayer ].y = artPlayers[ artCurrentPlayer ].y - 3;
    else if( artJumpStatus == 5 ) artPlayers[ artCurrentPlayer ].y = artPlayers[ artCurrentPlayer ].y - 2;
    else if( artJumpStatus == 4 ) artPlayers[ artCurrentPlayer ].y = artPlayers[ artCurrentPlayer ].y - 1;

    if( artJumpStatus > 0 ) artJumpStatus = artJumpStatus - 1;
}

// Direct port of real `fnctn_checkPlayerPos()` - see this file's own header
// comment ("Preserved real upstream bugs", #1) for the real operator-
// precedence quirk in the final "get unstuck" test, preserved exactly.
void artCheckPlayerPos()
{
    int i, k;
    for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
    {
        if( gbGetPixel( artPlayers[ i ].x, artPlayers[ i ].y + 4 ) == 0 && gbGetPixel( artPlayers[ i ].x + 3, artPlayers[ i ].y + 4 ) == 0 )
        {
            for( k = 0; k < artPlayers[ i ].fall + 1; k = k + 1 )
            {
                if( gbGetPixel( artPlayers[ i ].x, artPlayers[ i ].y + 4 ) == 0 && gbGetPixel( artPlayers[ i ].x + 3, artPlayers[ i ].y + 4 ) == 0 )
                {
                    if( artPlayers[ i ].y > 48 && artPlayers[ i ].y < 200 )
                    {
                        if( artPlayers[ i ].dead == 0 )
                        {
                            artTeams[ artPlayers[ i ].team ].nbAlive = artTeams[ artPlayers[ i ].team ].nbAlive - 1;
                            artPower = 0;
                            artPlayers[ i ].life = 0;
                            artPlayers[ i ].fall = 0;
                            artPlayers[ i ].dead = 1;
                        }
                    }
                    else artPlayers[ i ].y = artPlayers[ i ].y + 1;
                }
            }
            if( artPlayers[ i ].fall < 4 && artPlayers[ i ].dead == 0 ) artPlayers[ i ].fall = artPlayers[ i ].fall + 1;
        }
        else artPlayers[ i ].fall = 0;

        // Real precedence: `A || (B && C)`, NOT `(A||B) && C` - see this
        // file's own header comment.
        if( gbGetPixel( artPlayers[ i ].x + 2, artPlayers[ i ].y + 3 ) == 1
            || ( gbGetPixel( artPlayers[ i ].x + 1, artPlayers[ i ].y + 3 ) == 1 && artPlayers[ i ].dead == 0 ) )
        {
            artPlayers[ i ].y = artPlayers[ i ].y - 1;
            artPlayers[ i ].fall = 0;
        }
    }
}

// Forward declaration - artNextPlayer() (below) calls artGameOver(), which
// is itself defined later in this file (function.ino's own real
// PLAYER RELATED FUNCTIONS order: `fnctn_nextPlayer()` precedes
// `fnctn_gameOver()` too, and calls it the same way).
void artGameOver();

void artNextPlayer()
{
    do
    {
        artCurrentTeam = ( artCurrentTeam + 1 ) % artNbTeam;
    } while( artTeams[ artCurrentTeam ].nbAlive == 0 );

    do
    {
        artCurrentPlayer = ( artCurrentTeam * artNbPlayer ) + ( artTeams[ artCurrentTeam ].lastPlayer + 1 ) % artNbPlayer;
        artTeams[ artCurrentTeam ].lastPlayer = ( artTeams[ artCurrentTeam ].lastPlayer + 1 ) % artNbPlayer;
    } while( artPlayers[ artCurrentPlayer ].dead == 1 );

    artPlayers[ artCurrentPlayer ].timer = artConstTimer;
    artPower = 0;
    artAngle = 4;
    artIa.angle = 2;
    artIa.power = 10;
    artIa.targetLocked = 0;
    artGameStatus = ART_SELECT_UNIT;
    artGameOver();
}

void artDrawSelectUnitMarkers()
{
    int i;
    for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
    {
        if( artPlayers[ i ].timer > 0 )
        {
            artOutCountr3 = 1;
            gbDrawFastVLine( artPlayers[ i ].x + 1, artPlayers[ i ].y - 6 - ( artPlayers[ i ].timer % 4 ), 2 );
            gbDrawPixel( artPlayers[ i ].x,     artPlayers[ i ].y - 3 - ( artPlayers[ i ].timer % 4 ) );
            gbDrawPixel( artPlayers[ i ].x + 1, artPlayers[ i ].y - 2 - ( artPlayers[ i ].timer % 4 ) );
            gbDrawPixel( artPlayers[ i ].x + 2, artPlayers[ i ].y - 3 - ( artPlayers[ i ].timer % 4 ) );
            artPlayers[ i ].timer = artPlayers[ i ].timer - 1;
        }
    }
}

void artSelectUnit()
{
    artOutCountr3 = 0;
    artDrawSelectUnitMarkers();
    if( artOutCountr3 == 0 ) artGameStatus = ART_RUNNING;
}

void artCheckDead()
{
    int i;
    for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
    {
        if( artPlayers[ i ].dead == 0 )
        {
            if( artPlayers[ i ].x <= artRocket.x && artPlayers[ i ].x + 4 >= artRocket.x && artPlayers[ i ].y <= artRocket.y && artPlayers[ i ].y + 4 >= artRocket.y )
            {
                artPlayers[ i ].life = artPlayers[ i ].life - 1;
                if( artPlayers[ i ].life <= 0 )
                {
                    artPower = 0;
                    artPlayers[ i ].dead = 1;
                    artTeams[ artPlayers[ i ].team ].nbAlive = artTeams[ artPlayers[ i ].team ].nbAlive - 1;
                }
                else artPlayers[ i ].timer = artConstTimer;
            }
        }
    }
}

void artGameOver()
{
    int i, aliveTeams;
    aliveTeams = 0;
    for( i = 0; i < artNbTeam; i = i + 1 )
    {
        if( artTeams[ i ].nbAlive > 0 ) aliveTeams = aliveTeams + 1;
    }
    if( aliveTeams <= 1 ) artGameStatus = ART_GAMEOVER;
}

// -----------------------------------------------------------------------------
// Bullet-related functions (function.ino's own BULLET RELATED FUNCTIONS)
// -----------------------------------------------------------------------------

void artNextProjPosition()
{
    artRocket.x = artRocket.x + artRocket.xTraj / 3;
    artRocket.y = artRocket.y + artRocket.yTraj / 3;
    artRocket.xTraj = artRocket.xTraj + 0; // real upstream "wind" term - always literally 0
    artRocket.yTraj = artRocket.yTraj + artGravity;
    if( gbAbsInt( artRocket.xTraj ) > 12 ) artRocket.xTraj = 12 * artRocket.xTraj / gbAbsInt( artRocket.xTraj );
    if( gbAbsInt( artRocket.yTraj ) > 12 ) artRocket.yTraj = 12 * artRocket.yTraj / gbAbsInt( artRocket.yTraj );
}

// Direct port of real `fn_checkCollision()` - the off-screen dead-zone test
// is real upstream's own; the landscape lookup goes through
// `artLandscapeGetOr()` instead of a raw array read (see this file's own
// header comment - a shot landing exactly on the real x==84/y==48 edge
// falls back to "no ground here" instead of an out-of-bounds read, an
// unnoticeable one-pixel-wide edge case on an 84px-wide screen).
void artCheckCollision()
{
    int i;
    if( artRocket.x > 84 || artRocket.x < 0 || ( artRocket.y > 48 && artRocket.y < 100 ) )
    {
        artNextPlayer();
    }
    else
    {
        if( artLandscapeGetOr( artRocket.x / 4, artRocket.y / 4, 60 ) < 60 && artRocket.y / 4 >= 0 ) artGameStatus = ART_BOOM;
        for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
        {
            if( artPlayers[ i ].x <= artRocket.x && artPlayers[ i ].x + 4 >= artRocket.x && artPlayers[ i ].y <= artRocket.y && artPlayers[ i ].y + 4 >= artRocket.y ) artGameStatus = ART_BOOM;
        }
    }
}

// Direct port of real `fnctn_ia()` - see this file's own header comment for
// two real preserved upstream quirks (the angle search skipping index 0,
// and the trajParamY-instead-of-trajParamX mixup in the horizontal offset)
// and one required addition (the `artRocket.y < 0` loop-termination guard).
void artAiThink()
{
    int dirLoop, angleLoop, powerLoop, target, checkFlag;

    if( artTimer > 0 )
    {
        artTimer = artTimer - 1;
    }
    else
    {
        if( artIa.targetLocked == 0 )
        {
            for( dirLoop = 0; dirLoop < 2; dirLoop = dirLoop + 1 )
            {
                for( angleLoop = 8; angleLoop > 0; angleLoop = angleLoop - 1 )
                {
                    for( powerLoop = 0; powerLoop < 10; powerLoop = powerLoop + 1 )
                    {
                        checkFlag = 0;
                        artRocket.x = artPlayers[ artCurrentPlayer ].x + ( 1 + dirLoop ) + ( ( artTrajParamY[ angleLoop ] / 10 ) * ( ( dirLoop * 2 ) - 1 ) );
                        artRocket.y = artPlayers[ artCurrentPlayer ].y + ( artTrajParamY[ angleLoop ] / 10 ) + 1;
                        artRocket.xTraj = artTrajParamX[ angleLoop ] / 10 * ( powerLoop / 2 ) * ( ( dirLoop * 2 ) - 1 );
                        artRocket.yTraj = artTrajParamY[ angleLoop ] / 10 * ( powerLoop / 2 );

                        while( checkFlag == 0 )
                        {
                            for( target = 0; target < artNbPlayer * artNbTeam; target = target + 1 )
                            {
                                if( artPlayers[ target ].x - 4 <= artRocket.x
                                    && artPlayers[ target ].x + 8 >= artRocket.x
                                    && artPlayers[ target ].y - 4 <= artRocket.y
                                    && artPlayers[ target ].y + 8 >= artRocket.y
                                    && artPlayers[ target ].dead == 0
                                    && artPlayers[ target ].team != artPlayers[ artCurrentPlayer ].team )
                                {
                                    if( artIa.targetLocked == 0 )
                                    {
                                        artPlayers[ artCurrentPlayer ].dir = dirLoop;
                                        artIa.angle = angleLoop;
                                        artIa.power = powerLoop;
                                        if( artPlayers[ target ].x - 2 <= artRocket.x
                                            && artPlayers[ target ].x + 6 >= artRocket.x
                                            && artPlayers[ target ].y - 2 <= artRocket.y
                                            && artPlayers[ target ].y + 6 >= artRocket.y
                                            && artPlayers[ target ].dead == 0
                                            && artPlayers[ target ].team != artPlayers[ artCurrentPlayer ].team )
                                        {
                                            artIa.targetLocked = 1;
                                            artTimer = 20;
                                        }
                                    }
                                    checkFlag = 1;
                                }
                                if( artRocket.x > 84 ) checkFlag = 1;
                                if( artRocket.x < 0 ) checkFlag = 1;
                                if( artRocket.y > 48 ) checkFlag = 1;
                                if( artRocket.y < 0 ) checkFlag = 1; // required addition - see header comment
                                if( artLandscapeGetOr( artRocket.x / 4, artRocket.y / 4, 60 ) < 60 ) checkFlag = 1;
                                artNextProjPosition();
                            }
                        }
                    }
                }
            }
            if( artIa.targetLocked == 0 )
            {
                artIa.targetLocked = 1;
                artTimer = 20;
            }
        }
        else
        {
            if( artAngle != artIa.angle )
            {
                if( artAngle > artIa.angle ) artAngle = artAngle - 1;
                else
                {
                    artAngle = artAngle + 1;
                    if( artAngle == artIa.angle ) artTimer = 20;
                }
            }
            else
            {
                if( artPower < artIa.power ) artPower = artPower + 1;
                else
                {
                    artRocket.x = artPlayers[ artCurrentPlayer ].x + ( 1 + artPlayers[ artCurrentPlayer ].dir ) + ( ( artTrajParamX[ artAngle ] / 10 ) * ( ( artPlayers[ artCurrentPlayer ].dir * 2 ) - 1 ) );
                    artRocket.y = artPlayers[ artCurrentPlayer ].y + ( artTrajParamY[ artAngle ] / 10 ) + 1;
                    artRocket.xTraj = artTrajParamX[ artAngle ] / 10 * ( artPower / 2 ) * ( ( artPlayers[ artCurrentPlayer ].dir * 2 ) - 1 );
                    artRocket.yTraj = artTrajParamY[ artAngle ] / 10 * ( artPower / 2 );
                    artGameStatus = ART_ANIMFIRE;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Drawing (output.ino)
// -----------------------------------------------------------------------------

void artDrawSelectMap()
{
    int i, x, y, z;

    if( ( ( artGameLevel / 3 ) * 17 ) + 20 > 48 )
    {
        z = ( ( artGameLevel - 3 ) / 3 ) * 17;
    }
    else
    {
        z = 0;
        gbDrawRect( 3, 7, 23, 14 );
        gbDrawBitmap( 4, 9, artOptionsBitmap );
    }

    for( i = 0; i < artNbAvailableLevel; i = i + 1 )
    {
        x = 4 + ( ( ( i + 1 ) % 3 ) * 27 );
        y = 8 + ( ( ( i + 1 ) / 3 ) * 17 ) - z;
        if( y < 100 )
        {
            gbDrawRect( x - 1, y - 1, 23, 14 );
            gbDrawBitmap( x, y, artLevels[ i ] );
            if( i + 1 == artGameLevel ) gbDrawRect( x - 2, y - 2, 25, 16 );
        }
    }

    gbSetColor( GB_WHITE );
    gbFillRect( 0, 0, 84, 6 );
    gbSetColor( GB_BLACK );
    gbCursorY = 0;
    if( artGameLevel == 0 )
    {
        gbCursorX = 27;
        gbPrintString( "Settings" );
        gbDrawRect( 2, 6, 25, 16 );
    }
    else
    {
        gbCursorX = 28;
        gbPrintString( "Level " );
        gbPrintNumber( artGameLevel );
    }
}

void artDrawOptions()
{
    gbCursorY = 0;
    gbCursorX = 28;
    gbPrintString( "Settings" );

    gbCursorY = 12;
    gbCursorX = 16;
    gbPrintString( "Player:  <" );
    gbPrintNumber( artNbTeam );
    gbPrintString( ">" );

    gbCursorY = 18;
    gbCursorX = 16;
    gbPrintString( "Units:   <" );
    gbPrintNumber( artNbPlayer );
    gbPrintString( ">" );

    gbCursorY = 24;
    gbCursorX = 16;
    gbPrintString( "Life:    <" );
    gbPrintNumber( artUnitLife );
    gbPrintString( ">" );

    gbCursorY = 30;
    gbCursorX = 16;
    gbPrintString( "NbCPU:   <" );
    gbPrintNumber( artNbCpuTeam );
    gbPrintString( ">" );

    gbCursorY = 36;
    gbCursorX = 16;
    gbPrintString( "Gravity: <" );
    gbPrintNumber( artGravity );
    gbPrintString( ">" );

    gbCursorY = 42;
    gbCursorX = 16;
    gbPrintString( "Back" );

    gbDrawBitmap( 11, 13 + ( artSetting * 6 ), artUnitsBitmaps[ 0 ] );
}

void artDrawLandscape()
{
    int cx, cy, v, rot;
    for( cy = 0; cy < 12; cy = cy + 1 )
    {
        for( cx = 0; cx < 21; cx = cx + 1 )
        {
            v = artLandscape[ cx ][ cy ];
            rot = v % 10;
            if( rot == 0 ) gbDrawBitmapRotated( cx * 4, cy * 4, artLandscapeTiles[ v / 10 ], ART_ROT_NONE, 0 );
            else if( rot == 1 ) gbDrawBitmapRotated( cx * 4, cy * 4, artLandscapeTiles[ v / 10 ], ART_ROT_180, 0 );
            else if( rot == 2 ) gbDrawBitmapRotated( cx * 4, cy * 4, artLandscapeTiles[ v / 10 ], ART_ROT_CW, 0 );
            else if( rot == 3 ) gbDrawBitmapRotated( cx * 4, cy * 4, artLandscapeTiles[ v / 10 ], ART_ROT_CCW, 0 );
        }
    }
}

void artDrawPlayers()
{
    int i;
    for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
    {
        if( artPlayers[ i ].dead == 0 )
        {
            gbDrawBitmap( artPlayers[ i ].x, artPlayers[ i ].y, artUnitsBitmaps[ artPlayers[ i ].team ] );
        }
        else
        {
            gbDrawFastVLine( artPlayers[ i ].x + 1, artPlayers[ i ].y, 4 );
            if( artPlayers[ i ].team == 0 )
            {
                gbDrawFastHLine( artPlayers[ i ].x, artPlayers[ i ].y + 1, 3 );
            }
            else if( artPlayers[ i ].team == 1 )
            {
                gbDrawFastHLine( artPlayers[ i ].x, artPlayers[ i ].y, 3 );
                gbDrawFastHLine( artPlayers[ i ].x, artPlayers[ i ].y + 2, 3 );
            }
            else if( artPlayers[ i ].team == 2 )
            {
                gbDrawPixel( artPlayers[ i ].x, artPlayers[ i ].y );
                gbDrawPixel( artPlayers[ i ].x + 2, artPlayers[ i ].y + 1 );
            }
            else if( artPlayers[ i ].team == 3 )
            {
                gbDrawPixel( artPlayers[ i ].x, artPlayers[ i ].y + 1 );
                gbDrawPixel( artPlayers[ i ].x + 2, artPlayers[ i ].y );
            }
        }
    }
}

void artDrawPower()
{
    int i;
    if( artPower > 0 )
    {
        gbSetColor( GB_WHITE );
        gbFillRect( 0, 0, 12, 4 );
        gbSetColor( GB_BLACK );
        gbDrawPixel( 0, 0 );
        gbDrawPixel( 0, 3 );
        gbDrawPixel( 11, 0 );
        gbDrawPixel( 11, 3 );

        for( i = 1; i < artPower + 1; i = i + 1 )
        {
            gbDrawPixel( i, 1 );
            gbDrawPixel( i, 2 );
        }
    }
}

void artDrawLife()
{
    int i;
    for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
    {
        if( artPlayers[ i ].dead == 0 && artPlayers[ i ].fall == 0 )
        {
            if( i == artCurrentPlayer && artJumpStatus != 0 )
            {
                // real upstream draws nothing for the current unit's own life bar while it's mid-jump
            }
            else
            {
                gbSetColor( GB_BLACK );
                if( artPlayers[ i ].life == 3 ) gbDrawFastHLine( artPlayers[ i ].x, artPlayers[ i ].y - 3, 4 );
                else if( artPlayers[ i ].life == 2 ) gbDrawFastHLine( artPlayers[ i ].x, artPlayers[ i ].y - 3, 2 );
            }
        }
    }
}

void artDrawTeam()
{
    if( artPower == 0 )
    {
        gbPrintString( "P" );
        gbPrintNumber( artCurrentTeam + 1 );
    }
}

// Direct port of real `outpt_cursor()` - the real `*1.5` scaling needs
// genuine float math (this dialect has native `float`, unlike real AVR
// integer-only upstream would have needed to fake it - real upstream just
// promotes to `float` here too since `trajParamX`/`trajParamY` are `char`
// and the literal `1.5` forces it).
void artDrawCursor()
{
    int dirSign;
    float baseX, baseY, tipX, tipY;

    dirSign = ( artPlayers[ artCurrentPlayer ].dir * 2 ) - 1;
    baseX = artPlayers[ artCurrentPlayer ].x + artPlayers[ artCurrentPlayer ].dir + 1;
    baseY = artPlayers[ artCurrentPlayer ].y + 1;
    tipX = baseX + ( artTrajParamX[ artAngle ] / 10 ) * dirSign * 1.5;
    tipY = baseY + ( artTrajParamY[ artAngle ] / 10 ) * 1.5;

    gbSetColor( GB_WHITE );
    gbDrawPixel( (int)baseX, (int)baseY );
    gbDrawPixel( (int)tipX, (int)tipY );

    gbSetColor( GB_BLACK );
    gbDrawLine( (int)tipX - 1, (int)tipY,     (int)tipX,     (int)tipY - 1 );
    gbDrawLine( (int)tipX + 1, (int)tipY,     (int)tipX,     (int)tipY + 1 );
}

void artDrawProjectile()
{
    gbDrawPixel( artRocket.x, artRocket.y );
    gbDrawPixel( artRocket.x + 1, artRocket.y );
    gbDrawPixel( artRocket.x, artRocket.y - 1 );
    gbDrawPixel( artRocket.x + 1, artRocket.y - 1 );
}

void artDrawDamage()
{
    int i;
    for( i = 0; i < artNbPlayer * artNbTeam; i = i + 1 )
    {
        if( artPlayers[ i ].timer % 3 == 2 )
        {
            gbSetColor( GB_WHITE );
            gbFillRect( artPlayers[ i ].x, artPlayers[ i ].y, 4, 4 );
            gbSetColor( GB_BLACK );
        }
        if( artPlayers[ i ].timer > 0 )
        {
            artPlayers[ i ].timer = artPlayers[ i ].timer - 1;
            artOutCountr3 = 1;
        }
    }
}

void artDrawPause()
{
    gbCursorY = 0;
    gbCursorX = 28;
    gbPrintString( "Pause" );
    gbCursorY = 15;
    gbCursorX = 16;
    gbPrintString( "Back to Game" );
    gbCursorY = 25;
    gbCursorX = 16;
    gbPrintString( "Quit to New Map" );

    gbDrawBitmap( 11, 16 + ( artSetting * 10 ), artUnitsBitmaps[ 0 ] );
}

// Direct port of real `outpt_boom()` - real upstream has no case for
// `artTimer == 4` either (a genuine one-tick gap in the explosion
// animation, not an omission here).
void artDrawBoom()
{
    if( artTimer == 0 )
    {
        gbSetColor( GB_INVERT );
        gbDrawPixel( artRocket.x - 1, artRocket.y - 1 );
    }
    else if( artTimer == 1 )
    {
        gbSetColor( GB_INVERT );
        gbDrawPixel( artRocket.x - 1, artRocket.y - 1 );
    }
    else if( artTimer == 2 )
    {
        gbSetColor( GB_WHITE );
        gbDrawPixel( artRocket.x - 1, artRocket.y - 1 );
        gbSetColor( GB_INVERT );
        gbDrawLine( artRocket.x - 2, artRocket.y - 1, artRocket.x - 1, artRocket.y - 2 );
        gbDrawLine( artRocket.x,     artRocket.y - 1, artRocket.x - 1, artRocket.y );
    }
    else if( artTimer == 3 )
    {
        gbSetColor( GB_WHITE );
        gbFillRect( artRocket.x - 1, artRocket.y - 1, 2, 2 );
        gbSetColor( GB_INVERT );
        gbDrawLine( artRocket.x - 1, artRocket.y - 2, artRocket.x,     artRocket.y - 2 );
        gbDrawLine( artRocket.x + 1, artRocket.y - 1, artRocket.x + 1, artRocket.y );
        gbDrawLine( artRocket.x - 2, artRocket.y - 1, artRocket.x - 2, artRocket.y );
        gbDrawLine( artRocket.x - 1, artRocket.y + 1, artRocket.x,     artRocket.y + 1 );
    }
    else if( artTimer == 5 )
    {
        gbSetColor( GB_WHITE );
        gbDrawPixel( artRocket.x, artRocket.y );
        gbSetColor( GB_INVERT );
        gbDrawLine( artRocket.x - 1, artRocket.y,     artRocket.x,     artRocket.y - 1 );
        gbDrawLine( artRocket.x,     artRocket.y + 1, artRocket.x + 1, artRocket.y );
    }
    else if( artTimer == 6 )
    {
        gbSetColor( GB_INVERT );
        gbDrawPixel( artRocket.x, artRocket.y );
    }
}

void artDrawGameOver()
{
    int flavor;

    gbSetColor( GB_WHITE );
    gbFillRect( 4, 5, 76, 24 );
    gbSetColor( GB_BLACK );
    gbDrawRect( 5, 6, 74, 22 );
    gbCursorY = 11;
    gbCursorX = 17;
    if( artPlayers[ artCurrentPlayer ].isIA == 0 ) gbPrintString( "Player " );
    else gbPrintString( "  CPU " );
    gbPrintNumber( artCurrentTeam + 1 );
    gbPrintString( " win!\n" );
    gbCursorX = 7;

    flavor = artRandomVal % 10;
    if( flavor == 0 ) gbPrintString( "Others are losers!\n" );
    else if( flavor == 1 ) gbPrintString( "    Congrats!     \n" );
    else if( flavor == 2 ) gbPrintString( " Better next time \n" );
    else if( flavor == 3 ) gbPrintString( "   Frtzz Gzzuit!  \n" );
    else if( flavor == 4 ) gbPrintString( " Game by Frakasss \n" );
    else if( flavor == 5 ) gbPrintString( "  Not so bad...   \n" );
    else if( flavor == 6 ) gbPrintString( "  WHO'S THE BEST? \n" );
    else if( flavor == 7 ) gbPrintString( "Not really fair...\n" );
    else if( flavor == 8 ) gbPrintString( "Quite boring game.\n" );
    else if( flavor == 9 ) gbPrintString( " Just dust in eye \n" );
}

// -----------------------------------------------------------------------------
// Title screen - see this file's own header comment ("TITLE SCREEN").
// -----------------------------------------------------------------------------

void artUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 6, 4, artGamelogoBitmap );
    gbCursorX = 28;
    gbCursorY = 41;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) ) artGameStatus = artTitleReturnState;
}

// -----------------------------------------------------------------------------
// Button handling (function.ino's own `fnctn_checkbuttons()`, split one
// function per real `gamestatus` case).
// -----------------------------------------------------------------------------

void artCheckButtonsSelectMap()
{
    if( gbPressed( BTN_DOWN )  && artGameLevel + 3 <= artNbAvailableLevel ) artGameLevel = artGameLevel + 3;
    if( gbPressed( BTN_UP )    && artGameLevel - 3 >= 0 )                   artGameLevel = artGameLevel - 3;
    if( gbPressed( BTN_RIGHT ) && artGameLevel + 1 <= artNbAvailableLevel ) artGameLevel = artGameLevel + 1;
    if( gbPressed( BTN_LEFT )  && artGameLevel - 1 >= 0 )                   artGameLevel = artGameLevel - 1;
    if( gbPressed( BTN_A ) )
    {
        if( artGameLevel == 0 )
        {
            artGameStatus = ART_OPTIONS;
            artSetting = 0;
        }
        else
        {
            artRandomVal = arand( 21 );
            artGameStatus = ART_NEW_LEVEL;
        }
    }
    if( gbPressed( BTN_C ) )
    {
        artTitleReturnState = ART_SELECT_MAP;
        artGameStatus = ART_TITLE;
    }
}

void artCheckButtonsOptions()
{
    if( gbPressed( BTN_DOWN ) )
    {
        if( artSetting < 5 ) artSetting = artSetting + 1; else artSetting = 0;
    }
    if( gbPressed( BTN_UP ) )
    {
        if( artSetting > 0 ) artSetting = artSetting - 1; else artSetting = 5;
    }
    if( gbPressed( BTN_RIGHT ) )
    {
        if( artSetting == 0 )
        {
            if( artNbTeam < 4 ) artNbTeam = artNbTeam + 1; else artNbTeam = 2;
            if( artNbCpuTeam == artNbTeam ) artNbCpuTeam = artNbCpuTeam - 1;
        }
        else if( artSetting == 1 )
        {
            if( artNbPlayer < 4 ) artNbPlayer = artNbPlayer + 1; else artNbPlayer = 1;
        }
        else if( artSetting == 2 )
        {
            if( artUnitLife < 3 ) artUnitLife = artUnitLife + 1; else artUnitLife = 1;
        }
        else if( artSetting == 3 )
        {
            if( artNbCpuTeam < artNbTeam - 1 ) artNbCpuTeam = artNbCpuTeam + 1; else artNbCpuTeam = 0;
            if( artNbCpuTeam == artNbTeam ) artNbCpuTeam = artNbCpuTeam - 1;
        }
        else if( artSetting == 4 )
        {
            if( artGravity < 3 ) artGravity = artGravity + 1; else artGravity = 1;
        }
    }
    if( gbPressed( BTN_LEFT ) )
    {
        if( artSetting == 0 )
        {
            if( artNbTeam > 2 ) artNbTeam = artNbTeam - 1; else artNbTeam = 4;
            if( artNbCpuTeam == artNbTeam ) artNbCpuTeam = artNbCpuTeam - 1;
        }
        else if( artSetting == 1 )
        {
            if( artNbPlayer > 1 ) artNbPlayer = artNbPlayer - 1; else artNbPlayer = 4;
        }
        else if( artSetting == 2 )
        {
            if( artUnitLife > 1 ) artUnitLife = artUnitLife - 1; else artUnitLife = 3;
        }
        else if( artSetting == 3 )
        {
            if( artNbCpuTeam > 0 ) artNbCpuTeam = artNbCpuTeam - 1; else artNbCpuTeam = artNbTeam;
            if( artNbCpuTeam == artNbTeam ) artNbCpuTeam = artNbCpuTeam - 1;
        }
        else if( artSetting == 4 )
        {
            if( artGravity > 1 ) artGravity = artGravity - 1; else artGravity = 3;
        }
    }
    if( gbPressed( BTN_B ) || ( gbPressed( BTN_A ) && artSetting == 5 ) ) artGameStatus = ART_SELECT_MAP;
    if( gbPressed( BTN_C ) )
    {
        artTitleReturnState = ART_OPTIONS;
        artGameStatus = ART_TITLE;
    }
}

void artCheckButtonsPause()
{
    if( gbPressed( BTN_DOWN ) )
    {
        if( artSetting < 1 ) artSetting = artSetting + 1; else artSetting = 0;
    }
    if( gbPressed( BTN_UP ) )
    {
        if( artSetting > 0 ) artSetting = artSetting - 1; else artSetting = 1;
    }
    if( gbPressed( BTN_B ) ) artGameStatus = ART_RUNNING;
    // Real upstream reads RELEASED here, not pressed - a deliberate
    // real design choice (the same physical C-press that opened this
    // pause screen must not double as an instant A-release confirm).
    if( gbReleased( BTN_A ) )
    {
        if( artSetting == 0 ) artGameStatus = ART_RUNNING;
        else if( artSetting == 1 ) artGameStatus = ART_SELECT_MAP;
    }
    if( gbPressed( BTN_C ) ) artGameStatus = ART_SELECT_MAP;
}

void artCheckButtonsGameOver()
{
    if( gbPressed( BTN_A ) ) artGameStatus = ART_SELECT_MAP;
}

void artCheckButtonsSelectUnit()
{
    if( gbPressed( BTN_LEFT ) || gbPressed( BTN_RIGHT ) || gbPressed( BTN_B ) ) artPlayers[ artCurrentPlayer ].timer = 0;
}

void artCheckButtonsRunning()
{
    if( gbRepeat( BTN_A, 1 ) && artPower < 10 )
    {
        artPower = artPower + 1;
    }
    else
    {
        if( artPower > 0 )
        {
            artRocket.x = artPlayers[ artCurrentPlayer ].x + ( 1 + artPlayers[ artCurrentPlayer ].dir ) + ( ( artTrajParamX[ artAngle ] / 10 ) * ( ( artPlayers[ artCurrentPlayer ].dir * 2 ) - 1 ) );
            artRocket.y = artPlayers[ artCurrentPlayer ].y + ( artTrajParamY[ artAngle ] / 10 ) + 1;
            artRocket.xTraj = artTrajParamX[ artAngle ] / 10 * ( artPower / 2 ) * ( ( artPlayers[ artCurrentPlayer ].dir * 2 ) - 1 );
            artRocket.yTraj = artTrajParamY[ artAngle ] / 10 * ( artPower / 2 );
            artGameStatus = ART_ANIMFIRE;
        }
        else
        {
            if( ( gbRepeat( BTN_UP, 1 ) || gbPressed( BTN_UP ) ) && artAngle > 0 ) artAngle = artAngle - 1;
            if( ( gbRepeat( BTN_DOWN, 1 ) || gbPressed( BTN_DOWN ) ) && artAngle < 8 ) artAngle = artAngle + 1;

            if( gbPressed( BTN_RIGHT ) )
            {
                artPlayers[ artCurrentPlayer ].dir = 1;
            }
            else if( gbRepeat( BTN_RIGHT, 1 ) )
            {
                if( gbGetPixel( artPlayers[ artCurrentPlayer ].x + 4, artPlayers[ artCurrentPlayer ].y + 3 ) == 0 )
                  artPlayers[ artCurrentPlayer ].x = artPlayers[ artCurrentPlayer ].x + 1;
            }

            if( gbPressed( BTN_LEFT ) )
            {
                artPlayers[ artCurrentPlayer ].dir = 0;
            }
            else if( gbRepeat( BTN_LEFT, 1 ) )
            {
                if( gbGetPixel( artPlayers[ artCurrentPlayer ].x - 1, artPlayers[ artCurrentPlayer ].y + 3 ) == 0 )
                  artPlayers[ artCurrentPlayer ].x = artPlayers[ artCurrentPlayer ].x - 1;
            }

            if( gbPressed( BTN_B ) )
            {
                if( artJumpStatus == 0 && artPower == 0
                    && gbGetPixel( artPlayers[ artCurrentPlayer ].x, artPlayers[ artCurrentPlayer ].y - 3 ) == 0
                    && gbGetPixel( artPlayers[ artCurrentPlayer ].x + 3, artPlayers[ artCurrentPlayer ].y - 3 ) == 0 )
                {
                    artPlaySoundFx( 5 );
                    artJumpStatus = 6;
                }
            }
            if( gbPressed( BTN_C ) )
            {
                artSetting = 0;
                artGameStatus = ART_PAUSE;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void gameArtillery_init()
{
    gbBegin();

    artNbAvailableLevel = 26;
    artConstTimer = 20;
    artSetting = 0;
    artTimer = 0;
    artRandomVal = arand( 21 );

    artNbTeam = 2;
    artNbPlayer = 3;
    artNbCpuTeam = 1;
    artUnitLife = 3;
    artGravity = 1;

    artGameLevel = 0;
    artTitleReturnState = ART_SELECT_MAP;
    artGameStatus = ART_TITLE;
}

void gameArtillery_update()
{
    if( !gbUpdate() ) return;

    if( artGameStatus == ART_TITLE )
    {
        artUpdateTitle();
    }
    else if( artGameStatus == ART_SELECT_MAP )
    {
        artCheckButtonsSelectMap();
        artDrawSelectMap();
    }
    else if( artGameStatus == ART_OPTIONS )
    {
        artDrawOptions();
        artCheckButtonsOptions();
    }
    else if( artGameStatus == ART_NEW_LEVEL )
    {
        int t;
        artScreen = artGameLevel - 1;
        artNewLevel();
        artPower = 0;
        artAngle = 4;
        artJumpStatus = 0;
        artCurrentTeam = 0;
        artCurrentPlayer = 0;
        artPlayers[ 0 ].timer = artConstTimer;
        for( t = 0; t < artNbTeam; t = t + 1 )
        {
            artTeams[ t ].nbAlive = artNbPlayer;
            artTeams[ t ].lastPlayer = 0;
        }
        artGameStatus = ART_SELECT_UNIT;
    }
    else if( artGameStatus == ART_PAUSE )
    {
        artDrawPause();
        artCheckButtonsPause();
    }
    else if( artGameStatus == ART_SELECT_UNIT )
    {
        artDrawLandscape();
        artDrawPlayers();
        artDrawLife();
        artDrawTeam();
        artSelectUnit();
    }
    else if( artGameStatus == ART_RUNNING )
    {
        artDrawLandscape();
        artDrawPlayers();

        if( artPlayers[ artCurrentPlayer ].isIA == 0 )
        {
            artCheckButtonsRunning();
        }
        else
        {
            artAiThink();
            if( gbPressed( BTN_C ) ) artGameStatus = ART_PAUSE;
        }

        artCheckJump();
        if( artJumpStatus < 3 )
        {
            artCheckPlayerPos();
            if( artPlayers[ artCurrentPlayer ].dead == 1 ) artNextPlayer();
        }

        artDrawPower();
        artDrawTeam();
        artDrawLife();
        if( artPower == 0 && artJumpStatus == 0 ) artDrawCursor();
    }
    else if( artGameStatus == ART_ANIMFIRE )
    {
        artPlaySoundFx( 0 );
        artDrawLandscape();
        artDrawPlayers();
        artNextProjPosition();
        artCheckCollision();
        artDrawProjectile();
    }
    else if( artGameStatus == ART_BOOM )
    {
        artPlaySoundFx( 1 );
        artDrawLandscape();
        artDrawPlayers();
        artTimer = artTimer + 1;
        artDrawBoom();
        if( artTimer == 2 ) artRebuildMap();
        else if( artTimer == 7 )
        {
            artTimer = 0;
            artCheckDead();
            artGameStatus = ART_WAIT;
        }
    }
    else if( artGameStatus == ART_WAIT )
    {
        int j, anyFalling;
        artDrawLandscape();
        artDrawPlayers();
        artCheckPlayerPos();
        anyFalling = 0;
        for( j = 0; j < artNbPlayer * artNbTeam; j = j + 1 )
        {
            if( artPlayers[ j ].fall > 0 ) anyFalling = 1;
        }
        if( anyFalling == 0 ) artGameStatus = ART_DAMAGE;
    }
    else if( artGameStatus == ART_DAMAGE )
    {
        artOutCountr3 = 0;
        artDrawPlayers();
        artDrawDamage();
        artDrawLandscape();
        if( artOutCountr3 == 0 ) artNextPlayer();
        artGameOver();
    }
    else if( artGameStatus == ART_GAMEOVER )
    {
        artDrawLandscape();
        artDrawPlayers();
        artDrawGameOver();
        artCheckButtonsGameOver();
    }

    gbRenderFrame();
}
