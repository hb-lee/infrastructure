// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include <zlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "log.h"

/*************************************************************************
*************************************************************************/

static const char * const g_logname[LOG_BUTT] =
{
    [LOG_DEFAULT] = "default",
    [LOG_DELETE] = "del_log",
    [LOG_OP] = "op_log",
    [LOG_RECORD] = "nfs_record",
    [LOG_REST] = "rest_op"
};

static zlog_category_t *g_logger[LOG_BUTT];

/*************************************************************************
*************************************************************************/

__attribute__((constructor)) void _log_init()
{
    const char *cname = "init";

    /* zlog的初始化函数不可重入，这里判断逻辑如下：
     * 先get_category，如果返回为NULL，则没有初始化或内存失败（基本不可能）
     * 再zlog_init完成初始化 */
    if (NULL != zlog_get_category(cname))
    {
        return;
    }

    /* 配置文件路径传入NULL，zlog的日志会打印到标准输出 */
    int ret = dzlog_init(NULL, cname);
    if (0 != ret)
    {
        fprintf(stderr, "[%s:%d] log_init failed, ret=%d\n",
                    __FUNCTION__, __LINE__, ret);
        abort();
    }
}

__attribute__((destructor)) void _log_exit()
{
    /* zlog的注销函数是可以重入的，此处不需要重入保护 */
    zlog_fini();

    for (int i = LOG_DEFAULT; i < LOG_BUTT; i++)
    {
        g_logger[i] = NULL;
    }
}

int log_prepare(const char *config)
{
    int ret = zlog_reload(config);
    if (0 != ret)
    {
        fprintf(stderr, "[%s:%d] log reload failed, ret=%d, log_cfg_path=%s\n",
                        __FUNCTION__, __LINE__, ret, config);
        return -1;
    }

    ret = dzlog_set_category("default");
    if (0 != ret)
    {
        fprintf(stderr, "[%s:%d] log set default category failed, ret=%d\n",
                        __FUNCTION__, __LINE__, ret);
        return -1;
    }

    for (int i = LOG_DEFAULT; i < LOG_BUTT; i++)
    {
        g_logger[i] = zlog_get_category(g_logname[i]);
        if (NULL == g_logger[i])
        {
            fprintf(stderr, "[%s:%d] log get category failed, cname=%s\n",
                            __FUNCTION__, __LINE__, g_logname[i]);
            return -1;
        }
    }

    return 0;
}

/*************************************************************************
*************************************************************************/

void log_print(log_type_t type,
                const char *file, unsigned long filelen,
                const char *func, unsigned long funclen,
                long line, log_level_t level,
                const char *format, ...)
{
    int zlevel;

    switch (level)
    {
        case LOG_LEVEL_DEBUG:
            zlevel = ZLOG_LEVEL_DEBUG;
            break;

        case LOG_LEVEL_INFO:
            zlevel = ZLOG_LEVEL_INFO;
            break;

        case LOG_LEVEL_WARN:
            zlevel = ZLOG_LEVEL_WARN;
            break;

        case LOG_LEVEL_ERROR:
            zlevel = ZLOG_LEVEL_ERROR;
            break;

        case LOG_LEVEL_FATAL:
            zlevel = ZLOG_LEVEL_FATAL;
            break;

        default:
            /* 未知的日志级别 */
            return;
    }

    va_list args;
    va_start(args, format);

    zlog_category_t *cat = g_logger[type];
    (NULL == cat) ?
        vdzlog(file, filelen, func, funclen, line, zlevel, format, args) :
        vzlog(cat, file, filelen, func, funclen, line, zlevel, format, args);

    va_end(args);
}

void vlog_print(log_type_t type,
                    const char *file, unsigned long filelen,
                    const char *func, unsigned long funclen,
                    long line, log_level_t level,
                    const char *format, va_list args)
{
    int zlevel;

    switch(level)
    {
        case LOG_LEVEL_DEBUG:
            zlevel = ZLOG_LEVEL_DEBUG;
            break;

        case LOG_LEVEL_INFO:
            zlevel = ZLOG_LEVEL_INFO;
            break;

        case LOG_LEVEL_WARN:
            zlevel = ZLOG_LEVEL_WARN;
            break;

        case LOG_LEVEL_ERROR:
            zlevel = ZLOG_LEVEL_ERROR;
            break;

        case LOG_LEVEL_FATAL:
            zlevel = ZLOG_LEVEL_FATAL;
            break;

        default:
            /* 未知的日志级别 */
            return;
    }

    zlog_category_t *cat = g_logger[type];
    (NULL == cat) ?
        vdzlog(file, filelen, func, funclen, line, zlevel, format, args) :
        vzlog(cat, file, filelen, func, funclen, line, zlevel, format, args);
}
