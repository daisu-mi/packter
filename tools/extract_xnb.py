#!/usr/bin/env python3
"""Extract Texture2D assets from uncompressed XNA 3.1 .xnb files
(PackterViewer 2.4 distribution) into PNG.

Supports SurfaceFormat Color (BGRA8) and Dxt1. The 2.4 Content uses only
these two. Compiled binaries are treated as the authoritative assets.

usage: python extract_xnb.py <zip> <outdir>
"""
import io
import struct
import sys
import zipfile

from PIL import Image


def read_7bit(f):
    result = 0
    shift = 0
    while True:
        b = f.read(1)[0]
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result
        shift += 7


def read_string(f):
    n = read_7bit(f)
    return f.read(n).decode("utf-8")


def dxt1_decode(data, width, height):
    """Minimal DXT1 block decoder -> RGBA bytes."""
    out = bytearray(width * height * 4)
    bw = (width + 3) // 4
    bh = (height + 3) // 4
    pos = 0
    for by in range(bh):
        for bx in range(bw):
            c0, c1, bits = struct.unpack_from("<HHI", data, pos)
            pos += 8
            r0, g0, b0 = (c0 >> 11) << 3, ((c0 >> 5) & 0x3F) << 2, (c0 & 0x1F) << 3
            r1, g1, b1 = (c1 >> 11) << 3, ((c1 >> 5) & 0x3F) << 2, (c1 & 0x1F) << 3
            pal = [(r0, g0, b0, 255), (r1, g1, b1, 255)]
            if c0 > c1:
                pal.append(((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255))
                pal.append(((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255))
            else:
                pal.append(((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255))
                pal.append((0, 0, 0, 0))
            for py in range(4):
                for px in range(4):
                    x, y = bx * 4 + px, by * 4 + py
                    if x >= width or y >= height:
                        continue
                    idx = (bits >> (2 * (py * 4 + px))) & 0x3
                    o = (y * width + x) * 4
                    out[o:o + 4] = bytes(pal[idx])
    return bytes(out)


def extract_texture(data):
    """Return (PIL.Image, surface_format) or (None, reason)."""
    f = io.BytesIO(data)
    if f.read(3) != b"XNB":
        return None, "not xnb"
    platform = f.read(1)
    version = f.read(1)[0]
    flags = f.read(1)[0]
    struct.unpack("<I", f.read(4))
    if flags & 0x80:
        return None, "compressed (unsupported)"

    nreaders = read_7bit(f)
    readers = []
    for _ in range(nreaders):
        name = read_string(f)
        struct.unpack("<i", f.read(4))
        readers.append(name)
    read_7bit(f)                      # shared resources
    type_id = read_7bit(f)
    if type_id == 0 or "Texture2DReader" not in readers[type_id - 1]:
        return None, f"not a Texture2D ({readers[type_id - 1] if type_id else 'null'})"

    fmt = struct.unpack("<i", f.read(4))[0]
    width, height, mips = struct.unpack("<III", f.read(12))
    size = struct.unpack("<I", f.read(4))[0]
    pix = f.read(size)

    if fmt == 1:    # XNA 3.1 SurfaceFormat.Color (BGRA8)
        img = Image.frombytes("RGBA", (width, height), pix, "raw", "BGRA")
        return img, f"Color {width}x{height} mips={mips}"
    if fmt == 28:   # SurfaceFormat.Dxt1
        img = Image.frombytes("RGBA", (width, height), dxt1_decode(pix, width, height))
        return img, f"Dxt1 {width}x{height} mips={mips}"
    return None, f"unsupported SurfaceFormat {fmt} ({width}x{height})"


def main():
    zpath, outdir = sys.argv[1], sys.argv[2]
    import os
    os.makedirs(outdir, exist_ok=True)
    z = zipfile.ZipFile(zpath)
    for n in z.namelist():
        if not n.endswith(".xnb") or "/Content/" not in n:
            continue
        name = n.split("/")[-1][:-4]
        img, info = extract_texture(z.read(n))
        if img is None:
            print(f"{name:30s} SKIP: {info}")
            continue
        out = os.path.join(outdir, name + ".png")
        img.save(out)
        if img.size == (1, 1):
            r, g, b, a = img.getpixel((0, 0))
            info += f" -> #{r:02x}{g:02x}{b:02x} a={a}"
        print(f"{name:30s} {info}")


if __name__ == "__main__":
    main()
