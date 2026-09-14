// Copyright (c) 2018 Nicolae Popescu. MIT License.

/**
 * @brief Interface to \ref untangle::async::execution class.
 */
#pragma once

#include <actuator/actuator.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <thread>
#include <vector>

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
 * @brief Invalid attachment exception.
 *
 * @remark Raised by \ref execution::attach() when the attachment asked for would close a cycle -
 * either between two executions that each attach the other, or an execution attached to itself.
 * Driving such a cycle recurses until the stack is gone, so it is refused where the caller can
 * still be told about it.
 *
 * @remark Deliberately not an untangle::invalid_action. That one reports a binding whose target has
 * died, and execution::execute_actions() swallows it by design; a cycle is a caller error and
 * must not be swallowed.
 */
struct invalid_attachment : std::exception {
  /**
   * @brief Construct a new invalid attachment object.
   *
   * @param text - A message text, describing the reason of this exception.
   */
  explicit invalid_attachment(std::string text) : message(std::move(text)) {}

  /**
   * @brief The message text describing the reason of this exception.
   *
   * @return const char* - The message text.
   */
  const char* what() const noexcept override { return message.c_str(); }

 private:
  std::string message;  //!< It holds the message text.
};

/**
 * @brief One execution's place in the attachment graph: who attached it, if anyone.
 *
 * Deliberately at namespace scope rather than nested in \ref execution, so that it is one type
 * rather than one per specialisation. An execution may be attached by an execution of any
 * specialisation, and the whole point of this record is that the chain can be walked without
 * knowing what any link's action type is.
 *
 * An execution holds one of these by std::shared_ptr and it dies with the execution, so it doubles
 * as the liveness token the attached side needs: a link whose weak reference still locks belongs to
 * an execution that is still there.
 */
struct attachment {
  //! The link of the execution that attached this one; empty when nothing has.
  std::weak_ptr<attachment> attacher;
};

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
    std::lock_guard<std::mutex> lock(actuator_mutex);

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
    std::lock_guard<std::mutex> lock(actuator_mutex);

    actuator_is_running.remove(&async_exec.action_is_running);
  }

  /**
   * @brief Checks if the execution objects are running.
   *
   * @return true - if at least one \ref execution object in this poll is running.
   */
  auto is_running() {
    std::lock_guard<std::mutex> lock(actuator_mutex);
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
  mutable std::mutex actuator_mutex;
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
  // An execution may attach one of a different specialisation - the smoke test attaches an
  // execution<function<void(int)>> to an execution<function<void(void)>> - and that is a different
  // class with no access to this one's members. attach() and detach() need to read and write
  // attachment_lifetime on it.
  template <typename otherActionT>
  friend class execution;

 public:
  /**
   * @brief Creates an execution owned by a std::shared_ptr, which is what binding to it requires.
   *
   * \ref bind_action_and_method() and \ref bind_action_and_function() take the execution as a
   * std::shared_ptr and hold it weakly, so an execution that is to be bound has to be created this
   * way. One built on the stack, as a data member, or through a bare new is still a usable
   * execution
   * - actions can be handed to \ref add_action() directly - it simply cannot be bound, and the
   * attempt does not compile.
   *
   * The arguments are forwarded to a constructor, so this factory does not have to be revisited
   * when one is added.
   *
   * @param args - Constructor arguments: a name, or nothing for the default name.
   * @return - A std::shared_ptr owning the new execution.
   */
  template <typename... Args>
  static std::shared_ptr<execution> create_instance(Args&&... args) {
    return std::make_shared<execution>(std::forward<Args>(args)...);
  }

  /**
   * @brief Constructs a new execution object with the default name. Call \ref create_instance()
   * instead.
   *
   * The name identifies an execution in the warnings it reports, and is otherwise unused. An
   * execution built without one keeps the default name rather than an empty one, so a warning
   * always names something.
   */
  execution() : execution(std::string(default_name)) {}

  /**
   * @brief Constructs a new named execution object.
   *
   * An execution constructed directly cannot be bound to - \ref bind_action_and_method() and
   * \ref bind_action_and_function() require a std::shared_ptr. Use \ref create_instance() for one
   * that is going to carry bound actions.
   *
   * @param exec_name - A name for this execution.
   */
  explicit execution(std::string exec_name) : name(std::move(exec_name)) {
    other_this = this;
    action_execute = untangle::bind(other_this, &execution<actionT>::execute_actions);
    action_stop = untangle::bind(other_this, &execution<actionT>::stop);
    action_is_running = untangle::bind(other_this, &execution<actionT>::is_running);
  }

  /**
   * @brief Destroys the execution object, once its worker has left.
   *
   * The worker is detached and cannot be joined, so this waits on the same "running" state that
   * \ref execution_poll reports - the only handle a detached worker offers. It returns at once for
   * an execution that was never started, or that the caller has already polled to a stop.
   *
   * @remark It first takes this execution out of whatever attached it, which is the inverse of
   * \ref attach() and the counterpart of the execution_poll::remove() below.
   *
   * @remark This is safe only because running is cleared as the very last thing the worker does.
   * Nothing may be added after it in execute() or loop(): the object can be freed the moment it
   * reads false.
   */
  ~execution() {
    // Out of the attacher first, before the worker is even asked to stop: from here on nothing
    // may reach this object, and the actions about to be destroyed are the ones it holds.
    if (!attachment_lifetime->attacher.expired()) {
      attacher_execute->remove(&action_execute);
      attacher_stop->remove(&action_stop);
    }

    {
      std::lock_guard<std::mutex> lock(action_mutex);
      started = false;
      stopped = true;
    }

    action_cv.notify_all();

    while (running.load()) {  // time of check
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // time of use: this object is freed once the destructor returns, so the check above is only
    // safe because running is the last thing the worker touches
    execution_poll::get().remove(*this);
  }

  /**
   * @brief Binds asynchronously an external action to a class function member.
   *
   * It creates an action as a binding to a class method (by untangle::bind()), and assigns to
   * \p action a callable that passes it to \ref add_action(). The execution is taken as a
   * std::shared_ptr and held as a std::weak_ptr, exactly as untangle::bind() holds the bound
   * object: the action can therefore outlive the execution and report a dead binding rather than
   * following a dangling reference.
   *
   * @attention Invoking \p action after the execution has been destroyed throws
   * untangle::invalid_action. execute_actions() catches it, so such an action is dropped with a
   * warning rather than ending the worker thread.
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
    actionT async_action = untangle::bind(obj, method);

    action = [wp = std::weak_ptr<execution>(async_exec),
              async_action](auto... args) -> actionT::result_type {
      // lock() also keeps the execution alive for the duration of the call
      const auto exec = wp.lock();
      if (!exec) {
        throw invalid_action("bind: invalid execution");
      }
      exec->add_action(async_action, args...);
      return typename actionT::result_type();
    };
  }

  /**
   * @brief Binds asynchronously an external action to a plain function.
   *
   * It wraps \p Fn in an action and assigns to \p action a callable that passes it to \ref
   * add_action(). The execution is held weakly, for the reason given on \ref
   * bind_action_and_method().
   *
   * @param action [in,out] - An action of type std::function<...>.
   * @param Fn - A plain function.
   * @param async_exec - A std::shared_ptr owning the execution the action is queued on.
   */
  template <typename T>
  static void bind_action_and_function(actionT& action, const T& Fn,
                                       const std::shared_ptr<execution>& async_exec) {
    actionT async_action = Fn;

    action = [wp = std::weak_ptr<execution>(async_exec),
              async_action](auto... args) -> actionT::result_type {
      const auto exec = wp.lock();
      if (!exec) {
        throw invalid_action("bind: invalid execution");
      }
      exec->add_action(async_action, args...);
      return typename actionT::result_type();
    };
  }

  /**
   * @brief Checks if this execution has finished.
   *
   * @return true - The execution has not finished.
   * @return false - The execution has finished.
   */
  /**
   * @brief Is this execution still working?
   *
   * False from the moment the worker starts reporting itself finished, which is before `running` is
   * cleared - the two answer different questions. This one is for callers, and says whether there
   * is still work going on; `running` is the handshake ~execution() waits on, and says whether the
   * worker is still touching this object. They were the same answer until the notification needed
   * to be told the truth about itself.
   */
  bool is_running() const { return running.load() && !finishing.load(); }

  void run() {
    running = true;
    finishing = false;
    collecting_results = true;
    this_thread = std::thread(&execution::execute, this);
    this_thread.detach();
  }

  void start() {
    running = true;
    finishing = false;
    collecting_results = false;
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

  /**
   * @brief Attaches another execution, so that this one triggers it.
   *
   * The attached execution's pending actions are run by this one's worker, and stopping this one
   * stops that one too.
   *
   * The actuators are given \p other's own actions, so the attachment is a pointer into \p other.
   * What makes that safe is the record left on the other side: \p other is told which actuators
   * hold it, and ~execution() takes itself back out of them. The attacher's lifetime token guards
   * those pointers, so an attached execution outliving its attacher follows nothing.
   *
   * @param other - A std::shared_ptr owning the execution to trigger. Requiring it here is what
   * keeps an execution that no std::shared_ptr owns from being attached at all, exactly as
   * \ref bind_action_and_method() requires one to bind. Attaching the same execution twice adds
   * it twice; a single \ref detach() still removes it completely, because
   * untangle::actuator::remove() erases every match.
   *
   * @throw invalid_attachment - if \p other is attached already. An execution has at most one
   * attacher, which is what keeps the attachment graph a forest and the cycle check below a walk
   * rather than a search.
   *
   * @throw invalid_attachment - if the attachment would close a cycle, which includes attaching an
   * execution to itself. Driving a cycle recurses until the stack is gone, so it is refused here,
   * where the caller still has a stack to be told on.
   */
  template <typename otherT>
  void attach(const std::shared_ptr<otherT>& other) {
    if (static_cast<const void*>(other.get()) == static_cast<const void*>(this)) {
      throw invalid_attachment("attach: an execution cannot be attached to itself");
    }

    if (!other->attachment_lifetime->attacher.expired()) {
      throw invalid_attachment("attach: that execution is attached already");
    }

    // Refusing an execution that has an attacher already leaves every execution with at most one,
    // so the graph is a forest and this new edge closes a cycle exactly when other is somewhere up
    // this execution's own chain of attachers. Walking it is the whole check.
    for (auto link = attachment_lifetime->attacher.lock(); link; link = link->attacher.lock()) {
      if (link == other->attachment_lifetime) {
        throw invalid_attachment("attach: the attachment would close a cycle");
      }
    }

    if (!actuator_execute.is_connected()) {
      actuator_execute = untangle::connect(other->action_execute);
    } else {
      actuator_execute.add(&other->action_execute);
    }

    if (!actuator_stop.is_connected()) {
      actuator_stop = untangle::connect(other->action_stop);
    } else {
      actuator_stop.add(&other->action_stop);
    }

    // What ~execution() needs to take itself back out of these actuators: the link proves this
    // attacher is still there, and the two pointers are only ever followed while it does.
    other->attachment_lifetime->attacher = attachment_lifetime;
    other->attacher_execute = &actuator_execute;
    other->attacher_stop = &actuator_stop;
  }

  /**
   * @brief Detaches an execution previously attached by \ref attach().
   *
   * Both paths are unwired, the triggering one and the stopping one: an attachment that could only
   * be half undone would leave this execution still able to stop one it no longer drives.
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
    const auto& actions = actuator_execute.actions;
    if (std::find(actions.begin(), actions.end(), &other.action_execute) == actions.end()) {
      return false;
    }

    actuator_execute.remove(&other.action_execute);
    actuator_stop.remove(&other.action_stop);

    other.attachment_lifetime->attacher.reset();
    other.attacher_execute = nullptr;
    other.attacher_stop = nullptr;

    return true;
  }

  /**
   * @brief The return values of the actions run by run(), in the order they ran.
   *
   * Read it from on_finished, which fires once the queue has drained and before the execution
   * reports itself finished, so the results are complete by the time the callback can see them.
   *
   * Returned by value, under results_mutex - the worker appends to the vector as it goes, so
   * handing out a reference would hand out something being written.
   *
   * @remark Only run() fills this. The continuous worker started by start() does not:
   * it has no point at which a run is over, so there is nothing to hand back and nowhere to clear,
   * and filling it would grow without bound. Deferred until there is a use case that wants it.
   *
   * @return The results of the most recent run, or empty if none has produced any.
   */
  auto results() const {
    std::lock_guard<std::mutex> lock(results_mutex);
    return results_;
  }

  static constexpr auto default_name = "default_name";  //!< Name of an execution built unnamed.

  std::function<void(void)> action_execute;
  std::function<void(void)> action_stop;
  std::function<bool(void)> action_is_running;
  std::function<void(void)> on_finished;
  std::string name = default_name;

 private:
  using queued_action_t = std::function<typename actionT::result_type(void)>;

  /**
   * @brief Runs one action, keeping its return value when the action type has one.
   *
   * The value is kept only while collecting_results is set, which run() does and start() clears.
   * The worker invokes actions with action_mutex released, and takes results_mutex only here, after
   * the action has returned - so an action that calls add_action() never meets this lock held.
   */
  void execute_action(queued_action_t& action) {
    if constexpr (std::is_void_v<typename actionT::result_type>) {
      action();
    } else {
      auto value = action();

      if (collecting_results) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results_.push_back(std::move(value));
      }
    }
  }

  /**
   * @brief Runs everything queued, and reports how much that was.
   *
   * The count is what tells loop() whether a batch actually drained or whether it just woke on the
   * 10ms tick with nothing to do - the difference between a notification worth sending and a
   * hundred a second saying nothing happened.
   *
   * @return The number of actions run, including any that reported a dead binding.
   */
  std::size_t execute_actions() {
    std::size_t actions_run = 0;

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
      } catch (const invalid_action& ia) {
        std::println(stderr, "warning: execution '{}' dropped an invalid action: {}", name,
                     ia.what());
      }

      // Counted whether or not it reported a dead binding: it came off the queue and the queue is
      // what the notification is about.
      ++actions_run;
    }

    if (actuator_execute.is_connected()) {
      actuator_execute();
    }

    return actions_run;
  }

  /**
   * @brief Tells the caller that the queue this worker was given has drained.
   *
   * @param actions_run - How many actions the pass ran. Nothing is reported for a pass that ran
   * none: an idle worker has not finished anything.
   */
  void notify_finished(std::size_t actions_run) {
    if (actions_run == 0 || !on_finished) {
      return;
    }

    {
      // An action may queue another, so a pass that drained can leave more behind it. That is the
      // next batch, not the end of this one.
      std::lock_guard<std::mutex> lock(action_mutex);
      if (!action_list.empty()) {
        return;
      }
    }

    on_finished();
  }

  void execute() {
    // The results belong to this run: an execution that is run twice reports the second run's
    // results, not both runs' appended together.
    {
      std::lock_guard<std::mutex> lock(results_mutex);
      results_.clear();
    }

    const auto actions_run = execute_actions();

    // Set before the callback, not after: the run is over by the time it is told so, and a callback
    // that asks is_running() has to be told the truth. `running` cannot be cleared here instead -
    // ~execution() waits on it and may free this object the moment it reads false, so it has to
    // stay the last thing this worker touches.
    finishing = true;

    // Before running is cleared, so a caller waiting on the poll cannot see the execution finish
    // and read the results before this has filled them.
    notify_finished(actions_run);

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

      notify_finished(execute_actions());
    }

    // What was queued before stop() still belongs to this execution; nothing can have been added
    // after it, because add_action() refuses once stopped. A batch is a batch whichever side of the
    // stop it drained on, so it is reported like any other; a stop with nothing left to run reports
    // nothing, because nothing finished.
    notify_finished(execute_actions());

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

  /**
   * @brief The actuator type used for attachments.
   *
   * Named because it is the one type here that does not depend on actionT: \ref action_execute and
   * \ref action_stop are std::function<void(void)> whatever this execution's action type is. That
   * is what lets \ref attacher_execute and \ref attacher_stop point at an attacher of any
   * specialisation.
   */
  using void_actuator = actuator<std::function<void(void)>>;

  void_actuator actuator_execute;
  void_actuator actuator_stop;

  /**
   * @brief This execution's link in the attachment graph, and its liveness token.
   *
   * Two jobs in one object. `attachment_lifetime->attacher` is the execution that attached this
   * one, which \ref attach() walks up to find a cycle and ~execution() reads to know whether it is
   * still attached to anything. And because it dies with this execution, the std::weak_ptr an
   * attacher holds to it expires exactly then - an execution cannot take a std::weak_ptr to itself,
   * since it need not be owned by a std::shared_ptr at all and \ref bind_action_and_method()
   * deliberately kept it that way.
   *
   * It also serves as this execution's identity when comparing links, which is why it is never
   * null: it is created with the execution and never reset.
   */
  std::shared_ptr<attachment> attachment_lifetime = std::make_shared<attachment>();

  /**
   * @brief The attacher's own actuators, the ones holding this execution's two actions.
   *
   * The attacher is *not* held as an execution*: that would mean execution<actionT>*, the same
   * specialisation as this one, and an execution may be attached by one of any specialisation -
   * the smoke test attaches an execution<function<void(int)>> to an execution<function<void()>>.
   * A \ref void_actuator is the same type whatever actionT is, which is what makes these two able
   * to name an attacher of any kind.
   *
   * Raw, and safe only in company: they are followed just once, by ~execution(), and only while
   * `attachment_lifetime->attacher` has not expired - which is exactly while the attacher, and
   * therefore the actuators that are its members, are still there.
   */
  void_actuator* attacher_execute = nullptr;
  void_actuator* attacher_stop = nullptr;

  // std::vector cannot hold void type; use an arbitrary type e.g. int
  using resultT = std::conditional<std::is_void<typename actionT::result_type>::value, int,
                                   typename actionT::result_type>;

  /**
   * @brief The return values of the current run, in the order the actions ran.
   *
   * A vector rather than the single value this used to be: one value meant each action overwrote
   * the one before it, so a run of three actions reported one result and lost two. It also has no
   * uninitialised state to read - the single value had no initialiser, and reading it before any
   * action had run was undefined behaviour for every scalar result type.
   */
  std::vector<typename resultT::type> results_;

  /**
   * @brief Guards results_, and only that.
   *
   * Separate from action_mutex on purpose: the queue and the results are two different things, and
   * the worker holds this one only for the push, after an action has returned.
   */
  mutable std::mutex results_mutex;

  /**
   * @brief Whether the worker keeps what the actions return.
   *
   * Set by run() and cleared by start(), rather than inferred from `started`: which
   * worker is running is not the same question as whether a run is going to hand anything back, and
   * reading one as the other would be a trap for whoever changes the other next.
   */
  std::atomic_bool collecting_results = {false};

  /**
   * @brief Set by the worker once it is reporting itself finished, and read only by is_running().
   *
   * It exists because the worker cannot clear `running` before the callback - ~execution() waits on
   * that and may free the object the moment it reads false - and yet the callback must not be told
   * the execution is still working. Splitting the two answers is what lets the notification fire
   * while the object is still guaranteed to be there.
   */
  std::atomic_bool finishing = {false};

  execution* other_this;
};
}  // namespace async
}  // namespace untangle
