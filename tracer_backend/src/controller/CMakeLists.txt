# ===========================================
# Controller Library - Tracer controller
# ===========================================

# Find Frida Core
find_library(FRIDA_CORE_LIBRARY
    NAMES frida-core
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
    NO_DEFAULT_PATH
)

set(FRIDA_CORE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../third_parties/frida-core)

if(NOT FRIDA_CORE_LIBRARY)
    message(FATAL_ERROR "Frida Core library not found. Run init_third_parties.sh")
endif()

# Controller library
add_library(tracer_controller STATIC
    frida_controller.c
)

set_target_properties(tracer_controller PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

target_include_directories(tracer_controller
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(tracer_controller
    PUBLIC
        ${FRIDA_CORE_LIBRARY}
        tracer_utils
        Threads::Threads
)

# Platform-specific settings
if(APPLE)
    target_link_libraries(tracer_controller
        PUBLIC
            "-framework Foundation"
            "-framework AppKit"
            "-framework CoreFoundation"
            "-framework Security"
            "-framework IOKit"
            bsm
            dl
            resolv
    )
elseif(UNIX)
    target_link_libraries(tracer_controller
        PUBLIC
            dl
    )
endif()

# ===========================================
# Main tracer executable
# ===========================================
add_executable(tracer_poc
    main.c
)

target_include_directories(tracer_poc
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(tracer_poc
    PRIVATE
        tracer_controller
        tracer_utils
        Threads::Threads
)

# Platform-specific settings for executable
if(APPLE)
    target_link_libraries(tracer_poc
        PRIVATE
            "-framework Foundation"
            "-framework AppKit"
            "-framework CoreFoundation"
    )
endif()

# Set output name
set_target_properties(tracer_poc PROPERTIES
    OUTPUT_NAME tracer
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)