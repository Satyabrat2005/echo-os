// ECHO OS apps — mail app process entrypoint.
#include "echo/apps/mail/mail_app.hpp"
#include "echo/apps/framework/runner.hpp"

int main(int argc, char** argv) {
    const bool mock = !echo::apps::has_flag(argc, argv, "--real");
    return echo::apps::run_app(echo::apps::mail::make_mail_app(mock), argc, argv);
}
