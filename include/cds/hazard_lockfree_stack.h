#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cds {

// Lock-free stack that reclaims popped nodes via hazard pointers
template <typename T, std::size_t HazardCapacity> class HazardLockFreeStack {
public:
  HazardLockFreeStack() = default;
  ~HazardLockFreeStack() {
    reclaim_available();
    Node *cur = head_.load(std::memory_order_relaxed);
    while (cur != nullptr) {
      Node *next = cur->next;
      delete cur;
      cur = next;
    }
  }
  HazardLockFreeStack(const HazardLockFreeStack &) = delete;
  HazardLockFreeStack &operator=(const HazardLockFreeStack &) = delete;

  void push(T value) {
    Node *node = new Node{.value = std::make_unique<T>(std::move(value))};
    node->next = head_.load(std::memory_order_relaxed);
    while (!head_.compare_exchange_weak(
        node->next, node, std::memory_order_release, std::memory_order_relaxed))
      ;
  }

  // Safe to call concurrently with itself and push. Throws
  // std::runtime_error on a calling thread's first pop() if all
  // `HazardCapacity` hazard-pointer slots are already owned by other
  // live threads.
  std::unique_ptr<T> pop() {
    std::atomic<Node *> &hp = get_hp();

    Node *old_head = head_.load(std::memory_order_relaxed);
    do {
      Node *temp;
      do {
        temp = old_head;
        hp.store(old_head);
        old_head = head_.load();
      } while (old_head != temp);
    } while (old_head &&
             !head_.compare_exchange_weak(old_head, old_head->next,
                                          std::memory_order_release,
                                          std::memory_order_acquire));

    hp.store(nullptr);

    std::unique_ptr<T> res;
    if (old_head) {
      res = std::move(old_head->value);
      if (outstanding_hazards(old_head)) {
        reclaim_later(old_head, retired_);
      } else {
        delete old_head;
      }
      reclaim_available();
    }

    return res;
  }

private:
  struct Node {
    std::unique_ptr<T> value;
    Node *next;
    Node *reclaim_next;
  };

  struct HazardPointer {
    std::atomic<std::thread::id> id;
    std::atomic<Node *> ptr;
  };

  struct HpOwner {
    HpOwner() : hp{nullptr} {
      for (std::size_t i = 0; i < HazardCapacity; ++i) {
        std::thread::id old_id;
        if (hazard_pointers_[i].id.compare_exchange_strong(
                old_id, std::this_thread::get_id(), std::memory_order_relaxed,
                std::memory_order_relaxed)) {
          hp = &hazard_pointers_[i];
          break;
        }
      }
      if (hp == nullptr)
        throw std::runtime_error("No hazard pointers available.");
    }
    ~HpOwner() {
      if (Node *first = retired_.load(std::memory_order_relaxed))
        chain_pending_nodes(first, global_retired_);
      hp->ptr.store(nullptr);
      hp->id.store(std::thread::id());
    }
    HpOwner(const HpOwner &) = delete;
    HpOwner &operator=(const HpOwner &) = delete;

    HazardPointer *hp;
  };

  inline static std::array<HazardPointer, HazardCapacity> hazard_pointers_;
  inline thread_local static std::atomic<Node *> retired_ = nullptr;
  inline static std::atomic<Node *> global_retired_ = nullptr;

  bool outstanding_hazards(Node *node) const {
    for (const auto &hp : hazard_pointers_) {
      if (hp.ptr == node)
        return true;
    }
    return false;
  }

  void reclaim_later(Node *node, std::atomic<Node *> &list) {
    node->reclaim_next = list.load(std::memory_order_relaxed);
    while (!list.compare_exchange_weak(node->reclaim_next, node,
                                       std::memory_order_release,
                                       std::memory_order_relaxed))
      ;
  }

  // Drains the calling thread's and the shared global retired lists,
  // deleting whatever is no longer hazarded.
  void reclaim_available() {
    delete_retired(retired_);
    delete_retired(global_retired_);
  }

  void delete_retired(std::atomic<Node *> &list) {
    if (list.load(std::memory_order_relaxed) == nullptr)
      return;

    Node *retired = list.exchange(nullptr, std::memory_order_acquire);

    std::array<Node *, HazardCapacity> claimed;
    std::size_t claimed_count = 0;
    for (const auto &hp : hazard_pointers_) {
      if (Node *ptr = hp.ptr.load())
        claimed[claimed_count++] = ptr;
    }
    auto is_claimed = [&](Node *node) {
      return std::find(claimed.begin(), claimed.begin() + claimed_count,
                       node) != claimed.begin() + claimed_count;
    };

    Node *cur = retired;
    Node *prev = nullptr;
    Node *first = nullptr;
    while (cur != nullptr) {
      Node *next = cur->reclaim_next;
      if (!is_claimed(cur)) {
        delete cur;
      } else {
        if (first == nullptr) {
          first = cur;
        } else {
          prev->reclaim_next = cur;
        }
        prev = cur;
      }
      cur = next;
    }

    if (prev != nullptr) {
      prev->reclaim_next = nullptr;
      chain_pending_nodes(first, list);
    }
  }

  std::atomic<Node *> &get_hp() {
    thread_local static HpOwner hazard;
    return hazard.hp->ptr;
  }

  static void chain_pending_nodes(Node *first, std::atomic<Node *> &list) {
    Node *last = first;
    while (Node *next = last->reclaim_next) {
      last = next;
    }

    last->reclaim_next = list.load(std::memory_order_relaxed);
    while (!list.compare_exchange_weak(last->reclaim_next, first,
                                       std::memory_order_release,
                                       std::memory_order_relaxed))
      ;
  }

  std::atomic<Node *> head_ = nullptr;
};

} // namespace cds
