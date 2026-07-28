# echo-cognitive: on-device LLM orchestration + safe-mode gating.
add_library(echo-cognitive STATIC
    src/cognitive_core.cpp
)

target_include_directories(echo-cognitive PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-cognitive
    PUBLIC  echo::common echo::perception
    PRIVATE echo-project-options
)

add_library(echo::cognitive ALIAS echo-cognitive)
