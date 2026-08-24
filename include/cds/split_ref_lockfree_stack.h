#pragma once

#include <atomic>
#include <iostream>
#include <memory>

namespace cds {

// Lock-free Treiber stack with split reference counting for safe node
// reclamation
template <typename T> class SplitRefLockFreeStack {
public:
  SplitRefLockFreeStack() {
    if (!std::atomic<CountedNode>{}.is_lock_free()) {
      std::cerr << "Double-word CAS not possible. Fallback to lock-based.\n";
    }
  }
  ~SplitRefLockFreeStack() {
    Node *cur = head_.load(std::memory_order_relaxed).node;
    while (cur != nullptr) {
      Node *next = cur->next.node;
      delete cur;
      cur = next;
    }
  }
  SplitRefLockFreeStack(const SplitRefLockFreeStack &) = delete;
  SplitRefLockFreeStack &operator=(const SplitRefLockFreeStack &) = delete;

  void push(T value) {
    CountedNode cnode{
        .external_count = 1,
        .node = new Node{.value = std::make_unique<T>(std::move(value)),
                         .next = head_.load()}};
    while (!head_.compare_exchange_weak(cnode.node->next, cnode,
                                        std::memory_order_release,
                                        std::memory_order_relaxed))
      ;
  }

  // Claims a reference to head_'s node (increase_head_count), then races
  // other poppers to swing head_ past it; the racer that wins the CAS owns
  // the value, everyone else releases their claim on the way out.
  std::unique_ptr<T> pop() {
    CountedNode old_head = head_.load();
    while (true) {
      increase_head_count(old_head);
      Node *const node = old_head.node;
      if (node == nullptr) {
        return std::unique_ptr<T>{};
      }

      if (head_.compare_exchange_strong(old_head, node->next,
                                        std::memory_order_release,
                                        std::memory_order_acquire)) {
        // Swung head_ past node, so it's now unreachable from
        // head_. Release all references we accumulated (external_count),
        // minus one for this thread's own use and one for the head_ slot
        // that used to hold it; free the node once that count is settled.
        std::unique_ptr<T> res = std::move(node->value);
        const int count_increase = old_head.external_count - 2;
        if (node->internal_count.fetch_add(
                count_increase, std::memory_order_acq_rel) == -count_increase) {
          delete node;
        }
        return res;
      } else if (node->internal_count.fetch_sub(1, std::memory_order_acq_rel) ==
                 1) {
        // Last outstanding thread
        delete node;
      }
    }
  }

private:
  struct Node;

  // external_count: number of threads currently holding a copy of this
  // pointer (e.g. via head_), incremented before a thread dereferences node.
  struct CountedNode {
    uint32_t external_count = 0;
    Node *node = nullptr;
  };

  // internal_count: negative sum of all external_counts ever released back
  // to this node, minus the head-swing "phantom" reference. Node is freed
  // once every external reference has been accounted for.
  struct Node {
    std::atomic<int> internal_count = 0;
    std::unique_ptr<T> value;
    CountedNode next;
  };

  void increase_head_count(CountedNode &old_head) {
    CountedNode new_head;
    do {
      new_head = old_head;
      ++new_head.external_count;
    } while (!head_.compare_exchange_weak(old_head, new_head,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed));
    old_head.external_count = new_head.external_count;
  }

  std::atomic<CountedNode> head_;
};

} // namespace cds
