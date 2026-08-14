#ifndef GAMEBUINO_SDL2_H
#define GAMEBUINO_SDL2_H

// -----------------------------------------------------------------------------
// Keybind mapping consumed by CInput.c - see src/sdl3/gamebuinoSDL3.h's own
// header comment for the full "which real Gamebuino button maps to which
// CInput slot" reasoning (ButX = Button C, ButRB = real-gray toggle, ButLB
// = the combined pixel-grid/glow/CRT effect cycle, ButLT/ButRT = real
// volume down/up, ButY = mute) and the explicit keyboard choice (X=A, C=B,
// S=mute, D=Button C); unchanged here, only the plain letter-key SDLK_*
// spellings differ between SDL2 and SDL3.
//
// Non-letter SDLK_* names (UP/DOWN/LEFT/RIGHT/ESCAPE/RETURN) are identical
// between SDL2 and SDL3 - only the plain letter keys differ: SDL2's own
// SDLK_x/SDLK_c/SDLK_s/SDLK_l/SDLK_r/SDLK_d/SDLK_g/SDLK_w/SDLK_z are
// lowercase (matching their ASCII values, SDL2's original convention),
// while SDL3 renamed every letter keycode to uppercase (SDLK_X/etc,
// matching the physical key label instead) - confirmed against the
// sibling Tinyjoypad_SDL project's own identical SDL2/SDL3 divergence
// (its own tinyjoypadSDL2.h header comment).
//
// Button R (real-gray-color) additionally accepts v as an alternate key.
// The combined effect cycle (g) additionally accepts l/w/z as alternates -
// CInput.c's own keyboard switch has multiple case labels falling through
// to the same Buttons field for these. Real volume down/up (ButLT/ButRT,
// gamepad L2/R2 only) has no keyboard key of its own at all.

#define BUTTON_UP SDLK_UP
#define BUTTON_DOWN SDLK_DOWN
#define BUTTON_LEFT SDLK_LEFT
#define BUTTON_RIGHT SDLK_RIGHT
#define BUTTON_A SDLK_x          // Gamebuino Button A
#define BUTTON_B SDLK_c          // Gamebuino Button B
#define BUTTON_MENU SDLK_ESCAPE  // Start (quit-confirmation dialog)
#define BUTTON_VOLUP SDLK_r      // Gamebuino Button R (real-gray-color mode toggle) - routed into CInput's ButRB slot
#define BUTTON_VOLUP2 SDLK_v     // alternate key for Button R
#define BUTTON_SOUNDSWITCH SDLK_s // Gamebuino Button Y (global mute toggle) - routed into CInput's ButY slot
#define BUTTON_GLOWSWITCH SDLK_d // Gamebuino Button C - routed into CInput's ButX slot
#define BUTTON_DARKSWITCH SDLK_RETURN // redundant alternate Start keybind (d is already Button C above)
#define BUTTON_EFFECTCYCLE SDLK_g // pixel-grid/glow/CRT effect cycle - routed into CInput's ButLB (L1) slot
#define BUTTON_EFFECTCYCLE2 SDLK_l // alternate key for the effect cycle
#define BUTTON_EFFECTCYCLE3 SDLK_w // alternate key for the effect cycle
#define BUTTON_EFFECTCYCLE4 SDLK_z // alternate key for the effect cycle

#define BUTTON_COUNT 18

// This project's own real Gamebuino Classic LCD is 84x48 (LCD_WIDTH x
// LCD_HEIGHT, see machineDependent.h), scaled 7x plus a 26px/12px letterbox
// border to exactly fill a 640x360 window (84*7+26*2=640, 48*7+12*2=360) -
// matching the sibling gamebuino_classic_vircon32 build's own real
// TILE_SCALE/ORIGIN_X/ORIGIN_Y layout exactly, and src/sdl3's own identical
// choice. The window is resizable and the content re-scales/re-centers live
// via SDL2's own logical-size feature (SDL_RenderSetLogicalSize).
#define DEFAULT_WINDOW_WIDTH 640
#define DEFAULT_WINDOW_HEIGHT 360

#endif
