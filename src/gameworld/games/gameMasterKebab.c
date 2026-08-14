#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Master Kebab ("RMKebab", ogbaba, GPLv3). A tiny incremental/idle
// management game: buy fryers/rotisseries/kebab-makers to grow passive
// income, then buy the tier's own "amelioration" (upgrade) once you can
// afford it to jump to the next of 4 tiers (each with its own re-themed
// bitmap set - a food truck, then a full shop, then a chain, then a whole
// country) - clearing tier 3's own upgrade counts as one "victoire" and
// loops back to tier 0 with a fresh 100$, repeatable indefinitely to
// "follow the principles of capitalism" (real upstream's own README, its
// own joke, preserved here).
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a
// plain `gbY(...)` function call (this dialect has no classes/methods -
// see gamePong.c's own header comment). `gb.buttons.repeat()`/`.pressed()`
// -> `gbRepeat()`/`gbPressed()` directly; `min()`/`max()` -> `gbMin()`/
// `gbMax()`.
//
// REAL BITMAP ART: all 11 real upstream bitmaps were already stored as
// real `0x`-hex byte literals upstream (no `B01111111`-style binary
// literals needing conversion) - copied byte-for-byte into `int[]` arrays
// with their own real width/height headers preserved (verified by
// programmatically counting each array's own real element count against
// its declared w/h before transcribing, not hand-counted), not hand-
// redrawn.
//
// BLOCKING `gb.titleScreen(F("Master Kebab"))` -> EXPLICIT STATE: real
// upstream calls this real blocking widget twice - once from `setup()`
// (real boot) and again from `gerer_actions()` any time Button C is
// pressed mid-game (a real "pause and save" gesture - real upstream's own
// comment/README says C both saves progress AND opens the menu). Ported
// as `KEBAB_STATE_TITLE`/`KEBAB_STATE_PLAYING` using this project's own
// established "blocking widget -> explicit resumable state" treatment
// (see gamePong.c's own header comment); a fresh Button A press resumes
// play, matching upstream's own real `titleScreen()` contract.
//
// SOUND: real upstream's own one real non-one-shot call,
// `gb.sound.playPattern(musique, 1)` (a short victory jingle played on
// buying an upgrade), now plays for real via `gbPlayPattern()` - the real
// tracker/pattern engine gamebuinoShim.c/.h now implements (see that
// file's own Sound section header comment) - using the real, byte-for-
// byte `musique[]` pattern data (`kebabMusique[]` here), on channel 1
// exactly like upstream, through the real default square-wave instrument
// (no `changeInstrumentSet()`/`command(CMD_INSTRUMENT,...)` call precedes
// it upstream either, so the engine's own real per-channel default is
// already correct).
//
// REAL EEPROM SAVE, REDESIGNED FOR THIS DIALECT'S FLAT `int` LAYOUT: real
// upstream's own `sauvegarder()`/`restaurer()` do a raw byte-for-byte
// `EEPROM.write(i, ((uint8_t*)&partie)[i])` memcpy of the whole `t_partie`
// struct (5 real AVR 16-bit `int` fields, one real AVR 32-bit `long`, one
// more 16-bit `int`) - not portable here even in spirit, since this
// dialect has no byte-packed struct layout and no pointer-to-struct-member
// byte reinterpretation of that kind (`VIRCON32_C_DIALECT.md`'s own
// "copy struct members to a local first" guidance, and this project's own
// established "flatten a struct into plain named globals" convention -
// see gamePunkt.c's own header comment). Flattened to 7 plain
// `kebab*`-prefixed globals, each saved/loaded individually via
// `eeprom_read_dword()`/`eeprom_write_dword()` at its own fixed 4-byte-
// aligned address (every field widened to a full dword rather than
// preserving each one's own real narrower AVR width, matching this
// project's own established "only behavior needs to survive a reboot, not
// a bit-for-bit AVR memory layout" precedent from the EEPROM audit - see
// this project's own CLAUDE.md).
//
// A DELIBERATE, REASONED DEPARTURE FROM UPSTREAM'S OWN LITERAL FRESH-SAVE
// CHECK: real upstream detects "never saved before" via `if
// (!partie.premiere_partie)` - true only when the sentinel field reads
// back as exactly 0. That's true of a freshly-*constructed* C++ object
// (its own real default member initializer sets it to 0), but NOT of a
// genuinely fresh, factory-erased real EEPROM chip (all bytes 0xFF) -
// `restaurer()` unconditionally overwrites the sentinel from EEPROM before
// this check ever runs, so a truly blank real cartridge would read back
// `premiere_partie` as -1 (a real AVR 16-bit `int`, from two 0xFF bytes),
// which is non-zero, so upstream's own check would never fire and the
// game would start with un-initialized garbage state on its very first
// real boot - the same class of "fresh EEPROM reads as a nonzero
// sentinel, not the checked-for zero" bug this project's own EEPROM audit
// already found and fixed once for real in `gameSkibuino.c` (see
// CLAUDE.md's own "A project-wide EEPROM audit..." section). This shim's
// own fresh-slot state reads back as -1 for the exact same reason (every
// byte cell of a never-written slot defaults to 255, matching real AVR's
// own factory-erased state, not 0 - see `eepromShim.c`'s own header
// comment). Ported as an explicit `kebabPremierePartie != 1111` check
// instead (1111 being upstream's own real sentinel *value*, just tested
// the way its own intent actually requires) - this also means every
// other field (`kebabBon0`/`_1`/`_2`, left un-reset by upstream's own
// fresh-save branch, presumably because that branch was only ever
// observed to fire against a RAM-zeroed struct, never a truly 0xFF-fresh
// chip) is explicitly zeroed here too, so a genuinely fresh cartridge
// always starts from a clean, playable 100$/tier-0/zero-owned state
// rather than whatever a -1-filled read would otherwise produce.
//
// ON REAL UPSTREAM'S OWN README WARNING ("Ce jeu risque de ne pas
// fonctionner sur émulateur…" / "This game may not work on emulators"):
// read in context (both language versions, plus the surrounding "Button C
// saves and opens the menu" note) rather than assumed - the most likely
// concrete referent is real upstream's own `setup()` call order:
// `restaurer()` (a real `EEPROM.read()` loop) runs BEFORE `gb.begin()` is
// ever called. On real ATmega328 hardware the EEPROM is a genuine,
// independent chip peripheral, always readable with no prior library
// initialization at all - but a number of period third-party Gamebuino
// Classic emulators either didn't implement persistent EEPROM storage at
// all, or only wired up whatever stood in for it as part of their own
// "begin"-equivalent step, making an EEPROM read issued before that point
// plausibly return garbage, hang, or simply fail to persist - exactly the
// kind of hardware-vs-emulator EEPROM-timing mismatch this project's own
// EEPROM audit has already run into in a different form (see above). This
// port sidesteps the concern entirely, not just by coincidence: this
// shared shim's own `eepromSelectGame()` is called once by
// `portVircon32.c`'s own dispatch loop *before* any chosen game's own
// `init()` ever runs, so real EEPROM access here is always valid from this
// file's own very first statement - and this project's own web emulator
// has genuine, confirmed-working memory-card-backed EEPROM persistence
// (IndexedDB-backed, verified end-to-end - see this project's own
// CLAUDE.md "A real, initially-wrong claim..." section), unlike whatever
// the real, unnamed emulator(s) upstream's own README was warning
// against.

int kebabKebaberieBitmap[130] = { 32, 32, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7F, 0xFC, 0x0, 0x1, 0x80, 0x3, 0x0, 0x6, 0x0, 0x0, 0xC0, 0x8, 0x56, 0x9D, 0x20, 0x10, 0x66, 0x95, 0x10, 0x20, 0x54, 0xDD, 0x88, 0x40, 0x56, 0xD5, 0x84, 0x40, 0x0, 0x0, 0x6, 0x4F, 0xFF, 0xFF, 0xE7, 0x49, 0x0, 0x38, 0x25, 0x49, 0x0, 0x7C, 0x25, 0x4B, 0x86, 0x6C, 0x25, 0x4B, 0x86, 0x44, 0x25, 0x4B, 0x9E, 0x54, 0x25, 0x4B, 0x9B, 0x38, 0x25, 0x49, 0x1B, 0x12, 0x25, 0x4F, 0xFF, 0xFF, 0xE5, 0x40, 0x0, 0x0, 0x5, 0x40, 0x0, 0x0, 0x5, 0x40, 0x0, 0x0, 0x5, 0x50, 0x0, 0x80, 0xE5, 0x50, 0x0, 0x80, 0x45, 0x51, 0xF0, 0x80, 0x45, 0x50, 0x40, 0x80, 0x45, 0x5C, 0x47, 0x80, 0x45, 0x54, 0x44, 0x80, 0x45, 0x54, 0x44, 0x80, 0xA5 };
int kebabAmbulantBitmap[130] = { 32, 32, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xF, 0xFF, 0x80, 0x0, 0x1F, 0xFF, 0xC0, 0xE0, 0x3F, 0xFF, 0xE0, 0xE0, 0x1F, 0xFF, 0xC0, 0xE0, 0x8, 0x3, 0x81, 0xF0, 0x8, 0x1, 0x81, 0xB0, 0x1C, 0x1, 0x81, 0x10, 0x8, 0x1, 0x81, 0x50, 0x8, 0x1, 0x81, 0xF0, 0x8, 0x0, 0x80, 0xB0, 0x1C, 0x0, 0x80, 0x90, 0x1C, 0x0, 0x80, 0x90, 0x1C, 0x6, 0x80, 0xB0, 0x1C, 0x6, 0x80, 0xB0, 0x9, 0xF, 0x81, 0xB0, 0xB, 0x4F, 0x83, 0xF0, 0x1F, 0xFF, 0xFF, 0xF0, 0x10, 0x0, 0x4, 0x90, 0x15, 0x69, 0xD4, 0xD0, 0x16, 0x69, 0x54, 0x50, 0x15, 0x4D, 0xDC, 0x50, 0x15, 0x6D, 0x5C, 0x50, 0x30, 0x0, 0x6, 0x70, 0x4F, 0xFF, 0xFE, 0x30, 0x48, 0x0, 0x2, 0xF0, 0x30, 0x0, 0x2, 0xF0 };
int kebabBrocheBitmap[26] = { 16, 12, 0x40, 0x40, 0x64, 0xC0, 0x2E, 0x0, 0x1B, 0x0, 0x19, 0x0, 0x13, 0x0, 0x55, 0x0, 0x11, 0x40, 0x4E, 0x0, 0x14, 0xC0, 0x75, 0xE0, 0xFF, 0xF0 };
int kebabFriteuseBitmap[26] = { 16, 12, 0x2, 0x0, 0x20, 0x80, 0x0, 0x0, 0xA6, 0x0, 0x34, 0x0, 0x3F, 0xE0, 0xBD, 0x0, 0xFF, 0x0, 0xBD, 0x40, 0x81, 0x0, 0x81, 0x0, 0x81, 0x0 };
int kebabKebabierBitmap[26] = { 16, 12, 0x1F, 0x80, 0x3F, 0xC0, 0x30, 0xC0, 0x29, 0x40, 0x20, 0x40, 0x22, 0x40, 0x27, 0x40, 0x10, 0x80, 0x3F, 0xC0, 0x41, 0x20, 0x44, 0x20, 0x40, 0x20 };
int kebabAmeliorationBitmap[26] = { 16, 12, 0x6, 0x0, 0xF, 0x0, 0x19, 0x80, 0x30, 0xC0, 0x60, 0x60, 0x70, 0xE0, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x1F, 0x80 };
int kebabKebabdoBitmap[130] = { 32, 32, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3F, 0xFF, 0x80, 0x0, 0x20, 0x0, 0x8E, 0x0, 0x20, 0x0, 0x8E, 0x70, 0x27, 0xFC, 0x8A, 0xD0, 0x24, 0x44, 0x8B, 0xB0, 0x24, 0x44, 0x8B, 0x60, 0x24, 0x44, 0x88, 0xC0, 0x27, 0xFC, 0x89, 0x80, 0x24, 0x44, 0x8A, 0xC0, 0x24, 0x44, 0x8B, 0x60, 0x24, 0x44, 0x8B, 0xB0, 0x27, 0xFC, 0x8A, 0xD0, 0x20, 0x0, 0x8E, 0x70, 0x20, 0x0, 0xFF, 0xFE, 0x2A, 0xD3, 0xA2, 0xA2, 0x2C, 0xD2, 0xA2, 0xA2, 0x2A, 0x9B, 0xB0, 0x2, 0x2A, 0xDA, 0xB2, 0xA2, 0x20, 0x0, 0x0, 0x2, 0x23, 0xFC, 0x7F, 0xF2, 0x22, 0x4, 0x40, 0x12, 0x22, 0x4, 0x40, 0x12, 0x22, 0x64, 0x63, 0x72, 0x22, 0x64, 0x63, 0x72, 0x22, 0x24, 0x41, 0x32, 0x22, 0xE4, 0x4F, 0x32, 0x22, 0x24, 0x5D, 0xB2, 0x22, 0x24, 0x4B, 0xB2, 0x22, 0x34, 0x4B, 0xB2, 0x22, 0x74, 0x4B, 0xB2 };
int kebabMasterofkebabBitmap[130] = { 32, 32, 0x6B, 0xA0, 0x0, 0x0, 0x1C, 0x0, 0x0, 0x0, 0x14, 0x0, 0x1, 0xE0, 0x14, 0x7E, 0x3, 0xF8, 0x14, 0x42, 0x2, 0x18, 0x14, 0x56, 0x2, 0xB8, 0x14, 0x5A, 0x2, 0xD8, 0x17, 0xD7, 0x82, 0xB8, 0x10, 0x56, 0x42, 0xB8, 0x16, 0x42, 0x22, 0x18, 0x16, 0x7E, 0x12, 0x18, 0x11, 0xFF, 0x8A, 0x18, 0x16, 0x42, 0x7, 0xFC, 0x16, 0x56, 0x0, 0x4, 0x10, 0x42, 0x1, 0xC4, 0x10, 0x5A, 0x8, 0x4, 0x10, 0x42, 0xF, 0xC4, 0x10, 0x7E, 0xF, 0xE4, 0x10, 0x18, 0x8A, 0xB4, 0x11, 0xFF, 0x88, 0x1C, 0x11, 0x18, 0xF, 0xFC, 0x10, 0x18, 0xC, 0x1C, 0x17, 0xFF, 0xFF, 0xFC, 0x3C, 0x18, 0x0, 0x1E, 0x1, 0x3C, 0x0, 0x0, 0x0, 0x24, 0x0, 0x0, 0x13, 0xA4, 0x3, 0xE0, 0x2, 0xA4, 0x4, 0x10, 0x5F, 0xA4, 0x27, 0xF4, 0x11, 0xA4, 0x3E, 0x3E, 0x1F, 0xA4, 0x41, 0x41, 0x3F, 0xB6, 0xFF, 0x7F };
int kebabCamionBitmap[26] = { 16, 12, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7F, 0x80, 0x54, 0xF0, 0x58, 0xB0, 0x54, 0x90, 0xFF, 0xF0, 0x30, 0xC0, 0x0, 0x0, 0x6D, 0xB0, 0x0, 0x0 };
int kebabPaysBitmap[26] = { 16, 12, 0x2F, 0xC0, 0x2B, 0x50, 0x2B, 0x50, 0xAF, 0xD0, 0x68, 0x20, 0x3F, 0xF0, 0x19, 0x80, 0x28, 0xC0, 0x48, 0x20, 0x40, 0xA0, 0x31, 0xE0, 0x2F, 0x80 };
int kebabUsineBitmap[26] = { 16, 12, 0xB5, 0x0, 0x20, 0x0, 0x60, 0x0, 0x60, 0x0, 0x7E, 0x0, 0xFF, 0x80, 0xE0, 0xC0, 0xEA, 0xE0, 0xE0, 0xB0, 0xEA, 0x90, 0xE0, 0xD0, 0xEA, 0xD0 };

#define KEBAB_POS_X_BON 2
#define KEBAB_POS_Y_BON_0 2
#define KEBAB_POS_Y_BON_1 18
#define KEBAB_POS_Y_BON_2 34
#define KEBAB_POS_X_AMEL 70
#define KEBAB_POS_Y_AMEL 2
#define KEBAB_POS_X_ART 48
#define KEBAB_POS_Y_ART 16
#define KEBAB_LARG_BOITE 12
#define KEBAB_LARG_CURS ( KEBAB_LARG_BOITE + 2 )

#define KEBAB_PRIX_N0_BON_0 50
#define KEBAB_PRIX_N0_BON_1 200
#define KEBAB_PRIX_N0_BON_2 1000
#define KEBAB_PRIX_N0_AMEL 100000

#define KEBAB_PRIX_N1_BON_0 200
#define KEBAB_PRIX_N1_BON_1 700
#define KEBAB_PRIX_N1_BON_2 2000
#define KEBAB_PRIX_N1_AMEL 1000000

#define KEBAB_PRIX_N2_BON_0 2500
#define KEBAB_PRIX_N2_BON_1 4000
#define KEBAB_PRIX_N2_BON_2 10000
#define KEBAB_PRIX_N2_AMEL 50000000

#define KEBAB_PRIX_N3_BON_0 50000
#define KEBAB_PRIX_N3_BON_1 100000
#define KEBAB_PRIX_N3_BON_2 500000
#define KEBAB_PRIX_N3_AMEL 200000000

#define KEBAB_STATE_TITLE 0
#define KEBAB_STATE_PLAYING 1

// Flattened real upstream `t_partie` fields - see this file's own header
// comment for why a raw struct memcpy save couldn't be ported literally.
int kebabBon0;
int kebabBon1;
int kebabBon2;
int kebabNiveau;
int kebabVictoires;
int kebabArgent;
int kebabPremierePartie;

int kebabCurs;
int kebabState;

// Fixed dword-aligned EEPROM addresses, one real field each.
#define KEBAB_EE_PREMIERE 0
#define KEBAB_EE_BON0 4
#define KEBAB_EE_BON1 8
#define KEBAB_EE_BON2 12
#define KEBAB_EE_NIVEAU 16
#define KEBAB_EE_VICTOIRES 20
#define KEBAB_EE_ARGENT 24

void kebabRestaurer()
{
    kebabPremierePartie = eeprom_read_dword( KEBAB_EE_PREMIERE );
    kebabBon0 = eeprom_read_dword( KEBAB_EE_BON0 );
    kebabBon1 = eeprom_read_dword( KEBAB_EE_BON1 );
    kebabBon2 = eeprom_read_dword( KEBAB_EE_BON2 );
    kebabNiveau = eeprom_read_dword( KEBAB_EE_NIVEAU );
    kebabVictoires = eeprom_read_dword( KEBAB_EE_VICTOIRES );
    kebabArgent = eeprom_read_dword( KEBAB_EE_ARGENT );
}

void kebabSauvegarder()
{
    eeprom_write_dword( KEBAB_EE_PREMIERE, kebabPremierePartie );
    eeprom_write_dword( KEBAB_EE_BON0, kebabBon0 );
    eeprom_write_dword( KEBAB_EE_BON1, kebabBon1 );
    eeprom_write_dword( KEBAB_EE_BON2, kebabBon2 );
    eeprom_write_dword( KEBAB_EE_NIVEAU, kebabNiveau );
    eeprom_write_dword( KEBAB_EE_VICTOIRES, kebabVictoires );
    eeprom_write_dword( KEBAB_EE_ARGENT, kebabArgent );
}

int kebabPrixBon0()
{
    int prix = 0;
    if( kebabNiveau == 0 ) prix = KEBAB_PRIX_N0_BON_0;
    else if( kebabNiveau == 1 ) prix = KEBAB_PRIX_N1_BON_0;
    else if( kebabNiveau == 2 ) prix = KEBAB_PRIX_N2_BON_0;
    else if( kebabNiveau == 3 ) prix = KEBAB_PRIX_N3_BON_0;
    prix = prix + kebabBon0 * prix / 8;
    return prix;
}

int kebabPrixBon1()
{
    int prix = 0;
    if( kebabNiveau == 0 ) prix = KEBAB_PRIX_N0_BON_1;
    else if( kebabNiveau == 1 ) prix = KEBAB_PRIX_N1_BON_1;
    else if( kebabNiveau == 2 ) prix = KEBAB_PRIX_N2_BON_1;
    else if( kebabNiveau == 3 ) prix = KEBAB_PRIX_N3_BON_1;
    prix = prix + kebabBon1 * prix / 4;
    return prix;
}

int kebabPrixBon2()
{
    int prix = 0;
    if( kebabNiveau == 0 ) prix = KEBAB_PRIX_N0_BON_2;
    else if( kebabNiveau == 1 ) prix = KEBAB_PRIX_N1_BON_2;
    else if( kebabNiveau == 2 ) prix = KEBAB_PRIX_N2_BON_2;
    else if( kebabNiveau == 3 ) prix = KEBAB_PRIX_N3_BON_2;
    prix = prix + kebabBon2 * prix / 4;
    return prix;
}

int kebabPrixAmel()
{
    int prix = 0;
    if( kebabNiveau == 0 ) prix = KEBAB_PRIX_N0_AMEL;
    else if( kebabNiveau == 1 ) prix = KEBAB_PRIX_N1_AMEL;
    else if( kebabNiveau == 2 ) prix = KEBAB_PRIX_N2_AMEL;
    else if( kebabNiveau == 3 ) prix = KEBAB_PRIX_N3_AMEL;
    prix = prix + ( ( prix / 10 ) * kebabVictoires * kebabVictoires );
    return prix;
}

bool kebabAcheter( int* bon, int prix )
{
    if( kebabArgent >= prix )
    {
        kebabArgent = kebabArgent - prix;
        *bon = *bon + 1;
        return true;
    }
    return false;
}

// Real upstream `musique[]` (rmkebab.ino) - a short victory jingle, copied
// word-for-word, not hand-transcribed (see this file's own header comment).
int kebabMusique[ 19 ] = { 0x438,0x238,0x234,0x838,0x238,0x240,0x248,0x240,0x838,0x424,0x224,0x22C,0x234,0x22C,0x424,0x438,0x434,0x838,0x000 };

void kebabAcheterAmel()
{
    if( kebabAcheter( &kebabNiveau, kebabPrixAmel() ) )
    {
        gbPlayPattern( kebabMusique, 1 ); // real upstream's own gb.sound.playPattern(musique,1) jingle
        kebabBon0 = 0;
        kebabBon1 = 0;
        kebabBon2 = 0;
        kebabArgent = kebabPrixAmel() / 1000; // real upstream recomputes this AFTER the tier just changed above - preserved exactly
    }
    // real upstream's own "TODO pour eviter les bugs" - clearing tier 3's
    // own upgrade pushes kebabNiveau to 4, past every real price table's
    // own 0-3 range (every kebabPrix*() function above falls through to
    // prix=0 for niveau 4) - this catches that and resets to tier 0 as a
    // "victoire".
    if( kebabNiveau > 3 )
    {
        kebabArgent = 100;
        kebabVictoires = kebabVictoires + 1;
        kebabNiveau = 0;
    }
}

void kebabGagnerArgent()
{
    if( gbFrameCount % 20 == 0 )
      kebabArgent = kebabArgent + ( 1 + kebabBon2 ) * ( kebabBon0 * kebabPrixBon0() / 5 + kebabBon1 * kebabPrixBon1() / 7 ) / 10;
}

void kebabGererActions()
{
    if( gbRepeat( BTN_A, 5 ) )
    {
        if( kebabCurs == 0 ) kebabAcheter( &kebabBon0, kebabPrixBon0() );
        if( kebabCurs == 1 ) kebabAcheter( &kebabBon1, kebabPrixBon1() );
        if( kebabCurs == 2 ) kebabAcheter( &kebabBon2, kebabPrixBon2() );
        if( kebabCurs == 3 ) kebabAcheterAmel();
    }
    if( gbPressed( BTN_DOWN ) ) kebabCurs = gbMin( 3, kebabCurs + 1 );
    if( gbPressed( BTN_UP ) ) kebabCurs = gbMax( 0, kebabCurs - 1 );
    if( gbPressed( BTN_LEFT ) ) kebabCurs = gbMax( 0, kebabCurs - 3 );
    if( gbPressed( BTN_RIGHT ) ) kebabCurs = gbMin( 3, kebabCurs + 3 );
    if( gbPressed( BTN_C ) )
    {
        kebabSauvegarder();
        kebabState = KEBAB_STATE_TITLE;
    }
}

void kebabAfficherNiveau()
{
    if( kebabNiveau == 0 )
    {
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_0, kebabFriteuseBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_1, kebabBrocheBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_2, kebabKebabierBitmap );
        gbDrawBitmap( KEBAB_POS_X_ART, KEBAB_POS_Y_ART, kebabAmbulantBitmap );
    }
    else if( kebabNiveau == 1 )
    {
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_0, kebabFriteuseBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_1, kebabBrocheBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_2, kebabKebabierBitmap );
        gbDrawBitmap( KEBAB_POS_X_ART, KEBAB_POS_Y_ART, kebabKebaberieBitmap );
    }
    else if( kebabNiveau == 2 )
    {
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_0, kebabFriteuseBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_1, kebabBrocheBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_2, kebabKebabierBitmap );
        gbDrawBitmap( KEBAB_POS_X_ART, KEBAB_POS_Y_ART, kebabKebabdoBitmap );
    }
    else if( kebabNiveau == 3 )
    {
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_0, kebabCamionBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_1, kebabUsineBitmap );
        gbDrawBitmap( KEBAB_POS_X_BON, KEBAB_POS_Y_BON_2, kebabPaysBitmap );
        gbDrawBitmap( KEBAB_POS_X_ART, KEBAB_POS_Y_ART, kebabMasterofkebabBitmap );
    }
    gbDrawBitmap( KEBAB_POS_X_AMEL, KEBAB_POS_Y_AMEL, kebabAmeliorationBitmap );
}

void kebabAfficherCurseur()
{
    int posX = KEBAB_POS_X_BON;
    int posY = KEBAB_POS_Y_BON_0;

    if( kebabCurs <= 2 ) posX = KEBAB_POS_X_BON;
    else posX = KEBAB_POS_X_AMEL;

    if( kebabCurs == 0 ) posY = KEBAB_POS_Y_BON_0;
    else if( kebabCurs == 1 ) posY = KEBAB_POS_Y_BON_1;
    else if( kebabCurs == 2 ) posY = KEBAB_POS_Y_BON_2;
    else if( kebabCurs == 3 ) posY = KEBAB_POS_Y_BON_0;

    gbDrawRect( posX, posY, KEBAB_LARG_CURS, KEBAB_LARG_CURS );
}

void kebabAfficherTextes()
{
    // BONUS 0
    gbCursorX = KEBAB_POS_X_BON + KEBAB_LARG_CURS;
    gbCursorY = KEBAB_POS_Y_BON_0;
    gbPrintNumber( kebabPrixBon0() );
    gbPrintString( "$\n" );
    gbPrintNumber( kebabBon0 );

    // BONUS 1
    gbCursorX = KEBAB_POS_X_BON + KEBAB_LARG_CURS;
    gbCursorY = KEBAB_POS_Y_BON_1;
    gbPrintNumber( kebabPrixBon1() );
    gbPrintString( "$\n" );
    gbPrintNumber( kebabBon1 );

    // BONUS 2
    gbCursorX = KEBAB_POS_X_BON + KEBAB_LARG_CURS;
    gbCursorY = KEBAB_POS_Y_BON_2;
    gbPrintNumber( kebabPrixBon2() );
    gbPrintString( "$\n" );
    gbPrintNumber( kebabBon2 );

    // AMEL
    gbCursorY = KEBAB_POS_Y_AMEL + KEBAB_LARG_CURS;
    gbCursorX = KEBAB_POS_X_AMEL - 32;
    gbPrintNumber( kebabPrixAmel() );
    gbPrintString( "$" );

    // SOUSOUS (money)
    gbCursorX = KEBAB_POS_X_AMEL - 32;
    gbCursorY = 0;
    gbPrintString( "$:" );
    gbPrintNumber( kebabArgent );

    // Victoires
    gbCursorX = KEBAB_POS_X_AMEL - 32;
    gbCursorY = 8;
    gbPrintString( ":^) " );
    gbPrintNumber( kebabVictoires );
}

void kebabAfficher()
{
    kebabAfficherNiveau();
    kebabAfficherCurseur();
    kebabAfficherTextes();
}

void kebabUpdateTitle()
{
    gbSetColor( 1 );
    gbCursorX = 4;
    gbCursorY = 16;
    gbPrintString( "Master Kebab" );
    gbCursorX = 4;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      kebabState = KEBAB_STATE_PLAYING;
}

void kebabUpdatePlaying()
{
    kebabAfficher();
    kebabGererActions();
    kebabGagnerArgent();
}

void gameMasterKebab_init()
{
    gbBegin();

    kebabRestaurer();
    if( kebabPremierePartie != 1111 )
    {
        kebabPremierePartie = 1111;
        kebabArgent = 100;
        kebabVictoires = 0;
        kebabNiveau = 0;
        kebabBon0 = 0;
        kebabBon1 = 0;
        kebabBon2 = 0;
    }

    kebabCurs = 0;
    kebabState = KEBAB_STATE_TITLE;
}

void gameMasterKebab_update()
{
    if( !gbUpdate() ) return;

    if( kebabState == KEBAB_STATE_PLAYING )
      kebabUpdatePlaying();
    else
      kebabUpdateTitle();

    gbRenderFrame();
}
