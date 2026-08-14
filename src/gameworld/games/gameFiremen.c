#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Firemen (Vicking69, GPLv2 - github.com/Vicking69/firemen). A tiny
// timed arcade game: a person ("suicide" in upstream's own real variable/
// asset naming - a jumper falling from a burning building) drifts down
// and to the right across the screen; the player slides a fireman-held
// catch net ("pompier") left/right along the bottom to bounce the jumper
// back upward before they fall, with the real goal being to steer them
// into a stationary ambulance (bottom-right) for +1 score each time. Two
// windows on the building flicker with fire animation throughout. A
// single 1000-tick (50-second real-hardware) countdown timer ends the
// round; a game-over screen then shows the score/highscore (persisted via
// real EEPROM) and offers replay (A/B) or exit to the title screen (C).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment for the full reasoning).
// `gb.display.println(...)` became `gbPrintString()`/`gbPrintNumber()`
// (no function overloading in this dialect, so real Print's single
// overloaded print/println needed splitting into two, same as every
// other port in this project).
//
// PRINTLN CASCADE, resolved by hand: upstream's own end_page_display()
// leans on real Print::println()'s own automatic "advance cursorY by one
// font line, reset cursorX to 0" behavior between several of its own
// println() calls, WITHOUT re-setting cursorX/cursorY explicitly every
// time (e.g. `cursorX=5; println("High score : "); cursorX=30;
// println(highscore);` - no cursorY set before that second call, so the
// number lands on the line println() itself auto-advanced to, not the
// same line as the label). This shim's own gbPrintString() only auto-
// advances on an explicit '\n' character inside the string it's passed,
// not automatically after every plain call (see gameFlappyBirdo.c's own
// header comment on this exact same shim behavior) - so every one of
// upstream's implicit line advances here was resolved by hand ahead of
// time (font stays the real default 3x5 the whole game - upstream never
// calls setFont() - so one line = gbFontHeight = 6px) and baked into an
// explicit gbCursorY value at each call site below, landing on the exact
// same real per-line Y position upstream's own println() cascade would
// have produced. This reproduces a real, slightly odd upstream layout
// quirk on purpose rather than "fixing" it: the very last line ("Other
// touch - Replay") never gets its own cursorX reset the way the line
// above it ("Press C - Exit", explicitly placed at cursorX=3) did, so on
// real hardware it actually prints flush against the left edge (cursorX
// 0, from the previous line's own auto-reset) instead of lining up with
// the line above it - preserved exactly as upstream wrote it.
//
// EEPROM: upstream genuinely calls `EEPROM.read()`/`EEPROM.get()`/
// `EEPROM.put()` for a real persisted highscore (address 0), so this port
// genuinely uses this project's own eepromShim.h (`eeprom_read_byte()`/
// `eeprom_read_word()`/`eeprom_write_word()`) rather than inventing
// persistence upstream doesn't have. `EEPROM.get(0, highscore)`/
// `EEPROM.put(0, highscore)` operate on a real AVR `int` (16-bit on real
// hardware, a 2-byte read/write) - ported to this shim's own word-sized
// `eeprom_read_word()`/`eeprom_write_word()` primitives, the direct
// equivalent. The real fresh-EEPROM check (`EEPROM.read(0)==0xff`, real
// factory-erased AVR EEPROM reads as 0xFF) is preserved exactly via
// `eeprom_read_byte(0)==0xff` (this shim's own fresh cells also default
// to 0xFF, matching real hardware - see eepromShim.h's own header
// comment) - fireHighscore itself already starts at its own explicit `0`
// initializer either way, mirroring real hardware's own zero-initialized
// BSS global `int highscore;` upstream never explicitly assigns before
// this check runs.
//
// TITLE SCREEN, upstream's own two blocking `gb.titleScreen(F(...),
// casque)` calls (once in setup(), and again from inside the play loop as
// a genuine "pause" gesture on a Button B press) - converted into an
// explicit FIRE_STATE_TITLE/FIRE_STATE_PAUSED state pair (the "blocking
// loop -> explicit resumable state" treatment used throughout this
// project, see gamePong.c's own header comment), both rendered by one
// shared fireDrawTitleScreen() helper (upstream's own titleScreen() is
// itself one shared real library function called with two different name
// strings from two different call sites - this mirrors that directly).
// Dismissed by a genuine fresh `gbPressed(BTN_A)`, matching real
// `Gamebuino::titleScreen()`'s own real dismiss condition (confirmed by
// reading the real bundled `Gamebuino.cpp` source directly, at
// `more games/Gamebuino-Classic/Gamebuino.cpp`, rather than assumed):
// `buttons.pressed(BTN_A)`. That same real source also draws the real
// generic Gamebuino boot-splash logo (`gamebuinoLogo`, pure hardware
// branding, no bytes for it were ever staged for this project) plus a
// blinking bottom-right A/B/C button hint built from low-ASCII icon
// glyphs (`\25 \20`-style) and a real Button-B "cycle sound volume"
// interaction inside the generic titleScreen() chrome itself - none of
// that generic chrome is reproduced here, matching this project's own
// established precedent for every other port that goes through a
// titleScreen()-shaped state (gamePong.c/gameTaquin.c/
// gameFlappyBirdo.c all made the same simplification already): only the
// real game-specific content (the title name text, the real casque
// bitmap, and an A-button dismiss prompt reduced to plain "PRESS A" text
// instead of the real blinking icon glyphs) is kept. The real function's
// own actual logo placement math (name text at cursorX=0/cursorY=12, the
// passed-in logo bitmap at y = 12 + fontHeight when a name is given) was
// read directly from the source above but not reused verbatim here,
// since that spacing exists specifically to leave room for the real boot
// logo drawn above it - with that logo skipped, a simpler top-down custom
// layout (name, then "PRESS A", then the real casque bitmap, still ending
// exactly at the screen's own real bottom edge since 14+30=44 < 48) reads
// more cleanly on the real 84x48 canvas.
//
// SAME-TICK C-QUIT QUIRK - FIXED, NOT PRESERVED. Found by tracing real
// button-edge semantics carefully rather than assumed: upstream's own
// play-loop checks `gb.buttons.pressed(BTN_C)` once to decide whether to
// call `end_page_display()` at all - and `end_page_display()` itself then
// checks the exact same `gb.buttons.pressed(BTN_C)` a SECOND time, in the
// very same real tick (a plain synchronous function call, not a fresh
// `gb.update()` cycle in between - real `Buttons::pressed()` stays true
// for a button's entire "just transitioned" tick, however many times it's
// read that tick). The real, if probably accidental, consequence: quitting
// mid-game via Button C never actually shows the player the "GAME OVER"
// screen at all - the same C-press that triggers it also immediately
// satisfies `end_page_display()`'s own internal "C pressed -> go to the
// title screen" check, in the same tick, before a frame with the game-over
// text on it was ever actually presented. Flagged as a genuine negative
// player-experience bug and fixed: `fireUpdatePlay()` now only sets
// `fireState = FIRE_STATE_GAMEOVER` and returns, a real deferred state
// transition rather than a same-tick synchronous call - the next tick's
// own dispatch calls `fireUpdateGameOver()` once `gbPressed(BTN_C)` has
// naturally expired, so the game-over screen is always shown first.
//
// A real, load-bearing FLOAT PRECISION coincidence, checked rather than
// assumed: upstream's own fireman X-clamp (`if (PX!=67) PX=PX+1.5`) only
// actually stops exactly at 67 because 67-16=51 is an exact multiple of
// 1.5 (34 steps) - so repeated `+1.5` from the real starting position 16
// lands on precisely 67.0 with no float drift. Preserved exactly as
// upstream wrote it (a plain `!=`, not `>=`) rather than hardening it into
// a clamp, since it isn't actually a live bug on the real starting value
// this game always resets to.
//
// No real bitmap here is drawn with a separate solid fill/mask underneath
// it first (checked specifically for this, per this project's own
// established "look for a GRAY mask/fill pass before the real outline
// bitmap" bug class - see gameFlappyBirdo.c's own header comment for that
// bug's full history) - every one of firemen's own six sprites (casque/
// building/ambulance/pompier/suicide/flame) is a single self-contained
// filled silhouette with no upstream mask pass and no `setColor(GRAY)`/
// plain fillRect() anywhere in the real source at all, confirmed by
// reading both real .ino files directly. Real `GRAY` therefore never
// comes up in this port (nothing to substitute).
//
// Every real `const byte NAME[] PROGMEM = {w,h,B........,...}` array from
// sprites.ino was converted byte-for-byte into a plain `int[N] fireXxx
// Bitmap = {w,h,byte0,byte1,...}` array below (this dialect's own
// `int[N] name` array-declaration order, not C's `int name[N]`) via a
// small script reading the real sprites.ino source directly and
// converting each `B00000000`-style binary literal to decimal - verified
// by re-decoding every array's own bits back into an ASCII preview before
// trusting it (same discipline as gameFlappyBirdo.c's own bitmap
// restoration), not hand-transcribed.
//
// `gb.buttons.repeat(BTN_DOWN,2){}`/`gb.buttons.repeat(BTN_UP,2){}` -
// upstream's own real Up/Down handlers with empty bodies (the fireman
// only ever actually moves left/right) - preserved as bare `gbRepeat(...)`
// calls with their return values discarded, rather than deleted outright,
// since real `Buttons::repeat()` has real internal per-button timer state
// it updates on every call regardless of what the caller does with the
// result; dropping the calls entirely would be an invisible-but-real
// behavior change to that internal state. Vestigial/dead code either way -
// preserved rather than "cleaned up", matching this project's own default
// stance on real upstream quirks.
//
// `gb.pickRandomSeed()` was never actually called by this particular
// upstream sketch (firemen has no random behavior of its own at all - the
// jumper's path is fully deterministic), so gbPickRandomSeed() doesn't
// appear here either - nothing to port. `gb.battery.show = true;`/
// `gb.backlight.set(255);`/`gb.display.fontSize = 1;` were all dropped
// outright: the first two are purely cosmetic hardware-status chrome with
// no Vircon32 equivalent (same precedent as gamePong.c's own dropped
// `gb.battery.show = false;`), and `gbFontSize` already defaults to `1`
// from `gbBegin()` itself, making that particular assignment a genuine
// real no-op even on real hardware (the real `Display` constructor's own
// default is already 1).

int fireCasqueBitmap[242]= { 64, 30,
    0, 60, 31, 254, 63, 240, 0, 0,
    0, 48, 60, 3, 63, 252, 0, 0,
    0, 96, 112, 0, 255, 255, 0, 0,
    0, 193, 192, 15, 255, 255, 128, 0,
    1, 195, 128, 31, 255, 255, 224, 0,
    3, 143, 128, 31, 255, 224, 56, 0,
    3, 8, 0, 31, 240, 31, 200, 0,
    3, 8, 0, 31, 199, 255, 232, 0,
    3, 8, 0, 12, 56, 15, 248, 0,
    3, 8, 0, 56, 192, 15, 240, 0,
    3, 8, 0, 99, 0, 7, 224, 0,
    3, 8, 0, 28, 0, 3, 192, 0,
    3, 8, 0, 48, 0, 1, 128, 0,
    3, 4, 0, 96, 0, 0, 128, 0,
    3, 6, 0, 64, 0, 0, 128, 0,
    1, 131, 0, 64, 0, 0, 224, 0,
    0, 157, 1, 192, 0, 0, 224, 0,
    0, 127, 1, 0, 0, 0, 112, 0,
    0, 63, 1, 0, 3, 128, 240, 0,
    0, 49, 1, 0, 7, 241, 176, 0,
    0, 57, 1, 0, 7, 249, 48, 0,
    0, 31, 1, 240, 15, 255, 184, 0,
    0, 11, 129, 252, 31, 255, 168, 0,
    0, 26, 97, 255, 255, 255, 232, 0,
    0, 19, 225, 255, 255, 255, 200, 0,
    0, 19, 225, 255, 255, 255, 136, 0,
    0, 17, 1, 167, 255, 254, 8, 0,
    0, 17, 251, 48, 127, 252, 8, 0,
    0, 12, 2, 28, 0, 0, 8, 0,
    0, 15, 254, 6, 0, 0, 56, 0
};

int fireBuildingBitmap[98]= { 16, 48,
    255, 224,
    255, 224,
    255, 224,
    224, 224,
    224, 231,
    224, 231,
    224, 255,
    224, 255,
    255, 255,
    255, 224,
    224, 224,
    224, 231,
    224, 231,
    224, 255,
    224, 255,
    255, 255,
    255, 224,
    224, 224,
    224, 224,
    224, 231,
    224, 231,
    224, 255,
    255, 255,
    255, 255,
    224, 224,
    224, 224,
    224, 224,
    224, 231,
    224, 231,
    255, 255,
    255, 255,
    224, 255,
    224, 224,
    224, 224,
    224, 224,
    224, 224,
    255, 224,
    255, 224,
    224, 224,
    224, 224,
    224, 224,
    224, 224,
    224, 224,
    255, 224,
    255, 224,
    255, 224,
    255, 224,
    255, 224
};

int fireAmbulanceBitmap[10]= { 8, 8,
    24,
    124,
    238,
    199,
    239,
    255,
    126,
    102
};

int firePompierBitmap[10]= { 8, 8,
    66,
    231,
    66,
    102,
    90,
    66,
    66,
    165
};

int fireSuicideBitmap[10]= { 8, 8,
    56,
    11,
    107,
    255,
    255,
    107,
    11,
    56
};

int fireFlameBitmap[10]= { 8, 8,
    24,
    62,
    23,
    7,
    63,
    126,
    252,
    248
};

enum FireState
{
    FIRE_STATE_TITLE = 0,
    FIRE_STATE_PLAY = 1,
    FIRE_STATE_PAUSED = 2,
    FIRE_STATE_GAMEOVER = 3
};

int fireState;

// fireman ("pompier") catch position, real fixed size 8x8
float firePX, firePY;
int fireFireSize;

// jumper ("suicide") position/velocity, real fixed size 8x8
float fireSX, fireSY;
int fireSuicSize;
float fireSXd, fireSYd;

int fireHighscore = 0; // matches real global `int highscore;`'s own zero-initialized BSS default
int fireScore;
int fireTimer;
int fireAffFlame1, fireAffFlame2;

void fireInitVariables()
{
    firePX = 16; firePY = 40; fireFireSize = 8;
    fireSX = 16; fireSY = 0; fireSuicSize = 8;
    fireSXd = 0.5; fireSYd = 1;
    fireScore = 0; fireTimer = 1000; fireAffFlame1 = 10; fireAffFlame2 = 0;
}

void fireBeginTitle()
{
    fireState = FIRE_STATE_TITLE;
}

void fireBeginPlay()
{
    fireState = FIRE_STATE_PLAY;
}

void fireBeginPaused()
{
    fireState = FIRE_STATE_PAUSED;
}

// Shared real titleScreen()-equivalent layout - see this file's own
// header comment for why this doesn't reuse the real function's own exact
// pixel anchors (those existed to leave room for a real boot-splash logo
// this port never draws).
void fireDrawTitleScreen( char* nameText )
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( nameText );
    gbCursorX = 0;
    gbCursorY = 7;
    gbPrintString( "PRESS A" );
    gbDrawBitmap( 10, 14, fireCasqueBitmap );
}

void fireUpdateTitle()
{
    fireDrawTitleScreen( "FireMen" );

    if( gbPressed( BTN_A ) )
    {
        fireInitVariables();
        fireBeginPlay();
    }
}

void fireUpdatePaused()
{
    fireDrawTitleScreen( "FireMen - Paused" );

    if( gbPressed( BTN_A ) )
      fireBeginPlay();
}

// Defined before fireUpdatePlay() (which calls it directly, mid-frame -
// see that function's own header comment and this file's own "SAME-TICK
// C-QUIT QUIRK" writeup) since this dialect is compiled top-to-bottom
// with no separate function-prototype/forward-declaration mechanism -
// every other cross-function call in this file already only ever reaches
// backward/upward for that same reason.
void fireUpdateGameOver()
{
    gbSetColor( 1 );

    // see this file's own header comment ("PRINTLN CASCADE") for exactly
    // why each of these gbCursorY values is what it is
    gbCursorX = 15;
    gbCursorY = 3;
    gbPrintString( "GAME OVER !!!!" );

    // highscore valorisation
    if( fireScore > fireHighscore )
    {
        // save highscore
        fireHighscore = fireScore;
        eeprom_write_word( 0, fireHighscore );
    }

    gbCursorX = 5;
    gbCursorY = 10;
    gbPrintString( "High score : " );
    gbCursorX = 30;
    gbCursorY = 16;
    gbPrintNumber( fireHighscore );

    gbCursorX = 5;
    gbCursorY = 22;
    gbPrintString( "score : " );
    gbCursorX = 30;
    gbCursorY = 28;
    gbPrintNumber( fireScore );

    gbCursorX = 3;
    gbCursorY = 35;
    gbPrintString( "Press C - Exit" );
    gbCursorX = 0;
    gbCursorY = 41;
    gbPrintString( "Other touch - Replay" );

    if( gbPressed( BTN_C ) )
      fireBeginTitle();
    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
    {
        fireInitVariables();
        fireBeginPlay();
    }
}

void fireUpdatePlay()
{
    // End of time, OR out of game if C is pressed. Fixed here, not
    // preserved (see this file's own header comment, "SAME-TICK C-QUIT
    // QUIRK", for the full real-upstream mechanism this replaces): upstream
    // calls `end_page_display()` synchronously in the same tick the C-press
    // that triggers it is read, and `end_page_display()` itself re-checks
    // that same still-true `gb.buttons.pressed(BTN_C)` a second time,
    // immediately quitting to the title screen before the "GAME OVER"
    // screen was ever actually shown. Fixed by making this a genuine
    // deferred state transition instead of a synchronous call - the next
    // real tick's own dispatch (`gameFiremen_update()`) calls
    // `fireUpdateGameOver()` once `gbPressed(BTN_C)` has naturally expired,
    // so a Button-C quit now always shows the game-over screen first, same
    // as the timer-expiry path already did.
    if( ( fireTimer == 0 ) || gbPressed( BTN_C ) )
    {
        fireState = FIRE_STATE_GAMEOVER;
        return;
    }

    gbSetColor( 1 );

    // view the building
    gbDrawBitmap( 0, 0, fireBuildingBitmap );

    // view ambulance
    gbDrawBitmap( 75, 40, fireAmbulanceBitmap );

    // flame animation - alternates between two window positions every
    // ~10 frames (see this file's own header comment - a direct,
    // unmodified port of upstream's own real counter logic)
    if( fireAffFlame1 > 0 )
    {
        gbDrawBitmap( 16, 13, fireFlameBitmap );
        gbDrawBitmap( 10, 33, fireFlameBitmap );
        fireAffFlame1 = fireAffFlame1 - 1;
    }
    if( fireAffFlame1 == 1 ) { fireAffFlame2 = 10; fireAffFlame1 = 0; }

    if( fireAffFlame2 > 0 )
    {
        gbDrawBitmap( 14, 20, fireFlameBitmap );
        gbDrawBitmap( 16, 8, fireFlameBitmap );
        fireAffFlame2 = fireAffFlame2 - 1;
    }
    if( fireAffFlame2 == 1 ) { fireAffFlame1 = 10; fireAffFlame2 = 0; }

    // move and check position of the fireman (real soft clamp - see this
    // file's own header comment on why `!=67`/`!=18` actually holds exactly)
    if( gbRepeat( BTN_RIGHT, 2 ) )
    {
        if( firePX != 67 ) firePX = firePX + 1.5; else firePX = firePX;
    }
    if( gbRepeat( BTN_LEFT, 2 ) )
    {
        if( firePX != 18 ) firePX = firePX - 1.5; else firePX = firePX;
    }
    // Up/Down do nothing on real hardware either - see this file's own
    // header comment on why these calls are still made
    gbRepeat( BTN_DOWN, 2 );
    gbRepeat( BTN_UP, 2 );

    gbDrawBitmap( (int)firePX, (int)firePY, firePompierBitmap );

    // move and check position of the jumper
    if( fireSX < 75 )
      fireSX = fireSX + fireSXd;
    else
    {
        fireSX = 8; fireSY = 0; fireSYd = 1;
    }
    if( fireSY >= 40 )
    {
        fireSX = 8; fireSY = 0; fireSYd = 1;
    }
    else
      fireSY = fireSY + fireSYd;
    if( fireSY <= 18 ) fireSYd = 1;

    gbDrawBitmap( (int)fireSX, (int)fireSY, fireSuicideBitmap );

    // bounce off the fireman's own catch zone
    if( gbCollideRectRect( (int)fireSX, (int)fireSY, fireSuicSize, fireSuicSize, (int)firePX, (int)firePY, fireFireSize, fireFireSize ) )
      fireSYd = -1;

    // caught by the ambulance = a scored rescue
    if( gbCollideRectRect( (int)fireSX, (int)fireSY, fireSuicSize, fireSuicSize, 75, 40, 8, 8 ) )
    {
        fireScore = fireScore + 1;
        fireSX = 16; fireSY = 0; fireSYd = 1;
        gbPlayTick();
        gbPlayTick();
    }

    // view score
    gbCursorX = 73;
    gbCursorY = 1;
    gbPrintNumber( fireScore );

    // view timer
    fireTimer = fireTimer - 1;
    gbCursorX = 73;
    gbCursorY = 7;
    gbPrintNumber( fireTimer );

    // pause the game if B is pressed
    if( gbPressed( BTN_B ) )
      fireBeginPaused();
}

void gameFiremen_init()
{
    gbBegin();

    // highscore valorisation - real EEPROM.read(0)==0xff means a genuinely
    // fresh/never-written cell on real hardware (this shim's own fresh
    // cells default to 0xFF too - see eepromShim.h's own header comment)
    if( eeprom_read_byte( 0 ) == 0xff )
      eeprom_write_word( 0, 0 );
    else
      fireHighscore = eeprom_read_word( 0 );

    fireBeginTitle();
}

void gameFiremen_update()
{
    if( !gbUpdate() ) return;

    if( fireState == FIRE_STATE_TITLE ) fireUpdateTitle();
    else if( fireState == FIRE_STATE_PLAY ) fireUpdatePlay();
    else if( fireState == FIRE_STATE_PAUSED ) fireUpdatePaused();
    else fireUpdateGameOver();

    gbRenderFrame();
}
