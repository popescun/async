# async.hpp — tasks on the queue, feature plan

**The queue gains a second kind.** `7e07a51` made the action queue an actuator and `77ba66a` made
the drain fire it in one call; this is the step after both. `action_actuator_` keeps its actions
exactly as they are and gains a **tasks** list beside them — an action bound to its arguments *and*
to the callback it must notify — reached through a new `add_task()`.

**Status (2026-09-25) — nothing written, and blocked on the actuator.** This is a feature plan, not
a fix plan. Claims marked **PROBED** were compiled and run on 2026-09-24/25; the rest are read-only
and say so.

**This is the second of three.** `actuator/todo/FEATURE_PLAN.md` holds the mechanism and must land
and be bumped first; `todo/FEATURE_PLAN.md` in `executor` holds the pool and the two cases that
close the whole chain. Steps are numbered per repo, as in `FIX_PLAN.md`; cross-repo references are
qualified ("the actuator's step 6").

**Baseline.** `985a991`, actuator at `a8b8b47`. Sites are line numbers at those commits.

## Why — what a queued action cannot tell you

`add_action()` (`:495-497`) binds the caller's pack into a nullary callable with `std::bind`, and
the drain invokes the batch with **no arguments** (`:750`). The actuator's action convention reads
its callback out of the invocation pack, so the pack read there is empty:

| Path | What the actuator is invoked with | Callback |
|---|---|---|
| `connect(action)` then `a(21, cb)` | `(21, cb)` | **fired** — 42 |
| `execution::add_action(action, 21, cb)` | `()` — `std::bind` sealed the pack at `:496` | **not fired** |

**PROBED.** That is the observation this whole chain started from, and it is worth being precise
about what it now means. **It is not a defect to be fixed here.** The convention is about invoking
an actuator directly with a pack; a queue has no pack to read from, and cannot acquire one — a
batch holds many queued calls each wanting a different callback, while `operator()` broadcasts a
single pack to all of them. **So a queued action does not notify, by nature, and `add_action()`
keeps that behaviour unchanged.** What was missing is a second kind of queue entry that carries its
own callback. That is a task.

**Decided 2026-09-25.** `add_action()` stays callback-free and fire-and-forget. Under the actuator's
step 2 a task's callback is a named parameter and is **not** forwarded to the action, so any
trailing callable still passed to `add_action()` is necessarily a real parameter of the action's own
signature — otherwise the call would not compile. **Nothing is silently swallowed any more**; the
action receives it and does with it what it likes. The reference owes one line saying `add_action()`
does not carry the trailing-callback convention, and pointing at `add_task()`.

## What this costs — and a correction to an earlier draft

An earlier version of this plan had the queue holding tasks *only*, `queued_action_t` deleted and
`action_actuator_` collapsing to `actuator<actionT>`. **That is withdrawn.** It was premised on one
kind of queue entry, and the decision that `add_action()` stays callback-free means there are two.
The actuator carries both lists natively, so this file changes far less than promised — and the
deletions it advertised do not happen:

| | Today | After |
|---|---|---|
| `queued_action_t` (`:660`) | the nullary carrier for a queued action | **kept, unchanged** |
| `action_actuator_` (`:918`) | `actuator<queued_action_t>` | same type, now also holding tasks — `task<R>` where `R` is `queued_action_t::result_type` |
| `add_action()` (`:495`) | `std::bind` into the actions list | **unchanged** |
| `add_task()` | — | new: `bind_task(action, args..., callback)` into the tasks list |
| the drain (`:750`) | `batch()` | `batch()`, then `batch.call_tasks()` |
| `in_the_batch` (`:744`) | `batch.actions.size()` | plus `batch.tasks.size()` |

**The lock dance at `:729-740` does not change.** The swap is about not holding `action_mutex_`
while entries run, not about ownership, and tasks need it for the same reason.

## Step index

| # | Step | Sites | Evidence |
|---|---|---|---|
| 1 | `add_task(action, args..., callback)`, onto `actuator::add_task()` | beside `:495`, `:697` | read-only |
| 2 | the drain fires tasks after actions, and counts both | `:744`, `:750`, `:765` | read-only |
| 3 | `has_pending_actions()` accounts for tasks | `:686`, `:735` | blocked on the actuator's step 6 |
| 4 | `is_busy()` and `on_finished` across two kinds | `:403`, `:823-840` | **read-only, and the one to think about** |
| 5 | the suite gains the queued-callback cases | `test/async_tests.cpp` | — |
| 6 | what a queued task promises, and that the last argument is the callback — the reference | `:474-492`, `:634`, `:651-655` | decision |
| 7 | `tools/make_doc.sh`, the actuator bump, and the bump `executor` takes | `doc/` | — |

### Step 1 · the new door

```c++
template <typename... Args>
bool add_task(actionT action, Args&&... args) {
  return add_queued_task(
      untangle::bind_task(std::move(action), std::forward<Args>(args)...));
}
```

**The callback is the last of `args`, and this signature never says so.** It cannot: a pack cannot
be followed by a deducible parameter, which is why `untangle::bind_task()` splits the last element
off itself. The split is paid once, there, and this door is a pure forward — the same shape
`add_action()` (`:495-497`) already has, one word apart. What the reference here owes is the rule the
signature cannot state: **the last argument is the callback**, and a missing or unusable one is a
`static_assert` inside `bind_task`, not a silent no-op.

`add_queued_task()` mirrors `add_queued_action()` (`:697`) exactly — the same `stopped_` check, the
same warning, the same `false`, the same `notify_one()`. Both answers mean what they meant before:
**a stopped execution refuses and drops**, and a dropped task never notifies, which is a thing the
reference has to say plainly now that a caller is relying on being told.

### Step 3 · blocked on the actuator's step 6

`has_pending_actions()` (`:686`) answers from `action_actuator_.is_connected()`, which reads
`actions` and `actions_map`. Once tasks are queued, a batch of tasks with no actions reports itself
empty and the drain `break`s at `:735` **with work still queued** — the worker then parks on
`action_cv_` and the tasks sit there until something else wakes it.

One line here, but which line depends on the actuator: `is_connected()` extended to include tasks,
or a new `has_tasks()` read alongside. **Do not write this step until that is settled.**

### Step 4 · what "busy" and "finished" mean with two kinds

The one place where two kinds is more than bookkeeping, and it is read-only so far:

- `is_busy()` (`:403`) answers from `executing_action_` and `has_pending_actions()`. It must be
  true while a task is in flight, or the pool above reads an idle worker and hands it more.
- `notify_finished()` (`:823-827`) suppresses `on_finished` when `actions_run_ == 0`. A pass that
  ran only tasks must not count as having run nothing.
- **A task's callback fires before `on_finished`**, inside `call_tasks()`, which is inside the
  drain. Two notifications with different meanings now exist on the same thread — one per task,
  one per drained queue — and the reference has to keep them apart.

### Step 5 · the cases

Two, mirroring the pool's: a task queued to a **running** execution notifies with its result, and a
task queued to a **stopped** one does not — `add_task()` answers `false` and nothing fires. Plus a
void-returning task notifying with `void()`, which is the half no pool case covers.

They belong here and not only in `executor`, because a caller using `execution` directly meets this
with no pool in sight.

### Step 6 · what the reference has to say

- **`add_action()` does not notify.** One line at `:474-492`, pointing at `add_task()`. It is the
  question every reader of the actuator's callback convention will arrive with.
- **`add_task()`'s last argument is the callback**, and the signature cannot say so because the pack
  runs to the end. It has to be said in prose, with the `void()` form for a void action named
  explicitly — a reader who has only seen the actions convention will expect a callback to be
  optional and to be recognised by its type, and here it is neither.
- **The callback runs on the worker's thread**, inside the drain, before `on_finished` (`:634`).
  The same warning `on_error` (`:651-655`) already carries, for the same reason.
- **A throwing callback reaches `on_error`**, because it runs inside the actuator's `try` and its
  exception lands in `batch.errors`, which `:769` walks into `report_error()`. So `on_error` can
  fire for a task whose body succeeded.
- **A task that throws does not notify. Finished does not mean failed**, decided 2026-09-25, and
  documented as a choice rather than an omission.
- **A refused task does not notify**, because it never ran.

## What is no longer a risk

An earlier draft carried a step marked *the one that can send the chain back*:
`bind_action_and_method()` (`:325`) and `bind_action_and_function()` (`:361`) hold their action in a
`std::shared_ptr<const actionT>`, while `bind_task()` takes the action by value and reads
`result_type` off it — so routing `add_action()` through `bind_task()` would have needed a wrapper
type naming `result_type`, and the shape of that wrapper could have forced the actuator's
`bind_task` signature to change after it had landed.

**It is gone.** `add_action()` no longer routes through `bind_task()` — it keeps `std::bind` and
`add_queued_action()` untouched — so the two bindings are not touched at all. Nothing in this repo
now constrains the actuator's steps 1 or 2.

**What it leaves behind is a question, not a risk:** there is no `bind_task_and_method()`. A caller
who wants a *bound* action to notify on completion has no door, and would need one built the way
these two are. Out of scope here; worth recording so it is a decision later rather than a surprise.

## Order

1 and 2 in sequence. 3 waits on the actuator's step 6. 4 is read first — it decides whether 1 and 2
are complete or merely compiling. 5 is the acceptance criterion for this repo; the pool's two cases
in `executor` are the one for the chain.

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

**NEXT: not here.** The actuator's step 1. Read step 4 before this repo's step 1 is written.
