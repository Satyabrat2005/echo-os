# echo-safety: Phase 23 wandering/distress SOURCE boundary and POLICY, mirroring echo-power's
# Phase 18 structure exactly.
add_library(echo-safety STATIC
    src/safety_source.cpp   # ILocationSource/IArousalSource real-backend stubs (Phase 23)
    src/safety_policy.cpp   # pure location->wandering / arousal->distress policy (Phase 23)
)

target_include_directories(echo-safety PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-safety
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::safety ALIAS echo-safety)
