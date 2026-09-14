# async.hpp — fix plan

**Status (2026-09-14):** 17 of 27 steps done, the last one uncommitted. Steps 1-10 landed in one
commit — the queue race and the worker lifetime, which were the four critical findings and two of the five high ones. Step 11
landed in `cfa245d` (`async.hpp` — a mutex on `execution_poll`; two new tests). Step 12 landed in
`c4843cd` (`bind()` now holds the execution weakly; two new tests).
Step 13 landed in `2e3a8d0` (`attach()` takes a `shared_ptr`, gains `detach()` and refuses cycles;
seven new tests), closing group 2. Step 27 landed in `f251acf`, closing group 8. Steps 14 and 15 landed in
`f37ee83` (a `results` vector filled by `run()`; five new tests), closing group 3. **Step 16 is
applied and green in the working tree, not yet committed.**
**Tests:** 26/26 green — `ctest --test-dir test/build` (baseline was 2/5). AddressSanitizer 26/26;
ThreadSanitizer 25/26, the one failure being `async_smoke_test`'s own `std::cout` race, which is
step 19 and predates all of this. 30x repeat of the whole suite, no flakes.
**Docs:** 0 doxygen warnings; `doc/refman.pdf` is 37 pages (was 31), rebuilt with
`tools/make_doc.sh`.
**Source:** audit of 2026-09-10 (4 critical, 5 high, 5 medium, 8 hygiene), findings 1, 2, 3 and 5
reproduced under TSan/ASan. Items lettered A onwards were found while fixing, and are read from the
code unless marked otherwise.

## Progress

Done — steps 1 to 16, and step 27 out of order. Step 11 finished what step 4 left of the poll,
step 12 closed the dangling `bind()` capture, and step 13 closed `attach()`. **Group 2 is
complete**: every place the header held a raw pointer into another object now learns when that
object dies.
**Group 8 is complete** too, forced early by a red CI run, and **group 3** with steps 14 and 15.

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
| *(uncommitted)* | 16 — `on_finished` fires per drained batch, and is told the truth |

**NEXT: step 17** — item 12, attached executions report idle while running (`:314-326`, `:379-380`).
The last of group 4, and the one step 13 was expected to reshape.

**Carried forward from step 12, not done there:** the by-value/double-copy half of item 8. The
`bind()` lambdas still take `auto... args` by value and `std::bind` copies again, so a move-only
argument will not compile, and they return a default-constructed result, so an async call to an
`int`-returning method yields 0. It touches the same lines as steps 21 and 25 — land the three
together, or accept three passes over the same two lambdas.

**Remaining: 10 steps.** Groups 4 to 7 below; groups 1, 2, 3 and 8 are closed.

**Out of order:** step 27 was taken early, ahead of steps 14-26, because a CI run failed on it —
the ubuntu job could not compile `<print>` at all, so nothing else could be verified there.

**Not planned:** the `attach()` design itself. Items 9, 12 and the bounded wait in step 7 all trace
back to attached executions having their own list and no way to notify the attacher, but redesigning
that is a feature decision, not a fix. It is called out where it bites and left alone otherwise.

27 atomic steps. **One step = one commit = one concern**, and the suite must be green after every
one. The audit's numbering is preserved so items stay traceable; findings added while fixing are
lettered.

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
| **Group 4 — notifications** |
| 16 ✅ | 11 | `on_finished` never fires in continuous mode | `:387-388` vs `:396-421` | CONFIRMED (0/3 fired) |
| 17 | 12 | attached executions report idle while running | `:314-326`, `:379-380` | read-only |
| **Group 5 — output** |
| 18 | 14 | the header writes to `std::cout` unsynchronised | `:391`, `:418` | CONFIRMED (TSan) |
| 19 | E | the smoke test races on `std::cout` between two workers | `async_smoke_test.cpp:15,24` | CONFIRMED (TSan) |
| **Group 6 — API contract** |
| 20 | B | `~execution()` suppressed the implicit moves | `:203` | read-only |
| 21 | C | a refused action is reported but not returned | `:295-312` | read-only |
| **Group 7 — hygiene** |
| 22 | hyg | six more headers used but not included | `:8-14` | read-only |
| 23 | hyg | `actuator` forward-declared after its own `#include` | `:16-23` | read-only |
| 24 | hyg | `other_this = this` is pointless indirection | `:176`, `:449` | read-only |
| 25 | hyg | `add_action` copies the action and every argument twice | `:295-307` | read-only |
| 26 | hyg | `result \|= ret` on a bool | `:135` | read-only |
| **Group 8 — build (closed)** |
| 27 ✅ | F | C++23 raises the toolchain floor; CI may not clear it | `test/CMakeLists.txt:7` | CONFIRMED (CI) |

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
> args` takes by value and `std::bind` copies again, so a move-only argument will not compile; and
> the lambda returns a default-constructed result, so an async call to an `int`-returning method
> yields 0. Lands as `auto&&... args` with perfect forwarding.

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

## Group 4 — notifications

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

### Step 17 · item 12 — attached executions report idle while running
`async.hpp:314-326`, `:379-380`

The attacher's worker calls `execute_actions()` on attached objects, but only `run()`/`start()` set
`running`, and neither is called on an attached execution. The poll reports them idle while they are
executing, and their `on_finished` never fires either.

> An attached execution should carry the attacher's running state, or the poll should report through
> the attacher. Depends on how step 13 reshapes `attach()`.

---

## Group 5 — output

### Step 18 · item 14 — the header writes to `std::cout` unsynchronised
`async.hpp:391` (`"finishing thread"`), `:418` (`"thread finished"`)

Two debug prints from worker threads, in a header-only library, on a stream shared with the
application. `std::println` was introduced for the warnings in steps 2 and 21 and locks the stream;
these two predate it and do not.

> Route both through `std::println`, or delete them. They are debug leftovers, not diagnostics, and
> a library should not print at all on the success path. Consider taking them out entirely and
> letting the logger you have planned own this.

### Step 19 · item E — the smoke test races on `std::cout` between two workers
`test/async_smoke_test.cpp:15`, `:24` · CONFIRMED under TSan, and present before `60f7970`

`A::f_with_arg` and `A::f_with_arg_and_return` print from two different workers concurrently. This
is the single remaining TSan warning in the repo, and it is the test's own code, not the header's.
Visible in the output as interleaved lines:
`A::f_with_arg 10 thread A::f_with_arg_and_return 0x...`

> `std::println` per line, or a mutex in the test. Worth doing so that "TSan is clean" becomes true
> of the whole repo and a future regression is not lost in a known warning.

---

## Group 6 — API contract

### Step 20 · item B — `~execution()` suppressed the implicit moves
`async.hpp:203`

Step 3 gave `execution` a user-declared destructor, which suppresses the implicit move constructor
and move assignment. The class was already effectively non-copyable through its members, so nothing
in tree breaks, but the class now silently falls back to copy where a move was expected, and the
error a user gets is a template wall rather than a statement.

This is the same finding `actuator` closed as its item B, and the audit's hygiene note at the old
`:164` asked for it before the destructor existed.

> Rule of five: `= delete` the copy operations explicitly and decide whether moving an `execution`
> is meaningful at all. It probably is not, while `other_this` and the poll hold pointers to `this`
> — see step 24.

### Step 21 · item C — a refused action is reported but not returned
`async.hpp:295-312`

`add_action()` on a stopped execution prints a warning and returns. The caller has no programmatic
way to learn the action was dropped — and the caller is usually the lambda from `bind()`, which
returns a default-constructed result either way.

The original audit complaint about stranded actions was that "the caller is never told". A warning
on stderr is better than silence, and less than telling.

> `bool add_action(...)`, threaded back through the `bind()` lambdas. Touches the same two lambdas
> as step 12, so land them together or accept two passes over the same lines.

---

## Group 7 — hygiene

### Step 22 · hygiene — six more headers used but not included
`async.hpp:8-14`

Step 10 added four. Still used but not included: `<list>`, `<functional>`, `<memory>`,
`<iostream>`, `<chrono>`, `<type_traits>`. The header compiles only because
`<actuator/actuator.hpp>` pulls them in first.

> Include what you use. Cheap, and it stops a change in `actuator` from breaking this header.

### Step 23 · hygiene — `actuator` forward-declared after its own `#include`
`async.hpp:16-23`

The forward declaration of `untangle::actuator` sits below `#include <actuator/actuator.hpp>`, so it
redeclares an already-complete type. The forward declaration of `execution` in the same block is
genuine and stays.

> Delete the `actuator` forward declaration.

### Step 24 · hygiene — `other_this = this` is pointless indirection
`async.hpp:176`, `:449`

The constructor stores `this` in a member and binds through it. It buys nothing over `this`, and it
is a stale-pointer trap the moment the class gains a copy or move — see step 20.

> Bind through `this` and delete the member.

### Step 25 · hygiene — `add_action` copies the action and every argument twice
`async.hpp:295-307`

`add_action(actionT action, Args... args)` takes everything by value, then `std::bind` copies again.

> Perfect forwarding. Same lines as the by-value half of step 12; do them together.

### Step 26 · hygiene — `result |= ret` on a bool
`async.hpp:135`

Bitwise-or on a bool in `execution_poll::is_running()`, where logical-or is meant.

> `result = result || ret;`

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
