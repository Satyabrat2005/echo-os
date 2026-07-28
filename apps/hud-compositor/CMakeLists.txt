# echo-apps-hud: the HUD compositor — the single visual surface for all apps.
add_library(echo-apps-hud STATIC
    src/hud.cpp
)

target_include_directories(echo-apps-hud PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-apps-hud
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::apps-hud ALIAS echo-apps-hud)
