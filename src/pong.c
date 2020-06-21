
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

#include "pong.h" // Must be before FreeRTOS includes
#include "main.h"
#include "queue.h"

/** GAME DIMENSIONS */
#define WALL_OFFSET 20
#define WALL_THICKNESS 10
#define GAME_FIELD_OUTER WALL_OFFSET
#define GAME_FIELD_INNER (GAME_FIELD_OUTER + WALL_THICKNESS)
#define GAME_FIELD_HEIGHT_INNER (SCREEN_HEIGHT - 2 * GAME_FIELD_INNER)
#define GAME_FIELD_HEIGHT_OUTER (SCREEN_HEIGHT - 2 * GAME_FIELD_OUTER)
#define GAME_FIELD_WIDTH_INNER (SCREEN_WIDTH - 2 * GAME_FIELD_INNER)
#define GAME_FIELD_WIDTH_OUTER (SCREEN_WIDTH - 2 * GAME_FIELD_OUTER)

/** PADDLE MOVEMENT */
#define PADDLE_INCREMENT_SIZE 5
#define PADDLE_LENGTH (SCREEN_HEIGHT / 5)
#define PADDLE_INCREMENT_COUNT                                                 \
    (GAME_FIELD_HEIGHT_INNER - PADDLE_LENGTH) / PADDLE_INCREMENT_SIZE
#define PADDLE_START_LOCATION_Y ((SCREEN_HEIGHT / 2) - (PADDLE_LENGTH / 2))
#define PADDLE_EDGE_OFFSET 10
#define PADDLE_WIDTH 10

/** PADDLE MOVING FLAGS */
#define START_LEFT 1
#define START_RIGHT 2

/** HELPER MACRO TO RESOLVE SDL KEYCODES */
#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

const unsigned char start_left = START_LEFT;
const unsigned char start_right = START_RIGHT;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

TaskHandle_t LeftPaddleTask = NULL;
TaskHandle_t RightPaddleTask = NULL;
TaskHandle_t PongControlTask = NULL;
TaskHandle_t PausedStateTask = NULL;
// Main menu tasks
TaskHandle_t SinglePlayerMenu = NULL;
TaskHandle_t MultiPlayerMenu = NULL;
TaskHandle_t ScoreMenu = NULL;
TaskHandle_t CheatsMenu = NULL;
TaskHandle_t ExitMenu = NULL;


SemaphoreHandle_t ScreenLock = NULL;
SemaphoreHandle_t DrawSignal = NULL;

static QueueHandle_t LeftScoreQueue = NULL;
static QueueHandle_t RightScoreQueue = NULL;
static QueueHandle_t StartDirectionQueue = NULL;
static QueueHandle_t BallYQueue = NULL;
static QueueHandle_t PaddleYQueue = NULL;
static QueueHandle_t debounceQueue = NULL;
static QueueHandle_t restartGameQueue = NULL;

static SemaphoreHandle_t BallInactive = NULL;

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

void vIncrementPaddleY(unsigned short *paddle)
{
    if (paddle)
        if (*paddle != 0) {
            (*paddle)--;
        }
    if (paddle)
        if (*paddle != 0) {
            (*paddle)--;
        }
}

void vDecrementPaddleY(unsigned short *paddle)
{
    if (paddle)
        if (*paddle != PADDLE_INCREMENT_COUNT) {
            (*paddle)++;
        }
    if (paddle)
        if (*paddle != PADDLE_INCREMENT_COUNT) {
            (*paddle)++;
        }
}

unsigned char xCheckPongRightInput(unsigned short *right_paddle_y)
{
    xGetButtonInput(); // Update global button data

    if (xSemaphoreTake(buttons.lock, portMAX_DELAY) == pdTRUE) {
        if (buttons.buttons[KEYCODE(UP)]) {
            vIncrementPaddleY(right_paddle_y);
            xSemaphoreGive(buttons.lock);
            return 1;
        }
        if (buttons.buttons[KEYCODE(DOWN)]) {
            vDecrementPaddleY(right_paddle_y);
            xSemaphoreGive(buttons.lock);
            return 1;
        }
    }
    xSemaphoreGive(buttons.lock);
    return 0;
}

unsigned char xCheckPongLeftInput(unsigned short *left_paddle_y)
{
    xGetButtonInput(); // Update global button data

    if (xSemaphoreTake(buttons.lock, portMAX_DELAY) == pdTRUE) {
        if (buttons.buttons[KEYCODE(W)]) {
            vIncrementPaddleY(left_paddle_y);
            xSemaphoreGive(buttons.lock);
            return 1;
        }
        if (buttons.buttons[KEYCODE(S)]) {
            vDecrementPaddleY(left_paddle_y);
            xSemaphoreGive(buttons.lock);
            return 1;
        }
    }
    xSemaphoreGive(buttons.lock);
    return 0;
}

unsigned char xCheckForInput(void)
{
    if (xCheckPongLeftInput(NULL) || xCheckPongRightInput(NULL)) {
        return 1;
    }
    return 0;
}

void playBallSound(void *args)
{
    tumSoundPlaySample(a3);
}

void vDrawHelpText(void)
{
    static char str[100] = { 0 };
    static int text_width;
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)20);

    sprintf(str, "[Q]uit [P]ause [R]estart");

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        tumDrawText(str,
                    SCREEN_WIDTH - text_width - DEFAULT_FONT_SIZE * 2.5,
                    DEFAULT_FONT_SIZE * 2.5, White);

    tumFontSetSize(prev_font_size);
}

void vDrawWall(wall_t *wall)
{
    tumDrawFilledBox(wall->x1, wall->y1, wall->w, wall->h, wall->colour);
}

void vDrawPaddle(wall_t *wall, unsigned short y_increment)
{
    // Set wall Y
    setWallProperty(wall, 0,
                    y_increment * PADDLE_INCREMENT_SIZE + GAME_FIELD_INNER +
                    2,
                    0, 0, SET_WALL_Y);
    // Draw wall
    vDrawWall(wall);
}

#define SCORE_CENTER_OFFSET 20
#define SCORE_TOP_OFFSET 50

void vDrawScores(unsigned int left, unsigned int right)
{
    static char buffer[5];
    static int size;
    sprintf(buffer, "%u", right);
    tumGetTextSize(buffer, &size, NULL);
    tumDrawText(buffer, SCREEN_WIDTH / 2 - size - SCORE_CENTER_OFFSET,
                SCORE_TOP_OFFSET, White);

    sprintf(buffer, "%u", left);
    tumDrawText(buffer, SCREEN_WIDTH / 2 + SCORE_CENTER_OFFSET,
                SCORE_TOP_OFFSET, White);
}

typedef struct player_data {
    wall_t *paddle;
    unsigned short paddle_position;
} player_data_t;

void vResetPaddle(wall_t *wall)
{
    setWallProperty(wall, 0, PADDLE_INCREMENT_COUNT / 2, 0, 0, SET_WALL_Y);
}

void vRightWallCallback(void *player_data)
{
    // Reset ball's position and speed and increment left player's score
    const unsigned char point = 1;

    if (RightScoreQueue) {
        xQueueSend(RightScoreQueue, &point, portMAX_DELAY);
    }

    vResetPaddle(((player_data_t *)player_data)->paddle);

    xSemaphoreGive(BallInactive);

    xQueueOverwrite(StartDirectionQueue, &start_right);
}

void vRightPaddleTask(void *pvParameters)
{
    player_data_t right_player = { 0 };
    right_player.paddle_position = PADDLE_INCREMENT_COUNT / 2;

    // Right wall
    wall_t *right_wall =
        createWall(GAME_FIELD_INNER + GAME_FIELD_WIDTH_INNER,
                   GAME_FIELD_OUTER, WALL_THICKNESS,
                   GAME_FIELD_HEIGHT_OUTER, 0.1, White,
                   &vRightWallCallback, &right_player);
    // Right paddle
    right_player.paddle =
        createWall(SCREEN_WIDTH - PADDLE_EDGE_OFFSET - PADDLE_WIDTH -
                   GAME_FIELD_INNER,
                   PADDLE_START_LOCATION_Y, PADDLE_WIDTH, PADDLE_LENGTH,
                   0.1, White, NULL, NULL);

    RightScoreQueue = xQueueCreate(10, sizeof(unsigned char));

    while (1) {
        // Get input
        if (!ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {   // PLAYER
            xCheckPongRightInput(&right_player.paddle_position);
        }

        taskENTER_CRITICAL();
        if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
            vDrawWall(right_wall);
            vDrawPaddle(right_player.paddle,
                        right_player.paddle_position);
        }
        xSemaphoreGive(ScreenLock);
        taskEXIT_CRITICAL();
    }
}

void vLeftWallCallback(void *player_data)
{
    // Reset ball's position and speed and increment right player's score
    const unsigned char point = 1;

    if (LeftScoreQueue) {
        xQueueSend(LeftScoreQueue, &point, portMAX_DELAY);
    }

    vResetPaddle(((player_data_t *)player_data)->paddle);

    xSemaphoreGive(BallInactive);

    xQueueOverwrite(StartDirectionQueue, &start_left);
}

void vLeftPaddleTask(void *pvParameters)
{
    player_data_t left_player = { 0 };
    left_player.paddle_position = PADDLE_INCREMENT_COUNT / 2;

    // Left wall
    wall_t *left_wall =
        createWall(GAME_FIELD_OUTER, GAME_FIELD_OUTER, WALL_THICKNESS,
                   GAME_FIELD_HEIGHT_OUTER, 0.1, White,
                   &vLeftWallCallback, &left_player);
    // Left paddle
    left_player.paddle = createWall(GAME_FIELD_INNER + PADDLE_EDGE_OFFSET,
                                    PADDLE_START_LOCATION_Y, PADDLE_WIDTH,
                                    PADDLE_LENGTH, 0.1, White, NULL, NULL);

    LeftScoreQueue = xQueueCreate(10, sizeof(unsigned char));

    while (1) {
        // Get input
        if (!ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {   // PLAYER
            xCheckPongLeftInput(&left_player.paddle_position);
        }

        taskENTER_CRITICAL();

        if (xSemaphoreTake(ScreenLock, 0) == pdTRUE) {
            vDrawWall(left_wall);
            vDrawPaddle(left_player.paddle,
                        left_player.paddle_position);
        }

        xSemaphoreGive(ScreenLock);
        taskEXIT_CRITICAL();
    }
}

void vWakePaddles(void)
{
    if (xTaskNotify(LeftPaddleTask, 0x0, eSetValueWithOverwrite) != 
        pdPASS) {
        fprintf(stderr,
                "[ERROR] Task Notification to LeftPaddleTask failed\n");
    }
    if (xTaskNotify(RightPaddleTask, 0x0, eSetValueWithOverwrite) !=
        pdPASS) {
        fprintf(stderr,
                "[ERROR] Task Notification to RightPaddleTask failed\n");
    }
}

void vPongControlTask(void *pvParameters)
{
    TickType_t xLastWakeTime, prevWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    prevWakeTime = xLastWakeTime;
    const TickType_t updatePeriod = 10;
    unsigned char score_flag;

    image_handle_t philipp = tumDrawLoadImage("../resources/philipp.bmp");

    ball_t *my_ball = createBall(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, White,
                                 25, 1000, &playBallSound, NULL, philipp);

    unsigned char ball_active = 0;
    unsigned char ball_direction = 0;

    unsigned int left_score = 0;
    unsigned int right_score = 0;

    BallInactive = xSemaphoreCreateBinary();
    if (!BallInactive) {
        exit(EXIT_FAILURE);
    }
    StartDirectionQueue = xQueueCreate(1, sizeof(unsigned char));
    if (!StartDirectionQueue) {
        exit(EXIT_FAILURE);
    }
    BallYQueue = xQueueCreate(5, sizeof(unsigned long));
    if (!BallYQueue) {
        exit(EXIT_FAILURE);
    }
    PaddleYQueue = xQueueCreate(5, sizeof(unsigned long));
    if (!PaddleYQueue) {
        exit(EXIT_FAILURE);
    }

    setBallSpeed(my_ball, 250, 250, 0, SET_BALL_SPEED_AXES);

    // Top wall
    wall_t *top_wall = createWall(GAME_FIELD_INNER, GAME_FIELD_OUTER,
                                  GAME_FIELD_WIDTH_INNER, WALL_THICKNESS,
                                  0.1, White, NULL, NULL);
    // Bottom wall
    wall_t *bottom_wall = createWall(
                              GAME_FIELD_INNER, GAME_FIELD_INNER + GAME_FIELD_HEIGHT_INNER,
                              GAME_FIELD_WIDTH_INNER, WALL_THICKNESS, 0.1, White, NULL, NULL);

    while (1) {
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    xQueueReceive(restartGameQueue, &buttons.buttons[KEYCODE(R)], 0);
                    xSemaphoreGive(buttons.lock);
                }
                
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(P)]) {
                        xSemaphoreGive(buttons.lock);
                        if (PausedStateTask) {
                            vTaskResume(PausedStateTask);
                        }
                        if (LeftPaddleTask) {
                            vTaskSuspend(LeftPaddleTask);
                        }
                        if (RightPaddleTask) {
                            vTaskSuspend(RightPaddleTask);
                        }
                        if (PongControlTask) {
                            vTaskSuspend(PongControlTask);
                        }
                    }
                    else if (buttons.buttons[KEYCODE(R)]) {
                        xSemaphoreGive(buttons.lock);
                        ball_active = 0;
                        setBallLocation(
                            my_ball,
                            SCREEN_WIDTH / 2,
                            SCREEN_HEIGHT / 2);
                        setBallSpeed(
                            my_ball, 0, 0, 0,
                            SET_BALL_SPEED_AXES);
                        left_score = 0;
                        right_score = 0;
                    }
                    else {
                        xSemaphoreGive(buttons.lock);
                    }
                }

                // Ball is no longer active
                if (xSemaphoreTake(BallInactive, 0) == pdTRUE) {
                    ball_active = 0;
                }

                if (!ball_active) {
                    setBallLocation(my_ball,
                                    SCREEN_WIDTH / 2,
                                    SCREEN_HEIGHT / 2);
                    setBallSpeed(my_ball, 0, 0, 0,
                                 SET_BALL_SPEED_AXES);

                    if (xCheckForInput()) {
                        xQueueReceive(
                            StartDirectionQueue,
                            &ball_direction, 0);
                        ball_active = 1;
                        switch (ball_direction) {
                            case START_LEFT:
                                setBallSpeed(
                                    my_ball,
                                    -(rand() % 100 +
                                      200),
                                    ((rand() % 2) *
                                     2 -
                                     1) * (100 +
                                           (rand() %
                                            200)),
                                    0,
                                    SET_BALL_SPEED_AXES);
                                break;
                            default:
                            case START_RIGHT:
                                setBallSpeed(
                                    my_ball,
                                    rand() % 100 +
                                    200,
                                    ((rand() % 2) *
                                     2 -
                                     1) * (100 +
                                           (rand() %
                                            200)),
                                    0,
                                    SET_BALL_SPEED_AXES);
                                break;
                        }
                    }
                }

                vWakePaddles();

                // Check if ball has made a collision
                checkBallCollisions(my_ball, NULL, NULL);

                // Update the balls position now that possible collisions have
                // updated its speeds
                updateBallPosition(
                    my_ball, xLastWakeTime - prevWakeTime);

                unsigned long ball_y = my_ball->y;
                xQueueSend(BallYQueue, (void *)&ball_y, 0);

                taskENTER_CRITICAL();

                if (xSemaphoreTake(ScreenLock, portMAX_DELAY) ==
                    pdTRUE) {
                    tumDrawClear(Black);

                    vDrawFPS();

                    // Draw the walls
                    vDrawWall(top_wall);
                    vDrawWall(bottom_wall);
                    vDrawHelpText();

                    // Check for score updates
                    if (RightScoreQueue) {
                        while (xQueueReceive(
                                   RightScoreQueue,
                                   &score_flag,
                                   0) == pdTRUE) {
                            right_score++;
                        }
                    }
                    if (LeftScoreQueue) {
                        while (xQueueReceive(
                                   LeftScoreQueue,
                                   &score_flag,
                                   0) == pdTRUE) {
                            left_score++;
                        }
                    }

                    vDrawScores(left_score, right_score);

                    // Draw the ball
                    if (my_ball->sprite) {
                        tumDrawLoadedImage(
                            my_ball->sprite,
                            my_ball->x -
                            tumDrawGetLoadedImageWidth(
                                my_ball->sprite) /
                            2,
                            my_ball->y -
                            tumDrawGetLoadedImageHeight(
                                my_ball->sprite) /
                            2);
                    }
                    else
                        tumDrawCircle(my_ball->x,
                                      my_ball->y,
                                      my_ball->radius,
                                      my_ball->colour);
                }

                xSemaphoreGive(ScreenLock);
                taskEXIT_CRITICAL();

                // Keep track of when task last ran so that you know how many ticks
                //(in our case miliseconds) have passed so that the balls position
                // can be updated appropriatley
                prevWakeTime = xLastWakeTime;
                vTaskDelayUntil(&xLastWakeTime, updatePeriod);
            }
        }
    }
}

#define RESUME 0
#define MAIN_MENU 1
// TODO: Make sure that front and back buffer are filled
void vPausedStateTask(void *pvParameters)
{
    TickType_t last_change = xTaskGetTickCount();

    static const char *paused_text1 = "RESUME";
    static const char *paused_text2 = "RETURN TO MAIN MENU";
    static int paused_text_width1;
    static int paused_text_width2;

    static char selection = 0;

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
                            if (PausedStateTask) {
                                vTaskSuspend(PausedStateTask);
                            }
                        }else if(selection == MAIN_MENU){
                            last_change = xTaskGetTickCount();
                            xQueueSendToFront(debounceQueue, &last_change, portMAX_DELAY);
                            if (SinglePlayerMenu) {
                                vTaskResume(SinglePlayerMenu);
                            }
                            if (PausedStateTask) {
                                vTaskSuspend(PausedStateTask);
                            }
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
    debounceQueue = xQueueCreate(1, sizeof(TickType_t));
    restartGameQueue = xQueueCreate(1, sizeof(unsigned int));
    static const unsigned int restartSignal = 1;

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
                            if (SinglePlayerMenu) {
                                vTaskSuspend(SinglePlayerMenu);
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

int pongInit(void)
{
    //Random numbers
    srand(time(NULL));

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_button_lock;
    }

    DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!DrawSignal) {
        PRINT_ERROR("Failed to create draw signal");
        goto err_draw_signal;
    }
    ScreenLock = xSemaphoreCreateMutex();
    if (!ScreenLock) {
        PRINT_ERROR("Failed to create screen lock");
        goto err_screen_lock;
    }

    if (xTaskCreate(vLeftPaddleTask, "LeftPaddleTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &LeftPaddleTask) != pdPASS) {
        PRINT_TASK_ERROR("LeftPaddleTask");
        goto err_leftpaddle;
    }
    if (xTaskCreate(vRightPaddleTask, "RightPaddleTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &RightPaddleTask) != pdPASS) {
        PRINT_TASK_ERROR("RightPaddleTask");
        goto err_rightpaddle;
    }
    if (xTaskCreate(vPausedStateTask, "PausedStateTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &PausedStateTask) != pdPASS) {
        PRINT_TASK_ERROR("PausedStateTask");
        goto err_pausedstate;
    }
    if (xTaskCreate(vPongControlTask, "PongControlTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &PongControlTask) != pdPASS) {
        PRINT_TASK_ERROR("PongControlTask");
        goto err_pongcontrol;
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

    vTaskSuspend(LeftPaddleTask);
    vTaskSuspend(RightPaddleTask);
    vTaskSuspend(PongControlTask);
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
    vTaskDelete(PongControlTask);
err_pongcontrol:
    vTaskDelete(PausedStateTask);
err_pausedstate:
    vTaskDelete(RightPaddleTask);
err_rightpaddle:
    vTaskDelete(LeftPaddleTask);
err_leftpaddle:
    vSemaphoreDelete(ScreenLock);
err_screen_lock:
    vSemaphoreDelete(DrawSignal);
err_draw_signal:
    vSemaphoreDelete(buttons.lock);
err_button_lock:
    return -1;
}
