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
#include <vector>

namespace {

using namespace std::chrono_literals;

// The action types under test, named once: bind_action_and_*() are static, so every call has to
// qualify the execution type, and the full specialisation does not fit in a line.
using int_execution = untangle::async::execution<std::function<void(int)>>;
using void_execution = untangle::async::execution<std::function<void(void)>>;

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
  // time of check; the time of use is the caller's next statement, and for every caller here that
  // is letting the execution go out of scope
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
  auto exec = int_execution::create_instance("queued_before_start");
  int_execution::bind_action_and_method(s->action, s, &sink::count, exec);
  untangle::async::execution_poll::get().add(*exec);

  s->action(1);
  exec->run();

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
  auto exec = int_execution::create_instance("queued_while_draining");
  int_execution::bind_action_and_method(s->action, s, &sink::count, exec);
  untangle::async::execution_poll::get().add(*exec);

  exec->start();
  for (int i = 0; i < queued; ++i) {
    s->action(i);
  }

  EXPECT_TRUE(wait_for([&s] { return s->calls.load() == queued; }, 2000ms));
  EXPECT_EQ(s->calls.load(), queued) << "actions were dropped between push_back and pop_front";

  exec->stop();
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
  auto exec = int_execution::create_instance("queued_after_stop");
  int_execution::bind_action_and_method(s->action, s, &sink::count, exec);
  untangle::async::execution_poll::get().add(*exec);

  exec->start();
  s->action(1);
  ASSERT_TRUE(wait_for([&s] { return s->calls.load() == 1; }, 2000ms)) << "the first action ran";

  exec->stop();
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
  // Deliberately leaked when it wedges: the worker is detached and still holds this object, so
  // destroying it would turn a reported failure into a use-after-free. The execution is
  // shared-owned like every other, so leaking means keeping an owner alive for the rest of the
  // process rather than dropping a raw pointer on the floor.
  static std::vector<std::shared_ptr<void_execution>> wedged;

  auto exec = void_execution::create_instance("stop_after_start");
  untangle::async::execution_poll::get().add(*exec);

  exec->start();
  exec->stop();

  const auto stopped = wait_until_poll_idle(2000ms);
  EXPECT_TRUE(stopped) << "the worker was still spinning 2s after stop()";

  if (!stopped) {
    wedged.push_back(std::move(exec));
  }
}

/**
 * @brief The poll does not report idle while an execution is still running.
 *
 * execution_poll is a singleton, and waiting on it is what the interface offers in place of a join,
 * so more than one thread waits on it as a matter of course. is_running() invokes its actuator,
 * which clears one shared results vector and refills it. Two callers therefore walk over each
 * other, and the loser iterates a vector the winner has just emptied and reports idle.
 *
 * The action is held open for the whole measurement, so the execution provably cannot finish while
 * the poll is being asked. That matters: a waiter that checks exec->is_running() and then asks the
 * poll has a time-of-check to time-of-use gap of its own, and an execution that finishes inside it
 * makes the poll's "idle" correct rather than wrong. Such a reading is legitimate and this test
 * must not count it, so the possibility is removed rather than tolerated. It can only ever happen
 * once per waiter in any case - the loop would exit straight after - which is why it never
 * accounted for the counts seen here.
 *
 * One waiter is the control, and reports 0. Two waiters report roughly 15% wrong in a Debug build,
 * where is_running() is slow enough to leave the results vector cleared for longer, and about
 * 0.005% at -O1. The rate is build dependent; the count is not, so the assertion is on the count.
 */
TEST(execution_poll, does_not_report_idle_while_an_execution_runs) {
  std::atomic_bool action_started = {false};
  std::atomic_bool release_action = {false};

  auto hold_until_released = [&action_started, &release_action] {
    action_started = true;
    while (!release_action) {
      std::this_thread::yield();
    }
  };

  auto exec = void_execution::create_instance("held_open");
  std::function<void(void)> action;
  void_execution::bind_action_and_function(action, hold_until_released, exec);

  action();
  untangle::async::execution_poll::get().add(*exec);
  exec->run();

  // Past this point the action is mid-flight and cannot return, so the execution is running for
  // every poll below and there is no check-then-use gap left to explain a wrong answer away.
  while (!action_started) {
    std::this_thread::yield();
  }

  constexpr auto polls_per_waiter = 200000;
  std::atomic_int idle_reports = {0};

  auto wait_on_the_poll = [&exec, &idle_reports] {
    for (auto i = 0; i < polls_per_waiter; ++i) {
      assert(exec->is_running());                                  //  time of check
      if (!untangle::async::execution_poll::get().is_running()) {  // time of use
        idle_reports.fetch_add(1, std::memory_order_relaxed);
      }
    }
  };

  std::thread first(wait_on_the_poll);
  std::thread second(wait_on_the_poll);
  first.join();
  second.join();

  // Read before releasing: if this is ever false the action returned early and the measurement
  // above means nothing.
  const auto was_running_throughout = exec->is_running();
  release_action = true;

  ASSERT_TRUE(was_running_throughout) << "the action finished early; the measurement is void";
  EXPECT_EQ(idle_reports.load(), 0)
      << "the poll reported idle " << idle_reports.load() << " times in " << 2 * polls_per_waiter
      << " polls, while the execution was running";
}

/**
 * @brief The poll survives executions registering and withdrawing while another thread waits on it.
 *
 * Every execution adds itself to the poll and withdraws in its destructor, and waiting on the poll
 * is what a caller does in the meantime. Neither side takes a lock, and add() move-assigns the
 * actuator whenever the poll was empty - out from under a thread walking its action list.
 *
 * This one does not return a wrong answer, it crashes: the walking thread follows a pointer into a
 * list that has just been replaced.
 */
TEST(execution_poll, survives_executions_registering_while_another_thread_waits) {
  std::atomic_bool done = {false};

  std::thread waiter([&done] {
    while (!done) {
      untangle::async::execution_poll::get().is_running();
    }
  });

  for (int i = 0; i < 200; ++i) {
    auto exec = void_execution::create_instance("churn");
    untangle::async::execution_poll::get().add(*exec);
  }

  done = true;
  waiter.join();

  SUCCEED() << "the poll was walked while executions registered and withdrew";
}

/**
 * @brief An action does not reach an execution that has been destroyed.
 *
 * Both bind() overloads hand the caller a lambda to store - here on the sink, whose lifetime has
 * nothing to do with the execution's. The sink outliving the execution is the ordinary shape rather
 * than a contrived one: an action is a member of the bound object, and the execution is typically a
 * local or a member somewhere else.
 *
 * The contract asserted here is the one actuator::bind already follows: the binding holds weak
 * ownership, and invoking it after its target has gone throws untangle::invalid_action rather than
 * touching freed memory. execute_actions() catches exactly that exception, so an action that comes
 * in late through the worker is dropped with a warning instead of ending the process.
 *
 * @remark The throw is asserted, not just the absence of a side effect. EXPECT_EQ(calls, 0) alone
 * would also pass against a broken header that pushed onto a destroyed list, because a queue no
 * worker is draining never runs anything either.
 */
TEST(execution_binding, an_action_does_not_reach_a_destroyed_execution) {
  auto s = std::make_shared<sink>();

  {
    auto exec = int_execution::create_instance("short_lived");
    int_execution::bind_action_and_method(s->action, s, &sink::count, exec);
  }  // the execution is gone; s->action still holds a binding to it

  EXPECT_THROW(s->action(1), untangle::invalid_action)
      << "an action outliving its execution must report a dead binding, not follow it";

  EXPECT_EQ(s->calls.load(), 0) << "an action bound to a destroyed execution appeared to run";
}

/**
 * @brief The plain-function overload holds the same reference, and must not follow it either.
 *
 * bind(T& Fn, execution&) holds the execution exactly as the method overload does, so a function
 * action outliving its execution must report the same way. Kept as its own case because the two
 * overloads are separate code paths: a fix applied to one and not the other would leave the header
 * half repaired and this suite still green.
 *
 * The action is declared outside the scope so that it, rather than the execution, is what survives.
 */
TEST(execution_binding, a_function_action_does_not_reach_a_destroyed_execution) {
  std::atomic_int calls = {0};
  auto count_a_call = [&calls] { calls.fetch_add(1, std::memory_order_relaxed); };

  std::function<void(void)> action;

  {
    auto exec = void_execution::create_instance("short_lived_function");
    void_execution::bind_action_and_function(action, count_a_call, exec);
  }  // the execution is gone; action still holds a binding to it

  EXPECT_THROW(action(), untangle::invalid_action)
      << "an action outliving its execution must report a dead binding, not follow it";

  EXPECT_EQ(calls.load(), 0) << "an action bound to a destroyed execution appeared to run";
}
