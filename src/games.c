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
#define SMALLER_HITBOX 5
#define BUTTON_DEBOUNCE 200

// PLAYER DIMENSIONS
#define MY_SHIP_WIDTH 52
#define MY_SHIP_HEIGHT 30
#define MY_SHIP_SPEED 2
#define MAX_PLAYER_LIVES 3
#define EXTRA_LIFE_SCORE_THRESHOLD 1000

// adjusted for invaders just before inasion so they are just above ground
#define GROUND_POSITION (SCREEN_HEIGHT - 2 * MY_SHIP_HEIGHT - 10)
#define MY_SHIP_Y_POSITION (GROUND_POSITION - MY_SHIP_HEIGHT - 3)

// BULLET PROPERTIES
#define PASSIVE 0
#define ATTACKING 1
#define DESTROY_BULLET 1
#define BULLET_RADIUS 2
#define BULLET_SPEED 1700
#define RESET_BULLET 9999

// INVADERS PROPERTIES
#define ENEMY_ROWS 5
#define ENEMY_COLUMNS 11
#define RIGHT 0
#define LEFT 1
#define ALIVE 0
#define DEAD 1
#define GAP_SIZE 15 // gap between the invaders
#define MAX_ENEMY_BULLETS 3
#define BULLET_MAX_DELAY 2000 // bullet is shot after 2 seconds since the beginning of the game

#define INVADER1_POINTS 10
#define INVADER2_POINTS 20
#define INVADER3_POINTS 30

// BUNKER PROPERTIES
#define NUMBER_OF_BUNKERS 4 // units are in blocks (each bunker has BUNKER_WIDTH * BUNKER_HEIGHT blocks)
#define BUNKER_WIDTH 8 
#define BUNKER_HEIGHT 6 
#define BUNKER_BLOCK_LENGTH 13 // unit in pixels  // individual bunker block is a square
#define DISTANCE_BETWEEN_BUNKERS (SCREEN_WIDTH - NUMBER_OF_BUNKERS * BUNKER_WIDTH * BUNKER_BLOCK_LENGTH) / (NUMBER_OF_BUNKERS + 1)
#define BUNKER_LOCATION_Y GROUND_POSITION - (BUNKER_HEIGHT * BUNKER_BLOCK_LENGTH) - 3 * MY_SHIP_HEIGHT

// MOTHERHSIP
#define MYSTERY_SHIP_POSITION_Y 138
#define MYSTERY_SHIP_SPEED 1
#define MOTHERSHIP_PERIOD 20000    // mothership passes by every 20 seconds since it last appeared
#define MOTHERSHIP_MAX_POINTS 300

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
TaskHandle_t MothershipTask = NULL;
TaskHandle_t PlayerTask = NULL;

SemaphoreHandle_t ScreenLock = NULL;
SemaphoreHandle_t DrawSignal = NULL;

static QueueHandle_t LeftScoreQueue = NULL;
static QueueHandle_t RightScoreQueue = NULL;
static QueueHandle_t StartDirectionQueue = NULL;
static QueueHandle_t BallYQueue = NULL;
static QueueHandle_t PaddleYQueue = NULL;

static QueueHandle_t KilledInvaderQueue = NULL;

static QueueHandle_t KilledMothershipQueue = NULL;
static QueueHandle_t ResetMothershipQueue = NULL;
static QueueHandle_t OpponentModeQueueM = NULL;

static QueueHandle_t KilledPlayerQueue = NULL;
static QueueHandle_t ResetPlayerQueue = NULL;
static QueueHandle_t OpponentModeQueueP = NULL;
static QueueHandle_t HighscoreQueue = NULL;
static QueueHandle_t PlayerLivesQueue = NULL;

static SemaphoreHandle_t ResetRightPaddle = NULL;
static SemaphoreHandle_t ResetLeftPaddle = NULL;
static SemaphoreHandle_t BallInactive = NULL;

// AI *******************************************

TaskHandle_t UDPControlTask = NULL;

static QueueHandle_t PlayerPositionQueue = NULL;
static QueueHandle_t MothershipPositionQueue = NULL;
static QueueHandle_t BulletQueue = NULL;
static QueueHandle_t DifficultyQueue = NULL;
QueueHandle_t BinaryStateQueue = NULL;
QueueHandle_t NextKeyQueue = NULL;

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
    
    char last_binary_state = OFF;
    char binary_state = ON;
    
    char last_bullet_state = -8;
    char bullet_state = PASSIVE;

    char last_difficulty = -8;
    char current_difficulty = 1;


    // initialising structures used by the UDPControlTask
    // NextKeyQueue and BinaryStateQueue are initialised earlyer (with all the other queues)
    HandleUDP = xSemaphoreCreateMutex();
    if (!HandleUDP) {
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
        // state of the binary file
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
        sprintf(str, "AI Mode");
    }
    else {
        sprintf(str, "Normal Mode");
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
    SemaphoreHandle_t lock;
} space_ship_t;

typedef struct bullet_data {
    ball_t *bullet;
    //will have to implement this later in the bullet shotting functions for the mothership
    char bullet_state; // ATTACKING (on the screeen) or PASSIVE (not on she screen)
} bullet_t;

typedef struct invader_unit_data {
    wall_t *enemy;
    unsigned short *points; // how much is the enemy worth
    unsigned short dead; // alive or dead so we know if we need to draw it and detect collisions
    unsigned short image_state; // alternating between images
    //every enemy has different width and height
    unsigned int *width;
    unsigned int *height;
    SemaphoreHandle_t lock; // will only be initialized for the mothership
} enemy_t;

typedef struct invader_group_data {
    enemy_t enemys[ENEMY_ROWS][ENEMY_COLUMNS];
    unsigned short direction;
    unsigned int killed_invaders;
} invaders_t;

typedef struct bunker_block {
    wall_t *bunker_block;
    unsigned short block_row;
    unsigned short block_column;
    unsigned short dead; // alive or dead so whe know if we need to draw it and detect collisions
} block_t;

typedef struct bunker_data {
    block_t bunker[BUNKER_HEIGHT][BUNKER_WIDTH];
    // every bunker has different location, since there are 4 bunkers
    // point in the top left corner of the bunker is determined by the following point
    unsigned int bunker_y_location; 
    unsigned int bunker_x_location;
} bunker_t;

space_ship_t player = { 0 };
enemy_t MotherShip = { 0 };

void vDrawNextLevel(void){
    static char str[100] = { 0 };
    static int text_width;
    static int text_height;
    static unsigned short blinky = 0;

    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)30);

    sprintf(str, "YOU ADVANCED TO THE NEXT LEVEL!");

    if (!tumGetTextSize((char *)str, &text_width, &text_height))
        tumDrawText(str,
                    SCREEN_WIDTH / 2 - text_width / 2,
                    SCREEN_HEIGHT / 2 - text_height / 2, White);

    // a counter that makes the text blink 
    if(blinky <= 50){
        tumFontSetSize((ssize_t)15);

        sprintf(str, "PRESS SPACE TO CONTINUE");

        if (!tumGetTextSize((char *)str, &text_width, NULL))
            tumDrawText(str,
                        SCREEN_WIDTH / 2 - text_width / 2,
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
    static char str[100] = { 0 };
    static int text_width;
    static int text_height;
    static unsigned short blinky = 0;

    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)40);

    sprintf(str, "GAME OVER");

    if (!tumGetTextSize((char *)str, &text_width, &text_height))
        tumDrawText(str,
                    SCREEN_WIDTH / 2 - text_width / 2,
                    SCREEN_HEIGHT / 2 - text_height / 2, White);
    
    // a counter that makes the text blink 
    if(blinky <= 50){
        tumFontSetSize((ssize_t)15);

        sprintf(str, "PRESS R TO RESTART");

        if (!tumGetTextSize((char *)str, &text_width, NULL))
            tumDrawText(str,
                        SCREEN_WIDTH / 2 - text_width / 2,
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
    static char str[100] = { 0 };
    ssize_t prev_font_size = tumFontGetCurFontSize();

    tumFontSetSize((ssize_t)20);

    sprintf(str, "Current score: %hu", *score);

    if (!tumGetTextSize((char *)str, NULL, NULL))
        tumDrawText(str,
                    DEFAULT_FONT_SIZE * 1.5,
                    DEFAULT_FONT_SIZE * 1.5, White);

    sprintf(str, "Highscore: %hu", *highscore);

    if (!tumGetTextSize((char *)str, NULL, NULL))
        tumDrawText(str,
                    8 * DEFAULT_FONT_SIZE * 1.5,
                    DEFAULT_FONT_SIZE * 1.5, White);

    tumFontSetSize(prev_font_size);
}

void vDrawLives (unsigned short *lives, image_handle_t avatar){
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

    // draw player ships next to displayed lives
    for (int i = 1; i < *lives; i++) {
        if (avatar) {
            tumDrawLoadedImage(avatar,
                           GAP_SIZE + i * (GAP_SIZE / 2 + tumDrawGetLoadedImageWidth(avatar)),
                           SCREEN_HEIGHT - text_height);
        }
    }
}

void vExtraLives(unsigned short *lives, unsigned short *score, unsigned short *reset_score){
    static unsigned short extra_lives= 0;
    if (*score == *reset_score){
        extra_lives = 0;
    }

    if (floor((*score) / EXTRA_LIFE_SCORE_THRESHOLD) - extra_lives){
        extra_lives++;
        // add lives only if the player has less than maximum posssible lives
        if (*lives < MAX_PLAYER_LIVES){
            *lives += 1;
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


void resetBunkers (bunker_t *bunker){
    for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
        for (int bw = 0; bw < BUNKER_WIDTH; bw++){
            if (bunker->bunker[bh][bw].dead == DEAD){
                bunker->bunker[bh][bw].dead = ALIVE; // set the state to alive  
            }
        }
    }
}

void resetInvaders (invaders_t *invaders, unsigned int *invader_width, unsigned int *invader_height){
    for (int row = 0; row < ENEMY_ROWS; row++){
        for (int col = 0; col < ENEMY_COLUMNS; col++){
            invaders->killed_invaders = 0;
            invaders->direction = RIGHT;
            invaders->enemys[row][col].dead = ALIVE;
            invaders->enemys[row][col].image_state = 0; // resets the image state to initial image

            if (row == 0 || row == 1){
                setWallProperty(invaders->enemys[row][col].enemy,
                            col * (*invader_width + GAP_SIZE),
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

void resetMothership(enemy_t *mysteryship, unsigned int *direction){
    *direction = LEFT;
    mysteryship->dead = DEAD;
}

void resetPlayerData (space_ship_t *player, unsigned short *reset_score){
    player->score = *reset_score;
    player->lives = MAX_PLAYER_LIVES;
}

void updateMothershipPosition(enemy_t *mothership, unsigned int *direction){
    //static char pause_binary = OFF;
    if (*direction == LEFT) {
        if (mothership->enemy->x2 >= 0) {
            setWallProperty(mothership->enemy, mothership->enemy->x1 - 2, 0, 0, 0, SET_WALL_X);
        }
        else{
            // set the state to dead when out of the screen
            mothership->dead = DEAD;
            //xQueueSend(BinaryStateQueue, (void *)&pause_binary, portMAX_DELAY);
        }
    }
    else if (*direction == RIGHT) {
        if (mothership->enemy->x1 <= SCREEN_WIDTH) {
            setWallProperty(mothership->enemy, mothership->enemy->x1 + 2, 0, 0, 0, SET_WALL_X);
        }
        else{
            // set the state to dead when out of the screen
            mothership->dead = DEAD;
            //xQueueSend(BinaryStateQueue, (void *)&pause_binary, portMAX_DELAY);
        }
    }
}

void updateInvadersPosition(invaders_t *invaders, unsigned char reset){
    static int row = 0;
    static int col = 0;
    static int change_direction = 0;    // 1 if edge is detected and invaders must change direction
    static int descend = 0;             // 1 if edge is detected and invaders must descend 1 row
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
                    // if edge was detected also move one row lower
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

end_loop:
    // when we iterate through all of the invaders reset the variables and reset the processs
    if (row == ENEMY_ROWS){
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

unsigned int invadersReachedBunkers(invaders_t *invaders){
    for (int row = 0; row < ENEMY_ROWS; row++){
        for (int col = 0; col < ENEMY_COLUMNS; col++){
            if (!invaders->enemys[row][col].dead){
                if (invaders->enemys[row][col].enemy->y2 > BUNKER_LOCATION_Y){
                    return 1;
                }
            }
        }
    }
    return 0;   // invaders haven't reached the bunkers yet
}

unsigned int bulletHitBunker(bunker_t *bunker, bullet_t *bullet_obj){
    // if bulet betweeen x1 and x2 and y1 and y2 destroy the bunker block 
    // bh - bunker height & bw - bunker width
    for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
        for (int bw = 0; bw < BUNKER_WIDTH; bw++){
            if (!bunker->bunker[bh][bw].dead){
                // checking if the bullet is coliding with a specific bunker block
                if (bullet_obj->bullet->x >= bunker->bunker[bh][bw].bunker_block->x1 && 
                    bullet_obj->bullet->x <= bunker->bunker[bh][bw].bunker_block->x2 &&
                    bullet_obj->bullet->y >= bunker->bunker[bh][bw].bunker_block->y1 &&
                    bullet_obj->bullet->y <= bunker->bunker[bh][bw].bunker_block->y2){
                    bullet_obj->bullet_state = PASSIVE;
                    bunker->bunker[bh][bw].dead = DEAD; // set the state to dead
                    return 1;
                }
            }
        }
    }
    return 0; // no collisions detected
}

// TO-DO: make hit detection universal with casting or accepting wall_t
unsigned int bulletHitMothership(enemy_t *mothership, bullet_t *bullet_obj){
    if (!mothership->dead){
        if (bullet_obj->bullet->x >= mothership->enemy->x1 + SMALLER_HITBOX && 
            bullet_obj->bullet->x <= mothership->enemy->x2 - SMALLER_HITBOX &&
            bullet_obj->bullet->y >= mothership->enemy->y1 &&
            bullet_obj->bullet->y <= mothership->enemy->y2 - 3 * SMALLER_HITBOX){
            
            // bullet hit the mothership and it is no longer active
            bullet_obj->bullet_state = PASSIVE;
            // mothership is now dead and won't be displayed anymore
            mothership->dead = DEAD;
            xQueueSend(KilledMothershipQueue, &mothership->dead, portMAX_DELAY);
            // mothership is worth random amount of points up to MOTHERSHIP_MAX_POINTS

            return 1;
        }
    }
    return 0; // no collisions detected
}

unsigned int bulletHitInvader(invaders_t *invaders, bullet_t *bullet_obj){
    // if bulet betweeen x1 and x2 and y1 and y2 make the invader dead (if bullet is inside the invader) 
    for (int row = 0; row < ENEMY_ROWS; row++){
        for (int col = 0; col < ENEMY_COLUMNS; col++){
            if (!invaders->enemys[row][col].dead){
                // checking if the bullet is coliding with a specific invader
                if (bullet_obj->bullet->x >= invaders->enemys[row][col].enemy->x1 && 
                    bullet_obj->bullet->x <= invaders->enemys[row][col].enemy->x2 &&
                    bullet_obj->bullet->y >= invaders->enemys[row][col].enemy->y1 &&
                    bullet_obj->bullet->y <= invaders->enemys[row][col].enemy->y2){
                    
                    // bullet hit the invader and it is no longer active
                    bullet_obj->bullet_state = PASSIVE;
                    // this invader is now dead and will not be displayed anymore
                    invaders->enemys[row][col].dead = DEAD;
                    // one more killed invader
                    invaders->killed_invaders++;
                    // send the killed invader row and column information
                    // so the explosion animation can be drawn in the right location
                    xQueueReset(KilledInvaderQueue);
                    xQueueSend(KilledInvaderQueue, &row, portMAX_DELAY);
                    xQueueSend(KilledInvaderQueue, &col, portMAX_DELAY);
                    return 1;
                }
            }
        }
    }
    return 0; // no collisions detected
}

// TO-DO: put more things here like decreasing lives etc.
unsigned int bulletHitPlayer(space_ship_t *player, bullet_t *bullet_obj){
    // checking if the bullet is coliding with a player
    if (bullet_obj->bullet->x >= player->ship->x1  && 
        bullet_obj->bullet->x <= player->ship->x2  &&
        // this is just to lower the upper edge of the box so it is more accurate to the image, 
        // since it has a bit of a spike in the middle and this increases the height of the image
        bullet_obj->bullet->y >= player->ship->y1 + 3 * SMALLER_HITBOX &&   
        
        // this condition is adjusted so the invaders can not hit the player
        // when they are just one row above invasion (just before game over)
        bullet_obj->bullet->y <= player->ship->y1 + 5 * SMALLER_HITBOX){

        bullet_obj->bullet_state = PASSIVE;

        return 1;
    }
    return 0; // no collisions detected
}

ball_t *shootBulletPlayer(bullet_t *bullet_obj, signed int ship_position){
    setBallLocation(bullet_obj->bullet, 
                    // ship x coordinate (the middle of the ship)
                    ship_position,
                    // ship y coordinare (adding half the height so the bullet originates from the middle)
                    MY_SHIP_Y_POSITION + MY_SHIP_HEIGHT / 2);
    setBallSpeed(bullet_obj->bullet, 0, -BULLET_SPEED, BULLET_SPEED, SET_BALL_SPEED_Y);
    bullet_obj->bullet_state = ATTACKING;
    return bullet_obj->bullet;
}

ball_t *shootBulletInvader(bullet_t *bullet_obj, invaders_t *invaders){
    unsigned short column_selection;
    // this will be an array of bottommost invaders where the bullets will originate from
    wall_t *invader[ENEMY_COLUMNS] = { 0 };
    // finding the bottommost invaders
    for (int col = 0; col < ENEMY_COLUMNS; col++){
        for (int row = 0; row < ENEMY_ROWS; row++){
            // checking if the invader is alive
            if (!invaders->enemys[row][col].dead){
                invader[col] = invaders->enemys[row][col].enemy;
                // when an alive invader in a column was found, move to the next column
                break;
            }
        }
    }
    // randomly selecting the bottom most invader where the bullets will originate
    do {
    // ******************************************************
    // *** WHILE LOOP BE CAREFILL WITH THE IMPLEMENTATION ***
    // ******************************************************
        column_selection = abs(rand() % (ENEMY_COLUMNS - 1));
    } while (!invader[column_selection]);
    // we repeatedly check if there are any invaders in that column
    // since it is possible that no invaders are in that column
    // and repeat this process until we find an invader

    
    // setting the starting location of the enemy bullet
    setBallLocation(bullet_obj->bullet, 
            // middle of the selected invader
            invader[column_selection]->x1 + invader[column_selection]->w / 2,
            // the bottom of the selected invader
            invader[column_selection]->y2);

    setBallSpeed(bullet_obj->bullet, 0, BULLET_SPEED / 3, BULLET_SPEED, SET_BALL_SPEED_Y);
    bullet_obj->bullet_state = ATTACKING;
    return bullet_obj->bullet;
}

void updateBulletPosition(bullet_t *bullet_obj, unsigned int time_since_last_update){
    // checking if the bullet is inside the "active" field where we are playing the game
    if (bullet_obj->bullet->y >= (0 + BULLET_RADIUS) && bullet_obj->bullet->y <= (GROUND_POSITION - BULLET_RADIUS)
        && bullet_obj->bullet_state == ATTACKING){
        updateBallPosition(bullet_obj->bullet, time_since_last_update);
    }else{
        bullet_obj->bullet_state = PASSIVE;
    }
}

void vMultiPlayerGame(void *pvParameters){ 
    
    // task timers
    TickType_t xLastWakeTime, prevWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    prevWakeTime = xLastWakeTime;
    const TickType_t updatePeriod = 5;

    // AI VARIABLES
    const char turn_off = OFF;
    char opponent_mode = 0; // 0: player 1: computer
    char difficulty = 1; // 0: easy 1: normal 2: hard

    // DEBOUNCE TIMERS
    // bullet debounce
    TickType_t prevInvaderShootTime = 0;
    TickType_t bulletPeriodDelay = BULLET_MAX_DELAY; 
    // player shooting debounce
    TickType_t prevButtonTime = 0;
    const TickType_t buttonDebounceDelay = BUTTON_DEBOUNCE;
    unsigned int prevButtonState = 0;

    // CHEATS VARIABLES
    unsigned short infinite_lives = 0;
    unsigned short starting_level = 1;
    unsigned short reset_score = 0;

    // LEVEL
    // TO-DO: add predefined constants here
    unsigned char current_lives = 0;
    unsigned short current_level = 1;
    unsigned short init_game = 0;
    unsigned short init_invader = 1;
    unsigned short invader_init_delay = 0;

    // HIGHSCORE file location
    FILE *fp = NULL;
    unsigned short highscore;
    //unsigned short highscoreAI;
    char* highscore_file = "../resources/highscore.txt";
    char* highscoreAI_file = "../resources/highscoreAI.txt";
    // BUFFERS for delays
    // buffer for the explosion of the invader or the player so the game pauses / freezes for a moment when the explosion occours
    unsigned short bufferExplosion = 0;
    // BUFFER QUEUE is an empty variable to sent through queues to signal events
    unsigned char bufferQueue = 0;

    // flags if the player or any enemy have been killed to start explosion animation
    // unsigned short player_killed = 0;
    unsigned short invader_killed = 0;

    // variables where the killed invader coordinates are received from a queue
    int killedInvaderRow;
    int killedInvaderCol;

    unsigned short invader1_points = INVADER1_POINTS;
    unsigned short invader2_points = INVADER2_POINTS;
    unsigned short invader3_points = INVADER3_POINTS;

    // ************************** LOADING SOUNDS **************************
    tumSoundLoadUserSample("../resources/player_shoot.wav");
    tumSoundLoadUserSample("../resources/invader_explosion.wav"); 

    // ************************** LOADING IMAGES **************************
    image_handle_t invader_explosion = tumDrawLoadImage("../resources/invader_explosion.png");

    image_handle_t invader1_0 = tumDrawLoadImage("../resources/invader1_1.bmp");
    image_handle_t invader1_1 = tumDrawLoadImage("../resources/invader1_2.bmp");
    image_handle_t invader2_0 = tumDrawLoadImage("../resources/invader2_1.bmp");
    image_handle_t invader2_1 = tumDrawLoadImage("../resources/invader2_2.bmp");
    image_handle_t invader3_0 = tumDrawLoadImage("../resources/invader3_1.bmp");
    image_handle_t invader3_1 = tumDrawLoadImage("../resources/invader3_2.bmp");

    // ************************** DIMENSIONS ASSOCIATED WITH IMAGES **************************
    unsigned int invader_explosion_width = tumDrawGetLoadedImageWidth(invader_explosion);

    // all invaders have the same height
    unsigned int invaders_height = tumDrawGetLoadedImageHeight(invader1_0); 
    // but different widths
    unsigned int invader1_width = tumDrawGetLoadedImageWidth(invader1_0);
    unsigned int invader2_width = tumDrawGetLoadedImageWidth(invader2_0);
    unsigned int invader3_width = tumDrawGetLoadedImageWidth(invader3_0);


// ************************** INITIALISING OBJECTS **************************
    // initialising player bullet
    bullet_t my_bullet = { 0 };
    my_bullet.bullet = 
        createBall(BULLET_RADIUS,
                   BULLET_RADIUS,
                   White, BULLET_RADIUS, BULLET_SPEED, NULL, NULL, NULL);

    // initialising individual enemys

    // 3 different types of invaders
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

    // initialising the invaders group
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
    
    // initialising invaders bullets
    bullet_t invaders_bullets[MAX_ENEMY_BULLETS] = { 0 };
    for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
        invaders_bullets[b].bullet =
            createBall(BULLET_RADIUS,
                       BULLET_RADIUS,
                       White, BULLET_RADIUS, BULLET_SPEED, NULL, NULL, NULL);
    }

    // initialising all the bunkers
    bunker_t bunker[NUMBER_OF_BUNKERS] = { 0 };
    for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
        // setting up the location of individual bunkers
        bunker[nob].bunker_x_location = (1 + nob) * DISTANCE_BETWEEN_BUNKERS + nob * BUNKER_WIDTH * BUNKER_BLOCK_LENGTH;
        bunker[nob].bunker_y_location = BUNKER_LOCATION_Y; 
        // setting up the individual blocks of the bunkers
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
// **************************************************************************************************
    while(1){
        if (DrawSignal) {
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update global button data

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    // whenever this task is endered from the main menu, the game will restart
                    xQueueReceive(restartGameQueue, &buttons.buttons[KEYCODE(R)], 0);
                    xSemaphoreGive(buttons.lock);
                }

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(P)]) {
                        xSemaphoreGive(buttons.lock);
                        xQueueSend(BinaryStateQueue, (void *)&turn_off, portMAX_DELAY);
                        xQueueSend(gameModeQueue, &opponent_mode, portMAX_DELAY);
                        if (PausedStateTask) {
                            vTaskResume(PausedStateTask);
                        }
                        if (MothershipTask){
                            vTaskSuspend(MothershipTask);
                        }
                        if (PlayerTask){
                            vTaskSuspend(PlayerTask);
                        }
                        if (MultiPlayerGame) {
                            vTaskSuspend(MultiPlayerGame);
                        }

                    }
                    else if (buttons.buttons[KEYCODE(R)]) {
                        xSemaphoreGive(buttons.lock);
                        // Receiving all the cheats to initialize the game accordingly

                        xQueueReceive(livesQueue, &infinite_lives, 0);
                        xQueueReceive(levelQueue, &starting_level, 0);
                        while(xQueueReceive(scoreQueue, &reset_score, 0) == pdTRUE){    
                        }
                        xQueueOverwrite(scoreQueue, &reset_score);
                        xQueueReceive(gameModeQueue, &opponent_mode, 0);

                        current_level = starting_level;
                        current_lives = MAX_PLAYER_LIVES;                  
                        // RESETING PLAYER
                        
                        xQueueOverwrite(ResetPlayerQueue, &bufferQueue);

                        // RESETING INVADERS
                        
                        resetInvaders(&invaders, &invader1_width, &invaders_height);
                        updateInvadersPosition(NULL, RESET);
                        invader_killed = 0;
                        init_invader = 1;
                        invader_init_delay = 0;
                        // RESETING MOTHERSHIP
                        xQueueOverwrite(ResetMothershipQueue, &bufferQueue);
                        

                        // RESETING BUNKERS
                        for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                            resetBunkers(&bunker[nob]);
                        }
                        
                        // RESETING BULLETS
                        bulletPeriodDelay = BULLET_MAX_DELAY;
                        prevInvaderShootTime = xTaskGetTickCount();
                        prevButtonTime = prevInvaderShootTime;
                        my_bullet.bullet_state = PASSIVE;

                        for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                            invaders_bullets[b].bullet_state = PASSIVE;
                        }

                        // INITIALISING SEQUENCE (drawing invaders one by one)
                        init_game = 1;
                        
                        // HIGHSCORE RESET  (TO-DO do I even need this?)
                        if (opponent_mode){
                            fp = fopen(highscoreAI_file, "r");
                            if (fp != NULL){
                                highscore = getw(fp);
                            }else{
                                highscore = 0;
                            }
                            fclose(fp);
                            //xQueueSend(HighscoreQueue, &highscoreAI, portMAX_DELAY);
                        }else{
                            fp = fopen(highscore_file, "r");
                            if (fp != NULL){
                                highscore = getw(fp);
                            }else{
                                highscore = 0;
                            }
                            fclose(fp);

                        }
                        xQueueOverwrite(HighscoreQueue, &highscore);
                        if (MothershipTask){
                            vTaskResume(MothershipTask);
                        }
                        if (PlayerTask){
                            vTaskResume(PlayerTask);
                        }    

                    }
                    // change the level of difficulty of the AI mode
                    else if (buttons.buttons[KEYCODE(L)] && opponent_mode) {
                        xSemaphoreGive(buttons.lock);
                        if (xTaskGetTickCount() - prevButtonTime > buttonDebounceDelay){
                            difficulty = (difficulty + 1) % 3;
                            xQueueSend(DifficultyQueue, (void *) &difficulty, portMAX_DELAY);
                            prevButtonTime = xTaskGetTickCount();   
                        }
                    }
                    else {
                        xSemaphoreGive(buttons.lock);
                    }
                }
                
                xQueueReceive(PlayerLivesQueue, &current_lives, 0);
                // if the game ends go to gave over screen or advance to new level
                if (!current_lives || invaders.killed_invaders == (ENEMY_ROWS * ENEMY_COLUMNS) 
                    || init_game || invader_killed){
                    // if anyone was killed go directly to the drawing sequence and do not calculate / update any data
                    // could do this with multiple tasks with locking or notifyGive etc. TO-DO
                    goto draw;
                    
                }

                
                // CHECKING IF INVADERS HAVE REACHED THE BUNKERS
                if (invadersReachedBunkers(&invaders)){
                    //destroy all the bunkers
                    for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                        for (int bh = 0; bh < BUNKER_HEIGHT; bh++){
                            for (int bw = 0; bw < BUNKER_WIDTH; bw++){
                                bunker[nob].bunker[bh][bw].dead = DEAD;
                            }
                        }
                    }
                }
                // CHECKING IF INVADERS HAVE REACHED THE GROUND (INVASION)
                for (int row = 0; row < ENEMY_ROWS; row++){
                    for (int col = 0; col < ENEMY_COLUMNS; col++){
                        if (invaders.enemys[row][col].enemy->y2 >= GROUND_POSITION){
                            if (xSemaphoreTake(player.lock, portMAX_DELAY) == pdTRUE){
                                player.lives = 0;
                                current_lives = 0;
                                xSemaphoreGive(player.lock);
                            }
                        }
                    }
                }

                // ************************** CHECKING BULLET COLLISIONS ***************************
                // ************************* AND UPDATING BULLET POSITIONS ************************* 
                
                // CHECKING PLAYER'S BULLET 

                if (my_bullet.bullet_state == ATTACKING){
                    if (xSemaphoreTake(MotherShip.lock, 0) == pdTRUE){
                        if (bulletHitMothership(&MotherShip, &my_bullet)){
                            xSemaphoreGive(MotherShip.lock);
                            //tumSoundPlayUserSample("invader_explosion.wav");
                            if (xSemaphoreTake(player.lock, portMAX_DELAY) == pdTRUE) {
                                player.score += abs(rand() % (MOTHERSHIP_MAX_POINTS + 1));
                                xSemaphoreGive(player.lock);
                            }

                            if (opponent_mode){
                                xQueueSend(BulletQueue, (void *)&my_bullet.bullet_state, portMAX_DELAY);
                            }
                        }
                    }
                    // checking for bunker collisions
                    for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                        if (bulletHitBunker(&bunker[nob], &my_bullet)){
                            if (opponent_mode){
                                xQueueSend(BulletQueue, (void *)&my_bullet.bullet_state, portMAX_DELAY);
                            }
                        }
                    }
                    // checking for invader collisions
                    if (bulletHitInvader(&invaders, &my_bullet)){

                        // turn on the flag for the animation
                        invader_killed = 1; // could do this qith a queue or sth better
                        tumSoundPlayUserSample("invader_explosion.wav");
                        if (opponent_mode){
                            xQueueSend(BulletQueue, (void *)&my_bullet.bullet_state, portMAX_DELAY);
                        }
                        // if all invaders are killed delete all enemy bullets and players bullet             
                        if (invaders.killed_invaders == (ENEMY_ROWS * ENEMY_COLUMNS)){
                            for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                                invaders_bullets[b].bullet_state = PASSIVE;
                            }
                            goto draw;
                        }
                    }
                    // if no colisions are detected just increment the bullet's position
                    else{
                        updateBulletPosition(&my_bullet, xLastWakeTime - prevWakeTime);
                        if (opponent_mode && my_bullet.bullet_state == PASSIVE){
                            // send information about bullet only if it changes to passive
                            xQueueSend(BulletQueue, (void *)&my_bullet.bullet_state, portMAX_DELAY);
                        }
                    }
                }



                // CHECKING INVADER'S BULLETS
                
                for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                    if (invaders_bullets[b].bullet_state == ATTACKING){
                        
                        // checking for bunker collisions
                        for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                            if (bulletHitBunker(&bunker[nob], &invaders_bullets[b])){
                                break;
                            }
                        }
                        // checking for player collisions
                        
                        if (xSemaphoreTake(player.lock, portMAX_DELAY) == pdTRUE){
                            if(bulletHitPlayer(&player, &invaders_bullets[b])){
                                
                                // reset all the bullets if collision with player is detected
                                // CONDIITON IF INFINITE LIVES CHEAT IS ACTIVATED

                                if(!infinite_lives) {
                                    player.lives--;
                                    xQueueSend(PlayerLivesQueue, &player.lives, portMAX_DELAY);
                                    xQueueSend(KilledPlayerQueue, &player.lives, portMAX_DELAY);
                                    bulletPeriodDelay = BULLET_MAX_DELAY;
                                    prevInvaderShootTime = xTaskGetTickCount();
                                    my_bullet.bullet_state = PASSIVE;
                                    xSemaphoreGive(player.lock);
                                    for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                                        invaders_bullets[b].bullet_state = PASSIVE;
                                    }
                                    break;
                                }
                            }
                            xSemaphoreGive(player.lock);

                            
                        }
                        updateBulletPosition(&invaders_bullets[b], xLastWakeTime - prevWakeTime);
                    }
                }

                // ************************* SHOOTING BULLETS *************************

                // SHOOTING PLAYER'S BULLET
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (prevButtonState != buttons.buttons[KEYCODE(SPACE)]) {
                        if (prevButtonState < buttons.buttons[KEYCODE(SPACE)]){
                            if (xTaskGetTickCount() - prevButtonTime > buttonDebounceDelay){
                                if (my_bullet.bullet_state == PASSIVE && !invader_killed) {
                                    if (xSemaphoreTake(player.lock, portMAX_DELAY) == pdTRUE){
                                        my_bullet.bullet = shootBulletPlayer(&my_bullet, player.ship_position);
                                        xSemaphoreGive(player.lock);
                                        prevButtonTime = xTaskGetTickCount();
                                        tumSoundPlayUserSample("player_shoot.wav");
                                        if (opponent_mode){
                                            xQueueSend(BulletQueue, (void *)&my_bullet.bullet_state, portMAX_DELAY);
                                        }
                                    }

                                }
                            }
                        }
                        prevButtonState = buttons.buttons[KEYCODE(SPACE)];
                    }
                    xSemaphoreGive(buttons.lock);
                }
            
                // SHOOTING INVADER'S BULLETS
                // shooting bullet every time period bulletPeriodDelay that is randomized
                if (xTaskGetTickCount() - prevInvaderShootTime > bulletPeriodDelay){
                    for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                        if (invaders_bullets[b].bullet_state == PASSIVE){
                            invaders_bullets[b].bullet = shootBulletInvader(&invaders_bullets[b], &invaders);
                            prevInvaderShootTime = xTaskGetTickCount();
                            bulletPeriodDelay = (rand() % BULLET_MAX_DELAY);
                            //bullet is created with random time intervals that are smaller than BULLET_MAX_DELAY
                            //this is like a debounce timer but with a random delay period
                            break;
                        }
                    }
                }

                // ***************************** UPDATING POSITIONS OF *****************************
                // ************************* PLAYER, INVADERS & MOTHERSHIP *************************

                // UPDATE INVADERS POSITION
                for(int i = 0; i < current_level; i++){
                    // higher the level more invaders we increment resulting in faster invaders movement
                    updateInvadersPosition(&invaders, 0);
                }
               
                // ***************************** DRAWING EVERYTHING *****************************
draw:
                taskENTER_CRITICAL();
                if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
                    
                    tumDrawClear(Black);
                    // WHAT TO DRAW IF WE LOST ALL OUR LIVES (or if invaders reached the bottom)
                    if (!current_lives) {
                        vTaskSuspend(PlayerTask);
                        vTaskSuspend(MothershipTask);
                        // checks if the player achieved a highscore and updates it
                        if (xSemaphoreTake(player.lock, portMAX_DELAY) == pdTRUE){
                            if (player.score > highscore){
                                highscore = player.score;
                                xSemaphoreGive(player.lock);
                                // opens a highscore file and edits it
                                if (opponent_mode){
                                    fp = fopen(highscoreAI_file, "w");
                                    if (fp != NULL){
                                        putw(highscore, fp);
                                    }
                                    fclose(fp);
                                }else{
                                    fp = fopen(highscore_file, "w");
                                    if (fp != NULL){
                                        putw(highscore, fp);
                                    }
                                    fclose(fp);
                                }
                            }
                            xSemaphoreGive(player.lock);
                        }
                        vDrawGameOver();
                    // WHAT TO DRAW IF WE KILLED ALL THE INVADERS & THEIR ANIMATIONS ENDED
                    }else if (invaders.killed_invaders == (ENEMY_ROWS * ENEMY_COLUMNS) && !invader_killed){
                        vTaskSuspend(MothershipTask);
                        vTaskSuspend(PlayerTask);
                        vDrawNextLevel();
                        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                            if (prevButtonState != buttons.buttons[KEYCODE(SPACE)]) {
                                if (prevButtonState < buttons.buttons[KEYCODE(SPACE)]){
                                    if (xTaskGetTickCount() - prevButtonTime > buttonDebounceDelay){
                                        // RESETING PLAYER
                                        xQueueOverwrite(ResetPlayerQueue, &bufferQueue);

                                        // RESETING INVADERS
                                        resetInvaders(&invaders, &invader1_width, &invaders_height);
                                        updateInvadersPosition(NULL, RESET);
                                        invader_killed = 0;
                                        
                                        // RESETING MOTHERSHIP
                                        xQueueOverwrite(ResetMothershipQueue, &bufferQueue);

                                        // RESETING BUNKERS
                                        for (int nob = 0; nob < NUMBER_OF_BUNKERS; nob++){
                                            resetBunkers(&bunker[nob]);
                                        }

                                        // RESETING BULLETS
                                        bulletPeriodDelay = BULLET_MAX_DELAY;
                                        prevInvaderShootTime = xTaskGetTickCount();
                                        prevButtonTime = prevInvaderShootTime;
                                        my_bullet.bullet_state = PASSIVE;
                                        for (int b = 0; b < MAX_ENEMY_BULLETS; b++){
                                            invaders_bullets[b].bullet_state = PASSIVE;
                                        }

                                        // INITIALISING SEQUENCE (drawing invaders one by one)
                                        init_game = 1;
                                        init_invader = 1;
                                        invader_init_delay = 0;
                                        current_level++;
                                        if (MothershipTask){
                                            vTaskResume(MothershipTask);
                                        }
                                        if (PlayerTask){
                                            vTaskResume(PlayerTask);
                                        }                                      
                                    }
                                }
                                prevButtonState = buttons.buttons[KEYCODE(SPACE)];
                            }
                            xSemaphoreGive(buttons.lock);
                        }
                    }else{
                        vDrawFPS();
                        vDrawHelpText(opponent_mode, difficulty);
                        vDrawLevel(&current_level);
                        
                        // draws the ground
                        tumDrawLine(0, GROUND_POSITION, SCREEN_WIDTH, GROUND_POSITION, 3, Green);

                        // ***************************** DRAWING BULLETS *****************************

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

                        // ***************************** DRAWING INVADERS *****************************
                        if (init_game){
                            // initializing invaders one by one with a slight delay
                            for (int inv = 0; inv < init_invader; inv++){
                                if (inv < 2 * ENEMY_COLUMNS){
                                    tumDrawLoadedImage(invader1_0,
                                                       invaders.enemys[0][inv].enemy->x1, 
                                                       invaders.enemys[0][inv].enemy->y1);
                                }else if (inv >= 2 * ENEMY_COLUMNS 
                                        && inv < 4 * ENEMY_COLUMNS){
                                    tumDrawLoadedImage(invader2_0,
                                                       invaders.enemys[0][inv].enemy->x1, 
                                                       invaders.enemys[0][inv].enemy->y1);
                                }else if (inv >= 4 * ENEMY_COLUMNS){
                                    tumDrawLoadedImage(invader3_0,
                                                       invaders.enemys[0][inv].enemy->x1, 
                                                       invaders.enemys[0][inv].enemy->y1);
                                }

                            }
                            if (init_invader == (ENEMY_COLUMNS * ENEMY_ROWS) && invader_init_delay == 8){
                                init_invader = 1;
                                init_game = 0;
                                invader_init_delay = 0;
                                xSemaphoreGive(MotherShip.lock);
                                xSemaphoreGive(player.lock);
                            }else if(invader_init_delay == 8){
                                init_invader++;
                                invader_init_delay = 0;
                            }else{
                                invader_init_delay++;
                            }
                        }
                        else{                     
                            // draw invaders
                            for (int row = 0; row < ENEMY_ROWS; row++){
                                for (int col = 0; col < ENEMY_COLUMNS; col++){
                                    if (!invaders.enemys[row][col].dead){
                                        // alternating between images
                                        if (!invaders.enemys[row][col].image_state){
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
                        }

                        // draw an explosion of a recently killed invader
                        if (invader_killed){
                            xQueueReceive(KilledInvaderQueue, &killedInvaderRow, 0);
                            xQueueReceive(KilledInvaderQueue, &killedInvaderCol, 0);
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

                            // counter for how log the explosion is displayed
                            if (bufferExplosion == EXPLOSION_DELAY){
                                invader_killed = 0;
                                bufferExplosion = 0;
                                if (xSemaphoreTake(player.lock, portMAX_DELAY) == pdTRUE) {
                                    player.score += *invaders.enemys[killedInvaderRow][killedInvaderCol].points;
                                    xSemaphoreGive(player.lock);
                                }
                            }
                            else{
                                bufferExplosion++;
                            }
                        }
                        // ***************************** DRAWING BUNKERS *****************************
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

void vPlayerTask(void *pvParameters){
    
    unsigned short updatePeriod = 10;

    unsigned short player_killed = 0;
    unsigned short reset_score = 0;
    unsigned short highscore = 0;
    char opponent_mode = 0; // 0: player 1: computer

    // buffer for the explosion of the invader or the player so the game pauses / freezes for a moment when the explosion occours
    unsigned short bufferExplosion = 0;
    // LOADING RESCOURCES
    tumSoundLoadUserSample("../resources/player_explosion.wav");
    tumSoundLoadUserSample("../resources/player_shoot.wav");
    image_handle_t player_explosion = tumDrawLoadImage("../resources/player_explosion.png");
    image_handle_t myship = tumDrawLoadImage("../resources/myship_small.bmp");
    // TO-DO: should define this in constants if the images dont get loaded... or add if else statement
    
    unsigned int myship_height = tumDrawGetLoadedImageHeight(myship);
    unsigned int myship_width = tumDrawGetLoadedImageWidth(myship);
    // initialising player
    player.lock = xSemaphoreCreateMutex();
    if (!player.lock) {
        exit(EXIT_FAILURE);
    }
    player.ship_position = SCREEN_WIDTH / 2; // this is the position of the middle of the ship
    player.ship =
        createWall(SCREEN_WIDTH / 2 - myship_width / 2,
                   MY_SHIP_Y_POSITION, 
                   myship_width, 
                   myship_height, 
                   0, White, NULL, NULL);

    // should use binary semaphore here for player and for the mothership
    KilledPlayerQueue = xQueueCreate(1, sizeof(unsigned char));
    if (!KilledPlayerQueue) {
        exit(EXIT_FAILURE);
    }
    // should use binary semaphore here for player and for the mothership
    ResetPlayerQueue = xQueueCreate(1, sizeof(unsigned char));
    if (!ResetPlayerQueue) {
        exit(EXIT_FAILURE);
    }
    OpponentModeQueueP = xQueueCreate(1, sizeof(unsigned char));
    if (!OpponentModeQueueP) {
        exit(EXIT_FAILURE);
    }
    HighscoreQueue = xQueueCreate(1, sizeof(unsigned short));
    if (!HighscoreQueue) {
        exit(EXIT_FAILURE);
    }
    PlayerLivesQueue = xQueueCreate(1, sizeof(unsigned short));
    if (!PlayerLivesQueue) {
        exit(EXIT_FAILURE);
    }

                        
    while(1){
        if (xSemaphoreTake(player.lock, portMAX_DELAY) == pdTRUE) {
            if (xQueueReceive(ResetPlayerQueue, &player_killed, 0) == pdTRUE){

                xQueueReceive(HighscoreQueue, &highscore, 0);
                player.ship_position = SCREEN_WIDTH / 2;
                setWallProperty(player.ship, player.ship_position - myship_width / 2, 0, 0, 0, SET_WALL_X);
                player_killed = 0;
            }

            if (xQueueReceive(scoreQueue, &reset_score, 0) == pdTRUE){    // YOU DONT WANT THIS WHEN YOU GO TO NEXT LEVEL
                resetPlayerData(&player, &reset_score);
            }

            if (xQueueReceive(KilledPlayerQueue, &player_killed, 0) == pdTRUE){
                tumSoundPlayUserSample("player_explosion.wav"); 
            }

            if(xQueueReceive(OpponentModeQueueP, &opponent_mode, 0) == pdTRUE){
                vTaskDelay(200); // this delay is here because in the main control task there is also a delay every time we change opponent mode
            }
            // ********************** UPDATING POSITION OF MOTHERSHIP **********************
            if (!player_killed) {
                xCheckPlayerInput(&player.ship_position, myship_width);
                setWallProperty(player.ship, player.ship_position - myship_width / 2, 0, 0, 0, SET_WALL_X);
                if (opponent_mode){
                    unsigned short player_pos = player.ship->x1;
                    xQueueSend(PlayerPositionQueue, &player_pos, 0);
                }
            }
            // TO-DO: somehow put it in a function or something 
            // UPDATES LIVES if player has enough score
            vExtraLives(&player.lives, &player.score, &reset_score);

            // ***************************** DRAWING PLAYER *****************************
            taskENTER_CRITICAL();
            if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
                vDrawLives(&player.lives, myship);
                vDrawScore(&player.score, &highscore);
                if (player_killed){
                    tumDrawLoadedImage(player_explosion,
                                       player.ship->x1,
                                       player.ship->y1);
                    // counter for how log the explosion is displayed
                    if (bufferExplosion == EXPLOSION_DELAY){
                        player.ship_position = SCREEN_WIDTH / 2;
                        player_killed = 0;
                        bufferExplosion = 0;
                    }else{
                        bufferExplosion++;
                    }
                }
                else if (myship) {
                    tumDrawLoadedImage(myship,
                                    player.ship->x1,
                                    player.ship->y1);
                }
            }
            xSemaphoreGive(ScreenLock);
            taskEXIT_CRITICAL();
            xSemaphoreGive(player.lock);
        }
        vTaskDelay(updatePeriod);
        xSemaphoreGive(player.lock);
    }
}

void vMothershipTask(void *pvParameters){ 

    unsigned short updatePeriod = 10;

    char opponent_mode = 0; // 0: player 1: computer
    opponent_cmd_t current_key = NONE;

    // buffer so the sound is not repeatedly played from beginning but plays only once it has finished playing
    unsigned short bufferSound = 0;
    // buffer for the explosion of the invader or the player so the game pauses / freezes for a moment when the explosion occours
    unsigned short bufferExplosion = 0;
    // buffer for the AI mothership to move into the game screen
    unsigned short bufferAI = 0;
    // mysteryship debounce
    TickType_t prevMothershipTime = xTaskGetTickCount();
    unsigned int mothership_direction = LEFT;
    unsigned short mothership_killed = 0;
    
    // LOADING RESOURCES
    tumSoundLoadUserSample("../resources/mothership.wav"); 
    image_handle_t invader_explosion = tumDrawLoadImage("../resources/invader_explosion.png");
    image_handle_t mothership = tumDrawLoadImage("../resources/mothership.bmp");

    unsigned int invader_explosion_width = tumDrawGetLoadedImageWidth(invader_explosion);
    unsigned int mothership_height = tumDrawGetLoadedImageHeight(mothership);
    unsigned int mothership_width = tumDrawGetLoadedImageWidth(mothership);
    
    MotherShip.lock = xSemaphoreCreateMutex();
    if (!MotherShip.lock) {
        exit(EXIT_FAILURE);
    }
    MotherShip.dead = DEAD;
    MotherShip.height = &mothership_height;
    MotherShip.width = &mothership_width;
    MotherShip.enemy = createWall(- mothership_width,
                            MYSTERY_SHIP_POSITION_Y, 
                            mothership_width, 
                            mothership_height, 
                            0, Red, NULL, NULL);

    KilledMothershipQueue = xQueueCreate(1, sizeof(unsigned char));
    if (!KilledMothershipQueue) {
        exit(EXIT_FAILURE);
    }
    ResetMothershipQueue = xQueueCreate(1, sizeof(unsigned char));
    if (!ResetMothershipQueue) {
        exit(EXIT_FAILURE);
    }
    OpponentModeQueueM = xQueueCreate(1, sizeof(unsigned char));
    if (!OpponentModeQueueM) {
        exit(EXIT_FAILURE);
    }

    while(1){
        if (xSemaphoreTake(MotherShip.lock, portMAX_DELAY) == pdTRUE) {
            if (xQueueReceive(ResetMothershipQueue, &mothership_killed, 0) == pdTRUE){
                resetMothership(&MotherShip, &mothership_direction);
                prevMothershipTime = xTaskGetTickCount();
                mothership_killed = 0;
            }
            if (xQueueReceive(KilledMothershipQueue, &mothership_killed, 0) == pdTRUE){
                tumSoundPlayUserSample("invader_explosion.wav"); 
            }

            if(xQueueReceive(OpponentModeQueueM, &opponent_mode, 0) == pdTRUE){
                vTaskDelay(200); // this delay is here because in the main control task there is also a delay every time we change opponent mode
            }
            
            // ********************** UPDATING POSITION OF MOTHERSHIP **********************
            if (MotherShip.dead == ALIVE){
                // human mode = 0, computer mode = 1
                // can change the task maybe for AI separate task than for the normal mode
                if (opponent_mode){
                    // bufferAI loop moves the mothership into the game screen
                    if (bufferAI < 50){
                        updateMothershipPosition(&MotherShip, &mothership_direction);
                        bufferAI++;
                    }else if (NextKeyQueue) {
                        xQueueReceive(NextKeyQueue, &current_key, 0);
                        if (current_key == INC){
                            mothership_direction = RIGHT;
                            updateMothershipPosition(&MotherShip, &mothership_direction);
                        }else if (current_key == DEC){
                            mothership_direction = LEFT;
                            updateMothershipPosition(&MotherShip, &mothership_direction);
                        }
                        unsigned int mothership_pos = MotherShip.enemy->x1;
                        xQueueSend(MothershipPositionQueue, (void *)&mothership_pos, 0);
                    }
                }else{
                    updateMothershipPosition(&MotherShip, &mothership_direction);
                }
            }
            // if mysteryship is dead and enough time has passed create a new one
            else if (xTaskGetTickCount() - prevMothershipTime > MOTHERSHIP_PERIOD){
                // starts from different edge of the screen every time
                mothership_direction = !mothership_direction; 
                if (mothership_direction == RIGHT){
                    setWallProperty(MotherShip.enemy, -mothership_width, 0, 0, 0, SET_WALL_X);
                    MotherShip.dead = ALIVE;
                    prevMothershipTime = xTaskGetTickCount();
                }
                else if (mothership_direction == LEFT){
                    setWallProperty(MotherShip.enemy, SCREEN_WIDTH, 0, 0, 0, SET_WALL_X);
                    MotherShip.dead = ALIVE;
                    prevMothershipTime = xTaskGetTickCount();
                }
                bufferAI = 0;
                // reset the counter when a new mothership is created 
                // so it can be moved into the screen again
            }

            // ***************************** DRAWING MOTHERSHIP *****************************
            taskENTER_CRITICAL();
            if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
                
                if(!MotherShip.dead){
                    if (mothership) {
                        tumDrawLoadedImage(mothership,
                                           MotherShip.enemy->x1,
                                           MotherShip.enemy->y1);
                    }
                    //TO-DO numbers!!
                    if (bufferSound == 13){
                        tumSoundPlayUserSample("mothership.wav");
                        bufferSound = 0;
                        prevMothershipTime = xTaskGetTickCount();
                    }else{
                        bufferSound++;
                    }
                }
                else if (mothership_killed){
                    tumDrawLoadedImage(invader_explosion,
                                       MotherShip.enemy->x1 + (mothership_width - invader_explosion_width) / 2,
                                       MotherShip.enemy->y1);
                    // counter for how log the explosion is displayed
                    if (bufferExplosion == EXPLOSION_DELAY){
                        mothership_killed = 0;
                        bufferExplosion = 0;
                    }else{
                        bufferExplosion++;
                    }
                }
            }
            xSemaphoreGive(ScreenLock);
            taskEXIT_CRITICAL();
            xSemaphoreGive(MotherShip.lock);
        }
        vTaskDelay(updatePeriod);
        xSemaphoreGive(MotherShip.lock);
    }
}



int gamesInit(void)
{
    //Random numbers
    srand(time(NULL));

    BinaryStateQueue = xQueueCreate(10, sizeof(unsigned char));
    if (!BinaryStateQueue) {
        PRINT_ERROR("Could not open BinaryStateQueue");
        goto err_binarystatequeue;
    }
    NextKeyQueue = xQueueCreate(1, sizeof(opponent_cmd_t));
    if (!NextKeyQueue) {
        PRINT_ERROR("Could not open NextKeyQueue");
        goto err_nextkeyqueue;
    }
    KilledInvaderQueue = xQueueCreate(2, sizeof(int));
    if (!KilledInvaderQueue){
        PRINT_ERROR("Could not open KilledInvaderQueue");
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
                    configMAX_PRIORITIES, NULL, mainGENERIC_PRIORITY,
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
    if (xTaskCreate(vMothershipTask, "MothershipTask",
                    configMAX_PRIORITIES - 1, NULL, mainGENERIC_PRIORITY,
                    &MothershipTask) != pdPASS) {
        PRINT_TASK_ERROR("MothershipTask");
        goto err_mothershiptask;
    }
    if (xTaskCreate(vPlayerTask, "PlayerTask",
                    configMAX_PRIORITIES - 1, NULL, mainGENERIC_PRIORITY,
                    &PlayerTask) != pdPASS) {
        PRINT_TASK_ERROR("PlayerTask");
        goto err_playertask;
    }
    

    vTaskSuspend(LeftPaddleTask);
    vTaskSuspend(RightPaddleTask);
    vTaskSuspend(PongControlTask);
    vTaskSuspend(MultiPlayerGame);
    vTaskSuspend(UDPControlTask);
    vTaskSuspend(MothershipTask);
    vTaskSuspend(PlayerTask);
    
    return 0;

err_playertask:
    vTaskDelete(MothershipTask);
err_mothershiptask:
    vTaskDelete(UDPControlTask);
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
    vQueueDelete(KilledInvaderQueue);
err_killedinvaderqueue:
    vQueueDelete(NextKeyQueue);
err_nextkeyqueue:
    vQueueDelete(BinaryStateQueue);
err_binarystatequeue:
    return -1;
}
