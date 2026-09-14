// Copyright (c) 2018 Nicolae Popescu. MIT License.

/**
 * @brief PROTOTYPE: a thread pool executor built out of \ref untangle::async::execution objects.
 *
 * Not part of the library. It exists to answer whether execution is a usable building block for a
 * pool, and to make the gaps concrete rather than hypothetical. See prototypes/README.md.
 *
 * The pool holds N executions in continuous mode and a shared queue of tasks. A task is handed
 * straight to a free worker only when the shared queue is empty; otherwise it goes to the back of
 * the queue, and a worker takes the front of the queue as it frees up. Nothing overtakes: finding a
 * free worker does not let a task jump a queue that already has work in it.
 */
#pragma once

#include <async.hpp>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace untangle {
namespace async {
namespace prototype {

/**
 * @brief Runs tasks on a fixed pool of \ref execution workers.
 *
 * @remark Tasks are std::function<void(void)>. An execution's action type is fixed by its template
 * argument, so one pool runs one signature; a task that needs to return something carries its own
 * channel, because a continuous worker does not collect results.
 */
class executor {
 public:
  using task_t = std::function<void(void)>;

  /**
   * @brief Starts \p worker_count executions in continuous mode.
   */
  explicit executor(std::size_t worker_count) {
    workers_.reserve(worker_count);

    for (std::size_t index = 0; index < worker_count; ++index) {
      worker next;
      next.exec = worker_execution::create_instance("pool_worker_" + std::to_string(index));

      // The only signal an execution gives that it has run out of work. There is no "ask" - see
      // the note on free_workers_ below - so the pool has to be told, and this is the telling.
      next.exec->on_finished = [this, index] { take_next_task(index); };

      workers_.push_back(std::move(next));
      workers_.back().exec->start();
    }
  }

  /**
   * @brief Drains what has been submitted, then stops every worker.
   */
  ~executor() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      accepting_ = false;
      drained_cv_.wait(lock, [this] { return pending_.empty() && busy_count_ == 0; });
    }

    // Outside the lock: stop() wakes each worker, and a worker waking up runs on_finished, which
    // wants this mutex.
    for (auto& one : workers_) {
      one.exec->stop();
    }

    // Releasing the last shared_ptr runs ~execution(), which waits for the detached worker.
    workers_.clear();
  }

  /**
   * @brief Queues a task, or hands it to a worker that has nothing to do.
   *
   * @return true - the task was accepted. false - the pool is shutting down and refused it.
   */
  bool submit(task_t task) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!accepting_) {
      return false;
    }

    // The queue comes first whenever it has anything in it: a task that arrives while others are
    // waiting does not get to overtake them just because a worker happens to be free.
    if (pending_.empty()) {
      for (std::size_t index = 0; index < workers_.size(); ++index) {
        if (!workers_[index].busy) {
          give_to_worker(index, std::move(task));
          return true;
        }
      }
    }

    pending_.push_back(std::move(task));
    return true;
  }

  /**
   * @brief How many tasks are waiting for a worker.
   */
  std::size_t pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
  }

 private:
  using worker_execution = execution<std::function<void(void)>>;

  struct worker {
    std::shared_ptr<worker_execution> exec;

    /**
     * @brief Whether this worker has been given a task it has not reported finishing.
     *
     * The pool keeps this because an execution cannot be asked. is_running() reports the worker
     * thread, which in continuous mode is true from start() until after stop() whether or not
     * there is anything to do, and the action list is private with no accessor. So "free" is a
     * fact about the pool's dispatching, not a fact read back from the execution.
     */
    bool busy = false;
  };

  /**
   * @brief Hands one task to a worker and books it as busy. Call with the mutex held.
   */
  void give_to_worker(std::size_t index, task_t task) {
    workers_[index].busy = true;
    ++busy_count_;

    // add_action() takes the execution's own action_mutex while this holds mutex_. That is only
    // safe in one direction, and it holds: a worker calls on_finished with action_mutex released,
    // so it never takes mutex_ while holding action_mutex, and the two never form a cycle.
    workers_[index].exec->add_action(std::move(task));
  }

  /**
   * @brief Called on the worker's own thread once its queue has drained.
   */
  void take_next_task(std::size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);

    workers_[index].busy = false;
    --busy_count_;

    if (!pending_.empty()) {
      task_t next = std::move(pending_.front());
      pending_.pop_front();
      give_to_worker(index, std::move(next));
      return;
    }

    if (busy_count_ == 0) {
      drained_cv_.notify_all();
    }
  }

  mutable std::mutex mutex_;
  std::condition_variable drained_cv_;

  std::deque<task_t> pending_;  //!< Tasks waiting for a worker, in the order submitted.
  std::vector<worker> workers_;
  std::size_t busy_count_ = 0;
  bool accepting_ = true;
};

}  // namespace prototype
}  // namespace async
}  // namespace untangle
