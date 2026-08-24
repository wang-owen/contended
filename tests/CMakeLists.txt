set(CDS_TESTS
    spsc_test
    mutex_queue_test
    spinlock_queue_test
    hazard_lockfree_stack_test
    lock_stack_test
    mpmc_queue_test
    quiescent_lockfree_stack_test
    split_ref_lockfree_stack_test
    split_ref_lockfree_queue_test)

foreach(test_name IN LISTS CDS_TESTS)
  add_executable(${test_name} ${test_name}.cc)
  target_link_libraries(${test_name} PRIVATE cds)
  target_compile_options(${test_name} PRIVATE -UNDEBUG)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
