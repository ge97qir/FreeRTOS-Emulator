#ifndef __MENU_H__
#define __MENU_H__

#include "queue.h"
#include "task.h"

#define RESUME 0
#define MAIN_MENU 1

#define SINGLE_PLAYER_MODE 0
#define MULTIPLAYER_MODE 1

extern TaskHandle_t PausedStateTask;
extern TaskHandle_t SinglePlayerMenu;
extern TaskHandle_t MultiPlayerMenu;
extern TaskHandle_t ScoreMenu;
extern TaskHandle_t CheatsMenu;
extern TaskHandle_t ExitMenu;

extern QueueHandle_t restartGameQueue;
extern QueueHandle_t gameModeQueue;

int menuInit(void);

#endif // __MENU_H__
