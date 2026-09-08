#!/usr/bin/env python3
"""
Minimal ECHO sensor-node test listener.

Implements the laptop side of PROTOCOL.md well enough to prove the whole
pipeline works end to end: it accepts one sensor packet per touch cycle,
writes the JPEG and the audio to disk so you can actually look at / listen
to what the board captured, and sends the audio straight back as an
AUDIO_REPLY so the amp/speaker path gets exercised too.

This is a BRING-UP TEST RIG, not the real NetworkSensorSource. There is no
ASR, no processing, no auth, no TLS. It echoes audio back verbatim.

Usage:
    python test_server.py             # listen on 0.0.0.0:7890
    python test_server.py 9000        # listen on a different port

Captures land in ./captures/ as cycle_NNN.jpg and cycle_NNN.wav
"""

import os
import socket
import struct
import sys
import wave

HOST = "0.0.0.0"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 7890

MAGIC = b"ECH1"
MSG_SENSOR_PACKET = 0x01
MSG_AUDIO_REPLY = 0x02
MSG_ERROR = 0xFF

# Mirror of the device's MAX_FRAME_BYTES guard — PROTOCOL.md calls out that
# the receiver needs the same sanity check so a bad length prefix can't make
# us try to allocate something absurd.
MAX_FRAME_BYTES = 2 * 1024 * 1024

SAMPLE_RATE = 16000  # must match AUDIO_SAMPLE_RATE_HZ in main/config.h
OUT_DIR = "captures"


def recv_exactly(sock, n):
    """Read exactly n bytes or raise — TCP gives no guarantee per recv()."""
    chunks = []
    remaining = n
    while remaining > 0:
        chunk = sock.recv(min(remaining, 65536))
        if not chunk:
            raise ConnectionError(f"peer closed with {remaining} of {n} bytes left")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_length(sock, label):
    (length,) = struct.unpack(">I", recv_exactly(sock, 4))
    if length > MAX_FRAME_BYTES:
        raise ValueError(f"{label} = {length} exceeds {MAX_FRAME_BYTES} cap")
    return length


def save_wav(path, pcm_bytes):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)        # PCM16
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm_bytes)


def handle_cycle(conn, addr, cycle):
    magic = recv_exactly(conn, 4)
    if magic != MAGIC:
        print(f"  ! bad magic {magic!r} — not an ECHO client, dropping")
        return

    (msg_type,) = struct.unpack(">B", recv_exactly(conn, 1))
    if msg_type != MSG_SENSOR_PACKET:
        print(f"  ! unexpected msg_type 0x{msg_type:02x}, expected SENSOR_PACKET")
        conn.sendall(MAGIC + struct.pack(">B", MSG_ERROR))
        return

    image_len = read_length(conn, "image_len")
    image = recv_exactly(conn, image_len)

    audio_len = read_length(conn, "audio_len")
    audio = recv_exactly(conn, audio_len)

    seconds = audio_len / (SAMPLE_RATE * 2) if audio_len else 0
    print(f"  image: {image_len:,} bytes    audio: {audio_len:,} bytes ({seconds:.1f}s)")

    # A real JPEG starts with FFD8 and ends with FFD9. Worth checking, since
    # the boot log was showing "NO-SOI - JPEG start marker missing" earlier.
    if image[:2] == b"\xff\xd8":
        print("  jpeg: SOI marker OK", end="")
        print(", EOI marker OK" if image[-2:] == b"\xff\xd9" else ", EOI MISSING (truncated frame?)")
    else:
        print(f"  jpeg: BAD — starts with {image[:2].hex()}, expected ffd8")

    os.makedirs(OUT_DIR, exist_ok=True)
    jpg_path = os.path.join(OUT_DIR, f"cycle_{cycle:03d}.jpg")
    wav_path = os.path.join(OUT_DIR, f"cycle_{cycle:03d}.wav")
    with open(jpg_path, "wb") as f:
        f.write(image)
    save_wav(wav_path, audio)
    print(f"  saved: {jpg_path}  and  {wav_path}")

    # Echo the captured audio straight back, so the amp/speaker path gets
    # exercised. Swap this for real synthesized audio once there's something
    # generating it.
    reply = (
        MAGIC
        + struct.pack(">B", MSG_AUDIO_REPLY)
        + struct.pack(">I", SAMPLE_RATE)
        + struct.pack(">I", len(audio))
        + audio
    )
    conn.sendall(reply)
    print(f"  replied: {len(audio):,} bytes of PCM echoed back\n")


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(4)

    print(f"ECHO test listener up on {HOST}:{PORT}")
    print("Set SERVER_HOST in main/config.h to this machine's IP on the same")
    print("network as the board, then touch GPIO13 to trigger a cycle.\n")

    cycle = 0
    while True:
        conn, addr = srv.accept()
        cycle += 1
        print(f"[cycle {cycle}] connection from {addr[0]}:{addr[1]}")
        try:
            handle_cycle(conn, addr, cycle)
        except (ConnectionError, ValueError, struct.error) as e:
            print(f"  ! cycle failed: {e}\n")
        finally:
            conn.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nlistener stopped")
