// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include <stddef.h>

#include "bitmap.h"

#include "spinlock.h"
#include "log.h"

#include <assert.h>
#include <stdlib.h>

#define BMAP_MAX_LEVEL      (6)
#define BMAP_SLICE_BITS (RECOMMEND_BITS)
#define BMAP_U64_BITS       (64)
#define BMAP_SLICE_U64  (BMAP_SLICE_BITS / BMAP_U64_BITS)

#define BMAP_SLICE_MASK (BMAP_SLICE_BITS - 1)
#define  BMAP_U64_MASK  (BMAP_U64_BITS - 1)

/*************************************************************************
*************************************************************************/

typedef struct
{
    int     count;      /* 当前区域的bit数 */
    uint64_t    *area;  /* bit 区域 */
}_bits_t;

struct bitmap
{
    spinlock_t      lock;

    int             max;                /* 位总数 */
    int             level;              /* 位图的层次 */
    _bits_t         layer[BMAP_MAX_LEVEL];  /* 分层位图（0层为最高层）,上一层位图的一位对应下一层的一个slice,一个slice最多256个bit位 */
};

/*************************************************************************
*************************************************************************/

static inline int _div_upward_round(int num, int base)
{
    return (num + base -1) / base;
}

static inline int _get_slice_bit_count(_bits_t *bits, int pos)
{
    int cnt = bits->count - (pos & ~(BMAP_SLICE_MASK));
    if (cnt > BMAP_SLICE_BITS)
    {
        cnt = BMAP_SLICE_BITS;
    }

    return cnt;
}

static inline int _find_first_zero_bit(_bits_t *bits, int *pos)
{
    int array_size = _div_upward_round(bits->count, BMAP_U64_BITS);

    int64_t res = 0;
    for (int i = 0; i < array_size; i++)
    {
        uint64_t bmap = ~(bits->area[i]);

        res = bmap_bsf(bmap);
        if (res >= 0)
        {
            res += (int64_t)i * BMAP_U64_BITS;
            break;
        }
    }

    /* 位图位置只能是[0, count)区间的值 */
    if ((res < 0) || (res >= bits->count))
    {
        return BIT_FAIL;
    }

    if (NULL != pos)
    {
        *pos = (int)res;
    }

    return BIT_OK;
}

static inline int _set_bit(_bits_t *bits, int pos)
{
    int idx = pos / BMAP_U64_BITS;
    int off = pos & BMAP_U64_MASK;

    if (0 != bmap_testset_bit(off, &(bits->area[idx])))
    {
        return BIT_FAIL;
    }

    return BIT_OK;
}

static inline int _clear_bit(_bits_t *bits, int pos)
{
    int idx = pos / BMAP_U64_BITS;
    int off = pos & BMAP_U64_MASK;

    if (0 == bmap_testclear_bit(off, &(bits->area[idx])))
    {
        return BIT_FAIL;
    }

    return BIT_OK;
}

static int _bitmap_set_bit(bitmap_t *bmap, int pos)
{
    int ret = BIT_OK;
    int bit = pos;

    /* 从最底层开始设置位图，当下层的一个slice占满时，需要设置上层的一个bit */
    for (int i = bmap->level - 1; i >= 0; i--)
    {
        /* 设置当前层的bit位 */
        ret = _set_bit(&(bmap->layer[i]), bit);
        if ((BIT_OK != ret) || (i == 0))
        {
            break;
        }

        /* 获取bit位所在的slice信息 */
        int count = _get_slice_bit_count(&(bmap->layer[i]), bit);
        int slice = bit / BMAP_SLICE_BITS;
        _bits_t check = {.count = count,
                            .area = bmap->layer[i].area + slice * BMAP_SLICE_U64};

        /* 如果slice未占满，则直接返回 */
        if (BIT_OK == _find_first_zero_bit(&check, NULL))
        {
            break;
        }

        /* slice已占满，设置上一层需要设置的bit */
        bit = slice;
    }

    return ret;
}

static int _bitmap_clear_bit(bitmap_t *bmap, int pos)
{
    /* 清除最底层位图，如果失败，则存在重复清除问题 */
    int ret = _clear_bit(&(bmap->layer[bmap->level - 1]), pos);
    if (BIT_OK != ret)
    {
        return ret;
    }

    /* 循环清除高层位图，如果失败，则高层位图并未设置 */
    int bit = pos / BMAP_SLICE_BITS;
    for (int i = bmap->level - 2; i >= 0; i--, bit /= BMAP_SLICE_BITS)
    {
        /* 清除当前层的bit位 */
        ret = _clear_bit(&(bmap->layer[i]), bit);
        if (BIT_OK != ret)
        {
            break;
        }
    }

    return BIT_OK;
}

/*************************************************************************
*************************************************************************/

bitmap_t *bitmap_create(int bit_count)
{
    if (__builtin_expect(!!(bit_count <= 0), 0))
    {
        return NULL;
    }

    /* 计算level数和bit区域的总大小 */
    int u64_cnt = 0;
    int level = 0;
    int bits = bit_count;
    for (int i = bit_count; i != 0; level++, i /= BMAP_SLICE_BITS)
    {
        u64_cnt += _div_upward_round(bits, BMAP_U64_BITS);
        bits = _div_upward_round(bits, BMAP_SLICE_BITS);
    }

    if (level > BMAP_MAX_LEVEL)
    {
        return NULL;
    }

    /* 申请内存空间 */
    size_t msize = sizeof(bitmap_t) + (uint32_t)u64_cnt * sizeof(uint64_t);
    char *mem = (char *)calloc(1, msize);
    if (NULL == mem)
    {
        return NULL;
    }

    /* 初始化整个bitmap区间 */
    bitmap_t *bmap = (bitmap_t *)mem;
    uint64_t *area = (uint64_t *)(mem + sizeof(bitmap_t));

    spinlock_init(&bmap->lock);
    bmap->max = bit_count;
    bmap->level = level;
    bits = bit_count;
    for (int i = level - 1; i >= 0; i--)
    {
        bmap->layer[i].count = bits;
        bmap->layer[i].area = area;

        area += _div_upward_round(bits, BMAP_U64_BITS);
        bits = _div_upward_round(bits, BMAP_SLICE_BITS);
    }

    return bmap;
}

void bitmap_destroy(bitmap_t *bmap)
{
    if (NULL == bmap)
    {
        return;
    }

    spinlock_destroy(&bmap->lock);
    free(bmap);
}

int bitmap_allocbit(bitmap_t *bmap, int *bit)
{
    spinlock_lock(&bmap->lock);

    /* 先判断最高层 */
    int pos = 0;
    int ret = _find_first_zero_bit(&bmap->layer[0], &pos);
    if (BIT_OK != ret)
    {
        spinlock_unlock(&bmap->lock);
        return ret;
    }

    /* 一定有空闲bit位 */
    int start = 0;
    for (int i = 1; i < bmap->level; i++)
    {
        start += pos;
        start *= BMAP_SLICE_BITS;

        /* 获取bit位所在的slice信息 */
        int count = _get_slice_bit_count(&(bmap->layer[i]), start);
        _bits_t bits = {.count = count,
                            .area = bmap->layer[i].area + start / BMAP_U64_BITS};
        ret = _find_first_zero_bit(&bits, &pos);
        assert(ret == BIT_OK);
    }

    *bit = start + pos;

    ret = _bitmap_set_bit(bmap, *bit);
    assert(ret == BIT_OK);

    spinlock_unlock(&bmap->lock);

    return ret;
}

int bitmap_freebit(bitmap_t *bmap, int bit)
{
    if (__builtin_expect(!!(bit >= bmap->max), 0))
    {
        return BIT_FAIL;
    }

    spinlock_lock(&bmap->lock);
    int ret = _bitmap_clear_bit(bmap, bit);
    spinlock_unlock(&bmap->lock);

    return ret;
}
