# ===========================================
# Tests Module
# ===========================================

# Only build tests if BUILD_TESTING is enabled
if(NOT BUILD_TESTING)
    return()
endif()

# Fetch GoogleTest (includes GMock)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()
include(GoogleTest)

# Common test main
add_library(test_main STATIC test_main.cpp)
target_link_libraries(test_main PUBLIC GTest::gtest GTest::gmock)
target_include_directories(test_main 
    PRIVATE 
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)

# ===========================================
# Test Fixtures - Helper programs for integration tests
# ===========================================

# test_cli - Simple CLI program for tracing tests
add_executable(test_cli fixtures/test_cli.c)
target_link_libraries(test_cli PRIVATE m)
# Place in bin/ directory as build.rs expects
set_target_properties(test_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# test_runloop - RunLoop-based program for macOS-specific tests
if(APPLE)
    add_executable(test_runloop fixtures/test_runloop.c)
    target_link_libraries(test_runloop PRIVATE 
        m
        "-framework CoreFoundation"
    )
    # Place in bin/ directory as build.rs expects
    set_target_properties(test_runloop PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# ===========================================
# Add Test Subdirectories
# ===========================================

# Unit tests
add_subdirectory(unit/utils)
add_subdirectory(unit/drain_thread)
add_subdirectory(unit/controller)
add_subdirectory(unit/agent)

# Integration tests
add_subdirectory(integration/utils)
add_subdirectory(integration/drain_thread)
add_subdirectory(integration/controller)
add_subdirectory(integration/agent)

# Benchmark tests (when available)
add_subdirectory(bench/utils)
add_subdirectory(bench/controller)
add_subdirectory(bench/agent)
add_subdirectory(bench/registry)

# ===========================================
# Custom Targets for Running Tests
# ===========================================

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)

# Custom target to run only unit tests
add_custom_target(run_unit_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L unit --output-on-failure
    COMMENT "Running unit tests"
)

# Custom target to run only integration tests
add_custom_target(run_integration_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L integration --output-on-failure
    COMMENT "Running integration tests"
)
