// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __STIMER_H__
#define __STIMER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

#define TM_NAME     10

/*************************************************************************
*************************************************************************/

typedef struct sleeper sleeper_t;
typedef struct stimer stimer_t;

/*************************************************************************
*************************************************************************/

sleeper_t   *sleeper_create     (void);
void        sleeper_destroy     (sleeper_t *sleeper);
void        sleeper_wait        (sleeper_t *sleeper,
                                    uint32_t timeout);
void        sleeper_wakeup      (sleeper_t *sleeper);

/*************************************************************************
*************************************************************************/

int         stimer_modify       (stimer_t   *timer,
                                    uint32_t    cycle,
                                    void        *args,
                                    void        (*func)(void *args));

stimer_t    *stimer_create      (const char *name,
                                    uint32_t cycle,
                                    void    *args,
                                    void    (*func)(void *args));

void        stimer_wakeup       (stimer_t *timer);

void        stimer_destroy      (stimer_t *timer);

uint64_t    stimer_getnanosec   (void);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif