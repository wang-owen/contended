# concurrent-ds-bench

Bounded, fixed-capacity concurrent data structures in C++20, with a benchmark
harness that measures cross-core hand-off latency against a hardware
coherence floor and saturated throughput.

## Queues


| Type | Synchronization | Progress |
|---|---|---|
| `SpscQueue<T, N>` | Monotonic head/tail cursors, acquire/release | Lock-free; operations are wait-free (no retry loops). Single-producer/single-consumer by contract. |
| `SpinlockQueue<T, N>` | `std::atomic_flag` spinlock | Blocking (busy-wait). MPMC-safe. |
| `MutexQueue<T, N>` | `std::mutex` | Blocking (OS-mediated). MPMC-safe. |
| `MpmcQueue<T, N>` | Per-slot sequence numbers (Vyukov ring), CAS on the shared position counters | Lock-free. MPMC-safe, any number of producers/consumers. |

Contended fields are optionally padded to a cache line (compile-time toggle,
see below) to test false-sharing impact.

## Stacks


| Type | Reclamation | Progress |
|---|---|---|
| `LockStack<T>` | `std::mutex` around a `std::stack` | Blocking (OS-mediated). |
| `HazardLockFreeStack<T, N>` | Hazard pointers (`N` slots, one per live thread) | Lock-free. |
| `QuiescentLockFreeStack<T>` | Quiescent-state — nodes freed once no thread is mid-`pop()` | Lock-free. |
| `SplitRefLockFreeStack<T>` | Split reference counting (external + internal counts in one CAS) | Lock-free. |

## Build & test

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

Three metrics, measured in separate runs so they never contaminate each other:

- **Hand-off latency** — depth-1 ping-pong through two SPSC queues (one
  message in flight). Reports RTT p50/p90/p99/p99.9/max and a one-way
  estimate (≈ RTT/2).
- **Coherence floor** — the same ping-pong with a bare `std::atomic_flag`
  instead of a queue, as a lower bound; queue latency is also reported as
  overhead over this floor.
- **Throughput** — saturated single-producer/single-consumer, Mmsg/s after
  warm-up.

Two more target the new structures: **contended MPMC throughput** (4
producers / 4 consumers on one queue instance — `MpmcQueue`, `SpinlockQueue`,
`MutexQueue`; `SpscQueue` doesn't support this) and **stack throughput** (N
threads each push then pop their share, at N=2 and N=8, across all four
stacks). Both are timed launch-to-join rather than a precise warm-up
cutpoint — at 200k+ messages, thread-launch skew is noise.

Methodology: spin-only retry (no yielding); warm-up drops the first
`max(1%, 10k)` samples; each config runs N times, tables report median (IQR
for throughput); empty payload isolates synchronization cost from
message-copy cost; no core pinning on macOS, so tail figures (p99.9/max) are
scheduler-inflated upper bounds, not pure contention cost.

## Results

Apple M1 Pro (8P+2E cores), macOS, capacity 1024, 200k messages/run, N=10.
Medians (IQR for throughput) via `scripts/aggregate.py`.

Coherence floor (RTT p50): **125 ns (padded), 84 ns (unpadded)**. This should
be padding-invariant but isn't — a ≈49% gap, in the opposite direction from
an earlier capture that also showed ≈49% — so it's scheduler noise on an
unpinned macOS scheduler, not a padding effect. Each queue's "+over floor" is
computed against its own build's baseline.

**Latency** (depth-1 ping-pong):

| Queue | Padding | RTT p50 | RTT p99 | 1-way p50 | +over floor (p50) |
|---|---|---|---|---|---|
| spsc | on | 208 ns | 792 ns | 104 ns | 83 ns |
| spsc | off | 208 ns | 730 ns | 104 ns | 104 ns |
| mpmc | on | 167 ns | 583 ns | 84 ns | 42 ns |
| mpmc | off | 209 ns | 875 ns | 104 ns | 125 ns |
| spinlock | on | 875 ns | 13,709 ns | 438 ns | 750 ns |
| spinlock | off | 875 ns | 57,396 ns | 438 ns | 771 ns |
| mutex | on | 16,375 ns | 153,521 ns | 8,188 ns | 16,250 ns |
| mutex | off | 13,084 ns | 75,937 ns | 6,542 ns | 12,979 ns |

Padding barely matters at depth 1 — only one message is ever in flight, so
contended fields are never actually raced. `mpmc` lands right next to `spsc`
despite being fully MPMC-safe: one CAS per op is about as cheap as `spsc`'s
plain load/fetch-add here.

**Throughput** (Mmsg/s, single producer/single consumer):

| Queue | Padding | Median | IQR |
|---|---|---|---|
| spsc | on | 48.0 | 47.2 – 51.5 |
| spsc | off | 17.5 | 16.5 – 21.5 |
| mpmc | on | 101.1 | 68.6 – 124.0 |
| mpmc | off | 74.2 | 54.4 – 78.3 |
| spinlock | on | 5.9 | 5.6 – 6.3 |
| spinlock | off | 1.3 | 1.1 – 1.3 |
| mutex | on | 19.1 | 17.0 – 19.6 |
| mutex | off | 17.8 | 16.3 – 18.3 |

Padding still matters for the two spin-based queues (`spsc` ≈2.7×,
`spinlock` ≈4.5×); `mutex` is flat, since it's blocking-bound, not
line-contention-bound. `mpmc`'s median beats `spsc`'s, but its per-run spread
(45–125) is much wider than `spsc`'s tight 40–55 band — likely P/E core
migration (no pinning on macOS) — so treat it as an optimistic case, not a
stable number.

**Contended MPMC** (4 producers / 4 consumers, one queue, padded build):

| Queue | Median | IQR |
|---|---|---|
| mpmc | 4.4 | 4.1 – 4.9 |
| spinlock | 0.3 | 0.2 – 0.4 |
| mutex | 13.6 | 12.9 – 15.2 |

`spinlock`'s no-backoff retry loop collapses under real contention (≈20×
worse than 1p/1c). More interesting: `mutex` beats `mpmc` by ≈3× here,
inverting their 1p/1c ranking — `mpmc`'s CAS loop on
`enqueue_pos_`/`dequeue_pos_` has the same no-backoff problem once 8 threads
contend on it, while a blocking mutex parks waiting threads instead of
burning cycles. Lock-free isn't automatically faster; it depends on
contention level and whether the retry loop backs off.

**Stack throughput** (N threads push then pop their share, padded build):

| Stack | 2 threads | 8 threads |
|---|---|---|
| `LockStack` | 34.1 | 21.8 |
| `SplitRefLockFreeStack` | 7.2 | 1.5 |
| `QuiescentLockFreeStack` | 7.2 | 1.5 |
| `HazardLockFreeStack` | 6.7 | 1.8 |

`LockStack` wins outright (≈5× at 2 threads, ≈12–15× at 8) despite one global
mutex — but not because of the lock. All three lock-free stacks `new`/
`delete` a node per operation, while `LockStack`'s `std::deque` amortizes
allocation; this benchmark mostly measures allocator contention. The
lock-free stacks land within noise of each other, consistent with that
shared cost swamping whatever they do differently (hazard pointers vs.
quiescent-state vs. split refcounting).


## AI disclosure

Core data structure logic written by me.
AI assisted with build files, commit management, testing, aggregate script gen, and this README.
