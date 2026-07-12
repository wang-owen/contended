#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

#include "cds/detail/cache_padding.h"

namespace cds {

// Bounded single-producer/single-consumer queue.
//
// Capacity is fixed at compile time. try_push() may only be called from the
// producer thread; try_pop() may only be called from the consumer thread.
// No other synchronization between producer and consumer is assumed beyond
// what this queue provides internally.
template <typename T, std::size_t Capacity> class SpscQueue {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two");

public:
  SpscQueue() = default;

  SpscQueue(const SpscQueue &) = delete;
  SpscQueue &operator=(const SpscQueue &) = delete;

  // Attempts to enqueue `value`. Returns false if the queue is full.
  bool try_push(T value) {
    std::size_t r = tail_.load(std::memory_order_relaxed);
    if (r - head_.load(std::memory_order_acquire) == capacity()) {
      return false;
    }

    q_[r & (capacity() - 1)] = std::move(value);
    tail_.fetch_add(1, std::memory_order_release);
    return true;
  }

  // Attempts to dequeue the oldest element. Returns std::nullopt if the
  // queue is empty.
  std::optional<T> try_pop() {
    std::size_t l = head_.load(std::memory_order_relaxed);
    if (tail_.load(std::memory_order_acquire) == l) {
      return std::nullopt;
    }

    T x = std::move(q_[l & (capacity() - 1)]);
    head_.fetch_add(1, std::memory_order_release);
    return x;
  }

  static constexpr std::size_t capacity() { return Capacity; }

private:
  CACHE_ALIGNED std::atomic<std::size_t> head_ = 0;
  CACHE_ALIGNED std::atomic<std::size_t> tail_ = 0;
  CACHE_ALIGNED std::array<T, Capacity> q_;
};

} // namespace cds
