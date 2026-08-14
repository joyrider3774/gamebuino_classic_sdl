#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Solitaire (Andy O'Neill / aoneill01, MIT license -
// github.com/aoneill01/gamebuino-solitaire). Real Klondike solitaire: deal
// 7 tableau columns, draw 1 (easy) or 3 (hard) cards at a time from the
// stock, build foundations up in suit from ace to king, with a real 10-move
// undo stack and a real EEPROM-backed save-for-later/statistics feature.
// An earlier project audit pass had mis-recorded this game as GPLv3 - the
// real upstream `LICENSE` file (read directly for this port) is plain MIT.
//
// Upstream is 4 real files sharing one Arduino translation unit:
// `solitaire.ino` (game logic/drawing/EEPROM) plus `solitaire/card.h+.cpp`,
// `pile.h+.cpp`, `undo.h+.cpp` (real C++ classes) - all read in full before
// writing a line of this port, then consolidated into this one file, in
// dependency order (Card helpers, then Pile, then UndoAction/UndoStack,
// then game logic), matching this project's own established multi-file
// `.ino`+`.cpp`-to-one-file consolidation precedent.
//
// -----------------------------------------------------------------------
// CLASS FLATTENING (Card/Pile/UndoStack -> structs + free functions)
// -----------------------------------------------------------------------
// This dialect has no classes/methods (see gamebuinoShim.h's own header
// comment) - the same "flatten a real single-instance C++ library into
// plain C globals/functions" treatment used for gb itself applies equally
// to every OTHER real class upstream defines, matching the precedent
// already set porting SuperSpaceShooter's own classes in the sibling
// project:
//
// - `Card` (real upstream: a class wrapping one private `byte _value`,
//   value/suit/face-down bit-packed exactly as documented in card.cpp) is
//   flattened to a PLAIN `int` here, with the identical bit layout
//   (`soliMakeCard()`/`soliCardValue()`/`soliCardSuit()`/
//   `soliCardIsFaceDown()`/`soliCardIsRed()`/`soliCardFlip()` are direct
//   ports of `Card`'s own constructor/getters/`flip()`). This is a
//   deliberate, load-bearing choice, not just a simplification: a plain
//   `int` is exactly one word, so it can be returned BY VALUE from
//   functions like `soliPileGetCard()`/`soliPileRemoveTopCard()` (this
//   dialect's real "functions cannot return values of size > 1" rule -
//   see VIRCON32_C_DIALECT.md section 4/15.1) exactly like real
//   `Pile::getCard()`/`removeTopCard()` return `Card` by value upstream -
//   no out-pointer rewrite needed anywhere a `Card` crosses a function
//   boundary, because it was never more than one word to begin with.
// - `Pile` (real upstream: a class owning a heap-allocated `Card
//   _cards[]`, `_count`, `_maxCards`, plus public `x`/`y`/`isTableau`)
//   becomes `struct SoliPile` (scalar fields only: `id`/`maxCards`/
//   `count`/`x`/`y`/`isTableau`) plus free functions
//   (`soliPileAddCard()`/`soliPileAddPile()`/`soliPileGetCardCount()`/
//   `soliPileGetCard()`/`soliPileRemoveTopCard()`/`soliPileRemoveCards()`/
//   `soliPileEmpty()`/`soliPileShuffle()`/`soliPileNewDeck()`/
//   `soliPileGetMaxCards()`) taking `SoliPile*` where upstream took
//   `this`. The real per-pile card array is NOT stored as an array field
//   inside `SoliPile` itself - no other game shipped in this project has
//   proven an array-typed struct MEMBER compiles here (2D array
//   GLOBALS are proven, e.g. gameCastleDefence.c's own
//   `CdefFenceBitmaps`, but that's a different position in the grammar
//   than an array field inside a `struct { ... };` body) - rather than
//   spend a compile-error cycle finding out, every pile's cards live in
//   one shared global `int[SOLI_PILE_COUNT][SOLI_PILE_CAPACITY]
//   soliPileCards` table, and `SoliPile.id` is just the row index into
//   it. Every one of the 13 real board piles (stock/talon/4 foundations/7
//   tableau columns) uses its own real upstream `Location` enum value
//   (see below) directly as its row id, and the `moving` pile (upstream's
//   own real animation staging area, `Pile moving = Pile(13)`) gets the
//   one remaining row. This is an implementation-strategy choice made out
//   of caution, not a confirmed compiler rejection - flagged here per
//   this project's own "flag genuinely uncertain workarounds" norm, not
//   because a real wall was hit. Real per-pile capacities (stock=52,
//   talon=24, each foundation=13, each tableau column=20, moving=13) are
//   preserved exactly via each instance's own `maxCards` field, INCLUDING
//   real upstream's own tableau-column overflow behavior (`addCard()`
//   silently drops a card once `count == maxCards` - a real, virtually
//   unreachable-in-practice 20-card-per-column cap upstream ships with,
//   not "fixed" into unlimited here).
// - `UndoAction` (real upstream: a plain struct, not a class, but WITH
//   methods - `setCardCount()`/`getCardCount()`/`setRevealed()`/
//   `wasRevealed()`/`setDraw()`/`wasDraw()`/`setFlippedTalon()`/
//   `wasFlippedTalon()`, all just bit-twiddling one `byte special` field)
//   becomes `struct SoliUndoAction` (plain `int special`, plus
//   `sourceLoc`/`destLoc`) with the same 8 accessors as free functions
//   taking `SoliUndoAction*`. Real upstream's `source`/`destination`
//   fields are typed `Pile*` there; here they are `Location`-style int
//   IDs instead (`soliGetPileByLocation()` resolves one back to a real
//   `SoliPile*` on demand) - again a defensive choice avoiding an
//   unproven "pointer-typed struct member" pattern (as opposed to a
//   pointer LOCAL/GLOBAL/parameter/return, all of which are used freely
//   elsewhere in this file and are unambiguously supported), not a
//   confirmed rejection. Every real call site that used to write directly
//   into `action.source`/`.destination` instead computes the equivalent
//   `Location` value inline (always available at that exact point in the
//   real control flow - `sourcePile` is always one of the 13 real named
//   piles, never the `moving` staging pile itself).
// - `UndoStack` (real upstream: a class wrapping a fixed
//   `UndoAction[UNDO_STACK_SIZE]` ring buffer) becomes plain globals
//   (`soliUndoActions[10]`/`soliUndoIndex`/`soliUndoCount`) plus
//   `soliUndoPush()`/`soliUndoPop()`/`soliUndoIsEmpty()`. `soliUndoPop()`
//   takes an out-pointer rather than returning `SoliUndoAction` by value,
//   since that struct is 3 words (over this dialect's real 1-word
//   function-return limit) - the exact "convert a by-value struct return
//   into an out-pointer parameter" idiom VIRCON32_C_DIALECT.md documents
//   (section 15.2) for exactly this situation.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment). `byte` became plain `int`
// (avrCompat.h). Real upstream `switch` statements (`getActiveLocationPile()`,
// `drawSuit()`, `drawValue()`) all became if/else-if chains, matching this
// project's own "no switch statement used anywhere in this project's
// ported games" convention. Real `byte&`/`bool&` reference-out parameters
// (`getCursorDestination(byte& x, byte& y, bool& flipped)`) became real
// `int*`/`bool*` pointer-out parameters. `min()`/`max()` (Arduino macros)
// became `gbMin()`/`gbMax()` (no ternary operator in this dialect, so a
// `(a<b?a:b)`-style macro wouldn't have compiled either). `random(N)`
// became `arand(N)`; the one real ranged case,
// `random(0x0100,0x0200)`/`random(0x0200)` (the win-animation card-bounce
// initial velocity), became `0x0100 + arand(0x0100)`/`arand(0x0200)` (this
// project's own established ranged-random conversion formula). Every real
// array declaration uses this dialect's own required `TYPE name[N]`
// order, never `TYPE name[N]`. Global naming prefix: `soli` (checked
// unused by every other game shipped or concurrently in flight in this
// batch).
//
// -----------------------------------------------------------------------
// BLOCKING SCREENS -> EXPLICIT STATE MACHINE
// -----------------------------------------------------------------------
// Real upstream's `showTitle()` is a single function with a real `goto`-
// driven loop (a `start:` label re-entered on menu cancel, an `askAgain:`
// label re-entered after viewing statistics) mixing a blocking
// `gb.titleScreen()` call, a blocking `gb.menu()` call, and blocking
// `displayStatistics()`/`pause()` calls - all converted here into five
// explicit `SOLI_APP_*` states (`_TITLE`/`_NEWGAME_MENU`/`_STATS`/
// `_PLAYING`/`_PAUSE_MENU`), matching the "blocking loop -> explicit
// resumable state" treatment this project uses throughout (see
// gamePong.c's own header comment). Real upstream's own binary
// `gb.menu(items, count)` widget (used for both the new-game menu and the
// pause menu) has no equivalent in this shim - hand-rolled UP/DOWN-
// navigate, A-to-confirm replacements (`soliUpdateNewGameMenu()`/
// `soliUpdatePauseMenu()`) follow this project's own established
// `gameConduit.c`-style precedent exactly (no cancel gesture, matching
// that same precedent - real `gb.menu()`'s own actual cancel button isn't
// knowable without real `Menu.cpp` source, unavailable in this isolated
// copy). Real `Gamebuino::titleScreen()`'s own internal text/logo layout
// is not reproduced (a real library internal, not upstream's own code) -
// this port draws the real logo bitmap directly (`soliTitleBitmap`,
// copied byte-for-byte from upstream's own real `title[]` PROGMEM table)
// with this port's own simple centered "PRESS A" prompt beneath it,
// matching the same treatment `gameBlockdude.c` already established for
// its own real titleScreen() splash bitmap.
//
// Real upstream's pause menu shows only 3 items (Resume/Quit/Statistics)
// when paused mid-ANIMATION (`mode != selecting` - Save/Undo would be
// unsafe against a pile mid-transfer) and 4 or 5 items (adding Save, then
// Undo if the undo stack isn't empty) when paused with the cursor
// genuinely idle (`mode == selecting`) - preserved exactly via
// `soliUpdatePauseMenu()`'s own `fullMenu`/`showUndo` gate, reading
// `soliMode` directly (mode itself is simply never advanced while paused,
// since `soliUpdatePlaying()` - the only place mode-driven logic runs -
// is not called at all while `soliAppState == SOLI_APP_PAUSE_MENU`,
// matching upstream's own real "pause() blocks the rest of loop() from
// ever running" behavior).
//
// -----------------------------------------------------------------------
// SOUND: a real, direct port - patternA/patternB now play for real
// -----------------------------------------------------------------------
// Real upstream calls `gb.sound.playPattern(patternA/B, 0)` from
// `playSoundA()`/`playSoundB()` - two tiny real note patterns (3 words
// each, `const uint16_t patternA[]/patternB[] PROGMEM`), now ported
// verbatim as `soliPatternA`/`soliPatternB` (byte-for-byte identical hex
// values, real `0x0000` terminator preserved) and played for real via
// `gbPlayPattern()` - the real tracker/pattern engine this shim now
// implements (see gamebuinoShim.h's own Sound section header comment).
// Neither pattern is preceded upstream by a `changeInstrumentSet()`/
// `command(CMD_INSTRUMENT,...)` call, so both correctly play through
// channel 0's own real default square-wave instrument, matching real
// hardware exactly - no approximation needed anymore. `soliPlaySoundA()`
// (real call sites: drawing/dealing a card, triggering a fast-foundation
// auto-move) and `soliPlaySoundB()` (real call sites: attempting to place
// a held pile - fires on every attempt, legal or not, not specifically an
// error cue - and the win-animation's own bounce-off-the-bottom impact)
// are otherwise unchanged - same call sites, same real upstream trigger
// conditions, just genuine pattern playback instead of a one-shot-tone
// stand-in now that the shim supports it.
//
// -----------------------------------------------------------------------
// EEPROM: a full, real port (statistics AND save-for-later), not a stub
// -----------------------------------------------------------------------
// Real upstream's `readEeprom()`/`writeEeprom()`/`savePile()`/
// `loadPile()` are ported through this shim's own `eeprom_read_byte()`/
// `eeprom_write_byte()`/`eeprom_update_byte()`/`eeprom_read_word()`/
// `eeprom_write_word()` (see eepromShim.h) at the exact same real byte
// addresses upstream uses:
//   addr 0        - magic number (170, `SOLI_EEPROM_MAGIC_NUMBER`)
//   addr 1-2/3-4/5-6/7-8 - easyGameCount/easyGamesWon/hardGameCount/
//                    hardGamesWon (real upstream `int`s, 2 bytes each via
//                    `EEPROM.put()` - ported via `eeprom_write_word()`/
//                    `eeprom_read_word()`, a direct, mechanical
//                    equivalent, not a behavioral change, matching
//                    `gameBlockdude.c`'s own established word-vs-two-byte
//                    reasoning)
//   addr 9        - "has a saved game" flag (real upstream `EEPROM.update`)
//   addr 10       - cardsToDraw (1 real byte)
//   addr 11+      - each of the 13 real board piles, stock/talon/4
//                    foundations/7 tableau columns in that exact real
//                    order, each as 1 count byte + that pile's own real
//                    `maxCards` card bytes (`soliSavePile()`/
//                    `soliLoadPile()`, direct ports of upstream's own
//                    `savePile()`/`loadPile()`) - 281 bytes total,
//                    comfortably inside this shim's own 1024-byte-per-
//                    game EEPROM slot.
// `soliReadEeprom()` explicitly sets `soliContinueGame = false` on a
// magic-mismatch (upstream instead just returns, leaving `continueGame`
// at whatever it last was) - a safe, purely-defensive simplification, not
// a behavior change: `continueGame` is a plain global that always starts
// at C's own zero-init `false` by construction, matching what an
// unconditional explicit reset produces in every real reachable case
// (this function is only ever called once per real title-screen visit,
// and every prior visit that ever set it `true` also always writes the
// magic byte back via `soliWriteEeprom()`, so a stale `true` from a
// previous visit can never survive to be silently reused here).
//
// -----------------------------------------------------------------------
// A REAL UPSTREAM BUG FOUND AND PRESERVED: the win-animation "cards
// bounce off the foundations" sequence never actually plays
// -----------------------------------------------------------------------
// Traced directly through the real source, not assumed: `showTitle()`
// unconditionally sets `gb.display.persistence = true;` as its own very
// first line - and `showTitle()` is the ONLY setup path in the entire
// game (called once from `setup()`, and again from every real `pause()`
// "Quit"/"Save for later" branch) - so `persistence` is already `true`
// before any real gameplay frame is ever drawn, on every single run.
// Nothing in the entire real source ever sets it back to `false`.
// `drawWonGame()`'s own real logic is `if (!gb.display.persistence) {
// gb.display.persistence = true; drawBoard(); initializeCardBounce(); }`
// - since `persistence` is already `true` by the time `drawWonGame()` can
// ever run at all, this branch is genuine, real, unreachable dead code:
// `initializeCardBounce()` - the ONLY place a foundation card is ever
// actually picked up and given a real bounce velocity - is never called.
// The physics code just below the dead branch still runs unconditionally
// every tick regardless, though, operating on `bounce`'s own real,
// zero-initialized default fields (`x=0,y=0,xVelocity=0,yVelocity=0`,
// `card` defaulting to a real upstream `Card()` - ace of spades, face up)
// - so what actually happens on real hardware, traced through
// completely: gravity accumulates on `yVelocity` every tick, `y` grows
// from 0 and eventually exceeds the bottom-bounce threshold, reversing
// and damping `yVelocity` by the real `*-4/5` formula forever - i.e. a
// single default ace-of-spades card bounces up and down forever at a
// permanently-fixed `x=0` (since `xVelocity` is never set away from its
// zero default), and the exit condition
// (`bounce.x+(10<<8)<0 || bounce.x>LCDWIDTH<<8`) can never fire (`x`
// never moves), so `showTitle()` is never called again automatically -
// winning the game does NOT show the intended 52-card celebratory
// sequence and does NOT return to the title screen on its own. This is
// preserved here exactly (`soliPersistence` mirrors the real field,
// `soliBeginTitle()` sets it `true` at the same real call site,
// `soliDrawWonGame()`'s own dead branch is real, unreachable dead code
// here too, for the identical reason) - per this project's own rule to
// preserve a genuine, traced-through upstream bug rather than silently
// fix it. A player is not stuck, though: real upstream's own `pause()`
// (Button C, checked unconditionally at the very top of `loop()`
// regardless of `mode`) still works during this stuck bounce, and
// choosing "Quit" from there is a real, working way back to the title
// screen - reproduced identically here via `soliUpdatePlaying()`'s own
// `gbPressed(BTN_C)` check running before any mode dispatch, exactly
// mirroring upstream's own unconditional placement.
//
// One deliberate Vircon32-specific adaptation on top of that real, traced
// bug: this shim has no equivalent to real `Display::persistence` at the
// RENDERING level at all (`gbUpdate()` always calls `gbClear()` every
// real tick unconditionally - see this project's own CLAUDE.md and
// `gameBlockdude.c`'s/`game2048.c`'s own header comments on why dropping
// persistence is always safe here: every game already redraws everything
// relevant every tick). Faithfully leaving `soliDrawBoard()` un-called
// during the stuck-bounce state (mirroring upstream's own real "never
// redraws the board again" behavior under this bug) would render as a
// genuinely BLANK screen behind the bouncing card here (since nothing
// else ever draws over the auto-cleared buffer), materially WORSE than
// real hardware's own frozen-last-frame appearance - a second,
// independent, pre-existing shim limitation compounding with the first,
// real bug to produce a strictly worse result than real hardware ever
// had. `soliDrawWonGame()` therefore calls `soliDrawBoard()`
// unconditionally every tick regardless of `soliPersistence` (matching
// the same "call the real full redraw unconditionally since the shim
// always clears anyway" adaptation `game2048.c`/`gameBlockdude.c` already
// made for their own dropped `persistence` usage) - this correctly shows
// the true, static, fully-won board (all 4 foundations still full, since
// `initializeCardBounce()` never runs to remove a card from them) with
// the one stuck bouncing default card on top, matching what the traced-
// through real bug actually produces in spirit, without the unrelated
// "blank screen" artifact a literal one-time-only redraw would add here.
//
// -----------------------------------------------------------------------
// A VIRCON32-DIALECT-SPECIFIC FIX: logical, not arithmetic, right shift
// -----------------------------------------------------------------------
// Real upstream extracts pixel coordinates from the win-animation's own
// 8.8 fixed-point `bounce.x`/`bounce.y` via `bounce.x >> 8`/`bounce.y >>
// 8` - safe on real AVR hardware, where `>>` on a signed `int` sign-
// extends. VIRCON32_C_DIALECT.md documents directly that this dialect's
// `>>` is a LOGICAL shift (zero-fill, not sign-extending) - confirmed as
// a real, previously-hit bug in this exact codebase already
// (avrCompat.h's own `arand()` header comment describes the identical
// hazard biting a different game's own hollow-frequency formula). Given
// this preserved bug keeps `bounce.x` permanently at exactly `0` (see
// above - never negative in practice), only `bounce.y` is a real
// theoretical risk (a large rebound could in principle carry `y`
// transiently above the top of the screen, i.e. negative, before
// gravity pulls it back down) - `soliDrawWonGame()` uses plain integer
// division (`/ 256`) instead of `>> 8` for both coordinate extractions,
// a simple, safe substitute that cannot misinterpret a negative
// fixed-point value as a huge positive one (differing from a true
// arithmetic shift by at most 1 pixel on a negative, non-multiple-of-256
// input - immaterial for this purely decorative animation). The `<< 8`
// conversions elsewhere are never given this treatment since they only
// ever shift small non-negative pixel coordinates, where logical and
// arithmetic shifts agree exactly.
//
// -----------------------------------------------------------------------
// SHIM PRIMITIVES USED - no new primitive needed
// -----------------------------------------------------------------------
// Every drawing/input/sound/EEPROM primitive this port needs already
// exists in `gamebuinoShim.h`/`eepromShim.h`
// (gbDrawBitmap/gbDrawPixel/gbDrawFastHLine/gbDrawFastVLine/gbDrawRect/
// gbFillRect/gbSetColor/gbCursorX/gbCursorY/gbPrintString/gbPrintNumber/
// gbPressed/gbRepeat/gbFrameCount/gbPlayPattern/gbMin/gbMax/
// eeprom_read_byte/eeprom_write_byte/eeprom_update_byte/
// eeprom_read_word/eeprom_write_word) - nothing new was added.

#define SOLI_SUIT_SPADE 0
#define SOLI_SUIT_CLUB 1
#define SOLI_SUIT_HEART 2
#define SOLI_SUIT_DIAMOND 3

#define SOLI_ACE 1
#define SOLI_TWO 2
#define SOLI_THREE 3
#define SOLI_FOUR 4
#define SOLI_FIVE 5
#define SOLI_SIX 6
#define SOLI_SEVEN 7
#define SOLI_EIGHT 8
#define SOLI_NINE 9
#define SOLI_TEN 10
#define SOLI_JACK 11
#define SOLI_QUEEN 12
#define SOLI_KING 13

// Ace of spades, face up - matches real upstream `Card()`'s own default
// constructor `Card(ace, spade, false)`.
#define SOLI_DEFAULT_CARD 1

// Real upstream `Location` enum values, in the exact real order (arithmetic
// on these values, e.g. `activeLocation + 1`, is real load-bearing upstream
// logic - kept as plain ints rather than a real enum type since this
// dialect implicitly converts enum->int but never int->enum, and this game
// assigns arithmetic results straight back into the "enum" variable
// throughout - see gameShipwrek.c's own `SHIP_STATE_*` for the identical,
// already-proven precedent of using plain int constants for exactly this
// reason). Also doubles as each real board pile's own row id into
// `soliPileCards[]` below.
#define SOLI_LOC_STOCK 0
#define SOLI_LOC_TALON 1
#define SOLI_LOC_FOUNDATION1 2
#define SOLI_LOC_FOUNDATION2 3
#define SOLI_LOC_FOUNDATION3 4
#define SOLI_LOC_FOUNDATION4 5
#define SOLI_LOC_TABLEAU1 6
#define SOLI_LOC_TABLEAU2 7
#define SOLI_LOC_TABLEAU3 8
#define SOLI_LOC_TABLEAU4 9
#define SOLI_LOC_TABLEAU5 10
#define SOLI_LOC_TABLEAU6 11
#define SOLI_LOC_TABLEAU7 12

// Real upstream `GameMode` enum values, same plain-int treatment as above
// (this one is only ever compared/assigned directly, but kept consistent).
#define SOLI_MODE_DEALING 0
#define SOLI_MODE_SELECTING 1
#define SOLI_MODE_DRAWING_CARDS 2
#define SOLI_MODE_MOVING_PILE 3
#define SOLI_MODE_ILLEGAL_MOVE 4
#define SOLI_MODE_FAST_FOUNDATION 5
#define SOLI_MODE_WON_GAME 6

// This port's own app-level states - see this file's own header comment
// on "BLOCKING SCREENS -> EXPLICIT STATE MACHINE".
#define SOLI_APP_TITLE 0
#define SOLI_APP_NEWGAME_MENU 1
#define SOLI_APP_STATS 2
#define SOLI_APP_PLAYING 3
#define SOLI_APP_PAUSE_MENU 4

#define SOLI_MAX_CARDS_DRAWN_IN_PILE 10
#define SOLI_EEPROM_MAGIC_NUMBER 170
#define SOLI_UNDO_STACK_SIZE 10

// Shared card storage for every real pile - see this file's own header
// comment on why cards are not an array FIELD inside SoliPile itself.
// 14 rows: the 13 real named board piles (indexed by their own real
// Location value above) plus row 13 for the real upstream `moving`
// staging pile. 52 columns: the largest real per-pile capacity (the
// stock pile).
#define SOLI_PILE_COUNT 14
#define SOLI_PILE_CAPACITY 52
#define SOLI_MOVING_PILE_ID 13

int soliPileCards[SOLI_PILE_COUNT][SOLI_PILE_CAPACITY];

typedef struct{
    int id;
    int maxCards;
    int count;
    int x;
    int y;
    bool isTableau;
} SoliPile;

typedef struct{
    int special;
    int sourceLoc;
    int destLoc;
} SoliUndoAction;

// Direct port of real upstream's own `CardAnimation` struct (used only to
// animate the initial deal).
typedef struct{
    int card;
    int tableauIndex;
    int x;
    int y;
    int destX;
    int destY;
} SoliCardAnimation;

// Direct port of real upstream's own `CardBounce` struct (the win
// animation's own single currently-bouncing card).
typedef struct{
    int card;
    int x;
    int y;
    int xVelocity;
    int yVelocity;
} SoliCardBounce;

// ---- Real upstream globals (mode/cursor/piles/undo/stats) ----

int soliAppState;
int soliMenuIndex;
int soliPauseMenuIndex;
int soliStatsReturnState;
bool soliPersistence; // see this file's own header comment on the preserved win-animation bug

int soliMode;
int soliActiveLocation;
int soliCardIndex;
int soliCursorX;
int soliCursorY;

SoliPile soliMoving;
int soliRemainingDraws;
int soliCardsToDraw;

SoliPile* soliSourcePile;
int soliSourcePileLocation;

SoliPile soliStockDeck;
SoliPile soliTalonDeck;
SoliPile soliFoundations[4];
SoliPile soliTableau[7];

SoliUndoAction soliUndoActions[10];
int soliUndoIndex;
int soliUndoCount;

SoliCardAnimation soliCardAnimations[28];
int soliCardAnimationCount;

SoliCardBounce soliBounce;
int soliBounceIndex;

int soliEasyGameCount;
int soliEasyGamesWon;
int soliHardGameCount;
int soliHardGamesWon;
bool soliContinueGame;

// Real upstream `title[]` PROGMEM bitmap (64x36, the real Solitaire title
// logo), copied byte-for-byte - this shim's own `gbDrawBitmap()` format
// (width, height, then packed row bytes) matches real
// `Display::drawBitmap()`'s own format exactly, so no reshaping was
// needed, only the literal `byte`->`int` per-cell widening every bitmap
// table in this project already uses.
int soliTitleBitmap[290] = { 64, 36,
0x0,0x0,0x80,0x0,0x1,0x24,0x0,0x0,
0x0,0x84,0x80,0x0,0x1,0x4,0x0,0x0,
0x0,0x88,0x80,0x0,0x1,0x4,0x0,0x0,
0x0,0x90,0x8F,0x16,0x1D,0x24,0xCE,0x0,
0x0,0xA0,0x99,0x99,0x33,0x25,0x99,0x0,
0x0,0xC0,0x90,0x91,0x21,0x25,0x11,0x0,
0x0,0xA0,0x90,0x91,0x21,0x26,0x1F,0x0,
0x0,0x90,0x90,0x91,0x21,0x25,0x10,0x0,
0x0,0x88,0x99,0x91,0x33,0x25,0x98,0x0,
0x0,0x84,0x8F,0x11,0x1D,0x24,0xCF,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x10,0x1C,0x70,0xE,0x0,0x10,0x0,
0x0,0x10,0x3E,0xF8,0x3F,0x80,0x10,0x0,
0x0,0x38,0x3E,0xF8,0x3F,0x80,0x38,0x0,
0x0,0x7C,0x3F,0xF8,0x3F,0x80,0x7C,0x0,
0x0,0xFE,0x3F,0xF9,0xDF,0x70,0xFE,0x0,
0x1,0xFF,0x1F,0xF3,0xFF,0xF9,0xFF,0x0,
0x3,0xFF,0x8F,0xF3,0xFF,0xFB,0xFF,0x80,
0x3,0xFF,0x8F,0xE3,0xFF,0xF9,0xFF,0x0,
0x3,0xFF,0x87,0xC3,0xF5,0xF8,0xFE,0x0,
0x1,0xD7,0x3,0x81,0xE4,0xF0,0x7C,0x0,
0x0,0x10,0x3,0x80,0x4,0x0,0x38,0x0,
0x0,0x38,0x1,0x0,0xE,0x0,0x10,0x0,
0x0,0xFE,0x1,0x0,0x3F,0x80,0x10,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x90,0x0,0x40,0x0,0x0,
0x0,0x3C,0x0,0x82,0x0,0x0,0x0,0x0,
0x0,0x44,0x0,0x82,0x0,0x0,0x0,0x0,
0x0,0x40,0x78,0x97,0x9C,0x4B,0x38,0x0,
0x0,0x60,0xCC,0x92,0x2,0x4C,0x64,0x0,
0x0,0x18,0x84,0x92,0x2,0x48,0x44,0x0,
0x0,0xC,0x84,0x92,0x1E,0x48,0x7C,0x0,
0x0,0x4,0x84,0x92,0x22,0x48,0x40,0x0,
0x0,0x44,0xCC,0x92,0x22,0x48,0x60,0x0,
0x0,0x78,0x78,0x93,0x9E,0x48,0x3C,0x0
};

// soliBeginTitle() has no dependencies of its own (just two global
// assignments) so it is defined early, ahead of every function below that
// needs to call into it (soliDrawWonGame() included).
void soliBeginTitle()
{
    // Real upstream: `start: gb.display.persistence = true;` - see this
    // file's own header comment on the real bug this produces.
    soliPersistence = true;
    soliAppState = SOLI_APP_TITLE;
}

// -----------------------------------------------------------------------
// Card (flattened to a plain int - see this file's own header comment)
// -----------------------------------------------------------------------

int soliMakeCard( int value, int suit, bool faceDown )
{
    int result = value | ( suit << 4 );
    if( faceDown ) result = result | 0x80;
    return result;
}

int soliCardValue( int card )
{
    return card & 0x0F;
}

int soliCardSuit( int card )
{
    return ( card >> 4 ) & 0x03;
}

bool soliCardIsFaceDown( int card )
{
    return ( card & 0x80 ) != 0;
}

bool soliCardIsRed( int card )
{
    int suit = soliCardSuit( card );
    return suit == SOLI_SUIT_HEART || suit == SOLI_SUIT_DIAMOND;
}

int soliCardFlip( int card )
{
    return card ^ 0x80;
}

// -----------------------------------------------------------------------
// Pile (flattened to struct SoliPile + free functions)
// -----------------------------------------------------------------------

void soliPileInit( SoliPile* pile, int id, int maxCards )
{
    pile->id = id;
    pile->maxCards = maxCards;
    pile->count = 0;
}

int soliPileGetCardCount( SoliPile* pile )
{
    return pile->count;
}

int soliPileGetCard( SoliPile* pile, int indexFromTop )
{
    if( indexFromTop < pile->count ) return soliPileCards[ pile->id ][ pile->count - indexFromTop - 1 ];
    return SOLI_DEFAULT_CARD;
}

int soliPileGetMaxCards( SoliPile* pile )
{
    return pile->maxCards;
}

void soliPileAddCard( SoliPile* pile, int card )
{
    if( pile->count < pile->maxCards )
    {
        soliPileCards[ pile->id ][ pile->count ] = card;
        pile->count = pile->count + 1;
    }
}

void soliPileAddPile( SoliPile* pile, SoliPile* src )
{
    int i;
    for( i = soliPileGetCardCount( src ) - 1; i >= 0; i = i - 1 )
    {
        soliPileAddCard( pile, soliPileGetCard( src, i ) );
    }
}

int soliPileRemoveTopCard( SoliPile* pile )
{
    if( pile->count > 0 )
    {
        pile->count = pile->count - 1;
        return soliPileCards[ pile->id ][ pile->count ];
    }
    return SOLI_DEFAULT_CARD;
}

void soliPileRemoveCards( SoliPile* pile, int count, SoliPile* destination )
{
    if( count > pile->count ) count = pile->count;
    pile->count = pile->count - count;
    int i;
    for( i = 0; i < count; i++ ) soliPileAddCard( destination, soliPileCards[ pile->id ][ pile->count + i ] );
}

void soliPileEmpty( SoliPile* pile )
{
    pile->count = 0;
}

void soliPileShuffle( SoliPile* pile )
{
    int i;
    for( i = 0; i < pile->count; i++ )
    {
        int randomIndex = arand( pile->count - i );
        int tmp = soliPileCards[ pile->id ][ randomIndex ];
        soliPileCards[ pile->id ][ randomIndex ] = soliPileCards[ pile->id ][ pile->count - i - 1 ];
        soliPileCards[ pile->id ][ pile->count - i - 1 ] = tmp;
    }
}

void soliPileNewDeck( SoliPile* pile )
{
    soliPileEmpty( pile );
    int suit;
    int value;
    for( suit = SOLI_SUIT_SPADE; suit <= SOLI_SUIT_DIAMOND; suit++ )
    {
        for( value = SOLI_ACE; value <= SOLI_KING; value++ )
        {
            soliPileAddCard( pile, soliMakeCard( value, suit, true ) );
        }
    }
}

// -----------------------------------------------------------------------
// UndoAction / UndoStack (flattened to struct SoliUndoAction + free
// functions, plus plain global ring-buffer state)
// -----------------------------------------------------------------------

void soliUndoActionSetCardCount( SoliUndoAction* action, int cardCount )
{
    action->special = ( action->special & 0xF0 ) | ( cardCount & 0x0F );
}

int soliUndoActionGetCardCount( SoliUndoAction* action )
{
    return action->special & 0x0F;
}

void soliUndoActionSetRevealed( SoliUndoAction* action )
{
    action->special = action->special | 0x80;
}

bool soliUndoActionWasRevealed( SoliUndoAction* action )
{
    return ( action->special & 0x80 ) != 0;
}

void soliUndoActionSetDraw( SoliUndoAction* action )
{
    action->special = action->special | 0x40;
}

bool soliUndoActionWasDraw( SoliUndoAction* action )
{
    return ( action->special & 0x40 ) != 0;
}

void soliUndoActionSetFlippedTalon( SoliUndoAction* action )
{
    action->special = action->special | 0x20;
}

bool soliUndoActionWasFlippedTalon( SoliUndoAction* action )
{
    return ( action->special & 0x20 ) != 0;
}

bool soliUndoIsEmpty()
{
    return soliUndoCount == 0;
}

void soliUndoPush( SoliUndoAction* action )
{
    soliUndoActions[ soliUndoIndex ] = *action;
    soliUndoIndex = soliUndoIndex + 1;
    if( soliUndoIndex >= SOLI_UNDO_STACK_SIZE ) soliUndoIndex = 0;
    soliUndoCount = soliUndoCount + 1;
    if( soliUndoCount > SOLI_UNDO_STACK_SIZE ) soliUndoCount = SOLI_UNDO_STACK_SIZE;
}

void soliUndoPop( SoliUndoAction* result )
{
    if( !soliUndoIsEmpty() )
    {
        if( soliUndoIndex == 0 ) soliUndoIndex = SOLI_UNDO_STACK_SIZE - 1;
        else soliUndoIndex = soliUndoIndex - 1;
        soliUndoCount = soliUndoCount - 1;
        *result = soliUndoActions[ soliUndoIndex ];
        return;
    }
    result->special = 0;
    result->sourceLoc = SOLI_LOC_STOCK;
    result->destLoc = SOLI_LOC_STOCK;
}

// Direct port of real upstream `getActiveLocationPile()` - takes an
// explicit Location rather than always reading the global
// `soliActiveLocation`, since several real call sites resolve a
// DIFFERENT stored location (e.g. an undo action's own `sourceLoc`/
// `destLoc`), not just "whatever's currently selected".
SoliPile* soliGetPileByLocation( int loc )
{
    if( loc == SOLI_LOC_STOCK ) return &soliStockDeck;
    if( loc == SOLI_LOC_TALON ) return &soliTalonDeck;
    if( loc >= SOLI_LOC_FOUNDATION1 && loc <= SOLI_LOC_FOUNDATION4 ) return &soliFoundations[ loc - SOLI_LOC_FOUNDATION1 ];
    return &soliTableau[ loc - SOLI_LOC_TABLEAU1 ];
}

// -----------------------------------------------------------------------
// Sound (see this file's own header comment - a real, direct port now,
// not a one-shot-tone substitution)
// -----------------------------------------------------------------------

// Real upstream `patternA[]`/`patternB[]` (solitaire.ino), copied
// byte-for-byte (3 words each, real `0x0000`-terminated). Neither ever
// gets its own `changeInstrumentSet()`/`command(CMD_INSTRUMENT,...)` call
// upstream, so both play through channel 0's real default square-wave
// instrument.
int soliPatternA[3] = { 0x0045, 0x0118, 0x0000 };
int soliPatternB[3] = { 0x0045, 0x0108, 0x0000 };

void soliPlaySoundA()
{
    gbPlayPattern( soliPatternA, 0 );
}

void soliPlaySoundB()
{
    gbPlayPattern( soliPatternB, 0 );
}

// -----------------------------------------------------------------------
// Small shared helpers
// -----------------------------------------------------------------------

// Direct port of real upstream `cardYPosition()`.
int soliCardYPosition( SoliPile* pile, int cardIndex )
{
    if( pile->isTableau )
    {
        if( cardIndex > SOLI_MAX_CARDS_DRAWN_IN_PILE - 1 ) return pile->y;
        return pile->y + 2 * ( gbMin( soliPileGetCardCount( pile ), SOLI_MAX_CARDS_DRAWN_IN_PILE ) - cardIndex - 1 );
    }
    return pile->y;
}

// Direct port of real upstream `updatePosition()` - real upstream declares
// this `byte updatePosition(byte current, byte destination)`, whose
// intermediate `byte delta = (destination-current)/3` truncates through
// an 8-bit wraparound that (traced through) always cancels out exactly
// for every real coordinate this game ever uses (all real screen
// positions are 0-83, well inside the range where AVR's own modulo-256
// byte arithmetic reproduces plain signed-int arithmetic bit-for-bit) -
// so this port uses plain `int` throughout with no special-casing needed,
// per this project's own general avrCompat.h precedent that widening
// `byte`->`int` costs only range, not correctness, in the vast majority
// of real cases.
int soliUpdatePosition( int current, int destination )
{
    if( current == destination ) return current;

    int delta = ( destination - current ) / 3;
    if( delta == 0 && ( gbFrameCount % 3 ) == 0 )
    {
        if( destination > current ) delta = 1;
        else delta = -1;
    }
    return current + delta;
}

void soliDrawAndFlip( SoliPile* source, SoliPile* destination )
{
    int card = soliPileRemoveTopCard( source );
    card = soliCardFlip( card );
    soliPileAddCard( destination, card );
}

// -----------------------------------------------------------------------
// Drawing - suits/values/segments/cards/piles/cursor
// -----------------------------------------------------------------------

void soliDrawHeart( int x, int y )
{
    gbDrawPixel( x + 1, y );
    gbDrawPixel( x + 3, y );
    gbDrawFastHLine( x, y + 1, 5 );
    gbDrawFastHLine( x, y + 2, 5 );
    gbDrawFastHLine( x + 1, y + 3, 3 );
    gbDrawPixel( x + 2, y + 4 );
}

void soliDrawDiamond( int x, int y )
{
    gbDrawPixel( x + 2, y );
    gbDrawFastHLine( x + 1, y + 1, 3 );
    gbDrawFastHLine( x, y + 2, 5 );
    gbDrawFastHLine( x + 1, y + 3, 3 );
    gbDrawPixel( x + 2, y + 4 );
}

void soliDrawSpade( int x, int y )
{
    gbDrawPixel( x + 2, y );
    gbDrawFastHLine( x + 1, y + 1, 3 );
    gbDrawFastHLine( x, y + 2, 5 );
    gbDrawFastHLine( x, y + 3, 5 );
    gbDrawPixel( x + 2, y + 4 );
}

void soliDrawClub( int x, int y )
{
    gbDrawFastHLine( x + 1, y, 3 );
    gbDrawFastHLine( x + 1, y + 2, 3 );
    gbDrawFastVLine( x, y + 1, 3 );
    gbDrawFastVLine( x + 4, y + 1, 3 );
    gbDrawFastVLine( x + 2, y + 1, 4 );
}

void soliDrawSuit( int x, int y, int suit )
{
    if( suit == SOLI_SUIT_SPADE ) soliDrawSpade( x, y );
    else if( suit == SOLI_SUIT_CLUB ) soliDrawClub( x, y );
    else if( suit == SOLI_SUIT_HEART ) soliDrawHeart( x, y );
    else if( suit == SOLI_SUIT_DIAMOND ) soliDrawDiamond( x, y );
}

void soliDrawSegmentA( int x, int y ) { gbDrawFastHLine( x, y, 3 ); }
void soliDrawSegmentB( int x, int y ) { gbDrawFastVLine( x + 2, y, 3 ); }
void soliDrawSegmentC( int x, int y ) { gbDrawFastVLine( x + 2, y + 2, 3 ); }
void soliDrawSegmentD( int x, int y ) { gbDrawFastHLine( x, y + 4, 3 ); }
void soliDrawSegmentE( int x, int y ) { gbDrawFastVLine( x, y + 2, 3 ); }
void soliDrawSegmentF( int x, int y ) { gbDrawFastVLine( x, y, 3 ); }
void soliDrawSegmentG( int x, int y ) { gbDrawFastHLine( x, y + 2, 3 ); }

void soliDrawAce( int x, int y )
{
    gbDrawPixel( x + 1, y );
    gbDrawFastVLine( x, y + 1, 4 );
    gbDrawFastVLine( x + 2, y + 1, 4 );
    gbDrawPixel( x + 1, y + 2 );
}

void soliDrawTwo( int x, int y )
{
    soliDrawSegmentA( x, y ); soliDrawSegmentB( x, y ); soliDrawSegmentG( x, y );
    soliDrawSegmentE( x, y ); soliDrawSegmentD( x, y );
}

void soliDrawThree( int x, int y )
{
    soliDrawSegmentA( x, y ); soliDrawSegmentB( x, y ); soliDrawSegmentG( x, y );
    soliDrawSegmentC( x, y ); soliDrawSegmentD( x, y );
}

void soliDrawFour( int x, int y )
{
    soliDrawSegmentF( x, y ); soliDrawSegmentG( x, y ); soliDrawSegmentB( x, y ); soliDrawSegmentC( x, y );
}

void soliDrawFive( int x, int y )
{
    soliDrawSegmentA( x, y ); soliDrawSegmentF( x, y ); soliDrawSegmentG( x, y );
    soliDrawSegmentC( x, y ); soliDrawSegmentD( x, y );
}

void soliDrawSix( int x, int y ) { soliDrawFive( x, y ); soliDrawSegmentE( x, y ); }

void soliDrawSeven( int x, int y )
{
    soliDrawSegmentA( x, y ); soliDrawSegmentB( x, y ); soliDrawSegmentC( x, y );
}

void soliDrawEight( int x, int y ) { soliDrawSix( x, y ); soliDrawSegmentB( x, y ); }
void soliDrawNine( int x, int y ) { soliDrawFour( x, y ); soliDrawSegmentA( x, y ); }

void soliDrawTen( int x, int y )
{
    soliDrawSeven( x, y ); soliDrawSegmentD( x, y ); soliDrawSegmentE( x, y ); soliDrawSegmentF( x, y );
    gbDrawFastVLine( x - 2, y, 5 );
}

void soliDrawJack( int x, int y )
{
    soliDrawSegmentB( x, y ); soliDrawSegmentC( x, y ); soliDrawSegmentD( x, y );
    gbDrawPixel( x, y + 3 );
}

void soliDrawQueen( int x, int y )
{
    soliDrawSegmentA( x, y ); soliDrawSegmentB( x, y ); soliDrawSegmentF( x, y );
    gbDrawFastHLine( x, y + 3, 3 );
    gbDrawPixel( x + 1, y + 4 );
}

void soliDrawKing( int x, int y )
{
    soliDrawSegmentF( x, y ); soliDrawSegmentE( x, y );
    gbDrawPixel( x + 1, y + 2 );
    gbDrawFastVLine( x + 2, y, 2 );
    gbDrawFastVLine( x + 2, y + 3, 2 );
}

void soliDrawValue( int x, int y, int value )
{
    if( value == SOLI_ACE ) soliDrawAce( x, y );
    else if( value == SOLI_TWO ) soliDrawTwo( x, y );
    else if( value == SOLI_THREE ) soliDrawThree( x, y );
    else if( value == SOLI_FOUR ) soliDrawFour( x, y );
    else if( value == SOLI_FIVE ) soliDrawFive( x, y );
    else if( value == SOLI_SIX ) soliDrawSix( x, y );
    else if( value == SOLI_SEVEN ) soliDrawSeven( x, y );
    else if( value == SOLI_EIGHT ) soliDrawEight( x, y );
    else if( value == SOLI_NINE ) soliDrawNine( x, y );
    else if( value == SOLI_TEN ) soliDrawTen( x, y );
    else if( value == SOLI_JACK ) soliDrawJack( x, y );
    else if( value == SOLI_QUEEN ) soliDrawQueen( x, y );
    else if( value == SOLI_KING ) soliDrawKing( x, y );
}

// Direct port of real upstream `drawCard()`.
void soliDrawCard( int x, int y, int card )
{
    int fill = GB_WHITE;
    if( soliCardIsFaceDown( card ) ) fill = GB_GRAY;
    gbSetColor( fill );
    gbFillRect( x + 1, y + 1, 8, 12 );

    gbSetColor( GB_BLACK );
    gbDrawFastHLine( x + 1, y, 8 );
    gbDrawFastHLine( x + 1, y + 13, 8 );
    gbDrawFastVLine( x, y + 1, 12 );
    gbDrawFastVLine( x + 9, y + 1, 12 );

    if( soliCardIsFaceDown( card ) ) return;

    // No "else setColor(BLACK)" here - matches real upstream exactly:
    // black suits/values simply inherit whatever BLACK was already set
    // just above for the border, the same real reliance upstream itself
    // has on setColor() carrying over between calls.
    if( soliCardIsRed( card ) ) gbSetColor( GB_GRAY );
    soliDrawSuit( x + 2, y + 2, soliCardSuit( card ) );
    soliDrawValue( x + 5, y + 7, soliCardValue( card ) );
}

// Direct port of real upstream `drawPile()`.
void soliDrawPile( SoliPile* pile )
{
    int baseIndex = gbMax( 0, soliPileGetCardCount( pile ) - SOLI_MAX_CARDS_DRAWN_IN_PILE );
    int limit = gbMin( soliPileGetCardCount( pile ), SOLI_MAX_CARDS_DRAWN_IN_PILE );
    int i;
    for( i = 0; i < limit; i++ )
    {
        soliDrawCard( pile->x, pile->y + 2 * i, soliPileGetCard( pile, soliPileGetCardCount( pile ) - i - 1 - baseIndex ) );
    }
}

// Direct port of real upstream `getCursorDestination()` - real `byte&`/
// `bool&` reference-out parameters became real `int*`/`bool*` pointers.
void soliGetCursorDestination( int* x, int* y, bool* flipped )
{
    SoliPile* pile = soliGetPileByLocation( soliActiveLocation );
    int offset;

    if( soliActiveLocation == SOLI_LOC_STOCK )
    {
        *x = pile->x + 10;
        *y = pile->y + 4;
        *flipped = false;
    }
    else if( soliActiveLocation == SOLI_LOC_TALON )
    {
        *x = pile->x + 10 + 2 * gbMin( 2, gbMax( 0, soliPileGetCardCount( pile ) - 1 ) );
        *y = pile->y + 4;
        *flipped = false;
    }
    else if( soliActiveLocation >= SOLI_LOC_FOUNDATION1 && soliActiveLocation <= SOLI_LOC_FOUNDATION4 )
    {
        *x = pile->x - 7;
        *y = pile->y + 4;
        *flipped = true;
    }
    else if( soliActiveLocation >= SOLI_LOC_TABLEAU1 && soliActiveLocation <= SOLI_LOC_TABLEAU3 )
    {
        *x = pile->x + 10;
        if( soliCardIndex == 0 ) offset = 4; else offset = -2;
        *y = offset + soliCardYPosition( pile, soliCardIndex );
        *flipped = false;
    }
    else
    {
        *x = pile->x - 7;
        if( soliCardIndex == 0 ) offset = 4; else offset = -2;
        *y = offset + soliCardYPosition( pile, soliCardIndex );
        *flipped = true;
    }
}

// Direct port of real upstream's own 3-argument `drawCursor(x,y,flipped)`
// (renamed - this dialect has no function overloading, so it cannot share
// a name with the real 0-argument `drawCursor()` below).
void soliDrawCursorAt( int x, int y, bool flipped )
{
    int i;
    if( flipped )
    {
        for( i = 0; i < 4; i++ )
        {
            gbSetColor( GB_BLACK );
            gbDrawPixel( x + 3 + i, y + i );
            gbDrawPixel( x + 3 + i, y + ( 6 - i ) );
            gbSetColor( GB_WHITE );
            gbDrawFastHLine( x + 3, y + i, i );
            gbDrawFastHLine( x + 3, y + ( 6 - i ), i );
        }
        gbSetColor( GB_BLACK );
        gbDrawFastVLine( x + 2, y, 7 );
        gbDrawFastHLine( x, y + 2, 2 );
        gbDrawFastHLine( x, y + 4, 2 );
        gbDrawPixel( x, y + 3 );
        gbSetColor( GB_WHITE );
        gbDrawFastHLine( x + 1, y + 3, 2 );
        if( soliCardIndex != 0 )
        {
            int card = soliPileGetCard( soliGetPileByLocation( soliActiveLocation ), soliCardIndex );
            int extraWidth;
            if( soliCardValue( card ) == SOLI_TEN ) extraWidth = 2; else extraWidth = 0;
            gbSetColor( GB_BLACK );
            gbDrawRect( x - 12 - extraWidth, y - 1, 13 + extraWidth, 9 );
            gbSetColor( GB_WHITE );
            gbDrawPixel( x, y + 3 );
            gbFillRect( x - 11 - extraWidth, y, 11 + extraWidth, 7 );
            if( soliCardIsRed( card ) ) gbSetColor( GB_GRAY ); else gbSetColor( GB_BLACK );
            soliDrawValue( x - 10, y + 1, soliCardValue( card ) );
            soliDrawSuit( x - 6, y + 1, soliCardSuit( card ) );
        }
    }
    else
    {
        for( i = 0; i < 4; i++ )
        {
            gbSetColor( GB_BLACK );
            gbDrawPixel( x + 3 - i, y + i );
            gbDrawPixel( x + 3 - i, y + ( 6 - i ) );
            gbSetColor( GB_WHITE );
            gbDrawFastHLine( x + 4 - i, y + i, i );
            gbDrawFastHLine( x + 4 - i, y + ( 6 - i ), i );
        }
        gbSetColor( GB_BLACK );
        gbDrawFastVLine( x + 4, y, 7 );
        gbDrawFastHLine( x + 5, y + 2, 2 );
        gbDrawFastHLine( x + 5, y + 4, 2 );
        gbDrawPixel( x + 6, y + 3 );
        gbSetColor( GB_WHITE );
        gbDrawFastHLine( x + 4, y + 3, 2 );
        if( soliCardIndex != 0 )
        {
            int card = soliPileGetCard( soliGetPileByLocation( soliActiveLocation ), soliCardIndex );
            int extraWidth;
            if( soliCardValue( card ) == SOLI_TEN ) extraWidth = 2; else extraWidth = 0;
            gbSetColor( GB_BLACK );
            gbDrawRect( x + 6, y - 1, 13 + extraWidth, 9 );
            gbSetColor( GB_WHITE );
            gbDrawPixel( x + 6, y + 3 );
            gbFillRect( x + 7, y, 11 + extraWidth, 7 );
            if( soliCardIsRed( card ) ) gbSetColor( GB_GRAY ); else gbSetColor( GB_BLACK );
            soliDrawValue( x + 8 + extraWidth, y + 1, soliCardValue( card ) );
            soliDrawSuit( x + 12 + extraWidth, y + 1, soliCardSuit( card ) );
        }
    }
}

// Direct port of real upstream's own 0-argument `drawCursor()`.
void soliDrawCursor()
{
    bool flipped;
    int x;
    int y;
    soliGetCursorDestination( &x, &y, &flipped );

    soliCursorX = soliUpdatePosition( soliCursorX, x );
    soliCursorY = soliUpdatePosition( soliCursorY, y );

    soliDrawCursorAt( soliCursorX, soliCursorY, flipped );
}

// Direct port of real upstream `drawBoard()`.
void soliDrawBoard()
{
    if( soliPileGetCardCount( &soliStockDeck ) != 0 )
    {
        soliDrawCard( soliStockDeck.x, soliStockDeck.y, soliMakeCard( SOLI_ACE, SOLI_SUIT_SPADE, true ) );
    }

    int i;
    int talonVisible = gbMin( 3, soliPileGetCardCount( &soliTalonDeck ) );
    for( i = 0; i < talonVisible; i++ )
    {
        soliDrawCard( soliTalonDeck.x + i * 2, soliTalonDeck.y, soliPileGetCard( &soliTalonDeck, talonVisible - i - 1 ) );
    }

    for( i = 0; i < 4; i++ )
    {
        if( soliPileGetCardCount( &soliFoundations[ i ] ) != 0 )
        {
            soliDrawCard( soliFoundations[ i ].x, soliFoundations[ i ].y, soliPileGetCard( &soliFoundations[ i ], 0 ) );
        }
        else
        {
            gbSetColor( GB_GRAY );
            gbDrawRect( soliFoundations[ i ].x, soliFoundations[ i ].y, 10, 14 );
        }
    }

    for( i = 0; i < 7; i++ ) soliDrawPile( &soliTableau[ i ] );
}

// -----------------------------------------------------------------------
// EEPROM (see this file's own header comment for the real address layout)
// -----------------------------------------------------------------------

int soliSavePile( int address, SoliPile* pile )
{
    int count = soliPileGetCardCount( pile );
    int maxCards = soliPileGetMaxCards( pile );
    eeprom_write_byte( address, count );
    int i;
    for( i = 0; i < maxCards; i++ )
    {
        if( count > i ) eeprom_write_byte( address + i + 1, soliPileGetCard( pile, count - i - 1 ) );
    }
    return 1 + maxCards;
}

int soliLoadPile( int address, SoliPile* pile )
{
    soliPileEmpty( pile );
    int count = eeprom_read_byte( address );
    int i;
    for( i = 0; i < count; i++ ) soliPileAddCard( pile, eeprom_read_byte( address + i + 1 ) );
    return 1 + soliPileGetMaxCards( pile );
}

void soliReadEeprom()
{
    if( eeprom_read_byte( 0 ) != SOLI_EEPROM_MAGIC_NUMBER )
    {
        soliContinueGame = false;
        return;
    }

    soliEasyGameCount = eeprom_read_word( 1 );
    soliEasyGamesWon = eeprom_read_word( 3 );
    soliHardGameCount = eeprom_read_word( 5 );
    soliHardGamesWon = eeprom_read_word( 7 );

    if( eeprom_read_byte( 9 ) )
    {
        soliContinueGame = true;
        soliCardsToDraw = eeprom_read_byte( 10 );
        int address = 11;
        address = address + soliLoadPile( address, &soliStockDeck );
        address = address + soliLoadPile( address, &soliTalonDeck );
        int i;
        for( i = 0; i < 4; i++ ) address = address + soliLoadPile( address, &soliFoundations[ i ] );
        for( i = 0; i < 7; i++ ) address = address + soliLoadPile( address, &soliTableau[ i ] );
    }
    else
    {
        soliContinueGame = false;
    }
}

void soliWriteEeprom( bool saveGame )
{
    eeprom_update_byte( 0, SOLI_EEPROM_MAGIC_NUMBER );
    eeprom_write_word( 1, soliEasyGameCount );
    eeprom_write_word( 3, soliEasyGamesWon );
    eeprom_write_word( 5, soliHardGameCount );
    eeprom_write_word( 7, soliHardGamesWon );

    int savedFlag = 0;
    if( saveGame ) savedFlag = 1;
    eeprom_update_byte( 9, savedFlag );

    if( saveGame )
    {
        eeprom_write_byte( 10, soliCardsToDraw );
        int address = 11;
        address = address + soliSavePile( address, &soliStockDeck );
        address = address + soliSavePile( address, &soliTalonDeck );
        int i;
        for( i = 0; i < 4; i++ ) address = address + soliSavePile( address, &soliFoundations[ i ] );
        for( i = 0; i < 7; i++ ) address = address + soliSavePile( address, &soliTableau[ i ] );
    }
}

// -----------------------------------------------------------------------
// Core gameplay - direct ports of real upstream functions
// -----------------------------------------------------------------------

bool soliRevealCards()
{
    bool revealed = false;
    int i;
    for( i = 0; i < 7; i++ )
    {
        if( soliPileGetCardCount( &soliTableau[ i ] ) == 0 ) continue;
        int card = soliPileRemoveTopCard( &soliTableau[ i ] );
        if( soliCardIsFaceDown( card ) )
        {
            card = soliCardFlip( card );
            revealed = true;
        }
        soliPileAddCard( &soliTableau[ i ], card );
    }
    return revealed;
}

void soliCheckWonGame()
{
    if( soliPileGetCardCount( &soliFoundations[ 0 ] ) == 13 && soliPileGetCardCount( &soliFoundations[ 1 ] ) == 13 &&
        soliPileGetCardCount( &soliFoundations[ 2 ] ) == 13 && soliPileGetCardCount( &soliFoundations[ 3 ] ) == 13 )
    {
        soliMode = SOLI_MODE_WON_GAME;
        if( soliCardsToDraw == 1 )
        {
            soliEasyGamesWon = soliEasyGamesWon + 1;
            soliWriteEeprom( false );
        }
        else
        {
            soliHardGamesWon = soliHardGamesWon + 1;
            soliWriteEeprom( false );
        }
    }
}

bool soliUpdateAfterPlay()
{
    bool result = soliRevealCards();
    soliCheckWonGame();
    soliCardIndex = 0;
    bool unused;
    soliGetCursorDestination( &soliCursorX, &soliCursorY, &unused );
    return result;
}

void soliSetupNewGame()
{
    soliUndoIndex = 0;
    soliUndoCount = 0;
    soliActiveLocation = SOLI_LOC_STOCK;
    soliCardIndex = 0;
    soliCursorX = 11;
    soliCursorY = 4;

    soliPileEmpty( &soliTalonDeck );
    soliPileNewDeck( &soliStockDeck );
    soliPileShuffle( &soliStockDeck );
    int i;
    for( i = 0; i < 4; i++ ) soliPileEmpty( &soliFoundations[ i ] );
    for( i = 0; i < 7; i++ ) soliPileEmpty( &soliTableau[ i ] );

    soliCardAnimationCount = 0;
    int j;
    for( i = 0; i < 7; i++ )
    {
        for( j = i; j < 7; j++ )
        {
            int card = soliPileRemoveTopCard( &soliStockDeck );
            if( i == j ) card = soliCardFlip( card );
            soliCardAnimations[ soliCardAnimationCount ].x = 1;
            soliCardAnimations[ soliCardAnimationCount ].y = 0;
            soliCardAnimations[ soliCardAnimationCount ].destX = soliTableau[ j ].x;
            soliCardAnimations[ soliCardAnimationCount ].destY = soliTableau[ j ].y + 2 * i;
            soliCardAnimations[ soliCardAnimationCount ].tableauIndex = j;
            soliCardAnimations[ soliCardAnimationCount ].card = card;
            soliCardAnimationCount = soliCardAnimationCount + 1;
        }
    }
    soliCardAnimationCount = 0;

    soliMode = SOLI_MODE_DEALING;
}

void soliMoveCards()
{
    SoliPile* pile = soliGetPileByLocation( soliActiveLocation );
    SoliUndoAction action;
    action.special = 0;
    action.sourceLoc = soliSourcePileLocation;
    action.destLoc = soliActiveLocation;
    soliUndoActionSetCardCount( &action, soliPileGetCardCount( &soliMoving ) );
    soliPileAddPile( pile, &soliMoving );
    soliMode = SOLI_MODE_SELECTING;
    if( soliUpdateAfterPlay() ) soliUndoActionSetRevealed( &action );
    soliUndoPush( &action );
}

void soliHandleSelectingButtons()
{
    int originalLocation = soliActiveLocation;

    if( gbPressed( BTN_RIGHT ) )
    {
        if( soliActiveLocation != SOLI_LOC_FOUNDATION4 && soliActiveLocation != SOLI_LOC_TABLEAU7 )
          soliActiveLocation = soliActiveLocation + 1;
    }
    if( gbPressed( BTN_LEFT ) )
    {
        if( soliActiveLocation != SOLI_LOC_STOCK && soliActiveLocation != SOLI_LOC_TABLEAU1 )
          soliActiveLocation = soliActiveLocation - 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        if( soliCardIndex > 0 )
        {
            soliCardIndex = soliCardIndex - 1;
        }
        else
        {
            if( soliActiveLocation < SOLI_LOC_FOUNDATION1 ) soliActiveLocation = soliActiveLocation + 6;
            else if( soliActiveLocation <= SOLI_LOC_FOUNDATION4 ) soliActiveLocation = soliActiveLocation + 7;
        }
    }
    if( gbPressed( BTN_UP ) )
    {
        bool interPileNavigation = false;
        if( soliActiveLocation >= SOLI_LOC_TABLEAU1 && soliActiveLocation <= SOLI_LOC_TABLEAU7 )
        {
            SoliPile* pile = soliGetPileByLocation( soliActiveLocation );
            if( soliPileGetCardCount( pile ) > soliCardIndex + 1 && !soliCardIsFaceDown( soliPileGetCard( pile, soliCardIndex + 1 ) ) )
            {
                soliCardIndex = soliCardIndex + 1;
                interPileNavigation = true;
            }
        }
        if( !interPileNavigation )
        {
            if( soliActiveLocation > SOLI_LOC_TABLEAU2 ) soliActiveLocation = soliActiveLocation - 7;
            else if( soliActiveLocation >= SOLI_LOC_TABLEAU1 ) soliActiveLocation = soliActiveLocation - 6;
        }
    }
    if( gbPressed( BTN_B ) )
    {
        if( soliActiveLocation >= SOLI_LOC_TABLEAU1 || soliActiveLocation == SOLI_LOC_TALON )
        {
            SoliPile* pile = soliGetPileByLocation( soliActiveLocation );
            if( soliPileGetCardCount( pile ) > 0 )
            {
                int card = soliPileGetCard( pile, 0 );
                bool foundMatch = false;
                int i;
                for( i = 0; i < 4; i++ )
                {
                    if( soliPileGetCardCount( &soliFoundations[ i ] ) == 0 )
                    {
                        if( soliCardValue( card ) == SOLI_ACE ) foundMatch = true;
                    }
                    else
                    {
                        int card1 = soliPileGetCard( &soliFoundations[ i ], 0 );
                        int card2 = soliPileGetCard( pile, 0 );
                        if( soliCardSuit( card1 ) == soliCardSuit( card2 ) && soliCardValue( card1 ) + 1 == soliCardValue( card2 ) )
                          foundMatch = true;
                    }
                    if( foundMatch )
                    {
                        soliPileEmpty( &soliMoving );
                        soliMoving.x = pile->x;
                        soliMoving.y = soliCardYPosition( pile, 0 );
                        soliPileAddCard( &soliMoving, soliPileRemoveTopCard( pile ) );
                        soliSourcePile = &soliFoundations[ i ];
                        soliSourcePileLocation = SOLI_LOC_FOUNDATION1 + i;
                        soliMode = SOLI_MODE_FAST_FOUNDATION;
                        soliPlaySoundA();
                        break;
                    }
                }
            }
        }
    }
    else if( gbPressed( BTN_A ) )
    {
        if( soliActiveLocation == SOLI_LOC_STOCK )
        {
            if( soliPileGetCardCount( &soliStockDeck ) != 0 )
            {
                soliPileEmpty( &soliMoving );
                soliDrawAndFlip( &soliStockDeck, &soliMoving );
                soliMoving.x = soliStockDeck.x;
                soliMoving.y = soliStockDeck.y;
                soliRemainingDraws = gbMin( soliCardsToDraw - 1, soliPileGetCardCount( &soliStockDeck ) );
                soliMode = SOLI_MODE_DRAWING_CARDS;
                soliPlaySoundA();
            }
            else
            {
                while( soliPileGetCardCount( &soliTalonDeck ) != 0 ) soliDrawAndFlip( &soliTalonDeck, &soliStockDeck );
                SoliUndoAction action;
                action.special = 0;
                soliUndoActionSetFlippedTalon( &action );
                action.sourceLoc = SOLI_LOC_STOCK;
                action.destLoc = SOLI_LOC_STOCK;
                soliUndoPush( &action );
            }
        }
        else
        {
            soliSourcePileLocation = soliActiveLocation;
            soliSourcePile = soliGetPileByLocation( soliActiveLocation );
            if( soliPileGetCardCount( soliSourcePile ) != 0 )
            {
                soliPileEmpty( &soliMoving );
                soliMoving.x = soliSourcePile->x;
                soliMoving.y = soliCardYPosition( soliSourcePile, 0 );
                soliPileRemoveCards( soliSourcePile, soliCardIndex + 1, &soliMoving );
                soliMode = SOLI_MODE_MOVING_PILE;
                soliPlaySoundA();
            }
        }
    }
    if( originalLocation != soliActiveLocation ) soliCardIndex = 0;
}

void soliHandleMovingPileButtons()
{
    if( gbPressed( BTN_RIGHT ) )
    {
        if( soliActiveLocation != SOLI_LOC_FOUNDATION4 && soliActiveLocation != SOLI_LOC_TABLEAU7 )
          soliActiveLocation = soliActiveLocation + 1;
    }
    if( gbPressed( BTN_LEFT ) )
    {
        if( soliActiveLocation != SOLI_LOC_TALON && soliActiveLocation != SOLI_LOC_TABLEAU1 )
          soliActiveLocation = soliActiveLocation - 1;
    }
    if( gbPressed( BTN_DOWN ) )
    {
        if( soliActiveLocation == SOLI_LOC_TALON ) soliActiveLocation = SOLI_LOC_TABLEAU2;
        else if( soliActiveLocation <= SOLI_LOC_FOUNDATION4 ) soliActiveLocation = soliActiveLocation + 7;
    }
    if( gbPressed( BTN_UP ) )
    {
        if( soliActiveLocation >= SOLI_LOC_TABLEAU4 ) soliActiveLocation = soliActiveLocation - 7;
        else if( soliActiveLocation >= SOLI_LOC_TABLEAU1 ) soliActiveLocation = SOLI_LOC_TALON;
    }
    if( gbPressed( BTN_A ) )
    {
        soliPlaySoundB();
        if( soliActiveLocation == SOLI_LOC_TALON )
        {
            soliMode = SOLI_MODE_ILLEGAL_MOVE;
        }
        else if( soliActiveLocation >= SOLI_LOC_FOUNDATION1 && soliActiveLocation <= SOLI_LOC_FOUNDATION4 )
        {
            bool illegal = false;
            if( soliPileGetCardCount( &soliMoving ) != 1 )
            {
                illegal = true;
            }
            else
            {
                SoliPile* destinationFoundation = soliGetPileByLocation( soliActiveLocation );
                if( soliPileGetCardCount( destinationFoundation ) == 0 )
                {
                    if( soliCardValue( soliPileGetCard( &soliMoving, 0 ) ) != SOLI_ACE ) illegal = true;
                }
                else
                {
                    int card1 = soliPileGetCard( destinationFoundation, 0 );
                    int card2 = soliPileGetCard( &soliMoving, 0 );
                    if( soliCardSuit( card1 ) != soliCardSuit( card2 ) || soliCardValue( card1 ) + 1 != soliCardValue( card2 ) )
                      illegal = true;
                }
            }
            if( illegal ) soliMode = SOLI_MODE_ILLEGAL_MOVE;
            else
            {
                soliMoveCards();
                soliCheckWonGame();
            }
        }
        else if( soliActiveLocation >= SOLI_LOC_TABLEAU1 && soliActiveLocation <= SOLI_LOC_TABLEAU7 )
        {
            bool illegal = false;
            SoliPile* destinationTableau = soliGetPileByLocation( soliActiveLocation );
            if( soliPileGetCardCount( destinationTableau ) > 0 )
            {
                int card1 = soliPileGetCard( destinationTableau, 0 );
                int card2 = soliPileGetCard( &soliMoving, soliPileGetCardCount( &soliMoving ) - 1 );
                if( soliCardIsRed( card1 ) == soliCardIsRed( card2 ) || soliCardValue( card1 ) != soliCardValue( card2 ) + 1 )
                  illegal = true;
            }
            else
            {
                int card = soliPileGetCard( &soliMoving, soliPileGetCardCount( &soliMoving ) - 1 );
                if( soliCardValue( card ) != SOLI_KING ) illegal = true;
            }
            if( illegal ) soliMode = SOLI_MODE_ILLEGAL_MOVE;
            else soliMoveCards();
        }
    }
}

void soliDrawDealing()
{
    if( soliCardAnimationCount < 28 && gbFrameCount % 4 == 0 )
    {
        soliCardAnimationCount = soliCardAnimationCount + 1;
        soliPlaySoundA();
    }
    bool doneDealing = ( soliCardAnimationCount == 28 );
    int i;
    for( i = 0; i < soliCardAnimationCount; i++ )
    {
        if( soliCardAnimations[ i ].x != soliCardAnimations[ i ].destX || soliCardAnimations[ i ].y != soliCardAnimations[ i ].destY )
        {
            doneDealing = false;
            soliDrawCard( soliCardAnimations[ i ].x, soliCardAnimations[ i ].y, soliCardAnimations[ i ].card );
            soliCardAnimations[ i ].x = soliUpdatePosition( soliCardAnimations[ i ].x, soliCardAnimations[ i ].destX );
            soliCardAnimations[ i ].y = soliUpdatePosition( soliCardAnimations[ i ].y, soliCardAnimations[ i ].destY );
            if( soliCardAnimations[ i ].x == soliCardAnimations[ i ].destX && soliCardAnimations[ i ].y == soliCardAnimations[ i ].destY )
              soliPileAddCard( &soliTableau[ soliCardAnimations[ i ].tableauIndex ], soliCardAnimations[ i ].card );
        }
    }
    if( doneDealing ) soliMode = SOLI_MODE_SELECTING;
}

void soliDrawDrawingCards()
{
    soliDrawPile( &soliMoving );
    soliMoving.x = soliUpdatePosition( soliMoving.x, 17 );
    soliMoving.y = soliUpdatePosition( soliMoving.y, 0 );
    if( soliMoving.x == 17 && soliMoving.y == 0 )
    {
        soliPileAddCard( &soliTalonDeck, soliPileGetCard( &soliMoving, 0 ) );
        if( soliRemainingDraws )
        {
            soliRemainingDraws = soliRemainingDraws - 1;
            soliPileEmpty( &soliMoving );
            soliDrawAndFlip( &soliStockDeck, &soliMoving );
            soliMoving.x = soliStockDeck.x;
            soliMoving.y = soliStockDeck.y;
            soliPlaySoundA();
        }
        else
        {
            SoliUndoAction action;
            action.special = 0;
            soliUndoActionSetDraw( &action );
            action.sourceLoc = SOLI_LOC_STOCK;
            action.destLoc = SOLI_LOC_STOCK;
            soliUndoPush( &action );
            soliMode = SOLI_MODE_SELECTING;
        }
    }
}

void soliDrawMovingPile()
{
    soliDrawPile( &soliMoving );
    SoliPile* pile = soliGetPileByLocation( soliActiveLocation );
    int yDelta = 2;
    if( pile->isTableau ) yDelta = yDelta + 2 * soliPileGetCardCount( pile );
    soliMoving.x = soliUpdatePosition( soliMoving.x, pile->x );
    soliMoving.y = soliUpdatePosition( soliMoving.y, pile->y + yDelta );
}

void soliDrawIllegalMove()
{
    int yDelta = 0;
    if( soliSourcePile->isTableau ) yDelta = yDelta + 2 * soliPileGetCardCount( soliSourcePile );
    soliMoving.x = soliUpdatePosition( soliMoving.x, soliSourcePile->x );
    soliMoving.y = soliUpdatePosition( soliMoving.y, soliSourcePile->y + yDelta );
    soliDrawPile( &soliMoving );

    if( soliMoving.x == soliSourcePile->x && soliMoving.y == soliSourcePile->y + yDelta )
    {
        soliPileAddPile( soliSourcePile, &soliMoving );
        bool revealed = soliUpdateAfterPlay();

        if( soliMode == SOLI_MODE_FAST_FOUNDATION )
        {
            SoliUndoAction action;
            action.special = 0;
            action.sourceLoc = soliActiveLocation;
            action.destLoc = soliSourcePileLocation;
            soliUndoActionSetCardCount( &action, 1 );
            if( revealed ) soliUndoActionSetRevealed( &action );
            soliUndoPush( &action );
        }
        if( soliMode != SOLI_MODE_WON_GAME ) soliMode = SOLI_MODE_SELECTING;
    }
}

bool soliInitializeCardBounce()
{
    if( soliPileGetCardCount( &soliFoundations[ soliBounceIndex ] ) == 0 ) return false;
    soliBounce.card = soliPileRemoveTopCard( &soliFoundations[ soliBounceIndex ] );
    soliBounce.x = soliFoundations[ soliBounceIndex ].x << 8;
    soliBounce.y = soliFoundations[ soliBounceIndex ].y << 8;
    int sign;
    if( arand( 2 ) ) sign = 1; else sign = -1;
    soliBounce.xVelocity = sign * ( 0x0100 + arand( 0x0100 ) );
    soliBounce.yVelocity = -( arand( 0x0200 ) );
    soliBounceIndex = ( soliBounceIndex + 1 ) % 4;
    return true;
}

// See this file's own header comment on the two real, traced-through
// bugs/adaptations this function embodies (the real "persistence stuck
// true" bug that keeps the celebratory bounce sequence from ever really
// starting, and the Vircon32-specific logical-right-shift fix).
void soliDrawWonGame()
{
    soliDrawBoard();

    if( !soliPersistence )
    {
        soliPersistence = true;
        soliInitializeCardBounce();
    }

    soliBounce.yVelocity = soliBounce.yVelocity + 0x0080;
    soliBounce.x = soliBounce.x + soliBounce.xVelocity;
    soliBounce.y = soliBounce.y + soliBounce.yVelocity;
    if( soliBounce.y + ( 14 << 8 ) > ( LCDHEIGHT << 8 ) )
    {
        soliBounce.y = ( LCDHEIGHT - 14 ) << 8;
        soliBounce.yVelocity = soliBounce.yVelocity * -4 / 5;
        soliPlaySoundB();
    }
    soliDrawCard( soliBounce.x / 256, soliBounce.y / 256, soliBounce.card );
    if( soliBounce.x + ( 10 << 8 ) < 0 || soliBounce.x > ( LCDWIDTH << 8 ) )
    {
        if( !soliInitializeCardBounce() ) soliBeginTitle();
    }
}

void soliPerformUndo()
{
    if( !soliUndoIsEmpty() && soliMode == SOLI_MODE_SELECTING )
    {
        SoliUndoAction action;
        soliUndoPop( &action );

        if( soliUndoActionWasDraw( &action ) )
        {
            int i;
            for( i = 0; i < soliCardsToDraw; i++ ) soliDrawAndFlip( &soliTalonDeck, &soliStockDeck );
        }
        else if( soliUndoActionWasFlippedTalon( &action ) )
        {
            while( soliPileGetCardCount( &soliStockDeck ) != 0 ) soliDrawAndFlip( &soliStockDeck, &soliTalonDeck );
        }
        else
        {
            SoliPile* source = soliGetPileByLocation( action.sourceLoc );
            SoliPile* destination = soliGetPileByLocation( action.destLoc );
            if( soliUndoActionWasRevealed( &action ) ) soliDrawAndFlip( source, source );
            soliPileEmpty( &soliMoving );
            soliPileRemoveCards( destination, soliUndoActionGetCardCount( &action ), &soliMoving );
            soliPileAddPile( source, &soliMoving );
        }
        soliUpdateAfterPlay();
    }
}

// -----------------------------------------------------------------------
// App-level states (title / new-game menu / statistics / playing / pause)
// -----------------------------------------------------------------------

void soliUpdateTitle()
{
    // Explicit, rather than relying on gbBegin()'s own default draw color -
    // this state is reached both fresh from gameSolitaire_init() (where
    // the default happens to already be BLACK) and from mid-game (Button
    // C's "Quit" and "Save for later" pause-menu options), where the
    // color could be left at anything the game's own last draw call set -
    // see soliUpdatePauseMenu()'s own header comment for the real bug
    // this exact gap caused there.
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 10, 1, soliTitleBitmap );
    gbCursorX = 28;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        soliSetupNewGame();
        soliReadEeprom();
        if( soliContinueGame )
        {
            soliWriteEeprom( false );
            soliMode = SOLI_MODE_SELECTING;
            soliAppState = SOLI_APP_PLAYING;
        }
        else
        {
            soliMenuIndex = 0;
            soliAppState = SOLI_APP_NEWGAME_MENU;
        }
    }
}

// Hand-rolled replacement for real upstream's own blocking
// `gb.menu(newGameMenu, 3)` widget - see this file's own header comment.
void soliUpdateNewGameMenu()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "SOLITAIRE" );

    int i;
    for( i = 0; i < 3; i++ )
    {
        gbCursorY = 16 + i * 8;
        gbCursorX = 2;
        if( i == soliMenuIndex ) gbPrintString( ">" );
        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "NEW EASY GAME" );
        else if( i == 1 ) gbPrintString( "NEW HARD GAME" );
        else gbPrintString( "GAME STATISTICS" );
    }

    if( gbRepeat( BTN_UP, 5 ) ) soliMenuIndex = gbMax( 0, soliMenuIndex - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) ) soliMenuIndex = gbMin( 2, soliMenuIndex + 1 );

    if( gbPressed( BTN_A ) )
    {
        soliPlaySoundB();
        if( soliMenuIndex == 0 )
        {
            soliCardsToDraw = 1;
            soliEasyGameCount = soliEasyGameCount + 1;
            soliWriteEeprom( false );
            soliAppState = SOLI_APP_PLAYING;
        }
        else if( soliMenuIndex == 1 )
        {
            soliCardsToDraw = 3;
            soliHardGameCount = soliHardGameCount + 1;
            soliWriteEeprom( false );
            soliAppState = SOLI_APP_PLAYING;
        }
        else
        {
            soliStatsReturnState = SOLI_APP_NEWGAME_MENU;
            soliAppState = SOLI_APP_STATS;
        }
    }
}

// Direct port of real upstream `displayStatistics()`.
void soliUpdateStats()
{
    gbSetColor( GB_BLACK );
    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "EASY STARTED:" );
    gbPrintNumber( soliEasyGameCount );

    gbCursorX = 2;
    gbCursorY = 10;
    gbPrintString( "EASY WON:" );
    gbPrintNumber( soliEasyGamesWon );

    gbCursorX = 2;
    gbCursorY = 18;
    gbPrintString( "HARD STARTED:" );
    gbPrintNumber( soliHardGameCount );

    gbCursorX = 2;
    gbCursorY = 26;
    gbPrintString( "HARD WON:" );
    gbPrintNumber( soliHardGamesWon );

    gbCursorX = 2;
    gbCursorY = 38;
    gbPrintString( "PRESS A/B/C" );

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) ) soliAppState = soliStatsReturnState;
}

void soliBeginPauseMenu()
{
    soliPauseMenuIndex = 0;
    soliAppState = SOLI_APP_PAUSE_MENU;
}

// Hand-rolled replacement for real upstream's own blocking
// `gb.menu(pauseMenu, ...)` widget - see this file's own header comment
// on the real item-count gating this reproduces.
void soliUpdatePauseMenu()
{
    bool fullMenu = ( soliMode == SOLI_MODE_SELECTING );
    bool showUndo = fullMenu && !soliUndoIsEmpty();
    int itemCount;
    if( !fullMenu ) itemCount = 3;
    else if( showUndo ) itemCount = 5;
    else itemCount = 4;

    gbCursorX = 2;
    gbCursorY = 1;
    gbPrintString( "PAUSED" );

    int i;
    for( i = 0; i < itemCount; i++ )
    {
        gbCursorY = 10 + i * 7;
        gbCursorX = 2;
        if( i == soliPauseMenuIndex ) gbPrintString( ">" );
        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "RESUME GAME" );
        else if( i == 1 ) gbPrintString( "QUIT GAME" );
        else if( i == 2 ) gbPrintString( "GAME STATISTICS" );
        else if( i == 3 ) gbPrintString( "SAVE FOR LATER" );
        else gbPrintString( "UNDO LAST MOVE" );
    }

    if( gbRepeat( BTN_UP, 5 ) ) soliPauseMenuIndex = gbMax( 0, soliPauseMenuIndex - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) ) soliPauseMenuIndex = gbMin( itemCount - 1, soliPauseMenuIndex + 1 );

    if( gbPressed( BTN_A ) )
    {
        soliPlaySoundB();
        if( soliPauseMenuIndex == 0 )
        {
            soliAppState = SOLI_APP_PLAYING;
        }
        else if( soliPauseMenuIndex == 1 )
        {
            soliBeginTitle();
        }
        else if( soliPauseMenuIndex == 2 )
        {
            soliStatsReturnState = SOLI_APP_PAUSE_MENU;
            soliAppState = SOLI_APP_STATS;
        }
        else if( soliPauseMenuIndex == 3 && fullMenu )
        {
            soliWriteEeprom( true );
            soliBeginTitle();
        }
        else if( soliPauseMenuIndex == 4 && showUndo )
        {
            soliPerformUndo();
            soliAppState = SOLI_APP_PLAYING;
        }
    }
}

// Direct port of real upstream `loop()`'s own `if (gb.update()) { ... }`
// body (minus the outer gbUpdate() throttle itself, handled by
// gameSolitaire_update()).
void soliUpdatePlaying()
{
    if( gbPressed( BTN_C ) )
    {
        soliBeginPauseMenu();
        return;
    }

    if( soliMode == SOLI_MODE_SELECTING ) soliHandleSelectingButtons();
    else if( soliMode == SOLI_MODE_MOVING_PILE ) soliHandleMovingPileButtons();

    if( soliMode != SOLI_MODE_WON_GAME ) soliDrawBoard();

    if( soliMode == SOLI_MODE_DEALING ) soliDrawDealing();
    else if( soliMode == SOLI_MODE_SELECTING ) soliDrawCursor();
    else if( soliMode == SOLI_MODE_DRAWING_CARDS ) soliDrawDrawingCards();
    else if( soliMode == SOLI_MODE_MOVING_PILE ) soliDrawMovingPile();
    else if( soliMode == SOLI_MODE_ILLEGAL_MOVE ) soliDrawIllegalMove();
    else if( soliMode == SOLI_MODE_FAST_FOUNDATION ) soliDrawIllegalMove();
    else if( soliMode == SOLI_MODE_WON_GAME ) soliDrawWonGame();
}

// -----------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------

void gameSolitaire_init()
{
    gbBegin();

    soliPileInit( &soliStockDeck, SOLI_LOC_STOCK, 52 );
    soliStockDeck.x = 1;
    soliStockDeck.y = 0;
    soliStockDeck.isTableau = false;

    soliPileInit( &soliTalonDeck, SOLI_LOC_TALON, 24 );
    soliTalonDeck.x = 13;
    soliTalonDeck.y = 0;
    soliTalonDeck.isTableau = false;

    int i;
    for( i = 0; i < 4; i++ )
    {
        soliPileInit( &soliFoundations[ i ], SOLI_LOC_FOUNDATION1 + i, 13 );
        soliFoundations[ i ].x = 37 + i * 12;
        soliFoundations[ i ].y = 0;
        soliFoundations[ i ].isTableau = false;
    }
    for( i = 0; i < 7; i++ )
    {
        soliPileInit( &soliTableau[ i ], SOLI_LOC_TABLEAU1 + i, 20 );
        soliTableau[ i ].x = i * 12 + 1;
        soliTableau[ i ].y = 16;
        soliTableau[ i ].isTableau = true;
    }
    soliPileInit( &soliMoving, SOLI_MOVING_PILE_ID, 13 );

    soliBounce.card = SOLI_DEFAULT_CARD;
    soliBounceIndex = 0;

    soliBeginTitle();
}

void gameSolitaire_update()
{
    if( !gbUpdate() ) return;

    if( soliAppState == SOLI_APP_TITLE ) soliUpdateTitle();
    else if( soliAppState == SOLI_APP_NEWGAME_MENU ) soliUpdateNewGameMenu();
    else if( soliAppState == SOLI_APP_STATS ) soliUpdateStats();
    else if( soliAppState == SOLI_APP_PLAYING ) soliUpdatePlaying();
    else if( soliAppState == SOLI_APP_PAUSE_MENU ) soliUpdatePauseMenu();

    gbRenderFrame();
}
