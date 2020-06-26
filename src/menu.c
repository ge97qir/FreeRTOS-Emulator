
#include <stdlib.h>
#include <time.h>
#include <SDL2/SDL_scancode.h>

#include "TUM_Utils.h"
#include "TUM_Event.h"
#include "TUM_Font.h"
#include "TUM_Sound.h"
#include "TUM_Ball.h"
#include "TUM_Draw.h"

#include "AsyncIO.h"

// Must be before FreeRTOS includes
#include "main.h"
#include "games.h" 
#include "menu.h"

#include "queue.h"

TaskHandle_t PausedStateTask = NULL;
// Main menu tasks
TaskHandle_t SinglePlayerMenu = NULL;
TaskHandle_t MultiPlayerMenu = NULL;
TaskHandle_t ScoreMenu = NULL;
TaskHandle_t CheatsMenu = NULL;
TaskHandle_t ExitMenu = NULL;

QueueHandle_t debounceQueue = NULL;
QueueHandle_t restartGameQueue = NULL;
QueueHandle_t gameModeQueue = NULL;

static const char *singleplayer_text = "SIGNLE-PLAYER";
static const char *multiplayer_text = "MULTIPLAYER";
static const char *highscore_text = "HIGH-SCORE";
static const char *cheats_text = "CHEATS";
static const char *exit_text = "EXIT";

static int text_height;
static int singleplayer_text_width;
static int multiplayer_text_width;
static int highscore_text_width;
static int cheats_text_width;
static int exit_text_width;

// TODO: Make sure that front and back buffer are filled
void vPausedStateTask(void *pvParameters)
{
    static TickType_t last_change = 0;;

    static const char *paused_text1 = "RESUME";
    static const char *paused_text2 = "RETURN TO MAIN MENU";
    static int paused_text_width1;
    static int paused_text_width2;

    static char selection = RESUME;
    static unsigned int gameMode;

    tumGetTextSize((char *)paused_text1, &paused_text_width1, NULL);
    tumGetTextSize((char *)paused_text2, &paused_text_width2, NULL);

    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(RETURN)]) {
                        xSemaphoreGive(buttons.lock);
                        xQueueReceive(gameModeQueue, &gameMode, 0);
                        switch (gameMode) {
                            case SINGLE_PLAYER_MODE:
                                if(selection == RESUME){
                                    if (PongControlTask) {
                                        vTaskResume(PongControlTask);
                                    }
                                    if (LeftPaddleTask) {
                                        vTaskResume(LeftPaddleTask);
                                    }
                                    if (RightPaddleTask) {
                                        vTaskResume(RightPaddleTask);
                                    }
                                    selection = RESUME; // resets the selection 
                                    if (PausedStateTask) {
                                        vTaskSuspend(PausedStateTask);
                                    }
                                }else if(selection == MAIN_MENU){
                                    last_change = xTaskGetTickCount();
                                    xQueueSendToFront(debounceQueue, &last_change, portMAX_DELAY);
                                    if (SinglePlayerMenu) {
                                        vTaskResume(SinglePlayerMenu);
                                    }
                                    selection = RESUME; // resets the selection 
                                    if (PausedStateTask) {
                                        vTaskSuspend(PausedStateTask);
                                    }
                                }
                                break;
                            case MULTIPLAYER_MODE:
                                if(selection == RESUME){
                                    if (MultiPlayerGame) {
                                        vTaskResume(MultiPlayerGame);
                                    }
                                    selection = RESUME; // resets the selection 
                                    if (PausedStateTask) {
                                        vTaskSuspend(PausedStateTask);
                                    }
                                }else if(selection == MAIN_MENU){
                                    last_change = xTaskGetTickCount();
                                    xQueueSendToFront(debounceQueue, &last_change, portMAX_DELAY);
                                    if (MultiPlayerMenu) {
                                        vTaskResume(MultiPlayerMenu);
                                    }
                                    selection = RESUME; // resets the selection 
                                    if (PausedStateTask) {
                                        vTaskSuspend(PausedStateTask);
                                    }
                                }
                                break;
                            default:
                                break;
                        }

                    }
                    xSemaphoreGive(buttons.lock);
                }
                
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(UP)] || buttons.buttons[KEYCODE(W)]) {
                        xSemaphoreGive(buttons.lock);
                        if (xTaskGetTickCount() - last_change >
                            STATE_DEBOUNCE_DELAY) {
                            last_change = xTaskGetTickCount();
                            if(selection == RESUME){
                                selection = MAIN_MENU;
                            }else if(selection == MAIN_MENU){
                                selection = RESUME;
                            }
                        }
                    }else if(buttons.buttons[KEYCODE(DOWN)] || buttons.buttons[KEYCODE(S)]){
                        xSemaphoreGive(buttons.lock);
                        if (xTaskGetTickCount() - last_change >
                            STATE_DEBOUNCE_DELAY) {
                            last_change = xTaskGetTickCount();
                            if(selection == RESUME){
                                selection = MAIN_MENU;
                            }else if(selection == MAIN_MENU){
                                selection = RESUME;
                            }
                        }
                    }
                    xSemaphoreGive(buttons.lock);
                }
                // Don't suspend task until current execution loop has finished
                // and held resources have been released
                taskENTER_CRITICAL();

                if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
                    tumDrawClear(Black);

                    if(selection == RESUME){
                        tumDrawText((char *)paused_text1,
                                    SCREEN_WIDTH / 2 -
                                    paused_text_width1 / 2,
                                    SCREEN_HEIGHT / 2 - 30, 
                                    Blue);
                        tumDrawText((char *)paused_text2,
                                    SCREEN_WIDTH / 2 -
                                    paused_text_width2 / 2,
                                    SCREEN_HEIGHT / 2 + 30, 
                                    White);
                    }else if(selection == MAIN_MENU){
                        tumDrawText((char *)paused_text1,
                                    SCREEN_WIDTH / 2 -
                                    paused_text_width1 / 2,
                                    SCREEN_HEIGHT / 2 - 30, 
                                    White);
                        tumDrawText((char *)paused_text2,
                                    SCREEN_WIDTH / 2 -
                                    paused_text_width2 / 2,
                                    SCREEN_HEIGHT / 2 + 30, 
                                    Blue);                        
                    }
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}

void vSinglePlayerMenu(void *pvParameters) {
    
    TickType_t last_change = 0;
    static const unsigned int restartSignal = 1;
    static unsigned int gameModeSingle = SINGLE_PLAYER_MODE;

    tumGetTextSize((char *)singleplayer_text, &singleplayer_text_width, &text_height);
    tumGetTextSize((char *)multiplayer_text, &multiplayer_text_width, NULL);
    tumGetTextSize((char *)highscore_text, &highscore_text_width, NULL);
    tumGetTextSize((char *)cheats_text, &cheats_text_width, NULL);
    tumGetTextSize((char *)exit_text, &exit_text_width, NULL);

    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(RETURN)]) {
                        xSemaphoreGive(buttons.lock);
                        if (last_change){
                            xQueueReceive(debounceQueue, &last_change, 0);
                        }
                        if (xTaskGetTickCount() - last_change >
                            STATE_DEBOUNCE_DELAY) {
                            last_change = xTaskGetTickCount();
                            if (PongControlTask) {
                                vTaskResume(PongControlTask);
                            }
                            if (LeftPaddleTask) {
                                vTaskResume(LeftPaddleTask);
                            }
                            if (RightPaddleTask) {
                                vTaskResume(RightPaddleTask);
                            }
                            xQueueSendToFront(restartGameQueue, &restartSignal, portMAX_DELAY);
                            xQueueSendToFront(gameModeQueue, &gameModeSingle, portMAX_DELAY);
                            if (SinglePlayerMenu) {
                                vTaskSuspend(SinglePlayerMenu);
                            }
                        }
                    }else{
                        xSemaphoreGive(buttons.lock);
                    }
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(UP)] || buttons.buttons[KEYCODE(W)]) {
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &prev_state_signal,
                            portMAX_DELAY);
                    }else if(buttons.buttons[KEYCODE(DOWN)] || buttons.buttons[KEYCODE(S)]){
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &next_state_signal,
                            portMAX_DELAY);
                    }
                    xSemaphoreGive(buttons.lock);
                }

                // Don't suspend task until current execution loop has finished
                // and held resources have been released
                taskENTER_CRITICAL();

                if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
                    tumDrawClear(Black);

                    tumDrawText((char *)singleplayer_text,
                                SCREEN_WIDTH / 2 -
                                singleplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 6 * text_height,
                                Blue);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                White);
                    tumDrawText((char *)highscore_text,
                                SCREEN_WIDTH / 2 -
                                highscore_text_width / 2,
                                SCREEN_HEIGHT / 2, 
                                White);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
                                White);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 6 * text_height,
                                White);                                
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}

void vMultiPlayerMenu(void *pvParameters) {

    TickType_t last_change = 0;
    static unsigned int gameModeMulti = MULTIPLAYER_MODE;

    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(RETURN)]) {
                        xSemaphoreGive(buttons.lock);
                        if (last_change){
                            xQueueReceive(debounceQueue, &last_change, 0);
                        }
                        if (xTaskGetTickCount() - last_change >
                            STATE_DEBOUNCE_DELAY) {
                            last_change = xTaskGetTickCount();
                            if (MultiPlayerGame) {
                                vTaskResume(MultiPlayerGame);
                            }
                            //xQueueSendToFront(restartGameQueue, &restartSignal, portMAX_DELAY);
                            xQueueSendToFront(gameModeQueue, &gameModeMulti, portMAX_DELAY);
                            if (MultiPlayerMenu) {
                                vTaskSuspend(MultiPlayerMenu);
                            }
                        }
                    }
                    xSemaphoreGive(buttons.lock);
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(UP)] || buttons.buttons[KEYCODE(W)]) {
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &prev_state_signal,
                            portMAX_DELAY);
                    }else if(buttons.buttons[KEYCODE(DOWN)] || buttons.buttons[KEYCODE(S)]){
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &next_state_signal,
                            portMAX_DELAY);
                    }
                    xSemaphoreGive(buttons.lock);
                }

                // Don't suspend task until current execution loop has finished
                // and held resources have been released
                taskENTER_CRITICAL();

                if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
                    tumDrawClear(Black);

                    tumDrawText((char *)singleplayer_text,
                                SCREEN_WIDTH / 2 -
                                singleplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 6 * text_height,
                                White);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                Blue);
                    tumDrawText((char *)highscore_text,
                                SCREEN_WIDTH / 2 -
                                highscore_text_width / 2,
                                SCREEN_HEIGHT / 2, 
                                White);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
                                White);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 6 * text_height,
                                White);                                
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}

void vScoreMenu(void *pvParameters) {
     
    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(RETURN)]) {
                        xSemaphoreGive(buttons.lock);
                        /*xQueueSendToFront(
                            StateQueue,
                            &next_state_signal,
                            portMAX_DELAY);
                        */
                    }
                    xSemaphoreGive(buttons.lock);
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(UP)] || buttons.buttons[KEYCODE(W)]) {
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &prev_state_signal,
                            portMAX_DELAY);
                    }else if(buttons.buttons[KEYCODE(DOWN)] || buttons.buttons[KEYCODE(S)]){
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &next_state_signal,
                            portMAX_DELAY);
                    }
                    xSemaphoreGive(buttons.lock);
                }

                // Don't suspend task until current execution loop has finished
                // and held resources have been released
                taskENTER_CRITICAL();

                if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
                    tumDrawClear(Black);

                    tumDrawText((char *)singleplayer_text,
                                SCREEN_WIDTH / 2 -
                                singleplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 6 * text_height,
                                White);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                White);
                    tumDrawText((char *)highscore_text,
                                SCREEN_WIDTH / 2 -
                                highscore_text_width / 2,
                                SCREEN_HEIGHT / 2, 
                                Blue);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
                                White);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 6 * text_height,
                                White);                                
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}

void vCheatsMenu(void *pvParameters) {
     
    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(RETURN)]) {
                        xSemaphoreGive(buttons.lock);
                        /*xQueueSendToFront(
                            StateQueue,
                            &next_state_signal,
                            portMAX_DELAY);
                        */
                    }
                    xSemaphoreGive(buttons.lock);
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(UP)] || buttons.buttons[KEYCODE(W)]) {
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &prev_state_signal,
                            portMAX_DELAY);
                    }else if(buttons.buttons[KEYCODE(DOWN)] || buttons.buttons[KEYCODE(S)]){
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &next_state_signal,
                            portMAX_DELAY);
                    }
                    xSemaphoreGive(buttons.lock);
                }

                // Don't suspend task until current execution loop has finished
                // and held resources have been released
                taskENTER_CRITICAL();

                if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
                    tumDrawClear(Black);

                    tumDrawText((char *)singleplayer_text,
                                SCREEN_WIDTH / 2 -
                                singleplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 6 * text_height,
                                White);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                White);
                    tumDrawText((char *)highscore_text,
                                SCREEN_WIDTH / 2 -
                                highscore_text_width / 2,
                                SCREEN_HEIGHT / 2, 
                                White);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
                                Blue);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 6 * text_height,
                                White);                                
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}

void vExitMenu(void *pvParameters) {
   
    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(RETURN)]) {
                        xSemaphoreGive(buttons.lock);
                        exit(EXIT_SUCCESS);
                    }
                    xSemaphoreGive(buttons.lock);
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(UP)] || buttons.buttons[KEYCODE(W)]) {
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &prev_state_signal,
                            portMAX_DELAY);
                    }else if(buttons.buttons[KEYCODE(DOWN)] || buttons.buttons[KEYCODE(S)]){
                        xSemaphoreGive(buttons.lock);
                        xQueueSendToFront(
                            StateQueue,
                            &next_state_signal,
                            portMAX_DELAY);
                    }
                    xSemaphoreGive(buttons.lock);
                }

                // Don't suspend task until current execution loop has finished
                // and held resources have been released
                taskENTER_CRITICAL();

                if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
                    tumDrawClear(Black);

                    tumDrawText((char *)singleplayer_text,
                                SCREEN_WIDTH / 2 -
                                singleplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 6 * text_height,
                                White);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                White);
                    tumDrawText((char *)highscore_text,
                                SCREEN_WIDTH / 2 -
                                highscore_text_width / 2,
                                SCREEN_HEIGHT / 2, 
                                White);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
                                White);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 6 * text_height,
                                Blue);                                
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}

int menuInit(void)
{
    debounceQueue = xQueueCreate(1, sizeof(TickType_t));
    if (!debounceQueue){
        PRINT_ERROR("Could not open debounceQueue");
        goto err_debouncequeue;
    }
    gameModeQueue = xQueueCreate(1, sizeof(unsigned int));
    if (!gameModeQueue) {
        PRINT_ERROR("Could not open gameModeQueue");
        goto err_gamemodequeue;
    }
    restartGameQueue = xQueueCreate(1, sizeof(unsigned int));
    if (!restartGameQueue) {
        PRINT_ERROR("Could not open restartGameQueue");
        goto err_restartgamequeue;
    }
    if (xTaskCreate(vPausedStateTask, "PausedStateTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &PausedStateTask) != pdPASS) {
        PRINT_TASK_ERROR("PausedStateTask");
        goto err_pausedstate;
    }
    if (xTaskCreate(vSinglePlayerMenu, "SinglePlayerMenu",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &SinglePlayerMenu) != pdPASS) {
        PRINT_TASK_ERROR("SinglePlayerMenu");
        goto err_singlemenu;
    }
    if (xTaskCreate(vMultiPlayerMenu, "MultiPlayerMenu",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &MultiPlayerMenu) != pdPASS) {
        PRINT_TASK_ERROR("SinglePlayerMenu");
        goto err_multimenu;
    }
    if (xTaskCreate(vScoreMenu, "ScoreMenu",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &ScoreMenu) != pdPASS) {
        PRINT_TASK_ERROR("ScoreMenu");
        goto err_scoremenu;
    }
    if (xTaskCreate(vCheatsMenu, "CheatsMenu",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &CheatsMenu) != pdPASS) {
        PRINT_TASK_ERROR("CheatsMenu");
        goto err_cheatsmenu;
    }
    if (xTaskCreate(vExitMenu, "ExitMenu",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &ExitMenu) != pdPASS) {
        PRINT_TASK_ERROR("ExitMenu");
        goto err_exitemenu;
    }

    vTaskSuspend(PausedStateTask);
    vTaskSuspend(SinglePlayerMenu);
    vTaskSuspend(MultiPlayerMenu);
    vTaskSuspend(ScoreMenu);
    vTaskSuspend(CheatsMenu);
    vTaskSuspend(ExitMenu);

    return 0;

err_exitemenu:
    vTaskDelete(CheatsMenu);
err_cheatsmenu:
    vTaskDelete(ScoreMenu);
err_scoremenu:
    vTaskDelete(MultiPlayerMenu);
err_multimenu:
    vTaskDelete(SinglePlayerMenu);
err_singlemenu:
    vTaskDelete(PausedStateTask);
err_pausedstate:
    vQueueDelete(restartGameQueue);
err_restartgamequeue:
    vQueueDelete(gameModeQueue);
err_gamemodequeue:
    vQueueDelete(debounceQueue);
err_debouncequeue:
    return -1;
}
