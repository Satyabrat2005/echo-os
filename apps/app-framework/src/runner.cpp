#include "echo/apps/framework/runner.hpp"
#include "echo/apps/framework/ipc.hpp"
#include "echo/apps/framework/nlu.hpp"
#include "echo/log.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace echo::apps {

bool has_flag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}

std::string flag_value(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc) return argv[i + 1];
    return {};
}

namespace {

// Print an AppResponse the way a person would want to read it at a shell.
void print_human(const IApp& app, const AppResponse& r) {
    std::printf("app     : %s (%s)\n", app.metadata().name.c_str(), app.metadata().id.c_str());
    std::printf("status  : %s\n", to_string(r.status));
    if (!r.speech.empty()) std::printf("say     : %s\n", r.speech.c_str());
    if (r.hud.has_subtitle) std::printf("hud.text: %s\n", r.hud.subtitle.text.c_str());
    if (r.hud.has_icon)     std::printf("hud.icon: %s\n", hud::to_string(r.hud.icon.glyph));
    if (r.hud.has_status)   std::printf("hud.stat: %s\n", hud::to_string(r.hud.status.kind));
}

// The --serve loop: one Cmd line in, one Rsp line out, until stdin closes. This
// is the process-side of the supervisor's send_command path.
int serve(IApp& app) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto msg = ipc::decode(line);
        if (!msg || msg->kind != ipc::MessageKind::Cmd) continue;
        AppResponse r = app.on_command(ipc::to_command(*msg));
        std::cout << ipc::encode(ipc::to_message(r)) << "\n";
        std::cout.flush();
    }
    return 0;
}

}  // namespace

int run_app(std::unique_ptr<IApp> app, int argc, char** argv) {
    if (!app) return 2;

    if (app->initialize() != Status::Ok) {
        log_error(app->metadata().id.c_str(), "app failed to initialize");
        return 1;
    }

    int rc = 0;

    if (has_flag(argc, argv, "--once")) {
        // Handle a single typed utterance and print the result.
        const std::string phrase = flag_value(argc, argv, "--once");
        VoiceCommand cmd = nlu::parse(phrase, app->metadata().intents);
        print_human(*app, app->on_command(cmd));
    } else if (has_flag(argc, argv, "--selftest")) {
        // Prove a spawn works: run the app's first declared intent.
        VoiceCommand cmd;
        if (!app->metadata().intents.empty()) cmd.intent = app->metadata().intents.front();
        cmd.text = cmd.intent;
        AppResponse r = app->on_command(cmd);
        rc = (r.status == Status::Ok) ? 0 : 1;
    } else {
        // Default: serve the supervisor over stdio.
        rc = serve(*app);
    }

    app->shutdown();
    return rc;
}

}  // namespace echo::apps
