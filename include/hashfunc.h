// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __HASH_FUNC_H__
#define __HASH_FUNC_H__

#include <stddef.h>
#include <stdint.h>

static inline uint64_t hashstr_seed(const char *buf, size_t len, uint64_t seed)
{
    uint64_t hash = 0;

    for (size_t i = 0; i < len; i++)
    {
        hash = hash *seed + buf[i];
    }

    return hash;
}

static inline uint64_t hashstr(const char *buf, size_t len)
{
    return hashstr_seed(buf, len, 131UL);
}

static inline int u64_cmp(void *key1, void *key2)
{
    return (*((uint64_t *)key1) == *((uint64_t *)key2)) ? 0 : 1;
}

static inline uint32_t u64_hash(void *key)
{
    uint64_t val = *((uint64_t *)key);
    return (uint32_t)val;
}

static inline int u32_cmp(void *key1, void *key2)
{
    return (*((uint32_t *)key1) == *((uint32_t *)key2)) ? 0 : 1;
}

static inline uint32_t u32_hash(void *key)
{
    uint32_t val = *((uint32_t *)key);
    return val;
}

#endif