// ECHO OS apps — search app process entrypoint.
#include "echo/apps/search/search_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::search::make_search_app(mock), argc, argv);
}
