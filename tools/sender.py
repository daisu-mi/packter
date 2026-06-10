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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11300)
    ap.add_argument("--pps", type=int, default=300, help="records per second")
    ap.add_argument("--batch", type=int, default=10, help="records per datagram (bulk)")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = forever)")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)
    interval = args.batch / args.pps
    sent = 0
    t0 = time.time()
    print(f"sending ~{args.pps} records/s in bulk datagrams of {args.batch} to {dest}")
    try:
        while True:
            payload = "PACKTER\n" + "\n".join(random_record() for _ in range(args.batch)) + "\n"
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
