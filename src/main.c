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

static TaskHandle_t exercise2 = NULL;

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

void vDrawButtonText(void)
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
	    pressedA += buttons.buttons[KEYCODE(A)];
        pressedB += buttons.buttons[KEYCODE(B)];
        pressedC += buttons.buttons[KEYCODE(C)];
        pressedD += buttons.buttons[KEYCODE(D)];

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

void vDebounceButtons(buttons_buffer_t *buttonInput){
 
    static unsigned int lastButtonState[4] = { 0 };
    static unsigned int reading[4] = { 0 };
    
    if(xSemaphoreTake(buttonInput->lock, 0) == pdTRUE){
        // reads which buttons are pressed
        reading[0] = buttonInput->buttons[KEYCODE(A)];
        reading[1] = buttonInput->buttons[KEYCODE(B)];
        reading[2] = buttonInput->buttons[KEYCODE(C)];
        reading[3] = buttonInput->buttons[KEYCODE(D)];
    }
    for(int i = 0; i <=3 ; i++){
        // compares if the button state has changed and updates it if it has
        if(reading[i] != lastButtonState[i]){
            lastButtonState[i] = reading[i];
                
        }else{
            // if button was not pressed or released set it to unpressed state 
            switch(i){
                case 0:
                    buttonInput->buttons[KEYCODE(A)] = 0;
                    break;
                case 1:
                    buttonInput->buttons[KEYCODE(B)] = 0;
                    break;
                case 2:
                    buttonInput->buttons[KEYCODE(C)] = 0;
                    break;
                case 3:
                    buttonInput->buttons[KEYCODE(D)] = 0;
                    break;
            }       
        }
        xSemaphoreGive(buttonInput->lock);
    }
}

void vExercise2(void *pvParameters)
{
    TickType_t delay = 10;

    // structure to store time retrieved from Linux kernel
    static struct timespec the_time;
    static char our_time_string[100];
    static int our_time_strings_width = 0;
    
    // create moving text
    static char movingString[100];
    static int movingStringWidth = 0;
    createMovingStr(movingStringPosition);

    // Format our string into our char array
    sprintf(our_time_string,"Press Q to quit.");
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
        
        vDebounceButtons(&buttons); // Debounces buttons

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
 
        clock_gettime(CLOCK_REALTIME, &the_time); // Get kernel real time

        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)our_time_string,
                            &our_time_strings_width, NULL)){
            tumDrawText(our_time_string,
                        SCREEN_WIDTH / 2 - our_time_strings_width / 2
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
	                  cos(2 * 3.14 * the_time.tv_nsec / 1000000000)
                      + moveWithMouseInX(), 
                      circle.y + 2 * SHAPE_SIZE * 
		              sin(2 * 3.14 * the_time.tv_nsec / 1000000000)
                      + moveWithMouseInY(),
                      circle.radius, circle.colour);
        // Draws a moving rectangle
	    tumDrawFilledBox(rectangle.x + 2 * SHAPE_SIZE *
		                 cos(2 * 3.14 * the_time.tv_nsec / 1000000000)
                         + moveWithMouseInX(),
                         rectangle.y - 2 * SHAPE_SIZE * 
		                 sin(2 * 3.14 * the_time.tv_nsec / 1000000000)
                         + moveWithMouseInY(), 
                         rectangle.w, rectangle.h, rectangle.colour);
        // Draws a triangle               
        moveTriWithMouse(trianglePoints); 
        tumDrawTriangle(trianglePoints, Red); 

    	vDrawButtonText();

        tumDrawUpdateScreen(); // Refresh the screen to draw everything 

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

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }

    if (xTaskCreate(vExercise2, "Exercise 2", mainGENERIC_STACK_SIZE * 2,
		    NULL, mainGENERIC_PRIORITY, &exercise2) != pdPASS) {
        goto err_exercise2;
    }

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_exercise2:
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
