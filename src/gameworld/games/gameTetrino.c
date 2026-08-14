#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Tetrino (j0ff / Joffrey Carlier, MIT license - no explicit license file
// in the repo, but every real source header states "This code is licensed
// under the MIT license: http://www.opensource.org/licenses/mit-license.php"
// verbatim, both in Joffrey Carlier's own gb_platform.h/.cpp and in the
// underlying STC engine's own game.h/game.cpp/platform.h by Laurens
// Rodriguez Oscanoa), github.com/j0ff/tetrino - a real Gamebuino Classic
// port of "STC: Simple Tetris Clone" (the same well-known small MIT-licensed
// Tetris engine several other platforms have ported), consolidated here from
// its real 4 upstream files (game.h/game.cpp - the platform-independent
// engine; gb_platform.h/gb_platform.cpp - the real Gamebuino-specific
// rendering/input/sound layer; tetrino.ino - the real sketch entry point).
//
// ---- The rotated-screen layout is preserved exactly, on purpose ----
// Real upstream draws the whole game in PORTRAIT orientation onto the
// physical 84x48 LANDSCAPE LCD (`PlatformGB`'s own `SCREEN_WIDTH =
// LCDHEIGHT`/`SCREEN_HEIGHT = LCDWIDTH` swap, confirmed directly in
// gb_platform.h) - every tile/title/number position below is copied
// verbatim from real upstream's own computed `static const int` layout
// constants (`BOARD_X`/`BOARD_Y`/`GRID_WIDTH`/`PREVIEW_X`/etc, in
// gb_platform.h), not redesigned, since the board's own real 90-degree
// rotation is genuinely load-bearing: real upstream's own button mapping
// ("Rotated Gamebuino: LEFT->DOWN, UP->LEFT, DOWN->RIGHT", a comment left
// directly in `PlatformGB::processEvents()`) only makes sense against this
// exact rotated pixel layout, and is preserved 1:1 here too (`tetUpdatePlay()`
// below maps physical BTN_LEFT/UP/DOWN/RIGHT to logical down/left/right
// move/drop exactly as upstream does) - "fixing" the rotation would break
// the controls' own real visual correspondence to what's on screen.
//
// ---- Dialect rewrites ----
// Every real `mGb->display.x(...)`/`mGb->sound.x(...)` call site (already
// itself a thin C++ wrapper the real author built around `gb.display`/
// `gb.sound`) was mechanically rewritten to a plain `gbX(...)` call, per
// this project's own established convention - `gb_platform.h`'s own
// documented purpose (map its own abstraction straight to the real
// underlying Gamebuino calls) made this a direct one-hop rewrite rather
// than needing to trace back through two layers. `StcTetromino`/`StcStatics`
// (real upstream `struct`s, each an instance-per-object concept: current
// falling piece, next preview piece, running stats) are flattened into
// separate prefixed globals (`tetFallCells`/`tetFallX`/`tetFallY`/
// `tetFallSize`/`tetFallType`, `tetNextCells`/`tetNextSize`/`tetNextType`,
// `tetScore`/`tetLines`/`tetLevel`) rather than ported as real `struct`
// instances - this dialect's one-word function-argument/return limit makes
// passing a whole `StcTetromino` in/out of a function impossible anyway (it
// is far larger than one word), and every real call site here only ever
// needs one specific field at a time, so per-field globals avoid ever
// needing an out-pointer rewrite in the first place. `Game`'s own private
// `mEvents` bitmask (`EVENT_MOVE_DOWN`/`EVENT_ROTATE_CW`/etc) is kept
// verbatim as `tetEvents` + `TET_EVT_*` bit constants - the bitmask design
// itself needed no rework, only the class-method call sites around it.
// `random(0, GB_MAX_INT-1) % TETROMINO_TYPES` (`PlatformGB::random()`)
// becomes a direct `arand(TET_TETROMINO_TYPES)` (this dialect's own
// established bounded-RNG helper, see avrCompat.h) - functionally identical
// (both ultimately produce a uniform value in [0, TETROMINO_TYPES)), just
// without the pointless intermediate mod-32767 step. `long` score/time
// fields became plain `int` (this dialect's own "prefer plain int in new
// port code" convention - `long` is a full 32-bit word here regardless, so
// nothing is lost).
//
// ---- millis() -> per-tick accumulator ----
// Real `PlatformGB::getSystemTime()` reads real `millis()`; this shim has
// no wall-clock primitive to match it against (same conversion this
// project's own gameBlocksBuino.c/gameSnakeAbc.c already established for
// this exact class of upstream timing code). Ported as `gbFrameCount *
// TET_MS_PER_TICK` - not an approximation but an exact match, since
// `gameTetrino_update()` only ever calls `tetUpdatePlay()` once per real
// `gbUpdate()`-gated tick (mirroring upstream's own `Game::update()` being
// called exactly once per real `gb.update()`-true tick too), and this
// shim's own frame rate is left at its real 20fps default (Tetrino never
// calls `gb.setFrameRate()` upstream) - 1000/20 = 50ms per tick, exactly
// `TET_MS_PER_TICK`. All of the delayed-autoshift (DAS) timers/falling-delay
// math ported below therefore behaves identically to real hardware, not
// just approximately.
//
// ---- processEvents()'s own event QUEUE collapses to direct per-tick checks ----
// Real `PlatformGB::pollButtonEvent()` builds a real keydown/keyup event
// queue on top of `mGb->buttons.pressed()`/`released()` purely to give
// `processEvents()` a uniform "for each event this tick" loop - but
// `gbPressed()`/`gbReleased()` here are already real single-tick edge
// pulses (confirmed by this project's own established `Buttons::update()`
// semantics elsewhere), so the whole queue is a pure indirection with zero
// behavioral difference from checking each button directly once per tick.
// `tetUpdatePlay()` below does exactly that (one `gbPressed()`/`gbReleased()`
// check per real upstream `case BTN_x:` branch), a direct, not simplified,
// port of the real dispatch table.
//
// ---- Real upstream features never actually enabled, and therefore dropped ----
// `STC_WALL_KICK_ENABLED`/`STC_SHOW_GHOST_PIECE`/`STC_AUTO_ROTATION` are
// real `#ifdef`-gated upstream features (wall-kick rotation, a ghost/shadow
// piece preview, and continuous rotation auto-repeat) - confirmed directly
// that none of the real 6 upstream source files ever `#define` any of the
// three, so all three are genuinely dead code in the real shipped game, not
// features this port is choosing to cut. `tetRotateTetromino()` below is
// therefore a direct port of `Game::rotateTetromino()`'s own real
// `#else` (non-wall-kick) collision-check branch, no shadow piece is ever
// computed or drawn, and rotation is a plain single-shot event with no
// repeat timer - all three exactly matching what the real shipped ROM
// actually does today.
//
// ---- Two more real, confirmed-dead upstream code paths, also dropped ----
// - `EVENT_RESTART`/`EVENT_SHOW_NEXT` are real `Game` event bits, but no
//   real button ever maps to either one (`processEvents()`'s own
//   `SDLK_F5`/`SDLK_F2` cases are commented out) - confirmed by reading
//   every real `case` in `PlatformGB::processEvents()` directly. This
//   means `mShowPreview` never actually toggles (always stays `true`, its
//   own `start()`-time initial value) and the once-over `if (mIsOver) { if
//   (RESTART) ... }` branch never fires - the real game genuinely freezes
//   on the Game Over screen forever, with physical Button B (`EVENT_QUIT`,
//   which bypasses the whole event-bit system by setting the error code
//   directly, so it isn't gated by `mIsOver` at all) the only real way out.
//   Ported as: the preview tetromino is always drawn unconditionally (no
//   `tetShowPreview` flag at all), and `tetUpdatePlay()` below has no
//   restart-from-game-over path either - matching the real, shipped,
//   frozen-until-B-quit behavior exactly, not a bug this port introduces.
// - `mStats.totalPieces`/`mStats.pieces[type]` are incremented on every
//   piece lock but never read anywhere else in the real source (every real
//   `drawNumber()` call that would have displayed them is commented out in
//   `PlatformGB::renderGame()`) - dropped outright as genuinely dead
//   bookkeeping, matching this project's own established precedent for
//   confirmed-dead upstream code (e.g. gameBlocksBuino.c's own dropped
//   `ShowDebug()`).
//
// ---- A real upstream quirk, confirmed harmless and normalized away ----
// `PlatformGB::renderGame()`'s own board-cell draw loop reads
// `mGame->fallingBlock().cells[i][j]` as the tile "color" to pass to
// `drawTile()`, where `i`/`j` range over the full 10x22 board - far
// outside the falling tetromino's own real 4x4 `cells` array bounds for
// almost every `(i,j)`, a genuine out-of-bounds read on real hardware.
// Confirmed harmless by tracing it through, not just assumed: `drawTile()`
// itself explicitly ignores its own `tile` parameter ("tile type not used
// as we only have one type of tile", a comment left directly in the real
// source) - the out-of-bounds value is read and then immediately discarded
// without ever being used for anything, on real hardware and here alike.
// `tetDrawTile()` below takes no tile-type parameter at all (it would be
// dead weight), so the board-cell draw loop simply doesn't read anything
// resembling this value in the first place - normalizing away a read with
// zero observable effect, not a gameplay-visible bug.
//
// ---- Quit-to-title, matching gamePong.c's own precedent exactly ----
// Real upstream's own `EVENT_QUIT` (Button B) doesn't return to any
// cartridge-level menu - it calls `Game::end()` (a no-op besides a real
// battery-indicator flag, dropped entirely here, same as every other port
// in this project) then immediately `Game::init()` again (a fresh game,
// silently started behind the scenes) followed by a fresh, blocking real
// `gb.titleScreen()` call in `tetrino.ino`'s own `loop()`. Ported as
// `tetBeginTitle()` on a Button B press, mirroring gamePong.c's own
// `pongBeginTitle()`-on-Button-C precedent exactly, including that same
// precedent's one accepted minor divergence: returning immediately skips
// this tick's own render call (`tetUpdatePlay()` returns before reaching
// `tetRenderPlay()`), where real hardware's own unconditional
// `mPlatform->renderGame()` at the tail of `Game::update()` would draw one
// last frame of the about-to-be-discarded board first. A harmless,
// imperceptible one-tick (50ms) difference, not a behavioral regression.
// The cartridge-level quit-confirmation dialog (Start, handled entirely in
// portVircon32.c, outside any game's own control) remains the only way
// back to the shared top-level menu, same as every other ported game.
//
// ---- Real bitmap art and sound, restored from the very first pass ----
// All 4 real title/game-over bitmaps (`bitmapScoreTitle`/`bitmapNextTitle`/
// `bitmapLevelTitle`/`bitmapGameOver`), all 10 real digit glyphs
// (`bitmap0`-`bitmap9`), and the real 64x30 startup logo bitmap (from
// `tetrino.ino`'s own `logo[]`, shown here on `tetUpdateTitle()`'s own
// custom title screen in place of the real, blocking `gb.titleScreen()`
// this dialect has no equivalent for - the same "explicit resumable title
// state" treatment gamePong.c's own header comment documents) are copied
// verbatim, byte-for-byte, from the real upstream `PROGMEM` tables -
// real `Display::drawBitmap()`'s own width/height-prefixed, row-major,
// MSB-first byte layout (confirmed directly against the real bytes: e.g.
// `bitmapNextTitle`'s declared 16x15 size means 2 bytes/row x 15 rows = 30
// data bytes, which is exactly what's there) is bit-for-bit identical to
// this shim's own `gbDrawBitmap()` format, so no re-encoding was needed at
// all, only a `PROGMEM byte[]` -> plain `int[]` mechanical conversion.
// Real sound (`playSoundFX()`'s own `soundfx[4][8]` "FX Synth" table,
// credited upstream to a yodasvideoarcade.com generator tool - the same
// table shape gameSnakeAbc.c/gameAsterocks.c/gameBlocksBuino.c already
// ported) is copied verbatim, and `tetPlaySoundFX()` is a full, faithful
// port of real `PlatformGB::playSoundFX(fxno, channel)` - every real
// `gb.sound.command()`/`playNote()` call restored (instrument/volume-slide/
// pitch-slide effect columns included, via `gbSoundCommand()`), on the same
// two real channels upstream routes to (`TET_SND_CHANNEL_1`/`_2`, matching
// real `SND_FX_CHANNEL_1`/`_2`), not just the pitch/duration approximation
// an earlier pass here shipped before this shim had a real
// `gbSoundCommand()`/`gbPlayNoteChannel()` primitive to call.

#define TET_TILE_SIZE 3
#define TET_BOARD_W 10
#define TET_BOARD_H 22
#define TET_BOARD_X 67   // TET_TILE_SIZE*TET_BOARD_H + 1
#define TET_BOARD_Y 1
#define TET_BOARD_Y2 31  // TET_BOARD_Y + TET_TILE_SIZE*TET_BOARD_W
#define TET_GRID_HEIGHT 60 // (TET_BOARD_H-2)*TET_TILE_SIZE
#define TET_GRID_WIDTH 30  // TET_BOARD_W*TET_TILE_SIZE
#define TET_SCORE_TITLE_X 77 // LCDWIDTH+1-8
#define TET_SCORE_TITLE_Y 0
#define TET_NEXT_TITLE_X 41 // TET_BOARD_X-10-16
#define TET_NEXT_TITLE_Y 33 // TET_BOARD_Y2+2
#define TET_PREVIEW_X 21 // TET_NEXT_TITLE_X-20
#define TET_PREVIEW_Y 34
#define TET_NUMBER_WIDTH 7
#define TET_SCORE_X 69 // TET_SCORE_TITLE_X-TET_NUMBER_WIDTH-1
#define TET_SCORE_Y 0
#define TET_SCORE_LENGTH 7
#define TET_LEVEL_TITLE_X 10 // real NUMBER_HEIGHT(9)+1
#define TET_LEVEL_TITLE_Y 33 // == TET_NEXT_TITLE_Y
#define TET_LEVEL_X 1
#define TET_LEVEL_Y 33
#define TET_LEVEL_LENGTH 2
#define TET_GAME_OVER_X 30
#define TET_GAME_OVER_Y 4
#define TET_GAME_OVER_W 20
#define TET_GAME_OVER_H 30

#define TET_TETROMINO_SIZE 4
#define TET_TETROMINO_TYPES 7
#define TET_EMPTY -1

#define TET_I 0
#define TET_O 1
#define TET_T 2
#define TET_S 3
#define TET_Z 4
#define TET_J 5
#define TET_L 6

#define TET_COLOR_CYAN   1
#define TET_COLOR_RED    2
#define TET_COLOR_BLUE   3
#define TET_COLOR_ORANGE 4
#define TET_COLOR_GREEN  5
#define TET_COLOR_YELLOW 6
#define TET_COLOR_PURPLE 7

#define TET_INIT_DELAY_FALL 1000
#define TET_SCORE_1_FILLED_ROW 400
#define TET_SCORE_2_FILLED_ROW 1000
#define TET_SCORE_3_FILLED_ROW 3000
#define TET_SCORE_4_FILLED_ROW 12000
#define TET_SCORE_MOVE_DOWN_DIVISOR 1000
#define TET_SCORE_DROP_DIVISOR 20
#define TET_FILLED_ROWS_FOR_LEVEL_UP 10
#define TET_DELAY_FACTOR_FOR_LEVEL_UP 9
#define TET_DELAY_DIVISOR_FOR_LEVEL_UP 10
#define TET_DAS_DELAY_TIMER 200
#define TET_DAS_MOVE_TIMER 40
#define TET_MS_PER_TICK 50 // this shim's real 20fps default = 1000/20 ms per logic tick

#define TET_EVT_MOVE_DOWN  1
#define TET_EVT_MOVE_LEFT  2
#define TET_EVT_MOVE_RIGHT 4
#define TET_EVT_ROTATE_CW  8
#define TET_EVT_DROP       32
#define TET_EVT_PAUSE      64

#define TET_SND_LINE_COMPLETED 0
#define TET_SND_ROTATE         1
#define TET_SND_GAME_OVER      2
#define TET_SND_PIECE_DROP     3

#define TET_SND_CHANNEL_1 0
#define TET_SND_CHANNEL_2 1

#define TET_STATE_TITLE 0
#define TET_STATE_PLAY  1

// ---- Real upstream bitmaps, copied verbatim (see this file's own header
// comment on the byte layout match with gbDrawBitmap()) ----

int tetScoreTitleBitmap[34]= { 8,32,0x4c,0xda,0xfe,0xfe,0x76,0x0,0x7c,0xfa,0xc6,0xc6,0x82,0x0,0x7c,0xfa,0xc6,0xc6,0xfe,0x7c,0x0,0x7c,0xfa,0xfe,0x32,0x72,0xfe,0xdc,0x0,0x7c,0xfa,0xd6,0xc6,0x44 };
int tetNextTitleBitmap[32]= { 16,15,0xc6,0x7c,0xea,0xfa,0x7e,0x7c,0x7c,0x10,0xee,0x7c,0xc6,0xfe,0x0,0x7c,0x4,0x0,0xa,0x7c,0xe,0xfa,0xfe,0xd6,0xfe,0xd6,0xe,0xc6,0xe,0x44,0x4,0x0 };
int tetLevelTitleBitmap[32]= { 16,15,0x0,0x7c,0x38,0xfa,0xe0,0xfe,0xe0,0xe0,0x38,0xe0,0x0,0x60,0x0,0x0,0x70,0x70,0xa8,0xa8,0xa8,0xa8,0x90,0x90,0x0,0x0,0xc2,0x0,0xfe,0x0,0x80,0x0 };
int tetGameOverBitmap[50]= { 16,24,0xf1,0xe0,0xfd,0xf8,0xc5,0x8,0xc5,0x48,0xfd,0xd8,0x0,0x0,0x3d,0xe0,0x41,0xf8,0x80,0x48,0xf0,0x78,0x7d,0xe0,0x0,0x0,0xf1,0xe0,0xfd,0xf8,0x94,0x8,0x95,0xf8,0x84,0x8,0x1,0xf8,0x0,0x0,0xfd,0xe0,0xf5,0xf8,0x15,0x28,0x1d,0x28,0xf1,0x8 };

int tetDigit0Bitmap[8]= { 8,6,0x7c,0xfa,0x86,0x86,0xfe,0x7c };
int tetDigit1Bitmap[8]= { 8,6,0x4,0xa,0xe,0xfe,0xfe,0x7c };
int tetDigit2Bitmap[8]= { 8,6,0xc4,0xe2,0xf6,0xfe,0xde,0xcc };
int tetDigit3Bitmap[8]= { 8,6,0x44,0xc2,0xd6,0xd6,0xfe,0x7c };
int tetDigit4Bitmap[8]= { 8,6,0x1c,0x1a,0x18,0xfe,0xfe,0x18 };
int tetDigit5Bitmap[8]= { 8,6,0xdc,0xda,0xde,0xd6,0xf6,0x64 };
int tetDigit6Bitmap[8]= { 8,6,0xfc,0xfa,0x9e,0x96,0xf6,0x64 };
int tetDigit7Bitmap[8]= { 8,6,0x4,0x2,0xe6,0xf6,0x1e,0xe };
int tetDigit8Bitmap[8]= { 8,6,0x7c,0xfa,0xd6,0xd6,0xfe,0x7c };
int tetDigit9Bitmap[8]= { 8,6,0x1c,0xda,0xd6,0xd6,0xfe,0x7c };

// Direct port of upstream's own real `const byte *spritesNumbers[10]`
// pointer table - a real `int*[10] name = {...}` array declaration,
// already proven working in this project (see game2048.c's own
// `g2048TileSprites`/gameDescent.c's own `descentPlayerSprites`).
int* tetDigitBitmaps[10]= { tetDigit0Bitmap, tetDigit1Bitmap, tetDigit2Bitmap, tetDigit3Bitmap, tetDigit4Bitmap, tetDigit5Bitmap, tetDigit6Bitmap, tetDigit7Bitmap, tetDigit8Bitmap, tetDigit9Bitmap };

// Real startup logo (`tetrino.ino`'s own `logo[]`), shown on this port's
// own custom title screen in place of the real, blocking `gb.titleScreen()`.
int tetLogoBitmap[242]= {
    64,30,0x0,0x8,0x0,0x8,0x0,0x80,0x0,0x0,0x0,0x8,0x0,0x18,0x1,0x80,
    0x0,0x0,0x0,0x18,0x0,0x1c,0x1,0x40,0x0,0x0,0x0,0x38,0x0,0x34,0x3,0xc0,
    0x0,0x0,0x0,0x6c,0x0,0x3c,0x7,0x40,0x0,0x0,0x0,0x7c,0x0,0x3e,0x7,0xa0,
    0x0,0x0,0x0,0xff,0xff,0xff,0xff,0xff,0xf0,0x0,0x0,0xc0,0x0,0x0,0x0,0x0,
    0x10,0x0,0x1,0xff,0xf9,0xff,0xef,0xff,0xb0,0x0,0x3,0xff,0xf9,0xff,0xcf,0xff,
    0x30,0x0,0x7,0xff,0xf1,0xff,0xdf,0xff,0x30,0x0,0x7,0xff,0xf1,0xff,0x9f,0xfe,
    0x70,0x0,0xf,0xff,0xf3,0xff,0xbf,0xfe,0x70,0x0,0x1f,0xff,0xe3,0xff,0xbf,0xfc,
    0xf0,0x0,0x10,0x0,0x0,0x0,0x0,0x0,0xf0,0x0,0x10,0x0,0x0,0x0,0x3f,0xff,
    0xf0,0x0,0x1f,0xff,0x9f,0xfe,0x7f,0xff,0xf0,0x0,0x1f,0xff,0xbf,0xfc,0x7f,0xff,
    0xe0,0x0,0x1d,0xff,0x3f,0xfd,0x7f,0xff,0xe0,0x0,0x11,0x5f,0x7f,0xf9,0x7f,0xff,
    0xc0,0x0,0x0,0xc,0x7f,0xfb,0x7f,0xff,0xc0,0x0,0x0,0xc,0xff,0xfb,0x7f,0xff,
    0x80,0x0,0x0,0x1c,0xff,0xf3,0x7f,0xff,0x80,0x0,0x0,0x1,0xff,0xf7,0x7f,0xff,
    0x80,0x0,0x0,0x0,0x0,0x7,0x0,0x0,0x0,0x0,0x0,0x7,0xff,0xff,0x0,0x0,
    0x0,0x0,0x0,0x7,0xff,0xff,0x0,0x0,0x0,0x0,0x0,0x7,0x9f,0xbf,0x0,0x0,
    0x0,0x0,0x0,0x6,0x14,0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2f,0x0,0x0,
    0x0,0x0
};

// Real upstream "FX Synth" table (credited upstream to a yodasvideoarcade.com
// generator tool), copied verbatim - see this file's own header comment.
int tetSoundFx[4][8]=
{
    {0,45,26,1,0,1,7,10}, // 0: line completed
    {0,33,53,1,0,5,7,3},  // 1: rotate
    {0,14,46,6,0,0,7,11}, // 2: game over
    {1,1,0,0,0,0,7,2},    // 3: piece drop
};

// ---- Board / tetromino state (flattened from real upstream StcTetromino/
// StcStatics per-instance structs into plain globals - see this file's own
// header comment) ----

int tetMap[10][22]; // [x][y], TET_EMPTY = empty cell, matches real mMap exactly

int tetFallCells[4][4];
int tetFallX;
int tetFallY;
int tetFallSize;
int tetFallType;

int tetNextCells[4][4];
int tetNextSize;
int tetNextType;

int tetScore;
int tetLines;
int tetLevel;

bool tetIsOver;
bool tetIsPaused;
bool tetPlayedGameOverSnd;

int tetFallingDelay;  // ms between automatic downward moves
int tetSystemTime;    // ms, gbFrameCount * TET_MS_PER_TICK as of the last tick
int tetLastFallTime;  // ms, last time the falling tetromino dropped

int tetDelayLeft;  // delayed-autoshift (DAS) timers, ms; -1 = inactive
int tetDelayRight;
int tetDelayDown;

int tetEvents; // TET_EVT_* bitmask, mirrors real upstream mEvents exactly

int tetState; // TET_STATE_TITLE / TET_STATE_PLAY

int tetRandomPieceType()
{
    return arand( TET_TETROMINO_TYPES );
}

// Direct port of real PlatformGB::playSoundFX(fxno, channel) - every real
// gb.sound.command()/playNote() call restored verbatim (see this file's own
// header comment on tetSoundFx and the real column layout).
void tetPlaySoundFX( int fxno, int channel )
{
    gbSoundCommand( GB_CMD_VOLUME, tetSoundFx[fxno][6], 0, channel );
    gbSoundCommand( GB_CMD_INSTRUMENT, tetSoundFx[fxno][0], 0, channel );
    gbSoundCommand( GB_CMD_SLIDE, tetSoundFx[fxno][5], -tetSoundFx[fxno][4], channel );
    gbSoundCommand( GB_CMD_ARPEGGIO, tetSoundFx[fxno][3], tetSoundFx[fxno][2] - 58, channel );
    gbPlayNoteChannel( tetSoundFx[fxno][1], tetSoundFx[fxno][7], channel );
}

// Direct port of real Game::setTetromino(), specialized for the falling
// piece (see this file's own header comment on why the real struct-based
// version was split into two specialized functions instead of one taking
// a struct in/out pointer).
void tetNewFallingTetromino( int type )
{
    int i;
    int j;

    for( i = 0; i < TET_TETROMINO_SIZE; i = i + 1 )
      for( j = 0; j < TET_TETROMINO_SIZE; j = j + 1 )
        tetFallCells[i][j] = TET_EMPTY;

    tetFallSize = TET_TETROMINO_SIZE - 1;

    if( type == TET_I )
    {
        tetFallCells[0][1] = TET_COLOR_CYAN;
        tetFallCells[1][1] = TET_COLOR_CYAN;
        tetFallCells[2][1] = TET_COLOR_CYAN;
        tetFallCells[3][1] = TET_COLOR_CYAN;
        tetFallSize = TET_TETROMINO_SIZE;
    }
    else if( type == TET_O )
    {
        tetFallCells[0][0] = TET_COLOR_YELLOW;
        tetFallCells[0][1] = TET_COLOR_YELLOW;
        tetFallCells[1][0] = TET_COLOR_YELLOW;
        tetFallCells[1][1] = TET_COLOR_YELLOW;
        tetFallSize = TET_TETROMINO_SIZE - 2;
    }
    else if( type == TET_T )
    {
        tetFallCells[0][1] = TET_COLOR_PURPLE;
        tetFallCells[1][0] = TET_COLOR_PURPLE;
        tetFallCells[1][1] = TET_COLOR_PURPLE;
        tetFallCells[2][1] = TET_COLOR_PURPLE;
    }
    else if( type == TET_S )
    {
        tetFallCells[0][1] = TET_COLOR_GREEN;
        tetFallCells[1][0] = TET_COLOR_GREEN;
        tetFallCells[1][1] = TET_COLOR_GREEN;
        tetFallCells[2][0] = TET_COLOR_GREEN;
    }
    else if( type == TET_Z )
    {
        tetFallCells[0][0] = TET_COLOR_RED;
        tetFallCells[1][0] = TET_COLOR_RED;
        tetFallCells[1][1] = TET_COLOR_RED;
        tetFallCells[2][1] = TET_COLOR_RED;
    }
    else if( type == TET_J )
    {
        tetFallCells[0][0] = TET_COLOR_BLUE;
        tetFallCells[0][1] = TET_COLOR_BLUE;
        tetFallCells[1][1] = TET_COLOR_BLUE;
        tetFallCells[2][1] = TET_COLOR_BLUE;
    }
    else if( type == TET_L )
    {
        tetFallCells[0][1] = TET_COLOR_ORANGE;
        tetFallCells[1][1] = TET_COLOR_ORANGE;
        tetFallCells[2][0] = TET_COLOR_ORANGE;
        tetFallCells[2][1] = TET_COLOR_ORANGE;
    }

    tetFallType = type;
}

// Same as tetNewFallingTetromino() above, but for the preview/"next" piece.
void tetNewNextTetromino( int type )
{
    int i;
    int j;

    for( i = 0; i < TET_TETROMINO_SIZE; i = i + 1 )
      for( j = 0; j < TET_TETROMINO_SIZE; j = j + 1 )
        tetNextCells[i][j] = TET_EMPTY;

    tetNextSize = TET_TETROMINO_SIZE - 1;

    if( type == TET_I )
    {
        tetNextCells[0][1] = TET_COLOR_CYAN;
        tetNextCells[1][1] = TET_COLOR_CYAN;
        tetNextCells[2][1] = TET_COLOR_CYAN;
        tetNextCells[3][1] = TET_COLOR_CYAN;
        tetNextSize = TET_TETROMINO_SIZE;
    }
    else if( type == TET_O )
    {
        tetNextCells[0][0] = TET_COLOR_YELLOW;
        tetNextCells[0][1] = TET_COLOR_YELLOW;
        tetNextCells[1][0] = TET_COLOR_YELLOW;
        tetNextCells[1][1] = TET_COLOR_YELLOW;
        tetNextSize = TET_TETROMINO_SIZE - 2;
    }
    else if( type == TET_T )
    {
        tetNextCells[0][1] = TET_COLOR_PURPLE;
        tetNextCells[1][0] = TET_COLOR_PURPLE;
        tetNextCells[1][1] = TET_COLOR_PURPLE;
        tetNextCells[2][1] = TET_COLOR_PURPLE;
    }
    else if( type == TET_S )
    {
        tetNextCells[0][1] = TET_COLOR_GREEN;
        tetNextCells[1][0] = TET_COLOR_GREEN;
        tetNextCells[1][1] = TET_COLOR_GREEN;
        tetNextCells[2][0] = TET_COLOR_GREEN;
    }
    else if( type == TET_Z )
    {
        tetNextCells[0][0] = TET_COLOR_RED;
        tetNextCells[1][0] = TET_COLOR_RED;
        tetNextCells[1][1] = TET_COLOR_RED;
        tetNextCells[2][1] = TET_COLOR_RED;
    }
    else if( type == TET_J )
    {
        tetNextCells[0][0] = TET_COLOR_BLUE;
        tetNextCells[0][1] = TET_COLOR_BLUE;
        tetNextCells[1][1] = TET_COLOR_BLUE;
        tetNextCells[2][1] = TET_COLOR_BLUE;
    }
    else if( type == TET_L )
    {
        tetNextCells[0][1] = TET_COLOR_ORANGE;
        tetNextCells[1][1] = TET_COLOR_ORANGE;
        tetNextCells[2][0] = TET_COLOR_ORANGE;
        tetNextCells[2][1] = TET_COLOR_ORANGE;
    }

    tetNextType = type;
}

// Direct port of real PlatformGB::drawTile() - the real `tile`/`shadow`
// parameters are dropped (see this file's own header comment on why).
void tetDrawTile( int x, int y )
{
    gbDrawRect( x, y, TET_TILE_SIZE, TET_TILE_SIZE );
}

// Direct port of real PlatformGB::drawNumber().
void tetDrawNumber( int x, int y, int number, int length )
{
    int pos;
    int dy;
    int digit;

    pos = 0;
    do
    {
        dy = y + TET_NUMBER_WIDTH * ( length - 1 - pos );
        digit = number % 10;
        gbDrawBitmap( x, dy, tetDigitBitmaps[digit] );
        number = number / 10;
        pos = pos + 1;
    } while( pos < length );
}

// Direct port of real Game::checkCollision() - always checks the current
// falling tetromino against a hypothetical (dx,dy) move.
bool tetCheckCollision( int dx, int dy )
{
    int newx;
    int newy;
    int i;
    int j;

    newx = tetFallX + dx;
    newy = tetFallY + dy;

    for( i = 0; i < tetFallSize; i = i + 1 )
    {
        for( j = 0; j < tetFallSize; j = j + 1 )
        {
            if( tetFallCells[i][j] != TET_EMPTY )
            {
                if( ( newx + i < 0 ) || ( newx + i >= TET_BOARD_W ) || ( newy + j >= TET_BOARD_H ) )
                  return true;

                if( tetMap[newx + i][newy + j] != TET_EMPTY )
                  return true;
            }
        }
    }
    return false;
}

// Direct port of real Game::onFilledRows() - the real default/assert branch
// (filledRows outside 1-4) is dropped as genuinely unreachable: a single
// tetromino lock can clear at most 4 rows, matching upstream's own "This
// shouldn't happen" comment on its own dead branch.
void tetOnFilledRows( int filledRows )
{
    tetLines = tetLines + filledRows;

    if( filledRows == 1 )
      tetScore = tetScore + TET_SCORE_1_FILLED_ROW * ( tetLevel + 1 );
    else if( filledRows == 2 )
      tetScore = tetScore + TET_SCORE_2_FILLED_ROW * ( tetLevel + 1 );
    else if( filledRows == 3 )
      tetScore = tetScore + TET_SCORE_3_FILLED_ROW * ( tetLevel + 1 );
    else if( filledRows == 4 )
      tetScore = tetScore + TET_SCORE_4_FILLED_ROW * ( tetLevel + 1 );

    if( tetLines >= TET_FILLED_ROWS_FOR_LEVEL_UP * ( tetLevel + 1 ) )
    {
        tetLevel = tetLevel + 1;
        tetFallingDelay = ( TET_DELAY_FACTOR_FOR_LEVEL_UP * tetFallingDelay ) / TET_DELAY_DIVISOR_FOR_LEVEL_UP;
    }

    tetPlaySoundFX( TET_SND_LINE_COMPLETED, TET_SND_CHANNEL_2 );
}

// Direct port of real Game::moveTetromino().
void tetMoveTetromino( int dx, int dy )
{
    int i;
    int j;
    int row;
    int col;
    int aboveRow;
    int numFilledRows;
    bool hasFullRow;

    if( tetCheckCollision( dx, dy ) )
    {
        if( dy == 1 )
        {
            if( tetFallY <= 1 )
            {
                tetIsOver = true;
            }
            else
            {
                // Lock the falling tetromino's cells into the board
                for( i = 0; i < tetFallSize; i = i + 1 )
                {
                    for( j = 0; j < tetFallSize; j = j + 1 )
                    {
                        if( tetFallCells[i][j] != TET_EMPTY )
                          tetMap[tetFallX + i][tetFallY + j] = tetFallCells[i][j];
                    }
                }

                // Find and remove filled rows
                numFilledRows = 0;
                for( row = 1; row < TET_BOARD_H; row = row + 1 )
                {
                    hasFullRow = true;
                    for( col = 0; col < TET_BOARD_W; col = col + 1 )
                    {
                        if( tetMap[col][row] == TET_EMPTY )
                        {
                            hasFullRow = false;
                            col = TET_BOARD_W; // stop scanning this row
                        }
                    }

                    if( hasFullRow )
                    {
                        for( col = 0; col < TET_BOARD_W; col = col + 1 )
                          for( aboveRow = row; aboveRow > 0; aboveRow = aboveRow - 1 )
                            tetMap[col][aboveRow] = tetMap[col][aboveRow - 1];
                        numFilledRows = numFilledRows + 1;
                    }
                }

                if( numFilledRows > 0 )
                  tetOnFilledRows( numFilledRows );

                // The preview piece becomes the new falling piece
                for( i = 0; i < TET_TETROMINO_SIZE; i = i + 1 )
                  for( j = 0; j < TET_TETROMINO_SIZE; j = j + 1 )
                    tetFallCells[i][j] = tetNextCells[i][j];
                tetFallSize = tetNextSize;
                tetFallType = tetNextType;

                tetFallY = 0;
                tetFallX = ( TET_BOARD_W - tetFallSize ) / 2;

                tetNewNextTetromino( tetRandomPieceType() );
            }
        }
    }
    else
    {
        tetFallX = tetFallX + dx;
        tetFallY = tetFallY + dy;
    }
}

// Direct port of real Game::rotateTetromino() - the real
// STC_WALL_KICK_ENABLED branch is never compiled upstream (see this file's
// own header comment), so only the plain collision-check branch is ported.
void tetRotateTetromino( bool clockwise )
{
    int rotated[4][4];
    int i;
    int j;

    if( tetFallType == TET_O )
      return; // rotating TET_O never changes anything, matches upstream exactly

    for( i = 0; i < TET_TETROMINO_SIZE; i = i + 1 )
      for( j = 0; j < TET_TETROMINO_SIZE; j = j + 1 )
        rotated[i][j] = TET_EMPTY;

    for( i = 0; i < tetFallSize; i = i + 1 )
    {
        for( j = 0; j < tetFallSize; j = j + 1 )
        {
            if( clockwise )
              rotated[tetFallSize - j - 1][i] = tetFallCells[i][j];
            else
              rotated[j][tetFallSize - i - 1] = tetFallCells[i][j];
        }
    }

    for( i = 0; i < tetFallSize; i = i + 1 )
    {
        for( j = 0; j < tetFallSize; j = j + 1 )
        {
            if( rotated[i][j] != TET_EMPTY )
            {
                if( ( tetFallX + i < 0 ) || ( tetFallX + i >= TET_BOARD_W ) || ( tetFallY + j >= TET_BOARD_H ) )
                  return;

                if( tetMap[i + tetFallX][j + tetFallY] != TET_EMPTY )
                  return;
            }
        }
    }

    for( i = 0; i < TET_TETROMINO_SIZE; i = i + 1 )
      for( j = 0; j < TET_TETROMINO_SIZE; j = j + 1 )
        tetFallCells[i][j] = rotated[i][j];
}

// Direct port of real Game::dropTetromino() (the real non-ghost-piece
// branch - see this file's own header comment).
void tetDropTetromino()
{
    int y;

    y = 1;
    while( !tetCheckCollision( 0, y ) )
      y = y + 1;

    tetMoveTetromino( 0, y - 1 );
    tetMoveTetromino( 0, 1 ); // force lock

    tetScore = tetScore + ( TET_SCORE_2_FILLED_ROW * ( tetLevel + 1 ) ) / TET_SCORE_DROP_DIVISOR;
    tetPlaySoundFX( TET_SND_PIECE_DROP, TET_SND_CHANNEL_1 );
}

// Direct port of real Game::start().
void tetStartGame()
{
    int x;
    int y;

    tetSystemTime = gbFrameCount * TET_MS_PER_TICK;
    tetLastFallTime = tetSystemTime;
    tetIsOver = false;
    tetIsPaused = false;
    tetPlayedGameOverSnd = false;
    tetEvents = 0;
    tetFallingDelay = TET_INIT_DELAY_FALL;

    tetScore = 0;
    tetLines = 0;
    tetLevel = 0;

    for( x = 0; x < TET_BOARD_W; x = x + 1 )
      for( y = 0; y < TET_BOARD_H; y = y + 1 )
        tetMap[x][y] = TET_EMPTY;

    tetNewFallingTetromino( tetRandomPieceType() );
    tetFallX = ( TET_BOARD_W - tetFallSize ) / 2;
    tetFallY = 0;

    tetNewNextTetromino( tetRandomPieceType() );

    tetDelayLeft = -1;
    tetDelayRight = -1;
    tetDelayDown = -1;
}

void tetBeginTitle()
{
    tetState = TET_STATE_TITLE;
}

void tetBeginPlay()
{
    tetStartGame();
    tetState = TET_STATE_PLAY;
}

// This port's own custom title screen, replacing real upstream's blocking
// `gb.titleScreen(F("Tetrino by Joff (STC)"), logo)` call (this dialect has
// no equivalent - see this file's own header comment), matching gamePong.c's
// own "explicit resumable title state" precedent. The real logo bitmap is
// restored verbatim.
void tetUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 10, 2, tetLogoBitmap );
    gbCursorX = 28;
    gbCursorY = 38;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      tetBeginPlay();
}

// Direct port of real PlatformGB::renderGame() (the real `hasChanged()`
// dirty-flag gate is dropped - this port redraws unconditionally every
// tick regardless, matching this project's own established precedent for
// why that gate has no equivalent here - see this project's own CLAUDE.md).
void tetRenderPlay()
{
    int i;
    int j;

    gbSetColor( GB_BLACK );

    // Board background lines
    gbDrawFastVLine( 0, 1, TET_GRID_WIDTH );
    gbDrawFastHLine( 0, 0, TET_GRID_HEIGHT );
    gbDrawFastHLine( 0, TET_GRID_WIDTH + 1, TET_GRID_HEIGHT );

    // Title bitmaps
    gbDrawBitmap( TET_SCORE_TITLE_X, TET_SCORE_TITLE_Y, tetScoreTitleBitmap );
    gbDrawBitmap( TET_NEXT_TITLE_X, TET_NEXT_TITLE_Y, tetNextTitleBitmap );
    gbDrawBitmap( TET_LEVEL_TITLE_X, TET_LEVEL_TITLE_Y, tetLevelTitleBitmap );

    // Preview ("next") tetromino - always shown, see this file's own header comment
    for( i = 0; i < TET_TETROMINO_SIZE; i = i + 1 )
    {
        for( j = 0; j < TET_TETROMINO_SIZE; j = j + 1 )
        {
            if( tetNextCells[i][j] != TET_EMPTY )
            {
                tetDrawTile( TET_PREVIEW_X + ( TET_TILE_SIZE * ( TET_TETROMINO_SIZE - j ) ),
                             TET_PREVIEW_Y + ( TET_TILE_SIZE * i ) );
            }
        }
    }

    // Locked cells already on the board
    for( i = 0; i < TET_BOARD_W; i = i + 1 )
    {
        for( j = 0; j < TET_BOARD_H; j = j + 1 )
        {
            if( tetMap[i][j] != TET_EMPTY )
            {
                tetDrawTile( TET_BOARD_X - ( TET_TILE_SIZE * ( j + 1 ) ), TET_BOARD_Y + ( TET_TILE_SIZE * i ) );
            }
        }
    }

    // Falling tetromino
    for( i = 0; i < TET_TETROMINO_SIZE; i = i + 1 )
    {
        for( j = 0; j < TET_TETROMINO_SIZE; j = j + 1 )
        {
            if( tetFallCells[i][j] != TET_EMPTY )
            {
                tetDrawTile( TET_BOARD_X - ( TET_TILE_SIZE * ( tetFallY + j + 1 ) ),
                             TET_BOARD_Y + ( TET_TILE_SIZE * ( tetFallX + i ) ) );
            }
        }
    }

    if( !tetIsPaused )
    {
        tetDrawNumber( TET_LEVEL_X, TET_LEVEL_Y, tetLevel, TET_LEVEL_LENGTH );
        tetDrawNumber( TET_SCORE_X, TET_SCORE_Y, tetScore, TET_SCORE_LENGTH );
    }

    if( tetIsOver )
    {
        if( !tetPlayedGameOverSnd )
        {
            tetPlaySoundFX( TET_SND_GAME_OVER, TET_SND_CHANNEL_1 );
            tetPlayedGameOverSnd = true;
        }
        gbSetColor( GB_WHITE );
        gbFillRect( TET_GAME_OVER_X - 3, TET_GAME_OVER_Y - 3, TET_GAME_OVER_W, TET_GAME_OVER_H );
        gbSetColorBg( GB_BLACK, GB_WHITE );
        gbDrawBitmap( TET_GAME_OVER_X, TET_GAME_OVER_Y, tetGameOverBitmap );
    }
}

// Direct port of real Game::update() + PlatformGB::processEvents() fused
// together (see this file's own header comment on why the real event
// queue collapses to direct per-tick checks).
void tetUpdatePlay()
{
    int currentTime;
    int timeDelta;

    // Real EVENT_QUIT (Button B) - bypasses the whole event-bit system,
    // exactly like real upstream's own onEventStart(EVENT_QUIT).
    if( gbPressed( BTN_B ) )
    {
        tetBeginTitle();
        return;
    }

    // Real physical-to-logical button remap ("Rotated Gamebuino: LEFT->DOWN,
    // UP->LEFT, DOWN->RIGHT" - see this file's own header comment)
    if( gbPressed( BTN_LEFT ) )
    {
        tetEvents = tetEvents | TET_EVT_MOVE_DOWN;
        tetDelayDown = TET_DAS_DELAY_TIMER;
    }
    if( gbReleased( BTN_LEFT ) )
      tetDelayDown = -1;

    if( gbPressed( BTN_C ) )
    {
        tetEvents = tetEvents | TET_EVT_ROTATE_CW;
        if( !tetIsOver )
          tetPlaySoundFX( TET_SND_ROTATE, TET_SND_CHANNEL_1 );
    }

    if( gbPressed( BTN_UP ) )
    {
        tetEvents = tetEvents | TET_EVT_MOVE_LEFT;
        tetDelayLeft = TET_DAS_DELAY_TIMER;
    }
    if( gbReleased( BTN_UP ) )
      tetDelayLeft = -1;

    if( gbPressed( BTN_DOWN ) )
    {
        tetEvents = tetEvents | TET_EVT_MOVE_RIGHT;
        tetDelayRight = TET_DAS_DELAY_TIMER;
    }
    if( gbReleased( BTN_DOWN ) )
      tetDelayRight = -1;

    if( gbPressed( BTN_RIGHT ) )
      tetEvents = tetEvents | TET_EVT_DROP;

    if( gbPressed( BTN_A ) )
      tetEvents = tetEvents | TET_EVT_PAUSE;

    // Real upstream's own `if (mIsOver) { if (RESTART) ... }` branch is
    // dropped (see this file's own header comment) - once over, the board
    // is genuinely frozen and only Button B (handled above) escapes it.
    if( !tetIsOver )
    {
        currentTime = gbFrameCount * TET_MS_PER_TICK;
        timeDelta = currentTime - tetSystemTime;

        if( tetDelayDown > 0 )
        {
            tetDelayDown = tetDelayDown - timeDelta;
            if( tetDelayDown <= 0 )
            {
                tetDelayDown = TET_DAS_MOVE_TIMER;
                tetEvents = tetEvents | TET_EVT_MOVE_DOWN;
            }
        }
        if( tetDelayLeft > 0 )
        {
            tetDelayLeft = tetDelayLeft - timeDelta;
            if( tetDelayLeft <= 0 )
            {
                tetDelayLeft = TET_DAS_MOVE_TIMER;
                tetEvents = tetEvents | TET_EVT_MOVE_LEFT;
            }
        }
        else if( tetDelayRight > 0 )
        {
            tetDelayRight = tetDelayRight - timeDelta;
            if( tetDelayRight <= 0 )
            {
                tetDelayRight = TET_DAS_MOVE_TIMER;
                tetEvents = tetEvents | TET_EVT_MOVE_RIGHT;
            }
        }

        if( ( tetEvents & TET_EVT_PAUSE ) != 0 )
        {
            tetIsPaused = !tetIsPaused;
            tetEvents = 0;
        }

        if( tetIsPaused )
        {
            // Real upstream freezes the fall timer while paused by adding
            // the elapsed time onto mLastFallTime directly.
            tetLastFallTime = tetLastFallTime + ( currentTime - tetSystemTime );
        }
        else
        {
            if( tetEvents != 0 )
            {
                if( ( tetEvents & TET_EVT_DROP ) != 0 )
                  tetDropTetromino();

                if( ( tetEvents & TET_EVT_ROTATE_CW ) != 0 )
                  tetRotateTetromino( true );

                if( ( tetEvents & TET_EVT_MOVE_RIGHT ) != 0 )
                  tetMoveTetromino( 1, 0 );
                else if( ( tetEvents & TET_EVT_MOVE_LEFT ) != 0 )
                  tetMoveTetromino( -1, 0 );

                if( ( tetEvents & TET_EVT_MOVE_DOWN ) != 0 )
                {
                    tetScore = tetScore + ( TET_SCORE_2_FILLED_ROW * ( tetLevel + 1 ) ) / TET_SCORE_MOVE_DOWN_DIVISOR;
                    tetMoveTetromino( 0, 1 );
                }
                tetEvents = 0;
            }

            if( currentTime - tetLastFallTime >= tetFallingDelay )
            {
                tetMoveTetromino( 0, 1 );
                tetLastFallTime = currentTime;
            }
        }
        tetSystemTime = currentTime;
    }

    tetRenderPlay();
}

void gameTetrino_init()
{
    gbBegin();
    tetBeginTitle();
}

void gameTetrino_update()
{
    if( !gbUpdate() ) return;

    if( tetState == TET_STATE_TITLE ) tetUpdateTitle();
    else tetUpdatePlay();

    gbRenderFrame();
}
