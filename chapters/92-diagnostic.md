# Appendix E — An independent systems assessment

These original study exercises ask you to apply the earlier chapters without a supplied solution. Use them to find gaps and choose your next project when preparing for demanding engineering roles at HRT, OpenAI, Anthropic, or elsewhere. They are learning assessments, not official company questions or hiring predictions.

Attempt each task before opening related chapter solutions. Start from a blank implementation except in Task 2, where you repair the supplied code. State the requirements and assumptions, compare two designs, and write an invariant and counterexample before coding. Compiler diagnostics, debuggers, and primary documentation are allowed. Log any hints, review, or supplied code; save your independent attempt before accepting assistance.

The timeboxes help you compare attempts. Save the code and notes when time expires, then finish and repair them without the clock. Tasks 1–3 and 6 begin with focused implementation attempts; Tasks 4–5 are longer labs. Tasks 7–9 extend the assessment to retries, scheduling, and evaluation; choose them when they match your target work. Spread the tasks across sessions.

For each task, record four separate rubric results as **demonstrated**, **partial**, or **not yet demonstrated**. Explain each result with an artifact: code, failing input, test, trace, or measurement. A total score would hide important differences between correct implementation, explanation, and evidence. After the attempt, change one assumption and explain which part of the argument must change. The ability to do that matters even when the first implementation was successful.

## Task 1 — Merge without hiding the cost

**First attempt: 45–60 minutes.** Write C that accepts `k` sorted arrays of `int64_t` and emits the first `m` distinct values of their union in increasing order. Inputs are immutable and individually sorted; empty arrays are valid. An empty array may have a null pointer. Do not concatenate and sort the whole input.

For arrays `[-3, 1, 1, 8]`, `[-3, 0, 8, 9]`, and `[]`, with `m = 5`, emit:

```text
-3 0 1 8 9
```

With `m = 3`, emit `-3 0 1`; with `m = 0` or no arrays, emit nothing. If fewer distinct values exist, emit all of them. Define an output callback that accepts a value or reports failure. Stop on failure, release owned memory, and return an error.

Target `O(k)` auxiliary memory and `O(k + R log(k + 1))` work, where `R` is the number of input elements examined. Duplicates mean `R` can exceed `m`. Write the invariant before the implementation.

Rubric:

- Correct ordering, deduplication, stopping, and extreme values including `INT64_MIN` and `INT64_MAX`.
- An invariant explaining why the next emitted value is safe to choose.
- A bound based on consumed elements, with no hidden full-input materialization.
- An independent simple oracle, adversarial duplicate cases, and recorded error behavior.

## Task 2 — Repair unfamiliar code from its contract

**First attempt: 60–75 minutes.** This intentionally flawed excerpt collects length-prefixed frames. Its bugs are not marked. Build your own harness and repair it without replacing the task with an unrelated implementation.

The header is four bytes, most significant byte first. Bodies may contain any bytes and have lengths 0–4096. The callback borrows a complete body only during the call and cannot fail. It must not modify or free the parser or its buffers, feed the same parser recursively, or retain the body pointer after returning. Input chunks do not alias the buffer and remain unchanged during the call. A zero-length input may use a null pointer. Initialize `struct frames` to zero and eventually free its `data` field.

Required behavior: emit complete frames exactly once, preserve incomplete input across calls, reject an oversized length as soon as its header is available, and preserve the previous buffer on allocation failure. A failed allocation must consume none of the new chunk and emit no frames, so the caller may retry that chunk. Return zero for accepted input and minus one for an error. In the repaired version, set `errno` to `ENOMEM` for allocation failure, `EOVERFLOW` for an unrepresentable buffer size, and `EPROTO` for an oversized frame. This lets the caller distinguish retryable allocation failure from rejected input. Size overflow must also leave the previous state unchanged. After malformed input, require destruction or reinitialization before another feed. At EOF, remaining buffered bytes mean truncation.

```c
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct frames {
    unsigned char *data;
    size_t used, capacity;
};
typedef void (*emit_frame)(const unsigned char *, size_t, void *);

int frames_feed(struct frames *f, const unsigned char *src, size_t n,
                emit_frame emit, void *context)
{
    if (n == 0) return 0;
    size_t needed = f->used + n;
    if (needed > f->capacity) {
        f->data = realloc(f->data, needed);
        if (f->data == NULL) return -1;
        f->capacity = needed;
    }
    memcpy(f->data + f->used, src, n);
    f->used = needed;
    while (f->used >= 4) {
        size_t length = ((size_t)f->data[0] << 24) |
                        ((size_t)f->data[1] << 16) |
                        ((size_t)f->data[2] << 8) | f->data[3];
        if (f->used < 4 + length) return 0;
        emit(f->data + 4, length, context);
        f->used -= 4 + length;
        memcpy(f->data, f->data + 4 + length, f->used);
    }
    return 0;
}
```

The byte sequence `00 00 00 03 61 62 63 00 00 00 00 00 00 00 02 00 ff` must emit bodies `61 62 63`, empty, and `00 ff`, regardless of chunk divisions. Feed `00 00 10 01` alone to test rejection before a body arrives. Use an allocation wrapper to force a failed growth request; do not try to exhaust your machine.

Rubric:

- Reproducers establish actual violations of the stated contract.
- Explanations identify the violated lifetime, range, or progress invariant.
- Repairs preserve ownership and already buffered input on allocation failure.
- Regression tests cover chunk divisions, malformed input, EOF, and sanitizer execution.

## Task 3 — Close a queue while people are waiting

**First attempt: 75–90 minutes.** Implement a bounded queue with two producers and two consumers using pthread mutexes and condition variables. Define `push`, `pop`, and `close`. Closing rejects future pushes, preserves accepted items for draining, and wakes every waiter. A closed, empty queue makes `pop` report completion.

Producers submit disjoint IDs, such as 0–999 and 1000–1999. In a normal run, consumers collectively receive each ID exactly once. In a run closed early, every successful push must be received exactly once; rejected pushes must not appear. Record acceptance as part of the test oracle. Test capacity 1 as well as a larger capacity.

In the normal run, a coordinator joins both producers, closes the queue, then joins the consumers. For early-close cases, use barriers or test hooks to establish that the intended wait state has been reached before closing; an arbitrary sleep does not establish it. A startup-failure path must close and wake the queue before joining workers that could otherwise remain blocked.

Rubric:

- Queue state transitions preserve bounds and exactly-once delivery.
- Every wait has a protected predicate; wakeups are never treated as stored permissions.
- Closure works with blocked producers and consumers, without using sleeps as synchronization.
- Every started thread is joined and resources remain valid through cleanup, including partial startup failure.

Do not require a deterministic order between unrelated producers unless your contract explicitly defines one. Stress tests support the argument; they do not replace it.

## Task 4 — Keep a slow peer from owning the service

**Initial design: 45 minutes; implementation lab: 2–4 hours.** Build a loopback TCP service using `poll` and nonblocking connected sockets. Reuse Chapter 10's four-byte length header and 4096-byte maximum. Support several frames per connection and at least two simultaneous clients. Declare a finite connection limit.

Allow at most one pending reply per connection; stop reading another request until that reply drains. Interpret EOF between frames as normal, EOF inside a frame as truncation, and a write-half-close following a complete frame as compatible with receiving its reply. Give each request an absolute monotonic deadline from its first header byte through its completed reply, rather than restarting a timeout after every byte.

First smoke-test a client sending `00 00 00 03 63 61 74`, which must receive exactly that header and body. Then run client A in a separate worker that sends many maximum-size frames without reading replies, while client B repeats the small request. Confirm through instrumentation or an injected I/O harness that A actually exercises pending output and would-block handling; one small reply may fit entirely in kernel buffers. If you constrain socket buffers, do not assume the requested size is their exact effective capacity. The test controller must remain runnable if A's sending blocks. Add fragmented headers, zero-length frames, oversized lengths, and a peer that sends one header byte and stops.

Rubric:

- Explicit states and offsets handle positive progress, interruption, would-block, EOF, and failure.
- Connection count and buffered-byte bounds remain true under slow readers.
- Other clients progress, expired requests are closed, and idle writable sockets do not cause busy spinning.
- Automated peers verify byte equality, half-close behavior, deadlines, and eventual resource cleanup.

Use bounded test timeouts as watchdogs. Do not mistake their termination of a hung server for successful server deadline handling.

## Task 5 — Explain a measured CPU or memory cost

**Lab: 90–120 minutes.** Compute the same modulo-`2^64` sum over the same runtime-generated `uint64_t` values in two layouts: a contiguous array and a randomly ordered linked traversal. Vary the working set substantially. Keep a checked checksum observable so optimization cannot discard the computation.

Before measuring, predict how access order and dependent loads might matter. Separate construction time from traversal time, and state which you measure. Inspect optimized assembly or a profile to connect the executed work with your hypothesis. Use hardware counters if available; explicitly record when they are unavailable. Elapsed time alone does not identify a particular cache-miss cause.

Rubric:

- Equivalent answers, controlled workload generation, and recorded compiler settings.
- Repeated measurements with spread, working-set sizes, and machine details.
- Evidence distinguishes observation from a proposed mechanism and acknowledges confounders.
- One tested change preserves answers and produces a reproducible result, including an honestly reported neutral or slower result.

There is no required speedup or universal latency number. Unsupported certainty receives less credit than a carefully bounded conclusion.

## Task 6 — Change an API without losing its promises

**Design and first implementation: 60–90 minutes.** Extend the capstone matcher so an output callback may accept a match, pause without accepting it, or fail. Report how many input bytes were consumed. On pause, preserve the pending match and do not scan beyond it. A later call, including a zero-byte call, must retry that event before processing additional input.

For pattern `cat` and input `cat\ncat`, make the callback pause twice before accepting the first match. Eventually the accepted line numbers must be exactly `1, 2`. Document whether the byte completing a match is counted as consumed when output pauses. Test every chunk split and interruptions of output at both matches.

Rubric:

- A precise callback/consumption contract with a valid retry procedure.
- No lost or duplicated accepted matches, and no discarded unconsumed bytes.
- Reset, fatal failure, and borrowed-buffer lifetimes remain explicit.
- Tests exercise repeated pauses, zero-byte retries, line boundaries, and caller integration.

**Optional substitute for a C++-targeted role:** implement a move-only descriptor owner with explicit transfer, release, and one-shot cleanup policy. Test movement through a growing `std::vector`, replacement of an already owned descriptor, and exception unwinding. Explain which saved references into the vector become invalid. Assess four things: unique ownership, correct nonthrowing moves, exception-safe cleanup, and explicit lifetime/error reasoning. This substitution diagnoses a C++ foundation; it does not replace a full C++ course.

## Task 7 — Accept one result when work repeats

**Optional systems lab: 90–120 minutes.** Build an in-memory coordinator for logical jobs and execution attempts. A job ID identifies one immutable payload and computation version. Resubmitting that identity refers to the existing job; reusing its ID with different payload bytes or version is a conflict. Do not treat an unchecked hash match as proof of identity.

A timeout may launch another attempt while the original still runs. Accept the first valid success from an issued attempt for that job, even if the attempt timed out, provided no result has already been accepted. Subsequent successes cannot change the result or produce another acceptance event; a client resubmission may retrieve the already stored outcome. Reject replies for unknown attempts or mismatched job identities. This contract applies within one coordinator lifetime: a restart loses the record. Use a worker without external side effects; repeated execution remains possible, and the result rule would not by itself suppress repeated external effects.

Use a deterministic event trace: launch attempt A, time out A, launch B, receive B's success, receive A's delayed success, receive B's duplicate success. The expected accepted-result count is one; both attempts may have done work. Change the arrival order, inject an identity conflict, and show the expected decisions before running the code.

Declare finite limits for job records, attempts, payload bytes, and retained results. Retain identity and outcome records for the full run; reject a new job or refuse another attempt when its reservation cannot fit. A timed-out attempt that still runs retains its execution resources. These limits make the exercise implementable without assuming infinite deduplication storage.

Rubric:

- Identity checks distinguish resubmission from payload or version conflicts.
- Attempt and logical-job states explain exactly which replies may be accepted and which resources remain reserved.
- Deterministic traces cover duplicates, reordered replies, unknown attempts, and repeated work.
- The result guarantee states its lifetime and does not claim exactly-once execution or restart recovery.

## Task 8 — Schedule within a budget while requests expire

**Optional systems lab: 2–3 hours.** Simulate a job scheduler with a fake monotonic clock; no GPU is needed. Declare a waiting-job limit, a maximum number of executing jobs, and a total reserved-memory budget. Each job declares its reservation before admission. Count memory for both waiting and executing jobs, and reject work that cannot fit without breaking a bound.

Give every admitted job an absolute deadline covering its entire wait and execution. A completion at or after the deadline is too late. Canceling or expiring a waiting job removes it; canceling or expiring an executing job makes its result ineligible but retains its execution slot and reservation until execution actually stops. Cancellation requests alone cannot reclaim resources.

Add a maximum batch size and a finite batching wait. Keep the original deadlines as the batch fills. Define the synthetic batch service cost explicitly, then compare two batching waits against the same arrival trace and service model. Include an oversized job, overload, a deadline expiring during execution, and a delayed cancellation acknowledgment. At every event, print enough state to check the bounds independently.

Report admitted and successful throughput, successful-request latency percentiles, and counts of every rejected, canceled, or expired request. Explain what the latency population excludes; dropping slow requests must not silently make the service appear better.

Rubric:

- Every event preserves declared queue, execution, and memory bounds.
- Deadlines include batching delay, and resource release follows actual execution termination.
- Repeatable traces establish overload and cancellation behavior without wall-clock sleeps.
- Measurements use one workload and expose both latency distributions and unsuccessful outcomes.

## Task 9 — Make an evaluation result auditable

**Optional research/evaluation lab: 2–3 hours.** Build a runner around a deterministic fake model first. Identify each input by a stable sample ID and dataset version. Record the run configuration, seed, model version, input-processing version, and scorer version. Preserve every sample's final outcome, including incorrect answers, timeouts, execution errors, and skipped cases. A retry is another attempt for the same sample, not an extra example.

Create a fixture with 100 requested samples: 80 correct answers, 10 incorrect answers, and 10 timeouts. Expected reports include `80/90` accuracy conditional on completion, `90/100` completion rate, and `80/100` end-to-end success. Label each denominator. Reporting only the first number does not describe all requested work. Preserve attempt-level evidence so another reader can reconstruct the counts.

Require incompatible dataset or scorer versions to remain distinguishable rather than silently pooling their results. Keep development examples separate from held-out evaluation data, and declare how missing outcomes affect comparisons. Run an optimization experiment only after checking answer agreement or a stated quality criterion. A faster runner does not by itself demonstrate a better model.

Rubric:

- Stable identities and provenance make the evaluated work reconstructible.
- Retries and all terminal outcomes reconcile with the requested sample population.
- The fixture produces correctly labeled conditional and end-to-end metrics.
- Comparisons separate data selection, model quality, and execution performance.

Tasks 7–8 extend the systems path; Task 9 is an optional bridge toward research and evaluation work. These are lab specifications, not supplied implementations. Use [Appendix F's related derivations](#appendix-f--from-c-systems-to-ai-infrastructure) after your independent attempt.

Finish with a short revision of your own learning plan. Choose the weakest demonstrated skill, identify the failed invariant or missing evidence, and schedule a fresh problem that exercises it. Repeating a memorized solution is weaker evidence than transferring the same reasoning to unfamiliar code.
