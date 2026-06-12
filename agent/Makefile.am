# PACKTER Agent 3.0 — non-recursive automake build.
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/lib -D_DEFAULT_SOURCE
AM_CFLAGS   = -std=c99 -Wall -Wextra

# shared code, linked into every tool
noinst_LIBRARIES = libpackter.a
libpackter_a_SOURCES = \
	lib/capture.c lib/config.c lib/ctx.c lib/geoip.c lib/hash.c \
	lib/ipfix.c lib/map.c lib/md5.c lib/mesg.c lib/netflow.c \
	lib/nf_common.c lib/proto.c lib/send.c lib/sflow.c lib/sha256.c \
	lib/snort.c lib/util.c

bin_PROGRAMS = pt_agent pt_replay pt_sflow pt_netflow pt_ipfix pt_thmon

# default for the pure-UDP / file tools: lib + (optional) libmaxminddb only
LDADD = libpackter.a $(MMDB_LIBS)

# live-capture tools additionally need libpcap; pt_thmon also needs libm
pt_agent_SOURCES = src/pt_agent.c
pt_agent_LDADD   = libpackter.a $(PCAP_LIBS) $(MMDB_LIBS)
pt_thmon_SOURCES = src/pt_thmon.c
pt_thmon_LDADD   = libpackter.a $(PCAP_LIBS) $(MATH_LIBS) $(MMDB_LIBS)

pt_replay_SOURCES  = src/pt_replay.c
pt_sflow_SOURCES   = src/pt_sflow.c
pt_netflow_SOURCES = src/pt_netflow.c
pt_ipfix_SOURCES   = src/pt_ipfix.c

# ---- tests (make check) ----------------------------------------------------
check_PROGRAMS = test_unit
test_unit_SOURCES = tests/test_unit.c
test_unit_LDADD   = libpackter.a $(PCAP_LIBS) $(MATH_LIBS) $(MMDB_LIBS)
TESTS = test_unit

# golden cross-check against the independent Python implementation
# (invoke via sh so it does not depend on the exec bit surviving checkout)
check-local: pt_agent$(EXEEXT)
	cd $(srcdir)/tests && sh run_golden.sh
