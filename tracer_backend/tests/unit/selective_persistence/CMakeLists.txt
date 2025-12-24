# ===========================================
# Selective Persistence Unit Tests
# ===========================================

add_executable(test_marking_policy
    test_marking_policy.cpp
)
target_link_libraries(test_marking_policy
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_selective_persistence
        tracer_utils
        Threads::Threads
)
target_include_directories(test_marking_policy
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils
)

gtest_discover_tests(test_marking_policy
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)


add_executable(test_selective_persistence_support
    test_selective_persistence_support.cpp
)
target_link_libraries(test_selective_persistence_support
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_selective_persistence
        Threads::Threads
)
target_include_directories(test_selective_persistence_support
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(test_selective_persistence_support
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

# TEMPORARILY DISABLED - Uses AtfV4Writer API, needs migration to AtfV2
#add_executable(test_detail_lane_control
#    test_detail_lane_control.cpp
#)
#target_link_libraries(test_detail_lane_control
#    PRIVATE
#        test_main
#        GTest::gtest
#        GTest::gmock
#        tracer_selective_persistence
#        tracer_utils
#        Threads::Threads
#)
#target_include_directories(test_detail_lane_control
#    PRIVATE
#        ${CMAKE_SOURCE_DIR}/include
#        ${CMAKE_SOURCE_DIR}/src/utils
#)
#
#gtest_discover_tests(test_detail_lane_control
#    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#    DISCOVERY_MODE PRE_TEST
#    DISCOVERY_TIMEOUT 60
#    PROPERTIES LABELS "unit"
#)

# TEMPORARILY DISABLED - Uses AtfV4Writer API, needs migration to AtfV2
#add_executable(test_coverage_gaps
#    test_coverage_gaps.cpp
#)
#target_link_libraries(test_coverage_gaps
#    PRIVATE
#        test_main
#        GTest::gtest
#        GTest::gmock
#        tracer_selective_persistence
#        tracer_utils
#        Threads::Threads
#)
#target_include_directories(test_coverage_gaps
#    PRIVATE
#        ${CMAKE_SOURCE_DIR}/include
#        ${CMAKE_SOURCE_DIR}/src/utils
#)
#
#gtest_discover_tests(test_coverage_gaps
#    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#    DISCOVERY_MODE PRE_TEST
#    DISCOVERY_TIMEOUT 60
#    PROPERTIES LABELS "unit"
#)

# TEMPORARILY DISABLED - Uses AtfV4Writer API, needs migration to AtfV2
#add_executable(test_force_coverage
#    test_force_coverage.cpp
#)
#target_link_libraries(test_force_coverage
#    PRIVATE
#        test_main
#        GTest::gtest
#        GTest::gmock
#        tracer_selective_persistence
#        tracer_utils
#        Threads::Threads
#)
#target_include_directories(test_force_coverage
#    PRIVATE
#        ${CMAKE_SOURCE_DIR}/include
#        ${CMAKE_SOURCE_DIR}/src/utils
#)
#
#gtest_discover_tests(test_force_coverage
#    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#    DISCOVERY_MODE PRE_TEST
#    DISCOVERY_TIMEOUT 60
#    PROPERTIES LABELS "unit"
#)
