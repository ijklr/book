# Chapter 12 — Deriving experiments that can change your mind

You have two decisions to make. A streaming search sometimes misses a match near an input boundary, and you must decide whether a proposed fix addresses the cause. Someone also proposes a larger input buffer as a performance improvement, and you must decide whether the added resource cost is justified.

The goal is evidence strong enough for those decisions. Investigation time is limited, each tool sees only part of execution, and timings vary from run to run. Begin with a question that an observation can answer. A command earns its place in the investigation when different results would lead you to different next steps.

## Make the claim smaller than the uncertainty

For the suspected bug, the contract says read boundaries must not alter search results. The observation is a missing match in one input. One hypothesis is that the implementation discards a partial match after each chunk. Keep those statements distinct: the contract defines the required result, the observation records what happened, and the hypothesis proposes a cause that an experiment can challenge.

Write down the command, input bytes, expected output and status, actual output and status, and build identity. “Sometimes wrong” is not yet a reproducer. “Correct after adding a print” is an observation about an intervention, not proof that printing repaired the design.

Compare two plausible causes: state is lost at a chunk boundary, or a line-buffering design forgets to process its last unterminated line at EOF. A large file can exhibit both. Use pattern `needle` in two small experiments. In the first, call the matcher directly with `nee` and then `dle\n`. In the second, supply `needle` in one chunk and end the input. Direct calls control chunk boundaries; two writes to a pipe do not guarantee two corresponding reads.

Before checking the implementation, commit your predictions for each hypothesis. If either hypothesis could explain every possible outcome, it is too vague to guide this experiment.

:::reveal Review the discriminating cases

Assume each proposed defect is the only fault, and that ordinary matching works:

| Hypothesis | Feed `nee`, then `dle\n` | Feed `needle`, then signal EOF |
| --- | --- | --- |
| Partial-match state is discarded between chunks | Match can be lost | Match is found |
| A buffered final line is discarded at EOF | Match is found when the newline arrives | Match is lost |

Each experiment requires one report for line 1. Together, they distinguish these hypotheses better than one large failing input. Both defects may coexist; if both cases fail, do not force the result into a one-bug explanation.

:::

## Derive a test from an invariant

The matcher's state summarizes the relevant prefix already consumed. Therefore feeding byte sequences `a` and `b` in separate calls must produce the same match events as feeding their concatenation, provided no reset intervenes, callbacks succeed, and the stream ends at the same point. The capstone reports as soon as a match is found and needs no finalization function. A line-buffering alternative may need EOF processing; include that step in both versions of the experiment.

Try a small investigation in which the fault is visible. This intentionally defective Python candidate counts chunks containing a pattern. It stands in for a design that mistakes read boundaries for report boundaries; it is not the capstone implementation. The oracle instead counts matching lines in the complete, small input. Predict which rows disagree before running:

```sh
python3 - <<'PY'
pattern = b"needle"
cases = [
    ("one chunk", [b"needle\n"]),
    ("split match", [b"nee", b"dle\n"]),
    ("split after match", [b"needle", b"\n"]),
    ("no final newline", [b"needle"]),
    ("same line twice", [b"needle", b"needle\n"]),
]
for name, chunks in cases:
    observed = sum(pattern in chunk for chunk in chunks)
    expected = sum(pattern in line
                   for line in b"".join(chunks).split(b"\n"))
    print(f"{name}: observed={observed} expected={expected}")
PY
```

The lists supply the exact chunks to the candidate, so process scheduling cannot change this experiment's boundaries. The first three rows hold the logical input fixed. The fourth changes only the final newline; the fifth introduces a second occurrence on the same line. Each change challenges a different part of the reporting rule.

:::reveal Review the controlled investigation

Every expected count is 1. The candidate reports 0 for `split match`, 2 for `same line twice`, and 1 for the other rows. The first mismatch supports the lost-prefix hypothesis. The second reveals a separate missing fact: whether the current line has already been reported. Passing the unterminated-line case gives no reason to change EOF handling for this candidate.

A repair that preserves partial matches can fix the first defect while leaving duplicate reports. Keep both cases. After restoring cross-chunk matching, suppressing every report after the first could pass this small collection but fail an input containing matches on two different lines. Add that counterexample before accepting the repair. A test collection grows from the promises a repair could accidentally violate, not simply from the number of cases already passing.

:::

The capstone's C tests feed the same bytes at every two-part split and at many repeated chunk sizes. This tests a relation between executions: changing how bytes are grouped must leave the reports unchanged. A separate Python **oracle**, a simple implementation of the expected result, splits bounded test input into newline-delimited byte strings and performs direct membership checks. The oracle can use memory freely for small test cases even though the production tool must stream. Its different structure reduces the chance of reproducing the production algorithm's defect in the test.

Neither approach proves every input. Exhaustive splits are exhaustive only for the selected byte strings. Fixed-seed random inputs add variety and reproducibility; deliberately chosen cases still need to exercise empty input, embedded zero bytes, long lines, overlapping prefixes, and finalization. A thousand random strings may never put a match across the boundary you care about.

Safety and progress also need separate evidence. Correct match output does not show that a program terminates when a pipe writer exits. A timeout detects a progress failure during a test, but does not explain whether a leaked descriptor, deadlock, or slow environment caused it. Record the state needed to distinguish those possibilities.

## Select a tool for the missing fact

Compiler diagnostics are inexpensive evidence about source-level inconsistencies. With warnings treated as errors, investigate the violated type or interface contract before adding casts. A negative `read` result must be recognized while it is still signed; converting first destroys the distinction between failure and a byte count. A format warning similarly points toward an incompatible interpretation of a variadic argument.

Memory instrumentation answers a different question: did this execution perform an invalid operation that the instrumenter detects? AddressSanitizer detects classes of memory misuse; UndefinedBehaviorSanitizer checks selected undefined operations. They do not establish correctness on unexecuted paths. [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html), [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)

Run the provided instrumented build:

```sh
make sanitize
```

Its separate `build-sanitize/` directory prevents ordinary objects from being mistaken for instrumented ones. Start with the first report, reconstruct the object's lifetime, and explain the invalid transition. Patching the final crash site without identifying that transition may merely move the failure. After repairing it, run the original reproducer and then the broader suite.

For unsynchronized thread access, use a separate ThreadSanitizer build when your platform and runtime support it:

```sh
mkdir -p build
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g -O1 \
  -fsanitize=thread -pthread examples/threads.c -o build/threads-tsan
./build/threads-tsan
```

Do not combine that command with AddressSanitizer. A clean execution is evidence about observed schedules; it does not prove all schedules safe. Runtime limitations are documented by [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html). Keep an unsupported run distinct from a run that completed without a report.

## Inspect a transition, not just a crash

If the remaining question concerns matcher state, stop where input is processed. Build an unoptimized copy for easier source-level inspection:

```sh
mkdir -p build
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g -O0 \
  capstone/sift.c capstone/matcher.c -o build/sift-debug
printf 'before\nneedle\nafter\n' > debug-input.txt
lldb -- ./build/sift-debug needle debug-input.txt
```

Inside LLDB:

```text
breakpoint set --name matcher_feed
run
frame variable
thread backtrace
next
continue
quit
```

At function entry, inspect the arguments: `length` tells you how many input bytes this call may consume, and `m` points to the persistent state. Use `frame variable *m` to inspect that state, then step through the transitions of interest. These commands are a starting point, not a script that automatically locates the defect. Linux users preferring GDB can use `break matcher_feed`, `run`, `info locals`, `print *m`, `backtrace`, `next`, and `continue`. Consult the [LLDB tutorial](https://lldb.llvm.org/use/tutorial.html) for its interface.

State your expected value before inspecting it. If the partial-match length should be three after `nee`, an observed zero narrows the fault to a transition already passed. Watching arbitrary variables until something looks surprising provides weaker guidance.

Debugger pauses and diagnostic output alter scheduling and work. If a race vanishes under debugging, that observation is compatible with a changed schedule; it is not evidence of a valid synchronization protocol. Some environments also restrict debugger attachment. Record that limitation without turning it into a claim about the program.

## Challenge failure paths independently

A program can emit plausible output and still fail before completion. For `sift`, unreadable input yields status 2 even when earlier inputs matched. Output buffered in stdio can fail during the final flush. The contract therefore includes bytes, status, and termination, not merely terminal appearance.

The supplied tests use a pipe with no reader to challenge output failure without relying on Linux-specific `/dev/full`. They also test malformed socket frames and byte-preserving file copies. These tests target observable guarantees rather than the arrangement of helper functions.

Allocation failure needs its own controlled experiment, such as an injectable allocator that fails the first, second, and later calls. A machine with abundant memory does not test that path by running the program repeatedly. Likewise, a sanitizer does not establish behavior under disk exhaustion. State which resource each intervention changes and what evidence remains missing.

## Derive a performance experiment

Our proposed change is a larger input buffer. The hypothesis is specific: when read calls commonly fill the current buffer, a larger request may reduce the number of calls needed for a fixed amount of input, reducing one component of elapsed time. It may also increase memory per active stream. If the input is a slow pipe, that mechanism may have little influence on completion time.

Estimate the opportunity before sweeping buffer sizes. Suppose a preliminary model divides a 100 ms run into 8 ms of overhead that fewer read calls might remove and 92 ms of remaining work. Even eliminating all 8 ms gives a 92 ms run, an 8% time reduction, provided the other work stays unchanged. A requested 25% reduction cannot come from that mechanism alone under this model. The arithmetic does not prove the breakdown: separating call overhead from waiting or data transfer is itself an experimental question.

Use that uncertainty to choose the next small experiment. First check whether larger requests actually reduce the call count for this input source; if they do not, the proposed explanation loses support. If they do, compare elapsed time to learn whether the removed calls matter. Also list repeated work whose result cannot change. A candidate design that rebuilds an unchanged search table for every chunk could instead reuse it, provided its inputs and lifetime justify reuse. Removing that work creates a different opportunity from accelerating its execution.

The experiment must keep the workload and success criteria comparable across variants. Otherwise a faster run might have searched fewer bytes, suppressed output, or failed early. First verify identical results and exit statuses. Then define the timing boundary: whole-process launch to exit, input transfer plus matching, or matching alone. These answer different questions.

Create one repeatable workload from the book directory:

```sh
make build/sift
python3 -c 'from pathlib import Path; Path("measure-input.txt").write_bytes(b"a" * (16 * 1024 * 1024))'
time ./build/sift needle measure-input.txt > /dev/null
```

This no-match search should exit with status 1. It is a completed search, not a failure. Whole-process elapsed time includes startup and I/O; it does not isolate the matcher. Later runs may encounter cached file data. Do not call them storage-throughput measurements without controlling the storage and cache conditions.

Use the same compiler options and identical inputs when comparing variants. Alternate or randomize the order of baseline and candidate runs so background drift is less likely to favor one consistently. Record all repetitions, not only the best. Keep correctness and timing in the same record:

| Record | Why it is needed |
| --- | --- |
| Revision, compiler, options, and actual buffer size | Identifies the implementation being compared |
| Input identity, size, and match distribution | Identifies the work requested |
| Output comparison and exit status | Establishes that the intended work completed |
| Timing boundary, run order, machine, and all elapsed times | Makes variation and environmental differences visible |

Report the observed spread as well as a typical time. No universal throughput number follows from this experiment.

For an internal elapsed interval, take the start and end readings from the same monotonic clock. Subtracting wall-clock timestamps can mix clock corrections with the time spent doing work. Clock resolution and timestamp-call overhead still matter for short intervals. [Clock semantics](https://man7.org/linux/man-pages/man2/clock_gettime.2.html)

## Check whether the measured work still exists

Consider a function with unsigned argument `n` that loops `n` times, incrementing a local counter of the same unsigned type from zero, then returns that counter. Its observable result is just `n`. A compiler may replace the loop with that result. If the counter has a narrower range, account for its wraparound instead of assuming the result still equals `n`. If the result is unused, an optimizer may remove still more work.

Before timing such a loop, predict what observable behavior requires the iterations to remain. A source-level loop count alone supplies no answer. This matters when comparing Chapter 9's mutex-protected counter with a private counter: the latter might be simplified far more aggressively, so the experiment would not isolate mutex cost.

Check generated code when a microbenchmark depends on specific work occurring. Choose representative input and consume a result meaningfully. Merely printing the final answer does not prevent closed-form simplification. Adding `volatile` can force a different set of accesses, but it changes the workload contract and does not automatically make the benchmark representative. Similarly, disabling optimization answers a question about an unoptimized build, not necessarily the production one.

These are alternative experimental designs with different boundaries. An end-to-end benchmark preserves the application context but mixes costs. A microbenchmark isolates a mechanism more closely but risks measuring an artificial workload. Use the narrower experiment to test a causal explanation, and the broader one to check whether the improvement matters to the user.

## Look for a counterexample to your favorite explanation

If a larger buffer improves a no-match file scan, “fewer system calls caused it” remains a hypothesis until supporting evidence distinguishes it from compiler differences, caching, or other changes. A useful follow-up records call counts in a separate diagnostic experiment, then compares timing without that tracing overhead. Another varies buffer size while holding input constant and predicts where diminishing returns should appear.

Likewise, equal-size files with different line and match patterns do not isolate I/O cost. Many matches add formatting and output, while the capstone can skip matching work after the first match on a line. Redirecting to `/dev/null` removes terminal rendering but not all formatting and write overhead. One changed file can alter several mechanisms at once.

Set stopping criteria before endlessly tuning. For the bug, require a passing regression, an invariant argument explaining the repair, and clean relevant checks. For optimization, define a workload-specific practical improvement and acceptable resource cost. If observed differences are smaller than the run-to-run variation, report inconclusive evidence rather than promoting the most favorable repetition. A rejected optimization can still be a successful investigation.

## Transfer the investigation

1. Construct a split-boundary failure and an unterminated-line failure for a hypothetical matcher. Give each a competing explanation and an experiment that distinguishes it.
2. Change one assumption: production input comes from a slow pipe instead of a cached file. Which conclusions from the buffer-size experiment survive, and which require new measurement?
3. Design an experiment comparing a local counting loop with a mutex-protected loop. Explain how optimization can invalidate the intended comparison, what generated-code evidence you need, and why a faster result alone cannot identify its cause.

:::reveal Review after your attempt

For exercise 1, preserve the semantic input while moving chunk boundaries to challenge state retention; separately vary final newline presence while controlling chunking to challenge finalization. An oracle should state expected behavior independently of the proposed implementation.

For exercise 2, correctness properties and storage costs survive. Throughput and latency conclusions need the new workload: arrival rate may dominate, and larger requested reads do not force a pipe read to fill that request before returning available bytes.

For exercise 3, verify which loops and synchronization calls remain in generated code. If one loop became a constant-time calculation, its speed does not measure the cost of performing the same increments without a mutex. A representative workload or a carefully bounded mechanism experiment is needed, with its limitations stated.

:::

## Write your version

Write an investigation report containing the decision, competing hypotheses, one discriminating experiment, observations, and the revised explanation. End with a stopping decision and an explicit limit on the conclusion. This habit transfers to performance engineering, production debugging, and ML experiments. The supplementary study paths identify the additional language, operational, or mathematical practice each role needs.

To investigate a model-backed service, keep two questions separate: did the system execute the intended workload reliably, and were the model's answers useful under the chosen metric? A faster run that omits difficult or timed-out examples can improve both reported latency and apparent accuracy. Preserve an expected set of sample IDs and an outcome for every sample, including failures. Compare runs with the same data and configuration, then explain the denominator of each metric. [Appendix F](#appendix-f--from-c-systems-to-ai-infrastructure) turns that accounting into an evaluation exercise with a deliberately misleading aggregate score.
