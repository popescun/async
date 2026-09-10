# async

[![ci](https://github.com/popescun/async/actions/workflows/ci.yml/badge.svg)](https://github.com/popescun/async/actions/workflows/ci.yml)

*c++ mechanism to run queued callables in an asynchronous fashion*

The interface consists of one header file that exposes a generic execution class. It is a wrapper of a list of callables that are invoked on other thread. The callables are pumped into the list by invoking an action from the caller thread.

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

  // declare an execution specialized for an action type
  // the name is optional, and identifies the execution in the warnings it reports
  untangle::async::execution<std::function<void(void)>> execution{"oneoff"};
  std::function<void(void)> action;
  // create async binding between the action and f
  execution.bind_action_and_function(action, f);
  // whenever the action is invoked, a new callable wrapping f will be added to the execution's action list
  action();
  // run the execution
  execution.run();

  // add the execution object to the running poll
  untangle::async::execution_poll::get().add(execution);

  // wait the polled executions to finish
  while(untangle::async::execution_poll::get().is_running())
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

  // declare an execution specialized for an action type
  untangle::async::execution<std::function<void(int)>> execution{"oneoff_method"};
  execution.bind_action_and_method(a->action, a, &A::f); // note the bound object must be a shared pointer
  // whenever the action is invoked, a new callable wrapping f will be added to the execution's action list
  a->action(10);
  // run the execution
  execution.run();

  // add the execution object to the running poll
  untangle::async::execution_poll::get().add(execution);

  // wait the polled executions to finish
  while(untangle::async::execution_poll::get().is_running())
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

  // declare an execution specialized for an action type
  untangle::async::execution<std::function<void(void)>> execution{"continuous"};
  std::function<void(void)> action;
  // create async binding between the action and f
  execution.bind_action_and_function(action, f);

  // run the execution
  execution.start();
  // add f to the execution's queue (three times)
  action();action();action();
  // stop the execution
  execution.stop();

  // add the execution object to the running poll
  untangle::async::execution_poll::get().add(execution);

  // wait the polled executions to finish
  while(untangle::async::execution_poll::get().is_running())
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
destructor. `execution_poll::remove()` is available for withdrawing one earlier; calling it is not
required.
