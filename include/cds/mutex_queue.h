#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

#include "cds/detail/cache_padding.h"

namespace cds {

// Bounded queue protected by a single mutex
template <typename T, std::size_t Capacity> class MutexQueue {
  static_assert(Capacity > 0, "Capacity must be greater than zero");

public:
  MutexQueue() = default;

  MutexQueue(const MutexQueue &) = delete;
  MutexQueue &operator=(const MutexQueue &) = delete;

  // Attempts to enqueue `value`. Returns false if the queue is full.
  // Safe to call concurrently with itself and with try_pop.
  bool try_push(T value) {
    std::lock_guard<std::mutex> lock{mtx_};
    if (tail_ - head_ >= capacity()) {
      return false;
    }

    q_[tail_++ % capacity()] = std::move(value);
    return true;
  }

  // Attempts to dequeue the oldest element. Returns std::nullopt if the
  // queue is empty. Safe to call concurrently with itself and with
  // try_push.
  std::optional<T> try_pop() {
    std::lock_guard<std::mutex> lock{mtx_};
    if (tail_ == head_) {
      return std::nullopt;
    }

    return std::move(q_[head_++ % capacity()]);
  }

  static constexpr std::size_t capacity() { return Capacity; }

private:
  CACHE_ALIGNED std::mutex mtx_;
  std::size_t head_ = 0, tail_ = 0;
  std::array<T, Capacity> q_;
};

} // namespace cds
