#include "cds/split_ref_lockfree_stack.h"

#include <cassert>
#include <cstdio>
#include <memory>

namespace {

void test_single_threaded_lifo_order() {
  cds::SplitRefLockFreeStack<int> stack;

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
  cds::SplitRefLockFreeStack<std::unique_ptr<int>> stack;

  stack.push(std::make_unique<int>(42));
  auto out = stack.pop();
  assert(out && **out == 42);
  assert(stack.pop() == nullptr);
}

} // namespace

int main() {
  test_single_threaded_lifo_order();
  test_move_only_type();
  std::puts("split_ref_lockfree_stack_test: OK");
  return 0;
}
