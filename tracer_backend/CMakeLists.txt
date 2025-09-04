cmake_minimum_required(VERSION 3.15)
project(tracer_backend VERSION 0.1.0 LANGUAGES C CXX)

# ===========================================
# Global Settings
# ===========================================

# Set language standards
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

# ===========================================
# Global Compiler Flags
# ===========================================

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

# ===========================================
# Global Dependencies
# ===========================================

# Find required packages
find_package(Threads REQUIRED)

# Check for third-party dependencies
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_parties/frida-core")
    message(FATAL_ERROR 
        "Frida SDK not found. Please run:\n"
        "  ./utils/init_third_parties.sh\n"
        "from the repository root to download and extract Frida SDKs."
    )
endif()

# ===========================================
# Generate Configuration Headers
# ===========================================

# Convert CMAKE_BUILD_TYPE to lowercase for Rust compatibility
if(CMAKE_BUILD_TYPE)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" ADA_BUILD_PROFILE_LOWER)
else()
    set(ADA_BUILD_PROFILE_LOWER "debug")
endif()

# Generate ada_paths.h for tests
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ada_paths.h.in"
    "${CMAKE_BINARY_DIR}/ada_paths.h"
    @ONLY
)

# ===========================================
# Add Subdirectories
# ===========================================

# Build order matters for dependencies
add_subdirectory(src/utils)        # Utils library (no dependencies)
add_subdirectory(src/controller)   # Controller library (depends on utils)
add_subdirectory(src/agent)        # Agent library (depends on utils)

# C API library for Rust FFI
add_library(tracer_c_api SHARED src/tracer_c_api.cpp)
target_link_libraries(tracer_c_api 
    PRIVATE 
        tracer_controller 
        tracer_utils
)
target_include_directories(tracer_c_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build tests if enabled
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ===========================================
# Installation Rules
# ===========================================

# Install public headers
install(DIRECTORY include/tracer_backend
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Install libraries
install(TARGETS tracer_utils
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS tracer_controller
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS frida_agent
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS tracer_c_api
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install executables
install(TARGETS tracer_poc
    RUNTIME DESTINATION bin
)

# ===========================================
# Export Configuration
# ===========================================

# Create package configuration files
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tracer_backendConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Export targets (commented out for now - need to set up export properly)
# install(EXPORT tracer_backendTargets
#     FILE tracer_backendTargets.cmake
#     NAMESPACE tracer_backend::
#     DESTINATION lib/cmake/tracer_backend
# )

# ===========================================
# Summary
# ===========================================

message(STATUS "")
message(STATUS "tracer_backend configuration summary:")
message(STATUS "  Version:          ${PROJECT_VERSION}")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  Testing:          ${BUILD_TESTING}")
message(STATUS "  Coverage:         ${ENABLE_COVERAGE}")
message(STATUS "  Thread Sanitizer: ${ENABLE_THREAD_SANITIZER}")
message(STATUS "  Address Sanitizer: ${ENABLE_ADDRESS_SANITIZER}")
message(STATUS "")
message(STATUS "Modules:")
message(STATUS "  Utils:            src/utils")
message(STATUS "  Controller:       src/controller")
message(STATUS "  Agent:            src/agent")
if(BUILD_TESTING)
    message(STATUS "  Tests:            tests")
endif()
message(STATUS "")