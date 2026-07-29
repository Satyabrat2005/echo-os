// ECHO OS — Phase 3 laptop demo ("echo-demo").
//
// The first binary where the whole loop actually runs end to end on a laptop:
//   webcam + mic in  ->  wake word ("Hey ECHO")  ->  speech-to-text  ->
//   {face recognition | app routing | LLM reasoning, behind the safe-mode gate}
//   ->  spoken response (Piper)  +  HUD subtitle (SDL overlay).
//
// It reuses the real modules exactly as the glasses would wire them, with the
// laptop's webcam/mic/speakers standing in for the glasses' sensors. Each engine
// is real when compiled with its ECHO_WITH_* flag and a stub otherwise, so this
// binary always builds and runs — it just does less without the models.
//
// Isolation is preserved: the apps layer is reached only through the Router, the
// voice bridge, and the HUD compositor (never core internals), and the HUD runs
// on its own thread so it can never block or crash the core loop.
//
// Modes:
//   (default)      real capture: webcam + mic + wake word + ASR
//   --text         type utterances instead of speaking (no mic/wake needed)
//   --no-camera    skip the webcam (mic-only)
//   --headless-hud log HUD frames instead of opening the overlay window
#include "echo/perception/perception_engine.hpp"
#include "echo/cognitive/cognitive_core.hpp"
#include "echo/voice/voice_ui.hpp"
#include "echo/sensor/sensor_source.hpp"
#include "echo/config.hpp"
#include "echo/log.hpp"
#include "echo/types.hpp"

#include "echo/apps/hud/hud.hpp"
#include "echo/apps/framework/router.hpp"
#include "echo/apps/framework/voice_bridge.hpp"
#include "echo/apps/framework/nlu.hpp"
#include "echo/apps/framework/permission.hpp"
#include "registry.hpp"

#include "latency.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

bool has_flag(int argc, char** argv, const char* f) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], f) == 0) return true;
    return false;
}

bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// A "who is this / do you recognize them" style question about the camera view.
bool is_face_query(const std::string& t) {
    return (contains(t, "who") || contains(t, "recogn") || contains(t, "know this") ||
            contains(t, "know them")) &&
           (contains(t, "this") || contains(t, "that") || contains(t, "here") ||
            contains(t, "them") || contains(t, "person") || contains(t, "face") ||
            contains(t, "looking"));
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace echo;
    using namespace echo::apps;
    namespace hud = echo::apps::hud;

    (void)std::signal(SIGINT, on_signal);  // prior handler intentionally discarded
    set_log_level(LogLevel::Info);

    const bool text_mode   = has_flag(argc, argv, "--text");
    const bool no_camera   = has_flag(argc, argv, "--no-camera");
    const bool headless_hud = has_flag(argc, argv, "--headless-hud");

    log_info("demo", "ECHO OS Phase 3 demo starting (fully local)");

    // --- Core pipeline --------------------------------------------------------
    auto perception = perception::make_perception_engine();
    auto cognitive  = cognitive::make_cognitive_core();   // default safe-mode config
    auto voice      = voice::make_voice_ui();
    perception->initialize();
    cognitive->initialize();
    voice->initialize();

    // --- Visual surface: the HUD overlay window -------------------------------
    auto hud_c = hud::make_hud_compositor(headless_hud ? hud::HudMode::Headless
                                                       : hud::HudMode::Window);
    hud_c->initialize();
    auto idle_frame = [&] {
        hud_c->present("echo", hud::HudFrame{}
                                   .with_subtitle("ECHO ready — say \"Hey ECHO\"", 60000)
                                   .with_status(hud::StatusKind::Idle));
    };
    idle_frame();

    // --- Apps layer (mock services) reached only via the Router ---------------
    std::vector<std::unique_ptr<IApp>> apps;
    for (auto& e : registry::all()) {
        auto app = e.make(/*mock=*/true);
        app->initialize();
        apps.push_back(std::move(app));
    }
    PermissionModel permissions;
    for (auto& a : apps) permissions.grant(a->metadata().id, a->metadata().required_caps);
    auto bridge = make_voice_bridge(voice.get());
    Router router(hud_c.get(), bridge.get(), &permissions);
    for (auto& a : apps) router.register_app(a.get());

    LatencyLog latency("latency_log.csv");

    // Latest recognized faces from the camera stream (updated continuously).
    std::vector<perception::FaceObservation> last_faces;

    // --- One conversational turn ----------------------------------------------
    // `obs` carries the endpointed transcript; `perception_ms` is how long the
    // perception+ASR pass that produced it took (0 in --text mode).
    auto handle_turn = [&](const perception::Perception& obs, double perception_ms) {
        if (!obs.speech) return;
        const std::string transcript = obs.speech->text;
        const std::string t = lower(transcript);
        log_info("demo", std::string("heard: \"") + transcript + "\"");
        hud_c->present("echo", hud::HudFrame{}.with_subtitle(transcript, 4000)
                                   .with_status(hud::StatusKind::Working));

        const auto turn_start = now();
        double cognitive_ms = 0, voice_ms = 0;

        // 1) Face recognition query about the current camera view.
        if (is_face_query(t)) {
            hud::HudFrame f;
            std::string say;
            SpeechTone tone = SpeechTone::Neutral;
            auto named = std::find_if(last_faces.begin(), last_faces.end(),
                                      [](const auto& fo) { return !fo.identity.empty(); });
            if (named != last_faces.end()) {
                say = "That's " + named->identity + ".";
                f.with_subtitle(named->identity, 5000).with_icon(hud::Glyph::Check)
                 .with_status(hud::StatusKind::Success);
                tone = SpeechTone::Reassuring;
            } else if (!last_faces.empty()) {
                say = "I see someone, but I don't recognize them yet.";
                f.with_icon(hud::Glyph::Warning).with_status(hud::StatusKind::Working);
                tone = SpeechTone::Reassuring;
            } else {
                say = "I don't see anyone right now.";
                f.with_status(hud::StatusKind::Idle);
            }
            auto v0 = now();
            voice->speak(voice::Utterance{say, tone == SpeechTone::Reassuring
                                                    ? voice::Tone::Reassuring
                                                    : voice::Tone::Neutral});
            voice_ms = ms_since(v0);
            hud_c->present("echo", f);
            latency.record("face", perception_ms, cognitive_ms, voice_ms, ms_since(turn_start));
            idle_frame();
            return;
        }

        // 2) A device-app command (deterministic keyword routing → owning app).
        VoiceCommand cmd = nlu::parse(transcript, router.vocabulary());
        if (!cmd.intent.empty()) {
            RouteResult r = router.route(cmd);  // app speaks + draws via bridge/hud
            if (r.handled) {
                // The app's speak/draw happen inside route(); voice time isn't
                // split out here, so it stays folded into total_ms.
                latency.record(cmd.intent, perception_ms, cognitive_ms, voice_ms, ms_since(turn_start));
                idle_frame();
                return;
            }
        }

        // 3) General reasoning — the LLM, strictly behind the safe-mode gate.
        auto c0 = now();
        auto resp = cognitive->respond(obs);
        cognitive_ms = ms_since(c0);
        if (resp) {
            const cognitive::Response& R = resp.value();

            // The LLM may itself have routed the request to an app.
            if (!R.intent.empty() && router.find(R.intent)) {
                VoiceCommand c2;
                c2.intent = R.intent;
                c2.text   = transcript;
                c2.slots["query"] = transcript;
                router.route(c2);
            } else {
                auto v0 = now();
                voice->speak(voice::to_utterance(R));
                voice_ms = ms_since(v0);
                hud::StatusKind st = (R.kind == cognitive::ResponseKind::SafeMode)
                                         ? hud::StatusKind::Working
                                         : hud::StatusKind::Success;
                hud_c->present("echo", hud::HudFrame{}.with_subtitle(R.text, 5000).with_status(st));
                if (R.flag_caregiver)
                    log_warn("demo", "safe mode engaged — caregiver would be flagged");
            }
        }
        latency.record("reason", perception_ms, cognitive_ms, voice_ms, ms_since(turn_start));
        idle_frame();
    };

    // --- Text mode: type utterances (no mic/wake needed) ----------------------
    if (text_mode) {
        std::cout << "\nECHO demo (text mode). Type what you'd say after \"Hey ECHO\".\n"
                     "  e.g.  play some music   |   who is this   |   what should I do now\n"
                     "Type 'quit' to exit.\n\n> ";
        std::string line;
        while (!g_stop && std::getline(std::cin, line)) {
            if (line == "quit" || line == "exit") break;
            if (!line.empty()) {
                perception::Perception obs;
                // Typed text stands in for a confident transcript so the gate is
                // exercised realistically (safe mode still triggers if the LLM
                // can't answer — it never fabricates).
                obs.speech = perception::Transcript{line, Confidence{0.9f}, /*endpointed=*/true};
                handle_turn(obs, /*perception_ms=*/0.0);
            }
            std::cout << "> ";
        }
    } else {
        // --- Real capture: webcam + mic, each on its OWN SPSC lane ------------
        sensor::FrameQueue cam_q, mic_q;
        std::unique_ptr<sensor::ISensorSource> cam, mic;
        if (!no_camera) {
            cam = sensor::make_real_camera_source();
            if (cam) cam->start(cam_q);
        }
        mic = sensor::make_real_microphone_source();
        // Guard the deref: the cleanup path below already treats mic as possibly
        // null (`if (mic)`), and a real mic backend can legitimately fail to open
        // the device and return null. Fail like a missing mic, not with a crash.
        if (!mic || mic->start(mic_q) != Status::Ok)
            log_error("demo", "microphone unavailable — try --text mode");

        std::cout << "\nECHO demo listening. Say \"Hey ECHO\" then a request "
                     "(e.g. \"play some music\", \"who is this\"). Ctrl+C to stop.\n";

        while (!g_stop) {
            // Drain the camera lane: keep the latest recognized faces.
            while (auto cf = cam_q.pop()) {
                auto pr = perception->process(*cf);
                if (pr && !pr.value().faces.empty()) last_faces = pr.value().faces;
            }
            // Drain the mic lane: wake word, then endpointed transcript.
            while (auto mf = mic_q.pop()) {
                const auto p0 = now();
                auto pr = perception->process(*mf);
                const double pms = ms_since(p0);
                if (!pr) continue;
                const perception::Perception& P = pr.value();
                if (P.wake && P.wake->detected) {
                    voice->play_earcon("wake");
                    hud_c->present("echo", hud::HudFrame{}
                                               .with_subtitle("Listening…", 8000)
                                               .with_status(hud::StatusKind::Working));
                }
                if (P.speech && P.speech->endpointed && !P.speech->text.empty())
                    handle_turn(P, pms);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (cam) cam->stop();
        if (mic) mic->stop();
    }

    // --- Shutdown -------------------------------------------------------------
    latency.summarize();
    hud_c->shutdown();
    voice->shutdown();
    cognitive->shutdown();
    perception->shutdown();
    for (auto& a : apps) a->shutdown();
    log_info("demo", "ECHO OS demo stopped");
    return 0;
}
