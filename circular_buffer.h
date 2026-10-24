/*
 * circular_buffer.h - Thread-safe bounded circular buffer (SensorData)
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 */
#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <pthread.h>
#include "sms_common.h"

typedef struct {
	SensorData     *data;          /* heap, allocated once in cb_create() */
	size_t          capacity;
	size_t          head;          /* next slot to write */
	size_t          tail;          /* next slot to read  */
	size_t          size;
	int             closed;        /* producer finished */
	unsigned long   total_produced;
	unsigned long   total_consumed;
	unsigned long   overflow_count;
	pthread_mutex_t lock;
	pthread_cond_t  not_empty;
} CircularBuffer;

#define CB_OK        0
#define CB_FULL     (-1)
#define CB_TIMEOUT  (-2)
#define CB_CLOSED   (-3)

CircularBuffer *cb_create(size_t capacity);
void  cb_destroy(CircularBuffer *cb);
int   cb_push(CircularBuffer *cb, const SensorData *s);      /* non-blocking, CB_FULL on overflow */
int   cb_pop(CircularBuffer *cb, SensorData *out, int timeout_ms); /* blocks on cond var */
void  cb_close(CircularBuffer *cb);                          /* wake consumers, no more data */

size_t        cb_current_size(CircularBuffer *cb);
unsigned long cb_total_produced(CircularBuffer *cb);
unsigned long cb_total_consumed(CircularBuffer *cb);
unsigned long cb_overflow_count(CircularBuffer *cb);

#endif
