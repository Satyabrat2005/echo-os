# ECHO OS — Apps Layer

A **voice-first application layer** that runs on top of the ECHO OS core. It is a
separate layer, in its own processes, with hard isolation from the safety-critical
perception → cognitive → voice loop. Nothing here may block, slow, or crash that
loop (the core's latency budget and safe-mode gating are untouched by this layer).

This is **not a phone OS**. The glasses have no touchscreen and no window manager.
Every app is a *voice command → spoken / HUD response* loop, drawn on a minimal
overlay — never a GUI to navigate.

## Layout

```
apps/
  hud-compositor/   The ONLY visual surface. Three overlay primitives —
                    subtitle text, one icon, a status glyph — and nothing else.
  app-framework/    Two halves, split so the isolation boundary is in the build graph:
                      echo::app-sdk        what an APP links: the app contract,
                                           permissions, HUD primitives, IPC codec,
                                           intent parser, process-entry runner.
                                           Links NO core module.
                      echo::app-framework  what the HOST links: supervisor (process
                                           lifecycle), router (intent → app → output),
                                           voice bridge (the one seam to core voice-ui),
                                           cross-platform child-process wrapper.
  media/ browser/ search/ video/          The 8 apps. Each is a library (testable
  mail/ telephony/ camera/ gallery/       mock logic) + a thin executable that runs
                                          as its own supervised process.
  host/             echo-apps — the standalone laptop host that wires it all up.
  tests/            Dependency-free smoke tests (command routing, shared HUD,
                    permission gate, real cross-process IPC + crash containment).
```

## The five constraints, and where they live

1. **Voice-first, no touchscreen.** The only thing an app can draw is a
   `hud::HudFrame`, composed from `SubtitleText` + `Icon` + `StatusGlyph`. There
   is no framebuffer, canvas, or window API — a list to scroll or a button to tap
   is simply not expressible. See [`hud-compositor/`](hud-compositor).
2. **Hard isolation from the core.** Apps are separate OS processes managed by the
   `Supervisor`; a crashed or hung app is a local event (detected, restarted with
   bounded backoff, or parked) that never reaches the core loop. Apps reach the
   core's audio only through `IVoiceBridge`, which calls `voice-ui`'s *public*
   interface. `echo::app-sdk` links no core module — enforced by CMake, not just
   by comment.
3. **Telephony rides the paired phone.** `telephony/` models "phone" as a
   Bluetooth bridge to the wearer's smartphone (like AirPods / a smartwatch), not
   a SIM in the glasses. Mock mode simulates the call state machine.
4. **External services start mocked.** Spotify, Gmail, YouTube, Google Search each
   need OAuth credentials that don't exist yet. Every app defaults to a `--mock`
   backend returning realistic fake data behind the same interface the real
   backend will implement — wiring credentials later is zero interface change.
5. **Calm, private by default.** A `PermissionModel` gates each app's access to
   camera / mic / contacts / network / location / storage; the router refuses a
   command from an app that lacks a required capability. No raw personal data
   leaves the device except through the explicit, user-initiated service calls an
   app makes.

## How a command flows

```
utterance ─► nlu::parse ─► Router.route(intent)
                              │  find owning app (by declared intent)
                              │  permission gate (PermissionModel)
                              ▼
         in-process:  IApp::on_command            supervised:  IPC line over the
         (host --in-process)                       child's stdin/stdout pipe
                              │                                   │
                              ▼                                   ▼
                         AppResponse  ──►  HUD compositor (present frame)
                                      └─►  voice bridge  (speak line via core voice-ui)
```

The routing logic is identical for both paths; supervised mode swaps the direct
call for a message over the process boundary.

## Running on a laptop

Built by default with the top-level build (`-DECHO_BUILD_APPS=OFF` to skip), or
standalone with `cmake -S apps -B build-apps`. Executables land in
`<build>/apps-bin/`.

```bash
./apps-bin/echo-apps                 # supervised: 8 isolated app processes
./apps-bin/echo-apps --in-process    # apps in the host process (direct calls)
./apps-bin/echo-apps --window        # draw the simulated HUD band to the terminal

# Poke a single app process directly:
./apps-bin/echo-app-mail --once "any unread messages"
```

Every app also runs standalone in three modes via the shared runner: `--serve`
(stdio IPC, the supervised default), `--once "<phrase>"` (one command, printed),
and `--selftest` (init + one canned command, for spawn checks). All default to
`--mock`; `--real` selects the (not-yet-configured) real backend.

## Status

First-pass scaffold: real interfaces and mock implementations, each app running
as a genuine supervised process, with the full voice flow testable today. The
`TODO(<app>)` markers show exactly where real credentials/backends slot in behind
the unchanged interfaces.
