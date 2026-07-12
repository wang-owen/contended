#include "cds/spsc_queue.h"

#include <cassert>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>

namespace {

#ifdef ENABLE_PADDING
// Compile-time check that CACHE_ALIGNED actually pushes the contended cursors
// onto their own 128B line rather than the ABI-frozen 64B default.
static_assert(alignof(cds::SpscQueue<int, 1024>) == 128);
#endif

void test_single_threaded_fifo_and_bounds() {
  cds::SpscQueue<int, 4> q;

  assert(q.try_pop() == std::nullopt);

  assert(q.try_push(1));
  assert(q.try_push(2));
  assert(q.try_push(3));
  assert(q.try_push(4));
  assert(!q.try_push(5));

  assert(q.try_pop() == 1);
  assert(q.try_push(5));
  assert(q.try_pop() == 2);
  assert(q.try_pop() == 3);
  assert(q.try_pop() == 4);
  assert(q.try_pop() == 5);
  assert(q.try_pop() == std::nullopt);
}

void test_capacity_one() {
  cds::SpscQueue<int, 1> q;

  assert(q.try_pop() == std::nullopt);
  assert(q.try_push(1));
  assert(!q.try_push(2));
  assert(q.try_pop() == 1);
  assert(q.try_pop() == std::nullopt);

  for (int i = 0; i < 10; ++i) {
    assert(q.try_push(i));
    assert(!q.try_push(-1));
    assert(q.try_pop() == i);
  }
}

void test_move_only_type() {
  cds::SpscQueue<std::unique_ptr<int>, 4> q;

  assert(q.try_pop() == std::nullopt);

  assert(q.try_push(std::make_unique<int>(1)));
  assert(q.try_push(std::make_unique<int>(2)));
  assert(q.try_push(std::make_unique<int>(3)));
  assert(q.try_push(std::make_unique<int>(4)));
  assert(!q.try_push(std::make_unique<int>(5)));

  auto a = q.try_pop();
  assert(a && **a == 1);
  assert(q.try_push(std::make_unique<int>(5)));
  auto b = q.try_pop();
  assert(b && **b == 2);
  auto c = q.try_pop();
  assert(c && **c == 3);
  auto d = q.try_pop();
  assert(d && **d == 4);
  auto e = q.try_pop();
  assert(e && **e == 5);
  assert(q.try_pop() == std::nullopt);
}

void test_two_thread_producer_consumer() {
  constexpr int kCount = 1'000'000;
  cds::SpscQueue<int, 1024> q;

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      while (!q.try_push(i)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    for (int expected = 0; expected < kCount; ++expected) {
      std::optional<int> value;
      while (!(value = q.try_pop())) {
        std::this_thread::yield();
      }
      assert(*value == expected);
    }
  });

  producer.join();
  consumer.join();
}

// A failed try_push consumes its by-value argument, so retries need a fresh
// value each attempt rather than reusing an already-moved-from local.
void test_two_thread_producer_consumer_move_only() {
  constexpr int kCount = 100'000;
  cds::SpscQueue<std::unique_ptr<int>, 1024> q;

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      while (!q.try_push(std::make_unique<int>(i))) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    for (int expected = 0; expected < kCount; ++expected) {
      std::optional<std::unique_ptr<int>> value;
      while (!(value = q.try_pop())) {
        std::this_thread::yield();
      }
      assert(**value == expected);
    }
  });

  producer.join();
  consumer.join();
}

} // namespace

int main() {
  test_single_threaded_fifo_and_bounds();
  test_capacity_one();
  test_move_only_type();
  test_two_thread_producer_consumer();
  test_two_thread_producer_consumer_move_only();
  std::puts("spsc_test: OK");
  return 0;
}
