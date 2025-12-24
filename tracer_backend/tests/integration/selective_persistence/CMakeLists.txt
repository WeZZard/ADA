# TEMPORARILY DISABLED - Uses AtfV4Writer/AtfV4WriterConfig API, needs migration to AtfV2
#add_executable(test_selective_persistence_integration
#    test_selective_persistence_integration.cpp
#)
#
#target_include_directories(test_selective_persistence_integration
#    PRIVATE
#        ${CMAKE_SOURCE_DIR}/include
#        ${CMAKE_SOURCE_DIR}/src
#        ${CMAKE_SOURCE_DIR}/src/utils
#)
#
#target_link_libraries(test_selective_persistence_integration
#    PRIVATE
#        test_main
#        GTest::gtest
#        GTest::gmock
#        tracer_selective_persistence
#        tracer_utils
#        tracer_atf_writer
#        Threads::Threads
#)
#
## Integration tests are more expensive, discover lazily
#
#gtest_discover_tests(test_selective_persistence_integration
#    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#    DISCOVERY_MODE PRE_TEST
#    DISCOVERY_TIMEOUT 120
#    PROPERTIES LABELS "integration"
#)
#
#install(TARGETS
#    test_selective_persistence_integration
#    RUNTIME DESTINATION bin
#)
