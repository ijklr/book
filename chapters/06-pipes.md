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

A pipe permits both programs to run concurrently with a bounded kernel buffer. With the blocking descriptors used here, a full buffer makes the producer wait until the consumer frees space. This is **backpressure**: the slower consumer limits how quickly the producer can send. The intermediate bytes are not retained for replay.

| Requirement | Temporary file | Direct pipe |
| --- | --- | --- |
| Consumer starts before producer finishes | Requires an additional concurrent-file protocol. | Fits ordinary streaming use. |
| Inspect or replay the complete intermediate result | Natural after a successful producer. | Requires additional storage or duplication. |
| Bound intermediate buffering independently of total output | Needs a different storage policy or a size limit. | Finite pipe capacity provides backpressure. |
| Explain completion | Producer status and selected file-completion policy. | Both child statuses plus precise stream-closure ownership. |

We choose the pipe because early consumption and bounded buffering matter for this experiment. If replay became the primary requirement, the file design would deserve reconsideration. Neither choice eliminates the need to define what partial output means after producer failure.

## Derive the state that matters

For reasoning, model the channel with three quantities: buffered bytes `B`, open write references `W`, and open read references `R`. A reference is an open descriptor that keeps an endpoint alive; one process can hold several. Duplicating a descriptor or inheriting it across a fork adds a reference. Closing one removes only that reference. The kernel maintains the actual channel state; our program need not duplicate these counters.

For a positive-length blocking read, the normal outcomes follow from this state:

| Buffered bytes | Open write references | Reader's outcome |
| --- | --- | --- |
| `B > 0` | Any | Consume available bytes; a short read is possible. |
| `B == 0` | `W > 0` | Wait for data or for all writers to close. |
| `B == 0` | `W == 0` | Return zero: EOF. |

EOF is a return condition, not a special byte sent by the writer. “The producer finished” is insufficient: another process may still hold a write reference, or buffered bytes may remain unread. Errors and signal interruption still require their usual handling. [POSIX read rules for pipes](https://pubs.opengroup.org/onlinepubs/9799919799/functions/read.html)

The intended final topology is:

```text
producer stdout ---> [ bounded byte buffer ] ---> consumer stdin

parent:          owns no channel endpoints; owns collection obligations
producer:        owns its output reference; no unused channel references
consumer:        owns its input reference; no unused channel references
```

This gives two obligations. Bytes must flow through the intended producer output and consumer input. Each process must also close the channel references it does not need: children do so during setup, and the parent closes its own references before waiting. The parent may begin waiting before a child finishes its setup; correctness does not depend on which runs first. The first obligation concerns data flow; the second is essential for progress.

The parent needs only its owned endpoints, child PIDs, and collected results. Relaying the bytes through a second pipe would add a buffer, partial-transfer state, and failure paths without satisfying any additional requirement. If the parent later needs to inspect or retain the bytes, that requirement would justify a different data path. Inspecting child statuses alone does not.

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

The same reasoning reaches beyond these three processes. If the producer starts a descendant that inherits its standard output, that descendant holds a write reference too. The consumer may wait for EOF after the original producer has exited. When diagnosing the hang, look for every holder of the endpoint, including descendants. [Descriptor inheritance across execution](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html)

## Map the topology onto descriptors

`pipe(ends)` creates the channel and returns its read descriptor in `ends[0]` and write descriptor in `ends[1]`. Forking copies references into each child. A possible scheduling point after both forks, before child cleanup, has both original endpoints in all three processes. Other schedules perform some cleanup earlier; correctness must survive either order. [POSIX pipe creation](https://man7.org/linux/man-pages/man3/pipe.3p.html)

The producer needs its standard output to refer to the write end. `dup2(ends[1], STDOUT_FILENO)` makes that relationship; the consumer similarly redirects standard input. The original pipe descriptors still exist afterward. Duplication creates another reference rather than consuming the source.

`dup2()` replaces the target descriptor as one operation. Separately closing a target and calling `dup()` opens a gap in which another allocation could take the desired number. When its source and target differ, successful `dup2()` clears the target's close-on-exec flag, so the redirected standard stream survives execution. [POSIX `dup2` specification](https://man7.org/linux/man-pages/man3/dup2.3p.html)

The example checks that descriptors 0, 1, and 2 are initially open. Its new pipe endpoints are consequently above 2, so closing the originals cannot close the newly redirected standard stream. This is a checked precondition, not a universal fact about pipe descriptor numbers. A more general launcher can move temporary descriptors above the standard range and handle closed standard streams deliberately.

### Account for every reference through redirection

For one concrete run, suppose the pipe receives descriptors 3 and 4. At the instant just after both forks, let all three processes still hold both originals. Descriptor 3 names the read end and 4 the write end. The inherited standard streams name other resources. Track only references to this new pipe:

| Setup completed so far | Parent's pipe descriptors | Producer's pipe descriptors | Consumer's pipe descriptors |
| --- | --- | --- | --- |
| Both forks | `3:R, 4:W` | `3:R, 4:W` | `3:R, 4:W` |
| Producer redirects and closes originals | `3:R, 4:W` | `1:W` | `3:R, 4:W` |
| Consumer redirects and closes originals | `3:R, 4:W` | `1:W` | `0:R` |
| Parent closes originals | None | `1:W` | `0:R` |

In the producer, `dup2(4, 1)` first replaces that process's old standard-output reference. It leaves descriptor 4 open and adds descriptor 1 as another reference to the write end. Closing 3 and 4 then leaves 1 usable. The consumer performs the corresponding transition with `dup2(3, 0)`. Neither operation changes the parent's descriptors: the descriptor tables belong to different processes even though some entries refer to shared resources.

The final row has `R == 1` and `W == 1`. After the producer closes standard output or exits, `W` reaches zero. The consumer can still read buffered bytes; its next positive-length read after draining them returns EOF. No process sends an EOF byte.

**Predict:** the parent accidentally closes descriptor 1 instead of descriptor 4, then waits. Both children complete their correct setup. Can the consumer finish after the producer exits? What if the producer closes 4 before attempting its `dup2(4, 1)`?

:::reveal Review the descriptor trace

The parent retains its pipe write reference at 4. Closing its own standard output does not remove that reference, so the consumer can drain the pipe and then wait forever for EOF. In the second variation, `dup2()` fails because its source descriptor has already been closed, assuming nothing has reused that number. Redirection must acquire the surviving reference before cleanup releases the original. [POSIX descriptor duplication](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dup.html)

:::

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

There is a further consequence: a consumer may intentionally stop early. This is where `R` matters. Once `R == 0`, a later write generates `SIGPIPE`; its default action terminates the writer. If the signal is ignored, the write instead reports failure with `EPIPE`. Our tool can therefore report a nonzero producer result for an intentional early-consumer exit. A specialized pipeline might classify that outcome differently. [POSIX write errors](https://man7.org/linux/man-pages/man3/write.3p.html)

Also distinguish a byte stream from messages. If a producer writes `ab` and then `cd`, the consumer must accept `abcd` in one read or the same bytes split across smaller reads. The write boundaries are not part of the stream. Atomic insertion relative to competing writers does not establish read boundaries either. A record-oriented protocol needs lengths, delimiters, or another framing rule. Counting bytes needs no such rule, which is one reason it makes a useful first consumer.

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
