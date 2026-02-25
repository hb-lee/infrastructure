// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define GENERIC_CAS(_type, _var, _expect, _new, _old)                       \
    _type __tmp__ = _expect;                                                \
    _type *__ptr__ = &__tmp__;                                              \
    if (NULL != _old)                                                       \
    {                                                                       \
        *old = _expect;                                                     \
        __ptr__ = _old;                                                     \
    }                                                                       \
    return __atomic_compare_exchange_n(_var, __ptr__, _new, false,          \
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

static inline uint64_t atomic_u64_add(uint64_t *augend, uint64_t addend)
{
    return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_u64_fetch(uint64_t *var)
{
    return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_u64_inc(uint64_t *var)
{
    return __atomic_add_fetch(var, 1, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_u64_dec(uint64_t *var)
{
    return __atomic_sub_fetch(var, 1, __ATOMIC_SEQ_CST);
}

static inline bool atomic_u64_cas(uint64_t *var,
                                  uint64_t expect,
                                  uint64_t nw,
                                  uint64_t *old)
{
    GENERIC_CAS(uint64_t, var, expect, nw, old);
}

static inline void atomic_u64_store(uint64_t *var, uint64_t val)
{
    __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}

static inline bool atomic_bool_cas(bool *var,
                                   bool expect,
                                   bool nw,
                                   bool *old)
{
    GENERIC_CAS(bool, var, expect, nw, old);
}

static inline bool atomic_bool_fetch(bool *var)
{
    return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}

static inline void atomic_bool_store(bool *var, bool val)
{
    __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}

static inline bool atomic_point_cas(void **var,
                                    void *expect,
                                    void *nw,
                                    void **old)
{
    GENERIC_CAS(void *, var, expect, nw, old);
}

static inline int32_t atomic_s32_inc(int32_t *var)
{
    return __atomic_add_fetch(var, 1, __ATOMIC_SEQ_CST);
}

static inline int32_t atomic_s32_dec(int32_t *var)
{
    return __atomic_sub_fetch(var, 1, __ATOMIC_SEQ_CST);
}

static inline int32_t atomic_s32_fetch(int32_t *var)
{
    return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}

static inline void atomic_s32_store(int32_t *var, int32_t val)
{
    __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}

static inline bool atomic_s32_cas(int32_t *var,
                                  int32_t expect,
                                  int32_t nw,
                                  int32_t *old)
{
    GENERIC_CAS(int32_t, var, expect, nw, old);
}

static inline uint32_t atomic_u32_inc(uint32_t *var)
{
    return __atomic_add_fetch(var, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_u32_dec(uint32_t *var)
{
    return __atomic_sub_fetch(var, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_u32_fetch(uint32_t *var)
{
    return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}

static inline bool atomic_u32_cas(uint32_t *var,
                                  uint32_t expect,
                                  uint32_t nw,
                                  uint32_t *old)
{
    GENERIC_CAS(uint32_t, var, expect, nw, old);
}

static inline void atomic_u32_store(uint32_t *var, uint32_t val)
{
    __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_u32_clearbits(uint32_t *var, uint32_t bits)
{
    return __atomic_and_fetch(var, ~bits, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_u32_setbits(uint32_t *var, uint32_t bits)
{
    return __atomic_or_fetch(var, bits, __ATOMIC_SEQ_CST);
}

static inline uint16_t atomic_u16_clearbits(uint16_t *var, uint16_t bits)
{
    return __atomic_and_fetch(var, ~bits, __ATOMIC_SEQ_CST);
}

static inline uint16_t atomic_u16_setbits(uint16_t *var, uint16_t bits)
{
    return __atomic_or_fetch(var, bits, __ATOMIC_SEQ_CST);
}

static inline uint16_t atomic_u16_post_clearbits(uint16_t *var, uint16_t bits)
{
    return __atomic_fetch_and(var, ~bits, __ATOMIC_SEQ_CST);
}

static inline uint16_t atomic_u16_post_setbits(uint16_t *var, uint16_t bits)
{
    return __atomic_fetch_or(var, bits, __ATOMIC_SEQ_CST);
}

static inline uint16_t atomic_u16_fetch(uint16_t *var)
{
    return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}

#endif