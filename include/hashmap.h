// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __HASHMAP_H__
#define __HASHMAP_H__

#include "list.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
struct hashmap;
typedef struct hashmap hashmap_t;

typedef struct
{
    list_head_t list_node;      /* 冲突链 */
    void    *key;           /* hash key */
}hashlink_t;

typedef struct
{
    uint64_t    total_keys;     /* 哈希表中key的总数 */
    struct
    {
        uint64_t    count;      /* 哈希桶的数量 */
        uint64_t    avg_depth;      /* 平均哈希桶的深度（冲突链的长度） */
        uint64_t    max_depth;      /* 最大哈希桶的深度（冲突链的长度） */
        uint64_t    min_depth;      /* 最小哈希桶的深度（冲突链的长度） */
    }bucket;
}hashmap_info_t;

typedef int (*HMfunc_cmp)   (void *first, void *second);
typedef uint32_t    (*HMfunc_hash)(void *key);
typedef int (*HMfunc_int)  (void *args, hashlink_t *data);
typedef void    (*HMfunc_void)  (void *args, hashlink_t *data);

int hashmap_create  (uint32_t   scale,
                    HMfunc_cmp  cmp,
                    HMfunc_hash calc,
                    hashmap_t   **map);

void hashmap_destroy(hashmap_t *map, void *args, HMfunc_void func);
void hashmap_cleanup(hashmap_t *map, void *args, HMfunc_void func);

int hashmap_insert  (hashmap_t  *map,
                    hashlink_t  *new_data,
                    hashlink_t  **old_data,
                    void    *args,
                    HMfunc_void func);

int hashmap_replace (hashmap_t  *map,
                    hashlink_t  *new_data,
                    hashlink_t  **old_data,
                    void    *args,
                    HMfunc_int func);

int hashmap_search  (hashmap_t  *map,
                    void    *key,
                    hashlink_t  **data,
                    void    *args,
                    HMfunc_void func);

int hashmap_protect (hashmap_t *map,
                    void    *key,
                    void    *args,
                    HMfunc_int  work);

int hashmap_delete  (hashmap_t  *map,
                    void    *key,
                    hashlink_t  **del,
                    void    *args,
                    HMfunc_int  func);

int hashmap_foreach (hashmap_t  *map,
                    void    *args,
                    HMfunc_int  func,
                    bool    ret_flag);

void hashmap_eviction   (hashmap_t  *map,
                        uint64_t    depth,
                        void    *args,
                        HMfunc_int  func);

void hashmap_get_info(hashmap_t *map, bool detail, hashmap_info_t *info);

#ifdef __cplusplus
}
#endif
#endif