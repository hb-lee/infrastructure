// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "stimer.h"
#include "atomic.h"
#include "log.h"
#include "spinlock.h"
#include "sysdef.h"

#include "securec.h"
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/*************************************************************************
*************************************************************************/

struct sleeper
{
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
    pthread_condattr_t      cattr;

    bool                    enable;
    bool                    idle;
};

struct stimer
{
    void                    *args;
    void                   (*func)(void *args);

    bool                    is_run;
    uint32_t                cycle;
    sleeper_t               sleeper;

    spinlock_t              lock;
    pthread_t               thread;
};

/*************************************************************************
*************************************************************************/

static inline int _timespec_cmp(struct timespec *tm1, struct timespec *tm2)
{
    int64_t cmp = tm1->tv_sec - tm2->tv_sec;
    if (0 != cmp)
    {
        return (int)cmp;
    }

    return (int)(tm1->tv_nsec - tm2->tv_nsec);
}

static void _sleeper_wakeup(sleeper_t *sleeper)
{
    int ret = pthread_mutex_lock(&sleeper->mutex);
    sys_assert(0 == ret);

    sleeper->enable = false;
    ret = pthread_cond_signal(&sleeper->cond);
    sys_assert(0 == ret);

    ret = pthread_mutex_unlock(&sleeper->mutex);
    sys_assert(0 == ret);
}

static int _sleeper_init(sleeper_t *sleeper)
{
    /* 1. 将cond设置为相对时间，避免系统时间跳变造成问题 */
    int ret = pthread_condattr_init(&sleeper->cattr);
    if (0 != ret)
    {
        log_error("pthread_condattr_init failed, err(%s)", strerror(ret));
        return -1;
    }

    ret = pthread_condattr_setclock(&sleeper->cattr, CLOCK_MONOTONIC);
    if (0 != ret)
    {
        log_error("pthread_condattr_setclock failed, err(%s)", strerror(ret));
        (void)pthread_condattr_destroy(&sleeper->cattr);
        return -1;
    }

    /* 2. 初始化mutex和condition */
    ret = pthread_cond_init(&sleeper->cond, &sleeper->cattr);
    if (0 != ret)
    {
        (void)pthread_condattr_destroy(&sleeper->cattr);
        return -1;
    }

    ret = pthread_mutex_init(&sleeper->mutex, NULL);
    if (0 != ret)
    {
        log_error("pthread_mutex_init failed, err(%s)", strerror(ret));
        (void)pthread_cond_destroy(&sleeper->cond);
        (void)pthread_condattr_destroy(&sleeper->cattr);
        return -1;
    }

    sleeper->enable = true;
    sleeper->idle = true;
    return 0;
}

static void _sleeper_fini(sleeper_t *sleeper)
{
    /* 1. 如果sleeper还在wait状态，先做唤醒 */
    while (!sleeper->idle)
    {
        _sleeper_wakeup(sleeper);
        usleep(1000);
    }

    /* 2. 资源释放 */
    (void)pthread_mutex_destroy(&sleeper->mutex);
    (void)pthread_cond_destroy(&sleeper->cond);
    (void)pthread_condattr_destroy(&sleeper->cattr);
}

static void _sleeper_wait(sleeper_t *sleeper, uint64_t timeout)
{
    /* 1. 先判断sleeper状态，避免同时wait */
    int ret = pthread_mutex_lock(&sleeper->mutex);
    sys_assert(0 == ret);

    sleeper->idle = false;

    /* 2. 通过pthread_cond_timewait实现超时等待 */
    struct timespec tm = {0};
    ret = clock_gettime(CLOCK_MONOTONIC, &tm);
    sys_assert(0 == ret);

    struct timespec now = tm;

    tm.tv_nsec += (int64_t)(timeout * 1000000);
    tm.tv_sec += tm.tv_nsec / 1000000000;
    tm.tv_nsec %= 1000000000;

    while (sleeper->enable && (0 > _timespec_cmp(&now, &tm)))
    {
        (void)pthread_cond_timedwait(&sleeper->cond, &sleeper->mutex, &tm);

        ret = clock_gettime(CLOCK_MONOTONIC, &now);
        sys_assert(0 == ret);
    }

    sleeper->idle = true;

    ret = pthread_mutex_unlock(&sleeper->mutex);
    sys_assert(0 == ret);
}

/*************************************************************************
*************************************************************************/

static void *_timer_svc(void *args)
{
    stimer_t *timer = (stimer_t *)args;

    while (atomic_bool_cas(&timer->is_run, true, true, NULL))
    {
        _sleeper_wait(&timer->sleeper, (uint64_t)timer->cycle);
        timer->sleeper.enable = true;
        if (atomic_bool_cas(&timer->is_run, false, false, NULL))
        {
            break;
        }

        void (*func)(void *);
        void *private;

        spinlock_lock(&timer->lock);
        func = timer->func;
        private = timer->args;
        spinlock_unlock(&timer->lock);

        func(private);
    }

    return NULL;
}

/*************************************************************************
*************************************************************************/

sleeper_t *sleeper_create()
{
    sleeper_t *sleeper = (sleeper_t *)malloc(sizeof(sleeper_t ));
    if (NULL == sleeper)
    {
        return NULL;
    }

    if (0 != _sleeper_init(sleeper))
    {
        free(sleeper);
        sleeper = NULL;
    }

    return sleeper;
}

void sleeper_destroy(sleeper_t *sleeper)
{
    _sleeper_fini(sleeper);
    free(sleeper);
}

void sleeper_wait(sleeper_t *sleeper, uint32_t timeout)
{
    _sleeper_wait(sleeper, (uint64_t)timeout);
}

void sleeper_wakeup(sleeper_t *sleeper)
{
    _sleeper_wakeup(sleeper);
}

/*************************************************************************
*************************************************************************/

int stimer_modify(stimer_t *timer,
                    uint32_t cycle,
                    void *args,
                    void (*func)(void *args))
{
    if (NULL == timer)
    {
        log_error("timer: modify timer failed, timer handle id NULL");
        return EINVAL;
    }

    spinlock_lock(&timer->lock);
    timer->args = args;
    timer->func = func;
    timer->cycle = cycle;
    spinlock_unlock(&timer->lock);

    return 0;
}

stimer_t *stimer_create(const char *name,
                        uint32_t cycle,
                        void *args,
                        void (*func)(void *args))
{
    stimer_t *timer = (stimer_t *)malloc(sizeof(stimer_t));
    if (NULL == timer)
    {
        log_error("timer: malloc failed");
        return NULL;
    }

    spinlock_init(&timer->lock);

    if (0 != _sleeper_init(&timer->sleeper))
    {
        log_error("timer: sem_init failed, ret=%d", errno);
        spinlock_destroy(&timer->lock);
        free(timer);
        return NULL;
    }

    timer->args = args;
    timer->func = func;
    timer->cycle = cycle;
    timer->is_run = true;

    int ret = pthread_create(&timer->thread, NULL, _timer_svc, timer);
    if (0 != ret)
    {
        log_error("timer: pthread create failed, ret(%d)", ret);

        _sleeper_fini(&timer->sleeper);
        spinlock_destroy(&timer->lock);
        free(timer);
        return NULL;
    }

    char _name[TM_NAME + 1] ={0};
    sprintf_s(_name, sizeof(_name), "%.*s", TM_NAME, name);
    (void)pthread_setname_np(timer->thread, _name);

    return timer;
}

void stimer_wakeup(stimer_t *timer)
{
    if (NULL != timer)
    {
        _sleeper_wakeup(&timer->sleeper);
    }
}

void stimer_destroy(stimer_t *timer)
{
    if (NULL == timer)
    {
        return;
    }

    if (atomic_bool_cas(&timer->is_run, true, false, NULL))
    {
        _sleeper_wakeup(&timer->sleeper);
        _sleeper_fini(&timer->sleeper);
        (void)pthread_join(timer->thread, NULL);
    }

    free(timer);
}

uint64_t stimer_getnanosec()
{
    struct timespec ts = {0};
    (void)clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000UL + (uint64_t)ts.tv_nsec);
}
