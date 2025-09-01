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
# Unit Tests
# ===========================================

# Ring Buffer test
add_executable(test_ring_buffer
    unit/test_ring_buffer.cpp
)
target_link_libraries(test_ring_buffer
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_ring_buffer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Ring Buffer attach test
add_executable(test_ring_buffer_attach
    unit/test_ring_buffer_attach.cpp
)
target_link_libraries(test_ring_buffer_attach
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_ring_buffer_attach
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Shared Memory test
add_executable(test_shared_memory
    unit/test_shared_memory.cpp
)
target_link_libraries(test_shared_memory
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_shared_memory
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Thread Registry test
add_executable(test_thread_registry
    unit/test_thread_registry.cpp
)
target_include_directories(test_thread_registry
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/utils
        ${CMAKE_SOURCE_DIR}/src/utils  # For private headers
)
target_link_libraries(test_thread_registry
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

# Thread Registry C++ test
add_executable(test_thread_registry_cpp
    unit/test_thread_registry_cpp.cpp
)
target_include_directories(test_thread_registry_cpp
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/utils
        ${CMAKE_SOURCE_DIR}/src/utils  # For private headers
)
target_link_libraries(test_thread_registry_cpp
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

# ===========================================
# Integration Tests
# ===========================================

# Thread Registry Integration test
add_executable(test_thread_registry_integration
    integration/test_thread_registry_integration.cpp
)
target_include_directories(test_thread_registry_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils  # For private headers
)
target_link_libraries(test_thread_registry_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

# Controller tests (if Frida is available)
find_library(FRIDA_CORE_TEST
    NAMES frida-core
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
    NO_DEFAULT_PATH
)

if(FRIDA_CORE_TEST)
    # Spawn method test
    add_executable(test_spawn_method
        unit/test_spawn_method.cpp
    )
    target_include_directories(test_spawn_method
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_spawn_method
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Controller lifecycle test
    add_executable(test_controller_full_lifecycle
        integration/test_controller_full_lifecycle.cpp
    )
    target_include_directories(test_controller_full_lifecycle
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_controller_full_lifecycle
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Integration test
    add_executable(test_integration
        integration/test_integration.cpp
    )
    target_include_directories(test_integration
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_integration
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Agent loader test
    add_executable(test_agent_loader
        integration/test_agent_loader.cpp
    )
    target_include_directories(test_agent_loader
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_agent_loader
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Baseline hooks test
    add_executable(test_baseline_hooks
        integration/test_baseline_hooks.cpp
    )
    target_include_directories(test_baseline_hooks
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_baseline_hooks
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            frida_agent
            tracer_utils
            Threads::Threads
    )
endif()

# ===========================================
# Test Discovery and Custom Targets
# ===========================================

# Discover tests for CTest
gtest_discover_tests(test_ring_buffer
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_ring_buffer_attach
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_shared_memory
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_thread_registry
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_thread_registry_cpp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_thread_registry_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "integration"
)

if(FRIDA_CORE_TEST)
    gtest_discover_tests(test_spawn_method
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit"
    )
    gtest_discover_tests(test_controller_full_lifecycle
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_integration
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_agent_loader
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_baseline_hooks
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
endif()

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS
        test_ring_buffer
        test_ring_buffer_attach
        test_shared_memory
        test_thread_registry
        test_thread_registry_cpp
        test_thread_registry_integration
    COMMENT "Running all tests"
)

# Custom target to run only unit tests
add_custom_target(run_unit_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L unit --output-on-failure
    DEPENDS
        test_ring_buffer
        test_ring_buffer_attach
        test_shared_memory
        test_thread_registry
        test_thread_registry_cpp
    COMMENT "Running unit tests"
)

# Custom target to run only integration tests
add_custom_target(run_integration_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L integration --output-on-failure
    DEPENDS
        test_thread_registry_integration
    COMMENT "Running integration tests"
)

# ===========================================
# Install test executables so they are available to Rust tests
# ===========================================

install(TARGETS
    test_ring_buffer
    test_ring_buffer_attach
    test_shared_memory
    test_thread_registry
    test_thread_registry_cpp
    test_thread_registry_integration
    RUNTIME DESTINATION bin
)

if(FRIDA_CORE_TEST)
    install(TARGETS
        test_spawn_method
        test_controller_full_lifecycle
        test_integration
        test_agent_loader
        test_baseline_hooks
        RUNTIME DESTINATION bin
    )
endif()