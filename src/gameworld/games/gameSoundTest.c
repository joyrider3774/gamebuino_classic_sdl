#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// SoundTest - a real, in-cartridge diagnostic tool, not a port of any
// upstream Gamebuino Classic game. Exercises every one of gamebuinoShim.c's
// own real Sound-section primitives directly: the "default" one-shot tones
// (gbPlayOK()/gbPlayCancel()/gbPlayTick()), a raw gbPlayNote() pitch sweep
// across the real 0-35 _halfPeriods index range, and a few
// gbSoundCommand()-driven extras (the noise instrument, a volume slide, a
// quiet note). A direct port of a matching real Arduino sketch
// (more games/SoundTest/SoundTest.ino, built on the real Gamebuino Classic
// library itself) originally written to record reference audio from real
// hardware/Simbuino during this project's own ISR-rate investigation - this
// in-cartridge version lets the same set of test tones be replayed directly
// from inside the port itself, for a quick by-ear spot check with no
// separate hardware/build needed. Ported from the sibling
// gamebuino_classic_vircon32 build's own identical gameSoundTest.c (same
// dialect rewrite this whole project already applies everywhere: `int[N]
// name` becomes plain `int name[N]`, string literals are genuine `char*`
// instead of `int*`).
//
// UP/DOWN selects a test, A triggers it. The predicted frequency shown next
// to each pitch-sweep entry mirrors gbUpdateNoteChannel()'s own real
// GB_SOUND_ISR_HZ formula (gamebuinoShim.c) - duplicated here (rather than
// called directly, since it's an internal, non-exported implementation
// detail of the shim) purely to drive this display.

#define SND_NUM_PITCH_TESTS 8
int sndPitchTests[8] = { 0, 5, 10, 14, 20, 26, 30, 35 };

#define SND_NUM_TESTS ( 3 + SND_NUM_PITCH_TESTS + 3 )
// 0..2                       = playOK/playCancel/playTick
// 3..3+SND_NUM_PITCH_TESTS-1 = the pitch sweep above (pitch 14/26 are the
//                              exact two notes gbPlayOK() itself plays)
// last 3                     = command()-driven extras

int sndSelected = 0;

// Real _halfPeriods table (EXTENDED_NOTE_RANGE=0, the real default) -
// copied verbatim from utility/Sound.cpp (matching the real table already
// baked into gamebuinoShim.c itself), used only to compute the predicted
// frequency shown for each pitch-sweep entry.
int sndHalfPeriods[36] = {
    246,232,219,207,195,184,174,164,155,146,138,130,123,116,110,104,98,92,
    87,82,78,73,69,65,62,58,55,52,49,46,44,41,39,37,35,33
};

float sndPredictedFreq( int pitch )
{
    return ( 16000000.0 / 281.0 ) / ( 2.0 * (float)sndHalfPeriods[ pitch ] );
}

void sndDrawLabel( int i )
{
    if( i == 0 ) { gbPrintString( "playOK()" ); return; }
    if( i == 1 ) { gbPrintString( "playCancel()" ); return; }
    if( i == 2 ) { gbPrintString( "playTick()" ); return; }

    if( i < 3 + SND_NUM_PITCH_TESTS )
    {
        gbPrintString( "pitch " );
        gbPrintNumber( sndPitchTests[ i - 3 ] );
        if( sndPitchTests[ i - 3 ] == 14 ) gbPrintString( " (OK note1)" );
        if( sndPitchTests[ i - 3 ] == 26 ) gbPrintString( " (OK note2)" );
        return;
    }

    int extra = i - ( 3 + SND_NUM_PITCH_TESTS );
    if( extra == 0 ) { gbPrintString( "noise instrument" ); return; }
    if( extra == 1 ) { gbPrintString( "volume slide" ); return; }
    gbPrintString( "quiet note (vol 1)" );
}

void sndRunTest( int i )
{
    if( i == 0 ) { gbPlayOK(); return; }
    if( i == 1 ) { gbPlayCancel(); return; }
    if( i == 2 ) { gbPlayTick(); return; }

    if( i < 3 + SND_NUM_PITCH_TESTS )
    {
        // Fresh, known-good state before each raw pitch-sweep note: real
        // max note volume, real default square-wave instrument, no
        // slide/arpeggio left over from an earlier test.
        gbSoundCommand( GB_CMD_VOLUME, 9, 0, 0 );
        gbSoundCommand( GB_CMD_INSTRUMENT, 0, 0, 0 );
        gbSoundCommand( GB_CMD_SLIDE, 0, 0, 0 );
        gbSoundCommand( GB_CMD_ARPEGGIO, 0, 0, 0 );
        gbPlayNote( sndPitchTests[ i - 3 ], 30 ); // 30 real ticks long - long enough to clearly hear
        return;
    }

    int extra = i - ( 3 + SND_NUM_PITCH_TESTS );
    if( extra == 0 )
    {
        // Real noise instrument (index 1 of the real default instrument
        // pair) - the same one gbPlayTick() itself uses internally.
        gbSoundCommand( GB_CMD_VOLUME, 9, 0, 0 );
        gbSoundCommand( GB_CMD_INSTRUMENT, 1, 0, 0 );
        gbPlayNote( 20, 40 );
    }
    else if( extra == 1 )
    {
        // A real volume slide: starts loud, fades down over the note's
        // own real duration.
        gbSoundCommand( GB_CMD_VOLUME, 9, 0, 0 );
        gbSoundCommand( GB_CMD_INSTRUMENT, 0, 0, 0 );
        gbSoundCommand( GB_CMD_SLIDE, 3, -2, 0 );
        gbPlayNote( 20, 60 );
    }
    else
    {
        gbSoundCommand( GB_CMD_VOLUME, 1, 0, 0 );
        gbSoundCommand( GB_CMD_INSTRUMENT, 0, 0, 0 );
        gbSoundCommand( GB_CMD_SLIDE, 0, 0, 0 );
        gbPlayNote( 20, 30 );
    }
}

void gameSoundTest_init()
{
    gbBegin();
    gbSetFont( gbFont3x5 ); // real hardware's own smaller default font - gbFont5x7 overflowed this screen's own 84px width across the label/frequency lines
}

void gameSoundTest_update()
{
    if( !gbUpdate() ) return;

    if( gbRepeat( BTN_UP, 8 ) )
    {
        sndSelected = sndSelected - 1;
        if( sndSelected < 0 ) sndSelected = SND_NUM_TESTS - 1;
    }
    if( gbRepeat( BTN_DOWN, 8 ) )
    {
        sndSelected = sndSelected + 1;
        if( sndSelected >= SND_NUM_TESTS ) sndSelected = 0;
    }
    if( gbPressed( BTN_A ) )
      sndRunTest( sndSelected );

    gbSetColor( 1 );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "SOUND TEST\nUp/Dn:sel A:play\n> " );
    sndDrawLabel( sndSelected );
    gbPrintString( "\n" );

    if( ( sndSelected >= 3 ) && ( sndSelected < 3 + SND_NUM_PITCH_TESTS ) )
    {
        gbPrintString( "predicted: " );
        gbPrintFloat( sndPredictedFreq( sndPitchTests[ sndSelected - 3 ] ), 1 );
        gbPrintString( "Hz" );
    }

    gbRenderFrame();
}
