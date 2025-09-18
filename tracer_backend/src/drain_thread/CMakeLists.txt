# ===========================================
# Drain Thread Library
# ===========================================

set(DRAIN_THREAD_SOURCES
    drain_thread.c
)

add_library(tracer_drain_thread STATIC ${DRAIN_THREAD_SOURCES})

target_include_directories(tracer_drain_thread
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(tracer_drain_thread
    PUBLIC
        tracer_utils
        Threads::Threads
)

target_compile_features(tracer_drain_thread
    PUBLIC
        c_std_11
        cxx_std_17
)

set_target_properties(tracer_drain_thread PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
