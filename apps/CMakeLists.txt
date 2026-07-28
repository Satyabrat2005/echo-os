# ECHO OS — apps layer.
#
# A voice-first application layer that runs ON TOP of the ECHO OS core, in its
# own set of processes. Nothing here may block, slow, or crash the core's
# sub-120ms perception -> cognitive -> voice safety loop (constraint #2): apps are
# separate executables, supervised by app-framework, reaching the core only
# through the narrow voice bridge and HUD compositor.
#
# This file can be built two ways:
#   * as a subdirectory of the echo-os top-level build (the normal path), or
#   * standalone on a laptop:  cmake -S apps -B build-apps
# so the whole layer is testable on a dev machine with the webcam/mic standing in
# for the glasses' sensors and every external service defaulted to --mock.
cmake_minimum_required(VERSION 3.16)

# --- Standalone support ------------------------------------------------------
# When configured directly (not via add_subdirectory from the parent), bootstrap
# just enough of the parent to build against: the project options carrier and the
# core modules the apps layer links (common + voice and its dependencies).
if(NOT DEFINED ECHO_APPS_IN_TREE)
    project(echo-os-apps LANGUAGES CXX)

    if(NOT TARGET echo-project-options)
        add_library(echo-project-options INTERFACE)
        target_compile_features(echo-project-options INTERFACE cxx_std_17)
        if(MSVC)
            target_compile_options(echo-project-options INTERFACE /W4 /permissive-)
        else()
            target_compile_options(echo-project-options INTERFACE -Wall -Wextra -Wpedantic)
        endif()
    endif()

    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Pull in the core modules the apps layer depends on, from the sibling tree.
    set(_core ${CMAKE_CURRENT_SOURCE_DIR}/..)
    add_subdirectory(${_core}/common          ${CMAKE_BINARY_DIR}/_core/common)
    add_subdirectory(${_core}/sensor-pipeline  ${CMAKE_BINARY_DIR}/_core/sensor)
    add_subdirectory(${_core}/perception       ${CMAKE_BINARY_DIR}/_core/perception)
    add_subdirectory(${_core}/cognitive-core   ${CMAKE_BINARY_DIR}/_core/cognitive)
    add_subdirectory(${_core}/voice-ui         ${CMAKE_BINARY_DIR}/_core/voice)

    enable_testing()
endif()

# Co-locate every app executable and the host in one directory, so the host
# (which spawns the app binaries as sibling processes) finds them next to itself
# regardless of the generator's default per-target layout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps-bin)

# --- Foundations: the HUD surface and the framework (SDK + host) --------------
add_subdirectory(hud-compositor)
add_subdirectory(app-framework)

# --- Each app: a library (testable logic) + an isolated process executable ----
# Every app has the same shape — include/echo/apps/<name>/<name>_app.hpp, a
# src/<name>_app.cpp with a make_<name>_app() factory, and a tiny src/main.cpp.
# This helper stamps out both the library and the standalone binary so an app
# never carries its own build boilerplate.
function(echo_add_app name)
    add_library(echo-app-${name} STATIC ${name}/src/${name}_app.cpp)
    target_include_directories(echo-app-${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${name}/include)
    target_link_libraries(echo-app-${name} PUBLIC echo::app-sdk PRIVATE echo-project-options)
    add_library(echo::app-${name} ALIAS echo-app-${name})

    add_executable(echo-app-${name}-bin ${name}/src/main.cpp)
    set_target_properties(echo-app-${name}-bin PROPERTIES OUTPUT_NAME echo-app-${name})
    target_link_libraries(echo-app-${name}-bin PRIVATE echo::app-${name} echo::app-sdk echo-project-options)
endfunction()

echo_add_app(media)
echo_add_app(browser)
echo_add_app(search)
echo_add_app(video)
echo_add_app(mail)
echo_add_app(telephony)
echo_add_app(camera)
echo_add_app(gallery)

# --- The laptop host + the smoke tests ---------------------------------------
add_subdirectory(host)
add_subdirectory(tests)

# --- Phase 3 integrated demo (core pipeline + apps + real HUD/sensors) --------
add_subdirectory(demo)

message(STATUS "ECHO OS apps layer configured (8 apps, mock default)")
