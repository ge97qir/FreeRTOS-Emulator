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
#include <pthread.h>

#include "TUM_Print.h"
#include "TUM_Utils.h"

struct error_print_msg {
	FILE *__restrict stream; // Either stdout, stderr or user defined file
	char msg[SAFE_PRINT_MAX_MSG_LEN];
};

char rbuf_buffer[sizeof(struct error_print_msg) *
		 SAFE_PRINT_INPUT_BUFFER_COUNT] = { 0 };

rbuf_handle_t input_rbuf = NULL;
pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_t print_thread = 0;

static void vfprints(FILE *__restrict __stream, const char *__format,
		     va_list args)
{
	struct error_print_msg *tmp_msg;
	if ((__stream == NULL) || (__format == NULL))
		return;

	// Queue is not ready, lets risk it and just print
	if (print_thread == 0) {
		vfprintf(__stream, __format, args);
		return;
	}

	//Blocks until buffer item is returned, ring buf is locked and must be
	//unlocked manually when item is finished being used
	tmp_msg = (struct error_print_msg *)rbuf_get_buffer(input_rbuf);

	if (tmp_msg == NULL)
		return;

	tmp_msg->stream = __stream;
	vsnprintf((char *)tmp_msg->msg, SAFE_PRINT_MAX_MSG_LEN, __format, args);

	rbuf_unlock(input_rbuf);
}

void fprints(FILE *__restrict __stream, const char *__format, ...)
{
	va_list args;
	va_start(args, __format);
	pthread_mutex_lock(&cond_mutex);
	vfprints(__stream, __format, args);
	pthread_mutex_unlock(&cond_mutex);
	pthread_cond_broadcast(&cond);
	va_end(args);
}

void prints(const char *__format, ...)
{
	va_list args;
	va_start(args, __format);
	vfprints(stdout, __format, args);
	va_end(args);
}

static void *safePrintTask(void *args)
{
	struct error_print_msg msgToPrint = { 0 };

	while (1) {
		pthread_mutex_lock(&cond_mutex);
		while (rbuf_size(input_rbuf)) {
			rbuf_get(input_rbuf, &msgToPrint);
			fprintf(msgToPrint.stream, "%s", msgToPrint.msg);
		}
		pthread_cond_wait(&cond, &cond_mutex);
		pthread_mutex_unlock(&cond_mutex);
	}

    return NULL;
}

int safePrintInit(void)
{
	input_rbuf = rbuf_init_static(sizeof(struct error_print_msg),
				      SAFE_PRINT_INPUT_BUFFER_COUNT,
				      (void *)rbuf_buffer);

	if (input_rbuf == NULL)
		goto err_rbuf;

	if (pthread_create(&print_thread, NULL, safePrintTask, NULL))
		goto err_thread;

	return 0;

err_thread:
	rbuf_free(input_rbuf);
err_rbuf:
	return -1;
}

void safePrintExit(void)
{
    pthread_cancel(print_thread);
    pthread_mutex_destroy(&cond_mutex);
    pthread_cond_destroy(&cond);
}
