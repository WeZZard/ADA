# ATF v2 (raw binary format)
add_library(tracer_atf_writer STATIC
    thread_counters.c
    atf_index_writer.c
    atf_detail_writer.c
    atf_thread_writer.c
)

target_include_directories(tracer_atf_writer
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tracer_atf_writer
    PUBLIC
        c_std_11
)

set_target_properties(tracer_atf_writer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(tracer_atf_writer
    PUBLIC
        tracer_utils
)
