# echo-perception: quantized face/object recognition, wake-word, ASR.
add_library(echo-perception STATIC
    src/perception_engine.cpp
    src/wake_word.cpp
    src/asr.cpp
    src/vision.cpp
)

target_include_directories(echo-perception PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-perception
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

# --- Phase 3 real engines (linked only when their option is ON) --------------
if(ECHO_WITH_PORCUPINE)
    target_link_libraries(echo-perception PRIVATE echo::dep-porcupine)
endif()
if(ECHO_WITH_WHISPER)
    target_link_libraries(echo-perception PRIVATE echo::dep-whisper)
endif()
if(ECHO_WITH_OPENCV)
    target_link_libraries(echo-perception PRIVATE echo::dep-opencv)
endif()

add_library(echo::perception ALIAS echo-perception)
