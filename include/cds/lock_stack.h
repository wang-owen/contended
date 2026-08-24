#pragma once

#include <condition_variable>
#include <mutex>
#include <stack>
#include <stdexcept>
#include <utility>

namespace cds {

// Stack protected by a single mutex
template <typename T> class LockStack {
public:
  LockStack() = default;
  LockStack(const LockStack &) = delete;
  LockStack(LockStack &&) = delete;
  LockStack &operator=(const LockStack &) = delete;
  LockStack &operator=(LockStack &&) = delete;

  void push(T value) {
    std::lock_guard lock(mtx_);
    stack_.push(std::move(value));
  }

  bool try_pop(T &out) {
    std::lock_guard lock(mtx_);
    if (stack_.empty())
      return false;
    out = std::move(stack_.top());
    stack_.pop();
    return true;
  }

  void wait_and_pop(T &value) {
    std::unique_lock lock(mtx_);
    cv_.wait(lock, [this]() { return shutdown_ || !stack_.empty(); });
    if (shutdown_ && stack_.empty())
      throw std::runtime_error("Shutdown");
    value = std::move(stack_.top());
    stack_.pop();
  }

  void shutdown() {
    std::lock_guard lock(mtx_);
    shutdown_ = true;
  }

private:
  mutable std::mutex mtx_;
  std::stack<T> stack_;
  std::condition_variable cv_;
  bool shutdown_ = false;
};

} // namespace cds
