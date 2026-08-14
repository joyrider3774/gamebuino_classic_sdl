#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"

// PetitMonstre / "Futuromon" (Clement83 - github.com/Clement83/PetitMonstre,
// license: none specified - matching every other Clement83 port already in
// this cartridge, e.g. Copter/GlaciGlaca/CrazyTown/Bomber/StickFighter/Tron).
//
// A real Pokemon-style monster-catching/battling RPG: free-roam tile-map
// exploration with a scrolling camera, random wild-monster encounters and
// rival-trainer ("dresseur") fights, turn-based combat (speed-ordered
// attacks, 6 elemental types, a catch mechanic, XP/leveling), and a
// Team/Futurodex status-review menu. Four real "theme" world areas
// (Futur/worldExterieur/Space/Elec) are regenerated fresh every time the
// player walks off an edge, each with its own random trainer and 3 random
// pickup bonuses.
//
// STRUCTURAL NOTE - the single biggest porting challenge here, bigger than
// any bitmap/font work: real upstream is built almost entirely out of
// nested BLOCKING `while(true){ if(gb.update()){ ...; } }` loops (every
// combat sub-phase - intro flash, monster-arrival slide-in, attack/catch
// menus, attack animations, death animations, end-of-fight popup - is its
// own such loop, and a single call to `CombatMonste()` from the outer
// dispatcher runs an entire multi-round fight synchronously, potentially
// hundreds of real ticks, before ever returning). This shim's own contract
// only allows exactly one `gbUpdate()`/`gbRenderFrame()` pair per real
// engine tick (see gamebuinoShim.h's own `gbRenderFrame()` doc comment), so
// every one of those blocking loops was flattened into an explicit state -
// `petmState` for the outer exploration/menu/combat/game-over modes, and a
// second, nested `petmCombatPhase` for every sub-phase combat itself moves
// through (intro flash -> foe arrival -> monster-select menu -> arrival ->
// attack-pick menu -> resolve -> attack animations -> death animation ->
// catch/level-up popups -> round-continue-or-end). Every one of upstream's
// own real per-phase tick counts (45 for an attack animation, 40 for a
// death animation, 60 for an arrival slide, 35 for the end-of-fight pause,
// 20 for the intro flash, etc), early-exit-on-A conditions, and RNG calls
// are preserved exactly; `petmUpdateCombat()`'s own dispatcher just keeps
// re-entering its `switch` in the same real tick for every genuinely
// *instant* upstream step (a plain function call with no `while` loop of
// its own, e.g. picking the wild AI's attack, resolving damage, checking
// who died) and only stops to wait for the next real tick once it reaches
// a phase that actually needs to animate over multiple frames - the same
// shape upstream's own single `if(gb.update())` block naturally has when
// it calls several non-blocking functions in a row before finally calling
// one that opens its own fresh blocking loop.
//
// One deliberate, documented simplification from that flattening: real
// upstream's own blocking loops sometimes skip drawing anything at all on
// the exact tick a sub-phase finishes (its own `break` fires before that
// tick's `drawBitmap()` call), producing a real but genuinely imperceptible
// single blank 1/20th-second frame at a handful of phase boundaries. This
// port's own phases always draw on every tick they're active, including
// their own final tick, rather than replicating each real function's own
// idiosyncratic draw-or-not-before-break micro-timing - every real
// animation's own tick COUNT, EARLY-EXIT condition, and overall sequencing
// is still exact, only that one-frame flicker isn't reproduced bit-for-bit.
//
// CLASS FLATTENING - `Monster`/`Player` (Monster.h/.cpp, Player.h/.cpp) have
// no gameplay logic of their own beyond simple field accessors (matching
// gameDarkShmup.c's/gameSuperSpaceShooter.c's own precedent for a
// method-light real class), so both flattened directly:
//   Monster        -> struct PetmMonster (plain fields, `SelectedAttaque`/
//                      `NumeroPattern` renamed from upstream's own private
//                      `selectedAttaque`/`numeroPattern` since this dialect
//                      has no private/public distinction to preserve)
//   Player (x2 real instances, `ctx->Joueur`/`ctx->Adversaire`) -> plain
//     parallel globals instead of an array-of-Player, since this dialect's
//     own struct-member-array support is still unproven (see
//     gamebuino-solitaire's own documented caution) and there are only ever
//     two real instances anyway: `PetmMonster[4] petmPlayerMonsters`/
//     `petmPlayerNbMonstre`/`petmPlayerSelected` for `ctx->Joueur`, and the
//     identically-shaped `petmFoeMonsters`/`petmFoeNbMonstre`/
//     `petmFoeSelected`/`petmFoePosX`/`petmFoePosY`/`petmFoeIsMonster` for
//     `ctx->Adversaire`. Every real `Player`/`Monster` method became a
//     plain function taking an explicit `isFoe` (0=Joueur,1=Adversaire)
//     flag or a direct `PetmMonster*` pointer (address-of-array-element
//     pointers are a proven pattern in this project - see gameBomber.c's
//     own `&bombMasterBombe[i]`-style call sites). `IContexte`'s own real
//     `Monster`/`Dresseur`/`Joueur2`/`TypeAdversaire` fields and
//     `IsMaster` are all genuinely dead (confirmed via a full grep sweep of
//     every real .ino/.h file - never read anywhere, `IsMaster` is only
//     ever assigned `true` once and never checked) and were dropped
//     entirely, matching this project's own established "drop confirmed-
//     dead fields" precedent (see gameDarkShmup.c's own header comment for
//     the identical treatment of two dead `Bullet`/`StarShipPlayer`
//     fields). `Player::Remove()`/`ListeMonster()`/`Monster::IsFull()`
//     (the attack-slot-count check - `nbAttaque`/`maxAttaque` are never
//     actually incremented/used anywhere either) are likewise genuinely
//     dead and were not ported.
//
// DIALECT REWRITES: every real `gb.x.y(...)` call site became a plain
// `gbY(...)` call. `random(a,b)` (Arduino's own exclusive-upper-bound
// ranged form) became `a + arand(b-a)`; `random(0,n)`/`random(n)` became
// `arand(n)`. Real upstream bitmap byte tables were already plain `0x..`
// hex literals (not AVR `B`-binary literals) with one lone exception -
// `Fleche[]`'s own 5 rows *are* written as `B00100000`-style binary
// literals - converted to hex by hand (`0x20,0x60,0xFC,0x60,0x20`); every
// other bitmap's byte count was verified by script against its own
// declared `2 + ceil(width/8)*height` size before trusting it, not
// hand-transcribed. No ternary operator exists in this dialect - every
// real `a?b:c` (`GetWidthBarreVie`'s own trivial one, `ChoixMonsterDebut`'s
// wrap-around cursor pick, and one genuinely-broken one inside
// `CombatAttack`, see below) became explicit `if`/`else`. Real upstream's
// own `gb.menu(items, n)`/`display.textWrap` widgets have no equivalent in
// this shim - hand-rolled per this project's own established precedent
// (see gameConduit.c's own `condUpdateMenu()`): four small menus (main
// C-button menu, team-slot picker, attack picker, Yes/No), and
// `StartScene()`'s own real word-wrap was replaced with hand-inserted
// `\n` breaks in the same four sentences (this shim's `gbPrintString()`
// does support a real embedded `\n` line break, just not automatic
// wrapping). Real upstream's own accented `"électrique"` attack-type label
// was written as plain `"electrique"` - this shim's bitmap fonts only
// cover ASCII 0-127, with no glyph for the real accented character.
//
// UPSTREAM BUGS/QUIRKS - confirmed by tracing the real source directly,
// and preserved exactly (all of them are cosmetic/balance oddities, not
// crashes or hangs):
// - **`Monster::Type`'s own 0-5 numbering and `GetAttakByPatternNumero()`'s
//   own 0-5 switch use two DIFFERENT real orderings** (0=Feux,1=Eau,
//   2=Terre,3=Plante,4=Elec,5=Psy for the former, per
//   `getnumMonsterByNumZone()`'s own real comments in AllspriteBonus.ino;
//   0=Feux,1=Eau,2=Plante,3=Terre,4=Elec,5=Psy for the latter, per
//   AllAttaqueHelper.ino) - and `Monster::Type` is copied directly into
//   `numeroPattern` with no translation
//   (`monsterAgenerer->SetPatternAttaque(monsterAgenerer->Type)`), so a
//   real Terre-type monster silently fights with Plante's own attack/
//   type-effectiveness rules and vice versa. Preserved by transcribing
//   each real piece of source exactly as written and letting the two
//   numberings collide naturally, the same way real upstream does -
//   not "fixed" into a single consistent numbering.
// - **`CombatAttack()`'s own real onslaught-attack damage roll is dead
//   code by a misplaced paren**: `random((att->Force/2, att->Force +
//   att->Force/4) - def->Defence)` - the inner parens form a genuine C
//   comma expression, silently discarding `att->Force/2` and collapsing
//   to a single-argument `random(x)` call
//   (`arand(att->Force + att->Force/4 - def->Defence)` here) rather than
//   the apparently-intended two-argument `random(min,max) -
//   def->Defence`. Verified by hand-tracing the comma operator, not
//   assumed; preserved exactly.
// - **`HaveBonusAttak()`'s own real type-effectiveness lookup never
//   triggers for a Fire-pattern attack** (`numAttk>0` excludes the Fire
//   case's own `numAttk==0` outright) - a real, asymmetric quirk of the
//   type table, preserved via the same literal `numAttk>0 && numAttk<8`
//   guard upstream itself uses (the `<8` half of that guard is real but
//   unreachable in practice - traced every real call site and confirmed
//   `numAttaque` passed in is always 0/1/2, i.e. `numAttk` always lands in
//   -2..5, never 6/7, so the array is never actually read past its own
//   real 6-element bound despite the guard nominally allowing it).
// - **A genuinely fresh/empty team slot compares as "dead" instead of
//   real hardware's own uninitialized-memory value** - real `Monster`'s
//   own constructor never initializes `Vie` at all, so an unused
//   `Player::Monsters[]` slot holds real, non-deterministic garbage on
//   actual hardware (could even read as "alive"); this port's own struct
//   array is zero-initialized like every other global here, so an empty
//   slot deterministically reads `Vie<=0` ("dead") - matching the
//   sensible/intended behavior (the team-slot picker's own real do-while
//   re-prompts on a "dead" pick either way), not a replica of undefined
//   real-hardware memory contents.
// - **`DysplayFuturodex()`'s own real unseen-monster skip loop reads its
//   `monsterVue[]` bounds check AFTER the array access, not before**
//   (`while(!monsterVue[cptMonster] && cptMonster<Nb_MONSTERS)`) - a real,
//   if fairly benign, out-of-bounds read on real hardware once
//   `cptMonster` reaches exactly `Nb_MONSTERS`. Fixed here (bounds check
//   first) rather than preserved, since this dialect's own out-of-declared-
//   bounds array read behavior is unverified/could differ from real AVR's
//   own flat-memory-happens-to-be-harmless outcome - a genuine defensive
//   fix, not a gameplay change (every real call site already only reads
//   the corrected value the same way either way).
// - **A real, confirmed soft-lock, fixed rather than preserved**: pressing
//   A on the real Game Over screen calls `InitialisationGame()` (which
//   internally sets the dispatcher's own `state` global to 50, i.e.
//   `StartScene`) and then `GameOverScreen()` itself `return`s 0 - and
//   since the real top-level dispatcher does `state =
//   GameOverScreen();`, that `return 0` immediately overwrites the
//   `state=50` `InitialisationGame()` had just set one statement earlier.
//   The real, played-out consequence: the player drops straight back into
//   exploration with a freshly-cleared, completely EMPTY team (no starter
//   was ever re-picked) - and the next wild encounter's own team-select
//   menu do-while (`while(GetSelectedMonster()->Vie<=0)`) can then never
//   exit, since every one of the 4 slots is empty/"dead", a genuine
//   permanent soft-lock. This falls squarely under this project's own
//   explicit "don't preserve a real infinite loop / unplayable state"
//   exception - fixed by routing a Game Over restart to the real starter-
//   choice screen (`PETM_ST_START_SCENE`) instead, exactly what
//   `InitialisationGame()`'s own `state=50` line clearly intended before
//   being clobbered.
//
// SOUND: only one-shot tones exist here (`gbPlayOK`/`gbPlayCancel`/
// `gbPlayTick`/`gbPlayNote`) - real upstream never calls into
// `gb.sound.*` anywhere at all (confirmed via a full grep sweep), so
// this port adds no sound effects either, matching real upstream exactly.
//
// PERSISTENCE: real upstream has no `EEPROM.h` include and no save/load
// call anywhere - every run starts completely fresh (a brand new random
// world, a freshly-chosen starter), so no EEPROM persistence was wired up
// here either, matching real upstream exactly.
//
// No custom per-pixel bitmap-masking helper was needed anywhere in this
// port - every sprite here (monsters, world tiles, the player/trainer
// sprites, attack effects, bonus icons) is drawn with a single direct
// `gbDrawBitmap()`/`gbDrawBitmapRotated()` call, the shim's own already-
// optimized primitives, with no bitmap-vs-bitmap masking/compositing of
// any kind (real upstream itself only ever draws one opaque/transparent
// bitmap at a time here, never a fill-then-outline layered draw the way
// e.g. FlappyBirdo's pipes needed). No collision/hit-detection in this
// game depends on sprite facing/flip direction either - the only
// direction-aware check (`testGetDresseur()`/`TestGetBonus()`, "which
// adjacent tile is the player facing") is a plain coordinate offset by
// `directionPerso`, with no bounding box or bitmap involved at all.

#define PETM_WORLD_W 16
#define PETM_WORLD_H 16
#define PETM_NB_THEMES 4
#define PETM_NB_BONUS 3
#define PETM_NB_DRESSEUR_THEME 1
#define PETM_NB_MONSTERS 10

#define PETM_ST_START_SCENE 0
#define PETM_ST_CHOOSE_STARTER 1
#define PETM_ST_EXPLORATION 2
#define PETM_ST_MAINMENU 3
#define PETM_ST_TEAM_FLASH 4
#define PETM_ST_TEAM_SUMMARY 5
#define PETM_ST_DEX_SCAN 6
#define PETM_ST_DEX_SHOW 7
#define PETM_ST_COMBAT 8
#define PETM_ST_GAMEOVER 9

#define PETM_CP_INTRO 0
#define PETM_CP_FIRST_ARRIVE 1
#define PETM_CP_ROUND_ADV_GUARD 2
#define PETM_CP_FOE_REARRIVE 3
#define PETM_CP_PLAYER_GUARD 4
#define PETM_CP_PLAYER_MENU 5
#define PETM_CP_PLAYER_ARRIVE 6
#define PETM_CP_FOE_ATTACK 7
#define PETM_CP_PLAYER_ATTACK 8
#define PETM_CP_RESOLVE 9
#define PETM_CP_ANIM_A 10
#define PETM_CP_ANIM_B 11
#define PETM_CP_DEATH_CHECK 12
#define PETM_CP_ANIM_DEATH 13
#define PETM_CP_AFTERMATH 14
#define PETM_CP_CATCH_INFO 15
#define PETM_CP_CATCH_PROMPT 16
#define PETM_CP_CATCH_SWAP_SELECT 17
#define PETM_CP_LEVELUP_INFO 18
#define PETM_CP_ROUND_DECIDE 19
#define PETM_CP_END 20

// -----------------------------------------------------------------------------
// Bitmap data - extracted byte-for-byte from the real AllMonstersSprite.h/
// AllPersoSprite.h/AllWorldSprite.h/AllspriteBonus.ino/
// InterfaceCombatSprite.h/AllAttaqueHelper.ino/StartScene.ino (script-
// verified element counts against each one's own declared width/height).
// -----------------------------------------------------------------------------

int petmSprBigEyesBack[68] = { 24, 22,
  0xF, 0xE7, 0xF0, 0xF, 0xE7, 0xF0, 0xF, 0xE7, 0xF0, 0xF, 0xE7, 0xF0, 0xF, 0xE7, 0xF0, 0xF,
  0xE7, 0xF0, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0,
  0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0xF, 0xFF, 0xF0,
  0x18, 0x0, 0x1C, 0x39, 0xFF, 0x96, 0x69, 0xFF, 0x93, 0xC9, 0xFF, 0x91, 0x8F, 0xFF, 0xF1, 0xFE,
  0x0, 0x7F,
};

int petmSprBigEyesFront[68] = { 24, 22,
  0xF, 0xE7, 0xF0, 0x8, 0x24, 0x10, 0x9, 0x24, 0x10, 0x9, 0x25, 0x90, 0x8, 0x24, 0x10, 0xF,
  0xE7, 0xF0, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0,
  0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0x1, 0x0, 0x80, 0xF, 0xFF, 0xF0,
  0x18, 0x0, 0x1C, 0x38, 0x7E, 0x16, 0x68, 0x3C, 0x13, 0xC8, 0x0, 0x11, 0x8F, 0xFF, 0xF1, 0xFE,
  0x0, 0x7F,
};

int petmSprChampiDureBack[68] = { 24, 22,
  0x0, 0xFF, 0xC0, 0x7, 0xC8, 0xF8, 0x1E, 0x8, 0x3E, 0x3B, 0xFF, 0xF3, 0x77, 0xE2, 0x7B, 0x5C,
  0x22, 0xF, 0x4C, 0x22, 0xD, 0x5F, 0xE2, 0x79, 0x73, 0xE3, 0xED, 0x33, 0x63, 0x47, 0x1E, 0x23,
  0x2E, 0xF, 0xE1, 0x7C, 0x1, 0xE3, 0xE0, 0x1, 0xE1, 0xC0, 0x7, 0x80, 0x70, 0x8, 0x0, 0x18,
  0x8, 0x0, 0xC, 0x10, 0x7F, 0x6, 0x30, 0xE3, 0x86, 0x7F, 0xE3, 0xFF, 0xE7, 0xC1, 0xE7, 0x88,
  0x0, 0x11,
};

int petmSprChampiDureFront[68] = { 24, 22,
  0x0, 0xFF, 0xC0, 0x7, 0xC8, 0xF8, 0x1E, 0x8, 0x3E, 0x33, 0xFF, 0xF3, 0x77, 0xE3, 0xFB, 0x5C,
  0x0, 0xF, 0x4C, 0x73, 0x8D, 0x5F, 0x73, 0xB9, 0x73, 0x0, 0x3D, 0x33, 0x0, 0x47, 0x1E, 0x1E,
  0x2E, 0xF, 0x80, 0x7C, 0x1, 0xE3, 0xE0, 0x0, 0xE1, 0xC0, 0x7, 0x80, 0x70, 0x8, 0x0, 0x18,
  0x8, 0x0, 0xC, 0x10, 0x7F, 0x6, 0x30, 0xE3, 0x86, 0x7F, 0xE3, 0xFF, 0xE7, 0xC1, 0xE7, 0x88,
  0x0, 0x11,
};

int petmSprDalekomonBack[68] = { 24, 22,
  0x3, 0xF0, 0x60, 0xE, 0x1F, 0xC0, 0x18, 0xC, 0x60, 0x30, 0x4, 0x0, 0x20, 0x6, 0x0, 0x24,
  0x42, 0x0, 0x2E, 0xE2, 0xC, 0x24, 0x42, 0x38, 0x20, 0x1F, 0xF8, 0x20, 0x1, 0xC, 0x20, 0x1,
  0x80, 0x24, 0x44, 0xC0, 0x2E, 0xEE, 0x60, 0x24, 0x44, 0x30, 0x20, 0x0, 0x18, 0x20, 0x0, 0x4,
  0x7F, 0xFF, 0xFE, 0xD5, 0x55, 0x53, 0x80, 0x0, 0x1, 0xC0, 0x0, 0x3, 0x6A, 0xAA, 0xAE, 0x3F,
  0xFF, 0xFC,
};

int petmSprDalekomonFront[68] = { 24, 22,
  0x6, 0xF, 0xC0, 0x3, 0xF8, 0x70, 0x6, 0x30, 0x18, 0x0, 0x20, 0xC, 0x0, 0x60, 0x4, 0x0,
  0x42, 0x24, 0x30, 0x47, 0x74, 0x1C, 0x42, 0x24, 0x1F, 0xF8, 0x4, 0x30, 0x80, 0x4, 0x1, 0x80,
  0x4, 0x3, 0x22, 0x24, 0x6, 0x77, 0x74, 0xC, 0x22, 0x24, 0x18, 0x0, 0x4, 0x20, 0x0, 0x4,
  0x7F, 0xFF, 0xFE, 0xCA, 0xAA, 0xAB, 0x80, 0x0, 0x1, 0xC0, 0x0, 0x3, 0x75, 0x55, 0x56, 0x3F,
  0xFF, 0xFC,
};

int petmSprElectromignonBack[74] = { 24, 24,
  0x7F, 0x8F, 0xF0, 0x50, 0xA8, 0x50, 0x70, 0xA8, 0x70, 0x0, 0xF8, 0x0, 0x0, 0x84, 0xC, 0x81,
  0x2, 0x18, 0xC1, 0x2, 0x30, 0x60, 0x84, 0x20, 0x3B, 0x4B, 0x40, 0xC, 0x30, 0x80, 0x4, 0x0,
  0x80, 0x2, 0x69, 0x0, 0x3, 0x4B, 0x0, 0x4, 0x84, 0x80, 0x4, 0x48, 0x80, 0x2, 0x1, 0x0,
  0x3, 0x3, 0x0, 0x4, 0x78, 0x80, 0x4, 0x48, 0x80, 0x2, 0xCD, 0x0, 0x1, 0x4A, 0x0, 0x1,
  0x4A, 0x0, 0x1, 0x4A, 0x0, 0x1, 0xCE, 0x0,
};

int petmSprElectromignonFront[74] = { 24, 24,
  0x7F, 0x8F, 0xF0, 0x50, 0xA8, 0x50, 0x70, 0xA8, 0x70, 0x0, 0xF8, 0x0, 0x0, 0x84, 0x0, 0x81,
  0x4A, 0x4, 0xC1, 0x2, 0x8, 0x60, 0xB4, 0x18, 0x33, 0x4B, 0x60, 0xC, 0x30, 0x80, 0x4, 0x0,
  0x80, 0x2, 0x49, 0x0, 0x2, 0xB5, 0x0, 0x4, 0x84, 0x80, 0x4, 0x48, 0x80, 0x2, 0x31, 0x0,
  0x3, 0x3, 0x0, 0x4, 0x78, 0x80, 0x4, 0x48, 0x80, 0x2, 0xCD, 0x0, 0x1, 0x4A, 0x0, 0x1,
  0x4A, 0x0, 0x1, 0x4A, 0x0, 0x1, 0xCE, 0x0,
};

int petmSprFlottilleBack[68] = { 24, 22,
  0x0, 0x30, 0x0, 0x0, 0x3C, 0x0, 0xE0, 0x27, 0x0, 0x98, 0x20, 0x80, 0x8E, 0x23, 0xE0, 0x83,
  0x3C, 0x38, 0x91, 0xD8, 0x3C, 0x98, 0xF0, 0x1E, 0x4C, 0xB0, 0x13, 0x64, 0xC0, 0x1F, 0x26, 0x60,
  0xD, 0x22, 0x20, 0x3, 0x22, 0x67, 0xE2, 0x62, 0xEC, 0x62, 0x46, 0x88, 0xC4, 0x4C, 0xC9, 0x84,
  0x88, 0xE7, 0xC, 0x99, 0xB0, 0x38, 0x9B, 0x98, 0xF0, 0x8E, 0xF, 0x80, 0xB8, 0x0, 0x0, 0xE0,
  0x0, 0x0,
};

int petmSprFlottilleFront[68] = { 24, 22,
  0x0, 0xC, 0x0, 0x0, 0x3C, 0x0, 0x0, 0xE4, 0x7, 0x1, 0x4, 0x19, 0x7, 0xC4, 0x71, 0x1C,
  0x3C, 0xC1, 0x3C, 0x1B, 0x89, 0x70, 0xF, 0x19, 0xC6, 0xD, 0x32, 0x8A, 0x3, 0x26, 0x86, 0x6,
  0x64, 0xF0, 0x34, 0x44, 0xD8, 0x56, 0x44, 0x88, 0x97, 0x46, 0x89, 0x11, 0x62, 0x99, 0xF3, 0x32,
  0xF0, 0x7, 0x11, 0xC0, 0xD, 0x99, 0x7F, 0x19, 0xD9, 0x1D, 0xF0, 0x71, 0x0, 0x0, 0x1D, 0x0,
  0x0, 0x7,
};

int petmSprLapiDodoBack[62] = { 24, 20,
  0x3C, 0x7, 0x80, 0x24, 0x4, 0x80, 0x42, 0x8, 0x40, 0x5A, 0xB, 0x40, 0x5A, 0xB, 0x40, 0x5A,
  0xEB, 0x40, 0x5B, 0x1B, 0x40, 0x58, 0x3, 0x40, 0x46, 0xC, 0x40, 0x4C, 0xE6, 0x40, 0x48, 0x2,
  0x40, 0x40, 0x0, 0x40, 0x3F, 0xFF, 0x80, 0x62, 0x50, 0xE0, 0x9A, 0x53, 0x90, 0xCE, 0x5E, 0x30,
  0x86, 0x58, 0x10, 0xC0, 0x0, 0x30, 0x80, 0xE0, 0x10, 0xFF, 0x1F, 0xF0,
};

int petmSprLapiDodoFront[62] = { 24, 20,
  0x3C, 0x7, 0x80, 0x24, 0x4, 0x80, 0x42, 0x8, 0x40, 0x5A, 0xB, 0x40, 0x5A, 0xB, 0x40, 0x42,
  0xE8, 0x40, 0x43, 0x18, 0x40, 0x5E, 0xF, 0x40, 0x4A, 0xA, 0x40, 0x4A, 0xA, 0x40, 0x4F, 0x1E,
  0x40, 0x40, 0x0, 0x40, 0x3F, 0xFF, 0x80, 0x62, 0x50, 0xE0, 0x9A, 0x53, 0x90, 0xCE, 0x5E, 0x30,
  0x86, 0x58, 0x10, 0xC0, 0x0, 0x30, 0x80, 0xE0, 0x10, 0xFF, 0x1F, 0xF0,
};

int petmSprOctoSpaceBack[65] = { 24, 21,
  0xC0, 0x0, 0x18, 0xC0, 0x0, 0x38, 0x39, 0xFE, 0xE0, 0xF, 0x3, 0x80, 0x8, 0x78, 0x80, 0x18,
  0x0, 0xC0, 0x31, 0xFE, 0x60, 0x20, 0x0, 0x30, 0x23, 0xFF, 0x10, 0x20, 0x0, 0x10, 0x27, 0xFF,
  0x90, 0x20, 0x0, 0x10, 0x3F, 0xFF, 0xF0, 0x8, 0x94, 0x40, 0x8, 0xB6, 0x60, 0x19, 0xA2, 0x20,
  0x11, 0x33, 0x20, 0x21, 0x11, 0x30, 0x63, 0x31, 0x90, 0x42, 0x20, 0x90, 0xC6, 0x30, 0xD8,
};

int petmSprOctoSpaceFront[65] = { 24, 21,
  0xC0, 0x0, 0x18, 0xC0, 0x0, 0x38, 0x39, 0xFE, 0xE0, 0xF, 0x3, 0x80, 0x8, 0x0, 0x80, 0x19,
  0xFC, 0xC0, 0x30, 0xF8, 0x60, 0x20, 0x70, 0x30, 0x20, 0x20, 0x10, 0x20, 0x0, 0x10, 0x27, 0xFF,
  0x90, 0x21, 0x32, 0x10, 0x3F, 0xFF, 0xF0, 0x8, 0x94, 0x40, 0x8, 0xB6, 0x60, 0x19, 0xA2, 0x20,
  0x11, 0x33, 0x20, 0x21, 0x11, 0x30, 0x63, 0x31, 0x90, 0x42, 0x20, 0x90, 0xC6, 0x30, 0xD8,
};

int petmSprPolsanBack[74] = { 24, 24,
  0x7, 0x38, 0x0, 0x5, 0x28, 0x0, 0x7, 0x38, 0x0, 0x2, 0x10, 0x0, 0x2, 0x10, 0x0, 0x2,
  0x10, 0x0, 0x3, 0xF8, 0x0, 0x7, 0x5C, 0x0, 0x5, 0x54, 0x0, 0x5, 0x54, 0x0, 0x5, 0x54,
  0x0, 0x3F, 0xFF, 0x80, 0x40, 0x0, 0x40, 0x3F, 0xFF, 0x80, 0x10, 0x1, 0x0, 0x30, 0xA1, 0x80,
  0x60, 0x40, 0xC0, 0x40, 0xA0, 0x40, 0x40, 0x0, 0x40, 0x40, 0x0, 0x40, 0xD4, 0x5, 0x60, 0x88,
  0x2, 0x20, 0x94, 0x5, 0x20, 0x80, 0x0, 0x20,
};

int petmSprPolsanFront[74] = { 24, 24,
  0x7, 0x38, 0x0, 0x5, 0x28, 0x0, 0x7, 0x38, 0x0, 0x2, 0x10, 0x0, 0x2, 0x10, 0x0, 0x2,
  0x10, 0x0, 0x3, 0xF8, 0x0, 0x6, 0xC, 0x0, 0x4, 0xA4, 0x0, 0x4, 0x4, 0x0, 0x4, 0x4,
  0x0, 0x3F, 0xFF, 0x80, 0x40, 0x0, 0x40, 0x3F, 0xFF, 0x80, 0x10, 0x1, 0x0, 0x30, 0xA1, 0x80,
  0x60, 0x40, 0xC0, 0x40, 0xA0, 0x40, 0x40, 0x0, 0x40, 0x40, 0xA0, 0x40, 0xC0, 0x40, 0x60, 0x80,
  0xA0, 0x20, 0x80, 0x0, 0x20, 0xFF, 0xFF, 0xE0,
};

int petmSprRacinoideBack[42] = { 16, 20,
  0x5, 0x0, 0xA, 0x80, 0x8, 0x80, 0x5, 0x0, 0x2, 0x0, 0x1F, 0x80, 0x30, 0xF0, 0x25, 0x10,
  0x28, 0x50, 0x28, 0x50, 0x24, 0xA0, 0x30, 0xA0, 0x15, 0x20, 0x1A, 0x60, 0xD, 0x40, 0x4, 0xC0,
  0x6, 0xB8, 0x7D, 0xEC, 0xC5, 0x44, 0x89, 0x24,
};

int petmSprRacinoideFront[42] = { 16, 20,
  0x5, 0x0, 0xA, 0x80, 0x8, 0x80, 0x5, 0x0, 0x2, 0x0, 0x1F, 0x80, 0x30, 0xF0, 0x20, 0x10,
  0x2D, 0x90, 0x20, 0x10, 0x22, 0x20, 0x32, 0x20, 0x10, 0x20, 0x18, 0x60, 0xC, 0x40, 0x4, 0xC0,
  0x6, 0xB8, 0x7D, 0xEC, 0xC5, 0x44, 0x89, 0x24,
};

int petmSprSaloeoyeBack[74] = { 24, 24,
  0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAB, 0xFA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAB,
  0xFA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF,
  0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0,
  0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xFF,
  0xFF, 0xE0, 0x12, 0x9, 0x0, 0x12, 0x9, 0x0,
};

int petmSprSaloeoyeFront[74] = { 24, 24,
  0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xAB, 0x1A, 0xA0, 0xFF, 0x5F, 0xE0, 0xAB,
  0x1A, 0xA0, 0xFF, 0xFF, 0xE0, 0xAA, 0xAA, 0xA0, 0xFF, 0xFF, 0xE0, 0xA0, 0x0, 0xA0, 0xE7, 0xFC,
  0xE0, 0xA6, 0xAC, 0xA0, 0xE5, 0xF4, 0xE0, 0xA7, 0x1C, 0xA0, 0xE5, 0x14, 0xE0, 0xA7, 0x1C, 0xA0,
  0xE5, 0x14, 0xE0, 0xA7, 0x1C, 0xA0, 0xE0, 0x0, 0xE0, 0xBF, 0xFF, 0xA0, 0xD5, 0x55, 0x60, 0xFF,
  0xFF, 0xE0, 0x12, 0x9, 0x0, 0x12, 0x9, 0x0,
};

int petmSprBlockElec[10] = { 8, 8, 0xDB, 0xC3, 0xC3, 0x0, 0x0, 0xC3, 0xC3, 0xDB, };
int petmSprBlockFutur[10] = { 8, 8, 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF, };
int petmSprBlockSapce[10] = { 8, 8, 0xBD, 0x0, 0x81, 0x99, 0x99, 0x81, 0x0, 0xBD, };
int petmSprCapsule[10] = { 8, 8, 0x0, 0x18, 0x3C, 0x4A, 0x52, 0x3C, 0x18, 0x0, };
int petmSprmedikit[10] = { 8, 8, 0x0, 0x7E, 0x66, 0x42, 0x42, 0x66, 0x7E, 0x0, };
int petmSprEntreeElec[10] = { 8, 8, 0x8, 0x14, 0x22, 0x41, 0x8, 0x14, 0x22, 0x41, };
int petmSprEntreeFutur[10] = { 8, 8, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, };
int petmSprEntreeSapce[10] = { 8, 8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, };
int petmSprFleure[10] = { 8, 8, 0x2, 0x5, 0x2, 0x0, 0x20, 0x50, 0x20, 0x0, };
int petmSprherbe[10] = { 8, 8, 0x0, 0x0, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x42, };
int petmSprRoche[10] = { 8, 8, 0x3C, 0x26, 0xDB, 0xA5, 0xAD, 0x93, 0x44, 0x3C, };
int petmSprterre[10] = { 8, 8, 0x0, 0x2, 0x20, 0x0, 0x0, 0x2, 0x40, 0x0, };
int petmSprSol1Elec[10] = { 8, 8, 0x0, 0x10, 0x10, 0xE, 0x10, 0x60, 0x10, 0x10, };
int petmSprSol1Futur[10] = { 8, 8, 0x10, 0x20, 0x40, 0x80, 0x0, 0x1, 0x2, 0x4, };
int petmSprSol1Sapce[10] = { 8, 8, 0x0, 0x0, 0x4, 0x0, 0x0, 0x0, 0x0, 0x0, };
int petmSprSol2Elec[10] = { 8, 8, 0x0, 0x28, 0x4, 0x0, 0x0, 0x2, 0x44, 0x0, };
int petmSprSol2Futur[10] = { 8, 8, 0x0, 0x40, 0x20, 0x18, 0x18, 0x4, 0x2, 0x0, };
int petmSprSol2Sapce[10] = { 8, 8, 0x0, 0x0, 0x20, 0x0, 0x0, 0x0, 0x8, 0x0, };

int petmSprPersoArretBack[10] = { 8, 8, 0xFC, 0x7C, 0x7C, 0x10, 0x7C, 0x10, 0x28, 0x44, };
int petmSprPersoArretDroite[10] = { 8, 8, 0x3E, 0x34, 0x3C, 0x10, 0x1C, 0x10, 0x10, 0x18, };
int petmSprPersoArretFront[10] = { 8, 8, 0x7E, 0x54, 0x7C, 0x10, 0x7C, 0x10, 0x28, 0x44, };
int petmSprPersoArretGauche[10] = { 8, 8, 0x7C, 0x2C, 0x3C, 0x8, 0x38, 0x8, 0x8, 0x18, };
int petmSprPersoPas1Back[10] = { 8, 8, 0xFC, 0x7C, 0x7C, 0x10, 0x7C, 0x10, 0x28, 0x20, };
int petmSprPersoPas1Droite[10] = { 8, 8, 0x3E, 0x34, 0x3C, 0x10, 0x30, 0x50, 0x28, 0x44, };
int petmSprPersoPas1Front[10] = { 8, 8, 0x7E, 0x54, 0x7C, 0x10, 0x7C, 0x10, 0x28, 0x20, };
int petmSprPersoPas1Gauche[10] = { 8, 8, 0x7C, 0x2C, 0x3C, 0x8, 0xC, 0xA, 0x14, 0x22, };
int petmSprPersoPas2Back[10] = { 8, 8, 0xFC, 0x7C, 0x7C, 0x10, 0x7C, 0x10, 0x28, 0x8, };
int petmSprPersoPas2Droite[10] = { 8, 8, 0x3E, 0x34, 0x3C, 0x14, 0x18, 0x10, 0x18, 0x14, };
int petmSprPersoPas2Front[10] = { 8, 8, 0x7E, 0x54, 0x7C, 0x10, 0x7C, 0x10, 0x28, 0x8, };
int petmSprPersoPas2Gauche[10] = { 8, 8, 0x7C, 0x2C, 0x3C, 0x28, 0x18, 0x8, 0x18, 0x28, };
int petmSprDresseur1[10] = { 8, 8, 0x7E, 0xFF, 0x5A, 0x7E, 0x18, 0x7E, 0x3C, 0x66, };
int petmSprDresseur2[10] = { 8, 8, 0x7E, 0xFF, 0xDB, 0xFF, 0x99, 0x3C, 0x7E, 0x24, };

int petmSprsprBareVie[44] = { 48, 7,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xA8, 0x80, 0x0, 0x2, 0x0,
  0x0, 0x8, 0x80, 0x0, 0x0, 0x0, 0x0, 0x8, 0x80, 0x20, 0x0, 0x0, 0x20, 0x8, 0xAA, 0xAA,
  0xAA, 0xAA, 0xAA, 0xA8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8,
};

// Real upstream's own only binary-literal ("B..."-style) bitmap - converted
// to hex by hand (B00100000=0x20, B01100000=0x60, B11111100=0xFC).
int petmSprFleche[7] = { 8, 5, 0x20, 0x60, 0xFC, 0x60, 0x20, };

int petmSprattaqueGriffe[26] = { 16, 12,
  0x0, 0x0, 0xF, 0x80, 0x38, 0x0, 0x60, 0x20, 0x41, 0xE0, 0xC7, 0x0, 0x98, 0x0, 0x30, 0x70,
  0x21, 0xC0, 0x23, 0x0, 0x6, 0x0, 0x4, 0x0,
};
int petmSprCatchThemAll[34] = { 16, 16,
  0x3F, 0xF8, 0x20, 0x0, 0xAF, 0xFC, 0xA0, 0x4, 0xA0, 0x74, 0xA8, 0x14, 0xAB, 0xD4, 0xAA, 0x14,
  0xAA, 0x54, 0x88, 0xD5, 0x88, 0x11, 0x8F, 0xD1, 0x80, 0x5, 0xFF, 0xD, 0x0, 0x1, 0xF, 0xFF,
};
int petmSprCharge[5] = { 8, 3, 0x7F, 0x0, 0xFE, };
int petmSprElectrique[34] = { 16, 16,
  0x0, 0x80, 0x21, 0x0, 0x30, 0x84, 0x19, 0x82, 0x8F, 0xE4, 0x4C, 0x38, 0x28, 0x12, 0x19, 0x9D,
  0x19, 0x98, 0x58, 0x10, 0xAC, 0x3C, 0x7, 0xE2, 0x5, 0x85, 0x18, 0x80, 0x20, 0x40, 0x10, 0x80,
};
int petmSprFeuille[10] = { 8, 8, 0x1D, 0x26, 0x49, 0x91, 0xA6, 0xC8, 0xB0, 0xC0, };
int petmSprFeux[16] = { 8, 14, 0xE0, 0x70, 0x38, 0x1C, 0x16, 0x12, 0x2A, 0x4B, 0x55, 0xA5, 0x95, 0x4A, 0x24, 0x18, };
int petmSprGoutteDeau[6] = { 8, 4, 0x40, 0x60, 0x90, 0xE0, };
int petmSprTerre[12] = { 16, 5, 0xC0, 0x0, 0x38, 0xE7, 0x85, 0x2, 0x72, 0x79, 0x4, 0x4, };
int petmSprpsy[34] = { 16, 16,
  0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x3, 0x80, 0x2, 0x80,
  0x2, 0x80, 0x4, 0x40, 0x8, 0x20, 0x1F, 0xF0, 0x18, 0x38, 0x20, 0x4, 0x40, 0x2, 0x80, 0x1,
};

// -----------------------------------------------------------------------------
// Sprite lookup - direct ports of the real GetSpriteMonsterByNumero()/
// GetSpriteBonusByNumero() switch functions, plus the real `sprites[]`/
// `spritesPerso[]`/`spritesDresseur[]` pointer tables.
// -----------------------------------------------------------------------------

int* petmGetMonsterSprite( int num, bool isFront )
{
    if( num == 0 ) { if( isFront ) return petmSprBigEyesFront; return petmSprBigEyesBack; }
    if( num == 1 ) { if( isFront ) return petmSprChampiDureFront; return petmSprChampiDureBack; }
    if( num == 2 ) { if( isFront ) return petmSprDalekomonFront; return petmSprDalekomonBack; }
    if( num == 3 ) { if( isFront ) return petmSprElectromignonFront; return petmSprElectromignonBack; }
    if( num == 4 ) { if( isFront ) return petmSprFlottilleFront; return petmSprFlottilleBack; }
    if( num == 5 ) { if( isFront ) return petmSprLapiDodoFront; return petmSprLapiDodoBack; }
    if( num == 6 ) { if( isFront ) return petmSprOctoSpaceFront; return petmSprOctoSpaceBack; }
    if( num == 7 ) { if( isFront ) return petmSprPolsanFront; return petmSprPolsanBack; }
    if( num == 8 ) { if( isFront ) return petmSprRacinoideFront; return petmSprRacinoideBack; }
    if( isFront ) return petmSprSaloeoyeFront; return petmSprSaloeoyeBack; // num == 9
}

int* petmGetBonusSprite( int num )
{
    if( num == 3 ) return petmSprmedikit;
    return petmSprCapsule;
}

int* petmWorldSprites[16] = {
    petmSprSol1Futur, petmSprSol2Futur, petmSprEntreeFutur, petmSprBlockFutur,
    petmSprFleure, petmSprterre, petmSprherbe, petmSprRoche,
    petmSprSol1Sapce, petmSprSol2Sapce, petmSprEntreeSapce, petmSprBlockSapce,
    petmSprSol1Elec, petmSprSol2Elec, petmSprEntreeElec, petmSprBlockElec };

int* petmSpritesPerso[12] = {
    petmSprPersoArretFront, petmSprPersoArretDroite, petmSprPersoArretBack, petmSprPersoArretGauche,
    petmSprPersoPas1Front, petmSprPersoPas2Front, petmSprPersoPas1Droite, petmSprPersoPas2Droite,
    petmSprPersoPas1Back, petmSprPersoPas2Back, petmSprPersoPas1Gauche, petmSprPersoPas2Gauche };

int* petmSpritesDresseur[2] = { petmSprDresseur1, petmSprDresseur2 };

// -----------------------------------------------------------------------------
// Monster/Player data
// -----------------------------------------------------------------------------

typedef struct{
    int Vie;
    int Force;
    int Defence;
    int Vitesse;
    int VieMax;
    int OldVie;
    int Niveau;
    int NextNiveau;
    int Xp;
    int Type;
    int Numero;
    int NumeroPattern;
    int SelectedAttaque; // 255 = none
} PetmMonster;

PetmMonster petmPlayerMonsters[4];
int petmPlayerNbMonstre;
int petmPlayerSelected; // 255 = none

PetmMonster petmFoeMonsters[4];
int petmFoeNbMonstre;
int petmFoeSelected; // 255 = none
int petmFoePosX;
int petmFoePosY;
bool petmFoeIsMonster;

PetmMonster* petmGetMonster( int isFoe, int index )
{
    if( isFoe ) return &petmFoeMonsters[ index ];
    return &petmPlayerMonsters[ index ];
}

int petmNbMonstre( int isFoe )
{
    if( isFoe ) return petmFoeNbMonstre;
    return petmPlayerNbMonstre;
}

bool petmIsSelected( int isFoe )
{
    if( isFoe ) return petmFoeSelected < 255;
    return petmPlayerSelected < 255;
}

void petmSelectMonster( int isFoe, int num )
{
    if( isFoe ) petmFoeSelected = num;
    else petmPlayerSelected = num;
}

void petmUnselectMonster( int isFoe )
{
    petmSelectMonster( isFoe, 255 );
}

PetmMonster* petmGetSelected( int isFoe )
{
    if( isFoe ) return petmGetMonster( 1, petmFoeSelected );
    return petmGetMonster( 0, petmPlayerSelected );
}

bool petmIsFull( int isFoe )
{
    return petmNbMonstre( isFoe ) >= 4;
}

bool petmIsAlive( PetmMonster* m )
{
    return m->Vie > 0;
}

bool petmHaveMonsterOk( int isFoe )
{
    int n = petmNbMonstre( isFoe );
    int i = 0;
    while( i < n )
    {
        if( petmIsAlive( petmGetMonster( isFoe, i ) ) ) return true;
        i = i + 1;
    }
    return false;
}

void petmClearMonster( int isFoe )
{
    if( isFoe ) petmFoeNbMonstre = 0;
    else petmPlayerNbMonstre = 0;
}

// Real upstream's own `Player::AddMonster()` is always called with every
// field zeroed (`AddMonster(0,0,0,0,0,0,0,0)`, every real call site -
// confirmed via a full grep sweep) and the real fields are filled in
// afterward via `GetMonster(pos)` - so this port only ports the "append a
// fresh slot" half, returning its new index directly.
int petmAddEmptyMonster( int isFoe )
{
    if( petmIsFull( isFoe ) ) return -1;
    if( isFoe )
    {
        petmFoeNbMonstre = petmFoeNbMonstre + 1;
        return petmFoeNbMonstre - 1;
    }
    petmPlayerNbMonstre = petmPlayerNbMonstre + 1;
    return petmPlayerNbMonstre - 1;
}

int petmGetPourcentVieRestant( PetmMonster* m )
{
    return m->OldVie * 100 / m->VieMax;
}

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

void petmPrintlnStr( char* s )
{
    gbPrintString( s );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

void petmPrintlnNum( int v )
{
    gbPrintNumber( v );
    gbCursorY = gbCursorY + gbFontSize * gbFontHeight;
    gbCursorX = 0;
}

int petmWrap( int v, int n )
{
    if( v < 0 ) return v + n;
    if( v >= n ) return v - n;
    return v;
}

int petmWrapRdm0N( int n )
{
    return arand( n );
}

// Real Buttons::repeat()-adjacent RNG helper: `random(-2,2)`.
int petmWrapRdm()
{
    return -2 + arand( 4 );
}

// Real `TirrageDes(uint8_t maximun) { return random(1,maximun); }` -
// `arand()` already returns 0 for a non-positive argument, matching real
// Arduino `random(x)`'s own behavior for `x<=0` exactly (no special-casing
// needed even though this is called with an already-clamped-non-positive
// `dmg` sometimes - see `petmCombatAttack()` below).
int petmTirageDes( int maximun )
{
    return 1 + arand( maximun - 1 );
}

// -----------------------------------------------------------------------------
// Monster generation - direct ports of MonsterHelper.ino.
// -----------------------------------------------------------------------------

void petmGetNumMonsterByZone( int numZone, PetmMonster* m )
{
    int rdm2 = arand( 2 );
    if( numZone == 0 )
    {
        if( rdm2 == 0 ) m->Numero = 7; else m->Numero = 9; // Typepsy={7,9}
        m->Type = 5;
    }
    else if( numZone == 1 )
    {
        if( rdm2 == 0 ) { m->Numero = 3; m->Type = 4; } // TypeElec[0]
        else { m->Numero = 5; m->Type = 0; } // TypeFeux[0]
    }
    else if( numZone == 2 )
    {
        if( rdm2 == 0 ) { m->Numero = 0; m->Type = 2; } // Typeterre[0]
        else { m->Numero = 4; m->Type = 1; } // TypeEau[0]
    }
    else if( numZone == 3 )
    {
        if( rdm2 == 0 ) { m->Numero = 0; m->Type = 2; } // Typeterre[0]
        else { m->Numero = 1; m->Type = 3; } // TypePlante[1]
    }
    else if( numZone == 4 )
    {
        if( rdm2 == 0 ) { m->Numero = 4; m->Type = 1; }
        else { m->Numero = 5; m->Type = 0; }
    }
    else if( numZone == 5 )
    {
        if( rdm2 == 0 ) m->Numero = 3; else m->Numero = 2; // TypeElec={3,2}
        m->Type = 4;
    }
}

void petmGenerateMonsterByLvlAndZone( PetmMonster* m, int lvl, int numeroZone )
{
    if( lvl < 2 ) lvl = 2;
    m->Force = 2 * lvl + petmWrapRdm();
    m->Vie = 2 * lvl + petmWrapRdm();
    m->VieMax = m->Vie;
    m->OldVie = m->Vie;
    m->Vitesse = 2 * lvl + petmWrapRdm();
    m->Defence = 2 * lvl + petmWrapRdm();
    m->Niveau = lvl;
    m->NextNiveau = ( lvl * lvl ) / 2;
    m->Xp = 0;
    petmGetNumMonsterByZone( numeroZone, m );
    m->NumeroPattern = m->Type;
}

void petmGenerateStartMonster( PetmMonster* m, int numero )
{
    m->Numero = numero;
    if( numero == 0 ) m->Force = 20; else m->Force = 18;
    if( numero == 1 ) m->Vie = 20; else m->Vie = 18;
    m->VieMax = m->Vie;
    m->OldVie = m->Vie;
    m->Vitesse = 18;
    if( numero == 2 ) m->Defence = 20; else m->Defence = 18;
    m->Niveau = 5;
    m->NextNiveau = 12;
    m->Xp = 0;
    m->Type = numero + 2;
    m->NumeroPattern = m->Type;
}

void petmLevelUpMonster( PetmMonster* m )
{
    int rdm = 1 + arand( 2 ); // random(1,3)
    m->Force = m->Force + rdm;
    m->VieMax = m->VieMax + rdm;
    m->Vie = m->VieMax;
    m->OldVie = m->VieMax;
    m->Vitesse = m->Vitesse + rdm;
    m->Defence = m->Defence + rdm;
    m->Niveau = m->Niveau + 1;
    m->Xp = m->Xp - m->NextNiveau;
    m->NextNiveau = ( m->Niveau * m->Niveau ) / 2;
}

void petmCopyCaughtMonster( int isFoeSrc, int isFoeDst, int dstIndex )
{
    PetmMonster* src = petmGetSelected( isFoeSrc );
    PetmMonster* dst = petmGetMonster( isFoeDst, dstIndex );
    dst->Numero = src->Numero;
    dst->Force = src->Force;
    dst->Vie = src->VieMax; // real upstream: caught monster heals to full
    dst->VieMax = src->VieMax;
    dst->OldVie = src->VieMax;
    dst->Vitesse = src->Vitesse;
    dst->Defence = src->Defence;
    dst->Niveau = src->Niveau;
    dst->NextNiveau = src->NextNiveau;
    dst->Xp = src->Xp;
    dst->Type = src->Type;
    dst->NumeroPattern = src->NumeroPattern;
}

// -----------------------------------------------------------------------------
// Attack-type helpers - direct ports of AllAttaqueHelper.ino.
// -----------------------------------------------------------------------------

int petmAttaqueNumeroByPattern( int pattern, int idx )
{
    if( idx == 0 ) return 0; // claw
    if( idx == 1 ) return 1; // onslaught
    if( idx == 3 ) return 8; // "->" change monster
    if( idx == 4 ) return 9; // catch
    // idx == 2: elemental, depends on pattern
    if( pattern == 0 ) return 2; // Feux
    if( pattern == 1 ) return 3; // Eau
    if( pattern == 2 ) return 5; // Plante
    if( pattern == 3 ) return 4; // Terre
    if( pattern == 4 ) return 6; // Elec
    return 7; // Psy
}

int petmTypeIsFaibleAttk( int idx )
{
    if( idx == 0 ) return 3; // Feux
    if( idx == 1 ) return 0; // Eau
    if( idx == 2 ) return 5; // Plante
    if( idx == 3 ) return 4; // Terre
    if( idx == 4 ) return 1; // Elec
    return 2; // Psy
}

int petmHaveBonusAttak( int numAttaque, int numPattern, int typeDef )
{
    int numAttk = petmAttaqueNumeroByPattern( numPattern, numAttaque ) - 2;
    if( numAttk > 0 && numAttk < 8 )
    {
        if( petmTypeIsFaibleAttk( numAttk ) == typeDef ) return 1;
        if( petmTypeIsFaibleAttk( typeDef ) == numAttk ) return -1;
        if( typeDef == numAttk ) return -1;
    }
    return 0;
}

void petmCombatAttack( PetmMonster* att, PetmMonster* def )
{
    def->OldVie = def->Vie;
    int dmg;
    if( att->SelectedAttaque == 1 )
    {
        // Real upstream's own genuine bug - see this file's own header
        // comment ("CombatAttack()'s own real onslaught-attack damage roll
        // is dead code by a misplaced paren").
        dmg = arand( att->Force + att->Force / 4 - def->Defence );
    }
    else
    {
        dmg = att->Force - def->Defence;
    }
    dmg = dmg + petmTirageDes( dmg ) * petmHaveBonusAttak( att->SelectedAttaque, att->NumeroPattern, def->Type );
    if( dmg <= 0 ) dmg = 1;
    def->Vie = def->Vie - dmg;
    if( def->Vie < 0 ) def->Vie = 0;
}

void petmDecrementOldVie( PetmMonster* m, int nbframe )
{
    m->OldVie = m->OldVie - ( m->OldVie - m->Vie ) / nbframe;
    if( m->OldVie < m->Vie ) m->OldVie = m->Vie;
}

// -----------------------------------------------------------------------------
// Attack visual effects - direct ports of AllAttaqueHelper.ino's own
// AttaqueXxx() functions.
// -----------------------------------------------------------------------------

int petmAtkOffsetX;
int petmAtkOffsetY;

void petmAttaqueGriffe( bool isP1, int nbFrame )
{
    if( nbFrame <= 30 ) return;
    int nbframe1 = 45 - nbFrame;
    gbSetColor( GB_INVERT );
    if( isP1 ) gbDrawBitmap( 58 + nbframe1, 10 - nbframe1, petmSprattaqueGriffe );
    else gbDrawBitmap( 30 - nbframe1, 18 + nbframe1, petmSprattaqueGriffe );
    gbSetColor( GB_BLACK );
}

void petmAttaqueCharge( bool isP1, int nbFrame )
{
    if( nbFrame <= 30 ) return;
    gbSetColor( GB_INVERT );
    if( isP1 )
    {
        int nbframe1 = ( 45 - nbFrame ) * 2;
        gbDrawBitmapRotated( 60 + nbframe1, 2, petmSprCharge, 0, 0 );
        gbDrawBitmapRotated( 70 + nbframe1, 10, petmSprCharge, 0, 0 );
        gbDrawBitmapRotated( 65 + nbframe1, 20, petmSprCharge, 0, 0 );
    }
    else
    {
        int nbframe1 = ( nbFrame - 45 ) * 2;
        gbDrawBitmapRotated( 25 + nbframe1, 28, petmSprCharge, 0, 0 );
        gbDrawBitmapRotated( 15 + nbframe1, 36, petmSprCharge, 0, 0 );
        gbDrawBitmapRotated( 20 + nbframe1, 40, petmSprCharge, 0, 0 );
    }
    gbSetColor( GB_BLACK );
}

void petmAttaqueEau( bool isP1, int nbFrame )
{
    if( nbFrame <= 30 ) return;
    int nbframe1 = 45 - nbFrame;
    gbSetColor( GB_INVERT );
    int cptGoutte = 0;
    while( cptGoutte < nbframe1 )
    {
        if( cptGoutte % 2 == 0 )
        {
            if( isP1 ) gbDrawBitmapRotated( 65 - 9 + arand( 19 ), 10 - 9 + arand( 19 ), petmSprGoutteDeau, 0, 0 );
            else gbDrawBitmapRotated( 10 - 9 + arand( 19 ), 28 - 9 + arand( 19 ), petmSprGoutteDeau, 0, 0 );
        }
        cptGoutte = cptGoutte + 1;
    }
    gbSetColor( GB_BLACK );
}

void petmAttaqueFeux( bool isP1, int nbFrame )
{
    if( nbFrame % 4 )
    {
        petmAtkOffsetX = -4 + arand( 8 );
        petmAtkOffsetY = -4 + arand( 8 );
    }
    if( nbFrame <= 30 ) return;
    gbSetColor( GB_INVERT );
    if( isP1 ) gbDrawBitmapRotated( 65 + petmAtkOffsetX, 5 + petmAtkOffsetY, petmSprFeux, 0, 0 );
    else gbDrawBitmapRotated( 5 + petmAtkOffsetX, 35 + petmAtkOffsetY, petmSprFeux, 0, 0 );
    gbSetColor( GB_BLACK );
}

void petmAttaqueTerre( bool isP1, int nbFrame )
{
    if( nbFrame <= 30 ) return;
    gbSetColor( GB_INVERT );
    if( isP1 )
    {
        int nbframe1 = ( 45 - nbFrame ) * 2;
        gbDrawBitmapRotated( 60 + nbframe1, 2, petmSprTerre, 0, 0 );
    }
    else
    {
        int nbframe1 = ( nbFrame - 45 ) * 2;
        gbDrawBitmapRotated( 25 + nbframe1, 28, petmSprTerre, 0, 0 );
    }
    gbSetColor( GB_BLACK );
}

void petmAttaquePlante( bool isP1, int nbFrame )
{
    if( isP1 )
    {
        if( nbFrame > 37 )
        {
            int offset = ( 45 - nbFrame ) * 2;
            gbSetColor( GB_INVERT );
            gbDrawBitmapRotated( 60 + offset, 20 - offset, petmSprFeuille, 0, 1 );
            gbSetColor( GB_BLACK );
        }
        else if( nbFrame > 30 )
        {
            int offset = ( nbFrame - 30 ) * 2;
            gbSetColor( GB_INVERT );
            gbDrawBitmapRotated( 65 + offset, 20 - offset, petmSprFeuille, 0, 1 );
            gbSetColor( GB_BLACK );
        }
    }
    else
    {
        if( nbFrame > 37 )
        {
            int offset = ( nbFrame - 37 ) * 2;
            gbSetColor( GB_INVERT );
            gbDrawBitmapRotated( 2 + offset, 30 - offset, petmSprFeuille, 0, 1 );
            gbSetColor( GB_BLACK );
        }
        else if( nbFrame > 30 )
        {
            int offset = ( 37 - nbFrame ) * 2;
            gbSetColor( GB_INVERT );
            gbDrawBitmapRotated( 2 + offset, 30 - offset, petmSprFeuille, 0, 1 );
            gbSetColor( GB_BLACK );
        }
    }
}

void petmAttaqueElectrique( bool isP1, int nbFrame )
{
    if( nbFrame <= 30 ) return;
    gbSetColor( GB_INVERT );
    if( isP1 ) gbDrawBitmapRotated( 65 - 4 + arand( 8 ), 5 - 4 + arand( 8 ), petmSprElectrique, arand( 4 ), arand( 4 ) );
    else gbDrawBitmapRotated( 5 - 4 + arand( 8 ), 28 - 4 + arand( 8 ), petmSprElectrique, arand( 4 ), arand( 4 ) );
    gbSetColor( GB_BLACK );
}

void petmAttaquePsy( bool isP1, int nbFrame )
{
    if( nbFrame <= 30 ) return;
    gbSetColor( GB_INVERT );
    if( isP1 ) gbDrawBitmapRotated( 65 - 4 + arand( 8 ), 5 - 4 + arand( 8 ), petmSprpsy, arand( 4 ), arand( 4 ) );
    else gbDrawBitmapRotated( 5 - 4 + arand( 8 ), 28 - 4 + arand( 8 ), petmSprpsy, arand( 4 ), arand( 4 ) );
    gbSetColor( GB_BLACK );
}

void petmAttaqueCatch( bool isP1, int nbFrame )
{
    if( nbFrame <= 30 ) return;
    gbSetColor( GB_INVERT );
    if( isP1 ) gbDrawBitmapRotated( 65 - 4 + arand( 8 ), 5 - 4 + arand( 8 ), petmSprCatchThemAll, arand( 4 ), arand( 4 ) );
    else gbDrawBitmapRotated( 5 - 4 + arand( 8 ), 28 - 4 + arand( 8 ), petmSprCatchThemAll, arand( 4 ), arand( 4 ) );
    gbSetColor( GB_BLACK );
}

void petmResolutionAttaqueAnimation( int numAttaque, int numPattern, bool isP1, int nbFrame )
{
    int numAttk = petmAttaqueNumeroByPattern( numPattern, numAttaque );
    if( numAttk == 0 ) petmAttaqueGriffe( isP1, nbFrame );
    else if( numAttk == 1 ) petmAttaqueCharge( isP1, nbFrame );
    else if( numAttk == 2 ) petmAttaqueFeux( isP1, nbFrame );
    else if( numAttk == 3 ) petmAttaqueEau( isP1, nbFrame );
    else if( numAttk == 4 ) petmAttaqueTerre( isP1, nbFrame );
    else if( numAttk == 5 ) petmAttaquePlante( isP1, nbFrame );
    else if( numAttk == 6 ) petmAttaqueElectrique( isP1, nbFrame );
    else if( numAttk == 7 ) petmAttaquePsy( isP1, nbFrame );
    else if( numAttk == 9 ) petmAttaqueCatch( isP1, nbFrame );
    // numAttk == 8 ("->" change monster): real upstream draws nothing.
}

// -----------------------------------------------------------------------------
// World / exploration state - direct ports of modeExploration.ino/
// AllspriteBonus.ino.
// -----------------------------------------------------------------------------

int petmWorld[16][16];
int petmCursorX, petmCursorY, petmCursorXT, petmCursorYT;
int petmDirectionPerso;
bool petmIsMove;
bool petmIsDresseur;
bool petmIsDresseurKill;
int petmNbChanceAppearMonster;
int petmCameraX, petmCameraY;
int petmCurrentTheme;
int petmCptArea;
int petmCptKill;
int petmDresseurByTheme[4];
bool petmMonsterVue[10];
int petmNbVue;
bool petmMonsterCatch[10];
int petmNbCatch;
int petmBonus[3];

int petmGetSpriteID( int x, int y ) { return petmWorld[ x ][ y ] & 0x3F; }
int petmGetTileRotation( int x, int y ) { return ( petmWorld[ x ][ y ] >> 6 ) & 3; }
void petmSetTile( int x, int y, int spriteID, int rotation ) { petmWorld[ x ][ y ] = ( rotation << 6 ) + spriteID; }

void petmSetBonus( int x, int y, int bonusID, int numBonus )
{
    // Real hardware's `bonus[]` is a genuine 8-bit `byte` - this sum can
    // exceed 255 (bonusID needs bits 6-7, x can need up to bit 6, a real
    // overlap), and real hardware's own byte assignment silently
    // truncates modulo 256 - masked here to match that real narrowing.
    petmBonus[ numBonus ] = ( ( bonusID << 6 ) + ( x << 3 ) + y ) & 0xFF;
}

int petmGetBonusID( int px, int py, int pos )
{
    if( petmBonus[ pos ] > 0 )
    {
        int x = ( petmBonus[ pos ] >> 3 ) & 7;
        int y = petmBonus[ pos ] & 7;
        if( px == x && py == y ) return ( petmBonus[ pos ] >> 6 ) & 3;
    }
    return 255;
}

bool petmIsCaseBonus( int x, int y )
{
    int i = 0;
    while( i < PETM_NB_BONUS )
    {
        if( petmGetBonusID( x - 1, y - 1, i ) < 255 ) return true;
        i = i + 1;
    }
    return false;
}

void petmDrawAllBonus()
{
    int i = 0;
    while( i < PETM_NB_BONUS )
    {
        if( petmBonus[ i ] > 0 )
        {
            int x = ( petmBonus[ i ] >> 3 ) & 7;
            int y = petmBonus[ i ] & 7;
            int id = ( petmBonus[ i ] >> 6 ) & 3;
            int xs = ( x + 1 ) * 8 - petmCameraX;
            int ys = ( y + 1 ) * 8 - petmCameraY;
            gbDrawBitmap( xs, ys, petmGetBonusSprite( id ) );
        }
        i = i + 1;
    }
}

void petmTestGetBonus( int x, int y, int directionPerso )
{
    int xdir = x;
    int ydir = y;
    if( directionPerso == 0 ) ydir = ydir + 1;
    else if( directionPerso == 1 ) xdir = xdir + 1;
    else if( directionPerso == 2 ) ydir = ydir - 1;
    else if( directionPerso == 3 ) xdir = xdir - 1;

    int i = 0;
    while( i < PETM_NB_BONUS )
    {
        int id = petmGetBonusID( x - 1, y - 1, i );
        if( id == 255 ) id = petmGetBonusID( xdir - 1, ydir - 1, i );
        if( id < 255 )
        {
            petmSetBonus( 0, 0, 0, i );
            int j = 0;
            if( id == 0 )
            {
                while( j < petmNbMonstre( 0 ) ) { petmGetMonster( 0, j )->Defence = petmGetMonster( 0, j )->Defence + 1; j = j + 1; }
            }
            else if( id == 1 )
            {
                while( j < petmNbMonstre( 0 ) ) { petmGetMonster( 0, j )->Force = petmGetMonster( 0, j )->Force + 1; j = j + 1; }
            }
            else if( id == 2 )
            {
                while( j < petmNbMonstre( 0 ) ) { petmGetMonster( 0, j )->Vitesse = petmGetMonster( 0, j )->Vitesse + 1; j = j + 1; }
            }
            else if( id == 3 )
            {
                while( j < petmNbMonstre( 0 ) )
                {
                    PetmMonster* mm = petmGetMonster( 0, j );
                    mm->Vie = mm->VieMax;
                    mm->OldVie = mm->VieMax;
                    j = j + 1;
                }
            }
        }
        i = i + 1;
    }
}

void petmGenerateAllBonus()
{
    int i = 0;
    while( i < PETM_NB_BONUS )
    {
        petmSetBonus( arand( 15 ), arand( 15 ), arand( 4 ), i );
        i = i + 1;
    }
}

void petmInitialiseNbChance()
{
    int lo = gbMax( 300 - 2 * petmCptArea, 50 );
    petmNbChanceAppearMonster = lo + arand( 500 - lo );
}

void petmSpawnDresseur()
{
    if( petmDresseurByTheme[ petmCurrentTheme ] < PETM_NB_DRESSEUR_THEME && arand( 3 ) == 0 )
    {
        petmFoePosX = arand( 14 ) + 1;
        petmFoePosY = arand( 14 ) + 1;
        petmIsDresseurKill = false;
    }
    else
    {
        petmIsDresseurKill = true;
        petmFoePosX = 212;
        petmFoePosY = 212;
    }
}

void petmInitWorld()
{
    petmCptArea = petmCptArea + 1;
    petmCurrentTheme = arand( PETM_NB_THEMES );
    int add = petmCurrentTheme * 4;

    int y = 0;
    while( y < PETM_WORLD_H )
    {
        petmSetTile( 0, y, 3 + add, arand( 4 ) );
        petmSetTile( PETM_WORLD_W - 1, y, 3 + add, arand( 4 ) );
        y = y + 1;
    }
    int x = 0;
    while( x < PETM_WORLD_W )
    {
        petmSetTile( x, 0, 3 + add, arand( 4 ) );
        petmSetTile( x, PETM_WORLD_H - 1, 3 + add, arand( 4 ) );
        x = x + 1;
    }

    int spId = arand( 2 ) + add;
    y = 1;
    while( y < PETM_WORLD_H - 1 )
    {
        x = 1;
        while( x < PETM_WORLD_W - 1 )
        {
            if( y > 1 && y < PETM_WORLD_H - 2 && x > 1 && x < PETM_WORLD_W - 2 && arand( 5 ) == 0 )
              petmSetTile( x, y, 3 + add, 0 );
            else
              petmSetTile( x, y, spId, arand( 4 ) );
            x = x + 1;
        }
        y = y + 1;
    }

    petmSetTile( PETM_WORLD_W / 2, 0, 2, 0 );
    petmSetTile( PETM_WORLD_W / 2, PETM_WORLD_H - 1, 2, 0 );
    petmSetTile( 0, PETM_WORLD_H / 2, 2, 1 );
    petmSetTile( PETM_WORLD_W - 1, PETM_WORLD_H / 2, 2, 1 );

    petmGenerateAllBonus();
    petmSpawnDresseur();
    petmInitialiseNbChance();
}

void petmInitGame()
{
    petmInitWorld();
    petmCursorXT = PETM_WORLD_W / 4 + arand( PETM_WORLD_W / 2 - PETM_WORLD_W / 4 );
    petmCursorYT = PETM_WORLD_H / 4 + arand( PETM_WORLD_H / 2 - PETM_WORLD_H / 4 );
    petmCursorX = petmCursorXT * 8;
    petmCursorY = petmCursorYT * 8;
    petmDirectionPerso = 0;
    petmIsMove = false;
    petmNbChanceAppearMonster = 500;
}

void petmDrawWorld()
{
    int y = 0;
    while( y < PETM_WORLD_H )
    {
        int x = 0;
        while( x < PETM_WORLD_W )
        {
            int spriteID = petmGetSpriteID( x, y );
            int rotation = petmGetTileRotation( x, y );
            int xs = x * 8 - petmCameraX;
            int ys = y * 8 - petmCameraY;
            bool skip = false;
            if( xs < -8 || xs > LCDWIDTH || ys < -8 || ys > LCDHEIGHT ) skip = true;
            if( !skip && petmIsCaseBonus( x, y ) ) skip = true;
            if( !skip && petmFoePosX == x && petmFoePosY == y ) skip = true;
            if( !skip ) gbDrawBitmapRotated( xs, ys, petmWorldSprites[ spriteID ], rotation, 0 );
            x = x + 1;
        }
        y = y + 1;
    }
    petmDrawAllBonus();

    int xs = petmFoePosX * 8 - petmCameraX;
    int ys = petmFoePosY * 8 - petmCameraY;
    gbDrawBitmap( xs, ys, petmSpritesDresseur[ 0 ] );

    gbSetFont( gbFont3x3 );
    gbPrintString( "Area:" );
    gbPrintNumber( petmCptArea );
    gbPrintString( ".Kill:" );
    gbPrintNumber( petmCptKill );
    gbPrintString( ".View:" );
    gbPrintNumber( petmNbVue );
    gbSetFont( gbFont3x5 );
}

void petmDrawPerso()
{
    int xs = petmCursorX - petmCameraX;
    int ys = petmCursorY - petmCameraY;
    if( xs < -16 || xs > LCDWIDTH || ys < -16 || ys > LCDHEIGHT ) return;

    int index = petmDirectionPerso;
    if( petmIsMove )
    {
        index = petmDirectionPerso + petmDirectionPerso + 4;
        if( gbFrameCount % 8 > 3 ) index = index + 1;
    }
    gbDrawBitmapRotated( xs, ys, petmSpritesPerso[ index ], 0, 0 );
}

bool petmTestGetDresseur( int x, int y, int directionPerso )
{
    if( directionPerso == 0 ) y = y + 1;
    else if( directionPerso == 1 ) x = x + 1;
    else if( directionPerso == 2 ) y = y - 1;
    else if( directionPerso == 3 ) x = x - 1;
    if( petmFoePosX == x && petmFoePosY == y ) return true;
    return false;
}

bool petmUpdatePerso()
{
    if( petmIsMove )
    {
        if( petmCursorX != petmCursorXT * 8 )
        {
            if( petmCursorXT * 8 > petmCursorX ) petmCursorX = petmCursorX + 1;
            else petmCursorX = petmCursorX - 1;
        }
        else if( petmCursorY != petmCursorYT * 8 )
        {
            if( petmCursorYT * 8 > petmCursorY ) petmCursorY = petmCursorY + 1;
            else petmCursorY = petmCursorY - 1;
        }
        if( petmCursorY == petmCursorYT * 8 && petmCursorX == petmCursorXT * 8 ) petmIsMove = false;
    }

    if( !petmIsMove )
    {
        int lastX = petmCursorXT;
        int lastY = petmCursorYT;

        if( gbRepeat( BTN_RIGHT, 4 ) ) { petmCursorXT = petmWrap( petmCursorXT + 1, PETM_WORLD_W ); petmDirectionPerso = 1; }
        if( gbRepeat( BTN_LEFT, 4 ) )  { petmCursorXT = petmWrap( petmCursorXT - 1, PETM_WORLD_W ); petmDirectionPerso = 3; }
        if( gbRepeat( BTN_DOWN, 4 ) )  { petmCursorYT = petmWrap( petmCursorYT + 1, PETM_WORLD_H ); petmDirectionPerso = 0; }
        if( gbRepeat( BTN_UP, 4 ) )    { petmCursorYT = petmWrap( petmCursorYT - 1, PETM_WORLD_H ); petmDirectionPerso = 2; }

        int spriteID = petmGetSpriteID( petmCursorXT, petmCursorYT );

        if( ( spriteID + 1 ) % 4 == 0 || ( petmCursorXT == petmFoePosX && petmCursorYT == petmFoePosY ) )
        {
            petmCursorXT = lastX;
            petmCursorYT = lastY;
            spriteID = petmGetSpriteID( petmCursorXT, petmCursorYT );
        }

        if( lastX != petmCursorXT || lastY != petmCursorYT )
        {
            petmIsMove = true;
            if( ( spriteID + 2 ) % 4 == 0 )
            {
                petmInitWorld();
                if( petmCursorXT == 0 ) petmCursorXT = PETM_WORLD_W - 1;
                else if( petmCursorXT == PETM_WORLD_W - 1 ) petmCursorXT = 0;
                else if( petmCursorYT == 0 ) petmCursorYT = PETM_WORLD_H - 1;
                else if( petmCursorYT == PETM_WORLD_H - 1 ) petmCursorYT = 0;
                petmCursorX = petmCursorXT * 8;
                petmCursorY = petmCursorYT * 8;
            }
        }
    }

    if( gbPressed( BTN_A ) )
    {
        petmTestGetBonus( petmCursorXT, petmCursorYT, petmDirectionPerso );
        if( !petmIsDresseurKill && petmTestGetDresseur( petmCursorXT, petmCursorYT, petmDirectionPerso ) )
        {
            petmIsDresseur = true;
            petmInitialiseNbChance();
            petmDresseurByTheme[ petmCurrentTheme ] = petmDresseurByTheme[ petmCurrentTheme ] + 1;
            petmIsDresseurKill = true;
            return false;
        }
    }

    if( petmWrapRdm0N( petmNbChanceAppearMonster ) == 0 )
    {
        petmInitialiseNbChance();
        petmIsDresseur = false;
        return false;
    }
    petmNbChanceAppearMonster = petmNbChanceAppearMonster - 1;

    petmCameraX = petmCursorX - LCDWIDTH / 2 + 8;
    petmCameraY = petmCursorY - LCDHEIGHT / 2 + 8;
    return true;
}

void petmBeginCombat();

bool petmExplorationUpdate()
{
    if( petmUpdatePerso() )
    {
        petmDrawWorld();
        petmDrawPerso();
        return true;
    }

    if( !petmIsDresseur )
    {
        petmAddEmptyMonster( 1 );
        petmSelectMonster( 1, 0 );
        petmFoeIsMonster = true;
        PetmMonster* m = petmGetSelected( 1 );
        petmGenerateMonsterByLvlAndZone( m, petmCptArea + arand( petmCptArea / 4 ), petmCurrentTheme );
    }
    else
    {
        petmFoeIsMonster = false;
        int i = 0;
        while( i < 4 )
        {
            petmAddEmptyMonster( 1 );
            PetmMonster* m = petmGetMonster( 1, i );
            petmGenerateMonsterByLvlAndZone( m, petmCptArea + arand( petmCptArea / 2 ), petmCurrentTheme );
            i = i + 1;
        }
        petmSelectMonster( 1, 0 );
    }
    return false;
}

// -----------------------------------------------------------------------------
// Combat - see this file's own header comment for the full flattening
// rationale. `petmCombatPhase` walks through every real sub-phase of
// ModeCombat.ino's own `CombatMonste()`.
// -----------------------------------------------------------------------------

int petmState;
int petmCombatPhase;
int petmAnimNbFrame;
bool petmCombatIsWhite;
bool petmIsCaptureMode;
bool petmIsCatch;
bool petmIsP1First;
bool petmIsPNonInitiativeAlive;
int petmDeathIsFoe; // 0 = player dying, 1 = foe dying
int petmMenuCursor;
int petmInfoIsFoe;
int petmInfoOffset;
int petmTeamFlashIndex;
int petmDexIndex;
int petmDexOffset;
int petmStartCpt;
int petmStarterChoix;
int petmStarterOffset;

int petmGetWidthBarreVie( int pourcentVie, int tailleMaxPx )
{
    return pourcentVie * tailleMaxPx / 100;
}

void petmDrawHud()
{
    gbDrawBitmap( 2, 0, petmSprsprBareVie );
    gbFillRect( 3, 1, petmGetWidthBarreVie( petmGetPourcentVieRestant( petmGetSelected( 1 ) ), 43 ), 6 );
    gbDrawBitmap( 39, 41, petmSprsprBareVie );
    int wd = petmGetWidthBarreVie( petmGetPourcentVieRestant( petmGetSelected( 0 ) ), 43 );
    gbFillRect( 40 + ( 43 - wd ), 41, wd, 6 );
}

void petmDrawCheckerboard( bool isWhite )
{
    int i = 0;
    while( i < 15 )
    {
        int y = 0;
        while( y < 15 )
        {
            bool draw = false;
            if( isWhite )
            {
                if( y % 2 == 0 ) { if( i % 2 == 0 ) draw = true; }
                else { if( i % 2 != 0 ) draw = true; }
            }
            else
            {
                if( y % 2 == 0 ) { if( i % 2 != 0 ) draw = true; }
                else { if( i % 2 == 0 ) draw = true; }
            }
            if( draw )
            {
                gbFillRect( i * 4, y * 7, 4, 7 );
                gbSetColor( GB_BLACK );
            }
            y = y + 1;
        }
        i = i + 1;
    }
}

// Draws the foe's own selected-monster arrival slide-in for this tick;
// returns true once finished (early A-press, or the animation naturally
// ran out) - see this file's own header comment on the "always draw, even
// on the finishing tick" simplification.
bool petmStepArriveAnim()
{
    int xpos = 60;
    bool finished = false;
    if( petmAnimNbFrame > 30 ) xpos = 30 + petmAnimNbFrame;
    else if( gbPressed( BTN_A ) || petmAnimNbFrame <= 0 ) finished = true;
    gbDrawBitmap( xpos, 0, petmGetMonsterSprite( petmGetSelected( 1 )->Numero, true ) );
    if( !finished ) petmAnimNbFrame = petmAnimNbFrame - 1;
    return finished;
}

bool petmStepAttackAnim( bool isPlayerAttacker )
{
    petmDrawHud();
    bool blink = false;
    if( petmAnimNbFrame < 25 && petmAnimNbFrame % 4 > 1 && petmAnimNbFrame > 5 ) blink = true;

    if( isPlayerAttacker )
    {
        if( !blink ) gbDrawBitmap( 60, 0, petmGetMonsterSprite( petmGetSelected( 1 )->Numero, true ) );
        gbDrawBitmap( 2, 24, petmGetMonsterSprite( petmGetSelected( 0 )->Numero, false ) );
        petmResolutionAttaqueAnimation( petmGetSelected( 0 )->SelectedAttaque, petmGetSelected( 0 )->NumeroPattern, true, petmAnimNbFrame );
    }
    else
    {
        if( !blink ) gbDrawBitmap( 2, 24, petmGetMonsterSprite( petmGetSelected( 0 )->Numero, false ) );
        gbDrawBitmap( 60, 0, petmGetMonsterSprite( petmGetSelected( 1 )->Numero, true ) );
        petmResolutionAttaqueAnimation( petmGetSelected( 1 )->SelectedAttaque, petmGetSelected( 1 )->NumeroPattern, false, petmAnimNbFrame );
    }

    if( petmAnimNbFrame <= 0 ) return true;
    if( isPlayerAttacker ) petmDecrementOldVie( petmGetSelected( 1 ), petmAnimNbFrame );
    else petmDecrementOldVie( petmGetSelected( 0 ), petmAnimNbFrame );
    petmAnimNbFrame = petmAnimNbFrame - 1;
    return false;
}

// Shared "monster status page" - a direct port of real DysplayEtatFuturomon()
// (always shows the FRONT sprite variant, matching upstream exactly even
// for the player's own monster).
bool petmStepMonsterInfo( int isFoe )
{
    PetmMonster* m = petmGetSelected( isFoe );
    gbDrawBitmap( 60, 0 - petmInfoOffset, petmGetMonsterSprite( m->Numero, true ) );
    if( petmInfoOffset > 0 ) petmInfoOffset = petmInfoOffset - 3;

    gbPrintString( "LvL:" );
    petmPrintlnNum( m->Niveau );
    gbPrintString( "Hp:" );
    gbPrintNumber( m->Vie );
    gbPrintString( "/" );
    petmPrintlnNum( m->VieMax );
    gbPrintString( "Speed:" );
    petmPrintlnNum( m->Vitesse );
    gbPrintString( "For:" );
    petmPrintlnNum( m->Force );
    gbPrintString( "Def:" );
    petmPrintlnNum( m->Defence );
    gbPrintString( "XP:" );
    gbPrintNumber( m->Xp );
    gbPrintString( "/" );
    petmPrintlnNum( m->NextNiveau );

    if( gbPressed( BTN_A ) ) return true;
    return false;
}

void petmDrawTeamMenuLabels()
{
    gbCursorX = 2; gbCursorY = 1;
    gbPrintString( "CHOOSE" );
    int i = 0;
    while( i < 4 )
    {
        gbCursorY = 12 + i * 8;
        gbCursorX = 0;
        if( i == petmMenuCursor ) gbPrintString( "*" );
        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "Slot 1" );
        else if( i == 1 ) gbPrintString( "Slot 2" );
        else if( i == 2 ) gbPrintString( "Slot 3" );
        else gbPrintString( "Slot 4" );
        i = i + 1;
    }
}

void petmDrawAttackMenuLabels( int pattern )
{
    gbCursorX = 2; gbCursorY = 1;
    gbPrintString( "ATTACK" );
    int i = 0;
    while( i < 5 )
    {
        gbCursorY = 12 + i * 7;
        gbCursorX = 0;
        if( i == petmMenuCursor ) gbPrintString( "*" );
        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "claw" );
        else if( i == 1 ) gbPrintString( "onslaught" );
        else if( i == 2 )
        {
            if( pattern == 0 ) gbPrintString( "Fire" );
            else if( pattern == 1 ) gbPrintString( "Water" );
            else if( pattern == 2 ) gbPrintString( "plant" );
            else if( pattern == 3 ) gbPrintString( "ground" );
            else if( pattern == 4 ) gbPrintString( "electrique" );
            else gbPrintString( "psychic" );
        }
        else if( i == 3 ) gbPrintString( "->" );
        else gbPrintString( "Catch" );
        i = i + 1;
    }
}

void petmDrawYesNoMenuLabels()
{
    gbCursorX = 2; gbCursorY = 1;
    gbPrintString( "ADD TO TEAM?" );
    int i = 0;
    while( i < 2 )
    {
        gbCursorY = 14 + i * 10;
        gbCursorX = 0;
        if( i == petmMenuCursor ) gbPrintString( "*" );
        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "Yes" );
        else gbPrintString( "No" );
        i = i + 1;
    }
}

void petmCombatResolutionOfAttack()
{
    int v1 = petmGetSelected( 0 )->Vitesse + petmTirageDes( petmGetSelected( 0 )->Vitesse );
    int v2 = petmGetSelected( 1 )->Vitesse + petmTirageDes( petmGetSelected( 1 )->Vitesse );
    if( v1 > v2 )
    {
        petmIsP1First = true;
        petmCombatAttack( petmGetSelected( 0 ), petmGetSelected( 1 ) );
        if( petmIsAlive( petmGetSelected( 1 ) ) )
        {
            petmCombatAttack( petmGetSelected( 1 ), petmGetSelected( 0 ) );
            petmIsPNonInitiativeAlive = true;
        }
        else petmIsPNonInitiativeAlive = false;
    }
    else
    {
        petmIsP1First = false;
        petmCombatAttack( petmGetSelected( 1 ), petmGetSelected( 0 ) );
        if( petmIsAlive( petmGetSelected( 0 ) ) )
        {
            petmCombatAttack( petmGetSelected( 0 ), petmGetSelected( 1 ) );
            petmIsPNonInitiativeAlive = true;
        }
        else petmIsPNonInitiativeAlive = false;
    }
}

void petmCombatResolutionOfCatch()
{
    petmIsP1First = true;
    if( petmFoeIsMonster && arand( ( petmGetSelected( 1 )->Vie / 2 ) + 1 ) == 0 )
    {
        petmIsPNonInitiativeAlive = false;
        petmMonsterCatch[ petmGetSelected( 1 )->Numero ] = true;
        petmIsCatch = true;
        petmNbCatch = petmNbCatch + 1;
    }
    else
    {
        petmCombatAttack( petmGetSelected( 1 ), petmGetSelected( 0 ) );
        petmIsPNonInitiativeAlive = true;
    }
}

void petmEnterCombatEnd()
{
    if( petmHaveMonsterOk( 0 ) ) gbPopup( "Youpy!", 35 );
    else gbPopup( "...", 35 );
    petmAnimNbFrame = 35;
    petmCombatPhase = PETM_CP_END;
}

void petmBeginCombat()
{
    petmState = PETM_ST_COMBAT;
    petmCombatPhase = PETM_CP_INTRO;
    petmAnimNbFrame = 20;
    petmCombatIsWhite = false;
}

void petmCombatStep()
{
    if( petmCombatPhase == PETM_CP_INTRO )
    {
        if( petmAnimNbFrame > 0 )
        {
            petmAnimNbFrame = petmAnimNbFrame - 1;
            if( petmAnimNbFrame % 3 == 0 ) petmCombatIsWhite = !petmCombatIsWhite;
            petmDrawCheckerboard( petmCombatIsWhite );
            return;
        }
        if( petmFoeIsMonster ) gbPopup( "a wild Futuromon!", 30 );
        else gbPopup( "A champion", 30 );
        petmIsCatch = false;
        petmAnimNbFrame = 60;
        petmCombatPhase = PETM_CP_FIRST_ARRIVE;
        return;
    }

    if( petmCombatPhase == PETM_CP_FIRST_ARRIVE )
    {
        if( petmStepArriveAnim() )
        {
            petmUnselectMonster( 0 );
            petmCombatPhase = PETM_CP_ROUND_ADV_GUARD;
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_ROUND_ADV_GUARD )
    {
        if( !petmIsSelected( 1 ) )
        {
            int cpt = 0;
            petmSelectMonster( 1, cpt );
            cpt = cpt + 1;
            while( !petmIsAlive( petmGetSelected( 1 ) ) && cpt < petmNbMonstre( 1 ) )
            {
                petmSelectMonster( 1, cpt );
                cpt = cpt + 1;
            }
            petmIsCatch = false;
            petmAnimNbFrame = 60;
            petmCombatPhase = PETM_CP_FOE_REARRIVE;
            return;
        }
        if( !petmMonsterVue[ petmGetSelected( 1 )->Numero ] )
        {
            petmMonsterVue[ petmGetSelected( 1 )->Numero ] = true;
            petmNbVue = petmNbVue + 1;
        }
        petmCombatPhase = PETM_CP_PLAYER_GUARD;
        return;
    }

    if( petmCombatPhase == PETM_CP_FOE_REARRIVE )
    {
        if( petmStepArriveAnim() )
        {
            if( !petmMonsterVue[ petmGetSelected( 1 )->Numero ] )
            {
                petmMonsterVue[ petmGetSelected( 1 )->Numero ] = true;
                petmNbVue = petmNbVue + 1;
            }
            petmCombatPhase = PETM_CP_PLAYER_GUARD;
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_PLAYER_GUARD )
    {
        if( !petmIsSelected( 0 ) )
        {
            petmMenuCursor = 0;
            petmCombatPhase = PETM_CP_PLAYER_MENU;
            return;
        }
        petmCombatPhase = PETM_CP_FOE_ATTACK;
        return;
    }

    if( petmCombatPhase == PETM_CP_PLAYER_MENU )
    {
        petmDrawTeamMenuLabels();
        if( gbRepeat( BTN_UP, 5 ) ) petmMenuCursor = gbMax( 0, petmMenuCursor - 1 );
        if( gbRepeat( BTN_DOWN, 5 ) ) petmMenuCursor = gbMin( 3, petmMenuCursor + 1 );
        if( gbPressed( BTN_A ) )
        {
            petmSelectMonster( 0, petmMenuCursor );
            if( petmIsAlive( petmGetSelected( 0 ) ) )
            {
                gbPopup( "Go!", 60 );
                petmAnimNbFrame = 60;
                petmCombatPhase = PETM_CP_PLAYER_ARRIVE;
            }
            // else: real upstream's own do-while just re-prompts silently.
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_PLAYER_ARRIVE )
    {
        int xpos = 2;
        bool finished = false;
        if( petmAnimNbFrame > 30 ) xpos = 28 - petmAnimNbFrame;
        else if( gbPressed( BTN_A ) || petmAnimNbFrame <= 0 ) finished = true;
        gbDrawBitmap( 60, 0, petmGetMonsterSprite( petmGetSelected( 1 )->Numero, true ) );
        gbDrawBitmap( xpos, 24, petmGetMonsterSprite( petmGetSelected( 0 )->Numero, false ) );
        petmDrawHud();
        if( !finished ) { petmAnimNbFrame = petmAnimNbFrame - 1; return; }
        petmCombatPhase = PETM_CP_FOE_ATTACK;
        return;
    }

    if( petmCombatPhase == PETM_CP_FOE_ATTACK )
    {
        petmGetSelected( 1 )->SelectedAttaque = arand( 3 ); // NB_MAX_NUM_ATTAQUE_BY_IA
        petmMenuCursor = 0;
        petmCombatPhase = PETM_CP_PLAYER_ATTACK;
        return;
    }

    if( petmCombatPhase == PETM_CP_PLAYER_ATTACK )
    {
        petmDrawAttackMenuLabels( petmGetSelected( 0 )->NumeroPattern );
        if( gbRepeat( BTN_UP, 5 ) ) petmMenuCursor = gbMax( 0, petmMenuCursor - 1 );
        if( gbRepeat( BTN_DOWN, 5 ) ) petmMenuCursor = gbMin( 4, petmMenuCursor + 1 );
        if( gbPressed( BTN_A ) )
        {
            if( petmMenuCursor == 3 )
            {
                petmUnselectMonster( 0 );
                petmIsCaptureMode = false;
                petmCombatPhase = PETM_CP_ROUND_ADV_GUARD;
            }
            else if( petmMenuCursor == 4 )
            {
                petmIsCaptureMode = true;
                petmGetSelected( 0 )->SelectedAttaque = 4;
                petmCombatPhase = PETM_CP_RESOLVE;
            }
            else
            {
                petmIsCaptureMode = false;
                petmGetSelected( 0 )->SelectedAttaque = petmMenuCursor;
                petmCombatPhase = PETM_CP_RESOLVE;
            }
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_RESOLVE )
    {
        if( !petmIsCaptureMode ) petmCombatResolutionOfAttack();
        else petmCombatResolutionOfCatch();
        petmAnimNbFrame = 45;
        petmCombatPhase = PETM_CP_ANIM_A;
        return;
    }

    if( petmCombatPhase == PETM_CP_ANIM_A )
    {
        if( petmStepAttackAnim( petmIsP1First ) )
        {
            if( petmIsPNonInitiativeAlive )
            {
                petmAnimNbFrame = 45;
                petmCombatPhase = PETM_CP_ANIM_B;
            }
            else petmCombatPhase = PETM_CP_DEATH_CHECK;
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_ANIM_B )
    {
        if( petmStepAttackAnim( !petmIsP1First ) ) petmCombatPhase = PETM_CP_DEATH_CHECK;
        return;
    }

    if( petmCombatPhase == PETM_CP_DEATH_CHECK )
    {
        if( !petmIsAlive( petmGetSelected( 1 ) ) )
        {
            petmAnimNbFrame = 0;
            petmDeathIsFoe = 1;
            petmCombatPhase = PETM_CP_ANIM_DEATH;
        }
        else if( !petmIsAlive( petmGetSelected( 0 ) ) )
        {
            petmAnimNbFrame = 0;
            petmDeathIsFoe = 0;
            petmCombatPhase = PETM_CP_ANIM_DEATH;
        }
        else petmCombatPhase = PETM_CP_AFTERMATH;
        return;
    }

    if( petmCombatPhase == PETM_CP_ANIM_DEATH )
    {
        petmDrawHud();
        if( petmDeathIsFoe == 1 )
        {
            gbDrawBitmap( 60, 0 - petmAnimNbFrame, petmGetMonsterSprite( petmGetSelected( 1 )->Numero, true ) );
            gbDrawBitmap( 2, 24, petmGetMonsterSprite( petmGetSelected( 0 )->Numero, false ) );
        }
        else
        {
            gbDrawBitmap( 60, 0, petmGetMonsterSprite( petmGetSelected( 1 )->Numero, true ) );
            gbDrawBitmap( 2, 24 + petmAnimNbFrame, petmGetMonsterSprite( petmGetSelected( 0 )->Numero, false ) );
        }
        if( petmAnimNbFrame >= 40 ) { petmCombatPhase = PETM_CP_AFTERMATH; return; }
        petmAnimNbFrame = petmAnimNbFrame + 1;
        return;
    }

    if( petmCombatPhase == PETM_CP_AFTERMATH )
    {
        petmGetSelected( 0 )->SelectedAttaque = 255;
        petmGetSelected( 1 )->SelectedAttaque = 255;
        if( !petmIsAlive( petmGetSelected( 1 ) ) ) petmCptKill = petmCptKill + 1;

        if( petmIsCatch && petmIsCaptureMode )
        {
            petmInfoIsFoe = 1;
            petmInfoOffset = 30;
            petmCombatPhase = PETM_CP_CATCH_INFO;
            return;
        }
        if( !petmIsAlive( petmGetSelected( 1 ) ) && petmIsAlive( petmGetSelected( 0 ) ) )
        {
            petmGetSelected( 0 )->Xp = petmGetSelected( 0 )->Xp + 2 * petmGetSelected( 1 )->Niveau;
            if( petmGetSelected( 0 )->Xp > petmGetSelected( 0 )->NextNiveau )
            {
                petmLevelUpMonster( petmGetSelected( 0 ) );
                petmInfoIsFoe = 0;
                petmInfoOffset = 30;
                petmCombatPhase = PETM_CP_LEVELUP_INFO;
                return;
            }
        }
        petmCombatPhase = PETM_CP_ROUND_DECIDE;
        return;
    }

    if( petmCombatPhase == PETM_CP_CATCH_INFO )
    {
        if( petmStepMonsterInfo( petmInfoIsFoe ) )
        {
            gbPopup( "Add to team?", 40 );
            petmMenuCursor = 0;
            petmCombatPhase = PETM_CP_CATCH_PROMPT;
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_CATCH_PROMPT )
    {
        petmDrawYesNoMenuLabels();
        if( gbRepeat( BTN_UP, 5 ) ) petmMenuCursor = gbMax( 0, petmMenuCursor - 1 );
        if( gbRepeat( BTN_DOWN, 5 ) ) petmMenuCursor = gbMin( 1, petmMenuCursor + 1 );
        if( gbPressed( BTN_A ) )
        {
            if( petmMenuCursor == 0 )
            {
                if( petmIsFull( 0 ) )
                {
                    gbPopup( "Switch", 40 );
                    petmMenuCursor = 0;
                    petmCombatPhase = PETM_CP_CATCH_SWAP_SELECT;
                    return;
                }
                int newIdx = petmAddEmptyMonster( 0 );
                petmCopyCaughtMonster( 1, 0, newIdx );
            }
            petmEnterCombatEnd();
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_CATCH_SWAP_SELECT )
    {
        petmDrawTeamMenuLabels();
        if( gbRepeat( BTN_UP, 5 ) ) petmMenuCursor = gbMax( 0, petmMenuCursor - 1 );
        if( gbRepeat( BTN_DOWN, 5 ) ) petmMenuCursor = gbMin( 3, petmMenuCursor + 1 );
        if( gbPressed( BTN_A ) )
        {
            petmCopyCaughtMonster( 1, 0, petmMenuCursor );
            petmEnterCombatEnd();
        }
        return;
    }

    if( petmCombatPhase == PETM_CP_LEVELUP_INFO )
    {
        if( petmStepMonsterInfo( petmInfoIsFoe ) ) petmCombatPhase = PETM_CP_ROUND_DECIDE;
        return;
    }

    if( petmCombatPhase == PETM_CP_ROUND_DECIDE )
    {
        if( petmIsCatch ) { petmEnterCombatEnd(); return; }
        if( petmHaveMonsterOk( 0 ) && petmHaveMonsterOk( 1 ) )
        {
            if( !petmIsAlive( petmGetSelected( 1 ) ) ) petmUnselectMonster( 1 );
            if( !petmIsAlive( petmGetSelected( 0 ) ) ) petmUnselectMonster( 0 );
            petmCombatPhase = PETM_CP_ROUND_ADV_GUARD;
            return;
        }
        petmEnterCombatEnd();
        return;
    }

    if( petmCombatPhase == PETM_CP_END )
    {
        if( petmAnimNbFrame <= 0 )
        {
            petmUnselectMonster( 0 );
            petmUnselectMonster( 1 );
            petmClearMonster( 1 );
            if( !petmHaveMonsterOk( 0 ) ) petmState = PETM_ST_GAMEOVER;
            else petmState = PETM_ST_EXPLORATION;
            return;
        }
        petmAnimNbFrame = petmAnimNbFrame - 1;
        return;
    }
}

void petmUpdateCombat()
{
    bool again = true;
    while( again )
    {
        int before = petmCombatPhase;
        int beforeState = petmState;
        petmCombatStep();
        if( petmState != beforeState ) return; // left combat entirely
        if( petmCombatPhase == before ) again = false;
    }
}

// -----------------------------------------------------------------------------
// Team / Futurodex / main C-button menu - direct ports of MonsterHelper.ino.
// -----------------------------------------------------------------------------

void petmDrawGameStats()
{
    gbSetFont( gbFont3x5 );
    gbPrintString( "Area:" );
    gbPrintNumber( petmCptArea );
    gbPrintString( " " );
    gbPrintString( "Kill:" );
    petmPrintlnNum( petmCptKill );
    gbPrintString( "View:" );
    gbPrintNumber( petmNbVue );
    gbPrintString( " " );
    gbPrintString( "Catch:" );
    petmPrintlnNum( petmNbCatch );
    gbPrintString( "Champion kill : " );
    int nbD = 0;
    int i = 0;
    while( i < PETM_NB_THEMES ) { nbD = nbD + petmDresseurByTheme[ i ]; i = i + 1; }
    petmPrintlnNum( nbD );
}

void petmUpdateMainMenu()
{
    gbCursorX = 2; gbCursorY = 1;
    gbPrintString( "PETITMONSTRE" );
    int i = 0;
    while( i < 3 )
    {
        gbCursorY = 12 + i * 10;
        gbCursorX = 0;
        if( i == petmMenuCursor ) gbPrintString( "*" );
        gbCursorX = 8;
        if( i == 0 ) gbPrintString( "Team" );
        else if( i == 1 ) gbPrintString( "Futurodex" );
        else gbPrintString( "Change game" );
        i = i + 1;
    }

    if( gbRepeat( BTN_UP, 5 ) ) petmMenuCursor = gbMax( 0, petmMenuCursor - 1 );
    if( gbRepeat( BTN_DOWN, 5 ) ) petmMenuCursor = gbMin( 2, petmMenuCursor + 1 );
    if( gbPressed( BTN_A ) )
    {
        if( petmMenuCursor == 0 )
        {
            petmTeamFlashIndex = 0;
            petmSelectMonster( 0, 0 );
            petmInfoOffset = 30;
            petmState = PETM_ST_TEAM_FLASH;
        }
        else if( petmMenuCursor == 1 )
        {
            petmDexIndex = 0;
            petmState = PETM_ST_DEX_SCAN;
        }
        else
        {
            // Real upstream: gb.changeGame() - a real-hardware "switch
            // cartridge on the SD card" OS feature with no equivalent in
            // this single-cartridge shared-menu model, dropped outright
            // (matching gameA2K.c's/gameSnakeAbc.c's own established
            // treatment of the identical real call) - just returns to
            // exploration.
            petmState = PETM_ST_EXPLORATION;
        }
    }
}

void petmUpdateTeamFlash()
{
    if( petmNbMonstre( 0 ) == 0 )
    {
        petmState = PETM_ST_TEAM_SUMMARY;
        return;
    }
    if( petmStepMonsterInfo( 0 ) )
    {
        petmTeamFlashIndex = petmTeamFlashIndex + 1;
        if( petmTeamFlashIndex >= petmNbMonstre( 0 ) )
        {
            petmState = PETM_ST_TEAM_SUMMARY;
        }
        else
        {
            petmSelectMonster( 0, petmTeamFlashIndex );
            petmInfoOffset = 30;
        }
    }
}

void petmUpdateTeamSummary()
{
    petmDrawGameStats();
    if( gbPressed( BTN_A ) )
    {
        petmUnselectMonster( 0 );
        petmState = PETM_ST_EXPLORATION;
    }
}

void petmUpdateDexScan()
{
    while( petmDexIndex < PETM_NB_MONSTERS && !petmMonsterVue[ petmDexIndex ] ) petmDexIndex = petmDexIndex + 1;
    if( petmDexIndex < PETM_NB_MONSTERS && petmMonsterVue[ petmDexIndex ] )
    {
        petmDexOffset = 30;
        petmState = PETM_ST_DEX_SHOW;
    }
    else
    {
        gbPopup( "End", 40 );
        petmUnselectMonster( 0 );
        petmState = PETM_ST_EXPLORATION;
    }
}

void petmUpdateDexShow()
{
    if( petmMonsterCatch[ petmDexIndex ] ) gbPrintString( "Catch:" );
    else gbPrintString( "View:" );
    gbDrawBitmap( 60, 10 - petmDexOffset, petmGetMonsterSprite( petmDexIndex, true ) );
    gbDrawBitmap( 30, 10 - petmDexOffset, petmGetMonsterSprite( petmDexIndex, false ) );
    if( petmDexOffset > 0 ) petmDexOffset = petmDexOffset - 3;
    if( gbPressed( BTN_A ) )
    {
        petmDexIndex = petmDexIndex + 1;
        petmState = PETM_ST_DEX_SCAN;
    }
}

void petmUpdateExplorationTop()
{
    if( petmExplorationUpdate() )
    {
        if( gbPressed( BTN_C ) )
        {
            petmMenuCursor = 0;
            petmState = PETM_ST_MAINMENU;
        }
        return;
    }
    petmBeginCombat();
}

// -----------------------------------------------------------------------------
// Start scene / starter choice - direct ports of StartScene.ino. Real
// upstream's own `display.textWrap=true` has no equivalent here - each
// sentence was pre-wrapped by hand with an embedded `\n` instead (this
// shim's own `gbPrintString()` does support a real line break).
// -----------------------------------------------------------------------------

void petmUpdateStartScene()
{
    gbPrintString( "I don't remember\nwhy i here.\n" );
    if( petmStartCpt > 0 ) gbPrintString( "I wake up and a\nmonster come help me.\n" );
    if( petmStartCpt > 1 ) gbPrintString( "Without him I'd be\ndead.\n" );
    if( petmStartCpt > 2 ) gbPrintString( "Now i want fight for\nmy freedom!\n" );
    if( petmStartCpt > 3 )
    {
        petmState = PETM_ST_CHOOSE_STARTER;
        petmStarterChoix = 0;
        petmStarterOffset = 0;
        return;
    }
    if( gbPressed( BTN_A ) ) petmStartCpt = petmStartCpt + 1;
}

void petmUpdateChooseStarter()
{
    gbDrawBitmap( 5, 10, petmSprFleche );
    gbDrawBitmapRotated( 75, 10, petmSprFleche, 0, 1 );
    gbDrawBitmap( 30 + petmStarterOffset, 24, petmGetMonsterSprite( petmStarterChoix, true ) );

    if( petmStarterOffset > 0 ) petmStarterOffset = petmStarterOffset - 8;
    else if( petmStarterOffset < 0 ) petmStarterOffset = petmStarterOffset + 8;

    if( gbPressed( BTN_LEFT ) )
    {
        petmStarterChoix = petmStarterChoix + 1;
        petmStarterChoix = petmStarterChoix % 3;
        petmStarterOffset = 40;
    }
    if( gbPressed( BTN_RIGHT ) )
    {
        petmStarterChoix = petmStarterChoix - 1;
        if( petmStarterChoix < 0 ) petmStarterChoix = 2;
        petmStarterOffset = -40;
    }
    if( gbPressed( BTN_A ) )
    {
        int newIdx = petmAddEmptyMonster( 0 );
        petmGenerateStartMonster( petmGetMonster( 0, newIdx ), petmStarterChoix );
        petmMonsterVue[ petmStarterChoix ] = true;
        petmMonsterCatch[ petmStarterChoix ] = true;
        petmNbVue = petmNbVue + 1;
        petmNbCatch = petmNbCatch + 1;
        petmState = PETM_ST_EXPLORATION;
    }
}

// -----------------------------------------------------------------------------
// Game over - direct port of petitMonstre.ino's own GameOverScreen(), with
// one real fix - see this file's own header comment on the confirmed
// restart soft-lock.
// -----------------------------------------------------------------------------

void petmInitGameFull()
{
    petmNbVue = 0;
    petmCptArea = 0;
    petmCptKill = 0;
    petmClearMonster( 0 );
    petmInitGame();
}

void petmUpdateGameOver()
{
    gbSetFont( gbFont5x7 );
    petmPrintlnStr( "Game over" );
    petmDrawGameStats();
    gbPrintString( "Press :" );
    gbDrawChar( 21, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    petmPrintlnStr( " to continue" );
    gbDrawChar( 23, gbCursorX, gbCursorY );
    gbCursorX = gbCursorX + gbFontSize * gbFontWidth;
    petmPrintlnStr( " to main menu" );

    if( gbPressed( BTN_A ) )
    {
        petmInitGameFull();
        petmState = PETM_ST_START_SCENE;
    }
    // real upstream: gb.buttons.pressed(BTN_C) -> gb.changeGame() - no
    // equivalent, dropped per this file's own established precedent
    // (petmUpdateMainMenu()'s own identical case).
}

// -----------------------------------------------------------------------------
// Top-level dispatch
// -----------------------------------------------------------------------------

void gamePetitMonstre_init()
{
    gbBegin();

    petmPlayerNbMonstre = 0;
    petmPlayerSelected = 255;
    petmFoeNbMonstre = 0;
    petmFoeSelected = 255;
    petmFoeIsMonster = false;

    petmIsDresseur = false;
    petmIsDresseurKill = false;
    petmIsMove = false;

    int i = 0;
    while( i < PETM_NB_MONSTERS )
    {
        petmMonsterVue[ i ] = false;
        petmMonsterCatch[ i ] = false;
        i = i + 1;
    }
    i = 0;
    while( i < PETM_NB_THEMES ) { petmDresseurByTheme[ i ] = 0; i = i + 1; }

    petmNbVue = 0;
    petmNbCatch = 0;
    petmCptArea = 0;
    petmCptKill = 0;

    petmStartCpt = 0;

    petmInitGameFull();
    petmState = PETM_ST_START_SCENE;
}

void gamePetitMonstre_update()
{
    if( !gbUpdate() ) return;

    if( petmState == PETM_ST_START_SCENE ) petmUpdateStartScene();
    else if( petmState == PETM_ST_CHOOSE_STARTER ) petmUpdateChooseStarter();
    else if( petmState == PETM_ST_EXPLORATION ) petmUpdateExplorationTop();
    else if( petmState == PETM_ST_MAINMENU ) petmUpdateMainMenu();
    else if( petmState == PETM_ST_TEAM_FLASH ) petmUpdateTeamFlash();
    else if( petmState == PETM_ST_TEAM_SUMMARY ) petmUpdateTeamSummary();
    else if( petmState == PETM_ST_DEX_SCAN ) petmUpdateDexScan();
    else if( petmState == PETM_ST_DEX_SHOW ) petmUpdateDexShow();
    else if( petmState == PETM_ST_COMBAT ) petmUpdateCombat();
    else if( petmState == PETM_ST_GAMEOVER ) petmUpdateGameOver();

    gbRenderFrame();
}
