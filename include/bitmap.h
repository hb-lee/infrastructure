// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __BITMAP_H__
#define __BITMAP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __x86_64__

static inline int64_t bmap_bsf(uint64_t num)
{
    uint64_t count;
    __asm__ __volatile__(
        "bsfq %1, %0\n\t"
        "jnz 1f\n\t"
        "movq $-1, %0\n\t"
        "1:"
        :"=q"(count):"q"(num)  );

    return count;
}

static inline void bmap_set_bit(int siNr, void *pAddr)
{
    __asm__ __volatile__(
        "bts %1,%0"
        : "+m" (*(volatile uint64_t *)pAddr)
        : "Ir" (siNr)
        : "memory");
}

static inline void bmap_clear_bit(int siNr, void *pAddr)
{
    __asm__ __volatile__(
        "btr %1,%0"
        : "+m" (*(volatile uint64_t *)pAddr)
        : "Ir" (siNr));
}

static inline int bmap_test_bit(int siNr, void *pAddr)
{
    int oldbit;

    __asm__ __volatile__(
        "bt %2,%1\n\t"
        "sbb %0,%0"
        : "=r" (oldbit)
        : "m" (*(volatile uint64_t *)pAddr), "Ir" (siNr));

    return oldbit;
}

static inline int bmap_testset_bit(int siNr, void *pAddr)
{
    int oldbit;

    __asm__ __volatile__(
        "bts %2,%1\n\t"
        "sbb %0,%0"
        : "=r" (oldbit)
        : "m" (*(volatile uint64_t *)pAddr), "Ir" (siNr));

    return oldbit;
}

static inline int bmap_testclear_bit(int siNr, void *pAddr)
{
    int oldbit;

    __asm__ __volatile__(
        "btr %2,%1\n\t"
        "sbb %0,%0"
        : "=r" (oldbit)
        : "m" (*(volatile uint64_t *)pAddr), "Ir" (siNr));

    return oldbit;
}

#else

static inline int64_t bmap_bsf(uint64_t num)
{
    return (num == 0) ? -1 : __builtin_ctzl(num);
}

static inline void bmap_set_bit(int siNr, void *pAddr)
{
    *(uint64_t *)pAddr |= (1UL << siNr);
}

static inline void bmap_clear_bit(int siNr, void *pAddr)
{
    *(uint64_t *)pAddr &= ~(1UL << siNr);
}

static inline int bmap_test_bit(int siNr, void *pAddr)
{
    return (((*(uint64_t *)pAddr) & (1UL << siNr)) != 0UL) ? -1 : 0;
}

static inline int bmap_testset_bit(int siNr, void *pAddr)
{
    if (*(uint64_t *)pAddr & (1UL << siNr))
    {
        return -1;
    }

    *(uint64_t *)pAddr |= (1UL << siNr);
    return 0;
}

static inline int bmap_testclear_bit(int siNr, void *pAddr)
{
    if (*(uint64_t *)pAddr & (1UL << siNr))
    {
        *(uint64_t *)pAddr &= ~(1UL << siNr);
        return -1;
    }

    return 0;
}

#endif

#define BIT_OK  (0)
#define BIT_FAIL    (-1)

#define RECOMMEND_BITS  (256)

typedef struct bitmap bitmap_t;

bitmap_t *bitmap_create(int bit_count);
void    bitmap_destroy(bitmap_t *bitmap);

int bitmap_allocbit(bitmap_t *bitmap, int *bit);
int bitmap_freebit(bitmap_t *bitmap, int bit);

#ifdef __cplusplus
}
#endif

#endif
