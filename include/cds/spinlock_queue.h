#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

#include "cds/detail/cache_padding.h"

namespace cds {

// Bounded queue protected by a spinlock
template <typename T, std::size_t Capacity> class SpinlockQueue {
  static_assert(Capacity > 0, "Capacity must be greater than zero");

public:
  SpinlockQueue() = default;

  SpinlockQueue(const SpinlockQueue &) = delete;
  SpinlockQueue &operator=(const SpinlockQueue &) = delete;

  // Attempts to enqueue `value`. Returns false if the queue is full.
  // Safe to call concurrently with itself and with try_pop.
  bool try_push(T value) {
    Guard lock(m_);

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
    Guard lock(m_);

    if (tail_ == head_) {
      return std::nullopt;
    }

    return std::move(q_[head_++ % capacity()]);
  }

  static constexpr std::size_t capacity() { return Capacity; }

private:
  class Guard {
  public:
    explicit Guard(std::atomic_flag &flag) : flag_(flag) {
      while (flag_.test_and_set(std::memory_order_acquire)) { /* spin */
      }
    }
    ~Guard() { flag_.clear(std::memory_order_release); }

    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

  private:
    std::atomic_flag &flag_;
  };

  CACHE_ALIGNED std::atomic_flag m_;
  std::size_t head_ = 0, tail_ = 0;
  std::array<T, Capacity> q_;
};

} // namespace cds
