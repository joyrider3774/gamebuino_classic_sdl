#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Simonbuino v2 (Jerom / Forklift5, license: None specified -
// github.com/Forklift5/Simonbuino). A Simon-says memory game for Gamebuino
// Classic: the console plays a growing sequence of 4 tones (mapped to
// Up/Right/Down/Left), the player must repeat it back exactly; get it right
// and the sequence grows by one more note, get it wrong and the game shows
// a brief fail flash before returning to the title screen. Sound FX credited
// upstream to "FX Synth by yodasvideoarcade".
//
// Upstream is a real multi-tab Arduino sketch (Simonbuino.ino/Buttons.ino/
// Melody.ino/Player.ino/Sound.ino) - the Arduino IDE concatenates every .ino
// tab in a sketch folder into one translation unit before compiling, so
// this was always genuinely one program across 5 files, not 5 separate
// pieces - consolidated here into this one source file accordingly, in the
// same spirit as gamePong.c/gameAgaruino.c's own single-file ports. Every
// upstream global/function got a `simon`-prefixed name (this dialect's
// cartridge has no linker - every game shares one flat global namespace -
// see this project's own gamebuinoShim.h header comment) rather than
// upstream's own generic tab-derived names (`Buttons`/`Player`/`Sound`/
// `Melody` are exactly the kind of name a future game could collide with).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(N)` became `arand(N)`.
// `byte` became plain `int` (this dialect's own avrCompat.h aliasing).
//
// Two real simplifications remain beyond the usual mechanical rewrite (a
// third, dropped bitmap art, was later restored once this shim grew a real
// bitmap blitter - see the dedicated paragraph on that below the list):
//
// 1. **No Sound::command() effects.** Upstream's own `playsoundfx()` used
//    `gb.sound.command(...)` to set a per-note waveform, volume-slide, and
//    pitch-slide before calling `gb.sound.playNote(pitch, duration,
//    channel)` (an "FX Synth" preset system) - this shim only implements a
//    flat `gbPlayNote(pitch, duration)` (see gamebuinoShim.h's own header
//    comment: the tracker/pattern/effects part of real Sound.h is out of
//    scope for this project's first pass). Every `command()` call was
//    dropped; the plain `playNote()` call itself is kept, using exactly the
//    same pitch/duration values upstream's own `soundfx[][]` table already
//    stored for it (`soundfx[fx][1]` = pitch, `soundfx[fx][7]` = duration -
//    both already real Gamebuino pitch/tick values, not raw Hz, so no unit
//    conversion was needed at all here, unlike a game that called `tone()`
//    directly). The 4 direction tones and the fail tone all still play,
//    just without their own slide/vibrato flavor.
//
// 2. **`gb.titleScreen()`'s own real blocking-call shape.** Upstream's own
//    `initGame()` opens with a blocking `gb.titleScreen(titleBitmap)` call,
//    and only *after* that call returns (i.e. only once a real A-press
//    dismisses it) does the rest of `initGame()` run: reseeding
//    `melody[]`, and resetting every round variable. `initGame()` itself is
//    called from 3 places - real cartridge boot, a manual Button-C press
//    (`key_reset()`), and a failed round timing out (`player_fail()`) - so
//    on real hardware, *every one* of those events shows the title screen
//    again and needs a fresh A-press before a new sequence actually starts.
//    Ported as an explicit `SIMON_STATE_TITLE`/`SIMON_STATE_PLAY` state
//    machine (matching gamePong.c's own titleScreen-to-state-machine
//    treatment): `simonBeginTitle()` only switches state (mirroring the
//    blocking call itself), and the actual melody-reseed-plus-var-reset
//    logic lives in `simonBeginPlay()`, run only once, on the exact A-press
//    that dismisses the title - exactly mirroring upstream's own "reset
//    happens after titleScreen() returns" ordering. All 3 real reset call
//    sites (boot / Button C / fail-timeout) call `simonBeginTitle()` here.
//
// **Real bitmap art restored.** Upstream drew its entire play screen
// (title logo, background frame, all 4 direction-pad graphics, and the
// "CONGRATULATIONS!!" smiley screen) from real PROGMEM bitmap arrays via
// `gb.display.drawBitmap(x, y, bmp[, rotation, flip])` - when this file was
// first written, this shim had no `gbDrawBitmap()`/`gbDrawBitmapRotated()`
// at all, so every bitmap was dropped for plain-primitive stand-ins (title/
// win text, 4 outlined rectangles for the pads). Now that both primitives
// exist (ported bit-for-bit from the real Display.cpp, including its own
// real rotation/flip quirks - see gamebuinoShim.h's own header comment on
// `gbDrawBitmapRotated`), every real bitmap has been restored verbatim:
// `simonTitleBitmap` (title screen, plain `gbDrawBitmap` at upstream's own
// real `(0, 12+logoOffset)` position - confirmed directly against the real
// `Gamebuino::titleScreen()` source, `logoOffset` works out to 0 here since
// this game passes no title-screen name string), `simonFrameBitmap` (play-
// screen background, plain `gbDrawBitmap(10, 0, ...)`), `simonSmileyBitmap`
// (the win screen's own smiley, plain `gbDrawBitmap(34, 16, ...)`), and the
// 4 direction pads - each drawn as TWO overlaid bitmaps exactly as upstream
// does: a shared `simonButtonMaskBitmap` (upstream's own real GRAY
// "background" shape, reused via rotation/flip for all 4 directions - the
// one genuine "same base bitmap, 4 orientations" case in this game) plus
// its own distinct per-direction content bitmap (`simonTopBitmap`/
// `simonRightBitmap`/`simonBottomBitmap`/`simonLeftBitmap` - 4 separate
// real bitmaps, NOT the same art rotated, confirmed by reading their real
// byte data directly), drawn with the exact same rotation/flip constant as
// its own mask, read directly off upstream's own real `drawButtons()` call
// sites rather than guessed: top = `gbDrawBitmapRotated(30, 0, ..., 1, 0)`
// (ROTCCW/NOFLIP), right = `gbDrawBitmap(50, 12, ...)` (upstream passes no
// rotation/flip args at all here), bottom = `gbDrawBitmapRotated(30, 32,
// ..., 3, 0)` (ROTCW/NOFLIP), left = `gbDrawBitmapRotated(18, 12, ..., 0,
// 1)` (NOROT/FLIPH). All 8 real byte tables were already `0x`-hex literals
// (not Arduino `B`-binary), so every one was copied in verbatim,
// unconverted (matching gameConduit.c's own `condTitleBitmap` precedent).
//
// Each mask is drawn in real `GB_GRAY` (the shim's own dithered checkerboard
// color, matching real `Display::setColor(GRAY)`), then its own content
// bitmap on top in BLACK, matching upstream's own exact two-call-per-pad
// `setColor(GRAY); drawBitmap(mask); setColor(BLACK); drawBitmap(content);`
// sequence exactly.
//
// The center display (whose-turn indicator + sequence-length counter) is a
// direct, literal port of upstream's own real `drawButtons()` tail: the
// "whose turn" indicator is a single 2x2 BLACK dot (not text) at (37,25)
// during the CPU's own turn or (45,25) during the player's own turn, and
// the counter itself is one `cursorX = 39; cursorY = 18;
// print(melody_step - 1)` call at fontSize 1 - upstream's own exact real
// coordinates and scale, both confirmed directly against Buttons.ino rather
// than adapted to fit around the real pad bitmaps' own tighter footprint.
//
// One genuine upstream bug was found and silently normalized rather than
// preserved, since tracing it through shows it has zero effect on actual
// gameplay: the melody-seeding loop reads `melody[i - 1] = random(NBNOTES)
// + 1;` inside `for (byte i = 0; i < (NOTESMAX - 1); i++)` - on the very
// first iteration (i=0) this writes to `melody[-1]`, which on real AVR
// hardware (`byte i` is unsigned) wraps to `melody[255]`, an out-of-bounds
// write 255 elements past the real 53-element array. However, every
// *other* iteration (i=1..51) correctly fills `melody[0..50]` - exactly the
// full range the game ever actually reads (`melody_step`'s own win
// threshold means `melody_currentnote` never reaches past index 50 during
// real play) - so the array's own actually-read contents end up identical
// whether or not that one stray i=0 iteration ever ran. Ported as the
// obviously-intended `for (i = 0; i < NOTESMAX - 2; i++) simonMelody[i] =
// arand(NBNOTES) + 1;` (filling indices 0..50 directly), reproducing the
// exact same real melody contents without ever performing a genuinely
// unsafe out-of-bounds write into this cartridge's own shared global
// memory (unlike upstream's own array, "some other global variable" here
// could easily be a *different game's* own state).
//
// Two more upstream globals were dropped outright after confirming (by
// reading all 5 real source files completely) that they are genuinely
// inert: `melody_all` is only ever set to `false`, and its own single
// `if (melody_all == false) melody_all == true;` line is itself a typo
// (`==` instead of `=`) that never actually assigns it true - so it is
// never read anywhere as anything but its initial value. `melody_fail` is
// set on every failed round and reset on every game reset, but its one
// potential read site is commented out in upstream's own `key_reset()`
// (`//|| (melody_fail == true)`) - so it, too, is write-only and has no
// observable effect. `game_difficulty` likewise is set true once at reset
// and never toggled anywhere in the real source (apparently a vestigial
// hook for a difficulty option that was never wired up) - its own upstream
// display dot would therefore always just be a permanently-on pixel, so it
// was dropped along with the variable rather than ported as a meaningless
// always-on indicator.

#define SIMON_TIMER_MAX 16 // delay (real update() ticks) to play/wait one note
#define SIMON_NOTESMAX 53  // max notes storable; the real win threshold is 2 less (see below)
#define SIMON_NBNOTES 4    // number of distinct notes/buttons (Up/Right/Down/Left)

// Bitmaps - copied verbatim from upstream's own real `0x`-hex PROGMEM byte
// arrays (see this file's own header comment's "Real bitmap art restored"
// paragraph for exact draw-call coordinates/rotation/flip per bitmap).
int simonTitleBitmap[242]=
{
64,30,0x0,0x1F,0xE0,0x0,0x79,0xB1,0x9E,0x63,0x0,0xFF,0xFC,0x0,0xFD,0xBB,0xBF,0x73,0x3,0xFF,0xFF,0x0,0xFD,0xBF,0xBF,0x7B,0x7,0xFF,0xFF,0x80,0xED,0xBF,0xBF,0x7F,0xB,0xE0,0x1F,0x40,0xE1,0xBF,0xB7,0x7F,0x13,0x9F,0xE7,0x20,0xF9,0xBF,0xB3,0x7F,0x3E,0x6A,0xB9,0xF0,0x7D,0xB5,0xB3,0x6F,0x3C,0xD5,0x5C,0xF0,0x1D,0xB1,0xBF,0x67,0x7D,0xEA,0xAE,0xF8,0xFD,0xB1,0xBF,0x63,0x7B,0xF5,0x5F,0x78,0xF9,0xB1,0x9E,0x63,0x7A,0xFA,0xBB,0x78,0x0,0x0,0x0,0x0,0xF4,0x3F,0xFD,0xBC,0xF8,0xCD,0xB1,0x9E,0xF5,0x58,0x6A,0xBC,0xFC,0xCD,0xB1,0xBF,0xF4,0x10,0x37,0xBC,0xCC,0xCD,0xB9,0xBF,0xF6,0xB4,0xAA,0xBC,0xFC,0xCD,0xBD,0xB3,0xF4,0x10,0x3D,0xBC,0xFE,0xCD,0xBF,0xB3,0xF5,0x53,0x2A,0xBC,0xEE,0xDD,0xBF,0xB7,0xF4,0x18,0x77,0xBC,0xE6,0xFD,0xBF,0xBF,0xF6,0xBF,0xFA,0xBC,0xE6,0xFD,0xB7,0xBF,0x7A,0x70,0x3D,0x78,0xFE,0xFD,0xB3,0xBF,0x7B,0xE0,0x1F,0x78,0xFC,0x79,0xB1,0x9E,0x7D,0xC0,0xE,0xF8,0x0,0x0,0x0,0x0,0x3C,0xC0,0xC,0xF0,0x0,0x1,0xC1,0x80,0x3E,0x60,0x19,0xF0,0x6,0x53,0xE0,0xA0,0x13,0x9F,0xE7,0x20,0x7,0x73,0x6C,0x90,0xB,0xE0,0x1F,0x40,0x7,0x23,0xEA,0xB5,0x7,0xFF,0xFF,0x80,0x0,0x3,0xEA,0xD5,0x3,0xFF,0xFF,0x17,0x67,0x73,0x6D,0xBB,0x0,0xFF,0xFC,0x16,0x77,0x70,0x8,0x1,0x0,0x1F,0xE0,0x77,0x57,0x53,0xE8,0x6,
};

int simonFrameBitmap[386]=
{
64,48,0x0,0x3F,0xFF,0xE0,0x7,0xFF,0xFC,0x0,0x0,0x7F,0xFF,0x0,0x0,0xFF,0xFE,0x0,0x0,0xFF,0xF8,0x3F,0xFC,0x1F,0xFF,0x0,0x1,0xFF,0xF1,0xFF,0xFF,0x8F,0xFF,0x80,0x1,0xFF,0xC7,0xE0,0x7,0xE3,0xFF,0x80,0x3,0xFF,0x9F,0x0,0x0,0xF9,0xFF,0xC0,0x7,0xFF,0x3C,0x0,0x0,0x3C,0xFF,0xE0,0x7,0xFE,0x78,0x0,0x0,0x1E,0x7F,0xE0,0xF,0xFC,0xF0,0x0,0x0,0xF,0x3F,0xF0,0x1F,0xF9,0xF0,0x0,0x0,0xF,0x9F,0xF8,0x1F,0xF3,0xF8,0x0,0x0,0x1F,0xCF,0xF8,0x1F,0xF7,0xF8,0x0,0x0,0x1F,0xEF,0xF8,0x3F,0xE7,0x3C,0x0,0x0,0x3C,0xE7,0xFC,0x3F,0xCE,0xE,0x0,0x0,0x70,0x73,0xFC,0x7F,0xCC,0x7,0x7,0xE0,0xE0,0x33,0xFE,0x7F,0xDC,0x3,0x9F,0xF9,0xC0,0x3B,0xFE,0x7F,0x98,0x1,0xF8,0x1F,0x80,0x19,0xFE,0x7F,0x98,0x0,0xE0,0x7,0x0,0x19,0xFE,0xFF,0xB8,0x0,0xC0,0x3,0x0,0x1D,0xFF,0xFF,0x30,0x1,0x80,0x1,0x80,0xC,0xFF,0xFF,0x30,0x1,0x80,0x1,0x80,0xC,0xFF,0xFF,0x30,0x3,0x0,0x0,0xC0,0xC,0xFF,0xFF,0x30,0x3,0x0,0x0,0xC0,0xC,0xFF,0xFF,0x30,0x3,0x0,0x0,0xC0,0xC,0xFF,0xFF,0x30,0x3,0x18,0x18,0xC0,0xC,0xFF,0xFF,0x30,0x3,0x24,0x24,0xC0,0xC,0xFF,0xFF,0x30,0x3,0x24,0x24,0xC0,0xC,0xFF,0xFF,0x30,0x1,0x99,0x99,0x80,0xC,0xFF,0xFF,0x30,0x1,0x82,0x41,0x80,0xC,0xFF,0xFF,0xB8,0x0,0xC2,0x43,0x0,0x1D,0xFF,0x7F,0x98,0x0,0xE1,0x87,0x0,0x19,0xFE,0x7F,0x98,0x1,0xF8,0x1F,0x80,0x19,0xFE,0x7F,0xDC,0x3,0x9F,0xF9,0xC0,0x3B,0xFE,0x7F,0xCC,0x7,0x7,0xE0,0xE0,0x33,0xFE,0x3F,0xCE,0xE,0x0,0x0,0x70,0x73,0xFC,0x3F,0xE7,0x3C,0x0,0x0,0x3C,0xE7,0xFC,0x1F,0xF7,0xF8,0x0,0x0,0x1F,0xEF,0xF8,0x1F,0xF3,0xF8,0x0,0x0,0x1F,0xCF,0xF8,0x1F,0xF9,0xF0,0x0,0x0,0xF,0x9F,0xF8,0xF,0xFC,0xF0,0x0,0x0,0xF,0x3F,0xF0,0x7,0xFE,0x78,0x0,0x0,0x1E,0x7F,0xE0,0x7,0xFF,0x3C,0x0,0x0,0x3C,0xFF,0xE0,0x3,0xFF,0x9F,0x0,0x0,0xF9,0xFF,0xC0,0x1,0xFF,0xC7,0xE0,0x7,0xE3,0xFF,0x80,0x1,0xFF,0xF1,0xFF,0xFF,0x8F,0xFF,0x80,0x0,0xFF,0xF8,0x3F,0xFC,0x1F,0xFF,0x0,0x0,0x7F,0xFF,0x0,0x0,0xFF,0xFE,0x0,0x0,0x3F,0xFF,0xE0,0x7,0xFF,0xFC,0x0,
};

// Real upstream's own win message, `" CONGRATULATIONS!!\n \    \27 to
// reset"` - built as an explicit int array rather than a plain quoted
// string literal for two reasons: the embedded `\n` needs this shim's own
// real gbPrintString() newline handling (added alongside the font port),
// and `\27` is an octal escape (027 = ASCII 23), one of real Gamebuino's
// own custom low-ASCII icon glyphs, not a printable character a quoted
// string literal can hold directly. Upstream's own `\    ` (backslash
// followed by 4 spaces) is not a standard C escape sequence - real avr-gcc
// treats an unrecognized escape as the literal next character with the
// backslash silently dropped, so this reproduces it as one prior literal
// space (the char right after `\n`) plus that backslash's own literal
// space plus 3 more literal spaces = 5 spaces total before the icon.
char simonWinMessage[35]=
{
    32, 67, 79, 78, 71, 82, 65, 84, 85, 76, 65, 84, 73, 79, 78, 83, 33, 33, // " CONGRATULATIONS!!"
    10, // \n
    32, 32, 32, 32, 32, // 5 literal spaces (see comment above)
    23, // real reset-icon glyph
    32, 116, 111, 32, 114, 101, 115, 101, 116, // " to reset"
    0
};

// Shared diamond "mask" bitmap - reused via rotation/flip for all 4
// direction pads below, exactly matching upstream's own real
// `drawButtons()`. Drawn in real `GB_GRAY`.
int simonButtonMaskBitmap[50]=
{
16,24,0x3,0x0,0xF,0x80,0x1F,0xC0,0x3F,0xC0,0x7F,0xE0,0xFF,0xE0,0xFF,0xE0,0x7F,0xF0,0x7F,0xF0,0x3F,0xF0,0x3F,0xF0,0x3F,0xF0,0x3F,0xF0,0x3F,0xF0,0x3F,0xF0,0x7F,0xF0,0x7F,0xF0,0xFF,0xE0,0xFF,0xE0,0x7F,0xE0,0x3F,0xC0,0x1F,0xC0,0xF,0x80,0x3,0x0,
};

// The 4 direction-pad content bitmaps - genuinely distinct real bitmaps
// (NOT the same art rotated - confirmed by reading their real byte data
// directly), each drawn with its own real rotation/flip constant matching
// its own mask draw call (see header comment).
int simonTopBitmap[50]=
{
16,24,0x2,0x0,0x0,0x0,0xA,0x80,0x4,0x40,0x2A,0xA0,0x0,0x0,0xAA,0xA0,0x44,0x40,0x2A,0xA0,0x0,0x0,0x2A,0xA0,0x4,0x40,0x2A,0xA0,0x0,0x0,0x2A,0xA0,0x44,0x40,0x2A,0xA0,0x0,0x0,0xAA,0xA0,0x44,0x40,0x2A,0x80,0x0,0x0,0xA,0x80,0x0,0x0,
};

int simonRightBitmap[50]=
{
16,24,0x3,0x0,0xA,0x80,0x1D,0xC0,0x2A,0x80,0x77,0x60,0xAA,0xA0,0xDD,0xC0,0x2A,0xA0,0x77,0x70,0x2A,0xA0,0x1D,0xD0,0x2A,0xA0,0x37,0x70,0x2A,0xA0,0x1D,0xD0,0x2A,0xA0,0x77,0x70,0xAA,0xA0,0xDD,0xC0,0x2A,0xA0,0x37,0x40,0xA,0x80,0xD,0x80,0x2,0x0,
};

int simonBottomBitmap[50]=
{
16,24,0x1,0x0,0xA,0x80,0x15,0x40,0x2A,0x80,0x55,0x40,0xAA,0xA0,0x55,0x40,0x2A,0xA0,0x55,0x50,0x2A,0xA0,0x15,0x50,0x2A,0xA0,0x15,0x50,0x2A,0xA0,0x15,0x50,0x2A,0xA0,0x55,0x50,0xAA,0xA0,0x55,0x40,0x2A,0xA0,0x15,0x40,0xA,0x80,0x5,0x0,0x2,0x0,
};

int simonLeftBitmap[50]=
{
16,24,0x0,0x0,0x0,0x0,0x2,0x0,0x0,0x0,0x8,0x80,0x0,0x0,0x22,0x20,0x0,0x0,0x8,0x80,0x0,0x0,0x22,0x20,0x0,0x0,0x8,0x80,0x0,0x0,0x22,0x20,0x0,0x0,0x8,0x80,0x0,0x0,0x22,0x20,0x0,0x0,0x8,0x80,0x0,0x0,0x2,0x0,0x0,0x0,
};

int simonSmileyBitmap[34]=
{
16,16,0x7,0xE0,0x1F,0xF8,0x3F,0xFC,0x7F,0xFE,0x67,0xE6,0xCB,0xD3,0xCB,0xD3,0xCB,0xD3,0xE7,0xE7,0xFF,0xFF,0xC0,0x3,0x60,0x6,0x70,0xE,0x3C,0x3C,0x1F,0xF8,0x7,0xE0,
};

// Sound FX table, copied verbatim from upstream's own "FX Synth" preset
// data - only column 1 (pitch) and column 7 (duration) are actually used by
// this port's own simonPlaySoundFx() (see this file's own header comment,
// point 1, for why the other columns - waveform/pitch-slide/volume-slide -
// are dropped rather than ported).
int simonSoundFx[5][8]= {
    {0, 0, 0, 12, 3, 4, 5, 8},    // sound 1 = up
    {0, 12, 19, 8, 4, 4, 5, 6},   // sound 2 = right
    {0, 19, 13, 16, 4, 7, 5, 8},  // sound 3 = down
    {0, 27, 6, 17, 4, 5, 5, 7},   // sound 4 = left
    {0, 30, 57, 1, 5, 17, 4, 47}, // sound 5 = fail
};

bool simonGameStart;
bool simonDisplayTop;
bool simonDisplayRight;
bool simonDisplayBottom;
bool simonDisplayLeft;
bool simonMelodyPlaying; // true = CPU's turn to play the sequence, false = player's turn

int simonFailTimer;
int simonWaitTimerStart;
int simonWaitTimer;
int simonSoundPlay; // which note (1-5, see simonSoundFx) is currently playing
int simonMelody[53];
int simonMelodyCurrentNote;
int simonMelodyStep; // how many notes long the sequence currently is, plus 1
int simonMelodyTimer;
int simonDelayTimer;

enum SimonState
{
    SIMON_STATE_TITLE = 0,
    SIMON_STATE_PLAY = 1
};

int simonState;

void simonBeginTitle()
{
    simonState = SIMON_STATE_TITLE;
}

// HIDE BUTTONS - one, or all 4 (sound 5 = fail hides every pad at once).
void simonButtonsAllHide()
{
    simonDisplayTop = false;
    simonDisplayRight = false;
    simonDisplayBottom = false;
    simonDisplayLeft = false;
}

void simonButtonHide()
{
    if( simonSoundPlay == 1 ) simonDisplayTop = false;
    else if( simonSoundPlay == 2 ) simonDisplayRight = false;
    else if( simonSoundPlay == 3 ) simonDisplayBottom = false;
    else if( simonSoundPlay == 4 ) simonDisplayLeft = false;
    else if( simonSoundPlay == 5 ) simonButtonsAllHide();
}

void simonButtonsShow()
{
    simonDisplayTop = true;
    simonDisplayRight = true;
    simonDisplayBottom = true;
    simonDisplayLeft = true;
}

// Real direction-pad art, restored via gbDrawBitmap()/gbDrawBitmapRotated()
// - see this file's own header comment's "Real bitmap art restored"
// paragraph for exactly where each real coordinate/rotation/flip value
// came from (upstream's own real `drawButtons()`, read directly, not
// guessed). Each pad draws its shared diamond mask in `GB_GRAY`, then its
// own distinct content bitmap on top in BLACK.
void simonDrawButtons()
{
    if( simonDisplayTop )
    {
        gbSetColor( GB_GRAY );
        gbDrawBitmapRotated( 30, 0, simonButtonMaskBitmap, 1, 0 ); // ROTCCW, NOFLIP
        gbSetColor( 1 );
        gbDrawBitmapRotated( 30, 0, simonTopBitmap, 1, 0 );
    }
    if( simonDisplayRight )
    {
        gbSetColor( GB_GRAY );
        gbDrawBitmap( 50, 12, simonButtonMaskBitmap ); // upstream passes no rotation/flip here
        gbSetColor( 1 );
        gbDrawBitmap( 50, 12, simonRightBitmap );
    }
    if( simonDisplayBottom )
    {
        gbSetColor( GB_GRAY );
        gbDrawBitmapRotated( 30, 32, simonButtonMaskBitmap, 3, 0 ); // ROTCW, NOFLIP
        gbSetColor( 1 );
        gbDrawBitmapRotated( 30, 32, simonBottomBitmap, 3, 0 );
    }
    if( simonDisplayLeft )
    {
        gbSetColor( GB_GRAY );
        gbDrawBitmapRotated( 18, 12, simonButtonMaskBitmap, 0, 1 ); // NOROT, FLIPH
        gbSetColor( 1 );
        gbDrawBitmapRotated( 18, 12, simonLeftBitmap, 0, 1 );
    }

    // Whose-turn indicator: a real, literal upstream detail - not text at
    // all, just a single 2x2 BLACK dot at one of two fixed positions
    // (upstream's own real `drawButtons()`, read directly): (37,25) while
    // the CPU plays the sequence back, (45,25) once it's the player's own
    // turn - upstream's own real `buttonsShow()` call sits inline in this
    // same else-branch too, and is reproduced verbatim here.
    gbSetColor( 1 );
    if( simonMelodyPlaying )
      gbFillRect( 37, 25, 2, 2 );
    else
    {
        simonButtonsShow();
        gbFillRect( 45, 25, 2, 2 );
    }

    // Sequence-length counter (upstream's own real `melody_step - 1`) at
    // upstream's own real, single `cursorX = 39; cursorY = 18;` position -
    // upstream's own real `game_difficulty` indicator dot that would
    // normally sit right below this is dropped (see this file's own header
    // comment - a genuinely inert, always-true upstream flag).
    gbFontSize = 1;
    gbCursorX = 39;
    gbCursorY = 18;
    gbPrintNumber( simonMelodyStep - 1 );
}

void simonPlaySoundFx( int fx )
{
    gbPlayNote( simonSoundFx[ fx ][ 1 ], simonSoundFx[ fx ][ 7 ] );
}

void simonPlaySound()
{
    if( simonSoundPlay == 1 ) simonPlaySoundFx( 0 );
    else if( simonSoundPlay == 2 ) simonPlaySoundFx( 1 );
    else if( simonSoundPlay == 3 ) simonPlaySoundFx( 2 );
    else if( simonSoundPlay == 4 ) simonPlaySoundFx( 3 );
    else if( simonSoundPlay == 5 ) simonPlaySoundFx( 4 );
}

// FAIL - end animation: flash the wrong pad hidden for SIMON_TIMER_MAX
// ticks (playing the fail tone only once), then return to the title screen.
void simonPlayerFail()
{
    simonFailTimer = simonFailTimer + 1;
    simonSoundPlay = 5;
    if( simonFailTimer <= 1 )
      simonPlaySound();
    simonButtonHide();

    if( simonFailTimer > SIMON_TIMER_MAX )
    {
        simonBeginTitle();
        simonSoundPlay = 0;
    }
}

void simonPlayMelodyTurn()
{
    // MELODY DELAY
    if( simonMelodyTimer == 0 )
    {
        simonSoundPlay = simonMelody[ simonMelodyCurrentNote ];
        simonPlaySound();
        simonButtonHide();
        simonMelodyCurrentNote = simonMelodyCurrentNote + 1;
        simonMelodyTimer = simonMelodyTimer + 1;
    }

    if( simonMelodyTimer > 0 ) // start delay in-between notes
      simonMelodyTimer = simonMelodyTimer + 1;

    if( simonMelodyTimer > SIMON_TIMER_MAX ) // delay ended - more notes to play?
    {
        if( simonMelodyCurrentNote >= simonMelodyStep ) // no more notes - player's turn
        {
            simonMelodyCurrentNote = 0;
            simonMelodyTimer = 0;
            simonButtonsShow();
            simonMelodyPlaying = false;
        }
        else // more notes to play until currentnote hits the step limit
        {
            simonMelodyTimer = 0;
            simonButtonsShow();
        }
    }
}

void simonPlayerTurn()
{
    // KEYS
    if( simonWaitTimer == 0 )
    {
        if( gbPressed( BTN_UP ) )
        {
            simonWaitTimer = 1;
            simonSoundPlay = 1;
            simonPlaySound();
        }
        if( gbPressed( BTN_RIGHT ) )
        {
            simonWaitTimer = 1;
            simonSoundPlay = 2;
            simonPlaySound();
        }
        if( gbPressed( BTN_DOWN ) )
        {
            simonWaitTimer = 1;
            simonSoundPlay = 3;
            simonPlaySound();
        }
        if( gbPressed( BTN_LEFT ) )
        {
            simonWaitTimer = 1;
            simonSoundPlay = 4;
            simonPlaySound();
        }
    }

    // DELAY after the player has played a note
    if( simonWaitTimer > 0 )
    {
        simonWaitTimer = simonWaitTimer + 1;
        simonButtonHide();

        if( simonWaitTimer > ( SIMON_TIMER_MAX / 2 ) ) // delay ended
        {
            simonButtonsShow();

            if( simonSoundPlay != simonMelody[ simonMelodyCurrentNote ] ) // FAILED
            {
                simonPlayerFail();
            }
            else // WIN this note - more notes to match, or end of round?
            {
                simonMelodyCurrentNote = simonMelodyCurrentNote + 1;
                simonMelodyTimer = 1; // relaunch the melody
                simonWaitTimer = 0;

                if( simonMelodyCurrentNote >= simonMelodyStep ) // no more notes - CPU's turn
                {
                    simonMelodyCurrentNote = 0;
                    simonMelodyTimer = 0;
                    simonButtonsShow();
                    simonMelodyPlaying = true;
                    simonMelodyStep = simonMelodyStep + 1;
                    simonDelayTimer = 1; // start the delay before the CPU plays its turn
                }
            }
        }
    }
}

// Runs once, on the exact A-press that dismisses the title screen - see
// this file's own header comment, point 2, for why the melody reseed and
// full variable reset both live here rather than in simonBeginTitle().
void simonBeginPlay()
{
    gbPickRandomSeed(); // random notes each game (no-op stub - see gamebuinoShim.h)

    // See this file's own header comment for why this loop's own bound
    // differs from upstream's literal (buggy) source.
    int i;
    for( i = 0; i < ( SIMON_NOTESMAX - 2 ); i++ )
      simonMelody[ i ] = arand( SIMON_NBNOTES ) + 1;

    simonGameStart = false;
    simonDisplayTop = true;
    simonDisplayRight = true;
    simonDisplayBottom = true;
    simonDisplayLeft = true;
    simonMelodyPlaying = true;
    simonFailTimer = 0;
    simonWaitTimerStart = 0;
    simonWaitTimer = 0;
    simonSoundPlay = 0;
    simonMelodyCurrentNote = 0;
    simonMelodyStep = 1;
    simonMelodyTimer = 0;
    simonDelayTimer = 0;

    simonState = SIMON_STATE_PLAY;
}

void simonUpdateTitle()
{
    // Real title logo, restored at upstream's own real position - see this
    // file's own header comment's "Real bitmap art restored" paragraph for
    // how `(0, 12)` was derived from the real `Gamebuino::titleScreen()`
    // source. The logo itself already spells out the game's own name, so
    // the old plain-text "SIMONBUINO" line is gone; "PRESS A" moves above
    // the logo, the only gap it actually leaves open.
    gbSetColor( 1 );
    gbDrawBitmap( 0, 12, simonTitleBitmap );

    gbFontSize = 1;
    gbCursorX = 14;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      simonBeginPlay();
}

void simonUpdatePlay()
{
    // RESET KEY - matches upstream's own key_reset(): always sends the
    // player back to a fresh title screen (see this file's own header
    // comment, point 2), never resets in place.
    if( gbPressed( BTN_C ) )
    {
        simonBeginTitle();
        return;
    }

    // START delay - a brief pause right after the title screen is
    // dismissed, before the very first CPU turn begins.
    if( simonGameStart == false )
    {
        simonWaitTimerStart = simonWaitTimerStart + 1;
        if( simonWaitTimerStart > SIMON_TIMER_MAX )
        {
            simonMelodyCurrentNote = 0; // reinitialize at the start - very important!
            simonGameStart = true;
        }
    }

    // RECORD achieved - direct port of upstream's own real message now
    // that gbSetFont()/real fonts (and gbPrintString()'s own real '\n'
    // handling) make it practical - see simonWinMessage's own header
    // comment. No separate "PRESS C" hint is drawn: the real message
    // itself already spells out the reset control via its own icon glyph,
    // and this function's own real key_reset() equivalent (the `gbPressed(
    // BTN_C)` check above, unconditional every frame) already works from
    // this screen exactly like upstream's own "cancel at any time" design.
    if( simonMelodyStep >= ( SIMON_NOTESMAX - 1 ) )
    {
        gbSetColor( 1 );
        gbDrawBitmap( 34, 16, simonSmileyBitmap );

        gbFontSize = 1;
        gbSetFont( gbFont3x5 );
        gbCursorX = 0;
        gbCursorY = 0;
        gbPrintString( simonWinMessage );
        return;
    }

    // Real play-screen background frame, restored at upstream's own real
    // position (see header comment) - drawn first so the pad bitmaps below
    // land on top of it, exactly matching upstream's own real draw order.
    gbSetColor( 1 );
    gbDrawBitmap( 10, 0, simonFrameBitmap );

    // PLAYING TURNS
    if( ( simonDelayTimer == 0 ) && ( simonGameStart == true ) )
    {
        if( simonMelodyPlaying )
          simonPlayMelodyTurn();
        else
          simonPlayerTurn();
    }

    // delay after the player's turn
    if( simonDelayTimer > 0 )
    {
        simonDelayTimer = simonDelayTimer + 1;
        if( simonDelayTimer > ( SIMON_TIMER_MAX / 2 ) )
          simonDelayTimer = 0;
    }

    simonDrawButtons();
}

void gameSimonbuino_init()
{
    gbBegin();
    simonBeginTitle();
}

void gameSimonbuino_update()
{
    if( !gbUpdate() ) return;

    if( simonState == SIMON_STATE_TITLE ) simonUpdateTitle();
    else simonUpdatePlay();

    gbRenderFrame();
}
