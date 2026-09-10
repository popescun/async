// Copyright (c) 2018 Nicolae Popescu. MIT License.

/**
 * @brief Interface to \ref untangle::async::execution class.
 */
#pragma once

#include <actuator/actuator.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <print>
#include <string>
#include <thread>

namespace untangle {
template <typename actionT>
struct actuator;

namespace async {
template <typename actionT>
class execution;
}
}  // namespace untangle

namespace untangle {
namespace async {
/**
 *  @defgroup untangle_functions namespace untangle: functions
 */

/**
 * @brief Asynchronous binding to a class function member.
 *
 * It uses an \ref execution object to execute the bound class function member on other thread.
 * It creates an action as a binding to a class method (by untangle::bind()), and returns a
 * callable that passes this action to \ref execution::add_action()).
 *
 * @param obj - A std::shared_ptr that wraps the bound class object.
 * @param method - Pointer to function member. It is specified as &\<class type\>::\<function
 * member\>.
 * @param async_exec - An \ref execution object.
 * @return - A std::function<...>(lambda) that adds the action to the execution object's actions
 * list.
 *
 * @ingroup untangle_functions
 */
template <typename classT, typename T, typename actionT = std::function<T>>
actionT bind(const std::shared_ptr<classT>& obj, T classT::* method,
             execution<actionT>& async_exec) {
  actionT async_action = untangle::bind(obj, method);

  return [&async_exec, async_action](auto... args) -> typename actionT::result_type {
    async_exec.add_action(async_action, args...);
    return typename actionT::result_type();
  };
}

/**
 * @brief Asynchronous binding to a plain function.
 *
 * It uses an \ref execution object to execute the bound function on other thread.
 * It creates an action as std::function that wraps the function, and returns a callable that passes
 * this action to \ref execution::add_action()).
 *
 *
 * @param Fn - A plain function.
 * @param async_exec - An \ref execution object.
 * @return - A std::function(lambda) that adds the action to the execution object's actions list.
 *
 * @ingroup untangle_functions
 */
template <typename T, typename actionT = std::function<T>>
actionT bind(T& Fn, execution<actionT>& async_exec) {
  actionT async_action = Fn;
  return [&async_exec, async_action](auto... args) -> typename actionT::result_type {
    async_exec.add_action(async_action, args...);
    return typename actionT::result_type();
  };
}

/**
 * @brief Execution poll class.
 *
 * A single-tone class that may be used to verify if the polled \ref execution objects have finished
 * their processing. If at least one \ref execution object is running then this poll has the
 * "running" state.
 *
 * @remark An \ref execution object runs in a detached thread, hence it is not joinable.
 * The caller can wait for an \ref execution to be finished only by checking the "running"
 * state(\ref execution::is_running()). This class represents a convenient way to check any number
 * of
 * \ref execution objects.
 *
 */
class execution_poll {
 public:
  /**
   * @brief Adds an \ref execution object to the poll
   *
   * @param async_exec An \ref execution object.
   */
  template <typename asyncexecT>
  void add(asyncexecT& async_exec) {
    if (!actuator_is_running.is_connected()) {
      actuator_is_running = untangle::connect(async_exec.action_is_running);
    } else {
      actuator_is_running.add(&async_exec.action_is_running);
    }
  }

  /**
   * @brief Removes an \ref execution object from the poll.
   *
   * add() stores a pointer to the object's action_is_running, so an execution destroyed while still
   * registered leaves the poll calling into freed memory. ~execution() calls this.
   *
   * @param async_exec An \ref execution object.
   */
  template <typename asyncexecT>
  void remove(asyncexecT& async_exec) {
    actuator_is_running.remove(&async_exec.action_is_running);
  }

  /**
   * @brief Checks if the execution objects are running.
   *
   * @return true - if at least one \ref execution object in this poll is running.
   */
  auto is_running() {
    actuator_is_running();

    auto result = false;
    for (const auto& ret : actuator_is_running.results) {
      result |= ret;
    }
    return result;
  }

  /**
   * @brief Gets the single-tone instance.
   *
   * @return execution_poll An \ref execution_poll instance.
   */
  static execution_poll& get() {
    static execution_poll instance;
    return instance;
  }

 private:
  execution_poll() = default;
  ~execution_poll() = default;
  actuator<std::function<bool(void)>> actuator_is_running;
};

/**
 * @brief Asynchronous template execution class.
 *
 * An async execution provides a mechanism to queue actions and execute them sequentially on a
 * separate thread. It may also attach another \ref execution object and trigger its actions. This
 * way actions of different types may be executed on the same thread.
 *
 * The mechanism relies on an "asynchronous binding" created by using \ref bind().
 *
 * @tparam actionT It represents the type of the action. It is specified as std::function<...> and
 * should match the signature of the bound function or class method.
 */
template <typename actionT>
class execution {
 public:
  /**
   * @brief Constructs a new execution object.
   *
   */
  execution() {
    other_this = this;
    action_execute = untangle::bind(other_this, &execution<actionT>::execute_actions);
    action_stop = untangle::bind(other_this, &execution<actionT>::stop);
    action_is_running = untangle::bind(other_this, &execution<actionT>::is_running);
  }

  /**
   * @brief Constructs a new named execution object.
   *
   * The name identifies this execution in the warnings it reports, and is otherwise unused. An
   * execution built by the default constructor keeps the default name rather than an empty one, so
   * a warning always names something.
   *
   * @param exec_name - A name for this execution.
   */
  explicit execution(std::string exec_name) : execution() { name = std::move(exec_name); }
  /**
   * @brief Destroys the execution object, once its worker has left.
   *
   * The worker is detached and cannot be joined, so this waits on the same "running" state that
   * \ref execution_poll reports - the only handle a detached worker offers. It returns at once for
   * an execution that was never started, or that the caller has already polled to a stop.
   *
   * @remark This is safe only because running is cleared as the very last thing the worker does.
   * Nothing may be added after it in execute() or loop(): the object can be freed the moment it
   * reads false.
   */
  ~execution() {
    {
      std::lock_guard<std::mutex> lock(action_mutex);
      started = false;
      stopped = true;
    }

    action_cv.notify_all();

    while (running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    execution_poll::get().remove(*this);
  }

  /**
   * @brief Binds asynchronously an external action to a class function member.
   *
   * The binding is done by using a \ref bind().
   *
   * @param action [in,out] - An action of type std::function<...>.
   * @param obj - A std::shared_ptr that wraps the bound class object.
   * @param method - Pointer to function member. It is specified as &\<class type\>::\<function
   * member\>.
   */
  template <typename classT, typename T>
  void bind_action_and_method(actionT& action, const std::shared_ptr<classT>& obj,
                              T classT::* method) {
    action = bind(obj, method, *this);
  }

  template <typename T>
  void bind_action_and_function(actionT& action, const T& Fn) {
    action = bind(Fn, *this);
  }

  /**
   * @brief Checks if this execution has finished.
   *
   * @return true - The execution has not finished.
   * @return false - The execution has finished.
   */
  bool is_running() const { return running.load(); }

  void run() {
    running = true;
    this_thread = std::thread(&execution::execute, this);
    this_thread.detach();
  }

  void start() {
    running = true;
    {
      std::lock_guard<std::mutex> lock(action_mutex);
      started = true;
      stopped = false;
    }

    this_thread = std::thread(&execution::loop, this);
    this_thread.detach();
  }

  /**
   * @brief Stops the worker, after the actions already queued have run.
   *
   * @remark An action passed to add_action() after this call is refused, not queued: stop() is the
   * end of this execution's working life, and an action accepted then would never run.
   */
  void stop() {
    {
      std::lock_guard<std::mutex> lock(action_mutex);
      started = false;
      stopped = true;
    }

    // The worker drains what is already queued before it leaves loop(), so stop() does not have to
    // spin on the list; waking it is enough.
    action_cv.notify_all();

    if (actuator_stop.is_connected()) {
      actuator_stop();
    }
  }

  /**
   * @brief
   *
   * @tparam Args
   * @param action
   * @param args
   */
  template <typename... Args>
  void add_action(actionT action, Args... args) {
    {
      std::lock_guard<std::mutex> lock(action_mutex);

      // Once stopped, the worker is on its way out and would never reach this action; dropping it
      // here is what keeps it from sitting in the list looking as though it were pending.
      if (stopped) {
        std::println(stderr, "warning: execution '{}' is stopped, action not added", name);
        return;
      }

      action_list.push_back(std::bind(action, args...));
    }

    action_cv.notify_one();
  }

  template <typename otherT>
  void attach(otherT& other) {
    if (!actuator_execute.is_connected()) {
      actuator_execute = untangle::connect(other.action_execute);
    } else {
      actuator_execute.add(&other.action_execute);
    }

    if (!actuator_stop.is_connected()) {
      actuator_stop = untangle::connect(other.action_stop);
    } else {
      actuator_stop.add(&other.action_stop);
    }
  }

  auto result() { return _result; }

  static constexpr auto default_name = "unnamed";  //!< Name of an execution built unnamed.

  std::function<void(void)> action_execute;
  std::function<void(void)> action_stop;
  std::function<bool(void)> action_is_running;
  std::function<void(void)> on_finished;
  std::string name = default_name;

 private:
  using queued_action_t = std::function<typename actionT::result_type(void)>;

  /**
   * @brief Runs one action, keeping its return value when the action type has one.
   */
  void execute_action(queued_action_t& action) {
    if constexpr (std::is_void_v<typename actionT::result_type>) {
      action();
    } else {
      _result = action();
    }
  }

  void execute_actions() {
    // The action is taken off the list under the lock and invoked with the lock released: an action
    // is caller code that may run for a while, and may itself call add_action().
    for (;;) {
      queued_action_t action;

      {
        std::lock_guard<std::mutex> lock(action_mutex);
        if (action_list.empty()) {
          break;
        }

        action = std::move(action_list.front());
        action_list.pop_front();
      }

      // An action bound to an object that has since died throws invalid_action. Letting it leave a
      // thread function calls std::terminate, and a detached worker gives no one a chance to catch
      // it; the actuator drops such an action, and so does this.
      try {
        execute_action(action);
      } catch (const untangle::invalid_action& ia) {
        std::println(stderr, "warning: execution '{}' dropped an invalid action: {}", name,
                     ia.what());
      }
    }

    if (actuator_execute.is_connected()) {
      actuator_execute();
    }
  }

  void execute() {
    execute_actions();

    if (on_finished) {
      on_finished();
    }

    std::cout << "finishing thread" << std::endl;

    running = false;
  }

  void loop() {
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(action_mutex);

        // A bounded wait rather than a plain one: an attached execution has its own list and no way
        // to notify this condition variable, so the worker still has to look in on it periodically.
        action_cv.wait_for(lock, std::chrono::milliseconds(10),
                           [this] { return !action_list.empty() || !started; });

        if (!started && action_list.empty()) {
          break;
        }
      }

      execute_actions();
    }

    // What was queued before stop() still belongs to this execution; nothing can have been added
    // after it, because add_action() refuses once stopped.
    execute_actions();

    std::cout << "thread finished" << std::endl;

    // Must stay last: ~execution() may free this object the moment it reads false.
    running = false;
  }

  std::thread this_thread;

  std::atomic_bool started = {false};
  std::atomic_bool running = {false};

  // Set by stop(), and the difference between "not started yet" and "finished for good": actions
  // may be queued before run()/start(), but not after stop().
  std::atomic_bool stopped = {false};

  // action_list, started and stopped are written by every thread that calls add_action() or stop()
  // and read by the worker; nothing touches them outside this mutex. action_cv is what replaced the
  // spin in loop() and in stop().
  mutable std::mutex action_mutex;
  std::condition_variable action_cv;

  std::list<std::function<typename actionT::result_type(void)>> action_list;

  actuator<std::function<void(void)>> actuator_execute;
  actuator<std::function<void(void)>> actuator_stop;

  // std::vector cannot hold void type; use an arbitrary type e.g. int
  using resultT = std::conditional<std::is_void<typename actionT::result_type>::value, int,
                                   typename actionT::result_type>;
  typename resultT::type _result;

  execution<actionT>* other_this;
};
}  // namespace async
}  // namespace untangle
