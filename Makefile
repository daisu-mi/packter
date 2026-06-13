# PACKTER top-level build.
#
# The two native components use different toolchains — the broker is Rust
# (cargo), the agent is C (autotools) — so this Makefile just drives each in
# its own directory. The web viewer needs no build.
#
#   make            # build both: broker (release) + agent tools  (run as your user)
#   make broker     # cargo build --release  in broker/
#   make agent      # (autogen if needed +) configure + make  in agent/
#   make check      # run both test suites
#   sudo make install  # copy the built files under PREFIX (default /usr/local/packter):
#                   #   $(PREFIX)/bin/        -> packter-broker + pt_agent/pt_sflow/...
#                   #   $(PREFIX)/share/web/  -> the viewer the broker serves
#                   #   $(PREFIX)/etc/        -> packter.conf.sample
#                   # install does NOT build — run `make` first as your normal
#                   # user so cargo/cc never run as root.
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
PREFIX          ?= /usr/local/packter
DESTDIR         ?=
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

# Self-contained install: everything lands under $(PREFIX) (default
# /usr/local/packter) so it moves/removes as one tree. We copy the binaries that
# were already built rather than going through automake's install — that keeps
# placement fully under our control and identical on Linux/*BSD/macOS, and
# sidesteps automake's configure-time prefix. DESTDIR is honoured for
# staged/packaged installs.
#
# install does NOT build: build first as your normal user (`make`), then
# `sudo make install` only copies. This avoids running cargo/cc as root (which
# would leave root-owned target/ and pollute the root cargo cache). If the
# binaries are missing, install errors and tells you to build first.
# Then run:  $(PREFIX)/bin/packter-broker $(PREFIX)/share/web
AGENT_BINS = pt_agent pt_sflow pt_netflow pt_ipfix pt_thmon pt_replay

install:
	@test -x broker/target/release/packter-broker || { \
	  echo "error: broker not built. Run 'make' (as your normal user) first, then 'sudo make install'."; exit 1; }
	@for b in $(AGENT_BINS); do test -x agent/$$b || { \
	  echo "error: agent not built ($$b missing). Run 'make' (as your normal user) first, then 'sudo make install'."; exit 1; }; done
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/web $(DESTDIR)$(PREFIX)/etc
	cp broker/target/release/packter-broker $(DESTDIR)$(PREFIX)/bin/
	for b in $(AGENT_BINS); do cp agent/$$b $(DESTDIR)$(PREFIX)/bin/; done
	cp -R web/. $(DESTDIR)$(PREFIX)/share/web/
	cp broker/packter.conf.sample $(DESTDIR)$(PREFIX)/etc/packter.conf.sample
	@echo
	@echo "installed under $(PREFIX):"
	@echo "  bin/        packter-broker, $(AGENT_BINS)"
	@echo "  share/web/  viewer"
	@echo "  etc/        packter.conf.sample"
	@echo "run:  $(PREFIX)/bin/packter-broker $(PREFIX)/share/web"

clean:
	-cd broker && $(CARGO) clean
	-cd agent && [ -f Makefile ] && $(MAKE) clean || true

distclean:
	-cd broker && $(CARGO) clean
	-cd agent && [ -f Makefile ] && $(MAKE) distclean || true
