#!/usr/bin/env python3
"""Generate ECHO OS core-pipeline test fixtures.

Every fixture in this directory is SYNTHETIC and fully deterministic: this script
uses a fixed PRNG seed and closed-form waveforms, so re-running it reproduces the
committed bytes exactly. Nothing here is a recording of a real person -- see
README.md for what each clip stands in for and why that is enough for the
stub-engine test harness.

Run from anywhere:  python make_fixtures.py
"""
import math
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 16000  # 16 kHz mono int16 -- the rate the wake-word/ASR path expects.


class Lcg:
    """Tiny deterministic PRNG (glibc LCG constants). We avoid `random` so the
    bytes never depend on the host Python's RNG implementation."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next_u32(self):
        self.state = (1103515245 * self.state + 12345) & 0x7FFFFFFF
        return self.state

    def uniform(self, lo, hi):
        return lo + (hi - lo) * (self.next_u32() / 0x7FFFFFFF)


def write_wav(name, samples):
    path = os.path.join(HERE, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", clamp16(s)) for s in samples))
    print(f"  wrote {name}: {len(samples)} samples ({os.path.getsize(path)} bytes)")


def clamp16(x):
    return max(-32768, min(32767, int(round(x))))


def voiced_phrase(seconds):
    """A voiced, speech-shaped waveform: a ~120 Hz glottal fundamental plus a few
    harmonics under a syllable-rate amplitude envelope. It is NOT intelligible
    speech -- it just carries speech-like energy (RMS well above the endpointer's
    silence floor) so the perception buffering/framing path processes it as an
    utterance would be processed. The stub ASR returns empty regardless."""
    n = int(SR * seconds)
    out = []
    f0 = 120.0
    harmonics = [(1, 1.0), (2, 0.5), (3, 0.33), (4, 0.2)]
    for i in range(n):
        t = i / SR
        # ~4 Hz syllable envelope, never fully closing, plus onset/offset fade.
        syl = 0.55 + 0.45 * (0.5 - 0.5 * math.cos(2 * math.pi * 4.0 * t))
        fade = min(1.0, t / 0.03, (seconds - t) / 0.03)
        s = sum(a * math.sin(2 * math.pi * f0 * k * t) for k, a in harmonics)
        out.append(6000.0 * syl * max(0.0, fade) * s)
    return out


def silence(seconds, rng):
    """Near-silence: only a tiny dither so the file isn't a degenerate all-zero
    block. RMS stays far below the endpointer's ~550 int16 silence floor."""
    n = int(SR * seconds)
    return [rng.uniform(-12, 12) for _ in range(n)]


def noise(seconds, rng):
    """Loud broadband noise -- the 'garbled / unintelligible' clip. High RMS but
    no voiced structure; exercises the path with hostile-shaped input."""
    n = int(SR * seconds)
    return [rng.uniform(-14000, 14000) for _ in range(n)]


def write_ppm(name, w, h, pixel):
    """Write a binary PPM (P6). Chosen because it is self-describing (dimensions
    in the header) and trivial to parse with no image library on the C++ side.
    The perception stub does not decode pixels, so the content only needs to be
    real-shaped (w*h*3 bytes); `pixel(x, y) -> (r, g, b)` defines it."""
    path = os.path.join(HERE, name)
    body = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b = pixel(x, y)
            body += bytes((r & 255, g & 255, b & 255))
    header = f"P6\n{w} {h}\n255\n".encode("ascii")
    with open(path, "wb") as f:
        f.write(header)
        f.write(body)
    print(f"  wrote {name}: {w}x{h} ({os.path.getsize(path)} bytes)")


def oval_face(cx, cy, rx, ry, skin, eye):
    """A crude centered 'face': a bright skin oval with two darker eye spots.
    Enough structure to stand in for a face image; the stub vision returns
    nothing regardless of content."""

    def px(x, y):
        dx, dy = (x - cx) / rx, (y - cy) / ry
        if dx * dx + dy * dy <= 1.0:
            for ex in (cx - rx * 0.4, cx + rx * 0.4):
                if (x - ex) ** 2 + (y - (cy - ry * 0.2)) ** 2 <= (rx * 0.15) ** 2:
                    return eye
            return skin
        return (24, 28, 36)  # dark background

    return px


def main():
    print("Generating ECHO OS core-pipeline fixtures (deterministic)...")

    # --- audio -----------------------------------------------------------------
    write_wav("speech_hello.wav", voiced_phrase(0.6))
    write_wav("silence.wav", silence(0.5, Lcg(1)))
    write_wav("noisy_garble.wav", noise(0.5, Lcg(7)))

    # --- images (32x32 BGR-shaped) --------------------------------------------
    W = H = 32
    write_ppm("face_enrolled.ppm", W, H,
              oval_face(16, 16, 11, 13, skin=(210, 180, 150), eye=(40, 40, 60)))
    write_ppm("face_unknown.ppm", W, H,
              oval_face(14, 17, 9, 12, skin=(150, 170, 200), eye=(30, 30, 30)))
    write_ppm("no_face.ppm", W, H,
              lambda x, y: (30 + y * 3, 60 + x * 2, 90))  # smooth gradient, no face

    print("Done.")


if __name__ == "__main__":
    main()
