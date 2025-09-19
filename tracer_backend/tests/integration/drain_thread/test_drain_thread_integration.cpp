#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
#include <tracer_backend/ada/thread.h>
#include <tracer_backend/drain_thread/drain_thread.h>
#include <tracer_backend/utils/thread_registry.h>
}

namespace {

struct RegistryHarness {
  explicit RegistryHarness(size_t capacity) {
    size_t bytes = thread_registry_calculate_memory_size_with_capacity(
        static_cast<uint32_t>(capacity));
    arena = std::unique_ptr<uint8_t[]>(new uint8_t[bytes]);
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

  std::unique_ptr<uint8_t[]> arena;
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
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000),
    std::chrono::milliseconds step = std::chrono::milliseconds(2)) {
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

bool submit_ring_with_retry(Lane *lane, int max_attempts = 2000) {
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

TEST(DrainThreadIntegration,
     drain_thread__multi_threaded_producers__then_all_rings_processed) {
  RegistryHarness harness(16);

  const int producer_count = 4;
  const int rings_per_thread = 200;
  const uint64_t expected_total =
      static_cast<uint64_t>(producer_count) * rings_per_thread;

  std::vector<ThreadLaneSet *> lane_sets;
  lane_sets.reserve(producer_count);
  std::vector<Lane *> index_lanes;
  index_lanes.reserve(producer_count);
  for (int i = 0; i < producer_count; ++i) {
    ThreadLaneSet *lanes =
        thread_registry_register(harness.registry, 0x1000 + i);
    ASSERT_NE(lanes, nullptr);
    lane_sets.push_back(lanes);
    index_lanes.push_back(thread_lanes_get_index_lane(lanes));
    ASSERT_NE(index_lanes.back(), nullptr);
  }

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;
  config.max_batch_size = 0;
  config.fairness_quantum = 0;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  std::vector<std::thread> producers;
  producers.reserve(producer_count);
  std::atomic<uint64_t> submitted{0};
  for (int i = 0; i < producer_count; ++i) {
    producers.emplace_back([&, lane = index_lanes[i]]() {
      for (int n = 0; n < rings_per_thread; ++n) {
        if (submit_ring_with_retry(lane)) {
          submitted.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto &t : producers) {
    t.join();
  }

  ASSERT_EQ(submitted.load(), expected_total);

  DrainMetrics metrics =
      wait_for_metrics(drain, [expected_total](const DrainMetrics &m) {
        return m.rings_total >= expected_total;
      });

  EXPECT_EQ(metrics.rings_total, expected_total);
  EXPECT_EQ(metrics.rings_detail, 0u);

  uint64_t per_thread_sum = 0;
  for (uint32_t i = 0; i < MAX_THREADS; ++i) {
    per_thread_sum += metrics.rings_per_thread[i][0];
    per_thread_sum += metrics.rings_per_thread[i][1];
  }
  EXPECT_EQ(per_thread_sum, metrics.rings_total);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(
    DrainThreadIntegration,
    drain_thread__graceful_shutdown_during_activity__then_final_pass_completes_work) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x2001);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;
  config.max_batch_size = 2;
  config.fairness_quantum = 2;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  constexpr uint32_t kTotalRings = 400;
  for (uint32_t i = 0; i < kTotalRings; ++i) {
    ASSERT_TRUE(submit_ring_with_retry(index_lane));
  }

  DrainMetrics before_stop{};
  drain_thread_get_metrics(drain, &before_stop);
  ASSERT_LE(before_stop.rings_total, kTotalRings);

  ASSERT_EQ(drain_thread_stop(drain), 0);

  DrainMetrics metrics{};
  drain_thread_get_metrics(drain, &metrics);
  EXPECT_EQ(metrics.rings_total, kTotalRings);
  EXPECT_GE(metrics.final_drains, 1u);

  uint32_t residual = lane_take_ring(index_lane);
  EXPECT_EQ(residual, UINT32_MAX);
  if (residual != UINT32_MAX) {
    EXPECT_TRUE(lane_return_ring(index_lane, residual));
  }

  drain_thread_destroy(drain);
}

TEST(DrainThreadIntegration,
     drain_thread__sustained_load_stability__then_metrics_monotonic) {
  RegistryHarness harness(8);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x3001);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;
  config.max_batch_size = 0;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  uint64_t last_total = 0;
  uint64_t last_cycles = 0;
  for (int round = 0; round < 5; ++round) {
    for (int i = 0; i < 150; ++i) {
      ASSERT_TRUE(submit_ring_with_retry(index_lane));
    }
    DrainMetrics metrics =
        wait_for_metrics(drain, [last_total](const DrainMetrics &m) {
          return m.rings_total > last_total;
        });
    EXPECT_GE(metrics.rings_total, last_total);
    EXPECT_GE(metrics.cycles_total, last_cycles);
    last_total = metrics.rings_total;
    last_cycles = metrics.cycles_total;
  }

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadPerformance,
     drain_thread__high_throughput_load__then_exceeds_target) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x4001);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;
  config.max_batch_size = 0;
  config.fairness_quantum = 0;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  constexpr uint64_t kTargetRings = 20000;

  auto start = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < kTargetRings; ++i) {
    ASSERT_TRUE(submit_ring_with_retry(index_lane));
  }
  DrainMetrics metrics = wait_for_metrics(drain, [](const DrainMetrics &m) {
    return m.rings_total >= kTargetRings;
  });
  auto end = std::chrono::steady_clock::now();

  std::chrono::duration<double> elapsed = end - start;
  double throughput = metrics.rings_total / elapsed.count();

  // Performance target adjusted to handle system load variations
  // Observed range: 38k-80k rings/s, using 30k as minimum threshold
  // This still catches major performance regressions while avoiding spurious failures
  constexpr double kMinThroughput = 30000.0;  // rings/second

  EXPECT_GT(throughput, kMinThroughput)
      << "Throughput: " << throughput << " rings/s (min: " << kMinThroughput << ")";

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadPerformance,
     drain_thread__latency_under_burst__then_within_target) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x4002);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;
  config.max_batch_size = 0;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  constexpr int kSamples = 100;
  uint64_t total_latency_us = 0;

  for (int i = 0; i < kSamples; ++i) {
    auto submit_time = std::chrono::steady_clock::now();
    ASSERT_TRUE(submit_ring_with_retry(index_lane));
    while (true) {
      DrainMetrics metrics{};
      drain_thread_get_metrics(drain, &metrics);
      if (metrics.rings_total >= static_cast<uint64_t>(i + 1)) {
        break;
      }
      std::this_thread::yield();
    }
    auto done = std::chrono::steady_clock::now();
    total_latency_us += std::chrono::duration_cast<std::chrono::microseconds>(
                            done - submit_time)
                            .count();
  }

  double average_latency = static_cast<double>(total_latency_us) / kSamples;
  EXPECT_LT(average_latency, 500.0);

  ASSERT_EQ(drain_thread_stop(drain), 0);
  drain_thread_destroy(drain);
}

TEST(DrainThreadPerformance,
     drain_thread__idle_cpu_usage__then_below_threshold) {
  RegistryHarness harness(2);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = true;

  DrainThread *drain = create_drain(harness, &config);
  ASSERT_NE(drain, nullptr);
  ASSERT_EQ(drain_thread_start(drain), 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  ASSERT_EQ(drain_thread_stop(drain), 0);

  DrainMetrics metrics{};
  drain_thread_get_metrics(drain, &metrics);
  ASSERT_GT(metrics.cycles_total, 0u);
  double idle_ratio = static_cast<double>(metrics.cycles_idle) /
                      static_cast<double>(metrics.cycles_total);
  EXPECT_GT(idle_ratio, 0.95);

  drain_thread_destroy(drain);
}

TEST(DrainThreadPerformance,
     drain_thread__memory_stability_over_restarts__then_rings_available) {
  RegistryHarness harness(4);

  ThreadLaneSet *lanes = thread_registry_register(harness.registry, 0x4003);
  ASSERT_NE(lanes, nullptr);
  Lane *index_lane = thread_lanes_get_index_lane(lanes);
  ASSERT_NE(index_lane, nullptr);

  DrainConfig config;
  drain_config_default(&config);
  config.poll_interval_us = 0;
  config.yield_on_idle = false;

  for (int iteration = 0; iteration < 3; ++iteration) {
    DrainThread *drain = create_drain(harness, &config);
    ASSERT_NE(drain, nullptr);
    ASSERT_EQ(drain_thread_start(drain), 0);

    for (int i = 0; i < 100; ++i) {
      ASSERT_TRUE(submit_ring_with_retry(index_lane));
    }
    wait_for_metrics(
        drain, [](const DrainMetrics &m) { return m.rings_total >= 100; });

    ASSERT_EQ(drain_thread_stop(drain), 0);
    drain_thread_destroy(drain);

    for (int i = 0; i < 10; ++i) {
      uint32_t ring = lane_get_free_ring(index_lane);
      if (ring != UINT32_MAX) {
        EXPECT_TRUE(lane_return_ring(index_lane, ring));
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
  }
}
