// ECHO OS apps — browser app process entrypoint.
#include "echo/apps/browser/browser_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::browser::make_browser_app(mock), argc, argv);
}
