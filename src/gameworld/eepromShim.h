#ifndef EEPROMSHIM_H
#define EEPROMSHIM_H

// -----------------------------------------------------------------------------
// A "fake eeprom.h" - reproduces the surface of AVR-libc's own <avr/eeprom.h>
// (the real primitive layer Arduino's own EEPROM.h library is itself built
// on top of) so a ported game's dropped high-score-persistence code can be
// restored with minimal changes: raw avr-libc call sites
// (eeprom_read_byte()/eeprom_update_byte()/etc) port with zero renaming;
// upstream Arduino EEPROM.h call sites (EEPROM.read(x)/EEPROM.write(x,v)/
// EEPROM.update(x,v)) need only a mechanical rename to the matching
// eeprom_*_byte() function below, since this dialect has no class/method
// support and dot-call syntax can't be preserved literally.
//
// Ported from the sibling gamebuino_classic_vircon32 build's own
// eepromShim.h/.c (itself already sized for real Gamebuino Classic
// hardware's own genuine 1024-byte ATmega328 EEPROM - see that project's
// own CLAUDE.md "EEPROM persistence, ported from the sibling project (1024
// bytes, not 512)" section), cross-checked against the sibling
// Tinyjoypad_SDL project's own file-backed reimplementation of the
// identical scheme (same public API and on-disk slot format/hashing,
// just backed here by a real file - see machineDependent.h's own
// md_card*() declarations, implemented per-port against a real dotfile in
// the user's home directory on the SDL ports - instead of Vircon32's own
// memory-card hardware abstraction). `title` is a real `char*` here, not
// the Vircon32 dialect's own `int[]`-string convention.
//
// Games do not select their own slot - eepromSelectGame() is called once,
// automatically, by gamesMain.c right before a newly-chosen game's init()
// runs, keyed by that game's own menu title rather than its registration
// index (see eepromShim.c's own header comment for why) - every eeprom_*()
// call below implicitly operates on whichever slot was most recently
// selected.
// -----------------------------------------------------------------------------

// Called once by gamesMain.c's dispatch code, before a game's init() - not
// something a game itself ever needs to call.
void eepromSelectGame( char* title );

int  eeprom_read_byte( int address );
void eeprom_write_byte( int address, int value );
void eeprom_update_byte( int address, int value );

int  eeprom_read_word( int address );
void eeprom_write_word( int address, int value );

int  eeprom_read_dword( int address );
void eeprom_write_dword( int address, int value );

void eeprom_read_block( void* dest, int address, int size );
void eeprom_write_block( void* src, int address, int size );

// no-op - real file I/O has no write latency to wait out, kept only so
// upstream eeprom_busy_wait() call sites port with zero changes.
void eeprom_busy_wait();

#endif
