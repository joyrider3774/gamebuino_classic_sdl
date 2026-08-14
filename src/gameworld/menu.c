#include "avrCompat.h"
#include "menu.h"
#include "machineDependent.h"
#include "biosFont.h"
#include <string.h>

// Ported from the sibling gamebuino_classic_vircon32 build's own menu.c
// (itself modeled on the tinyjoypad_vircon32/Tinyjoypad_SDL projects' own
// identical-shaped menu) - dialect fixes only (array-declaration order,
// `int[]` text -> `char[]`, Vircon32's own BIOS print_at()/clear_screen()/
// screen_width/bios_character_width -> this project's biosDrawText()/
// md_beginFrame()/MD_SCREEN_WIDTH/BIOS_FONT_CHAR_W - see biosFont.h/
// machineDependent.h), no structural changes; unfinished-game red-text
// handling ported too (see biosDrawTextColor()'s own doc comment in
// biosFont.h for why that needed one small additive primitive this
// project's own menu needs that the sibling Tinyjoypad_SDL project's menu
// never did).

// Matches the sibling gamebuino_classic_vircon32 build's own real current
// headroom (112) rather than starting back at Tinyjoypad_SDL's smaller
// 64 - this project is porting that build's own already-99-strong game
// list, not starting from zero.
#define MAX_GAMES 112

// How many entries fit in the vertical space between the list's start
// (y=140) and the bottom of the 360px-tall screen at 24px/row.
#define GAMES_PER_PAGE 9

// Top of the game list/thumbnail area - the header lines above it
// (title + 2 hint lines) are centered independently of this.
#define LIST_AREA_TOP 140

// Centers a fixed-width-bios-font string horizontally on screen.
static int menuCenteredX( char* text )
{
    return ( MD_SCREEN_WIDTH - strlen( text ) * BIOS_FONT_CHAR_W ) / 2;
}

int gameCount = 0;
Game games[ MAX_GAMES ];
int selection = 0;

// `games[]` stays in addGame()'s own registration order always - that's
// also what a launched game's init/update function pointers are looked up
// by (menu_getGame(), called from gamesMain.c's dispatch loop), and what
// the thumbnail atlas is keyed by. Alphabetical sorting is purely a
// *display* concern, so it lives in a separate indirection array instead
// of reordering games[] itself: displayOrder[i] holds the original
// games[] index shown at display position i. `selection` walks display
// positions (0..gameCount-1) - every place that used to index games[]
// directly with `selection` must go through displayOrder[selection]
// instead, or it'll show/launch the wrong game the moment the
// alphabetical order differs from registration order.
int displayOrder[ MAX_GAMES ];
bool displayOrderBuilt = false;

bool prevUp = false;
bool prevDown = false;
bool prevA = false;
bool prevLeft = false;
bool prevRight = false;

int addGame( char* title, char* author, char* info, GameFunc init, GameFunc update, GameFunc onResume )
{
    if( gameCount >= MAX_GAMES )
      return -1;

    games[ gameCount ].title = title;
    games[ gameCount ].author = author;
    games[ gameCount ].info = info;
    games[ gameCount ].init = init;
    games[ gameCount ].update = update;
    games[ gameCount ].onResume = onResume;
    games[ gameCount ].unfinished = false;
    gameCount++;
    return gameCount - 1;
}

void markUnfinished( int index )
{
    if( index < 0 || index >= gameCount )
      return;

    games[ index ].unfinished = true;
}

Game* menu_getGame( int index )
{
    return &games[ index ];
}

// Selection sort on displayOrder (by games[].title) - gameCount is always
// a small handful of entries, so O(n^2) costs nothing measurable here.
static void menu_buildDisplayOrder()
{
    for( int i = 0; i < gameCount; i++ )
      displayOrder[ i ] = i;

    for( int i = 0; i < gameCount - 1; i++ )
    {
        int best = i;
        for( int j = i + 1; j < gameCount; j++ )
          if( strcmp( games[ displayOrder[ j ] ].title, games[ displayOrder[ best ] ].title ) < 0 )
            best = j;

        if( best != i )
        {
            int tmp = displayOrder[ i ];
            displayOrder[ i ] = displayOrder[ best ];
            displayOrder[ best ] = tmp;
        }
    }

    displayOrderBuilt = true;
}

// menu_init() samples each button's own real current state (rather than
// assuming released) before setting up prevUp/Down/Left/Right/A - see the
// sibling gamebuino_classic_vircon32 build's own menu.c for the full real,
// live-found bug this fixes (a player still holding a direction the exact
// moment they confirm Quit would otherwise manufacture a false "just
// pressed" edge on the very next tick and instantly page the just-reopened
// menu sideways with no real new input).
void menu_init()
{
    prevUp = md_inputUp();
    prevDown = md_inputDown();
    prevA = md_inputA();
    prevLeft = md_inputLeft();
    prevRight = md_inputRight();

    // Built once (addGames() has already run by the time menu_init() is
    // first called, and gameCount/games[] never change afterward) - not
    // redone on every return-to-menu.
    if( !displayOrderBuilt )
      menu_buildDisplayOrder();
}

int menu_update()
{
    bool up = md_inputUp();
    bool down = md_inputDown();
    bool a = md_inputA();
    bool left = md_inputLeft();
    bool right = md_inputRight();

    if( down && !prevDown )
    {
        selection++;
        if( selection >= gameCount )
          selection = 0;
    }
    if( up && !prevUp )
    {
        selection--;
        if( selection < 0 )
          selection = gameCount - 1;
    }

    int totalPages = ( gameCount + GAMES_PER_PAGE - 1 ) / GAMES_PER_PAGE;

    // LEFT/RIGHT jump a whole page at a time (wrapping past the last/first
    // page), same idea as UP/DOWN moving one entry at a time.
    if( right && !prevRight )
    {
        int currentPage = selection / GAMES_PER_PAGE;
        currentPage++;
        if( currentPage >= totalPages )
          currentPage = 0;
        selection = currentPage * GAMES_PER_PAGE;
        if( selection >= gameCount )
          selection = gameCount - 1;
    }
    if( left && !prevLeft )
    {
        int currentPage = selection / GAMES_PER_PAGE;
        currentPage--;
        if( currentPage < 0 )
          currentPage = totalPages - 1;
        selection = currentPage * GAMES_PER_PAGE;
        if( selection >= gameCount )
          selection = gameCount - 1;
    }

    bool justPressedA = ( a && !prevA );

    prevUp = up;
    prevDown = down;
    prevA = a;
    prevLeft = left;
    prevRight = right;

    // ---- draw ----
    // A genuinely different clear than a game's own md_beginFrame() - see
    // that function's own doc comment in machineDependent.h for why.
    md_beginMenuFrame();
    biosDrawText( "GAMEBUINO CLASSIC FOR SDL", menuCenteredX( "GAMEBUINO CLASSIC FOR SDL" ), 40 );
    biosDrawText( "UP/DOWN: SELECT     A: PLAY", menuCenteredX( "UP/DOWN: SELECT     A: PLAY" ), 80 );

    int currentPage = selection / GAMES_PER_PAGE;

    if( totalPages > 1 )
    {
        char pageNumText[ 8 ];
        char totalPagesText[ 8 ];
        char pageHintText[ 48 ];
        itoa( currentPage + 1, pageNumText, 10 );
        itoa( totalPages, totalPagesText, 10 );
        strcpy( pageHintText, "LEFT/RIGHT: CHANGE PAGE " );
        strcat( pageHintText, pageNumText );
        strcat( pageHintText, "/" );
        strcat( pageHintText, totalPagesText );
        biosDrawText( pageHintText, menuCenteredX( pageHintText ), 105 );
    }

    int startIndex = currentPage * GAMES_PER_PAGE;

    int y = LIST_AREA_TOP;
    for( int i = 0; i < GAMES_PER_PAGE; i++ )
    {
        int idx = startIndex + i;
        if( idx >= gameCount )
          break;

        // Kept close to the left edge (small margin only) so the right
        // side of the screen stays free for the current game's thumbnail
        // screenshot (see THUMBNAIL_* in machineDependent.h).
        int x = 60;
        if( idx == selection )
          x = 40;

        if( idx == selection )
          biosDrawText( ">", x, y );

        // Zero-padded "NN " position number (1-based on the whole
        // alphabetized list, not per-page) prepended to the title.
        char numText[ 8 ];
        itoa( idx + 1, numText, 10 );
        char labelText[ 64 ];
        if( idx + 1 < 10 )
          strcpy( labelText, "0" );
        else
          strcpy( labelText, "" );
        strcat( labelText, numText );
        strcat( labelText, ". " );
        strcat( labelText, games[ displayOrder[ idx ] ].title );

        // Unfinished games (see markUnfinished()) still show up in their
        // normal alphabetical position, still fully selectable/playable -
        // only the list text itself turns red, as a plain visual "known
        // incomplete" warning.
        if( games[ displayOrder[ idx ] ].unfinished )
          biosDrawTextColor( labelText, x + 20, y, MD_COLOR_RED );
        else
          biosDrawText( labelText, x + 20, y );

        y += 24;
    }

    // Real gameplay screenshot of the currently-selected game, in the
    // margin freed up on the right by keeping the list itself close to
    // the left edge - switches immediately whenever the selection moves,
    // since it's just read straight off `selection` every frame. Centered
    // vertically (as a group with the "BY <author>" line, and optionally a
    // second info line, below it) within the list/selection area
    // (LIST_AREA_TOP down to the bottom of the screen) rather than
    // top-aligned with the list. Indexed through displayOrder[] like the
    // title above - the thumbnail atlas is keyed by registration index,
    // not by alphabetical position.
    int selectedGameIndex = displayOrder[ selection ];
    if( selectedGameIndex < md_getThumbnailCount() )
    {
        char authorText[ 64 ];
        strcpy( authorText, "BY " );
        strcat( authorText, games[ selectedGameIndex ].author );

        // A second, independent info line (Game.info - see menu.h) is
        // drawn directly below the author credit whenever a game supplies
        // one - either a porter-credit continuation, or a short
        // unfinished-game reason (e.g. "Ball can get stuck").
        bool hasInfo = games[ selectedGameIndex ].info != NULL;

        int authorGapY = 8;
        int lineCount = 1;
        if( hasInfo ) lineCount = 2;
        int blockHeight = MD_THUMBNAIL_HEIGHT + authorGapY + BIOS_FONT_CHAR_H * lineCount;
        int blockY = LIST_AREA_TOP + ( ( MD_SCREEN_HEIGHT - LIST_AREA_TOP ) - blockHeight ) / 2;

        md_drawGameThumbnail( selectedGameIndex, 340, blockY );

        int authorX = 340 + ( MD_THUMBNAIL_WIDTH - strlen( authorText ) * BIOS_FONT_CHAR_W ) / 2;
        biosDrawText( authorText, authorX, blockY + MD_THUMBNAIL_HEIGHT + authorGapY );

        if( hasInfo )
        {
            int infoX = 340 + ( MD_THUMBNAIL_WIDTH - strlen( games[ selectedGameIndex ].info ) * BIOS_FONT_CHAR_W ) / 2;
            int infoY = blockY + MD_THUMBNAIL_HEIGHT + authorGapY + BIOS_FONT_CHAR_H;

            if( games[ selectedGameIndex ].unfinished )
              biosDrawTextColor( games[ selectedGameIndex ].info, infoX, infoY, MD_COLOR_RED );
            else
              biosDrawText( games[ selectedGameIndex ].info, infoX, infoY );
        }
    }

    if( justPressedA )
      return selectedGameIndex;

    return -1;
}
