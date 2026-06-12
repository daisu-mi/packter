#!/usr/bin/env python3
"""Test traffic generator for the Packter broker.

Sends legacy Packter protocol datagrams (canonical bulk form:
one PACKTER header + N records per datagram).

usage: python sender.py [--host 127.0.0.1] [--port 11300] [--pps 300] [--batch 10]
"""
import argparse
import random
import socket
import time

FLAG_WEIGHTS = {0: 55, 1: 12, 2: 8, 3: 18, 4: 4, 5: 1, 8: 1, 9: 1}
FLAGS = list(FLAG_WEIGHTS)
WEIGHTS = list(FLAG_WEIGHTS.values())

SUBNETS = ["192.168.1", "192.168.2", "10.0.0", "172.16.5"]
SERVERS = ["10.0.0.80", "10.0.0.25", "10.0.0.53", "172.16.5.10"]


def random_record():
    flag = random.choices(FLAGS, WEIGHTS)[0]
    src = f"{random.choice(SUBNETS)}.{random.randint(2, 254)}"
    dst = random.choice(SERVERS) if random.random() < 0.7 else \
        f"{random.choice(SUBNETS)}.{random.randint(2, 254)}"
    sport = random.randint(1024, 65535)
    dport = random.choice([80, 443, 53, 25, 22, random.randint(1024, 65535)])
    return f"{src},{dst},{sport},{dport},{flag},test traffic"


# (name, lat, lon) for the PACKTEARTH globe view
CITIES = [
    ("Tokyo", 35.68, 139.77), ("New York", 40.71, -74.01), ("London", 51.51, -0.13),
    ("Moscow", 55.75, 37.62), ("Sydney", -33.87, 151.21), ("Sao Paulo", -23.55, -46.63),
    ("Beijing", 39.90, 116.40), ("Nairobi", -1.29, 36.82), ("Los Angeles", 34.05, -118.24),
    ("Mumbai", 19.08, 72.88), ("Cape Town", -33.92, 18.42), ("Reykjavik", 64.15, -21.94),
]


def earth_record():
    s = random.choice(CITIES)
    d = random.choice(CITIES)
    flag = random.choices(FLAGS, WEIGHTS)[0]
    return f"{s[1]},{s[2]},{d[1]},{d[2]},{flag},{s[0]}->{d[0]}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11300)
    ap.add_argument("--pps", type=int, default=300, help="records per second")
    ap.add_argument("--batch", type=int, default=10, help="records per datagram (bulk)")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = forever)")
    ap.add_argument("--earth", action="store_true",
                    help="send PACKTEARTH (lat/lon city pairs) for the globe view")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)
    interval = args.batch / args.pps
    header = "PACKTEARTH\n" if args.earth else "PACKTER\n"
    record = earth_record if args.earth else random_record
    sent = 0
    t0 = time.time()
    print(f"sending ~{args.pps} {'PACKTEARTH' if args.earth else 'records'}/s "
          f"in bulk datagrams of {args.batch} to {dest}")
    try:
        while True:
            payload = header + "\n".join(record() for _ in range(args.batch)) + "\n"
            sock.sendto(payload.encode(), dest)
            sent += args.batch
            if args.seconds and time.time() - t0 >= args.seconds:
                break
            time.sleep(interval)
    except KeyboardInterrupt:
        pass
    print(f"sent {sent} records in {time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
