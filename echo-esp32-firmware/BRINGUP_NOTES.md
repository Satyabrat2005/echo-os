# Bring-up notes — ECHO sensor node, v2 firmware

Honest status as of writing this: **nothing in this firmware has been
flashed to real hardware yet.** This was written in a cloud sandbox with no
ESP-IDF toolchain and no physical board attached — it compiles nowhere
right now because it hasn't been built at all yet, not because it's been
tested and passed. Treat everything below as "written to match the spec
and reviewed by hand," not "verified by running it." That distinction
matters, and previous phases of this project have held that same bar, so
this one should too.

## What exists

- `main/app_main.c` and friends: a complete ESP-IDF project implementing
  the touch-trigger -> capture -> stream -> reply -> playback loop
  described in `echo-esp32-firmware-prompt-v2.md`.
- `PROTOCOL.md`: freshly drafted, not a diff of an existing file (see
  below for why).
- No `NetworkSensorSource` yet — that needs the real ECHO OS repo's
  `ISensorSource` interface, which isn't present in this workspace. See
  "Blocked" below.

## Assumptions made that need physical verification before trusting this build

These are called out inline in the code too (`config.h`, `audio_io.c`,
`touch_trigger.c`), repeated here so they're not missed:

1. **Amp wiring (BCLK/LRC/DIN) is guessed, not confirmed.** The v2 spec
   documents the mic's I2S pins (WS=14, SCK=15, SD=2) and the touch pin
   (13), but never states which GPIOs the MAX98357A's control pins are
   soldered to. This firmware assumes the amp shares GPIO14/15 as its
   BCK/WS lines with the mic (full-duplex I2S) and uses GPIO12 for the
   amp's DIN. **This is a guess based on which GPIOs are left free on a
   classic ESP32-CAM with the SD card unused — it is not a confirmed fact
   about your soldered board.** Before trusting audio playback at all,
   check the actual wiring against this assumption. If it's wrong, the fix
   is either rewiring or changing `AMP_DIN_GPIO` (and possibly splitting
   the shared I2S port into two) in `config.h`.

2. **Touch sensor active-high polarity is assumed, not confirmed.** Most
   cheap capacitive touch boards drive their output high on touch, but not
   all of them. `touch_trigger.c` has a one-line flag
   (`TOUCH_ACTIVE_LEVEL`) to flip this if bring-up shows the trigger firing
   backwards (i.e., "listening" LED behaves as if touched when it isn't).

3. **`AUDIO_CAPTURE_SECONDS = 5` is a starting guess, not a measured
   value.** The v2 spec explicitly asks for the real usable capture window
   to be tested and reported rather than assumed. It hasn't been tested
   here. Flash, touch the pad, and listen to (or otherwise inspect) an
   actual captured clip before trusting this number.

4. **Speaker output is completely unverified.** Per the v2 spec, no
   speaker/transducer was on hand at spec time. `audio_io_playback()` will
   push PCM out over I2S without error if the amp itself is present and
   responding, but "no error" only proves the write succeeded, not that
   intelligible audio comes out of a speaker. This remains an honest "wired
   and coded, not acoustically verified" gap until a speaker is attached
   and someone actually listens to it.

5. **GPIO12 is a strapping pin (MTDI).** It needs to read low at boot to
   select 3.3V flash voltage, which is normally fine, but attaching the
   amp's DIN line to it is new relative to the original spec (which never
   assigned it). Watch for boot/flash weirdness after wiring this up — if
   the board stops flashing or boots into a bad state with the amp
   connected, this pin is the first suspect, exactly as the v2 spec's
   honesty-requirements section anticipated for GPIO conflicts in general.

## Blocked

`NetworkSensorSource` (the laptop-side `ISensorSource` implementation) has
**not** been written. Building it correctly requires the actual ECHO OS
repository — specifically the real `ISensorSource` interface definition
and however Phase 3/14b wired the local SDL mic/webcam source into
`sensor-pipeline` — so the new implementation matches the existing
interface exactly and doesn't require upstream changes to `cognitive-core`
or `memory`, per the v2 spec's constraint. That repo was not present in
this workspace (no `.git`, no `sensor-pipeline`/`cognitive-core` source
tree found). Once the repo (or at minimum the `ISensorSource` interface
file) is available, this is the next piece to build, and it should be a
straightforward implementation: accept a TCP connection per `PROTOCOL.md`,
decode the sensor packet, feed it into the pipeline the same way the local
SDL source does, then encode whatever reply audio the pipeline produces
back into an `AUDIO_REPLY` frame.

## What's explicitly NOT in this build (scope cuts, stated plainly)

- No on-device voice wake-word. Touch is the only trigger.
- No OLED display.
- No SD card usage.
- No real on/off switch — power is physically plugging/unplugging the
  battery.
- WiFi credentials are hardcoded in `config.h` for this prototype only;
  this is explicitly not how a shipped product would handle provisioning.

## Suggested bring-up order, once you have a board in hand

1. Flash with WiFi credentials filled in, camera and audio disabled
   (comment out their init calls), just to confirm the board boots, the
   touch pin polls correctly, and the status LED behaves as expected. This
   isolates the GPIO12/strapping-pin risk from everything else.
2. Enable camera only, confirm a JPEG capture logs a believable byte count
   (a few tens of KB at SVGA quality 12 is the right ballpark; if you see
   0 bytes or a stack overflow, PSRAM likely isn't enabled — check
   `sdkconfig.defaults` took effect).
3. Enable audio I2S, capture a clip, and get the raw PCM off the device
   (e.g. temporarily log it over serial in small chunks, or add a debug
   endpoint) to confirm it's not silence or garbage before trusting the
   full pipeline.
4. Only then bring up WiFi streaming and the full touch-to-reply loop
   against a minimal test server (even a script that just echoes back a
   short beep) before pointing it at the real ECHO OS pipeline.
5. Physical/final mounting into the wearable housing should happen *after*
   step 4, not before — mounting hides the soldered joints and makes it
   much harder to re-check wiring if the touch, mic, or amp behave
   unexpectedly during bring-up. Get the electronics verified loose on the
   bench first, then mount.
