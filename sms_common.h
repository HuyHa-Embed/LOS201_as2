/*
 * sms_common.h - Types, configuration and globals shared by all SMS modules
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 */
#ifndef SMS_COMMON_H
#define SMS_COMMON_H

#include <stdatomic.h>
#include <signal.h>
#include <mqueue.h>
#include <stddef.h>

/* ---- Paths and names (no hard-coded paths elsewhere; override with options) ---- */
#define SMS_DEVICE_PATH      "/dev/sms_sensor"
#define SMS_LOG_PATH         "/tmp/sms.log"
#define SMS_ALARM_QUEUE_NAME "/sms_alarm_queue"
#define SMS_LOG_QUEUE_NAME   "/sms_log_queue"

/* ---- Defaults from the assignment ---- */
#define SMS_DEFAULT_INTERVAL_MS  500
#define SMS_DEFAULT_TEMP_THR     40.0
#define SMS_DEFAULT_HUM_THR      80.0
#define SMS_DEFAULT_DURATION_S   60
#define SMS_TEMP_SPIKE_DELTA     5.0
#define SMS_ROLLING_WINDOW       10
#define SMS_BUFFER_CAPACITY      64
#define SMS_RT_PRIORITY          50
#define SMS_THREAD_STACK_BYTES   (256 * 1024)  /* small stacks: mlockall(MCL_FUTURE) locks them */
#define SMS_LOG_MAX_BYTES        (1024 * 1024)
#define SMS_LOG_FLUSH_SEC        5
#define SMS_MQ_MAXMSG            10            /* default fs.mqueue.msg_max */
#define SMS_QUEUE_TIMEOUT_MS     200

#define SMS_LOG_TEXT_MAX         200

typedef struct {
	long long ts_ms;        /* driver timestamp (ms, CLOCK_MONOTONIC) */
	double    temperature;  /* degrees C */
	double    humidity;     /* percent   */
} SensorData;

typedef enum { ALERT_TEMP_HIGH = 0, ALERT_HUMID_HIGH, ALERT_TEMP_SPIKE, ALERT_TYPE_COUNT } AlertType;

typedef struct {
	int       type;         /* AlertType */
	long long ts_ms;
	double    value;
	double    threshold;
} AlertMsg;

typedef struct {
	char text[SMS_LOG_TEXT_MAX];
} LogMsg;

typedef enum { SCHED_MODE_FIFO = 0, SCHED_MODE_OTHER } SchedMode;

typedef struct {
	int         interval_ms;
	double      temp_thr;
	double      hum_thr;
	int         duration_s;     /* 0 = infinite */
	int         verbose;
	SchedMode   sched_mode;
	int         rt_priority;
	const char *device_path;
	const char *log_path;
	const char *jitter_path;    /* NULL = do not write a jitter report */
	long        log_max_bytes;
} SmsConfig;

/* ---- Global run state ---- */
extern atomic_int g_stop;                /* SIGINT/SIGTERM or duration elapsed (atomic: shared by all threads) */
extern atomic_int g_sensor_done;         /* upstream-finished flags drive the  */
extern atomic_int g_proc_done;           /* ordered shutdown of the pipeline:  */
extern atomic_int g_alert_done;          /* sensor -> processor -> alert -> logger */
extern atomic_int g_logger_stop;
extern mqd_t g_alarm_q;
extern mqd_t g_log_q;

typedef struct {
	atomic_long samples_read;
	atomic_long samples_processed;
	atomic_long alerts[ALERT_TYPE_COUNT];
	atomic_long read_errors;
	atomic_long missed_deadlines;
} SmsStats;
extern SmsStats g_stats;

/* Send a formatted message to the logger thread (never blocks > 1 s). */
void sms_log(const char *src, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

long long sms_now_ms(void);   /* CLOCK_MONOTONIC in ms */

#endif /* SMS_COMMON_H */
