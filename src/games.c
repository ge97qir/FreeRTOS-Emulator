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

#include "main.h" // Must be before FreeRTOS includes
#include "games.h"
#include "menu.h"

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

// SPACE SHIP DIMENSIONS
//#define SHIP_WIDTH 30
//#define SHIP_HEIGHT 15
#define SHIP_SPEED 4

// BULLET PROPERTIES
#define FRIENDLY_BULLET 0
#define ENEMY_BULLET 1
#define DESTROY_BULLET 1

#define BULLET_RADIUS 2
#define BULLET_WIDTH 2 
#define BULLET_HEIGHT 5
#define BULLET_SPEED 800


/** PADDLE MOVING FLAGS */
#define START_LEFT 1
#define START_RIGHT 2

const unsigned char start_left = START_LEFT;
const unsigned char start_right = START_RIGHT;

buttons_buffer_t buttons = { 0 };

TaskHandle_t LeftPaddleTask = NULL;
TaskHandle_t RightPaddleTask = NULL;
TaskHandle_t PongControlTask = NULL;
TaskHandle_t MultiPlayerGame = NULL;

SemaphoreHandle_t ScreenLock = NULL;
SemaphoreHandle_t DrawSignal = NULL;

static QueueHandle_t LeftScoreQueue = NULL;
static QueueHandle_t RightScoreQueue = NULL;
static QueueHandle_t StartDirectionQueue = NULL;
static QueueHandle_t BallYQueue = NULL;
static QueueHandle_t PaddleYQueue = NULL;

static SemaphoreHandle_t ResetRightPaddle = NULL;
static SemaphoreHandle_t ResetLeftPaddle = NULL;
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
                    y_increment * PADDLE_INCREMENT_SIZE + GAME_FIELD_INNER + 2,
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

        if (xSemaphoreTake(ResetRightPaddle, 0) == pdTRUE) {
            right_player.paddle_position = PADDLE_INCREMENT_COUNT / 2;
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

        // reset the player position if the game was restarted
        if (xSemaphoreTake(ResetLeftPaddle, 0) == pdTRUE) {
            left_player.paddle_position = PADDLE_INCREMENT_COUNT / 2;
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
    ResetLeftPaddle = xSemaphoreCreateBinary();
    if (!ResetLeftPaddle) {
        exit(EXIT_FAILURE);
    }
    ResetRightPaddle = xSemaphoreCreateBinary();
    if (!ResetRightPaddle) {
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

                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    xQueueReceive(restartGameQueue, &buttons.buttons[KEYCODE(R)], 0);
                    xSemaphoreGive(buttons.lock);
                }

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
                        xSemaphoreGive(ResetLeftPaddle);
                        xSemaphoreGive(ResetRightPaddle);
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

void vDrawScore(unsigned int *score)
{
    static char str[100] = { 0 };
    static int text_width;
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)20);

    sprintf(str, "Score: %u", *score);

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        tumDrawText(str,
                    text_width - DEFAULT_FONT_SIZE * 2.5,
                    DEFAULT_FONT_SIZE * 2.5, White);

    tumFontSetSize(prev_font_size);
}

void vIncrement(unsigned short *position, int obj_width)
{
    for (int i = 0; i < SHIP_SPEED; i++){
        if (position){
            if (*position <= SCREEN_WIDTH - obj_width / 2) {
                (*position)++;
            }else{
                break;
            }
        }
    }
}

void vDecrement(unsigned short *position, int obj_width)
{
    for (int i = 0; i < SHIP_SPEED; i++){
        if (position){
        if (*position != 0 + obj_width / 2) {
            (*position)--;
        }else{
                break;
            }
        }
    }
}

unsigned char xCheckPlayerInput(unsigned short *player_position_x, int obj_width)
{
    xGetButtonInput(); // Update global button data

    if (xSemaphoreTake(buttons.lock, portMAX_DELAY) == pdTRUE) {
        if (buttons.buttons[KEYCODE(A)] || buttons.buttons[KEYCODE(LEFT)]) {
            vDecrement(player_position_x, obj_width);
            xSemaphoreGive(buttons.lock);
            return 1;
        }
        if (buttons.buttons[KEYCODE(D)] || buttons.buttons[KEYCODE(RIGHT)]) {
            vIncrement(player_position_x, obj_width);
            xSemaphoreGive(buttons.lock);
            return 1;
        }
    }
    xSemaphoreGive(buttons.lock);
    return 0;
}

typedef struct player_info {
    wall_t *ship;
    unsigned short ship_position;
    unsigned short lives;
    unsigned int score;
} space_ship_t;

typedef struct bullet_data {
    ball_t *bullet;
    signed short bullet_position;
    char allegiance; // friendly or enemy bullet
} bullet_t;

void resetPlayerData (space_ship_t *player){
    player->ship_position = SCREEN_WIDTH / 2;
    player->score = 0;
    player->lives = 3;
}

void updateBulletPosition(ball_t *bullet, unsigned int time_since_last_update){
    if (bullet->y >= (0 + BULLET_RADIUS) && bullet->y <= (SCREEN_HEIGHT - BULLET_RADIUS)){
        updateBallPosition(bullet, time_since_last_update);
    }else{
        //if (bullet->y <= (0 + BULLET_RADIUS) || bullet->y >= (SCREEN_HEIGHT - BULLET_RADIUS)){
        setBallSpeed(bullet, 0, 0, 0, SET_BALL_SPEED_Y);
    }
}

ball_t *shootBullet(ball_t *bullet, unsigned short ship_position){
    
    setBallLocation(bullet, 
                    ship_position,
                    SCREEN_HEIGHT - 3 * 30);
    setBallSpeed(bullet, 0, -BULLET_SPEED, 1000, SET_BALL_SPEED_Y);
    return bullet;
}

void vMultiPlayerGame(void *pvParameters){ 
    
    image_handle_t myship = tumDrawLoadImage("../resources/myship_small.bmp");


    bullet_t my_bullet = { 0 };
    my_bullet.bullet = 
        createBall(BULLET_RADIUS,
                   BULLET_RADIUS,
                   White, BULLET_RADIUS, 1000, NULL, NULL, NULL);;
    my_bullet.bullet_position = SCREEN_HEIGHT - 3 * tumDrawGetLoadedImageHeight(myship); // ship position in y coordinates
    my_bullet.allegiance = FRIENDLY_BULLET;

    TickType_t xLastWakeTime, prevWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    prevWakeTime = xLastWakeTime;
    const TickType_t updatePeriod = 10;


    space_ship_t my_ship = { 0 };
    my_ship.ship_position = SCREEN_WIDTH / 2; // this is the position of the middle of the ship
    my_ship.ship =
        createWall(SCREEN_WIDTH / 2 - tumDrawGetLoadedImageWidth(myship) / 2,
                   SCREEN_HEIGHT - 3 * tumDrawGetLoadedImageHeight(myship), 
                   tumDrawGetLoadedImageWidth(myship), 
                   tumDrawGetLoadedImageHeight(myship), 
                   0, White, NULL, NULL);


    while(1){
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                /*
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    xQueueReceive(restartGameQueue, &buttons.buttons[KEYCODE(R)], 0);
                    xSemaphoreGive(buttons.lock);
                }
                */
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(P)]) {
                        xSemaphoreGive(buttons.lock);
                        if (PausedStateTask) {
                            vTaskResume(PausedStateTask);
                        }
                        if (MultiPlayerGame) {
                            vTaskSuspend(MultiPlayerGame);
                        }
                    }
                    else if (buttons.buttons[KEYCODE(R)]) {
                        xSemaphoreGive(buttons.lock);
                        resetPlayerData(&my_ship);
                        // reset space invaders
                    }
                    else{
                        xSemaphoreGive(buttons.lock);
                    }
                }

                /*
                updateBulletPosition(
                    my_bullet, xLastWakeTime - prevWakeTime);
                updateBulletPosition(
                    enemy_bullet, xLastWakeTime - prevWakeTime);
                */

                // when space is pressed bullet is created at ship_position and then it starts moving up...
                // updating position with passing time my_bullet goes up and enemy bullet goes down

                xCheckPlayerInput(&my_ship.ship_position, tumDrawGetLoadedImageWidth(myship));
                my_ship.ship->x1 = my_ship.ship_position - tumDrawGetLoadedImageWidth(myship) / 2;
                /*
                update_interval = (xLastWakeTime - prevWakeTime) / 10;
                my_bullet.bullet->y1 -= round(BULLET_SPEED * update_interval);
                */
                
                //destroy bullet object if out of bounds
                
                updateBulletPosition(my_bullet.bullet, 
                    xLastWakeTime - prevWakeTime );
                
                    
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(SPACE)]) {
                        xSemaphoreGive(buttons.lock);
                        if (my_bullet.bullet->dy == 0) {
                            my_bullet.bullet = shootBullet(my_bullet.bullet, my_ship.ship_position);
                        }          
                    }
                    xSemaphoreGive(buttons.lock);
                
                }
                

                taskENTER_CRITICAL();
                if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
                    
                    tumDrawClear(Black);
                    vDrawFPS();
                    vDrawHelpText();
                    vDrawScore(&my_ship.score);

                    if (myship) {
                        tumDrawLoadedImage(myship,
                                           my_ship.ship->x1,
                                           //tumDrawGetLoadedImageWidth(myship) / 2,
                                           my_ship.ship->y1
                                           //tumDrawGetLoadedImageHeight(myship) / 2);
                                           );
                    }else{
                        tumDrawFilledBox(my_ship.ship->x1, my_ship.ship->y1, 
                                         my_ship.ship->w, my_ship.ship->h, White);
                    }

                    if (my_bullet.bullet){
                        tumDrawCircle(my_bullet.bullet->x, my_bullet.bullet->y,
                                         my_bullet.bullet->radius, White);
                    }
                }
                xSemaphoreGive(ScreenLock);
                taskEXIT_CRITICAL();

                // Keep track of when task last ran so that you know how many ticks
                // (in our case miliseconds) have passed so that the position
                // can be updated appropriatley
                prevWakeTime = xLastWakeTime;
                vTaskDelayUntil(&xLastWakeTime, updatePeriod);
            }
        }
    }
}

int gamesInit(void)
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
    if (xTaskCreate(vPongControlTask, "PongControlTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &PongControlTask) != pdPASS) {
        PRINT_TASK_ERROR("PongControlTask");
        goto err_pongcontrol;
    }
    if (xTaskCreate(vMultiPlayerGame, "MultiPlayerGame",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &MultiPlayerGame) != pdPASS) {
        PRINT_TASK_ERROR("MultiPlayerGame");
        goto err_multiplayergame;
    }

    vTaskSuspend(LeftPaddleTask);
    vTaskSuspend(RightPaddleTask);
    vTaskSuspend(PongControlTask);
    vTaskSuspend(MultiPlayerGame);

    return 0;

err_multiplayergame:
    vTaskDelete(PongControlTask);
err_pongcontrol:
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
