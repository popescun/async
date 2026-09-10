#pragma once

#include <async.hpp>
#include <string>

template <typename T>
class other_async : public std::enable_shared_from_this<other_async<T>> {
 public:
  other_async() = default;
  ~other_async() = default;

  void set(const T& text) { std::cout << text << std::endl; }

  void run() {
    sharedThis = this->shared_from_this();
    asyncexec.bind_action_and_method(async_action, sharedThis, &other_async<std::string>::set);
    untangle::async::execution_poll::get().add(asyncexec);
    async_action("test");
    asyncexec.run();
  }

 private:
  std::shared_ptr<other_async<T>> sharedThis;
  untangle::async::execution<std::function<void(const std::string&)>> asyncexec{"other_async"};
  std::function<void(const std::string&)> async_action;
};