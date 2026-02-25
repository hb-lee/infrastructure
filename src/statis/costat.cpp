// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "costat.h"
#include "cmdline.h"
#include "log.h"

#include <string.h>
#include <map>
#include <string>

#define COSTAT_CMD      "costat"
#define COSTAT_ARGC     2
#define COCOUNT         6

static const char * const lwt_op[] =
{
    [LwtQue]        = "LwtQue",
    [LwtRun]        = "LwtRun",
    [LwtSche]       = "LwtSche",
    [LwtSemup]      = "LwtSemup"
};

/*************************************************************************
*************************************************************************/

static void _costat_help(void *, void (*)(const char *, ...));
static void _costat_func(void *, void (*)(const char *, ...), int, argv_t);

class CostatMgr
{
    private:
        static std::map<std::string, comgr_t *> coMap;

        static void printOp(const char *name,
                            comgr_t *mgr,
                            void (*print)(const char *, ...))
        {
            const coinfo_t *info = comgr_getinfo(mgr);
            for (int i = LwtQue; i < LwtEnd; i++)
            {
                uint64_t begin = info->lwt.op[i].begin;
                uint64_t end = info->lwt.op[i].end;
                uint64_t doing = (begin > end) ? begin - end : 0;
                uint64_t avg = (0 == end) ? 0 : info->lwt.op[i].delay / end;

                (i == LwtQue) ?
                    print("| %-10s | %-10s | %8lu | %8lu | %10lu |",
                        name, lwt_op[i], doing, avg, info->lwt.op[i].max) :
                    print("| %-10s | %-10s | %8lu | %8lu | %10lu |",
                        "  ", lwt_op[i], doing, avg, info->lwt.op[i].max);
            }
        }

        static void printLwt(const char *name,
                            comgr_t *mgr,
                            void (*print)(const char *, ...))
        {
            const coinfo_t *info = comgr_getinfo(mgr);
            for (uint32_t i = 0; i < info->worker.total; i += COCOUNT)
            {
                uint32_t val[COCOUNT] = {0};
                for (uint32_t j = 0; j < COCOUNT; j++)
                {
                    if ((i + j) < info->worker.total)
                    {
                        val[j] = info->worker.count[i + j];
                    }
                }

                (i == 0) ?
                    print("| %-10s | %5u | %5u | %5u | %4u | %4u | %4u | %4u | %4u | %4u |",
                        name, info->worker.total, info->lwt.total, info->lwt.used,
                        val[0], val[1], val[2], val[3], val[4], val[5]) :
                    print("| %-10s | %5s | %5s | %5s | %4u | %4u | %4u | %4u | %4u | %4u |",
                        " ", " ", " ", " ",
                        val[0], val[1], val[2], val[3], val[4], val[5]);
            }
        }

    public:

        static void Register(const char *name, comgr_t *mgr)
        {
            auto _md = coMap.find(std::string(name));
            if (_md != coMap.end())
            {
                log_error("mod(%s) already registered", name);
                return;
            }

            if (coMap.empty())
            {
                cmd_register(COSTAT_CMD, NULL, _costat_help, _costat_func);
            }

            coMap[std::string(name)] = mgr;
        }

        static void Unregister(const char *name)
        {
            auto _md = coMap.find(std::string(name));
            if (_md != coMap.end())
            {
                coMap.erase(_md);
            }

            if (coMap.empty())
            {
                cmd_unregister(COSTAT_CMD);
            }
        }

        static void ResetAll()
        {
            for (auto iter = coMap.begin(); iter != coMap.end(); ++iter)
            {
                comgr_resetinfo(iter->second);
            }
        }

        static void PrintAll(void (*print)(const char *, ...))
        {
            /* 1. 打印lwt延时统计信息 */
            print("---------------------------------------------------------------------");
            print("| %-10s | %-10s | %8s | %8s | %10s |",
                    "Name", "Operation", "Doing", "Average", "Max");

            for (auto iter = coMap.begin(); iter != coMap.end(); ++iter)
            {
                print("|------------|------------|------------|------------|------------|");
                printOp(iter->first.c_str(), iter->second, print);
            }
            print("---------------------------------------------------------------------");

            /* 2. 打印lwt线程分布信息 */
            print("\n---------------------------------------------------------------------");
            print("|    Name    |   WMax    |   LMax    |   LUse    |   LwtPerWorker    |");
            for (auto iter = coMap.begin(); iter != coMap.end(); ++iter)
            {
                print("------------|-------|-------|-------|------------------------------");
                printLwt(iter->first.c_str(), iter->second, print);
            }
            print("---------------------------------------------------------------------");
        }
};

std::map<std::string, comgr_t *> CostatMgr::coMap;

/*************************************************************************
*************************************************************************/

static void _costat_help(void *nouse, void (*print)(const char *, ...))
{
    print("Usage: "
            "\t%-10s %-10s{help information}\n"
            "\t%-10s %-10s{get statistic data}\n"
            "\t%-10s %-10s{reset statistic data}\n",
            COSTAT_CMD, "help", COSTAT_CMD, "get", COSTAT_CMD, "reset");
}

static void _costat_func(void *nouse,
                    void (*print)(const char *, ...),
                    int argc,
                    argv_t argv)
{
    if (argc != COSTAT_ARGC)
    {
        _costat_help(nouse, print);
        return;
    }

    if (0 == strcasecmp(argv[1], "get"))
    {
        CostatMgr::PrintAll(print);
        return;
    }

    if (0 == strcasecmp(argv[1], "reset"))
    {
        CostatMgr::ResetAll();
        return;
    }

    _costat_help(nouse, print);
}

/*************************************************************************
*************************************************************************/

void costat_register(const char *name, comgr_t *mgr)
{
    CostatMgr::Register(name, mgr);
}

void costat_unregister(const char *name)
{
    CostatMgr::Unregister(name);
}
