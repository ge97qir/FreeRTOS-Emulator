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

#include "games.h"
#include "menu.h"
#include "main.h" // Must be before FreeRTOS includes

// the semphr.h and task.h are in the header
#include "queue.h"

/** GAME DIMENSIONS */
#define GAME_BORDER 10

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

// GAME PROPERTIES
//#define MAX_LEVEL 3
#define UDP_BUFFER_SIZE 1024
#define UDP_RECEIVE_PORT 1234
#define UDP_TRANSMIT_PORT 1235

#define RESET 1
#define EXPLOSION_DELAY 25  // the delay when an enemy or player gets hit by a bullet and explosion happens
// SPACE SHIP DIMENSIONS
#define MY_SHIP_WIDTH 52
#define MY_SHIP_HEIGHT 30
#define MY_SHIP_SPEED 2

// adjusted for invaders just before inasion so they are just above ground
#define GROUND_POSITION (SCREEN_HEIGHT - 2 * MY_SHIP_HEIGHT - 10)
#define MY_SHIP_Y_POSITION (GROUND_POSITION - MY_SHIP_HEIGHT - 3)

// BULLET PROPERTIES
#define PASSIVE 0
#define ATTACKING 1
#define FRIENDLY_BULLET 0
#define ENEMY_BULLET 1
#define DESTROY_BULLET 1

#define BULLET_RADIUS 2
//#define BULLET_WIDTH 2 
//#define BULLET_HEIGHT 5
#define BULLET_SPEED 1700
#define RESET_BULLET 9999

// bullet is shot after 2 seconds since the beginning of the game 
#define BULLET_START_DELAY 2000

// INVADERS PROPERTIES
#define ENEMY_ROWS 5
#define ENEMY_COLUMNS 11
#define RIGHT 0
#define LEFT 1
#define ALIVE 0
#define DEAD 1
#define GAP_SIZE 15 // gap between the invaders

#define MAX_ENEMY_BULLETS 3

// BUNKER PROPERTIES
#define NUMBER_OF_BUNKERS 4
#define BUNKER_WIDTH 8 // these units are in blocks 
#define BUNKER_HEIGHT 6 
// individual bunker block is a square
#define BUNKER_BLOCK_LENGTH 13 // unit in pixels
#define DISTANCE_BETWEEN_BUNKERS (SCREEN_WIDTH - NUMBER_OF_BUNKERS * BUNKER_WIDTH * BUNKER_BLOCK_LENGTH) / (NUMBER_OF_BUNKERS + 1)
#define BUNKER_LOCATION_Y GROUND_POSITION - (BUNKER_HEIGHT * BUNKER_BLOCK_LENGTH) - 3 * MY_SHIP_HEIGHT
//#define BUNKER_CLEARANCE 100 // distance from the ground to the bunker top

// MYSTERY SHIP
#define MYSTERY_SHIP_POSITION_Y 138
#define MYSTERY_SHIP_SPEED 1
#define MYSTERY_SHIP_PERIOD 20000    // 20 seconds

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

static QueueHandle_t killedInvaderQueue = NULL;

static SemaphoreHandle_t ResetRightPaddle = NULL;
static SemaphoreHandle_t ResetLeftPaddle = NULL;
static SemaphoreHandle_t BallInactive = NULL;

// AI *******************************************

TaskHandle_t UDPControlTask = NULL;

static QueueHandle_t NextKeyQueue = NULL;
static QueueHandle_t PlayerPositionQueue = NULL;
static QueueHandle_t MothershipPositionQueue = NULL;
static QueueHandle_t BulletQueue = NULL;
static QueueHandle_t DifficultyQueue = NULL;
static QueueHandle_t BinaryStateQueue = NULL;

static SemaphoreHandle_t HandleUDP = NULL;

aIO_handle_t udp_soc_receive = NULL, udp_soc_transmit = NULL;

typedef enum { NONE = 0, INC = 1, DEC = -1 } opponent_cmd_t;

void UDPHandler(size_t read_size, char *buffer, void *args)
{
    opponent_cmd_t next_key = NONE;
    BaseType_t xHigherPriorityTaskWoken1 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken2 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken3 = pdFALSE;

    if (xSemaphoreTakeFromISR(HandleUDP, &xHigherPriorityTaskWoken1) ==
        pdTRUE) {

        char send_command = 0;
        if (strncmp(buffer, "INC", 
                         (read_size < 3) ? read_size : 3) == 0) {
            next_key = INC;
            send_command = 1;
        }
        else if (strncmp(buffer, "DEC",
                         (read_size < 3) ? read_size : 3) == 0) {
            next_key = DEC;
            send_command = 1;
        }
        else if (strncmp(buffer, "NONE",
                         (read_size < 4) ? read_size : 4) == 0) {
            next_key = NONE;
            send_command = 1;
        }

        if (NextKeyQueue && send_command) {
            xQueueSendFromISR(NextKeyQueue, (void *)&next_key,
                              &xHigherPriorityTaskWoken2);
        }
        xSemaphoreGiveFromISR(HandleUDP, &xHigherPriorityTaskWoken3);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken1 |
                           xHigherPriorityTaskWoken2 |
                           xHigherPriorityTaskWoken3);
    }
    else {
        fprintf(stderr, "[ERROR] Overlapping UDPHandler call\n");
    }
}

void vUDPControlTask(void *pvParameters)
{
    static char buf[50];
    char *addr = NULL; // Loopback
    in_port_t port = UDP_RECEIVE_PORT;
    
    unsigned int player_x = 0;
    unsigned int mothership_x = 0;
    
    char last_binary_state = -1;
    char binary_state = OFF;
    
    char last_bullet_state = -1;
    char bullet_state = PASSIVE;

    char last_difficulty = -1;
    char current_difficulty = 1;

    udp_soc_receive =
        aIOOpenUDPSocket(addr, port, UDP_BUFFER_SIZE, UDPHandler, NULL);

    printf("UDP socket opened on port %d\n", port);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15));
        while (xQueueReceive(PlayerPositionQueue, &player_x, 0) == pdTRUE) {
        }
        while (xQueueReceive(MothershipPositionQueue, &mothership_x, 0) == pdTRUE) {
        }
        while (xQueueReceive(DifficultyQueue, &current_difficulty, 0) == pdTRUE) {
        }
        while (xQueueReceive(BulletQueue, &bullet_state, 0) == pdTRUE) {
        }
        while (xQueueReceive(BinaryStateQueue, &binary_state, 0) == pdTRUE) {
        }
        // player and mothership position
        signed int diff = player_x - mothership_x;
        if (diff > 0) {
            sprintf(buf, "+%d", diff);
        }
        else {
            sprintf(buf, "-%d", -diff);
        }
        aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, buf,
                     strlen(buf));
        // bullet state
        if (last_bullet_state != bullet_state) {
            if (bullet_state == PASSIVE){
                sprintf(buf, "PASSIVE");
            }else{
                sprintf(buf, "ATTACKING");
            }
            aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, buf,
                         strlen(buf));
            last_bullet_state = bullet_state;
        } 

        // AI difficulty
        if (last_difficulty != current_difficulty) {
            sprintf(buf, "D%d", (current_difficulty + 1));
            aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, buf,
                         strlen(buf));
            last_difficulty = current_difficulty;
        }
        // binary state
        if (last_binary_state != binary_state) {
            if (binary_state == OFF){
                sprintf(buf, "PAUSE");
            }else{
                sprintf(buf, "RESUME");
            }
            aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, buf,
                         strlen(buf));
            last_binary_state = binary_state;
        }

    }
}


// AI ******************************************



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

void vDrawHelpText(char mode, char difficulty)
{
    static char str[100] = { 0 };
    static int text_width1;
    static int text_width2;
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)20);

    sprintf(str, "[Q]uit [P]ause [R]estart");

    if (!tumGetTextSize((char *)str, &text_width1, NULL)){
        tumDrawText(str,
                    SCREEN_WIDTH - text_width1 - DEFAULT_FONT_SIZE * 2.5,
                    DEFAULT_FONT_SIZE * 1.5, White);
    }
    
    if (mode) {
        sprintf(str, "AI [M]ode");
    }
    else {
        sprintf(str, "Normal [M]ode");
    }

    tumDrawText(str,SCREEN_WIDTH - text_width1 - DEFAULT_FONT_SIZE * 2.5,
                    2 * DEFAULT_FONT_SIZE * 1.5, White);

    
    if (!tumGetTextSize((char *)str, &text_width2, NULL)){
        if (mode) {
            sprintf(str, "AI [L]evel: %d", difficulty + 1);
        
            tumDrawText(str,
                    SCREEN_WIDTH - text_width1 - DEFAULT_FONT_SIZE * 2.5 + text_width2 + DEFAULT_FONT_SIZE,
                    2 * DEFAULT_FONT_SIZE * 1.5, White);
        }
    }

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
                    //vDrawHelpText();

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


// this will be renamed player_data but now the pong is using that struct
typedef struct player_info {
    wall_t *ship;
    signed int ship_position;
    unsigned short lives;
    unsigned short score;
} space_ship_t;

typedef struct bullet_data {
    ball_t *bullet;
    //will have to implement this later in the bullet shotting functions for the mothership
    char bullet_state; // ATTACKING (on the screeen) or PASSIVE (not on she screen)
} bullet_t;

typedef struct invader_unit_data {
    wall_t *enemy;
    unsigned short *points; // how much is the enemy worth
    unsigned short dead; // alive or dead so we know if we need to draw it and detect colisions
    unsigned short image_state; // alternating between images
    unsigned int *width;
    unsigned int *height;
} enemy_t;

typedef struct invader_group_data {
    enemy_t enemys[ENEMY_ROWS][ENEMY_COLUMNS];
    unsigned short direction;
    unsigned int killed_invaders;
    unsigned int speed; // speed is a function of killed invaders
} invaders_t;

typedef struct bunker_block {
    wall_t *bunker_block;
    unsigned short block_row;
    unsigned short block_column;
    unsigned short dead; // alive or dead so whe know if we need to draw it and detect colisions
} block_t;

typedef struct bunker_data {
    block_t bunker[BUNKER_HEIGHT][BUNKER_WIDTH];
    unsigned int bunker_y_location; // point in the top left corner of the bunker
    unsigned int bunker_x_location;
} bunker_t;

void vDrawNextLevel(void){
    static char str1[100] = { 0 };
    static char str2[100] = { 0 };
    static int text_width1;
    static int text_width2;
    static int text_height;
    static unsigned int blinky = 0;

    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)30);

    sprintf(str1, "YOU ADVANCED TO THE NEXT LEVEL!");

    if (!tumGetTextSize((char *)str1, &text_width1, &text_height))
        tumDrawText(str1,
                    SCREEN_WIDTH / 2 - text_width1 / 2,
                    SCREEN_HEIGHT / 2 - text_height / 2, White);

    if(blinky <= 50){
        tumFontSetSize((ssize_t)15);

        sprintf(str2, "PRESS SPACE TO CONTINUE");

        if (!tumGetTextSize((char *)str2, &text_width2, NULL))
            tumDrawText(str2,
                        SCREEN_WIDTH / 2 - text_width2 / 2,
                        SCREEN_HEIGHT / 2 + 2 * text_height, White);
        blinky++;
    }else if (blinky <= 100){
        blinky++;
    }else{
        blinky = 0;
    }

    tumFontSetSize(prev_font_size);   
}


void vDrawGameOver(void){
    static char str1[100] = { 0 };
    static char str2[100] = { 0 };
    static int text_width1;
    static int text_width2;
    static int text_height;
    static unsigned int blinky = 0;

    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)40);

    sprintf(str1, "GAME OVER");

    if (!tumGetTextSize((char *)str1, &text_width1, &text_height))
        tumDrawText(str1,
                    SCREEN_WIDTH / 2 - text_width1 / 2,
                    SCREEN_HEIGHT / 2 - text_height / 2, White);
    if(blinky <= 50){
        tumFontSetSize((ssize_t)15);

        sprintf(str2, "PRESS R TO RESTART");

        if (!tumGetTextSize((char *)str2, &text_width2, NULL))
            tumDrawText(str2,
                        SCREEN_WIDTH / 2 - text_width2 / 2,
                        SCREEN_HEIGHT / 2 + 2 * text_height, White);
        blinky++;
    }else if (blinky <= 100){
        blinky++;
    }else{
        blinky = 0;
    }

    tumFontSetSize(prev_font_size);   
}

void vDrawLevel(unsigned short *level){
    static char str[100] = { 0 };
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)20);

    sprintf(str, "Level: %hu", *level);

    if (!tumGetTextSize((char *)str, NULL, NULL))
        tumDrawText(str,
                    14 * DEFAULT_FONT_SIZE * 1.5,
                    DEFAULT_FONT_SIZE * 1.5, White);

    tumFontSetSize(prev_font_size);
}

void vDrawScore(unsigned short *score, unsigned short *highscore){
    static char str1[100] = { 0 };
    static char str2[100] = { 0 };
    static int text_width1;
    static int text_width2;
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)20);

    sprintf(str1, "Current score: %hu", *score);
    sprintf(str2, "Highscore: %hu", *highscore);

    if (!tumGetTextSize((char *)str1, &text_width1, NULL))
        tumDrawText(str1,
                    DEFAULT_FONT_SIZE * 1.5,
                    DEFAULT_FONT_SIZE * 1.5, White);

    if (!tumGetTextSize((char *)str2, &text_width2, NULL))
        tumDrawText(str2,
                    8 * DEFAULT_FONT_SIZE * 1.5,
                    DEFAULT_FONT_SIZE * 1.5, White);

    tumFontSetSize(prev_font_size);
}

void vDrawLives (unsigned short *lives, image_handle_t avatar, space_ship_t *my_ship){
    static char str[100] = { 0 };
    static int text_width;
    static int text_height;
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)28);

    sprintf(str, "%u", *lives);

    if (!tumGetTextSize((char *)str, &text_width, &text_height))
        tumDrawText(str,
                    DEFAULT_FONT_SIZE * 1.5,
                    SCREEN_HEIGHT - text_height, White);

    tumFontSetSize(prev_font_size);

    for (int i = 1; i < *lives; i++) {
        if (avatar) {
            tumDrawLoadedImage(avatar,
                               i * (5 + tumDrawGetLoadedImageWidth(avatar)),
                               SCREEN_HEIGHT - 8 - tumDrawGetLoadedImageHeight(avatar));
        }
        else{
            tumDrawFilledBox(my_ship->ship->x1, my_ship->ship->y1, 
                             my_ship->ship->w, my_ship->ship->h, Green);
        }
    }
}

// carefull with the implementation better use setWallProperty*******************************
void vIncrement(signed int *position, int obj_width, int speed){
    for (int i = 0; i < speed; i++){
        if (position){
            if (*position <= SCREEN_WIDTH - obj_width / 2) {
                (*position)++;
            }else{
                break;
            }
        }
    }
}

void vDecrement(signed int *position, int obj_width, int speed){
    for (int i = 0; i < speed; i++){
        if (position){
            if (*position != 0 + obj_width / 2) {
                (*position)--;
            }else{
                break;
                }
        }
    }
}

void vExtraLives(unsigned short *lives, unsigned short *score){
    static unsigned short extra_lives= 0;
    if (*score == 0){
        extra_lives = 0;
    }

    if (floor((*score) / 1000) - extra_lives){
        // add lives if the player has less than 3 lives (3 lives = MAX_LIVES)
        if (*lives < 3){
            *lives += 1;
        }
        // we still need to add counter to extra lives since we add lives 
        // when player reaches the threshold every 1000 points and do not add lives later
        extra_lives++;
    }

}

unsigned char xCheckPlayerInput(signed int *player_position_x, int obj_width){
    xGetButtonInput(); // Update global button data

    if (xSemaphoreTake(buttons.lock, portMAX_DELAY) == pdTRUE) {
        if (buttons.buttons[KEYCODE(A)] || buttons.buttons[KEYCODE(LEFT)]) {
            xSemaphoreGive(buttons.lock);
            vDecrement(player_position_x, obj_width, MY_SHIP_SPEED);
            return 1;
        }
        if (buttons.buttons[KEYCODE(D)] || buttons.buttons[KEYCODE(RIGHT)]) {
            xSemaphoreGive(buttons.lock);
            vIncrement(player_position_x, obj_width, MY_SHIP_SPEED);
            return 1;
        }
    }
    xSemaphoreGive(buttons.lock);
    return 0;
}


void bunkersReset (bunker_t *bunker){
    for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
        for (int bw = 0; bw < BUNKER_WIDTH; bw++){
            if (bunker->bunker[bh][bw].dead == DEAD){
                bunker->bunker[bh][bw].dead = ALIVE; // set the state to alive  
            }
        }
    }
}

void invadersReset (invaders_t *invaders, unsigned int *invader_width, unsigned int *invader_height){
    for (int row = 0; row < ENEMY_ROWS; row++){
        for (int col = 0; col < ENEMY_COLUMNS; col++){
            invaders->killed_invaders = 0;
            invaders->direction = RIGHT;
            invaders->enemys[row][col].dead = ALIVE;
            invaders->enemys[row][col].image_state = 0; // resets the image state to initial image

            if (row == 0 || row == 1){
                setWallProperty(invaders->enemys[row][col].enemy,
                            0 + col * (*invader_width + GAP_SIZE),
                            SCREEN_HEIGHT / 2 - row * (2 * *invader_height), 
                            0, 0, SET_WALL_AXES);
            }else if (row == 2 || row == 3){
                setWallProperty(invaders->enemys[row][col].enemy,
                            ((*invader_width - *invaders->enemys[row][col].width) / 2) 
                            + col * (*invader_width + GAP_SIZE),
                            SCREEN_HEIGHT / 2 - row * (2 * *invader_height), 
                            0, 0, SET_WALL_AXES);
            }else if (row == 4){
                setWallProperty(invaders->enemys[row][col].enemy,
                            ((*invader_width - *invaders->enemys[row][col].width) / 2) 
                            + col * (*invader_width + GAP_SIZE),
                            SCREEN_HEIGHT / 2 - row * (2 * *invader_height), 
                            0, 0, SET_WALL_AXES);
            }
        }
    }
}

void mysteryshipReset(enemy_t *mysteryship, unsigned int *direction){
    *direction = LEFT;
    mysteryship->dead = DEAD;
}


void updateMysteryshipPosition(enemy_t *mysteryship, unsigned int *direction){
    if (*direction == LEFT) {
        if (mysteryship->enemy->x2 >= 0) {
            setWallProperty(mysteryship->enemy, mysteryship->enemy->x1 - 2, 0, 0, 0, SET_WALL_X);
        }
        else{
            // set the state to dead when out of the screen
            mysteryship->dead = DEAD;
        }
    }
    else if (*direction == RIGHT) {
        if (mysteryship->enemy->x1 <= SCREEN_WIDTH) {
            setWallProperty(mysteryship->enemy, mysteryship->enemy->x1 + 2, 0, 0, 0, SET_WALL_X);
        }
        else{
            // set the state to dead when out of the screen
            mysteryship->dead = DEAD;
        }
    }
}


// TO-DO:
// need to add speed here in the function... how often the function gets called to increment the positions
// speed is a function of killed invaders... now I dont track killed invaders since the func is called in such a way
// that increments only alive invaders, so I dont need to keep count of how many were killed
void updateInvadersPosition(invaders_t *invaders, unsigned short *lives, unsigned char reset){
    static int row = 0;
    static int col = 0;
    static int change_direction = 0;    // 1 if edge is detected and invaders must change direction
    static int descend = 0;            // 1 if edge is detected and invaders must descend 1 row
    if(reset){
        row = 0;
        col = 0;
        change_direction = 0;
        descend = 0;
        return;
    }

    if (invaders->direction == RIGHT){
        while(row < ENEMY_ROWS){
            while (col < ENEMY_COLUMNS){
                // consideres only alive invaders
                if (!invaders->enemys[row][col].dead){
                    // if edge was detected move one row lower
                    if (descend){
                        setWallProperty(invaders->enemys[row][col].enemy, 0,
                                        invaders->enemys[row][col].enemy->y1 + invaders->enemys[row][col].enemy->h,
                                        0, 0, SET_WALL_Y);
                    }
                    // if the invader is still alive increment its position
                    setWallProperty(invaders->enemys[row][col].enemy,
                                    invaders->enemys[row][col].enemy->x1 + GAP_SIZE / 2,
                                    0, 0, 0, SET_WALL_X);
                    invaders->enemys[row][col].image_state = !invaders->enemys[row][col].image_state;
                    // edge of the screen detection for changing direction
                    if (invaders->enemys[row][col].enemy->x2 >= SCREEN_WIDTH - GAME_BORDER){
                        change_direction = 1;
                    }
                    // if an alive invader has reached the ground set lives to zero to trigger game over sequence
                    if (invaders->enemys[row][col].enemy->y2 >= GROUND_POSITION){
                        *lives = 0;
                    }

                    col++;
                    if(col == ENEMY_COLUMNS){
                        col = 0;
                        row++;
                    }
                    goto end_loop;
                }
                col++;
            }
            if(col == ENEMY_COLUMNS){
                col = 0;
                row++;
            }
        }
    }else if (invaders->direction == LEFT){
        while(row < ENEMY_ROWS){
            while (col < ENEMY_COLUMNS){
                // consideres only alive invaders
                if (!invaders->enemys[row][col].dead){
                    // if edge was detected move one row lower
                    if (descend){
                        setWallProperty(invaders->enemys[row][col].enemy, 0,
                                        invaders->enemys[row][col].enemy->y1 + invaders->enemys[row][col].enemy->h, 
                                        0, 0, SET_WALL_Y);
                    }
                    // if the invader is still alive increment its position
                    setWallProperty(invaders->enemys[row][col].enemy,
                                    invaders->enemys[row][col].enemy->x1 - GAP_SIZE / 2,
                                    0, 0, 0, SET_WALL_X);
                    invaders->enemys[row][col].image_state = !invaders->enemys[row][col].image_state;
                    // edge of the screen detection for changing direction
                    if (invaders->enemys[row][col].enemy->x1 <= 0 + GAME_BORDER){
                        change_direction = 1;
                    }
                    // if an alive invader has reached the ground set lives to zero to trigger game over sequence
                    if (invaders->enemys[row][col].enemy->y2 >= GROUND_POSITION){
                        *lives = 0;
                    }

                    col++;
                    if(col == ENEMY_COLUMNS){
                        col = 0;
                        row++;
                    }
                    goto end_loop;
                }
                col++;
            }
            if(col == ENEMY_COLUMNS){
                col = 0;
                row++;
            }
        }
    }
    // you need to update this... it doesnt make sense since the end loop gets executed only one time at the end
end_loop:
    if (row == ENEMY_ROWS || *lives == 0){
        row = 0;
        col = 0;
        descend = 0;
        if (change_direction){
            invaders->direction = !invaders->direction;
            change_direction = 0;
            descend = 1;
        }
    }
}

unsigned int invaderHitBunker(invaders_t *invaders){
    for (int row = 0; row < ENEMY_ROWS; row++){
        for (int col = 0; col < ENEMY_COLUMNS; col++){
            if (!invaders->enemys[row][col].dead){
                // checking if the bullet is coliding with a specific invader
                if (invaders->enemys[row][col].enemy->y2 > BUNKER_LOCATION_Y){
                    return 1;
                }
            }
        }
    }
    return 0; // no collisions detected    
}

// TO-DO: hit animation and sound
unsigned int bulletHitBunker(bunker_t *bunker, ball_t *bullet){
    // if bulet betweeen x1 and x2 and y1 and y2 destroy the bunker block 
    // bh - bunker height & bw - bunker width
    for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
        for (int bw = 0; bw < BUNKER_WIDTH; bw++){
            if (!bunker->bunker[bh][bw].dead){
                // checking if the bullet is coliding with a specific invader
                if (bullet->x >= bunker->bunker[bh][bw].bunker_block->x1 && 
                    bullet->x <= bunker->bunker[bh][bw].bunker_block->x2 &&
                    bullet->y >= bunker->bunker[bh][bw].bunker_block->y1 &&
                    bullet->y <= bunker->bunker[bh][bw].bunker_block->y2){
                    
                    bunker->bunker[bh][bw].dead = DEAD; // set the state to dead
                    return 1;
                }
            }
        }
    }
    return 0; // no collisions detected
}

// TO-DO: make hit detection universal with casting or accepting wall_t

unsigned int bulletHitMysteryship(enemy_t *mysteryship, ball_t *bullet, unsigned short *player_score){
    if (!mysteryship->dead){
        // checking if the bullet is coliding with a specific invader
        if (bullet->x >= mysteryship->enemy->x1 + 5 && 
            bullet->x <= mysteryship->enemy->x2 - 5 &&
            bullet->y >= mysteryship->enemy->y1 &&
            bullet->y <= mysteryship->enemy->y2 - 15){
            
            mysteryship->dead = DEAD; // set the state to dead
            //*player_score += *mysteryship->points;
            *player_score += abs(rand() % 301); // mystery ship is worth random amount of points (max 300)
            return 1;
        }
    }
    return 0; // no collisions detected
}

// TO-DO: hit animation and sound
unsigned int bulletHitInvader(invaders_t *invaders, ball_t *bullet, unsigned short *player_score){
    // if bulet betweeen x1 and x2 and y1 and y2 make the invader dead 
    // and increment score by points amount
    for (int row = 0; row < ENEMY_ROWS; row++){
        for (int col = 0; col < ENEMY_COLUMNS; col++){
            if (!invaders->enemys[row][col].dead){
                // checking if the bullet is coliding with a specific invader
                if (bullet->x >= invaders->enemys[row][col].enemy->x1 && 
                    bullet->x <= invaders->enemys[row][col].enemy->x2 &&
                    bullet->y >= invaders->enemys[row][col].enemy->y1 &&
                    bullet->y <= invaders->enemys[row][col].enemy->y2){
                    invaders->enemys[row][col].dead = DEAD; // set the state to dead
                    invaders->killed_invaders++;
                    *player_score += *invaders->enemys[row][col].points;

                    xQueueReset(killedInvaderQueue);
                    xQueueSend(killedInvaderQueue, &row, portMAX_DELAY);
                    xQueueSend(killedInvaderQueue, &col, portMAX_DELAY);

                    return 1;
                }
            }
        }
    }
    return 0; // no collisions detected
}

// TO-DO: hit animation and sound
unsigned int bulletHitPlayer(space_ship_t *player, ball_t *bullet){
    // if bulet betweeen x1 and x2 and y1 and y2 make the player lose a life
    // and reset player position
    // checking if the bullet is coliding with a player
    if (bullet->x >= player->ship->x1  && 
        bullet->x <= player->ship->x2  &&
        bullet->y >= player->ship->y1 + 15 &&   // this is just to lower the upper edge of the box 
                                                // so it is more accurate to the image, 
                                                // since it has a bit of a spike in the middle
        // this condition is adjusted so the invaders can not hit the player
        // when they are just one row above invasion (game over)
        bullet->y <= player->ship->y1 + 25){
        return 1;
    }
    return 0; // no collisions detected
}

void resetPlayerData (space_ship_t *player, unsigned short *resetScore){
    player->score = *resetScore;
    player->lives = 3;
}

void updateBulletPosition(bullet_t *bullet_obj, unsigned int time_since_last_update){
    // checking if the bullet is inside the "active" field where we are playing the game
    if (bullet_obj->bullet->y >= (0 + BULLET_RADIUS) && bullet_obj->bullet->y <= (GROUND_POSITION - BULLET_RADIUS)){
        updateBallPosition(bullet_obj->bullet, time_since_last_update);
    }else{
        setBallSpeed(bullet_obj->bullet, 0, 0, 0, SET_BALL_SPEED_Y);
        bullet_obj->bullet_state = PASSIVE;
    }
}

ball_t *shootBulletPlayer(bullet_t *bullet_obj, signed int ship_position){
    setBallLocation(bullet_obj->bullet, 
                    // ship x coordinate
                    ship_position,
                    // the middle of the ship (adding half the height so the bullet originates from the middle of the wall)
                    MY_SHIP_Y_POSITION + MY_SHIP_HEIGHT / 2);
    setBallSpeed(bullet_obj->bullet, 0, -BULLET_SPEED, BULLET_SPEED, SET_BALL_SPEED_Y);
    bullet_obj->bullet_state = ATTACKING;
    return bullet_obj->bullet;
}

ball_t *shootBulletInvaders(ball_t *bullet, invaders_t *invaders){
    unsigned short column_selection;
    wall_t *invader[ENEMY_COLUMNS] = { 0 };
    // finding the bottom most invaders
    for (int col = 0; col < ENEMY_COLUMNS; col++){
        for (int row = 0; row < ENEMY_ROWS; row++){
            // checking if the invader is alive
            if (!invaders->enemys[row][col].dead){
                invader[col] = invaders->enemys[row][col].enemy;
                break;
            }
        }
    }
    // randomly selecting the bottom most invader where the bullets will originate
    // we first check if there are any invaders in that column
    do {
        column_selection = rand() % (ENEMY_COLUMNS - 1);
    } while (!invader[column_selection]);

    // ******************************************************
    // *** WHILE LOOP BE CAREFILL WITH THE IMPLEMENTATION ***
    // ******************************************************
    
    // setting the starting location of the enemy bullet
    setBallLocation(bullet, 
            // middle of the selected invader
            invader[column_selection]->x1 + invader[column_selection]->w / 2,
            // the bottom of the selected invader
            invader[column_selection]->y2);

    setBallSpeed(bullet, 0, BULLET_SPEED / 3, BULLET_SPEED, SET_BALL_SPEED_Y);
    return bullet;
}



unsigned char xCheckPongUDPInput(unsigned short *paddle_y)
{
    static opponent_cmd_t current_key = NONE;

    if (NextKeyQueue) {
        xQueueReceive(NextKeyQueue, &current_key, 0);
    }

    if (current_key == INC) {
        // increment mothership
        // vDecrementPaddleY(paddle_y);
    }
    else if (current_key == DEC) {
        // decrement mothership
        // vIncrementPaddleY(paddle_y);
    }
    return 0;
}

void vMultiPlayerGame(void *pvParameters){ 
    
    //*********************************************************************************************************************
    char opponent_mode = 0; // 0: player 1: computer
    char difficulty = 1; // 0: easy 1: normal 2: hard
    opponent_cmd_t current_key = NONE;
    unsigned int binary_direction = 0;

    
    HandleUDP = xSemaphoreCreateMutex();
    if (!HandleUDP) {
        exit(EXIT_FAILURE);
    }
    NextKeyQueue = xQueueCreate(1, sizeof(opponent_cmd_t));
    if (!NextKeyQueue) {
        exit(EXIT_FAILURE);
    }
    PlayerPositionQueue = xQueueCreate(5, sizeof(unsigned long));
    if (!PlayerPositionQueue) {
        exit(EXIT_FAILURE);
    }
    MothershipPositionQueue = xQueueCreate(5, sizeof(unsigned long));
    if (!MothershipPositionQueue) {
        exit(EXIT_FAILURE);
    }
    BulletQueue = xQueueCreate(10, sizeof(unsigned char));
    if (!BulletQueue) {
        exit(EXIT_FAILURE);
    }
    DifficultyQueue = xQueueCreate(10, sizeof(unsigned char));
    if (!DifficultyQueue) {
        exit(EXIT_FAILURE);
    }
    BinaryStateQueue = xQueueCreate(10, sizeof(unsigned char));
    if (!BinaryStateQueue) {
        exit(EXIT_FAILURE);
    }
    
    //***********************************************************************************************************************

    TickType_t xLastWakeTime, prevWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    prevWakeTime = xLastWakeTime;
    const TickType_t updatePeriod = 5;

    // bullet debounce
    TickType_t prevBulletTime = xLastWakeTime;
    TickType_t bulletPeriodDelay = BULLET_START_DELAY; 
    // mysteryship debounce
    TickType_t prevMysteryTime = xLastWakeTime;
    unsigned int mysteryship_direction = LEFT;
    // player shooting debounce
    TickType_t prevShootTime = 0;
    const TickType_t shootDebounceDelay = 100;
    unsigned int prevButtonState = 0;

    // CHEATS VARIABLES
    unsigned short infinite_lives = 0;
    unsigned short resetScore = 0;
    unsigned short starting_level = 1;

    // LEVEL
    // TO-DO: add predefined constants here
    unsigned short current_level = 1;
    unsigned short initGame = 0;
    unsigned short init_row = 1;
    unsigned short init_col = 1;
    unsigned short invaderInitDelay = 0;

    // read HIGHSCORE from a file
    FILE *fp = NULL;
    unsigned short highscore;
    char* highscore_file = "../resources/highscore.txt";

    // loading sounds
    tumSoundLoadUserSample("../resources/mystery_ship.wav"); 
    tumSoundLoadUserSample("../resources/player_shoot.wav");
    tumSoundLoadUserSample("../resources/player_explosion.wav"); 
    tumSoundLoadUserSample("../resources/invader_explosion.wav"); 
    // buffer so the sound is not repeatedly played from beginning but plays only once it has finished playing
    unsigned short bufferSound = 0;
    // buffer for the explosion of the invader or the player so the game pauses / freezes for a moment when the explosion occours
    unsigned short bufferExplosion = 0;
    // buffer for the AI mothership to move into the game screen
    unsigned short bufferAI = 0;

    // flags if the player or any enemy have been killed
    unsigned short player_killed = 0;
    unsigned short invader_killed = 0;
    unsigned short mystery_killed = 0;
    // variables where the killed invader coordinates are received
    int killedInvaderRow;
    int killedInvaderCol;


    // load all the images
    image_handle_t player_explosion = tumDrawLoadImage("../resources/player_explosion.png");
    image_handle_t invader_explosion = tumDrawLoadImage("../resources/invader_explosion.png");
    unsigned int invader_explosion_width = tumDrawGetLoadedImageWidth(invader_explosion);

    image_handle_t myship = tumDrawLoadImage("../resources/myship_small.bmp");
    image_handle_t mystery_ship = tumDrawLoadImage("../resources/mothership.bmp");

    image_handle_t invader1_0 = tumDrawLoadImage("../resources/invader1_1.bmp");
    image_handle_t invader1_1 = tumDrawLoadImage("../resources/invader1_2.bmp");
    image_handle_t invader2_0 = tumDrawLoadImage("../resources/invader2_1.bmp");
    image_handle_t invader2_1 = tumDrawLoadImage("../resources/invader2_2.bmp");
    image_handle_t invader3_0 = tumDrawLoadImage("../resources/invader3_1.bmp");
    image_handle_t invader3_1 = tumDrawLoadImage("../resources/invader3_2.bmp");
    
    // TO-DO: should define this in constants if the images dont get loaded... or add if else statement
    unsigned int myship_height = tumDrawGetLoadedImageHeight(myship);
    unsigned int myship_width = tumDrawGetLoadedImageWidth(myship);

    unsigned int mystery_ship_height = tumDrawGetLoadedImageHeight(mystery_ship);
    unsigned int mystery_ship_width = tumDrawGetLoadedImageWidth(mystery_ship);

    unsigned int invaders_height = tumDrawGetLoadedImageHeight(invader1_0); // all invaders have the same height
    unsigned int invader1_width = tumDrawGetLoadedImageWidth(invader1_0);
    unsigned int invader2_width = tumDrawGetLoadedImageWidth(invader2_0);
    unsigned int invader3_width = tumDrawGetLoadedImageWidth(invader3_0);

    //unsigned int mystery_ship_points = 100;
    unsigned short invader1_points = 10;
    unsigned short invader2_points = 20;
    unsigned short invader3_points = 30;

    // initialising my bullet
    bullet_t my_bullet = { 0 };
    my_bullet.bullet = 
        createBall(BULLET_RADIUS,
                   BULLET_RADIUS,
                   White, BULLET_RADIUS, BULLET_SPEED, NULL, NULL, NULL);
    my_bullet.bullet_state = PASSIVE;
    //my_bullet.bullet_position = SCREEN_HEIGHT - 3 * tumDrawGetLoadedImageHeight(myship); // also ship position in y coordinates

    // initialising player
    space_ship_t my_ship = { 0 };
    my_ship.ship_position = SCREEN_WIDTH / 2; // this is the position of the middle of the ship
    my_ship.ship =
        createWall(SCREEN_WIDTH / 2 - myship_width / 2,
                   MY_SHIP_Y_POSITION, 
                   myship_width, 
                   myship_height, 
                   0, White, NULL, NULL);

    // initialising invaders bullets
    bullet_t invaders_bullets[MAX_ENEMY_BULLETS] = { 0 };
    for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
        invaders_bullets[b].bullet =
            createBall(BULLET_RADIUS,
                       BULLET_RADIUS,
                       White, BULLET_RADIUS, BULLET_SPEED, NULL, NULL, NULL);
        invaders_bullets[b].bullet_state = PASSIVE;
    }

    // initialising individual enemys
    enemy_t mysteryShip = { 0 };
    //mysteryShip.points = &mystery_ship_points;
    mysteryShip.height = &mystery_ship_height;
    mysteryShip.width = &mystery_ship_width;
    mysteryShip.enemy = createWall(SCREEN_WIDTH / 2 - mystery_ship_width / 2,
                                MYSTERY_SHIP_POSITION_Y, 
                                mystery_ship_width, 
                                mystery_ship_height, 
                                0, Red, NULL, NULL);

    enemy_t invader1 = { 0 };
    invader1.points = &invader1_points;
    invader1.height = &invaders_height;
    invader1.width = &invader1_width;
    enemy_t invader2 = { 0 };
    invader2.points = &invader2_points;
    invader2.height = &invaders_height;
    invader2.width = &invader2_width;
    enemy_t invader3 = { 0 };
    invader3.points = &invader3_points;
    invader3.height = &invaders_height;
    invader3.width = &invader3_width;


    // initialising all the invaders
    invaders_t invaders = { 0 };
    for (int row = 0; row < ENEMY_ROWS; row++){
        for (int col = 0; col < ENEMY_COLUMNS; col++){
            if (row == 0 || row == 1){
                invaders.enemys[row][col] = invader1;
                invaders.enemys[row][col].enemy =         
                    createWall(col * (invader1_width + GAP_SIZE),
                               SCREEN_HEIGHT / 2 - row * (2 * invaders_height), 
                               invader1_width, 
                               invaders_height, 
                               0, White, NULL, NULL);
            }else if (row == 2 || row == 3){
                invaders.enemys[row][col] = invader2;
                invaders.enemys[row][col].enemy =         
                    createWall(((invader1_width - invader2_width) / 2) + col * (invader1_width + GAP_SIZE),
                               SCREEN_HEIGHT / 2 - row * (2 * invaders_height), 
                               invader2_width, 
                               invaders_height, 
                               0, White, NULL, NULL);
            }else if (row == 4){
                invaders.enemys[row][col] = invader3;
                invaders.enemys[row][col].enemy =         
                    createWall(((invader1_width - invader3_width) / 2) + col * (invader1_width + GAP_SIZE),
                               SCREEN_HEIGHT / 2 - row * (2 * invaders_height), 
                               invader3_width, 
                               invaders_height, 
                               0, White, NULL, NULL);
            }
        }
    }

    // initialising all the bunkers
    bunker_t bunker[NUMBER_OF_BUNKERS] = { 0 };
    for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
        
        bunker[nob].bunker_x_location = (1 + nob) * DISTANCE_BETWEEN_BUNKERS + nob * BUNKER_WIDTH * BUNKER_BLOCK_LENGTH;
        bunker[nob].bunker_y_location = BUNKER_LOCATION_Y; 
        // bunker clearance redefined in the future with inclusion of bunker height and player height
        for (int bw = 0; bw < BUNKER_WIDTH; bw++){
            for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
                bunker[nob].bunker[bh][bw].bunker_block = 
                    createWall(bunker[nob].bunker_x_location + bw * BUNKER_BLOCK_LENGTH,
                               bunker[nob].bunker_y_location + bh * BUNKER_BLOCK_LENGTH, 
                               BUNKER_BLOCK_LENGTH, 
                               BUNKER_BLOCK_LENGTH, 
                               0, Green, NULL, NULL);
            }
        }
    }


    while(1){
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
                        if (MultiPlayerGame) {
                            vTaskSuspend(MultiPlayerGame);
                            vTaskSuspend(UDPControlTask);
                        }
                    }
                    else if (buttons.buttons[KEYCODE(R)]) {
                        xSemaphoreGive(buttons.lock);
                        // Receiving all the cheats to initialize the game accordingly
                        xQueueReceive(livesQueue, &infinite_lives, 0);
                        xQueueReceive(levelQueue, &starting_level, 0);
                        xQueueReceive(scoreQueue, &resetScore, 0);

                        // reset player score and lives
                        resetPlayerData(&my_ship, &resetScore);
                        // reset player position
                        my_ship.ship_position = SCREEN_WIDTH / 2;
                        setWallProperty(my_ship.ship, my_ship.ship_position - myship_width / 2, 0, 0, 0, SET_WALL_X);

                        // reset invaders position and data
                        invadersReset(&invaders, &invader1_width, &invaders_height);
                        updateInvadersPosition(NULL, NULL, RESET);
                        // reset mysteryship position
                        mysteryshipReset(&mysteryShip, &mysteryship_direction);
                        // reset all the bunkers
                        for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                            bunkersReset(&bunker[nob]);
                        }

                        // reset the position of all bullets
                        for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                            updateBulletPosition(&invaders_bullets[b], RESET_BULLET);
                        }
                        updateBulletPosition(&my_bullet, RESET_BULLET);
                        // reset the bullet debounce timers
                        bulletPeriodDelay = BULLET_START_DELAY;
                        prevBulletTime = xTaskGetTickCount();
                        prevMysteryTime = prevBulletTime;

                        current_level = starting_level;
                        // INITIALISING SEQUENCE
                        initGame = 1;

                        fp = fopen(highscore_file, "r");
                        if (fp != NULL){
                            highscore = getw(fp);
                        }else{
                            highscore = 0;
                        }
                        fclose(fp);
                        
                        player_killed = 0;
                        invader_killed = 0;
                        mystery_killed = 0;

                    }
                    else if (buttons.buttons[KEYCODE(M)]) {
                        xSemaphoreGive(buttons.lock);
                        opponent_mode = (opponent_mode + 1) % 2;
                        /*if (opponent_mode) {
                            
                            vTaskResume(UDPControlTask);
                        }
                        else {
                            
                            vTaskSuspend(UDPControlTask);
                        }*/
                        xQueueSend(BinaryStateQueue, (void *)&opponent_mode, portMAX_DELAY);
                        vTaskDelay(200);
                    }
                    else if (buttons.buttons[KEYCODE(L)] && opponent_mode) {
                        xSemaphoreGive(buttons.lock);
                        difficulty = (difficulty + 1) % 3;
                        xQueueSend(DifficultyQueue, (void *) &difficulty, portMAX_DELAY);
                        vTaskDelay(200);
                    }
                    else {
                        xSemaphoreGive(buttons.lock);
                    }
                }
                // if the game ends go to gave over screen or advance to new level
                if (!my_ship.lives || invaders.killed_invaders == (ENEMY_ROWS * ENEMY_COLUMNS) || initGame 
                    || player_killed || invader_killed || mystery_killed){
                    goto draw;
                }


                // checking if alive invaders are already at the bunkers 
                // if so the invaders destroy the bunkers
                if (invaderHitBunker(&invaders)){
                    //destroy all the bunkers
                    for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                        for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
                            for (int bw = 0; bw < BUNKER_WIDTH; bw++){
                                bunker[nob].bunker[bh][bw].dead = DEAD;
                            }
                        }
                    }
                }


                // checking players bullet collisions with invaders or bunkers
                // make enemy dead and increment score by points amount if colision is detected               
                
                // YOU HAVE TO CHANGE THIS!
                // checking for bunker colisions
                for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                    if (bulletHitBunker(&bunker[nob], my_bullet.bullet)){
                        updateBulletPosition(&my_bullet, RESET_BULLET);
                        if (opponent_mode){
                            xQueueSend(BulletQueue, &my_bullet.bullet_state, portMAX_DELAY);
                        }
                    }
                }
                if (bulletHitInvader(&invaders, my_bullet.bullet, &my_ship.score)){
                    invader_killed = 1;
                    tumSoundPlayUserSample("invader_explosion.wav");
                    updateBulletPosition(&my_bullet, RESET_BULLET);
                    if (opponent_mode){
                        xQueueSend(BulletQueue, &my_bullet.bullet_state, portMAX_DELAY);
                    }
                    // if all invaders are killed delete all enemy bullets and players bullet             
                    if (invaders.killed_invaders == (ENEMY_ROWS * ENEMY_COLUMNS)){
                        for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                            updateBulletPosition(&invaders_bullets[b], RESET_BULLET);
                        }
                        goto draw;
                    }
                }
                else if (bulletHitMysteryship(&mysteryShip, my_bullet.bullet, &my_ship.score)){
                    mystery_killed = 1;
                    tumSoundPlayUserSample("invader_explosion.wav");
                    updateBulletPosition(&my_bullet, RESET_BULLET);
                    if (opponent_mode){
                        xQueueSend(BulletQueue, &my_bullet.bullet_state, portMAX_DELAY);
                    }
                }
                else if (my_bullet.bullet_state == ATTACKING){
                    updateBulletPosition(&my_bullet, xLastWakeTime - prevWakeTime);
                } 



                // checking for bullet collisions of invader bullets with the bunker or player
                // checking for bunker collisions

                for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                    for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                        if (bulletHitBunker(&bunker[nob], invaders_bullets[b].bullet)){
                            updateBulletPosition(&invaders_bullets[b], RESET_BULLET);
                        }
                    }
                    // CONDIITON IF INFINITE LIVES CHEAT IS ACTIVATED


                    if(bulletHitPlayer(&my_ship, invaders_bullets[b].bullet)){
                            // reset all the bullets if colision with player is detected
                            if(!infinite_lives) {
                                for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                                    updateBulletPosition(&invaders_bullets[b], RESET_BULLET);
                                }
                                tumSoundPlayUserSample("player_explosion.wav");
                                player_killed = 1;
                                // decrement one life, reset position and make involnurable to bullets for a few moments
                                break;
                            }
                    }
                    else if (invaders_bullets[b].bullet_state == ATTACKING){
                        updateBulletPosition(&invaders_bullets[b], xLastWakeTime - prevWakeTime);
                    }
                }


                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (prevButtonState != buttons.buttons[KEYCODE(SPACE)]) {
                        if (prevButtonState < buttons.buttons[KEYCODE(SPACE)]){
                            if (xTaskGetTickCount() - prevShootTime > shootDebounceDelay){
                                if (my_bullet.bullet->dy == 0) {
                                    my_bullet.bullet = shootBulletPlayer(&my_bullet, my_ship.ship_position);
                                    prevShootTime = xTaskGetTickCount();
                                    tumSoundPlayUserSample("player_shoot.wav");
                                    if (opponent_mode){
                                        xQueueSend(BulletQueue, &my_bullet.bullet_state, portMAX_DELAY);
                                    }
                                }
                            }
                        }
                        prevButtonState = buttons.buttons[KEYCODE(SPACE)];
                    }
                    xSemaphoreGive(buttons.lock);
                }
            
                
                // shooting bullet every time period bulletPeriodDelay
                if (xTaskGetTickCount() - prevBulletTime > bulletPeriodDelay){
                    for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                        if (invaders_bullets[b].bullet->dy == 0){
                            invaders_bullets[b].bullet = shootBulletInvaders(invaders_bullets[b].bullet, &invaders);
                            invaders_bullets[b].bullet_state = ATTACKING;
                            //bullet is created with random time intervals that are smaller than BULLET_START_DELAY (in ms)
                            //this is like a debounce timer but with a random delay period
                            prevBulletTime = xTaskGetTickCount();
                            bulletPeriodDelay = (rand() % BULLET_START_DELAY);
                            
                            break;
                        }
                    }
                }


                // UPDATE PLAYER POSITION
                // checks player input to change its position accordingly
                xCheckPlayerInput(&my_ship.ship_position, myship_width);
                // updates the position of players spaceship

                // TO-DO: YOU NEED TO UPDATE THIS somehow put it in a function or something 
                setWallProperty(my_ship.ship, my_ship.ship_position - myship_width / 2, 0, 0, 0, SET_WALL_X);


                // ***************************************************
                // UPDATE INVADERS POSITION
                for(int i = 0; i < current_level; i++){
                    updateInvadersPosition(&invaders, &my_ship.lives, 0);
                }
                // ***************************************************





                // UPDATE MYSTERYSHIP POSITION
                // mysteryship flies by every 20 seconds alternating in direction it comes from
                if (mysteryShip.dead == ALIVE){
                    // human mode = 0, computer mode = 1
                    if (opponent_mode){
                        
                        if (bufferAI < 50){
                            updateMysteryshipPosition(&mysteryShip, &mysteryship_direction);
                            bufferAI++;
                        }else if (NextKeyQueue) {
                            xQueueReceive(NextKeyQueue, &current_key, 0);
                            if (current_key == INC){
                                binary_direction = RIGHT;
                                updateMysteryshipPosition(&mysteryShip, &binary_direction);
                            }else if (current_key == DEC){
                                binary_direction = LEFT;
                                updateMysteryshipPosition(&mysteryShip, &binary_direction);
                            }
                            /*
                            xQueueSend(MothershipPositionQueue, (void *)&mysteryShip.enemy->x1, 0);
                            xQueueSend(PlayerPositionQueue, (void *)&my_ship.ship->x1, 0);
                            prevMysteryTime = xTaskGetTickCount();
                            */
                        }
                        xQueueSend(MothershipPositionQueue, (void *)&mysteryShip.enemy->x1, 0);
                        xQueueSend(PlayerPositionQueue, (void *)&my_ship.ship->x1, 0);
                        prevMysteryTime = xTaskGetTickCount();

                    }else{
                        updateMysteryshipPosition(&mysteryShip, &mysteryship_direction);
                        prevMysteryTime = xTaskGetTickCount();
                        /*
                        xCheckPongUDPInput(&right_player.paddle_position);
                        unsigned long paddle_y = right_player.paddle_position *
                                                 PADDLE_INCREMENT_SIZE +
                                                 PADDLE_LENGTH / 2 +
                                                 WALL_OFFSET + WALL_THICKNESS;
                        xQueueSend(MothershipPositionQueue, (void *)&paddle_y, 0);
                        */
                    }
                }
                // if mysteryship is dead and enough time has passed create a new one
                else if (xTaskGetTickCount() - prevMysteryTime > MYSTERY_SHIP_PERIOD){
                    mysteryship_direction = !mysteryship_direction;
                    if (mysteryship_direction == RIGHT){
                        setWallProperty(mysteryShip.enemy, -mystery_ship_width, 0, 0, 0, SET_WALL_X);
                        mysteryShip.dead = ALIVE;
                        prevMysteryTime = xTaskGetTickCount();
                    }
                    else if (mysteryship_direction == LEFT){
                        setWallProperty(mysteryShip.enemy, SCREEN_WIDTH, 0, 0, 0, SET_WALL_X);
                        mysteryShip.dead = ALIVE;
                        prevMysteryTime = xTaskGetTickCount();
                    }
                    bufferAI = 0;
                }



                //PLUS ONE EXTRA LIFE EVERY 1000 SCORE / POINTS
                vExtraLives(&my_ship.lives, &my_ship.score);
               
                /*
                
                they need a value for speed (how often they increment / how short the delay of incrementing is)
                count the number of alive invaders to adjust the speed

                higher level -> higher starting speed
                when invaders go to a new row they also move horitontally
                they move down as the height of the image
                the incrementation pauses for a bit when the alien is hit

                at the beginning the images are loaded one by one from the bottom up (same as incrementation)

                */

                /*
                if you win the game or if you lose a game you need to send the score to the score task where it will be stored
                ---how do we create a file where our programm reads from... for the scores for the next time we start the app
                */        
                    



draw:
                taskENTER_CRITICAL();
                if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
                    
                    tumDrawClear(Black);

                    if (!my_ship.lives) {
                        // checks if the player achieved a highscore and updates it
                        if (my_ship.score > highscore){
                            highscore = my_ship.score;
                            // opens a highscore file and edits it
                            fp = fopen(highscore_file, "w");
                            if (fp != NULL){
                                putw(highscore, fp);
                            }
                            fclose(fp);
                        }

  

                        vDrawGameOver();
                    }else if (invaders.killed_invaders == (ENEMY_ROWS * ENEMY_COLUMNS)){
                        vDrawNextLevel();
                        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                            if (prevButtonState != buttons.buttons[KEYCODE(SPACE)]) {
                                if (prevButtonState < buttons.buttons[KEYCODE(SPACE)]){
                                    if (xTaskGetTickCount() - prevShootTime > shootDebounceDelay){
                                        // reset player position
                                        my_ship.ship_position = SCREEN_WIDTH / 2;
                                        my_ship.ship->x1 = my_ship.ship_position - myship_width / 2;
                                        my_ship.ship->x2 = my_ship.ship->x1 + myship_width;
                                        
                                        // reset invaders position and data
                                        invadersReset(&invaders, &invader1_width, &invaders_height);
                                        updateInvadersPosition(NULL, NULL, RESET);
                                        // reset mysteryship position
                                        mysteryshipReset(&mysteryShip, &mysteryship_direction);
                                        // reset all the bunkers
                                        for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                                            bunkersReset(&bunker[nob]);
                                        }

                                        // reset the position of all bullets
                                        for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                                            updateBulletPosition(&invaders_bullets[b], RESET_BULLET);
                                        }
                                        updateBulletPosition(&my_bullet, RESET_BULLET);
                                        // reset the bullet debounce timers
                                        bulletPeriodDelay = BULLET_START_DELAY;
                                        prevBulletTime = xTaskGetTickCount();
                                        prevMysteryTime = prevBulletTime;
                                        
                                        current_level++;
                                        // INITIALISING SEQUENCE
                                        initGame = 1;
                                        prevShootTime = xTaskGetTickCount();

                                        player_killed = 0;
                                        invader_killed = 0;
                                        mystery_killed = 0;
                                        
                                    }
                                }
                                prevButtonState = buttons.buttons[KEYCODE(SPACE)];
                            }
                            xSemaphoreGive(buttons.lock);
                        }
                    }else{
                        vDrawFPS();
                        vDrawHelpText(opponent_mode, difficulty);
                        vDrawScore(&my_ship.score, &highscore);
                        vDrawLevel(&current_level);
                        vDrawLives(&my_ship.lives, myship, &my_ship);
                        // draws the ground
                        tumDrawLine(0, GROUND_POSITION, SCREEN_WIDTH, GROUND_POSITION, 3, Green);
                        //vDrawHUD

                        // draw bullets
                        if (my_bullet.bullet_state == ATTACKING){
                            tumDrawCircle(my_bullet.bullet->x, my_bullet.bullet->y,
                                             my_bullet.bullet->radius, White);
                        }
                        for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                            if (invaders_bullets[b].bullet_state == ATTACKING){
                                tumDrawCircle(invaders_bullets[b].bullet->x, invaders_bullets[b].bullet->y,
                                              invaders_bullets[b].bullet->radius, Aqua);
                            }
                        }


                        


                        if (initGame){
                            if (invaderInitDelay == 0){
                                // draw invaders one by one

                                for (int row = 0; row < init_row; row++){
                                    if (row == (init_row - 1)){
                                        // draw invaders one by one in new row
                                        for (int col = 0; col < init_col; col++){
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            } 
                                        }

                                    }else{
                                        // draw all the invaders of the previous rows
                                        for (int col = 0; col < ENEMY_COLUMNS; col++){
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            } 
                                        }
                                    }
                                }
                                // after initializing process is done set initGame to zero
                                if (init_row == ENEMY_ROWS && init_col == ENEMY_COLUMNS){
                                    initGame = 0;
                                    init_row = 1;
                                    init_col = 1;
                                }
                                // after one row is drawn move to the next row of invaders
                                if (init_col == ENEMY_COLUMNS){
                                    init_row++;
                                    init_col = 1;
                                }else{
                                    init_col++;
                                }
                                invaderInitDelay++;
                            }
                            else if (invaderInitDelay < 8){

                                for (int row = 0; row < init_row; row++){
                                    if (row == (init_row - 1)){
                                        // draw invaders one by one in new row
                                        for (int col = 0; col < init_col; col++){
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            } 
                                        }

                                    }else{
                                        // draw all the invaders of the previous rows
                                        for (int col = 0; col < ENEMY_COLUMNS; col++){
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            } 
                                        }
                                    }
                                }
                                invaderInitDelay++;
                            }
                            else{
                                for (int row = 0; row < init_row; row++){
                                    if (row == (init_row - 1)){
                                        // draw invaders one by one in new row
                                        for (int col = 0; col < init_col; col++){
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            } 
                                        }

                                    }else{
                                        // draw all the invaders of the previous rows
                                        for (int col = 0; col < ENEMY_COLUMNS; col++){
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            } 
                                        }
                                    }
                                }
                                invaderInitDelay = 0;
                            }
                        }
                        else{                     
                            // draw invaders
                            for (int row = 0; row < ENEMY_ROWS; row++){
                                for (int col = 0; col < ENEMY_COLUMNS; col++){
                                    if (!invaders.enemys[row][col].dead){
                                        // alternating between images based if the X pixel coordinate is even or odd
                                        if (!invaders.enemys[row][col].image_state){
            
                                            /*tumDrawFilledBox(invaders.enemys[row][col].enemy->x1, 
                                                             invaders.enemys[row][col].enemy->y1, 
                                                             invaders.enemys[row][col].enemy->w, 
                                                             invaders.enemys[row][col].enemy->h, 
                                                             White);
                                            */
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_0,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }

                                        }else if (invaders.enemys[row][col].image_state){
                                            if (row == 0 || row == 1){
                                                tumDrawLoadedImage(invader1_1,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 2 || row == 3){
                                                tumDrawLoadedImage(invader2_1,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }else if (row == 4){
                                                tumDrawLoadedImage(invader3_1,
                                                                   invaders.enemys[row][col].enemy->x1, 
                                                                   invaders.enemys[row][col].enemy->y1);
                                            }
                                        }    
                                    }
                                }
                            }

                            if (invader_killed){
                                xQueueReceive(killedInvaderQueue, &killedInvaderRow, 0);
                                xQueueReceive(killedInvaderQueue, &killedInvaderCol, 0);
                                if (killedInvaderRow == 0 || killedInvaderRow == 1){
                                    tumDrawLoadedImage(invader_explosion,
                                                       invaders.enemys[killedInvaderRow][killedInvaderCol].enemy->x1
                                                       - (invader_explosion_width - invader1_width) / 2, 
                                                       invaders.enemys[killedInvaderRow][killedInvaderCol].enemy->y1);
                                }else if (killedInvaderRow == 2 || killedInvaderRow == 3){
                                    tumDrawLoadedImage(invader_explosion,
                                                       invaders.enemys[killedInvaderRow][killedInvaderCol].enemy->x1
                                                       - (invader_explosion_width - invader2_width) / 2, 
                                                       invaders.enemys[killedInvaderRow][killedInvaderCol].enemy->y1);
                                }else if (killedInvaderRow == 4){
                                    tumDrawLoadedImage(invader_explosion,
                                                       invaders.enemys[killedInvaderRow][killedInvaderCol].enemy->x1 
                                                       - (invader_explosion_width - invader3_width) / 2,
                                                       invaders.enemys[killedInvaderRow][killedInvaderCol].enemy->y1);
                                }

                                if (bufferExplosion == EXPLOSION_DELAY){
                                    invader_killed = 0;
                                    bufferExplosion = 0;
                                }
                                else{
                                    bufferExplosion++;
                                }

                            }

                            // draw an explosion of a recently killed invader
                        }

                        // draw my spaceship and the ground
                        if (player_killed){
                                tumDrawLoadedImage(player_explosion,
                                                   my_ship.ship->x1,
                                                   my_ship.ship->y1);

                                if (bufferExplosion == EXPLOSION_DELAY){
                                    my_ship.ship_position = SCREEN_WIDTH / 2;
                                    my_ship.lives--;
                                    player_killed = 0;
                                    bufferExplosion = 0;
                                }else{
                                    bufferExplosion++;
                                }

                        }
                        else {
                            if (myship) {
                                tumDrawLoadedImage(myship,
                                                   my_ship.ship->x1,
                                                   my_ship.ship->y1);
                            
                            }else{
                                tumDrawFilledBox(my_ship.ship->x1, my_ship.ship->y1, 
                                                 my_ship.ship->w, my_ship.ship->h, Green);

                                tumDrawLine(0, GROUND_POSITION, SCREEN_WIDTH, GROUND_POSITION, 3, Green);
                            }
                        }

                        // draw bunkers
                        for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                            for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
                                for (int bw = 0; bw < BUNKER_WIDTH; bw++){
                                    if (!bunker[nob].bunker[bh][bw].dead){
                                        tumDrawFilledBox(bunker[nob].bunker[bh][bw].bunker_block->x1, 
                                                         bunker[nob].bunker[bh][bw].bunker_block->y1, 
                                                         bunker[nob].bunker[bh][bw].bunker_block->w, 
                                                         bunker[nob].bunker[bh][bw].bunker_block->h, 
                                                         Green);
                                                     
                                    }
                                }
                            }
                        }

                        // draw mystery ship
                        if(!mysteryShip.dead){
                            if (mystery_ship) {
                                tumDrawLoadedImage(mystery_ship,
                                                   mysteryShip.enemy->x1,
                                                   mysteryShip.enemy->y1);
                            }else{
                                tumDrawFilledBox(mysteryShip.enemy->x1, mysteryShip.enemy->y1, 
                                                 mysteryShip.enemy->w, mysteryShip.enemy->h, Red);
                            }
                            if (bufferSound == 13){
                                tumSoundPlayUserSample("mystery_ship.wav");
                                bufferSound = 0;
                            }else{
                                bufferSound++;
                            }
                        }
                        else if (mystery_killed){
                            tumDrawLoadedImage(invader_explosion,
                                               mysteryShip.enemy->x1 + (mystery_ship_width - invader_explosion_width) / 2,
                                               mysteryShip.enemy->y1);

                            if (bufferExplosion == EXPLOSION_DELAY){
                                mystery_killed = 0;
                                bufferExplosion = 0;
                            }else{
                                bufferExplosion++;
                            }
                        }
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

    killedInvaderQueue = xQueueCreate(2, sizeof(int));
    if (!killedInvaderQueue){
        PRINT_ERROR("Could not open killedInvaderQueue");
        goto err_killedinvaderqueue;
    }

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
    if (xTaskCreate(vUDPControlTask, "UDPControlTask",
                    mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY,
                    &UDPControlTask) != pdPASS) {
        PRINT_TASK_ERROR("UDPControlTask");
        goto err_udpcontrol;
    }
    

    vTaskSuspend(LeftPaddleTask);
    vTaskSuspend(RightPaddleTask);
    vTaskSuspend(PongControlTask);
    vTaskSuspend(MultiPlayerGame);
    vTaskSuspend(UDPControlTask);
    
    return 0;

err_udpcontrol:
    vTaskDelete(MultiPlayerGame);
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
    vQueueDelete(killedInvaderQueue);
err_killedinvaderqueue:
    return -1;
}
