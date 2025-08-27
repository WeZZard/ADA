cmake_minimum_required(VERSION 3.15)
project(tracer_backend VERSION 0.1.0 LANGUAGES C CXX)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Always generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTING "Build tests" ON)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
option(ENABLE_ADDRESS_SANITIZER "Enable AddressSanitizer" OFF)

# Add the cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../utils/cmake")

# Coverage flags
if(ENABLE_COVERAGE)
    message(STATUS "Coverage enabled")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()

# Sanitizer flags
if(ENABLE_THREAD_SANITIZER)
    message(STATUS "ThreadSanitizer enabled")
    add_compile_options(-fsanitize=thread -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if(ENABLE_ADDRESS_SANITIZER)
    message(STATUS "AddressSanitizer enabled")
    add_compile_options(-fsanitize=address -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_THREAD_SANITIZER AND ENABLE_ADDRESS_SANITIZER)
    message(FATAL_ERROR "Cannot enable both ThreadSanitizer and AddressSanitizer at the same time")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Fetch GoogleTest (includes GMock)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    enable_testing()
    include(GoogleTest)
endif()

# Set third_parties directory
set(THIRD_PARTIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_parties")

# Manually specify Frida paths since we have a custom setup
set(FRIDA_CORE_INCLUDE_DIR "${THIRD_PARTIES_DIR}/frida-core")
set(FRIDA_CORE_LIBRARY "${THIRD_PARTIES_DIR}/frida-core/libfrida-core.a")
set(FRIDA_GUM_INCLUDE_DIR "${THIRD_PARTIES_DIR}/frida-gum")
set(FRIDA_GUM_LIBRARY "${THIRD_PARTIES_DIR}/frida-gum/libfrida-gum.a")

# Verify Frida files exist
if(NOT EXISTS ${FRIDA_CORE_LIBRARY})
    message(FATAL_ERROR "Frida Core library not found. Run utils/init_third_parties.sh first.")
endif()
if(NOT EXISTS ${FRIDA_GUM_LIBRARY})
    message(FATAL_ERROR "Frida Gum library not found. Run utils/init_third_parties.sh first.")
endif()

message(STATUS "Found Frida Core: ${FRIDA_CORE_LIBRARY}")
message(STATUS "Found Frida Gum: ${FRIDA_GUM_LIBRARY}")

# ===========================================
# Shared utilities library
# ===========================================
add_library(tracer_utils STATIC
    src/shared_memory.c
    src/ring_buffer.c
    src/thread_registry.c
)

target_include_directories(tracer_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tracer_utils
    PUBLIC
        Threads::Threads
)

# ===========================================
# Controller Library (runs in tracer process)
# ===========================================
add_library(tracer_controller STATIC
    src/frida_controller.c
)

target_include_directories(tracer_controller
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(tracer_controller
    PUBLIC
        tracer_utils
        ${FRIDA_CORE_LIBRARY}
        Threads::Threads
)

# Platform-specific linking for Frida Core
if(APPLE)
    target_link_libraries(tracer_controller
        PUBLIC
            "-framework Foundation"
            "-framework AppKit"
            "-framework IOKit"
            "-framework Security"
            "-lbsm"
            "-lresolv"
    )
elseif(UNIX)
    target_link_libraries(tracer_controller
        PUBLIC
            pthread
            dl
    )
endif()

# ===========================================
# Agent Library (injected into target process)
# ===========================================
add_library(frida_agent SHARED
    src/frida_agent.c
)

target_include_directories(frida_agent
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_GUM_INCLUDE_DIR}
)

target_link_libraries(frida_agent
    PUBLIC
        tracer_utils
        ${FRIDA_GUM_LIBRARY}
        Threads::Threads
)

# Platform-specific linking for Frida Gum
if(APPLE)
    target_link_libraries(frida_agent
        PUBLIC
            "-framework Foundation"
            "-lresolv"
    )
    
    # Set proper rpath
    set_target_properties(frida_agent PROPERTIES
        INSTALL_RPATH "@loader_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(UNIX)
    target_link_libraries(frida_agent
        PUBLIC
            pthread
            dl
            m
    )
    
    set_target_properties(frida_agent PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Export the agent entry point
set_target_properties(frida_agent PROPERTIES
    COMPILE_FLAGS "-fvisibility=hidden"
)

# ===========================================
# Test Fixtures
# ===========================================
add_executable(test_cli
    tests/fixtures/test_cli.c
)

target_link_libraries(test_cli
    PRIVATE
        m
)

add_executable(test_runloop
    tests/fixtures/test_runloop.c
)

if(APPLE)
    target_link_libraries(test_runloop
        PRIVATE
            "-framework CoreFoundation"
    )
endif()

# ===========================================
# Main tracer executable
# ===========================================
add_executable(tracer_poc
    src/main.c
)

target_include_directories(tracer_poc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(tracer_poc
    PRIVATE
        tracer_controller
        tracer_utils
)

# ===========================================
# Tests
# ===========================================
enable_testing()

# ===========================================
# Google Test Integration
# ===========================================
if(BUILD_TESTING)
    # Test infrastructure: compiled-in defaults and test environment variables
    # These are provided by build.rs via -D on cmake configure
    if(NOT DEFINED ADA_WORKSPACE_ROOT)
        set(ADA_WORKSPACE_ROOT "${CMAKE_SOURCE_DIR}/..")
    endif()
    if(NOT DEFINED ADA_BUILD_PROFILE)
        set(ADA_BUILD_PROFILE $<CONFIG>)
    endif()

    # Generate a header with compile-time defaults
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/ada_paths.h.in
      ${CMAKE_CURRENT_BINARY_DIR}/generated/ada_paths.h
      @ONLY
    )
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)
    # Unit tests with Google Test
    add_executable(test_ring_buffer
        tests/unit/test_ring_buffer.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_ring_buffer
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(test_ring_buffer
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_utils
            Threads::Threads
    )
    
    add_executable(test_ring_buffer_attach
        tests/unit/test_ring_buffer_attach.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_ring_buffer_attach
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(test_ring_buffer_attach
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_utils
            Threads::Threads
    )
    
    add_executable(test_shared_memory
        tests/unit/test_shared_memory.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_shared_memory
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(test_shared_memory
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_utils
            Threads::Threads
    )
    
    # Unit test for spawn method
    add_executable(test_spawn_method
        tests/unit/test_spawn_method.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_spawn_method
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${FRIDA_CORE_INCLUDE_DIR}
    )
    
    target_link_libraries(test_spawn_method
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )
    
    # Unit test for thread registry
    add_executable(test_thread_registry
        tests/unit/test_thread_registry.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_thread_registry
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(test_thread_registry
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_utils
            Threads::Threads
    )
    
    # Integration test for controller full lifecycle
    add_executable(test_controller_full_lifecycle
        tests/integration/test_controller_full_lifecycle.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_controller_full_lifecycle
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${FRIDA_CORE_INCLUDE_DIR}
    )
    
    target_link_libraries(test_controller_full_lifecycle
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
            ${CMAKE_DL_LIBS}
    )
    
    # Integration test
    add_executable(test_integration
        tests/integration/test_integration.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_integration
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${FRIDA_CORE_INCLUDE_DIR}
    )
    
    target_link_libraries(test_integration
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )
    
    # Thread Registry Integration test
    add_executable(test_thread_registry_integration
        tests/integration/test_thread_registry_integration.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_thread_registry_integration
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(test_thread_registry_integration
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_utils
            Threads::Threads
    )
    
    # C++ Thread Registry test
    add_executable(test_thread_registry_cpp
        tests/unit/test_thread_registry_cpp.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_thread_registry_cpp
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(test_thread_registry_cpp
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_utils
            Threads::Threads
    )
    
    # Agent loader test for M1_E1_I1
    add_executable(test_agent_loader
        tests/integration/test_agent_loader.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_agent_loader
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${FRIDA_CORE_INCLUDE_DIR}
    )
    
    target_link_libraries(test_agent_loader
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )
    
    # Discover tests for CTest
    # Integration test for baseline hooks
    add_executable(test_baseline_hooks
        tests/integration/test_baseline_hooks.cpp
        tests/test_main.cpp
    )
    
    target_include_directories(test_baseline_hooks
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(test_baseline_hooks
        PRIVATE
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            ${FRIDA_CORE_LIB}
            Threads::Threads
    )
    
    
    gtest_discover_tests(test_ring_buffer
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_ring_buffer_attach
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_shared_memory
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_spawn_method
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_thread_registry
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_controller_full_lifecycle
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_integration
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_thread_registry_integration
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_agent_loader
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    gtest_discover_tests(test_baseline_hooks
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    
    # Add a custom target to run all GTests with nice output
    add_custom_target(run_gtests
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_ring_buffer --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_ring_buffer_attach --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_shared_memory --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_spawn_method --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_thread_registry --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_controller_full_lifecycle --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_integration --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_thread_registry_integration --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_agent_loader --gtest_color=yes
        COMMAND env ADA_BUILD_PROFILE=$<CONFIG> ADA_WORKSPACE_ROOT=${ADA_WORKSPACE_ROOT} test_baseline_hooks --gtest_color=yes
        DEPENDS test_ring_buffer test_ring_buffer_attach test_shared_memory test_spawn_method test_thread_registry test_controller_full_lifecycle test_integration test_thread_registry_integration test_agent_loader test_baseline_hooks
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

# ===========================================
# Installation
# ===========================================
install(TARGETS tracer_controller tracer_utils frida_agent tracer_poc test_cli test_runloop
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# ===========================================
# Summary
# ===========================================
message(STATUS "")
message(STATUS "tracer-backend configuration summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  C compiler:     ${CMAKE_C_COMPILER}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Components to build:")
message(STATUS "  Controller library: tracer_controller")
message(STATUS "  Agent library:      frida_agent")
message(STATUS "  Test CLI:          test_cli")
message(STATUS "  Test RunLoop:      test_runloop")
message(STATUS "  POC executable:    tracer_poc")
message(STATUS "")