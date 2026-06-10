#!/usr/bin/env python3
"""Convert a text-format DirectX .x mesh (legacy Packter Content) into a
minimal JSON geometry {positions, indices, normals?, color?} for the web
viewer. Handles the single-Mesh files produced for Packter 2.x (ball.x).

usage: python convert_x_mesh.py in.x out.json
"""
import json
import re
import sys


def tokens_after(text, start):
    """Yield numeric tokens from `start`, skipping separators."""
    for m in re.finditer(r"-?\d+(?:\.\d+)?", text[start:]):
        yield m.group(0)


def parse_block(text, name):
    i = text.find(name + " {")
    if i < 0:
        i = re.search(rf"{name}\s*\{{", text)
        if i is None:
            return None
        i = i.start()
    return i


def parse_mesh(text):
    i = re.search(r"^Mesh\s*\{", text, re.M).start()
    toks = tokens_after(text, i)
    nverts = int(next(toks))
    positions = []
    for _ in range(nverts):
        positions.extend([float(next(toks)), float(next(toks)), float(next(toks))])
    nfaces = int(next(toks))
    indices = []
    for _ in range(nfaces):
        n = int(next(toks))
        face = [int(next(toks)) for _ in range(n)]
        for k in range(1, n - 1):  # triangle fan
            indices.extend([face[0], face[k], face[k + 1]])
    return positions, indices


def parse_normals(text):
    m = re.search(r"MeshNormals\s*\{", text)
    if m is None:
        return None
    toks = tokens_after(text, m.start())
    n = int(next(toks))
    normals = []
    for _ in range(n):
        normals.extend([float(next(toks)), float(next(toks)), float(next(toks))])
    return normals


def parse_material_color(text):
    m = re.search(r"(?<!template )\bMaterial\s+[\w]*\s*\{", text)
    if m is None:
        return None
    toks = tokens_after(text, m.end())
    rgb = [float(next(toks)), float(next(toks)), float(next(toks))]
    if all(0.0 <= c <= 1.0 for c in rgb):
        return rgb
    return None


def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding="ascii", errors="replace").read()
    positions, indices = parse_mesh(text)
    out = {"positions": positions, "indices": indices}
    normals = parse_normals(text)
    if normals is not None and len(normals) == len(positions):
        out["normals"] = normals
    color = parse_material_color(text)
    if color is not None:
        out["color"] = color
    json.dump(out, open(dst, "w"), separators=(",", ":"))
    print(f"{dst}: {len(positions)//3} verts, {len(indices)//3} tris,"
          f" normals={'yes' if 'normals' in out else 'no'}, color={out.get('color')}")


if __name__ == "__main__":
    main()
