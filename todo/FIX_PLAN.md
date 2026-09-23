# async.hpp — fix plan

**Status (2026-09-23) — reopened by steps 39-47**, all of which have landed; group 9
holds it. Before it: **closed again by step 38.** 37 of 38 steps done, and one
part-done: 1 to 34 landed or declined with the reasoning recorded, 35 part-done (A landed; B and C
measured and deferred), 36 and 37 landed, and **38 answers the question steps 21 and 28 left open**
— what the caller is told when an action fails. It came back from the executor repo, which needed
it to close its own step 5. The
per-step commits are in the table under **Progress**; this line no longer restates them, because
that is how it kept drifting.
**Tests:** 46 of 46 green in Debug, under AddressSanitizer and under ThreadSanitizer, measured at
step 38 on 2026-09-22; `async_smoke_test` exit 0 on all three, 0 TSan warnings; clang-format clean.
**Docs:** 0 doxygen warnings; `doc/refman.pdf` is 43 pages (was 31), rebuilt with
`tools/make_doc.sh`. **The PDF embeds the header's own source listing** - there is no separate file
page - so the page count tracks `async.hpp`'s length, and a few lines either way can move it by a
page or two at a boundary. Do not read a page count as a content change.
**Source:** audit of 2026-09-10 (4 critical, 5 high, 5 medium, 8 hygiene), findings 1, 2, 3 and 5
reproduced under TSan/ASan. Items lettered A onwards were found while fixing, and are read from the
code unless marked otherwise.

## Progress

Done — steps 1 to 21, plus 27, 28, 30 and 31 out of order. Step 20 counts as done by having been
**declined**: the rule of five was applied, measured and reverted, and what landed was the reasoning
and a guard.

Step 11 finished what step 4 left of the poll, step 12 closed the dangling `bind()` capture, and
step 13 closed `attach()`. **Group 2 is
complete**: every place the header held a raw pointer into another object now learns when that
object dies.
**Group 8 is complete** too, forced early by a red CI run, and **group 3** with steps 14 and 15.

**Group 4 closed on 2026-09-17**, with 16, 17, 30 and 31. The notification is now raised from one
place — inside `execute_actions()`, as the list empties — which is the only point every driver
passes through, so an execution reached through `attach()` reports its own batches like any other.
It cost half of step 16's contract: `on_finished` now sees `is_running()` true, deliberately.
**Group 5 closed the same day**, with 18 and 19: every `std::cout` in the repo is a `std::println`,
and both binaries are ThreadSanitizer-clean, which had never been true before.

**Group 6 is under way**: 20 declined, 21 and 28 landed, 29 closed by documenting rather than by
changing access, and the sweep it prompted is step 33, done. 21 and 28 were meant to settle "what
happened to the action I gave you" together,
and each answered it only for the direct caller — see the note under NEXT. **Step 32 is all that
remains of the group**, and step 33 joins group 7.

| Commit | Step |
|---|---|
| `60f7970` | 1 — mutex + condition variable on the action list |
| `60f7970` | 2 — a throwing action no longer calls std::terminate |
| `60f7970` | 3 — ~execution() waits for its worker |
| `60f7970` | 4 — execution_poll::remove(), called from ~execution() |
| `60f7970` | 5 — `started` set by start(), not by the worker |
| `60f7970` | 6 — stop() no longer spins on an empty check |
| `60f7970` | 7 — loop() waits instead of burning a core |
| `60f7970` | 8 — if constexpr replaces the SFINAE pair |
| `60f7970` | 9 — named constructor; `name` is read by the warnings |
| `60f7970` | 10 — four missing includes added *(partial — see step 22)* |
| `cfa245d` | 11 — a mutex on execution_poll: add(), remove() and is_running() |
| `c4843cd` | 12 — `bind()` holds the execution weakly, through a `shared_ptr` parameter |
| `2e3a8d0` | 13 — `attach()` takes a `shared_ptr`, gains `detach()`, refuses cycles |
| `f251acf` | 27 — CI pins GCC 14 on Linux, which is where `<print>` arrived |
| `f37ee83` | 14, 15 — `_result` becomes a `results` vector, filled by `run()` only |
| `282ceb7` | 16 — `on_finished` fires per drained batch *(half retired by 17)* |
| `5408eff` | 30 — `loop()` sets `finishing_` after its last drain, as `execute()` does |
| `5408eff` | 17 — `execute_actions()` raises the notification, so a driven execution reports |
| `86f343a` | 18, 19 — every `std::cout` becomes `std::println`; the repo is TSan-clean |
| `91c6112` | 31 — the unreachable tail notification goes; `actions_run_` resets in the pass |
| `f59c19d` | 20 — **declined**; the reasoning recorded instead, plus a guard |
| `f8472c0` | 21 — `add_action()` returns bool; the bound path stays untold |
| `b14373e` | 28 — a throwing action is caught in three arms; the worker survives |
| `622660b` | 29 — **declined**; the three connection points are documented instead |
| `588b7ca` | 33 — the header's doc comments cut to what each entity is and does |
| `5f09f17` | 22 — five headers the header uses and did not include |
| `686b7e5` | 23 — both dead forward declarations go |
| `d20640f` | 24 — the three connection points bind through `this` |
| `5d4a4ee` | 25 — `add_action()` and both `bind()` lambdas forward |
| `305b672` | 26 — `\|=` on a bool becomes `\|\|`, and the poll's fold gains a test |
| `26f91ed` | 36 — `run()` and `start()` wait for a resident worker to leave |
| *(uncommitted)* | 32 — `finishing_` is gone; `is_running()` is `running_` |
| *(uncommitted)* | 38 — `on_error` carries what an action threw to the caller |
| *(planned)* | 39-47 — the action queue becomes an actuator |

**NEXT: nothing in group 9** — the action queue is an actuator; **39 to 47 have landed**, raised by the user on 2026-09-23 on
the strength of the actuator's `43fefca`. A POC is in `git stash@{0}` and measures 42 of 46;
group 9 has the analysis and the steps.

**Before that, nothing was outstanding.** Step 38 closed the last question that was anybody's. What is
left of the "what happened to the action I gave you" ground is refusal reaching a bound caller,
recorded under step 21 and not worth its own step until something asks for it.
Step 35's B and C are recorded and deliberately not taken: B is
40 lines of new apparatus for the last allocation per action and for move-only work, C only pays off
once B exists. Take them when move-only work is wanted.

**Answered by step 38, half of it.** This paragraph read: a caller who reaches `add_action()`
through `bind()` learns nothing — not that an action was refused, not that one threw — because those
lambdas return `actionT::result_type`, which has no room for an answer. That framing is what kept it
open: it looked for the answer in the return type, and there is no room there for anyone. \ref
execution::on_error puts it somewhere else entirely, so **"not that one threw" is now answered for
every caller**, bound or direct. What is still unanswered is refusal — `add_action()` returns
`bool`, and a bound caller cannot see it. Take that with step 21's lines if it is ever wanted.

**Read the couplings before picking an order.** Step 21 touches the same two `bind()` lambdas as
step 25 and as the half of item 8 carried forward below — land the three together or accept three
passes over the same lines. Steps 21 and 28 ask the same question, "what happened to the action I
gave you", and the plan says to settle them together. Step 29 was blocked on step 17 and no longer
is. Step 32 was blocked on step 30 and no longer is; step 17 also strengthened its case, since
`finishing_` now does less than it did.

**The notification contract changed on 2026-09-17.** The callback is raised from inside
`execute_actions()`, on the worker's thread, at the moment the list empties — so `on_finished` sees
`is_running()` true, on both worker paths, and that is deliberate. Read the note at the end of step
16 before touching anything that notifies.

**The notification contract changed on 2026-09-17** and is worth reading before touching group 4:
the callback is raised from inside `execute_actions()`, on the worker's thread, at the moment the
list empties — so `on_finished` sees `is_running()` true, on both worker paths, and that is
deliberate. See the note at the end of step 16.

**Carried forward from step 12, and half of it is now done.** The by-value/double-copy half landed
with **step 25** on 2026-09-19: both `bind()` lambdas forward their arguments, and the copy count
through the bound path went from two to one. What is still open is the other half — they return a
default-constructed result, so an async call to an `int`-returning method yields 0. A move-only
argument still will not compile, and never could have here: the queue stores `std::function`, which
needs a copy-constructible target. See step 25.

**Remaining: step 35's B and C**, neither a defect: a move-only holder for the queue's element, and
the public entry point that only becomes useful with it. Every group is closed and nothing else is
open.

**The plan is concluded for now, 2026-09-19.** Every audit finding and every item found while fixing
has landed, been declined with the reasoning recorded, or - for B and C - been measured, written up
and deliberately deferred. What the header looks like at the end: the queue is guarded and drains
through one path; nothing holds a raw pointer into an object that can die without learning of it;
a worker is one per execution, `running_` says so and is the only flag that does; an action that
throws or is refused is answered rather than dropped silently; and a call through a binding costs
one allocation instead of three. The suite is 42 cases, green in Debug, under AddressSanitizer and
under ThreadSanitizer.

**Out of order:** step 27 was taken early, ahead of steps 14-26, because a CI run failed on it —
the ubuntu job could not compile `<print>` at all, so nothing else could be verified there.

**Not planned:** the `attach()` design itself. Items 9, 12 and the bounded wait in step 7 all trace
back to attached executions having their own list and no way to notify the attacher, but redesigning
that is a feature decision, not a fix. It is called out where it bites and left alone otherwise.

37 atomic steps. **One step = one commit = one concern**, and the suite must be green after every
one. The audit's numbering is preserved so items stay traceable; findings added while fixing are
lettered. Step 28 was added on 2026-09-14, steps 29 to 31 on 2026-09-15, step 32 on 2026-09-16 and
steps 33 to 37 on 2026-09-19, after the others.
The letter D is unused and skipped:
nothing in this file or in the history ever claimed it, and reusing a letter that may have meant
something in the original audit would cost more than the gap does.

**Granularity rule:** steps split by *concern*, not by *edit count*. Where one concern touches
several sites — item 8's two `bind` overloads, item 14's two `std::cout` calls — it stays one step.
Splitting those would leave the header internally inconsistent for a commit, which is the opposite
of atomic.

---

## Step index

| # | Item | Concern | Sites | Verified |
|---|---|---|---|---|
| **Group 1 — the queue and the worker (closed)** |
| 1 ✅ | 1 | action list shared across threads unguarded | `:295-312`, `:340-382` | CONFIRMED (TSan) |
| 2 ✅ | 2 | a dead binding called std::terminate | `:365-371` | CONFIRMED (exit 134) |
| 3 ✅ | 3 | defaulted dtor vs. a detached worker | `:203-219` | CONFIRMED (ASan) |
| 4 ✅ | 4 | poll held raw pointers it could not withdraw | `:113-124` | CONFIRMED (ASan) |
| 5 ✅ | 5 | stop() after start() wedged the worker | `:254-270` | CONFIRMED (5/5 hangs) |
| 6 ✅ | 6 | stop() spun on a condition nothing guaranteed | `:272-290` | CONFIRMED |
| 7 ✅ | 10 | loop() burned a full core | `:396-421` | read-only |
| 8 ✅ | hyg | SFINAE pair required default-constructible | `:340-352` | read-only |
| 9 ✅ | hyg | `name` was never read by anything | `:186-191`, `:336` | read-only |
| 10 ✅ | hyg | four headers used but not included | `:10-13` | read-only |
| **Group 2 — dangling references (closed)** |
| 11 ✅ | A | `execution_poll` is a shared mutable singleton with no lock | `:104-137` | CONFIRMED |
| 12 ✅ | 8 | `bind()` captures the execution by reference | `:53`, `:76` | CONFIRMED (5/5; ASan via probe) |
| 13 ✅ | 9 | `attach()` stores pointers, has no inverse, no cycle check | `:314-326` | CONFIRMED (ASan; SIGSEGV) |
| **Group 3 — results (closed)** |
| 14 ✅ | 7 | `_result` read uninitialised, written unsynchronised | `:447`, `:328`, `:348` | CONFIRMED (0xabababab; TSan) |
| 15 ✅ | 13 | only the last action's return value survives | `:328`, `:348`, `:447` | CONFIRMED |
| **Group 4 — notifications (closed)** |
| 16 ✅ | 11 | `on_finished` never fires in continuous mode | `:387-388` vs `:396-421` | CONFIRMED (0/3 fired) |
| 17 ✅ | 12 | an attached execution's `on_finished` never fires | `:652-671`, `:672-698`, `:700-728` | CONFIRMED (0 fired) |
| 30 ✅ | I | `loop()` never sets `finishing_` | `:700-728` | CONFIRMED (probe); read-only |
| 31 ✅ | J | the drain after `loop()` breaks can never report a batch | `:735-745` | CONFIRMED (read + probe) |
| **Group 5 — output (closed)** |
| 18 ✅ | 14 | the header writes to `std::cout` unsynchronised | `:712`, `:751` | CONFIRMED (TSan) |
| 19 ✅ | E | the smoke test races on `std::cout` between two workers | `async_smoke_test.cpp:15,24` | CONFIRMED (TSan) |
| **Group 6 — API contract** |
| 20 ✅ | B | `~execution()` suppressed the implicit moves | `:255` | declined, see step |
| 21 ✅ | C | a refused action is reported but not returned | `:440-480` | CONFIRMED (probe) |
| 28 ✅ | G | an action that throws anything but `invalid_action` terminates | `:670-690` | CONFIRMED (terminate) |
| 29 ✅ | H | the `action_*` members are public internal seams | `:600-624` | declined, documented |
| 32 ✅ | K | `finishing_` was redundant and is gone | `:305`, `:744` | investigation |
| **Group 7 — hygiene** |
| 22 ✅ | hyg | headers used but not included | `:8-23` | read-only |
| 23 ✅ | hyg | a forward-declaration block in which both declarations were dead | `:24` | read-only |
| 24 ✅ | hyg | `other_this_ = this` was pointless indirection | `:188-192` | read-only |
| 25 ✅ | hyg | `add_action` copied the action and every argument twice | `:394` | CONFIRMED (counted) |
| 26 ✅ | hyg | `result \|= ret` on a bool | `:135` | read-only |
| 33 ✅ | hyg | doc comments carry plan-sized narrative | `async.hpp` (throughout) | read-only |
| 34 ✅ | perf | a bound action was copied once per invocation | `:573`, `:262`, `:296` | CONFIRMED (counted) |
| 35 | api | what the queue holds: A done, B and C open | `:778`, `:573` | investigation |
| 36 ✅ | bug | two workers in one execution, and one handshake between them | `:341`, `:359` | CONFIRMED (ASan) |
| 37 ✅ | hyg | test comments carry plan-sized narrative | `test/async_tests.cpp` | read-only |
| **Group 8 — build (closed)** |
| 27 ✅ | F | C++23 raises the toolchain floor; CI may not clear it | `test/CMakeLists.txt:7` | CONFIRMED (CI) |
| 38 ✅ | L | what an action throws reaches a log and no code | `:655`, `:762-777`, `:814` | CONFIRMED (test) |
| **Group 9 — the queue becomes an actuator (open)** |
| 39 ✅ | api | a moved actuator keeps its action pointers valid | `actuator_test.cpp:1212-1259` | CONFIRMED (58/58; 2 breaks caught) |
| 40 ✅ | api | the drain takes the batch out under the lock | `:736`, `:690` | CONFIRMED (5 races on the POC) |
| 41 ✅ | api | a throwing action neither re-runs nor stops its batch | `:736`, `:757-775` | CONFIRMED (1.37M retries on the POC) |
| 42 ✅ | api | a dead binding still reaches `on_error` | `actuator.hpp:359` | CONFIRMED (stdout on the POC) |
| 43 ✅ | api | one predicate for "is there work" | `:403`, `:836`, `:890` | CONFIRMED (2 failures on the POC) |
| 44 ✅ | api | `loop()` wakes on an add again | `:890-893` | CONFIRMED (2263 ms on the POC) |
| 45 ✅ | api | results survive a second batch, written under their mutex | `:726-739`, `:868` | CONFIRMED (`{2}` on the POC) |
| 46 ✅ | api | what the change leaves behind: `<deque>`, docs, allocations | `:13`, `:739`, `:1009` | measured (3.00/action, 61 ns) |
| 47 ✅ | api | the gate: Debug, ASan, TSan, clang-format, make_doc.sh | whole repo | 51/51 on four presets; 0 doc warnings |

---

## Group 1 — the queue and the worker (closed)

All ten steps landed in `60f7970`. Recorded here so the findings stay traceable; no action.

### Step 1 · item 1 — action list shared across threads unguarded — DONE
`async.hpp:295-312`, `:340-382` · CONFIRMED under ThreadSanitizer

`add_action()` pushed on the caller thread while the worker popped, with no mutex and no condition
variable. Roughly 1 action in 10 went missing at 1000 queued; a worker reaching a `std::function`
still under construction threw `std::bad_function_call` and aborted.

> `action_mutex` guards the list. `add_action()` pushes under it and notifies. `execute_actions()`
> pops under it and invokes with the lock released, because an action may itself call `add_action()`.

### Step 2 · item 2 — a dead binding called std::terminate — DONE
`async.hpp:365-371` · CONFIRMED: exit 134

`untangle::bind(shared_ptr, method)` throws `invalid_action` when the object is gone. The worker
invoked the action with no `try`, and an exception leaving a thread function calls `std::terminate`.

> `try { execute_action(action); } catch (const untangle::invalid_action& ia) { warn }` — matching
> what `actuator::operator()` already does.

### Step 3 · item 3 — defaulted dtor vs. a detached worker — DONE
`async.hpp:203-219` · CONFIRMED under ASan: stack-use-after-scope

`~execution() = default` while a detached worker still read `action_list` and `running`.

> The destructor stops the worker, then waits on `running` — the same state the poll reports — and
> withdraws from the poll. The worker stays detached; it is waited on, not joined.

### Step 4 · item 4 — poll held raw pointers it could not withdraw — DONE (in part)
`async.hpp:113-124` · CONFIRMED under ASan: SEGV in `execution_poll::is_running()`

`add()` stored `&async_exec.action_is_running` and there was no way to take it back.

> `execution_poll::remove()`, called from `~execution()`. The rest of the poll — locking `add()`,
> `remove()` and `is_running()` against each other — is item A, closed by step 11.

### Step 5 · item 5 — stop() after start() wedged the worker — DONE
`async.hpp:254-270` · CONFIRMED: 5 hangs out of 5

`loop()` set `started` from inside the worker; `stop()` cleared it from the caller; nothing ordered
them, so a `stop()` that landed first was overwritten and the loop spun forever.

> `start()` sets `started` under the lock before spawning. The worker no longer writes it.

### Step 6 · item 6 — stop() spun on a condition nothing guaranteed — DONE
`async.hpp:272-290` · CONFIRMED

`stop()` slept in a loop until the list drained. Called without a prior `start()`, or after the
worker had exited, it never returned.

> `stop()` marks the execution stopped under the lock and notifies. The worker drains what was
> already queued and leaves. Waiting for it is the destructor's job, or the caller's via the poll.

### Step 7 · item 10 — loop() burned a full core — DONE
`async.hpp:396-421`

> `action_cv.wait_for(lock, 10ms, ...)`. The wait is **bounded, not plain**: an attached execution
> has its own list and no way to notify this condition variable, so the worker still has to look in
> on attached work. That 10ms tick is the cost of `attach()` as it stands — see items 9 and 12.

### Step 8 · hygiene — SFINAE pair required default-constructible — DONE
`async.hpp:340-352`

Both `enable_if_t` branches required `result_type` to be default-constructible, and the non-void
branch returned a fresh default that every caller discarded.

> One `execute_action()` using `if constexpr`. Both overloads and the discarded return are gone.

### Step 9 · hygiene — `name` was never read — DONE
`async.hpp:186-191`, `:336`

A public `std::string name` that nothing in the header read.

> The warnings from steps 2 and 21 name the execution that reported them. Added
> `explicit execution(std::string)` and `default_name = "unnamed"` so a warning always names
> something. All six executions in the tests now set it through the constructor.

### Step 10 · hygiene — four headers used but not included — DONE
`async.hpp:10-13`

> `<condition_variable>`, `<mutex>`, `<print>`, `<string>`. Six more remain — step 22.

---

## Group 2 — dangling references

The same shape as item 4, which step 4 closed: an object holds a raw pointer or reference to
another with no way to learn it has died.

### Step 11 · item A — `execution_poll` is a shared mutable singleton with no lock — DONE
`async.hpp:104-137` · CONFIRMED: wrong answers, and a SEGV, both in a plain Debug build

**This step's original description was wrong and is corrected here.** It claimed that `add()` being
non-idempotent left a stale registration a single `remove()` could not undo. It does not:
`actuator::remove()` uses `actions.remove_if(...)` (`actuator.hpp:208`), which removes *every*
match, so a double add followed by one remove clears both. Verified under ASan — registered twice,
destroyed, polled, clean. Idempotence is not a bug here.

What was real is thread safety, and it was worse than "read-only" suggested. Two defects, both
reproducible without a sanitizer:

- **`is_running()` returned wrong answers to concurrent waiters.** It invokes an actuator that
  clears one shared `results` vector and refills it, so two callers walk over each other and the
  loser iterates a vector the winner has just emptied — and reports idle for an execution that is
  still running. One waiter: 0 wrong answers in 3.4M polls. Two waiters: 11.9% wrong at `-O0`,
  0.0045% at `-O1`. The rate swings with optimisation because a slower `is_running()` leaves the
  vector cleared for proportionally longer; the suite builds Debug, so it sees the high rate.
- **`add()` and `remove()` crashed a concurrent waiter.** `add()` move-assigns the whole actuator
  (`:106`, the `connect()` branch) out from under a thread walking its action list. SEGV 15/15.
  Not an exotic path: `~execution()` calls `remove()` itself, so any execution going out of scope
  on one thread while another waits on the poll hits it — and waiting on the poll is the documented
  way to wait.

`running` itself was never at fault; it is atomic and correct. Everything wrong happened above it,
in the poll's aggregation.

> One `mutable std::mutex actuator_mutex`, taken by `add()`, `remove()` and `is_running()`. No
> deadlock risk today: `is_running()` invokes actions while holding it, but those actions are bound
> to `execution::is_running()`, which only reads an atomic and never re-enters the poll. That stops
> being true if `add()` ever accepts arbitrary callables.

Tests: `execution_poll.does_not_report_idle_while_an_execution_runs` and
`execution_poll.survives_executions_registering_while_another_thread_waits`. The first holds its
action open so the execution provably cannot finish mid-measurement — otherwise a waiter that checks
`exec.is_running()` and then asks the poll races itself, and the poll's "idle" is correct rather
than wrong. That time-of-check to time-of-use reading can only happen once per waiter, so it never
accounted for the counts above, but a test must not count it at all.

**Left open:** `is_running()` could be `const` — the `mutable` on the mutex is already there for it.

### Step 12 · item 8 — `bind()` captures the execution by reference — DONE
`async.hpp:53`, `:76` · CONFIRMED: 5/5 in Debug and under ASan; sanitizer named it via probe

Both free `bind()` overloads return `[&async_exec, async_action](auto... args)`. The returned
`std::function` is typically stored on the bound object, which has no relationship to the
execution's lifetime; invoking it after the execution has died calls `add_action()` on freed memory.
This is the hazard `actuator::bind` avoids with a `weak_ptr`, and the hazard step 4 just removed
from the poll.

Two smaller problems in the same two lambdas: `auto... args` takes by value and `std::bind` copies
again, so a `const std::string&` parameter is copied twice and a move-only argument will not
compile; and the lambda returns a default-constructed result, so an async call to an `int`-returning
method silently yields 0.

**Empirically, 2026-09-11.** Both cases fail 5/5 in a Debug build and 5/5 again with
`-DASYNC_SANITIZE=address`, as `std::system_error: mutex lock failed: Invalid argument` thrown out of
the action — `add_action()` takes `action_mutex` before it touches anything else, so the destroyed
mutex is the first thing hit. googletest catches it and reports a failure, so the binary does not
abort and the rest of the suite still runs: 6 of 8 pass, these two are the only red.

**ASan does see the dangling execution — but not through the production path.** Probed three ways
in the same toolchain (Apple clang 21, `-fsanitize=address`, runtime confirmed linked into
`async_tests`):

| probe | access | ASan |
|---|---|---|
| execution `new`ed, `delete`d, then `is_running()` | instrumented load, `async.hpp:254` | `heap-use-after-free` |
| execution in an inner scope, then `is_running()` via a saved pointer (the tests' shape) | instrumented load, same line | `stack-use-after-scope` |
| the action invoked after the execution died (what the tests do) | `pthread_mutex_lock`, libsystem | **nothing** |

The memory is poisoned and the sanitizer would name it; what preempts the report is the ordering
inside `add_action()`, whose first statement is `std::lock_guard(action_mutex)`. That first touch
happens inside uninstrumented libsystem_pthread, whose own validation sees the destroyed mutex and
returns `EINVAL`, so libc++ throws `std::system_error` before any instrumented load executes. TSan,
which does intercept `pthread_mutex_lock`, reports nothing either — same exception, 5/5.

**Worth remembering for later steps:** the step-1 mutex now sits in front of every access to the
queue, so any future dangling-execution defect will surface as `mutex lock failed` and mask the
sanitizer report. Reach for a direct instrumented read (`is_running()` through a saved pointer) when
a use-after-free needs naming, rather than concluding the sanitizer is silent.

**Consequence for the tests:** assert the dead-binding *signal* rather than the absence of a side
effect. `EXPECT_EQ(calls, 0)` passes in the silent-success case too, because a push onto a destroyed
list still never runs. `EXPECT_THROW(action(...), untangle::invalid_action)` is a checked reason in
both directions, and it matches the convention already in the header — `actuator::bind` throws
`invalid_action` for a dead object, and `execute_actions()` catches exactly that (`:376-383`). It
does presuppose that a dead execution's action throws rather than drops silently, which is part of
the ownership decision below.

**Ownership model chosen: the binding holds the execution weakly, the way `actuator::bind` does.**
The two free `bind()` overloads are gone; their bodies moved into `bind_action_and_method()` and
`bind_action_and_function()`, which became static and take the execution as
`const std::shared_ptr<execution>&`. Each captures a plain `std::weak_ptr` built from that argument -
no `enable_shared_from_this` anywhere - `lock()`s it and holds the `shared_ptr` across `add_action()`,
and throws `untangle::invalid_action` when the lock fails, the exception `execute_actions()` already
catches.

Taking the `shared_ptr` as a parameter is what makes this better than capturing `weak_from_this()`:
**binding an execution that no `shared_ptr` owns does not compile**, instead of compiling and throwing
on first invocation. Verified - `bind_action_and_function(action, f, stack_exec)` fails with "no
matching function", while a shared-owned execution destroyed before its action fires throws
`invalid_action`. Shared ownership is now required exactly where it is needed, at bind time: a stack
execution is still perfectly usable through `add_action()` directly.

`create_instance()` stays as the variadic factory, though it is now convenience rather than
load-bearing - `std::make_shared<execution<actionT>>(...)` does the same. The explicit copy deletions
were dropped as redundant: the `mutex`, `condition_variable`, `thread` and `atomic_bool` members
already make `execution` non-copyable, confirmed by static_assert.

Verified after the change: Debug, ASan and TSan all 8/8, smoke test clean under Debug and ASan.

One consequence to keep in mind: an action invocation is a temporary co-owner, so releasing the last
caller-held `shared_ptr` while an action is in flight runs `~execution()` - which waits on the worker -
on whichever thread drops the temporary.

> Still open in this step: the by-value/double-copy half, separable and not done here — `auto...
> args` takes by value and `std::bind` copies again; and the lambda returns a default-constructed
> result, so an async call to an `int`-returning method yields 0.

**The copying half landed in step 25** as `auto&&... args` with perfect forwarding. The move-only
argument named there is *not* what that fixed - see step 25 for why it cannot be, while the queue
stores `std::function`. The default-constructed result is still open.

### Step 13 · item 9 — `attach()` stores pointers, has no inverse, no cycle check — DONE
`async.hpp:314-326` · CONFIRMED: ASan `heap-use-after-free`; SIGSEGV for the cycle

`attach()` stored `&other.action_execute` and `&other.action_stop` — pointers into the attached
object. There was no `detach()`, no notification when the attached object died, and no cycle check.

**Empirically, 2026-09-14.** Three separate defects, each reproduced before anything was written:

| defect | how it showed |
|---|---|
| attacher outlives the execution it attached | ASan `heap-use-after-free`, READ of size 8 at `actuator.hpp:134` from `execute_actions()` (`async.hpp:410`) |
| `a.attach(b); b.attach(a);` then driven | SIGSEGV, exit 139; ASan `stack-overflow` |
| `a.attach(a)` then driven | SIGSEGV, exit 139 |

The dangling read is the `!*action` emptiness test inside `actuator::operator()`, which dereferences
the stored pointer before invoking it. Unlike step 12 this one is an instrumented load, so ASan names
it directly rather than being preempted by `pthread_mutex_lock` — the masking effect noted there does
not apply here. **In a plain Debug build it is nondeterministic**: five runs gave SIGSEGV, SIGBUS,
clean, SIGBUS, clean. Configure with `-DASYNC_SANITIZE=address` to see it named, where it is 3/3.

**The fix, in `attach()` and `detach()` only.** `attach()` takes the execution as
`const std::shared_ptr<otherT>&`, the same trade step 12 made for binding: an execution that no
`shared_ptr` owns cannot be attached at all. The actuators are still given `&other->action_execute`
and `&other->action_stop`; what makes that safe is the record left on the other side. The attached
execution is told which actuators hold it, and `~execution()` takes itself back out of them.

Four members carry that, all on the attached side:

- `attachment_lifetime`, a `std::shared_ptr<attachment>` created with the execution and never reset.
  `struct attachment` holds one `std::weak_ptr<attachment> attacher` and lives at **namespace scope,
  not nested in `execution`** — nested would make it one type per specialisation, and heterogeneous
  attach is load-bearing (the smoke test attaches an `execution<void(int)>` to an
  `execution<void()>`). It is identity, liveness token and parent link in one object.
- `attacher_execute` and `attacher_stop`, raw `void_actuator*` to the attacher's two actuators.
  Raw, and safe only in company: followed once, by `~execution()`, and only while
  `attachment_lifetime->attacher` has not expired — which is exactly while the attacher, and
  therefore the actuator members inside it, are still there. They cannot be collapsed into a single
  `execution*`: that would mean `execution<actionT>*`, the same specialisation, and it fails to
  compile against the smoke test's heterogeneous attaches. An `actuator<std::function<void(void)>>`
  is the same type whatever `actionT` is, which is why two pointers work where one does not.

`detach()` needs no record of its own. `untangle::actuator` is a `struct` with no `private:`, so its
`actions` list — of `action_t*` — is the record of what is attached, and an attachment is found in it
by identity with `std::find`.

**Cycle rules.** Refused: attaching an execution to itself, attaching one that already has a live
attacher, and attaching one that is anywhere up this execution's own chain of attachers. The
once-only rule is what makes the last check a walk rather than a search — every execution has at most
one attacher, so the graph is a forest. It also narrows the problem more than it first appears: in a
chain `a→b→c→d`, every ancestor of `d` except the root is already refused as *attached already*, so
the only cycle that could ever be built is one attaching the root of its own chain. Probed four deep.

> An earlier round caught only the two-object case, because the check compared an execution against
> its own attacher and stopped there. Walking the chain needed the parent link that
> `struct attachment` now carries; before it, `attachment_lifetime` was a `shared_ptr` to a bare
> `char` and the actuator pointers led nowhere useful. Adding the link **removed** a member — the
> separate `attacher_lifetime` became `attachment_lifetime->attacher` — and removed a rule, the
> one-step attach-back check being the loop's first iteration.

**Designs tried and dropped**, both green before they were rejected as too much structure:

1. A non-template `attachable` base holding the attachment state, with lists kept symmetric from both
   ends and a hook in `~execution()`.
2. The attacher owning weakly-bound lambdas in two `std::map`s keyed by target address, so nothing
   pointed into the other object and `~execution()` needed no hook at all. Dropped because the
   actuator's own action list already records what is attached.
3. A single `attachment_point` struct bundling pointers to the attacher's two actuators — replaced by
   naming the two actuators directly.

**Tests**, seven cases under `execution_attach`: the dangling attachment; the two-object cycle and
self-attach; `detach()` unwiring the execute path and, separately, the stop path; detaching something
never attached; a legitimate `a→b→c` chain running end to end; and that chain refusing to close into
`a→b→c→a`. The stop-path case is worth its own entry — `stop()` is final for an execution, because
`add_action()` refuses everything once stopped, so an execution that still accepts an action after
its former attacher stopped is one the stop did not reach.

> The transitive-cycle case was written after its fix rather than before it. The walk was mutated to
> a single step to confirm the case has teeth: it fails, while the chain case still passes.

**Left open, deliberately:** `attach()` still has no thread safety, and neither did what it replaced.
`attach()`, `detach()` and the withdrawal in `~execution()` all assume the attachment graph is edited
from one thread while the worker only reads it through the actuators.

Verified after the change: Debug 16/16, ASan 16/16, TSan 15/16 — the one failure being step 19's
pre-existing `std::cout` race in `async_smoke_test`, confirmed by stashing to be present on a clean
tree. 30x repeat of the whole suite, no flakes. clang-format clean.

`tools/make_doc.sh` fails the build on any documentation warning, so the unresolved
`\ref execute_actions()` that step 12 left at `async.hpp:282` had to go before the PDF could be
rebuilt — it is fixed here rather than carried, because it was blocking, not cosmetic.
`doc/refman.pdf` is regenerated: 37 pages, 0 warnings.

---

## Group 3 — results (closed)

### Step 14 · item 7 — `_result` read uninitialised, written unsynchronised — DONE
### Step 15 · item 13 — only the last action's return value survives — DONE
`async.hpp:447` (declaration), `:328` (`result()`), `:348` (write) · CONFIRMED

**Taken as one step**, as the plan said they should be: step 15 replaces the single `_result` with a
vector, which removes two of step 14's three defects outright rather than fixing them. Splitting
would have meant touching the same three lines twice.

**Empirically, 2026-09-14.** Three defects, each reproduced first:

| defect | how it showed |
|---|---|
| `_result` indeterminate before any action ran | a stack execution read back `0xabababab`, the pattern written there beforehand, 3/3 |
| only the last return value survived | 1, 2, 3 queued left `result()` reporting 3 |
| the write raced the read | TSan: write at `async.hpp:524` (`execute_action`) against read at `:504` (`result()`) |

The first is worth recording. On the heap `_result` read 0 and looked perfectly innocent; it took a
stack-allocated execution, over memory dirtied on purpose, to show the value was never there. An
execution may be built on the stack — only `bind()` and `attach()` require shared ownership — so
that is an ordinary shape, not a contrivance.

**The fix.** `_result` becomes `std::vector<typename resultT::type> results_`, and `result()`
becomes `results()`, returning a copy. The uninitialised read stops existing rather than being
patched: an empty vector has no value to read. The lost return values stop existing too.

**Filled by `run()` only.** The continuous worker started by `start()` does not collect. This is the
user's decision and deliberate: `loop()` has no point at which a run is over, so there is nothing to
hand back and nowhere to clear, and collecting there would grow without bound. Deferred until a use
case wants it. `run()` sets `collecting_results`, `start()` clears it — set explicitly rather than
inferred from `started`, because which worker is running is not the same question as whether a run
will hand anything back.

`execute()` clears the vector at the start of a run, so a run reports its own results and not the
previous run's, and `on_finished` fires after the queue has drained and before `running` is cleared
— which is what makes the callback the place to read them.

> **A separate `results_mutex`**, not `action_mutex`. The queue and the results are two different
> things, and the worker takes this one only for the push, after an action has returned — so an
> action that calls `add_action()` never meets it held. Re-probing the race that TSan found is
> clean: 2000 actions appended by the worker while another thread read `results()` in a loop, no
> report.

**Tests**, five cases under `execution_results`: empty before any action runs; every action's result
kept, in order; available to `on_finished`; cleared between runs; and not filled by the continuous
worker. The last one pins the deferral, so collecting in `loop()` would have to be a deliberate
change rather than a drift. Three of the five were red against the stub.

The `on_finished` case asserts on what the callback read, not on what is readable afterwards, since
that is the contract. The cleared-between-runs case is stated separately because an implementation
that only ever appends passes the others and fails that one.

**Callers updated:** `async_smoke_test.cpp` read `asyncexec3->result()` in two `on_finished`
callbacks; both now print the whole vector.

Verified: Debug 21/21, ASan 21/21, UBSan 21/21, TSan 20/21 — the one failure being step 19's
pre-existing `std::cout` race in `async_smoke_test`. 30x repeat, no flakes. clang-format clean,
0 doxygen warnings, `doc/refman.pdf` rebuilt.

---

## Group 4 — notifications (closed)

### Step 16 · item 11 — `on_finished` never fires in continuous mode — DONE
`async.hpp:387-388` (fires in `execute()`) vs `:396-421` (`loop()` does not call it) · CONFIRMED

Two defects. A caller using `start()`/`stop()` never got the callback — probed 2026-09-14, fired 0
times out of 3 runs in that mode against 1 out of 1 for `run()`. And where it did fire, it fired
before `running = false`, so a callback asking `is_running()` was told yes by the very notification
that it had finished, 3/3.

**"Finished" means a batch drained, not that the worker left.** That is the user's decision, and the
reason is the use it has to serve: waiting for one batch of actions to complete before firing the
next. A callback that only arrived when the worker ended would be useless for that — getting it
would mean calling `stop()`, leaving no worker to fire the next batch at.

> `execute_actions()` now returns how many actions it ran, and `loop()` reports a batch through
> `notify_finished()`. **A pass that ran nothing reports nothing**: the wait in `loop()` is bounded
> at 10ms so the worker can look in on attached executions, and it calls `execute_actions()` on
> every pass, so an unguarded notification would arrive about a hundred times a second on an idle
> execution saying that nothing had happened. A pass that drained but left more behind — an action
> may queue another — reports nothing either; that is the next batch, not the end of this one.

`run()` is unchanged in what it fires: it drains once and leaves, so per-batch and per-worker are
the same thing there.

**The ordering needed a second flag.** The callback cannot move below `running = false`, because
step 3 made `~execution()` wait on that and the object may be freed the moment it reads false. So
`finishing` is set before the callback and `is_running()` became
`running && !finishing`. The two now answer different questions: `is_running()` is for callers and
says whether work is going on, while `running` stays the handshake the destructor waits on and says
whether the worker is still touching the object. They were the same answer until the notification
needed to be told the truth about itself.

> **Consequence to keep in mind:** `execution_poll` reads `is_running()`, so it reports an execution
> idle while its `on_finished` is still running. That is the intended reading — idle means no
> actions left, and the callback is the caller's own code — and it is safe, because a caller that
> destroys the execution then still blocks in `~execution()` until the worker clears `running`.

**Tests**, five cases under `execution_notification`: a batch drains and reports, twice over for two
batches; an idle worker reports nothing across 200ms, which is twenty of those 10ms passes; `run()`
reports once; `run()`'s callback is not told it is still running; and a drained batch does *not*
claim the worker has stopped. The last two are separate assertions on purpose — in continuous mode
a drained batch says nothing about the worker, which is still there waiting for the next one.

**Deferred:** a second signal when the worker actually stops with nothing left to run. Nothing wants
it yet.

**Correction to this plan.** This step previously said to settle the ordering "once" together with
step 20. That was wrong: step 20 is about `~execution()` suppressing the implicit moves and does not
touch this ordering at all. The step it actually couples to is 3, whose destructor handshake is the
constraint.

Verified: Debug 26/26, ASan 26/26, TSan 25/26 — the one failure being step 19's pre-existing
`std::cout` race in `async_smoke_test`. 30x repeat, no flakes; clang-format clean, 0 doxygen
warnings.

**Half of this step was retired on 2026-09-17, by step 17.** The notification moved *into*
`execute_actions()` so that an attached execution could be told its own batch had drained, which
means the callback is now raised the moment the list empties — before `finishing_` is set. So
**`on_finished` sees `is_running()` true, and that is the contract now**: the callback runs on the
worker's own thread, with the rest of the worker's path still to go, and a drained batch says
nothing about the worker on either path. The user decided this on 2026-09-17.

What that changes here: `run()`'s callback *is* told the execution is still running, and the test
that asserted otherwise is now `a_run_batch_does_not_claim_the_worker_stopped`, asserting the
opposite. The two cases this step called "separate assertions on purpose" are now two halves of one
contract, kept apart only because the paths differ.

What survives: `finishing_` itself, and the rest of this step. Its stated reason above — that the
callback must not be told the execution is still working — is gone, but the other one holds and is
now the only one. `~execution()` waits on `running_` and may free the object the moment it reads
false, so `running_` must stay the last thing the worker touches, and a caller polling
`is_running()` still needs an answer that goes false before the lifetime handshake does. See step 32,
which asks whether the two flags should collapse now that one of them does less.

### Step 17 · item 12 — an attached execution's `on_finished` never fires — DONE
`async.hpp:652-671` (`notify_finished`), `:672-698` (`execute`), `:700-728` (`loop`)

**Narrowed on 2026-09-15**, after probing. The item as written had three parts; two of them are not
defects, and the title above is what is left. The original wording is kept below so the change is
traceable.

`notify_finished()` is called from `execute()` and from `loop()`, and an attached execution is in
neither: the attacher's worker reaches it through `action_execute`, which is `execute_actions()` and
nothing else. So an attached execution drains batch after batch and reports none of them. Probed
2026-09-15: `on_finished` fired 0 times for an attached execution across a full run, driven both
synchronously and by a real `start()`ed attacher.

> Report from where the batch actually drains. The constraint is that `execute()` already calls
> `execute_actions()` and then `notify_finished()`, so a `notify_finished()` moved inside
> `execute_actions()` would fire twice per `run()` — guarded by the existing
> `on_finished_fires_once_per_run`. The rule from step 16 holds here too: a pass that ran nothing
> reports nothing.

**Applied 2026-09-17**, and landed in `5408eff`. `execute_actions()` raises the
notification itself, at the point its list empties, which is the one place every driver passes
through — a worker in `execute()` or `loop()`, and an attacher's worker arriving via
`action_execute`. `execute()` no longer notifies separately, which is what keeps `run()` to one
callback. The count moved onto the object as `actions_run_` (`std::atomic_size_t`), so
`notify_finished()` takes no argument and can be raised from wherever the pass was driven.

**The notification is raised outside `action_mutex_`**, and that is not incidental: `on_finished` is
caller code and may call `add_action()`, which takes the same non-recursive mutex. Holding it across
the callback deadlocks the worker against itself — probed 2026-09-17, `add_action()` never returned,
the worker never left the drain, and `~execution()` then spun on `running_` for ever. The pass reads
whether the list emptied under the lock, releases, and notifies.

**A rejected design, recorded so it is not re-proposed.** A separate private `execute_driven_batch()`
bound to `action_execute`, calling `execute_actions()` then `notify_finished()`. It worked and was
fully green, but the user turned it down on 2026-09-16 — no new method for this. Kept in the session
scratchpad as `async_with_step17_fix.hpp`.

**It cost half of step 16.** The callback is now raised before `finishing_` is set, so it sees
`is_running()` true. That is the contract now, decided 2026-09-17 — see the note at the end of step
16, and the reframed `a_run_batch_does_not_claim_the_worker_stopped`.

Verified: Debug 33/33; ThreadSanitizer 0 warnings and AddressSanitizer 0 errors across the whole
suite; 30x repeat, no failures; `async_smoke_test` exit 0; clang-format clean; `tools/make_doc.sh`
0 warnings, 41 pages.

**Not a defect — `is_running()` false for an attached execution.** `is_running()` is
`running_ && !finishing_`, and `running_` says whether *this execution's own worker thread* is still
touching the object — the header says so at `:347-353`, and `is_busy()`'s comment at `:362` turns on
the same distinction. An attached execution has no worker of its own, so `false` is the documented
answer, not a wrong one. `is_busy()` is the one that answers "is there work outstanding", and it was
already correct for attached executions when probed.

**Not a defect — the poll reporting an attached execution idle.** The poll answers for the worker
threads registered with it, nothing more and nothing less; an execution with no worker contributing
`false` is that contract working. Registering both an attacher and the executions it drives is fine
and changes nothing. Decided by the user on 2026-09-15.

**Two rejected designs, recorded so they are not re-proposed.** (a) An attached execution sets
`running_`/`finishing_` around its own pass so it reports for itself — rejected: it makes
`is_running()` mean "work is happening" instead of "my worker is alive", which is a different
question that `is_busy()` already answers. (b) The attacher absorbs the attached work and reports
for the whole tree — rejected: it needs `action_execute` widened from `std::function<void(void)>` to
return the count, a public API change, and it makes an execution's answers depend on who attached
it. A third, making the poll ask `is_running() || is_busy()`, dies with (a): it widens the poll's
contract past the threads added to it.

**Original wording, from the 2026-09-10 audit:**

> The attacher's worker calls `execute_actions()` on attached objects, but only `run()`/`start()`
> set `running`, and neither is called on an attached execution. The poll reports them idle while
> they are executing, and their `on_finished` never fires either.
>
> An attached execution should carry the attacher's running state, or the poll should report through
> the attacher. Depends on how step 13 reshapes `attach()`.

### Step 30 · item I — `loop()` never sets `finishing_` — DONE
`async.hpp:700-728` · CONFIRMED by probe; **read-only — no black-box test is possible, see below**

Raised by the user on 2026-09-15, reading the two worker paths against each other. Not an audit
finding.

`finishing_` separates "my worker has finished" from "my worker is still touching this object", and
its comment at `:823-826` says it is set by the worker once it is reporting itself finished.
`execute()` sets it at `:688`. **`loop()` never sets it.** So a continuous worker answers
`is_running()` true from `start()` until `running_` is cleared, through the break, the drain that
follows it and everything else it does on the way out.

Measured 2026-09-15: the last `on_finished` of a `run()` saw `is_running()` false; the last
`on_finished` of a `start()`/`stop()` saw it true, with the worker exiting immediately after.

`finishing_` is read in exactly one place - `is_running()` at `:353` - so that is the whole
observable effect, and the whole blast radius.

> After the break: drain, **then** set `finishing_`, then notify, then clear `running_` last -
> exactly `execute()`'s order at `:686-696`.
>
> ```
> const auto actions_run = execute_actions();
> finishing_ = true;
> notify_finished(actions_run);
> ```
>
> **Not in `stop()`** - that runs on the caller's thread, and would report the worker finished while
> it is still draining. Worker-side, as the comment says.

**Not before the drain, which is the trap.** This step first read "set it at the break, before the
final drain"; the user rejected that on 2026-09-15 and was right. `execute()` runs its actions
*first* and sets `finishing_` only afterwards, so an execution executing actions reports
`is_running()` true - and the post-break drain still runs work, because `execute_actions()` ends by
driving the attacher's actuator (`:645-647`). Setting `finishing_` before it would report an
execution finished while attached actions were still running under it.

**Why there is no failing test.** With the correct ordering the false window holds only
`notify_finished(...)` - unreachable, see step 31 - and a `std::cout`. Nothing caller-written runs
between `finishing_ = true` and the store, so the fix changes nothing observable from outside the
header today. It is a consistency fix, and it earns its place because the flag's documented meaning
is not honoured on one of the two worker paths, and because anything later placed in that window -
step 17's notification seam among them - would be told the wrong thing.

**Guard instead of a reproduction:**
`execution_lifecycle.an_execution_running_its_last_actions_does_not_report_itself_finished` passes
today and must keep passing. It samples `is_running()` from an attached execution's action run
during the post-break drain, and expects **true** - which is what fails if `finishing_` is set too
early. It does not pin *where* the sample was taken, since mid-loop gives true as well; its value is
the guard. 30x repeat, no flakes; TSan silent.

**What must also keep passing:** `execution_notification.a_drained_batch_does_not_claim_the_worker_
stopped`. Mid-loop, a drained batch says nothing about the worker and `is_running()` must stay true.

**Applied 2026-09-16**, and landed in `5408eff`. `loop()`'s tail now reads as
`execute()`'s does - the post-break drain is held in `actions_run`, `finishing_` is set after it and
before `notify_finished()`, and `running_` stays last. Verified: 32/33 Debug, the one failure being
step 17's own test; ThreadSanitizer 31 passed and **0 warnings** across the whole suite;
AddressSanitizer the same with 0 errors; 30x repeat of the full suite, exactly one failing test every
run, no flakes. clang-format clean.

### Step 31 · item J — the drain after `loop()` breaks can never report a batch — DONE
`async.hpp:735-745` · line references refreshed 2026-09-17, after steps 17 and 30 reshaped `loop()`

Found 2026-09-15 while analysing step 30.

`loop()` breaks only when `!started_ && action_list_.empty()` (`:728`), and `add_action()` refuses
once `stopped_` (`:443`). So by the time control reaches the tail drain, this execution's own list is
always empty. Nothing can report there by either route: `execute_actions()` raises the notification
only after an action has run and the list has emptied (`:661`), and the explicit
`notify_finished()` at `:749` finds `actions_run_` still 0. The comment above it - "What was queued
before stop() still belongs to this execution... a batch is a batch whichever side of the stop it
drained on" - describes a case the break condition prevents. The last batch always drains inside the
loop.

The call is **not** dead, though, and must not simply be deleted: `execute_actions()` ends by driving
`actuator_execute_` (`:667-669`), so this is the one last pass over the attached executions - and
since step 17 those executions now report their own batches from inside it. Step 30's guard depends
on exactly that.

> Correct the comment, which currently claims something that cannot happen, and say what the call is
> actually for - a final pass over the attachments, not a final batch of this execution's own. Then
> decide whether the `notify_finished()` wrapper around it stays: it is unreachable as a
> notification, and leaving it there implies a stop-path notification that does not exist.

**Related, and deliberately not reopened here:** there is no second `on_finished` when a continuous
worker actually stops with nothing left to run. That was decided, and this step does not change it -
it only stops the code from implying otherwise.

**Applied 2026-09-17**, and landed in `91c6112`. Three changes, the third decided
with this step rather than carried separately.

1. **The tail `notify_finished()` is gone.** Unreachable, and leaving it implied a stop-path
   notification that does not exist.
2. **Two comments corrected, not one.** The comment above the drain described a batch being
   reported there; it now says what the pass is for - a last pass over the attachments, not a last
   batch of this execution's own - and why this execution's own list is always empty by then. The
   `finishing_` comment immediately below it was stale in the same way: it ended "and before the
   callback, which has to be told the truth if it asks `is_running()`", and after step 17 there is
   no callback at that point at all. It now reads against what the code does.
3. **`actions_run_` is reset at the top of `execute_actions()`** (`:615`), not by its callers. The
   three resets in `execute()` and `loop()` came out with it. This was raised while probing this
   step: the reset used to belong to the two worker functions, and an execution driven through
   `attach()` runs neither, so its counter was never reset and its `actions_run_ == 0` guard was
   permanently false after the first action. The notification was still correct, but only because
   its one call site sits immediately after an action ran - which is not a property worth relying
   on. Every driver now starts a pass from zero.

**Confirmed by probe rather than by reading.** Since step 30 sets `finishing_` between the two
notification sites, `is_running()` inside the callback says which one raised it. Three arrangements
- `stop()` landing mid-batch, an action that queues another with `stop()` in between, and an
attached execution queueing back onto its attacher during the final pass - each gave
`from_drain=1, from_tail=0`, before and after the change. Nothing was receiving the removed
notification.

None of the three changes behaviour today: the call was unreachable, and the reset only mattered to
a guard that the call site's position was already masking. The suite passing identically before and
after is the expected result here, not weak evidence.

Verified: 33/33; 30x repeat, no failures; ThreadSanitizer 0 warnings and AddressSanitizer 0 errors
on **both** binaries; `tools/make_doc.sh` clean, 41 pages; clang-format clean.

---

## Group 5 — output (closed)

### Step 18 · item 14 — the header writes to `std::cout` unsynchronised — DONE
`async.hpp:712` (`execute()`), `:751` (`loop()`) · line references refreshed 2026-09-17

Two debug prints from worker threads, in a header-only library, on a stream shared with the
application. `std::println` was introduced for the warnings in steps 2 and 21 and locks the stream;
these two predate it and do not.

> Route both through `std::println`, or delete them. They are debug leftovers, not diagnostics, and
> a library should not print at all on the success path. Consider taking them out entirely and
> letting the logger you have planned own this.

**Applied 2026-09-17**, and landed in `86f343a`. Both routed through `std::println`,
which the user chose over deleting them. Each now names the execution, as the two existing warnings
at `:444` and `:641` do — with several workers running they were otherwise anonymous, which was most
of what made them useless:

```
std::println("execution '{}' finishing thread", name);
std::println("execution '{}' thread finished", name);
```

The first read `"finishing_ thread"` until now. That underscore was collateral from `f20119f`'s
rename pass catching a string literal, not something anyone wrote.

**Still open, and deliberately not settled here:** whether a library should print on the success
path at all. These are locked now, so they are no longer a race; they are still debug leftovers, and
the suggestion above to let a logger own them stands.

**Flow-on:** `async.hpp` no longer uses `<iostream>`, which shortens step 22's list.

### Step 19 · item E — the smoke test races on `std::cout` between two workers — DONE
`test/async_smoke_test.cpp:15`, `:24` · CONFIRMED under TSan, and present before `60f7970`

`A::f_with_arg` and `A::f_with_arg_and_return` print from two different workers concurrently. This
is the single remaining TSan warning in the repo, and it is the test's own code, not the header's.
Visible in the output as interleaved lines:
`A::f_with_arg 10 thread A::f_with_arg_and_return 0x...`

> `std::println` per line, or a mutex in the test. Worth doing so that "TSan is clean" becomes true
> of the whole repo and a future regression is not lost in a known warning.

**Applied 2026-09-17**, and landed in `86f343a`. Fifteen sites converted: fourteen in
`async_smoke_test.cpp` and one in `test/other_async.hpp:13`, which this item never named and which
produced the stray `test` line in the output.

**Per line, not per call, is the whole fix.** The two `on_finished` bodies built one line out of four
`<<` calls, which is exactly where another worker cut in. Converting call-for-call would have kept
that open, so the values are accumulated first and the line is printed once:

```
std::string values;
for (const auto& value : results) {
  values += std::format(" {}", value);
}
std::println("results={} value(s){}", results.size(), values);
```

A comment in the test says so, because the next person adding a line there will reach for `<<`.

Both files were relying entirely on transitive includes through `async.hpp`; they now include
`<format>`, `<print>`, `<string>` and `<thread>` for what they use directly.

**Verified, and this one needed repeating rather than a single run** — the race was intermittent, so
one clean run would have proved little. 25 consecutive TSan runs of `async_smoke_test`, 0 warnings
every time; `async_tests` 0; ASan 0 on both; 33/33; smoke exit 0 with every line intact;
clang-format clean.

**`TSan is clean` is now true of the whole repo**, which it was not before — and note that it had
been claimed once during this session on the strength of running `async_tests` alone. Both binaries,
from here on.

---

## Group 6 — API contract

### Step 20 · item B — `~execution()` suppressed the implicit moves — DONE, **declined**
`async.hpp:255` (`~execution()`) · line reference refreshed 2026-09-17

Step 3 gave `execution` a user-declared destructor, which suppresses the implicit move constructor
and move assignment. The class was already effectively non-copyable through its members, so nothing
in tree breaks, but the class now silently falls back to copy where a move was expected, and the
error a user gets is a template wall rather than a statement.

This is the same finding `actuator` closed as its item B, and the audit's hygiene note at the old
`:164` asked for it before the destructor existed.

> Rule of five: `= delete` the copy operations explicitly and decide whether moving an `execution`
> is meaningful at all. It probably is not, while `other_this` and the poll hold pointers to `this`
> — see step 24.

**Closed on 2026-09-17 by declining the instruction above, not by carrying it out.** The rule of
five was written, applied, measured and reverted. What landed instead is the reasoning, as a remark
on `~execution()`, plus a guard in the suite. Both halves of the item were probed rather than argued
about.

**The premise was wrong about the thread member** - `this_thread_` then, `thread_` since
2026-09-19. The compiler blames that field only because
`std::thread` is the first member declared. Removing the thread from a scratch header changed
nothing: `started_` is blamed next and all four traits stay false. Every atomic, both mutexes and
the condition variable delete the copy operations independently. The deletion is over-determined, so
four declarations would restate what a dozen members already enforce, and would begin to matter only
if all of them were replaced by copyable ones at once. That is not a change anyone makes by accident.

**"A template wall rather than a statement" only half holds.** Measured against a scratch header
with the rule of five applied:

| case | today | with `= delete` |
|---|---|---|
| `execution b = std::move(a)` | `call to implicitly-deleted copy constructor` (7 lines) | `call to deleted constructor` (7 lines) |
| `execution b = a` | identical message to the move | `call to deleted constructor` |
| `std::vector<execution>` | 48 lines, fails in `allocator_traits` | **48 lines, unchanged** |

So the direct case becomes a statement, and the case that actually bites a user stays a wall. The
gain is real and smaller than the item implies.

**The decision, and it is a design one:** an execution is declared once and used in place, and is
never copied, moved or assigned. `create_instance()` hands out a `std::shared_ptr`, so nothing needs
to relocate one, and four things hold pointers into a live execution that a move would leave behind
- `other_this_`, `execution_poll` holding `&action_is_running`, `attach()` holding
`&other.action_execute` and `&actuator_execute_`, and the detached worker reading `this`. The
remark on `~execution()` says this where the question arises, and reaches the generated docs.

**The guard that came with this step was removed on 2026-09-19**, by the user's call:
`execution_special_members.cannot_be_copied_or_moved` asserted four traits that `std::thread`,
`std::atomic` and `std::mutex` already enforce, so it tested the standard library rather than this
code. It could not have failed against item B either - the defect's whole effect is on diagnostic
text, which no trait reports. The intent it was meant to state lives in the remark on
`~execution()`.

A `try_compile` test asserting the diagnostic text *would* go red until the fix landed, and was
rejected: it introduces a test mechanism this repo does not have, and matches on compiler message
text across the clang/gcc/MSVC matrix, which a toolchain bump would break.

**Alignment with `actuator` was the starting point and did not survive contact.** `actuator.hpp:81-92`
declares all five, every one `= default`, because an actuator is copyable. The shape aligns; the
values do not, and here the conclusion was that the declarations are not worth their place at all.

Verified: 34/34; 30x repeat, no failures; ThreadSanitizer 0 warnings and AddressSanitizer 0 errors
on both binaries; `async_smoke_test` exit 0; clang-format clean; `tools/make_doc.sh` 0 warnings,
41 pages.

### Step 21 · item C — a refused action is reported but not returned — DONE
`async.hpp:440-480` (`add_action()`) · line reference refreshed 2026-09-17

`add_action()` on a stopped execution prints a warning and returns. The caller has no programmatic
way to learn the action was dropped — and the caller is usually the lambda from `bind()`, which
returns a default-constructed result either way.

The original audit complaint about stranded actions was that "the caller is never told". A warning
on stderr is better than silence, and less than telling.

> `bool add_action(...)`, threaded back through the `bind()` lambdas. Touches the same two lambdas
> as step 12, so land them together or accept two passes over the same lines.

**Applied 2026-09-17**, and landed in `f8472c0`. `add_action()` returns `bool` —
`false` when the execution is stopped and the action was dropped, `true` when it was queued. The
empty `@brief` stub it carried was written properly at the same time; the return value needed
documenting and there was nothing there to add it to.

**Half the instruction above is not implementable, and was dropped.** The bool cannot be threaded
back through the `bind()` lambdas: they return `actionT::result_type`, fixed by the specialisation —
`void` for `execution<function<void()>>`, `int` for an `int`-returning one — so there is nowhere to
put it. A caller arriving through `bind_action_and_method()` or `bind_action_and_function()` is
therefore still not told, and that is recorded in `add_action()`'s own documentation rather than left
to be rediscovered. It is the same ground as the carried-forward half of item 8 and as what step 28
owes for an action whose body throws.

**An exception was proposed and rejected**, on 2026-09-17. The test first asserted that a refused
action throws `untangle::invalid_action`, on the reasoning that a bound call on a *destroyed*
execution already throws exactly that while a *stopped* one returns silently — two answers to the
same "your action will never run". The user rejected the premise: `invalid_action` means a binding
whose target has died, so the action itself is broken, while a refusal is a sound action meeting an
execution that is closed to new work. Conflating them would blur both the catch site and the
exception's own meaning. **An answer, not a fault** — the distinction is now in the header.

**Measured before any of this was written**, 2026-09-17. On an `execution<function<int(int)>>`, a
bound call that was queued and really ran returned `0`, and a bound call refused after `stop()` also
returned `0`; the accepted one's result was sitting in `results()` at the time. `add_action()`'s own
return type was `void`, so a direct caller had nothing to check either.

**Test:** `execution_queue.tells_the_caller_when_an_action_is_refused`, sitting beside
`refuses_an_action_queued_after_the_worker_stops` — that one pins that a refused action does not run,
this one that the caller finds out. Two notes on its construction, because neither is obvious:

- It asserts the **return type** before exercising it, so the case builds against a `void`
  `add_action()` and fails as an expectation rather than as a compile error that would take the
  whole suite down.
- The behavioural half lives in a **templated lambda**. A discarded `if constexpr` branch is still
  instantiated outside a template, so written directly in the test body those calls would not
  compile against the old signature. Inside a templated lambda the compiler really does skip them.

Confirmed to go green with the fix, not merely red without it: against a scratch header, `true`
before `stop()`, `false` after, and the action count unchanged.

**Also fixed:** the warning read `execution '...' is stopped_, action not added`. That underscore was
`f20119f` rename collateral catching a string literal — the third such found this session, after
`"finishing thread"` and the `is_running()` comment.

**Not a behaviour change for existing callers**, which is the risk worth stating: ignoring a new
return value compiles cleanly, so every direct caller in tree stays exactly as silent as before until
it is changed to check.

Verified: 35/35; 30x repeat, no failures; ThreadSanitizer 0 warnings and AddressSanitizer 0 errors on
both binaries; `async_smoke_test` exit 0; clang-format clean; `tools/make_doc.sh` 0 warnings, 41
pages — after fixing two `\ref`s in the new documentation that do not resolve in this file.

### Step 28 · item G — an action that throws anything but `invalid_action` terminates — DONE
`async.hpp:670-690` (the catch site) · CONFIRMED: `libc++abi: terminating due to uncaught exception`
· line reference refreshed 2026-09-17

Found on 2026-09-14 while prototyping a thread pool on top of `execution` — see
`prototypes/README.md`.

`execute_actions()` wraps each action in a `try` that catches `invalid_action` and nothing else, so
any other exception leaves the detached worker thread and calls `std::terminate`. Probed with a task
throwing `std::runtime_error`: the process dies, and it dies whatever else was queued.

This is the other half of step 2. That step stopped a *dead binding* from killing the process, which
was the failure the header could produce by itself, and it was the right fix for what was in front
of it. What it did not cover is an action whose own body throws — and that is the caller's code, not
the header's, so nothing constrains what comes out of it.

It has stayed harmless because every action in tree is a binding the header made, and those throw
`invalid_action` or nothing. It stops being harmless the moment an execution runs arbitrary caller
code: a pool cannot ship on a worker that dies with the first task that throws, and neither can any
caller who queues work they did not write.

> Catch `...` around the action, not just `invalid_action`, and keep the worker alive. Then decide
> what the caller is told, which is the same question step 21 asks about a refused action: the
> submitter of a task that threw has no more way to learn about it than the submitter of an action
> that was dropped. **Settle the two together** — one answer for "what happened to the action I
> gave you", rather than a warning on stderr for one case and a different mechanism for the other.

A warning naming the execution, as step 2 already prints for a dead binding, is the floor. Anything
better means the results question from steps 14 and 15 reappears for failures, so keep this cheap
unless there is a use case pushing it.

**Landed 2026-09-17 in `b14373e`.** Re-probed first, and it is worse than "latent": an action
throwing `std::runtime_error` killed the process on **both** paths, `run()` and `start()` alike,
exit 134, with nothing queued behind it ever running.

**Three catch arms, not the one the instruction asked for.** `invalid_action` keeps its own message.
A `std::exception` gets its own arm so the warning can carry `what()` — a bare `catch (...)` would
have satisfied the instruction and thrown away the only useful detail. Everything else lands in
`catch (...)`. All three name the execution and drop the action, which is what step 2 already did
for a dead binding: a throwing action is not a special kind of failure, it is the second kind the
worker has to survive.

**The `catch (...)` arm was probed on its own**, with an action throwing a bare `int` and another
throwing a plain struct. With the `std::exception` arm in front of it, a broken catch-all would look
identical in the suite — both were caught, both warned, and the worker carried on.

The action is still counted as having come off the queue, so its batch reports finished like any
other; the comment saying so listed two outcomes and now lists three.

**The other half was left open, deliberately, and step 38 closed it.** What the submitter is told
had not changed here: step 21 could answer a refused action with a `bool` because the caller was
standing there, while the submitter of a throwing action is long gone by the time it runs, so the
stderr warning stayed the floor. The way out was not the return type — there is no room in it for
anyone, bound or direct — but a handler the caller assigns, which is step 38's `on_error`. The
warning is still the floor for a caller who assigns nothing.

**Test:** `execution_queue.an_action_that_throws_does_not_kill_the_worker`. It **aborted rather than
failed** before the fix, because that is what the defect did — `gtest_discover_tests` gives every
case its own process, so it took down that one case and not the suite, the same arrangement
`does_not_reach_an_attached_execution_that_has_been_destroyed` relies on. Its doc comment says to
expect that, so it is not read as broken.

Verified: 36/36; 30x repeat, no failures; ThreadSanitizer 0 warnings and AddressSanitizer 0 errors
on both binaries; `async_smoke_test` exit 0; clang-format clean; `tools/make_doc.sh` 0 warnings,
41 pages.

### Step 29 · item H — the `action_*` members are public internal seams — DONE, by documenting them
`async.hpp:600-624` · raised by the user on 2026-09-15, while reviewing the step 17 tests. Not an
audit finding.

`action_execute`, `action_stop` and `action_is_running` are public `std::function`s wired by the
constructor at `:229-231`. A caller who assigns to one silently unwires the attachment or the poll.
**Probed 2026-09-17:** overwriting an attached execution's `action_execute` with an empty lambda and
driving its attacher left the queued action **unrun** while `is_busy()` went on reporting `true` —
work queued, nothing left that will ever run it, and no sign of it except that nothing happens.

**Closed without changing the access, by the user's decision on 2026-09-19.** They are *connection
points*, not internal seams: an actuator belonging to another object connects by taking a member's
address, so being reachable is what they are for. Making them private would mean granting friendship
to every object that may ever connect — `execution_poll` today, anything tomorrow — and assigning to
one is gross misuse rather than an accident waiting to happen. What landed is a short doc comment on
each of the three, plus one on `on_finished`, naming what it is bound to and warning that assigning
unwires silently.

**What was written and then dropped**, recorded so it is not rediscovered:

- A detection trait asserting the members are unreachable. It pinned the wrong property — the harm
  is *assignment*, not reachability — and was removed with the decision. The idiom itself works:
  access checking happens during substitution, so a private member makes such a trait SFINAE away
  rather than fail to compile.
- The seam survey the access change would have needed. `attach()`/`detach()` need nothing
  (`template <typename otherActionT> friend class execution;` at `:184`); `execution_poll::add()`
  and `remove()` take `&async_exec.action_is_running` and are not friends; `async_tests.cpp` drives
  `action_execute()` at **five** sites —
  `does_not_reach_an_attached_execution_that_has_been_destroyed`,
  `detach_stops_an_attached_execution_from_being_triggered`, `detach_unwires_the_stop_path_as_well`,
  `allows_a_chain_of_attached_executions` and `refuses_a_cycle_that_closes_through_a_third_execution`
  — as the only way to drain on the calling thread with no worker to wait on. Four of those five
  could be driven by a real worker; the first cannot, because ASan must attribute the dangling read
  to the test thread.

**A finding for the `actuator` submodule, not for this repo.** `actuator<action_t>` does not require
`action_t` to be a `std::function`. Read from `actuator/actuator.hpp`, it needs only
`action_t::result_type` (`:67`), `(*action)(args...)` (`:141`), `!*action` (`:135`) and
`*action == nullptr` in `connect()` (`:260`). A callable, addressable, **non-assignable** wrapper
therefore satisfies every connection this header makes — probed 2026-09-19 against the real header
with the project compiler: `connect()`, `add()`, `remove()`, void and `bool` results and the
dead-binding drop all worked, while assignment stopped compiling. Not taken here: `std::function`
stays this repo's accepted callable. The user's note is that the actuator's "works only with
`std::function`" statement could be reworked there instead.

### Step 32 · item K — `finishing_` was redundant and is gone — DONE
`async.hpp:305` (`is_running()`), `:744` (`running_`) · **investigation, closed by removing a flag**

Raised by the user on 2026-09-16: the two flags look like they overlap. They did. Read from the code
on 2026-09-19, they encoded three meanings in two bools, with the fourth combination reachable but
not distinct:

| `running_` | `finishing_` | meant |
|---|---|---|
| false | false | never started |
| true | false | working - `is_running()` true |
| true | true | winding down: work over, worker still inside the object |
| false | true | gone; the terminal state after a run |

**What `finishing_` bought, and it was only this:** `is_running()` went false a few instructions
early - while the worker was still printing its farewell and had yet to clear `running_`. Nothing
read that difference for its own sake, and step 17 had already taken away its other job: the
notification is raised from inside the drain, before any such store.

**What it cost was step 36.** That early false is precisely what invited a caller to start a second
run into the first worker's tail, which is where the two workers, the shared handshake and the
use-after-free came from. The flag did not merely fail to earn its place; it opened the door.

> `finishing_` deleted. `is_running()` is `running_.load()`, so it means what `~execution()` and
> `wait_thread_to_finish()` already meant by it: **a worker is still there**. The two stores in
> `run()`/`start()` and the two in the worker tails go with it - seven lines added, twenty-seven
> removed.

**Deliberately not done:** no enum, no `idle`/`working`/`finishing` state, no rename of `running_`.
The user's call, and the right one - once the redundant flag is gone there is one thing to say and
one flag saying it, and a three-valued state would only re-introduce a distinction nothing reads.

**No test could fail on this**, and none was invented. The semantics that moved are the ones no case
asserts: `a_run_batch_does_not_claim_the_worker_stopped` checks `is_running()` **after** waiting for
the worker, which holds either way, and
`an_execution_running_its_last_actions_does_not_report_itself_finished` pins the true answer during
the last drain, which is unchanged. Four test comments described the two-flag world and were
rewritten.

**Verified:** 41/41 Debug with a 30x repeat; 41/41 under AddressSanitizer and under
ThreadSanitizer; `async_smoke_test` exit 0 on all three, 0 TSan warnings; the flake that led here
**0 of 20 batches** of 50 TSan repeats; clang-format clean; `tools/make_doc.sh` 0 warnings, 41
pages.

---

## Group 7 — hygiene

### Step 22 · hygiene — headers used but not included — DONE
`async.hpp:8-23` · verified by reading the code, 2026-09-19

Step 10 added four. **Five more were missing, not the four the entry used to list.** Read from the
code rather than from the old list:

| added | used at |
|---|---|
| `<chrono>` | `:231`, `:707` — `std::chrono::milliseconds` |
| `<functional>` | 13 sites — `std::function`, and `std::bind` at `:422` |
| `<list>` | `:743` — `std::list` |
| `<type_traits>` | `:577`, `:785` — `std::is_void_v`, `std::conditional` |
| `<utility>` | 5 sites — `std::move`, and `std::forward` at `:178` |

**`<memory>` was already there**, added by step 10; the old entry listed it wrongly. **`<utility>`
was not listed at all** and is the one this step would have missed.

**Where they were coming from.** `<actuator/actuator.hpp>` includes `<functional>`, `<list>`,
`<type_traits>` and `<utility>` among others, so those four arrived with it. `<chrono>` does not
come from there: it arrives through `<thread>` and `<condition_variable>`, which is a libc++
implementation detail, not a guarantee — probed 2026-09-19 with a two-line translation unit.

**`<iostream>` came off this list on 2026-09-17**, when step 18 replaced the last two `std::cout`
calls in the header with `std::println`. Nothing in `async.hpp` uses it any more.

> Include what you use. Cheap, and it stops a change in `actuator` - or a different standard
> library - from breaking this header.

**The reverse direction was checked too:** all fifteen includes are used, none is dead.

**Verified:** 35/35; `async_smoke_test` exit 0; clang-format clean.

### Step 23 · hygiene — the whole forward-declaration block goes — DONE
`async.hpp:24` (where it was) · verified by reading the code, 2026-09-19

The forward declaration of `untangle::actuator` sat below `#include <actuator/actuator.hpp>`, which
defines it at `actuator/actuator.hpp:56-57`, so it redeclared an already-complete type.

**The entry's other claim did not hold: the `execution` declaration was dead too**, and the user
took it out with the first on 2026-09-19. Nothing between the block and the definition named
`execution` in code; the only mentions are `\ref` in comments, which doxygen resolves against the
definition. Probed before removing: both binaries compile, 35/35 pass, and doxygen emits the
identical set of `.tex` files - equal but for the source listing, which is the header itself.

> Both declarations deleted; the block is gone and the includes now run straight into
> `namespace untangle { namespace async {`.

**Verified:** 35/35; `async_smoke_test` exit 0; clang-format clean; `tools/make_doc.sh` 0 warnings,
41 pages.

### Step 24 · hygiene — `other_this_ = this` was pointless indirection — DONE
`async.hpp:188-192` (the constructor) · verified by reading the code, 2026-09-19

The constructor stored `this` in a member and bound the three connection points through it. It
bought nothing: `untangle::bind(class_t*, method)` at `actuator/actuator.hpp:364` **captures the
pointer by value** in its lambda, so what the member held afterwards was never read again. It was
also a stale-pointer trap the moment the class gained a copy or move - see step 20.

> Bound through `this`; the member is gone. `execution* other_this_;` was also the only member
> without an initialiser.

**It retires one of the four pointers step 20 listed** as reasons an execution must not be moved.
Three remain: `execution_poll` holding `&action_is_running`, `attach()` holding
`&other.action_execute` and `&actuator_execute_`, and the detached worker reading `this`. The case
for not moving is unchanged.

**Verified:** 35/35 Debug; **35/35 under AddressSanitizer and 35/35 under ThreadSanitizer**, both
reconfigured for this step because the directories were gone; `async_smoke_test` exit 0 on all
three, 0 TSan warnings; clang-format clean; `tools/make_doc.sh` 0 warnings, 41 pages.

### Step 25 · hygiene — `add_action` copied the action and every argument twice — DONE
`async.hpp:394` (`add_action`), `:262-276`, `:292-305` (the two `bind()` overloads) · CONFIRMED by
counting, 2026-09-19

`add_action(actionT action, Args... args)` took everything by value and then handed those copies to
`std::bind`, which copies again. Counted with two types that tally their own copy constructors, on
an action that is queued and never run:

| | action | argument |
|---|---|---|
| before | 2 | 2 |
| `Args&&... args` + `std::forward` | 2 | **1** |
| + `std::bind(std::move(action), ...)` | **1** | 1 |

So one copy of each came from the by-value parameter and one from `std::bind` — the two halves
attributed separately, by changing one thing at a time. What remains is the copy the queue needs:
the argument's lands in the bind object, the action's in the by-value parameter, which is the sink
the caller keeps their own copy behind.

**The bound path had the same defect and is fixed with it**, as the plan said to do: both `bind()`
lambdas took `auto... args` by value, which put the second copy back however `add_action()` was
written. `auto&&... args` forwarded on. Measured the same way: 2 copies before, 1 after.

> `add_action(actionT action, Args&&... args)`, queueing
> `std::bind(std::move(action), std::forward<Args>(args)...)`; `auto&&... args` in both lambdas.

**Tests:** three cases, reading the counters as a **delta** rather than from zero, so they hold when
the binary is run directly and every case shares one process:
`queueing_copies_the_action_and_its_arguments_once` (an action the caller keeps: one copy, was two),
`queueing_an_action_the_caller_gives_up_copies_it_not_at_all` (moved in, or a temporary: none), and
`queueing_through_a_binding_copies_the_argument_once`.

**What each call shape costs, measured 2026-09-19 against the fixed header:**

| call | action copies | action moves |
|---|---|---|
| `add_action(action, ...)`, an lvalue the caller keeps | 1 | 0 |
| `add_action(std::move(action), ...)` | **0** | 0 |
| `add_action(counter{}, ...)`, a temporary | **0** | 1 |
| `add_action([]{...}, ...)`, a lambda | **0** | 0 |
| one invocation of a bound action | 1 | 0 |

**Building a binding was copying too, and that is fixed here as well** - the user's question,
2026-09-19. `bind_action_and_function()` took its callable by `const T&` and copied it into the
action, then the lambda captured that action by copy: two copies before a single call was made.
`bind_action_and_method()` had the second of those. Both lambdas now capture with
`async_action = std::move(async_action)`, and the plain-function overload takes `T Fn` by value and
moves it in - the same sink shape as `add_action()`.

| building a binding | callable copies | moves |
|---|---|---|
| a temporary, before | 2 | 1 |
| a temporary, after | **0** | 2 |
| an lvalue the caller keeps, after | 1 | 2 |

Pinned by `execution_binding.building_a_binding_copies_a_callable_the_caller_gives_up_not_at_all`.

**The by-value parameter is what makes that table possible, and is why there is no second
overload.** `add_action(actionT action, ...)` is a sink: an rvalue is constructed straight into the
parameter and `std::bind` then steals it, so `std::move()`, a temporary and a lambda all cost
nothing. A second `actionT&&` overload could not improve on zero and would be ambiguous against the
by-value one for every rvalue call. The lvalue copy is the caller's own choice to keep their action;
the queue must still own one.

**A move-only argument still will not compile, and forwarding was never going to fix that** - the
claim carried in item 8 and in step 12's "still open" note was wrong. Probed 2026-09-19: `std::bind`
holds a `unique_ptr` happily, but the queue element is `std::function<result_type(void)>`, which
requires a **copy-constructible** target; storing such a bind object is a hard error inside libc++'s
`__clone`, not a substitution failure, so even `std::is_constructible_v` answers `true` for it.
`std::move_only_function` would take it and **does not exist in this libc++** (Apple clang 21). The
blocker is the queue's element type, and changing that is its own decision, not hygiene.

**Also still open, and untouched here:** the lambdas return a default-constructed
`actionT::result_type`, so an async call to an `int`-returning method yields 0. That is the results
question of steps 14, 15 and 21, not a copying one.

**Verified:** 39/39 Debug, 39/39 under AddressSanitizer, 39/39 under ThreadSanitizer with 0
warnings; `async_smoke_test` exit 0 on all three; clang-format clean; `tools/make_doc.sh` 0
warnings, 41 pages.

### Step 34 · a bound action was copied once per invocation — DONE
`async.hpp:573` (`add_queued_action()`), `:262`, `:296` (the two `bind()` overloads) · CONFIRMED by
counting, 2026-09-19

Each `bind()` lambda captured its action and handed it to `add_action()` as an lvalue, because it
has to keep its own for the next call. `add_action()` takes `actionT` by value, so every call
through a binding copied a `std::function` - and libc++ heap-allocates the target of one whose own
target is not nothrow-copy-constructible.

**Measured with a counting `operator new`**, 1000 calls through a binding at `-O1`:

| per call | action copies | allocations |
|---|---|---|
| before | 1 | 3 |
| after | **0** | **2** |

The three were the copied action's target, the `std::bind` object inside the queued
`std::function`, and the `std::list` node. The first is what went.

> A binding now holds its action once, in a `std::shared_ptr<const actionT>`, and queues a callable
> carrying the pointer and the bound arguments - so a call costs a reference rather than a copy.
> That callable is already nullary, which `add_action()` cannot take: its parameter is `actionT`,
> the action type **with** its arguments, while the queue holds
> `std::function<result_type(void)>`. Hence `add_queued_action()`, private, holding what
> `add_action()` does once it has bound one; `add_action()` is now a one-line forwarder onto it.

**Why not just pass the lambda to `add_action()`** - the first thing to try, and it does not
compile: *no known conversion from '(lambda)' to 'std::function<void (int)>'*. It **does** compile
when `actionT` is itself nullary, which is most of the test suite, so the idea looks right until
`execution<std::function<void(int)>>` - the smoke test's own type - stops building.

**Why not an overload of `add_action()`:** when `actionT` is nullary the two have identical
parameter lists, so which one a call selects would depend on the specialisation.

**The indirection the entry warned about does not show up.** Queue-and-drain of 200k calls at
`-O2`, medians of nine runs:

| | queue | drain | total |
|---|---|---|---|
| before | 31 ns/call | 43 ns/action | 75 ns |
| after | 27 ns/call | 31 ns/action | 58 ns |

**Test:** `execution_binding.calling_a_binding_does_not_copy_the_action` - two calls through a
binding, 2 copies before and 0 after.

**What is left, and it belongs to step 35:** two allocations per call remain - the `std::list` node
and the queued callable's target, where a 16-byte `shared_ptr` plus arguments plus libc++'s vptr
overflows the 24-byte small buffer. Both need the queue's container or element type to change.

**Verified:** 42/42 Debug with a 30x repeat; 42/42 under AddressSanitizer and under
ThreadSanitizer; `async_smoke_test` exit 0 on all three, 0 TSan warnings; clang-format clean;
`tools/make_doc.sh` 0 warnings, 43 pages.

### Step 35 · what the queue holds — A DONE, B and C recorded
`async.hpp:778` (the action queue), `:573` (`add_queued_action()`) · investigation, prototyped
2026-09-19

Raised by the user on 2026-09-19 out of step 25: should `add_action()` become private, with a
separate public entry point for lambdas and movable actions? Step 25 answered the first half - the
by-value parameter already makes `add_action()` a sink, so `std::move()`, a temporary and a lambda
all reach the queue with **zero** copies, and a second overload could not improve on that. What was
left is the real question: **what the queue holds**.

**Prototyped outside the header**, 200k queue-and-drain operations at `-O2` with a counting
`operator new`, four combinations of element type and container:

| queue element / container | allocations/op | queue | drain |
|---|---|---|---|
| `std::function` in `std::list` - before | 2.00 | 16 ns | 26 ns |
| `std::function` in `std::deque` | 1.01 | 11 ns | 17 ns |
| a move-only holder in `std::list` | 1.00 | 8 ns | 10 ns |
| a move-only holder in `std::deque` | **0.01** | **5 ns** | **5 ns** |

Two allocations per queued action, and they come off independently.

#### A - `std::list` becomes `std::deque` — DONE

The list was used for exactly four things: `empty()`, `push_back()`, `front()`, `pop_front()`. All
four are `std::deque`'s as well, so this is a one-line change with no interface and no semantic
difference, and `<list>` came off the includes with it. The member is `action_queue_` now, at the
user's call - it had been `action_list_` after the container it no longer is.

**Measured on the header**, 1000 calls through a binding: **1.01 allocations per call, down from
2.00** - the node allocation is amortised across a deque block. Queue-and-drain of 200k calls,
medians of nine runs: 56 ns per action against 58 ns, which is within the noise; the win here is
allocation count rather than time, and it compounds with B.

**Verified:** 42/42 Debug with a 30x repeat; 42/42 under AddressSanitizer and under
ThreadSanitizer; `async_smoke_test` exit 0 on all three, 0 TSan warnings; clang-format clean;
`tools/make_doc.sh` 0 warnings, 43 pages.

#### B - a move-only holder in place of `std::function` — **open, recorded 2026-09-19**

About 40 lines: an inline buffer, a call pointer, a destroy pointer. It removes the remaining
allocation - the queued callable's target, where a 16-byte `std::shared_ptr` plus the bound
arguments plus libc++'s vptr overflows the 24-byte small buffer - and it is the only thing that
makes **move-only work storable at all**: `std::function` requires a copy-constructible target, and
`std::move_only_function` does not exist in this libc++ (Apple clang 21), re-checked 2026-09-19.

**What A left, and what B would take.** The first two rows are measured on the header itself, 1000
calls through a binding at `-O1` and 200k queue-and-drain at `-O2` (medians of nine runs). The third
is a projection, not a measurement - see below:

| | allocations per queued action | queue + drain |
|---|---|---|
| before step 34 | 3.00 | 75 ns |
| after step 34 (action shared) | 2.00 | 58 ns |
| **after A (deque)** - where the header is now | **1.01** | **56 ns** |
| after B (move-only holder) - projected | ~0.01 | ~35-40 ns |

The projection comes from the prototype's own two rows, which isolate the element type with the
container held at `deque`: `std::function` 28 ns per operation against the holder's 10 ns. The
header's 56 ns is that same work plus a mutex, a condition-variable notify and the results check, so
B can only take the element's share of it - call it 18 ns of the 56, and less once the holder carries
a real move function pointer instead of the prototype's `memcpy`. **Anyone taking B should re-measure
rather than trust this row.**

> Two caveats from the prototype, both real. Its move constructor used `memcpy`, which is valid only
> for trivially relocatable callables - a real holder needs a move function pointer, so the 5 ns
> above would rise. And it is 40 bytes against `std::function`'s 32: fewer allocations, larger
> elements, which a deque absorbs better than a list did.

**The case against is not performance but apparatus.** This header has so far preferred to have
none, and B is a type of its own to maintain. Land it only if move-only work is wanted, or if the
last allocation per action is worth 40 lines.

#### C - `add_queued_action()` becomes public — **open, recorded 2026-09-19**

Step 34 already built the seam. Making it public is the second entry point this step started from:
a caller who has bound their own work hands over a ready-made nullary callable and pays no copy,
while `add_action()` stays exactly as it is for callers who want the library to bind for them.

**Only interesting with B.** Without it the public path already costs nothing for an action the
caller gives up, and the entry point would buy only the `std::bind` call. With B it becomes the way
move-only work is queued.

**No test can fail for any of the three.** A is invisible from outside; C adds a name; and a case
that queues a move-only lambda does not fail today, it fails to **compile**, which would take the
suite with it. When B lands, its test is a new case that only compiles once the element type allows
it.

### Step 26 · hygiene — `result |= ret` on a bool — DONE
`async.hpp:135` · verified by reading the code, 2026-09-19

Bitwise-or on a bool in `execution_poll::is_running()`, where logical-or is meant. **Behaviour was
never wrong** - `|=` and `||` agree on two bools, and the loop's `ret` is a `std::vector<bool>`
proxy that converts to one - so no test could fail on this and none does. It is a readability fix,
and `||` also stops evaluating once the answer is known.

> `result = result || ret;`

**Coverage added while here, because the fold had none.** Nothing in the suite registered more than
one execution with the poll at a time, so "reports running while any one of them is" - the whole
point of the class - was never asserted.
`execution_poll.reports_running_while_one_of_several_executions_is` holds one execution's action
open, registers an idle one first so the fold starts from `false`, and expects the poll to report
running, then idle once released. It passes before and after the change, as it must; it is coverage,
not a reproduction.

**Verified:** 40/40 Debug, 40/40 under AddressSanitizer; `async_smoke_test` exit 0; clang-format
clean; `tools/make_doc.sh` 0 warnings, 41 pages. ThreadSanitizer is 40/40 too, but see step 36:
one case there is flaky for reasons that predate this step.

### Step 36 · two workers in one execution — DONE
`async.hpp:341`, `:359` (`run()`, `start()`), `:694` (the wait) · CONFIRMED: ASan
`heap-use-after-free`, deterministic

Found as a flaky test - `execution_results.are_cleared_between_runs` failing about one iteration in
a hundred under ThreadSanitizer, reporting `results()` as `{}` or as the previous run's `{1}`. The
flake was the narrow door onto a real defect.

**What it was.** `run()` never asked whether a worker was already there. A second `run()` spawned a
second one, and both shared `running_` - which is not merely a status flag but the handshake
`~execution()` waits on. The first worker to finish cleared it, the destructor's wait was satisfied
while the other was still inside the object, and the object was freed under it.

**Probed 2026-09-19.** Two `run()` calls back to back, then letting the object go: **5 of 5 runs**
aborted under AddressSanitizer with `heap-use-after-free` at `async.hpp:629`, written by the
surviving worker into the block the destructor had already returned. The flaky case is the same
thing arriving by timing: `is_running()` goes false when `finishing_` is set, *before* `running_` is
cleared - deliberately, since steps 17 and 30 - so a caller told the run is over starts its second
run into the first worker's tail.

> `run()` and `start()` now call `wait_thread_to_finish()`, which takes `running_` with a single
> `compare_exchange_weak`, waiting 1ms at a time while a worker still holds it. One step rather than
> a wait followed by a store, so two callers racing cannot both pass it. The destructor's own spin
> on `running_` is unchanged.

**Chosen over refusing.** A refusal would have been the `add_action()` shape - an answer rather than
a wait - but it breaks the legitimate pattern the flaky case uses: a caller told the work is
finished runs again straight away, and has no other way to know when that is safe, precisely because
`is_running()` goes false first. Waiting keeps every published contract and costs the caller the
remainder of a tail.

**@attention on both, and it is new:** calling `run()` or `start()` **from inside an action** now
hangs - the worker that must leave is the one making the call. Probed on the old header first: it
did not deadlock there, it silently started a second worker, which is the defect itself. A hang in
place of corruption is the better failure, and it is documented on both functions rather than
detected.

**Test:** `execution_lifecycle.a_second_run_does_not_leave_two_workers_in_one_execution`. Twenty
double runs, each let go at once so the destructor's handshake meets both workers. It **aborted
rather than failed** under ASan before the fix, 3 of 3 runs, and passes after. In a plain Debug build
it passed either way, which the case says out loud: nothing observable goes wrong there.

**Also renamed, at the user's call:** the thread member `this_thread_` is now `thread_`, which no
longer reads like `std::this_thread`.

**Step 32 would subsume this.** One atomic `idle`/`working`/`finishing` makes "a worker is resident"
a state rather than a flag shared by two meanings, and `run()`'s wait becomes a transition. This fix
does not block that; it closes the use-after-free in the meantime.

**Verified:** 41/41 Debug, 30x repeat with no failures; 41/41 under AddressSanitizer and under
ThreadSanitizer, 10x repeat each with no failures; `async_smoke_test` exit 0 on all three, 0 TSan
warnings; the original flake
**0 of 40 batches** of 50 TSan repeats, where it was 21 of 40; clang-format clean;
`tools/make_doc.sh` 0 warnings.

### Step 37 · hygiene — test comments carry plan-sized narrative — DONE
`test/async_tests.cpp` (throughout) · added and done 2026-09-19, the user's own

Step 33 did this for `async.hpp` and left the test file reading the way the header used to: 591 of
1456 lines comment, case comments of 22 to 29 lines, and 17 lines naming a step, an item or a date.

> Every one of the 40 case comments rewritten to say what the case does and what it asserts. Kept:
> what a reader cannot get from the code - that a case is sanitizer-sensitive, that counters are
> read as deltas because one process may run every case, that a case is a guard rather than a
> reproduction, why a case drives synchronously or waits. Dropped: how the defect was found, what it
> measured on which date, which step moved which contract.

**352 comment lines of 1217**, down from 591 of 1456. No case comment is over 12 lines, and no
inline comment over two. No case was renamed: the names are the contract.

**Two comments were not just long but wrong**, and that is what a sweep is for:

- The **file's own header** still said the suite was "expected to FAIL against the current header",
  describing `action_list` as a bare `std::list` with nothing between the caller and the worker.
  True before step 1, which was 36 steps ago.
- `wait_until_poll_idle()` explained that "execution_poll::add() has no inverse, so an execution
  stays registered after it has been destroyed". Step 4 gave it `remove()` and ~execution() has
  called it ever since.

**Verified:** 41/41 Debug with a 30x repeat; 41/41 under AddressSanitizer and under
ThreadSanitizer; `async_smoke_test` exit 0; clang-format clean.

### Step 33 · hygiene — doc comments carry plan-sized narrative — DONE
`async.hpp` (throughout) · added and done 2026-09-19, the user's own

512 of the header's 958 lines were comment. The volume was not the complaint; the altitude was.
Several comments explained how a defect was found, what was tried and rejected, and which test
pinned the result — `~execution()` spent 21 lines on why there is no rule of five, `attach()` 25,
`add_action()` 30, `is_busy()` 20. A reader of the API wants what a thing is and what it does; the
reasoning is here and in the git history, and saying it twice means it drifts in one of the two.

> Every comment trimmed to what the entity is, what it does, and what a caller must not do. The
> `@attention` and `@remark` lines that state a **contract** stay: `on_finished` sees
> `is_running()` true, a refusal is the return value of `add_action()`, a false `is_busy()` is
> durable only for the caller adding the actions, `running_` is cleared last and the object may be
> freed the instant it reads false. The implementation comments that explain why the code is
> *ordered* as it is stay too — they are contracts of another kind.

**393 comment lines of 839.** The longest blocks are `add_action()` and `attach()` at 21 and 19
lines, of which 7 and 6 are `@param`/`@return`/`@throw`; nothing else is over 17. **No
implementation comment runs more than two lines** — the second pass cut those too, after the first
left five of them at three to five lines. A `//` comment inside a function says the one thing the
code cannot: why an order, a
lock or a bound is what it is.

**Two real defects found while sweeping, not just prose:**

- `is_running()` carried **two** doc comments, stacked. The first was a leftover — "Checks if this
  execution has finished", with `@return true - The execution has not finished` — contradicting the
  live one below it. Doxygen takes the last, so the generated docs were right and the header was
  not.
- `run()` and `start()`, the two ways to start a worker, had **no documentation at all**. Each now
  has a brief: `run()` is one-shot and fills `results()`; `start()` is continuous, reports
  `is_running()` true for its whole life, and collects nothing.

**Verified:** 35/35; `tools/make_doc.sh` 0 warnings, 41 pages; clang-format clean. Landed in
`588b7ca`, the fix plan with it.

---

## Group 8 — build (closed)

### Step 27 · item F — C++23 raises the toolchain floor; CI may not clear it — DONE
`test/CMakeLists.txt:7` · CONFIRMED by a CI run, 2026-09-14

`std::println` moved the project from C++20 to C++23. Locally that is fine (Apple clang 21). The CI
matrix is `macos-latest`, `windows-latest`, `ubuntu-latest` on default compilers, and `ubuntu-latest`
ships GCC 13 by default while libstdc++ got `<print>` in GCC 14.

It failed exactly as predicted, on the ubuntu job:

```
async.hpp:15:10: fatal error: print: No such file or directory
   15 | #include <print>
```

Note what the failure is not: `-std=c++23` is accepted by GCC 13, so nothing complains about the
standard. Only the include fails, which is why this surfaces as a missing header rather than as a
"C++23 not supported" diagnostic.

> The compiler is pinned in the workflow rather than the header guarded: a `Use GCC 14 on Linux`
> step installs `g++-14` and exports `CC`/`CXX`, conditioned on `runner.os == 'Linux'`. macOS
> (libc++ 18+) and Windows (MSVC 17.9+) already ship `<print>`, so this is a Linux-only gap and
> guarding the include would spread `#if` through the header to fix one runner.

Taken from `fluxcpp`, which hit the same wall and solved it this way; the step is copied verbatim,
comment included, so the two projects do not drift. Note that `actuator` is **not** a precedent here
— it never included `<print>` at all and reports through `std::cout`, which is why its CI was always
green on GCC 13.

**Left open:** the same guard question returns if the planned logger replaces `std::println`, and
step 18 still wants the two remaining `std::cout` calls routed through it.

### Step 38 · item L — what an action throws reaches a log and no code — DONE
`async.hpp:655` (the member), `:762-777` (the arms), `:814` (`report_error()`) · CONFIRMED by test:
`what_an_action_throws_reaches_the_caller`

Raised by the executor repo, whose own step 5 could not be written without it. Step 28 made a
throwing action survivable and said so plainly: *"what the submitter is told has not changed... the
stderr warning remains the floor"*. That floor is a line naming this execution — the header's own
name for its worker, nothing the caller chose — and it reaches a log and no code. An action that
failed and one that succeeded are the same from outside.

**The seam is a member beside `on_finished`**, assigned the same way and called on the same thread:

```c++
std::function<void(std::exception_ptr)> on_error;
```

All three catch arms consult it through `report_error()`, which returns whether a handler ran:

```c++
} catch (const std::exception& e) {
  if (!report_error()) {
    std::println(stderr, "warning: execution '{}' dropped an action that threw: {}", name, e.what());
  }
}
```

**A handler replaces the warning rather than adding to it.** A caller that has taken responsibility
for reporting should not be reported over, and one that has not keeps exactly the behaviour it had —
which is why the other 45 cases needed no change.

**`invalid_action` is reported too**, not only the caller's own exceptions. A dead binding is one of
the reasons an action did not run, and a caller asking why wants both answers rather than the one
the header happens to raise itself.

**`std::exception_ptr`, not a caught reference.** It is the only thing that carries what is *not* a
`std::exception` — the arm step 28 added specifically because a bare `catch (...)` throws away the
detail. Two handlers, or a string, would each lose something.

**`report_error()` catches around the handler.** It is caller code, on a detached thread, inside the
try that caught the action — precisely the shape step 28 existed to fix. An exception escaping it
would call `std::terminate`, so a broken handler costs its own warning and not the process.

**Test:** `execution_queue.what_an_action_throws_reaches_the_caller`, beside step 28's case so the
two read as a pair — that one says the worker survives, this one says the caller finds out. It reads
both arms (a `std::runtime_error` through `what()`, and `throw 42` through the catch-all) and asserts
**stderr is empty**, which is what proves the handler replaced the warning instead of doubling it.

Verified: 46/46 in Debug, under ThreadSanitizer and under AddressSanitizer; `async_smoke_test`
exit 0; clang-format clean; `tools/make_doc.sh` 0 warnings, 43 pages.

**One repair on the way in.** `\ref invalid_action` does not resolve — it lives in `actuator.hpp`,
outside this Doxyfile's input — and `make_doc.sh` treats doxygen warnings as errors, so it would have
failed the build. Written as `untangle::invalid_action`, which is what the header's other mentions
of it already do.

## Group 9 — the queue becomes an actuator (open)

### Background · `action_queue_` becomes an `actuator` — the POC, measured
`async.hpp:923` (the deque), `:690` (`add_queued_action()`), `:736` (`execute_actions()`), `:403`
(`is_busy()`), `:836` (`notify_finished()`), `:883-893` (`loop()`), `:714` (`execute_action()`) ·
POC measured 2026-09-23 against actuator `43fefca`

Raised by the user on 2026-09-23, out of the actuator's own `43fefca`: `add()` and `connect()` now
take an action **by value** and move it into `actuator::owned`, so an actuator can hold an anonymous
lambda with no named variable behind it. That is the one thing that had kept the queue from being an
actuator, and the header already uses actuators for the other two lists it keeps
(`actuator_execute_`, `actuator_stop_`).

**The POC is `git stash@{0}`, "POC using actuator instead action queue".** It adds
`actuator<queued_action_t> actuator_`, queues into it with `add(std::move(action))`, and drains by
invoking it whole and moving `actuator_.results` into `results_`. Measured, not argued: **42 of 46
green**, and the four failures are the shape of the work left.

| Failure | What it is |
|---|---|
| `execution_queue.an_action_that_throws_does_not_kill_the_worker` — **timeout 120 s** | the throwing action is retried for ever: **129,346 warnings in 2 s** |
| `execution_queue.what_an_action_throws_reaches_the_caller` — **timeout 120 s** | the same loop |
| `execution_busy.a_queued_action_makes_an_execution_busy` | `is_busy()` still reads the deque, which is now always empty |
| `execution_busy.an_execution_is_not_busy_once_its_actions_have_run` | the same, and it shows the cost: `ran` is **0**, the case ran straight past its wait |

**Why the retry loop.** An exception that is not `invalid_action` escapes `actuator::operator()`
(`actuator.hpp:336-368` has no catch-all), so `actuator_.reset()` is never reached. The list still
holds **every** action of the batch, the ones that already ran included, and the next pass runs them
all again — for ever.

**And a data race the suite does not exercise.** The existing suite is TSan-clean under the POC, but
only because nothing in it adds an action while a batch is in flight. A probe that does — 200
actions of 200 µs, a producer adding every 100 µs — reports **13 data races**, among them
`actuator.hpp:347` (the invocation loop) against `add()`, and a **vptr race in `function.h:274`**,
which is the worker calling a `std::function` that `reset()` is destroying. An actuator is not
thread-safe and was never meant to be; the header's own attachment actuators are only ever touched
by one thread. There is a drop window with it: an action added between the end of the invocation and
`reset()` is destroyed without running. Not observed in 3 × 20,000 — the window is narrow — but it
is there by construction.

**What it costs, measured at step 46.** Three allocations per queued action against the deque's
1.01 - the callable's target, the `owned` node and the `actions` node - and **61 ns per action
through queue and drain against the deque's 88**, because the drain takes the mutex once per batch
rather than once per action. More allocations, less lock traffic, and the lock traffic wins.

#### The fork: does the actuator **drive** the batch, or only **hold** it?

Everything below depends on this, so take it first.

- **Hold only** — the drain moves the pending actuator out under the lock, then walks
  `batch.actions` and calls each action through `execute_action()` inside the three catch arms
  already at `:757-775`. Per-action isolation, `on_error`, the results push and `actions_run_` all
  keep exactly the meaning they have. `actuator::operator()` is never called on this path.
- **Drive** — the drain calls `batch()`. Then three contracts have to be rebuilt **in the actuator
  repo**, for every one of its users: an action that throws must not abort the rest of the batch
  (and must not leave the batch un-reset); `invalid_action` must stop being swallowed at
  `actuator.hpp:359`, where it is written to `std::cout` and dropped, so that step 38's `on_error`
  can still be told about a dead binding; and that `std::cout` has to go, which steps 18 and 19
  removed from this repo entirely.

**Recommended: hold only.** It is the whole of the user's stated goal — one owning action list,
lambdas included — without reopening a contract in another repo that this header's tests pin down.

**The steps.** Each one starts with a test that is red before the change, and is its own commit.

### Step 39 · the invariant the design rests on — DONE
`actuator/test/actuator_test.cpp:1212-1259` · three cases, green on arrival

Moving an actuator must keep its `action_t*` pointing at live actions: `actions` holds addresses
into `owned`, and a `std::list` move transfers nodes rather than elements, so the addresses survive.
Step 40 takes the pending batch out of the shared member with exactly that move, so this is tested
rather than trusted.

**Three cases, in the actuator repo:**

- `test_move_keeps_the_handles_of_the_source` — the handle `add()` returned, and the one the named
  overload returned, still equal what the moved-to actuator points at, and the handle is the
  address of the stored action.
- `test_move_carries_the_owned_actions_and_empties_the_source` — the destination invokes through
  both the list and the map, and the source reports `is_connected() == false` with `owned` empty.
- `test_move_assignment_carries_the_owned_actions` — the same for move assignment, over an actuator
  that already held an action of its own.

**Green on arrival, 58 of 58**, so they are characterisation rather than repair — which is why they
were then checked for being vacuous, two ways, both against a patched copy of the header in a
scratch tree:

| Deliberate break | Result |
|---|---|
| `owned` as a `std::vector` | `test_move_keeps_the_handles_of_the_source` and `test_move_carries_the_owned_actions_and_empties_the_source` **fail** — the second `add()` reallocates and the first handle dangles before any move happens |
| a move constructor that **copies** `owned` and keeps `actions` as it was | `test_move_keeps_the_handles_of_the_source` **fails** |

The first break is the one the actuator's own ownership note already warns about; the second is the
failure this step exists for, and the case catches it.

**Verified:** 58 of 58 in the actuator repo, Debug; clang-format clean. The actuator repo has no
build directory of its own — configured out of tree, reusing the async build's googletest:
`cmake -S actuator/test -B <dir> -G Ninja -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=test/build/_deps/googletest-src`.

**Left for the async side:** nothing. Step 40 may be written.

### Steps 40 to 45 · the conversion — DONE, as one commit
`async.hpp:736-800` (the drain), `:690` (the add), `:403` (`is_busy()`), `:880-895` (`loop()`),
`:923` (the member) · guards written first, then the change

**Why one commit.** Steps 41 to 45 are not defects in the header as it was: a throwing action was
already isolated, `is_busy()` already answered, `loop()` already woke on an add, and a run already
kept every batch's results. They are the four ways the POC **regressed** it. So each was written as
a guard - green on the deque, red against the POC - and the conversion then had to keep all of them
green. Landing 40 on its own would have left the suite red until 45, which the plan's own rule
forbids.

**The guards, each red against the POC (`git stash@{0}`, built out of tree):**

| Step | Case | Against the POC |
|---|---|---|
| 40 | `execution_queue.queueing_during_a_batch_runs_every_action_exactly_once` | **5 data races** under TSan, `actuator.hpp:347` against `add()` and a vptr race in `function.h:274`; exit 134 with `halt_on_error=1` |
| 41 | `execution_queue.a_throwing_action_runs_once` | never returns - **1,370,266** retry warnings in 8 s |
| 42 | `execution_binding.a_dead_binding_reaches_on_error` | `on_error` told nothing, and `bind: invalid object` on **stdout**, printed by the actuator |
| 43 | `execution_busy.a_queued_action_makes_an_execution_busy`, `…is_not_busy_once_its_actions_have_run` | both fail; the second shows `ran` = 0, the wait returned before the action ran |
| 44 | `execution_queue.a_continuous_worker_is_woken_by_an_add_rather_than_by_its_timeout` | 200 rounds in **2263 ms** against a 1000 ms limit |
| 45 | `execution_results.keep_the_results_of_every_batch_in_a_run` | `{2}` instead of `{1, 2}` |

43's two cases already existed; the other four are new.

**What landed.**

`action_queue_` is `actuator<queued_action_t> action_actuator_`, named after what it is as the deque
was before it. `add_queued_action()` moves the action in with `add(action_t&&)`, so the actuator
owns it and no named variable stands behind it.

`execute_actions()` **takes the batch out and runs it outside the lock**:

```c++
actuator<queued_action_t> batch;
{
  std::lock_guard<std::mutex> lock(action_mutex_);
  if (!has_pending_actions()) {
    break;
  }
  batch = std::move(action_actuator_);
  executing_action_ = true;
}

batch();  // the actuator invokes its own actions

for (const auto& thrown : batch.errors) {
  report_error(thrown);
}
```

Three things follow from it, and they are steps 40, 41 and 45 respectively. The worker never walks a
list a producer can write to, and never holds `action_mutex_` across caller code that may call
`add_action()`. The batch is driven by **`actuator::operator()`** - the actuator invokes its own
actions, which is the point of it being one - and an action that throws cannot run twice, because
the batch it is in is not the member any more. And nothing clears `results_` per batch; `execute()`
clears it per run, as it always did. What the actions threw comes back in `batch.errors`, and what
they returned in `batch.results`.

**Driving it took two changes in the actuator**, made on 2026-09-23 and tested there. Every action
is isolated now: an exception that is not `invalid_action` used to escape `operator()` mid-batch,
skipping the actions behind it and leaving the batch un-reset. And what an action threw is recorded
in the new `actuator::errors`, a `std::exception_ptr` per failure, instead of being written to
`std::cout` - which is what settles step 42, because a dead binding that is printed reaches neither
`on_error` nor stderr. `<iostream>` came off the actuator's includes with it.

On this side, `execute_action()` is gone and `report_error()` takes what was thrown rather than
reading `std::current_exception()`: it hands it to `on_error`, or rethrows it to pick the same three
warning texts the catch arms used to print. The drain keeps one `catch (...)` of its own - around
the call and the results move, not around the actions - because nothing may leave a detached thread
function, and `batch.results` is moved into `results_` there.

**One predicate**, `has_pending_actions()`, replaces the three direct reads of the container -
`is_busy()`, `notify_finished()` and `loop()`'s wait and break. It reads `action_actuator_` and is
documented as requiring `action_mutex_`; every caller is inside a `lock_guard` or is the condition
variable's predicate, which is called with the lock held. `loop()` waking on an add is step 44, and
it is a one-line consequence of the predicate being right.

**The invariant under it all is step 39's**: `batch = std::move(action_actuator_)` moves a
`std::list` of owned actions, so the handles in `actions` still point at live actions afterwards,
and the member is left empty - `test_move_carries_the_owned_actions_and_empties_the_source` is what
says so, and the drain's comment cites it.

**Verified:** 51 of 51 on all four presets - debug, release, asan, tsan - with
`ASAN_OPTIONS`/`TSAN_OPTIONS=halt_on_error=1`; clang-format clean. `<deque>` came off the includes
with the member.

**Left of the group:** step 46's re-measurement and step 47's doc gate.

### Step 46 · what the change leaves behind — DONE
`async.hpp:13` (the include), `:739`, `:1009` (the comments)

`<deque>` came off with the member. `execute_actions()` no longer says an action "came off the
queue" - it says the batch - and `executing_action_` is documented as the batch being out of
`action_actuator_` rather than as a pop. `add_queued_action()` and `actions_run_` read correctly as
they stand: both are about queueing and about what a pass ran, neither about a container.
`tools/make_doc.sh`: **0 warnings, 43 pages**.

**The allocation count, measured 2026-09-23.** Step 35 A had the deque at 1.01 allocations per
queued action. Both headers measured here with one probe - a counting `operator new`, 1000 calls
through a binding for the count, 200k queue-and-drain for the time, medians of nine runs at `-O2`:

| header | allocations per queued action | queue + drain |
|---|---|---|
| the deque, `7e07a51^` | **1.01** | 88 ns (min 82, max 99) |
| the actuator, as it is now | **3.00** | **61 ns** (min 49, max 67) |

**Three allocations, and they are all accounted for**: the queued lambda's own target, as before;
the node `owned` stores the action in; and the node `actions` stores the pointer in. The deque's
1.01 was one target plus a block amortised across many actions.

**And it is faster anyway, by about 30%.** The drain takes `action_mutex_` once per batch instead of
once per action, and under load a batch is large - which is worth more here than the two extra
allocations cost. The figure is the whole path, queue through drain, so it includes the lock traffic
the deque paid per action.

Step 35 A's own number was 56 ns on a probe that is not this one; the comparison that counts is the
two rows above, measured the same way. **If the allocation count is ever the thing that matters more
than the lock traffic, step 35 B is still recorded and still the answer.**

### Step 47 · the gate — DONE for the conversion
51 of 51 on all four presets: `debug`, `release`, `asan` and `tsan`, the sanitizer ones with
`ASAN_OPTIONS`/`TSAN_OPTIONS=halt_on_error=1` through `test/CMakePresets.json`. clang-format clean.
`tools/make_doc.sh` 0 warnings, 43 pages. The gate is re-run when step 46's measurement lands.

**Order:** 39, 40, 41, 43, then 42, 44, 45, and 46 with 47. 40 before 41, because a batch that is not taken out
of the member cannot be drained twice safely whatever the error handling does.

**Unchanged, and worth knowing before touching the notification contract:** an action that queues
another still runs it in the same pass. Probed on both headers — `ran=2 batches=1` either way, so
the POC does not move that line, it only moves the mechanism from "pop the next" to "the list grew
while it was being walked".
