/*
 * logger_thread.h - Logger Thread: log_queue -> /tmp/sms.log with rotation
 * Author : Ha Anh Huy - SE203732
 * Date   : 2026-10-18
 */
#ifndef LOGGER_THREAD_H
#define LOGGER_THREAD_H

#include "sms_common.h"

void *logger_thread(void *arg);   /* arg = const SmsConfig * */

#endif
