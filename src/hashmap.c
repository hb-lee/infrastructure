// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "hashmap.h"
#include "atomic.h"
#include "spinlock.h"
#include "log.h"

#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>

#define AVG_DEPTH       4U
#define DOUBLE_ZERO     0.00000001

/*************************************************************************
*************************************************************************/

typedef struct
{
    spinlock_t          lock;  /* 互斥锁 */
    list_head_t         list;  /* 冲突链 */
    uint64_t            depth;  /* 冲突链长度 */
}_bucket_t;

struct hashmap
{
    uint64_t            total_keys;  /* Hash表总的key数目 */

    HMfunc_cmp          cmp;    /* key比较函数 */
    HMfunc_hash         hash;   /* key哈希计算函数 */

    uint64_t            max_depth;  /* 不精确的最大桶深度 */
    uint32_t            b_count;    /* Hash桶数量 */
    _bucket_t           buckets[0]; /* Hash桶 */
};

/*************************************************************************
*************************************************************************/

static uint32_t _adjust_size(uint32_t size)
{
    double exp = log((double)size) / log((double)2);

    uint32_t shift = (uint32_t)exp;

    if (((exp - (double)shift) > DOUBLE_ZERO) || (shift == 0))
    {
        shift++;
    }

    return (1U << shift);
}

int hashmap_create(uint32_t     scale,
                    HMfunc_cmp  cmp,
                    HMfunc_hash hash,
                    hashmap_t   **map)
{
    if (0 == scale)
    {
        log_error("hashmap scale can't be 0");
        return EINVAL;
    }

    uint32_t b_count = scale / AVG_DEPTH;
    b_count = _adjust_size(b_count);

    /* 分配Hash表结构 */
    size_t mem_size = sizeof(hashmap_t) + b_count * sizeof(_bucket_t);
    hashmap_t *hmap = (hashmap_t *)malloc(mem_size);
    if (NULL == hmap)
    {
        return ENOMEM;
    }

    /* 初始化hash表 */
    hmap->total_keys = 0;
    hmap->cmp   = cmp;
    hmap->hash  = hash;
    hmap->b_count   = b_count;
    hmap->max_depth = 0;

    for (uint32_t i = 0; i < b_count; i++)
    {
        list_init(&(hmap->buckets[i].list));
        spinlock_init(&hmap->buckets[i].lock);
        hmap->buckets[i].depth = 0;
    }

    *map = hmap;

    return 0;
}

void hashmap_destroy(hashmap_t *map, void *args, HMfunc_void func)
{
    if (NULL == map)
    {
        return;
    }

    for (uint32_t i = 0; i < map->b_count; i++)
    {
        _bucket_t *bucket = &(map->buckets[i]);

        spinlock_lock(&bucket->lock);

        /* 扫描冲突链，查找要删除的元素 */
        list_head_t *curr;
        list_head_t *next;
        list_foreach_safe(curr, next, &bucket->list)
        {
            hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
            list_del(&exist_data->list_node);
            if (NULL != func)
            {
                func(args, exist_data);
            }

            (void)atomic_u64_dec(&map->total_keys);
        }

        bucket->depth = 0;

        spinlock_unlock(&bucket->lock);
        spinlock_destroy(&bucket->lock);
    }

    /* 释放hash表结构 */
    free(map);
}

void hashmap_cleanup(hashmap_t *map, void *args, HMfunc_void func)
{
    for (uint32_t i = 0; i < map->b_count; i++)
    {
        _bucket_t *bucket = &(map->buckets[i]);

        spinlock_lock(&bucket->lock);

        /* 扫描冲突链，查找要删除的元素 */
        list_head_t *curr;
        list_head_t *next;
        list_foreach_safe(curr, next, &bucket->list)
        {
            hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
            list_del(&exist_data->list_node);
            if (NULL != func)
            {
                func(args, exist_data);
            }

            (void)atomic_u64_dec(&map->total_keys);
        }

        bucket->depth = 0;

        spinlock_unlock(&bucket->lock);
    }
}


int hashmap_insert(hashmap_t    *map,
                    hashlink_t  *new_data,
                    hashlink_t  **old_data,
                    void        *args,
                    HMfunc_void func)
{
    /* 计算Hash地址 */
    uint32_t hash_val = map->hash(new_data->key) & (map->b_count - 1);
    _bucket_t *bucket = &(map->buckets[hash_val]);

    spinlock_lock(&bucket->lock);

    /* 设置最大深度 */
    if (bucket->depth > map->max_depth)
    {
        map->max_depth = bucket->depth;
    }

    /* 判断是否已经存在该项 */
    list_head_t *curr;
    list_foreach(curr, &bucket->list)
    {
        hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
        if (0 == (map->cmp(new_data->key, exist_data->key)))
        {
            if (NULL != func)
            {
                func(args, exist_data);
            }

            if (NULL != old_data)
            {
                *old_data = exist_data;
            }

            spinlock_unlock(&bucket->lock);
            return EEXIST;
        }
    }

    /* 若不存在该项，则在hash表中加入该元素 */
    list_add(&new_data->list_node, &bucket->list);
    bucket->depth++;

    spinlock_unlock(&bucket->lock);

    (void)atomic_u64_inc(&map->total_keys);

    return 0;
}

int hashmap_replace(hashmap_t       *map,
                    hashlink_t      *new_data,
                    hashlink_t      **old_data,
                    void            *args,
                    HMfunc_int      func)
{
    /* 计算Hash地址 */
    uint32_t hash_val = map->hash(new_data->key) & (map->b_count - 1);
    _bucket_t *bucket = &(map->buckets[hash_val]);

    spinlock_lock(&bucket->lock);

    /* 设置最大桶深度 */
    if (bucket->depth > map->max_depth)
    {
        map->max_depth = bucket->depth;
    }

    /* 扫描冲突链，查找要替换的node */
    list_head_t *curr;
    list_foreach(curr, &bucket->list)
    {
        hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
        if (0 == (map->cmp(new_data->key, exist_data->key)))
        {
            if (NULL != func)
            {
                int ret = func(args, exist_data);
                if (0 != ret)
                {
                    spinlock_unlock(&bucket->lock);
                    return ret;
                }
            }

            if (NULL != old_data)
            {
                *old_data = exist_data;
            }

            /* 摘除旧的node */
            list_del(&exist_data->list_node);
            bucket->depth--;

            (void)atomic_u64_dec(&map->total_keys);

            break;
        }
    }

    /* 插入新的node */
    list_add(&new_data->list_node, &bucket->list);
    bucket->depth++;

    spinlock_unlock(&bucket->lock);

    (void)atomic_u64_inc(&map->total_keys);

    return 0;
}

int hashmap_search(hashmap_t        *map,
                    void            *key,
                    hashlink_t      **data,
                    void            *args,
                    HMfunc_void     func)
{
    /* 计算Hash地址 */
    uint32_t hash_val = map->hash(key) & (map->b_count - 1);
    _bucket_t *bucket = &(map->buckets[hash_val]);

    spinlock_lock(&bucket->lock);

    /* 设置最大桶深度 */
    if (bucket->depth > map->max_depth)
    {
        map->max_depth = bucket->depth;
    }

    /* 扫描冲突链，查找指定元素 */
    list_head_t *curr;
    list_foreach(curr, &bucket->list)
    {
        hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
        if (0 == (map->cmp(key, exist_data->key)))
        {
            if (NULL != func)
            {
                func(args, exist_data);
            }

            if (NULL != data)
            {
                *data = exist_data;
            }

            spinlock_unlock(&bucket->lock);
            return EEXIST;
        }
    }

    /* 若不存在该项，则返回失败 */
    spinlock_unlock(&bucket->lock);

    return ENOENT;
}

int hashmap_protect(hashmap_t       *map,
                    void            *key,
                    void            *args,
                    HMfunc_int      work)
{
    /* 计算Hash地址 */
    uint32_t hash_val = map->hash(key) & (map->b_count - 1);
    _bucket_t *bucket = &(map->buckets[hash_val]);

    spinlock_lock(&bucket->lock);

    /* 设置最大桶深度 */
    if (bucket->depth > map->max_depth)
    {
        map->max_depth = bucket->depth;
    }

    /* 扫描冲突链，查找指定元素 */
    list_head_t *curr;
    list_foreach(curr, &bucket->list)
    {
        hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
        if (0 == (map->cmp(key, exist_data->key)))
        {
            int ret = 0;
            if (NULL != work)
            {
                ret = work(args, exist_data);
            }

            spinlock_unlock(&bucket->lock);
            return ret;
        }
    }

    /* 若不存在该项，则返回失败 */
    spinlock_unlock(&bucket->lock);

    return ENOENT;
}

int hashmap_delete(hashmap_t        *map,
                    void            *key,
                    hashlink_t      **del,
                    void            *args,
                    HMfunc_int      func)
{
    /* 计算Hash地址 */
    uint32_t hash_val = map->hash(key) & (map->b_count - 1);
    _bucket_t *bucket = &(map->buckets[hash_val]);

    spinlock_lock(&bucket->lock);

    /* 设置最大桶深度 */
    if (bucket->depth > map->max_depth)
    {
        map->max_depth = bucket->depth;
    }

    /* 扫描冲突链，查找要删除的元素 */
    list_head_t *curr;
    list_foreach(curr, &bucket->list)
    {
        hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
        if (0 == (map->cmp(key, exist_data->key)))
        {
            if (NULL != func)
            {
                /* 若func判定不为OK，则取消删除并返回判定结果 */
                int ret = func(args, exist_data);
                if (0 != ret)
                {
                    spinlock_unlock(&bucket->lock);
                    return ret;
                }
            }

            if (NULL != del)
            {
                /* 若hash_data内存是静态申请，则输出由用户释放 */
                *del = exist_data;
            }

            list_del(&exist_data->list_node);
            bucket->depth--;

            spinlock_unlock(&bucket->lock);

            (void)atomic_u64_dec(&map->total_keys);

            return 0;
        }
    }

    spinlock_unlock(&bucket->lock);

    /* 若不存在该项，则返回失败 */
    return ENOENT;
}

void hashmap_eviction(hashmap_t     *map,
                        uint64_t    depth,
                        void        *args,
                        HMfunc_int  func)
{
    uint64_t avg = map->total_keys / map->b_count;
    if (avg > depth)
    {
        avg = depth;
    }

    /* 最大桶深度归零 */
    map->max_depth = 0;

    for (uint32_t i = 0; i < map->b_count; i++)
    {
        _bucket_t *bucket = &(map->buckets[i]);

        spinlock_lock(&bucket->lock);

        /* 桶深度小于平均值，不做清除 */
        if (bucket->depth <= avg)
        {
            spinlock_unlock(&bucket->lock);
            continue;
        }

        uint64_t skip = avg;

        /* 扫描冲突链，确定淘汰项 */
        list_head_t *curr;
        list_head_t *next;
        list_foreach_safe(curr, next, &bucket->list)
        {
            /* 新插入的数据都在链表头，故先跳过表头的数据 */
            if (skip != 0)
            {
                skip--;
                continue;
            }

            hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);
            list_del(&exist_data->list_node);

            /* evict_func将释放内存 */
            if (0 == func(args, exist_data))
            {
                bucket->depth--;
                (void)atomic_u64_dec(&map->total_keys);
            }
            else
            {
                list_add_tail(&exist_data->list_node, next);
            }
        }

        spinlock_unlock(&bucket->lock);
    }
}

int hashmap_foreach(hashmap_t       *map,
                    void            *args,
                    HMfunc_int      func,
                    bool            ret_flag)
{
    for (uint32_t i = 0; i < map->b_count; i++)
    {
        _bucket_t *bucket = &(map->buckets[i]);

        spinlock_lock(&bucket->lock);

        /* 设置最大桶深度 */
        if (bucket->depth > map->max_depth)
        {
            map->max_depth = bucket->depth;
        }

        /* 扫描冲突链，查找要处理的元素 */
        list_head_t *curr;
        list_head_t *next;
        list_foreach_safe(curr, next, &bucket->list)
        {
            hashlink_t *exist_data = container_of(curr, hashlink_t, list_node);

            int ret = func(args, exist_data);
            if (ret_flag && (0 != ret))
            {
                spinlock_unlock(&bucket->lock);
                return ret;
            }
        }

        spinlock_unlock(&bucket->lock);
    }

    return 0;
}

void hashmap_get_info(hashmap_t *table, bool detail, hashmap_info_t *info)
{
    info->total_keys        = table->total_keys;
    info->bucket.count      = table->b_count;
    info->bucket.avg_depth  = info->total_keys / table->b_count;
    info->bucket.max_depth  = table->max_depth;
    info->bucket.min_depth  = 0;

    if (!detail)
    {
        return;
    }

    uint64_t max_d = 0;
    uint64_t min_d = (uint64_t)-1;

    for (uint32_t index = 0; index < table->b_count; index++)
    {
        _bucket_t *bucket = &(table->buckets[index]);

        spinlock_lock(&bucket->lock);

        min_d = (min_d < bucket->depth) ? min_d : bucket->depth;
        max_d = (max_d > bucket->depth) ? max_d : bucket->depth;

        spinlock_unlock(&bucket->lock);
    }

    info->bucket.max_depth = max_d;
    info->bucket.min_depth = min_d;
}

