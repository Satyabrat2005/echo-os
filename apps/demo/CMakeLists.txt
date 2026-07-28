# echo-demo — the Phase 3 integrated laptop demo binary.
#
# The one place core + apps run together: perception -> cognitive -> voice on the
# laptop's real sensors, with app-intents routed through the apps Router and the
# response drawn on the SDL HUD overlay. It links the core pipeline modules and
# the app-framework/apps; it reuses the host's app registry so the app list stays
# defined in exactly one place.
add_executable(echo-demo echo_demo.cpp)

target_include_directories(echo-demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../host   # registry.hpp
)

target_link_libraries(echo-demo PRIVATE
    echo::perception
    echo::cognitive
    echo::voice
    echo::sensor
    echo::apps-hud
    echo::app-framework
    echo::app-media
    echo::app-browser
    echo::app-search
    echo::app-video
    echo::app-mail
    echo::app-telephony
    echo::app-camera
    echo::app-gallery
    echo-project-options
)

# `cmake --build <dir> --target run_demo` launches the demo with the HUD overlay.
# (The scripts/run_demo.* wrappers also configure the real-AI options for you.)
add_custom_target(run_demo
    COMMAND $<TARGET_FILE:echo-demo> --window
    DEPENDS echo-demo
    USES_TERMINAL
    COMMENT "Launching ECHO OS Phase 3 demo (webcam + wake word + HUD overlay)")
