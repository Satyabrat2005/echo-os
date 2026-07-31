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

# memory engine (Phase 15): real SQLite-backed store against fixture embeddings —
# CRUD, recurrence, the no-auto-create rule, the RAG medication query, utterance
# parsing, and a COMPILE-TIME extension of the companion-sync privacy proof (hence
# the echo::companion link). Runs entirely in the dependency-free stub build.
echo_add_core_test(echo-memory-tests memory_engine_test.cpp
    echo::common echo::memory echo::companion)
add_test(NAME echo-memory COMMAND echo-memory-tests)

# End-to-end: sensor-pipeline -> perception -> cognitive-core -> voice-ui, now with
# the memory engine attached (Phase 15 face-recall turn added).
echo_add_core_test(echo-e2e-tests e2e_pipeline_test.cpp
    echo::common echo::sensor echo::perception echo::cognitive echo::memory echo::voice)
add_test(NAME echo-e2e-fixture COMMAND echo-e2e-tests)

# --- Phase 17: fault injection ------------------------------------------------
# Drives the REAL runtime (echo::runtime) with fault-injecting fake engines, making
# each engine boundary throw / return an error / hang, and asserts containment,
# defined degraded behaviour, and watchdog recovery. Links the whole runtime (which
# transitively brings every core module + Threads). Fully in the stub build.
echo_add_core_test(echo-fault-injection-tests fault_injection_test.cpp echo::runtime)
add_test(NAME echo-fault-injection COMMAND echo-fault-injection-tests)

# --- Phase 18: power & thermal management ------------------------------------
# Drives the REAL runtime (echo::runtime) with the shared fault-injection fakes PLUS
# deterministic fake power/thermal sources, exercising the battery duty-cycle, the thermal
# throttle (relaxing the Phase 17 hang budget), the shared EngineDegraded alert path, the
# critical-battery reminder pass, and source-fault containment — alongside the pure policy
# functions. Fully in the dependency-free stub build (no real sensor). See docs/STATE.md
# for the policy-verified vs. hardware-unvalidated split.
echo_add_core_test(echo-power-mgmt-tests power_mgmt_test.cpp echo::runtime)
add_test(NAME echo-power-mgmt COMMAND echo-power-mgmt-tests)

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

# --- Phase 13: REAL wake-word verification -----------------------------------
# Real wake-word: the openWakeWord ONNX pipeline (on ONNX Runtime) against real
# models + Piper-synthesized speech, proving the wake phrase fires and silence /
# garble / ordinary speech do not. Built ONLY with -DECHO_WITH_OPENWAKEWORD=ON;
# needs the private wake_word.hpp from perception/src. Self-skips (exit 0, loud)
# when the flag is on but the model files aren't present. Runs in the "real-engines"
# CI job (which already provides Piper for synthesis).
if(ECHO_WITH_OPENWAKEWORD)
    echo_add_core_test(echo-real-wakeword-tests real_wakeword_test.cpp
        echo::common echo::perception)
    target_include_directories(echo-real-wakeword-tests PRIVATE ${CMAKE_SOURCE_DIR}/perception/src)
    add_test(NAME echo-real-wakeword COMMAND echo-real-wakeword-tests)
endif()

# --- Phase 19: REAL audio-robustness verification (noise/SNR) -----------------
# Mixes the noise beds into the clean speech/wake clips at defined SNR levels and
# measures the real whisper.cpp + openWakeWord engines' actual degradation, plus the
# single-mic pre-processing stage and the noise-driven safe-mode confidence gate.
# Built when EITHER real audio engine is compiled in; each engine's section is
# further #if-guarded inside the TU. Needs the private asr.hpp / wake_word.hpp from
# perception/src. Self-skips (exit 0, loud) when models or clips/beds are absent.
# Runs in the "real-engines" CI job (which provides Piper + generates the beds).
if(ECHO_WITH_WHISPER OR ECHO_WITH_OPENWAKEWORD)
    echo_add_core_test(echo-real-audio-robustness-tests audio_robustness_test.cpp
        echo::common echo::perception)
    target_include_directories(echo-real-audio-robustness-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/perception/src)
    add_test(NAME echo-real-audio-robustness COMMAND echo-real-audio-robustness-tests)
endif()

# --- Phase 12: REAL reasoning-LLM verification -------------------------------
# Real LLM: the llama.cpp adapter against a tiny quantized instruct GGUF, proving
# route-tag parsing and the safe-mode gate against a real model's real output.
# Built ONLY with -DECHO_WITH_LLAMA=ON; needs the private llm.hpp from
# cognitive-core/src (make_llm()/ILlm/LlmReply). Self-skips (exit 0, loud) when the
# flag is on but the model file isn't present. See the dedicated "real-llm" CI job.
if(ECHO_WITH_LLAMA)
    echo_add_core_test(echo-real-llm-tests real_llm_test.cpp
        echo::common echo::perception echo::cognitive)
    target_include_directories(echo-real-llm-tests PRIVATE ${CMAKE_SOURCE_DIR}/cognitive-core/src)
    add_test(NAME echo-real-llm COMMAND echo-real-llm-tests)
endif()
