# ===========================================
# Backpressure Benchmark Tests
# ===========================================

add_executable(bench_backpressure_overhead
    bench_backpressure_overhead.cpp
)

target_include_directories(bench_backpressure_overhead
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)

target_link_libraries(bench_backpressure_overhead
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_backpressure
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(bench_backpressure_overhead
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "bench"
)

install(TARGETS
    bench_backpressure_overhead
    RUNTIME DESTINATION bin
)

