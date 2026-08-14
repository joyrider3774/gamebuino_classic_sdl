#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// SnakeAbcBuino (frthery, license: None specified -
// github.com/frthery/SnakeAbcBuino). A Snake variant with an educational
// twist: instead of generic food pellets, the snake collects letters of the
// alphabet in strict order (A, B, C, ... Z, then wrapping back to A), each
// pickup growing the snake by one segment and speeding the game up a
// little; 9 selectable starting speed levels via an in-game level-select
// menu (Button C, mid-play); running into a wall or your own body ends the
// round.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every upstream global/function got
// an `sabc`-prefixed name, since Vircon32 has no linker and every game in
// this single compiled cartridge shares one flat global namespace -
// especially important here since a second, separate Snake port
// (snake-gamebuino-classic, prefix `snkc`) already exists in this same
// cartridge (see gameSnakeClassic.c's own header comment, which already
// calls out this exact game by name for that reason). Upstream's own
// `boolean`/`short`/`byte`/`unsigned long` all became plain `int`/`bool`
// (this dialect's own real primitives).
//
// Several real adaptations were needed beyond the usual mechanical rewrite:
//
// 1. **Title screen.** Upstream's own blocking `gb.titleScreen(logo)` was
//    converted into an explicit SABC_STATE_TITLE state, matching gamePong.c's
//    own titleScreen-to-state-machine treatment. The real 64x36 `logo[]`
//    bitmap itself is drawn as sabcTitleBitmap via this project's own
//    gbDrawBitmap() primitive (see that array's own comment for the
//    B-binary-literal conversion this needed).
//
// 2. **The level-select menu's own long/unsupported text.** Upstream's own
//    `GameMenu()` (reached by pressing C mid-play, matching real
//    `Buttons::pressed(BTN_C)`) drew two `gb.display.fillTriangle(...)`
//    up/down arrows - ported directly at their real upstream coordinates
//    via the real `gbFillTriangle()` shim primitive (see
//    `sabcUpdateMenu()`). Its own title line
//    ("-CHOOSE GAME LEVEL-", 20 characters) and its accept/cancel hint
//    (`"\x15:accept \x16:cancel"`, using non-printable custom Gamebuino
//    icon glyphs plus a colon, neither supported by this shim's font - see
//    gamebuinoShim.h's own supported-character list) were both shortened/
//    replaced: "PICK LEVEL" (fits the 84px width at font size 1) and
//    "A=YES B=NO" (spells out the same two real buttons in plain
//    supported characters), matching this project's own established
//    "shorten/adapt unsupported upstream text" precedent (see
//    gameAgaruino.c's/gameTaquin.c's own header comments for other worked
//    examples). Converted into an explicit SABC_STATE_MENU state (upstream
//    itself already modeled this as a plain re-entrant `game_menu` flag
//    checked at the top of loop(), not a blocking call - ported the same
//    way here for consistency with every other state in this file).
//    Upstream's own third menu option (Button C = `gb.changeGame()`, a
//    real-hardware "switch to a different game on the SD card" OS
//    feature) has no equivalent in this single-cartridge menu model and
//    was dropped outright - Button C simply does nothing while this menu
//    is open here.
//
// 3. **The Game Over screen's own letter listing.** Upstream builds and
//    prints a `String` of every letter collected so far, using
//    `gb.display.textWrap = true` to wrap it across as many lines as
//    needed - this shim's `gbPrintString()` has no wrapping at all, and
//    the 48px-tall screen has no room left for a multi-line list anyway
//    once the "GAME OVER" title, score, and level are also shown. Replaced
//    with a plain "LETTERS <count>" line instead - genuinely equivalent
//    information, not a loss: this game always collects letters starting
//    from 'A' in strict fixed order every single playthrough
//    (`sabcLetterIndex`/`sabcLetterMaxIndex` both reset to 0 in
//    `sabcResetGame()`), so a bare count fully determines which letters
//    were shown (always "A" through the Nth letter) without needing to
//    actually list them. Upstream's own secondary derived stat
//    (`score * level`, a display-only bonus multiply) was dropped too, to
//    fit the remaining vertical space - a cosmetic-only trim, since both
//    of its own inputs (score, level) are still shown separately.
//
// 4. **Sound.** Upstream's own `PlaySoundFx()` used `gb.sound.command(...)`
//    to set a per-note waveform/volume-slide/pitch-slide "FX Synth" preset
//    (credited upstream to yodasvideoarcade.com) before calling
//    `gb.sound.playNote(pitch, duration, channel)` - this shim only
//    implements a flat `gbPlayNote(pitch, duration)` (real
//    `Sound::command()`/pattern effects are out of scope for this
//    project's first pass, see gamebuinoShim.h's own header comment).
//    Ported as `gbPlayNote()` alone, using the exact same pitch/duration
//    values upstream's own `soundfx[][]` table already stored (column 1 =
//    pitch, column 7 = duration) - matching gameSimonbuino.c's own
//    identical treatment of the same "FX Synth"-shaped preset table. Both
//    real cues (letter pickup, game over) still play their correct note
//    pitch, just without the slide/envelope flavor. `gb.pickRandomSeed()`
//    became `gbPickRandomSeed()` (a documented no-op), and
//    `gb.battery.show = false;` was dropped outright, matching every
//    other port's own treatment of these two calls.
//
// 5. **The collectible letter itself.** Upstream already draws it as plain
//    text (`gb.display.print(letters[letter_index])`, a single character -
//    no custom bitmap involved at all), so this ports directly as a
//    one-character `gbPrintString()` call (`sabcPrintChar()` below). Since
//    upstream's own `letters[]` table is just 'A'..'Z' in strict
//    consecutive order, this port computes the ASCII code directly as
//    `65 + sabcLetterIndex` instead of keeping a redundant 26-entry lookup
//    table.
//
// 6. **Dead code dropped.** `ShowFrame()`/`ShowDebug()` are defined
//    upstream but never actually called from `setup()`/`loop()` or
//    anywhere else in the real source (confirmed by reading the entire
//    459-line file before dropping them) - left out entirely, matching
//    gameSimonbuino.c's own precedent for dropping confirmed-inert
//    upstream code. Upstream never calls any `EEPROM.*` function anywhere
//    (no high-score persistence exists in the real game), so none was
//    added here either, per this project's own "don't invent gameplay
//    behavior not in the upstream source" rule.
//
// 7. **Timing.** Upstream gates each grid-step move on real wall-clock
//    time (`millis()` compared against a per-level `game_delai`
//    millisecond threshold, from the verbatim-copied `game_levels[]`
//    table) rather than on `gb.update()`'s own frame throttle - this port
//    keeps the exact same millisecond thresholds, but compares them
//    against a simple per-tick accumulator (`sabcMoveTimer`, incremented
//    by 50ms - this shim's own fixed default logic-tick duration at its
//    unchanged 20fps rate, matching upstream's own commented-out
//    `gb.setFrameRate(game_frame_rate)` call, i.e. the two were always
//    going to run at the same real tick rate anyway) instead of a genuine
//    `millis()` readout, since Vircon32 has no wall-clock primitive to
//    read here.
//
// Multiple real upstream quirks found while reading the source closely,
// preserved deliberately rather than "fixed":
// - **Letter pickup is detected one full move-step late.** Upstream's own
//   `CheckLetterCollision()` (called from inside `MoveSnake()`, right
//   after that step's new head position is computed) reads the collision
//   rectangle straight from the global `snake[0][]` - which at that exact
//   point in the code has *not yet* been updated with this step's new head
//   position (the body-shift loop that does that runs afterward). So the
//   check actually compares the position the head is *currently sitting
//   at* (as drawn on the previous move-step) against the letter, not the
//   position it's moving into. Net effect: the snake has to already be
//   sitting visibly on top of the letter for one full move-interval before
//   the pickup actually registers and the letter respawns. Preserved
//   exactly below (`sabcCheckLetterCollision()` is likewise called with
//   `sabcSnakeX[0]`/`sabcSnakeY[0]`, before that tick's body-shift loop
//   runs), since this is a real, visible piece of the original game's own
//   feel, not a porting mistake.
// - **The letter's position (and any in-progress "needs a new spot"
//   search) is never reset across a Game Over -> restart.** Upstream's own
//   `InitGame()` resets the snake's position/size/direction, the score,
//   and the letter *index*, but never touches `letter_x`/`letter_y`/
//   `new_letter` - so a freshly-restarted game keeps showing the letter
//   wherever the previous game happened to leave it, exactly like real
//   hardware. `sabcResetGame()` below matches this precisely (only
//   `sabcLetterX`/`sabcLetterY`/`sabcNewLetter`'s *initial* cold-boot
//   values are set, once, in `gameSnakeAbc_init()`).
// - **A cancelled level-menu edit is sticky.** Pressing B in the level
//   menu discards the edited level *without* resetting the menu's own
//   displayed value back to the active level - it keeps showing whatever
//   was last dialed in until the next real game reset re-syncs it
//   (`sabcResetGame()` always sets `sabcMenuLevel = sabcGameLevel`).
//   Matches upstream's own identical `game_menu_level` behavior exactly.
//
// One small, deliberate divergence (not a preserved quirk): upstream's own
// move-timer (`game_prevTime`, a raw `millis()` snapshot) and
// `snake_has_moved` flag are both, like the letter position above, never
// explicitly reset across a restart - but unlike the letter position, that
// carryover has no clean equivalent for this port's own tick-accumulator
// timer (there's no stale "millis() snapshot" concept to carry over), so
// `sabcMoveTimer`/`sabcSnakeHasMoved` are both reset to a clean state in
// `sabcResetGame()` instead. The practical difference is negligible (at
// most, whether the very first directional input after a restart is
// accepted immediately or one move-interval later).
//
// Upstream's own `snake[100][2]` position array (a 2D array - proven to
// work fine in this dialect, see gameSimonbuino.c's own `simonSoundFx`
// table) was still split into two parallel `int[100]` arrays
// (`sabcSnakeX`/`sabcSnakeY`) below purely as a style choice for
// readability, not a syntax workaround. Upstream's own snake length is
// technically unbounded (it keeps growing forever, one letter at a time,
// indefinitely re-cycling A-Z) against a fixed 100-slot array - a genuine
// latent out-of-bounds write on real hardware too if a session ever ran
// long enough (~97 pickups), but one this port defensively clamps
// (`sabcSnakeSize` stops growing at `SABC_SNAKE_MAX_SIZE - 1`) rather than
// risk corrupting a *different* game's own global state the way an actual
// out-of-bounds write could in this shared, linker-less single binary -
// matching gameSimonbuino.c's own precedent of avoiding a real OOB write
// even where upstream's own version of it was harmless.

#define SABC_SNAKE_MAX_SIZE     100
#define SABC_SNAKE_DEFAULT_SIZE 3
#define SABC_SNAKE_W 4
#define SABC_SNAKE_H 4
#define SABC_SNAKE_V 4

#define SABC_LETTER_W 4
#define SABC_LETTER_H 6
#define SABC_LETTERS_MAX 26

#define SABC_GAME_LEVEL_MAX 9
#define SABC_GAME_SPEED 2      // ms shaved off the move interval per letter
#define SABC_MS_PER_FRAME 50   // this shim's own fixed 20fps logic-tick duration

// The real upstream title logo (a 64x36 bitmap shown via `gb.titleScreen
// (logo)`) is drawn via this project's own gbDrawBitmap() primitive.
// Upstream's own literal bytes use Arduino's `B00000000`-style binary
// notation, not valid syntax in this dialect - converted to hex via a
// small Python script that regexed every token out of the real .ino and
// re-emitted it as `hex(int(bits, 2))`, spot-checked by hand against the
// first 3 bytes (`B00000000,B00000000,B00000000` -> `0x0,0x0,0x0`, trivially
// correct) and a later non-zero row (`B11110111,B11101111,B11000000` ->
// `0xf7,0xef,0xc0`, confirmed via `$((2#11110111))`=247=0xf7 etc by hand).
int sabcTitleBitmap[290]=
{
64,36,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xe7,0x8e,0x0,0x0,0x0,0x1d,0x49,0x5d,0x14,0x50,0x0,0x0,0x0,0x11,0xd5,0x51,0x14,0x50,0x0,0x0,0x0,0x1d,0x5d,0x99,0xf4,0x50,0x65,0x55,0xc0,0x5,0x55,0x51,0x17,0x90,0x55,0x1d,0x40,0xfd,0x55,0x5d,0x14,0x50,0x65,0x55,0x40,0x0,0x0,0x1,0x14,0x50,0x55,0x55,0x40,0x0,0x0,0x1,0x17,0x8e,0x67,0x55,0xc0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3f,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3f,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3f,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3f,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3f,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3f,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xdf,0xbf,0x0,0x0,0x0,0x0,0x0,0xf,0xdf,0xbf,0x0,0x0,0x0,0x0,0x0,0xf,0xdf,0xbf,0x0,0x0,0x0,0x0,0x0,0xf,0xdf,0xbf,0x0,0x0,0x0,0x0,0x0,0xf,0xdf,0xbf,0x0,0x0,0x0,0x0,0x0,0xf,0xdf,0xbf,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf7,0xef,0xc0,0x0,0x0,0x0,0x0,0x0,0xf7,0xef,0xc0,0x0,0x0,0x0,0x0,0x0,0xf7,0xef,0xc0,0x0,0x0,0x0,0x0,0x0,0xf7,0xef,0xc0,0x0,0x0,0x0,0x0,0x0,0xf7,0xef,0xc0,0x0,0x0,0x0,0x0,0x0,0xf7,0xef,0xc0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0
};

enum SabcState
{
    SABC_STATE_TITLE   = 0,
    SABC_STATE_PLAY     = 1,
    SABC_STATE_MENU     = 2,
    SABC_STATE_GAMEOVER = 3
};

int sabcState;

// Verbatim copy of upstream's own game_levels[] millisecond-per-move table.
int sabcLevelDelays[9]= { 300, 250, 200, 175, 150, 125, 100, 90, 80 };

int sabcGameLevel;  // 1..9, persists across the whole session (like upstream)
int sabcMenuLevel;  // temp value while adjusting in the level menu
int sabcGameScore;
int sabcGameDelay;  // current ms-per-move interval (shrinks as letters are eaten)
int sabcMoveTimer;  // per-tick accumulator, compared against sabcGameDelay

int sabcSnakeDirection; // 1=right, 2=left, 3=up, 4=down
bool sabcSnakeHasMoved;
int sabcSnakeSize;
int sabcSnakeX[100];
int sabcSnakeY[100];
int sabcSnakeHeadX;
int sabcSnakeHeadY;

int sabcLetterX;
int sabcLetterY;
int sabcLetterIndex;    // 0..25, cycles - which letter ('A'+index) is on screen now
int sabcLetterMaxIndex; // 0..26, capped count of distinct letters collected (display only)
bool sabcNewLetter;     // true = letter needs a fresh spot picked
bool sabcGameOver;

enum SabcSoundFx
{
    SABC_FX_LETTER    = 0,
    SABC_FX_GAME_OVER = 1
};

// Verbatim pitch/duration columns from upstream's own soundfx[][] table
// (column 1 = pitch, column 7 = duration) - see this file's own header
// comment, point 4, for why the other columns (waveform/slide setup, no
// shim equivalent) are dropped.
int sabcSoundFx[2][8]= {
    { 0, 45, 26, 1, 0, 1, 7, 10 }, // letter collected
    { 0, 30, 34, 10, 0, 1, 7, 25 } // game over
};

void sabcPlaySoundFx( int fx )
{
    gbPlayNote( sabcSoundFx[ fx ][ 1 ], sabcSoundFx[ fx ][ 7 ] );
}

void sabcPlaySoundFxLetter()
{
    sabcPlaySoundFx( SABC_FX_LETTER );
}

void sabcPlaySoundFxGameOver()
{
    sabcPlaySoundFx( SABC_FX_GAME_OVER );
}

// Prints a single character at the current cursor position/font size -
// gbPrintString() itself needs a 0-terminated array, not a bare int.
void sabcPrintChar( int ch )
{
    char buf[2];
    buf[ 0 ] = ch;
    buf[ 1 ] = 0;
    gbPrintString( buf );
}

// Direct port of upstream's own InitGame() - see this file's own header
// comment for why the letter's own position/pending-search state is
// deliberately left untouched here, matching upstream exactly.
void sabcResetGame()
{
    int index;
    for( index = 0; index < SABC_SNAKE_MAX_SIZE; index++ )
    {
        if( index == 0 )
        {
            sabcSnakeX[ index ] = 30;
            sabcSnakeHeadX = 30;
            sabcSnakeHeadY = 30;
        }
        else
          sabcSnakeX[ index ] = 30 - ( index * ( SABC_SNAKE_W + 1 ) );

        sabcSnakeY[ index ] = 30;
    }

    sabcSnakeSize = SABC_SNAKE_DEFAULT_SIZE;
    sabcSnakeDirection = 1;
    sabcSnakeHasMoved = true; // see this file's own header comment on this small divergence
    sabcMoveTimer = 0;        // ditto

    sabcLetterIndex = 0;
    sabcLetterMaxIndex = 0;

    sabcGameScore = 0;
    sabcMenuLevel = sabcGameLevel;
    sabcGameDelay = sabcLevelDelays[ sabcGameLevel - 1 ];
    sabcGameOver = false;
}

void sabcBeginTitle()
{
    sabcState = SABC_STATE_TITLE;
}

void sabcBeginMenu()
{
    sabcState = SABC_STATE_MENU;
}

// Full reset + start - used by the title dismiss, a confirmed level-menu
// change, and a Game Over restart. Resuming from a *cancelled* level menu
// does NOT go through this (see sabcUpdateMenu() below) - matches
// upstream's own asymmetry exactly (see this file's own header comment).
void sabcBeginPlay()
{
    sabcResetGame();
    sabcState = SABC_STATE_PLAY;
}

void sabcUpdateGameScore()
{
    sabcGameScore = sabcGameScore + 1;

    if( sabcGameDelay > 60 )
      sabcGameDelay = sabcGameDelay - SABC_GAME_SPEED;

    // Defensively capped - see this file's own header comment for why.
    if( sabcSnakeSize < SABC_SNAKE_MAX_SIZE - 1 )
      sabcSnakeSize = sabcSnakeSize + 1;
}

// Direct port of upstream's own CheckLetterCollision() - see this file's
// own header comment for the one-move-step-late quirk this preserves
// (called with the snake's OLD, not-yet-shifted head position).
void sabcCheckLetterCollision()
{
    if( gbCollideRectRect( sabcSnakeX[ 0 ], sabcSnakeY[ 0 ], SABC_SNAKE_W, SABC_SNAKE_H, sabcLetterX, sabcLetterY, SABC_LETTER_W, SABC_LETTER_H ) )
    {
        sabcNewLetter = true;

        if( ( sabcLetterIndex + 1 ) <= ( SABC_LETTERS_MAX - 1 ) )
        {
            sabcLetterIndex = sabcLetterIndex + 1;
            if( sabcLetterMaxIndex != SABC_LETTERS_MAX )
              sabcLetterMaxIndex = sabcLetterMaxIndex + 1;
        }
        else
        {
            sabcLetterIndex = 0;
            sabcLetterMaxIndex = SABC_LETTERS_MAX;
        }

        sabcPlaySoundFxLetter();
        sabcUpdateGameScore();
    }
}

// Direct port of upstream's own MoveSnake() - the direction-change gating,
// the timed grid-step move, the letter/border/self collision checks, and
// the array-shift body movement are all preserved in the exact same order
// upstream wrote them (see this file's own header comment for the letter
// collision's own preserved quirk).
void sabcMoveSnake()
{
    if( sabcSnakeHasMoved )
    {
        if( gbRepeat( BTN_RIGHT, 1 ) && sabcSnakeDirection != 2 && sabcSnakeDirection != 1 )
        {
            sabcSnakeDirection = 1;
            sabcSnakeHasMoved = false;
        }
        else if( gbRepeat( BTN_LEFT, 1 ) && sabcSnakeDirection != 1 && sabcSnakeDirection != 2 )
        {
            sabcSnakeDirection = 2;
            sabcSnakeHasMoved = false;
        }
        else if( gbRepeat( BTN_UP, 1 ) && sabcSnakeDirection != 4 && sabcSnakeDirection != 3 )
        {
            sabcSnakeDirection = 3;
            sabcSnakeHasMoved = false;
        }
        else if( gbRepeat( BTN_DOWN, 1 ) && sabcSnakeDirection != 3 && sabcSnakeDirection != 4 )
        {
            sabcSnakeDirection = 4;
            sabcSnakeHasMoved = false;
        }
    }

    sabcMoveTimer = sabcMoveTimer + SABC_MS_PER_FRAME;
    if( sabcMoveTimer >= sabcGameDelay )
    {
        int nextX = sabcSnakeX[ 0 ];
        int nextY = sabcSnakeY[ 0 ];

        if( sabcSnakeDirection == 1 )
          nextX = nextX + ( SABC_SNAKE_V + 1 );
        else if( sabcSnakeDirection == 2 )
          nextX = nextX - ( SABC_SNAKE_V + 1 );
        else if( sabcSnakeDirection == 3 )
          nextY = nextY - ( SABC_SNAKE_V + 1 );
        else if( sabcSnakeDirection == 4 )
          nextY = nextY + ( SABC_SNAKE_V + 1 );

        sabcSnakeHeadX = nextX;
        sabcSnakeHeadY = nextY;

        // check collision with letter (uses the OLD head pos - see header comment)
        sabcCheckLetterCollision();

        // check collision with border
        if( nextX > LCDWIDTH || nextX < 0 || nextY < 0 || nextY > LCDHEIGHT )
          sabcGameOver = true;

        // move snake (array shift, with self-collision check against each
        // body segment's post-shift position - see header comment)
        int index;
        for( index = 0; index < SABC_SNAKE_MAX_SIZE; index++ )
        {
            int lastX = sabcSnakeX[ index ];
            int lastY = sabcSnakeY[ index ];

            if( index > 0 && index < sabcSnakeSize && gbCollideRectRect( sabcSnakeHeadX, sabcSnakeHeadY, SABC_SNAKE_W - 1, SABC_SNAKE_H - 1, nextX, nextY, SABC_SNAKE_W - 1, SABC_SNAKE_H - 1 ) )
              sabcGameOver = true;

            sabcSnakeX[ index ] = nextX;
            sabcSnakeY[ index ] = nextY;

            nextX = lastX;
            nextY = lastY;
        }

        sabcMoveTimer = 0;
        sabcSnakeHasMoved = true;
    }
}

void sabcDrawField()
{
    gbSetColor( 1 );
    gbDrawRect( 0, 0, LCDWIDTH, LCDHEIGHT );
}

void sabcDrawSnake()
{
    gbSetColor( 1 );

    int index;
    for( index = 0; index < sabcSnakeSize; index++ )
      gbFillRect( sabcSnakeX[ index ], sabcSnakeY[ index ], SABC_SNAKE_W, SABC_SNAKE_H );
}

// Direct port of upstream's own DrawLetter() - re-rolls a random spot
// (matching upstream's own ranged `random(min, max)` via `min + arand(range)`)
// until one doesn't overlap the snake's own body, exactly like upstream's
// own while() loop (no attempt cap - see gameSnakeClassic.c's own
// snkcMakeFood() for the identical un-capped precedent already established
// in this project).
void sabcDrawLetter()
{
    if( sabcNewLetter )
    {
        int x = -1;
        int y = -1;

        while( sabcNewLetter )
        {
            if( x == -1 )
              x = SABC_LETTER_W + arand( LCDWIDTH - 2 * SABC_LETTER_W );
            if( y == -1 )
              y = SABC_LETTER_H + arand( LCDHEIGHT - 2 * SABC_LETTER_H );

            int index;
            for( index = 0; index < sabcSnakeSize; index++ )
            {
                if( gbCollideRectRect( sabcSnakeX[ index ], sabcSnakeY[ index ], SABC_SNAKE_W, SABC_SNAKE_H, x, y, SABC_LETTER_W, SABC_LETTER_H ) )
                {
                    x = -1;
                    y = -1;
                    break;
                }
            }

            if( x != -1 && y != -1 )
            {
                sabcLetterX = x;
                sabcLetterY = y;
                sabcNewLetter = false;
            }
        }
    }

    gbSetColor( 1 );
    gbFontSize = 1;
    gbCursorX = sabcLetterX;
    gbCursorY = sabcLetterY;
    sabcPrintChar( 65 + sabcLetterIndex ); // 'A' + index - see header comment
}

void sabcUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 4, sabcTitleBitmap );

    gbFontSize = 1;
    gbCursorX = 14;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      sabcBeginPlay();
}

// Direct port of upstream's own GameMenu() (minus the dropped
// gb.changeGame() branch - see this file's own header comment). The
// up/down arrow triangles are drawn, at their real upstream coordinates,
// via the real gbFillTriangle() shim primitive.
void sabcUpdateMenu()
{
    gbSetColor( 1 );
    gbFontSize = 1;

    gbCursorX = 2;
    gbCursorY = 4;
    gbPrintString( "PICK LEVEL" );

    gbFillTriangle( 30, 10, 25, 15, 35, 15 );
    gbFillTriangle( 30, 28, 25, 23, 35, 23 );

    gbCursorX = 14;
    gbCursorY = 20;
    gbPrintString( "LEVEL " );
    gbPrintNumber( sabcMenuLevel );

    gbCursorX = 2;
    gbCursorY = 34;
    gbPrintString( "A=YES B=NO" );

    if( gbPressed( BTN_UP ) )
    {
        if( ( sabcMenuLevel + 1 ) <= SABC_GAME_LEVEL_MAX )
          sabcMenuLevel = sabcMenuLevel + 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        if( ( sabcMenuLevel - 1 ) >= 1 )
          sabcMenuLevel = sabcMenuLevel - 1;
    }
    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        sabcGameLevel = sabcMenuLevel;
        sabcBeginPlay();
    }
    if( gbPressed( BTN_B ) )
    {
        gbPlayOK();
        sabcState = SABC_STATE_PLAY; // resume unchanged, no reset - see header comment
    }
}

// Direct port of upstream's own Play() (plus the Button C pause handling
// that upstream itself checks right before calling Play() - hoisted in
// here, matching this project's own gameTaquin.c precedent for the same
// kind of hoist).
void sabcUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        sabcBeginMenu();
        return;
    }

    sabcMoveSnake();
    sabcDrawField();
    sabcDrawSnake();
    sabcDrawLetter();

    if( sabcGameOver )
    {
        sabcPlaySoundFxGameOver();
        sabcState = SABC_STATE_GAMEOVER;
    }
}

// Direct port of upstream's own GameOver() - see this file's own header
// comment for the letter-listing/score-multiplier display simplifications.
void sabcUpdateGameOver()
{
    gbSetColor( 1 );
    gbFontSize = 1;

    gbCursorX = 6;
    gbCursorY = 1;
    gbPrintString( "GAME OVER" );

    gbCursorX = 14;
    gbCursorY = 12;
    gbPrintString( "SCORE " );
    gbPrintNumber( sabcGameScore );

    gbCursorX = 14;
    gbCursorY = 22;
    gbPrintString( "LEVEL " );
    gbPrintNumber( sabcGameLevel );

    gbCursorX = 6;
    gbCursorY = 32;
    gbPrintString( "LETTERS " );
    gbPrintNumber( sabcLetterMaxIndex );

    gbCursorX = 14;
    gbCursorY = 40;
    gbPrintString( "PRESS B" );

    if( gbPressed( BTN_B ) )
    {
        gbPlayOK();
        sabcBeginPlay();
    }
}

void gameSnakeAbc_init()
{
    gbBegin();

    sabcGameLevel = 1;
    sabcLetterX = 50; // upstream's own cold-boot defaults - see header comment
    sabcLetterY = 30;
    sabcNewLetter = false;

    sabcBeginTitle();
}

void gameSnakeAbc_update()
{
    if( !gbUpdate() ) return;

    if( sabcState == SABC_STATE_TITLE ) sabcUpdateTitle();
    else if( sabcState == SABC_STATE_MENU ) sabcUpdateMenu();
    else if( sabcState == SABC_STATE_GAMEOVER ) sabcUpdateGameOver();
    else sabcUpdatePlay();

    gbRenderFrame();
}
