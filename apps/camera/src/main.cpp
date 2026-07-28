// ECHO OS apps — camera app process entrypoint.
#include "echo/apps/camera/camera_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::camera::make_camera_app(mock), argc, argv);
}
