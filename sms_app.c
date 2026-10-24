/*
 * sms_app.c - Sensor Monitoring System: main(), config, lifecycle, shutdown
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 *
 * State machine:  INIT -> RUNNING -> SHUTDOWN
 *   INIT     : parse args, mlockall, create queues/buffer, open driver, start threads
 *   RUNNING  : main waits for duration or SIGINT/SIGTERM (g_stop)
 *   SHUTDOWN : sensor -> processor -> alert -> logger are stopped in pipeline
 *              order so nothing queued is lost, then resources are released.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <sys/mman.h>
#include "sms_common.h"
#include "circular_buffer.h"
#include "sensor_thread.h"
#include "processor_thread.h"
#include "alert_thread.h"
#include "logger_thread.h"

atomic_int g_stop;
atomic_int g_sensor_done, g_proc_done, g_alert_done, g_logger_stop;
mqd_t g_alarm_q = (mqd_t)-1;
mqd_t g_log_q = (mqd_t)-1;
SmsStats g_stats;

long long sms_now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void sms_log(const char *src, const char *fmt, ...)
{
	LogMsg m;
	struct timespec dl;
	int n;
	va_list ap;

	if (g_log_q == (mqd_t)-1)
		return;
	memset(&m, 0, sizeof(m));       /* no uninitialised bytes sent through the queue */
	n = snprintf(m.text, sizeof(m.text), "[%-6s] ", src);
	va_start(ap, fmt);
	vsnprintf(m.text + n, sizeof(m.text) - (size_t)n, fmt, ap);
	va_end(ap);
	clock_gettime(CLOCK_REALTIME, &dl);
	dl.tv_sec += 1;
	mq_timedsend(g_log_q, (const char *)&m, sizeof(m), 0, &dl);
}

static void on_signal(int sig)
{
	(void)sig;
	atomic_store(&g_stop, 1);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [OPTIONS]\n"
		"  -i <ms>    Sensor sampling interval (default: %d ms)\n"
		"  -t <C>     Temperature alert threshold (default: %.1f)\n"
		"  -h <%%>     Humidity alert threshold (default: %.1f)\n"
		"  -d <sec>   Run duration in seconds, 0 = infinite (default: %d)\n"
		"  -v         Verbose mode: print each sample to stdout\n"
		"Extra options:\n"
		"  -p <fifo|other>  Sensor thread policy (default fifo)\n"
		"  -r <prio>        SCHED_FIFO priority 1-99 (default %d)\n"
		"  -D <path>        Device path (default %s)\n"
		"  -l <path>        Log file (default %s)\n"
		"  -j <path>        Write jitter report to this file\n"
		"  -m <bytes>       Log rotation size (default %d)\n",
		prog, SMS_DEFAULT_INTERVAL_MS, SMS_DEFAULT_TEMP_THR, SMS_DEFAULT_HUM_THR,
		SMS_DEFAULT_DURATION_S, SMS_RT_PRIORITY, SMS_DEVICE_PATH, SMS_LOG_PATH, SMS_LOG_MAX_BYTES);
}

static int parse_args(int argc, char **argv, SmsConfig *c)
{
	int opt;

	*c = (SmsConfig){ SMS_DEFAULT_INTERVAL_MS, SMS_DEFAULT_TEMP_THR, SMS_DEFAULT_HUM_THR,
			  SMS_DEFAULT_DURATION_S, 0, SCHED_MODE_FIFO, SMS_RT_PRIORITY,
			  SMS_DEVICE_PATH, SMS_LOG_PATH, NULL, SMS_LOG_MAX_BYTES };
	while ((opt = getopt(argc, argv, "i:t:h:d:vp:r:D:l:j:m:")) != -1) {
		switch (opt) {
		case 'i': c->interval_ms = atoi(optarg); break;
		case 't': c->temp_thr = atof(optarg); break;
		case 'h': c->hum_thr = atof(optarg); break;
		case 'd': c->duration_s = atoi(optarg); break;
		case 'v': c->verbose = 1; break;
		case 'p':
			if (!strcmp(optarg, "fifo")) c->sched_mode = SCHED_MODE_FIFO;
			else if (!strcmp(optarg, "other")) c->sched_mode = SCHED_MODE_OTHER;
			else return -1;
			break;
		case 'r': c->rt_priority = atoi(optarg); break;
		case 'D': c->device_path = optarg; break;
		case 'l': c->log_path = optarg; break;
		case 'j': c->jitter_path = optarg; break;
		case 'm': c->log_max_bytes = atol(optarg); break;
		default: return -1;
		}
	}
	if (c->interval_ms < 1 || c->interval_ms > 60000 || c->duration_s < 0 ||
	    c->rt_priority < 1 || c->rt_priority > 99 || c->log_max_bytes < 100)
		return -1;
	return 0;
}

static mqd_t open_queue(const char *name, size_t msgsize)
{
	struct mq_attr at = { .mq_flags = 0, .mq_maxmsg = SMS_MQ_MAXMSG, .mq_msgsize = (long)msgsize };

	mq_unlink(name);                /* drop a stale queue from a previous crash */
	return mq_open(name, O_CREAT | O_RDWR, 0600, &at);
}

/* Start the sensor thread with SCHED_FIFO when requested; fall back with a warning. */
static int start_sensor(pthread_t *th, SensorCtx *ctx, const SmsConfig *cfg)
{
	pthread_attr_t at;
	struct sched_param sp = { .sched_priority = cfg->rt_priority };
	int rc;

	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, SMS_THREAD_STACK_BYTES);
	if (cfg->sched_mode == SCHED_MODE_FIFO) {
		pthread_attr_setinheritsched(&at, PTHREAD_EXPLICIT_SCHED);
		pthread_attr_setschedpolicy(&at, SCHED_FIFO);
		pthread_attr_setschedparam(&at, &sp);
		rc = pthread_create(th, &at, sensor_thread, ctx);
		if (rc == 0) {
			fprintf(stderr, "[SMS] sensor thread: SCHED_FIFO priority %d\n", cfg->rt_priority);
			pthread_attr_destroy(&at);
			return 0;
		}
		fprintf(stderr, "[SMS] WARN SCHED_FIFO failed (%s), falling back to SCHED_OTHER\n", strerror(rc));
		pthread_attr_setinheritsched(&at, PTHREAD_INHERIT_SCHED);
	}
	rc = pthread_create(th, &at, sensor_thread, ctx);
	pthread_attr_destroy(&at);
	return rc;
}

static int start_plain(pthread_t *th, void *(*fn)(void *), void *arg)
{
	pthread_attr_t at;
	int rc;

	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, SMS_THREAD_STACK_BYTES);
	rc = pthread_create(th, &at, fn, arg);
	pthread_attr_destroy(&at);
	return rc;
}

int main(int argc, char **argv)
{
	SmsConfig cfg;
	CircularBuffer *buf;
	SensorCtx sctx;
	ProcessorCtx pctx;
	pthread_t t_sensor, t_proc, t_alert, t_log;
	struct sigaction sa;
	sigset_t block, old;
	int fd, exit_code = 0;
	long long t0;

	if (parse_args(argc, argv, &cfg) != 0) {
		usage(argv[0]);
		return 2;
	}

	/* ---- INIT ---- */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;          /* no SA_RESTART: blocking calls return EINTR */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigemptyset(&block);
	sigaddset(&block, SIGINT);
	sigaddset(&block, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &block, &old);   /* workers inherit: only main gets signals */

	if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
		fprintf(stderr, "[SMS] WARN mlockall failed: %s\n", strerror(errno));

	g_alarm_q = open_queue(SMS_ALARM_QUEUE_NAME, sizeof(AlertMsg));
	g_log_q   = open_queue(SMS_LOG_QUEUE_NAME, sizeof(LogMsg));
	if (g_alarm_q == (mqd_t)-1 || g_log_q == (mqd_t)-1) {
		fprintf(stderr, "[SMS] ERROR mq_open: %s\n", strerror(errno));
		return 1;
	}
	buf = cb_create(SMS_BUFFER_CAPACITY);
	if (!buf) {
		fprintf(stderr, "[SMS] ERROR out of memory\n");
		return 1;
	}
	{
		size_t n = cfg.duration_s ? (size_t)cfg.duration_s * 1000 / (size_t)cfg.interval_ms + 16 : 100000;

		if (n > 200000)
			n = 200000;
		if (jitter_init(n) != 0)
			fprintf(stderr, "[SMS] WARN no memory for jitter samples\n");
	}

	fd = sensor_open(&cfg);
	if (fd == -1) {
		fprintf(stderr, "[SMS] ERROR cannot open %s: %s\n", cfg.device_path, strerror(errno));
		exit_code = 1;
		goto cleanup_early;
	}

	sctx = (SensorCtx){ &cfg, buf, fd };
	pctx = (ProcessorCtx){ &cfg, buf };
	if (start_plain(&t_log, logger_thread, &cfg) ||
	    start_plain(&t_alert, alert_thread, NULL) ||
	    start_plain(&t_proc, processor_thread, &pctx) ||
	    start_sensor(&t_sensor, &sctx, &cfg)) {
		fprintf(stderr, "[SMS] ERROR cannot create threads\n");
		atomic_store(&g_stop, 1);
		exit_code = 1;
		goto cleanup_early;
	}
	pthread_sigmask(SIG_SETMASK, &old, NULL);       /* main thread now receives SIGINT */
	sms_log("MAIN", "RUNNING interval=%dms tempThr=%.1f humThr=%.1f duration=%ds",
		cfg.interval_ms, cfg.temp_thr, cfg.hum_thr, cfg.duration_s);
	fprintf(stderr, "[SMS] RUNNING pid=%d (Ctrl+C to stop)\n", getpid());

	/* ---- RUNNING ---- */
	t0 = sms_now_ms();
	while (!atomic_load(&g_stop)) {
		struct timespec ts = { 0, 100 * 1000000L };

		nanosleep(&ts, NULL);
		if (cfg.duration_s && sms_now_ms() - t0 >= (long long)cfg.duration_s * 1000)
			atomic_store(&g_stop, 1);
	}

	/* ---- SHUTDOWN (pipeline order) ---- */
	fprintf(stderr, "[SMS] SHUTDOWN\n");
	pthread_join(t_sensor, NULL);       /* closes the buffer on exit */
	pthread_join(t_proc, NULL);         /* drains the buffer         */
	pthread_join(t_alert, NULL);        /* drains the alarm queue    */
	sms_log("MAIN", "SUMMARY samples=%ld processed=%ld produced=%lu consumed=%lu overflow=%lu "
		"TEMP_HIGH=%ld HUMID_HIGH=%ld TEMP_SPIKE=%ld read_errors=%ld",
		atomic_load(&g_stats.samples_read), atomic_load(&g_stats.samples_processed),
		cb_total_produced(buf), cb_total_consumed(buf), cb_overflow_count(buf),
		atomic_load(&g_stats.alerts[0]), atomic_load(&g_stats.alerts[1]),
		atomic_load(&g_stats.alerts[2]), atomic_load(&g_stats.read_errors));
	atomic_store(&g_logger_stop, 1);
	pthread_join(t_log, NULL);          /* flushes and closes the log */

	printf("=== SMS summary ===\n"
	       "samples read      : %ld\nsamples processed : %ld\n"
	       "buffer produced   : %lu\nbuffer consumed   : %lu\nbuffer overflow   : %lu\n"
	       "alerts TEMP_HIGH  : %ld\nalerts HUMID_HIGH : %ld\nalerts TEMP_SPIKE : %ld\n"
	       "read errors       : %ld\nmissed deadlines  : %ld\n",
	       atomic_load(&g_stats.samples_read), atomic_load(&g_stats.samples_processed),
	       cb_total_produced(buf), cb_total_consumed(buf), cb_overflow_count(buf),
	       atomic_load(&g_stats.alerts[0]), atomic_load(&g_stats.alerts[1]),
	       atomic_load(&g_stats.alerts[2]), atomic_load(&g_stats.read_errors),
	       atomic_load(&g_stats.missed_deadlines));

	if (cfg.jitter_path && jitter_write_report(cfg.jitter_path, &cfg) != 0)
		fprintf(stderr, "[SMS] WARN could not write jitter report\n");
	sensor_close(fd);

cleanup_early:
	cb_destroy(buf);
	jitter_free();
	mq_close(g_alarm_q);
	mq_close(g_log_q);
	mq_unlink(SMS_ALARM_QUEUE_NAME);
	mq_unlink(SMS_LOG_QUEUE_NAME);
	return exit_code;
}
