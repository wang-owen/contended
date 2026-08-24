#include "cds/split_ref_lockfree_queue.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

void test_single_threaded_fifo_order() {
  cds::SplitRefLockFreeQueue<int> queue;

  assert(queue.pop() == nullptr);

  queue.push(1);
  queue.push(2);
  queue.push(3);

  auto a = queue.pop();
  assert(a && *a == 1);
  auto b = queue.pop();
  assert(b && *b == 2);
  auto c = queue.pop();
  assert(c && *c == 3);
  assert(queue.pop() == nullptr);
}

constexpr std::size_t kThreadCount = 8;
constexpr int kIterations = 1000;

// Each thread pushes kIterations values then pops kIterations values back
// off (possibly items pushed by other threads), exercising concurrent
// push/pop and split-refcount reclamation across threads. Pushes and pops
// balance out, so the queue must be empty once every thread has finished.
void test_concurrent_push_pop() {
  cds::SplitRefLockFreeQueue<int> queue;

  std::vector<std::thread> pool;
  pool.reserve(kThreadCount);
  for (std::size_t t = 0; t < kThreadCount; ++t) {
    pool.emplace_back([&queue, t] {
      for (int i = 0; i < kIterations; ++i) {
        queue.push(static_cast<int>(t) * kIterations + i);
      }
      for (int i = 0; i < kIterations; ++i) {
        while (!queue.pop()) { /* spin */
        }
      }
    });
  }
  for (auto &t : pool)
    t.join();

  assert(queue.pop() == nullptr);
}

} // namespace

int main() {
  test_single_threaded_fifo_order();
  test_concurrent_push_pop();
  std::puts("split_ref_lockfree_queue_test: OK");
  return 0;
}
