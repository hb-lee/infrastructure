// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __MCACHE_H__
#define __MCACHE_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

typedef struct mc mc_t;

typedef int (*MCfunc_cmp)   (void *first, void *second);
typedef uint32_t    (*MCfunc_hash)  (void *key);
typedef void    (*MCfunc_dump)  (void *item, char *buff, uint64_t len);
typedef void    (*MCfunc_clean) (void *item);
typedef bool    (*MCfunc_freeable)  (void *item);
typedef int (*MCfunc_int)   (void *args, void *mem);
typedef void    (*MCfunc_void)  (void *args, void *mem);

/*************************************************************************
*************************************************************************/

/* create a mcache manager */
mc_t *mc_create(const char *name,
                uint32_t scale,
                uint32_t isize,
                MCfunc_cmp cmp,
                MCfunc_hash hash,
                MCfunc_dump dump,
                MCfunc_clean  clean,
                MCfunc_freeable freeable);

/* destroy a mcache manager */
void mc_destroy(mc_t *mc);

/* cleanup a mcache manager */
void mc_cleanup(mc_t *mc);

/*************************************************************************
*************************************************************************/

/* alloc a item in mcache */
void *mc_item_alloc(mc_t *mc);

/* destroy a item from mcache */
void mc_item_free(mc_t *mc, void *ptr);

/* set key of item */
int mc_item_setkey(mc_t *mc, void *mem, void *key);

/* search item in mcache */
void *mc_item_search(mc_t *mc, void *key, void *args, MCfunc_void found);

/* insert item into mcache */
int mc_item_insert(mc_t *mc, void *nw, void **old, void *args, MCfunc_void found);

/* delete key of item */
int mc_item_delete(mc_t *mc, void *key, void *args, MCfunc_int condition);

/* run a function for item under lock protection */
int mc_item_protect(mc_t *mc, void *key, void *args, MCfunc_int func);

/* let all item in mcache to call func */
int mc_item_foreach(mc_t *mc, void *args, MCfunc_int func);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif