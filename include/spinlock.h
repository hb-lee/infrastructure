// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __x86_64__

#ifndef LOCK_PREFIX
#define LOCK_PREFIX "lock;"
#endif

typedef struct
{
    volatile long locked;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lock)
{
    lock->locked = 0;
}

static inline void spinlock_destroy(spinlock_t *lock)
{
    lock->locked = -1;
}

static inline void spinlock_lock(spinlock_t *lock)
{
    long lock_val = 1;
    __asm__ __volatile__(
                        "1:\n"  LOCK_PREFIX
                        "xchg %[locked], %[lv]\n"
                        "test %[lv], %[lv]\n"
                        "jz 3f\n"
                        "2:\n"
                        "pause\n"
                        "cmpl $0, %[locked]\n"
                        "jnz 2b\n"
                        "jmp 1b\n"
                        "3:\n"
                        : [locked] "=m" (lock->locked), [lv] "=q" (lock_val)
                        : "[lv]" (lock_val)
                        : "memory");
}

static inline void spinlock_unlock(spinlock_t *lock)
{
    long unlock_val = 0;
    __asm__ __volatile__(
                        LOCK_PREFIX
                        "xchg %[locked], %[ulv]\n"
                        : [locked] "=m" (lock->locked), [ulv] "=q" (unlock_val)
                        : "[ulv]" (unlock_val)
                        : "memory");
}

#else

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef pthread_spinlock_t spinlock_t;

static inline void spinlock_init(spinlock_t *lock)
{
    int ret = pthread_spin_init(lock, 0);
    if (0 != ret)
    {
        fprintf(stderr, "pthread_spin_init failed, ret=%d\n", ret);
        abort();
    }
}

static inline void spinlock_destroy(spinlock_t *lock)
{
    int ret = pthread_spin_destroy(lock);
    if (0 != ret)
    {
        fprintf(stderr, "pthread_spin_destroy failed, ret=%d\n", ret);
        abort();
    }
}

static inline void spinlock_lock(spinlock_t *lock)
{
    int ret = pthread_spin_lock(lock);
    if (0 != ret)
    {
        fprintf(stderr, "pthread_spin_lock failed, ret=%d\n", ret);
        abort();
    }
}

static inline void spinlock_unlock(spinlock_t *lock)
{
    int ret = pthread_spin_unlock(lock);
    if (0 != ret)
    {
        fprintf(stderr, "pthread_spin_unlock failed, ret=%d\n", ret);
        abort();
    }
}

#endif

#ifdef __cplusplus
}
#endif

#endif