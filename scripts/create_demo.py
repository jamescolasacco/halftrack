#!/usr/bin/env python3
"""Generate four original 8-bar stems. No external samples or packages."""
import array
import math
from pathlib import Path
import random
import sys
import wave

RATE = 48000
SECONDS = 16
BEAT = 0.5
N = RATE * SECONDS
TAU = 2 * math.pi


def write_stem(directory, name, sample):
    pcm = array.array("h")
    for i in range(N):
        t = i / RATE
        fade = min(1, t / 0.01, (SECONDS - t) / 0.08)
        left, right = sample(t)
        for value in (left, right):
            pcm.append(round(max(-0.95, min(0.95, value * fade)) * 32767))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(directory / (name + ".wav")), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: create_demo.py OUTPUT_DIRECTORY")
    directory = Path(sys.argv[1]).expanduser().resolve()
    directory.mkdir(parents=True, exist_ok=True)
    rng = random.Random(1701)

    def drums(t):
        beat = int(t / BEAT)
        local = t % BEAT
        kick = 0.35 * math.exp(-local * 20) * math.sin(TAU * (48 * local + 2 * (1 - math.exp(-local * 45))))
        noise = rng.uniform(-1, 1)
        snare = (0.14 * noise * math.exp(-local * 25) + 0.045 * math.sin(TAU * 180 * local) * math.exp(-local * 35)) if beat % 4 in (1, 3) else 0
        hat_time = t % (BEAT / 2)
        hat = 0.045 * noise * math.exp(-hat_time * 100)
        return kick + snare + hat, kick + snare + hat * 0.75

    def bass(t):
        pitches = (41.2034, 49.0, 36.7081, 43.6535)
        f = pitches[int(t / 4) % 4]
        local = t % BEAT
        envelope = min(1, local / 0.008) * math.exp(-local * 6)
        tone = 0.12 * envelope * (math.sin(TAU * f * t) + 0.22 * math.sin(TAU * f * 2 * t))
        return tone, tone

    def chords(t):
        roots = (164.8138, 195.9977, 146.8324, 174.6141)
        root = roots[int(t / 4) % 4]
        local = t % 2
        envelope = min(1, local / 0.025) * math.exp(-local * 1.8)
        ratios = (1, 2 ** (3 / 12), 2 ** (7 / 12), 2 ** (10 / 12))
        left = sum(math.sin(TAU * root * r * t) for r in ratios)
        right = sum(math.sin(TAU * root * r * 1.0007 * t) for r in ratios)
        return 0.034 * envelope * left, 0.034 * envelope * right

    air_rng = random.Random(291)
    def air(t):
        envelope = 0.5 + 0.5 * math.sin(TAU * t / 8)
        n = air_rng.uniform(-1, 1) * 0.006
        left = 0.024 * math.sin(TAU * 329.6276 * t + 0.8 * math.sin(TAU * 0.4 * t))
        right = 0.024 * math.sin(TAU * 493.8833 * t + 0.6 * math.sin(TAU * 0.3 * t))
        return (left + n) * envelope, (right + n) * envelope

    for name, fn in (("01 Pulse drums", drums), ("02 Warm bass", bass), ("03 Glass chords", chords), ("04 Tape air", air)):
        write_stem(directory, name, fn)
        print(name)

if __name__ == "__main__":
    main()
