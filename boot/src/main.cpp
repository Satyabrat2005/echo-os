// ECHO OS — reference entrypoint.
//
// On the glasses this is PID 1's payload, launched by the init sequence on top of
// the minimal embedded Linux base. Here it doubles as a smoke test: it boots the
// runtime, runs one tick of the core loop through the stub pipeline, and shuts
// down cleanly — proving the module wiring compiles and links end to end.
#include "echo/boot/runtime.hpp"
#include "echo/log.hpp"

int main() {
    echo::set_log_level(echo::LogLevel::Debug);
    echo::log_info("main", "ECHO OS starting");

    echo::boot::Runtime runtime;

    if (runtime.boot() != echo::Status::Ok) {
        echo::log_error("main", "boot failed");
        return 1;
    }

    runtime.run();
    runtime.shutdown();

    echo::log_info("main", "ECHO OS stopped");
    return 0;
}
