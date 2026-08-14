#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Kill Race (Yoda Zhang / "yodasvideoarcade", License: none specified -
// yodasvideoarcade.com/gamebuino.php). A top-down driving game: steer a
// little car around an 84x48 lot running over pedestrians ("men") to score
// points and clear each level, avoiding the tombstones their corpses leave
// behind (hitting one crashes the car and costs a life). The first of this
// author's five "yoda-*" games sharing one file-split convention
// (killrace/lander/invaders/asterocks/paqman) - this file only covers
// killrace itself.
//
// Upstream splits one Arduino sketch across six `.ino` tabs compiled as a
// single translation unit (killrace.ino/standard.ino/specific.ino/
// nonstandard.ino/images.ino/sounds.ino) - flattened here into this one
// file, matching how every other multi-tab-`.ino` upstream in this project
// has been ported. Every real `gb.x.y(...)` call site was mechanically
// rewritten to a plain `gbY(...)` function call (see gamePong.c's own
// header comment for why - this dialect has no classes/methods).
// Upstream's own `and`/`or` (C++'s iso646 alternative tokens for `&&`/`||`,
// used throughout checkbuttons()) were rewritten to `&&`/`||` - a pure
// syntax substitution, not a behavior change, since this dialect's own
// documented operator set (see VIRCON32_C_DIALECT.md) makes no mention of
// supporting the alternative spellings the way real g++ does.
// `random(N)`/`random(a,b)` became `arand(N)`/`a+arand(b-a)` (this
// dialect's own established RNG helper). `gb.pickRandomSeed()` was dropped
// outright (this port never calls `gbPickRandomSeed()` either - upstream's
// own call site is in the boot splash setup this port folds away, see
// below). `gb.battery.show=false;` was dropped outright (purely cosmetic
// on real hardware, no equivalent needed here). Every global gets a
// `kill`-prefixed name (checked against every other game already in this
// cartridge's single flat namespace - not taken).
//
// STATE MACHINE: upstream's own `gamestatus` is a real Arduino `String`,
// compared throughout via `=="newgame"`/`=="running"`/etc (a real
// content-comparison operator overload C++'s String class provides - not
// available in this classless dialect). Replaced with a plain `int
// killState` enum (KILL_STATE_TITLE/RUNNING/GAMEOVER) compared via `==`
// exactly like upstream's own String comparisons read.
//
// A GENUINE UPSTREAM STRUCTURAL QUIRK, PRESERVED VIA A DELIBERATE
// SIMPLIFICATION: upstream's own loop() body is a flat run of SEPARATE
// `if (gamestatus==X) { ... }` statements (not an else-if chain), executed
// in this fixed order every tick: newgame, newlevel, newlife, running,
// title, gameover. Because these are independent ifs, not mutually
// exclusive branches, a status change made by an earlier block (e.g.
// newgame() setting gamestatus="newlevel") is immediately visible to a
// LATER if-check in the very same tick - so pressing "PLAY" on the title
// screen (which sets gamestatus="newgame" at the very end of that same
// tick's title block) actually cascades newgame()->newlevel()->newlife()
// ALL the way into a fully-reset "running" state within a single
// subsequent tick, without ever executing an intervening blank frame.
// This port reproduces the identical net effect directly and more
// simply: killNewGame() calls killNewLevel() calls killNewLife() as a
// plain synchronous function-call chain, ending by setting
// killState=KILL_STATE_RUNNING - the same one-tick (at most) resolution
// upstream's own cascade achieves, without needing to replicate the
// separate-if/shared-tick mechanics that produce it. The same treatment is
// used wherever upstream defers a transition via a status string that
// would only get picked up by an if-check appearing EARLIER than the
// point it was set from (handledeath()'s own "newlife" hand-off, and
// nextlevelcheck()'s own "newlevel" hand-off) - both call the target
// transition function directly instead.
//
// A GENUINE UPSTREAM BUG, FOUND AND PRESERVED: the exact same
// separate-if/shared-tick quirk above means `handledeath()` is actually
// invoked TWICE on every tick the car is mid-crash-animation: once from
// inside movecar()'s own `else` branch (drawing the crash sprite), and
// again, unconditionally, later in the very same running block's own
// tail call. Each invocation decrements `deadcounter` by 1 and redraws
// the "N CARS LEFT" box - so the crash animation upstream clearly sized
// for 50 ticks (`deadcounter=50` on collision) actually always resolves
// in half that many real ticks (25) on real hardware, not the 50 the
// constant alone suggests. Reproduced verbatim here (killMoveCar() calls
// killHandleDeath() once internally when killDeadCounter != -1, and
// killUpdateRunning() calls it again unconditionally afterward) rather
// than "fixed" into a single call - this project's own established norm
// is to preserve real upstream behavior/bugs by default. (The second call
// simply becomes a no-op once the first has already reset
// killDeadCounter back to -1 within the same tick, so no double
// life-loss/double-newlife can ever happen from this - the discrepancy is
// purely a shorter-than-intended crash animation.)
//
// BYTE-WRAPAROUND TRICKS, TRANSLATED TO DIRECT SIGNED CHECKS: several of
// upstream's own screen-edge/array-recycle checks are written as
// `if (x>200) { x=0; }` immediately followed by `if (x>76) { x=76; }` -
// exploiting real AVR `byte`'s (unsigned 8-bit) wraparound: decrementing a
// byte already at 0 produces 255, which the ">200" check catches and
// resets before the value is ever read or drawn. This shim's own
// avrCompat.h aliases every AVR fixed-width type (including `byte`) to a
// plain, full-range, non-wrapping `int` (see that file's own header
// comment) - so a literal port of these checks would never fire, letting
// the car/pedestrians drift off-screen indefinitely instead of snapping
// back. Checked case by case and translated to the DIRECT equivalent that
// produces the exact same real observable result on real hardware (since
// the wrapped value is always caught and corrected before ever being
// drawn or read elsewhere - never a visible byte-255 flash):
//   - killCarX/killCarY (killMoveCar()): `carx>200`/`cary>200`
//     (catches going below 0) -> `killCarX < 0`/`killCarY < 0`, both
//     clamping to 0 - a real clamp-to-edge, not a wrap.
//   - killManX (killMoveMen()): `manx[i]>100` (catches going below 0)
//     -> `killManX[i] < 0`, resetting to 80 - a real WRAP to the opposite
//     edge (not a clamp!), confirmed by reading the surrounding logic: a
//     man walking left off-screen without ever having turned back
//     reappears walking in from the right, and `manx[i]>80` (normal
//     rightward overflow) resets to 0 - the opposite edge again. Both
//     directions genuinely wrap on real hardware; only the leftward one
//     needed the byte-overflow trick to express in AVR terms.
//   - killManY (killMoveMen()): `many[i]>100` (catches going below 0)
//     -> `killManY[i] < 0`, resetting to 0 - a real CLAMP this time (0 is
//     both the wrapped-reset value AND the minimum), not a wrap, matching
//     the existing `many[i]>41` high clamp's own symmetric intent.
//   - the dust-trail shift loop in movecar() (`for (i=2; i<255; i--)`)
//     deliberately underflows `i` from 0 to 255 (byte) to end the loop
//     after exactly 3 iterations (i=2,1,0) - translated directly to a
//     natural signed countdown `for (i = 2; i >= 0; i--)` producing the
//     identical 3 iterations without relying on wraparound at all.
//
// A REAL DOUBLE-SCREEN COLLAPSED INTO ONE, DOCUMENTED RATHER THAN GUESSED
// AT: upstream's own setup() shows a real blocking `gb.titleScreen(
// F("    Yoda's"), gamelogo)` splash exactly once at boot (waits for any
// button on real hardware, then sets gamestatus="title"), and separately,
// showtitle() (gamestatus=="title", re-entered on every subsequent title
// visit) draws its OWN custom screen using the very same gamelogo bitmap
// plus a real LAST/HIGH score line and an "A: PLAY C: QUIT" prompt.
// Per this project's own established "blocking titleScreen() -> explicit
// resumable state" treatment (see gamePong.c's own header comment),
// gb.titleScreen()'s own real internal caption-text layout isn't available
// to read from this upstream source at all (it's a library-internal
// function; only the caption string and bitmap were ever passed in) - and
// since the two screens already share the identical real gamelogo bitmap
// and would look almost indistinguishable at a glance, this port folds
// both into ONE KILL_STATE_TITLE (killUpdateTitle(), a direct, complete
// port of showtitle() itself) rather than guessing at a separate one-shot
// splash layout for a screen that would only ever be seen once, for a
// fraction of a second, before falling straight into an almost-identical
// one. showtitle()'s own C-press handler (which just calls
// `gb.titleScreen(...)` again, then falls right back to this exact same
// screen next tick) is likewise a documented no-op here rather than a
// separately-modeled state, for the same reason.
//
// MID-GAME "C RETURNS TO TITLE": checkbuttons()'s own unconditional
// (not deadcounter-gated) BTN_C check calls `gb.titleScreen()` again and
// sets gamestatus="title" - on real hardware, because of the same
// separate-if/shared-tick mechanic described above, gameplay actually
// keeps running for the REST of that exact tick (movecar()/manappear()/
// etc all still execute once more, using the game state exactly as it was
// the instant C was pressed) even though gamestatus already reads
// "title" by the time they run - an artifact of the blocking titleScreen()
// call being interleavable mid-block at all. This port instead adopts the
// same "return immediately, skip the rest of this frame's update" handling
// already established for every other mid-game "back to title" gesture in
// this project (see gamePong.c's/gameFlappyBirdo.c's own BTN_C handling) -
// killCheckButtons() returns `true` on a fresh C-press, and
// killUpdateRunning() returns immediately without running movecar()/
// manappear()/etc for that tick, rather than reproducing a one-tick
// "still moves after quitting" artifact this non-blocking engine has no
// natural way to express anyway (there's no blocking call to interleave
// inside of here).
//
// REAL BITMAP ART RESTORED: every upstream `const byte ...[] PROGMEM`
// sprite table (gamelogo, background, carsprite[12][10], mansprite[5][9])
// was converted byte-for-byte from its real Arduino `B00000000`-style
// binary literals into this dialect's own `int[N] name = { width, height,
// 0x.., ... }` array shape gbDrawBitmap() expects - via a small script
// reading images.ino directly and re-decoding every resulting array's own
// bits back into an ASCII preview before trusting it (not hand-
// transcribed - the same discipline gameFlappyBirdo.c's own header
// comment describes). carsprite[shape]/mansprite[shape] (a real 2D
// PROGMEM table upstream) were kept as INDIVIDUALLY NAMED 1D arrays
// (killCarSprite0..11/killManSprite0..4) plus a small killCarBitmap()/
// killManBitmap() if-chain lookup function returning the matching one,
// rather than a genuine `int[12][10]` 2D array indexed down to a single
// row and handed to gbDrawBitmap() as an `int*` - this project already has
// proven, working precedent for passing a plain named int[] array as an
// `int*` argument (every gbDrawBitmap() call site in every game ported so
// far), but no proven precedent anywhere in this codebase for a partially-
// indexed 2D array row itself decaying the same way, so this port doesn't
// gamble on that being equivalent. No separate mask/fill layer exists
// upstream for any of these sprites (confirmed directly in images.ino -
// every sprite is a single self-contained outline bitmap, unlike the real
// GRAY-body/BLACK-outline layering found and fixed in gameFlappyBirdo.c) -
// so every gbDrawBitmap() call here is a single direct call, no
// background-bleed risk to guard against.
//
// SOUND: upstream's own playsoundfx(fxno,channel) builds a small
// per-channel FX-synth preset each call (waveform/instrument, volume
// slide, pitch/arpeggio slide `gb.sound.command(...)` calls, from the
// real soundfx[5][8] table) layered on top of one
// `gb.sound.playNote(pitch,duration,channel)` call - killPlaySfx(fxno,
// channel) is a real, faithful port of this using this shim's own
// `gbSoundCommand()`/`gbPlayNoteChannel()` primitives (a direct port of
// real Sound.h's own per-channel tracker commands). killSoundFx[5][8] is
// a verbatim copy of upstream's own soundfx[5][8] table (re-verified
// byte-for-byte against sounds.ino), and all 9 real playsoundfx() call
// sites now pass the same real channel number upstream itself uses at
// that exact site (0 or 1).
//
// FONT: upstream's own source never calls `gb.display.setFont(...)`
// anywhere in any of its six .ino files (confirmed directly, not
// assumed) - so this port leaves the shim's own real default font
// (gbFont3x5, already set by gbBegin()) active untouched for the whole
// game, exactly matching real hardware. Every upstream on-screen string
// was measured against this real font's own real per-glyph cell width
// (3+1=4px) and fits the real 84px-wide display without clipping at its
// own real cursor positions unmodified (e.g. the title screen's own
// 21-character " A: PLAY     C: QUIT" line is exactly 84px at cursorX=0).
//
// EEPROM PERSISTENCE ADDED, BEYOND REAL UPSTREAM - no real `EEPROM.read()`/
// `EEPROM.write()` call exists anywhere in upstream's own source -
// `killHighscore` was originally genuine in-session/RAM-only state,
// explicitly reset to 0 by upstream's own real `setup()` every single
// launch. Added directly on request once an audit found this game
// displays a real highscore that never survives a cartridge reboot:
// `gameKillrace_init()`'s own real `killHighscore = 0;` line is now a real
// `eeprom_read_word(0)` load instead (with the same `==0xFFFF`
// fresh-EEPROM-cell reset check already established elsewhere in this
// project, e.g. gameCrabator.c/gameDescent.c, rather than trusting a raw
// 65535 sentinel) - the very same effective "0 on a genuinely fresh card"
// outcome upstream's own hardcoded `=0` produced, just no longer
// discarding a real earlier save. Saved via `eeprom_write_word(0,
// killHighscore)` at the exact point upstream's own real
// highscore-tracking line already updates it in memory - a one-shot
// write per new high score, not a per-frame write.
//
// A KNOWN, UNADDRESSED UPSTREAM LIMIT, LEFT AS-IS: `killMenToKill` grows
// by 2 every level (`6 + gamelevel*2`) with no cap, while
// killManX/Y/Shape/Xr are all fixed at 50 elements - a real out-of-bounds
// array access once a player survives to roughly level 22+. Upstream has
// the exact same real limit (mentokill/manx[50]/etc, unbounded gamelevel)
// - not a porting artifact, and not fixed here either, matching this
// project's own "preserve real upstream behavior/bugs by default" norm;
// realistically never reached given the escalating difficulty long before
// then.

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

enum KillState
{
    KILL_STATE_TITLE = 0,
    KILL_STATE_RUNNING = 1,
    KILL_STATE_GAMEOVER = 2
};

// -----------------------------------------------------------------------------
// Sound - real upstream soundfx[5][8] table, copied verbatim - see header
// comment.
// -----------------------------------------------------------------------------

int killSoundFx[5][8]= {
    { 1, 42, 58, 1, 7, 0, 7, 3 },  // 0 = change direction
    { 1, 0, 0, 0, 0, 0, 7, 3 },    // 1 = drive
    { 1, 58, 50, 1, 0, 0, 7, 5 },  // 2 = run over man
    { 1, 0, 96, 1, 1, 3, 7, 20 },  // 3 = crash into tombstone
    { 0, 0, 19, 1, 1, 4, 7, 20 },  // 4 = next level (all men killed)
};

void killPlaySfx( int fxno, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, killSoundFx[ fxno ][ 6 ], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, killSoundFx[ fxno ][ 0 ], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, killSoundFx[ fxno ][ 5 ], -killSoundFx[ fxno ][ 4 ], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, killSoundFx[ fxno ][ 3 ], killSoundFx[ fxno ][ 2 ] - 58, channel );
    gbPlayNoteChannel( killSoundFx[ fxno ][ 1 ], killSoundFx[ fxno ][ 7 ], channel );
}

// -----------------------------------------------------------------------------
// Real upstream sprite bitmaps - copied byte-for-byte from images.ino's own
// real `const byte NAME[] PROGMEM = { width, height, B..., ... }` arrays
// (see header comment for the conversion/verification method).
// -----------------------------------------------------------------------------

int killGameLogoBitmap[210]= { 64, 26,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x7E, 0x77, 0x77, 0xE7, 0xF8, 0x3C, 0xF, 0x7E, 0xE, 0x77, 0x70, 0xE1, 0xFC, 0x7E, 0x3F, 0x7E,
    0x3E, 0x77, 0x73, 0xEF, 0xFE, 0x7E, 0x3F, 0x7E, 0xE, 0x77, 0x70, 0xE1, 0xCE, 0xFF, 0x7F, 0x7E,
    0x7E, 0xE7, 0x71, 0xE3, 0xCE, 0xE7, 0x78, 0x70, 0xF, 0xCF, 0x70, 0xE1, 0xCE, 0xEF, 0x73, 0xF0,
    0x1F, 0x87, 0x73, 0xE7, 0xDE, 0xE7, 0x70, 0x7C, 0xF, 0x3F, 0x70, 0xE1, 0xFC, 0xFF, 0x71, 0xFC,
    0x7F, 0x87, 0x77, 0xEF, 0xF8, 0xFF, 0x70, 0x7C, 0xF, 0xDF, 0x70, 0xE1, 0xFC, 0xFF, 0x73, 0xF0,
    0x1E, 0xE7, 0x73, 0xE7, 0xDE, 0xE7, 0x70, 0x70, 0xE, 0x77, 0x70, 0xE1, 0xCE, 0xEF, 0x78, 0xF0,
    0x3E, 0x77, 0x7E, 0xFD, 0xDE, 0xE7, 0x7F, 0x7E, 0xE, 0x77, 0x7E, 0xFD, 0xCE, 0xEF, 0x3F, 0x7E,
    0x7E, 0x77, 0x7E, 0xFD, 0xDE, 0xE7, 0x3F, 0x7E, 0xE, 0x77, 0x7E, 0xFD, 0xCE, 0xE7, 0xF, 0x7E,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xA6, 0xC6, 0x6A, 0xEC, 0xE6, 0x6E, 0x66, 0xCE,
    0xAA, 0xAA, 0x8A, 0x4A, 0x8A, 0xAA, 0x8A, 0xA8, 0xAA, 0xAE, 0xEA, 0x4A, 0xCA, 0xEC, 0x8E, 0xAC,
    0x4A, 0xAA, 0x2A, 0x4A, 0x8A, 0xAA, 0x8A, 0xA8, 0x4E, 0xCA, 0xE4, 0xEC, 0xEE, 0xAA, 0xEA, 0xCE,
};

int killBackgroundBitmap[530]= { 84, 48,
    0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0,
    0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0,
    0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x8, 0x0, 0x80, 0x0, 0x0, 0x0, 0x10, 0x1,
    0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x8, 0x0,
    0x80, 0x0, 0x0, 0x0, 0x10, 0x1, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0,
    0x0, 0x0, 0x10, 0x0, 0x8, 0x0, 0x80, 0x0, 0x0, 0x0, 0x10, 0x1, 0x0, 0x0, 0x80, 0x0,
    0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1,
    0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x8, 0x0, 0x80, 0x8, 0x1, 0x0, 0x10, 0x1, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x8, 0x0, 0x80, 0x8, 0x1, 0x0, 0x10, 0x1, 0x0,
    0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x8, 0x0, 0x80,
    0x8, 0x1, 0x0, 0x10, 0x1, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0,
    0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0,
    0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x8, 0x0, 0x80, 0x0, 0x0, 0x0,
    0x10, 0x1, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0,
    0x8, 0x0, 0x80, 0x0, 0x0, 0x0, 0x10, 0x1, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1,
    0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x8, 0x0, 0x80, 0x0, 0x0, 0x0, 0x10, 0x1, 0x0, 0x0,
    0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0,
    0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0,
    0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x80, 0x8, 0x0, 0x80, 0x8, 0x1, 0x0, 0x10, 0x1, 0x0, 0x10, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x8, 0x0, 0x80, 0x8, 0x1, 0x0, 0x10,
    0x1, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x8,
    0x0, 0x80, 0x8, 0x1, 0x0, 0x10, 0x1, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x0, 0x0, 0x0, 0x10, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0, 0x0, 0x8, 0x1,
    0x0, 0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

// carshape 0=left,1=up,2=right,3=down; 4-7=dust-trail puffs (drawn behind
// the car at its last few positions); 8-11=crash sprites (carshape+8).
int killCarSprite0[10]= { 8, 8, 0x77, 0x22, 0x7F, 0xCE, 0xCE, 0x7F, 0x22, 0x77 };
int killCarSprite1[10]= { 8, 8, 0x18, 0xBD, 0xE7, 0xA5, 0x3C, 0xBD, 0xFF, 0xA5 };
int killCarSprite2[10]= { 8, 8, 0xEE, 0x44, 0xFE, 0x73, 0x73, 0xFE, 0x44, 0xEE };
int killCarSprite3[10]= { 8, 8, 0xA5, 0xFF, 0xBD, 0x3C, 0xA5, 0xE7, 0xBD, 0x18 };
int killCarSprite4[10]= { 8, 8, 0x34, 0x4A, 0x91, 0x45, 0xA2, 0x89, 0x52, 0x2C };
int killCarSprite5[10]= { 8, 8, 0x89, 0x54, 0x12, 0x69, 0x96, 0x48, 0x2A, 0x91 };
int killCarSprite6[10]= { 8, 8, 0x84, 0x22, 0x65, 0x0, 0x0, 0xA6, 0x44, 0x21 };
int killCarSprite7[10]= { 8, 8, 0x42, 0x81, 0x4, 0x0, 0x0, 0x20, 0x81, 0x42 };
int killCarSprite8[10]= { 8, 8, 0xD8, 0x6C, 0x78, 0xDC, 0xD0, 0x78, 0xAC, 0x50 };
int killCarSprite9[10]= { 8, 8, 0x59, 0xBF, 0x66, 0xBD, 0x6F, 0x4A, 0x0, 0x0 };
int killCarSprite10[10]= { 8, 8, 0xA, 0x35, 0x1E, 0xB, 0x3B, 0x1E, 0x36, 0x1B };
int killCarSprite11[10]= { 8, 8, 0x0, 0x0, 0x52, 0xF6, 0xBD, 0x66, 0xFD, 0x9A };

// manshape 0=walking right, 1=(right, other anim frame), 2=walking left,
// 3=(left, other anim frame), 4=tombstone (run over).
int killManSprite0[9]= { 5, 7, 0x60, 0x60, 0x40, 0x70, 0xC0, 0xA0, 0xB0 };
int killManSprite1[9]= { 5, 7, 0x60, 0x60, 0x40, 0x70, 0x40, 0x40, 0x60 };
int killManSprite2[9]= { 5, 7, 0x60, 0x60, 0x20, 0xE0, 0x30, 0x50, 0xD0 };
int killManSprite3[9]= { 5, 7, 0x60, 0x60, 0x20, 0xE0, 0x20, 0x20, 0x60 };
int killManSprite4[9]= { 5, 7, 0x70, 0xD8, 0x88, 0xD8, 0xD8, 0xD8, 0xF8 };

// See header comment for why these are individually-named 1D arrays plus
// an if-chain lookup, instead of a genuine `int[12][10]`/`int[5][9]` 2D
// array indexed down to one row and handed to gbDrawBitmap() as an int*.
int* killCarBitmap( int shape )
{
    if( shape == 0 ) return killCarSprite0;
    if( shape == 1 ) return killCarSprite1;
    if( shape == 2 ) return killCarSprite2;
    if( shape == 3 ) return killCarSprite3;
    if( shape == 4 ) return killCarSprite4;
    if( shape == 5 ) return killCarSprite5;
    if( shape == 6 ) return killCarSprite6;
    if( shape == 7 ) return killCarSprite7;
    if( shape == 8 ) return killCarSprite8;
    if( shape == 9 ) return killCarSprite9;
    if( shape == 10 ) return killCarSprite10;
    return killCarSprite11;
}

int* killManBitmap( int shape )
{
    if( shape == 0 ) return killManSprite0;
    if( shape == 1 ) return killManSprite1;
    if( shape == 2 ) return killManSprite2;
    if( shape == 3 ) return killManSprite3;
    return killManSprite4;
}

// -----------------------------------------------------------------------------
// Game state (real upstream globals, `kill`-prefixed)
// -----------------------------------------------------------------------------

int killState;

int killScore;
int killHighscore;
int killLives;
int killLevel;

int killCarX;
int killCarY;
int killCarXr;
int killCarYr;
int killCarShape;

int killDustX[4];
int killDustY[4];
int killDustCounter;

int killManX[50];
int killManY[50];
int killManShape[50]; // 0/2=alive walking, 4=tombstone, 10=not on screen
int killManXr[50];

int killManFrame;
int killManFrameCounter;
int killManAppearCounter;
int killManCounter;
int killMenToKill;
int killMenKilled;
int killManFrameMax;

int killYeahTimer;
int killDeadCounter;

// -----------------------------------------------------------------------------
// Small helpers - real upstream cursorX offsets shift left by 2px per extra
// score digit (`-2*(score>9)-2*(score>99)-...`); no bool-in-arithmetic trick
// used here (unconfirmed in this dialect - see gamebuinoShim.h's own header
// comment on there being no ternary operator either), just a plain if-chain
// producing the identical offset.
// -----------------------------------------------------------------------------

int killDigitOffset3( int v ) // showtitle()'s own LAST/HIGH fields (3 thresholds)
{
    int n = 0;
    if( v > 9 ) n = n + 1;
    if( v > 99 ) n = n + 1;
    if( v > 999 ) n = n + 1;
    return n * 2;
}

int killDigitOffset4( int v ) // showscore()'s own in-game score readout (4 thresholds)
{
    int n = 0;
    if( v > 9 ) n = n + 1;
    if( v > 99 ) n = n + 1;
    if( v > 999 ) n = n + 1;
    if( v > 9999 ) n = n + 1;
    return n * 2;
}

// -----------------------------------------------------------------------------
// State transitions - see header comment on the newgame/newlevel/newlife
// cascade simplification.
// -----------------------------------------------------------------------------

void killBeginTitle()
{
    killState = KILL_STATE_TITLE;
}

void killNewLife()
{
    int j;

    killCarX = 38;
    killCarY = 22;
    killCarXr = 0;
    killCarYr = 0;
    killCarShape = 1;
    killDeadCounter = -1;

    for( j = 0; j < 4; j++ )
    {
        killDustX[ j ] = 100;
        killDustY[ j ] = 0;
    }

    killState = KILL_STATE_RUNNING;
}

void killNewLevel()
{
    int m;

    for( m = 0; m < 50; m++ )
      killManShape[ m ] = 10;

    killMenToKill = 6 + killLevel * 2;
    killMenKilled = 0;
    killManAppearCounter = 20;
    killYeahTimer = 0;
    killManCounter = 0;
    killManFrameMax = 6 - killLevel;
    if( killManFrameMax < 1 ) killManFrameMax = 1;

    killNewLife();
}

void killNewGame()
{
    killScore = 0;
    killLives = 3;
    killLevel = 0;

    killNewLevel();
}

// -----------------------------------------------------------------------------
// Running - gameplay logic + drawing, called every tick while
// killState == KILL_STATE_RUNNING (see killUpdateRunning() at the bottom).
// -----------------------------------------------------------------------------

// == upstream checkbuttons(). Returns true if BTN_C was freshly pressed
// (mid-game "back to title") - see header comment on why this port returns
// immediately instead of literally finishing the rest of that tick's
// gameplay update the way real hardware's own blocking titleScreen() call
// happens to.
bool killCheckButtons()
{
    int changed = 0;

    if( gbPressed( BTN_LEFT ) && killDeadCounter == -1 )
    {
        if( killCarXr != -2 )
        {
            killPlaySfx( 0, 0 );
            changed = 1;
        }
        killCarXr = -2;
        killCarYr = 0;
        killCarShape = 0;
    }
    if( gbPressed( BTN_RIGHT ) && killDeadCounter == -1 )
    {
        if( killCarXr != 2 )
        {
            killPlaySfx( 0, 0 );
            changed = 1;
        }
        killCarXr = 2;
        killCarYr = 0;
        killCarShape = 2;
    }
    if( gbPressed( BTN_UP ) && killDeadCounter == -1 )
    {
        if( killCarYr != -2 )
        {
            killPlaySfx( 0, 0 );
            changed = 1;
        }
        killCarXr = 0;
        killCarYr = -2;
        killCarShape = 1;
    }
    if( gbPressed( BTN_DOWN ) && killDeadCounter == -1 )
    {
        if( killCarYr != 2 )
        {
            killPlaySfx( 0, 0 );
            changed = 1;
        }
        killCarXr = 0;
        killCarYr = 2;
        killCarShape = 3;
    }
    if( gbPressed( BTN_A ) && killDeadCounter == -1 )
    {
        if( killCarXr != 0 || killCarYr != 0 )
        {
            killPlaySfx( 0, 0 );
            killScore = killScore - 1;
            if( killScore < 0 ) killScore = 0;
        }
        killCarXr = 0;
        killCarYr = 0;
    }
    if( gbPressed( BTN_C ) )
    {
        killBeginTitle();
        return true;
    }
    if( changed == 0 && killDeadCounter == -1 && killYeahTimer == 0 && ( killCarXr != 0 || killCarYr != 0 ) )
      killPlaySfx( 1, 0 );

    return false;
}

// == upstream handledeath(). See header comment on the real double-call-
// per-tick upstream bug this reproduces verbatim.
void killHandleDeath()
{
    if( killDeadCounter != -1 )
    {
        killDeadCounter = killDeadCounter - 1;

        gbSetColor( 0 );
        gbFillRect( 19, 19, 46, 8 );
        gbSetColor( 1 );
        gbCursorX = 20;
        gbCursorY = 20;
        gbPrintNumber( killLives - 1 );
        gbCursorX = 28;
        gbPrintString( "CARS LEFT" );

        if( killDeadCounter == 0 )
        {
            killDeadCounter = -1;
            killLives = killLives - 1;

            if( killLives == 0 )
              killState = KILL_STATE_GAMEOVER;
            else
              killNewLife();
        }
    }
}

// == upstream movecar(). See header comment on the byte-wraparound ->
// direct-signed-check translation for the edge clamps and the dust-trail
// shift loop.
void killMoveCar()
{
    int i;

    gbSetColor( 1 );
    gbDrawBitmap( 0, 0, killBackgroundBitmap );

    killDustCounter = ( killDustCounter + 1 ) % 4;
    if( killDustCounter == 0 )
    {
        for( i = 2; i >= 0; i-- )
        {
            killDustX[ i + 1 ] = killDustX[ i ];
            killDustY[ i + 1 ] = killDustY[ i ];
        }
        killDustX[ 0 ] = killCarX;
        killDustY[ 0 ] = killCarY;
    }

    if( killDeadCounter == -1 && killYeahTimer == 0 )
    {
        killCarX = killCarX + killCarXr;
        killCarY = killCarY + killCarYr;

        if( killCarX < 0 ) { killCarX = 0; killCarXr = 0; }
        if( killCarX > 76 ) { killCarX = 76; killCarXr = 0; } // 76 = LCDWIDTH-8
        if( killCarY < 0 ) { killCarY = 0; killCarYr = 0; }
        if( killCarY > 40 ) { killCarY = 40; killCarYr = 0; } // 40 = LCDHEIGHT-8
    }

    if( killDeadCounter == -1 )
      gbDrawBitmap( killCarX, killCarY, killCarBitmap( killCarShape ) );
    else
    {
        gbDrawBitmap( killCarX, killCarY, killCarBitmap( killCarShape + 8 ) );
        killHandleDeath(); // real upstream double-call - see header comment
    }

    for( i = 0; i < 4; i++ )
    {
        if( killDustX[ i ] != killCarX || killDustY[ i ] != killCarY )
          gbDrawBitmap( killDustX[ i ], killDustY[ i ], killCarBitmap( i + 4 ) );
    }
}

// == upstream manappear().
void killManAppear()
{
    killManAppearCounter = killManAppearCounter - 1;
    if( killManAppearCounter == 0 && killManCounter < killMenToKill )
    {
        killManAppearCounter = 80 - killLevel * 10;
        if( killManAppearCounter < 10 ) killManAppearCounter = 10;

        killManX[ killManCounter ] = arand( 2 ) * 80;
        killManY[ killManCounter ] = arand( 42 );
        killManXr[ killManCounter ] = 1;
        killManShape[ killManCounter ] = 0;

        if( killManX[ killManCounter ] == 80 )
        {
            killManXr[ killManCounter ] = -1;
            killManShape[ killManCounter ] = 2;
        }

        killManCounter = killManCounter + 1;
    }
}

// == upstream movemen(). See header comment on the wrap-vs-clamp
// byte-wraparound translation for killManX/killManY.
void killMoveMen()
{
    int i;

    killManFrameCounter = ( killManFrameCounter + 1 ) % killManFrameMax;
    if( killManFrameCounter == 0 )
    {
        killManFrame = ( killManFrame + 1 ) % 2;

        for( i = 0; i <= killMenToKill; i++ )
        {
            // change direction near the car?
            if( killManXr[ i ] == -1 && killManX[ i ] - 4 < killCarX + 7 && killManX[ i ] > killCarX + 3 && killManShape[ i ] == 2 )
            {
                killManXr[ i ] = 1;
                killManShape[ i ] = 0;
            }
            if( killManXr[ i ] == 1 && killManX[ i ] + 8 > killCarX && killManX[ i ] < killCarX && killManShape[ i ] == 0 )
            {
                killManXr[ i ] = -1;
                killManShape[ i ] = 2;
            }
            // move
            if( killManShape[ i ] != 10 && killManShape[ i ] != 4 )
            {
                killManX[ i ] = killManX[ i ] + killManXr[ i ];
                killManY[ i ] = killManY[ i ] + arand( 3 ) - 1;

                if( killManX[ i ] < 0 ) killManX[ i ] = 80;  // wraps to the opposite edge
                if( killManX[ i ] > 80 ) killManX[ i ] = 0;  // wraps to the opposite edge
                if( killManY[ i ] < 0 ) killManY[ i ] = 0;   // clamps
                if( killManY[ i ] > 41 ) killManY[ i ] = 41; // clamps
            }
        }
    }

    gbSetColor( 1 );
    for( i = 0; i <= killMenToKill; i++ )
    {
        if( killManShape[ i ] != 10 )
        {
            if( killManShape[ i ] == 4 )
              gbDrawBitmap( killManX[ i ], killManY[ i ], killManBitmap( 4 ) );
            else
              gbDrawBitmap( killManX[ i ], killManY[ i ], killManBitmap( killManShape[ i ] + killManFrame ) );
        }
    }
}

// == upstream checkcollission().
void killCheckCollision()
{
    if( killDeadCounter == -1 )
    {
        int i;

        for( i = 0; i <= killMenToKill; i++ )
        {
            if( killManShape[ i ] == 4 )
            {
                // collision with a tombstone
                int colFlag = 0;
                if( killCarXr < 0 && killManX[ i ] + 5 >= killCarX && killManX[ i ] + 3 < killCarX && killManY[ i ] + 6 >= killCarY && killManY[ i ] <= killCarY + 7 ) colFlag = 1;
                if( killCarXr > 0 && killManX[ i ] - 1 <= killCarX + 7 && killManX[ i ] + 1 > killCarX + 7 && killManY[ i ] + 6 >= killCarY && killManY[ i ] <= killCarY + 7 ) colFlag = 1;
                if( killCarYr < 0 && killManY[ i ] + 7 >= killCarY && killManY[ i ] + 5 < killCarY && killManX[ i ] + 4 >= killCarX && killManX[ i ] <= killCarX + 7 ) colFlag = 1;
                if( killCarYr > 0 && killManY[ i ] - 1 <= killCarY + 7 && killManY[ i ] + 1 > killCarY + 7 && killManX[ i ] + 4 >= killCarX && killManX[ i ] <= killCarX + 7 ) colFlag = 1;

                if( colFlag == 1 )
                {
                    killPlaySfx( 3, 1 );
                    killDeadCounter = 50;
                    killManShape[ i ] = 10;
                }
            }
            if( killManShape[ i ] == 0 || killManShape[ i ] == 2 )
            {
                // collision with a live man
                if( killCarX <= killManX[ i ] && killCarX + 7 >= killManX[ i ] + 3 && killCarY <= killManY[ i ] && killCarY + 7 >= killManY[ i ] + 6 )
                {
                    killManShape[ i ] = 4;
                    killScore = killScore + 25;
                    killMenKilled = killMenKilled + 1;

                    if( killMenKilled == killMenToKill )
                      killPlaySfx( 4, 1 );
                    else
                      killPlaySfx( 2, 1 );
                }
            }
        }
    }
}

// == upstream nextlevelcheck().
void killNextLevelCheck()
{
    if( killMenToKill == killMenKilled )
    {
        killYeahTimer = killYeahTimer + 1;

        gbSetColor( 0 );
        gbFillRect( 5, 17, 74, 14 );
        gbSetColor( 1 );
        gbCursorX = 6;
        gbCursorY = 18;
        gbPrintString( "READY FOR LEVEL" );
        gbCursorX = 70;
        gbPrintNumber( killLevel + 2 );
        gbCursorX = 26;
        gbCursorY = 24;
        gbPrintString( "KILL" );
        gbCursorX = 48;
        gbPrintNumber( killMenToKill + 2 );

        if( killYeahTimer >= 50 )
        {
            killLevel = killLevel + 1;
            killNewLevel();
        }
    }
}

// == upstream showscore().
void killShowScore()
{
    if( killCarY > 4 )
    {
        gbCursorY = 0;
        gbCursorX = 40 - killDigitOffset4( killScore );
        gbPrintNumber( killScore );
    }
}

void killUpdateRunning()
{
    if( killCheckButtons() ) return; // fresh BTN_C - see header comment

    killMoveCar();
    killManAppear();
    killMoveMen();
    killCheckCollision();
    killNextLevelCheck();
    killHandleDeath(); // real upstream double-call - see header comment
    killShowScore();
}

// -----------------------------------------------------------------------------
// Title / game over
// -----------------------------------------------------------------------------

// == upstream showtitle() (also stands in for upstream's own one-shot boot
// splash - see header comment on why the two are collapsed into one).
void killUpdateTitle()
{
    if( killScore > killHighscore )
    {
        killHighscore = killScore;
        eeprom_write_word( 0, killHighscore );
    }

    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "  LAST         HIGH" );

    gbCursorX = 14 - killDigitOffset3( killScore );
    gbCursorY = 6;
    gbPrintNumber( killScore );

    gbCursorX = 66 - killDigitOffset3( killHighscore );
    gbCursorY = 6;
    gbPrintNumber( killHighscore );

    gbDrawBitmap( 10, 13, killGameLogoBitmap );

    gbCursorX = 0;
    gbCursorY = 42;
    gbPrintString( " A: PLAY     C: QUIT" );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        killNewGame();
    }
    // BTN_C here is a documented no-op - see header comment.
}

// == upstream showgameover().
void killUpdateGameOver()
{
    gbSetColor( 0 );
    gbFillRect( 22, 16, 39, 9 );
    gbSetColor( 1 );
    gbCursorX = 24;
    gbCursorY = 18;
    gbPrintString( "GAME OVER" );
    gbDrawRect( 22, 16, 39, 9 );
    gbCursorX = 4;
    gbCursorY = 42;
    gbPrintString( "PRESS B TO CONTINUE" );

    if( gbPressed( BTN_B ) )
    {
        killBeginTitle();
        gbPlayOK();
    }
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameKillrace_init()
{
    gbBegin();
    gbSetFrameRate( 25 ); // real upstream gb.setFrameRate(25) call in setup()

    killScore = 0;
    killHighscore = eeprom_read_word( 0 );
    if( killHighscore == 0xFFFF ) killHighscore = 0;

    killBeginTitle();
}

void gameKillrace_update()
{
    if( !gbUpdate() ) return;

    if( killState == KILL_STATE_TITLE ) killUpdateTitle();
    else if( killState == KILL_STATE_RUNNING ) killUpdateRunning();
    else killUpdateGameOver();

    gbRenderFrame();
}
