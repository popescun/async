# async.hpp — fix plan

**Status (2026-09-10):** 11 of 27 steps done. Steps 1-10 landed in one commit — the queue race and
the worker lifetime, which were the four critical findings and two of the five high ones. Step 11
landed in `cfa245d` (`async.hpp` — a mutex on `execution_poll`; two new tests).
**Tests:** 7/7 green — `ctest --test-dir test/build` (baseline was 2/5).
ThreadSanitizer and AddressSanitizer clean over all cases in one process; 50x repeat, no flakes.
**Docs:** 0 doxygen warnings; `doc/refman.pdf` is 31 pages (was 23).
**Source:** audit of 2026-09-10 (4 critical, 5 high, 5 medium, 8 hygiene), findings 1, 2, 3 and 5
reproduced under TSan/ASan. Items lettered A onwards were found while fixing, and are read from the
code unless marked otherwise.

## Progress

Done — steps 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11. Step 11 finished what step 4 left of the poll.

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

**NEXT: step 12** — item 8, `bind()` captures the execution by reference (`:53`, `:76`). Read the
step first: the dangling-capture half is **blocked on an ownership decision**, because a `weak_ptr`
capture requires `execution` to be held by `shared_ptr`. The by-value/double-copy half is separable
and can land on its own.

**Remaining: 16 steps.** Groups 2 to 8 below.

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
| **Group 2 — dangling references** |
| 11 ✅ | A | `execution_poll` is a shared mutable singleton with no lock | `:104-137` | CONFIRMED |
| 12 | 8 | `bind()` captures the execution by reference | `:53`, `:76` | read-only |
| 13 | 9 | `attach()` stores pointers, has no inverse, no cycle check | `:314-326` | read-only |
| **Group 3 — results** |
| 14 | 7 | `_result` read uninitialised, written unsynchronised | `:447`, `:328`, `:348` | read-only |
| 15 | 13 | only the last action's return value survives | `:328`, `:348`, `:447` | read-only |
| **Group 4 — notifications** |
| 16 | 11 | `on_finished` never fires in continuous mode | `:387-388` vs `:396-421` | read-only |
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
| **Group 8 — build** |
| 27 | F | C++23 raises the toolchain floor; CI may not clear it | `test/CMakeLists.txt:7` | UNVERIFIED |

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

### Step 12 · item 8 — `bind()` captures the execution by reference
`async.hpp:53`, `:76`

Both free `bind()` overloads return `[&async_exec, async_action](auto... args)`. The returned
`std::function` is typically stored on the bound object, which has no relationship to the
execution's lifetime; invoking it after the execution has died calls `add_action()` on freed memory.
This is the hazard `actuator::bind` avoids with a `weak_ptr`, and the hazard step 4 just removed
from the poll.

Two smaller problems in the same two lambdas: `auto... args` takes by value and `std::bind` copies
again, so a `const std::string&` parameter is copied twice and a move-only argument will not
compile; and the lambda returns a default-constructed result, so an async call to an `int`-returning
method silently yields 0.

> No safe capture exists while `execution` is a bare object — this needs the execution to be held by
> `shared_ptr` and captured as `weak_ptr`, which is an API change. **Decide the ownership model
> before writing this step;** the thread-pool work may settle it. The by-value/double-copy part is
> separable and can land first as `auto&&... args` with perfect forwarding.

### Step 13 · item 9 — `attach()` stores pointers, has no inverse, no cycle check
`async.hpp:314-326`

`attach()` stores `&other.action_execute` and `&other.action_stop`. There is no `detach()`, no
notification when the attached object dies, and no cycle check: `a.attach(b); b.attach(a);` compiles
and recurses until the stack is gone.

> `detach()`, plus deregistration from `~execution()` — the same fix as step 4, applied to the other
> place the header holds pointers into another object. Reject an attach that would close a cycle.

---

## Group 3 — results

### Step 14 · item 7 — `_result` read uninitialised, written unsynchronised
`async.hpp:447` (declaration), `:328` (`result()`), `:348` (write)

`typename resultT::type _result;` has no initialiser and no constructor touches it, so `result()`
before any action has run is undefined behaviour for the `int` substitute and any scalar result
type. Separately the worker writes it while any thread may read it — the queue is now guarded, but
this is not.

> `= {}` on the declaration closes the read. The write needs the same treatment as the queue: either
> take `action_mutex` around it, or make the whole results question moot with step 15.

### Step 15 · item 13 — only the last action's return value survives
`async.hpp:328`, `:348`, `:447`

`_result` is one value that each action overwrites. Queue three actions and two return values are
lost. `actuator` solves this next door with a `results` vector.

> Follow `actuator`: a `results` vector, cleared per run. Supersedes half of step 14, so decide the
> two together.

---

## Group 4 — notifications

### Step 16 · item 11 — `on_finished` never fires in continuous mode
`async.hpp:387-388` (fires in `execute()`) vs `:396-421` (`loop()` does not call it)

A caller who uses `start()`/`stop()` never gets the callback. Where it does fire, it fires *before*
`running = false`, so a callback that checks `is_running()` sees itself as running.

> Call it from `loop()` too, and after `running = false` — but note the ordering constraint added by
> step 3: nothing may touch the object after `running = false`, because the destructor may free it
> the moment it reads false. The callback has to fire before that store and be told the truth some
> other way, or the destructor needs a different handshake. **This step and step 20 both press on
> that convention; settle it once.**

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

## Group 8 — build

### Step 27 · item F — C++23 raises the toolchain floor; CI may not clear it
`test/CMakeLists.txt:7` · UNVERIFIED — local builds only

`std::println` moved the project from C++20 to C++23. Locally that is fine (Apple clang 21). The CI
matrix is `macos-latest`, `windows-latest`, `ubuntu-latest` on default compilers, and `ubuntu-latest`
ships GCC 13 by default while libstdc++ got `<print>` in GCC 14. That job is expected to fail.

Not verified from here — it needs a CI run.

> Either pin a newer compiler in the workflow, or guard the include:
> `#if __has_include(<print>)` with the two call sites behind `__cpp_lib_print`. The guard is also
> the natural seam for the logger that is planned, which would remove the dependency entirely.
