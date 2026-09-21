// Copyright (c) 2018 Nicolae Popescu. MIT License.

/**
 * @brief Interface to \ref untangle::async::execution class.
 */
#pragma once

#include <actuator/actuator.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace untangle {
namespace async {
/**
 *  @defgroup untangle_functions namespace untangle: functions
 */

/**
 * @brief Raised by \ref execution::attach() when the attachment asked for is refused.
 *
 * @remark Not an untangle::invalid_action: that one reports a binding whose target has died and is
 * swallowed by the worker, while this is a caller error and must not be.
 */
struct invalid_attachment : std::exception {
  /**
   * @brief Construct a new invalid attachment object.
   *
   * @param text - A message text, describing the reason of this exception.
   */
  explicit invalid_attachment(std::string text) : message_(std::move(text)) {}

  /**
   * @brief The message text describing the reason of this exception.
   *
   * @return const char* - The message text.
   */
  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;  //!< It holds the message text.
};

/**
 * @brief One execution's place in the attachment graph: who attached it, if anyone.
 *
 * At namespace scope, so it is one type rather than one per specialisation: an execution may be
 * attached by an execution of any specialisation, and the chain is walked without knowing any
 * link's action type. It dies with its execution, so it doubles as a liveness token - a link that
 * still locks belongs to an execution that is still there.
 */
struct attachment {
  //! The link of the execution that attached this one; empty when nothing has.
  std::weak_ptr<attachment> attacher;
};

/**
 * @brief Poll that reports whether any \ref execution added to it is still running.
 *
 * @remark An execution runs in a detached thread and cannot be joined, so checking the "running"
 * state is the only way to wait for one. This checks any number of them at once.
 *
 * @remark **Instantiable, and \ref get() is one instance rather than the only one.** A caller that
 * owns a set of workers - a thread pool is the case this was written for - wants to wait for its
 * own and not for whatever else the process is running, and that is a poll of its own. The
 * singleton stays for callers with nothing to separate themselves from.
 */
class execution_poll {
 public:
  /**
   * @brief Constructs an empty poll. \ref add() is what puts anything in it.
   */
  execution_poll() = default;

  /**
   * @brief Destroys the poll, releasing every execution still in it.
   *
   * @remark The record runs both ways, and it has to: each side holds a pointer into the other, so
   * whichever dies first takes itself out of the other. ~execution() withdraws from every poll
   * holding it; this tells every execution it holds that this poll is gone. Neither order leaves a
   * dangling pointer, which is what lets a poll be a local rather than only a singleton.
   */
  ~execution_poll() {
    std::vector<registration> held;

    {
      std::lock_guard<std::mutex> lock(actuator_mutex_);
      held.swap(registrations_);
    }

    // Outside the lock: each of these takes the execution's own, and ~execution() takes them in
    // the opposite order. Releasing this one first is what keeps the two from meeting in a cycle.
    for (auto& one : held) {
      one.forget();
    }
  }
  /**
   * @brief Adds an \ref execution object to the poll
   *
   * @param async_exec An \ref execution object.
   */
  template <typename asyncexecT>
  void add(asyncexecT& async_exec) {
    {
      std::lock_guard<std::mutex> lock(actuator_mutex_);

      if (!actuator_is_running_.is_connected()) {
        actuator_is_running_ = untangle::connect(async_exec.action_is_running);
      } else {
        actuator_is_running_.add(&async_exec.action_is_running);
      }

      registrations_.emplace_back(&async_exec.action_is_running,
                                  [this, &async_exec] { async_exec.forget_poll(*this); });
    }

    // Outside the lock, and deliberately: this takes the execution's own. The execution has to
    // know which polls hold a pointer into it, because ~execution() is what takes it back out and
    // there is now more than one poll it could be in.
    async_exec.remember_poll(*this);
  }

  /**
   * @brief Removes an \ref execution object from the poll.
   *
   * @attention \ref add() stores a pointer into the object, so one destroyed while still added
   * would leave the poll calling into freed memory. ~execution() calls this.
   *
   * @param async_exec An \ref execution object.
   */
  template <typename asyncexecT>
  void remove(asyncexecT& async_exec) {
    withdraw(&async_exec.action_is_running);
    async_exec.forget_poll(*this);
  }

  /**
   * @brief Takes one action pointer out, without touching the execution it came from.
   *
   * What ~execution() calls for each poll holding it: by then the object is being destroyed and
   * must not be called back into, so the two halves of \ref remove() are separated here.
   *
   * @param action - The address \ref add() stored.
   */
  void withdraw(const std::function<bool(void)>* action) {
    std::lock_guard<std::mutex> lock(actuator_mutex_);

    actuator_is_running_.remove(action);
    std::erase_if(registrations_,
                  [action](const registration& one) { return one.action == action; });
  }

  /**
   * @brief Checks if the execution objects are running.
   *
   * @return true - if at least one \ref execution object in this poll is running.
   */
  auto is_running() {
    std::lock_guard<std::mutex> lock(actuator_mutex_);
    actuator_is_running_();

    auto result = false;
    for (const auto& ret : actuator_is_running_.results) {
      result = result || ret;
    }
    return result;
  }

  /**
   * @brief Gets the process-wide instance.
   *
   * @remark One poll among however many are constructed, not the only one there can be. It answers
   * for every execution added to *it*; an owner that wants an answer about its own workers alone
   * constructs a poll and adds them to that.
   *
   * @return execution_poll The process-wide \ref execution_poll instance.
   */
  static execution_poll& get() {
    static execution_poll instance;
    return instance;
  }

 private:
  /**
   * @brief One execution's presence in this poll: what the actuator holds, and how to tell the
   * execution that this poll is gone.
   */
  struct registration {
    const std::function<bool(void)>* action;
    std::function<void()> forget;
  };

  actuator<std::function<bool(void)>> actuator_is_running_;
  std::vector<registration> registrations_;
  mutable std::mutex actuator_mutex_;
};

/**
 * @brief Asynchronous template execution class.
 *
 * An async execution provides a mechanism to queue actions and execute them sequentially on a
 * separate thread. It may also attach another \ref execution object and trigger its actions. This
 * way actions of different types may be executed on the same thread.
 *
 * The mechanism relies on an "asynchronous binding" created by \ref bind_action_and_method() or
 * \ref bind_action_and_function().
 *
 * @tparam actionT It represents the type of the action. It is specified as std::function<...> and
 * should match the signature of the bound function or class method.
 */
template <typename actionT>
class execution {
  // An execution may attach one of a different specialisation, which is a different class;
  // attach() and detach() reach into its attachment_lifetime_.
  template <typename otherActionT>
  friend class execution;

  // add() and remove() keep the record this object holds of the polls that hold it.
  friend class execution_poll;

 public:
  /**
   * @brief Creates an execution owned by a std::shared_ptr, which is what binding to it requires.
   *
   * An execution built any other way is still usable - \ref add_action() takes actions directly -
   * but cannot be bound, and the attempt does not compile.
   *
   * @param args - Constructor arguments: a name, or nothing for the default name.
   * @return - A std::shared_ptr owning the new execution.
   */
  template <typename... Args>
  static std::shared_ptr<execution> create_instance(Args&&... args) {
    return std::make_shared<execution>(std::forward<Args>(args)...);
  }

  /**
   * @brief Constructs an execution with the default name. Call \ref create_instance() instead.
   *
   * The name identifies this execution in the warnings it reports, and is otherwise unused.
   */
  execution() : execution(std::string(default_name)) {}

  /**
   * @brief Constructs a named execution. Use \ref create_instance() for one that is to be bound.
   *
   * @param exec_name - A name for this execution.
   */
  explicit execution(std::string exec_name) : name(std::move(exec_name)) {
    action_execute = untangle::bind(this, &execution<actionT>::execute_actions);
    action_stop = untangle::bind(this, &execution<actionT>::stop);
    action_is_running = untangle::bind(this, &execution<actionT>::is_running);
  }

  /**
   * @brief Destroys the execution, once its worker has left.
   *
   * The worker is detached and cannot be joined, so this waits on the "running" state instead. It
   * detaches this execution from whatever attached it first, and returns at once for one that was
   * never started.
   *
   * @attention Safe only because `running_` is cleared as the very last thing the worker does:
   * this object may be freed the moment it reads false, so nothing may follow it in execute() or
   * loop().
   *
   * @remark **No rule of five, deliberately.** An execution is declared once and used in place;
   * the members already delete the copy operations several times over.
   */
  ~execution() {
    // Out of the attacher first, before the worker is even asked to stop: from here on nothing
    // may reach this object, and the actions about to be destroyed are the ones it holds.
    if (!attachment_lifetime_->attacher.expired()) {
      attacher_execute_->remove(&action_execute);
      attacher_stop_->remove(&action_stop);
    }

    {
      std::lock_guard<std::mutex> lock(action_mutex_);
      started_ = false;
      stopped_ = true;
    }

    action_cv_.notify_all();

    while (running_.load()) {  // time of check
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // time of use: this object is freed once the destructor returns, so the check above is only
    // safe because running_ is the last thing the worker touches.
    //
    // Out of every poll that holds a pointer into this object. Withdrawing from the singleton
    // alone was enough while it was the only poll there could be; now that a caller may own one,
    // an execution can be in several, and an address left in any of them outlives the object.
    // Taken by swap so nothing else can be added to the record while this walks it, and withdrawn
    // rather than removed because remove() would call back into an object that is going away.
    std::vector<execution_poll*> holding_polls;

    {
      std::lock_guard<std::mutex> lock(polls_mutex_);
      holding_polls.swap(polls_);
    }

    for (auto* poll : holding_polls) {
      poll->withdraw(&action_is_running);
    }
  }

  /**
   * @brief Binds asynchronously an external action to a class function member.
   *
   * It creates an action as a binding to a class method (by untangle::bind()), and assigns to
   * \p action a callable that queues it. The execution is held weakly, so the action may outlive
   * it; the action itself is shared with every call queued from it, rather than copied per call.
   *
   * @attention Invoking \p action after the execution has been destroyed throws
   * untangle::invalid_action. The worker catches it and drops the action with a warning.
   *
   * @param action [in,out] - An action of type std::function<...>.
   * @param obj - A std::shared_ptr that wraps the bound class object.
   * @param method - Pointer to function member. It is specified as &\<class type\>::\<function
   * member\>.
   * @param async_exec - A std::shared_ptr owning the execution the action is queued on. Requiring
   * it here is what keeps an execution that no shared_ptr owns from being bound at all.
   */
  template <typename classT, typename T>
  static void bind_action_and_method(actionT& action, const std::shared_ptr<classT>& obj,
                                     T classT::* method,
                                     const std::shared_ptr<execution>& async_exec) {
    auto async_action = std::make_shared<const actionT>(untangle::bind(obj, method));

    action = [wp = std::weak_ptr<execution>(async_exec),
              async_action](auto&&... args) -> actionT::result_type {
      // lock() also keeps the execution alive for the duration of the call
      const auto exec = wp.lock();
      if (!exec) {
        throw invalid_action("bind: invalid execution");
      }

      exec->add_queued_action([async_action, ... args = std::forward<decltype(args)>(args)] {
        return (*async_action)(args...);
      });

      return typename actionT::result_type();
    };
  }

  /**
   * @brief Binds asynchronously an external action to a plain function.
   *
   * It wraps \p Fn in an action and assigns to \p action a callable that queues it. The execution
   * is held weakly, as in \ref bind_action_and_method(), and the action is shared with every call
   * queued from it rather than copied per call.
   *
   * @param action [in,out] - An action of type std::function<...>.
   * @param Fn - A plain function, or any callable; moved into the binding.
   * @param async_exec - A std::shared_ptr owning the execution the action is queued on.
   */
  template <typename T>
  static void bind_action_and_function(actionT& action, T Fn,
                                       const std::shared_ptr<execution>& async_exec) {
    auto async_action = std::make_shared<const actionT>(std::move(Fn));

    action = [wp = std::weak_ptr<execution>(async_exec),
              async_action](auto&&... args) -> actionT::result_type {
      const auto exec = wp.lock();
      if (!exec) {
        throw invalid_action("bind: invalid execution");
      }

      exec->add_queued_action([async_action, ... args = std::forward<decltype(args)>(args)] {
        return (*async_action)(args...);
      });

      return typename actionT::result_type();
    };
  }

  /**
   * @brief Is a worker of this execution's still there?
   *
   * True from \ref run() or \ref start() until the worker has left, which is the same instant
   * \ref ~execution() waits for and the same one \ref run() waits for before starting another.
   *
   * @remark \ref on_finished is raised from inside the drain and reads this as **true** - the
   * worker is standing in the callback with the rest of its path still to go.
   *
   * @return true - a worker is still there.
   */
  bool is_running() const { return running_.load(); }

  /**
   * @brief Is this execution working through its action queue?
   *
   * True from the moment an action is added until the last one has returned. Unlike
   * \ref is_running(), which is about the worker thread, this answers "has it room for more work"
   * - a worker started by \ref start() is running for its whole life whether busy or idle.
   *
   * @remark Answers for this execution's own actions, not for those it triggers through
   * \ref attach().
   *
   * @remark A false answer is durable only for the caller that is itself adding the actions; a
   * true one may stop being true the moment after.
   *
   * @return true - actions are queued, or one is running.
   */
  bool is_busy() const {
    std::lock_guard<std::mutex> lock(action_mutex_);
    return !action_queue_.empty() || executing_action_.load();
  }

  /**
   * @brief Runs what is queued on a worker of its own, once, and keeps the results.
   *
   * The worker is detached: wait for it with \ref is_running() or \ref execution_poll. Actions
   * queued while it is draining are run by it too. \ref results() is filled by this path only.
   *
   * Blocks until a worker still resident from a previous run has left, so one execution never has
   * two. \ref is_running() reads false at exactly that moment, so a caller that waits for it and
   * then runs again does not wait here at all.
   *
   * @attention Never call this from inside an action: the worker that would have to leave is the
   * one making the call, so it waits for itself and hangs.
   */
  void run() {
    wait_thread_to_finish();
    collecting_results_ = true;
    thread_ = std::thread(&execution::execute, this);
    thread_.detach();
  }

  /**
   * @brief Starts a continuous worker that runs whatever is queued until \ref stop().
   *
   * Unlike \ref run(), the worker stays for the life of the execution, so \ref is_running() is
   * true whether or not there is work; ask \ref is_busy() instead. Results are not collected.
   *
   * Blocks until a worker still resident has left, as \ref run() does.
   *
   * @attention Never call this from inside an action, for the reason given on \ref run().
   */
  void start() {
    wait_thread_to_finish();
    collecting_results_ = false;
    {
      std::lock_guard<std::mutex> lock(action_mutex_);
      started_ = true;
      stopped_ = false;
    }

    thread_ = std::thread(&execution::loop, this);
    thread_.detach();
  }

  /**
   * @brief Stops the worker, after the actions already queued have run.
   *
   * @remark An action passed to add_action() after this call is refused, not queued: stop() is the
   * end of this execution's working life, and an action accepted then would never run.
   */
  void stop() {
    {
      std::lock_guard<std::mutex> lock(action_mutex_);
      started_ = false;
      stopped_ = true;
    }

    // The worker drains what is already queued before it leaves loop(), so stop() does not have to
    // spin on the queue; waking it is enough.
    action_cv_.notify_all();

    if (actuator_stop_.is_connected()) {
      actuator_stop_();
    }
  }

  /**
   * @brief Queues an action, bound to \p args, and says whether it was taken.
   *
   * It runs later, on whichever thread drives this execution - its own worker, or an attacher's.
   * Queueing before \ref run() or \ref start() is normal.
   *
   * @attention A stopped execution refuses and drops the action, silently for a caller that
   * ignores the answer. Nothing is thrown: untangle::invalid_action means a dead binding, while a
   * refused action is sound and merely too late.
   *
   * @remark A caller arriving through \ref bind_action_and_method() or
   * \ref bind_action_and_function() never sees the answer: those lambdas return
   * `actionT::result_type`, which has no room for it.
   *
   * @tparam Args - The argument types the action is bound to.
   * @param action - The action to queue.
   * @param args - The arguments to bind to \p action.
   *
   * @return true - queued, and this execution's worker will run it.
   * @return false - the execution is stopped and the action was dropped.
   */
  template <typename... Args>
  bool add_action(actionT action, Args&&... args) {
    return add_queued_action(std::bind(std::move(action), std::forward<Args>(args)...));
  }

  /**
   * @brief Attaches another execution, so that this one triggers it.
   *
   * The attached execution's pending actions are run by this one's worker, and stopping this one
   * stops that one too. Actions of different types can be run on one thread this way.
   *
   * The attachment is a pointer into \p other; what makes that safe is the record left on the
   * other side, which ~execution() and \ref detach() use to take it back out.
   *
   * @param other - A std::shared_ptr owning the execution to trigger; requiring one keeps an
   * unowned execution from being attached at all. Attaching the same execution twice adds it
   * twice, and one \ref detach() still removes it completely.
   *
   * @throw invalid_attachment - if \p other is attached already. An execution has at most one
   * attacher, which keeps the graph a forest and the cycle check a walk rather than a search.
   *
   * @throw invalid_attachment - if the attachment would close a cycle, itself included. Driving a
   * cycle recurses until the stack is gone, so it is refused where the caller can still be told.
   */
  template <typename otherT>
  void attach(const std::shared_ptr<otherT>& other) {
    if (static_cast<const void*>(other.get()) == static_cast<const void*>(this)) {
      throw invalid_attachment("attach: an execution cannot be attached to itself");
    }

    if (!other->attachment_lifetime_->attacher.expired()) {
      throw invalid_attachment("attach: that execution is attached already");
    }

    // At most one attacher each makes the graph a forest, so a cycle closes exactly when other is
    // somewhere up this chain. Walking it is the whole check.
    for (auto link = attachment_lifetime_->attacher.lock(); link; link = link->attacher.lock()) {
      if (link == other->attachment_lifetime_) {
        throw invalid_attachment("attach: the attachment would close a cycle");
      }
    }

    if (!actuator_execute_.is_connected()) {
      actuator_execute_ = untangle::connect(other->action_execute);
    } else {
      actuator_execute_.add(&other->action_execute);
    }

    if (!actuator_stop_.is_connected()) {
      actuator_stop_ = untangle::connect(other->action_stop);
    } else {
      actuator_stop_.add(&other->action_stop);
    }

    // What ~execution() needs to take itself back out of these actuators: the link proves this
    // attacher is still there, and the two pointers are only ever followed while it does.
    other->attachment_lifetime_->attacher = attachment_lifetime_;
    other->attacher_execute_ = &actuator_execute_;
    other->attacher_stop_ = &actuator_stop_;
  }

  /**
   * @brief Detaches an execution previously attached by \ref attach().
   *
   * Both paths are unwired together, the triggering one and the stopping one.
   *
   * @param other - The execution to stop triggering. Detaching one that was never attached is not
   * an error: the caller asked for a state that already holds.
   *
   * @return true - \p other was attached and is no longer.
   * @return false - \p other was not attached to this execution.
   */
  template <typename otherT>
  bool detach(otherT& other) {
    // The actuator's own action list is the record of what is attached; nothing else has to keep
    // one. An action is stored as a pointer, so the attachment is found by identity.
    const auto& actions = actuator_execute_.actions;
    if (std::find(actions.begin(), actions.end(), &other.action_execute) == actions.end()) {
      return false;
    }

    actuator_execute_.remove(&other.action_execute);
    actuator_stop_.remove(&other.action_stop);

    other.attachment_lifetime_->attacher.reset();
    other.attacher_execute_ = nullptr;
    other.attacher_stop_ = nullptr;

    return true;
  }

  /**
   * @brief The return values of the actions run by run(), in the order they ran.
   *
   * Read it from \ref on_finished, which fires once the queue has drained, so the results are
   * complete by the time the callback sees them. Returned by value: the worker is appending to it.
   *
   * @remark Only \ref run() fills this. A continuous worker has no point at which a run is over,
   * so \ref start() collects nothing.
   *
   * @return The results of the most recent run, or empty if none has produced any.
   */
  auto results() const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return results_;
  }

  static constexpr auto default_name = "default_name";  //!< Name of an execution built unnamed.

  /**
   * @brief Connection point that runs this execution's queued actions. Bound to execute_actions().
   *
   * @attention Wired by the constructor. Assigning to it unwires whatever is connected, silently.
   */
  std::function<void(void)> action_execute;

  /**
   * @brief Connection point that stops this execution. Bound to \ref stop().
   *
   * \ref attach() wires it together with \ref action_execute, and \ref detach() unwires both.
   *
   * @attention Wired by the constructor. Assigning to it unwires whatever is connected, silently.
   */
  std::function<void(void)> action_stop;

  /**
   * @brief Connection point that reports whether this execution is running. Bound to
   * \ref is_running().
   *
   * \ref execution_poll holds its address, and ~execution() takes it back out.
   *
   * @attention Wired by the constructor. Assigning to it unwires whatever is connected, silently.
   */
  std::function<bool(void)> action_is_running;

  /**
   * @brief Called on the worker's thread once the action queue has drained. Assigned by the caller.
   *
   * \ref results() is complete by the time it runs, and \ref is_running() reads true there.
   */
  std::function<void(void)> on_finished;
  std::string name = default_name;

 private:
  using queued_action_t = std::function<typename actionT::result_type(void)>;

  /**
   * @brief Records a poll that now holds this execution's \ref action_is_running. Called by
   * execution_poll::add().
   */
  void remember_poll(execution_poll& poll) {
    std::lock_guard<std::mutex> lock(polls_mutex_);

    polls_.push_back(&poll);
  }

  /**
   * @brief Drops that record. Called by execution_poll::remove().
   */
  void forget_poll(execution_poll& poll) {
    std::lock_guard<std::mutex> lock(polls_mutex_);

    std::erase(polls_, &poll);
  }

  /**
   * @brief Queues a callable already bound to its arguments, and says whether it was taken.
   *
   * What \ref add_action() does once it has bound one, and what a binding queues directly: a
   * binding holds its action in a std::shared_ptr and hands over a callable carrying the pointer,
   * so a call costs a reference rather than a copy of the action.
   *
   * @return true - queued; false - the execution is stopped and the callable was dropped.
   */
  bool add_queued_action(queued_action_t action) {
    {
      std::lock_guard<std::mutex> lock(action_mutex_);

      // Once stopped, the worker is on its way out and would never reach this action; dropping it
      // here is what keeps it from sitting in the queue looking as though it were pending.
      if (stopped_) {
        std::println(stderr, "warning: execution '{}' is stopped, action not added", name);
        return false;
      }

      action_queue_.push_back(std::move(action));
    }

    action_cv_.notify_one();
    return true;
  }

  /**
   * @brief Runs one action, keeping its return value when the action type has one and run() asked.
   *
   * results_mutex_ is taken only here, after the action has returned, so an action that calls
   * add_action() never meets it held.
   */
  void execute_action(queued_action_t& action) {
    if constexpr (std::is_void_v<typename actionT::result_type>) {
      action();
    } else {
      auto value = action();

      if (collecting_results_) {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_.push_back(std::move(value));
      }
    }
  }

  /**
   * @brief Runs everything queued, then drives the attached executions.
   *
   * Counts what it ran in \ref actions_run_, which is how \ref notify_finished() tells a batch
   * that drained from a worker that woke with nothing to do. Every action that came off the queue
   * counts, including one that reported a dead binding.
   *
   * @remark The count is of this execution's own actions; an attached execution records its own.
   */
  void execute_actions() {
    actions_run_ = 0;

    // The action is taken off the queue under the lock and invoked with the lock released: an
    // action is caller code that may run for a while, and may itself call add_action().
    for (;;) {
      queued_action_t action;

      {
        std::lock_guard<std::mutex> lock(action_mutex_);
        if (action_queue_.empty()) {
          break;
        }

        action = std::move(action_queue_.front());
        action_queue_.pop_front();

        // Under the lock that emptied the queue, so is_busy() sees the pop and this together or
        // neither.
        executing_action_ = true;
      }

      // Nothing may leave this loop: an exception escaping a detached thread function calls
      // std::terminate.
      try {
        execute_action(action);
      } catch (const invalid_action& ia) {
        std::println(stderr, "warning: execution '{}' dropped an invalid action: {}", name,
                     ia.what());
      } catch (const std::exception& e) {
        std::println(stderr, "warning: execution '{}' dropped an action that threw: {}", name,
                     e.what());
      } catch (...) {
        std::println(stderr, "warning: execution '{}' dropped an action that threw an unknown type",
                     name);
      }

      executing_action_ = false;

      // Counted whether it ran cleanly, reported a dead binding, or threw: it came off the queue,
      // and the queue is what the notification is about.
      ++actions_run_;

      // The notification is raised without the lock: on_finished may call add_action(), which
      // takes it.
      bool drained = false;
      {
        std::lock_guard<std::mutex> lock(action_mutex_);
        drained = action_queue_.empty();
      }

      if (drained) {
        notify_finished();
      }
    }

    // Last, and part of the pass: an attachment is only ever reached through action_execute, which
    // is this function, so whoever drives this execution drives the ones attached to it.
    if (actuator_execute_.is_connected()) {
      actuator_execute_();
    }
  }

  /**
   * @brief Raises \ref on_finished, unless the pass ran nothing: an idle worker has finished
   * nothing.
   */
  void notify_finished() {
    if (actions_run_ == 0 || !on_finished) {
      return;
    }

    {
      // An action may queue another, so a pass that drained can leave more behind it. That is the
      // next batch, not the end of this one.
      std::lock_guard<std::mutex> lock(action_mutex_);
      if (!action_queue_.empty()) {
        return;
      }
    }

    on_finished();
  }

  /**
   * @brief Waits for a resident worker to finish, and takes its place in the same step.
   *
   * `running_` is what a worker owns for its whole life and clears last, so taking it with one
   * compare-exchange is what keeps two workers out of one execution - and out of one handshake,
   * which ~execution() reads to decide the object may be freed.
   */
  void wait_thread_to_finish() {
    auto resident = false;
    while (!running_.compare_exchange_weak(resident, true)) {
      resident = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void execute() {
    // The results belong to this run, not to this run and the last one.
    {
      std::lock_guard<std::mutex> lock(results_mutex_);
      results_.clear();
    }

    execute_actions();

    std::println("execution '{}' finishing thread", name);

    // Must stay last: ~execution() may free this object the moment it reads false.
    running_ = false;
  }

  void loop() {
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(action_mutex_);

        // Bounded: an attached execution has its own list and cannot notify this condition
        // variable.
        action_cv_.wait_for(lock, std::chrono::milliseconds(10),
                            [this] { return !action_queue_.empty() || !started_; });

        if (!started_ && action_queue_.empty()) {
          break;
        }
      }
      execute_actions();
    }

    // A last pass for the attachments; this execution's own list is already empty here.
    execute_actions();

    std::println("execution '{}' thread finished", name);

    // Must stay last: ~execution() may free this object the moment it reads false.
    running_ = false;
  }

  std::thread thread_;

  std::atomic_bool started_ = {false};
  std::atomic_bool running_ = {false};

  // Set by stop(), and the difference between "not started yet" and "finished for good": actions
  // may be queued before run()/start(), but not after stop().
  std::atomic_bool stopped_ = {false};

  // action_queue_, started_ and stopped_ are written by every thread that calls add_action() or
  // stop() and read by the worker; nothing touches them outside this mutex.
  mutable std::mutex action_mutex_;
  std::condition_variable action_cv_;

  std::deque<queued_action_t> action_queue_;

  /**
   * @brief The actuator type used for attachments: the one type here that does not depend on
   * actionT.
   *
   * \ref action_execute and \ref action_stop are std::function<void(void)> whatever actionT is,
   * which is what lets an attacher of any specialisation be named.
   */
  using void_actuator = actuator<std::function<void(void)>>;

  void_actuator actuator_execute_;
  void_actuator actuator_stop_;

  /**
   * @brief This execution's link in the attachment graph, and its liveness token.
   *
   * Two jobs in one object: it names the attacher, which \ref attach() walks up to find a cycle,
   * and it dies with this execution, so the std::weak_ptr an attacher holds expires exactly then.
   * An execution cannot take a std::weak_ptr to itself - it need not be owned by a std::shared_ptr
   * at all.
   *
   * Never null: it is created with the execution and never reset, and serves as its identity when
   * links are compared.
   */
  std::shared_ptr<attachment> attachment_lifetime_ = std::make_shared<attachment>();

  /**
   * @brief The attacher's own actuators, the ones holding this execution's two actions.
   *
   * Not held as an execution*, which would mean this specialisation only: an execution may be
   * attached by one of any specialisation, and a \ref void_actuator is the same type whatever
   * actionT is.
   *
   * @attention Raw, and safe only in company: followed once, by ~execution(), and only while
   * `attachment_lifetime_->attacher` has not expired - which is exactly while the attacher, and so
   * these actuators, are still there.
   */
  void_actuator* attacher_execute_ = nullptr;
  void_actuator* attacher_stop_ = nullptr;

  // std::vector cannot hold void type; use an arbitrary type e.g. int
  using resultT = std::conditional<std::is_void<typename actionT::result_type>::value, int,
                                   typename actionT::result_type>;

  /**
   * @brief The return values of the current run, in the order the actions ran.
   */
  std::vector<typename resultT::type> results_;

  /**
   * @brief Guards results_, and only that: the worker holds it for the push and nothing else.
   */
  mutable std::mutex results_mutex_;

  /**
   * @brief Whether the worker keeps what the actions return. Set by run(), cleared by start().
   *
   * Kept apart from `started_`: which worker is running is a different question from whether a run
   * hands anything back.
   */
  std::atomic_bool collecting_results_ = {false};

  /**
   * @brief Whether an action that has already left the queue is still running.
   *
   * The worker pops under action_mutex_ and runs with the lock released, leaving a window in which
   * the queue is empty and the execution is anything but idle. This is what closes it for
   * \ref is_busy().
   */
  std::atomic_bool executing_action_ = {false};

  //! The polls holding this execution's address, so ~execution() can take it out of each of them.
  std::vector<execution_poll*> polls_;
  mutable std::mutex polls_mutex_;

  /**
   * @brief How many of this execution's own actions the last \ref execute_actions() pass ran.
   *
   * On the object rather than returned, so \ref notify_finished() can be raised wherever the pass
   * was driven. Atomic because that need not be this execution's own worker: one driven through
   * \ref attach() runs on its attacher's.
   */
  std::atomic_size_t actions_run_ = {0};
};
}  // namespace async
}  // namespace untangle
