// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __SYSDEF_H__
#define __SYSDEF_H__

#include <stdlib.h>

/*************************************************************************
*************************************************************************/

#define UNREFERENCE(x)      ((void)x)

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define sys_assert(_cond)                       \
    if (unlikely(!(_cond))) {                   \
        abort();                                \
    }

/*************************************************************************
*************************************************************************/

#endif