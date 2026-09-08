# ECHO sensor-node protocol (v2, touch-trigger build)

Status: freshly drafted for this firmware. No prior `PROTOCOL.md` was found
in this workspace to update in place (see BRINGUP_NOTES.md for why), so
this is a new document describing what the firmware in this repo actually
implements, not a diff against an earlier version. If you have the original
`PROTOCOL.md` from an earlier phase, send it over and this should be
reconciled against it rather than treated as the source of truth.

## Transport

Plain TCP, one connection per touch-trigger cycle. The device connects to
`SERVER_HOST:SERVER_PORT` (configured in `main/config.h`), sends one sensor
packet, reads one reply packet, then closes the connection. There is no
persistent/streaming connection in this build — each touch press is a
fresh, independent request-response cycle. This is a deliberate
simplification for the deadline: it costs a bit of latency (TCP handshake
per cycle) in exchange for much simpler error recovery (a dropped
connection just means "this cycle failed," not "the stream is in an
unknown state").

No TLS. This is a prototype on a private WiFi network; do not deploy this
as-is anywhere the traffic isn't trusted.

## Framing

All multi-byte integers are big-endian (network byte order). All frames
start with a 4-byte magic string `"ECH1"` so a receiver can immediately
tell it's talking to this protocol and not, say, an HTTP error page from a
misconfigured proxy.

### Device to server: sensor packet

```
offset  size  field
0       4     magic = "ECH1"
4       1     msg_type = 0x01 (SENSOR_PACKET)
5       4     image_len (uint32, bytes of JPEG that follow)
9       N     image bytes (JPEG, from the OV3660)
9+N     4     audio_len (uint32, bytes of PCM that follow)
13+N    M     audio bytes (PCM16, mono, little-endian samples, 16000 Hz
              as configured by AUDIO_SAMPLE_RATE_HZ in config.h)
```

### Server to device: audio reply

```
offset  size  field
0       4     magic = "ECH1"
4       1     msg_type = 0x02 (AUDIO_REPLY) or 0xFF (ERROR)
5       4     sample_rate (uint32, Hz) — only present/meaningful if
              msg_type == AUDIO_REPLY
9       4     audio_len (uint32, bytes of PCM that follow) — only
              present/meaningful if msg_type == AUDIO_REPLY
13      M     audio bytes (PCM16, mono, little-endian)
```

If `msg_type == 0xFF` (ERROR), the device firmware currently just logs it
and aborts the cycle back to idle — there's no structured error payload
yet. If the laptop side needs to communicate *why* a cycle failed (bad
audio, ASR failure, no camera frame, etc.), that would be a reasonable
next addition to the ERROR frame, but it's out of scope for this pass.

## Sanity limits

The device firmware refuses to allocate more than 2 MiB for an incoming
reply frame (`MAX_FRAME_BYTES` in `network_client.c`), so a corrupted or
malicious length prefix can't make the device try to allocate an
unreasonable amount of memory and wedge. The same kind of check should
exist on the laptop-side `NetworkSensorSource` receiver for the incoming
sensor packet's `image_len`/`audio_len` — that's called out again in
`BRINGUP_NOTES.md` as something to add when the receiver is implemented
against the real repo.

## What changed from the trigger mechanism in earlier phases

There is no wake-word event in this protocol. The only thing that starts a
cycle is a physical touch on GPIO13. If an earlier protocol version assumed
a voice-activated trigger, that assumption no longer applies to this build
— see `echo-esp32-firmware-prompt-v2.md` for why that scope was cut.

## v2 additions (display + place fingerprint)

The v1 frames above still work — the device only ever *sends* v2 now, but
the laptop side accepts both, and a v1 `AUDIO_REPLY` is still understood by
the firmware. Two new message types were added:

### Device to server: sensor packet v2 — `msg_type = 0x04`

Identical to `0x01`, with the nearby-access-point fingerprint appended:

```
...as 0x01 (magic, type, image_len, image, audio_len, audio), then:
        2     ap_count (uint16)
   per AP:
        6     bssid
        1     rssi (int8)
        1     ssid_len
        N     ssid bytes (UTF-8, not NUL-terminated)
```

The AP list is the device's answer to "where am I". The ESP32 has no GPS,
and GPS would not work indoors anyway, so the set of BSSIDs visible from a
spot is used as a fingerprint of that spot. The device sends at most 8 APs,
strongest first, refreshed by a scan immediately before each upload.

Matching is done server-side by Jaccard overlap against previously named
places, with a deliberately forgiving threshold — routers come and go
between scans, and naming the wrong room is less bad than saying nothing.

### Server to device: rich reply — `msg_type = 0x03`

```
offset  size  field
0       4     magic = "ECH1"
4       1     msg_type = 0x03 (RICH_REPLY)
5       4     text_len (uint32)
9       T     text bytes (UTF-8), for the OLED
9+T     4     sample_rate (uint32, Hz)
13+T    4     audio_len (uint32)
17+T    M     audio bytes (PCM16, mono, little-endian)
```

`text` is laid out for a 128x64 SSD1306: 21 columns by 8 rows. Newlines are
honoured, longer lines are word-wrapped on the device, and anything past 8
rows is dropped rather than scrolled — nobody can read a scrolling 0.96"
display. The device refuses a `text_len` of 512 or more.

`audio_len` may be 0, which means "nothing to play, just show the text".

## Hardware note: the OLED shares the camera's I2C bus

The display sits on GPIO26/27, the same pins the camera's SCCB uses, and
reuses the I2C driver the camera component installs on that port. The
camera answers at **0x3C** and stock SSD1306 modules also ship at 0x3C, so
the display must be jumpered to **0x3D**. `oled_init()` probes both and logs
which it found — check the boot log before mounting anything permanently.
