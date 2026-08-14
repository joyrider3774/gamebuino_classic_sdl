#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Senet (Maximilian Timmerkamp, Apache License 2.0). A real ancient-Egyptian
// board game: two players race their own pieces around a 3x10 spiral of 30
// squares using a real 4-paddle throw (0/1/2/3/4 white sides up, with 0
// silently promoted to a "6"), with real attack/defence/blockade/trap rules
// exactly as documented in this port's own HELP pages.
//
// Ported a second time here, from the sibling gamebuino_classic_vircon32
// build's own gameSenet.c (which already did the real upstream-Arduino-to-
// Vircon32-dialect porting work - the singleton senet_state_t/senet_move_t
// flattening to plain globals, the real I2C multiplayer drop, the dropped
// getDefaultName()/keyboard() name entry, the local senetPrint()/
// senetPrintln() textwrap substitute, the low-ASCII icon glyph decoding, and
// every real preserved/normalized upstream quirk - see that file's own
// header comment for the full original write-up, none of which needed
// revisiting here) - converting Vircon32's own C dialect back to standard C:
//  - `int[N] name;`/`int[N] name = {...};` -> `int name[N];`/`int
//    name[N] = {...};` (array-declaration order) for every numeric bitmap
//    table (senetTileXxx/senetPiecePlayerN/senetLogoBitmap) - genuinely
//    numeric byte data, not text, so it stays `int[]`.
//  - Text-holding arrays became real `char[]`/`char*`: senetNamePlayer1/2
//    (plain name strings), senetTextCancelHint/OkHint/CloseHint/Controls
//    (explicit decimal-code initializer lists mixing low-ASCII icon glyphs
//    with printable text - a plain char-array initializer list holds these
//    exactly as well as the Vircon32 int[] convention did, no string-literal
//    parsing needed for the non-printable icon codes), and senetHelp0..9
//    (plain string literals). senetHelpPages (`int*[N]` -> `char*[N]`) and
//    senetPrint()/senetPrintln() (`int* text` -> `char* text`) follow suit.
// Every numeric bitmap table's own real byte data was copied directly from
// the source file, not retyped from memory, and diff-verified byte-for-byte
// against it afterward (see this project's own CLAUDE.md).

#define SENET_BOARD_SIZE  30
#define SENET_BOARD_BYTES 8

#define SENET_MIN_SQUARE 0
#define SENET_MAX_SQUARE 29

#define SENET_SQUARE_REPEATING_LIFE 14
#define SENET_SQUARE_V              25
#define SENET_SQUARE_WATER          26
#define SENET_SQUARE_III            27
#define SENET_SQUARE_II             28
#define SENET_SQUARE_I              29

#define SENET_PLAYER_NONE 0
#define SENET_PLAYER1     1
#define SENET_PLAYER2     2

#define SENET_PIECE_COUNT_MIN 3
#define SENET_PIECE_COUNT_MAX 7

#define SENET_GAMEMODE_SINGLE   0
#define SENET_GAMEMODE_MULTI    1
#define SENET_GAMEMODES_LENGTH  2
#define SENET_MODE_STR_LEN      11

// Real Gamebuino low-ASCII icon glyphs used by this game, decoded directly
// from font3x5.c (see this file's own header comment above).
#define SENET_ICON_ARROW_RIGHT  16 // "\x10" - upstream's own real ">" icon
#define SENET_ICON_ARROW_LEFT   17 // "\x11" - upstream's own real "<" icon
#define SENET_ICON_BTN_A        21 // "\x15"
#define SENET_ICON_BTN_B        22 // "\x16"
#define SENET_ICON_BTN_C        23 // "\x17"
#define SENET_ICON_DPAD_UP      24 // "\x18"
#define SENET_ICON_DPAD_DOWN    25 // "\x19"
#define SENET_ICON_DPAD_LEFT    26 // "\x1A"
#define SENET_ICON_DPAD_RIGHT   27 // "\x1B"
#define SENET_ICON_CURSOR_OPEN  30 // "\x1e"
#define SENET_ICON_CURSOR_CLOSE 31 // "\x1f"

#define SENET_APP_TITLE         0
#define SENET_APP_MAIN_MENU     1
#define SENET_APP_GAME_SETUP    2
#define SENET_APP_PLAYING       3
#define SENET_APP_MULTI_HANDOFF 4
#define SENET_APP_WINNER        5
#define SENET_APP_HELP          6
#define SENET_APP_CONTROLS      7

#define SENET_HELP_PAGE_COUNT 10

// -----------------------------------------------------------------------
// Real board state (the singleton `senet_state_t game_state`/
// `senet_move_t current_move` upstream always passed by pointer - see this
// file's own header comment on why this port keeps them as plain globals
// instead)
// -----------------------------------------------------------------------

int senetBoard[ SENET_BOARD_BYTES ];
int senetTurnCount;
int senetCurrentPlayer;
bool senetTurnFinished;
int senetPiecesPerPlayer;
int senetPiecesOffBoard[ 3 ]; // indexed by SENET_PLAYER1/SENET_PLAYER2

int senetMoveStart;
int senetMoveTarget;
int senetMoveMoves;
int senetMovePlayer;

// -----------------------------------------------------------------------
// Real session/UI state
// -----------------------------------------------------------------------

int senetAppState;
int senetMenuIndex;
int senetHelpPage;

int senetGameMode;
int senetSetupPieces;
bool senetGameStarted;
bool senetFirstMove;
bool senetRoundStarted;
int senetSelectedSquare;
int senetPlayer1; // which internal player number (1 or 2) is human/"P1"
int senetPlayer2;
int senetPreviousPlayer; // hot-seat: whose handoff screen was last shown
int senetMoveTimer;
int senetDisplayMovesTimer;
int senetBtnDownHeld;
bool senetHandoffConfirmed;

char senetNamePlayer1[ 8 ] = "Player1";
char senetNamePlayer2[ 8 ] = "Player2";

// -----------------------------------------------------------------------
// Real upstream bitmap tables (tiles.ino), converted from B-binary
// literals to decimal - byte layout itself is already this shim's own
// native gbDrawBitmap() format (width, height, then row-major MSB-first
// bytes)
// -----------------------------------------------------------------------

int senetTileRepeatingLife[ 9 ] =
{
    7, 7, 146, 40, 40, 16, 56, 18, 146
};

int senetTileSquareV[ 9 ] =
{
    7, 7, 130, 40, 124, 40, 84, 40, 130
};

int senetTileSquareWater1[ 9 ] =
{
    7, 7, 130, 36, 88, 0, 52, 72, 130
};

int senetTileSquareWater2[ 9 ] =
{
    7, 7, 130, 72, 52, 0, 80, 44, 130
};

int senetTileSquareIii[ 9 ] =
{
    7, 7, 130, 84, 84, 84, 84, 84, 130
};

int senetTileSquareIi[ 9 ] =
{
    7, 7, 130, 40, 40, 40, 40, 40, 130
};

int senetTileSquareI[ 9 ] =
{
    7, 7, 130, 16, 16, 16, 16, 16, 130
};

int senetPiecePlayer1[ 9 ] =
{
    7, 7, 0, 68, 124, 40, 68, 124, 0
};

int senetPiecePlayer2[ 9 ] =
{
    7, 7, 0, 84, 124, 40, 40, 56, 0
};

// Real title-screen logo (64x30 - Senet in hieroglyphs and some pieces)
int senetLogoBitmap[ 242 ] =
{
    64, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 108, 0, 0, 0, 0, 0, 63, 255,
    147, 255, 248, 0, 124, 0, 63, 255, 147, 255, 248, 1,
    255, 0, 0, 0, 108, 0, 0, 3, 131, 128, 0, 0,
    0, 0, 0, 7, 1, 192, 0, 0, 0, 0, 0, 12,
    0, 96, 0, 0, 0, 0, 0, 24, 0, 48, 0, 0,
    0, 0, 0, 24, 0, 48, 4, 8, 16, 32, 64, 48,
    0, 24, 14, 28, 56, 112, 224, 48, 0, 24, 27, 54,
    108, 217, 176, 63, 255, 248, 49, 227, 199, 143, 24, 63,
    255, 248, 32, 193, 131, 6, 8, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 2, 0, 0, 128, 0, 16,
    0, 0, 7, 0, 1, 192, 0, 56, 0, 0, 7, 0,
    1, 192, 0, 56, 0, 0, 15, 128, 3, 224, 0, 124,
    0, 0, 7, 0, 1, 192, 0, 56, 0, 0, 7, 31,
    241, 195, 254, 56, 255, 128, 7, 15, 225, 193, 252, 56,
    127, 0, 15, 135, 195, 224, 248, 124, 62, 0, 15, 135,
    195, 224, 248, 124, 62, 0, 15, 135, 195, 224, 248, 124,
    62, 0, 15, 143, 227, 225, 252, 124, 127, 0, 31, 223,
    247, 243, 254, 254, 255, 128, 0, 0, 0, 0, 0, 0,
    0, 0
};

// -----------------------------------------------------------------------
// Text needing a real low-ASCII icon glyph - built as explicit char[]
// arrays of decimal ASCII/glyph codes (see this file's own header comment)
// -----------------------------------------------------------------------

char senetTextCancelHint[ 9 ] =
{
    SENET_ICON_BTN_B, 58, 67, 97, 110, 99, 101, 108, 0 // "\x16:Cancel"
};

char senetTextOkHint[ 5 ] =
{
    79, 75, 58, SENET_ICON_BTN_A, 0 // "OK:\x15"
};

char senetTextCloseHint[ 8 ] =
{
    67, 108, 111, 115, 101, 58, SENET_ICON_BTN_A, 0 // "Close:\x15"
};

char senetTextControls[ 105 ] =
{
    32, 32, 45, 61, 45, 32, 67, 79, 78, 84, 82, 79, 76, 83, 32, 45,
    61, 45, 10, SENET_ICON_BTN_A, 58, 32, 109, 111, 118, 101, 32, 112, 105, 101, 99, 101,
    10, SENET_ICON_DPAD_UP, SENET_ICON_DPAD_DOWN, SENET_ICON_DPAD_LEFT, SENET_ICON_DPAD_RIGHT, 58, 32, 109, 111, 118, 101, 32, 115, 101, 108, 101,
    99, 116, 105, 111, 110, 10, 32, 104, 111, 108, 100, 32, SENET_ICON_DPAD_DOWN, 32, 116, 111,
    32, 103, 101, 116, 32, 110, 101, 120, 116, 32, 112, 111, 115, 115, 105, 98,
    108, 101, 32, 109, 111, 118, 101, 10, SENET_ICON_BTN_C, 58, 32, 98, 97, 99, 107, 32,
    116, 111, 32, 109, 101, 110, 117, 10, 0
    // "  -=- CONTROLS -=-\n\x15: move piece\n\x18\x19\x1A\x1B: move
    //  selection\n hold \x19 to get next possible move\n\x17: back to
    //  menu\n"
};

// -----------------------------------------------------------------------
// Real help text (main_menu.ino's own HELP_PAGE0..9), plain string
// literals - all printable ASCII, no icon glyphs involved
// -----------------------------------------------------------------------

char senetHelp0[ 154 ] = " -=- SENET INTRO -=-\nSenet is an old game played by everyone in ancient Egypt. Using proper boards and pieces or just the street and some stone and wood.";
char senetHelp1[ 140 ] = "START: Each player gets 3-7 pieces placed alternating starting at square 1.\nMOVE: 4 two-sided paddles are thrown to determine the player's ";
char senetHelp2[ 145 ] = "moves for one piece.\nMoving direction:\n|1|>|>|>|>|>|>|>|>|v||v|<|<|<|<|O|<|<|<|<||>|>|>|>|>|V|~|>|>|I|~: Trap; O: House of Rebirth; I: sq30 Goal";
char senetHelp3[ 162 ] = " white +      +throw\n sides +moves +again\n   1   |   1  | Yes\n   2   |   2  | No\n   3   |   3  | No\n   4   |   4  | Yes\n   0   |   6  | Yes\n(4x black => 6 moves)";
char senetHelp4[ 154 ] = "You move again if throwing a 1, 4 or 0.\nATTACK: Landing on an opponents' piece is an attack,and you exchange places; you may not land on your own pieces.";
char senetHelp5[ 119 ] = "RESTRICTIONS:\n-First move must move the piece on highest square.\n-Safe squares: 15, 26, 28 and 29 cannot be attacked.\n";
char senetHelp6[ 139 ] = "-Defence: Two or more opponent pieces in a row cannot be attacked.\n-Blockade: Three or more opponent pieces in a row cannot be passed; -> ";
char senetHelp7[ 132 ] = " however blockades do not  turn around corners.\n-Trap: Land on square 20 means moving back to the first empty square before sq.16.\n";
char senetHelp8[ 165 ] = "-Exit: You may not move past 30. A piece on 30 can be removed at START of your turn if all your pieces are out of first row.\n-No Move: If you cannot move foreward, ";
char senetHelp9[ 138 ] = " you must move backward (according to the same rules). If no move is possible, your turn ends.\n\nWIN: You win by removing all your pieces.";

char* senetHelpPages[ SENET_HELP_PAGE_COUNT ] =
{
    senetHelp0, senetHelp1, senetHelp2, senetHelp3, senetHelp4,
    senetHelp5, senetHelp6, senetHelp7, senetHelp8, senetHelp9
};

// -----------------------------------------------------------------------
// Real print()-with-auto-wrap replacement (see this file's own header
// comment - a direct port of real Display::write()'s per-character loop
// with textWrap forced on, matching real hardware's own default and every
// real print() call site in this game)
// -----------------------------------------------------------------------

void senetPrint( char* text )
{
    int i = 0;

    while( text[ i ] != 0 )
    {
        if( text[ i ] == 10 )
        {
            gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
            gbCursorX = 0;
        }
        else
        {
            gbDrawChar( text[ i ], gbCursorX, gbCursorY );
            gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
            if( gbCursorX > ( LCDWIDTH - gbFontSize * gbFontWidth ) )
            {
                gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
                gbCursorX = 0;
            }
        }
        i = i + 1;
    }
}

void senetPrintln( char* text )
{
    senetPrint( text );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

// -----------------------------------------------------------------------
// Real board rules (SenetGame.cpp), ported onto the singleton globals
// above - direct, bottom-up ports of every real senet_*() free function,
// in dependency order
// -----------------------------------------------------------------------

int senetGetEnemy( int player )
{
    if( player == SENET_PLAYER1 ) return SENET_PLAYER2;
    if( player == SENET_PLAYER2 ) return SENET_PLAYER1;
    return SENET_PLAYER_NONE;
}

bool senetIsPastCornerSquare( int square )
{
    return ( square == 10 ) || ( square == 20 );
}

bool senetIsProtectedSquare( int square )
{
    return ( square == 14 ) || ( square == 25 ) || ( square == 27 ) || ( square == 28 );
}

void senetSetPiece( int square, int piece )
{
    senetBoard[ square / 4 ] = ( ( piece & 0x3 ) << ( 2 * ( square % 4 ) ) ) |
        ( senetBoard[ square / 4 ] & ~( 0x3 << ( 2 * ( square % 4 ) ) ) );
}

int senetGetPiece( int square )
{
    return ( senetBoard[ square / 4 ] >> ( 2 * ( square % 4 ) ) ) & 0x3;
}

bool senetIsBlocked()
{
    int i, start, end, count;
    count = 0;

    if( senetMoveMoves > 0 )
    {
        start = gbMax( SENET_MIN_SQUARE, senetMoveStart + 1 );
        end = gbMin( SENET_MAX_SQUARE, senetMoveStart + senetMoveMoves );
    }
    else
    {
        start = gbMax( SENET_MIN_SQUARE, senetMoveStart - 1 );
        end = gbMin( SENET_MAX_SQUARE, senetMoveStart + senetMoveMoves );
    }

    for( i = start; i <= end; i = i + 1 )
    {
        if( senetIsPastCornerSquare( i ) ) count = 0;

        if( senetGetPiece( i ) == senetGetEnemy( senetCurrentPlayer ) )
        {
            count = count + 1;
            if( count >= 3 ) return true;
        }
        else
        {
            count = 0;
        }
    }

    return false;
}

bool senetCanAttack( int square, int player )
{
    int i, start, end, count;
    count = 0;

    if( senetGetPiece( square ) == SENET_PLAYER_NONE ) return true;
    if( senetGetPiece( square ) == player ) return false;
    if( senetIsProtectedSquare( square ) ) return false;

    start = gbMax( SENET_MIN_SQUARE, square - 1 );
    end = gbMin( SENET_MAX_SQUARE, square + 1 );

    for( i = start; i <= end; i = i + 1 )
    {
        if( senetGetPiece( i ) == senetGetEnemy( senetCurrentPlayer ) )
        {
            count = count + 1;
            if( count >= 2 ) return false;
        }
        else
        {
            count = 0;
        }
    }

    return true;
}

bool senetIsValidMove()
{
    int target;

    // First move of the whole game must move the piece on the highest
    // starting square (real upstream's own real "first move" restriction).
    if( senetTurnCount == 0 )
    {
        return senetMoveStart == SENET_MIN_SQUARE + 2 * senetPiecesPerPlayer - 1;
    }

    target = senetMoveStart + senetMoveMoves;
    return ( target <= SENET_MAX_SQUARE ) && ( target >= SENET_MIN_SQUARE ) &&
        ( senetGetPiece( target ) != senetMovePlayer ) &&
        senetCanAttack( target, senetMovePlayer ) &&
        !senetIsBlocked();
}

// See this file's own header comment - real square-14-down-to-0 search for
// an empty square to relocate a piece trapped on the real water square
// (square 26) into. Exits harmlessly (leaving the piece un-moved) if no
// empty square exists in that range, rather than reproducing real
// hardware's own unsigned-underflow infinite loop for that unreachable-in-
// practice case.
void senetHandleHouseOfWater()
{
    int square;
    for( square = SENET_SQUARE_REPEATING_LIFE; square >= SENET_MIN_SQUARE; square = square - 1 )
    {
        if( senetGetPiece( square ) == SENET_PLAYER_NONE )
        {
            senetSetPiece( square, senetGetPiece( SENET_SQUARE_WATER ) );
            senetSetPiece( SENET_SQUARE_WATER, SENET_PLAYER_NONE );
            senetMoveTarget = square;
            return;
        }
    }
}

bool senetDoMove()
{
    int targetPiece;

    if( senetIsValidMove() )
    {
        senetMoveTarget = senetMoveStart + senetMoveMoves;

        targetPiece = senetGetPiece( senetMoveTarget );
        senetSetPiece( senetMoveTarget, senetMovePlayer );
        senetSetPiece( senetMoveStart, targetPiece );

        senetTurnFinished = true;

        if( senetMoveTarget == SENET_SQUARE_WATER )
        {
            senetHandleHouseOfWater();
        }
    }
    else
    {
        senetTurnFinished = false;
    }

    return senetTurnFinished;
}

void senetUndoMove()
{
    int targetPiece = senetGetPiece( senetMoveTarget );
    senetSetPiece( senetMoveTarget, senetGetPiece( senetMoveStart ) );
    senetSetPiece( senetMoveStart, targetPiece );

    senetTurnFinished = false;
}

bool senetRemovePieceFromSquare30()
{
    int square;

    if( !senetTurnFinished && ( senetGetPiece( SENET_SQUARE_I ) == senetCurrentPlayer ) )
    {
        for( square = SENET_MIN_SQUARE; square < 10; square = square + 1 )
        {
            if( senetGetPiece( square ) == senetCurrentPlayer )
            {
                return false;
            }
        }

        senetSetPiece( SENET_SQUARE_I, SENET_PLAYER_NONE );
        senetPiecesOffBoard[ senetCurrentPlayer ] = senetPiecesOffBoard[ senetCurrentPlayer ] + 1;
        return true;
    }

    return false;
}

bool senetHasWon( int player )
{
    return senetPiecesOffBoard[ player ] == senetPiecesPerPlayer;
}

bool senetSearchNextPossibleMove()
{
    int oldStart = senetMoveStart;
    int square;

    for( square = senetMoveStart; square <= SENET_MAX_SQUARE; square = square + 1 )
    {
        if( senetGetPiece( square ) == senetMovePlayer )
        {
            senetMoveStart = square;
            if( senetIsValidMove() )
            {
                return true;
            }
        }
    }

    senetMoveStart = oldStart;
    return false;
}

bool senetSearchPossibleMove()
{
    int oldStart = senetMoveStart;

    senetMoveStart = SENET_MIN_SQUARE;
    if( senetSearchNextPossibleMove() ) return true;

    senetMoveMoves = -senetMoveMoves;
    if( senetSearchNextPossibleMove() ) return true;

    senetMoveStart = oldStart;
    senetMoveMoves = -senetMoveMoves;
    senetTurnFinished = true;

    return false;
}

int senetThrowPaddles()
{
    int count = 0;
    int i;

    for( i = 0; i < 4; i = i + 1 )
    {
        if( arand( 2 ) == 1 ) count = count + 1;
    }

    if( count == 0 ) count = 6;
    return count;
}

void senetBeginTurn()
{
    if( senetTurnFinished )
    {
        senetMoveMoves = senetThrowPaddles();
        senetMoveStart = 0;
        senetMovePlayer = senetCurrentPlayer;

        senetTurnFinished = false;
    }
}

void senetEndTurn()
{
    if( !senetTurnFinished ) return;

    if( senetMoveMoves == 6 || senetMoveMoves == 1 || senetMoveMoves == 4 )
    {
        // Throwing 1, 4, or 6 grants an extra turn - current player is
        // unchanged (matches real upstream's own empty case bodies).
    }
    else
    {
        senetCurrentPlayer = senetGetEnemy( senetCurrentPlayer );
    }

    senetTurnCount = senetTurnCount + 1;
}

void senetInitState( int pieces )
{
    int i;
    for( i = SENET_MIN_SQUARE; i < SENET_MIN_SQUARE + 2 * pieces; )
    {
        senetSetPiece( i, SENET_PLAYER1 );
        i = i + 1;
        senetSetPiece( i, SENET_PLAYER2 );
        i = i + 1;
    }

    for( i = 2 * pieces; i <= SENET_MAX_SQUARE; i = i + 1 )
    {
        senetSetPiece( i, SENET_PLAYER_NONE );
    }

    senetTurnCount = 0;
    senetTurnFinished = true;
    senetCurrentPlayer = SENET_PLAYER2;

    senetPiecesPerPlayer = pieces;
    senetPiecesOffBoard[ SENET_PLAYER1 ] = 0;
    senetPiecesOffBoard[ SENET_PLAYER2 ] = 0;
}

// -----------------------------------------------------------------------
// Real CPU opponent (SenetGameAI.cpp) - a greedy, one-ply "try every legal
// move, keep the one with the best real board-value delta" search
// -----------------------------------------------------------------------

int senetStateValue( int player )
{
    int result = 0;
    int lastPiece = SENET_PLAYER_NONE;
    int piecesInRow = 0;
    int sq, value;

    for( sq = SENET_MIN_SQUARE; sq <= SENET_MAX_SQUARE; sq = sq + 1 )
    {
        if( ( senetGetPiece( sq ) == lastPiece ) && ( lastPiece != SENET_PLAYER_NONE ) &&
            !senetIsPastCornerSquare( sq ) )
        {
            piecesInRow = piecesInRow + 1;
        }
        else
        {
            piecesInRow = 0;
        }
        lastPiece = senetGetPiece( sq );

        if( sq == 14 || sq == 25 || sq == 27 || sq == 28 )
        {
            value = 3 * sq;
        }
        else if( sq == 29 )
        {
            value = 10 * sq;
        }
        else
        {
            value = sq;
        }

        if( piecesInRow > 1 )
        {
            value = value * piecesInRow;
        }

        if( lastPiece == player )
        {
            result = result + value;
        }
        else if( lastPiece == senetGetEnemy( player ) )
        {
            result = result - value;
        }
    }
    return result;
}

bool senetGetBestMove()
{
    int value, bestValue, bestStart;

    if( senetSearchPossibleMove() )
    {
        bestValue = -( 1 << 15 );
        bestStart = senetMoveStart; // defensive default - see header comment

        do
        {
            senetDoMove();
            value = senetStateValue( senetCurrentPlayer ) - senetStateValue( senetGetEnemy( senetCurrentPlayer ) );

            if( value > bestValue )
            {
                bestValue = value;
                bestStart = senetMoveStart;
            }

            senetUndoMove();
            senetMoveStart = senetMoveStart + 1;
        }
        while( senetSearchNextPossibleMove() );

        senetMoveStart = bestStart;
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------
// Drawing (Senet.ino's own draw_board()/draw_game_state()/
// draw_player_name(), plus their real coordinate-math helpers)
// -----------------------------------------------------------------------

void senetGetSquareCoordinates( int square, int* x, int* y )
{
    square = square % SENET_BOARD_SIZE;

    if( square < 10 )
    {
        *y = 10;
        *x = 2 + 8 * square;
    }
    else if( square < 20 )
    {
        *y = 18;
        *x = 74 - 8 * ( square - 10 );
    }
    else
    {
        *y = 26;
        *x = 2 + 8 * ( square - 20 );
    }
}

int senetRowColToSquare( int row, int col )
{
    row = row % 3;
    col = col % 10;

    if( row == 1 ) col = 9 - col;
    return row * 10 + col;
}

void senetSquareToRowCol( int square, int* row, int* col )
{
    *row = square / 10;
    *col = square % 10;
    if( *row == 1 ) *col = 9 - *col;
}

void senetDrawField( int square, int* bitmap )
{
    int x, y;
    senetGetSquareCoordinates( square, &x, &y );
    gbDrawBitmap( x, y, bitmap );
}

void senetDrawBoard()
{
    int row, column, square, x, y, i, start;

    for( row = 9; row <= 33; row = row + 8 )
    {
        gbDrawFastHLine( 1, row, 81 );
    }
    for( column = 1; column <= 81; column = column + 8 )
    {
        gbDrawFastVLine( column, 10, 24 );
    }

    for( square = SENET_MIN_SQUARE; square <= SENET_MAX_SQUARE; square = square + 1 )
    {
        switch( senetGetPiece( square ) )
        {
            case SENET_PLAYER_NONE:
                switch( square )
                {
                    case SENET_SQUARE_REPEATING_LIFE:
                        senetDrawField( square, senetTileRepeatingLife );
                        break;
                    case SENET_SQUARE_V:
                        senetDrawField( square, senetTileSquareV );
                        break;
                    case SENET_SQUARE_WATER:
                        if( gbFrameCount % 20 > 9 ) senetDrawField( square, senetTileSquareWater1 );
                        else senetDrawField( square, senetTileSquareWater2 );
                        break;
                    case SENET_SQUARE_III:
                        senetDrawField( square, senetTileSquareIii );
                        break;
                    case SENET_SQUARE_II:
                        senetDrawField( square, senetTileSquareIi );
                        break;
                    case SENET_SQUARE_I:
                        senetDrawField( square, senetTileSquareI );
                        break;
                }
                break;
            case SENET_PLAYER1:
                senetDrawField( square, senetPiecePlayer1 );
                break;
            case SENET_PLAYER2:
                senetDrawField( square, senetPiecePlayer2 );
                break;
        }

        switch( square )
        {
            case SENET_SQUARE_REPEATING_LIFE:
            case SENET_SQUARE_V:
            case SENET_SQUARE_WATER:
            case SENET_SQUARE_III:
            case SENET_SQUARE_II:
            case SENET_SQUARE_I:
                senetGetSquareCoordinates( square, &x, &y );
                gbDrawPixel( x, y );
                gbDrawPixel( x + 6, y );
                gbDrawPixel( x, y + 6 );
                gbDrawPixel( x + 6, y + 6 );
                break;
        }
    }

    if( senetSelectedSquare <= SENET_MAX_SQUARE )
    {
        senetGetSquareCoordinates( senetSelectedSquare, &x, &y );
        x = x - 1;
        y = y - 1;

        if( gbFrameCount % 10 >= 5 ) start = 1;
        else start = 0;

        gbSetColor( GB_WHITE );
        for( i = start; i < 9; i = i + 2 )
        {
            gbDrawPixel( x + i, y );
            gbDrawPixel( x, y + i );
            gbDrawPixel( x + i, y + 8 );
            gbDrawPixel( x + 8, y + i );
        }
        gbSetColor( GB_BLACK );
    }
}

void senetDrawGameState()
{
    int x = ( LCDWIDTH - 3 ) / 2;
    int y = 2;

    if( senetDisplayMovesTimer < 7 )
    {
        senetDisplayMovesTimer = senetDisplayMovesTimer + 1;

        gbSetColor( senetDisplayMovesTimer % 2 );
        gbFillRect( x - 2, y - 2, gbFontWidth + 3, gbFontHeight + 2 );
        gbSetColor( GB_BLACK );
    }
    else
    {
        gbDrawChar( '0' + gbAbsInt( senetMoveMoves ), x, y );
        gbDrawRect( x - 2, y - 2, gbFontWidth + 3, gbFontHeight + 2 );
    }
}

void senetDrawPlayerName()
{
    int length = 0;
    gbCursorX = 0;
    gbCursorY = 1;

    while( senetNamePlayer1[ length ] != 0 )
    {
        gbDrawChar( senetNamePlayer1[ length ], gbCursorX, gbCursorY );
        length = length + 1;
        gbCursorX = gbCursorX + gbFontWidth;
    }
    if( senetCurrentPlayer == senetPlayer1 )
    {
        gbCursorX = 0;
        gbCursorY = gbCursorY + gbFontHeight;
        gbDrawFastHLine( gbCursorX, gbCursorY, length * gbFontWidth );
    }

    if( senetGameMode == SENET_GAMEMODE_SINGLE )
    {
        gbCursorX = LCDWIDTH - 3 * gbFontWidth - 3;
        gbCursorY = 1;
        senetPrint( "CPU" );
        if( senetCurrentPlayer == senetPlayer2 )
        {
            gbCursorX = LCDWIDTH - 3 * gbFontWidth - 3;
            gbCursorY = gbCursorY + gbFontHeight;
            gbDrawFastHLine( gbCursorX, gbCursorY, 3 * gbFontWidth );
        }
    }
    else
    {
        length = 0;
        while( senetNamePlayer2[ length ] != 0 )
        {
            length = length + 1;
        }

        gbCursorX = LCDWIDTH - 3 - length * gbFontWidth;
        gbCursorY = 1;
        senetPrint( senetNamePlayer2 );

        if( senetCurrentPlayer == senetPlayer2 )
        {
            gbCursorX = LCDWIDTH - 3 - length * gbFontWidth;
            gbCursorY = gbCursorY + gbFontHeight;
            gbDrawFastHLine( gbCursorX, gbCursorY, length * gbFontWidth );
        }
    }

    gbCursorX = ( LCDWIDTH - 3 * gbFontWidth ) / 2;
    gbCursorY = 36;
    gbDrawChar( '0' + senetPiecesOffBoard[ senetPlayer1 ], gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbDrawChar( ':', gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbDrawChar( '0' + senetPiecesOffBoard[ senetPlayer2 ], gbCursorX, gbCursorY );
}

// -----------------------------------------------------------------------
// App-level states
// -----------------------------------------------------------------------

// Real upstream play()'s own first line - called both starting a brand new
// game and resuming an in-progress one (matches real upstream exactly:
// `play()` unconditionally calls `next_turn()` on every entry).
void senetNextTurn()
{
    senetMoveTimer = 0;
    senetDisplayMovesTimer = 0;

    if( senetFirstMove )
    {
        senetBeginTurn();
        senetFirstMove = false;
    }
    else if( senetTurnFinished )
    {
        senetEndTurn();
        senetBeginTurn();
        senetRemovePieceFromSquare30();
    }

    senetRoundStarted = true;
}

void senetCpuMove()
{
    if( senetRoundStarted )
    {
        if( senetGetBestMove() ) senetSelectedSquare = senetMoveStart;
        else senetSelectedSquare = SENET_MAX_SQUARE + 1;
    }

    senetRoundStarted = false;

    if( senetMoveTimer >= 10 && !senetTurnFinished )
    {
        // Real upstream's own acknowledged redundant recomputation - see
        // this file's own header comment ("TODO: this second call is
        // unnecessary" in real play.ino) - preserved verbatim.
        if( senetGetBestMove() ) senetDoMove();
        else gbPopup( "No move. CPU skips.", 15 );

        senetSelectedSquare = SENET_MAX_SQUARE + 1;
        senetMoveTimer = 0;
    }
    else if( senetMoveTimer >= 30 )
    {
        senetNextTurn();
    }
}

void senetLocalPlayerMove()
{
    int row, column;

    if( senetRoundStarted )
    {
        senetSelectedSquare = SENET_MIN_SQUARE;
        while( senetGetPiece( senetSelectedSquare ) != senetCurrentPlayer )
        {
            senetSelectedSquare = senetSelectedSquare + 1;
        }

        if( !senetSearchPossibleMove() )
        {
            gbPopup( "No move for you.", 20 );
            senetMoveTimer = 0;
        }
        else if( senetMoveMoves < 0 )
        {
            gbPopup( "Moving backwards!", 40 );
        }
    }

    senetSquareToRowCol( senetSelectedSquare, &row, &column );

    if( gbRepeat( BTN_RIGHT, 3 ) )
    {
        gbPlayTick();
        if( column < 10 ) column = column + 1; // see header comment - always true, dead else, harmless
        else column = 0;
    }
    if( gbRepeat( BTN_LEFT, 3 ) )
    {
        gbPlayTick();
        if( column > 0 ) column = column - 1;
        else column = 9;
    }

    if( ( senetBtnDownHeld >= 5 ) && gbRepeat( BTN_DOWN, 10 ) )
    {
        gbPlayTick();
        senetMoveStart = senetSelectedSquare + 1;
        if( !senetSearchNextPossibleMove() )
        {
            senetMoveStart = SENET_MIN_SQUARE;
            if( !senetSearchNextPossibleMove() )
            {
                senetMoveStart = senetSelectedSquare;
            }
        }
        senetSquareToRowCol( senetMoveStart, &row, &column );
    }
    else if( gbReleased( BTN_DOWN ) && ( senetBtnDownHeld < 5 ) )
    {
        gbPlayTick();
        if( row < 3 ) row = row + 1; // see header comment - always true, dead else, harmless
        else row = 0;
    }
    senetBtnDownHeld = gbTimeHeld( BTN_DOWN );

    if( gbRepeat( BTN_UP, 5 ) )
    {
        gbPlayTick();
        if( row > 0 ) row = row - 1;
        else row = 2;
    }

    senetSelectedSquare = senetRowColToSquare( row, column );

    if( gbPressed( BTN_A ) )
    {
        if( senetGetPiece( senetSelectedSquare ) != senetCurrentPlayer )
        {
            gbPlayCancel();
            gbPopup( "Can't move piece.", 20 );
        }
        else
        {
            senetMoveStart = senetSelectedSquare;
            if( senetIsValidMove() )
            {
                gbPlayOK();
                senetDoMove();
                senetMoveTimer = 0;
            }
            else
            {
                gbPlayCancel();
                gbPopup( "Invalid move!", 20 );
            }
        }
    }

    senetRoundStarted = false;
    // Real upstream guards this with `game_mode != GAMEMODE_MULTI_I2C ||
    // is_master` - unconditionally true now that I2C mode is gone (see
    // this file's own header comment).
    if( senetTurnFinished && senetMoveTimer >= 5 )
    {
        senetNextTurn();
    }
}

void senetPlaySinglePlayer()
{
    if( senetCurrentPlayer == senetPlayer2 ) senetCpuMove();
    else senetLocalPlayerMove();
}

void senetPlayMultiPlayer()
{
    if( senetRoundStarted && senetCurrentPlayer != senetPreviousPlayer )
    {
        senetPreviousPlayer = senetCurrentPlayer;
        senetHandoffConfirmed = false;
        senetAppState = SENET_APP_MULTI_HANDOFF;
        return;
    }

    senetLocalPlayerMove();
}

void senetUpdateMultiHandoff()
{
    if( senetHandoffConfirmed )
    {
        senetAppState = SENET_APP_PLAYING;
        return;
    }

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        senetHandoffConfirmed = true;
    }

    gbSetColor( GB_BLACK );
    gbCursorX = 0;
    gbCursorY = 0;
    senetPrint( "\nNext turn for:\n" );

    gbCursorX = 1 * gbFontWidth;
    gbFontSize = 2;
    if( senetCurrentPlayer == senetPlayer1 ) senetPrintln( senetNamePlayer1 );
    else senetPrintln( senetNamePlayer2 );
    gbFontSize = 1;

    senetPrint( "\n\nPress " );
    gbDrawChar( SENET_ICON_BTN_A, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    senetPrint( " to continue." );
}

void senetUpdatePlaying()
{
    gbSetColor( GB_BLACK );

    if( senetHasWon( senetPlayer2 ) || senetHasWon( senetPlayer1 ) )
    {
        senetGameStarted = false;
        senetAppState = SENET_APP_WINNER;
        return;
    }

    if( senetMoveTimer < 255 ) senetMoveTimer = senetMoveTimer + 1;

    if( gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        senetAppState = SENET_APP_MAIN_MENU;
        return;
    }

    senetDrawPlayerName();
    senetDrawBoard();
    senetDrawGameState();

    if( senetGameMode == SENET_GAMEMODE_SINGLE ) senetPlaySinglePlayer();
    else senetPlayMultiPlayer();
}

// Real upstream play()'s own entry point - always runs next_turn() first,
// whether starting fresh (from game setup) or resuming an already-started
// game (from the main menu's own "Play / Resume" entry).
void senetBeginPlaying()
{
    senetNextTurn();
    senetAppState = SENET_APP_PLAYING;
}

bool senetSetupSingle()
{
    // Real upstream calls `gb.getDefaultName(name_player1)` here - dropped,
    // see this file's own header comment. senetNamePlayer1 stays "Player1".
    gbPlayOK();
    return true;
}

bool senetSetupMulti()
{
    // Real upstream calls `gb.keyboard(name_player2, USERNAME_LENGTH)`
    // here - dropped, see this file's own header comment. senetNamePlayer2
    // stays "Player2".
    gbPlayOK();
    return true;
}

void senetBeginGameSetup()
{
    senetPlayer1 = 1 + arand( 2 );
    senetPlayer2 = senetGetEnemy( senetPlayer1 );
    senetSetupPieces = SENET_PIECE_COUNT_MIN;
    senetAppState = SENET_APP_GAME_SETUP;
}

void senetUpdateGameSetup()
{
    bool started;

    if( gbPressed( BTN_A ) )
    {
        if( senetGameMode == SENET_GAMEMODE_SINGLE ) started = senetSetupSingle();
        else started = senetSetupMulti();

        if( started )
        {
            senetInitState( senetSetupPieces );
            senetFirstMove = true;
            senetSelectedSquare = 0;
            senetGameStarted = true;
            senetPreviousPlayer = SENET_PLAYER_NONE;
            senetBeginPlaying();
        }
        return;
    }

    if( gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        senetAppState = SENET_APP_MAIN_MENU;
        return;
    }

    if( gbRepeat( BTN_LEFT, 5 ) )
    {
        gbPlayTick();
        if( senetSetupPieces == SENET_PIECE_COUNT_MIN ) senetSetupPieces = SENET_PIECE_COUNT_MAX;
        else senetSetupPieces = senetSetupPieces - 1;
    }
    else if( gbRepeat( BTN_RIGHT, 5 ) )
    {
        gbPlayTick();
        if( senetSetupPieces == SENET_PIECE_COUNT_MAX ) senetSetupPieces = SENET_PIECE_COUNT_MIN;
        else senetSetupPieces = senetSetupPieces + 1;
    }

    if( gbRepeat( BTN_DOWN, 5 ) )
    {
        gbPlayTick();
        senetGameMode = ( senetGameMode + 1 ) % SENET_GAMEMODES_LENGTH;
    }
    else if( gbRepeat( BTN_UP, 5 ) )
    {
        gbPlayTick();
        if( senetGameMode > 0 ) senetGameMode = senetGameMode - 1;
        else senetGameMode = SENET_GAMEMODES_LENGTH - 1;
    }

    gbSetColor( GB_BLACK );

    gbCursorX = ( LCDWIDTH - 20 * gbFontWidth ) / 2;
    gbCursorY = 0;
    senetPrint( " -=- GAME SETUP -=- " );

    gbCursorX = 0;
    gbCursorY = 1 * gbFontHeight;
    senetPrint( "Game Mode:" );

    gbCursorX = ( LCDWIDTH - ( SENET_MODE_STR_LEN + 2 ) * gbFontWidth ) / 2;
    gbCursorY = gbCursorY + gbFontHeight;
    gbDrawChar( SENET_ICON_CURSOR_OPEN, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    if( senetGameMode == SENET_GAMEMODE_SINGLE ) senetPrint( "  single   " );
    else senetPrint( "   multi   " );
    gbDrawChar( SENET_ICON_CURSOR_CLOSE, gbCursorX, gbCursorY );

    gbCursorX = 0;
    gbCursorY = 3 * gbFontHeight;
    senetPrint( "Pieces per Player:" );

    gbDrawChar( SENET_ICON_ARROW_LEFT, LCDWIDTH - 5 * gbFontWidth, 4 * gbFontHeight );
    gbDrawChar( SENET_ICON_ARROW_RIGHT, LCDWIDTH - 1 * gbFontWidth, 4 * gbFontHeight );
    gbDrawChar( '0' + senetSetupPieces, LCDWIDTH - 3 * gbFontWidth, 4 * gbFontHeight );

    gbCursorX = 0;
    gbCursorY = 5 * gbFontHeight;
    senetPrint( "Your Pieces:" );
    if( senetPlayer1 == SENET_PLAYER1 ) gbDrawBitmap( 13 * gbFontWidth, gbCursorY, senetPiecePlayer1 );
    else gbDrawBitmap( 13 * gbFontWidth, gbCursorY, senetPiecePlayer2 );

    gbCursorX = 0;
    gbCursorY = LCDHEIGHT - gbFontHeight;
    senetPrint( senetTextCancelHint );

    gbCursorX = LCDWIDTH - 4 * gbFontWidth;
    senetPrint( senetTextOkHint );
}

void senetUpdateWinner()
{
    gbSetColor( GB_BLACK );

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        senetAppState = SENET_APP_MAIN_MENU;
        return;
    }

    gbFontSize = 2;
    gbCursorX = ( LCDWIDTH - 2 * 10 * gbFontWidth ) / 2;
    gbCursorY = 2;
    senetPrint( "GAME OVER!" );

    gbCursorY = 2 + 2 * gbFontHeight;
    gbCursorX = ( LCDWIDTH - 2 * 7 * gbFontWidth ) / 2;
    if( senetHasWon( senetPlayer1 ) )
    {
        senetPrint( "YOU WON" );
    }
    else
    {
        // Real upstream quirk, preserved verbatim - see this file's own
        // header comment: always "CPU WON" here, even in hot-seat MULTI
        // mode where there is no CPU at all.
        senetPrint( "CPU WON" );
    }

    gbCursorX = ( LCDWIDTH - 3 * gbFontWidth ) / 2;
    gbCursorY = 2 + 4 * gbFontHeight;
    gbDrawChar( '0' + senetPiecesOffBoard[ senetPlayer1 ], gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbDrawChar( ':', gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontWidth;
    gbDrawChar( '0' + senetPiecesOffBoard[ senetPlayer2 ], gbCursorX, gbCursorY );

    gbFontSize = 1;

    gbCursorX = LCDWIDTH - 7 * gbFontWidth;
    gbCursorY = LCDHEIGHT - gbFontHeight;
    senetPrint( senetTextCloseHint );
}

void senetBeginTitle()
{
    senetAppState = SENET_APP_TITLE;
}

void senetUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbDrawBitmap( 10, 6, senetLogoBitmap );
    gbCursorX = ( LCDWIDTH - 7 * gbFontWidth ) / 2;
    gbCursorY = 40;
    senetPrint( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        senetMenuIndex = 0;
        senetAppState = SENET_APP_MAIN_MENU;
    }
}

// Hand-rolled replacement for real upstream's own blocking
// `gb.menu(main_menu, MAIN_MENU_LENGTH)` widget - see this file's own
// header comment.
void senetUpdateMainMenu()
{
    int i;

    gbSetColor( GB_BLACK );
    gbCursorX = 2;
    gbCursorY = 2;
    senetPrint( "SENET" );

    for( i = 0; i < 5; i = i + 1 )
    {
        gbCursorY = 12 + i * 7;
        gbCursorX = 2;
        if( i == senetMenuIndex ) senetPrint( ">" );
        gbCursorX = 8;
        if( i == 0 ) senetPrint( "Play / Resume" );
        else if( i == 1 ) senetPrint( "Restart" );
        else if( i == 2 ) senetPrint( "Help" );
        else if( i == 3 ) senetPrint( "Controls" );
        else senetPrint( "Main Menu" );
    }

    if( gbRepeat( BTN_UP, 5 ) ) senetMenuIndex = gbMax( 0, senetMenuIndex - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) ) senetMenuIndex = gbMin( 4, senetMenuIndex + 1 );

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        if( senetMenuIndex == 0 )
        {
            // Real upstream: "Play / Resume" skips game_setup() entirely
            // (resuming as-is) if a game is already started; otherwise it
            // goes through setup like a fresh game.
            if( senetGameStarted ) senetBeginPlaying();
            else senetBeginGameSetup();
        }
        else if( senetMenuIndex == 1 )
        {
            // Real upstream: "Restart" always goes through game setup
            // again, even if a game is already in progress (discarding it
            // only once setup is actually confirmed).
            senetBeginGameSetup();
        }
        else if( senetMenuIndex == 2 )
        {
            senetHelpPage = 0;
            senetAppState = SENET_APP_HELP;
        }
        else if( senetMenuIndex == 3 )
        {
            senetAppState = SENET_APP_CONTROLS;
        }
        else
        {
            // Real upstream: "Main Menu" just re-shows the title screen
            // (gb.titleScreen()), not a real "quit cartridge" - Start
            // already provides that globally, everywhere in this cartridge.
            senetBeginTitle();
        }
    }
}

void senetUpdateHelp()
{
    if( gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayCancel();
        senetAppState = SENET_APP_MAIN_MENU;
        return;
    }

    if( gbPressed( BTN_LEFT ) )
    {
        gbPlayTick();
        if( senetHelpPage > 0 ) senetHelpPage = senetHelpPage - 1;
        else senetHelpPage = SENET_HELP_PAGE_COUNT - 1;
    }
    else if( gbPressed( BTN_RIGHT ) || gbPressed( BTN_A ) )
    {
        gbPlayTick();
        senetHelpPage = ( senetHelpPage + 1 ) % SENET_HELP_PAGE_COUNT;
    }

    gbSetColor( GB_BLACK );
    gbCursorX = 0;
    gbCursorY = 0;
    senetPrint( senetHelpPages[ senetHelpPage ] );
}

void senetUpdateControls()
{
    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) || gbPressed( BTN_C ) )
    {
        gbPlayOK();
        senetAppState = SENET_APP_MAIN_MENU;
        return;
    }

    gbSetColor( GB_BLACK );
    gbCursorX = 0;
    gbCursorY = 0;
    senetPrint( senetTextControls );
}

// -----------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------

void gameSenet_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 );
    gbPickRandomSeed(); // documented no-op - see gamePong.c's own precedent

    senetPreviousPlayer = SENET_PLAYER_NONE;
    senetGameMode = SENET_GAMEMODE_SINGLE;
    senetGameStarted = false;

    senetBeginTitle();
}

void gameSenet_update()
{
    if( !gbUpdate() ) return;

    if( senetAppState == SENET_APP_TITLE ) senetUpdateTitle();
    else if( senetAppState == SENET_APP_MAIN_MENU ) senetUpdateMainMenu();
    else if( senetAppState == SENET_APP_GAME_SETUP ) senetUpdateGameSetup();
    else if( senetAppState == SENET_APP_PLAYING ) senetUpdatePlaying();
    else if( senetAppState == SENET_APP_MULTI_HANDOFF ) senetUpdateMultiHandoff();
    else if( senetAppState == SENET_APP_WINNER ) senetUpdateWinner();
    else if( senetAppState == SENET_APP_HELP ) senetUpdateHelp();
    else if( senetAppState == SENET_APP_CONTROLS ) senetUpdateControls();

    gbRenderFrame();
}
