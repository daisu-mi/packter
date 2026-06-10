#!/bin/sh
# Continuously replay the golden pcap to a broker (board-demo traffic).
# usage: ./loop_send.sh [iterations] [broker-ip]
N="${1:-90}"
HOSTIP="$2"
if [ -z "$HOSTIP" ]; then
    HOSTIP=$(ip route show default | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+')
fi
python3 make_pcap.py >/dev/null 2>&1
i=0
while [ "$i" -lt "$N" ]; do
    ../pt_agent -v "$HOSTIP" -B 50 -r test.pcap >/dev/null 2>&1
    sleep 0.8
    i=$((i + 1))
done
echo "loop_send done ($N iterations to $HOSTIP)"
