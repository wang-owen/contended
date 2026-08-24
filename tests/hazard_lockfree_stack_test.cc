#include "cds/hazard_lockfree_stack.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kThreadCount = 8;
constexpr int kIterations = 1000;

void test_single_threaded_lifo_order() {
  cds::HazardLockFreeStack<int, 1> stack;

  assert(stack.pop() == nullptr);

  stack.push(1);
  stack.push(2);
  stack.push(3);

  auto a = stack.pop();
  assert(a && *a == 3);
  auto b = stack.pop();
  assert(b && *b == 2);
  auto c = stack.pop();
  assert(c && *c == 1);
  assert(stack.pop() == nullptr);
}

void test_move_only_type() {
  cds::HazardLockFreeStack<std::unique_ptr<int>, 1> stack;

  stack.push(std::make_unique<int>(42));
  auto out = stack.pop();
  assert(out && **out == 42);
  assert(stack.pop() == nullptr);
}

// Each thread pushes kIterations values then pops kIterations values back
// off (possibly items pushed by other threads), exercising concurrent
// push/pop and hazard-pointer reclamation across threads. Pushes and pops
// balance out, so the stack must be empty once every thread has finished.
void test_concurrent_push_pop() {
  cds::HazardLockFreeStack<int, kThreadCount + 1> stack;

  std::vector<std::thread> pool;
  pool.reserve(kThreadCount);
  for (std::size_t t = 0; t < kThreadCount; ++t) {
    pool.emplace_back([&stack, t] {
      for (int i = 0; i < kIterations; ++i) {
        stack.push(static_cast<int>(t) * kIterations + i);
      }
      for (int i = 0; i < kIterations; ++i) {
        while (!stack.pop()) { /* spin */
        }
      }
    });
  }
  for (auto &t : pool)
    t.join();

  assert(stack.pop() == nullptr);
}

} // namespace

int main() {
  test_single_threaded_lifo_order();
  test_move_only_type();
  test_concurrent_push_pop();
  std::puts("hazard_lockfree_stack_test: OK");
  return 0;
}
