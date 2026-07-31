#!/usr/bin/env python3
"""Generate ECHO OS noise-bed fixtures for the Phase 19 audio-robustness tests.

These are the *noise sources* that the real-engine robustness test mixes into the
clean (Piper-synthesized) speech and wake clips at several defined SNR levels. Like
every other fixture in this repo they are SYNTHETIC and fully deterministic — but
unlike make_fixtures.py, this generator uses INTEGER-ONLY arithmetic (no math.sin /
libm), so the output bytes are identical on every platform. That is what lets CI
regenerate the beds and verify them against the SHA-256s pinned in MANIFEST.md
(fail-closed, the same "checksum before use" discipline as every fetched asset).

Three representative beds (single-channel 16 kHz int16, the mic/ASR rate):

  hum.wav       — steady low-frequency hum + faint broadband hiss (a fan / AC / the
                  device itself). Mostly low-frequency energy, so it is the bed the
                  high-pass stage of the pre-processor should measurably help on.
  transient.wav — near-silence punctuated by short loud broadband bursts (a door, a
                  dish clatter, a dropped object). Non-stationary on purpose.
  babble.wav    — a buzzy, syllable-enveloped competing talker (someone else / a TV
                  in the background). Overlaps the wearer's speech spectrally, so it
                  is the hard case a single mic cannot clean up.

Run:  python make_noise_fixtures.py <out_dir> [seconds]
      (defaults: out_dir = ./noise_beds, seconds = 4.0)
"""
import os
import struct
import sys
import wave

SR = 16000  # 16 kHz mono int16 — the rate the wake-word/ASR path expects.


class Lcg:
    """Integer LCG (glibc constants) — deterministic on every platform."""

    def __init__(self, seed):
        self.state = seed & 0x7FFFFFFF

    def next(self):
        self.state = (1103515245 * self.state + 12345) & 0x7FFFFFFF
        return self.state

    def sym(self, amp):
        """A symmetric integer sample in [-amp, amp]."""
        return (self.next() % (2 * amp + 1)) - amp


def clamp16(x):
    return max(-32768, min(32767, int(x)))


def triangle(i, period, amp):
    """Integer triangle wave, amplitude +/-amp, given period in samples."""
    if period < 2:
        return 0
    x = i % period
    half = period // 2
    if x < half:
        v = -amp + (2 * amp * x) // half
    else:
        v = amp - (2 * amp * (x - half)) // half
    return v


def write_wav(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", clamp16(s)) for s in samples))
    print(f"  wrote {os.path.basename(path)}: {len(samples)} samples "
          f"({os.path.getsize(path)} bytes)")


def hum(n):
    """Steady 100 Hz triangle hum + a faint broadband hiss."""
    rng = Lcg(19001)
    period = SR // 100  # 100 Hz
    out = []
    for i in range(n):
        out.append(triangle(i, period, 2600) + rng.sym(280))
    return out


def transient(n):
    """Near-silent bed with periodic short loud broadband bursts."""
    rng = Lcg(19002)
    out = []
    burst_period = int(SR * 1.1)   # a clatter ~ every 1.1 s
    burst_len = int(SR * 0.06)     # 60 ms bursts
    for i in range(n):
        phase = i % burst_period
        if phase < burst_len:
            # Loud burst with a quick linear decay envelope.
            env = (burst_len - phase) * 12000 // burst_len
            out.append(rng.sym(env))
        else:
            out.append(rng.sym(90))  # quiet room floor between clatters
    return out


def babble(n):
    """A competing talker: a buzzy ~150 Hz sawtooth-ish tone under a ~4 Hz syllable
    envelope, plus a little broadband so it isn't a pure tone. Integer-only."""
    rng = Lcg(19003)
    period = SR // 150            # 150 Hz fundamental (distinct from the ASR clip)
    syl_period = SR // 4          # ~4 Hz syllable rate
    out = []
    for i in range(n):
        # Triangle syllable envelope in [0, 1000], never fully closing.
        env = 350 + triangle(i, syl_period, 650) + 650  # ranges ~350..1650
        carrier = triangle(i, period, 4200) + triangle(i, period // 2, 1400)
        out.append((carrier * env) // 1650 + rng.sym(120))
    return out


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "noise_beds")
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
    n = int(SR * seconds)
    os.makedirs(out_dir, exist_ok=True)

    print(f"Generating ECHO OS noise beds ({seconds:.1f}s @ {SR} Hz, deterministic) "
          f"into {out_dir} ...")
    write_wav(os.path.join(out_dir, "hum.wav"), hum(n))
    write_wav(os.path.join(out_dir, "transient.wav"), transient(n))
    write_wav(os.path.join(out_dir, "babble.wav"), babble(n))
    print("Done.")


if __name__ == "__main__":
    main()
