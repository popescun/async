# async.hpp — the queue on tasks, feature plan

**The action queue becomes a task queue.** `7e07a51` made the queue an actuator and `77ba66a` made
the drain fire it in one call; this is the step after both. `queued_action_t` — the nullary carrier
this file hand-rolled because the queue needed one with a `result_type` — is replaced by the
actuator's own `task<R>`, and `add_action()` becomes a delegation to `untangle::bind_task()`.

**Status (2026-09-25) — nothing written, and blocked on the actuator.** This is a feature plan, not
a fix plan. It has one defect at its root: the actuator's callback convention does not survive
being queued **here**, and the pool downstream only inherits what this file does. Claims marked
**PROBED** were compiled and run on 2026-09-24/25; the rest are read-only and say so.

**This is the second of three.** `actuator/todo/FEATURE_PLAN.md` holds the mechanism and must land
and be bumped first; `todo/FEATURE_PLAN.md` in `executor` holds the pool and the two cases that
close the whole chain. Steps are numbered per repo, as in `FIX_PLAN.md`; cross-repo references are
qualified ("the actuator's step 3").

**Baseline.** `985a991`, actuator at `a8b8b47`. Sites are line numbers at those commits.

## Why — this file is where the callback dies

`add_action()` (`:495-497`) binds the caller's pack into a nullary callable with `std::bind`, and
stores it as `queued_action_t` (`:660`). The drain then invokes the batch with **no arguments**
(`:750`). The actuator's convention reads its callback out of the invocation pack, so the pack it
reads is empty and the callback is never fired:

| Path | What the actuator is invoked with | Callback |
|---|---|---|
| `connect(action)` then `a(21, cb)` | `(21, cb)` | **fired** — 42 |
| `execution::add_action(action, 21, cb)` | `()` — `std::bind` sealed the pack at `:496` | lost |

**PROBED.** The action runs and the callback is silently skipped. Nothing warns: under the
convention the callback is also a legitimate argument of the action, so every overload is satisfied.
**A caller using `add_action()` directly has this today, with no pool in sight** — which is why
step 6 puts the cases in this suite and not only in the pool's.

**Why it cannot be fixed here alone.** `std::bind` returns an opaque object; once the pack is inside
it, nothing downstream can find the callback again — so the extraction has to happen at the moment
of binding, which is `bind_task()`'s whole job. And the actuator cannot fire it from its own
invocation pack, because a batch holds many queued calls each with a different callback while
`operator()` broadcasts one pack to all of them. The callback must travel **with** its action. That
is the actuator's steps 1 to 5.

## What this file gives up, and what it gets

**This is where the simplification lands.** `queued_action_t` exists only because the queue needed a
nullary carrier naming a `result_type` that C++20 removed from `std::function`. `task<R>` is that
carrier:

| Today | After |
|---|---|
| `using queued_action_t = std::function<R(void)>` (`:660`) | deleted |
| `actuator<queued_action_t> action_actuator_` (`:918`) | `actuator<actionT>` — the same template argument as everything else in the class |
| `actuator<queued_action_t> batch` (`:729`) | `actuator<actionT> batch` |
| `add_queued_action(std::bind(...))` (`:496`) | `add_queued_action(untangle::bind_task(...))` |
| `batch()` (`:750`) | `batch.call_tasks()` |
| `batch.actions.size()` (`:744`) | `batch.tasks.size()` |

**The lock dance at `:729-740` does not change.** The swap is about not holding `action_mutex_`
while actions run, not about ownership, and tasks need it for the same reason.

## Step index

| # | Step | Sites | Evidence |
|---|---|---|---|
| 1 | `queued_action_t` deleted, `action_actuator_` becomes `actuator<actionT>` | `:660`, `:918`, `:729` | read-only |
| 2 | `add_action()` delegates to `bind_task()`; `add_queued_action()` takes a task | `:495-497`, `:697` | read-only |
| 3 | `execute_actions()` fires `call_tasks()`, counts `tasks.size()` | `:744`, `:750`, `:765` | read-only |
| 4 | `has_pending_actions()` reads tasks | `:686` | blocked on the actuator's step 7 |
| 5 | `bind_action_and_method()` / `_function()` onto `bind_task()` | `:325-338`, `:361-374` | **the risk — read first** |
| 6 | the suite gains the two callback cases | `test/async_tests.cpp` | — |
| 7 | what a queued callback promises — the reference | `:634`, `:651-655` | decision |
| 8 | `tools/make_doc.sh`, the actuator bump, and the bump `executor` takes | `doc/` | — |

### Step 4 · blocked on the actuator's step 7

`has_pending_actions()` (`:686`) answers from `action_actuator_.is_connected()`, which reads
`actions` and `actions_map`. After step 1 the queue holds tasks and no actions, so left alone **a
queue full of tasks reports itself empty and the drain breaks** — it would `break` out of the loop
at `:735` with work still queued.

The fix is one line here, but which line depends on a decision in the actuator: extend
`is_connected()` to include tasks, or add `has_tasks()` and read that instead. **Do not write this
step until that is settled.**

### Step 5 · the one place this is not a simplification — READ BEFORE THE ACTUATOR'S STEP 3

`bind_action_and_method()` (`:325`) and `bind_action_and_function()` (`:361`) hold the action in a
`std::shared_ptr<const actionT>` so that a queued call costs a pointer rather than a copy of the
action, and hand `add_queued_action()` a lambda that captures it:

```c++
exec->add_queued_action([async_action, ... args = std::forward<decltype(args)>(args)] {
  return (*async_action)(args...);
});
```

`bind_task()` takes the action **by value** and reads `action_t::result_type` off it. These two
therefore need a small wrapper that owns the `shared_ptr` and names `result_type` — the only step in
this plan that adds a type rather than removing one.

**This is what can send the whole chain back.** If the wrapper turns out to constrain `bind_task`'s
signature, the actuator's step 3 is rewritten after it has landed. Settle the shape of the wrapper
here *before* the actuator writes `bind_task`, not after.

A second question rides along: these two bindings queue directly through `add_queued_action()` and
never see a trailing callback, because the action they queue is `(*async_action)(args...)` with the
caller's own pack. Whether a bound action should carry the convention at all is open — **it is not
a regression either way**, since it does not work today, but it should be answered rather than
inherited.

### Step 7 · what the reference has to say

Three rules, all decided in the actuator's step 5, all needing a sentence here because this is
where a caller meets them:

- **The callback runs on the worker's thread**, inside the drain, before `on_finished` (`:634`) and
  before the batch's results reach `results_`. The same warning `on_error` (`:651-655`) already
  carries.
- **A throwing callback reaches `on_error`**, because it runs inside the actuator's `try` and its
  exception lands in `batch.errors`, which `:769` walks into `report_error()`. So `on_error` can
  fire for an action whose body succeeded.
- **An action that throws gets no callback**, because no result exists to report.

## Order

1 to 3 in sequence — each is small and the file does not build between them. 4 waits on the
actuator's step 7. **5 is read first and settled first**, though it is written last. 6 is the
acceptance criterion for this repo; the pool's two cases in `executor` are the one for the chain.

## Working method

Inherited from `FIX_PLAN.md`, unchanged. **Each step is a test first**: a case that shows what is
missing, put up for review on its own, and the code written only once the case is agreed. The test
is the unit of review, not the implementation. **One step per commit**, and a step's case travels
with its own fix. The plan's own updates are their own commit, and always a later one.

**Every header change is followed by `tools/make_doc.sh`.**

## Progress

| Commit | Step |
|---|---|
| — | nothing landed, and nothing can until the actuator is green and bumped |

**NEXT: read step 5 and settle the wrapper**, because the actuator cannot write `bind_task` without
it.
