#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Dark Tower (Marcus Hutchings, GPLv3 - github.com/marcushutchings/DarkTower,
// "Copyright (C) 2018 Marcus Hutchings ... either version 3 of the License,
// or (at your option) any later version", confirmed directly from the repo's
// own README.md and DarkTower.cpp header, not assumed).
//
// A real, complete text adventure: you are cursed with undeath and trapped
// inside a dead mage's tower, hunting the cure. Five floors (a flooded
// cellar, the entrance hall, an angel statue, an alchemy lab with a
// key-cutting machine, and the locked master room), one bat that kills you
// on sight without a sword, a real multi-step key-copying puzzle, one
// winning potion and one lethal one.
//
// ---------------------------------------------------------------------------
// Structure: upstream's own function-pointer "event" object system
// ---------------------------------------------------------------------------
// Upstream is a single 2240-line .cpp file (no .ino - still a perfectly
// normal Arduino sketch, it just never got renamed) built around a value-type
// `event` class whose polymorphism is hand-rolled out of member function
// pointers rather than virtuals (upstream's own comment: "Do not use virtual
// methods because the vtable resides in both progmem, and sram. Do not add
// properties to sub-classes otherwise object shearing will result"). Every
// room/outcome is an `event` subclass that only ever assigns a fixed set of
// those pointers in its constructor - so each subclass is, in data terms,
// exactly the base class plus an identity.
//
// That maps cleanly onto this dialect without needing function pointers at
// all: `DtwrEvent` is a plain 5-field struct carrying a `kind` id, and the
// seven virtual-ish operations (load_description / load_object_menu /
// process_action_on_object / process_item_on_object / get_continue_event /
// actions_are_allowed / should_return_to_previous_event) became seven free
// functions that dispatch on `kind` with an if/else chain. Same behavior,
// same per-instance mutable state (`localTags`, upstream's own
// `local_event_tags`), and events are still copied by value into a real
// 2-slot event stack exactly like upstream's own `events[EVENT_MEMORY]`.
// Since this dialect cannot return a struct larger than one word (see
// VIRCON32_C_DIALECT.md section 4), every "constructor" and every operation
// that upstream returns an `event` from instead takes a `DtwrEvent*` result
// out-parameter as its last argument - the same out-pointer conversion this
// project already applies wherever an upstream API returns an aggregate.
//
// The three engine classes (`action_menu_type`, `word_wrapped_text_box_type`,
// `game_screen_type`) and the presenter (`game_presenter_type`) are all real
// singletons upstream, so they are flattened into plain globals/functions -
// the same treatment this project applies to the Gamebuino API itself. The
// presenter's own two member-function-pointer menu callbacks
// (`handle_menu_selection`/`handle_menu_cancel`) became two plain handler-id
// globals dispatched by `dtwrMenuRaiseSelection()`/`dtwrMenuRaiseCancel()`.
//
// ---------------------------------------------------------------------------
// Menu strings: pointers instead of upstream's own copy-into-a-buffer step
// ---------------------------------------------------------------------------
// Upstream copies each menu's item names out of PROGMEM into a real
// `char object_menu_buf[10][13]` staging buffer, then builds a `char*[10]`
// pointer table over it, because on real AVR the strings live in a separate
// address space that `strlen()`/`print()` cannot read directly. There is no
// such split here (this dialect's string literals are plain `int[]` in the
// one flat address space, and a literal is directly usable as an `char*` -
// see gameBRally.c's own `brallyTrackName()` for the same already-proven
// pattern), so the staging buffer is dropped and the menu asks for item
// `i`'s text through `dtwrMenuItemText()` instead. Verified this changes
// nothing observable: upstream's own 13-byte-per-item cell truncates any
// name past 12 characters, and the longest name in the entire game
// ("Silver Sword"/"Modified Key"/"Angel Statue") is exactly 12, so no real
// menu entry was ever actually truncated on hardware either. The staging
// buffer is also only ever refilled at menu-switch time, never mid-frame,
// and every code path that changes the current event switches the menu away
// from the object list in the same call - so a live lookup can never observe
// a different list than the snapshot would have.
//
// ---------------------------------------------------------------------------
// Real AVR 8-bit wraparound, emulated deliberately
// ---------------------------------------------------------------------------
// Upstream's scroll/layout engine keeps almost every counter in `byte`/
// `uint8_t` (top_line, select_line, line_count, top_line_limit,
// select_line_limit, menu_top_line, the visible-line window in
// `print_menu()`, the selection highlight's own rect coordinates) and
// genuinely relies on - or at least genuinely produces - unsigned
// wraparound in several places, where this dialect's always-32-bit `int`
// would instead go negative and take a different branch. Two of those are
// load-bearing:
//
//   - `top_line_limit = line_count - screen_height;` underflows to a large
//     positive value whenever a whole event fits on screen (a short
//     "Continue"-only message). Upstream's own `calculate_line_count()`
//     then takes its `top_line_limit > menu_top_line` branch because of it.
//   - `( --event_stack_pos ) %= EVENT_MEMORY;` in `load_previous_event()`
//     wraps 0 to 255 and then takes 255 % 2 == 1. Ported to a plain `int`
//     this is `-1 % 2 == -1`, a genuine out-of-bounds array index on the
//     very first "return to the previous event" - a real crash risk on this
//     platform, not a cosmetic difference.
//
// Rather than pick and choose, every one of those variables is masked with
// `& 255` at each assignment - exactly where C's own `uint8_t` truncation
// happens - so the layout math behaves bit-for-bit like real hardware. Same
// approach as gameDarkShmup.c's own `& 255`/`& 65535` narrow-type emulation
// and gameUnderTheTower.c's own `uttNarrowS8()`.
//
// ---------------------------------------------------------------------------
// Real upstream quirks preserved deliberately
// ---------------------------------------------------------------------------
//   - Looking at, taking, or using ANYTHING other than the bat itself while
//     the bat is attacking kills you instantly (upstream's own
//     `f0_monster_attacks_event` falls through to
//     `killed_by_bat_while_distracted()` for every action but LOOK-at-bat,
//     and for every item used on anything but the bat). Harsh, and clearly
//     intentional.
//   - Using a non-sword item ON the bat is safe and simply does nothing,
//     while using the same item on the well right next to it is fatal - an
//     asymmetry that falls straight out of upstream's own branch order,
//     preserved as-is.
//   - Dying wipes every item AND every achievement (`resurrect_event`), so a
//     death after opening the master-room door still costs you the crystal
//     and the door state - a genuine full restart wearing a story hat.
//   - The well is a one-way trip. "Rope" only appears in the cellar's own
//     object list while the room event instance has personally noticed it
//     (looking at the barrels), and that notice is refused outright once the
//     rope is already tied around the well - so climbing back out builds a
//     fresh cellar event that can never re-show the rope, and the well
//     cannot be re-entered. Take the crystal (and the blank key) on the
//     first visit or the run is unwinnable. Genuinely how the original
//     plays; left alone rather than "fixed" into a different game.
//   - The key-cutting machine is reset by leaving and re-entering the room
//     (its state lives in the event instance's own `local_event_tags`, and
//     a fresh `f3_main_hall_event` is constructed on every entry) - upstream
//     even documents this in its own in-game hint text.
//   - `GAME_STATE_ID_FINISH_PLAY`/`GAME_STATE_ID_PLAYER_DIED` are declared
//     upstream but never assigned or tested anywhere; kept as dead constants
//     for fidelity.
//   - `player_type::get_item_by_index()` upstream walks a 16-bit id from 1 up
//     by shifting, and its loop guard (`id < 0xffff`) never terminates once
//     the shift wraps a real `unsigned int` to 0 - `has_item(0)` returns true
//     for a zero mask, so the walk would spin forever if it were ever called
//     with an out-of-range index. It never is (the menu only ever offers real
//     held items), so the port keeps the same walk with plain `int`
//     arithmetic, where the shift simply runs past the guard and exits - a
//     divergence only in a case upstream cannot reach either, chosen over
//     faithfully reproducing a potential hang.
//   - Sound: upstream calls no sound API at all (confirmed by grep - no
//     `gb.sound.*` anywhere), so nothing was approximated here.
//   - EEPROM: upstream has no save/load of any kind (no `EEPROM.h` include,
//     no read/write call anywhere in the file), and there is no score or
//     progress concept to persist, so none was invented.
//
// ---------------------------------------------------------------------------
// Necessary deviations
// ---------------------------------------------------------------------------
//   - `gb.titleScreen(F("The Dark Tower"))` is a blocking call upstream;
//     converted into an explicit DTWR_STATE_TITLE state dismissed by a real
//     Button A press, the same "blocking loop -> resumable state" treatment
//     every other game in this cartridge uses. Upstream's own
//     `setFont(font3x5)` before it and `setFont(font5x7)` after it are both
//     preserved, so the title draws in the small font and all gameplay text
//     in the large one exactly like real hardware.
//   - Text output goes through `gbPrintString()`, which reproduces real
//     `Display::write()`'s own '\n' handling but does not auto-wrap at the
//     right screen edge. This game word-wraps every line itself before
//     printing (that is what `word_wrapped_text_box_type` exists for) and
//     hand-manages the menu's own line breaks, so the wrap point is chosen
//     by the game in both cases; the only place hardware's own auto-wrap (if
//     any) could ever have fired is on an exactly-full 14-character line,
//     where it would have inserted a second, blank line on top of the
//     game's own explicit newline.
//   - Upstream's `char event_description[250]` buffer is an `int[260]` here;
//     the real 250-character limit (and upstream's own forced terminator at
//     index 249) is kept exactly, the extra words only cover this dialect's
//     own `strncpy()`/`strncat()` writing their terminator one past the
//     requested count.

// -----------------------------------------------------------------------
//   Constants
// -----------------------------------------------------------------------

#define DTWR_EVENT_MEMORY 2
#define DTWR_DESCRIPTION_SIZE 250
#define DTWR_MAX_OBJECTS_ON_PLAYER 10
#define DTWR_OBJECT_MENU_LENGTH DTWR_MAX_OBJECTS_ON_PLAYER

// Real action ids (ACTION_ID_LOOK shares 0 with ACTION_ID_NONE upstream)
#define DTWR_ACTION_LOOK 0
#define DTWR_ACTION_TAKE 1
#define DTWR_ACTION_USE  2
#define DTWR_ACTION_ITEM 3

// Real player achievement tags (bit 5 is genuinely unused upstream)
#define DTWR_TAG_CRYPT_MONSTER_DEAD       1
#define DTWR_TAG_SECRET_PASSAGE_OPENED    2
#define DTWR_TAG_RELEASED_SWORD           4
#define DTWR_TAG_UNCOVERED_KEY_MACHINE    8
#define DTWR_TAG_ROPE_TIED_AROUND_WELL    16
#define DTWR_TAG_MASTER_ROOM_DOOR_OPENED  64
#define DTWR_TAG_ALL                      255

// Real player item ids
#define DTWR_ITEM_MASTER_KEY     1
#define DTWR_ITEM_OIL_LAMP       2
#define DTWR_ITEM_SWORD          4
#define DTWR_ITEM_BROKEN_KEY     8
#define DTWR_ITEM_BLANK_KEY      16
#define DTWR_ITEM_MOD_CHEST_KEY  32
#define DTWR_ITEM_SHEET          64
#define DTWR_ITEM_COPIED_KEY     128
#define DTWR_ITEM_CHEST_KEY      256
#define DTWR_ITEM_ROPE           512
#define DTWR_ITEM_ALL            65535

// Real game states (FINISH_PLAY/PLAYER_DIED are dead upstream - see header)
#define DTWR_STATE_TITLE       0
#define DTWR_STATE_INIT        1
#define DTWR_STATE_PLAY        2
#define DTWR_STATE_FINISH_PLAY 3
#define DTWR_STATE_PLAYER_DIED 4

// Event kinds - one per real `event` subclass upstream
#define DTWR_EV_GENERIC             0
#define DTWR_EV_INTRO               1
#define DTWR_EV_RESURRECT           2
#define DTWR_EV_RETURN_TO_GAME      3
#define DTWR_EV_WIN                 4
#define DTWR_EV_PLAYER_DIES         5
#define DTWR_EV_DRINKS_YELLOW       6
#define DTWR_EV_F0_IN_WELL          7
#define DTWR_EV_F0_LIGHT_ROOM       8
#define DTWR_EV_F0_MONSTER_DIES     9
#define DTWR_EV_F0_MONSTER_ATTACKS  10
#define DTWR_EV_F0_DARK_ROOM        11
#define DTWR_EV_F4_CHEST_COPIED     12
#define DTWR_EV_F4_CHEST_MODIFIED   13
#define DTWR_EV_F4_CHEST_CHEST      14
#define DTWR_EV_F4_MAIN_HALL        15
#define DTWR_EV_F4_DOOR_UNLOCKED    16
#define DTWR_EV_F4_MAIN_HALL_LOCKED 17
#define DTWR_EV_F3_MAIN_HALL        18
#define DTWR_EV_F2_MAIN_HALL        19
#define DTWR_EV_F1_MAIN_HALL        20

// Real per-event local tag bits (each event subclass upstream declares its
// own private set over the same shared `local_event_tags` byte)
#define DTWR_WELL_NOTICE_KEY   1
#define DTWR_LIGHT_NOTICE_ROPE 1

#define DTWR_F3_NOTICE_KEY     1
#define DTWR_F3_CUT_CHEST_KEY  2
#define DTWR_F3_CUT_BLANK_KEY  4
#define DTWR_F3_COPY_CHEST_KEY 8
#define DTWR_F3_COPY_BROKEN_KEY 16
#define DTWR_F3_CUT_KEY_PLACED  6
#define DTWR_F3_COPY_KEY_PLACED 24

#define DTWR_F2_NOTICE_SWORD 1
#define DTWR_F2_NOTICE_KEY   2

// Real f2_main_hall_event::event_object_ids
#define DTWR_F2_OBJ_WINDOW         0
#define DTWR_F2_OBJ_STATUE         1
#define DTWR_F2_OBJ_STAIRS_UP      2
#define DTWR_F2_OBJ_STAIRS_DOWN    3
#define DTWR_F2_OBJ_SILVER_SWORD   4
#define DTWR_F2_OBJ_BROKEN_KEY     5
#define DTWR_F2_OBJ_SWORD_ON_FLOOR 5
#define DTWR_F2_OBJ_ANGEL_CHANGED  6

// Menu item sources (upstream: whichever string list is currently loaded)
#define DTWR_MENU_SRC_ACTIONS  0
#define DTWR_MENU_SRC_CONTINUE 1
#define DTWR_MENU_SRC_OBJECTS  2
#define DTWR_MENU_SRC_ITEMS    3

// Menu selection handlers (upstream: menu_event_handler's own two member
// function pointers)
#define DTWR_H_NONE                0
#define DTWR_H_ACTION_SEL          1
#define DTWR_H_CONTINUE_SEL        2
#define DTWR_H_OBJECT_SEL          3
#define DTWR_H_OBJECT_FOR_ITEM_SEL 4
#define DTWR_H_ITEM_SEL            5

// Menu cancel handlers
#define DTWR_HC_NONE          0
#define DTWR_HC_CONTINUE      1
#define DTWR_HC_OBJECT_CANCEL 2

// -----------------------------------------------------------------------
//   Event data (upstream's own `event` base class, minus the seven member
//   function pointers - see this file's own header comment)
// -----------------------------------------------------------------------

typedef struct
{
    int kind;
    char* description;
    bool allowActions;
    bool returnToPrevious;
    int localTags;
} DtwrEvent;

DtwrEvent dtwrEvents[DTWR_EVENT_MEMORY];
int dtwrEventsScrollPos[DTWR_EVENT_MEMORY];
int dtwrEventStackPos;
int dtwrSelectedAction;
int dtwrSelectedItem;

char dtwrDescBuf[260];
int dtwrDescWidthInChars;

int dtwrGameState;

// player_type
int dtwrItemsCarried;
int dtwrAchievements;

// action_menu_type
int dtwrMenuSource;
int dtwrMenuCount;
int dtwrMenuSelected;
int dtwrMenuWidthInChars;
int dtwrMenuScrollDelay;
int dtwrMenuMinLine;
int dtwrMenuMaxLine;
int dtwrMenuSelHandler;
int dtwrMenuCancelHandler;

// game_screen_type
int dtwrScrLineCount;
int dtwrScrTopLine;
int dtwrScrTopLineLimit;
int dtwrScrSelectLine;
int dtwrScrSelectLineLimit;
int dtwrScrMenuTopLine;
int dtwrScrHeight;
int dtwrScrWidth;
int dtwrScrScrollDelay;
bool dtwrScrEventScrollUp;
bool dtwrScrEventScrollDown;
char* dtwrScrMenuTitle;

// Forward declarations - the event constructors reference each other
// (a room's own "continue" outcome is another room), exactly like
// upstream's own create_fN_main_hall_event() forward declarations.
void dtwrMakeF0LightRoom( DtwrEvent* e );
void dtwrMakeF1MainHall( DtwrEvent* e );
void dtwrMakeF2MainHall( DtwrEvent* e );
void dtwrMakeF3MainHall( DtwrEvent* e );
void dtwrMakeF4MainHall( DtwrEvent* e );
void dtwrMakeIntro( DtwrEvent* e );
void dtwrMakeResurrect( DtwrEvent* e );
void dtwrMakeReturnToGame( DtwrEvent* e );
void dtwrMakeWin( DtwrEvent* e );

// Presenter handlers, called from the menu code defined above them
void dtwrHandleActionMenuSelection( int actionSelected );
void dtwrHandleActionMenuContinue();
void dtwrHandleObjectMenuSelection( int selection );
void dtwrHandleObjectForItemMenuSelection( int selection );
void dtwrHandleItemMenuSelection( int selection );
void dtwrHandleObjectMenuCancel();

// -----------------------------------------------------------------------
//   Shared description strings (upstream's own three named PROGMEM strings
//   reused by more than one event)
// -----------------------------------------------------------------------

char* dtwrTextDrinkPinkVial()
{
    return "You take the pink vial and drink it. It tastes foul. A few moments later, you start coughing blood violently and collapse. Everything goes dark.";
}

char* dtwrTextOpenChest()
{
    return "The key slots in and turns. The chest unlocks and you open it. The door slams shut behind you. Inside you see ";
}

char* dtwrTextF0Hall()
{
    return "The light from your lamp shows this is a store room with many barrels. There is a well in the middle of the room.";
}

// -----------------------------------------------------------------------
//   String helpers (upstream's own load_progmem_string_to_var() /
//   append_progmem_string_to_string() - no PROGMEM address space here, so
//   these are plain copies)
// -----------------------------------------------------------------------

void dtwrLoadStringLimited( char* src, char* dest, int limit )
{
    strncpy( dest, src, limit );
    dest[ limit - 1 ] = 0;
}

void dtwrAppendString( char* src, char* dest, int maxStringLength )
{
    int usedLength = strlen( dest );
    int remainingLength = maxStringLength - usedLength;

    if( remainingLength < 1 ) return;
    strncat( dest, src, remainingLength );
}

// -----------------------------------------------------------------------
//   player_type
// -----------------------------------------------------------------------

void dtwrAddAchievement( int newAchievement )
{
    dtwrAchievements = dtwrAchievements | newAchievement;
}

void dtwrRemoveAchievement( int lostAchievement )
{
    dtwrAchievements = dtwrAchievements & ~lostAchievement;
}

bool dtwrHasAchievement( int checkAchievement )
{
    return ( dtwrAchievements & checkAchievement ) != 0;
}

void dtwrAddItem( int item )
{
    dtwrItemsCarried = dtwrItemsCarried | item;
}

void dtwrRemoveItem( int item )
{
    dtwrItemsCarried = dtwrItemsCarried & ~item;
}

bool dtwrHasItem( int item )
{
    return ( dtwrItemsCarried & item ) == item;
}

// Real player_item_name_full_list[], indexed by bit position
char* dtwrItemNameByBit( int bitIndex )
{
    if( bitIndex == 0 ) return "Crystal";
    if( bitIndex == 1 ) return "Lamp";
    if( bitIndex == 2 ) return "Silver Sword";
    if( bitIndex == 3 ) return "Broken Key";
    if( bitIndex == 4 ) return "Blank Key";
    if( bitIndex == 5 ) return "Modified Key";
    if( bitIndex == 6 ) return "Curtain";
    if( bitIndex == 7 ) return "Copied Key";
    if( bitIndex == 8 ) return "Copper Key";
    return "Rope";
}

// Real player_type::get_item_by_index() - see this file's own header comment
// for why the plain-int walk is used instead of upstream's own wrapping one.
int dtwrGetItemByIndex( int itemIndex )
{
    int result = 0;
    int count = 0;
    int id;

    for( id = 1; id < DTWR_ITEM_ALL; id = id << 1 )
    {
        if( dtwrHasItem( id ) )
        {
            if( count == itemIndex )
            {
                result = id;
                break;
            }
            count = count + 1;
        }
    }

    return result;
}

// Real player_type::load_item_menu() - here it only needs to report how many
// entries the menu has (the names come from dtwrItemMenuName() below).
int dtwrItemMenuCount()
{
    int count = 0;
    int i;

    for( i = 0; i < DTWR_MAX_OBJECTS_ON_PLAYER; i = i + 1 )
      if( dtwrHasItem( 1 << i ) )
        count = count + 1;

    return count;
}

char* dtwrItemMenuName( int menuIndex )
{
    int count = 0;
    int i;

    for( i = 0; i < DTWR_MAX_OBJECTS_ON_PLAYER; i = i + 1 )
    {
        if( dtwrHasItem( 1 << i ) )
        {
            if( count == menuIndex )
              return dtwrItemNameByBit( i );
            count = count + 1;
        }
    }

    return "";
}

// -----------------------------------------------------------------------
//   Shared event predicates (upstream duplicates should_show_crystal()
//   verbatim in two different event classes)
// -----------------------------------------------------------------------

bool dtwrShouldShowCrystal()
{
    bool crystalIsAvailable = !dtwrHasItem( DTWR_ITEM_MASTER_KEY );
    bool crystalHasNotBeenUsed = !dtwrHasAchievement( DTWR_TAG_MASTER_ROOM_DOOR_OPENED );

    return crystalIsAvailable && crystalHasNotBeenUsed;
}

bool dtwrWellShouldShowKey( DtwrEvent* e )
{
    bool keyHasBeenNoticed = ( e->localTags & DTWR_WELL_NOTICE_KEY ) != 0;
    bool keyIsAvailable = !dtwrHasItem( DTWR_ITEM_BLANK_KEY );
    bool keyIsNotCut = !dtwrHasItem( DTWR_ITEM_COPIED_KEY );
    bool crystalIsShown = dtwrShouldShowCrystal();

    return keyHasBeenNoticed && keyIsAvailable && crystalIsShown && keyIsNotCut;
}

bool dtwrLightRoomShouldShowRope( DtwrEvent* e )
{
    bool ropeHasBeenNoticed = ( e->localTags & DTWR_LIGHT_NOTICE_ROPE ) != 0;
    bool ropeIsAvailable = !dtwrHasItem( DTWR_ITEM_ROPE );

    return ropeHasBeenNoticed && ropeIsAvailable;
}

bool dtwrF3PlayerCanSeeTheKey( DtwrEvent* e )
{
    bool keyIsAvailable = !dtwrHasItem( DTWR_ITEM_CHEST_KEY );
    bool keyHasBeenNoticed = ( e->localTags & DTWR_F3_NOTICE_KEY ) != 0;

    return keyIsAvailable && keyHasBeenNoticed;
}

bool dtwrF3KeyCutterIsReady( DtwrEvent* e )
{
    bool copyKeyPlaced = ( e->localTags & DTWR_F3_COPY_KEY_PLACED ) != 0;
    bool cutKeyPlaced = ( e->localTags & DTWR_F3_CUT_KEY_PLACED ) != 0;

    return copyKeyPlaced && cutKeyPlaced;
}

void dtwrF3MakeNewKey( DtwrEvent* e )
{
    int keyToCut = e->localTags & DTWR_F3_CUT_KEY_PLACED;

    if( keyToCut == DTWR_F3_CUT_CHEST_KEY )
    {
        dtwrRemoveItem( DTWR_ITEM_CHEST_KEY );
        dtwrAddItem( DTWR_ITEM_MOD_CHEST_KEY );
    }
    else if( keyToCut == DTWR_F3_CUT_BLANK_KEY )
    {
        dtwrRemoveItem( DTWR_ITEM_BLANK_KEY );
        dtwrAddItem( DTWR_ITEM_COPIED_KEY );
    }
}

bool dtwrF2SwordIsOnGround()
{
    bool swordIsReleased = dtwrHasAchievement( DTWR_TAG_RELEASED_SWORD );
    bool swordIsAvailable = !dtwrHasItem( DTWR_ITEM_SWORD );

    return swordIsReleased && swordIsAvailable;
}

bool dtwrF2SwordInObjectList( DtwrEvent* e )
{
    bool swordIsReleased = dtwrHasAchievement( DTWR_TAG_RELEASED_SWORD );
    bool swordIsAvailable = !dtwrHasItem( DTWR_ITEM_SWORD );
    bool swordHasBeenNoticed = ( e->localTags & DTWR_F2_NOTICE_SWORD ) != 0;

    return swordIsAvailable && ( swordIsReleased || swordHasBeenNoticed );
}

bool dtwrF2PlayerCanAccessKey()
{
    bool swordIsReleased = dtwrHasAchievement( DTWR_TAG_RELEASED_SWORD );
    bool swordIsAvailable = !dtwrHasItem( DTWR_ITEM_SWORD );
    bool keyIsAvailable = !dtwrHasItem( DTWR_ITEM_BROKEN_KEY );

    return swordIsReleased && swordIsAvailable && keyIsAvailable;
}

bool dtwrF2KeyInObjectList( DtwrEvent* e )
{
    bool keyIsAccessible = dtwrF2PlayerCanAccessKey();
    bool keyHasBeenNoticed = ( e->localTags & DTWR_F2_NOTICE_KEY ) != 0;

    return keyIsAccessible && keyHasBeenNoticed;
}

// -----------------------------------------------------------------------
//   Event constructors (one per real `event` subclass)
// -----------------------------------------------------------------------

void dtwrEventInitDefault( DtwrEvent* e )
{
    e->kind = DTWR_EV_GENERIC;
    e->description = "Nothing happens.";
    e->allowActions = false;
    e->returnToPrevious = true;
    e->localTags = 0;
}

void dtwrMakeGeneric( DtwrEvent* e, char* desc )
{
    dtwrEventInitDefault( e );
    e->description = desc;
}

void dtwrMakeIntro( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_INTRO;
    e->description = "Stairs lead up to the first floor of the abandoned tower. A tower flowing with what Angels fear; the dark. You ascend hoping to find a way to break the curse of undeath that has come upon you. The tower's doors close behind you. You are trapped!";
    e->returnToPrevious = false;
    dtwrRemoveAchievement( DTWR_TAG_ALL );
    dtwrRemoveItem( DTWR_ITEM_ALL );
}

void dtwrMakeResurrect( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_RESURRECT;
    e->description = "You wake up in the entrance hall of the tower. Not sure of what has happened, you find you have lost your items!";
    e->returnToPrevious = false;
    dtwrRemoveAchievement( DTWR_TAG_ALL );
    dtwrRemoveItem( DTWR_ITEM_ALL );
}

void dtwrMakeReturnToGame( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_RETURN_TO_GAME;
    e->description = "Well Done! You have won! If you would like to play again, then please continue.";
    e->returnToPrevious = false;
}

void dtwrMakeWin( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_WIN;
    e->description = "You leave the tower feeling reborn. The ordeal of the tower may well live with you forever, but now no longer bearing the curse of undeath you can explore and enjoy everything the world has to offer.";
    e->returnToPrevious = false;
}

void dtwrMakePlayerDies( DtwrEvent* e, char* desc )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_PLAYER_DIES;
    e->description = desc;
    e->returnToPrevious = false;
}

void dtwrMakeDrinksYellowVial( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_DRINKS_YELLOW;
    e->description = "You take the yellow vial and drink it. It tastes foul. A few moments later, you feel warmth return to your body. You starting breathing again. The potion has cured you of the curse of undeath!";
    e->returnToPrevious = false;
}

void dtwrMakeF0InTheWell( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F0_IN_WELL;
    e->description = "The water comes up to your waist, it feels cold.";
    e->allowActions = true;
}

void dtwrMakeF0LightRoom( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F0_LIGHT_ROOM;
    e->description = dtwrTextF0Hall();
    e->allowActions = true;
}

void dtwrMakeF0MonsterDies( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F0_MONSTER_DIES;
    e->description = "The bat flies towards you, fangs ready to bite you. You quickly draw your sword and swing at the creature. The sword cuts the creature and the fell beast screeches and bursts into flames before evaporating into mist.";
    e->returnToPrevious = false;
}

void dtwrMakeF0MonsterAttacks( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F0_MONSTER_ATTACKS;
    e->description = dtwrTextF0Hall();
    e->allowActions = true;
}

void dtwrMakeF0DarkRoom( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F0_DARK_ROOM;
    e->description = "You descend several steps, but it quickly gets too dark to proceed further.";
    e->allowActions = true;
}

void dtwrMakeF4ChestCopiedKey( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F4_CHEST_COPIED;
    e->description = dtwrTextOpenChest();
    e->allowActions = true;
}

void dtwrMakeF4ChestModifiedKey( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F4_CHEST_MODIFIED;
    e->description = dtwrTextOpenChest();
    e->allowActions = true;
}

void dtwrMakeF4ChestChestKey( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F4_CHEST_CHEST;
    e->description = dtwrTextOpenChest();
    e->allowActions = true;
}

void dtwrMakeF4MainHall( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F4_MAIN_HALL;
    e->description = "Four arrow-slit windows cast a dim light in this room. A chest stands in the middle of the room.";
    e->allowActions = true;
}

void dtwrMakeF4DoorUnlocked( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F4_DOOR_UNLOCKED;
    e->description = "You place the crystal in the door. The magic circle and the symbols glow faintly and hum with energy. With a loud grinding sound, the door swings open to reveal the room beyond.";
    e->returnToPrevious = false;
}

void dtwrMakeF4MainHallLocked( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F4_MAIN_HALL_LOCKED;
    e->description = "You ascend the stairs to the next floor. A solid oak door awaits you at the top of the stairs.";
    e->allowActions = true;
}

void dtwrMakeF3MainHall( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F3_MAIN_HALL;
    e->description = "At opposite ends of the room are stairs; one leads up, one leads down. There is a table of alchemical instruments and broken glass.";
    e->allowActions = true;
}

void dtwrMakeF2MainHall( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F2_MAIN_HALL;
    e->description = "A barred window casts a ray of light over a statue of a knightly angel. Stairs continue to lead up as well as down.";
    e->allowActions = true;
}

void dtwrMakeF1MainHall( DtwrEvent* e )
{
    dtwrEventInitDefault( e );
    e->kind = DTWR_EV_F1_MAIN_HALL;
    e->description = "A red carpet leads between the entrance door and stairs that lead up and down. The oil lamps on the wall dimly light the room in dancing shadows.";
    e->allowActions = true;
}

// -----------------------------------------------------------------------
//   event::load_description() dispatch
// -----------------------------------------------------------------------

void dtwrLoadDescription( DtwrEvent* e, char* out, int maxStringLength )
{
    dtwrLoadStringLimited( e->description, out, maxStringLength );

    if( e->kind == DTWR_EV_F0_IN_WELL )
    {
        if( dtwrShouldShowCrystal() )
          dtwrAppendString( " You can see what looks like a crystal in the water.", out, maxStringLength );
    }
    else if( e->kind == DTWR_EV_F0_MONSTER_ATTACKS )
      dtwrAppendString( " Your light disturbs a large black creature, hanging from the ceiling. The large bat unfolds its wings and attacks you!", out, maxStringLength );
    else if( e->kind == DTWR_EV_F4_CHEST_COPIED )
      dtwrAppendString( "two vials, each containing a different coloured liquid.", out, maxStringLength );
    else if( e->kind == DTWR_EV_F4_CHEST_MODIFIED || e->kind == DTWR_EV_F4_CHEST_CHEST )
      dtwrAppendString( "a vial containing a pink liquid.", out, maxStringLength );
    else if( e->kind == DTWR_EV_F3_MAIN_HALL )
    {
        if( dtwrHasAchievement( DTWR_TAG_UNCOVERED_KEY_MACHINE ) )
          dtwrAppendString( " Next to it is a key-cutting machine.", out, maxStringLength );
        else
          dtwrAppendString( " Next to it a dusty curtain covers something large.", out, maxStringLength );
    }
    else if( e->kind == DTWR_EV_F2_MAIN_HALL )
    {
        if( dtwrF2SwordIsOnGround() )
          dtwrAppendString( " The sword is lying on the ground before the statue.", out, maxStringLength );
    }
}

// -----------------------------------------------------------------------
//   event::load_object_menu() dispatch - a count plus a per-index name,
//   instead of upstream's own copy-into-a-buffer step (see header comment)
// -----------------------------------------------------------------------

int dtwrObjectMenuCount( DtwrEvent* e )
{
    int count;

    if( e->kind == DTWR_EV_F0_IN_WELL )
    {
        count = 2; // real object_list of 4, minus 2
        if( dtwrShouldShowCrystal() )
        {
            count = count + 1;
            if( dtwrWellShouldShowKey( e ) )
              count = count + 1;
        }
        return count;
    }

    if( e->kind == DTWR_EV_F0_LIGHT_ROOM )
    {
        count = 3; // real object_list of 4, minus 1
        if( dtwrLightRoomShouldShowRope( e ) )
          count = count + 1;
        return count;
    }

    if( e->kind == DTWR_EV_F0_MONSTER_ATTACKS ) return 4;
    if( e->kind == DTWR_EV_F0_DARK_ROOM ) return 2;
    if( e->kind == DTWR_EV_F4_CHEST_COPIED ) return 3;
    if( e->kind == DTWR_EV_F4_CHEST_MODIFIED ) return 2;
    if( e->kind == DTWR_EV_F4_CHEST_CHEST ) return 2;
    if( e->kind == DTWR_EV_F4_MAIN_HALL ) return 2;
    if( e->kind == DTWR_EV_F4_MAIN_HALL_LOCKED ) return 2;

    if( e->kind == DTWR_EV_F3_MAIN_HALL )
    {
        count = 4; // real object_list of 5, minus 1
        if( dtwrF3PlayerCanSeeTheKey( e ) )
          count = count + 1;
        return count;
    }

    if( e->kind == DTWR_EV_F2_MAIN_HALL )
    {
        count = 4; // real object_list of 6, minus 2
        if( dtwrF2SwordInObjectList( e ) )
          count = count + 1;
        if( dtwrF2KeyInObjectList( e ) )
          count = count + 1;
        return count;
    }

    if( e->kind == DTWR_EV_F1_MAIN_HALL ) return 4;

    return 0; // real event::default_load_object_menu() - an empty menu
}

char* dtwrObjectMenuName( DtwrEvent* e, int i )
{
    if( e->kind == DTWR_EV_F0_IN_WELL )
    {
        if( i == 0 ) return "Water";
        if( i == 1 ) return "Rope";
        if( i == 2 ) return "Crystal";
        return "Key";
    }

    if( e->kind == DTWR_EV_F0_LIGHT_ROOM )
    {
        if( i == 0 ) return "Stairs up";
        if( i == 1 ) return "Barrels";
        if( i == 2 ) return "Well";
        return "Rope";
    }

    if( e->kind == DTWR_EV_F0_MONSTER_ATTACKS )
    {
        if( i == 0 ) return "Stairs up";
        if( i == 1 ) return "Barrels";
        if( i == 2 ) return "Well";
        return "Large Bat";
    }

    if( e->kind == DTWR_EV_F0_DARK_ROOM )
    {
        if( i == 0 ) return "Stairs up";
        return "Darkness";
    }

    if( e->kind == DTWR_EV_F4_CHEST_COPIED )
    {
        if( i == 0 ) return "Pink Vial";
        if( i == 1 ) return "Yellow Vial";
        return "Door";
    }

    if( e->kind == DTWR_EV_F4_CHEST_MODIFIED )
    {
        if( i == 0 ) return "Pink Vial";
        return "Door";
    }

    if( e->kind == DTWR_EV_F4_CHEST_CHEST )
    {
        if( i == 0 ) return "Vial";
        return "Door";
    }

    if( e->kind == DTWR_EV_F4_MAIN_HALL )
    {
        if( i == 0 ) return "Chest";
        return "Stairs down";
    }

    if( e->kind == DTWR_EV_F4_MAIN_HALL_LOCKED )
    {
        if( i == 0 ) return "Stairs Down";
        return "Door";
    }

    if( e->kind == DTWR_EV_F3_MAIN_HALL )
    {
        if( i == 0 ) return "Table";
        if( i == 1 )
        {
            if( dtwrHasAchievement( DTWR_TAG_UNCOVERED_KEY_MACHINE ) )
              return "Key Machine";
            return "Curtain";
        }
        if( i == 2 ) return "Stairs up";
        if( i == 3 ) return "Stairs down";
        return "Key";
    }

    if( e->kind == DTWR_EV_F2_MAIN_HALL )
    {
        if( i == 0 ) return "Window";
        if( i == 1 ) return "Angel Statue";
        if( i == 2 ) return "Stairs up";
        if( i == 3 ) return "Stairs down";
        if( i == 4 ) return "Silver Sword";
        return "Key";
    }

    if( e->kind == DTWR_EV_F1_MAIN_HALL )
    {
        if( i == 0 ) return "Entrance";
        if( i == 1 ) return "Stairs up";
        if( i == 2 ) return "Stairs down";
        return "Lamp";
    }

    return "";
}

// -----------------------------------------------------------------------
//   Per-room LOOK description tables
// -----------------------------------------------------------------------

char* dtwrWellLook( int i )
{
    if( i == 0 ) return "The water is clear and stagnant.";
    if( i == 1 ) return "The rope hangs down from above.";
    if( i == 2 ) return "The diamond-shaped crystal seems to glow with magical energy.";
    if( i == 3 ) return "The key is has a no cuttings on its head. It is like a blank key.";
    return "You notice in the light of the crystal there is a key in water.";
}

char* dtwrLightRoomLook( int i )
{
    if( i == 0 ) return "The stairs lead up to the light of the entrance room.";
    if( i == 1 ) return "The barrels contain grain.";
    if( i == 2 ) return "There seems to be water in the well.";
    if( i == 3 ) return "The long length of rope is made of hemp and looks strong.";
    return "One of the barrels contains some rope.";
}

char* dtwrShimmerInWater()
{
    return "There seems to be water in the well. Something glitters in the light under the shallow water.";
}

char* dtwrDarkRoomLook( int i )
{
    if( i == 0 ) return "The stairs lead up to the light of the entrance room.";
    return "This area is too dark to see anything.";
}

char* dtwrChestCopiedLook( int i )
{
    if( i == 0 ) return "The small vial contains a pink liquid.";
    if( i == 1 ) return "The small vial contains a yellow liquid.";
    return "The door is shut tight. You cannot open it.";
}

// Shared by both single-vial chest events - their real look_descriptions
// tables are identical (only the object NAMES differ: "Pink Vial" vs "Vial")
char* dtwrChestSingleVialLook( int i )
{
    if( i == 0 ) return "The small vial contains a pink liquid.";
    return "The door is shut tight. You cannot open it.";
}

char* dtwrF4HallLook( int i )
{
    if( i == 0 ) return "The chest is sturdy with iron bands and a lock built in. Could this have the cure you are looking for?";
    return "The stairs lead down to the floor below.";
}

char* dtwrF4LockedLook( int i )
{
    if( i == 0 ) return "The stairs lead down to the hall below.";
    return "The solid oak door is sturdy and is locked. A magic circle with strange symbols mark the door. In the centre is a diamond shaped hole.";
}

char* dtwrF3Look( int i )
{
    if( i == 0 ) return "The table is stained with spilled chemicals. The tools and instruments are rusted and broken. In amongst the mess there is a copper key.";
    if( i == 1 )
    {
        if( dtwrHasAchievement( DTWR_TAG_UNCOVERED_KEY_MACHINE ) )
          return "The key cutting machine seems to take first the key you wish to copy and then the cut you wish to cut.";
        return "The elegant red curtain with gold trim completely covers something large and box shaped.";
    }
    if( i == 2 ) return "The stairs wind up to the next floor.";
    if( i == 3 ) return "The stairs lead down to the faint light of the hall below.";
    if( i == 4 ) return "The key is small and made of copper. It has an elegant floral pattern on the handle.";
    return "The table is stained with spilled chemicals. The tools and instruments are rusted and broken.";
}

char* dtwrF2Look( int i )
{
    if( i == 0 ) return "The lonely window has rusted iron bars. A broken rail clings to the wall above the window.";
    if( i == 1 ) return "The statue is of an angelic knight kneeling before the light of the window. One hand on its breast plate the other holding a silver sword up-side-down.";
    if( i == 2 ) return "The stairs lead up to the next floor.";
    if( i == 3 ) return "Stairs lead down to a warm glow.";
    if( i == 4 ) return "The sword glitters beautifully in the light. It carries a sharp edge.";
    if( i == 5 ) return "The sword is lying on the stone floor, light reflects off it onto the wall, which becomes translucent revealing a cache. In the cache you see a key.";
    return "The statue is of an angelic knight kneeling before the window. One hand on its breast plate the other reaching out in despair.";
}

char* dtwrF1Look( int i )
{
    if( i == 0 ) return "The doors are made of old oak. Patterns of trees and falling leaves are carved into the doors.";
    if( i == 1 ) return "The wooden stairs go up as they wind around the wall, leading to the next floor.";
    if( i == 2 ) return "The stone stairs hug the wall as they descend into darkness.";
    return "The lamps are still running; though, they have not been touched for a long time.";
}

// -----------------------------------------------------------------------
//   event::process_action_on_object() dispatch
// -----------------------------------------------------------------------

void dtwrKilledByBatWhileDistracted( DtwrEvent* result )
{
    dtwrMakePlayerDies( result, "While distracted the bat grabs you and sinks its fangs deep into your neck. Everything goes dark." );
}

void dtwrProcessActionOnObject( DtwrEvent* self, int selectedAction, int objectSelected, DtwrEvent* result )
{
    bool keyDisappears;

    if( self->kind == DTWR_EV_F0_IN_WELL )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            if( objectSelected == 0 && dtwrShouldShowCrystal() )
            {
                self->localTags = self->localTags | DTWR_WELL_NOTICE_KEY;
                objectSelected = 4;
            }
            dtwrMakeGeneric( result, dtwrWellLook( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == 1 )
            {
                dtwrMakeF0LightRoom( result );
                return;
            }
        }
        else if( selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == 2 )
            {
                keyDisappears = dtwrWellShouldShowKey( self );
                dtwrAddItem( DTWR_ITEM_MASTER_KEY );
                if( keyDisappears )
                  dtwrMakeGeneric( result, "You take the diamond-shaped crystal. The key in the water fades into nothingness." );
                else
                  dtwrMakeGeneric( result, "You take the diamond-shaped crystal." );
                return;
            }
            if( objectSelected == 3 )
            {
                dtwrAddItem( DTWR_ITEM_BLANK_KEY );
                dtwrMakeGeneric( result, "You take the blank key." );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F0_LIGHT_ROOM )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            if( objectSelected == 1 )
            {
                if( !dtwrHasItem( DTWR_ITEM_ROPE ) && !dtwrHasAchievement( DTWR_TAG_ROPE_TIED_AROUND_WELL ) )
                {
                    self->localTags = self->localTags | DTWR_LIGHT_NOTICE_ROPE;
                    objectSelected = 4;
                }
            }
            else if( objectSelected == 2 )
            {
                if( dtwrShouldShowCrystal() )
                {
                    dtwrMakeGeneric( result, dtwrShimmerInWater() );
                    return;
                }
            }
            dtwrMakeGeneric( result, dtwrLightRoomLook( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == 0 )
            {
                dtwrMakeF1MainHall( result );
                return;
            }
            else if( objectSelected == 3 && dtwrHasAchievement( DTWR_TAG_ROPE_TIED_AROUND_WELL ) )
            {
                dtwrMakeF0InTheWell( result );
                return;
            }
        }
        else if( selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == 3 )
            {
                dtwrAddItem( DTWR_ITEM_ROPE );
                dtwrRemoveAchievement( DTWR_TAG_ROPE_TIED_AROUND_WELL );
                dtwrMakeGeneric( result, "You gather the rope." );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F0_MONSTER_ATTACKS )
    {
        // Real upstream: only LOOK-at-the-bat is survivable; every other
        // action - including looking at anything else - is fatal.
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            if( objectSelected == 3 )
            {
                dtwrMakeGeneric( result, "The bat has a five foot wing span and very sharp fangs." );
                return;
            }
        }
        dtwrKilledByBatWhileDistracted( result );
        return;
    }

    if( self->kind == DTWR_EV_F0_DARK_ROOM )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            dtwrMakeGeneric( result, dtwrDarkRoomLook( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == 0 )
            {
                dtwrMakeF1MainHall( result );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F4_CHEST_COPIED )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            dtwrMakeGeneric( result, dtwrChestCopiedLook( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE || selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == 0 )
            {
                dtwrMakePlayerDies( result, dtwrTextDrinkPinkVial() );
                return;
            }
            if( objectSelected == 1 )
            {
                dtwrMakeDrinksYellowVial( result );
                return;
            }
            dtwrMakeGeneric( result, dtwrChestCopiedLook( objectSelected ) );
            return;
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F4_CHEST_MODIFIED || self->kind == DTWR_EV_F4_CHEST_CHEST )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            dtwrMakeGeneric( result, dtwrChestSingleVialLook( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE || selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == 0 )
            {
                dtwrMakePlayerDies( result, dtwrTextDrinkPinkVial() );
                return;
            }
            dtwrMakeGeneric( result, dtwrChestSingleVialLook( objectSelected ) );
            return;
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F4_MAIN_HALL )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            dtwrMakeGeneric( result, dtwrF4HallLook( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == 1 )
            {
                dtwrMakeF3MainHall( result );
                return;
            }
        }
        else if( selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == 0 )
            {
                dtwrMakeGeneric( result, "You try to move the chest, but it won't budge. It is like it is held in place by some force." );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F4_MAIN_HALL_LOCKED )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            dtwrMakeGeneric( result, dtwrF4LockedLook( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == 0 )
            {
                dtwrMakeF3MainHall( result );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F3_MAIN_HALL )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            if( objectSelected == 0 )
            {
                if( dtwrHasItem( DTWR_ITEM_CHEST_KEY ) )
                  objectSelected = 5;
                self->localTags = self->localTags | DTWR_F3_NOTICE_KEY;
            }
            dtwrMakeGeneric( result, dtwrF3Look( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == 2 )
            {
                if( dtwrHasAchievement( DTWR_TAG_MASTER_ROOM_DOOR_OPENED ) )
                  dtwrMakeF4MainHall( result );
                else
                  dtwrMakeF4MainHallLocked( result );
                return;
            }
            if( objectSelected == 3 )
            {
                dtwrMakeF2MainHall( result );
                return;
            }
            if( objectSelected == 1 )
            {
                if( dtwrF3KeyCutterIsReady( self ) )
                {
                    dtwrF3MakeNewKey( self );
                    dtwrMakeGeneric( result, "You use the machine and get a new key." );
                    return;
                }
                else
                {
                    if( dtwrHasAchievement( DTWR_TAG_UNCOVERED_KEY_MACHINE ) )
                    {
                        dtwrMakeGeneric( result, "First place a key to copy then one to cut. To reset choices, leave and return to this room." );
                        return;
                    }
                }
            }
        }
        else if( selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == 1 )
            {
                if( !dtwrHasAchievement( DTWR_TAG_UNCOVERED_KEY_MACHINE ) )
                {
                    dtwrAddAchievement( DTWR_TAG_UNCOVERED_KEY_MACHINE );
                    dtwrAddItem( DTWR_ITEM_SHEET );
                    dtwrMakeGeneric( result, "You collect the curtain and uncover what seems to be a key cutting machine." );
                    return;
                }
            }
            else if( objectSelected == 4 )
            {
                dtwrAddItem( DTWR_ITEM_CHEST_KEY );
                dtwrMakeGeneric( result, "You pick up the small copper key." );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F2_MAIN_HALL )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            if( objectSelected == DTWR_F2_OBJ_STATUE )
            {
                if( !dtwrHasItem( DTWR_ITEM_SWORD ) )
                  self->localTags = self->localTags | DTWR_F2_NOTICE_SWORD;
                if( dtwrHasAchievement( DTWR_TAG_RELEASED_SWORD ) )
                  objectSelected = DTWR_F2_OBJ_ANGEL_CHANGED;
            }
            if( objectSelected == DTWR_F2_OBJ_SILVER_SWORD )
            {
                if( dtwrF2PlayerCanAccessKey() )
                {
                    self->localTags = self->localTags | DTWR_F2_NOTICE_KEY;
                    objectSelected = DTWR_F2_OBJ_SWORD_ON_FLOOR;
                }
            }
            dtwrMakeGeneric( result, dtwrF2Look( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == DTWR_F2_OBJ_STAIRS_UP )
            {
                dtwrMakeF3MainHall( result );
                return;
            }
            if( objectSelected == DTWR_F2_OBJ_STAIRS_DOWN )
            {
                dtwrMakeF1MainHall( result );
                return;
            }
        }
        else if( selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == DTWR_F2_OBJ_SILVER_SWORD )
            {
                if( dtwrHasAchievement( DTWR_TAG_RELEASED_SWORD ) )
                {
                    dtwrAddItem( DTWR_ITEM_SWORD );
                    dtwrMakeGeneric( result, "The sword is in perfect condition and glistens silver in the light." );
                }
                else
                  dtwrMakeGeneric( result, "You are unable to release the sword from the statue's grip." );
                return;
            }
            if( objectSelected == DTWR_F2_OBJ_BROKEN_KEY )
            {
                dtwrAddItem( DTWR_ITEM_BROKEN_KEY );
                dtwrMakeGeneric( result, "The key's handle is broken off and missing." );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F1_MAIN_HALL )
    {
        if( selectedAction == DTWR_ACTION_LOOK )
        {
            dtwrMakeGeneric( result, dtwrF1Look( objectSelected ) );
            return;
        }
        else if( selectedAction == DTWR_ACTION_USE )
        {
            if( objectSelected == 0 )
            {
                dtwrMakeGeneric( result, "The door is shut tight. You cannot open it." );
                return;
            }
            if( objectSelected == 1 )
            {
                dtwrMakeF2MainHall( result );
                return;
            }
            if( objectSelected == 2 )
            {
                dtwrMakeF0DarkRoom( result );
                return;
            }
        }
        else if( selectedAction == DTWR_ACTION_TAKE )
        {
            if( objectSelected == 3 )
            {
                if( !dtwrHasItem( DTWR_ITEM_OIL_LAMP ) )
                {
                    dtwrAddItem( DTWR_ITEM_OIL_LAMP );
                    dtwrMakeGeneric( result, "You take one of the lamps off the wall." );
                }
                else
                  dtwrMakeGeneric( result, "You already have a lamp!" );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    // Real event::default_process_action_on_object()
    dtwrEventInitDefault( result );
}

// -----------------------------------------------------------------------
//   event::process_item_on_object() dispatch
// -----------------------------------------------------------------------

void dtwrProcessItemOnObject( DtwrEvent* self, int selectedItem, int objectSelected, DtwrEvent* result )
{
    if( self->kind == DTWR_EV_F0_LIGHT_ROOM )
    {
        if( selectedItem == DTWR_ITEM_ROPE )
        {
            if( objectSelected == 2 )
            {
                dtwrAddAchievement( DTWR_TAG_ROPE_TIED_AROUND_WELL );
                dtwrRemoveItem( DTWR_ITEM_ROPE );
                dtwrMakeGeneric( result, "You tie the rope around the well." );
                return;
            }
        }
        else if( selectedItem == DTWR_ITEM_OIL_LAMP )
        {
            if( objectSelected == 2 )
            {
                if( dtwrShouldShowCrystal() )
                  dtwrMakeGeneric( result, dtwrShimmerInWater() );
                else
                  dtwrMakeGeneric( result, "The water is still." );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F0_MONSTER_ATTACKS )
    {
        if( objectSelected == 3 )
        {
            if( selectedItem == DTWR_ITEM_SWORD )
            {
                dtwrAddAchievement( DTWR_TAG_CRYPT_MONSTER_DEAD );
                dtwrMakeF0MonsterDies( result );
                return;
            }
            dtwrEventInitDefault( result );
            return;
        }
        dtwrKilledByBatWhileDistracted( result );
        return;
    }

    if( self->kind == DTWR_EV_F0_DARK_ROOM )
    {
        if( selectedItem == DTWR_ITEM_OIL_LAMP )
        {
            if( objectSelected == 1 )
            {
                if( dtwrHasAchievement( DTWR_TAG_CRYPT_MONSTER_DEAD ) )
                  dtwrMakeF0LightRoom( result );
                else
                  dtwrMakeF0MonsterAttacks( result );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F4_MAIN_HALL )
    {
        if( objectSelected == 0 )
        {
            if( selectedItem == DTWR_ITEM_CHEST_KEY )
            {
                dtwrMakeF4ChestChestKey( result );
                return;
            }
            if( selectedItem == DTWR_ITEM_MOD_CHEST_KEY )
            {
                dtwrMakeF4ChestModifiedKey( result );
                return;
            }
            if( selectedItem == DTWR_ITEM_COPIED_KEY )
            {
                dtwrMakeF4ChestCopiedKey( result );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F4_MAIN_HALL_LOCKED )
    {
        if( selectedItem == DTWR_ITEM_MASTER_KEY )
        {
            if( objectSelected == 1 )
            {
                dtwrAddAchievement( DTWR_TAG_MASTER_ROOM_DOOR_OPENED );
                dtwrMakeF4DoorUnlocked( result );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F3_MAIN_HALL )
    {
        if( objectSelected == 1 )
        {
            if( dtwrHasAchievement( DTWR_TAG_UNCOVERED_KEY_MACHINE ) )
            {
                if( ( self->localTags & DTWR_F3_COPY_KEY_PLACED ) == 0 )
                {
                    if( selectedItem == DTWR_ITEM_CHEST_KEY )
                    {
                        dtwrMakeGeneric( result, "This key looks okay. It doesn't need copying." );
                        return;
                    }
                    if( selectedItem == DTWR_ITEM_BROKEN_KEY )
                    {
                        self->localTags = self->localTags | DTWR_F3_COPY_BROKEN_KEY;
                        dtwrMakeGeneric( result, "You place the broken key in the machine for copying." );
                        return;
                    }
                }
                else if( ( self->localTags & DTWR_F3_CUT_KEY_PLACED ) == 0 )
                {
                    if( selectedItem == DTWR_ITEM_CHEST_KEY )
                    {
                        self->localTags = self->localTags | DTWR_F3_CUT_CHEST_KEY;
                        dtwrMakeGeneric( result, "You place the copper key in the machine for cutting." );
                        return;
                    }
                    if( selectedItem == DTWR_ITEM_BLANK_KEY )
                    {
                        self->localTags = self->localTags | DTWR_F3_CUT_BLANK_KEY;
                        dtwrMakeGeneric( result, "You place the blank key in the machine for cutting." );
                        return;
                    }
                }
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    if( self->kind == DTWR_EV_F2_MAIN_HALL )
    {
        if( selectedItem == DTWR_ITEM_SHEET )
        {
            if( objectSelected == DTWR_F2_OBJ_WINDOW )
            {
                dtwrAddAchievement( DTWR_TAG_RELEASED_SWORD );
                dtwrRemoveItem( DTWR_ITEM_SHEET );
                dtwrMakeGeneric( result, "You cover the window with the curtain. The room is cloaked in darkness. A high pitch scream echoes in the room and then a clang of metal. You drop the curtain to see the sword is now lying on the floor." );
                return;
            }
            else if( objectSelected == DTWR_F2_OBJ_STATUE )
            {
                dtwrMakeGeneric( result, "The curtain is too small to cover the statue." );
                return;
            }
        }
        dtwrEventInitDefault( result );
        return;
    }

    // Real event::default_process_item_on_object()
    dtwrEventInitDefault( result );
}

// -----------------------------------------------------------------------
//   event::get_continue_event() dispatch
// -----------------------------------------------------------------------

void dtwrGetContinueEvent( DtwrEvent* e, DtwrEvent* result )
{
    if( e->kind == DTWR_EV_INTRO || e->kind == DTWR_EV_RESURRECT )
      dtwrMakeF1MainHall( result );
    else if( e->kind == DTWR_EV_RETURN_TO_GAME )
      dtwrMakeIntro( result );
    else if( e->kind == DTWR_EV_WIN )
      dtwrMakeReturnToGame( result );
    else if( e->kind == DTWR_EV_PLAYER_DIES )
      dtwrMakeResurrect( result );
    else if( e->kind == DTWR_EV_DRINKS_YELLOW )
      dtwrMakeWin( result );
    else if( e->kind == DTWR_EV_F0_MONSTER_DIES )
      dtwrMakeF0LightRoom( result );
    else if( e->kind == DTWR_EV_F4_DOOR_UNLOCKED )
      dtwrMakeF4MainHall( result );
    else
      dtwrEventInitDefault( result );
}

// -----------------------------------------------------------------------
//   word_wrapped_text_box_type - the description box
//
//   Upstream walks the description with `const char*` cursors; this port
//   walks it with plain integer indices into dtwrDescBuf[] instead, which
//   sidesteps this dialect's own restrictions around pointer arithmetic
//   (see VIRCON32_C_DIALECT.md section 17.1) without changing the logic.
// -----------------------------------------------------------------------

int dtwrGetWordLength( int pos )
{
    bool cont = true;
    int length = 0;
    int c;

    while( cont )
    {
        c = dtwrDescBuf[ pos ];
        if( c == 0 || c == 32 )
          cont = false;
        else
          length = length + 1;

        pos = pos + 1;
    }

    return length;
}

int dtwrCountCharsOnOneLine( int pos, int maxWidthInChars )
{
    bool cont = true;
    int curLineLength = 0;
    int curWordLength;

    while( cont )
    {
        curWordLength = dtwrGetWordLength( pos );

        if( curWordLength == 0 )
        {
            if( dtwrDescBuf[ pos ] == 0 )
              cont = false;
            else
              curWordLength = curWordLength + 1;
        }

        // Real upstream runs this second test even on the terminating pass
        // (where curWordLength is 0, so it adds nothing) - kept verbatim.
        if( curLineLength + curWordLength <= maxWidthInChars )
        {
            curLineLength = curLineLength + curWordLength;
            pos = pos + curWordLength;
        }
        else
          cont = false;
    }

    return curLineLength;
}

int dtwrFindNextWord( int pos )
{
    bool cont = true;

    while( cont )
    {
        if( dtwrDescBuf[ pos ] == 0 || dtwrDescBuf[ pos ] != 32 )
          cont = false;
        else
          pos = pos + 1;
    }

    return pos;
}

int dtwrFindStartOfNextLine( int pos )
{
    int curPos = dtwrFindNextWord( pos );
    int curWidth = dtwrCountCharsOnOneLine( curPos, dtwrDescWidthInChars );

    return curPos + curWidth;
}

int dtwrFindFirstLineToShow( int lineToFind )
{
    int curPos = 0;
    int lineCount;

    for( lineCount = 0; lineCount < lineToFind; lineCount = lineCount + 1 )
      curPos = dtwrFindStartOfNextLine( curPos );

    return curPos;
}

void dtwrPrintWrapped( int pos, int maxLines, int maxWidthInChars )
{
    bool cont = true;
    int lineCount = 0;
    int curWidth, i;
    char lineToPrint[64];

    while( cont )
    {
        pos = dtwrFindNextWord( pos );
        curWidth = dtwrCountCharsOnOneLine( pos, maxWidthInChars );

        if( curWidth > 0 )
        {
            if( curWidth > 62 ) curWidth = 62; // buffer guard - real width is 14
            for( i = 0; i < curWidth; i = i + 1 )
              lineToPrint[ i ] = dtwrDescBuf[ pos + i ];
            lineToPrint[ curWidth ] = 10; // '\n'
            lineToPrint[ curWidth + 1 ] = 0;
            gbPrintString( lineToPrint );
            pos = pos + curWidth;
            lineCount = lineCount + 1;
        }
        else
          cont = false;

        if( lineCount >= maxLines )
          cont = false;
    }
}

int dtwrDescCountLines( int maxWidthInChars )
{
    int pos = 0;
    bool cont = true;
    int lineCount = 0;
    int curWidth;

    while( cont )
    {
        pos = dtwrFindNextWord( pos );
        curWidth = dtwrCountCharsOnOneLine( pos, maxWidthInChars );

        if( curWidth > 0 )
        {
            pos = pos + curWidth;
            lineCount = lineCount + 1;
        }
        else
          cont = false;
    }

    return lineCount;
}

void dtwrDescLoadEvent( DtwrEvent* e )
{
    dtwrLoadDescription( e, dtwrDescBuf, DTWR_DESCRIPTION_SIZE );
    dtwrDescWidthInChars = LCDWIDTH / gbFontWidth;
}

int dtwrDescGetDisplayLineCount()
{
    return dtwrDescCountLines( dtwrDescWidthInChars );
}

void dtwrDescDisplayPortion( int startLine, int linesToShow )
{
    int pos = dtwrFindFirstLineToShow( startLine );
    dtwrPrintWrapped( pos, linesToShow, dtwrDescWidthInChars );
}

// -----------------------------------------------------------------------
//   action_menu_type
// -----------------------------------------------------------------------

char* dtwrMenuItemText( int i )
{
    if( dtwrMenuSource == DTWR_MENU_SRC_ACTIONS )
    {
        if( i == 0 ) return "Look";
        if( i == 1 ) return "Take";
        if( i == 2 ) return "Use";
        return "Item";
    }

    if( dtwrMenuSource == DTWR_MENU_SRC_CONTINUE )
      return "Continue";

    if( dtwrMenuSource == DTWR_MENU_SRC_ITEMS )
      return dtwrItemMenuName( i );

    return dtwrObjectMenuName( &dtwrEvents[ dtwrEventStackPos ], i );
}

void dtwrMenuLoad( int source, int count )
{
    dtwrMenuSource = source;
    dtwrMenuCount = count;
    dtwrMenuWidthInChars = LCDWIDTH / gbFontWidth;
    dtwrMenuSelected = 0;
}

void dtwrMenuSetMinMaxLineForSelection( int minLine, int maxLine )
{
    dtwrMenuMinLine = minLine;
    dtwrMenuMaxLine = maxLine;
}

void dtwrMenuSelectNextItem()
{
    if( dtwrMenuSelected + 1 < dtwrMenuCount )
      dtwrMenuSelected = dtwrMenuSelected + 1;
}

void dtwrMenuSelectPreviousItem()
{
    if( dtwrMenuSelected > 0 )
      dtwrMenuSelected = dtwrMenuSelected - 1;
}

void dtwrMenuRaiseSelectionEvent()
{
    if( dtwrMenuSelHandler == DTWR_H_ACTION_SEL )
      dtwrHandleActionMenuSelection( dtwrMenuSelected );
    else if( dtwrMenuSelHandler == DTWR_H_CONTINUE_SEL )
      dtwrHandleActionMenuContinue();
    else if( dtwrMenuSelHandler == DTWR_H_OBJECT_SEL )
      dtwrHandleObjectMenuSelection( dtwrMenuSelected );
    else if( dtwrMenuSelHandler == DTWR_H_OBJECT_FOR_ITEM_SEL )
      dtwrHandleObjectForItemMenuSelection( dtwrMenuSelected );
    else if( dtwrMenuSelHandler == DTWR_H_ITEM_SEL )
      dtwrHandleItemMenuSelection( dtwrMenuSelected );
}

void dtwrMenuRaiseCancelEvent()
{
    if( dtwrMenuCancelHandler == DTWR_HC_CONTINUE )
      dtwrHandleActionMenuContinue();
    else if( dtwrMenuCancelHandler == DTWR_HC_OBJECT_CANCEL )
      dtwrHandleObjectMenuCancel();
}

bool dtwrMenuShouldMoveRight()
{
    bool scrollRight = gbRepeat( BTN_RIGHT, dtwrMenuScrollDelay );
    if( gbPressed( BTN_RIGHT ) ) scrollRight = true;
    return scrollRight;
}

bool dtwrMenuShouldMoveLeft()
{
    bool scrollLeft = gbRepeat( BTN_LEFT, dtwrMenuScrollDelay );
    if( gbPressed( BTN_LEFT ) ) scrollLeft = true;
    return scrollLeft;
}

void dtwrMenuProcessControllerInput()
{
    if( dtwrMenuShouldMoveRight() )
      dtwrMenuSelectNextItem();
    else if( dtwrMenuShouldMoveLeft() )
      dtwrMenuSelectPreviousItem();

    if( gbPressed( BTN_A ) )
      dtwrMenuRaiseSelectionEvent();
    else if( gbPressed( BTN_B ) )
      dtwrMenuRaiseCancelEvent();
}

void dtwrMenuPrintItem( char* item )
{
    int curItemLength, printEndX;

    if( gbCursorX == 0 )
      gbPrintString( " " );
    else
    {
        curItemLength = ( strlen( item ) + 1 ) & 255;
        printEndX = ( gbCursorX + curItemLength * gbFontWidth ) & 255;
        if( printEndX > LCDWIDTH )
        {
            gbPrintString( "\n" );
            gbPrintString( " " );
        }
    }

    gbPrintString( item );
    gbPrintString( " " );
}

void dtwrMenuPrintSelectedItem( char* item )
{
    int rectWidth = ( ( strlen( item ) + 1 ) * gbFontWidth ) & 255;
    int goBack = ( rectWidth + gbFontWidth ) & 255;
    int menuItemX, menuItemY, curScreenXPos, curScreenYPos, rectHeight;

    if( goBack > gbCursorX )
      goBack = gbCursorX;

    menuItemX = ( gbCursorX - goBack ) & 255;
    menuItemY = gbCursorY & 255;

    curScreenXPos = ( menuItemX + ( gbFontWidth / 2 ) ) & 255;
    curScreenYPos = ( menuItemY - 2 ) & 255;
    rectHeight = ( gbFontHeight + 3 ) & 255;

    gbDrawRoundRect( curScreenXPos, curScreenYPos, rectWidth, rectHeight, 3 );
}

int dtwrMenuFirstItemSkippingLines( int linesToSkip )
{
    int curWidth = 1;
    int lineCount = 1;
    int i = 0;
    int curActionWidth;

    if( linesToSkip == 0 )
      return 0;

    for( ; i < dtwrMenuCount; i = i + 1 )
    {
        curActionWidth = strlen( dtwrMenuItemText( i ) ) + 1;
        if( curWidth + curActionWidth <= dtwrMenuWidthInChars )
          curWidth = curWidth + curActionWidth;
        else
        {
            curWidth = 1 + curActionWidth;
            lineCount = lineCount + 1;
            if( lineCount > linesToSkip )
              break;
        }
    }

    return i;
}

void dtwrMenuPrint( int startLine, int linesToShow )
{
    int lineCount = 1;
    int curY = gbCursorY;
    int i = dtwrMenuFirstItemSkippingLines( startLine );
    int minVisibleLine = ( dtwrMenuMinLine + (-startLine) ) & 255;
    int maxVisibleLine = ( dtwrMenuMaxLine + (-startLine) ) & 255;

    for( ; i < dtwrMenuCount && lineCount <= linesToShow; i = i + 1 )
    {
        dtwrMenuPrintItem( dtwrMenuItemText( i ) );

        if( gbCursorY != curY )
        {
            lineCount = lineCount + 1;
            curY = gbCursorY;
        }

        if( i == dtwrMenuSelected )
        {
            if( lineCount >= minVisibleLine )
              dtwrMenuPrintSelectedItem( dtwrMenuItemText( i ) );
            else
              dtwrMenuSelectNextItem();

            if( lineCount > maxVisibleLine )
              dtwrMenuSelectPreviousItem();
        }
    }
}

int dtwrMenuGetLineCount()
{
    int curWidth = 1;
    int lineCount = 1;
    int i, curActionWidth;

    for( i = 0; i < dtwrMenuCount; i = i + 1 )
    {
        curActionWidth = strlen( dtwrMenuItemText( i ) ) + 1;
        if( curWidth + curActionWidth <= dtwrMenuWidthInChars )
          curWidth = curWidth + curActionWidth;
        else
        {
            curWidth = 1 + curActionWidth;
            lineCount = lineCount + 1;
        }
    }

    return lineCount;
}

// -----------------------------------------------------------------------
//   game_screen_type - description + menu-heading + menu, scrolled as one
//   continuous list of text lines
// -----------------------------------------------------------------------

int dtwrScrMenuHeadingLineCount()
{
    if( dtwrScrMenuTitle != NULL )
      return 3;
    return 1;
}

void dtwrScrCalculateLineCount()
{
    int descriptionLineCount = dtwrDescGetDisplayLineCount();
    int menuHeadingLineCount = dtwrScrMenuHeadingLineCount();
    int actionMenuLineCount = dtwrMenuGetLineCount();
    int negativeAdjustmentForMenu = 0;

    dtwrScrMenuTopLine = ( descriptionLineCount + menuHeadingLineCount ) & 255;
    dtwrScrLineCount = ( descriptionLineCount + menuHeadingLineCount + actionMenuLineCount + 1 ) & 255;
    dtwrScrTopLineLimit = ( dtwrScrLineCount - dtwrScrHeight ) & 255;

    if( actionMenuLineCount >= 3 )
    {
        if( actionMenuLineCount >= 5 )
          negativeAdjustmentForMenu = 3;
        else
          negativeAdjustmentForMenu = actionMenuLineCount + (-2);
    }

    if( dtwrScrTopLineLimit > dtwrScrMenuTopLine )
      dtwrScrSelectLineLimit = dtwrScrMenuTopLine;
    else
      dtwrScrSelectLineLimit = dtwrScrTopLineLimit;

    dtwrScrSelectLineLimit = ( dtwrScrSelectLineLimit + actionMenuLineCount + (-1) + (-negativeAdjustmentForMenu) ) & 255;
}

void dtwrScrCheckAndCorrectScroll()
{
    if( dtwrScrHeight >= dtwrScrLineCount )
    {
        dtwrScrTopLine = 0;
        dtwrScrSelectLine = 0;
    }
    else if( dtwrScrTopLine > dtwrScrTopLineLimit )
    {
        dtwrScrTopLine = dtwrScrTopLineLimit;
        dtwrScrSelectLine = dtwrScrTopLineLimit;
    }
}

void dtwrScrSetScrollPosition( int newPosition )
{
    dtwrScrTopLine = newPosition & 255;
    dtwrScrSelectLine = dtwrScrTopLine;
    dtwrScrCheckAndCorrectScroll();
}

int dtwrScrGetScrollPosition()
{
    return dtwrScrTopLine;
}

void dtwrScrRecalculateLineCount()
{
    dtwrScrCalculateLineCount();
    dtwrScrSetScrollPosition( dtwrScrTopLine );
    dtwrScrCheckAndCorrectScroll();
}

void dtwrScrLoadEvent( DtwrEvent* e )
{
    dtwrScrTopLine = 0;
    dtwrScrSelectLine = 0;
    dtwrScrHeight = LCDHEIGHT / gbFontHeight;
    dtwrScrWidth = LCDWIDTH / gbFontWidth;

    dtwrDescLoadEvent( e );
    dtwrScrCalculateLineCount();
}

void dtwrScrSetMenuTitle( char* title )
{
    dtwrScrMenuTitle = title;
}

bool dtwrScrShouldScrollDown()
{
    dtwrScrEventScrollDown = gbRepeat( BTN_DOWN, dtwrScrScrollDelay );
    if( gbPressed( BTN_DOWN ) ) dtwrScrEventScrollDown = true;
    return dtwrScrEventScrollDown;
}

bool dtwrScrShouldScrollUp()
{
    dtwrScrEventScrollUp = gbRepeat( BTN_UP, dtwrScrScrollDelay );
    if( gbPressed( BTN_UP ) ) dtwrScrEventScrollUp = true;
    return dtwrScrEventScrollUp;
}

void dtwrScrScrollDown()
{
    if( dtwrScrSelectLine < dtwrScrSelectLineLimit )
    {
        if( dtwrScrSelectLine < dtwrScrTopLineLimit )
          dtwrScrTopLine = ( dtwrScrSelectLine + 1 ) & 255;

        dtwrScrSelectLine = ( dtwrScrSelectLine + 1 ) & 255;
    }
}

void dtwrScrScrollUp()
{
    if( dtwrScrSelectLine > 0 )
    {
        if( dtwrScrSelectLine <= dtwrScrTopLineLimit )
          dtwrScrTopLine = ( dtwrScrSelectLine + (-1) ) & 255;

        dtwrScrSelectLine = ( dtwrScrSelectLine + (-1) ) & 255;
    }
}

bool dtwrScrShouldJumpToMenu()
{
    bool jump = gbPressed( BTN_A );
    if( gbPressed( BTN_B ) ) jump = true;
    return jump;
}

void dtwrScrJumpToMenu()
{
    if( dtwrScrTopLineLimit < dtwrScrMenuTopLine )
    {
        dtwrScrTopLine = dtwrScrTopLineLimit;
        dtwrScrSelectLine = dtwrScrTopLineLimit;
    }
    else
    {
        dtwrScrTopLine = ( dtwrScrMenuTopLine + (-3) ) & 255;
        dtwrScrSelectLine = dtwrScrTopLine;
    }
}

void dtwrScrDisplayDescription()
{
    dtwrDescDisplayPortion( dtwrScrTopLine, dtwrScrHeight );
}

void dtwrScrDisplaySpacer()
{
    if( gbCursorY < LCDHEIGHT )
      gbPrintString( "\n" );
}

void dtwrScrDisplayMenuHeading()
{
    if( gbCursorY < LCDHEIGHT )
    {
        gbPrintString( dtwrScrMenuTitle );
        gbPrintString( "\n" );
    }
}

void dtwrScrDisplayMenuHeadingBlock()
{
    if( dtwrScrMenuTitle == NULL )
    {
        if( dtwrScrTopLine < dtwrScrMenuTopLine )
          dtwrScrDisplaySpacer();
    }
    else
    {
        if( dtwrScrTopLine < ( dtwrScrMenuTopLine - 2 ) )
          dtwrScrDisplaySpacer();
        if( dtwrScrTopLine < ( dtwrScrMenuTopLine - 1 ) )
          dtwrScrDisplayMenuHeading();
        if( dtwrScrTopLine < dtwrScrMenuTopLine )
          dtwrScrDisplaySpacer();
    }
}

void dtwrScrDisplayAdjustForMenuJustInView( int remainingLines )
{
    if( remainingLines == 1 )
    {
        if( dtwrScrEventScrollDown )
          dtwrScrScrollDown();
        else if( dtwrScrEventScrollUp )
          dtwrScrScrollUp();
    }
}

void dtwrScrDisplayMenu()
{
    int remainingYResolution = 0;
    int remainingLines, startOnLine, minSelectLine, maxSelectLine;

    if( gbCursorY < LCDHEIGHT )
      remainingYResolution = LCDHEIGHT - gbCursorY;

    remainingLines = remainingYResolution / gbFontHeight;

    if( remainingLines > 0 )
    {
        startOnLine = 0;

        // Real upstream reads the controller from inside its own draw pass,
        // so a selection made this frame swaps the menu (and possibly the
        // whole event) out from under the rest of this same draw - kept
        // exactly as-is, ordering included.
        dtwrMenuProcessControllerInput();

        if( dtwrScrTopLine > dtwrScrMenuTopLine )
          startOnLine = ( dtwrScrTopLine + (-dtwrScrMenuTopLine) ) & 255;

        minSelectLine = ( startOnLine + 1 ) & 255;
        maxSelectLine = minSelectLine;

        if( dtwrScrSelectLine >= dtwrScrTopLineLimit )
        {
            if( dtwrScrTopLineLimit > dtwrScrMenuTopLine )
              maxSelectLine = ( dtwrScrSelectLine + (-dtwrScrMenuTopLine) + 1 ) & 255;
            else
              maxSelectLine = ( dtwrScrSelectLine + (-dtwrScrTopLineLimit) + 1 ) & 255;

            minSelectLine = maxSelectLine;
        }

        if( remainingLines >= 4 )
        {
            maxSelectLine = ( maxSelectLine + remainingLines - 3 ) & 255;
            minSelectLine = maxSelectLine;
        }

        dtwrMenuSetMinMaxLineForSelection( minSelectLine, maxSelectLine );
        dtwrScrDisplayAdjustForMenuJustInView( remainingLines );
        dtwrMenuPrint( startOnLine, remainingLines );
    }
    else
    {
        // Real upstream's own comment: "Stupid hack due do doing too much in
        // one frame - button presses would otherwise be detected for other
        // things."
        if( dtwrScrTopLine + dtwrScrHeight <= dtwrScrMenuTopLine )
          if( dtwrScrShouldJumpToMenu() )
            dtwrScrJumpToMenu();
    }
}

void dtwrScrDisplayEvent()
{
    dtwrScrDisplayDescription();
    dtwrScrDisplayMenuHeadingBlock();
    dtwrScrDisplayMenu();
}

void dtwrScrUpdateDisplay()
{
    if( dtwrScrShouldScrollDown() )
      dtwrScrScrollDown();
    if( dtwrScrShouldScrollUp() )
      dtwrScrScrollUp();

    dtwrScrDisplayEvent();
}

// -----------------------------------------------------------------------
//   game_presenter_type
// -----------------------------------------------------------------------

DtwrEvent* dtwrCurrentEvent()
{
    return &dtwrEvents[ dtwrEventStackPos ];
}

void dtwrCopyEvent( DtwrEvent* dst, DtwrEvent* src )
{
    dst->kind = src->kind;
    dst->description = src->description;
    dst->allowActions = src->allowActions;
    dst->returnToPrevious = src->returnToPrevious;
    dst->localTags = src->localTags;
}

void dtwrSaveCurrentScreenScrollPosition()
{
    dtwrEventsScrollPos[ dtwrEventStackPos ] = dtwrScrGetScrollPosition();
}

void dtwrClearCurrentSavedScreenScrollPosition()
{
    dtwrEventsScrollPos[ dtwrEventStackPos ] = 0;
}

int dtwrGetCurrentSavedScreenScrollPosition()
{
    return dtwrEventsScrollPos[ dtwrEventStackPos ];
}

void dtwrLoadNewEvent( DtwrEvent* newEvent )
{
    dtwrSaveCurrentScreenScrollPosition();
    dtwrEventStackPos = ( ( dtwrEventStackPos + 1 ) & 255 ) % DTWR_EVENT_MEMORY;
    dtwrCopyEvent( &dtwrEvents[ dtwrEventStackPos ], newEvent );
    dtwrClearCurrentSavedScreenScrollPosition();
}

// Real upstream's own `( --event_stack_pos ) %= EVENT_MEMORY;` - the `& 255`
// reproduces the real uint8_t underflow this relies on (see header comment)
void dtwrLoadPreviousEvent()
{
    dtwrEventStackPos = ( ( dtwrEventStackPos - 1 ) & 255 ) % DTWR_EVENT_MEMORY;
}

void dtwrSwitchToActionMenu()
{
    if( dtwrCurrentEvent()->allowActions )
    {
        dtwrScrSetMenuTitle( "Select action" );
        dtwrMenuLoad( DTWR_MENU_SRC_ACTIONS, 4 );
        dtwrMenuSelHandler = DTWR_H_ACTION_SEL;
        dtwrMenuCancelHandler = DTWR_HC_NONE;
    }
    else
    {
        dtwrScrSetMenuTitle( NULL );
        dtwrMenuLoad( DTWR_MENU_SRC_CONTINUE, 1 );
        dtwrMenuSelHandler = DTWR_H_CONTINUE_SEL;
        dtwrMenuCancelHandler = DTWR_HC_CONTINUE;
    }

    dtwrScrRecalculateLineCount();
}

void dtwrSwitchToEvent()
{
    dtwrScrLoadEvent( dtwrCurrentEvent() );
    dtwrSwitchToActionMenu();
    dtwrScrSetScrollPosition( dtwrGetCurrentSavedScreenScrollPosition() );
}

void dtwrSwitchToObjectMenu()
{
    dtwrScrSetMenuTitle( "Which object?" );
    dtwrMenuLoad( DTWR_MENU_SRC_OBJECTS, dtwrObjectMenuCount( dtwrCurrentEvent() ) );
    dtwrScrRecalculateLineCount();
    dtwrMenuSelHandler = DTWR_H_OBJECT_SEL;
    dtwrMenuCancelHandler = DTWR_HC_OBJECT_CANCEL;
}

void dtwrSwitchToObjectForItemMenu()
{
    dtwrScrSetMenuTitle( "On what?" );
    dtwrMenuLoad( DTWR_MENU_SRC_OBJECTS, dtwrObjectMenuCount( dtwrCurrentEvent() ) );
    dtwrScrRecalculateLineCount();
    dtwrMenuSelHandler = DTWR_H_OBJECT_FOR_ITEM_SEL;
    dtwrMenuCancelHandler = DTWR_HC_OBJECT_CANCEL;
}

void dtwrSwitchToItemMenu()
{
    DtwrEvent emptyItemMenu;
    int menuLength = dtwrItemMenuCount();

    if( menuLength > 0 )
    {
        dtwrScrSetMenuTitle( "Use what?" );
        dtwrMenuLoad( DTWR_MENU_SRC_ITEMS, menuLength );
        dtwrScrRecalculateLineCount();
        dtwrMenuSelHandler = DTWR_H_ITEM_SEL;
        dtwrMenuCancelHandler = DTWR_HC_OBJECT_CANCEL;
    }
    else
    {
        dtwrMakeGeneric( &emptyItemMenu, "You are carrying no useful items!" );
        dtwrLoadNewEvent( &emptyItemMenu );
        dtwrSwitchToEvent();
    }
}

void dtwrHandleObjectForItemMenuSelection( int selection )
{
    DtwrEvent nextEvent;

    dtwrProcessItemOnObject( dtwrCurrentEvent(), dtwrSelectedItem, selection, &nextEvent );
    dtwrLoadNewEvent( &nextEvent );
    dtwrSwitchToEvent();
}

void dtwrHandleObjectMenuSelection( int selection )
{
    DtwrEvent nextEvent;

    dtwrProcessActionOnObject( dtwrCurrentEvent(), dtwrSelectedAction, selection, &nextEvent );
    dtwrLoadNewEvent( &nextEvent );
    dtwrSwitchToEvent();
}

void dtwrHandleItemMenuSelection( int selection )
{
    dtwrSelectedItem = dtwrGetItemByIndex( selection );
    dtwrSwitchToObjectForItemMenu();
}

void dtwrHandleActionMenuSelection( int actionSelected )
{
    if( actionSelected == DTWR_ACTION_ITEM )
      dtwrSwitchToItemMenu();
    else
      dtwrSwitchToObjectMenu();

    dtwrSelectedAction = actionSelected;
}

void dtwrHandleActionMenuContinue()
{
    DtwrEvent nextEvent;

    if( dtwrCurrentEvent()->returnToPrevious )
      dtwrLoadPreviousEvent();
    else
    {
        dtwrGetContinueEvent( dtwrCurrentEvent(), &nextEvent );
        dtwrLoadNewEvent( &nextEvent );
    }

    dtwrSwitchToEvent();
}

void dtwrHandleObjectMenuCancel()
{
    dtwrSwitchToActionMenu();
}

void dtwrPresLoadFirstEvent()
{
    DtwrEvent firstEvent;

    dtwrEventStackPos = 0;
    dtwrScrSetScrollPosition( 0 );
    dtwrMakeIntro( &firstEvent );
    dtwrLoadNewEvent( &firstEvent );
}

void dtwrPresInit()
{
    dtwrPresLoadFirstEvent();
    dtwrSwitchToEvent();
}

void dtwrPresUpdate()
{
    dtwrScrUpdateDisplay();
}

// -----------------------------------------------------------------------
//   Title screen - upstream's own blocking gb.titleScreen() as a real
//   resumable state (see this file's own header comment)
// -----------------------------------------------------------------------

void dtwrUpdateTitle()
{
    gbSetFont( gbFont3x5 );

    gbCursorX = 14;
    gbCursorY = 14;
    gbPrintString( "The Dark Tower" );

    gbCursorX = 22;
    gbCursorY = 30;
    gbPrintString( "Press A" );

    if( gbPressed( BTN_A ) )
    {
        dtwrGameState = DTWR_STATE_INIT;
        gbSetFont( gbFont5x7 );
    }
}

// -----------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------

void gameDarkTower_init()
{
    int i;

    gbBegin();

    dtwrGameState = DTWR_STATE_TITLE;

    dtwrItemsCarried = 0;
    dtwrAchievements = 0;

    dtwrEventStackPos = 0;
    dtwrSelectedAction = 0;
    dtwrSelectedItem = 0;

    for( i = 0; i < DTWR_EVENT_MEMORY; i = i + 1 )
    {
        dtwrEventInitDefault( &dtwrEvents[ i ] );
        dtwrEventsScrollPos[ i ] = 0;
    }

    dtwrDescBuf[ 0 ] = 0;
    dtwrDescWidthInChars = 0;

    dtwrMenuSource = DTWR_MENU_SRC_ACTIONS;
    dtwrMenuCount = 0;
    dtwrMenuSelected = 0;
    dtwrMenuWidthInChars = 0;
    dtwrMenuScrollDelay = 5;
    dtwrMenuMinLine = 0;
    dtwrMenuMaxLine = 255;
    dtwrMenuSelHandler = DTWR_H_NONE;
    dtwrMenuCancelHandler = DTWR_HC_NONE;

    dtwrScrLineCount = 0;
    dtwrScrTopLine = 0;
    dtwrScrTopLineLimit = 0;
    dtwrScrSelectLine = 0;
    dtwrScrSelectLineLimit = 0;
    dtwrScrMenuTopLine = 0;
    dtwrScrHeight = 0;
    dtwrScrWidth = 0;
    dtwrScrScrollDelay = 4;
    dtwrScrEventScrollUp = false;
    dtwrScrEventScrollDown = false;
    dtwrScrMenuTitle = NULL;
}

void gameDarkTower_update()
{
    if( !gbUpdate() ) return;

    if( dtwrGameState == DTWR_STATE_INIT )
    {
        dtwrPresInit();
        dtwrGameState = DTWR_STATE_PLAY;
    }
    else if( dtwrGameState == DTWR_STATE_PLAY )
    {
        dtwrPresUpdate();

        if( gbPressed( BTN_C ) )
          dtwrGameState = DTWR_STATE_TITLE;
    }
    else if( dtwrGameState == DTWR_STATE_TITLE )
      dtwrUpdateTitle();

    gbRenderFrame();
}
