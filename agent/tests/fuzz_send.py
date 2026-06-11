#!/usr/bin/env python3
"""Hostile-input battery for the sFlow/NetFlow/IPFIX collectors.

Fires a mix of empty, truncated, wrong-version, bogus-length, absurd
field-count, variable-length, enterprise-bit and random-fuzz datagrams at
each collector over BOTH IPv4 and IPv6, to confirm the decoders never crash
or read out of bounds (run the collectors under ASan/UBSan).

usage: python fuzz_send.py [rounds]   (default 3000 random datagrams/shape)
"""
import os
import random
import socket
import struct
import sys

rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
random.seed(7)


def cases(hdr):
    c = [
        b"", b"\x00", os.urandom(3),
        struct.pack("!H", 99) + os.urandom(30),          # bogus version
        hdr,                                              # header only
        hdr + struct.pack("!HH", 0, 0),                   # set len 0
        hdr + struct.pack("!HH", 0, 3),                   # set len < 4
        hdr + struct.pack("!HH", 0, 0xffff),              # set claims huge
        hdr + struct.pack("!HHHH", 0, 8, 256, 0xffff),    # absurd field count
        hdr + struct.pack("!HHHH", 2, 12, 256, 1)         # varlen + enterprise
            + struct.pack("!HH", 0x8000 | 150, 0xffff),
        hdr + struct.pack("!HH", 257, 8) + os.urandom(4),  # data, unknown tmpl
    ]
    for _ in range(rounds):
        c.append(hdr + os.urandom(random.randint(0, 63)))
    for _ in range(rounds):
        c.append(os.urandom(random.randint(0, 80)))
    return c


def send(port, hdr):
    payloads = cases(hdr)
    for fam, host in ((socket.AF_INET, "127.0.0.1"), (socket.AF_INET6, "::1")):
        s = socket.socket(fam, socket.SOCK_DGRAM)
        for p in payloads:
            s.sendto(p, (host, port))
        s.close()


send(25002, struct.pack("!HHIIII", 9, 0, 0, 0, 0, 0))    # NetFlow v9 header
send(4739,  struct.pack("!HHIII", 10, 16, 0, 0, 0))      # IPFIX v10 header
send(6343,  struct.pack("!I", 4) + os.urandom(20))       # sFlow v4 header
print("battery sent (v4+v6) to 25002/4739/6343")
