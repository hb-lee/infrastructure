// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "costat.h"
#include "threadpool.h"
#include "spinlock.h"
#include "atomic.h"
#include "stimer.h"
#include "mempool.h"
#include "sysdef.h"
#include "sema.h"
#include "list.h"
#include "log.h"

#include "securec.h"
#include <ucontext.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>

/*************************************************************************
*************************************************************************/

#define CLEN_MAX        256
#define MIN_LWT         16
#define MIN_WORKER      1

#define LWT_BEGIN(_mgr, _op, _ts)                       \
    _lwt_begin(&(_mgr)->info->lwt.op[_op], _ts)

#define LWT_END(_mgr, _op, _ts)                         \
    _lwt_end(&(_mgr)->info->lwt.op[_op], _ts)

/*************************************************************************
*************************************************************************/

typedef struct _worker _worker_t;
typedef struct _sleeper _sleeper_t;
typedef struct _lwt _lwt_t;

typedef struct
{
    _lwt_t          *lwt;
    int32_t         val;
    int             ret;
    uint64_t        ts;
    list_head_t     link;
}_cosem_t;

struct _worker
{
    ucontext_t          ctx;
    comgr_t             *mgr;

    threadraw_t         *thread;

    spinlock_t          lock;
    struct
    {
        int32_t         count;
        int32_t         wait;
        list_head_t     head;
    }lwt;

    struct
    {
        int32_t         count;
        list_head_t     head;
    }sem;

    uint64_t            ts;
    bool                swapped;
};

struct _sleeper
{
    _lwt_t              *lwt;
    uint32_t            timeout;
    list_head_t         link;
};

struct _lwt
{
    list_head_t         link;
    ucontext_t          ctx;

    void                *args;
    coroutine_func      func;
    coroutine_func      fini;

    uint64_t            ts;
    _worker_t           *worker;
};

struct coroutine_mgr
{
    char                *name;
    mempool_t           *mem;
    uint32_t            stack_size;

    struct
    {
        stimer_t        *timer;
        spinlock_t      lock;
        list_head_t     list;
    }sleeper;

    struct
    {
        uint32_t        idx;
        uint32_t        count;
        _worker_t       *list;
    }worker;

    coinfo_t            *info;
};

/*************************************************************************
*************************************************************************/

static __thread _lwt_t *lwt_curr = NULL;

/*************************************************************************
*************************************************************************/

static inline void _lwt_begin(lwtop_t *op, uint64_t *ts)
{
    struct timespec now = {0};
    (void)clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
    *ts = ((uint64_t)now.tv_sec * 1000000000UL + (uint64_t)now.tv_nsec);

    atomic_u64_inc(&op->begin);
}

static inline void _lwt_end(lwtop_t *op, uint64_t start)
{
    struct timespec now = {0};
    (void)clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
    uint64_t end = ((uint64_t)now.tv_sec * 1000000000UL + (uint64_t)now.tv_nsec);
    if (end < start)
    {
        end = start;
    }

    atomic_u64_inc(&op->end);

    uint64_t cost = (end - start) / 1000;
    atomic_u64_add(&op->delay, cost);
    if (cost > op->max)
    {
        op->max = cost;
    }
}

/*************************************************************************
*************************************************************************/

static void _timer_svc(void *args)
{
    comgr_t *mgr = (comgr_t *)args;
    spinlock_lock(&mgr->sleeper.lock);
    do
    {
        /* 1. 如果队列为空直接返回 */
        if (list_empty(&mgr->sleeper.list))
        {
            break;
        }

        /* 2. 判断队头元素是否可唤醒，如果不能唤醒则返回 */
        _sleeper_t *sleeper = container_of(mgr->sleeper.list.next, _sleeper_t, link);
        if (0 != sleeper->timeout)
        {
            sleeper->timeout--;
        }

        if (sleeper->timeout > 0)
        {
            break;
        }

        /* 3. 从头部开始遍历，唤醒所有能够唤醒的元素 */
        list_head_t *curr;
        list_head_t *next;
        list_foreach_safe(curr, next, &mgr->sleeper.list)
        {
            _sleeper_t *_slp = container_of(curr, _sleeper_t, link);
            if (0 != _slp->timeout)
            {
                break;
            }

            list_del(&_slp->link);

            _worker_t *worker  = _slp->lwt->worker;
            spinlock_lock(&worker->lock);
            LWT_BEGIN(mgr, LwtQue, &_slp->lwt->ts);
            list_add(&_slp->lwt->link, &worker->lwt.head);
            ++(worker->lwt.wait);
            spinlock_unlock(&worker->lock);
            threadraw_wakeup(worker->thread);
        }
    } while(0);
    spinlock_unlock(&mgr->sleeper.lock);
}

static void _timer_cleanup(comgr_t *mgr)
{
    list_head_t *curr;
    list_head_t *next;
    list_foreach_safe(curr, next, &mgr->sleeper.list)
    {
        _sleeper_t *_slp = container_of(curr, _sleeper_t, link);
        list_del(&_slp->link);

        _worker_t *worker = _slp->lwt->worker;
        spinlock_lock(&worker->lock);
        LWT_BEGIN(mgr, LwtQue, &_slp->lwt->ts);
        list_add(&_slp->lwt->link, &worker->lwt.head);
        ++(worker->lwt.wait);
        spinlock_unlock(&worker->lock);
    }
}

static inline _worker_t *_choose_worker(comgr_t *mgr)
{
    uint32_t idx = ++(mgr->worker.idx);
    return &mgr->worker.list[idx % mgr->worker.count];
}

/*************************************************************************
*************************************************************************/

static void _worker_svc(void *args)
{
    _worker_t *worker = (_worker_t *)args;

    list_head_t lwt_head;
    list_init(&lwt_head);

    spinlock_lock(&worker->lock);
    list_splice(&worker->lwt.head, &lwt_head);
    worker->lwt.wait = 0;
    spinlock_unlock(&worker->lock);

    while (!list_empty(&lwt_head))
    {
        /* 取出lwt，调度执行 */
        _lwt_t *lwt = container_of(lwt_head.next, _lwt_t, link);
        list_del(&lwt->link);
        LWT_END(worker->mgr, LwtQue, lwt->ts);

        lwt_curr = lwt;

        worker->swapped = false;
        LWT_BEGIN(worker->mgr, LwtRun, &worker->ts);
        if (0 != swapcontext(&worker->ctx, &lwt->ctx))
        {
            log_error("swapcontext fail, err(%s)", strerror(errno));
        }

        if (worker->swapped)
        {
            LWT_END(worker->mgr, LwtSche, worker->ts);
            continue;
        }

        LWT_END(worker->mgr, LwtRun, worker->ts);

        /* 释放lwt空间之后调用外部传入的finish方法 */
        void *_args = lwt->args;
        coroutine_func _fini = lwt->fini;
        mempool_free(worker->mgr->mem, lwt);
        if (NULL != _fini)
        {
            _fini(_args);
        }

        (void)atomic_s32_dec(&worker->lwt.count);
    }

    lwt_curr = NULL;
}

static void _worker_cleanup(void *args)
{
    _worker_t *worker = (_worker_t *)args;

    /* 1. 清空未调度的lwt */
    spinlock_lock(&worker->lock);
    worker->lwt.wait = 0;
    while (!list_empty(&worker->lwt.head))
    {
        _lwt_t *lwt = container_of(worker->lwt.head.next, _lwt_t, link);
        list_del(&lwt->link);
        mempool_free(worker->mgr->mem, lwt);
        (void)atomic_s32_dec(&worker->lwt.count);
    }

    /* 2. 唤醒所有的信号量，返回失败 */
    while (!list_empty(&worker->sem.head))
    {
        _cosem_t *sem = container_of(worker->sem.head.next, _cosem_t, link);
        list_del(&sem->link);
        --(worker->sem.count);
        spinlock_unlock(&worker->lock);
        sem->ret = -1;
        if (0 != swapcontext(&worker->ctx, &sem->lwt->ctx))
        {
            log_error("swapcontext fail, err(%s)", strerror(errno));
        }

        spinlock_lock(&worker->lock);
    }

    spinlock_unlock(&worker->lock);
}

static int _worker_need_sleep(void *args)
{
    _worker_t *worker = (_worker_t *)args;
    return (0 == worker->lwt.wait) ? 1 : 0;
}

static int _worker_init(comgr_t *mgr)
{
    mgr->worker.list = (_worker_t *)calloc(mgr->worker.count, sizeof(_worker_t));
    if (NULL == mgr->worker.list)
    {
        log_error("calloc fail");
        return -1;
    }

    uint32_t i = 0;
    for (; i < mgr->worker.count; i++)
    {
        char name[CLEN_MAX * 2] = {0};
        sprintf_s(name, sizeof(name), "%.8s%d", mgr->name, i);
        _worker_t *worker = &mgr->worker.list[i];
        worker->thread = threadraw_create(name,
                                        worker,
                                        _worker_svc,
                                        _worker_cleanup,
                                        _worker_need_sleep);
        if (NULL == worker->thread)
        {
            log_error("threadraw_create fail");
            break;
        }

        worker->ts = 0;
        worker->mgr = mgr;
        worker->lwt.count = 0;
        worker->lwt.wait = 0;
        list_init(&worker->lwt.head);

        worker->sem.count = 0;
        list_init(&worker->sem.head);

        spinlock_init(&worker->lock);
    }

    if (mgr->worker.count != i)
    {
        for (uint32_t j = 0; j < i; j++)
        {
            threadraw_destroy(mgr->worker.list[j].thread);
        }

        free(mgr->worker.list);
        mgr->worker.list = NULL;
        return -1;
    }

    return 0;
}

static void _worker_fini(comgr_t *mgr)
{
    for (uint32_t i = 0; i < mgr->worker.count; i++)
    {
        threadraw_destroy(mgr->worker.list[i].thread);
        spinlock_destroy(&mgr->worker.list[i].lock);
    }

    free(mgr->worker.list);
    mgr->worker.list = NULL;
}

static void _lwt_func(uint32_t low, uint32_t hig)
{
    uintptr_t ptr = (uintptr_t)low | ((uintptr_t)hig << 32);
    _lwt_t *lwt = (_lwt_t *)ptr;

    lwt->func(lwt->args);
}

static inline void _comgr_cleanup(comgr_t *mgr)
{
    _timer_cleanup(mgr);
    if (NULL != mgr->sleeper.timer)
    {
        stimer_destroy(mgr->sleeper.timer);
    }

    if (NULL != mgr->worker.list)
    {
        _worker_fini(mgr);
    }

    if (NULL != mgr->mem)
    {
        mempool_destroy(mgr->mem);
    }

    if (NULL != mgr->info)
    {
        free(mgr->info);
    }

    if (NULL != mgr->name)
    {
        free(mgr->name);
    }
}

/*************************************************************************
*************************************************************************/

int cosem_special(void)
{
    return (NULL == lwt_curr) ? 0 : 1;
}

int cosem_init(void *sem)
{
    if (NULL == lwt_curr)
    {
        log_error("not coroutine context");
        return -1;
    }

    _cosem_t *cosem = (_cosem_t *)sem;

    cosem->lwt = lwt_curr;
    cosem->val = 0;
    cosem->ret = 0;
    list_init(&cosem->link);
    return 0;
}

int cosem_fini(void *sem)
{
    _cosem_t *cosem = (_cosem_t *)sem;
    if (!list_empty(&cosem->link) || (0 != cosem->val))
    {
        log_error("coroutine semaphore is still in use(%d)", cosem->val);
        return -1;
    }

    cosem->lwt = NULL;
    cosem->val = 0;

    return 0;
}

int cosem_up(void *sem)
{
    _cosem_t *cosem = (_cosem_t *)sem;
    if (NULL == cosem->lwt)
    {
        log_error("coroutine semaphore belongs to no lwt");
        return -1;
    }

    _worker_t *worker = cosem->lwt->worker;

    /* 1. 需要在coroutine sem锁形成的互斥区中执行一系列操作 */
    spinlock_lock(&worker->lock);
    {
        /* 1.1 判断是否能够唤醒coroutine sem，只有val为0时才能唤醒 */
        --(cosem->val);
        if (0 != cosem->val)
        {
            spinlock_unlock(&worker->lock);
            return 0;
        }

        /* 1.2 将coroutine sem从worker中的队列移除 */
        LWT_BEGIN(worker->mgr, LwtSemup, &cosem->ts);
        list_del(&cosem->link);
        --(worker->sem.count);

        /* 1.3 将coroutine sem对应的lwt加入worker中的调度队列 */
        LWT_BEGIN(worker->mgr, LwtQue, &cosem->lwt->ts);
        list_add(&cosem->lwt->link, &worker->lwt.head);
        ++(worker->lwt.wait);
    }
    spinlock_unlock(&worker->lock);

    /* 2. 唤醒worker */
    threadraw_wakeup(worker->thread);
    return 0;
}

int cosem_down(void *sem)
{
    _cosem_t *cosem = (_cosem_t *)sem;
    if (NULL == cosem->lwt)
    {
        log_error("coroutine semaphore belongs to no lwt");
        return -1;
    }

    _worker_t *worker = cosem->lwt->worker;
    LWT_END(worker->mgr, LwtRun, worker->ts);

    /* 1. 需要在coroutine sem锁形成的互斥区中执行一系列的操作 */
    spinlock_lock(&worker->lock);
    {
        /* 1.1 如果val值小于等于0， 表明up操作先于down执行，直接返回即可 */
        ++(cosem->val);
        if (0 >= cosem->val)
        {
            spinlock_unlock(&worker->lock);
            return 0;
        }

        /* 1.2 coroutine sem需要加入worker中的相关队列(调测用途) */
        worker->swapped = true;
        LWT_BEGIN(worker->mgr, LwtSche, &worker->ts);
        list_add_tail(&cosem->link, &worker->sem.head);
        ++(worker->sem.count);
    }
    spinlock_unlock(&worker->lock);

    /* 2. 切换调度 */
    if (0 != swapcontext(&cosem->lwt->ctx, &worker->ctx))
    {
        log_error("swapcontext fail, err(%s)", strerror(errno));
    }

    LWT_END(worker->mgr, LwtSemup, cosem->ts);

    return cosem->ret;
}

void cosem_sleep(uint32_t ms)
{
    if (NULL == lwt_curr)
    {
        log_error("not coroutine context");
        return;
    }

    /* 1. 加入超时列表 */
    _sleeper_t sleeper = {lwt_curr, ms, {NULL, NULL}};

    comgr_t *mgr = lwt_curr->worker->mgr;
    spinlock_lock(&mgr->sleeper.lock);
    do
    {
        list_head_t *node;
        list_foreach(node, &mgr->sleeper.list)
        {
            _sleeper_t *_slp = container_of(node, _sleeper_t, link);
            if (_slp->timeout > sleeper.timeout)
            {
                _slp->timeout -= sleeper.timeout;
                list_add(&sleeper.link, _slp->link.prev);
                break;
            }

            sleeper.timeout -= _slp->timeout;
        }

        if (node == &mgr->sleeper.list)
        {
            list_add_tail(&sleeper.link, &mgr->sleeper.list);
        }

    } while(0);
    spinlock_unlock(&mgr->sleeper.lock);

    /* 2. 切换lwt */
    LWT_END(mgr, LwtRun, lwt_curr->worker->ts);
    lwt_curr->worker->swapped = true;

    if (0 != swapcontext(&lwt_curr->ctx, &lwt_curr->worker->ctx))
    {
        log_error("swapcontext fail, err(%s)", strerror(errno));
    }
}

/*************************************************************************
*************************************************************************/

int coroutine_run(comgr_t       *mgr,
                    void        *args,
                    coroutine_func  func,
                    coroutine_func  fini)
{
    _lwt_t *lwt = mempool_alloc(mgr->mem);
    if (NULL == lwt)
    {
        log_error("lwt used up");
        return -1;
    }

    _worker_t *worker = _choose_worker(mgr);

    lwt->args = args;
    lwt->func = func;
    lwt->fini = fini;
    lwt->worker = worker;

    if (0 != getcontext(&lwt->ctx))
    {
        log_error("getcontext fail, err(%s)", strerror(errno));
        mempool_free(mgr->mem, lwt);
        return -1;
    }

    lwt->ctx.uc_stack.ss_sp = (lwt + 1);
    lwt->ctx.uc_stack.ss_size = mgr->stack_size;
    lwt->ctx.uc_link = &worker->ctx;

    uintptr_t ptr = (uintptr_t)lwt;
    makecontext(&lwt->ctx,
                (void  (*)(void))_lwt_func,
                2, (uint32_t)ptr, (uint32_t)(ptr >> 32));

    spinlock_lock(&worker->lock);
    {
        LWT_BEGIN(mgr, LwtQue, &lwt->ts);
        list_add_tail(&lwt->link, &worker->lwt.head);
        worker->lwt.wait++;
        (void)atomic_s32_inc(&worker->lwt.count);
    }
    spinlock_unlock(&worker->lock);

    threadraw_wakeup(worker->thread);
    return 0;
}

void coroutine_yield()
{
    if (NULL == lwt_curr)
    {
        log_error("not coroutine context");
        return;
    }

    _worker_t *worker = lwt_curr->worker;
    LWT_END(worker->mgr, LwtRun, worker->ts);

    spinlock_lock(&worker->lock);
    {
        worker->swapped = true;
        LWT_BEGIN(worker->mgr, LwtSche, &worker->ts);
        list_add_tail(&lwt_curr->link, &worker->lwt.head);
        ++(worker->lwt.wait);
    }
    spinlock_unlock(&worker->lock);

    if (0 != swapcontext(&lwt_curr->ctx, &worker->ctx))
    {
        log_error("swapcontext fail, err(%s)", strerror(errno));
    }
}

comgr_t *comgr_create(const char *name,
                    uint32_t max_lwt,
                    uint32_t max_worker,
                    uint32_t stack_size)
{
    /* 1. 分配mgr内存空间 */
    comgr_t *mgr = (comgr_t *)calloc(1, sizeof(comgr_t));
    if (NULL == mgr)
    {
        log_error("malloc fail");
        return NULL;
    }

    mgr->stack_size = stack_size;

    /* 2. 创建lwt内存池 */
    max_lwt = (max_lwt < MIN_LWT) ? MIN_LWT : max_lwt;
    uint32_t size = (uint32_t)sizeof(_lwt_t) + stack_size;
    mgr->mem = mempool_create(size, max_lwt, NULL);
    if (NULL == mgr->mem)
    {
        log_error("mempool_create fail");
        free(mgr);
        return NULL;
    }

    /* 3. 创建lwt工作线程 */
    mgr->name = strdup(name);
    sys_assert(NULL != mgr->name);

    max_worker = (max_worker < MIN_WORKER) ? MIN_WORKER : max_worker;
    mgr->worker.count = max_worker;
    if (0 != _worker_init(mgr))
    {
        _comgr_cleanup(mgr);
        free(mgr);
        return NULL;
    }

    /* 4. 创建定时器 */
    spinlock_init(&mgr->sleeper.lock);
    list_init(&mgr->sleeper.list);

    char buff[CLEN_MAX] = {0};
    (void)strncpy_s(buff, CLEN_MAX, name, 4);
    (void)strncat_s(buff, CLEN_MAX, "Timer", 6);
    mgr->sleeper.timer = stimer_create(buff, 1U, mgr, _timer_svc);
    if (NULL == mgr->sleeper.timer)
    {
        log_error("stimer_create fail");
        _comgr_cleanup(mgr);
        free(mgr);
        return NULL;
    }

    /* 5. 初始化统计信息 */
    size_t siz = sizeof(coinfo_t) + mgr->worker.count * sizeof(uint32_t);
    mgr->info = (coinfo_t *)calloc(1, siz);
    if (NULL == mgr->info)
    {
        log_error("calloc failed");
        _comgr_cleanup(mgr);
        free(mgr);
        return NULL;
    }

    mgr->info->worker.count = (uint32_t *)(mgr->info + 1);
    mgr->info->worker.total = mgr->worker.count;
    costat_register(mgr->name, mgr);

    return mgr;
}

void comgr_destroy(comgr_t *mgr)
{
    if (NULL == mgr)
    {
        return;
    }

    costat_unregister(mgr->name);
    _comgr_cleanup(mgr);
    free(mgr);
}

const coinfo_t *comgr_getinfo(comgr_t *mgr)
{
    coinfo_t *info = mgr->info;
    for (uint32_t i = 0; i < mgr->worker.count; i++)
    {
        info->worker.count[i] = (uint32_t)mgr->worker.list[i].lwt.count;
    }

    mempool_info_t mem = {0};
    mempool_getinfo(mgr->mem, &mem);
    info->lwt.total = mem.total;
    info->lwt.used = mem.used;

    return info;
}

void comgr_resetinfo(comgr_t *mgr)
{
    coinfo_t *info = mgr->info;
    size_t size = sizeof(lwtop_t) * LwtEnd;
    (void)memset_s(info->lwt.op, size, 0, size);
}
