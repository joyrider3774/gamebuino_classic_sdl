#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// FlappyBirdo (Forklift5 [Jerom, 2015, with help from Skyrunner65 and
// clement; SFX built with yodasvideoarcade's own "FX Synth" tool], License:
// None specified - github.com/Forklift5/FlappyBirdo). A real Flappy Bird
// clone for Gamebuino Classic: tap to flap a bird through a scrolling field
// of pipes, pick one of three difficulties (SLOW/NORMAL/FAST) from a menu
// first, each tracking its own in-session highscore.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(a,b)` became `a + arand(b-a)`
// (this dialect's own established RNG helper).
//
// REAL BITMAP ART RESTORED (this file's own earlier pass, before
// `gbDrawBitmap()` existed, dropped every upstream sprite in favor of
// plain-shape approximations - that pass's own writeup is gone now,
// superseded by this one). Every real `const byte NAME[] PROGMEM = {...}`
// array upstream (bird1Bitmap, bird2Bitmap, cityBitmap, pipeBitmap,
// skyBitmap, gameoverBitmap, trophyBitmap, titleBitmap) was copied
// byte-for-byte (via a small script reading the real .ino source directly
// and verified by re-decoding each array's own bits back into an ASCII
// preview before trusting it - not hand-transcribed) into a plain
// `int[N] flapXxxBitmap = { width, height, byte0, byte1, ... }` array
// below (this dialect's own `int[N] name` array-declaration order, not
// C's `int name[N]` - see gameConduit.c's own tile arrays for the same
// established precedent), exactly the format `gbDrawBitmap()`/
// `gbDrawBitmapRotated()` expect - no conversion needed, confirmed
// already valid in this dialect.
// Every real `gb.display.drawBitmap(...)` call site now has a direct
// `gbDrawBitmap()`/`gbDrawBitmapRotated()` counterpart at upstream's own
// exact real coordinates:
//   - bird1Bitmap/bird2Bitmap (the player) -> flapDrawPlayerAlive()
//     alternates them exactly like upstream's own drawPlayerAlive() (bird1
//     when animation>2, else bird2); flapDrawPlayerEnd() draws bird1Bitmap
//     ROTCW (rotation=3, matching upstream's own real
//     `drawBitmap(...,bird1Bitmap,ROTCW,NOFLIP)` call in drawPlayerEnd()).
//     A SECOND small real call site restored as a bonus (not originally
//     asked for, but a genuine `drawBitmap(bird1/2Bitmap,...)` call in
//     upstream's own Menu.ino): the difficulty menu's own tiny animated
//     bird "cursor" icon next to the selected line, in flapUpdateMenu().
//   - pipeBitmap -> flapDrawPipes() draws real upstream's own full 3-layer
//     pipe exactly: a GRAY body fill, then WHITE highlight strips (a 1px
//     line plus a 2px vertical strip near each pipe's own gap-facing
//     opening edge), then the BLACK outline bitmap on top (drawn directly
//     for the bottom pipe, via `gbDrawBitmapRotated(...,NOROT,FLIPV)` for
//     the top pipe) - matching upstream's own real `setColor(GRAY);
//     fillRect(...); setColor(WHITE); fillRect(...); setColor(BLACK);
//     drawBitmap(pipe[a].x, ..., pipeBitmap, NOROT, FLIPV);` sequence
//     exactly.
//   - cityBitmap/skyBitmap (the tiled background) -> flapDrawBackground()
//     draws a real GRAY top-of-sky band first, then tiles skyMaskBitmap
//     (WHITE, punching a cloud-shaped hole through the gray band),
//     skyBitmap (BLACK outline), and cityBitmap (GRAY silhouette) across
//     the real 84px-wide screen every 16px (upstream's own real per-tile
//     anchor, `(i*16, 13)` for clouds/`(i*16, 23)` for city - its own loop
//     bound of `256/16` iterations was itself sized for a wider reference
//     screen than this real 84px one, so it was scaled down here to
//     `i < LCDWIDTH` - same 16px stride, just fewer tiles, enough to cover
//     the real screen with one tile of clipped overhang past the right
//     edge exactly like upstream's own excess tiles did).
//   - the ground's own dithered/highlighted strip -> no bitmap involved at
//     all upstream either (checked Background.ino's own real drawGround()
//     directly) - flapDrawGround() draws the same real 4-layer
//     fillRect() sequence (BLACK top line, GRAY body, WHITE highlight
//     line, BLACK bottom shadow line) plus the real BLACK "dent" rects,
//     matching upstream exactly.
//   - gameoverBitmap/trophyBitmap -> flapUpdateGameover()/flapUpdateWin()
//     draw them directly at upstream's own real GAMEOVERX/TROPHYX x
//     coordinates (`floor((LCDWIDTH-30)/2)`=27 and `floor((LCDWIDTH-22)/2)`
//     =31 respectively - TROPHYX kept literal even though the real
//     trophyBitmap's own width is actually 24, not 22, an upstream
//     approximation of its own, not a porting artifact) and the same
//     sliding/floating Y upstream already used.
//   - titleBitmap (the logo shown by the very first `gb.titleScreen()`
//     call) -> flapUpdateTitle() now draws it directly via `gbDrawBitmap()`
//     at (0, 12) - the real anchor the actual `Gamebuino::titleScreen()`
//     function itself draws a passed-in logo at (confirmed by reading a
//     real full `Gamebuino.cpp`'s own `titleScreen()` body, since this
//     particular upstream source calls the library function rather than
//     drawing the logo inline itself) - replacing the previous "FLAPPY"/
//     "BIRDO" text-line stand-in. The "PRESS A" prompt text stays (that's
//     this port's own real UI text, not a sprite substitute), moved above
//     the bitmap instead of overlapping it.
//
// MASKS: every real `*MaskBitmap` array (upstream drew each one first in
// WHITE, immediately before drawing the real BLACK outline bitmap on the
// exact same spot) was ORIGINALLY skipped entirely on this file's first
// bitmap-restoration pass, on the assumption that "off" bits being fully
// transparent would already look identical to the real masked result -
// that assumption was wrong, caught via two separate real user reports
// (a pipe showing background bleeding through its own "white" interior,
// then the same thing for the bird) after this file had already shipped
// with it. Corrected per-sprite, case by case:
//   - bird1Bitmap/bird2Bitmap: **real bird1MaskBitmap/bird2MaskBitmap are
//     now genuinely restored and drawn** (flapDrawPlayerAlive()/
//     flapDrawPlayerEnd()) - a first attempt at fixing this with a plain
//     rectangular `gbFillRect()` (matching the pipe's own fix, below) was
//     itself wrong for a *different* reason: the bird's silhouette is
//     rounded, not rectangular, so a rectangle mask left a visibly
//     rectangular "halo" around it - restoring the real, bird-shaped mask
//     bytes was the only fix that actually looked right.
//   - pipeBitmap: has no real mask bitmap of its own upstream at all -
//     real hardware instead draws a real GRAY `fillRect()` body plus WHITE
//     highlight strips before the BLACK outline bitmap (see
//     flapDrawPipes() below) - a plain rectangle is correct here (not a
//     halo-prone approximation like the bird) since the real pipe body
//     genuinely is a rectangle on real hardware too, not a mask-shaped
//     silhouette.
//   - gameoverBitmap/trophyBitmap: same plain-`gbFillRect()` treatment as
//     the pipe (drawn on top of a busy already-drawn scene, where the real
//     mask mattered) - both are genuinely rectangular banner/cup shapes on
//     real hardware too, so no halo risk here either.
//   - skyMaskBitmap: real upstream's own genuine cloud-shaped WHITE mask,
//     restored as `flapSkyMaskBitmap` and drawn immediately before
//     `flapSkyBitmap` in `flapDrawBackground()`, punching a hole through
//     the real GRAY top-of-sky band exactly like upstream.
//
// STATE MACHINE: upstream's own two blocking `gb.titleScreen(...)` calls
// (a logo screen, then a text instructions screen, both inside
// `initGame()`) plus its own separately-interactive difficulty menu
// (`initDifficulty()`, gated by a `difficulty_menu` bool checked every
// `loop()` iteration) were converted into an explicit FlapState enum
// (FLAP_STATE_TITLE -> FLAP_STATE_MENU -> FLAP_STATE_PLAY), matching the
// "blocking loop -> explicit resumable state" treatment used throughout
// this project (see gamePong.c's own header comment). Upstream's own
// `player_death` bool (checked inline inside the main play branch) and its
// score>=SCOREMAX "win" branch became two further explicit states,
// FLAP_STATE_GAMEOVER and FLAP_STATE_WIN, each mirroring upstream's own
// `gameOver()`/`gameWin()` functions almost line for line. Every state
// re-entered via `flapBeginTitle()` (a fresh C-button press mid-game, or a
// completed win-animation cycle) performs the exact same full reset
// upstream's own `initGame()` does every time it runs: difficulty resets
// to NORMAL and the mute toggle resets to unmuted, discarding whatever the
// player had previously chosen - preserved here exactly since that really
// is what pressing C does on real hardware, not a porting artifact.
//
// SOUND: upstream's own `playsoundfx()` drives a small hand-rolled
// "FX Synth" tracker effect (per-call waveform/volume-slide/pitch-slide
// `gb.sound.command(...)` calls) layered on top of a single `playNote()`
// call - this shim has no tracker/pattern engine at all (see
// gamebuinoShim.h's own header comment), so only the underlying
// `playNote(pitch, duration)` call for each of upstream's five sound
// effects (fly/point/death/win-melody/menu-bip) was kept, via a small
// `flapPlaySfx()` wrapper; the waveform/slide shaping is dropped outright
// (documented here, not silently). Upstream's own mute toggle
// (`gb.sound.setVolume(sound_volume, 0)`, checked every tick) has no
// direct equivalent (no `gbSetVolume()` in this shim) - reimplemented
// instead by gating `flapPlaySfx()` itself on a `flapSoundOn` bool, which
// the B button still toggles exactly like upstream's own `soundMute()`.
//
// A GENUINE UPSTREAM BUG, FOUND, INITIALLY PRESERVED, THEN FIXED ON REQUEST:
// upstream's own `difficulty_level` is declared `long` ("easy=0.5 <
// normal=1.0 < hard=2.0" per its own comment) but the menu's own confirm
// handler does `if (difficulty_level == 0) difficulty_level = 0.5;` -
// assigning a fractional value to an integer-typed variable truncates it
// straight back to 0 on real hardware too, so selecting SLOW never actually
// gave a 0.5x speed at all: the pipes' own `pipe[z].x -= difficulty_level;`
// update became a no-op, and since the player never moves horizontally
// either, SLOW genuinely never scrolled a single pipe into range - the only
// way to "lose" on it was hitting the ground or ceiling. This port first
// kept that behavior verbatim (it read as a real, if surely unintended,
// quirk of original gameplay rather than a porting artifact), but was then
// asked directly to make SLOW's pipes actually move - unlike real
// hardware's own `long`, this dialect's `float` has no trouble holding a
// genuine `0.5`, so `flapDifficultySpeed` (used in real float arithmetic
// throughout `flapUpdatePipes()`) was changed from `int` to `float` and
// SLOW now resolves to a real `0.5` in `flapBeginPlay()` - the speed
// upstream's own comment clearly intended, not the truncated value its own
// `long` variable actually produced.
//
// DROPPED AS DEAD CODE UPSTREAM: `highscore_1`/`highscore_2`/`highscore_3`
// (plain `byte` globals, declared but never actually read or written
// anywhere in the real source - the actual highscore storage upstream
// uses is `pipe[0..2].hs`, itself never written to any persistent storage
// either) were not ported at all. This port's own equivalent
// (`flapHighscore[3]`) was likewise originally in-RAM/session-only,
// matching real upstream behavior exactly.
//
// EEPROM PERSISTENCE ADDED, BEYOND REAL UPSTREAM - added directly on
// request once an audit found this game displays 3 real per-difficulty
// highscores that never survived a cartridge reboot (real upstream never
// persisted them either, per the paragraph above - this is a genuine
// enhancement past real hardware, not a restoration). `gameFlappyBirdo_
// init()`'s own 3 real `flapHighscore[i] = 0;` lines are now 3 real
// `eeprom_read_word(i*2)` loads instead (2 bytes/entry, matching this
// project's own established per-entry layout, e.g. gameCrabator.c/
// gameDescent.c - with the same `==0xFFFF` fresh-EEPROM-cell reset check
// rather than trusting a raw 65535 sentinel) - the very same effective
// "0 on a genuinely fresh card" outcome the old hardcoded `=0` produced,
// just no longer discarding a real earlier save. Saved via
// `eeprom_write_word(flapDifficultyIndex*2, flapScore)` at the exact
// point this port's own highscore-tracking line already updates
// `flapHighscore[flapDifficultyIndex]` in memory - a one-shot write per
// new high score, not a per-frame write.
//
// A SECOND PRESERVED UPSTREAM QUIRK: upstream's own `loop()` always calls
// both `playerMove()` and `updatePipes()` together, and always draws
// `drawPlayerAlive()` right after, based on whether `player_death` was
// false *at the start* of that same tick - even on the exact tick a
// collision is detected inside `playerMove()` itself (which sets
// `player_death = true` only for the *next* tick's branch to notice).
// The practical effect is one extra "still alive" frame drawn on the same
// tick a fatal collision registers, before the death animation begins.
// Reproduced exactly below in `flapUpdateActive()`'s own FLAP_STATE_PLAY
// branch (checked once per tick, not re-checked after `flapPlayerMove()`
// runs).
//
// A PREVIOUSLY "REINTERPRETED" LINE, NOW RESOLVED FOR REAL: `drawPlayerEnd()`'s
// own `if (...) player_y += 4;` is captioned "the image move to the top" in
// upstream's own comment, even though a plain `+=` on a top-down Y axis
// normally moves a sprite *down* - this file's earlier pass (written before
// `gbDrawBitmapRotated()` existed) couldn't rule out the real hardware's own
// `ROTCW`-rotated `drawBitmap()` call changing what that Y offset anchors to,
// and guessed the comment's stated intent (float upward) over the literal
// arithmetic. Now actually checked against `gbDrawBitmapRotated()`'s own real,
// bit-for-bit-verified rotation math (see gamebuinoShim.c): rotation only
// remaps a bitmap's own internal pixel coordinates before adding the same x/y
// anchor - it never changes which screen direction a larger Y moves toward.
// So the literal arithmetic was right all along and the upstream comment was
// simply wrong (or referring to something else) - the dead bird's own corpse
// genuinely falls DOWN toward the ground (stopping just short of it), tumbling
// via the real `ROTCW` rotation on the way, in `flapDrawPlayerEnd()` below.
//
// `gb.pickRandomSeed()` became `gbPickRandomSeed()`, a documented no-op
// (see this project's own established precedent). `gb.battery`/backlight/
// tft calls: none exist in this particular upstream source to begin with.
// Real `font5x7`/`font3x5` (upstream switches between them per screen) were
// dropped like every other port here - this shim always renders with its
// own single fixed 8x8 glyph table at size 1 (see gamebuinoShim.h's own
// header comment); this game never needs size 2. Several menu/score text
// layouts were shortened or reflowed to fit this shim's own wider 8x8 font
// on the real 84px-wide display (the "PRESS A" prompt on the title screen,
// this port's own added UI text rather than a real upstream string, is
// positioned above the real titleBitmap logo rather than overlapping it).
//
// A REAL COLLISION-FIDELITY BUG, FOUND AND FIXED: bird-vs-pipe death
// detection used `gbCollideRectRect()` (an earlier, undocumented
// substitution - this shim had no `gbCollideBitmapBitmap()` primitive yet
// when this game was first ported), while real upstream's own
// `Player.ino` genuinely calls `gb.collideBitmapBitmap(PLAYERX, player_y,
// bird1Bitmap, pipe[cc].x, pipe[cc].y, pipeBitmap)` - real, per-pixel
// collision (confirmed directly against the real Gamebuino Classic
// library's own `Gamebuino::collideBitmapBitmap()` source, which
// genuinely does per-overlap-pixel testing, using `collideRectRect()`
// only as an early-exit bounding-box pre-check, not a replacement for it
// - see gameDescent.c's own `gbCollideBitmapBitmap()` for the same real
// source read). Restored to the real `gbCollideBitmapBitmap()` primitive
// (promoted to the shared shim later, once `gameDescent.c` needed it, but
// never backported to this file until now), matching upstream's own exact
// call shape - the bird's own real outline (not its full rectangular
// bounding box) now has to actually touch a pipe's own real solid pixels
// to die, exactly like real hardware.

#define FLAP_PIPEW 12
#define FLAP_PIPEH 24
#define FLAP_PIPEGAPV 24
#define FLAP_PIPEGAPH 30
#define FLAP_GROUNDH 4
#define FLAP_SCOREMAX 99
#define FLAP_PLAYERX 12
#define FLAP_PLAYERW 11
#define FLAP_PLAYERH 10

// pitch/duration pairs lifted straight out of upstream's own soundfx[][]
// table (element [1] = note pitch, element [7] = duration) - see this
// file's own header comment for why only these two fields of each entry
// survived the port.
#define FLAP_NOTE_FLY 0
#define FLAP_DUR_FLY 3
#define FLAP_NOTE_POINT 6
#define FLAP_DUR_POINT 8
#define FLAP_NOTE_DEATH 24
#define FLAP_DUR_DEATH 7
#define FLAP_NOTE_WIN 57
#define FLAP_DUR_WIN 57
#define FLAP_NOTE_MENUBIP 4
#define FLAP_DUR_MENUBIP 1

// upstream's own real GAMEOVERX/TROPHYX macros, `floor((LCDWIDTH-30)/2)` and
// `floor((LCDWIDTH-22)/2)` respectively, evaluated ahead of time against the
// real LCDWIDTH=84 (TROPHYX kept literal even though trophyBitmap's own real
// width is 24, not 22 - an upstream approximation of its own).
#define FLAP_GAMEOVERX 27
#define FLAP_TROPHYX 31

// -----------------------------------------------------------------------------
// Real upstream sprite bitmaps - copied byte-for-byte from the real
// `const byte NAME[] PROGMEM = { width, height, ... }` arrays in upstream's
// own FlappyBirdo.ino (see this file's own header comment). Every real
// PROGMEM byte becomes one plain int cell here, matching the exact
// `{ width, height, byte0, byte1, ... }` shape gbDrawBitmap()/
// gbDrawBitmapRotated() expect - no conversion needed. The `*MaskBitmap`
// arrays upstream also declared are deliberately not ported - see this
// file's own header comment for why, checked case by case rather than
// assumed uniformly safe.
// -----------------------------------------------------------------------------

int flapBird1Bitmap[22]= { 16, 10,
    0xF, 0x0, 0x3C, 0x80, 0x79, 0x40, 0xF8, 0x40, 0xFD, 0xC0, 0xC6, 0x20, 0x8D, 0xC0, 0x9E, 0x20,
    0x7F, 0xC0, 0x1E, 0x0, };

// Real bird1MaskBitmap/bird2MaskBitmap - restored after a direct user
// report that a plain rectangular fill (this file's own first attempt at
// an opaque backing, added when the pipe-transparency bug was found and
// fixed) left a visibly rectangular "halo" around the bird's own actual
// rounded silhouette. Real hardware never used a plain rectangle either -
// it draws this real, bird-shaped WHITE mask bitmap first, then the BLACK
// outline bitmap on the exact same spot, so restoring the genuine mask
// bytes (rather than approximating with a rectangle) matches real
// hardware exactly and fixes the halo for good.
int flapBird1MaskBitmap[22]= { 16, 10,
    0xF, 0x0, 0x3F, 0x80, 0x7F, 0xC0, 0xFF, 0xC0, 0xFF, 0xC0, 0xFF, 0xE0, 0xFF, 0xC0, 0xFF, 0xE0,
    0x7F, 0xC0, 0x1E, 0x0, };

int flapBird2MaskBitmap[22]= { 16, 10,
    0xF, 0x0, 0x7F, 0x80, 0xFF, 0xC0, 0xFF, 0xC0, 0xFF, 0xC0, 0xFF, 0xE0, 0xFF, 0xC0, 0xFF, 0xE0,
    0x7F, 0xC0, 0x1E, 0x0, };

int flapBird2Bitmap[22]= { 16, 10,
    0xF, 0x0, 0x7C, 0x80, 0x99, 0x40, 0x88, 0x40, 0x8D, 0xC0, 0xC6, 0x20, 0xFD, 0xC0, 0xFE, 0x20,
    0x7F, 0xC0, 0x1E, 0x0, };

int flapCityBitmap[40]= { 16, 19,
    0x7E, 0x0, 0x42, 0x0, 0x56, 0x3F, 0x42, 0x21, 0x57, 0xAB, 0xC8, 0xA1, 0x3A, 0xEA, 0xA8, 0xA2,
    0x3A, 0xAA, 0xA8, 0xA2, 0x3A, 0xAA, 0xA8, 0xA2, 0xA, 0x7A, 0xBC, 0x86, 0xC3, 0x19, 0xC, 0x81,
    0x11, 0xC2, 0x38, 0x20, 0x44, 0x0, };

int flapPipeBitmap[50]= { 16, 24,
    0xFF, 0xF0, 0x80, 0x10, 0x83, 0xF0, 0xC5, 0xD0, 0xFF, 0xF0, 0xCF, 0xF0, 0x85, 0xD0, 0x83, 0xF0,
    0x85, 0xD0, 0x83, 0xF0, 0x85, 0xD0, 0x83, 0xF0, 0x85, 0xD0, 0x83, 0xF0, 0x85, 0xD0, 0x83, 0xF0,
    0x85, 0xD0, 0x83, 0xF0, 0x85, 0xD0, 0x83, 0xF0, 0x85, 0xD0, 0x83, 0xF0, 0x85, 0xD0, 0x83, 0xF0, };

int flapSkyBitmap[20]= { 24, 6,
    0x1C, 0x0, 0x0, 0xA2, 0x7, 0xA0, 0x41, 0x8, 0x40, 0x43, 0xD0, 0x40, 0x4, 0x30, 0x0, 0x0,
    0x10, 0x0, };

// Real upstream `skyMaskBitmap` - a real WHITE mask, drawn immediately
// before flapSkyBitmap at the exact same spot, punching a cloud-shaped hole
// through the GRAY top-of-sky band so the BLACK cloud outline shows cleanly
// against white rather than against the gray band (the same mask-before-
// outline pattern already restored elsewhere in this file).
int flapSkyMaskBitmap[20]= { 24, 6,
    0x1C, 0x0, 0x0, 0xBE, 0x7, 0xA0, 0xFF, 0xF, 0xE0, 0xFF, 0xDF, 0xE0, 0xFF, 0xFF, 0xE0, 0xFF,
    0xFF, 0xE0, };

int flapGameoverBitmap[102]= { 32, 25,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF, 0x80, 0x0, 0x0, 0x10, 0x80, 0x0, 0x70,
    0x20, 0x80, 0x0, 0x88, 0x27, 0xBF, 0xFD, 0x4, 0x24, 0xC2, 0x3, 0x24, 0x24, 0x82, 0x1, 0x24,
    0x24, 0x92, 0x49, 0x4, 0x20, 0x92, 0x49, 0x1C, 0x20, 0x82, 0x49, 0x1C, 0x31, 0xC2, 0x49, 0x90,
    0x1E, 0x3F, 0xFF, 0xF0, 0xC, 0x1F, 0xE3, 0x70, 0x4, 0x1F, 0xC1, 0x7C, 0x4, 0x92, 0x49, 0x84,
    0x4, 0x92, 0x49, 0x4, 0x4, 0x92, 0x41, 0x1C, 0x4, 0x10, 0x47, 0x3C, 0x4, 0x10, 0xC7, 0x20,
    0x6, 0x31, 0xE5, 0x20, 0x3, 0xFF, 0x3D, 0xE0, 0x1, 0xDE, 0x1D, 0xE0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, };

int flapTrophyBitmap[98]= { 24, 32,
    0x0, 0x0, 0x0, 0x3F, 0xFF, 0xF0, 0x20, 0x0, 0x10, 0x3F, 0xFF, 0xF0, 0x1C, 0x0, 0xE0, 0x20,
    0x0, 0x10, 0x48, 0x0, 0x48, 0x58, 0x0, 0x68, 0x58, 0x0, 0x68, 0x4C, 0x0, 0xC8, 0x64, 0x0,
    0x98, 0x3A, 0x1, 0x70, 0x1F, 0x3, 0xE0, 0x7, 0x87, 0x80, 0x0, 0xFC, 0x0, 0x0, 0x78, 0x0,
    0x0, 0x48, 0x0, 0x0, 0xFC, 0x0, 0x3, 0x3, 0x0, 0x3F, 0xF0, 0x80, 0x24, 0x9F, 0x80, 0x24,
    0x9F, 0x80, 0x24, 0x9F, 0xE0, 0x24, 0x92, 0x10, 0x24, 0x9E, 0x8, 0x24, 0x92, 0x48, 0x24, 0x92,
    0x48, 0x20, 0x12, 0x48, 0x20, 0x32, 0x48, 0x3F, 0xFF, 0xF8, 0x3F, 0xDF, 0xF8, 0x0, 0x0, 0x0, };

int flapTitleBitmap[242]= { 64, 30,
    0x0, 0xF, 0xFC, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0x44, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x20, 0x44, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x40, 0x44, 0x0, 0x0, 0x7, 0xFC, 0x0,
    0x0, 0x47, 0xC4, 0xFF, 0xE3, 0xE4, 0x44, 0x0, 0x0, 0x40, 0x45, 0x4, 0x14, 0x14, 0x44, 0x0,
    0x0, 0x40, 0x46, 0x4, 0xC, 0xC, 0x44, 0x0, 0x0, 0x40, 0x44, 0x4, 0x4, 0x4, 0x44, 0x0,
    0x0, 0x47, 0xC4, 0x44, 0x44, 0x44, 0x44, 0x0, 0x0, 0x47, 0xC4, 0x44, 0x44, 0x44, 0x44, 0x0,
    0x0, 0x44, 0x44, 0x4, 0x4, 0x4, 0x4, 0x0, 0x0, 0x44, 0x46, 0x4, 0xC, 0xE, 0x4, 0x0,
    0x0, 0x44, 0x47, 0x4, 0x1C, 0x1F, 0x4, 0x0, 0x0, 0x7C, 0x7D, 0xFC, 0x74, 0x7B, 0xC4, 0x0,
    0x0, 0x7C, 0x7C, 0xFC, 0x64, 0x71, 0x4, 0x0, 0x0, 0x0, 0xF, 0xC4, 0x44, 0x5F, 0xC, 0x0,
    0x0, 0x0, 0x8, 0x24, 0x44, 0x51, 0x1C, 0x0, 0x0, 0x0, 0x8, 0x17, 0xC7, 0xD1, 0xF8, 0x0,
    0x0, 0x0, 0x8, 0xF, 0xC7, 0xD1, 0x0, 0x0, 0x0, 0x0, 0x8, 0x8F, 0x9F, 0x31, 0x38, 0x0,
    0x0, 0x0, 0x8, 0x88, 0xA1, 0x41, 0x44, 0x0, 0x0, 0x0, 0x8, 0x8, 0xC1, 0x81, 0x82, 0x0,
    0x0, 0x0, 0x8, 0xF, 0x81, 0x1, 0x1, 0x0, 0x0, 0x0, 0x8, 0x88, 0x87, 0x11, 0x11, 0x0,
    0x0, 0x0, 0x8, 0x88, 0x8F, 0x11, 0x11, 0x0, 0x0, 0x0, 0x8, 0x8, 0x89, 0x1, 0x1, 0x0,
    0x0, 0x0, 0x8, 0x18, 0x89, 0x81, 0x83, 0x0, 0x0, 0x0, 0x8, 0x38, 0x88, 0xC1, 0xC6, 0x0,
    0x0, 0x0, 0xF, 0xEF, 0xF8, 0x7F, 0x7C, 0x0, 0x0, 0x0, 0xF, 0xCF, 0xF8, 0x3F, 0x38, 0x0, };

enum FlapState
{
    FLAP_STATE_TITLE = 0,
    FLAP_STATE_MENU = 1,
    FLAP_STATE_PLAY = 2,
    FLAP_STATE_GAMEOVER = 3,
    FLAP_STATE_WIN = 4
};

typedef struct
{
    float x;
    float y;
} FlapPipe;

int flapState;

float flapPlayerY = 0;
float flapGravity = 0;
int flapPlayerAnim = 0;

int flapDifficultyIndex = 1; // 0=SLOW 1=NORMAL 2=FAST, menu selection
float flapDifficultySpeed = 1; // resolved pixels/tick once play begins - see header comment: SLOW is a real 0.5, not upstream's own truncated-to-0 bug
bool flapSoundOn = true;

int flapScore = 0;
int flapScoreUnits = 0; // upstream's own `score_units`: 0 while score<10, sticks at 1 (wider outline box) once it reaches double digits, reset per round
int flapHighscore[3]; // per-difficulty, in-session only - see header comment

int flapScoreMaxReached = 0; // win-animation timer
int flapGameoverY = 0;
int flapGameoverTimer = 0;

FlapPipe flapPipes[3];

// -----------------------------------------------------------------------------
// Sound
// -----------------------------------------------------------------------------

void flapPlaySfx( int pitch, int duration )
{
    if( flapSoundOn )
      gbPlayNote( pitch, duration );
}

// -----------------------------------------------------------------------------
// Round / highscore bookkeeping
// -----------------------------------------------------------------------------

void flapUpdateHighscore()
{
    if( flapScore > flapHighscore[ flapDifficultyIndex ] )
    {
        flapHighscore[ flapDifficultyIndex ] = flapScore;
        eeprom_write_word( flapDifficultyIndex * 2, flapScore );
    }
}

void flapPipesStart()
{
    int bb;
    for( bb = 0; bb < 3; bb++ )
    {
        flapPipes[ bb ].x = (float)( LCDWIDTH + FLAP_PIPEGAPH * bb );
        flapPipes[ bb ].y = (float)( 2 + arand( 15 ) + FLAP_PIPEGAPV );
    }
}

void flapUpdateScore()
{
    if( flapScore + 1 <= FLAP_SCOREMAX )
    {
        flapScore = flapScore + 1;
        flapPlaySfx( FLAP_NOTE_POINT, FLAP_DUR_POINT );
    }
    else
      flapScore = FLAP_SCOREMAX;
}

// == upstream initVariables(): a "simple death = simple reset" - keeps
// whatever difficulty/mute state is already active, just starts a fresh
// round.
void flapResetRound()
{
    flapUpdateHighscore();
    flapPipesStart();
    flapPlayerY = (float)( ( ( LCDHEIGHT - FLAP_GROUNDH ) / 2 ) - 8 );
    flapGravity = 0;
    flapPlayerAnim = 0;
    flapScore = 0;
    flapScoreUnits = 0;
    flapGameoverTimer = 0;
    flapGameoverY = LCDHEIGHT;
}

// == upstream initGame(): a full reset back to the title screen, also
// forcing difficulty back to NORMAL and sound back on - see header
// comment for why that forced reset is preserved, not just a soft resume.
void flapBeginTitle()
{
    flapResetRound();
    flapDifficultyIndex = 1;
    flapSoundOn = true;
    flapState = FLAP_STATE_TITLE;
}

void flapBeginMenu()
{
    flapState = FLAP_STATE_MENU;
}

void flapBeginPlay()
{
    if( flapDifficultyIndex == 0 )
      flapDifficultySpeed = 0.5; // SLOW - see header comment: a real half-speed, not upstream's own bug
    else if( flapDifficultyIndex == 1 )
      flapDifficultySpeed = 1; // NORMAL
    else
      flapDifficultySpeed = 2; // FAST

    flapState = FLAP_STATE_PLAY;
}

void flapBeginGameover()
{
    flapGameoverY = LCDHEIGHT;
    flapGameoverTimer = 0;
    flapState = FLAP_STATE_GAMEOVER;
}

void flapBeginWin()
{
    flapScoreMaxReached = 0;
    flapGameoverY = LCDHEIGHT;
    flapState = FLAP_STATE_WIN;
}

// -----------------------------------------------------------------------------
// Drawing - background / ground / pipes / score (see header comment for the
// real-bitmap restoration details)
// -----------------------------------------------------------------------------

// Direct port of real Background.ino's own drawBackground(): a GRAY
// top-of-sky band, then skyMaskBitmap (WHITE)/skyBitmap (BLACK)/cityBitmap
// (GRAY) tiled every 16px - upstream's own real per-tile anchor. Upstream's
// own loop bound (256/16 iterations) targeted a wider reference screen than
// this real 84px-wide one, so it's scaled down here to just cover
// LCDWIDTH (with one tile of harmless clipped overhang past the right
// edge, same as upstream's own excess tiles).
void flapDrawBackground()
{
    gbSetColor( GB_GRAY );
    gbFillRect( 0, 0, LCDWIDTH, 17 );

    int i;
    for( i = 0; i < LCDWIDTH; i = i + 16 )
    {
        gbSetColor( 0 ); // white mask - punches a hole through the gray band
        gbDrawBitmap( i, 13, flapSkyMaskBitmap );
        gbSetColor( 1 ); // black cloud outline
        gbDrawBitmap( i, 13, flapSkyBitmap );
        gbSetColor( GB_GRAY ); // gray city silhouette
        gbDrawBitmap( i, 23, flapCityBitmap );
    }
}

// Direct port of real Background.ino's own drawGround() - no bitmap here,
// just 4 real layered fillRect() passes (top BLACK line, GRAY body, WHITE
// highlight line, bottom BLACK shadow line) plus a row of small BLACK
// "dents" punched into the gray body every 4px.
void flapDrawGround()
{
    gbSetColor( 1 ); // 1st top black line
    gbFillRect( 0, LCDHEIGHT - FLAP_GROUNDH, LCDWIDTH, 1 );
    gbSetColor( GB_GRAY ); // gray background
    gbFillRect( 0, LCDHEIGHT - FLAP_GROUNDH + 1, LCDWIDTH, FLAP_GROUNDH - 1 );
    gbSetColor( 0 ); // white highlight
    gbFillRect( 0, LCDHEIGHT - FLAP_GROUNDH + 1, LCDWIDTH, 1 );
    gbSetColor( 1 ); // shadow on the bottom
    gbFillRect( 0, LCDHEIGHT - 1, LCDWIDTH, 1 );

    int j;
    for( j = 0; j < LCDWIDTH / 2; j = j + 1 )
      gbFillRect( j * 4, LCDHEIGHT - 2, 2, 1 );
}

void flapDrawPipes()
{
    // pipeBitmap is not a self-contained texture - real upstream
    // `drawPipes()` draws three real layers per pipe: a GRAY fillRect
    // body, then WHITE highlight strips, and only THEN the BLACK outline
    // bitmap on top - pipeBitmap by itself is only ever the outline/rivet
    // linework, never meant to be drawn alone.
    //
    // The fill/highlight rects use `FLAP_PIPEW` (12), not `flapPipeBitmap`'s
    // own declared width (16): decoding the real bitmap's own bits by hand
    // (a quick ASCII-art dump) showed its own rightmost 4 columns are
    // blank/off in every single one of its 24 rows - the real drawn art
    // only ever occupies the left 12 columns, exactly matching `FLAP_PIPEW`
    // (also the real collision-box width upstream's own code already
    // used) - matching upstream's own actual real `fillRect(pipe[a].x,
    // pipe[a].y, PIPEW, 24)` call width exactly instead of the bitmap's
    // own (partly-padded) declared header.
    int a;
    for( a = 0; a < 3; a++ )
    {
        int px = (int)flapPipes[ a ].x;
        int py = (int)flapPipes[ a ].y;
        int topY = py - FLAP_PIPEH - FLAP_PIPEGAPV;

        gbSetColor( GB_GRAY );
        gbFillRect( px, py, FLAP_PIPEW, FLAP_PIPEH );
        gbFillRect( px, topY, FLAP_PIPEW, FLAP_PIPEH );

        gbSetColor( 0 ); // white highlights
        gbFillRect( px, py + 1, FLAP_PIPEW, 1 );
        gbFillRect( px + 2, py, 2, FLAP_PIPEH );
        gbFillRect( px, py - FLAP_PIPEGAPV - 1, FLAP_PIPEW, 1 );
        gbFillRect( px + 2, topY, 2, FLAP_PIPEH );

        gbSetColor( 1 );
        gbDrawBitmap( px, py, flapPipeBitmap ); // bottom pipe
        gbDrawBitmapRotated( px, topY, flapPipeBitmap, 0, 2 ); // top pipe (NOROT, FLIPV)
    }
}

// Direct port of upstream's own real drawScore(): a filled BLACK outline
// box (widening once the score reaches double digits, via flapScoreUnits)
// with the score digits printed in WHITE on top of it - genuine real
// hardware behavior this file originally dropped in favor of plain BLACK
// text with no box at all, found and restored once gbSetFont()/real fonts
// made a literal port of this function practical.
void flapDrawScore()
{
    gbFontSize = 1;
    gbSetFont( gbFont3x5 );

    if( flapScore > 9 ) flapScoreUnits = 1;

    gbSetColor( 1 );
    gbFillRect( ( LCDWIDTH - 3 * ( 1 + flapScoreUnits ) ) / 2 - 1, 3, 5 + 4 * flapScoreUnits, 7 );

    gbCursorX = ( LCDWIDTH - 3 * ( 1 + flapScoreUnits ) ) / 2;
    gbCursorY = 4;
    gbSetColor( 0 );
    gbPrintNumber( flapScore );
    gbSetColor( 1 );
}

// -----------------------------------------------------------------------------
// Drawing - the player (see header comment for the real-bitmap restoration
// details)
// -----------------------------------------------------------------------------

// == upstream playerAnimation(): a shared 0->1->2->3->4->0 wing-flap timer,
// used both by drawPlayerAlive() and the difficulty menu's own bird cursor.
void flapAnimatePlayer()
{
    if( ( flapPlayerAnim > 0 ) && ( flapPlayerAnim < 4 ) )
      flapPlayerAnim = flapPlayerAnim + 1;
    else
      flapPlayerAnim = 0;
}

void flapDrawPlayerAlive()
{
    flapAnimatePlayer();

    // Real bird1MaskBitmap/bird2MaskBitmap, drawn WHITE first (see this
    // array's own comment above for why a plain rectangle wasn't good
    // enough), then the matching BLACK outline bitmap on the exact same
    // spot - both selected by the same real animation threshold upstream
    // uses, so the mask always matches the outline drawn on top of it.
    if( flapPlayerAnim > 2 )
    {
        gbSetColor( 0 );
        gbDrawBitmap( FLAP_PLAYERX, (int)flapPlayerY, flapBird1MaskBitmap );
        gbSetColor( 1 );
        gbDrawBitmap( FLAP_PLAYERX, (int)flapPlayerY, flapBird1Bitmap );
    }
    else
    {
        gbSetColor( 0 );
        gbDrawBitmap( FLAP_PLAYERX, (int)flapPlayerY, flapBird2MaskBitmap );
        gbSetColor( 1 );
        gbDrawBitmap( FLAP_PLAYERX, (int)flapPlayerY, flapBird2Bitmap );
    }
}

void flapDrawPlayerEnd()
{
    // literal upstream arithmetic, now confirmed correct - see header
    // comment: the dead bird's own corpse falls DOWN toward the ground,
    // stopping just short of it, tumbling via the real ROTCW rotation on
    // the way (upstream's own real
    // `drawBitmap(PLAYERX, player_y, bird1Bitmap, ROTCW, NOFLIP)` call).
    if( flapPlayerY < (float)( LCDHEIGHT - FLAP_GROUNDH - FLAP_PLAYERH ) )
      flapPlayerY = flapPlayerY + 4;

    // Real bird1MaskBitmap, ROTCW like the outline itself - matching
    // upstream's own real `drawBitmap(PLAYERX, player_y, bird1MaskBitmap,
    // ROTCW, NOFLIP)` call exactly (see flapDrawPlayerAlive()'s own comment
    // for why a plain rectangle wasn't good enough here either).
    gbSetColor( 0 );
    gbDrawBitmapRotated( FLAP_PLAYERX, (int)flapPlayerY, flapBird1MaskBitmap, 3, 0 ); // ROTCW, NOFLIP

    gbSetColor( 1 );
    gbDrawBitmapRotated( FLAP_PLAYERX, (int)flapPlayerY, flapBird1Bitmap, 3, 0 ); // ROTCW, NOFLIP
}

// -----------------------------------------------------------------------------
// Gameplay
// -----------------------------------------------------------------------------

void flapPlayerMove()
{
    flapGravity = flapGravity + 0.18;
    flapGravity = flapGravity * 0.95;
    flapPlayerY = flapPlayerY + flapGravity;

    if( flapPlayerY > 0 )
    {
        if( gbPressed( BTN_A ) || gbPressed( BTN_RIGHT ) || gbPressed( BTN_DOWN ) || gbPressed( BTN_LEFT ) || gbPressed( BTN_UP ) )
        {
            if( flapGravity > -0.5 )
              flapGravity = flapGravity - 2;
            flapPlaySfx( FLAP_NOTE_FLY, FLAP_DUR_FLY );
            flapPlayerAnim = 1;
        }
    }

    if( flapPlayerY < 0 )
    {
        flapPlayerY = 1;
        flapGravity = 0;
    }

    int cc;
    for( cc = 0; cc < 3; cc++ )
    {
        int pipeX = (int)flapPipes[ cc ].x;
        int pipeY = (int)flapPipes[ cc ].y;

        if( ( ( flapPlayerY + FLAP_PLAYERH ) > ( LCDHEIGHT - FLAP_GROUNDH ) )
            || gbCollideBitmapBitmap( FLAP_PLAYERX, (int)flapPlayerY, flapBird1Bitmap, pipeX, pipeY, flapPipeBitmap )
            || gbCollideBitmapBitmap( FLAP_PLAYERX, (int)flapPlayerY, flapBird1Bitmap, pipeX, pipeY - FLAP_PIPEH - FLAP_PIPEGAPV, flapPipeBitmap ) )
        {
            flapPlaySfx( FLAP_NOTE_DEATH, FLAP_DUR_DEATH );
            flapBeginGameover();
        }
    }
}

void flapUpdatePipes()
{
    int z;
    for( z = 0; z < 3; z++ )
    {
        flapPipes[ z ].x = flapPipes[ z ].x - (float)flapDifficultySpeed;

        if( flapPipes[ z ].x <= (float)( -FLAP_PIPEW ) )
          flapPipes[ z ].x = (float)LCDWIDTH;

        if( flapPipes[ z ].x >= (float)LCDWIDTH )
          flapPipes[ z ].y = (float)( 2 + arand( 15 ) + FLAP_PIPEGAPV );

        // Upstream's own real check is a genuine float equality
        // (`pipe[z].x == PLAYERX`), not a cast-then-compare - deliberate:
        // on SLOW (difficulty_level 0.5) x crosses PLAYERX at a half-integer
        // step (e.g. 12.5, then 12.0), and truncating to int before
        // comparing (as this file used to) makes both of those steps equal
        // FLAP_PLAYERX, firing flapUpdateScore() twice for one pipe - found
        // via a real user report of SLOW awarding 2 points per pipe instead
        // of 1. A real float compare only matches the single exact frame
        // upstream itself matches (and, as a preserved upstream quirk, FAST's
        // own 2.0 step can skip over FLAP_PLAYERX entirely and score nothing
        // for a given pipe - not a bug introduced by this port).
        if( flapPipes[ z ].x == (float)FLAP_PLAYERX )
          flapUpdateScore();
    }
}

void flapUpdateGameover()
{
    if( flapGameoverY > ( LCDHEIGHT / 3 ) )
      flapGameoverY = flapGameoverY - 4;

    if( flapGameoverTimer < LCDHEIGHT / 2 )
      flapGameoverTimer = flapGameoverTimer + 2;
    else
    {
        flapResetRound();
        flapState = FLAP_STATE_PLAY;
        return;
    }

    // real gameoverBitmap, at upstream's own real
    // GAMEOVERX = floor((LCDWIDTH-30)/2). Upstream erases first via a
    // WHITE gameoverMaskBitmap pass before the BLACK outline - not
    // ported (see header comment): a plain white fillRect covering the
    // same real 32x25 footprint gives the same legible-outline-on-white
    // effect without porting the mask's own actual bytes.
    gbSetColor( 0 );
    gbFillRect( FLAP_GAMEOVERX, flapGameoverY, 32, 25 );
    gbSetColor( 1 );
    gbDrawBitmap( FLAP_GAMEOVERX, flapGameoverY, flapGameoverBitmap );
}

void flapUpdateWin()
{
    if( flapScoreMaxReached <= 1 )
      flapPlaySfx( FLAP_NOTE_WIN, FLAP_DUR_WIN );
    flapScoreMaxReached = flapScoreMaxReached + 1;

    if( flapScoreMaxReached > LCDHEIGHT )
    {
        flapBeginTitle();
        return;
    }

    if( flapGameoverY > ( LCDHEIGHT / 3 ) - 2 )
      flapGameoverY = flapGameoverY - 2;

    // real trophyBitmap, at upstream's own real
    // TROPHYX = floor((LCDWIDTH-22)/2) (kept literal even though the real
    // bitmap's own width is 24, not 22 - an upstream approximation of its
    // own). Mask dropped exactly like the gameover banner above, same
    // plain-white-fillRect substitute.
    gbSetColor( 0 );
    gbFillRect( FLAP_TROPHYX, flapGameoverY, 24, 32 );
    gbSetColor( 1 );
    gbDrawBitmap( FLAP_TROPHYX, flapGameoverY, flapTrophyBitmap );
}

// -----------------------------------------------------------------------------
// States
// -----------------------------------------------------------------------------

void flapUpdateTitle()
{
    gbSetColor( 1 );

    // real titleBitmap, at (0, 12) - the real anchor the actual
    // Gamebuino::titleScreen() function itself draws a passed-in logo at
    // (see header comment). "PRESS A" is this port's own UI text (not a
    // real upstream string), placed above the bitmap instead of
    // overlapping it.
    gbFontSize = 1;
    gbCursorX = 14;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );
    gbDrawBitmap( 0, 12, flapTitleBitmap );

    if( gbPressed( BTN_A ) )
      flapBeginMenu();
}

void flapUpdateMenu()
{
    if( gbPressed( BTN_UP ) )
    {
        flapDifficultyIndex = flapDifficultyIndex - 1;
        flapPlayerAnim = 1;
        flapPlaySfx( FLAP_NOTE_MENUBIP, FLAP_DUR_MENUBIP );
    }
    if( gbPressed( BTN_DOWN ) )
    {
        flapDifficultyIndex = flapDifficultyIndex + 1;
        flapPlayerAnim = 1;
        flapPlaySfx( FLAP_NOTE_MENUBIP, FLAP_DUR_MENUBIP );
    }
    if( flapDifficultyIndex < 0 ) flapDifficultyIndex = 2;
    if( flapDifficultyIndex > 2 ) flapDifficultyIndex = 0;

    // real bird1Bitmap/bird2Bitmap - a genuine upstream `drawBitmap(...)`
    // in Menu.ino's own real initDifficulty(): a tiny animated bird
    // "cursor" next to the selected difficulty line, at upstream's own
    // real (0, 22+8*difficulty) anchor. Note upstream's own real
    // selection threshold here (anim>2 => bird2) is the OPPOSITE of
    // drawPlayerAlive()'s own (anim>2 => bird1) - a genuine upstream
    // asymmetry, preserved exactly rather than unified. This bird is
    // upstream's own ONLY real selection indicator - its own would-be text
    // arrow (`//gb.display.print(F("\20")); //arrow`) is commented out,
    // dead code in the real source, so no ">"-style cursor is drawn here
    // either (this file used to add one anyway, back when the fixed-width
    // 8x8 shim font made the real layout below impractical to fit).
    flapAnimatePlayer();
    gbSetColor( 1 );
    if( flapPlayerAnim > 2 )
      gbDrawBitmap( 0, 22 + 8 * flapDifficultyIndex, flapBird2Bitmap );
    else
      gbDrawBitmap( 0, 22 + 8 * flapDifficultyIndex, flapBird1Bitmap );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        flapBeginPlay();
    }

    // Direct port of upstream's own real initDifficulty() text block,
    // cursor positions/fonts and all (now practical - see this file's own
    // header comment on the font port). Real upstream draws this as two
    // `print()` calls with embedded '\n's; expanded here into one
    // gbPrintString() per line (this dialect's own string literals aren't
    // relied on for '\n' escapes) landing on the exact same real per-line Y
    // positions '\n' would have advanced to.
    gbFontSize = 1;
    gbSetFont( gbFont5x7 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "DIFFICULTY:" );
    gbCursorX = 0;
    gbCursorY = 24;
    gbPrintString( "  SLOW" );
    gbCursorX = 0;
    gbCursorY = 32;
    gbPrintString( "  NORMAL" );
    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( "  FAST" );

    gbSetFont( gbFont3x5 );
    gbCursorX = 0;
    gbCursorY = 8;
    gbPrintString( "UP/DOWN to choose" );
    gbCursorX = 0;
    gbCursorY = 14;
    gbPrintString( "A to validate" );

    int pp;
    for( pp = 0; pp < 3; pp = pp + 1 )
    {
        gbCursorX = 49;
        gbCursorY = 25 + pp * 8;
        gbPrintString( "HIGH:" );
        gbCursorX = 70;
        gbPrintNumber( flapHighscore[ pp ] );
    }
}

// == upstream loop()'s own "else" branch (difficulty_menu == false): the C
// button and mute toggle are checked unconditionally, then background/
// pipes/ground/score are always drawn, then exactly one of PLAY/GAMEOVER/
// WIN runs - matching upstream's own structure line for line, including
// the one-extra-alive-frame quirk documented in this file's own header
// comment.
void flapUpdateActive()
{
    if( gbPressed( BTN_C ) )
    {
        flapBeginTitle();
        return;
    }
    if( gbPressed( BTN_B ) )
    {
        if( flapSoundOn ) flapSoundOn = false;
        else flapSoundOn = true;
    }

    flapDrawBackground();
    flapDrawPipes();
    flapDrawGround();
    flapDrawScore();

    if( flapState == FLAP_STATE_PLAY )
    {
        if( flapScore < FLAP_SCOREMAX )
        {
            flapPlayerMove();
            flapUpdatePipes();
        }
        else
          flapBeginWin();

        flapDrawPlayerAlive();
    }
    else if( flapState == FLAP_STATE_GAMEOVER )
    {
        flapDrawPlayerEnd();
        flapUpdateGameover();
    }
    else if( flapState == FLAP_STATE_WIN )
      flapUpdateWin();
}

void gameFlappyBirdo_init()
{
    gbBegin();

    flapHighscore[ 0 ] = eeprom_read_word( 0 );
    if( flapHighscore[ 0 ] == 0xFFFF ) flapHighscore[ 0 ] = 0;
    flapHighscore[ 1 ] = eeprom_read_word( 2 );
    if( flapHighscore[ 1 ] == 0xFFFF ) flapHighscore[ 1 ] = 0;
    flapHighscore[ 2 ] = eeprom_read_word( 4 );
    if( flapHighscore[ 2 ] == 0xFFFF ) flapHighscore[ 2 ] = 0;

    flapBeginTitle();
}

void gameFlappyBirdo_update()
{
    if( !gbUpdate() ) return;

    if( flapState == FLAP_STATE_TITLE ) flapUpdateTitle();
    else if( flapState == FLAP_STATE_MENU ) flapUpdateMenu();
    else flapUpdateActive();

    gbRenderFrame();
}
