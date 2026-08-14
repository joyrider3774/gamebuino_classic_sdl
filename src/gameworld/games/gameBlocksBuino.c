#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// BlocksBuino (frthery, license: None specified -
// github.com/frthery/BlocksBuino - same author as this project's own
// gameSnakeAbc.c, see that file's own header comment for the same
// author's own recurring UI conventions, several of which reappear here
// verbatim). A Tetris-style falling-block game: the 7 standard tetromino
// shapes (line/cube/T/L/L-mirrored/S/S-mirrored) drop down a 10x16 well,
// move/rotate them with the D-pad/Button A, clear full rows for points,
// 9 selectable starting speed levels via an in-game level-select menu
// (Button C, mid-play, exactly like gameSnakeAbc.c's own GameMenu()).
// Source spread across 4 real upstream .ino tabs sharing one translation
// unit (blocksBuino.ino/rotation.ino/sounds.ino/sprites.ino) - all 4 read
// in full before porting.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (see gamePong.c's own header comment for
// why - this dialect has no classes/methods). Every global got a
// `blk`-prefixed name (checked against every other `src/games/*.c` file
// first, including the concurrently-ported batch - not already used).
// Upstream's own `boolean`/`short` all became plain `bool`/`int` (this
// dialect's own real primitives). `random(1, 8)` (Arduino's ranged
// random, exclusive of its upper bound) became `arand(7) + 1` (this
// dialect's own established RNG helper). `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op; `gb.battery.show = false;`
// was dropped outright - both match every other port's own identical
// treatment.
//
// ---- Array declarations (TYPE[N] name, not TYPE name[N]) ----
// Upstream's own `boolean blocks_activation[BLOCKS_MAX_Y][BLOCKS_MAX_X]`
// (the well/board grid) and `boolean lines_completions[BLOCKS_MAX_Y]`
// became `int[16][10] blkBlocksActivation` / `int blkLinesCompletions[16]`
// (0/1 flags) rather than `bool[][]`/`bool[]` - genuine 2D *int* arrays
// are already proven to work fine in this dialect (see gameConduit.c's
// own `int[6][8] condMap`/gameAsterocks.c's own `int[20][9]
// asterShipFrames`), but no `bool[N]` or `bool[N][M]` array has been
// proven anywhere in this project yet, so `int` (with explicit `0`/`1`
// assignments, never a bare `true`/`false` stored into either array) was
// used defensively instead of gambling on an unproven combination. The 4
// falling-piece cell arrays (`player_blocks1..4[2]`, rotation-point
// row/col pairs) became `int blkPlayerBlocks1[2]..4`, matching
// gameSnakeAbc.c's own identical `int sabcSnakeX[100]`-style precedent.
//
// ---- switch -> if/else-if (no switch statements proven to work) ----
// Two real upstream `switch(player_blocks_type)`/`switch
// (player_blocks_current_type)` statements (the next-piece preview shape
// in DrawNextBlocks(), and the rotation-function dispatch in
// RotateBlocks()) were both converted to if/else-if chains, every case
// preserved. Upstream's own rotation dispatch switch has NO case at all
// for BLOCKS_CUBE (cubes never rotate) and no `default` - ported exactly
// the same way: `blkRotateBlocks()`'s if/else-if chain simply has no
// branch for BLK_CUBE either, so pressing A while a cube is falling is a
// silent no-op, matching real hardware exactly. The preview-shape switch
// DOES have a `default` (an exact literal duplicate of upstream's own
// BLOCKS_CUBE preview, but with each cell's column shifted by one versus
// the real CUBE case - a genuine upstream inconsistency, verified by
// diffing the two bodies directly) - ported as the trailing `else`
// clause verbatim, unreachable in practice since `player_blocks_type` is
// always 1-7 from `arand(7)+1`, kept only for 1:1 fidelity.
//
// ---- Bitmap - restored as a real gbDrawBitmap() call ----
// The real upstream 64x36 title `logo[]` (shown via `gb.titleScreen
// (logo)`) is restored via this project's own gbDrawBitmap() primitive as
// `blkTitleBitmap`. Upstream's own literal bytes use Arduino's
// `B00000000`-style binary notation, not valid syntax in this dialect -
// converted to hex via a small Python script (regexed every `B[01]{8}`
// token out of the real sprites.ino and re-emitted as `hex(int(bits,
// 2))`), matching gameSnakeAbc.c's own identical conversion method for
// its own title bitmap. Spot-checked by hand: the first data byte
// `B00111110` -> 62 decimal -> `0x3e` (script output's first data byte,
// confirmed correct), and the very last row's `B11111111,B11111111,
// B11100000` -> `0xff,0xff,0xe0` (script output's last 3 bytes, confirmed
// correct: 0b11100000 = 224 = 0xe0). No GRAY mask/fill layer precedes
// this bitmap anywhere in the real source (only one drawBitmap call
// exists in the whole game) - the mask-bleed bug class found in
// gameFlappyBirdo.c/gameParachute.c does not apply here.
//
// ---- The blocking gb.titleScreen(logo) -> BLK_STATE_TITLE ----
// Converted to an explicit state (BLK_STATE_TITLE), dismissed by a
// genuine fresh `gbPressed(BTN_A)`, matching gamePong.c's own worked
// "blocking loop -> explicit resumable state" pattern. Unlike
// gameSnakeAbc.c's own GameMenu(), there is no way back to the title
// screen once dismissed (upstream itself never re-shows it - Button C
// mid-play opens the level-select menu instead, not the title screen),
// so this state is only ever entered once, at boot.
//
// Upstream's own re-entrant `game_menu`/`game_over` flags (checked at the
// top of loop(), each dispatching to GameMenu()/GameOver()) became
// BLK_STATE_MENU/BLK_STATE_GAMEOVER, and its own lazy
// "`if (!initialize) InitGame();`" re-init check (run unconditionally at
// the very top of every loop() iteration) was collapsed into direct calls
// to `blkBeginPlay()` at each of its own 3 real trigger points (title
// dismiss, a confirmed level-menu change, and a Game Over restart) -
// matching gamePong.c's/gameSnakeAbc.c's own identical "lazy re-init flag
// -> direct call-site reset" treatment. This changes nothing observable:
// upstream's own `initialize = false;` always leaves that same tick's
// remaining code doing nothing else useful anyway (game_menu/game_over is
// also cleared in the same breath), so the very next tick's dispatch
// already lands on the freshly-reset state either way.
//
// Upstream's own third level-menu option (Button C = `gb.changeGame()`, a
// real-hardware "switch cartridge on the SD card" OS feature) has no
// equivalent in this single-cartridge menu model and was dropped outright
// - Button C does nothing while the level menu is open here, matching
// gameSnakeAbc.c's/gameConduit.c's own identical precedent for the same
// real call. The level menu's own two `gb.display.fillTriangle(...)`
// up/down arrow graphics are drawn with the real `gbFillTriangle()` shim
// primitive (a direct port of real `Display::fillTriangle()`'s own
// scanline algorithm), at their real upstream coordinates
// (`gbFillTriangle(30,10,25,15,35,15)`/`gbFillTriangle(30,28,25,23,35,23)`
// in `blkUpdateMenu()`). Its own
// title line ("-CHOOSE GAME LEVEL-", plain ASCII) ported unchanged, but
// its own accept/cancel hints (`"\x15:accept \x16:cancel"` in the level
// menu, `"\x16:accept"` on the Game Over screen - non-printable custom
// Gamebuino icon glyphs, not supported by this shim's font) were replaced
// with "A=YES B=NO" and "PRESS B" respectively, matching gameSnakeAbc.c's
// own identical replacement text for the very same two glyphs.
//
// ---- Timing: millis() -> per-tick accumulators ----
// Upstream gates both the gravity/drop step (`game_prevTime`/
// `game_delai`, a per-level ms table) and the line-clear flash animation
// (`game_animation_delai_prevTime`/`game_animation_delai`, a flat 1000ms)
// on real `millis()` reads rather than `gb.update()`'s own frame
// throttle. Ported using a plain per-tick millisecond accumulator
// (`blkDropTimer`/`blkAnimTimer`, both bumped by `BLK_MS_PER_TICK` = 50 -
// this shim's own fixed logic-tick duration at its unchanged 20fps
// default) instead of a genuine `millis()` readout, matching
// gameSnakeAbc.c's own header comment point 7 for the exact same kind of
// conversion (that file's own established, accepted project-wide
// approach for this class of upstream timing code, not a new pattern
// invented here). One real behavioral difference this necessarily
// introduces: real wall-clock time keeps advancing while the level menu
// is open (paused), so on real hardware the drop timer would likely read
// as wildly overdue the instant play resumes; this shim's own
// accumulator instead simply doesn't advance at all while paused (Play()
// itself doesn't run then) - the same already-accepted divergence
// category as gameSnakeAbc.c's own, not a new one. Upstream's own
// `gb.setFrameRate(game_frame_rate)` call in InitGame() is commented out
// in the real source (dead, never executed) anyway, and this shim's own
// unchanged 20fps default already equals the real hardware default (see
// this project's own CLAUDE.md "frame-rate default bug" writeup) - so no
// `gbSetFrameRate()` call was needed here at all.
//
// ---- GetScoreString() -> blkPrintPadded() ----
// Upstream's own zero-padded HUD score/lines counters
// (`GetScoreString()`) built a `sprintf("%03i"/"%05i", ...)` string into
// a fixed-size `char buf[sizeBuffer]`, then split each resulting digit
// character back out by hand into a rebuilt `String` - a very roundabout
// way to print a zero-padded number, with no String/sprintf equivalent
// in this dialect anyway. Replaced with `blkPrintPadded(value, digits)`,
// a plain digit-extraction loop (divide/mod by descending powers of 10)
// that draws the exact same zero-padded text via `gbPrintString()`. One
// deliberate, documented divergence: upstream's own `sizeBuffer`-byte
// buffer (3 bytes for the lines counter, 5 for score) is smaller than
// what `sprintf("%03i"/"%05i", ...)` would actually write once the real
// value exceeds that width (`%03i` is a *minimum* field width, not a
// cap) - i.e. real hardware has a genuine latent stack-buffer-overflow
// bug the moment lines-cleared reaches 1000 or score reaches 100000.
// `blkPrintPadded()` instead just displays the low 3/5 digits once a
// counter grows past its field width - a deliberately more benign
// divergence from a real memory-corruption-class bug (not from any
// bug that's actually visible/load-bearing during ordinary play), chosen
// over reproducing an out-of-bounds write in this shared, linker-less
// single Vircon32 binary (see gameSnakeAbc.c's own header comment for the
// identical "avoid a real OOB write even where upstream's own version was
// harmless in its own isolated memory" reasoning already established in
// this project).
//
// ---- A real latent out-of-bounds READ, fixed defensively ----
// Upstream's own `CheckBlocksRotationCollision(x, y)` checks `x` against
// both bounds (`>= BLOCKS_MAX_X`, `< 0`) but only checks `y`'s LOWER bound
// (`< 0`) - never an upper one - before indexing straight into
// `blocks_activation[y][x]`. A freshly spawned piece can have cells at
// rows 16-19 (see blkNewPlayerBlocks() below - the well itself is only
// 16 rows, 0-15), and rotating while any cell is still up there computes
// a `y` of 17-20, reading straight past the real 16-row array - a genuine
// latent out-of-bounds read on real hardware. `blkCheckBlocksRotationCollision()`
// below adds an explicit `yNewValue >= BLOCKS_MAX_Y` guard (returning
// false - never blocks the rotation) rather than reproducing this
// verbatim, matching gameSnakeAbc.c's own documented precedent of
// defensively avoiding a real OOB access even where upstream's own
// version of it was harmless on real AVR's own isolated per-sketch
// memory - an actual OOB read here could instead read a completely
// unrelated game's own global state in this shared, linker-less single
// binary. "False" is also the semantically correct answer regardless:
// there are never any locked blocks above the real playfield.
//
// ---- Real, preserved upstream quirks (kept deliberately, not "fixed") ----
// - **Rotating never delays gravity.** `MovePlayerBlocks()`'s own local
//   `action` flag (set by a successful left/right move or a successful
//   soft-drop, and used to gate/suppress that tick's gravity check) is
//   NEVER set by a rotation - `if (gb.buttons.pressed(BTN_A)) { RotateBlocks(); }`
//   has no `action = true;` of its own, unlike the three movement
//   branches above it. So spamming rotation, unlike spamming movement,
//   does nothing at all to hold off the next automatic drop. Preserved
//   exactly (`blkRotateBlocks()` is called with no assignment to `action`
//   either, in `blkMovePlayerBlocks()` below).
// - **T/S/S-mirrored pieces spawn with one cell already inside the well.**
//   Every other piece's 4 spawn cells all start at row >= `BLOCKS_MAX_Y`
//   (16, i.e. fully above/outside the visible 16-row well) - but
//   `NewPlayerBlocks()`'s own real T/S/S-mirrored cases each place their
//   4th cell at row `BLOCKS_MAX_Y - 1` (15), already inside the visible
//   well one row lower than the other 3 cells, which still start at
//   16+. Since `CheckBlocksCollision()`/`DrawPlayerBlocks()` both only
//   test/draw a cell once its own row is `< BLOCKS_MAX_Y`, this one lone
//   cell is genuinely visible AND collision-tested starting the instant
//   these 3 piece types spawn, one tick before the rest of the piece
//   catches up - confirmed directly by diffing all 7 real spawn cases
//   against each other, not assumed to be a typo, and preserved exactly
//   (`blkNewPlayerBlocks()` below uses the identical row values).
// - **FIXED, NOT PRESERVED: DrawField()'s own field-outline rectangles.**
//   Taken completely literally, real upstream's own negative-height
//   `drawRect(field_x,field_y,field_w,field_h)` call draws nothing at all
//   (a negative `h` makes `Display::drawFastVLine()`'s own loop condition
//   false immediately, and both horizontal edges land outside the visible
//   0-47 row range) - an earlier pass through this file took that at face
//   value and shipped it as a preserved dead call. That conclusion was
//   wrong: a real screenshot bundled in upstream's own repo
//   (`pictures/BlocksBuino.png`) and a live user report against a real
//   emulator both clearly show a full-height double-line border down EACH
//   SIDE of the field only, with no horizontal cap at top or bottom.
//   `blkDrawField()` now draws the 4 real vertical edges directly (not via
//   `gbDrawRect()`, which would also draw the 2 horizontal edges), reading
//   `field_y`/`field_h` as "anchored at the bottom edge, height extending
//   upward" (the real top of each vertical line is `field_y+field_h`, the
//   real length is `-field_h`) - a normalization real
//   `Display::drawFastVLine()` itself never performs, but the only
//   geometry that reproduces the real, observed border - while the 2
//   horizontal edges stay at their original, always-off-screen position
//   (matching the real "no horizontal cap" look). See `blkDrawField()`'s
//   own inline comment for the full reasoning, including a first attempt
//   that went through `gbDrawRect()` and drew an unwanted extra top line,
//   caught and corrected via direct live user report.
// - **A single-line clear always scores flat 1 point regardless of
//   level, but 2+ simultaneous lines scale with level.**
//   `UpdateGameScore()`'s own `(player_nb_lines_completions > 1) ? (...*
//   (game_level+1)) : 1` - preserved exactly in `blkUpdateGameScore()`
//   below (as an if/else, no ternary operator in this dialect).
// - **`game_force_level`** is a compile-time-`const short` debug toggle
//   hardcoded to 0 - its own `if (game_force_level > 0)` branch is
//   therefore genuine, provably dead code in the shipped game. Kept as a
//   real `#define BLK_FORCE_LEVEL 0` + its own always-false `if` for 1:1
//   structural fidelity rather than silently dropped, since it costs
//   nothing to keep.
// - **`ShowDebug()`** is defined upstream but never actually called from
//   anywhere in the real source (confirmed by reading all 4 real .ino
//   files in full) - dropped entirely, matching gameSnakeAbc.c's own
//   precedent for dropping confirmed-dead upstream code.
//
// ---- Sound ----
// `PlaySoundFx(fxno, channel)` upstream builds a small "FX Synth" preset
// via several `gb.sound.command(...)` calls (waveform/volume-slide/
// pitch-slide, credited upstream to yodasvideoarcade.com - the exact same
// 8-column table shape already seen in gameSnakeAbc.c's/
// gameAsterocks.c's own `soundfx[][]` tables) before a final
// `gb.sound.playNote(pitch, duration, channel)` - this shim only exposes
// a flat `gbPlayNote(pitch, duration)` (real `Sound::command()`/pattern
// effects are an already-documented, deliberate scope limit - see
// gamebuinoShim.h's own header comment). Ported as `blkPlaySoundFx()`
// using only column 1 (pitch) and column 7 (duration) of the verbatim-
// copied `blkSoundFx[4][8]` table, exactly like every other port of this
// same "FX Synth" table shape in this project.
//
// ---- gbFillTriangle() ----
// Used for two small decorative up/down arrows in the level-select menu
// (see `blkUpdateMenu()`).

enum BlkBlockType
{
    BLK_LINE      = 1,
    BLK_CUBE      = 2,
    BLK_T         = 3,
    BLK_L         = 4,
    BLK_L_REVERT  = 5,
    BLK_S         = 6,
    BLK_S_REVERT  = 7
};

enum BlkState
{
    BLK_STATE_TITLE    = 0,
    BLK_STATE_PLAY     = 1,
    BLK_STATE_MENU     = 2,
    BLK_STATE_GAMEOVER = 3
};

enum BlkSoundFx
{
    BLK_FX_LINE_COMPLETED = 0,
    BLK_FX_ROTATE         = 1,
    BLK_FX_GAME_OVER      = 2,
    BLK_FX_PIECE_DROP     = 3
};

#define BLOCKS_MAX_Y 16
#define BLOCKS_MAX_X 10

#define BLK_BLOCK_DRAW_W 3
#define BLK_BLOCK_DRAW_H 3

// Verbatim upstream field geometry (field_x/_y/_w/_h) - see blkDrawField()'s
// own comment for why field_y/field_h need normalizing before drawing.
#define BLK_FIELD_X 26
#define BLK_FIELD_Y 49  // LCDHEIGHT + 1 (LCDHEIGHT is always 48 on this platform)
#define BLK_FIELD_W 32
#define BLK_FIELD_H -49 // -(LCDHEIGHT) - 1

#define BLK_GAME_LEVEL_MAX 9
#define BLK_FORCE_LEVEL 0 // always-false debug toggle - see header comment

#define BLK_ANIM_DEFAULT_COUNTER 60
#define BLK_ANIM_DELAY_MS 1000
#define BLK_MS_PER_TICK 50 // this shim's own fixed 20fps logic-tick duration

int blkState;

// Verbatim copy of upstream's own game_levels[] ms-per-drop table.
int blkGameLevels[9] = { 800, 750, 700, 650, 600, 500, 400, 300, 200 };

bool blkPlayerNewBlocks;
int blkPlayerNbLinesCompletions;
int blkLinesCompletions[16]; // 0/1 flags - see header comment on why int, not bool

int blkPlayerBlocksType;        // the NEXT queued piece type
int blkPlayerBlocksCurrentType; // the currently falling piece type
int blkPlayerBlocksRotation;    // 1..4

int blkPlayerBlocks1[2]; // rotation pivot: [0]=row, [1]=col
int blkPlayerBlocks2[2];
int blkPlayerBlocks3[2];
int blkPlayerBlocks4[2];

int blkBlocksActivation[16][10]; // 0/1 flags - see header comment on why int, not bool

int blkGameLevel;     // 1..9, persists across the whole session (like upstream)
int blkGameMenuLevel; // temp value while adjusting in the level menu
int blkGameScore;
int blkGameLines;
int blkGameDelay;  // current ms-per-drop interval
int blkDropTimer;  // per-tick accumulator, compared against blkGameDelay
bool blkGameOver;

int blkAnimTimer;    // per-tick accumulator, compared against BLK_ANIM_DELAY_MS
int blkAnimCounter = BLK_ANIM_DEFAULT_COUNTER;
int blkAnimColor;    // 0/1, toggled every other blkDrawAnimationBlocks() call

// The real upstream 64x36 title logo, shown via `gb.titleScreen(logo)` -
// restored via gbDrawBitmap() - see this file's own header comment for
// the B-binary-literal-to-hex conversion and its own spot-check.
int blkTitleBitmap[290] =
{
64,36,0x3e,0x61,0xf7,0xdb,0x7c,0x0,0x0,0x0,0x3f,0x61,0xf7,0xdb,0x7c,0x0,0x0,0x0,0x33,0x61,0xb6,0x1b,0x60,0x0,0x0,0x0,0x3f,0x61,0xb6,0x1c,0x60,0xca,0xab,0x80,0x3e,0x61,0xb6,0x1c,0x7c,0xaa,0x3a,0x80,0x3f,0x61,0xb6,0x1c,0xc,0xca,0xaa,0x80,0x33,0x7d,0xf7,0xdb,0x7c,0xaa,0xaa,0x80,0x3f,0x7d,0xf7,0xdb,0x7c,0xce,0xab,0x80,0x3e,0x7d,0xf7,0xdb,0x7c,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x8,0x0,0x0,0x20,0x0,0x0,0x0,0x0,0x8,0x3,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x2,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x3,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x2,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x3,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x2,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x3,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x2,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x3,0x80,0x20,0x0,0x0,0x0,0x0,0x8,0x0,0x0,0x20,0x0,0x0,0x0,0x0,0xf,0xe0,0x70,0x20,0x0,0x0,0x0,0x0,0xf,0xe0,0x70,0x20,0x0,0x0,0x0,0x0,0xf,0xe0,0x70,0x20,0x0,0x0,0x0,0x0,0xf,0x1c,0x70,0x20,0x0,0x0,0x0,0x0,0xf,0x1c,0x70,0x20,0x0,0x0,0x0,0x0,0xf,0x1c,0x70,0x20,0x0,0x0,0x0,0x0,0xf,0x1c,0x7e,0x20,0x0,0x0,0x0,0x0,0xf,0x1c,0x7e,0x20,0x0,0x0,0x0,0x0,0xf,0x1c,0x7e,0x20,0x0,0x0,0x0,0x0,0xf,0xfc,0x7f,0xe0,0x0,0x0,0x0,0x0,0xf,0xfc,0x7f,0xe0,0x0,0x0,0x0,0x0,0xf,0xfc,0x7f,0xe0,0x0,0x0,0x0,0x0,0x8,0xff,0xff,0xe0,0x0,0x0,0x0,0x0,0x8,0xff,0xff,0xe0,0x0,0x0,0x0,0x0,0x8,0xff,0xff,0xe0,0x0,0x0
};

// Verbatim pitch/duration columns from upstream's own soundfx[][] table
// (column 1 = pitch, column 7 = duration) - see this file's own header
// comment for why the other columns (real waveform/slide setup, no shim
// equivalent) are dropped.
int blkSoundFx[4][8] =
{
    { 0, 34, 75, 1, 0, 1, 7, 11 }, // BLK_FX_LINE_COMPLETED
    { 0, 33, 53, 1, 0, 5, 7, 3  }, // BLK_FX_ROTATE
    { 0, 30, 34, 10, 0, 1, 7, 25 }, // BLK_FX_GAME_OVER
    { 1, 1, 0, 0, 0, 0, 7, 2  }  // BLK_FX_PIECE_DROP
};

void blkPlaySoundFx( int fx )
{
    gbPlayNote( blkSoundFx[ fx ][ 1 ], blkSoundFx[ fx ][ 7 ] );
}

void blkPlaySoundFxGameOver()
{
    blkPlaySoundFx( BLK_FX_GAME_OVER );
}

void blkPlaySoundFxLineCompleted()
{
    blkPlaySoundFx( BLK_FX_LINE_COMPLETED );
}

void blkPlaySoundFxRotation()
{
    blkPlaySoundFx( BLK_FX_ROTATE );
}

void blkPlaySoundFxPieceDrop()
{
    blkPlaySoundFx( BLK_FX_PIECE_DROP );
}

// Replaces upstream's own GetScoreString() (sprintf + manual digit split
// into a rebuilt String) - see this file's own header comment for the
// digit-extraction approach and the deliberate divergence once `value`
// exceeds `digits` worth of digits.
void blkPrintPadded( int value, int digits )
{
    char buf[6]; // up to 5 digits + terminator
    int i;
    int divisor = 1;

    for( i = 1; i < digits; i++ )
      divisor = divisor * 10;

    for( i = 0; i < digits; i++ )
    {
        buf[ i ] = 48 + ( ( value / divisor ) % 10 );
        divisor = divisor / 10;
    }
    buf[ digits ] = 0;

    gbPrintString( buf );
}

void blkMoveYBlocks( int value )
{
    blkPlayerBlocks1[ 0 ] = blkPlayerBlocks1[ 0 ] + value;
    blkPlayerBlocks2[ 0 ] = blkPlayerBlocks2[ 0 ] + value;
    blkPlayerBlocks3[ 0 ] = blkPlayerBlocks3[ 0 ] + value;
    blkPlayerBlocks4[ 0 ] = blkPlayerBlocks4[ 0 ] + value;
}

void blkMoveXBlocks( int value )
{
    blkPlayerBlocks1[ 1 ] = blkPlayerBlocks1[ 1 ] + value;
    blkPlayerBlocks2[ 1 ] = blkPlayerBlocks2[ 1 ] + value;
    blkPlayerBlocks3[ 1 ] = blkPlayerBlocks3[ 1 ] + value;
    blkPlayerBlocks4[ 1 ] = blkPlayerBlocks4[ 1 ] + value;
}

// Direct port of upstream's own CheckBlocksCollision() - each of the 4
// cells is only tested against the activation grid once its own row is
// inside the real 16-row well (`< BLOCKS_MAX_Y`), exactly like upstream.
bool blkCheckBlocksCollision( int xValue, int yValue )
{
    if( xValue > 0 && ( ( blkPlayerBlocks1[ 1 ] + xValue ) >= BLOCKS_MAX_X || ( blkPlayerBlocks2[ 1 ] + xValue ) >= BLOCKS_MAX_X
        || ( blkPlayerBlocks3[ 1 ] + xValue ) >= BLOCKS_MAX_X || ( blkPlayerBlocks4[ 1 ] + xValue ) >= BLOCKS_MAX_X ) )
      return true;

    if( xValue < 0 && ( ( blkPlayerBlocks1[ 1 ] + xValue ) < 0 || ( blkPlayerBlocks2[ 1 ] + xValue ) < 0
        || ( blkPlayerBlocks3[ 1 ] + xValue ) < 0 || ( blkPlayerBlocks4[ 1 ] + xValue ) < 0 ) )
      return true;

    if( yValue < 0 && ( ( blkPlayerBlocks1[ 0 ] + yValue ) < 0 || ( blkPlayerBlocks2[ 0 ] + yValue ) < 0
        || ( blkPlayerBlocks3[ 0 ] + yValue ) < 0 || ( blkPlayerBlocks4[ 0 ] + yValue ) < 0 ) )
      return true;

    return ( blkPlayerBlocks1[ 0 ] < BLOCKS_MAX_Y && blkBlocksActivation[ blkPlayerBlocks1[ 0 ] + yValue ][ blkPlayerBlocks1[ 1 ] + xValue ] )
        || ( blkPlayerBlocks2[ 0 ] < BLOCKS_MAX_Y && blkBlocksActivation[ blkPlayerBlocks2[ 0 ] + yValue ][ blkPlayerBlocks2[ 1 ] + xValue ] )
        || ( blkPlayerBlocks3[ 0 ] < BLOCKS_MAX_Y && blkBlocksActivation[ blkPlayerBlocks3[ 0 ] + yValue ][ blkPlayerBlocks3[ 1 ] + xValue ] )
        || ( blkPlayerBlocks4[ 0 ] < BLOCKS_MAX_Y && blkBlocksActivation[ blkPlayerBlocks4[ 0 ] + yValue ][ blkPlayerBlocks4[ 1 ] + xValue ] );
}

// Direct port of upstream's own CheckBlocksRotationCollision(), PLUS a
// defensive upper-bound check on yNewValue - see this file's own header
// comment ("A real latent out-of-bounds READ, fixed defensively").
bool blkCheckBlocksRotationCollision( int xNewValue, int yNewValue )
{
    if( xNewValue >= BLOCKS_MAX_X )
      return true;

    if( xNewValue < 0 || yNewValue < 0 )
      return true;

    if( yNewValue >= BLOCKS_MAX_Y )
      return false; // defensive addition, not in upstream - see header comment

    return blkBlocksActivation[ yNewValue ][ xNewValue ] != 0;
}

void blkSetRotation( int currentRotation )
{
    blkPlaySoundFxRotation();
    blkPlayerBlocksRotation = currentRotation;
}

// line
void blkRotationType1( int currentRotation )
{
    if( currentRotation == 2 || currentRotation == 4 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 2, blkPlayerBlocks1[ 0 ] ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 2;

            blkSetRotation( currentRotation );
        }
    }
    else
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 2 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] + 2;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkSetRotation( currentRotation );
        }
    }
}

// T
void blkRotationType3( int currentRotation )
{
    if( currentRotation == 1 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 2 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 3 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 4 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkSetRotation( currentRotation );
        }
    }
}

// L
void blkRotationType4( int currentRotation )
{
    if( currentRotation == 1 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] + 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 2 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] + 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 3 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 4 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkSetRotation( currentRotation );
        }
    }
}

// L(revert)
void blkRotationType5( int currentRotation )
{
    if( currentRotation == 1 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] + 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 2 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 3 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 4 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkSetRotation( currentRotation );
        }
    }
}

// S
void blkRotationType6( int currentRotation )
{
    if( currentRotation == 1 || currentRotation == 3 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 2 || currentRotation == 4 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] - 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkSetRotation( currentRotation );
        }
    }
}

// S(revert)
void blkRotationType7( int currentRotation )
{
    if( currentRotation == 1 || currentRotation == 3 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] - 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] - 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkSetRotation( currentRotation );
        }
    }
    else if( currentRotation == 2 || currentRotation == 4 )
    {
        if( !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] + 1, blkPlayerBlocks1[ 0 ] ) && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ], blkPlayerBlocks1[ 0 ] + 1 )
            && !blkCheckBlocksRotationCollision( blkPlayerBlocks1[ 1 ] - 1, blkPlayerBlocks1[ 0 ] + 1 ) )
        {
            blkPlayerBlocks2[ 0 ] = blkPlayerBlocks1[ 0 ];
            blkPlayerBlocks2[ 1 ] = blkPlayerBlocks1[ 1 ] + 1;

            blkPlayerBlocks3[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks3[ 1 ] = blkPlayerBlocks1[ 1 ];

            blkPlayerBlocks4[ 0 ] = blkPlayerBlocks1[ 0 ] + 1;
            blkPlayerBlocks4[ 1 ] = blkPlayerBlocks1[ 1 ] - 1;

            blkSetRotation( currentRotation );
        }
    }
}

// Direct port of upstream's own RotateBlocks() - no ternary operator in
// this dialect, so the rotation-increment is an if/else. No branch exists
// for BLK_CUBE (cubes never rotate) - see header comment.
void blkRotateBlocks()
{
    int currentRotation;
    if( ( blkPlayerBlocksRotation + 1 ) > 4 )
      currentRotation = 1;
    else
      currentRotation = blkPlayerBlocksRotation + 1;

    if( blkPlayerBlocksCurrentType == BLK_LINE )
      blkRotationType1( currentRotation );
    else if( blkPlayerBlocksCurrentType == BLK_T )
      blkRotationType3( currentRotation );
    else if( blkPlayerBlocksCurrentType == BLK_L )
      blkRotationType4( currentRotation );
    else if( blkPlayerBlocksCurrentType == BLK_L_REVERT )
      blkRotationType5( currentRotation );
    else if( blkPlayerBlocksCurrentType == BLK_S )
      blkRotationType6( currentRotation );
    else if( blkPlayerBlocksCurrentType == BLK_S_REVERT )
      blkRotationType7( currentRotation );
}

// Direct port of upstream's own NewPlayerBlocks() - see header comment
// for the real T/S/S-mirrored spawn quirk (one cell starting at row 15,
// already inside the well) preserved exactly below.
void blkNewPlayerBlocks()
{
    blkPlayerBlocksRotation = 1;

    if( blkPlayerBlocksType == BLK_LINE )
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y + 2;
        blkPlayerBlocks1[ 1 ] = BLOCKS_MAX_X / 2;

        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y + 3;
        blkPlayerBlocks2[ 1 ] = BLOCKS_MAX_X / 2;

        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks3[ 1 ] = BLOCKS_MAX_X / 2;

        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks4[ 1 ] = BLOCKS_MAX_X / 2;
    }
    else if( blkPlayerBlocksType == BLK_CUBE )
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks1[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks2[ 1 ] = ( BLOCKS_MAX_X / 2 );
        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks3[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks4[ 1 ] = ( BLOCKS_MAX_X / 2 );
    }
    else if( blkPlayerBlocksType == BLK_T )
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks1[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks2[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks3[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y - 1; // see header comment: already inside the well
        blkPlayerBlocks4[ 1 ] = BLOCKS_MAX_X / 2;
    }
    else if( blkPlayerBlocksType == BLK_L )
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks1[ 1 ] = ( BLOCKS_MAX_X / 2 );
        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks2[ 1 ] = ( BLOCKS_MAX_X / 2 );
        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y + 2;
        blkPlayerBlocks3[ 1 ] = ( BLOCKS_MAX_X / 2 );
        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y + 2;
        blkPlayerBlocks4[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
    }
    else if( blkPlayerBlocksType == BLK_L_REVERT )
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks1[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks2[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y + 2;
        blkPlayerBlocks3[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y + 2;
        blkPlayerBlocks4[ 1 ] = ( BLOCKS_MAX_X / 2 ) + 1;
    }
    else if( blkPlayerBlocksType == BLK_S )
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks1[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks2[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks3[ 1 ] = ( BLOCKS_MAX_X / 2 );
        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y - 1; // see header comment: already inside the well
        blkPlayerBlocks4[ 1 ] = ( BLOCKS_MAX_X / 2 );
    }
    else if( blkPlayerBlocksType == BLK_S_REVERT )
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks1[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks2[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks3[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y - 1; // see header comment: already inside the well
        blkPlayerBlocks4[ 1 ] = ( BLOCKS_MAX_X / 2 ) - 1;
    }
    else // unreachable - matches upstream's own literal (and genuinely different-from-BLK_CUBE) default fallback, see header comment
    {
        blkPlayerBlocks1[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks1[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks2[ 0 ] = BLOCKS_MAX_Y;
        blkPlayerBlocks2[ 1 ] = ( BLOCKS_MAX_X / 2 ) + 1;
        blkPlayerBlocks3[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks3[ 1 ] = BLOCKS_MAX_X / 2;
        blkPlayerBlocks4[ 0 ] = BLOCKS_MAX_Y + 1;
        blkPlayerBlocks4[ 1 ] = ( BLOCKS_MAX_X / 2 ) + 1;
    }

    blkPlayerBlocksCurrentType = blkPlayerBlocksType;
    blkPlayerBlocksType = arand( 7 ) + 1; // random(1,8)
}

// Direct port of upstream's own InitGame().
void blkResetGame()
{
    int i, j;

    blkPlayerNewBlocks = true;

    for( i = 0; i < BLOCKS_MAX_Y; i++ )
      for( j = 0; j < BLOCKS_MAX_X; j++ )
        blkBlocksActivation[ i ][ j ] = 0;

    for( i = 0; i < BLOCKS_MAX_Y; i++ )
      blkLinesCompletions[ i ] = 0;

    blkPlayerNbLinesCompletions = 0;

    blkPlayerBlocksType = arand( 7 ) + 1; // random(1,8)
    blkPlayerBlocksRotation = 1;

    blkGameLines = 0;
    blkGameScore = 0;

    if( BLK_FORCE_LEVEL > 0 ) // always false - see header comment
      blkGameLevel = BLK_FORCE_LEVEL;
    else if( blkGameMenuLevel != 1 )
      blkGameLevel = blkGameMenuLevel;
    else
      blkGameLevel = 1;

    blkGameMenuLevel = blkGameLevel;
    blkGameDelay = blkGameLevels[ blkGameLevel - 1 ];

    blkGameOver = false;

    // Shim-specific: no wall clock to carry a stale timestamp across a
    // reset the way upstream's own untouched millis()-based globals would
    // - freshly zeroed instead, matching gameSnakeAbc.c's own identical
    // precedent (see header comment).
    blkDropTimer = 0;
    blkAnimTimer = 0;
    blkAnimCounter = BLK_ANIM_DEFAULT_COUNTER;
}

void blkUpdateGameScore()
{
    blkGameLines = blkGameLines + blkPlayerNbLinesCompletions;

    if( blkPlayerNbLinesCompletions > 1 )
      blkGameScore = blkGameScore + ( blkPlayerNbLinesCompletions * ( blkGameLevel + 1 ) );
    else
      blkGameScore = blkGameScore + 1;

    if( blkGameLines >= ( blkGameLevel * 10 ) )
    {
        if( ( blkGameLevel + 1 ) <= BLK_GAME_LEVEL_MAX )
          blkGameLevel = blkGameLevel + 1;

        blkGameDelay = blkGameLevels[ blkGameLevel - 1 ];
    }
}

void blkCheckLinesCompletion()
{
    int i, j;
    bool completion;

    for( i = 0; i < BLOCKS_MAX_Y; i++ )
    {
        completion = true;

        for( j = 0; j < BLOCKS_MAX_X; j++ )
        {
            if( !blkBlocksActivation[ i ][ j ] )
            {
                completion = false;
                break;
            }
        }

        if( completion )
          blkPlayerNbLinesCompletions = blkPlayerNbLinesCompletions + 1;

        if( completion )
          blkLinesCompletions[ i ] = 1;
        else
          blkLinesCompletions[ i ] = 0;
    }

    if( blkPlayerNbLinesCompletions > 0 )
      blkPlaySoundFxLineCompleted();
}

void blkUpdateBlocks()
{
    int i, j;
    int newIndex = 0;
    bool increment;

    i = 0;
    while( i < BLOCKS_MAX_Y )
    {
        increment = false;

        j = 0;
        while( j < BLOCKS_MAX_X )
        {
            if( !blkLinesCompletions[ i ] )
            {
                blkBlocksActivation[ newIndex ][ j ] = blkBlocksActivation[ i ][ j ];
                increment = true;
            }
            j = j + 1;
        }

        if( increment )
          newIndex = newIndex + 1;

        i = i + 1;
    }

    i = newIndex;
    while( i < BLOCKS_MAX_Y )
    {
        j = 0;
        while( j < BLOCKS_MAX_X )
        {
            blkBlocksActivation[ i ][ j ] = 0;
            j = j + 1;
        }
        i = i + 1;
    }

    for( i = 0; i < BLOCKS_MAX_Y; i++ )
      blkLinesCompletions[ i ] = 0;

    blkUpdateGameScore();

    blkPlayerNbLinesCompletions = 0;
}

int blkGetXCoord( int x )
{
    return BLK_FIELD_X + ( BLK_BLOCK_DRAW_W * x ) + 1;
}

int blkGetYCoord( int y )
{
    return ( BLK_FIELD_Y - 1 ) - ( BLK_BLOCK_DRAW_H * ( y + 1 ) );
}

void blkDrawField()
{
    // Real upstream calls gb.display.drawRect(field_x, field_y, field_w,
    // field_h) with field_y=49 (one row below the real 48px-tall screen)
    // and a NEGATIVE field_h=-49 - taken completely literally against the
    // real Display::drawRect()/drawFastVLine()/drawFastHLine() (which
    // never normalize a negative height), this draws nothing at all: the
    // vertical edges' own `for(i=0;i<h;i++)` loop is false immediately for
    // a negative h, and both horizontal edges land at y=49/y=-1, off the
    // visible 0..47 range. An earlier pass through this file took that at
    // face value and left the two calls as a preserved no-op - but a real
    // screenshot bundled in upstream's own repo (pictures/BlocksBuino.png)
    // and the user's own tested real emulator both clearly show a
    // full-height double-line border down EACH SIDE of the field only - no
    // horizontal cap at the top or bottom - so that conclusion was wrong.
    // Reproduced by drawing just the 4 real vertical edges directly (not
    // via gbDrawRect(), which would also draw the 2 horizontal edges): the
    // real y=49/y=-1 horizontal edges stay off-screen exactly as upstream's
    // own literal math already has them (matching the real "no horizontal
    // cap" look), while the 4 vertical edges are read as "anchored at the
    // bottom edge, height extending upward" - i.e. the actual top of each
    // vertical line is field_y+field_h (49-49=0) and the actual length is
    // -field_h (49) - a normalization real Display::drawFastVLine() itself
    // never performs, but clearly the intended shape (GetYcoordonnee()'s
    // own real formula already anchors block positions off field_y-1=48 as
    // the field's real bottom edge the same way). A first attempt at this
    // fix went through gbDrawRect() for both rects, which also drew an
    // unwanted horizontal line across the top (y=0) that neither the real
    // screenshot nor the user's own tested emulator actually show - caught
    // via direct live user report and corrected to this 4-VLine-only form.
    gbSetColor( 1 );
    gbDrawFastVLine( BLK_FIELD_X, BLK_FIELD_Y + BLK_FIELD_H, -BLK_FIELD_H );
    gbDrawFastVLine( BLK_FIELD_X + BLK_FIELD_W - 1, BLK_FIELD_Y + BLK_FIELD_H, -BLK_FIELD_H );
    gbDrawFastVLine( BLK_FIELD_X - 2, BLK_FIELD_Y + BLK_FIELD_H, -BLK_FIELD_H );
    gbDrawFastVLine( BLK_FIELD_X - 2 + BLK_FIELD_W + 4 - 1, BLK_FIELD_Y + BLK_FIELD_H, -BLK_FIELD_H );
}

void blkDrawPlayerBlocks()
{
    gbSetColor( 1 );

    if( blkPlayerBlocks1[ 0 ] < BLOCKS_MAX_Y )
      gbDrawRect( blkGetXCoord( blkPlayerBlocks1[ 1 ] ), blkGetYCoord( blkPlayerBlocks1[ 0 ] ), BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    if( blkPlayerBlocks2[ 0 ] < BLOCKS_MAX_Y )
      gbDrawRect( blkGetXCoord( blkPlayerBlocks2[ 1 ] ), blkGetYCoord( blkPlayerBlocks2[ 0 ] ), BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    if( blkPlayerBlocks3[ 0 ] < BLOCKS_MAX_Y )
      gbDrawRect( blkGetXCoord( blkPlayerBlocks3[ 1 ] ), blkGetYCoord( blkPlayerBlocks3[ 0 ] ), BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    if( blkPlayerBlocks4[ 0 ] < BLOCKS_MAX_Y )
      gbDrawRect( blkGetXCoord( blkPlayerBlocks4[ 1 ] ), blkGetYCoord( blkPlayerBlocks4[ 0 ] ), BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
}

// Direct port of upstream's own DrawNextBlocks() - draws the SCORE
// counter and the upcoming-piece preview shape (using blkPlayerBlocksType,
// which by this point already holds the NEXT queued type - see
// blkNewPlayerBlocks()). The switch->if/else-if conversion (including its
// own literal, genuinely-different-from-BLK_CUBE default) is explained in
// this file's own header comment.
void blkDrawNextBlocks()
{
    int x = -2;
    int y = 10;
    int bx = ( BLK_FIELD_X / 2 ) + x;
    int by = ( LCDHEIGHT / 2 ) + y;

    gbSetColor( 1 );

    gbCursorX = 1;
    gbCursorY = 5;
    gbPrintString( "SCORE" );
    gbCursorX = 1;
    gbCursorY = 13;
    blkPrintPadded( blkGameScore, 5 );

    gbCursorX = 1;
    gbCursorY = ( LCDHEIGHT / 2 ) + y - 13;
    gbPrintString( "NEXT" );

    if( blkPlayerBlocksType == BLK_LINE )
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + ( BLK_BLOCK_DRAW_H * 2 ), BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
    else if( blkPlayerBlocksType == BLK_CUBE )
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
    else if( blkPlayerBlocksType == BLK_T )
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx - BLK_BLOCK_DRAW_W, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
    else if( blkPlayerBlocksType == BLK_L )
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx - BLK_BLOCK_DRAW_W, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
    else if( blkPlayerBlocksType == BLK_L_REVERT )
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
    else if( blkPlayerBlocksType == BLK_S )
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
    else if( blkPlayerBlocksType == BLK_S_REVERT )
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by - BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
    else // unreachable - matches upstream's own literal default fallback (identical body to BLK_CUBE here), see header comment
    {
        gbDrawRect( bx, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        gbDrawRect( bx + BLK_BLOCK_DRAW_W, by + BLK_BLOCK_DRAW_H, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
    }
}

void blkDrawScore()
{
    blkDrawNextBlocks();

    gbSetColor( 1 );

    gbCursorX = 62;
    gbCursorY = 5;
    gbPrintString( "Lvl." );
    gbPrintNumber( blkGameLevel );

    gbCursorX = 62;
    gbCursorY = 15;
    gbPrintString( "LINES" );
    gbCursorX = 62;
    gbCursorY = 22;
    blkPrintPadded( blkGameLines, 3 );
}

void blkDrawBlocks()
{
    int i, j, x, y;

    gbSetColor( 1 );

    for( i = 0; i < BLOCKS_MAX_Y; i++ )
    {
        y = blkGetYCoord( i );
        for( j = 0; j < BLOCKS_MAX_X; j++ )
        {
            x = blkGetXCoord( j );
            if( blkBlocksActivation[ i ][ j ] )
              gbFillRect( x, y, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
        }
    }
}

void blkDrawAnimationBlocks()
{
    int i, j, x, y;

    gbSetColor( 1 );

    if( ( blkAnimCounter % 2 ) == 0 )
    {
        if( blkAnimColor == 0 )
          blkAnimColor = 1;
        else
          blkAnimColor = 0;
    }

    for( i = 0; i < BLOCKS_MAX_Y; i++ )
    {
        y = blkGetYCoord( i );

        if( !blkLinesCompletions[ i ] )
        {
            for( j = 0; j < BLOCKS_MAX_X; j++ )
            {
                x = blkGetXCoord( j );
                if( blkBlocksActivation[ i ][ j ] )
                  gbFillRect( x, y, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
            }
        }
        else
        {
            for( j = 0; j < BLOCKS_MAX_X; j++ )
            {
                x = blkGetXCoord( j );

                if( blkAnimColor == 0 )
                  gbDrawRect( x, y, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
                else
                  gbFillRect( x, y, BLK_BLOCK_DRAW_W, BLK_BLOCK_DRAW_H );
            }
        }
    }

    blkAnimCounter = blkAnimCounter - 1;
}

// Direct port of upstream's own MovePlayerBlocks() - see header comment
// for the preserved "rotation never delays gravity" quirk and the
// millis()->per-tick-accumulator conversion.
void blkMovePlayerBlocks()
{
    bool action = false;

    if( gbRepeat( BTN_RIGHT, 3 ) )
    {
        if( !blkCheckBlocksCollision( 1, 0 ) )
        {
            blkMoveXBlocks( 1 );
            action = true;
        }
    }
    if( gbRepeat( BTN_LEFT, 3 ) )
    {
        if( !blkCheckBlocksCollision( -1, 0 ) )
        {
            blkMoveXBlocks( -1 );
            action = true;
        }
    }
    if( gbRepeat( BTN_DOWN, 1 ) )
    {
        if( blkPlayerBlocks1[ 0 ] > 0 && !blkCheckBlocksCollision( 0, -1 ) )
        {
            blkMoveYBlocks( -1 );
            action = true;
        }
    }
    if( gbPressed( BTN_A ) )
      blkRotateBlocks(); // does NOT set action = true - see header comment

    blkDropTimer = blkDropTimer + BLK_MS_PER_TICK;
    if( !action && blkDropTimer >= blkGameDelay )
    {
        if( !blkPlayerNewBlocks && blkCheckBlocksCollision( 0, -1 ) )
        {
            if( blkPlayerBlocks1[ 0 ] >= ( BLOCKS_MAX_Y - 1 ) )
            {
                blkGameOver = true;
            }
            else
            {
                blkPlaySoundFxPieceDrop();

                blkBlocksActivation[ blkPlayerBlocks1[ 0 ] ][ blkPlayerBlocks1[ 1 ] ] = 1;
                blkBlocksActivation[ blkPlayerBlocks2[ 0 ] ][ blkPlayerBlocks2[ 1 ] ] = 1;
                blkBlocksActivation[ blkPlayerBlocks3[ 0 ] ][ blkPlayerBlocks3[ 1 ] ] = 1;
                blkBlocksActivation[ blkPlayerBlocks4[ 0 ] ][ blkPlayerBlocks4[ 1 ] ] = 1;
                blkPlayerNewBlocks = true;

                blkCheckLinesCompletion();
            }
        }
        else
        {
            blkMoveYBlocks( -1 );
        }

        blkDropTimer = 0;
    }

    if( blkPlayerNbLinesCompletions == 0 && blkPlayerNewBlocks )
    {
        blkNewPlayerBlocks();
        blkPlayerNewBlocks = false;
    }
}

void blkBeginTitle()
{
    blkState = BLK_STATE_TITLE;
}

void blkBeginMenu()
{
    blkState = BLK_STATE_MENU;
}

// Used by the title dismiss, a confirmed level-menu change, and a Game
// Over restart - see header comment for why this collapses upstream's
// own lazy `initialize` re-init flag with no behavior change.
void blkBeginPlay()
{
    blkResetGame();
    blkState = BLK_STATE_PLAY;
}

void blkUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 4, blkTitleBitmap );

    gbCursorX = 28;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      blkBeginPlay();
}

// Direct port of upstream's own GameMenu() (minus the dropped
// gb.changeGame() branch - see header comment). The up/down arrow
// triangles are drawn via the real gbFillTriangle() shim primitive.
void blkUpdateMenu()
{
    gbSetColor( 1 );

    gbCursorX = 5;
    gbCursorY = 1;
    gbPrintString( "-CHOOSE GAME LEVEL-" );

    gbFillTriangle( 30, 10, 25, 15, 35, 15 );
    gbFillTriangle( 30, 28, 25, 23, 35, 23 );

    gbCursorX = 0;
    gbCursorY = 17;
    gbPrintString( "LEVEL: " );
    gbPrintNumber( blkGameMenuLevel );

    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( "A=YES B=NO" );

    if( gbPressed( BTN_UP ) )
    {
        if( ( blkGameMenuLevel + 1 ) <= BLK_GAME_LEVEL_MAX )
          blkGameMenuLevel = blkGameMenuLevel + 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        if( ( blkGameMenuLevel - 1 ) >= 1 )
          blkGameMenuLevel = blkGameMenuLevel - 1;
    }
    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();

        blkGameLevel = blkGameMenuLevel;
        blkBeginPlay();
    }
    if( gbPressed( BTN_B ) )
    {
        gbPlayOK();

        blkGameMenuLevel = blkGameLevel; // upstream's own explicit discard-edit-on-cancel
        blkState = BLK_STATE_PLAY;       // resume unchanged, no reset
    }
}

// Direct port of upstream's own Play() (plus the Button C pause handling
// that upstream itself checks right before calling Play() - hoisted in
// here, matching gameSnakeAbc.c's own identical treatment of the same
// kind of hoist).
void blkUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        blkBeginMenu();
        return;
    }

    if( blkPlayerNbLinesCompletions > 0 )
    {
        blkAnimTimer = blkAnimTimer + BLK_MS_PER_TICK;
        if( blkAnimTimer >= BLK_ANIM_DELAY_MS )
        {
            blkUpdateBlocks(); // removes the completed lines

            blkDrawScore();
            blkDrawField();
            blkDrawBlocks();

            blkAnimTimer = 0;
            blkAnimCounter = BLK_ANIM_DEFAULT_COUNTER;
        }
        else
        {
            blkDrawScore();
            blkDrawField();
            blkDrawAnimationBlocks();
        }
    }
    else
    {
        blkMovePlayerBlocks();

        blkDrawScore();
        blkDrawField();
        blkDrawPlayerBlocks();
        blkDrawBlocks();

        blkAnimTimer = 0;
    }

    if( blkGameOver )
    {
        blkPlaySoundFxGameOver();
        blkState = BLK_STATE_GAMEOVER;
    }
}

// Direct port of upstream's own GameOver().
void blkUpdateGameOver()
{
    gbSetColor( 1 );

    gbCursorX = 22;
    gbCursorY = 1;
    gbPrintString( "!GAME OVER!" );

    gbCursorX = 0;
    gbCursorY = 10;
    gbPrintString( "Level: [" );
    gbPrintNumber( blkGameLevel );
    gbPrintString( "]" );

    gbCursorX = 0;
    gbCursorY = 20;
    gbPrintString( "Lines: " );
    gbPrintNumber( blkGameLines );

    gbCursorX = 0;
    gbCursorY = 30;
    gbPrintString( "Score: " );
    gbPrintNumber( blkGameScore );

    gbCursorX = 0;
    gbCursorY = 40;
    gbPrintString( "PRESS B" );

    if( gbPressed( BTN_B ) )
    {
        gbPlayOK();
        blkBeginPlay();
    }
}

void gameBlocksBuino_init()
{
    gbBegin();

    blkGameLevel = 1;
    blkGameMenuLevel = 1;

    blkBeginTitle();
}

void gameBlocksBuino_update()
{
    if( !gbUpdate() ) return;

    if( blkState == BLK_STATE_TITLE ) blkUpdateTitle();
    else if( blkState == BLK_STATE_MENU ) blkUpdateMenu();
    else if( blkState == BLK_STATE_GAMEOVER ) blkUpdateGameOver();
    else blkUpdatePlay();

    gbRenderFrame();
}
