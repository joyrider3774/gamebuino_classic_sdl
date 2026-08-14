#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// CopterStrike (Frakasss, license: none specified upstream - no LICENSE file
// anywhere in the repo, github.com/Frakasss/CopterStrike). A top-down
// Desert-Strike-style attack-helicopter game: fly a scrolling 630x630 world,
// strafe or rotate, shoot towers/bunkers/infantry/tanks, manage fuel and hull
// life by landing on heliports, and complete each mission's own objective
// before returning to base.
//
// -----------------------------------------------------------------------------
// What was merged, and why this cartridge has four missions
// -----------------------------------------------------------------------------
// Real hardware's 32KB AVR flash forced this game to ship as several separate
// physical cartridges - one mission each, flashed from an SD card by a
// separate `copterMenu` loader sketch. Vircon32 has no such ceiling, so this
// port is one genuine four-mission binary, something no real CopterStrike
// release ever was:
//
//   0 DESERT STRIKE   - destroy all 21 hostile buildings, return to base
//   1 FOREST STRIKE   - same objective, forest tileset and map
//   2 CONVOI          - escort a truck along a fixed road to the far corner
//   3 SEARCHDOC       - find the data hidden in one random bunker, return
//
// The repo's own folder layout is genuinely confusing (a triple-nested
// byte-identical duplicate of the root build, four `missions/` variants, an
// asset-only unfinished `mer/` mission, a separate loader sketch); that
// analysis is already written up in this project's CLAUDE.md and was not
// redone here. Concretely:
//   - the REPO ROOT build (`CopterStrike.ino`/`Function.ino`/`Output.ino`/
//     `Sprites.ino`) is the real, author-merged two-mission game (Desert +
//     Forest behind one `outpt_menu()` with an Easy/Normal/Hard picker), and
//     is the base/template this port extends from 2 missions to 4;
//   - `missions/desert/` and `missions/forest/` are flash-trimmed standalone
//     re-cuts of content already in that root build - not ported separately;
//   - `missions/convoi/` and `missions/searchDoc/` are two real, distinct
//     missions the author never folded into the merged build - their own
//     objectives, level data and (for convoi) sprite art are ported here as
//     two more entries on the same mission-select menu;
//   - `missions/mer/` has sprite assets and no `.ino` at all - nothing to
//     port; `copterMenu/` is a real-hardware multi-cartridge SD flasher with
//     no Vircon32 equivalent, dropped the same way every other `load_game()`
//     call site in this project has been.
//
// -----------------------------------------------------------------------------
// Sprite-index renumbering (the author's own merge methodology, reapplied)
// -----------------------------------------------------------------------------
// Each standalone mission ships its own trimmed `sprites[]`/`destroy[]` table,
// so the same building has a different index in each one (standalone: bunker
// 5, camp 6, tour 7, oasis 8). When the author merged Desert+Forest into the
// root build he renumbered both missions' level data against one combined
// 11-entry table (bunker 6, camp 7, tour 8, oasis 9, desert village 4, forest
// village 5, forest 10). Convoi's and SearchDoc's level data are renumbered
// the exact same way here, into that same combined table. A real side effect
// worth stating plainly: several of convoi's own money-award `switch` cases
// never fired upstream precisely because its level data used the standalone
// numbering while its (copy-pasted) award code used the merged numbering -
// renumbering the data makes those awards start paying out. That is a
// consequence of the merge the author himself would have hit, not a
// discretionary gameplay change.
//
// -----------------------------------------------------------------------------
// Real upstream quirks preserved deliberately
// -----------------------------------------------------------------------------
//   - `fnctn_initEnnemyFire()`'s own fire-rate switch tests sprite 7 for
//     "tour", a leftover from the standalone numbering where 7 WAS the tower;
//     under the merged numbering towers are sprite 8, so that case never
//     fires and towers fall through to the hardcoded `fireTimer = 20` set
//     just above it - which happens to equal TMPTOUR anyway, so the visible
//     behaviour is identical. Kept exactly as written.
//   - `outpt_animBoom()`'s own triple-explosion special case tests sprite 4
//     (the desert village) only, so a destroyed FOREST village (sprite 5)
//     gets a single explosion instead of three. Kept.
//   - Every mobile-unit "tank" record in the root build (and in convoi)
//     assigns its spawn building to `mobilUnit_hostile[0].batiment` instead
//     of its own index - a real copy-paste bug that leaves all five tanks
//     with batiment 0 and repeatedly overwrites infantry unit 0's own value.
//     Kept verbatim. SearchDoc's own copy of that block is already correct
//     upstream and is ported correct.
//   - `fnctn_checkPlayerFire()`'s own mobile-unit money award switches on
//     `building_friend[i].sprite` rather than the unit's own sprite - a real
//     upstream bug that makes that award dead code (friend buildings are only
//     ever sprite 4/5/7, never 0/1). Kept as dead code, just index-bounded
//     (see the out-of-bounds note below).
//   - Convoi's `outpt_draw_route()` picks its explosion frame with
//     `cptExplosion / NB_FRAME_EXPLOSION` (35 ticks / 7 frames) instead of
//     `cptExplosion / (TEMP_EXPOLOSION / NB_FRAME_EXPLOSION)`, so only frames
//     0-4 of the 7-frame road-explosion animation are ever shown. Kept.
//   - The forest level's own heliport block writes `bkgrnd[5].sprite` five
//     times instead of `bkgrnd[0..4].sprite` (another copy-paste bug). Kept:
//     it is harmless because sprite 0 IS the heliport and those cells are
//     either still zero from cartridge boot or left at 0 by whichever
//     desert-tileset mission ran before.
//   - Convoi's `mecaB`/`mecaD`/`mecaH`/... "mechanic" sprites are declared
//     upstream and never referenced by any draw call - genuinely dead art on
//     real hardware too, so they are not ported. Convoi's `loading` bitmap is
//     likewise only ever drawn on the dropped SD-flash path.
//
// -----------------------------------------------------------------------------
// Deliberate deviations, and why each one was necessary
// -----------------------------------------------------------------------------
//   - Three real upstream loops read one element past the end of their own
//     array (`outpt_animBoom()`'s `i <= nbBuilding_Friend` / `i <= 20`, and
//     `fnctn_checkPlayerFire()`'s `building_friend[i]` for i up to 19). On AVR
//     those land in whatever global happens to sit next in RAM; here they
//     would read unrelated port globals and hand arbitrary values to a draw
//     call. Each is bounded to its own real array length. The visible result
//     is unchanged: on real hardware the extra `building_friend[9]` read
//     aliases `building_hostile[0]`, so at most it painted a duplicate
//     explosion on an already-exploding tower.
//   - `fnctn_resurection()` indexes `building_friend[batiment]` with the
//     value left by the copy-paste bug above (20, well past that 9-entry
//     array). Bounded here for the same reason, with the same reasoning.
//   - `updateFriendMobile()`'s own `sqrt(pow(dx,2)+pow(dy,2)) < 3` proximity
//     test is rewritten as the exactly-equivalent `dx*dx + dy*dy < 9`: this
//     platform hard-traps on `sqrt` of a negative value, and integer
//     truncation makes the two forms identical for every input anyway.
//   - Upstream's blocking `while(true){ if(gb.update()){...} }` screens
//     (convoi's own `endGameOK()` win screen, both missions' `returnToMenu()`
//     confirm dialog) are flattened into real states of this port's own
//     state machine - this shim's `gbUpdate()` is a per-tick gate, not a
//     re-entrant frame pump. `returnToMenu()` itself is dropped outright: its
//     only two outcomes were "restart the mission" and "SD-flash the loader
//     cartridge", and this cartridge's own Start-button quit dialog already
//     covers leaving a game. Button C returns to the mission select exactly
//     like the root build's own Button C does.
//   - `gb.titleScreen(gamelogo)` (blocking, real-library) is hand-rolled as a
//     title state drawing the same real logo bitmap, matching the treatment
//     every other port in this project gives that call.
//   - Sound: upstream's `gb.sound.command()` waveform/volume/slide setup has
//     no equivalent here, so `outpt_soundfx()` collapses to this project's
//     established one-shot-tone approximation - upstream's own literal
//     `playNote(17, 3, 0)`. Real hardware has ONE sound channel, so several
//     `soundfx()` calls in the same tick simply overwrite each other and only
//     the last is heard; this port reproduces that by recording the last
//     request and emitting one tone per tick, rather than firing up to ~50
//     simultaneous Vircon32 voices from `outpt_animBoom()`'s own per-building
//     loop.
//   - The mission-select screen had room for exactly two preview boxes; with
//     four missions it now pages two at a time (`lvl % 2` picks the box,
//     `lvl / 2` the page) and each box is drawn from the tileset its own
//     mission actually uses. Convoi's box additionally shows a real road tile
//     and SearchDoc's a real bunker - both drawn from those missions' own
//     art, no new artwork invented.
//   - Difficulty stays wired exactly as each mission's own upstream shipped
//     it: Desert/Forest gate their mobile enemies on difficulty > 0 and
//     enemy respawning on difficulty == 2 (the root build's own behaviour),
//     while Convoi always has mobile enemies and never respawns them, and
//     SearchDoc always has both - matching those two sketches, which have no
//     difficulty concept of their own at all.
//
// No EEPROM/highscore exists anywhere in this repo (confirmed by grep across
// every sketch) - none was invented.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` call (see gamePong.c's own header comment for why), upstream's
// `typedef struct{...} Name;` blocks became named `struct Name {...}`, and
// every global/function this file introduces carries a `cstr` prefix
// (`gameCopter.c`, an unrelated already-shipped game, already owns `copt`).

// -----------------------------------------------------------------------------
//   Real bitmap art, extracted byte-for-byte from upstream Sprites.ino
//   (repo root) and convoi.ino. bitmap[0]/[1] are the real width/height
//   header bytes; every Arduino `Bxxxxxxxx` binary literal was converted to
//   plain hex by script rather than by hand.
// -----------------------------------------------------------------------------

// gamelogo (72x35)
int cstrGamelogoBitmap[317]= {
    0x48, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0xC0, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7C, 0x02, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xF2, 0x7F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3D, 0xE0, 0x00, 0x00, 0x80, 0x20, 0x00, 0x00, 0x03, 0xDF, 0xDF,
    0x00, 0x01, 0x1B, 0x36, 0xC0, 0x00, 0x1E, 0x02, 0x01, 0xE0, 0x01, 0x2A, 0xA4, 0x80, 0x01, 0xF0,
    0x07, 0x00, 0x3E, 0x00, 0xB3, 0x12, 0x80, 0x07, 0x00, 0x02, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00,
    0x18, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x80, 0x00, 0x00, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x18, 0xC0, 0x00, 0x00, 0x50, 0x10, 0x00, 0x00, 0x00, 0x18, 0xC0, 0x00,
    0x00, 0x9B, 0x55, 0x80, 0x00, 0x00, 0x3F, 0xE0, 0x00, 0x00, 0x52, 0x59, 0x00, 0x00, 0x00, 0xEF,
    0xB8, 0x00, 0x00, 0x8A, 0x54, 0x80, 0x00, 0x01, 0xEF, 0xBC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x80,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xF8, 0x07, 0xFB, 0xEF, 0xF0, 0x00, 0x7F, 0xFF, 0xFF, 0xF0, 0x00,
    0x01, 0xC0, 0x00, 0x00, 0x22, 0x1F, 0xC2, 0x20, 0x00, 0x79, 0x4F, 0x00, 0x00, 0x77, 0x1F, 0xC7,
    0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x72, 0x0F, 0x82, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35,
    0x07, 0x05, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x21, 0x00, 0x02, 0x00, 0x04, 0x00, 0xC5, 0x63, 0x10, 0xA1, 0x44,
    0x44, 0x20, 0xC5, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// HUD (40x4)
int cstrHudBitmap[22]= {
    0x28, 0x04, 0xDB, 0xFF, 0xC5, 0xFF, 0xE0, 0xFA, 0x00, 0x49, 0x00, 0x20, 0x72, 0x00, 0x4D, 0x00,
    0x20, 0x23, 0xFF, 0xC9, 0xFF, 0xE0,
};

// copterShadow (8x5)
int cstrCopterShadowBitmap[7]= {
    0x08, 0x05, 0x00, 0x50, 0xA8, 0x50, 0x00,
};

// copterProfile (16x6)
int cstrCopterProfileBitmap[14]= {
    0x10, 0x06, 0x80, 0x40, 0xC1, 0xF0, 0xBF, 0xE8, 0x47, 0xE4, 0x03, 0x3A, 0x01, 0xFE,
};

// copterProfile_mask (16x6)
int cstrCopterProfileMaskBitmap[14]= {
    0x10, 0x06, 0x80, 0x40, 0xC1, 0xF0, 0xFF, 0xF8, 0x47, 0xFC, 0x03, 0xFE, 0x01, 0xFE,
};

// copterDiag1 (16x14)
int cstrCopterDiag1Bitmap[30]= {
    0x10, 0x0E, 0x80, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0xA0, 0x00, 0x70, 0x00, 0x18, 0x00, 0x0E, 0x00,
    0x0F, 0x00, 0x0F, 0x80, 0xFE, 0x60, 0x7E, 0x70, 0x27, 0x48, 0x13, 0xC0, 0x01, 0xC0,
};

// copterDiag1_mask (16x14)
int cstrCopterDiag1MaskBitmap[30]= {
    0x10, 0x0E, 0x80, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0xE0, 0x00, 0x70, 0x00, 0x18, 0x00, 0x0E, 0x00,
    0x0F, 0x00, 0x0F, 0x80, 0xFF, 0xE0, 0x7F, 0xF0, 0x37, 0xD8, 0x1B, 0xC0, 0x01, 0xC0,
};

// copterDiag2 (16x9)
int cstrCopterDiag2Bitmap[20]= {
    0x10, 0x09, 0x00, 0x78, 0x02, 0xF8, 0x05, 0xF2, 0x03, 0xFC, 0x07, 0xF8, 0x0D, 0xC0, 0x58, 0x00,
    0x68, 0x00, 0x30, 0x00,
};

// copterDiag2_mask (16x9)
int cstrCopterDiag2MaskBitmap[20]= {
    0x10, 0x09, 0x00, 0x78, 0x03, 0xF8, 0x07, 0xFE, 0x03, 0xFC, 0x07, 0xF8, 0x0D, 0xC0, 0x58, 0x00,
    0x78, 0x00, 0x30, 0x00,
};

// copterDown (15x7)
int cstrCopterDownBitmap[16]= {
    0x0F, 0x07, 0x1C, 0x00, 0x22, 0x00, 0x22, 0x00, 0xF7, 0x80, 0xBE, 0x80, 0x1C, 0x00, 0x22, 0x00,
};

// copterUp (15x7)
int cstrCopterUpBitmap[16]= {
    0x0F, 0x07, 0x1C, 0x00, 0x3E, 0x00, 0x36, 0x00, 0xF3, 0x80, 0xBE, 0x80, 0x1C, 0x00, 0x22, 0x00,
};

// copterUpDown_mask (16x7)
int cstrCopterUpDownMaskBitmap[16]= {
    0x10, 0x07, 0x1C, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0xFF, 0x80, 0xBE, 0x80, 0x1C, 0x00, 0x22, 0x00,
};

// Friend_heliport (16x9)
int cstrFriendHeliportBitmap[20]= {
    0x10, 0x09, 0xAA, 0xA8, 0x00, 0x00, 0x8D, 0x88, 0x0D, 0x80, 0x8F, 0x88, 0x0D, 0x80, 0x8D, 0x88,
    0x00, 0x00, 0xAA, 0xA8,
};

// Friend_basecamp (24x15)
int cstrFriendBasecampBitmap[47]= {
    0x18, 0x0F, 0x01, 0xF0, 0x00, 0x03, 0xF8, 0x00, 0x07, 0xFC, 0x00, 0x02, 0x08, 0x00, 0x3E, 0x4F,
    0x80, 0x7E, 0xAF, 0xC0, 0xFC, 0x47, 0xE0, 0x40, 0x00, 0x40, 0x5C, 0xE7, 0x40, 0x54, 0xE5, 0x40,
    0x5C, 0xE7, 0x40, 0x40, 0xE0, 0x40, 0x43, 0xF8, 0x40, 0x7C, 0x07, 0xC0, 0x07, 0xFC, 0x00,
};

// Friend_fuel (32x29)
int cstrFriendFuelBitmap[118]= {
    0x20, 0x1D, 0x00, 0x3F, 0x80, 0x00, 0x00, 0xD5, 0x40, 0x00, 0x1F, 0xAB, 0xE0, 0x00, 0x75, 0xD6,
    0x00, 0x00, 0xAA, 0xBC, 0x00, 0x00, 0xDD, 0x6B, 0x00, 0x00, 0xA7, 0xD5, 0x80, 0x00, 0xCB, 0x7E,
    0xC0, 0x00, 0x95, 0x40, 0x3F, 0xFC, 0x1D, 0x40, 0x40, 0x02, 0x0B, 0x40, 0xFF, 0xFF, 0x0D, 0x40,
    0x90, 0x09, 0x09, 0x40, 0xA5, 0x69, 0x02, 0x80, 0xB5, 0x49, 0x02, 0x80, 0xA6, 0x25, 0x04, 0x80,
    0xFF, 0xFF, 0x07, 0x86, 0x40, 0x02, 0x00, 0x69, 0x4E, 0xFA, 0x00, 0x9F, 0x4E, 0xAA, 0x00, 0xE5,
    0x4E, 0xFA, 0x00, 0xBD, 0x4E, 0x02, 0x60, 0xA5, 0x7F, 0xFE, 0x96, 0x66, 0x00, 0x00, 0xF9, 0x18,
    0x0E, 0x38, 0xA7, 0x00, 0x0B, 0x2C, 0xBD, 0x00, 0x0E, 0xBA, 0xA5, 0x00, 0x0F, 0x3C, 0x66, 0x00,
    0x0E, 0x38, 0x18, 0x00, 0x00, 0x00,
};

// Friend_garage (32x26)
int cstrFriendGarageBitmap[106]= {
    0x20, 0x1A, 0x03, 0xE0, 0x00, 0x00, 0x0C, 0x18, 0x00, 0x00, 0x10, 0x04, 0x00, 0x00, 0x22, 0x02,
    0x00, 0x00, 0x40, 0x11, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x83, 0xE0, 0x80, 0x00, 0xAC, 0x18,
    0x80, 0x00, 0x90, 0x04, 0xBF, 0x80, 0xAF, 0xFA, 0xBD, 0x80, 0xD7, 0xF5, 0xBC, 0x80, 0xD7, 0xF5,
    0xBB, 0x80, 0x97, 0xF4, 0xA7, 0x80, 0x97, 0xF4, 0xB7, 0x80, 0x97, 0xF4, 0xBF, 0x80, 0xFF, 0xFF,
    0xA0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x03, 0x88,
    0x00, 0x00, 0x02, 0x50, 0x00, 0x00, 0x02, 0x60, 0x00, 0x00, 0xFF, 0xBC, 0x70, 0x00, 0xB0, 0x32,
    0xE0, 0x00, 0xC8, 0x4A, 0x70, 0x00, 0xCF, 0xCC, 0xE0, 0x00,
};

// Desert_bush (8x5)
int cstrDesertBushBitmap[7]= {
    0x08, 0x05, 0x25, 0x92, 0x62, 0x3C, 0x10,
};

// Desert_cactus (8x9)
int cstrDesertCactusBitmap[11]= {
    0x08, 0x09, 0x10, 0x18, 0x18, 0x58, 0xDA, 0xFB, 0x7F, 0x1E, 0x18,
};

// Desert_oasis (40x24)
int cstrDesertOasisBitmap[122]= {
    0x28, 0x18, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x03, 0xB8, 0x00, 0x00, 0x00, 0x05, 0x54, 0x00,
    0x00, 0x00, 0x0A, 0xAA, 0x00, 0x00, 0x00, 0x15, 0x55, 0x00, 0x00, 0x00, 0x19, 0xAB, 0x00, 0x00,
    0x00, 0x11, 0x75, 0x00, 0x00, 0x08, 0x01, 0xB3, 0x00, 0x00, 0x0A, 0x01, 0x29, 0x00, 0x00, 0x2C,
    0x40, 0x28, 0x00, 0x00, 0x1C, 0x50, 0x28, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x3F, 0xFE,
    0x30, 0x00, 0x00, 0x55, 0x55, 0xC0, 0xFC, 0x00, 0xAA, 0xAA, 0xA1, 0x42, 0x00, 0xD5, 0x55, 0x62,
    0xA1, 0x00, 0x6A, 0xFA, 0xA4, 0x90, 0x80, 0x1F, 0x07, 0xCC, 0x9F, 0xC0, 0x00, 0x30, 0x17, 0xF4,
    0xA0, 0x01, 0x14, 0x00, 0x00, 0x00, 0x03, 0x9E, 0x00, 0x00, 0x00, 0x01, 0x1E, 0x00, 0x00, 0x00,
    0x02, 0x8A, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
};

// Desert_sand (8x8)
int cstrDesertSandBitmap[10]= {
    0x08, 0x08, 0x02, 0x00, 0x40, 0x04, 0x00, 0x20, 0x04, 0x00,
};

// Desert_tree (8x12)
int cstrDesertTreeBitmap[14]= {
    0x08, 0x0C, 0x04, 0x04, 0x8C, 0xA8, 0xF8, 0x60, 0x68, 0x70, 0x20, 0x60, 0x70, 0xF8,
};

// Desert_rock (14x9)
int cstrDesertRockBitmap[20]= {
    0x0E, 0x09, 0x0C, 0x00, 0x1E, 0x00, 0x17, 0x00, 0x2B, 0x80, 0x37, 0xE0, 0x6B, 0xF0, 0xD5, 0xF8,
    0xAA, 0xFC, 0xFF, 0xFC,
};

// Forest_grass (8x8)
int cstrForestGrassBitmap[10]= {
    0x08, 0x08, 0x05, 0xA2, 0x40, 0x00, 0x0A, 0x04, 0x50, 0x20,
};

// Forest_forest (40x24)
int cstrForestForestBitmap[122]= {
    0x28, 0x18, 0x00, 0x00, 0x00, 0x30, 0x20, 0x20, 0x00, 0x40, 0x58, 0x60, 0x20, 0x00, 0x40, 0x68,
    0xA0, 0x50, 0x00, 0xA0, 0x58, 0xE0, 0xA8, 0x01, 0x50, 0x6C, 0xB0, 0x50, 0x00, 0xA0, 0xD5, 0x50,
    0xF8, 0x01, 0xF0, 0xA9, 0xB0, 0x20, 0x4C, 0x41, 0x59, 0x50, 0x00, 0xEC, 0x01, 0xA9, 0xB0, 0x21,
    0x5C, 0x00, 0xD9, 0x50, 0x32, 0xAC, 0x00, 0xA8, 0xE0, 0x55, 0x13, 0x00, 0x70, 0x40, 0xB6, 0x0C,
    0xC0, 0x20, 0x40, 0xD6, 0x33, 0x20, 0x21, 0x00, 0xB2, 0x30, 0xE1, 0x00, 0x80, 0xDA, 0xCD, 0x61,
    0x42, 0xA0, 0xAA, 0xCD, 0x40, 0x01, 0x40, 0xDA, 0x0C, 0x4F, 0xFD, 0x40, 0xAB, 0xFF, 0xD5, 0x56,
    0x40, 0xD8, 0x00, 0x2A, 0xAB, 0xC0, 0xA8, 0x08, 0x55, 0x55, 0x60, 0x70, 0x0A, 0xAA, 0xAA, 0xB0,
    0x20, 0x2C, 0xD5, 0x57, 0xE0, 0x20, 0x1C, 0x7F, 0xFC, 0x00,
};

// Forest_tree (8x14)
int cstrForestTreeBitmap[16]= {
    0x08, 0x0E, 0x18, 0x2C, 0x34, 0x2C, 0x36, 0x6A, 0x54, 0xAC, 0xD4, 0x6C, 0x54, 0x38, 0x10, 0x10,
};

// Forest_Tree1 (16x17)
int cstrForestTree1Bitmap[36]= {
    0x10, 0x11, 0x0F, 0x00, 0x35, 0xC0, 0x6A, 0xB0, 0xD5, 0x50, 0xAA, 0xA8, 0xD5, 0x54, 0xAA, 0xAC,
    0x55, 0x54, 0xBA, 0xAC, 0xDD, 0xD4, 0x6F, 0xA8, 0x37, 0x50, 0x1F, 0xE0, 0x07, 0x80, 0x07, 0x00,
    0x07, 0x00, 0x0F, 0x80,
};

// Forest_Tree2 (16x21)
int cstrForestTree2Bitmap[44]= {
    0x10, 0x15, 0x1F, 0x00, 0x35, 0xF0, 0x6A, 0xAC, 0x55, 0x56, 0x2A, 0xAA, 0x35, 0x5E, 0x0F, 0xE0,
    0x06, 0x40, 0x03, 0x4E, 0x01, 0xD5, 0x7D, 0xCA, 0xAB, 0xFC, 0x57, 0xE0, 0x39, 0xC0, 0x0D, 0xC0,
    0x07, 0xC0, 0x03, 0xC0, 0x01, 0xC0, 0x01, 0xC0, 0x01, 0xC0, 0x03, 0xE0,
};

// Ennemy_camp (16x22)
int cstrEnnemyCampBitmap[46]= {
    0x10, 0x16, 0x10, 0x10, 0x38, 0x38, 0x54, 0x54, 0x92, 0x92, 0xAA, 0xAA, 0xC6, 0xC6, 0x92, 0x92,
    0x92, 0x92, 0xFE, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x38, 0x38,
    0x54, 0x54, 0x92, 0x92, 0xAA, 0xAA, 0xC6, 0xC6, 0x82, 0x82, 0x82, 0x82, 0xFE, 0xFE,
};

// Ennemy_camp_destr (16x22)
int cstrEnnemyCampDestrBitmap[46]= {
    0x10, 0x16, 0x10, 0x10, 0x10, 0x10, 0x54, 0x10, 0xAA, 0x00, 0x54, 0x00, 0x8A, 0x88, 0x14, 0x44,
    0x92, 0x80, 0x50, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x00,
    0x20, 0x14, 0x10, 0x80, 0x00, 0x00, 0x12, 0x00, 0x8A, 0x12, 0x82, 0x90, 0xEC, 0xFE,
};

// Desert_village (72x41)
int cstrDesertVillageBitmap[371]= {
    0x48, 0x29, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0xFF, 0x03, 0xFF, 0xE7, 0xFC, 0x00,
    0x00, 0x11, 0x11, 0x11, 0x02, 0x00, 0x24, 0x06, 0x00, 0x18, 0x1F, 0xFF, 0xFF, 0x02, 0xFF, 0xA5,
    0xF5, 0x00, 0xEE, 0x11, 0x11, 0x11, 0x02, 0x80, 0xA5, 0x16, 0xC1, 0x55, 0x10, 0xC0, 0x01, 0x02,
    0x80, 0xA5, 0x15, 0x62, 0xAA, 0x90, 0x50, 0x0D, 0x03, 0xFF, 0xE7, 0xFE, 0xA5, 0x55, 0x50, 0x78,
    0x29, 0x02, 0x00, 0x24, 0x05, 0x66, 0xAC, 0xD0, 0x78, 0x79, 0x02, 0xAA, 0xA5, 0x56, 0xA5, 0x74,
    0x50, 0x28, 0x79, 0x02, 0x00, 0x24, 0x05, 0x66, 0x6C, 0x10, 0x28, 0x51, 0x02, 0xCC, 0x25, 0x84,
    0xE5, 0xA4, 0x10, 0x00, 0x51, 0x02, 0xCC, 0x25, 0x84, 0x20, 0xA0, 0x10, 0x00, 0x01, 0x02, 0xC0,
    0x25, 0x84, 0x20, 0xA0, 0x10, 0x33, 0x01, 0x03, 0xFF, 0xE7, 0xFC, 0x20, 0xA0, 0x10, 0xA1, 0x41,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x11, 0xE1, 0xE1, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
    0xE1, 0xE1, 0x1D, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x11, 0x40, 0xA1, 0x2A, 0xB0, 0x00, 0x00, 0x00,
    0x00, 0x11, 0x40, 0xA1, 0x55, 0x50, 0x00, 0x1E, 0x00, 0x00, 0x10, 0x00, 0x01, 0xAA, 0xA8, 0x80,
    0x21, 0x00, 0x00, 0x1F, 0xFF, 0xFF, 0xD5, 0x98, 0x00, 0x5E, 0x80, 0x00, 0x11, 0x11, 0x11, 0xAE,
    0x88, 0x00, 0x5E, 0x80, 0x00, 0x1F, 0xFF, 0xFF, 0xCD, 0x80, 0x00, 0x61, 0x80, 0x00, 0x11, 0x11,
    0x11, 0x94, 0x80, 0x00, 0x5E, 0x80, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x21, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x14, 0xFF, 0xFE, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x80, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0xBF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0,
    0x01, 0x03, 0xFE, 0x03, 0xFE, 0x00, 0xC0, 0x00, 0xA0, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0xB8,
    0x00, 0xFF, 0xFF, 0x02, 0xFB, 0x8E, 0xFA, 0x05, 0x54, 0x00, 0x80, 0x09, 0x02, 0x8B, 0x76, 0x8A,
    0x0A, 0xAA, 0x00, 0xAA, 0xAF, 0x02, 0x8A, 0xAA, 0x8A, 0x15, 0x55, 0x00, 0x80, 0x09, 0x03, 0xFF,
    0x57, 0xFE, 0x19, 0xAB, 0x00, 0xB3, 0x0F, 0x02, 0x02, 0xAA, 0x02, 0x11, 0x75, 0x00, 0xB3, 0x09,
    0x02, 0xAB, 0x56, 0xAA, 0x01, 0xB3, 0x00, 0xB0, 0x0F, 0x02, 0x03, 0xAE, 0x02, 0x01, 0x29, 0x00,
    0xFF, 0xF8, 0x02, 0xC2, 0x72, 0xC2, 0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0xC2, 0x02, 0xC2, 0x00,
    0x28, 0x00, 0x08, 0x00, 0x02, 0xC2, 0x02, 0xC2, 0x00, 0x28, 0x00, 0x00, 0x00, 0x03, 0xFE, 0x03,
    0xFE, 0x00, 0x30,
};

// Desert_village_destr (72x41)
int cstrDesertVillageDestrBitmap[371]= {
    0x48, 0x29, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xF8, 0xFF, 0x00, 0x00, 0x07, 0xC0, 0x00,
    0x00, 0x01, 0x10, 0x11, 0x00, 0x80, 0x04, 0x60, 0x00, 0x00, 0x0F, 0xFC, 0xFF, 0x10, 0x09, 0x85,
    0xE0, 0x00, 0x00, 0x11, 0x10, 0x11, 0x08, 0x81, 0x25, 0x70, 0x00, 0x40, 0x10, 0x00, 0x01, 0x00,
    0x00, 0x05, 0x18, 0x20, 0xE0, 0x12, 0x80, 0x01, 0x30, 0x00, 0x07, 0x8C, 0x20, 0x40, 0x12, 0x80,
    0x28, 0x02, 0x58, 0x64, 0xEC, 0x20, 0xE0, 0x13, 0xC0, 0x28, 0x03, 0xFF, 0xFD, 0x7C, 0x20, 0x61,
    0x13, 0xC0, 0x78, 0x26, 0xC1, 0x64, 0x3C, 0x00, 0x60, 0x11, 0x40, 0x78, 0x66, 0x30, 0x75, 0x8C,
    0x20, 0xE0, 0x10, 0x60, 0x51, 0x0E, 0x98, 0x25, 0x84, 0x20, 0xE4, 0x00, 0x00, 0xC1, 0x42, 0x00,
    0xE5, 0x84, 0x20, 0xE2, 0x10, 0x00, 0x01, 0x07, 0xFF, 0xE7, 0xFC, 0x20, 0xE0, 0x07, 0x82, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x60, 0x11, 0xC3, 0xC1, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x17,
    0x80, 0xF1, 0x00, 0x09, 0x64, 0x00, 0x00, 0x00, 0x11, 0xE1, 0xC1, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x20, 0xF1, 0x0C, 0x02, 0x00, 0x1E, 0x00, 0x00, 0x10, 0x00, 0x01, 0x08, 0x00, 0x80,
    0x21, 0x00, 0x00, 0x0F, 0x9C, 0x3F, 0x0C, 0x00, 0x00, 0x5E, 0x80, 0x00, 0x01, 0x50, 0x11, 0x0C,
    0x00, 0x00, 0x5E, 0x80, 0x03, 0x3F, 0xFE, 0x1F, 0x1C, 0x00, 0x00, 0x61, 0x80, 0x00, 0x11, 0x11,
    0x11, 0x1C, 0x00, 0x00, 0x5E, 0x80, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x21, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1C, 0x1C, 0x1E, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x22, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x5F, 0xFB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA7,
    0xE5, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xA6, 0xE2, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00,
    0x00, 0xFE, 0x3C, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x83, 0x88, 0x01, 0x06, 0x00, 0x66,
    0x10, 0x20, 0x00, 0xA2, 0xA8, 0x01, 0x00, 0x01, 0xEE, 0x30, 0x10, 0x08, 0x80, 0x38, 0x00, 0x00,
    0x03, 0x9E, 0x00, 0x30, 0x00, 0x33, 0x7E, 0x70, 0x00, 0x03, 0x72, 0x00, 0x78, 0x06, 0xBB, 0x79,
    0x50, 0x20, 0x03, 0xCA, 0x00, 0x94, 0x10, 0xB0, 0xFF, 0x49, 0x58, 0x9B, 0x82, 0x61, 0x32, 0x00,
    0xFF, 0xF8, 0x31, 0xEF, 0x02, 0xC2, 0x00, 0x3A, 0x00, 0x00, 0x00, 0x07, 0xD5, 0x82, 0xC2, 0x1C,
    0x38, 0x10, 0x08, 0x00, 0x26, 0xC6, 0x82, 0xC2, 0x06, 0x18, 0x00, 0x00, 0x00, 0x03, 0xFF, 0x03,
    0xFE, 0x00, 0x18,
};

// Forest_Village (72x41)
int cstrForestVillageBitmap[371]= {
    0x48, 0x29, 0x0E, 0x00, 0x00, 0x08, 0x0F, 0xE0, 0x00, 0x00, 0x00, 0x1B, 0xC0, 0x00, 0x1C, 0x15,
    0x50, 0x00, 0x00, 0x00, 0x15, 0x70, 0x00, 0x08, 0x2A, 0xB0, 0x00, 0x09, 0x24, 0x2A, 0xA8, 0x00,
    0x08, 0x35, 0x50, 0x14, 0x04, 0x92, 0x55, 0x58, 0x00, 0x1C, 0x2A, 0xB0, 0x08, 0x09, 0x24, 0x6A,
    0xAC, 0x00, 0x1C, 0x15, 0x50, 0x00, 0x04, 0x92, 0x35, 0x54, 0x28, 0x3E, 0x0F, 0xE0, 0x7E, 0x09,
    0x24, 0x1A, 0xB8, 0x10, 0x3E, 0x03, 0x80, 0xFF, 0x04, 0x92, 0x07, 0xC0, 0x00, 0x7F, 0x03, 0x81,
    0xFF, 0x89, 0x24, 0x03, 0x00, 0x00, 0x7F, 0x03, 0x81, 0xFF, 0x84, 0x92, 0x03, 0x00, 0x00, 0x22,
    0x00, 0x00, 0x81, 0x09, 0x24, 0x00, 0x1E, 0x00, 0x22, 0x00, 0x00, 0xAD, 0x04, 0x92, 0x00, 0x3F,
    0x00, 0x2A, 0x00, 0x00, 0xAD, 0x09, 0x24, 0x00, 0x73, 0x80, 0x2B, 0xFC, 0x00, 0xA1, 0x04, 0x92,
    0x00, 0x6D, 0xE0, 0x23, 0xFE, 0x00, 0xFF, 0x09, 0x24, 0x00, 0x2D, 0xF0, 0x23, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x20, 0x2A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0xA0, 0x2A, 0xDA,
    0x00, 0x1F, 0xC0, 0x00, 0x00, 0x2D, 0xB0, 0x2A, 0xDA, 0x00, 0x3F, 0xE0, 0x00, 0x00, 0x2C, 0x28,
    0x2A, 0x02, 0x00, 0x7F, 0xF0, 0x0A, 0x28, 0x3F, 0xF4, 0x3F, 0xFE, 0x00, 0x7F, 0xF0, 0x04, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0xA0,
    0x00, 0x00, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x2D, 0xBF, 0x80, 0x01, 0xAA, 0x00, 0x03, 0xC0, 0x00,
    0x20, 0x3F, 0xC0, 0x01, 0x55, 0x80, 0x04, 0x20, 0x28, 0x20, 0x20, 0xC0, 0x02, 0xAA, 0x80, 0x0B,
    0xD0, 0x10, 0x2D, 0xAE, 0x80, 0x03, 0x55, 0x40, 0x0B, 0xD0, 0x00, 0x2D, 0xAE, 0x80, 0x02, 0xAA,
    0xC0, 0x0C, 0x30, 0x00, 0x2C, 0x2E, 0x80, 0x03, 0x55, 0x40, 0x0B, 0xD0, 0x00, 0x3F, 0xFF, 0xB8,
    0x02, 0xAA, 0xC0, 0x04, 0x21, 0xFC, 0x00, 0x00, 0x56, 0x01, 0x55, 0x80, 0x03, 0xC3, 0xFE, 0x00,
    0x01, 0xAB, 0x00, 0xFF, 0x01, 0x40, 0x07, 0xFF, 0x00, 0x01, 0x55, 0x00, 0x3C, 0x00, 0x80, 0x07,
    0xFF, 0x00, 0x02, 0xAB, 0x00, 0x3C, 0x00, 0x00, 0x02, 0x02, 0x00, 0x03, 0x55, 0x00, 0x3C, 0x00,
    0x00, 0x02, 0xBA, 0x00, 0x02, 0xAB, 0x00, 0x3E, 0x00, 0x00, 0x02, 0xBA, 0x00, 0x01, 0x56, 0x14,
    0x55, 0x00, 0x00, 0x02, 0x82, 0x00, 0x14, 0xFC, 0x08, 0x00, 0x00, 0x00, 0x03, 0xFE, 0x00, 0x08,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

// Forest_Village_destr (72x41)
int cstrForestVillageDestrBitmap[371]= {
    0x48, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x05,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x18, 0xB0, 0x00, 0x09, 0x24, 0x04, 0x20, 0x00,
    0x00, 0x08, 0x60, 0x14, 0x04, 0x92, 0x04, 0x14, 0x00, 0x00, 0x06, 0x40, 0x08, 0x09, 0x24, 0x26,
    0x78, 0x00, 0x00, 0x02, 0xC0, 0x00, 0x05, 0x96, 0x3B, 0xC0, 0x28, 0x00, 0x03, 0x80, 0x40, 0x09,
    0x2C, 0x0F, 0x80, 0x10, 0x00, 0x03, 0x80, 0x44, 0x04, 0x92, 0x03, 0x00, 0x00, 0x00, 0x03, 0x81,
    0x55, 0x09, 0xFC, 0x03, 0x00, 0x00, 0x00, 0x03, 0x80, 0xFF, 0x8D, 0xFA, 0x03, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x81, 0x09, 0x24, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0C, 0xAD, 0x04, 0x9E, 0x00, 0x00,
    0x00, 0x0A, 0x00, 0x02, 0xAD, 0x0F, 0x26, 0x00, 0x02, 0x10, 0x1A, 0xA8, 0x00, 0xA2, 0x6C, 0x92,
    0x00, 0x41, 0x20, 0x12, 0xAA, 0x10, 0xFC, 0x09, 0x24, 0x00, 0x19, 0x58, 0x23, 0xFF, 0x06, 0x02,
    0x00, 0x00, 0x09, 0x06, 0x00, 0x2A, 0x02, 0x24, 0x80, 0x00, 0x00, 0x00, 0xBF, 0xE4, 0x6A, 0xDA,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x20, 0xEA, 0xDA, 0x00, 0x00, 0x00, 0x00, 0x01, 0xA4, 0x31,
    0xAA, 0x02, 0x80, 0x00, 0x00, 0x0A, 0x28, 0x3F, 0xEA, 0xFF, 0xFE, 0x40, 0x00, 0x00, 0x04, 0x10,
    0x61, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8C, 0x46, 0x14, 0x04, 0x00, 0x20, 0x60,
    0x00, 0x00, 0x05, 0x10, 0x90, 0x02, 0x00, 0x32, 0x68, 0x00, 0x00, 0x10, 0x81, 0xE3, 0xC0, 0x00,
    0x2D, 0xFC, 0x00, 0x00, 0x10, 0x00, 0x84, 0x20, 0x28, 0x20, 0x22, 0x08, 0x03, 0x30, 0x80, 0x0B,
    0xD0, 0x10, 0x2D, 0xAF, 0x10, 0x01, 0xE4, 0x80, 0x0B, 0xD0, 0x00, 0x2D, 0xAE, 0x80, 0x01, 0x87,
    0x80, 0x0C, 0x30, 0x00, 0x2C, 0x2E, 0x9C, 0x01, 0xC3, 0x00, 0x0B, 0xD0, 0x01, 0xBF, 0xFF, 0x80,
    0x00, 0xE6, 0x00, 0x04, 0x21, 0x41, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x03, 0xC3, 0xC0, 0x00,
    0x80, 0x58, 0x00, 0x3C, 0x01, 0x40, 0x07, 0xE0, 0x06, 0x60, 0x8A, 0x00, 0x3C, 0x00, 0x80, 0x07,
    0xF0, 0x48, 0x02, 0x86, 0x00, 0x3C, 0x00, 0x00, 0x02, 0x08, 0x80, 0x01, 0x84, 0x00, 0x3C, 0x00,
    0x00, 0x02, 0x9E, 0x00, 0x00, 0xCC, 0x00, 0x3E, 0x00, 0x00, 0x01, 0xAA, 0x00, 0x00, 0x68, 0x14,
    0x55, 0x00, 0x00, 0x22, 0x82, 0x80, 0x14, 0x38, 0x08, 0x00, 0x00, 0x00, 0x73, 0xFE, 0x20, 0x08,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

// Ennemy_bunker (32x32)
int cstrEnnemyBunkerBitmap[130]= {
    0x20, 0x20, 0x08, 0x00, 0x00, 0x20, 0x3E, 0x00, 0x00, 0xF8, 0x49, 0x00, 0x01, 0x24, 0x41, 0xFF,
    0xFF, 0x04, 0xE1, 0x55, 0x55, 0x0E, 0x41, 0xAA, 0xAB, 0x04, 0x41, 0x55, 0x55, 0x04, 0x7E, 0xFF,
    0xFE, 0xFC, 0x55, 0x80, 0x03, 0x54, 0x5A, 0x91, 0x12, 0xB4, 0x55, 0x91, 0x13, 0x54, 0x5A, 0x91,
    0x12, 0xB4, 0x55, 0x80, 0x03, 0x54, 0x7A, 0xFF, 0xFE, 0xBC, 0x15, 0x80, 0x03, 0x50, 0x1A, 0x80,
    0x02, 0xB0, 0x15, 0x80, 0x03, 0x50, 0x1A, 0x80, 0x02, 0xB0, 0x15, 0x80, 0x03, 0x50, 0x3E, 0xFF,
    0xFE, 0xF8, 0x41, 0x55, 0x55, 0x04, 0x41, 0xAA, 0xAB, 0x04, 0xE1, 0x55, 0x55, 0x0E, 0x41, 0xFF,
    0xFF, 0x04, 0x49, 0x00, 0x01, 0x24, 0x7F, 0x0F, 0xE1, 0xFC, 0x49, 0x17, 0xD1, 0x24, 0x41, 0x17,
    0xD1, 0x04, 0x49, 0x17, 0xD1, 0x24, 0x49, 0xFF, 0xFF, 0x24, 0x41, 0x00, 0x01, 0x04, 0x7F, 0x00,
    0x01, 0xFC,
};

// Ennemy_bunker_destr (32x32)
int cstrEnnemyBunkerDestrBitmap[130]= {
    0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x50, 0x09, 0x00, 0x02, 0x12, 0x05, 0x5C,
    0x01, 0x00, 0x09, 0x14, 0x00, 0x00, 0x71, 0xAA, 0x40, 0x0C, 0x41, 0x55, 0x0A, 0x20, 0x7E, 0xFF,
    0x81, 0x04, 0x55, 0x80, 0x40, 0x12, 0x50, 0x91, 0x20, 0x00, 0x51, 0x95, 0x21, 0xE4, 0x4A, 0x91,
    0x1A, 0xB8, 0x4D, 0x80, 0x0D, 0x50, 0x6E, 0xFF, 0xFA, 0xBE, 0x16, 0x01, 0x87, 0x56, 0x5A, 0x03,
    0x62, 0xBC, 0x97, 0x01, 0xC1, 0x50, 0x1A, 0x81, 0x3A, 0xB0, 0x15, 0x80, 0x03, 0x50, 0x3E, 0xFE,
    0x0E, 0xF8, 0x01, 0x55, 0xF5, 0x04, 0x01, 0xAA, 0xAB, 0x24, 0x0F, 0x57, 0xD5, 0x14, 0x35, 0xF7,
    0xFF, 0x28, 0x6B, 0x1F, 0x81, 0x48, 0xD5, 0x19, 0xE1, 0xE4, 0xAB, 0x3F, 0xF1, 0x5C, 0xD5, 0x63,
    0xD1, 0x24, 0x79, 0x1F, 0xD1, 0x44, 0x49, 0xF7, 0xFF, 0x24, 0xC1, 0x14, 0x01, 0x14, 0x7F, 0x0C,
    0x01, 0xFC,
};

// Ennemy_tour (8x10)
int cstrEnnemyTourBitmap[12]= {
    0x08, 0x0A, 0x7C, 0xFE, 0x44, 0x7C, 0x7C, 0x44, 0x6C, 0x54, 0x6C, 0x44,
};

// Ennemy_tour_destr (8x10)
int cstrEnnemyTourDestrBitmap[12]= {
    0x08, 0x0A, 0x00, 0x00, 0x00, 0x20, 0x70, 0x44, 0x68, 0xD4, 0x7A, 0xFE,
};

// Ennemy_Unit (6x6)
int cstrEnnemyUnitBitmap[8]= {
    0x06, 0x06, 0xE0, 0xE0, 0x5C, 0xF0, 0xE0, 0xA0,
};

// Ennemy_Tank (16x14)
int cstrEnnemyTankBitmap[30]= {
    0x10, 0x0E, 0x08, 0x00, 0x08, 0x00, 0x09, 0x80, 0x06, 0x7F, 0x08, 0x20, 0x38, 0x3C, 0x3F, 0xFE,
    0x3F, 0xFE, 0x3F, 0xFE, 0x20, 0x02, 0x15, 0x54, 0x0F, 0xF8, 0x00, 0x00, 0x00, 0x00,
};

// Ennemy_Tank_diagdown (16x14)
int cstrEnnemyTankDiagdownBitmap[30]= {
    0x10, 0x0E, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0xE0, 0xFE, 0x10, 0xFC, 0x08, 0xBC, 0x68,
    0x9E, 0x38, 0xCF, 0xEC, 0x67, 0xF6, 0x33, 0xFB, 0x19, 0xFF, 0x0F, 0xFF, 0x03, 0x8E,
};

// Ennemy_Tank_Down (16x14)
int cstrEnnemyTankDownBitmap[30]= {
    0x10, 0x0E, 0x02, 0x00, 0x3F, 0xF8, 0x3D, 0xF8, 0x3C, 0x78, 0x38, 0x38, 0x38, 0x38, 0x39, 0x38,
    0x3D, 0x78, 0x3E, 0xF8, 0x3E, 0xF8, 0x3E, 0xF8, 0x3E, 0xF8, 0x27, 0xC8, 0x3C, 0x78,
};

// Ennemy_Tank_Up (16x14)
int cstrEnnemyTankUpBitmap[30]= {
    0x10, 0x0E, 0x00, 0x80, 0x00, 0x80, 0x0F, 0xF8, 0x1F, 0x7C, 0x1F, 0x7C, 0x1E, 0x3C, 0x1C, 0x5C,
    0x1C, 0x5C, 0x1E, 0x3C, 0x1F, 0xFC, 0x1F, 0xFC, 0x1F, 0xFC, 0x13, 0xE4, 0x1E, 0x3C,
};

// Ennemy_Tank_diagup (16x14)
int cstrEnnemyTankDiagupBitmap[30]= {
    0x10, 0x0E, 0x02, 0x18, 0x02, 0x30, 0x02, 0x60, 0x07, 0xC0, 0x0A, 0x7E, 0x16, 0x3F, 0x10, 0x3D,
    0x10, 0x79, 0x3F, 0xF3, 0x7F, 0xE6, 0xFF, 0xCC, 0xFF, 0x98, 0xFF, 0xF0, 0x71, 0xC0,
};

int cstrHelix0Bitmap[8]= {
    0x0F, 0x03, 0x00, 0x70, 0x03, 0x80, 0x1C, 0x00,
};
int cstrHelix1Bitmap[8]= {
    0x0F, 0x03, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00,
};
int cstrHelix2Bitmap[8]= {
    0x0F, 0x03, 0x1C, 0x00, 0x03, 0x80, 0x00, 0x70,
};
int cstrHelix3Bitmap[8]= {
    0x0F, 0x03, 0x01, 0x00, 0x03, 0x80, 0x01, 0x00,
};
int* cstrHelix[4]= { cstrHelix0Bitmap, cstrHelix1Bitmap, cstrHelix2Bitmap, cstrHelix3Bitmap };

int cstrImpact0Bitmap[10]= {
    0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x1C, 0x00,
};
int cstrImpact1Bitmap[10]= {
    0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08, 0x22, 0x14, 0x00,
};
int cstrImpact2Bitmap[10]= {
    0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x22, 0x08, 0x44, 0x00,
};
int* cstrImpact[3]= { cstrImpact0Bitmap, cstrImpact1Bitmap, cstrImpact2Bitmap };

int cstrBoom0Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x2A,
};
int cstrBoom1Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x3E, 0x3E,
};
int cstrBoom2Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x7F, 0x7F, 0x7F,
};
int cstrBoom3Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x7F, 0x7F, 0x7F, 0x7F, 0x1C,
};
int cstrBoom4Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x00, 0x00, 0x3E, 0x7F, 0x7F, 0x7F, 0x7F, 0x1C, 0x1C, 0x3E,
};
int cstrBoom5Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x3E, 0x7F, 0x7F, 0x7F, 0x7F, 0x1C, 0x08, 0x08, 0x1C, 0x3E,
};
int cstrBoom6Bitmap[13]= {
    0x08, 0x0B, 0x3E, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x08, 0x1C,
};
int cstrBoom7Bitmap[13]= {
    0x08, 0x0B, 0x1C, 0x3E, 0x7F, 0x7F, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
};
int cstrBoom8Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x1C, 0x3E, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
int cstrBoom9Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x08, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
int cstrBoom10Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
int cstrBoom11Bitmap[13]= {
    0x08, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
int* cstrBoom[12]= { cstrBoom0Bitmap, cstrBoom1Bitmap, cstrBoom2Bitmap, cstrBoom3Bitmap, cstrBoom4Bitmap, cstrBoom5Bitmap, cstrBoom6Bitmap, cstrBoom7Bitmap, cstrBoom8Bitmap, cstrBoom9Bitmap, cstrBoom10Bitmap, cstrBoom11Bitmap };

// FinalScreen (88x48)
int cstrFinalScreenBitmap[530]= {
    0x58, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x00, 0x00,
    0x08, 0x40, 0x00, 0x00, 0x40, 0x08, 0x00, 0x00, 0x24, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x26, 0x23, 0x6A, 0x6A, 0x59, 0x91, 0xAB, 0x59, 0x18, 0x00, 0x00, 0x25, 0x52,
    0x4A, 0x8C, 0x51, 0x2A, 0x2A, 0x52, 0xA8, 0x00, 0x00, 0x25, 0x61, 0x46, 0x6A, 0x73, 0x31, 0x9A,
    0x73, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x48, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x40, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0xDC, 0xCB, 0xB1, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x25, 0x55, 0x4A, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1D, 0xDC, 0xCB, 0xB1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xFF, 0x80, 0x1F, 0xFF, 0xF0, 0x00, 0x00, 0x00,
    0x03, 0xFF, 0xFF, 0xEF, 0xE0, 0x75, 0x7F, 0xF0, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xF5, 0x7F,
    0xEA, 0xBF, 0xF0, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x6A, 0xAA, 0xB5, 0x78, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x80, 0x00, 0x55, 0xFD, 0xFE, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x03, 0x9F, 0xFE, 0x6A,
    0xAA, 0xAB, 0x79, 0xF0, 0x00, 0x00, 0x00, 0x03, 0x9F, 0xFE, 0x57, 0xFF, 0xFF, 0xF9, 0xF0, 0x00,
    0x00, 0x00, 0x03, 0x9F, 0xFE, 0x6A, 0xAA, 0xAA, 0xB9, 0xF0, 0x00, 0x00, 0x00, 0x03, 0x9C, 0x0E,
    0x55, 0x55, 0xFB, 0x79, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x9C, 0x0E, 0x6A, 0xFA, 0xA6, 0xB9, 0xC0,
    0x00, 0x00, 0x00, 0x03, 0x9C, 0x0E, 0x55, 0x2F, 0x5D, 0x79, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x9C,
    0x0E, 0x7F, 0xD5, 0xBF, 0xF9, 0xC0, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xC0, 0x7F, 0xE0, 0x3F,
    0xF0, 0x03, 0x11, 0xC8, 0x03, 0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x3F, 0xF0, 0x22, 0xA8, 0x94, 0x03,
    0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x3F, 0xF0, 0x53, 0x90, 0x88, 0x03, 0x80, 0x00, 0x40, 0x00, 0x00,
    0x38, 0x00, 0x50, 0x00, 0x00, 0x03, 0x80, 0x00, 0x40, 0x1F, 0x00, 0x38, 0x00, 0x00, 0x31, 0x1B,
    0x83, 0x98, 0xC6, 0x40, 0xFF, 0xE0, 0x39, 0x80, 0x70, 0x3A, 0x93, 0x03, 0x98, 0xC6, 0x40, 0x80,
    0x20, 0x0F, 0xC0, 0x00, 0x32, 0xB3, 0x83, 0x80, 0x00, 0x40, 0x80, 0x20, 0x0F, 0xC0, 0x00, 0x00,
    0x00, 0x03, 0x80, 0x00, 0x40, 0xFF, 0xE0, 0x3C, 0xF0, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x41,
    0xFF, 0xF0, 0x3C, 0xF0, 0x00, 0x00, 0x00, 0x03, 0x9F, 0x00, 0x41, 0x20, 0x90, 0x33, 0x30, 0x33,
    0x39, 0x93, 0x3B, 0x9F, 0x00, 0x41, 0x20, 0x90, 0x33, 0x30, 0x43, 0xB1, 0x2B, 0x93, 0x9F, 0x00,
    0x41, 0x20, 0x90, 0x30, 0x30, 0x32, 0xBB, 0x2A, 0x93, 0x9F, 0x00, 0x41, 0x20, 0x90, 0x30, 0x30,
    0x00, 0x00, 0x00, 0x03, 0x97, 0x00, 0x41, 0x20, 0x90, 0x33, 0x30, 0x00, 0x00, 0x00, 0x03, 0x9F,
    0x00, 0x41, 0xE0, 0xF0, 0x33, 0x30, 0x00, 0x00, 0x00, 0x03, 0x9F, 0x00, 0x41, 0x20, 0x90, 0x3F,
    0xF0, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xC1, 0xFF, 0xF0, 0x3F, 0xF0, 0x00, 0x00, 0x00, 0x03,
    0xFF, 0xFF, 0xC0, 0xC0, 0x60, 0x0F, 0xC0, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xC0, 0xC0, 0x60,
    0x0F, 0xC0,
};

// ex1 (32x32)
int cstrEx1Bitmap[130]= {
    0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0xFF, 0xE0, 0x00, 0x01, 0xFF,
    0xF0, 0x00, 0xDF, 0xFF, 0xFF, 0x64, 0x7F, 0xFF, 0xFF, 0xFC, 0x07, 0xFF, 0xBE, 0x00, 0x0F, 0xFF,
    0xBE, 0x00, 0x0F, 0xFE, 0x07, 0x00, 0x0F, 0xFE, 0x07, 0xC0, 0x5F, 0xF5, 0x4F, 0xE4, 0x2F, 0x88,
    0x3B, 0xE8, 0x5F, 0x81, 0x4B, 0xE0, 0x0F, 0x28, 0x07, 0xE8, 0x8F, 0x00, 0x81, 0xE0, 0x06, 0x00,
    0x00, 0xE0, 0x02, 0x40, 0x01, 0xC0, 0x00, 0x38, 0xFB, 0x80, 0x3F, 0xDF, 0x3F, 0xDC, 0xED, 0xBF,
    0xED, 0xBC,
};

// ex2 (32x32)
int cstrEx2Bitmap[130]= {
    0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x01, 0xFF, 0xC0, 0x00, 0x01, 0xFF, 0xF0, 0x00, 0x03, 0xFF,
    0xFC, 0x00, 0x01, 0xFC, 0xFF, 0x80, 0x00, 0x7C, 0x9F, 0xE0, 0x00, 0x79, 0xC7, 0xF0, 0x00, 0x09,
    0x0F, 0xF0, 0xEF, 0xBE, 0x04, 0x38, 0x3D, 0xFE, 0x0C, 0x3C, 0x00, 0x3F, 0xBF, 0xF8, 0x71, 0xBF,
    0xFF, 0xF8, 0x41, 0xFF, 0xFF, 0xF0, 0x03, 0xFF, 0xFF, 0xE0, 0xA7, 0xFB, 0xFF, 0xF0, 0x13, 0xF5,
    0xFF, 0xC4, 0xA5, 0xE0, 0xF5, 0xC0, 0x01, 0xF4, 0x03, 0x94, 0x59, 0x80, 0x41, 0x00, 0x0C, 0x80,
    0x04, 0x00, 0x00, 0xC0, 0x1C, 0x00, 0x00, 0x1E, 0xC0, 0x00, 0x9F, 0xEF, 0x9F, 0xEC, 0xF6, 0xDF,
    0xF6, 0xDC,
};

// ex3 (32x32)
int cstrEx3Bitmap[130]= {
    0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x80, 0x00, 0x08, 0x81, 0x00, 0x00, 0x2D, 0x81, 0x00, 0x01, 0x7F, 0xC1, 0x00, 0x01, 0xFF,
    0xC3, 0x20, 0x03, 0xFF, 0xE3, 0x20, 0x1D, 0x7F, 0xFF, 0x60, 0x3F, 0x1E, 0x7F, 0xF0, 0x3F, 0x1E,
    0x3F, 0xF8, 0x73, 0x1F, 0xBF, 0xFC, 0x61, 0x0F, 0x9F, 0xFC, 0x32, 0x6F, 0x37, 0xFC, 0x22, 0x7F,
    0xDF, 0xFC, 0xFE, 0xFF, 0xD7, 0xFC, 0x33, 0xFF, 0xE7, 0xF8, 0x63, 0xF8, 0xFF, 0xFC, 0x43, 0xF8,
    0xFF, 0xFC, 0x41, 0xF0, 0xFF, 0xFC, 0x61, 0xF0, 0x7F, 0xF8, 0xA1, 0xF0, 0xFE, 0x78, 0x30, 0xE0,
    0x7D, 0xF4, 0xBC, 0xE0, 0x34, 0xE0, 0x0E, 0xC0, 0x01, 0xD4, 0x43, 0x80, 0x01, 0x80, 0x00, 0x80,
    0x01, 0x00, 0x00, 0xE0, 0x06, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x9F, 0xEF, 0x9F, 0xEC, 0xF6, 0xDF,
    0xF6, 0xDC,
};

// ex4 (32x32)
int cstrEx4Bitmap[130]= {
    0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x09,
    0x01, 0x00, 0x00, 0x19, 0x81, 0x00, 0x00, 0x3F, 0x83, 0x10, 0x01, 0xFF, 0xC3, 0x20, 0x07, 0xFF,
    0xE7, 0x60, 0x0F, 0xFF, 0x7D, 0xC0, 0x3D, 0x7F, 0xBF, 0xE0, 0x7F, 0x3F, 0x3F, 0xF8, 0x7F, 0xBF,
    0xFF, 0xF8, 0x41, 0xBF, 0xFF, 0xFC, 0x61, 0x3B, 0xFB, 0xFC, 0x73, 0x71, 0xCF, 0x9C, 0x7E, 0x70,
    0xE3, 0xFC, 0xC3, 0xF0, 0xC7, 0xEC, 0xC3, 0xF0, 0xEF, 0xFC, 0xC7, 0xE0, 0xFF, 0xFC, 0xC3, 0xC0,
    0x7F, 0xEC, 0x73, 0xC0, 0xEF, 0xFC, 0x73, 0xC0, 0x7F, 0xF8, 0xB1, 0xC0, 0xFF, 0xF8, 0x3D, 0xC0,
    0x7F, 0xD4, 0xEC, 0xC0, 0x01, 0xD0, 0x6E, 0xC0, 0x03, 0xF4, 0x7F, 0x00, 0x03, 0xC0, 0x7F, 0x80,
    0x03, 0xA0, 0x3F, 0xC0, 0x0F, 0x40, 0x1F, 0xF0, 0x7F, 0x40, 0x9F, 0xFC, 0xFF, 0x6C, 0xFE, 0xFF,
    0xD7, 0x7C,
};

// ex5 (32x32)
int cstrEx5Bitmap[130]= {
    0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x19,
    0x01, 0x10, 0x00, 0x3E, 0x83, 0x20, 0x02, 0xD7, 0xC2, 0x20, 0x07, 0x7D, 0x66, 0x40, 0x1F, 0xEB,
    0xFC, 0xC0, 0x7F, 0xFB, 0xFF, 0xE0, 0x7F, 0xFF, 0xFF, 0x30, 0xFF, 0xFD, 0xFF, 0xA8, 0xFB, 0xFE,
    0x7D, 0xE8, 0xFF, 0xFE, 0xFF, 0x84, 0x7F, 0xFF, 0xFF, 0xAC, 0xFF, 0xFF, 0xFE, 0xEC, 0xFF, 0xFB,
    0x7F, 0xFC, 0xDF, 0xFB, 0xFF, 0xFC, 0xFF, 0x73, 0xFD, 0xBC, 0xFF, 0x73, 0x9E, 0x7C, 0xFD, 0xF3,
    0xDE, 0xFC, 0x7E, 0x63, 0xBD, 0x1C, 0xFF, 0xF3, 0xEF, 0x98, 0xFF, 0xF1, 0xFF, 0x78, 0xFF, 0xC1,
    0xFE, 0xFC, 0xFF, 0xE0, 0xFF, 0xD0, 0x7F, 0xC0, 0x7F, 0xF4, 0x6F, 0x80, 0x0E, 0xE0, 0x3F, 0xC0,
    0x1F, 0xC0, 0x17, 0xE0, 0xBF, 0x40, 0x0B, 0xF5, 0xFD, 0xC0, 0x9C, 0xFF, 0xFF, 0xEC, 0xFF, 0x9F,
    0x36, 0xFC,
};

// ex6 (32x32)
int cstrEx6Bitmap[130]= {
    0x20, 0x20, 0xFF, 0xFF, 0xFF, 0xFC, 0xFF, 0xFF, 0xFF, 0xFC, 0xFF, 0xF9, 0xFF, 0xFC, 0xFF, 0xF9,
    0xFF, 0xFC, 0xFF, 0xFE, 0xFF, 0xFC, 0xED, 0xFB, 0xFF, 0xFC, 0xFF, 0xFE, 0xF7, 0xFC, 0xFF, 0xFD,
    0xEB, 0xFC, 0xFF, 0xFF, 0xF5, 0xDC, 0x7F, 0xFB, 0xFE, 0xCC, 0x7F, 0xFF, 0xFD, 0xCC, 0xFD, 0xFB,
    0xFE, 0x9C, 0xFF, 0xFF, 0xFF, 0x94, 0xFF, 0xFF, 0xFE, 0x3C, 0xFF, 0xFF, 0xFF, 0x4C, 0x7F, 0xFF,
    0x9F, 0xB4, 0x7F, 0xFF, 0xFE, 0xB8, 0xFA, 0xFF, 0xFF, 0xF8, 0xFE, 0xFF, 0xFB, 0x68, 0xFF, 0xBF,
    0x66, 0xD4, 0xFF, 0xBF, 0xBF, 0x6C, 0xFD, 0xFF, 0xBC, 0xFC, 0xFD, 0xBF, 0xDF, 0xF4, 0xEE, 0xFF,
    0xFE, 0x7C, 0xEE, 0xFF, 0xFF, 0xFC, 0xFF, 0xFF, 0xFE, 0xFC, 0xFF, 0xFF, 0xFF, 0xFC, 0xFD, 0x7F,
    0xFF, 0xFC, 0xFA, 0xFF, 0xCF, 0xFC, 0xFF, 0x6F, 0xFF, 0xFC, 0xFB, 0x6F, 0xFF, 0xFC, 0xFE, 0x3F,
    0xFF, 0xBC,
};

// ex7 (32x32)
int cstrEx7Bitmap[130]= {
    0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x96, 0x7E, 0x80, 0x0E, 0x1F,
    0xFF, 0xC0, 0x0D, 0xDD, 0xFE, 0xC0, 0x0F, 0x6F, 0xFF, 0xC0, 0x1E, 0xFF, 0xDF, 0x80, 0x0E, 0x6B,
    0x87, 0x80, 0x0F, 0xEC, 0xC0, 0x00, 0x06, 0xDB, 0xD0, 0x00, 0x00, 0x08, 0x40, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x10, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF7, 0xD9, 0xEF, 0xB0, 0x98, 0x37, 0x3D, 0xFC, 0x09, 0x48, 0x00, 0x00, 0x19, 0x46,
    0x00, 0x00, 0x21, 0x22, 0x00, 0x00, 0x42, 0x20, 0x00, 0x80, 0xC4, 0x13, 0xA4, 0x10, 0x84, 0x8A,
    0x11, 0x44, 0xC8, 0xC8, 0xA4, 0x00, 0x84, 0x12, 0x00, 0x14, 0x62, 0x11, 0x40, 0x00, 0x62, 0x22,
    0x00, 0x00, 0x21, 0x42, 0x00, 0x00, 0x30, 0x94, 0x00, 0x00, 0xC8, 0x97, 0x9F, 0xEC, 0xFB, 0x6F,
    0xF6, 0xDC,
};

// camionB (16x14)
int cstrCamionBBitmap[30]= {
    0x10, 0x0E, 0x07, 0xE0, 0x0A, 0x50, 0x0E, 0x70, 0x04, 0x20, 0x04, 0x20, 0x04, 0x20, 0x07, 0xE0,
    0x07, 0xE0, 0x07, 0xE0, 0x04, 0x20, 0x07, 0xE0, 0x0F, 0xF0, 0x0B, 0xD0, 0x07, 0xE0,
};

// camionBD (16x15)
int cstrCamionBDBitmap[32]= {
    0x10, 0x0F, 0x0C, 0x00, 0x0F, 0x00, 0x1F, 0xC0, 0x27, 0xF0, 0xE3, 0xF8, 0xF0, 0xCE, 0xFC, 0x9B,
    0xFF, 0xF1, 0xBF, 0x23, 0x9F, 0xEF, 0x7F, 0xF9, 0x1F, 0xE3, 0x07, 0x7E, 0x01, 0x3C, 0x00, 0xF0,
};

// camionBG (16x15)
int cstrCamionBGBitmap[32]= {
    0x10, 0x0F, 0x00, 0x30, 0x00, 0xF0, 0x03, 0xFD, 0x0F, 0xE3, 0x1F, 0xCF, 0x73, 0x1F, 0xD9, 0xBF,
    0x8F, 0xFF, 0xC4, 0xFD, 0xF7, 0xF9, 0x9F, 0xFE, 0xC7, 0xF8, 0x7E, 0xE0, 0x3C, 0x80, 0x0F, 0x00,
};

// camionD (16x14)
int cstrCamionDBitmap[30]= {
    0x10, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0xFF, 0xC8, 0xFF, 0xC8,
    0xFF, 0xFC, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0x50, 0x28, 0x20, 0x10, 0x00, 0x00,
};

// camionG (16x14)
int cstrCamionGBitmap[30]= {
    0x10, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x13, 0xFF, 0x13, 0xFF,
    0x3F, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x14, 0x0A, 0x08, 0x04, 0x00, 0x00,
};

// camionH (16x14)
int cstrCamionHBitmap[30]= {
    0x10, 0x0E, 0x00, 0x00, 0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0, 0x04, 0x20, 0x0F, 0xF0, 0x0C, 0x30,
    0x0C, 0x30, 0x0C, 0x30, 0x0C, 0x30, 0x0C, 0x30, 0x0B, 0xD0, 0x07, 0xE0, 0x02, 0x40,
};

// camionHD (16x14)
int cstrCamionHDBitmap[30]= {
    0x10, 0x0E, 0x00, 0xF0, 0x01, 0xFC, 0x03, 0x7E, 0x0F, 0x3A, 0x1F, 0x97, 0x7E, 0x7F, 0x58, 0x7E,
    0xD1, 0xFC, 0xE7, 0xF8, 0xCB, 0xE8, 0xEB, 0xB0, 0x7F, 0x80, 0x1D, 0x00, 0x0E, 0x00,
};

// camionHG (16x14)
int cstrCamionHGBitmap[30]= {
    0x10, 0x0E, 0x0F, 0x00, 0x3F, 0x80, 0x7E, 0xC0, 0x5C, 0xF0, 0xE9, 0xF8, 0xFE, 0x7E, 0x7E, 0x1A,
    0x3F, 0x8B, 0x1F, 0xE7, 0x17, 0xD3, 0x0D, 0xD7, 0x01, 0xFE, 0x00, 0xB8, 0x00, 0x70,
};

// roudeH (16x16)
int cstrRouteHBitmap[34]= {
    0x10, 0x10, 0xF7, 0xD9, 0x9E, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x52, 0x09,
    0x08, 0xA2, 0x52, 0x00, 0x00, 0x0A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCF, 0xF7,
    0xFB, 0x6F,
};

// roudeV (16x16)
int cstrRouteVBitmap[34]= {
    0x10, 0x10, 0xC0, 0x03, 0xC1, 0x41, 0x84, 0x01, 0x80, 0x43, 0xC1, 0x02, 0x40, 0x83, 0xC1, 0x43,
    0xC0, 0x01, 0x45, 0x03, 0xC0, 0xA3, 0xC0, 0x82, 0x40, 0x03, 0x81, 0x43, 0xC2, 0x02, 0xC2, 0x82,
    0xC0, 0x03,
};

// virage1 (16x16)
int cstrVirage1Bitmap[34]= {
    0x10, 0x10, 0xEE, 0x04, 0xFF, 0x80, 0x07, 0xA0, 0x01, 0xE2, 0x00, 0x78, 0x00, 0x3C, 0xC0, 0x1C,
    0x51, 0x0E, 0x04, 0x0E, 0x0A, 0x84, 0x00, 0x06, 0x05, 0x07, 0x00, 0x03, 0x01, 0x43, 0xC0, 0x83,
    0xC1, 0x03,
};

// virage2 (16x16)
int cstrVirage2Bitmap[34]= {
    0x10, 0x10, 0x20, 0x77, 0x01, 0xFF, 0x05, 0xE0, 0x47, 0x80, 0x1E, 0x00, 0x3C, 0x08, 0x38, 0x02,
    0x70, 0x8A, 0x70, 0x20, 0x21, 0x10, 0x60, 0x08, 0xE0, 0xA0, 0xC0, 0x00, 0xC2, 0x80, 0xC0, 0x03,
    0xC0, 0x83,
};

// virage3 (16x16)
int cstrVirage3Bitmap[34]= {
    0x10, 0x10, 0xC1, 0x03, 0xC0, 0x83, 0x01, 0x43, 0x00, 0x03, 0x05, 0x07, 0x00, 0x06, 0x0A, 0x84,
    0x04, 0x0E, 0x71, 0x0E, 0x40, 0x1C, 0x00, 0x3C, 0x00, 0x78, 0x01, 0xE2, 0x07, 0xA0, 0xFF, 0x80,
    0xEE, 0x04,
};

// virage4 (16x16)
int cstrVirage4Bitmap[34]= {
    0x10, 0x10, 0xC0, 0x83, 0xC1, 0x03, 0xC2, 0x80, 0xC3, 0x00, 0xE2, 0xA0, 0x60, 0x80, 0x21, 0x50,
    0x70, 0x20, 0x70, 0x8A, 0x38, 0x03, 0x3C, 0x00, 0x1E, 0x00, 0x47, 0x80, 0x05, 0xE0, 0x01, 0xFF,
    0x20, 0x77,
};

// routeExplose (16x16)
int cstrRouteExploseBitmap[34]= {
    0x10, 0x10, 0xF7, 0xD9, 0x98, 0x37, 0x09, 0x48, 0x19, 0x46, 0x21, 0x22, 0x42, 0x20, 0xC4, 0x13,
    0x84, 0x8A, 0xC8, 0xC8, 0x84, 0x12, 0x62, 0x11, 0x62, 0x22, 0x21, 0x42, 0x30, 0x94, 0xC8, 0x97,
    0xFB, 0x6F,
};

// convoi HUD (56x4) - a real 3-gauge variant of the shared 2-gauge HUD
int cstrHudConvoiBitmap[30]= {
    0x38, 0x04, 0x09, 0xFF, 0xE2, 0x9F, 0xFE, 0x2F, 0xFF, 0x7D, 0x00, 0x27, 0xD0, 0x02, 0x48, 0x01,
    0xFD, 0x00, 0x23, 0x90, 0x02, 0x68, 0x01, 0x49, 0xFF, 0xE1, 0x1F, 0xFE, 0x4F, 0xFF,
};

// -----------------------------------------------------------------------------
//   Convoi's own road/checkpoint tables (upstream's own PROGMEM `Routes[]`/
//   `CheckPoint[]` structs, split into parallel arrays)
// -----------------------------------------------------------------------------

int cstrCheckPointX[19]= {
    160, 161, 249, 270, 270, 350, 353, 342, 373, 329, 230, 220, 155, 145, 104, 98, 66, 64, 30,
};
int cstrCheckPointY[19]= {
    56, 25, 25, 45, 117, 122, 196, 202, 279, 349, 301, 335, 332, 387, 395, 454, 486, 536, 543,
};
int cstrRouteX[68]= {
    112, 128, 144, 160, 160, 160, 176, 192, 208, 224, 240, 256, 256, 272, 272, 272, 272,
    272, 272, 288, 304, 320, 336, 352, 352, 352, 352, 352, 352, 336, 320, 304, 288, 272,
    256, 256, 256, 256, 256, 240, 224, 224, 224, 224, 224, 208, 192, 176, 160, 144, 144,
    144, 144, 144, 128, 112, 96, 96, 96, 96, 96, 80, 80, 64, 64, 64, 64, 64,
};
int cstrRouteY[68]= {
    56, 56, 56, 56, 40, 24, 24, 24, 24, 24, 24, 25, 41, 41, 57, 73, 89,
    105, 121, 121, 121, 121, 121, 121, 137, 153, 169, 185, 200, 200, 200, 200, 200, 200,
    200, 216, 232, 248, 264, 264, 264, 280, 296, 312, 328, 328, 328, 328, 328, 328, 344,
    360, 376, 392, 392, 392, 392, 408, 424, 440, 456, 456, 472, 472, 488, 504, 520, 536,
};
int cstrRouteSprite[68]= {
    0, 0, 0, 4, 1, 3, 0, 0, 0, 0, 0, 2, 5, 2, 1, 1, 1,
    1, 5, 0, 0, 0, 0, 2, 1, 1, 1, 1, 4, 0, 6, 0, 0, 0,
    3, 1, 1, 1, 4, 0, 3, 1, 1, 1, 4, 0, 0, 0, 0, 3, 1,
    1, 1, 4, 0, 0, 3, 1, 1, 1, 4, 3, 4, 3, 1, 1, 1, 4,
};

// SearchDoc's own `possblePosData[]` - the hostile-building indices the data
// can be hidden in. Index 20 really is listed twice upstream.
int cstrPossiblePosData[5]= { 20, 19, 17, 20, 18 };


// -----------------------------------------------------------------------------
//   Sprite lookup tables - the merged 11-entry numbering (see header comment)
// -----------------------------------------------------------------------------

int* cstrSpriteBkg[11]=
{
    cstrDesertBushBitmap,    //  0
    cstrDesertCactusBitmap,  //  1
    cstrDesertOasisBitmap,   //  2
    cstrDesertSandBitmap,    //  3
    cstrDesertTreeBitmap,    //  4
    cstrDesertRockBitmap,    //  5
    cstrForestGrassBitmap,   //  6
    cstrForestTreeBitmap,    //  7
    cstrForestTree1Bitmap,   //  8
    cstrForestTree2Bitmap,   //  9
    cstrForestForestBitmap   // 10
};

int* cstrSprites[11]=
{
    cstrFriendHeliportBitmap,  //  0
    cstrFriendBasecampBitmap,  //  1
    cstrFriendFuelBitmap,      //  2
    cstrFriendGarageBitmap,    //  3
    cstrDesertVillageBitmap,   //  4
    cstrForestVillageBitmap,   //  5
    cstrEnnemyBunkerBitmap,    //  6
    cstrEnnemyCampBitmap,      //  7
    cstrEnnemyTourBitmap,      //  8
    cstrDesertOasisBitmap,     //  9
    cstrForestForestBitmap     // 10
};

int* cstrDestroy[9]=
{
    cstrFriendHeliportBitmap,       // 0
    cstrFriendBasecampBitmap,       // 1
    cstrFriendFuelBitmap,           // 2
    cstrFriendGarageBitmap,         // 3
    cstrDesertVillageDestrBitmap,   // 4
    cstrForestVillageDestrBitmap,   // 5
    cstrEnnemyBunkerDestrBitmap,    // 6
    cstrEnnemyCampDestrBitmap,      // 7
    cstrEnnemyTourDestrBitmap       // 8
};

int* cstrTank[5]=
{
    cstrEnnemyTankBitmap,          // 0
    cstrEnnemyTankDiagdownBitmap,  // 1
    cstrEnnemyTankDownBitmap,      // 2
    cstrEnnemyTankUpBitmap,        // 3
    cstrEnnemyTankDiagupBitmap     // 4
};

// Convoi's own escort-truck facing sprites, in upstream's own literal
// `friendMobile::sprites[8]` order.
int* cstrCamionSprites[8]=
{
    cstrCamionDBitmap,   // 0 right
    cstrCamionHDBitmap,  // 1 up-right
    cstrCamionBDBitmap,  // 2 down-right
    cstrCamionGBitmap,   // 3 left
    cstrCamionHGBitmap,  // 4 up-left
    cstrCamionBGBitmap,  // 5 down-left
    cstrCamionHBitmap,   // 6 up
    cstrCamionBBitmap    // 7 down
};

int* cstrExplosion[7]=
{
    cstrEx1Bitmap, cstrEx2Bitmap, cstrEx3Bitmap, cstrEx4Bitmap,
    cstrEx5Bitmap, cstrEx6Bitmap, cstrEx7Bitmap
};

int* cstrRouteSprites[6]=
{
    cstrRouteHBitmap,   // 0 straight, horizontal
    cstrRouteVBitmap,   // 1 straight, vertical
    cstrVirage1Bitmap,  // 2 corner
    cstrVirage2Bitmap,  // 3 corner
    cstrVirage3Bitmap,  // 4 corner
    cstrVirage4Bitmap   // 5 corner
};

// -----------------------------------------------------------------------------
//   Constants - upstream's own #defines, prefixed
// -----------------------------------------------------------------------------

#define CSTR_SCREENWIDTH 84
#define CSTR_SCREENHEIGHT 48
#define CSTR_VERTALIGNMENT 10
#define CSTR_LEVELWIDTH 630
#define CSTR_LEVELHEIGHT 630
#define CSTR_SPRITESIZE 125

#define CSTR_MAXALTITUDE 8
#define CSTR_MAXFUEL 30
#define CSTR_MAXLIFE 50
#define CSTR_MAXBULLET 10

#define CSTR_TMPUNIT 20
#define CSTR_TMPTANK 40
#define CSTR_TMPTOUR 20
#define CSTR_TMPBUNKER 10
#define CSTR_TMPRESURECTION 200

// Convoi only
#define CSTR_WAIT_TIME 240
#define CSTR_MAX_LIFE_CAM 250
#define CSTR_NB_CHECK_POINT 19
#define CSTR_NB_TSPRITE_ROUTE 68
#define CSTR_NB_FRAME_EXPLOSION 7
#define CSTR_TEMP_EXPOLOSION 35

// SearchDoc only - the five hostile-building indices the data can hide in
// (all four bunkers, with index 20 listed twice, exactly as upstream wrote it)
#define CSTR_NB_POSSIBLE_POS 5

// Array capacities - the largest any single mission needs
#define CSTR_MAX_BKGRND 15
#define CSTR_MAX_BKG 19
#define CSTR_MAX_BUILDING_FRIEND 9
#define CSTR_MAX_BUILDING_HOSTILE 21
#define CSTR_MAX_MOBILE 20

#define CSTR_MISSION_DESERT 0
#define CSTR_MISSION_FOREST 1
#define CSTR_MISSION_CONVOI 2
#define CSTR_MISSION_SEARCHDOC 3
#define CSTR_MISSION_COUNT 4

#define CSTR_STATE_TITLE 0
#define CSTR_STATE_SELECTMAP 1
#define CSTR_STATE_GAME 2
#define CSTR_STATE_ENDGAME 3

// -----------------------------------------------------------------------------
//   Upstream's own structs, flattened to this dialect's named-struct form
// -----------------------------------------------------------------------------

typedef struct
{
    int x_world;
    int y_world;
    int dir;
    int vSpeed;
    int hSpeed;
    int altitude;
    int isLanding;
    int fire;
    int fuel;
    int fuelCheck;
    int life;
    int isCrashing;
    int animHelix;
    int animBoom;
    int animDamage;
    int moveMode;
} CstrPlayer;

typedef struct
{
    int shooter;
    int x_world;
    int y_world;
    int dir;
    int distance;
} CstrBullet;

// Upstream's `Hostile` (shooting, destructible) and `Object` (non-shooting,
// destructible) differ only by a trailing fireTimer field; both are carried
// by this one struct here, with friend buildings simply never using it.
typedef struct
{
    int x_world;
    int y_world;
    int width;
    int height;
    int sprite;
    int life;
    int animBoom;
    int fireTimer;
} CstrHostile;

typedef struct
{
    int x_world;
    int y_world;
    int width;
    int height;
    int sprite;
    int life;
    int animBoom;
    int fireTimer;
    int dir;
    int batiment;
} CstrMobile;

// Upstream's `Friend` - a non-destructible landmark (heliport, base, fuel
// depot, garage, oasis)
typedef struct
{
    int x_world;
    int y_world;
    int width;
    int height;
    int sprite;
} CstrLandmark;

typedef struct
{
    int x_world;
    int y_world;
    int sprite;
} CstrBkg;

// Convoi's own escort truck (upstream's `friendMobile Camion`)
typedef struct
{
    int x_world;
    int y_world;
    int width;
    int height;
    int life;
    int animBoom;
    int dir;
    int animDamage;
} CstrCamion;

// -----------------------------------------------------------------------------
//   Globals (one variable per declaration - a comma-separated list of
//   struct-typed globals silently corrupts unrelated global memory on this
//   platform, see gameTron.c's own header comment)
// -----------------------------------------------------------------------------

CstrPlayer cstrPlayer;
CstrCamion cstrCamion;
CstrLandmark cstrBkgrnd[CSTR_MAX_BKGRND];
CstrBkg cstrBkg[CSTR_MAX_BKG];
CstrHostile cstrBuildingFriend[CSTR_MAX_BUILDING_FRIEND];
CstrHostile cstrBuildingHostile[CSTR_MAX_BUILDING_HOSTILE];
CstrMobile cstrMobilUnit[CSTR_MAX_MOBILE];
CstrBullet cstrBullet[CSTR_MAXBULLET];

int cstrState;
int cstrLvl;
int cstrDifficulty;
int cstrMoney;
int cstrDestroyedBuildings;
int cstrCoordx;
int cstrCoordy;
int cstrCptAnim;
int cstrCheck01;
int cstrCheck02;
int cstrNbHeliport;
int cstrNbBuildingHostile;
int cstrNbBuildingFriend;
int cstrNbMobileUnit;
bool cstrHasMobileUnits;
bool cstrHasResurrection;

// Convoi state
bool cstrConvoiSecuriser;
int cstrWaitTime;
int cstrCptExplosion;
int cstrCurrentCheckPoint;

// SearchDoc state
int cstrWherIsData;
bool cstrFindData;

// Real hardware has a single sound channel, so only the last sound requested
// within one tick is ever heard - see this file's own header comment.
int cstrSfxRequest;

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

void cstrSoundFx( int fxno )
{
    cstrSfxRequest = fxno;
}

void cstrFlushSound()
{
    if( cstrSfxRequest < 0 )
      return;

    // Upstream's own literal `gb.sound.playNote(17, 3, 0)` - the surrounding
    // waveform/volume/pitch-slide commands have no equivalent here.
    gbPlayNote( 17, 3 );
    cstrSfxRequest = -1;
}

// -----------------------------------------------------------------------------
//   World-to-screen position helpers (direct ports, same clamping shape)
// -----------------------------------------------------------------------------

int cstrPlayerXpos( int x )
{
    if( x < CSTR_SCREENWIDTH / 2 )
      return x;
    if( x <= CSTR_LEVELWIDTH - CSTR_SCREENWIDTH / 2 )
      return CSTR_SCREENWIDTH / 2;

    return CSTR_SCREENWIDTH - ( CSTR_LEVELWIDTH - x );
}

int cstrPlayerYpos( int y )
{
    if( y < CSTR_SCREENHEIGHT / 2 + CSTR_VERTALIGNMENT )
      return y;
    if( y <= CSTR_LEVELHEIGHT - CSTR_SCREENHEIGHT / 2 + CSTR_VERTALIGNMENT )
      return CSTR_SCREENHEIGHT / 2 + CSTR_VERTALIGNMENT;

    return CSTR_SCREENHEIGHT - ( CSTR_LEVELHEIGHT - y );
}

int cstrLndscapeXpos( int x )
{
    if( cstrPlayer.x_world < CSTR_SCREENWIDTH / 2 )
      return x;
    if( cstrPlayer.x_world <= CSTR_LEVELWIDTH - CSTR_SCREENWIDTH / 2 )
      return x - ( cstrPlayer.x_world - CSTR_SCREENWIDTH / 2 );

    return CSTR_SCREENWIDTH - ( CSTR_LEVELWIDTH - x );
}

int cstrLndscapeYpos( int y )
{
    if( cstrPlayer.y_world < CSTR_SCREENHEIGHT / 2 + CSTR_VERTALIGNMENT )
      return y;
    if( cstrPlayer.y_world <= CSTR_LEVELHEIGHT - CSTR_SCREENHEIGHT / 2 + CSTR_VERTALIGNMENT )
      return y - ( cstrPlayer.y_world - ( CSTR_VERTALIGNMENT + CSTR_SCREENHEIGHT / 2 ) );

    return CSTR_SCREENHEIGHT - ( CSTR_LEVELHEIGHT - y );
}

// Upstream's own `((v % SPRITESIZE) + SPRITESIZE) % SPRITESIZE` wrap, applied
// to the same three landscape cases - the scenery layer tiles every 125 world
// units instead of scrolling with the map.
int cstrBackgrndXpos( int x )
{
    int v = cstrLndscapeXpos( x );
    return ( ( v % CSTR_SPRITESIZE ) + CSTR_SPRITESIZE ) % CSTR_SPRITESIZE;
}

int cstrBackgrndYpos( int y )
{
    int v = cstrLndscapeYpos( y );
    return ( ( v % CSTR_SPRITESIZE ) + CSTR_SPRITESIZE ) % CSTR_SPRITESIZE;
}

// -----------------------------------------------------------------------------
//   Level / player initialisation
// -----------------------------------------------------------------------------

void cstrInitPlayer()
{
    if( cstrLvl == CSTR_MISSION_FOREST )
    {
        cstrPlayer.x_world = 572;
        cstrPlayer.y_world = 593;
    }
    else
    {
        // Desert, Convoi and SearchDoc all start at the same desert base
        cstrPlayer.x_world = 65;
        cstrPlayer.y_world = 43;
    }

    cstrPlayer.altitude = 0;
    cstrPlayer.dir = 0;

    cstrPlayer.hSpeed = 0;
    cstrPlayer.vSpeed = 0;

    cstrPlayer.isLanding = 1;
    cstrPlayer.isCrashing = 0;

    cstrPlayer.life = CSTR_MAXLIFE - CSTR_MAXFUEL;
    cstrPlayer.fuel = 1;
    cstrPlayer.fuelCheck = 0;
    cstrPlayer.fire = 0;

    cstrPlayer.animHelix = 0;
    cstrPlayer.animBoom = 0;
    cstrPlayer.animDamage = 0;

    cstrPlayer.moveMode = 0; // 0 rotate, 1 strafe

    int i;
    for( i = 0; i < CSTR_MAXBULLET; i++ )
    {
        cstrBullet[ i ].shooter = 0;
        cstrBullet[ i ].x_world = 0;
        cstrBullet[ i ].y_world = 0;
        cstrBullet[ i ].dir = 0;
        cstrBullet[ i ].distance = 0;
    }
}

// The desert map's own scenery/landmark/village/tower layout, shared verbatim
// by Desert Strike, Convoi and SearchDoc (all three upstream sketches carry
// byte-identical copies of it, only their enemy rosters differ).
void cstrInitDesertScenery()
{
    cstrBkg[ 0].x_world = 22;   cstrBkg[ 0].y_world = 16;  cstrBkg[ 0].sprite = 3;
    cstrBkg[ 1].x_world = 30;   cstrBkg[ 1].y_world = 21;  cstrBkg[ 1].sprite = 3;
    cstrBkg[ 2].x_world = 22;   cstrBkg[ 2].y_world = 26;  cstrBkg[ 2].sprite = 3;
    cstrBkg[ 3].x_world = 77;   cstrBkg[ 3].y_world = 11;  cstrBkg[ 3].sprite = 3;
    cstrBkg[ 4].x_world = 43;   cstrBkg[ 4].y_world = 45;  cstrBkg[ 4].sprite = 3;
    cstrBkg[ 5].x_world = 82;   cstrBkg[ 5].y_world = 48;  cstrBkg[ 5].sprite = 3;
    cstrBkg[ 6].x_world = 27;   cstrBkg[ 6].y_world = 81;  cstrBkg[ 6].sprite = 3;
    cstrBkg[ 7].x_world = 36;   cstrBkg[ 7].y_world = 85;  cstrBkg[ 7].sprite = 3;
    cstrBkg[ 8].x_world = 28;   cstrBkg[ 8].y_world = 91;  cstrBkg[ 8].sprite = 3;
    cstrBkg[ 9].x_world = 103;  cstrBkg[ 9].y_world = 116; cstrBkg[ 9].sprite = 3;
    cstrBkg[10].x_world = 64;   cstrBkg[10].y_world = 30;  cstrBkg[10].sprite = 0;
    cstrBkg[11].x_world = 14;   cstrBkg[11].y_world = 64;  cstrBkg[11].sprite = 0;
    cstrBkg[12].x_world = 53;   cstrBkg[12].y_world = 119; cstrBkg[12].sprite = 0;
    cstrBkg[13].x_world = 4;    cstrBkg[13].y_world = 3;   cstrBkg[13].sprite = 1;
    cstrBkg[14].x_world = 89;   cstrBkg[14].y_world = 73;  cstrBkg[14].sprite = 1;
    cstrBkg[15].x_world = 98;   cstrBkg[15].y_world = 80;  cstrBkg[15].sprite = 1;
    cstrBkg[16].x_world = 84;   cstrBkg[16].y_world = 84;  cstrBkg[16].sprite = 5;
    cstrBkg[17].x_world = 7;    cstrBkg[17].y_world = 109; cstrBkg[17].sprite = 4;
    cstrBkg[18].x_world = 110;  cstrBkg[18].y_world = 26;  cstrBkg[18].sprite = 4;

    cstrBkgrnd[ 0].x_world = 60;   cstrBkgrnd[ 0].y_world = 40;   cstrBkgrnd[ 0].width = 13;  cstrBkgrnd[ 0].height = 7;   cstrBkgrnd[ 0].sprite = 0; // heliport
    cstrBkgrnd[ 1].x_world = 593;  cstrBkgrnd[ 1].y_world = 57;   cstrBkgrnd[ 1].width = 13;  cstrBkgrnd[ 1].height = 7;   cstrBkgrnd[ 1].sprite = 0;
    cstrBkgrnd[ 2].x_world = 395;  cstrBkgrnd[ 2].y_world = 133;  cstrBkgrnd[ 2].width = 13;  cstrBkgrnd[ 2].height = 7;   cstrBkgrnd[ 2].sprite = 0;
    cstrBkgrnd[ 3].x_world = 258;  cstrBkgrnd[ 3].y_world = 294;  cstrBkgrnd[ 3].width = 13;  cstrBkgrnd[ 3].height = 7;   cstrBkgrnd[ 3].sprite = 0;
    cstrBkgrnd[ 4].x_world = 46;   cstrBkgrnd[ 4].y_world = 567;  cstrBkgrnd[ 4].width = 13;  cstrBkgrnd[ 4].height = 7;   cstrBkgrnd[ 4].sprite = 0;
    cstrBkgrnd[ 5].x_world = 57;   cstrBkgrnd[ 5].y_world = 19;   cstrBkgrnd[ 5].width = 19;  cstrBkgrnd[ 5].height = 15;  cstrBkgrnd[ 5].sprite = 1; // base camp
    cstrBkgrnd[ 6].x_world = 24;   cstrBkgrnd[ 6].y_world = 27;   cstrBkgrnd[ 6].width = 32;  cstrBkgrnd[ 6].height = 29;  cstrBkgrnd[ 6].sprite = 2; // fuel
    cstrBkgrnd[ 7].x_world = 10;   cstrBkgrnd[ 7].y_world = 550;  cstrBkgrnd[ 7].width = 32;  cstrBkgrnd[ 7].height = 29;  cstrBkgrnd[ 7].sprite = 2;
    cstrBkgrnd[ 8].x_world = 385;  cstrBkgrnd[ 8].y_world = 103;  cstrBkgrnd[ 8].width = 32;  cstrBkgrnd[ 8].height = 29;  cstrBkgrnd[ 8].sprite = 2;
    cstrBkgrnd[ 9].x_world = 77;   cstrBkgrnd[ 9].y_world = 33;   cstrBkgrnd[ 9].width = 25;  cstrBkgrnd[ 9].height = 26;  cstrBkgrnd[ 9].sprite = 3; // garage
    cstrBkgrnd[10].x_world = 589;  cstrBkgrnd[10].y_world = 26;   cstrBkgrnd[10].width = 25;  cstrBkgrnd[10].height = 26;  cstrBkgrnd[10].sprite = 3;
    cstrBkgrnd[11].x_world = 274;  cstrBkgrnd[11].y_world = 287;  cstrBkgrnd[11].width = 25;  cstrBkgrnd[11].height = 26;  cstrBkgrnd[11].sprite = 3;
    cstrBkgrnd[12].x_world = 253;  cstrBkgrnd[12].y_world = 159;  cstrBkgrnd[12].width = 36;  cstrBkgrnd[12].height = 24;  cstrBkgrnd[12].sprite = 9; // oasis
    cstrBkgrnd[13].x_world = 152;  cstrBkgrnd[13].y_world = 414;  cstrBkgrnd[13].width = 36;  cstrBkgrnd[13].height = 24;  cstrBkgrnd[13].sprite = 9;
    cstrBkgrnd[14].x_world = 526;  cstrBkgrnd[14].y_world = 284;  cstrBkgrnd[14].width = 36;  cstrBkgrnd[14].height = 24;  cstrBkgrnd[14].sprite = 9;
}

// Shared size/life/sprite pass over both building arrays - byte-identical in
// all four upstream sketches once renumbered, so it is written once here.
void cstrInitBuildingStats( int villageSprite )
{
    int i;
    for( i = 0; i < CSTR_MAX_BUILDING_FRIEND; i++ )
    {
        cstrBuildingFriend[ i ].animBoom = 0;
        cstrBuildingFriend[ i ].fireTimer = 0;
        if( i < 3 )
        {
            cstrBuildingFriend[ i ].width = 72;
            cstrBuildingFriend[ i ].height = 41;
            cstrBuildingFriend[ i ].sprite = villageSprite;
            cstrBuildingFriend[ i ].life = 80;
        }
        else
        {
            cstrBuildingFriend[ i ].width = 16;
            cstrBuildingFriend[ i ].height = 22;
            cstrBuildingFriend[ i ].sprite = 7; // enemy camp
            cstrBuildingFriend[ i ].life = 20;
        }
    }

    for( i = 0; i < CSTR_MAX_BUILDING_HOSTILE; i++ )
    {
        cstrBuildingHostile[ i ].animBoom = 0;
        cstrBuildingHostile[ i ].fireTimer = 0;
        if( i < 17 )
        {
            cstrBuildingHostile[ i ].width = 7;
            cstrBuildingHostile[ i ].height = 10;
            cstrBuildingHostile[ i ].sprite = 8; // tower
            cstrBuildingHostile[ i ].life = 20;
        }
        else
        {
            cstrBuildingHostile[ i ].width = 32;
            cstrBuildingHostile[ i ].height = 32;
            cstrBuildingHostile[ i ].sprite = 6; // bunker
            cstrBuildingHostile[ i ].life = 80;
        }
    }
}

// Shared size/life pass over the mobile-enemy roster: `infantryCount` records
// are 6x6 infantry, the rest 16x14 tanks.
void cstrInitMobileStats( int infantryCount )
{
    int i;
    for( i = 0; i < cstrNbMobileUnit; i++ )
    {
        cstrMobilUnit[ i ].animBoom = 0;
        cstrMobilUnit[ i ].fireTimer = 0;
        cstrMobilUnit[ i ].dir = 4;
        if( i < infantryCount )
        {
            cstrMobilUnit[ i ].width = 6;
            cstrMobilUnit[ i ].height = 6;
            cstrMobilUnit[ i ].sprite = 0;
            cstrMobilUnit[ i ].life = 5;
        }
        else
        {
            cstrMobilUnit[ i ].width = 16;
            cstrMobilUnit[ i ].height = 14;
            cstrMobilUnit[ i ].sprite = 1;
            cstrMobilUnit[ i ].life = 30;
        }
    }
}

// The desert map's own building layout - shared verbatim by Desert Strike and
// SearchDoc (Convoi keeps the same scenery but moves every building).
void cstrInitDesertBuildings()
{
    cstrInitDesertScenery();

    cstrBuildingFriend[0].x_world = 31;   cstrBuildingFriend[0].y_world = 72;  // village
    cstrBuildingFriend[1].x_world = 538;  cstrBuildingFriend[1].y_world = 73;
    cstrBuildingFriend[2].x_world = 16;   cstrBuildingFriend[2].y_world = 580;
    cstrBuildingFriend[3].x_world = 289;  cstrBuildingFriend[3].y_world = 164; // camp
    cstrBuildingFriend[4].x_world = 568;  cstrBuildingFriend[4].y_world = 276;
    cstrBuildingFriend[5].x_world = 381;  cstrBuildingFriend[5].y_world = 456;
    cstrBuildingFriend[6].x_world = 600;  cstrBuildingFriend[6].y_world = 517;
    cstrBuildingFriend[7].x_world = 533;  cstrBuildingFriend[7].y_world = 532;
    cstrBuildingFriend[8].x_world = 511;  cstrBuildingFriend[8].y_world = 600;

    cstrBuildingHostile[ 0].x_world = 95;   cstrBuildingHostile[ 0].y_world = 286; // towers
    cstrBuildingHostile[ 1].x_world = 77;   cstrBuildingHostile[ 1].y_world = 324;
    cstrBuildingHostile[ 2].x_world = 117;  cstrBuildingHostile[ 2].y_world = 344;
    cstrBuildingHostile[ 3].x_world = 134;  cstrBuildingHostile[ 3].y_world = 304;
    cstrBuildingHostile[ 4].x_world = 381;  cstrBuildingHostile[ 4].y_world = 38;
    cstrBuildingHostile[ 5].x_world = 436;  cstrBuildingHostile[ 5].y_world = 21;
    cstrBuildingHostile[ 6].x_world = 417;  cstrBuildingHostile[ 6].y_world = 77;
    cstrBuildingHostile[ 7].x_world = 397;  cstrBuildingHostile[ 7].y_world = 396;
    cstrBuildingHostile[ 8].x_world = 378;  cstrBuildingHostile[ 8].y_world = 428;
    cstrBuildingHostile[ 9].x_world = 413;  cstrBuildingHostile[ 9].y_world = 456;
    cstrBuildingHostile[10].x_world = 451;  cstrBuildingHostile[10].y_world = 424;
    cstrBuildingHostile[11].x_world = 436;  cstrBuildingHostile[11].y_world = 399;
    cstrBuildingHostile[12].x_world = 608;  cstrBuildingHostile[12].y_world = 552;
    cstrBuildingHostile[13].x_world = 586;  cstrBuildingHostile[13].y_world = 554;
    cstrBuildingHostile[14].x_world = 556;  cstrBuildingHostile[14].y_world = 564;
    cstrBuildingHostile[15].x_world = 550;  cstrBuildingHostile[15].y_world = 585;
    cstrBuildingHostile[16].x_world = 547;  cstrBuildingHostile[16].y_world = 609;
    cstrBuildingHostile[17].x_world = 94;   cstrBuildingHostile[17].y_world = 302; // bunkers
    cstrBuildingHostile[18].x_world = 401;  cstrBuildingHostile[18].y_world = 36;
    cstrBuildingHostile[19].x_world = 401;  cstrBuildingHostile[19].y_world = 411;
    cstrBuildingHostile[20].x_world = 577;  cstrBuildingHostile[20].y_world = 584;

    cstrInitBuildingStats( 4 ); // desert village
}

void cstrInitLevelDesert()
{
    cstrInitDesertBuildings();

    if( !cstrHasMobileUnits )
      return;

    cstrMobilUnit[ 0].x_world = 572;  cstrMobilUnit[ 0].y_world = 123;  cstrMobilUnit[ 0].batiment = 1; // infantry
    cstrMobilUnit[ 1].x_world = 309;  cstrMobilUnit[ 1].y_world = 166;  cstrMobilUnit[ 1].batiment = 3;
    cstrMobilUnit[ 2].x_world = 294;  cstrMobilUnit[ 2].y_world = 191;  cstrMobilUnit[ 2].batiment = 3;
    cstrMobilUnit[ 3].x_world = 586;  cstrMobilUnit[ 3].y_world = 275;  cstrMobilUnit[ 3].batiment = 4;
    cstrMobilUnit[ 4].x_world = 579;  cstrMobilUnit[ 4].y_world = 300;  cstrMobilUnit[ 4].batiment = 4;
    cstrMobilUnit[ 5].x_world = 366;  cstrMobilUnit[ 5].y_world = 455;  cstrMobilUnit[ 5].batiment = 5;
    cstrMobilUnit[ 6].x_world = 389;  cstrMobilUnit[ 6].y_world = 483;  cstrMobilUnit[ 6].batiment = 5;
    cstrMobilUnit[ 7].x_world = 617;  cstrMobilUnit[ 7].y_world = 520;  cstrMobilUnit[ 7].batiment = 6;
    cstrMobilUnit[ 8].x_world = 610;  cstrMobilUnit[ 8].y_world = 541;  cstrMobilUnit[ 8].batiment = 6;
    cstrMobilUnit[ 9].x_world = 550;  cstrMobilUnit[ 9].y_world = 535;  cstrMobilUnit[ 9].batiment = 7;
    cstrMobilUnit[10].x_world = 544;  cstrMobilUnit[10].y_world = 558;  cstrMobilUnit[10].batiment = 7;
    cstrMobilUnit[11].x_world = 519;  cstrMobilUnit[11].y_world = 592;  cstrMobilUnit[11].batiment = 8;
    cstrMobilUnit[12].x_world = 528;  cstrMobilUnit[12].y_world = 603;  cstrMobilUnit[12].batiment = 8;
    cstrMobilUnit[13].x_world = 93;   cstrMobilUnit[13].y_world = 586;  cstrMobilUnit[13].batiment = 2;
    cstrMobilUnit[14].x_world = 345;  cstrMobilUnit[14].y_world = 540;  cstrMobilUnit[14].batiment = 2;
    // Upstream assigns every tank's spawn building to unit 0 instead of its
    // own index - a real copy-paste bug, kept verbatim (see header comment).
    cstrMobilUnit[15].x_world = 379;  cstrMobilUnit[15].y_world = 23;   cstrMobilUnit[0].batiment = 18; // tanks
    cstrMobilUnit[16].x_world = 73;   cstrMobilUnit[16].y_world = 284;  cstrMobilUnit[0].batiment = 17;
    cstrMobilUnit[17].x_world = 411;  cstrMobilUnit[17].y_world = 398;  cstrMobilUnit[0].batiment = 19;
    cstrMobilUnit[18].x_world = 279;  cstrMobilUnit[18].y_world = 544;  cstrMobilUnit[0].batiment = 20;
    cstrMobilUnit[19].x_world = 576;  cstrMobilUnit[19].y_world = 531;  cstrMobilUnit[0].batiment = 20;

    cstrInitMobileStats( 15 );
}

void cstrInitLevelForest()
{
    cstrBkg[ 0].x_world = 55;   cstrBkg[ 0].y_world = 40;  cstrBkg[ 0].sprite = 6; // grass
    cstrBkg[ 1].x_world = 78;   cstrBkg[ 1].y_world = 37;  cstrBkg[ 1].sprite = 6;
    cstrBkg[ 2].x_world = 72;   cstrBkg[ 2].y_world = 58;  cstrBkg[ 2].sprite = 6;
    cstrBkg[ 3].x_world = 52;   cstrBkg[ 3].y_world = 92;  cstrBkg[ 3].sprite = 6;
    cstrBkg[ 4].x_world = 105;  cstrBkg[ 4].y_world = 108; cstrBkg[ 4].sprite = 6;
    cstrBkg[ 5].x_world = 19;   cstrBkg[ 5].y_world = 16;  cstrBkg[ 5].sprite = 5; // rock
    cstrBkg[ 6].x_world = 89;   cstrBkg[ 6].y_world = 23;  cstrBkg[ 6].sprite = 5;
    cstrBkg[ 7].x_world = 4;    cstrBkg[ 7].y_world = 111; cstrBkg[ 7].sprite = 5;
    cstrBkg[ 8].x_world = 5;    cstrBkg[ 8].y_world = 72;  cstrBkg[ 8].sprite = 7; // small tree
    cstrBkg[ 9].x_world = 105;  cstrBkg[ 9].y_world = 64;  cstrBkg[ 9].sprite = 7;
    cstrBkg[10].x_world = 49;   cstrBkg[10].y_world = 11;  cstrBkg[10].sprite = 8; // normal tree
    cstrBkg[11].x_world = 2;    cstrBkg[11].y_world = 33;  cstrBkg[11].sprite = 8;
    cstrBkg[12].x_world = 103;  cstrBkg[12].y_world = 39;  cstrBkg[12].sprite = 8;
    cstrBkg[13].x_world = 34;   cstrBkg[13].y_world = 55;  cstrBkg[13].sprite = 8;
    cstrBkg[14].x_world = 75;   cstrBkg[14].y_world = 82;  cstrBkg[14].sprite = 8;
    cstrBkg[15].x_world = 26;   cstrBkg[15].y_world = 95;  cstrBkg[15].sprite = 8;
    cstrBkg[16].x_world = 3;    cstrBkg[16].y_world = 9;   cstrBkg[16].sprite = 9; // big tree
    cstrBkg[17].x_world = 33;   cstrBkg[17].y_world = 29;  cstrBkg[17].sprite = 9;
    cstrBkg[18].x_world = 11;   cstrBkg[18].y_world = 51;  cstrBkg[18].sprite = 9;

    // Upstream writes bkgrnd[5].sprite five times here instead of
    // bkgrnd[0..4].sprite - a real copy-paste bug, reproduced literally. It is
    // harmless: sprite 0 IS the heliport, and those five cells are either
    // still zero from cartridge boot or were left at 0 by whichever
    // desert-tileset mission ran before this one.
    cstrBkgrnd[ 0].x_world = 567;  cstrBkgrnd[ 0].y_world = 590;  cstrBkgrnd[ 0].width = 13;  cstrBkgrnd[ 0].height = 7;   cstrBkgrnd[5].sprite = 0; // heliport
    cstrBkgrnd[ 1].x_world = 273;  cstrBkgrnd[ 1].y_world = 92;   cstrBkgrnd[ 1].width = 13;  cstrBkgrnd[ 1].height = 7;   cstrBkgrnd[5].sprite = 0;
    cstrBkgrnd[ 2].x_world = 569;  cstrBkgrnd[ 2].y_world = 31;   cstrBkgrnd[ 2].width = 13;  cstrBkgrnd[ 2].height = 7;   cstrBkgrnd[5].sprite = 0;
    cstrBkgrnd[ 3].x_world = 31;   cstrBkgrnd[ 3].y_world = 344;  cstrBkgrnd[ 3].width = 13;  cstrBkgrnd[ 3].height = 7;   cstrBkgrnd[5].sprite = 0;
    cstrBkgrnd[ 4].x_world = 58;   cstrBkgrnd[ 4].y_world = 600;  cstrBkgrnd[ 4].width = 13;  cstrBkgrnd[ 4].height = 7;   cstrBkgrnd[5].sprite = 0;
    cstrBkgrnd[ 5].x_world = 565;  cstrBkgrnd[ 5].y_world = 571;  cstrBkgrnd[ 5].width = 19;  cstrBkgrnd[ 5].height = 15;  cstrBkgrnd[5].sprite = 1; // base
    cstrBkgrnd[ 6].x_world = 544;  cstrBkgrnd[ 6].y_world = 595;  cstrBkgrnd[ 6].width = 32;  cstrBkgrnd[ 6].height = 29;  cstrBkgrnd[6].sprite = 2; // fuel
    cstrBkgrnd[ 7].x_world = 536;  cstrBkgrnd[ 7].y_world = 18;   cstrBkgrnd[ 7].width = 32;  cstrBkgrnd[ 7].height = 29;  cstrBkgrnd[7].sprite = 2;
    cstrBkgrnd[ 8].x_world = 23;   cstrBkgrnd[ 8].y_world = 584;  cstrBkgrnd[ 8].width = 32;  cstrBkgrnd[ 8].height = 29;  cstrBkgrnd[8].sprite = 2;
    cstrBkgrnd[ 9].x_world = 585;  cstrBkgrnd[ 9].y_world = 597;  cstrBkgrnd[ 9].width = 25;  cstrBkgrnd[ 9].height = 26;  cstrBkgrnd[9].sprite = 3; // garage
    cstrBkgrnd[10].x_world = 292;  cstrBkgrnd[10].y_world = 85;   cstrBkgrnd[10].width = 25;  cstrBkgrnd[10].height = 26;  cstrBkgrnd[10].sprite = 3;
    cstrBkgrnd[11].x_world = 47;   cstrBkgrnd[11].y_world = 336;  cstrBkgrnd[11].width = 25;  cstrBkgrnd[11].height = 26;  cstrBkgrnd[11].sprite = 3;
    cstrBkgrnd[12].x_world = 169;  cstrBkgrnd[12].y_world = 465;  cstrBkgrnd[12].width = 36;  cstrBkgrnd[12].height = 24;  cstrBkgrnd[12].sprite = 10; // forest
    cstrBkgrnd[13].x_world = 147;  cstrBkgrnd[13].y_world = 211;  cstrBkgrnd[13].width = 36;  cstrBkgrnd[13].height = 24;  cstrBkgrnd[13].sprite = 10;
    cstrBkgrnd[14].x_world = 394;  cstrBkgrnd[14].y_world = 339;  cstrBkgrnd[14].width = 36;  cstrBkgrnd[14].height = 24;  cstrBkgrnd[14].sprite = 10;

    cstrBuildingFriend[0].x_world = 395;  cstrBuildingFriend[0].y_world = 577; // village
    cstrBuildingFriend[1].x_world = 36;   cstrBuildingFriend[1].y_world = 81;
    cstrBuildingFriend[2].x_world = 535;  cstrBuildingFriend[2].y_world = 91;
    cstrBuildingFriend[3].x_world = 122;  cstrBuildingFriend[3].y_world = 8;   // camp
    cstrBuildingFriend[4].x_world = 372;  cstrBuildingFriend[4].y_world = 7;
    cstrBuildingFriend[5].x_world = 372;  cstrBuildingFriend[5].y_world = 133;
    cstrBuildingFriend[6].x_world = 247;  cstrBuildingFriend[6].y_world = 257;
    cstrBuildingFriend[7].x_world = 372;  cstrBuildingFriend[7].y_world = 382;
    cstrBuildingFriend[8].x_world = 123;  cstrBuildingFriend[8].y_world = 507;

    cstrBuildingHostile[ 0].x_world = 3;    cstrBuildingHostile[ 0].y_world = 16;  // towers
    cstrBuildingHostile[ 1].x_world = 43;   cstrBuildingHostile[ 1].y_world = 2;
    cstrBuildingHostile[ 2].x_world = 59;   cstrBuildingHostile[ 2].y_world = 51;
    cstrBuildingHostile[ 3].x_world = 73;   cstrBuildingHostile[ 3].y_world = 35;
    cstrBuildingHostile[ 4].x_world = 177;  cstrBuildingHostile[ 4].y_world = 97;
    cstrBuildingHostile[ 5].x_world = 406;  cstrBuildingHostile[ 5].y_world = 86;
    cstrBuildingHostile[ 6].x_world = 423;  cstrBuildingHostile[ 6].y_world = 70;
    cstrBuildingHostile[ 7].x_world = 463;  cstrBuildingHostile[ 7].y_world = 107;
    cstrBuildingHostile[ 8].x_world = 435;  cstrBuildingHostile[ 8].y_world = 225;
    cstrBuildingHostile[ 9].x_world = 132;  cstrBuildingHostile[ 9].y_world = 279;
    cstrBuildingHostile[10].x_world = 272;  cstrBuildingHostile[10].y_world = 352;
    cstrBuildingHostile[11].x_world = 314;  cstrBuildingHostile[11].y_world = 325;
    cstrBuildingHostile[12].x_world = 335;  cstrBuildingHostile[12].y_world = 362;
    cstrBuildingHostile[13].x_world = 17;   cstrBuildingHostile[13].y_world = 433;
    cstrBuildingHostile[14].x_world = 126;  cstrBuildingHostile[14].y_world = 389;
    cstrBuildingHostile[15].x_world = 98;   cstrBuildingHostile[15].y_world = 453;
    cstrBuildingHostile[16].x_world = 4;    cstrBuildingHostile[16].y_world = 515;
    cstrBuildingHostile[17].x_world = 36;   cstrBuildingHostile[17].y_world = 14;  // bunkers
    cstrBuildingHostile[18].x_world = 46;   cstrBuildingHostile[18].y_world = 462;
    cstrBuildingHostile[19].x_world = 298;  cstrBuildingHostile[19].y_world = 337;
    cstrBuildingHostile[20].x_world = 422;  cstrBuildingHostile[20].y_world = 85;

    cstrInitBuildingStats( 5 ); // forest village

    if( !cstrHasMobileUnits )
      return;

    cstrMobilUnit[ 0].x_world = 154;  cstrMobilUnit[ 0].y_world = 28;   cstrMobilUnit[ 0].batiment = 3; // infantry
    cstrMobilUnit[ 1].x_world = 131;  cstrMobilUnit[ 1].y_world = 49;   cstrMobilUnit[ 1].batiment = 3;
    cstrMobilUnit[ 2].x_world = 117;  cstrMobilUnit[ 2].y_world = 79;   cstrMobilUnit[ 2].batiment = 1;
    cstrMobilUnit[ 3].x_world = 65;   cstrMobilUnit[ 3].y_world = 120;  cstrMobilUnit[ 3].batiment = 1;
    cstrMobilUnit[ 4].x_world = 405;  cstrMobilUnit[ 4].y_world = 32;   cstrMobilUnit[ 4].batiment = 4;
    cstrMobilUnit[ 5].x_world = 586;  cstrMobilUnit[ 5].y_world = 70;   cstrMobilUnit[ 5].batiment = 2;
    cstrMobilUnit[ 6].x_world = 521;  cstrMobilUnit[ 6].y_world = 100;  cstrMobilUnit[ 6].batiment = 2;
    cstrMobilUnit[ 7].x_world = 590;  cstrMobilUnit[ 7].y_world = 136;  cstrMobilUnit[ 7].batiment = 2;
    cstrMobilUnit[ 8].x_world = 347;  cstrMobilUnit[ 8].y_world = 128;  cstrMobilUnit[ 8].batiment = 5;
    cstrMobilUnit[ 9].x_world = 406;  cstrMobilUnit[ 9].y_world = 155;  cstrMobilUnit[ 9].batiment = 5;
    cstrMobilUnit[10].x_world = 226;  cstrMobilUnit[10].y_world = 252;  cstrMobilUnit[10].batiment = 6;
    cstrMobilUnit[11].x_world = 279;  cstrMobilUnit[11].y_world = 280;  cstrMobilUnit[11].batiment = 6;
    cstrMobilUnit[12].x_world = 404;  cstrMobilUnit[12].y_world = 405;  cstrMobilUnit[12].batiment = 7;
    cstrMobilUnit[13].x_world = 101;  cstrMobilUnit[13].y_world = 534;  cstrMobilUnit[13].batiment = 8;
    cstrMobilUnit[14].x_world = 145;  cstrMobilUnit[14].y_world = 597;  cstrMobilUnit[14].batiment = 8;
    // Same real upstream copy-paste bug as the desert roster above
    cstrMobilUnit[15].x_world = 36;   cstrMobilUnit[15].y_world = 47;   cstrMobilUnit[0].batiment = 17; // tanks
    cstrMobilUnit[16].x_world = 84;   cstrMobilUnit[16].y_world = 479;  cstrMobilUnit[0].batiment = 18;
    cstrMobilUnit[17].x_world = 300;  cstrMobilUnit[17].y_world = 373;  cstrMobilUnit[0].batiment = 19;
    cstrMobilUnit[18].x_world = 462;  cstrMobilUnit[18].y_world = 68;   cstrMobilUnit[0].batiment = 20;
    cstrMobilUnit[19].x_world = 435;  cstrMobilUnit[19].y_world = 475;  cstrMobilUnit[0].batiment = 17;

    cstrInitMobileStats( 15 );
}

void cstrInitLevelConvoi()
{
    cstrInitDesertScenery();

    cstrBuildingFriend[0].x_world = 31;   cstrBuildingFriend[0].y_world = 72;  // village
    cstrBuildingFriend[1].x_world = 538;  cstrBuildingFriend[1].y_world = 73;
    cstrBuildingFriend[2].x_world = 16;   cstrBuildingFriend[2].y_world = 580;
    cstrBuildingFriend[3].x_world = 221;  cstrBuildingFriend[3].y_world = 53;  // camp
    cstrBuildingFriend[4].x_world = 288;  cstrBuildingFriend[4].y_world = 166;
    cstrBuildingFriend[5].x_world = 430;  cstrBuildingFriend[5].y_world = 223;
    cstrBuildingFriend[6].x_world = 306;  cstrBuildingFriend[6].y_world = 276;
    cstrBuildingFriend[7].x_world = 269;  cstrBuildingFriend[7].y_world = 355;
    cstrBuildingFriend[8].x_world = 166;  cstrBuildingFriend[8].y_world = 304;

    cstrBuildingHostile[ 0].x_world = 108;  cstrBuildingHostile[ 0].y_world = 180; // towers
    cstrBuildingHostile[ 1].x_world = 77;   cstrBuildingHostile[ 1].y_world = 205;
    cstrBuildingHostile[ 2].x_world = 294;  cstrBuildingHostile[ 2].y_world = 105;
    cstrBuildingHostile[ 3].x_world = 294;  cstrBuildingHostile[ 3].y_world = 141;
    cstrBuildingHostile[ 4].x_world = 441;  cstrBuildingHostile[ 4].y_world = 126;
    cstrBuildingHostile[ 5].x_world = 278;  cstrBuildingHostile[ 5].y_world = 222;
    cstrBuildingHostile[ 6].x_world = 242;  cstrBuildingHostile[ 6].y_world = 249;
    cstrBuildingHostile[ 7].x_world = 400;  cstrBuildingHostile[ 7].y_world = 272;
    cstrBuildingHostile[ 8].x_world = 439;  cstrBuildingHostile[ 8].y_world = 276;
    cstrBuildingHostile[ 9].x_world = 455;  cstrBuildingHostile[ 9].y_world = 301;
    cstrBuildingHostile[10].x_world = 416;  cstrBuildingHostile[10].y_world = 334;
    cstrBuildingHostile[11].x_world = 381;  cstrBuildingHostile[11].y_world = 305;
    cstrBuildingHostile[12].x_world = 95;   cstrBuildingHostile[12].y_world = 286;
    cstrBuildingHostile[13].x_world = 135;  cstrBuildingHostile[13].y_world = 305;
    cstrBuildingHostile[14].x_world = 117;  cstrBuildingHostile[14].y_world = 344;
    cstrBuildingHostile[15].x_world = 78;   cstrBuildingHostile[15].y_world = 325;
    cstrBuildingHostile[16].x_world = 169;  cstrBuildingHostile[16].y_world = 351;
    cstrBuildingHostile[17].x_world = 28;   cstrBuildingHostile[17].y_world = 171; // bunkers
    cstrBuildingHostile[18].x_world = 401;  cstrBuildingHostile[18].y_world = 36;
    cstrBuildingHostile[19].x_world = 94;   cstrBuildingHostile[19].y_world = 303;
    cstrBuildingHostile[20].x_world = 405;  cstrBuildingHostile[20].y_world = 289;

    cstrInitBuildingStats( 4 ); // desert village

    cstrMobilUnit[ 0].x_world = 211;  cstrMobilUnit[ 0].y_world = 49;   cstrMobilUnit[ 0].batiment = 3; // infantry
    cstrMobilUnit[ 1].x_world = 236;  cstrMobilUnit[ 1].y_world = 13;   cstrMobilUnit[ 1].batiment = 3;
    cstrMobilUnit[ 2].x_world = 260;  cstrMobilUnit[ 2].y_world = 107;  cstrMobilUnit[ 2].batiment = 4;
    cstrMobilUnit[ 3].x_world = 309;  cstrMobilUnit[ 3].y_world = 166;  cstrMobilUnit[ 3].batiment = 4;
    cstrMobilUnit[ 4].x_world = 293;  cstrMobilUnit[ 4].y_world = 191;  cstrMobilUnit[ 4].batiment = 4;
    cstrMobilUnit[ 5].x_world = 310;  cstrMobilUnit[ 5].y_world = 264;  cstrMobilUnit[ 5].batiment = 6;
    cstrMobilUnit[ 6].x_world = 294;  cstrMobilUnit[ 6].y_world = 276;  cstrMobilUnit[ 6].batiment = 6;
    cstrMobilUnit[ 7].x_world = 448;  cstrMobilUnit[ 7].y_world = 225;  cstrMobilUnit[ 7].batiment = 5;
    cstrMobilUnit[ 8].x_world = 441;  cstrMobilUnit[ 8].y_world = 246;  cstrMobilUnit[ 8].batiment = 5;
    cstrMobilUnit[ 9].x_world = 209;  cstrMobilUnit[ 9].y_world = 298;  cstrMobilUnit[ 9].batiment = 7;
    cstrMobilUnit[10].x_world = 192;  cstrMobilUnit[10].y_world = 346;  cstrMobilUnit[10].batiment = 7;
    cstrMobilUnit[11].x_world = 203;  cstrMobilUnit[11].y_world = 358;  cstrMobilUnit[11].batiment = 7;
    cstrMobilUnit[12].x_world = 277;  cstrMobilUnit[12].y_world = 346;  cstrMobilUnit[12].batiment = 8;
    cstrMobilUnit[13].x_world = 286;  cstrMobilUnit[13].y_world = 358;  cstrMobilUnit[13].batiment = 8;
    cstrMobilUnit[14].x_world = 92;   cstrMobilUnit[14].y_world = 585;  cstrMobilUnit[14].batiment = 8;
    // Same real upstream copy-paste bug as the desert/forest rosters above
    cstrMobilUnit[15].x_world = 299;  cstrMobilUnit[15].y_world = 29;   cstrMobilUnit[0].batiment = 18; // tanks
    cstrMobilUnit[16].x_world = 414;  cstrMobilUnit[16].y_world = 276;  cstrMobilUnit[0].batiment = 17;
    cstrMobilUnit[17].x_world = 219;  cstrMobilUnit[17].y_world = 354;  cstrMobilUnit[0].batiment = 19;
    cstrMobilUnit[18].x_world = 170;  cstrMobilUnit[18].y_world = 366;  cstrMobilUnit[0].batiment = 20;
    cstrMobilUnit[19].x_world = 73;   cstrMobilUnit[19].y_world = 402;  cstrMobilUnit[0].batiment = 20;

    cstrInitMobileStats( 15 );
}

void cstrInitLevelSearchDoc()
{
    // SearchDoc ships a byte-identical copy of the desert map; only its enemy
    // roster and objective differ.
    cstrInitDesertBuildings();

    cstrMobilUnit[ 0].x_world = 572;  cstrMobilUnit[ 0].y_world = 123;  cstrMobilUnit[ 0].batiment = 1; // infantry
    cstrMobilUnit[ 1].x_world = 309;  cstrMobilUnit[ 1].y_world = 166;  cstrMobilUnit[ 1].batiment = 3;
    cstrMobilUnit[ 2].x_world = 586;  cstrMobilUnit[ 2].y_world = 275;  cstrMobilUnit[ 2].batiment = 4;
    cstrMobilUnit[ 3].x_world = 366;  cstrMobilUnit[ 3].y_world = 455;  cstrMobilUnit[ 3].batiment = 5;
    cstrMobilUnit[ 4].x_world = 617;  cstrMobilUnit[ 4].y_world = 520;  cstrMobilUnit[ 4].batiment = 6;
    cstrMobilUnit[ 5].x_world = 550;  cstrMobilUnit[ 5].y_world = 535;  cstrMobilUnit[ 5].batiment = 7;
    cstrMobilUnit[ 6].x_world = 519;  cstrMobilUnit[ 6].y_world = 592;  cstrMobilUnit[ 6].batiment = 8;
    cstrMobilUnit[ 7].x_world = 93;   cstrMobilUnit[ 7].y_world = 586;  cstrMobilUnit[ 7].batiment = 2;
    // SearchDoc's own copy of the tank block is already correct upstream -
    // each tank gets its own batiment, not unit 0's.
    cstrMobilUnit[ 8].x_world = 379;  cstrMobilUnit[ 8].y_world = 23;   cstrMobilUnit[ 8].batiment = 18; // tanks
    cstrMobilUnit[ 9].x_world = 73;   cstrMobilUnit[ 9].y_world = 284;  cstrMobilUnit[ 9].batiment = 17;
    cstrMobilUnit[10].x_world = 411;  cstrMobilUnit[10].y_world = 398;  cstrMobilUnit[10].batiment = 19;
    cstrMobilUnit[11].x_world = 576;  cstrMobilUnit[11].y_world = 531;  cstrMobilUnit[11].batiment = 20;

    cstrInitMobileStats( 8 );
}

void cstrInitLevel()
{
    cstrDestroyedBuildings = 0;
    cstrMoney = 0;
    cstrSfxRequest = -1;

    cstrNbHeliport = 5;
    cstrNbBuildingFriend = 9;
    cstrNbBuildingHostile = 21;
    cstrNbMobileUnit = CSTR_MAX_MOBILE;

    // Per-mission enemy behaviour, exactly as each mission's own upstream
    // sketch shipped it (see this file's own header comment).
    if( cstrLvl == CSTR_MISSION_CONVOI )
    {
        cstrHasMobileUnits = true;
        cstrHasResurrection = false;
    }
    else if( cstrLvl == CSTR_MISSION_SEARCHDOC )
    {
        cstrHasMobileUnits = true;
        cstrHasResurrection = true;
        cstrNbMobileUnit = 12;
    }
    else
    {
        cstrHasMobileUnits = ( cstrDifficulty > 0 );
        cstrHasResurrection = ( cstrDifficulty == 2 );
    }

    // Convoi state
    cstrCptExplosion = 0;
    cstrWaitTime = CSTR_WAIT_TIME;
    cstrCurrentCheckPoint = 0;
    cstrConvoiSecuriser = false;

    // SearchDoc state - upstream's own `gb.pickRandomSeed()` call kept for
    // fidelity (a documented no-op in this shim).
    gbPickRandomSeed();
    cstrFindData = false;
    cstrWherIsData = cstrPossiblePosData[ arand( CSTR_NB_POSSIBLE_POS ) ];

    if( cstrLvl == CSTR_MISSION_FOREST )        cstrInitLevelForest();
    else if( cstrLvl == CSTR_MISSION_CONVOI )   cstrInitLevelConvoi();
    else if( cstrLvl == CSTR_MISSION_SEARCHDOC )cstrInitLevelSearchDoc();
    else                                        cstrInitLevelDesert();
}

void cstrInitFriendMobile()
{
    cstrCamion.x_world = 117;
    cstrCamion.y_world = 63;
    cstrCamion.width = 16;
    cstrCamion.height = 14;
    cstrCamion.animBoom = 14;
    cstrCamion.animDamage = 0;
    cstrCamion.dir = 0;
    cstrCamion.life = CSTR_MAX_LIFE_CAM;
    cstrConvoiSecuriser = false;
}

void cstrStartMission()
{
    cstrInitLevel();
    cstrInitPlayer();
    cstrInitFriendMobile();
    cstrCptAnim = 0;
    cstrState = CSTR_STATE_GAME;
}

// -----------------------------------------------------------------------------
//   Enemy AI / fire
// -----------------------------------------------------------------------------

void cstrMoveUnit( int i )
{
    if( cstrLvl == CSTR_MISSION_CONVOI )
    {
        // Convoi's own variant: the mobile enemies converge on the escorted
        // truck rather than the player, and only inside a real distance band.
        int dist = gbAbsInt( cstrCamion.x_world - cstrMobilUnit[ i ].x_world )
                 + gbAbsInt( cstrCamion.y_world - cstrMobilUnit[ i ].y_world );

        if( cstrMobilUnit[ i ].life <= 0 ) return;
        if( dist >= 100 ) return;
        if( dist <= 30 ) return;

        cstrCheck02 = 8;

        if( cstrMobilUnit[ i ].x_world + ( cstrMobilUnit[ i ].width / 2 ) > cstrCamion.x_world + 10 )
        {
            cstrMobilUnit[ i ].x_world--;
            cstrMobilUnit[ i ].dir = 4;
            cstrCheck02 = 4;
        }

        if( cstrMobilUnit[ i ].x_world + ( cstrMobilUnit[ i ].width / 2 ) < cstrCamion.x_world - 10 )
        {
            cstrMobilUnit[ i ].x_world++;
            cstrMobilUnit[ i ].dir = 0;
            cstrCheck02 = 0;
        }

        if( cstrMobilUnit[ i ].y_world + ( cstrMobilUnit[ i ].height / 2 ) < cstrCamion.y_world - 8 )
        {
            cstrMobilUnit[ i ].y_world++;
            if( cstrCheck02 == 0 )      cstrMobilUnit[ i ].dir = 1;
            else if( cstrCheck02 == 4 ) cstrMobilUnit[ i ].dir = 3;
            else                        cstrMobilUnit[ i ].dir = 2;
        }

        if( cstrMobilUnit[ i ].y_world + ( cstrMobilUnit[ i ].height / 2 ) > cstrCamion.y_world + 8 )
        {
            cstrMobilUnit[ i ].y_world--;
            if( cstrCheck02 == 0 )      cstrMobilUnit[ i ].dir = 7;
            else if( cstrCheck02 == 4 ) cstrMobilUnit[ i ].dir = 5;
            else                        cstrMobilUnit[ i ].dir = 6;
        }

        return;
    }

    if( cstrMobilUnit[ i ].life <= 0 )
      return;

    cstrCheck02 = 8;

    if( cstrMobilUnit[ i ].x_world + ( cstrMobilUnit[ i ].width / 2 ) > cstrPlayer.x_world + 10 )
    {
        cstrMobilUnit[ i ].x_world--;
        cstrMobilUnit[ i ].dir = 4;
        cstrCheck02 = 4;
    }

    if( cstrMobilUnit[ i ].x_world + ( cstrMobilUnit[ i ].width / 2 ) < cstrPlayer.x_world - 10 )
    {
        cstrMobilUnit[ i ].x_world++;
        cstrMobilUnit[ i ].dir = 0;
        cstrCheck02 = 0;
    }

    if( cstrMobilUnit[ i ].y_world + ( cstrMobilUnit[ i ].height / 2 ) < cstrPlayer.y_world - 8 )
    {
        cstrMobilUnit[ i ].y_world++;
        if( cstrCheck02 == 0 )      cstrMobilUnit[ i ].dir = 1;
        else if( cstrCheck02 == 4 ) cstrMobilUnit[ i ].dir = 3;
        else                        cstrMobilUnit[ i ].dir = 2;
    }

    if( cstrMobilUnit[ i ].y_world + ( cstrMobilUnit[ i ].height / 2 ) > cstrPlayer.y_world + 8 )
    {
        cstrMobilUnit[ i ].y_world--;
        if( cstrCheck02 == 0 )      cstrMobilUnit[ i ].dir = 7;
        else if( cstrCheck02 == 4 ) cstrMobilUnit[ i ].dir = 5;
        else                        cstrMobilUnit[ i ].dir = 6;
    }
}

// Upstream's own 8-way aiming ladder, shared by towers/bunkers and infantry
void cstrAimBullet( int j, int coordx, int coordy )
{
    cstrBullet[ j ].dir = 0;
    if( cstrPlayer.x_world > coordx + 5 && cstrPlayer.y_world - cstrPlayer.altitude > coordy + 5 )                                                                cstrBullet[ j ].dir = 1;
    if( cstrPlayer.x_world > coordx - 6 && cstrPlayer.x_world < coordx + 6 && cstrPlayer.y_world - cstrPlayer.altitude > coordy )                                 cstrBullet[ j ].dir = 2;
    if( cstrPlayer.x_world < coordx - 5 && cstrPlayer.y_world - cstrPlayer.altitude > coordy + 5 )                                                                cstrBullet[ j ].dir = 3;
    if( cstrPlayer.x_world < coordx   && cstrPlayer.y_world - cstrPlayer.altitude > coordy - 6 && cstrPlayer.y_world - cstrPlayer.altitude < coordy + 6 )         cstrBullet[ j ].dir = 4;
    if( cstrPlayer.x_world < coordx - 5 && cstrPlayer.y_world - cstrPlayer.altitude < coordy - 5 )                                                                cstrBullet[ j ].dir = 5;
    if( cstrPlayer.x_world > coordx - 6 && cstrPlayer.x_world < coordx + 6 && cstrPlayer.y_world - cstrPlayer.altitude < coordy - 6 )                             cstrBullet[ j ].dir = 6;
    if( cstrPlayer.x_world > coordx + 5 && cstrPlayer.y_world - cstrPlayer.altitude < coordy - 5 )                                                                cstrBullet[ j ].dir = 7;
}

void cstrInitEnnemyFire()
{
    int i;
    int j;

    // move bullets
    for( j = 0; j < CSTR_MAXBULLET; j++ )
    {
        if( cstrBullet[ j ].distance <= 0 )
          continue;

        if( cstrBullet[ j ].dir == 0 ) cstrBullet[ j ].x_world = cstrBullet[ j ].x_world + 2;
        if( cstrBullet[ j ].dir == 1 ) { cstrBullet[ j ].x_world += 1; cstrBullet[ j ].y_world += 1; }
        if( cstrBullet[ j ].dir == 2 ) cstrBullet[ j ].y_world = cstrBullet[ j ].y_world + 2;
        if( cstrBullet[ j ].dir == 3 ) { cstrBullet[ j ].x_world -= 1; cstrBullet[ j ].y_world += 1; }
        if( cstrBullet[ j ].dir == 4 ) cstrBullet[ j ].x_world = cstrBullet[ j ].x_world - 2;
        if( cstrBullet[ j ].dir == 5 ) { cstrBullet[ j ].x_world -= 1; cstrBullet[ j ].y_world -= 1; }
        if( cstrBullet[ j ].dir == 6 ) cstrBullet[ j ].y_world = cstrBullet[ j ].y_world - 2;
        if( cstrBullet[ j ].dir == 7 ) { cstrBullet[ j ].x_world += 1; cstrBullet[ j ].y_world -= 1; }

        cstrBullet[ j ].distance++;
        if( cstrBullet[ j ].distance > 30 )
        {
            // Upstream clears the shooter's cooldown through building_hostile
            // even for a bullet fired by a mobile unit - the index is always
            // in range, it just credits the wrong object. Kept.
            cstrBuildingHostile[ cstrBullet[ j ].shooter ].fireTimer = 0;
            cstrBullet[ j ].x_world = 0;
            cstrBullet[ j ].y_world = 0;
            cstrBullet[ j ].dir = 0;
            cstrBullet[ j ].distance = 0;
        }
    }

    // define new
    for( i = 0; i < cstrNbBuildingHostile; i++ )
    {
        cstrCoordx = cstrBuildingHostile[ i ].x_world + ( cstrBuildingHostile[ i ].width / 2 );
        cstrCoordy = cstrBuildingHostile[ i ].y_world + ( cstrBuildingHostile[ i ].height / 2 );

        if( gbAbsInt( cstrPlayer.x_world - cstrCoordx ) < 75
        &&  gbAbsInt( cstrPlayer.y_world - cstrCoordy ) < 75
        &&  cstrBuildingHostile[ i ].fireTimer < 1
        &&  cstrBuildingHostile[ i ].life > 0 )
        {
            cstrCheck01 = 0;
            for( j = 0; j < CSTR_MAXBULLET; j++ )
            {
                if( cstrBullet[ j ].distance == 0 && cstrCheck01 == 0 )
                {
                    cstrCheck01 = 1;
                    cstrBuildingHostile[ i ].fireTimer = 20;
                    // sprite 7 is the enemy CAMP under the merged numbering,
                    // never a hostile building - so this case never fires and
                    // towers keep the 20 set above (== TMPTOUR anyway). Kept
                    // exactly as upstream wrote it.
                    if( cstrBuildingHostile[ i ].sprite == 6 ) cstrBuildingHostile[ i ].fireTimer = CSTR_TMPBUNKER;
                    if( cstrBuildingHostile[ i ].sprite == 7 ) cstrBuildingHostile[ i ].fireTimer = CSTR_TMPTOUR;

                    cstrBullet[ j ].shooter = i;
                    cstrBullet[ j ].x_world = cstrCoordx;
                    cstrBullet[ j ].y_world = cstrCoordy;
                    cstrBullet[ j ].distance = 1;
                    cstrAimBullet( j, cstrCoordx, cstrCoordy );
                }
            }
        }
    }

    if( !cstrHasMobileUnits )
      return;

    for( i = 0; i < cstrNbMobileUnit; i++ )
    {
        cstrCoordx = cstrMobilUnit[ i ].x_world + ( cstrMobilUnit[ i ].width / 2 );
        cstrCoordy = cstrMobilUnit[ i ].y_world + ( cstrMobilUnit[ i ].height / 2 );

        if( gbAbsInt( cstrPlayer.x_world - cstrCoordx ) < 75
        &&  gbAbsInt( cstrPlayer.y_world - cstrCoordy ) < 75
        &&  cstrMobilUnit[ i ].fireTimer < 1
        &&  cstrMobilUnit[ i ].life > 0 )
        {
            cstrCheck01 = 0;
            for( j = 0; j < CSTR_MAXBULLET; j++ )
            {
                if( cstrBullet[ j ].distance == 0 && cstrCheck01 == 0 )
                {
                    cstrCheck01 = 1;
                    cstrBullet[ j ].shooter = i;
                    cstrBullet[ j ].x_world = cstrCoordx;
                    cstrBullet[ j ].y_world = cstrCoordy;
                    cstrBullet[ j ].distance = 1;

                    if( cstrMobilUnit[ i ].sprite == 0 ) // infantry
                    {
                        cstrMobilUnit[ i ].fireTimer = CSTR_TMPUNIT;
                        cstrAimBullet( j, cstrCoordx, cstrCoordy );
                    }
                    else // tank - fires straight ahead, from its own turret
                    {
                        cstrMobilUnit[ i ].fireTimer = CSTR_TMPTANK;
                        cstrBullet[ j ].dir = cstrMobilUnit[ i ].dir;
                        cstrBullet[ j ].y_world = cstrBullet[ j ].y_world - 5;
                    }
                }
            }
        }
    }
}

void cstrHitPlayer()
{
    if( cstrPlayer.life > 0 )
      cstrPlayer.life = cstrPlayer.life - 5;

    if( cstrPlayer.life <= 0 )
    {
        cstrPlayer.isCrashing = 1;
    }
    else
    {
        cstrPlayer.animDamage = 12;
        cstrPlayer.isLanding = 0;
    }
}

bool cstrBulletHitsPlayer( int j )
{
    if( cstrBullet[ j ].x_world <= cstrPlayer.x_world - 4 ) return false;
    if( cstrBullet[ j ].x_world >= cstrPlayer.x_world + 4 ) return false;
    if( cstrBullet[ j ].y_world <= cstrPlayer.y_world - cstrPlayer.altitude - 6 ) return false;
    if( cstrBullet[ j ].y_world >= cstrPlayer.y_world - cstrPlayer.altitude + 2 ) return false;

    return true;
}

void cstrCheckEnnemyFire()
{
    int j;

    if( cstrLvl == CSTR_MISSION_CONVOI )
    {
        // Convoi's own variant: the animDamage gate is re-tested per bullet
        // (so only one hit lands per tick) and a parked bullet at (0,0) is
        // explicitly excluded - both differences from the shared build below,
        // kept exactly as each sketch wrote them.
        for( j = 0; j < CSTR_MAXBULLET; j++ )
        {
            if( cstrPlayer.animDamage == 0 && cstrBullet[ j ].distance > 0 )
              if( cstrBulletHitsPlayer( j ) )
                cstrHitPlayer();

            if( cstrCamion.animDamage == 0 )
            {
                if( gbCollidePointRect( cstrBullet[ j ].x_world, cstrBullet[ j ].y_world,
                                        cstrCamion.x_world, cstrCamion.y_world, 14, 14 ) )
                {
                    if( cstrCamion.life > 0 )
                      cstrCamion.life = cstrCamion.life - 5;

                    if( cstrCamion.life <= 0 )
                    {
                        // Upstream's own "TODO faire un truc mieux !" - losing
                        // the truck simply kills the player too.
                        cstrPlayer.life = 0;
                        cstrPlayer.isCrashing = 1;
                    }
                    else
                    {
                        cstrCamion.animDamage = 12;
                    }
                }
            }
        }

        return;
    }

    // The shared build tests animDamage ONCE, outside the loop, so several
    // bullets arriving on the same tick each take 5 life. Kept.
    if( cstrPlayer.animDamage != 0 )
      return;

    for( j = 0; j < CSTR_MAXBULLET; j++ )
      if( cstrBulletHitsPlayer( j ) )
        cstrHitPlayer();
}

void cstrResurection()
{
    // Upstream's own loop bound: 15 in the shared build, NB_MOBILE_UNIT in
    // SearchDoc's own copy.
    int limit = 15;
    if( cstrLvl == CSTR_MISSION_SEARCHDOC )
      limit = cstrNbMobileUnit;
    if( limit > cstrNbMobileUnit )
      limit = cstrNbMobileUnit;

    int i;
    for( i = 0; i < limit; i++ )
    {
        if( cstrMobilUnit[ i ].batiment == 0 )               continue;
        if( cstrMobilUnit[ i ].life != 0 )                   continue;
        if( cstrMobilUnit[ i ].animBoom != CSTR_TMPRESURECTION ) continue;

        int b = cstrMobilUnit[ i ].batiment;

        if( cstrMobilUnit[ i ].sprite == 0 )
        {
            // Bounded: the tank copy-paste bug above can leave an infantry
            // unit's batiment pointing past this 9-entry array (see header).
            if( b >= cstrNbBuildingFriend )
              continue;

            if( cstrBuildingFriend[ b ].life > 0 )
            {
                cstrMobilUnit[ i ].life = 5;
                cstrMobilUnit[ i ].animBoom = 0;
                cstrMobilUnit[ i ].x_world = cstrBuildingFriend[ b ].x_world + ( cstrBuildingFriend[ b ].width / 2 );
                cstrMobilUnit[ i ].y_world = cstrBuildingFriend[ b ].y_world + ( cstrBuildingFriend[ b ].height / 2 );
            }
        }

        if( cstrMobilUnit[ i ].sprite == 1 )
        {
            if( b >= cstrNbBuildingHostile )
              continue;

            if( cstrBuildingHostile[ b ].life > 0 )
            {
                cstrMobilUnit[ i ].life = 30;
                cstrMobilUnit[ i ].animBoom = 0;
                cstrMobilUnit[ i ].x_world = cstrBuildingHostile[ b ].x_world + ( cstrBuildingHostile[ b ].width / 2 );
                cstrMobilUnit[ i ].y_world = cstrBuildingHostile[ b ].y_world + cstrBuildingHostile[ b ].height + 5;
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Player checks
// -----------------------------------------------------------------------------

void cstrCheckPlayerAltitude()
{
    if( cstrPlayer.isLanding == 0 )
    {
        if( cstrPlayer.altitude < CSTR_MAXALTITUDE && cstrPlayer.isCrashing == 0 )
          cstrPlayer.altitude++;
    }
    else
    {
        if( cstrPlayer.altitude > 0 )
          cstrPlayer.altitude--;
    }

    if( cstrPlayer.isCrashing == 1 && cstrPlayer.altitude > 0 )
    {
        cstrPlayer.dir = ( cstrPlayer.dir + 1 ) % 8;
        cstrPlayer.altitude--;
    }
}

void cstrCheckPlayerFire()
{
    if( cstrPlayer.fire != 1 )
      return;

    if( cstrPlayer.dir == 0 ) { cstrCoordx = cstrPlayer.x_world + 16; cstrCoordy = cstrPlayer.y_world;     }
    if( cstrPlayer.dir == 1 ) { cstrCoordx = cstrPlayer.x_world + 8;  cstrCoordy = cstrPlayer.y_world + 5; }
    if( cstrPlayer.dir == 2 ) { cstrCoordx = cstrPlayer.x_world;      cstrCoordy = cstrPlayer.y_world + 8; }
    if( cstrPlayer.dir == 3 ) { cstrCoordx = cstrPlayer.x_world - 8;  cstrCoordy = cstrPlayer.y_world + 5; }
    if( cstrPlayer.dir == 4 ) { cstrCoordx = cstrPlayer.x_world - 16; cstrCoordy = cstrPlayer.y_world;     }
    if( cstrPlayer.dir == 5 ) { cstrCoordx = cstrPlayer.x_world - 8;  cstrCoordy = cstrPlayer.y_world - 5; }
    if( cstrPlayer.dir == 6 ) { cstrCoordx = cstrPlayer.x_world;      cstrCoordy = cstrPlayer.y_world - 8; }
    if( cstrPlayer.dir == 7 ) { cstrCoordx = cstrPlayer.x_world + 8;  cstrCoordy = cstrPlayer.y_world - 5; }

    int i;
    for( i = 0; i < cstrNbBuildingHostile; i++ )
    {
        if( cstrCoordx > cstrBuildingHostile[ i ].x_world
        &&  cstrCoordx < cstrBuildingHostile[ i ].x_world + cstrBuildingHostile[ i ].width
        &&  cstrCoordy > cstrBuildingHostile[ i ].y_world
        &&  cstrCoordy < cstrBuildingHostile[ i ].y_world + cstrBuildingHostile[ i ].height )
        {
            if( cstrBuildingHostile[ i ].life > 0 )
            {
                cstrBuildingHostile[ i ].life--;
                if( cstrBuildingHostile[ i ].life == 0 && cstrBuildingHostile[ i ].animBoom == 0 )
                {
                    cstrDestroyedBuildings = cstrDestroyedBuildings + 1;

                    // SearchDoc's own objective: the data is inside one
                    // randomly-chosen bunker.
                    if( cstrLvl == CSTR_MISSION_SEARCHDOC && i == cstrWherIsData )
                      cstrFindData = true;

                    if( cstrBuildingHostile[ i ].sprite == 8 ) cstrMoney = cstrMoney + 250;  // tower
                    if( cstrBuildingHostile[ i ].sprite == 6 ) cstrMoney = cstrMoney + 1000; // bunker
                }
            }
        }
    }

    for( i = 0; i < cstrNbBuildingFriend; i++ )
    {
        if( cstrCoordx > cstrBuildingFriend[ i ].x_world
        &&  cstrCoordx < cstrBuildingFriend[ i ].x_world + cstrBuildingFriend[ i ].width
        &&  cstrCoordy > cstrBuildingFriend[ i ].y_world
        &&  cstrCoordy < cstrBuildingFriend[ i ].y_world + cstrBuildingFriend[ i ].height )
        {
            if( cstrBuildingFriend[ i ].life > 0 )
            {
                cstrBuildingFriend[ i ].life--;
                if( cstrBuildingFriend[ i ].life == 0 && cstrDifficulty == 2 && cstrBuildingFriend[ i ].animBoom == 0 )
                {
                    if( cstrBuildingFriend[ i ].sprite == 4 ) cstrMoney = cstrMoney + 200; // village
                    if( cstrBuildingFriend[ i ].sprite == 7 ) cstrMoney = cstrMoney + 350; // camp
                    if( cstrBuildingFriend[ i ].sprite == 9 ) cstrMoney = cstrMoney + 200; // village
                }
            }
        }
    }

    if( !cstrHasMobileUnits )
      return;

    for( i = 0; i < cstrNbMobileUnit; i++ )
    {
        if( cstrCoordx > cstrMobilUnit[ i ].x_world
        &&  cstrCoordx < cstrMobilUnit[ i ].x_world + cstrMobilUnit[ i ].width
        &&  cstrCoordy > cstrMobilUnit[ i ].y_world
        &&  cstrCoordy < cstrMobilUnit[ i ].y_world + cstrMobilUnit[ i ].height )
        {
            if( cstrMobilUnit[ i ].life > 0 )
            {
                cstrMobilUnit[ i ].life--;
                if( cstrMobilUnit[ i ].life == 0 && cstrDifficulty == 1 && cstrMobilUnit[ i ].animBoom == 0 )
                {
                    // Upstream switches on building_friend[i].sprite here, not
                    // the unit's own - dead code (friend buildings are only
                    // ever sprite 4/5/7), kept as such and index-bounded.
                    if( i < cstrNbBuildingFriend )
                    {
                        if( cstrBuildingFriend[ i ].sprite == 0 ) cstrMoney = cstrMoney + 50;  // unit
                        if( cstrBuildingFriend[ i ].sprite == 1 ) cstrMoney = cstrMoney + 100; // tank
                    }
                }
            }
        }
    }
}

void cstrCheckFuel()
{
    if( cstrPlayer.altitude <= 0 )
      return;

    cstrPlayer.fuelCheck = ( cstrPlayer.fuelCheck + 1 ) % 100;
    if( cstrPlayer.fuelCheck == 0 && cstrPlayer.fuel > 0 )
      cstrPlayer.fuel--;

    if( cstrPlayer.fuel == 0 )
    {
        cstrPlayer.isCrashing = 1;
        cstrPlayer.isLanding = 1;
    }
}

void cstrAnimation()
{
    if( cstrPlayer.fuel > 0 && cstrPlayer.altitude > 0 )
      cstrPlayer.animHelix = ( cstrPlayer.animHelix + 1 ) % 4;

    cstrCptAnim = ( cstrCptAnim + 1 ) % 50;

    if( cstrPlayer.isCrashing == 1 && cstrPlayer.altitude == 0 && cstrPlayer.animBoom < 11 )
      cstrPlayer.animBoom++;

    if( cstrPlayer.animDamage > 0 )
      cstrPlayer.animDamage--;

    if( cstrLvl == CSTR_MISSION_CONVOI && cstrCamion.animDamage > 0 )
      cstrCamion.animDamage--;

    int i;
    for( i = 0; i < cstrNbBuildingHostile; i++ )
    {
        if( cstrBuildingHostile[ i ].life == 0 && cstrBuildingHostile[ i ].animBoom < 12 )
          cstrBuildingHostile[ i ].animBoom++;
        if( cstrBuildingHostile[ i ].fireTimer > 0 )
          cstrBuildingHostile[ i ].fireTimer--;
    }

    for( i = 0; i < cstrNbBuildingFriend; i++ )
      if( cstrBuildingFriend[ i ].life == 0 && cstrBuildingFriend[ i ].animBoom < 12 )
        cstrBuildingFriend[ i ].animBoom++;

    if( !cstrHasMobileUnits )
      return;

    for( i = 0; i < cstrNbMobileUnit; i++ )
    {
        if( cstrMobilUnit[ i ].life == 0 && cstrMobilUnit[ i ].animBoom < CSTR_TMPRESURECTION + 1 )
          cstrMobilUnit[ i ].animBoom++;
        if( cstrMobilUnit[ i ].fireTimer > 0 )
          cstrMobilUnit[ i ].fireTimer--;
    }
}

// -----------------------------------------------------------------------------
//   Drawing
// -----------------------------------------------------------------------------

void cstrDrawPlayer()
{
    cstrCoordx = cstrPlayerXpos( cstrPlayer.x_world );
    cstrCoordy = cstrPlayerYpos( cstrPlayer.y_world );
    gbDrawBitmap( cstrCoordx - 2, cstrCoordy - 2, cstrCopterShadowBitmap );

    if( cstrPlayer.animDamage % 3 != 0 )
      return;

    gbDrawBitmap( cstrCoordx - 7, cstrCoordy - cstrPlayer.altitude - 8, cstrHelix[ cstrPlayer.animHelix ] );

    int ax = cstrCoordx;
    int ay = cstrCoordy - cstrPlayer.altitude;

    if( cstrPlayer.dir == 0 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 9, ay - 6, cstrCopterProfileMaskBitmap, 0, 0 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 9, ay - 6, cstrCopterProfileBitmap, 0, 0 );
    }
    else if( cstrPlayer.dir == 1 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 7, ay - 13, cstrCopterDiag1MaskBitmap, 0, 0 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 7, ay - 13, cstrCopterDiag1Bitmap, 0, 0 );
    }
    else if( cstrPlayer.dir == 2 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 4, ay - 6, cstrCopterUpDownMaskBitmap, 0, 0 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 4, ay - 6, cstrCopterDownBitmap, 0, 0 );
    }
    else if( cstrPlayer.dir == 3 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 9, ay - 13, cstrCopterDiag1MaskBitmap, 0, 1 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 9, ay - 13, cstrCopterDiag1Bitmap, 0, 1 );
    }
    else if( cstrPlayer.dir == 4 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 7, ay - 6, cstrCopterProfileMaskBitmap, 0, 1 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 7, ay - 6, cstrCopterProfileBitmap, 0, 1 );
    }
    else if( cstrPlayer.dir == 5 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 7, ay - 6, cstrCopterDiag2MaskBitmap, 0, 1 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 7, ay - 6, cstrCopterDiag2Bitmap, 0, 1 );
    }
    else if( cstrPlayer.dir == 6 )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 4, ay - 6, cstrCopterUpDownMaskBitmap, 0, 0 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 4, ay - 6, cstrCopterUpBitmap, 0, 0 );
    }
    else
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( ax - 9, ay - 6, cstrCopterDiag2MaskBitmap, 0, 0 );
        gbSetColor( GB_BLACK );
        gbDrawBitmapRotated( ax - 9, ay - 6, cstrCopterDiag2Bitmap, 0, 0 );
    }
}

void cstrDrawPlayerFire()
{
    if( cstrPlayer.fire != 1 )
      return;

    cstrSoundFx( 0 );
    cstrCoordx = cstrPlayerXpos( cstrPlayer.x_world );
    cstrCoordy = cstrPlayerYpos( cstrPlayer.y_world );

    int cx = cstrCoordx;
    int cy = cstrCoordy;
    int alt = cstrPlayer.altitude;
    int* imp = cstrImpact[ cstrCptAnim % 3 ];
    bool tracer = ( cstrCptAnim % 4 == 0 );

    if( cstrPlayer.dir == 0 )
    {
        if( tracer ) gbDrawLine( cx + 6, cy - alt, cx + 16, cy - 0 );
        gbDrawBitmap( cx + 12, cy - 4, imp );
    }
    else if( cstrPlayer.dir == 1 )
    {
        if( tracer ) gbDrawLine( cx + 3, cy - ( alt - 1 ), cx + 8, cy + 5 );
        gbDrawBitmap( cx + 4, cy + 1, imp );
    }
    else if( cstrPlayer.dir == 2 )
    {
        if( tracer ) gbDrawLine( cx + 0, cy - alt, cx + 0, cy + 8 );
        gbDrawBitmap( cx - 4, cy + 4, imp );
    }
    else if( cstrPlayer.dir == 3 )
    {
        if( tracer ) gbDrawLine( cx - 3, cy - ( alt - 1 ), cx - 8, cy + 5 );
        gbDrawBitmapRotated( cx - 12, cy + 1, imp, 0, 1 );
    }
    else if( cstrPlayer.dir == 4 )
    {
        if( tracer ) gbDrawLine( cx - 6, cy - alt, cx - 16, cy + 0 );
        gbDrawBitmapRotated( cx - 20, cy - 4, imp, 0, 1 );
    }
    else if( cstrPlayer.dir == 5 )
    {
        if( tracer ) gbDrawLine( cx - 5, cy - ( alt + 2 ), cx - 8, cy - 5 );
        gbDrawBitmapRotated( cx - 12, cy - 9, imp, 0, 1 );
    }
    else if( cstrPlayer.dir == 6 )
    {
        if( tracer ) gbDrawLine( cx + 0, cy - alt, cx + 0, cy - 8 );
        gbDrawBitmapRotated( cx - 4, cy - 12, imp, 0, 1 );
    }
    else
    {
        if( tracer ) gbDrawLine( cx + 5, cy - ( alt + 2 ), cx + 8, cy - 5 );
        gbDrawBitmap( cx + 4, cy - 9, imp );
    }
}

void cstrDrawEnnemyFire()
{
    int i;
    for( i = 0; i < CSTR_MAXBULLET; i++ )
    {
        if( cstrBullet[ i ].distance <= 0 )
          continue;

        if( cstrBullet[ i ].distance == 1 )
          cstrSoundFx( 0 );

        cstrCoordx = cstrLndscapeXpos( cstrBullet[ i ].x_world );
        cstrCoordy = cstrLndscapeYpos( cstrBullet[ i ].y_world );
        if( cstrCoordx > 0 && cstrCoordx < CSTR_SCREENWIDTH && cstrCoordy > 0 && cstrCoordy < CSTR_SCREENHEIGHT )
          gbFillRect( cstrCoordx, cstrCoordy, 2, 2 );
    }
}

// True once this mission's own objective is complete
bool cstrObjectiveDone()
{
    if( cstrLvl == CSTR_MISSION_CONVOI )
      return cstrConvoiSecuriser;
    if( cstrLvl == CSTR_MISSION_SEARCHDOC )
      return cstrFindData;

    return ( cstrDestroyedBuildings == cstrNbBuildingHostile );
}

void cstrDrawHUD()
{
    gbSetColor( GB_WHITE );
    gbFillRect( 0, 0, 84, 5 );
    gbSetColor( GB_BLACK );

    if( cstrObjectiveDone() && cstrCptAnim < 15 && cstrPlayer.altitude > 0 )
    {
        gbPrintString( "    Return to base." );
        return;
    }

    if( cstrLvl == CSTR_MISSION_CONVOI )
    {
        // Convoi's own wider 3-gauge HUD plate, at its own real x
        gbDrawBitmap( 21, 1, cstrHudConvoiBitmap );
    }
    else
    {
        gbDrawBitmap( 42, 1, cstrHudBitmap );
    }

    if( cstrPlayer.moveMode == 0 )
    {
        gbPrintString( "$" );
        gbPrintNumber( cstrMoney );
    }
    else
    {
        gbPrintNumber( cstrDestroyedBuildings );
        gbPrintString( "/" );
        gbPrintNumber( cstrNbBuildingHostile );
    }

    if( cstrLvl == CSTR_MISSION_CONVOI )
      gbFillRect( 29, 2, cstrCamion.life / ( CSTR_MAX_LIFE_CAM / 10 ), 2 ); // truck integrity

    gbFillRect( 49, 2, cstrPlayer.life / ( CSTR_MAXLIFE / 10 ), 2 ); // life
    gbFillRect( 66, 2, cstrPlayer.fuel / ( CSTR_MAXFUEL / 10 ), 2 ); // fuel

    gbCursorX = 79;
    gbCursorY = 0;
    if( cstrPlayer.moveMode == 0 ) gbPrintString( "@  " );
    else                           gbPrintString( "-  " );
}

void cstrAnimBoom()
{
    if( cstrPlayer.isCrashing == 1 && cstrPlayer.altitude == 0 && cstrPlayer.animBoom < 11 )
    {
        cstrSoundFx( 1 );
        cstrCoordx = cstrPlayerXpos( cstrPlayer.x_world ) - 2;
        cstrCoordy = cstrPlayerYpos( cstrPlayer.y_world ) - 10;
        gbDrawBitmap( cstrCoordx, cstrCoordy, cstrBoom[ cstrPlayer.animBoom ] );
    }

    if( cstrNbBuildingHostile > cstrNbBuildingFriend ) cstrCheck01 = cstrNbBuildingHostile;
    else                                               cstrCheck01 = cstrNbBuildingFriend;
    if( cstrCheck01 < 20 && cstrHasMobileUnits )
      cstrCheck01 = 20;

    int i;
    for( i = 0; i < cstrCheck01; i++ )
    {
        if( i < cstrNbBuildingHostile )
        {
            if( cstrBuildingHostile[ i ].life == 0 && cstrBuildingHostile[ i ].animBoom < 12 )
            {
                cstrSoundFx( 1 );
                cstrCoordx = cstrLndscapeXpos( cstrBuildingHostile[ i ].x_world );
                cstrCoordy = cstrLndscapeYpos( cstrBuildingHostile[ i ].y_world ) - 3;
                if( cstrBuildingHostile[ i ].sprite == 4 )
                {
                    gbDrawBitmap( cstrCoordx + 5,  cstrCoordy,      cstrBoom[ cstrBuildingHostile[ i ].animBoom ] );
                    gbDrawBitmap( cstrCoordx + 20, cstrCoordy + 5,  cstrBoom[ cstrBuildingHostile[ i ].animBoom ] );
                    gbDrawBitmap( cstrCoordx + 8,  cstrCoordy + 15, cstrBoom[ cstrBuildingHostile[ i ].animBoom ] );
                }
                else
                {
                    gbDrawBitmap( cstrCoordx, cstrCoordy, cstrBoom[ cstrBuildingHostile[ i ].animBoom ] );
                }
            }
        }

        if( i < cstrNbBuildingFriend )
        {
            if( cstrBuildingFriend[ i ].life == 0 && cstrBuildingFriend[ i ].animBoom < 12 )
            {
                cstrSoundFx( 1 );
                cstrCoordx = cstrLndscapeXpos( cstrBuildingFriend[ i ].x_world );
                cstrCoordy = cstrLndscapeYpos( cstrBuildingFriend[ i ].y_world ) - 3;
                if( cstrBuildingFriend[ i ].sprite == 4 )
                {
                    gbDrawBitmap( cstrCoordx + 10, cstrCoordy + 3,  cstrBoom[ cstrBuildingFriend[ i ].animBoom ] );
                    gbDrawBitmap( cstrCoordx + 41, cstrCoordy + 4,  cstrBoom[ cstrBuildingFriend[ i ].animBoom ] );
                    gbDrawBitmap( cstrCoordx + 38, cstrCoordy + 24, cstrBoom[ cstrBuildingFriend[ i ].animBoom ] );
                }
                else
                {
                    gbDrawBitmap( cstrCoordx + 3, cstrCoordy, cstrBoom[ cstrBuildingFriend[ i ].animBoom ] );
                }
            }
        }

        if( i < cstrNbMobileUnit && cstrHasMobileUnits )
        {
            if( cstrMobilUnit[ i ].life == 0 && cstrMobilUnit[ i ].animBoom < 12 )
            {
                cstrCoordx = cstrLndscapeXpos( cstrMobilUnit[ i ].x_world ) + 5;
                cstrCoordy = cstrLndscapeYpos( cstrMobilUnit[ i ].y_world ) - 3;
                if( cstrMobilUnit[ i ].sprite == 1 )
                {
                    cstrSoundFx( 1 );
                    gbDrawBitmap( cstrCoordx, cstrCoordy, cstrBoom[ cstrMobilUnit[ i ].animBoom ] );
                }
            }
        }
    }
}

void cstrDrawBackground()
{
    int i;
    for( i = 0; i < CSTR_MAX_BKG; i++ )
    {
        cstrCoordx = cstrBackgrndXpos( cstrBkg[ i ].x_world );
        cstrCoordy = cstrBackgrndYpos( cstrBkg[ i ].y_world );
        gbDrawBitmap( cstrCoordx - 20, cstrCoordy - 20, cstrSpriteBkg[ cstrBkg[ i ].sprite ] );
    }
}

void cstrDrawBaseCamps()
{
    int i;
    for( i = 0; i < CSTR_MAX_BKGRND; i++ )
    {
        cstrCoordx = cstrLndscapeXpos( cstrBkgrnd[ i ].x_world );
        cstrCoordy = cstrLndscapeYpos( cstrBkgrnd[ i ].y_world );
        if( cstrCoordx < CSTR_SCREENWIDTH  && cstrCoordx > 0 - cstrBkgrnd[ i ].width
        &&  cstrCoordy < CSTR_SCREENHEIGHT && cstrCoordy > 0 - cstrBkgrnd[ i ].height )
          gbDrawBitmap( cstrCoordx, cstrCoordy, cstrSprites[ cstrBkgrnd[ i ].sprite ] );
    }
}

void cstrDrawBuildingFriend()
{
    int i;
    for( i = 0; i < cstrNbBuildingFriend; i++ )
    {
        cstrCoordx = cstrLndscapeXpos( cstrBuildingFriend[ i ].x_world );
        cstrCoordy = cstrLndscapeYpos( cstrBuildingFriend[ i ].y_world );
        if( cstrCoordx < CSTR_SCREENWIDTH  && cstrCoordx > 0 - cstrBuildingFriend[ i ].width
        &&  cstrCoordy < CSTR_SCREENHEIGHT && cstrCoordy > 0 - cstrBuildingFriend[ i ].height )
        {
            if( cstrBuildingFriend[ i ].life == 0 && cstrBuildingFriend[ i ].animBoom > 5 )
              gbDrawBitmap( cstrCoordx, cstrCoordy, cstrDestroy[ cstrBuildingFriend[ i ].sprite ] );
            else
              gbDrawBitmap( cstrCoordx, cstrCoordy, cstrSprites[ cstrBuildingFriend[ i ].sprite ] );
        }
    }
}

void cstrDrawBuildingHostile()
{
    int i;
    for( i = 0; i < cstrNbBuildingHostile; i++ )
    {
        cstrCoordx = cstrLndscapeXpos( cstrBuildingHostile[ i ].x_world );
        cstrCoordy = cstrLndscapeYpos( cstrBuildingHostile[ i ].y_world );
        if( cstrCoordx < CSTR_SCREENWIDTH  && cstrCoordx > 0 - cstrBuildingHostile[ i ].width
        &&  cstrCoordy < CSTR_SCREENHEIGHT && cstrCoordy > 0 - cstrBuildingHostile[ i ].height )
        {
            if( cstrBuildingHostile[ i ].life == 0 && cstrBuildingHostile[ i ].animBoom > 5 )
              gbDrawBitmap( cstrCoordx, cstrCoordy, cstrDestroy[ cstrBuildingHostile[ i ].sprite ] );
            else
              gbDrawBitmap( cstrCoordx, cstrCoordy, cstrSprites[ cstrBuildingHostile[ i ].sprite ] );
        }
    }
}

void cstrDrawMobileHostile()
{
    int i;
    for( i = 0; i < cstrNbMobileUnit; i++ )
    {
        bool visible = ( cstrMobilUnit[ i ].life > 0 )
                    || ( cstrMobilUnit[ i ].animBoom < 8 )
                    || ( cstrMobilUnit[ i ].animBoom < 24 && cstrMobilUnit[ i ].animBoom % 4 == 2 );
        if( !visible )
          continue;

        cstrCoordx = cstrLndscapeXpos( cstrMobilUnit[ i ].x_world );
        cstrCoordy = cstrLndscapeYpos( cstrMobilUnit[ i ].y_world );

        // Convoi drives its units from anywhere on the map (upstream comments
        // the on-screen gate out); every other mission only wakes a unit once
        // it is nearly on screen.
        if( cstrLvl == CSTR_MISSION_CONVOI )
        {
            cstrMoveUnit( i );
        }
        else
        {
            if( cstrCoordx < CSTR_SCREENWIDTH + 20  && cstrCoordx > 0 - cstrMobilUnit[ i ].width - 20
            &&  cstrCoordy < CSTR_SCREENHEIGHT + 20 && cstrCoordy > 0 - cstrMobilUnit[ i ].height - 20 )
              cstrMoveUnit( i );
        }

        if( cstrCoordx >= CSTR_SCREENWIDTH  || cstrCoordx <= 0 - cstrMobilUnit[ i ].width )  continue;
        if( cstrCoordy >= CSTR_SCREENHEIGHT || cstrCoordy <= 0 - cstrMobilUnit[ i ].height ) continue;

        if( cstrMobilUnit[ i ].sprite == 0 )
        {
            if( cstrMobilUnit[ i ].life > 0 )
            {
                if( cstrMobilUnit[ i ].dir > 1 && cstrMobilUnit[ i ].dir < 6 )
                  gbDrawBitmapRotated( cstrCoordx, cstrCoordy, cstrEnnemyUnitBitmap, 0, 1 );
                else
                  gbDrawBitmap( cstrCoordx, cstrCoordy, cstrEnnemyUnitBitmap );
            }
            else
            {
                // Upstream's own two branches here are literally identical -
                // a dead unit always lies down (ROTCCW), whichever way it
                // was facing. Collapsed to one call.
                gbDrawBitmapRotated( cstrCoordx, cstrCoordy, cstrEnnemyUnitBitmap, 1, 0 );
            }
        }
        else
        {
            if( cstrMobilUnit[ i ].dir == 0 )      gbDrawBitmap( cstrCoordx, cstrCoordy, cstrTank[ 0 ] );
            else if( cstrMobilUnit[ i ].dir == 1 ) gbDrawBitmap( cstrCoordx, cstrCoordy, cstrTank[ 1 ] );
            else if( cstrMobilUnit[ i ].dir == 2 ) gbDrawBitmap( cstrCoordx, cstrCoordy, cstrTank[ 2 ] );
            else if( cstrMobilUnit[ i ].dir == 3 ) gbDrawBitmapRotated( cstrCoordx, cstrCoordy, cstrTank[ 1 ], 0, 1 );
            else if( cstrMobilUnit[ i ].dir == 4 ) gbDrawBitmapRotated( cstrCoordx, cstrCoordy, cstrTank[ 0 ], 0, 1 );
            else if( cstrMobilUnit[ i ].dir == 5 ) gbDrawBitmapRotated( cstrCoordx, cstrCoordy, cstrTank[ 4 ], 0, 1 );
            else if( cstrMobilUnit[ i ].dir == 6 ) gbDrawBitmap( cstrCoordx, cstrCoordy, cstrTank[ 3 ] );
            else                                   gbDrawBitmap( cstrCoordx, cstrCoordy, cstrTank[ 4 ] );
        }
    }
}

// Convoi's own road layer
void cstrDrawRoute()
{
    int i;
    for( i = 0; i < CSTR_NB_TSPRITE_ROUTE; i++ )
    {
        cstrCoordx = cstrLndscapeXpos( cstrRouteX[ i ] );
        cstrCoordy = cstrLndscapeYpos( cstrRouteY[ i ] );
        if( cstrCoordx >= CSTR_SCREENWIDTH || cstrCoordx <= -16 )  continue;
        if( cstrCoordy >= CSTR_SCREENHEIGHT || cstrCoordy <= -16 ) continue;

        int tile = cstrRouteSprite[ i ];
        if( tile < 6 )
        {
            gbDrawBitmap( cstrCoordx, cstrCoordy, cstrRouteSprites[ tile ] );
        }
        else
        {
            // Tile 6 is the road section the convoy stops at and blows up.
            if( cstrCurrentCheckPoint < 7 )
            {
                gbDrawBitmap( cstrCoordx, cstrCoordy, cstrRouteHBitmap );
            }
            else if( cstrCptExplosion < CSTR_TEMP_EXPOLOSION )
            {
                // Upstream divides by NB_FRAME_EXPLOSION rather than by
                // TEMP_EXPOLOSION/NB_FRAME_EXPLOSION, so frames 5 and 6 of
                // this 7-frame animation are never reached. Kept.
                int index = cstrCptExplosion / CSTR_NB_FRAME_EXPLOSION;
                gbDrawBitmap( cstrCoordx, cstrCoordy - 16, cstrExplosion[ index ] );
            }
            else
            {
                gbDrawBitmap( cstrCoordx, cstrCoordy, cstrRouteExploseBitmap );
            }
        }
    }
}

void cstrDrawCamion()
{
    cstrCoordx = cstrLndscapeXpos( cstrCamion.x_world );
    cstrCoordy = cstrLndscapeYpos( cstrCamion.y_world );
    if( cstrCoordx >= CSTR_SCREENWIDTH || cstrCoordx <= -16 )  return;
    if( cstrCoordy >= CSTR_SCREENHEIGHT || cstrCoordy <= -16 ) return;

    if( cstrCamion.animDamage % 3 == 0 )
      gbDrawBitmap( cstrCoordx, cstrCoordy, cstrCamionSprites[ cstrCamion.dir ] );
}

// Convoi's own escort logic: drive the truck from checkpoint to checkpoint at
// half speed, pausing at three of them (and blowing up the road at one).
void cstrUpdateFriendMobile()
{
    if( cstrCurrentCheckPoint >= CSTR_NB_CHECK_POINT || cstrCamion.life <= 0 )
    {
        cstrConvoiSecuriser = true;
        return;
    }

    int cx = cstrCheckPointX[ cstrCurrentCheckPoint ];
    int cy = cstrCheckPointY[ cstrCurrentCheckPoint ];
    int dx = cx - cstrCamion.x_world;
    int dy = cy - cstrCamion.y_world;

    // Upstream's own `(int)sqrt(dx*dx + dy*dy) < 3`, rewritten to the exactly
    // equivalent squared form: this platform hard-traps on sqrt of a negative
    // value, and integer truncation makes the two tests identical anyway.
    if( dx * dx + dy * dy < 9 )
    {
        if( cstrCurrentCheckPoint == 5 || cstrCurrentCheckPoint == 7 || cstrCurrentCheckPoint == 10 )
        {
            cstrWaitTime--;
            if( cstrWaitTime == 0 )
            {
                cstrWaitTime = CSTR_WAIT_TIME;
                cstrCurrentCheckPoint++;
            }

            if( cstrCurrentCheckPoint == 7 && cstrCptExplosion < CSTR_TEMP_EXPOLOSION )
            {
                if( cstrCptExplosion == 0 )
                  cstrSoundFx( 1 );
                cstrCptExplosion++;
            }
        }
        else
        {
            cstrCurrentCheckPoint++;
        }

        return;
    }

    if( gbFrameCount % 2 == 0 )
      return;

    cstrCamion.dir = 5;
    if( cx > cstrCamion.x_world )
    {
        cstrCamion.x_world++;
        cstrCamion.dir = 0;
    }
    else if( cx < cstrCamion.x_world )
    {
        cstrCamion.x_world--;
        cstrCamion.dir = 3;
    }

    if( cy > cstrCamion.y_world )
    {
        cstrCamion.y_world++;
        cstrCamion.dir += 2;
    }
    else if( cy < cstrCamion.y_world )
    {
        cstrCamion.y_world--;
        cstrCamion.dir += 1;
    }
}

void cstrGameOver()
{
    if( cstrPlayer.life == 0 && cstrPlayer.animBoom == 11 )
    {
        gbSetColor( GB_WHITE );
        gbFillRect( 24, 18, 38, 7 );
        gbSetColor( GB_BLACK );
        gbDrawRect( 23, 17, 40, 9 );
        gbCursorX = 25;
        gbCursorY = 19;
        gbPrintString( "Game Over" );
    }
}

void cstrCongratulation()
{
    gbSetColor( GB_WHITE );
    gbFillRect( 4, 4, 76, 15 );
    gbSetColor( GB_BLACK );
    gbDrawRect( 3, 3, 74, 17 );
    gbCursorX = 9;
    gbCursorY = 5;
    gbPrintString( "Mission Complete" );

    int cx = 40;
    if( cstrMoney >= 10 )     cx = 35;
    if( cstrMoney >= 100 )    cx = 31;
    if( cstrMoney >= 1000 )   cx = 27;
    if( cstrMoney >= 10000 )  cx = 23;
    if( cstrMoney >= 100000 ) cx = 19;

    gbCursorX = cx;
    gbCursorY = 12;
    gbPrintString( "$" );
    gbPrintNumber( cstrMoney );
}

// -----------------------------------------------------------------------------
//   Landing / refuelling / mission end
// -----------------------------------------------------------------------------

void cstrCheckLanding()
{
    int i;

    if( cstrPlayer.vSpeed == 0 && cstrPlayer.hSpeed == 0 && cstrPlayer.isLanding == 0 )
    {
        for( i = 0; i < cstrNbHeliport; i++ )
        {
            if( cstrPlayer.x_world > cstrBkgrnd[ i ].x_world
            &&  cstrPlayer.x_world < cstrBkgrnd[ i ].x_world + cstrBkgrnd[ i ].width
            &&  cstrPlayer.y_world > cstrBkgrnd[ i ].y_world
            &&  cstrPlayer.y_world < cstrBkgrnd[ i ].y_world + cstrBkgrnd[ i ].height )
            {
                // Pad 0 is the home base (repairs AND refuels); 1/3 repair,
                // 2/4 refuel. Landing is only offered if it would do anything.
                if( i == 0 )
                {
                    if( cstrPlayer.life < CSTR_MAXLIFE - 1 || cstrPlayer.fuel < CSTR_MAXFUEL - 1 )
                      cstrPlayer.isLanding = 1;
                }
                else if( i == 1 || i == 3 )
                {
                    if( cstrPlayer.life < CSTR_MAXLIFE - 1 )
                      cstrPlayer.isLanding = 1;
                }
                else
                {
                    if( cstrPlayer.fuel < CSTR_MAXFUEL - 1 )
                      cstrPlayer.isLanding = 1;
                }
            }
        }
    }

    if( cstrPlayer.isLanding != 1 || cstrPlayer.altitude != 0 )
      return;

    for( i = 0; i < cstrNbHeliport; i++ )
    {
        if( cstrPlayer.x_world <= cstrBkgrnd[ i ].x_world )                              continue;
        if( cstrPlayer.x_world >= cstrBkgrnd[ i ].x_world + cstrBkgrnd[ i ].width )      continue;
        if( cstrPlayer.y_world <= cstrBkgrnd[ i ].y_world )                              continue;
        if( cstrPlayer.y_world >= cstrBkgrnd[ i ].y_world + cstrBkgrnd[ i ].height )     continue;

        if( i == 0 )
        {
            if( cstrObjectiveDone() )
            {
                if( cstrLvl == CSTR_MISSION_CONVOI )
                {
                    // Convoi's own `endGameOK()` - a blocking full-screen win
                    // bitmap upstream, a real state here.
                    cstrState = CSTR_STATE_ENDGAME;
                }
                else
                {
                    cstrCongratulation();
                }
            }
            else
            {
                if( cstrPlayer.life == CSTR_MAXLIFE && cstrPlayer.fuel == CSTR_MAXFUEL )
                {
                    cstrPlayer.isLanding = 0;
                }
                else
                {
                    if( cstrPlayer.life < CSTR_MAXLIFE ) cstrPlayer.life++;
                    if( cstrPlayer.fuel < CSTR_MAXFUEL ) cstrPlayer.fuel++;
                    if( cstrPlayer.life == CSTR_MAXLIFE && cstrPlayer.fuel == CSTR_MAXFUEL )
                    {
                        if( cstrMoney < 1000 ) cstrMoney = 0;
                        else                   cstrMoney = cstrMoney - 800;
                    }
                }
            }
        }
        else if( i == 1 || i == 3 )
        {
            if( cstrPlayer.life == CSTR_MAXLIFE )
            {
                cstrPlayer.isLanding = 0;
            }
            else
            {
                cstrPlayer.life++;
                if( cstrPlayer.life == CSTR_MAXLIFE )
                {
                    if( cstrMoney < 750 ) cstrMoney = 0;
                    else                  cstrMoney = cstrMoney - 750;
                }
            }
        }
        else
        {
            if( cstrPlayer.fuel == CSTR_MAXFUEL )
            {
                cstrPlayer.isLanding = 0;
            }
            else
            {
                cstrPlayer.fuel++;
                if( cstrPlayer.fuel == CSTR_MAXFUEL )
                {
                    // Upstream really does test `money < 750` before charging
                    // only 150 here. Kept.
                    if( cstrMoney < 750 ) cstrMoney = 0;
                    else                  cstrMoney = cstrMoney - 150;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Input
// -----------------------------------------------------------------------------

void cstrCheckButtonsMenu()
{
    if( gbPressed( BTN_RIGHT ) )
      cstrLvl = ( cstrLvl + 1 ) % CSTR_MISSION_COUNT;

    if( gbPressed( BTN_LEFT ) )
    {
        if( cstrLvl == 0 ) cstrLvl = CSTR_MISSION_COUNT - 1;
        else               cstrLvl = cstrLvl - 1;
    }

    if( gbPressed( BTN_DOWN ) )
      cstrDifficulty = ( cstrDifficulty + 1 ) % 3;

    if( gbPressed( BTN_UP ) )
    {
        if( cstrDifficulty == 0 ) cstrDifficulty = 2;
        else                      cstrDifficulty = cstrDifficulty - 1;
    }

    if( gbPressed( BTN_A ) )
      cstrStartMission();

    if( gbPressed( BTN_C ) )
      cstrState = CSTR_STATE_TITLE;
}

void cstrCheckButtonsGame()
{
    if( gbPressed( BTN_C ) )
    {
        cstrState = CSTR_STATE_SELECTMAP;
        return;
    }

    if( gbPressed( BTN_B ) )
    {
        if( cstrPlayer.moveMode == 0 ) cstrPlayer.moveMode = 1;
        else                           cstrPlayer.moveMode = 0;
    }

    if( gbRepeat( BTN_A, 0 ) && cstrPlayer.isLanding == 0 && cstrPlayer.altitude == CSTR_MAXALTITUDE )
      cstrPlayer.fire = 1;
    else
      cstrPlayer.fire = 0;

    if( cstrPlayer.isLanding != 0 || cstrPlayer.life <= 0 )
      return;

    if( gbRepeat( BTN_RIGHT, 0 ) )
    {
        if( cstrPlayer.hSpeed < 3 ) cstrPlayer.hSpeed++;
    }
    else if( gbRepeat( BTN_LEFT, 0 ) )
    {
        if( cstrPlayer.hSpeed > -3 ) cstrPlayer.hSpeed--;
    }
    else
    {
        if( cstrPlayer.hSpeed > 0 )      cstrPlayer.hSpeed--;
        else if( cstrPlayer.hSpeed < 0 ) cstrPlayer.hSpeed++;
    }

    if( gbRepeat( BTN_DOWN, 0 ) )
    {
        if( cstrPlayer.vSpeed < 3 ) cstrPlayer.vSpeed++;
    }
    else if( gbRepeat( BTN_UP, 0 ) )
    {
        if( cstrPlayer.vSpeed > -3 ) cstrPlayer.vSpeed--;
    }
    else
    {
        if( cstrPlayer.vSpeed > 0 )      cstrPlayer.vSpeed--;
        else if( cstrPlayer.vSpeed < 0 ) cstrPlayer.vSpeed++;
    }

    int i;
    if( cstrPlayer.hSpeed > 0 )
    {
        for( i = 0; i < cstrPlayer.hSpeed; i++ )
          if( cstrPlayer.x_world < CSTR_LEVELWIDTH ) cstrPlayer.x_world++;
    }
    else if( cstrPlayer.hSpeed < 0 )
    {
        for( i = 0; i < gbAbsInt( cstrPlayer.hSpeed ); i++ )
          if( cstrPlayer.x_world > 0 ) cstrPlayer.x_world--;
    }

    if( cstrPlayer.vSpeed > 0 )
    {
        for( i = 0; i < cstrPlayer.vSpeed; i++ )
          if( cstrPlayer.y_world < CSTR_LEVELHEIGHT ) cstrPlayer.y_world++;
    }
    else if( cstrPlayer.vSpeed < 0 )
    {
        for( i = 0; i < gbAbsInt( cstrPlayer.vSpeed ); i++ )
          if( cstrPlayer.y_world > 13 ) cstrPlayer.y_world--;
    }

    // Upstream re-tests the fire condition a second time here, inside the
    // same tick. Kept - it is a genuine no-op.
    if( gbRepeat( BTN_A, 0 ) && cstrPlayer.isLanding == 0 && cstrPlayer.altitude == CSTR_MAXALTITUDE )
      cstrPlayer.fire = 1;
    else
      cstrPlayer.fire = 0;

    if( cstrPlayer.moveMode == 0 )
    {
        if( cstrPlayer.hSpeed > 0  && cstrPlayer.vSpeed == 0 ) cstrPlayer.dir = 0;
        if( cstrPlayer.hSpeed > 0  && cstrPlayer.vSpeed > 0 )  cstrPlayer.dir = 1;
        if( cstrPlayer.hSpeed == 0 && cstrPlayer.vSpeed > 0 )  cstrPlayer.dir = 2;
        if( cstrPlayer.hSpeed < 0  && cstrPlayer.vSpeed > 0 )  cstrPlayer.dir = 3;
        if( cstrPlayer.hSpeed < 0  && cstrPlayer.vSpeed == 0 ) cstrPlayer.dir = 4;
        if( cstrPlayer.hSpeed < 0  && cstrPlayer.vSpeed < 0 )  cstrPlayer.dir = 5;
        if( cstrPlayer.hSpeed == 0 && cstrPlayer.vSpeed < 0 )  cstrPlayer.dir = 6;
        if( cstrPlayer.hSpeed > 0  && cstrPlayer.vSpeed < 0 )  cstrPlayer.dir = 7;
    }
}

// -----------------------------------------------------------------------------
//   Title / mission select / win screens
// -----------------------------------------------------------------------------

void cstrDrawTitle()
{
    // Hand-rolled stand-in for upstream's own blocking
    // `gb.titleScreen(gamelogo)`, drawing that same real logo bitmap.
    gbDrawBitmap( 6, 3, cstrGamelogoBitmap );
    gbCursorX = 26;
    gbCursorY = 42;
    gbPrintString( "A: START" );

    if( gbPressed( BTN_A ) )
      cstrState = CSTR_STATE_SELECTMAP;
}

// One mission preview box, drawn from that mission's own real tileset
void cstrDrawMissionBox( int bx, int mission )
{
    gbDrawRect( bx, 7, 30, 19 );

    if( mission == CSTR_MISSION_FOREST )
    {
        gbDrawBitmap( bx + 2,  9,  cstrSpriteBkg[ 7 ] ); // small tree
        gbDrawBitmap( bx + 8,  15, cstrSpriteBkg[ 5 ] ); // rock
        gbDrawBitmap( bx + 18, 9,  cstrSpriteBkg[ 6 ] ); // grass
        return;
    }

    // Desert tileset - shared by Desert Strike, Convoi and SearchDoc
    gbDrawBitmap( bx + 12, 17, cstrSpriteBkg[ 3 ] ); // sand
    gbDrawBitmap( bx + 19, 19, cstrSpriteBkg[ 3 ] );
    gbDrawBitmap( bx + 20, 9,  cstrSpriteBkg[ 0 ] ); // bush
    gbDrawBitmap( bx + 2,  9,  cstrSpriteBkg[ 1 ] ); // cactus

    // The two extra missions need something to tell them apart from Desert
    // Strike at a glance - each gets a real sprite from its own mission.
    if( mission == CSTR_MISSION_CONVOI )
      gbDrawBitmap( bx + 6, 12, cstrRouteHBitmap );
    else if( mission == CSTR_MISSION_SEARCHDOC )
      gbDrawBitmap( bx + 8, 10, cstrEnnemyTourBitmap );
}

void cstrDrawMenu()
{
    if( cstrLvl == CSTR_MISSION_DESERT )         gbPrintString( "     Desert Strike" );
    else if( cstrLvl == CSTR_MISSION_FOREST )    gbPrintString( "     Forest Strike" );
    else if( cstrLvl == CSTR_MISSION_CONVOI )    gbPrintString( "        Convoi" );
    else                                         gbPrintString( "       SearchDoc" );

    // Two preview boxes at a time; `lvl / 2` picks the page, `lvl % 2` the box
    int page = cstrLvl / 2;
    cstrDrawMissionBox( 10, page * 2 );
    cstrDrawMissionBox( 50, page * 2 + 1 );
    gbDrawRect( 9 + ( ( cstrLvl % 2 ) * 40 ), 6, 32, 21 );

    gbCursorX = 35;
    gbCursorY = 30;
    gbPrintString( "Easy" );
    gbCursorX = 35;
    gbCursorY = 36;
    gbPrintString( "Normal" );
    gbCursorX = 35;
    gbCursorY = 42;
    gbPrintString( "Hard" );
    gbDrawBitmap( 15, 29 + ( cstrDifficulty * 6 ),     cstrCopterProfileBitmap );
    gbDrawBitmap( 17, 29 + ( cstrDifficulty * 6 ) - 2, cstrHelix[ 1 ] );
}

void cstrDrawEndGame()
{
    // Convoi's own real win screen bitmap. It is 88px wide against an 84px
    // LCD; the last 4 columns are clipped, exactly as on real hardware.
    gbDrawBitmap( 0, 0, cstrFinalScreenBitmap );

    if( gbPressed( BTN_C ) )
    {
        cstrStartMission();
        return;
    }

    // Upstream SD-flashes the loader cartridge here; the closest real
    // equivalent on this one is its own mission select.
    if( gbPressed( BTN_A ) || gbPressed( BTN_B ) )
      cstrState = CSTR_STATE_SELECTMAP;
}

void cstrUpdateGame()
{
    cstrDrawBaseCamps();
    cstrDrawBuildingFriend();
    cstrDrawBuildingHostile();
    if( cstrHasMobileUnits )
      cstrDrawMobileHostile();

    if( cstrLvl == CSTR_MISSION_CONVOI )
      cstrDrawRoute();

    cstrDrawBackground();

    cstrDrawPlayerFire();
    cstrDrawEnnemyFire();

    cstrDrawHUD();
    cstrDrawPlayer();

    if( cstrLvl == CSTR_MISSION_CONVOI )
    {
        cstrUpdateFriendMobile();
        cstrDrawCamion();
    }

    cstrAnimBoom();
    cstrGameOver();

    cstrCheckLanding();
    if( cstrState != CSTR_STATE_GAME )
      return; // the mission just ended (convoi) - don't run another tick of logic

    cstrCheckButtonsGame();
    if( cstrState != CSTR_STATE_GAME )
      return; // Button C left for the mission select

    cstrCheckPlayerAltitude();
    cstrCheckPlayerFire();
    cstrCheckEnnemyFire();
    cstrCheckFuel();

    cstrInitEnnemyFire();
    cstrAnimation();
    if( cstrHasResurrection )
      cstrResurection();
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameCopterStrike_init()
{
    gbBegin();

    cstrState = CSTR_STATE_TITLE;
    cstrLvl = 0;
    cstrDifficulty = 0;
    cstrMoney = 0;
    cstrCptAnim = 0;
    cstrSfxRequest = -1;
    gbSetColor( GB_BLACK );
}

void gameCopterStrike_update()
{
    if( !gbUpdate() ) return;

    if( cstrState == CSTR_STATE_TITLE )         cstrDrawTitle();
    else if( cstrState == CSTR_STATE_SELECTMAP ){ cstrDrawMenu(); cstrCheckButtonsMenu(); }
    else if( cstrState == CSTR_STATE_ENDGAME )  cstrDrawEndGame();
    else                                        cstrUpdateGame();

    cstrFlushSound();
    gbRenderFrame();
}
