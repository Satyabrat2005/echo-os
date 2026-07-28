# echo-sensor: real-time sensor frontend capture + lock-free buffering.
add_library(echo-sensor STATIC
    src/sensor_pipeline.cpp
    src/stub_sources.cpp
)

target_include_directories(echo-sensor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-sensor
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::sensor ALIAS echo-sensor)
