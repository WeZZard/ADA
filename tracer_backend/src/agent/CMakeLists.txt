# ===========================================
# Agent Library - Injected into target process
# ===========================================

# Find Frida Gum
find_library(FRIDA_GUM_LIBRARY
    NAMES frida-gum
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
    NO_DEFAULT_PATH
)

set(FRIDA_GUM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum)

if(NOT FRIDA_GUM_LIBRARY)
    message(FATAL_ERROR "Frida Gum library not found. Run init_third_parties.sh")
endif()

# Agent shared library
add_library(frida_agent SHARED
    frida_agent.c
)

target_include_directories(frida_agent
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${FRIDA_GUM_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frida_agent
    PUBLIC
        ${FRIDA_GUM_LIBRARY}
        tracer_utils
        Threads::Threads
)

# Platform-specific settings
if(APPLE)
    target_link_libraries(frida_agent
        PUBLIC
            "-framework Foundation"
            bsm
            dl
    )
    # Force correct install name
    set_target_properties(frida_agent PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(UNIX)
    target_link_libraries(frida_agent
        PUBLIC
            dl
    )
endif()

# Set output properties
set_target_properties(frida_agent PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE ON
)