// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "cmdline.h"
#include "log.h"

#include "securec.h"

#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <new>
#include <map>
#include <string>

#define BUFF_RES        (2)
#define DIAGNOSE_MAX    (256)
#define MAX_BUF_SIZE    (1048576)
#define LOCAL_BUF       (1024)

#define gettid()    syscall(__NR_gettid)

/*************************************************************************
*************************************************************************/

static void OutPrint(const char *format, ...);

class CmdSession
{
    private:
        char        buff[MAX_BUF_SIZE];
        char       *ptr;
        uint32_t    res;

    public:
        CmdSession()
        {
            memset_s(buff, sizeof(buff), 0, sizeof(buff));
            res = sizeof(buff);
            ptr = buff;
        }

        CmdSession(CmdSession &se) = delete;

        void Printf(const char *str)
        {
            if (res < BUFF_RES)
            {
                return;
            }

            size_t len = strlen(str);
            len = (len > (res - BUFF_RES)) ? (res - BUFF_RES) : len;

            if (EOK != memcpy_s(ptr, res - BUFF_RES, str, len))
            {
                return;
            }

            res -= len;
            ptr += len;

            ptr[0] = '\n';
            ptr++;
            res--;

            ptr[0] = '\0';
        }

        const char *GetBuff()
        {
            return buff;
        }
};

class Cmd
{
    private:
        std::string     name;
        void           *ctx;
        void           (*help)(void     *ctx,
                                void    (*print)(const char *, ...));
        void           (*func)(void     *ctx,
                                void    (*print)(const char *, ...),
                                int     argc,
                                argv_t  argv);

    public:
        explicit Cmd(const char *_name,
                    void        *_ctx,
                    void       (*_help)(void    *ctx,
                                         void   (*print)(const char *, ...)),
                    void       (*_func)(void    *ctx,
                                         void   (*print)(const char *, ...),
                                         int    argc,
                                         argv_t argv))
        {
            if ((NULL == _name) || (NULL == _help) || (NULL == _func))
            {
                throw std::bad_alloc();
            }

            name = std::string(_name);
            ctx = _ctx;
            help = _help;
            func = _func;
        }

        Cmd(Cmd &cmd) = delete;

        void Help()
        {
            help(ctx, OutPrint);
        }

        void Handler(int argc, argv_t argv)
        {
            func(ctx, OutPrint, argc, argv);
        }
};

class CmdSet
{
    private:
        static std::map<std::string, Cmd *> cmdMap;
        static std::map<pid_t, CmdSession *> sessionMap;

    public:
        CmdSet(){}
        ~CmdSet(){}

        static void RegisterCmd(const char *name,
                                void *ctx,
                                void (*help)(void *ctx,
                                            void (*print)(const char *, ...)),
                                void (*func)(void *ctx,
                                            void (*print)(const char *, ...),
                                            int argc,
                                            argv_t argv))
        {
            auto cmd = cmdMap.find(std::string(name));
            if (cmd != cmdMap.end())
            {
                log_error("cmdline: cmd(%s) already registered", name);
                return;
            }

            try
            {
                cmdMap[std::string(name)] = new Cmd(name, ctx, help, func);
            }
            catch (std::bad_alloc &)
            {
                log_error("cmdline: alloc cmd failed");
            }
        }

        static void UnregisterCmd(const char *name)
        {
            auto cmd = cmdMap.find(std::string(name));
            if (cmd != cmdMap.end())
            {
                delete cmd->second;
                cmdMap.erase(cmd);
            }
        }

        static Cmd *FindCmd(const char *name)
        {
            auto cmd = cmdMap.find(std::string(name));
            if (cmd != cmdMap.end())
            {
                return cmd->second;
            }

            return NULL;
        }

        static CmdSession *FetchSession()
        {
            pid_t id = gettid();

            auto se = sessionMap.find(id);
            if (se != sessionMap.end())
            {
                return se->second;
            }

            try
            {
                CmdSession *sess = new CmdSession();
                sessionMap[id] = sess;

                return sess;
            }
            catch (std::bad_alloc &)
            {
                log_error("diagnose: alloc session failed");
                return NULL;
            }
        }

        static char *FreeSession(CmdSession *se)
        {
            pid_t id = gettid();
            (void)sessionMap.erase(id);

            char *output = strdup(se->GetBuff());
            delete se;

            return output;
        }

        static void Help()
        {
            for (auto iter = cmdMap.begin(); iter != cmdMap.end(); ++iter)
            {
                iter->second->Help();
            }
        }
};

std::map<std::string, Cmd *> CmdSet::cmdMap;
std::map<pid_t, CmdSession *> CmdSet::sessionMap;

/*************************************************************************
*************************************************************************/

static void OutPrint(const char *format, ...)
{
    char buff[LOCAL_BUF];

    va_list ap;

    va_start(ap, format);
    vsprintf_s(buff, sizeof(buff), format, ap);
    va_end(ap);

    CmdSession *se = CmdSet::FetchSession();
    if (NULL != se)
    {
        se->Printf(buff);
    }
}

/*************************************************************************
*************************************************************************/

enum { HELP_ALL, HELP_ONE, HELP_NONE };

static int inline _help_mode(int argc, argv_t argv)
{
    if (argc <= 0)
    {
        return HELP_ALL;
    }

    if (0 == strcasecmp(argv[0], "help"))
    {
        if (argc == 1)
        {
            return HELP_ALL;
        }

        return (NULL != CmdSet::FindCmd(argv[1])) ? HELP_ONE : HELP_ALL;
    }

    return (NULL == CmdSet::FindCmd(argv[0])) ? HELP_ALL : HELP_NONE;
}

static bool _need_help(CmdSession *se, int argc, argv_t argv)
{
    int mode = _help_mode(argc, argv);

    Cmd  *cmd = NULL;
    switch (mode)
    {
        case HELP_ALL:
            CmdSet::Help();
            return true;

        case HELP_ONE:
            cmd = CmdSet::FindCmd(argv[1]);
            if (NULL != cmd)
            {
                cmd->Help();
            }

            return true;

        default:
            return false;
    }
}

/*************************************************************************
*************************************************************************/

void cmd_register(const char *name,
                void        *ctx,
                void       (*help)(void     *ctx,
                                    void    (*print)(const char *, ...)),
                void       (*func)(void     *ctx,
                                    void    (*print)(const char *, ...),
                                    int     argc,
                                    argv_t  argv))
{
    if (0 == strcasecmp(name, "help"))
    {
        return;
    }

    CmdSet::RegisterCmd(name, ctx, help, func);
}

void cmd_unregister(const char *name)
{
    CmdSet::UnregisterCmd(name);
}

char *cmd_handler(int argc, argv_t argv)
{
    CmdSession *se = CmdSet::FetchSession();
    if (NULL == se)
    {
        return NULL;
    }

    if (!_need_help(se, argc, argv))
    {
        Cmd *cmd = CmdSet::FindCmd(argv[0]);
        if (NULL == cmd)
        {
            return NULL;
        }

        cmd->Handler(argc, argv);
    }

    return CmdSet::FreeSession(se);
}

void cmd_free(char *output)
{
    free(output);
}