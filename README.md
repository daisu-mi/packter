# PACKTER 3.0

*English | [日本語](README.ja.md)*

**PACKTER** shows internet traffic and cyber attacks as a real-time 3D picture.
Small programs ("agents") watch network traffic, and your browser draws each
packet as a flying object. This is the modernized version of PACKTER, first
released in 2008.

![PACKTER architecture: agents send over UDP (PACKTER protocol) to the broker; the broker serves the browser over HTTP/WebSocket](docs/img/architecture.svg)

## Parts

- **broker/** — the broker, written in Rust. It receives traffic in the old
  PACKTER format (UDP 11300), batches the flying objects every 33 ms, and sends
  them to the browser over a WebSocket. One single program also serves the web
  viewer, keeps the last 5 minutes so you can rewind (and lets late viewers
  catch up), records to a file, watches for traffic spikes (thmon), reads
  Suricata EVE alerts, and authenticates agents.
- **agent/** — the agents, written in C (`pt_agent` / `pt_sflow` / `pt_netflow`
  / `pt_ipfix` / `pt_thmon` / `pt_replay`). A full rewrite of PackterAgent 2.5.
  The only dependency is libpcap. The sFlow/NetFlow/IPFIX collectors handle both
  IPv4 and IPv6.
- **web/** — the web viewer (uses the Three.js 3D library). Colored balls fly
  from the agent walls to the receiver wall. Supports several wall layouts
  (a triangle to a hexagon seen from above), rewind, click-to-select, pop-up
  messages, sound, sky backdrop swapping, and saving a PNG.
- **tools/** — a test traffic generator (`sender.py`) and asset-conversion scripts.
- **docs/** — install guide and layout diagrams.

## Quick start

```sh
# 0) build everything (broker via cargo + agent via autotools; needs cargo and GNU make)
make

# 1) start the broker (receives on UDP 11300, serves the viewer at http://localhost:11300/)
broker/target/release/packter-broker  web

# 2) point an agent at it (real traffic)
agent/pt_agent -v <broker IP> -i eth0

#    or send test traffic
python tools/sender.py --pps 300
```

Open `http://localhost:11300/` in your browser.

## Multiple agents (several walls)

The broker assigns each agent to a wall, and the viewer arranges the walls in a
ring on the ground (seen from above, a polygon with the receiver at the top).

```sh
packter-broker web --boards 4 \
  --agent border-fw=1 --agent dmz-sflow=2 --agent core-tap=3
```

Wall numbers are **0 = receiver (the target, fixed) / 1 = sender / 2.. = agent2,
agent3, …**. An agent that introduces itself with `pt_agent -A <id>` becomes the
caption on its wall.

| Walls | Shape | Example |
|---|---|---|
| 2 | facing | sender / receiver |
| 3 | triangle | ![3](docs/img/3board-flow.png) |
| 4 | square | ![4](docs/img/4board-flow.png) |
| 5 | pentagon | ![5](docs/img/5board-flow.png) |
| 6 | hexagon | ![6](docs/img/6board-flow.png) |

## Globe view (PACKTEARTH)

A mode that places source and destination by **latitude/longitude** and flies
each attack as an **arc over a globe** wrapped in a world-map image. Start it
with `http://<broker>:11300/?mode=earth` (or `?config=config-earth.json`).

```sh
pt_agent -v <broker> -i eth0 -G dbip-city-lite.mmdb   # turn IPs into lat/lon (needs ./configure --with-geoip)
python tools/sender.py --earth                         # test traffic between cities
```

For the location data we recommend **DB-IP "IP to City Lite" (CC BY 4.0)**. It
is redistributable, **but you must show a credit (a link to DB-IP.com)**.
MaxMind GeoLite2 is not redistributable, so it is not recommended.

## Viewer controls

`S` = stop / `C` = back to live / `B`,`F` = step a frame / `Backspace` = -5 min /
slider = scrub / `Space` = toggle the on-screen info / `1`-`9` = hide a wall /
`P` = save a PNG / click = select a flying object / drag = rotate the view.

## Documentation

- Build and run (broker / agent / viewer): [English](docs/INSTALL.en.md) / [日本語](docs/INSTALL.md)
- Wire protocol (the UDP messages agents send): [docs/PROTOCOL.md](docs/PROTOCOL.md)

## Compatibility

The broker's parser accepts all of the following (lenient input), so **an
existing PackterAgent 2.5 works as-is, with no changes**.

- `PACKTER\n` followed by a list of records (the normal bulk form) / repeated
  pairs / `PACKTER <record>` (one line)
- the old `PACTER` header, `PACKTERBALLISTIC`, `PACKTERWITHGATEWAY`,
  `PACKTEARTH` (GeoIP)
- control messages: `PACKTERMSG` / `PACKTERHTML` / `PACKTERSE` / `PACKTERSOUND` /
  `PACKTERVOICE` / `PACKTERSKYDOMETEXTURE`
- coordinate fields: IPv4 / IPv6 / normalized coordinates (0–1) / integers (1–65536)

## License

Code: BSD 2-Clause. The assets (sky backdrop, flag colors, wall textures, etc.)
come from the old PACKTER project and are under Creative Commons Attribution
(CC BY).
