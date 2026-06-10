#!/usr/bin/env python3
"""Craft tests/test.pcap AND the expected pt_agent output.

This is an independent reimplementation of the record-formatting rules
(from docs and the 2.5 sources) — it does not share code with the C
implementation, so a diff between `pt_agent -n -r test.pcap` and the
generated expectation cross-checks both.

Outputs:
  test.pcap        - ethernet pcap with TCP/UDP/ICMP, VLAN, IPv6 packets
  expected.txt     - records for `pt_agent -n`
  expected_tb.txt  - records for `pt_agent -n -T 172.16.45.1` (hash mode)
"""
import hashlib
import socket
import struct

OUT_PCAP = "test.pcap"
TRACE_SERVER = "172.16.45.1"


def eth(src, dst, ethertype, payload, vlan=None):
    hdr = bytes.fromhex(dst) + bytes.fromhex(src)
    if vlan is not None:
        hdr += struct.pack(">HH", 0x8100, vlan)
    hdr += struct.pack(">H", ethertype)
    return hdr + payload


def ipv4(src, dst, proto, payload, tos=0, ttl=64, ident=0x1234):
    total = 20 + len(payload)
    hdr = struct.pack(">BBHHHBBH4s4s", 0x45, tos, total, ident, 0x4000,
                      ttl, proto, 0, socket.inet_aton(src), socket.inet_aton(dst))
    # checksum left zero (agent does not verify)
    return hdr + payload


def ipv6(src, dst, nxt, payload, hlim=64, tclass=0):
    vtcfl = (6 << 28) | (tclass << 20)
    hdr = struct.pack(">IHBB", vtcfl, len(payload), nxt, hlim)
    hdr += socket.inet_pton(socket.AF_INET6, src)
    hdr += socket.inet_pton(socket.AF_INET6, dst)
    return hdr + payload


def tcp(sport, dport, flags, payload=b""):
    return struct.pack(">HHIIBBHHH", sport, dport, 0x1000, 0x2000,
                       0x50, flags, 0xffff, 0, 0) + payload


def udp(sport, dport, payload=b"x" * 8):
    return struct.pack(">HHHH", sport, dport, 8 + len(payload), 0) + payload


def icmp(typ, code, payload=b"\x00" * 6):
    return struct.pack(">BB", typ, code) + payload


def ip4_traceback_hash(ip_packet: bytes) -> str:
    """Reimplements the 2.5 hash: zero tos/len/off/ttl/sum (+options),
    md5 over header + 8 payload bytes."""
    b = bytearray(ip_packet)
    hdrlen = (b[0] & 0x0F) * 4
    if hdrlen > 20:
        for i in range(20, hdrlen):
            b[i] = 0
    b[1] = 0                      # tos
    b[2] = b[3] = 0               # total length
    b[6] = b[7] = 0               # frag offset
    b[8] = 0                      # ttl
    b[10] = b[11] = 0             # checksum
    return hashlib.md5(bytes(b[: hdrlen + 8])).hexdigest()


def ip6_traceback_hash(ip6_packet: bytes) -> str:
    """2.5 quirk: only byte0 masked to version nibble, hop limit zeroed."""
    b = bytearray(ip6_packet)
    b[0] &= 0xF0
    b[7] = 0                      # hop limit
    return hashlib.md5(bytes(b[: 40 + 8])).hexdigest()


SRC_MAC = "aabbccddee01"
DST_MAC = "aabbccddee02"

packets = []   # (frame_bytes, expected_plain_record or None, expected_tb_record or None)


def record(src, dst, d1, d2, flag, desc):
    return f"PACKTER\n{src},{dst},{d1},{d2},{flag},{desc}\n"


def add_ip4(proto_payload, proto, src, dst, plain_desc, d1, d2, flag, vlan=None):
    ippkt = ipv4(src, dst, proto, proto_payload)
    frame = eth(SRC_MAC, DST_MAC, 0x0800, ippkt, vlan=vlan)
    h = ip4_traceback_hash(ippkt)
    packets.append((
        frame,
        record(src, dst, d1, d2, flag, plain_desc),
        record(src, dst, d1, d2, flag, f"{h}-{TRACE_SERVER}"),
    ))


def add_ip6(proto_payload, nxt, src, dst, plain_desc, d1, d2, flag):
    ippkt = ipv6(src, dst, nxt, proto_payload)
    frame = eth(SRC_MAC, DST_MAC, 0x86DD, ippkt)
    h = ip6_traceback_hash(ippkt)
    packets.append((
        frame,
        record(src, dst, d1, d2, flag, plain_desc),
        record(src, dst, d1, d2, flag, f"{h}-{TRACE_SERVER}"),
    ))


# 1. TCP SYN  -> flag 1
add_ip4(tcp(49152, 80, 0x02), 6, "192.168.1.10", "10.0.0.80",
        "TCP src:192.168.1.10(49152) dst:10.0.0.80(80)", 49152, 80, 1)
# 2. TCP ACK  -> flag 0
add_ip4(tcp(49152, 80, 0x10), 6, "192.168.1.10", "10.0.0.80",
        "TCP src:192.168.1.10(49152) dst:10.0.0.80(80)", 49152, 80, 0)
# 3. TCP FIN+ACK -> flag 2
add_ip4(tcp(49152, 80, 0x11), 6, "192.168.1.10", "10.0.0.80",
        "TCP src:192.168.1.10(49152) dst:10.0.0.80(80)", 49152, 80, 2)
# 4. TCP RST -> flag 2
add_ip4(tcp(80, 49152, 0x04), 6, "10.0.0.80", "192.168.1.10",
        "TCP src:10.0.0.80(80) dst:192.168.1.10(49152)", 80, 49152, 2)
# 5. UDP -> flag 3
add_ip4(udp(53000, 53), 17, "192.168.1.10", "10.0.0.53",
        "UDP src:192.168.1.10(53000) dst:10.0.0.53(53)", 53000, 53, 3)
# 6. ICMP echo request (type 8) -> flag 4, data = type*256, code*256
add_ip4(icmp(8, 0), 1, "192.168.1.10", "10.0.0.1",
        "ICMPv4 src:192.168.1.10 dst:10.0.0.1 (type:8 code:0)", 8 * 256, 0, 4)
# 7. VLAN-tagged TCP ACK -> same as plain
add_ip4(tcp(50000, 443, 0x10), 6, "192.168.2.20", "10.0.0.25",
        "TCP src:192.168.2.20(50000) dst:10.0.0.25(443)", 50000, 443, 0, vlan=7)
# 8. IPv6 TCP SYN -> flag 6  (2.5 never emitted IPv6 records; fixed in 3.0)
add_ip6(tcp(1234, 80, 0x02), 6, "2001:db8::1", "2001:db8::2",
        "TCP src:2001:db8::1(1234) dst:2001:db8::2(80)", 1234, 80, 6)
# 9. IPv6 UDP -> flag 8
add_ip6(udp(5353, 53), 17, "2001:db8::1", "2001:db8::2",
        "UDP src:2001:db8::1(5353) dst:2001:db8::2(53)", 5353, 53, 8)
# 10. ICMPv6 echo request (type 128) -> flag 9
add_ip6(icmp(128, 0), 58, "2001:db8::1", "2001:db8::2",
        "ICMPv6 src:2001:db8::1 dst:2001:db8::2 (type:128 code:0)", 128 * 256, 0, 9)
# 11. non-IP frame (ARP): no output
packets.append((eth(SRC_MAC, DST_MAC, 0x0806, b"\x00" * 28), None, None))


def write_pcap(path):
    with open(path, "wb") as f:
        f.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        ts = 1700000000
        for i, (frame, _, _) in enumerate(packets):
            f.write(struct.pack("<IIII", ts + i, i * 1000, len(frame), len(frame)))
            f.write(frame)


write_pcap(OUT_PCAP)
with open("expected.txt", "w", newline="") as f:
    f.write("".join(r for _, r, _ in packets if r))
with open("expected_tb.txt", "w", newline="") as f:
    f.write("".join(r for _, _, r in packets if r))
print(f"wrote {OUT_PCAP} ({len(packets)} frames), expected.txt, expected_tb.txt")
