# Apps-layer smoke tests — dependency-free, wired into CTest.
add_executable(echo-apps-tests apps_smoke.cpp)

target_link_libraries(echo-apps-tests PRIVATE
    echo::app-framework
    echo::app-sdk
    echo::apps-hud
    echo::app-media
    echo::app-mail
    echo::app-telephony
    echo-project-options
)

# Tell the test where the built app executables live, so the process-isolation
# cases can spawn a real app binary. Uses the media app binary's output dir.
target_compile_definitions(echo-apps-tests PRIVATE
    ECHO_APPS_BIN_DIR="$<TARGET_FILE_DIR:echo-app-media-bin>"
)

# The isolation tests spawn the real app process, so it must exist first.
add_dependencies(echo-apps-tests echo-app-media-bin)

add_test(NAME echo-apps-smoke COMMAND echo-apps-tests)
