# echo-sensor: real-time sensor frontend capture + lock-free buffering.
add_library(echo-sensor STATIC
    src/sensor_pipeline.cpp
    src/stub_sources.cpp
    src/real_sources.cpp
)

target_include_directories(echo-sensor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-sensor
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

# Real laptop capture: webcam via OpenCV, mic via SDL (linked only when enabled).
if(ECHO_WITH_OPENCV)
    target_link_libraries(echo-sensor PRIVATE echo::dep-opencv)
endif()
if(ECHO_WITH_SDL)
    target_link_libraries(echo-sensor PRIVATE echo::dep-sdl)
endif()

add_library(echo::sensor ALIAS echo-sensor)
