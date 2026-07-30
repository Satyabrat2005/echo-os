# Scaffold smoke tests — no external framework, wired into CTest.
add_executable(echo-tests test_main.cpp)

target_link_libraries(echo-tests PRIVATE
    echo::common
    echo::sensor
    echo::perception
    echo::cognitive
    echo-project-options
)

add_test(NAME echo-smoke COMMAND echo-tests)

# --- Phase 10: fixture-driven core-pipeline test harness ---------------------
# These targets feed the checked-in fixtures (tests/fixtures/) through the REAL
# core modules — the same spirit as apps/tests but for the core loop. Each is
# framework-free (check.hpp) and reads its fixtures via ECHO_FIXTURES_DIR so it
# runs from any working directory (CI included). See tests/fixtures/README.md.
set(ECHO_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# A helper so every harness target gets the same include path + fixtures define.
function(echo_add_core_test name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE ECHO_FIXTURES_DIR="${ECHO_FIXTURES_DIR}")
    target_link_libraries(${name} PRIVATE echo-project-options ${ARGN})
endfunction()

# sensor-pipeline: real capture/queue path with fixture-shaped frames.
echo_add_core_test(echo-sensor-tests sensor_pipeline_test.cpp echo::common echo::sensor)
add_test(NAME echo-sensor COMMAND echo-sensor-tests)

# perception: real engine routing/framing/fusion in stub-engine mode.
echo_add_core_test(echo-perception-tests perception_test.cpp echo::common echo::perception)
add_test(NAME echo-perception COMMAND echo-perception-tests)

# voice-ui: real to_utterance() tone mapping + stub shell (no SDL audio).
echo_add_core_test(echo-voice-tests voice_ui_test.cpp echo::common echo::voice echo::cognitive)
add_test(NAME echo-voice COMMAND echo-voice-tests)

# companion-sync: real message construction + compile-time privacy guarantee.
echo_add_core_test(echo-companion-tests companion_sync_test.cpp echo::common echo::companion)
add_test(NAME echo-companion COMMAND echo-companion-tests)

# End-to-end: sensor-pipeline -> perception -> cognitive-core -> voice-ui.
echo_add_core_test(echo-e2e-tests e2e_pipeline_test.cpp
    echo::common echo::sensor echo::perception echo::cognitive echo::voice)
add_test(NAME echo-e2e-fixture COMMAND echo-e2e-tests)

# --- Phase 11: REAL engine verification (only when the engine is compiled in) --
# These targets drive the REAL whisper.cpp / OpenCV / Piper engines (not the
# stubs) against real model weights, so each is built ONLY when its ECHO_WITH_*
# flag is ON. With every flag OFF (the default + the stub CI job) none of these
# exist, so the dependency-free build is completely unaffected. See the dedicated
# "real-engines" CI job, which turns the flags on and downloads the models. Each
# test self-skips (exit 0 with a loud message) when its flag is on but the model
# file isn't present, so a local flag-on build without downloads doesn't go red.

# Real ASR: whisper.cpp against the speech/silence/garble clips. Needs the private
# asr.hpp from perception/src.
if(ECHO_WITH_WHISPER)
    echo_add_core_test(echo-real-asr-tests real_asr_test.cpp echo::common echo::perception)
    target_include_directories(echo-real-asr-tests PRIVATE ${CMAKE_SOURCE_DIR}/perception/src)
    add_test(NAME echo-real-asr COMMAND echo-real-asr-tests)
endif()

# Real vision: YuNet/SFace against real face images. Needs the private vision.hpp
# and links OpenCV directly (the test uses cv::imread to load frames to BGR).
if(ECHO_WITH_OPENCV)
    echo_add_core_test(echo-real-vision-tests real_vision_test.cpp
        echo::common echo::perception echo::dep-opencv)
    target_include_directories(echo-real-vision-tests PRIVATE ${CMAKE_SOURCE_DIR}/perception/src)
    add_test(NAME echo-real-vision COMMAND echo-real-vision-tests)
endif()

# Real TTS: Piper synthesis for the neutral + reassuring tones. Needs the private
# tts_synth.hpp from voice-ui/src.
if(ECHO_WITH_PIPER)
    echo_add_core_test(echo-real-tts-tests real_tts_test.cpp
        echo::common echo::voice echo::cognitive)
    target_include_directories(echo-real-tts-tests PRIVATE ${CMAKE_SOURCE_DIR}/voice-ui/src)
    add_test(NAME echo-real-tts COMMAND echo-real-tts-tests)
endif()
