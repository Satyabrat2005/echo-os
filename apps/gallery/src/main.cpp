// ECHO OS apps — gallery app process entrypoint.
#include "echo/apps/gallery/gallery_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::gallery::make_gallery_app(mock), argc, argv);
}
