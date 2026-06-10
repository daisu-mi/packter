#!/usr/bin/env python3
"""Cross-language check of the PACKTERAGENT auth line.

Reads `pt_agent -n -A <id> -K <pskfile>` output from stdin, splits it into
datagrams at TIME lines, and independently verifies every HMAC-SHA256
with python's hmac module. Exits non-zero on any mismatch.

usage: pt_agent -n -A test-agent -K psk.txt -r test.pcap | python3 check_auth.py psk.txt test-agent
"""
import hmac
import hashlib
import sys
import time

pskfile, agent_id = sys.argv[1], sys.argv[2]
psk = open(pskfile, "rb").readline().strip()

datagrams = []
current = None
for line in sys.stdin.read().splitlines(keepends=True):
    if line.startswith("TIME "):
        if current is not None:
            datagrams.append(current)
        current = ""
    elif current is not None:
        current += line
if current:
    datagrams.append(current)

ok = bad = 0
now = int(time.time())
for d in datagrams:
    head, _, rest = d.partition("\n")
    if not head.startswith("PACKTERAGENT "):
        print(f"FAIL: datagram without agent line: {head[:60]!r}")
        bad += 1
        continue
    fields = head[len("PACKTERAGENT "):].split(",")
    if len(fields) != 3 or fields[0] != agent_id:
        print(f"FAIL: bad agent line: {head!r}")
        bad += 1
        continue
    ts = int(fields[1])
    if abs(now - ts) > 300:
        print(f"FAIL: timestamp out of window: {ts}")
        bad += 1
        continue
    mac = hmac.new(psk, f"{fields[0]},{ts}\n".encode() + rest.encode(), hashlib.sha256)
    if mac.hexdigest() != fields[2]:
        print(f"FAIL: hmac mismatch on datagram: {rest[:50]!r}")
        bad += 1
    else:
        ok += 1

print(f"auth check: {ok} datagrams verified, {bad} failures")
sys.exit(1 if bad or ok == 0 else 0)
