# Chapter 7 — Receiving a request to stop

Imagine a small service that owns an output file. A controller must be able to ask it to stop, let it finish its cleanup, and collect its result. The request may arrive while the service is between ordinary instructions. We want a protocol that preserves the request until the service can act on it, without forcing cleanup to run inside an arbitrary interrupted operation.

For the first experiment, leave out the service's work and output file. Keep only startup, a stop request, and normal termination. This isolates the synchronization problem. We can add resources after understanding when the request becomes safe to send and how the receiver remembers it.

The requirement is an orderly response to a supported request after announced readiness. It is not a promise to recover from every possible process termination. Establishing that boundary is part of the design, before choosing a signal number or handler.

## Derive the protocol before the mechanism

The service passes through these logical states:

```text
initializing --> ready --> stop accepted --> cleaning up --> finished
                   ^
          controller may send now
```

The controller needs evidence that the receiver is prepared to handle the request. The receiver must remember a request that arrives just before it starts waiting. Stopping is **idempotent**: repeating the request has the same intended effect as making it once. A boolean “stop requested” therefore carries enough information; ten requests need not produce ten separate cleanups.

This gives a safety property: cleanup executes once through ordinary controlled flow, with resources still valid. It also gives a conditional progress property: after a supported request and readiness, the receiver eventually reaches cleanup if it gets execution time and its remaining operations complete. Whether cleanup itself can block indefinitely is a separate design question.

Predict the dangerous intervals. What happens if the controller sends before readiness? What if it sends after readiness but just before the receiver begins waiting? A correct protocol must either handle each interval or exclude it explicitly through a justified precondition. “The controller probably waits long enough” supplies neither.

## Distinguish the operating-system rules

POSIX signals provide notifications with three separate controls:

| Concept | Question it answers |
| --- | --- |
| **Disposition** | What happens on delivery: the default action, ignoring, or a handler? |
| **Mask** | Which signals does this thread block from ordinary delivery? |
| **Pending state** | Which generated signals are still waiting to be delivered or accepted? |

For a blocked signal whose disposition is not ignore, pending state remembers the notification. Ignoring a signal does not provide that guarantee. These controls explain how we can defer a stop request without discarding it. [POSIX signal generation and delivery](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html)

Ordinary signals can **coalesce**: several occurrences of one signal need not become several separately countable events. That fits our idempotent stop request but would fail a requirement to count every submitted job. A signal number identifies a notification type; it does not identify one unique request.

Our chosen notifications are `SIGINT` and `SIGTERM`. Their symbolic names express the source-level interface; raw numeric values should not become a portable protocol. `SIGKILL` and `SIGSTOP` cannot be caught, blocked, or ignored. Consequently, the service's cleanup guarantee must be conditional on the supported termination path. [Signal dispositions and restrictions](https://man7.org/linux/man-pages/man7/signal.7.html)

Separate those rules from the model around them:

| Kind of statement | Statement used here |
| --- | --- |
| External contract | Blocked signals in the waited-for set can be accepted through `sigwait`; ordinary signals need not count every occurrence. |
| Model assumption | One thread controls this policy, and the controller waits for readiness before sending. |
| Design choice | Either supported request triggers one orderly exit with success status. |
| Observation | A particular PID was printed and one test completed after a particular delay. |

The observed delay cannot establish a deadline guarantee. If shutdown must finish within a fixed interval, that requirement reaches every blocking operation in the cleanup path.

Work backward from a proposed requirement: “exit within five seconds, after uploading final metrics and saving all pending work.” Suppose the upload must await a remote reply, with no bound on when that reply arrives. Those requirements cannot jointly supply the deadline. Faster signal handling cannot bound an unbounded dependency after acceptance.

If the metrics upload is optional, specify when it may be abandoned. If saving pending work is mandatory, its duration needs a bound or the deadline needs revision. Test the dependency by making the metrics receiver withhold its reply: that can expose an unbounded cleanup path, even when signal acceptance works correctly.

## Compare two viable ways to receive the event

One design installs a minimal handler that records a flag. Ordinary code inspects the flag and performs cleanup. Waiting must coordinate the flag check with signal delivery; otherwise it can miss the transition that should wake it.

A second design blocks the selected signals and accepts one synchronously with `sigwait()`. “Synchronously” means the program receives the signal as the result of a call at a chosen point in its control flow; no handler runs for that acceptance. For this program, whose only job is to await shutdown, the operating system's pending state replaces a user-maintained request flag.

| Design | Main benefit | Obligation it introduces |
| --- | --- | --- |
| Minimal handler plus a coordinated wait | Can record an event while ordinary code runs between defined checkpoints. | Keep the handler restricted, and make testing and sleeping agree on the signal mask. |
| Blocked set plus `sigwait` | Turns acceptance into an ordinary function result with ordinary cleanup afterward. | Keep waited-for signals blocked and allocate an execution path that can wait for them. |

A handler-based design is viable; its correctness depends on more than choosing a special flag type. A dedicated waiting thread is another useful form of the second design when work must continue concurrently. Chapter 9 provides the ownership and synchronization concepts needed for that extension.

## Break the intuitive handler loop

Suppose a handler sets a flag and the main program repeatedly checks the flag, calling `pause()` when no request appears. The counterexample needs only three events:

```text
main observes stop_requested == 0
handler runs and sets stop_requested = 1
main calls pause and waits for another signal
```

The notification was delivered, the flag holds the desired value, and the program can still wait forever. This is a lost-wakeup problem, not a compiler optimization problem. Making the flag volatile or adding a delay does not make the check and wait atomic.

A handler-based repair blocks the signal while inspecting state, then uses `sigsuspend()` to change the mask and wait as a coordinated operation. The surrounding loop rechecks state when control returns. The temporary mask change closes the gap in which the notification could be consumed before the intended wait. [The `sigsuspend` interface](https://man7.org/linux/man-pages/man2/sigsuspend.2.html)

There is also a safety failure in “just clean up inside the handler.” A handler might interrupt `printf()` while the library is updating internal stream state, then call `printf()` again on that inconsistent state. It might attempt to acquire a lock already held by the interrupted execution. POSIX specifies a restricted async-signal-safe set precisely because ordinary operations cannot all tolerate that interruption context. [Async-signal safety](https://man7.org/linux/man-pages/man7/signal-safety.7.html)

A minimal illustrative handler can assign a `volatile sig_atomic_t` flag. That does not make a larger data structure safe and does not replace thread synchronization. If a handler calls an allowed operation that changes `errno`, it must preserve the interrupted code's value. The simplest safe handler remains part of a larger synchronization design.

## Choose the synchronous design and prove its ordering

Our receiver first blocks `SIGINT` and `SIGTERM`, establishes dispositions, and then prints and flushes its readiness line. Finally it calls `sigwait()`. Consider the relevant arrival intervals:

| Request arrives | Consequence under this protocol |
| --- | --- |
| Before readiness | Outside the cooperating controller's contract; startup protection is not yet established at every earlier instant. |
| After readiness, before the wait | It can remain pending for acceptance. |
| While waiting | Acceptance lets the call return with the selected signal. |
| During cleanup | Selected signals remain blocked; this one-shot design does not start cleanup again. |

The key invariant is that the selected signals remain blocked throughout the announced interval. `sigwait()` accepts a selected pending signal while it is blocked; there is no need to unblock it first. Readiness reports that the receiver has established this policy. A sleep in the controller could only estimate when that happened.

`sigwait()` returns zero on success and places the signal number in its output parameter. Its error result is an error number directly, rather than a `-1` result whose explanation must be read from `errno`. Handling it like `read()` would mix two contracts. [POSIX `sigwait` specification](https://man7.org/linux/man-pages/man3/sigwait.3p.html)

### Let the request arrive at the awkward moment

Assume one `SIGTERM` request and successful setup. Deliberately delay the receiver after its controller has read the readiness line but before the receiver calls `sigwait()`. The delay need not have any particular duration; the following order is enough:

| Step | Receiver or controller action | Receiver's signal state |
| --- | --- | --- |
| 1 | Receiver blocks the selected set and establishes default dispositions. | `SIGTERM` blocked; none pending. |
| 2 | Receiver flushes readiness; controller reads it. | Policy established; receiver need not be waiting yet. |
| 3 | Controller sends `SIGTERM`. | `SIGTERM` blocked and pending. |
| 4 | Receiver eventually calls `sigwait()`. | The call accepts the pending signal. |
| 5 | Receiver enters cleanup after a successful return. | `SIGTERM` still blocked; the accepted occurrence is no longer pending. |

The controller does not need evidence that step 4 has begun. Evidence of step 2 is enough because pending state bridges the interval. Compare the broken flag-and-`pause()` loop: there, delivery consumes the event before the wait begins, and `pause()` has no reason to inspect the flag. Here, acceptance and observation of the request are one interface operation.

Blocking also protects the cleanup path from a second supported request. If another `SIGTERM` arrives during cleanup, it can become pending again, but it does not start another cleanup or take its default action while blocked. This program keeps the mask in place through normal termination. In a reusable component, restoring a caller's mask would require a separate policy for pending requests.

**Predict:** just before step 4, the receiver decides to “let the signal through” by unblocking `SIGTERM`. What happens to the pending request under its default disposition? Does `sigwait()` require that unblocking step?

:::reveal Review the arrival trace

Unblocking allows ordinary delivery of the pending `SIGTERM`, whose default action terminates the process. The receiver can lose its intended cleanup path before reaching the wait. `sigwait()` instead requires the selected signals to be blocked and accepts one directly. Blocking is what makes the waiting protocol work; it is not an obstacle to remove before acceptance.

:::

Before opening the reference implementation, write the three operations “block, announce, accept” and justify each ordering edge. Include the flush in your explanation: output buffered only in the receiver's memory cannot yet serve as evidence for another process.

:::reveal Reference implementation

{{include:examples/signal_wait.c}}

:::

The program restores default dispositions while the signals are blocked. A background shell command can inherit an ignored `SIGINT`; resetting that disposition avoids depending on inherited ignore behavior. This does not deliver a blocked signal immediately. Disposition and mask are separate controls. [POSIX signal actions](https://man7.org/linux/man-pages/man3/sigaction.3p.html)

After `sigwait()` returns, `printf()` runs in ordinary control flow, where the handler restrictions do not apply. The example has no application resource to release, so its cleanup consists of finishing the normal path and checking its output stream. A real service needs an explicit order: stop accepting new work, finish or abandon existing work according to its contract, then release the resources that work used. Reporting “cleanup complete” does not itself prove the process has exited; a supervising parent still collects completion.

## Test a synchronization claim, not a timing guess

In the first terminal:

```sh
make build/signal_wait
build/signal_wait
```

After observing `ready pid=12345`, use the actual printed PID in a second terminal:

```sh
kill -TERM 12345
```

Predict the result before sending. Expect `received SIGTERM; cleanup complete` and a successful exit. Run again and press Control-C after readiness; expect the `SIGINT` version. A supported request was handled successfully, so our choice of exit success is consistent with the contract. It is not a requirement that every shutdown convention use that status.

The command name `kill` describes the sending utility, not a guarantee of immediate recipient termination. Here the request becomes a normal returned value. That distinction is observable: the receiver prints its message rather than disappearing through the signal's default action.

For automated tests, read the flushed readiness record before signaling. A test that sleeps briefly and then sends may usually pass while retaining the startup race. Conversely, readiness output can itself block if its destination stops consuming. This protocol has a precise ordering guarantee, but it does not promise bounded time independently of the output channel.

## Change the assumptions carefully

The example is single-threaded and uses `sigprocmask()`. In a threaded application, use `pthread_sigmask()` and establish the policy before creating workers, which inherit the mask. A process-directed signal can otherwise reach an eligible thread that does not block it. Dispositions remain process-wide even though masks are per-thread. [POSIX mask rules](https://man7.org/linux/man-pages/man3/sigprocmask.3p.html)

The waiting program also does no simultaneous blocking input. Adding an indefinite `read()` before `sigwait()` could prevent the chosen acceptance path from running. A dedicated waiting thread, or an event-loop wakeup design, addresses that new progress requirement. Adding unsafe handler work does not remove it. Chapter 11 develops the event-loop side.

Finally, inherited masks can survive execution. A launcher that changes its own policy must decide what the new program should inherit. A program that appears unresponsive to an interrupt may be following an inherited blocked mask rather than an installed ignore disposition.

## Exercises

1. **Repair the counterexample.** Describe a handler-and-wait solution in terms of when the selected signal is blocked, when the flag is tested, and how the wait changes the mask. Explain both the safety and progress obligations.
2. **Test a different notification meaning.** Add a conceptual `SIGUSR1` status request. Would ten rapidly sent requests guarantee ten separate replies? Compare a signal with a framed control pipe if every request must receive a distinct reply.
3. **Transfer: keep work running.** Change the program so worker threads continue processing while shutdown is awaited. State who owns the mask policy, how the stop decision reaches workers, and when shared resources may be released.

**Write your version.** Explain why a request can be safely remembered yet still fail to produce progress. Use the lost-wakeup trace and the readiness protocol as two different examples of ordering that must be designed.

## Selected answers

:::reveal Review after your attempt

1. Block delivery while testing the flag, then temporarily unblock and wait through `sigsuspend`; recheck after returning. The handler still needs restricted operations, and normal code must eventually reach a checkpoint. A correct flag access alone proves neither coordinated waiting nor eventual cleanup.
2. Ordinary signals may coalesce, so a one-request/one-reply interpretation is unsupported. A control pipe with explicit framing can represent individual requests, at the cost of queue capacity, parsing, and response ownership. Signals remain suitable when repeated status or stop notifications can be merged.
3. Block the set before creating workers and use an appropriate waiting thread. Communicate the accepted decision through the synchronized state workers already follow; their masks do not make ordinary shared memory race-free. Join workers before releasing storage they borrow. A wakeup mechanism is additionally needed if they could otherwise wait indefinitely without checking the stop condition.

:::
