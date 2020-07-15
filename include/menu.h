/**
 * @file menu.h
 * @author Luka Keserič
 * @date 15 July 2020
 * @brief menu interface for the game
 *
 */

#ifndef __MENU_H__
#define __MENU_H__

#include "queue.h"
#include "task.h"

#define RESUME 0
#define MAIN_MENU 1

#define OFF 0
#define ON 1

#define SINGLE_PLAYER_MODE 0
#define MULTIPLAYER_MODE 1

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)

/** HELPER MACRO TO RESOLVE SDL KEYCODES */
#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

extern TaskHandle_t PausedStateTask;

extern TaskHandle_t SinglePlayerMenu;
extern TaskHandle_t MultiPlayerMenu;
extern TaskHandle_t CheatsMenu;
extern TaskHandle_t ExitMenu;

extern QueueHandle_t restartGameQueue;
extern QueueHandle_t gameModeQueue;
extern QueueHandle_t livesQueue;
extern QueueHandle_t levelQueue;
extern QueueHandle_t scoreQueue;

extern QueueHandle_t BinaryStateQueue;
extern QueueHandle_t NextKeyQueue;

int menuInit(void);

#endif // __MENU_H__
