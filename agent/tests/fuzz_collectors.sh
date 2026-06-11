#!/usr/bin/env bash
# Robustness check: run the three collectors under ASan/UBSan, fire the
# hostile-input battery (fuzz_send.py) at them over IPv4 and IPv6, and assert
# that all three survive and no sanitizer error is reported.
#
# Build first with:  make clean && make SANITIZE=1
set -u
cd "$(dirname "$0")/.." || exit 2

export ASAN_OPTIONS=detect_leaks=0:halt_on_error=0
export UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=0

pkill -f 'pt_netflow|pt_ipfix|pt_sflow' 2>/dev/null
sleep 0.4

./pt_netflow -n -l 25002 >/tmp/fz_nf.out 2>&1 & NF=$!
./pt_ipfix   -n -l 4739  >/tmp/fz_ix.out 2>&1 & IX=$!
./pt_sflow   -n -l 6343  >/tmp/fz_sf.out 2>&1 & SF=$!
sleep 0.6

python3 tests/fuzz_send.py
sleep 1.2

rc=0
for pair in "netflow $NF fz_nf" "ipfix $IX fz_ix" "sflow $SF fz_sf"; do
    set -- $pair
    name=$1; pid=$2; log=/tmp/$3.out
    if kill -0 "$pid" 2>/dev/null; then
        echo "  $name: alive=YES"
    else
        echo "  $name: alive=NO (crashed/exited)"; rc=1
    fi
    if grep -qE 'AddressSanitizer|runtime error|ERROR: |SEGV' "$log"; then
        echo "  $name: SANITIZER REPORT:"; grep -E 'AddressSanitizer|runtime error|SEGV' "$log" | head -3; rc=1
    fi
done

kill "$NF" "$IX" "$SF" 2>/dev/null
if [ "$rc" -eq 0 ]; then echo "RESULT: PASS (all alive, no sanitizer errors)"; else echo "RESULT: FAIL"; fi
exit "$rc"
