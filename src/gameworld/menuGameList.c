#include "menu.h"
#include "menuGameList.h"
#include "machineDependent.h"
#include "games/games.h"

// Registers every ported game, in the same order (and with the same
// title/author credit strings) as the sibling gamebuino_classic_vircon32
// build's own menuGameList.c - see that file for the full per-game
// credit/license commentary this project's own game files carry forward
// verbatim in their own header comments. `info`/`onResume` follow that
// project's own established conventions exactly (NULL unless a game
// genuinely needs a porter-credit continuation, an unfinished-game reason,
// or a forced-redraw hook - see menu.h's own Game struct doc comment).
//
// Registration order here is NOT alphabetical (menu.c's own displayOrder[]
// handles that independently for display) - it matches the Vircon32
// sibling's own addGames() call order exactly, so the two projects' menu
// entries stay in 1:1 correspondence for anyone comparing them side by
// side.
void addGames()
{
    addGame( "PONG SOLO", "AURELIEN RODOT", NULL, &gamePong_init, &gamePong_update, NULL );
    addGame( "AGARUINO", "OGBABA", NULL, &gameAgaruino_init, &gameAgaruino_update, NULL );
    addGame( "SHUFFLEPUCK CAFE", "AWOT83", NULL, &gameShufflepuck_init, &gameShufflepuck_update, NULL );
    addGame( "TAQUIN", "RACKHAMLENOIR", NULL, &gameTaquin_init, &gameTaquin_update, NULL );
    addGame( "CRAZYCAR", "BAPTISTE POUGET", NULL, &gameCrazyCar_init, &gameCrazyCar_update, NULL );
    addGame( "MAZE", "ANDY O'NEILL", NULL, &gameMaze_init, &gameMaze_update, NULL );
    addGame( "SIMONBUINO", "FORKLIFT5", NULL, &gameSimonbuino_init, &gameSimonbuino_update, NULL );
    addGame( "CONDUIT", "ADEKTO", NULL, &gameConduit_init, &gameConduit_update, NULL );
    addGame( "FLAPPY BIRDO", "FORKLIFT5", NULL, &gameFlappyBirdo_init, &gameFlappyBirdo_update, NULL );
    addGame( "PARACHUTE", "JICEHEL", NULL, &gameParachute_init, &gameParachute_update, NULL );
    addGame( "SNAKE CLASSIC", "RIPPER121 / TNXEC2", NULL, &gameSnakeClassic_init, &gameSnakeClassic_update, NULL );
    addGame( "SNAKE ABC", "FRTHERY", NULL, &gameSnakeAbc_init, &gameSnakeAbc_update, NULL );
    addGame( "FIREMEN", "VICKING69", NULL, &gameFiremen_init, &gameFiremen_update, NULL );
    addGame( "CATCHER", "QUBIST", NULL, &gameCatcher_init, &gameCatcher_update, NULL );
    addGame( "UFO RACE", "RODOT", NULL, &gameUfoRace_init, &gameUfoRace_update, NULL );
    addGame( "MINESWEEPER", "DIRKSTEINDORF", NULL, &gameMinesweeper_init, &gameMinesweeper_update, NULL );
    addGame( "KILL RACE", "YODA ZHANG", NULL, &gameKillrace_init, &gameKillrace_update, NULL );
    addGame( "BLOCKDUDE", "SORUNOME", NULL, &gameBlockdude_init, &gameBlockdude_update, NULL );
    addGame( "LANDER", "YODA ZHANG", NULL, &gameLander_init, &gameLander_update, NULL );
    addGame( "PUNKT", "ANDY O'NEILL", NULL, &gamePunkt_init, &gamePunkt_update, NULL );
    addGame( "INVADERS", "YODA ZHANG", NULL, &gameInvaders_init, &gameInvaders_update, NULL );
    addGame( "ASTEROCKS", "YODA ZHANG", NULL, &gameAsterocks_init, &gameAsterocks_update, NULL );
    addGame( "GRUNIOZERCA", "ARKADIUSZ KAMINSKI", NULL, &gameGruniozerca_init, &gameGruniozerca_update, NULL );
    addGame( "VIDEO POKER", "MIKE DEL POZZO", NULL, &gameVideoPoker_init, &gameVideoPoker_update, NULL );
    addGame( "BLOB ATTACK", "TEAM ARG", NULL, &gameBlobAttack_init, &gameBlobAttack_update, NULL );
    addGame( "SMASH AND CRASH", "SKYRUNNER65", NULL, &gameSmash_init, &gameSmash_update, NULL );
    addGame( "2048", "JOSIAH WINSLOW", NULL, &game2048_init, &game2048_update, NULL );
    addGame( "ARMAGEDDON", "WUUFF", NULL, &gameArmageddon_init, &gameArmageddon_update, NULL );
    addGame( "SKIBUINO", "MIKE DEL POZZO", NULL, &gameSkibuino_init, &gameSkibuino_update, NULL );
    addGame( "MICROHEXAGON", "VALDENTHORANAR", NULL, &gameHexagon_init, &gameHexagon_update, NULL );
    addGame( "JEZZBALL", "RACKHAMLENOIR", NULL, &gameJezzball_init, &gameJezzball_update, NULL );
    addGame( "SHIPWREK", "YAWN-G", NULL, &gameShipwrek_init, &gameShipwrek_update, NULL );
    addGame( "PAQMAN", "YODA ZHANG", NULL, &gamePaqman_init, &gamePaqman_update, NULL );
    addGame( "BLOCKSBUINO", "FRTHERY", NULL, &gameBlocksBuino_init, &gameBlocksBuino_update, NULL );
    addGame( "WORLD'S HARDEST GAME", "SORUNOME", NULL, &gameWhg_init, &gameWhg_update, NULL );
    addGame( "CRAZYTOWN", "CLEMENT QUINTARD", NULL, &gameCrazyTown_init, &gameCrazyTown_update, NULL );
    addGame( "COPTER", "CLEMENT83", NULL, &gameCopter_init, &gameCopter_update, NULL );
    addGame( "ZOMBIESCAPE", "FRAKASSS", NULL, &gameZombiEscape_init, &gameZombiEscape_update, NULL );
    addGame( "GLACIGLACA", "CLEMENT83", NULL, &gameGlaciGlaca_init, &gameGlaciGlaca_update, NULL );
    addGame( "DESCENT INTO HELL", "ETIENNE72230", NULL, &gameDescent_init, &gameDescent_update, NULL );
    addGame( "CASTLE DEFENCE", "KH9282", NULL, &gameCastleDefence_init, &gameCastleDefence_update, NULL );
    addGame( "DIGGER", "SCMAR", NULL, &gameDigger_init, &gameDigger_update, NULL );
    addGame( "SUPER SPACE SHOOTER", "MSEVILGENIUS", NULL, &gameSuperSpaceShooter_init, &gameSuperSpaceShooter_update, NULL );
    addGame( "TETRINO", "J0FF", NULL, &gameTetrino_init, &gameTetrino_update, NULL );
    addGame( "ARTILLERY", "FRAKASSS", NULL, &gameArtillery_init, &gameArtillery_update, NULL );
    addGame( "CRABATOR", "RODOT", NULL, &gameCrabator_init, &gameCrabator_update, NULL );
    addGame( "B-RALLY", "SCMAR", NULL, &gameBRally_init, &gameBRally_update, NULL );
    addGame( "MARUINO", "AJSB113", NULL, &gameMaruino_init, &gameMaruino_update, NULL );
    addGame( "SUPER CRATE BUINO", "AURELIEN RODOT", NULL, &gameSuperCrateBuino_init, &gameSuperCrateBuino_update, NULL );
    addGame( "FIREBUINO", "LADBSOFT", NULL, &gameFirebuino_init, &gameFirebuino_update, NULL );
    addGame( "101 STARSHIPS", "ZOGLU", NULL, &gameStarships101_init, &gameStarships101_update, NULL );
    addGame( "SOLITAIRE", "ANDY O'NEILL", NULL, &gameSolitaire_init, &gameSolitaire_update, NULL );
    addGame( "A TO K", "CARLOS MARI", NULL, &gameA2K_init, &gameA2K_update, NULL );
    addGame( "SOKOBUINO", "MARTINSUSTEK", NULL, &gameSokobuino_init, &gameSokobuino_update, NULL );
    addGame( "DEATHMAZE", "MSEVILGENIUS", NULL, &gameDeathMaze_init, &gameDeathMaze_update, NULL );
    addGame( "ASTEROIDRIPPER", "RIPPER121", NULL, &gameAsteroidRipper_init, &gameAsteroidRipper_update, NULL );
    markUnfinished( addGame( "BANG! BANG!", "RACKHAMLENOIR", "Player 2 never shoots", &gameBangBang_init, &gameBangBang_update, NULL ) );
    addGame( "BREAKOUT RIPPER", "RIPPER121", NULL, &gameBreakoutRipper_init, &gameBreakoutRipper_update, NULL );
    addGame( "LIGHTS OUT AD", "94K", NULL, &gameLightsOutAD_init, &gameLightsOutAD_update, NULL );
    addGame( "THUNDER SHOOT", "AWOT83", NULL, &gameThunderShoot_init, &gameThunderShoot_update, NULL );
    addGame( "BUB", "SMOGHEAP", NULL, &gameBub_init, &gameBub_update, NULL );
    addGame( "T-REX QUEST", "AWOT83", NULL, &gameTrexQuest_init, &gameTrexQuest_update, NULL );
    addGame( "SHOOTBUINO", "FRTHERY", NULL, &gameShootBuino_init, &gameShootBuino_update, NULL );
    addGame( "SENET", "MAXIMILIAN TIMMERKAMP", NULL, &gameSenet_init, &gameSenet_update, NULL );
    addGame( "BOMBER", "CLEMENT83", NULL, &gameBomber_init, &gameBomber_update, NULL );
    addGame( "STICKFIGHTER", "CLEMENT83", NULL, &gameStickFighter_init, &gameStickFighter_update, NULL );
    addGame( "TRON", "CLEMENT83", NULL, &gameTron_init, &gameTron_update, NULL );
    addGame( "SPIN SPIN SPINBUINO!", "ZOGLU", NULL, &gameSpinSpinSpinbuino_init, &gameSpinSpinSpinbuino_update, NULL );
    addGame( "SNAKE 5110", "LADY AWESOME & MAKERSQUIRREL", NULL, &gameSnake5110_init, &gameSnake5110_update, NULL );
    addGame( "PONG LOCAL MULTIPLAYER", "QUBIST", NULL, &gamePongLocalMultiplayer_init, &gamePongLocalMultiplayer_update, NULL );
    markUnfinished( addGame( "SAVE PRINCESSE", "CLEMENT83", "No Dying / Game Over", &gameSavePrincesse_init, &gameSavePrincesse_update, NULL ) );
    markUnfinished( addGame( "MOTOCROSS", "CLEMENT83", "No Dying / Game Over", &gameMotoCross_init, &gameMotoCross_update, NULL ) );
    markUnfinished( addGame( "NO NAME PLATFORM GAME", "FRAKASSS", "Engine Demo", &gameNoNamePlatformGame_init, &gameNoNamePlatformGame_update, NULL ) );
    addGame( "STIJN'S PONG", "STIJN CAERTS", NULL, &gameStijnPong_init, &gameStijnPong_update, NULL );
    addGame( "STIJN'S SNAKE", "STIJN CAERTS", NULL, &gameStijnSnake_init, &gameStijnSnake_update, NULL );
    addGame( "MASTER KEBAB", "OGBABA", NULL, &gameMasterKebab_init, &gameMasterKebab_update, NULL );
    addGame( "AIMBUINO", "BAPTISTE POUGET", NULL, &gameAimbuino_init, &gameAimbuino_update, NULL );
    markUnfinished( addGame( "RALPH", "CLEMENT83", "Non interactive", &gameRalph_init, &gameRalph_update, NULL ) );
    addGame( "FOOTLOL", "BAPTISTE POUGET", NULL, &gameFootlol_init, &gameFootlol_update, NULL );
    markUnfinished( addGame( "MYRPG", "FRAKASSS", "Engine Demo", &gameMyRpg_init, &gameMyRpg_update, NULL ) );
    addGame( "PONG 2017", "YAWN-G", NULL, &gamePong2017_init, &gamePong2017_update, NULL );
    markUnfinished( addGame( "DARKSHMUP", "CLEMENT83", "No Dying / Game Over", &gameDarkShmup_init, &gameDarkShmup_update, NULL ) );
    markUnfinished( addGame( "PINBALL", "CLEMENT83", "Ball can get stuck", &gamePinball_init, &gamePinball_update, NULL ) );
    markUnfinished( addGame( "ROBOT", "FRAKASSS", "No Game Over / only 2 levels", &gameRobot_init, &gameRobot_update, NULL ) );
    addGame( "ELVENTURE", "TRODOSS", NULL, &gameElventure_init, &gameElventure_update, NULL );
    addGame( "STAR HONOR", "WENCESLAO VILLANUEVA JR", "WUUFF", &gameStarHonor_init, &gameStarHonor_update, NULL );
    addGame( "PETITMONSTRE", "CLEMENT83", NULL, &gamePetitMonstre_init, &gamePetitMonstre_update, NULL );
    addGame( "UNDER THE TOWER", "WUUFF", NULL, &gameUnderTheTower_init, &gameUnderTheTower_update, NULL );
    addGame( "BIGBLACKBOX", "STUDIOCRAFTAPPS", NULL, &gameBigBlackBox_init, &gameBigBlackBox_update, NULL );
    markUnfinished( addGame( "CRUISER", "MICHAEL SPECHT", "No Dying / Game Over", &gameCruiser_init, &gameCruiser_update, NULL ) );
    addGame( "COPTERSTRIKE", "FRAKASSS", NULL, &gameCopterStrike_init, &gameCopterStrike_update, NULL );
    addGame( "XONIX", "TNXEC2", NULL, &gameXonix_init, &gameXonix_update, NULL );
    addGame( "ANOTHER 2048", "GRAFMAKULADER2TE", NULL, &gameAnother2048_init, &gameAnother2048_update, NULL );
    addGame( "DARK TOWER", "MARCUS HUTCHINGS", NULL, &gameDarkTower_init, &gameDarkTower_update, NULL );
    addGame( "GEMGEM", "TNXEC2", NULL, &gameGemgem_init, &gameGemgem_update, NULL );
    markUnfinished( addGame( "COMMUNITY RPG", "SORUNOME", "Unfinished game", &gameCommunityRpg_init, &gameCommunityRpg_update, NULL ) );
    addGame( "FIFTEEN", "TNXEC2", NULL, &gameFifteen_init, &gameFifteen_update, NULL );
    addGame( "MOLE CONTROL", "GRAFMAKULADER2TE", NULL, &gameMoleControl_init, &gameMoleControl_update, NULL );
    addGame( "AERIAL-ASSAULT", "SKYLARHYLAR", NULL, &gameAerialAssault_init, &gameAerialAssault_update, NULL );
    // Not a ported upstream game - a real, in-cartridge Sound-primitive
    // diagnostic tool (see gameSoundTest.c's own header comment). Marked
    // unfinished purely to visually flag it as a different kind of entry
    // from every other, real game in this list, not because it's an
    // incomplete game.
    markUnfinished( addGame( "SOUND TEST", "WILLEMS DAVY", "Diagnostic tool, not a game", &gameSoundTest_init, &gameSoundTest_update, NULL ) );
}
