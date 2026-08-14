#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// =============================================================================
// Elventure - ported from the real Gamebuino Classic "Elventure" cartridge.
//
// Source: https://github.com/wuuff/Elventure (this project's own staged
// copy: "more games/Elventure/"). Real original game (2011-2013) by
// trodoss, published by TEAM a.r.g. (http://www.team-arg.org/) - the real
// "Original by TEAM ARG." credit line the real upstream `setup()` itself
// prints via `gb.titleScreen(F("Original by TEAM ARG.\nPort by Wuff."),
// title_bitmap_unified)`. This specific Gamebuino Classic port/build (the
// one actually staged here) is itself already a port-of-a-port: the real
// git history (`git log`) shows a single real commit by "wuuff"
// (wuuuufff@gmail.com), the same real author already credited elsewhere in
// this project for CrazyCar/Armageddon/Crabator/UFO Race. License: GPLv3
// (`more games/Elventure/LICENSE`) - note the real header comment inside
// `more games/Elventure/ELV_TV_v10.ino` itself instead says "version 2 of
// the License, or (at your option) any later version", a real, pre-existing
// discrepancy between the checked-in top-level LICENSE file (GPLv3) and the
// per-file header text (GPLv2-or-later) - not introduced by this port,
// flagged here the same way this project's own README already flags a
// similar real GPLv2/GPLv3 concern for `gameFiremen.c`.
//
// WHAT THIS GAME IS: a real, top-down Zelda-like action-adventure - a
// single elf character explores a 128-room (8 columns x 16 rows), scrolling
// overworld/underworld map one screen-sized room at a time, fighting real
// roaming monsters with a throwable sword, collecting hearts (health) and 4
// real quest items (crystal/orb/armor/staff) from fixed per-room locations,
// with two real portals linking the overworld (rooms 64-127) and
// underworld (rooms 0-63) halves of the map. Collecting any 3 of the 4 real
// quest items wins the game.
//
// REAL CLASS FLATTENING: real upstream is genuine class-based C++ (`Elf`/
// `RoomElement`/`Display` structs plus free functions taking them by value/
// reference across `elf.cpp`/`monster.cpp`/`item.cpp`/`room.cpp`/`map.cpp`/
// `display.cpp`/`logo.cpp`/`sound.cpp`) - flattened here into:
//   - `Elf`      -> plain scalar globals (`elvElfFacing`/`Step`/`X`/`Y`/
//     `Hearts`/`State` plus a separate `int elvElfItems[4]` global array) -
//     only one instance ever exists, and this dialect's own array-typed-
//     struct-member support is unproven (flagged directly by
//     `gamebuino-solitaire`'s own header comment earlier this project), so
//     the inventory array was kept as an independent global rather than a
//     struct field, sidestepping the open question entirely rather than
//     being the one to test it.
//   - `RoomElement` (real upstream's own already-class-free plain struct,
//     used for the sword/monsters/items in the current room, max 4 at
//     once) -> `struct ElvRoomElement { type,x,y,state,step,counter }`
//     (scalar fields only) plus `ElvRoomElement[4] elvRoomElements` - the
//     proven `Type[N] name;` array-of-named-struct pattern already shipped
//     by `gameSuperSpaceShooter.c`'s own `SssBullet[64] sssBullets`. Real
//     upstream's own `id` field (which just mirrored a `RoomElement`'s own
//     array slot number back to itself) was dropped outright - every
//     function here takes an explicit array index instead of a struct
//     copy, the same "index instead of by-value struct" treatment this
//     project has used since `gamePunkt.c`, which sidesteps real
//     upstream's own pervasive "function takes/returns a `RoomElement` by
//     value" shape entirely (a 6-field struct return is >1 word, illegal
//     here per VIRCON32_C_DIALECT.md #4 - `hitElf()`/`hitMonster()`/
//     `moveMonster()`/`moveItem()`/`hitItem()`/`getRoomElement()` all
//     returned one). `elvHandleRoomElements()`'s own real "monster killed
//     mid-loop adds a heart, which the same real per-room-load `while
//     (element_count < MAX_ELEMENT_RECORDS)` cap already guarantees never
//     overflows the real 4-slot array (verified by hand: every one of the
//     128 real rooms only ever seeds at most 1 custom element beyond the
//     always-present sword, since `room_element_data`'s own real 4th quad
//     field is *always* 255 - a real, always-true per-room terminator, not
//     a sometime one - so a room can reach at most 3 live elements
//     (sword + 1 spawned + 1 heart-on-kill), never the 4-slot ceiling)"
//     quirk is preserved by re-reading the real, global `elvElementCount`
//     bound on every loop iteration, exactly like real upstream's own
//     `for (i=0; i<element_count; i++)`.
//   - `Map`/`Room` (mostly free functions already, no real class state
//     beyond one `map_curr_room` global) -> `elvMapCurrRoom` global plus
//     `elvGetMapBlock()`/`elvCheckMapRoomMove()`/`elvScrollMap()`/
//     `elvSetMapRoom()`/`elvLoadRoomElements()` free functions, ported
//     near-verbatim.
//   - `Display` (this game's own `display.cpp`/`bitmap_funcs.cpp`, real
//     files distinct from the real Gamebuino Display class - see the
//     dedicated section below) -> folded into `elvDrawHud()`/
//     `elvDrawFrame()` directly on top of this shim's own already-existing
//     `gbDrawBitmap()`/`gbFillRect()` primitives, nothing new needed.
//   - `Logo`/`Sound` -> `elvDrawLogo()` (near-verbatim) and dropped
//     entirely (see the real dead-code finding below).
// CIRCULAR DEPENDENCY, RESOLVED FOR FREE: real upstream's own `room.cpp`
// needs `elf.h`/`item.h`/`monster.h`, `elf.cpp` needs `room.h`/`item.h`,
// `monster.cpp`/`item.cpp` both need `map.h`/`room.h` - a real mutual-
// include web only possible in real C++ via forward-declared header
// prototypes across many translation units. Flattening every one of those
// real classes into this single one-file, one-translation-unit port
// (`src/main.c` #includes exactly one `.c` per game, matching every other
// game in this cartridge) makes the "circular dependency" question moot
// entirely: every function below is just declared in a straightforward
// dependency order (small self-contained helpers first, `elvHandleRoomElements()`
// last among the logic functions, `elvDrawFrame()` last of all) with no
// forward prototypes needed anywhere, the same "it's all one file now"
// resolution already used by every multi-class game shipped in this
// cartridge before this one.
//
// REAL `display.cpp`/`bitmap_funcs.cpp` CONFIRMED TO CONTAIN NO CUSTOM
// PER-PIXEL BITMAP-MASKING CODE - checked directly and carefully per this
// batch's own explicit instruction to verify this rather than assume it,
// given the real, serious performance bug a sibling game in this same
// batch of Tier-3 RPG ports hit from exactly this class of mistake (see
// this project's own "Pirates ported, then reverted" section). Reading
// `bitmap_funcs.cpp`'s real `overlaybitmap()`/`erasebitmap()`/
// `eraseBitmapRect()` bodies directly shows every one of them is a *real,
// already-dead* hand-rolled per-pixel/per-byte TVout compositing routine,
// entirely wrapped in a `/* ... */` block comment (a leftover from this
// same file's own prior life as part of the unrelated "Parachute" hackvision
// sketch it was borrowed from, credited in this game's own real header
// comment) - the actual, live, compiled body of all three functions is
// nothing but a single direct call straight through to real
// `gb.display.drawBitmap()`/`fillRect()`/`setColor()`, i.e. this shim's own
// already-optimized `gbDrawBitmap()`/`gbFillRect()`/`gbSetColor()`
// primitives are a exact, direct, already-fast drop-in replacement with
// zero custom per-pixel code needed or written by this port at all. Real
// `display.cpp`'s own `updateDisplay()` is likewise nothing but a sequence
// of `gb.display.drawBitmap()`/`eraseBitmapRect()` calls (the real HUD
// heart/item icons) - ported directly the same way, see `elvDrawHud()`.
//
// A DELIBERATE, DOCUMENTED RENDERING-STRATEGY CHANGE (not a gameplay
// change): real upstream sets `gb.display.persistence = true` and then
// hand-manages exactly which small regions get erased/redrawn each tick
// (an old-AVR-hardware CPU optimization: only the ~2-4 sprites that moved
// this tick get touched, not the other ~56 already-correct map tiles) -
// this shim's own `gbUpdate()` unconditionally clears the whole framebuffer
// every real logic tick instead (matching real hardware's own *default*
// `persistence=false` behavior, the same default every other game in this
// cartridge already relies on). Unlike `gameTron.c` (this cartridge's one
// other real `persistence=true` game), Elventure's own real incremental-
// erase/redraw scheme is *purely* a CPU-cost optimization, never a source
// of truth a later read depends on - real upstream never once reads pixels
// back from the framebuffer for game logic (`checkMapRoomMove()` reads the
// real `map_room_data`/`map_pattern_data` tables directly, not
// `getPixel()`), unlike Tron's own real trail-collision mechanic, which
// generally *requires* true persistence since the drawn trail pixels
// themselves are the only record of where every rider has already been.
// So this port simply redraws the *entire* current room (all 60 real 8x8
// map tiles), the real HUD, the elf, and every live room element fresh
// every single tick (`elvDrawFrame()`, called once per tick from
// `elvUpdatePlaying()`) instead of reproducing real upstream's own
// incremental erase-old/draw-new bookkeeping - provably the same final
// on-screen pixels every tick either way (a full redraw already draws the
// correct map tile under a sprite's old position directly, the same
// pixels real upstream's own explicit `eraseBitmapRect()` call would have
// restored first), and trivially affordable at real upstream's own
// deliberately slow `gb.setFrameRate(10)` (10fps) - about 60-70 real
// `gbDrawBitmap()` calls/tick, nowhere near this dialect's real ~250,000-
// instruction/frame budget at 60fps, let alone 10fps. Every draw call in
// `elvDrawFrame()`/`elvDrawHud()`/`elvDrawLogo()` is a single already-
// optimized shim primitive call (`gbDrawBitmap()`/`gbFillRect()`), never a
// per-pixel loop, so this simplification is also strictly cheaper than a
// faithfully-incremental port would have been, not just simpler to write.
//
// REAL SOUND SYSTEM (`sound.cpp`/`sound_data.h`) IS DEAD CODE IN THE REAL
// SHIPPED BUILD, CONFIRMED BY READING IT DIRECTLY: real `play_song(char
// song)` has a literal `return;` as its very first executable statement,
// with an adjacent real upstream comment removing all doubt this is
// deliberate: "//NOTE EARLY RETURN!! Currently NOT playing any music." -
// meaning the entire real melody/tempo/duration/song_start pattern-player
// data and the `play_song()`/`play_song_once()`/`update_sound()`/
// `play_sfx()` functions built on top of it never actually produce sound
// in the real, already-shipped cartridge this port is translating (traced
// by hand: `play_song_once()` still unconditionally sets `music_state =
// SONG_PLAYING_ONCE` even though the `play_song()` call right before it
// already returned early, so `update_sound()` does fire exactly one real
// `gb.sound.playNote()` call with a genuinely computed `0`-tick duration
// the very first tick after a game-over/won screen appears, then falls
// permanently idle - a real, but functionally silent/inaudible artifact of
// dead code, not real music). This port drops the entire melody/tempo/
// duration data tables and the `play_song()`/`play_song_once()`/
// `update_sound()`/`play_sfx()` functions outright, matching the real
// shipped cartridge's own actual audible behavior exactly: zero music
// ever plays, matching this project's own already-documented, already-
// accepted "only one-shot representative tones ported, no pattern/track
// player" scope limit (see the main project CLAUDE.md's own "Open
// questions" section). The four real one-shot SFX calls this game
// actually does make audible use of - `gb.sound.playCancel()` (hit by a
// monster), `gb.sound.playTick()` (heart collected), `gb.sound.playOK()`
// (quest item collected) - are real, live, direct calls (NOT routed
// through the dead song system at all) and are ported directly via
// `gbPlayCancel()`/`gbPlayTick()`/`gbPlayOK()`. Real upstream's own
// `gb.sound.command(1,0,0,0)` (a real jukebox-channel-enable call, made
// once in `start_game()`) has no equivalent primitive in this shim at all
// - dropped outright, since it only ever configured the same dead song
// system anyway, so dropping it changes nothing observable.
//
// NO FACING/FLIP-DEPENDENT COLLISION ANYWHERE IN THIS GAME - checked
// directly per this batch's own explicit instruction, given a sibling
// game in this same batch hit a real, serious pre-existing upstream bug
// from exactly this class of mistake (see "Pirates ported, then reverted"
// again). Real `testRoomElement()`'s own hitbox test always uses the same
// fixed `(x, x+8)` / `(y, y+ySize)` axis-aligned rectangle regardless of
// which way the elf is currently facing, and the elf's own 12 sprite
// frames (`elf_bitmap.cpp`) are 12 genuinely *separate*, hand-drawn images
// (one full frame per facing x per animation step) rather than one base
// frame reused via `gbDrawBitmapRotated()`'s own rotate/flip parameters -
// this port never calls `gbDrawBitmapRotated()` at all, only plain
// `gbDrawBitmap()`, so there is no rotation/flip-vs-hitbox mismatch to get
// wrong in the first place.
//
// NO REAL SAVE/PERSISTENCE CONCEPT - confirmed by a direct
// `grep -rn "EEPROM\|eeprom"` across all 36 real staged source files,
// zero matches. Real upstream never includes `<EEPROM.h>` and has no
// highscore/progress-save mechanic of any kind (death/victory both just
// restart the same fixed 3-heart/no-inventory run) - no EEPROM wiring
// added here, matching real shipped behavior exactly (nothing to persist).
//
// REAL UPSTREAM QUIRKS PRESERVED DELIBERATELY (bugs/oddities, not fixed):
//   - `elvScrollMap()`'s own DOWN/RIGHT bounds checks are only ever
//     `< ELV_MAP_ROOM_COUNT` (128), never guarded against overshooting the
//     *edge* of the map's own real 8-wide/16-tall grid (e.g. scrolling
//     LEFT from a room at the start of a row silently wraps into the
//     *previous* row's own rightmost room, and DOWN from the bottom row
//     can reach room 128-135, one row past the real 1280-entry
//     `elvMapRoomData` table) - ported byte-for-byte from real upstream's
//     own identical, equally unguarded `scrollMap()` (confirmed against
//     `map.cpp` directly), on the same reasoning already established by
//     this project's own `gameUfoRace.c` for an analogous unguarded
//     `getTile()`: real map *design* (the outer boundary of the whole
//     128-room grid being walled) almost certainly keeps a real player
//     from ever reaching the edge case in practice, and even if it were
//     reached, an out-of-bounds *read* is not a hard-trap on this
//     platform (VIRCON32_C_DIALECT.md section 17.3's own list of what
//     does hard-trap - div/mod-by-zero, `sqrt` of a negative, `atan2(0,0)`
//     - does not include a plain array read), so the worst realistic
//     outcome is a garbage-tiled room, never a crash.
//   - `elvHitElf()`'s own monster-contact "bounce" (flip the monster's
//     movement direction to the opposite one) fires on *every* tick the
//     elf and a live monster's hitboxes overlap, not just the tick a
//     heart is actually lost (the heart-loss/invulnerability-counter gate
//     is a separate, inner check) - ported exactly as real upstream's own
//     `hitElf()` structures it.
//   - Picking up any 3 items (not necessarily 3 *distinct* quest item
//     types) wins the game, per real upstream's own literal
//     `addElfItem()` slot-counting logic - preserved exactly (in practice
//     each of the 4 real quest items only ever spawns once across the
//     whole 128-room map per real `room_element_data`, so this distinction
//     is real but never actually observable in a normal playthrough).
//
// REAL BITMAP/DATA TABLES EXTRACTED VERBATIM, NOT HAND-TRANSCRIBED: every
// one of `elf_bitmap.cpp`/`item_bitmap.cpp`/`monster_bitmap.cpp`/
// `map_bitmap.cpp`/`logo_bitmap.cpp`/`display_bitmap.cpp` (plus
// `title_bitmap_unified`, the one real bitmap `other_bitmap.h` actually
// contributes - its own `team_arg_bitmap`/`logo2_bitmap` are real, but
// dead: only ever reached through commented-out `TV.bitmap()` calls, never
// a live `gb.display.drawBitmap()` one, so neither was ported) and
// `map_data.h`/`room_data.h` were parsed and converted by a small Python
// script directly from the real source text (comments stripped first, so
// a real in-line comment like `//Moved from x=40, y=24 ...` couldn't be
// mistaken for array data by a naive comma-split), every `0b...`-style
// Arduino binary literal converted to `0x` hex (this dialect has no binary
// literal syntax, the same conversion this project's own `gameArmageddon.c`/
// `gameSpinSpinSpinbuino.c` already needed), and every resulting record's
// real byte count cross-checked against its own declared width/height
// header (`2 + ceil(width/8)*height`) before being trusted - every single
// one matched exactly (12 elf frames, 14 item frames, 9 monster frames, 7
// map-tile frames, 12 logo frames, 6 HUD-icon frames, 210
// `map_pattern_data` entries, 1280 `map_room_data` entries, 128
// `room_element_index_data` entries, 196 `room_element_data` entries - all
// matching their own real declared `#define` counts in `map_data.h`/
// `room_data.h` exactly). Each bitmap frame is its own named `int[N]`
// array (`elvElfBmp0`..`elvElfBmp11` etc), collected into an
// `int* elvXBitmaps[N] = { ... };` lookup array indexed by the real
// upstream frame-offset formula (e.g. `elvElfBitmaps[facing]`/
// `[facing+step]`) - the same proven "array of bitmap pointers, indexed"
// pattern already shipped by `gameGlaciGlaca.c`'s own `glaciGoutSprite`/
// `gameArtillery.c`'s own `artUnitsBitmaps`, chosen deliberately over raw
// pointer-arithmetic into one flat blob (real upstream's own
// `elf_bitmap + (facing * SIZEOF_ELF_RECORD)` idiom) to avoid needing to
// re-derive the exact per-record word offset by hand for every one of the
// 6 bitmap tables.
//
// TITLE/PAUSE SCREEN TEXT, ADAPTED: real upstream's own single blocking
// `gb.titleScreen(text, bitmap)` call (used both at boot and, with
// different text, as a mid-game Button-C pause) draws its passed-in logo
// at real hardware's own fixed `(0, 12)` anchor (confirmed against real
// `Gamebuino.cpp` during this project's own earlier `gameArmageddon.c`/
// `gameUfoRace.c` ports of the exact same real library function) - reused
// here unchanged for both `elvUpdateBootTitle()`/`elvUpdatePauseTitle()`.
// The real credit text ("Original by TEAM ARG." / "Port by Wuff.") is
// slightly shortened to fit this shim's own smallest real font
// (`gbFont3x5`) within the real 84px screen width, matching this
// project's own already-established precedent (`gameCrazyCar.c`/
// `gameConduit.c` both already shortened real upstream text for the exact
// same reason) - the real names/roles are kept intact, only the
// surrounding words are dropped. The real pause hint text's own icon
// escape (`"Press \25 to resume."` - a real octal `\25` = decimal 21,
// this project's own already-established "A button" icon glyph, the same
// one `gameTaquin.c`/`gameSimonbuino.c`/`gameSpinSpinSpinbuino.c` already
// restored for an identical real upstream icon reference) is reproduced
// as a real `gbDrawChar(21, ...)` call spliced between two `gbPrintString()`
// calls (rather than a hand-built `int[]` array, since `gbPrintString()`
// already exposes `gbCursorX`/`gbFontWidth` for exactly this splice).
//
// A DELIBERATE, MINOR RENDERING-TIMING SIMPLIFICATION: real upstream's own
// mid-game pause is a genuinely *blocking* call - the instant Button C is
// pressed, `gb.titleScreen()` itself loops internally (calling `gb.update()`
// over and over) until Button A is pressed, then the *same* real Arduino
// `loop()` iteration immediately redraws the full map/HUD/elf and resumes
// `handleRoomElements()` before that iteration ends. This port instead
// models PAUSE as an explicit state (`ELV_GS_PAUSE`, matching this
// project's own established "blocking widget -> explicit resumable state"
// treatment, e.g. `gamePong.c`'s own `PONG_STATE_TITLE`) - dismissing it
// (Button A) simply returns to `ELV_GS_PLAYING`, whose very next real tick
// (100ms later at this game's own 10fps) redraws the room and resumes
// monster/item movement, rather than resuming within the exact same real
// tick boundary. A harmless, single-tick (100ms) difference - gameplay
// state itself (elf position, room contents, monster positions) is
// unaffected either way, since `elvHandleRoomElements()` never runs at all
// while `ELV_GS_PAUSE` is active (monsters/items are genuinely frozen
// during the pause, matching real upstream's own real behavior exactly,
// for the same reason: `handleRoomElements()` is only ever reachable
// *after* the real blocking call returns).
// =============================================================================


// ---- Game states (this port's own state machine - see the header comment
// above for how this maps to real upstream's own blocking-widget calls) ----
#define ELV_GS_BOOT_TITLE 0
#define ELV_GS_PLAYING    1
#define ELV_GS_PAUSE      2
#define ELV_GS_GAME_OVER  3
#define ELV_GS_GAME_WON   4

// ---- Elf constants (elf.h) ----
#define ELV_FACING_DOWN  0
#define ELV_FACING_UP    3
#define ELV_FACING_LEFT  6
#define ELV_FACING_RIGHT 9

#define ELV_STEP_LENGTH  4
#define ELV_MAX_ITEMS    3
#define ELV_MAX_HEARTS   4

#define ELV_ELFSTATE_PLAYING 0
#define ELV_ELFSTATE_DEAD    1
#define ELV_ELFSTATE_WON     2

// ---- Room element constants (room.h) ----
#define ELV_MAX_ELEMENT_RECORDS 4

#define ELV_ITEM_SWORD   50
#define ELV_ITEM_HEART   52
#define ELV_ITEM_CRYSTAL 54
#define ELV_ITEM_PORTAL  56
#define ELV_ITEM_ORB     58
#define ELV_ITEM_ARMOR   60
#define ELV_ITEM_STAFF   62

#define ELV_STATE_HIDDEN     0
#define ELV_STATE_VISIBLE    1
#define ELV_STATE_MOVE_UP    2
#define ELV_STATE_MOVE_DOWN  3
#define ELV_STATE_MOVE_LEFT  4
#define ELV_STATE_MOVE_RIGHT 5

#define ELV_COUNTER_START 20

// ---- Map constants (map.h / map_data.h) ----
#define ELV_SCROLL_UP    0
#define ELV_SCROLL_DOWN  1
#define ELV_SCROLL_LEFT  2
#define ELV_SCROLL_RIGHT 3

#define ELV_MAP_WIDTH      8
#define ELV_MAP_ROOM_COUNT 128


// ===== elf_bitmap (12 frames, 8x8) =====
int elvElfBmp0[10] =
{
8,8,0x0,0x8,0x1c,0x41,0x6b,0x2a,0x1c,0x0,
};

int elvElfBmp1[10] =
{
8,8,0x57,0x87,0x2a,0x30,0x6,0x30,0x0,0x0,
};

int elvElfBmp2[10] =
{
8,8,0x57,0xc7,0x92,0xc,0x60,0xc,0x0,0x0,
};

int elvElfBmp3[10] =
{
8,8,0x0,0x0,0x10,0xba,0xba,0x54,0x38,0x0,
};

int elvElfBmp4[10] =
{
8,8,0xda,0xc3,0xb9,0x30,0x6,0x30,0x0,0x0,
};

int elvElfBmp5[10] =
{
8,8,0xda,0xc1,0x9c,0xc,0x60,0xc,0x0,0x0,
};

int elvElfBmp6[10] =
{
8,8,0x0,0x2c,0xf2,0xe,0x5a,0xd8,0x70,0x0,
};

int elvElfBmp7[10] =
{
8,8,0xb8,0xcc,0xb4,0x40,0xcc,0x0,0x0,0x0,
};

int elvElfBmp8[10] =
{
8,8,0xb8,0xc8,0xb0,0x0,0x30,0x0,0x0,0x0,
};

int elvElfBmp9[10] =
{
8,8,0x0,0x34,0x4f,0x70,0x5a,0x1b,0xe,0x0,
};

int elvElfBmp10[10] =
{
8,8,0x1d,0x33,0x2d,0x2,0x33,0x0,0x0,0x0,
};

int elvElfBmp11[10] =
{
8,8,0x1d,0x13,0xd,0x0,0xc,0x0,0x0,0x0,
};

int* elvElfBitmaps[12] =
{
elvElfBmp0,
elvElfBmp1,
elvElfBmp2,
elvElfBmp3,
elvElfBmp4,
elvElfBmp5,
elvElfBmp6,
elvElfBmp7,
elvElfBmp8,
elvElfBmp9,
elvElfBmp10,
elvElfBmp11
};

// ===== item_bitmap (14 frames, 8x8) =====
int elvItemBmp0[10] =
{
8,8,0x3,0x5,0xa,0x54,0x68,0x30,0x58,0x0,
};

int elvItemBmp1[10] =
{
8,8,0x0,0x58,0x30,0x68,0x54,0xa,0x5,0x3,
};

int elvItemBmp2[10] =
{
8,8,0x6c,0xfe,0xfe,0xfe,0xfe,0x7c,0x38,0x10,
};

int elvItemBmp3[10] =
{
8,8,0x6c,0x92,0x82,0x82,0x82,0x44,0x28,0x10,
};

int elvItemBmp4[10] =
{
8,8,0x1f,0x3d,0x7d,0xf8,0xf2,0xe4,0x88,0xe0,
};

int elvItemBmp5[10] =
{
8,8,0x1f,0x25,0x45,0x88,0x92,0xe4,0x88,0xe0,
};

int elvItemBmp6[10] =
{
8,8,0x30,0x6c,0xde,0xde,0xf6,0xf6,0x6c,0x18,
};

int elvItemBmp7[10] =
{
8,8,0x3c,0x7e,0x73,0xbd,0xce,0x7e,0x3c,0x0,
};

int elvItemBmp8[10] =
{
8,8,0x0,0x1c,0x3e,0x4f,0x5f,0x7f,0x3e,0x1c,
};

int elvItemBmp9[10] =
{
8,8,0x0,0x1c,0x22,0x59,0x51,0x41,0x22,0x1c,
};

int elvItemBmp10[10] =
{
8,8,0x66,0xc3,0x24,0x7e,0x3c,0x18,0x42,0x99,
};

int elvItemBmp11[10] =
{
8,8,0x66,0xc3,0x24,0x5a,0x24,0x18,0x42,0x99,
};

int elvItemBmp12[10] =
{
8,8,0x6,0x9,0x9,0x16,0x28,0x70,0xa0,0xc0,
};

int elvItemBmp13[10] =
{
8,8,0x6,0xf,0xf,0x16,0x28,0x70,0xa0,0xc0,
};

int* elvItemBitmaps[14] =
{
elvItemBmp0,
elvItemBmp1,
elvItemBmp2,
elvItemBmp3,
elvItemBmp4,
elvItemBmp5,
elvItemBmp6,
elvItemBmp7,
elvItemBmp8,
elvItemBmp9,
elvItemBmp10,
elvItemBmp11,
elvItemBmp12,
elvItemBmp13
};

// ===== monster_bitmap (9 frames, 8x8) =====
int elvMonsterBmp0[10] =
{
8,8,0x34,0x81,0x66,0xdb,0x5a,0x24,0x99,0x5a,
};

int elvMonsterBmp1[10] =
{
8,8,0xbd,0x80,0x0,0x0,0x0,0x0,0x0,0x0,
};

int elvMonsterBmp2[10] =
{
8,8,0x99,0x3d,0x0,0x0,0x0,0x0,0x0,0x0,
};

int elvMonsterBmp3[10] =
{
8,8,0x0,0x0,0xee,0x38,0x44,0xee,0xc6,0x38,
};

int elvMonsterBmp4[10] =
{
8,8,0x82,0xb3,0x19,0x46,0xe0,0x0,0x0,0x0,
};

int elvMonsterBmp5[10] =
{
8,8,0x82,0xda,0xb0,0x44,0xe,0x0,0x0,0x0,
};

int elvMonsterBmp6[10] =
{
8,8,0x0,0x74,0xba,0x92,0x6c,0x4,0x38,0x0,
};

int elvMonsterBmp7[10] =
{
8,8,0xba,0x93,0x29,0x46,0xe0,0x0,0x0,0x0,
};

int elvMonsterBmp8[10] =
{
8,8,0xba,0xd2,0xa8,0x44,0xe,0x0,0x0,0x0,
};

int* elvMonsterBitmaps[9] =
{
elvMonsterBmp0,
elvMonsterBmp1,
elvMonsterBmp2,
elvMonsterBmp3,
elvMonsterBmp4,
elvMonsterBmp5,
elvMonsterBmp6,
elvMonsterBmp7,
elvMonsterBmp8
};

// ===== map_bitmap (7 frames, 8x8) =====
int elvMapBmp0[10] =
{
8,8,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
};

int elvMapBmp1[10] =
{
8,8,0x0,0x34,0x7a,0xe8,0xb4,0x42,0x18,0x0,
};

int elvMapBmp2[10] =
{
8,8,0xd9,0xdd,0xdc,0xda,0x9a,0x86,0x76,0xf8,
};

int elvMapBmp3[10] =
{
8,8,0x60,0xb0,0x6,0x0,0x10,0x3c,0x0,0x0,
};

int elvMapBmp4[10] =
{
8,8,0x0,0x6a,0x6a,0xa,0x60,0x6e,0x6e,0x0,
};

int elvMapBmp5[10] =
{
8,8,0x0,0x7f,0x41,0x1e,0x14,0x4,0x3e,0x0,
};

int elvMapBmp6[10] =
{
8,8,0x8,0x46,0x2c,0x81,0x7e,0x5e,0x3c,0x42,
};

int* elvMapBitmaps[7] =
{
elvMapBmp0,
elvMapBmp1,
elvMapBmp2,
elvMapBmp3,
elvMapBmp4,
elvMapBmp5,
elvMapBmp6
};

// ===== logo_bitmap (12 frames, 8x8) =====
int elvLogoBmp0[10] =
{
8,8,0x0,0xe0,0xa0,0x80,0xc9,0x89,0xa9,0xec,
};

int elvLogoBmp1[10] =
{
8,8,0x0,0x0,0x0,0x0,0x5d,0x59,0x51,0x9d,
};

int elvLogoBmp2[10] =
{
8,8,0x10,0x10,0x6c,0x54,0x91,0x55,0x55,0x53,
};

int elvLogoBmp3[10] =
{
8,8,0x0,0x0,0x0,0x0,0x67,0x56,0x64,0x57,
};

int elvLogoBmp4[10] =
{
8,8,0x0,0x40,0xa0,0x84,0xea,0xae,0xaa,0x4a,
};

int elvLogoBmp5[10] =
{
8,8,0x0,0x0,0x0,0x0,0xae,0xec,0xe8,0xae,
};

int elvLogoBmp6[10] =
{
8,8,0x0,0x20,0x50,0x50,0x55,0x55,0x55,0x23,
};

int elvLogoBmp7[10] =
{
8,8,0x0,0x0,0x0,0x0,0x76,0x65,0x46,0x75,
};

int elvLogoBmp8[10] =
{
8,8,0x0,0xa0,0xa0,0xa8,0xa1,0xaa,0x4a,0x49,
};

int elvLogoBmp9[10] =
{
8,8,0x10,0x10,0x6c,0x54,0x93,0x15,0x15,0x93,
};

int elvLogoBmp10[10] =
{
8,8,0x0,0x0,0x0,0x0,0x32,0xaa,0xb1,0x29,
};

int elvLogoBmp11[10] =
{
8,8,0x0,0x0,0x0,0x14,0x94,0x94,0x0,0x14,
};

int* elvLogoBitmaps[12] =
{
elvLogoBmp0,
elvLogoBmp1,
elvLogoBmp2,
elvLogoBmp3,
elvLogoBmp4,
elvLogoBmp5,
elvLogoBmp6,
elvLogoBmp7,
elvLogoBmp8,
elvLogoBmp9,
elvLogoBmp10,
elvLogoBmp11
};

// ===== display_bitmap (6 frames, 4x4) =====
int elvDispBmp0[6] =
{
4,4,0x0,0x0,0x0,0x0,
};

int elvDispBmp1[6] =
{
4,4,0xd0,0xf0,0x60,0x40,
};

int elvDispBmp2[6] =
{
4,4,0x30,0x50,0xa0,0xc0,
};

int elvDispBmp3[6] =
{
4,4,0xf0,0xf0,0xf0,0x60,
};

int elvDispBmp4[6] =
{
4,4,0x60,0x90,0x90,0x60,
};

int elvDispBmp5[6] =
{
4,4,0x30,0x30,0x40,0x80,
};

int* elvDisplayBitmaps[6] =
{
elvDispBmp0,
elvDispBmp1,
elvDispBmp2,
elvDispBmp3,
elvDispBmp4,
elvDispBmp5
};

// ===== title_bitmap_unified (32x16) =====
int elvTitleBitmap[66] =
{
32,16,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x10,0x0,0xe0,0x0,0x10,0x0,0xa0,0x0,0x6c,0x0,0x80,0x0,0x54,0x0,0xc9,0x5d,0x91,0x67,0x89,0x59,0x55,0x56,0xa9,0x51,0x55,0x64,0xec,0x9d,0x53,0x57,
};


// map_pattern_data: 210 entries (expect 35*6=210)
// map_room_data: 1280 entries (expect 128*10=1280)
int elvMapPatternData[210] =
{
1,1,1,1,1,1,1,1,0,0,1,1,1,0,0,0,0,1,1,0,
0,0,0,0,1,1,0,0,1,1,1,0,1,0,0,1,2,2,2,2,
2,2,2,2,0,0,2,2,2,0,0,0,0,2,2,0,0,0,0,0,
2,2,0,0,2,2,0,0,0,0,0,1,0,0,0,0,0,0,2,0,
2,0,0,2,0,0,0,0,0,2,1,3,3,0,0,0,2,2,0,0,
0,0,2,0,0,0,3,3,2,0,0,0,3,3,0,0,0,0,2,2,
3,3,0,0,0,2,3,3,0,0,0,2,2,3,3,3,3,3,2,2,
0,0,0,0,3,3,3,3,3,3,2,0,0,0,0,3,0,0,0,0,
0,3,4,4,4,4,4,4,4,0,0,0,0,4,4,4,0,0,4,4,
4,0,5,0,0,4,4,0,0,0,0,0,4,0,5,0,0,0,0,0,
0,0,0,4,0,0,5,0,0,4,
};

int elvMapRoomData[1280] =
{
0,1,2,2,3,3,2,2,1,4,4,1,2,2,2,2,2,2,1,4,
4,1,2,2,3,3,2,2,1,0,6,7,8,8,9,9,8,8,7,6,
6,7,8,8,9,9,8,8,7,10,10,7,8,8,8,8,8,8,8,8,
8,8,8,8,9,9,8,8,7,6,6,7,8,8,9,9,8,8,7,6,
0,1,2,2,11,11,2,2,2,2,2,2,2,2,2,2,2,2,1,4,
4,1,2,2,12,12,2,2,1,0,6,7,8,8,12,12,8,8,7,10,
10,7,8,8,12,12,8,8,7,6,6,7,8,8,9,9,8,8,7,10,
10,7,8,8,14,14,8,8,7,6,6,7,8,8,12,12,8,8,7,6,
0,1,2,2,15,15,2,2,1,4,4,1,2,2,2,2,2,2,1,4,
4,1,2,2,11,11,2,2,1,0,6,7,8,8,12,12,8,8,7,6,
6,7,8,8,12,12,8,8,7,10,10,7,8,8,12,12,8,8,7,6,
6,7,8,8,9,9,8,8,8,8,8,8,8,8,14,14,8,8,7,6,
6,7,8,8,14,14,8,8,8,8,8,8,8,8,8,8,8,8,7,10,
10,16,9,9,9,9,17,18,18,18,18,18,18,18,12,12,9,9,16,6,
6,7,8,8,14,14,8,8,7,6,6,7,8,8,14,14,8,8,7,10,
10,7,8,8,14,14,8,8,7,10,10,7,8,8,8,8,8,8,7,6,
6,7,8,8,9,9,8,8,7,10,10,7,8,8,8,8,8,8,7,10,
10,19,14,14,14,14,20,21,21,21,21,21,21,21,14,14,14,14,19,6,
6,7,8,8,9,9,8,8,7,10,10,7,8,8,8,8,8,8,7,10,
10,7,8,8,9,9,8,8,7,10,10,16,9,9,9,9,9,9,9,22,
0,1,2,2,12,12,2,2,1,0,0,1,2,2,3,3,2,2,1,4,
4,1,2,2,2,2,2,2,1,4,4,1,2,2,15,15,2,2,1,0,
6,23,16,9,12,12,9,17,18,18,18,18,18,18,18,18,18,18,18,18,
18,18,18,18,12,12,9,9,16,6,6,12,12,12,12,12,12,12,12,24,
0,1,2,2,12,12,2,2,1,4,4,1,2,2,11,11,2,2,1,4,
4,1,2,2,2,2,2,2,1,0,0,1,2,2,11,11,2,2,1,4,
10,19,14,14,14,14,14,20,21,21,21,21,21,21,21,21,21,21,21,21,
21,21,21,21,14,14,14,14,19,6,6,12,12,12,12,12,12,12,12,24,
0,1,2,2,11,11,2,2,1,4,4,1,2,2,2,2,2,2,2,2,
2,2,2,2,2,2,2,2,1,0,0,1,2,2,2,2,2,2,1,4,
10,7,8,8,8,8,8,8,7,10,10,7,8,25,25,25,25,25,25,25,
25,25,25,25,25,25,25,25,25,25,25,26,26,26,26,26,26,26,26,24,
27,28,28,28,28,28,28,28,28,29,29,28,28,30,31,31,30,28,28,27,
27,28,28,28,31,31,28,28,28,28,28,28,28,28,31,31,28,28,28,29,
10,7,8,8,9,9,8,8,7,6,27,28,28,28,31,31,28,28,28,29,
29,28,28,28,28,28,28,28,28,29,29,28,28,28,31,31,28,28,28,27,
27,31,31,32,31,31,32,31,31,31,31,31,31,32,12,12,32,31,31,29,
29,28,28,30,12,12,30,28,28,27,27,28,28,30,12,12,30,28,28,27,
6,7,8,8,12,12,8,8,7,10,10,7,8,8,14,14,8,8,7,6,
27,28,28,28,31,31,28,28,28,28,28,28,28,30,33,33,30,28,28,27,
27,33,33,34,12,12,34,33,33,33,33,33,33,34,33,33,34,33,33,27,
27,28,28,28,12,12,28,28,28,27,27,28,28,28,12,12,28,28,28,27,
6,7,8,8,12,12,8,8,7,6,27,28,28,28,31,31,28,28,28,27,
27,31,31,32,12,12,32,31,31,31,31,31,31,32,31,31,32,31,31,27,
27,28,28,28,12,12,28,28,28,27,27,28,28,28,28,28,28,28,28,29,
29,28,28,28,33,33,28,28,28,29,29,28,28,28,33,33,28,28,28,27,
6,7,8,8,12,12,8,8,7,6,27,28,28,28,12,12,28,28,28,27,
27,33,33,34,33,33,34,33,33,33,33,33,33,34,12,12,34,33,33,27,
27,28,28,28,33,33,28,28,28,29,29,28,28,30,28,28,30,28,28,29,
29,28,28,28,28,28,28,28,28,27,27,28,28,28,28,28,28,28,28,29,
10,7,8,8,12,12,8,8,7,6,27,28,28,30,12,12,30,28,28,27,
27,28,28,30,31,31,30,28,28,27,27,28,28,28,12,12,28,28,28,27,
6,7,8,8,9,9,8,8,7,10,10,7,8,8,8,8,8,8,7,10,
10,7,8,8,8,8,8,8,7,10,10,7,8,8,8,8,8,8,7,10,
10,7,8,8,12,12,8,8,7,6,27,28,28,28,33,33,28,28,28,29,
29,28,28,28,12,12,28,28,28,27,27,28,28,30,12,12,30,28,28,27,
6,7,8,8,14,14,8,8,7,6,27,28,28,30,31,31,30,28,28,29,
29,28,28,28,31,31,28,28,28,28,28,28,28,28,31,31,28,28,28,27,
6,7,8,8,14,14,8,8,7,6,27,28,28,30,31,31,30,28,28,27,
27,28,28,28,12,12,28,28,28,29,29,28,28,28,33,33,28,28,28,27,
27,28,28,28,28,28,28,28,28,29,29,28,28,28,33,33,28,28,28,29,
29,28,28,28,33,33,28,28,28,27,27,28,28,28,33,33,28,28,28,29,
29,28,28,30,28,28,30,28,28,29,29,28,28,28,33,33,28,28,28,29,
29,28,28,28,33,33,28,28,28,29,29,28,28,28,28,28,28,28,28,27,
};

// room_element_index_data: 128 entries (expect 128)
// room_element_data: 196 entries (expect 49*4=196)
int elvRoomElementIndex[128] =
{
255,255,0,1,255,255,2,3,4,255,255,255,255,255,255,255,255,5,255,6,
7,255,8,255,255,9,255,10,255,11,255,255,12,255,13,255,255,255,255,255,
255,255,255,255,255,255,255,255,14,255,15,16,255,255,255,255,255,255,17,18,
19,255,20,255,255,21,255,22,23,255,24,255,25,255,255,255,255,255,26,255,
255,27,28,29,30,31,32,255,255,33,255,34,255,35,255,255,36,255,37,255,
255,255,255,38,255,39,255,255,40,41,255,255,42,255,255,43,255,255,255,44,
45,46,47,255,255,255,48,255,
};

int elvRoomElementData[196] =
{
0,64,16,255,3,64,16,255,3,8,16,255,60,36,16,255,0,64,24,255,
0,24,16,255,0,24,16,255,3,32,16,255,3,64,16,255,0,16,24,255,
3,64,24,255,3,48,16,255,3,24,16,255,3,16,8,255,0,24,24,255,
0,64,16,255,0,24,24,255,56,56,24,255,54,32,24,255,0,24,24,255,
3,24,24,255,6,64,16,255,6,64,16,255,3,64,16,255,6,64,16,255,
6,16,16,255,6,64,16,255,6,64,16,255,6,16,16,255,6,16,24,255,
3,32,16,255,58,36,16,255,6,64,16,255,6,16,16,255,6,64,24,255,
6,64,24,255,6,64,16,255,6,64,16,255,6,16,24,255,3,40,16,255,
3,64,16,255,6,16,16,255,62,24,24,255,6,24,24,255,6,64,16,255,
56,32,24,255,6,16,16,255,6,64,24,255,6,24,24,255,
};



// =============================================================================
//   Room element struct (room.h's own RoomElement, `id` field dropped -
//   every function below takes an explicit array index instead)
// =============================================================================
typedef struct{
    int type;
    int x;
    int y;
    int state;
    int step;
    int counter;
} ElvRoomElement;

ElvRoomElement elvRoomElements[ELV_MAX_ELEMENT_RECORDS];
int elvElementCount = 0;

// ---- Elf globals (elf.h's own Elf struct, flattened - see header comment) ----
int elvElfFacing;
int elvElfStep;
int elvElfX;
int elvElfY;
int elvElfHearts;
int elvElfState;
int elvElfItems[4];

// ---- Map globals (map.h) ----
int elvMapCurrRoom = 0;

// ---- Top-level game state ----
int elvState;

// =============================================================================
//   Map (map.cpp)
// =============================================================================

int elvGetMapBlock( int mapX, int mapY, int room )
{
    int indexPtr;

    if( room == -1 )
      room = elvMapCurrRoom;

    indexPtr = elvMapRoomData[ ( room * 10 ) + mapX ];

    return elvMapPatternData[ ( indexPtr * 6 ) + mapY ];
}

int elvCheckMapRoomMove( int x, int y )
{
    int mapX = x / 8;
    int mapY = y / 8;

    if( ( mapX > 9 ) || ( mapY > 5 ) )
      return 0;

    return elvGetMapBlock( mapX, mapY, -1 );
}

// ---- Room elements (room.cpp) ----

void elvAddRoomElement( int type, int x, int y, int state, int counter )
{
    elvRoomElements[ elvElementCount ].type = type;
    elvRoomElements[ elvElementCount ].x = x;
    elvRoomElements[ elvElementCount ].y = y;
    elvRoomElements[ elvElementCount ].step = 1;
    elvRoomElements[ elvElementCount ].state = state;
    elvRoomElements[ elvElementCount ].counter = counter;
    elvElementCount = elvElementCount + 1;
}

// forward-referenced by elvLoadRoomElements() below
int elvElfHasItem( int type );

void elvLoadRoomElements( int room )
{
    int i;
    int indexPtr;

    for( i = 0; i < ELV_MAX_ELEMENT_RECORDS; i = i + 1 )
    {
        elvRoomElements[ i ].type = 0;
        elvRoomElements[ i ].x = 0;
        elvRoomElements[ i ].y = 0;
        elvRoomElements[ i ].state = 0;
        elvRoomElements[ i ].step = 0;
        elvRoomElements[ i ].counter = 0;
    }
    elvElementCount = 0;

    // real upstream always seeds the sword as room element 0
    elvAddRoomElement( ELV_ITEM_SWORD, 0, 0, ELV_STATE_HIDDEN, 0 );

    indexPtr = elvRoomElementIndex[ room ];

    if( indexPtr < 255 )
    {
        indexPtr = indexPtr * 4;

        while( elvElementCount < ELV_MAX_ELEMENT_RECORDS )
        {
            if( elvElfHasItem( elvRoomElementData[ indexPtr ] ) == 0 )
              elvAddRoomElement( elvRoomElementData[ indexPtr ], elvRoomElementData[ indexPtr + 1 ], elvRoomElementData[ indexPtr + 2 ], ELV_STATE_VISIBLE, 0 );

            // real upstream's own room_element_data 4th quad field is
            // always 255 in every real room - this loop always runs
            // exactly once per room in practice, ported exactly as
            // upstream structures it regardless (see header comment)
            if( elvRoomElementData[ indexPtr + 3 ] == 255 )
              break;

            indexPtr = indexPtr + 4;
        }
    }
}

void elvScrollMap( int direction )
{
    if( direction == ELV_SCROLL_UP )
    {
        if( elvMapCurrRoom > 0 )
          elvMapCurrRoom = elvMapCurrRoom - ELV_MAP_WIDTH;
    }
    else if( direction == ELV_SCROLL_DOWN )
    {
        if( elvMapCurrRoom < ELV_MAP_ROOM_COUNT )
          elvMapCurrRoom = elvMapCurrRoom + ELV_MAP_WIDTH;
    }
    else if( direction == ELV_SCROLL_LEFT )
    {
        if( elvMapCurrRoom > 0 )
          elvMapCurrRoom = elvMapCurrRoom - 1;
    }
    else if( direction == ELV_SCROLL_RIGHT )
    {
        if( elvMapCurrRoom < ELV_MAP_ROOM_COUNT )
          elvMapCurrRoom = elvMapCurrRoom + 1;
    }

    elvLoadRoomElements( elvMapCurrRoom );
}

void elvSetMapRoom( int room )
{
    elvMapCurrRoom = room;
    elvLoadRoomElements( elvMapCurrRoom );
}

// =============================================================================
//   Elf (elf.cpp)
// =============================================================================

void elvResetElf( int resetItems )
{
    int i;

    elvElfFacing = ELV_FACING_DOWN;
    elvElfStep = 1;
    elvElfX = 36;
    elvElfY = 24;
    elvElfHearts = 3;
    elvElfState = ELV_ELFSTATE_PLAYING;

    if( resetItems )
    {
        for( i = 0; i < 4; i = i + 1 )
          elvElfItems[ i ] = 0;
    }
}

void elvMoveElf( int facing )
{
    if( facing != elvElfFacing )
    {
        elvElfStep = 1;
    }
    else
    {
        elvElfStep = elvElfStep + 1;
        if( elvElfStep > 2 )
          elvElfStep = 1;
    }

    elvElfFacing = facing;

    if( facing == ELV_FACING_DOWN )
    {
        if( elvElfY < 40 )
        {
            if( elvCheckMapRoomMove( elvElfX, elvElfY + 16 ) == 0 )
              if( elvCheckMapRoomMove( elvElfX + 4, elvElfY + 16 ) == 0 )
                elvElfY = elvElfY + ELV_STEP_LENGTH;
        }
        else
        {
            elvScrollMap( ELV_SCROLL_DOWN );
            elvElfX = 36;
            elvElfY = 0;
            elvElfFacing = ELV_FACING_DOWN;
        }
    }
    else if( facing == ELV_FACING_UP )
    {
        if( elvElfY > 0 )
        {
            if( elvCheckMapRoomMove( elvElfX, elvElfY - 4 ) == 0 )
              if( elvCheckMapRoomMove( elvElfX + 4, elvElfY - 4 ) == 0 )
                elvElfY = elvElfY - ELV_STEP_LENGTH;
        }
        else
        {
            elvScrollMap( ELV_SCROLL_UP );
            elvElfX = 36;
            elvElfY = 40;
            elvElfFacing = ELV_FACING_UP;
        }
    }
    else if( facing == ELV_FACING_LEFT )
    {
        if( elvElfX > 0 )
        {
            if( elvCheckMapRoomMove( elvElfX - 4, elvElfY ) == 0 )
              if( elvCheckMapRoomMove( elvElfX - 4, elvElfY + 12 ) == 0 )
                elvElfX = elvElfX - ELV_STEP_LENGTH;
        }
        else
        {
            elvScrollMap( ELV_SCROLL_LEFT );
            elvElfX = 72;
            elvElfY = 16;
            elvElfFacing = ELV_FACING_LEFT;
        }
    }
    else if( facing == ELV_FACING_RIGHT )
    {
        if( elvElfX < 72 )
        {
            if( elvCheckMapRoomMove( elvElfX + 12, elvElfY ) == 0 )
              if( elvCheckMapRoomMove( elvElfX + 12, elvElfY + 12 ) == 0 )
                elvElfX = elvElfX + ELV_STEP_LENGTH;
        }
        else
        {
            elvScrollMap( ELV_SCROLL_RIGHT );
            elvElfX = 0;
            elvElfY = 16;
            elvElfFacing = ELV_FACING_RIGHT;
        }
    }
}

void elvThrowSword()
{
    // room element 0 is always the sword
    if( elvRoomElements[ 0 ].state == ELV_STATE_HIDDEN )
    {
        if( elvElfFacing == ELV_FACING_DOWN )
        {
            elvRoomElements[ 0 ].state = ELV_STATE_MOVE_DOWN;
            elvRoomElements[ 0 ].x = elvElfX;
            elvRoomElements[ 0 ].y = elvElfY + 16;
        }
        else if( elvElfFacing == ELV_FACING_UP )
        {
            elvRoomElements[ 0 ].state = ELV_STATE_MOVE_UP;
            elvRoomElements[ 0 ].x = elvElfX;
            elvRoomElements[ 0 ].y = elvElfY - 8;
        }
        else if( elvElfFacing == ELV_FACING_LEFT )
        {
            elvRoomElements[ 0 ].state = ELV_STATE_MOVE_LEFT;
            elvRoomElements[ 0 ].x = elvElfX - 8;
            elvRoomElements[ 0 ].y = elvElfY;
        }
        else if( elvElfFacing == ELV_FACING_RIGHT )
        {
            elvRoomElements[ 0 ].state = ELV_STATE_MOVE_RIGHT;
            elvRoomElements[ 0 ].x = elvElfX + 8;
            elvRoomElements[ 0 ].y = elvElfY;
        }
    }
}

void elvAddElfItem( int type )
{
    int count = 0;
    int i;

    for( i = 0; i < ELV_MAX_ITEMS; i = i + 1 )
    {
        if( elvElfItems[ i ] == 0 )
        {
            elvElfItems[ i ] = type;
            break;
        }
        else
        {
            count = count + 1;
        }
    }

    if( count == ELV_MAX_ITEMS )
    {
        // won the game (real upstream's own literal slot-counting logic -
        // see header comment)
        elvElfState = ELV_ELFSTATE_WON;
    }
    else
    {
        gbPlayOK();
    }
}

int elvElfHasItem( int type )
{
    int i;

    for( i = 0; i < ELV_MAX_ITEMS; i = i + 1 )
    {
        if( ( elvElfItems[ i ] > 50 ) && ( elvElfItems[ i ] == type ) )
          return 1;
    }

    return 0;
}

// =============================================================================
//   Monster (monster.cpp)
// =============================================================================

int elvChangeMonsterDirection()
{
    return arand( 4 ) + ELV_STATE_MOVE_UP;
}

void elvMoveMonster( int idx )
{
    if( elvRoomElements[ idx ].state <= ELV_STATE_HIDDEN )
      return;

    elvRoomElements[ idx ].step = elvRoomElements[ idx ].step + 1;
    if( elvRoomElements[ idx ].step > 2 )
      elvRoomElements[ idx ].step = 1;

    if( elvRoomElements[ idx ].state == ELV_STATE_VISIBLE )
    {
        elvRoomElements[ idx ].state = elvChangeMonsterDirection();
    }
    else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_UP )
    {
        elvRoomElements[ idx ].state = ELV_STATE_VISIBLE;
        if( elvRoomElements[ idx ].y > 4 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x, elvRoomElements[ idx ].y - 4 ) == 0 )
            {
                if( elvCheckMapRoomMove( elvRoomElements[ idx ].x + 4, elvRoomElements[ idx ].y - 4 ) == 0 )
                {
                    elvRoomElements[ idx ].y = elvRoomElements[ idx ].y - ELV_STEP_LENGTH;
                    elvRoomElements[ idx ].state = ELV_STATE_MOVE_UP;
                }
            }
        }
    }
    else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_DOWN )
    {
        elvRoomElements[ idx ].state = ELV_STATE_VISIBLE;
        if( elvRoomElements[ idx ].y < 40 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x, elvRoomElements[ idx ].y + 16 ) == 0 )
            {
                if( elvCheckMapRoomMove( elvRoomElements[ idx ].x + 4, elvRoomElements[ idx ].y + 16 ) == 0 )
                {
                    elvRoomElements[ idx ].y = elvRoomElements[ idx ].y + ELV_STEP_LENGTH;
                    elvRoomElements[ idx ].state = ELV_STATE_MOVE_DOWN;
                }
            }
        }
    }
    else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_LEFT )
    {
        elvRoomElements[ idx ].state = ELV_STATE_VISIBLE;
        if( elvRoomElements[ idx ].x > 4 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x - 4, elvRoomElements[ idx ].y ) == 0 )
            {
                if( elvCheckMapRoomMove( elvRoomElements[ idx ].x - 4, elvRoomElements[ idx ].y + 12 ) == 0 )
                {
                    elvRoomElements[ idx ].x = elvRoomElements[ idx ].x - ELV_STEP_LENGTH;
                    elvRoomElements[ idx ].state = ELV_STATE_MOVE_LEFT;
                }
            }
        }
    }
    else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_RIGHT )
    {
        elvRoomElements[ idx ].state = ELV_STATE_VISIBLE;
        if( elvRoomElements[ idx ].x < 72 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x + 12, elvRoomElements[ idx ].y ) == 0 )
            {
                if( elvCheckMapRoomMove( elvRoomElements[ idx ].x + 12, elvRoomElements[ idx ].y + 12 ) == 0 )
                {
                    elvRoomElements[ idx ].x = elvRoomElements[ idx ].x + ELV_STEP_LENGTH;
                    elvRoomElements[ idx ].state = ELV_STATE_MOVE_RIGHT;
                }
            }
        }
    }

    if( elvRoomElements[ idx ].counter > 0 )
      elvRoomElements[ idx ].counter = elvRoomElements[ idx ].counter - 1;
}

void elvHitMonster( int idx )
{
    int x = elvRoomElements[ idx ].x;
    int y = elvRoomElements[ idx ].y;

    elvRoomElements[ idx ].state = ELV_STATE_HIDDEN;

    // drop a heart where the monster died
    elvAddRoomElement( ELV_ITEM_HEART, x, y, ELV_STATE_VISIBLE, ELV_COUNTER_START );
}

// =============================================================================
//   Item (item.cpp)
// =============================================================================

void elvMoveItem( int idx )
{
    if( elvRoomElements[ idx ].state <= ELV_STATE_HIDDEN )
      return;

    elvRoomElements[ idx ].step = elvRoomElements[ idx ].step + 1;
    if( elvRoomElements[ idx ].step > 2 )
      elvRoomElements[ idx ].step = 1;

    if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_UP )
    {
        elvRoomElements[ idx ].state = ELV_STATE_HIDDEN;
        if( elvRoomElements[ idx ].y > 0 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x, elvRoomElements[ idx ].y - 4 ) == 0 )
            {
                elvRoomElements[ idx ].y = elvRoomElements[ idx ].y - ELV_STEP_LENGTH;
                elvRoomElements[ idx ].state = ELV_STATE_MOVE_UP;
            }
        }
    }
    else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_DOWN )
    {
        elvRoomElements[ idx ].state = ELV_STATE_HIDDEN;
        if( elvRoomElements[ idx ].y < 40 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x, elvRoomElements[ idx ].y + 8 ) == 0 )
            {
                elvRoomElements[ idx ].y = elvRoomElements[ idx ].y + ELV_STEP_LENGTH;
                elvRoomElements[ idx ].state = ELV_STATE_MOVE_DOWN;
            }
        }
    }
    else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_LEFT )
    {
        elvRoomElements[ idx ].state = ELV_STATE_HIDDEN;
        if( elvRoomElements[ idx ].x > 0 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x - 4, elvRoomElements[ idx ].y ) == 0 )
            {
                elvRoomElements[ idx ].x = elvRoomElements[ idx ].x - ELV_STEP_LENGTH;
                elvRoomElements[ idx ].state = ELV_STATE_MOVE_LEFT;
            }
        }
    }
    else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_RIGHT )
    {
        elvRoomElements[ idx ].state = ELV_STATE_HIDDEN;
        if( elvRoomElements[ idx ].x < 72 )
        {
            if( elvCheckMapRoomMove( elvRoomElements[ idx ].x + 12, elvRoomElements[ idx ].y ) == 0 )
            {
                elvRoomElements[ idx ].x = elvRoomElements[ idx ].x + ELV_STEP_LENGTH;
                elvRoomElements[ idx ].state = ELV_STATE_MOVE_RIGHT;
            }
        }
    }
    // no case for ELV_STATE_VISIBLE -> state stays ELV_STATE_VISIBLE (a
    // real, deliberate upstream idle-blink: a stationary item keeps
    // animating its own step frame forever)

    if( elvRoomElements[ idx ].counter > 0 )
      elvRoomElements[ idx ].counter = elvRoomElements[ idx ].counter - 1;
}

void elvHitItem( int idx )
{
    elvRoomElements[ idx ].state = ELV_STATE_HIDDEN;
}

// =============================================================================
//   Room element collision + tick handler (room.cpp)
// =============================================================================

int elvTestRoomElement( int idx, int testX, int testY, int ySize )
{
    int isHit = 0;
    int ex = elvRoomElements[ idx ].x;
    int ey = elvRoomElements[ idx ].y;
    int etype = elvRoomElements[ idx ].type;

    if( ( ( ex >= testX ) && ( ex <= testX + 8 ) ) || ( ( ex + 8 >= testX ) && ( ex + 8 <= testX + 8 ) ) )
    {
        if( etype < 50 )
        {
            if( ( ( ey >= testY ) && ( ey <= testY + ySize ) ) || ( ( ey + ySize >= testY ) && ( ey + ySize <= testY + ySize ) ) )
              isHit = 1;
        }
        else
        {
            if( ( ( ey >= testY ) && ( ey <= testY + 8 ) ) || ( ( ey + 8 >= testY ) && ( ey + 8 <= testY + 8 ) ) )
              isHit = 1;
        }
    }

    return isHit;
}

void elvHitElf( int idx )
{
    if( elvRoomElements[ idx ].type < 50 )
    {
        // hit by a monster
        if( elvRoomElements[ idx ].counter == 0 )
        {
            elvRoomElements[ idx ].counter = ELV_COUNTER_START;
            elvElfHearts = elvElfHearts - 1;
            gbPlayCancel();

            if( elvElfHearts < 1 )
              elvElfState = ELV_ELFSTATE_DEAD;
        }

        // bump: bounce the monster back the opposite way (every contact
        // tick, not just the tick a heart is lost - see header comment)
        if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_UP )
          elvRoomElements[ idx ].state = ELV_STATE_MOVE_DOWN;
        else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_DOWN )
          elvRoomElements[ idx ].state = ELV_STATE_MOVE_UP;
        else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_LEFT )
          elvRoomElements[ idx ].state = ELV_STATE_MOVE_RIGHT;
        else if( elvRoomElements[ idx ].state == ELV_STATE_MOVE_RIGHT )
          elvRoomElements[ idx ].state = ELV_STATE_MOVE_LEFT;
    }
    else if( elvRoomElements[ idx ].type == ELV_ITEM_HEART )
    {
        if( elvElfHearts < ELV_MAX_HEARTS )
          elvElfHearts = elvElfHearts + 1;
        elvHitItem( idx );
        gbPlayTick();
    }
    else if( ( elvRoomElements[ idx ].type == ELV_ITEM_CRYSTAL ) || ( elvRoomElements[ idx ].type == ELV_ITEM_ORB ) || ( elvRoomElements[ idx ].type == ELV_ITEM_ARMOR ) || ( elvRoomElements[ idx ].type == ELV_ITEM_STAFF ) )
    {
        elvAddElfItem( elvRoomElements[ idx ].type );
        elvHitItem( idx );
    }
    else if( elvRoomElements[ idx ].type == ELV_ITEM_PORTAL )
    {
        elvHitItem( idx );

        if( elvMapCurrRoom > 63 )
        {
            // go to the underworld
            elvSetMapRoom( 0 );
        }
        else
        {
            // back to the overworld
            elvSetMapRoom( 64 );
        }

        elvElfX = 36;
        elvElfY = 24;
        elvElfFacing = ELV_FACING_DOWN;
    }
}

void elvHandleRoomElements()
{
    int i;

    for( i = 0; i < elvElementCount; i = i + 1 )
    {
        if( elvRoomElements[ i ].state <= ELV_STATE_HIDDEN )
          continue;

        if( elvTestRoomElement( i, elvElfX, elvElfY, 16 ) )
          elvHitElf( i );

        if( elvRoomElements[ i ].type < 50 )
        {
            // room element 0 is always the sword
            if( elvRoomElements[ 0 ].state > ELV_STATE_HIDDEN )
            {
                if( elvTestRoomElement( i, elvRoomElements[ 0 ].x, elvRoomElements[ 0 ].y, 8 ) )
                {
                    elvHitMonster( i );
                    elvHitItem( 0 );
                }
            }

            elvMoveMonster( i );
        }
        else
        {
            elvMoveItem( i );

            // hide the heart once its timer has run out
            if( ( elvRoomElements[ i ].type == ELV_ITEM_HEART ) && ( elvRoomElements[ i ].counter == 0 ) )
              elvHitItem( i );
        }
    }
}

// =============================================================================
//   Rendering (display.cpp / bitmap_funcs.cpp / logo.cpp - see header
//   comment: real display.cpp/bitmap_funcs.cpp contain no custom per-pixel
//   masking code at all, every real draw call here maps directly onto this
//   shim's own already-optimized gbDrawBitmap()/gbFillRect() primitives)
// =============================================================================

void elvDrawHud()
{
    int i;
    int y;
    int xOff = 80;

    y = 8;
    for( i = 1; i <= ELV_MAX_HEARTS; i = i + 1 )
    {
        if( i <= elvElfHearts )
        {
            gbSetColor( GB_BLACK );
            gbDrawBitmap( xOff, y, elvDisplayBitmaps[ 1 ] );
        }
        else
        {
            gbSetColor( GB_WHITE );
            gbFillRect( xOff, y, 4, 4 );
            gbSetColor( GB_BLACK );
        }
        y = y + 5;
    }

    y = 28;
    for( i = 0; i < ELV_MAX_ITEMS; i = i + 1 )
    {
        if( elvElfItems[ i ] == ELV_ITEM_CRYSTAL )
        {
            gbSetColor( GB_BLACK );
            gbDrawBitmap( xOff, y, elvDisplayBitmaps[ 2 ] );
        }
        else if( elvElfItems[ i ] == ELV_ITEM_ARMOR )
        {
            gbSetColor( GB_BLACK );
            gbDrawBitmap( xOff, y, elvDisplayBitmaps[ 3 ] );
        }
        else if( elvElfItems[ i ] == ELV_ITEM_ORB )
        {
            gbSetColor( GB_BLACK );
            gbDrawBitmap( xOff, y, elvDisplayBitmaps[ 4 ] );
        }
        else if( elvElfItems[ i ] == ELV_ITEM_STAFF )
        {
            gbSetColor( GB_BLACK );
            gbDrawBitmap( xOff, y, elvDisplayBitmaps[ 5 ] );
        }
        else
        {
            gbSetColor( GB_WHITE );
            gbFillRect( xOff, y, 4, 4 );
            gbSetColor( GB_BLACK );
        }
        y = y + 5;
    }
}

void elvDrawFrame()
{
    int x, y, block, i;

    gbSetColor( GB_BLACK );

    for( y = 0; y < 6; y = y + 1 )
    {
        for( x = 0; x < 10; x = x + 1 )
        {
            block = elvGetMapBlock( x, y, -1 );
            gbDrawBitmap( x * 8, y * 8, elvMapBitmaps[ block ] );
        }
    }

    elvDrawHud();

    gbSetColor( GB_BLACK );
    gbDrawBitmap( elvElfX, elvElfY, elvElfBitmaps[ elvElfFacing ] );
    gbDrawBitmap( elvElfX, elvElfY + 8, elvElfBitmaps[ elvElfFacing + elvElfStep ] );

    for( i = 0; i < elvElementCount; i = i + 1 )
    {
        if( elvRoomElements[ i ].state <= ELV_STATE_HIDDEN )
          continue;

        if( elvRoomElements[ i ].type < 50 )
        {
            gbDrawBitmap( elvRoomElements[ i ].x, elvRoomElements[ i ].y, elvMonsterBitmaps[ elvRoomElements[ i ].type ] );
            gbDrawBitmap( elvRoomElements[ i ].x, elvRoomElements[ i ].y + 8, elvMonsterBitmaps[ elvRoomElements[ i ].type + elvRoomElements[ i ].step ] );
        }
        else
        {
            gbDrawBitmap( elvRoomElements[ i ].x, elvRoomElements[ i ].y, elvItemBitmaps[ ( elvRoomElements[ i ].type - 51 ) + elvRoomElements[ i ].step ] );
        }
    }
}

void elvDrawLogo( int start )
{
    int xOff = 26;
    int i;

    gbSetColor( GB_BLACK );
    for( i = start; i < start + 4; i = i + 1 )
    {
        gbDrawBitmap( xOff, 16, elvLogoBitmaps[ i ] );
        xOff = xOff + 8;
    }
}

// =============================================================================
//   Title / pause screens (real upstream's own blocking gb.titleScreen()
//   calls, ported as explicit states - see header comment)
// =============================================================================

void elvUpdateBootTitle()
{
    gbSetColor( GB_BLACK );
    gbSetFont( gbFont3x5 );

    gbDrawBitmap( 0, 12, elvTitleBitmap );

    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "TEAM ARG." );

    gbCursorX = 2;
    gbCursorY = 30;
    gbPrintString( "PORT: WUFF" );

    gbCursorX = 20;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        gbSetFrameRate( 10 );
        elvResetElf( 1 );
        elvSetMapRoom( 0 );
        elvState = ELV_GS_PLAYING;
    }
}

void elvUpdatePauseTitle()
{
    gbSetColor( GB_BLACK );
    gbSetFont( gbFont3x5 );

    gbDrawBitmap( 0, 12, elvTitleBitmap );

    gbCursorX = 2;
    gbCursorY = 2;
    gbPrintString( "PAUSED" );

    gbCursorX = 2;
    gbCursorY = 32;
    gbPrintString( "PRESS " );
    gbDrawChar( 21, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    gbPrintString( " TO RESUME" );

    if( gbPressed( BTN_A ) )
      elvState = ELV_GS_PLAYING;
}

void elvUpdateGameOver()
{
    elvDrawLogo( 4 );

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
    {
        elvResetElf( 0 );
        elvSetMapRoom( 0 );
        elvState = ELV_GS_PLAYING;
    }
}

void elvUpdateGameWon()
{
    elvDrawLogo( 8 );

    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
    {
        elvResetElf( 1 );
        elvSetMapRoom( 0 );
        elvState = ELV_GS_PLAYING;
    }
}

// =============================================================================
//   Main gameplay tick (ELV_TV_v10.ino's own loop() GAME_PLAYING case)
// =============================================================================

void elvUpdatePlaying()
{
    if( gbRepeat( BTN_UP, 1 ) )
      elvMoveElf( ELV_FACING_UP );
    if( gbRepeat( BTN_DOWN, 1 ) )
      elvMoveElf( ELV_FACING_DOWN );
    if( gbRepeat( BTN_RIGHT, 1 ) )
      elvMoveElf( ELV_FACING_RIGHT );
    if( gbRepeat( BTN_LEFT, 1 ) )
      elvMoveElf( ELV_FACING_LEFT );
    if( gbPressed( BTN_A ) )
      elvThrowSword();

    if( gbPressed( BTN_C ) )
    {
        // real upstream's own blocking pause - see header comment on the
        // deliberate single-tick resume-timing simplification
        elvState = ELV_GS_PAUSE;
        elvUpdatePauseTitle();
        return;
    }

    elvHandleRoomElements();

    if( elvElfState != ELV_ELFSTATE_PLAYING )
    {
        if( elvElfState == ELV_ELFSTATE_DEAD )
        {
            elvState = ELV_GS_GAME_OVER;
            elvUpdateGameOver();
        }
        else
        {
            elvState = ELV_GS_GAME_WON;
            elvUpdateGameWon();
        }
        return;
    }

    elvDrawFrame();
}

// =============================================================================
//   Entry points
// =============================================================================

void gameElventure_init()
{
    gbBegin();
    gbPickRandomSeed();
    gbSetFont( gbFont3x5 );

    elvState = ELV_GS_BOOT_TITLE;
    elvResetElf( 1 );
}

void gameElventure_update()
{
    if( !gbUpdate() )
      return;

    if( elvState == ELV_GS_BOOT_TITLE )
      elvUpdateBootTitle();
    else if( elvState == ELV_GS_PLAYING )
      elvUpdatePlaying();
    else if( elvState == ELV_GS_PAUSE )
      elvUpdatePauseTitle();
    else if( elvState == ELV_GS_GAME_OVER )
      elvUpdateGameOver();
    else if( elvState == ELV_GS_GAME_WON )
      elvUpdateGameWon();

    gbRenderFrame();
}
