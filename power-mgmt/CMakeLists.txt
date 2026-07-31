# echo-power: DVFS / duty-cycle scheduling balancing latency vs. battery, plus the
# Phase 18 power/thermal SOURCE boundary and the duty-cycle/throttle POLICY.
add_library(echo-power STATIC
    src/power_manager.cpp
    src/power_source.cpp   # IPowerSource/IThermalSource real-backend stubs (Phase 18)
    src/power_policy.cpp   # pure battery→duty-cycle / thermal→throttle policy (Phase 18)
)

target_include_directories(echo-power PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-power
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::power ALIAS echo-power)
