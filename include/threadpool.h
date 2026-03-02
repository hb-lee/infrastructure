// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

#define THD_NAME        10

/*************************************************************************
*************************************************************************/

typedef struct threadpool threadpool_t;
typedef struct threadraw threadraw_t;
typedef void (*work_func)(void *args);

/*************************************************************************
*************************************************************************/

threadraw_t *threadraw_create       (const char *name,
                                    void        *args,
                                    work_func   func,
                                    void        (*cleanup)(void *args),
                                    int         (*need_sleep)(void *args));

void        threadraw_destroy       (threadraw_t    *thread);
void        threadraw_wakeup        (threadraw_t    *thread);

uint32_t    threadcount_recommend   (void);

threadpool_t    *threadpool_create      (const char *name,
                                        uint32_t    threads,
                                        int         numaid);

void        threadpool_destroy      (threadpool_t *pool);

void        threadpool_submit       (threadpool_t   *pool,
                                    void            *args,
                                    work_func       func);

void        threadpool_seed_submit  (threadpool_t   *pool,
                                    uint32_t        seed,
                                    void            *args,
                                    work_func       func);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
