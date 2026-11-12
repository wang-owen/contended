cmake_minimum_required(VERSION 3.20)
project(contended CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Only Debug (tests, unoptimized) and RelWithDebInfo (benchmarks/sanitizers) are
# supported. No default, so every invocation states its build type explicitly.
set(CDS_SUPPORTED_BUILD_TYPES Debug RelWithDebInfo)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                                             ${CDS_SUPPORTED_BUILD_TYPES})

if(NOT CMAKE_BUILD_TYPE)
  message(
    FATAL_ERROR
      "CMAKE_BUILD_TYPE must be set explicitly. Supported values: ${CDS_SUPPORTED_BUILD_TYPES}"
  )
endif()

if(NOT CMAKE_BUILD_TYPE IN_LIST CDS_SUPPORTED_BUILD_TYPES)
  message(
    FATAL_ERROR
      "Unsupported CMAKE_BUILD_TYPE '${CMAKE_BUILD_TYPE}'. Supported values: ${CDS_SUPPORTED_BUILD_TYPES}"
  )
endif()

option(CDS_BUILD_TESTS "Build tests" ON)
option(CDS_BUILD_BENCHMARKS "Build benchmarks" ON)
option(CDS_ENABLE_TSAN "Build with ThreadSanitizer instrumentation" OFF)
option(CDS_ENABLE_ASAN
       "Build with Address/UndefinedBehaviorSanitizer instrumentation" OFF)
option(
  CDS_ENABLE_CACHE_PADDING
  "Pad contended atomics/locks to their own cache line to prevent false sharing"
  ON)

if(CDS_ENABLE_TSAN AND CDS_ENABLE_ASAN)
  message(
    FATAL_ERROR "CDS_ENABLE_TSAN and CDS_ENABLE_ASAN are mutually exclusive")
endif()

if((CDS_ENABLE_TSAN OR CDS_ENABLE_ASAN) AND NOT CMAKE_BUILD_TYPE STREQUAL
                                            "RelWithDebInfo")
  message(
    FATAL_ERROR
      "CDS_ENABLE_TSAN/CDS_ENABLE_ASAN require -DCMAKE_BUILD_TYPE=RelWithDebInfo"
  )
endif()

find_package(Threads REQUIRED)

# Header-only lib: linking `cds` picks up include path, warnings, sanitizers.
add_library(cds INTERFACE)
target_include_directories(cds INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cds INTERFACE cxx_std_20)
target_compile_options(cds INTERFACE -Wall -Wextra)
target_link_libraries(cds INTERFACE Threads::Threads)

if(CDS_ENABLE_CACHE_PADDING)
  target_compile_definitions(cds INTERFACE ENABLE_PADDING)
endif()

if(CDS_ENABLE_TSAN)
  target_compile_options(cds INTERFACE -fsanitize=thread)
  target_link_options(cds INTERFACE -fsanitize=thread)
elseif(CDS_ENABLE_ASAN)
  target_compile_options(cds INTERFACE -fsanitize=address,undefined)
  target_link_options(cds INTERFACE -fsanitize=address,undefined)
endif()

if(CDS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CDS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
