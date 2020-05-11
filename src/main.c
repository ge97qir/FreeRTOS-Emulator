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

//#define h_screen_w SCREEN_WIDTH/2
//#define h_screen_h SCREEN_HEIGHT/2

static TaskHandle_t DemoTask = NULL;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

typedef struct my_circle_t {
    signed short x_pos;
    signed short y_pos;
    signed short radius;
    unsigned int color;
} my_circle_t;

typedef struct my_square_t{
    signed short x_pos;
    signed short y_pos;
    signed short width;
    signed short height;
    unsigned int color;
} my_square_t;

typedef struct my_triangle_t{
    coord_t* points;
    unsigned int color;
} my_triangle_t;

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) 
    {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

void vDemoTask(void *pvParameters)
{
    // String
    static char my_string[100];
    static int my_strings_width = 0;

    // structure for my circle variables
    static my_circle_t circ;
    circ.x_pos=SCREEN_WIDTH / 4;
    circ.y_pos=SCREEN_HEIGHT / 4;
    circ.color=Red;
    circ.radius=20;

    //structure for my triangle variables
    static my_triangle_t tri;

    coord_t p_1;
    p_1.x=320;
    p_1.y=200;
    coord_t p_2;
    p_2.x=250;
    p_2.y=200;
    coord_t p_3;
    p_3.x=p_2.x+(p_1.x-p_2.x)/2;
    p_3.y=100;
    
    coord_t points[3] ={p_1,p_2,p_3};

    tri.points = points;
    tri.color = Green;

    //structure for my square
    static my_square_t box;
    box.x_pos=380;
    box.y_pos=100;
    box.width=100;
    box.height=100;


    tumDrawBindThread();

    while (1) 
    {
        tumEventFetchEvents(); 
        xGetButtonInput(); 

        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) 
        {
            if (buttons.buttons[KEYCODE(Q)]) // Equiv to SDL_SCANCODE_Q
            { 
                exit(EXIT_SUCCESS);
            }
            xSemaphoreGive(buttons.lock);
        }

        tumDrawClear(White); // Clear screen

        sprintf(my_string,"Press Q to quit"); // Formatting string into char array.

        if (!tumGetTextSize((char *)my_string,&my_strings_width, NULL))
            tumDrawText(my_string,SCREEN_WIDTH / 2 -
                        my_strings_width / 2,
                        SCREEN_HEIGHT*3 / 4 - DEFAULT_FONT_SIZE / 2,
                        Navy);
        
        if (!tumDrawCircle(circ.x_pos,circ.y_pos,circ.radius,circ.color)){} //Draw Circle.

        if (!tumDrawTriangle(tri.points,tri.color)){} //Draw Triangle.
        
        if(!tumDrawFilledBox(box.x_pos,box.y_pos,box.width,box.height,TUMBlue)){} //Draw Box.

        tumDrawUpdateScreen(); // Refresh the screen to draw string

        vTaskDelay((TickType_t)1000); // Basic sleep of 1000 milliseconds
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
