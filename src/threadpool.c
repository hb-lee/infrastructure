// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "tpstat.h"
#include "atomic.h"
#include "spinlock.h"
#include "list.h"
#include "log.h"
#include "numa.h"

#include "securec.h"
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <sched.h>

/*************************************************************************
*************************************************************************/

#define MIN_THREADS     1U
#define MAX_THREADS     64U

#define MAX_RATIO       2.0
#define MIN_RATIO       1.0
#define DELTA           0.015

#define RETRY           3

#define SEM_POST_MAX    (1024)

/*************************************************************************
*************************************************************************/

typedef struct
{
    void            *args;
    work_func       func;
    list_head_t     link;
}_job_t;

typedef struct
{
    struct
    {
        spinlock_t  lock;

        int         count;
        list_head_t list;
    }wait;

    pthread_t       id;
    bool            is_run;
    sem_t           sem;

    int             numaid;
    uint32_t        jobs;
}_thread_t;

struct threadraw
{
    _thread_t       thd;
    void            *args;
    work_func       func;
    void            (*cleanup)(void *args);
    int             (*need_sleep)(void *args);
};

struct threadpool
{
    char            name[2*THD_NAME];

    uint32_t        index;
    uint32_t        count;
    _thread_t       *threads;
    uint32_t        *joblist;
};

/*************************************************************************
*************************************************************************/

static inline _job_t *_job_malloc()
{
    for (int i = 0; i < RETRY; i++)
    {
        _job_t *job = (_job_t *)malloc(sizeof(_job_t));
        if (NULL != job)
        {
            return job;
        }
    }

    return NULL;
}

static inline void _job_free(_job_t *job)
{
    free(job);
}

static inline void _joblist_cleanup(list_head_t *list)
{
    while (!list_empty(list))
    {
        _job_t *job = container_of(list->next, _job_t, link);
        list_del(&job->link);
        free(job);
    }
}

static inline void _sem_wait_time(struct timespec *ts)
{
    /* 工作线程sem等待时间，毫秒 */
    #define SEM_WAIT_TIME   (100)
    #define NS_PER_MS       (1000 * 1000)
    #define NS_PER_S        (1000 * 1000 * 1000)

    (void)clock_gettime(CLOCK_REALTIME, ts);

    long nsec = ts->tv_nsec + (SEM_WAIT_TIME * NS_PER_MS);
    ts->tv_sec += nsec / NS_PER_S;
    ts->tv_nsec = nsec % NS_PER_S;
}

/*************************************************************************
*************************************************************************/

static void *_threadraw_svc(void *args)
{
    threadraw_t *raw = (threadraw_t *)args;

    while (atomic_bool_cas(&raw->thd.is_run, true, true, NULL))
    {
        while (raw->need_sleep(raw->args))
        {
            struct timespec ts;
            _sem_wait_time(&ts);

            (void)sem_timedwait(&raw->thd.sem, &ts);
            if (atomic_bool_cas(&raw->thd.is_run, false, false, NULL))
            {
                return NULL;
            }
        }

        raw->func(raw->args);
    }

    return NULL;
}

static void *_thread_svc(void *args)
{
    _thread_t *thread = (_thread_t *)args;

    if (thread->numaid >= 0)
    {
        if (numa_available()) {
            log_error("numa bind not support in current system");
            return NULL;
        }
        int nums = numa_max_node();
        if (0 != numa_run_on_node(thread->numaid % nums)) {
            log_error("numa bind failed on %d", thread->numaid % nums);
            return NULL;
        }
    }
    while (atomic_bool_cas(&thread->is_run, true, true, NULL))
    {
        /* 1. 判断线程是否需要睡眠 */
        list_head_t que;
        list_init(&que);

        spinlock_lock(&thread->wait.lock);
        while (0 == thread->wait.count)
        {
            spinlock_unlock(&thread->wait.lock);

            struct timespec ts;
            _sem_wait_time(&ts);

            (void)sem_timedwait(&thread->sem, &ts);

            if (atomic_bool_cas(&thread->is_run, false, false, NULL))
            {
                return NULL;
            }

            spinlock_lock(&thread->wait.lock);
        }

        /* 2. 取出所有任务 */
        list_splice(&thread->wait.list, &que);
        thread->wait.count = 0;

        spinlock_unlock(&thread->wait.lock);

        /* 3. 依次执行任务 */
        while (!list_empty(&que))
        {
            _job_t *job = container_of(que.next, _job_t, link);
            list_del(&job->link);

            work_func func = job->func;
            void *arg = job->args;
            _job_free(job);

            func(arg);
            (void)atomic_u32_dec(&thread->jobs);
        }
    }

    return NULL;
}

static int _thread_start(_thread_t *thread,
                        const char *name,
                        void *args,
                        void *(*svc)(void *args))
{
    /* 1. 初始化thread相关成员 */
    spinlock_init(&thread->wait.lock);
    thread->wait.count = 0;
    list_init(&thread->wait.list);

    thread->is_run = true;
    if (0 != sem_init(&thread->sem, 0, 0))
    {
        log_fatal("sem_init failed, errno=%d", errno);
        spinlock_destroy(&thread->wait.lock);
        return -1;
    }

    /* 2. 启动线程 */
    if (0 != pthread_create(&thread->id, NULL, svc, args))
    {
        log_error("pthread_create failed, errno=%d", errno);
        (void)sem_destroy(&thread->sem);
        spinlock_destroy(&thread->wait.lock);
        return -1;
    }

    /* 3. 为线程设置名字 */
    char _name[THD_NAME + 1] = {0};
    sprintf_s(_name, sizeof(_name), "%.*s", THD_NAME, name);
    (void)pthread_setname_np(thread->id, _name);

    return 0;
}

static void _thread_stop(_thread_t *thread)
{
    /* 此函数非线程安全 */
    /* 1. 等待线程结束 */
    if (atomic_bool_cas(&thread->is_run, true, false, NULL))
    {
        (void)sem_post(&thread->sem);
        (void)pthread_join(thread->id, NULL);
    }

    /* 2. 清空任务 */
    _joblist_cleanup(&thread->wait.list);

    thread->jobs = 0;
    thread->wait.count = 0;
    spinlock_destroy(&thread->wait.lock);
}

static inline void _thread_submit(_thread_t *thread, void *args, work_func func)
{
    _job_t *job = _job_malloc();
    if (NULL == job)
    {
        func(args);
        return;
    }

    job->args = args;
    job->func = func;

    spinlock_lock(&thread->wait.lock);
    list_add_tail(&job->link, &thread->wait.list);
    thread->wait.count++;
    (void)atomic_u32_inc(&thread->jobs);
    spinlock_unlock(&thread->wait.lock);

    int val = 0;
    (void)sem_getvalue(&thread->sem, &val);
    if (val < SEM_POST_MAX)
    {
        (void)sem_post(&thread->sem);
    }
}

/*************************************************************************
*************************************************************************/

threadraw_t *threadraw_create(const char *name,
                            void *args,
                            work_func func,
                            void (*cleanup)(void *args),
                            int (*need_sleep)(void *args))
{
    threadraw_t *raw = (threadraw_t *)malloc(sizeof(threadraw_t));
    if (NULL == raw)
    {
        log_error("malloc threadraw fail");
        return NULL;
    }

    raw->args = args;
    raw->func = func;
    raw->cleanup = cleanup;
    raw->need_sleep = need_sleep;

    if (0 != _thread_start(&raw->thd, name, raw, _threadraw_svc))
    {
        log_error("create thread(%s) fail", name);
        free(raw);
        return NULL;
    }

    return raw;
}

void threadraw_destroy(threadraw_t *raw)
{
    _thread_stop(&raw->thd);
    if (NULL != raw->cleanup)
    {
        raw->cleanup(raw->args);
    }

    free(raw);
}

void threadraw_wakeup(threadraw_t *raw)
{
    int val = 0;
    (void)sem_getvalue(&raw->thd.sem, &val);
    if (val < SEM_POST_MAX)
    {
        (void)sem_post(&raw->thd.sem);
    }
}

uint32_t threadcount_recommend()
{
    long cpu = sysconf(_SC_NPROCESSORS_CONF);
    if (cpu < 0)
    {
        log_warn("thread: get cpu count failed, ret=%ld", cpu);
        return 4U;
    }

    float ratio = (float)(MAX_RATIO - cpu * DELTA);
    if (ratio < MIN_RATIO)
    {
        ratio = MIN_RATIO;
    }

    return (uint32_t)(cpu * ratio);
}

threadpool_t *threadpool_create(const char *name, unsigned int threads, int nid)
{
    /* 1. 参数调节及内存申请 */
    uint32_t count = threads;
    count = (count < MIN_THREADS) ? MIN_THREADS : count;
    count = (count > MAX_THREADS) ? MAX_THREADS : count;

    if (nid >= 0 && numa_available() < 0)
    {
        log_error("numa bind not support in current system");
        return NULL;
    }
    size_t mem_size = sizeof(threadpool_t) + count * sizeof(_thread_t)
                    + count * sizeof(uint32_t);
    char *ptr = (char *)calloc(1, mem_size);
    if (NULL == ptr)
    {
        log_error("malloc threadpool_t failed");
        return NULL;
    }

    threadpool_t *pool = (threadpool_t *)ptr;
    ptr += sizeof(threadpool_t);

    pool->threads = (_thread_t *)ptr;
    ptr += count * sizeof(_thread_t);

    pool->joblist = (uint32_t *)ptr;

    /* 2. 依次创建工作线程 */
    sprintf_s(pool->name, sizeof(pool->name), "%.12s", name);

    uint32_t i = 0;
    for (; i < count; i++)
    {
        char _name[THD_NAME * 2] = {0};
        sprintf_s(_name, sizeof(_name), "%.8s%d", name, i);
        pool->threads[i].numaid = nid;
        if (0 != _thread_start(&pool->threads[i], _name,
                                &pool->threads[i], _thread_svc))
        {
            log_error("start thread(%s), failed", _name);
            break;
        }
    }

    /* 3. 销毁已经创建的线程，释放相关空间并返回NULL */
    if (i != count)
    {
        for (uint32_t j = 0; j < i; j++)
        {
            _thread_stop(&pool->threads[j]);
        }

        free(pool);
        return NULL;
    }

    /* 4. 创建成功 */
    pool->index = 0U;
    pool->count = count;

    tpstat_register(pool->name, pool);
    return pool;
}

void threadpool_destroy(threadpool_t *pool)
{
    if (NULL == pool)
    {
        return;
    }

    tpstat_unregister(pool->name);

    for (uint32_t i = 0; i < pool->count; i++)
    {
        _thread_stop(&pool->threads[i]);
    }

    free(pool);
}

void threadpool_submit(threadpool_t *pool, void *args, work_func func)
{
    uint32_t index = pool->index++;
    index %= pool->count;
    _thread_submit(&pool->threads[index], args, func);
}

void threadpool_seed_submit(threadpool_t *pool,
                            uint32_t seed,
                            void *args,
                            work_func func)
{
    _thread_submit(&pool->threads[seed % pool->count], args, func);
}

void threadpool_get_info(threadpool_t *pool, tp_info_t *info)
{
    info->name = pool->name;
    info->total = pool->count;
    info->clist = pool->joblist;

    for (uint32_t i = 0; i < pool->count; i++)
    {
        pool->joblist[i] = pool->threads[i].jobs;
    }
}
