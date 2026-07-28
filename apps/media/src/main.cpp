// ECHO OS apps — media app process entrypoint.
//
// A thin shell: pick the backend from argv, construct the app, hand it to the
// shared runner. The runner owns the serve/once/selftest modes so every app's
// main() stays this small.
#include "echo/apps/media/media_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::media::make_media_app(mock), argc, argv);
}
