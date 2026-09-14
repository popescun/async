# prototypes

Throwaway code that answers a design question. Nothing here is part of the library, nothing here is
built by `test/CMakeLists.txt`, and nothing here should be included from `async.hpp`.

Build one by hand:

```sh
clang++ -std=c++23 -g -O0 -I. -Iprototypes prototypes/executor_demo.cpp -o /tmp/executor_demo
```

---

## executor.hpp — a thread pool built out of `execution` objects

**Question:** can `execution` serve as the worker in a pool, with a shared queue feeding whichever
worker frees up next?

**Answer: yes, and the prototype works** — with one addition to the header, `is_busy()`, which
came out of this exercise and is described under gap 3. 200 tasks over 4 workers all ran exactly once, spread
100/100/100/100; submission order is preserved; 4×150ms tasks on 4 workers finish in 156ms against
~600ms serial; 400 tasks submitted from 4 threads at once all ran. Clean under AddressSanitizer.

Four gaps turned up. One is now closed, and one of the rest is a blocker.

### The design

N executions in continuous mode, plus a shared `std::deque` of tasks. A task goes straight to a free
worker **only when the shared queue is empty**; otherwise it joins the back of the queue. Nothing
overtakes — finding a free worker does not let a task jump a queue that already has work in it,
which is the rule this pool was asked for.

Workers pull rather than being pushed to: when one reports its queue drained, it takes the front of
the shared queue. That is what produced the even spread above, with no scheduling logic of its own.

### Gap 1 — a task that throws takes the process down *(blocker, and not in the fix plan)*

`execute_actions()` catches `invalid_action` and nothing else, so any other exception leaves the
worker thread and calls `std::terminate`. Probed: a task throwing `std::runtime_error` gives
`libc++abi: terminating due to uncaught exception`.

That was tolerable while actions were bindings the header itself made. A pool runs arbitrary caller
code, and arbitrary code throws. **A pool cannot ship until this is handled** — the worker has to
survive a failed task, and the failure has to reach whoever submitted it.

Step 2 closed the `invalid_action` half of this and is not at fault; the remaining half only becomes
urgent once the actions are not the library's own.

### Gap 2 — the header's `std::cout` races, and a pool makes it certain *(step 18)*

Every ThreadSanitizer report from the demo was `async.hpp:690`, the `"thread finished"` print, with N
workers writing at once on shutdown. Removing just those two prints in a scratch copy of the header
makes the whole demo **TSan-silent** — so the executor itself is clean and the races are entirely
the header's.

With one or two executions this is a latent nuisance. With a pool it is guaranteed.

### Gap 3 — an execution could not say whether it was free *(closed: `is_busy()`)*

There was no way to ask. `is_running()` reports the *worker thread*, which in continuous mode is
true from `start()` until after `stop()` whether or not there is anything to do, and `action_list`
is private. So the first version of this pool kept a `busy` flag per worker and a `busy_count_`,
booked at dispatch and cleared in the callback — the pool's belief about its workers rather than
anything read back from them.

`execution::is_busy()` replaced all of it. The flag, the count and the bookkeeping are gone; the
pool asks, and the answer cannot drift from the truth.

It is not simply a test for an empty list, which is what makes it worth having in the header rather
than approximated here. The worker pops an action under `action_mutex` and runs it with the lock
released, so there is a window in which the list is empty and the execution is anything but idle. A
list-only check reports such a worker free and invites the pool to pile more on it. `is_busy()`
covers the window with `executing_action`, set under the same lock that empties the list.

`on_finished` firing per drained batch — step 16 — is still what tells the pool *the moment* a
worker frees up, so nothing has to poll.

### Gap 4 — a refused action is lost silently *(step 21)*

`add_action()` returns `void` and prints to stderr when the execution is stopped, so a dispatch that
races shutdown is dropped with nothing to tell the caller. The prototype dodges this by draining
before it stops anything, but that is the prototype being careful, not the interface being safe.

### Not problems, measured rather than assumed

- **The 10ms tick is cheap.** Eight idle workers burned 8ms of CPU over a second of wall clock. The
  bounded wait that step 7 introduced does not make an idle pool expensive.
- **Load balance needs no help.** Pulling from a shared queue spread 400 tasks 100/100/100/100.
- **Concurrent submission is fine.** 400 tasks from 4 threads, all accounted for.

### Deliberately not addressed

Priorities, which the pool was always going to want later; results, which a continuous worker does
not collect by design, so a task wanting to return something carries its own channel; and
`on_finished` being a single slot, which the pool takes over, leaving the caller without one.
