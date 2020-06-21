#ifndef __PONG_H__
#define __PONG_H__

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)

extern TaskHandle_t LeftPaddleTask;
extern TaskHandle_t RightPaddleTask;
extern TaskHandle_t PongControlTask;
extern TaskHandle_t PausedStateTask;
extern TaskHandle_t SinglePlayerMenu;
extern TaskHandle_t MultiPlayerMenu;
extern TaskHandle_t ScoreMenu;
extern TaskHandle_t CheatsMenu;
extern TaskHandle_t ExitMenu;

int pongInit(void);

#endif // __PONG_H__
