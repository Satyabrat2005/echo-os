# echo-os: the runtime library + reference entrypoint binary.
#
# The boot module is the top of the dependency graph — it links every other
# module and wires them into the core perception-to-response loop.
add_library(echo-runtime STATIC
    src/runtime.cpp
    src/engine_guard.cpp    # Phase 17: engine-boundary containment + hang bound
)

target_include_directories(echo-runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Phase 17: the watchdog runs hung engine calls on a worker thread and bounds them
# with std::async/std::future, so the runtime needs the platform threading library
# (a no-op on toolchains where std::thread needs no extra link, required on others).
find_package(Threads REQUIRED)

target_link_libraries(echo-runtime
    PUBLIC
        echo::common
        echo::sensor
        echo::memory
        echo::perception
        echo::cognitive
        echo::voice
        echo::companion
        echo::power
        Threads::Threads
    PRIVATE
        echo-project-options
)

add_library(echo::runtime ALIAS echo-runtime)

# The reference entrypoint binary.
add_executable(echo-os src/main.cpp)
target_link_libraries(echo-os PRIVATE echo::runtime echo-project-options)
