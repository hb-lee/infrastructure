// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __MCACHE_STAT_H__
#define __MCACHE_STAT_H__

#include "mcache.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

typedef struct
{
    const char          *name;      /* name of mcache */

    struct
    {
        uint64_t        bcount;     /* bucket count */
        uint64_t        total;      /* total key in hash map */
        uint64_t        min;        /* min depth of buckets */
        uint64_t        max;        /* max depth of buckets */
        uint64_t        avg;        /* avg depth of buckets */
    }hmap;

    struct
    {
        uint64_t        size;       /* size of item */
        uint64_t        max;        /* max count of item */
        uint64_t        fcount;     /* free count of item */
        uint64_t        ucount;     /* inuse count of item */
    }item;
}mc_info_t;

void mc_get_info(mc_t *mc, mc_info_t *info);

/*************************************************************************
*************************************************************************/

void mcstat_register(const char *name, mc_t *mc);
void mcstat_unregister(const char *name);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif