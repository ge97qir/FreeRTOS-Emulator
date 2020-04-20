/**
 * @file TUM_Utils.c
 * @author Alex Hoffman
 * @date 27 August 2019
 * @brief Utilities required by other TUM_XXX files
 *
 * @verbatim
   ----------------------------------------------------------------------
    Copyright (C) Alexander Hoffman, 2019
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

#include <stdio.h>
#include <string.h>
#include <libgen.h>

#ifndef __STDC_NO_ATOMICS__
#include <stdatomic.h>
#endif // _STDC_NO_ATOMICS__

#include <pthread.h>

#include "TUM_Utils.h"

char *prepend_path(char *path, char *file)
{
	char *ret = calloc(1, sizeof(char) * (strlen(path) + strlen(file) + 2));
	if (!ret) {
		fprintf(stderr, "[ERROR] prepend_bin_path malloc failed\n");
		return NULL;
	}

	strcpy(ret, path);
	strcat(ret, file);

	return ret;
}

char *getBinFolderPath(char *bin_path)
{
	char *dir_name = dirname(bin_path);

	char *ret = calloc(1, sizeof(char) * (strlen(dir_name) + 1));
	if (!ret)
		return NULL;

	strcpy(ret, dir_name);

	return ret;
}

// Ring buffer
struct ring_buf {
	pthread_mutex_t lock;
    unsigned char is_static;
	void *buffer;
	_Atomic int head; // Next free slot
	_Atomic int tail; // Last stored value
	size_t size;
	size_t item_size;
	unsigned char full;
};

#define CAST_RBUF(rbuf) ((struct ring_buf *)rbuf)

static void inc_buf(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	if (rb->full) {
		rb->tail += 1;
		rb->tail %= rb->size;

		rb->head = rb->tail;
	} else {
		rb->head += 1;
		rb->head %= rb->size;
	}

	rb->full = (rb->head == rb->tail);
}

static void dec_buf(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	rb->full = 0;
	rb->tail += 1;
	rb->tail %= rb->size;
}

rbuf_handle_t rbuf_init(size_t item_size, size_t item_count)
{
	struct ring_buf *ret =
		(struct ring_buf *)calloc(1, sizeof(struct ring_buf));

	if (ret == NULL)
		goto err_alloc_rbuf;

	ret->buffer = calloc(item_count, item_size);

	if (ret->buffer == NULL)
		goto err_alloc_buffer;

    ret->is_static = 0;
	ret->head = 0;
	ret->tail = 0;
	ret->size = item_count;
	ret->item_size = item_size;

	if (pthread_mutex_init(&ret->lock, NULL))
		goto err_mutex;

	return (rbuf_handle_t)ret;

err_mutex:
    if(!ret->is_static)
	    free(ret->buffer);
err_alloc_buffer:
	free(ret);
err_alloc_rbuf:
	return NULL;
}

rbuf_handle_t rbuf_init_static(size_t item_size, size_t item_count,
			       void *buffer)
{
	if (buffer == NULL)
		goto err_buffer;

	struct ring_buf *ret =
		(struct ring_buf *)calloc(1, sizeof(struct ring_buf));

	if (ret == NULL)
		goto err_alloc_rbuf;

    ret->is_static = 1;
	ret->head = 0;
	ret->tail = 0;
	ret->buffer = buffer;
	ret->size = item_count;
	ret->item_size = item_size;

	if (pthread_mutex_init(&ret->lock, NULL))
		goto err_mutex;

	return (rbuf_handle_t)ret;

err_mutex:
	free(ret);
err_alloc_rbuf:
err_buffer:
	return NULL;
}

//Destroy
void rbuf_free(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

    if(!rb->is_static)
	    free(rb->buffer);

    pthread_mutex_unlock(&rb->lock);
    pthread_mutex_destroy(&rb->lock);

	free(rb);
}

//Reset
void rbuf_reset(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	rb->head = 0;
	rb->tail = 0;
	rb->full = 0;
	
    pthread_mutex_unlock(&rb->lock);
}

//Put pointer to buffer back
//Works the same as get, using references already put could result in undefined
//behaviour
int rbuf_put_buffer(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return -1;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	dec_buf(rb);

	pthread_mutex_unlock(&rb->lock);

	return 0;
}

//Add data
int rbuf_put(rbuf_handle_t rbuf, void *data)
{
	if (rbuf == NULL)
		return -1;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	if (rb->buffer == NULL)
		goto err_fail;

	if (rb->full)
		goto err_fail;

	memcpy(rb->buffer + rb->head * rb->item_size, data, rb->item_size);

	inc_buf(rb);

	pthread_mutex_unlock(&rb->lock);

	return 0;

err_fail:
	pthread_mutex_unlock(&rb->lock);
	return -1;
}

//Add and overwrite
int rbuf_fput(rbuf_handle_t rbuf, void *data)
{
	if (rbuf == NULL)
		return -1;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	if (rb->buffer == NULL)
        goto err_fail;

	memcpy(rb->buffer + rb->head * rb->item_size, data, rb->item_size);

	inc_buf(rb);
	
    pthread_mutex_unlock(&rb->lock);

	return 0;

err_fail:
	pthread_mutex_unlock(&rb->lock);
	return -1;
}

void rbuf_unlock(rbuf_handle_t rbuf)
{
	struct ring_buf *rb = CAST_RBUF(rbuf);
   
    pthread_mutex_unlock(&rb->lock);
}

//Get pointer to buffer slot
//Works similar to put except it just returns a pointer to the ringbuf slot
//Is returned in a locked state
void *rbuf_get_buffer(rbuf_handle_t rbuf)
{
	static const _Atomic int increment = 1;
	if (rbuf == NULL)
		return NULL;

	void *ret;
	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	if (rb->buffer == NULL)
		goto err_fail;

	if (rb->full)
		goto err_fail;

	int offset = __sync_fetch_and_add((int *)&rb->head, increment);
	ret = rb->buffer + offset * rb->item_size;

	inc_buf(rb);

	return ret;

err_fail:
	pthread_mutex_unlock(&rb->lock);
	return NULL;
}

//Get data
int rbuf_get(rbuf_handle_t rbuf, void *data)
{
	if (rbuf == NULL)
		return -1;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	if (rb->buffer == NULL)
		goto err_fail;

	if (rbuf_empty(rb))
		goto err_fail;

	memcpy(data, rb->buffer + rb->tail * rb->item_size, rb->item_size);

	dec_buf(rb);

	pthread_mutex_unlock(&rb->lock);

	return 0;

err_fail:
	pthread_mutex_unlock(&rb->lock);
	return -1;
}

//Check empty or full
unsigned char rbuf_empty(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return -1;

	unsigned char ret;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	ret = (!rb->full) && (rb->head == rb->tail);

	pthread_mutex_unlock(&rb->lock);

	return ret;
}

unsigned char rbug_full(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return -1;

    unsigned char ret;

	struct ring_buf *rb = CAST_RBUF(rbuf);

	pthread_mutex_lock(&rb->lock);

	ret = rb->full;
	
    pthread_mutex_unlock(&rb->lock);

    return ret;
}

//Num of elements
size_t rbuf_size(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return -1;

    size_t ret;
	struct ring_buf *rb = CAST_RBUF(rbuf);
	
    pthread_mutex_lock(&rb->lock);

	ret = rb->size;

	if (!rb->full) {
		ret = rb->head - rb->tail;
		if (rb->tail > rb->head)
			ret += rb->size;
	}
    
    pthread_mutex_unlock(&rb->lock);

	return ret;
}

//Get max capacity
size_t rbuf_capacity(rbuf_handle_t rbuf)
{
	if (rbuf == NULL)
		return -1;

    size_t ret;
	struct ring_buf *rb = CAST_RBUF(rbuf);
    
    pthread_mutex_lock(&rb->lock);

	ret = rb->size;
    
    pthread_mutex_unlock(&rb->lock);

    return ret;
}
