# async.hpp — tasks on the queue, feature plan

**The queue gains a second kind.** `7e07a51` made the action queue an actuator and `77ba66a` made
the drain fire it in one call; this is the step after both. `action_actuator_` keeps its actions
exactly as they are and gains a **tasks** list beside them — an action bound to its arguments *and*
to the callback it must notify — reached through a new `add_task()`.

**Status (2026-09-25) — steps 1, 4, 5 and 6 are done, 62 of 62 green; steps 2 and 3 were merged
into step 1, and step 5 landed with it. Only step 7's commits are left.** The
actuator is closed and bumped, so nothing blocks this repo. This is a feature plan, not a fix plan.
Claims marked **PROBED** were compiled and run on 2026-09-24/25; the rest are read-only and say so.

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
| 1 ✅ | `add_task()`, `add_queued_task()`, the drain firing tasks, and `has_pending_actions_or_tasks()` | `:499-534`, `:719-729`, `:758-786`, `:836` | CONFIRMED (8 cases) — **DONE**, hash pending |
| 2 | — merged into step 1, see below | — | — |
| 3 | — merged into step 1, see below | — | — |
| 4 ✅ | `is_busy()` and `on_finished` across two kinds | `:816-818`, `:846`, `:903-926`, `:1076-1087` | CONFIRMED (4 cases) — **DONE**, hash pending |
| 5 | the suite gains the queued-callback cases — landed with step 1 | `test/async_tests.cpp` | ✅ 8 cases |
| 6 ✅ | what a queued task promises — the reference and `README.md` | `:203-207`, `:493-497`, `:683-689`, `:706-710` | transcription, nothing open — **DONE**, hash pending |
| 7 | `README.md`, `tools/make_doc.sh`, and the commits | `README.md`, `doc/` | **part done** — only the commits are left |

### Step 1 ✅ · the door, the drain and the predicate — DONE

`async.hpp:499-534` (`add_task()`), `:719-729` (`has_pending_actions_or_tasks()`), `:758-786`
(`add_queued_task()`), `:836` (the drain). Eight cases at `test/async_tests.cpp:440-657`. 58 of
58 green, clang-format clean, doxygen clean, `doc/refman.pdf` at 45 pages.

**Steps 1, 2 and 3 were merged, because they are one behaviour and not three.** The plan had them
separate and its own Order section half-knew better — "each is small and the file does not build
between them". Reading the drain first, as this plan instructed, showed the reason is worse than
compilation:

```c++
if (!has_pending_actions()) {   // step 3: actions only, so a queued task means "nothing here"
  break;
}
batch = std::move(action_actuator_);
...
batch();                        // step 2: fires actions, never tasks
```

After step 1 alone a queued task is invisible to the predicate, the loop breaks at once, the task
never runs, and `is_busy()` reports the execution idle. **Its only observable surface would have
been `add_task()`'s own `bool`** — and cases asserting that could not tell a working implementation
from one that drops the task on the floor, because both answer `true`. Three small pieces, one
review, because there is one claim: a task queued here runs and notifies.

**What landed:**

- `add_task(action, args..., callback)` — a pure forward to `untangle::bind_task()`, the same shape
  `add_action()` (`:495-497`) has, one word apart. The callback is the last of `args` and the
  signature cannot say so, so the documentation does.
- `add_queued_task()` — mirrors `add_queued_action()` (`:797`) with **one refusal more**: the
  actuator turns away a task with nothing to run or no callback to notify, and that answer is
  passed straight back rather than swallowed, under its own warning.
- `has_pending_actions_or_tasks()` — **renamed**, not merely extended. It answers two questions
  now, and a name saying only the first would have been the kind of thing a reader trusts and is
  wrong about. Seven call sites.
- The drain — `batch.call_tasks()` after `batch()`, inside the same `try`.

**Actions run before tasks within a batch. Decided 2026-09-25.** One pass fires every action the
actuator holds and then every task, so the two kinds do not interleave in the order they were
queued; across batches the queue is still first in, first out. `a_batch_runs_its_actions_before
_its_tasks` states it, with a message pointing at the drain as the place it would change.

> **It differs from what `executor`'s plan says of its own queue**, where first in, first out across
> both kinds is called "the behaviour, not an implementation detail". The two are different queues
> and may legitimately differ — the pool's `pending_` is one container it controls, while this is an
> actuator fired in two calls — but if they should agree, the change is here and in the actuator,
> and the actuator is closed. Worth settling before `executor`'s own steps are written.

**`actions_run_` is deliberately untouched**, which leaves step 4 a real defect rather than a
read-only note: it counts `batch.actions.size()`, so a pass that ran only tasks counts zero and
`notify_finished()` suppresses `on_finished` entirely.

### Steps 2 and 3 — merged into step 1

Kept as numbers so the Order section and the async entries in the other two plans still resolve.
What they were is in step 1: the drain firing tasks after actions, and the predicate it reads
seeing them. The actuator's step 6 unblocked the second, and `has_tasks()` is what it now calls.

### Step 4 ✅ · what "busy" and "finished" mean with two kinds — DONE

`async.hpp:816-818` (the count), `:846` (the add), `:903-926` (`notify_finished()`), `:1076-1087`
(the member). Four cases at `test/async_tests.cpp:660-746`. 62 of 62 green, clang-format clean,
doxygen clean, `doc/refman.pdf` at 45 pages.

**Three of the four cases passed before the fix**, which is what made them worth writing: they are
the constraints the fix had to respect, not the thing being fixed.

| Case | Before | What it holds the fix to |
|---|---|---|
| `reports_finishing_a_pass_that_ran_only_tasks` | **failed** | the defect itself |
| `reports_finishing_a_pass_of_both_kinds_once` | passed | not once per *kind* — the obvious wrong fix |
| `an_idle_continuous_worker_still_reports_nothing` | passed | zero still means zero — the rule the count exists for, which dropping it would break |
| `a_queued_task_makes_the_execution_busy` | passed | what step 1's rename gave, asserted nowhere until now |

So the shape was constrained from three sides at once: count both kinds, keep the total per *pass*,
and keep an idle worker silent.

**The fix is one line of arithmetic.** `const auto in_the_batch = batch.actions.size() +
batch.tasks.size();` — and both sizes have to be read at that point for different reasons: the
actions list shrinks *during* `operator()` when a dead binding is dropped, and the tasks list is
emptied outright by `call_tasks()`.

**`actions_run_` is renamed `actions_and_tasks_run_`**, on the precedent set by
`has_pending_actions_or_tasks()`: it decides `on_finished` for both kinds now, and a name saying
only the first is the kind a reader trusts and is wrong about. Private, five call sites.

**`is_busy()` needed nothing**, as the note above had already concluded — step 1's rename is what
made it see a queued task. `a_queued_task_makes_the_execution_busy` is there because that was true
by inheritance and asserted nowhere, and a later change to the predicate would otherwise break the
pool above in silence.

> **What the reference still owes, and it is step 6's**: a task's callback fires **before**
> `on_finished`, inside `call_tasks()`, inside the drain. Two notifications with different meanings
> now run on the same thread — one per task, one per drained queue — and nothing yet tells a caller
> which is which.

### Step 5 · the cases

Two, mirroring the pool's: a task queued to a **running** execution notifies with its result, and a
task queued to a **stopped** one does not — `add_task()` answers `false` and nothing fires. Plus a
void-returning task notifying with `void()`, which is the half no pool case covers.

They belong here and not only in `executor`, because a caller using `execution` directly meets this
with no pool in sight.

### Step 6 ✅ · what the reference has to say — DONE

`async.hpp:203-207` (the class doc), `:493-497` (`add_action()`), `:683-689` (`on_finished`),
`:706-710` (`on_error`), and `README.md` — a `## tasks: work that reports back` section plus the two
new warnings in *queueing and lifetime*. 62 of 62 green, clang-format clean, doxygen clean,
`doc/refman.pdf` at 47 pages.

**The Evidence column said "decision", and by the time this step was reached there was nothing left
to decide.** That label was accurate when the step was written — every rule below was open — and
stale by the time it was read. All of them had been settled in an earlier step, which is what a
documentation step should be: transcription, not choice.

| Rule | Settled in |
|---|---|
| `add_action()` does not notify | decided 2026-09-25 — `add_action()` stays callback-free |
| the last argument is the callback | decided 2026-09-25 — callback-last |
| finished does not mean failed | the actuator's step 4 |
| a throwing callback reaches `on_error` | the actuator's step 4 |
| a task's result goes only to its callback; errors are appended | the actuator's step 4 |
| `on_finished` fires for a task-only pass | this repo's step 4 |
| a refused task never notifies | this repo's step 1 |

**Worth keeping as method:** an Evidence column entry is a claim about the state of a step *when it
is read*, not when it is written. "decision" left standing after the decision was made reads as a
question waiting on the caller, and cost a round trip asking whether one was.

**What the header now says**, beyond the rules listed above:

- The class doc names the two kinds and why a task carries its own callback — a queued call has no
  invocation left for the actuator's convention to read one from.
- `add_action()` says it notifies nobody, **and** that a trailing callable passed to it is an
  ordinary argument of the action. That is the mistake a reader of the actuator's convention will
  arrive ready to make.
- `on_finished` says it is raised **once per pass, not once per piece of work**, that a task-only
  pass raises it like any other, and that it must not be read as "my task finished" — the task's own
  callback has already run by then, inside the same drain and on the same thread. That was the
  blockquote step 4 handed forward, and it is discharged.
- `on_error` says a task's failure arrives there, and so does its callback's — so it can fire for a
  task whose action **succeeded**: the work was done and only the telling failed.

**The README's *queueing and lifetime* section listed the stderr warnings for actions only**, so it
now carries `task not added` after `stop()` and `refused a task that cannot report`, the second with
the note that it is caught while the caller is still on the stack.

### Step 7 · the reference and the commits — PART DONE

**Done: `README.md` and `doc/refman.pdf`.** The reference was rebuilt at every step rather than once
at the end, so it never drifted — 45 pages when step 1 started, 47 now. `README.md` gained a tasks
section and the two new warnings, in step 6.

**Left: the commits.** Steps 1, 4, 5 and 6 are one tree across four files. A message covering them
is written and handed over; the shape is the same question the actuator answered by doing it —
one commit for the feature rather than one per step, because every step was reviewed at its **red
test** rather than at its commit.

**The bump `executor` takes was never this repo's, and listing it here was the same error the
actuator's step 7 made.** `executor` records async's commit in `executor`'s own tree, so moving that
pointer is a change to `executor`, made in `executor`, and its plan owns it. Struck from here rather
than tracked in two places — a step that waits on another repo's commit can never close on its own
terms.

> **Record the hash once, and only once the commit is final.** The actuator's plan had to write its
> hash three times: two amends rewrote the commit the plan was citing, and each rewrite left a dozen
> citations pointing at a commit no branch reached. A plan's own update belongs in a **later**
> commit, and the number belongs in that one.

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
| hash pending | 1 — `add_task()`, the drain, the predicate, and 8 cases (steps 2 and 3 merged in) |
| hash pending | 4 — both kinds counted, `actions_and_tasks_run_`, and 4 cases |
| hash pending | 6 — the reference and `README.md` |

**NEXT: `executor`, once these are committed.** Everything here but the commits is done. Its plan
opens on a naming decision — whether the pool's existing `add_task()` becomes `add_action()` so the
two repos read alike — and on a question this repo's step 1 raised: the pool's own queue is
documented as first in, first out across both kinds, while a pass here runs every action before any
task. The two are different queues and may differ, but it should be a decision rather than a
discovery.
