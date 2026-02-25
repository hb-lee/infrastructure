// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#include "mcstat.h"
#include "cmdline.h"
#include "log.h"

#include <string.h>
#include <map>
#include <string>

#define MCSTAT_CMD  "mcstat"
#define MCSTAT_ARGC 2

/*************************************************************************
*************************************************************************/

static void _mcstat_help(void *, void (*)(const char *, ...));
static void _mcstat_func(void *, void (*)(const char *, ...), int, argv_t);

class McstatMgr
{
    private:
        static std::map<std::string, mc_t *> mcMap;

        static void printMcache(mc_t *mc, void (*print)(const char *, ...))
        {
            mc_info_t info = {0};
            mc_get_info(mc, &info);

            print("| %-8s | %7lu | %8lu | %3lu | %3lu | %3lu | %4lu | %8lu | %8lu | %8lu |",
                info.name, info.hmap.bcount, info.hmap.total, info.hmap.min, info.hmap.max,
                info.hmap.avg, info.item.size, info.item.max, info.item.fcount, info.item.ucount);
        }

    public:

        static void Register(const char *name, mc_t *mc)
        {
            auto _md = mcMap.find(std::string(name));
            if (_md != mcMap.end())
            {
                log_error("mod(%s) already registered", name);
                return;
            }

            if (mcMap.empty())
            {
                cmd_register(MCSTAT_CMD, NULL, _mcstat_help, _mcstat_func);
            }

            mcMap[std::string(name)] = mc;
        }

        static void Unregister(const char *name)
        {
            auto _md = mcMap.find(std::string(name));
            if (_md != mcMap.end())
            {
                mcMap.erase(_md);
            }

            if (mcMap.empty())
            {
                cmd_unregister(MCSTAT_CMD);
            }
        }

        static void PrintAll(void (*print)(const char *, ...))
        {
            print("---------------------------------------------------------------------");
            print("| %-8s |             Hash Map            |           item            |", " ");
            print("|    Name    |-------------------------------------------------------");
            print(" %-8s | %7s | %8s | %3s | %3s | %3s | %4s | %8s | %8s |",
                    " ", "Bucket", "Total", "Min", "Max", "Avg", "Size", "Total", "Free");
            print("---------------------------------------------------------------------");

            for (auto iter = mcMap.begin(); iter != mcMap.end(); ++iter)
            {
                printMcache(iter->second, print);
            }

            print("---------------------------------------------------------------------");
        }
};


std::map<std::string, mc_t *> McstatMgr::mcMap;

/*************************************************************************
*************************************************************************/

static void _mcstat_help(void *nouse, void (*print)(const char *, ...))
{
    print("Usage: "
            "\t%-10s %-10s{help information}\n"
            "\t%-10s %-10s{get statistic data}\n",
            MCSTAT_CMD, "help", MCSTAT_CMD, "get");
}

static void _mcstat_func(void *nouse,
                            void (*print)(const char *, ...),
                            int argc,
                            argv_t argv)
{
    if (argc != MCSTAT_ARGC)
    {
        _mcstat_help(nouse, print);
        return;
    }

    if (0 == strcasecmp(argv[1], "get"))
    {
        McstatMgr::PrintAll(print);
        return;
    }

    _mcstat_help(nouse, print);
}

/*************************************************************************
*************************************************************************/

void mcstat_register(const char *name, mc_t *mc)
{
    McstatMgr::Register(name, mc);
}

void mcstat_unregister(const char *name)
{
    McstatMgr::Unregister(name);
}
