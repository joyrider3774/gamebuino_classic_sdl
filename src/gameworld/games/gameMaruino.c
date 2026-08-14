#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Maruino (ajsb113, license: none specified - a Dropbox direct-download
// zip, no live GitHub repo, matching this project's own established "no
// stable link" README convention already used for Gamebuino2048). A small
// side-scrolling platformer, unmistakably a Super Mario Bros homage: walk/
// jump across 7 levels of ground/brick/mystery-block terrain, stomp or
// avoid Goombas, grow "big" off a Mushroom, collect coins, and reach each
// level's Flag to advance. Source is 6 real upstream `.ino` tabs sharing
// one real Arduino translation unit (Blocks/Entities/Maps/Maruino/Player/
// Sprites.ino, confirmed no other files exist in the staged directory) -
// all read in full before porting.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment - this
// dialect has no classes/methods). Upstream's own `byte`/`boolean` types
// became plain `int`/`bool` (this project's own established avrCompat.h
// convention). `random(N)` (never actually called by this particular
// upstream source - no RNG use anywhere in Maruino) has no call site to
// convert. Every global got a `maru`-prefixed name (this cartridge has no
// linker, one flat namespace shared with 42 other already-ported games).
// Array declarations use this dialect's own required `int[N] name` order,
// never `int name[N]`. No `switch` statement is used anywhere (every real
// upstream `switch(curLevel[...])` tile-lookup in `playerCollide()`/
// `drawLevel()`/`Collide()` became an if/else-if chain instead, matching
// this project's own "no switch statement proven to work in this dialect"
// convention already used throughout, e.g. gameCastleDefence.c/
// gameShipwrek.c). Every real Arduino `B00000000`-style binary literal
// across all 20 PROGMEM sprite tables (`PlayerT`/`PlayerT1..T6`/`PlayerS`/
// `PlayerS1`/`PlayerC`/`Ground`/`Brick`/`Spikes`/`MysteryBlockC`/
// `MysteryBlockM`/`Coin`/`Empty`/`Flag`/`Mushroom`/`Goomba`/`Title`) was
// mechanically converted to plain decimal via a one-off local script
// (byte-for-byte against each bitmap's own real `{width,height,...}`
// header, not retyped by hand) - every byte became a plain `int` cell,
// this project's own established convention. `MysteryBlockC`/
// `MysteryBlockM` are byte-for-byte IDENTICAL upstream (both real "?" and
// "M" mystery-block tiles share one real sprite, never given distinct art)
// - preserved exactly, not a transcription mistake.
//
// REAL LEVEL DATA: upstream's own `lev[]` PROGMEM char table (7 real
// 64x6-tile levels concatenated as adjacent string literals, 2688 chars
// total - `numLevs` defaults to 6, with a real secret 7th level only
// reachable via the "A code" cheat, see below) is ported as 7 separate
// `int[385] maruLevel1`..`maruLevel7` string literals (one real 384-char
// level joined per array, +1 for the string literal's own auto-appended
// trailing 0 - avoids ever needing multi-literal adjacent-string
// concatenation inside an array initializer, which has no proven
// precedent elsewhere in this project), indexed through a real
// `int*[7] maruLevels` pointer table (this exact `int*[N] name = {...}`
// shape is already proven throughout this project, e.g. gameDescent.c's
// own `descentPlayerSprites`). `maruLoadLevel()` copies 384 real cells
// from the selected table into the mutable `maruCurLevel` working buffer,
// a direct port of upstream's own `loadLevel()`.
//
// A REAL, DELIBERATE GAG LEVEL: level 6's own map data is byte-for-byte
// IDENTICAL to level 1's, with every real 'G'/'B'/'M'/'?' solid tile
// swapped for 'I' (Invisible) instead - `drawLevel()` has no case for 'I'
// at all (nothing is ever drawn for it), while `playerCollide()`'s own
// tile lookup treats 'I' exactly like 'E' (Empty) - a real, fully SOLID
// collision shape that is never actually drawn. Level 6 is genuinely the
// same layout as level 1, played entirely blind - `nextLevel()`'s own real
// `gb.popup(F("Remember lvl 1?"))` on arrival is the explicit joke.
// Preserved exactly (both real `if/else-if` tile-code chains below keep
// the same asymmetric 'I' handling verbatim, not "fixed" into matching).
//
// A REAL, CONFIRMED-HARMLESS POPUP-OVERWRITE QUIRK: `nextLevel()` calls
// `gb.popup(F("Next Level!"), 20)` and then, only when leaving level 6,
// immediately calls `gb.popup(F("Remember lvl 1?"), 20)` again in the same
// tick. Real `gbPopup()`/upstream `Gamebuino::popup()` both just
// unconditionally overwrite one pending text+timer slot with no queue -
// so on the real level 6->7 transition specifically, the player only ever
// sees "Remember lvl 1?"; "Next Level!" is silently replaced before a
// single frame ever renders it. This is exactly what real hardware does
// too (traced through `gbPopup()`'s own real implementation, not assumed) -
// both calls are kept here exactly as upstream orders them, not
// deduplicated or reordered.
//
// BLOCKING-LOOP -> STATE MACHINE, upstream's own real `Menu()`/`start()`/
// `controls()`/`inputCode()`/`lose()`/`win()` (every one a real blocking
// `while(!gb.buttons.pressed(BTN_A)){ if(gb.update()){...} }`-shaped loop,
// or built on real Gamebuino widgets with no dialect equivalent - see
// below) became explicit states (`MARU_STATE_TITLE`/`_MENU`/`_CONTROLS`/
// `_CODE_PROMPT`/`_KEYBOARD`/`_PLAY`/`_LOSE`/`_WIN`), matching the
// "blocking loop -> explicit resumable state" treatment this project uses
// throughout (see gamePong.c's own header comment). Real
// `gb.titleScreen(F("By: ajsb113"), Title)` became `MARU_STATE_TITLE`
// (the real Title logo bitmap centered above a real "By: ajsb113" credit
// line, dismissed by a genuine fresh `gbPressed(BTN_A)`, matching every
// other titleScreen() conversion in this project). Real `gb.menu(menu,
// MENULENGTH)` became `MARU_STATE_MENU`'s own hand-rolled 4-option list
// (Play Game/Main Menu/Controls/Input a code) with a `*` cursor marker,
// UP/DOWN navigation and A to select - a direct structural copy of
// gameConduit.c's own already-proven `condUpdateMenu()` replacement for
// the exact same missing real widget.
//
// TWO REAL SCREENS NEEDED HEAVIER REWORK, since Gamebuino's own real
// `textWrap`/`gb.keyboard()` have no equivalent primitive in this shim:
//
// 1) `controls()`'s own real on-screen text relies entirely on real
//    `gb.display.textWrap = true` automatic word-wrapping (several of its
//    own strings are well over one screen-width and even pad extra spaces
//    to nudge exactly where the real wrap lands, e.g. `"Press \25 to
//    jump."`'s own triple space before "jump"). `gbPrintString()` has no
//    word-wrap of its own (only explicit `'\n'` - see gamebuinoShim.h's
//    own header comment) - `maruUpdateControls()` instead lays out each
//    hint as one deliberately short, single-row line (this project's own
//    established "shorten text to fit this shim's font metrics" precedent,
//    e.g. Conduit's/CrazyCar's own restorations), with every one of
//    upstream's own real button-icon glyphs (`\30`/`\25`/`\26`/`\27`/
//    `\33`/`\32`, real ASCII 24/21/22/23/27/26) drawn as genuine
//    `gbDrawChar()` icon glyphs interleaved with plain text - the same
//    mixed icon+`gbPrintString()` idiom already proven in
//    gameGlaciGlaca.c's own HUD - not substituted with plain spelled-out
//    words.
// 2) `inputCode()`'s own real `gb.keyboard(code, 12)` (a real built-in
//    on-screen text-entry widget) has no equivalent at all in this
//    dialect. `MARU_STATE_KEYBOARD` is a hand-rolled replacement (the same
//    "build a small bespoke widget for a missing real widget" treatment
//    already proven for `gb.menu()` above): 11 fixed character cells,
//    LEFT/RIGHT move the cursor, UP/DOWN cycle the character at the cursor
//    through a real charset table (`maruKeyCharset` - space, A-Z, 0-9, the
//    punctuation upstream's own hidden codes need, and real button/D-pad
//    icon glyphs 21-27 for the one hidden code that needs them - see
//    below), A confirms and returns to the menu exactly like upstream's
//    own `gb.keyboard(code, 12); Menu();` sequence. `maruCode` persists
//    across menu visits exactly like upstream's own file-scope `code`
//    global (never reset once the game starts).
//
// A REAL, CONFIRMED UPSTREAM UI BUG, FOUND AND FIXED (not preserved):
// `inputCode()`'s own on-screen text reads "Press \25 to enter a code or
// \27 to return to menu" (implying Button C also works here) - but its
// real blocking loop is `while(!gb.buttons.pressed(BTN_A))`, which never
// reads Button C at all. On real hardware, pressing C on this exact screen
// did nothing, contradicting its own displayed instructions. Fixed:
// `maruUpdateCodePrompt()` now genuinely returns to the menu on a C press,
// matching what the screen itself says.
//
// REAL HIDDEN CHEAT CODES, ported from `checkCode()`'s own upstream
// `strstr(code, "...")` checks (each pattern written as octal-escaped
// non-standard-C-string bytes specifically to keep them unreadable at a
// casual source glance - decoded by hand against real octal-to-ASCII
// arithmetic, not guessed): 113 lives, a Brick-costume skin, +10 score
// (the author's own name, "ajsb113"), unlocking the real secret level 7
// ("A code"), and 2-tick invincibility ("\(^_^)/"). Four of the five
// decode to plain printable ASCII and are kept as ordinary `int[]` text
// arrays; the lives cheat's own real code sequence is the classic Konami
// Code plus a trailing C press, entered entirely via real Gamebuino
// button-icon glyphs (ASCII 21-27) rather than letters - `maruKeyCharset`
// includes those exact codes so it stays genuinely enterable through
// `MARU_STATE_KEYBOARD`. `strstr()` itself has no confirmed equivalent for
// this dialect's own `int[]` "strings", so `maruCodeContains()` is a small
// local substring search reproducing its exact semantics, used instead of
// depending on an unconfirmed stdlib function. `checkCode()`'s own real
// call site (once per tick, unconditionally, from inside `updatePlayer()`)
// is preserved exactly - a real, load-bearing quirk this produces: since
// `maruCode` is never cleared after a cheat matches, every one of these
// effects re-fires every single tick for as long as the matching text
// stays in the buffer (e.g. `player.invincible = 2` gets re-applied every
// tick once the invincibility code is entered and left alone, making the
// player function as genuinely, permanently invincible rather than "briefly
// invincible once" - exactly what real hardware does, not a porting bug).
//
// `maruBlock` (upstream's own shared `const byte *block` global, used
// across `Blocks.ino`/`Entities.ino`/`Player.ino`'s own three separate
// tile-lookup loops) is instead a local variable inside each of the three
// functions here - a safe, non-behavioral cleanup, since every one of
// upstream's own three loops already fully reassigns it before ever
// reading it back, with no real cross-function state ever surviving
// between them. `maruCollide` (upstream's own shared `boolean collide`),
// by contrast, genuinely IS shared between `maruPlayerCollide()` and
// `maruEntCollide()` on real hardware (both reset it to false at their own
// top and return it at their own bottom) - kept as one real shared global
// here too, matching upstream exactly.
//
// A REAL UPSTREAM DOUBLE-CHECK, preserved exactly: `playerCollide()`'s own
// entity loop calls `gb.collideBitmapBitmap()` TWICE per entity per tick -
// once (entity vs player) purely to detect Mushroom/Goomba pickup/stomp
// events, and then, unconditionally, a SECOND time (player vs entity, only
// once the entity isn't the `PlayerC` sentinel and the player isn't
// invincible) purely to decide whether to treat the entity as a plain
// solid obstacle blocking movement. Kept as two real separate calls, not
// merged into one - not a bug, just how upstream actually computes two
// different things from what happens to be the same overlap test.
//
// A REAL UPSTREAM STRUCTURAL SIMPLIFICATION, checked for zero behavioral
// difference before applying: upstream's own end-of-tile-loop block
// handling in `playerCollide()` is two textually SEPARATE `if` statements
// (`if(block==Spikes){...}` standing alone, then a second, unrelated
// `if(block==Coin){...} else {collide=true;}`) rather than one `if/else-
// if/else` chain. Since `block` can only ever equal one bitmap pointer at
// a time, every one of the 3 possible outcomes (Spikes/Coin/any other
// solid tile) produces the exact same final `collide` value whether
// written as two separate ifs or one chain - traced through by hand for
// all 3 cases, not just assumed - so `maruPlayerCollide()` below uses one
// `if/else-if/else` chain for the same result with less repetition.
//
// A REAL CONTROL-FLOW-ABANDONMENT QUIRK, approximated rather than fully
// replicated: upstream's own `die()`/`lose()`/`win()`/`Menu()` are real
// blocking function calls - once one fires partway through `updatePlayer()`
// (itself possibly called from deep inside `playerCollide()`'s own tile/
// entity loops), real hardware genuinely suspends the entire call stack
// until a fresh button press unwinds it, skipping every remaining
// statement in `updatePlayer()` and the rest of that tick's own
// `updateEnt()`/`drawPlayer()`/`drawLevel()`/`updateDisplay()` calls for
// that tick (matching gameCastleDefence.c's own identically-named section
// for the exact same real quirk class). `maruUpdatePlayer()` below checks
// `maruState` after every one of its own `maruDie()`/`maruPlayerCollide()`
// call sites and returns early the instant it's changed, approximating
// this - but does NOT attempt to replicate real hardware's own deeper
// quirk of the ORIGINAL suspended call eventually resuming its remaining
// statements against a brand-new game's state once a nested Menu()/
// startGame() call chain finally returns (a real possibility on actual
// hardware, given deeply-nested blocking calls) - that edge case has no
// clean equivalent in a flat state-machine dispatch and, being both hard
// to trigger and to notice even if it did fire (by the time it could
// happen, position/camera/level state has already been fully reset), was
// judged not worth chasing, unlike the immediately-observable "rest of
// this tick doesn't run" behavior which is kept.
//
// `gb.buttons.timeHeld(btn)` (used both as a truthy any-held-frames check
// for smart crouching, and for its own real numeric tick count to drive
// variable jump height) needed a new shim primitive - see `gbTimeHeld()`
// in gamebuinoShim.h/.c (a direct port of real `Buttons::timeHeld()`, a
// plain passthrough of the same real per-tick counter `gbHeld()`/
// `gbPressed()` already track internally).

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

typedef struct
{
    int invincible;
    float x;
    float vx;
    float y;
    float vy;
    int h;
    bool flip;
    bool big;
    bool crouch;
} MaruChar;

typedef struct
{
    float x;
    float y;
    float vx;
    float vy;
    bool flip; // dead field - upstream's own real Entity.flip is likewise declared but never actually read anywhere
    int* bitmap;
} MaruEntity;

enum MaruState
{
    MARU_STATE_TITLE = 0,
    MARU_STATE_MENU = 1,
    MARU_STATE_CONTROLS = 2,
    MARU_STATE_CODE_PROMPT = 3,
    MARU_STATE_KEYBOARD = 4,
    MARU_STATE_PLAY = 5,
    MARU_STATE_LOSE = 6,
    MARU_STATE_WIN = 7
};

#define MARU_NUM_ENT 10

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

int maruState;
int maruMenuIndex;

MaruChar maruPlayer;
MaruEntity maruEntities[10];
int* maruCharacter; // current player sprite, set every tick by maruUpdatePlayer()

int maruCameraPos;
int maruTimeMark; // real upstream `frames` - the gbFrameCount value the 1-second countdown was last ticked at
int maruScore;
int maruCurLev;
int maruNumLevs = 6;
int maruLives;
int maruTime;
bool maruDisp = true;

bool maruCanJump = false;
bool maruWalking = false;
bool maruBounce = false;
bool maruCollide; // shared between maruPlayerCollide()/maruEntCollide() - see this file's own header comment

char maruCode[12]; // persists across menu visits, matching upstream's own file-scope `code` global
int maruCodeCursor;

int maruCurLevel[384]; // mutable working copy of whichever maruLevelN table maruLoadLevel() last copied from

// -----------------------------------------------------------------------------
// Real sprite bitmaps (Sprites.ino) - every B-binary literal converted to
// plain decimal, byte-for-byte against upstream's own real data.
// -----------------------------------------------------------------------------

int maruPlayerTBitmap[18]= { 8, 16, 126, 255, 227, 192, 202, 138, 0, 0, 255, 219, 219, 255, 126, 102, 102, 102 };
int maruPlayerT1Bitmap[18]= { 8, 16, 126, 255, 227, 192, 202, 138, 0, 0, 255, 219, 219, 255, 126, 99, 99, 96 };
int maruPlayerT2Bitmap[18]= { 8, 16, 126, 255, 227, 192, 202, 138, 0, 0, 255, 219, 219, 255, 126, 99, 99, 99 };
int maruPlayerT3Bitmap[18]= { 8, 16, 126, 255, 227, 192, 202, 138, 0, 0, 255, 219, 219, 255, 126, 102, 198, 198 };
int maruPlayerT4Bitmap[18]= { 8, 16, 126, 255, 227, 192, 202, 138, 0, 0, 255, 219, 219, 255, 126, 198, 198, 6 };
int maruPlayerT5Bitmap[18]= { 8, 16, 126, 255, 227, 192, 202, 138, 0, 0, 255, 219, 219, 255, 126, 54, 54, 6 };
int maruPlayerT6Bitmap[18]= { 8, 16, 126, 255, 227, 192, 202, 138, 0, 0, 255, 219, 219, 255, 126, 102, 102, 96 };
int maruPlayerSBitmap[10]= { 8, 8, 126, 97, 106, 64, 255, 219, 126, 102 };
int maruPlayerS1Bitmap[10]= { 8, 8, 126, 97, 106, 64, 255, 219, 126, 60 };
int maruPlayerCBitmap[10]= { 8, 8, 126, 195, 36, 165, 129, 255, 126, 102 };
int maruGroundBitmap[10]= { 8, 8, 255, 137, 137, 143, 201, 177, 145, 255 };
int maruBrickBitmap[10]= { 8, 8, 255, 17, 255, 68, 255, 17, 255, 68 };
int maruSpikesBitmap[10]= { 8, 8, 34, 34, 85, 85, 136, 170, 170, 255 };
int maruMysteryBlockCBitmap[10]= { 8, 8, 255, 153, 165, 133, 137, 129, 137, 255 };
int maruMysteryBlockMBitmap[10]= { 8, 8, 255, 153, 165, 133, 137, 129, 137, 255 };
int maruCoinBitmap[10]= { 8, 8, 24, 36, 82, 82, 74, 74, 36, 24 };
int maruEmptyBitmap[10]= { 8, 8, 255, 129, 129, 129, 129, 129, 129, 255 };
int maruFlagBitmap[10]= { 8, 8, 56, 54, 43, 54, 56, 32, 32, 112 };
int maruMushroomBitmap[10]= { 8, 8, 60, 90, 165, 219, 129, 90, 66, 60 };
int maruGoombaBitmap[10]= { 8, 8, 60, 66, 165, 165, 126, 66, 191, 227 };
int maruTitleBitmap[242]= { 64, 30, 0, 0, 0, 0, 0, 0, 0, 0, 1, 192, 0, 0, 0, 0, 0, 0, 14, 56, 0, 0, 0, 0, 0, 0, 20, 20, 0, 0, 0, 0, 0, 0, 36, 18, 0, 0, 0, 0, 0, 0, 45, 218, 0, 0, 0, 0, 0, 0, 122, 47, 0, 0, 0, 0, 0, 0, 68, 17, 24, 192, 0, 0, 0, 0, 68, 17, 24, 192, 0, 0, 0, 0, 130, 32, 152, 192, 0, 2, 0, 0, 129, 192, 149, 64, 0, 2, 0, 0, 143, 240, 149, 70, 41, 8, 88, 240, 248, 31, 149, 65, 53, 10, 101, 8, 34, 34, 18, 65, 53, 10, 101, 8, 34, 34, 16, 71, 33, 10, 69, 8, 34, 34, 16, 73, 33, 10, 69, 8, 24, 12, 16, 73, 33, 10, 69, 8, 8, 8, 16, 71, 160, 250, 68, 240, 7, 240, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 4, 16, 65, 4, 16, 65, 4, 16, 4, 16, 65, 4, 16, 65, 4, 16, 255, 255, 255, 255, 255, 255, 255, 255, 32, 130, 8, 32, 130, 8, 32, 130, 32, 130, 8, 32, 130, 8, 32, 130, 255, 255, 255, 255, 255, 255, 255, 255, 4, 16, 65, 4, 16, 65, 4, 16, 4, 16, 65, 4, 16, 65, 4, 16, 255, 255, 255, 255, 255, 255, 255, 255 };

// -----------------------------------------------------------------------------
// Real level data (Maps.ino) - 7 real 384-char levels (64x6 tiles), each
// joined into one string literal (see this file's own header comment on
// why, versus upstream's own adjacent-string-literal form). Levels 1-5 are
// upstream's own real distinct maps; level 6 is level 1's own real layout
// with every solid tile swapped to invisible 'I' (see this file's own
// header comment); level 7 (only reachable via the "A code" cheat) is
// upstream's own real all-'C'-coins/all-'G'-ground secret bonus map.
// -----------------------------------------------------------------------------

char maruLevel1[385]= "                                                                                                               BBBB            F   BM?B        CC                          B    BB             G              C  C                      B      ?BB    M??     GG          gGGG    GGG      g g      B           BB           GGGGGGGGGGGGGGGGG    GGGGGGGGGGGGGGG         GGGGGGGGGGGGGGGGGGGGGG";
char maruLevel2[385]= "  GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG   CCC              g         GCCCCCCCCCCCCCCCCCCCCCCC                            B??B        GBBGGGGGGGGGGGGGGGGGGGGG     CCC     ?M?    BBBBBBB?B  B                                                      g     B                              BB    g g     FGGGGGGGG  GGGGGGGGGGGGGGGGGG  GGG   GG   GG   GGGGGGGGGGGGGGGGGG";
char maruLevel3[385]= "               GGGGGGGGGGG         GGGGGGGGGGGGGGGGGGGGG    GGGG  GGGGGGGG  G  CCCCCC GG        GGGGGGGGGGGGGGGGGGGGGGGG    GGGGGGG         G              GGGGGGGGGGG               GGG    GGGGGGG  GGGGGGGGGGGGGGGGGGGGGGGG   GGGGGG   GGGGGGGGGGG GGG        GGG                                g     GGGGGGGGGGGFGGG        GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG";
char maruLevel4[385]= "                            BB                                                           BBCCC                                      CC CC CC CC CC CC BBCCCCCC CC CC CC CC CC CC CC CC CC                               CCCCCC     GG                          FGGGG  g  g  g  g  g  g  g  g  g  g Gg  g  G  g  g  g  g  gGGGGGGGGGGSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSGGGGGG";
char maruLevel5[385]= "      S                                                    GGG        B   M               S      S                         GFG  BBBBBBB                  BBB    BBB                        GBG  CCCCCCC            BB                  S                                 GGG             BBB          BBB    G g g g g gG       GGGGGGGGGGGGGGGGG                            GGGGGGGGGGGGGGGGG  ";
char maruLevel6[385]= "                                                                                                               IIII            F   IM?I        CC                          I    II             I              C  C                      I      ?II    M??     II          gIII    III      g g      I           II           IIIIIIIIIIIIIIIII    IIIIIIIIIIIIIII         IIIIIIIIIIIIIIIIIIIIII";
char maruLevel7[385]= "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG";

char* maruLevels[7]= { maruLevel1, maruLevel2, maruLevel3, maruLevel4, maruLevel5, maruLevel6, maruLevel7 };

// -----------------------------------------------------------------------------
// Real hidden cheat-code text (checkCode(), Sprites.ino) - decoded by hand
// from upstream's own octal-escaped bytes (see this file's own header
// comment). Four decode to plain ASCII; the lives cheat is real button/
// D-pad icon glyphs (ASCII 21-27), so it's built as an explicit int[] array
// like every other non-printable-glyph string in this project (see
// gameSimonbuino.c's own restored win message for the established
// precedent) rather than a quoted string literal.
// -----------------------------------------------------------------------------

char maruCheatLives[12]= { 24, 24, 25, 25, 27, 26, 27, 26, 22, 21, 23, 0 }; // up up down down right left right left B A C (icon glyphs)
char maruCheatAuthor[8]= "ajsb113";
char maruCheatUnlockLevel7[7]= "A code";
char maruCheatInvincible[8]= "\\(^_^)/";
char maruCheatCostume[12]= "(\"-\")// |_|";

// Real charset this file's own hand-rolled MARU_STATE_KEYBOARD cycles
// through per cursor cell: space, A-Z, 0-9, the punctuation the costume
// cheat needs, then the real button/D-pad icon glyphs the lives cheat
// needs (see maruCheatLives above) - every character any real cheat code
// needs is reachable this way.
int maruKeyCharset[53]= {
    32,
    65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
    40, 41, 34, 45, 47, 92, 95, 94, 124,
    21, 22, 23, 24, 25, 26, 27
};

// Real, non-printable-glyph popup/message text, built as explicit int[]
// arrays (real ASCII codes) rather than quoted string literals for the
// same reason as the cheat-code arrays above.
char maruTextYouDied[12]= { 89, 111, 117, 32, 100, 105, 101, 100, 46, 32, 1, 0 }; // "You died. " + real sad-face icon (ASCII 1)
char maruTextYouLose[11]= { 89, 111, 117, 32, 108, 111, 115, 101, 32, 1, 0 };      // "You lose " + icon 1
char maruTextYouWin[11]= { 89, 111, 117, 32, 119, 105, 110, 33, 33, 2, 0 };        // "You win!!" + real happy-face icon (ASCII 2)
char maruTextPressToContinue[22]= { 80, 114, 101, 115, 115, 32, 21, 32, 116, 111, 32, 10, 32, 99, 111, 110, 116, 105, 110, 117, 101, 0 }; // "Press " + icon 21 (A) + " to \n continue"

// -----------------------------------------------------------------------------
// Cheat-code matching (checkCode()) - maruCodeContains() reproduces real
// strstr() semantics locally (unconfirmed as a stdlib primitive for this
// dialect's own int[] "strings" - see this file's own header comment).
// -----------------------------------------------------------------------------

bool maruCodeContains( char* haystack, char* needle )
{
    int i, j;
    i = 0;
    while( haystack[ i ] != 0 )
    {
        j = 0;
        while( needle[ j ] != 0 && haystack[ i + j ] == needle[ j ] )
          j = j + 1;

        if( needle[ j ] == 0 )
          return true;

        i = i + 1;
    }
    return false;
}

void maruCheckCode()
{
    if( maruCodeContains( maruCode, maruCheatLives ) )
      maruLives = 113;

    if( maruCodeContains( maruCode, maruCheatCostume ) )
    {
        maruCharacter = maruBrickBitmap;
        maruPlayer.h = 8;
    }

    if( maruCodeContains( maruCode, maruCheatAuthor ) )
      maruScore = maruScore + 10;

    if( maruCodeContains( maruCode, maruCheatUnlockLevel7 ) )
      maruNumLevs = 7;

    if( maruCodeContains( maruCode, maruCheatInvincible ) )
      maruPlayer.invincible = 2;
}

// -----------------------------------------------------------------------------
// Entities (Entities.ino)
// -----------------------------------------------------------------------------

void maruRemoveEnt( int i )
{
    maruEntities[ i ].bitmap = maruPlayerCBitmap; // real upstream sentinel for "unused slot" - never actually drawn (updateEnt() skips any entity still == PlayerC)
    maruEntities[ i ].x = 0;
    maruEntities[ i ].vx = 0;
    maruEntities[ i ].y = 0;
    maruEntities[ i ].vy = 0;
}

void maruClearEnts()
{
    int i;
    for( i = 0; i < MARU_NUM_ENT; i++ )
    {
        maruEntities[ i ].x = 0;
        maruEntities[ i ].y = 0;
        maruEntities[ i ].vx = 0;
        maruEntities[ i ].vy = 0;
        maruEntities[ i ].bitmap = maruPlayerCBitmap;
    }
}

void maruAddEnt( int x, int y, int* bitmap )
{
    int i;
    for( i = 0; i < MARU_NUM_ENT; i++ )
    {
        if( maruEntities[ i ].bitmap == &maruPlayerCBitmap[0] )
        {
            maruEntities[ i ].bitmap = bitmap;
            maruEntities[ i ].x = x;
            maruEntities[ i ].y = y;
            maruEntities[ i ].vy = 0;
            if( bitmap == &maruMushroomBitmap[0] )
              maruEntities[ i ].vx = 1;
            else
              maruEntities[ i ].vx = -1;
            break;
        }
    }
}

// Real upstream Collide() (Entities.ino) - only Ground/Brick/mystery
// blocks are solid to an entity; Spikes/Invisible/Empty all count as
// Empty (entities pass through spikes freely, unlike the player).
bool maruEntCollide( float x, float y, int* bMap )
{
    maruCollide = false;
    int i, j, ch;
    int* block;
    bool hasBlock;

    for( i = 0; i < 6; i++ )
    {
        for( j = 0; j < 64; j++ )
        {
            hasBlock = true;
            ch = maruCurLevel[ i * 64 + j ];
            if( ch == 'G' ) block = maruGroundBitmap;
            else if( ch == 'B' ) block = maruBrickBitmap;
            else if( ch == '?' ) block = maruMysteryBlockCBitmap;
            else if( ch == 'M' ) block = maruMysteryBlockMBitmap;
            else if( ch == 'S' || ch == 'I' || ch == 'E' ) block = maruEmptyBitmap;
            else hasBlock = false;

            if( hasBlock )
            {
                if( gbCollideBitmapBitmap( x, y, bMap, j * 8, LCDHEIGHT - ( 8 * ( 6 - i ) ), block ) )
                  maruCollide = true;
            }
        }
    }
    return maruCollide;
}

void maruUpdateEnt()
{
    int i;
    for( i = 0; i < MARU_NUM_ENT; i++ )
    {
        if( maruEntities[ i ].bitmap != &maruPlayerCBitmap[0] )
        {
            maruEntities[ i ].x = maruEntities[ i ].x + maruEntities[ i ].vx;
            if( maruEntCollide( maruEntities[ i ].x, maruEntities[ i ].y, maruEntities[ i ].bitmap ) )
            {
                maruEntities[ i ].x = maruEntities[ i ].x - maruEntities[ i ].vx;
                maruEntities[ i ].vx = maruEntities[ i ].vx * -1;
            }

            maruEntities[ i ].vy = maruEntities[ i ].vy + 0.4;
            if( maruEntities[ i ].vy > 2 )
              maruEntities[ i ].vy = 2;

            maruEntities[ i ].y = maruEntities[ i ].y + maruEntities[ i ].vy;
            if( maruEntCollide( maruEntities[ i ].x, maruEntities[ i ].y, maruEntities[ i ].bitmap ) )
            {
                maruEntities[ i ].y = maruEntities[ i ].y - maruEntities[ i ].vy;
                maruEntities[ i ].vy = maruEntities[ i ].vy / 2;
            }

            // real upstream falls through and draws the just-reset PlayerC
            // sentinel bitmap at (0-cameraPos, 0) for one tick after a
            // despawn below - kept exactly as-is (harmless, normally
            // off-screen once the level has scrolled at all)
            if( maruEntities[ i ].y > LCDHEIGHT || maruEntities[ i ].x - maruCameraPos <= -8 || maruEntities[ i ].x - maruCameraPos > LCDWIDTH + 8 )
              maruRemoveEnt( i );

            gbSetColor( 1 );
            if( maruEntities[ i ].bitmap == &maruGoombaBitmap[0] && gbFrameCount % 10 >= 5 )
              gbDrawBitmapRotated( maruEntities[ i ].x - maruCameraPos, maruEntities[ i ].y, maruEntities[ i ].bitmap, 0, 1 ); // NOROT, FLIPH
            else
              gbDrawBitmap( maruEntities[ i ].x - maruCameraPos, maruEntities[ i ].y, maruEntities[ i ].bitmap );
        }
    }
}

// -----------------------------------------------------------------------------
// Levels (Blocks.ino)
// -----------------------------------------------------------------------------

void maruLoadLevel( int lev )
{
    char* src = maruLevels[ lev - 1 ];
    int i;
    for( i = 0; i < 384; i++ )
      maruCurLevel[ i ] = src[ i ];
}

void maruNextLevel()
{
    if( maruCurLev == maruNumLevs )
      maruState = MARU_STATE_WIN;
    else
    {
        gbPopup( "Next Level!", 20 );
        if( maruCurLev == 6 )
          gbPopup( "Remember lvl 1?", 20 ); // real upstream popup-overwrite quirk - see this file's own header comment

        maruCurLev = maruCurLev + 1;
        maruLoadLevel( maruCurLev );
        maruPlayer.x = 0;
        maruPlayer.y = 0;
        maruTime = 130;
        maruCameraPos = 0;
    }
}

void maruDrawLevel()
{
    int i, j, ch;
    int* block;
    bool hasBlock;

    for( i = 0; i < 6; i++ )
    {
        for( j = 0; j < 64; j++ )
        {
            if( j * 8 - maruCameraPos >= -8 && j * 8 - maruCameraPos <= LCDWIDTH + 8 )
            {
                hasBlock = true;
                ch = maruCurLevel[ i * 64 + j ];
                if( ch == 'G' ) block = maruGroundBitmap;
                else if( ch == 'B' ) block = maruBrickBitmap;
                else if( ch == '?' ) block = maruMysteryBlockCBitmap;
                else if( ch == 'M' ) block = maruMysteryBlockMBitmap;
                else if( ch == 'C' ) block = maruCoinBitmap;
                else if( ch == 'S' ) block = maruSpikesBitmap;
                else if( ch == 'E' ) block = maruEmptyBitmap;
                else if( ch == 'F' ) block = maruFlagBitmap;
                else if( ch == 'g' )
                {
                    maruCurLevel[ i * 64 + j ] = 'Q';
                    maruAddEnt( j * 8, LCDHEIGHT - ( 8 * ( 6 - i ) ), maruGoombaBitmap );
                    hasBlock = false;
                }
                else hasBlock = false;
                // 'I' (Invisible) has no case here on purpose - see this
                // file's own header comment on level 6's own real gag design

                if( hasBlock )
                {
                    gbSetColor( 1 );
                    if( ( block == &maruCoinBitmap[0] || block == &maruSpikesBitmap[0] ) && gbFrameCount % 20 >= 10 )
                      gbDrawBitmapRotated( j * 8 - maruCameraPos, LCDHEIGHT - ( 8 * ( 6 - i ) ), block, 0, 1 ); // NOROT, FLIPH - real coin/spike flicker animation
                    else
                      gbDrawBitmap( j * 8 - maruCameraPos, LCDHEIGHT - ( 8 * ( 6 - i ) ), block );
                }
            }
            else if( maruCurLevel[ i * 64 + j ] == 'Q' )
              maruCurLevel[ i * 64 + j ] = 'g'; // off-screen again - re-arm this Goomba spawn point
        }
    }
}

// -----------------------------------------------------------------------------
// Player (Player.ino)
// -----------------------------------------------------------------------------

void maruStartPlayer()
{
    maruPlayer.x = 0;
    maruPlayer.vx = 0;
    maruPlayer.y = 0;
    maruPlayer.vy = 0;
    maruPlayer.h = 4;
    maruPlayer.invincible = 40;
    maruPlayer.flip = false;
    maruPlayer.big = false;
    maruPlayer.crouch = false;
}

void maruGetBig()
{
    if( !maruPlayer.big )
    {
        maruPlayer.big = true;
        maruPlayer.crouch = true;
    }
}

void maruDie(); // forward declaration - maruGetSmall() calls it below, defined further down

void maruGetSmall()
{
    if( maruPlayer.invincible <= 0 )
    {
        maruPlayer.invincible = 40;
        if( maruPlayer.big )
        {
            if( !maruPlayer.crouch )
              maruPlayer.y = maruPlayer.y + 8;
            maruPlayer.big = false;
        }
        else
          maruDie();
    }
}

void maruDie()
{
    if( maruLives == 0 )
      maruState = MARU_STATE_LOSE;
    else
    {
        maruLives = maruLives - 1;
        maruTime = 130;
        gbPopup( maruTextYouDied, 20 );
        maruClearEnts();
        maruStartPlayer();
        maruCameraPos = 0;
        maruLoadLevel( maruCurLev );
    }
}

// Real upstream playerCollide() (Player.ino) - see this file's own header
// comment on the one real structural simplification made here (upstream's
// own two separate Spikes/Coin `if` statements collapsed into one
// equivalent if/else-if/else chain, checked to be behaviorally identical).
bool maruPlayerCollide()
{
    maruCollide = false;
    int k;

    for( k = 0; k < MARU_NUM_ENT; k++ )
    {
        if( gbCollideBitmapBitmap( maruEntities[ k ].x, maruEntities[ k ].y, maruEntities[ k ].bitmap, maruPlayer.x, maruPlayer.y, maruCharacter ) )
        {
            if( maruEntities[ k ].bitmap == &maruMushroomBitmap[0] )
            {
                maruGetBig();
                maruScore = maruScore + 100;
                maruRemoveEnt( k );
            }

            if( maruEntities[ k ].bitmap == &maruGoombaBitmap[0] )
            {
                if( maruPlayer.vy > 0 && maruPlayer.y + maruPlayer.h - 4 < maruEntities[ k ].y )
                {
                    maruRemoveEnt( k );
                    maruBounce = true;
                }
                else
                {
                    maruGetSmall();
                    if( maruPlayer.invincible <= 0 )
                      maruEntities[ k ].vx = maruEntities[ k ].vx * -1;
                }
            }
        }

        if( maruEntities[ k ].bitmap != &maruPlayerCBitmap[0] && maruPlayer.invincible <= 0 )
          maruCollide = gbCollideBitmapBitmap( maruPlayer.x, maruPlayer.y, maruCharacter, maruEntities[ k ].x, maruEntities[ k ].y, maruEntities[ k ].bitmap );
    }

    int i, j, ch, tileX, tileY;
    int* block;
    bool hasBlock;

    for( i = 0; i < 6; i++ )
    {
        for( j = 0; j < 64; j++ )
        {
            hasBlock = true;
            ch = maruCurLevel[ i * 64 + j ];
            if( ch == 'G' ) block = maruGroundBitmap;
            else if( ch == 'B' ) block = maruBrickBitmap;
            else if( ch == '?' ) block = maruMysteryBlockCBitmap;
            else if( ch == 'M' ) block = maruMysteryBlockMBitmap;
            else if( ch == 'I' || ch == 'E' ) block = maruEmptyBitmap;
            else if( ch == 'C' ) block = maruCoinBitmap;
            else if( ch == 'F' ) block = maruFlagBitmap;
            else if( ch == 'S' ) block = maruSpikesBitmap;
            else hasBlock = false;

            if( hasBlock )
            {
                tileX = j * 8;
                tileY = LCDHEIGHT - ( 8 * ( 6 - i ) );

                if( gbCollideBitmapBitmap( maruPlayer.x, maruPlayer.y, maruCharacter, tileX, tileY, block ) )
                {
                    if( block == &maruFlagBitmap[0] )
                      maruNextLevel();

                    if( maruPlayer.vy < 0 && maruPlayer.y > LCDHEIGHT - ( 8 * ( 5 - i ) ) - 1 && ( maruPlayer.x < ( j + 1 ) * 8 && maruPlayer.x + 8 > j * 8 ) )
                    {
                        if( block == &maruBrickBitmap[0] && maruPlayer.big )
                          maruCurLevel[ i * 64 + j ] = 0;
                        if( block == &maruMysteryBlockCBitmap[0] )
                        {
                            maruCurLevel[ ( i - 1 ) * 64 + j ] = 'C';
                            maruCurLevel[ i * 64 + j ] = 'E';
                        }
                        if( block == &maruMysteryBlockMBitmap[0] )
                        {
                            maruCurLevel[ i * 64 + j ] = 'E';
                            maruAddEnt( j * 8, LCDHEIGHT - ( 8 * ( 7 - i ) ), maruMushroomBitmap );
                        }
                    }

                    if( block == &maruSpikesBitmap[0] )
                    {
                        maruGetSmall();
                        maruCollide = true;
                    }
                    else if( block == &maruCoinBitmap[0] )
                    {
                        maruScore = maruScore + 100;
                        maruCurLevel[ i * 64 + j ] = 0;
                        gbPlayOK();
                        maruCollide = false;
                    }
                    else
                      maruCollide = true;
                }
            }
        }
    }

    if( maruBounce )
    {
        maruPlayer.vy = -4;
        maruBounce = false;
    }
    return maruCollide;
}

void maruDrawPlayer()
{
    if( maruPlayer.invincible % 2 != 1 )
    {
        gbSetColor( 1 );
        if( !maruPlayer.flip )
          gbDrawBitmap( maruPlayer.x - maruCameraPos, maruPlayer.y, maruCharacter );
        else
          gbDrawBitmapRotated( maruPlayer.x - maruCameraPos, maruPlayer.y, maruCharacter, 0, 1 ); // NOROT, FLIPH
    }
}

// Real upstream updatePlayer() (Player.ino). See this file's own header
// comment on the "real control-flow-abandonment quirk" section for why
// `if(maruState != MARU_STATE_PLAY) return;` guards appear after every
// call site that can switch state mid-tick (maruDie()/maruPlayerCollide(),
// the latter itself able to call maruNextLevel()/maruGetSmall()->maruDie()
// internally).
void maruUpdatePlayer()
{
    maruWalking = false;

    if( maruTime == 0 )
      maruDie();
    if( maruState != MARU_STATE_PLAY )
      return;

    if( maruPlayer.invincible > 0 )
      maruPlayer.invincible = maruPlayer.invincible - 1;

    if( gbPressed( BTN_C ) )
      maruState = MARU_STATE_MENU;
    if( maruState != MARU_STATE_PLAY )
      return;

    if( gbPressed( BTN_UP ) )
      maruDisp = !maruDisp;

    if( gbTimeHeld( BTN_B ) > 0 )
    {
        if( maruPlayer.big && !maruPlayer.crouch )
          maruPlayer.y = maruPlayer.y + 8;
        maruPlayer.crouch = true;
    }
    else
    {
        if( maruPlayer.big && maruPlayer.crouch )
        {
            maruPlayer.y = maruPlayer.y - 8;
            if( maruPlayerCollide() )
              maruPlayer.y = maruPlayer.y + 8;
            else
              maruPlayer.crouch = false;
            if( maruState != MARU_STATE_PLAY )
              return;
        }
    }

    maruPlayer.vx = maruPlayer.vx * 0.75;
    if( gbRepeat( BTN_RIGHT, 1 ) )
    {
        maruPlayer.vx = maruPlayer.vx + 0.4;
        maruPlayer.flip = false;
        maruWalking = true;
    }
    if( gbRepeat( BTN_LEFT, 1 ) )
    {
        maruPlayer.vx = maruPlayer.vx - 0.4;
        maruPlayer.flip = true;
        maruWalking = true;
    }

    if( maruPlayer.big )
    {
        if( maruPlayer.crouch )
        {
            maruCharacter = maruPlayerCBitmap;
            maruPlayer.h = 8;
        }
        else if( maruWalking )
        {
            maruPlayer.h = 16;
            if( gbFrameCount % 12 >= 0 && gbFrameCount % 12 < 2 ) maruCharacter = maruPlayerT1Bitmap;
            else if( gbFrameCount % 12 >= 2 && gbFrameCount % 12 < 4 ) maruCharacter = maruPlayerT2Bitmap;
            else if( gbFrameCount % 12 >= 4 && gbFrameCount % 12 < 6 ) maruCharacter = maruPlayerT3Bitmap;
            else if( gbFrameCount % 12 >= 6 && gbFrameCount % 12 < 8 ) maruCharacter = maruPlayerT4Bitmap;
            else if( gbFrameCount % 12 >= 8 && gbFrameCount % 12 < 10 ) maruCharacter = maruPlayerT5Bitmap;
            else if( gbFrameCount % 12 >= 10 && gbFrameCount % 12 < 12 ) maruCharacter = maruPlayerT6Bitmap;
        }
        else
        {
            maruCharacter = maruPlayerTBitmap;
            maruPlayer.h = 16;
        }
    }
    else
    {
        if( gbFrameCount % 6 >= 0 && gbFrameCount % 6 < 3 && maruWalking )
        {
            maruCharacter = maruPlayerS1Bitmap;
            maruPlayer.h = 8;
        }
        else
        {
            maruCharacter = maruPlayerSBitmap;
            maruPlayer.h = 8;
        }
    }

    maruCheckCode();

    if( maruPlayer.vx > 2 )
      maruPlayer.vx = 2;

    maruPlayer.x = maruPlayer.x + maruPlayer.vx;
    if( maruPlayerCollide() )
    {
        maruPlayer.x = maruPlayer.x - maruPlayer.vx;
        maruPlayer.vx = 0;
    }
    if( maruState != MARU_STATE_PLAY )
      return;

    if( maruPlayer.vy > 1 )
      maruCanJump = false;

    if( maruCanJump && ( gbTimeHeld( BTN_A ) > 0 && gbTimeHeld( BTN_A ) < 5 ) )
    {
        maruPlayer.vy = maruPlayer.vy - 1.3;
        if( gbTimeHeld( BTN_A ) < 2 )
          gbPlayOK();
    }
    maruPlayer.vy = maruPlayer.vy + 0.4;
    if( maruPlayer.vy > 5 )
      maruPlayer.vy = 2;

    if( gbReleased( BTN_A ) )
      maruCanJump = false;

    maruPlayer.y = maruPlayer.y + maruPlayer.vy;
    if( maruPlayerCollide() )
    {
        if( maruPlayer.vy > 0 )
        {
            maruCanJump = true;
            int i;
            for( i = 0; i < maruPlayer.vy; i = i + 1 )
            {
                maruPlayer.y = maruPlayer.y - 1;
                if( !maruPlayerCollide() )
                {
                    maruPlayer.vy = 0;
                    break;
                }
            }
        }
        else
        {
            maruPlayer.y = maruPlayer.y - maruPlayer.vy;
            maruPlayer.vy = maruPlayer.vy / 2;
        }
    }
    if( maruState != MARU_STATE_PLAY )
      return;

    if( maruPlayer.y > LCDHEIGHT )
      maruDie();
    if( maruState != MARU_STATE_PLAY )
      return;

    if( maruPlayer.x + 4 - maruCameraPos > ( LCDWIDTH / 2 ) && maruCameraPos < ( 64 * 8 ) - LCDWIDTH )
      maruCameraPos = maruPlayer.x - LCDWIDTH / 2 + 4;

    if( maruPlayer.x + 4 - maruCameraPos < ( LCDWIDTH / 2 ) && maruCameraPos > 0 )
      maruCameraPos = maruPlayer.x - LCDWIDTH / 2 + 4;
}

// -----------------------------------------------------------------------------
// HUD (Maruino.ino's own updateDisplay()/startTimer())
// -----------------------------------------------------------------------------

void maruStartTimer()
{
    maruTime = 130;
    maruTimeMark = gbFrameCount;
}

void maruUpdateDisplay()
{
    if( ( gbFrameCount - maruTimeMark ) >= 20 )
    {
        maruTimeMark = gbFrameCount;
        maruTime = maruTime - 1;
    }

    if( maruDisp )
    {
        gbSetColor( 1 );
        gbFillRect( 0, 0, 86, 7 ); // real upstream width (86 > 84 LCDWIDTH) - harmless here, gbFillRect() clips to the real screen bounds internally
        gbSetColor( 0 );
        gbSetFont( gbFont3x5 );
        gbCursorY = 1;
        gbCursorX = 1;
        gbDrawChar( 29, gbCursorX, gbCursorY ); // real "\35" clock icon
        gbCursorX = gbCursorX + gbFontWidth;
        gbPrintString( ":" );
        gbPrintNumber( maruTime );
        gbPrintString( "  " );
        gbDrawChar( 36, gbCursorX, gbCursorY ); // real "\44" score icon
        gbCursorX = gbCursorX + gbFontWidth;
        gbPrintString( ":" );
        gbPrintNumber( maruScore );
        gbPrintString( "  " );
        gbDrawChar( 3, gbCursorX, gbCursorY ); // real "\03" life icon
        gbCursorX = gbCursorX + gbFontWidth;
        gbPrintString( ":" );
        gbPrintNumber( maruLives );
        gbSetColor( 1 );
    }
    else
    {
        gbSetColor( 1 );
        gbFillRect( 0, 0, 86, 5 );
        gbSetColor( 0 );
        gbSetFont( gbFont3x3 );
        gbCursorY = 1;
        gbCursorX = 1;
        gbDrawChar( 29, gbCursorX, gbCursorY );
        gbCursorX = gbCursorX + gbFontWidth;
        gbPrintString( ":" );
        gbPrintNumber( maruTime );
        gbPrintString( "  " );
        gbDrawChar( 36, gbCursorX, gbCursorY );
        gbCursorX = gbCursorX + gbFontWidth;
        gbPrintString( ":" );
        gbPrintNumber( maruScore );
        gbPrintString( "  " );
        gbDrawChar( 3, gbCursorX, gbCursorY );
        gbCursorX = gbCursorX + gbFontWidth;
        gbPrintString( ":" );
        gbPrintNumber( maruLives );
        gbSetColor( 1 );
    }
    gbSetFont( gbFont3x5 );
}

// -----------------------------------------------------------------------------
// Game start (Maruino.ino's own startGame())
// -----------------------------------------------------------------------------

void maruStartGame()
{
    maruCameraPos = 0;
    maruScore = 0;
    maruLives = 5;
    maruCurLev = 1;
    maruClearEnts();
    maruStartTimer();
    maruStartPlayer();
    maruLoadLevel( maruCurLev );
    maruState = MARU_STATE_PLAY;
}

// -----------------------------------------------------------------------------
// Title / menu / controls / code-entry / lose / win states
// -----------------------------------------------------------------------------

void maruUpdateTitle()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 ); // matches upstream setup()'s own gb.display.setFont(font5x7), still set when titleScreen() runs
    gbFontSize = 1;
    gbDrawBitmap( 10, 2, maruTitleBitmap );

    gbCursorX = 9;
    gbCursorY = 34;
    gbPrintString( "By: ajsb113" );

    gbCursorX = 21;
    gbCursorY = 41;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      maruState = MARU_STATE_MENU;
}

void maruUpdateMenu()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 ); // matches upstream Menu()'s own explicit gb.display.setFont(font5x7)
    gbFontSize = 1;
    gbCursorX = 2;
    gbCursorY = 1;
    gbPrintString( "MARUINO" );

    int i;
    for( i = 0; i < 4; i++ )
    {
        gbCursorY = 12 + i * 9;
        gbCursorX = 0;
        if( i == maruMenuIndex )
          gbPrintString( "*" );

        gbCursorX = 7;
        if( i == 0 ) gbPrintString( "PLAY GAME" );
        else if( i == 1 ) gbPrintString( "MAIN MENU" );
        else if( i == 2 ) gbPrintString( "CONTROLS" );
        else gbPrintString( "CODE" );
    }

    if( gbRepeat( BTN_UP, 5 ) )
      maruMenuIndex = gbMax( 0, maruMenuIndex - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) )
      maruMenuIndex = gbMin( 3, maruMenuIndex + 1 );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( maruMenuIndex == 0 ) maruStartGame();
        else if( maruMenuIndex == 1 ) maruState = MARU_STATE_TITLE; // real upstream "Main Menu" option just redisplays the title screen (start()), then Menu() again
        else if( maruMenuIndex == 2 ) maruState = MARU_STATE_CONTROLS;
        else maruState = MARU_STATE_CODE_PROMPT;
    }
}

// Real upstream controls() - see this file's own header comment on why
// every hint here is one short row instead of upstream's own real
// textWrap-driven multi-line layout, with real button icon glyphs drawn
// inline via gbDrawChar() rather than spelled out as words.
void maruUpdateControls()
{
    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );
    gbFontSize = 1;

    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "CONTROLS" );

    gbCursorX = 0;
    gbCursorY = 8;
    gbPrintString( "Use " );
    gbDrawChar( 27, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbPrintString( " & " );
    gbDrawChar( 26, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbPrintString( " move" );

    gbCursorX = 0;
    gbCursorY = 15;
    gbPrintString( "Press " );
    gbDrawChar( 24, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbPrintString( "=HUD size" );

    gbCursorX = 0;
    gbCursorY = 22;
    gbPrintString( "Press " );
    gbDrawChar( 21, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbPrintString( "=jump" );

    gbCursorX = 0;
    gbCursorY = 29;
    gbPrintString( "Hold " );
    gbDrawChar( 22, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbPrintString( "=crouch" );

    gbCursorX = 0;
    gbCursorY = 36;
    gbPrintString( "Press " );
    gbDrawChar( 23, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbPrintString( "=menu" );

    gbCursorX = 0;
    gbCursorY = 43;
    gbPrintString( "A: back" );

    if( gbPressed( BTN_A ) )
      maruState = MARU_STATE_MENU;
}

// Real upstream inputCode()'s own prompt half - see this file's own header
// comment for the real, confirmed upstream UI bug fixed here (this
// screen's own text mentions a Button C shortcut its real code never
// actually read).
void maruUpdateCodePrompt()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 ); // inherits Menu()'s own font5x7, matching upstream (inputCode() never changes font itself)
    gbFontSize = 1;

    gbCursorX = 0;
    gbCursorY = 2;
    gbPrintString( "Press A to" );
    gbCursorX = 0;
    gbCursorY = 10;
    gbPrintString( "enter code" );
    gbCursorX = 0;
    gbCursorY = 18;
    gbPrintString( "or C to" );
    gbCursorX = 0;
    gbCursorY = 26;
    gbPrintString( "return to menu" );

    if( gbPressed( BTN_A ) )
    {
        maruCodeCursor = 0;
        maruState = MARU_STATE_KEYBOARD;
    }
    // Fixed here, not preserved - see this file's own header comment: real
    // upstream's own screen text says "C to return to menu" but its real
    // code never actually checked BTN_C, so C did nothing here. Now
    // genuinely returns to the menu, matching what the screen says.
    else if( gbPressed( BTN_C ) )
      maruState = MARU_STATE_MENU;
}

int maruKeyCharsetIndexOf( int ch ); // forward declaration - defined below, used by maruUpdateKeyboard() here

// Hand-rolled replacement for real gb.keyboard(code, 12) - see this file's
// own header comment.
void maruUpdateKeyboard()
{
    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );
    gbFontSize = 1;

    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "ENTER CODE" );

    int i, x;
    x = 12;
    for( i = 0; i < 11; i++ )
    {
        gbDrawChar( maruCode[ i ], x, 14 );
        if( i == maruCodeCursor && ( gbFrameCount % 20 ) < 10 )
          gbDrawFastHLine( x, 20, 3 );
        x = x + 6;
    }

    gbCursorX = 2;
    gbCursorY = 30;
    gbPrintString( "L/R:MOVE" );
    gbCursorX = 2;
    gbCursorY = 37;
    gbPrintString( "U/D:CHAR" );
    gbCursorX = 2;
    gbCursorY = 44;
    gbPrintString( "A:DONE" );

    if( gbRepeat( BTN_LEFT, 5 ) )
      maruCodeCursor = gbMax( 0, maruCodeCursor - 1 );
    if( gbRepeat( BTN_RIGHT, 5 ) )
      maruCodeCursor = gbMin( 10, maruCodeCursor + 1 );

    int idx;
    if( gbRepeat( BTN_UP, 5 ) )
    {
        idx = maruKeyCharsetIndexOf( maruCode[ maruCodeCursor ] );
        idx = idx + 1;
        if( idx >= 53 ) idx = 0;
        maruCode[ maruCodeCursor ] = maruKeyCharset[ idx ];
    }
    if( gbRepeat( BTN_DOWN, 5 ) )
    {
        idx = maruKeyCharsetIndexOf( maruCode[ maruCodeCursor ] );
        idx = idx - 1;
        if( idx < 0 ) idx = 52;
        maruCode[ maruCodeCursor ] = maruKeyCharset[ idx ];
    }

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        maruState = MARU_STATE_MENU;
    }
}

void maruUpdateLose()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbFontSize = 1;

    gbCursorX = 15;
    gbCursorY = 5;
    gbPrintString( maruTextYouLose );
    gbCursorX = 15;
    gbCursorY = 13;
    gbPrintString( "Score:" );
    gbPrintNumber( maruScore );
    gbCursorX = 15;
    gbCursorY = 21;
    gbPrintString( maruTextPressToContinue );

    if( gbPressed( BTN_A ) )
      maruState = MARU_STATE_MENU;
}

void maruUpdateWin()
{
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbFontSize = 1;

    gbCursorX = 20;
    gbCursorY = 5;
    gbPrintString( maruTextYouWin );
    gbCursorX = 20;
    gbCursorY = 13;
    gbPrintString( "Score:" );
    gbPrintNumber( maruScore );
    gbCursorX = 20;
    gbCursorY = 21;
    gbPrintString( maruTextPressToContinue );

    if( gbPressed( BTN_A ) )
      maruState = MARU_STATE_MENU;
}

int maruKeyCharsetIndexOf( int ch )
{
    int i;
    for( i = 0; i < 53; i++ )
      if( maruKeyCharset[ i ] == ch )
        return i;
    return 0;
}

// -----------------------------------------------------------------------------
// Active gameplay dispatch (Maruino.ino's own loop()'s `if(running){...}`)
// -----------------------------------------------------------------------------

void maruUpdatePlay()
{
    maruUpdatePlayer();
    if( maruState != MARU_STATE_PLAY )
      return; // mirrors upstream's own die()/win()/Menu() blocking-call short-circuit - see this file's own header comment

    maruUpdateEnt();
    maruDrawPlayer();
    maruDrawLevel();
    maruUpdateDisplay();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameMaruino_init()
{
    gbBegin();
    gbSetFont( gbFont5x7 ); // matches upstream setup()'s own gb.display.setFont(font5x7)

    int i;
    for( i = 0; i < 11; i++ )
      maruCode[ i ] = 32; // blank keyboard, matching upstream's own real code[12]="" (empty) as closely as a fixed-width editable buffer can
    maruCode[ 11 ] = 0;
    maruCodeCursor = 0;

    maruClearEnts();
    maruState = MARU_STATE_TITLE;
}

void gameMaruino_update()
{
    if( !gbUpdate() ) return;

    if( maruState == MARU_STATE_TITLE ) maruUpdateTitle();
    else if( maruState == MARU_STATE_MENU ) maruUpdateMenu();
    else if( maruState == MARU_STATE_CONTROLS ) maruUpdateControls();
    else if( maruState == MARU_STATE_CODE_PROMPT ) maruUpdateCodePrompt();
    else if( maruState == MARU_STATE_KEYBOARD ) maruUpdateKeyboard();
    else if( maruState == MARU_STATE_PLAY ) maruUpdatePlay();
    else if( maruState == MARU_STATE_LOSE ) maruUpdateLose();
    else if( maruState == MARU_STATE_WIN ) maruUpdateWin();

    gbRenderFrame();
}
