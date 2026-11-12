cds_tests = [
  'spsc_test',
  'mutex_queue_test',
  'spinlock_queue_test',
  'hazard_lockfree_stack_test',
  'lock_stack_test',
  'mpmc_queue_test',
  'quiescent_lockfree_stack_test',
  'split_ref_lockfree_stack_test',
  'split_ref_lockfree_queue_test',
]

foreach test_name : cds_tests
  exe = executable(
    test_name,
    test_name + '.cc',
    dependencies: cds_dep,
    cpp_args: '-UNDEBUG',
  )
  test(test_name, exe)
endforeach
