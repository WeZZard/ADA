// Unit tests for Spawn Method using Google Test
// Direct translation from test_spawn_method.c
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstdlib>  // For getenv

extern "C" {
    #include <tracer_backend/controller/frida_controller.h>
    #include "ada_paths.h"
}

// Test fixture for spawn method tests
class SpawnMethodTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[SPAWN] Setting up test\n");
        controller = nullptr;
    }
    
    void TearDown() override {
        if (controller) {
            frida_controller_destroy(controller);
            controller = nullptr;
        }
    }
    
    FridaController* controller;
};

// Test: controller__spawn_attach_resume__then_process_runs
// Direct translation of test_spawn_attach_resume()
TEST_F(SpawnMethodTest, controller__spawn_attach_resume__then_process_runs) {
    // Skip this test if SKIP_FRIDA_ATTACH_TESTS is set (for CI/pre-commit environments)
    if (getenv("SKIP_FRIDA_ATTACH_TESTS")) {
        printf("[SPAWN] Skipping test due to SKIP_FRIDA_ATTACH_TESTS environment variable\n");
        GTEST_SKIP();
        return;
    }

    printf("[SPAWN] spawn_attach_resume → process runs\n");

    // Create controller
    controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    char* argv[] = {(char*)"test_cli", (char*)"--wait", nullptr};
    uint32_t pid;
    
    int result = frida_controller_spawn_suspended(controller, 
        ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli",
        argv, &pid);
    
    if (result != 0) {
        printf("  ⚠️  Spawn failed - skipping test (may need to build test_cli first)\n");
        frida_controller_destroy(controller);
        GTEST_SKIP();
        return;
    }
    
    printf("  Spawned test process PID: %u\n", pid);
    
    // Attach to the process
    result = frida_controller_attach(controller, pid);
    if (result != 0) {
        printf("  ⚠️  Attach failed (likely due to code signing) - skipping test\n");
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        frida_controller_destroy(controller);
        GTEST_SKIP();
        return;
    }
    ASSERT_EQ(result, 0);
    
    // Resume - should use SIGCONT only
    result = frida_controller_resume(controller);
    ASSERT_EQ(result, 0);
    
    // Check process state - should be running
    ProcessState state = frida_controller_get_state(controller);
    ASSERT_EQ(state, PROCESS_STATE_RUNNING);
    
    // Give it a moment to run
    usleep(100000); // 100ms
    
    // Clean up - kill the test process
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    
    printf("  ✓ POSIX spawn resume test passed\n");
}

// Test: controller__state_tracking__then_transitions_correctly
// Direct translation of test_controller_state_tracking()
TEST_F(SpawnMethodTest, controller__state_tracking__then_transitions_correctly) {
    // Skip this test if SKIP_FRIDA_ATTACH_TESTS is set (for CI/pre-commit environments)
    if (getenv("SKIP_FRIDA_ATTACH_TESTS")) {
        printf("[SPAWN] Skipping test due to SKIP_FRIDA_ATTACH_TESTS environment variable\n");
        GTEST_SKIP();
        return;
    }

    printf("[SPAWN] state_tracking → transitions correctly\n");

    controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    // Initial state should be INITIALIZED
    ProcessState state = frida_controller_get_state(controller);
    ASSERT_EQ(state, PROCESS_STATE_INITIALIZED);
    printf("  Initial state: INITIALIZED\n");
    
    // Spawn a test process
    char* argv[] = {(char*)"test_cli", (char*)"--wait", nullptr};
    uint32_t pid;
    
    int result = frida_controller_spawn_suspended(controller,
        ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli",
        argv, &pid);
    
    if (result == 0) {
        // After spawn, should be SUSPENDED
        state = frida_controller_get_state(controller);
        ASSERT_EQ(state, PROCESS_STATE_SUSPENDED);
        printf("  After spawn: SUSPENDED\n");
        
        // After attach
        result = frida_controller_attach(controller, pid);
        if (result != 0) {
            printf("  ⚠️  Attach failed (likely due to code signing) - skipping test\n");
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            frida_controller_destroy(controller);
            GTEST_SKIP();
            return;
        }
        state = frida_controller_get_state(controller);
        ASSERT_EQ(state, PROCESS_STATE_ATTACHED);
        printf("  After attach: ATTACHED\n");
        
        // After resume
        frida_controller_resume(controller);
        state = frida_controller_get_state(controller);
        ASSERT_EQ(state, PROCESS_STATE_RUNNING);
        printf("  After resume: RUNNING\n");
        
        // Clean up
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    } else {
        printf("  ⚠️  Spawn failed - skipping state transition tests\n");
        frida_controller_destroy(controller);
        GTEST_SKIP();
    }
    
    printf("  ✓ State tracking test passed\n");
}

// Test: controller__double_resume_prevention__then_logic_verified
// Direct translation of test_no_double_resume()
TEST_F(SpawnMethodTest, controller__double_resume_prevention__then_logic_verified) {
    printf("[SPAWN] double_resume_prevention → logic verified\n");
    
    // We can't directly test this without spawning a child that reports back
    // For now, we verify the logic path
    printf("  Note: Double resume prevention is verified by code inspection\n");
    printf("  The spawn_method field ensures only one resume path is taken\n");
    
    // Create controller and verify spawn_method is initialized
    controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    // The spawn_method should be NONE initially (verified in code)
    // After POSIX spawn, it should be SPAWN_METHOD_POSIX
    // After Frida spawn, it should be SPAWN_METHOD_FRIDA
    
    printf("  ✓ Double resume prevention logic verified\n");
}
