// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __LOG_H__
#define __LOG_H__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

typedef enum
{
    LOG_LEVEL_DEBUG = 10,
    LOG_LEVEL_INFO = 20,
    LOG_LEVEL_WARN = 30,
    LOG_LEVEL_ERROR = 40,
    LOG_LEVEL_FATAL = 50,
}log_level_t;

typedef enum
{
    LOG_DEFAULT = 0,
    LOG_DELETE,
    LOG_OP,
    LOG_RECORD,
    LOG_REST,
    LOG_BUTT,
}log_type_t;

/*************************************************************************
*************************************************************************/

#define log_fatal(format, ...)                                      \
    log_print(LOG_DEFAULT,                                          \
                __FILE__, sizeof(__FILE__) - 1,                     \
                __func__, sizeof(__func__) - 1,                     \
                __LINE__, LOG_LEVEL_FATAL, format, ##__VA_ARGS__)

#define log_error(format, ...)                                      \
    log_print(LOG_DEFAULT,                                          \
                __FILE__, sizeof(__FILE__) - 1,                     \
                __func__, sizeof(__func__) - 1,                     \
                __LINE__, LOG_LEVEL_ERROR, format, ##__VA_ARGS__)

#define log_warn(format, ...)                                      \
    log_print(LOG_DEFAULT,                                          \
                __FILE__, sizeof(__FILE__) - 1,                     \
                __func__, sizeof(__func__) - 1,                     \
                __LINE__, LOG_LEVEL_WARN, format, ##__VA_ARGS__)

#define log_info(format, ...)                                      \
    log_print(LOG_DEFAULT,                                          \
                __FILE__, sizeof(__FILE__) - 1,                     \
                __func__, sizeof(__func__) - 1,                     \
                __LINE__, LOG_LEVEL_INFO, format, ##__VA_ARGS__)

#ifdef _DEBUG

#define log_debug(format, ...)                                      \
    log_print(LOG_DEFAULT,                                          \
                __FILE__, sizeof(__FILE__) - 1,                     \
                __func__, sizeof(__func__) - 1,                     \
                __LINE__, LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)

#else

#define log_debug(format, ...)

#endif

/*************************************************************************
*************************************************************************/

#define log_special(type, level, format, ...)                       \
    log_print(type,                                                 \
                __FILE__, sizeof(__FILE__) - 1,                     \
                __func__, sizeof(__func__) - 1,                     \
                __LINE__, level, format, ##__VA_ARGS__)

/*************************************************************************
*************************************************************************/

int log_prepare(const char *config);
void    log_print   (log_type_t type,
                    const char *file, unsigned long filelen,
                    const char *func, unsigned long funclen,
                    long line, log_level_t level,
                    const char *format, ...);

void    vlog_print  (log_type_t type,
                    const char *file, unsigned long filelen,
                    const char *func, unsigned long funclen,
                    long line, log_level_t level,
                    const char *format, va_list args);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif