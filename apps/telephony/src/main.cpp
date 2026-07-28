// ECHO OS apps — telephony app process entrypoint.
#include "echo/apps/telephony/telephony_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::telephony::make_telephony_app(mock), argc, argv);
}
