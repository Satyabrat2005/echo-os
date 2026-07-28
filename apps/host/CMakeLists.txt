# echo-apps: the standalone laptop host for the whole apps layer.
#
# Links the host framework (router + supervisor + voice bridge) and every app
# library, so it can both spawn the app processes and — in --in-process mode —
# dispatch to them directly. Depends on the core voice-ui through the bridge only.
add_executable(echo-apps host.cpp)

target_include_directories(echo-apps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(echo-apps PRIVATE
    echo::app-framework
    echo::app-sdk
    echo::apps-hud
    echo::voice
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

# The host locates the app executables next to itself at runtime; make sure they
# are built before the host so a fresh `cmake --build` produces a runnable layer.
add_dependencies(echo-apps
    echo-app-media-bin echo-app-browser-bin echo-app-search-bin echo-app-video-bin
    echo-app-mail-bin echo-app-telephony-bin echo-app-camera-bin echo-app-gallery-bin
)
