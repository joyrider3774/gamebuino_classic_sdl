#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// Cruiser (Michael Specht / github.com/specht/cruiser) - a real portal-
// rendering 3D shooter for Gamebuino Classic: convex map "segments" (a 2D
// floor outline plus a floor/ceiling height each) connected by portals,
// rendered with Sutherland-Hodgman frustum clipping done in view space,
// fixed-point 3D math throughout, and sub-pixel-accurate Bresenham line
// drawing for the wireframe polygon edges. No LICENSE file or license
// header exists anywhere in the real upstream repo (confirmed directly -
// no top-level LICENSE/COPYING file, no header comment in cruiser.ino) -
// matching this project's own earlier porting-priority audit's "no license
// specified" finding for this game. Real author confirmed via the repo's
// own git history (`specht/cruiser`, Michael Specht).
//
// The player flies freely through the level (accelerate/turn/pitch, a
// gentle auto-leveling pitch, a subtle vertical "wobble" while moving) and
// can fire a pair of laser bolts that travel through the world and vanish
// on hitting a wall or a closed door - there is no enemy/target and no
// win/lose condition of any kind (confirmed by reading the entire real
// source: no health, lives, score, or "game over" concept exists anywhere
// upstream) - this is a real, playable flythrough tech demo of the engine
// itself, not a scored arcade game, and is ported that way faithfully
// rather than having an artificial objective added.
//
// PORTING ENTRY POINT: real upstream ships several `#define`-gated
// features that are left COMMENTED OUT in the actual shipped/compiled
// configuration (DEBUG, MONITOR_RAM, SHOW_FRAME_TIME, SHOW_TITLE_SCREEN,
// ENABLE_STRAFE, ENABLE_MAP - all `// #define X`, never enabled) - only
// COLLISION_DETECTION, ENABLE_SHOOTING, ROLL_SHIP, WOBBLE_SHIP,
// CLIP_TO_FRUSTUM, and SUB_PIXEL_ACCURACY are genuinely active. This port
// ports exactly the real active/compiled configuration and drops the
// dead `#ifdef`-gated code for the disabled features entirely (map mode,
// strafing, the debug/RAM/frame-time HUD text, and upstream's own
// gb.titleScreen() call, which - because SHOW_TITLE_SCREEN is disabled -
// is itself real dead code upstream: real `title_screen()` in the actual
// shipped build is just a state RESET, not a UI screen, called once from
// setup() and again as a real mid-game "respawn" whenever Button C is
// pressed). The `port/` subfolder (a separate, unrelated desktop/GLUT
// development harness gated behind its own `PORT_ENABLED` define, which
// this project's real build never sets) was not read or ported from at
// all, per this game's own porting brief. `sprites.h`'s entire contents
// (sprite/sprite_polygon and the sample sprite data) and cruiser.ino's own
// `render_sprite()` are also real, confirmed-dead code (the only call
// site is commented out) and were not ported either. `wall_normal_templates[]`/
// `wall_normals[]` (map.h) and `vec3d::maximize_length_16()`/
// `operator>>`/`operator>>=`/`operator<<=` (cruiser.ino) are likewise real,
// confirmed-unused leftover infrastructure (never read/called anywhere)
// and were dropped for the same reason.
//
// A real title screen (dismissed by a fresh Button-A press) was ADDED -
// not a restoration of upstream's own disabled gb.titleScreen(), which
// (see above) never actually shows anything in the real shipped build -
// but a genuine Vircon32-cartridge-specific requirement, matching every
// other game in this cartridge's own established pattern, since this
// menu launches many games from one shared A-press-driven menu (real
// standalone Gamebuino hardware has no such menu to bleed a button-press
// from). Because THIS game reads Button A directly for forward thrust
// (unlike e.g. Pong Solo, whose gameplay buttons don't overlap its own
// menu-select button), a fresh, real risk of the exact "menu-launch
// button bleeding into the game" bug class this project's CLAUDE.md
// documents was introduced by adding this title screen - fixed by
// genuinely activating upstream's own `allow_steering` gate (declared
// upstream but never given a real reason to start false, since
// SHOW_TITLE_SCREEN is disabled there): `cruiAllowSteering` starts false
// the instant the title screen is dismissed, and every directional/thrust
// control (matching upstream's own real `if (allow_steering) { ... }`
// block exactly) stays disarmed until a genuine `gbReleased(BTN_A)` fires.
//
// FIXED-POINT MATH AND THE PLATFORM-FORCED >> -> / CONVERSION: this
// dialect's `>>` is a LOGICAL (zero-fill) shift, never arithmetic/sign-
// extending, unlike real AVR's own genuine signed-int `>>`, which this
// engine's entire fixed-point 3D math relies on as a real "divide a
// SIGNED value by a power of two" idiom throughout (3D coordinates,
// directions, sin/cos outputs, dot products, and rotation math are all
// genuinely signed and routinely negative). Every `>>` in the real vec3d
// math (component-wise `>>8` in add/scale/dot/cross/normalize/rotate,
// the `>>` in project_vertex/transform_world_space_to_view_space/
// clip_polygon_against_plane/translate7/collision_detection, the camera
// rotation trig outputs, and the sub-pixel line rasterizer's own final
// `>>4` pixel-coordinate extraction) was converted to real `/` division
// instead - never behaviorally wrong even where the value happens to
// always be non-negative (division and a logical shift agree exactly
// whenever the dividend isn't negative), and correct - unlike a logical
// shift - wherever it can go negative, which is the normal case
// throughout this engine's own real 3D math. `>>` was kept ONLY for
// genuine non-negative bit/byte-unpacking operations that are not signed
// division at all: extracting a wall/portal/door index's packed nibble/
// top-bits from an encoded 0-255 map-data byte, `draw_edges` bitmask
// tests, `segments_touched[]` bit-array indexing, and one already-guarded
// case (`log2()` - not actually used by any live code path and dropped
// entirely, see below) - and for the one place a real per-frame elapsed-
// time value (`micros_per_frame`) is shifted, which is provably always
// non-negative by construction (see the timing note below) even though
// this port keeps it as a constant rather than a measured value.
//
// TIMING: real upstream reads raw Arduino `millis()`/`micros()` directly
// (not through `gb.`, so this shim has no equivalent primitive) to time
// door-open/close animation and to scale movement by real elapsed time
// each tick. Since `gbUpdate()` throttles every game here to a genuinely
// FIXED tick rate (never overridden here, so the real 20fps default - see
// this project's CLAUDE.md's own frame-rate-default writeup), this port
// treats the elapsed time per logic tick as an exact constant
// (`CRUI_MICROS_PER_FRAME` = 50000us = 1000/20fps) rather than a measured
// value - real hardware's own actual elapsed time jitters by a small,
// inconsequential amount frame to frame (Arduino loop overhead etc); this
// port's own throttle is exact, so there is no jitter to model at all.
// `cruiMillis()` (`gbFrameCount * 50`) stands in for real `millis()` for
// door-animation/shot-cooldown timing, for the same reason - `gbFrameCount`
// is the only free-running tick counter this shim exposes.
//
// STRUCTURAL DIALECT REWRITES (this dialect has no classes/methods/
// operator overloading/references/default parameters/ternary operator -
// see VIRCON32_C_DIALECT.md):
// - Every `vec3d`/`vec3d_16` method and operator (`+`,`-`,`+=`,`-=`,`*`,
//   `dot()`,`cross()`,`length()`,`normalize()`,`rotate()`,`divby256()`,
//   `translate7()`) became a free `cruiVec3dXxx()` function taking
//   pointers. `vec3d_16` itself was dropped entirely and folded into one
//   `cruiVec3d` type - real upstream only used the 16-bit variant to save
//   AVR RAM for values that are already small by construction (post-
//   divby256 camera basis vectors); Vircon32 has no narrower int type at
//   all (every value is one 32-bit word regardless), so keeping two
//   parallel vector types would have bought nothing but duplication.
// - By-value struct returns (illegal here - a function's return value
//   must be exactly one word, and every real `vec3d operator+/-/*` etc.
//   upstream RETURNS a `vec3d` BY VALUE) became `void ...(..., cruiVec3d*
//   out)` out-pointer functions. Every one of these computes into a local
//   temporary and only assigns `*out = temp;` as its very last statement
//   (a real, necessary safety measure, not just a style choice - upstream's
//   own real C++ compiler-generated temporary-then-assign behavior is
//   exactly what makes e.g. `*target = target->cross(a)` safe from
//   self-aliasing in the real source; a naive direct-into-`*out` port
//   would have introduced a real aliasing bug at that exact call site,
//   where `out` IS one of the two input pointers, since `cross()`'s own
//   per-component formula reads a DIFFERENT input component than the one
//   just written on the previous line).
// - `vec3d`'s own `x/y/z` union-with-`v[3]`-array duality (used upstream
//   for real "loop over 3 components" code in `+=`/`-=`/`dot()`/`cross()`/
//   `rotate()`) was flattened to plain named `x`/`y`/`z` fields ONLY (no
//   `v[]` array view) and every one of those loops was hand-unrolled into
//   3 explicit x/y/z statements - a deliberate choice, not a dialect
//   requirement (array-typed struct members do work here, confirmed
//   directly - see below), made to avoid a real, only-partially-proven
//   pattern (a prior port's own header comment flagged this exact
//   avoidance as untested caution, not a confirmed wall) in code this
//   dense and correctness-critical.
// - `loop_through_segment_walls()`'s own real generic `bool(*callback)
//   (wall_loop_info*, void*)` function-pointer parameter (used to share
//   one wall-iteration loop between `collision_detection_callback()` and
//   `render_segment_callback()`) was NOT ported as a real function
//   pointer - but this was a real mistake in this port's own first-draft
//   investigation, since corrected (see VIRCON32_C_DIALECT.md's own
//   "Function pointers" section for the full writeup): function pointers
//   DO work on this platform, proven directly by this very cartridge's
//   own `menu.h`/`menuGameList.c` (`GameFunc* init`/`update`/`onResume`,
//   driving all ~90 games via real `&function` assignment). What actually
//   fails is only the raw, standard-C *inline* declaration syntax
//   (`bool (*callback)(wall_loop_info*, void*);` -
//   `fatal error: expected a type`) - the dialect's usual
//   `<type> <name>` rule applies to function types too, so the fix is to
//   `typedef` the signature first (`typedef bool(wall_loop_info*, void*)
//   WallCallback;`) and declare the parameter as `WallCallback* callback`,
//   exactly like `menu.h`'s own `GameFunc`. This file was NOT reworked to
//   use that idiom retroactively, since the already-shipped, already-
//   verified plain int-mode dispatch below works correctly and a
//   behavior-only doc fix doesn't warrant a functional-code churn - but a
//   future porting agent should reach for the real function-pointer idiom
//   directly rather than re-discover this false negative independently.
//   Ported instead as `cruiLoopThroughSegmentWalls(..., int
//   callbackMode)`, an int-mode dispatch
//   (`CRUI_CB_COLLISION`/`CRUI_CB_RENDER`) that calls one of the two real
//   callback functions directly via a plain `if`/`else` at the one call
//   site inside the shared loop - the wall-iteration logic itself stays a
//   single shared function, only the dynamic dispatch changed shape. The
//   real `void* callback_info` threading this enabled upstream was also
//   dropped in favor of plain global scratch state per callback
//   (`cruiColl*`/the render-side globals), since a real, permanent
//   two-case dispatch has no need for it - matching this project's own
//   established "flatten to plain C, no dynamic dispatch" convention used
//   throughout every other port.
// - `polygon2`/`polygon4`/`textured_polygon4` (upstream's own separate,
//   smaller polygon variants storing only 2/4 vertex slots instead of the
//   full `MAX_POLYGON_VERTICES` 8, purely to save AVR RAM, then
//   deliberately force-cast through a plain `polygon*` - upstream's own
//   header comment literally says "THIS SAVES US 48 BYTES... PRECIOUS
//   PRECIOUS BYTES") were dropped. This pointer-reinterpretation between
//   differently-sized struct layouts has no real benefit on Vircon32 (no
//   comparable RAM pressure) and no proven-safe equivalent in a type-
//   checked, word-addressed dialect - `textured_polygon4` was unused
//   regardless (upstream's own real active code never uses texture
//   mapping). Every one of the 4 real scratch-polygon globals (`_wall`,
//   `_portal`, `_line`, `clipped_polygon`) is instead a full, uniform
//   `cruiPolygon` (8-vertex capacity) here.
// - `segment`'s real bitfields (`byte floor_height:5`, etc - a genuine
//   AVR RAM-packing trick, 8 real bytes total) became plain `int` fields
//   (no bit-fields exist in this dialect at all) - since the real map.h
//   data table's own initializer values are already the plain field
//   values themselves (not pre-packed bit patterns), the exact same
//   positional initializer list ports verbatim with no bit-math needed.
//   `frustum_plane_2d_vertex`'s own `int16_t x:12, y:12` bitfields
//   (packing 2 signed 12-bit screen coordinates into 3 bytes) likewise
//   became plain `int` fields - real values here (screen-space fixed-
//   point coordinates, max ~±700) fit comfortably inside a 12-bit signed
//   range, so dropping the bitfield changes nothing observable, only the
//   (irrelevant here) storage size.
// - The real byte-addressed, hand-packed `frustum_stack[48]` (raw pointer
//   arithmetic over mixed-type packed records: a segment-id byte, a
//   vertex-count byte, then N 3-byte 2D vertices) has no meaningful
//   equivalent in a word-addressed dialect with no byte addressing at
//   all - ported instead as a plain fixed-size array of 16 whole
//   `cruiFrustumStackEntry` records (each holding its own real 8-slot
//   `cruiFrustumVertex[8]` array - confirmed directly that a struct
//   containing an array of ANOTHER struct type compiles and runs
//   correctly here, see the dialect note below), preserving the exact
//   same LIFO push/pop order and the same graceful "stack full -> stop
//   enqueueing further portal-render jobs" degradation upstream's own
//   `push_frustum()` already has (returns a null pointer, checked by
//   every real call site) - just with real headroom (16 vs upstream's
//   own real ~3-4-deep practical capacity at 48 bytes) Vircon32 can
//   easily afford, since there is no AVR-style RAM pressure here at all.
// - `render_segment_callback_info`/`collision_detection_callback_info`
//   (upstream's own real structs threaded through the dropped void*
//   callback-info parameter above) were flattened into plain global
//   scratch variables read/written directly by the two real callback
//   functions, per the function-pointer removal above.
//
// A REAL, PREVIOUSLY-UNDOCUMENTED COMPILER QUIRK FOUND WHILE PROBING THIS
// PORT'S OWN ARCHITECTURE (worth flagging for VIRCON32_C_DIALECT.md
// directly, not just noted here): a bare integer literal `0` cannot be
// used as a null-pointer constant for ANY pointer type in this compiler -
// not just named struct pointers, but plain `int*`/`void*` too. Every
// `return 0;` from a pointer-returning function, every `SomePtr* p = 0;`
// initialization, and every `if (p != 0)` comparison fails to compile
// with "cannot assign int to const-qualified <type>*" / "invalid operands
// for equality comparison" - contradicting ordinary C's implicit null-
// pointer-constant conversion. Confirmed via direct, minimal, isolated
// probes against the real compiler (not assumed). Fixed throughout this
// file by using an explicit cast everywhere a null pointer is produced or
// compared (`(cruiFrustumVertex*)0`, `(cruiPolygon*)0`, etc) - this
// affects `cruiPushFrustum()`/`cruiPopFrustum()`'s own "stack full/empty"
// null returns, `cruiRenderPolygon()`'s own "clipped away" null return,
// and every call site that checks either result.
//
// Two more real dialect facts confirmed directly by isolated compiler
// probes before committing to this file's own architecture (both
// contradicted an earlier, over-cautious assumption from a prior port's
// own header comment, which had avoided testing this at all): a struct
// CAN contain a plain array member, a nested named-struct member, and an
// array-of-another-struct-type member, all three - AS LONG AS the
// variable of that struct type is declared WITHOUT the `struct` keyword
// at the usage site (`cruiPolygon p;`, matching VIRCON32_C_DIALECT.md's
// own "no `struct` keyword needed at use" - it turns out to be REQUIRED
// to omit it for these nested-member cases, not merely optional; writing
// `struct cruiPolygon p;` at a variable-declaration site fails to parse
// here even though it's exactly how the TYPE itself must be defined).
// Pointer arithmetic (`ptr + 1`) and bracket indexing on a pointer into
// an array-of-struct struct member also both work correctly. This
// confirms the natural, direct translation of `polygon`/`frustum_stack`
// (real arrays of small structs, nested inside other structs) was safe
// to use throughout this file, rather than needing a further redesign
// into parallel flat scalar arrays.
//
// REAL UPSTREAM QUIRKS/BUGS PRESERVED DELIBERATELY:
// - No near-plane clipping exists anywhere in this engine (real upstream
//   only ever clips against the four SCREEN-EDGE frustum planes, derived
//   fresh from each portal's own on-screen extent - there is no genuine
//   near/far clip plane at all). `project_vertex()`'s own `-16777216 /
//   p.z` divide can reach p.z==0 for in-view-frustum-but-behind-camera
//   geometry this engine's own math was never designed to exclude - a
//   real, inherited characteristic of the original engine, not something
//   this port introduced. **This was initially assumed dormant/unreached
//   by the real 27-segment shipped map during normal play, but a live
//   user session hit a genuine division-by-zero crash here** - unlike
//   real AVR hardware, which doesn't hard-trap on an integer divide by
//   zero, this platform's own CPU does. A platform-forced fix (not a
//   preference): `cruiProjectVertex()` now clamps a zero `p.z` to 1
//   before dividing, letting the point project somewhere extreme/
//   off-screen (matching the same kind of visual glitch real upstream's
//   own missing near-clip would already produce for a vertex very close
//   to zero) instead of crashing outright.
// - `vec3d::normalize()`'s own `(1L<<24) / length()` can likewise divide
//   by zero for a genuinely zero-length wall-normal vector (a degenerate,
//   zero-length map wall edge) - not confirmed hit in practice (no
//   degenerate walls exist in the real 27-segment map data), but given
//   the sibling `project_vertex()` risk above turned out to be live, this
//   was guarded proactively too rather than waiting for a second live
//   crash report: `cruiVec3dNormalize()` now returns early (leaving `*v`
//   as the zero vector it already is) when the input length is zero.
// - `collision_detection_callback()`'s real "am I facing this wall"
//   check, the real wall-vs-portal branch, the real project-trajectory-
//   onto-wall sliding math, and the exact real bump-distance (16384) are
//   all preserved verbatim, including upstream's own real reliance on
//   `n.dot(dir) > 0` (a real, direction-aware facing test - the exact
//   class of bug this project's CLAUDE.md's own "Pirates ported, then
//   reverted" writeup warns to check for in ported collision code; this
//   one traces back to real, correct upstream math, not a doubtful
//   substitution introduced by this port).
// - The real per-segment `normals` byte-offset field (map.h's own "N"
//   column) is real per-segment map data with real values in every row,
//   but is never actually READ by any live code path anywhere upstream
//   (confirmed directly - only ever copied around as part of a whole-
//   struct `memcpy_P`) - kept as a genuinely inert `normals_offset`
//   struct field here purely for 1:1 structural fidelity with the real
//   map.h table, exactly like real upstream's own dead field.
//
// SHIM GAPS: none found - every real primitive this game needs
// (`gbDrawPixel`, `gbPressed`/`gbReleased`/`gbRepeat`, `gbFrameCount`,
// `gbPrintString`, `gbSetFont`) already existed. The one real timing gap
// (no raw `millis()`/`micros()` equivalent) was worked around locally as
// described above, not promoted to the shared shim, since every other
// game in this cartridge already manages its own timing/animation pacing
// off `gbFrameCount` alone and has no comparable need for a real
// millisecond clock.

#define CRUI_SCREEN_WIDTH 84
#define CRUI_SCREEN_HEIGHT 48
#define CRUI_FIXED_POINT_SCALE 4
#define CRUI_MAX_POLYGON_VERTICES 8
#define CRUI_MAX_SHOTS 12
#define CRUI_DOOR_COUNT 2
#define CRUI_SEGMENTS_TOUCHED_SIZE 4
#define CRUI_FRUSTUM_STACK_SIZE 16

#define CRUI_PI2 411775
#define CRUI_PI1 205887

// Real upstream reads raw Arduino millis()/micros() directly - see this
// file's own header comment on why this port instead uses a fixed
// per-tick constant (gbUpdate() throttles to a genuinely fixed rate) and
// a gbFrameCount-derived millisecond clock instead of a measured value.
#define CRUI_MICROS_PER_FRAME 50000
#define CRUI_MPF_SHIFT10 48   // 50000 / 1024, truncated (integer division, matching upstream's own real `micros_per_frame >> 10` idiom applied to this now-constant value)
#define CRUI_MPF_SHIFT5 1562  // 50000 / 32, truncated - used only by the wobble accumulator

#define CRUI_CB_COLLISION 0
#define CRUI_CB_RENDER 1

#define CRUI_STATE_TITLE 0
#define CRUI_STATE_PLAYING 1

// -----------------------------------------------------------------------------
//   Vector math (real vec3d, flattened to free functions - see header comment)
// -----------------------------------------------------------------------------

typedef struct
{
    int x;
    int y;
    int z;
} cruiVec3d;

void cruiVec3dAdd( cruiVec3d* a, cruiVec3d* b, cruiVec3d* out )
{
    cruiVec3d r;
    r.x = a->x + b->x;
    r.y = a->y + b->y;
    r.z = a->z + b->z;
    *out = r;
}

void cruiVec3dAddEq( cruiVec3d* a, cruiVec3d* b )
{
    a->x = a->x + b->x;
    a->y = a->y + b->y;
    a->z = a->z + b->z;
}

void cruiVec3dSub( cruiVec3d* a, cruiVec3d* b, cruiVec3d* out )
{
    cruiVec3d r;
    r.x = a->x - b->x;
    r.y = a->y - b->y;
    r.z = a->z - b->z;
    *out = r;
}

void cruiVec3dSubEq( cruiVec3d* a, cruiVec3d* b )
{
    a->x = a->x - b->x;
    a->y = a->y - b->y;
    a->z = a->z - b->z;
}

// Real upstream operator*(int32_t d): `d >>= 8` (a signed fixed-point
// multiplier that can legitimately be negative - e.g. a dot-product
// projection factor), then each component `>>= 8` too (vec3d components
// are signed 3D coordinates, routinely negative) - both converted from
// `>>` to real `/` (see this file's own header comment on why).
void cruiVec3dScale( cruiVec3d* v, int d, cruiVec3d* out )
{
    int dd = d / 256;
    cruiVec3d r;
    r.x = ( v->x / 256 ) * dd;
    r.y = ( v->y / 256 ) * dd;
    r.z = ( v->z / 256 ) * dd;
    *out = r;
}

int cruiVec3dDot( cruiVec3d* a, cruiVec3d* b )
{
    int result = 0;
    result = result + ( a->x / 256 ) * ( b->x / 256 );
    result = result + ( a->y / 256 ) * ( b->y / 256 );
    result = result + ( a->z / 256 ) * ( b->z / 256 );
    return result;
}

void cruiVec3dCross( cruiVec3d* a, cruiVec3d* b, cruiVec3d* out )
{
    cruiVec3d r;
    r.x = ( a->y / 256 ) * ( b->z / 256 ) - ( a->z / 256 ) * ( b->y / 256 );
    r.y = ( a->z / 256 ) * ( b->x / 256 ) - ( a->x / 256 ) * ( b->z / 256 );
    r.z = ( a->x / 256 ) * ( b->y / 256 ) - ( a->y / 256 ) * ( b->x / 256 );
    *out = r;
}

int cruiLsin( int a );
int cruiLsqrt( int a );

int cruiVec3dLength( cruiVec3d* v )
{
    return cruiLsqrt( cruiVec3dDot( v, v ) );
}

// Real upstream `void normalize()` mutates `this` in place - safe here
// too (each component is computed from the ORIGINAL x/y/z before any of
// the three is overwritten, matching upstream's own real evaluation
// order), but still finished with a single `*v = r;` block assignment
// for consistency with every other vector helper in this file.
void cruiVec3dNormalize( cruiVec3d* v )
{
    int len = cruiVec3dLength( v );
    int l;
    cruiVec3d r;
    // Real, platform-forced guard: a genuinely degenerate zero-length
    // vector (e.g. a zero-length wall normal) divides by zero here on
    // real AVR too, but AVR hardware doesn't hard-trap on it - this
    // dialect's own CPU does (a real, live crash hit during verification).
    // Real upstream never guards this either; leaving `*v` as the zero
    // vector it already is matches the only sane "normalized" result for
    // a vector with no direction to begin with.
    if( len == 0 )
      return;
    l = 16777216 / len; // (1L<<24) / length()
    r.x = ( v->x / 256 ) * l;
    r.y = ( v->y / 256 ) * l;
    r.z = ( v->z / 256 ) * l;
    *v = r;
}

// axis: 0=X, 1=Y, 2=Z. s/c are pre-scaled sin/cos (already divided by 256
// by the caller, matching upstream's own real per-call-site pre-shift).
// In-place, matching upstream's own real statement order exactly (each
// overwritten component is never read again afterward within the same
// call - verified by hand against the real source, see header comment).
void cruiVec3dRotateSC( cruiVec3d* v, int axis, int s, int c )
{
    int temp;
    if( axis == 0 )
    {
        temp = v->y;
        v->y = ( v->y / 256 ) * c + ( v->z / 256 ) * -s;
        v->z = ( temp / 256 ) * s + ( v->z / 256 ) * c;
    }
    else if( axis == 1 )
    {
        temp = v->x;
        v->x = ( v->x / 256 ) * c + ( v->z / 256 ) * s;
        v->z = ( temp / 256 ) * -s + ( v->z / 256 ) * c;
    }
    else
    {
        temp = v->x;
        v->x = ( v->x / 256 ) * c + ( v->y / 256 ) * -s;
        v->y = ( temp / 256 ) * s + ( v->y / 256 ) * c;
    }
}

void cruiVec3dRotate( cruiVec3d* v, int axis, int phi )
{
    int s = cruiLsin( phi ) / 256;
    int c = cruiLsin( phi + 102943 ) / 256; // real upstream lcos(x) macro: lsin(x + 102943)
    cruiVec3dRotateSC( v, axis, s, c );
}

// Real upstream `vec3d_16 divby256()` - folded into plain cruiVec3d, see
// this file's own header comment on why the separate 16-bit type was
// dropped entirely.
void cruiVec3dDivBy256( cruiVec3d* v, cruiVec3d* out )
{
    cruiVec3d r;
    r.x = v->x / 256;
    r.y = v->y / 256;
    r.z = v->z / 256;
    *out = r;
}

// -----------------------------------------------------------------------------
//   Trig / sqrt helpers (real upstream lsin()/lcos()/lsqrt())
// -----------------------------------------------------------------------------

int cruiLsin( int a )
{
    return (int)( sin( (float)a / 65536.0 ) * 65536.0 );
}

int cruiLsqrt( int a )
{
    return (int)( sqrt( (float)a / 65536.0 ) * 65536.0 );
}

// -----------------------------------------------------------------------------
//   Map data (real map.h, ported verbatim - see this file's own header
//   comment for wall_normal_templates[]/wall_normals[] being dropped as
//   genuinely dead data)
// -----------------------------------------------------------------------------

int cruiVertexData[121]= {0x1, 0x3, 0x43, 0x42, 0x41, 0x30, 0x20, 0x10, 0x4, 0x14, 0x10, 0x0, 0x2, 0x13, 0x23, 0xa2, 0xa0, 0x10, 0x1, 0x0, 0x2, 0x12, 0x20, 0x10, 0x2, 0x23, 0x31, 0x10, 0x2, 0x13, 0x32, 0x1, 0x13, 0x32, 0x20, 0x1, 0x2, 0x22, 0x20, 0x30, 0x11, 0x2, 0x3, 0x14, 0x34, 0x50, 0x20, 0x4, 0x44, 0x54, 0x53, 0x41, 0x0, 0x1, 0x11, 0x71, 0x81, 0x80, 0x0, 0x4, 0x44, 0x40, 0x1, 0x21, 0x20, 0x0, 0x2, 0x31, 0x20, 0x1, 0x12, 0x21, 0x10, 0x1, 0x13, 0x20, 0x10, 0x2, 0x11, 0x10, 0x0, 0x1, 0x43, 0x73, 0x70, 0x0, 0x3, 0x33, 0x20, 0x2, 0x3, 0x33, 0x53, 0x83, 0x82, 0x60, 0x10, 0x1, 0x24, 0x64, 0x81, 0x70, 0x0, 0x6, 0x26, 0x23, 0x3, 0x6, 0x17, 0x27, 0x26, 0x20, 0x1, 0x11, 0x10, 0x0, 0x0, 0x3, 0x4, 0x64, 0x60};

int cruiPortalData[38]= {0x50, 0xa, 0xc0, 0x8, 0x40, 0x28, 0x60, 0x20, 0x68, 0x41, 0xa0, 0xc8, 0x8, 0x21, 0x62, 0x10, 0x0, 0x50, 0x13, 0x89, 0x26, 0x69, 0x20, 0x6a, 0xa8, 0x23, 0xa4, 0xee, 0x20, 0x61, 0xb0, 0xa, 0x60, 0xa9, 0x2b, 0x68, 0x20, 0x6c};

int cruiDoorData[4]= {0x60, 0x0, 0x51, 0x31};

typedef struct
{
    int floor_height;
    int ceiling_height;
    int x;
    int y;
    int vertex_count;
    int portal_count;
    int door_count;
    int vertices_offset;
    int normals_offset; // real map data, genuinely unused by any code path - see header comment
    int portals_offset;
    int doors_offset;
} cruiSegment;

cruiSegment cruiSegments[27]= {
//   FH  CH    X    Y  VC PC DC   V   N   P  D
    {16, 20,   0,   7,  8, 2, 1,   0,  0,  0, 0},
    {16, 20,   1,   3,  4, 2, 1,   8,  0,  3, 1},
    {16, 20,   0,   0,  7, 2, 0,  12,  4,  5, 0},
    {16, 20,  10,   0,  4, 2, 0,  19,  9,  3, 0},
    {16, 20,  11,   0,  4, 2, 0,  23, 11,  3, 0},
    {16, 20,  13,   1,  4, 2, 0,  27, 13,  3, 0},
    {16, 20,  14,   3,  4, 2, 0,  31, 11,  7, 0},
    {16, 20,  15,   5,  4, 2, 0,  35,  0,  7, 0},
    {16, 20,  12,   7,  7, 3, 0,  39, 15,  9, 0},
    {16, 20,  15,   7,  6, 3, 0,  46, 20, 12, 0},
    {16, 20,   4,   9,  6, 3, 0,  52, 23, 15, 0},
    {16, 20,  15,  11,  4, 2, 0,  58,  0, 20, 0},
    {16, 20,  20,  10,  4, 2, 0,  62,  0, 22, 0},
    {16, 20,  22,   9,  4, 2, 0,  66, 26,  7, 0},
    {16, 20,  24,   8,  4, 2, 0,  70, 28,  7, 0},
    {16, 20,  25,   6,  4, 2, 0,  74, 30,  7, 0},
    {16, 20,  26,   5,  4, 2, 0,  78,  0,  7, 0},
    {16, 20,  23,   2,  6, 1, 0,  82, 23, 24, 0},
    {16, 20,  13,  15,  8, 3, 1,  88, 32, 25, 2},
    {16, 20,   4,  10,  6, 3, 0,  96, 36, 28, 0},
    {16, 20,   4,  11,  4, 1, 0, 102,  0,  8, 0},
    {16, 20,  10,  11,  6, 2, 0, 106,  6, 32, 0},
    {16, 20,  12,  17,  4, 2, 0, 112,  0, 34, 0},
    {16, 20,  21,  17,  4, 2, 1, 112,  0, 36, 3},
    {16, 20,  22,  17,  4, 2, 0, 112,  0,  7, 0},
    {16, 20,  23,  17,  4, 2, 0, 112,  0,  7, 0},
    {16, 20,  24,  14,  5, 1, 0, 116, 39,  5, 0}
};

int cruiSegmentsTouched[4];
int cruiDoorState[2];

// -----------------------------------------------------------------------------
//   Camera / player state (declared this early because
//   cruiTransformWorldSpaceToViewSpace() and everything defined after it
//   needs cruiCamera already visible - this dialect has no forward
//   declarations for globals used this way, so ordering matters)
// -----------------------------------------------------------------------------

typedef struct
{
    cruiVec3d at;
    cruiVec3d up;
    cruiVec3d forward;
    cruiVec3d right;
    cruiVec3d up8;
    cruiVec3d forward8;
    cruiVec3d right8;
    int yaw;
    int ayaw;
    int pitch;
    int apitch;
    int a;
    int width;  // real upstream field, write-only (set once, never read anywhere) - kept for structural fidelity
    int height; // same as width above
    int current_segment_index;
    cruiSegment current_segment;
    int wobble;
    int wobble_sin;
} cruiCameraT;

cruiCameraT cruiCamera;
bool cruiAllowSteering = true;

// -----------------------------------------------------------------------------
//   Polygon / frustum clipping
// -----------------------------------------------------------------------------

typedef struct
{
    int num_vertices;
    int draw_edges;
    cruiVec3d vertices[8];
} cruiPolygon;

void cruiPolygonSetVertex( cruiPolygon* p, int index, cruiVec3d* v, bool drawEdge )
{
    if( index >= CRUI_MAX_POLYGON_VERTICES )
      return;

    p->vertices[ index ] = *v;
    if( drawEdge )
        p->draw_edges = p->draw_edges | ( 1 << index );
    else
        p->draw_edges = p->draw_edges & ~( 1 << index );
}

cruiPolygon cruiWall;
cruiPolygon cruiPortal;
cruiPolygon cruiLineBuf;
cruiPolygon cruiClippedPolygon;

typedef struct
{
    int x;
    int y;
} cruiFrustumVertex;

#define CRUI_HALF_SCREEN_W_SCALED ( ( CRUI_SCREEN_WIDTH << CRUI_FIXED_POINT_SCALE ) / 2 )
#define CRUI_HALF_SCREEN_H_SCALED ( ( CRUI_SCREEN_HEIGHT << CRUI_FIXED_POINT_SCALE ) / 2 )

void cruiMakeFrustumVertex( int rawX, int rawY, cruiFrustumVertex* out )
{
    cruiFrustumVertex r;
    r.x = rawX - CRUI_HALF_SCREEN_W_SCALED;
    r.y = CRUI_HALF_SCREEN_H_SCALED - rawY;
    *out = r;
}

// Real upstream `to_vec3d(other, target)`, called as `this->to_vec3d(other,
// target)`: builds a vec3d `a` from `this` (self), assigns a vec3d built
// from `other` into `*target`, then `*target = target->cross(a)` - i.e.
// target = cross(other-derived, self-derived). Order preserved exactly.
void cruiFrustumVertexToVec3d( cruiFrustumVertex* self, cruiFrustumVertex* other, cruiVec3d* target )
{
    cruiVec3d a;
    a.x = self->x << 8;
    a.y = self->y << 8;
    a.z = -172032;

    cruiVec3d t;
    t.x = other->x << 8;
    t.y = other->y << 8;
    t.z = -172032;

    cruiVec3dCross( &t, &a, target );
}

typedef struct
{
    int segment;
    int vertex_count;
    cruiFrustumVertex vertices[8];
} cruiFrustumStackEntry;

cruiFrustumStackEntry cruiFrustumStack[16];
int cruiFrustumStackTop;

// Returns a pointer to the entry's own vertex slots to fill in, or a real
// null pointer if the stack is full (matching real push_frustum()'s own
// graceful "give up" return, checked by every real call site) - see this
// file's own header comment on why this is a plain array of whole records
// rather than upstream's own real byte-packed pointer-arithmetic stack.
cruiFrustumVertex* cruiPushFrustum( int segment, int vertexCount )
{
    if( cruiFrustumStackTop >= CRUI_FRUSTUM_STACK_SIZE )
      return (cruiFrustumVertex*)0;

    cruiFrustumStack[ cruiFrustumStackTop ].segment = segment;
    cruiFrustumStack[ cruiFrustumStackTop ].vertex_count = vertexCount;
    cruiFrustumVertex* result = &cruiFrustumStack[ cruiFrustumStackTop ].vertices[0];
    cruiFrustumStackTop = cruiFrustumStackTop + 1;
    return result;
}

cruiFrustumVertex* cruiPopFrustum( int* segmentOut, int* vertexCountOut )
{
    if( cruiFrustumStackTop == 0 )
      return (cruiFrustumVertex*)0;

    cruiFrustumStackTop = cruiFrustumStackTop - 1;
    *segmentOut = cruiFrustumStack[ cruiFrustumStackTop ].segment;
    *vertexCountOut = cruiFrustumStack[ cruiFrustumStackTop ].vertex_count;
    return &cruiFrustumStack[ cruiFrustumStackTop ].vertices[0];
}

cruiVec3d cruiCurrentFrustumNormals[8];
int cruiCurrentFrustumNormalCount;

// Direct port of real clip_polygon_against_plane() (Sutherland-Hodgman,
// in-place-capable). `source == target` is a real, exercised case here
// (render_polygon() clips the same polygon against a second-or-later
// frustum plane in place) - the real in-place vertex-shift logic that
// makes this safe is ported verbatim, including the real pointer
// arithmetic (`v1 = v1 + 1`) into the polygon's own vertex array,
// confirmed directly to compile and behave correctly in this dialect.
void cruiClipPolygonAgainstPlane( cruiPolygon* source, cruiPolygon* target, cruiVec3d* clipPlaneNormal )
{
    int sourceVertexIndex = 0;
    int sourceVertexCount = source->num_vertices;
    target->num_vertices = 0;
    int targetVertexIndex = 0;
    int targetDrawEdges = 0;
    cruiVec3d* v0 = (cruiVec3d*)0;
    cruiVec3d* v1 = (cruiVec3d*)0;
    int d0 = 0;
    int d1 = 0;
    bool flag0 = false;
    bool flag1 = false;
    cruiVec3d intersection;
    cruiVec3d firstVertex = source->vertices[0];
    int sourceDrawEdgeOffset = 0;

    while( sourceVertexIndex < sourceVertexCount )
    {
        if( v1 != (cruiVec3d*)0 )
        {
            v0 = v1;
            d0 = d1;
            flag0 = flag1;
        }
        else
        {
            v0 = &source->vertices[ sourceVertexIndex ];
            d0 = cruiVec3dDot( v0, clipPlaneNormal );
            flag0 = d0 > 0;
        }

        if( sourceVertexIndex == sourceVertexCount - 1 )
            v1 = &firstVertex;
        else
            v1 = &source->vertices[ sourceVertexIndex + 1 ];
        d1 = cruiVec3dDot( v1, clipPlaneNormal );
        flag1 = d1 > 0;

        if( flag0 != flag1 )
        {
            int f = ( -d1 << 8 ) / ( d0 - d1 );
            int f1 = 256 - f;
            intersection.x = ( v0->x / 256 ) * f + ( v1->x / 256 ) * f1;
            intersection.y = ( v0->y / 256 ) * f + ( v1->y / 256 ) * f1;
            intersection.z = ( v0->z / 256 ) * f + ( v1->z / 256 ) * f1;
        }

        if( flag0 )
        {
            target->vertices[ targetVertexIndex ] = *v0;
            if( ( ( source->draw_edges >> ( sourceVertexIndex - sourceDrawEdgeOffset ) ) & 1 ) == 1 )
                targetDrawEdges = targetDrawEdges | ( 1 << targetVertexIndex );
            targetVertexIndex = targetVertexIndex + 1;
            if( targetVertexIndex == CRUI_MAX_POLYGON_VERTICES )
              break;

            if( !flag1 )
            {
                if( source == target )
                {
                    int shiftVertexCount = sourceVertexCount - sourceVertexIndex - 1;
                    if( shiftVertexCount != 0 )
                    {
                        if( sourceVertexCount == CRUI_MAX_POLYGON_VERTICES )
                          break;

                        int i;
                        for( i = shiftVertexCount; i > 0; i = i - 1 )
                            source->vertices[ sourceVertexIndex + i + 1 ] = source->vertices[ sourceVertexIndex + i ];
                        sourceVertexCount = sourceVertexCount + 1;
                        sourceVertexIndex = sourceVertexIndex + 1;
                        v1 = v1 + 1;
                        sourceDrawEdgeOffset = sourceDrawEdgeOffset + 1;
                    }
                }
                target->vertices[ targetVertexIndex ] = intersection;
                targetVertexIndex = targetVertexIndex + 1;
                if( targetVertexIndex == CRUI_MAX_POLYGON_VERTICES )
                  break;
            }
        }
        else if( flag1 )
        {
            target->vertices[ targetVertexIndex ] = intersection;
            if( ( ( source->draw_edges >> ( sourceVertexIndex - sourceDrawEdgeOffset ) ) & 1 ) == 1 )
                targetDrawEdges = targetDrawEdges | ( 1 << targetVertexIndex );
            targetVertexIndex = targetVertexIndex + 1;
            if( targetVertexIndex == CRUI_MAX_POLYGON_VERTICES )
              break;
        }
        sourceVertexIndex = sourceVertexIndex + 1;
    }
    target->num_vertices = targetVertexIndex;
    target->draw_edges = targetDrawEdges;
}

// -----------------------------------------------------------------------------
//   Projection and line drawing
// -----------------------------------------------------------------------------

// Real upstream `project_vertex()`. `z1`'s divide-by-p.z and every
// subsequent `>>` in this formula operate on genuinely signed,
// potentially-negative fixed-point values (see this file's own header
// comment on the >> -> / conversion and the real "no near-plane clip"
// caveat this inherits unmodified from upstream).
void cruiProjectVertex( cruiVec3d* p, int* tv )
{
    int pz = p->z;
    int z1;
    // Real, platform-forced guard: real upstream has no near-plane clip
    // at all (see this file's own header comment) - a vertex sitting
    // exactly on the camera plane (p.z==0) divides by zero here on real
    // AVR too, but AVR hardware doesn't hard-trap on it - this dialect's
    // own CPU does (a real, live crash hit during verification). Clamped
    // to the smallest representable non-zero magnitude rather than
    // skipping the draw entirely, so the point still projects somewhere
    // (off-screen or extreme, matching the same kind of visual glitch
    // real upstream's own missing near-clip would already produce for a
    // vertex very close to zero) instead of crashing outright.
    if( pz == 0 )
      pz = 1;
    z1 = -16777216 / pz; // (-1L<<24) / p.z
    tv[0] = ( ( CRUI_SCREEN_WIDTH << CRUI_FIXED_POINT_SCALE ) + ( ( ( p->x * ( CRUI_SCREEN_WIDTH << CRUI_FIXED_POINT_SCALE ) ) / 256 ) * z1 ) / 65536 ) / 2;
    tv[1] = ( ( CRUI_SCREEN_HEIGHT << CRUI_FIXED_POINT_SCALE ) - ( ( ( p->y * ( CRUI_SCREEN_WIDTH << CRUI_FIXED_POINT_SCALE ) ) / 256 ) * z1 ) / 65536 ) / 2;
}

// Real upstream draw_line_fixed_point() (the SUB_PIXEL_ACCURACY branch -
// the only one this port needs, see this file's own header comment on
// why the PORT_ENABLED-gated fallback macro was dropped as dead code in
// this build). A real Bresenham line rasterizer with 4 fractional bits of
// sub-pixel accuracy, calling gbDrawPixel() once per real pixel step -
// the same "gbDrawPixel() in a Bresenham loop" shape this shim's own
// primitive is designed for, not the per-pixel-multi-primitive-call
// pattern this project's CLAUDE.md warns against.
void cruiDrawLineFixedPoint( int x0In, int y0In, int x1In, int y1In )
{
    int x0 = x0In;
    int y0 = y0In;
    int x1 = x1In;
    int y1 = y1In;

    bool steep = gbAbsInt( y1 - y0 ) > gbAbsInt( x1 - x0 );

    if( steep )
    {
        int t;
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }

    if( x0 > x1 )
    {
        int t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }

    int nx = x1 - x0;
    int ny = y0 - y1;
    int mx = ( ( x0 & ~0xf ) << 1 ) + 0x0f;
    int my = ( y0 & ~0xf ) << 1;
    if( ny > 0 )
        my = my - 1;
    else
        my = my + 0x1f;

    int error = ( mx - ( x0 << 1 ) ) * ny + ( my - ( y0 << 1 ) ) * nx;
    int ddx = ny << 5;
    int ddy = nx << 5;

    int sy = 1;
    if( ny > 0 )
    {
        error = -error;
        ddx = -ddx;
        sy = -1;
    }

    x0 = x0 / 16;
    y0 = y0 / 16;
    x1 = x1 / 16;

    for( ; x0 <= x1; x0 = x0 + 1 )
    {
        if( steep )
            gbDrawPixel( y0, x0 );
        else
            gbDrawPixel( x0, y0 );

        error = error + ddx;
        if( error < 0 )
        {
            error = error + ddy;
            y0 = y0 + sy;
        }
    }
}

// Real upstream render_polygon(). Clips against every currently active
// frustum plane (in place from the 2nd plane onward), then draws each
// real screen-space edge flagged in draw_edges via cruiDrawLineFixedPoint().
cruiPolygon* cruiRenderPolygon( cruiPolygon* p, int minVertexCount )
{
    int k;
    for( k = 0; k < cruiCurrentFrustumNormalCount; k = k + 1 )
    {
        if( k == 0 )
            cruiClipPolygonAgainstPlane( p, &cruiClippedPolygon, &cruiCurrentFrustumNormals[k] );
        else
            cruiClipPolygonAgainstPlane( &cruiClippedPolygon, &cruiClippedPolygon, &cruiCurrentFrustumNormals[k] );
        if( cruiClippedPolygon.num_vertices < minVertexCount )
          break;
    }
    p = &cruiClippedPolygon;

    if( p->num_vertices < minVertexCount )
      return (cruiPolygon*)0;

    int first[2];
    int last[2];

    for( k = 0; k < p->num_vertices; k = k + 1 )
    {
        int tv[2];
        cruiProjectVertex( &p->vertices[k], tv );

        if( k == 0 )
        {
            first[0] = tv[0];
            first[1] = tv[1];
        }
        else
        {
            if( ( ( p->draw_edges >> ( k - 1 ) ) & 1 ) == 1 )
                cruiDrawLineFixedPoint( last[0], last[1], tv[0], tv[1] );
        }
        last[0] = tv[0];
        last[1] = tv[1];
    }
    if( ( ( p->draw_edges >> ( p->num_vertices - 1 ) ) & 1 ) == 1 )
        cruiDrawLineFixedPoint( last[0], last[1], first[0], first[1] );

    return p;
}

// Real upstream `vec3d::translate7()` - computes 2 vertices of `line` from
// `self` plus weighted `dx`/`dy` offsets, THEN immediately renders it (a
// real, deliberate "compute and draw" combo upstream itself relies on for
// its own door-open animation's 4 real translate7-then-render calls) -
// ported as a free function taking `self` explicitly rather than an
// implicit `this`, matching every other flattened vec3d method here.
// `line->num_vertices`/`draw_edges` are NOT touched here, matching real
// upstream exactly (the caller sets them once, before the first call).
void cruiVec3dTranslate7( cruiVec3d* self, cruiPolygon* line, cruiVec3d* dx, cruiVec3d* dy, int sx0, int sy0, int sx1, int sy1 )
{
    line->vertices[0].x = self->x + ( dx->x * sx0 ) / 128 + ( dy->x * sy0 ) / 128;
    line->vertices[0].y = self->y + ( dx->y * sx0 ) / 128 + ( dy->y * sy0 ) / 128;
    line->vertices[0].z = self->z + ( dx->z * sx0 ) / 128 + ( dy->z * sy0 ) / 128;
    line->vertices[1].x = self->x + ( dx->x * sx1 ) / 128 + ( dy->x * sy1 ) / 128;
    line->vertices[1].y = self->y + ( dx->y * sx1 ) / 128 + ( dy->y * sy1 ) / 128;
    line->vertices[1].z = self->z + ( dx->z * sx1 ) / 128 + ( dy->z * sy1 ) / 128;
    cruiRenderPolygon( line, 2 );
}

// Real upstream transform_world_space_to_view_space(). WOBBLE_SHIP is
// real and active in this build (ENABLE_MAP is not, so upstream's own
// `if (!map_mode)` guard around the wobble-add is always true here).
void cruiTransformWorldSpaceToViewSpace( cruiVec3d* v, int count )
{
    int i;
    for( i = 0; i < count; i = i + 1 )
    {
        cruiVec3d* r = &v[i];
        int sx = ( r->x - cruiCamera.at.x ) / 256;
        int sy = ( r->y - cruiCamera.at.y ) / 256;
        int sz = ( r->z - cruiCamera.at.z ) / 256;

        int rx = sx * cruiCamera.right8.x + sy * cruiCamera.right8.y + sz * cruiCamera.right8.z;
        int ry = sx * cruiCamera.up8.x + sy * cruiCamera.up8.y + sz * cruiCamera.up8.z;
        int rz = -sx * cruiCamera.forward8.x - sy * cruiCamera.forward8.y - sz * cruiCamera.forward8.z;

        ry = ry + ( cruiCamera.wobble_sin * 13 ) / 256;

        r->x = rx;
        r->y = ry;
        r->z = rz;
    }
}

// -----------------------------------------------------------------------------
//   Camera / player state (struct + globals declared earlier, right after
//   the map data section, since cruiTransformWorldSpaceToViewSpace() and
//   everything after it needs cruiCamera already visible)
// -----------------------------------------------------------------------------

typedef struct
{
    int x;
    int y;
    int z;
    int dx;
    int dy;
    int dz;
    int current_segment;
} cruiShot;

cruiShot cruiShots[12];
int cruiNumShots;

cruiSegment cruiTempSegmentBuffer;

int cruiLastShotFrameMillis = -10000;

int cruiMillis()
{
    return gbFrameCount * 50;
}

void cruiSetCurrentSegment( int segmentIndex )
{
    cruiCamera.current_segment_index = segmentIndex;
    cruiCamera.current_segment = cruiSegments[ segmentIndex ];
}

// Real upstream title_screen() as it exists in the REAL, actually-compiled
// build (SHOW_TITLE_SCREEN disabled - see header comment): a real state
// RESET, not a UI screen. Called once at startup and again as a genuine
// mid-game "respawn" on every Button C press.
void cruiResetGame()
{
    cruiCamera.yaw = 0;
    cruiCamera.ayaw = 0;
    cruiCamera.pitch = 0;
    cruiCamera.apitch = 0;
    cruiCamera.a = 0;
    cruiCamera.at.x = 98304;  // 1.5 * 65536
    cruiCamera.at.y = 294912; // 4.5 * 65536
    cruiCamera.at.z = 638976; // 9.75 * 65536
    cruiSetCurrentSegment( 0 );
    cruiNumShots = 0;
    cruiCamera.wobble = 0;

    int i;
    for( i = 0; i < CRUI_DOOR_COUNT; i = i + 1 )
        cruiDoorState[i] = -1;
}

// -----------------------------------------------------------------------------
//   Wall/portal/door iteration (real loop_through_segment_walls() -
//   flattened to an int-mode dispatch, see header comment)
// -----------------------------------------------------------------------------

typedef struct
{
    int wall_index;
    int x0;
    int z0;
    int x1;
    int z1;
    int adjacent_segment_index;
    int adjacent_floor_height;
    int adjacent_ceiling_height;
    int door_index;
    int door_time;
    bool door_is_open;
    bool also_drew_previous_wall;
} cruiWallLoopInfo;

cruiWallLoopInfo cruiWallInfo;

bool cruiCollisionDetectionCallback( cruiWallLoopInfo* wallInfo );
bool cruiRenderSegmentCallback( cruiWallLoopInfo* wallInfo );

void cruiLoopThroughSegmentWalls( int segmentIndex, cruiSegment* segment, bool earlyAdjacentSegmentCulling, int callbackMode )
{
    // Real upstream pointer arithmetic into the shared portal/door byte
    // tables (`portals + segment->portals`) - ported as `&arr[index]`
    // rather than `arr + index`, since this compiler does not accept a
    // bare array name as the left operand of pointer arithmetic (confirmed
    // directly via an isolated probe: `arr + offset` fails to compile even
    // though `&arr[offset]` produces an equivalent, further-incrementable
    // pointer that behaves identically from here on).
    int* nextPortalPointer = &cruiPortalData[ segment->portals_offset ];
    int nextPortalPoint = *nextPortalPointer;
    int remainingPortals = segment->portal_count;

    int* nextDoorPointer = &cruiDoorData[ segment->doors_offset ];
    int nextDoor = *nextDoorPointer;
    int remainingDoors = segment->door_count;

    int x1z1FromPoint = -1;
    int tempByte;

    int i;
    for( i = 0; i < segment->vertex_count; i = i + 1 )
    {
        cruiWallInfo.wall_index = i;
        cruiWallInfo.adjacent_segment_index = -1;

        // test if it's a portal (nextPortalPoint is a real encoded 0-255
        // map-data byte - always non-negative, so `>>`/`&` here are real
        // bit-unpacking, not signed division - see header comment)
        if( remainingPortals != 0 && i == ( nextPortalPoint >> 5 ) )
        {
            remainingPortals = remainingPortals - 1;
            if( ( nextPortalPoint & 0x10 ) != 0 )
            {
                cruiWallInfo.adjacent_segment_index = *( nextPortalPointer + 1 );
                nextPortalPointer = nextPortalPointer + 2;
            }
            else
            {
                int diff = ( nextPortalPoint & 7 ) + 1;
                if( ( nextPortalPoint & 8 ) != 0 )
                    diff = -diff;
                cruiWallInfo.adjacent_segment_index = segmentIndex + diff;
                nextPortalPointer = nextPortalPointer + 1;
            }
            nextPortalPoint = *nextPortalPointer;

            if( earlyAdjacentSegmentCulling )
            {
                if( ( ( cruiSegmentsTouched[ cruiWallInfo.adjacent_segment_index >> 3 ] >> ( cruiWallInfo.adjacent_segment_index & 7 ) ) & 1 ) == 1 )
                    continue;
            }

            // Real upstream's own PORT_ENABLED branch reads the adjacent
            // segment's floor/ceiling height via a full memcpy_P of the
            // whole segment struct; its real non-PORT_ENABLED branch
            // (the one actually active here) instead reads just the
            // first packed word to avoid a full-struct AVR PROGMEM read -
            // an AVR-specific micro-optimization with no bitfield left to
            // extract from once bitfields were dropped (see header
            // comment) and no benefit on this platform anyway - ported as
            // a plain, direct field read of the same two real values.
            cruiWallInfo.adjacent_floor_height = cruiSegments[ cruiWallInfo.adjacent_segment_index ].floor_height;
            cruiWallInfo.adjacent_ceiling_height = cruiSegments[ cruiWallInfo.adjacent_segment_index ].ceiling_height;
        }

        cruiWallInfo.door_index = -1;
        cruiWallInfo.door_is_open = false;
        cruiWallInfo.door_time = 0;

        if( remainingDoors != 0 && i == ( nextDoor >> 4 ) )
        {
            remainingDoors = remainingDoors - 1;
            cruiWallInfo.door_index = nextDoor & 0xf;
            nextDoorPointer = nextDoorPointer + 1;
            nextDoor = *nextDoorPointer;
            if( cruiDoorState[ cruiWallInfo.door_index ] >= 0 )
            {
                int doorTime = cruiMillis() - cruiDoorState[ cruiWallInfo.door_index ];
                if( doorTime > 4000 )
                    doorTime = 4000;
                cruiWallInfo.door_time = doorTime;
                if( doorTime > 500 && doorTime < 3500 )
                    cruiWallInfo.door_is_open = true;
            }
        }

        int thisVertexIndex = ( i + 1 ) % segment->vertex_count + segment->vertices_offset;
        cruiWallInfo.also_drew_previous_wall = ( x1z1FromPoint == thisVertexIndex );
        if( cruiWallInfo.also_drew_previous_wall )
        {
            cruiWallInfo.x0 = cruiWallInfo.x1;
            cruiWallInfo.z0 = cruiWallInfo.z1;
        }
        else
        {
            tempByte = cruiVertexData[ i % segment->vertex_count + segment->vertices_offset ];
            cruiWallInfo.x0 = segment->x + ( tempByte >> 4 );
            cruiWallInfo.z0 = segment->y + ( tempByte & 0xf );
        }

        x1z1FromPoint = thisVertexIndex;
        tempByte = cruiVertexData[ x1z1FromPoint ];
        cruiWallInfo.x1 = segment->x + ( tempByte >> 4 );
        cruiWallInfo.z1 = segment->y + ( tempByte & 0xf );

        bool keepGoing;
        if( callbackMode == CRUI_CB_COLLISION )
            keepGoing = cruiCollisionDetectionCallback( &cruiWallInfo );
        else
            keepGoing = cruiRenderSegmentCallback( &cruiWallInfo );

        if( !keepGoing )
          break;
    }
}

// -----------------------------------------------------------------------------
//   Collision detection (real collision_detection_callback()/
//   collision_detection())
// -----------------------------------------------------------------------------

cruiVec3d* cruiCollFrom;
cruiVec3d* cruiCollTo;
cruiVec3d cruiCollDir;
int cruiCollBumpDistance;
int cruiCollCollided;
int cruiCollNewSegmentIndex;
int cruiCollHitDoorIndex;

bool cruiCollisionDetectionCallback( cruiWallLoopInfo* wallInfo )
{
    cruiVec3d n;
    n.x = ( wallInfo->z0 - wallInfo->z1 ) << 16;
    n.y = 0;
    n.z = ( wallInfo->x1 - wallInfo->x0 ) << 16;

    // real "are we facing this wall" test - a genuine, correct direction-
    // aware check inherited from upstream (see header comment)
    if( cruiVec3dDot( &n, &cruiCollDir ) > 0 )
    {
        cruiVec3d wallP;
        wallP.x = wallInfo->x0 << 16;
        wallP.y = 0;
        wallP.z = wallInfo->z0 << 16;

        bool isWall = false;
        if( wallInfo->adjacent_segment_index == -1 )
            isWall = true;
        else if( wallInfo->door_index >= 0 && !wallInfo->door_is_open )
            isWall = true;

        if( isWall )
        {
            cruiVec3dNormalize( &n );
            cruiVec3d bumpOffset;
            cruiVec3dScale( &n, cruiCollBumpDistance, &bumpOffset );
            cruiVec3dSubEq( &wallP, &bumpOffset );
            cruiVec3dSubEq( &wallP, cruiCollTo );
            int f = cruiVec3dDot( &wallP, &n );
            if( f < 0 )
            {
                if( wallInfo->door_index >= 0 )
                    cruiCollHitDoorIndex = wallInfo->door_index;

                cruiVec3d nf;
                cruiVec3dScale( &n, f, &nf );
                cruiVec3d temp;
                cruiVec3dAdd( cruiCollTo, &nf, &temp );

                cruiVec3d edgeA;
                edgeA.x = ( wallInfo->x1 - wallInfo->x0 ) << 16;
                edgeA.y = 0;
                edgeA.z = ( wallInfo->z1 - wallInfo->z0 ) << 16;
                cruiVec3d fromA;
                fromA.x = wallInfo->x0 << 16;
                fromA.y = 0;
                fromA.z = wallInfo->z0 << 16;
                cruiVec3d diffA;
                cruiVec3dSub( &temp, &fromA, &diffA );

                cruiVec3d edgeB;
                edgeB.x = ( wallInfo->x0 - wallInfo->x1 ) << 16;
                edgeB.y = 0;
                edgeB.z = ( wallInfo->z0 - wallInfo->z1 ) << 16;
                cruiVec3d fromB;
                fromB.x = wallInfo->x1 << 16;
                fromB.y = 0;
                fromB.z = wallInfo->z1 << 16;
                cruiVec3d diffB;
                cruiVec3dSub( &temp, &fromB, &diffB );

                if( cruiVec3dDot( &diffA, &edgeA ) > 0 && cruiVec3dDot( &diffB, &edgeB ) > 0 )
                {
                    *cruiCollTo = temp;
                    cruiVec3dSub( cruiCollTo, cruiCollFrom, &cruiCollDir );
                    cruiCollCollided = wallInfo->wall_index;
                }
            }
        }
        else
        {
            cruiVec3dSubEq( &wallP, cruiCollTo );
            int f = cruiVec3dDot( &wallP, &n );
            if( f < 0 )
            {
                if( wallInfo->adjacent_segment_index != -1 )
                {
                    cruiCollNewSegmentIndex = wallInfo->adjacent_segment_index;
                    return false;
                }
            }
        }
    }
    return true;
}

// Returns 255 (no collision), 253 (floor), 254 (ceiling), or a real wall
// index (0xfd/0xfe/0xff in real upstream).
int cruiCollisionDetection( int currentSegmentIndex, cruiSegment* currentSegment, int* newSegmentIndexOut, int* hitDoorIndexOut, cruiVec3d* from, cruiVec3d* to, int bumpDistance )
{
    cruiCollFrom = from;
    cruiCollTo = to;
    cruiVec3dSub( to, from, &cruiCollDir );
    cruiCollBumpDistance = bumpDistance;
    cruiCollCollided = 255;
    cruiCollNewSegmentIndex = currentSegmentIndex;
    cruiCollHitDoorIndex = -1;

    cruiVec3d n;
    n.x = 0;
    n.y = -65536;
    n.z = 0;
    int y = currentSegment->floor_height << 14;
    int collisionCode = 253;
    if( cruiCollDir.y > 0 )
    {
        n.y = 65536;
        y = currentSegment->ceiling_height << 14;
        collisionCode = 254;
    }

    cruiVec3d p;
    p.x = 0;
    p.y = y - ( ( n.y * 26 ) / 256 );
    p.z = 0;
    cruiVec3dSubEq( &p, to );
    int f = cruiVec3dDot( &p, &n );
    if( f < 0 )
    {
        cruiVec3d nf;
        cruiVec3dScale( &n, f, &nf );
        cruiVec3dAddEq( to, &nf );
        cruiVec3dSub( to, from, &cruiCollDir );
        cruiCollCollided = collisionCode;
    }

    cruiLoopThroughSegmentWalls( currentSegmentIndex, currentSegment, false, CRUI_CB_COLLISION );

    *newSegmentIndexOut = cruiCollNewSegmentIndex;
    *hitDoorIndexOut = cruiCollHitDoorIndex;
    return cruiCollCollided;
}

// -----------------------------------------------------------------------------
//   Player movement (real move_player())
// -----------------------------------------------------------------------------

void cruiMovePlayer()
{
    if( cruiCamera.ayaw < 0 )
    {
        cruiCamera.yaw = cruiCamera.yaw + CRUI_PI2;
        cruiCamera.yaw = cruiCamera.yaw - ( ( -cruiCamera.ayaw ) * CRUI_MPF_SHIFT10 ) / 1024;
        cruiCamera.ayaw = -( ( ( -cruiCamera.ayaw ) * 205 ) / 256 );
    }
    else
    {
        cruiCamera.yaw = cruiCamera.yaw + ( cruiCamera.ayaw * CRUI_MPF_SHIFT10 ) / 1024;
        cruiCamera.ayaw = ( cruiCamera.ayaw * 205 ) / 256;
    }
    cruiCamera.yaw = cruiCamera.yaw % CRUI_PI2;

    if( cruiCamera.apitch < 0 )
    {
        cruiCamera.pitch = cruiCamera.pitch + CRUI_PI2;
        cruiCamera.pitch = cruiCamera.pitch - ( ( -cruiCamera.apitch ) * CRUI_MPF_SHIFT10 ) / 1024;
        cruiCamera.apitch = -( ( ( -cruiCamera.apitch ) * 205 ) / 256 );
    }
    else
    {
        cruiCamera.pitch = cruiCamera.pitch + ( cruiCamera.apitch * CRUI_MPF_SHIFT10 ) / 1024;
        cruiCamera.apitch = ( cruiCamera.apitch * 205 ) / 256;
    }
    cruiCamera.pitch = cruiCamera.pitch % CRUI_PI2;

    // auto-leveling
    if( cruiCamera.pitch < CRUI_PI1 )
        cruiCamera.pitch = ( cruiCamera.pitch * 243 ) / 256;
    else
        cruiCamera.pitch = CRUI_PI2 - ( ( ( CRUI_PI2 - cruiCamera.pitch ) * 243 ) / 256 );

    cruiCamera.up.x = 0;
    cruiCamera.up.y = 65536;
    cruiCamera.up.z = 0;
    cruiCamera.forward.x = 0;
    cruiCamera.forward.y = 0;
    cruiCamera.forward.z = -65536;

    int s;
    int c;

    // roll (ROLL_SHIP active)
    s = cruiLsin( cruiCamera.ayaw / 16 ) / 256;
    c = cruiLsin( cruiCamera.ayaw / 16 + 102943 ) / 256;
    cruiVec3dRotateSC( &cruiCamera.up, 2, s, c );

    // pitch
    s = cruiLsin( cruiCamera.pitch ) / 256;
    c = cruiLsin( cruiCamera.pitch + 102943 ) / 256;
    cruiVec3dRotateSC( &cruiCamera.up, 0, s, c );
    cruiVec3dRotateSC( &cruiCamera.forward, 0, s, c );

    // yaw
    s = cruiLsin( cruiCamera.yaw ) / 256;
    c = cruiLsin( cruiCamera.yaw + 102943 ) / 256;
    cruiVec3dRotateSC( &cruiCamera.up, 1, s, c );
    cruiVec3dRotateSC( &cruiCamera.forward, 1, s, c );

    cruiVec3dCross( &cruiCamera.forward, &cruiCamera.up, &cruiCamera.right );

    cruiVec3dDivBy256( &cruiCamera.up, &cruiCamera.up8 );
    cruiVec3dDivBy256( &cruiCamera.forward, &cruiCamera.forward8 );
    cruiVec3dDivBy256( &cruiCamera.right, &cruiCamera.right8 );

    cruiVec3d thrustDelta;
    cruiVec3dScale( &cruiCamera.forward, ( cruiCamera.a * CRUI_MPF_SHIFT10 ) / 1024, &thrustDelta );
    cruiVec3d newAt;
    cruiVec3dAdd( &cruiCamera.at, &thrustDelta, &newAt );

    // COLLISION_DETECTION active
    if( cruiCamera.a != 0 )
    {
        int newSegmentIndex;
        int hitDoorIndex;
        cruiCollisionDetection( cruiCamera.current_segment_index, &cruiCamera.current_segment, &newSegmentIndex, &hitDoorIndex, &cruiCamera.at, &newAt, 16384 );
        if( newSegmentIndex != cruiCamera.current_segment_index )
            cruiSetCurrentSegment( newSegmentIndex );
        if( hitDoorIndex >= 0 )
        {
            int doorTime = cruiMillis() - cruiDoorState[ hitDoorIndex ];
            if( doorTime > 3500 || cruiDoorState[ hitDoorIndex ] < 0 )
                cruiDoorState[ hitDoorIndex ] = cruiMillis();
        }
    }

    cruiCamera.at = newAt;
    cruiCamera.a = ( cruiCamera.a * 230 ) / 256;

    cruiCamera.wobble = ( cruiCamera.wobble + CRUI_MPF_SHIFT5 ) % 65536;
    cruiCamera.wobble_sin = cruiLsin( ( cruiCamera.wobble / 256 ) * ( CRUI_PI2 / 256 ) );

    // ENABLE_SHOOTING active - move shots
    int i;
    for( i = 0; i < cruiNumShots; i = i + 1 )
    {
        cruiVec3d p;
        p.x = cruiShots[i].x << 8;
        p.y = cruiShots[i].y << 8;
        p.z = cruiShots[i].z << 8;

        cruiVec3d shotNewAt;
        shotNewAt.x = p.x + ( ( cruiShots[i].dx * CRUI_MICROS_PER_FRAME ) << 7 ) / 50000;
        shotNewAt.y = p.y + ( ( cruiShots[i].dy * CRUI_MICROS_PER_FRAME ) << 7 ) / 50000;
        shotNewAt.z = p.z + ( ( cruiShots[i].dz * CRUI_MICROS_PER_FRAME ) << 7 ) / 50000;

        int newSegmentIndex;
        cruiSegment* segment = &cruiCamera.current_segment;
        if( cruiShots[i].current_segment != cruiCamera.current_segment_index )
        {
            cruiTempSegmentBuffer = cruiSegments[ cruiShots[i].current_segment ];
            segment = &cruiTempSegmentBuffer;
        }
        int hitDoorIndex;
        int wallCollided = cruiCollisionDetection( cruiShots[i].current_segment, segment, &newSegmentIndex, &hitDoorIndex, &p, &shotNewAt, 16384 );
        bool shotStoppedByDoor = false;
        if( hitDoorIndex >= 0 )
        {
            int doorTime = cruiMillis() - cruiDoorState[ hitDoorIndex ];
            shotStoppedByDoor = true;
            if( doorTime > 500 && doorTime < 3500 )
                shotStoppedByDoor = false;
            if( doorTime > 3500 || cruiDoorState[ hitDoorIndex ] < 0 )
                cruiDoorState[ hitDoorIndex ] = cruiMillis();
        }

        if( shotStoppedByDoor || wallCollided != 255 )
        {
            if( cruiNumShots > 1 )
                cruiShots[i] = cruiShots[ cruiNumShots - 1 ];
            cruiNumShots = cruiNumShots - 1;
            i = i - 1;
        }
        else
        {
            cruiShots[i].current_segment = newSegmentIndex;
            cruiShots[i].x = shotNewAt.x / 256;
            cruiShots[i].y = shotNewAt.y / 256;
            cruiShots[i].z = shotNewAt.z / 256;
        }
    }
}

// -----------------------------------------------------------------------------
//   Controls (real handle_controls())
// -----------------------------------------------------------------------------

void cruiHandleControls()
{
    bool aPressed = gbRepeat( BTN_A, 1 );
    bool bPressed = gbRepeat( BTN_B, 1 );

    if( gbPressed( BTN_C ) )
        cruiResetGame();

    if( bPressed )
    {
        // ENABLE_SHOOTING active - fire a pair of shots
        if( cruiMillis() - cruiLastShotFrameMillis > 250 )
        {
            cruiLastShotFrameMillis = cruiMillis();
            int i;
            for( i = 0; i < 2; i = i + 1 )
            {
                if( cruiNumShots < CRUI_MAX_SHOTS )
                {
                    int px = cruiCamera.at.x / 256;
                    int py = cruiCamera.at.y / 256;
                    int pz = cruiCamera.at.z / 256;

                    px = px - ( ( cruiCamera.up.x * 3277 ) / 16777216 );
                    py = py - ( ( cruiCamera.up.y * 3277 ) / 16777216 );
                    pz = pz - ( ( cruiCamera.up.z * 3277 ) / 16777216 );

                    if( i == 0 )
                    {
                        px = px + ( ( cruiCamera.right.x * 3277 ) / 16777216 );
                        py = py + ( ( cruiCamera.right.y * 3277 ) / 16777216 );
                        pz = pz + ( ( cruiCamera.right.z * 3277 ) / 16777216 );
                    }
                    else
                    {
                        px = px - ( ( cruiCamera.right.x * 3277 ) / 16777216 );
                        py = py - ( ( cruiCamera.right.y * 3277 ) / 16777216 );
                        pz = pz - ( ( cruiCamera.right.z * 3277 ) / 16777216 );
                    }

                    cruiShots[ cruiNumShots ].x = px;
                    cruiShots[ cruiNumShots ].y = py;
                    cruiShots[ cruiNumShots ].z = pz;
                    cruiShots[ cruiNumShots ].dx = cruiCamera.forward.x / 512;
                    cruiShots[ cruiNumShots ].dy = cruiCamera.forward.y / 512;
                    cruiShots[ cruiNumShots ].dz = cruiCamera.forward.z / 512;
                    cruiShots[ cruiNumShots ].current_segment = cruiCamera.current_segment_index;
                    cruiNumShots = cruiNumShots + 1;
                }
            }
        }
    }

    if( gbReleased( BTN_A ) )
        cruiAllowSteering = true;

    if( cruiAllowSteering )
    {
        if( gbRepeat( BTN_LEFT, 1 ) )
            cruiCamera.ayaw = 80000;
        if( gbRepeat( BTN_RIGHT, 1 ) )
            cruiCamera.ayaw = -80000;
        if( gbRepeat( BTN_DOWN, 1 ) )
            cruiCamera.apitch = 80000;
        if( gbRepeat( BTN_UP, 1 ) )
            cruiCamera.apitch = -80000;
        if( aPressed )
            cruiCamera.a = 200000;
    }
}

// -----------------------------------------------------------------------------
//   Segment rendering (real render_segment_callback()/render_segment())
// -----------------------------------------------------------------------------

int cruiRenderSegmentIndex;
cruiSegment cruiRenderCurrentSegment;
cruiVec3d cruiP0;
cruiVec3d cruiP1;

bool cruiRenderSegmentCallback( cruiWallLoopInfo* wallInfo )
{
    cruiP0.x = wallInfo->x0 << 16;
    cruiP0.y = cruiRenderCurrentSegment.floor_height << 14;
    cruiP0.z = wallInfo->z0 << 16;

    cruiP1.x = wallInfo->x1 << 16;
    cruiP1.y = cruiRenderCurrentSegment.floor_height << 14;
    cruiP1.z = wallInfo->z1 << 16;

    cruiWall.num_vertices = 4;
    bool wallOrDoor = false;
    if( wallInfo->adjacent_segment_index < 0 )
        wallOrDoor = true;
    else if( wallInfo->door_index >= 0 )
        wallOrDoor = true;

    if( wallInfo->also_drew_previous_wall )
    {
        cruiPolygonSetVertex( &cruiWall, 0, &cruiP1, wallOrDoor );
        cruiPolygonSetVertex( &cruiWall, 3, &cruiP1, false );
    }
    else
    {
        cruiPolygonSetVertex( &cruiWall, 0, &cruiP1, wallOrDoor );
        cruiPolygonSetVertex( &cruiWall, 1, &cruiP0, true );
        cruiPolygonSetVertex( &cruiWall, 2, &cruiP0, wallOrDoor );
        cruiPolygonSetVertex( &cruiWall, 3, &cruiP1, false );
    }
    cruiWall.vertices[2].y = cruiRenderCurrentSegment.ceiling_height << 14;
    cruiWall.vertices[3].y = cruiRenderCurrentSegment.ceiling_height << 14;

    if( wallInfo->adjacent_segment_index >= 0 )
    {
        if( wallInfo->adjacent_floor_height != cruiRenderCurrentSegment.floor_height )
            cruiWall.draw_edges = cruiWall.draw_edges | 1;
        if( wallInfo->adjacent_ceiling_height != cruiRenderCurrentSegment.ceiling_height )
            cruiWall.draw_edges = cruiWall.draw_edges | 4;
    }

    if( wallInfo->also_drew_previous_wall )
    {
        cruiTransformWorldSpaceToViewSpace( &cruiWall.vertices[0], 1 );
        cruiTransformWorldSpaceToViewSpace( &cruiWall.vertices[3], 1 );
    }
    else
        cruiTransformWorldSpaceToViewSpace( cruiWall.vertices, 4 );

    cruiPolygon* clippedPortal = cruiRenderPolygon( &cruiWall, 3 );
    if( clippedPortal == (cruiPolygon*)0 )
      return true;

    if( wallInfo->door_index >= 0 )
    {
        cruiVec3d dx;
        cruiVec3dSub( &cruiWall.vertices[1], &cruiWall.vertices[0], &dx );
        cruiVec3d dy;
        cruiVec3dSub( &cruiWall.vertices[3], &cruiWall.vertices[0], &dy );
        cruiLineBuf.num_vertices = 2;
        cruiLineBuf.draw_edges = 1;

        cruiVec3d* w = &cruiWall.vertices[0];
        if( wallInfo->door_time > 0 && wallInfo->door_time < 4000 )
        {
            int dt = 128;
            if( wallInfo->door_time < 500 )
                dt = wallInfo->door_time * 128 / 500;
            else if( wallInfo->door_time >= 3500 )
                dt = ( 4000 - wallInfo->door_time ) * 128 / 500;

            int t39 = 39 * dt / 128;
            int t40 = 40 * dt / 128;
            int t51 = 51 * dt / 128;
            int t52 = 52 * dt / 128;
            int t59 = 59 * dt / 128;
            int t60 = 60 * dt / 128;

            clippedPortal = &cruiPortal;
            clippedPortal->num_vertices = 4;
            clippedPortal->draw_edges = 0;

            cruiVec3dTranslate7( w, &cruiLineBuf, &dx, &dy, 0, 51 - t51, 128, 76 - t51 );
            cruiVec3dTranslate7( w, &cruiLineBuf, &dx, &dy, 64 + t59, 64 - t40, 64 + t39, 64 + t59 );
            clippedPortal->vertices[0] = cruiLineBuf.vertices[0];
            clippedPortal->vertices[1] = cruiLineBuf.vertices[1];

            cruiVec3dTranslate7( w, &cruiLineBuf, &dx, &dy, 128, 76 + t52, 0, 51 + t51 );
            cruiVec3dTranslate7( w, &cruiLineBuf, &dx, &dy, 64 - t60, 64 + t39, 64 - t40, 64 - t60 );
            clippedPortal->vertices[2] = cruiLineBuf.vertices[0];
            clippedPortal->vertices[3] = cruiLineBuf.vertices[1];

            clippedPortal = cruiRenderPolygon( clippedPortal, 3 );
        }
        else
        {
            cruiVec3dTranslate7( w, &cruiLineBuf, &dx, &dy, 0, 51, 128, 76 );
            clippedPortal = (cruiPolygon*)0;
        }
    }

    if( wallInfo->adjacent_segment_index >= 0 )
    {
        if( wallInfo->adjacent_floor_height > cruiRenderCurrentSegment.floor_height || wallInfo->adjacent_ceiling_height < cruiRenderCurrentSegment.ceiling_height )
        {
            clippedPortal = &cruiPortal;
            clippedPortal->num_vertices = 4;
            cruiPolygonSetVertex( clippedPortal, 0, &cruiP1, false );
            cruiPolygonSetVertex( clippedPortal, 1, &cruiP0, false );
            cruiPolygonSetVertex( clippedPortal, 2, &cruiP0, false );
            cruiPolygonSetVertex( clippedPortal, 3, &cruiP1, false );
            clippedPortal->draw_edges = 0;

            if( wallInfo->adjacent_floor_height > cruiRenderCurrentSegment.floor_height )
            {
                clippedPortal->vertices[0].y = wallInfo->adjacent_floor_height << 14;
                clippedPortal->vertices[1].y = wallInfo->adjacent_floor_height << 14;
                clippedPortal->draw_edges = clippedPortal->draw_edges | 1;
            }
            else
            {
                clippedPortal->vertices[0].y = cruiRenderCurrentSegment.floor_height << 14;
                clippedPortal->vertices[1].y = cruiRenderCurrentSegment.floor_height << 14;
            }
            if( wallInfo->adjacent_ceiling_height < cruiRenderCurrentSegment.ceiling_height )
            {
                clippedPortal->vertices[2].y = wallInfo->adjacent_ceiling_height << 14;
                clippedPortal->vertices[3].y = wallInfo->adjacent_ceiling_height << 14;
                clippedPortal->draw_edges = clippedPortal->draw_edges | 4;
            }
            else
            {
                clippedPortal->vertices[2].y = cruiRenderCurrentSegment.ceiling_height << 14;
                clippedPortal->vertices[3].y = cruiRenderCurrentSegment.ceiling_height << 14;
            }

            cruiTransformWorldSpaceToViewSpace( clippedPortal->vertices, 4 );
            clippedPortal = cruiRenderPolygon( clippedPortal, 3 );
        }

        if( clippedPortal != (cruiPolygon*)0 )
        {
            if( ( ( cruiSegmentsTouched[ wallInfo->adjacent_segment_index >> 3 ] >> ( wallInfo->adjacent_segment_index & 7 ) ) & 1 ) == 0 )
            {
                cruiFrustumVertex* p = cruiPushFrustum( wallInfo->adjacent_segment_index, clippedPortal->num_vertices );
                if( p != (cruiFrustumVertex*)0 )
                {
                    int k;
                    for( k = 0; k < clippedPortal->num_vertices; k = k + 1 )
                    {
                        int tv[2];
                        cruiProjectVertex( &clippedPortal->vertices[k], tv );
                        cruiMakeFrustumVertex( tv[0], tv[1], &p[k] );
                    }
                }
            }
        }
    }

    return true;
}

void cruiRenderSegment( int segmentIndex, int frustumCount, cruiFrustumVertex* frustumVertices )
{
    cruiSegmentsTouched[ segmentIndex >> 3 ] = cruiSegmentsTouched[ segmentIndex >> 3 ] | ( 1 << ( segmentIndex & 7 ) );

    cruiSegment* currentSegment;
    if( segmentIndex == cruiCamera.current_segment_index )
    {
        cruiRenderCurrentSegment = cruiCamera.current_segment;
        currentSegment = &cruiCamera.current_segment;
    }
    else
    {
        cruiRenderCurrentSegment = cruiSegments[ segmentIndex ];
        currentSegment = &cruiRenderCurrentSegment;
    }

    cruiRenderSegmentIndex = segmentIndex;

    cruiCurrentFrustumNormalCount = frustumCount;
    int i;
    for( i = 0; i < frustumCount; i = i + 1 )
    {
        int nextIdx = ( i + 1 ) % frustumCount;
        cruiFrustumVertexToVec3d( &frustumVertices[i], &frustumVertices[nextIdx], &cruiCurrentFrustumNormals[i] );
    }

    cruiLoopThroughSegmentWalls( segmentIndex, currentSegment, true, CRUI_CB_RENDER );

    // ENABLE_SHOOTING active - render shots visible in this segment
    int s;
    for( s = 0; s < cruiNumShots; s = s + 1 )
    {
        if( cruiShots[s].current_segment != segmentIndex )
            continue;

        cruiPolygon flarePolygon;
        flarePolygon.draw_edges = 1;
        flarePolygon.num_vertices = 2;
        flarePolygon.vertices[0].x = cruiShots[s].x << 8;
        flarePolygon.vertices[0].y = cruiShots[s].y << 8;
        flarePolygon.vertices[0].z = cruiShots[s].z << 8;
        flarePolygon.vertices[1].x = flarePolygon.vertices[0].x + ( cruiShots[s].dx << 5 );
        flarePolygon.vertices[1].y = flarePolygon.vertices[0].y + ( cruiShots[s].dy << 5 );
        flarePolygon.vertices[1].z = flarePolygon.vertices[0].z + ( cruiShots[s].dz << 5 );

        cruiTransformWorldSpaceToViewSpace( flarePolygon.vertices, 2 );
        cruiRenderPolygon( &flarePolygon, 2 );
    }
}

// Real upstream update_scene() (its own debug/RAM/frame-time HUD text is
// all real dead code in this build - see header comment - so this port
// draws nothing but the real 3D scene itself, matching the real active
// configuration exactly).
void cruiUpdateScene()
{
    int i;
    for( i = 0; i < CRUI_SEGMENTS_TOUCHED_SIZE; i = i + 1 )
        cruiSegmentsTouched[i] = 0;

    cruiFrustumVertex* p = cruiPushFrustum( cruiCamera.current_segment_index, 4 );
    if( p != (cruiFrustumVertex*)0 )
    {
        cruiMakeFrustumVertex( 0, 0, &p[0] );
        cruiMakeFrustumVertex( 0, 768, &p[1] );
        cruiMakeFrustumVertex( 1344, 768, &p[2] );
        cruiMakeFrustumVertex( 1344, 0, &p[3] );
    }

    int segment;
    int vertexCount;
    p = cruiPopFrustum( &segment, &vertexCount );
    while( p != (cruiFrustumVertex*)0 )
    {
        cruiRenderSegment( segment, vertexCount, p );
        p = cruiPopFrustum( &segment, &vertexCount );
    }
}

// -----------------------------------------------------------------------------
//   Top-level state machine
// -----------------------------------------------------------------------------

int cruiState;

void cruiUpdateTitle()
{
    gbSetColor( GB_BLACK );
    gbSetFont( gbFont5x7 );
    gbCursorX = 12;
    gbCursorY = 16;
    gbPrintString( "CRUISER" );
    gbSetFont( gbFont3x5 );
    gbCursorX = 14;
    gbCursorY = 32;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        cruiState = CRUI_STATE_PLAYING;
        // see this file's own header comment on why this is genuinely
        // needed here (unlike upstream's own dormant allow_steering)
        cruiAllowSteering = false;
    }
}

void gameCruiser_init()
{
    gbBegin();
    cruiState = CRUI_STATE_TITLE;
    cruiFrustumStackTop = 0;
    cruiAllowSteering = true;
    cruiResetGame();
    cruiCamera.width = LCDWIDTH;
    cruiCamera.height = LCDHEIGHT;
}

void gameCruiser_update()
{
    if( !gbUpdate() ) return;

    if( cruiState == CRUI_STATE_TITLE )
    {
        cruiUpdateTitle();
        gbRenderFrame();
        return;
    }

    cruiHandleControls();
    cruiMovePlayer();
    cruiUpdateScene();
    gbRenderFrame();
}
