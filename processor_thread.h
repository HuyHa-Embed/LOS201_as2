/*
 * processor_thread.h - Processor Thread: rolling average + anomaly detection
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 */
#ifndef PROCESSOR_THREAD_H
#define PROCESSOR_THREAD_H

#include "sms_common.h"
#include "circular_buffer.h"

typedef struct {
	const SmsConfig *cfg;
	CircularBuffer  *buf;
} ProcessorCtx;

void *processor_thread(void *arg);

/* Pure detection logic, exposed for unit testing / GDB scenario B.
 * Returns number of alerts raised (0..3) and fills 'out'. */
int detect_anomalies(const SmsConfig *cfg, const SensorData *s, const SensorData *prev,
		     int have_prev, AlertMsg out[ALERT_TYPE_COUNT]);

#endif
