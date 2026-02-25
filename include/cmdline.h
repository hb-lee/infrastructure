// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __CMDLINE_H__
#define __CMDLINE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
*************************************************************************/

typedef const char * const * argv_t;

/*************************************************************************
*************************************************************************/

void cmd_register(const char *name,
                void    *ctx,
                void    (*help)(void    *ctx,
                                 void   (*print)(const char *, ...)),
                void    (*func)(void    *ctx,
                                 void   (*print)(const char *, ...),
                                 int    argc,
                                 argv_t argv));

void cmd_unregister(const char *name);

char *cmd_handler(int argc, argv_t argv);

void cmd_free(char *output);

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif