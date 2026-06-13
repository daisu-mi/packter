/*
 * pt_sflow — sFlow v4 collector, forwards sampled frames to the viewer.
 * All scaffolding (argv, viewer connect, dual-stack listen, recv loop) and the
 * decode dispatch live in lib/collector.c, shared with pt_netflow/pt_ipfix and
 * with pt_agent -t. sFlow advertises -T (the sampled frame is a real packet).
 */
#include "packter.h"

int main(int argc, char *argv[])
{
    return packter_collector_run(argc, argv, PT_TRANS_SFLOW,
                                 "sFlow", PACKTER_SFLOW_PORT, 1 /* has -T */);
}
