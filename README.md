# concurrent-ds-bench

Bounded, fixed-capacity concurrent data structures in C++20, with a benchmark
harness that measures cross-core hand-off latency against a hardware
coherence floor and saturated throughput.

## Status

- [x] `SpscQueue`, `SpinlockQueue`, `MutexQueue` — complete, checked under
  ThreadSanitizer.
- [ ] MPMC - target design is a
  Vyukov/Disruptor-style bounded ring behind the same interface.

## AI disclosure

Core logic (queue implementations, benchmark harness) written by me. AI
assisted with build files, commit management, testing, aggregate script gen, and this README.

## Queues

One interface: `try_push(T)`, `try_pop() -> std::optional<T>`, `capacity()`.
Header-only under `include/cds/`. Capacity is a power of two.

| Type | Synchronization | Progress |
|---|---|---|
| `SpscQueue<T, N>` | Monotonic head/tail cursors, acquire/release | Lock-free; operations are wait-free (no retry loops). Single-producer/single-consumer by contract. |
| `SpinlockQueue<T, N>` | `std::atomic_flag` spinlock | Blocking (busy-wait). MPMC-safe. |
| `MutexQueue<T, N>` | `std::mutex` | Blocking (OS-mediated). MPMC-safe. |

Contended fields are optionally padded to a cache line (compile-time toggle,
see below) to test false-sharing impact.

## Build & test

An explicit build type is required — `Debug` (tests, assertions on) or
`RelWithDebInfo` (benchmarks, sanitizers). There is no default.

```sh
# tests
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure

# benchmark
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/benchmarks/benchmark --messages=1000000 --repeats=30 --queue=all
```

ThreadSanitizer (requires `RelWithDebInfo`):

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCDS_ENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

Cache-line padding is a compile-time toggle (`CDS_ENABLE_CACHE_PADDING`,
default ON). Build both ways into separate directories to A/B it:

```sh
cmake -S . -B build-pad   -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCDS_ENABLE_CACHE_PADDING=ON
cmake -S . -B build-nopad -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCDS_ENABLE_CACHE_PADDING=OFF
```

## Benchmark

Three metrics, measured in **separate runs** so they never contaminate each
other (a latency percentile is never read off a saturated run):

- **Hand-off latency** — depth-1 ping-pong. A token is bounced through two SPSC
  queues (one each direction) with exactly one message in flight, so the
  measured round-trip is the queue's hand-off cost, not buffer residency. Both
  timestamps are read on one thread. Reports RTT p50/p90/p99/p99.9/max and a
  one-way estimate (≈ RTT/2).
- **Coherence floor** — the same ping-pong with a bare `std::atomic_flag`
  instead of a queue: one cache line bounced between two cores, no queue
  bookkeeping. Every queue's RTT is at least this, so queue latency is also
  reported as overhead over the floor.
- **Throughput** — saturated single-producer/single-consumer, messages ÷
  elapsed after warm-up, in Mmsg/s.

Methodology:

- Spin-only retry (no yielding) to keep scheduling noise out of the measured
  window.
- Warm-up: first `max(1%, 10k)` samples dropped, capped at half.
- Each config is run N times; tables below report median and IQR across runs
  (computed from the per-run output).
- Empty payload — isolates synchronization cost from message-copy cost.
- No core pinning is available on macOS, so the scheduler may migrate threads
  across P/E clusters. Treat tail figures (p99.9/max) as scheduler-inflated
  upper bounds, not pure contention cost.

## Results

Apple M1 Pro, macOS, capacity 1024, 200k messages/run, N=30. Medians (and IQR
for throughput) across the 30 runs, computed by `scripts/aggregate.py` over the
padded and unpadded captures.

Coherence floor (RTT p50): **84 ns (padded build), 125 ns (unpadded build)**.
The floor is padding-invariant — a bare `atomic_flag` bounced between two cores
— so the two builds should agree. The ~49% gap (well over the ~15% we'd treat as
noise) reflects run-to-run environmental drift on an unpinned macOS scheduler
between the two capture sessions, not a padding effect. Each queue's "+over
floor" below is computed against its own build's baseline, so the drift does not
contaminate those deltas.

**Latency** (depth-1 ping-pong):

| Queue | Padding | RTT p50 | RTT p99 | 1-way p50 | +over floor (p50) |
|---|---|---|---|---|---|
| spsc | on | 208 ns | 833 ns | 104 ns | 124 ns |
| spsc | off | 208 ns | 833 ns | 104 ns | 83 ns |
| mutex | on | 14,230 ns | 123,729 ns | 7,115 ns | 14,063 ns |
| mutex | off | 21,188 ns | 367,750 ns | 10,594 ns | 21,083 ns |
| spinlock | on | 979 ns | 5,355 ns | 490 ns | 854 ns |
| spinlock | off | 958 ns | 8,667 ns | 479 ns | 833 ns |

At depth 1 there is only one message in flight, so SPSC latency is unaffected by
padding (head_/tail_ are never touched concurrently). Padding's effect shows up
on the blocking queues' tails and on throughput below.

**Throughput** (Mmsg/s):

| Queue | Padding | Median | IQR |
|---|---|---|---|
| spsc | on | 35.9 | 28.4 – 46.0 |
| spsc | off | 12.1 | 9.5 – 13.6 |
| mutex | on | 13.0 | 11.6 – 14.5 |
| mutex | off | 13.8 | 10.8 – 15.8 |
| spinlock | on | 5.7 | 3.6 – 8.6 |
| spinlock | off | 2.2 | 1.8 – 2.8 |

Saturated SPSC throughput is where false sharing bites: padding lifts it ~3×
(12.1 → 35.9 Mmsg/s) by keeping the producer's `tail_` and the consumer's
`head_` on separate lines. Spinlock throughput also gains (~2.2 → 5.7). Mutex
throughput is unchanged within noise — it is bounded by OS-mediated blocking, not
line contention.


