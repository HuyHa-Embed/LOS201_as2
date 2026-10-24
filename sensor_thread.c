/*
 * sensor_thread.c - Sensor Thread: periodic read of /dev/sms_sensor
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 *
 * Period is driven by clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME) so that
 * jitter does not accumulate. Wake-up latency (actual - expected) of every
 * cycle is stored in a pre-allocated array for Min/Avg/Max/P99 + histogram.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include "sensor_thread.h"
#include "../driver/sms_sensor.h"

#define NSEC_PER_SEC 1000000000LL

static long long *g_jitter;     /* microseconds, heap, allocated once */
static size_t     g_jitter_cap;
static size_t     g_jitter_n;

static inline long long ts_ns(const struct timespec *t)
{
	return (long long)t->tv_sec * NSEC_PER_SEC + t->tv_nsec;
}

int jitter_init(size_t max_samples)
{
	g_jitter = calloc(max_samples, sizeof(*g_jitter));
	g_jitter_cap = g_jitter ? max_samples : 0;
	g_jitter_n = 0;
	return g_jitter ? 0 : -1;
}

void jitter_free(void)
{
	free(g_jitter);
	g_jitter = NULL;
	g_jitter_cap = g_jitter_n = 0;
}

static int cmp_ll(const void *a, const void *b)
{
	long long x = *(const long long *)a, y = *(const long long *)b;
	return (x > y) - (x < y);
}

int jitter_write_report(const char *path, const SmsConfig *cfg)
{
	FILE *f;
	size_t i, n = g_jitter_n;
	long long min, max, sum = 0, p99;
	long b[4] = {0, 0, 0, 0};
	long long *sorted;

	if (!path || n == 0)
		return -1;
	sorted = malloc(n * sizeof(*sorted));
	if (!sorted)
		return -1;
	memcpy(sorted, g_jitter, n * sizeof(*sorted));
	qsort(sorted, n, sizeof(*sorted), cmp_ll);
	min = sorted[0];
	max = sorted[n - 1];
	p99 = sorted[(size_t)((n - 1) * 0.99)];
	for (i = 0; i < n; i++) {
		long long j = g_jitter[i];
		sum += j;
		if (j < 100)        b[0]++;
		else if (j < 500)   b[1]++;
		else if (j < 1000)  b[2]++;
		else                b[3]++;
	}
	f = fopen(path, "w");
	if (!f) {
		free(sorted);
		return -1;
	}
	fprintf(f, "=== Sensor thread wake-up jitter ===\n");
	fprintf(f, "policy   : %s (priority %d)\n",
		cfg->sched_mode == SCHED_MODE_FIFO ? "SCHED_FIFO" : "SCHED_OTHER", cfg->rt_priority);
	fprintf(f, "interval : %d ms\n", cfg->interval_ms);
	fprintf(f, "samples  : %zu\n", n);
	fprintf(f, "min_us   : %lld\n", min);
	fprintf(f, "avg_us   : %.1f\n", (double)sum / (double)n);
	fprintf(f, "max_us   : %lld\n", max);
	fprintf(f, "p99_us   : %lld\n", p99);
	fprintf(f, "missed_deadlines (late by >= 1 period): %ld\n", atomic_load(&g_stats.missed_deadlines));
	fprintf(f, "histogram (text):\n");
	fprintf(f, "  0-100us    : %6ld  %5.1f%%\n", b[0], 100.0 * b[0] / n);
	fprintf(f, "  100-500us  : %6ld  %5.1f%%\n", b[1], 100.0 * b[1] / n);
	fprintf(f, "  500-1000us : %6ld  %5.1f%%\n", b[2], 100.0 * b[2] / n);
	fprintf(f, "  >1ms       : %6ld  %5.1f%%\n", b[3], 100.0 * b[3] / n);
	fclose(f);
	free(sorted);
	return 0;
}

int sensor_open(const SmsConfig *cfg)
{
#ifdef SMS_SIMULATE_DRIVER
	(void)cfg;
	return -2;                  /* host build: samples are generated in user space */
#else
	unsigned int ivl = (unsigned int)cfg->interval_ms;
	int fd = open(cfg->device_path, O_RDONLY);

	if (fd < 0)
		return -1;
	if (ioctl(fd, SMS_SET_INTERVAL, &ivl) < 0) {
		int e = errno;
		close(fd);
		errno = e;
		return -1;
	}
	return fd;
#endif
}

void sensor_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

/* Read one CSV line. Returns 0 on success, -1 on error (already logged). */
static int read_sample(int fd, SensorData *out)
{
	char line[96];
	ssize_t n;
	int tries = 0;

#ifdef SMS_SIMULATE_DRIVER
	{
		static unsigned int seed = 12345;
		(void)fd;
		out->ts_ms = sms_now_ms();
		out->temperature = 15.0 + (rand_r(&seed) % 3001) / 100.0;
		out->humidity    = 30.0 + (rand_r(&seed) % 6001) / 100.0;
		(void)line; (void)n; (void)tries;
		return 0;
	}
#else
	for (;;) {
		n = read(fd, line, sizeof(line) - 1);
		if (n > 0)
			break;
		if (n == 0) {                       /* EOF: driver has no data */
			sms_log("SENSOR", "WARN read() returned EOF");
			return -1;
		}
		if (errno == EINTR) {               /* interrupted: retry unless stopping */
			if (atomic_load(&g_stop))
				return -1;
			continue;
		}
		if (errno == EAGAIN && ++tries < 5) {   /* driver not ready: back off */
			usleep(2000);
			continue;
		}
		sms_log("SENSOR", "ERROR read(): %s", strerror(errno));
		return -1;
	}
	line[n] = '\0';
	if (sscanf(line, "%lld,%lf,%lf", &out->ts_ms, &out->temperature, &out->humidity) != 3) {
		sms_log("SENSOR", "ERROR bad CSV line: %.60s", line);
		return -1;
	}
	return 0;
#endif
}

void *sensor_thread(void *arg)
{
	SensorCtx *ctx = arg;
	const long long period_ns = (long long)ctx->cfg->interval_ms * 1000000LL;
	struct timespec next, now;
	long long expected_ns, actual_ns;

	pthread_setname_np(pthread_self(), "sms_sensor");
	sms_log("SENSOR", "started, interval=%d ms", ctx->cfg->interval_ms);

	clock_gettime(CLOCK_MONOTONIC, &next);
	expected_ns = ts_ns(&next) + period_ns;
	while (!atomic_load(&g_stop)) {
		next.tv_sec  = expected_ns / NSEC_PER_SEC;
		next.tv_nsec = expected_ns % NSEC_PER_SEC;
		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) != 0)
			continue;           /* EINTR (signal): re-check g_stop, sleep again */

		clock_gettime(CLOCK_MONOTONIC, &now);
		actual_ns = ts_ns(&now);
		if (g_jitter_n < g_jitter_cap)
			g_jitter[g_jitter_n++] = (actual_ns - expected_ns) / 1000;

		{
			SensorData s;

			if (read_sample(ctx->fd, &s) == 0) {
				atomic_fetch_add(&g_stats.samples_read, 1);
				if (cb_push(ctx->buf, &s) == CB_FULL)
					sms_log("SENSOR", "WARN buffer full, sample dropped (ts=%lld)", s.ts_ms);
			} else {
				atomic_fetch_add(&g_stats.read_errors, 1);
			}
		}

		expected_ns += period_ns;
		clock_gettime(CLOCK_MONOTONIC, &now);
		while (ts_ns(&now) > expected_ns + period_ns) {   /* overran whole periods */
			atomic_fetch_add(&g_stats.missed_deadlines, 1);
			expected_ns += period_ns;
		}
	}
	sms_log("SENSOR", "stopped");
	atomic_store(&g_sensor_done, 1);
	cb_close(ctx->buf);
	return NULL;
}
