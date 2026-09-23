// Copyright (c) 2018 Nicolae Popescu. MIT License.

/**
 * @brief Behaviour tests for untangle::async::execution.
 *
 * Unlike async_smoke_test.cpp, which drives the interface by hand and prints what happened, every
 * case here states an expectation and fails when it does not hold.
 *
 * @remark A few cases are sanitizer-sensitive and say so. Configure with -DASYNC_SANITIZE=thread or
 * =address to run them where a race or a dangling read is named rather than inferred.
 */
#include <gtest/gtest.h>

#include <async.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
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
 * @brief Counts how many times its bound method was invoked.
 *
 * The counter is atomic, so the queue under test is the only unsynchronised thing here.
 */
struct sink {
  void count(int) { calls.fetch_add(1, std::memory_order_relaxed); }

  std::atomic_int calls = {0};
  std::function<void(int)> action;
};

/**
 * @brief Waits for the executions added to \p poll to finish, the way async_smoke_test.cpp does.
 *
 * A detached worker goes on reading its execution, so returning from a case while one is still
 * running would replace a reported failure with a use-after-free. An execution is not joinable, so
 * the poll is what stands in for a join.
 *
 * @return true - the poll reported idle before the limit elapsed.
 */
bool wait_until_poll_idle(untangle::async::execution_poll& poll, std::chrono::milliseconds limit) {
  // time of check; the time of use is the caller's next statement, and for every caller here that
  // is letting the execution go out of scope
  return wait_for([&poll] { return !poll.is_running(); }, limit);
}

/**
 * @brief Counts the copies made of it, so the queue's own copying is measurable.
 *
 * The move constructor is declared because declaring the copy constructor would otherwise suppress
 * it, and every move would be reported as a copy.
 */
struct copy_counter {
  copy_counter() = default;
  copy_counter(const copy_counter&) { copies.fetch_add(1, std::memory_order_relaxed); }
  copy_counter(copy_counter&&) noexcept = default;

  static inline std::atomic_int copies = {0};
};

/**
 * @brief The same, for the action itself: a callable the std::function carries its copies with.
 *
 * @attention The copy constructor must stay non-noexcept. libc++ keeps a nothrow-copyable target in
 * std::function's small buffer and copy-constructs it on every relocation, which turns the one copy
 * counted below into three - a fact about the counter, not about add_action().
 */
struct counting_action {
  counting_action() = default;
  counting_action(const counting_action&) { copies.fetch_add(1, std::memory_order_relaxed); }
  counting_action(counting_action&&) noexcept = default;

  void operator()(const copy_counter&) const {}

  static inline std::atomic_int copies = {0};
};

//! A method to bind, so the copies the bound lambda makes can be counted too.
struct counting_target {
  void take(const copy_counter&) {}
};

using counting_action_t = std::function<void(const copy_counter&)>;
using counting_execution = untangle::async::execution<counting_action_t>;

}  // namespace

/**
 * @brief An action the caller keeps is copied once on its way to the queue, and so is its argument.
 *
 * The queue owns what it is given, so one copy each is the price of queueing; a second is not. The
 * action is queued and never run - running it would copy for its own reasons.
 */
TEST(execution_queue, queueing_copies_the_action_and_its_arguments_once) {
  auto exec = counting_execution::create_instance("copies");

  const copy_counter argument;
  counting_action_t action = counting_action{};

  // Counted as a delta rather than from zero: the counters are static, and running this binary
  // directly puts every case in one process.
  const auto actions_before = counting_action::copies.load();
  const auto arguments_before = copy_counter::copies.load();

  EXPECT_TRUE(exec->add_action(action, argument));

  EXPECT_EQ(counting_action::copies.load() - actions_before, 1)
      << "the action was copied " << counting_action::copies.load() - actions_before
      << " times on its way to the queue";
  EXPECT_EQ(copy_counter::copies.load() - arguments_before, 1)
      << "the argument was copied " << copy_counter::copies.load() - arguments_before
      << " times on its way to the queue";
}

/**
 * @brief An action the caller gives up is not copied at all.
 *
 * add_action() takes the action by value, so std::move() at the call site and a temporary - the
 * shape every lambda arrives in - both reach the queue without a copy.
 */
TEST(execution_queue, queueing_an_action_the_caller_gives_up_copies_it_not_at_all) {
  auto exec = counting_execution::create_instance("given_up");

  const copy_counter argument;
  counting_action_t action = counting_action{};

  const auto moved_before = counting_action::copies.load();
  EXPECT_TRUE(exec->add_action(std::move(action), argument));
  EXPECT_EQ(counting_action::copies.load() - moved_before, 0)
      << "an action moved into add_action() was copied "
      << counting_action::copies.load() - moved_before << " times";

  // A temporary is the same shape, and is how a lambda arrives.
  const auto temporary_before = counting_action::copies.load();
  EXPECT_TRUE(exec->add_action(counting_action{}, argument));
  EXPECT_EQ(counting_action::copies.load() - temporary_before, 0)
      << "a temporary action was copied " << counting_action::copies.load() - temporary_before
      << " times";
}

/**
 * @brief An argument passed through a binding is copied once, not twice.
 *
 * The binding forwards its arguments to add_action() rather than taking them by value, so the only
 * copy left is the queue's own.
 */
TEST(execution_queue, queueing_through_a_binding_copies_the_argument_once) {
  auto target = std::make_shared<counting_target>();
  auto exec = counting_execution::create_instance("bound_copies");

  counting_action_t bound;
  counting_execution::bind_action_and_method(bound, target, &counting_target::take, exec);

  const copy_counter argument;
  const auto arguments_before = copy_counter::copies.load();

  bound(argument);

  EXPECT_EQ(copy_counter::copies.load() - arguments_before, 1)
      << "the argument was copied " << copy_counter::copies.load() - arguments_before
      << " times on its way through the binding";
}

/**
 * @brief An action queued before run() runs exactly once.
 *
 * The simplest path through the queue, and the control that tells the cases below apart from a
 * broken test.
 */
TEST(execution_queue, runs_an_action_queued_before_the_worker_starts) {
  auto s = std::make_shared<sink>();
  untangle::async::execution_poll poll;
  auto exec = int_execution::create_instance("queued_before_start");
  int_execution::bind_action_and_method(s->action, s, &sink::count, exec);
  poll.add(*exec);

  s->action(1);
  exec->run();

  EXPECT_TRUE(wait_for([&s] { return s->calls.load() == 1; }, 2000ms));
  EXPECT_EQ(s->calls.load(), 1);

  ASSERT_TRUE(wait_until_poll_idle(poll, 5000ms))
      << "the worker was still running 5s later; the object cannot be destroyed safely";
}

/**
 * @brief Every action queued while the worker drains runs.
 *
 * A thousand actions pushed from this thread while the worker pops them, which is what the class
 * exists to do. A short count means a push was lost against a concurrent pop.
 */
TEST(execution_queue, runs_every_action_queued_while_the_worker_drains) {
  constexpr int queued = 1000;

  auto s = std::make_shared<sink>();
  untangle::async::execution_poll poll;
  auto exec = int_execution::create_instance("queued_while_draining");
  int_execution::bind_action_and_method(s->action, s, &sink::count, exec);
  poll.add(*exec);

  exec->start();
  for (int i = 0; i < queued; ++i) {
    s->action(i);
  }

  EXPECT_TRUE(wait_for([&s] { return s->calls.load() == queued; }, 2000ms));
  EXPECT_EQ(s->calls.load(), queued) << "actions were dropped between push_back and pop_front";

  exec->stop();
  ASSERT_TRUE(wait_until_poll_idle(poll, 5000ms))
      << "the worker was still running 5s after stop(); the object cannot be destroyed safely";
}

/**
 * @brief Every action queued while a batch is running runs, and runs exactly once.
 *
 * The worker cannot hold action_mutex_ across a call - an action may queue another - so it runs
 * with the lock released, and whatever holds the pending actions is being written to by the caller
 * while it does. The drain has to take the batch out of that shared object before it walks it.
 *
 * @remark Sanitizer-sensitive. The counts below hold even for a drain that walks the shared list,
 * because the window is narrow; -DASYNC_SANITIZE=thread is what names the race instead.
 */
TEST(execution_queue, queueing_during_a_batch_runs_every_action_exactly_once) {
  constexpr int queued = 200;

  // Value-initialised in place: an atomic is neither copyable nor movable, so the count is the only
  // thing the vector is ever asked to do.
  std::vector<std::atomic_int> runs(queued);

  untangle::async::execution_poll poll;
  const auto exec = void_execution::create_instance("queued_during_a_batch");
  poll.add(*exec);

  exec->start();

  // Each action outlasts the gap before the next one is queued, so the worker is inside a batch
  // while the rest arrive - which is the window the drain has to be safe in.
  for (int i = 0; i < queued; ++i) {
    ASSERT_TRUE(exec->add_action([&runs, i] {
      runs[i].fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(200us);
    })) << "the action was refused at "
        << i;
    std::this_thread::sleep_for(100us);
  }

  const auto all_ran = [&runs] {
    for (const auto& count : runs) {
      if (count.load(std::memory_order_relaxed) == 0) {
        return false;
      }
    }
    return true;
  };

  EXPECT_TRUE(wait_for(all_ran, 5000ms)) << "an action queued while a batch was running never ran";

  for (int i = 0; i < queued; ++i) {
    EXPECT_EQ(runs[i].load(std::memory_order_relaxed), 1) << "action " << i << " did not run once";
  }

  exec->stop();
  ASSERT_TRUE(wait_until_poll_idle(poll, 5000ms))
      << "the worker was still running 5s after stop(); the object cannot be destroyed safely";
}

/**
 * @brief A continuous worker picks an action up when it is queued, not when its wait times out.
 *
 * loop() waits on a condition variable with a bounded timeout, so a worker that is never notified
 * still runs everything - late. Two hundred rounds of "queue one, wait for it" is where the
 * difference shows: woken, each round costs the call itself; polled, each costs the timeout.
 */
TEST(execution_queue, a_continuous_worker_is_woken_by_an_add_rather_than_by_its_timeout) {
  constexpr int rounds = 200;

  untangle::async::execution_poll poll;
  const auto exec = void_execution::create_instance("woken_by_add");
  poll.add(*exec);

  std::atomic_int ran = {0};
  exec->start();

  const auto started = std::chrono::steady_clock::now();

  for (int round = 0; round < rounds; ++round) {
    ASSERT_TRUE(exec->add_action([&ran] { ran.fetch_add(1, std::memory_order_relaxed); }))
        << "the action was refused at round " << round;
    ASSERT_TRUE(
        wait_for([&ran, round] { return ran.load(std::memory_order_relaxed) > round; }, 2000ms))
        << "round " << round << " never ran";
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  EXPECT_LT(elapsed, 1000ms) << rounds << " rounds took " << elapsed.count()
                             << "ms; the worker is waiting out its timeout rather than being woken";

  exec->stop();
  ASSERT_TRUE(wait_until_poll_idle(poll, 5000ms))
      << "the worker was still running 5s after stop(); the object cannot be destroyed safely";
}

/**
 * @brief An action queued after stop() never runs.
 *
 * stop() ends the execution's working life: the worker drains what was queued before it and leaves,
 * so anything offered afterwards is dropped rather than left in the queue looking pending.
 */
TEST(execution_queue, refuses_an_action_queued_after_the_worker_stops) {
  auto s = std::make_shared<sink>();
  untangle::async::execution_poll poll;
  auto exec = int_execution::create_instance("queued_after_stop");
  int_execution::bind_action_and_method(s->action, s, &sink::count, exec);
  poll.add(*exec);

  exec->start();
  s->action(1);
  ASSERT_TRUE(wait_for([&s] { return s->calls.load() == 1; }, 2000ms)) << "the first action ran";

  exec->stop();
  s->action(2);

  ASSERT_TRUE(wait_until_poll_idle(poll, 5000ms))
      << "the worker was still running 5s after stop(); the object cannot be destroyed safely";

  // The worker has left, so the count is final rather than merely not there yet.
  EXPECT_EQ(s->calls.load(), 1) << "an action queued after stop() ran anyway";
}

/**
 * @brief add_action() answers the caller: true when it queued the action, false when it refused.
 *
 * A refusal is a return value rather than an exception - untangle::invalid_action means a binding
 * whose target has died, which is a different question - and rather than only a line on stderr.
 *
 * @remark The direct caller only. The lambdas from bind() return actionT::result_type, which has no
 * room for an answer, so a caller arriving that way is still not told.
 *
 * @remark The return type is asserted first and the behaviour checked inside a templated lambda, so
 * that a header returning void fails this case instead of failing to compile the suite.
 */
TEST(execution_queue, tells_the_caller_when_an_action_is_refused) {
  using answer_t = decltype(std::declval<int_execution&>().add_action(
      std::declval<std::function<void(int)>>(), 0));

  EXPECT_TRUE((std::is_same_v<answer_t, bool>))
      << "add_action() returns void, so a caller cannot learn that its action was refused";

  // In a templated lambda because a discarded `if constexpr` branch is still instantiated outside
  // a template, and these calls must not stop the suite compiling if the answer goes away.
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
 * @brief An action whose own body throws does not take the worker with it.
 *
 * The worker is a detached thread function, so an exception escaping it would call std::terminate.
 * The throwing action is dropped with a warning naming the execution, the actions before and behind
 * it still run, and the batch still reports that it drained.
 */
TEST(execution_queue, an_action_that_throws_does_not_kill_the_worker) {
  auto exec = void_execution::create_instance("throwing_action");

  std::atomic_int before = {0};
  std::atomic_int behind = {0};
  std::atomic_int finished = {0};
  exec->on_finished = [&finished] { finished.fetch_add(1, std::memory_order_relaxed); };

  exec->add_action([&before] { before.fetch_add(1, std::memory_order_relaxed); });
  exec->add_action([] { throw std::runtime_error("the caller's own code threw"); });
  exec->add_action([&behind] { behind.fetch_add(1, std::memory_order_relaxed); });

  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 5000ms))
      << "the worker did not finish";

  EXPECT_EQ(before.load(), 1) << "the action before the throwing one did not run";
  EXPECT_EQ(behind.load(), 1) << "an action queued behind a throwing one never ran";
  EXPECT_EQ(finished.load(), 1) << "the batch drained without reporting that it had finished";
}

/**
 * @brief An action that throws is dropped, not retried: it runs once and once only.
 *
 * The action is counted before it throws, so the count is of attempts rather than of completions.
 * A drain that cannot tell a batch it has already run from one it has not would run it again on
 * the next pass, and go on doing so.
 */
TEST(execution_queue, a_throwing_action_runs_once) {
  auto exec = void_execution::create_instance("throwing_action_once");

  std::atomic_int attempts = {0};
  std::atomic_int finished = {0};
  exec->on_finished = [&finished] { finished.fetch_add(1, std::memory_order_relaxed); };

  exec->add_action([&attempts] {
    attempts.fetch_add(1, std::memory_order_relaxed);
    throw std::runtime_error("the caller's own code threw");
  });

  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 5000ms))
      << "the worker did not finish";

  EXPECT_EQ(attempts.load(), 1) << "the throwing action was attempted more than once";
  EXPECT_EQ(finished.load(), 1) << "the batch drained without reporting that it had finished";
}

/**
 * @brief What an action throws reaches the caller, and not only stderr.
 *
 * The warning names the execution, which is the header's own name for its worker and nothing the
 * caller chose. It reaches a log and no code: an action that failed and one that succeeded are the
 * same from outside. on_error carries what was thrown to whoever assigned it, the way on_finished
 * carries the end of a batch.
 *
 * @remark Both arms are read. The header catches a std::exception separately so its warning can
 * carry what(), and everything else in a catch-all; a handler told about only the first would leave
 * the second as silent as it is now.
 */
TEST(execution_queue, what_an_action_throws_reaches_the_caller) {
  auto exec = void_execution::create_instance("throwing_action_reported");

  std::atomic_int reported = {0};
  std::atomic_int reported_unknown = {0};
  std::string reported_what;

  exec->on_error = [&](std::exception_ptr thrown) {
    try {
      std::rethrow_exception(thrown);
    } catch (const std::exception& e) {
      reported_what = e.what();
    } catch (...) {
      reported_unknown.fetch_add(1, std::memory_order_relaxed);
    }

    reported.fetch_add(1, std::memory_order_relaxed);
  };

  exec->add_action([] { throw std::runtime_error("the caller's own code threw"); });
  exec->add_action([] { throw 42; });  // not a std::exception; the catch-all arm

  testing::internal::CaptureStderr();

  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 5000ms))
      << "the worker did not finish";

  const std::string warned = testing::internal::GetCapturedStderr();

  EXPECT_EQ(reported.load(), 2) << "what the actions threw reached nobody";
  EXPECT_EQ(reported_what, "the caller's own code threw");
  EXPECT_EQ(reported_unknown.load(), 1) << "the catch-all arm reported nothing";
  EXPECT_EQ(warned, "") << "a handler was assigned, so the warning is its job now; stderr held: "
                        << warned;
}

/**
 * @brief stop() ends a worker that has only just been started.
 *
 * The worker sets `started_` from its own thread and stop() clears it from the caller's, so a
 * stop() that lands first must not be overwritten by a worker still coming up.
 *
 * @remark The execution is leaked deliberately if it wedges: the detached worker still holds it, so
 * destroying it would turn a reported failure into a use-after-free.
 */
TEST(execution_lifecycle, stop_ends_a_worker_that_just_started) {
  // Kept alive when it wedges: the detached worker still holds this object, so destroying it would
  // turn a reported failure into a use-after-free.
  static std::vector<std::shared_ptr<void_execution>> wedged;

  untangle::async::execution_poll poll;
  auto exec = void_execution::create_instance("stop_after_start");
  poll.add(*exec);

  exec->start();
  exec->stop();

  const auto stopped = wait_until_poll_idle(poll, 2000ms);
  EXPECT_TRUE(stopped) << "the worker was still spinning 2s after stop()";

  if (!stopped) {
    wedged.push_back(std::move(exec));
  }
}

/**
 * @brief An execution still running actions does not report itself finished.
 *
 * A guard, not a reproduction. The sample is taken from an attached execution's action during the
 * attacher's final drain, after its loop has broken - that drain still runs work, because
 * execute_actions() ends by driving the attachments.
 *
 * @remark The two are attached in this order on purpose: the actuator invokes in attachment order,
 * so shutdown_probe is passed over while still empty and armed only afterwards by trigger.
 */
TEST(execution_lifecycle, an_execution_running_its_last_actions_does_not_report_itself_finished) {
  auto attacher = void_execution::create_instance("attacher");
  auto shutdown_probe = void_execution::create_instance("shutdown_probe");
  auto trigger = void_execution::create_instance("trigger");

  attacher->attach(shutdown_probe);
  attacher->attach(trigger);

  std::atomic_bool running_during_last_drain = {false};
  std::atomic_int sampled = {0};

  // Armed before stop(), because add_action() refuses once stopped and stopping the attacher stops
  // everything it has attached.
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
 * @brief A second run() does not leave two workers inside one execution.
 *
 * run() takes `running_`, which is also the handshake ~execution() waits on. Two workers sharing it
 * would let the first to finish satisfy that wait while the second was still inside the object.
 * Twenty double runs, each let go at once so the destructor meets both.
 *
 * @attention Sanitizer-sensitive: this aborts rather than fails when the defect is present, and a
 * plain Debug build reports nothing either way.
 */
TEST(execution_lifecycle, a_second_run_does_not_leave_two_workers_in_one_execution) {
  // Repeated, because the window is the instant between one worker clearing `running_` and the
  // other leaving; one pass can miss it and twenty do not.
  for (auto i = 0; i < 20; ++i) {
    auto exec = void_execution::create_instance("twice_over");
    exec->add_action([] { std::this_thread::sleep_for(2ms); });

    exec->run();
    exec->run();  // a second worker, while the first is still resident
  }  // let go at once, so the destructor's handshake meets both workers

  SUCCEED() << "twenty double runs, and the object outlived both workers each time";
}

/**
 * @brief The poll does not report idle while an execution is running.
 *
 * Two threads poll 200000 times each while the execution holds an action open, so it provably
 * cannot finish mid-measurement and every idle answer is a wrong one.
 *
 * @remark is_running() is read once more before the action is released: if that were false, the
 * execution had finished early and the measurement would mean nothing.
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

  untangle::async::execution_poll poll;
  auto exec = void_execution::create_instance("held_open");
  std::function<void(void)> action;
  void_execution::bind_action_and_function(action, hold_until_released, exec);

  action();
  poll.add(*exec);
  exec->run();

  // Past this point the action is mid-flight and cannot return, so the execution is running for
  // every poll below and there is no check-then-use gap left to explain a wrong answer away.
  while (!action_started) {
    std::this_thread::yield();
  }

  constexpr auto polls_per_waiter = 200000;
  std::atomic_int idle_reports = {0};

  auto wait_on_the_poll = [&exec, &poll, &idle_reports] {
    for (auto i = 0; i < polls_per_waiter; ++i) {
      assert(exec->is_running());  // time of check
      if (!poll.is_running()) {    // time of use
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
 * @brief The poll reports running while any one of the executions it holds is.
 *
 * is_running() invokes every execution it holds and folds the answers. The idle one is registered
 * first, so the fold starts from false.
 */
TEST(execution_poll, reports_running_while_one_of_several_executions_is) {
  std::atomic_bool action_started = {false};
  std::atomic_bool release_action = {false};

  auto hold_until_released = [&action_started, &release_action] {
    action_started = true;
    while (!release_action) {
      std::this_thread::yield();
    }
  };

  untangle::async::execution_poll poll;
  auto idle = void_execution::create_instance("idle");
  auto busy = void_execution::create_instance("busy");

  std::function<void(void)> action;
  void_execution::bind_action_and_function(action, hold_until_released, busy);
  action();

  // The idle one is added first, so its false answer is the one the fold starts from.
  poll.add(*idle);
  poll.add(*busy);

  busy->run();
  while (!action_started) {
    std::this_thread::yield();
  }

  EXPECT_TRUE(poll.is_running())
      << "the poll reported idle while one of the executions it holds was running";

  release_action = true;

  EXPECT_TRUE(wait_until_poll_idle(poll, 5000ms))
      << "the poll never reported idle after the running execution finished";
}

/**
 * @brief A poll of one's own answers for what was added to it, and for nothing else.
 *
 * The case execution_poll exists to serve: an owner of a set of workers - a thread pool - waits for
 * its own and not for whatever else the process happens to be running. Two polls are constructed
 * here and only one is given the running execution.
 *
 * @remark The second poll never has anything added to it, so its is_running() also covers a poll
 * that was never connected to anything.
 */
TEST(execution_poll, an_instance_answers_only_for_what_was_added_to_it) {
  std::atomic_bool action_started = {false};
  std::atomic_bool release_action = {false};

  auto hold_until_released = [&action_started, &release_action] {
    action_started = true;
    while (!release_action) {
      std::this_thread::yield();
    }
  };

  auto busy = void_execution::create_instance("owned_by_one_poll");
  std::function<void(void)> action;
  void_execution::bind_action_and_function(action, hold_until_released, busy);
  action();

  untangle::async::execution_poll holding;
  untangle::async::execution_poll empty;
  holding.add(*busy);

  busy->run();
  while (!action_started) {
    std::this_thread::yield();
  }

  EXPECT_TRUE(holding.is_running())
      << "a poll reported idle while the execution it holds was running";
  EXPECT_FALSE(empty.is_running()) << "a poll reported an execution that was never added to it";

  release_action = true;

  EXPECT_TRUE(wait_for([&holding] { return !holding.is_running(); }, 5000ms))
      << "the poll never reported idle after the execution it holds finished";
}

/**
 * @brief An execution takes itself out of every poll holding it, not just one of them.
 *
 * add() stores the address of the execution's action_is_running, so a poll still holding one after
 * the execution has died calls into freed memory. An execution can be in several polls at once,
 * and withdrawing from one alone would leave the others dangling.
 *
 * @attention The failure here is a crash or a wrong answer rather than a failed expectation, and
 * -fsanitize=address is what names it. The expectations below are what a poll with nothing left in
 * it should say.
 */
TEST(execution_poll, an_execution_withdraws_from_every_poll_holding_it) {
  untangle::async::execution_poll first;
  untangle::async::execution_poll second;

  {
    auto exec = void_execution::create_instance("in_two_polls");
    first.add(*exec);
    second.add(*exec);
  }

  EXPECT_FALSE(first.is_running()) << "the first poll still holds a destroyed execution";
  EXPECT_FALSE(second.is_running()) << "the second poll still holds a destroyed execution";
}

/**
 * @brief remove() withdraws from one poll and leaves the others holding the execution.
 */
TEST(execution_poll, remove_withdraws_from_only_the_poll_it_was_called_on) {
  std::atomic_bool action_started = {false};
  std::atomic_bool release_action = {false};

  auto hold_until_released = [&action_started, &release_action] {
    action_started = true;
    while (!release_action) {
      std::this_thread::yield();
    }
  };

  auto busy = void_execution::create_instance("removed_from_one");
  std::function<void(void)> action;
  void_execution::bind_action_and_function(action, hold_until_released, busy);
  action();

  untangle::async::execution_poll kept;
  untangle::async::execution_poll dropped;
  kept.add(*busy);
  dropped.add(*busy);

  busy->run();
  while (!action_started) {
    std::this_thread::yield();
  }

  dropped.remove(*busy);

  EXPECT_TRUE(kept.is_running()) << "removing from one poll withdrew the execution from another";
  EXPECT_FALSE(dropped.is_running()) << "the execution was still reported after it was removed";

  release_action = true;

  EXPECT_TRUE(wait_for([&kept] { return !kept.is_running(); }, 5000ms))
      << "the poll never reported idle after the execution it holds finished";
}

/**
 * @brief The poll survives executions registering and withdrawing while another thread walks it.
 *
 * Two hundred executions register and are destroyed while a second thread polls without stopping.
 * The failure here is a crash, not a wrong answer.
 */
TEST(execution_poll, survives_executions_registering_while_another_thread_waits) {
  untangle::async::execution_poll poll;
  std::atomic_bool done = {false};

  std::thread waiter([&poll, &done] {
    while (!done) {
      poll.is_running();
    }
  });

  for (int i = 0; i < 200; ++i) {
    auto exec = void_execution::create_instance("churn");
    poll.add(*exec);
  }

  done = true;
  waiter.join();

  SUCCEED() << "the poll was walked while executions registered and withdrew";
}

/**
 * @brief An action outliving its execution reports a dead binding instead of following it.
 *
 * bind() hands the caller a lambda to store on the bound object, whose lifetime has nothing to do
 * with the execution's. Invoking it afterwards throws untangle::invalid_action.
 *
 * @remark The throw is asserted, not just the absence of a side effect: a queue that no worker
 * drains would leave the count at zero as well.
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
 * @brief The plain-function overload answers the same way.
 *
 * Its own case because the two overloads are separate code paths: a fix applied to one and not the
 * other would leave the header half repaired and this suite still green.
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
 * @brief A binding whose object is gone reports through on_error, like anything else that throws.
 *
 * The action was queued while the object was alive, so the dead binding is discovered by the
 * worker rather than by the caller. A caller asking why an action did not run wants that answer
 * too, not only the ones the action raised itself - and it must arrive through on_error rather
 * than as a line printed by whatever the queue is made of.
 */
TEST(execution_binding, a_dead_binding_reaches_on_error) {
  auto s = std::make_shared<sink>();
  const auto exec = int_execution::create_instance("dead_binding_reported");
  int_execution::bind_action_and_method(s->action, s, &sink::count, exec);

  s->action(1);  // queued while the object is alive
  s.reset();     // and dead by the time the worker reaches it

  std::atomic_int reported = {0};
  exec->on_error = [&reported](std::exception_ptr thrown) {
    try {
      std::rethrow_exception(thrown);
    } catch (const untangle::invalid_action&) {
      reported.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
    }
  };

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 5000ms))
      << "the worker did not finish";

  const std::string printed = testing::internal::GetCapturedStdout();
  const std::string warned = testing::internal::GetCapturedStderr();

  EXPECT_EQ(reported.load(), 1) << "the dead binding reached nobody";
  // Not "stdout is empty": the worker announces its own exit there. What may not appear is the
  // binding's own complaint, printed by whatever holds the actions instead of being reported.
  EXPECT_EQ(printed.find("bind: invalid object"), std::string::npos)
      << "the dead binding was printed instead of reported; stdout held: " << printed;
  EXPECT_EQ(warned, "") << "a handler was assigned, so the warning is its job now; stderr held: "
                        << warned;
}

/**
 * @brief Building a binding copies the callable only if the caller keeps it.
 *
 * The callable is taken by value and moved into the action, which is moved again into the lambda
 * that carries it, so a temporary costs nothing and an lvalue costs the one copy of keeping it.
 *
 * @remark About building the binding, not calling it; what a call costs is
 * queueing_through_a_binding_copies_the_argument_once.
 */
TEST(execution_binding, building_a_binding_copies_a_callable_the_caller_gives_up_not_at_all) {
  auto exec = counting_execution::create_instance("bind_cost");

  counting_action_t bound;

  const auto temporary_before = counting_action::copies.load();
  counting_execution::bind_action_and_function(bound, counting_action{}, exec);
  EXPECT_EQ(counting_action::copies.load() - temporary_before, 0)
      << "binding a temporary copied it " << counting_action::copies.load() - temporary_before
      << " times";

  const counting_action kept;
  const auto lvalue_before = counting_action::copies.load();
  counting_execution::bind_action_and_function(bound, kept, exec);
  EXPECT_EQ(counting_action::copies.load() - lvalue_before, 1)
      << "binding a callable the caller keeps copied it "
      << counting_action::copies.load() - lvalue_before
      << " times, and one is the price of keeping";
}

/**
 * @brief Calling a binding does not copy the action it carries.
 *
 * The binding owns its action once and shares it with every call it queues, so an invocation costs
 * a reference rather than a copy of the std::function - and a std::function copy is an allocation
 * whenever its target is not nothrow-copy-constructible.
 */
TEST(execution_binding, calling_a_binding_does_not_copy_the_action) {
  auto exec = counting_execution::create_instance("per_call");

  counting_action_t bound;
  counting_execution::bind_action_and_function(bound, counting_action{}, exec);

  const copy_counter argument;
  const auto before = counting_action::copies.load();

  bound(argument);
  bound(argument);

  EXPECT_EQ(counting_action::copies.load() - before, 0)
      << "two calls through a binding copied the action " << counting_action::copies.load() - before
      << " times";
}

/**
 * @brief An attacher does not reach into an attached execution that has been destroyed.
 *
 * attach() hands the attacher's actuator a pointer into the attached object, so a destroyed
 * attachment has to drop out of its attacher and leave the other attachments working. The dead
 * entry is attached first, so the survivor behind it can only run if the dead one is stepped over.
 *
 * @remark Driven through action_execute() on this thread rather than by a worker, so that a
 * dangling read is attributed to this line by the sanitizer instead of crashing a worker thread.
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

  // Driven directly, so the pass is synchronous and a dangling read lands on this thread where the
  // sanitizer attributes it to this line.
  attacher->action_execute();

  EXPECT_EQ(short_lived_ran->load(), 0)
      << "an action pending on a destroyed attached execution appeared to run";

  EXPECT_EQ(survivor_ran.load(), 1)
      << "a live attached execution must still be triggered past a destroyed one";
}

/**
 * @brief attach() refuses an attachment that would close a cycle, an execution onto itself
 * included.
 *
 * Driving a cycle recurses until the stack is gone, so it is refused where the caller still has a
 * stack to be told on. The legitimate first direction must keep working.
 *
 * @remark invalid_attachment, deliberately not invalid_action: the worker swallows that one by
 * design, and a cycle is a caller error that must not be swallowed.
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
 * The action is queued on the attached execution and never drained by a worker of its own, so the
 * count reads whether the attachment is still live.
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
 * stop() is final for an execution, so one that still accepts and runs an action after its former
 * attacher has stopped is one the stop did not reach.
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
 * The caller gets false rather than an exception: the state asked for already holds. A guard on the
 * boundary an implementation is most likely to get wrong once detach() erases from the actuator's
 * list.
 */
TEST(execution_attach, detaching_an_execution_that_was_never_attached_reports_false) {
  auto attacher = void_execution::create_instance("attacher");
  auto stranger = void_execution::create_instance("stranger");

  EXPECT_FALSE(attacher->detach(*stranger))
      << "detach() claimed to have removed an attachment that was never made";
}

/**
 * @brief A chain of attachments is legitimate, and driving its head runs the whole chain.
 *
 * a -> b -> c, driven once at a. This is the case the cycle check has to leave alone.
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
 * @brief A cycle that closes through a third execution is refused, and the chain survives it.
 *
 * Three deep on purpose: with a -> b -> c, c attaching a can only be caught by walking up the chain
 * of attachers, which a one-step check cannot do. A refused attach may leave nothing half done.
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
 * @brief An attached execution tells on_finished that its own batch has drained.
 *
 * Its actions are run by the attacher's worker, so the notification has to come from the drain
 * itself: a batch that drained is reported once, and a pass that ran nothing reports nothing.
 *
 * @remark Driven by a real start()ed attacher rather than from this thread, because which thread
 * raises the callback is part of the contract.
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
 * @brief A fresh execution has no results at all.
 *
 * Not that the value reads zero - that there is no value until an action has produced one.
 */
TEST(execution_results, are_empty_before_any_action_runs) {
  const auto exec = int_ret_execution::create_instance("fresh");

  EXPECT_TRUE(exec->results().empty()) << "a fresh execution reported a result no action produced";
}

/**
 * @brief Every action's return value is kept, in the order they ran.
 *
 * The three actions are queued before run(), so one pass drains them all.
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
 * @brief A run that drains twice keeps both batches' results.
 *
 * on_finished runs inside the drain, so an action queued from it is picked up by the same pass as
 * a second batch. The results belong to the run, not to the batch: clearing them per batch would
 * hand the caller only what the last one returned.
 */
TEST(execution_results, keep_the_results_of_every_batch_in_a_run) {
  const auto exec = int_ret_execution::create_instance("two_batches");

  std::atomic_bool queued_the_second = {false};
  exec->on_finished = [&exec, &queued_the_second] {
    if (queued_the_second.exchange(true)) {
      return;  // the second batch's own notification; a third would never end
    }
    exec->add_action([](int value) { return value; }, 2);
  };

  exec->add_action([](int value) { return value; }, 1);

  exec->run();

  ASSERT_TRUE(wait_for([&exec] { return !exec->is_running(); }, 5000ms))
      << "the worker did not finish";

  EXPECT_EQ(exec->results(), (std::vector<int>{1, 2}))
      << "a run's results must be every batch's, not only the last one's";
}

/**
 * @brief on_finished is where a caller reads the results of a run.
 *
 * It fires once the queue has drained, so the results are complete by the time it can see them.
 * The assertion is on what the callback read, not on what is readable afterwards.
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
 * Stated separately because an implementation that only ever appends passes the case above.
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
 * start() has no point at which a run is over, so collecting would grow without bound. Pinned here
 * so that it is changed on purpose if a use case ever wants it.
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
 * "Finished" means the queue this worker was given has drained, which is the signal for firing the
 * next batch - so it is per batch and not per worker.
 *
 * @remark The batches are kept one pass apart on purpose: execute_actions() drains everything
 * queued in a single call, so the third action is queued only after the first notification.
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
 * loop() wakes every 10ms to look in on its attachments and calls execute_actions() each time.
 * 200ms is twenty such passes, none of which finished anything.
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
 * The one-shot path drains once and leaves, so per batch and per worker are the same thing here.
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
 * on_finished is raised from inside the drain, on the worker's own thread, with the rest of the
 * path still to go. Once the worker has left, is_running() reads false; both halves are checked.
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

  // The other half: once the worker has left, the execution reports finished.
  EXPECT_FALSE(exec->is_running()) << "the execution still reported running after its run was over";
}

/**
 * @brief The same from the continuous side: a drained batch says nothing about the worker.
 *
 * start()'s worker is still there and waiting for the next batch, so is_running() must keep saying
 * so. Kept apart from the case above because the paths differ, not because the answers do.
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
 * Actions may be queued before run() or start(), so busy means work outstanding, not a worker
 * running.
 */
TEST(execution_busy, a_queued_action_makes_an_execution_busy) {
  const auto exec = void_execution::create_instance("queued");

  exec->add_action([] {});

  EXPECT_TRUE(exec->is_busy()) << "an execution with an action queued reported itself idle";
}

/**
 * @brief An execution is busy while an action is running, not only while one is queued.
 *
 * The worker pops an action under the lock and runs it with the lock released, so between those two
 * the queue is empty and the execution is anything but idle. The action is held open until the
 * reading has been taken, so it cannot race the action finishing early.
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

  // The queue is empty by now - the worker took the only action off it - and the action is still
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
 * is_running() is about the worker and stays true from start() until after stop(); is_busy() is
 * about the work. An execution that conflated them could not be asked whether it had room.
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
