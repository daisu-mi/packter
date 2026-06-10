#!/bin/sh
# Golden cross-check: pt_agent output vs an independent Python
# reimplementation of the record rules. Run from agent/tests.
set -e

python3 make_pcap.py

echo "--- plain mode ---"
../pt_agent -n -r test.pcap | grep -v '^TIME ' > got.txt
diff -u expected.txt got.txt && echo "golden(plain): PASS"

echo "--- traceback mode ---"
../pt_agent -n -T 172.16.45.1 -r test.pcap | grep -v '^TIME ' > got_tb.txt
diff -u expected_tb.txt got_tb.txt && echo "golden(traceback): PASS"

echo "--- bulk mode preserves records ---"
../pt_agent -n -B 1000 -r test.pcap | grep -v '^TIME \|^PACKTER$' > got_bulk.txt
grep -v '^PACKTER$' expected.txt > expected_bulk.txt
diff -u expected_bulk.txt got_bulk.txt && echo "golden(bulk): PASS"

frames_plain=$(grep -c '^PACKTER$' got.txt || true)
frames_bulk=$(../pt_agent -n -B 1000 -r test.pcap | grep -c '^PACKTER$' || true)
echo "datagrams: plain=$frames_plain bulk=$frames_bulk"
test "$frames_bulk" -lt "$frames_plain" && echo "golden(bulk-framing): PASS"
