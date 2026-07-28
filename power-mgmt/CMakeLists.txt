# echo-power: DVFS / duty-cycle scheduling balancing latency vs. battery.
add_library(echo-power STATIC
    src/power_manager.cpp
)

target_include_directories(echo-power PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-power
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::power ALIAS echo-power)
