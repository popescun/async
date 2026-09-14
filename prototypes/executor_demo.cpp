// Copyright (c) 2018 Nicolae Popescu. MIT License.

/**
 * @brief PROTOTYPE driver: exercises prototypes/executor.hpp and reports what it observed.
 *
 * Asserts nothing; it prints, and it is meant to be run under ThreadSanitizer as well as plain.
 */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <executor.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using untangle::async::prototype::executor;
using namespace std::chrono_literals;

int main() {
  // ---- 1. every task runs exactly once, and the work is spread ----
  {
    constexpr int task_count = 200;
    std::atomic_int ran{0};

    std::mutex seen_mutex;
    std::vector<std::thread::id> seen;

    {
      executor pool(4);
      for (int i = 0; i < task_count; ++i) {
        pool.submit([&ran, &seen_mutex, &seen] {
          ran.fetch_add(1, std::memory_order_relaxed);
          std::lock_guard<std::mutex> lock(seen_mutex);
          seen.push_back(std::this_thread::get_id());
        });
      }
    }  // the destructor drains, then stops

    std::vector<std::thread::id> distinct;
    for (const auto& id : seen) {
      bool known = false;
      for (const auto& one : distinct) {
        known = known || (one == id);
      }
      if (!known) {
        distinct.push_back(id);
      }
    }

    std::printf("1. %d/%d tasks ran, across %zu worker thread(s)\n", ran.load(), task_count,
                distinct.size());
  }

  // ---- 2. the shared queue keeps submission order ----
  {
    std::mutex order_mutex;
    std::vector<int> order;

    {
      executor pool(1);  // one worker, so the order observed is the queue's own
      for (int i = 0; i < 50; ++i) {
        pool.submit([i, &order_mutex, &order] {
          std::lock_guard<std::mutex> lock(order_mutex);
          order.push_back(i);
        });
      }
    }

    bool in_order = (order.size() == 50);
    for (std::size_t i = 0; in_order && i < order.size(); ++i) {
      in_order = (order[i] == static_cast<int>(i));
    }
    std::printf("2. %zu task(s) ran, submission order %s\n", order.size(),
                in_order ? "preserved" : "NOT preserved");
  }

  // ---- 3. a slow task does not stop the others being picked up ----
  {
    std::atomic_int ran{0};
    const auto started = std::chrono::steady_clock::now();

    {
      executor pool(4);
      pool.submit([&ran] {
        std::this_thread::sleep_for(150ms);
        ran.fetch_add(1, std::memory_order_relaxed);
      });
      for (int i = 0; i < 3; ++i) {
        pool.submit([&ran] {
          std::this_thread::sleep_for(150ms);
          ran.fetch_add(1, std::memory_order_relaxed);
        });
      }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::printf("3. 4 x 150ms tasks on 4 workers took %lldms (serial would be ~600ms)\n",
                static_cast<long long>(elapsed.count()));
  }

  // ---- 4. submitting from several threads at once ----
  {
    constexpr int per_thread = 100;
    constexpr int submitters = 4;
    std::atomic_int ran{0};

    {
      executor pool(3);
      std::vector<std::thread> threads;
      for (int t = 0; t < submitters; ++t) {
        threads.emplace_back([&pool, &ran] {
          for (int i = 0; i < per_thread; ++i) {
            pool.submit([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
          }
        });
      }
      for (auto& one : threads) {
        one.join();
      }
    }

    std::printf("4. %d/%d tasks ran, submitted from %d threads at once\n", ran.load(),
                per_thread * submitters, submitters);
  }

  // ---- 5. the pool refuses work once it is shutting down ----
  {
    executor pool(2);
    std::printf("5. submit() while accepting: %s\n", pool.submit([] {}) ? "accepted" : "refused");
  }

  std::puts("done");
  return 0;
}
