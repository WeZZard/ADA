# ===========================================
# Symbol Resolver Library
# ===========================================
# Platform-agnostic C ABI with platform-specific implementations.
# Currently: macOS only
# TODO: Linux implementation (libdw)
# TODO: Windows implementation (Rust pdb crate via FFI)

# Common sources (shared across platforms)
set(SYMBOL_COMMON_SOURCES
    symbol_resolver.cpp
)

if(APPLE)
    # macOS implementation
    add_library(symbol_resolver STATIC
        ${SYMBOL_COMMON_SOURCES}
        resolver_macos.cpp
        dsym_locator_macos.cpp
        demangler.cpp
    )

    target_include_directories(symbol_resolver
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(symbol_resolver
        PRIVATE
            "-framework CoreFoundation"
    )

    # Optional: libdwarf for DWARF parsing (Phase 3)
    # find_package(LibDwarf)
    # if(LibDwarf_FOUND)
    #     target_link_libraries(symbol_resolver PRIVATE LibDwarf::LibDwarf)
    #     target_compile_definitions(symbol_resolver PRIVATE ADA_HAS_LIBDWARF=1)
    # endif()

    target_compile_features(symbol_resolver PRIVATE cxx_std_17)

elseif(UNIX)
    # Linux stub - TODO: implement with libdw
    add_library(symbol_resolver STATIC
        ${SYMBOL_COMMON_SOURCES}
        resolver_stub.cpp
    )

    target_include_directories(symbol_resolver
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(symbol_resolver PRIVATE cxx_std_17)

else()
    # Windows stub - TODO: implement with Rust pdb crate
    add_library(symbol_resolver STATIC
        ${SYMBOL_COMMON_SOURCES}
        resolver_stub.cpp
    )

    target_include_directories(symbol_resolver
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(symbol_resolver PRIVATE cxx_std_17)

endif()
