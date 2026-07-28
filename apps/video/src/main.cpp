// ECHO OS apps — video app process entrypoint.
#include "echo/apps/video/video_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::video::make_video_app(mock), argc, argv);
}
