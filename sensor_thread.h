/*
 * sensor_thread.h - Periodic sensor reader (SCHED_FIFO) + jitter measurement
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 */
#ifndef SENSOR_THREAD_H
#define SENSOR_THREAD_H

#include "sms_common.h"
#include "circular_buffer.h"

typedef struct {
	const SmsConfig *cfg;
	CircularBuffer  *buf;
	int              fd;            /* /dev/sms_sensor (or -1 in simulation) */
} SensorCtx;

/* Open the device and program SMS_SET_INTERVAL. Returns fd (>=0) or -1 (errno set). */
int   sensor_open(const SmsConfig *cfg);
void  sensor_close(int fd);
void *sensor_thread(void *arg);

/* Jitter statistics (wake-up latency = actual - expected, microseconds). */
int   jitter_init(size_t max_samples);
void  jitter_free(void);
int   jitter_write_report(const char *path, const SmsConfig *cfg);

#endif
