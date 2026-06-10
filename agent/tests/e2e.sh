#!/bin/sh
# Send the golden pcap through pt_agent to a live broker (e2e smoke).
# usage: ./e2e.sh [broker-ip]   (default: WSL2 -> Windows host gateway)
set -e
HOSTIP="$1"
if [ -z "$HOSTIP" ]; then
    HOSTIP=$(ip route show default | awk '{print $3}')
fi
echo "broker host: $HOSTIP"
python3 make_pcap.py >/dev/null
../pt_agent -v "$HOSTIP" -B 50 -r test.pcap
echo "SENT-OK"
