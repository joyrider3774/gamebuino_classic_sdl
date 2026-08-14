#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Under the Tower (wuuff, GPLv3 - github.com/wuuff/under-the-tower, author
// confirmed directly via the real repo's own git history/remote, not
// guessed). A real turn-based RPG: an overworld town to walk around (real
// door-triggered transitions into interior "dungeons"), 10 real dungeons
// with procedurally-generated room layouts (a recursive BSP-style
// splitter, same shape ported here unmodified), random wandering-monster
// encounters plus real scripted boss fights and dialogue events tied to a
// single linear `game_status[STATUS_MAIN]` main-quest counter (plus five
// smaller side-quest counters), a 3-member party (Mudlark/Shadow/Nurse,
// the latter two recruited over the course of the story), a menu-driven
// battle system (attack/ability/item/run), and real EEPROM save/load.
//
// Ported as a flat, single-file state machine, matching this project's own
// established "flatten a real single-instance C/C++ game into plain C
// globals/functions" treatment - every real upstream file (overworld/
// dungeon/battle/dialogue/events/save .ino+.h) is folded in here under one
// `utt` prefix (this dialect has no per-file namespacing and the whole
// cartridge is one translation unit sharing one flat global namespace, so
// every real upstream #define/global needed renaming - RAT/WORLD/UP/DOOR/
// SHADOW/etc are all real, generic names that would otherwise collide with
// some other already-shipped game's own globals).
//
// DIALECT REWRITES:
// - No ternary operator anywhere in this dialect - every real upstream
//   `a?b:c` (menu-limit clamps, spawn-pool indexing, the `random(3)` vs
//   `random(2)` vs `0` enemy-target pick) rewritten as if/else.
// - `char dungeon_map[16][16]` (and every function that took it as a
//   `map[][MAPSIZE]` parameter) was flattened to one global 1D
//   `uttDungeonMap[256]` (`UTT_MAPIDX(row,col)` indexing) - there is only
//   ever one active dungeon at a time, so no parameter was needed at all
//   once flattened; this also sidesteps ever needing to pass a 2D array as
//   a function parameter, a pattern with no proven precedent anywhere in
//   this project's own history.
// - Every real upstream `struct` with more than one field (character
//   party stats, the enemy-buffer, the dungeon table's own per-entry
//   `spawns[3]`, the dialogue/boss event tables) was flattened to parallel
//   scalar arrays (`uttPartyLevel[3]`/`uttPartyHealth[3]`/etc) rather than
//   real struct types - not because structs themselves are unsupported
//   (they are - see e.g. gameFirebuino.c's own struct-free plain-array
//   style, or other ported games' real struct usage elsewhere), but
//   specifically because a real upstream `spawns[3]`-style ARRAY-typed
//   struct FIELD has no proven-working precedent in this project (flagged
//   explicitly as a deliberately-untested pattern during gamebuino-
//   solitaire's own port) - parallel arrays sidestep the question
//   entirely rather than being the first real test of it, given how much
//   state this one file already has to get right.
// - Real upstream's `const char name[][8]` string tables (player/enemy
//   names, menu labels, combat message fragments, item names) were ported
//   as real `int[N][8]` tables of explicit per-character ASCII codes
//   (proven-safe 2D numeric arrays, matching every other ported game's own
//   `int[N][8]`-shaped sound-effect/bitmap tables) rather than betting on
//   an unproven "string literal initializes one nested array row"
//   shorthand. Since a function can't take "which 2D table" as a generic
//   array-of-arrays parameter either, `uttNameChar(table,row,col)` (a
//   small `if`-chain over a `UTT_TBL_*` selector constant) replaces every
//   real upstream call site that used to pass `player_names`/
//   `enemy_names`/etc directly as an argument (`copy_to_buffer()`/
//   `append_to_msg_buffer()`/`display_dialogue()` all gained a `table`
//   parameter in place of their own real array parameter).
// - Real upstream's `F("\20")` (a Gamebuino icon-glyph escape, the cursor-
//   arrow bitmap builtin to font5x7/font3x5) can't live inside a quoted
//   string literal here - ported as an explicit `int[2] uttArrowIcon =
//   {16,0}` (ASCII 16 = the real glyph code), matching this project's own
//   already-established Taquin/Simonbuino/SpinSpinSpinbuino precedent for
//   the exact same real gap.
// - Real upstream's run-length-encoded `world[]` overworld map and the
//   huffman-compressed `dialogue[]`/`huff_tree[]` text-compression tables
//   were extracted byte-for-byte from the real PROGMEM data (every
//   `B01111111`-style Arduino binary literal converted straight to hex,
//   verified element counts against the real source before trusting them)
//   rather than re-derived or approximated - both are still real,
//   functioning run-length/huffman decoders here, ported as literal
//   algorithms operating on the same literal compressed bytes real
//   hardware ships.
//
// PLATFORM-FORCED FIXES (behavior changed from real upstream because the
// original would risk a crash/hang/undefined-memory-read on this platform
// specifically - not because it "looked wrong"):
// - **`COMPRESSED_SIZE` (2652) doesn't match the real `world[]` array's
//   own actual element count (2630, counted directly, not assumed)** - a
//   real, pre-existing upstream mismatch. On real AVR, `world_get()`'s own
//   search loop reads up to 22 bytes past the real array's own end before
//   giving up (harmless-in-practice PROGMEM overread on real hardware,
//   same class of issue as this project's own previously-documented
//   Pirates sprite-state OOB read). `uttWorldGet()` instead bounds its
//   search to the real, measured 2630-element size and returns a safe
//   fallback tile (`UTT_OW_WATER`, matching the real data's own trailing
//   filler tile) if the search ever exhausts the real data without
//   finding the requested index, rather than reading adjacent unrelated
//   globals.
// - **`NUM_DUNGEONS` (18) doesn't match the real `dungeons[]` table's own
//   actual entry count (12, counted directly from the real, uncommented
//   array - several more entries exist only inside a `/* ... */`-commented
//   alternate revision that was never compiled)** - another real upstream
//   mismatch. `uttStepWorld()`'s own door-to-dungeon lookup loop is
//   bounded to `UTT_NUM_DUNGEONS` (12, the real corrected count) instead,
//   avoiding a real 6-entry overread into whatever unrelated global
//   happens to sit next in memory.
// - **`uttCheckProximity()`'s own row-scan loop bound (`i+1+extra`, real
//   upstream `check_proximity()`) can read one row past `dungeon_map[16]
//   [16]`'s own real 0-15 range when called with `extra==1` near the
//   bottom map edge** - a real, if narrow, upstream OOB read (confirmed by
//   tracing the actual loop bounds against the real 16-row array, not
//   assumed). Fixed by skipping any row index outside 0-15 rather than
//   reading it.
// - **`uttMapExits()`'s three real `while(1)` random-placement retry loops
//   (entrance door / stairs down / stairs up) have no upper bound on real
//   hardware at all** - provably safe in practice given how the real
//   recursive room generator behaves, but a genuine unbounded-loop hang
//   risk in principle if a future map-gen change ever left no valid
//   placement at all. Each loop is now capped at 1000 attempts with a
//   guaranteed-safe fallback placement, matching this project's own
//   established "avoid a real hang, even one accepted as very unlikely on
//   paper" precedent - the cap is high enough to never actually change
//   real behavior in normal generation.
// - **A real division/modulo-by-zero hard-trap risk in `uttDoCombat()`'s
//   own cursor-wrap `combat_selection %= mod;`** - `mod` is provably >=1
//   under every real reachable game state (the ENEMY_MENU is only ever
//   entered while at least one enemy is still alive), but this platform
//   hard-traps the CPU on an actual division/modulo by zero rather than
//   silently producing garbage the way real AVR might - a defensive
//   `if(mod<1) mod=1;` guard was added purely as insurance against a
//   crash, not because the unguarded value was ever observed to reach
//   zero.
//
// A real, previously-undocumented int8_t-narrowing hazard, audited and
// fixed exactly per this project's own established EEPROM-narrowing
// methodology: real upstream's `game_status[]` is a genuine `int8_t`
// array whose very first element starts at -1 (`{-1,0,0,0,0,0}`, "quest
// not yet started"). Real `EEPROM.update(addr,int8_t)` implicitly narrows
// -1 to the byte 0xFF on write, and real `int8_t x = EEPROM.read(addr);`
// (an EEPROM.read() call assigned into a genuinely 8-bit signed variable)
// narrows 0xFF back to -1 on read - this dialect's own always-32-bit
// `int` never narrows either way, so a naive port would instead save 0xFF
// (255, correctly) but then load it back as +255, not -1, silently
// corrupting the "quest not started" sentinel into an unreachable status
// value forever after the very first save/load round-trip. Fixed with a
// small `uttNarrowS8()` helper (mirroring real AVR int8_t narrowing
// exactly: any raw byte >127 becomes `value-256`) applied to every
// `game_status[]` byte on load, and `& 0xFF` masking on save (already
// correct either way for a plain byte write, kept explicit for clarity).
// No other persisted value in this game is ever genuinely negative
// (health/level/xp/inventory/coordinates are all real non-negative
// `uint8_t`/`uint16_t` on real hardware), so this is the one and only
// real narrow-int save/load divergence this game has.
//
// UPSTREAM QUIRKS PRESERVED DELIBERATELY (confirmed real, not accidental,
// by tracing the actual logic rather than assumed):
// - `uttTestWorldCollision()`'s own UP-direction branch returns the real
//   raw tile value (needed to distinguish a real door tile from a plain
//   wall), while DOWN/LEFT/RIGHT return a plain boolean - a real, genuine
//   asymmetry (only building entrances, approached from below, need door
//   detection at all), not a bug, ported exactly as upstream wrote it.
// - `uttLoadEnemyData()`'s own boss branch unconditionally resets
//   `uttMetaMode` back to `UTT_DUNGEON` after loading the boss into the
//   combat's own center slot - meaning if `uttGenEnemies()`'s own 50/50
//   coin-flip then decides to also spawn a left/right companion, THOSE
//   calls see `uttMetaMode==UTT_DUNGEON` already and pull a normal,
//   dungeon-appropriate mook instead of a second boss copy. A real,
//   deliberate-reading-confirmed upstream quirk (a boss fight can
//   genuinely have 1-2 regular extra enemies alongside the real boss),
//   preserved exactly.
// - Real upstream's own `restore_game()` sets `mode=TO_WORLD` on a
//   successful load WITHOUT resetting `transition` to `-SCREEN_HEIGHT/2`
//   first - since `transition` is left over from whatever it last settled
//   at (>=0, either its untouched initial 0 on a fresh boot, or wherever
//   it stopped changing after the last real transition completed), the
//   real wipe-in animation never actually plays on a loaded game; the
//   world just cuts in instantly. A genuine, if minor, real upstream
//   oversight, preserved rather than "fixed" into a different experience
//   than real hardware ships.
// - Real `mapinit()`'s own border-wall loop rewrites the same four border
//   cells on every single inner-loop iteration (16x for each of the 16
//   outer iterations) instead of once - genuinely wasteful but harmless,
//   real upstream's own header comment already calls this out
//   ("Less runtime efficiency, but more space efficiency putting this
//   here") - ported exactly, not "optimized away".
// - The real VICTORY/DEFEAT message-dismiss branches inside `do_combat()`
//   both `return` immediately with NO trailing `drawRect()` call, unlike
//   every other path through the function, which falls through to one at
//   the very end - a real, if cosmetically minor, upstream quirk (the
//   combat divider rect just isn't freshly redrawn on the exact tick
//   victory/defeat is confirmed) - preserved exactly, not smoothed over.
//
// SIMPLIFIED FROM REAL UPSTREAM, DOCUMENTED RATHER THAN SILENTLY DROPPED:
// - Real upstream's scene transitions rely on real `gb.display.
//   persistence` (freeze the CPU framebuffer, don't auto-clear it every
//   tick) to let the growing/shrinking black wipe animation
//   (`step_transition()`) draw over a genuinely frozen previous scene for
//   the first half of the animation. This shim has no persistence concept
//   at all (`gbUpdate()` always clears+redraws every real tick, matching
//   every other game in this cartridge). The real INPUT-PROCESSING gate
//   (`if(transition>=0){...}`, which also stops the player from moving or
//   the dungeon regenerating while transition<0) is preserved byte-for-
//   byte - so there is zero gameplay-logic difference from real hardware.
//   The one real, deliberate, documented visual difference: during the
//   first half of a transition, this port's own screen shows the wipe
//   lines drawn over a blank cleared background rather than over a frozen
//   copy of the previous scene (no per-pixel snapshot/restore was
//   implemented for what is a purely cosmetic ~6-tick effect, given how
//   large this file already is) - the wipe's own signature growing/
//   shrinking black-bar shape, and its real per-case draw ordering
//   (world/dungeon draw UNDER the wipe, but combat draws OVER the wipe -
//   "Draw first to avoid the text overdrawing the transition", a real
//   upstream comment, preserved exactly), are both otherwise unchanged.
// - Real upstream's `gb.pickRandomSeed()`/`gb.changeGame()` (an SD-card
//   multi-cartridge menu handoff, triggered by the MAIN_MENU's own real
//   "QUIT" option) have no Vircon32 equivalent - `gbPickRandomSeed()` is
//   this shim's own already-established no-op (see gamebuinoShim.h), and
//   "QUIT" itself is left a deliberate no-op menu entry, matching this
//   project's own established precedent (gamePirates.c's own real
//   `load_game()` drop) that the cartridge's own global Start-button
//   quit-confirmation dialog already provides the equivalent "return to
//   the shared menu" functionality project-wide.
//
// No custom per-pixel bitmap-masking helper was needed anywhere in this
// port - every real upstream `drawBitmap()` call here draws a genuinely
// self-contained, non-mask-dependent sprite (matching `gbDrawBitmap()`
// directly, 1:1), unlike e.g. FlappyBirdo's/Pirates' own real outline-
// plus-mask sprite layers. No collision/hit-detection in this game
// depends on sprite facing/flip direction either - verified directly by
// reading `overworld.ino`/`dungeon.ino`/`battle.ino` in full: every real
// collision check here is a plain 4-directional tile lookahead
// (`uttTestWorldCollision()`/`uttTestCollision()`, checking the single
// tile the player is about to step into) or a pure turn-based menu
// selection in combat (no positional hitboxes of any kind) - there is no
// real-time movement collision or facing-dependent hurtbox anywhere in
// this game to get wrong.
//
// Sound: every real upstream sound call in this game (`playTick()`/
// `playOK()`/`playCancel()`) already matches this shim's own supported
// one-shot repertoire exactly - no approximation was needed at all, a
// rare case among this project's larger ports.

// -----------------------------------------------------------------------
// Mode / state constants (numeric values matter: uttMode-UTT_TRANSITION_DIFF
// is real arithmetic upstream itself relies on to convert a TO_* transition
// state back to its plain destination state once the wipe finishes).
// -----------------------------------------------------------------------
#define UTT_WORLD 0
#define UTT_COMBAT 1
#define UTT_DUNGEON 2
#define UTT_DIALOGUE 3
#define UTT_TO_WORLD 4
#define UTT_TO_COMBAT 5
#define UTT_TO_DUNGEON 6
#define UTT_MAIN_MENU 7
#define UTT_PAUSE_MENU 8
#define UTT_GAME_OVER 9
#define UTT_YOU_WIN 10
#define UTT_TRANSITION_DIFF 4

#define UTT_SCREEN_WIDTH 84
#define UTT_SCREEN_HEIGHT 48

#define UTT_UP 0
#define UTT_DOWN 1
#define UTT_LEFT 2
#define UTT_RIGHT 3

// Overworld tile ids actually referenced by draw/collision logic.
#define UTT_OW_DOOR 4
#define UTT_OW_DRAIN 5
#define UTT_OW_FLOW 7
#define UTT_OW_WATER 17
#define UTT_OW_DOCK1 21
#define UTT_OW_DOCK2 23
#define UTT_OW_DOCK3 43

// Dungeon tile ids.
#define UTT_DUN_TILE_DOOR 6
#define UTT_DUN_TILE_STAIRSUP 7
#define UTT_DUN_TILE_STAIRSDN 8
#define UTT_NUM_DUN_TILES 4
#define UTT_NUM_COMMON_TILES 4
#define UTT_DUN_WALL 9

// Dungeon themes (real upstream aliases several onto the same value -
// preserved exactly, not an error).
#define UTT_DUN_THEME_CATPAW 0
#define UTT_DUN_THEME_BRICK 1
#define UTT_DUN_THEME_SHIP 2
#define UTT_DUN_THEME_WAREHOUSE 1
#define UTT_DUN_THEME_HOSPITAL 3
#define UTT_DUN_THEME_HOUSE 0
#define UTT_DUN_THEME_TOWER 3

#define UTT_MAPSIZE 16
#define UTT_MAPIDX(r,c) ((r)*UTT_MAPSIZE+(c))
// Real count of the actual, uncommented `dungeons[]` table - see header
// comment (real upstream's own NUM_DUNGEONS=18 doesn't match).
#define UTT_NUM_DUNGEONS 12

#define UTT_HORIZONTAL 0
#define UTT_VERTICAL 1
#define UTT_MIN_WIDTH 6
#define UTT_MIN_HEIGHT 8
#define UTT_HALL_CHANCE 90
#define UTT_MIN_HALL_WIDTH 8
#define UTT_MIN_HALL_HEIGHT 10
#define UTT_MAX_HALL_WIDTH 12
#define UTT_MAX_HALL_HEIGHT 14
#define UTT_EXTRA_DOOR 10
#define UTT_DECORATION_CHANCE 20

// Combat character-role indices (also used as party array indices).
#define UTT_MUDLARK 0
#define UTT_SHADOW 1
#define UTT_NURSE 2

#define UTT_ITEM_FRUIT 0
#define UTT_ITEM_BREAD 1
#define UTT_ITEM_MEAT 2
#define UTT_ITEM_TONIC 3
#define UTT_ITEM_TEA 4
#define UTT_ITEM_LIQUOR 5
#define UTT_INVENTORY_SIZE 6
#define UTT_INVENTORY_MAX 8

#define UTT_MUDLARK_MENU 0
#define UTT_SHADOW_MENU 1
#define UTT_NURSE_MENU 2
#define UTT_SECONDARY_MENU 3
#define UTT_ENEMY_MENU 4
#define UTT_ALLY_MENU 5
#define UTT_FOOD_MENU 6
#define UTT_DRINK_MENU 7

#define UTT_PRECOMBAT -1
#define UTT_ENEMY1 3
#define UTT_ENEMY2 4
#define UTT_ENEMY3 5
#define UTT_MESSAGE 6
#define UTT_VICTORY 7
#define UTT_POSTCOMBAT 8
#define UTT_DEFEAT 9

#define UTT_PL2EN 0
#define UTT_EN2PL 1
#define UTT_EFALL 2
#define UTT_PWIN 3
#define UTT_PHEAL 4
#define UTT_PSPEED 5
#define UTT_PITEM 6
#define UTT_PROTECT 7
#define UTT_PDAMAGE 8
#define UTT_PDEFENSE 9
#define UTT_HEALALL 10
#define UTT_PFALL 11

#define UTT_STATUS_MAIN 0
#define UTT_STATUS_BAR 1
#define UTT_STATUS_MUTINY 2
#define UTT_STATUS_RATS 3
#define UTT_STATUS_SNOBS 4
#define UTT_STATUS_WARES 5

// Name/text table selectors for uttNameChar()/uttAppendToMsgBuffer()/etc,
// standing in for real upstream passing the table array itself.
#define UTT_TBL_PLAYER 0
#define UTT_TBL_ENEMY 1
#define UTT_TBL_MENU 2
#define UTT_TBL_COMBAT 3
#define UTT_TBL_ITEM 4

// Enemy indices (RAT..EN_SHADOW, real upstream battle.h).
#define UTT_RAT 0
#define UTT_SCAMP 1
#define UTT_RUFFIAN 2
#define UTT_THUG 3
#define UTT_BIG_RAT 4
#define UTT_PATRON 5
#define UTT_BOUNCER 6
#define UTT_SLAVER 7
#define UTT_SEA_RAT 8
#define UTT_SWABBIE 9
#define UTT_SAILOR 10
#define UTT_SKIPPER 11
#define UTT_CAPTAIN 12
#define UTT_BAD_RAT 13
#define UTT_WATCHER 14
#define UTT_BRUISER 15
#define UTT_MUSCLER 16
#define UTT_DOCTOR 17
#define UTT_PATIENT 18
#define UTT_SUBJECT 19
#define UTT_MUTANT 20
#define UTT_MADMAN 21
#define UTT_WOW_RAT 22
#define UTT_SNOB 23
#define UTT_RICHMAN 24
#define UTT_MAX_RAT 25
#define UTT_GUARD 26
#define UTT_GOLEM 27
#define UTT_OFFICER 28
#define UTT_LEADER 29
#define UTT_CRAB 30
#define UTT_EN_SHADOW 31

// Dialogue byte-offsets into uttDialogue[] (real upstream dialogue.h).
#define UTT_TXT_INTRO 0
#define UTT_TXT_INTRO_LEN 6
#define UTT_TXT_SDW_INTRO 228
#define UTT_TXT_SDW_INTRO_LEN 5
#define UTT_TXT_SDW_CATPAW 460
#define UTT_TXT_SDW_CATPAW_LEN 0
#define UTT_TXT_SLAVER 483
#define UTT_TXT_SLAVER_LEN 0
#define UTT_TXT_GIRL_THX 523
#define UTT_TXT_GIRL_THX_LEN 2
#define UTT_TXT_SDW_BATTLE 603
#define UTT_TXT_SDW_BATTLE_LEN 0
#define UTT_TXT_SDW_WIN 646
#define UTT_TXT_SDW_WIN_LEN 0
#define UTT_TXT_GIRL_FATHER 685
#define UTT_TXT_GIRL_FATHER_LEN 0
#define UTT_TXT_ENEMY 718
#define UTT_TXT_ENEMY_LEN 0
#define UTT_TXT_FATHER 734
#define UTT_TXT_FATHER_LEN 3
#define UTT_TXT_SDW_SHIP 892
#define UTT_TXT_SDW_SHIP_LEN 2
#define UTT_TXT_NSE_THX 1008
#define UTT_TXT_NSE_THX_LEN 3
#define UTT_TXT_SDW_RETURN 1124
#define UTT_TXT_SDW_RETURN_LEN 0
#define UTT_TXT_NSE_SUSPICIOUS 1155
#define UTT_TXT_NSE_SUSPICIOUS_LEN 1
#define UTT_TXT_NSE_CHAOS 1220
#define UTT_TXT_NSE_CHAOS_LEN 1
#define UTT_TXT_MAD_ESCAPED 1260
#define UTT_TXT_MAD_ESCAPED_LEN 0
#define UTT_TXT_NSE_WHY 1275
#define UTT_TXT_NSE_WHY_LEN 0
#define UTT_TXT_MAD_OFFER 1300
#define UTT_TXT_MAD_OFFER_LEN 1
#define UTT_TXT_NSE_SPREAD 1351
#define UTT_TXT_NSE_SPREAD_LEN 0
#define UTT_TXT_MAD_MISHAP 1376
#define UTT_TXT_MAD_MISHAP_LEN 1
#define UTT_TXT_NSE_MONSTER 1411
#define UTT_TXT_NSE_MONSTER_LEN 0
#define UTT_TXT_NSE_CONFRONT 1423
#define UTT_TXT_NSE_CONFRONT_LEN 0
#define UTT_TXT_SDW_TOWER 1472
#define UTT_TXT_SDW_TOWER_LEN 1
#define UTT_TXT_LDR_ANTS 1527
#define UTT_TXT_LDR_ANTS_LEN 0
#define UTT_TXT_NSE_PAY 1564
#define UTT_TXT_NSE_PAY_LEN 0
#define UTT_TXT_LDR_CLEAN 1594
#define UTT_TXT_LDR_CLEAN_LEN 0
#define UTT_TXT_NSE_DESTROY 1632
#define UTT_TXT_NSE_DESTROY_LEN 0
#define UTT_TXT_SDW_VICTORY 1655
#define UTT_TXT_SDW_VICTORY_LEN 2
#define UTT_TXT_NSE_VICTORY 1734
#define UTT_TXT_NSE_VICTORY_LEN 0
#define UTT_TXT_ENDING 1772
#define UTT_TXT_ENDING_LEN 3
#define UTT_TXT_BAR 1905
#define UTT_TXT_BAR_LEN 0
#define UTT_TXT_FIGHTER 1934
#define UTT_TXT_FIGHTER_LEN 1
#define UTT_TXT_MUTINY 1964
#define UTT_TXT_MUTINY_LEN 0
#define UTT_TXT_MUTINEER 2005
#define UTT_TXT_MUTINEER_LEN 0
#define UTT_TXT_CAPTAIN_THX 2022
#define UTT_TXT_CAPTAIN_THX_LEN 0
#define UTT_TXT_RATS 2051
#define UTT_TXT_RATS_LEN 0
#define UTT_TXT_MEGARAT 2089
#define UTT_TXT_MEGARAT_LEN 0
#define UTT_TXT_RAT_THX 2121
#define UTT_TXT_RAT_THX_LEN 0
#define UTT_TXT_SNOBS 2149
#define UTT_TXT_SNOBS_LEN 0
#define UTT_TXT_RICH 2171
#define UTT_TXT_RICH_LEN 0
#define UTT_TXT_SNOB_WOW 2186
#define UTT_TXT_SNOB_WOW_LEN 0
#define UTT_TXT_WARES 2222
#define UTT_TXT_WARES_LEN 1
#define UTT_TXT_WARES_THX 2274
#define UTT_TXT_WARES_THX_LEN 0

#define UTT_NUM_DIALOGUE_EVENTS 58
#define UTT_NUM_BOSS_EVENTS 11
// Real element count of the actual `world[]` RLE stream - see header
// comment (real upstream's own COMPRESSED_SIZE=2652 doesn't match).
#define UTT_WORLD_SIZE 2630
#define UTT_DIALOGUE_SIZE 1237

// -----------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------
int uttMode;
int uttTransition;
int uttMetaMode;
int uttMenuSelection;

int uttPlayerMoving;
int uttDudeX;
int uttDudeY;
int uttDudeAnimation;
int uttDudeFrame;
int uttWorldFrame;

int uttWorldNdx;
int uttWorldCnt;

int uttDungeonId;
int uttDungeonGenerated;
int uttDungeonLevel;
int uttPreviousLevel;
int uttDungeonMap[256];

int uttEnemyBufLvl[3];
int uttEnemyBufSpd[3];
int uttEnemyBufImg[3];
int uttEnemyBufNme[3];

int uttPartyLevel[3];
int uttPartyHealth[3];
int uttPartySpeed[3];
int uttPartyXp[3];
int uttPartyBonusSpeed[3];
int uttPartyBonusDamage[3];
int uttPartyBonusDefense[3];

int uttShadowStealthBonus;
int uttNurseProtectBonus;

int uttInventory[6];
int uttNextCombat;

int uttCombatMode;
int uttCombatSelection;
char uttCombatBuffer[8];
char uttCombatMessage[64];
int uttCombatStatus[6];
int uttCombatXp;
int uttIsBoss;
int uttEnemyHealth[3];

int uttDialogueIndex;
int uttDialogueRemaining;
int uttHuffIndex;
int uttHuffTreeIndex;
int uttHuffMask;

int uttGameStatus[6];

char uttArrowIcon[2]= {16,0};

// -----------------------------------------------------------------------
// Bitmap tables - real upstream PROGMEM byte data, `B01111111`-style
// binary literals converted straight to hex, verified element counts
// against the real source before trusting them (see header comment).
// -----------------------------------------------------------------------

// Player sprite sheet: Up/Down/Left/Right x 3 animation frames, 10 ints
// each (8,8 header + 8 row bytes) - real overworld.ino player_sprites[].
int uttPlayerSprites[120]= {
8, 8, 0x18, 0x66, 0x81, 0x81, 0x42, 0xBC, 0x5A, 0x21, 8, 8, 0x18, 0x66, 0x81, 0x81, 0x42, 0x3C, 0xDB, 0x24,
8, 8, 0x18, 0x66, 0x81, 0x81, 0x42, 0x3D, 0x5A, 0x84, 8, 8, 0x18, 0x66, 0x81, 0xA5, 0x42, 0xBC, 0x5A, 0x21,
8, 8, 0x18, 0x66, 0x81, 0xA5, 0x42, 0x3C, 0xDB, 0x24, 8, 8, 0x18, 0x66, 0x81, 0xA5, 0x42, 0x3D, 0x5A, 0x84,
8, 8, 0x18, 0x66, 0x81, 0xA1, 0x42, 0x3C, 0x5A, 0x14, 8, 8, 0x18, 0x66, 0x81, 0xA1, 0x42, 0x3C, 0x5A, 0x24,
8, 8, 0x18, 0x66, 0x81, 0xA1, 0x42, 0x3C, 0x5A, 0x28, 8, 8, 0x18, 0x66, 0x81, 0x85, 0x42, 0x3C, 0x5A, 0x28,
8, 8, 0x18, 0x66, 0x81, 0x85, 0x42, 0x3C, 0x5A, 0x24, 8, 8, 0x18, 0x66, 0x81, 0x85, 0x42, 0x3C, 0x5A, 0x14,
};

// Overworld tile atlas (56 tiles, 10 ints each) - real overworld.ino tiles[].
int uttTiles[560]= {
8, 8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 8, 8, 0x22, 0xFF, 0x88, 0xFF, 0x22, 0xFF, 0x88, 0xFF,
8, 8, 0x81, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x81, 0xFF, 8, 8, 0x18, 0x66, 0x81, 0xFF, 0x81, 0x81, 0xDB, 0xFF,
8, 8, 0xFF, 0x81, 0x81, 0x81, 0xC1, 0x81, 0x81, 0x81, 8, 8, 0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0x85, 0x56, 0x52,
8, 8, 0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0x91, 0x56, 0x46, 8, 8, 0x52, 0x42, 0x42, 0x4A, 0x4A, 0x4A, 0x42, 0x52,
8, 8, 0x46, 0x52, 0x52, 0x52, 0x42, 0x4A, 0x4A, 0x4A, 8, 8, 0x3A, 0xFF, 0x98, 0xFF, 0x3A, 0xFF, 0x98, 0xFF,
8, 8, 0x00, 0xFF, 0xAA, 0x55, 0x00, 0xAA, 0x55, 0xFF, 8, 8, 0x3C, 0x66, 0xFF, 0xA5, 0xA5, 0xFF, 0x66, 0x3C,
8, 8, 0x22, 0x66, 0x88, 0x99, 0x22, 0x66, 0x88, 0x99, 8, 8, 0x22, 0x54, 0x88, 0x15, 0x22, 0x54, 0x88, 0x15,
8, 8, 0x3A, 0xFF, 0xA4, 0xE7, 0x26, 0xE7, 0x98, 0xFF, 8, 8, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55, 0x88, 0x55,
8, 8, 0x22, 0x55, 0x00, 0x55, 0x22, 0x55, 0x00, 0x55, 8, 8, 0x00, 0x20, 0x5C, 0x00, 0x00, 0x04, 0x1A, 0x00,
8, 8, 0x00, 0x01, 0xE2, 0x00, 0x00, 0x80, 0x43, 0x00, 8, 8, 0xFF, 0x99, 0x99, 0xFF, 0x99, 0x99, 0xFF, 0xFF,
8, 8, 0xFF, 0x55, 0x36, 0x14, 0x14, 0x14, 0x14, 0x14, 8, 8, 0x80, 0x90, 0xAC, 0x80, 0x80, 0x84, 0xBA, 0x80,
8, 8, 0x80, 0x84, 0xBA, 0x80, 0x80, 0x90, 0xAC, 0x80, 8, 8, 0x02, 0x1D, 0x00, 0x08, 0x74, 0x00, 0x00, 0xFF,
8, 8, 0x20, 0x5C, 0x00, 0x04, 0x0B, 0x00, 0x00, 0xFF, 8, 8, 0xFF, 0x89, 0x91, 0xA3, 0xC5, 0x89, 0x91, 0xFF,
8, 8, 0x00, 0xFF, 0xA1, 0x99, 0xC5, 0xA3, 0x99, 0xFF, 8, 8, 0x01, 0x01, 0x01, 0xFF, 0x10, 0x10, 0x10, 0xFF,
8, 8, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 8, 8, 0x00, 0x03, 0x0C, 0xF0, 0xC0, 0x00, 0x00, 0x00,
8, 8, 0x00, 0x00, 0x00, 0xFF, 0x01, 0x0E, 0x70, 0x80, 8, 8, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xFF,
8, 8, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 8, 8, 0x00, 0x00, 0x58, 0x66, 0xBA, 0xBD, 0xE3, 0xFF,
8, 8, 0x00, 0x04, 0x5A, 0x32, 0x66, 0x3B, 0x66, 0x10, 8, 8, 0x18, 0xDB, 0xC3, 0x08, 0xDE, 0xDE, 0x1C, 0x00,
8, 8, 0x18, 0x18, 0x18, 0xFF, 0xFF, 0x18, 0x18, 0x18, 8, 8, 0x03, 0x0C, 0x30, 0xD0, 0x13, 0x1D, 0x31, 0xC1,
8, 8, 0xC0, 0x30, 0x0C, 0x0B, 0xC8, 0xB8, 0x8C, 0x83, 8, 8, 0x03, 0x0C, 0x30, 0xD0, 0x13, 0x1C, 0x30, 0xC0,
8, 8, 0x03, 0x0C, 0x30, 0xC0, 0x00, 0x00, 0x00, 0x00, 8, 8, 0xC0, 0x30, 0x0C, 0x0B, 0xC8, 0x38, 0x0C, 0x03,
8, 8, 0xC0, 0x30, 0x0C, 0x03, 0x00, 0x00, 0x00, 0x00, 8, 8, 0x80, 0x90, 0xAE, 0x80, 0x88, 0xB4, 0x80, 0xFF,
8, 8, 0x80, 0x84, 0x9A, 0x80, 0x90, 0xAE, 0x80, 0xFF, 8, 8, 0xFF, 0xD5, 0xB6, 0x94, 0x94, 0x94, 0x94, 0x94,
8, 8, 0x00, 0x00, 0x00, 0xFF, 0x80, 0x70, 0x0E, 0xFF, 8, 8, 0x00, 0xC0, 0x30, 0x0F, 0x03, 0x00, 0x00, 0xFF,
8, 8, 0x10, 0x08, 0x08, 0x10, 0x08, 0x08, 0x04, 0x08, 8, 8, 0x00, 0x00, 0x18, 0x24, 0x13, 0x10, 0x08, 0x10,
8, 8, 0x00, 0x00, 0x0C, 0x92, 0x61, 0x00, 0x00, 0x00, 8, 8, 0x00, 0x00, 0x18, 0x24, 0xC2, 0x04, 0x08, 0x10,
8, 8, 0x08, 0x10, 0x10, 0x08, 0x04, 0x08, 0x10, 0x08, 8, 8, 0x10, 0x08, 0x04, 0x08, 0xC4, 0x22, 0x1C, 0x00,
8, 8, 0x00, 0x00, 0x00, 0x69, 0x96, 0x00, 0x00, 0x00, 8, 8, 0x10, 0x20, 0x20, 0x41, 0x22, 0x1A, 0x04, 0x00,
};

// Dungeon-theme tile atlas (4 themes x 4 tiles, 10 ints each) - real
// dungeon.ino dungeon_tiles[].
int uttDungeonTiles[160]= {
8, 8, 0x55, 0x55, 0x55, 0x55, 0xFF, 0x81, 0x81, 0xFF, 8, 8, 0x7E, 0x81, 0x81, 0xC3, 0xBD, 0x81, 0x81, 0x81,
8, 8, 0x81, 0x81, 0x81, 0xE7, 0xBD, 0x81, 0xBD, 0xC3, 8, 8, 0x00, 0x00, 0x7E, 0x81, 0x7E, 0x18, 0x3C, 0x42,
8, 8, 0x22, 0xFF, 0x88, 0xFF, 0x22, 0xFF, 0x88, 0xFF, 8, 8, 0x00, 0xFF, 0xA1, 0x99, 0xC5, 0xA3, 0x99, 0xFF,
8, 8, 0xFF, 0x89, 0x91, 0xA3, 0xC5, 0x89, 0x91, 0xFF, 8, 8, 0x18, 0x66, 0x81, 0xE7, 0x99, 0x81, 0xE7, 0xFF,
8, 8, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 8, 8, 0x18, 0x66, 0x81, 0xE7, 0x99, 0x81, 0xE7, 0xFF,
8, 8, 0x99, 0x81, 0xE7, 0xFF, 0x99, 0x81, 0x66, 0x18, 8, 8, 0xFF, 0x81, 0x81, 0xFF, 0x87, 0x99, 0xE1, 0xFF,
8, 8, 0x01, 0x01, 0x01, 0xFF, 0x10, 0x10, 0x10, 0xFF, 8, 8, 0x7E, 0x81, 0x81, 0xC3, 0xBD, 0x81, 0x81, 0x81,
8, 8, 0x81, 0x81, 0x81, 0xE7, 0xBD, 0x81, 0xBD, 0xC3, 8, 8, 0x1C, 0x36, 0xEC, 0xDE, 0x5B, 0x3B, 0x77, 0xDC,
};

// Common (theme-independent) dungeon tiles: door / stairs up / stairs
// down / wall-top (4 tiles, 10 ints each) - real dungeon.ino common_tiles[].
int uttCommonTiles[40]= {
8, 8, 0xFF, 0x81, 0x81, 0x81, 0xC1, 0x81, 0x81, 0x81, 8, 8, 0xF0, 0x9C, 0x97, 0x95, 0x95, 0xF5, 0x9D, 0x87,
8, 8, 0xFF, 0xFF, 0xBF, 0xAF, 0xAB, 0xAB, 0xAB, 0xFF, 8, 8, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55,
};

// Enemy portrait sheet (8 portraits, 8x16, 18 ints each) - real
// battle.ino enemybmps[]. Only the first 8 img indices used by real
// enemies[].img are ever actually drawn (img values run 0-7).
int uttEnemyBmps[144]= {
8, 16, 0x40, 0x78, 0x2C, 0x24, 0x2A, 0x1D, 0x21, 0x25, 0x25, 0x56, 0x94, 0x1C, 0x14, 0x32, 0xE2, 0x83, 8, 16,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x52, 0x79, 0xFE, 0x7C, 8, 16, 0x00, 0x00,
0x18, 0x18, 0x3C, 0x7E, 0xFF, 0xF7, 0xF9, 0x8E, 0x7E, 0x7E, 0x66, 0x66, 0x24, 0x66, 8, 16, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x42, 0x81, 0xC3, 0x81, 0xBD, 0x42, 0xA9, 0x81, 0x7E, 0x24, 8, 16, 0x3C, 0x7C, 0xA4, 0x24, 0x3C, 0x7E,
0xC3, 0xFF, 0xA5, 0xBD, 0x24, 0x3C, 0x7E, 0x42, 0x42, 0xC3, 8, 16, 0x18, 0x3C, 0x3C, 0x24, 0x18, 0xFF, 0x81, 0xE7,
0xA5, 0xA5, 0x24, 0x24, 0x24, 0x3C, 0x24, 0x66, 8, 16, 0xC3, 0x81, 0xBD, 0xA5, 0xBD, 0xFF, 0x5A, 0x66, 0x42, 0x7E,
0x3C, 0x7C, 0x6C, 0x6C, 0x6E, 0xE6, 8, 16, 0x3C, 0x3C, 0x7E, 0x24, 0x24, 0x7E, 0xFF, 0xBD, 0xBD, 0xBD, 0x3C, 0x3C,
0x7E, 0x42, 0x42, 0xC3,
};

// Overworld tile-index map, real run-length-encoded (count,tile) pairs -
// real overworld.ino world[], the terminating pair (255,17)+(6,17) is a
// real, deliberate "off the encoded map" filler (see uttWorldGet()).
int uttWorldMap[2630]= {
11, 15, 1, 2, 5, 15, 1, 2, 3, 1, 4, 38, 8, 27, 4, 37, 5, 1, 1, 2, 1, 1, 1, 2, 13, 1, 1, 2,
5, 17, 11, 1, 1, 2, 5, 1, 1, 2, 3, 1, 4, 38, 1, 27, 1, 11, 4, 27, 1, 11, 1, 27, 4, 37, 5, 1,
1, 2, 1, 1, 1, 2, 13, 1, 1, 2, 5, 17, 7, 1, 1, 19, 1, 1, 1, 19, 1, 1, 1, 2, 1, 1, 1, 19,
1, 1, 1, 19, 1, 1, 1, 2, 3, 1, 4, 38, 8, 27, 4, 37, 5, 1, 1, 2, 1, 1, 1, 2, 5, 1, 2, 19,
6, 1, 1, 2, 5, 17, 10, 1, 1, 14, 1, 2, 1, 14, 3, 1, 1, 14, 1, 2, 1, 1, 1, 14, 1, 1, 4, 38,
1, 27, 1, 11, 4, 27, 1, 11, 1, 27, 4, 37, 5, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 14, 1, 1, 1, 14,
1, 1, 2, 19, 1, 1, 1, 14, 4, 1, 1, 2, 5, 17, 8, 1, 1, 4, 1, 1, 1, 9, 1, 2, 1, 9, 1, 1,
1, 4, 1, 1, 1, 9, 1, 2, 1, 1, 1, 9, 1, 1, 4, 38, 8, 27, 4, 37, 1, 1, 1, 14, 1, 1, 1, 14,
1, 1, 1, 2, 1, 0, 1, 2, 1, 1, 1, 9, 1, 1, 1, 9, 4, 1, 1, 9, 1, 1, 1, 4, 2, 1, 1, 2,
5, 17, 11, 0, 1, 32, 9, 0, 4, 38, 1, 27, 1, 11, 4, 27, 1, 11, 1, 27, 4, 37, 1, 1, 1, 9, 1, 1,
1, 9, 1, 1, 1, 2, 16, 0, 1, 21, 4, 17, 11, 0, 1, 32, 9, 0, 4, 38, 8, 27, 4, 37, 21, 0, 1, 26,
1, 21, 4, 17, 11, 10, 1, 3, 5, 10, 1, 3, 3, 0, 4, 38, 1, 27, 1, 11, 4, 27, 1, 11, 1, 27, 4, 37,
6, 0, 1, 3, 12, 10, 1, 3, 1, 0, 1, 25, 1, 21, 4, 17, 11, 15, 1, 2, 5, 15, 1, 2, 3, 0, 4, 38,
8, 27, 4, 37, 6, 0, 1, 2, 12, 15, 1, 2, 2, 0, 1, 21, 4, 17, 11, 15, 1, 2, 5, 15, 1, 2, 3, 0,
4, 38, 1, 27, 1, 11, 4, 27, 1, 11, 1, 27, 4, 37, 4, 0, 1, 3, 1, 10, 1, 2, 12, 15, 1, 2, 1, 10,
1, 3, 5, 17, 11, 1, 1, 2, 5, 1, 1, 2, 3, 0, 4, 38, 8, 27, 4, 37, 4, 0, 1, 32, 1, 27, 1, 2,
12, 27, 1, 2, 1, 27, 1, 32, 5, 17, 11, 1, 1, 2, 1, 1, 1, 19, 1, 1, 1, 19, 1, 1, 1, 2, 3, 0,
4, 38, 1, 27, 1, 11, 4, 27, 1, 11, 1, 27, 4, 37, 4, 0, 1, 32, 1, 27, 1, 2, 1, 27, 1, 19, 1, 27,
1, 19, 4, 27, 1, 19, 1, 27, 1, 19, 1, 27, 1, 2, 1, 27, 1, 32, 5, 17, 11, 1, 1, 2, 1, 14, 3, 1,
1, 14, 1, 2, 3, 0, 4, 38, 8, 27, 4, 37, 4, 0, 1, 32, 1, 0, 1, 2, 12, 27, 1, 2, 1, 0, 1, 32,
5, 17, 11, 1, 1, 2, 1, 9, 1, 1, 1, 4, 1, 1, 1, 9, 1, 2, 3, 0, 4, 38, 1, 27, 1, 11, 4, 27,
1, 11, 1, 27, 4, 37, 4, 0, 1, 32, 1, 0, 1, 2, 1, 27, 1, 19, 1, 27, 1, 19, 4, 27, 1, 19, 1, 27,
1, 19, 1, 27, 1, 2, 1, 0, 1, 32, 5, 17, 15, 0, 1, 32, 5, 0, 4, 38, 8, 27, 4, 37, 4, 0, 1, 32,
1, 0, 1, 2, 12, 27, 1, 2, 1, 0, 1, 32, 5, 17, 15, 0, 1, 32, 5, 0, 1, 42, 1, 41, 2, 38, 1, 27,
1, 11, 4, 27, 1, 11, 1, 27, 1, 39, 1, 37, 1, 39, 1, 40, 4, 0, 1, 32, 1, 0, 1, 2, 1, 27, 1, 19,
1, 27, 1, 19, 1, 36, 2, 27, 1, 36, 1, 19, 1, 27, 1, 19, 1, 27, 1, 2, 1, 0, 1, 32, 5, 17, 17, 10,
1, 3, 5, 0, 1, 42, 1, 41, 3, 27, 2, 4, 3, 27, 1, 39, 1, 40, 6, 0, 1, 32, 1, 0, 1, 2, 5, 27,
2, 4, 5, 27, 1, 2, 1, 0, 1, 32, 5, 17, 17, 15, 1, 2, 23, 0, 1, 32, 16, 0, 1, 32, 5, 17, 3, 10,
1, 3, 13, 15, 1, 2, 3, 0, 1, 26, 19, 0, 1, 32, 16, 0, 1, 32, 5, 17, 3, 12, 1, 2, 4, 1, 1, 3,
3, 10, 1, 3, 4, 1, 1, 2, 3, 0, 1, 25, 5, 0, 1, 26, 13, 0, 1, 32, 6, 0, 1, 36, 2, 0, 1, 36,
6, 0, 1, 32, 5, 17, 3, 12, 1, 2, 4, 1, 1, 2, 3, 12, 1, 2, 2, 1, 1, 19, 1, 1, 1, 2, 9, 0,
1, 25, 1, 26, 7, 0, 1, 26, 4, 0, 1, 3, 6, 10, 1, 3, 2, 0, 1, 3, 6, 10, 1, 3, 5, 17, 3, 1,
1, 2, 4, 10, 1, 2, 3, 12, 1, 2, 4, 1, 1, 2, 10, 0, 1, 25, 6, 0, 1, 26, 1, 25, 4, 0, 1, 2,
6, 27, 1, 2, 2, 0, 1, 2, 6, 27, 1, 2, 5, 17, 3, 1, 1, 2, 4, 1, 1, 2, 1, 19, 1, 1, 1, 19,
1, 2, 2, 1, 1, 19, 1, 1, 1, 2, 17, 0, 1, 25, 5, 0, 1, 2, 6, 27, 1, 2, 2, 0, 1, 2, 6, 27,
1, 2, 5, 17, 3, 1, 1, 2, 4, 1, 1, 2, 1, 1, 1, 35, 1, 1, 1, 2, 4, 1, 1, 2, 40, 0, 1, 26,
5, 17, 5, 10, 1, 3, 2, 0, 1, 2, 1, 19, 1, 4, 1, 19, 1, 3, 16, 10, 1, 3, 27, 0, 1, 26, 1, 25,
5, 17, 5, 12, 1, 2, 6, 0, 1, 2, 16, 13, 1, 2, 1, 3, 2, 10, 1, 3, 23, 0, 2, 25, 5, 17, 5, 12,
1, 2, 6, 0, 1, 2, 16, 13, 2, 2, 2, 1, 1, 2, 1, 3, 11, 10, 1, 3, 4, 0, 1, 3, 6, 10, 1, 3,
5, 17, 5, 1, 1, 2, 6, 0, 1, 2, 4, 19, 3, 27, 4, 19, 3, 27, 2, 19, 2, 2, 2, 1, 2, 2, 11, 13,
1, 2, 4, 0, 1, 2, 6, 12, 1, 2, 5, 17, 5, 1, 1, 2, 6, 0, 1, 2, 16, 27, 1, 2, 4, 0, 1, 2,
11, 13, 1, 2, 1, 3, 2, 0, 1, 3, 1, 2, 6, 12, 1, 2, 5, 17, 2, 1, 1, 5, 1, 1, 1, 5, 1, 2,
6, 0, 1, 2, 4, 19, 13, 27, 4, 0, 1, 2, 11, 1, 2, 2, 2, 0, 2, 2, 6, 1, 1, 2, 5, 17, 2, 1,
1, 7, 1, 1, 1, 7, 1, 2, 1, 54, 1, 3, 2, 0, 1, 3, 1, 50, 2, 54, 1, 50, 1, 51, 18, 0, 1, 2,
3, 1, 1, 19, 2, 1, 1, 19, 2, 1, 1, 19, 1, 1, 1, 2, 4, 0, 1, 2, 1, 1, 1, 19, 2, 1, 1, 19,
1, 1, 1, 2, 12, 17, 1, 32, 2, 0, 1, 32, 4, 17, 1, 55, 1, 51, 17, 0, 1, 2, 1, 1, 1, 4, 9, 1,
1, 2, 4, 0, 1, 2, 2, 1, 1, 4, 3, 1, 1, 2, 12, 17, 1, 3, 2, 0, 1, 3, 5, 17, 1, 48, 1, 3,
13, 10, 7, 0, 1, 3, 20, 0, 1, 21, 4, 17, 2, 54, 1, 50, 1, 54, 1, 50, 2, 54, 1, 2, 2, 0, 1, 2,
1, 54, 1, 50, 1, 51, 2, 17, 1, 48, 1, 2, 13, 13, 7, 0, 1, 3, 20, 0, 1, 21, 4, 17, 6, 10, 1, 3,
6, 0, 1, 48, 2, 17, 1, 52, 1, 2, 13, 13, 7, 0, 1, 3, 20, 0, 1, 21, 4, 17, 6, 12, 1, 2, 6, 0,
1, 48, 2, 17, 1, 52, 1, 2, 13, 27, 7, 0, 1, 3, 20, 0, 1, 21, 4, 17, 6, 12, 1, 2, 6, 0, 1, 52,
2, 17, 1, 52, 1, 2, 11, 27, 1, 3, 10, 10, 1, 3, 4, 0, 1, 45, 13, 20, 5, 17, 6, 1, 1, 2, 6, 0,
1, 52, 2, 17, 1, 52, 1, 2, 1, 19, 3, 27, 1, 19, 3, 27, 1, 19, 2, 27, 1, 2, 10, 13, 1, 2, 4, 0,
1, 21, 18, 17, 1, 1, 4, 19, 1, 1, 1, 2, 6, 0, 1, 48, 2, 17, 1, 48, 1, 2, 11, 27, 1, 2, 10, 13,
1, 2, 4, 0, 1, 21, 6, 17, 1, 20, 4, 17, 1, 20, 3, 17, 1, 20, 2, 17, 6, 1, 1, 2, 6, 0, 1, 3,
2, 10, 1, 3, 12, 0, 1, 2, 10, 1, 1, 2, 4, 0, 1, 21, 6, 17, 1, 20, 4, 17, 1, 20, 3, 17, 1, 20,
2, 17, 5, 10, 1, 3, 7, 0, 1, 2, 2, 1, 1, 2, 12, 0, 1, 2, 10, 1, 1, 2, 4, 0, 1, 43, 3, 23,
3, 17, 1, 32, 4, 17, 1, 32, 3, 17, 1, 32, 2, 17, 5, 12, 1, 2, 12, 0, 1, 3, 7, 10, 1, 3, 2, 0,
1, 2, 10, 1, 1, 2, 6, 0, 1, 26, 1, 0, 1, 21, 2, 23, 1, 31, 4, 23, 1, 31, 3, 23, 1, 31, 2, 23,
5, 12, 1, 2, 7, 0, 1, 3, 2, 10, 1, 3, 1, 0, 1, 2, 7, 12, 1, 2, 2, 0, 1, 2, 10, 1, 1, 2,
6, 0, 1, 25, 1, 0, 1, 43, 1, 47, 1, 46, 4, 28, 1, 4, 3, 28, 1, 11, 1, 28, 1, 30, 1, 29, 5, 1,
1, 2, 1, 0, 1, 3, 3, 10, 1, 3, 1, 0, 1, 2, 2, 1, 1, 2, 1, 0, 1, 2, 7, 1, 1, 2, 4, 0,
1, 3, 4, 10, 1, 3, 7, 10, 1, 3, 9, 0, 1, 26, 4, 0, 1, 28, 1, 30, 1, 29, 2, 17, 2, 1, 2, 19,
1, 1, 1, 2, 1, 0, 1, 2, 3, 12, 1, 2, 1, 0, 1, 2, 2, 17, 1, 2, 1, 0, 1, 2, 1, 5, 1, 1,
1, 5, 1, 1, 1, 5, 1, 1, 1, 5, 1, 2, 4, 0, 1, 2, 4, 12, 1, 2, 7, 13, 1, 2, 6, 0, 1, 26,
2, 0, 1, 25, 1, 26, 3, 0, 1, 21, 4, 17, 2, 1, 2, 19, 1, 1, 1, 2, 1, 0, 1, 2, 3, 1, 1, 2,
1, 49, 1, 53, 2, 17, 1, 48, 1, 0, 1, 2, 1, 7, 1, 1, 1, 7, 1, 1, 1, 7, 1, 1, 1, 7, 1, 2,
4, 0, 1, 2, 4, 12, 1, 2, 7, 13, 1, 2, 6, 0, 1, 25, 1, 26, 2, 0, 1, 25, 2, 0, 1, 26, 1, 21,
4, 17, 5, 10, 1, 3, 1, 0, 1, 2, 1, 5, 1, 1, 1, 5, 1, 2, 1, 48, 3, 17, 1, 55, 1, 50, 1, 2,
1, 7, 1, 1, 1, 7, 1, 1, 1, 7, 1, 1, 1, 7, 1, 2, 1, 51, 3, 0, 1, 2, 4, 1, 1, 2, 7, 1,
1, 2, 6, 0, 2, 25, 5, 0, 1, 25, 1, 21, 4, 17, 5, 12, 1, 2, 1, 0, 1, 2, 1, 7, 1, 1, 1, 7,
1, 2, 1, 53, 14, 17, 1, 55, 1, 51, 2, 0, 1, 2, 4, 1, 1, 2, 2, 19, 3, 1, 2, 19, 1, 2, 4, 0,
1, 45, 9, 20, 5, 17, 5, 12, 1, 2, 1, 0, 1, 52, 20, 17, 1, 55, 1, 51, 1, 0, 1, 2, 4, 1, 1, 2,
3, 1, 1, 4, 3, 1, 1, 2, 4, 0, 1, 21, 14, 17, 5, 1, 1, 2, 1, 0, 1, 48, 9, 17, 1, 49, 1, 50,
1, 54, 1, 51, 8, 17, 1, 52, 19, 0, 1, 21, 14, 17, 2, 1, 2, 19, 1, 1, 1, 2, 1, 0, 1, 55, 1, 54,
1, 51, 7, 17, 1, 48, 2, 0, 1, 55, 1, 51, 7, 17, 1, 55, 1, 54, 1, 51, 3, 0, 1, 3, 8, 10, 1, 3,
4, 0, 1, 21, 11, 23, 3, 17, 2, 1, 2, 19, 1, 1, 1, 2, 3, 0, 1, 55, 1, 50, 1, 54, 1, 50, 1, 54,
1, 50, 1, 54, 1, 50, 1, 53, 3, 0, 1, 55, 1, 54, 1, 50, 1, 54, 1, 50, 1, 51, 4, 17, 1, 48, 3, 0,
1, 2, 8, 13, 1, 2, 4, 0, 1, 43, 1, 47, 1, 46, 5, 28, 1, 4, 1, 28, 1, 30, 1, 29, 3, 17, 5, 1,
1, 2, 6, 0, 1, 33, 1, 34, 1, 0, 1, 34, 3, 0, 1, 34, 4, 0, 1, 34, 1, 0, 1, 48, 4, 17, 1, 52,
3, 0, 1, 2, 8, 13, 1, 2, 14, 0, 1, 21, 4, 17, 8, 10, 1, 3, 3, 0, 1, 33, 4, 0, 1, 34, 1, 33,
3, 0, 1, 34, 3, 0, 1, 48, 4, 17, 1, 48, 3, 0, 1, 2, 8, 1, 1, 2, 14, 0, 1, 21, 4, 17, 8, 12,
1, 2, 4, 0, 1, 34, 2, 0, 1, 34, 7, 0, 1, 33, 1, 34, 1, 48, 4, 17, 1, 3, 3, 0, 1, 2, 8, 1,
1, 2, 4, 0, 1, 45, 9, 20, 5, 17, 8, 12, 1, 2, 3, 0, 1, 34, 3, 0, 1, 34, 1, 0, 1, 34, 4, 0,
1, 33, 2, 0, 1, 55, 1, 51, 3, 17, 1, 32, 3, 0, 1, 2, 1, 1, 1, 14, 5, 1, 1, 14, 1, 2, 4, 0,
1, 21, 7, 17, 1, 26, 6, 17, 8, 1, 1, 2, 3, 0, 1, 33, 4, 0, 1, 33, 3, 0, 1, 34, 1, 0, 1, 34,
3, 0, 1, 48, 3, 17, 1, 3, 3, 0, 1, 2, 1, 1, 1, 9, 2, 1, 1, 4, 2, 1, 1, 9, 1, 2, 4, 0,
1, 43, 6, 23, 1, 26, 1, 25, 1, 26, 5, 17, 8, 1, 1, 2, 2, 0, 1, 33, 4, 0, 1, 34, 3, 0, 1, 33,
1, 0, 1, 34, 4, 0, 1, 52, 3, 17, 1, 32, 24, 0, 3, 25, 1, 21, 4, 17, 5, 1, 1, 5, 1, 1, 1, 5,
1, 2, 5, 0, 1, 49, 2, 54, 1, 51, 7, 0, 1, 34, 1, 49, 1, 53, 3, 17, 1, 3, 1, 0, 1, 3, 1, 0,
1, 3, 1, 0, 1, 3, 1, 0, 1, 3, 1, 0, 1, 3, 1, 0, 1, 3, 1, 0, 1, 3, 1, 0, 1, 3, 11, 0,
1, 21, 4, 17, 5, 1, 1, 7, 1, 1, 1, 7, 1, 2, 2, 54, 1, 50, 1, 54, 1, 50, 1, 53, 2, 17, 1, 55,
2, 50, 1, 54, 2, 50, 1, 54, 2, 50, 1, 53, 4, 17, 1, 2, 1, 20, 1, 2, 1, 20, 1, 2, 1, 20, 1, 2,
1, 20, 1, 2, 1, 20, 1, 2, 1, 20, 1, 2, 1, 20, 1, 2, 1, 20, 1, 2, 11, 20, 255, 17, 6, 17,
};

// -----------------------------------------------------------------------
// Name/text tables - real upstream `const char x[][8]` PROGMEM string
// tables, ported as explicit per-character ASCII codes (see header
// comment for why, not a string-literal initializer).
// -----------------------------------------------------------------------
int uttPlayerNames[7][8]= {
  {77,85,68,76,65,82,75,0},
  {83,72,65,68,79,87,0,0},
  {78,85,82,83,69,0,0,0},
  {71,73,82,76,0,0,0,0},
  {70,65,84,72,69,82,0,0},
  {0,0,0,0,0,0,0,0},
  {77,65,78,0,0,0,0,0},
};

int uttEnemyNames[32][8]= {
  {82,65,84,0,0,0,0,0},
  {83,67,65,77,80,0,0,0},
  {82,85,70,70,73,65,78,0},
  {84,72,85,71,0,0,0,0},
  {66,73,71,32,82,65,84,0},
  {80,65,84,82,79,78,0,0},
  {66,79,85,78,67,69,82,0},
  {83,76,65,86,69,82,0,0},
  {83,69,65,32,82,65,84,0},
  {83,87,65,66,66,73,69,0},
  {83,65,73,76,79,82,0,0},
  {83,75,73,80,80,69,82,0},
  {67,65,80,84,65,73,78,0},
  {66,65,68,32,82,65,84,0},
  {87,65,84,67,72,69,82,0},
  {66,82,85,73,83,69,82,0},
  {77,85,83,67,76,69,82,0},
  {68,79,67,84,79,82,0,0},
  {80,65,84,73,69,78,84,0},
  {83,85,66,74,69,67,84,0},
  {77,85,84,65,78,84,0,0},
  {77,65,68,77,65,78,0,0},
  {87,79,87,32,82,65,84,0},
  {83,78,79,66,0,0,0,0},
  {82,73,67,72,77,65,78,0},
  {77,65,88,32,82,65,84,0},
  {71,85,65,82,68,0,0,0},
  {71,79,76,69,77,0,0,0},
  {79,70,70,73,67,69,82,0},
  {76,69,65,68,69,82,0,0},
  {67,82,65,66,0,0,0,0},
  {83,72,65,68,79,87,0,0},
};

int uttMenuText[16][8]= {
  {70,76,65,73,76,0,0,0},
  {82,65,76,76,89,0,0,0},
  {83,69,65,82,67,72,0,0},
  {79,84,72,69,82,0,0,0},
  {83,84,82,73,75,69,0,0},
  {86,65,78,73,83,72,0,0},
  {72,65,83,84,69,0,0,0},
  {79,84,72,69,82,0,0,0},
  {72,80,32,79,78,69,0,0},
  {72,80,32,65,76,76,0,0},
  {80,82,79,84,69,67,84,0},
  {79,84,72,69,82,0,0,0},
  {70,79,79,68,0,0,0,0},
  {68,82,73,78,75,0,0,0},
  {82,85,78,0,0,0,0,0},
  {0,0,0,0,0,0,0,0},
};

int uttCombatText[18][8]= {
  {32,72,73,84,32,0,0,0},
  {10,32,70,79,82,32,0,0},
  {32,68,65,77,65,71,69,0},
  {32,72,69,65,76,32,0,0},
  {32,70,65,76,76,83,0,0},
  {89,79,85,32,87,73,78,0},
  {32,88,80,32,71,69,84,0},
  {84,82,65,80,80,69,68,0},
  {32,83,80,68,32,85,80,0},
  {32,70,79,85,78,68,10,0},
  {10,32,71,73,86,69,32,0},
  {84,79,32,87,72,79,63,0},
  {80,82,79,84,69,67,84,0},
  {32,68,77,71,32,85,80,0},
  {32,68,69,70,32,85,80,0},
  {65,76,76,0,0,0,0,0},
  {66,65,71,32,77,65,88,0},
  {32,85,83,69,83,32,0,0},
};

int uttItemNames[6][8]= {
  {70,82,85,73,84,0,0,0},
  {66,82,69,65,68,0,0,0},
  {77,69,65,84,0,0,0,0},
  {84,79,78,73,67,0,0,0},
  {84,69,65,0,0,0,0,0},
  {76,73,81,85,79,82,0,0},
};

// -----------------------------------------------------------------------
// Dungeon table (real upstream `struct dungeon dungeons[]`, flattened to
// parallel arrays - see header comment). Real, uncommented entry count is
// 12, not the stale `NUM_DUNGEONS 18` upstream itself never updated.
// -----------------------------------------------------------------------
int uttDungeonX[12]= {10,40,40,57,56,54,50,49,55,14,28,29};
int uttDungeonY[12]= {24,56,48,51,42,31,16,16,4,4,16,16};
int uttDungeonTheme[12]= {UTT_DUN_THEME_CATPAW,UTT_DUN_THEME_HOUSE,UTT_DUN_THEME_BRICK,UTT_DUN_THEME_SHIP,UTT_DUN_THEME_SHIP,UTT_DUN_THEME_WAREHOUSE,UTT_DUN_THEME_HOSPITAL,UTT_DUN_THEME_HOSPITAL,UTT_DUN_THEME_WAREHOUSE,UTT_DUN_THEME_HOUSE,UTT_DUN_THEME_TOWER,UTT_DUN_THEME_TOWER};
int uttDungeonSize[12]= {3,3,5,2,4,4,7,7,6,2,15,15};
int uttDungeonSpawns[12][3]= {
  {UTT_BIG_RAT,UTT_PATRON,UTT_BOUNCER},
  {UTT_THUG,UTT_SEA_RAT,UTT_SWABBIE},
  {UTT_SAILOR,UTT_SEA_RAT,UTT_SWABBIE},
  {UTT_SWABBIE,UTT_SAILOR,UTT_SKIPPER},
  {UTT_SWABBIE,UTT_SAILOR,UTT_SKIPPER},
  {UTT_WATCHER,UTT_BRUISER,UTT_MUSCLER},
  {UTT_DOCTOR,UTT_PATIENT,UTT_SUBJECT},
  {UTT_DOCTOR,UTT_PATIENT,UTT_SUBJECT},
  {UTT_WOW_RAT,UTT_WOW_RAT,UTT_WOW_RAT},
  {UTT_SNOB,UTT_RICHMAN,UTT_SNOB},
  {UTT_GUARD,UTT_GOLEM,UTT_OFFICER},
  {UTT_GUARD,UTT_GOLEM,UTT_OFFICER},
};

// Real overworld region-based enemy spawn pools (4x4 map regions x 3
// candidates each) - real battle.ino world_spawns[4][4][3], [y-region]
// [x-region][pool-slot].
int uttWorldSpawns[4][4][3]= {
  {
    {UTT_WOW_RAT,UTT_SNOB,UTT_RICHMAN},
    {UTT_WOW_RAT,UTT_SNOB,UTT_RICHMAN},
    {UTT_WOW_RAT,UTT_PATIENT,UTT_SUBJECT},
    {UTT_WOW_RAT,UTT_SUBJECT,UTT_MUTANT},
  },
  {
    {UTT_RAT,UTT_PATRON,UTT_BOUNCER},
    {UTT_BAD_RAT,UTT_BRUISER,UTT_MUSCLER},
    {UTT_WATCHER,UTT_BRUISER,UTT_DOCTOR},
    {UTT_MUSCLER,UTT_DOCTOR,UTT_PATIENT},
  },
  {
    {UTT_RAT,UTT_RUFFIAN,UTT_THUG},
    {UTT_BIG_RAT,UTT_RUFFIAN,UTT_THUG},
    {UTT_SEA_RAT,UTT_SKIPPER,UTT_WATCHER},
    {UTT_SEA_RAT,UTT_SAILOR,UTT_SKIPPER},
  },
  {
    {UTT_RAT,UTT_SCAMP,UTT_RUFFIAN},
    {UTT_RAT,UTT_CRAB,UTT_SCAMP},
    {UTT_SEA_RAT,UTT_SWABBIE,UTT_SAILOR},
    {UTT_SEA_RAT,UTT_SAILOR,UTT_SKIPPER},
  },
};

// Enemy stat table (lvl,spd,img per row, index = enemy id UTT_RAT..
// UTT_EN_SHADOW) - real battle.ino enemies[] (struct min_enemy).
int uttEnemies[32][3]= {
  {1,5,1}, {1,4,0}, {2,3,0}, {4,2,0}, {4,5,1}, {5,4,0}, {5,5,2}, {10,5,0},
  {8,6,1}, {9,4,4}, {11,3,4}, {10,5,4}, {20,4,4}, {14,7,1}, {15,5,0}, {19,3,2},
  {24,2,2}, {25,4,5}, {26,5,0}, {26,6,0}, {26,7,0}, {32,8,5}, {30,7,1}, {34,3,0},
  {37,2,0}, {42,8,1}, {40,4,2}, {45,2,6}, {42,5,0}, {50,8,7}, {1,2,3}, {10,7,0},
};

// -----------------------------------------------------------------------
// Dialogue/boss event tables (real events.ino, flattened to parallel
// arrays - see header comment).
// -----------------------------------------------------------------------
int uttDlgEvStatusIdx[58]= {UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_BAR,UTT_STATUS_BAR,UTT_STATUS_MUTINY,UTT_STATUS_MUTINY,UTT_STATUS_MUTINY,UTT_STATUS_RATS,UTT_STATUS_RATS,UTT_STATUS_RATS,UTT_STATUS_SNOBS,UTT_STATUS_SNOBS,UTT_STATUS_SNOBS,UTT_STATUS_WARES,UTT_STATUS_WARES,UTT_STATUS_WARES};
int uttDlgEvStatusVal[58]= {2,4,5,7,8,9,11,12,13,15,16,17,18,18,19,19,20,20,21,21,22,22,23,23,24,24,26,26,27,27,28,28,29,29,30,30,31,31,33,33,34,34,35,35,0,1,0,1,3,0,1,3,0,1,3,0,1,3};
int uttDlgEvDungeonId[58]= {0,0,0,0,1,1,1,4,4,4,4,4,6,7,6,7,6,7,6,7,6,7,6,7,6,7,6,7,10,11,10,11,10,11,10,11,10,11,10,11,10,11,10,11,2,2,3,3,3,8,8,8,9,9,9,5,5,5};
int uttDlgEvDungeonLevel[58]= {2,2,0,0,0,2,2,0,3,3,3,3,0,0,6,6,6,6,6,6,6,6,6,6,6,6,6,6,0,0,14,14,14,14,14,14,14,14,14,14,14,14,14,14,0,4,0,1,0,0,5,0,0,1,0,0,3,0};
int uttDlgEvDialogueIdx[58]= {UTT_TXT_SLAVER,UTT_TXT_GIRL_THX,UTT_TXT_SDW_BATTLE,UTT_TXT_SDW_WIN,UTT_TXT_GIRL_FATHER,UTT_TXT_ENEMY,UTT_TXT_FATHER,UTT_TXT_SDW_SHIP,UTT_TXT_ENEMY,UTT_TXT_NSE_THX,UTT_TXT_SDW_RETURN,UTT_TXT_NSE_SUSPICIOUS,UTT_TXT_NSE_CHAOS,UTT_TXT_NSE_CHAOS,UTT_TXT_MAD_ESCAPED,UTT_TXT_MAD_ESCAPED,UTT_TXT_NSE_WHY,UTT_TXT_NSE_WHY,UTT_TXT_MAD_OFFER,UTT_TXT_MAD_OFFER,UTT_TXT_NSE_SPREAD,UTT_TXT_NSE_SPREAD,UTT_TXT_MAD_MISHAP,UTT_TXT_MAD_MISHAP,UTT_TXT_NSE_MONSTER,UTT_TXT_NSE_MONSTER,UTT_TXT_NSE_CONFRONT,UTT_TXT_NSE_CONFRONT,UTT_TXT_SDW_TOWER,UTT_TXT_SDW_TOWER,UTT_TXT_LDR_ANTS,UTT_TXT_LDR_ANTS,UTT_TXT_NSE_PAY,UTT_TXT_NSE_PAY,UTT_TXT_LDR_CLEAN,UTT_TXT_LDR_CLEAN,UTT_TXT_NSE_DESTROY,UTT_TXT_NSE_DESTROY,UTT_TXT_SDW_VICTORY,UTT_TXT_SDW_VICTORY,UTT_TXT_NSE_VICTORY,UTT_TXT_NSE_VICTORY,UTT_TXT_ENDING,UTT_TXT_ENDING,UTT_TXT_BAR,UTT_TXT_FIGHTER,UTT_TXT_MUTINY,UTT_TXT_MUTINEER,UTT_TXT_CAPTAIN_THX,UTT_TXT_RATS,UTT_TXT_MEGARAT,UTT_TXT_RAT_THX,UTT_TXT_SNOBS,UTT_TXT_RICH,UTT_TXT_SNOB_WOW,UTT_TXT_WARES,UTT_TXT_ENEMY,UTT_TXT_WARES_THX};
int uttDlgEvDialogueLen[58]= {UTT_TXT_SLAVER_LEN,UTT_TXT_GIRL_THX_LEN,UTT_TXT_SDW_BATTLE_LEN,UTT_TXT_SDW_WIN_LEN,UTT_TXT_GIRL_FATHER_LEN,UTT_TXT_ENEMY_LEN,UTT_TXT_FATHER_LEN,UTT_TXT_SDW_SHIP_LEN,UTT_TXT_ENEMY_LEN,UTT_TXT_NSE_THX_LEN,UTT_TXT_SDW_RETURN_LEN,UTT_TXT_NSE_SUSPICIOUS_LEN,UTT_TXT_NSE_CHAOS_LEN,UTT_TXT_NSE_CHAOS_LEN,UTT_TXT_MAD_ESCAPED_LEN,UTT_TXT_MAD_ESCAPED_LEN,UTT_TXT_NSE_WHY_LEN,UTT_TXT_NSE_WHY_LEN,UTT_TXT_MAD_OFFER_LEN,UTT_TXT_MAD_OFFER_LEN,UTT_TXT_NSE_SPREAD_LEN,UTT_TXT_NSE_SPREAD_LEN,UTT_TXT_MAD_MISHAP_LEN,UTT_TXT_MAD_MISHAP_LEN,UTT_TXT_NSE_MONSTER_LEN,UTT_TXT_NSE_MONSTER_LEN,UTT_TXT_NSE_CONFRONT_LEN,UTT_TXT_NSE_CONFRONT_LEN,UTT_TXT_SDW_TOWER_LEN,UTT_TXT_SDW_TOWER_LEN,UTT_TXT_LDR_ANTS_LEN,UTT_TXT_LDR_ANTS_LEN,UTT_TXT_NSE_PAY_LEN,UTT_TXT_NSE_PAY_LEN,UTT_TXT_LDR_CLEAN_LEN,UTT_TXT_LDR_CLEAN_LEN,UTT_TXT_NSE_DESTROY_LEN,UTT_TXT_NSE_DESTROY_LEN,UTT_TXT_SDW_VICTORY_LEN,UTT_TXT_SDW_VICTORY_LEN,UTT_TXT_NSE_VICTORY_LEN,UTT_TXT_NSE_VICTORY_LEN,UTT_TXT_ENDING_LEN,UTT_TXT_ENDING_LEN,UTT_TXT_BAR_LEN,UTT_TXT_FIGHTER_LEN,UTT_TXT_MUTINY_LEN,UTT_TXT_MUTINEER_LEN,UTT_TXT_CAPTAIN_THX_LEN,UTT_TXT_RATS_LEN,UTT_TXT_MEGARAT_LEN,UTT_TXT_RAT_THX_LEN,UTT_TXT_SNOBS_LEN,UTT_TXT_RICH_LEN,UTT_TXT_SNOB_WOW_LEN,UTT_TXT_WARES_LEN,UTT_TXT_ENEMY_LEN,UTT_TXT_WARES_THX_LEN};
int uttDlgEvNameIdx[58]= {UTT_SLAVER,3,UTT_SHADOW,UTT_SHADOW,3,UTT_BRUISER,4,UTT_SHADOW,UTT_CAPTAIN,UTT_NURSE,UTT_SHADOW,UTT_NURSE,UTT_NURSE,UTT_NURSE,UTT_MADMAN,UTT_MADMAN,UTT_NURSE,UTT_NURSE,UTT_MADMAN,UTT_MADMAN,UTT_NURSE,UTT_NURSE,UTT_MADMAN,UTT_MADMAN,UTT_NURSE,UTT_NURSE,UTT_NURSE,UTT_NURSE,UTT_SHADOW,UTT_SHADOW,UTT_LEADER,UTT_LEADER,UTT_NURSE,UTT_NURSE,UTT_LEADER,UTT_LEADER,UTT_NURSE,UTT_NURSE,UTT_SHADOW,UTT_SHADOW,UTT_NURSE,UTT_NURSE,5,5,6,UTT_MUSCLER,UTT_CAPTAIN,UTT_SAILOR,UTT_CAPTAIN,6,5,6,UTT_SNOB,UTT_RICHMAN,UTT_SNOB,6,UTT_MUSCLER,6};

int uttBossEvStatusIdx[11]= {UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_MAIN,UTT_STATUS_BAR,UTT_STATUS_MUTINY,UTT_STATUS_RATS,UTT_STATUS_SNOBS,UTT_STATUS_WARES};
int uttBossEvStatusVal[11]= {3,6,10,14,25,32,2,2,2,2,2};
int uttBossEvBossId[11]= {UTT_SLAVER,UTT_EN_SHADOW,UTT_BRUISER,UTT_CAPTAIN,UTT_MADMAN,UTT_LEADER,UTT_MUSCLER,UTT_CAPTAIN,UTT_MAX_RAT,UTT_RICHMAN,UTT_MUSCLER};

// -----------------------------------------------------------------------
// Huffman tree + compressed dialogue text (real dialogue.ino huff_tree[]/
// dialogue[]). Leaf rows store the real decoded byte (255 = internal
// node, decode via left/right child index in columns 1/2).
// -----------------------------------------------------------------------
int uttHuffTree[53][3]= {
  {255,1,24}, {255,2,15}, {255,3,4}, {69,0,0}, {255,5,14}, {255,6,7}, {89,0,0}, {255,8,13},
  {255,9,12}, {255,10,11}, {74,0,0}, {63,0,0}, {86,0,0}, {71,0,0}, {73,0,0}, {255,16,19},
  {255,17,18}, {83,0,0}, {79,0,0}, {255,20,23}, {255,21,22}, {68,0,0}, {76,0,0}, {72,0,0},
  {255,25,46}, {255,26,33}, {255,27,28}, {65,0,0}, {255,29,30}, {85,0,0}, {255,31,32}, {67,0,0},
  {70,0,0}, {255,34,41}, {255,35,36}, {0,0,0}, {255,37,38}, {80,0,0}, {255,39,40}, {75,0,0},
  {66,0,0}, {255,42,43}, {78,0,0}, {255,44,45}, {77,0,0}, {87,0,0}, {255,47,48}, {32,0,0},
  {255,49,50}, {84,0,0}, {255,51,52}, {10,0,0}, {82,0,0},
};

int uttDialogue[1237]= {
0xe7, 0x34, 0xd3, 0x1f, 0x13, 0x1a, 0x7b, 0x30, 0x87, 0x62, 0xe8, 0x5c, 0x70, 0x4c, 0x82, 0x1a, 0x19, 0xec, 0xc9, 0xd,
0xcb, 0xed, 0x53, 0x60, 0xb9, 0xa, 0x1f, 0x26, 0xfc, 0x9a, 0x3e, 0x99, 0x22, 0x71, 0xec, 0xe6, 0x5b, 0xe8, 0xae, 0xa5,
0x46, 0x3b, 0x7b, 0x73, 0x8d, 0xcb, 0x78, 0xfd, 0x2f, 0x73, 0x68, 0xdc, 0xe3, 0x54, 0xab, 0xff, 0x64, 0x9a, 0xa, 0x8b,
0x16, 0x32, 0xdb, 0xda, 0xe9, 0x33, 0x4e, 0xd8, 0xe4, 0xd5, 0xc9, 0xed, 0xce, 0x37, 0xcc, 0xa8, 0xfd, 0x39, 0xc1, 0x34,
0x7c, 0x6a, 0xd6, 0x5b, 0xed, 0xed, 0x9, 0xae, 0x93, 0x1b, 0x1f, 0xac, 0x94, 0xe7, 0x34, 0xc6, 0x9a, 0x2b, 0xac, 0xbb,
0xd9, 0x6c, 0x32, 0xcf, 0xb9, 0xc2, 0xf, 0x6e, 0x78, 0xec, 0xd3, 0x2a, 0x12, 0x92, 0xb3, 0x7, 0xf7, 0x38, 0xdc, 0xb7,
0x8f, 0xd0, 0xf3, 0xc1, 0x51, 0x96, 0xba, 0xf, 0x95, 0xc, 0xf6, 0x9f, 0xeb, 0x76, 0x89, 0xe3, 0xfe, 0xc4, 0x59, 0x7f,
0x25, 0x63, 0x6b, 0x68, 0x79, 0xe0, 0xa8, 0xd1, 0xba, 0x12, 0xb7, 0xb7, 0x3c, 0x76, 0xbc, 0xda, 0xdd, 0x54, 0x13, 0xda,
0x33, 0xca, 0xcc, 0xf8, 0x64, 0xea, 0xd6, 0x52, 0xbc, 0xbb, 0x4e, 0x7f, 0x4e, 0xc4, 0x59, 0x7b, 0x5d, 0x24, 0xed, 0x57,
0xd4, 0xa8, 0xc7, 0xed, 0x33, 0xaa, 0xd, 0x79, 0xa0, 0x69, 0x28, 0x80, 0x56, 0xdc, 0xe3, 0x4d, 0x1d, 0x54, 0x5f, 0xed,
0x1a, 0xa6, 0xc4, 0xc3, 0x2c, 0xfe, 0xcc, 0x13, 0x43, 0x5, 0xa6, 0x14, 0x8c, 0x59, 0xfd, 0xb8, 0xd3, 0x38, 0x6b, 0x3d,
0xab, 0xac, 0xac, 0xcd, 0x79, 0xf3, 0x9d, 0xa9, 0x3f, 0xc0, 0xce, 0x3f, 0xa2, 0xcc, 0xf6, 0xaf, 0xf3, 0xb1, 0x79, 0xc7,
0xf7, 0x2f, 0xb3, 0x8f, 0xe9, 0xe3, 0x9c, 0x7e, 0x9c, 0xe3, 0x4d, 0x1d, 0x54, 0x5f, 0xec, 0xd3, 0x9, 0xac, 0xbf, 0xe7,
0xa1, 0x16, 0x59, 0x91, 0xf1, 0xb9, 0x7d, 0x8e, 0xdc, 0x3f, 0x38, 0xf8, 0xc7, 0x6f, 0x6b, 0x89, 0xab, 0xc9, 0xe, 0xc1,
0x10, 0xa6, 0x9c, 0xf1, 0x6a, 0xd8, 0x8b, 0x2a, 0x1e, 0xbe, 0x13, 0x44, 0xff, 0x83, 0x67, 0xb4, 0xeb, 0xfd, 0x71, 0x33,
0x4e, 0x71, 0x4b, 0x89, 0xa7, 0x8e, 0x71, 0xfc, 0xd3, 0x2a, 0x13, 0xda, 0x3b, 0x73, 0x8c, 0xc5, 0x9a, 0xb2, 0x7b, 0x72,
0xee, 0x71, 0x91, 0x65, 0xcf, 0x43, 0xba, 0xaf, 0x84, 0x43, 0x2a, 0x35, 0x79, 0x77, 0xb5, 0x96, 0xfc, 0x45, 0x96, 0xba,
0x49, 0xde, 0xd5, 0x42, 0x26, 0xb8, 0x9b, 0x82, 0x75, 0x2f, 0xd, 0x6e, 0x62, 0xd8, 0x7b, 0x1e, 0x47, 0x86, 0xb7, 0x14,
0x29, 0xdb, 0xd8, 0x8b, 0x2c, 0x76, 0xd5, 0xe3, 0xb9, 0xa2, 0x8d, 0x34, 0xe1, 0x6f, 0x6e, 0x79, 0x16, 0x99, 0xd5, 0xac,
0xf6, 0x9e, 0x39, 0xc7, 0xf4, 0x57, 0x52, 0xa2, 0x97, 0xba, 0xe2, 0xd4, 0x26, 0xe7, 0x1f, 0x5, 0x34, 0x3d, 0x17, 0x63,
0xb3, 0x5, 0x7e, 0xc, 0xf6, 0x20, 0xec, 0x7a, 0x12, 0xb7, 0xb5, 0xcb, 0xf1, 0x96, 0x7c, 0x45, 0x95, 0x2e, 0x26, 0xd,
0x60, 0x4e, 0xf6, 0x64, 0x48, 0xb7, 0xe1, 0xfc, 0x69, 0xed, 0x58, 0xd9, 0x68, 0xaa, 0xa0, 0xcd, 0xca, 0xb4, 0x47, 0x1a,
0xb5, 0x96, 0xf4, 0xcb, 0x3f, 0xb2, 0x9, 0xbe, 0x39, 0x37, 0x38, 0x27, 0xb5, 0xf1, 0x6e, 0xce, 0x6c, 0x60, 0xb5, 0x2a,
0x68, 0x84, 0x18, 0xb5, 0xdc, 0xbe, 0xdc, 0xe3, 0x57, 0x32, 0xf2, 0x39, 0xd5, 0xec, 0x76, 0xdc, 0xe3, 0x3c, 0x7e, 0xba,
0xfe, 0x9c, 0xe6, 0x99, 0x1c, 0xea, 0xc6, 0x9e, 0xd7, 0xb8, 0xf8, 0xce, 0x69, 0xec, 0xc8, 0x91, 0x6f, 0xc3, 0xf8, 0xd2,
0x88, 0xe3, 0x5f, 0x9, 0xa3, 0x5a, 0x5f, 0x41, 0xed, 0x1d, 0xb9, 0xc6, 0x98, 0xf8, 0x9e, 0xce, 0xa9, 0x51, 0xf4, 0x37,
0x5e, 0xe2, 0xd4, 0x47, 0x1a, 0x75, 0x95, 0x99, 0xed, 0x19, 0x4, 0xdf, 0x1d, 0xb9, 0x7d, 0xb9, 0xc6, 0xa9, 0xb0, 0x5c,
0x85, 0x39, 0xe2, 0xd5, 0xb1, 0x16, 0x54, 0x3d, 0x74, 0x93, 0xb7, 0xc7, 0x4b, 0xf6, 0xdc, 0xbe, 0xdc, 0xe3, 0x3a, 0xa5,
0x47, 0xd0, 0xda, 0x73, 0x8c, 0xe2, 0x19, 0x98, 0xb3, 0x72, 0xff, 0xd9, 0xe1, 0x31, 0x6b, 0x61, 0xae, 0x86, 0x51, 0xc6,
0xba, 0x18, 0x37, 0x38, 0xd5, 0x36, 0xb, 0x90, 0xf6, 0x8b, 0x33, 0x1e, 0xba, 0x49, 0xde, 0xd3, 0x9d, 0x99, 0x96, 0x5d,
0xaf, 0x72, 0x52, 0xf1, 0xaf, 0x36, 0xb7, 0x38, 0x6d, 0x5e, 0xc4, 0x59, 0x6f, 0x8e, 0x97, 0xed, 0xb9, 0xc7, 0xc5, 0xf,
0x31, 0x75, 0x97, 0xb7, 0x7e, 0x49, 0xde, 0xc4, 0x59, 0x6a, 0xf2, 0xec, 0x79, 0xe0, 0xa8, 0xf6, 0xb2, 0xe9, 0x9d, 0x4e,
0x61, 0x4e, 0x71, 0x9d, 0x52, 0xa3, 0xe8, 0x6f, 0xb1, 0xa6, 0xb2, 0xff, 0x9e, 0x9c, 0xe6, 0x9a, 0xa6, 0xc4, 0xc3, 0x1a,
0x7b, 0x4c, 0xf0, 0xa9, 0x4b, 0xde, 0x3b, 0x3c, 0x26, 0x71, 0xec, 0xc5, 0xb0, 0x29, 0xa2, 0x2e, 0x22, 0xcb, 0x4, 0x9a,
0x2a, 0xc, 0xa5, 0xee, 0x4d, 0x1f, 0x18, 0x8b, 0x2f, 0x66, 0x29, 0xd8, 0xbd, 0xce, 0x68, 0x53, 0x45, 0x97, 0xf1, 0x7f,
0x11, 0xd9, 0xa2, 0x18, 0x3f, 0xed, 0x74, 0x30, 0x68, 0xb6, 0x59, 0xe7, 0x1f, 0xa1, 0xe9, 0x96, 0x4d, 0x66, 0xb2, 0xf6,
0xf8, 0x43, 0x4e, 0xa7, 0x38, 0xd5, 0x36, 0xb, 0x90, 0xf6, 0x34, 0xc9, 0x57, 0xc4, 0x30, 0xec, 0x5d, 0x23, 0x5c, 0x68,
0xf1, 0x55, 0xf, 0xb5, 0xf0, 0x99, 0x6c, 0xd2, 0x7b, 0x4e, 0xbf, 0xdc, 0xe3, 0x54, 0xab, 0xfa, 0x11, 0x65, 0xae, 0x5b,
0x27, 0xf, 0xd2, 0xf1, 0xae, 0x92, 0x76, 0x2d, 0x77, 0x2e, 0xe7, 0x1e, 0xdc, 0xb7, 0x8f, 0xee, 0x5d, 0x32, 0xda, 0x7f,
0xad, 0xbb, 0xdb, 0x9c, 0x66, 0x88, 0x60, 0xfd, 0x39, 0xcd, 0x37, 0x2d, 0xe3, 0xf8, 0xd3, 0xd9, 0x91, 0x62, 0xc7, 0xd6,
0x49, 0x4b, 0xc6, 0xba, 0x49, 0xda, 0xb8, 0xc9, 0x2f, 0x8f, 0x6b, 0xc6, 0x8f, 0x8d, 0xf1, 0xc, 0x25, 0x11, 0x77, 0x38,
0x41, 0xa3, 0xe3, 0xdb, 0x9c, 0x68, 0xb7, 0x26, 0x3f, 0x6b, 0xcd, 0xad, 0xd3, 0x7e, 0x48, 0xf4, 0x22, 0xcb, 0x5e, 0x6d,
0x6e, 0xaa, 0x9, 0xed, 0x3a, 0xff, 0x11, 0x65, 0xfd, 0x37, 0xce, 0xe0, 0x94, 0xbd, 0xe3, 0xb4, 0xdf, 0x3b, 0x82, 0x14,
0xfb, 0x1e, 0x8b, 0xb4, 0xcd, 0x11, 0x63, 0xb1, 0x7e, 0xdc, 0xe3, 0x4c, 0x7c, 0x4a, 0x11, 0x65, 0xa3, 0xe3, 0xd9, 0x81,
0x3b, 0xea, 0x43, 0xb1, 0x78, 0xfa, 0x87, 0xd8, 0xd3, 0x31, 0x6c, 0x28, 0x45, 0x96, 0x8f, 0x8c, 0xd1, 0xc, 0x1f, 0xf6,
0xb2, 0xdf, 0xae, 0x93, 0x1b, 0x1f, 0xad, 0x46, 0x88, 0x66, 0xe7, 0x1a, 0xa0, 0xb5, 0x34, 0x7b, 0x45, 0x99, 0x8f, 0x5e,
0x6d, 0x6f, 0xb4, 0x30, 0xa9, 0xa0, 0xc4, 0x59, 0x52, 0xcb, 0xec, 0xd1, 0xdb, 0x9c, 0x6a, 0x82, 0xd4, 0xd1, 0xec, 0xc1,
0x31, 0xb0, 0x6e, 0x70, 0x7f, 0xd3, 0xc7, 0xa, 0x45, 0x99, 0x91, 0x75, 0xe7, 0xcf, 0xb9, 0xc7, 0xb4, 0xc7, 0xc4, 0xd1,
0xdb, 0x9c, 0x1f, 0xfd, 0xa7, 0x3, 0xa8, 0x7c, 0x9a, 0x79, 0x74, 0xbe, 0x33, 0x60, 0x9e, 0xc7, 0x6d, 0xce, 0x33, 0xc5,
0x98, 0x99, 0x67, 0xf6, 0x8c, 0x87, 0x62, 0xda, 0x35, 0xd2, 0x63, 0x63, 0xf5, 0xa9, 0x7b, 0xc7, 0x66, 0x1b, 0x3d, 0xb9,
0xc1, 0x34, 0xce, 0xaa, 0x80, 0xa6, 0x9c, 0xf1, 0xd8, 0xd3, 0xda, 0x2c, 0xbc, 0xe3, 0xf9, 0x39, 0x7e, 0x4a, 0x5e, 0x1b,
0x32, 0xdc, 0x37, 0x2e, 0xe7, 0x1e, 0xd3, 0x99, 0x6f, 0xc7, 0x62, 0xf5, 0x78, 0xfd, 0xf, 0x45, 0xdb, 0x9c, 0x6e, 0x5a,
0xb4, 0xe6, 0x5b, 0xf0, 0xfd, 0x27, 0x32, 0xdf, 0xb5, 0xc2, 0x97, 0x13, 0x4d, 0xf1, 0x7e, 0x78, 0x4f, 0x6b, 0xa5, 0xc7,
0x63, 0xc, 0xd1, 0x66, 0x7b, 0x74, 0x56, 0x16, 0xd7, 0x13, 0x23, 0x9d, 0x54, 0x3d, 0x17, 0x69, 0xa2, 0xae, 0x83, 0xb6,
0xb2, 0xdf, 0x4e, 0x78, 0xb5, 0x6c, 0x45, 0x96, 0x9d, 0x7f, 0xec, 0x9c, 0xb5, 0x54, 0x76, 0x2f, 0x73, 0x8b, 0xa9, 0xce,
0x69, 0xaf, 0x8f, 0x87, 0x59, 0x20, 0xc6, 0x9e, 0xd3, 0xc9, 0xad, 0xcb, 0x3e, 0xfc, 0x72, 0x7b, 0x38, 0x6d, 0x54, 0xe7,
0x1a, 0xb9, 0x96, 0x58, 0x4e, 0xdf, 0x8e, 0xf6, 0x5, 0x47, 0xf3, 0x44, 0x54, 0x99, 0x65, 0xd4, 0xbd, 0xc5, 0xfe, 0xdc,
0xea, 0x83, 0x7e, 0x39, 0x3d, 0x92, 0x68, 0xf8, 0x66, 0xb8, 0x53, 0xe7, 0x33, 0xe4, 0xb2, 0xd7, 0x4f, 0x66, 0x99, 0x51,
0x9c, 0x7c, 0x50, 0xf4, 0x5d, 0x8a, 0x8f, 0x93, 0x7c, 0xe6, 0x7a, 0x1e, 0xe7, 0x3b, 0x56, 0xd7, 0x41, 0x2b, 0x8f, 0x6e,
0x78, 0xed, 0x7c, 0x26, 0x8f, 0x6a, 0xe7, 0xda, 0xe9, 0x4c, 0xf4, 0x45, 0xb8, 0x2d, 0x86, 0x4e, 0x56, 0x8f, 0x6b, 0x89,
0xaf, 0x8f, 0x84, 0xa7, 0x38, 0x26, 0x8f, 0x8c, 0x76, 0xf6, 0xe7, 0x8e, 0xd7, 0xc7, 0xc3, 0xac, 0x90, 0x53, 0x9e, 0x2d,
0x59, 0x34, 0xeb, 0xff, 0x62, 0xc7, 0x71, 0xd8, 0xbd, 0xce, 0x2e, 0xf6, 0xaf, 0x13, 0x56, 0xa0, 0x0,
};

// -----------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------

// Real AVR int8_t narrowing, replicated exactly for a byte read back from
// EEPROM into a value real upstream declares int8_t (see header comment -
// only uttGameStatus[] ever needs this, since it's the only persisted
// value that's genuinely negative on real hardware).
int uttNarrowS8( int v )
{
    if( v > 127 ) return v - 256;
    return v;
}

// Stands in for real upstream passing one of player_names/enemy_names/
// menu_text/combat_text/item_names directly as a function argument (see
// header comment - no proven-safe way to pass a 2D array as a parameter
// in this dialect).
int uttNameChar( int table, int row, int col )
{
    if( table == UTT_TBL_PLAYER ) return uttPlayerNames[row][col];
    if( table == UTT_TBL_ENEMY ) return uttEnemyNames[row][col];
    if( table == UTT_TBL_MENU ) return uttMenuText[row][col];
    if( table == UTT_TBL_COMBAT ) return uttCombatText[row][col];
    return uttItemNames[row][col];
}

// -----------------------------------------------------------------------
// Overworld (real overworld.ino)
// -----------------------------------------------------------------------

// Real upstream world_get() - decodes the run-length-encoded uttWorldMap[]
// stream, with the same "resume search from where we left off" cache
// upstream itself keeps (uttWorldNdx/uttWorldCnt persist between calls).
// Bounded to the real, measured UTT_WORLD_SIZE (not the stale, too-large
// real COMPRESSED_SIZE) with a safe fallback return - see header comment.
int uttWorldGet( int index )
{
    int curr, count;
    if( index < 0 ) return UTT_OW_WATER;
    if( uttWorldCnt > index )
    {
        uttWorldNdx = 0;
        uttWorldCnt = 0;
    }
    while( uttWorldNdx < UTT_WORLD_SIZE )
    {
        curr = uttWorldMap[ uttWorldNdx + 1 ];
        count = uttWorldMap[ uttWorldNdx ];
        uttWorldCnt = uttWorldCnt + count;
        if( uttWorldCnt > index )
        {
            uttWorldCnt = uttWorldCnt - count;
            return curr;
        }
        uttWorldNdx = uttWorldNdx + 2;
    }
    return UTT_OW_WATER;
}

void uttDrawWorld()
{
    int tile, x, y;
    for( y = uttDudeY/8 - 4; y <= uttDudeY/8 + 4; y++ )
    {
        for( x = uttDudeX/8 - 6; x <= uttDudeX/8 + 6; x++ )
        {
            tile = uttWorldGet( y*64+x );
            if( tile == UTT_OW_DRAIN || tile == UTT_OW_FLOW || tile == UTT_OW_WATER || tile == UTT_OW_DOCK1 || tile == UTT_OW_DOCK2 || tile == UTT_OW_DOCK3 )
            {
                if( uttWorldFrame >= 0 && uttWorldFrame <= 3 )
                {
                    gbDrawBitmap( (x*8 - uttDudeX) + UTT_SCREEN_WIDTH/2 - 4, (y*8 - uttDudeY) + UTT_SCREEN_HEIGHT/2 - 4, &uttTiles[tile*10] );
                }
                else
                {
                    gbDrawBitmap( (x*8 - uttDudeX) + UTT_SCREEN_WIDTH/2 - 4, (y*8 - uttDudeY) + UTT_SCREEN_HEIGHT/2 - 4, &uttTiles[(tile+1)*10] );
                }
            }
            else
            {
                gbDrawBitmap( (x*8 - uttDudeX) + UTT_SCREEN_WIDTH/2 - 4, (y*8 - uttDudeY) + UTT_SCREEN_HEIGHT/2 - 4, &uttTiles[tile*10] );
            }
        }
    }
    if( uttPlayerMoving && (uttDudeFrame/2 == 0 || uttDudeFrame/2 == 2) )
    {
        gbDrawBitmap( UTT_SCREEN_WIDTH/2-4, UTT_SCREEN_HEIGHT/2-4, &uttPlayerSprites[((uttDudeFrame/2)+uttDudeAnimation*3)*10] );
    }
    else
    {
        gbDrawBitmap( UTT_SCREEN_WIDTH/2-4, UTT_SCREEN_HEIGHT/2-4, &uttPlayerSprites[(1+uttDudeAnimation*3)*10] );
    }
}

// Real upstream quirk, preserved exactly: only the UP branch returns the
// real raw tile value (needed to detect a door specifically); DOWN/LEFT/
// RIGHT return a plain boolean - see header comment.
int uttTestWorldCollision( int dir )
{
    if( dir == UTT_UP )
    {
        int tile = uttWorldGet( ((uttDudeY-1)/8)*64 + uttDudeX/8 );
        if( tile != UTT_OW_DOOR && uttWorldGet( ((uttDudeY-1)/8)*64 + (uttDudeX+7)/8 ) )
            return uttWorldGet( ((uttDudeY-1)/8)*64 + (uttDudeX+7)/8 );
        return tile;
    }
    else if( dir == UTT_DOWN )
    {
        if( uttWorldGet( ((uttDudeY+8)/8)*64 + uttDudeX/8 ) != 0 || uttWorldGet( ((uttDudeY+8)/8)*64 + (uttDudeX+7)/8 ) != 0 )
            return 1;
        return 0;
    }
    else if( dir == UTT_LEFT )
    {
        if( uttWorldGet( (uttDudeY/8)*64 + (uttDudeX-1)/8 ) != 0 || uttWorldGet( ((uttDudeY+7)/8)*64 + (uttDudeX-1)/8 ) != 0 )
            return 1;
        return 0;
    }
    else
    {
        if( uttWorldGet( (uttDudeY/8)*64 + (uttDudeX+8)/8 ) != 0 || uttWorldGet( ((uttDudeY+7)/8)*64 + (uttDudeX+8)/8 ) != 0 )
            return 1;
        return 0;
    }
}

void uttTryCombat()
{
    uttNextCombat--;
    if( uttNextCombat == 0 )
    {
        uttMetaMode = uttMode;
        uttMode = UTT_TO_COMBAT;
        uttTransition = -UTT_SCREEN_HEIGHT/2;
        uttNextCombat = arand(192)+64;
    }
}

void uttStepWorld()
{
    int collision;
    uttPlayerMoving = 0;
    if( gbRepeat(BTN_UP,1) )
    {
        collision = uttTestWorldCollision(UTT_UP);
        if( collision == UTT_OW_DOOR )
        {
            int dungeonx, dungeony, i;
            collision = uttWorldGet( ((uttDudeY-1)/8)*64+uttDudeX/8 );
            if( collision == UTT_OW_DOOR )
            {
                dungeonx = uttDudeX/8;
                dungeony = (uttDudeY-1)/8;
            }
            else
            {
                dungeonx = (uttDudeX+7)/8;
                dungeony = (uttDudeY-1)/8;
            }
            for( i = 0; i < UTT_NUM_DUNGEONS; i++ )
            {
                if( uttDungeonX[i] == dungeonx && uttDungeonY[i] == dungeony )
                {
                    uttDungeonId = i;
                    break;
                }
            }
            uttMode = UTT_TO_DUNGEON;
            uttTransition = -UTT_SCREEN_HEIGHT/2;
            uttDungeonGenerated = 0;
            uttDungeonLevel = 0;
            uttPreviousLevel = -1;
            return;
        }
        if( !collision )
        {
            uttDudeY--;
            uttDudeAnimation = UTT_UP;
            uttPlayerMoving = 1;
        }
    }
    else if( gbRepeat(BTN_DOWN,1) && !uttTestWorldCollision(UTT_DOWN) )
    {
        uttDudeY++;
        uttDudeAnimation = UTT_DOWN;
        uttPlayerMoving = 1;
    }
    if( gbRepeat(BTN_LEFT,1) && !uttTestWorldCollision(UTT_LEFT) )
    {
        uttDudeX--;
        uttDudeAnimation = UTT_LEFT;
        uttPlayerMoving = 1;
    }
    else if( gbRepeat(BTN_RIGHT,1) && !uttTestWorldCollision(UTT_RIGHT) )
    {
        uttDudeX++;
        uttDudeAnimation = UTT_RIGHT;
        uttPlayerMoving = 1;
    }

    if( uttPlayerMoving ) uttTryCombat();
}

// -----------------------------------------------------------------------
// Battle helpers (real battle.ino)
// -----------------------------------------------------------------------

int uttCalculateDamage( int lvl )
{
    return lvl*10/8;
}

// Real upstream quirk, preserved exactly: the boss branch always resets
// uttMetaMode back to UTT_DUNGEON after loading the boss, so a subsequent
// call (a randomly-spawned companion enemy) sees a normal dungeon spawn
// pool instead of a second boss copy - see header comment.
void uttLoadEnemyData( int index, int slot )
{
    int enInd;
    if( uttMetaMode == UTT_WORLD )
    {
        enInd = uttWorldSpawns[uttDudeY/8/16][uttDudeX/8/16][index];
    }
    else if( uttMetaMode == UTT_DUNGEON )
    {
        enInd = uttDungeonSpawns[uttDungeonId][index];
    }
    else
    {
        uttIsBoss = 1;
        enInd = uttMetaMode;
        uttMetaMode = UTT_DUNGEON;
    }
    uttEnemyBufLvl[slot] = uttEnemies[enInd][0];
    uttEnemyBufSpd[slot] = uttEnemies[enInd][1];
    uttEnemyBufImg[slot] = uttEnemies[enInd][2];
    uttEnemyBufNme[slot] = enInd;
}

void uttGenEnemies()
{
    int enemyIndex;
    uttCombatXp = 0;
    uttIsBoss = 0;
    enemyIndex = arand(3);
    uttLoadEnemyData( enemyIndex, 1 );
    uttCombatXp += uttEnemyBufLvl[1];
    uttEnemyHealth[1] = uttEnemyBufLvl[1]*10;

    if( arand(2) == 0 )
    {
        enemyIndex = arand(3);
        uttLoadEnemyData( enemyIndex, 0 );
        uttCombatXp += uttEnemyBufLvl[0];
        uttEnemyHealth[0] = uttEnemyBufLvl[0]*10;
    }
    else
    {
        uttEnemyBufLvl[0] = -1;
    }

    if( arand(2) == 0 )
    {
        enemyIndex = arand(3);
        uttLoadEnemyData( enemyIndex, 2 );
        uttCombatXp += uttEnemyBufLvl[2];
        uttEnemyHealth[2] = uttEnemyBufLvl[2]*10;
    }
    else
    {
        uttEnemyBufLvl[2] = -1;
    }
}

void uttCopyToBuffer( int index, int table )
{
    int i;
    for( i = 0; i < 7; i++ )
    {
        uttCombatBuffer[i] = uttNameChar( table, index, i );
    }
}

// Platform-forced fallback return added at the end (real upstream's own
// function has no return at all if a table row is ever exactly 8 chars
// with no null - never actually happens for any real table here, but
// avoids ever returning an undefined value - see header comment).
int uttAppendToMsgBuffer( int index, int table, int offset )
{
    int i, tmp;
    for( i = 0; i < 8; i++ )
    {
        tmp = uttNameChar( table, index, i );
        if( tmp != 0 )
        {
            uttCombatMessage[offset+i] = tmp;
        }
        else
        {
            return offset+i;
        }
    }
    return offset+8;
}

void uttCopyActionToMsgBuffer( int source, int dest, int amount, int type )
{
    int offset = 0;
    if( type == UTT_PL2EN || type == UTT_PHEAL || type == UTT_PSPEED || type == UTT_PITEM || type == UTT_PDAMAGE || type == UTT_PDEFENSE )
        offset = uttAppendToMsgBuffer( source, UTT_TBL_PLAYER, offset );
    else if( type == UTT_EN2PL || type == UTT_PROTECT )
    {
        if( type == UTT_PROTECT )
        {
            offset = uttAppendToMsgBuffer( 12, UTT_TBL_COMBAT, offset );
            uttCombatMessage[offset++] = ' ';
            offset = uttAppendToMsgBuffer( dest, UTT_TBL_PLAYER, offset );
            uttCombatMessage[offset++] = '\n';
            uttCombatMessage[offset++] = ' ';
            dest = UTT_NURSE;
        }
        offset = uttAppendToMsgBuffer( source, UTT_TBL_ENEMY, offset );
    }
    else if( type == UTT_EFALL )
    {
        offset = uttAppendToMsgBuffer( source, UTT_TBL_ENEMY, offset );
        offset = uttAppendToMsgBuffer( 4, UTT_TBL_COMBAT, offset );
        uttCombatMessage[offset] = 0;
        return;
    }
    else if( type == UTT_PFALL )
    {
        offset = uttAppendToMsgBuffer( source, UTT_TBL_PLAYER, offset );
        offset = uttAppendToMsgBuffer( 4, UTT_TBL_COMBAT, offset );
        uttCombatMessage[offset] = 0;
        return;
    }
    else if( type == UTT_PWIN )
    {
        offset = uttAppendToMsgBuffer( 5, UTT_TBL_COMBAT, offset );
        uttCombatMessage[offset++] = '\n';
        uttCombatMessage[offset++] = ' ';
        uttCombatMessage[offset++] = amount/100+'0';
        uttCombatMessage[offset++] = amount%100/10+'0';
        uttCombatMessage[offset++] = amount%100%10+'0';
        offset = uttAppendToMsgBuffer( 6, UTT_TBL_COMBAT, offset );
        if( uttPartyLevel[UTT_SHADOW] != 0 || uttPartyLevel[UTT_NURSE] != 0 )
        {
            offset = uttAppendToMsgBuffer( 10, UTT_TBL_COMBAT, offset );
            offset = uttAppendToMsgBuffer( 11, UTT_TBL_COMBAT, offset );
        }
        uttCombatMessage[offset] = 0;
        return;
    }
    else if( type == UTT_HEALALL )
    {
        offset = uttAppendToMsgBuffer( 15, UTT_TBL_COMBAT, offset );
    }

    if( type == UTT_PHEAL || type == UTT_HEALALL )
        offset = uttAppendToMsgBuffer( 3, UTT_TBL_COMBAT, offset );
    else if( type == UTT_PSPEED )
        offset = uttAppendToMsgBuffer( 8, UTT_TBL_COMBAT, offset );
    else if( type == UTT_PDAMAGE )
        offset = uttAppendToMsgBuffer( 13, UTT_TBL_COMBAT, offset );
    else if( type == UTT_PDEFENSE )
        offset = uttAppendToMsgBuffer( 14, UTT_TBL_COMBAT, offset );
    else if( type == UTT_PITEM )
    {
        offset = uttAppendToMsgBuffer( 9, UTT_TBL_COMBAT, offset );
        uttCombatMessage[offset++] = ' ';
        offset = uttAppendToMsgBuffer( amount, UTT_TBL_ITEM, offset );
        if( uttInventory[amount] > UTT_INVENTORY_MAX )
        {
            uttCombatMessage[offset++] = '\n';
            uttCombatMessage[offset++] = ' ';
            offset = uttAppendToMsgBuffer( 16, UTT_TBL_COMBAT, offset );
            uttCombatMessage[offset++] = '\n';
            uttCombatMessage[offset++] = ' ';
            offset = uttAppendToMsgBuffer( 0, UTT_TBL_PLAYER, offset );
            offset = uttAppendToMsgBuffer( 17, UTT_TBL_COMBAT, offset );
            offset = uttAppendToMsgBuffer( amount, UTT_TBL_ITEM, offset );
        }
    }
    else
    {
        offset = uttAppendToMsgBuffer( 0, UTT_TBL_COMBAT, offset );
    }

    if( type == UTT_PL2EN )
        offset = uttAppendToMsgBuffer( dest, UTT_TBL_ENEMY, offset );
    else if( type == UTT_EN2PL || type == UTT_PROTECT )
        offset = uttAppendToMsgBuffer( dest, UTT_TBL_PLAYER, offset );

    if( type != UTT_PSPEED && type != UTT_PDAMAGE && type != UTT_PDEFENSE && type != UTT_PITEM )
    {
        offset = uttAppendToMsgBuffer( 1, UTT_TBL_COMBAT, offset );
        uttCombatMessage[offset++] = amount/100+'0';
        uttCombatMessage[offset++] = amount%100/10+'0';
        uttCombatMessage[offset++] = amount%100%10+'0';
        offset = uttAppendToMsgBuffer( 2, UTT_TBL_COMBAT, offset );
        if( type == UTT_EN2PL && dest == UTT_SHADOW && uttShadowStealthBonus > 0 )
        {
            uttShadowStealthBonus = 0;
            uttCombatMessage[offset++] = '\n';
            uttCombatMessage[offset++] = ' ';
            offset = uttAppendToMsgBuffer( UTT_SHADOW, UTT_TBL_PLAYER, offset );
            offset = uttAppendToMsgBuffer( 9, UTT_TBL_COMBAT, offset );
        }
    }
    uttCombatMessage[offset] = 0;
}

void uttDrawMenu( int index )
{
    int i, offset, j, yeses, item, limit;
    gbCursorY = UTT_SCREEN_HEIGHT/2-6;
    for( i = 0; i < 4; i++ )
    {
        gbCursorX = 1;
        gbCursorY += 6;
        if( uttCombatSelection == i ) gbPrintString( uttArrowIcon );
        gbCursorX = 4;
        offset = 0;
        uttCombatMessage[0] = 0;
        if( index <= UTT_SECONDARY_MENU )
        {
            offset = uttAppendToMsgBuffer( index*4+i, UTT_TBL_MENU, 0 );
        }
        else if( index == UTT_ENEMY_MENU )
        {
            yeses = 0;
            for( j = 0; j < 3; j++ )
            {
                if( uttEnemyBufLvl[j] != -1 )
                {
                    yeses++;
                    if( yeses > i )
                    {
                        offset = uttAppendToMsgBuffer( uttEnemyBufNme[j], UTT_TBL_ENEMY, 0 );
                        break;
                    }
                }
            }
        }
        else if( index == UTT_FOOD_MENU || index == UTT_DRINK_MENU )
        {
            item = 0;
            if( index == UTT_FOOD_MENU ) j = 0;
            else j = 3;
            if( index == UTT_FOOD_MENU ) limit = UTT_INVENTORY_SIZE/2;
            else limit = UTT_INVENTORY_SIZE;
            for( ; j < limit; j++ )
            {
                if( uttInventory[j] > 0 ) item++;
                if( item == i+1 )
                {
                    offset = uttAppendToMsgBuffer( j, UTT_TBL_ITEM, 0 );
                    uttCombatMessage[offset++] = 'x';
                    uttCombatMessage[offset++] = uttInventory[j]/10+'0';
                    uttCombatMessage[offset++] = uttInventory[j]%10+'0';
                    break;
                }
            }
        }
        else
        {
            if( i < 3 && uttPartyLevel[i] != 0 )
            {
                offset = uttAppendToMsgBuffer( i, UTT_TBL_PLAYER, 0 );
            }
        }
        uttCombatMessage[offset] = 0;
        gbPrintString( uttCombatMessage );
    }
}

void uttGiveXp()
{
    uttPartyXp[uttCombatSelection] += uttCombatXp;
    if( uttPartyXp[uttCombatSelection] >= uttPartyLevel[uttCombatSelection]*2 )
    {
        uttPartyXp[uttCombatSelection] -= uttPartyLevel[uttCombatSelection]*2;
        uttPartyHealth[uttCombatSelection] += ((uttPartyLevel[uttCombatSelection]+1)*20) - (uttPartyLevel[uttCombatSelection]*20);
        uttPartyLevel[uttCombatSelection]++;
    }
    uttCombatMode = UTT_PRECOMBAT;
    uttMode = uttMetaMode;
}

void uttDoCombatStep()
{
    int i, maxi;
    for( i = 0; i < 3; i++ )
    {
        if( uttPartyLevel[i] != 0 ) uttCombatStatus[i] += uttPartySpeed[i] + uttPartyBonusSpeed[i];
        if( uttEnemyBufLvl[i] != -1 ) uttCombatStatus[i+UTT_ENEMY1] += uttEnemyBufSpd[i];
    }
    maxi = 0;
    for( i = 0; i < 6; i++ )
    {
        if( uttCombatStatus[i] > uttCombatStatus[maxi] ) maxi = i;
    }
    if( maxi == UTT_NURSE ) uttNurseProtectBonus = -1;
    uttCombatMode = maxi;
    uttCombatSelection = 0;
    uttMenuSelection = maxi;
    uttCombatStatus[maxi] = 0;
}

// -----------------------------------------------------------------------
// Dungeon generation + movement (real dungeon.ino). uttDungeonMap[256] is
// a flattened 16x16 grid (UTT_MAPIDX(row,col) indexing) - see header
// comment for why no map parameter is passed around.
// -----------------------------------------------------------------------

void uttMapInit()
{
    int i, j;
    for( i = 0; i < UTT_MAPSIZE; i++ )
    {
        for( j = 0; j < UTT_MAPSIZE; j++ )
        {
            uttDungeonMap[ UTT_MAPIDX(j,i) ] = 0;
            uttDungeonMap[ UTT_MAPIDX(0,j) ] = UTT_DUN_WALL;
            uttDungeonMap[ UTT_MAPIDX(UTT_MAPSIZE-1,j) ] = UTT_DUN_WALL;
            uttDungeonMap[ UTT_MAPIDX(j,0) ] = UTT_DUN_WALL;
            uttDungeonMap[ UTT_MAPIDX(j,UTT_MAPSIZE-1) ] = UTT_DUN_WALL;
        }
    }
}

void uttMapGen( int startx, int starty, int endx, int endy )
{
    int i, orientation, position, door, door2, doorcount, hall;
    int width = endx - startx;
    int height = endy - starty;

    if( width < UTT_MIN_WIDTH && height < UTT_MIN_HEIGHT ) return;

    if( width >= UTT_MIN_HALL_WIDTH && height >= UTT_MIN_HALL_HEIGHT && width < UTT_MAX_HALL_WIDTH && height < UTT_MAX_HALL_HEIGHT )
    {
        hall = arand(100);
        if( UTT_HALL_CHANCE > hall ) return;
    }

    if( width >= height )
    {
        doorcount = 0;
        for( i = startx; i < endx; i++ )
        {
            if( uttDungeonMap[ UTT_MAPIDX(starty,i) ] == 0 ) doorcount++;
            if( uttDungeonMap[ UTT_MAPIDX(endy,i) ] == 0 ) doorcount++;
        }
        if( width < UTT_MIN_WIDTH + doorcount ) return;
        orientation = UTT_VERTICAL;
    }
    else
    {
        doorcount = 0;
        for( i = starty; i < endy; i++ )
        {
            if( uttDungeonMap[ UTT_MAPIDX(i,startx) ] == 0 ) doorcount++;
            if( uttDungeonMap[ UTT_MAPIDX(i,endx) ] == 0 ) doorcount++;
        }
        if( height < UTT_MIN_HEIGHT + doorcount ) return;
        orientation = UTT_HORIZONTAL;
    }

    position = -1;
    if( orientation == UTT_HORIZONTAL )
    {
        while( position == -1 || position < starty + (UTT_MIN_HEIGHT/2) || position > endy - (UTT_MIN_HEIGHT/2) || uttDungeonMap[UTT_MAPIDX(position,startx)] == 0 || uttDungeonMap[UTT_MAPIDX(position,endx)] == 0 )
        {
            position = starty + arand(height);
        }
        door = startx + 1 + arand(width-1);
        door2 = -1;
        if( width >= UTT_EXTRA_DOOR ) door2 = startx + 1 + arand(width-1);
        for( i = startx; i < startx + width; i++ )
        {
            if( i != door && i != door2 )
                uttDungeonMap[ UTT_MAPIDX(position,i) ] = UTT_DUN_WALL;
        }
        uttMapGen( startx, starty, endx, position );
        uttMapGen( startx, position, endx, endy );
    }
    else if( orientation == UTT_VERTICAL )
    {
        while( position == -1 || position < startx + (UTT_MIN_WIDTH/2) || position > endx - (UTT_MIN_WIDTH/2) || uttDungeonMap[UTT_MAPIDX(starty,position)] == 0 || uttDungeonMap[UTT_MAPIDX(endy,position)] == 0 )
        {
            position = startx + arand(width);
        }
        door = starty + 1 + arand(height-3);
        door2 = -1;
        if( height >= UTT_EXTRA_DOOR ) door2 = starty + 1 + arand(height-3);
        for( i = starty; i < starty + height; i++ )
        {
            if( i != door && i != door+1 && i != door2 && i != door2+1 )
                uttDungeonMap[ UTT_MAPIDX(i,position) ] = UTT_DUN_WALL;
        }
        uttMapGen( startx, starty, position, endy );
        uttMapGen( position, starty, endx, endy );
    }
}

// Platform-forced row clamp added (real upstream can read one row past
// its own 16x16 array when extra==1 near the bottom edge) - see header
// comment.
int uttCheckProximity( int i, int j, int extra )
{
    int k, m;
    for( k = i-2; k <= i+1+extra; k++ )
    {
        if( k < 0 || k >= UTT_MAPSIZE ) continue;
        for( m = j-1; m <= j+1; m++ )
        {
            if( uttDungeonMap[ UTT_MAPIDX(k,m) ] == UTT_DUN_WALL ) return 0;
        }
    }
    return 1;
}

void uttMapDetail()
{
    int i, j;
    for( i = 1; i < UTT_MAPSIZE-1; i++ )
    {
        for( j = 1; j < UTT_MAPSIZE-1; j++ )
        {
            if( uttDungeonMap[UTT_MAPIDX(i-1,j)] == UTT_DUN_WALL && uttDungeonMap[UTT_MAPIDX(i,j)] == 0 )
            {
                uttDungeonMap[UTT_MAPIDX(i,j)] = 2;
            }
            if( i > 1 && uttDungeonMap[UTT_MAPIDX(i,j)] == 0 && UTT_DECORATION_CHANCE > arand(100) )
            {
                if( 50 > arand(100) )
                {
                    if( uttCheckProximity(i,j,0) )
                    {
                        uttDungeonMap[UTT_MAPIDX(i,j)] = 5;
                    }
                }
                else
                {
                    if( uttCheckProximity(i,j,1) )
                    {
                        uttDungeonMap[UTT_MAPIDX(i,j)] = 3;
                        uttDungeonMap[UTT_MAPIDX(i+1,j)] = 4;
                    }
                }
            }
        }
    }
}

// Real upstream's three bounded-by-nothing `while(1)` retry loops, each
// now capped at 1000 attempts with a guaranteed-safe fallback placement -
// see header comment (a real, if very unlikely on paper, unbounded-hang
// risk this platform can't tolerate the way real hardware's own infinite
// loop-until-input-changes tolerance might).
void uttMapExits()
{
    int i, j, attempts;

    if( uttDungeonLevel == 0 )
    {
        attempts = 0;
        while( attempts < 1000 )
        {
            i = UTT_MAPSIZE - 1;
            j = 1 + arand(UTT_MAPSIZE-1);
            if( uttDungeonMap[UTT_MAPIDX(i-1,j)] == 0 )
            {
                uttDungeonMap[UTT_MAPIDX(i,j)] = UTT_DUN_TILE_DOOR;
                if( uttPreviousLevel == -1 )
                {
                    uttDudeX = j*8;
                    uttDudeY = (i-1)*8;
                }
                break;
            }
            attempts++;
        }
        if( attempts >= 1000 )
        {
            i = UTT_MAPSIZE - 1; j = UTT_MAPSIZE/2;
            uttDungeonMap[UTT_MAPIDX(i,j)] = UTT_DUN_TILE_DOOR;
            if( uttPreviousLevel == -1 ) { uttDudeX = j*8; uttDudeY = (i-1)*8; }
        }
    }
    else
    {
        attempts = 0;
        while( attempts < 1000 )
        {
            i = 1 + arand(UTT_MAPSIZE-1);
            j = 1 + arand(UTT_MAPSIZE-1);
            if( uttDungeonMap[UTT_MAPIDX(i,j)] == 0 )
            {
                uttDungeonMap[UTT_MAPIDX(i,j)] = UTT_DUN_TILE_STAIRSDN;
                if( uttPreviousLevel >= 0 && uttPreviousLevel < uttDungeonLevel )
                {
                    if( uttDungeonMap[UTT_MAPIDX(i,j+1)] == 0 ) { uttDudeX=(j+1)*8; uttDudeY=i*8; }
                    else if( uttDungeonMap[UTT_MAPIDX(i+1,j)] == 0 ) { uttDudeX=j*8; uttDudeY=(i+1)*8; }
                    else if( uttDungeonMap[UTT_MAPIDX(i,j-1)] == 0 ) { uttDudeX=(j-1)*8; uttDudeY=i*8; }
                    else if( uttDungeonMap[UTT_MAPIDX(i-1,j)] == 0 ) { uttDudeX=j*8; uttDudeY=(i-1)*8; }
                }
                break;
            }
            attempts++;
        }
        if( attempts >= 1000 )
        {
            i = UTT_MAPSIZE/2; j = UTT_MAPSIZE/2;
            uttDungeonMap[UTT_MAPIDX(i,j)] = UTT_DUN_TILE_STAIRSDN;
        }
    }

    if( uttDungeonLevel < uttDungeonSize[uttDungeonId] - 1 )
    {
        attempts = 0;
        while( attempts < 1000 )
        {
            i = 1 + arand(UTT_MAPSIZE-1);
            j = 1 + arand(UTT_MAPSIZE-1);
            if( uttDungeonMap[UTT_MAPIDX(i,j)] == 0 )
            {
                uttDungeonMap[UTT_MAPIDX(i,j)] = UTT_DUN_TILE_STAIRSUP;
                if( uttPreviousLevel > uttDungeonLevel )
                {
                    if( uttDungeonMap[UTT_MAPIDX(i,j+1)] == 0 ) { uttDudeX=(j+1)*8; uttDudeY=i*8; }
                    else if( uttDungeonMap[UTT_MAPIDX(i+1,j)] == 0 ) { uttDudeX=j*8; uttDudeY=(i+1)*8; }
                    else if( uttDungeonMap[UTT_MAPIDX(i,j-1)] == 0 ) { uttDudeX=(j-1)*8; uttDudeY=i*8; }
                    else if( uttDungeonMap[UTT_MAPIDX(i-1,j)] == 0 ) { uttDudeX=j*8; uttDudeY=(i-1)*8; }
                }
                break;
            }
            attempts++;
        }
        // No fallback needed here: if no placement is ever found, this
        // generation of the dungeon simply has no stairs up - a real,
        // reachable possibility already implied by upstream's own gate
        // above (top floor of a dungeon has none by design).
    }
}

void uttDrawDungeon()
{
    int tile, i, j;
    for( i = 0; i < UTT_MAPSIZE; i++ )
    {
        for( j = 0; j < UTT_MAPSIZE; j++ )
        {
            tile = uttDungeonMap[ UTT_MAPIDX(i,j) ];
            if( tile != 0 )
            {
                if( tile <= UTT_NUM_DUN_TILES+1 )
                {
                    gbDrawBitmap( (j*8 - uttDudeX)+(UTT_SCREEN_WIDTH/2-4), (i*8 - uttDudeY)+(UTT_SCREEN_HEIGHT/2-4),
                        &uttDungeonTiles[ ((tile-2)+(uttDungeonTheme[uttDungeonId]*UTT_NUM_DUN_TILES))*10 ] );
                }
                else
                {
                    gbDrawBitmap( (j*8 - uttDudeX)+(UTT_SCREEN_WIDTH/2-4), (i*8 - uttDudeY)+(UTT_SCREEN_HEIGHT/2-4),
                        &uttCommonTiles[ (tile-2-UTT_NUM_DUN_TILES)*10 ] );
                }
            }
        }
    }
    if( uttPlayerMoving && (uttDudeFrame/2 == 0 || uttDudeFrame/2 == 2) )
    {
        gbDrawBitmap( UTT_SCREEN_WIDTH/2-4, UTT_SCREEN_HEIGHT/2-4, &uttPlayerSprites[((uttDudeFrame/2)+uttDudeAnimation*3)*10] );
    }
    else
    {
        gbDrawBitmap( UTT_SCREEN_WIDTH/2-4, UTT_SCREEN_HEIGHT/2-4, &uttPlayerSprites[(1+uttDudeAnimation*3)*10] );
    }
}

int uttTestCollision( int dir )
{
    int tile = 0;
    if( dir == UTT_UP )
    {
        tile = uttDungeonMap[ UTT_MAPIDX((uttDudeY-1)/8, uttDudeX/8) ];
        if( tile != UTT_DUN_TILE_DOOR && tile != UTT_DUN_TILE_STAIRSUP && tile != UTT_DUN_TILE_STAIRSDN && uttDungeonMap[UTT_MAPIDX((uttDudeY-1)/8,(uttDudeX+7)/8)] )
            return uttDungeonMap[ UTT_MAPIDX((uttDudeY-1)/8,(uttDudeX+7)/8) ];
        return tile;
    }
    else if( dir == UTT_DOWN )
    {
        tile = uttDungeonMap[ UTT_MAPIDX((uttDudeY+8)/8, uttDudeX/8) ];
        if( tile != UTT_DUN_TILE_DOOR && tile != UTT_DUN_TILE_STAIRSUP && tile != UTT_DUN_TILE_STAIRSDN && uttDungeonMap[UTT_MAPIDX((uttDudeY+8)/8,(uttDudeX+7)/8)] )
            return uttDungeonMap[ UTT_MAPIDX((uttDudeY+8)/8,(uttDudeX+7)/8) ];
        return tile;
    }
    else if( dir == UTT_LEFT )
    {
        tile = uttDungeonMap[ UTT_MAPIDX(uttDudeY/8,(uttDudeX-1)/8) ];
        if( tile != UTT_DUN_TILE_DOOR && tile != UTT_DUN_TILE_STAIRSUP && tile != UTT_DUN_TILE_STAIRSDN && uttDungeonMap[UTT_MAPIDX((uttDudeY+7)/8,(uttDudeX-1)/8)] )
            return uttDungeonMap[ UTT_MAPIDX((uttDudeY+7)/8,(uttDudeX-1)/8) ];
        return tile;
    }
    else
    {
        tile = uttDungeonMap[ UTT_MAPIDX(uttDudeY/8,(uttDudeX+8)/8) ];
        if( tile != UTT_DUN_TILE_DOOR && tile != UTT_DUN_TILE_STAIRSUP && tile != UTT_DUN_TILE_STAIRSDN && uttDungeonMap[UTT_MAPIDX((uttDudeY+7)/8,(uttDudeX+8)/8)] )
            return uttDungeonMap[ UTT_MAPIDX((uttDudeY+7)/8,(uttDudeX+8)/8) ];
        return tile;
    }
}

void uttTestCollideExit( int collision )
{
    if( collision == UTT_DUN_TILE_DOOR )
    {
        uttMode = UTT_TO_WORLD;
        uttTransition = -UTT_SCREEN_HEIGHT/2;
        uttDudeX = uttDungeonX[uttDungeonId]*8;
        uttDudeY = (uttDungeonY[uttDungeonId]+1)*8;
        return;
    }
    if( collision == UTT_DUN_TILE_STAIRSUP )
    {
        uttPreviousLevel = uttDungeonLevel;
        uttDungeonLevel++;
        uttDungeonGenerated = 0;
        uttMode = UTT_TO_DUNGEON;
        uttTransition = -UTT_SCREEN_HEIGHT/2;
        return;
    }
    if( collision == UTT_DUN_TILE_STAIRSDN )
    {
        uttPreviousLevel = uttDungeonLevel;
        uttDungeonLevel--;
        uttDungeonGenerated = 0;
        uttMode = UTT_TO_DUNGEON;
        uttTransition = -UTT_SCREEN_HEIGHT/2;
        return;
    }
}

void uttStepDungeon()
{
    int collision;

    if( uttDungeonGenerated == 0 )
    {
        uttMapInit();
        uttMapGen( 0, 0, UTT_MAPSIZE, UTT_MAPSIZE );
        uttMapDetail();
        uttDudeX = 16;
        uttDudeY = 16;
        uttMapExits();
        uttDungeonGenerated = 1;
    }

    uttPlayerMoving = 0;
    collision = 0;

    if( gbRepeat(BTN_UP,1) )
    {
        collision = uttTestCollision(UTT_UP);
        if( collision ) uttTestCollideExit(collision);
        else { uttDudeY--; uttDudeAnimation = UTT_UP; uttPlayerMoving = 1; }
    }
    else if( gbRepeat(BTN_DOWN,1) )
    {
        collision = uttTestCollision(UTT_DOWN);
        if( collision ) uttTestCollideExit(collision);
        else { uttDudeY++; uttDudeAnimation = UTT_DOWN; uttPlayerMoving = 1; }
    }
    if( gbRepeat(BTN_LEFT,1) )
    {
        collision = uttTestCollision(UTT_LEFT);
        if( collision ) uttTestCollideExit(collision);
        else { uttDudeX--; uttDudeAnimation = UTT_LEFT; uttPlayerMoving = 1; }
    }
    else if( gbRepeat(BTN_RIGHT,1) )
    {
        collision = uttTestCollision(UTT_RIGHT);
        if( collision ) uttTestCollideExit(collision);
        else { uttDudeX++; uttDudeAnimation = UTT_RIGHT; uttPlayerMoving = 1; }
    }

    if( uttPlayerMoving ) uttTryCombat();
}

// -----------------------------------------------------------------------
// Combat (real battle.ino do_combat() - the largest single function in
// this port). VICTORY/DEFEAT bare `return;` calls (no trailing drawRect)
// are a real, deliberate, preserved upstream quirk - see header comment.
// -----------------------------------------------------------------------
void uttDoCombat()
{
    int i;

    if( uttCombatMode == UTT_PRECOMBAT )
    {
        uttGenEnemies();
        for( i = 0; i < 6; i++ )
        {
            uttCombatStatus[i] = -1;
            if( i < 3 )
            {
                uttPartyBonusSpeed[i] = 0;
                uttPartyBonusDamage[i] = 0;
                uttPartyBonusDefense[i] = 0;
            }
        }
        uttShadowStealthBonus = 0;
        uttNurseProtectBonus = -1;
        uttDoCombatStep();
    }

    gbCursorY = 0;

    if( uttEnemyBufLvl[0] != -1 )
    {
        gbCursorX = 0;
        uttCopyToBuffer( uttEnemyBufNme[0], UTT_TBL_ENEMY );
        gbPrintString( uttCombatBuffer );
        gbDrawBitmap( UTT_SCREEN_WIDTH/4-4, UTT_SCREEN_HEIGHT/8, &uttEnemyBmps[uttEnemyBufImg[0]*18] );
    }
    if( uttEnemyBufLvl[1] != -1 )
    {
        gbCursorX = UTT_SCREEN_WIDTH/2-(3*4)-2;
        uttCopyToBuffer( uttEnemyBufNme[1], UTT_TBL_ENEMY );
        gbPrintString( uttCombatBuffer );
        gbDrawBitmap( UTT_SCREEN_WIDTH/2-4, UTT_SCREEN_HEIGHT/8, &uttEnemyBmps[uttEnemyBufImg[1]*18] );
    }
    if( uttEnemyBufLvl[2] != -1 )
    {
        gbCursorX = UTT_SCREEN_WIDTH-(7*4);
        uttCopyToBuffer( uttEnemyBufNme[2], UTT_TBL_ENEMY );
        gbPrintString( uttCombatBuffer );
        gbDrawBitmap( UTT_SCREEN_WIDTH/4*3-4, UTT_SCREEN_HEIGHT/8, &uttEnemyBmps[uttEnemyBufImg[2]*18] );
    }

    if( uttCombatMode == UTT_MESSAGE || uttCombatMode == UTT_VICTORY || uttCombatMode == UTT_DEFEAT )
    {
        gbCursorX = 4;
        gbCursorY = UTT_SCREEN_HEIGHT/2;
        gbPrintString( uttCombatMessage );
        if( gbPressed(BTN_A) )
        {
            int stepForward = 1;
            int fi;
            gbPlayTick();
            if( uttCombatMode == UTT_VICTORY )
            {
                if( uttPartyLevel[UTT_SHADOW] > 0 )
                {
                    uttCombatMode = UTT_POSTCOMBAT;
                    uttMenuSelection = UTT_ALLY_MENU;
                }
                else
                {
                    uttCombatSelection = 0;
                    uttGiveXp();
                }
                return;
            }
            if( uttCombatMode == UTT_DEFEAT )
            {
                uttCombatMode = UTT_PRECOMBAT;
                uttMode = UTT_GAME_OVER;
                return;
            }
            if( uttEnemyBufLvl[0] == -1 && uttEnemyBufLvl[1] == -1 && uttEnemyBufLvl[2] == -1 )
            {
                stepForward = 0;
                uttCombatMode = UTT_VICTORY;
                uttCopyActionToMsgBuffer( 0, 0, uttCombatXp, UTT_PWIN );
            }
            for( fi = 0; fi < 3; fi++ )
            {
                if( uttEnemyBufLvl[fi] != -1 && uttEnemyHealth[fi] == 0 )
                {
                    uttCopyActionToMsgBuffer( uttEnemyBufNme[fi], 0, 0, UTT_EFALL );
                    uttCombatStatus[UTT_ENEMY1+fi] = -1;
                    uttEnemyBufLvl[fi] = -1;
                    stepForward = 0;
                    break;
                }
                if( uttPartyHealth[fi] == 0 )
                {
                    uttCopyActionToMsgBuffer( fi, 0, 0, UTT_PFALL );
                    stepForward = 0;
                    uttCombatMode = UTT_DEFEAT;
                    break;
                }
            }
            if( stepForward ) uttDoCombatStep();
        }
    }
    else if( (uttCombatMode >= UTT_MUDLARK && uttCombatMode <= UTT_NURSE) || uttCombatMode == UTT_POSTCOMBAT )
    {
        int mod;

        if( uttCombatMode >= UTT_MUDLARK && uttCombatMode <= UTT_NURSE )
        {
            gbDrawRect( UTT_SCREEN_WIDTH/2, UTT_SCREEN_HEIGHT/2+1, UTT_SCREEN_WIDTH/2-2, 3 );
            gbDrawLine( UTT_SCREEN_WIDTH/2, UTT_SCREEN_HEIGHT/2+2,
                UTT_SCREEN_WIDTH/2 + ((UTT_SCREEN_WIDTH/2-4)*uttPartyHealth[uttCombatMode]) / (uttPartyLevel[uttCombatMode]*20),
                UTT_SCREEN_HEIGHT/2+2 );

            gbCursorX = UTT_SCREEN_WIDTH-33;
            gbCursorY = UTT_SCREEN_HEIGHT/2+6;
            uttCopyToBuffer( uttCombatMode, UTT_TBL_PLAYER );
            gbPrintString( uttCombatBuffer );
            gbCursorX = UTT_SCREEN_WIDTH-33;
            gbCursorY = UTT_SCREEN_HEIGHT/2+12;
            gbPrintNumber( uttPartyHealth[uttCombatMode] );
            gbPrintString( "/" );
            gbPrintNumber( uttPartyLevel[uttCombatMode]*20 );

            gbCursorX = UTT_SCREEN_WIDTH-33;
            gbCursorY = UTT_SCREEN_HEIGHT/2+18;
            gbPrintString( "XP" );
            gbPrintNumber( uttPartyXp[uttCombatMode] );
            gbPrintString( "L" );
            gbPrintNumber( uttPartyLevel[uttCombatMode] );
        }

        uttDrawMenu( uttMenuSelection );

        if( gbPressed(BTN_UP) )
        {
            gbPlayTick();
            uttCombatSelection--;
        }
        else if( gbPressed(BTN_DOWN) )
        {
            gbPlayTick();
            uttCombatSelection++;
        }
        else if( gbPressed(BTN_A) )
        {
            gbPlayOK();
            if( (uttMenuSelection == UTT_MUDLARK_MENU || uttMenuSelection == UTT_SHADOW_MENU) && uttCombatSelection == 0 )
            {
                uttMenuSelection = UTT_ENEMY_MENU;
                uttCombatSelection = 0;
            }
            else if( uttMenuSelection == UTT_NURSE_MENU && uttCombatSelection == 0 )
            {
                uttNurseProtectBonus = -1;
                uttMenuSelection = UTT_ALLY_MENU;
                uttCombatSelection = 0;
            }
            else if( uttMenuSelection == UTT_MUDLARK_MENU && uttCombatSelection == 1 )
            {
                uttMenuSelection = UTT_ALLY_MENU;
                uttCombatSelection = 0;
            }
            else if( uttMenuSelection == UTT_SHADOW_MENU && uttCombatSelection == 1 )
            {
                uttShadowStealthBonus++;
                uttCombatMessage[ uttAppendToMsgBuffer(5, UTT_TBL_MENU, 0) ] = 0;
                uttCombatMode = UTT_MESSAGE;
            }
            else if( uttMenuSelection == UTT_NURSE_MENU && uttCombatSelection == 1 )
            {
                int k, healing;
                healing = uttPartyLevel[UTT_NURSE];
                healing += (healing/10/2 + 1)*uttPartyBonusDamage[UTT_NURSE];
                for( k = 0; k < 3; k++ )
                {
                    uttPartyHealth[k] += healing;
                    if( uttPartyHealth[k] > uttPartyLevel[k]*20 ) uttPartyHealth[k] = uttPartyLevel[k]*20;
                }
                uttCopyActionToMsgBuffer( 0, 0, healing, UTT_HEALALL );
                uttCombatMode = UTT_MESSAGE;
            }
            else if( uttMenuSelection == UTT_MUDLARK_MENU && uttCombatSelection == 2 )
            {
                if( arand(100) < 30 )
                {
                    uttInventory[UTT_ITEM_FRUIT]++;
                    uttCopyActionToMsgBuffer( 0, 0, UTT_ITEM_FRUIT, UTT_PITEM );
                    if( uttInventory[UTT_ITEM_FRUIT] > UTT_INVENTORY_MAX )
                    {
                        uttInventory[UTT_ITEM_FRUIT] = UTT_INVENTORY_MAX;
                        uttPartyHealth[UTT_MUDLARK] += 2*uttPartyLevel[UTT_MUDLARK];
                    }
                }
                else if( arand(100) < 30 )
                {
                    uttInventory[UTT_ITEM_BREAD]++;
                    uttCopyActionToMsgBuffer( 0, 0, UTT_ITEM_BREAD, UTT_PITEM );
                    if( uttInventory[UTT_ITEM_BREAD] > UTT_INVENTORY_MAX )
                    {
                        uttInventory[UTT_ITEM_BREAD] = UTT_INVENTORY_MAX;
                        uttPartyHealth[UTT_MUDLARK] += 3*uttPartyLevel[UTT_MUDLARK];
                    }
                }
                else
                {
                    int chosen = arand(4) + 2;
                    uttInventory[chosen]++;
                    uttCopyActionToMsgBuffer( 0, 0, chosen, UTT_PITEM );
                    if( uttInventory[chosen] > UTT_INVENTORY_MAX )
                    {
                        uttInventory[chosen] = UTT_INVENTORY_MAX;
                        if( chosen == UTT_ITEM_MEAT ) uttPartyBonusDamage[UTT_MUDLARK] += 4;
                        else if( chosen == UTT_ITEM_TONIC ) uttPartyHealth[UTT_MUDLARK] += 5*uttPartyLevel[UTT_MUDLARK];
                        else if( chosen == UTT_ITEM_TEA ) uttPartyBonusSpeed[UTT_MUDLARK] += 4;
                        else if( chosen == UTT_ITEM_LIQUOR ) uttPartyBonusDefense[UTT_MUDLARK] += 2;
                    }
                }
                if( uttPartyHealth[UTT_MUDLARK] > uttPartyLevel[UTT_MUDLARK]*20 ) uttPartyHealth[UTT_MUDLARK] = uttPartyLevel[UTT_MUDLARK]*20;
                uttCombatMode = UTT_MESSAGE;
            }
            else if( uttMenuSelection == UTT_SHADOW_MENU && uttCombatSelection == 2 )
            {
                uttPartyBonusSpeed[UTT_SHADOW] += 3;
                uttCopyActionToMsgBuffer( UTT_SHADOW, 0, 1, UTT_PSPEED );
                uttCombatMode = UTT_MESSAGE;
            }
            else if( uttMenuSelection == UTT_NURSE_MENU && uttCombatSelection == 2 )
            {
                uttNurseProtectBonus = 4;
                uttMenuSelection = UTT_ALLY_MENU;
                uttCombatSelection = 0;
            }
            else if( (uttMenuSelection >= UTT_MUDLARK_MENU && uttMenuSelection <= UTT_NURSE_MENU) && uttCombatSelection == 3 )
            {
                uttMenuSelection = UTT_SECONDARY_MENU;
                uttCombatSelection = 0;
            }
            else if( uttMenuSelection == UTT_SECONDARY_MENU )
            {
                if( uttCombatSelection == 0 )
                {
                    if( uttInventory[0] > 0 || uttInventory[1] > 0 || uttInventory[2] > 0 )
                    {
                        uttMenuSelection = UTT_FOOD_MENU;
                        uttCombatSelection = 0;
                    }
                }
                else if( uttCombatSelection == 1 )
                {
                    if( uttInventory[3] > 0 || uttInventory[4] > 0 || uttInventory[5] > 0 )
                    {
                        uttMenuSelection = UTT_DRINK_MENU;
                        uttCombatSelection = 0;
                    }
                }
                else
                {
                    if( !uttIsBoss && arand(2) == 0 )
                    {
                        uttCombatMode = UTT_PRECOMBAT;
                        uttMode = uttMetaMode;
                    }
                    else
                    {
                        uttCombatMessage[ uttAppendToMsgBuffer(7, UTT_TBL_COMBAT, 0) ] = 0;
                        uttCombatMode = UTT_MESSAGE;
                    }
                }
            }
            else if( uttMenuSelection == UTT_ENEMY_MENU )
            {
                int damage = uttCalculateDamage( uttPartyLevel[uttCombatMode] );
                if( uttCombatMode == UTT_MUDLARK ) damage *= 2;
                if( uttCombatMode == UTT_SHADOW ) damage *= (uttShadowStealthBonus+1);
                damage += (damage/10/2 + 1)*uttPartyBonusDamage[uttCombatMode];
                if( uttCombatSelection == 1 && uttEnemyBufLvl[0] == -1 )
                {
                    uttCombatSelection = 2;
                }
                else
                {
                    for( ; uttCombatSelection < 3; uttCombatSelection++ )
                    {
                        if( uttEnemyBufLvl[uttCombatSelection] != -1 ) break;
                    }
                }
                uttCopyActionToMsgBuffer( uttCombatMode, uttEnemyBufNme[uttCombatSelection], damage, UTT_PL2EN );
                if( damage > uttEnemyHealth[uttCombatSelection] ) uttEnemyHealth[uttCombatSelection] = 0;
                else uttEnemyHealth[uttCombatSelection] -= damage;
                uttCombatMode = UTT_MESSAGE;
            }
            else if( uttMenuSelection == UTT_ALLY_MENU )
            {
                if( uttCombatMode == UTT_POSTCOMBAT )
                {
                    uttGiveXp();
                    return;
                }
                else if( uttCombatStatus[UTT_MUDLARK] == 0 )
                {
                    uttPartyBonusDamage[uttCombatSelection] += 2;
                    uttCopyActionToMsgBuffer( uttCombatSelection, 0, 1, UTT_PDAMAGE );
                    uttCombatMode = UTT_MESSAGE;
                }
                else if( uttNurseProtectBonus == -1 )
                {
                    int healing = 3*uttPartyLevel[UTT_NURSE];
                    healing += (healing/10/2 + 1)*uttPartyBonusDamage[UTT_NURSE];
                    uttPartyHealth[uttCombatSelection] += healing;
                    if( uttPartyHealth[uttCombatSelection] > uttPartyLevel[uttCombatSelection]*20 )
                        uttPartyHealth[uttCombatSelection] = uttPartyLevel[uttCombatSelection]*20;
                    uttCopyActionToMsgBuffer( uttCombatSelection, 0, healing, UTT_PHEAL );
                    uttCombatMode = UTT_MESSAGE;
                }
                else
                {
                    int offset;
                    uttNurseProtectBonus = uttCombatSelection;
                    offset = uttAppendToMsgBuffer( 12, UTT_TBL_COMBAT, 0 );
                    uttCombatMessage[offset++] = ' ';
                    offset = uttAppendToMsgBuffer( uttCombatSelection, UTT_TBL_PLAYER, offset );
                    uttCombatMessage[offset++] = 0;
                    uttCombatMode = UTT_MESSAGE;
                }
            }
            else if( uttMenuSelection == UTT_FOOD_MENU || uttMenuSelection == UTT_DRINK_MENU )
            {
                int item = 0;
                int ii, idx;
                for( ii = 0; ii < UTT_INVENTORY_SIZE/2; ii++ )
                {
                    if( uttMenuSelection == UTT_FOOD_MENU ) idx = ii;
                    else idx = ii + (UTT_INVENTORY_SIZE/2);
                    if( uttInventory[idx] != 0 ) item++;
                    if( item == uttCombatSelection+1 )
                    {
                        item = idx;
                        break;
                    }
                }
                if( item == UTT_ITEM_FRUIT )
                {
                    uttPartyHealth[uttCombatMode] += 2*uttPartyLevel[uttCombatMode];
                    uttInventory[item]--;
                    uttCopyActionToMsgBuffer( uttCombatMode, 0, 2*uttPartyLevel[uttCombatMode], UTT_PHEAL );
                }
                else if( item == UTT_ITEM_BREAD )
                {
                    uttPartyHealth[uttCombatMode] += 3*uttPartyLevel[uttCombatMode];
                    uttInventory[item]--;
                    uttCopyActionToMsgBuffer( uttCombatMode, 0, 3*uttPartyLevel[uttCombatMode], UTT_PHEAL );
                }
                else if( item == UTT_ITEM_MEAT )
                {
                    uttPartyBonusDamage[uttCombatMode] += 4;
                    uttInventory[item]--;
                    uttCopyActionToMsgBuffer( uttCombatMode, 0, 1, UTT_PDAMAGE );
                }
                else if( item == UTT_ITEM_TONIC )
                {
                    uttPartyHealth[uttCombatMode] += 5*uttPartyLevel[uttCombatMode];
                    uttInventory[item]--;
                    uttCopyActionToMsgBuffer( uttCombatMode, 0, 5*uttPartyLevel[uttCombatMode], UTT_PHEAL );
                }
                else if( item == UTT_ITEM_TEA )
                {
                    uttPartyBonusSpeed[uttCombatMode] += 4;
                    uttInventory[item]--;
                    uttCopyActionToMsgBuffer( uttCombatMode, 0, 1, UTT_PSPEED );
                }
                else if( item == UTT_ITEM_LIQUOR )
                {
                    uttPartyBonusDefense[uttCombatMode] += 2;
                    uttInventory[item]--;
                    uttCopyActionToMsgBuffer( uttCombatMode, 0, 1, UTT_PDEFENSE );
                }
                if( uttPartyHealth[uttCombatMode] > uttPartyLevel[uttCombatMode]*20 )
                    uttPartyHealth[uttCombatMode] = uttPartyLevel[uttCombatMode]*20;
                uttCombatMode = UTT_MESSAGE;
            }
        }
        else if( gbPressed(BTN_B) )
        {
            gbPlayCancel();
            if( uttMenuSelection == UTT_ENEMY_MENU || uttMenuSelection == UTT_ALLY_MENU || uttMenuSelection == UTT_SECONDARY_MENU )
            {
                uttMenuSelection = uttCombatMode;
                uttCombatSelection = 0;
            }
            else if( uttMenuSelection == UTT_FOOD_MENU || uttMenuSelection == UTT_DRINK_MENU )
            {
                uttMenuSelection = UTT_SECONDARY_MENU;
                uttCombatSelection = 0;
            }
        }

        mod = 4;
        if( uttMenuSelection == UTT_ENEMY_MENU )
        {
            int ei;
            mod--;
            for( ei = 0; ei < 3; ei++ )
            {
                if( uttEnemyBufLvl[ei] == -1 ) mod--;
            }
        }
        else if( uttMenuSelection == UTT_ALLY_MENU )
        {
            mod = 1;
            if( uttPartyLevel[UTT_SHADOW] != 0 ) mod++;
            if( uttPartyLevel[UTT_NURSE] != 0 ) mod++;
        }
        else if( uttMenuSelection == UTT_SECONDARY_MENU )
        {
            mod = 3;
        }
        else if( uttMenuSelection == UTT_FOOD_MENU || uttMenuSelection == UTT_DRINK_MENU )
        {
            int item = 0;
            int ii, idx;
            for( ii = 0; ii < UTT_INVENTORY_SIZE/2; ii++ )
            {
                if( uttMenuSelection == UTT_FOOD_MENU ) idx = ii;
                else idx = ii + (UTT_INVENTORY_SIZE/2);
                if( uttInventory[idx] != 0 ) item++;
            }
            if( item < 4 ) mod = item;
        }
        if( mod < 1 ) mod = 1; // platform-forced div/mod-by-zero hard-trap guard - see header comment
        uttCombatSelection %= mod;
    }
    else if( uttCombatMode >= UTT_ENEMY1 && uttCombatMode <= UTT_ENEMY3 )
    {
        int damage = uttCalculateDamage( uttEnemyBufLvl[uttCombatMode-UTT_ENEMY1] );
        int member;
        if( uttPartyLevel[UTT_NURSE] > 0 ) member = arand(3);
        else if( uttPartyLevel[UTT_SHADOW] > 0 ) member = arand(2);
        else member = 0;

        if( uttNurseProtectBonus == member )
        {
            if( uttPartyLevel[UTT_NURSE] >= damage ) damage = 1;
            else damage = damage - uttPartyLevel[UTT_NURSE];
            damage -= (damage/10/2 + 1)*uttPartyBonusDefense[UTT_NURSE];
            if( damage <= 0 ) damage = 1;
            uttCopyActionToMsgBuffer( uttEnemyBufNme[uttCombatMode-UTT_ENEMY1], member, damage, UTT_PROTECT );
            member = UTT_NURSE;
        }
        else
        {
            damage -= (damage/10/2 + 1)*uttPartyBonusDefense[member];
            if( damage <= 0 ) damage = 1;
            uttCopyActionToMsgBuffer( uttEnemyBufNme[uttCombatMode-UTT_ENEMY1], member, damage, UTT_EN2PL );
        }

        if( damage > uttPartyHealth[member] ) uttPartyHealth[member] = 0;
        else uttPartyHealth[member] -= damage;
        uttCombatMode = UTT_MESSAGE;
    }

    gbDrawRect( 0, UTT_SCREEN_HEIGHT/2-1, UTT_SCREEN_WIDTH, UTT_SCREEN_HEIGHT/2+1 );
}

// -----------------------------------------------------------------------
// Dialogue (real dialogue.ino - a real huffman decoder over uttDialogue[],
// walking uttHuffTree[] one bit at a time).
// -----------------------------------------------------------------------

int uttHuffTreeGet( int item )
{
    return uttHuffTree[uttHuffTreeIndex][item];
}

// Platform-forced safety net added (`uttHuffIndex>=UTT_DIALOGUE_SIZE`):
// real upstream's own equivalent loop has no bound at all, relying
// entirely on every real message ending in a real 0 byte before the
// compressed stream runs out - true for every real TXT_* entry actually
// used in this game, but guarded anyway rather than ever reading past the
// real embedded array.
void uttFillDialogueBuffer( int seekIndex )
{
    int tmp, offset;
    offset = 0;
    while( true )
    {
        if( uttHuffIndex >= UTT_DIALOGUE_SIZE )
        {
            uttCombatMessage[offset] = 0;
            return;
        }
        tmp = uttDialogue[uttHuffIndex];
        if( (tmp & uttHuffMask) == 0 )
            uttHuffTreeIndex = uttHuffTreeGet(1);
        else
            uttHuffTreeIndex = uttHuffTreeGet(2);
        tmp = uttHuffTreeGet(0);
        if( tmp != 255 )
        {
            if( seekIndex > 0 )
            {
                seekIndex--;
            }
            else
            {
                uttCombatMessage[offset] = tmp;
                offset++;
                if( tmp == 0 ) return;
            }
            uttHuffTreeIndex = 0;
        }
        uttHuffMask = uttHuffMask >> 1; // always non-negative (128..0) - safe logical shift
        if( uttHuffMask == 0 )
        {
            uttHuffIndex++;
            uttHuffMask = 128;
        }
    }
}

void uttDisplayDialogue( int index, int len, int who, int table )
{
    uttMetaMode = uttMode;
    uttDialogueIndex = index;
    uttDialogueRemaining = len;
    uttCopyToBuffer( who, table );
    uttHuffMask = 128;
    uttHuffIndex = 0;
    uttHuffTreeIndex = 0;
    uttFillDialogueBuffer( uttDialogueIndex );
    uttMode = UTT_DIALOGUE;
}

void uttStepDialogue()
{
    gbSetColor(GB_WHITE);
    gbFillRect( 0, UTT_SCREEN_HEIGHT/2-1, UTT_SCREEN_WIDTH, UTT_SCREEN_HEIGHT/2+1 );
    gbSetColor(GB_BLACK);
    gbDrawRect( 0, UTT_SCREEN_HEIGHT/2-1, UTT_SCREEN_WIDTH, UTT_SCREEN_HEIGHT/2+1 );

    gbCursorX = 1;
    gbCursorY = UTT_SCREEN_HEIGHT/2;
    if( uttCombatBuffer[0] != 0 )
    {
        gbPrintString( uttCombatBuffer );
        gbCursorY += 6;
    }
    gbCursorX = 4;
    gbPrintString( uttCombatMessage );

    if( gbPressed(BTN_A) )
    {
        gbPlayOK();
        if( uttDialogueRemaining == 0 )
        {
            if( uttGameStatus[UTT_STATUS_MAIN] == 36 ) uttMode = UTT_YOU_WIN;
            else uttMode = uttMetaMode;
        }
        else
        {
            uttDialogueRemaining--;
            uttFillDialogueBuffer(0);
        }
    }
}

// -----------------------------------------------------------------------
// Events (real events.ino)
// -----------------------------------------------------------------------
void uttCheckEvents()
{
    int i;
    for( i = 0; i < UTT_NUM_DIALOGUE_EVENTS; i++ )
    {
        if( uttGameStatus[uttDlgEvStatusIdx[i]] == uttDlgEvStatusVal[i] && uttDungeonId == uttDlgEvDungeonId[i] && uttDungeonLevel == uttDlgEvDungeonLevel[i] )
        {
            if( uttGameStatus[uttDlgEvStatusIdx[i]] == 7 ) uttPartyLevel[UTT_SHADOW] = 8;
            if( uttGameStatus[uttDlgEvStatusIdx[i]] == 17 ) uttPartyLevel[UTT_NURSE] = 10;
            uttGameStatus[uttDlgEvStatusIdx[i]]++;
            if( uttDlgEvNameIdx[i] < 7 )
                uttDisplayDialogue( uttDlgEvDialogueIdx[i], uttDlgEvDialogueLen[i], uttDlgEvNameIdx[i], UTT_TBL_PLAYER );
            else
                uttDisplayDialogue( uttDlgEvDialogueIdx[i], uttDlgEvDialogueLen[i], uttDlgEvNameIdx[i], UTT_TBL_ENEMY );
            return;
        }
    }
    for( i = 0; i < UTT_NUM_BOSS_EVENTS; i++ )
    {
        if( uttGameStatus[uttBossEvStatusIdx[i]] == uttBossEvStatusVal[i] )
        {
            uttGameStatus[uttBossEvStatusIdx[i]]++;
            uttMetaMode = uttBossEvBossId[i];
            uttMode = UTT_COMBAT;
            return;
        }
    }
}

// -----------------------------------------------------------------------
// Save/load (real save.ino) - real EEPROM byte layout preserved exactly:
// addr 0 = 'T' signature, 1-6 = game_status[6], 7-18 = 3x(level,health_lo,
// health_hi,xp), 19-24 = inventory[6], 25-26 = dudex/8,dudey/8. See header
// comment for the real uttNarrowS8() int8_t-narrowing fix this needed.
// -----------------------------------------------------------------------
void uttSaveGame()
{
    int i, base;
    eeprom_write_byte( 0, 'T' );
    base = 1;
    for( i = 0; i < 6; i++ )
    {
        eeprom_write_byte( i+base, uttGameStatus[i] & 0xFF );
    }
    base = 7;
    for( i = 0; i < 3; i++ )
    {
        eeprom_write_byte( base, uttPartyLevel[i] ); base++;
        eeprom_write_byte( base, uttPartyHealth[i] & 0xFF ); base++;
        eeprom_write_byte( base, (uttPartyHealth[i]/256) & 0xFF ); base++;
        eeprom_write_byte( base, uttPartyXp[i] ); base++;
    }
    base = 7 + 12;
    for( i = 0; i < UTT_INVENTORY_SIZE; i++ )
    {
        eeprom_write_byte( base, uttInventory[i] ); base++;
    }
    base = 7 + 12 + UTT_INVENTORY_SIZE;
    eeprom_write_byte( base, uttDudeX/8 ); base++;
    eeprom_write_byte( base, uttDudeY/8 );
}

// Real upstream quirk, preserved exactly: no `uttTransition` reset here,
// so the real wipe-in animation never actually plays on a loaded game -
// see header comment.
void uttRestoreGame()
{
    int i, base, lo, hi;
    if( eeprom_read_byte(0) == 'T' )
    {
        base = 1;
        for( i = 0; i < 6; i++ )
        {
            uttGameStatus[i] = uttNarrowS8( eeprom_read_byte(i+base) );
        }
        base = 7;
        for( i = 0; i < 3; i++ )
        {
            uttPartyLevel[i] = eeprom_read_byte(base); base++;
            lo = eeprom_read_byte(base); base++;
            hi = eeprom_read_byte(base); base++;
            uttPartyHealth[i] = lo + hi*256;
            uttPartyXp[i] = eeprom_read_byte(base); base++;
        }
        base = 7 + 12;
        for( i = 0; i < UTT_INVENTORY_SIZE; i++ )
        {
            uttInventory[i] = eeprom_read_byte(base); base++;
        }
        base = 7 + 12 + UTT_INVENTORY_SIZE;
        uttDudeX = eeprom_read_byte(base)*8; base++;
        uttDudeY = eeprom_read_byte(base)*8;
        uttMode = UTT_TO_WORLD;
    }
}

// -----------------------------------------------------------------------
// Transition wipe + pause-menu progress readout (real under_the_tower.ino)
// -----------------------------------------------------------------------
void uttStepTransition()
{
    int i, lim;
    lim = UTT_SCREEN_HEIGHT/2 + 1 - gbAbsInt(uttTransition);
    for( i = 0; i < lim; i++ )
    {
        gbDrawFastVLine( i, 0, UTT_SCREEN_HEIGHT );
        gbDrawFastVLine( UTT_SCREEN_WIDTH-i, 0, UTT_SCREEN_HEIGHT );
        gbDrawFastHLine( 0, i, UTT_SCREEN_WIDTH );
        gbDrawFastHLine( 0, UTT_SCREEN_HEIGHT-i, UTT_SCREEN_WIDTH );
    }
    uttTransition += 4;
    if( uttTransition > UTT_SCREEN_HEIGHT/2 )
    {
        uttMode -= UTT_TRANSITION_DIFF;
    }
}

void uttPrintProgress()
{
    int i, progress;
    progress = 0;
    for( i = 0; i < 6; i++ ) progress += uttGameStatus[i];
    progress = (100*progress + (36+3+4+4+4+4)/2) / (36+3+4+4+4+4);
    gbCursorX = UTT_SCREEN_WIDTH/2-2*4;
    gbCursorY = UTT_SCREEN_HEIGHT-6;
    gbPrintNumber(progress);
    gbPrintString("%");
}

// -----------------------------------------------------------------------
// Shared per-mode draw+step helpers, called from the top-level dispatch
// below. Real upstream gates both the drawing AND the input-processing
// (movement/dungeon-regen/combat-turn) side of these behind
// `transition>=0` during a TO_* transition - preserved exactly (see
// header comment on the real, cosmetic-only simplification this port
// makes to the "old scene frozen under the wipe" first-half visual).
// -----------------------------------------------------------------------
void uttRunWorld()
{
    uttDrawWorld();
    uttStepWorld();
    if( uttGameStatus[UTT_STATUS_MAIN] == -1 )
    {
        uttGameStatus[UTT_STATUS_MAIN] = 0;
        uttDisplayDialogue( UTT_TXT_INTRO, UTT_TXT_INTRO_LEN, 5, UTT_TBL_PLAYER );
    }
    else if( uttGameStatus[UTT_STATUS_MAIN] == 0 && uttDudeX < 7*8 )
    {
        uttGameStatus[UTT_STATUS_MAIN] = 1;
        uttDisplayDialogue( UTT_TXT_SDW_INTRO, UTT_TXT_SDW_INTRO_LEN, UTT_SHADOW, UTT_TBL_PLAYER );
    }
    else if( uttGameStatus[UTT_STATUS_MAIN] == 1 && uttDudeY < 41*8 )
    {
        uttGameStatus[UTT_STATUS_MAIN] = 2;
        uttDisplayDialogue( UTT_TXT_SDW_CATPAW, UTT_TXT_SDW_CATPAW_LEN, UTT_SHADOW, UTT_TBL_PLAYER );
    }
}

void uttRunDungeon()
{
    uttDrawDungeon();
    uttStepDungeon();
    uttCheckEvents();
}

// -----------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------

// Explicit fresh-state reset every real launch (this cartridge can launch
// the same game more than once per session, unlike real hardware which
// only ever powers on once - matching this project's own established
// precedent, e.g. gameArmageddon.c's own init()) - matches real upstream
// setup()'s own real fresh-boot defaults exactly.
void gameUnderTheTower_init()
{
    int i;
    gbBegin();

    uttMode = UTT_MAIN_MENU;
    uttTransition = 0;
    uttMetaMode = UTT_WORLD;
    uttMenuSelection = 0;

    uttPlayerMoving = 0;
    uttDudeX = 25*8;
    uttDudeY = 57*8;
    uttDudeAnimation = UTT_DOWN;
    uttDudeFrame = 0;
    uttWorldFrame = 0;

    uttWorldNdx = 0;
    uttWorldCnt = 0;

    uttDungeonId = 0;
    uttDungeonGenerated = 0;
    uttDungeonLevel = 0;
    uttPreviousLevel = -1;

    for( i = 0; i < 3; i++ )
    {
        uttEnemyBufLvl[i] = -1;
        uttEnemyBufSpd[i] = 0;
        uttEnemyBufImg[i] = 0;
        uttEnemyBufNme[i] = 0;
        uttEnemyHealth[i] = 0;
        uttPartyBonusSpeed[i] = 0;
        uttPartyBonusDamage[i] = 0;
        uttPartyBonusDefense[i] = 0;
    }
    uttPartyLevel[0] = 1; uttPartyLevel[1] = 0; uttPartyLevel[2] = 0;
    uttPartyHealth[0] = 20; uttPartyHealth[1] = 160; uttPartyHealth[2] = 200;
    uttPartySpeed[0] = 4; uttPartySpeed[1] = 3; uttPartySpeed[2] = 4;
    uttPartyXp[0] = 0; uttPartyXp[1] = 0; uttPartyXp[2] = 0;

    uttShadowStealthBonus = 0;
    uttNurseProtectBonus = -1;

    uttInventory[0] = 2; uttInventory[1] = 1; uttInventory[2] = 0;
    uttInventory[3] = 0; uttInventory[4] = 1; uttInventory[5] = 0;
    uttNextCombat = 32;

    uttCombatMode = UTT_PRECOMBAT;
    uttCombatSelection = 0;
    uttCombatXp = 0;
    uttIsBoss = 0;
    for( i = 0; i < 6; i++ ) uttCombatStatus[i] = -1;
    for( i = 0; i < 8; i++ ) uttCombatBuffer[i] = 0;
    uttCombatMessage[0] = 0;

    uttDialogueIndex = 0;
    uttDialogueRemaining = 0;
    uttHuffIndex = 0;
    uttHuffTreeIndex = 0;
    uttHuffMask = 128;

    uttGameStatus[0] = -1;
    uttGameStatus[1] = 0;
    uttGameStatus[2] = 0;
    uttGameStatus[3] = 0;
    uttGameStatus[4] = 0;
    uttGameStatus[5] = 0;
}

void gameUnderTheTower_update()
{
    if( !gbUpdate() ) return;

    gbCursorY = 6;
    uttDudeFrame++;
    uttDudeFrame %= 7;
    uttWorldFrame++;
    uttWorldFrame %= 8;

    if( uttMode == UTT_TO_WORLD )
    {
        uttStepTransition();
        if( uttTransition >= 0 ) uttRunWorld();
    }
    else if( uttMode == UTT_WORLD )
    {
        uttRunWorld();
    }
    else if( uttMode == UTT_TO_DUNGEON )
    {
        uttStepTransition();
        if( uttTransition >= 0 ) uttRunDungeon();
    }
    else if( uttMode == UTT_DUNGEON )
    {
        uttRunDungeon();
    }
    else if( uttMode == UTT_TO_COMBAT )
    {
        if( uttTransition >= 0 ) uttDoCombat();
        uttStepTransition();
    }
    else if( uttMode == UTT_COMBAT )
    {
        uttDoCombat();
    }
    else if( uttMode == UTT_DIALOGUE )
    {
        if( uttMetaMode == UTT_WORLD ) uttDrawWorld();
        else uttDrawDungeon();
        uttStepDialogue();
    }
    else if( uttMode == UTT_MAIN_MENU )
    {
        gbCursorX = UTT_SCREEN_WIDTH/2-7*4;
        gbPrintString( "UNDER THE TOWER" );
        gbCursorX = UTT_SCREEN_WIDTH/2-2*4;
        gbCursorY = UTT_SCREEN_HEIGHT/2;
        gbPrintString( "NEW" );
        gbCursorY += 6;
        gbCursorX = UTT_SCREEN_WIDTH/2-2*4;
        gbPrintString( "LOAD" );
        gbCursorY += 6;
        gbCursorX = UTT_SCREEN_WIDTH/2-2*4;
        gbPrintString( "QUIT" );

        gbCursorX = UTT_SCREEN_WIDTH/2-3*4;
        gbCursorY = UTT_SCREEN_HEIGHT/2;
        if( uttMenuSelection == 1 ) gbCursorY += 6;
        if( uttMenuSelection == 2 ) gbCursorY += 12;
        gbPrintString( uttArrowIcon );

        if( gbPressed(BTN_UP) )
        {
            uttMenuSelection--;
            if( uttMenuSelection < 0 ) uttMenuSelection = 2;
        }
        else if( gbPressed(BTN_DOWN) )
        {
            uttMenuSelection++;
            uttMenuSelection %= 3;
        }
        else if( gbPressed(BTN_A) )
        {
            gbPickRandomSeed();
            if( uttMenuSelection == 1 )
            {
                uttRestoreGame();
            }
            else if( uttMenuSelection == 2 )
            {
                // real upstream gb.changeGame() (SD-card multi-cartridge
                // menu handoff) - no Vircon32 equivalent, left a no-op;
                // see header comment.
            }
            else
            {
                uttMode = UTT_WORLD;
            }
        }
    }
    else if( uttMode == UTT_PAUSE_MENU )
    {
        gbCursorX = UTT_SCREEN_WIDTH/2-3*4;
        gbPrintString( "PAUSED" );
        gbCursorX = UTT_SCREEN_WIDTH/2-2*4;
        gbCursorY = UTT_SCREEN_HEIGHT/2;
        gbPrintString( "BACK" );
        gbCursorY += 6;
        if( uttMetaMode == UTT_WORLD )
        {
            gbCursorX = UTT_SCREEN_WIDTH/2-2*4;
            gbPrintString( "SAVE" );
        }
        gbCursorX = UTT_SCREEN_WIDTH/2-3*4;
        gbCursorY = UTT_SCREEN_HEIGHT/2;
        if( uttMenuSelection == 1 ) gbCursorY += 6;
        gbPrintString( uttArrowIcon );
        uttPrintProgress();

        if( uttMetaMode == UTT_WORLD && gbPressed(BTN_UP) )
        {
            uttMenuSelection--;
            if( uttMenuSelection < 0 ) uttMenuSelection = 1;
        }
        else if( uttMetaMode == UTT_WORLD && gbPressed(BTN_DOWN) )
        {
            uttMenuSelection++;
            uttMenuSelection %= 2;
        }
        else if( gbPressed(BTN_A) )
        {
            if( uttMenuSelection == 1 ) uttSaveGame();
            uttMode = uttMetaMode;
        }
    }
    else if( uttMode == UTT_GAME_OVER )
    {
        gbCursorX = UTT_SCREEN_WIDTH/2-4*4;
        gbPrintString( "GAME OVER" );
        gbCursorX = UTT_SCREEN_WIDTH/2-3*4;
        gbCursorY = UTT_SCREEN_HEIGHT/2;
        gbPrintString( uttArrowIcon );
        gbPrintString( "LOAD" );
        if( gbPressed(BTN_A) )
        {
            uttRestoreGame();
        }
    }
    else if( uttMode == UTT_YOU_WIN )
    {
        gbCursorX = UTT_SCREEN_WIDTH/2-3*4;
        gbPrintString( "YOU WIN" );
        uttPrintProgress();
    }

    if( uttMode != UTT_COMBAT && uttMode < UTT_TO_WORLD && gbPressed(BTN_C) )
    {
        uttMenuSelection = 0;
        uttMetaMode = uttMode;
        uttMode = UTT_PAUSE_MENU;
    }

    gbRenderFrame();
}

