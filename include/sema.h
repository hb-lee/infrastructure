// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __SEMA_H__
#define __SEMA_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

#define SEM_SIZE 11

typedef uint64_t sema_t[SEM_SIZE];

/*************************************************************************
*************************************************************************/

int sema_register(int   (*special)(void),
                    int (*init)(void *sem),
                    int (*fini)(void *sem),
                    int (*up)(void *sem),
                    int (*down)(void *sem),
                    void    (*sleep)(uint32_t ms));

/*************************************************************************
*************************************************************************/

void    sema_init   (sema_t sem);
void    sema_fini   (sema_t sem);
void    sema_down   (sema_t sem);
void    sema_up (sema_t sem);

void    sema_msleep (sema_t sem, uint32_t ms);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
