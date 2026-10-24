/*
 * logger_thread.c - Logger Thread
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 *
 * Only this thread touches the log file, so the file needs no lock.
 * Flushes every SMS_LOG_FLUSH_SEC seconds (not per line) and rotates
 * sms.log -> sms.log.1 when the size exceeds cfg->log_max_bytes.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include "logger_thread.h"

static void rotate(FILE **f, const char *path)
{
	char old[256];

	fclose(*f);
	snprintf(old, sizeof(old), "%s.1", path);
	rename(path, old);              /* replaces a previous .1 */
	*f = fopen(path, "w");
}

static void write_line(FILE *f, const LogMsg *m)
{
	struct timeval tv;
	struct tm tm;
	char ts[32];

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
	fprintf(f, "%s.%03ld %s\n", ts, (long)(tv.tv_usec / 1000), m->text);
}

void *logger_thread(void *arg)
{
	const SmsConfig *cfg = arg;
	FILE *f = fopen(cfg->log_path, "a");
	time_t last_flush = time(NULL);
	long written = 0;

	pthread_setname_np(pthread_self(), "sms_logger");
	if (!f) {
		fprintf(stderr, "logger: cannot open %s: %s\n", cfg->log_path, strerror(errno));
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	written = ftell(f);

	for (;;) {
		LogMsg m;
		struct timespec dl;
		ssize_t n;

		clock_gettime(CLOCK_REALTIME, &dl);
		dl.tv_nsec += SMS_QUEUE_TIMEOUT_MS * 1000000L;
		if (dl.tv_nsec >= 1000000000L) {
			dl.tv_sec++;
			dl.tv_nsec -= 1000000000L;
		}
		n = mq_timedreceive(g_log_q, (char *)&m, sizeof(m), NULL, &dl);
		if (n == (ssize_t)sizeof(m)) {
			m.text[SMS_LOG_TEXT_MAX - 1] = '\0';
			write_line(f, &m);
			written += (long)strlen(m.text) + 24;
			if (written > cfg->log_max_bytes) {
				rotate(&f, cfg->log_path);
				written = 0;
				if (!f)
					return NULL;
			}
		} else if (n < 0 && errno != ETIMEDOUT && errno != EINTR) {
			break;
		} else if (n < 0 && atomic_load(&g_logger_stop)) {
			break;                  /* queue drained and everyone upstream finished */
		}
		if (time(NULL) - last_flush >= SMS_LOG_FLUSH_SEC) {
			fflush(f);
			last_flush = time(NULL);
		}
	}
	fflush(f);
	fclose(f);
	return NULL;
}
