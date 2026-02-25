// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include <stdint.h>
#include <stdbool.h>

#ifndef __COROUTINE_H__
#define __COROUTINE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*coroutine_func)(void *args);

typedef struct coroutine_mgr comgr_t;

int cosem_special   (void);
int cosem_init  (void *sem);
int cosem_fini  (void *sem);
int cosem_up    (void *sem);
int cosem_down  (void *sem);
void    cosem_sleep (uint32_t ms);

int coroutine_run   (comgr_t    *mgr,
                    void    *args,
                    coroutine_func func,
                    coroutine_func fini);

void    coroutine_yield (void);

comgr_t *comgr_create   (const char *name,
                        uint32_t    max_lwt,
                        uint32_t    max_worker,
                        uint32_t    stack_size);

void    comgr_destroy   (comgr_t    *mgr);

#ifdef __cplusplus
}
#endif

#endif