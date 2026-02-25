// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "tpstat.h"
#include "cmdline.h"
#include "log.h"

#include <string.h>
#include <map>
#include <string>

#define TPCOUNT     8
#define TPSTAT_CMD  "tpstat"
#define TPSTAT_ARGC 2

/*************************************************************************
*************************************************************************/

static void _tpstat_help(void *, void (*)(const char *, ...));
static void _tpstat_func(void *, void (*)(const char *, ...), int, argv_t);

class TpstatMgr
{
    private:
        static std::map<std::string, threadpool_t *> tpMap;

        static void printThreadpool(threadpool_t *tp,
                                void    (*print)(const char *, ...))
        {
            tp_info_t info = {NULL};
            threadpool_get_info(tp, &info);

            for (uint32_t i = 0; i < info.total; i+= TPCOUNT)
            {
                uint32_t val[TPCOUNT] = {0};
                for (uint32_t j = 0; j < TPCOUNT; j++)
                {
                    if ((i + j) < info.total)
                    {
                        val[j] = info.clist[i + j];
                    }
                }

                (i == 0) ?
                    print("| %-12s | %5u | %4u | %4u | %4u | %4u | %4u | %4u | %4u | %4u |",
                        info.name, info.total, val[0], val[1], val[2], val[3], val[4],
                        val[5], val[6], val[7]) :
                    print("| %-12s | %5s | %4u | %4u | %4u | %4u | %4u | %4u | %4u | %4u |",
                        " ", " ", val[0], val[1], val[2], val[3], val[4], val[5],
                        val[6], val[7]);
            }
        }

    public:

        static void Register(const char *name, threadpool_t *tp)
        {
            auto _md = tpMap.find(std::string(name));
            if (_md != tpMap.end())
            {
                log_error("mod(%s) already registered", name);
                return;
            }

            if (tpMap.empty())
            {
                cmd_register(TPSTAT_CMD, NULL, _tpstat_help, _tpstat_func);
            }

            tpMap[std::string(name)] = tp;
        }

        static void Unregister(const char *name)
        {
            auto _md = tpMap.find(std::string(name));
            if (_md != tpMap.end())
            {
                tpMap.erase(_md);
            }

            if (tpMap.empty())
            {
                cmd_unregister(TPSTAT_CMD);
            }
        }

        static void PrintAll(void (*print)(const char *, ...))
        {
            print("---------------------------------------------------------------------");
            print("|    Name    | Count |               JobsPerThread                   |");

            for (auto iter = tpMap.begin(); iter != tpMap.end(); ++iter)
            {
                print("|---------------------|--------|---------------------------------|");
                printThreadpool(iter->second, print);
            }

            print("---------------------------------------------------------------------");
        }
};

std::map<std::string, threadpool_t *> TpstatMgr::tpMap;

/*************************************************************************
*************************************************************************/

static void _tpstat_help(void *nouse, void (*print)(const char *, ...))
{
    print("Usage: "
            "\t%-10s %-10s{help information}\n"
            "\t%-10s %-10s{get statistic data}\n",
            TPSTAT_CMD, "help", TPSTAT_CMD, "get");
}

static void _tpstat_func(void *nouse,
                            void (*print)(const char *, ...),
                            int argc,
                            argv_t argv)
{
    if (argc != TPSTAT_ARGC)
    {
        _tpstat_help(nouse, print);
        return;
    }

    if (0 == strcasecmp(argv[1], "get"))
    {
        TpstatMgr::PrintAll(print);
        return;
    }

    _tpstat_help(nouse, print);
}

/*************************************************************************
*************************************************************************/

void tpstat_register(const char *name, threadpool_t *tp)
{
    TpstatMgr::Register(name, tp);
}

void tpstat_unregister(const char *name)
{
    TpstatMgr::Unregister(name);
}
