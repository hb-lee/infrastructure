// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "mcstat.h"
#include "hashfunc.h"
#include "hashmap.h"
#include "sema.h"
#include "threadpool.h"
#include "spinlock.h"
#include "log.h"
#include "atomic.h"

#include "securec.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

/*************************************************************************
*************************************************************************/

#define MEM(x)      ((x)->mem)
#define M_LIMIT(x)  (x * 65 / 100)
#define M_NLEN      9
#define ROUND8(x)   (((x) + 7) / 8 * 8)
#define MC_RETRY    3

/*************************************************************************
*************************************************************************/

typedef struct
{
    uint64_t            magic;      /* 魔数标识 */
    bool                free_out;   /* 标识是否能在外部释放 */
    union
    {
        hashlink_t      hash;       /* hash链接 */
        list_head_t     link;       /* 普通链表链接 */
    };
    char                mem[0];     /* 返回的内存空间 */
}_item_t;

typedef struct
{
    int                 count;
    spinlock_t          lock;
    list_head_t         list;
}_list_mgr_t;

struct mc
{
    char                name[M_NLEN];       /* 字符串标识 */
    uint64_t            magic;      /* 根据name计算魔数，用于item校验 */

    hashmap_t           *map;       /* item哈希表 */
    threadraw_t         *sweeper;   /* 清理者，清除map中的无用项 */

    struct
    {
        uint32_t        scale;     /* item最大数量 */
        uint32_t        alloc_count;    /* item已分配数量 */
        uint32_t        size;       /* item大小 */
        MCfunc_dump     dump;       /* item信息函数 */
        MCfunc_clean    clean;      /* 释放item内部额外申请的空间 */
        MCfunc_freeable freeable;   /* 判断item项能否被释放 */
    }item;

    struct
    {
        spinlock_t      lock;       /* 互斥锁 */
        bool            evicting;   /* 正在同步淘汰标识 */
        int             count;      /* 等待数量 */
        list_head_t     list;       /* 等待队列 */
    }wait;

    struct
    {
        _list_mgr_t     free;       /* 空闲链表 */
        _list_mgr_t     inuse;      /* 仍占用项inuse链表 */
    }mempool;
};

typedef struct
{
    void                *args;      /* 外部传入的私有变量 */
    union
    {
        MCfunc_void     void_f;
        MCfunc_int      int_f;
    };                              /* 在哈希表中找到item之后的执行函数 */
}_ctx_t;

typedef struct
{
    sema_t              sem;        /* 等待信号量 */
    list_head_t         link;       /* 等待队列链接 */
}_waiter_t;

/*************************************************************************
*************************************************************************/

static void void_clean(void *mem)
{
    ((void)mem);
}

static inline int _evict_begin(mc_t *mc)
{
    spinlock_lock(&mc->wait.lock);
    if (mc->wait.evicting)
    {
        _waiter_t waiter;
        sema_init(waiter.sem);
        mc->wait.count++;
        list_add_tail(&waiter.link, &mc->wait.list);
        spinlock_unlock(&mc->wait.lock);

        sema_down(waiter.sem);
        sema_fini(waiter.sem);

        return -1;
    }

    mc->wait.evicting = true;
    spinlock_unlock(&mc->wait.lock);

    return 0;
}

static inline void _evict_end(mc_t *mc)
{
    spinlock_lock(&mc->wait.lock);
    while (!list_empty(&mc->wait.list))
    {
        _waiter_t *waiter = container_of(mc->wait.list.next, _waiter_t, link);
        list_del(&waiter->link);
        mc->wait.count--;
        sema_up(waiter->sem);
    }

    mc->wait.evicting = false;
    spinlock_unlock(&mc->wait.lock);
}

static inline bool _evict_enable(mc_t *mc, uint32_t limit, hashmap_info_t *info)
{
    hashmap_get_info(mc->map, false, info);
    uint64_t holds = info->total_keys + (uint64_t)mc->mempool.inuse.count;
    return (holds > (uint64_t)limit) ? true : false;
}

static inline void _sweeper_inuse(mc_t *mc)
{
    spinlock_lock(&mc->mempool.inuse.lock);

    list_head_t *curr;
    list_head_t *next;
    list_foreach_safe(curr, next, &mc->mempool.inuse.list)
    {
        _item_t *item = container_of(curr, _item_t, link);
        if (mc->item.freeable(MEM(item)))
        {
            mc->item.clean(MEM(item));
            list_del(&item->link);

            mc->mempool.inuse.count--;
            spinlock_lock(&mc->mempool.free.lock);
            list_add(&item->link, &mc->mempool.free.list);
            mc->mempool.free.count++;
            spinlock_unlock(&mc->mempool.free.lock);
        }
    }

    spinlock_unlock(&mc->mempool.inuse.lock);
}

static int _sweeper_item(void *args, hashlink_t *link)
{
    mc_t *mc = (mc_t *)args;
    _item_t *item = container_of(link, _item_t, hash);
    if (mc->item.freeable(MEM(item)))
    {
        mc->item.clean(MEM(item));

        spinlock_lock(&mc->mempool.free.lock);
        list_add(&item->link, &mc->mempool.free.list);
        mc->mempool.free.count++;
        spinlock_unlock(&mc->mempool.free.lock);

        return 0;
    }

    return -1;
}

static void _sweeper_main(void *args)
{
    mc_t *mc = (mc_t *)args;
    _sweeper_inuse(mc);

    uint64_t depth = (uint64_t)mc->item.scale;
    hashmap_info_t info;
    while (_evict_enable(mc, M_LIMIT(mc->item.scale), &info) && (depth > 0))
    {
        if (depth > info.bucket.avg_depth)
        {
            depth = info.bucket.avg_depth;
        }

        depth >>= 1;
        hashmap_eviction(mc->map, depth, mc, _sweeper_item);
    }
}

static int _sweeper_needsleep(void *args)
{
    mc_t *mc = (mc_t *)args;
    hashmap_info_t info;
    return _evict_enable(mc, M_LIMIT(mc->item.scale), &info) ? 0 : 1;
}

/*************************************************************************
*************************************************************************/

static _item_t *_alloc_item(mc_t *mc)
{
    /* 1. 强制清除 */
    hashmap_info_t info;
    for (int retry = 0; _evict_enable(mc, mc->item.scale, &info); )
    {
        if (retry++ == MC_RETRY)
        {
            log_error("can't evict item, return NULL");
            return NULL;
        }

        if (0 == _evict_begin(mc))
        {
            _sweeper_inuse(mc);
            hashmap_eviction(mc->map, 0UL, mc, _sweeper_item);
            _evict_end(mc);
        }
    }

    /* 2. 异步清除 */
    if (_evict_enable(mc, M_LIMIT(mc->item.scale), &info))
    {
        threadraw_wakeup(mc->sweeper);
    }

    /* 3. 从mempool中获取 */
    spinlock_lock(&mc->mempool.free.lock);

    if (!list_empty(&mc->mempool.free.list))
    {
        _item_t *item = container_of(mc->mempool.free.list.next, _item_t, link);
        list_del(&item->link);
        mc->mempool.free.count--;

        spinlock_unlock(&mc->mempool.free.lock);
        return item;
    }

    spinlock_unlock(&mc->mempool.free.lock);

    spinlock_lock(&mc->mempool.inuse.lock);

    list_head_t *node, *next;
    list_foreach_safe(node, next, &mc->mempool.inuse.list)
    {
        _item_t *item = container_of(node, _item_t, link);
        if (mc->item.freeable(MEM(item)))
        {
            mc->item.clean(MEM(item));
            list_del(&item->link);
            mc->mempool.inuse.count--;

            spinlock_unlock(&mc->mempool.inuse.lock);
            return item;
        }
    }

    spinlock_unlock(&mc->mempool.inuse.lock);

    /* 4. 直接从内存中分配 */
    _item_t *item = NULL;
    if (mc->item.alloc_count < mc->item.scale)
    {
        item = (_item_t *)calloc(1, (sizeof(_item_t) + mc->item.size));
        if (NULL != item)
        {
            atomic_u32_inc(&mc->item.alloc_count);
        }
    }

    return item;
}

static void _free_item(void *args, hashlink_t *link)
{
    mc_t *mc  = (mc_t *)args;
    _item_t *item = container_of(link, _item_t, hash);

    if (mc->item.freeable(MEM(item)))
    {
        spinlock_lock(&mc->mempool.free.lock);
        mc->item.clean(MEM(item));
        list_add(&item->link, &mc->mempool.free.list);
        mc->mempool.free.count++;
        spinlock_unlock(&mc->mempool.free.lock);
    }
    else
    {
        spinlock_lock(&mc->mempool.inuse.lock);
        list_add_tail(&item->link, &mc->mempool.inuse.list);
        mc->mempool.inuse.count++;
        spinlock_unlock(&mc->mempool.inuse.lock);
    }
}

static void _void_found(void *args, hashlink_t *link)
{
    _ctx_t *ctx = (_ctx_t *)args;
    _item_t *item = container_of(link, _item_t, hash);

    if (NULL != ctx->void_f)
    {
        ctx->void_f(ctx->args, MEM(item));
    }
}

static int _int_found(void *args, hashlink_t *link)
{
    _ctx_t *ctx = (_ctx_t *)args;
    _item_t *item = container_of(link, _item_t, hash);

    int ret = 0;
    if (NULL != ctx->int_f)
    {
        ret = ctx->int_f(ctx->args, MEM(item));
    }

    return ret;
}

/*************************************************************************
*************************************************************************/

mc_t *mc_create(const char          *name,
                uint32_t            scale,
                uint32_t            isize,
                MCfunc_cmp          cmp,
                MCfunc_hash         hash,
                MCfunc_dump         dump,
                MCfunc_clean        clean,
                MCfunc_freeable     freeable)
{
    /* 1. 基本入参判定 */
    if ((NULL == name)
        || (NULL == cmp)
        || (NULL == hash)
        || (NULL == freeable))
    {
        log_error("parameter is invalid");
        return NULL;
    }

    /* 2. 内存空间分配 */
    mc_t *mc = (mc_t *)calloc(1, sizeof(struct mc));
    if (NULL == mc)
    {
        log_error("malloc mc(%s) failed", name);
        return NULL;
    }

    int ret = hashmap_create(scale, cmp, hash, &mc->map);
    if (0 != ret)
    {
        log_error("hashmap_create for mc(%s) failed, ret=%d",
                    name, ret);
        free(mc);
        return NULL;
    }

    char sname[THD_NAME + 1] = {0};
    sprintf_s(sname, sizeof(sname), "%.8sGc", name);
    mc->sweeper = threadraw_create(sname, mc, _sweeper_main,
                                    NULL, _sweeper_needsleep);
    if (NULL == mc->sweeper)
    {
        log_error("threadpool_create(%s) failed", name);
        hashmap_destroy(mc->map, NULL, NULL);
        free(mc);
        return NULL;
    }

    spinlock_init(&mc->mempool.free.lock);
    spinlock_init(&mc->mempool.inuse.lock);
    spinlock_init(&mc->wait.lock);

    /* 3. 成员变量赋值 */
    sprintf_s(mc->name, M_NLEN, "%.8s", name);
    mc->name[M_NLEN - 1] = 0;
    mc->magic = hashstr(mc->name, M_NLEN);

    mc->item.scale = scale;
    mc->item.alloc_count = 0;
    mc->item.size = ROUND8(isize);
    mc->item.dump = dump;
    mc->item.clean = (NULL == clean) ? void_clean : clean;
    mc->item.freeable = freeable;

    mc->wait.evicting = false;
    list_init(&mc->wait.list);

    mc->mempool.free.count = 0;
    mc->mempool.inuse.count = 0;
    list_init(&mc->mempool.free.list);
    list_init(&mc->mempool.inuse.list);

    mcstat_register(mc->name, mc);
    return mc;
}

void mc_destroy(mc_t *mc)
{
    if (NULL == mc)
    {
        return;
    }

    mcstat_unregister(mc->name);

    /* 1. 关闭清理线程 */
    threadraw_destroy(mc->sweeper);

    /* 2. 删除哈希表，将所有哈希项挂入空闲列表 */
    hashmap_destroy(mc->map, mc, _free_item);

    /* 3. 等待同步淘汰完成 */
    spinlock_lock(&mc->wait.lock);

    while ((mc->wait.evicting) || (mc->wait.count != 0))
    {
        spinlock_unlock(&mc->wait.lock);

        struct timespec time;
        time.tv_sec = 0;
        time.tv_nsec = 100 * 1000 * 1000;
        nanosleep(&time, NULL);

        spinlock_lock(&mc->wait.lock);
    }

    spinlock_unlock(&mc->wait.lock);

    /* 4. 清空空闲链表 */
    spinlock_lock(&mc->mempool.free.lock);
    while (!list_empty(&mc->mempool.free.list))
    {
        _item_t *item = container_of(mc->mempool.free.list.next, _item_t, link);
        list_del(&item->link);
        atomic_u32_dec(&mc->item.alloc_count);
        free(item);
    }
    spinlock_unlock(&mc->mempool.free.lock);

    spinlock_lock(&mc->mempool.inuse.lock);
    while (!list_empty(&mc->mempool.inuse.list))
    {
        _item_t *item = container_of(mc->mempool.inuse.list.next, _item_t, link);
        list_del(&item->link);

        if ((!mc->item.freeable(MEM(item))) && (NULL != mc->item.dump))
        {
            char buff[128];
            mc->item.dump(MEM(item), buff, sizeof(buff));
            log_error("item(%s) in mc(%s) not freeable", buff, mc->name);
        }

        mc->item.clean(MEM(item));
        atomic_u32_dec(&mc->item.alloc_count);
        free(item);
    }
    spinlock_unlock(&mc->mempool.inuse.lock);

    /* 5. 锁释放 */
    spinlock_destroy(&mc->wait.lock);
    spinlock_destroy(&mc->mempool.inuse.lock);
    spinlock_destroy(&mc->mempool.free.lock);

    /* 6. 释放mcache */
    free(mc);
}

void mc_cleanup(mc_t *mc)
{
    threadraw_wakeup(mc->sweeper);
}

/*************************************************************************
*************************************************************************/

void *mc_item_alloc(mc_t *mc)
{
    if (NULL == mc)
    {
        return NULL;
    }

    _item_t *item = _alloc_item(mc);
    if (NULL == item)
    {
        return NULL;
    }

    item->magic = mc->magic;
    item->free_out = true;
    return MEM(item);
}

void mc_item_free(mc_t *mc, void *mem)
{
    if ((NULL == mc) || (NULL == mem))
    {
        return;
    }

    _item_t *item = container_of(mem, _item_t, mem);
    if ((item->magic != mc->magic) || !(item->free_out))
    {
        return;
    }

    mc->item.clean(MEM(item));
    atomic_u32_dec(&mc->item.alloc_count);
    free(item);
}

/*************************************************************************
*************************************************************************/

int mc_item_setkey(mc_t *mc, void *mem, void *key)
{
    _item_t *item = container_of(mem, _item_t, mem);
    if (item->magic != mc->magic)
    {
        return EINVAL;
    }

    item->hash.key = key;
    return 0;
}

void *mc_item_search(mc_t *mc, void *key, void *args, MCfunc_void found)
{
    _ctx_t ctx;
    ctx.args = args;
    ctx.void_f = found;

    hashlink_t *data = NULL;
    int ret = hashmap_search(mc->map, key, &data, &ctx, _void_found);
    if (EEXIST == ret)
    {
        _item_t *item = container_of(data, _item_t, hash);
        return MEM(item);
    }

    return NULL;
}

int mc_item_insert(mc_t *mc, void *ne, void **old, void *args, MCfunc_void found)
{
    _item_t *item = container_of(ne, _item_t, mem);
    if (item->magic != mc->magic)
    {
        return EINVAL;
    }

    _ctx_t ctx;
    ctx.args = args;
    ctx.void_f = found;

    hashlink_t *exist = NULL;
    int ret = hashmap_insert(mc->map, &item->hash, &exist, &ctx, _void_found);
    if (EEXIST == ret)
    {
        _item_t *old_item = container_of(exist, _item_t, hash);

        if (NULL != old)
        {
            *old = MEM(old_item);
        }

        return EEXIST;
    }

    item->free_out = false;
    return 0;
}

int mc_item_delete(mc_t *mc, void *key, void *args, MCfunc_int condition)
{
    _ctx_t ctx;
    ctx.args = args;
    ctx.int_f = condition;

    hashlink_t *link = NULL;
    int ret = hashmap_delete(mc->map, key, &link, &ctx, _int_found);
    if (0 == ret)
    {
        _free_item(mc, link);
    }

    return ret;
}

int mc_item_protect(mc_t *mc, void *key, void *args, MCfunc_int func)
{
    _ctx_t ctx;
    ctx.args = args;
    ctx.int_f = func;
    return hashmap_protect(mc->map, key, &ctx, _int_found);
}

int mc_item_foreach(mc_t *mc, void *args, MCfunc_int func)
{
    _ctx_t ctx;
    ctx.args = args;
    ctx.int_f = func;
    return hashmap_foreach(mc->map, &ctx, _int_found, true);
}

void mc_get_info(mc_t *mc, mc_info_t *info)
{
    if ((NULL == mc) || (NULL == info))
    {
        return;
    }

    info->name = mc->name;

    hashmap_info_t hi;
    hashmap_get_info(mc->map, true, &hi);

    info->hmap.bcount = hi.bucket.count;
    info->hmap.total = hi.total_keys;
    info->hmap.min = hi.bucket.min_depth;
    info->hmap.max = hi.bucket.max_depth;
    info->hmap.avg = hi.bucket.avg_depth;

    info->item.size = mc->item.size;
    info->item.max = mc->item.scale;
    info->item.fcount = (uint64_t)mc->mempool.free.count;
    info->item.ucount = (uint64_t)mc->mempool.inuse.count;
}
