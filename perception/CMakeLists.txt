# echo-perception: quantized face/object recognition, wake-word, ASR.
add_library(echo-perception STATIC
    src/perception_engine.cpp
)

target_include_directories(echo-perception PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-perception
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::perception ALIAS echo-perception)
