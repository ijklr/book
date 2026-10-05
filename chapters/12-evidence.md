# Chapter 12 — Deriving experiments that can change your mind

You have two decisions to make. A streaming search sometimes misses a match near an input boundary, and you must decide whether a proposed fix addresses the cause. Someone also proposes a larger input buffer as a performance improvement, and you must decide whether the added resource cost is justified.

The goal is evidence strong enough for those decisions. The constraints are limited investigation time, tools that observe only part of execution, and a machine whose timing varies. Running more commands is useful only when their results can distinguish plausible explanations.

## Make the claim smaller than the uncertainty

For the suspected bug, the application contract says read boundaries must not alter search results. A model of a faulty implementation says it discards a partial match after each input chunk. An observation might be a missing match in one supplied file. The hypothesis is that discarding state caused that missing match. These are different statements: the contract tells us what should happen, the model suggests a mechanism, and the observation needs an explanation.

Write down the command, input bytes, expected output and status, actual output and status, and build identity. “Sometimes wrong” is not yet a reproducer. “Correct after adding a print” is an observation about an intervention, not proof that printing repaired the design.

For a candidate implementation, compare two plausible causes: state is lost at a chunk boundary, or a line-buffering design forgets to process its last unterminated line at EOF. A large file can exhibit both. Feed `nee` then `dle\n`, then separately supply `needle` without a newline and end the input. Different failures point toward different transitions.

Before checking the implementation, commit your predictions for each hypothesis. If either hypothesis could explain every possible outcome, it is too vague to guide this experiment.

:::reveal Review the discriminating cases

A boundary-state defect can fail the split `nee`/`dle\n` case even though finalization is unnecessary to recognize the completed newline-terminated line. A finalization defect can fail the unterminated-line case even when the pattern arrives in one chunk. Both defects may coexist, so a result narrows the investigation without establishing that only one fault exists.

:::

## Derive a test from an invariant

The matcher's state summarizes the relevant prefix already consumed. Therefore feeding chunks `a` and `b` must agree with feeding their concatenation, provided no reset intervenes and the stream ends at the same point. The capstone reports when a match is found and needs no finalization function; a line-buffering alternative may need EOF processing. The property applies to either design.

The capstone's C tests feed the same bytes at every two-part split and at many repeated chunk sizes. A separate Python oracle splits bounded test input into newline-delimited byte strings and performs direct membership checks. The oracle can use memory freely for small test cases even though the production tool must stream. Different structures make identical defects less likely than copying the production algorithm into its tests.

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
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g -O1 \
  -fsanitize=thread -pthread examples/threads.c -o build/threads-tsan
./build/threads-tsan
```

Do not combine that command with AddressSanitizer. A clean execution is evidence about observed schedules; it does not prove all schedules safe. Runtime limitations are documented by [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html). Keep an unsupported run distinct from a run that completed without a report.

## Inspect a transition, not just a crash

If the remaining question concerns matcher state, stop where input is processed. Build an unoptimized copy for easier source-level inspection:

```sh
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

These commands inspect state and the call path. Linux users preferring GDB can use `break matcher_feed`, `run`, `info locals`, `backtrace`, `next`, and `continue`. Consult the [LLDB tutorial](https://lldb.llvm.org/use/tutorial.html) for its interface.

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

Use the same compiler options and identical inputs when comparing variants. Alternate or randomize the order of baseline and candidate runs so background drift is less likely to favor one consistently. Record all repetitions, not only the best. Record the machine, revision, compiler, options, input, timing boundary, and observed spread. No universal throughput number follows from this experiment.

For an internal elapsed interval, use compatible readings of a monotonic clock. Wall-clock corrections are not work performed by your algorithm. Clock resolution and timestamp-call overhead still matter for short intervals. [Clock semantics](https://man7.org/linux/man-pages/man2/clock_gettime.2.html)

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
