#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// GlaciGlaca (Clement83, License: None specified -
// github.com/Clement83/GlaciGlaca). An ice-cream-shop management sim: each
// of up to 7 real days ("jour"), the player first spends the previous day's
// takings ("cagnotte") restocking cone types ("pot"/"cornet"/"luxeCornet")
// and 12 real ice-cream flavors from a small in-game shop, then serves a
// randomly-sized queue of clients (queue size driven by a random "weather"
// roll shown as a sun/cloud/rain icon) - each client silently "orders" one
// cone + 2 scoops (drawn as icons in a speech bubble), the player picks a
// cone + 2 scoops to match, and gets paid (or docked a real per-item
// "malus") based on how many of the 3 choices were actually correct. Stock
// decays 20% overnight; running out of both cagnotte and stock ends the
// game early, as does reaching day 7.
//
// Every real `gb.x.y(...)` call site was mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment for why -
// this dialect has no classes/methods). Every global got a `glaci`-prefixed
// name (this cartridge has no linker - every ported game shares one C
// translation unit). `random(a,b)` became `a + arand(b-a)` (this dialect's
// own established RNG helper); `gb.pickRandomSeed()` became the documented
// no-op `gbPickRandomSeed()`, called at the same real upstream site
// (`initGame()`, ported as `glaciBeginDifficultyMenu()`) purely for
// fidelity. `gb.battery.show = false;` was dropped outright (purely
// cosmetic - same precedent as gamePong.c).
//
// REAL BITMAP ART RESTORED, verified byte-for-byte via a small Python
// script that re-parsed every real `const byte NAME[] PROGMEM = {...}`
// array directly out of the real `.ino` source (not hand-transcribed) and
// cross-checked each one's own real `width,height` header against its own
// real byte count (`ceil(width/8)*height + 2 == arrayLength`) before
// trusting it - all 72 real sprites needed by this port (12 flavor icons,
// 3 weather icons, 3 cone/cornet icons, 1 speech-bubble outline, 1 UI
// arrow, 1 title splash, and all 17 clients' own 3 real mood sprites -
// N(ormal, arriving)/C(ontent, happy departure)/PC(pas content, unhappy
// departure) - each) passed. Every one is a real, direct
// `int[N] glaciBmpXxx = { width, height, byte0, byte1, ... }` array, the
// exact format `gbDrawBitmap()` expects - no conversion needed. Three more
// real byte arrays exist verbatim in the upstream source (an unnumbered
// generic `pC`/`pN`/`pPC` sprite trio, declared between p6 and p7) but are
// never referenced by upstream's own real `clients[NB_CLIENT]` table
// (which only ever indexes p1..p17) - confirmed genuinely dead/unused
// leftover art, not a missed 18th client, and not ported here.
//
// Every real `gb.display.drawBitmap(...)` call site now has a direct
// `gbDrawBitmap()` counterpart at upstream's own exact real coordinates -
// no rotation/flip is used anywhere in this game (upstream never calls the
// 5-argument overload), so only the plain `gbDrawBitmap(x,y,bitmap)` form
// is needed. Every screen here draws onto a freshly-cleared (plain white)
// frame every tick with no persistent background layer underneath any
// sprite (`gbUpdate()`'s own automatic per-tick `gbClear()`) - so, unlike
// FlappyBirdo/Parachute, there is no mask/fill-underneath-outline bug class
// to check for here: every bitmap this game draws is self-contained, with
// nothing behind it that could bleed through.
//
// STRUCTS AVOIDED BY CHOICE, NOT BY DIALECT LIMITATION: upstream's own
// `Gout`/`Client` typedef structs (an icon pointer bundled with
// price/stock/mood fields) were ported as parallel `int`/`int*` arrays
// instead of a real Vircon32 struct array, even though
// `VIRCON32_C_DIALECT.md` confirms named struct arrays with nested
// initializer lists do genuinely compile (`struct Name {...}; Name[3] arr
// = {{...},{...}};`) - this is a deliberate, defensive choice, not a
// dialect gap: no other game shipped in this project has used a struct yet
// (zero local precedent to lean on, and this project's own toolchain isn't
// reachable from this session to compile-test against), whereas the
// `int*[N] name = { bmpA, bmpB, ... }` array-of-bitmap-pointers pattern
// used here for `glaciGoutSprite`/`glaciPotSprite`/`glaciClientN/C/PC` is
// already proven and shipped (`gameUfoRace.c`'s own `int*[UFO_NUM_SPRITES]
// ufoSprites`). Every real `allGout[i].sprite`/`.prix`/`.nbStock`/
// `.prixAchat`/`allPots[i].*`/`clients[i].sprite[k]` upstream call site
// became a plain parallel-array index instead.
//
// Upstream's own blocking `gb.titleScreen(TitleScreen)` (called once at
// boot) became an explicit `GLACI_STATE_TITLE`, dismissed by a genuine
// fresh Button A press, matching every other ported game's own title
// screen in this project (see gamePong.c's own header comment). Upstream's
// own blocking `gb.menu(menu, 3)` (a real, built-in Gamebuino Classic
// list-menu widget with no equivalent here, used to pick a difficulty that
// sets the starting `cagnotte`) became `GLACI_STATE_DIFFICULTY`, a small
// hand-rolled up/down/A list - the same "blocking widget -> explicit
// state" treatment already used for `gameConduit.c`'s own
// `COND_STATE_MENU` (upstream's own `gb.menu()` there too) and
// `gameFlappyBirdo.c`'s own difficulty picker.
//
// Two MORE real blocking calls existed here that neither Pong nor Conduit
// needed a precedent for: upstream's own `affichageJour()`/
// `affichageFinJour()` are each a full second `while(true){if(gb.update())
// {...}}` loop (a real day-start/day-end splash, shown for up to 80 real
// frames or until dismissed early by Button A) called *from inside* the
// `PREPARE_TEMPS`/`FIN_DEPART_CLIENT` cases of the outer, real, top-level
// `loop()`'s own `switch(gameState)`. Both became their own explicit
// states (`GLACI_STATE_DAYINTRO`/`GLACI_STATE_DAYEND`), each with a local
// `glaciSplashFrames` counter standing in for the blocking loop's own local
// `cpt` counter, dismissed the same two ways upstream's own loop was
// (`glaciSplashFrames==80` or a fresh Button A press).
//
// Upstream's own top-level `if(gb.buttons.pressed(BTN_C)){goTitleScreen();}`
// check runs unconditionally at the very top of the real `loop()`, *before*
// its own `switch(gameState)` - but since `goTitleScreen()` itself is
// blocking (title screen, then the difficulty menu, both fully resolved
// before the call returns), a genuine Button C press during real gameplay
// immediately freezes the current screen and jumps straight into that
// whole blocking title+menu flow, only re-entering the switch once a fresh
// game has actually been set up. This global "Button C = full restart"
// shortcut is preserved here too, applied to every one of this port's own
// "real loop() case" states (`PREPARE_TEMPS`/`MAGASIN`/`CHOIX_CLIENT`/
// `ARRIVE_CLIENT`/`CHOIX_VENDEUR`/`DEPART_CLIENT_CALC`/`DEPART_CLIENT`/
// `FIN_DEPART_CLIENT`/`GAME_OVER`) exactly matching which states upstream's
// own single top-level `switch` covers - but deliberately NOT wired into
// `GLACI_STATE_TITLE`/`DIFFICULTY`/`DAYINTRO`/`DAYEND`, matching upstream
// exactly too (those four all correspond to upstream's OWN separate
// blocking calls, each with no Button C check of their own either).
//
// A REAL UPSTREAM DATA-PERSISTENCE QUIRK, PRESERVED: `initGame()` (ported
// as `glaciBeginDifficultyMenu()`) only ever resets `allGout[]`'s own
// `nbStock` field (12 literal, explicit `allGout[N].nbStock = ...`
// assignments) - it never touches `allPots[]`'s own `nbStock` at all. Real
// `allPots[NB_POT] = {{0,9,1,pot}, {1,0,2,cornet}, {2,0,2,luxeCornet}}`'s
// own `9,0,0` starting stock is therefore a real C/C++ static initializer
// that only ever actually runs once, at real program start - a full
// restart via Button C mid-game, or via pressing A on the real Game Over
// screen (both of which re-run `initGame()`), genuinely carries over
// whatever cone/cornet/luxe-cornet stock was left over from the PREVIOUS
// playthrough instead of resetting it to 9/0/0. Reproduced exactly:
// `glaciPotStock[]` is only ever seeded with `{9,0,0}` once, in
// `gameGlaciGlaca_init()` (this port's own one-time "real program start"
// equivalent - see `portVircon32.c`'s own dispatch loop, which calls a
// game's `_init()` fresh every single time it's chosen from the top-level
// menu) - `glaciBeginDifficultyMenu()` itself resets `glaciGoutStock[]`
// (matching upstream's own 12 explicit assignments) but never touches
// `glaciPotStock[]`, exactly like upstream's own `initGame()`.
//
// A REAL UPSTREAM BUG FOUND BUT *NOT* PRESERVED, since reproducing it
// literally would be a genuine out-of-bounds array write rather than a
// harmless leftover-state quirk: `updateChoixVendeur()`'s own cone-index
// wraparound reads `if(currentChoixInterface>=NB_POT){currentChoixInterface
// = 0;} else if(currentChoixInterface<0){currentChoixInterface = NB_POT;}`
// - the forward wrap correctly lands in-bounds at index 0, but the
// backward wrap lands at index `NB_POT` (3) itself, one PAST the real
// 3-element `allPots[0..2]` array, unlike the symmetric, genuinely correct
// modulo wraparound the flavor-selection code just below it uses
// (`currentChoixInterface = NB_GOUT + currentChoixInterface` - never lands
// out of bounds). On real hardware this is already a real out-of-bounds
// struct-array read *and write* (`allPots[3].nbStock--` on a genuine
// Button A press at that index, corrupting whatever real SRAM byte happens
// to sit right after the `allPots` array) - not a "wrap to an
// intentionally-unreachable slot" design, just a plain off-by-one typo
// (the sibling flavor-selection wraparound, 6 lines below in the same real
// function, gets this right). This project's own norm is to preserve real,
// reachable bugs rather than silently "fix" them - but every other
// preserved bug in this project so far (Conduit's AVR-underflow-as-
// implicit-bounds-check, Catcher's Button-C soft-lock) leaves behind
// harmless stale *state*, never an actual out-of-bounds memory write into
// an adjacent global whose real identity depends entirely on this port's
// own unrelated declaration order (a plain `int[3] glaciPotStock` here,
// not a packed AVR struct array) - so the "reproduced" consequence
// wouldn't even be a recognizable analog of the real bug, just an
// unrelated stray global silently corrupted. Fixed here to wrap to
// `GLACI_NB_POT - 1` (the same correct treatment already used one branch
// over), rather than porting forward a genuine memory-safety hazard for no
// faithful behavioral gain.
//
// A genuinely dead, result-discarded upstream call, dropped outright: the
// real `CHOIX_VENDEUR` case calls `verrifCanServClient()` a SECOND time,
// right after `printCagnotte()`, with its own return value completely
// unused (the function is pure/read-only, so this second call has zero
// effect on anything) - not ported, matching this project's own precedent
// for the same category of leftover call (Conduit's own dead `count` debug
// variable).
//
// The real `STATS` game-state value is declared, and has its own (empty)
// case in upstream's own top-level `switch` - but nothing anywhere in the
// real source ever actually sets `gameState = STATS`, so it's genuinely
// unreachable. Dropped outright rather than porting a dead enum value.
//
// A REAL FONT-STATE "LEAK" BETWEEN SCREENS, PRESERVED EXACTLY: upstream's
// own `drawArriveClient()`/`drawChoixVendeur()` never call `setFont()`
// themselves, so each one draws with whatever font a DIFFERENT, earlier
// screen happened to leave selected - real hardware's font is one
// persistent global, not a per-screen setting. Concretely: the very first
// client of a given day sees its own greeting message rendered in the tiny
// `font3x3` (leftover from `drawMagasin()`'s own explicit
// `setFont(font3x3)`, since nothing resets it before `drawArriveClient()`
// runs), while every client after that instead inherits `font3x5`
// (leftover from the PREVIOUS client's own `drawDepartClient()`, which
// *does* call `setFont(font3x5)` explicitly). This port reproduces the
// exact same leak by likewise never inserting an extra `gbSetFont()` call
// into `glaciDrawArriveClient()`/`glaciDrawChoixVendeur()` - this shim's
// own font state is just as global/persistent as real hardware's, so the
// same inconsistency naturally falls out of the same real call-site gaps.
//
// Upstream's own `println()`-based multi-line text blocks
// (`affichageFinJour()`'s 3-line splash, `drawGameOver()`'s 2-line tally)
// were rewritten as separate `gbPrintString()`/`gbPrintNumber()` calls with
// the cursor explicitly repositioned between lines (`gbCursorX = 0;
// gbCursorY = gbCursorY + gbFontSize*gbFontHeight;`, matching
// `gbPrintString()`'s own real `'\n'`-handling formula exactly) rather than
// a single string literal embedding a literal `\n` - matching this
// project's own established precedent of not relying on `'\n'` inside a
// quoted string literal (see `gameFlappyBirdo.c`'s own header comment: its
// difficulty screen's real two-`'\n'` text block was likewise expanded
// into one `gbPrintString()` call per line).
//
// Real octal-escape icon glyphs (`"\34"` = octal 34 = decimal 28, a coin/
// currency icon printed after every real cagnotte amount; `"\02"`/`"\01"`
// = decimal 2/1, the real happy/unhappy client-tally icons on the Game
// Over screen) are non-printable low-ASCII codes a quoted string literal
// can't hold directly - `"\34"` alone (no other text sharing the same
// literal) is drawn with a single direct `gbDrawChar(28, x, y)` call,
// exactly the "single non-text glyph" use case documented on
// `gbDrawChar()`'s own header comment; the one real mixed case (upstream's
// `print("\34 les 3")`, an icon plus trailing printable text in one
// literal) was built as a small explicit `int[]` array instead, matching
// the same established precedent as `gameTaquin.c`'s own D-pad-arrow
// array/`gameSimonbuino.c`'s own reset-icon array.
//
// This game needs real `INVERT` (used upstream to blink the currently-
// selected shop item/UI button by XOR-toggling whatever's already drawn
// underneath it, not painting a flat fill). Ported via the shim's own
// `GB_INVERT` color constant
// (every drawing primitive's own color branch XORs the target bit when
// `gbColor == GB_INVERT`), so every call site here just does
// `gbSetColor(GB_INVERT); gbFillRect(...);` like real upstream's own
// `setColor(INVERT); fillRect(...);` pairing.
//
// Upstream reads `gb.frameCount` (a real, free-running frame counter
// exposed by the real Gamebuino Classic library) to blink the shop/vendor
// selection cursor (`gb.frameCount % 10 > 4`). Ported here via the shim's
// own `gbFrameCount` global (incremented once per real logic tick inside
// `gbUpdate()`, matching real hardware's own placement).
//
// No EEPROM persistence is used - upstream never touches real EEPROM
// either (there's no cross-session high score here, just a single
// in-session `cagnotte`/day counter).

// -----------------------------------------------------------------------------
// Bitmap data - real upstream PROGMEM byte arrays, copied verbatim (see this
// file's own header comment on how these were verified)
// -----------------------------------------------------------------------------

// rectangleSimple (9x8)
int glaciBmpRectangleSimple[18]=
{
9,8,0,0,60,0,36,0,36,0,36,0,36,0,36,0,60,0,
};

// rondBarre (9x8)
int glaciBmpRondBarre[18]=
{
9,8,0,0,28,0,50,0,121,0,93,0,79,0,38,0,28,0,
};

// rondCroix (9x8)
int glaciBmpRondCroix[18]=
{
9,8,0,0,28,0,34,0,73,0,93,0,73,0,34,0,28,0,
};

// rondMultip (9x8)
int glaciBmpRondMultip[18]=
{
9,8,0,0,28,0,34,0,85,0,73,0,85,0,34,0,28,0,
};

// rondSimple (9x8)
int glaciBmpRondSimple[18]=
{
9,8,0,0,56,0,68,0,130,0,130,0,130,0,68,0,56,0,
};

// trianglePois (9x8)
int glaciBmpTrianglePois[18]=
{
9,8,0,0,0,0,8,0,20,0,42,0,85,0,170,128,255,128,
};

// triangleSimple (9x8)
int glaciBmpTriangleSimple[18]=
{
9,8,0,0,0,0,8,0,20,0,34,0,65,0,128,128,255,128,
};

// carreCroix (9x8)
int glaciBmpCarreCroix[18]=
{
9,8,0,0,255,0,129,0,153,0,189,0,153,0,129,0,255,0,
};

// carreSimple (9x8)
int glaciBmpCarreSimple[18]=
{
9,8,0,0,255,0,129,0,129,0,129,0,129,0,129,0,255,0,
};

// croixOuverte (9x8)
int glaciBmpCroixOuverte[18]=
{
9,8,24,0,24,0,36,0,195,0,195,0,36,0,24,0,24,0,
};

// croixPleine (9x8)
int glaciBmpCroixPleine[18]=
{
9,8,24,0,24,0,24,0,255,0,255,0,24,0,24,0,24,0,
};

// rectanglePlein (9x8)
int glaciBmpRectanglePlein[18]=
{
9,8,0,0,60,0,36,0,60,0,60,0,60,0,36,0,60,0,
};

// pluie (32x19)
int glaciBmpPluie[78]=
{
32,19,3,252,0,0,4,31,224,0,12,7,144,0,56,0,8,0,32,0,
7,0,96,0,0,192,224,0,0,48,128,0,0,16,240,0,0,16,31,255,
255,240,0,2,0,48,1,10,8,0,1,42,73,0,9,40,73,0,8,32,
65,0,8,162,16,0,0,130,16,0,0,130,16,0,0,0,16,0,
};

// soleil (32x21)
int glaciBmpSoleil[86]=
{
32,21,1,8,64,0,1,136,192,0,48,201,134,0,24,0,12,0,12,62,
24,0,6,65,48,0,0,128,128,0,1,0,64,0,162,0,34,128,90,0,
45,0,2,0,32,0,2,0,32,0,1,0,64,0,8,128,136,0,24,65,
12,0,48,62,6,0,33,128,194,0,3,16,96,0,6,16,48,0,0,16,
0,0,0,16,0,0,
};

// soleilNuage (40x21)
int glaciBmpSoleilNuage[107]=
{
40,21,0,132,32,0,0,0,196,96,0,0,24,100,192,0,0,12,0,0,
0,0,6,31,14,15,224,3,32,249,16,16,0,64,192,160,8,0,129,128,
64,4,81,3,0,32,4,45,2,0,32,4,1,2,0,16,4,1,2,0,
0,4,0,130,0,0,4,4,66,0,0,8,12,35,0,0,40,24,31,128,
224,16,16,192,255,24,32,1,136,0,7,192,3,8,0,0,0,0,8,0,
0,0,0,8,0,0,0,
};

// cornet (11x6)
int glaciBmpCornet[14]=
{
11,6,255,224,85,64,42,128,21,0,10,0,4,0,
};

// luxeCornet (11x9)
int glaciBmpLuxeCornet[20]=
{
11,9,255,224,85,64,42,128,21,0,10,0,4,0,4,0,4,0,31,0,
};

// pot (12x5)
int glaciBmpPot[12]=
{
12,5,255,240,224,112,112,224,63,192,31,128,
};

// bulleVide (52x27)
int glaciBmpBulleVide[191]=
{
52,27,0,0,0,1,192,0,0,0,0,1,255,126,0,0,0,0,63,0,
3,248,0,0,7,192,0,0,14,0,0,60,0,0,0,3,0,0,224,0,
0,0,0,192,1,128,0,0,0,0,96,3,0,0,0,0,0,32,2,0,
0,0,0,0,48,2,0,0,0,0,0,16,6,0,0,0,0,0,16,12,
0,0,0,0,0,16,16,0,0,0,0,0,16,48,0,0,0,0,0,16,
64,0,0,0,0,0,16,95,0,0,0,0,0,16,243,0,0,0,0,0,
16,6,0,0,0,0,0,16,4,0,0,0,0,0,16,4,0,0,0,0,
0,48,4,0,0,0,0,0,32,4,0,0,0,0,0,64,4,0,0,0,
0,1,192,7,252,0,0,0,7,0,0,3,192,0,0,28,0,0,0,124,
0,3,240,0,0,0,7,255,254,0,0,
};

// flecheDroite (8x5)
int glaciBmpFlecheDroite[7]=
{
8,5,16,8,252,8,16,
};

// TitleScreen (64x36)
int glaciBmpTitleScreen[290]=
{
64,36,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,208,137,192,0,0,0,0,1,17,84,128,0,0,63,0,1,81,208,
128,0,1,192,224,1,81,84,128,0,6,0,24,0,221,73,192,0,24,31,
196,0,0,0,0,0,48,16,34,0,0,0,0,0,64,48,52,0,0,0,
0,0,67,192,10,128,13,8,136,0,132,0,1,0,17,21,92,0,132,0,
0,128,21,29,28,0,132,0,1,0,21,21,84,0,132,51,12,128,13,212,
148,0,134,76,213,0,0,0,0,0,133,136,34,128,0,0,0,0,132,0,
0,128,0,0,0,0,68,48,48,128,0,0,0,0,72,0,0,128,0,0,
0,0,56,0,0,128,0,0,0,0,12,0,1,12,112,0,0,0,12,8,
2,12,136,0,0,0,10,15,4,19,84,0,0,0,8,128,8,97,36,0,
0,0,24,127,240,97,84,0,0,0,16,0,0,18,136,0,0,0,16,0,
0,63,248,0,0,0,16,0,0,21,80,0,0,0,16,0,0,10,160,0,
0,0,16,0,0,5,64,0,0,0,16,0,0,2,128,0,0,0,16,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,
0,0,0,0,0,7,192,0,0,0,
};

// p1C (26x29)
int glaciBmpP1C[118]=
{
26,29,0,0,0,0,0,0,192,0,2,3,192,0,6,7,128,0,7,63,
128,0,15,255,128,0,31,255,248,0,31,255,248,0,31,255,248,0,63,255,
255,128,63,255,255,128,63,255,255,0,63,255,254,0,63,255,254,0,63,255,
255,0,127,255,255,128,254,247,239,0,126,114,206,0,62,48,200,0,60,16,
136,0,30,0,8,0,14,0,8,0,6,0,8,0,4,33,16,0,2,63,
16,0,2,0,32,0,1,0,64,0,0,225,128,0,0,30,0,0,
};

// p1N (26x29)
int glaciBmpP1N[118]=
{
26,29,0,0,0,0,0,0,192,0,2,3,192,0,6,7,128,0,7,63,
128,0,15,255,128,0,31,255,248,0,31,255,248,0,31,255,248,0,63,255,
255,128,63,255,255,128,63,255,255,0,63,255,254,0,63,255,254,0,63,255,
255,0,127,255,255,128,254,247,239,0,126,114,206,0,62,48,200,0,60,16,
136,0,30,0,8,0,14,0,8,0,6,0,8,0,4,30,16,0,2,9,
16,0,2,7,32,0,1,0,64,0,0,225,128,0,0,30,0,0,
};

// p1PC (26x29)
int glaciBmpP1PC[118]=
{
26,29,0,0,0,0,0,0,192,0,2,3,192,0,6,7,128,0,7,63,
128,0,15,255,128,0,31,255,248,0,31,255,248,0,31,255,248,0,63,255,
255,128,63,255,255,128,63,255,255,0,63,255,254,0,63,255,254,0,63,255,
255,0,127,255,255,128,254,247,239,0,126,114,206,0,62,48,200,0,60,16,
136,0,30,0,8,0,14,0,8,0,6,0,8,0,4,0,16,0,2,124,
16,0,2,68,32,0,1,0,64,0,0,225,128,0,0,30,0,0,
};

// p2C (26x29)
int glaciBmpP2C[118]=
{
26,29,0,0,0,0,0,31,224,0,0,255,192,0,1,255,192,0,3,255,
224,0,23,255,248,0,31,255,248,0,31,255,252,0,31,255,252,0,31,255,
252,0,31,254,62,0,31,252,222,0,31,253,46,0,31,48,207,0,31,0,
7,0,31,0,6,128,31,128,14,0,31,144,142,0,63,143,14,0,31,64,
28,0,15,32,124,0,15,31,252,0,15,0,124,0,15,0,124,0,7,0,
124,0,7,0,116,0,6,0,64,0,14,0,0,0,0,0,0,0,
};

// p2N (26x29)
int glaciBmpP2N[118]=
{
26,29,0,0,0,0,0,31,224,0,0,255,192,0,1,255,192,0,3,255,
224,0,23,255,248,0,31,255,248,0,31,255,252,0,31,255,252,0,31,255,
252,0,31,254,62,0,31,252,222,0,31,253,46,0,31,48,207,0,31,0,
7,0,31,0,6,128,31,140,14,0,31,146,14,0,63,140,14,0,31,64,
28,0,15,32,124,0,15,31,252,0,15,0,124,0,15,0,124,0,7,0,
124,0,7,0,116,0,6,0,64,0,14,0,0,0,0,0,0,0,
};

// p2PC (26x29)
int glaciBmpP2PC[118]=
{
26,29,0,0,0,0,0,31,224,0,0,255,192,0,1,255,192,0,3,255,
224,0,23,255,248,0,31,255,248,0,31,255,252,0,31,255,252,0,31,255,
252,0,31,254,62,0,31,252,222,0,31,253,46,0,31,48,207,0,31,0,
7,0,31,0,6,128,31,128,14,0,31,156,14,0,63,146,14,0,31,64,
28,0,15,32,124,0,15,31,252,0,15,0,124,0,15,0,124,0,7,0,
124,0,7,0,116,0,6,0,64,0,14,0,0,0,0,0,0,0,
};

// p3C (26x29)
int glaciBmpP3C[118]=
{
26,29,0,0,0,0,0,255,128,0,3,255,224,0,7,255,240,0,15,255,
248,0,31,255,252,0,31,255,252,0,63,255,254,0,63,255,255,0,63,255,
255,0,63,255,255,0,255,255,255,192,125,182,219,192,124,146,75,128,124,0,
1,128,60,80,161,128,60,32,65,0,60,0,1,0,28,0,3,0,28,0,
2,0,28,0,2,0,14,4,130,0,14,7,132,0,7,0,4,0,6,0,
8,0,3,0,24,0,1,128,96,0,0,255,192,0,0,30,0,0,
};

// p3N (26x29)
int glaciBmpP3N[118]=
{
26,29,0,0,0,0,0,255,128,0,3,255,224,0,7,255,240,0,15,255,
248,0,31,255,252,0,31,255,252,0,63,255,254,0,63,255,255,0,63,255,
255,0,63,255,255,0,255,255,255,192,125,182,219,192,124,146,75,128,124,0,
1,128,60,112,225,128,60,0,1,0,60,0,1,0,28,0,3,0,28,0,
2,0,28,0,2,0,14,0,2,0,14,2,4,0,7,3,132,0,6,0,
8,0,3,0,24,0,1,128,96,0,0,255,192,0,0,30,0,0,
};

// p3PC (26x29)
int glaciBmpP3PC[118]=
{
26,29,0,0,0,0,0,255,128,0,3,255,224,0,7,255,240,0,15,255,
248,0,31,255,252,0,31,255,252,0,63,255,254,0,63,255,255,0,63,255,
255,0,63,255,255,0,255,255,255,192,125,182,219,192,124,146,75,128,124,0,
1,128,60,112,225,128,60,32,65,0,60,0,1,0,28,0,3,0,28,0,
2,0,28,9,2,0,14,15,2,0,14,9,4,0,7,0,4,0,6,0,
8,0,3,0,24,0,1,128,96,0,0,255,192,0,0,30,0,0,
};

// p4C (26x29)
int glaciBmpP4C[118]=
{
26,29,0,31,224,0,1,255,248,0,3,255,252,0,15,255,254,0,31,255,
255,0,31,255,255,0,31,255,255,128,127,255,255,128,127,248,31,192,255,247,
207,192,255,239,239,192,255,239,231,192,255,255,55,192,255,190,215,192,252,30,
215,192,121,206,151,192,122,32,252,128,121,192,112,128,120,0,0,128,56,0,
0,128,56,0,1,0,24,0,1,0,28,15,1,0,12,25,130,0,4,6,
2,0,2,6,4,0,1,128,24,0,0,224,112,0,0,63,192,0,
};

// p4N (26x29)
int glaciBmpP4N[118]=
{
26,29,0,31,224,0,1,255,248,0,3,255,252,0,15,255,254,0,31,255,
255,0,31,255,255,0,31,255,255,128,127,255,255,128,127,248,31,192,255,247,
207,192,255,239,239,192,255,239,231,192,255,255,55,192,255,190,215,192,252,30,
215,192,121,206,151,192,122,32,252,128,121,192,112,128,120,0,0,128,56,0,
0,128,56,0,1,0,24,0,1,0,28,15,1,0,12,25,130,0,4,6,
2,0,2,0,4,0,1,128,24,0,0,224,112,0,0,63,192,0,
};

// p4PC (26x29)
int glaciBmpP4PC[118]=
{
26,29,0,31,224,0,1,255,248,0,3,255,252,0,15,255,254,0,31,255,
255,0,31,255,255,0,31,255,255,128,127,255,255,128,127,248,31,192,255,247,
207,192,255,239,239,192,255,239,231,192,255,255,55,192,255,190,215,192,252,30,
215,192,121,206,151,192,122,32,252,128,121,192,112,128,120,0,0,128,56,0,
0,128,56,0,1,0,24,0,1,0,28,0,1,0,12,60,2,0,4,32,
2,0,2,0,4,0,1,128,24,0,0,224,112,0,0,63,192,0,
};

// p5C (26x29)
int glaciBmpP5C[118]=
{
26,29,12,0,0,0,3,192,0,0,3,255,196,0,3,255,242,0,15,255,
255,0,31,255,255,128,255,255,255,192,255,255,255,192,255,255,255,192,240,255,
255,192,224,127,255,192,96,63,255,192,96,31,255,64,96,15,252,64,96,7,
0,64,97,224,60,64,32,160,40,64,48,96,48,64,48,0,0,64,16,0,
0,64,16,0,0,64,8,0,0,192,8,0,0,128,12,64,0,128,4,32,
1,0,2,28,2,0,1,0,6,0,0,192,24,0,0,63,224,0,
};

// p5N (26x29)
int glaciBmpP5N[118]=
{
26,29,12,0,0,0,3,192,0,0,3,255,196,0,3,255,242,0,15,255,
255,0,31,255,255,128,255,255,255,192,255,255,255,192,255,255,255,192,240,255,
255,192,224,127,255,192,96,63,255,192,96,31,255,64,96,15,252,64,96,7,
0,64,97,224,60,64,32,160,40,64,48,96,48,64,48,0,0,64,16,0,
0,64,16,0,0,64,8,0,0,192,8,0,0,128,12,0,0,128,4,3,
225,0,2,3,226,0,1,0,6,0,0,192,24,0,0,63,224,0,
};

// p5PC (26x29)
int glaciBmpP5PC[118]=
{
26,29,12,0,0,0,3,192,0,0,3,255,196,0,3,255,242,0,15,255,
255,0,31,255,255,128,255,255,255,192,255,255,255,192,255,255,255,192,240,255,
255,192,224,127,255,192,96,63,255,192,96,31,255,64,96,15,252,64,96,7,
0,64,97,224,60,64,32,192,24,64,48,96,48,64,48,0,0,64,16,0,
0,64,16,0,0,64,8,0,0,192,8,0,0,128,12,3,240,128,4,5,
81,0,2,0,2,0,1,0,6,0,0,192,24,0,0,63,224,0,
};

// p6C (26x29)
int glaciBmpP6C[118]=
{
26,29,0,0,0,0,0,127,224,0,3,255,248,0,95,255,252,0,95,255,
254,0,127,255,255,0,31,255,255,128,95,255,255,128,127,255,255,192,63,255,
255,192,191,0,0,64,191,4,16,64,255,218,44,64,63,192,0,64,63,128,
0,64,191,128,0,64,191,136,16,128,255,196,33,128,63,227,195,0,191,176,
6,0,191,143,248,0,255,128,0,0,63,0,0,0,191,0,0,0,191,128,
0,0,255,0,0,0,62,0,0,0,191,128,0,0,255,0,0,0,
};

// p6N (26x29)
int glaciBmpP6N[118]=
{
26,29,0,0,0,0,0,127,224,0,3,255,248,0,95,255,252,0,95,255,
254,0,127,255,255,0,31,255,255,128,95,255,255,128,127,255,255,192,63,255,
255,192,191,0,0,64,191,4,16,64,255,218,44,64,63,192,0,64,63,128,
0,64,191,128,0,64,191,128,64,128,255,192,113,128,63,224,3,0,191,176,
6,0,191,143,248,0,255,128,0,0,63,0,0,0,191,0,0,0,191,128,
0,0,255,0,0,0,62,0,0,0,191,128,0,0,255,0,0,0,
};

// p6PC (26x29)
int glaciBmpP6PC[118]=
{
26,29,0,0,0,0,0,127,224,0,3,255,248,0,95,255,252,0,95,255,
254,0,127,255,255,0,31,255,255,128,95,255,255,128,127,255,255,192,63,255,
255,192,191,0,0,64,191,0,0,64,255,218,44,64,63,196,16,64,63,128,
0,64,191,128,0,64,191,128,0,128,255,195,129,128,63,231,195,0,191,176,
6,0,191,143,248,0,255,128,0,0,63,0,0,0,191,0,0,0,191,128,
0,0,255,0,0,0,62,0,0,0,191,128,0,0,255,0,0,0,
};

// p7C (26x29)
int glaciBmpP7C[118]=
{
26,29,0,0,0,0,0,16,0,0,0,102,0,0,0,255,128,0,3,255,
224,0,7,255,224,0,7,255,240,0,15,249,252,0,15,192,60,0,31,191,
222,0,31,127,238,0,63,255,254,0,255,255,254,0,255,255,255,0,127,255,
255,0,127,255,255,128,127,255,255,128,127,255,255,128,63,223,239,128,63,131,
31,128,62,128,55,0,30,80,131,0,31,9,3,0,15,0,6,0,15,0,
6,0,7,4,68,0,7,3,128,0,1,128,0,0,0,128,0,0,
};

// p7N (26x29)
int glaciBmpP7N[118]=
{
26,29,0,0,0,0,0,16,0,0,0,102,0,0,0,255,128,0,3,255,
224,0,7,255,224,0,7,255,240,0,15,249,252,0,15,192,60,0,31,191,
222,0,31,127,238,0,63,255,254,0,255,255,254,0,255,255,255,0,127,255,
255,0,127,255,255,128,127,255,255,128,127,255,255,128,63,223,239,128,63,131,
31,128,62,128,55,0,30,80,131,0,31,25,131,0,15,0,6,0,15,0,
6,0,7,0,68,0,7,3,128,0,1,128,0,0,0,128,0,0,
};

// p7PC (26x29)
int glaciBmpP7PC[118]=
{
26,29,0,0,0,0,0,16,0,0,0,102,0,0,0,255,128,0,3,255,
224,0,7,255,224,0,7,255,240,0,15,249,252,0,15,192,60,0,31,191,
222,0,31,127,238,0,63,255,254,0,255,255,254,0,255,255,255,0,127,255,
255,0,127,255,255,128,127,255,255,128,127,255,255,128,63,223,239,128,63,131,
31,128,62,128,55,0,30,89,131,0,31,9,3,0,15,0,6,0,15,0,
6,0,7,0,4,0,7,3,128,0,1,132,0,0,0,128,0,0,
};

// p8C (26x29)
int glaciBmpP8C[118]=
{
26,29,0,0,0,0,0,31,128,0,0,127,128,0,0,255,0,0,7,255,
240,0,31,255,248,0,63,255,252,0,63,255,252,0,127,255,252,0,127,255,
254,0,127,255,254,0,255,255,255,0,255,255,255,128,127,255,255,128,127,255,
255,0,127,190,254,0,127,44,156,0,127,0,14,0,124,113,198,0,60,0,
14,0,124,0,14,0,124,0,12,0,124,0,12,0,124,16,28,0,62,15,
24,0,62,0,48,0,23,128,240,0,18,127,192,0,0,0,0,0,
};

// p8N (26x29)
int glaciBmpP8N[118]=
{
26,29,0,0,0,0,0,31,128,0,0,127,128,0,0,255,0,0,7,255,
240,0,31,255,248,0,63,255,252,0,63,255,252,0,127,255,252,0,127,255,
254,0,127,255,254,0,255,255,255,0,255,255,255,128,127,255,255,128,127,255,
255,0,127,190,254,0,127,44,156,0,127,0,14,0,124,113,198,0,60,0,
14,0,124,0,14,0,124,0,12,0,124,0,12,0,124,0,28,0,62,15,
24,0,62,0,48,0,23,128,240,0,18,127,192,0,0,0,0,0,
};

// p8PC (26x29)
int glaciBmpP8PC[118]=
{
26,29,0,0,0,0,0,31,128,0,0,127,128,0,0,255,0,0,7,255,
240,0,31,255,248,0,63,255,252,0,63,255,252,0,127,255,252,0,127,255,
254,0,127,255,254,0,255,255,255,0,255,255,255,128,127,255,255,128,127,255,
255,0,127,190,254,0,127,44,156,0,127,0,14,0,124,113,198,0,60,0,
14,0,124,0,14,0,124,0,12,0,124,0,12,0,124,0,28,0,62,15,
24,0,62,16,48,0,23,128,240,0,18,127,192,0,0,0,0,0,
};

// p9C (26x29)
int glaciBmpP9C[118]=
{
26,29,0,0,0,0,0,0,0,0,0,0,64,0,0,1,192,0,1,15,
240,0,3,255,252,0,13,127,234,0,29,127,235,0,63,255,255,0,63,247,
255,128,127,251,255,128,127,253,239,192,127,254,79,128,127,195,143,128,127,200,
71,128,123,212,167,192,123,192,7,192,121,128,7,192,121,128,7,192,120,196,
15,192,120,195,15,128,124,96,31,128,120,16,39,128,56,15,199,0,56,0,
3,0,24,0,2,0,16,0,2,0,0,0,0,0,0,0,0,0,
};

// p9N (26x29)
int glaciBmpP9N[118]=
{
26,29,0,0,0,0,0,0,0,0,0,0,64,0,0,1,192,0,1,15,
240,0,3,255,252,0,13,127,234,0,29,127,235,0,63,255,255,0,63,247,
255,128,127,251,255,128,127,253,239,192,127,254,79,128,127,195,143,128,127,200,
71,128,123,212,167,192,123,192,7,192,121,128,7,192,121,132,7,192,120,195,
15,192,120,196,15,128,124,96,31,128,120,16,39,128,56,15,199,0,56,0,
3,0,24,0,2,0,16,0,2,0,0,0,0,0,0,0,0,0,
};

// p9PC (26x29)
int glaciBmpP9PC[118]=
{
26,29,0,0,0,0,0,0,0,0,0,0,64,0,0,1,192,0,1,15,
240,0,3,255,252,0,13,127,234,0,29,127,235,0,63,255,255,0,63,247,
255,128,127,251,255,128,127,253,239,192,127,254,79,128,127,195,143,128,127,192,
7,128,123,212,167,192,123,200,71,192,121,128,7,192,121,128,7,192,120,199,
15,192,120,196,15,128,124,96,31,128,120,16,39,128,56,15,199,0,56,0,
3,0,24,0,2,0,16,0,2,0,0,0,0,0,0,0,0,0,
};

// p10C (26x29)
int glaciBmpP10C[118]=
{
26,29,6,0,3,0,7,63,199,0,5,224,124,128,8,128,8,128,12,128,
9,128,26,64,18,128,40,128,8,128,111,128,15,128,70,24,51,192,192,24,
48,64,128,3,128,64,142,1,131,64,155,134,199,192,148,252,124,192,148,80,
40,192,148,80,40,192,148,32,17,128,212,32,16,192,84,0,0,192,84,0,
0,192,44,0,0,192,58,0,1,64,26,0,1,64,12,8,131,64,13,15,
134,64,21,7,15,128,25,128,57,128,9,240,247,0,7,63,194,0,
};

// p10N (26x29)
int glaciBmpP10N[118]=
{
26,29,6,0,3,0,7,63,199,0,5,224,124,128,8,128,8,128,12,128,
9,128,26,64,18,128,40,128,8,128,111,128,15,128,70,24,51,192,192,24,
48,64,128,3,128,64,142,1,131,64,155,134,199,192,148,252,124,192,148,80,
40,192,148,80,40,192,148,32,17,128,212,32,16,192,84,0,0,192,84,0,
0,192,44,0,0,192,58,0,1,64,26,15,129,64,12,8,131,64,13,8,
134,64,21,7,15,128,25,128,57,128,9,240,247,0,7,63,194,0,
};

// p10PC (26x29)
int glaciBmpP10PC[118]=
{
26,29,6,0,3,0,7,63,199,0,5,224,124,128,8,128,8,128,12,128,
9,128,26,64,18,128,40,128,8,128,111,128,15,128,70,24,51,192,192,24,
48,64,128,3,128,64,142,1,131,64,155,134,199,192,148,252,124,192,148,80,
40,192,148,80,40,192,148,32,17,128,212,32,16,192,84,0,0,192,84,0,
0,192,44,0,0,192,58,0,1,64,26,0,1,64,12,7,3,64,13,13,
134,64,21,0,15,128,25,128,57,128,9,240,247,0,7,63,194,0,
};

// p11C (26x29)
int glaciBmpP11C[118]=
{
26,29,0,0,0,0,1,207,192,0,7,255,248,0,31,255,252,0,31,255,
255,0,31,255,255,128,31,255,255,128,31,255,255,128,31,255,255,128,31,254,
255,128,31,188,123,128,31,152,51,128,31,136,35,128,31,128,3,128,31,128,
3,128,31,144,135,128,31,207,15,128,31,192,31,128,15,240,127,128,15,143,
254,0,7,128,126,0,7,128,124,0,7,128,124,0,7,128,120,0,3,128,
120,0,3,192,120,0,3,192,120,0,3,192,248,0,3,192,248,0,
};

// p11N (26x29)
int glaciBmpP11N[118]=
{
26,29,0,0,0,0,1,207,192,0,7,255,248,0,31,255,252,0,31,255,
255,0,31,255,255,128,31,255,255,128,31,255,255,128,31,255,255,128,31,254,
255,128,31,188,123,128,31,152,51,128,31,136,35,128,31,128,3,128,31,128,
3,128,31,128,7,128,31,200,15,128,31,207,31,128,15,240,127,128,15,143,
254,0,7,128,126,0,7,128,124,0,7,128,124,0,7,128,120,0,3,128,
120,0,3,192,120,0,3,192,120,0,3,192,248,0,3,192,248,0,
};

// p11PC (26x29)
int glaciBmpP11PC[118]=
{
26,29,0,0,0,0,1,207,192,0,7,255,248,0,31,255,252,0,31,255,
255,0,31,255,255,128,31,255,255,128,31,255,255,128,31,255,255,128,31,254,
255,128,31,188,123,128,31,152,51,128,31,136,35,128,31,128,3,128,31,128,
3,128,31,128,7,128,31,199,143,128,31,196,159,128,15,240,127,128,15,143,
254,0,7,128,126,0,7,128,124,0,7,128,124,0,7,128,120,0,3,128,
120,0,3,192,120,0,3,192,120,0,3,192,248,0,3,192,248,0,
};

// p12C (26x29)
int glaciBmpP12C[118]=
{
26,29,0,63,128,0,1,241,224,0,3,0,48,0,4,0,24,0,8,0,
4,0,24,0,6,0,16,0,3,0,32,0,1,0,32,0,1,128,96,0,
0,128,64,0,0,128,67,7,128,128,70,226,254,192,71,185,2,192,70,135,
2,128,65,33,137,128,65,24,49,128,33,24,49,128,36,128,1,128,18,128,
1,0,27,128,3,0,11,128,2,0,63,128,3,192,29,15,199,0,3,132,
140,0,0,199,152,0,0,96,48,0,0,63,224,0,0,7,0,0,
};

// p12N (26x29)
int glaciBmpP12N[118]=
{
26,29,0,63,128,0,1,241,224,0,3,0,48,0,4,0,24,0,8,0,
4,0,24,0,6,0,16,0,3,0,32,0,1,0,32,0,1,128,96,0,
0,128,64,0,0,128,67,7,128,128,70,226,254,192,71,185,2,192,70,135,
2,128,65,33,137,128,65,24,49,128,33,24,49,128,36,128,1,128,18,128,
1,0,27,128,3,0,11,128,2,0,63,128,3,192,29,15,199,0,3,128,
12,0,0,192,24,0,0,96,48,0,0,63,224,0,0,7,0,0,
};

// p12PC (26x29)
int glaciBmpP12PC[118]=
{
26,29,0,63,128,0,1,241,224,0,3,0,48,0,4,0,24,0,8,0,
4,0,24,0,6,0,16,0,3,0,32,0,1,0,32,0,1,128,96,0,
0,128,64,0,0,128,67,7,128,128,70,226,254,192,71,185,2,192,70,135,
2,128,65,33,137,128,65,24,49,128,33,24,49,128,36,128,1,128,18,128,
1,0,27,128,3,0,11,128,2,0,63,135,131,192,29,12,7,0,3,140,
12,0,0,192,24,0,0,96,48,0,0,63,224,0,0,7,0,0,
};

// p13C (26x29)
int glaciBmpP13C[118]=
{
26,29,16,16,0,0,16,16,0,0,24,16,0,0,8,32,0,0,12,96,
0,0,4,64,0,0,7,224,0,0,31,240,0,0,63,255,224,0,63,255,
248,0,63,255,252,0,127,255,254,0,127,255,255,0,255,255,255,128,255,255,
255,128,255,255,255,192,255,255,255,192,255,255,127,192,255,252,63,192,255,224,
3,192,127,0,1,192,62,28,113,192,30,0,3,192,14,0,3,128,6,4,
135,128,7,3,11,0,3,192,115,0,1,63,194,0,1,0,0,0,
};

// p13N (26x29)
int glaciBmpP13N[118]=
{
26,29,16,16,0,0,16,16,0,0,24,16,0,0,8,32,0,0,12,96,
0,0,4,64,0,0,7,224,0,0,31,240,0,0,63,255,224,0,63,255,
248,0,63,255,252,0,127,255,254,0,127,255,255,0,255,255,255,128,255,255,
255,128,255,255,255,192,255,255,255,192,255,255,127,192,255,252,63,192,255,224,
3,192,127,0,1,192,62,28,113,192,30,0,3,192,14,0,3,128,6,0,
7,128,7,3,11,0,3,192,115,0,1,63,194,0,1,0,0,0,
};

// p13PC (26x29)
int glaciBmpP13PC[118]=
{
26,29,16,16,0,0,16,16,0,0,24,16,0,0,8,32,0,0,12,96,
0,0,4,64,0,0,7,224,0,0,31,240,0,0,63,255,224,0,63,255,
248,0,63,255,252,0,127,255,254,0,127,255,255,0,255,255,255,128,255,255,
255,128,255,255,255,192,255,255,255,192,255,255,127,192,255,252,63,192,255,224,
3,192,127,0,1,192,62,28,113,192,30,8,35,192,14,0,3,128,6,1,
135,128,7,2,11,0,3,192,115,0,1,63,194,0,1,0,0,0,
};

// p14C (26x29)
int glaciBmpP14C[118]=
{
26,29,0,127,128,0,1,255,224,0,3,255,248,0,7,255,252,0,31,255,
254,0,31,255,254,0,63,255,255,0,127,255,255,128,127,255,255,128,127,255,
255,192,255,255,255,192,255,255,255,192,252,7,241,192,252,3,224,192,252,1,
192,192,252,24,140,192,224,16,8,192,224,0,0,128,96,0,0,128,112,0,
0,128,112,0,0,128,120,8,17,128,120,7,225,128,124,3,195,128,126,0,
7,128,127,0,15,128,127,128,63,128,127,255,255,128,127,128,63,128,
};

// p14N (26x29)
int glaciBmpP14N[118]=
{
26,29,0,127,128,0,1,255,224,0,3,255,248,0,7,255,252,0,31,255,
254,0,31,255,254,0,63,255,255,0,127,255,255,128,127,255,255,128,127,255,
255,192,255,255,255,192,255,255,255,192,252,7,241,192,252,3,224,192,252,1,
192,192,252,24,140,192,224,16,8,192,224,0,0,128,96,0,0,128,112,0,
0,128,112,0,0,128,120,0,1,128,120,16,1,128,124,15,131,128,126,0,
15,128,127,0,15,128,127,128,63,128,127,255,255,128,127,128,63,128,
};

// p14PC (26x29)
int glaciBmpP14PC[118]=
{
26,29,0,127,128,0,1,255,224,0,3,255,248,0,7,255,252,0,31,255,
254,0,31,255,254,0,63,255,255,0,127,255,255,128,127,255,255,128,127,255,
255,192,255,255,255,192,255,255,255,192,252,7,241,192,252,3,224,192,252,1,
192,192,252,24,140,192,224,16,8,192,224,0,0,128,96,0,0,128,112,0,
0,128,112,0,0,128,120,7,1,128,120,5,1,128,124,7,3,128,126,0,
7,128,127,0,15,128,127,128,63,128,127,255,255,128,127,128,63,128,
};

// p15C (32x29)
int glaciBmpP15C[118]=
{
32,29,0,31,128,0,0,224,112,0,3,0,12,0,12,15,226,0,24,8,
17,0,32,24,26,0,33,224,5,64,66,0,0,128,66,0,0,64,66,0,
0,128,66,25,134,64,67,38,106,128,66,196,17,64,66,8,16,64,32,24,
24,64,36,0,0,64,28,0,0,64,6,4,64,128,6,4,65,0,5,3,
130,0,4,192,4,0,12,63,248,0,8,0,0,0,8,0,0,0,8,0,
0,0,8,0,0,0,8,0,0,0,8,0,0,0,8,0,0,0,
};

// p15N (32x29)
int glaciBmpP15N[118]=
{
32,29,0,31,128,0,0,224,112,0,3,0,12,0,12,15,226,0,24,8,
17,0,32,24,26,0,33,224,5,64,66,0,0,128,66,0,0,64,66,0,
0,128,66,25,134,64,67,38,106,128,66,196,17,64,66,0,0,64,34,24,
24,64,36,0,0,64,28,0,0,64,6,0,0,128,6,4,1,0,5,7,
130,0,4,64,4,0,12,63,248,0,8,0,0,0,8,0,0,0,8,0,
0,0,8,0,0,0,8,0,0,0,8,0,0,0,8,0,0,0,
};

// p15PC (32x29)
int glaciBmpP15PC[118]=
{
32,29,0,31,128,0,0,224,112,0,3,0,12,0,12,15,226,0,24,8,
17,0,32,24,26,0,33,224,5,64,66,0,0,128,66,0,0,64,66,0,
0,128,66,25,134,64,67,38,106,128,66,196,17,64,66,0,0,64,32,24,
24,64,36,0,0,64,28,0,0,64,6,7,0,128,6,4,129,0,5,7,
194,0,4,192,4,0,12,63,248,0,8,0,0,0,8,0,0,0,8,0,
0,0,8,0,0,0,8,0,0,0,8,0,0,0,8,0,0,0,
};

// p16C (32x29)
int glaciBmpP16C[118]=
{
32,29,0,0,0,0,0,0,24,0,0,0,112,0,0,1,240,0,0,15,
224,0,0,255,248,0,63,255,254,0,31,255,255,0,15,255,255,128,15,255,
255,192,31,255,255,192,31,255,251,192,63,255,249,192,63,191,252,192,63,129,
224,192,63,188,30,64,63,164,18,64,63,44,22,64,31,24,12,64,30,0,
0,128,126,24,12,128,15,0,0,128,7,3,225,0,6,130,33,0,12,65,
194,0,12,48,28,0,4,15,224,0,0,0,0,0,0,0,0,0,
};

// p16N (32x29)
int glaciBmpP16N[118]=
{
32,29,0,0,0,0,0,0,24,0,0,0,112,0,0,1,240,0,0,15,
224,0,0,255,248,0,63,255,254,0,31,255,255,0,15,255,255,128,15,255,
255,192,31,255,255,192,31,255,251,192,63,255,249,192,63,191,252,192,63,129,
224,192,63,188,30,64,63,164,18,64,63,44,22,64,31,24,12,64,30,0,
0,128,126,24,12,128,15,0,0,128,7,2,1,0,6,131,193,0,12,66,
2,0,12,48,28,0,4,15,224,0,0,0,0,0,0,0,0,0,
};

// p16PC (32x29)
int glaciBmpP16PC[118]=
{
32,29,0,0,0,0,0,0,24,0,0,0,112,0,0,1,240,0,0,15,
224,0,0,255,248,0,63,255,254,0,31,255,255,0,15,255,255,128,15,255,
255,192,31,255,255,192,31,255,251,192,63,255,249,192,63,191,252,192,63,129,
224,192,63,188,30,64,63,164,18,64,63,44,22,64,31,24,12,64,30,0,
0,128,126,24,12,128,15,1,192,128,7,2,33,0,6,131,225,0,12,64,
2,0,12,48,28,0,4,15,224,0,0,0,0,0,0,0,0,0,
};

// p17C (32x29)
int glaciBmpP17C[118]=
{
32,29,0,31,224,0,0,240,32,0,1,128,96,0,3,0,240,0,22,1,
24,0,28,2,4,0,28,2,4,0,24,0,2,0,16,0,2,0,16,0,
2,0,19,0,1,0,16,128,5,0,16,124,9,0,16,255,252,128,32,170,
85,128,32,144,37,192,80,128,5,0,112,196,13,0,240,199,11,0,16,192,
18,0,16,112,114,0,24,31,196,0,8,0,4,0,8,0,4,0,8,0,
30,0,12,0,62,0,14,0,42,0,14,0,64,0,11,0,64,0,
};

// p17N (32x29)
int glaciBmpP17N[118]=
{
32,29,0,31,224,0,0,240,32,0,1,128,96,0,3,0,240,0,22,1,
24,0,28,2,4,0,28,2,4,0,24,0,2,0,16,0,2,0,16,0,
2,0,19,0,1,0,16,128,5,0,16,124,9,0,16,255,252,128,32,170,
85,128,32,144,37,192,80,128,5,0,112,192,13,0,240,199,11,0,16,192,
18,0,16,112,114,0,24,31,196,0,8,0,4,0,8,0,4,0,8,0,
30,0,12,0,62,0,14,0,42,0,14,0,64,0,11,0,64,0,
};

// p17PC (32x29)
int glaciBmpP17PC[118]=
{
32,29,0,31,224,0,0,240,32,0,1,128,96,0,3,0,240,0,22,1,
24,0,28,2,4,0,28,2,4,0,24,0,2,0,16,0,2,0,16,0,
2,0,19,0,1,0,16,128,5,0,16,124,9,0,16,255,252,128,32,170,
85,128,32,144,37,192,80,128,5,0,112,192,13,0,240,199,11,0,16,196,
18,0,16,112,114,0,24,31,196,0,8,0,4,0,8,0,4,0,8,0,
30,0,12,0,62,0,14,0,42,0,14,0,64,0,11,0,64,0,
};

// -----------------------------------------------------------------------------
// Constants - real upstream #define values, glaci-prefixed
// -----------------------------------------------------------------------------

#define GLACI_NB_GOUT 12
#define GLACI_NB_POT 3
#define GLACI_NB_CLIENT 17
#define GLACI_NB_MAX_BOULLE 2
#define GLACI_NB_MESSAGE 5
#define GLACI_X_MESSAGE 43
#define GLACI_Y_MESSAGE 15
#define GLACI_MALUS_BOULLE 1
#define GLACI_MALUS_CORNET 1
#define GLACI_DEPART_CAGNOTE_FACILE 21
#define GLACI_DEPART_CAGNOTE_NORMAL 15
#define GLACI_DEPART_CAGNOTE_DIFFICIL 12
#define GLACI_NB_MAX_CLIENT 16
#define GLACI_NB_MIN_CLIENT 3
#define GLACI_SEUIL_BEAU_TEMP 10
#define GLACI_SEUIL_PLUIE 6
#define GLACI_NB_MAX_JOUR 7

// -----------------------------------------------------------------------------
// Data tables - real upstream Gout/Client arrays, ported as parallel
// int/int* arrays instead of a real struct array (see this file's own
// header comment on why). Order matches upstream's own real
// allGout[]/allPots[]/clients[] initializer order exactly.
// -----------------------------------------------------------------------------

int glaciGoutPrix[GLACI_NB_GOUT]=
{
1,1,1,1,2,1,2,2,2,2,2,1
};

int glaciGoutPrixAchat[GLACI_NB_GOUT]=
{
1,1,1,1,2,1,1,1,2,2,2,1
};

int glaciGoutStock[GLACI_NB_GOUT]; // runtime - reset every new game, see glaciResetGoutStock()

int* glaciGoutSprite[GLACI_NB_GOUT]=
{
    glaciBmpCarreSimple,
    glaciBmpRectangleSimple,
    glaciBmpTriangleSimple,
    glaciBmpRondSimple,
    glaciBmpRondMultip,
    glaciBmpCroixPleine,
    glaciBmpRectanglePlein,
    glaciBmpCroixOuverte,
    glaciBmpRondBarre,
    glaciBmpTrianglePois,
    glaciBmpCarreCroix,
    glaciBmpRondCroix
};

int glaciPotPrix[GLACI_NB_POT]=
{
0,1,2
};

int glaciPotPrixAchat[GLACI_NB_POT]=
{
1,2,2
};

// Runtime - only ever seeded once, in gameGlaciGlaca_init() (this port's
// own "real program start" equivalent), never reset by
// glaciBeginDifficultyMenu() - a real, preserved upstream quirk, see this
// file's own header comment.
int glaciPotStock[GLACI_NB_POT];

int* glaciPotSprite[GLACI_NB_POT]=
{
    glaciBmpPot,
    glaciBmpCornet,
    glaciBmpLuxeCornet
};

int* glaciClientN[GLACI_NB_CLIENT]=
{
    glaciBmpP1N, glaciBmpP2N, glaciBmpP3N, glaciBmpP4N, glaciBmpP5N,
    glaciBmpP6N, glaciBmpP7N, glaciBmpP8N, glaciBmpP9N, glaciBmpP10N,
    glaciBmpP11N, glaciBmpP12N, glaciBmpP13N, glaciBmpP14N, glaciBmpP15N,
    glaciBmpP16N, glaciBmpP17N
};

int* glaciClientC[GLACI_NB_CLIENT]=
{
    glaciBmpP1C, glaciBmpP2C, glaciBmpP3C, glaciBmpP4C, glaciBmpP5C,
    glaciBmpP6C, glaciBmpP7C, glaciBmpP8C, glaciBmpP9C, glaciBmpP10C,
    glaciBmpP11C, glaciBmpP12C, glaciBmpP13C, glaciBmpP14C, glaciBmpP15C,
    glaciBmpP16C, glaciBmpP17C
};

int* glaciClientPC[GLACI_NB_CLIENT]=
{
    glaciBmpP1PC, glaciBmpP2PC, glaciBmpP3PC, glaciBmpP4PC, glaciBmpP5PC,
    glaciBmpP6PC, glaciBmpP7PC, glaciBmpP8PC, glaciBmpP9PC, glaciBmpP10PC,
    glaciBmpP11PC, glaciBmpP12PC, glaciBmpP13PC, glaciBmpP14PC, glaciBmpP15PC,
    glaciBmpP16PC, glaciBmpP17PC
};

// Real upstream `print("\34 les 3")` - a mix of one non-printable icon
// glyph (28, the coin/currency icon) and printable text in a single real
// literal - built as an explicit int array (see this file's own header
// comment).
char glaciTextLes3[8]=
{
    28, 32, 108, 101, 115, 32, 51, 0 // "<coin icon> les 3"
};

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

enum GlaciState
{
    GLACI_STATE_TITLE = 0,
    GLACI_STATE_DIFFICULTY = 1,
    GLACI_STATE_DAYINTRO = 2,
    GLACI_STATE_DAYEND = 3,
    GLACI_STATE_PREPARE_TEMPS = 4,
    GLACI_STATE_MAGASIN = 5,
    GLACI_STATE_CHOIX_CLIENT = 6,
    GLACI_STATE_ARRIVE_CLIENT = 7,
    GLACI_STATE_CHOIX_VENDEUR = 8,
    GLACI_STATE_DEPART_CLIENT_CALC = 9,
    GLACI_STATE_DEPART_CLIENT = 10,
    GLACI_STATE_FIN_DEPART_CLIENT = 11,
    GLACI_STATE_GAME_OVER = 12
};

int glaciState;

// Local stand-in for real gb.frameCount (missing from this shim - see this
// file's own header comment) - incremented once per real gbUpdate() tick.

// Local stand-in for affichageJour()/affichageFinJour()'s own real local
// `cpt` counter (each blocking splash loop's own real frame counter).
int glaciSplashFrames;

int glaciDifficultyIndex;

int glaciCurrentMessage;
int glaciChoixCornet;
int glaciChoixBoulle[GLACI_NB_MAX_BOULLE];

int glaciChoixCornetVendeur;
int glaciChoixBoulleVendeur[GLACI_NB_MAX_BOULLE];
int glaciCurrentChoixVendeur;

int glaciCagnotte;
int glaciClientPayer;

int glaciCurrentClient;
int glaciNbClient;
int glaciJour;

int glaciNbClientContent;
int glaciNbClientPasContent;

int glaciMnuMagSelectionCurrent;

bool glaciCornetIsOk;
int glaciGlaceIsOk;

int glaciCurrentChoixInterface = 0;

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

// Direct port of upstream's own printCagnotte(x,y) - the no-argument
// overload's real call sites (`printCagnotte();`) became explicit
// `glaciPrintCagnotte(0,0)` calls instead (this dialect has no function
// overloading).
void glaciPrintCagnotte( int x, int y )
{
    gbCursorX = x;
    gbCursorY = y;
    gbPrintNumber( glaciCagnotte );
    gbDrawChar( 28, gbCursorX, gbCursorY ); // real "\34" coin icon
}

// Real upstream `bonjours[NB_MESSAGE]`/`aurevoir[NB_MESSAGE]`/
// `aurevoirPasContent[NB_MESSAGE]` string tables - ported as if/else-if
// chains rather than an `int*[5]` array of string-literal pointers (no
// precedent anywhere else in this project for an array of string-literal
// pointers, unlike the already-proven `int*[N]` array-of-*bitmap*-pointers
// pattern used elsewhere in this file - kept to the safer, already-common
// "one gbPrintString() call per branch" shape instead).
void glaciPrintBonjour( int idx )
{
    if( idx == 0 ) gbPrintString( "Yooo" );
    else if( idx == 1 ) gbPrintString( "Hello" );
    else if( idx == 2 ) gbPrintString( "Bonjour" );
    else if( idx == 3 ) gbPrintString( "Yop" );
    else gbPrintString( "hi" );
}

void glaciPrintAurevoir( int idx )
{
    if( idx == 0 ) gbPrintString( "Tcho" );
    else if( idx == 1 ) gbPrintString( "Merci" );
    else if( idx == 2 ) gbPrintString( "   A+" );
    else if( idx == 3 ) gbPrintString( "Hummm" );
    else gbPrintString( "Thank" );
}

void glaciPrintAurevoirPasContent( int idx )
{
    if( idx == 0 ) gbPrintString( "Howww" );
    else if( idx == 1 ) gbPrintString( "Greee" );
    else if( idx == 2 ) gbPrintString( "pfeevv" );
    else if( idx == 3 ) gbPrintString( "Roow!" );
    else gbPrintString( "..." );
}

// -----------------------------------------------------------------------------
// Real end-of-game helpers - direct ports of upstream's own
// verrifCanServClient()/canPlay()
// -----------------------------------------------------------------------------

bool glaciVerrifCanServClient()
{
    int nbGlace = 0;
    int nbCornet = 0;
    int i;

    for( i = 0; i < GLACI_NB_GOUT; i = i + 1 )
      nbGlace = nbGlace + glaciGoutStock[ i ];

    for( i = 0; i < GLACI_NB_POT; i = i + 1 )
      nbCornet = nbCornet + glaciPotStock[ i ];

    return ( nbCornet > 0 && nbGlace > 1 );
}

bool glaciCanPlay()
{
    return glaciJour < GLACI_NB_MAX_JOUR && ( glaciCagnotte > 0 || glaciVerrifCanServClient() );
}

// -----------------------------------------------------------------------------
// Real per-client setup helpers - direct ports of upstream's own
// initClient()/loadChoixClient()
// -----------------------------------------------------------------------------

// Renamed from upstream's own initClient() to avoid reading like it resets
// "the current client" (it actually resets the vendor's own in-progress
// cone/scoop selection, called both at full-game setup and at the start of
// every new client's own serving cycle, exactly like upstream).
void glaciInitVendeurChoice()
{
    glaciCurrentChoixVendeur = -1;
    glaciChoixCornetVendeur = -1;
    int i;
    for( i = 0; i < GLACI_NB_MAX_BOULLE; i = i + 1 )
      glaciChoixBoulleVendeur[ i ] = -1;
    glaciClientPayer = 0;
}

void glaciLoadChoixClient()
{
    glaciCurrentClient = arand( GLACI_NB_CLIENT );
    glaciChoixCornet = arand( GLACI_NB_POT );
    int i;
    for( i = 0; i < GLACI_NB_MAX_BOULLE; i = i + 1 )
      glaciChoixBoulle[ i ] = arand( GLACI_NB_GOUT );
    glaciCurrentMessage = arand( GLACI_NB_MESSAGE );
}

// Real upstream's own 12 explicit `allGout[N].nbStock = ...` assignments in
// initGame() - allPots[] is deliberately NOT touched here, matching
// upstream exactly (see this file's own header comment).
void glaciResetGoutStock()
{
    glaciGoutStock[ 0 ] = 3;
    glaciGoutStock[ 1 ] = 3;
    glaciGoutStock[ 2 ] = 3;
    glaciGoutStock[ 3 ] = 0;
    glaciGoutStock[ 4 ] = 0;
    glaciGoutStock[ 5 ] = 0;
    glaciGoutStock[ 6 ] = 0;
    glaciGoutStock[ 7 ] = 0;
    glaciGoutStock[ 8 ] = 0;
    glaciGoutStock[ 9 ] = 0;
    glaciGoutStock[ 10 ] = 0;
    glaciGoutStock[ 11 ] = 0;
}

// -----------------------------------------------------------------------------
// State transitions
// -----------------------------------------------------------------------------

void glaciBeginTitle()
{
    glaciState = GLACI_STATE_TITLE;
}

void glaciBeginGameOver()
{
    glaciState = GLACI_STATE_GAME_OVER;
}

void glaciBeginDayIntro()
{
    glaciState = GLACI_STATE_DAYINTRO;
    glaciSplashFrames = 0;
}

// Direct port of upstream's own affichageJour() (a real blocking splash,
// see this file's own header comment) - draws "JOUR : N" until dismissed by
// a fresh Button A press or 80 real frames elapse.
void glaciUpdateDayIntro()
{
    glaciSplashFrames = glaciSplashFrames + 1;

    gbFontSize = 1;
    gbSetFont( gbFont5x7 );
    gbCursorX = 10;
    gbCursorY = 10;
    gbPrintString( "JOUR : " );
    gbPrintNumber( glaciJour );

    if( glaciSplashFrames == 80 || gbPressed( BTN_A ) )
      glaciState = GLACI_STATE_MAGASIN;
}

void glaciBeginDayEnd()
{
    glaciState = GLACI_STATE_DAYEND;
    glaciSplashFrames = 0;
}

// Direct port of upstream's own affichageFinJour() - its own real
// println()-based 3-line layout rewritten as explicit per-line cursor
// placement (see this file's own header comment).
void glaciUpdateDayEnd()
{
    glaciSplashFrames = glaciSplashFrames + 1;

    gbFontSize = 1;
    gbSetFont( gbFont5x7 );
    gbCursorX = 10;
    gbCursorY = 10;
    gbPrintString( "SOIR : " );
    gbPrintNumber( glaciJour );
    gbCursorX = 0;
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbPrintString( "Perte de 20%" );
    gbCursorX = 0;
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbPrintString( "   du stock" );

    if( glaciSplashFrames == 80 || gbPressed( BTN_A ) )
      glaciState = GLACI_STATE_PREPARE_TEMPS;
}

// Direct port of upstream's own real initGame() setup steps that run
// BEFORE its own blocking gb.menu() call (the menu itself is
// glaciUpdateDifficulty() below) - real gb.battery.show=false dropped (see
// this file's own header comment).
void glaciBeginDifficultyMenu()
{
    glaciState = GLACI_STATE_DIFFICULTY;
    glaciDifficultyIndex = 0;
    gbPickRandomSeed();

    glaciResetGoutStock();
    glaciInitVendeurChoice();
    glaciJour = 0;
    glaciNbClientContent = 0;
    glaciNbClientPasContent = 0;
}

// Direct port of upstream's own real blocking gb.titleScreen(TitleScreen)
// (see this file's own header comment).
void glaciUpdateTitle()
{
    gbSetColor( 1 );
    gbDrawBitmap( ( LCDWIDTH - 64 ) / 2, 2, glaciBmpTitleScreen );

    gbFontSize = 1;
    gbSetFont( gbFont3x5 );
    gbCursorX = 14;
    gbCursorY = 40;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
      glaciBeginDifficultyMenu();
}

// Hand-rolled replacement for upstream's own blocking `gb.menu(menu, 3)`
// widget (see this file's own header comment) - three difficulty options,
// matching upstream's own real switch(gb.menu(...)) cases exactly
// (upstream's own case -1, "nothing chosen", is unreachable here since this
// hand-rolled list always has a selection - folded into the same default
// FACILE outcome as upstream's own real case 0/-1/default all share).
void glaciUpdateDifficulty()
{
    gbFontSize = 1;
    gbSetFont( gbFont5x7 );
    gbCursorX = 8;
    gbCursorY = 4;
    gbPrintString( "GLACIGLACA" );

    gbSetFont( gbFont3x5 );
    int i;
    for( i = 0; i < 3; i = i + 1 )
    {
        gbCursorX = 10;
        gbCursorY = 20 + i * 8;
        if( i == glaciDifficultyIndex )
          gbPrintString( "> " );
        else
          gbPrintString( "  " );

        if( i == 0 ) gbPrintString( "Facile" );
        else if( i == 1 ) gbPrintString( "Normal" );
        else gbPrintString( "Difficile" );
    }

    if( gbPressed( BTN_UP ) )
      glaciDifficultyIndex = glaciDifficultyIndex - 1;
    if( gbPressed( BTN_DOWN ) )
      glaciDifficultyIndex = glaciDifficultyIndex + 1;
    if( glaciDifficultyIndex < 0 ) glaciDifficultyIndex = 2;
    if( glaciDifficultyIndex > 2 ) glaciDifficultyIndex = 0;

    if( gbPressed( BTN_A ) )
    {
        if( glaciDifficultyIndex == 0 ) glaciCagnotte = GLACI_DEPART_CAGNOTE_FACILE;
        else if( glaciDifficultyIndex == 1 ) glaciCagnotte = GLACI_DEPART_CAGNOTE_NORMAL;
        else glaciCagnotte = GLACI_DEPART_CAGNOTE_DIFFICIL;

        glaciState = GLACI_STATE_PREPARE_TEMPS;
    }
}

// Direct port of upstream's own real updatePrepareTemps() (the 20%
// overnight stock decay + next-day client-count roll).
void glaciDoPrepareTemps()
{
    glaciNbClient = GLACI_NB_MIN_CLIENT + arand( GLACI_NB_MAX_CLIENT - GLACI_NB_MIN_CLIENT );
    glaciMnuMagSelectionCurrent = 0;
    if( glaciJour > 1 )
    {
        int i;
        for( i = 0; i < GLACI_NB_GOUT; i = i + 1 )
          glaciGoutStock[ i ] = (int)( (float)glaciGoutStock[ i ] * 0.8 );
    }
}

// Direct port of upstream's own real `case PREPARE_TEMPS` - the real
// affichageJour() splash it calls inline became the separate
// GLACI_STATE_DAYINTRO state (see this file's own header comment).
void glaciUpdatePrepareTemps()
{
    if( glaciCanPlay() )
    {
        glaciJour = glaciJour + 1;
        glaciDoPrepareTemps();
        glaciBeginDayIntro();
    }
    else
      glaciBeginGameOver();
}

// -----------------------------------------------------------------------------
// Shop ("magasin")
// -----------------------------------------------------------------------------

// Direct port of upstream's own real drawMagasin().
void glaciDrawMagasin()
{
    int x = 0;
    int offsetY = 0;

    gbSetColor( 1 );
    if( glaciNbClient > GLACI_SEUIL_BEAU_TEMP )
      gbDrawBitmap( 50, 1, glaciBmpSoleil );
    else if( glaciNbClient > GLACI_SEUIL_PLUIE )
      gbDrawBitmap( 50, 1, glaciBmpSoleilNuage );
    else
      gbDrawBitmap( 50, 1, glaciBmpPluie );

    gbFontSize = 1;
    gbSetFont( gbFont3x3 );

    int i;
    for( i = 0; i < GLACI_NB_GOUT; i = i + 1 )
    {
        if( i == 5 ) { x = 16; offsetY = 45; }
        if( i == 10 ) { x = 33; offsetY = 90; }
        int y = ( i * 9 ) - offsetY;

        gbSetColor( 1 );
        gbDrawBitmap( x, y, glaciGoutSprite[ i ] );

        gbCursorX = x + 10;
        gbCursorY = y + 4;
        gbPrintNumber( glaciGoutStock[ i ] );

        if( glaciMnuMagSelectionCurrent == i )
        {
            if( ( gbFrameCount % 10 ) > 4 )
            {
                gbSetColor( GB_INVERT );
                gbFillRect( x, y, 9, 9 );
                gbSetColor( 1 );
            }
        }
    }

    for( i = 0; i < GLACI_NB_POT; i = i + 1 )
    {
        int py = 19 + ( i * 8 );

        gbSetColor( 1 );
        gbDrawBitmap( 33, py, glaciPotSprite[ i ] );

        gbCursorX = 46;
        gbCursorY = py + 2;
        gbPrintNumber( glaciPotStock[ i ] );

        if( glaciMnuMagSelectionCurrent == ( i + 12 ) && glaciMnuMagSelectionCurrent < 15 )
        {
            int h = 7;
            if( glaciMnuMagSelectionCurrent == 14 ) h = 11;
            if( ( gbFrameCount % 10 ) > 4 )
            {
                gbSetColor( GB_INVERT );
                gbFillRect( 33, py - 1, 12, h );
                gbSetColor( 1 );
            }
        }
    }

    gbSetColor( 1 );
    gbDrawRect( 64, 41, 19, 7 );
    gbDrawBitmap( 75, 42, glaciBmpFlecheDroite );
    if( glaciMnuMagSelectionCurrent == 19 && ( gbFrameCount % 10 ) > 4 )
    {
        gbSetColor( GB_INVERT );
        gbFillRect( 64, 41, 19, 7 );
        gbSetColor( 1 );
    }
    gbCursorX = 66;
    gbCursorY = 43;
    gbPrintString( "GO" );

    if( glaciMnuMagSelectionCurrent != 19 )
    {
        glaciPrintCagnotte( 60, 24 );
        gbCursorX = 60;
        gbCursorY = 30;
        gbPrintString( "prix" );

        gbCursorX = 54;
        gbCursorY = 36;
        if( glaciMnuMagSelectionCurrent < 12 )
          gbPrintNumber( glaciGoutPrixAchat[ glaciMnuMagSelectionCurrent ] );
        else if( glaciMnuMagSelectionCurrent != 19 )
          gbPrintNumber( glaciPotPrixAchat[ glaciMnuMagSelectionCurrent - 12 ] );
        gbPrintString( glaciTextLes3 );
    }
}

// Direct port of upstream's own real updateMagasin().
void glaciUpdateMagasin()
{
    if( gbPressed( BTN_UP ) )
      glaciMnuMagSelectionCurrent = glaciMnuMagSelectionCurrent - 1;
    else if( gbPressed( BTN_DOWN ) )
      glaciMnuMagSelectionCurrent = glaciMnuMagSelectionCurrent + 1;
    else if( gbPressed( BTN_LEFT ) )
      glaciMnuMagSelectionCurrent = glaciMnuMagSelectionCurrent - 5;
    else if( gbPressed( BTN_RIGHT ) )
      glaciMnuMagSelectionCurrent = glaciMnuMagSelectionCurrent + 5;

    if( glaciMnuMagSelectionCurrent < 0 )
      glaciMnuMagSelectionCurrent = 15 + glaciMnuMagSelectionCurrent;
    else if( glaciMnuMagSelectionCurrent > 14 )
    {
        if( glaciMnuMagSelectionCurrent != 19 )
          glaciMnuMagSelectionCurrent = glaciMnuMagSelectionCurrent - 15;
    }

    if( gbPressed( BTN_A ) )
    {
        if( glaciMnuMagSelectionCurrent < 12 )
        {
            if( glaciCagnotte >= glaciGoutPrixAchat[ glaciMnuMagSelectionCurrent ] )
            {
                glaciGoutStock[ glaciMnuMagSelectionCurrent ] = glaciGoutStock[ glaciMnuMagSelectionCurrent ] + 3;
                if( glaciGoutStock[ glaciMnuMagSelectionCurrent ] > 9 )
                  glaciGoutStock[ glaciMnuMagSelectionCurrent ] = 9;

                glaciCagnotte = glaciCagnotte - glaciGoutPrixAchat[ glaciMnuMagSelectionCurrent ];
            }
        }
        else if( glaciMnuMagSelectionCurrent != 19 )
        {
            int potIdx = glaciMnuMagSelectionCurrent - 12;
            if( glaciCagnotte >= glaciPotPrixAchat[ potIdx ] )
            {
                glaciPotStock[ potIdx ] = glaciPotStock[ potIdx ] + 3;
                if( glaciPotStock[ potIdx ] > 9 )
                  glaciPotStock[ potIdx ] = 9;

                glaciCagnotte = glaciCagnotte - glaciPotPrixAchat[ potIdx ];
            }
        }
        else
          glaciState = GLACI_STATE_CHOIX_CLIENT;
    }
}

// -----------------------------------------------------------------------------
// Arriving client
// -----------------------------------------------------------------------------

// Direct port of upstream's own real drawArriveClient() - deliberately NO
// gbSetFont() call here (see this file's own header comment on the real
// font-state leak this preserves).
void glaciDrawArriveClient()
{
    gbSetColor( 1 );
    gbDrawBitmap( 4, 10, glaciClientN[ glaciCurrentClient ] );
    gbDrawBitmap( 28, 10, glaciBmpBulleVide );
    gbCursorX = GLACI_X_MESSAGE;
    gbCursorY = GLACI_Y_MESSAGE;
    glaciPrintBonjour( glaciCurrentMessage );
    gbDrawBitmap( 37, 23, glaciPotSprite[ glaciChoixCornet ] );

    int i;
    for( i = 0; i < GLACI_NB_MAX_BOULLE; i = i + 1 )
      gbDrawBitmap( 57 + ( i * 11 ), 21, glaciGoutSprite[ glaciChoixBoulle[ i ] ] );
}

void glaciUpdateArriveClient()
{
    if( gbPressed( BTN_A ) )
      glaciState = GLACI_STATE_CHOIX_VENDEUR;
}

// -----------------------------------------------------------------------------
// Vendor's own cone/scoop selection
// -----------------------------------------------------------------------------

// Direct port of upstream's own real drawChoixVendeur() - deliberately NO
// gbSetFont() call here either (see this file's own header comment).
void glaciDrawChoixVendeur()
{
    int y = 28;
    int offsetX = 0;
    int i;

    for( i = 0; i < GLACI_NB_GOUT; i = i + 1 )
    {
        if( i == 6 ) { y = 38; offsetX = 66; }
        int x = ( 2 + ( i * 11 ) ) - offsetX;

        gbSetColor( 1 );
        gbDrawBitmap( x, y, glaciGoutSprite[ i ] );
        if( glaciGoutStock[ i ] == 0 )
        {
            gbSetColor( GB_INVERT );
            gbFillRect( x, y, 9, 8 );
            gbSetColor( 1 );
        }

        if( glaciCurrentChoixVendeur >= 0 )
        {
            int f;
            for( f = 0; f < GLACI_NB_MAX_BOULLE; f = f + 1 )
              if( glaciChoixBoulleVendeur[ f ] == i )
                gbDrawRect( x - 2, y - 1, 12, 11 );

            if( glaciCurrentChoixInterface == i && ( gbFrameCount % 10 ) > 4 )
              gbDrawRect( x - 2, y - 1, 12, 11 );
        }
    }

    for( i = 0; i < GLACI_NB_POT; i = i + 1 )
    {
        int py = 2 + ( i * 11 );

        gbSetColor( 1 );
        gbDrawBitmap( 70, py, glaciPotSprite[ i ] );
        if( glaciPotStock[ i ] == 0 )
        {
            gbSetColor( GB_INVERT );
            gbFillRect( 70, py, 11, 9 );
            gbSetColor( 1 );
        }

        if( glaciCurrentChoixVendeur < 0 )
        {
            if( glaciCurrentChoixInterface == i && ( gbFrameCount % 10 ) > 4 )
              gbDrawRect( 68, py - 2, 16, 12 );
        }
        else
        {
            if( glaciChoixCornetVendeur == i )
              gbDrawRect( 68, py - 2, 16, 12 );
        }
    }

    int f;
    for( f = 0; f < GLACI_NB_MAX_BOULLE; f = f + 1 )
      if( glaciChoixBoulleVendeur[ f ] > -1 )
        gbDrawBitmap( 24 + ( f * 5 ), 5, glaciGoutSprite[ glaciChoixBoulleVendeur[ f ] ] );

    if( glaciChoixCornetVendeur > -1 )
      gbDrawBitmap( 25, 13, glaciPotSprite[ glaciChoixCornetVendeur ] );

    if( glaciCurrentChoixVendeur == GLACI_NB_MAX_BOULLE && ( gbFrameCount % 10 ) > 4 )
      gbDrawRect( 22, 3, 16, 22 );
}

// Direct port of upstream's own real updateChoixVendeur() - one real bug
// fixed rather than preserved (the cone-index backward-wrap off-by-one -
// see this file's own header comment for why).
void glaciUpdateChoixVendeur()
{
    if( glaciCurrentChoixVendeur < GLACI_NB_MAX_BOULLE )
    {
        if( glaciCurrentChoixVendeur == -1 )
        {
            // picking the cone
            if( gbPressed( BTN_A ) )
            {
                if( glaciPotStock[ glaciCurrentChoixInterface ] > 0 )
                {
                    glaciPotStock[ glaciCurrentChoixInterface ] = glaciPotStock[ glaciCurrentChoixInterface ] - 1;
                    glaciChoixCornetVendeur = glaciCurrentChoixInterface;
                    glaciCurrentChoixInterface = 0;
                    glaciCurrentChoixVendeur = glaciCurrentChoixVendeur + 1;
                }
            }
        }
        else
        {
            // picking a scoop
            if( gbPressed( BTN_A ) )
            {
                if( glaciGoutStock[ glaciCurrentChoixInterface ] > 0 )
                {
                    glaciGoutStock[ glaciCurrentChoixInterface ] = glaciGoutStock[ glaciCurrentChoixInterface ] - 1;
                    glaciChoixBoulleVendeur[ glaciCurrentChoixVendeur ] = glaciCurrentChoixInterface;
                    glaciCurrentChoixVendeur = glaciCurrentChoixVendeur + 1;
                }
            }
        }
    }
    else
    {
        // fully chosen - only validating left
        if( gbPressed( BTN_A ) )
        {
            glaciCurrentChoixVendeur = -1;
            glaciState = GLACI_STATE_DEPART_CLIENT_CALC;
        }
    }

    if( gbPressed( BTN_B ) )
    {
        glaciCurrentChoixVendeur = glaciCurrentChoixVendeur - 1;
        if( glaciCurrentChoixVendeur > -1 )
        {
            glaciCurrentChoixInterface = glaciChoixBoulleVendeur[ glaciCurrentChoixVendeur ];
            glaciChoixBoulleVendeur[ glaciCurrentChoixVendeur ] = -1;
        }
        else
        {
            glaciCurrentChoixInterface = glaciChoixCornetVendeur;
            glaciChoixCornetVendeur = -1;
        }
    }

    // real upstream safety clamp on glaciCurrentChoixVendeur itself
    if( glaciCurrentChoixVendeur < -1 ) glaciCurrentChoixVendeur = -1;
    if( glaciCurrentChoixVendeur > GLACI_NB_MAX_BOULLE ) glaciCurrentChoixVendeur = GLACI_NB_MAX_BOULLE - 1;

    if( glaciCurrentChoixVendeur < 0 )
    {
        if( gbPressed( BTN_LEFT ) || gbPressed( BTN_UP ) )
          glaciCurrentChoixInterface = glaciCurrentChoixInterface - 1;
        else if( gbPressed( BTN_RIGHT ) || gbPressed( BTN_DOWN ) )
          glaciCurrentChoixInterface = glaciCurrentChoixInterface + 1;

        if( glaciCurrentChoixInterface >= GLACI_NB_POT )
          glaciCurrentChoixInterface = 0;
        else if( glaciCurrentChoixInterface < 0 )
          // real upstream wraps to GLACI_NB_POT here (one past the real
          // valid 0..NB_POT-1 range - see this file's own header comment
          // on why this is fixed, not preserved, in this port)
          glaciCurrentChoixInterface = GLACI_NB_POT - 1;
    }
    else if( glaciCurrentChoixVendeur < GLACI_NB_MAX_BOULLE )
    {
        if( gbPressed( BTN_LEFT ) )
          glaciCurrentChoixInterface = glaciCurrentChoixInterface - 1;
        else if( gbPressed( BTN_RIGHT ) )
          glaciCurrentChoixInterface = glaciCurrentChoixInterface + 1;
        else if( gbPressed( BTN_UP ) )
          glaciCurrentChoixInterface = glaciCurrentChoixInterface + ( GLACI_NB_GOUT / 2 );
        else if( gbPressed( BTN_DOWN ) )
          glaciCurrentChoixInterface = glaciCurrentChoixInterface - ( GLACI_NB_GOUT / 2 );

        if( glaciCurrentChoixInterface >= GLACI_NB_GOUT )
          glaciCurrentChoixInterface = glaciCurrentChoixInterface - GLACI_NB_GOUT;
        else if( glaciCurrentChoixInterface < 0 )
          glaciCurrentChoixInterface = GLACI_NB_GOUT + glaciCurrentChoixInterface;
    }
}

// -----------------------------------------------------------------------------
// Client departure
// -----------------------------------------------------------------------------

// Direct port of upstream's own real updateDepartClientCalc() (the real
// order-matching/payment calculation).
void glaciUpdateDepartClientCalc()
{
    glaciCornetIsOk = ( glaciChoixCornetVendeur == glaciChoixCornet );
    glaciClientPayer = glaciClientPayer + glaciPotPrix[ glaciChoixCornetVendeur ];
    if( glaciCornetIsOk == false )
      glaciClientPayer = glaciClientPayer - GLACI_MALUS_CORNET;

    glaciGlaceIsOk = GLACI_NB_MAX_BOULLE;
    int index[GLACI_NB_MAX_BOULLE];
    int i, t;
    for( i = 0; i < GLACI_NB_MAX_BOULLE; i = i + 1 )
      index[ i ] = -1;

    for( i = 0; i < GLACI_NB_MAX_BOULLE; i = i + 1 )
    {
        glaciClientPayer = glaciClientPayer + glaciGoutPrix[ glaciChoixBoulleVendeur[ i ] ];
        for( t = 0; t < GLACI_NB_MAX_BOULLE; t = t + 1 )
        {
            if( index[ t ] == t ) continue;
            if( glaciChoixBoulleVendeur[ i ] == glaciChoixBoulle[ t ] )
              index[ t ] = t;
        }
    }

    for( i = 0; i < GLACI_NB_MAX_BOULLE; i = i + 1 )
    {
        if( index[ i ] == -1 )
        {
            glaciGlaceIsOk = glaciGlaceIsOk - 1;
            glaciClientPayer = glaciClientPayer - GLACI_MALUS_BOULLE;
        }
    }

    if( glaciCornetIsOk && glaciGlaceIsOk == GLACI_NB_MAX_BOULLE )
      glaciNbClientContent = glaciNbClientContent + 1;
    else
      glaciNbClientPasContent = glaciNbClientPasContent + 1;
}

// Direct port of upstream's own real drawDepartClient() (its own real
// setFont(font3x5) call, unlike drawArriveClient()/drawChoixVendeur() -
// see this file's own header comment).
void glaciDrawDepartClient()
{
    gbSetFont( gbFont3x5 );
    gbSetColor( 1 );

    if( glaciCornetIsOk && glaciGlaceIsOk == GLACI_NB_MAX_BOULLE )
    {
        gbDrawBitmap( 4, 10, glaciClientC[ glaciCurrentClient ] );
        gbCursorX = GLACI_X_MESSAGE;
        gbCursorY = GLACI_Y_MESSAGE;
        glaciPrintAurevoir( glaciCurrentMessage );
    }
    else
    {
        gbDrawBitmap( 4, 10, glaciClientPC[ glaciCurrentClient ] );
        gbCursorX = GLACI_X_MESSAGE;
        gbCursorY = GLACI_Y_MESSAGE;
        glaciPrintAurevoirPasContent( glaciCurrentMessage );
    }

    gbDrawBitmap( 28, 10, glaciBmpBulleVide );
    gbCursorX = 39;
    gbCursorY = 27;
    gbPrintNumber( glaciClientPayer );
    gbDrawChar( 28, gbCursorX, gbCursorY ); // real "\34" coin icon
}

void glaciUpdateDepartClient()
{
    if( gbPressed( BTN_A ) )
      glaciState = GLACI_STATE_FIN_DEPART_CLIENT;
}

// -----------------------------------------------------------------------------
// Game over
// -----------------------------------------------------------------------------

void glaciUpdateGameOver()
{
    if( gbPressed( BTN_A ) )
      glaciBeginDifficultyMenu();
}

// Direct port of upstream's own real drawGameOver() - its own real
// println()-based layout rewritten as explicit per-line cursor placement,
// and its own real "\02"/"\01" icon glyphs drawn directly via gbDrawChar()
// (see this file's own header comment).
void glaciDrawGameOver()
{
    gbSetColor( 1 );

    gbCursorX = 30;
    gbCursorY = 10;
    gbSetFont( gbFont3x5 );
    gbPrintString( "Game Over" );

    gbSetFont( gbFont5x7 );
    gbCursorX = 5;
    gbCursorY = 20;
    gbDrawChar( 2, gbCursorX, gbCursorY ); // real "\02" happy-client icon
    gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    gbPrintNumber( glaciNbClientContent );

    gbCursorX = 5;
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbDrawChar( 1, gbCursorX, gbCursorY ); // real "\01" unhappy-client icon
    gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    gbPrintNumber( glaciNbClientPasContent );

    glaciPrintCagnotte( 20, 35 );
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameGlaciGlaca_init()
{
    gbBegin();

    glaciCurrentChoixInterface = 0;

    // Real allPots[]'s own static-initializer stock (9/0/0) - only ever
    // seeded once, here, matching upstream's own real one-time struct
    // initializer (see this file's own header comment on why
    // glaciBeginDifficultyMenu() never touches this again).
    glaciPotStock[ 0 ] = 9;
    glaciPotStock[ 1 ] = 0;
    glaciPotStock[ 2 ] = 0;

    glaciBeginTitle();
}

// Direct port of upstream's own real loop()/switch(gameState) - see this
// file's own header comment for the top-of-tick Button C restart shortcut
// and the DAYINTRO/DAYEND/DIFFICULTY exceptions to it.
void gameGlaciGlaca_update()
{
    if( !gbUpdate() ) return;


    if( glaciState != GLACI_STATE_TITLE && glaciState != GLACI_STATE_DIFFICULTY &&
        glaciState != GLACI_STATE_DAYINTRO && glaciState != GLACI_STATE_DAYEND &&
        gbPressed( BTN_C ) )
    {
        glaciBeginTitle();
    }
    else if( glaciState == GLACI_STATE_TITLE )
      glaciUpdateTitle();
    else if( glaciState == GLACI_STATE_DIFFICULTY )
      glaciUpdateDifficulty();
    else if( glaciState == GLACI_STATE_DAYINTRO )
      glaciUpdateDayIntro();
    else if( glaciState == GLACI_STATE_DAYEND )
      glaciUpdateDayEnd();
    else if( glaciState == GLACI_STATE_PREPARE_TEMPS )
      glaciUpdatePrepareTemps();
    else if( glaciState == GLACI_STATE_MAGASIN )
    {
        glaciUpdateMagasin();
        glaciDrawMagasin();
    }
    else if( glaciState == GLACI_STATE_CHOIX_CLIENT )
    {
        glaciLoadChoixClient();
        glaciInitVendeurChoice();
        glaciState = GLACI_STATE_ARRIVE_CLIENT;
        glaciDrawArriveClient();
        glaciPrintCagnotte( 0, 0 );
    }
    else if( glaciState == GLACI_STATE_ARRIVE_CLIENT )
    {
        glaciUpdateArriveClient();
        glaciDrawArriveClient();
        glaciPrintCagnotte( 0, 0 );
    }
    else if( glaciState == GLACI_STATE_CHOIX_VENDEUR )
    {
        if( glaciVerrifCanServClient() )
        {
            glaciUpdateChoixVendeur();
            glaciDrawChoixVendeur();
            glaciPrintCagnotte( 0, 0 );
        }
        else
          glaciState = GLACI_STATE_PREPARE_TEMPS;
    }
    else if( glaciState == GLACI_STATE_DEPART_CLIENT_CALC )
    {
        glaciUpdateDepartClientCalc();
        glaciDrawDepartClient();
        glaciState = GLACI_STATE_DEPART_CLIENT;
    }
    else if( glaciState == GLACI_STATE_DEPART_CLIENT )
    {
        glaciUpdateDepartClient();
        glaciDrawDepartClient();
        glaciPrintCagnotte( 0, 0 );
    }
    else if( glaciState == GLACI_STATE_FIN_DEPART_CLIENT )
    {
        glaciCagnotte = glaciCagnotte + glaciClientPayer;
        if( glaciCagnotte < 0 ) glaciCagnotte = 0;

        if( glaciNbClient > 0 )
        {
            glaciState = GLACI_STATE_CHOIX_CLIENT;
            glaciNbClient = glaciNbClient - 1;
        }
        else
          glaciBeginDayEnd();
    }
    else if( glaciState == GLACI_STATE_GAME_OVER )
    {
        glaciUpdateGameOver();
        glaciDrawGameOver();
    }

    gbRenderFrame();
}
