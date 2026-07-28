// ECHO OS apps — the app lifecycle supervisor.
//
// The supervisor is app-framework's process manager. It starts each app as its
// own OS process, tracks liveness, restarts an app that crashes (with a bounded
// backoff so a crash-looping app can't hammer the system), and — critically —
// contains all of that so it never touches the safety-critical core (constraint
// #2). A hung or crashed app is a local event: the supervisor notices and reacts;
// the perception -> cognitive -> voice safety path keeps running regardless.
//
// It also carries the command path to a supervised app: send_command() writes a
// VoiceCommand to the child over its stdin pipe and reads back the AppResponse —
// the real cross-process version of what Router::route does in-process.
#pragma once

#include "echo/apps/framework/app.hpp"
#include "echo/apps/framework/process.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace echo::apps {

// How an app is launched and supervised.
struct AppSpec {
    std::string              id;             // must match the app's metadata id
    std::string              executable;     // path to the app binary
    std::vector<std::string> args;           // extra argv (mode flag added by the supervisor)
    bool                     auto_restart = true;
    int                      max_restarts = 3;  // within a window before giving up
};

// Runtime view of one supervised app.
struct AppStatus {
    std::string id;
    bool        running   = false;
    int         restarts  = 0;
    bool        gave_up   = false;   // exceeded max_restarts: parked, not respawned
    std::optional<int> last_exit_code;
};

class Supervisor {
public:
    Supervisor() = default;
    ~Supervisor();

    // Register an app to be managed. Does not launch it yet.
    void add(const AppSpec& spec);

    // Launch all registered apps (in --serve mode). Returns how many came up.
    int start_all();

    // Launch / stop / restart a single app by id.
    Status start(const std::string& id);
    void   stop(const std::string& id);
    Status restart(const std::string& id);

    // Poll children: reap exits, apply the restart policy. Call periodically from
    // the host's idle loop. Returns the number of apps restarted this pass.
    int reap();

    // Send a command to a supervised app and await its response over the pipe.
    // nullopt if the app isn't running or the exchange failed.
    std::optional<AppResponse> send_command(const std::string& id, const VoiceCommand& command);

    AppStatus              status(const std::string& id) const;
    std::vector<AppStatus> statuses() const;

    // Stop every app (orderly, then force). Safe to call more than once.
    void shutdown();

private:
    struct Managed {
        AppSpec                       spec;
        std::unique_ptr<ChildProcess> proc;   // null when not running
        int                           restarts = 0;
        bool                          gave_up  = false;
        std::optional<int>            last_exit_code;
    };

    Managed*       find(const std::string& id);
    const Managed* find(const std::string& id) const;

    std::vector<std::unique_ptr<Managed>> apps_;
};

}  // namespace echo::apps
