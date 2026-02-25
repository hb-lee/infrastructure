// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __COSTATIS_H__
#define __COSTATIS_H__

#include "coroutine.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

enum
{
    LwtQue,             /* 等待队列 */
    LwtRun,             /* 执行 */
    LwtSche,            /* 切换调度 */
    LwtSemup,           /* 信号量唤醒 */
    LwtEnd
};

typedef struct
{
    uint64_t            begin;  /* 开始次数 */
    uint64_t            end;    /* 结束次数 */
    uint64_t            delay;  /* 总延迟 */
    uint64_t            max;    /* 最大延时 */
}lwtop_t;

typedef struct
{
    struct
    {
        uint32_t        total;  /* lwt的总数 */
        uint32_t        used;   /* 已经使用的lwt数 */
        lwtop_t         op[LwtEnd]; /* lwt操作统计 */
    }lwt;

    struct
    {
        uint32_t        total;  /* worker的总数 */
        uint32_t       *count;  /* 每个worker上的lwt数 */
    }worker;
}coinfo_t;

const coinfo_t *comgr_getinfo   (comgr_t    *mgr);
void            comgr_resetinfo     (comgr_t *mgr);

/*************************************************************************
*************************************************************************/

void            costat_register     (const char *name, comgr_t *mgr);
void            costat_unregister   (const char *name);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif