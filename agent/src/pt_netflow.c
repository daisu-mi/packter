/*
 * pt_netflow — NetFlow v9 collector, forwards flows to the viewer.
 * Scaffolding and decode dispatch are shared (lib/collector.c) with
 * pt_sflow/pt_ipfix and with pt_agent -t.
 */
#include "packter.h"

int main(int argc, char *argv[])
{
    return packter_collector_run(argc, argv, PT_TRANS_NETFLOW,
                                 "NetFlow", PACKTER_NETFLOW_PORT, 0);
}
