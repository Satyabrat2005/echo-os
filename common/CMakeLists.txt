# echo-common: shared vocabulary and utilities used by every runtime module.
add_library(echo-common STATIC
    src/types.cpp
    src/result.cpp
    src/latency.cpp
    src/log.cpp
    src/config.cpp
)

target_include_directories(echo-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Every module links this, so give it the project-wide warning/standard settings.
target_link_libraries(echo-common PUBLIC echo-project-options)

add_library(echo::common ALIAS echo-common)
