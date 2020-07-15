#ifndef __GAMES_H__
#define __GAMES_H__

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)

/** HELPER MACRO TO RESOLVE SDL KEYCODES */
#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

extern TaskHandle_t LeftPaddleTask;
extern TaskHandle_t RightPaddleTask;
extern TaskHandle_t PongControlTask;
extern TaskHandle_t MultiPlayerGame;
extern TaskHandle_t UDPControlTask;
extern TaskHandle_t MothershipTask;
extern TaskHandle_t PlayerTask;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

extern buttons_buffer_t buttons;

void xGetButtonInput(void);

int gamesInit(void);

#endif // __GAMES_H__
