/**
 * @file TUM_Print.c
 * @author Alex Hoffman
 * @date 18 April 2020
 * @brief A couple of drop in replacements for `printf` and `fprintf` to be used
 * for thread safe printing when using FreeRTOS.
 *
 * @verbatim
 ----------------------------------------------------------------------
 Copyright (C) Alexander Hoffman, 2020
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 any later version.
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ----------------------------------------------------------------------
 @endverbatim
 */

#include <stdarg.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "TUM_Print.h"

struct error_print_msg {
	FILE *stream; // Either stdout, stderr or user defined file
	char msg[SAFE_PRINT_MAX_MSG_LEN];
};

xQueueHandle safePrintQueue = NULL;
xTaskHandle safePrintTaskHandle = NULL;

static void vfprints(FILE *__restrict __stream, const char *__format,
		     va_list args)
{
	struct error_print_msg tmp_msg;

	if ((__stream == NULL) || (__format == NULL))
		return;

	// Queue is not ready, lets risk it and just print
	if (safePrintQueue == NULL) {
		vfprintf(__stream, __format, args);
		return;
	}

	tmp_msg.stream = __stream;
	vsnprintf((char *)tmp_msg.msg, SAFE_PRINT_MAX_MSG_LEN, __format,
		  args);

	xQueueSend(safePrintQueue, &tmp_msg, 0);
}

void fprints(FILE *__restrict __stream, const char *__format, ...)
{
	va_list args;
	va_start(args, __format);
	vfprints(__stream, __format, args);
	va_end(args);
}

void prints(const char *__format, ...)
{
	va_list args;
	va_start(args, __format);
	vfprints(stdout, __format, args);
	va_end(args);
}

static void safePrintTask(void *pvParameters)
{
	static struct error_print_msg msgToPrint = { 0 };

	while (1) {
		if (safePrintQueue)
			if (xQueueReceive(safePrintQueue, &msgToPrint,
					  portMAX_DELAY) == pdTRUE) {
				fprintf(msgToPrint.stream, "%s",
					msgToPrint.msg);
			}
	}
}

int safePrintInit(void)
{
	safePrintQueue =
		xQueueCreate(SAFE_PRINT_QUEUE_LEN, SAFE_PRINT_MAX_MSG_LEN);

	if (safePrintQueue == NULL)
		return -1;

	xTaskCreate(safePrintTask, "Print", SAFE_PRINT_STACK_SIZE, NULL,
		    tskIDLE_PRIORITY, &safePrintTaskHandle);

	if (safePrintTaskHandle == NULL)
		return -1;

	return 0;
}

void safePrintExit(void)
{
	vTaskDelete(safePrintTaskHandle);

	vQueueDelete(safePrintQueue);
}
