# async

[![ci](https://github.com/popescun/async/actions/workflows/ci.yml/badge.svg)](https://github.com/popescun/async/actions/workflows/ci.yml)

*c++ mechanism to run queued callables in an asynchronous fashion*

The interface consists of one header file that exposes a generic execution class. It is a wrapper of a queue of callables that are invoked on other thread. The callables are pumped into the queue by invoking an action from the caller thread.

The implementation is using the [actuator](https://github.com/popescun/actuator) callable, and therefore it exemplifies how a generic callable may 
improve the code structure, by easily creating `interfaces` inside a class instead of using external ones.

There are provided two execution modes: `one-off` and `continuous`.

## requirements

C++23. The interface reports warnings with `std::println`, so it needs a standard library that
provides `<print>` — GCC 14 or newer; older libc++ and MSVC releases may not have it yet.

## one-off execution
![alt text](res/oneoff.png)

### example: one-off execution of a plain function
```c++
#include <async.hpp>

void f()
{
  std::cout << "f thread " << std::this_thread::get_id() << std::endl;
}

int main()
{
  std::cout << "main thread " << std::this_thread::get_id() << std::endl;

  // declare an execution specialized for an action type; it must be held by a shared pointer,
  // because an async binding keeps only weak ownership of it
  // the name is optional, and identifies the execution in the warnings it reports
  using void_execution = untangle::async::execution<std::function<void(void)>>;
  auto execution = void_execution::create_instance("oneoff");
  std::function<void(void)> action;
  // create async binding between the action and f; the execution is passed as a shared pointer,
  // which the binding holds weakly - that is what lets the action outlive it safely
  void_execution::bind_action_and_function(action, f, execution);
  // whenever the action is invoked, a new callable wrapping f will be added to the execution's action queue
  action();
  // run the execution
  execution->run();

  // add the execution object to a poll of one's own; the poll answers for what was added to it
  untangle::async::execution_poll poll;
  poll.add(*execution);

  // wait the polled executions to finish
  while(poll.is_running())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return 0;
}
```

### example: one-off execution of a class method
```c++
#include <async.hpp>

class A
{
public:
  void f(int x)
  {
    std::cout << "A::f " << x << " thread " << std::this_thread::get_id() << std::endl;
  }
  std::function<void(int)> action;
};

int main()
{
  std::cout << "main thread " << std::this_thread::get_id() << std::endl;

  auto a = std::make_shared<A>();

  // declare an execution specialized for an action type; it must be held by a shared pointer
  using int_execution = untangle::async::execution<std::function<void(int)>>;
  auto execution = int_execution::create_instance("oneoff_method");
  // note both the bound object and the execution must be shared pointers
  int_execution::bind_action_and_method(a->action, a, &A::f, execution);
  // whenever the action is invoked, a new callable wrapping f will be added to the execution's action queue
  a->action(10);
  // run the execution
  execution->run();

  // add the execution object to a poll of one's own; the poll answers for what was added to it
  untangle::async::execution_poll poll;
  poll.add(*execution);

  // wait the polled executions to finish
  while(poll.is_running())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return 0;
}
```

## continuous execution
![alt text](res/continuous.png)

### example: continuous execution of a plain function
```c++
#include <async.hpp>

void f()
{
  std::cout << "f thread " << std::this_thread::get_id() << std::endl;
}

int main()
{
  std::cout << "main thread " << std::this_thread::get_id() << std::endl;

  // declare an execution specialized for an action type; it must be held by a shared pointer
  using void_execution = untangle::async::execution<std::function<void(void)>>;
  auto execution = void_execution::create_instance("continuous");
  std::function<void(void)> action;
  // create async binding between the action and f
  void_execution::bind_action_and_function(action, f, execution);

  // run the execution
  execution->start();
  // add f to the execution's queue (three times)
  action();action();action();
  // stop the execution
  execution->stop();

  // add the execution object to a poll of one's own; the poll answers for what was added to it
  untangle::async::execution_poll poll;
  poll.add(*execution);

  // wait the polled executions to finish
  while(poll.is_running())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return 0;
}
```

## queueing and lifetime

**Queueing is thread safe.** An action may be invoked from any thread; the execution's list is
guarded, and the worker is woken rather than polled for.

**`stop()` ends the execution's working life.** The worker runs whatever was queued before the call
and then leaves. An action invoked *after* `stop()` is refused rather than queued, because it would
never run, and the refusal is reported:

```
warning: execution 'continuous' is stopped, action not added
```

A task is refused after `stop()` for the same reason, and says so in its own words:

```
warning: execution 'continuous' is stopped, task not added
```

A task is also refused before it is ever queued if it could not do its job — no callback to notify,
or nothing to run. That one is the caller's own mistake and is caught while they are still on the
stack, rather than surfacing later as a failure of a task whose action had in fact succeeded:

```
warning: execution 'continuous' refused a task that cannot report
```

An action bound to an object that has since been destroyed is dropped the same way, rather than
ending the process:

```
warning: execution 'continuous' dropped an invalid action: bind: invalid object
```

**The worker is detached and cannot be joined.** Waiting for an execution therefore means waiting on
its `running` state, which is what `execution_poll` reports and what the examples above do. The
destructor waits on the same state, so an execution that goes out of scope while its worker is still
running blocks until the worker is finished rather than leaving it reading freed memory.

**The poll holds a pointer to each execution added to it**, so an execution withdraws itself in its
destructor - from every poll it was added to, since it may be in more than one. A poll destroyed
first likewise releases what it still holds, so a poll may be a local. `execution_poll::remove()` is
available for withdrawing one earlier; calling it is not required.

## tasks: work that reports back

Everything above queues **actions**, through `add_action()`. An action is fire and forget: it runs, what it returns is dropped, and the caller hears nothing more about it. A **task**, through `add_task()`, carries the callback it must notify:

```c++
auto exec = untangle::async::execution<std::function<int(int)>>::create_instance("scale");
exec->start();

// the callback comes last, after the arguments the action is bound to
exec->add_task([](int n) { return n * 2; }, 21, [](int result) { /* result == 42 */ });
```

A task whose action returns `void` still reports that it finished, with a callback taking nothing — *finished* is the message and the result is optional:

```c++
exec->add_task([](int by) { /* ... */ }, 5, [] { /* finished */ });
```

**The callback is the last argument, and the signature cannot say so.** A parameter pack cannot be followed by a deducible parameter, so it arrives inside the arguments and `untangle::bind_task()` splits it off. It must be callable with the action's result and return nothing — or callable with nothing at all, when the action returns void. A missing or unusable one is a compile error rather than silence.

**Why a task carries its own callback rather than being handed one.** The actuator has a callback convention already: pass a trailing callable to an actuator's call operator and it is invoked with each action's result. That convention reads the trailing argument of an *invocation*, and a queued call has no invocation left to read from — so the callback has to travel with the work. That is what a task is.

**`add_task()` answers `bool`, and the answer is the whole report.** It refuses a task the execution can never run — because it is stopped — or one that could never do its job: no callback to notify, or nothing to run. A refused task is dropped, so it never runs and never notifies, and a caller waiting on the callback alone would wait for ever.

**Finished does not mean failed.** A task whose action throws is *not* notified: there is no result to hand over, and for a void task no completion to report either. What it threw goes to `on_error`, as a failing action's does. And because a callback runs inside the same `try` as the task that owns it, `on_error` can fire for a task whose action in fact succeeded — the work was done and only the telling failed.

**`on_finished` is not a task's callback.** It is raised once per *pass*, when the queue has drained, and a pass that ran only tasks raises it like any other. A task's own callback has already run by then, inside the same drain and on the same thread. Two notifications, two meanings: one per task, one per drained queue.

Both kinds share one queue and one worker. Within a single pass every action runs before any task, so the two do not interleave in the order they were queued; across passes the queue is still first in, first out.

## building the tests

```sh
cmake -S test -B test/build
cmake --build test/build
ctest --test-dir test/build
```

`test/CMakeLists.txt` fetches googletest at configure time, so the first configure needs network
access. The `actuator` submodule has to be present: `git submodule update --init`.

### sanitizers

`ASYNC_SANITIZE` builds the tests under a sanitizer. It is off by default, because a sanitized build
is several times slower and ThreadSanitizer does not ship for every toolchain.

```sh
cmake -S test -B test/build-asan -DASYNC_SANITIZE=address
cmake --build test/build-asan
ctest --test-dir test/build-asan
```

Accepted values are `address`, `thread`, `undefined`, or empty. Anything else is refused at
configure time rather than passed through to the compiler.

**Use a separate build directory per sanitizer.** `address` and `thread` instrument the same
accesses in incompatible ways and cannot be combined, so one build directory is one sanitizer.

The two answer different questions. ThreadSanitizer names both sides of a data race, which is how
the queue's races were diagnosed. AddressSanitizer reports a use-after-free, which is how the
dangling attachment was. Neither sees everything: a defect reached through a destroyed
`std::mutex` throws `std::system_error` from inside libsystem before any instrumented load runs, so
both sanitizers stay silent. Reach for a direct instrumented read when that happens.
