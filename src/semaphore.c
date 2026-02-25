// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "sema.h"
#include "atomic.h"
#include "log.h"

#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <semaphore.h>
#include <string.h>
#include <stdlib.h>

/*************************************************************************
*************************************************************************/

#define COND_WAIT       0
#define COND_DONE       1

typedef struct
{
    uint64_t    pad[SEM_SIZE - 1];
    int         cond;
    int         flag;
}_sem_t;

typedef struct
{
    int         (*special)(void);
    int         (*init)(void *sem);
    int         (*fini)(void *sem);

    int         (*up)(void *sem);
    int         (*down)(void *sem);
    void        (*sleep)(uint32_t ms);
}_sem_ops_t;

static _sem_ops_t g_sem_ops = {NULL};

/*************************************************************************
*************************************************************************/

int sema_register(int       (*special)(void),
                    int     (*init)(void *sem),
                    int     (*fini)(void *sem),
                    int     (*up)(void *sem),
                    int     (*down)(void *sem),
                    void    (*sleep)(uint32_t ms))
{
    if ((NULL == init)
        || (NULL == fini)
        || (NULL == up)
        || (NULL == down)
        || (NULL == sleep))
    {
        return EINVAL;
    }

        g_sem_ops.special = special;
        g_sem_ops.init = init;
        g_sem_ops.fini = fini;
        g_sem_ops.up = up;
        g_sem_ops.down = down;
        g_sem_ops.sleep = sleep;

        return 0;
}

void sema_init(sema_t sem)
{
    _sem_t *local = (_sem_t *)(void *)sem;

    local->flag = (NULL != g_sem_ops.special) ? g_sem_ops.special() : 0;
    local->cond = COND_WAIT;

    if (local->flag)
    {
        int ret = g_sem_ops.init(local->pad);
        if (0 != ret)
        {
            log_fatal("init(%p) failed, error(%s)", local->pad, strerror(ret));
            abort();
        }
    }
    else
    {
        if (0 != sem_init((sem_t *)(void *)local->pad, 0, 0))
        {
            log_error("sem_init(%p) failed, error(%s)", local->pad, strerror(errno));
            abort();
        }
    }
}

void sema_fini(sema_t sem)
{
    _sem_t *local = (_sem_t *)(void *)sem;

    if (local->flag)
    {
        int ret = g_sem_ops.fini(local->pad);
        if (0 != ret)
        {
            log_warn("fini(%p) failed, error(%s)", local->pad, strerror(ret));
        }
    }
    else
    {
        if (0 != sem_destroy((sem_t *)(void *)local->pad))
        {
            log_warn("sem_destroy(%p) failed, error(%s)", local->pad, strerror(errno));
        }
    }
}

void sema_up(sema_t sem)
{
    _sem_t *local = (_sem_t *)(void *)sem;

    if (local->flag)
    {
        int ret = g_sem_ops.up(local->pad);
        if (0 != ret)
        {
            log_fatal("up(%p) failed, error(%s)", local->pad, strerror(ret));
            abort();
        }
    }
    else
    {
        if (0 != sem_post((sem_t *)(void *)local->pad))
        {
            int err = errno;

            int val = 0;
            if (0 != sem_getvalue((sem_t *)(void *)local->pad, &val))
            {
                log_error("sem(%p) is invalid(maybe destroy), error(%s)",
                            local->pad, strerror(errno));
            }

            log_error("sem_post(%p/%d) failed, error(%s)", local->pad, val, strerror(err));
            abort();
        }

        if (!atomic_s32_cas(&local->cond, COND_WAIT, COND_DONE, NULL))
        {
            log_error("up(%p) again", local->pad);
            abort();
        }
    }
}

void sema_down(sema_t sem)
{
    _sem_t *local = (_sem_t *)(void *)sem;

    if (local->flag)
    {
        int ret = g_sem_ops.down(local->pad);
        if (0 != ret)
        {
            log_warn("down(%p) failed, error(%s)", local->pad, strerror(ret));
        }
    }
    else
    {
        int ret = 0;
        do
        {
            ret = sem_wait((sem_t *)(void *)local->pad);
            if (0 != ret)
            {
                log_warn("sem_wait(%p) failed, error(%s)", local->pad, strerror(errno));
            }
        } while((ret != 0) && (errno == EINTR));

        while (!atomic_s32_cas(&local->cond, COND_DONE, COND_WAIT, NULL))
        {
            sched_yield();
        }
    }
}

void sema_msleep(sema_t sem, uint32_t ms)
{
    _sem_t *local = (_sem_t *)(void *)sem;

    if (local->flag)
    {
        g_sem_ops.sleep(ms);
    }
    else
    {
        usleep(ms * 1000);
    }
}
