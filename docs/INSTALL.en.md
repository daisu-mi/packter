# Installation and Build

*[日本語](INSTALL.md) | English*

PACKTER 3.0 has three parts (broker / agent / web viewer). The web viewer needs
no build — just open it in a browser. (Its on-screen 3D rendering uses a library
called Three.js, which the page loads automatically from the internet when you
open it.)

## Build everything at once (recommended)

The broker (Rust/cargo) and the agent (C/autotools) use different languages and
toolchains, but the `Makefile` at the repository root drives both. On a host
that has **both the Rust toolchain (`cargo`) and GNU make**:

```sh
make            # build broker (release) + agent together
make broker     # broker only
make agent      # agent only (runs autogen -> configure if needed)
make check      # run both test suites
make install    # self-contained install under PREFIX (default /usr/local/packter)
make agent CONFIGURE_FLAGS=--with-geoip   # e.g. build with PACKTEARTH (GeoIP) enabled
```

`make install` lays everything out **under PREFIX (default `/usr/local/packter`)**
as one tree:

```
/usr/local/packter/
  bin/        packter-broker, pt_agent, pt_sflow, pt_netflow, pt_ipfix, pt_thmon, pt_replay
  share/web/  the viewer (static files the broker serves)
  etc/        packter.conf.sample (thmon config template)
```

After installing, start it with this one line (pass the viewer location):

```sh
/usr/local/packter/bin/packter-broker /usr/local/packter/share/web
```

Relocatable via `PREFIX=/opt/packter make install`; `DESTDIR` is honoured for
packaging. On \*BSD use GNU make (`gmake`). To build a single component, see the
sections below.

## Broker (Rust)

Requires: Rust 1.75+ (`cargo`).

```sh
cd broker
cargo build --release
# output: broker/target/release/packter-broker[.exe]
cd ..
broker/target/release/packter-broker        # serves ./web by default (127.0.0.1:11300)
```

Main options:

| Option | Default | Description |
|---|---|---|
| `<WEB_DIR>` | `./web` | Directory of the viewer's static files (relative to cwd) |
| `--bind <addr>` | **127.0.0.1** | Listen address (**secure-by-default = loopback**). To accept remote agents/browsers, set `0.0.0.0` or a specific IP explicitly. Applies to both UDP and TCP |
| `--udp <port>` | 11300 | Legacy UDP ingest port |
| `--http <port>` | 11300 | HTTP/WebSocket port (TCP; coexists with UDP 11300 since the protocols differ) |
| `--boards <N>` | auto | Number of boards (derived from rules if omitted, max 16) |
| `--agent <id>=<board>` | — | Assign an agent ID to a board |
| `--agent-key <id>=<pskfile>` | — | PSK for HMAC auth (read from file) |
| `--strict` | off | Drop unassigned sources (`--agent` becomes an allow-list) |
| `--require-auth` | off | Reject all anonymous/unauthenticated traffic |
| `--forward <ip:port>` | — | Forward received raw data to a legacy viewer etc. (the PACKTERAGENT line is stripped) |
| `--record <file>` | — | Record to JSONL |
| `--thmon <conf>` | — | Adaptive traffic monitoring (CUSUM+EWMA, legacy packter.conf compatible, `broker/packter.conf.sample`) |
| `--eve <file>` | — | Tail a Suricata EVE JSON file and ingest it |
| `--eve-board <N>` | 0 | Board for EVE-derived events |

> **secure-by-default**: the broker listens on **127.0.0.1 (loopback) only** by
> default. Set `--bind 0.0.0.0` (or a management IP) explicitly only when you
> need to accept remote agents or browsers. Exposing all interfaces prints a
> warning at startup, so run it on a trusted segment and gate control commands
> (PACKTERMSG/HTML) with `--require-auth`.

> When building on Windows with the Rust GNU toolchain, `tokio` is pinned to the
> 1.38 series (newer tokio pulls in a raw-dylib windows-sys that needs dlltool).

## Agent (C)

Requires: a C99 compiler + libpcap development headers. glib/OpenSSL are not
needed (MD5/SHA-256 are built in). The build uses **autoconf/automake (only to
generate `configure`; release tarballs ship `configure`, so it is not needed
there)**. `configure` detects libpcap, libm, libmaxminddb and platform
differences, so it builds as-is not only on Linux but on **\*BSD / macOS** too
(the GNU-make-only legacy Makefile was dropped).

> **Runtime libpcap dependency**: only `pt_agent` and `pt_thmon` (live capture)
> need libpcap (`libpcap.so` / `libpcap0.8` on Linux). `pt_sflow` / `pt_netflow`
> / `pt_ipfix` / `pt_replay` are pure UDP/file processors that do not link
> libpcap — i.e. **they run on hosts without libpcap installed** (`configure`
> emits the right links per tool). The broker (a single Rust exe) does not
> depend on libpcap at all.

```sh
cd agent
./autogen.sh                    # generate configure (only after a git clone; needs autoconf/automake)
./configure                     # detect libpcap/libm/libmaxminddb
make                            # pt_agent pt_sflow pt_netflow pt_ipfix pt_thmon pt_replay
make check                      # unit + golden (cross-checked against an independent Python impl)

# optional flags
./configure --with-geoip        # enable -G (PACKTEARTH); needs libmaxminddb
./configure --enable-sanitizer  # ASan/UBSan build
./configure --without-geoip     # explicitly disable GeoIP
```

Typical usage:

```sh
pt_agent  -v <broker> -i eth0                 # live capture
pt_agent  -v <broker> -r dump.pcap            # pcap replay
pt_agent  -v <broker> -A core-tap -K psk.txt  # authenticated (multi-host on one box / XSS hardening)
pt_agent  -v <broker> -B 50 -i eth0           # 50ms bulk send (saves bandwidth)
pt_agent  -v <broker> -i eth0 -t netflow "udp port 2055"  # translate: visualize captured NetFlow export
pt_sflow  -v <broker> -l 6343                 # sFlow v4 collector
pt_netflow -v <broker> -l 2055                # NetFlow v9 collector
pt_ipfix  -v <broker> -l 4739                 # IPFIX (v10) collector
pt_thmon  -v <broker> -i eth0                 # adaptive monitoring (CUSUM+EWMA, works untuned)
```

Run any tool with `-h` for all options.

### `-t` translate mode (interpret pcap contents as flow export)

`pt_agent -t {sflow|netflow|ipfix}` interprets the **UDP payload of captured
frames as a flow-export datagram**, decodes it exactly as the collectors
(pt_netflow etc.) do, and sends it to the broker. Useful for sniffing export UDP
on a SPAN/mirror port, or replaying a pcap that contains exports.

- **Use a BPF filter** to select the target UDP (e.g. `"udp port 2055"`). Version
  guards safely ignore mis-fed data, but a filter is the robust choice.
- Live (`-i`) capture **automatically raises snaplen to 65535** to grab whole
  datagrams. A `-r` pcap must have been **captured full-frame** (a headers-only
  pcap truncates and cannot be decoded).
- **netflow/ipfix cache templates across packets** (if data arrives before its
  template, the exporter's periodic resend recovers it naturally). sflow is
  stateless.
- **IP-fragmented exports are unsupported** (most exporters keep them under the
  MTU).
- Broker send and `-B`/`-A`/`-K`/`-R` work as usual. Combine with `-G` to also
  geolocate the IPs inside the flows (PACKTEARTH).

### Operational notes for the sFlow / NetFlow collectors

- `pt_sflow` parses **sFlow v4** only, `pt_netflow` **NetFlow v9** only, and
  `pt_ipfix` **IPFIX (v10)** only (other versions are ignored). NetFlow v9 and
  IPFIX share the template/data parsing core (`lib/nf_common.c`). Templates that
  contain IPFIX variable-length (0xFFFF) fields are currently not decoded
  (safely ignored).
- All three collectors' receive sockets are **dual-stack** (`AF_INET6` +
  `IPV6_V6ONLY=0`), but **secure-by-default the bind is `127.0.0.1` (loopback)**.
  To receive flows from real devices (routers/switches), set `-b <mgmt-ip>`
  (or `-b 0.0.0.0` / `-b ::`) explicitly. `-b` accepts a v4 or v6 literal (a v4
  is internally mapped to `::ffff:a.b.c.d`; `::` receives both v4 and v6). IPv6
  addresses *inside* a flow (NetFlow/IPFIX IE 27/28, sFlow sampled frames, IPv6
  in pcap) have always been supported, independent of the transport.
- If you expose it, limit it to a **trusted management segment** and drop
  privileges with `-u <user>`. `pt_netflow`'s template cache is unbounded, so a
  flood of forged templates from unknown sources can consume memory (no real
  harm if kept on the management plane). Do not expose it to the internet.

## Web viewer

No build needed; the broker serves it. To serve it directly, host `web/` on any
static server placed on the same origin so it can reach `ws://<host>:<port>/ws`.

- Public reverse proxy: you can keep the broker on its default
  `127.0.0.1:11300` (loopback) and front it with nginx etc. The viewer uses
  page-relative URLs (including the WebSocket) and follows the page scheme
  (https→`wss`), so it works both at the root and under a subpath (e.g.
  `/packter/`). The proxy just needs to forward `/` (viewer/REST) and `/ws`
  (WebSocket — the `Upgrade`/`Connection` handshake must be passed through) to
  11300.

- `web/config.json` … size, flag colors, board names, radius, terrain glTF, etc.
  (optional; every key may be omitted)
- Alternate layout: `http://<broker>:11300/?config=<file>` loads a different config
- Globe view: `?mode=earth` (or `?config=config-earth.json`). Draws the lat/lon
  from PACKTEARTH (`pt_agent -G <MMDB>` or `sender.py --earth`) as great-circle
  arcs on a globe. The default texture is NASA Blue Marble (public domain, from a
  CDN). Replace it with any equirectangular image via `config`'s `earthTexture`.
  For an offline/self-contained setup, `earthStylize:true` colors the bundled
  coastline outline (ocean=blue / land=green / desert belt=sand, approximate).
  Build `-G` with `./configure --with-geoip` (libmaxminddb) and supply the
  **DB-IP "IP to City Lite" MMDB (CC BY 4.0, attribution required)**. MaxMind
  GeoLite2 is not redistributable and therefore not recommended.
  `web/assets/compiled/world_ga_worldmap_*.png` are assets from the old Packter
  (CC BY).
- Getting the MMDB: `tools/fetch-geoip.sh [out.mmdb]` downloads DB-IP Lite (a
  direct download, no registration/contract). **The build/CI does not fetch it
  automatically and does not bundle the data** — running it is an optional manual
  step. To download by hand: <https://db-ip.com/db/download/ip-to-city-lite>.
  **Because it is CC BY 4.0, an "IP geolocation by DB-IP" credit (linking to
  db-ip.com) is required wherever data-derived output is shown.**
- Internet access: the on-screen 3D library Three.js 0.160 is loaded at display
  time from an external distribution site (jsDelivr). For an environment with no
  internet, put the Three.js files in `web/` and point the page at them instead.

## Verification

```sh
# broker unit tests
cd broker && cargo test          # 27 tests

# agent
cd agent && ./configure && make check   # unit + golden (plain/traceback/bulk/auth)

# end-to-end (manual): start the broker -> sender.py -> browser
```
