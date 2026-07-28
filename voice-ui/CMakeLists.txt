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

# Piper TTS plays through SDL audio (ECHO_WITH_PIPER forces ECHO_WITH_SDL on).
if(ECHO_WITH_PIPER)
    target_link_libraries(echo-voice PRIVATE echo::dep-sdl)
endif()

add_library(echo::voice ALIAS echo-voice)
