// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __THREADPOOL_STAT_H__
#define __THREADPOOL_STAT_H__

#include "threadpool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

typedef struct
{
    const char          *name;      /* 线程池名字 */
    uint32_t            total;      /* 线程池中的线程数 */
    const uint32_t     *clist;      /* 每个线程上的任务数 */
}tp_info_t;

void threadpool_get_info(threadpool_t *pool, tp_info_t *info);

/*************************************************************************
*************************************************************************/

void tpstat_register    (const char *name, threadpool_t *tp);
void tpstat_unregister(const char *name);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif