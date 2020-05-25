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

#define EX3_FONT "IBMPlexSans-SemiBold.ttf"
#define EX4_FONT "IBMPlexSans-BoldItalic.ttf"

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
static TaskHandle_t Exercise2 = NULL;
static TaskHandle_t Exercise3 = NULL;
static TaskHandle_t Exercise4 = NULL;

static QueueHandle_t StateQueue = NULL;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

coord_t trianglePoints[3] = { 0 };
coord_t movingStringPosition[1] = { 0 };

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
void createCircle(circle_data_t *circleStruct){
    circleStruct->x = SCREEN_WIDTH / 2;
    circleStruct->y = SCREEN_HEIGHT / 2;
    circleStruct->radius = SHAPE_SIZE / 2;
    circleStruct->colour = Green;
}

// Creates a rectangle and asigns it color
void createRectangle(rect_data_t *rectStruct){
    rectStruct->x = SCREEN_WIDTH / 2 - SHAPE_SIZE / 2;
    rectStruct->y = SCREEN_HEIGHT / 2 - SHAPE_SIZE / 2;                 
    rectStruct->w = SHAPE_SIZE;                    
    rectStruct->h = SHAPE_SIZE;
    rectStruct->colour = Purple;
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
        if (buttons.buttons[KEYCODE(C)]) {
            buttons.buttons[KEYCODE(C)] = 0;
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
                    }
                    break;
                case STATE_THREE:
                    if (Exercise3) {
                        vTaskSuspend(Exercise3);
                    }
                    if (Exercise2) {
                        vTaskSuspend(Exercise2);
                    }
                    if (Exercise4) {
                        vTaskResume(Exercise4);
                    }
                    break;
                default:
                    break;
            }
            state_changed = 0;
        }
    }
}

void vExercise4(void *pvParameters){

    static char quitInstructions[100];
    static int quitInstructionssWidth = 0;
    
    static char randomString[100];
    static int randomStringWidth = 0;
    static int randomStringHeight = 0;
    
    // Format our string into our char array
    sprintf(quitInstructions,"Press Q to quit or C to change state.");
    sprintf(randomString, "Exercise 4?"); 
    
    tumDrawBindThread();

    while (1) {
        // Query events backend for new events, ie. button presses
        tumEventFetchEvents();    
   
        xGetButtonInput(); // Update global input
        
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            if (buttons.buttons[KEYCODE(Q)]) {
                exit(EXIT_SUCCESS);
            }
            xSemaphoreGive(buttons.lock);
        }

        tumDrawClear(TUMBlue); // Clear screen
 
        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)quitInstructions,
                            &quitInstructionssWidth, NULL)){
            tumDrawText(quitInstructions,
                        SCREEN_WIDTH / 2 - quitInstructionssWidth / 2
                        + moveWithMouseInX(),
                        SCREEN_HEIGHT - 5 * DEFAULT_FONT_SIZE
                        + moveWithMouseInY(),
                        White);
	    }
        // saves current font
        font_handle_t cur_font = tumFontGetCurFontHandle();
        // loads new font
        tumFontSelectFontFromName(EX4_FONT);
       
        // Draws the random text
        if(!tumGetTextSize((char *)randomString, 
                           &randomStringWidth, &randomStringHeight)){
            // draws the string
            tumDrawText(randomString, 
                       SCREEN_WIDTH / 2 - randomStringWidth / 2
                       + moveWithMouseInX(),
                       SCREEN_HEIGHT / 2 - randomStringHeight 
                       + moveWithMouseInY(), 
                       Pink);
        }
        
        // loads the previously saved font
        tumFontSelectFontFromHandle(cur_font);
        // frees the saved font from memory
        tumFontPutFontHandle(cur_font);

        tumDrawUpdateScreen(); // Refresh the screen to draw everything 

	    vCheckStateInput(); 
    }
}


void vExercise3(void *pvParameters){

    static char quitInstructions[100];
    static int quitInstructionssWidth = 0;
    
    static char randomString[100];
    static int randomStringWidth = 0;
    
    // Format our string into our char array
    sprintf(quitInstructions,"Press Q to quit or C to change state.");
    sprintf(randomString, "Exercise 3!"); 
    
    tumDrawBindThread();

    while (1) {
        // Query events backend for new events, ie. button presses
        tumEventFetchEvents();    
   
        xGetButtonInput(); // Update global input
        
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            if (buttons.buttons[KEYCODE(Q)]) {
                exit(EXIT_SUCCESS);
            }
            xSemaphoreGive(buttons.lock);
        }

        tumDrawClear(White); // Clear screen
 
        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)quitInstructions,
                            &quitInstructionssWidth, NULL)){
            tumDrawText(quitInstructions,
                        SCREEN_WIDTH / 2 - quitInstructionssWidth / 2,
                        SCREEN_HEIGHT - 2 * DEFAULT_FONT_SIZE,
                        TUMBlue);
	    }
        // saves current font
        font_handle_t cur_font = tumFontGetCurFontHandle();
        // loads new font
        tumFontSelectFontFromName(EX3_FONT);
       
        // Draws the random text
        if(!tumGetTextSize((char *)randomString, &randomStringWidth, NULL)){
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

        tumDrawUpdateScreen(); // Refresh the screen to draw everything 

	    vCheckStateInput();
    }

}

void vExercise2(void *pvParameters)
{
    TickType_t delay = 10;
    unsigned int *pressedButtons;

    static char quitInstructions[100];
    static int quitInstructionssWidth = 0;
    
    // create moving text
    static char movingString[100];
    static int movingStringWidth = 0;
    createMovingStr(movingStringPosition);

    // Format our string into our char array
    sprintf(quitInstructions,"Press Q to quit or C to change state.");
    sprintf(movingString, 
            "Click right mouse button to resset the pressed button count."); 
    
    // creating structs for three objects
    createTrianglePoints(trianglePoints); 
    createCircle(&circle);
    createRectangle(&rectangle);    
    
    // Needed such that Gfx library knows which thread controlls drawing
    // Only one thread can call tumDrawUpdateScreen while and thread can call
    // the drawing functions to draw objects. This is a limitation of the SDL
    // backend.
    tumDrawBindThread();

    while (1) {
        // Query events backend for new events, ie. button presses
        tumEventFetchEvents();    
   
        xGetButtonInput(); // Update global input
        
        pressedButtons = vDebounceButtons(); // Debounces buttons

        // `buttons` is a global shared variable and as such needs to be
        // guarded with a mutex, mutex must be obtained before accessing the
        // resource and given back when you're finished. If the mutex is not
        // given back then no other task can access the reseource.
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            if (buttons.buttons[KEYCODE(
                                    Q)]) { // Equiv to SDL_SCANCODE_Q
                exit(EXIT_SUCCESS);
            }
            xSemaphoreGive(buttons.lock);
        }

        tumDrawClear(White); // Clear screen
 
        // Get the width of the string on the screen so we can center it
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
                        movingStringPosition->x + moveWithMouseInX(),
                        movingStringPosition->y + moveWithMouseInY(), 
                        Navy)){ 
            tumGetTextSize((char *)movingString, &movingStringWidth, NULL);
 	        moveStringHorizontal(movingStringPosition, movingStringWidth);
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

        tumDrawUpdateScreen(); // Refresh the screen to draw everything 

        vCheckStateInput();

	    vTaskDelay(delay);
   }
}

int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize drawing");
        goto err_init_drawing;
    }

    if (tumEventInit()) {
        PRINT_ERROR("Failed to initialize events");
        goto err_init_events;
    }

    if (tumSoundInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize audio");
        goto err_init_audio;
    }

    tumFontLoadFont(EX4_FONT, 5 * DEFAULT_FONT_SIZE);

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }
    
    StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }
    
    if (xTaskCreate(basicSequentialStateMachine, "StateMachine",
                    mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES - 1, StateMachine) != pdPASS) {
        goto err_statemachine;
    }

    if (xTaskCreate(vExercise2, "Exercise 2", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY, &Exercise2) != pdPASS) {
        goto err_exercise2;
    }
    
    if (xTaskCreate(vExercise3, "Exercise 3", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY, &Exercise3) != pdPASS) {
        goto err_exercise3;
    }

    if (xTaskCreate(vExercise4, "Exercise 4", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY, &Exercise4) != pdPASS) {
        goto err_exercise4;
    }

    vTaskSuspend(Exercise2);
    vTaskSuspend(Exercise3);
    vTaskSuspend(Exercise4);    
    
    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_exercise4:
    vTaskDelete(Exercise3);
err_exercise3:
    vTaskDelete(Exercise2);
err_exercise2:
    vTaskDelete(StateMachine);
err_statemachine:
    vQueueDelete(StateQueue);
err_state_queue:
    vSemaphoreDelete(buttons.lock);
err_buttons_lock:
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
    /* This is just an example implementation of the "queue send" trace hook. */
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
