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

# Agent utility library (no frida dependency): exclude list and helpers used by tests
add_library(agent_utils STATIC
    exclude_list.cpp
    dso_management.cpp
    hook_registry.cpp
    comprehensive_hooks.cpp
    module_uuid.cpp
)

target_include_directories(agent_utils
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# Agent shared library
add_library(frida_agent SHARED
    frida_agent.cpp
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
        agent_utils
)

# Platform-specific settings
if(APPLE)
    target_link_libraries(frida_agent
        PUBLIC
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
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Sign the agent library on macOS
if(APPLE)
    # Check for APPLE_DEVELOPER_ID environment variable
    if(DEFINED ENV{APPLE_DEVELOPER_ID})
        set(SIGNING_IDENTITY "$ENV{APPLE_DEVELOPER_ID}")
        message(STATUS "Signing with identity: ${SIGNING_IDENTITY}")
    else()
        set(SIGNING_IDENTITY "-")
        message(WARNING "APPLE_DEVELOPER_ID not set. Using ad-hoc signing. 'debugger' entitlement will be ignored.")
    endif()

    add_custom_command(TARGET frida_agent POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force $<TARGET_FILE:frida_agent>
        COMMENT "Signing frida_agent with entitlements"
        VERBATIM
    )
endif()
