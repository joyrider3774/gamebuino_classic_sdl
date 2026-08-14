#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Bub (smogheap, GPLv3 - https://smogheap.github.io/bub/, source also
// mirrored at https://gitlab.com/smogheap/bub per the staged repo's own
// README.md - the GitHub copy used for this port is a read-only mirror).
// A single-screen puzzle-platformer: guide "ork" around an 8x8-tile level,
// collecting bubbles (o) and keys (-) into a real 2-item inventory,
// unlocking doors (X) with keys, climbing ladders (H), pushing crates (=)
// with one-way kick tiles (< / >), to reach a flag (4) and complete the
// level. 100 real levels ship upstream, each with a normal and an "evil
// twin" (odd-indexed) variant - see bubLevels below.
//
// Real upstream is a 4-tab .ino (bub.ino/sprites.ino/sounds.ino/
// levels.ino) - consolidated here into one file, exactly like every other
// multi-tab port in this project (see gamePong.c's own header comment for
// the general rewrite pattern this project uses throughout). Every real
// `gb.x.y(...)` call site is mechanically rewritten to a plain `gbY(...)`
// call (no classes/methods in this dialect); every global/function got a
// `bub`-prefixed name (this cartridge has one flat namespace across 53+
// other already-ported games). `byte`/`boolean` all became plain `int`/
// `bool`; `char` locals holding a level-cell code stayed plain `int`
// (a "character" is just an int in this dialect - see this project's own
// CLAUDE.md dialect notes). `PROGMEM`/`pgm_read_byte()` are dropped
// outright (already-established no-ops here) - the underlying data tables
// are copied verbatim either way.
//
// REAL BITMAP ART: all 12 real sprites (title/orkstand/orkdown/orkup/
// bubble/wall/ladder/key/door/right/left/crate/flag, from sprites.ino) and
// the 2 tiny ok/ko checkmark icons (declared directly in bub.ino itself)
// were converted byte-for-byte from upstream's own `B00000000`-style
// Arduino binary literals to plain decimal ints via a one-off script (not
// hand-transcribed) - every real PROGMEM byte became one plain `int` cell,
// matching this project's own established bitmap convention
// ({width,height} header + gbDrawBitmap()'s own real row-major/MSB-first
// packed body). Every real `gb.display.drawBitmap(...)` call site has a
// direct `gbDrawBitmap()`/`gbDrawBitmapRotated()` counterpart - the ork's
// own draw call uses the rotated form since upstream passes a real
// NOROT/dir (FLIPH) pair to flip it when facing left.
//
// REAL LEVEL DATA: `levels.ino`'s own 901-line, 100-level `const char
// PROGMEM levels[]` (a real, adjacent-string-literal-concatenated 6400-
// character table, 64 chars/level) is copied verbatim below as
// `bubLevels`, a single flat `int[6401]` string literal - not abbreviated
// or randomly sampled, and not reproduced as separate adjacent string
// literals the way upstream's own source does it, since this dialect's
// own support for implicit adjacent-string-literal concatenation (a real
// standard-C feature) is unconfirmed and no other already-ported game in
// this project relies on it - joining all 100 levels' own real row strings
// into one single literal sidesteps the question entirely while keeping
// the exact same real character data upstream ships (verified directly:
// extracted and counted 6400 characters across exactly 100 levels via a
// script, not assumed).
//
// SOUND: upstream's own `sfx(fxno, channel)` drives a real low-level
// tracker envelope per call (`gb.sound.command(...)` for waveform/volume/
// volume-slide/pitch-slide, THEN a final `gb.sound.playNote(pitch,
// duration, channel)`) - `gb.sound.command()` is exactly the low-level
// tracker/pattern primitive this project's own CLAUDE.md already documents
// as out of scope for this first sound pass (see gameShipwrek.c's own
// header comment for the same real gap, hit and handled identically
// there first). Approximated here as a single `gbPlayNote(pitch,
// duration)` call using the real envelope table's own final pitch/duration
// values (`bubSoundFx[fxno][1]`/`[7]`) - `bubSfx()` below. The `channel`
// parameter upstream always passes as a literal `0` (single-channel
// upstream anyway) has no equivalent parameter here since `gbPlayNote()`
// has no channel argument (this shim's own single implicit channel design)
// - dropped, not a loss since upstream never varies it.
//
// REAL EEPROM USAGE: upstream genuinely persists which of the 100 levels
// have been completed (`levelsDone[13]`, a 100-bit table packed 8 bits/
// byte) plus the evil-twin toggle (1 byte) via real `EEPROM.read()`/
// `EEPROM.write()` - ported directly via this shim's own
// `eeprom_read_byte()`/`eeprom_write_byte()` (addresses 0-12 for the 13
// packed bytes, address 13 for the evil-twin flag), matching
// gameUfoRace.c's/gameShipwrek.c's own real EEPROM call pattern exactly.
// `eepromSelectGame()` (called centrally in portVircon32.c right before
// this game's own init() runs) already resolves/loads this game's own
// EEPROM slot, so `bubLoadLevelsDone()`/`bubWriteLevelsDone()` below need
// no extra wiring of their own. A genuine, preserved-not-fixed real
// upstream quirk: this shim's own fresh/unwritten EEPROM cells read back
// as 255 (0xFF), matching real AVR EEPROM's own factory-erased state
// (documented in eepromShim.h) - so on a truly fresh save slot,
// `bubIsLevelDone()` reads every level as "done" (every bit of 0xFF is
// set) and `bubEvilTwin` reads as true (255 is non-zero). Traced through
// rather than assumed harmless: `gameBub_init()`'s own starting-level scan
// (`while (i<100 && bubIsLevelDone(i)) i += ...`) then runs off the end of
// the loop (i reaches 100) on a fresh slot, exactly as it does on real
// fresh hardware, and both fall back to `bubLevel = 0` - i.e. a fresh
// cartridge and a fresh save slot produce the exact same real starting
// state, so this quirk is genuinely inert here, not silently wrong.
//
// REAL ICON GLYPHS - `showmenu()`'s own real level-select screen prints
// two non-printable octal-escape icon glyphs, `\21` (ASCII 17) and `\20`
// (ASCII 16), real Gamebuino font icons standing in for left/right
// selection carets - a quoted string literal in this dialect cannot hold
// a non-printable escape directly (see gameSimonbuino.c's own header
// comment for the same real gap, hit first there with a different glyph)
// so `bubIconLeft`/`bubIconLeftSp`/`bubIconRight` below are small explicit
// `int[]` arrays instead of quoted string literals.
//
// STATE-MACHINE CONVERSION: upstream's real control flow is built on two
// blocking calls - `gb.titleScreen(title)` (called from `setup()`, and
// again from `loop()` on a mid-game Button C press when not game-over) and
// `showmenu()` (an internal, always-true `while(true)` loop entered on
// Button B, with its own nested `if (gb.update())`) - converted here into
// an explicit `BUB_STATE_TITLE`/`BUB_STATE_PLAY`/`BUB_STATE_MENU` state
// machine, matching the same "blocking loop -> explicit resumable state"
// treatment gamePong.c's/gameShipwrek.c's own header comments document.
// `bubTitleReturnState` remembers whether Button C's title-screen pause
// was entered from PLAY or from MENU (upstream's own `showmenu()` has its
// own real Button-C handler that also calls `gb.titleScreen(title)` and
// then continues its own `while(true)` loop right where it left off - a
// real "pause and resume into the menu" gesture, not a hidden quit),
// exactly like real hardware's own behavior for this game.
//
// GENUINE UPSTREAM QUIRKS FOUND AND PRESERVED (not "fixed"):
// - `fall()`'s own real `char who = cell(fromx, fromy);` (plus the
//   `who = '@'` special-mode reassignment right after it) and `int origy =
//   y;` are both genuinely dead upstream locals - computed but never once
//   read anywhere else in the real function body. Dropped here (matching
//   gameShipwrek.c's own precedent for dropping a provably-dead upstream
//   local), not ported as unused variables.
// - `moveleft()`/`moveright()`'s own real `if (moveto(...)) { //sfx("step");
//   }` bodies are empty except for a commented-out line - real dead
//   branches, upstream itself never uncommented that sfx call. Reproduced
//   here as a plain unconditional `bubMoveTo(...)` call with the return
//   value ignored, which is exactly what the dead `if` amounts to.
// - Real `showmenu()`'s own level-wrap-on-decrement is asymmetric by
//   design, not a bug this port introduces: decrementing left from level 0
//   sets `selectedLevel = 99` directly with no further odd/even
//   adjustment, even though level 99 is an odd ("evil twin") index - so
//   with Evil Twin off, wrapping left from level 0 can genuinely land the
//   selector on level 99 anyway, one frame before the player would notice.
//   Reproduced exactly (`bubMenuSelectedLevel = 99;` with no extra
//   even/odd fixup after the wrap), matching real upstream's own
//   `if (selectedLevel < 0) selectedLevel = 99;` line for line.
// - Real `Gamebuino::EEPROM` byte semantics (see the EEPROM section above)
//   make `eviltwin`'s own real upstream toggle, `eviltwin = !eviltwin;`,
//   load-bearing beyond a simple 0/1 flip: on a fresh save, `eviltwin`
//   starts at 255 (truthy), and only real logical negation (`!`, not a
//   `1 - x` arithmetic flip) normalizes it down to a clean 0 the first
//   time the player actually toggles it in the menu. Ported as the exact
//   same `bubEvilTwin = !bubEvilTwin;` logical negation, not the
//   `1 - x`-style flip this project uses elsewhere for values already
//   known to always be clean 0/1 (e.g. gameShipwrek.c's own `boat_rot`).
// - The real game-over "flash" effect (`gameover` true fills the whole
//   screen solid BLACK via `setColor(WHITE, BLACK); fillScreen(BLACK);`,
//   then draws the ENTIRE level grid, the ork, and every UI element - the
//   vertical divider, level-number readout, and inventory box - all again,
//   now in WHITE ink on top of that black fill) is preserved exactly:
//   `bubDrawPlayScreen()` below draws the grid/ork/UI unconditionally
//   every tick regardless of `bubGameOver`, only the color/background
//   setup differs, matching upstream's own real draw-order exactly (the
//   game-over branch never skips or replaces any of the normal drawing,
//   it only recolors it).
//
// SHIM PRIMITIVES USED: gbDrawBitmap()/gbDrawBitmapRotated(),
// gbSetColor()/gbSetColorBg(), gbFillScreen() (real hardware's own
// documented "always fills solid BLACK regardless of the color argument"
// quirk - harmless here since upstream's own only call already passes
// BLACK literally), gbFont5x7/gbFont3x5 real fonts, eeprom_read_byte()/
// eeprom_write_byte(). No new shim primitive was needed and no gap was
// hit - every real upstream call site had a direct, already-existing
// counterpart.
//
// NOT PORTED: `gb.display.persistence` has no equivalent in this shim
// (dropped project-wide - see gameBlockdude.c's/game2048.c's own header
// comments for the established reasoning) - `gbUpdate()` always clears
// and fully recomposites every tick here, so `bubDrawMenu()` simply
// redraws its own background text every tick instead of once, with no
// observable difference. `gb.battery.show` has no equivalent either
// (dropped project-wide since gamePong.c's own port) and was dropped here
// too.

int bubTitleBitmap[290] =
{
    64, 36,
    15, 255, 128, 240, 15, 15, 255, 128, 15, 255, 192, 240, 15, 15, 255, 192,
    15, 255, 224, 240, 15, 15, 255, 224, 15, 255, 240, 240, 15, 15, 255, 240,
    15, 248, 240, 240, 15, 15, 248, 240, 15, 249, 240, 240, 15, 15, 249, 240,
    15, 255, 224, 240, 15, 15, 255, 224, 15, 255, 192, 255, 255, 15, 255, 192,
    15, 255, 192, 255, 255, 15, 255, 192, 15, 255, 224, 255, 255, 15, 255, 224,
    15, 249, 240, 255, 255, 15, 249, 240, 15, 248, 240, 255, 255, 15, 248, 240,
    15, 255, 240, 255, 255, 15, 255, 240, 15, 255, 224, 127, 254, 15, 255, 224,
    15, 255, 192, 63, 252, 15, 255, 192, 15, 255, 128, 31, 248, 15, 255, 128,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 126, 0, 0, 48, 0, 0, 0,
    1, 129, 129, 195, 91, 0, 0, 0, 2, 0, 66, 38, 156, 128, 0, 0,
    4, 0, 34, 165, 134, 24, 0, 0, 4, 0, 37, 158, 73, 36, 0, 0,
    8, 0, 21, 0, 52, 68, 0, 0, 8, 0, 20, 124, 7, 68, 0, 0,
    8, 0, 19, 132, 4, 136, 0, 0, 9, 0, 144, 2, 11, 8, 0, 0,
    4, 195, 32, 1, 28, 144, 0, 0, 4, 126, 32, 0, 224, 96, 0, 0,
    2, 0, 64, 1, 32, 0, 0, 12, 1, 129, 128, 2, 64, 0, 0, 60,
    0, 126, 0, 1, 32, 0, 0, 12, 0, 0, 0, 12, 160, 0, 0, 22,
    0, 0, 0, 19, 160, 0, 0, 22, 0, 0, 0, 32, 32, 0, 0, 23,
    0, 0, 0, 32, 64, 0, 0, 12, 0, 0, 0, 31, 128, 0, 0, 30
};

int bubOrkstandBitmap[8] =
{
    8, 6,
    56, 105, 127, 57, 40, 238
};

int bubOrkdownBitmap[8] =
{
    8, 6,
    12, 62, 122, 222, 136, 220
};

int bubOrkupBitmap[8] =
{
    8, 6,
    56, 16, 120, 92, 56, 238
};

int bubBubbleBitmap[8] =
{
    8, 6,
    28, 34, 65, 65, 34, 28
};

int bubWallBitmap[8] =
{
    8, 6,
    0, 254, 254, 0, 239, 239
};

int bubLadderBitmap[8] =
{
    8, 6,
    66, 126, 66, 66, 126, 66
};

int bubKeyBitmap[8] =
{
    8, 6,
    0, 96, 240, 191, 242, 103
};

int bubDoorBitmap[8] =
{
    8, 6,
    62, 99, 99, 119, 119, 127
};

int bubRightBitmap[8] =
{
    8, 6,
    24, 116, 66, 116, 24, 24
};

int bubLeftBitmap[8] =
{
    8, 6,
    24, 46, 66, 46, 24, 24
};

int bubCrateBitmap[8] =
{
    8, 6,
    126, 66, 90, 66, 66, 126
};

int bubFlagBitmap[8] =
{
    8, 6,
    24, 30, 24, 16, 16, 56
};

// {waveform, pitch, pmd/pmt-ish, vmt, vmd, vol-slide, volume, length} - real
// upstream envelope table (sounds.ino); only [1] (pitch) and [7] (length)
// are used here (see this file's own header comment on bubSfx()'s
// approximation of the real tracker envelope).
int bubSoundFx[7][8] =
{
    {0,3,116,1,6,3,4,3},   // oof
    {0,27,53,1,1,1,4,5},   // slurp
    {0,23,60,1,1,1,4,5},   // plop
    {0,16,28,1,1,2,4,20},  // restart
    {0,30,63,3,1,1,4,5},   // door
    {0,24,1,1,1,4,4,9},    // flag
    {1,16,56,1,1,1,4,3},   // cough
};

// Real ok/ko checkmark icons (declared directly in bub.ino, not
// sprites.ino) - byte-for-byte identical to the `dudeOkBitmap`/
// `dudeKoBitmap`/`whgOkBitmap`/`whgKoBitmap` tables already in this
// project's own gameBlockdude.c/gameWhg.c, both of whose own header
// comments note upstream's own "from bub" attribution - this is that
// same real bitmap, ported at its real source for the first time here.
int bubOkBitmap[9] = { 8, 7, 0x2, 0x4, 0x88, 0x48, 0x50, 0x30, 0x20 };
int bubKoBitmap[9] = { 8, 7, 0x82, 0x44, 0x28, 0x10, 0x28, 0x44, 0x82 };

// Real upstream levels.ino (901 lines, 100 levels x 64 chars = 6400
// characters total) - copied verbatim as one flat string literal (see this
// file's own header comment on why this isn't broken into separate
// adjacent string literals the way upstream itself does it). Level N's own
// 8x8 grid occupies characters [N*64, N*64+64) - row-major, 8 chars/row,
// 8 rows. Cell codes: ' '=empty, '#'=wall, 'H'=ladder, 'o'=bubble,
// '-'=key, 'X'=locked door, '<'/'>'=one-way crate-kick tile, '='=crate,
// '4'=flag (goal), '@'=ork's own real starting position (consumed by the
// very first real draw after a level loads - see bubDrawPlayScreen()'s
// own comment on this).
char bubLevels[6401] =
"         >      ###            4      ##oo @ ###################         >      ###            4      ##oo @    ################            4     H###H   H   H   H     @ H o o ################            4      ###H       H         @   o o ################                @       o       oo    4 ooo  ###oooo ###########                                @     4 o    ###o    ###########4       #H       H # o   H # o  ######H#      H o @ # H ########@       #H      oH #    oH #    ######H#      H   4 # H ########4       #######  oo      #######     oo ####### @oo     ########4       ######o   o      o######     o  ######o @ o     ########     4      ###Ho    ##Ho @   XH#####H#######H####-  H##########     4      ###Ho    ##Ho @   XH#####H#######H####o  H##########      4     @ #H  # - #H  #####H    X  H o- #  H################     #4   X @ #H  # oo#H  #####H    X  H-o- #   ################          =     H##     H       H@=   4 #### ####### ###########  =       o     H##     H       H@    4 #### ####### ###########     4   H## ##H H #   H H #   H@H #  o #  #  o #  #   o########   X 4X  H## ##H H #  -H H #   H@H #  o o  #  o o -#   o######## -  4   H## # #HH   ###H -     H #H#   H  H @   #####H  oooXXH## -  4 X H## # #HH   ###H -     H #H#   H  H @   #####H  oooXXH-#    o      o  #   o  ##  o  ##  o  ##   o@##     ##     ##4         o      o  #   o  ##  o  ##  -  ##   o@##     ##     ##4X             o    #   o @ # #######          4   =  ### ##H##   =oH#-     X  o    #   o @ # #######          4   =  ##  ##H##   =oH#########################        o @ >  4###### #########################################o   =   o @ >  4###### ####### ####### #- <  >   ##H###  @ H  o ####  ##          ######    X 4 ### ####- <  >   ##H###  @ H  o ####  ##          ######    = 4 ### X###             =  4 =  =  ##=##=#H  =  = H  =  o H  o @  H########             =  4 =  =  ##=# =#H  =  = H  =  o H  o @  H######## @=     H##     H       H##   4 H    ###H       H = o o HHHHH###  =     H #     H@      H##   4 H    ###H       H = o o H#HHH###         HHHHHH  H    H  H HH H  H 4H H  H  H H  HHHH H       H@         oooooo  o    o  o oo o  o 4o o  o  o o  oooo o       o@ @=    -##HHH  o    ## o    #       #### -   ###     XX4     ### @=    o #HHH  o    ## o    #-   -  ####     ###     XX4     ###########-   o    H ## H H # 4# H H#X #H H o#H  HH   @  H################o  #-    H ## H H # 4# H H#X #H H o#H  HH   @  H########                   @      oooo   oo ooo oo oooooooooo4oo########                   @      oooo  ooo<ooo  o>oooooooooo4oo######## -  - o  ----4o  -  -oo  ----@o   H  H    H##H    H  H    HHHH   o  o #  oooo4#  o  o##  oooo@#   =  =    =##=    =  =    ====  #####################      ooooo@ ooooo4########################################   #####  ooooo @oooooo4############################    - @    o####       # H##   # H o  =X  H  ###      4#########    - @    o####     o # H##oo # H o  =X  H  ###     o4########### @   Xoo # o#### 4oo   # H###H # H    -# HH#####  H    <  ###### @    o# #  #### 4oo   o ####H # H    o# HH#####  H       @o      Ho= = =4Ho= = = Ho= = = Ho= = = Ho= = = Ho= = = Ho= = = @o      Ho= = = Ho= = =4H = = = H== = = H== = = H==== = H==== =    -      ooo    ooooo  #     #H # X # H  #4#  H   #   H@      H   -      ooo    o   o  #     #H # X # H  #4#  H   #   H@  o   H   H 4 #   H ###   H       H#      H       H      oH      oH@      H 4 #   H ###   H       H       H       H      oH      oH@   #       ##     ####   ##4X   ######H = @## H ####     ##  ooo -##       ##     ####   ##4X   ######H = @## H ####     ##  oo-  #  X4    #H###    H       H       H     -      o  @  oooo########  X4    #####    H       H       H     -      o  @  oooo########      =  =   ##HH##    HH  = @ HH# # # HH##### HH ###- HH X4#  H   -  =  -   ##HH##o  oHH    @ HH#   # HH## ## HH ###- HHXXX4# H HHHHH   H  = =  H  #### H=      H#     @H     4###   ########## HHHHH   H  = o  H  #### H=      H#     @H     4###   #####  ### #  -    #       X   #H# ###  H  #  @ H  # o# H 4# o  H ######## ## -    ##    o XX  #H# ###  H  #- @ H  # o# H 4#    H ######## o       o=   = H##  ##HHo     HH   ####H@     4## # ##### # ### o    -  o=   = H##  ##HH-     HH   ####H@   XX4## # ##### # ###4ooooooooo  oooo oooo o o oooo oooo ooooooo o oo ooooooo oo@oo o4ooooooooo  oooo #ooo o o oooo oooo ooooooo o oo ooooooo oo@oo o    4       H    o o o   # # # #o o o   # # # #  @   oo ########    4       H   o        # # # #o o     # # # #  @   oo ########    =       =    =  =   H# H# ##H  H  X4H# H# ##H@ H   -## ## ## =  =    =  =    =  =   H# H# ##H  H  X4H# ##  #H@ o   -## ## ##       4     ###   o o     o         o   o      @   o   ########       4     ###      o     ooo   o oo   oo     @   o   ########o   # 4  H# # # H - # X H   o##H H o   HH o    HH      @########    # 4  H# # # H - # X H   o##H H o   HH o    HH      @########@      4##o   ##  o       o       o       o       o       o     @      4##    ##                                        ooooooo      =     ##=#   ###=## o###=## =@  o 4 o### ##   ## # o            =     ##=#   ###=## o###=## =@  oX4 o### ##   ## # o    -  @ -X-X-XX--XX--  X---XXX####### ----      -       ######  XXXXX4@o-X-X-XX--XX--  X---XXX####### ----      -            #  XXXXX4 XXXXXX4H#######H  --   H    -  H   --  H   -   o       @   o    XX>XXX4H#######H  --   H    -  H   <-  H   -   o       @   o   @  > o  #oH#  o ##### #  4#=### H##=  < H#=X ##   o H-#   # H###@  > o  #-H#  - ##### #  4#=### H##=  <  #oX ##   o H-X   # Ho##@      4oooooo           ooooooo        ooooooo          ooooooo@      4o#o#o#           ooo#o#o        o#o#ooo          ooo#o#o    4       H            o     o o # # oo  # #o  H## ##H H <@> H    4                    o     o o = = oo  # #o  Hoo ooH H <@> H 4 ##    -###      ##@    # ##    # ##    # ###    oo     oooo   @ o#    ####     -##     # ##    # ##4   # ###    oo     oooo  ####@###### H ##4X  H  X### H H#- # H Ho#H oo H##H    H##H##########@###### H ##4X  H  X### H H#- # H Ho#H  o H##H    H##H######    = 4   H## #H oHo# #H o o< #HH#### #HH  >=  HH####  #H @##  #-   = 4   H## #H -H-# #H o o< #HH###  #HH  >=  XH####  #H @##  #-   # -   o X     # ###HH###oHHHH   # # H  o# ## @ o# X4########-   # -   o X     # ### H###oHH H     H H  o  ## @ o  X4##### ##       4      ##            ##            ## ooo@    ooo########-     X4      ##            ##            ## ooo@    ooo########";

// -----------------------------------------------------------------------
// Constants / state
// -----------------------------------------------------------------------

#define BUB_TILE_W 8
#define BUB_TILE_H 6
#define BUB_NB_LEVELS_DONE 13

enum BubState
{
    BUB_STATE_TITLE = 0,
    BUB_STATE_PLAY = 1,
    BUB_STATE_MENU = 2
};

int bubState = BUB_STATE_TITLE;
int bubTitleReturnState = BUB_STATE_PLAY;

int bubDir = 0;      // NOFLIP(0) / FLIPH(1) - which way the ork sprite currently faces
int bubLevel = 0;
int bubLvlData[64];  // the current level's own live, mutable 8x8 grid (loaded from bubLevels)
int* bubOrk;         // which of the 3 real ork sprites is currently shown (stand/down/up)
int bubOrkX = 0;
int bubOrkY = 0;
int bubFlagX = 0;
int bubFlagY = 0;
int bubMaxInv = 2;
int bubBubs = 0;
int bubKeys = 0;
int bubGameOver = 0;
int bubEvilTwin = 0;

int bubLevelsDone[13];

bool bubMenuEvilSelected = false;
int bubMenuSelectedLevel = 0;

// Real Gamebuino icon glyphs (octal \21/\20, ASCII 17/16) used by the
// level-select menu - see this file's own header comment on why these are
// explicit int arrays[] rather than quoted string literals.
char bubIconLeftSp[3] = { 17, 32, 0 }; // icon + space (level-row selector, selected)
char bubIconLeft[2] = { 17, 0 };       // icon alone (evil-twin-row selector, selected)
char bubIconRight[2] = { 16, 0 };      // icon alone (either row's own right-hand marker)

// -----------------------------------------------------------------------
// Sound
// -----------------------------------------------------------------------

void bubSfx( int fxno )
{
    gbPlayNote( bubSoundFx[ fxno ][ 1 ], bubSoundFx[ fxno ][ 7 ] );
}

// -----------------------------------------------------------------------
// Level data access / mutation - direct ports of bub.ino's own cell()/
// isempty()/moveto()/fall()/allfall()/movedown()/moveup()/moveleft()/
// moveright()
// -----------------------------------------------------------------------

void bubLoadLevel( int level )
{
    int i;
    for( i = 0; i < 64; i = i + 1 )
      bubLvlData[ i ] = bubLevels[ ( level * 64 ) + i ];
    bubBubs = 0;
    bubKeys = 0;
    bubGameOver = 0;
}

int bubCell( int x, int y )
{
    if( x < 0 || x > 7 || y < 0 || y > 7 )
      return 0;
    return bubLvlData[ ( y * 8 ) + x ];
}

int bubIsEmpty( int c )
{
    if( c == ' ' || c == '4' )
      return 1;
    return 0;
}

int bubMoveTo( int x, int y, int fromx, int fromy )
{
    int stay = 0;
    int who = bubCell( fromx, fromy );
    int targ;

    if( fromx == bubOrkX && fromy == bubOrkY )
      who = '@'; // special mode

    targ = bubCell( x, y );

    if( who != '@' )
    {
        if( !bubIsEmpty( targ ) )
          stay = 1;
    }
    else
    {
        if( !targ || targ == '#' || targ == '=' )
        {
            bubSfx( 0 );
            return 0;
        }
        else if( targ == 'o' )
        {
            if( bubBubs + bubKeys < bubMaxInv )
            {
                bubLvlData[ ( y * 8 ) + x ] = ' ';
                bubSfx( 1 );
                stay = 1;
                bubBubs = bubBubs + 1;
            }
            else
            {
                return 0;
            }
        }
        else if( targ == '-' )
        {
            if( bubBubs + bubKeys < bubMaxInv )
            {
                bubLvlData[ ( y * 8 ) + x ] = ' ';
                bubSfx( 1 );
                stay = 1;
                bubKeys = bubKeys + 1;
            }
            else
            {
                return 0;
            }
        }
        else if( targ == 'X' )
        {
            if( !bubKeys )
            {
                bubSfx( 0 );
                return 0;
            }
            stay = 1;
            bubKeys = bubKeys - 1;
            bubLvlData[ ( y * 8 ) + x ] = ' ';
            bubSfx( 4 );
        }
    }

    if( stay )
      return 0;

    if( who != '@' )
    {
        bubLvlData[ ( y * 8 ) + x ] = who;
        bubLvlData[ ( fromy * 8 ) + fromx ] = ' ';
        if( fromx == bubFlagX && fromy == bubFlagY )
          bubLvlData[ ( fromy * 8 ) + fromx ] = '4';
    }
    else
    {
        bubOrkX = x;
        bubOrkY = y;
        if( targ == '4' )
        {
            bubGameOver = 1;
            bubSfx( 5 );
        }
    }
    return 1;
}

// Real upstream's own `who`/`origy` locals are dead here (computed, never
// read) - dropped, see this file's own header comment.
void bubFall( int fromx, int fromy )
{
    int x = fromx;
    int y = fromy;
    int below;
    int fell = 0;

    if( bubCell( x, y ) == 'H' )
      return;

    below = bubCell( x, y + 1 );

    while( below && bubIsEmpty( below ) )
    {
        if( bubMoveTo( x, y + 1, x, y ) )
        {
            y = y + 1;
            fell = fell + 1;
            below = bubCell( x, y + 1 );
        }
        else
        {
            below = 0;
        }
    }

    if( fell )
      bubSfx( 0 );
}

void bubAllFall()
{
    int x, y;
    for( y = 7; y >= 0; y = y - 1 )
    {
        for( x = 0; x < 8; x = x + 1 )
        {
            if( bubCell( x, y ) == '=' || ( x == bubOrkX && y == bubOrkY ) )
              bubFall( x, y );
        }
    }
}

int bubMoveDown()
{
    int moved = 0;
    int targ = bubCell( bubOrkX, bubOrkY + 1 );

    bubOrk = bubOrkdownBitmap;

    if( bubCell( bubOrkX, bubOrkY + 1 ) == 'H' )
    {
        bubMoveTo( bubOrkX, bubOrkY + 1, bubOrkX, bubOrkY );
        return 0;
    }
    else if( bubCell( bubOrkX, bubOrkY ) == '<' || bubCell( bubOrkX, bubOrkY ) == '>' )
    {
        return 0;
    }
    else if( !bubCell( bubOrkX, bubOrkY + 1 ) )
    {
        if( bubCell( bubOrkX, bubOrkY ) == 'H' )
        {
            bubFall( bubOrkX, bubOrkY );
            bubSfx( 0 );
            return 0;
        }
    }
    else if( bubCell( bubOrkX, bubOrkY ) == 'H' )
    {
        if( bubIsEmpty( targ ) )
        {
            moved = bubMoveTo( bubOrkX, bubOrkY + 1, bubOrkX, bubOrkY );
            bubFall( bubOrkX, bubOrkY );
        }
        return moved;
    }

    if( !bubBubs && !bubKeys )
    {
        bubFall( bubOrkX, bubOrkY );
        bubSfx( 6 );
        return 0;
    }

    // plop and climb
    targ = bubCell( bubOrkX, bubOrkY - 1 );
    if( !targ || ( !bubIsEmpty( targ ) && targ != 'H' && targ != '<' && targ != '>' ) )
    {
        bubSfx( 0 );
        return 0;
    }
    if( bubBubs )
    {
        bubLvlData[ ( bubOrkY * 8 ) + bubOrkX ] = 'o';
        bubBubs = bubBubs - 1;
        bubSfx( 2 );
    }
    else if( bubKeys )
    {
        bubLvlData[ ( bubOrkY * 8 ) + bubOrkX ] = '-';
        bubKeys = bubKeys - 1;
        bubSfx( 2 );
    }
    bubMoveTo( bubOrkX, bubOrkY - 1, bubOrkX, bubOrkY );
    bubFall( bubOrkX, bubOrkY );
    return 1;
}

void bubMoveUp()
{
    int moved = 0;
    bubOrk = bubOrkupBitmap;
    if( !bubCell( bubOrkX, bubOrkY - 1 ) )
    {
        bubSfx( 0 );
        return;
    }
    if( bubCell( bubOrkX, bubOrkY ) == 'H' &&
        ( bubCell( bubOrkX, bubOrkY - 1 ) == 'H' || bubIsEmpty( bubCell( bubOrkX, bubOrkY - 1 ) ) ) )
    {
        bubMoveTo( bubOrkX, bubOrkY - 1, bubOrkX, bubOrkY );
        return;
    }
    else if( !bubCell( bubOrkX, bubOrkY + 1 ) )
    {
        moved = bubMoveDown();
    }
    else if( bubCell( bubOrkX, bubOrkY + 1 ) != 'H' )
    {
        moved = bubMoveDown();
    }
    if( !moved )
    {
        bubOrk = bubOrkupBitmap;
        bubSfx( 6 );
    }
}

void bubMoveLeft()
{
    int kick = bubCell( bubOrkX - 1, bubOrkY );

    bubOrk = bubOrkstandBitmap;
    bubDir = 1; // FLIPH
    if( !bubCell( bubOrkX - 1, bubOrkY ) || bubCell( bubOrkX - 1, bubOrkY ) == '>' )
    {
        bubSfx( 0 );
        return;
    }
    if( kick == '=' )
    {
        bubOrk = bubOrkdownBitmap;
        bubSfx( 0 );
        bubMoveTo( bubOrkX - 2, bubOrkY, bubOrkX - 1, bubOrkY );
        bubAllFall();
        return;
    }
    bubMoveTo( bubOrkX - 1, bubOrkY, bubOrkX, bubOrkY );
    bubAllFall();
}

void bubMoveRight()
{
    int kick = bubCell( bubOrkX + 1, bubOrkY );

    bubOrk = bubOrkstandBitmap;
    bubDir = 0; // NOFLIP
    if( !bubCell( bubOrkX + 1, bubOrkY ) || bubCell( bubOrkX + 1, bubOrkY ) == '<' )
    {
        bubSfx( 0 );
        return;
    }
    if( kick == '=' )
    {
        bubOrk = bubOrkdownBitmap;
        bubSfx( 0 );
        bubMoveTo( bubOrkX + 2, bubOrkY, bubOrkX + 1, bubOrkY );
        bubAllFall();
        return;
    }
    bubMoveTo( bubOrkX + 1, bubOrkY, bubOrkX, bubOrkY );
    bubAllFall();
}

// -----------------------------------------------------------------------
// Level-completion tracking + EEPROM (see this file's own header comment)
// -----------------------------------------------------------------------

void bubSetLevelDone( int lvl )
{
    int i = lvl / 8;
    int bitShift = lvl % 8;
    bubLevelsDone[ i ] = bubLevelsDone[ i ] | ( 1 << bitShift );
}

bool bubIsLevelDone( int lvl )
{
    int i = lvl / 8;
    int bitShift = lvl % 8;
    if( ( ( bubLevelsDone[ i ] >> bitShift ) & 0x01 ) == 0x01 )
      return true;
    return false;
}

void bubLoadLevelsDone()
{
    int i;
    for( i = 0; i < BUB_NB_LEVELS_DONE; i = i + 1 )
      bubLevelsDone[ i ] = eeprom_read_byte( i );
    bubEvilTwin = eeprom_read_byte( BUB_NB_LEVELS_DONE );
}

void bubWriteLevelsDone()
{
    int i;
    for( i = 0; i < BUB_NB_LEVELS_DONE; i = i + 1 )
      eeprom_write_byte( i, bubLevelsDone[ i ] );
    eeprom_write_byte( BUB_NB_LEVELS_DONE, bubEvilTwin );
}

void bubNext()
{
    bubSetLevelDone( bubLevel );
    bubLevel = bubLevel + 1;
    if( !bubEvilTwin && ( bubLevel % 2 ) )
      bubLevel = bubLevel + 1;
    if( bubLevel >= 99 )
      bubLevel = 0;
    bubWriteLevelsDone();
    bubLoadLevel( bubLevel );
}

// -----------------------------------------------------------------------
// Drawing - direct port of bub.ino's own loop() draw section (see this
// file's own header comment on the real game-over "flash" effect this
// preserves) and menuBackground()/refreshMenu()
// -----------------------------------------------------------------------

void bubDrawPlayScreen()
{
    int i, j, ch;
    int* block;
    bool hasBlock;

    if( bubGameOver )
    {
        gbSetColorBg( 0, 1 ); // WHITE ink, BLACK background
        gbFillScreen( 1 );    // real hardware always fills solid BLACK regardless of the argument
    }
    else
    {
        gbSetColorBg( 1, 0 ); // BLACK ink, WHITE background
    }

    // Draw the level grid. Real upstream also (re-)discovers the ork's own
    // starting position from the '@' marker here, on the first real draw
    // after a level loads - see this file's own header comment.
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 8; j = j + 1 )
        {
            hasBlock = true;
            ch = bubLvlData[ ( i * 8 ) + j ];
            if( ch == 'o' ) block = bubBubbleBitmap;
            else if( ch == '#' ) block = bubWallBitmap;
            else if( ch == 'H' ) block = bubLadderBitmap;
            else if( ch == '-' ) block = bubKeyBitmap;
            else if( ch == 'X' ) block = bubDoorBitmap;
            else if( ch == '>' ) block = bubRightBitmap;
            else if( ch == '<' ) block = bubLeftBitmap;
            else if( ch == '=' ) block = bubCrateBitmap;
            else if( ch == '4' )
            {
                block = bubFlagBitmap;
                bubFlagX = j;
                bubFlagY = i;
            }
            else if( ch == '@' )
            {
                bubOrkX = j;
                bubOrkY = i;
                bubOrk = bubOrkstandBitmap;
                bubDir = 0; // NOFLIP
                bubLvlData[ ( i * 8 ) + j ] = ' ';
                hasBlock = false;
            }
            else
            {
                hasBlock = false;
            }
            if( hasBlock )
              gbDrawBitmap( j * BUB_TILE_W, i * BUB_TILE_H, block );
        }
    }

    gbDrawBitmapRotated( bubOrkX * BUB_TILE_W, bubOrkY * BUB_TILE_H, bubOrk, 0, bubDir ); // NOROT, dir

    // UI: vertical divider, flag icon + level number, inventory box
    gbDrawFastVLine( 64, 0, 48 );
    gbDrawBitmap( 65, 42, bubFlagBitmap );
    gbSetFont( gbFont5x7 );
    gbCursorX = 73;
    gbCursorY = 41;
    gbPrintNumber( bubLevel );

    gbDrawRect( 68, 16, BUB_TILE_W + 4, ( BUB_TILE_H * bubMaxInv ) + 4 );
    gbDrawFastVLine( 67, 16, BUB_TILE_H + 2 );
    gbDrawFastVLine( 67 + BUB_TILE_W + 5, 16, BUB_TILE_H + 2 );
    i = 18;
    for( j = 0; j < bubBubs; j = j + 1 )
    {
        gbDrawBitmap( 70, i, bubBubbleBitmap );
        i = i + BUB_TILE_H;
    }
    for( j = 0; j < bubKeys; j = j + 1 )
    {
        gbDrawBitmap( 70, i, bubKeyBitmap );
        i = i + BUB_TILE_H;
    }
}

void bubDrawMenu()
{
    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "Level Menu\n\nLevel\nEvil Twin" );

    gbCursorX = 46;
    gbCursorY = 12;
    if( !bubMenuEvilSelected )
      gbPrintString( bubIconLeftSp );
    else
      gbPrintString( "  " );
    if( bubMenuSelectedLevel < 10 )
      gbPrintString( " " );
    gbPrintNumber( bubMenuSelectedLevel );
    if( !bubMenuEvilSelected )
      gbPrintString( bubIconRight );
    else
      gbPrintString( " " );

    gbSetColor( 0 );
    gbFillRect( 70, 11, 10, 10 );
    gbSetColor( 1 );
    if( bubIsLevelDone( bubMenuSelectedLevel ) )
      gbDrawBitmap( 70, 11, bubOkBitmap );
    else
      gbDrawBitmap( 70, 11, bubKoBitmap );

    gbSetColor( 0 );
    gbFillRect( 46, 18, 20, 10 );
    gbSetColor( 1 );
    gbCursorX = 46;
    gbCursorY = 18;
    if( bubMenuEvilSelected )
      gbPrintString( bubIconLeft );
    else
      gbPrintString( " " );
    if( bubEvilTwin )
      gbPrintString( " ON" );
    else
      gbPrintString( "OFF" );
    if( bubMenuEvilSelected )
      gbPrintString( bubIconRight );
    else
      gbPrintString( " " );
}

// -----------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------

void bubBeginTitle( int returnState )
{
    bubTitleReturnState = returnState;
    bubState = BUB_STATE_TITLE;
}

void bubBeginMenu()
{
    bubMenuEvilSelected = false;
    bubMenuSelectedLevel = bubLevel;
    gbSetFont( gbFont3x5 );
    bubState = BUB_STATE_MENU;
}

// Real `gb.titleScreen(title)` - shows the real title bitmap and waits for
// a genuine fresh Button A press. Upstream's own real titleScreen() call
// draws no text of its own beyond the passed-in logo; the "PRESS A" prompt
// here is this project's own established convention for every titleScreen()
// port so far (see gamePong.c's/gameUfoRace.c's own title-screen functions).
void bubUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( 0, 12, bubTitleBitmap );
    gbSetFont( gbFont3x5 );
    gbCursorX = 28;
    gbCursorY = 3;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      bubState = bubTitleReturnState;
}

void bubUpdateMenu()
{
    if( gbPressed( BTN_C ) )
    {
        bubBeginTitle( BUB_STATE_MENU );
        return;
    }

    if( gbPressed( BTN_A ) )
    {
        bubLevel = bubMenuSelectedLevel;
        bubLoadLevel( bubLevel );
        bubState = BUB_STATE_PLAY;
        return;
    }

    if( gbPressed( BTN_B ) )
    {
        bubWriteLevelsDone();
        bubState = BUB_STATE_PLAY;
        return;
    }

    if( gbPressed( BTN_RIGHT ) )
    {
        if( bubMenuEvilSelected )
        {
            bubEvilTwin = !bubEvilTwin;
        }
        else
        {
            bubMenuSelectedLevel = bubMenuSelectedLevel + 1;
            if( !bubEvilTwin && ( bubMenuSelectedLevel % 2 ) )
              bubMenuSelectedLevel = bubMenuSelectedLevel + 1;
            if( bubMenuSelectedLevel >= 99 )
              bubMenuSelectedLevel = 0;
        }
    }

    if( gbPressed( BTN_LEFT ) )
    {
        if( bubMenuEvilSelected )
        {
            bubEvilTwin = !bubEvilTwin;
        }
        else
        {
            bubMenuSelectedLevel = bubMenuSelectedLevel - 1;
            if( !bubEvilTwin && ( bubMenuSelectedLevel % 2 ) )
              bubMenuSelectedLevel = bubMenuSelectedLevel - 1;
            if( bubMenuSelectedLevel < 0 )
              bubMenuSelectedLevel = 99;
        }
    }

    if( gbPressed( BTN_UP ) || gbPressed( BTN_DOWN ) )
      bubMenuEvilSelected = !bubMenuEvilSelected;

    bubDrawMenu();
}

void bubUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        if( bubGameOver )
        {
            bubNext();
        }
        else
        {
            bubBeginTitle( BUB_STATE_PLAY );
            return;
        }
    }

    if( gbPressed( BTN_B ) )
    {
        bubBeginMenu();
        return;
    }

    if( gbPressed( BTN_A ) )
    {
        if( bubGameOver )
          bubNext();
        else
        {
            bubLoadLevel( bubLevel );
            bubSfx( 3 );
        }
    }

    if( gbPressed( BTN_LEFT ) )
    {
        if( bubGameOver ) bubNext(); else bubMoveLeft();
    }

    if( gbPressed( BTN_RIGHT ) )
    {
        if( bubGameOver ) bubNext(); else bubMoveRight();
    }

    if( gbPressed( BTN_UP ) )
    {
        if( bubGameOver ) bubNext(); else bubMoveUp();
    }

    if( gbPressed( BTN_DOWN ) )
    {
        if( bubGameOver ) bubNext(); else bubMoveDown();
    }

    bubDrawPlayScreen();
}

// -----------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------

void gameBub_init()
{
    gbBegin();

    bubLoadLevelsDone();

    int i = 0;
    while( i < 100 && bubIsLevelDone( i ) )
    {
        if( bubEvilTwin )
          i = i + 1;
        else
          i = i + 2;
    }
    if( i == 100 )
      bubLevel = 0;
    else
      bubLevel = i;

    bubLoadLevel( bubLevel );

    bubBeginTitle( BUB_STATE_PLAY );
}

void gameBub_update()
{
    if( !gbUpdate() ) return;

    if( bubState == BUB_STATE_TITLE ) bubUpdateTitle();
    else if( bubState == BUB_STATE_MENU ) bubUpdateMenu();
    else bubUpdatePlay();

    gbRenderFrame();
}
