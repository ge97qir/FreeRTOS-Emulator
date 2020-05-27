#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_Font.h"

#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

#define SHAPE_SIZE 38
#define RIGHT 1
#define LEFT 0
#define PRESSED 1
#define ON 1
#define OFF 0

#define EX3_FONT "IBMPlexSans-SemiBold.ttf"
#define EX4_FONT "IBMPlexSans-BoldItalic.ttf"

#define BLINK_QUEUE_LENGTH 1 
#define STATE_QUEUE_LENGTH 1
#define STATE_COUNT 3
#define STATE_ONE 0
#define STATE_TWO 1
#define STATE_THREE 2

#define NEXT_TASK 0
#define PREV_TASK 1

#define STARTING_STATE STATE_ONE
#define STATE_DEBOUNCE_DELAY 500

const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;

static TaskHandle_t StateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;
static TaskHandle_t Exercise2 = NULL;
static TaskHandle_t Exercise3 = NULL;
static TaskHandle_t Exercise4 = NULL;
static TaskHandle_t ExerciseStatic = NULL;
static TaskHandle_t ExerciseDynamic = NULL;
static TaskHandle_t AdditionalTask1 = NULL;
static TaskHandle_t AdditionalTask2 = NULL;
static TaskHandle_t ResetTask = NULL;
static TaskHandle_t IncreaseVariable = NULL;

static TaskHandle_t Task1 = NULL;
static TaskHandle_t Task2 = NULL;
static TaskHandle_t Task3 = NULL;
static TaskHandle_t Task4 = NULL;

static TimerHandle_t ResetTimer = NULL;

static StaticTask_t xTaskBuffer;
static StackType_t xStack[mainGENERIC_STACK_SIZE];

static QueueHandle_t BlinkQueue1 = NULL;
static QueueHandle_t BlinkQueue2 = NULL;
static QueueHandle_t StateQueue = NULL;
static QueueHandle_t AdditionalQueue1 = NULL;
static QueueHandle_t AdditionalQueue2 = NULL;
static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;
static SemaphoreHandle_t v_Lock = NULL;
static SemaphoreHandle_t x_Lock = NULL;
static SemaphoreHandle_t additionalSemaphore = NULL;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };
static unsigned short pressedX = 0;
static unsigned short pressedV = 0;

coord_t trianglePoints[3] = { 0 };
coord_t movingStringPosition[1] = { 0 };

#define FPS_AVERAGE_COUNT 50

void vDrawFPS(void)
{
    static unsigned int periods[FPS_AVERAGE_COUNT] = { 0 };
    static unsigned int periods_total = 0;
    static unsigned int index = 0;
    static unsigned int average_count = 0;
    static TickType_t xLastWakeTime = 0, prevWakeTime = 0;
    static char str[10] = { 0 };
    static int text_width;
    int fps = 0;

    xLastWakeTime = xTaskGetTickCount();

    if (prevWakeTime != xLastWakeTime) {
        periods[index] =
            configTICK_RATE_HZ / (xLastWakeTime - prevWakeTime);
        prevWakeTime = xLastWakeTime;
    }
    else {
        periods[index] = 0;
    }

    periods_total += periods[index];

    if (index == (FPS_AVERAGE_COUNT - 1)) {
        index = 0;
    }
    else {
        index++;
    }

    if (average_count < FPS_AVERAGE_COUNT) {
        average_count++;
    }
    else {
        periods_total -= periods[index];
    }

    fps = periods_total / average_count;

    
    sprintf(str, "FPS: %3d", fps);

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        tumDrawText(str, 
                    SCREEN_WIDTH - text_width - 5,
                    SCREEN_HEIGHT - 2 * DEFAULT_FONT_SIZE,
                    Skyblue);
}

typedef struct circle_data {
    signed short x;
    signed short y;
    signed short radius;
    unsigned int colour;
} circle_data_t;

typedef struct rect_data {
    signed short x;
    signed short y;
    signed short w;
    signed short h;
    unsigned int colour;
} rect_data_t;

circle_data_t circle = { 0 };
rect_data_t rectangle = { 0 };

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

signed int moveWithMouseInX(){
    signed int x = ((tumEventGetMouseX() - SCREEN_WIDTH / 2) / 5);
    return x;
}

signed int moveWithMouseInY(){
    signed int y = ((tumEventGetMouseY() - SCREEN_HEIGHT / 2) / 5);
    return y;
}

// Creates points that represent a triangle
void createTrianglePoints(coord_t *points) {
    points->x = SCREEN_WIDTH / 2 - SHAPE_SIZE / 2;
    points->y = SCREEN_HEIGHT / 2 + SHAPE_SIZE / 2;
    points++;          
    points->x = SCREEN_WIDTH / 2;
    points->y = SCREEN_HEIGHT / 2 - SHAPE_SIZE / 2;
    points++;
    points->x = SCREEN_WIDTH / 2 + SHAPE_SIZE / 2;
    points->y = SCREEN_HEIGHT / 2 + SHAPE_SIZE / 2;  
}

void moveTriWithMouse(coord_t *points) {
    points->x = SCREEN_WIDTH / 2 - SHAPE_SIZE / 2 + moveWithMouseInX();
    points->y = SCREEN_HEIGHT / 2 + SHAPE_SIZE / 2 + moveWithMouseInY();
    points++;          
    points->x = SCREEN_WIDTH / 2 + moveWithMouseInX();
    points->y = SCREEN_HEIGHT / 2 - SHAPE_SIZE / 2 + moveWithMouseInY();
    points++;
    points->x = SCREEN_WIDTH / 2 + SHAPE_SIZE / 2 + moveWithMouseInX();
    points->y = SCREEN_HEIGHT / 2 + SHAPE_SIZE / 2 + moveWithMouseInY();  
}
// Creates a circle and asigns it color
void createCircle(circle_data_t *circleStruct, unsigned int myColour){
    circleStruct->x = SCREEN_WIDTH / 2;
    circleStruct->y = SCREEN_HEIGHT / 2;
    circleStruct->radius = SHAPE_SIZE / 2;
    circleStruct->colour = myColour;
}

// Creates a rectangle and asigns it color
void createRectangle(rect_data_t *rectStruct, unsigned int myColour){
    rectStruct->x = SCREEN_WIDTH / 2 - SHAPE_SIZE / 2;
    rectStruct->y = SCREEN_HEIGHT / 2 - SHAPE_SIZE / 2;                 
    rectStruct->w = SHAPE_SIZE;                    
    rectStruct->h = SHAPE_SIZE;
    rectStruct->colour = myColour;
}

void createMovingStr(coord_t *movingStringPos){
    movingStringPos->x = SCREEN_WIDTH / 2;
    movingStringPos->y = DEFAULT_FONT_SIZE;
}

void moveStringHorizontal(coord_t *movingStringPos, int stringWidth){
    static int direction = RIGHT;

    // checks the direction of movement and checks for collision of 
    // the strign with the screen edge
    if(direction == RIGHT && 
       (movingStringPos->x + stringWidth < SCREEN_WIDTH)){
        movingStringPos->x += 1;
    }else{
        direction = LEFT;
    }

    if(direction == LEFT && 
       (movingStringPos->x > 0)){
        movingStringPos->x -= 1;
    }else{
        direction = RIGHT;
    } 
}

void vDrawButtonText(unsigned int *buttonInput)
{
    static char str[50] = { 0 };
    // counters for pressed buttons
    static int pressedA = 0;
    static int pressedB = 0;
    static int pressedC = 0;
    static int pressedD = 0;
    
    // displays mouse potition in x and y coordinates
    sprintf(str, "Axis 1: %5d | Axis 2: %5d", 
            tumEventGetMouseX(), tumEventGetMouseY());
    tumDrawText(str, 
                10 + moveWithMouseInX(), 
                DEFAULT_FONT_SIZE * 3 + moveWithMouseInY(),
                Navy);

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        
        // counts the number of times a button has been pressed
	    pressedA += (*buttonInput++);
        pressedB += (*buttonInput++);
        pressedC += (*buttonInput++);
        pressedD += (*buttonInput);

        sprintf(str, "A: %3d | B: %3d | C: %3d | D: %3d",
                pressedA, pressedB, pressedC, pressedD);

        xSemaphoreGive(buttons.lock);
        // resets the button press count on right mouse click
        if(tumEventGetMouseRight()){
            pressedA = 0; 	    
            pressedB = 0;
            pressedC = 0;
            pressedD = 0;
	}
        tumDrawText(str, 
                    10 + moveWithMouseInX(), 
                    DEFAULT_FONT_SIZE * 4 + moveWithMouseInY(), 
                    Navy);
    }
}

unsigned int* vDebounceButtons(){
 
    static unsigned int lastButtonState[4] = { 0 };
    static unsigned int reading[4] = { 0 };
    
    
    if(xSemaphoreTake(buttons.lock, 0) == pdTRUE){
        // reads which buttons are pressed
        reading[0] = buttons.buttons[KEYCODE(A)];
        reading[1] = buttons.buttons[KEYCODE(B)];
        reading[2] = buttons.buttons[KEYCODE(C)];
        reading[3] = buttons.buttons[KEYCODE(D)];
    
        for(int i = 0; i <=3 ; i++){
            // compares if the button state has changed and updates it if it has
            if(reading[i] != lastButtonState[i]){
                lastButtonState[i] = reading[i];
                    
            }else{
                // if state has not changed set it to not pressed
                reading[i] = 0;                    
            }
        }
    }
    xSemaphoreGive(buttons.lock);
    return reading;
}

static int vCheckStateInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        if (buttons.buttons[KEYCODE(E)]) {
            buttons.buttons[KEYCODE(E)] = 0;
            if (StateQueue) {
                xSemaphoreGive(buttons.lock);
                xQueueSend(StateQueue, &next_state_signal, 0);
                return -1;
            }
        }
        xSemaphoreGive(buttons.lock);
    }

    return 0;
}

void changeState(volatile unsigned char *state, unsigned char forwards)
{
    switch (forwards) {
        case NEXT_TASK:
            if (*state == STATE_COUNT - 1) {
                *state = 0;
            }
            else {
                (*state)++;
            }
            break;
        case PREV_TASK:
            if (*state == 0) {
                *state = STATE_COUNT - 1;
            }
            else {
                (*state)--;
            }
            break;
        default:
            break;
    }
}

/*
 * Example basic state machine with sequential states
 */
void basicSequentialStateMachine(void *pvParameters)
{
    unsigned char current_state = STARTING_STATE; // Default state
    unsigned char state_changed =
        1; // Only re-evaluate state if it has changed
    unsigned char input = 0;

    const int state_change_period = STATE_DEBOUNCE_DELAY;

    TickType_t last_change = xTaskGetTickCount();

    while (1) {
        if (state_changed) {
            goto initial_state;
        }

        // Handle state machine input
        if (StateQueue)
            if (xQueueReceive(StateQueue, &input, portMAX_DELAY) ==
                pdTRUE)
                if (xTaskGetTickCount() - last_change >
                    state_change_period) {
                    changeState(&current_state, input);
                    state_changed = 1;
                    last_change = xTaskGetTickCount();
                }

initial_state:
        // Handle current state
        if (state_changed) {
            switch (current_state) {
                case STATE_ONE:
                    if (Exercise4) {
                        vTaskSuspend(Exercise4);
                    }
                    if (Exercise3) {
                        vTaskSuspend(Exercise3);
                        vTaskSuspend(ExerciseStatic);
                        vTaskSuspend(ExerciseDynamic);
                        vTaskSuspend(AdditionalTask1);
                        xTimerStop(ResetTimer, 0);
                    }
                    if (Exercise2) {
                        vTaskResume(Exercise2);
                    }
                    break;
                case STATE_TWO:
                    if (Exercise4) {
                        vTaskSuspend(Exercise4);
                    }
                    if (Exercise2) {
                        vTaskSuspend(Exercise2);
                    }
                    if (Exercise3) {
                        vTaskResume(Exercise3);
                        vTaskResume(ExerciseStatic);
                        vTaskResume(ExerciseDynamic);
                        vTaskResume(AdditionalTask1);
                        xTimerStart(ResetTimer, 0);
                    }
                    break;
                case STATE_THREE:
                    if (Exercise3) {
                        vTaskSuspend(Exercise3);
                        vTaskSuspend(ExerciseStatic);
                        vTaskSuspend(ExerciseDynamic);
                        vTaskSuspend(AdditionalTask1);
                        xTimerStop(ResetTimer, 0);
                        
                    }
                    if (Exercise2) {
                        vTaskSuspend(Exercise2);
                    }
                    if (Exercise4) {
                        vTaskResume(Exercise4);
                      /*vTaskResume(Task1);
                        vTaskResume(Task2);
                        vTaskResume(Task3); 
                        vTaskResume(Task4);*/
                    }
                    break;
                default:
                    break;
            }
            state_changed = 0;
        }
    }
}
// need a queue for transmission of the final arrays (or every array POSEBEJ)

char ticks1[15] = {0};
char ticks2[15] = {0};
char ticks3[15] = {0};
char ticks4[15] = {0};


void vTask1 (void *pvParameters){
    static TickType_t tickStart, maxTicks; 
    maxTicks = 15 + xTaskGetTickCount();
    tickStart = xTaskGetTickCount();
    //static unsigned int row = 0;
    //static int column = 0;
    //vTaskSuspend(BufferSwap);    
    while(1){
        if(xTaskGetTickCount() >= maxTicks){ 
            //vTaskResume(BufferSwap);
            //vTaskResume(Exercise4);
            /*vTaskDelete(Task4);
            vTaskDelete(Task3);
            vTaskDelete(Task2);*/
            vTaskDelete(Task1);

        }
        //sprintf(&ticks[tickStart-xTaskGetTickCount()][column], "1");

        sprintf(&ticks1[xTaskGetTickCount()-tickStart], "1");
        //row++;
     //   tickStart = xTaskGetTickCount();
        vTaskDelayUntil(&tickStart, 1);
    }
}
void vTask2 (void *pvParameters){
    static TickType_t tickStart, maxTicks; 
    maxTicks = 15 + xTaskGetTickCount();
    tickStart = xTaskGetTickCount();
    //static unsigned int row = 0;
    //static int column = 1;
    while(1){
        if(xTaskGetTickCount() >= maxTicks){
            vTaskDelete(Task2);
        }
        sprintf(&ticks2[xTaskGetTickCount()-tickStart], "2");
        //sprintf(&ticks[tickStart-xTaskGetTickCount()][column], "2");
        //row++;
        //tickStart = xTaskGetTickCount();
        vTaskDelayUntil(&tickStart, 2);
    }
}
void vTask3 (void *pvParameters){
    static TickType_t tickStart, maxTicks; 
    maxTicks = 15 + xTaskGetTickCount();
    tickStart = xTaskGetTickCount();
    //static unsigned int row = 0;
    //static int column = 2;
    while(1){
        if(xTaskGetTickCount() >= maxTicks){ 
            vTaskDelete(Task3);
        }

        /*if(row >= 15){
            vTaskDelete(Task3);
        }*/
        
        //tickStart = xTaskGetTickCount();
        sprintf(&ticks3[xTaskGetTickCount() - tickStart], "3");
        //sprintf(&ticks3[row], "3");
        //row++;
        vTaskDelayUntil(&tickStart, 3);
        }
}

void vTask4 (void *pvParameters){
    static TickType_t tickStart, maxTicks; 
    maxTicks = 15 + xTaskGetTickCount();
    tickStart = xTaskGetTickCount();
    //static unsigned int row = 0;
    //static int column = 3;    
    while(1){
        if(xTaskGetTickCount() >= maxTicks){ 
            vTaskDelete(Task4);
        }
        //tickStart = xTaskGetTickCount();
        sprintf(&ticks4[xTaskGetTickCount()-tickStart], "4");
        /*if(row >= 15){
            vTaskDelete(Task4);
        }*/
        //sprintf(&ticks4[row], "4");
        //row++;
        vTaskDelayUntil(&tickStart, 4);

    }
}

void vExercise4(void *pvParameters){

    static char quitInstructions[100];
    static int quitInstructionssWidth = 0;
    
    static char randomString[100];
    static int randomStringWidth = 0;
    static int randomStringHeight = 0;
   
    char ticksString[15][10];
    // Format our string into our char array
    sprintf(quitInstructions,"Press Q to quit or E to change state.");
    sprintf(randomString, "Exercise 4? (3.3) :D "); 
    
    while (1) {
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {
                xGetButtonInput();

                xSemaphoreTake(ScreenLock, portMAX_DELAY);

                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(Q)]) {
                        exit(EXIT_SUCCESS);
                    }
                    xSemaphoreGive(buttons.lock);
                }

                tumDrawClear(TUMBlue); // Clear screen
         
                // Returns 0 if width was successfully obtained
                if (!tumGetTextSize((char *)quitInstructions,
                                    &quitInstructionssWidth, NULL)){
                    tumDrawText(quitInstructions,
                                SCREEN_WIDTH - quitInstructionssWidth,
                                SCREEN_HEIGHT - 4 * DEFAULT_FONT_SIZE,
                                White);
                }
                // saves current font
                font_handle_t cur_font = tumFontGetCurFontHandle();
                // loads new font
                tumFontSelectFontFromName(EX4_FONT);
               
                // Draws the random text
                if(!tumGetTextSize((char *)randomString, 
                                   &randomStringWidth, 
                                   &randomStringHeight)){
                    // draws the string
                    tumDrawText(randomString, 
                               SCREEN_WIDTH / 2 - randomStringWidth / 2,
                               0, Pink);
                }
                
                // loads the previously saved font
                tumFontSelectFontFromHandle(cur_font);
                // frees the saved font from memory
                tumFontPutFontHandle(cur_font);
                vDrawFPS();

              
                vTaskResume(Task1);
                vTaskResume(Task2);
                vTaskResume(Task3);
                vTaskResume(Task4);
                
                for(int i = 0; i < 15; i++){ 
                    sprintf(ticksString[i], "Tick %d: %c%c%c%c", i, 
                            ticks1[i], ticks2[i], ticks3[i], ticks4[i]); 
                    tumDrawText(ticksString[i], 42, randomStringHeight +
                            DEFAULT_FONT_SIZE * (i + 3), Black);     
                }   


                xSemaphoreGive(ScreenLock);

                vCheckStateInput(); 
            }
        }
    }            
}



void vExercise3(void *pvParameters){
  
    static char quitInstructions[100];
    static int quitInstructionssWidth = 0;
    
    static char randomString[100];
    static int randomStringWidth = 0;
    
    // for checking button input   
    static unsigned char lastButtonStateX = OFF;
    static unsigned char lastButtonStateV = OFF;
    static unsigned char lastButtonStateI = OFF;
    static unsigned char taskState = OFF;
       
    // Format our string into our char array
    sprintf(quitInstructions,"Press Q to quit or E to change state.");
    sprintf(randomString, "Exercise 3!"); 
    
    while (1) {
         if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {
                xGetButtonInput();

                xSemaphoreTake(ScreenLock, portMAX_DELAY); 
                
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(Q)]) {
                        exit(EXIT_SUCCESS);
                    }
                    xSemaphoreGive(buttons.lock);
                }
            
                // Returns 0 if width was successfully obtained
                if (!tumGetTextSize((char *)quitInstructions,
                                    &quitInstructionssWidth, NULL)){
                    tumDrawText(quitInstructions,
                                SCREEN_WIDTH / 2 - 
                                quitInstructionssWidth / 2,
                                SCREEN_HEIGHT - 2 * DEFAULT_FONT_SIZE,
                                TUMBlue);
                }
                // saves current font
                font_handle_t cur_font = tumFontGetCurFontHandle();
                // loads new font
                tumFontSelectFontFromName(EX3_FONT);
               
                // Draws the random text
                if(!tumGetTextSize((char *)randomString, 
                                    &randomStringWidth, NULL)){
                     // draws the string
                    tumDrawText(randomString, 
                               SCREEN_WIDTH / 2 - randomStringWidth / 2,
                               DEFAULT_FONT_SIZE, 
                               TUMBlue);
                }
                
                // loads the previously saved font
                tumFontSelectFontFromHandle(cur_font);
                // frees the saved font from memory
                tumFontPutFontHandle(cur_font);
              
                vDrawFPS();
                

                if (buttons.buttons[KEYCODE(X)] > lastButtonStateX){
                    xSemaphoreGive(additionalSemaphore); 
                }
                lastButtonStateX = buttons.buttons[KEYCODE(X)];
                if (buttons.buttons[KEYCODE(V)] > lastButtonStateV){
                    xTaskNotifyGive(AdditionalTask2);
                }
                lastButtonStateV = buttons.buttons[KEYCODE(V)];
                if (buttons.buttons[KEYCODE(I)] > lastButtonStateI){
                    taskState = !taskState;
                    // taskState = OFF (0) by default
                    if (taskState){
                        vTaskResume(IncreaseVariable);
                    }else{
                        vTaskSuspend(IncreaseVariable);
                    }
                }
                lastButtonStateI = buttons.buttons[KEYCODE(I)];
                           


                xSemaphoreGive(ScreenLock);

                vCheckStateInput();
            }
        }
    }
}

void vExercise2(void *pvParameters)
{
    unsigned int *pressedButtons;

    static char quitInstructions[100];
    static int quitInstructionssWidth = 0;
    
    // create moving text
    static char movingString[100];
    static int movingStringWidth = 0;
    createMovingStr(movingStringPosition);

    // Format our string into our char array
    sprintf(quitInstructions,"Press Q to quit or E to change state.");
    sprintf(movingString, 
            "Click right mouse button to resset the pressed button count."); 
    
    // creating structs for three objects
    createTrianglePoints(trianglePoints); 
    createCircle(&circle, Green);
    createRectangle(&rectangle, Purple);     

    while (1) {
         if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {
                xGetButtonInput();

                xSemaphoreTake(ScreenLock, portMAX_DELAY);
                
                pressedButtons = vDebounceButtons(); // Debounces buttons
          
                if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
                    if (buttons.buttons[KEYCODE(Q)]) {
                        exit(EXIT_SUCCESS);
                    }
                    xSemaphoreGive(buttons.lock);
                }
         
                // Returns 0 if width was successfully obtained
                if (!tumGetTextSize((char *)quitInstructions,
                                    &quitInstructionssWidth, NULL)){
                    tumDrawText(quitInstructions,
                                SCREEN_WIDTH / 2 - quitInstructionssWidth / 2
                                + moveWithMouseInX(),
                                SCREEN_HEIGHT - 5 * DEFAULT_FONT_SIZE
                                + moveWithMouseInY(),
                                Navy);
                }
                // Draws the moving text
                if(!tumDrawText(movingString, 
                                movingStringPosition->x 
                                + moveWithMouseInX(),
                                movingStringPosition->y 
                                + moveWithMouseInY(),
                                Navy)){ 
                    tumGetTextSize((char *)movingString, 
                                    &movingStringWidth, NULL);
                    moveStringHorizontal(movingStringPosition, 
                                         movingStringWidth);
                }
                // Draws a moving circle
                tumDrawCircle(circle.x - 2 * SHAPE_SIZE * 
                              cos(2 * 3.14 * xTaskGetTickCount()/2000)
                              + moveWithMouseInX(), 
                              circle.y + 2 * SHAPE_SIZE * 
                              sin(2 * 3.14 * xTaskGetTickCount()/2000)
                              + moveWithMouseInY(),
                              circle.radius, circle.colour);
                // Draws a moving rectangle
                tumDrawFilledBox(rectangle.x + 2 * SHAPE_SIZE *
                                 cos(2 * 3.14 * xTaskGetTickCount()/2000)
                                 + moveWithMouseInX(),
                                 rectangle.y - 2 * SHAPE_SIZE * 
                                 sin(2 * 3.14 * xTaskGetTickCount()/2000)
                                 + moveWithMouseInY(), 
                                 rectangle.w, rectangle.h, rectangle.colour);
                // Draws a triangle               
                moveTriWithMouse(trianglePoints); 
                tumDrawTriangle(trianglePoints, Red); 

                vDrawButtonText(pressedButtons); 
                vDrawFPS();                
                xSemaphoreGive(ScreenLock);

                vCheckStateInput();
            }
        }
    }
}

void vExerciseStatic(void *pvParameters){
 
    static TickType_t xLastWakeTime, prevDrawTime1;
    prevDrawTime1 = xTaskGetTickCount();
    static TickType_t frequency1 = 500;
    static unsigned char flagDraw1 = 0;   

    while(1){    
        xLastWakeTime = xTaskGetTickCount();
      
        if ((xLastWakeTime - prevDrawTime1) >= frequency1){
            prevDrawTime1 = xLastWakeTime;
            flagDraw1 = !flagDraw1;
        }  
        xQueueSend(BlinkQueue1, &flagDraw1, 0);   
    }
}
void vExerciseDynamic(void *pvParameters){
 
    static TickType_t xLastWakeTime, prevDrawTime2;
    prevDrawTime2 = xTaskGetTickCount();
    static TickType_t frequency2 = 250; 
    static unsigned char flagDraw2 = 0;   
    
    while(1){
        xLastWakeTime = xTaskGetTickCount();
        
        if ((xLastWakeTime - prevDrawTime2) >= frequency2){
            prevDrawTime2 = xLastWakeTime;
            flagDraw2 = !flagDraw2;
        }
        
        xQueueSend(BlinkQueue2, &flagDraw2, 0);
         
    }
}

void vAdditionalTask1(void *pvParameters){
   
    while(1){ 
        if (additionalSemaphore){
            if(xSemaphoreTake(additionalSemaphore, 0) == pdTRUE){
                if (xSemaphoreTake(x_Lock, 0) == pdTRUE){
                    pressedX++;
                  
                } 
            }
        }
        xQueueSend(AdditionalQueue1, &pressedX, 0);
        xQueueSend(AdditionalQueue2, &pressedV, 0);
    }
}

void vAdditionalTask2(void *pvParameters){
    
    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  
        if(xSemaphoreTake(v_Lock, 0)){
            pressedV++;
        }
        //xQueueSend(AdditionalQueue2, &pressedV, 0);
    }
}

void vResetTask(TimerHandle_t Timer){  
    const unsigned short resetValue = 0;
    if(xSemaphoreTake(v_Lock, 0) == pdTRUE){
        pressedV = resetValue;
        //xQueueSend(AdditionalQueue2, &pressedV, 0);
    } 
    if(xSemaphoreTake(x_Lock, 0) == pdTRUE){
        pressedX = resetValue;
        //xQueueSend(AdditionalQueue1, &pressedX, 0);
    }
    xTimerStart( ResetTimer, 0 );
  
}

void vIncreaseVariable(void *pvParameters){
    static TickType_t xLastCountTime; 
    const TickType_t countPeriod = 1000;  
    
    while(1){
        if (xSemaphoreTake(x_Lock, 0) == pdTRUE){
            pressedX++;
            //xQueueSend(AdditionalQueue1, &pressedX, 0);
            xLastCountTime = xTaskGetTickCount(); 
            vTaskDelayUntil(&xLastCountTime, countPeriod);
        }
    }
}

void vSwapBuffers(void *pvParameters)
{
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t frameratePeriod = 20;
    
    //structures to be drawn      
    static circle_data_t circle1 = { 0 };
    createCircle(&circle1, Red);
    static circle_data_t circle2 = { 0 };
    createCircle(&circle2, Blue);
    static char str[50] = { 0 };

    // queue receive
    static unsigned char draw1 = 0;
    static unsigned char draw2 = 0;
    static unsigned short pressed1 = 0;
    static unsigned short pressed2 = 0; 
    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {
        if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
            tumEventFetchEvents();
            xGetButtonInput();
            
            if(AdditionalQueue1){
                if(xQueueReceive(AdditionalQueue1, &pressed1, 0)
                        == pdTRUE){
                    if (AdditionalQueue2){
                        xQueueReceive(AdditionalQueue2, &pressed2, 0);
                    }
                    sprintf(str, "X %d | V %d", pressed1, pressed2);
                    tumDrawText(str, 10, 
                            SCREEN_HEIGHT-DEFAULT_FONT_SIZE*2, 
                            Black);
                }
            }
          
            xSemaphoreGive(x_Lock);
            xSemaphoreGive(v_Lock);

            if(BlinkQueue1){
                if(xQueueReceive(BlinkQueue1, &draw1, 0)
                        == pdTRUE){;
                    if(draw1){
                        tumDrawCircle(circle1.x - SHAPE_SIZE, 
                                      circle1.y,
                                      circle1.radius, circle1.colour); 
                    }
                }
            }
            if(BlinkQueue2){
                if(xQueueReceive(BlinkQueue2, &draw2, 0)
                        == pdTRUE){
                    if(draw2){
                        tumDrawCircle(circle2.x + SHAPE_SIZE, 
                                      circle2.y,
                                      circle2.radius, circle2.colour);
                    }
                }
            }
            
            tumDrawUpdateScreen();
            tumDrawClear(White);
            
            xSemaphoreGive(ScreenLock);
            xSemaphoreGive(DrawSignal);
            vTaskDelayUntil(&xLastWakeTime,
                            pdMS_TO_TICKS(frameratePeriod));
            }
    }
}

int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        goto err_init_drawing;
    }

    if (tumEventInit()) {
        goto err_init_events;
    }

    if (tumSoundInit(bin_folder_path)) {
        goto err_init_audio;
    }
    atexit(aIODeinit);
    tumFontLoadFont(EX4_FONT, 3 * DEFAULT_FONT_SIZE);
    
    // Semaphores and Mutexes
    v_Lock = xSemaphoreCreateMutex();
    if (!v_Lock) {
        goto err_v_lock;
    }
    x_Lock = xSemaphoreCreateMutex();
    if (!x_Lock) {
        goto err_x_lock;
    }
    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        goto err_buttons_lock;
    }
    DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!DrawSignal) {
        goto err_draw_signal;
    }
    ScreenLock = xSemaphoreCreateMutex();
    if (!ScreenLock) {
        goto err_screen_lock;
    }
    additionalSemaphore = xSemaphoreCreateBinary();
    if (!additionalSemaphore){
        goto err_additional_semaphore;
    }
    
    // Message sending
    
    AdditionalQueue1 = xQueueCreate(STATE_QUEUE_LENGTH, 
                                    sizeof(unsigned short));
    if (!AdditionalQueue1){
        goto err_additional_queue1;
    }
    AdditionalQueue2 = xQueueCreate(STATE_QUEUE_LENGTH, 
                                    sizeof(unsigned short)); 
    if (!AdditionalQueue2){
        goto err_additional_queue2;
    }
    StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateQueue) {
        goto err_state_queue;
    }
    BlinkQueue1 = xQueueCreate(BLINK_QUEUE_LENGTH, sizeof(unsigned char));
    if (!BlinkQueue1){
        goto err_blink_queue1;
    } 
    BlinkQueue2 = xQueueCreate(BLINK_QUEUE_LENGTH, sizeof(unsigned char));
    if(!BlinkQueue2){
        goto err_blink_queue2;
    }

    if (xTaskCreate(basicSequentialStateMachine, "StateMachine",
                    mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES - 1, StateMachine) != pdPASS) {
        goto err_statemachine;
    }
    if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES,
                    BufferSwap) != pdPASS) {
        goto err_bufferswap;
    }

    // Exercise tasks (dynaimc allocation)
    if (xTaskCreate(vExercise2, "Exercise 2", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY, &Exercise2) != pdPASS) {
        goto err_exercise2;
    }
    if (xTaskCreate(vExercise3, "Exercise 3", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY + 1, &Exercise3) != pdPASS) {
        goto err_exercise3;
    }

    if (xTaskCreate(vExercise4, "Exercise 4", mainGENERIC_STACK_SIZE * 2,
		    NULL, 5, &Exercise4) != pdPASS) {
        goto err_exercise4;
    }
    // Static allocation    
    ExerciseStatic = xTaskCreateStatic(vExerciseStatic, "Exercise Static", 
                (unsigned short)0, NULL, mainGENERIC_PRIORITY + 1,
                xStack, &xTaskBuffer);
    if (!ExerciseStatic) {
        goto err_exerciseStatic;
    }
    // Dynamic allocation
    if (xTaskCreate(vExerciseDynamic, "Exercise Dynamic", 
                mainGENERIC_STACK_SIZE, NULL, 
                mainGENERIC_PRIORITY + 1, &ExerciseDynamic) != pdPASS) {
        goto err_exerciseDynamic;
    }
    // Additional tasks / synchronisation
    if (xTaskCreate(vAdditionalTask1, "Additional Task 1", 
                mainGENERIC_STACK_SIZE, NULL, 
                mainGENERIC_PRIORITY + 1, &AdditionalTask1) != pdPASS) {
        goto err_additional_task1;
    }
    if (xTaskCreate(vAdditionalTask2, "Additional Task 2", 
                mainGENERIC_STACK_SIZE, NULL, 
                mainGENERIC_PRIORITY + 2, &AdditionalTask2) != pdPASS) {
        goto err_additional_task2;
    }
    if (xTaskCreate(vResetTask, "Reset Task", mainGENERIC_STACK_SIZE,
                NULL, configMAX_PRIORITIES, &ResetTask) != pdPASS){
        goto err_reset_task;
    }
    if (xTaskCreate(vIncreaseVariable, "Increase Bariable", 
                mainGENERIC_STACK_SIZE, NULL, configMAX_PRIORITIES, 
                &IncreaseVariable) != pdPASS){
        goto err_increase_variable;
    }
    ResetTimer = xTimerCreate("Reset Counter",pdMS_TO_TICKS(15000), pdTRUE,
                    NULL, vResetTask);
    if (!ResetTimer){
        goto err_reset_timer;
    }
    // numbered tasks
    if (xTaskCreate(vTask1, "Task 1", mainGENERIC_STACK_SIZE, 
                NULL, 1, &Task1) != pdPASS){
        goto err_task1;
    }
    if (xTaskCreate(vTask2, "Task 2", mainGENERIC_STACK_SIZE, 
                NULL, 2, &Task2) != pdPASS){
        goto err_task2;
    }
    if (xTaskCreate(vTask3, "Task 3", mainGENERIC_STACK_SIZE, 
                NULL, 3, &Task3) != pdPASS){
        goto err_task3;
    }
    if (xTaskCreate(vTask4, "Task 4", mainGENERIC_STACK_SIZE, 
                NULL, 4, &Task4) != pdPASS){
        goto err_task4;
    }
    
    vTaskSuspend(Exercise2);
    vTaskSuspend(Exercise3);
    vTaskSuspend(Exercise4);
    vTaskSuspend(ExerciseStatic);
    vTaskSuspend(ExerciseDynamic);
    vTaskSuspend(AdditionalTask1);
    vTaskSuspend(ResetTask);
    vTaskSuspend(IncreaseVariable);

    vTaskSuspend(Task1);
    vTaskSuspend(Task2);
    vTaskSuspend(Task3);  
    vTaskSuspend(Task4);

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_task4:
    vTaskDelete(Task3);
err_task3:
    vTaskDelete(Task2);
err_task2:
    vTaskDelete(Task1);
err_task1:
    xTimerDelete(ResetTimer, 0);
err_reset_timer:
    vTaskDelete(IncreaseVariable);
err_increase_variable:
    vTaskDelete(ResetTask);
err_reset_task:
    vTaskDelete(AdditionalTask2);
err_additional_task2:
    vTaskDelete(AdditionalTask1);
err_additional_task1:
    vTaskDelete(ExerciseDynamic);
err_exerciseDynamic:
    vTaskDelete(ExerciseStatic);
err_exerciseStatic:
    vTaskDelete(Exercise4);
err_exercise4:
    vTaskDelete(Exercise3);
err_exercise3:
    vTaskDelete(Exercise2);
err_exercise2:
    vTaskDelete(BufferSwap);
err_bufferswap:
    vTaskDelete(StateMachine);
err_statemachine:
    vQueueDelete(BlinkQueue2);
err_blink_queue2:
    vQueueDelete(BlinkQueue1);
err_blink_queue1:
    vQueueDelete(StateQueue);
err_state_queue:
    vQueueDelete(AdditionalQueue2);
err_additional_queue2:
    vQueueDelete(AdditionalQueue1);
err_additional_queue1:
    vSemaphoreDelete(additionalSemaphore);
err_additional_semaphore:
    vSemaphoreDelete(ScreenLock);
err_screen_lock:
    vSemaphoreDelete(DrawSignal);
err_draw_signal:
    vSemaphoreDelete(buttons.lock);
err_buttons_lock:
    vSemaphoreDelete(x_Lock);
err_x_lock:
    vSemaphoreDelete(v_Lock);
err_v_lock:
    tumSoundExit();
err_init_audio:
    tumEventExit();
err_init_events:
    tumDrawExit();
err_init_drawing:
    return EXIT_FAILURE;
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vMainQueueSendPassed(void)
{
    /* This is just an example implementation of the
     * "queue send" trace hook. */
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vApplicationIdleHook(void)
{
#ifdef __GCC_POSIX__
    struct timespec xTimeToSleep, xTimeSlept;
    /* Makes the process more agreeable when using the Posix simulator. */
    xTimeToSleep.tv_sec = 1;
    xTimeToSleep.tv_nsec = 0;
    nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}
