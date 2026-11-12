# contended

Bounded, fixed-capacity concurrent data structures in C++20, with a benchmark
harness that measures cross-core hand-off latency against a hardware
coherence floor and saturated throughput.

## Queues

| Type | Synchronization | Progress |
|---|---|---|
| `SpscQueue<T, N>` | Monotonic head/tail cursors, acquire/release | Wait-free. SPSC only. |
| `SpinlockQueue<T, N>` | `std::atomic_flag` spinlock | Blocking (busy-wait). MPMC. |
| `MutexQueue<T, N>` | `std::mutex` | Blocking (OS-mediated). MPMC. |
| `MpmcQueue<T, N>` | Per-slot sequence numbers (Vyukov ring), CAS on shared positions | Lock-free. MPMC. |

## Stacks

| Type | Reclamation | Progress |
|---|---|---|
| `LockStack<T>` | `std::mutex` around a `std::stack` | Blocking (OS-mediated). |
| `HazardLockFreeStack<T, N>` | Hazard pointers (`N` slots, one per live thread) | Lock-free. |
| `QuiescentLockFreeStack<T>` | Quiescent-state: nodes freed once no thread is mid-`pop()` | Lock-free. |
| `SplitRefLockFreeStack<T>` | Split reference counting (external + internal counts in one CAS) | Lock-free. |

## Build & test

Requires [Meson](https://mesonbuild.com) >= 1.1 and Ninja. Use `debug` for
tests and `debugoptimized` for benchmarks and sanitizers.

```sh
# tests
meson setup build-debug --buildtype=debug
meson test -C build-debug --print-errorlogs

# benchmark
meson setup build --buildtype=debugoptimized
meson compile -C build
./build/benchmarks/benchmark --messages=1000000 --repeats=30 --queue=all

# sanitizers: -Db_sanitize=thread or -Db_sanitize=address,undefined
meson setup build-tsan --buildtype=debugoptimized -Db_sanitize=thread
meson test -C build-tsan --print-errorlogs
```

Contended fields are padded to their own cache line by default. Pass
`-Dcache_padding=false` to a separate build directory to measure false sharing.

## Benchmark

Each metric is measured in a separate run:

- **Coherence floor**: depth-1 ping-pong on a bare `std::atomic_flag`, as a
  lower bound for cross-core hand-off.
- **Hand-off latency**: the same ping-pong through two queues. Reports RTT
  percentiles, one-way (~RTT/2), and overhead over the floor.
- **Throughput**: saturated 1 producer / 1 consumer, after warm-up.
- **Contended MPMC**: 4 producers / 4 consumers on one `MpmcQueue`,
  `SpinlockQueue` or `MutexQueue`.
- **Stack throughput**: N threads each push then pop their share, N = 2 and 8.

The last two are timed launch-to-join; at 200k+ messages, thread-launch skew is
noise.

Retries spin without yielding. Warm-up drops the first `max(1%, 10k)` samples.
The payload is empty, so results measure synchronization, not copying. macOS
allows no core pinning, so tail figures (p99.9/max) include scheduler noise.

## Results

Apple M1 Pro (8P+2E), macOS, capacity 1024, 200k messages per run, 10 runs per
config. Medians (and IQR for throughput) from `scripts/aggregate.py`.

The coherence floor (RTT p50) measured 125 ns padded and 84 ns unpadded. It
should not depend on padding, and an earlier capture showed the opposite gap,
so this is scheduler noise. Each queue's overhead is computed against its own
build's floor.

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

Padding barely matters at depth 1, since with one message in flight the
contended fields are never raced. `mpmc` matches `spsc`: one CAS per operation
costs about as much as a plain load and fetch-add here.

**Throughput** (Mmsg/s, 1 producer / 1 consumer):

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

Padding helps the spin-based queues (`spsc` ~2.7×, `spinlock` ~4.5×). `mutex`
is unaffected because it is bound by blocking, not cache-line contention.
`mpmc`'s median beats `spsc`'s, but its spread is far wider, likely from P/E
core migration, so treat it as optimistic.

**Contended MPMC** (Mmsg/s, 4 producers / 4 consumers, padded):

| Queue | Median | IQR |
|---|---|---|
| mpmc | 4.4 | 4.1 – 4.9 |
| spinlock | 0.3 | 0.2 – 0.4 |
| mutex | 13.6 | 12.9 – 15.2 |

`spinlock` collapses under contention (~20× worse than 1p/1c). `mutex` beats
`mpmc` by ~3×, reversing the 1p/1c ranking: `mpmc`'s CAS loop on the shared
positions has no backoff, so 8 threads burn cycles retrying, while a mutex parks
waiters. Lock-free is not automatically faster.

**Stack throughput** (Mmsg/s, padded):

| Stack | 2 threads | 8 threads |
|---|---|---|
| `LockStack` | 34.1 | 21.8 |
| `SplitRefLockFreeStack` | 7.2 | 1.5 |
| `QuiescentLockFreeStack` | 7.2 | 1.5 |
| `HazardLockFreeStack` | 6.7 | 1.8 |

`LockStack` wins (~5× at 2 threads, ~12–15× at 8), but not because of the
lock. The lock-free stacks allocate and free a node per operation, while
`std::stack`'s underlying `std::deque` amortizes allocation, so this mostly
measures allocator contention. That shared cost also explains why the three
lock-free stacks land within noise of each other.
