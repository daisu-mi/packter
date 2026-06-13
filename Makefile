# PACKTER top-level build.
#
# The two native components use different toolchains — the broker is Rust
# (cargo), the agent is C (autotools) — so this Makefile just drives each in
# its own directory. The web viewer needs no build.
#
#   make            # build both: broker (release) + agent tools
#   make broker     # cargo build --release  in broker/
#   make agent      # (autogen if needed +) configure + make  in agent/
#   make check      # run both test suites
#   make install    # install agent tools + broker binary under PREFIX
#   make clean      # clean both
#   make distclean  # clean both + drop the agent's configured build
#
# Pass options through to the agent's configure, e.g. GeoIP / PACKTEARTH:
#   make agent CONFIGURE_FLAGS=--with-geoip
#
# Use GNU make (on *BSD it is `gmake`); cargo and the autotools-generated
# Makefile expect it.

CARGO           ?= cargo
MAKE            ?= make
PREFIX          ?= /usr/local
CONFIGURE_FLAGS ?=

.PHONY: all broker agent check install clean distclean

all: broker agent

broker:
	cd broker && $(CARGO) build --release

# configure is committed, so a fresh checkout configures straight away;
# autogen.sh only runs if configure is somehow absent. `sh` prefixes dodge the
# exec bit not surviving a checkout (same reason check-local uses `sh`).
agent:
	cd agent && \
	  if [ ! -f Makefile ]; then \
	    [ -f configure ] || sh autogen.sh; \
	    sh configure --prefix=$(PREFIX) $(CONFIGURE_FLAGS); \
	  fi && \
	  $(MAKE)

check:
	cd broker && $(CARGO) test
	cd agent && $(MAKE) check

# Agent tools go to $(PREFIX)/bin via automake; the broker is a single binary
# installed with cargo. The broker serves ./web relative to its working dir, so
# run it from a checkout (or copy web/ next to wherever you launch it).
install: all
	cd agent && $(MAKE) install
	$(CARGO) install --path broker --root $(PREFIX) --force

clean:
	-cd broker && $(CARGO) clean
	-cd agent && [ -f Makefile ] && $(MAKE) clean || true

distclean:
	-cd broker && $(CARGO) clean
	-cd agent && [ -f Makefile ] && $(MAKE) distclean || true
