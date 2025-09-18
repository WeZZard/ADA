# ===========================================
# Drain Thread Integration Tests
# ===========================================

add_executable(test_drain_thread_integration
    test_drain_thread_integration.cpp
)

target_include_directories(test_drain_thread_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_drain_thread_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_drain_thread
        tracer_utils
        Threads::Threads
)

# Test discovery
gtest_discover_tests(test_drain_thread_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration"
)

install(TARGETS
    test_drain_thread_integration
    RUNTIME DESTINATION bin
)
