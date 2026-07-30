# ECHO OS — Phase 3 local-AI dependency wiring.
#
# Included once from the top-level CMakeLists. Each real engine is behind an
# option that defaults OFF, so the tree still configures and builds end-to-end
# with ZERO external dependencies (the stub fallback path). Flipping an option ON
# does two things:
#   1. adds a compile definition (ECHO_WITH_<X>) onto echo-project-options, which
#      every module links — so the real adapter's `#if defined(ECHO_WITH_<X>)`
#      code is compiled in place of its stub, and
#   2. resolves the corresponding library into an imported/interface target
#      `echo::dep-<x>` that the owning module links.
#
# Nothing here is target hardware — these are the laptop stand-ins named in the
# task (whisper.cpp, llama.cpp, Porcupine, Piper, OpenCV, SDL2). The embedded
# builds swap the same options for their on-SoC equivalents (see README Phase 3).

# --- The toggles ------------------------------------------------------------
option(ECHO_WITH_PORCUPINE   "Wake word via Picovoice Porcupine (perception)"        OFF)
option(ECHO_WITH_OPENWAKEWORD "Wake word via openWakeWord/ONNX Runtime (perception)" OFF)
option(ECHO_WITH_WHISPER   "Speech-to-text via whisper.cpp (perception)"    OFF)
option(ECHO_WITH_OPENCV    "Face/object recognition + camera via OpenCV"    OFF)
option(ECHO_WITH_LLAMA     "Reasoning via llama.cpp (cognitive-core)"       OFF)
option(ECHO_WITH_PIPER     "Text-to-speech via Piper (voice-ui)"           OFF)
option(ECHO_WITH_SDL       "SDL2 HUD overlay + laptop mic/speaker I/O"      OFF)

# Which wake-word backend make_wake_word() prefers when more than one is compiled
# in. Default openWakeWord — it is account-free (Apache-2.0 models, no Picovoice
# key), so it is the one CI and anyone without a Picovoice account can actually
# run. This only sets the *preference* among backends that are already compiled;
# it never forces a backend ON (that stays each option's job, so the default stub
# build has no wake-word dependency). Porcupine remains available/better-accuracy
# for a production build that has done the account/key step (Phase 3).
set(ECHO_WAKEWORD_BACKEND "openwakeword" CACHE STRING
    "Preferred wake-word backend when compiled in: openwakeword | porcupine")
set_property(CACHE ECHO_WAKEWORD_BACKEND PROPERTY STRINGS openwakeword porcupine)
if(ECHO_WAKEWORD_BACKEND STREQUAL "porcupine")
    target_compile_definitions(echo-project-options INTERFACE ECHO_WAKEWORD_PREFER_PORCUPINE=1)
endif()

# Convenience umbrella for the full laptop demo: -DECHO_REAL_AI=ON. The wake-word
# slot uses openWakeWord (account-free) so the umbrella build is runnable by
# anyone; Porcupine stays an explicit opt-in (-DECHO_WITH_PORCUPINE=ON) since it
# needs the account-gated SDK + key.
option(ECHO_REAL_AI "Enable the full real local-AI laptop stack" OFF)
if(ECHO_REAL_AI)
    set(ECHO_WITH_OPENWAKEWORD ON CACHE BOOL "" FORCE)
    set(ECHO_WITH_WHISPER   ON CACHE BOOL "" FORCE)
    set(ECHO_WITH_OPENCV    ON CACHE BOOL "" FORCE)
    set(ECHO_WITH_LLAMA     ON CACHE BOOL "" FORCE)
    set(ECHO_WITH_PIPER     ON CACHE BOOL "" FORCE)
    set(ECHO_WITH_SDL       ON CACHE BOOL "" FORCE)
endif()

# A place to hang the imported targets so module CMakeLists stay short.
add_library(echo-ai-deps INTERFACE)
add_library(echo::ai-deps ALIAS echo-ai-deps)

# --- Porcupine (no upstream CMake config; point at an unpacked SDK) ----------
if(ECHO_WITH_PORCUPINE)
    # Expected layout under PORCUPINE_ROOT (the picovoice/porcupine repo):
    #   include/pv_porcupine.h
    #   lib/<platform>/<arch>/libpv_porcupine.{so,dylib} | pv_porcupine.lib
    set(PORCUPINE_ROOT "" CACHE PATH "Root of the unpacked Porcupine SDK")
    find_path(PORCUPINE_INCLUDE_DIR pv_porcupine.h
        HINTS ${PORCUPINE_ROOT}/include)
    find_library(PORCUPINE_LIB NAMES pv_porcupine libpv_porcupine
        HINTS ${PORCUPINE_ROOT}/lib ${PORCUPINE_ROOT} PATH_SUFFIXES windows/amd64 linux/x86_64 mac/x86_64 mac/arm64)
    if(NOT PORCUPINE_INCLUDE_DIR OR NOT PORCUPINE_LIB)
        message(FATAL_ERROR
            "ECHO_WITH_PORCUPINE=ON but the SDK was not found. Set -DPORCUPINE_ROOT=<path to unpacked porcupine>. See README 'Phase 3'.")
    endif()
    add_library(echo-dep-porcupine INTERFACE)
    target_include_directories(echo-dep-porcupine INTERFACE ${PORCUPINE_INCLUDE_DIR})
    target_link_libraries(echo-dep-porcupine INTERFACE ${PORCUPINE_LIB})
    add_library(echo::dep-porcupine ALIAS echo-dep-porcupine)
    target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_PORCUPINE=1)
    message(STATUS "ECHO: Porcupine wake-word enabled (${PORCUPINE_LIB})")
endif()

# --- openWakeWord via ONNX Runtime (no upstream CMake config in the prebuilt) --
# The prebuilt ONNX Runtime release tarball (onnxruntime-linux-x64-*) ships just
# headers + libonnxruntime.so, no CMake package — so, like Porcupine, we point at
# an unpacked root and find_path/find_library it. MIT-licensed, downloaded free
# (no account/key), pinned + checksum-verified by the fetch script (see MANIFEST.md).
if(ECHO_WITH_OPENWAKEWORD)
    # Expected layout under ONNXRUNTIME_ROOT (the unpacked release tarball):
    #   include/onnxruntime_cxx_api.h   lib/libonnxruntime.so
    set(ONNXRUNTIME_ROOT "" CACHE PATH "Root of the unpacked ONNX Runtime release")
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS ${ONNXRUNTIME_ROOT}/include ${ONNXRUNTIME_ROOT})
    find_library(ONNXRUNTIME_LIB NAMES onnxruntime
        HINTS ${ONNXRUNTIME_ROOT}/lib ${ONNXRUNTIME_ROOT})
    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
        message(FATAL_ERROR
            "ECHO_WITH_OPENWAKEWORD=ON but ONNX Runtime was not found. Set "
            "-DONNXRUNTIME_ROOT=<path to unpacked onnxruntime-linux-x64-*>. See MANIFEST.md.")
    endif()
    add_library(echo-dep-onnxruntime INTERFACE)
    target_include_directories(echo-dep-onnxruntime INTERFACE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(echo-dep-onnxruntime INTERFACE ${ONNXRUNTIME_LIB})
    add_library(echo::dep-onnxruntime ALIAS echo-dep-onnxruntime)
    target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_OPENWAKEWORD=1)
    message(STATUS "ECHO: openWakeWord wake-word enabled (${ONNXRUNTIME_LIB})")
endif()

# --- whisper.cpp -------------------------------------------------------------
if(ECHO_WITH_WHISPER)
    # `cmake --install` of whisper.cpp exports a `whisper` config package.
    find_package(whisper CONFIG REQUIRED)
    add_library(echo-dep-whisper INTERFACE)
    target_link_libraries(echo-dep-whisper INTERFACE whisper)
    add_library(echo::dep-whisper ALIAS echo-dep-whisper)
    target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_WHISPER=1)
    message(STATUS "ECHO: whisper.cpp ASR enabled")
endif()

# --- llama.cpp ---------------------------------------------------------------
if(ECHO_WITH_LLAMA)
    find_package(llama CONFIG REQUIRED)
    add_library(echo-dep-llama INTERFACE)
    target_link_libraries(echo-dep-llama INTERFACE llama)
    add_library(echo::dep-llama ALIAS echo-dep-llama)
    target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_LLAMA=1)

    # The KV-cache clear call is the one symbol that moved across llama.cpp API
    # revisions; which one compiles depends on your checkout. Default to the
    # current memory API; flip this if the LLM build fails on that single line.
    #   current : llama_memory_clear(llama_get_memory(ctx), true)   (mid-2025+)
    #   self    : llama_kv_self_clear(ctx)                          (early–mid 2025)
    #   cache   : llama_kv_cache_clear(ctx)                         (pre-2025)
    set(ECHO_LLAMA_KV_CLEAR "current" CACHE STRING
        "llama.cpp KV-clear API variant: current | self | cache")
    set_property(CACHE ECHO_LLAMA_KV_CLEAR PROPERTY STRINGS current self cache)
    if(ECHO_LLAMA_KV_CLEAR STREQUAL "self")
        target_compile_definitions(echo-project-options INTERFACE ECHO_LLAMA_KV_CLEAR_SELF=1)
    elseif(ECHO_LLAMA_KV_CLEAR STREQUAL "cache")
        target_compile_definitions(echo-project-options INTERFACE ECHO_LLAMA_KV_CLEAR_CACHE=1)
    endif()
    message(STATUS "ECHO: llama.cpp reasoning enabled (KV-clear=${ECHO_LLAMA_KV_CLEAR})")
endif()

# --- OpenCV ------------------------------------------------------------------
if(ECHO_WITH_OPENCV)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio objdetect dnn)
    add_library(echo-dep-opencv INTERFACE)
    target_include_directories(echo-dep-opencv INTERFACE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(echo-dep-opencv INTERFACE ${OpenCV_LIBS})
    add_library(echo::dep-opencv ALIAS echo-dep-opencv)
    target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_OPENCV=1)
    message(STATUS "ECHO: OpenCV face/object + camera enabled (${OpenCV_VERSION})")
endif()

# --- Piper (invoked as a local subprocess; no link dependency) ---------------
# Piper ships a self-contained `piper` binary that reads text on stdin and emits
# raw PCM on stdout — fully local. We only need the code path, gated here; the
# binary is located at runtime via ECHO_PIPER_BIN (see echo/config.hpp). Playback
# reuses the SDL audio path, so Piper implies SDL.
if(ECHO_WITH_PIPER)
    if(NOT ECHO_WITH_SDL)
        message(STATUS "ECHO: ECHO_WITH_PIPER forces ECHO_WITH_SDL=ON (for playback)")
        set(ECHO_WITH_SDL ON CACHE BOOL "" FORCE)
    endif()
    target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_PIPER=1)
    message(STATUS "ECHO: Piper TTS enabled (subprocess)")
endif()

# --- SDL2 (HUD overlay window + laptop mic capture + speaker playback) -------
if(ECHO_WITH_SDL)
    find_package(SDL2 CONFIG REQUIRED)
    add_library(echo-dep-sdl INTERFACE)
    if(TARGET SDL2::SDL2)
        target_link_libraries(echo-dep-sdl INTERFACE SDL2::SDL2)
    else()
        target_include_directories(echo-dep-sdl INTERFACE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(echo-dep-sdl INTERFACE ${SDL2_LIBRARIES})
    endif()
    add_library(echo::dep-sdl ALIAS echo-dep-sdl)
    target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_SDL=1)
    message(STATUS "ECHO: SDL2 HUD overlay + audio I/O enabled")

    # SDL2_ttf is optional: when present, the HUD renders real subtitle text.
    # Without it the overlay still draws the icon + status dot (subtitle omitted).
    find_package(SDL2_ttf CONFIG QUIET)
    if(SDL2_ttf_FOUND OR TARGET SDL2_ttf::SDL2_ttf)
        add_library(echo-dep-sdl-ttf INTERFACE)
        if(TARGET SDL2_ttf::SDL2_ttf)
            target_link_libraries(echo-dep-sdl-ttf INTERFACE SDL2_ttf::SDL2_ttf)
        else()
            target_link_libraries(echo-dep-sdl-ttf INTERFACE ${SDL2_TTF_LIBRARIES})
            target_include_directories(echo-dep-sdl-ttf INTERFACE ${SDL2_TTF_INCLUDE_DIRS})
        endif()
        add_library(echo::dep-sdl-ttf ALIAS echo-dep-sdl-ttf)
        target_compile_definitions(echo-project-options INTERFACE ECHO_WITH_SDL_TTF=1)
        set(ECHO_HAVE_SDL_TTF ON CACHE INTERNAL "SDL2_ttf available")
        message(STATUS "ECHO: SDL2_ttf found — HUD subtitles enabled")
    else()
        message(STATUS "ECHO: SDL2_ttf not found — HUD will draw icon/status only")
    endif()
endif()
