#ifndef SYSTEM_TEST_FIXTURE_H
#define SYSTEM_TEST_FIXTURE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

extern "C" {
#include <tracer_backend/atf/atf_v4_writer.h>
#include <tracer_backend/utils/thread_registry.h>
}

struct test_fixture_t;

typedef enum {
    TEST_FIXTURE_MODE_SPAWN = 0,
    TEST_FIXTURE_MODE_ATTACH = 1
} test_fixture_mode_t;

struct test_fixture_options_t {
    test_fixture_mode_t mode{TEST_FIXTURE_MODE_SPAWN};
    uint32_t registry_capacity{16};
    bool enable_manifest{false};
    std::string session_label;
};

struct test_fixture_t {
    test_fixture_mode_t mode{TEST_FIXTURE_MODE_SPAWN};
    test_fixture_options_t options{};
    std::unique_ptr<uint8_t[]> registry_arena;
    size_t registry_bytes{0};
    ThreadRegistry* registry{nullptr};
    AtfV4Writer writer{};
    bool writer_initialized{false};
    std::string output_root;
    std::string session_dir;
    std::string events_path;
    std::atomic<uint64_t> spawn_operations{0};
    std::atomic<uint64_t> attach_operations{0};
    std::atomic<uint64_t> shutdown_operations{0};
    std::mutex process_mutex;
    pid_t child_pid{-1};
    bool child_running{false};
};

bool test_fixture_init(test_fixture_t* fixture, const test_fixture_options_t& options,
                       std::string* error_message);
void test_fixture_publish_trace_start(test_fixture_t* fixture,
                                      const std::vector<std::string>& argv);
void test_fixture_publish_trace_end(test_fixture_t* fixture, int32_t exit_code);

bool test_fixture_launch_target(test_fixture_t* fixture, const std::string& executable,
                                const std::vector<std::string>& args,
                                std::string* error_message);

bool test_fixture_attach_to_pid(test_fixture_t* fixture, pid_t pid,
                                std::string* error_message);

void test_fixture_shutdown(test_fixture_t* fixture);

ThreadRegistry* test_fixture_registry(test_fixture_t* fixture);
AtfV4Writer* test_fixture_writer(test_fixture_t* fixture);
std::string test_fixture_events_path(const test_fixture_t* fixture);
size_t test_fixture_registry_bytes(const test_fixture_t* fixture);
pid_t test_fixture_pid(const test_fixture_t* fixture);

#endif  // SYSTEM_TEST_FIXTURE_H
