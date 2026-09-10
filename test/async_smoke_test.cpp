#include <async.hpp>

#include "other_async.hpp"

class A {
 public:
  A() = default;
  void f() {
    std::cout << "A::f thread " << std::this_thread::get_id() << std::endl;

    // on_finished();
  }

  void f_with_arg(int x) {
    std::cout << "A::f_with_arg " << x << " thread " << std::this_thread::get_id() << std::endl;
  }

  void f_with_arg_2(int x, int y) {
    std::cout << "A::f_with_arg_2 " << x << " " << y << " thread " << std::this_thread::get_id()
              << std::endl;
  }

  int f_with_arg_and_return(int x) {
    std::cout << "A::f_with_arg_and_return " << x << " thread " << std::this_thread::get_id()
              << std::endl;
    return x;
  }

  std::function<void(void)> async_f;
  std::function<void(int)> async_f_with_arg;
  std::function<void(int, int)> async_f_with_arg_2;
  std::function<int(int)> async_f_with_arg_and_return;
};

void f() { std::cout << "f thread " << std::this_thread::get_id() << std::endl; }

int main() {
  std::cout << "main thread " << std::this_thread::get_id() << std::endl;

  auto a = std::make_shared<A>();
  untangle::async::execution<std::function<void(void)>> asyncexec{"asyncexec"};
  asyncexec.bind_action_and_method(a->async_f, a, &A::f);
  // std::function<void(void)> async_f;
  // asyncexec.bind_action_and_function(async_f, f);
  // asyncexec.start(); // spawns new thread, and start executing the pending actions
  a->async_f();  // add an action, wrapping A::f, to the pending actions
  // async_f();
  // asyncexec.stop(); // execute all pending actions, and terminates the thread

  // attach other async execution that will run in the same thread
  untangle::async::execution<std::function<void(int)>> asyncexec1{"asyncexec1"};
  asyncexec1.bind_action_and_method(a->async_f_with_arg, a, &A::f_with_arg);
  asyncexec.attach(asyncexec1);
  a->async_f_with_arg(10);

  untangle::async::execution<std::function<void(int, int)>> asyncexec2{"asyncexec2"};
  asyncexec2.bind_action_and_method(a->async_f_with_arg_2, a, &A::f_with_arg_2);
  asyncexec.attach(asyncexec2);
  a->async_f_with_arg_2(20, 30);

  untangle::async::execution<std::function<int(int)>> asyncexec3{"asyncexec3"};
  asyncexec3.bind_action_and_method(a->async_f_with_arg_and_return, a, &A::f_with_arg_and_return);
  a->async_f_with_arg_and_return(40);

  // receive on finished using an attached execution
  std::function<void(void)> action_on_finished;
  auto on_finished = [&asyncexec3]() {
    std::cout << "on_finished thread " << std::this_thread::get_id() << std::endl;
    auto result = asyncexec3.result();
    std::cout << "result=" << result << std::endl;
  };

  // receive on finished by assigning the internal notifier
  asyncexec3.on_finished = [&asyncexec3]() {
    std::cout << "on_finished thread " << std::this_thread::get_id() << std::endl;
    auto result = asyncexec3.result();
    std::cout << "result=" << result << std::endl;
  };

  untangle::async::execution<std::function<void(void)>> asyncexec5{"asyncexec5"};
  asyncexec5.bind_action_and_function(action_on_finished, on_finished);
  asyncexec3.attach(asyncexec5);
  action_on_finished();

  untangle::async::execution_poll::get().add(asyncexec);
  untangle::async::execution_poll::get().add(asyncexec3);

  asyncexec.run();
  // asyncexec.stop();
  asyncexec3.run();

  auto otherasync = std::make_shared<other_async<std::string>>();
  otherasync->run();

  // wait the polled executions to finish
  while (untangle::async::execution_poll::get().is_running()) {  // time of check
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // time of use: returning destroys every execution above
  return 0;
}
