// Copyright (c) 2018 Nicolae Popescu. MIT License.

/**
 * @brief Behaviour tests for untangle::async::execution.
 *
 * Unlike async_smoke_test.cpp, which drives the interface by hand and prints what happened, every
 * case here states an expectation and fails when it does not hold.
 *
 * @remark These tests are written against the queue contract: an action handed to add_action() runs
 * exactly once on the worker thread, and stop() ends the worker. They are expected to FAIL against
 * the current header - execution::action_list is a bare std::list shared by the caller thread and
 * the worker with nothing between them, so the contract holds only by luck.
 *
 * @remark A short count is the symptom, not the diagnosis. Configure with -DASYNC_SANITIZE=thread
 * and ThreadSanitizer names the two racing accesses directly.
 */
#include <gtest/gtest.h>

#include <async.hpp>
#include <atomic>
#include <chrono>
#include <memory>

namespace {

using namespace std::chrono_literals;

/**
 * @brief Waits for a predicate to hold, so a defect is reported as a failure rather than a hang.
 *
 * @return true - the predicate held before the limit elapsed.
 */
template <typename predicateT>
bool wait_for(predicateT predicate, std::chrono::milliseconds limit) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

/**
 * @brief Counts how many times its bound method was actually invoked.
 *
 * The counter is atomic so that the queue under test is the only unsynchronised thing in the test.
 */
struct sink {
  void count(int) { calls.fetch_add(1, std::memory_order_relaxed); }

  std::atomic_int calls = {0};
  std::function<void(int)> action;
};

/**
 * @brief Waits for the polled executions to finish, the way async_smoke_test.cpp does.
 *
 * The worker thread is detached and keeps reading the object's members, so returning from a test
 * while it is still running would replace a reported failure with a use-after-free. Waiting on the
 * poll is the interface's own answer to that - execution is not joinable, so a registered object
 * and execution_poll::is_running() are what stand in for a join.
 *
 * @remark execution_poll::add() has no inverse, so an execution stays registered after it has been
 * destroyed and the singleton goes on calling into it. gtest_discover_tests gives every case its
 * own process, which is what keeps a stale registration from reaching the next case; running the
 * binary directly with more than one case does not.
 *
 * @return true - the poll reported idle before the limit elapsed.
 */
bool wait_until_poll_idle(std::chrono::milliseconds limit) {
  return wait_for([] { return !untangle::async::execution_poll::get().is_running(); }, limit);
}

}  // namespace

/**
 * @brief The queue works at all: one action, queued before the worker exists, runs once.
 *
 * Nothing races here - the push happens before run() spawns anything - so this case passes against
 * the current header. It is what tells the failures below apart from a broken test.
 */
TEST(execution_queue, runs_an_action_queued_before_the_worker_starts) {
  auto s = std::make_shared<sink>();
  untangle::async::execution<std::function<void(int)>> exec{"queued_before_start"};
  exec.bind_action_and_method(s->action, s, &sink::count);
  untangle::async::execution_poll::get().add(exec);

  s->action(1);
  exec.run();

  EXPECT_TRUE(wait_for([&s] { return s->calls.load() == 1; }, 2000ms));
  EXPECT_EQ(s->calls.load(), 1);

  ASSERT_TRUE(wait_until_poll_idle(5000ms))
      << "the worker was still running 5s later; the object cannot be destroyed safely";
}

/**
 * @brief Every action pushed while the worker drains runs.
 *
 * This is what the class exists to do: the caller thread pushes through add_action() while the
 * worker pops in execute_actions(), and neither side takes a lock. It fails one of two ways, and
 * which one comes up is a matter of timing rather than of how many actions are queued:
 *
 * - the count comes out short, because a push landed against a concurrent pop_front() - roughly 1
 *   action in 10 goes missing at this size;
 * - the process aborts, because the worker reached a std::function that was still being
 *   constructed, and invoking it threw std::bad_function_call out of the thread function.
 */
TEST(execution_queue, runs_every_action_queued_while_the_worker_drains) {
  constexpr int queued = 1000;

  auto s = std::make_shared<sink>();
  untangle::async::execution<std::function<void(int)>> exec{"queued_while_draining"};
  exec.bind_action_and_method(s->action, s, &sink::count);
  untangle::async::execution_poll::get().add(exec);

  exec.start();
  for (int i = 0; i < queued; ++i) {
    s->action(i);
  }

  EXPECT_TRUE(wait_for([&s] { return s->calls.load() == queued; }, 2000ms));
  EXPECT_EQ(s->calls.load(), queued) << "actions were dropped between push_back and pop_front";

  exec.stop();
  ASSERT_TRUE(wait_until_poll_idle(5000ms))
      << "the worker was still running 5s after stop(); the object cannot be destroyed safely";
}

/**
 * @brief An action queued after stop() is refused, not silently stranded.
 *
 * stop() ends this execution's working life: the worker drains what was queued before it and then
 * leaves. An action handed to add_action() after that point would never run, so it is dropped where
 * the caller can see the count stand still, rather than left in the list looking pending.
 */
TEST(execution_queue, refuses_an_action_queued_after_the_worker_stops) {
  auto s = std::make_shared<sink>();
  untangle::async::execution<std::function<void(int)>> exec{"queued_after_stop"};
  exec.bind_action_and_method(s->action, s, &sink::count);
  untangle::async::execution_poll::get().add(exec);

  exec.start();
  s->action(1);
  ASSERT_TRUE(wait_for([&s] { return s->calls.load() == 1; }, 2000ms)) << "the first action ran";

  exec.stop();
  s->action(2);

  ASSERT_TRUE(wait_until_poll_idle(5000ms))
      << "the worker was still running 5s after stop(); the object cannot be destroyed safely";

  // The worker has left, so the count is final rather than merely not there yet.
  EXPECT_EQ(s->calls.load(), 1) << "an action queued after stop() ran anyway";
}

/**
 * @brief stop() ends a worker that has only just been started.
 *
 * loop() sets `started` from inside the worker thread; stop() clears it from the caller. Nothing
 * orders the two, so a stop() that lands first is overwritten by the worker, and the loop spins on
 * forever at 100% of a core.
 */
TEST(execution_lifecycle, stop_ends_a_worker_that_just_started) {
  // Heap allocated and deliberately leaked when it wedges: the worker is detached and still holds
  // this pointer, so destroying the object would turn a reported failure into a use-after-free.
  auto* exec = new untangle::async::execution<std::function<void(void)>>{"stop_after_start"};
  untangle::async::execution_poll::get().add(*exec);

  exec->start();
  exec->stop();

  const auto stopped = wait_until_poll_idle(2000ms);
  EXPECT_TRUE(stopped) << "the worker was still spinning 2s after stop()";

  if (stopped) {
    delete exec;
  }
}
