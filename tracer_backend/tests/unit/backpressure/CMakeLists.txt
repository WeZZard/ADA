# ===========================================
# Backpressure Unit Tests
# ===========================================

add_executable(test_backpressure_state
    test_backpressure_state.cpp
)

target_include_directories(test_backpressure_state
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils # for thread_registry_private helpers if needed
)

target_link_libraries(test_backpressure_state
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        tracer_backpressure
        Threads::Threads
)

gtest_discover_tests(test_backpressure_state
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

