#include <stdlib.h>
#include <math.h>
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
// FreeRTOS includes
#include "queue.h"

// CHEATS MENU
// MAIN_MENE defined in the header as 1
#define LIVES 6
#define LEVEL 5
#define SCORE 4
#define CHEATS_OFF 3
#define HIGHSCORE 2

TaskHandle_t CheatsTask = NULL;
TaskHandle_t PausedStateTask = NULL;
// Main menu tasks
TaskHandle_t SinglePlayerMenu = NULL;
TaskHandle_t MultiPlayerMenu = NULL;
TaskHandle_t CheatsMenu = NULL;
TaskHandle_t ExitMenu = NULL;

QueueHandle_t debounceQueue = NULL;
QueueHandle_t restartGameQueue = NULL;
QueueHandle_t gameModeQueue = NULL;
QueueHandle_t livesQueue = NULL;
QueueHandle_t levelQueue = NULL;
QueueHandle_t scoreQueue = NULL;


static const char *singleplayer_text = "SIGNLE-PLAYER";
static const char *multiplayer_text = "MULTIPLAYER";
static const char *cheats_text = "CHEATS";
static const char *exit_text = "EXIT";

static int text_height;
static int singleplayer_text_width;
static int multiplayer_text_width;
static int cheats_text_width;
static int exit_text_width;


void vPausedStateTask(void *pvParameters)
{
    static TickType_t last_change = 1;

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
    
    TickType_t last_change = 1;
    static const unsigned int restartSignal = 1;
    static unsigned int gameModeSingle = SINGLE_PLAYER_MODE;

    tumGetTextSize((char *)singleplayer_text, &singleplayer_text_width, &text_height);
    tumGetTextSize((char *)multiplayer_text, &multiplayer_text_width, NULL);
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
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                Blue);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 1 * text_height,
                                White);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 1 * text_height,
                                White);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
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

    TickType_t last_change = 1;
    static const unsigned int restartSignal = 1;
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
                            xQueueSendToFront(restartGameQueue, &restartSignal, portMAX_DELAY);
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
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                White);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 1 * text_height,
                                Blue);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 1 * text_height,
                                White);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
                                White);                                
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}


void vCheatsTask(void *pvParameters)
{
    unsigned short prevButtonState1 = OFF;
    unsigned short currentButtonState1 = OFF;
    unsigned short prevButtonState2 = OFF;
    unsigned short currentButtonState2 = OFF;

    FILE *fp = NULL;
    char* highscore_file = "../resources/highscore.txt";
    static unsigned int highscore = 0;

    static TickType_t last_change = 1;
    static unsigned short lives_cheat = OFF;
    static unsigned short level_cheat = 1;
    static signed short score_cheat = 0;

    static char highscore_text[10] = { 0 };
    static char level_text[10] = "< 1 >";
    static char score_text[10] = "< 0 >";
    static const char *on_text = "< ON >";
    static const char *off_text = "< OFF >";
    // actual cheats
    static const char *cheats_text1 = "INFINITE LIVES";
    static const char *cheats_text2 = "STARTING LEVEL";
    static const char *cheats_text3 = "STARTING SCORE";
    static const char *cheats_text4 = "TURN OFF CHEATS";
    // my addition for better user experience
    static const char *cheats_text5 = "RESET HIGHSCORE";
    static const char *cheats_text6 = "RETURN TO MAIN MENU";

    static int cheats_text_width1;
    static int cheats_text_width2;
    static int cheats_text_width3;
    static int cheats_text_width4;
    static int cheats_text_width5;
    static int cheats_text_width6;

    static char selection = LIVES;

    tumGetTextSize((char *)cheats_text1, &cheats_text_width1, NULL);
    tumGetTextSize((char *)cheats_text2, &cheats_text_width2, NULL);
    tumGetTextSize((char *)cheats_text3, &cheats_text_width3, NULL);
    tumGetTextSize((char *)cheats_text4, &cheats_text_width4, NULL);
    tumGetTextSize((char *)cheats_text5, &cheats_text_width5, NULL);
    tumGetTextSize((char *)cheats_text6, &cheats_text_width6, NULL);

    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data
                
                fp = fopen(highscore_file, "r");
                if (fp != NULL){
                    highscore = getw(fp);
                }else{
                    // need to create a highscore queue
                    highscore = 0;
                }
                fclose(fp);
                
                sprintf(highscore_text, "[ %u ]", highscore);

                
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(A)] || buttons.buttons[KEYCODE(LEFT)]) {
                        xSemaphoreGive(buttons.lock);
                        if (last_change){
                            xQueueReceive(debounceQueue, &last_change, 0);
                        }
                        switch (selection) {
                            case LIVES:
                                if (xTaskGetTickCount() - last_change >
                                    STATE_DEBOUNCE_DELAY) {
                                    last_change = xTaskGetTickCount();
                                    
                                    lives_cheat = !lives_cheat;
                                }
                                break;
                            case LEVEL:
                                if (xTaskGetTickCount() - last_change >
                                    STATE_DEBOUNCE_DELAY) {
                                    last_change = xTaskGetTickCount();

                                    if (level_cheat > 1){
                                        level_cheat--;
                                        sprintf(level_text, "< %hu >", level_cheat);
                                    }
                                }
                                break;
                            case SCORE:
                                if (currentButtonState1 == ON){
                                    if (score_cheat > 0){
                                        // adding by 13 to increase score faster when the user is holding the button
                                        // 13 is chosen instead of 10 to move all the numbers so it looks better
                                        score_cheat -= 13;
                                        sprintf(score_text, "< %d >", score_cheat);
                                    }else{
                                        score_cheat = 0;
                                        sprintf(score_text, "< %d >", score_cheat);
                                    }
                                }   
                                else if (xTaskGetTickCount() - last_change >
                                        STATE_DEBOUNCE_DELAY) {

                                    last_change = xTaskGetTickCount();
                                    
                                    if (prevButtonState1 == ON){
                                        currentButtonState1 = ON;
                                    }
                                    prevButtonState1 = ON;

                                    if (score_cheat > 0){
                                        score_cheat--;
                                        sprintf(score_text, "< %d >", score_cheat);
                                    }
                                }
                                break;  
                            default:
                                break;
                        }

                    }else{
                        currentButtonState1 = OFF;
                        prevButtonState1 = OFF;
                    }
                    xSemaphoreGive(buttons.lock);
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(D)] || buttons.buttons[KEYCODE(RIGHT)]) {
                        xSemaphoreGive(buttons.lock);
                        if (last_change){
                            xQueueReceive(debounceQueue, &last_change, 0);
                        }

                        switch (selection) {
                            case LIVES:
                                if (xTaskGetTickCount() - last_change >
                                    STATE_DEBOUNCE_DELAY) {
                                    last_change = xTaskGetTickCount();
                                    
                                    lives_cheat = !lives_cheat;
                                    xQueueOverwrite(livesQueue, &lives_cheat);

                                }
                                break;
                            case LEVEL:
                                if (xTaskGetTickCount() - last_change >
                                    STATE_DEBOUNCE_DELAY) {
                                    last_change = xTaskGetTickCount();
                                    
                                    if (level_cheat < 5){
                                        level_cheat++;
                                        sprintf(level_text, "< %hu >", level_cheat);
                                    }

                                }
                                break;
                            case SCORE:
                                if (currentButtonState2 == ON){
                                    if (score_cheat < 9999){
                                        // adding by 13 to increase score faster when the user is holding the button
                                        // 13 is chosen instead of 10 to move all the numbers so it looks better
                                        score_cheat += 13;
                                        sprintf(score_text, "< %d >", score_cheat);
                                    }else{
                                        score_cheat = 9999;
                                        sprintf(score_text, "< %d >", score_cheat);
                                    }
                                }   
                                else if (xTaskGetTickCount() - last_change >
                                        STATE_DEBOUNCE_DELAY) {

                                    last_change = xTaskGetTickCount();
                                    
                                    if (prevButtonState2 == ON){
                                        currentButtonState2 = ON;
                                    }
                                    prevButtonState2 = ON;

                                    if (score_cheat < 9999){
                                        score_cheat++;
                                        sprintf(score_text, "< %d >", score_cheat);
                                    }
                                }
                                break;  
                            default:
                                break;
                        }

                    }else{
                        currentButtonState2 = OFF;
                        prevButtonState2 = OFF;
                    }
                    xSemaphoreGive(buttons.lock);
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(RETURN)]) {
                        xSemaphoreGive(buttons.lock);
                        if (last_change){
                            xQueueReceive(debounceQueue, &last_change, 0);
                        }
                        switch (selection) {
                            case CHEATS_OFF:
                                if (xTaskGetTickCount() - last_change >
                                    STATE_DEBOUNCE_DELAY) {
                                    last_change = xTaskGetTickCount();
                                    
                                    lives_cheat = OFF;
                                    level_cheat = 1;
                                    score_cheat = 0;
                                    sprintf(level_text, "< %hu >", level_cheat);
                                    sprintf(score_text, "< %hu >", score_cheat);
                                }
                                break;  
                            case HIGHSCORE:
                                if (xTaskGetTickCount() - last_change >
                                    STATE_DEBOUNCE_DELAY) {
                                    last_change = xTaskGetTickCount();
                                    
                                    fp = fopen(highscore_file, "w");
                                    if (fp != NULL){
                                        putw(0, fp);
                                    }
                                    fclose(fp);

                                }
                                break;                                                                                               
                            case MAIN_MENU:
                                if (xTaskGetTickCount() - last_change >
                                    STATE_DEBOUNCE_DELAY) {
                                    last_change = xTaskGetTickCount();
                                    xQueueSendToFront(debounceQueue, &last_change, portMAX_DELAY);
                                    // when exiting cheats task send all the information about cheats with queues
                                    xQueueOverwrite(livesQueue, &lives_cheat);
                                    xQueueOverwrite(levelQueue, &level_cheat);
                                    xQueueOverwrite(scoreQueue, &score_cheat);

                                    if (CheatsMenu) {
                                        vTaskResume(CheatsMenu);
                                    }
                                    selection = LIVES;
                                    if (CheatsTask) {
                                        vTaskSuspend(CheatsTask);
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
                            
                            if(selection == LIVES){
                                selection = MAIN_MENU;
                            }else{
                                selection++;
                            }
                        }
                    }else if(buttons.buttons[KEYCODE(DOWN)] || buttons.buttons[KEYCODE(S)]){
                        xSemaphoreGive(buttons.lock);
                        if (xTaskGetTickCount() - last_change >
                            STATE_DEBOUNCE_DELAY) {
                            last_change = xTaskGetTickCount();
                            
                            if(selection == MAIN_MENU){
                                selection = LIVES;
                            }else{
                                selection--;
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
                    // LIVES CHEAT
                    tumDrawText((char *)cheats_text1,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width6 / 2,
                                SCREEN_HEIGHT / 2 - 150, 
                                White);
                    if (lives_cheat){
                        tumDrawText((char *)on_text,
                                SCREEN_WIDTH / 2 +
                                cheats_text_width6 / 2 + 38,
                                SCREEN_HEIGHT / 2 - 150, 
                                White);
                    }else{
                        tumDrawText((char *)off_text,
                                SCREEN_WIDTH / 2 +
                                cheats_text_width6 / 2 + 38,
                                SCREEN_HEIGHT / 2 - 150, 
                                White);
                    }
                    // LEVEL CHEAT
                    tumDrawText((char *)cheats_text2,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width6 / 2,
                                SCREEN_HEIGHT / 2 - 100, 
                                White);
                    tumDrawText(level_text,
                                SCREEN_WIDTH / 2 +
                                cheats_text_width6 / 2 + 38,
                                SCREEN_HEIGHT / 2 - 100, 
                                White);
                    // SCORE CHEAT
                    tumDrawText((char *)cheats_text3,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width6 / 2,
                                SCREEN_HEIGHT / 2 - 50, 
                                White);
                    tumDrawText(score_text,
                                SCREEN_WIDTH / 2 +
                                cheats_text_width6 / 2 + 38,
                                SCREEN_HEIGHT / 2 - 50, 
                                White);
                    // TURN OFF CHEATS
                    tumDrawText((char *)cheats_text4,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width6 / 2,
                                SCREEN_HEIGHT / 2, 
                                White);
                    // HIGHSCORE RESET
                    tumDrawText((char *)cheats_text5,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width6 / 2,
                                SCREEN_HEIGHT / 2 + 50, 
                                White);
                    tumDrawText(highscore_text,
                                SCREEN_WIDTH / 2 +
                                cheats_text_width6 / 2 + 38,
                                SCREEN_HEIGHT / 2 + 50, 
                                White);
                    // MAIN MENU
                    tumDrawText((char *)cheats_text6,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width6 / 2,
                                SCREEN_HEIGHT / 2 + 100, 
                                White);

                    // draw the highlighted selected text
                    switch (selection) {
                        case LIVES:
                            tumDrawText((char *)cheats_text1,
                                        SCREEN_WIDTH / 2 -
                                        cheats_text_width6 / 2,
                                        SCREEN_HEIGHT / 2 - 150, 
                                        Blue);
                            if (lives_cheat){
                                tumDrawText((char *)on_text,
                                        SCREEN_WIDTH / 2 +
                                        cheats_text_width6 / 2 + 38,
                                        SCREEN_HEIGHT / 2 - 150, 
                                        Blue);
                            }else{
                                tumDrawText((char *)off_text,
                                        SCREEN_WIDTH / 2 +
                                        cheats_text_width6 / 2 + 38,
                                        SCREEN_HEIGHT / 2 - 150, 
                                        Blue);
                            }
                            break;
                        case LEVEL:
                            tumDrawText((char *)cheats_text2,
                                        SCREEN_WIDTH / 2 -
                                        cheats_text_width6 / 2,
                                        SCREEN_HEIGHT / 2 - 100, 
                                        Blue);
                            tumDrawText(level_text,
                                        SCREEN_WIDTH / 2 +
                                        cheats_text_width6 / 2 + 38,
                                        SCREEN_HEIGHT / 2 - 100, 
                                        Blue);
                            break;
                        case SCORE:
                            tumDrawText((char *)cheats_text3,
                                        SCREEN_WIDTH / 2 -
                                        cheats_text_width6 / 2,
                                        SCREEN_HEIGHT / 2 - 50, 
                                        Blue);
                            tumDrawText(score_text,
                                        SCREEN_WIDTH / 2 +
                                        cheats_text_width6 / 2 + 38,
                                        SCREEN_HEIGHT / 2 - 50, 
                                        Blue);
                            break;
                        case CHEATS_OFF:
                            tumDrawText((char *)cheats_text4,
                                        SCREEN_WIDTH / 2 -
                                        cheats_text_width6 / 2,
                                        SCREEN_HEIGHT / 2, 
                                        Blue);
                            break;  
                        case HIGHSCORE:
                            tumDrawText((char *)cheats_text5,
                                        SCREEN_WIDTH / 2 -
                                        cheats_text_width6 / 2,
                                        SCREEN_HEIGHT / 2 + 50, 
                                        Blue);
                            break;                                                                                               
                        case MAIN_MENU:
                            tumDrawText((char *)cheats_text6,
                                        SCREEN_WIDTH / 2 -
                                        cheats_text_width6 / 2,
                                        SCREEN_HEIGHT / 2 + 100, 
                                        Blue);
                            break;
                        default:
                            break;
                    }
                }

                xSemaphoreGive(ScreenLock);

                taskEXIT_CRITICAL();

                vTaskDelay(10);
            }
        }
    }
}

void vCheatsMenu(void *pvParameters) {
    
    TickType_t last_change = 1;

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
                            xQueueSendToFront(debounceQueue, &last_change, portMAX_DELAY);
                            if (CheatsTask) {
                                vTaskResume(CheatsTask);
                            }
                            if (CheatsMenu) {
                                vTaskSuspend(CheatsMenu);
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
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                White);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 1 * text_height,
                                White);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 1 * text_height,
                                Blue);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
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
                                SCREEN_HEIGHT / 2 - 3 * text_height,
                                White);
                    tumDrawText((char *)multiplayer_text,
                                SCREEN_WIDTH / 2 -
                                multiplayer_text_width / 2,
                                SCREEN_HEIGHT / 2 - 1 * text_height,
                                White);
                    tumDrawText((char *)cheats_text,
                                SCREEN_WIDTH / 2 -
                                cheats_text_width / 2,
                                SCREEN_HEIGHT / 2 + 1 * text_height,
                                White);         
                    tumDrawText((char *)exit_text,
                                SCREEN_WIDTH / 2 -
                                exit_text_width / 2,
                                SCREEN_HEIGHT / 2 + 3 * text_height,
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
    scoreQueue = xQueueCreate(1, sizeof(signed short));
    if (!scoreQueue){
        PRINT_ERROR("Could not open scoreQueue");
        goto err_scorequeue;
    }    
    levelQueue = xQueueCreate(1, sizeof(unsigned short));
    if (!levelQueue){
        PRINT_ERROR("Could not open levelQueue");
        goto err_levelqueue;
    }
    livesQueue = xQueueCreate(1, sizeof(unsigned short));
    if (!livesQueue){
        PRINT_ERROR("Could not open livesQueue");
        goto err_livesqueue;
    }
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
    if (xTaskCreate(vCheatsTask, "CheatsTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &CheatsTask) != pdPASS) {
        PRINT_TASK_ERROR("CheatsTask");
        goto err_cheatstask;
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
    vTaskSuspend(CheatsTask);
    vTaskSuspend(CheatsMenu);
    vTaskSuspend(ExitMenu);

    return 0;

err_exitemenu:
    vTaskDelete(CheatsMenu);
err_cheatsmenu:
    vTaskDelete(CheatsTask);
err_cheatstask:
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
    vQueueDelete(livesQueue);
err_livesqueue:
    vQueueDelete(levelQueue);
err_levelqueue:
    vQueueDelete(scoreQueue);
err_scorequeue:
    return -1;
}
