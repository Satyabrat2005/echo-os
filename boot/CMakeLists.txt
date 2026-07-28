# echo-os: the runtime library + reference entrypoint binary.
#
# The boot module is the top of the dependency graph — it links every other
# module and wires them into the core perception-to-response loop.
add_library(echo-runtime STATIC
    src/runtime.cpp
)

target_include_directories(echo-runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-runtime
    PUBLIC
        echo::common
        echo::sensor
        echo::perception
        echo::cognitive
        echo::voice
        echo::companion
        echo::power
    PRIVATE
        echo-project-options
)

add_library(echo::runtime ALIAS echo-runtime)

# The reference entrypoint binary.
add_executable(echo-os src/main.cpp)
target_link_libraries(echo-os PRIVATE echo::runtime echo-project-options)
