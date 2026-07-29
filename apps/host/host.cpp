// ECHO OS apps — the laptop host ("echo-apps").
//
// The standalone entrypoint that runs the whole apps layer on a dev machine
// (deliverable #3). On the glasses this host is launched by the core once the
// safety loop is up; here it stands in so the full voice flow is exercisable by
// typing commands, with the laptop's keyboard/mic substituting for the glasses'
// ASR and a simulated HUD band substituting for the overlay. Every external
// service defaults to --mock.
//
// It wires the layer together exactly as the device would:
//   * a PermissionModel granting each first-party app its declared capabilities,
//   * the HUD compositor (the only visual surface),
//   * a voice bridge over the core's voice-ui (the only audio surface),
//   * a Router that maps an utterance to the owning app, and
//   * a Supervisor that runs each app as an isolated OS process (default).
//
// Modes:
//   --supervised (default)  apps run as separate processes; commands go over IPC.
//   --in-process            apps run in the host; commands are direct calls.
//   --window                draw the simulated HUD band instead of logging frames.
//   --real                  ask apps for their real backend (unconfigured today).
#include "registry.hpp"

#include "echo/apps/framework/router.hpp"
#include "echo/apps/framework/supervisor.hpp"
#include "echo/apps/framework/voice_bridge.hpp"
#include "echo/apps/framework/nlu.hpp"
#include "echo/apps/hud/hud.hpp"
#include "echo/voice/voice_ui.hpp"
#include "echo/log.hpp"

#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool has(int argc, char** argv, const char* f) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], f) == 0) return true;
    return false;
}

// Directory portion of argv[0], so we can find the sibling app executables.
std::string exe_dir(const char* argv0) {
    std::string p = argv0 ? argv0 : "";
    auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return p.substr(0, slash);
}

std::string app_exe(const std::string& dir, const std::string& id) {
    std::string path = dir + "/echo-app-" + id;
#if defined(_WIN32)
    path += ".exe";
#endif
    return path;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace echo;
    using namespace echo::apps;

    set_log_level(LogLevel::Info);

    const bool mock       = !has(argc, argv, "--real");
    const bool in_process = has(argc, argv, "--in-process");
    const bool window     = has(argc, argv, "--window");

    log_info("host", mock ? "starting apps layer (mock services)"
                          : "starting apps layer (REAL services requested)");

    // --- Construct apps in-process to read their metadata (routing + policy) --
    // Even in supervised mode we build the objects once here to learn each app's
    // intents and required capabilities without duplicating them; the objects are
    // cheap and are also what the in-process router dispatches to.
    std::vector<std::unique_ptr<IApp>> apps;
    for (auto& e : registry::all()) {
        auto app = e.make(mock);
        if (app->initialize() != Status::Ok && mock) {
            log_warn("host", std::string("app '") + e.id + "' failed to initialize");
        }
        apps.push_back(std::move(app));
    }

    // --- Permission policy: grant each first-party app what it declared --------
    PermissionModel permissions;
    for (auto& app : apps)
        permissions.grant(app->metadata().id, app->metadata().required_caps);

    // --- Output surfaces (the only two an app can reach) ----------------------
    auto hud   = hud::make_hud_compositor(window);
    hud->initialize();
    auto voice = voice::make_voice_ui();
    voice->initialize();
    auto bridge = make_voice_bridge(voice.get());

    // --- Router: owns intent matching, permission gate, output fan-out --------
    Router router(hud.get(), bridge.get(), &permissions);
    for (auto& app : apps) router.register_app(app.get());

    // --- Supervisor: run each app as an isolated OS process (default) ---------
    Supervisor supervisor;
    if (!in_process) {
        const std::string dir = exe_dir(argv[0]);
        for (auto& app : apps) {
            AppSpec spec;
            spec.id         = app->metadata().id;
            spec.executable = app_exe(dir, spec.id);
            if (mock) spec.args = {"--mock"};
            supervisor.add(spec);
        }
        int up = supervisor.start_all();
        log_info("host", std::to_string(up) + " app process(es) supervised");
    }

    // --- Voice loop: read utterances from stdin (the mic substitute) ----------
    std::cout << "\nECHO OS apps — say something ("
              << (in_process ? "in-process" : "supervised")
              << " mode). Try:\n"
                 "  play some jazz | read my unread email | call Sam | search for pharmacies\n"
                 "  take a photo | show my photos | watch the news | open wikipedia\n"
                 "Type 'quit' to exit.\n\n> ";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit" || line == "exit") break;
        if (!line.empty()) {
            VoiceCommand cmd = nlu::parse(line, router.vocabulary());
            if (cmd.intent.empty()) {
                std::cout << "  (no app understood that)\n";
            } else if (in_process) {
                RouteResult r = router.route(cmd);
                if (!r.handled) std::cout << "  (no app claimed that)\n";
            } else {
                // Supervised: dispatch to the owning app's process over IPC, then
                // fan the response out to the same two surfaces the router uses.
                IApp* owner = router.find(cmd.intent);
                if (!owner) {
                    std::cout << "  (no app claimed that)\n";
                } else {
                    const std::string id = owner->metadata().id;
                    auto resp = supervisor.send_command(id, cmd);
                    if (!resp) {
                        std::cout << "  (app '" << id << "' did not respond)\n";
                    } else {
                        if (!resp->hud.empty()) hud->present(id, resp->hud);
                        if (!resp->speech.empty()) bridge->speak(id, resp->speech, resp->tone);
                    }
                }
                supervisor.reap();  // keep the process pool healthy
            }
        }
        std::cout << "> ";
    }

    // --- Orderly shutdown ------------------------------------------------------
    supervisor.shutdown();
    for (auto& app : apps) app->shutdown();
    voice->shutdown();
    hud->shutdown();
    log_info("host", "apps layer stopped");
    return 0;
}
