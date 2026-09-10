# Copyright (c) 2018 Nicolae Popescu. MIT License.

cmake_minimum_required(VERSION 4.1)

project(test_async)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# googletest and the tests both want a thread library; ask for it explicitly rather than relying on
# googletest's own find_package call having run first.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# googletest is fetched at configure time rather than vendored or added as a second submodule:
# `actuator` is a submodule because it is this project's own dependency under active development,
# while googletest is a pinned third-party release that nothing here ever edits.
include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
  GIT_SHALLOW    TRUE
)

# Without this, googletest links the static CRT on MSVC while this project links the dynamic one,
# and the two object files will not go together. It is a no-op everywhere else.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# googletest's own tests are not this project's business to build or run.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# enable_testing() creates the CTest targets, and writes the CTestTestfile.cmake that
# `ctest --test-dir` needs.
enable_testing()

#include headers directories
include_directories(
  ../
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

# Sanitizers are how the threading defects get diagnosed rather than guessed at: async_tests can
# only observe a queue that has already gone wrong, while -fsanitize=thread names the two racing
# accesses. Off by default - a sanitized build is several times slower, and TSan does not ship for
# every toolchain the CI matrix covers.
set(ASYNC_SANITIZE "" CACHE STRING "Build the tests with a sanitizer: thread, address, or empty")

# async_smoke_test drives the interface by hand and prints what happened; it asserts nothing and
# only fails by crashing. async_tests is the googletest suite that states expectations.
add_executable(async_smoke_test async_smoke_test.cpp)
add_executable(async_tests async_tests.cpp)

target_link_libraries(async_tests PRIVATE GTest::gtest_main)

foreach(target async_smoke_test async_tests)
  if (UNIX)
    target_link_libraries(${target} PRIVATE Threads::Threads)
  endif (UNIX)

  if (ASYNC_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=${ASYNC_SANITIZE} -g -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=${ASYNC_SANITIZE})
  endif()
endforeach()

# gtest_discover_tests registers every TEST() as its own CTest entry, which is what keeps this
# suite reportable: a defect in the header can abort the process, and one test per process means
# that takes down the single case that provoked it instead of every case after it.
include(GoogleTest)
gtest_discover_tests(async_tests
  PROPERTIES TIMEOUT 120
  DISCOVERY_TIMEOUT 60
)

add_test(NAME async_smoke_test COMMAND async_smoke_test)

# A defect in this header shows up as a wedged worker as readily as a wrong answer, and a test
# binary that never returns would hang the run rather than report. The cases carry their own
# waits; this is the backstop for the case where the process itself will not exit.
set_tests_properties(async_smoke_test PROPERTIES TIMEOUT 120)
