# echo-companion: BLE/WiFi sync to the caregiver app (alerts/status/firmware only).
add_library(echo-companion STATIC
    src/companion_sync.cpp
)

target_include_directories(echo-companion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-companion
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::companion ALIAS echo-companion)
