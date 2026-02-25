// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "mempool.h"
#include "atomic.h"

#include "bitmap.h"
#include "sema.h"
#include "log.h"

#include "securec.h"

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#define MAX_CPUS        64U     /* 最大核数 */
#define MIN_WAIT        1       /* 申请失败最小等待毫秒数 */
#define MAX_WAIT        1024    /* 申请失败最大等待毫秒数 */

struct memorypool
{
    uint32_t        fix_size;       /* 内存单元长度 */

    uint32_t        max;            /* 最大单元数 */
    uint32_t        used;           /* 已分配的数量 */

    uint64_t        b_idx;          /* 位图索引指针 */
    int             b_avg;          /* 位图平均bit数 */
    int             b_cnt;          /* 位图数量 */
    bitmap_t      **b_map;          /* 位图指针数组 */
    char           *mem;            /* 内存空间 */
};

/*************************************************************************
*************************************************************************/

static inline void _mempool_destroy_bitmap(mempool_t *pool)
{
    for (int i = 0; i < pool->b_cnt; i++)
    {
        bitmap_destroy(pool->b_map[i]);
        pool->b_map[i] = NULL;
    }
}

static inline void *_mempool_malloc(mempool_t *pool)
{
    uint64_t val = atomic_u64_inc(&pool->b_idx);
    int _idx = (int)(val % (uint64_t)(uint32_t)pool->b_cnt);
    int _bit = -1;
    for (int i = 0; i < pool->b_cnt; i++)
    {
        if (BIT_OK == bitmap_allocbit(pool->b_map[_idx], &_bit))
        {
            int pos = _idx * pool->b_avg + _bit;
            (void)atomic_u32_inc(&pool->used);

            return pool->mem + (uint64_t)pool->fix_size * (uint32_t)pos;
        }

        _idx++;
        _idx %= pool->b_cnt;
    }

    return NULL;
}

/*************************************************************************
*************************************************************************/

mempool_t *mempool_create(uint32_t size, uint32_t count, void *ptr)
{
    if (0 == count)
    {
        log_error("mem: count is 0");
        return NULL;
    }

    /* 计算位图数量，位图的总数不超过CPU数量，
     * 每个位图的bit数不少于RECOMMEND_BITS（除非只有一个位图） */
    int cpu = (int)sysconf(_SC_NPROCESSORS_CONF);
    if (0 > cpu)
    {
        log_warn("sysconf get CPUS fail, err=%s", strerror(errno));
        cpu = (int)MAX_CPUS;
    }

    uint32_t b_cnt = (uint32_t)cpu * 5 / 4;
    if (b_cnt > MAX_CPUS)
    {
        b_cnt = MAX_CPUS;
    }

    if ((count / b_cnt) < RECOMMEND_BITS)
    {
        b_cnt = count / RECOMMEND_BITS;
        if ((count % RECOMMEND_BITS) || (0 == b_cnt))
        {
            b_cnt++;
        }
    }

    /* 计算每个位图的bit数，如果不能均分，最后一个位图的bit数将变多 */
    uint32_t bits_in_map[MAX_CPUS] = {0};
    uint32_t average = count / b_cnt;
    for (uint32_t i = 0; i < b_cnt; i++)
    {
        bits_in_map[i] = average;
    }

    if ((b_cnt * average) < count)
    {
        bits_in_map[b_cnt - 1] += (count - b_cnt * average);
    }

    /* 分配内存空间 */
    size_t m_size = sizeof(mempool_t) + b_cnt * sizeof(bitmap_t *);
    if (!ptr)
    {
        m_size += (uint64_t)size * count;
    }
    char *mem = (char *)malloc(m_size);
    if (NULL == mem)
    {
        log_error("mempool: malloc failed");
        return NULL;
    }

    /* 初始化内存空间 */
    mempool_t *pool = (mempool_t *)mem;
    mem += sizeof(mempool_t);

    pool->fix_size = size;
    pool->max = count;
    pool->used = 0;
    pool->b_idx = 0;
    pool->b_avg = (int)average;
    pool->b_cnt = 0;
    pool->b_map = (bitmap_t **)(void *)(mem);
    mem += (b_cnt * sizeof(void *));

    pool->mem = !ptr ? mem : (char *)ptr;

    /* 创建位图 */
    for (uint32_t i = 0; i < b_cnt; i++, pool->b_cnt++)
    {
        pool->b_map[i] = bitmap_create((int)bits_in_map[i]);
        if (NULL == pool->b_map[i])
        {
            log_error("mempool: create bitmap(%u,%u) failed",
                        i, bits_in_map[i]);

            _mempool_destroy_bitmap(pool);
            free(pool);
            pool = NULL;
            break;
        }
    }

    return pool;
}

void mempool_destroy(mempool_t *pool)
{
    if (NULL == pool)
    {
        return;
    }

    _mempool_destroy_bitmap(pool);
    free(pool);
}

void *mempool_alloc(mempool_t *pool)
{
    void *mem = NULL;

    sema_t sem;
    sema_init(sem);
    for (uint32_t tw = MIN_WAIT; tw < MAX_WAIT; tw <<= 1)
    {
        mem = _mempool_malloc(pool);
        if (NULL != mem)
        {
            break;
        }

        sema_msleep(sem, tw);
    }

    sema_fini(sem);
    return mem;
}

void mempool_free(mempool_t *mempool, void *mem)
{
    /* 计算内存块所在的位置 */
    int bit = (int)(((char *)mem - mempool->mem) / (int)mempool->fix_size);
    if ((bit < 0) || (bit >= (int)mempool->max))
    {
        log_error("mempool: %p not in mempool", mem);
        return;
    }

    /* 计算位图位置 */
    int idx = bit / mempool->b_avg;
    bit %= mempool->b_avg;
    if (idx > mempool->b_cnt - 1)
    {
        idx = mempool->b_cnt - 1;
        bit += mempool->b_avg;
    }

    if (0 != bitmap_freebit(mempool->b_map[idx], bit))
    {
        log_error("mempool: may be double-free");
        abort();
    }

    (void)atomic_u32_dec(&mempool->used);
}

/*************************************************************************
*************************************************************************/

void mempool_getinfo(mempool_t *mempool, mempool_info_t *info)
{
    info->fix_size = mempool->fix_size;
    info->total = mempool->max;
    info->used = mempool->used;
}