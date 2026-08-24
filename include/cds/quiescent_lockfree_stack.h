#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace cds {

// Lock-free stack that reclaims popped nodes once no thread is mid-pop
// (quiescent-state-based reclamation)
template <typename T> class QuiescentLockFreeStack {
public:
  QuiescentLockFreeStack() = default;
  ~QuiescentLockFreeStack() {
    delete_nodes(retired_);
    Node *cur = head_;
    while (cur != nullptr) {
      Node *next = cur->next;
      delete cur;
      cur = next;
    }
  }
  QuiescentLockFreeStack(const QuiescentLockFreeStack &) = delete;
  QuiescentLockFreeStack &operator=(const QuiescentLockFreeStack &) = delete;

  void push(T value) {
    Node *const new_node = new Node(std::make_unique<T>(std::move(value)));
    new_node->next = head_.load();
    while (!head_.compare_exchange_weak(new_node->next, new_node,
                                        std::memory_order_release,
                                        std::memory_order_relaxed))
      ;
  }

  std::unique_ptr<T> pop() {
    ++threads_in_pop_;

    Node *old_head = head_.load();
    while (old_head && !head_.compare_exchange_weak(old_head, old_head->next,
                                                    std::memory_order_release,
                                                    std::memory_order_acquire))
      ;

    std::unique_ptr<T> out;
    if (old_head)
      out = std::move(old_head->value);

    try_reclaim(old_head);
    return out;
  }

private:
  struct Node {
    std::unique_ptr<T> value;
    Node *next;
    Node *to_reclaim;
  };

  std::atomic<Node *> retired_ = nullptr;
  std::atomic<unsigned> threads_in_pop_ = 0;

  void chain_pending_nodes(Node *first) {
    Node *last = first;
    while (Node *next = last->to_reclaim) {
      last = next;
    }

    last->to_reclaim = retired_.load();
    while (!retired_.compare_exchange_weak(last->to_reclaim, first,
                                           std::memory_order_release,
                                           std::memory_order_relaxed))
      ;
  }

  void delete_nodes(Node *node) {
    Node *next = nullptr;
    while (node != nullptr) {
      next = node->to_reclaim;
      delete node;
      node = next;
    }
  }

  void try_reclaim(Node *node) {
    if (threads_in_pop_ == 1) {
      Node *nodes_to_delete =
          retired_.exchange(nullptr, std::memory_order_acquire);
      if (--threads_in_pop_ == 0) {
        delete_nodes(nodes_to_delete);
      } else if (nodes_to_delete != nullptr) {
        chain_pending_nodes(nodes_to_delete);
      }
      delete node;
    } else {
      if (node != nullptr)
        chain_pending_nodes(node);
      --threads_in_pop_;
    }
  }

  std::atomic<Node *> head_ = nullptr;
};

} // namespace cds
