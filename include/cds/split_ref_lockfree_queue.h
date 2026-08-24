#pragma once

#include <atomic>
#include <memory>

#include "detail/cache_padding.h"

namespace cds {

template <typename T> class SplitRefLockFreeQueue {
public:
  SplitRefLockFreeQueue() {
    CountedNode initial{new Node, 1};
    head_.store(initial, std::memory_order_relaxed);
    tail_.store(initial, std::memory_order_relaxed);
  }
  SplitRefLockFreeQueue(const SplitRefLockFreeQueue &) = delete;
  SplitRefLockFreeQueue &operator=(const SplitRefLockFreeQueue &) = delete;

  void push(T value) {
    std::unique_ptr<T> new_value = std::make_unique<T>(std::move(value));
    CountedNode new_dummy{new Node, 1};

    CountedNode old_tail = tail_.load();
    while (true) {
      increase_external_count(tail_, old_tail);
      T *old_value = nullptr;
      // KNOWN ABA GAP: if this thread is suspended right here, another
      // thread can fill old_tail.node, advance past it, and a consumer can
      // drain it (resetting `value` back to nullptr) before this CAS runs.
      // The CAS below then can't tell "never filled" from "filled and
      // already drained" -- both read as nullptr -- so it can succeed
      // against a node that's already behind head_, silently losing the
      // value. Root cause: `value` cycles nullptr -> ptr -> nullptr, and
      // nothing tags which cycle a given nullptr belongs to. Fix would be
      // pairing `value` with a generation counter (CAS on {ptr, gen} as one
      // unit, like CountedNode does for {node, external_count}) so a stale
      // thread's expected generation no longer matches after a drain.
      if (old_tail.node->value.compare_exchange_strong(
              old_value, new_value.get(), std::memory_order_release,
              std::memory_order_relaxed)) {
        CountedNode old_next{};
        if (!old_tail.node->next.compare_exchange_strong(
                old_next, new_dummy, std::memory_order_release,
                std::memory_order_acquire)) {
          delete new_dummy.node;
          new_dummy = old_next;
        }

        set_new_tail(old_tail, new_dummy);
        new_value.release();
        break;
      } else {
        CountedNode old_next{};
        if (old_tail.node->next.compare_exchange_strong(
                old_next, new_dummy, std::memory_order_release,
                std::memory_order_acquire)) {
          old_next = new_dummy;
          new_dummy.node = new Node;
        }
        set_new_tail(old_tail, old_next);
      }
    }
  }

  std::unique_ptr<T> pop() {
    CountedNode old_head = head_.load();
    while (true) {
      increase_external_count(head_, old_head);
      Node *const node = old_head.node;

      // Queue empty
      if (node == tail_.load().node) {
        node->release_ref();
        return std::unique_ptr<T>{};
      }

      // Claim data
      CountedNode next = node->next.load();
      if (head_.compare_exchange_strong(old_head, next,
                                        std::memory_order_release,
                                        std::memory_order_acquire)) {
        T *const res = node->value.exchange(nullptr, std::memory_order_acquire);
        node->release_external(old_head.external_count);
        return std::unique_ptr<T>(res);
      }

      node->release_ref();
    }
  }

private:
  struct Node;

  struct CountedNode {
    Node *node = nullptr;
    uint64_t external_count = 0;
  };

  // A node is reachable from up to two external roots (head_ and tail_),
  // each holding one of the "external counter" slots below.
  static constexpr int kExternalRoots = 2;

  struct NodeCounter {
    unsigned internal_count : 30;
    unsigned external_counters : 2;
  };

  struct Node {
    Node() : counter{NodeCounter{.external_counters = kExternalRoots}} {}

    void release_ref() { adjust_counter(-1, 0); }

    void release_external(uint64_t external_count) {
      adjust_counter(static_cast<int>(external_count) - kExternalRoots, -1);
    }

    std::atomic<T *> value;
    std::atomic<NodeCounter> counter;
    std::atomic<CountedNode> next;

  private:
    void adjust_counter(int internal_delta, int external_delta) {
      NodeCounter old_counter = counter.load();
      NodeCounter new_counter;
      do {
        new_counter = old_counter;
        new_counter.internal_count += internal_delta;
        new_counter.external_counters += external_delta;
      } while (!counter.compare_exchange_strong(old_counter, new_counter,
                                                std::memory_order_release,
                                                std::memory_order_relaxed));

      maybe_delete(this, new_counter);
    }
  };

  static_assert(
      std::atomic<NodeCounter>::is_always_lock_free,
      "NodeCounter must be lock-free for single-CAS split refcounting");

  static void increase_external_count(std::atomic<CountedNode> &counter,
                                      CountedNode &old_counter) {
    CountedNode new_counter;
    do {
      new_counter = old_counter;
      ++new_counter.external_count;
    } while (!counter.compare_exchange_strong(old_counter, new_counter,
                                              std::memory_order_relaxed,
                                              std::memory_order_acquire));
    old_counter = new_counter;
  }

  static void maybe_delete(Node *node, const NodeCounter &counter) {
    if (counter.internal_count == 0 && counter.external_counters == 0) {
      std::atomic_thread_fence(std::memory_order_acquire);
      delete node;
    }
  }

  void set_new_tail(CountedNode &old_tail, const CountedNode &new_tail) {
    Node *const current_tail_node = old_tail.node;
    while (!tail_.compare_exchange_weak(old_tail, new_tail,
                                        std::memory_order_release,
                                        std::memory_order_acquire) &&
           old_tail.node == current_tail_node) {
    }
    if (old_tail.node == current_tail_node) {
      current_tail_node->release_external(old_tail.external_count);
    } else {
      current_tail_node->release_ref();
    }
  }

  CACHE_ALIGNED std::atomic<CountedNode> tail_;
  CACHE_ALIGNED std::atomic<CountedNode> head_;
};

} // namespace cds
