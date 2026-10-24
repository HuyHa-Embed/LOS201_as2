/*
 * processor_thread.c - Processor Thread
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 *
 * Consumer of the circular buffer. For every sample it updates a 10-sample
 * rolling average, checks the three alert rules and forwards alarms to the
 * POSIX message queue read by the Alert Thread.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "processor_thread.h"

int detect_anomalies(const SmsConfig *cfg, const SensorData *s, const SensorData *prev,
		     int have_prev, AlertMsg out[ALERT_TYPE_COUNT])
{
	int n = 0;
	volatile double temperature = s->temperature;   /* volatile: lets GDB 'set variable' take effect */

	if (temperature > cfg->temp_thr) {
		out[n++] = (AlertMsg){ ALERT_TEMP_HIGH, s->ts_ms, temperature, cfg->temp_thr };
	}
	if (s->humidity > cfg->hum_thr) {
		out[n++] = (AlertMsg){ ALERT_HUMID_HIGH, s->ts_ms, s->humidity, cfg->hum_thr };
	}
	if (have_prev && fabs(temperature - prev->temperature) > SMS_TEMP_SPIKE_DELTA) {
		out[n++] = (AlertMsg){ ALERT_TEMP_SPIKE, s->ts_ms, fabs(temperature - prev->temperature),
				       SMS_TEMP_SPIKE_DELTA };
	}
	return n;
}

static void send_alarm(const AlertMsg *src)
{
	struct timespec dl;
	AlertMsg a_zero, *a = &a_zero;

	memset(a, 0, sizeof(*a));       /* zero struct padding before it crosses the queue */
	a->type = src->type;
	a->ts_ms = src->ts_ms;
	a->value = src->value;
	a->threshold = src->threshold;

	clock_gettime(CLOCK_REALTIME, &dl);
	dl.tv_sec += 1;
	if (mq_timedsend(g_alarm_q, (const char *)a, sizeof(*a), 0, &dl) != 0)
		sms_log("PROC", "WARN alarm queue send failed: %s", strerror(errno));
}

void *processor_thread(void *arg)
{
	ProcessorCtx *ctx = arg;
	double wt[SMS_ROLLING_WINDOW] = {0}, wh[SMS_ROLLING_WINDOW] = {0};
	int wn = 0, wi = 0, have_prev = 0;
	SensorData s, prev = {0, 0, 0};

	pthread_setname_np(pthread_self(), "sms_proc");
	sms_log("PROC", "started, window=%d", SMS_ROLLING_WINDOW);

	for (;;) {
		int rc = cb_pop(ctx->buf, &s, SMS_QUEUE_TIMEOUT_MS);

		if (rc == CB_TIMEOUT)
			continue;
		if (rc == CB_CLOSED)
			break;                      /* producer done and buffer drained */

		wt[wi] = s.temperature;
		wh[wi] = s.humidity;
		wi = (wi + 1) % SMS_ROLLING_WINDOW;
		if (wn < SMS_ROLLING_WINDOW)
			wn++;
		{
			double at = 0, ah = 0;
			AlertMsg al[ALERT_TYPE_COUNT];
			int i, na;

			for (i = 0; i < wn; i++) {
				at += wt[i];
				ah += wh[i];
			}
			at /= wn;
			ah /= wn;

			na = detect_anomalies(ctx->cfg, &s, &prev, have_prev, al);
			for (i = 0; i < na; i++)
				send_alarm(&al[i]);

			sms_log("PROC", "ts=%lld T=%.2f H=%.2f avgT=%.2f avgH=%.2f alerts=%d",
				s.ts_ms, s.temperature, s.humidity, at, ah, na);
			if (ctx->cfg->verbose)
				printf("[SAMPLE] ts=%lld T=%.2f H=%.2f avgT=%.2f avgH=%.2f\n",
				       s.ts_ms, s.temperature, s.humidity, at, ah);
		}
		prev = s;
		have_prev = 1;
		atomic_fetch_add(&g_stats.samples_processed, 1);
	}
	sms_log("PROC", "stopped");
	atomic_store(&g_proc_done, 1);
	return NULL;
}
