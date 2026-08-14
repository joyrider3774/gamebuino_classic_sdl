#ifndef MENU_GAME_LIST_H
#define MENU_GAME_LIST_H

// Registers every ported game via addGame() (menu.h) - called once by
// gamesMain_init(). Ported from the sibling gamebuino_classic_vircon32
// build's own menuGameList.c/.h (same one-addGame()-call-per-game list,
// same markUnfinished() follow-up calls for the handful of games flagged
// that way there) - see games/games.h for the per-game init()/update()
// declarations this file's own addGame() calls reference.
void addGames();

#endif
