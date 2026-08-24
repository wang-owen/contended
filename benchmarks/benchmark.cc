#include "cds/hazard_lockfree_stack.h"
#include "cds/lock_stack.h"
#include "cds/mpmc_queue.h"
#include "cds/mutex_queue.h"
#include "cds/quiescent_lockfree_stack.h"
#include "cds/spinlock_queue.h"
#include "cds/split_ref_lockfree_stack.h"
#include "cds/spsc_queue.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

namespace {

#ifdef ENABLE_PADDING
inline constexpr bool kPaddingEnabled = true;
#else
inline constexpr bool kPaddingEnabled = false;
#endif

constexpr std::size_t kQueueCapacity = 1024;

struct Payload {
  std::uint64_t seq;
};

struct Stats {
  double p50_ns, p90_ns, p99_ns, p999_ns, max_ns;
};

bool consume_flag(std::string_view arg, std::string_view name,
                  std::string_view &out) {
  if (arg.substr(0, name.size()) == name) {
    out = arg.substr(name.size());
    return true;
  }
  return false;
}

// Percentiles over durations[warmup_count:]
Stats compute_percentiles(std::vector<std::int64_t> &durations,
                          std::size_t warmup_count) {
  const auto begin = durations.begin() + warmup_count;
  std::sort(begin, durations.end());
  const std::size_t count = durations.size() - warmup_count;

  auto percentile = [&](double p) {
    std::size_t idx = static_cast<std::size_t>(p * (count - 1));
    return static_cast<double>(*(begin + idx));
  };

  Stats stats{};
  stats.p50_ns = percentile(0.50);
  stats.p90_ns = percentile(0.90);
  stats.p99_ns = percentile(0.99);
  stats.p999_ns = percentile(0.999);
  stats.max_ns = static_cast<double>(durations.back());
  return stats;
}

std::size_t warmup_count_for(std::size_t n) {
  // Drop max(1%, 10k) samples as warm-up
  return std::min(n / 2, std::max(n / 100, std::size_t{10000}));
}

std::int64_t elapsed_ns(Clock::time_point t0, Clock::time_point t1) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

void report(const char *label, std::size_t run_index, const Stats &s,
            std::optional<double> floor_p50_ns = std::nullopt) {
  std::printf("[%-8s] pad=%d run=%zu RTT p50=%.0fns p90=%.0fns p99=%.0fns "
              "p99.9=%.0fns max=%.0fns | 1-way p50~=%.0fns",
              label, kPaddingEnabled ? 1 : 0, run_index, s.p50_ns, s.p90_ns,
              s.p99_ns, s.p999_ns, s.max_ns, s.p50_ns / 2.0);
  if (floor_p50_ns) {
    std::printf(" | +%.0fns over floor", s.p50_ns - *floor_p50_ns);
  }
  std::printf("\n");
}

Stats run_latency_baseline(std::size_t message_count, std::size_t run_index) {
  std::atomic_flag flag{};
  std::vector<std::int64_t> rtt;
  rtt.reserve(message_count);

  std::thread worker([&] {
    for (std::size_t i = 0; i < message_count; ++i) {
      while (!flag.test(std::memory_order_acquire)) { /* spin */
      }
      flag.clear(std::memory_order_release);
    }
  });

  for (std::size_t i = 0; i < message_count; ++i) {
    auto t0 = Clock::now();
    flag.test_and_set(std::memory_order_release);
    while (flag.test(std::memory_order_acquire)) { /* spin */
    }
    auto t1 = Clock::now();
    rtt.push_back(elapsed_ns(t0, t1));
  }

  worker.join();

  Stats stats = compute_percentiles(rtt, warmup_count_for(rtt.size()));
  report("baseline", run_index, stats);
  return stats;
}

template <typename Queue>
void run_throughput_bench(const char *label, std::size_t message_count,
                          std::size_t run_index) {
  Queue q;
  const std::size_t warmup = warmup_count_for(message_count);
  Clock::time_point start{}, end{};

  std::thread producer([&] {
    for (std::size_t i = 0; i < message_count; ++i)
      while (!q.try_push(Payload{i})) { /* spin */
      }
  });

  for (std::size_t i = 0; i < message_count; ++i) {
    while (!q.try_pop()) { /* spin */
    }
    if (i == warmup)
      start = Clock::now();
  }
  end = Clock::now();
  producer.join();

  const double secs = std::chrono::duration<double>(end - start).count();
  if (message_count <= warmup + 1 || secs <= 0.0) {
    std::printf("[%-8s] pad=%d run=%zu throughput=n/a (too few messages)\n",
                label, kPaddingEnabled ? 1 : 0, run_index);
    return;
  }
  const double msgs = static_cast<double>(message_count - warmup - 1);
  std::printf("[%-8s] pad=%d run=%zu throughput=%.2f Mmsg/s\n", label,
              kPaddingEnabled ? 1 : 0, run_index, msgs / secs / 1e6);
}

template <typename Queue>
void run_latency_bench(const char *label, std::size_t message_count,
                       std::size_t run_index, double floor_p50_ns) {
  Queue to_worker, to_main;
  std::vector<std::int64_t> rtt;
  rtt.reserve(message_count);

  std::thread worker([&] {
    for (std::size_t i = 0; i < message_count; ++i) {
      std::optional<Payload> recv;
      while (!(recv = to_worker.try_pop())) { /* spin */
      }
      assert(recv->seq == i);
      while (!to_main.try_push(Payload{recv->seq})) { /* spin */
      }
    }
  });

  for (std::size_t i = 0; i < message_count; ++i) {
    std::optional<Payload> recv;
    auto t0 = Clock::now();
    while (!to_worker.try_push(Payload{i})) { /* spin */
    }
    while (!(recv = to_main.try_pop())) { /* spin */
    }
    auto t1 = Clock::now();
    assert(recv->seq == i);
    rtt.push_back(elapsed_ns(t0, t1));
  }

  worker.join();

  Stats stats = compute_percentiles(rtt, warmup_count_for(rtt.size()));
  report(label, run_index, stats, floor_p50_ns);
}

// Sustained throughput with multiple producers and consumers hammering one
// queue instance. Unlike run_throughput_bench (one producer, one consumer),
// this actually exercises MPMC contention. Times launch-to-join rather than
// picking a precise warm-up cutpoint: with message_count in the hundreds of
// thousands, thread-launch skew is noise.
template <typename Queue>
void run_contended_throughput_bench(const char *label,
                                    std::size_t message_count,
                                    std::size_t producers,
                                    std::size_t consumers,
                                    std::size_t run_index) {
  Queue q;
  std::atomic<std::size_t> produced{0};
  std::atomic<std::size_t> consumed{0};

  std::vector<std::thread> pool;
  pool.reserve(producers + consumers);

  const auto start = Clock::now();
  for (std::size_t p = 0; p < producers; ++p) {
    pool.emplace_back([&] {
      while (true) {
        std::size_t i = produced.fetch_add(1, std::memory_order_relaxed);
        if (i >= message_count)
          return;
        while (!q.try_push(Payload{i})) { /* spin */
        }
      }
    });
  }
  for (std::size_t c = 0; c < consumers; ++c) {
    pool.emplace_back([&] {
      while (true) {
        std::size_t i = consumed.fetch_add(1, std::memory_order_relaxed);
        if (i >= message_count)
          return;
        while (!q.try_pop()) { /* spin */
        }
      }
    });
  }
  for (auto &t : pool)
    t.join();
  const auto end = Clock::now();

  const double secs = std::chrono::duration<double>(end - start).count();
  const double msgs = static_cast<double>(message_count);
  std::printf("[%-8s] pad=%d run=%zu throughput=%.2f Mmsg/s\n", label,
              kPaddingEnabled ? 1 : 0, run_index, msgs / secs / 1e6);
}

// Adapts LockStack's try_pop(T&) -> bool to the other stacks' pop() ->
// unique_ptr<T>, so run_stack_throughput_bench can drive either shape.
template <typename Stack>
bool stack_try_pop(Stack &s, Payload &out) {
  if constexpr (requires { s.try_pop(out); }) {
    return s.try_pop(out);
  } else {
    auto p = s.pop();
    if (!p)
      return false;
    out = *p;
    return true;
  }
}

// Each of thread_count threads pushes its share of message_count values then
// pops the same number back off (spinning on empty), mirroring the balanced
// push/pop pattern in tests/split_ref_lockfree_queue_test.cc. Times
// launch-to-join.
template <typename Stack>
void run_stack_throughput_bench(const char *label, std::size_t message_count,
                                std::size_t thread_count,
                                std::size_t run_index) {
  Stack s;
  const std::size_t per_thread = message_count / thread_count;

  std::vector<std::thread> pool;
  pool.reserve(thread_count);

  const auto start = Clock::now();
  for (std::size_t t = 0; t < thread_count; ++t) {
    pool.emplace_back([&] {
      for (std::size_t i = 0; i < per_thread; ++i)
        s.push(Payload{i});
      Payload out;
      for (std::size_t i = 0; i < per_thread; ++i)
        while (!stack_try_pop(s, out)) { /* spin */
        }
    });
  }
  for (auto &t : pool)
    t.join();
  const auto end = Clock::now();

  const double secs = std::chrono::duration<double>(end - start).count();
  const double msgs = static_cast<double>(per_thread * thread_count);
  std::printf("[%-8s] pad=%d run=%zu throughput=%.2f Mmsg/s\n", label,
              kPaddingEnabled ? 1 : 0, run_index, msgs / secs / 1e6);
}

struct Bench {
  std::string_view name;
  void (*run_latency)(std::size_t message_count, std::size_t run_index,
                      double floor_p50_ns);
  void (*run_throughput)(std::size_t message_count, std::size_t run_index);
};

const Bench benches[] = {
    {"spsc",
     [](std::size_t n, std::size_t r, double f) {
       run_latency_bench<cds::SpscQueue<Payload, kQueueCapacity>>("spsc_lat", n,
                                                                  r, f);
     },
     [](std::size_t n, std::size_t r) {
       run_throughput_bench<cds::SpscQueue<Payload, kQueueCapacity>>("spsc_tp",
                                                                     n, r);
     }},
    {"mutex",
     [](std::size_t n, std::size_t r, double f) {
       run_latency_bench<cds::MutexQueue<Payload, kQueueCapacity>>("mutex_lat",
                                                                   n, r, f);
     },
     [](std::size_t n, std::size_t r) {
       run_throughput_bench<cds::MutexQueue<Payload, kQueueCapacity>>(
           "mutex_tp", n, r);
     }},
    {"spinlock",
     [](std::size_t n, std::size_t r, double f) {
       run_latency_bench<cds::SpinlockQueue<Payload, kQueueCapacity>>(
           "spinlock_lat", n, r, f);
     },
     [](std::size_t n, std::size_t r) {
       run_throughput_bench<cds::SpinlockQueue<Payload, kQueueCapacity>>(
           "spinlock_tp", n, r);
     }},
    {"mpmc",
     [](std::size_t n, std::size_t r, double f) {
       run_latency_bench<cds::MpmcQueue<Payload, kQueueCapacity>>("mpmc_lat",
                                                                  n, r, f);
     },
     [](std::size_t n, std::size_t r) {
       run_throughput_bench<cds::MpmcQueue<Payload, kQueueCapacity>>("mpmc_tp",
                                                                     n, r);
     }},
};

} // namespace

int main(int argc, char *argv[]) {
  std::size_t message_count = 1'000'000;
  std::size_t repeats = 1;
  std::string queue_filter = "all";

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    std::string_view value;
    if (consume_flag(arg, "--messages=", value)) {
      message_count = std::stoull(std::string(value));
    } else if (consume_flag(arg, "--repeats=", value)) {
      repeats = std::stoull(std::string(value));
    } else if (consume_flag(arg, "--queue=", value)) {
      queue_filter = std::string(value);
    }
  }

  if (message_count < 2) {
    std::fprintf(stderr, "error: --messages must be >= 2\n");
    return 1;
  }

  auto selected = [&queue_filter](std::string_view name) {
    return queue_filter == "all" || name == queue_filter;
  };

  for (std::size_t run_index = 0; run_index < repeats; ++run_index) {
    const double floor_p50 =
        run_latency_baseline(message_count, run_index).p50_ns;
    for (const Bench &b : benches)
      if (selected(b.name))
        b.run_latency(message_count, run_index, floor_p50);
  }

  for (std::size_t run_index = 0; run_index < repeats; ++run_index)
    for (const Bench &b : benches)
      if (selected(b.name))
        b.run_throughput(message_count, run_index);

  // Contended MPMC: multiple producers and consumers on one queue, unlike
  // the SPSC-style sections above. Always runs in full (small, fixed set).
  constexpr std::size_t kContendedProducers = 4;
  constexpr std::size_t kContendedConsumers = 4;
  for (std::size_t run_index = 0; run_index < repeats; ++run_index) {
    run_contended_throughput_bench<cds::MpmcQueue<Payload, kQueueCapacity>>(
        "mpmc_c", message_count, kContendedProducers, kContendedConsumers,
        run_index);
    run_contended_throughput_bench<cds::SpinlockQueue<Payload, kQueueCapacity>>(
        "spinlock_c", message_count, kContendedProducers, kContendedConsumers,
        run_index);
    run_contended_throughput_bench<cds::MutexQueue<Payload, kQueueCapacity>>(
        "mutex_c", message_count, kContendedProducers, kContendedConsumers,
        run_index);
  }

  // Stacks: same push/pop contract, different reclamation strategy. Run at
  // two thread counts to show how the lock-free variants scale relative to
  // the mutex baseline as contention increases.
  for (std::size_t run_index = 0; run_index < repeats; ++run_index) {
    for (std::size_t threads : {std::size_t{2}, std::size_t{8}}) {
      const std::string suffix = std::to_string(threads);
      run_stack_throughput_bench<cds::LockStack<Payload>>(
          ("lock" + suffix).c_str(), message_count, threads, run_index);
      run_stack_throughput_bench<cds::QuiescentLockFreeStack<Payload>>(
          ("quies" + suffix).c_str(), message_count, threads, run_index);
      run_stack_throughput_bench<cds::SplitRefLockFreeStack<Payload>>(
          ("splitref" + suffix).c_str(), message_count, threads, run_index);
      if (threads == 2) {
        run_stack_throughput_bench<cds::HazardLockFreeStack<Payload, 2>>(
            "haz2", message_count, threads, run_index);
      } else {
        run_stack_throughput_bench<cds::HazardLockFreeStack<Payload, 8>>(
            "haz8", message_count, threads, run_index);
      }
    }
  }

  return 0;
}
