#!/usr/bin/env python3
"""Generate placeholder SE files se0.wav..se9.wav (one per flag).

The legacy distribution never shipped seN.wav (PACKTERSE expected them in
the viewer working dir), so these synthesized blips fill the gap until the
AudioCraft regeneration pass (see web/assets/sound/RECIPES.md).

Each flag gets a distinct pitch; IPv6 flags (5-9) get a second harmonic.
"""
import math
import struct
import wave
import os

RATE = 22050
DUR = 0.12

BASE = [392, 523, 330, 587, 262, 392, 523, 330, 587, 262]  # G4 C5 E4 D5 C4...

outdir = os.path.join(os.path.dirname(__file__), "..", "web", "assets", "legacy")
for flag in range(10):
    f0 = BASE[flag]
    harmonic = flag >= 5
    n = int(RATE * DUR)
    samples = []
    for i in range(n):
        t = i / RATE
        env = math.exp(-t * 18)
        v = math.sin(2 * math.pi * f0 * t)
        if harmonic:
            v = 0.6 * v + 0.4 * math.sin(2 * math.pi * f0 * 1.5 * t)
        samples.append(int(max(-1, min(1, v * env)) * 32000))
    path = os.path.join(outdir, f"se{flag}.wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack(f"<{n}h", *samples))
    print(f"se{flag}.wav {f0}Hz{' +harm' if harmonic else ''}")
