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
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

using namespace std::chrono_literals;

// The action types under test, named once: bind_action_and_*() are static, so every call has to
// qualify the execution type, and the full specialisation does not fit in a line.
using int_execution = untangle::async::execution<std::function<void(int)>>;
using void_execution = untangle::async::execution<std::function<void(void)>>;
using int_ret_execution = untangle::async::execution<std::function<int(int)>>;

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
 * @brief A refused action tells the caller, rather than only stderr.
 *
 * The case above pins that a refused action does not run. This one pins that the caller finds out.
 * add_action() prints `warning: execution '...' is stopped_, action not added` and returns void, so
 * there is nothing for a caller to check.
 *
 * @attention Measured 2026-09-17. On an execution<function<int(int)>>, a bound call that was queued
 * and really ran returned 0, and a bound call refused after stop() also returned 0 - the two are
 * indistinguishable. The accepted one had produced a result; results() held it.
 *
 * @remark **A bool, not an exception.** untangle::invalid_action means a binding whose target has
 * died - the action itself is broken. A refusal is not that: the action is perfectly good and the
 * execution is simply closed to new work. Throwing it here would make both a catch site and the
 * exception's own meaning ambiguous. The two cases stay separate answers to two separate questions.
 *
 * @remark **This case covers the direct caller only, and cannot cover the bound one.** The lambdas
 * from bind() return `actionT::result_type`, fixed by the specialisation - void here, int for an
 * int-returning execution - so a bool cannot be threaded back through them, and the plan's
 * instruction to do so is not implementable as written. A caller reaching add_action() through
 * bind() therefore stays untold, which is the half of item 8 carried forward from step 12 and is
 * the same ground step 28 has to cover for an action that throws.
 *
 * @remark The type is asserted rather than assumed so that this case **builds** against the current
 * header: decltype of a void call is well-formed, so the defect shows up as a failed expectation
 * instead of a compile error that would take the whole suite down with it. The behaviour below it
 * is guarded on the same condition, and starts testing once the return type is there to test.
 */
TEST(execution_queue, tells_the_caller_when_an_action_is_refused) {
  using answer_t = decltype(std::declval<int_execution&>().add_action(
      std::declval<std::function<void(int)>>(), 0));

  EXPECT_TRUE((std::is_same_v<answer_t, bool>))
      << "add_action() returns void, so a caller cannot learn that its action was refused";

  // In a templated lambda because a discarded `if constexpr` branch is still instantiated outside a
  // template: written directly in this function, the calls below would fail to compile against the
  // current void return and take the whole suite down with them.
  [&]<typename execT = int_execution>() {
    if constexpr (std::is_same_v<decltype(std::declval<execT&>().add_action(
                                     std::declval<std::function<void(int)>>(), 0)),
                                 bool>) {
      std::atomic_int ran = {0};
      const auto action = [&ran](int) { ran.fetch_add(1, std::memory_order_relaxed); };

      auto exec = execT::create_instance("refusal_is_reported");
      exec->start();

      EXPECT_TRUE(exec->add_action(action, 1))
          << "an action the worker went on to run was reported refused";

      ASSERT_TRUE(wait_for([&ran] { return ran.load() == 1; }, 2000ms))
          << "the first action never ran, so this case has not reached the refusal it is about";

      exec->stop();
      ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 5000ms))
          << "the worker did not stop";

      EXPECT_FALSE(exec->add_action(action, 2))
          << "an action refused by a stopped execution was reported accepted";

      EXPECT_EQ(ran.load(), 1) << "the refused action ran anyway";
    }
  }();
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
 * @brief An execution still running actions on its way out does not report itself finished.
 *
 * A guard, not a reproduction: it passes against the current header and has to keep passing.
 *
 * `finishing_` separates "my worker has finished" from "my worker is still touching this object",
 * and execute() shows where the line falls - async.hpp:686-696 runs the actions first, sets
 * `finishing_` only afterwards, and notifies last. So an execution executing actions reports
 * is_running() true, and reports false only once there is nothing left to run. loop() never sets
 * `finishing_` at all (step 30), and the obvious way to correct that - setting it at the break,
 * before the drain that follows - would break this rule: that drain still runs work, because
 * execute_actions() ends by driving the attacher's actuator. This case is what says so.
 *
 * @remark The sample is taken from an attached execution's action, run by the attacher's worker
 * during the final drain after the loop has broken. `trigger` arms `shutdown_probe` and then stops
 * the worker, both from the attacher's own thread, which leaves the probe's action for that drain.
 * The two are attached in this order on purpose: the actuator invokes in attachment order - a
 * std::list walked front to back - so `shutdown_probe` is passed over while still empty and is
 * armed only afterwards by `trigger`.
 *
 * @remark Reversed, or driven mid-loop, the answer is the same true, so this case does not pin
 * *where* the sample was taken and is not evidence about the shutdown window itself. Step 30's
 * defect has no black-box test - `finishing_` is read only by is_running(), and once it is set
 * correctly, at the end of the drain, nothing caller-written runs before `running_` is cleared.
 */
TEST(execution_lifecycle, an_execution_running_its_last_actions_does_not_report_itself_finished) {
  auto attacher = void_execution::create_instance("attacher");
  auto shutdown_probe = void_execution::create_instance("shutdown_probe");
  auto trigger = void_execution::create_instance("trigger");

  attacher->attach(shutdown_probe);
  attacher->attach(trigger);

  std::atomic_bool running_during_last_drain = {false};
  std::atomic_int sampled = {0};

  // Runs on the attacher's worker, in the last pass before the loop breaks. The probe is armed
  // before stop() because add_action() refuses once stopped, and attacher->stop() stops every
  // execution it has attached.
  trigger->add_action([&] {
    shutdown_probe->add_action([&] {
      running_during_last_drain = attacher->is_running();
      sampled.fetch_add(1, std::memory_order_relaxed);
    });

    attacher->stop();
  });

  attacher->start();

  ASSERT_TRUE(wait_for([&sampled] { return sampled.load() == 1; }, 2000ms))
      << "the drain after the break never reached the attached probe";

  ASSERT_TRUE(wait_for([&attacher] { return !attacher->is_running(); }, 2000ms))
      << "the worker did not finish";

  EXPECT_TRUE(running_during_last_drain.load())
      << "an execution reported itself finished while it was still running an action";
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

/**
 * @brief An attacher does not reach into an attached execution that has been destroyed.
 *
 * attach() hands the attacher's actuator a raw `&other.action_execute` - a pointer into the
 * attached object. There is no detach(), and ~execution() withdraws from execution_poll but not
 * from anything that attached it, so the attacher goes on holding that pointer after the target
 * is gone. Driving the attacher then dereferences it: actuator::operator() reads `*action` to test
 * the std::function for emptiness before invoking it, and that read lands in freed memory.
 *
 * The contract asserted here is the one step 4 applied to execution_poll and step 12 to bind():
 * an object that holds a pointer into another must learn when that other dies. A destroyed
 * attached execution must simply drop out of its attacher, leaving the rest of the attachment
 * list working.
 *
 * @attention Measured 2026-09-14. Under -DASYNC_SANITIZE=address this case fails 3/3, exit 134,
 * as `heap-use-after-free` - a READ of size 8 at actuator.hpp:134, the `!*action` emptiness test,
 * reached from execution::execute_actions() (async.hpp:410). That is the reproduction, and it is
 * deterministic.
 *
 * In a plain Debug build the same read is undefined rather than diagnosed, and it behaves like it:
 * 5 runs gave SIGSEGV, SIGBUS, clean, SIGBUS, clean. So this case does fail without a sanitizer,
 * but only about three times in five and as a crashed process rather than a reported expectation.
 * Configure with the sanitizer to see it named. The two expectations below are what must hold once
 * the dead entry is dropped; neither of them is what fails today.
 */
TEST(execution_attach, does_not_reach_an_attached_execution_that_has_been_destroyed) {
  auto attacher = void_execution::create_instance("attacher");
  auto survivor = void_execution::create_instance("survivor");

  // Held by shared_ptr so the counter outlives the execution whose action increments it; the
  // action must not run, and reading the count must not itself be a use-after-free.
  auto short_lived_ran = std::make_shared<std::atomic_int>(0);
  std::atomic_int survivor_ran = {0};

  {
    auto short_lived = void_execution::create_instance("short_lived");
    short_lived->add_action(
        [short_lived_ran] { short_lived_ran->fetch_add(1, std::memory_order_relaxed); });

    // Attached first, so it is the first entry the actuator walks: the dead entry has to be
    // stepped over for the survivor behind it to run at all.
    attacher->attach(short_lived);
  }  // short_lived is gone; attacher still points at its action_execute

  survivor->add_action([&survivor_ran] { survivor_ran.fetch_add(1, std::memory_order_relaxed); });
  attacher->attach(survivor);

  // action_execute is the public seam onto execute_actions(), which is what triggers the attached
  // executions. Driving it directly keeps the case synchronous - no worker, no waiting, and the
  // dangling read happens on this thread where the sanitizer attributes it to this line.
  attacher->action_execute();

  EXPECT_EQ(short_lived_ran->load(), 0)
      << "an action pending on a destroyed attached execution appeared to run";

  EXPECT_EQ(survivor_ran.load(), 1)
      << "a live attached execution must still be triggered past a destroyed one";
}

/**
 * @brief attach() refuses an attachment that would close a cycle.
 *
 * `a.attach(b); b.attach(a);` is accepted today, and driving either one recurses until the stack
 * is gone: execute_actions() triggers the attached action_execute, which is execute_actions() on
 * the other object, which triggers this one. Probed 2026-09-14 - SIGSEGV, exit 139 in a plain
 * Debug build, and `stack-overflow` under AddressSanitizer. Self-attachment is the same defect
 * with one object and fails the same way, exit 139.
 *
 * A cycle is a caller error at attach() time, not a run-time condition to be survived, so the
 * contract asserted is that attach() rejects it there - where the caller still has a stack to be
 * told on - rather than that the recursion is somehow bounded later.
 *
 * @remark The cycle is deliberately never driven. Today both calls succeed, so on failure this
 * case leaves two mutually attached executions behind; letting the worker or a direct
 * action_execute() reach them would replace a reported failure with a crashed test process.
 * ~execution() does not trigger the attachment, so returning from here is safe.
 *
 * @remark untangle::async::invalid_attachment is the type asserted, deliberately not
 * untangle::invalid_action: that one reports a binding whose target has died, and
 * execute_actions() swallows it by design. A cycle is a caller error and must not be swallowed.
 */
TEST(execution_attach, refuses_an_attach_that_would_close_a_cycle) {
  auto a = void_execution::create_instance("a");
  auto b = void_execution::create_instance("b");

  a->attach(b);  // the first direction is legitimate and must keep working

  EXPECT_THROW(b->attach(a), untangle::async::invalid_attachment)
      << "attaching a to b and b to a closes a cycle that recurses until the stack is gone";

  auto self = void_execution::create_instance("self");

  EXPECT_THROW(self->attach(self), untangle::async::invalid_attachment)
      << "an execution attached to itself is the same cycle with one object";
}

/**
 * @brief detach() is attach()'s inverse: a detached execution stops being triggered.
 *
 * attach() had no inverse at all, which is half of why a destroyed attached execution could not
 * get out of its attacher. An explicit detach() is the other half of that fix, and is the contract
 * a caller needs in its own right - an attachment that can only ever be added is a leak of
 * behaviour, not just of memory.
 *
 * The action is queued on the attached execution and never drained by a worker of its own, so the
 * only thing that can run it is the attacher reaching in. That makes the count a direct reading of
 * whether the attachment is still live.
 */
TEST(execution_attach, detach_stops_an_attached_execution_from_being_triggered) {
  auto attacher = void_execution::create_instance("attacher");
  auto attached = void_execution::create_instance("attached");

  std::atomic_int attached_ran = {0};
  attached->add_action([&attached_ran] { attached_ran.fetch_add(1, std::memory_order_relaxed); });

  attacher->attach(attached);

  EXPECT_TRUE(attacher->detach(*attached))
      << "detach() must report that it removed an attachment this execution actually held";

  attacher->action_execute();

  EXPECT_EQ(attached_ran.load(), 0) << "a detached execution was still triggered by its attacher";
}

/**
 * @brief detach() unwires the stop path too, not only the execute path.
 *
 * attach() wires two actuators - actuator_execute and actuator_stop - so an inverse that forgot
 * the second would leave the attacher still able to stop an execution it no longer drives. That
 * is observable without reaching into the header: stop() is final for an execution, because
 * add_action() refuses everything once stopped is set. So an execution that still accepts and runs
 * an action after its former attacher has stopped is one the stop did not reach.
 */
TEST(execution_attach, detach_unwires_the_stop_path_as_well) {
  auto attacher = void_execution::create_instance("attacher");
  auto attached = void_execution::create_instance("attached");

  attacher->attach(attached);
  attacher->detach(*attached);

  attacher->stop();  // would propagate to attached while the attachment stood

  std::atomic_int attached_ran = {0};
  attached->add_action([&attached_ran] { attached_ran.fetch_add(1, std::memory_order_relaxed); });
  attached->action_execute();

  EXPECT_EQ(attached_ran.load(), 1)
      << "a detached execution was stopped by its former attacher, so it refused the action";
}

/**
 * @brief Detaching an execution that was never attached is answered, not an error.
 *
 * The caller gets false rather than an exception: asking to remove an attachment that is not there
 * leaves exactly the state the caller wanted, so there is nothing to report as a failure. Stated
 * as its own case because it is the boundary an implementation is most likely to get wrong once
 * detach() starts erasing from the actuator's list.
 *
 * @remark This case passes against the stub, which returns false for everything. It is here to
 * pin the contract, not to reproduce the defect.
 */
TEST(execution_attach, detaching_an_execution_that_was_never_attached_reports_false) {
  auto attacher = void_execution::create_instance("attacher");
  auto stranger = void_execution::create_instance("stranger");

  EXPECT_FALSE(attacher->detach(*stranger))
      << "detach() claimed to have removed an attachment that was never made";
}

/**
 * @brief A chain of attachments is legitimate, and the whole of it runs.
 *
 * a -> b -> c is not a cycle and must keep working: nothing in the cycle check may refuse it, and
 * driving the head has to reach all the way down. execute_actions() runs this execution's own
 * actions and then triggers whatever is attached, so each link in turn drains its own queue - which
 * is what the two counts below read.
 *
 * This is the case the cycle check has to leave alone, so it is stated on its own rather than as a
 * setup step inside the refusal case.
 */
TEST(execution_attach, allows_a_chain_of_attached_executions) {
  auto a = void_execution::create_instance("a");
  auto b = void_execution::create_instance("b");
  auto c = void_execution::create_instance("c");

  a->attach(b);
  b->attach(c);

  std::atomic_int b_ran = {0};
  std::atomic_int c_ran = {0};
  b->add_action([&b_ran] { b_ran.fetch_add(1, std::memory_order_relaxed); });
  c->add_action([&c_ran] { c_ran.fetch_add(1, std::memory_order_relaxed); });

  a->action_execute();  // the head of the chain, driven once

  EXPECT_EQ(b_ran.load(), 1) << "the attached execution was not triggered by its attacher";
  EXPECT_EQ(c_ran.load(), 1) << "the chain stopped at the first link instead of running through";
}

/**
 * @brief Closing that chain into a cycle is refused.
 *
 * The two-object case is the one that is easy to spot by eye, and it was the only one caught while
 * attach() compared an execution against its own attacher and stopped there. A chain that comes
 * back round is the same defect and recurses the same way.
 *
 * Refusing an execution that already has an attacher leaves every execution with at most one, so
 * the attachment graph is a forest and the only cycle that can be built is one that attaches the
 * root of its own chain - every other ancestor is refused as attached already. attach() therefore
 * walks up the chain of attachers rather than looking one step back.
 *
 * @remark Three deep on purpose. With a -> b -> c, `c` attaching `a` has to walk past `b` to find
 * it, which a one-step check cannot do.
 */
TEST(execution_attach, refuses_a_cycle_that_closes_through_a_third_execution) {
  auto a = void_execution::create_instance("a");
  auto b = void_execution::create_instance("b");
  auto c = void_execution::create_instance("c");

  a->attach(b);
  b->attach(c);  // a -> b -> c, the chain the case above shows is legitimate

  EXPECT_THROW(c->attach(a), untangle::async::invalid_attachment)
      << "a -> b -> c -> a closes a cycle just as surely as a -> b -> a";

  // The chain itself must survive the refusal: a rejected attach may not leave anything half done.
  std::atomic_int c_ran = {0};
  c->add_action([&c_ran] { c_ran.fetch_add(1, std::memory_order_relaxed); });
  a->action_execute();

  EXPECT_EQ(c_ran.load(), 1) << "a refused attach disturbed an attachment that was already there";
}

/**
 * @brief An attached execution tells on_finished that its batch has drained.
 *
 * notify_finished() is called from execute() and from loop(), and an attached execution is in
 * neither: its actions are run straight out of execute_actions() by the attacher's worker. So its
 * on_finished never fires, however much work it does. Probed 2026-09-15: 0 firings across a full
 * run, driven both synchronously and by a started attacher.
 *
 * The rule the existing notification cases set is the one that has to hold here too - a batch that
 * drained is reported once, and a pass that ran nothing reports nothing. See
 * execution_notification.on_finished_fires_each_time_a_batch_drains, whose contract this extends to
 * the executions an attacher drives.
 *
 * @remark Driven by a real start()ed attacher rather than by action_execute() from this thread, as
 * the attach cases above it are. on_finished is signalled only from a worker thread, and for an
 * attached execution the worker in question is the attacher's - so the notification's thread is
 * part of the contract, not an incidental detail, and the case asserts it. A synchronous drive
 * would exercise the same code and prove the wrong thing about who sent the notification.
 *
 * @remark is_running() is deliberately not asserted here. It reports whether this execution's own
 * worker thread is alive - see its comment on async.hpp - and an attached execution has no worker,
 * so false is the right answer rather than the defect. on_finished is a different promise: it is
 * about a queue draining, and this queue drains.
 */
TEST(execution_attach, on_finished_fires_when_an_attached_batch_drains) {
  auto attacher = void_execution::create_instance("attacher");
  auto attached = void_execution::create_instance("attached");
  attacher->attach(attached);

  const auto test_thread = std::this_thread::get_id();

  std::atomic_int finished = {0};
  std::atomic_int ran = {0};
  std::atomic_bool signalled_off_this_thread = {false};

  attached->on_finished = [&] {
    signalled_off_this_thread = std::this_thread::get_id() != test_thread;
    finished.fetch_add(1, std::memory_order_relaxed);
  };

  attached->add_action([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });

  // A continuous worker: its loop drives the attachment on every pass, which is how an attached
  // execution is reached in practice.
  attacher->start();

  ASSERT_TRUE(wait_for([&ran] { return ran.load() == 1; }, 2000ms))
      << "the attacher's worker never ran the attached action";

  EXPECT_TRUE(wait_for([&finished] { return finished.load() == 1; }, 2000ms))
      << "an attached execution never told on_finished its batch drained";

  EXPECT_TRUE(signalled_off_this_thread.load())
      << "on_finished was not signalled from the attacher's worker";

  // The loop keeps passing over the attachment every 10ms with nothing left to run. A pass that
  // ran nothing has finished nothing, so none of them may report a batch.
  std::this_thread::sleep_for(200ms);

  EXPECT_EQ(finished.load(), 1) << "an empty pass over an attached execution reported a batch";

  attacher->stop();
  ASSERT_TRUE(wait_for([&attacher] { return !attacher->is_running(); }, 2000ms))
      << "the attacher's worker did not stop";
}

/**
 * @brief A fresh execution has no results, rather than one indeterminate value.
 *
 * `_result` was a bare `typename resultT::type` with no initialiser, and no constructor touched it,
 * so reading it before any action had run was undefined behaviour for every scalar result type -
 * and for the `int` that stands in when the action returns void. On the heap it read 0 and looked
 * innocent; built on a stack that had been written over first, it read back 0xabababab, 3 times out
 * of 3. An execution may be built on the stack: only bind() and attach() require shared ownership.
 *
 * A vector of results has no such state to read. That is the point of this case - not that the
 * value is now zero, but that there is no value until an action has produced one.
 */
TEST(execution_results, are_empty_before_any_action_runs) {
  const auto exec = int_ret_execution::create_instance("fresh");

  EXPECT_TRUE(exec->results().empty()) << "a fresh execution reported a result no action produced";
}

/**
 * @brief Every action's return value is kept, not just the last one.
 *
 * `_result` was a single value that each action overwrote, so queueing three actions lost two
 * return values: 1, 2 and 3 queued left `result()` reporting 3. This is the case that fails against
 * that header, and it is why the single value becomes a vector rather than gaining a lock.
 *
 * The actions are queued before run(), so the worker drains all three in one pass and the run is
 * over when it reports itself finished.
 */
TEST(execution_results, keep_every_action_result) {
  const auto exec = int_ret_execution::create_instance("three_actions");

  exec->add_action([](int value) { return value; }, 1);
  exec->add_action([](int value) { return value; }, 2);
  exec->add_action([](int value) { return value; }, 3);

  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";

  EXPECT_EQ(exec->results(), (std::vector<int>{1, 2, 3}))
      << "the results of a run must be every action's, in the order they ran";
}

/**
 * @brief on_finished is where a caller reads the results of a run.
 *
 * That is the contract this step is written to: run() collects, on_finished hands over. It fires
 * after the queue has drained and before the execution reports itself finished, so the results are
 * complete by the time the callback can see them - the assertion is on what the callback read, not
 * on what is readable afterwards.
 */
TEST(execution_results, are_available_to_on_finished) {
  const auto exec = int_ret_execution::create_instance("notifying");

  std::vector<int> seen_by_the_callback;
  exec->on_finished = [&exec, &seen_by_the_callback] { seen_by_the_callback = exec->results(); };

  exec->add_action([](int value) { return value; }, 7);
  exec->add_action([](int value) { return value; }, 8);

  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";

  EXPECT_EQ(seen_by_the_callback, (std::vector<int>{7, 8}))
      << "on_finished saw the wrong results, or none";
}

/**
 * @brief A run reports its own results, not those of the run before it.
 *
 * The results belong to one run, so the second run has to start from empty. Stated separately
 * because an implementation that only ever appends passes the case above and fails this one.
 */
TEST(execution_results, are_cleared_between_runs) {
  const auto exec = int_ret_execution::create_instance("twice");

  exec->add_action([](int value) { return value; }, 1);
  exec->run();
  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the first run did not finish";

  exec->add_action([](int value) { return value; }, 2);
  exec->run();
  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the second run did not finish";

  EXPECT_EQ(exec->results(), (std::vector<int>{2}))
      << "a run reported a result carried over from the run before it";
}

/**
 * @brief The continuous worker does not collect results, deliberately.
 *
 * start() has no point at which a run is over, so there is nothing to hand back and nowhere to
 * clear; collecting there would grow without bound. This is a decision rather than an oversight,
 * and it is pinned here so that it is changed on purpose if a use case ever wants it.
 */
TEST(execution_results, are_not_filled_by_the_continuous_worker) {
  const auto exec = int_ret_execution::create_instance("continuous");

  exec->start();
  exec->add_action([](int value) { return value; }, 1);
  exec->add_action([](int value) { return value; }, 2);
  exec->stop();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";

  EXPECT_TRUE(exec->results().empty())
      << "the continuous worker collected results; that is deferred until something wants them";
}

/**
 * @brief on_finished fires each time a batch of actions drains, not only after run().
 *
 * It was called from execute(), the one-shot path, and from nowhere else - so a caller driving the
 * execution with start() and stop() never heard from it. Probed 2026-09-14: fired 0 times out of 3
 * runs in that mode, against 1 out of 1 for run().
 *
 * "Finished" means the queue this worker was given has drained, which is the signal a caller needs
 * to fire the next batch once the previous one is done. It is therefore per batch and not per
 * worker: a callback that only arrived when the worker ended would be useless for that, since
 * getting it would mean calling stop() and having no worker left to fire the next batch at.
 *
 * The batches are kept one pass apart on purpose. execute_actions() drains everything queued in a
 * single call, so the two actions queued before start() are one batch and one notification; the
 * third, queued after that notification arrives, is a second batch.
 */
TEST(execution_notification, on_finished_fires_each_time_a_batch_drains) {
  const auto exec = void_execution::create_instance("continuous");

  std::atomic_int fired = {0};
  exec->on_finished = [&fired] { fired.fetch_add(1, std::memory_order_relaxed); };

  exec->add_action([] {});
  exec->add_action([] {});
  exec->start();

  ASSERT_TRUE(wait_for([&fired] { return fired.load() == 1; }, 2000ms))
      << "the first batch drained without reporting that it had finished";

  exec->add_action([] {});

  ASSERT_TRUE(wait_for([&fired] { return fired.load() == 2; }, 2000ms))
      << "a second batch drained without reporting that it had finished";

  exec->stop();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";

  EXPECT_EQ(fired.load(), 2) << "stopping an already drained execution reported a third batch";
}

/**
 * @brief An idle worker reports nothing.
 *
 * loop() does not wait on the condition variable indefinitely - it wakes every 10ms so that it can
 * look in on attached executions - and it calls execute_actions() on every pass whether or not
 * anything was queued. A notification per pass would therefore arrive about a hundred times a
 * second on a completely idle execution, each one reporting that nothing had finished.
 *
 * 200ms is twenty of those passes, which is enough for the difference to be unmistakable.
 */
TEST(execution_notification, an_idle_worker_reports_nothing) {
  const auto exec = void_execution::create_instance("idle");

  std::atomic_int fired = {0};
  exec->on_finished = [&fired] { fired.fetch_add(1, std::memory_order_relaxed); };

  exec->start();
  std::this_thread::sleep_for(200ms);
  exec->stop();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";

  EXPECT_EQ(fired.load(), 0) << "an idle worker reported batches that never existed";
}

/**
 * @brief run() reports finished exactly once.
 *
 * The one-shot path drains once and leaves, so per-batch and per-worker are the same thing here.
 * Stated so that the cases above are telling us about the continuous worker rather than about a
 * callback that never fires anywhere.
 */
TEST(execution_notification, on_finished_fires_once_per_run) {
  const auto exec = void_execution::create_instance("one_shot");

  std::atomic_int fired = {0};
  exec->on_finished = [&fired] { fired.fetch_add(1, std::memory_order_relaxed); };

  exec->add_action([] {});
  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";

  EXPECT_EQ(fired.load(), 1) << "a run reported finished more than once, or not at all";
}

/**
 * @brief run()'s callback is told the execution is still running, because it is.
 *
 * on_finished is raised from inside the drain, on the worker's own thread, at the moment the list
 * empties. The worker has not finished at that point - it is standing in the callback, and has the
 * rest of execute() still to do - so is_running() answering true is the honest answer rather than a
 * stale one. A callback is not a place to ask whether the work is over; it *is* the notification
 * that the batch is over.
 *
 * @remark This case asserted the opposite until 2026-09-17, when the notification moved into
 * execute_actions() so that an attached execution could be told its own batch had drained (item 12
 * / step 17). The old contract came from step 16 and was written when `finishing_` was set before
 * the callback; the callback is now raised earlier than that store, and the ordering it described
 * no longer exists to be tested.
 *
 * @remark `finishing_` still does its other job, which is the one it was really introduced for:
 * ~execution() waits on `running_` and may free the object the moment that reads false, so a
 * caller polling is_running() needs an answer that goes false before the lifetime handshake does.
 * That is untouched here.
 *
 * With this, both worker paths say the same thing, and this case and the one below it are two
 * halves of one contract rather than opposites - see
 * execution_notification.a_drained_batch_does_not_claim_the_worker_stopped.
 */
TEST(execution_notification, a_run_batch_does_not_claim_the_worker_stopped) {
  const auto exec = void_execution::create_instance("one_shot");

  std::atomic_bool seen_running = {false};
  std::atomic_int fired = {0};
  exec->on_finished = [&exec, &seen_running, &fired] {
    seen_running = exec->is_running();
    fired.fetch_add(1, std::memory_order_relaxed);
  };

  exec->add_action([] {});
  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";

  ASSERT_EQ(fired.load(), 1) << "the batch drained without reporting that it had finished";

  EXPECT_TRUE(seen_running.load())
      << "run()'s on_finished was told the worker had stopped while standing in the callback";

  // The other half, and what `finishing_` is still for: once the worker is past the drain, the
  // execution reports finished without waiting for the lifetime handshake.
  EXPECT_FALSE(exec->is_running()) << "the execution still reported running after its run was over";
}

/**
 * @brief A drained batch does not claim the worker has stopped.
 *
 * The counterpart of the case above, and since 2026-09-17 the same contract seen from the
 * continuous side: a drained batch says nothing about the worker, which is still there and waiting
 * for the next one. is_running() must keep saying so, or a caller waiting for the execution to
 * finish would be told it had, mid-life. The two cases are kept apart because the paths are - one
 * worker leaves after its batch and the other does not - not because they disagree.
 */
TEST(execution_notification, a_drained_batch_does_not_claim_the_worker_stopped) {
  const auto exec = void_execution::create_instance("continuous");

  std::atomic_bool seen_running = {false};
  std::atomic_int fired = {0};
  exec->on_finished = [&exec, &seen_running, &fired] {
    seen_running = exec->is_running();
    fired.fetch_add(1, std::memory_order_relaxed);
  };

  exec->add_action([] {});
  exec->start();

  ASSERT_TRUE(wait_for([&fired] { return fired.load() == 1; }, 2000ms))
      << "the batch drained without reporting that it had finished";

  EXPECT_TRUE(seen_running.load())
      << "a drained batch reported the worker stopped while it was still running";

  exec->stop();
  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";
}

/**
 * @brief A fresh execution has nothing to do.
 */
TEST(execution_busy, a_fresh_execution_is_not_busy) {
  const auto exec = void_execution::create_instance("fresh");

  EXPECT_FALSE(exec->is_busy()) << "an execution with nothing queued reported itself busy";
}

/**
 * @brief A queued action makes an execution busy before any worker has touched it.
 *
 * add_action() can be called before run() or start(), so "busy" cannot mean "a worker is running".
 * It means there is work outstanding.
 */
TEST(execution_busy, a_queued_action_makes_an_execution_busy) {
  const auto exec = void_execution::create_instance("queued");

  exec->add_action([] {});

  EXPECT_TRUE(exec->is_busy()) << "an execution with an action queued reported itself idle";
}

/**
 * @brief An execution is busy while an action is running, not only while one is queued.
 *
 * This is the case that makes is_busy() more than a test for an empty list. The worker pops an
 * action under the lock and runs it with the lock released, so between those two the list is empty
 * and the execution is anything but idle. An implementation that only asked whether the list was
 * empty would report this execution free and invite a caller to hand it more work.
 *
 * The action is held open until the assertion has been made, so the reading cannot be a race
 * against the action finishing early.
 */
TEST(execution_busy, an_execution_is_busy_while_its_last_action_runs) {
  const auto exec = void_execution::create_instance("running");

  std::atomic_bool action_started = {false};
  std::atomic_bool may_finish = {false};

  exec->add_action([&action_started, &may_finish] {
    action_started = true;
    while (!may_finish.load()) {
      std::this_thread::sleep_for(1ms);
    }
  });

  exec->start();

  ASSERT_TRUE(wait_for([&action_started] { return action_started.load(); }, 2000ms))
      << "the action never started";

  // The list is empty by now - the worker took the only action off it - and the action is still
  // running, which is exactly the window under test.
  EXPECT_TRUE(exec->is_busy()) << "an execution running its last action reported itself idle";

  may_finish = true;
  exec->stop();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";
}

/**
 * @brief An execution that has drained is free again.
 *
 * The counterpart of the case above: busy has to become false on its own, or a caller asking
 * whether a worker has room would never be told yes twice.
 */
TEST(execution_busy, an_execution_is_not_busy_once_its_actions_have_run) {
  const auto exec = void_execution::create_instance("drained");

  std::atomic_int ran = {0};
  exec->add_action([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });

  exec->start();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_busy(); }, 2000ms))
      << "an execution stayed busy after its actions had run";

  EXPECT_EQ(ran.load(), 1) << "the action did not run";

  exec->stop();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";
}

/**
 * @brief A continuous worker is running whether or not it is busy.
 *
 * The two are different questions, and this is the case that says so. is_running() is about the
 * worker thread and stays true from start() until after stop(); is_busy() is about the work. An
 * execution that conflated them could not be asked whether it had room for more, which is the
 * question a pool of executions has to ask.
 */
TEST(execution_busy, a_continuous_worker_runs_while_idle) {
  const auto exec = void_execution::create_instance("idle_but_running");

  exec->start();

  ASSERT_TRUE(wait_for([&exec] { return exec->is_running(); }, 2000ms))
      << "the worker never started";

  EXPECT_FALSE(exec->is_busy()) << "a worker with nothing queued reported itself busy";
  EXPECT_TRUE(exec->is_running()) << "a started worker reported itself not running";

  exec->stop();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 2000ms))
      << "the worker did not finish";
}

/**
 * @brief An execution cannot be copied or moved, and says so itself.
 *
 * @attention **This case does not reproduce item B, and no case can.** It is a guard. The defect is
 * a compile-time one whose whole effect is on the diagnostic, and the four assertions below read
 * the same before and after the fix - measured 2026-09-17, all four false either way. They are
 * recorded here so the next person does not go looking for a reproduction that cannot exist.
 *
 * What item B actually is: `execution` has a user-declared destructor since step 3, which
 * suppresses the implicit move constructor and move assignment. The copy operations are still
 * declared, and deleted by the members - std::mutex, std::thread, std::atomic_bool. So a caller who
 * writes a move gets overload resolution falling back to the copy constructor, and is told:
 *
 *     error: call to implicitly-deleted copy constructor of 'void_execution'
 *     note: copy constructor is implicitly deleted because field 'this_thread_' has an
 *           inaccessible copy constructor
 *
 * A plain copy produces the identical message, so the two cases cannot be told apart.
 *
 * **The rule of five was measured and declined**, on 2026-09-17. Declaring the four `= delete` does
 * improve that one message to `call to deleted constructor` - the right operation, named - but it
 * leaves `std::vector<execution>` at 48 lines of template diagnostic either way, because that one
 * fails inside allocator_traits regardless. And it would not be load-bearing: the copy operations
 * are deleted several times over, by std::thread, by every atomic, by both mutexes and by the
 * condition variable, each on its own. Removing the std::thread from a scratch header changed
 * nothing - `started_` is blamed next, and all four traits stay false. Four declarations restating
 * what a dozen members already enforce were judged not worth their place; see the remark on
 * ~execution().
 *
 * What this guard is for, then: it is the only executable statement of the intent. An execution
 * that could be copied would duplicate `other_this_`, its registration in execution_poll, and the
 * pointers an attacher holds into it, and nothing in the class says so out loud - the members
 * merely happen to prevent it. These assertions say it, and would fail if that ever stopped being
 * true.
 */
TEST(execution_special_members, cannot_be_copied_or_moved) {
  static_assert(!std::is_copy_constructible_v<void_execution>,
                "an execution must not be copyable: copying would duplicate other_this_, its poll "
                "registration, and the pointers an attacher holds into it");
  static_assert(!std::is_copy_assignable_v<void_execution>,
                "an execution must not be copy-assignable, for the same reason");
  static_assert(!std::is_move_constructible_v<void_execution>,
                "an execution must not be movable: other_this_, execution_poll and attach() all "
                "hold pointers to it that a move would leave behind");
  static_assert(!std::is_move_assignable_v<void_execution>,
                "an execution must not be move-assignable, for the same reason");

  // The traits above are the whole of this case; it has no runtime behaviour to check. Asserted so
  // that the case reports as run rather than as empty.
  SUCCEED() << "checked at compile time";
}
