#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Gamebuino2048 (Josiah Winslow / "JWinslow23", License: None specified -
// recovered via direct zip download, see more games/DISCOVERED_GAMES.md for
// provenance). A real, complete port of Gabriele Cirulli's 2048 to
// Gamebuino Classic: slide tiles with the D-pad, merge equal tiles, reach
// 2048 to win (the game keeps going afterward), reach a completely full
// board with no more legal merges to lose. Also a genuine EEPROM consumer -
// A saves the current board/score/highscore, B resets the current game
// (keeping the highscore), hold B and press A to wipe the save AND
// highscore, and C saves then returns to the title screen (matching this
// game's own real upstream README.txt control list verbatim). NOTE: the
// sibling tinyjoypad_vircon32 project already has its own, unrelated 2048
// port - mentioned here only for novelty/context tracking, not something
// this port needs to compare against or match.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). `random(N)` became `arand(N)` (this
// dialect's own established RNG helper). `gb.pickRandomSeed()` became
// `gbPickRandomSeed()`, a documented no-op. `gb.battery.show = false;` was
// dropped outright (purely cosmetic, no equivalent exists or is needed).
// `gb.display.persistence = true;` was also dropped outright, NOT because
// persistence is unsupported but because it's moot here either way: real
// `persistence=true` only means "don't auto-clear the buffer", and this
// game already calls a real, unconditional `gb.display.clear()` itself
// (upstream's own ResetDisplay(), ported below as g2048DrawBoard()) at the
// top of every single frame it draws - so whether the underlying auto-clear
// runs or not can never be observed either way.
//
// REAL BITMAP ART RESTORED: every real `const byte NAME[] PROGMEM = {...}`
// array upstream (Title2048, Logo2048, NewTile_1/2/3, and all 18
// Tile_0..Tile_131072 tile-face sprites) was copied byte-for-byte (via a
// small script reading the real .ino source directly, converting both its
// `0x..` hex bytes and its Arduino `B00000000`-style binary literals to
// plain decimal/hex - this game's tile sprites are the first bitmaps in
// this project to use the binary-literal form at all) into a plain
// `int[N] name = { width, height, byte0, byte1, ... }` array below (this
// dialect's own `int[N] name` array-declaration order, not C's
// `int name[N]` - see gameConduit.c's own tile arrays for the established
// precedent), exactly the format `gbDrawBitmap()` expects - no further
// conversion needed. Every real upstream `gb.display.drawBitmap(...)` call
// site has a direct `gbDrawBitmap()` counterpart at the exact same real
// coordinates: Title2048 (the title-screen logo, upstream's own
// `gb.titleScreen(F("JWinslow23 presents"), Title2048)` argument - restored
// via an explicit title-screen state, see below), Logo2048 (the small
// in-game HUD logo at (56,0), part of every real ResetDisplay() call),
// NewTile_1/2/3 (the 3-frame "flash" animation upstream plays over a newly
// spawned tile's cell before its real value becomes visible - see the
// SPAWN ANIMATION note below), and TileSprites[Board2048[cell]] (the actual
// 4x4 grid of numbered tile faces) - upstream's own real `const byte
// *TileSprites[]` pointer table is ported as a real `int*[18]
// g2048TileSprites` array (this exact `int*[N] name = {...}` declaration
// form was already proven working in this project by gameUfoRace.c's own
// `ufoSprites` table, so it's used directly here rather than the more
// defensive if-chain-returning-a-pointer fallback gameBlockdude.c's own
// header comment describes using instead, for a case where no such
// precedent existed yet).
//
// SPECIAL GLYPH ICONS: upstream's own real "\25:save"/"\26:reset" (etc.)
// UI hint strings embed Gamebuino's own custom low-ASCII icon glyphs
// (decimal 21/22 - real up/down "confirm/cancel" arrow icons, replacing
// the usual unprintable control-character range, now ported into this
// shim's real font tables). A plain quoted string literal can't hold a
// non-printable code point, so each one was built as an explicit
// 0-terminated `int[N]` array of decimal character codes instead (the
// `gbPrintString()`-compatible pattern already established by
// gameUfoRace.c's own `ufoSaveExitText`/gamePunkt.c's own
// `punktPrintChar()` helper) - g2048SaveHintText/g2048ResetHintText/
// g2048YesNoText/g2048ContinueText/g2048PressResetText below, one per real
// upstream string that used this glyph range.
//
// STATE MACHINE: upstream's own blocking `gb.titleScreen(...)` (called once
// from setup(), and again - synchronously, mid-loop() - whenever Button C
// is pressed, saving first) was converted into an explicit
// G2048_STATE_TITLE state, dismissed by a genuine `gbPressed(BTN_A)`,
// matching every other port's own "blocking loop -> explicit resumable
// state" treatment (see gamePong.c's own header comment). Both of
// upstream's real post-dismiss branches (`isValidGame()` ->
// restoreGame()+"Back already?" popup, vs. `newSave(false)`+"Welcome to
// 2048!" popup) are preserved verbatim in g2048OnTitleDismiss().
//
// SPAWN ANIMATION, a second real blocking loop converted to a state:
// upstream's own SpawnTile() picks a random empty cell, then blocks for
// exactly 3 real engine ticks (`while(gb.update()==false){}` per frame)
// flashing NewTile_1/2/3 over that cell before finally committing its real
// value (90% a "2", 10% a "4" - `if(random(10))`, preserved exactly) -
// ported as an explicit G2048_STATE_SPAWN state (g2048UpdateSpawn()) that
// spends 3 real ticks doing the same flash before committing. A fresh game
// needs TWO such spawns back-to-back (upstream's own newGame() calls
// SpawnTile() twice in a row, sequentially blocking) - reproduced via a
// small `g2048SpawnRemaining` counter so the state machine chains straight
// into a second spawn instead of returning to PLAY after the first.
//
// THE MOST INTRICATE REAL CONTROL-FLOW PORTED HERE: upstream's own
// `loop()` is one big linear sequence with THREE further blocking dialogs
// nested inside it (the lose box, the win box, and a hold-B-then-A
// "delete save" confirmation), each only reachable by falling through
// several `if` conditions in a fixed real order, all within the same
// logical `if(gb.update())` tick. Since the SPAWN state above already
// turns "resolve this move" into a multi-tick process, this port defers
// upstream's own post-spawn checks (the lose probe, the win dialog, and
// the trailing A-save/B-hold/C-title checks) into a single
// g2048ResolveAfterMoveSpawn()/g2048HandleIdleButtons() pair run once the
// spawn animation actually finishes - functionally equivalent to
// upstream's own real behavior (all of this happens as part of handling
// ONE player input either way, upstream just pays its own 3-frame delay
// via nested `gb.update()` calls instead of a separate top-level state),
// documented here as a deliberate simplification of the exact nested-loop
// plumbing rather than a behavior change. One real, deliberate
// consequence: on a tick where a move actually changes the board (so a
// spawn animation plays), this port's own A-save/B-hold/C-title checks are
// evaluated once the spawn finishes rather than on the original triggering
// tick - matching upstream's own real wall-clock timing (which reads those
// same buttons several real frames later than the movement check, for
// exactly this reason) far more closely than checking them immediately
// would.
//
// THE LOSE-CHECK ONLY PROBES 2 OF 4 POSSIBLE ROTATIONS - CONFIRMED NOT A
// BUG, NOT "FIXED": upstream's own full-board lose-check does
// `MoveRight(false); RotateClockwise(); MoveRight(false);` then rotates 3
// more times back to identity - only ever testing what amounts to a RIGHT
// move and an UP move, never LEFT or DOWN. Worked through by hand before
// deciding to preserve this exactly: on a completely full board (this
// check only ever runs at 16/16 tiles), no compression is ever possible in
// any direction (compression needs an empty cell to slide into) - the only
// way any move can still change the board is a same-row or same-column
// adjacent-equal-pair MERGE, and MoveRight's own merge scan
// (`Board2048[x+y]==Board2048[x+y-1]`) detects any adjacent-equal pair in a
// row regardless of which direction the merge would nominally slide toward
// - so testing RIGHT already finds every possible horizontal merge, and
// testing UP (after one rotation) already finds every possible vertical
// one. Testing LEFT/DOWN as well would be redundant, not more correct.
// Ported verbatim as g2048ResolveAfterMoveSpawn()'s own probe sequence.
//
// A GENUINE UPSTREAM BUG, NORMALIZED (see gameAgaruino.c's own header
// comment for the established "obvious typo, no gameplay impact, silently
// normalize" precedent): PopupMessage()'s own two `for(int x; x<16; x++)`
// loops never initialize `x` to 0 at all - real, if likely harmless in
// practice on AVR's own stack-reuse behavior, undefined behavior nonetheless,
// and not reproducible meaningfully in this dialect regardless (there is no
// equivalent "leftover stack value" to inherit here). Ported below
// (g2048CheckMilestone()) with `x` properly initialized to 0, the obviously
// intended value - this makes both loops do exactly what their own logic
// clearly means to: find the current highest tile, then suppress the popup
// if that same value already existed on the board before this move.
//
// FOUR INDEPENDENT `if`s, NOT `if`/`else if`, PRESERVED: upstream's own
// four D-pad direction checks in loop() are four separate, unchained `if`
// statements - if a real controller could somehow register two directions
// as freshly pressed on the exact same tick (e.g. a diagonal press),
// upstream would apply BOTH moves in sequence rather than picking just one.
// Preserved exactly in g2048UpdatePlay() below rather than "fixed" into a
// single-direction-only `else if` chain that was never upstream's own real
// behavior.
//
// EEPROM: this game genuinely uses real persistent storage upstream
// (`EEPROM.read()`/`EEPROM.write()`, gated by a real magic-ID+struct
// layout unique to AVR's own in-memory byte layout that can't be
// reproduced bit-for-bit here) - ported to this project's own
// eepromShim.h using the same simple "magic byte at address 0" convention
// gamePunkt.c/gameUfoRace.c already established, storing the full 16-cell
// board (1 byte/cell, addresses 1-16), score and highscore (4-byte
// `eeprom_write_dword()`/`eeprom_read_dword()` - both already existed in
// eepromShim.h, no shim gap here at all) at addresses 17 and 21, and the
// win-state flag at address 25. `isValidGame()`'s own real text-ID compare
// (`strcmp_P` against `"2048 GAMEBUINO"`) became a single magic-byte check
// at address 0, matching the project's own established simplification (the
// exact byte value doesn't need to be globally unique - eepromSelectGame()
// already gives every game its own independent slot keyed by title).
//
// POPUP MESSAGES: real `Gamebuino::popup()` (a small transient on-screen
// notification, non-blocking, drawn as an overlay on top of whatever's
// already on screen) is reimplemented locally rather than routed through
// the shim's generic `gbPopup()` primitive (see below for why): a simple
// frame-counter overlay (g2048PopupTimer, drawn as a bordered text box via
// g2048DrawPopupOverlay(), called from every gameplay-ish state's own
// update function). The active message is stored as a small integer ID
// (g2048PopupMsgId) and resolved back to real text on demand via
// g2048GetPopupText() - an if-chain function returning `int*`, the same
// pattern gameBlockdude.c's own `dudeGetMapData()` and this file's own
// `g2048GetMilestoneText()` use.
//
// A real `gbPopup(int* text, int duration)` shim primitive exists in
// gamebuinoShim.h/.c and auto-draws itself via gbRenderFrame(), but this
// file's own local implementation is deliberately not used in its place:
// real `gbPopup()` always draws at real hardware's own fixed
// bottom-of-screen position using whatever font/fontSize happens to be
// active at draw time, while this game's own `g2048DrawPopupOverlay()`
// explicitly forces `gbFont3x5` first specifically so its two longest
// milestone strings ("16384! Keep playing!"/"32768! Unbelievable!", 21
// characters) still mostly fit - switching to the generic primitive would
// silently drop that per-call font guarantee for any code path that
// happens to leave a different font active first, a real regression risk
// this port can't verify without a full rebuild+playtest across every
// state transition. Kept as its own local, deliberately font-locked
// implementation.
//
// Every one of upstream's real milestone congratulation strings (msg8
// through msg131072, the "8! Good!"/"16! Great!"/etc. table) is preserved
// verbatim, along with every other real popup string ("Welcome to
// 2048!"/"Back already?"/"Game saved."/"Game reset."/"Save deleted."/
// "Deletion cancelled."/"Game continued."). All real popups use upstream's
// own uniform 40-tick duration. Two of the longest milestone strings
// ("16384! Keep playing!"/"32768! Unbelievable!", 21 characters) clip by
// about one character against this popup box's own real available width at
// the smallest real font - an extension of this project's own
// already-documented "Font fidelity" limitation (see CLAUDE.md's own Open
// Questions), not a new gap.
//
// No missing shim primitives were found or needed while porting this game -
// gbDrawBitmap()/eeprom_read_dword()/eeprom_write_dword() and everything
// else this port needed already existed.

int g2048TitleBmp[242] = { 64, 30,
    0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFC, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0,
    0x0, 0x1, 0x3C, 0x78, 0x38, 0xF2, 0x0, 0x0, 0x0, 0x1, 0x7E, 0xFC, 0x79, 0xFA, 0x0, 0x0,
    0x0, 0x1, 0x66, 0xCC, 0xD9, 0x9A, 0x0, 0x0, 0x0, 0x1, 0x6, 0xCD, 0x99, 0xFA, 0x0, 0x0,
    0x0, 0x1, 0xE, 0xCD, 0xFC, 0xF2, 0x0, 0x0, 0x0, 0x1, 0x1C, 0xCD, 0xFD, 0xFA, 0x0, 0x0,
    0x0, 0x1, 0x38, 0xCC, 0x19, 0x9A, 0x0, 0x0, 0x0, 0x1, 0x7E, 0xFC, 0x19, 0xFA, 0x0, 0x0,
    0x0, 0x1, 0x7E, 0x78, 0x18, 0xF2, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0,
    0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFC, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xEA, 0xE7, 0x73, 0x73, 0xA2, 0x57, 0x60, 0x0, 0x4E, 0xC7, 0x52, 0x23, 0xA7, 0x26, 0x50, 0x0,
    0x4A, 0xE5, 0x76, 0x22, 0x35, 0x27, 0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x6, 0x27, 0x73, 0xB8, 0x7, 0xFF, 0xFF, 0xE0, 0x5, 0x77, 0x62, 0xB0, 0x8, 0x0, 0x0, 0x10,
    0x7, 0x55, 0x73, 0xA0, 0x8, 0xDF, 0xF8, 0x10, 0x0, 0x0, 0x0, 0x0, 0x8, 0xDF, 0xF8, 0x10,
    0x0, 0x62, 0x23, 0x0, 0x8, 0x10, 0x8, 0x10, 0x0, 0x15, 0x65, 0x0, 0x8, 0x10, 0x8, 0x10,
    0x0, 0x25, 0x27, 0x80, 0x8, 0x90, 0x8, 0xD0, 0x0, 0x72, 0x71, 0x0, 0x9, 0x50, 0x8, 0xD0,
    0x0, 0x0, 0x0, 0x0, 0x8, 0x90, 0xB, 0x10, 0x0, 0x0, 0x0, 0x0, 0x8, 0xF, 0xF3, 0x10,
    0x0, 0x0, 0x0, 0x0, 0x6, 0x0, 0x0, 0x60, 0x0, 0x0, 0x0, 0x0, 0x1, 0xFF, 0xFF, 0x80,
};

int g2048LogoBmp[20] = { 24, 6,
    0x71, 0xC9, 0x8E, 0x9A, 0x69, 0x93, 0x1A, 0x69, 0x8E, 0x32, 0x6F, 0xD3, 0x62, 0x61, 0x93, 0xF9,
    0xC1, 0x8E,
};

int g2048NewTileBmp1[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x80, 0x8, 0x80, 0x8, 0x8F, 0x88, 0x88, 0x88, 0x88, 0x88, 0x8F, 0x88,
    0x80, 0x8, 0x80, 0x8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048NewTileBmp2[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x80, 0x8, 0x9F, 0xC8, 0x90, 0x48, 0x90, 0x48, 0x90, 0x48, 0x90, 0x48,
    0x9F, 0xC8, 0x80, 0x8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048NewTileBmp3[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0xBF, 0xE8, 0xA0, 0x28, 0xA0, 0x28, 0xA7, 0x28, 0xA7, 0x28, 0xA0, 0x28,
    0xA0, 0x28, 0xBF, 0xE8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp0[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x80, 0x8, 0x80, 0x8, 0x80, 0x8, 0x80, 0x8, 0x80, 0x8, 0x80, 0x8,
    0x80, 0x8, 0x80, 0x8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp2[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x8F, 0x88, 0x90, 0x48, 0x80, 0x48, 0x83, 0x88, 0x8C, 0x8, 0x90, 0x8,
    0x90, 0x8, 0x9F, 0xC8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp4[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x83, 0x88, 0x84, 0x88, 0x88, 0x88, 0x90, 0x88, 0x9F, 0xC8, 0x80, 0x88,
    0x80, 0x88, 0x80, 0x88, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp8[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x8F, 0x88, 0x90, 0x48, 0x90, 0x48, 0x8F, 0x88, 0x90, 0x48, 0x90, 0x48,
    0x90, 0x48, 0x8F, 0x88, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp16[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x93, 0xC8, 0xB4, 0x28, 0x94, 0x8, 0x97, 0xC8, 0x94, 0x28, 0x94, 0x28,
    0x94, 0x28, 0x93, 0xC8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp32[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x98, 0xC8, 0xA5, 0x28, 0x84, 0x28, 0x88, 0x28, 0x84, 0xC8, 0x85, 0x8,
    0xA5, 0x8, 0x99, 0xE8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp64[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x98, 0x48, 0xA4, 0xC8, 0xA1, 0x48, 0xB9, 0xE8, 0xA4, 0x48, 0xA4, 0x48,
    0xA4, 0x48, 0x98, 0x48, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp128[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0xAC, 0x48, 0xA2, 0xA8, 0xA2, 0xA8, 0xA4, 0x48, 0xA8, 0xA8, 0xA8, 0xA8,
    0xA8, 0xA8, 0xAE, 0x48, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp256[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0xB6, 0x68, 0x94, 0x88, 0x94, 0x88, 0x96, 0xC8, 0xA2, 0xA8, 0xA2, 0xA8,
    0xA2, 0xA8, 0xB6, 0x48, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp512[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0xB2, 0xC8, 0xA6, 0x28, 0xA2, 0x28, 0xA2, 0x48, 0x92, 0x88, 0x92, 0x88,
    0x92, 0x88, 0xB2, 0xE8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp1024[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x8B, 0x88, 0x8A, 0x88, 0x8B, 0x88, 0x80, 0x8, 0x9A, 0x88, 0x8A, 0x88,
    0x93, 0xC8, 0x98, 0x88, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp2048[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x99, 0xC8, 0x89, 0x48, 0x8D, 0xC8, 0x80, 0x8, 0x95, 0xC8, 0x95, 0xC8,
    0x9D, 0x48, 0x85, 0xC8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp4096[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x95, 0xC8, 0x9D, 0x48, 0x85, 0xC8, 0x80, 0x8, 0x9D, 0x8, 0x95, 0xC8,
    0x9D, 0x48, 0x85, 0xC8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp8192[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x8C, 0x88, 0x8E, 0x88, 0x86, 0x88, 0x80, 0x8, 0x9D, 0x88, 0x94, 0x48,
    0x9C, 0x88, 0x85, 0xC8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp16384[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0xA8, 0xE8, 0xAE, 0x68, 0xAE, 0xE8, 0x80, 0x8, 0x9D, 0x48, 0x9D, 0xC8,
    0x94, 0x48, 0x9C, 0x48, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp32768[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0xBB, 0x68, 0x99, 0x28, 0x8A, 0x48, 0xBB, 0x48, 0x80, 0x8, 0x91, 0x88,
    0x9D, 0xC8, 0x9C, 0xC8, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp65536[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0xA3, 0x68, 0xBA, 0x48, 0xA9, 0x28, 0xBB, 0x68, 0x80, 0x8, 0x9C, 0xC8,
    0x8C, 0x88, 0x9D, 0x88, 0x80, 0x8, 0xFF, 0xF8,
};

int g2048TileBmp131072[26] = { 13, 12,
    0xFF, 0xF8, 0x80, 0x8, 0x97, 0x48, 0x93, 0x48, 0x97, 0x48, 0x80, 0x8, 0x97, 0x68, 0xA9, 0x28,
    0xAA, 0x48, 0x92, 0x68, 0x80, 0x8, 0xFF, 0xF8,
};

// Direct port of upstream's own real `const byte *TileSprites[]` pointer
// table - a real `int* name[18] = {...}` array declaration, already proven
// working in this project by gameUfoRace.c's own `ufoSprites` table (see
// this file's own header comment).
int* g2048TileSprites[18] =
{
    g2048TileBmp0,      // 0  (empty)
    g2048TileBmp2,      // 1
    g2048TileBmp4,      // 2
    g2048TileBmp8,      // 3
    g2048TileBmp16,     // 4
    g2048TileBmp32,     // 5
    g2048TileBmp64,     // 6
    g2048TileBmp128,    // 7
    g2048TileBmp256,    // 8
    g2048TileBmp512,    // 9
    g2048TileBmp1024,   // 10
    g2048TileBmp2048,   // 11 - reaching this triggers the WIN dialog once
    g2048TileBmp4096,   // 12
    g2048TileBmp8192,   // 13
    g2048TileBmp16384,  // 14
    g2048TileBmp32768,  // 15
    g2048TileBmp65536,  // 16
    g2048TileBmp131072, // 17
};

// Direct port of upstream's own real RotArray[] rotation lookup table.
int g2048RotArray[16] = { 12, 8, 4, 0, 13, 9, 5, 1, 14, 10, 6, 2, 15, 11, 7, 3 };

// Real upstream glyph-icon strings, built as explicit 0-terminated int
// arrays (see this file's own header comment - decimal 21/22 are real
// Gamebuino up/down "confirm/cancel" icon glyphs, not printable ASCII).
char g2048SaveHintText[7] = { 21, 58, 115, 97, 118, 101, 0 };              // "\25:save"
char g2048ResetHintText[8] = { 22, 58, 114, 101, 115, 101, 116, 0 };       // "\26:reset"
char g2048YesNoText[13] = { 21, 58, 121, 101, 115, 32, 32, 32, 22, 58, 110, 111, 0 }; // "\25:yes   \26:no"
char g2048ContinueText[11] = { 21, 58, 99, 111, 110, 116, 105, 110, 117, 101, 0 };    // "\25:continue"
char g2048PressResetText[8] = { 80, 114, 101, 115, 115, 32, 22, 0 };       // "Press \26"

// -----------------------------------------------------------------------------
// Game state
// -----------------------------------------------------------------------------

enum G2048State
{
    G2048_STATE_TITLE = 0,
    G2048_STATE_PLAY = 1,
    G2048_STATE_SPAWN = 2,
    G2048_STATE_HOLDB = 3,
    G2048_STATE_DELETE_WAIT = 4,
    G2048_STATE_DELETE_CONFIRM = 5,
    G2048_STATE_WIN = 6,
    G2048_STATE_LOSE = 7
};

int g2048State;

int g2048Board[16];
int g2048BoardOld[16];
int g2048TempA[16]; // RotateClockwise()'s own scratch buffer
int g2048TempB[16]; // the lose-probe's own "restore if a move was possible" scratch buffer

int g2048Score;
int g2048HighScore;
bool g2048WinState;

int g2048PopupTimer;
int g2048PopupMsgId;

int g2048SpawnCell;
bool g2048SpawnPlaySound;
int g2048SpawnRemaining;
int g2048SpawnFrame;
bool g2048SpawnPendingLoseCheck;
bool g2048SpawnPendingWinBox;
int g2048SpawnPendingTilesOnBoard;

#define G2048_EEPROM_MAGIC 172

// -----------------------------------------------------------------------------
// Text lookups (if-chains returning int* - see gameBlockdude.c's own
// dudeGetMapData() for the established precedent for this pattern)
// -----------------------------------------------------------------------------

// Real upstream newTileStrings[] milestone congratulation table, indexed by
// the tile's own exponent (3=value 8, up to 17=value 131072).
char* g2048GetMilestoneText( int tileIndex )
{
    if( tileIndex == 3 ) return "8! Good!";
    if( tileIndex == 4 ) return "16! Great!";
    if( tileIndex == 5 ) return "32! Awesome!";
    if( tileIndex == 6 ) return "64! Sweet!";
    if( tileIndex == 7 ) return "128! Cool!";
    if( tileIndex == 8 ) return "256! Keep it up!";
    if( tileIndex == 9 ) return "512! Almost there!";
    if( tileIndex == 10 ) return "1024! One more!";
    if( tileIndex == 11 ) return "2048! You win!";
    if( tileIndex == 12 ) return "4096! Step it up!";
    if( tileIndex == 13 ) return "8192! You're good!";
    if( tileIndex == 14 ) return "16384! Keep playing!";
    if( tileIndex == 15 ) return "32768! Unbelievable!";
    if( tileIndex == 16 ) return "65536! Woohoo!";
    return "131072! INSANE!!!";
}

// g2048PopupMsgId encodes either a milestone tile index (3-17, resolved via
// g2048GetMilestoneText() above) or one of these fixed message IDs.
#define G2048_MSG_WELCOME 100
#define G2048_MSG_BACK_ALREADY 101
#define G2048_MSG_SAVED 102
#define G2048_MSG_RESET 103
#define G2048_MSG_SAVE_DELETED 104
#define G2048_MSG_DELETE_CANCELLED 105
#define G2048_MSG_CONTINUED 106

char* g2048GetPopupText( int id )
{
    if( id == G2048_MSG_WELCOME ) return "Welcome to 2048!";
    if( id == G2048_MSG_BACK_ALREADY ) return "Back already?";
    if( id == G2048_MSG_SAVED ) return "Game saved.";
    if( id == G2048_MSG_RESET ) return "Game reset.";
    if( id == G2048_MSG_SAVE_DELETED ) return "Save deleted.";
    if( id == G2048_MSG_DELETE_CANCELLED ) return "Deletion cancelled.";
    if( id == G2048_MSG_CONTINUED ) return "Game continued.";
    return g2048GetMilestoneText( id ); // 3-17
}

void g2048ShowPopup( int id )
{
    g2048PopupMsgId = id;
    g2048PopupTimer = 40; // real upstream's own uniform popup duration
}

// == upstream PopupMessage(), with its own real uninitialized-loop-variable
// bug normalized (see this file's own header comment).
void g2048CheckMilestone()
{
    int maxTile = g2048Board[ 0 ];
    int x;
    for( x = 0; x < 16; x++ )
      if( maxTile < g2048Board[ x ] ) maxTile = g2048Board[ x ];

    for( x = 0; x < 16; x++ )
      if( maxTile == g2048BoardOld[ x ] ) maxTile = 0;

    if( maxTile >= 3 )
      g2048ShowPopup( maxTile );
}

// -----------------------------------------------------------------------------
// Board mechanics - direct ports of upstream's own real functions
// -----------------------------------------------------------------------------

int g2048PickEmptyCell()
{
    int cell = arand( 16 );
    while( g2048Board[ cell ] != 0 )
      cell = arand( 16 );
    return cell;
}

void g2048CompressRight()
{
    int x;
    for( x = 0; x < 16; x = x + 4 )
    {
        int farthest = 4;
        int y;
        for( y = 0; y < 4; y++ )
          if( g2048Board[ x + y ] == 0 ) farthest = y;

        for( y = 3; y >= 0; y = y - 1 )
        {
            if( g2048Board[ x + y ] != 0 && y < farthest && farthest < 4 )
            {
                g2048Board[ x + farthest ] = g2048Board[ x + y ];
                g2048Board[ x + y ] = 0;
                farthest = farthest - 1;
            }
        }
    }
}

// animate=false is used only by the lose-probe below (matches upstream's
// own MoveRight(false) calls there) - score/highscore must stay untouched.
void g2048MoveRight( bool animate )
{
    g2048CompressRight();

    int x;
    for( x = 0; x < 16; x = x + 4 )
    {
        int y;
        for( y = 3; y >= 1; y = y - 1 )
        {
            if( g2048Board[ x + y ] == g2048Board[ x + y - 1 ] && g2048Board[ x + y ] != 0 )
            {
                g2048Board[ x + y ] = g2048Board[ x + y ] + 1;
                // upstream computes this via a repeated-multiply loop
                // (`for(z=0;z<Board2048[x+y];z++) MergeScore*=2;`) - a
                // value-identical shift, using the already-incremented
                // exponent exactly like upstream's own loop does.
                if( animate )
                  g2048Score = g2048Score + ( 1 << g2048Board[ x + y ] );
                g2048Board[ x + y - 1 ] = 0;
            }
        }
    }

    if( animate )
      if( g2048Score > g2048HighScore )
        g2048HighScore = g2048Score;

    g2048CompressRight();
}

void g2048RotateCW()
{
    int i;
    for( i = 0; i < 16; i++ ) g2048TempA[ i ] = g2048Board[ i ];
    for( i = 0; i < 16; i++ ) g2048Board[ i ] = g2048TempA[ g2048RotArray[ i ] ];
}

// -----------------------------------------------------------------------------
// EEPROM persistence (see this file's own header comment)
// -----------------------------------------------------------------------------

bool g2048IsValidGame()
{
    return eeprom_read_byte( 0 ) == G2048_EEPROM_MAGIC;
}

void g2048SaveGame()
{
    eeprom_write_byte( 0, G2048_EEPROM_MAGIC );
    int i;
    for( i = 0; i < 16; i++ )
      eeprom_write_byte( 1 + i, g2048Board[ i ] );
    eeprom_write_dword( 17, g2048Score );
    eeprom_write_dword( 21, g2048HighScore );

    int winByte;
    if( g2048WinState ) winByte = 1;
    else winByte = 0;
    eeprom_write_byte( 25, winByte );

    gbPlayOK();
    g2048ShowPopup( G2048_MSG_SAVED );
}

void g2048RestoreGame()
{
    int i;
    for( i = 0; i < 16; i++ )
      g2048Board[ i ] = eeprom_read_byte( 1 + i );
    g2048Score = eeprom_read_dword( 17 );
    g2048HighScore = eeprom_read_dword( 21 );

    if( eeprom_read_byte( 25 ) == 1 ) g2048WinState = true;
    else g2048WinState = false;
}

// -----------------------------------------------------------------------------
// Drawing - direct ports of upstream's own real ResetDisplay()/DrawBoard()/
// DrawSaveBox()/DrawWinBox()/DrawLoseBox()
// -----------------------------------------------------------------------------

void g2048DrawPopupOverlay()
{
    if( g2048PopupTimer <= 0 ) return;
    g2048PopupTimer = g2048PopupTimer - 1;

    // A local bordered overlay box (see this file's own header comment on
    // why this doesn't use the shim's own `gbPopup()` primitive), the same
    // convention gameMaze.c also uses.
    gbSetColor( 0 );
    gbFillRect( 2, 36, 80, 11 );
    gbSetColor( 1 );
    gbDrawRect( 2, 36, 80, 11 );
    gbSetFont( gbFont3x5 );
    gbCursorX = 4;
    gbCursorY = 38;
    gbPrintString( g2048GetPopupText( g2048PopupMsgId ) );
}

// == upstream ResetDisplay() + the tile-grid loop from DrawBoard().
void g2048DrawBoard()
{
    gbSetColor( 1 );
    gbClear();

    gbDrawBitmap( 56, 0, g2048LogoBmp );
    gbDrawRect( 53, 7, 31, 13 );
    gbDrawRect( 53, 21, 31, 13 );

    gbSetFont( gbFont3x5 );
    gbCursorX = 55;
    gbCursorY = 9;
    gbPrintNumber( g2048Score );
    gbCursorX = 55;
    gbCursorY = 23;
    gbPrintNumber( g2048HighScore );

    gbSetFont( gbFont3x3 );
    gbCursorX = 55;
    gbCursorY = 15;
    gbPrintString( "POINTS" );
    gbCursorX = 55;
    gbCursorY = 29;
    gbPrintString( "HIGH" );

    gbSetFont( gbFont3x5 );
    gbCursorX = 53;
    gbCursorY = 35;
    gbPrintString( g2048SaveHintText );
    gbCursorX = 53;
    gbCursorY = 42;
    gbPrintString( g2048ResetHintText );

    int gx;
    int gy;
    for( gy = 0; gy < 4; gy++ )
    {
        for( gx = 0; gx < 4; gx++ )
          gbDrawBitmap( gx * 13, gy * 12, g2048TileSprites[ g2048Board[ gy * 4 + gx ] ] );
    }
}

void g2048DrawSaveBox()
{
    g2048DrawBoard();
    gbSetColor( 1 );
    gbDrawRect( 16, 4, 51, 40 );
    gbSetColor( 0 );
    gbFillRect( 17, 5, 49, 38 );
    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );
    gbCursorX = 18;
    gbCursorY = 6;
    gbPrintString( "ARE YOU SURE" );
    gbCursorX = 18;
    gbCursorY = 12;
    gbPrintString( "YOU WANT TO" );
    gbCursorX = 18;
    gbCursorY = 18;
    gbPrintString( "DELETE YOUR" );
    gbCursorX = 18;
    gbCursorY = 24;
    gbPrintString( "HI-SCORE AND" );
    gbCursorX = 18;
    gbCursorY = 30;
    gbPrintString( "SAVE DATA?" );
    gbCursorX = 18;
    gbCursorY = 36;
    gbPrintString( g2048YesNoText );
}

void g2048DrawWinBox()
{
    g2048DrawBoard();
    gbSetColor( 1 );
    gbDrawRect( 16, 11, 51, 25 );
    gbSetColor( 0 );
    gbFillRect( 17, 12, 49, 23 );
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbCursorX = 18;
    gbCursorY = 13;
    gbPrintString( "YOU WIN!" );
    gbSetFont( gbFont3x5 );
    gbCursorX = 18;
    gbCursorY = 21;
    gbPrintString( g2048ContinueText );
    gbCursorX = 18;
    gbCursorY = 28;
    gbPrintString( g2048ResetHintText );
}

void g2048DrawLoseBox()
{
    g2048DrawBoard();
    gbSetColor( 1 );
    gbDrawRect( 13, 15, 57, 18 );
    gbSetColor( 0 );
    gbFillRect( 14, 16, 55, 16 );
    gbSetColor( 1 );
    gbSetFont( gbFont5x7 );
    gbCursorX = 15;
    gbCursorY = 17;
    gbPrintString( "GAME OVER" );
    gbSetFont( gbFont3x5 );
    gbCursorX = 27;
    gbCursorY = 25;
    gbPrintString( g2048PressResetText );
}

// -----------------------------------------------------------------------------
// Spawn / new-game sequencing (see this file's own header comment on the
// SPAWN ANIMATION state)
// -----------------------------------------------------------------------------

void g2048StartSpawn( int cell, bool sound, int remaining )
{
    g2048SpawnCell = cell;
    g2048SpawnPlaySound = sound;
    g2048SpawnRemaining = remaining;
    g2048SpawnFrame = 0;
    g2048State = G2048_STATE_SPAWN;
}

// == upstream newGame(makeSound): always spawns exactly 2 tiles, the first
// with the caller's own requested sound, the second always silent (matches
// upstream's own literal `SpawnTile(makeSound); SpawnTile(false);`).
void g2048BeginNewGame( bool makeSound )
{
    g2048Score = 0;
    int i;
    for( i = 0; i < 16; i++ ) g2048Board[ i ] = 0;
    g2048WinState = false;

    g2048SpawnPendingLoseCheck = false;
    g2048SpawnPendingWinBox = false;
    g2048SpawnPendingTilesOnBoard = 0;

    int cell = g2048PickEmptyCell();
    g2048StartSpawn( cell, makeSound, 1 );
}

// == upstream newSave(makeSound): also wipes the highscore, then defers to
// newGame() above.
void g2048BeginFreshSave( bool makeSound )
{
    g2048HighScore = 0;
    g2048BeginNewGame( makeSound );
}

// == upstream's own trailing A-save / hold-B / C-title checks, run once
// per handled "turn" (see this file's own header comment on why this is
// sometimes deferred to the tick a spawn animation finishes rather than
// the original triggering tick).
void g2048HandleIdleButtons()
{
    if( gbPressed( BTN_A ) )
      g2048SaveGame();

    if( gbPressed( BTN_B ) )
    {
        g2048State = G2048_STATE_HOLDB;
        return;
    }

    if( gbPressed( BTN_C ) )
    {
        g2048SaveGame();
        g2048State = G2048_STATE_TITLE;
    }
}

// == upstream's own post-SpawnTile(true) lose-check + WinBox block, run
// once the post-move spawn animation actually finishes.
void g2048ResolveAfterMoveSpawn()
{
    bool didLose = false;

    if( g2048SpawnPendingTilesOnBoard == 15 )
    {
        int i;
        for( i = 0; i < 16; i++ ) g2048TempB[ i ] = g2048Board[ i ];

        // real upstream probe sequence - see this file's own header
        // comment on why only 2 of 4 rotations are tested here (confirmed
        // sufficient, not a bug).
        g2048MoveRight( false );
        g2048RotateCW();
        g2048MoveRight( false );
        g2048RotateCW();
        g2048RotateCW();
        g2048RotateCW();

        int same = 0;
        for( i = 0; i < 16; i++ )
          if( g2048Board[ i ] == g2048TempB[ i ] ) same = same + 1;

        if( same == 16 )
          didLose = true;
        else
        {
            // a move WAS still possible - undo the probe's own trial
            // mutation, matching upstream's own restore-on-non-loss branch.
            for( i = 0; i < 16; i++ ) g2048Board[ i ] = g2048TempB[ i ];
        }
    }

    if( didLose )
    {
        g2048State = G2048_STATE_LOSE;
        return;
    }

    if( g2048SpawnPendingWinBox )
    {
        g2048State = G2048_STATE_WIN;
        return;
    }

    g2048State = G2048_STATE_PLAY;
    g2048HandleIdleButtons();
}

// -----------------------------------------------------------------------------
// States
// -----------------------------------------------------------------------------

void g2048UpdateSpawn()
{
    g2048DrawBoard();

    gbSetColor( 1 );
    int col = ( g2048SpawnCell % 4 ) * 13;
    int row = ( g2048SpawnCell / 4 ) * 12;
    if( g2048SpawnFrame == 0 ) gbDrawBitmap( col, row, g2048NewTileBmp1 );
    else if( g2048SpawnFrame == 1 ) gbDrawBitmap( col, row, g2048NewTileBmp2 );
    else gbDrawBitmap( col, row, g2048NewTileBmp3 );

    g2048DrawPopupOverlay();

    g2048SpawnFrame = g2048SpawnFrame + 1;
    if( g2048SpawnFrame < 3 ) return;

    // commit - real upstream 90%/10% weighted value (a "2" tile vs a "4"
    // tile), preserved exactly.
    int value;
    if( arand( 10 ) ) value = 1;
    else value = 2;
    g2048Board[ g2048SpawnCell ] = value;
    if( g2048SpawnPlaySound ) gbPlayOK();

    if( g2048SpawnRemaining > 0 )
    {
        int cell = g2048PickEmptyCell();
        g2048StartSpawn( cell, false, g2048SpawnRemaining - 1 );
        return;
    }

    if( g2048SpawnPendingLoseCheck )
      g2048ResolveAfterMoveSpawn();
    else
      g2048State = G2048_STATE_PLAY;
}

// == upstream loop()'s own main body.
void g2048UpdatePlay()
{
    g2048DrawBoard();

    int i;
    for( i = 0; i < 16; i++ ) g2048BoardOld[ i ] = g2048Board[ i ];

    // Four independent real upstream `if`s, not `else if` - see this
    // file's own header comment.
    bool buttonPressed = false;
    if( gbPressed( BTN_LEFT ) )
    {
        buttonPressed = true;
        g2048RotateCW();
        g2048RotateCW();
        g2048MoveRight( true );
        g2048RotateCW();
        g2048RotateCW();
    }
    if( gbPressed( BTN_RIGHT ) )
    {
        buttonPressed = true;
        g2048MoveRight( true );
    }
    if( gbPressed( BTN_UP ) )
    {
        buttonPressed = true;
        g2048RotateCW();
        g2048MoveRight( true );
        g2048RotateCW();
        g2048RotateCW();
        g2048RotateCW();
    }
    if( gbPressed( BTN_DOWN ) )
    {
        buttonPressed = true;
        g2048RotateCW();
        g2048RotateCW();
        g2048RotateCW();
        g2048MoveRight( true );
        g2048RotateCW();
    }

    g2048CheckMilestone();

    int same = 0;
    int tilesOnBoard = 0;
    bool winBox = false;
    for( i = 0; i < 16; i++ )
    {
        if( g2048Board[ i ] != 0 ) tilesOnBoard = tilesOnBoard + 1;
        if( g2048BoardOld[ i ] == g2048Board[ i ] ) same = same + 1;
        if( g2048Board[ i ] == 11 && g2048WinState == false )
        {
            g2048WinState = true;
            winBox = true;
        }
    }

    if( same != 16 )
    {
        int cell = g2048PickEmptyCell();
        g2048SpawnPendingLoseCheck = true;
        g2048SpawnPendingTilesOnBoard = tilesOnBoard;
        g2048SpawnPendingWinBox = winBox;
        g2048StartSpawn( cell, true, 0 );
        g2048DrawPopupOverlay();
        return;
    }

    if( buttonPressed )
      gbPlayCancel();

    g2048HandleIdleButtons();
    g2048DrawPopupOverlay();
}

void g2048UpdateHoldB()
{
    g2048DrawBoard();
    g2048DrawPopupOverlay();

    if( gbPressed( BTN_A ) )
    {
        g2048State = G2048_STATE_DELETE_WAIT;
        return;
    }
    if( gbReleased( BTN_B ) )
    {
        g2048BeginNewGame( true );
        g2048ShowPopup( G2048_MSG_RESET );
    }
}

void g2048UpdateDeleteWait()
{
    g2048DrawSaveBox();
    g2048DrawPopupOverlay();

    if( gbReleased( BTN_B ) )
      g2048State = G2048_STATE_DELETE_CONFIRM;
}

void g2048UpdateDeleteConfirm()
{
    g2048DrawSaveBox();
    g2048DrawPopupOverlay();

    if( gbPressed( BTN_A ) )
    {
        g2048BeginFreshSave( true );
        g2048ShowPopup( G2048_MSG_SAVE_DELETED );
    }
    else if( gbPressed( BTN_B ) )
    {
        gbPlayOK();
        g2048ShowPopup( G2048_MSG_DELETE_CANCELLED );
        g2048State = G2048_STATE_PLAY;
    }
}

void g2048UpdateWin()
{
    g2048DrawWinBox();
    g2048DrawPopupOverlay();

    if( gbPressed( BTN_A ) )
    {
        gbPlayOK();
        g2048ShowPopup( G2048_MSG_CONTINUED );
        g2048State = G2048_STATE_PLAY;
    }
    else if( gbPressed( BTN_B ) )
    {
        g2048BeginNewGame( true );
        g2048ShowPopup( G2048_MSG_RESET );
    }
}

void g2048UpdateLose()
{
    g2048DrawLoseBox();
    g2048DrawPopupOverlay();

    if( gbPressed( BTN_B ) )
    {
        g2048BeginNewGame( true );
        g2048ShowPopup( G2048_MSG_RESET );
    }
}

// == upstream's own real post-titleScreen() branch, run both at initial
// boot and every time Button C brings the title screen back.
void g2048OnTitleDismiss()
{
    gbPickRandomSeed();
    if( g2048IsValidGame() )
    {
        g2048RestoreGame();
        g2048ShowPopup( G2048_MSG_BACK_ALREADY );
        g2048State = G2048_STATE_PLAY;
    }
    else
    {
        g2048BeginFreshSave( false );
        g2048ShowPopup( G2048_MSG_WELCOME );
    }
}

void g2048UpdateTitle()
{
    gbSetColor( 1 );
    gbSetFont( gbFont3x5 );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 1, g2048TitleBmp );
    gbCursorX = 2;
    gbCursorY = 33;
    gbPrintString( "JWinslow23 presents" );
    gbCursorX = 26;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      g2048OnTitleDismiss();
}

void game2048_init()
{
    gbBegin();

    g2048Score = 0;
    g2048HighScore = 0;
    g2048WinState = false;
    g2048PopupTimer = 0;
    g2048PopupMsgId = 0;

    int i;
    for( i = 0; i < 16; i++ )
    {
        g2048Board[ i ] = 0;
        g2048BoardOld[ i ] = 0;
    }

    g2048SpawnPendingLoseCheck = false;
    g2048SpawnPendingWinBox = false;
    g2048SpawnPendingTilesOnBoard = 0;

    g2048State = G2048_STATE_TITLE;
}

void game2048_update()
{
    if( !gbUpdate() ) return;

    if( g2048State == G2048_STATE_TITLE ) g2048UpdateTitle();
    else if( g2048State == G2048_STATE_PLAY ) g2048UpdatePlay();
    else if( g2048State == G2048_STATE_SPAWN ) g2048UpdateSpawn();
    else if( g2048State == G2048_STATE_HOLDB ) g2048UpdateHoldB();
    else if( g2048State == G2048_STATE_DELETE_WAIT ) g2048UpdateDeleteWait();
    else if( g2048State == G2048_STATE_DELETE_CONFIRM ) g2048UpdateDeleteConfirm();
    else if( g2048State == G2048_STATE_WIN ) g2048UpdateWin();
    else g2048UpdateLose();

    gbRenderFrame();
}
