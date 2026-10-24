/*
 * alert_thread.h - Alert Thread: consumes alarm_queue (POSIX mq)
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 */
#ifndef ALERT_THREAD_H
#define ALERT_THREAD_H

#include "sms_common.h"

void *alert_thread(void *arg);
const char *alert_type_name(int type);

#endif
