# CMake target names must be globally unique, and Google Benchmark's own
# CMakeLists always defines a library target literally called "benchmark" --
# so this target is renamed internally, but OUTPUT_NAME keeps the produced
# binary at the same path (./build/benchmarks/benchmark) as before.
add_executable(cds_benchmark_legacy benchmark.cc)
set_target_properties(cds_benchmark_legacy PROPERTIES OUTPUT_NAME benchmark)
target_link_libraries(cds_benchmark_legacy PRIVATE cds)
