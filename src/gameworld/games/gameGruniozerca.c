#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Gruniozerca (arhneu / Arkadiusz "Dark Archon" Kaminski, Unlicense/public
// domain - real LICENSE file confirmed, github.com/arhneu/gruniozerca-
// gamebuino). A burger-themed arcade game, itself a Gamebuino port of an
// original NES game called Gruniozerca ("Pig-Eater"): steer Grunio left/
// right along the bottom of the screen, toggle his colour with Button A,
// and catch the falling carrot whose colour matches his own - a match
// scores a point, a colour mismatch (or a missed carrot) costs a life.
// Three lives, unlimited carrots, topscore persisted across sessions.
//
// Upstream ships two nearly-identical language variants sharing one
// support tab (`arhn.ino`, the custom font). Per this task's own explicit
// instruction, only the English-translated main file (`gruniozerca-en.ino`)
// was read and ported; `gruniozerca.ino` (the original, undecorated
// filename) is the Polish-language duplicate and was deliberately never
// opened - the two differ only in on-screen text strings, and porting the
// English one is the only sensible choice for this cartridge's audience.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (this dialect has no classes/methods - see
// gamePong.c's own header comment for the general pattern). Every global
// got a `gruni`-prefixed name (this cartridge has no linker - one flat
// global namespace across every game). `rand() % N` became `arand(N)`
// (this dialect's own established, sign-safe RNG helper - see
// avrCompat.h). `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a
// documented no-op. Array declarations use this dialect's own required
// `TYPE[N] name` order throughout (bitmaps, the font table, the Konami
// buffer) - verified by re-reading this whole file end to end before
// finishing. Every real PROGMEM bitmap/font byte became one plain `int`
// cell; every byte upstream wrote as an `0b...` binary literal (the four
// Grunio sprite frames - the burger/logo bitmaps and the whole font table
// were already plain `0x`-hex upstream, so those needed no conversion at
// all) was converted to `0x` hex here (e.g. `0b01111110` -> `0x7E`). No
// `switch` was used anywhere in this file (this project has no proven
// switch-statement support - every branch below is a plain if/else-if
// chain, including the menu's own option dispatch, which upstream itself
// wrote as four independent, mutually-exclusive `if`s rather than a
// switch anyway).
//
// Upstream's own blocking `gb.titleScreen(F(""), logo)` (called once from
// setup(), before its own real `loop()`/`start`-flag state machine even
// begins) was converted into an explicit GRUNI_STATE_TITLE state, matching
// this project's own established "blocking widget -> explicit resumable
// state" treatment (see gamePong.c's own header comment) - dismissed by a
// genuine `gbPressed(BTN_A)`, exactly like every other ported title
// screen. The real 64x36 `logo[]` bitmap is restored via `gbDrawBitmap()`
// and, matching gameMaze.c's own documented precedent for the exact same
// situation, centered horizontally here (`x = (LCDWIDTH-64)/2`) rather
// than upstream's real hardware-only `x=0` (real `titleScreen()` composes
// the logo alongside Gamebuino's own boot-logo furniture this shim has no
// equivalent for). Upstream's own remaining three screens (`start==1`
// main menu, `start==2` credits, `start==3` how-to-play) became
// GRUNI_STATE_MENU/CREDITS/RULES; `alive==1`/`alive==0` became
// GRUNI_STATE_PLAY/GAMEOVER. The main menu's own "EXIT GAME" option
// called real `gb.changeGame()` (a real-hardware "switch cartridge slot"
// OS feature with no equivalent in this single-cartridge shared-menu
// model) - substituted with a re-entry into GRUNI_STATE_TITLE instead,
// the exact same "Exit re-shows the title screen" substitution
// gameCrazyCar.c's own header comment already established for its own
// upstream Exit option.
//
// Upstream's own custom `arhn3x5` font (a real, separate PROGMEM table in
// `arhn.ino`, distinct from - though byte-format-identical to - this
// shim's own built-in `gbFont3x5`) replaces the low ASCII control range
// (0-31) with hand-drawn icon glyphs (two carrot variants at 0/1, a life-
// heart at 3, and three button-prompt icons at 21/22/23) instead of the
// usual unprintable characters, on top of standard printable ASCII from
// 32 up. Ported verbatim below as a local `gruniFontArhn` table and
// selected once via `gbSetFont(gruniFontArhn)` in init - `gbSetFont()`
// accepts any conforming `{width,height,glyphs...}` array, not just the
// three shim-provided built-ins, so this needed no shim changes at all.
// Upstream prints these icon glyphs inline mid-string via raw octal
// escapes in C-string literals (`"\25 - MENU"`, `"\26 - RESTART  \27 -
// MENU"`, `gb.display.print("\3")` in a loop) - since this dialect's own
// string-literal escape-sequence support was never proven out and octal
// escapes specifically looked like a real risk not worth taking, every
// one of these was instead rewritten as a direct `gbDrawChar(iconCode,
// gbCursorX, gbCursorY)` call (already a real, declared shim primitive -
// see gamebuinoShim.h) followed by manually advancing `gbCursorX` by one
// glyph cell (`gbFontWidth`), then resuming with a normal `gbPrintString()`
// call for the rest of the line - behaviourally identical to what real
// `Print::write()`'s own per-character loop does internally, just split
// by hand at the icon boundary. The repeated-heart lives HUD
// (`while(draw<lives) print("\3")`) became a small manual loop calling
// `gbDrawChar(3, x, 0)` and stepping `x` by `gbFontWidth` itself, for the
// same reason.
//
// Upstream's own `String konami` (Arduino's `String` class - unavailable,
// no classes in this dialect) tracked the last 16 typed characters as
// text and compared it against the literal "upupdndnlfrtlfrt". Reimplemented
// as `gruniKonamiBuf[8]`, a fixed-size rolling buffer of the last 8 D-pad
// button codes (BTN_UP/DOWN/LEFT/RIGHT's own real integer values already
// happen to be exactly the 4 codes this buffer needs - 0/1/2/3 - so the
// buttons' own existing IDs are pushed directly, no separate mapping
// table needed) compared against {UP,UP,DOWN,DOWN,LEFT,RIGHT,LEFT,RIGHT} -
// the same real Konami-code gesture upstream implements, just without a
// String class backing it. Every Up/Down/Left/Right press in the main
// menu still feeds this buffer exactly like upstream's own unconditional
// `konami.concat(...)` calls (Left/Right feed it too, even though they
// don't move the menu cursor - preserved exactly).
//
// `gb.popup(...)` (a real, small Gamebuino Classic transient-notification
// widget) is provided by `gbPopup()` in `gamebuinoShim.h`/`.c` (a direct
// port of real `Gamebuino::popup()`/`updatePopup()`) and auto-draws itself
// on top of everything else already drawn that frame - every call site
// here calls `gbPopup(text, 20)` directly (the same real upstream duration
// throughout).
//
// Upstream's own global `int topscore = EEPROM.read(0);` initializer runs
// once at real static-init time, before real hardware's own setup()/loop()
// ever start - safe there because real EEPROM is always valid from power-
// on. Here, `eepromSelectGame()` (this cartridge's own per-game EEPROM-slot
// picker) isn't called until right before this game's own init() runs
// (see this project's own CLAUDE.md), so reading it as a plain global
// initializer would read whatever slot happened to be selected *previously*
// - moved into `gameGruniozerca_init()` instead, which runs at exactly the
// right time. This makes Gruniozerca this project's own actual first real
// EEPROM consumer (ahead of `shipwrek`, previously expected to be first -
// see this project's own CLAUDE.md "porting priority audit"): real,
// working high-score persistence, not just an exercised fallback path.
// Fresh/never-written EEPROM cells read back as 255 (this shim's own
// documented real-AVR-accurate default), exactly matching real hardware's
// own behaviour too (`topscore` is a real `int`, and 255 fits it without
// any narrowing, unlike the multi-byte-composition sentinel bugs found
// elsewhere in this project - see gameCrazyTown.c's own header comment) -
// but a fresh save slot showing an already-maxed-out topscore looks broken
// to a player regardless of how faithfully it matches real hardware, so
// `gameGruniozerca_init()` treats a freshly-read 255 as "no score yet" and
// resets it to 0. This narrows, rather than eliminates, the real ambiguity
// upstream's own single-byte-range design already has - a genuinely-earned
// topscore of exactly 255 would also read back as 255 and get reset the
// same way - accepted as a real but vanishingly unlikely edge case (this
// port's own `eeprom_write_byte()` stores a full, unmasked int rather than
// truncating to a real byte the way `EEPROM.write(uint8_t)` does, so a
// genuine score above 255 persists correctly here rather than wrapping the
// way it would on real hardware - a real, one-directional improvement over
// upstream's own byte-truncation limit, not reproduced deliberately).
// `EEPROM.write(0, topscore)` became `eeprom_write_byte(0,
// gruniTopScore)`, called only when `score > topscore`. The Game Over
// screen's own upstream `if(score == topscore)` "!NEW HIGH SCORE!" check
// (not a separate `rekord` flag - upstream computes one but never actually
// reads it anywhere, a genuine dead local variable, dropped here rather
// than ported as an unused int) also fires on an exact *tie* with the
// existing topscore, not just a fresh record - preserved exactly, since
// it's real, shipped upstream behaviour.
//
// Two more real, load-bearing quirks preserved exactly as upstream wrote
// them rather than "fixed": (1) the left/right acceleration counter
// (`acc`/`gruniAcc`) keeps incrementing every held frame even while
// Grunio is pinned against the left/right screen edge (the position-clamp
// guard sits *inside* the acceleration branch, but the `acc++` itself sits
// outside it) - holding a direction into a wall for >10 frames then
// releasing and re-pressing briefly moves at the fast 2px/frame rate
// immediately. (2) the right-move branch's own two separate `posx+1`
// statements (one gated behind `acc>10`, one unconditional) are the
// intentional right-side mirror of the left branch's single `posx-=2` -
// both branches move 2px/frame once accelerated, just written differently
// upstream; kept exactly as two separate increments here rather than
// consolidated into one clearer `-=2`-style line. The burger-mode Konami
// easter egg also has a real, genuine limitation of its own: unlike
// Grunio himself (grunioa/b idle/walk, grunioc/d idle/walk in his other
// colour), the burger sprite has no walking-animation frame of its own at
// all - upstream's own animation-frame if/else both draw the exact same
// `burger`/`burgerb` bitmap either way, so burger-mode Grunio never
// animates while moving. Preserved exactly (both branches call the same
// bitmap on purpose, matching upstream line-for-line) rather than
// "fixing" it with an invented walk frame that never existed upstream.
//
// No `setColor(fg,bg)`/GRAY calls anywhere upstream, and every sprite here
// (grunioa/b/c/d, burger/burgerb) is a single, already-complete, self-
// contained bitmap - none of them are outline+separate-mask pairs like
// gameFlappyBirdo.c's own bird sprites - so the mask/fill-bleed bug that
// bit that file twice (and gameParachute.c once, via setColor's own bg
// argument) has no way to occur here; nothing was skipped or dropped.
//
// `gbDrawChar()`/`gbRepeat()` are used directly as declared in
// gamebuinoShim.h, and no missing shim primitive was found while porting
// this file.

enum GruniState
{
    GRUNI_STATE_TITLE    = 0,
    GRUNI_STATE_MENU     = 1,
    GRUNI_STATE_CREDITS  = 2,
    GRUNI_STATE_RULES    = 3,
    GRUNI_STATE_PLAY     = 4,
    GRUNI_STATE_GAMEOVER = 5
};

int gruniState;

// ---------------------------------------------------------------------------
// Real sprite bitmaps - copied verbatim from upstream's own `grunioa`/
// `gruniob`/`grunioc`/`gruniod`/`burger`/`burgerb` PROGMEM arrays, with
// every `0b...` binary literal converted to `0x` hex (this dialect has no
// binary-literal syntax) and one plain `int` per real byte.
// ---------------------------------------------------------------------------

int gruniGrunioABitmap[10]= // Grunio, idle, colour 0
{
    8, 8,
    0x7E, 0x81, 0x85, 0x81, 0x81, 0x7E, 0x44, 0x66,
};

int gruniGrunioBBitmap[10]= // Grunio, walking, colour 0
{
    8, 8,
    0x7E, 0x81, 0x85, 0x81, 0x81, 0x7E, 0x22, 0x33,
};

int gruniGrunioCBitmap[10]= // Grunio, idle, colour 1 ("black")
{
    8, 8,
    0x7E, 0xFF, 0xFB, 0xFF, 0xFF, 0x7E, 0x44, 0x66,
};

int gruniGrunioDBitmap[10]= // Grunio, walking, colour 1 ("black")
{
    8, 8,
    0x7E, 0xFF, 0xFB, 0xFF, 0xFF, 0x7E, 0x22, 0x33,
};

int gruniBurgerBitmap[10]= // Konami-code easter egg sprite, colour 0
{
    8, 8,
    0x3C, 0x52, 0xA5, 0x81, 0xFF, 0xFF, 0x81, 0x7E,
};

int gruniBurgerBBitmap[10]= // Konami-code easter egg sprite, colour 1
{
    8, 8,
    0x3C, 0x6E, 0xDB, 0xFF, 0x81, 0x81, 0xFF, 0x7E,
};

// Real 64x36 title-screen logo, copied verbatim (already plain 0x-hex
// upstream, no bit conversion needed).
int gruniLogoBitmap[290]=
{
    64, 36,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x7, 0x0, 0x0, 0x0,
    0x1F, 0x7D, 0xBF, 0xFF, 0xFD, 0xFF, 0xDF, 0x38,
    0x31, 0xC7, 0xE9, 0x16, 0x37, 0x44, 0x31, 0xEC,
    0x20, 0x92, 0x49, 0xB4, 0x10, 0x45, 0x24, 0x84,
    0x60, 0x92, 0x49, 0xA4, 0x94, 0xD5, 0x24, 0x92,
    0x47, 0xC6, 0x48, 0xA4, 0x95, 0xDC, 0x67, 0x92,
    0x44, 0x4C, 0x4A, 0xA4, 0x9D, 0xC4, 0xC4, 0x82,
    0x67, 0x4C, 0xA, 0x24, 0x99, 0x5C, 0xC4, 0x82,
    0x20, 0x46, 0xA, 0x24, 0x93, 0x54, 0x64, 0x92,
    0x20, 0x53, 0x1B, 0x24, 0x10, 0x45, 0x30, 0x92,
    0x30, 0xD9, 0xB1, 0x26, 0x30, 0x45, 0x93, 0x92,
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x1, 0xFF, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0xF, 0x0, 0x80, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x18, 0x0, 0x40, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x70, 0x0, 0xA0, 0x0, 0x0,
    0x0, 0x0, 0x1, 0xC0, 0xF0, 0xB0, 0x0, 0x0,
    0x0, 0x0, 0x3, 0x0, 0xD, 0x18, 0x0, 0x0,
    0x0, 0x0, 0xE, 0x0, 0x4, 0x8, 0x0, 0x0,
    0x0, 0x0, 0x18, 0x0, 0x1, 0x8, 0x0, 0x0,
    0x0, 0x0, 0x30, 0x0, 0x1, 0x8, 0x0, 0x0,
    0x0, 0x0, 0x67, 0x0, 0x1, 0x8C, 0x0, 0x0,
    0x0, 0x0, 0xCF, 0x80, 0x1, 0xCE, 0x0, 0x0,
    0x0, 0x1, 0x89, 0x80, 0x3, 0x63, 0x0, 0x0,
    0x0, 0x1, 0x1B, 0x80, 0x7, 0x31, 0x0, 0x0,
    0x0, 0x3, 0x1F, 0x80, 0xE, 0xA9, 0x80, 0x0,
    0x0, 0x6, 0xF, 0x80, 0x3A, 0xA0, 0x80, 0x0,
    0x0, 0xC, 0x7, 0x0, 0x1, 0xE0, 0xC0, 0x0,
    0x0, 0x38, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0,
    0x0, 0xE0, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0,
    0x0, 0x86, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0,
    0x1, 0x8F, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0,
    0x1, 0x19, 0x80, 0x0, 0x0, 0x0, 0x40, 0x0,
};

// ---------------------------------------------------------------------------
// Real custom font (upstream `arhn3x5`, from the shared `arhn.ino` tab) -
// same real {width,height,glyphs...} layout gbFont5x7/gbFont3x5/gbFont3x3
// use (3 column-major, LSB-top bytes per glyph, indexed directly by ASCII
// 0-127), copied verbatim (already plain 0x-hex upstream). Glyphs 0/1 are
// the two carrot icons, 3 is the life-heart, 21/22/23 are the three
// button-prompt icons this file's own header comment explains restoring
// via direct gbDrawChar() calls instead of risking octal string escapes.
// ---------------------------------------------------------------------------

int gruniFontArhn[386]=
{
    3, 5,
    0x0D, 0x16, 0x0D, 0x1D, 0x1F, 0x1D, 0x0A, 0x10, 0x0A, 0x0E, 0x1C, 0x0E,
    0x0C, 0x1E, 0x0C, 0x14, 0x1A, 0x14, 0x16, 0x1F, 0x16, 0x1E, 0x13, 0x1E,
    0x1E, 0x1B, 0x1E, 0x1E, 0x1F, 0x1E, 0x3F, 0x21, 0x3F, 0x3A, 0x2F, 0x3A,
    0x17, 0x3D, 0x17, 0x3F, 0x21, 0x3F, 0x18, 0x1F, 0x02, 0x04, 0x0A, 0x04,
    0x1F, 0x0E, 0x04, 0x04, 0x0E, 0x1F, 0x0A, 0x1F, 0x0A, 0x0E, 0x0E, 0x1F,
    0x04, 0x11, 0x0E, 0x2E, 0x25, 0x2E, 0x2F, 0x2A, 0x2E, 0x26, 0x29, 0x29,
    0x02, 0x1F, 0x02, 0x08, 0x1F, 0x08, 0x15, 0x0E, 0x04, 0x04, 0x0E, 0x15,
    0x0E, 0x15, 0x15, 0x1B, 0x15, 0x1B, 0x04, 0x06, 0x04, 0x04, 0x0C, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x03, 0x00, 0x03, 0x1F, 0x0A, 0x1F,
    0x16, 0x37, 0x1A, 0x19, 0x04, 0x13, 0x0A, 0x15, 0x3A, 0x00, 0x03, 0x00,
    0x00, 0x0E, 0x11, 0x11, 0x0E, 0x00, 0x0A, 0x04, 0x0A, 0x04, 0x0E, 0x04,
    0x00, 0x30, 0x00, 0x04, 0x04, 0x04, 0x00, 0x10, 0x00, 0x18, 0x04, 0x03,
    0x1F, 0x11, 0x1F, 0x12, 0x1F, 0x10, 0x1D, 0x15, 0x17, 0x11, 0x15, 0x1F,
    0x07, 0x04, 0x1F, 0x17, 0x15, 0x1D, 0x1F, 0x15, 0x1D, 0x01, 0x01, 0x1F,
    0x1F, 0x15, 0x1F, 0x17, 0x15, 0x1F, 0x00, 0x0A, 0x00, 0x00, 0x32, 0x00,
    0x04, 0x0A, 0x11, 0x0A, 0x0A, 0x0A, 0x11, 0x0A, 0x04, 0x01, 0x15, 0x02,
    0x0E, 0x11, 0x17, 0x1E, 0x05, 0x1E, 0x1F, 0x15, 0x0A, 0x0E, 0x11, 0x0A,
    0x1F, 0x11, 0x0E, 0x1F, 0x15, 0x11, 0x1F, 0x05, 0x01, 0x0E, 0x11, 0x1D,
    0x1F, 0x04, 0x1F, 0x11, 0x1F, 0x11, 0x08, 0x10, 0x0F, 0x1F, 0x04, 0x1B,
    0x1F, 0x10, 0x10, 0x1F, 0x06, 0x1F, 0x1E, 0x04, 0x0F, 0x0E, 0x11, 0x0E,
    0x1F, 0x09, 0x06, 0x0E, 0x11, 0x2E, 0x1F, 0x05, 0x1A, 0x12, 0x15, 0x09,
    0x01, 0x1F, 0x01, 0x1F, 0x10, 0x1F, 0x0F, 0x18, 0x0F, 0x1F, 0x0C, 0x1F,
    0x1B, 0x04, 0x1B, 0x03, 0x1C, 0x03, 0x19, 0x15, 0x13, 0x00, 0x1F, 0x11,
    0x03, 0x04, 0x18, 0x11, 0x1F, 0x00, 0x02, 0x01, 0x02, 0x20, 0x20, 0x20,
    0x00, 0x01, 0x02, 0x0C, 0x12, 0x1E, 0x1F, 0x12, 0x0C, 0x0C, 0x12, 0x12,
    0x0C, 0x12, 0x1F, 0x0C, 0x1A, 0x14, 0x04, 0x1E, 0x05, 0x24, 0x2A, 0x1E,
    0x1F, 0x02, 0x1C, 0x14, 0x1D, 0x10, 0x20, 0x20, 0x1D, 0x1F, 0x08, 0x14,
    0x11, 0x1F, 0x10, 0x1E, 0x04, 0x1E, 0x1E, 0x02, 0x1C, 0x0C, 0x12, 0x0C,
    0x3E, 0x0A, 0x04, 0x0C, 0x12, 0x3E, 0x1E, 0x04, 0x02, 0x14, 0x16, 0x0A,
    0x02, 0x0F, 0x12, 0x0E, 0x10, 0x1E, 0x0E, 0x10, 0x0E, 0x1E, 0x08, 0x1E,
    0x12, 0x0C, 0x12, 0x26, 0x28, 0x1E, 0x32, 0x2A, 0x26, 0x04, 0x1E, 0x21,
    0x00, 0x1F, 0x00, 0x21, 0x1E, 0x04, 0x01, 0x02, 0x01, 0x3F, 0x21, 0x3F,
};

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------

int gruniScore = 0;
int gruniLives = 3;
int gruniPosX = 35;
int gruniPosY = 30;
int gruniCarrotActive = 0;
int gruniGrunioColor = 0;   // upstream `gcolor` - Grunio's own current colour
int gruniCarrotX;
int gruniCarrotY;
int gruniCarrotColor = 0;   // upstream `color` - the falling carrot's colour
int gruniFlip = 0;          // NOFLIP=0 / FLIPH=1 - Grunio's own facing direction
int gruniOption = 0;
int gruniAcc = 0;
int gruniTopScore = 0;      // loaded from EEPROM in init - see this file's own header comment
int gruniBurgerMode = 0;    // upstream `burgerek` - Konami-code easter egg

int gruniKonamiBuf[8];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void gruniKonamiPush( int code )
{
    int i;
    for( i = 0; i < 7; i++ )
      gruniKonamiBuf[ i ] = gruniKonamiBuf[ i + 1 ];
    gruniKonamiBuf[ 7 ] = code;
}

bool gruniKonamiMatches()
{
    return ( gruniKonamiBuf[ 0 ] == BTN_UP    && gruniKonamiBuf[ 1 ] == BTN_UP
          && gruniKonamiBuf[ 2 ] == BTN_DOWN  && gruniKonamiBuf[ 3 ] == BTN_DOWN
          && gruniKonamiBuf[ 4 ] == BTN_LEFT  && gruniKonamiBuf[ 5 ] == BTN_RIGHT
          && gruniKonamiBuf[ 6 ] == BTN_LEFT  && gruniKonamiBuf[ 7 ] == BTN_RIGHT );
}

// Draws one real icon glyph at the current cursor, then steps the cursor
// forward by one glyph cell - see this file's own header comment on why
// this replaces upstream's own inline octal-escape icon characters.
void gruniDrawIcon( int ch )
{
    gbDrawChar( ch, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
}

// Direct port of upstream's own Grunio/burger draw dispatch - both real
// branches for burger mode draw the exact same bitmap (see this file's
// own header comment on why that's a genuine, preserved upstream quirk,
// not a bug introduced here).
void gruniDrawPlayer( int keyp )
{
    bool animFrame = ( ( gbFrameCount % 2 ) == 0 ) && ( keyp == 1 );

    if( animFrame )
    {
        if( gruniBurgerMode == 1 )
        {
            if( gruniGrunioColor == 0 ) gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniBurgerBitmap, 0, gruniFlip );
            else gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniBurgerBBitmap, 0, gruniFlip );
        }
        else
        {
            if( gruniGrunioColor == 0 ) gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniGrunioBBitmap, 0, gruniFlip );
            else gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniGrunioDBitmap, 0, gruniFlip );
        }
    }
    else
    {
        if( gruniBurgerMode == 1 )
        {
            if( gruniGrunioColor == 0 ) gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniBurgerBitmap, 0, gruniFlip );
            else gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniBurgerBBitmap, 0, gruniFlip );
        }
        else
        {
            if( gruniGrunioColor == 0 ) gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniGrunioABitmap, 0, gruniFlip );
            else gbDrawBitmapRotated( gruniPosX, gruniPosY, gruniGrunioCBitmap, 0, gruniFlip );
        }
    }
}

void gruniDrawMenuIcon()
{
    int y = 10 + gruniOption * 6;
    if( gruniBurgerMode == 1 ) gbDrawBitmapRotated( 1, y, gruniBurgerBitmap, 0, gruniFlip );
    else gbDrawBitmapRotated( 1, y, gruniGrunioABitmap, 0, gruniFlip );
}

// ---------------------------------------------------------------------------
// States
// ---------------------------------------------------------------------------

void gruniUpdateTitle()
{
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, gruniLogoBitmap );
    gbCursorX = 28;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      gruniState = GRUNI_STATE_MENU;
}

void gruniUpdateMenu()
{
    if( gbPressed( BTN_B ) )
    {
        if( gruniKonamiMatches() )
        {
            gruniBurgerMode = 1;
            gbPopup( "BURGER Mode!", 20 );
        }
    }

    if( gbPressed( BTN_A ) )
    {
        if( gruniOption == 1 ) gruniState = GRUNI_STATE_CREDITS;
        else if( gruniOption == 2 ) gruniState = GRUNI_STATE_RULES;
        // upstream: gb.changeGame() - no equivalent in this single-cartridge
        // shared menu; re-shows the title screen instead, matching
        // gameCrazyCar.c's own "Exit" substitution.
        else if( gruniOption == 3 ) gruniState = GRUNI_STATE_TITLE;
        else
        {
            gruniState = GRUNI_STATE_PLAY;
            gbPopup( "Go, Grunio!", 20 );
        }
    }

    gbPrintString( "Main menu:\n \n   NEW GAME\n   CREDITS\n   HOW TO PLAY\n   EXIT GAME\n" );
    gbPrintString( "         : " );
    gbPrintNumber( gruniTopScore );
    gbPrintString( "\n" );
    gbDrawChar( 0, 32, 36 ); // carrot icon next to the high score
    gbPrintString( "         ARHn.EU 2018\n" );

    if( gbPressed( BTN_DOWN ) )
    {
        gruniOption = gruniOption + 1;
        if( gruniOption > 3 ) gruniOption = 0;
        gruniKonamiPush( BTN_DOWN );
    }

    if( gbPressed( BTN_UP ) )
    {
        gruniOption = gruniOption - 1;
        if( gruniOption < 0 ) gruniOption = 3;
        gruniKonamiPush( BTN_UP );
    }

    if( gbPressed( BTN_LEFT ) )
      gruniKonamiPush( BTN_LEFT );

    if( gbPressed( BTN_RIGHT ) )
      gruniKonamiPush( BTN_RIGHT );

    gruniDrawMenuIcon();
}

void gruniUpdateCredits()
{
    gbPrintString( "Gruniozerca GBuino\n \nCode,gfx: Dark Archon\nLogo: Neko\nConcept: Dizzy9\n \nWWW: ARHN.EU\n" );
    gruniDrawIcon( 21 );
    gbPrintString( " - MENU\n" );

    // Leftover Polish-only artifact from the original bilingual source (a
    // manual ogonek-dot hack for the letter z with a diacritic, "z with a
    // dot above") - the English text above draws no such letter, so this
    // is just a single stray pixel, but it's preserved exactly since
    // upstream's own English-translated file still executes it unchanged.
    gbDrawPixel( 25, 0 );

    if( gbPressed( BTN_A ) )
      gruniState = GRUNI_STATE_MENU;
}

void gruniUpdateRules()
{
    gbPrintString( "Grunio's peckish!\nEat carrots and\ncollect points.\n \nUse " );
    gruniDrawIcon( 21 );
    gbPrintString( " to match\nGrunio's color to\nvegetables.\nEnjoy! Mlem!\n" );

    if( gbPressed( BTN_A ) )
      gruniState = GRUNI_STATE_MENU;
}

void gruniUpdatePlay()
{
    // Upstream: an instant, deliberate "give up" gesture - Button C forces
    // a game over rather than just pausing/quitting back to the menu.
    if( gbPressed( BTN_C ) )
      gruniLives = 0;

    int keyp = 0;

    // Grunio moves left - see this file's own header comment on the real,
    // preserved "acc keeps climbing against a wall" quirk.
    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        gruniAcc = gruniAcc + 1;
        if( gruniPosX > 0 )
        {
            if( gruniAcc > 10 )
            {
                gruniPosX = gruniPosX - 2;
                if( gruniPosX < 0 ) gruniPosX = 0;
            }
            else
              gruniPosX = gruniPosX - 1;

            gruniFlip = 1; // FLIPH
            keyp = 1;
        }
    }

    // Grunio moves right
    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        gruniAcc = gruniAcc + 1;
        if( gruniPosX < LCDWIDTH - 8 )
        {
            if( gruniAcc > 10 )
              gruniPosX = gruniPosX + 1;

            gruniPosX = gruniPosX + 1;
            if( gruniPosX > LCDWIDTH - 8 ) gruniPosX = LCDWIDTH - 8;

            gruniFlip = 0; // NOFLIP
            keyp = 1;
        }
    }

    if( gbReleased( BTN_LEFT ) || gbReleased( BTN_RIGHT ) )
      gruniAcc = 0;

    // change Grunio's colour
    if( gbPressed( BTN_A ) )
    {
        gruniGrunioColor = gruniGrunioColor + 1;
        if( gruniGrunioColor > 1 ) gruniGrunioColor = 0;
    }

    // HUD - one life-heart icon per remaining life, left to right
    int i;
    int lifeX = 0;
    for( i = 0; i < gruniLives; i++ )
    {
        gbDrawChar( 3, lifeX, 0 );
        lifeX = lifeX + gbFontWidth;
    }

    gbDrawChar( 0, 65, 0 ); // carrot icon next to the score
    gbCursorX = 70;
    gbCursorY = 0;
    gbPrintNumber( gruniScore );

    gruniDrawPlayer( keyp );

    // falling carrot
    if( gruniCarrotActive == 0 )
    {
        gruniCarrotColor = arand( 2 );
        gruniCarrotActive = 1;
        gruniCarrotX = arand( 78 );
        gruniCarrotY = 5;
    }

    gbDrawChar( gruniCarrotColor, gruniCarrotX, gruniCarrotY );
    gruniCarrotY = gruniCarrotY + 1;

    // Grunio - carrot collision
    if( gruniCarrotY > gruniPosY - 3 && ( gruniCarrotX >= gruniPosX - 1 && gruniCarrotX < gruniPosX + 8 ) )
    {
        if( gruniCarrotColor == gruniGrunioColor )
        {
            gruniCarrotActive = 0;
            gruniScore = gruniScore + 1;
            gbPlayTick();
            gbPopup( "Mlem!", 20 );
        }
    }

    // missed carrot - life lost
    if( gruniCarrotY > LCDHEIGHT )
    {
        gruniLives = gruniLives - 1;
        gruniCarrotActive = 0;
        gbPlayCancel();
        gbPopup( "Noooooo!", 20 );
    }

    if( gruniLives == 0 )
      gruniState = GRUNI_STATE_GAMEOVER;
}

void gruniUpdateGameOver()
{
    // Upstream also computes a `rekord` flag here but never actually reads
    // it anywhere - a genuine dead local variable, dropped (see this
    // file's own header comment).
    if( gruniScore > gruniTopScore )
    {
        gruniTopScore = gruniScore;
        eeprom_write_byte( 0, gruniTopScore );
    }

    gbDrawBitmap( -7, -14, gruniLogoBitmap );

    gbCursorX = 44;
    gbCursorY = 6;
    gbPrintString( "GAME OVER!\n" );
    gbCursorX = 44;
    gbPrintString( "Score: " );
    gbPrintNumber( gruniScore );
    gbPrintString( "\n" );

    gbCursorY = 24;
    gbCursorX = 0;
    // Fires on an exact tie with the existing topscore too, not just a
    // fresh record - a real, preserved upstream quirk (see this file's
    // own header comment).
    if( gruniScore == gruniTopScore )
      gbPrintString( "!NEW HIGH SCORE!\n" );
    else
      gbPrintString( " \n" );

    gbPrintString( "High: " );
    gbPrintNumber( gruniTopScore );
    gbPrintString( "\n \n" );

    gruniDrawIcon( 22 );
    gbPrintString( " - RESTART  " );
    gruniDrawIcon( 23 );
    gbPrintString( " - MENU" );

    if( gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gruniScore = 0;
        gruniLives = 3;
        gruniPosX = 35;
        gruniPosY = 30;
        gruniCarrotActive = 0;
        gruniGrunioColor = 0;
        gruniState = GRUNI_STATE_PLAY;

        if( gbPressed( BTN_B ) ) gbPopup( "Play it again, Guno!", 20 );
        if( gbPressed( BTN_C ) ) gruniState = GRUNI_STATE_MENU;
    }
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

void gameGruniozerca_init()
{
    gbBegin();
    gbSetFont( gruniFontArhn );

    // Read after gbBegin() (not as a plain global initializer - see this
    // file's own header comment) so this always reads the slot
    // eepromSelectGame() already picked for this game.
    gruniTopScore = eeprom_read_byte( 0 );
    if( gruniTopScore == 255 ) gruniTopScore = 0;

    int i;
    for( i = 0; i < 8; i++ )
      gruniKonamiBuf[ i ] = -1;

    gruniState = GRUNI_STATE_TITLE;
    gbPickRandomSeed();
}

void gameGruniozerca_update()
{
    if( !gbUpdate() ) return;

    gbSetColor( 1 );

    if( gruniState == GRUNI_STATE_TITLE ) gruniUpdateTitle();
    else if( gruniState == GRUNI_STATE_MENU ) gruniUpdateMenu();
    else if( gruniState == GRUNI_STATE_CREDITS ) gruniUpdateCredits();
    else if( gruniState == GRUNI_STATE_RULES ) gruniUpdateRules();
    else if( gruniState == GRUNI_STATE_PLAY ) gruniUpdatePlay();
    else gruniUpdateGameOver();

    // real gb.popup()'s own overlay is drawn automatically by
    // gbRenderFrame() below - see header comment.
    gbRenderFrame();
}
