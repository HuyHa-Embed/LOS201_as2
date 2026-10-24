/*
 * alert_thread.c - Alert Thread
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 *
 * Blocks in mq_timedreceive() on the alarm queue (the timeout only exists so
 * the thread can notice that the processor has finished), prints the alert to
 * stderr, counts it per type and forwards it to the Logger Thread.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "alert_thread.h"

static const char *const names[ALERT_TYPE_COUNT] = { "TEMP_HIGH", "HUMID_HIGH", "TEMP_SPIKE" };

const char *alert_type_name(int type)
{
	return (type >= 0 && type < ALERT_TYPE_COUNT) ? names[type] : "UNKNOWN";
}

void *alert_thread(void *arg)
{
	(void)arg;
	pthread_setname_np(pthread_self(), "sms_alert");
	sms_log("ALERT", "started");

	for (;;) {
		AlertMsg a;
		struct timespec dl;
		ssize_t n;

		clock_gettime(CLOCK_REALTIME, &dl);
		dl.tv_nsec += SMS_QUEUE_TIMEOUT_MS * 1000000L;
		if (dl.tv_nsec >= 1000000000L) {
			dl.tv_sec++;
			dl.tv_nsec -= 1000000000L;
		}
		n = mq_timedreceive(g_alarm_q, (char *)&a, sizeof(a), NULL, &dl);
		if (n < 0) {
			if (errno == ETIMEDOUT || errno == EINTR) {
				if (atomic_load(&g_proc_done))    /* queue empty and no producer left */
					break;
				continue;
			}
			sms_log("ALERT", "ERROR mq_receive: %d", errno);
			break;
		}
		if (n != (ssize_t)sizeof(a))
			continue;
		if (a.type >= 0 && a.type < ALERT_TYPE_COUNT)
			atomic_fetch_add(&g_stats.alerts[a.type], 1);
		fprintf(stderr, "[ALERT][%lld] TYPE: %s VALUE: %.2f THRESHOLD: %.2f\n",
			a.ts_ms, alert_type_name(a.type), a.value, a.threshold);
		sms_log("ALERT", "[ALERT][%lld] TYPE: %s VALUE: %.2f THRESHOLD: %.2f",
			a.ts_ms, alert_type_name(a.type), a.value, a.threshold);
	}
	sms_log("ALERT", "stopped (TEMP_HIGH=%ld HUMID_HIGH=%ld TEMP_SPIKE=%ld)",
		atomic_load(&g_stats.alerts[0]), atomic_load(&g_stats.alerts[1]),
		atomic_load(&g_stats.alerts[2]));
	atomic_store(&g_alert_done, 1);
	return NULL;
}
