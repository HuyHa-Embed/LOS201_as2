/*
 * circular_buffer.c - Thread-safe bounded circular buffer
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 *
 * One mutex protects every field; the condition variable (CLOCK_MONOTONIC)
 * implements the blocking consumer. Producers never block: when full the
 * sample is dropped and overflow_count is incremented.
 */
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "circular_buffer.h"

CircularBuffer *cb_create(size_t capacity)
{
	pthread_condattr_t ca;
	CircularBuffer *cb = calloc(1, sizeof(*cb));

	if (!cb)
		return NULL;
	cb->data = calloc(capacity, sizeof(SensorData));
	if (!cb->data) {
		free(cb);
		return NULL;
	}
	cb->capacity = capacity;
	pthread_mutex_init(&cb->lock, NULL);
	pthread_condattr_init(&ca);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_cond_init(&cb->not_empty, &ca);
	pthread_condattr_destroy(&ca);
	return cb;
}

void cb_destroy(CircularBuffer *cb)
{
	if (!cb)
		return;
	pthread_cond_destroy(&cb->not_empty);
	pthread_mutex_destroy(&cb->lock);
	free(cb->data);
	free(cb);
}

int cb_push(CircularBuffer *cb, const SensorData *s)
{
	int rc = CB_OK;

	pthread_mutex_lock(&cb->lock);
	if (cb->size == cb->capacity) {
		cb->overflow_count++;
		rc = CB_FULL;
	} else {
		cb->data[cb->head] = *s;
		cb->head = (cb->head + 1) % cb->capacity;
		cb->size++;
		cb->total_produced++;
		pthread_cond_signal(&cb->not_empty);
	}
	pthread_mutex_unlock(&cb->lock);
	return rc;
}

int cb_pop(CircularBuffer *cb, SensorData *out, int timeout_ms)
{
	struct timespec dl;
	int rc = CB_OK;

	clock_gettime(CLOCK_MONOTONIC, &dl);
	dl.tv_sec  += timeout_ms / 1000;
	dl.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (dl.tv_nsec >= 1000000000L) {
		dl.tv_sec++;
		dl.tv_nsec -= 1000000000L;
	}

	pthread_mutex_lock(&cb->lock);
	while (cb->size == 0 && !cb->closed) {
		if (pthread_cond_timedwait(&cb->not_empty, &cb->lock, &dl) == ETIMEDOUT)
			break;
	}
	if (cb->size > 0) {
		*out = cb->data[cb->tail];
		cb->tail = (cb->tail + 1) % cb->capacity;
		cb->size--;
		cb->total_consumed++;
	} else {
		rc = cb->closed ? CB_CLOSED : CB_TIMEOUT;
	}
	pthread_mutex_unlock(&cb->lock);
	return rc;
}

void cb_close(CircularBuffer *cb)
{
	pthread_mutex_lock(&cb->lock);
	cb->closed = 1;
	pthread_cond_broadcast(&cb->not_empty);
	pthread_mutex_unlock(&cb->lock);
}

#define CB_GETTER(name, type, field)                 \
type name(CircularBuffer *cb)                        \
{                                                    \
	type v;                                      \
	pthread_mutex_lock(&cb->lock);               \
	v = cb->field;                               \
	pthread_mutex_unlock(&cb->lock);             \
	return v;                                    \
}
CB_GETTER(cb_current_size,   size_t,        size)
CB_GETTER(cb_total_produced, unsigned long, total_produced)
CB_GETTER(cb_total_consumed, unsigned long, total_consumed)
CB_GETTER(cb_overflow_count, unsigned long, overflow_count)
