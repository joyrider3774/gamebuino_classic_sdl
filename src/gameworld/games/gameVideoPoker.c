#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Video Poker (Mike Del Pozzo, GPLv3 - github.com/delpozzo/videopoker-gamebuino).
// A real 5-card draw "Jacks or Better" video poker machine for Gamebuino
// Classic/MAKERbuino - bet chips, deal 5 cards, hold whichever you want to
// keep, draw new cards for the rest, and get paid out on a real poker-hand
// paytable (Jacks or Better through Royal Flush). A genuinely new genre for
// this cartridge - no card game ported here before. Picked from
// `more games/` per this project's own DISCOVERED_GAMES.md porting-priority
// audit.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every global got a `vpoker`-prefixed
// name (this cartridge has no linker - every ported game's globals share one
// namespace, see this project's own CLAUDE.md). `boolean` -> `bool` (a real
// primitive type here). `byte`/`long` -> plain `int` (this dialect's only
// integer width - see VIRCON32_C_DIALECT.md section 2; upstream's own 32-bit
// `long bank` loses nothing since Vircon32's `int` is already a full 32-bit
// word). Every real `B00000000`-style Arduino binary literal in the bitmap
// tables (Sprite.h) was converted to plain decimal (no such literal syntax
// exists here) via a small one-off Python conversion script (not checked
// in - a mechanical, verifiable text transform done once, the same category
// of one-off step as this project's own thumbnail-atlas compositing).
// `random(52)` -> `arand(52)` (this dialect's own established RNG helper -
// see avrCompat.h).
//
// **No switch statements** (unproven in this dialect - see this project's
// own CLAUDE.md/other ported games' header comments) - every one of
// upstream's real `switch` blocks (bet cycling on Up/Down, per-card-index x
// position lookup, suit-to-bitmap dispatch, face-card letter printing, the
// win-message text dispatch, and the debug-only `testHand()` dispatch) was
// rewritten as an if/else-if chain, preserving every real case and the
// original "no matching case -> no-op" fallthrough behavior (upstream's own
// switches have no `default` case either). The per-card x-position lookup
// specifically became a `vpokerCardX[5]` array indexed by card slot instead
// of a switch on the loop index - a pure constant-table lookup with no
// side effects, so replacing it changes nothing observable.
//
// **No `qsort()`** - Vircon32's own `misc.h` stdlib (see this project's own
// VIRCON32_C_DIALECT.md section 10's real header survey) has no `qsort`/
// comparator-callback sort at all, unlike AVR's own libc. Upstream's own
// `sortCards()` (`qsort(HandValue, 5, sizeof(int), valueCompare)`) was
// replaced with a small hand-rolled insertion sort over the same 5-element
// `vpokerHandValue[]` array - correct and trivially fast at n=5, and every
// hand-ranking function downstream of it (`isStraight`/`isFullHouse`/etc)
// only ever reads the sorted array, so behavior is unchanged.
//
// **Card model changed from a pointer array to an index array**, sidestepping
// any need to prove `struct*[N]` pointer-array declaration syntax in this
// dialect at all - the same dodge gameBlockdude.c's own header comment
// documents for its `gamemaps[]` table (only a single confirmed precedent,
// gameUfoRace.c's own `int*[N] ufoSprites`, exists project-wide, and this
// port didn't want to be the second data point). Upstream's real
// `Card *Hand[5]` (5 pointers into `CardDeck[52]`, NULL = empty slot) became
// `int[5] vpokerHandIdx` (5 indices into `vpokerDeck[52]`, -1 = empty slot,
// matching this dialect's own real NULL-pointer value per
// VIRCON32_C_DIALECT.md section 12 so the sentinel reads the same way).
// Every `Hand[i]->field` access became `vpokerDeck[vpokerHandIdx[i]].field`;
// every `Hand[i] == NULL` check became `vpokerHandIdx[i] == -1`. Pure
// mechanical substitution - no gameplay logic changed.
//
// Upstream's own three real blocking loops - `pauseGame()`, `showHandInfo()`,
// and `gameOver()` (each a `while(1) { if(gb.update()) {...} }` that steals
// the whole program until a button choice is made) - were converted into
// explicit states (`VPOKER_STATE_PAUSE`/`VPOKER_STATE_HANDINFO`/
// `VPOKER_STATE_GAMEOVER`), matching this project's established "blocking
// widget -> explicit resumable state" treatment (see gamePong.c's own
// `PONG_STATE_TITLE` for `gb.titleScreen()`, gameConduit.c's own
// `COND_STATE_MENU` for `gb.menu()`). Likewise upstream's own top-level
// `gb.titleScreen(gameLogo)` (blocking, called once from `setup()`) became
// `VPOKER_STATE_TITLE`, dismissed by a genuine `gbPressed(BTN_A)` exactly
// like every other ported game's own title screen here.
// Upstream's own `setup()` (`gb.begin(); gb.titleScreen(...); pickRandomSeed();
// startGame();`) is preserved as a real, callable sequence, not just inlined
// once: both `pauseGame()`'s own "A: Title Screen" and `gameOver()`'s own
// "A: Title Screen" options call real upstream `setup()` again on real
// hardware - ported here as `gbBegin()` (re-init display/frame state, safe
// to call again - see gamebuinoShim.c's own `gbBegin()`) followed by
// `vpokerBeginTitle()`, with `vpokerStartGame()`/`gbPickRandomSeed()` only
// actually run once the title is freshly dismissed again (matching upstream's
// own exact ordering - `startGame()` runs *after* `titleScreen()` returns,
// not before). `gameOver()`'s own "B: New Game" instead calls `startGame()`
// directly with no title screen in between, exactly like upstream.
//
// Real bitmap art restored bit-for-bit via `gbDrawBitmap()`: the 72x48
// `gameLogo` title splash (`vpokerGameLogo`), the 16x22 card-slot/selection
// border (`vpokerCardBorder`), the 16x22 card back (`vpokerCardBack`), and
// the four 16x22 suit glyphs (`vpokerCardClub`/`Diamond`/`Heart`/`Spade`).
// **Checked for the mask/fill-under-bitmap bug** (found in gameFlappyBirdo.c/
// gameParachute.c) - it does not apply here: upstream's own real draw order
// per card is text-first (`gb.display.print()` at cursor `(x+3,y+3)`), *then*
// the suit-glyph bitmap on top at `(x,y)`. This works correctly with this
// shim's own real transparency rule ("off" bits fully transparent, see
// gamebuinoShim.h) because the suit-glyph bitmaps only ever set "on" bits
// along their own outer border and the small suit icon itself - the whole
// interior (where the value text lives) is "off"/transparent in every one of
// these bitmaps, so drawing the bitmap after the text overlays only the
// border+icon and never blanks the text underneath, exactly matching real
// hardware's own real compositing. No mask layer was dropped - there never
// was one; preserved with the exact same draw order upstream uses.
//
// Sound: upstream's own `playSound()` helper drives `gb.sound.command()`
// four times per effect (volume, instrument, volume-slide, pitch-slide,
// tremolo) before a final `gb.sound.playNote(pitch, duration, channel)` -
// this shim has no `command()`/slide/tremolo machinery, only one-shot
// `gbPlayNote(pitch, duration)` (a documented, pre-existing scope limit, not
// a new gap - see this project's own CLAUDE.md "Open questions"). Every one
// of upstream's 9 real sound-effect tables (`sndWin`/`sndLose`/`sndFlip`/
// `sndHold`/`sndBet1..5`/`sndResume`/`sndPause` in Sound.h) was collapsed to
// a tiny `vpokerPlayXxx()` wrapper calling `gbPlayNote()` with that same
// table's own real pitch/duration pair (indices 8/9) - the slide/tremolo
// shaping is lost, the actual notes/durations are not.
//
// `testHand(int hand)` - upstream's own real debug helper for jumping
// straight to a specific hand result - has no live call site anywhere in
// the real source (only reachable by hand-editing the commented-out `//
// DEBUG START`/`DEBUG END` block in `pauseGame()`, which itself never calls
// it either). Genuinely dead code on real hardware as shipped; intentionally
// not ported rather than silently dropped without mention.
//
// EEPROM: upstream genuinely persists the chip bank as a real `EEPROM.read()`/
// `EEPROM.write()` 4-byte decomposition of a 32-bit `long` (`saveWallet()`/
// `loadWallet()`, addresses 0-3, LSB first) - ported byte-for-byte via
// `eeprom_read_byte()`/`eeprom_write_byte()` at the same 4 addresses with the
// exact same shift/mask arithmetic (see eepromShim.h). This project's own
// fresh-EEPROM-cell default (255/0xFF, matching real factory-erased AVR
// EEPROM - see this project's own CLAUDE.md) reproduces upstream's own real
// fresh-cartridge behavior for free: four all-0xFF bytes decompose to -1
// (32-bit all-ones is -1 in this dialect's only signed integer type), which
// upstream's own `if(bank < 5) bank = 100;` in `startGame()` already treats
// as "no real save yet" and resets to a 100-chip starting bank - the same
// outcome real, never-before-used AVR EEPROM would produce, not a new
// special case added for this port.
//
// No shim gaps were found while porting this game - every primitive used
// here (`gbDrawBitmap`, `gbSetFont`/`gbPrintString`/`gbPrintNumber`,
// `gbPressed`, `gbPlayNote`, `eeprom_read_byte`/`eeprom_write_byte`, `arand`)
// already existed and worked as documented.

#define VPOKER_CARDY 14
#define VPOKER_YHELD 37
#define VPOKER_YWIN 20
#define VPOKER_XWIN 7
#define VPOKER_INSTX 3
#define VPOKER_INSTY 42
#define VPOKER_BANKX 3
#define VPOKER_BANKY 0
#define VPOKER_XOFFSET 3
#define VPOKER_YOFFSET 3
#define VPOKER_FLIPTIME 5
#define VPOKER_WINFLASHTIME 40

#define VPOKER_SUIT_CLUBS 0
#define VPOKER_SUIT_DIAMONDS 1
#define VPOKER_SUIT_HEARTS 2
#define VPOKER_SUIT_SPADES 3

#define VPOKER_CARD_INDECK 0
#define VPOKER_CARD_DRAWN 1
#define VPOKER_CARD_HELD 2
#define VPOKER_CARD_DISCARDED 3

#define VPOKER_FACE_JACK 11
#define VPOKER_FACE_QUEEN 12
#define VPOKER_FACE_KING 13
#define VPOKER_FACE_ACE 14

#define VPOKER_ROUND_BET 0
#define VPOKER_ROUND_DEAL 1
#define VPOKER_ROUND_DRAW 2

#define VPOKER_HAND_JACKSORBETTER 0
#define VPOKER_HAND_TWOPAIR 1
#define VPOKER_HAND_THREEOFAKIND 2
#define VPOKER_HAND_STRAIGHT 3
#define VPOKER_HAND_FLUSH 4
#define VPOKER_HAND_FULLHOUSE 5
#define VPOKER_HAND_FOUROFAKIND 6
#define VPOKER_HAND_STRAIGHTFLUSH 7
#define VPOKER_HAND_ROYALFLUSH 8
#define VPOKER_HAND_NOHAND 9

// -----------------------------------------------------------------------------
//   Real bitmap art (Sprite.h), B-literal bytes converted to decimal - see
//   this file's own header comment.
// -----------------------------------------------------------------------------

int vpokerGameLogo[434]=
{
    72, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 16, 0,
    0, 0, 8, 0, 0, 17, 24, 16, 0, 1, 224, 8,
    0, 0, 19, 0, 48, 0, 1, 176, 24, 0, 0, 18,
    113, 243, 142, 1, 179, 155, 56, 240, 18, 17, 36, 211,
    1, 52, 220, 76, 128, 28, 51, 47, 243, 1, 236, 220,
    253, 128, 28, 51, 108, 50, 3, 12, 150, 193, 0, 28,
    249, 231, 156, 3, 7, 50, 121, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 127, 254, 0, 0, 0, 0, 0, 0, 0, 64,
    2, 0, 0, 0, 0, 0, 0, 0, 72, 2, 0, 0,
    0, 0, 0, 0, 0, 84, 2, 0, 0, 0, 0, 0,
    0, 0, 92, 2, 0, 0, 0, 0, 0, 0, 0, 84,
    2, 96, 0, 128, 66, 0, 0, 0, 84, 2, 140, 205,
    204, 195, 40, 0, 0, 64, 2, 137, 148, 153, 66, 184,
    0, 0, 64, 130, 104, 204, 140, 195, 8, 0, 0, 65,
    194, 0, 0, 0, 0, 48, 0, 0, 67, 226, 0, 0,
    0, 0, 0, 0, 0, 71, 242, 138, 128, 24, 16, 192,
    0, 0, 79, 250, 216, 166, 20, 208, 164, 238, 64, 79,
    250, 170, 204, 21, 144, 202, 68, 160, 70, 178, 138, 166,
    24, 208, 132, 238, 64, 64, 130, 0, 0, 0, 0, 0,
    0, 0, 65, 194, 0, 0, 0, 0, 0, 0, 0, 67,
    226, 0, 0, 0, 0, 0, 0, 0, 64, 2, 0, 0,
    0, 0, 0, 0, 0, 127, 254, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0
};

int vpokerCardBorder[46]=
{
    16, 22, 255, 255, 128, 1, 128, 1, 128, 1, 128, 1,
    128, 1, 128, 1, 128, 1, 128, 1, 128, 1, 128, 1,
    128, 1, 128, 1, 128, 1, 128, 1, 128, 1, 128, 1,
    128, 1, 128, 1, 128, 1, 128, 1, 255, 255
};

int vpokerCardBack[46]=
{
    16, 22, 0, 0, 127, 254, 106, 170, 85, 86, 106, 170,
    85, 86, 106, 170, 85, 86, 106, 170, 85, 86, 106, 170,
    85, 86, 106, 170, 85, 86, 106, 170, 85, 86, 106, 170,
    85, 86, 106, 170, 85, 86, 127, 254, 0, 0
};

int vpokerCardSpade[46]=
{
    16, 22, 0, 0, 127, 254, 64, 2, 64, 2, 64, 2,
    64, 2, 64, 2, 64, 2, 64, 2, 64, 130, 65, 194,
    67, 226, 71, 242, 79, 250, 79, 250, 70, 178, 64, 130,
    65, 194, 67, 226, 64, 2, 127, 254, 0, 0
};

int vpokerCardHeart[46]=
{
    16, 22, 0, 0, 127, 254, 64, 2, 64, 2, 64, 2,
    64, 2, 64, 2, 64, 2, 64, 2, 64, 2, 64, 2,
    71, 114, 79, 250, 79, 250, 79, 250, 71, 242, 67, 226,
    65, 194, 64, 130, 64, 2, 127, 254, 0, 0
};

int vpokerCardDiamond[46]=
{
    16, 22, 0, 0, 127, 254, 64, 2, 64, 2, 64, 2,
    64, 2, 64, 2, 64, 2, 64, 2, 64, 2, 64, 130,
    65, 194, 67, 226, 71, 242, 79, 250, 71, 242, 67, 226,
    65, 194, 64, 130, 64, 2, 127, 254, 0, 0
};

int vpokerCardClub[46]=
{
    16, 22, 0, 0, 127, 254, 64, 2, 64, 2, 64, 2,
    64, 2, 64, 2, 64, 2, 64, 2, 65, 194, 67, 226,
    67, 226, 65, 194, 71, 242, 79, 250, 79, 250, 79, 250,
    70, 178, 65, 194, 64, 2, 127, 254, 0, 0
};

// Real card slot x-positions - upstream's own CARD1XPOS..CARD5XPOS, kept as
// a lookup table instead of a per-index switch (see this file's own header
// comment).
int vpokerCardX[5]= { 2, 18, 34, 50, 66 };

// -----------------------------------------------------------------------------
//   Card model
// -----------------------------------------------------------------------------

typedef struct
{
    int suit;      // VPOKER_SUIT_*
    int value;     // 2-10, or VPOKER_FACE_JACK/QUEEN/KING/ACE
    int state;     // VPOKER_CARD_*
    int flipTimer; // frames left in the flip-in animation
} VpokerCard;

VpokerCard vpokerDeck[52];
int vpokerHandIdx[5];   // index into vpokerDeck, -1 = empty slot (real NULL here - see header comment)
int vpokerHandValue[5]; // sorted copy of the current hand's values, used by the hand-ranking checks

int vpokerCardSelect;
int vpokerBet;
int vpokerLastBet;
int vpokerBank;
int vpokerHandResult;
int vpokerWinTimer;
int vpokerWinAmount;
int vpokerRound;
bool vpokerLockInput;
int vpokerHandInfoPage;

enum VpokerState
{
    VPOKER_STATE_TITLE = 0,
    VPOKER_STATE_PLAY = 1,
    VPOKER_STATE_PAUSE = 2,
    VPOKER_STATE_HANDINFO = 3,
    VPOKER_STATE_GAMEOVER = 4
};

int vpokerState;

// -----------------------------------------------------------------------------
//   Sound - see this file's own header comment on why the slide/tremolo
//   shaping from upstream's own playSound()/Sound.h is dropped, keeping just
//   the real pitch/duration pair from each table.
// -----------------------------------------------------------------------------

void vpokerPlayWin()    { gbPlayNote( 16, 8 ); }
void vpokerPlayLose()   { gbPlayNote( 6, 4 ); }
void vpokerPlayFlip()   { gbPlayNote( 0, 2 ); }
void vpokerPlayHold()   { gbPlayNote( 0, 4 ); }
void vpokerPlayResume() { gbPlayNote( 60, 6 ); }
void vpokerPlayPause()  { gbPlayNote( 55, 6 ); }

void vpokerPlayBet( int pitch )
{
    gbPlayNote( pitch, 4 );
}

// -----------------------------------------------------------------------------
//   Wallet persistence (real upstream EEPROM.read()/write() port - see this
//   file's own header comment).
// -----------------------------------------------------------------------------

void vpokerSaveWallet()
{
    int four = vpokerBank & 0xFF;
    int three = ( vpokerBank >> 8 ) & 0xFF;
    int two = ( vpokerBank >> 16 ) & 0xFF;
    int one = ( vpokerBank >> 24 ) & 0xFF;

    eeprom_write_byte( 0, four );
    eeprom_write_byte( 1, three );
    eeprom_write_byte( 2, two );
    eeprom_write_byte( 3, one );
}

int vpokerLoadWallet()
{
    int four = eeprom_read_byte( 0 );
    int three = eeprom_read_byte( 1 );
    int two = eeprom_read_byte( 2 );
    int one = eeprom_read_byte( 3 );

    return ( ( four << 0 ) & 0xFF ) + ( ( three << 8 ) & 0xFFFF ) + ( ( two << 16 ) & 0xFFFFFF ) + ( ( one << 24 ) & 0xFFFFFFFF );
}

// -----------------------------------------------------------------------------
//   Card/deck helpers
// -----------------------------------------------------------------------------

void vpokerInitCardDeck()
{
    int suit = 0;
    int i;

    for( i = 0; i < 52; i = i + 1 )
    {
        vpokerDeck[ i ].suit = suit;
        vpokerDeck[ i ].value = 2 + ( i % 13 );
        vpokerDeck[ i ].state = VPOKER_CARD_INDECK;
        vpokerDeck[ i ].flipTimer = 0;

        if( ( 2 + ( i % 13 ) ) == 14 )
          suit = suit + 1;
    }
}

void vpokerClearHand()
{
    int i, j;

    vpokerCardSelect = 0;
    vpokerWinTimer = 0;
    vpokerHandResult = VPOKER_HAND_NOHAND;

    for( i = 0; i < 5; i = i + 1 )
      vpokerHandIdx[ i ] = -1;

    for( j = 0; j < 52; j = j + 1 )
    {
        vpokerDeck[ j ].state = VPOKER_CARD_INDECK;
        vpokerDeck[ j ].flipTimer = 0;
    }
}

int vpokerPickRandomCard()
{
    int card;

    while( 1 )
    {
        card = arand( 52 );

        if( vpokerDeck[ card ].state == VPOKER_CARD_INDECK )
        {
            vpokerDeck[ card ].state = VPOKER_CARD_DRAWN;
            return card;
        }
    }

    return -1;
}

void vpokerDealCards()
{
    int i;

    vpokerCardSelect = 0;

    for( i = 0; i < 5; i = i + 1 )
    {
        vpokerHandIdx[ i ] = vpokerPickRandomCard();
        vpokerDeck[ vpokerHandIdx[ i ] ].flipTimer = ( 1 + i ) * VPOKER_FLIPTIME;
    }
}

int vpokerCheckHand();

void vpokerDrawCards()
{
    int i;
    int drawCtr = 1;

    vpokerCardSelect = 0;

    for( i = 0; i < 5; i = i + 1 )
    {
        if( vpokerDeck[ vpokerHandIdx[ i ] ].state == VPOKER_CARD_DRAWN )
        {
            vpokerDeck[ vpokerHandIdx[ i ] ].state = VPOKER_CARD_DISCARDED;
            vpokerHandIdx[ i ] = vpokerPickRandomCard();
            vpokerDeck[ vpokerHandIdx[ i ] ].flipTimer = drawCtr * VPOKER_FLIPTIME;
            drawCtr = drawCtr + 1;
        }
    }

    vpokerHandResult = vpokerCheckHand();
    vpokerSaveWallet();
}

// Replacement for upstream's own `qsort(HandValue, 5, sizeof(int),
// valueCompare)` - see this file's own header comment (no qsort() in this
// dialect's stdlib). A plain insertion sort is exact and trivially fast at
// n=5.
void vpokerSortHandValues()
{
    int i, j, key;

    for( i = 1; i < 5; i = i + 1 )
    {
        key = vpokerHandValue[ i ];
        j = i - 1;

        while( ( j >= 0 ) && ( vpokerHandValue[ j ] > key ) )
        {
            vpokerHandValue[ j + 1 ] = vpokerHandValue[ j ];
            j = j - 1;
        }

        vpokerHandValue[ j + 1 ] = key;
    }
}

// -----------------------------------------------------------------------------
//   Hand ranking - direct ports of upstream's own isXxx()/checkHand()
// -----------------------------------------------------------------------------

bool vpokerIsJacksOrBetter()
{
    int i;

    for( i = 0; i < 4; i = i + 1 )
    {
        if( ( vpokerHandValue[ i ] == vpokerHandValue[ i + 1 ] ) && ( vpokerHandValue[ i ] >= VPOKER_FACE_JACK ) )
          return true;
    }

    return false;
}

bool vpokerIsTwoPair()
{
    if( ( vpokerHandValue[ 0 ] == vpokerHandValue[ 1 ] ) && ( vpokerHandValue[ 2 ] == vpokerHandValue[ 3 ] ) )
      return true;
    if( ( vpokerHandValue[ 1 ] == vpokerHandValue[ 2 ] ) && ( vpokerHandValue[ 3 ] == vpokerHandValue[ 4 ] ) )
      return true;
    if( ( vpokerHandValue[ 0 ] == vpokerHandValue[ 1 ] ) && ( vpokerHandValue[ 3 ] == vpokerHandValue[ 4 ] ) )
      return true;

    return false;
}

bool vpokerIsThreeOfAKind()
{
    int i;

    for( i = 0; i < 3; i = i + 1 )
    {
        if( ( vpokerHandValue[ i ] == vpokerHandValue[ i + 1 ] ) && ( vpokerHandValue[ i + 1 ] == vpokerHandValue[ i + 2 ] ) )
          return true;
    }

    return false;
}

bool vpokerIsStraight()
{
    if( ( vpokerHandValue[ 4 ] == ( vpokerHandValue[ 3 ] + 1 ) ) &&
        ( ( vpokerHandValue[ 3 ] + 1 ) == ( vpokerHandValue[ 2 ] + 2 ) ) &&
        ( ( vpokerHandValue[ 2 ] + 2 ) == ( vpokerHandValue[ 1 ] + 3 ) ) &&
        ( ( vpokerHandValue[ 1 ] + 3 ) == ( vpokerHandValue[ 0 ] + 4 ) ) )
      return true;

    if( ( vpokerHandValue[ 4 ] == VPOKER_FACE_ACE ) && ( vpokerHandValue[ 0 ] == 2 ) && ( vpokerHandValue[ 1 ] == 3 ) &&
        ( vpokerHandValue[ 2 ] == 4 ) && ( vpokerHandValue[ 3 ] == 5 ) )
      return true;

    return false;
}

bool vpokerIsFlush()
{
    int s0 = vpokerDeck[ vpokerHandIdx[ 0 ] ].suit;
    int s1 = vpokerDeck[ vpokerHandIdx[ 1 ] ].suit;
    int s2 = vpokerDeck[ vpokerHandIdx[ 2 ] ].suit;
    int s3 = vpokerDeck[ vpokerHandIdx[ 3 ] ].suit;
    int s4 = vpokerDeck[ vpokerHandIdx[ 4 ] ].suit;

    if( ( s0 == s1 ) && ( s1 == s2 ) && ( s2 == s3 ) && ( s3 == s4 ) )
      return true;

    return false;
}

bool vpokerIsFullHouse()
{
    if( ( vpokerHandValue[ 0 ] == vpokerHandValue[ 1 ] ) && ( vpokerHandValue[ 1 ] == vpokerHandValue[ 2 ] ) && ( vpokerHandValue[ 3 ] == vpokerHandValue[ 4 ] ) )
      return true;
    if( ( vpokerHandValue[ 0 ] == vpokerHandValue[ 1 ] ) && ( vpokerHandValue[ 2 ] == vpokerHandValue[ 3 ] ) && ( vpokerHandValue[ 3 ] == vpokerHandValue[ 4 ] ) )
      return true;

    return false;
}

bool vpokerIsFourOfAKind()
{
    if( ( vpokerHandValue[ 0 ] == vpokerHandValue[ 1 ] ) && ( vpokerHandValue[ 1 ] == vpokerHandValue[ 2 ] ) && ( vpokerHandValue[ 2 ] == vpokerHandValue[ 3 ] ) )
      return true;
    if( ( vpokerHandValue[ 1 ] == vpokerHandValue[ 2 ] ) && ( vpokerHandValue[ 2 ] == vpokerHandValue[ 3 ] ) && ( vpokerHandValue[ 3 ] == vpokerHandValue[ 4 ] ) )
      return true;

    return false;
}

bool vpokerIsStraightFlush()
{
    return ( vpokerIsStraight() && vpokerIsFlush() );
}

bool vpokerIsRoyalFlush()
{
    if( vpokerIsFlush() && ( vpokerHandValue[ 0 ] == 10 ) && ( vpokerHandValue[ 1 ] == VPOKER_FACE_JACK ) &&
        ( vpokerHandValue[ 2 ] == VPOKER_FACE_QUEEN ) && ( vpokerHandValue[ 3 ] == VPOKER_FACE_KING ) && ( vpokerHandValue[ 4 ] == VPOKER_FACE_ACE ) )
      return true;

    return false;
}

int vpokerCheckHand()
{
    int i;

    for( i = 0; i < 5; i = i + 1 )
      vpokerHandValue[ i ] = vpokerDeck[ vpokerHandIdx[ i ] ].value;

    vpokerSortHandValues();

    if( vpokerIsRoyalFlush() )
    {
        vpokerWinAmount = 250 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_ROYALFLUSH;
    }
    if( vpokerIsStraightFlush() )
    {
        vpokerWinAmount = 50 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_STRAIGHTFLUSH;
    }
    if( vpokerIsFourOfAKind() )
    {
        vpokerWinAmount = 25 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_FOUROFAKIND;
    }
    if( vpokerIsFullHouse() )
    {
        vpokerWinAmount = 9 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_FULLHOUSE;
    }
    if( vpokerIsFlush() )
    {
        vpokerWinAmount = 6 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_FLUSH;
    }
    if( vpokerIsStraight() )
    {
        vpokerWinAmount = 4 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_STRAIGHT;
    }
    if( vpokerIsThreeOfAKind() )
    {
        vpokerWinAmount = 3 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_THREEOFAKIND;
    }
    if( vpokerIsTwoPair() )
    {
        vpokerWinAmount = 2 * vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_TWOPAIR;
    }
    if( vpokerIsJacksOrBetter() )
    {
        vpokerWinAmount = vpokerLastBet;
        vpokerBank = vpokerBank + vpokerWinAmount;
        return VPOKER_HAND_JACKSORBETTER;
    }

    return VPOKER_HAND_NOHAND;
}

// -----------------------------------------------------------------------------
//   Game flow
// -----------------------------------------------------------------------------

void vpokerStartGame()
{
    vpokerBank = vpokerLoadWallet();
    if( vpokerBank < 5 )
      vpokerBank = 100;

    vpokerBet = 5;
    vpokerLastBet = 0;
    vpokerLockInput = false;
    vpokerHandResult = VPOKER_HAND_NOHAND;
    vpokerWinTimer = 0;
    vpokerWinAmount = 0;

    vpokerInitCardDeck();
    vpokerClearHand();
    vpokerRound = VPOKER_ROUND_BET;
}

void vpokerBeginTitle()
{
    vpokerState = VPOKER_STATE_TITLE;
}

void vpokerBeginPlay()
{
    vpokerState = VPOKER_STATE_PLAY;
}

// -----------------------------------------------------------------------------
//   Display
// -----------------------------------------------------------------------------

void vpokerDisplayCards()
{
    int i;

    if( ( vpokerCardSelect >= 1 ) && ( vpokerCardSelect <= 5 ) )
      gbDrawBitmap( vpokerCardX[ vpokerCardSelect - 1 ], VPOKER_CARDY, vpokerCardBorder );

    for( i = 0; i < 5; i = i + 1 )
    {
        int x = vpokerCardX[ i ];

        if( vpokerHandIdx[ i ] != -1 )
        {
            if( vpokerDeck[ vpokerHandIdx[ i ] ].flipTimer > 0 )
            {
                vpokerDeck[ vpokerHandIdx[ i ] ].flipTimer = vpokerDeck[ vpokerHandIdx[ i ] ].flipTimer - 1;
                gbDrawBitmap( x, VPOKER_CARDY, vpokerCardBack );
                if( vpokerDeck[ vpokerHandIdx[ i ] ].flipTimer == 1 )
                  vpokerPlayFlip();
                continue;
            }

            gbCursorY = VPOKER_CARDY + VPOKER_YOFFSET;
            gbCursorX = x + VPOKER_XOFFSET;

            if( vpokerDeck[ vpokerHandIdx[ i ] ].value > 10 )
            {
                if( vpokerDeck[ vpokerHandIdx[ i ] ].value == VPOKER_FACE_JACK )
                  gbPrintString( "J" );
                else if( vpokerDeck[ vpokerHandIdx[ i ] ].value == VPOKER_FACE_QUEEN )
                  gbPrintString( "Q" );
                else if( vpokerDeck[ vpokerHandIdx[ i ] ].value == VPOKER_FACE_KING )
                  gbPrintString( "K" );
                else if( vpokerDeck[ vpokerHandIdx[ i ] ].value == VPOKER_FACE_ACE )
                  gbPrintString( "A" );
            }
            else
              gbPrintNumber( vpokerDeck[ vpokerHandIdx[ i ] ].value );

            if( vpokerDeck[ vpokerHandIdx[ i ] ].suit == VPOKER_SUIT_CLUBS )
              gbDrawBitmap( x, VPOKER_CARDY, vpokerCardClub );
            else if( vpokerDeck[ vpokerHandIdx[ i ] ].suit == VPOKER_SUIT_DIAMONDS )
              gbDrawBitmap( x, VPOKER_CARDY, vpokerCardDiamond );
            else if( vpokerDeck[ vpokerHandIdx[ i ] ].suit == VPOKER_SUIT_HEARTS )
              gbDrawBitmap( x, VPOKER_CARDY, vpokerCardHeart );
            else if( vpokerDeck[ vpokerHandIdx[ i ] ].suit == VPOKER_SUIT_SPADES )
              gbDrawBitmap( x, VPOKER_CARDY, vpokerCardSpade );

            if( vpokerDeck[ vpokerHandIdx[ i ] ].state == VPOKER_CARD_HELD )
            {
                gbSetFont( gbFont3x3 );
                gbCursorY = VPOKER_YHELD;
                gbCursorX = x + VPOKER_XOFFSET - 2;
                gbPrintString( "hold" );
                gbSetFont( gbFont3x5 );
            }
        }
        else
          gbDrawBitmap( x, VPOKER_CARDY, vpokerCardBack );
    }
}

void vpokerDisplayWin()
{
    int flipSum = vpokerDeck[ vpokerHandIdx[ 0 ] ].flipTimer + vpokerDeck[ vpokerHandIdx[ 1 ] ].flipTimer +
                  vpokerDeck[ vpokerHandIdx[ 2 ] ].flipTimer + vpokerDeck[ vpokerHandIdx[ 3 ] ].flipTimer +
                  vpokerDeck[ vpokerHandIdx[ 4 ] ].flipTimer;

    if( ( vpokerWinTimer < ( VPOKER_WINFLASHTIME >> 1 ) ) || ( flipSum > 0 ) )
      vpokerDisplayCards();
    else
    {
        gbCursorY = VPOKER_YWIN;
        gbCursorX = VPOKER_XWIN;

        if( vpokerHandResult == VPOKER_HAND_JACKSORBETTER )
          gbPrintString( "Jacks or better!\n" );
        else if( vpokerHandResult == VPOKER_HAND_TWOPAIR )
          gbPrintString( "Two pair!\n" );
        else if( vpokerHandResult == VPOKER_HAND_THREEOFAKIND )
          gbPrintString( "Three of a kind!\n" );
        else if( vpokerHandResult == VPOKER_HAND_STRAIGHT )
          gbPrintString( "Straight!\n" );
        else if( vpokerHandResult == VPOKER_HAND_FLUSH )
          gbPrintString( "Flush!\n" );
        else if( vpokerHandResult == VPOKER_HAND_FULLHOUSE )
          gbPrintString( "Full House!\n" );
        else if( vpokerHandResult == VPOKER_HAND_FOUROFAKIND )
          gbPrintString( "Four of a kind!\n" );
        else if( vpokerHandResult == VPOKER_HAND_STRAIGHTFLUSH )
          gbPrintString( "Straight flush!\n" );
        else if( vpokerHandResult == VPOKER_HAND_ROYALFLUSH )
          gbPrintString( "Royal flush!\n" );

        gbCursorX = VPOKER_XWIN;
        gbCursorY = VPOKER_YWIN + 8;
        gbPrintString( "Bet $" );
        gbPrintNumber( vpokerLastBet );
        gbPrintString( " Won $" );
        gbPrintNumber( vpokerWinAmount );
    }

    vpokerWinTimer = vpokerWinTimer + 1;
    if( vpokerWinTimer > VPOKER_WINFLASHTIME )
    {
        vpokerPlayWin();
        vpokerWinTimer = 0;
    }
}

void vpokerUpdateDisplay()
{
    gbSetColor( 1 );

    gbCursorY = VPOKER_BANKY;
    gbCursorX = VPOKER_BANKX;
    gbPrintString( "Bank: $" );
    gbPrintNumber( vpokerBank );

    gbCursorY = VPOKER_BANKY + 7;
    gbCursorX = VPOKER_BANKX;
    gbPrintString( " Bet: $" );
    gbPrintNumber( vpokerBet );

    if( vpokerHandResult == VPOKER_HAND_NOHAND )
      vpokerDisplayCards();
    else
      vpokerDisplayWin();

    if( vpokerRound == VPOKER_ROUND_BET )
    {
        gbCursorY = VPOKER_INSTY;
        gbCursorX = VPOKER_INSTX;
        gbPrintString( "Up/Down:Bet  B:Deal" );
    }
    else if( vpokerRound == VPOKER_ROUND_DEAL )
    {
        gbCursorY = VPOKER_INSTY;
        gbCursorX = VPOKER_INSTX;
        gbPrintString( "A:Hold  B:Draw" );
    }
}

// -----------------------------------------------------------------------------
//   Input / round logic (upstream's own updateInput()/updateRound())
// -----------------------------------------------------------------------------

void vpokerUpdateInput()
{
    if( vpokerLockInput )
      return;

    if( ( vpokerRound == VPOKER_ROUND_BET ) && gbPressed( BTN_UP ) )
    {
        if( vpokerBet == 5 ) { vpokerBet = 10; vpokerPlayBet( 8 ); }
        else if( vpokerBet == 10 ) { vpokerBet = 25; vpokerPlayBet( 10 ); }
        else if( vpokerBet == 25 ) { vpokerBet = 50; vpokerPlayBet( 12 ); }
        else if( vpokerBet == 50 ) { vpokerBet = 100; vpokerPlayBet( 14 ); }
        else if( vpokerBet == 100 ) { vpokerBet = 5; vpokerPlayBet( 6 ); }

        if( vpokerBet > vpokerBank )
        {
            vpokerBet = 5;
            vpokerPlayBet( 6 );
        }
    }

    if( ( vpokerRound == VPOKER_ROUND_BET ) && gbPressed( BTN_DOWN ) )
    {
        if( vpokerBet == 5 ) { vpokerBet = 100; vpokerPlayBet( 14 ); }
        else if( vpokerBet == 10 ) { vpokerBet = 5; vpokerPlayBet( 6 ); }
        else if( vpokerBet == 25 ) { vpokerBet = 10; vpokerPlayBet( 8 ); }
        else if( vpokerBet == 50 ) { vpokerBet = 25; vpokerPlayBet( 10 ); }
        else if( vpokerBet == 100 ) { vpokerBet = 50; vpokerPlayBet( 12 ); }

        if( vpokerBet > vpokerBank )
        {
            vpokerBet = 5;
            vpokerPlayBet( 6 );
        }
    }

    if( ( vpokerRound == VPOKER_ROUND_DEAL ) && gbPressed( BTN_LEFT ) )
    {
        vpokerCardSelect = vpokerCardSelect - 1;
        if( vpokerCardSelect < 1 )
          vpokerCardSelect = 1;
    }

    if( ( vpokerRound == VPOKER_ROUND_DEAL ) && gbPressed( BTN_RIGHT ) )
    {
        vpokerCardSelect = vpokerCardSelect + 1;
        if( vpokerCardSelect > 5 )
          vpokerCardSelect = 5;
    }

    if( ( vpokerRound == VPOKER_ROUND_DEAL ) && gbPressed( BTN_A ) )
    {
        if( ( vpokerCardSelect > 0 ) && ( vpokerHandIdx[ vpokerCardSelect - 1 ] != -1 ) )
        {
            vpokerPlayHold();
            if( vpokerDeck[ vpokerHandIdx[ vpokerCardSelect - 1 ] ].state == VPOKER_CARD_HELD )
              vpokerDeck[ vpokerHandIdx[ vpokerCardSelect - 1 ] ].state = VPOKER_CARD_DRAWN;
            else
              vpokerDeck[ vpokerHandIdx[ vpokerCardSelect - 1 ] ].state = VPOKER_CARD_HELD;
        }
    }

    if( gbPressed( BTN_B ) )
    {
        if( vpokerRound == VPOKER_ROUND_BET )
        {
            vpokerClearHand();
            vpokerBank = vpokerBank - vpokerBet;
            vpokerLastBet = vpokerBet;
            vpokerWinAmount = 0;
            vpokerDealCards();
            vpokerRound = VPOKER_ROUND_DEAL;
        }
        else if( vpokerRound == VPOKER_ROUND_DEAL )
        {
            vpokerRound = VPOKER_ROUND_DRAW;
            vpokerDrawCards();
        }
    }

    if( gbPressed( BTN_C ) )
    {
        vpokerPlayPause();
        vpokerState = VPOKER_STATE_PAUSE;
    }
}

void vpokerUpdateRound()
{
    if( ( vpokerBank < 5 ) && ( vpokerRound == VPOKER_ROUND_BET ) )
    {
        vpokerPlayLose();
        vpokerState = VPOKER_STATE_GAMEOVER;
        return;
    }

    if( vpokerBet > vpokerBank )
      vpokerBet = 5;

    if( vpokerRound == VPOKER_ROUND_DRAW )
    {
        vpokerLockInput = true;

        if( ( vpokerDeck[ vpokerHandIdx[ 0 ] ].flipTimer <= 0 ) && ( vpokerDeck[ vpokerHandIdx[ 1 ] ].flipTimer <= 0 ) &&
            ( vpokerDeck[ vpokerHandIdx[ 2 ] ].flipTimer <= 0 ) && ( vpokerDeck[ vpokerHandIdx[ 3 ] ].flipTimer <= 0 ) &&
            ( vpokerDeck[ vpokerHandIdx[ 4 ] ].flipTimer <= 0 ) )
        {
            vpokerLockInput = false;
            vpokerRound = VPOKER_ROUND_BET;
        }
    }

    if( vpokerRound == VPOKER_ROUND_DEAL )
    {
        vpokerLockInput = true;

        if( ( vpokerDeck[ vpokerHandIdx[ 0 ] ].flipTimer <= 0 ) && ( vpokerDeck[ vpokerHandIdx[ 1 ] ].flipTimer <= 0 ) &&
            ( vpokerDeck[ vpokerHandIdx[ 2 ] ].flipTimer <= 0 ) && ( vpokerDeck[ vpokerHandIdx[ 3 ] ].flipTimer <= 0 ) &&
            ( vpokerDeck[ vpokerHandIdx[ 4 ] ].flipTimer <= 0 ) )
          vpokerLockInput = false;
    }
}

void vpokerUpdatePlay()
{
    vpokerUpdateInput();
    if( vpokerState != VPOKER_STATE_PLAY )
      return;

    vpokerUpdateRound();
    if( vpokerState != VPOKER_STATE_PLAY )
      return;

    vpokerUpdateDisplay();
}

// -----------------------------------------------------------------------------
//   Title / pause / hand-info / game-over states - see this file's own
//   header comment on why these are real states now instead of upstream's
//   own blocking while(1) loops.
// -----------------------------------------------------------------------------

void vpokerUpdateTitle()
{
    gbSetFont( gbFont3x5 ); // defensive - matches gameConduit.c's own precedent
    gbSetColor( 1 );
    gbDrawBitmap( ( LCDWIDTH - 72 ) / 2, 0, vpokerGameLogo );

    gbCursorX = 28;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbPickRandomSeed(); // no-op, see gamebuinoShim.h
        vpokerStartGame();
        vpokerBeginPlay();
    }
}

void vpokerUpdatePause()
{
    gbSetColor( 1 );

    gbCursorY = 18;
    gbCursorX = 20;
    gbPrintString( "P A U S E D\n" );

    gbCursorY = 30;
    gbPrintString( "A: Title Screen\n" );
    gbPrintString( "B: Show Hand Info\n" );
    gbPrintString( "C: Resume Game\n" );

    if( gbPressed( BTN_A ) )
    {
        gbBegin(); // real upstream setup() re-run - see this file's own header comment
        vpokerBeginTitle();
        return;
    }

    if( gbPressed( BTN_C ) )
    {
        vpokerPlayResume();
        vpokerState = VPOKER_STATE_PLAY;
        return;
    }

    if( gbPressed( BTN_B ) )
    {
        vpokerHandInfoPage = 0;
        vpokerState = VPOKER_STATE_HANDINFO;
    }
}

void vpokerUpdateHandInfo()
{
    gbSetColor( 1 );
    gbCursorY = 0;
    gbCursorX = 0;

    if( vpokerHandInfoPage == 0 )
    {
        gbPrintString( "Royal Flush:250*Bet\n" );
        gbPrintString( "Straight Flush:50*Bet\n" );
        gbPrintString( "Four of a Kind:25*Bet\n" );
        gbPrintString( "Full House:9*Bet\n" );
        gbPrintString( "Flush:6*Bet\n" );
        gbPrintString( "\n" );
        gbPrintString( "A: Next Page\n" );
        gbPrintString( "C: Back\n" );
    }
    else
    {
        gbPrintString( "Straight:4*Bet\n" );
        gbPrintString( "Three of a Kind:3*Bet\n" );
        gbPrintString( "Two Pair:2*Bet\n" );
        gbPrintString( "Jacks or Better:1*Bet\n" );
        gbPrintString( "\n" );
        gbPrintString( "\n" );
        gbPrintString( "A: Prev Page\n" );
        gbPrintString( "C: Back\n" );
    }

    if( gbPressed( BTN_A ) )
      vpokerHandInfoPage = 1 - vpokerHandInfoPage;

    if( gbPressed( BTN_C ) )
      vpokerState = VPOKER_STATE_PAUSE;
}

void vpokerUpdateGameOver()
{
    gbSetColor( 1 );

    gbCursorY = 18;
    gbCursorX = 10;
    gbPrintString( "G A M E  O V E R\n" );

    gbCursorY = 30;
    gbPrintString( "A: Title Screen\n" );
    gbPrintString( "B: New Game\n" );

    if( gbPressed( BTN_A ) )
    {
        gbBegin(); // real upstream setup() re-run - see this file's own header comment
        vpokerBeginTitle();
        return;
    }

    if( gbPressed( BTN_B ) )
    {
        vpokerStartGame();
        vpokerBeginPlay();
    }
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameVideoPoker_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 ); // real hardware's own default font - see this file's own header comment
    vpokerBeginTitle();
}

void gameVideoPoker_update()
{
    if( !gbUpdate() ) return;

    if( vpokerState == VPOKER_STATE_TITLE ) vpokerUpdateTitle();
    else if( vpokerState == VPOKER_STATE_PLAY ) vpokerUpdatePlay();
    else if( vpokerState == VPOKER_STATE_PAUSE ) vpokerUpdatePause();
    else if( vpokerState == VPOKER_STATE_HANDINFO ) vpokerUpdateHandInfo();
    else if( vpokerState == VPOKER_STATE_GAMEOVER ) vpokerUpdateGameOver();

    gbRenderFrame();
}
