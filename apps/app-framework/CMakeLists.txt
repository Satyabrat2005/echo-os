# app-framework — split into two targets so the isolation boundary is enforced
# by the build graph, not just by convention:
#
#   echo::app-sdk        what an APP links. Knows the app contract, permissions,
#                        the HUD primitives, the IPC codec, the intent parser, and
#                        the process-entry runner. Does NOT link any core module.
#
#   echo::app-framework  what the HOST links. The supervisor, the router, and the
#                        voice bridge — the only place the apps layer reaches the
#                        safety-critical core (through voice-ui's public API).

# --- SDK: the app-facing half ------------------------------------------------
add_library(echo-app-sdk STATIC
    src/permission.cpp
    src/ipc.cpp
    src/nlu.cpp
    src/runner.cpp
)

target_include_directories(echo-app-sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-app-sdk
    PUBLIC  echo::common echo::apps-hud
    PRIVATE echo-project-options
)

add_library(echo::app-sdk ALIAS echo-app-sdk)

# --- Host framework: the core-facing half ------------------------------------
add_library(echo-app-framework STATIC
    src/voice_bridge.cpp
    src/router.cpp
    src/process.cpp
    src/supervisor.cpp
)

target_include_directories(echo-app-framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-app-framework
    PUBLIC  echo::app-sdk echo::apps-hud echo::voice
    PRIVATE echo-project-options
)

# Windows needs no extra libs for CreateProcess/pipes (kernel32 is implicit);
# POSIX process control is in libc. Nothing to add here.

add_library(echo::app-framework ALIAS echo-app-framework)
