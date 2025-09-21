# ===========================================
# Backpressure library
# ===========================================

set(BACKPRESSURE_SOURCES
    backpressure.cpp
)

add_library(tracer_backpressure STATIC ${BACKPRESSURE_SOURCES})

target_include_directories(tracer_backpressure
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tracer_backpressure
    PUBLIC
        c_std_11
        cxx_std_17
)

set_target_properties(tracer_backpressure PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

