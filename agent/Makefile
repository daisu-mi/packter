# PACKTER Agent 3.0 — plain Makefile (replaces autotools)
# Build deps: gcc/clang + libpcap headers. No glib, no openssl.
# Optional: make GEOIP=1 adds the legacy libGeoIP lookup for -G.

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c99 -Wall -Wextra -D_DEFAULT_SOURCE -Iinclude -Ilib
LDLIBS   = -lpcap

ifeq ($(GEOIP),1)
CFLAGS  += -DPACKTER_HAVE_GEOIP
LDLIBS  += -lGeoIP
endif

ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

LIB_SRCS := $(wildcard lib/*.c)
LIB_OBJS := $(LIB_SRCS:.c=.o)

BINS := pt_agent pt_replay pt_sflow pt_netflow pt_thmon

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
