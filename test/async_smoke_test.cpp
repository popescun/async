#include <async.hpp>
#include <format>
#include <print>
#include <string>
#include <thread>

#include "other_async.hpp"

class A {
 public:
  A() = default;
  void f() {
    std::println("A::f thread {}", std::this_thread::get_id());

    // on_finished();
  }

  void f_with_arg(int x) {
    std::println("A::f_with_arg {} thread {}", x, std::this_thread::get_id());
  }

  void f_with_arg_2(int x, int y) {
    std::println("A::f_with_arg_2 {} {} thread {}", x, y, std::this_thread::get_id());
  }

  int f_with_arg_and_return(int x) {
    std::println("A::f_with_arg_and_return {} thread {}", x, std::this_thread::get_id());
    return x;
  }

  std::function<void(void)> async_f;
  std::function<void(int)> async_f_with_arg;
  std::function<void(int, int)> async_f_with_arg_2;
  std::function<int(int)> async_f_with_arg_and_return;
};

// bind_action_and_*() are static, so each call qualifies the execution type; named once here.
using void_exec = untangle::async::execution<std::function<void(void)>>;
using int_exec = untangle::async::execution<std::function<void(int)>>;
using int_int_exec = untangle::async::execution<std::function<void(int, int)>>;
using int_ret_exec = untangle::async::execution<std::function<int(int)>>;

void f() { std::println("f thread {}", std::this_thread::get_id()); }

int main() {
  std::println("main thread {}", std::this_thread::get_id());

  // Declared before the executions so it outlives them: the poll and the executions take each
  // other out on destruction either way, and this keeps the order the readable one.
  untangle::async::execution_poll poll;

  auto a = std::make_shared<A>();
  auto asyncexec = void_exec::create_instance("asyncexec");
  void_exec::bind_action_and_method(a->async_f, a, &A::f, asyncexec);
  // std::function<void(void)> async_f;
  // void_exec::bind_action_and_function(async_f, f, asyncexec);
  // asyncexec->start(); // spawns new thread, and start executing the pending actions
  a->async_f();  // add an action, wrapping A::f, to the pending actions
  // async_f();
  // asyncexec->stop(); // execute all pending actions, and terminates the thread

  // attach other async execution that will run in the same thread
  auto asyncexec1 = int_exec::create_instance("asyncexec1");
  int_exec::bind_action_and_method(a->async_f_with_arg, a, &A::f_with_arg, asyncexec1);
  asyncexec->attach(asyncexec1);
  a->async_f_with_arg(10);

  auto asyncexec2 = int_int_exec::create_instance("asyncexec2");
  int_int_exec::bind_action_and_method(a->async_f_with_arg_2, a, &A::f_with_arg_2, asyncexec2);
  asyncexec->attach(asyncexec2);
  a->async_f_with_arg_2(20, 30);

  auto asyncexec3 = int_ret_exec::create_instance("asyncexec3");
  int_ret_exec::bind_action_and_method(a->async_f_with_arg_and_return, a, &A::f_with_arg_and_return,
                                       asyncexec3);
  a->async_f_with_arg_and_return(40);

  // receive on finished using an attached execution
  std::function<void(void)> action_on_finished;
  auto on_finished = [&asyncexec3]() {
    std::println("on_finished thread {}", std::this_thread::get_id());
    auto results = asyncexec3->results();

    // Built into one string and printed once: a line assembled by several calls can be split down
    // the middle by another worker's line, which is what this test used to do.
    std::string values;
    for (const auto& value : results) {
      values += std::format(" {}", value);
    }
    std::println("results={} value(s){}", results.size(), values);
  };

  // receive on finished by assigning the internal notifier
  asyncexec3->on_finished = [&asyncexec3]() {
    std::println("on_finished thread {}", std::this_thread::get_id());
    auto results = asyncexec3->results();

    // Built into one string and printed once: a line assembled by several calls can be split down
    // the middle by another worker's line, which is what this test used to do.
    std::string values;
    for (const auto& value : results) {
      values += std::format(" {}", value);
    }
    std::println("results={} value(s){}", results.size(), values);
  };

  auto asyncexec5 = void_exec::create_instance("asyncexec5");
  void_exec::bind_action_and_function(action_on_finished, on_finished, asyncexec5);
  asyncexec3->attach(asyncexec5);
  action_on_finished();

  poll.add(*asyncexec);
  poll.add(*asyncexec3);

  asyncexec->run();
  // asyncexec->stop();
  asyncexec3->run();

  auto otherasync = std::make_shared<other_async<std::string>>();
  otherasync->run(poll);

  // wait the polled executions to finish
  while (poll.is_running()) {  // time of check
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // time of use: returning destroys every execution above
  return 0;
}
