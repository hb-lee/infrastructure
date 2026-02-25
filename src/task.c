// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "task.h"

#include "atomic.h"
#include "list.h"
#include "sysdef.h"
#include "spinlock.h"
#include "log.h"

#include <errno.h>

/*************************************************************************
*************************************************************************/

#define _MAX_DEPTH      128U
#define _MIN_DEPTH      4U

enum job_state
{
    JOB_INIT    = 0,    /* 初始化状态 */
    JOB_DONE    = 1,    /* 完成状态 */
    JOB_FAIL    = 2     /* 失败状态 */
};

/*************************************************************************
*************************************************************************/

struct taskset
{
    void            *task;      /* 实际任务集 */
    int             retcode;    /* 任务集返回值 */
    int32_t         ref;        /* 引用计数 */

    spinlock_t      lock;       /* 互斥锁 */
    bool            done;       /* 标识是否完成 */
    bool            run;        /* 标识是否启动 */
    bool            no_jobs;    /* 标识子任务是否取空 */

    uint32_t        max_depth;  /* 子任务最大并发度 */
    uint32_t        cur_depth;  /* 子任务当前并发度 */

    list_head_t     job_list;   /* 待处理的子任务列表 */

    struct
    {
        spinlock_t  lock;
        list_head_t free_list;
    }mem;

    struct                      /* 外部注册的任务集函数 */
    {
        void        (*task_done)(int ret, void *task);
        tjob_t      *(*job_fetch)(tset_t *set);
        void        (*job_handle)(tjob_t *job);
        void        (*job_release)(tjob_t *job);
    }ops;
};

struct taskjob
{
   void             *job;       /* 实际子任务 */
   int              retcode;    /* 子任务返回码 */

   tset_t           *set;       /* 指向taskset_t */
   int              state;      /* 子任务状态 */
   list_head_t      link;       /* 链接结构 */
};

/*************************************************************************
*************************************************************************/

static inline tjob_t *_job_alloc(tset_t *set)
{
    spinlock_lock(&set->mem.lock);
    if (list_empty(&set->mem.free_list))
    {
        log_error("job memory leak");
        sys_assert(0);

        spinlock_unlock(&set->mem.lock);
        return NULL;
    }

    tjob_t *tjob = container_of(set->mem.free_list.next, tjob_t, link);
    list_del(&tjob->link);
    spinlock_unlock(&set->mem.lock);

    tjob->job = NULL;
    tjob->retcode = 0;
    tjob->set = set;
    tjob->state = JOB_INIT;

    return tjob;
}

static void _job_free(tjob_t *tjob)
{
    tset_t *set = tjob->set;
    if (NULL == set)
    {
        log_error("tjob(%p) is invalid", tjob);
        return;
    }

    spinlock_lock(&set->mem.lock);
    list_add_tail(&tjob->link, &set->mem.free_list);
    spinlock_unlock(&set->mem.lock);
}

static inline void _fetch_job(tset_t *set)
{
    /* 1. 如果任务集已经失败，则不取子任务 */
    if (0 != set->retcode)
    {
        set->no_jobs = true;
        return;
    }

    /* 2. 如果子任务取完，标识无子任务 */
    tjob_t *job = set->ops.job_fetch(set);
    if (NULL == job)
    {
        set->no_jobs = true;
        return;
    }

    /* 3. 将子任务挂接到列表中 */
    job->state = JOB_INIT;
    spinlock_lock(&set->lock);
    list_add_tail(&job->link, &set->job_list);
    atomic_u32_inc(&set->cur_depth);
    spinlock_unlock(&set->lock);
}

static inline tset_t *_taskset_create(uint32_t depth,
                                    void *task,
                                    void (*tdone)(int ret, void *task),
                                    tjob_t *(*job_fetch)(tset_t *set),
                                    void (*job_handle)(tjob_t *job),
                                    void (*job_release)(tjob_t *job))
{
    /* 1. 分配总空间 */
    size_t size = sizeof(tjob_t) * depth + sizeof(tset_t);
    tset_t *set = (tset_t *)calloc(1, size);
    if (NULL == set)
    {
        log_error("alloc taskset failed");
        return NULL;
    }

    /* 2. 初始化set */
    set->ref = 1;
    set->task = task;
    set->max_depth = depth;

    spinlock_init(&set->lock);
    spinlock_init(&set->mem.lock);

    list_init(&set->job_list);
    list_init(&set->mem.free_list);

    set->ops.task_done = tdone;
    set->ops.job_fetch = job_fetch;
    set->ops.job_handle = job_handle;
    set->ops.job_release = job_release;

    /* 3. 配置job空闲内存列表 */
    tjob_t *jobs = (tjob_t *)(set + 1);
    for (uint32_t i = 0; i < depth; i++)
    {
        tjob_t *_job = &jobs[i];
        list_add_tail(&_job->link, &set->mem.free_list);
    }

    return set;
}

static void _taskset_destroy(tset_t *set)
{
    if (NULL != set)
    {
        spinlock_destroy(&set->lock);
        spinlock_destroy(&set->mem.lock);
        free(set);
    }
}

static inline void _taskset_process(tset_t *set)
{
    spinlock_lock(&set->lock);

    /* 1. 如果set正在process，则直接返回即可；否则设置run标识 */
    if (set->run)
    {
        spinlock_unlock(&set->lock);
        return;
    }

    set->run = true;

    /* 2. 依次处理job_list上的job */
    while (!list_empty(&set->job_list))
    {
        tjob_t *job = container_of(set->job_list.next, tjob_t, link);
        list_del(&job->link);
        spinlock_unlock(&set->lock);

        switch (job->state)
        {
            case JOB_INIT:
                set->ops.job_handle(job);
                break;

            case JOB_DONE:
                set->ops.job_release(job);
                _job_free(job);
                atomic_u32_dec(&set->cur_depth);

                _fetch_job(set);
                break;

            case JOB_FAIL:
                set->retcode = job->retcode;
                set->no_jobs = true;

                set->ops.job_release(job);
                _job_free(job);
                atomic_u32_dec(&set->cur_depth);

                break;

            default:
                sys_assert(0);
        }

        spinlock_lock(&set->lock);
    }

    /* 3. 判定任务是否结束 */
    if (set->no_jobs && (0 == atomic_u32_fetch(&set->cur_depth)))
    {
        if (atomic_bool_cas(&set->done, false, true, NULL))
        {
            (void)atomic_s32_dec(&set->ref);
        }
    }

    /* 4. 清除run标识 */
    set->run = false;
    spinlock_unlock(&set->lock);
}

static inline void _taskset_inc(tset_t *set)
{
    (void)atomic_s32_inc(&set->ref);
}

static inline void _taskset_dec(tset_t *set)
{
    int32_t ref = atomic_s32_dec(&set->ref);
    if (0 > ref)
    {
        log_error("set ref(%d) invalid", ref);
        abort();
    }

    if (0 == ref)
    {
        void *task = set->task;
        int _ret = set->retcode;
        void (*task_done)(int, void *) = set->ops.task_done;

        _taskset_destroy(set);
        task_done(_ret, task);
    }
}

/*************************************************************************
*************************************************************************/

void taskset_process(uint32_t       depth,
                        void        *task,
                        void        (*task_done)(int ret, void *task),
                        tjob_t      *(*job_fetch)(tset_t *set),
                        void        (*job_handle)(tjob_t *job),
                        void        (*job_release)(tjob_t *job))
{
    /* 1. 参数判定，task和task_done一定不能为NULL */
    sys_assert(NULL != task);
    sys_assert(NULL != task_done);

    if ((NULL == job_fetch) || (NULL == job_handle))
    {
        log_error("invalid argument, job_fetch or job_handle is NULL");
        task_done(EINVAL, task);
        return;
    }

    depth = (depth < _MIN_DEPTH) ? _MIN_DEPTH : depth;
    depth = (depth > _MAX_DEPTH) ? _MAX_DEPTH : depth;

    /* 2. 创建taskset */
    tset_t *set = _taskset_create(depth, task, task_done, job_fetch, job_handle, job_release);
    if (NULL == set)
    {
        task_done(ENOMEM, task);
        return;
    }

    /* 3. 先按最大深度取job */
    for (uint32_t i = 0; i < set->max_depth; i++)
    {
        _fetch_job(set);
        if (set->no_jobs)
        {
            break;
        }
    }

    /* 4. 启动taskset */
    _taskset_inc(set);
    _taskset_process(set);
    _taskset_dec(set);
}

void *taskset_task(tset_t *set)
{
    return set->task;
}

tjob_t *taskjob_init(tset_t *set, void *job)
{
    tjob_t *tjob = _job_alloc(set);
    if (NULL != tjob)
    {
        tjob->job = job;
    }

    return tjob;
}

void taskjob_fini(tjob_t *tjob, int ret)
{
    if ((NULL == tjob) || (NULL == tjob->set))
    {
        log_error("tjob is invalid");
        return;
    }

    /* 1. 根据返回码设置job状态 */
    tjob->retcode = ret;
    tjob->state = (0 == ret) ? JOB_DONE : JOB_FAIL;

    /* 2. 将job加入set之中，并尝试处理set */
    tset_t *set = tjob->set;
    _taskset_inc(set);
    spinlock_lock(&set->lock);
    list_add_tail(&tjob->link, &set->job_list);
    spinlock_unlock(&set->lock);

    _taskset_process(set);
    _taskset_dec(set);
}

void *taskjob_job(tjob_t *tjob)
{
    return tjob->job;
}
