#!/usr/bin/env python3
"""
ECHO brain — the laptop side that makes the sensor node intelligent.

What one touch does:

  1. The board sends a photo, 5s of audio, and a list of the WiFi access
     points it can see (PROTOCOL.md sensor packet v2, msg_type 0x04).
  2. Whisper transcribes the audio locally.
  3. What happens next depends on what was said:

       "hello my name is Satyabrat"  -> enrol this face under that name
       "we are at the college lab"   -> name the place we're standing in
       "who is this guy"             -> identify + recall everything
       anything else                 -> identify, and log what was discussed

  4. The answer goes back to the board as display text (msg_type 0x03) and
     appears on the OLED, and simultaneously on http://localhost:8080.

Places are recognised by which WiFi routers are visible — no GPS needed,
and it works indoors where GPS would not. Key points are summarised by a
local Ollama model. Everything runs offline once the models are pulled.

    python echo_brain.py
"""

import html
import json
import os
import re
import shutil
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
import wave
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

# ----------------------------------------------------------------------
# Config
# ----------------------------------------------------------------------

HOST, PORT = "0.0.0.0", 7890
WEB_PORT = 8080

MAGIC = b"ECH1"
MSG_SENSOR_V1 = 0x01
MSG_AUDIO_REPLY = 0x02
MSG_RICH_REPLY = 0x03      # display text + audio
MSG_SENSOR_V2 = 0x04       # image + audio + AP fingerprint
MSG_ERROR = 0xFF
MAX_FRAME_BYTES = 2 * 1024 * 1024

SAMPLE_RATE = 16000
CAPTURE_DIR = "captures"
FACES_DIR = "faces"
MEMORY_FILE = "memory.json"

WHISPER_MODEL = "base"
FACE_MODEL = "SFace"
FACE_DETECTOR = "opencv"
FACE_DISTANCE_MAX = 0.60

OLLAMA_URL = "http://localhost:11434"
OLLAMA_MODELS = ["llama3.2", "llama3.2:3b", "qwen2.5:3b", "phi3.5", "gemma2:2b"]

# The OLED is 21 characters by 8 lines. Everything sent for display is
# composed against that budget — there is no scrolling on a 0.96" screen.
OLED_COLS, OLED_ROWS = 21, 8

# Two places count as the same spot when this much of their strongest-AP
# sets overlap. 0.35 is deliberately forgiving: routers come and go between
# scans, phones hotspotting nearby appear and vanish, and being told the
# wrong room is better than being told nothing.
PLACE_MATCH_THRESHOLD = 0.35

INTENT_WHO = [
    r"\bwho is (this|that|he|she|they|him|her)\b",
    r"\bwho'?s (this|that|he|she)\b",
    r"\bdo (i|we) know (this|him|her|them)\b",
    r"\bwho am i (talking|speaking) to\b",
    r"\bremind me who\b",
]
INTENT_NAME = [
    r"\bmy name is\s+([a-z][a-z'\-]*(?:\s+[a-z][a-z'\-]*)?)",
    r"\bi am\s+([a-z][a-z'\-]*(?:\s+[a-z][a-z'\-]*)?)",
    r"\bi'm\s+([a-z][a-z'\-]*(?:\s+[a-z][a-z'\-]*)?)",
    r"\bthis is\s+([a-z][a-z'\-]*(?:\s+[a-z][a-z'\-]*)?)\s+(?:here|speaking)?$",
    r"\bcall me\s+([a-z][a-z'\-]*(?:\s+[a-z][a-z'\-]*)?)",
]
INTENT_PLACE = [
    r"\bwe(?:'re| are)? (?:at|in)\s+(?:the\s+)?([a-z][a-z0-9'\- ]{1,28})",
    r"\bthis (?:place )?is\s+(?:the\s+)?([a-z][a-z0-9'\- ]{1,28})\s*(?:lab|office|room|cafe|campus|home)\b",
    r"\bremember this place as\s+(?:the\s+)?([a-z][a-z0-9'\- ]{1,28})",
    r"\bi(?:'m| am) (?:at|in)\s+(?:the\s+)?([a-z][a-z0-9'\- ]{1,28})",
]

NOT_NAMES = {
    "a", "an", "the", "here", "there", "fine", "good", "ok", "okay", "not",
    "going", "doing", "sorry", "very", "just", "so", "really", "back",
    "ready", "done", "sure", "happy", "tired", "hungry", "testing", "test",
    "talking", "speaking", "trying", "looking", "working", "thinking",
    "thanks", "thank", "glad", "great", "well", "still", "also", "now",
}

# ----------------------------------------------------------------------
# Memory — people, places, and what was said
# ----------------------------------------------------------------------

MEMORY = {"people": {}, "places": []}
MEM_LOCK = threading.Lock()


def load_memory():
    global MEMORY
    if os.path.exists(MEMORY_FILE):
        try:
            with open(MEMORY_FILE, "r", encoding="utf-8") as f:
                MEMORY = json.load(f)
            MEMORY.setdefault("people", {})
            MEMORY.setdefault("places", [])
        except (OSError, ValueError) as e:
            print(f"  ! could not read {MEMORY_FILE} ({e}) — starting fresh")


def save_memory():
    tmp = MEMORY_FILE + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(MEMORY, f, indent=2, ensure_ascii=False)
        os.replace(tmp, MEMORY_FILE)      # atomic, so a crash can't corrupt it
    except OSError as e:
        print(f"  ! could not save memory: {e}")


def now_iso():
    return datetime.now(timezone.utc).isoformat()


def human_when(iso):
    """'23 Aug, 5 days ago' — short enough for a 21-column display."""
    try:
        then = datetime.fromisoformat(iso)
    except (TypeError, ValueError):
        return ""
    if then.tzinfo is None:
        then = then.replace(tzinfo=timezone.utc)
    days = (datetime.now(timezone.utc) - then).days
    stamp = then.astimezone().strftime("%d %b")
    if days <= 0:
        return f"{stamp}, today"
    if days == 1:
        return f"{stamp}, yesterday"
    if days < 30:
        return f"{stamp}, {days}d ago"
    return stamp


# ----------------------------------------------------------------------
# Places, by WiFi fingerprint
# ----------------------------------------------------------------------

def jaccard(a, b):
    a, b = set(a), set(b)
    if not a or not b:
        return 0.0
    return len(a & b) / len(a | b)


def match_place(bssids):
    """Which known place do these visible routers correspond to?"""
    best, best_score = None, 0.0
    with MEM_LOCK:
        for place in MEMORY["places"]:
            score = jaccard(bssids, place.get("bssids", []))
            if score > best_score:
                best, best_score = place, score
    if best and best_score >= PLACE_MATCH_THRESHOLD:
        return best["name"], best_score
    return None, best_score


def remember_place(name, bssids):
    """Name the spot we're standing in. Updating an existing place merges
    the router sets, so a place slowly learns all the APs around it."""
    with MEM_LOCK:
        for place in MEMORY["places"]:
            if place["name"].lower() == name.lower():
                merged = set(place.get("bssids", [])) | set(bssids)
                place["bssids"] = sorted(merged)
                place["updated"] = now_iso()
                save_memory()
                return "updated"
        MEMORY["places"].append({
            "name": name,
            "bssids": sorted(set(bssids)),
            "created": now_iso(),
        })
        save_memory()
    return "created"


# ----------------------------------------------------------------------
# Whisper
# ----------------------------------------------------------------------

_whisper, _whisper_failed = None, False


def get_whisper():
    global _whisper, _whisper_failed
    if _whisper is not None or _whisper_failed:
        return _whisper
    try:
        from faster_whisper import WhisperModel
        print(f"  loading whisper '{WHISPER_MODEL}' (first run downloads it)...")
        _whisper = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")
        print("  whisper ready")
    except ImportError:
        _whisper_failed = True
        print("  ! faster-whisper not installed:  pip install faster-whisper")
    except Exception as e:
        _whisper_failed = True
        print(f"  ! whisper failed to load: {e}")
    return _whisper


def transcribe(wav_path):
    model = get_whisper()
    if model is None:
        return ""
    try:
        segments, _ = model.transcribe(wav_path, language="en", beam_size=1)
        return " ".join(s.text for s in segments).strip()
    except Exception as e:
        print(f"  ! transcription failed: {e}")
        return ""


# ----------------------------------------------------------------------
# Ollama — key points
# ----------------------------------------------------------------------

_ollama_model, _ollama_checked = None, False


def get_ollama_model():
    """Pick whichever small model is actually installed."""
    global _ollama_model, _ollama_checked
    if _ollama_checked:
        return _ollama_model
    _ollama_checked = True
    try:
        with urllib.request.urlopen(f"{OLLAMA_URL}/api/tags", timeout=3) as r:
            tags = json.load(r)
        installed = [m["name"] for m in tags.get("models", [])]
        if not installed:
            print("  ! ollama is running but has no models:  ollama pull llama3.2")
            return None
        for want in OLLAMA_MODELS:
            for have in installed:
                if have == want or have.startswith(want + ":"):
                    _ollama_model = have
                    print(f"  ollama model: {have}")
                    return _ollama_model
        _ollama_model = installed[0]
        print(f"  ollama model: {_ollama_model} (fallback)")
    except (urllib.error.URLError, OSError, ValueError):
        print("  ! ollama not reachable — key points will fall back to raw speech")
    return _ollama_model


def summarise(transcript):
    """Two or three very short bullets that fit a 21-column display."""
    if not transcript or len(transcript.split()) < 4:
        return []

    model = get_ollama_model()
    if model is None:
        # No LLM: take the longest sentence as the gist. Crude but honest.
        parts = [p.strip() for p in re.split(r"[.!?]", transcript) if p.strip()]
        return [max(parts, key=len)[:60]] if parts else []

    prompt = (
        "Summarise what was discussed in this conversation snippet as at most "
        "3 bullet points. Each bullet MUST be under 19 characters — they are "
        "displayed on a tiny screen. No punctuation at the end. Reply with "
        "ONLY the bullets, one per line, no numbering, no preamble.\n\n"
        f"Snippet: {transcript}"
    )
    body = json.dumps({
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {"temperature": 0.2, "num_predict": 80},
    }).encode()

    try:
        req = urllib.request.Request(
            f"{OLLAMA_URL}/api/generate", data=body,
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=45) as r:
            out = json.load(r).get("response", "")
    except (urllib.error.URLError, OSError, ValueError) as e:
        print(f"  ! ollama call failed: {e}")
        return []

    bullets = []
    for line in out.splitlines():
        line = line.strip().lstrip("-*•0123456789. ").strip()
        if line:
            bullets.append(line[:19])
    return bullets[:3]


# ----------------------------------------------------------------------
# Faces
# ----------------------------------------------------------------------

def _deepface():
    try:
        from deepface import DeepFace
        return DeepFace
    except ImportError:
        print("  ! deepface not installed:  pip install deepface tf-keras")
        return None
    except Exception as e:
        print(f"  ! deepface import failed: {e}")
        return None


def _invalidate_face_cache():
    if not os.path.isdir(FACES_DIR):
        return
    for n in os.listdir(FACES_DIR):
        if n.endswith(".pkl"):
            try:
                os.remove(os.path.join(FACES_DIR, n))
            except OSError:
                pass


def enrolled_names():
    if not os.path.isdir(FACES_DIR):
        return []
    return sorted(d for d in os.listdir(FACES_DIR)
                  if os.path.isdir(os.path.join(FACES_DIR, d)))


def enroll_face(name, jpg_path):
    DeepFace = _deepface()
    if DeepFace is None:
        return False, "deepface not installed"
    try:
        faces = DeepFace.extract_faces(img_path=jpg_path,
                                       detector_backend=FACE_DETECTOR,
                                       enforce_detection=True)
        if not faces:
            return False, "no face in the photo"
    except Exception:
        return False, "no face in the photo"

    d = os.path.join(FACES_DIR, name)
    os.makedirs(d, exist_ok=True)
    shutil.copyfile(jpg_path, os.path.join(d, f"{int(time.time())}.jpg"))
    _invalidate_face_cache()
    n = len([f for f in os.listdir(d) if f.endswith(".jpg")])
    return True, f"{n} photo(s) on file"


def identify_face(jpg_path):
    DeepFace = _deepface()
    if DeepFace is None:
        return None, "deepface not installed"
    if not enrolled_names():
        return None, "nobody enrolled yet"
    try:
        results = DeepFace.find(img_path=jpg_path, db_path=FACES_DIR,
                                model_name=FACE_MODEL,
                                detector_backend=FACE_DETECTOR,
                                enforce_detection=False, silent=True)
    except Exception as e:
        return None, f"lookup failed: {e}"

    frames = [df for df in results if df is not None and not df.empty]
    if not frames:
        return None, "no match"

    df = frames[0]
    col = next((c for c in df.columns if "distance" in c.lower()), None)
    if col is None:
        return None, "unexpected deepface result"

    best = df.sort_values(col).iloc[0]
    dist = float(best[col])
    if dist > FACE_DISTANCE_MAX:
        return None, f"match too weak ({dist:.2f})"
    return os.path.basename(os.path.dirname(str(best["identity"]))), dist


# ----------------------------------------------------------------------
# Intent
# ----------------------------------------------------------------------

def clean_name(raw):
    """Turn a captured phrase into a name, or None if it clearly isn't one.

    The first word decides it. 'I am fine thanks' must NOT enrol a person —
    filtering stop-words out of the middle would leave 'Thanks' and quietly
    create a bogus identity, so a non-name in first position rejects the
    whole match. Trailing junk is trimmed instead ('Bob and then' -> 'Bob')."""
    words = raw.strip().split()
    if not words or words[0] in NOT_NAMES:
        return None
    kept = []
    for w in words:
        if w in NOT_NAMES:
            break
        kept.append(w)
    if not kept:
        return None
    return " ".join(w.capitalize() for w in kept)


def detect_intent(transcript):
    """Returns (intent, argument). Order matters: an explicit question about
    identity beats a name pattern, so 'who is this, is it Satyabrat' asks
    rather than enrols."""
    if not transcript:
        return "identify", None
    text = transcript.lower()

    for p in INTENT_WHO:
        if re.search(p, text):
            return "who", None
    for p in INTENT_PLACE:
        m = re.search(p, text)
        if m:
            name = clean_name(m.group(1))
            if name:
                return "place", name
    for p in INTENT_NAME:
        m = re.search(p, text)
        if m:
            name = clean_name(m.group(1))
            if name:
                return "name", name
    return "identify", None


# ----------------------------------------------------------------------
# Display composition — hard budget of 21 x 8
# ----------------------------------------------------------------------

def fit(line, width=OLED_COLS):
    line = " ".join(line.split())
    return line if len(line) <= width else line[: width - 1] + "."


def compose(lines):
    return "\n".join(fit(l) for l in lines[:OLED_ROWS] if l is not None)


def person_card(name, place_now):
    """Everything the device knows about this person, squeezed onto 8 lines."""
    with MEM_LOCK:
        rec = MEMORY["people"].get(name, {})
        met_place = rec.get("first_met_place")
        met_when = rec.get("first_met")
        points = list(rec.get("key_points", []))
        seen = rec.get("encounters", 0)

    lines = [f"He is {name}"]

    if met_place:
        lines.append(f"Met at {met_place}")
    if met_when:
        w = human_when(met_when)
        if w:
            lines.append(w + (f" - {seen}x" if seen > 1 else ""))
    if place_now and place_now != met_place:
        lines.append(f"Now: {place_now}")

    if points:
        lines.append("Talked about:")
        for p in points[-3:]:
            lines.append(f"- {p}")
    return compose(lines)


# ----------------------------------------------------------------------
# Live page state
# ----------------------------------------------------------------------

STATE = {
    "headline": "Waiting for the first touch",
    "detail": "Touch the sensor and say: hello my name is Satyabrat",
    "transcript": "", "image": None, "cycle": 0, "when": "",
    "kind": "idle", "oled": "",
}
STATE_LOCK = threading.Lock()


def set_state(**kw):
    with STATE_LOCK:
        STATE.update(kw)
        STATE["when"] = datetime.now().strftime("%H:%M:%S")


def get_state():
    with STATE_LOCK:
        return dict(STATE)


# ----------------------------------------------------------------------
# Wire protocol
# ----------------------------------------------------------------------

def recv_exactly(sock, n):
    chunks, remaining = [], n
    while remaining > 0:
        chunk = sock.recv(min(remaining, 65536))
        if not chunk:
            raise ConnectionError(f"peer closed with {remaining} of {n} left")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_len(sock, label):
    (n,) = struct.unpack(">I", recv_exactly(sock, 4))
    if n > MAX_FRAME_BYTES:
        raise ValueError(f"{label}={n} exceeds cap")
    return n


def read_fingerprint(sock):
    """ap_count(u16), then bssid[6] + rssi(int8) + ssid_len(u8) + ssid."""
    (count,) = struct.unpack(">H", recv_exactly(sock, 2))
    if count > 64:
        raise ValueError(f"ap_count={count} implausible")
    aps = []
    for _ in range(count):
        rec = recv_exactly(sock, 8)
        bssid = ":".join(f"{b:02x}" for b in rec[:6])
        (rssi,) = struct.unpack(">b", rec[6:7])
        ssid_len = rec[7]
        ssid = recv_exactly(sock, ssid_len).decode("utf-8", "replace") if ssid_len else ""
        aps.append({"bssid": bssid, "rssi": rssi, "ssid": ssid})
    return aps


def save_wav(path, pcm):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)


def send_rich_reply(conn, text, pcm=b""):
    """magic, 0x03, text_len, text, sample_rate, audio_len, audio."""
    blob = text.encode("utf-8")[:500]
    if not pcm:
        pcm = b"\x00\x00" * int(SAMPLE_RATE * 0.2)
    conn.sendall(
        MAGIC
        + struct.pack(">B", MSG_RICH_REPLY)
        + struct.pack(">I", len(blob)) + blob
        + struct.pack(">I", SAMPLE_RATE)
        + struct.pack(">I", len(pcm)) + pcm
    )


def banner(headline, detail=""):
    bar = "=" * 64
    print(f"\n{bar}\n  {headline}")
    if detail:
        print(f"  {detail}")
    print(f"{bar}\n")


# ----------------------------------------------------------------------
# One touch cycle
# ----------------------------------------------------------------------

def handle_cycle(conn, cycle):
    if recv_exactly(conn, 4) != MAGIC:
        print("  ! bad magic — not an ECHO client")
        return
    (msg_type,) = struct.unpack(">B", recv_exactly(conn, 1))
    if msg_type not in (MSG_SENSOR_V1, MSG_SENSOR_V2):
        print(f"  ! unexpected msg_type 0x{msg_type:02x}")
        conn.sendall(MAGIC + struct.pack(">B", MSG_ERROR))
        return

    image = recv_exactly(conn, read_len(conn, "image_len"))
    audio = recv_exactly(conn, read_len(conn, "audio_len"))
    aps = read_fingerprint(conn) if msg_type == MSG_SENSOR_V2 else []

    secs = len(audio) / (SAMPLE_RATE * 2)
    print(f"  image {len(image):,}B   audio {len(audio):,}B ({secs:.1f}s)   {len(aps)} AP(s)")

    os.makedirs(CAPTURE_DIR, exist_ok=True)
    jpg = os.path.join(CAPTURE_DIR, f"cycle_{cycle:03d}.jpg")
    wav = os.path.join(CAPTURE_DIR, f"cycle_{cycle:03d}.wav")
    with open(jpg, "wb") as f:
        f.write(image)
    save_wav(wav, audio)

    bssids = [a["bssid"] for a in aps]
    place, score = match_place(bssids)
    if place:
        print(f"  place: {place} (fingerprint match {score:.0%})")
    elif bssids:
        print(f"  place: unknown (best match {score:.0%})")

    transcript = transcribe(wav)
    print(f'  heard: "{transcript}"' if transcript else "  heard: (nothing usable)")

    intent, arg = detect_intent(transcript)
    print(f"  intent: {intent}" + (f" -> {arg}" if arg else ""))

    # ---- name this place ----
    if intent == "place":
        if not bssids:
            text = compose(["Cannot save place", "No WiFi scan", "from the board"])
            kind, head = "error", "No WiFi scan to fingerprint"
        else:
            what = remember_place(arg, bssids)
            text = compose([f"Place {what}:", arg, f"{len(bssids)} routers seen"])
            kind, head = "enrolled", f"Place {what}: {arg}"
        banner(head)
        send_rich_reply(conn, text)
        set_state(kind=kind, headline=head, detail=f"{len(bssids)} access points fingerprinted",
                  transcript=transcript, image=jpg, cycle=cycle, oled=text)
        return

    # ---- enrol a person ----
    if intent == "name":
        ok, info = enroll_face(arg, jpg)
        if ok:
            with MEM_LOCK:
                rec = MEMORY["people"].setdefault(arg, {})
                rec.setdefault("first_met", now_iso())
                rec.setdefault("first_met_place", place or "somewhere")
                rec["encounters"] = rec.get("encounters", 0) + 1
                rec.setdefault("key_points", [])
                save_memory()
            text = compose([f"Hello {arg}", "Saved your face", f"Met at {place}" if place else "Place unknown"])
            banner(f"Nice to meet you, {arg}", info)
            set_state(kind="enrolled", headline=f"Nice to meet you, {arg}",
                      detail=f"Enrolled — {info}" + (f", at {place}" if place else ""),
                      transcript=transcript, image=jpg, cycle=cycle, oled=text)
        else:
            text = compose([f"Heard: {arg}", "But no face seen", "Look at camera"])
            banner(f"Heard the name {arg}, but could not enrol", info)
            set_state(kind="error", headline=f"Couldn't enrol {arg}", detail=info,
                      transcript=transcript, image=jpg, cycle=cycle, oled=text)
        send_rich_reply(conn, text)
        return

    # ---- identify (explicit question, or just a normal capture) ----
    who, info = identify_face(jpg)

    if who:
        points = [] if intent == "who" else summarise(transcript)
        with MEM_LOCK:
            rec = MEMORY["people"].setdefault(who, {})
            rec.setdefault("first_met", now_iso())
            rec.setdefault("first_met_place", place or "somewhere")
            rec["encounters"] = rec.get("encounters", 0) + 1
            rec["last_seen"] = now_iso()
            if points:
                kp = rec.setdefault("key_points", [])
                for p in points:
                    if p not in kp:
                        kp.append(p)
                del kp[:-8]        # keep the 8 most recent
            save_memory()

        text = person_card(who, place)
        banner(f"He is {who}", f"distance {info:.2f}" if isinstance(info, float) else str(info))
        if points:
            print("  key points: " + " | ".join(points))
        send_rich_reply(conn, text)
        set_state(kind="known", headline=f"He is {who}",
                  detail=(f"Recognised at {place}. " if place else "") +
                         (f"Noted: {', '.join(points)}" if points else "No new key points"),
                  transcript=transcript, image=jpg, cycle=cycle, oled=text)
        return

    known = enrolled_names()
    text = compose([
        "Unknown person",
        f"At {place}" if place else None,
        "Ask them to say:",
        "my name is ___",
    ])
    banner("I don't know this person yet",
           f"{info} — " + (f"known: {', '.join(known)}" if known else "nobody enrolled yet"))
    send_rich_reply(conn, text)
    set_state(kind="unknown", headline="I don't know this person yet",
              detail=f"{info}. " + (f"Known: {', '.join(known)}" if known else "Nobody enrolled yet"),
              transcript=transcript, image=jpg, cycle=cycle, oled=text)


# ----------------------------------------------------------------------
# Live page
# ----------------------------------------------------------------------

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>ECHO</title>
<meta http-equiv="refresh" content="2">
<style>
  :root {{ color-scheme: dark; }}
  body {{ margin:0; min-height:100vh; display:flex; align-items:center;
         justify-content:center; background:#0b0d10; color:#e8eaed;
         font-family:system-ui,-apple-system,Segoe UI,sans-serif; }}
  .wrap {{ display:flex; gap:5vw; align-items:center; flex-wrap:wrap;
          justify-content:center; padding:5vh 4vw; max-width:1200px; }}
  .main {{ text-align:center; max-width:620px; }}
  .headline {{ font-size:clamp(2rem,6vw,4.4rem); font-weight:650;
               line-height:1.1; margin:0 0 .35em; color:{color}; }}
  .detail {{ font-size:clamp(.95rem,1.8vw,1.3rem); opacity:.75; margin:0 0 1.2em; }}
  .said {{ font-size:clamp(.9rem,1.5vw,1.1rem); opacity:.55;
           font-style:italic; margin:0 0 1.4em; }}
  img {{ max-width:min(420px,70vw); border-radius:14px; border:1px solid #23262b; }}
  .screen {{ background:#04121a; border:2px solid #1d2a33; border-radius:10px;
             padding:16px 18px; font-family:ui-monospace,Consolas,monospace;
             font-size:1.05rem; line-height:1.45; color:#7ef7d0;
             white-space:pre; text-align:left; box-shadow:0 0 40px #0a3a4a55; }}
  .cap {{ font-size:.75rem; letter-spacing:.08em; text-transform:uppercase;
          opacity:.4; margin:0 0 .6em; }}
  .foot {{ margin-top:1.4em; font-size:.8rem; opacity:.4;
           font-variant-numeric:tabular-nums; }}
</style></head>
<body><div class="wrap">
  <div class="main">
    <p class="headline">{headline}</p>
    <p class="detail">{detail}</p>
    {said}
    {img}
    <p class="foot">cycle {cycle} &middot; {when}</p>
  </div>
  <div>
    <p class="cap">what the glasses show</p>
    <div class="screen">{oled}</div>
  </div>
</div></body></html>"""

COLORS = {"idle": "#8b95a1", "enrolled": "#6ee7a8", "known": "#7cc4ff",
          "unknown": "#ffcc66", "error": "#ff8a8a"}


class PageHandler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        st = get_state()
        if self.path.startswith("/photo"):
            p = st.get("image")
            if p and os.path.exists(p):
                data = open(p, "rb").read()
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            self.send_error(404)
            return

        said = (f'<p class="said">&ldquo;{html.escape(st["transcript"])}&rdquo;</p>'
                if st.get("transcript") else "")
        img = (f'<img src="/photo?c={st["cycle"]}" alt="last capture">'
               if st.get("image") else "")
        oled = html.escape(st.get("oled") or "").ljust(1) or "(nothing yet)"

        body = PAGE.format(
            color=COLORS.get(st.get("kind", "idle"), "#e8eaed"),
            headline=html.escape(st["headline"]), detail=html.escape(st["detail"]),
            said=said, img=img, oled=oled,
            cycle=st["cycle"], when=st["when"] or "—",
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ----------------------------------------------------------------------

def main():
    os.makedirs(FACES_DIR, exist_ok=True)
    load_memory()

    page_server = HTTPServer(("0.0.0.0", WEB_PORT), PageHandler)
    threading.Thread(target=page_server.serve_forever, daemon=True).start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(4)

    people = enrolled_names()
    places = [p["name"] for p in MEMORY["places"]]
    print(f"ECHO brain listening on {HOST}:{PORT}")
    print(f"Display page:  http://localhost:{WEB_PORT}")
    print(f"People:        {', '.join(people) if people else 'nobody yet'}")
    print(f"Places:        {', '.join(places) if places else 'none yet'}")
    print()
    print("Say while touching:")
    print("   'hello my name is Satyabrat'   -> remembers the face")
    print("   'we are at the college lab'    -> names this place")
    print("   'who is this guy'              -> shows who + where + what you discussed")
    print()

    cycle = 0
    while True:
        conn, addr = srv.accept()
        cycle += 1
        print(f"[cycle {cycle}] from {addr[0]}")
        try:
            handle_cycle(conn, cycle)
        except (ConnectionError, ValueError, struct.error) as e:
            print(f"  ! cycle failed: {e}")
        except Exception as e:
            print(f"  ! unexpected error: {e}")
        finally:
            conn.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nstopped")
