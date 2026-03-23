cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# C++ test executable
# ---------------------------------------------------------------------------
add_executable(test_store
    test_store.cpp
)

target_link_libraries(test_store
    PRIVATE
        maintenance_state_store
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_store)

# ---------------------------------------------------------------------------
# CLI tool for cross-language compatibility tests
# ---------------------------------------------------------------------------
add_executable(mss_cli
    mss_cli.cpp
)

target_link_libraries(mss_cli
    PRIVATE
        maintenance_state_store
)
