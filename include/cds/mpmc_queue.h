#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "cds/detail/cache_padding.h"

namespace cds {

// Bounded multi-producer/multi-consumer queue (Vyukov ring buffer).
//
// Capacity is fixed at compile time. try_push() and try_pop() are safe to
// call concurrently, from any number of producer and consumer threads.
template <typename T, std::size_t Capacity> class MpmcQueue {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two");

public:
  MpmcQueue() {
    for (std::size_t i = 0; i < Capacity; ++i) {
      buffer_[i].sequence.store(i);
    }
  }
  MpmcQueue(const MpmcQueue &) = delete;
  MpmcQueue &operator=(const MpmcQueue &) = delete;

  // Attempts to enqueue `value`. Returns false if the queue is full.
  bool try_push(T value) {
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    while (true) {
      Cell &cell = buffer_[pos & (capacity() - 1)];
      std::size_t seq = cell.sequence.load(std::memory_order_acquire);
      std::intptr_t dif =
          static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

      if (dif == 0) {
        if (!enqueue_pos_.compare_exchange_strong(pos, pos + 1,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
          continue;
        }
        cell.value = std::move(value);
        cell.sequence.store(seq + 1, std::memory_order_release);
        return true;
      } else if (dif < 0) {
        return false;
      }
      pos = enqueue_pos_.load(std::memory_order_relaxed);
    }
  }

  // Attempts to dequeue the oldest element. Returns std::nullopt if the
  // queue is empty.
  std::optional<T> try_pop() {
    std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    while (true) {
      Cell &cell = buffer_[pos & (capacity() - 1)];
      std::size_t seq = cell.sequence.load(std::memory_order_acquire);
      std::size_t target = pos + 1;
      std::intptr_t dif =
          static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(target);

      if (dif == 0) {
        if (!dequeue_pos_.compare_exchange_strong(pos, pos + 1,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
          continue;
        }
        std::optional<T> res = std::move(cell.value);
        cell.sequence.store(pos + Capacity, std::memory_order_release);
        return res;
      } else if (dif < 0) {
        return std::nullopt;
      }
      pos = dequeue_pos_.load(std::memory_order_relaxed);
    }
  }

  static constexpr std::size_t capacity() { return Capacity; }

private:
  struct Cell {
    T value;
    std::atomic<std::size_t> sequence;
  };

  std::array<Cell, Capacity> buffer_;
  CACHE_ALIGNED std::atomic<std::size_t> dequeue_pos_ = 0;
  CACHE_ALIGNED std::atomic<std::size_t> enqueue_pos_ = 0;
};

} // namespace cds
