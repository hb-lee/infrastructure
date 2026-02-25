// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __TASK_H__
#define __TASK_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

typedef struct taskset tset_t;
typedef struct taskjob tjob_t;

/*************************************************************************
*************************************************************************/

void taskset_process(uint32_t   depth,
                    void        *task,
                    void        (*task_done)(int ret, void *task),
                    tjob_t      *(*job_fetch)(tset_t *set),
                    void        (*job_handle)(tjob_t *job),
                    void        (*job_release)(tjob_t *job));

void *taskset_task(tset_t *set);

tjob_t *taskjob_init(tset_t *set, void *job);

void taskjob_fini(tjob_t *tjob, int ret);

void *taskjob_job(tjob_t *tjob);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif