# ===========================================
# System Integration Tests
# ===========================================

add_executable(test_system_integration
    system_test_fixture.cpp
    system_stress_generator.cpp
    system_validator.cpp
    system_perf_monitor.cpp
    test_system_pipeline.cpp
)

target_include_directories(test_system_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_system_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_drain_thread
        tracer_utils
        tracer_atf_writer
        Threads::Threads
)

# Test discovery
gtest_discover_tests(test_system_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 180
    PROPERTIES LABELS "integration;system"
)

install(TARGETS
    test_system_integration
    RUNTIME DESTINATION bin
)
