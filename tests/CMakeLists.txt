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
