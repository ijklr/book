# Chapter 6 — Pipes and the ownership of an ending

Suppose one existing program produces bytes and another summarizes them. You want the summary to begin while production continues, and the amount of intermediate data might exceed your available memory. You also need the final result of both programs. The parent should supervise the connection without copying every byte itself.

These constraints suggest a bounded communication channel and concurrent execution. They do not yet specify a pipe, a buffer size, or an exit-status convention. Begin by separating the data path from the supervision path: bytes flow between commands, while completion information returns to their parent.

Our interface will be:

```sh
build/pipeline printf 'hello\n' -- wc -c
```

Before running it, predict three things: how many bytes enter the consumer, what event lets the consumer stop reading, and whether a successful consumer proves that its producer succeeded. The first question is arithmetic. The other two determine most of the design.

## Compare ways to connect the programs

A temporary file is a viable intermediate representation. Run the producer, check its result, then give the completed file to the consumer. The file can be inspected or replayed, and the commands need not run together. This costs storage proportional to the intermediate output and delays consumption until the selected completion boundary. Creation, permissions, removal, and storage exhaustion become part of the design.

A pipe instead permits both programs to run concurrently with a bounded kernel buffer. A slow consumer constrains its producer through backpressure. The intermediate bytes are not retained for replay. A hang can arise from relationships among processes and open references, even when every individual call behaves correctly.

| Requirement | Temporary file | Direct pipe |
| --- | --- | --- |
| Consumer starts before producer finishes | Requires an additional concurrent-file protocol. | Fits ordinary streaming use. |
| Inspect or replay the complete intermediate result | Natural after a successful producer. | Requires additional storage or duplication. |
| Bound intermediate buffering independently of total output | Needs a different storage policy or a size limit. | Finite pipe capacity provides backpressure. |
| Explain completion | Producer status and selected file-completion policy. | Both child statuses plus precise stream-closure ownership. |

We choose the pipe because early consumption and bounded buffering matter for this experiment. If replay became the primary requirement, the file design would deserve reconsideration. Neither choice eliminates the need to define what partial output means after producer failure.

## Derive the state that matters

For reasoning, model the channel with three quantities: buffered bytes `B`, open write references `W`, and open read references `R`. The kernel maintains the actual channel state; our program need not duplicate these counters. The parent needs only its owned endpoints, the child PIDs it must collect, and their results.

For a positive-length blocking read, the important cases are: available bytes can be consumed; an empty pipe with possible writers requires waiting; an empty pipe with no writers produces EOF. Thus “the producer finished” is insufficient. A different process may still hold a write reference. [POSIX read rules for pipes](https://man7.org/linux/man-pages/man3/read.3p.html)

The intended final topology is:

```text
producer stdout ---> [ bounded byte buffer ] ---> consumer stdin

parent:          owns no channel endpoints; owns collection obligations
producer:        owns its output reference; no unused channel references
consumer:        owns its input reference; no unused channel references
```

This gives two invariants. Bytes enter through the intended producer output and leave through the intended consumer input. Every additional reference has a defined removal point before the parent waits for completion. The first concerns correct data flow; the second is also essential for progress.

Consider a proposed version with two pipes: the parent reads the producer's bytes into a buffer and writes them to the consumer. Before asking how large that buffer should be, ask what required result the relay creates. If the parent neither transforms nor inspects the bytes, the direct connection already satisfies our contract. Deleting the relay removes its buffer, its partially forwarded range, and its transfer-failure paths. We can eliminate work before trying to make that work faster.

Now add a genuine requirement: the parent must retain a copy of every intermediate byte for later inspection. The direct connection no longer suffices by itself. Forwarding with storage, or another duplication design, may earn its added state. Ask what “later inspection” means before implementing it: inspecting child statuses requires supervision, but inspecting their bytes requires a data-path change. Similar wording can hide very different obligations.

The external interface supplies ordering and EOF rules. Our model assumes ordinary blocking descriptors, commands that use their streams as intended, and no requirement to retain the intermediate output. Choosing two commands, a `--` separator, and a particular result policy are application decisions. Actual scheduling and pipe capacity are observations, not assumptions the algorithm should depend on.

## Find the deadlock before writing the happy path

Consider a sequential plan: create the producer, wait for it, then create the consumer. A short output may make this appear successful. A larger output produces a counterexample:

```text
producer fills the pipe
producer waits for a reader to remove bytes
parent waits for producer termination
consumer has not been created
```

The wait graph is closed: the action that could release the writer depends on that writer finishing. Increasing capacity merely moves the failing input size. Starting both commands before waiting changes the dependency graph and removes this cycle. Pipe capacity is finite and not a portable fixed constant. [Pipe capacity and blocking](https://man7.org/linux/man-pages/man7/pipe.7.html)

Now consider a different plan: both commands run, but the parent retains its write endpoint “for cleanup at the end.” After the producer exits and the consumer drains buffered data:

```text
consumer waits for EOF: parent's write reference still exists
parent waits for consumer: plans to close its reference afterward
```

No byte is corrupted, and no invalid pointer is involved. This is a liveness failure caused by an ownership decision. Closing the parent's reference before waiting removes the cycle. Sleeping, changing priority, or closing only the producer's descriptor cannot repair the missing ownership transition.

## Map the topology onto descriptors

`pipe(ends)` creates the channel and returns its read descriptor in `ends[0]` and write descriptor in `ends[1]`. Forking copies references into each child. A possible scheduling point after both forks, before child cleanup, has both original endpoints in all three processes. Other schedules perform some cleanup earlier; correctness must survive either order. [POSIX pipe creation](https://man7.org/linux/man-pages/man3/pipe.3p.html)

The producer needs its standard output to refer to the write end. `dup2(ends[1], STDOUT_FILENO)` makes that relationship; the consumer similarly redirects standard input. The original pipe descriptors still exist afterward. Duplication creates another reference rather than consuming the source.

`dup2()` replaces the target descriptor as one operation. Separately closing a target and calling `dup()` opens a gap in which another allocation could take the desired number. When its source and target differ, successful `dup2()` clears the target's close-on-exec flag, so the redirected standard stream survives execution. [POSIX `dup2` specification](https://man7.org/linux/man-pages/man3/dup2.3p.html)

The example checks that descriptors 0, 1, and 2 are initially open. Its new pipe endpoints are consequently above 2, so closing the originals cannot close the newly redirected standard stream. This is a checked precondition, not a universal fact about pipe descriptor numbers. A more general launcher can move temporary descriptors above the standard range and handle closed standard streams deliberately.

Before revealing the source, draw a row for the parent and each child. Mark the reference created by each duplication, then cross out every original endpoint. Finally, put each wait after the parent's last required close. Your drawing should explain the implementation's order without depending on the numbers 3 and 4.

:::reveal Reference implementation

{{include:examples/pipeline.c}}

:::

## Review exceptional transitions

The original endpoints are marked `FD_CLOEXEC` and explicitly closed in each child. Marking expresses their intended lifetime; explicit closure establishes the topology before execution. The gap between `pipe()` and setting these flags is acceptable under this example's single-threaded launch model. A threaded launcher must reconsider concurrent descriptor creation and execution.

Each child redirects, closes the original ends, executes, or terminates through `_exit()`. As in Chapter 5, the `execvp()` design assumes a single-threaded launcher. The first `--` splits two existing argument vectors; the invoking shell handles quoting. A literal `--` cannot be passed to the left command with this grammar. A library interface taking two separate vectors would avoid that restriction.

If the second fork fails, a first child already exists. The parent closes its endpoints, terminates that owned child, and collects it. If either parent close fails after both children exist, it terminates both before collection: an uncertain retained endpoint could otherwise prevent EOF. This avoids blindly retrying a descriptor number and avoids waiting for a closure condition the parent may itself be preventing. See Chapter 3 for interrupted-close portability.

The cancellation policy uses `SIGKILL` for an incomplete setup. That is an explicit policy for this small tool, not orderly application shutdown. It addresses the immediate children, not every descendant an arbitrary command might create. Commands with other blocking dependencies can still wait indefinitely; this example does not provide general job control, signal forwarding, or time bounds.

## Decide what success reports

A byte stream does not carry the producer's process status. A consumer can successfully count an empty stream produced by a failing program. Returning only the right command's status is one useful policy for shell composition; checking both statuses is another. Our tool returns a nonzero right status if present, otherwise the left status, making producer failure visible.

There is a further consequence: a consumer may intentionally stop early. Once all read references are closed, a later write raises `SIGPIPE`; when that signal is ignored, the write reports `EPIPE`. Our tool can therefore report a nonzero producer result for an intentional early-consumer exit. A specialized pipeline might classify that outcome differently. [POSIX write errors](https://man7.org/linux/man-pages/man3/write.3p.html)

Also distinguish a byte stream from messages. Two writes do not require two corresponding reads. Atomic insertion relative to competing writers does not establish read boundaries. A record-oriented protocol would need lengths, delimiters, or another framing rule. Counting bytes needs no such rule, which is one reason it makes a useful first consumer.

## Challenge the implementation with inputs

Predict output and overall status before each command:

```sh
make build/pipeline
build/pipeline printf 'hello\n' -- wc -c
build/pipeline sh -c 'exit 9' -- wc -c
printf 'pipeline status: %s\n' "$?"
build/pipeline printf '%s\n' alphabet -- tr a-z A-Z
build/pipeline dd if=/dev/zero bs=1024 count=256 -- wc -c
```

Expect a count of `6`; then `0` with pipeline status `9`; then `ALPHABET`; then `262144`, with `dd` diagnostics on standard error. `wc` may pad counts with spaces. The larger transfer exercises concurrent consumption, but one passing size does not prove the absence of all deadlocks. The ownership argument explains why the normal EOF path can finish.

The parent waits for the left child first only after both children are running and its pipe references are gone. Ordering collection operations does not serialize the commands themselves. Keeping those events separate prevents a misleading explanation based only on source order.

## Exercises

1. **Produce a counterexample.** In a disposable copy, retain the parent's write reference until after waiting. Explain the resulting wait graph, then repair it. Predict why changing a delay cannot establish the missing EOF condition.
2. **Change the result policy.** Suppose the consumer intentionally reads one record and stops. Compare returning only its status with reporting producer failure. What application knowledge is needed to classify a broken pipe as expected?
3. **Transfer: add one stage.** Design a three-command pipeline using two channels. Give every original endpoint a cleanup owner and specify the state after failure to create the third child. Compare launching all stages with using a completed intermediate file between two of them.

**Write your version.** Explain EOF through the channel's state, then show how an apparently harmless parent reference changes that state. Include a separate account of why the producer and consumer can make progress.

## Selected answers

:::reveal Review after your attempt

1. The consumer needs every write reference gone; the parent plans to release its reference only after the consumer exits. Closing before waiting breaks the dependency cycle. A delay changes when the same cycle is reached, not the condition that releases it.
2. Right-only status can describe a successful sampling task while hiding an unrelated producer failure. Checking both exposes more failures but can label intentional early termination unsuccessful. A special policy needs knowledge of the consumer's contract and the producer's actual termination cause; treating every nonzero producer status as harmless would discard evidence.
3. The middle stage reads the first channel and writes the second. Each child closes every original endpoint, including those associated with the other channel; the parent retains none after setup. A late creation failure leaves already-created children and endpoint references to unwind. Spooling can simplify that concurrent failure topology and allow inspection, but adds storage and a different completion boundary. The chosen constraints decide which cost is acceptable.

:::
