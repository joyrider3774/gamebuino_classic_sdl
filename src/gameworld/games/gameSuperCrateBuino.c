#include "avrCompat.h"
#include "machineDependent.h"
#include "gamebuinoShim.h"
#include "eepromShim.h"

// Super Crate Buino (Aurelien Rodot, license: none specified -
// github.com/Rodot/Super-Crate-Buino). A real action-platformer inspired by
// the PC game "Super Crate Box": one arena per map, a steady stream of
// enemies spawns from the top-center and chases the player, and the only
// way to score is to keep grabbing crates that fall from the sky - each
// crate re-rolls the player's current weapon at random (from whatever has
// been unlocked so far) and awards one point. Reaching per-map score
// thresholds unlocks new weapons and, eventually, the next of 5 real maps.
// Dying (touching an active enemy, or a real DISK bullet) resets score to
// zero and respawns on the same map.
//
// STRUCTURAL FLATTENING: real upstream is a small real C++ class hierarchy
// (`Box` as a base class with virtual `getWidth()`/`getHeight()`/
// `getGravity()`/`getMaxSpeed()`/`getXFriction()`/`getYFriction()`/
// `getXBounce()`/`getYBounce()`, and `Bullet`/`Player`/`Enemy`/`Crate` all
// inheriting it and overriding a subset) - this dialect has no classes or
// inheritance at all (see VIRCON32_C_DIALECT.md), so every entity's real
// per-subtype "virtual getter" became a plain function taking whatever
// state it needs as parameters (`scbBulletGetWidth( subtype, vx, timeLeft )`
// etc), and the one real shared physics step (`Box::update()`) became
// `scbBoxUpdate()`, a single function taking every entity's current x/y/vx/
// vy/dir as OUT-parameters (the "everything by pointer" idiom
// VIRCON32_C_DIALECT.md documents for any >1-word return) plus that
// entity's own already-computed gravity/friction/bounce/maxSpeed values -
// called identically by Bullet/Player/Enemy/Crate's own update functions
// below, exactly mirroring how each of their real C++ counterparts called
// the one real inherited `Box::update()`. `Bullet`/`Enemy` (both used as
// real arrays upstream) became `struct ScbBullet`/`struct ScbEnemy` arrays;
// `Player`/`Crate`/`World`/`Weapon` (each only ever a single real instance)
// became plain flat globals. `Weapon.shooter` is always the single real
// player instance in upstream (set once in `Player::init()` and never
// reassigned), so every `shooter->x`/`shooter->getWidth()` call below reads
// the player's own globals directly instead of carrying a "shooter"
// reference of any kind - a simplification that loses nothing, matching
// this project's own established "flatten a real single-instance
// relationship" precedent.
//
// Every real `gb.x.y(...)` call site is mechanically rewritten to a plain
// `gbY(...)` function call (see gamePong.c's own header comment - this
// dialect has no classes/methods). `random(N)`/`random(a,b)` became
// `arand(N)`/`scbArduinoRandom(a,b)` (the latter a direct, deliberate port
// of real Arduino core's own `random(long,long)` - see its own comment
// below for why this exact port, not a naive `a+arand(b-a)` rewrite,
// matters for two real, verified upstream quirks). `byte`/`char` became
// plain `int` throughout (no genuinely-boolean `byte` usage here the way
// gameAgaruino.c had). Every real `switch` statement became an if/else-if
// chain (this dialect's own `switch` support remains unproven - matching
// the same caution gamePong.c/gameTaquin.c/gameUfoRace.c already
// established), which also made every real (and there are several - see
// below) upstream fallthrough behavior something that had to be traced
// through by hand and reproduced explicitly rather than something the
// if/else-if rewrite could get by accident.
//
// REAL BITMAP ART RESTORED VERBATIM: every one of upstream's real
// `PROGMEM` byte tables (the title `logo`, the 5 real tile-bitmaps-encoded-
// as-bitmaps world maps, all 7 real ground/platform/wall tile textures used
// across them, all 13 real weapon sprites plus their 5 real white-highlight
// overlays, the 5-frame player sprite, the 5-frame small-enemy sprite, the
// 6-frame big-enemy sprite, and the crate sprite) was copied byte-for-byte
// into plain `int[]` arrays below - hex literals copied directly, and the
// 5 real world maps (upstream's own `B########`-style binary literals, one
// per byte) converted to the equivalent hex value one-for-one via a small
// verification script (counting each map's own real declared width/height
// against its own real total element count, catching any transcription
// mistake immediately rather than trusting it by eye - every one of the 5
// matched its own expected count exactly). `largeChecker` (a real declared
// `PROGMEM` tile bitmap) and `revolver_sound`/`player_damage_sound` (two
// real declared `PROGMEM` sound patterns) are never referenced anywhere
// else in the real upstream source - confirmed via a direct grep, not
// assumed - so all three are genuinely dead upstream constants, correctly
// omitted here rather than ported as unused data.
//
// MAPS ARE REAL BITMAPS, REUSED DIRECTLY AS BITMAPS: upstream's own real
// `World::tileAtPosition()`/`World::draw()` call `gb.display.
// getBitmapPixel(tiles, x, y)` directly on the very same `map0..map4[]`
// byte arrays also passed to `gb.display.drawBitmap()` for the map-select
// preview - a real, deliberate upstream dual-use of one data format (this
// shim's own `gbGetBitmapPixel()`/`gbDrawBitmap()` share the exact same
// `[width,height,then packed rows]` layout real `Display::getBitmapPixel()`/
// `drawBitmap()` do), so `scbMap0..scbMap4[]` below are genuine drop-in
// replacements needing no reinterpretation, and `scbTileAtPosition()`/
// `scbWorldDraw()` call `gbGetBitmapPixel()` on them exactly like upstream.
//
// EEPROM: upstream's own real `loadEEPROM()`/`saveEEPROM()`/
// `EEPROMreadInt()`/`EEPROMwriteInt()`/`cleanEEPROM()` (a real 2-byte
// token + per-map best-score table + unlocked-weapons/unlocked-maps bytes,
// with a real "only write if the stored value is stale" guard to limit
// wear) port directly onto this shim's own `eeprom_read_byte()`/
// `eeprom_write_byte()` (real address range 0-1023, matching
// `cleanEEPROM()`'s own real 1024-byte wipe loop) - see `scbLoadEeprom()`/
// `scbSaveEeprom()` below, called from this game's own `init()` and from
// every real call site upstream calls them from (every scoring death, the
// pause-menu's own "B: SAVE & QUIT", and once per real gameplay tick from
// the main PLAY state, exactly matching upstream's own `saveEEPROM()`
// call inside `loop()`'s own `if (gb.update())` block).
//
// A REAL CROSS-GAME-LAUNCH RESET THIS PORT NEEDS THAT UPSTREAM NEVER DID:
// this game's own local popup state (`scbPopupTimeLeft`/`scbPopupText` -
// see "POPUP" below for why it is local rather than the shared shim's own
// `gbPopup()`) is explicitly zeroed in `gameSuperCrateBuino_init()`, along
// with `scbShakeTimeLeft`. Real hardware only ever runs one sketch per
// power-on, so upstream never needed this; this cartridge can relaunch the
// same game many times in one session, and without this reset a stale
// nonzero popup timer left over from a previous play session (e.g. a
// "GAME OVER!" message that was still mid-slide-out when the player backed
// out to the top-level menu) would incorrectly resume animating on the
// next launch - the same class of fix `gbFrameCount`/the shared shim's own
// `gbPopupTimeLeft` already needed, documented in this project's own
// CLAUDE.md, just applied here to a second, game-local popup timer that
// mechanism doesn't cover.
//
// POPUP - A LOCAL REIMPLEMENTATION, NOT THE SHARED `gbPopup()`: matching
// `gameMinesweeper.c`'s/`game2048.c`'s own established precedent for
// deliberately NOT migrating to the shared shim primitive when a game has
// a genuine, specific extra requirement it doesn't cover - here, real
// `Player::kill()` explicitly force-cancels whatever popup is currently
// showing (`popupTimeLeft = 0;`) before deciding whether to show a fresh
// "NEW HIGHSCORE!" one, and the shared `gbPopup()`'s own internal timer
// isn't exposed for a game to reset directly. `scbPopup()`/
// `scbUpdatePopup()` below are instead a direct, hand-ported copy of real
// upstream's own `popup()`/`updatePopup()` (the same real slide-in-from-
// the-bottom, auto-dismissing bordered box, built from the same
// `gbDrawRect()`/`gbFillRect()`/`gbPrintString()` primitives), giving this
// game the real force-cancel capability upstream's own upstream code
// relies on.
//
// SOUND - APPROXIMATED WITH ONE-SHOT PRIMITIVES, DOCUMENTED: this shim has
// no pattern/track player (`gbPlayNote()`/`gbPlayTick()`/`gbPlayOK()`/
// `gbPlayCancel()` only - see gamebuinoShim.h's own header comment), while
// upstream calls real `gb.sound.playPattern(name_sound, channel)` for
// every weapon-fire/explosion/enemy/jump/pickup effect (13 real distinct
// `PROGMEM` note patterns). Matching gameUfoRace.c's own established
// "approximate with the closest one-shot primitive" treatment: every real
// `playPattern()` call site below instead plays one representative
// `gbPlayNote(pitch, duration)` (a distinct pitch per effect, picked purely
// for audible variety, not derived from the real pattern's own actual
// note data) or, where upstream itself already only calls a one-shot
// primitve (`playTick()`/`playOK()`), that same real call unchanged.
// `gb.sound.chanVolumes[2] = 1;` (a real 3-channel pattern-mixer volume
// tweak, upstream's own setup()-time comment: "this game requires 3
// channels...") has no equivalent at all in this shim (no per-channel
// volume, no multi-channel pattern engine) and is dropped outright,
// matching this project's established norm for a feature with no shim
// equivalent. `gb.pickRandomSeed()`/`gb.battery.show = false;` are dropped
// too, matching gamePong.c's own identical treatment of both.
//
// SEVERAL REAL UPSTREAM BUGS/QUIRKS, TRACED THROUGH (NOT ASSUMED) AND
// PRESERVED EXACTLY, PER THIS PROJECT'S OWN ESTABLISHED NORM:
//
// 1) `Box::update()`'s own real Y-axis collision-resolution block is gated
//    on `if (getXBounce() >= 0)` - the SAME condition guarding the X-axis
//    block above it, not `getYBounce() >= 0` (confirmed directly against
//    the real source, not assumed a typo away). Traced through for every
//    entity that ever returns a negative `getXBounce()` (dead Player,
//    Bullet subtypes CLUB/EXPLOSION/LASER, dead Enemy): every one of them
//    ends up with ZERO real Y-axis world collision at all while that
//    condition holds, letting a dead player's/dead enemy's ragdoll (and a
//    club swing's/explosion's/laser bolt's own bullet hitbox) fall or fly
//    straight through floors and walls - a real, visible, load-bearing
//    gameplay effect (the death "fling" clipping through the map), not an
//    inert corner case, so `scbBoxUpdate()` below gates both its X and Y
//    resolution blocks on the same `xBounce` parameter, exactly like real
//    `Box::update()` does.
//
// 2) `Weapon::addBullet()`'s own real screen-shake `switch` has a genuine
//    fallthrough: `case W_SNIPER: case W_REVOLVER: shakeTimeLeft=4;
//    shakeAmplitude=4; case W_MACHINEGUN: shakeTimeLeft=2;
//    shakeAmplitude=1;` - SNIPER/REVOLVER's own distinct heavier shake
//    value is set, then immediately overwritten by falling into
//    MACHINEGUN's own weaker one (no `break`). Net real effect: all three
//    weapons produce the exact same (weaker) shake. `scbWeaponAddBullet()`
//    below reproduces this by setting the MACHINEGUN values directly for
//    all three subtypes, rather than the SNIPER/REVOLVER values the
//    upstream `case` labels suggest were intended.
//
// 3) The same function's own real initial-bullet-speed `switch` has a
//    second fallthrough: `case W_MACHINEGUN: vx=...; vy=random(-16,17);
//    shooter->vx -= shooter->dir*32; case W_SHOTGUN: vx=...;
//    vy=random(-10,11); break;` - MACHINEGUN's own distinct vy spread
//    (-16..17) is computed (and the real player-recoil side effect fires,
//    using the pre-fallthrough `shooter->dir`), then immediately discarded
//    by falling into SHOTGUN's own fresh recompute. Net real effect:
//    MACHINEGUN bullets get the exact same vx/vy DISTRIBUTION as SHOTGUN
//    bullets (just an independent random draw), while still applying the
//    real recoil kick SHOTGUN itself never gets. Reproduced exactly below.
//
// 4) The same function's own real horizontal-offset `switch` has a THIRD
//    fallthrough: `case W_SHELL: x-=16; case W_ROCKET: case W_CLUB:
//    x-=dir*32; case W_MINE: break; default: x+=dir*46;` - SHELL genuinely
//    gets BOTH its own `-16` offset AND the ROCKET/CLUB `-dir*32` offset
//    applied on top of it (not a bug exactly, but a real cascade worth
//    calling out) - reproduced exactly (`x -= 16; x -= dir*32;` for SHELL
//    specifically, both applied).
//
// 5) Real `Bullet::getDamage()`'s own `switch (subtype) { return 1; case
//    W_REVOLVER: ... }` has a leading `return 1;` statement with no `case`
//    label of its own, positioned before the first real case label - under
//    real C/C++ switch semantics this is unreachable dead code (a `switch`
//    jumps straight to whichever label matches; nothing ever "falls into"
//    the very top of the body unless a label sits there) - the function's
//    real reachable behavior is exactly its own explicit `case`s plus its
//    own `default: return 1;` at the bottom. `scbBulletGetDamage()` below
//    implements only the real reachable logic.
//
// 6) Real Arduino core's own `random(long howsmall, long howbig)`
//    (`WMath.cpp`) has a real, defined short-circuit: if `howsmall >=
//    howbig`, it returns `howsmall` UNMODIFIED, with no random draw at
//    all. `scbArduinoRandom()` below is a direct, deliberate port of that
//    exact function (not a naive `min + arand(max-min)` rewrite) because
//    two real upstream call sites depend on this short-circuit for their
//    own real, verified behavior, not just a hypothetical edge case:
//      - `EnemiesEngine::update()`'s own real `enemies[i].vy =
//        random(-48, -64);` (arguments reversed from the "obviously
//        intended" `random(-64,-48)`, presumably a real upstream typo) -
//        under the real short-circuit this ALWAYS evaluates to exactly
//        `-48`, not a randomized upward launch speed at all. A killed
//        enemy's own vertical launch velocity is therefore a real
//        constant on real hardware too, not a range - preserved exactly.
//      - `Crate::update()`'s own real weapon-reroll `random(1,
//        unlockedWeapons+1)`: while `unlockedWeapons==0` (true for every
//        map before its own first weapon unlock), `howsmall(1) >=
//        howbig(1)` holds, so this also always evaluates to the constant
//        `1` - though this particular case is provably inert either way,
//        since the surrounding `% (unlockedWeapons+1)` is `% 1`, which is
//        always `0` regardless of the random draw's own value.
//
// 7) Because of bug/quirk 6 immediately above, real upstream's own weapon-
//    name-popup `switch (player.weapon.subtype)` (in `Crate::update()`)
//    has no `case W_PISTOL:` at all - and since the reroll can only ever
//    actually land on subtype 0 (PISTOL) while `unlockedWeapons==0`
//    (every subsequent reroll's own real range is `[1,unlockedWeapons]`,
//    never touching 0 again), this is exactly the one subtype the reroll
//    can produce that has no matching popup case. Net real effect: the
//    very first crate pickup on a fresh map0 (before anything is unlocked)
//    silently shows NO weapon-name popup at all (score still increments,
//    the popup logic below just has nothing to show) - preserved exactly,
//    no `case`/`else if` added for PISTOL in `scbCrateUpdate()` below.
//
// 8) `loop()`'s own real camera-follow gating literally reads
//    `world.getWidth()*SCALE <= LCDWIDTH` / `...getHeight()*SCALE <=
//    LCDHEIGHT` - `getWidth()`/`getHeight()` already have one real `SCALE`
//    factor baked in (`SPRITE_SIZE*tiles_wide*SCALE`), so multiplying by
//    `SCALE` again yields a number in the tens of thousands for every one
//    of the 5 real shipped maps, meaning this condition is realistically
//    always false (a likely real upstream typo - probably meant `/SCALE`)
//    - but verified provably inert for all 5 real maps either way (even
//    the "corrected" divide-based version stays false for every one of
//    them too, since even the smallest map's own screen-space width/height
//    already exceeds LCDWIDTH/LCDHEIGHT), so ported literally
//    (`scbWorldGetWidth() * SCB_SCALE <= LCDWIDTH`) rather than "fixed".
//
// 9) `World::draw()`'s own real edge/corner-tile detection runs two
//    independent, non-exclusive `if` checks (right-neighbor-empty sets
//    `flip=FLIPH; offset=2`; left-neighbor-empty sets `bitmap=edge` again
//    but does NOT reset `flip`/`offset`) - for the (real, if rare) case of
//    an isolated single-tile-wide platform where BOTH neighbors are empty,
//    the tile is drawn using the FIRST check's own flip/offset regardless.
//    Ported as the same two sequential, non-exclusive `if`s (no `else`,
//    no reset) in `scbWorldDraw()` below.
//
// 10) `loop()`'s own real camera-smoothing code (a 3-line `cameraX =
//     (3*cameraX+x)/4` EMA blend) is entirely commented out in upstream's
//     own shipped source (`//int x = ...; //cameraX = ...`) - real
//     shipped behavior is an instant, unsmoothed camera snap every tick
//     (the two lines immediately below it, which are NOT commented out).
//     Not ported here either, matching the real shipped behavior exactly
//     (this is not a simplification on this port's part - the dead code
//     was already dead in the real, distributed upstream source).
//
// DROPPED WITH NO OBSERVABLE EFFECT: `World::draw()`'s own real `byte
// tileNumber = 1; //platform by default` is assigned once and never read
// again anywhere in that function - genuinely dead, dropped here too.

#define SCB_SCALE 8
#define SCB_SPRITE_SIZE 6

#define SCB_NUM_MAPS 5
#define SCB_NUM_BULLETS 20
#define SCB_NUM_ENEMIES 20
#define SCB_NUM_WEAPONS 13
#define SCB_NUM_THRESHOLDS 5

// weapon/bullet subtypes - real upstream `W_*`/`E_*` defines
#define SCB_W_PISTOL 0
#define SCB_W_RIFLE 1
#define SCB_W_SHOTGUN 2
#define SCB_W_ROCKET 3
#define SCB_W_CLUB 4
#define SCB_W_REVOLVER 5
#define SCB_W_MINE 6
#define SCB_W_SNIPER 7
#define SCB_W_MACHINEGUN 8
#define SCB_W_GRENADE 9
#define SCB_W_AKIMBO 10
#define SCB_W_DISK 11
#define SCB_W_LASER 12
#define SCB_W_EXPLOSION 13
#define SCB_W_SHELL 14

#define SCB_E_SMALL 0
#define SCB_E_BIG 1

#define SCB_SCORETHRESHOLD_1 4
#define SCB_SCORETHRESHOLD_2 8
#define SCB_SCORETHRESHOLD_3 12
#define SCB_SCORETHRESHOLD_4 14
#define SCB_SCORETHRESHOLD_5 16

#define SCB_EEPROM_TOKEN 0xCAB2
#define SCB_EEPROM_WEAPONS_OFFSET 4
#define SCB_EEPROM_MAPS_OFFSET 6
#define SCB_EEPROM_SCORE_OFFSET 32

// -----------------------------------------------------------------------------
// Real upstream bitmap art, byte-for-byte (see this file's own header
// comment on how the 5 maps' own real `B########`-style binary literals
// were converted to hex, and how everything else needed no conversion at
// all since it was already declared in hex).
// -----------------------------------------------------------------------------

int scbLogoBitmap[242]= {
    64, 30, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xF0, 0x0, 0x0, 0x0, 0x81, 0x32, 0x4, 0x8, 0x10, 0x0, 0x0, 0x0, 0x9F, 0x32, 0x64,
    0xF9, 0x90, 0x0, 0x0, 0x0, 0x81, 0x32, 0x4, 0x38, 0x30, 0x0, 0x0, 0x0, 0xF9, 0x32, 0x7C,
    0xF9, 0x90, 0x0, 0x0, 0x0, 0x81, 0x2, 0x7C, 0x9, 0x90, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xF0, 0x0, 0x0, 0x0, 0x81, 0x2, 0x4, 0x8, 0x10, 0x0, 0x0, 0x0, 0x9F, 0x32, 0x67,
    0x39, 0xF0, 0x0, 0x0, 0x0, 0x9F, 0x6, 0x7, 0x38, 0x70, 0x0, 0x0, 0x0, 0x9F, 0x32, 0x67,
    0x39, 0xF0, 0x0, 0x0, 0x0, 0x81, 0x32, 0x67, 0x38, 0x10, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xF0, 0x0, 0x0, 0x0, 0x60, 0x4C, 0x93, 0x20, 0x60, 0x60, 0x0, 0x0, 0x26, 0x4C, 0x91,
    0x26, 0x40, 0x1, 0xF8, 0x0, 0x20, 0xCC, 0x92, 0x26, 0x40, 0xD, 0xF8, 0x20, 0xA6, 0x4C, 0x93,
    0x26, 0x40, 0x81, 0xF, 0xE4, 0x20, 0x40, 0x93, 0x20, 0x40, 0x1, 0xBF, 0x80, 0x3F, 0xFF, 0xFF,
    0xFF, 0xC0, 0x1, 0xF8, 0x0, 0x7, 0xFF, 0xFF, 0xFE, 0x0, 0x1, 0xF8, 0x0, 0x0, 0xFF, 0xFF,
    0xF0, 0x0, 0x1, 0xF8, 0x0, 0x0, 0x1F, 0xFF, 0x80, 0x0, 0x1, 0x98, 0x0, 0x0, 0x3, 0xFC,
    0x0, 0x0, 0xFF, 0xFF, 0xF0, 0x0, 0x0, 0x60, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x0, 0x0, 0x0, 0xC3, 0xC, 0x8, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x34, 0xD3, 0x48, 0x0, 0x0, 0x0, 0x0, 0x0, 0xCB, 0x2C, 0xB8, 0x0, 0x0, 0x0,
    0x0, 0x0,
};

// 5 real maps - each encoded as a real bitmap (width tiles, height tiles,
// then packed rows, MSB-first) reused directly by both `gbDrawBitmap()`
// (the map-select preview) and `gbGetBitmapPixel()` (real tile collision) -
// see this file's own header comment.
int scbMap0[22]= {
    16, 10,
    0xFE, 0x7F, 0x80, 0x1, 0x80, 0x1, 0x8F, 0xF1, 0x80, 0x1, 0x80, 0x1, 0xFC, 0x3F, 0x80, 0x1,
    0x80, 0x1, 0x9F, 0xF9,
};

int scbMap1[50]= {
    24, 16,
    0xFF, 0xE7, 0xFF, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0x83,
    0xFF, 0xC1, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0xFC, 0x0, 0x3F, 0x80, 0x0, 0x1, 0x80, 0x0,
    0x1, 0x87, 0xFF, 0xE1, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0xF8, 0x0, 0x1F, 0xFF, 0xC3, 0xFF,
};

int scbMap2[50]= {
    24, 16,
    0xFF, 0xE7, 0xFF, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0x80, 0x3C, 0x1, 0x80, 0x0, 0x1, 0x80,
    0x0, 0x1, 0x87, 0xE7, 0xE1, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0xFC, 0x18, 0x3F, 0xE0, 0x0,
    0x7, 0xE0, 0x0, 0x7, 0xE0, 0xFF, 0x7, 0xE0, 0x0, 0x7, 0xE0, 0x0, 0x7, 0xFF, 0xC3, 0xFF,
};

int scbMap3[50]= {
    24, 16,
    0xFF, 0xE7, 0xFF, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0x83, 0xFF, 0xC1, 0x80,
    0x0, 0x1, 0x80, 0x0, 0x1, 0xF8, 0x0, 0x1F, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0x8F, 0xC3,
    0xF1, 0x80, 0x0, 0x1, 0x80, 0x0, 0x1, 0xF0, 0x3C, 0xF, 0xF3, 0xFF, 0xCF, 0xF3, 0xFF, 0xCF,
};

int scbMap4[98]= {
    32, 24,
    0xFF, 0xFE, 0x7F, 0xFF, 0x80, 0x0, 0x0, 0x1, 0x80, 0x0, 0x0, 0x1, 0xF0, 0x7, 0xE0, 0xF,
    0x80, 0x0, 0x0, 0x1, 0x80, 0x0, 0x0, 0x1, 0x87, 0xFC, 0x3F, 0xE1, 0x80, 0x0, 0x0, 0x1,
    0x80, 0x0, 0x0, 0x1, 0xF0, 0x3, 0xC0, 0x1, 0x80, 0x3, 0xC1, 0xFF, 0x80, 0x3, 0xC0, 0x3F,
    0x8F, 0xFF, 0xC0, 0x3, 0x80, 0x3, 0xFC, 0x3, 0x80, 0x0, 0x0, 0x3, 0xFC, 0x0, 0x0, 0x3,
    0xC0, 0xC, 0x1, 0xFF, 0xC0, 0xC, 0x0, 0x3F, 0xCF, 0xFC, 0x3, 0xF, 0xC0, 0xF, 0x3, 0xF,
    0xC0, 0x3, 0xC0, 0xF, 0xFC, 0x0, 0x0, 0xF, 0xFF, 0xC0, 0x0, 0xF, 0xFF, 0xFF, 0xFF, 0xF,
};

int* scbMaps[SCB_NUM_MAPS]= { scbMap0, scbMap1, scbMap2, scbMap3, scbMap4 };

// real tile textures (8x6 each)
int scbBricks[8]= { 8, 6, 0xFC, 0x24, 0xFC, 0x90, 0xFC, 0x48 };
int scbGrass[8]= { 8, 6, 0xFC, 0x0, 0x0, 0x18, 0xA4, 0x58 };
int scbGrassEdge[8]= { 8, 6, 0x7C, 0x80, 0x80, 0x80, 0x94, 0xE8 };
int scbBeam[8]= { 8, 6, 0xFC, 0x10, 0x28, 0x44, 0x80, 0xFC };
int scbRoundPlatform[8]= { 8, 6, 0xFC, 0x0, 0x0, 0x0, 0xFC, 0xFC };
int scbRoundPlatformEdge[8]= { 8, 6, 0x7C, 0xE0, 0xC0, 0xE0, 0xFC, 0x7C };
int scbBlackWall[8]= { 8, 6, 0xF4, 0xFC, 0xBC, 0xF8, 0xFC, 0xDC };
int scbCrateBitmap[8]= { 8, 6, 0xFC, 0x84, 0xEC, 0xDC, 0x84, 0xFC };

// real weapon sprites (24 wide) and their real white-highlight overlays
int scbClubBitmap[20]= { 24, 6, 0xC0, 0x0, 0x0, 0xF0, 0x0, 0x0, 0x7C, 0x0, 0x0, 0x1F, 0x0, 0x0, 0x7, 0x80, 0x0, 0x1, 0x80, 0x0 };
int scbPistolBitmap[11]= { 24, 3, 0x0, 0x0, 0xF0, 0x0, 0x1, 0xE0, 0x0, 0x1, 0x0 };
int scbLaserBitmap[17]= { 24, 5, 0x0, 0x0, 0x80, 0x0, 0xFF, 0xA8, 0x0, 0xFF, 0xFC, 0x0, 0xFF, 0xA8, 0x0, 0x0, 0x80 };
int scbRevolverBitmap[14]= { 24, 4, 0x0, 0x0, 0x80, 0x0, 0x0, 0xF8, 0x0, 0x1, 0xF8, 0x0, 0x1, 0xC0 };
int scbRifleBitmap[14]= { 24, 4, 0x0, 0x0, 0x4, 0x0, 0x21, 0xFC, 0x0, 0x37, 0xF0, 0x0, 0x31, 0x0 };
int scbRifleWhiteBitmap[11]= { 24, 3, 0x0, 0x0, 0x0, 0x0, 0x1E, 0x0, 0x0, 0x8, 0x0 };
int scbSniperBitmap[14]= { 24, 4, 0x0, 0x0, 0xC0, 0x0, 0xFF, 0xFE, 0x0, 0xFF, 0xE0, 0x0, 0xE3, 0x0 };
int scbShotgunBitmap[14]= { 24, 4, 0x0, 0x0, 0x10, 0x0, 0x63, 0xF0, 0x0, 0x7F, 0xF0, 0x0, 0x7C, 0x0 };
int scbShotgunWhiteBitmap[8]= { 24, 2, 0x0, 0x0, 0x0, 0x0, 0x1C, 0x0 };
int scbMachinegunBitmap[17]= { 24, 5, 0x0, 0x0, 0x10, 0x0, 0xC7, 0xF0, 0x0, 0xC7, 0xF0, 0x0, 0xC7, 0xC0, 0x0, 0xFC, 0x0 };
int scbMachinegunWhiteBitmap[14]= { 24, 4, 0x0, 0x0, 0x0, 0x0, 0x38, 0x0, 0x0, 0x38, 0x0, 0x0, 0x38, 0x0 };
int scbDiskBitmap[17]= { 24, 5, 0x0, 0x0, 0x20, 0x0, 0xFF, 0xF0, 0x0, 0xFC, 0x0, 0x0, 0xFC, 0x0, 0x0, 0xFF, 0xF0 };
int scbRocketBitmap[17]= { 24, 5, 0x1, 0x0, 0x20, 0x1, 0xFF, 0xE0, 0x1, 0xD7, 0xE0, 0x1, 0xFF, 0xE0, 0x1, 0x0, 0x20 };
int scbGrenadeBitmap[14]= { 24, 4, 0x0, 0xC7, 0xE0, 0x0, 0xFF, 0xE0, 0x0, 0xC7, 0xE0, 0x0, 0x7C, 0x0 };
int scbGrenadeWhiteBitmap[11]= { 24, 3, 0x0, 0x38, 0x0, 0x0, 0x0, 0x0, 0x0, 0x38, 0x0 };

// real player sprite (5 walk-cycle frames, 8x9 each)
int scbPlayerBitmap[5][11]= {
    { 8, 9, 0x0, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x66 },
    { 8, 9, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x3C, 0xC },
    { 8, 9, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x38, 0x38, 0x18 },
    { 8, 9, 0x0, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x1C, 0x1C, 0x18 },
    { 8, 9, 0x0, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x30 },
};

// real small-enemy sprite (5 frames, 8x8 each)
int scbSmallEnemyBitmap[5][10]= {
    { 8, 8, 0x0, 0x7E, 0x6C, 0x6C, 0x7E, 0x7E, 0x7E, 0x66 },
    { 8, 8, 0x7E, 0x6C, 0x6C, 0x7E, 0x7E, 0x3C, 0x3C, 0xC },
    { 8, 8, 0x7E, 0x6C, 0x6C, 0x7E, 0x7E, 0x38, 0x38, 0x18 },
    { 8, 8, 0x7E, 0x6C, 0x6C, 0x7E, 0x7E, 0x1C, 0x1C, 0x18 },
    { 8, 8, 0x0, 0x7E, 0x6C, 0x6C, 0x7E, 0x7E, 0x3C, 0x30 },
};

// real big-enemy sprite (6 frames, 16x10 each)
int scbBigEnemyBitmap[6][22]= {
    { 16, 10, 0x0, 0x0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3D, 0xA0, 0x3D, 0xA0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x38, 0xE0, 0x38, 0xE0 },
    { 16, 10, 0x0, 0x0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3D, 0xA0, 0x3D, 0xA0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x1, 0xC0 },
    { 16, 10, 0x3F, 0xE0, 0x3F, 0xE0, 0x3D, 0xA0, 0x3D, 0xA0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0xF, 0x0, 0x7, 0x0 },
    { 16, 10, 0x3F, 0xE0, 0x3F, 0xE0, 0x3D, 0xA0, 0x3D, 0xA0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x7, 0x80, 0x7, 0x0 },
    { 16, 10, 0x3F, 0xE0, 0x3F, 0xE0, 0x3D, 0xA0, 0x3D, 0xA0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0xF, 0x80, 0xE, 0x0 },
    { 16, 10, 0x3F, 0xE0, 0x3F, 0xE0, 0x3D, 0xA0, 0x3D, 0xA0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x3F, 0xE0, 0x1D, 0xC0, 0x1C, 0x0 },
};

int scbScoreThresholds[SCB_NUM_THRESHOLDS]= {
    SCB_SCORETHRESHOLD_1, SCB_SCORETHRESHOLD_2, SCB_SCORETHRESHOLD_3, SCB_SCORETHRESHOLD_4, SCB_SCORETHRESHOLD_5
};

// real upstream `"\21 Select map \20"` - \21/\20 (octal, ASCII 17/16) are
// real Gamebuino icon glyphs (the same low-ASCII custom-icon range other
// ported games' own restored text already uses) - built as an explicit
// int array since a quoted string literal can't hold a non-printable
// low-ASCII code.
char scbSelectMapText[15]= { 17, 32, 83, 101, 108, 101, 99, 116, 32, 109, 97, 112, 32, 16, 0 };

// -----------------------------------------------------------------------------
// Global state - see this file's own header comment on why every real
// single-instance upstream class (World/Player/Weapon/Crate) is flattened
// to plain globals here, while Bullet/Enemy (real upstream arrays) become
// struct arrays.
// -----------------------------------------------------------------------------

int scbCameraX, scbCameraY, scbShakeTimeLeft, scbShakeAmplitude;

int scbPopupTimeLeft;
char* scbPopupText;

// World - the currently selected/playing map (also the currently
// highlighted map while on the map-select screen - matches real upstream's
// own single `mapNumber` field serving both purposes).
int* scbTiles;
int* scbWallBitmap;
int* scbPlatformBitmap;
int* scbEdgeBitmap;
int scbHasEdge;
int scbWorldMapNumber;
int scbScore[SCB_NUM_MAPS];
int scbUnlockedWeapons;
int scbUnlockedMaps;
int scbChooseMapIndex;

typedef struct
{
    int x, y, vx, vy, dir;
    int subtype;
    int timeLeft;
} ScbBullet;
ScbBullet scbBullets[SCB_NUM_BULLETS];

int scbWeaponSubtype;
int scbWeaponCooldown;

int scbPlayerX, scbPlayerY, scbPlayerVx, scbPlayerVy, scbPlayerDir;
int scbPlayerScore;
int scbPlayerDead;
int scbPlayerJumping;
int scbPlayerDoubleJumped;

typedef struct
{
    int x, y, vx, vy, dir;
    int subtype;
    int active;
    int health;
} ScbEnemy;
ScbEnemy scbEnemies[SCB_NUM_ENEMIES];
int scbNextSpawnCount;

int scbCrateX, scbCrateY, scbCrateVx, scbCrateVy, scbCrateDir;

int scbGameOverCount;

enum ScbState
{
    SCB_STATE_TITLE = 0,
    SCB_STATE_CHOOSEMAP = 1,
    SCB_STATE_PLAY = 2,
    SCB_STATE_PAUSED = 3,
    SCB_STATE_GAMEOVER = 4
};
int scbState;

// -----------------------------------------------------------------------------
// Small helpers - Arduino macro stand-ins this dialect has no equivalent
// for (no ternary operator, no macros with side-effect-bearing arguments).
// -----------------------------------------------------------------------------

int scbConstrain( int amt, int lo, int hi )
{
    if( amt < lo ) return lo;
    if( amt > hi ) return hi;
    return amt;
}

// real Arduino `map(value, fromLo, fromHi, toLo, toHi)`.
int scbMap( int value, int fromLo, int fromHi, int toLo, int toHi )
{
    return ( value - fromLo ) * ( toHi - toLo ) / ( fromHi - fromLo ) + toLo;
}

// Direct port of real Arduino core's own `random(long howsmall, long
// howbig)` (`WMath.cpp`), INCLUDING its own real `howsmall >= howbig`
// short-circuit (returns `howsmall` unmodified, no random draw at all) -
// see this file's own header comment (quirks 6/7) for two real, verified
// upstream call sites whose own real behavior depends on this exact
// short-circuit, not a generic "clamp then randomize" rewrite.
int scbArduinoRandom( int minVal, int maxVal )
{
    if( minVal >= maxVal )
      return minVal;
    return minVal + arand( maxVal - minVal );
}

int scbToScreenX( int x )
{
    return x / SCB_SCALE - scbCameraX;
}

int scbToScreenY( int y )
{
    return y / SCB_SCALE - scbCameraY;
}

// Direct port of real `Box::isOffScreen()`.
int scbIsOffScreen( int x, int y, int w, int h )
{
    if( ( scbToScreenX( x ) + scbToScreenX( x + w ) ) < 0 ) return 1;
    if( scbToScreenX( x ) > LCDWIDTH ) return 1;
    if( ( scbToScreenY( y ) + scbToScreenY( y + h ) ) < 0 ) return 1;
    if( scbToScreenY( y ) > LCDHEIGHT ) return 1;
    return 0;
}

// Direct port of real `Box::draw()` (the generic fillRect fallback - only
// `Bullet` ever actually uses this one; `Player`/`Enemy`/`Crate` all draw
// their own real bitmap instead).
void scbBoxDrawRect( int x, int y, int w, int h )
{
    if( scbIsOffScreen( x, y, w, h ) ) return;
    gbFillRect( scbToScreenX( x ), scbToScreenY( y ), w / SCB_SCALE, h / SCB_SCALE );
}

// Direct port of real `World::tileAtPosition()`.
int scbTileAtPosition( int x, int y )
{
    int tileX = ( x - SCB_SCALE / 2 ) / SCB_SPRITE_SIZE / SCB_SCALE;
    int tileY = ( y - SCB_SCALE / 2 ) / SCB_SPRITE_SIZE / SCB_SCALE;
    int w = scbTiles[ 0 ];
    int h = scbTiles[ 1 ];

    if( tileX < 0 || tileX >= w || tileY < 0 || tileY >= h )
      return 0;

    if( gbGetBitmapPixel( scbTiles, tileX, tileY ) )
      return 1;
    return 0;
}

// Direct port of real `World::solidCollisionAtPosition()`.
int scbWorldSolidCollision( int x, int y, int w, int h )
{
    if( scbTileAtPosition( x, y + h ) ) return 1;
    if( scbTileAtPosition( x + w, y + h ) ) return 1;
    if( scbTileAtPosition( x + w, y ) ) return 1;
    if( scbTileAtPosition( x, y ) ) return 1;

    if( w > SCB_SPRITE_SIZE * SCB_SCALE )
    {
        if( scbTileAtPosition( x + w / 2, y ) ) return 1;
        if( scbTileAtPosition( x + w / 2, y + h ) ) return 1;
    }
    if( h > SCB_SPRITE_SIZE * SCB_SCALE )
    {
        if( scbTileAtPosition( x, y + h / 2 ) ) return 1;
        if( scbTileAtPosition( x + w, y + h / 2 ) ) return 1;
    }
    return 0;
}

// Direct ports of real `World::getWidth()`/`getHeight()`.
int scbWorldGetWidth()
{
    return SCB_SPRITE_SIZE * scbTiles[ 0 ] * SCB_SCALE;
}

int scbWorldGetHeight()
{
    return SCB_SPRITE_SIZE * scbTiles[ 1 ] * SCB_SCALE;
}

// Direct port of real `World::addScore()`.
int scbWorldAddScore( int newScore )
{
    if( newScore > scbScore[ scbWorldMapNumber ] )
    {
        scbScore[ scbWorldMapNumber ] = newScore;
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// EEPROM - direct port of real `loadEEPROM()`/`saveEEPROM()`/
// `EEPROMreadInt()`/`EEPROMwriteInt()`/`cleanEEPROM()` onto this shim's own
// `eeprom_read_byte()`/`eeprom_write_byte()` - see this file's own header
// comment.
// -----------------------------------------------------------------------------

int scbEepromReadInt( int addr )
{
    int value = eeprom_read_byte( addr + 1 ) & 0xFF;
    value = value + ( ( eeprom_read_byte( addr ) << 8 ) & 0xFF00 );
    return value;
}

void scbEepromWriteInt( int addr, int value )
{
    eeprom_write_byte( addr + 1, value & 0xFF );
    eeprom_write_byte( addr, ( value >> 8 ) & 0xFF );
}

void scbCleanEeprom()
{
    int i;
    for( i = 0; i < 1024; i++ )
      if( eeprom_read_byte( i ) )
        eeprom_write_byte( i, 0 );
}

void scbLoadEeprom()
{
    if( scbEepromReadInt( 0 ) != SCB_EEPROM_TOKEN )
    {
        scbCleanEeprom();
        scbUnlockedWeapons = 0;
        scbUnlockedMaps = 0;
        scbWorldMapNumber = 0;
        int i;
        for( i = 0; i < SCB_NUM_MAPS; i++ )
          scbScore[ i ] = 0;
        return;
    }

    int i;
    for( i = 0; i < SCB_NUM_MAPS; i++ )
      scbScore[ i ] = scbEepromReadInt( i * 2 + SCB_EEPROM_SCORE_OFFSET );

    scbUnlockedWeapons = eeprom_read_byte( SCB_EEPROM_WEAPONS_OFFSET );
    scbUnlockedMaps = eeprom_read_byte( SCB_EEPROM_MAPS_OFFSET );
    scbWorldMapNumber = scbUnlockedMaps; // real upstream - select the last unlocked map by default
}

void scbSaveEeprom()
{
    scbEepromWriteInt( 0, SCB_EEPROM_TOKEN );

    int i;
    for( i = 0; i < SCB_NUM_MAPS; i++ )
      if( scbEepromReadInt( i * 2 + SCB_EEPROM_SCORE_OFFSET ) < scbScore[ i ] )
        scbEepromWriteInt( i * 2 + SCB_EEPROM_SCORE_OFFSET, scbScore[ i ] );

    if( eeprom_read_byte( SCB_EEPROM_WEAPONS_OFFSET ) < scbUnlockedWeapons )
      eeprom_write_byte( SCB_EEPROM_WEAPONS_OFFSET, scbUnlockedWeapons );
    if( eeprom_read_byte( SCB_EEPROM_MAPS_OFFSET ) < scbUnlockedMaps )
      eeprom_write_byte( SCB_EEPROM_MAPS_OFFSET, scbUnlockedMaps );
}

// -----------------------------------------------------------------------------
// Popup - see this file's own header comment on why this is a local
// reimplementation rather than the shared shim's own `gbPopup()`.
// -----------------------------------------------------------------------------

void scbPrintCentered( char* text )
{
    gbCursorX = ( LCDWIDTH / 2 ) - ( strlen( text ) * gbFontSize * gbFontWidth / 2 );
    gbPrintString( text );
}

void scbPopup( char* text, int duration )
{
    scbPopupText = text;
    scbPopupTimeLeft = duration + 12;
}

void scbUpdatePopup()
{
    if( scbPopupTimeLeft )
    {
        int yOffset = 0;
        if( scbPopupTimeLeft < 12 )
          yOffset = scbPopupTimeLeft - 12;

        int width = strlen( scbPopupText ) * gbFontSize * gbFontWidth;
        gbFontSize = 1;
        gbSetColor( GB_BLACK );
        gbDrawRect( LCDWIDTH / 2 - width / 2 - 2, yOffset - 1, width + 2, gbFontHeight + 2 );
        gbSetColor( GB_WHITE );
        gbFillRect( LCDWIDTH / 2 - width / 2 - 1, yOffset - 1, width + 1, gbFontHeight + 1 );
        gbSetColor( GB_BLACK );
        gbCursorY = yOffset;
        scbPrintCentered( scbPopupText );
        scbPopupTimeLeft = scbPopupTimeLeft - 1;
    }
}

// -----------------------------------------------------------------------------
// Box physics - the one real shared step every entity below calls, see
// this file's own header comment (quirk 1) on the real Y-axis-gated-on-
// xBounce behavior this reproduces exactly.
// -----------------------------------------------------------------------------

int scbBoxUpdate( int* x, int* y, int* vx, int* vy, int* dir, int width, int height, int gravity, int maxSpeed, int xFriction, int yFriction, int xBounce, int yBounce )
{
    *vy = *vy + gravity;
    *vx = ( *vx * ( 100 - xFriction ) ) / 100;
    *vy = ( *vy * ( 100 - yFriction ) ) / 100;
    *vx = scbConstrain( *vx, -maxSpeed, maxSpeed );
    *vy = scbConstrain( *vy, -maxSpeed, maxSpeed );

    int collided = 0;

    *x = *x + *vx;
    if( xBounce >= 0 )
    {
        int vxdir = -1;
        if( *vx > 0 ) vxdir = 1;

        if( scbWorldSolidCollision( *x, *y, width, height ) )
        {
            collided = 1;
            while( scbWorldSolidCollision( *x, *y, width, height ) )
              *x = *x - vxdir;
            *vx = -( *vx * xBounce ) / 100;
        }
    }

    *y = *y + *vy;
    if( xBounce >= 0 ) // real upstream gates the Y block on getXBounce() too - see header comment (quirk 1)
    {
        int vydir = -1;
        if( *vy > 0 ) vydir = 1;

        if( scbWorldSolidCollision( *x, *y, width, height ) )
        {
            collided = 1;
            while( scbWorldSolidCollision( *x, *y, width, height ) )
              *y = *y - vydir;
            *vy = -( *vy * yBounce ) / 100;
        }
    }

    if( *vx > 0 ) *dir = 1;
    if( *vx < 0 ) *dir = -1;

    return collided;
}

// -----------------------------------------------------------------------------
// Bullet - real per-subtype "virtual getters" as plain functions.
// -----------------------------------------------------------------------------

int scbBulletGetWidth( int subtype, int vx, int timeLeft )
{
    if( subtype == SCB_W_CLUB ) return 96;
    if( subtype == SCB_W_PISTOL || subtype == SCB_W_AKIMBO || subtype == SCB_W_RIFLE || subtype == SCB_W_SHOTGUN )
      return scbConstrain( gbAbsInt( vx ), 8, 16 );
    if( subtype == SCB_W_MACHINEGUN )
      return scbConstrain( gbAbsInt( vx ), 8, 24 );
    if( subtype == SCB_W_REVOLVER || subtype == SCB_W_SNIPER )
      return scbConstrain( gbAbsInt( vx ), 8, 32 );
    if( subtype == SCB_W_DISK )
      return gbMin( timeLeft * 8, 48 );
    if( subtype == SCB_W_LASER ) return 96;
    if( subtype == SCB_W_GRENADE ) return 24;
    if( subtype == SCB_W_ROCKET ) return 48;
    if( subtype == SCB_W_MINE ) return 32;
    if( subtype == SCB_W_EXPLOSION ) return 256;
    if( subtype == SCB_W_SHELL ) return 16;
    return 32;
}

int scbBulletGetHeight( int subtype, int vx, int timeLeft )
{
    if( subtype == SCB_W_CLUB ) return 16;
    if( subtype == SCB_W_REVOLVER ) return gbMax( scbBulletGetWidth( subtype, vx, timeLeft ) / 2, 8 );
    if( subtype == SCB_W_SNIPER || subtype == SCB_W_SHELL || subtype == SCB_W_LASER ) return 8;
    if( subtype == SCB_W_DISK || subtype == SCB_W_MINE ) return 16;
    if( subtype == SCB_W_ROCKET ) return 24;
    return scbBulletGetWidth( subtype, vx, timeLeft );
}

int scbBulletGetGravity( int subtype, int vx )
{
    if( subtype == SCB_W_GRENADE || subtype == SCB_W_MINE || subtype == SCB_W_SHELL ) return 5;
    if( subtype == SCB_W_CLUB || subtype == SCB_W_DISK || subtype == SCB_W_LASER || subtype == SCB_W_ROCKET || subtype == SCB_W_EXPLOSION ) return 0;
    if( gbAbsInt( vx ) > 16 ) return 0;
    return 2;
}

int scbBulletGetXFriction( int subtype )
{
    if( subtype == SCB_W_SHOTGUN ) return 10;
    if( subtype == SCB_W_CLUB || subtype == SCB_W_DISK || subtype == SCB_W_LASER ) return 0;
    if( subtype == SCB_W_ROCKET ) return -20; // negative so it accelerates - matches real upstream comment
    if( subtype == SCB_W_EXPLOSION ) return 100;
    return 5; // real Box::getXFriction() default
}

int scbBulletGetXBounce( int subtype )
{
    if( subtype == SCB_W_DISK ) return 100;
    if( subtype == SCB_W_CLUB || subtype == SCB_W_EXPLOSION || subtype == SCB_W_LASER ) return -1; // don't collide the world
    if( subtype == SCB_W_GRENADE || subtype == SCB_W_SHELL ) return 80;
    if( subtype == SCB_W_ROCKET ) return 0;
    return 30;
}

int scbBulletGetYBounce( int subtype )
{
    if( subtype == SCB_W_GRENADE || subtype == SCB_W_SHELL )
      return scbBulletGetXBounce( subtype );
    return 0;
}

// real `Bullet::getDamage()`'s own leading, label-less `return 1;` is
// unreachable dead code under real switch semantics - see this file's own
// header comment (quirk 5). This implements only the real reachable logic.
int scbBulletGetDamage( int subtype )
{
    if( subtype == SCB_W_REVOLVER || subtype == SCB_W_CLUB ) return 2;
    if( subtype == SCB_W_SNIPER || subtype == SCB_W_DISK || subtype == SCB_W_EXPLOSION || subtype == SCB_W_LASER ) return 10;
    if( subtype == SCB_W_GRENADE || subtype == SCB_W_ROCKET || subtype == SCB_W_MINE || subtype == SCB_W_SHELL ) return 0;
    return 1;
}

int scbBulletGetMaxTimeLeft( int subtype )
{
    if( subtype == SCB_W_CLUB ) return 2;
    if( subtype == SCB_W_SHELL ) return 20;
    if( subtype == SCB_W_MINE || subtype == SCB_W_DISK ) return 100;
    if( subtype == SCB_W_EXPLOSION ) return 5;
    if( subtype == SCB_W_GRENADE || subtype == SCB_W_ROCKET ) return 40;
    return 25;
}

int scbBulletExplodes( int subtype )
{
    if( subtype == SCB_W_GRENADE || subtype == SCB_W_ROCKET || subtype == SCB_W_MINE ) return 1;
    return 0;
}

int scbBulletDestroyOnWorldContact( int subtype )
{
    if( subtype == SCB_W_ROCKET ) return 1;
    return 0;
}

int scbBulletDestroyOnEnemyContact( int subtype )
{
    if( subtype == SCB_W_CLUB || subtype == SCB_W_DISK || subtype == SCB_W_LASER || subtype == SCB_W_EXPLOSION || subtype == SCB_W_SHELL ) return 0;
    return 1;
}

int scbBulletDamagePlayer( int subtype )
{
    if( subtype == SCB_W_DISK ) return 1;
    return 0;
}

// Direct port of real `Bullet::update()`.
void scbBulletUpdate( int i )
{
    if( scbBullets[ i ].timeLeft == 0 ) return;

    int subtype = scbBullets[ i ].subtype;
    int width = scbBulletGetWidth( subtype, scbBullets[ i ].vx, scbBullets[ i ].timeLeft );
    int height = scbBulletGetHeight( subtype, scbBullets[ i ].vx, scbBullets[ i ].timeLeft );
    int gravity = scbBulletGetGravity( subtype, scbBullets[ i ].vx );
    int xFriction = scbBulletGetXFriction( subtype );
    int xBounce = scbBulletGetXBounce( subtype );
    int yBounce = scbBulletGetYBounce( subtype );

    int collided = scbBoxUpdate( &scbBullets[ i ].x, &scbBullets[ i ].y, &scbBullets[ i ].vx, &scbBullets[ i ].vy, &scbBullets[ i ].dir,
                                  width, height, gravity, 128, xFriction, 5, xBounce, yBounce );
    scbBullets[ i ].timeLeft = scbBullets[ i ].timeLeft - 1;

    if( scbBulletDestroyOnWorldContact( subtype ) && collided == 1 )
      scbBullets[ i ].timeLeft = 0;

    if( scbBullets[ i ].timeLeft == 0 && scbBulletExplodes( subtype ) )
    {
        scbBullets[ i ].subtype = SCB_W_EXPLOSION;
        // offset the explosion so it's centered - real upstream computes
        // getWidth()/getHeight() AFTER the subtype change above, so both
        // read the real W_EXPLOSION values (256/256), not the original
        // subtype's own.
        scbBullets[ i ].x = scbBullets[ i ].x - scbBulletGetWidth( SCB_W_EXPLOSION, scbBullets[ i ].vx, scbBullets[ i ].timeLeft ) / 2;
        scbBullets[ i ].y = scbBullets[ i ].y - scbBulletGetHeight( SCB_W_EXPLOSION, scbBullets[ i ].vx, scbBullets[ i ].timeLeft ) / 2;
        scbBullets[ i ].timeLeft = 8;
        scbShakeTimeLeft = 10;
        scbShakeAmplitude = 2;
        gbPlayNote( 25, 8 ); // approximates real blast_sound - see header comment on SOUND
    }
}

// Direct port of real `Bullet::draw()`.
void scbBulletDraw( int i )
{
    if( scbBullets[ i ].timeLeft == 0 ) return;
    if( scbBullets[ i ].subtype == SCB_W_LASER )
      gbSetColor( GB_INVERT );

    int width = scbBulletGetWidth( scbBullets[ i ].subtype, scbBullets[ i ].vx, scbBullets[ i ].timeLeft );
    int height = scbBulletGetHeight( scbBullets[ i ].subtype, scbBullets[ i ].vx, scbBullets[ i ].timeLeft );
    scbBoxDrawRect( scbBullets[ i ].x, scbBullets[ i ].y, width, height );
}

// -----------------------------------------------------------------------------
// Weapon - always owned by the single player instance (see header comment
// on why no "shooter" reference is carried at all).
// -----------------------------------------------------------------------------

int scbWeaponGetMaxCooldown( int subtype )
{
    if( subtype == SCB_W_CLUB ) return 10;
    if( subtype == SCB_W_PISTOL || subtype == SCB_W_AKIMBO || subtype == SCB_W_REVOLVER ) return 0;
    if( subtype == SCB_W_SNIPER ) return 7;
    if( subtype == SCB_W_SHOTGUN ) return 11;
    if( subtype == SCB_W_RIFLE ) return 2;
    if( subtype == SCB_W_MACHINEGUN ) return 1;
    if( subtype == SCB_W_DISK ) return 19;
    if( subtype == SCB_W_LASER ) return 30;
    if( subtype == SCB_W_GRENADE || subtype == SCB_W_ROCKET || subtype == SCB_W_MINE ) return 19;
    return 5;
}

int scbWeaponIsAutomatic( int subtype )
{
    if( subtype == SCB_W_RIFLE || subtype == SCB_W_MACHINEGUN ) return 1;
    return 0;
}

void scbWeaponInit()
{
    scbWeaponCooldown = 0;
    int i;
    for( i = 0; i < SCB_NUM_BULLETS; i++ )
      scbBullets[ i ].timeLeft = 0;
}

// Direct port of real `Weapon::addBullet()` - see this file's own header
// comment (quirks 2/3/4) for the three real fallthrough behaviors
// reproduced exactly below.
void scbWeaponAddBullet( int x, int y, int dir, int subtype )
{
    int i;
    for( i = 0; i < SCB_NUM_BULLETS; i++ )
    {
        if( scbBullets[ i ].timeLeft != 0 ) continue;

        scbBullets[ i ].subtype = subtype;
        scbBullets[ i ].timeLeft = scbBulletGetMaxTimeLeft( subtype );

        // screen shake - quirk 2: SNIPER/REVOLVER's own heavier shake is
        // always overwritten by MACHINEGUN's own weaker values.
        if( subtype == SCB_W_SNIPER || subtype == SCB_W_REVOLVER || subtype == SCB_W_MACHINEGUN )
        {
            scbShakeTimeLeft = 2;
            scbShakeAmplitude = 1;
        }

        // initial bullet speed - quirk 3: MACHINEGUN's own vx/vy is
        // computed (firing the real player-recoil side effect below), then
        // discarded by falling into SHOTGUN's own fresh recompute.
        if( subtype == SCB_W_CLUB )
        {
            scbBullets[ i ].vx = dir * 32;
            scbBullets[ i ].vy = scbPlayerVy;
        }
        else if( subtype == SCB_W_MACHINEGUN || subtype == SCB_W_SHOTGUN )
        {
            if( subtype == SCB_W_MACHINEGUN )
              scbPlayerVx = scbPlayerVx - scbPlayerDir * 32; // player recoil
            scbBullets[ i ].vx = ( dir * 48 ) + scbArduinoRandom( -8, 9 );
            scbBullets[ i ].vy = scbArduinoRandom( -10, 11 );
        }
        else if( subtype == SCB_W_DISK )
        {
            scbBullets[ i ].vx = dir * 26;
            scbBullets[ i ].vy = 0;
        }
        else if( subtype == SCB_W_LASER )
        {
            scbBullets[ i ].vx = dir * 50;
            scbBullets[ i ].vy = 0;
        }
        else if( subtype == SCB_W_GRENADE )
        {
            scbBullets[ i ].vx = ( dir * 32 ) + scbPlayerVx / 2;
            scbBullets[ i ].vy = -32 + scbPlayerVy / 2;
        }
        else if( subtype == SCB_W_ROCKET )
        {
            scbBullets[ i ].vx = dir * 16;
            scbBullets[ i ].vy = 0;
        }
        else if( subtype == SCB_W_MINE )
        {
            scbBullets[ i ].vx = 0;
            scbBullets[ i ].vy = 0;
        }
        else if( subtype == SCB_W_SHELL )
        {
            scbBullets[ i ].vx = -dir * scbArduinoRandom( 16, 24 );
            scbBullets[ i ].vy = scbPlayerVy - scbArduinoRandom( 16, 24 );
        }
        else
        {
            scbBullets[ i ].vx = ( dir * 64 ) + scbArduinoRandom( -8, 9 );
            scbBullets[ i ].vy = scbArduinoRandom( 0, 11 ) - 5;
        }

        // vertical offset
        if( subtype == SCB_W_SHOTGUN || subtype == SCB_W_LASER || subtype == SCB_W_DISK )
          y = y + 32;
        else if( subtype == SCB_W_ROCKET )
          y = y + 16;
        else
          y = y + 24;

        // horizontal offset - quirk 4: SHELL gets BOTH its own -16 offset
        // and the ROCKET/CLUB -dir*32 offset, applied on top of it.
        if( subtype == SCB_W_SHELL )
        {
            x = x - 16;
            x = x - dir * 32;
        }
        else if( subtype == SCB_W_ROCKET || subtype == SCB_W_CLUB )
        {
            x = x - dir * 32;
        }
        else if( subtype == SCB_W_MINE )
        {
            // no offset - matches real upstream's own empty case
        }
        else
        {
            x = x + dir * 46;
        }

        if( dir > 0 )
          x = x + 48; // shooter->getWidth() - the player's own real constant width
        else
          x = x - scbBulletGetWidth( subtype, scbBullets[ i ].vx, scbBullets[ i ].timeLeft );

        x = x + scbPlayerVx / 2;
        scbBullets[ i ].x = x;
        scbBullets[ i ].y = y;

        return;
    }
}

// Direct port of real `Weapon::shoot()`.
void scbWeaponShoot()
{
    scbWeaponCooldown = scbWeaponGetMaxCooldown( scbWeaponSubtype );
    scbWeaponAddBullet( scbPlayerX, scbPlayerY, scbPlayerDir, scbWeaponSubtype );

    if( scbWeaponSubtype == SCB_W_SHOTGUN )
    {
        scbWeaponAddBullet( scbPlayerX, scbPlayerY, scbPlayerDir, scbWeaponSubtype );
        scbWeaponAddBullet( scbPlayerX, scbPlayerY, scbPlayerDir, scbWeaponSubtype );
        scbWeaponAddBullet( scbPlayerX, scbPlayerY, scbPlayerDir, scbWeaponSubtype );
        scbWeaponAddBullet( scbPlayerX, scbPlayerY, scbPlayerDir, scbWeaponSubtype );
    }
    if( scbWeaponSubtype == SCB_W_AKIMBO )
      scbWeaponAddBullet( scbPlayerX, scbPlayerY, -scbPlayerDir, scbWeaponSubtype );

    if( scbWeaponSubtype == SCB_W_RIFLE || scbWeaponSubtype == SCB_W_SNIPER || scbWeaponSubtype == SCB_W_SHOTGUN )
      scbWeaponAddBullet( scbPlayerX, scbPlayerY, scbPlayerDir, SCB_W_SHELL );

    if( scbWeaponSubtype == SCB_W_ROCKET )
      gbPlayNote( 30, 6 );
    else if( scbWeaponSubtype == SCB_W_REVOLVER || scbWeaponSubtype == SCB_W_MACHINEGUN || scbWeaponSubtype == SCB_W_SNIPER )
      gbPlayNote( 50, 2 );
    else if( scbWeaponSubtype == SCB_W_GRENADE || scbWeaponSubtype == SCB_W_DISK )
      gbPlayNote( 40, 3 );
    else if( scbWeaponSubtype == SCB_W_SHOTGUN )
      gbPlayNote( 45, 3 );
    else if( scbWeaponSubtype == SCB_W_MINE )
    {
        // no sound - matches real upstream's own empty case
    }
    else if( scbWeaponSubtype == SCB_W_PISTOL || scbWeaponSubtype == SCB_W_AKIMBO || scbWeaponSubtype == SCB_W_RIFLE )
      gbPlayTick();
    else if( scbWeaponSubtype == SCB_W_LASER )
      gbPlayNote( 70, 4 );
    else if( scbWeaponSubtype == SCB_W_CLUB )
      gbPlayNote( 35, 2 );
}

void scbWeaponUpdate()
{
    int i;
    for( i = 0; i < SCB_NUM_BULLETS; i++ )
      scbBulletUpdate( i );

    if( scbWeaponCooldown > 0 )
      scbWeaponCooldown = scbWeaponCooldown - 1;
    else
    {
        if( scbWeaponIsAutomatic( scbWeaponSubtype ) )
        {
            if( gbRepeat( BTN_A, 1 ) ) scbWeaponShoot();
        }
        else
        {
            if( gbPressed( BTN_A ) ) scbWeaponShoot();
        }
    }
}

// Direct port of real `Weapon::draw()`.
void scbWeaponDraw()
{
    int bx = scbToScreenX( scbPlayerX ) - 9;
    int by = scbToScreenY( scbPlayerY );
    int flip = 0; // NOFLIP
    if( scbPlayerDir <= 0 ) flip = 1; // FLIPH

    int* bitmap = scbClubBitmap;      // harmless placeholder - only read when hasBitmap is true
    int* bitmapWhite = scbClubBitmap; // harmless placeholder - only read when hasBitmapWhite is true
    int hasBitmap = 0;
    int hasBitmapWhite = 0;

    if( scbWeaponSubtype == SCB_W_CLUB )
    {
        if( scbWeaponCooldown > 8 )
        {
            hasBitmap = 0; // don't draw the club when already in use
        }
        else
        {
            bitmap = scbClubBitmap;
            hasBitmap = 1;
            bx = bx + scbPlayerDir * scbWeaponCooldown / 2; // sliding back the club
        }
        by = by - 2;
    }
    else if( scbWeaponSubtype == SCB_W_PISTOL || scbWeaponSubtype == SCB_W_AKIMBO )
    {
        bitmap = scbPistolBitmap; hasBitmap = 1;
        by = by + 3;
    }
    else if( scbWeaponSubtype == SCB_W_REVOLVER )
    {
        bitmap = scbRevolverBitmap; hasBitmap = 1;
        by = by + 2;
    }
    else if( scbWeaponSubtype == SCB_W_SNIPER )
    {
        bitmap = scbSniperBitmap; hasBitmap = 1;
        bitmapWhite = scbShotgunWhiteBitmap; hasBitmapWhite = 1;
        by = by + 2;
        if( scbWeaponCooldown > 4 ) bx = bx - scbPlayerDir;
    }
    else if( scbWeaponSubtype == SCB_W_RIFLE )
    {
        bitmap = scbRifleBitmap; hasBitmap = 1;
        bitmapWhite = scbRifleWhiteBitmap; hasBitmapWhite = 1;
        by = by + 2;
        bx = bx - scbPlayerDir * scbWeaponCooldown;
    }
    else if( scbWeaponSubtype == SCB_W_SHOTGUN )
    {
        bitmap = scbShotgunBitmap; hasBitmap = 1;
        bitmapWhite = scbShotgunWhiteBitmap; hasBitmapWhite = 1;
        by = by + 3;
        bx = bx - scbPlayerDir * scbWeaponCooldown / 4;
    }
    else if( scbWeaponSubtype == SCB_W_MACHINEGUN )
    {
        bitmap = scbMachinegunBitmap; hasBitmap = 1;
        bitmapWhite = scbMachinegunWhiteBitmap; hasBitmapWhite = 1;
        by = by + 3;
    }
    else if( scbWeaponSubtype == SCB_W_DISK )
    {
        bitmap = scbDiskBitmap; hasBitmap = 1;
        by = by + 2;
    }
    else if( scbWeaponSubtype == SCB_W_LASER )
    {
        bitmap = scbLaserBitmap; hasBitmap = 1;
        by = by + 2;
    }
    else if( scbWeaponSubtype == SCB_W_GRENADE )
    {
        bitmap = scbGrenadeBitmap; hasBitmap = 1;
        bitmapWhite = scbGrenadeWhiteBitmap; hasBitmapWhite = 1;
        by = by + 4;
    }
    else if( scbWeaponSubtype == SCB_W_ROCKET )
    {
        bitmap = scbRocketBitmap; hasBitmap = 1;
        by = by + 1;
    }
    // default (MINE): no weapon sprite drawn - matches real upstream's own default case

    if( hasBitmap )
      gbDrawBitmapRotated( bx, by, bitmap, 0, flip ); // NOROT
    if( hasBitmapWhite )
    {
        gbSetColor( GB_WHITE );
        gbDrawBitmapRotated( bx, by, bitmapWhite, 0, flip );
        gbSetColor( GB_BLACK );
    }

    if( scbWeaponSubtype == SCB_W_AKIMBO ) // draw the symmetric pistol in the akimbo case
    {
        int mirrorFlip = ( flip + 1 ) % 2;
        if( hasBitmap )
          gbDrawBitmapRotated( bx, by, bitmap, 0, mirrorFlip );
        if( hasBitmapWhite )
        {
            gbSetColor( GB_WHITE );
            gbDrawBitmapRotated( bx, by, bitmapWhite, 0, mirrorFlip );
            gbSetColor( GB_BLACK );
        }
    }

    if( scbWeaponSubtype == SCB_W_LASER ) // reloading line on the laser
    {
        gbSetColor( GB_WHITE );
        gbDrawFastHLine( scbToScreenX( scbPlayerX ), scbToScreenY( scbPlayerY ) + 4, 6 - scbWeaponCooldown / 5 );
        gbSetColor( GB_BLACK );
    }

    if( scbWeaponSubtype == SCB_W_DISK || scbWeaponSubtype == SCB_W_MINE ) // refill animation
    {
        if( scbPlayerDir > 0 )
          gbFillRect( scbToScreenX( scbPlayerX ) + 6, scbToScreenY( scbPlayerY ) + 4, 4 - scbWeaponCooldown / 4, 2 );
        else
          gbFillRect( scbToScreenX( scbPlayerX ) + scbWeaponCooldown / 4 - 4, scbToScreenY( scbPlayerY ) + 4, 4, 2 );
    }
}

// -----------------------------------------------------------------------------
// Player
// -----------------------------------------------------------------------------

int scbPlayerGetXFriction()
{
    if( scbPlayerDead ) return 10;
    return 40;
}

int scbPlayerGetXBounce()
{
    if( scbPlayerDead ) return -1;
    return 0;
}

// Direct port of real `Player::init()`.
void scbPlayerInit()
{
    scbPlayerX = 128;
    scbPlayerY = 150;
    scbPlayerDir = 1;
    scbPlayerScore = 0;
    scbPlayerDead = 0;
    scbWeaponInit();
}

// Direct port of real `Player::kill()`.
void scbPlayerKill( int dir )
{
    scbPlayerDead = 1;
    scbPlayerVx = dir * 32;
    scbPlayerVy = -32;
    scbPopupTimeLeft = 0;
    if( scbWorldAddScore( scbPlayerScore ) )
      scbPopup( "NEW HIGHSCORE!", 40 );
    scbSaveEeprom();
}

// Direct port of real `Player::update()`.
void scbPlayerUpdate()
{
    if( !scbPlayerDead )
    {
        if( gbRepeat( BTN_LEFT, 1 ) )
        {
            scbPlayerDir = -1;
            scbPlayerVx = scbPlayerVx + 16 * scbPlayerDir;
        }
        if( gbRepeat( BTN_RIGHT, 1 ) )
        {
            scbPlayerDir = 1;
            scbPlayerVx = scbPlayerVx + 16;
        }
        // real `gb.buttons.timeHeld(BTN_DOWN) > 40` -> "held at least 41 ticks"
        if( gbRepeat( BTN_UP, 10 ) && gbHeld( BTN_DOWN, 41 ) )
        {
            scbWeaponSubtype = scbWeaponSubtype + 1;
            scbWeaponSubtype = scbWeaponSubtype % SCB_NUM_WEAPONS;
            scbPlayerScore = 0;
            scbPopup( "WEAPON CHEAT", 20 );
        }

        if( scbPlayerY > scbWorldGetHeight() )
          scbPlayerKill( scbPlayerDir );

        if( scbWorldSolidCollision( scbPlayerX, scbPlayerY + 1, 48, 72 ) )
          scbPlayerDoubleJumped = 0;

        if( gbPressed( BTN_B ) )
        {
            if( scbWorldSolidCollision( scbPlayerX, scbPlayerY + 1, 48, 72 ) )
            {
                scbPlayerVy = -32;
                scbPlayerJumping = 1;
                gbPlayNote( 60, 3 ); // approximates real jump_sound
            }
            else if( !scbPlayerDoubleJumped )
            {
                scbPlayerVy = -32;
                scbPlayerDoubleJumped = 1;
                scbPlayerJumping = 1;
                gbPlayNote( 60, 3 );
            }
        }
        // real `(timeHeld(B) > 0) && (timeHeld(B) < 5)` -> held for between 1 and 4 ticks inclusive
        if( gbHeld( BTN_B, 1 ) && !gbHeld( BTN_B, 5 ) && scbPlayerVy < 0 && scbPlayerJumping )
        {
            if( scbPlayerDoubleJumped )
              scbPlayerVy = scbPlayerVy - 6;
            else
              scbPlayerVy = scbPlayerVy - 12;
        }
        if( scbPlayerVy > 0 )
          scbPlayerJumping = 0;
    }

    scbWeaponUpdate();

    int d = scbPlayerDir;
    int xFriction = scbPlayerGetXFriction();
    int xBounce = scbPlayerGetXBounce();
    scbBoxUpdate( &scbPlayerX, &scbPlayerY, &scbPlayerVx, &scbPlayerVy, &scbPlayerDir, 48, 72, 8, 128, xFriction, 5, xBounce, 0 );
    scbPlayerDir = d; // real upstream override - direction depends only on input, not Box::update()'s own vx-based recalculation
}

// Direct port of real `Player::draw()`.
void scbPlayerDraw()
{
    if( scbIsOffScreen( scbPlayerX, scbPlayerY, 48, 72 ) ) return;

    int flip = 0;
    if( scbPlayerDir <= 0 ) flip = 1;

    int frame = ( scbPlayerDir * scbPlayerX / 32 + 255 ) % 5;
    if( scbPlayerVx == 0 ) frame = 0;
    if( !scbWorldSolidCollision( scbPlayerX, scbPlayerY + 1, 48, 72 ) ) // in the air
    {
        if( scbPlayerVy < 0 ) frame = 4;
        else frame = 1;
    }

    gbDrawBitmapRotated( scbToScreenX( scbPlayerX ) - 1, scbToScreenY( scbPlayerY ), scbPlayerBitmap[ frame ], 0, flip ); // NOROT
    scbWeaponDraw();
}

// -----------------------------------------------------------------------------
// Enemy / EnemiesEngine
// -----------------------------------------------------------------------------

int scbEnemyGetWidth( int subtype )
{
    if( subtype == SCB_E_SMALL ) return 48;
    return 72;
}

int scbEnemyGetHeight( int subtype )
{
    if( subtype == SCB_E_SMALL ) return 64;
    return 80;
}

int scbEnemyGetGravity( int health )
{
    if( health > 0 ) return 4;
    return 10;
}

int scbEnemyGetXBounce( int health )
{
    if( health > 0 ) return 100;
    return -1;
}

int scbEnemyGetMaxHealth( int subtype )
{
    if( subtype == SCB_E_SMALL ) return 2;
    return 10;
}

void scbEnemiesInit()
{
    scbNextSpawnCount = 10;
    int i;
    for( i = 0; i < SCB_NUM_ENEMIES; i++ )
    {
        scbEnemies[ i ].active = 0;
        scbEnemies[ i ].health = 0;
    }
}

// Direct port of real `EnemiesEngine::addEnemy()`.
void scbEnemiesAdd()
{
    int i;
    for( i = 0; i < SCB_NUM_ENEMIES; i++ )
    {
        if( scbEnemies[ i ].active ) continue;

        scbEnemies[ i ].active = 1;
        if( scbArduinoRandom( 0, 6 ) == 0 && scbWorldMapNumber != 0 ) // randomly spawn a few big monsters
          scbEnemies[ i ].subtype = SCB_E_BIG;
        else
          scbEnemies[ i ].subtype = SCB_E_SMALL;

        scbEnemies[ i ].health = scbEnemyGetMaxHealth( scbEnemies[ i ].subtype );
        scbEnemies[ i ].x = scbWorldGetWidth() / 2 - scbEnemyGetWidth( scbEnemies[ i ].subtype ) / 2;
        scbEnemies[ i ].y = 0;
        scbEnemies[ i ].vx = scbArduinoRandom( 0, 2 ) * 20 - 10;
        scbEnemies[ i ].vy = 0;
        return;
    }
}

// Direct port of real `Enemy::update()` - declared `int` upstream but never
// actually returns a value on any path (a harmless real upstream
// declaration quirk - its own return value is never read anywhere it's
// called from either), ported as `void`.
void scbEnemyUpdate( int i )
{
    if( !scbEnemies[ i ].active ) return;

    int subtype = scbEnemies[ i ].subtype;
    int width = scbEnemyGetWidth( subtype );
    int height = scbEnemyGetHeight( subtype );
    int gravity = scbEnemyGetGravity( scbEnemies[ i ].health );
    int xBounce = scbEnemyGetXBounce( scbEnemies[ i ].health );

    scbBoxUpdate( &scbEnemies[ i ].x, &scbEnemies[ i ].y, &scbEnemies[ i ].vx, &scbEnemies[ i ].vy, &scbEnemies[ i ].dir,
                  width, height, gravity, 32, 0, 5, xBounce, 0 );

    if( scbEnemies[ i ].y > scbWorldGetHeight() )
    {
        if( scbEnemies[ i ].health > 0 ) // respawn in "angry" mode when it falls off the bottom of the map
        {
            scbEnemies[ i ].x = scbWorldGetWidth() / 2 - width / 2;
            scbEnemies[ i ].y = 0;
            scbEnemies[ i ].vx = scbEnemies[ i ].dir * 20;
            gbPlayNote( 20, 4 ); // approximates real enemy_felt_sound
        }
        else
          scbEnemies[ i ].active = 0;
    }
}

// Direct port of real `EnemiesEngine::update()`.
void scbEnemiesUpdate()
{
    int j, i;
    for( j = 0; j < SCB_NUM_BULLETS; j++ )
    {
        if( scbBullets[ j ].timeLeft <= 0 ) continue;

        for( i = 0; i < SCB_NUM_ENEMIES; i++ )
        {
            if( scbEnemies[ i ].health <= 0 ) continue;

            // skip bullets with a low speed (falling particles) except
            // explosions/mines/grenades - real upstream `break`s out of
            // the enemy loop here (harmless either way, since this
            // condition never depends on `i`).
            if( gbAbsInt( scbBullets[ j ].vx ) < 20 &&
                !( scbBullets[ j ].subtype == SCB_W_EXPLOSION || scbBullets[ j ].subtype == SCB_W_MINE || scbBullets[ j ].subtype == SCB_W_GRENADE ) )
              break;

            int ew = scbEnemyGetWidth( scbEnemies[ i ].subtype );
            int eh = scbEnemyGetHeight( scbEnemies[ i ].subtype );
            int bw = scbBulletGetWidth( scbBullets[ j ].subtype, scbBullets[ j ].vx, scbBullets[ j ].timeLeft );
            int bh = scbBulletGetHeight( scbBullets[ j ].subtype, scbBullets[ j ].vx, scbBullets[ j ].timeLeft );

            if( gbCollideRectRect( scbEnemies[ i ].x, scbEnemies[ i ].y, ew, eh, scbBullets[ j ].x, scbBullets[ j ].y, bw, bh ) )
            {
                if( scbBulletExplodes( scbBullets[ j ].subtype ) )
                  scbBullets[ j ].timeLeft = 1;
                if( scbBulletDestroyOnEnemyContact( scbBullets[ j ].subtype ) )
                  scbBullets[ j ].vx = ( scbBullets[ j ].vx * scbBulletGetXBounce( scbBullets[ j ].subtype ) ) / 100;

                scbEnemies[ i ].health = scbEnemies[ i ].health - scbBulletGetDamage( scbBullets[ j ].subtype );

                if( scbEnemies[ i ].health <= 0 ) // make the enemy jump when dead
                {
                    int dir;
                    if( scbBullets[ j ].subtype == SCB_W_EXPLOSION ) // fly away from the explosive
                    {
                        if( ( ( scbEnemies[ i ].x + ew / 2 ) - ( scbBullets[ j ].x + bw / 2 ) ) > 0 )
                          dir = 1;
                        else
                          dir = -1;
                    }
                    else // fly in the same direction as the incoming bullet
                    {
                        if( scbBullets[ j ].vx > 0 )
                          dir = 1;
                        else
                          dir = -1;
                    }

                    scbEnemies[ i ].vx = dir * scbArduinoRandom( 24, 32 );
                    // real `random(-48,-64)` - see header comment (quirk 6): min>=max collapses to the constant -48, not a range.
                    scbEnemies[ i ].vy = scbArduinoRandom( -48, -64 );
                    gbPlayNote( 55, 3 ); // approximates real enemy_death_sound
                }
                else if( scbBullets[ j ].subtype == SCB_W_CLUB ) // go away from the player when hit by a club
                {
                    int dir = 1;
                    if( ( scbEnemies[ i ].x + ew / 2 ) - ( scbPlayerX + 48 / 2 ) > 0 )
                      dir = 1;
                    else
                      dir = -1;
                    scbEnemies[ i ].vx = dir * gbAbsInt( scbEnemies[ i ].vx );
                }
            }
        }
    }

    scbNextSpawnCount = scbNextSpawnCount - 1;
    if( scbNextSpawnCount == 0 ) // spawn rate increases slowly depending on score
    {
        scbNextSpawnCount = scbMap( scbPlayerScore, 0, 50, 60, 30 );
        scbNextSpawnCount = gbMax( scbNextSpawnCount, 10 );
        scbEnemiesAdd();
    }

    for( i = 0; i < SCB_NUM_ENEMIES; i++ )
      scbEnemyUpdate( i );
}

// Direct port of real `Enemy::draw()`.
void scbEnemyDraw( int i )
{
    int subtype = scbEnemies[ i ].subtype;
    int width = scbEnemyGetWidth( subtype );
    int height = scbEnemyGetHeight( subtype );
    if( scbIsOffScreen( scbEnemies[ i ].x, scbEnemies[ i ].y, width, height ) ) return;

    int flip = 0;
    if( scbEnemies[ i ].dir <= 0 ) flip = 1;

    if( subtype == SCB_E_SMALL )
    {
        int frame = ( scbEnemies[ i ].dir * scbEnemies[ i ].x / 16 + 255 ) % 5;
        gbDrawBitmapRotated( scbToScreenX( scbEnemies[ i ].x ) - 1, scbToScreenY( scbEnemies[ i ].y ), scbSmallEnemyBitmap[ frame ], 0, flip );
    }
    else
    {
        int frame = ( scbEnemies[ i ].dir * scbEnemies[ i ].x / 16 + 255 ) % 6;
        gbDrawBitmapRotated( scbToScreenX( scbEnemies[ i ].x ) - 4, scbToScreenY( scbEnemies[ i ].y ), scbBigEnemyBitmap[ frame ], 0, flip );
    }
}

void scbEnemiesDraw()
{
    int i;
    for( i = 0; i < SCB_NUM_ENEMIES; i++ )
      if( scbEnemies[ i ].active )
        scbEnemyDraw( i );
}

// -----------------------------------------------------------------------------
// Crate
// -----------------------------------------------------------------------------

// Direct port of real `Crate::init()`.
void scbCrateInit()
{
    scbCrateVy = 0;
    int goodSpot;
    do
    {
        scbCrateX = scbArduinoRandom( SCB_SPRITE_SIZE * SCB_SCALE, scbWorldGetWidth() - SCB_SPRITE_SIZE * SCB_SCALE - 48 );
        scbCrateY = scbArduinoRandom( SCB_SPRITE_SIZE * SCB_SCALE, scbWorldGetHeight() - SCB_SPRITE_SIZE * SCB_SCALE - 48 );

        goodSpot = 1;
        if( gbAbsInt( scbPlayerX - scbCrateX ) < 128 || gbAbsInt( scbPlayerY - scbCrateY ) < 128 )
          goodSpot = 0; // too close to the player
        if( scbCrateX > ( scbWorldGetWidth() / 2 - 128 ) && scbCrateX < ( scbWorldGetWidth() / 2 + 128 ) && scbCrateY < 336 )
          goodSpot = 0; // avoid the top central zone where mobs spawn
    }
    while( !goodSpot );
}

// Direct port of real `Crate::update()` - see this file's own header
// comment (quirks 6/7) on the weapon-reroll edge case and the missing
// PISTOL popup case.
void scbCrateUpdate()
{
    scbBoxUpdate( &scbCrateX, &scbCrateY, &scbCrateVx, &scbCrateVy, &scbCrateDir, 48, 48, 8, 128, 5, 5, 100, 100 );

    if( scbCrateY > scbWorldGetHeight() ) // reinit the crate if it fell out of the world
      scbCrateInit();

    if( gbCollideRectRect( scbCrateX, scbCrateY, 48, 48, scbPlayerX, scbPlayerY, 48, 72 ) )
    {
        scbPlayerScore = scbPlayerScore + 1;
        gbPlayOK();
        // add a random value to the weapon type, inferior to the number of
        // unlocked weapons, to avoid picking the same weapon twice in a row
        scbWeaponSubtype = ( scbWeaponSubtype + scbArduinoRandom( 1, scbUnlockedWeapons + 1 ) ) % ( scbUnlockedWeapons + 1 );

        if( scbWeaponSubtype == SCB_W_CLUB ) scbPopup( "CLUB", 20 );
        else if( scbWeaponSubtype == SCB_W_AKIMBO ) scbPopup( "AKIMBO", 20 );
        else if( scbWeaponSubtype == SCB_W_REVOLVER ) scbPopup( "REVOLVER", 20 );
        else if( scbWeaponSubtype == SCB_W_SNIPER ) scbPopup( "SNIPER", 20 );
        else if( scbWeaponSubtype == SCB_W_SHOTGUN ) scbPopup( "SHOTGUN", 20 );
        else if( scbWeaponSubtype == SCB_W_RIFLE ) scbPopup( "RIFLE", 20 );
        else if( scbWeaponSubtype == SCB_W_MACHINEGUN ) scbPopup( "MACHINEGUN", 20 );
        else if( scbWeaponSubtype == SCB_W_DISK ) scbPopup( "DISK", 20 );
        else if( scbWeaponSubtype == SCB_W_LASER ) scbPopup( "LASER", 20 );
        else if( scbWeaponSubtype == SCB_W_GRENADE ) scbPopup( "GRENADE", 20 );
        else if( scbWeaponSubtype == SCB_W_ROCKET ) scbPopup( "ROCKET", 20 );
        else if( scbWeaponSubtype == SCB_W_MINE ) scbPopup( "MINE", 20 );
        // no case for SCB_W_PISTOL - see this file's own header comment (quirk 7)

        if( scbWorldMapNumber == 0 )
        {
            if( scbPlayerScore == SCB_SCORETHRESHOLD_1 )
            {
                if( scbUnlockedWeapons < SCB_W_RIFLE )
                {
                    scbUnlockedWeapons = SCB_W_RIFLE;
                    scbWeaponSubtype = SCB_W_RIFLE;
                    scbPopup( "RIFLE UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 ); // approximates real power_up_sound
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_2 )
            {
                if( scbUnlockedWeapons < SCB_W_SHOTGUN )
                {
                    scbUnlockedWeapons = SCB_W_SHOTGUN;
                    scbWeaponSubtype = SCB_W_SHOTGUN;
                    scbPopup( "SHOTGUN UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_3 )
            {
                if( scbUnlockedMaps < 1 )
                {
                    scbUnlockedMaps = 1;
                    scbPopup( "NEW MAP UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
        }
        else if( scbWorldMapNumber == 1 )
        {
            if( scbPlayerScore == SCB_SCORETHRESHOLD_1 )
            {
                if( scbUnlockedWeapons < SCB_W_ROCKET )
                {
                    scbUnlockedWeapons = SCB_W_ROCKET;
                    scbWeaponSubtype = SCB_W_ROCKET;
                    scbPopup( "ROCKETS UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_2 )
            {
                if( scbUnlockedWeapons < SCB_W_CLUB )
                {
                    scbUnlockedWeapons = SCB_W_CLUB;
                    scbWeaponSubtype = SCB_W_CLUB;
                    scbPopup( "CLUB UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_3 )
            {
                if( scbUnlockedWeapons < SCB_W_REVOLVER )
                {
                    scbUnlockedWeapons = SCB_W_REVOLVER;
                    scbWeaponSubtype = SCB_W_REVOLVER;
                    scbPopup( "REVOLVER UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_4 )
            {
                if( scbUnlockedWeapons < SCB_W_MINE )
                {
                    scbUnlockedWeapons = SCB_W_MINE;
                    scbWeaponSubtype = SCB_W_MINE;
                    scbPopup( "MINES UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_5 )
            {
                if( scbUnlockedMaps < 2 )
                {
                    scbUnlockedMaps = 2;
                    scbPopup( "NEW MAP UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
        }
        else if( scbWorldMapNumber == 2 )
        {
            if( scbPlayerScore == SCB_SCORETHRESHOLD_1 )
            {
                if( scbUnlockedWeapons < SCB_W_SNIPER )
                {
                    scbUnlockedWeapons = SCB_W_SNIPER;
                    scbWeaponSubtype = SCB_W_SNIPER;
                    scbPopup( "SNIPER UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_2 )
            {
                if( scbUnlockedWeapons < SCB_W_MACHINEGUN )
                {
                    scbUnlockedWeapons = SCB_W_MACHINEGUN;
                    scbWeaponSubtype = SCB_W_MACHINEGUN;
                    scbPopup( "MACHINEGUN UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_3 )
            {
                if( scbUnlockedWeapons < SCB_W_GRENADE )
                {
                    scbUnlockedWeapons = SCB_W_GRENADE;
                    scbWeaponSubtype = SCB_W_GRENADE;
                    scbPopup( "GRENADES UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_4 )
            {
                if( scbUnlockedWeapons < SCB_W_AKIMBO )
                {
                    scbUnlockedWeapons = SCB_W_AKIMBO;
                    scbWeaponSubtype = SCB_W_AKIMBO;
                    scbPopup( "AKIMBO UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_5 )
            {
                if( scbUnlockedMaps < 3 )
                {
                    scbUnlockedMaps = 3;
                    scbPopup( "NEW MAP UNLOCKED!", 40 );
                    gbPlayNote( 80, 10 );
                }
            }
        }
        else if( scbWorldMapNumber == 3 ) // real upstream has no sound call at all on any of these 3 unlocks
        {
            if( scbPlayerScore == SCB_SCORETHRESHOLD_3 )
            {
                if( scbUnlockedWeapons < SCB_W_DISK )
                {
                    scbUnlockedWeapons = SCB_W_DISK;
                    scbWeaponSubtype = SCB_W_DISK;
                    scbPopup( "DISK UNLOCKED!", 40 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_4 )
            {
                if( scbUnlockedWeapons < SCB_W_LASER )
                {
                    scbUnlockedWeapons = SCB_W_LASER;
                    scbWeaponSubtype = SCB_W_LASER;
                    scbPopup( "LASER UNLOCKED!", 40 );
                }
            }
            else if( scbPlayerScore == SCB_SCORETHRESHOLD_5 )
            {
                if( scbUnlockedMaps < 4 )
                {
                    scbUnlockedMaps = 4;
                    scbPopup( "LAST MAP UNLOCKED!", 40 );
                }
            }
        }

        scbCrateInit(); // move the crate
    }
}

void scbCrateDraw()
{
    if( scbIsOffScreen( scbCrateX, scbCrateY, 48, 48 ) ) return;
    gbDrawBitmap( scbToScreenX( scbCrateX ), scbToScreenY( scbCrateY ), scbCrateBitmap );
}

// -----------------------------------------------------------------------------
// World map-select tile assignment, gameplay tile rendering, compass
// -----------------------------------------------------------------------------

// Direct port of the real per-map `switch` inside `World::chooseMap()` that
// picks which real wall/platform/edge textures a map uses.
void scbSelectMapTiles( int mapIndex )
{
    scbTiles = scbMaps[ mapIndex ];
    scbWorldMapNumber = mapIndex;

    if( mapIndex == 1 )
    {
        scbWallBitmap = scbBricks;
        scbPlatformBitmap = scbBeam;
        scbHasEdge = 0;
    }
    else if( mapIndex == 3 )
    {
        scbWallBitmap = scbBlackWall;
        scbPlatformBitmap = scbRoundPlatform;
        scbEdgeBitmap = scbRoundPlatformEdge;
        scbHasEdge = 1;
    }
    else // 0, 2, 4
    {
        scbWallBitmap = scbBricks;
        scbPlatformBitmap = scbGrass;
        scbEdgeBitmap = scbGrassEdge;
        scbHasEdge = 1;
    }
}

// Direct port of real `World::draw()` - see this file's own header comment
// (quirk 9) on the preserved non-exclusive edge/corner-tile `if`s.
void scbWorldDraw()
{
    int xMin = scbCameraX / SCB_SPRITE_SIZE;
    int xMax = LCDWIDTH / SCB_SPRITE_SIZE + scbCameraX / SCB_SPRITE_SIZE + 1;
    int yMin = scbCameraY / SCB_SPRITE_SIZE;
    int yMax = LCDHEIGHT / SCB_SPRITE_SIZE + scbCameraY / SCB_SPRITE_SIZE + 1;

    int w = scbTiles[ 0 ];
    int h = scbTiles[ 1 ];

    int x, y;
    for( y = yMin; y < yMax; y++ )
    {
        for( x = xMin; x < xMax; x++ )
        {
            if( x < 0 || x >= w || y < 0 || y >= h ) continue;
            if( !gbGetBitmapPixel( scbTiles, x, y ) ) continue;

            int flip = 0;
            int offset = 0;
            int* bitmap = scbPlatformBitmap;

            if( y >= scbWorldGetHeight() / SCB_SPRITE_SIZE / SCB_SCALE - 1 || y <= 0 || gbGetBitmapPixel( scbTiles, x, y - 1 ) )
            {
                bitmap = scbWallBitmap;
            }
            else if( scbHasEdge )
            {
                if( y > 0 && !gbGetBitmapPixel( scbTiles, x + 1, y ) )
                {
                    bitmap = scbEdgeBitmap; // platform corner
                    flip = 1; // FLIPH
                    offset = 2;
                }
                if( y > 0 && !gbGetBitmapPixel( scbTiles, x - 1, y ) )
                {
                    bitmap = scbEdgeBitmap; // platform corner - see header comment (quirk 9): flip/offset are NOT reset here
                }
            }

            gbDrawBitmapRotated( x * SCB_SPRITE_SIZE - scbCameraX - offset, y * SCB_SPRITE_SIZE - scbCameraY, bitmap, 0, flip ); // NOROT
        }
    }
}

// Direct port of real `drawCompass()`.
void scbDrawCompass()
{
    int x = ( scbCrateX + 48 / 2 - scbPlayerX - 48 / 2 ) / SCB_SCALE;
    int y = ( scbCrateY + 48 / 2 - scbPlayerY - 72 / 2 ) / SCB_SCALE; // real upstream reuses crate.getWidth() here too - inert, since Crate's width==height==48
    int dist = (int)sqrt( (float)( x * x + y * y ) );
    if( dist > 20 )
    {
        int dx = scbToScreenX( scbPlayerX + 48 / 2 ) + ( 16 * x / dist );
        int dy = scbToScreenY( scbPlayerY + 72 / 2 ) + ( 16 * y / dist );
        gbDrawLine( dx, dy, dx + x / 8, dy + y / 8 );
    }
}

// Direct port of real `drawAll()`.
void scbDrawAll()
{
    scbWorldDraw();
    scbCrateDraw();
    scbEnemiesDraw();
    scbPlayerDraw();
    if( !scbPlayerDead && scbWorldMapNumber )
      scbDrawCompass();

    int i;
    for( i = 0; i < SCB_NUM_BULLETS; i++ )
    {
        if( scbBullets[ i ].subtype == SCB_W_EXPLOSION )
          gbSetColor( GB_INVERT );
        scbBulletDraw( i );
        gbSetColor( GB_BLACK );
    }

    gbSetColorBg( GB_BLACK, GB_WHITE );
    gbPrintNumber( scbPlayerScore );

    scbUpdatePopup();
}

// Direct port of real `initGame()`.
void scbInitGame()
{
    scbPlayerInit();
    scbEnemiesInit();
    scbCrateInit();
    scbWeaponSubtype = 0;
    scbShakeTimeLeft = 0;
}

// -----------------------------------------------------------------------------
// States - real upstream's own blocking loops (`mainMenu()`'s
// `gb.titleScreen(logo)`, `World::chooseMap()`, `gamePaused()`, and
// `loop()`'s own post-death `while(1)`) flattened into explicit states,
// matching this project's own established "blocking loop -> explicit
// resumable state" treatment (see gamePong.c's own header comment).
// -----------------------------------------------------------------------------

void scbBeginTitle()
{
    scbState = SCB_STATE_TITLE;
}

// == real `mainMenu()`'s own `gb.titleScreen(logo)` - dismissed by a
// genuine fresh Button A press (this engine's own menu-select button,
// matching real `titleScreen()`'s own real dismiss button).
void scbUpdateTitle()
{
    // real Gamebuino::titleScreen()'s own real left/bottom anchor (x=0,
    // y=LCDHEIGHT-logoHeight) - already confirmed directly against the
    // real source during gameUfoRace.c's own port.
    gbDrawBitmap( 0, LCDHEIGHT - 30, scbLogoBitmap );
    gbCursorX = 28;
    gbCursorY = 2;
    gbPrintString( "PRESS A" );

    if( gbPressed( BTN_A ) )
    {
        scbState = SCB_STATE_CHOOSEMAP;
        scbChooseMapIndex = scbWorldMapNumber;
    }
}

// == real `World::chooseMap()`'s own per-tick body.
void scbUpdateChooseMap()
{
    scbSelectMapTiles( scbChooseMapIndex );

    gbCursorY = LCDHEIGHT - 17;
    scbPrintCentered( scbSelectMapText );
    gbCursorX = 24;
    gbCursorY = LCDHEIGHT - 11;
    gbPrintString( "Score: " );
    gbPrintNumber( scbScore[ scbChooseMapIndex ] );

    // draw the map centered on the screen
    gbDrawBitmap( LCDWIDTH / 2 - scbTiles[ 0 ] / 2, LCDHEIGHT / 2 - scbTiles[ 1 ] / 2 - 5, scbTiles );

    int x, y;
    for( x = SCB_SPRITE_SIZE; x < LCDWIDTH - SCB_SPRITE_SIZE; x = x + SCB_SPRITE_SIZE )
      gbDrawBitmap( x, 0, scbPlatformBitmap );
    for( y = SCB_SPRITE_SIZE; y < LCDHEIGHT; y = y + SCB_SPRITE_SIZE )
    {
        gbDrawBitmap( 0, y, scbWallBitmap );
        gbDrawBitmap( LCDWIDTH - SCB_SPRITE_SIZE, y, scbWallBitmap );
    }
    if( scbHasEdge )
    {
        gbDrawBitmap( 0, 0, scbEdgeBitmap );
        gbDrawBitmapRotated( LCDWIDTH - SCB_SPRITE_SIZE - 2, 0, scbEdgeBitmap, 0, 1 ); // NOROT, FLIPH
    }
    else
    {
        gbDrawBitmap( 0, 0, scbPlatformBitmap );
        gbDrawBitmap( LCDWIDTH - SCB_SPRITE_SIZE, 0, scbPlatformBitmap );
    }

    if( scbChooseMapIndex == scbUnlockedMaps )
    {
        int i;
        for( i = 0; i < SCB_NUM_THRESHOLDS; i++ )
        {
            if( scbScore[ scbChooseMapIndex ] < scbScoreThresholds[ i ] )
            {
                if( ( gbFrameCount % 10 ) > 3 ) // make it blink
                {
                    gbCursorY = LCDHEIGHT - 5;
                    gbCursorX = 12;
                    gbPrintString( "Next unlock: " );
                    gbPrintNumber( scbScoreThresholds[ i ] );
                }
                break;
            }
        }
    }
    if( scbChooseMapIndex > scbUnlockedMaps )
    {
        if( ( gbFrameCount % 10 ) > 3 ) // make it blink
        {
            gbSetColor( GB_BLACK );
            gbFillRect( 28, 15, 28, 7 );
            gbSetColor( GB_WHITE );
            gbCursorX = 29;
            gbCursorY = 16;
            gbPrintString( "LOCKED!" );
        }
    }

    if( gbPressed( BTN_A ) && scbChooseMapIndex <= scbUnlockedMaps )
    {
        scbInitGame();
        scbState = SCB_STATE_PLAY;
        return;
    }
    if( gbPressed( BTN_RIGHT ) )
      scbChooseMapIndex = ( scbChooseMapIndex + 1 ) % SCB_NUM_MAPS;
    if( gbPressed( BTN_LEFT ) )
      scbChooseMapIndex = ( scbChooseMapIndex - 1 + SCB_NUM_MAPS ) % SCB_NUM_MAPS;
    if( gbPressed( BTN_C ) || gbPressed( BTN_B ) )
      scbBeginTitle();
}

// == real `loop()`'s own `if (gb.update())` body.
void scbUpdatePlay()
{
    if( gbPressed( BTN_C ) )
    {
        scbState = SCB_STATE_PAUSED;
        return;
    }

    scbCrateUpdate();
    scbPlayerUpdate();
    scbEnemiesUpdate();
    scbSaveEeprom(); // real upstream - checks whether values changed before writing, so this won't wear out a real EEPROM

    // real upstream literally checks `getWidth()*SCALE<=LCDWIDTH` here - see
    // this file's own header comment (quirk 8) on why this is ported
    // literally despite reading like a typo.
    if( scbWorldGetWidth() * SCB_SCALE <= LCDWIDTH )
      scbCameraX = 0;
    else
    {
        scbCameraX = ( scbPlayerX + 48 / 2 ) / SCB_SCALE - LCDWIDTH / 2;
        scbCameraX = scbConstrain( scbCameraX, 0, scbWorldGetWidth() / SCB_SCALE - LCDWIDTH );
    }
    if( scbWorldGetHeight() * SCB_SCALE <= LCDHEIGHT )
      scbCameraY = 0;
    else
    {
        scbCameraY = ( scbPlayerY + 72 / 2 ) / SCB_SCALE - LCDHEIGHT / 2;
        scbCameraY = scbConstrain( scbCameraY, 0, scbWorldGetHeight() / SCB_SCALE - LCDHEIGHT - SCB_SPRITE_SIZE / 2 );
    }

    if( scbShakeTimeLeft > 0 )
    {
        scbShakeTimeLeft = scbShakeTimeLeft - 1;
        scbCameraX = scbCameraX + scbArduinoRandom( -1, 2 ) * scbShakeAmplitude;
        scbCameraY = scbCameraY + scbArduinoRandom( -1, 2 ) * scbShakeAmplitude;
    }

    scbDrawAll();

    int i;
    for( i = 0; i < SCB_NUM_ENEMIES; i++ ) // player - monster collisions
    {
        if( scbEnemies[ i ].health <= 0 ) continue;
        if( gbCollideRectRect( scbEnemies[ i ].x, scbEnemies[ i ].y, scbEnemyGetWidth( scbEnemies[ i ].subtype ), scbEnemyGetHeight( scbEnemies[ i ].subtype ),
                                scbPlayerX, scbPlayerY, 48, 72 ) )
        {
            int dir = 1;
            if( ( ( scbEnemies[ i ].x + scbEnemyGetWidth( scbEnemies[ i ].subtype ) / 2 ) - ( scbPlayerX + 48 / 2 ) ) > 0 )
              dir = -1;
            scbPlayerDead = 1;
            scbPlayerKill( dir );
            break;
        }
    }

    for( i = 0; i < SCB_NUM_BULLETS; i++ ) // player - bullet collisions
    {
        if( scbBullets[ i ].timeLeft <= 0 ) continue;
        if( scbBulletDamagePlayer( scbBullets[ i ].subtype ) &&
            gbCollideRectRect( scbBullets[ i ].x, scbBullets[ i ].y,
                                scbBulletGetWidth( scbBullets[ i ].subtype, scbBullets[ i ].vx, scbBullets[ i ].timeLeft ),
                                scbBulletGetHeight( scbBullets[ i ].subtype, scbBullets[ i ].vx, scbBullets[ i ].timeLeft ),
                                scbPlayerX, scbPlayerY, 48, 72 ) )
          scbPlayerKill( scbBullets[ i ].dir );
    }

    if( scbPlayerDead )
    {
        scbGameOverCount = 20;
        if( !scbPopupTimeLeft ) // real upstream - only shown if the "NEW HIGHSCORE!" popup isn't already showing
          scbPopup( "GAME OVER!", 20 );
        scbState = SCB_STATE_GAMEOVER;
    }
}

// == real `gamePaused()`.
void scbUpdatePaused()
{
    scbDrawAll();
    gbSetColorBg( GB_BLACK, GB_WHITE );
    gbCursorX = 0;
    gbCursorY = 0;
    gbPrintString( "GAME PAUSED\nB: SAVE & QUIT\nC: RESUME" );

    if( gbPressed( BTN_C ) )
    {
        scbState = SCB_STATE_PLAY;
        return;
    }
    if( gbPressed( BTN_B ) )
    {
        scbWorldAddScore( scbPlayerScore );
        scbSaveEeprom();
        scbState = SCB_STATE_CHOOSEMAP;
        scbChooseMapIndex = scbWorldMapNumber;
    }
}

// == real `loop()`'s own post-death `while(1)` (the ragdoll-fling window).
void scbUpdateGameOver()
{
    scbPlayerUpdate();
    scbEnemiesUpdate();
    scbDrawAll();

    scbGameOverCount = scbGameOverCount - 1;
    if( scbGameOverCount <= 0 || gbPressed( BTN_C ) )
    {
        scbInitGame();
        scbState = SCB_STATE_PLAY;
    }
}

void gameSuperCrateBuino_init()
{
    gbBegin();
    scbLoadEeprom();
    // this game's own local popup state needs a fresh reset on every
    // launch, not just real hardware's own single power-on - see this
    // file's own header comment.
    scbPopupTimeLeft = 0;
    scbShakeTimeLeft = 0;
    scbBeginTitle();
}

void gameSuperCrateBuino_update()
{
    if( !gbUpdate() ) return;

    if( scbState == SCB_STATE_TITLE ) scbUpdateTitle();
    else if( scbState == SCB_STATE_CHOOSEMAP ) scbUpdateChooseMap();
    else if( scbState == SCB_STATE_PLAY ) scbUpdatePlay();
    else if( scbState == SCB_STATE_PAUSED ) scbUpdatePaused();
    else scbUpdateGameOver();

    gbRenderFrame();
}
