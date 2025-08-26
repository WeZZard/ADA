# Backlogs — M1 E1 I2 Thread Registry

## Sprint Overview
**Duration**: 3 days (24 hours)
**Priority**: P0 (Critical - foundation for all per-thread operations)
**Dependencies**: M1_E1_I1 (Shared Memory Setup)

## Day 1: Core Structures (8 hours)

### TR-001: Define ThreadRegistry structure (P0, 1h)
- [ ] Create ThreadRegistry with 64 slots
- [ ] Add atomic thread_count
- [ ] Add control flags (accepting_registrations, shutdown_requested)
- [ ] Define in tracer_types.h

### TR-002: Define ThreadLaneSet structure (P0, 2h)
- [ ] Thread identification fields (thread_id, slot_index)
- [ ] Active flag with atomic
- [ ] Embed Lane structures for index and detail
- [ ] Thread-local metrics fields
- [ ] Ensure cache-line alignment (64 bytes)

### TR-003: Define Lane structure (P0, 2h)
- [ ] Ring pool array pointer
- [ ] Active ring index (atomic)
- [ ] SPSC submit queue (head, tail, array)
- [ ] SPSC free queue (head, tail, array)
- [ ] Lane-specific state (marked flag for detail)
- [ ] Per-lane metrics (events_written, events_dropped)

### TR-004: Memory layout in shared memory (P0, 2h)
- [ ] Calculate total size requirements
- [ ] Allocate ThreadRegistry in shared memory segment
- [ ] Initialize all slots as inactive
- [ ] Set up memory barriers
- [ ] Verify alignment requirements

### TR-005: Thread-local storage setup (P0, 1h)
- [ ] Define __thread pointer for lane caching
- [ ] Create TLS initialization function
- [ ] Add cleanup handlers
- [ ] Test TLS on both macOS and Linux

## Day 2: Registration Logic (8 hours)

### TR-006: Implement atomic slot allocation (P0, 2h)
- [ ] Atomic fetch_add for thread_count
- [ ] Boundary check for MAX_THREADS
- [ ] Return slot index or error
- [ ] Memory ordering: memory_order_acq_rel

### TR-007: Thread registration function (P0, 3h)
- [ ] Check TLS for existing registration
- [ ] Allocate new slot if needed
- [ ] Initialize ThreadLaneSet fields
- [ ] Initialize both lanes (index and detail)
- [ ] Set active flag with release semantics
- [ ] Cache pointer in TLS

### TR-008: Lane initialization (P0, 2h)
- [ ] Allocate ring buffers (4 for index, 2 for detail)
- [ ] Initialize SPSC queues
- [ ] Set initial active ring to 0
- [ ] Place all other rings in free queue
- [ ] Zero metrics counters

### TR-009: Fast path optimization (P1, 1h)
- [ ] Inline TLS check for hot path
- [ ] Branch prediction hints
- [ ] Measure and validate < 10ns overhead
- [ ] Create benchmarks

## Day 3: SPSC Queues & Integration (8 hours)

### TR-010: SPSC submit queue operations (P0, 2h)
- [ ] Producer enqueue (thread side)
- [ ] Consumer dequeue (drain side)
- [ ] Memory ordering: release/acquire
- [ ] Wraparound handling with modulo

### TR-011: SPSC free queue operations (P0, 2h)
- [ ] Producer enqueue (drain side)
- [ ] Consumer dequeue (thread side)
- [ ] Memory ordering: release/acquire
- [ ] Queue full/empty detection

### TR-012: Thread discovery for drain (P0, 1h)
- [ ] Iterate active threads
- [ ] Check active flags with acquire
- [ ] Skip inactive slots
- [ ] Handle dynamic registration

### TR-013: Unit tests (P0, 2h)
- [ ] Single thread registration
- [ ] Concurrent registration
- [ ] MAX_THREADS boundary
- [ ] SPSC queue operations
- [ ] Memory ordering validation

### TR-014: Integration tests (P0, 1h)
- [ ] Multi-thread registration
- [ ] Drain thread discovery
- [ ] ThreadSanitizer validation
- [ ] Performance benchmarks

## Testing Tasks

### TR-015: Unit test suite (4h)
- [ ] test_thread_registry__single_registration__then_succeeds
- [ ] test_thread_registry__concurrent_registration__then_unique_slots
- [ ] test_thread_registry__max_threads_exceeded__then_returns_null
- [ ] test_spsc_queue__single_producer__then_maintains_order
- [ ] test_spsc_queue__wraparound__then_correct
- [ ] test_memory_ordering__registration__then_visible_to_drain
- [ ] test_isolation__no_false_sharing__then_independent_performance

### TR-016: Performance tests (2h)
- [ ] Registration latency (< 1μs)
- [ ] Fast path overhead (< 10ns)
- [ ] SPSC throughput (> 10M ops/sec)
- [ ] Scaling with thread count (linear)

### TR-017: Stress tests (2h)
- [ ] 64 threads concurrent registration
- [ ] Rapid registration/deregistration
- [ ] Memory pressure scenarios
- [ ] Cache effects measurement

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| False sharing between threads | High | Medium | Cache-line alignment, padding |
| ABA problem in SPSC queues | High | Low | Monotonic counters, careful ordering |
| Thread limit too low | Medium | Low | Make MAX_THREADS configurable |
| TLS not portable | Medium | Low | Fallback to manual lookup |
| Memory ordering bugs | High | Medium | ThreadSanitizer, careful review |

## Success Metrics

- [ ] All 64 threads can register successfully
- [ ] Zero contention after registration
- [ ] Registration time < 1μs
- [ ] Fast path < 10ns
- [ ] SPSC queues achieve 10M+ ops/sec
- [ ] ThreadSanitizer reports no races
- [ ] No false sharing detected
- [ ] Linear scaling with thread count

## Definition of Done

- [ ] All code implemented and reviewed
- [ ] All unit tests passing
- [ ] Integration tests passing
- [ ] Performance targets met
- [ ] ThreadSanitizer clean
- [ ] Documentation updated
- [ ] Code coverage ≥ 100% on changed lines
- [ ] Approved by technical lead

## Notes

- This is THE critical iteration that fixes the shared ring buffer problem
- Must ensure true SPSC semantics per thread
- Cache-line alignment crucial for performance
- Memory ordering must be explicitly correct
- Foundation for all subsequent per-thread operations

## Dependencies

### Depends On:
- M1_E1_I1: Shared Memory Setup (complete)

### Depended By:
- M1_E1_I3: Ring Buffer Core (needs lane structure)
- M1_E1_I5: Thread Registration (needs registry)
- M1_E1_I6: Ring Pool Swap (needs lanes)
- M1_E2_I1: Drain Thread (needs registry to iterate)
- M1_E2_I2: Per-Thread Drain (needs thread lanes)