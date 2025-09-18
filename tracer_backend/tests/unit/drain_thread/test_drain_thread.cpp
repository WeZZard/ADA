#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <cstdlib>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
#include <pthread.h>
#include <tracer_backend/ada/thread.h>
#include <tracer_backend/drain_thread/drain_thread.h>
#include <tracer_backend/utils/thread_registry.h>

void drain_thread_test_set_state(DrainThread *drain, DrainState state);
void drain_thread_test_set_thread_started(DrainThread *drain, bool started);
void drain_thread_test_set_worker(DrainThread *drain, pthread_t worker);
void drain_thread_test_set_rr_cursor(DrainThread *drain, uint32_t value);
uint32_t drain_thread_test_get_rr_cursor(const DrainThread *drain);
void drain_thread_test_set_registry(DrainThread *drain, ThreadRegistry *registry);
uint32_t drain_thread_test_drain_lane(DrainThread *drain, uint32_t slot_index,
                                      Lane *lane, bool is_detail,
                                      bool final_pass, bool *out_hit_limit);
bool drain_thread_test_cycle(DrainThread *drain, bool final_pass);
void drain_thread_test_return_ring(Lane *lane, uint32_t ring_idx);
void *drain_thread_test_worker_entry(void *arg);
}

namespace {

struct HookConfig {
  bool override_mutex_init{false};
  int mutex_init_rc{0};
  bool override_pthread_create{false};
  int pthread_create_rc{0};
  bool override_pthread_join{false};
  int pthread_join_rc{0};
  int lane_return_failures{0};
  int lane_return_calls{0};
  int lane_return_successes{0};
  int pthread_mutex_init_calls{0};
  int pthread_create_calls{0};
  int pthread_join_calls{0};
  bool override_calloc{false};
  void *calloc_result{nullptr};
  int calloc_calls{0};
};

HookConfig &hook_state() {
  static HookConfig state;
  return state;
}

struct HookScope {
  HookScope() { reset(); }
  ~HookScope() { reset(); }

  static void reset() { hook_state() = HookConfig{}; }
};

void configure_mutex_init_failure(int rc) {
  auto &state = hook_state();
  state.override_mutex_init = true;
  state.mutex_init_rc = rc;
}

void configure_pthread_create_failure(int rc) {
  auto &state = hook_state();
  state.override_pthread_create = true;
  state.pthread_create_rc = rc;
}

void configure_pthread_join_override(int rc) {
  auto &state = hook_state();
  state.override_pthread_join = true;
  state.pthread_join_rc = rc;
}

void configure_pthread_join_failure(int rc) { configure_pthread_join_override(rc); }

void configure_lane_return_failures(int failures) {
  auto &state = hook_state();
  state.lane_return_failures = failures;
}

void configure_lane_return_successes(int successes) {
  auto &state = hook_state();
  state.lane_return_successes = successes;
}

int lane_return_call_count() { return hook_state().lane_return_calls; }

int pthread_join_call_count() { return hook_state().pthread_join_calls; }

void configure_calloc_failure() {
  auto &state = hook_state();
  state.override_calloc = true;
  state.calloc_result = nullptr;
}

struct RegistryHarness {
  explicit RegistryHarness(size_t capacity) {
    size_t bytes = thread_registry_calculate_memory_size_with_capacity(
        static_cast<uint32_t>(capacity));
    constexpr size_t kAlignment = 64;
    void *raw = nullptr;
    int rc = posix_memalign(&raw, kAlignment, bytes);
    EXPECT_EQ(rc, 0);
    arena.reset(static_cast<uint8_t *>(raw));
    if (!arena) {
      return;
    }
    std::memset(arena.get(), 0, bytes);
    registry = thread_registry_init_with_capacity(
        arena.get(), bytes, static_cast<uint32_t>(capacity));
    EXPECT_NE(registry, nullptr);
    if (registry) {
      EXPECT_NE(thread_registry_attach(registry), nullptr);
    }
  }

  ~RegistryHarness() {
    if (registry) {
      thread_registry_deinit(registry);
    }
    ada_set_global_registry(nullptr);
  }

  RegistryHarness(const RegistryHarness &) = delete;
  RegistryHarness &operator=(const RegistryHarness &) = delete;

  std::unique_ptr<uint8_t, decltype(&std::free)> arena{nullptr, &std::free};
  ThreadRegistry *registry{nullptr};
};

DrainThread *create_drain(RegistryHarness &harness,
                          const DrainConfig *config = nullptr) {
  DrainThread *drain = drain_thread_create(harness.registry, config);
  EXPECT_NE(drain, nullptr);
  return drain;
}

template <typename Predicate>
DrainMetrics wait_for_metrics(
    DrainThread *drain, Predicate predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
    std::chrono::milliseconds step = std::chrono::milliseconds(1)) {
  DrainMetrics metrics{};
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout) {
    drain_thread_get_metrics(drain, &metrics);
    if (predicate(metrics)) {
      return metrics;
    }
    std::this_thread::sleep_for(step);
  }
  drain_thread_get_metrics(drain, &metrics);
  return metrics;
}

bool submit_ring_with_retry(Lane *lane, int max_attempts = 1000) {
  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    uint32_t ring = lane_get_free_ring(lane);
    if (ring != UINT32_MAX) {
      return lane_submit_ring(lane, ring);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  return false;
}

} // namespace

extern "C" int drain_thread_test_override_pthread_mutex_init(
    pthread_mutex_t *, const pthread_mutexattr_t *, bool *handled) {
  if (!handled) {
    return 0;
  }
  auto &state = hook_state();
  ++state.pthread_mutex_init_calls;
  if (state.override_mutex_init) {
    *handled = true;
    return state.mutex_init_rc;
  }
  *handled = false;
  return 0;
}

extern "C" int drain_thread_test_override_pthread_create(
    pthread_t *, const pthread_attr_t *, void *(*)(void *), void *, bool *handled) {
  if (!handled) {
    return 0;
  }
  auto &state = hook_state();
  ++state.pthread_create_calls;
  if (state.override_pthread_create) {
    *handled = true;
    return state.pthread_create_rc;
  }
  *handled = false;
  return 0;
}

extern "C" int drain_thread_test_override_pthread_join(pthread_t,
                                                        void **,
                                                        bool *handled) {
  if (!handled) {
    return 0;
  }
  auto &state = hook_state();
  ++state.pthread_join_calls;
  if (state.override_pthread_join) {
    *handled = true;
    return state.pthread_join_rc;
  }
  *handled = false;
  return 0;
}

extern "C" bool drain_thread_test_override_lane_return_ring(Lane *,
                                                             uint32_t,
                                                             bool *handled) {
  if (!handled) {
    return false;
  }
  auto &state = hook_state();
  if (state.lane_return_failures > 0) {
    --state.lane_return_failures;
    ++state.lane_return_calls;
    *handled = true;
    return false;
  }
  if (state.lane_return_successes > 0) {
    --state.lane_return_successes;
    ++state.lane_return_calls;
    *handled = true;
    return true;
  }
  ++state.lane_return_calls;
  *handled = false;
  return false;
}

extern "C" void *drain_thread_test_override_calloc(size_t nmemb, size_t size,
                                                    bool *handled) {
  if (!handled) {
    return nullptr;
  }
  auto &state = hook_state();
  ++state.calloc_calls;
  if (state.override_calloc) {
    *handled = true;
    (void)nmemb;
    (void)size;
    return state.calloc_result;
  }
  *handled = false;
  return nullptr;
}

TEST(DrainThreadUnit,
     drain_thread__create_with_defaults__then_initialized_state) {
  RegistryHarness harness(4);
  DrainThread *drain = drain_thread_create(harness.registry, nullptr);
  ASSERT_NE(drain, nullptr);
  EXPECT_EQ(drain_thread_get_state(drain), DRAIN_STATE_INITIALIZED);

  DrainMetrics metrics{};
  drain_thread_get_metrics(drain, &metrics);
  EXPECT_EQ(metrics.cycles_total, 0u);
  EXPECT_EQ(metrics.rings_total, 0u);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__create_with_null_registry__then_returns_null) {
  EXPECT_EQ(drain_thread_create(nullptr, nullptr), nullptr);
}

TEST(DrainThreadUnit,
     drain_thread__start_and_stop_without_work__then_idle_metrics_increment) {
  RegistryHarness harness(4);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = true;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);

  ASSERT_EQ(drain_thread_start(drain), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  ASSERT_EQ(drain_thread_stop(drain), 0);
  EXPECT_EQ(drain_thread_get_state(drain), DRAIN_STATE_STOPPED);

  DrainMetrics metrics{};
  drain_thread_get_metrics(drain, &metrics);
  EXPECT_GT(metrics.cycles_total, 0u);
  EXPECT_EQ(metrics.rings_total, 0u);
  EXPECT_GT(metrics.cycles_idle, 0u);
  EXPECT_GT(metrics.yields, 0u);
  EXPECT_EQ(metrics.sleeps, 0u);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__idle_with_sleep_config__then_sleeps_and_tracks_duration) {
  RegistryHarness harness(2);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 2000; // 2ms sleep
  config.yield_on_idle = false;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);

  ASSERT_EQ(drain_thread_start(drain), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_EQ(drain_thread_stop(drain), 0);

  DrainMetrics metrics{};
  drain_thread_get_metrics(drain, &metrics);
  EXPECT_GE(metrics.sleeps, 1u);
  EXPECT_EQ(metrics.yields, 0u);
  EXPECT_GE(metrics.total_sleep_us,
            static_cast<uint64_t>(config.poll_interval_us));

  drain_thread_destroy(drain);
}

TEST(
    DrainThreadUnit,
    drain_thread__process_single_lane__then_ring_returned_and_metrics_updated) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0xABC1);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = true;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  ASSERT_TRUE(submit_ring_with_retry(index_lane));

  DrainMetrics metrics = wait_for_metrics(
      drain, [](const DrainMetrics &m) { return m.rings_total >= 1u; });
  EXPECT_GE(metrics.rings_total, 1u);
  EXPECT_GE(metrics.rings_index, 1u);

  EXPECT_EQ(lane_take_ring(index_lane), UINT32_MAX);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__detail_lane_submission__then_detail_metrics_increment) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0xBC02);
  ASSERT_NE(lanes, nullptr);
  Lane *detail_lane = thread_lanes_get_detail_lane(lanes);
  ASSERT_NE(detail_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  ASSERT_TRUE(submit_ring_with_retry(detail_lane));

  DrainMetrics metrics = wait_for_metrics(
      drain, [](const DrainMetrics &m) { return m.rings_detail >= 1u; });
  EXPECT_GE(metrics.rings_detail, 1u);
  EXPECT_EQ(metrics.rings_index, 0u);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__fair_round_robin__then_all_threads_progress) {
  RegistryHarness harness(8);

  ThreadLaneSet *lanes_a = thread_registry_register(harness.registry, 0x1111);
  ASSERT_NE(lanes_a, nullptr);
  ThreadLaneSet *lanes_b = thread_registry_register(harness.registry, 0x2222);
  ASSERT_NE(lanes_b, nullptr);

  Lane *index_a = thread_lanes_get_index_lane(lanes_a);
  Lane *index_b = thread_lanes_get_index_lane(lanes_b);
  ASSERT_NE(index_a, nullptr);
  ASSERT_NE(index_b, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = true;
  config.max_batch_size = 1;
  config.fairness_quantum = 1;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  ASSERT_TRUE(submit_ring_with_retry(index_a));
  ASSERT_TRUE(submit_ring_with_retry(index_a));
  ASSERT_TRUE(submit_ring_with_retry(index_a));
  ASSERT_TRUE(submit_ring_with_retry(index_b));

  DrainMetrics metrics = wait_for_metrics(
      drain, [](const DrainMetrics &m) { return m.rings_total >= 4u; });
  EXPECT_GE(metrics.rings_total, 4u);

  ThreadLaneSet *slot0 = thread_registry_get_thread_at(harness.registry, 0);
  ThreadLaneSet *slot1 = thread_registry_get_thread_at(harness.registry, 1);
  ASSERT_TRUE(slot0 == lanes_a || slot1 == lanes_a);
  ASSERT_TRUE(slot0 == lanes_b || slot1 == lanes_b);

  size_t index_slot = (slot0 == lanes_a) ? 0 : 1;
  size_t other_slot = index_slot == 0 ? 1 : 0;

  EXPECT_GE(metrics.rings_per_thread[index_slot][0], 3u);
  EXPECT_GE(metrics.rings_per_thread[other_slot][0], 1u);
  EXPECT_GT(metrics.fairness_switches, 0u);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__fairness_quantum_hit__then_switch_counter_increments) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x3333);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  Lane *detail_lane = thread_lanes_get_detail_lane(lanes);
  ASSERT_NE(index_lane, nullptr);
  ASSERT_NE(detail_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;
  config.max_batch_size = 4;
  config.fairness_quantum = 2;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(
        submit_ring_with_retry((i % 2 == 0) ? index_lane : detail_lane));
  }

  DrainMetrics metrics = wait_for_metrics(
      drain, [](const DrainMetrics &m) { return m.rings_total >= 6u; });
  EXPECT_GE(metrics.fairness_switches, 1u);
  EXPECT_GE(metrics.rings_index, 3u);
  EXPECT_GE(metrics.rings_detail, 3u);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__limit_zero_values__then_handles_quantum_and_unbounded) {
  HookScope guard;
  RegistryHarness harness(2);
  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x6677);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig quantum_only;
  drain_config_default(&quantum_only);
  quantum_only.max_batch_size = 0;
  quantum_only.fairness_quantum = 3;

  DrainThread *drain_quantum = create_drain(harness, &quantum_only);
  ASSERT_NE(drain_quantum, nullptr);
  bool hit_limit = true;
  EXPECT_EQ(drain_thread_test_drain_lane(drain_quantum, 0, index_lane, false, false,
                                         &hit_limit),
            0u);
  EXPECT_FALSE(hit_limit);
  drain_thread_destroy(drain_quantum);

  DrainConfig unbounded;
  drain_config_default(&unbounded);
  unbounded.max_batch_size = 0;
  unbounded.fairness_quantum = 0;

  DrainThread *drain_unbounded = create_drain(harness, &unbounded);
  ASSERT_NE(drain_unbounded, nullptr);
  hit_limit = true;
  EXPECT_EQ(drain_thread_test_drain_lane(drain_unbounded, 0, index_lane, false, false,
                                         &hit_limit),
            0u);
  EXPECT_FALSE(hit_limit);
  drain_thread_destroy(drain_unbounded);
}

TEST(DrainThreadUnit,
     drain_thread__detail_lane_limit_hit__then_fairness_switches_increment) {
  HookScope guard;
  RegistryHarness harness(2);
  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x6688);
  ASSERT_NE(lanes, nullptr);
  Lane *detail_lane = thread_lanes_get_detail_lane(lanes);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(detail_lane, nullptr);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.max_batch_size = 1;
  config.fairness_quantum = 1;
  config.poll_interval_us = 0;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);

  ASSERT_TRUE(submit_ring_with_retry(detail_lane));

  DrainMetrics before{};
  drain_thread_get_metrics(drain, &before);

  EXPECT_TRUE(drain_thread_test_cycle(drain, false));

  DrainMetrics after{};
  drain_thread_get_metrics(drain, &after);
  EXPECT_GT(after.fairness_switches, before.fairness_switches);
  EXPECT_GE(after.rings_detail, before.rings_detail + 1u);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__final_drain_flushes_pending_work__then_all_rings_processed) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x4444);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;
  config.max_batch_size = 1;
  config.fairness_quantum = 1;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  constexpr uint32_t kRings = 6;
  for (uint32_t i = 0; i < kRings; ++i) {
    ASSERT_TRUE(submit_ring_with_retry(index_lane));
  }

  ASSERT_EQ(drain_thread_stop(drain), 0);

  DrainMetrics metrics{};
  drain_thread_get_metrics(drain, &metrics);
  EXPECT_EQ(metrics.rings_total, kRings);
  EXPECT_GE(metrics.final_drains, 1u);
  EXPECT_EQ(lane_take_ring(index_lane), UINT32_MAX);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__destroy_while_running__then_stop_is_invoked) {
  RegistryHarness harness(2);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x5555);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  ASSERT_TRUE(submit_ring_with_retry(index_lane));

  drain_thread_destroy(drain);

  uint32_t ring = lane_get_free_ring(index_lane);
  EXPECT_NE(ring, UINT32_MAX);
  if (ring != UINT32_MAX) {
    EXPECT_TRUE(lane_return_ring(index_lane, ring));
  }
}

TEST(DrainThreadUnit, drain_thread__stop_without_start__then_returns_success) {
  RegistryHarness harness(2);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  EXPECT_EQ(drain_thread_stop(drain), 0);
  EXPECT_EQ(drain_thread_get_state(drain), DRAIN_STATE_INITIALIZED);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__start_double_invocation__then_second_call_is_noop) {
  RegistryHarness harness(2);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  ASSERT_EQ(drain_thread_start(drain), 0);
  ASSERT_EQ(drain_thread_start(drain), 0);
  EXPECT_EQ(drain_thread_get_state(drain), DRAIN_STATE_RUNNING);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit, drain_thread__start_after_stop__then_returns_ealready) {
  RegistryHarness harness(2);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);

  ASSERT_EQ(drain_thread_start(drain), 0);
  ASSERT_EQ(drain_thread_stop(drain), 0);
  EXPECT_EQ(drain_thread_get_state(drain), DRAIN_STATE_STOPPED);
  EXPECT_EQ(drain_thread_start(drain), -EALREADY);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__update_config_before_start__then_new_config_applied) {
  RegistryHarness harness(2);

  DrainConfig initial{};
  drain_config_default(&initial);
  initial.poll_interval_us = 0;
  initial.yield_on_idle = false;

  DrainThread *drain = create_drain(harness, &initial);
  ASSERT_NE(drain, nullptr);

  DrainConfig updated = initial;
  updated.poll_interval_us = 3000;
  updated.yield_on_idle = false;

  ASSERT_EQ(drain_thread_update_config(drain, &updated), 0);
  ASSERT_EQ(drain_thread_start(drain), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_EQ(drain_thread_stop(drain), 0);

  DrainMetrics metrics{};
  drain_thread_get_metrics(drain, &metrics);
  EXPECT_GE(metrics.sleeps, 1u);
  EXPECT_EQ(metrics.yields, 0u);
  EXPECT_GE(metrics.total_sleep_us,
            static_cast<uint64_t>(updated.poll_interval_us));

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__update_config_while_running__then_returns_busy) {
  RegistryHarness harness(2);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  ASSERT_EQ(drain_thread_start(drain), 0);

  DrainConfig config;
  drain_config_default(&config);
  EXPECT_EQ(drain_thread_update_config(drain, &config), -EBUSY);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__concurrent_stop_calls__then_all_callers_succeed) {
  RegistryHarness harness(4);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  ASSERT_EQ(drain_thread_start(drain), 0);

  constexpr int kStopThreads = 4;
  std::vector<int> results(kStopThreads, -1);
  std::vector<std::thread> stoppers;
  stoppers.reserve(kStopThreads);
  for (int i = 0; i < kStopThreads; ++i) {
    stoppers.emplace_back([&, i]() { results[i] = drain_thread_stop(drain); });
  }
  for (auto &t : stoppers) {
    t.join();
  }

  int zero_count = 0;
  for (int rc : results) {
    EXPECT_TRUE(rc == 0 || rc == EINVAL) << "unexpected rc=" << rc;
    if (rc == 0) {
      ++zero_count;
    }
  }
  EXPECT_GE(zero_count, 1);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__metrics_snapshot_with_null_target__then_no_crash) {
  RegistryHarness harness(2);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  drain_thread_get_metrics(drain, nullptr);

  DrainMetrics metrics{};
  metrics.cycles_total = 321u;
  drain_thread_get_metrics(nullptr, &metrics);
  EXPECT_EQ(metrics.cycles_total, 321u);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__state_and_api_null_inputs__then_reports_errors) {
  EXPECT_EQ(drain_thread_get_state(nullptr), DRAIN_STATE_UNINITIALIZED);
  EXPECT_EQ(drain_thread_start(nullptr), -EINVAL);
  EXPECT_EQ(drain_thread_stop(nullptr), -EINVAL);

  DrainConfig config{};
  drain_config_default(&config);

  EXPECT_EQ(drain_thread_update_config(nullptr, &config), -EINVAL);
}

TEST(DrainThreadUnit, drain_thread__config_default_null__then_noop) {
  drain_config_default(nullptr);
}

TEST(DrainThreadUnit, drain_thread__worker_null_argument__then_returns_null) {
  EXPECT_EQ(drain_thread_test_worker_entry(nullptr), nullptr);
}

TEST(DrainThreadUnit,
     drain_thread__update_config_with_null__then_invalid_argument) {
  RegistryHarness harness(2);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  EXPECT_EQ(drain_thread_update_config(drain, nullptr), -EINVAL);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__update_config_while_stopping__then_returns_busy) {
  HookScope guard;
  RegistryHarness harness(1);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  drain_thread_test_set_state(drain, DRAIN_STATE_STOPPING);
  DrainConfig config;
  drain_config_default(&config);
  EXPECT_EQ(drain_thread_update_config(drain, &config), -EBUSY);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__create_mutex_init_failure__then_returns_null) {
  HookScope guard;
  configure_mutex_init_failure(EBUSY);

  RegistryHarness harness(1);
  EXPECT_EQ(drain_thread_create(harness.registry, nullptr), nullptr);
}

TEST(DrainThreadUnit,
     drain_thread__create_allocation_failure__then_returns_null) {
  HookScope guard;
  configure_calloc_failure();

  RegistryHarness harness(1);
  EXPECT_EQ(drain_thread_create(harness.registry, nullptr), nullptr);
}

TEST(DrainThreadUnit, drain_thread__start_with_invalid_state__then_returns_einval) {
  HookScope guard;
  RegistryHarness harness(1);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  drain_thread_test_set_state(drain, DRAIN_STATE_UNINITIALIZED);
  EXPECT_EQ(drain_thread_start(drain), -EINVAL);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__start_while_stopping__then_returns_ealready) {
  HookScope guard;
  RegistryHarness harness(1);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  drain_thread_test_set_state(drain, DRAIN_STATE_STOPPING);
  EXPECT_EQ(drain_thread_start(drain), -EALREADY);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__start_thread_creation_failure__then_state_rolls_back) {
  HookScope guard;
  configure_pthread_create_failure(EAGAIN);

  RegistryHarness harness(2);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  EXPECT_EQ(drain_thread_start(drain), EAGAIN);
  EXPECT_EQ(drain_thread_get_state(drain), DRAIN_STATE_INITIALIZED);

  HookScope::reset();
  ASSERT_EQ(drain_thread_start(drain), 0);
  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__stop_after_external_stop__then_join_error_is_propagated) {
  HookScope guard;
  configure_pthread_join_failure(ESRCH);

  RegistryHarness harness(1);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  drain_thread_test_set_state(drain, DRAIN_STATE_STOPPED);
  drain_thread_test_set_thread_started(drain, true);
  drain_thread_test_set_worker(drain, pthread_self());

  EXPECT_EQ(drain_thread_stop(drain), ESRCH);
  EXPECT_EQ(drain_thread_get_state(drain), DRAIN_STATE_STOPPED);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__stop_stopped_thread_started__then_join_clears_flag) {
  HookScope guard;
  configure_pthread_join_override(0);

  RegistryHarness harness(1);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  drain_thread_test_set_state(drain, DRAIN_STATE_STOPPED);
  drain_thread_test_set_thread_started(drain, true);
  drain_thread_test_set_worker(drain, pthread_self());

  EXPECT_EQ(drain_thread_stop(drain), 0);
  EXPECT_EQ(pthread_join_call_count(), 1);

  EXPECT_EQ(drain_thread_stop(drain), 0);
  EXPECT_EQ(pthread_join_call_count(), 1);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__internal_return_ring_retry_paths__then_eventually_succeeds) {
  HookScope guard;
  configure_lane_return_failures(1001);

  RegistryHarness harness(2);
  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x9900);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  uint32_t ring = lane_get_free_ring(index_lane);
  ASSERT_NE(ring, UINT32_MAX);
  ASSERT_TRUE(lane_submit_ring(index_lane, ring));
  uint32_t taken = lane_take_ring(index_lane);
  ASSERT_EQ(taken, ring);

  drain_thread_test_return_ring(index_lane, ring);
  EXPECT_GE(lane_return_call_count(), 1002);

  drain_thread_test_return_ring(nullptr, ring);
  EXPECT_GE(lane_return_call_count(), 1002);

  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);
  bool hit_limit = true;
  EXPECT_EQ(drain_thread_test_drain_lane(drain, 0, nullptr, false, false, &hit_limit),
            0u);
  EXPECT_FALSE(hit_limit);

  drain_thread_destroy(drain);
}

TEST(DrainThreadUnit,
     drain_thread__return_ring_override_success__then_short_circuits) {
  HookScope guard;
  configure_lane_return_successes(1);

  RegistryHarness harness(2);
  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x4400);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  uint32_t ring = lane_get_free_ring(index_lane);
  ASSERT_NE(ring, UINT32_MAX);

  drain_thread_test_return_ring(index_lane, ring);
  EXPECT_EQ(lane_return_call_count(), 1);

  configure_lane_return_successes(0);
  EXPECT_TRUE(lane_return_ring(index_lane, ring));
}

TEST(DrainThreadUnit,
     drain_thread__drain_lane_and_cycle_null_inputs__then_return_zero_or_false) {
  HookScope guard;
  EXPECT_FALSE(drain_thread_test_cycle(nullptr, false));

  RegistryHarness harness_zero(0);
  DrainThread *drain_zero = create_drain(harness_zero, nullptr);
  ASSERT_NE(drain_zero, nullptr);

  drain_thread_test_set_registry(drain_zero, nullptr);
  EXPECT_FALSE(drain_thread_test_cycle(drain_zero, false));
  drain_thread_test_set_registry(drain_zero, harness_zero.registry);

  EXPECT_FALSE(drain_thread_test_cycle(drain_zero, false));

  drain_thread_destroy(drain_zero);

  drain_thread_destroy(nullptr);
}

TEST(DrainThreadUnit,
     drain_thread__test_helpers_null_inputs__then_noops) {
  HookScope guard;
  drain_thread_test_set_state(nullptr, DRAIN_STATE_RUNNING);
  drain_thread_test_set_thread_started(nullptr, true);
  drain_thread_test_set_worker(nullptr, pthread_self());
  drain_thread_test_set_rr_cursor(nullptr, 42);
  EXPECT_EQ(drain_thread_test_get_rr_cursor(nullptr), 0u);
  drain_thread_test_set_registry(nullptr, nullptr);
  drain_thread_test_return_ring(nullptr, 0);
}

TEST(DrainThreadUnit,
     drain_thread__drain_cycle_cursor_wraps__then_resets_to_zero) {
  HookScope guard;
  RegistryHarness harness(3);
  DrainThread *drain = create_drain(harness, nullptr);
  ASSERT_NE(drain, nullptr);

  const uint32_t capacity = thread_registry_get_capacity(harness.registry);
  ASSERT_GT(capacity, 0u);

  drain_thread_test_set_rr_cursor(drain, capacity + 5u);

  EXPECT_FALSE(drain_thread_test_cycle(drain, false));
  EXPECT_EQ(drain_thread_test_get_rr_cursor(drain), 1u % capacity);

  drain_thread_destroy(drain);
}
