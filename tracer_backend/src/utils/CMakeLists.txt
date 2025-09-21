# ===========================================
# Utils Library - Shared utilities
# ===========================================

# Source files for utils library
set(UTILS_SOURCES
    shared_memory.c
    shm_directory.c
    ring_buffer.cpp
    thread_registry.cpp
    spsc_queue.cpp
    ring_pool.cpp
    thread_pools.cpp
    ada_thread.c
    agent_mode.cpp
)

# Create static library
add_library(tracer_utils STATIC ${UTILS_SOURCES})

# Set include directories
target_include_directories(tracer_utils
    PUBLIC
        # Public headers for external use
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        # Private headers for internal use
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies
target_link_libraries(tracer_utils
    PUBLIC
        Threads::Threads
    PRIVATE
        tracer_backpressure
)

# Set compile features
target_compile_features(tracer_utils 
    PUBLIC 
        c_std_11
        cxx_std_17
)

# Export for use by other targets
set_target_properties(tracer_utils PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
