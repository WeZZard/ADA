# ===========================================
# Metrics Unit Tests
# ===========================================

if (TARGET tracer_utils)
    target_compile_definitions(tracer_utils PUBLIC ADA_TESTING=1)
endif()

add_executable(test_thread_metrics
    test_thread_metrics.cpp
)

target_link_libraries(test_thread_metrics
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

target_include_directories(test_thread_metrics
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_SOURCE_DIR}/src/metrics
)

gtest_discover_tests(test_thread_metrics
    TEST_PREFIX metrics_
    PROPERTIES LABELS unit
)

# Coverage gap tests for metrics
add_executable(test_metrics_coverage
    test_coverage_gaps.cpp
)

target_link_libraries(test_metrics_coverage
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        tracer_drain_thread
        Threads::Threads
)

target_include_directories(test_metrics_coverage
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_SOURCE_DIR}/src/metrics
        ${CMAKE_SOURCE_DIR}/src/drain_thread
)

gtest_discover_tests(test_metrics_coverage
    TEST_PREFIX metrics_coverage_
    PROPERTIES LABELS unit
)

add_executable(test_metrics_reporter
    test_metrics_reporter.cpp
)

target_link_libraries(test_metrics_reporter
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

target_compile_definitions(test_metrics_reporter
    PRIVATE
        ADA_TESTING=1
)

target_include_directories(test_metrics_reporter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_SOURCE_DIR}/src/metrics
)

gtest_discover_tests(test_metrics_reporter
    TEST_PREFIX metrics_reporter_
    PROPERTIES LABELS unit
)
