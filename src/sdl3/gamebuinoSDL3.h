#ifndef GAMEBUINO_SDL3_H
#define GAMEBUINO_SDL3_H

// -----------------------------------------------------------------------------
// Keybind mapping consumed by CInput.c (copied byte-for-byte verbatim from
// the sibling Tinyjoypad_SDL project - it's fully generic input-polling
// plumbing with no TinyJoypad-specific logic in it at all, only referencing
// these macro NAMES, which this project's own header just repoints at
// different physical keys/gamepad buttons and different real Gamebuino
// semantics).
//
// CInput's own SButtons struct (CInput.h) has exactly one field per real
// gamepad face/shoulder/trigger button (ButA/ButB/ButX/ButY/ButLB/ButRB/
// ButLT/ButRT) - not a dedicated "ButC" - so real Gamebuino Button C is
// mapped onto ButX (the same slot Tinyjoypad_SDL's own CInput.c already
// routes its BUTTON_GLOWSWITCH keybind into), Button R (real-gray-color
// mode) is mapped onto ButRB, and the combined effect cycle below is
// mapped onto ButLB - this project's own real Button L/pixel-grid no
// longer has a dedicated slot of its own at all (see below).
//
// Real gamepad layout this produces (Xbox/Switch-style face buttons):
// South=A, East=B, West=C, North=mute, Left shoulder(L1)=effect cycle,
// Right shoulder(R1)=real-gray-color, Left trigger(L2)=volume down,
// Right trigger(R2)=volume up, Start/Back=Start (quit-confirm dialog).
//
// Keyboard layout is a direct, explicit choice (not inherited from
// Tinyjoypad_SDL's own Z/X/S/G/D scheme): X=A, C=B, S=mute, D=Button C.
// Button R (real-gray-color) additionally accepts V as an alternate key -
// CInput.c's own keyboard switch has multiple case labels falling through
// to the same Buttons field for these, so any one of the alternates works
// identically to the primary key.
//
// BUTTON_DARKSWITCH is kept defined only because CInput.c's own keyboard-
// event switch references it (same as Tinyjoypad_SDL's own file) - D is
// already claimed by Button C above, so this is routed to Enter instead,
// a genuine (if redundant with Escape) second keyboard shortcut for
// Start, not dead.
//
// BUTTON_EFFECTCYCLE (G, plus L/W/Z as alternate keys) steps a real 3-bit
// counter (sdlBackend.c's own gEffectState) through ALL 8 combinations of
// pixel-grid/glow/CRT, per direct request - a genuine superset of
// Tinyjoypad_SDL's own curated 5-state cycle. Pixel-grid itself no longer
// has its own separate toggle button (L used to drive it alone, directly)
// - it's purely one of the three bits reachable through this combined
// cycle now, freeing L's own physical key/slot to become an alternate for
// this instead. Routed into CInput's ButLB slot (gamepad L1) - the same
// real keyboard letter Tinyjoypad_SDL's own BUTTON_GLOWSWITCH uses for its
// own, differently-shaped cycle, even though that exact macro NAME was
// already claimed here for Gamebuino Button C (see above) - a fresh macro
// name was needed, not a fresh key.
//
// Real analog volume down/up (ButLT/ButRT, gamepad L2/R2 only - read
// directly off the real trigger axes CInput.c's own joystick-axis handler
// already populates, no BUTTON_* keyboard macro of their own at all) step
// real output volume, matching Tinyjoypad_SDL's own PageDown/PageUp step
// size (0.05f/press) and behavior exactly, just on the two real trigger
// axes instead of that project's own shoulder buttons (both already
// spoken for here). No keyboard equivalent was added since S already
// covers the keyboard's own mute need and no specific key was requested
// for gradual volume.
// -----------------------------------------------------------------------------

#define BUTTON_UP SDLK_UP
#define BUTTON_DOWN SDLK_DOWN
#define BUTTON_LEFT SDLK_LEFT
#define BUTTON_RIGHT SDLK_RIGHT
#define BUTTON_A SDLK_X          // Gamebuino Button A
#define BUTTON_B SDLK_C          // Gamebuino Button B
#define BUTTON_MENU SDLK_ESCAPE  // Start (quit-confirmation dialog)
#define BUTTON_VOLUP SDLK_R      // Gamebuino Button R (real-gray-color mode toggle) - routed into CInput's ButRB slot
#define BUTTON_VOLUP2 SDLK_V     // alternate key for Button R
#define BUTTON_SOUNDSWITCH SDLK_S // Gamebuino Button Y (global mute toggle) - routed into CInput's ButY slot
#define BUTTON_GLOWSWITCH SDLK_D // Gamebuino Button C - routed into CInput's ButX slot
#define BUTTON_DARKSWITCH SDLK_RETURN // redundant alternate Start keybind (D is already Button C above)
#define BUTTON_EFFECTCYCLE SDLK_G // pixel-grid/glow/CRT effect cycle - routed into CInput's ButLB (L1) slot
#define BUTTON_EFFECTCYCLE2 SDLK_L // alternate key for the effect cycle
#define BUTTON_EFFECTCYCLE3 SDLK_W // alternate key for the effect cycle
#define BUTTON_EFFECTCYCLE4 SDLK_Z // alternate key for the effect cycle

#define BUTTON_COUNT 18

// This project's own real Gamebuino Classic LCD is 84x48 (LCD_WIDTH x
// LCD_HEIGHT, see machineDependent.h), scaled 7x plus a 26px/12px letterbox
// border to exactly fill a 640x360 window (84*7+26*2=640, 48*7+12*2=360) -
// matching the sibling gamebuino_classic_vircon32 build's own real
// TILE_SCALE/ORIGIN_X/ORIGIN_Y layout exactly (see sdlBackend.c's own VIDEO
// section comment), not Tinyjoypad_SDL's own different 128x64/5x/20px
// layout. The window is resizable and the content re-scales/re-centers
// live via SDL's own logical-presentation feature.
#define DEFAULT_WINDOW_WIDTH 640
#define DEFAULT_WINDOW_HEIGHT 360

#endif
