/*
 * pt_ipfix — IPFIX (NetFlow v10) collector, forwards flows to the viewer.
 * Scaffolding and decode dispatch are shared (lib/collector.c) with
 * pt_sflow/pt_netflow and with pt_agent -t.
 */
#include "packter.h"

int main(int argc, char *argv[])
{
    return packter_collector_run(argc, argv, PT_TRANS_IPFIX,
                                 "IPFIX", PACKTER_IPFIX_PORT, 0);
}
