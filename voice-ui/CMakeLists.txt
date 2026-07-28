# echo-voice: TTS output + audio-first interaction shell.
add_library(echo-voice STATIC
    src/voice_ui.cpp
)

target_include_directories(echo-voice PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-voice
    PUBLIC  echo::common echo::cognitive
    PRIVATE echo-project-options
)

add_library(echo::voice ALIAS echo-voice)
