# ===========================================
# Controller Unit Tests
# ===========================================

# Check if Frida is available
find_library(FRIDA_CORE_TEST
    NAMES frida-core
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
    NO_DEFAULT_PATH
)

if(FRIDA_CORE_TEST)
    # Spawn method test
    add_executable(test_spawn_method
        test_spawn_method.cpp
    )
    target_include_directories(test_spawn_method
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_spawn_method
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )
    
    # Test Discovery
    gtest_discover_tests(test_spawn_method
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit"
    )
    
    # Install target
    install(TARGETS
        test_spawn_method
        RUNTIME DESTINATION bin
    )

    # Heartbeat/unit controller behaviors
    add_executable(test_controller_heartbeat
        test_controller_heartbeat.cpp
    )
    target_include_directories(test_controller_heartbeat
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_controller_heartbeat
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    gtest_discover_tests(test_controller_heartbeat
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit"
    )

    install(TARGETS
        test_controller_heartbeat
        RUNTIME DESTINATION bin
    )

    # Environment propagation test
    add_executable(test_controller_env_propagation
        test_controller_env_propagation.cpp
    )
    target_include_directories(test_controller_env_propagation
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_controller_env_propagation
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    gtest_discover_tests(test_controller_env_propagation
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit"
    )

    install(TARGETS
        test_controller_env_propagation
        RUNTIME DESTINATION bin
    )

    # Controller coverage test
    add_executable(test_controller_coverage
        test_controller_coverage.cpp
    )
    target_include_directories(test_controller_coverage
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_controller_coverage
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    gtest_discover_tests(test_controller_coverage
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit"
    )

    install(TARGETS
        test_controller_coverage
        RUNTIME DESTINATION bin
    )

    # Startup timeout configuration tests (M1_E6_I1)
    add_executable(test_startup_timeout
        test_startup_timeout.cpp
    )
    target_include_directories(test_startup_timeout
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/src          # For frida_controller_internal.h
            ${CMAKE_BINARY_DIR}              # For ada_paths.h
    )
    target_link_libraries(test_startup_timeout
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    gtest_discover_tests(test_startup_timeout
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit"
    )

    install(TARGETS
        test_startup_timeout
        RUNTIME DESTINATION bin
    )
endif()

# Usage formatting test (does not require Frida)
add_executable(test_controller_usage
    test_controller_usage.cpp
)
target_include_directories(test_controller_usage
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_controller_usage
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_controller_cli
)

gtest_discover_tests(test_controller_usage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_controller_usage
    RUNTIME DESTINATION bin
)

# TEMPORARILY DISABLED - Uses AtfV4Writer API and drain_.atf_writer field, needs migration to AtfV2
#add_executable(test_signal_shutdown
#    test_signal_shutdown.cpp
#)
#target_include_directories(test_signal_shutdown
#    PRIVATE
#        ${CMAKE_SOURCE_DIR}/include
#        ${CMAKE_BINARY_DIR}  # For ada_paths.h
#)
#target_link_libraries(test_signal_shutdown
#    PRIVATE
#        test_main
#        GTest::gtest
#        GTest::gmock
#        tracer_controller_cli
#        tracer_utils
#        tracer_drain_thread
#        tracer_atf_writer
#        Threads::Threads
#)
#
#gtest_discover_tests(test_signal_shutdown
#    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#    DISCOVERY_MODE PRE_TEST
#    DISCOVERY_TIMEOUT 60
#    PROPERTIES LABELS "unit"
#)
#
#install(TARGETS
#    test_signal_shutdown
#    RUNTIME DESTINATION bin
#)

add_executable(test_controller_main
    test_controller_main.cpp
    controller_main_test_support.c
)
target_include_directories(test_controller_main
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_controller_main
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_controller_cli
)

gtest_discover_tests(test_controller_main
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_controller_main
    RUNTIME DESTINATION bin
)
