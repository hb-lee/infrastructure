// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __MEMPOOL_H__
#define __MEMPOOL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

typedef struct memorypool mempool_t;

/*************************************************************************
*************************************************************************/

mempool_t *mempool_create(uint32_t size, uint32_t count, void *ptr);
void mempool_destroy(mempool_t *pool);

void *mempool_alloc(mempool_t *pool);
void mempool_free(mempool_t *pool, void *mem);

/*************************************************************************
*************************************************************************/

typedef struct
{
    uint32_t    fix_size;       /* 内存分片大小 */
    uint32_t    total;          /* 总数 */
    uint32_t    used;           /* 已用数量 */
}mempool_info_t;

void mempool_getinfo(mempool_t *pool, mempool_info_t *info);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif