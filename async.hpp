/**
 * @brief Interface to \ref untangle::async::execution class.
 *
 * @file async.hpp
 * @author Nicu Popescu
 * @date 2018-05-10
 */
#pragma once

#include <atomic>
#include <thread>

#include <actuator.hpp>

namespace untangle
{
  template<typename actionT>
  struct actuator;

 namespace async
 {
  template<typename actionT>
  class execution;
 }
}

namespace untangle
{
namespace async
{
  /**
   *  @defgroup untangle_functions namespace untangle: functions
   */

  /**
   * @brief Asynchronous binding to a class function member.
   *
   * It uses an \ref execution object to execute the bound class function member on other thread.
   * It creates an action as a binding to a class method(by \ref untangle::bind()), and returns a callable that passes this action to \ref execution::add_action()).
   *
   * @param obj - A std::shared_ptr that wraps the bound class object.
   * @param classT::*method - Pointer to function member. It is specified as &<class type>::<function member>.
   * @param asyncexec - An \ref execution object.
   * @return actionT - A std::function(lambda) that adds the action to the execution object's actions list.
   *
   * @ingroup untangle_functions
   */
  template <typename classT, typename T, typename actionT = std::function<T>>
  actionT bind(const std::shared_ptr<classT>& obj, T classT::*method, execution<actionT>& asyncexec)
  {
    actionT async_action = untangle::bind(obj, method);

    return [&asyncexec, async_action](auto... args) -> typename actionT::result_type
    {
      asyncexec.add_action(async_action, args...);
      return typename actionT::result_type();
    };
  }

  /**
   * @brief Asynchronous binding to a plain function.
   *
   * It uses an \ref execution object to execute the bound function on other thread.
   * It creates an action as std::function that wraps the function, and returns a callable that passes this action to \ref execution::add_action()).
   *
   *
   * @param Fn - A plain function.
   * @param asyncexec - An \ref execution object.
   * @return actionT - A std::function(lambda) that adds the action to the execution object's actions list.
   *
   * @ingroup untangle_functions
   */
  template <typename T, typename actionT = std::function<T>>
  actionT bind(T& Fn, execution<actionT>& asyncexec)
  {
    actionT async_action = Fn;
    return [&asyncexec, async_action](auto... args) -> typename actionT::result_type
    {
      asyncexec.add_action(async_action, args...);
      return typename actionT::result_type();
    };
  }

  /**
   * @brief Execution poll class.
   *
   * A single-tone class that may be used to verify if the polled \ref execution objects have finished their processing. If at least one \ref execution object is running then this poll has the "running" state.
   *
   * @remark An \ref execution object runs in a detached thread, hence it is not joinable.
   * The caller can wait for an \ref execution to be finished only by checking the "running" state(\ref execution::isrunning()).
   * This class represents a convenient way to check any number of \ref execution objects.
   *
   */
  class execution_poll
  {
  public:
    /**
     * @brief Adds an \ref execution object to the poll
     *
     * @param aysncexec An \ref execution object.
     */
    template<typename asyncexecT>
    void add(asyncexecT& aysncexec)
    {
      if (!actuator_isrunning.is_connected())
      {
        actuator_isrunning = untangle::connect(aysncexec.action_isrunning);
      }
      else
      {
        actuator_isrunning.add(&aysncexec.action_isrunning);
      }
    }

    /**
     * @brief Checks if the execution objects are running.
     *
     * @return true - if at least one \ref execution object in this poll is running.
     */
    auto isrunning()
    {
      actuator_isrunning();

      auto result = false;
      for (const auto& ret : actuator_isrunning.results)
      {
        result |= ret;
      }
      return result;
    }

    /**
     * @brief Gets the single-tone instance.
     *
     * @return execution_poll An \ref execution_poll instance.
     */
    static execution_poll& get()
    {
      static execution_poll instance;
      return instance;
    }

    private:
      execution_poll() = default;
      ~execution_poll() = default;
      untangle::actuator<std::function<bool(void)>> actuator_isrunning;
  };

  /**
   * @brief Asynchronous template execution class.
   *
   * An async execution provides a mechanism to queue actions and execute them sequentially on a separate thread.
   * It may also attach another \ref execution object and trigger its actions. This way actions of different types may be executed on the same thread.
   *
   * The mechanism relies on an "asynchornous binding" created by using \ref bind().
   *
   * @actionT It represents the type of the action. It is specified as std::function<...> and should match the signature of the bound function or class method.
   */
  template<typename actionT>
  class execution
  {
  public:
    /**
     * @brief Constructs a new execution object.
     *
     */
    execution()
    {
      other_this = this;
      action_execute = untangle::bind(other_this, &execution<actionT>::execute_actions);
      action_stop = untangle::bind(other_this, &execution<actionT>::stop);
      action_isrunning = untangle::bind(other_this, &execution<actionT>::isrunning);
    }
    ~execution(){}

    /**
     * @brief Binds asynchronously an external action to a class function member.
     *
     * The binding is done by using a \ref bind().
     *
     * @param action [in,out] - An action of type std::function<...>.
     * @param obj - A std::shared_ptr that wraps the bound class object.
     * @param classT::*method - Pointer to function member. It is specified as &<class type>::<function member>.
     */
    template<typename classT, typename T>
    void bind_action_and_method(actionT& action, const std::shared_ptr<classT>& obj, T classT::*method)
    {
      action = bind(obj, method, *this);
    }

    template<typename T>
    void bind_action_and_function(actionT& action, const T& Fn)
    {
      action = bind(Fn, *this);
    }

    /**
     * @brief Checks if this execution has finished.
     *
     * @return true - The execution has not finished.
     * @return false - The execution has finished.
     */
    bool isrunning()
    {
      return running.load();
    }

    void run()
    {
      running = true;
      this_thread = std::thread(&execution::execute, this);
      this_thread.detach();
    }

    void start()
    {
      running = true;
      this_thread = std::thread(&execution::loop, this);
      this_thread.detach();
    }

    void stop()
    {
      while (!action_list.empty())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      if (actuator_stop.is_connected())
      {
        actuator_stop();
      }

      started = false;
    }

    /**
     * @brief
     *
     * @tparam Args
     * @param action
     * @param args
     */
    template <typename ...Args>
    void add_action(actionT action, Args... args)
    {
      action_list.push_back(std::bind(action, args...));
    }

    template<typename otherT>
    void attach(otherT& other)
    {
      if (!actuator_execute.is_connected())
      {
        actuator_execute = untangle::connect(other.action_execute);
      }
      else
      {
        actuator_execute.add(&other.action_execute);
      }

      if (!actuator_stop.is_connected())
      {
        actuator_stop = untangle::connect(other.action_stop);
      }
      else
      {
        actuator_stop.add(&other.action_stop);
      }
    }

    auto result()
    {
      return _result;
    }

    std::function<void(void)> action_execute;
    std::function<void(void)> action_stop;
    std::function<bool(void)> action_isrunning;
    std::function<void(void)> onfinished;
    std::string name;

  private:
    // SFINAE by return (void)
    template<typename T = actionT>
    typename std::enable_if_t<std::is_void<typename T::result_type>::value, typename T::result_type> select_execute_actions()
    {
      action_list.front()();
    }

    // SFINAE by return (non void)
    template<typename T = actionT>
    typename std::enable_if_t<!std::is_void<typename T::result_type>::value, typename T::result_type> select_execute_actions()
    {
      _result = action_list.front()();

      return typename T::result_type();
    }

    void execute_actions()
    {
      while (!action_list.empty())
      {
        select_execute_actions();

        action_list.pop_front();
      }

      if (actuator_execute.is_connected())
      {
        actuator_execute();
      }
    }

    void execute()
    {
      execute_actions();

      if (onfinished)
      {
        onfinished();
      }

      std::cout << "finishing thread" << std::endl;

      running = false;
    }

    void loop()
    {
      started = true;

      while (started.load())
      {
        execute_actions();
      }

      running = false;

      std::cout << "thread finished" << std::endl;
    }

    std::thread this_thread;

    std::atomic_bool started = {false};
    std::atomic_bool running = {false};

    std::list<std::function<typename actionT::result_type(void)>> action_list;

    untangle::actuator<std::function<void(void)>> actuator_execute;
    untangle::actuator<std::function<void(void)>> actuator_stop;

    // std::vector cannot hold void type; use an arbitrary type e.g. int
    using resultT = std::conditional<std::is_void<typename actionT::result_type>::value, int , typename actionT::result_type>;
    typename resultT::type _result;

    execution<actionT>* other_this;
  };
}
}
