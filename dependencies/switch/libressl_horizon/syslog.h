#pragma once
#include <stdarg.h>

#define LOG_EMERG 0
#define LOG_ALERT 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7

#define LOG_PID 0x01
#define LOG_CONS 0x02

#define LOG_KERN (0 << 3)
#define LOG_USER (1 << 3)
#define LOG_DAEMON (3 << 3)
#define LOG_LOCAL2 (18 << 3)

#ifdef __cplusplus
extern "C" {
#endif
void syslog(int priority, const char* format, ...) __attribute__((format(printf, 2, 3)));
void vsyslog(int priority, const char* format, va_list args);
#ifdef __cplusplus
}
#endif
