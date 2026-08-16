#ifndef GAMES_H
#define GAMES_H

// -----------------------------------------------------------------------------
// One shared header for every ported game's own tiny public surface (just
// `_init`/`_update`, and `_onResume` for the handful of games that need
// one - see menu.h's own Game struct) - deliberately ONE file for every
// game, not a header per game: each games/gameXxx.c is otherwise a fully
// self-contained translation unit (see CLAUDE.md's own "Translation-unit
// boundary" section, ported from the sibling Tinyjoypad_SDL project's own
// identical design) with no cross-game symbol sharing, so there is nothing
// else here for a per-game header to usefully declare. The only consumer
// is menuGameList.c's own addGames(), which needs every one of these
// visible before it can take their address.
//
// Populated incrementally as each game in the sibling gamebuino_classic_
// vircon32 build's own 99-game list gets ported here (see that project's
// own src/games/*.c for the real per-game upstream porting notes this
// project's own game files carry forward verbatim in their own header
// comments, mechanically translated - see CLAUDE.md's "Dialect
// conversion" section for the exact recipe).
// -----------------------------------------------------------------------------

void gamePong_init();
void gamePong_update();

void gameAgaruino_init();
void gameAgaruino_update();

void gameDeathMaze_init();
void gameDeathMaze_update();

void gameUfoRace_init();
void gameUfoRace_update();

void gameMinesweeper_init();
void gameMinesweeper_update();

void gameKillrace_init();
void gameKillrace_update();

void gameBlockdude_init();
void gameBlockdude_update();

void gameLander_init();
void gameLander_update();

void gamePunkt_init();
void gamePunkt_update();

void gameInvaders_init();
void gameInvaders_update();

void gameAsterocks_init();
void gameAsterocks_update();

void gameGruniozerca_init();
void gameGruniozerca_update();

void gameVideoPoker_init();
void gameVideoPoker_update();

void gameBlobAttack_init();
void gameBlobAttack_update();

void gameSmash_init();
void gameSmash_update();

void gameUnderTheTower_init();
void gameUnderTheTower_update();

void gameBigBlackBox_init();
void gameBigBlackBox_update();

void gameCruiser_init();
void gameCruiser_update();

void gameCopterStrike_init();
void gameCopterStrike_update();

void gameXonix_init();
void gameXonix_update();

void gameAnother2048_init();
void gameAnother2048_update();

void gameDarkTower_init();
void gameDarkTower_update();

void gameGemgem_init();
void gameGemgem_update();

void gameFifteen_init();
void gameFifteen_update();

void gameMoleControl_init();
void gameMoleControl_update();

void gameAerialAssault_init();
void gameAerialAssault_update();

void gameCommunityRpg_init();
void gameCommunityRpg_update();

void gameShufflepuck_init();
void gameShufflepuck_update();

void gameTaquin_init();
void gameTaquin_update();

void gameCrazyCar_init();
void gameCrazyCar_update();

void gameMaze_init();
void gameMaze_update();

void gameSimonbuino_init();
void gameSimonbuino_update();

void gameConduit_init();
void gameConduit_update();

void gameFlappyBirdo_init();
void gameFlappyBirdo_update();

void gameParachute_init();
void gameParachute_update();

void gameSnakeClassic_init();
void gameSnakeClassic_update();

void gameSnakeAbc_init();
void gameSnakeAbc_update();

void gameFiremen_init();
void gameFiremen_update();

void gameCatcher_init();
void gameCatcher_update();

void gameGlaciGlaca_init();
void gameGlaciGlaca_update();

void gameDescent_init();
void gameDescent_update();

void gameCastleDefence_init();
void gameCastleDefence_update();

void gameDigger_init();
void gameDigger_update();

void gameSuperSpaceShooter_init();
void gameSuperSpaceShooter_update();

void gameTetrino_init();
void gameTetrino_update();

void gameArtillery_init();
void gameArtillery_update();

void gameCrabator_init();
void gameCrabator_update();

void gameBRally_init();
void gameBRally_update();

void gameMaruino_init();
void gameMaruino_update();

void gameSuperCrateBuino_init();
void gameSuperCrateBuino_update();

void gameFirebuino_init();
void gameFirebuino_update();

void game2048_init();
void game2048_update();

void gameArmageddon_init();
void gameArmageddon_update();

void gameSkibuino_init();
void gameSkibuino_update();

void gameHexagon_init();
void gameHexagon_update();

void gameJezzball_init();
void gameJezzball_update();

void gameShipwrek_init();
void gameShipwrek_update();

void gamePaqman_init();
void gamePaqman_update();

void gameBlocksBuino_init();
void gameBlocksBuino_update();

void gameWhg_init();
void gameWhg_update();

void gameCrazyTown_init();
void gameCrazyTown_update();

void gameCopter_init();
void gameCopter_update();

void gameZombiEscape_init();
void gameZombiEscape_update();

void gameSenet_init();
void gameSenet_update();

void gameBomber_init();
void gameBomber_update();

void gameStickFighter_init();
void gameStickFighter_update();

void gameTron_init();
void gameTron_update();

void gameSpinSpinSpinbuino_init();
void gameSpinSpinSpinbuino_update();

void gameSnake5110_init();
void gameSnake5110_update();

void gamePongLocalMultiplayer_init();
void gamePongLocalMultiplayer_update();

void gameSavePrincesse_init();
void gameSavePrincesse_update();

void gameMotoCross_init();
void gameMotoCross_update();

void gameNoNamePlatformGame_init();
void gameNoNamePlatformGame_update();

void gameStijnPong_init();
void gameStijnPong_update();

void gameStijnSnake_init();
void gameStijnSnake_update();

void gameMasterKebab_init();
void gameMasterKebab_update();

void gameAimbuino_init();
void gameAimbuino_update();

void gameRalph_init();
void gameRalph_update();

void gameFootlol_init();
void gameFootlol_update();

void gameMyRpg_init();
void gameMyRpg_update();

void gamePong2017_init();
void gamePong2017_update();

void gameDarkShmup_init();
void gameDarkShmup_update();

void gamePinball_init();
void gamePinball_update();

void gameRobot_init();
void gameRobot_update();

void gameElventure_init();
void gameElventure_update();

void gameStarHonor_init();
void gameStarHonor_update();

void gamePetitMonstre_init();
void gamePetitMonstre_update();

void gameStarships101_init();
void gameStarships101_update();

void gameSolitaire_init();
void gameSolitaire_update();

void gameA2K_init();
void gameA2K_update();

void gameSokobuino_init();
void gameSokobuino_update();

void gameAsteroidRipper_init();
void gameAsteroidRipper_update();

void gameBangBang_init();
void gameBangBang_update();

void gameBreakoutRipper_init();
void gameBreakoutRipper_update();

void gameLightsOutAD_init();
void gameLightsOutAD_update();

void gameThunderShoot_init();
void gameThunderShoot_update();

void gameBub_init();
void gameBub_update();

void gameTrexQuest_init();
void gameTrexQuest_update();

void gameShootBuino_init();
void gameShootBuino_update();

void gameSoundTest_init();
void gameSoundTest_update();

#endif
