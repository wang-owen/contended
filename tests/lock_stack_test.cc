#include "cds/lock_stack.h"

#include <cassert>
#include <cstdio>
#include <stdexcept>

namespace {

void test_push_try_pop_lifo_order() {
  cds::LockStack<int> stack;

  int out = 0;
  assert(!stack.try_pop(out));

  stack.push(1);
  stack.push(2);
  stack.push(3);

  assert(stack.try_pop(out) && out == 3);
  assert(stack.try_pop(out) && out == 2);
  assert(stack.try_pop(out) && out == 1);
  assert(!stack.try_pop(out));
}

void test_wait_and_pop_returns_pushed_value() {
  cds::LockStack<int> stack;
  stack.push(42);

  int out = 0;
  stack.wait_and_pop(out);
  assert(out == 42);
}

void test_shutdown_unblocks_waiters() {
  cds::LockStack<int> stack;
  stack.shutdown();

  int out = 0;
  bool threw = false;
  try {
    stack.wait_and_pop(out);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
}

} // namespace

int main() {
  test_push_try_pop_lifo_order();
  test_wait_and_pop_returns_pushed_value();
  test_shutdown_unblocks_waiters();
  std::puts("lock_stack_test: OK");
  return 0;
}
