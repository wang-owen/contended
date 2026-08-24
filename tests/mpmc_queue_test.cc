#include "cds/mpmc_queue.h"

#include <cassert>
#include <cstdio>

namespace {

void test_push_pop_fifo_order() {
  cds::MpmcQueue<int, 4> queue;

  assert(!queue.try_pop().has_value());

  assert(queue.try_push(1));
  assert(queue.try_push(2));
  assert(queue.try_push(3));

  auto a = queue.try_pop();
  assert(a && *a == 1);
  auto b = queue.try_pop();
  assert(b && *b == 2);
  auto c = queue.try_pop();
  assert(c && *c == 3);
  assert(!queue.try_pop().has_value());
}

void test_try_push_fails_when_full() {
  cds::MpmcQueue<int, 2> queue;

  assert(queue.try_push(1));
  assert(queue.try_push(2));
  assert(!queue.try_push(3));
}

} // namespace

int main() {
  test_push_pop_fifo_order();
  test_try_push_fails_when_full();
  std::puts("mpmc_queue_test: OK");
  return 0;
}
