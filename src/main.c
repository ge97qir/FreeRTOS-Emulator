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

static TaskHandle_t DemoTask = NULL;

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
    movingStringPos->y = 2 * DEFAULT_FONT_SIZE;
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

void vDemoTask(void *pvParameters)
{
    TickType_t delay = 10;

    // structure to store time retrieved from Linux kernel
    static struct timespec the_time;
    static char our_time_string[100];
    static int our_time_strings_width = 0;
    
    // create moving text
    static char movingString[50];
    static int movingStringWidth = 0;

    // Format our string into our char array
    sprintf(our_time_string,"Press Q to quit.");
    sprintf(movingString, "Weeeee!!!"); 

    createMovingStr(movingStringPosition);

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
        tumEventFetchEvents(); // Query events backend for new events, ie. button presses
        xGetButtonInput(); // Update global input

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

        clock_gettime(CLOCK_REALTIME,
                      &the_time); // Get kernel real time

        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)our_time_string,
                            &our_time_strings_width, NULL)){
            tumDrawText(our_time_string,
                        SCREEN_WIDTH / 2 -
                        our_time_strings_width / 2,
                        SCREEN_HEIGHT - 2 * DEFAULT_FONT_SIZE,
                        Navy);
	}
        // Draws the moving text
        if(!tumDrawText(movingString, movingStringPosition->x,
                        movingStringPosition->y, Navy)){ 

            tumGetTextSize((char *)movingString, &movingStringWidth, NULL);
 	    moveStringHorizontal(movingStringPosition, movingStringWidth);
	}

 	tumDrawCircle(circle.x - 3 * SHAPE_SIZE * 
	             cos(2 * 3.14 * the_time.tv_nsec / 1000000000), 
                     circle.y + 3 * SHAPE_SIZE * 
		     sin(2 * 3.14 * the_time.tv_nsec / 1000000000),
                     circle.radius, circle.colour);
        tumDrawFilledBox(rectangle.x + 3 * SHAPE_SIZE *
		        cos(2 * 3.14 * the_time.tv_nsec / 1000000000),
                        rectangle.y - 3 * SHAPE_SIZE * 
		 	sin(2 * 3.14 * the_time.tv_nsec / 1000000000), 
                        rectangle.w, rectangle.h, rectangle.colour);
                        
        tumDrawTriangle(trianglePoints, Red); 

        tumDrawUpdateScreen(); // Refresh the screen to draw string

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

    if (xTaskCreate(vDemoTask, "DemoTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &DemoTask) != pdPASS) {
        goto err_demotask;
    }

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_demotask:
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
