# PACKTER Agent 3.0 — plain Makefile (replaces autotools)
# Build deps: gcc/clang + libpcap headers. No glib, no openssl.
# Optional: make GEOIP=1 adds the legacy libGeoIP lookup for -G.

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c99 -Wall -Wextra -D_DEFAULT_SOURCE -Iinclude -Ilib

# Only the live-capture tools (pt_agent, pt_thmon) need libpcap. The
# sFlow/NetFlow/IPFIX collectors and pt_replay are pure UDP/file readers, so
# they link nothing extra and run on hosts WITHOUT libpcap installed. We scope
# -lpcap to the tools that use it rather than relying on the linker's
# --as-needed default, which is not universal across toolchains.
LDLIBS   =
PCAP_LIBS = -lpcap

# GEOIP=1 enables -G (PACKTEARTH) via libmaxminddb against a DB-IP "IP to City
# Lite" MMDB (CC BY 4.0). lib/mesg.c references the lookup, so every sender
# binary pulls it in — add the lib to both the base and the capture link sets.
ifeq ($(GEOIP),1)
CFLAGS    += -DPACKTER_HAVE_GEOIP
LDLIBS    += -lmaxminddb
PCAP_LIBS += -lmaxminddb
endif

pt_agent pt_thmon tests/test_unit: LDLIBS = $(PCAP_LIBS)

ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

LIB_SRCS := $(wildcard lib/*.c)
LIB_OBJS := $(LIB_SRCS:.c=.o)

BINS := pt_agent pt_replay pt_sflow pt_netflow pt_ipfix pt_thmon

all: $(BINS)

libpackter.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

pt_%: src/pt_%.o libpackter.a
	$(CC) $(LDFLAGS) -o $@ $< libpackter.a $(LDLIBS)

%.o: %.c include/packter.h
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- tests ----------------------------------------------------------
test: unit golden

unit: tests/test_unit
	./tests/test_unit

tests/test_unit: tests/test_unit.c libpackter.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< libpackter.a $(LDLIBS)

golden: pt_agent
	cd tests && ./run_golden.sh

clean:
	rm -f $(BINS) libpackter.a lib/*.o src/*.o tests/test_unit
	rm -f tests/test.pcap tests/got*.txt tests/expected*.txt

.PHONY: all test unit golden clean
