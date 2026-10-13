# Chapter 3 — Files and partial I/O

Copy an input stream of unknown length to an output stream using a fixed amount of memory. This time output may begin before input finishes. Preserve every input byte, including zero bytes, in order. If an operation fails, report failure even if an output prefix has already escaped. The command accepts no filenames; its caller supplies the two connections.

That single change from Chapter 2 removes the need to remember the entire input. It does not remove the need to remember unfinished work. The central design question is now: when may the program safely reuse a buffer?

## Write down the conditions for the promise

Our program, `copyfd`, uses ordinary blocking byte-stream operations. It assumes usable standard descriptors and exclusive responsibility for consuming input and producing output while copying. An unrelated writer sharing the output can interleave its own bytes; an unrelated reader sharing input can consume bytes we never see. For seekable files, another actor can also change a shared offset. These conditions belong in the contract because the copy loop cannot enforce them on its own.

| Statement | Its role |
| --- | --- |
| A successful transfer reports the number of bytes transferred, which may be short. | A POSIX interface contract. |
| The next request concerns bytes not yet transferred. | A consequence of our no-loss, no-duplication requirement. |
| The reusable array has 16384 bytes. | An implementation choice. |
| Descriptors use blocking behavior. | An assumption of this command's supported operating mode. |
| A successful call stored data durably on a physical device. | An unjustified inference; the transfer interface does not promise it. |

The target is the byte sequence observed and emitted through these interfaces. It is not a coherent snapshot of a concurrently modified file, nor a promise about a terminal's rendering of those bytes. Make the boundary of the claim explicit before testing it.

## Compare designs by the requirement they satisfy

A whole-input buffer preserves bytes but fails our fixed-memory requirement for arbitrarily long input. A one-byte buffer satisfies it and simplifies bookkeeping, at the cost of many operations. A larger reusable buffer has the same logical behavior while reducing calls when endpoints permit larger transfers. We choose the third, without pretending that 16384 is a universal optimum.

There is also a choice of abstraction. C's buffered stream interface can implement a correct copier. POSIX descriptor operations expose transfer counts more directly, making incomplete progress easier to study. This chapter uses that interface for the data path and standard I/O for diagnostics on a separate stream. Neither family of functions is intrinsically more correct.

A tempting larger-buffer implementation performs one read, one write of everything read, then repeats. Suppose the read supplies ten bytes but the write accepts three. Reusing the buffer immediately loses seven bytes. Retrying the original ten instead duplicates the accepted prefix: after `ABC`, the next output begins `ABC` again. These are different failures caused by the same missing fact—how far the output operation progressed.

## Derive state from conservation of bytes

For one chunk, store `len`, the valid byte count, and `done`, the count already accepted by the output. The pending region is `[done, len)`, with the invariant `0 <= done <= len <= capacity`. That region shrinks as writes succeed. Its bytes must remain available and unchanged until accepted by the output or the copy fails.

Conceptually, during successful copying, total bytes read equal bytes accepted by the output plus bytes still pending. These totals let us check the accounting; the implementation need not store ever-growing counters. The bounded pair `len` and `done` contains enough information for the next action.

A write that accepts `n` bytes increases `done` by `n` and leaves `len` unchanged. When `done == len`, there are no pending bytes, so the array can safely receive the next input chunk. The program must not read into that array earlier. This is the ownership rule connecting the two loops.

Separate safety from progress. Bounds ensure that an output request refers to live bytes in the pending region. For progress, track `len - done`: each positive write reduces the bytes remaining. An interrupted call with no reported progress leaves that count unchanged. A zero result for a nonempty request supplies no progress, so this implementation treats it as an error instead of advancing the offset or retrying indefinitely.

The proof is conditional. A blocking operation may wait indefinitely, and a repeated interruption need not eventually stop. This program has no overall deadline. “Every positive result decreases remaining work” is a useful statement; “this loop always finishes quickly” would be unsupported.

## Read the result before classifying the event

A file descriptor is a nonnegative integer naming a process-local reference to an operating-system-managed endpoint. The standard descriptors are 0, 1, and 2, named by `STDIN_FILENO`, `STDOUT_FILENO`, and `STDERR_FILENO`. Their numbers describe their conventional roles, not the kinds of objects connected to them: standard input might be a file or a pipe.

For an ordinary file, the descriptor refers to an *open file description*, which carries state such as the current offset. Copying the descriptor integer into another C variable creates a second copy of the number, not a new resource. Closing through either variable makes that number invalid for both unless a later operation reuses it. Chapter 5 examines how distinct descriptors can also share an open file description.

A C `FILE *` also carries library stream state and buffering. Mixing buffered operations and descriptor operations on the same connection requires coordination because a library stream may already hold data the descriptor-level code cannot see. Our data path avoids that additional state.

For a positive-size `read`, a positive result is the count obtained and minus one denotes failure. This copier treats zero as the end of its input, as appropriate for regular files, pipes, and ordinary connected stream sockets. A short success is not itself EOF: a pipe can supply the bytes currently available while its writer remains open. These return rules are documented in the [read manual](https://man7.org/linux/man-pages/man2/read.2.html). Store the result in `ssize_t`; converting a negative failure result to `size_t` before testing it destroys the distinction between count and error.

Do not generalize that zero-result interpretation to every device configuration. A noncanonical terminal with `VMIN = 0` can return zero because no byte arrived before its timer expired, even with `O_NONBLOCK` clear. Such terminal modes need a different input policy and are outside this copier's contract. [Terminal read modes](https://man7.org/linux/man-pages/man3/termios.3.html)

Only a reported failure gives `errno` its relevant meaning. Successful calls need not reset it. A stale `EINTR` after a positive count must not cause a retry that discards progress. If cleanup occurs before reporting a failure, save the original error first; another operation may overwrite it.

For this copier, `read` or `write` reporting `-1` with `EINTR` is retried without changing the offset. Other failures end the copy. A nonblocking descriptor may report `EAGAIN` or `EWOULDBLOCK`; that is outside this version's supported mode and is reported as failure. Repeating immediately would waste CPU rather than establish readiness.

Positive output counts mean accepted bytes, not durable storage or completed processing by another application. The [write manual](https://man7.org/linux/man-pages/man2/write.2.html) describes partial transfers and delayed errors. Durability would add different requirements and operations to our design.

## Derive it yourself

Trace a ten-byte chunk `ABCDEFGHIJ` through writes returning 3, then `-1/EINTR`, then 2, then 5. Before each call, write the starting offset and requested length. State which bytes remain valid in the array throughout the trace.

Then trace reads returning 4, then 3, then zero, even though every request asked for a full buffer. Explain what goes wrong if the first short read is treated as EOF. Finally, identify exactly when a new read may overwrite the previous chunk. Do this before opening the reference.

:::reveal Reference implementation

{{include:examples/copyfd.c}}

:::

The small signal setup makes a broken output pipe observable through an error return by ignoring `SIGPIPE`. Otherwise the default signal action normally terminates the writer. That is explicit process policy, explained in Chapter 7, rather than part of the byte-conservation argument.

## Follow the bytes through a short-write trace

Suppose one read has filled the first eight array slots with `ABCDEFGH`. Set `len = 8` and `done = 0`. A scripted output endpoint returns the results below; each positive result accepts that many bytes from the beginning of the requested range.

| Call | Requested bytes | Result | `done` afterward | Output accepted so far | Pending bytes |
| --- | --- | --- | --- | --- | --- |
| 1 | `ABCDEFGH`, length 8 | 2 | 2 | `AB` | `CDEFGH` |
| 2 | `CDEFGH`, length 6 | `-1`, `EINTR` | 2 | `AB` | `CDEFGH` |
| 3 | `CDEFGH`, length 6 | 3 | 5 | `ABCDE` | `FGH` |
| 4 | `FGH`, length 3 | 3 | 8 | `ABCDEFGH` | Empty |

After each row, accepted bytes plus pending bytes still account for the original eight. The actual requests are `data + done` and `len - done`: the pointer advances exactly as the count shrinks. Call 2 repeats call 1's *remaining suffix*, because the interruption reported no progress. It does not restart the whole chunk.

Notice what the implementation does not need to do. It does not erase `AB`, shift `CDEFGH` to the beginning, or allocate a new array. The bytes can stay where they are; two integers describe which part still matters. After call 4 the whole array becomes reusable, even though it still physically contains the previous bytes.

**Predict:** replace call 4 with a fatal error that accepts no further bytes. What remains in the output, what does `done` mean, and may the copier claim success because all eight bytes were read? Separately, suppose call 3 returns 3 while `errno` still contains `EINTR` from call 2. Which observation controls the next request?

:::reveal Review the transfer trace

The failing version has accepted only `ABCDE`; `FGH` remains pending when `write_all` returns failure. Reading a byte and delivering it are separate events, so consuming all input does not establish a complete copy. The command reports failure and cannot retract the accepted prefix. A zero result on call 4 also fails under this implementation's no-progress policy.

The positive count from call 3 controls the transition even if `errno` still says `EINTR`. Advance to offset 5 and request three bytes. Retrying from offset 2 would duplicate `CDE`. Inspecting `errno` only after the documented failure result prevents an old error from undoing new progress.

:::

## Cleanup has a different contract

Do not generalize an I/O retry rule into “retry every interrupted system call.” A close operation releases a reference whose numeric identifier can be reused. If release already happened, a retry might act on a different resource assigned the same integer.

Interrupted-close behavior differs across standards editions and implementations. Linux releases the descriptor even when reporting `EINTR`, whereas POSIX.1-2024 requires the descriptor to remain open on that error. The [close manual](https://man7.org/linux/man-pages/man2/close.2.html) explains this divergence. Our short-lived command attempts each close once, reports failure, and exits; termination releases any remaining descriptors. This is a stated policy for this program, not a portable resource-management recipe for a long-lived library.

The command returns zero only after successful copying and its checked closes; detected operational failures return 1 and arguments are rejected with 2. A failure may leave an output prefix. Status communicates that the copy did not complete; it cannot retract bytes already accepted downstream. Even success does not establish durable storage.

## Make the tests target the reasoning

```sh
mkdir -p build
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -g examples/copyfd.c -o build/copyfd
printf 'one\ntwo\n' > build/copy-input.txt
./build/copyfd < build/copy-input.txt > build/copy-output.txt
printf 'copy status=%s\n' "$?"
cmp build/copy-input.txt build/copy-output.txt
printf 'comparison status=%s\n' "$?"
printf 'A\000B\n' | ./build/copyfd | od -An -tx1
./build/copyfd < /dev/null > build/copy-empty.txt
```

These cases check whole-output equality, a byte that string operations can mishandle, and EOF without a data chunk. Capture the copier's status before running `cmp`: matching bytes alone do not show that checked cleanup succeeded. The binary pipeline makes byte values visible, but its shell status ordinarily belongs to the final `od` command. None of these cases forces short writes; a scripted transfer substitute is more decisive for that property.

For a performance experiment, derive a prediction before changing the buffer. In a model where every transfer supplies or accepts the full requested chunk, copying 1 MiB through a 16 KiB array needs 64 data reads, 64 writes, and one read to discover EOF: 129 transfer calls. A one-byte array needs 2,097,153. Short transfers and interruptions increase those counts. This explains one possible source of savings without predicting the same ratio in elapsed time; waiting for an endpoint may dominate either version.

Question the need for the pass itself before tuning those calls. If a pipeline stage only forwards bytes and its consumer can receive the original input connection directly, removing the stage removes its copying work. If the stage must also save a separate copy, that requirement justifies additional work. For a necessary copier, compare measured call counts with the model, then vary only buffer size. A mismatch identifies an assumption to investigate before adding threads or a more elaborate I/O design.

An invalid inherited input supplies a separate failure case:

```sh
./build/copyfd <&- > build/copy-error.txt
printf 'status=%s\n' "$?"
```

The shell closes standard input before execution. Compare the operation and status rather than requiring identical English error messages across platforms.

Never use the same pathname for both ordinary shell redirections:

```text
Do not run: ./build/copyfd < important.txt > important.txt
```

The shell may truncate the output before this program reads. A stream copier cannot repair that setup. Safe file replacement requires a separate contract involving naming, temporary storage, metadata, and failures.

## Exercises and a change in scheduling

1. Use a one-byte array in an experimental copy. Verify identical output and count calls for a fixed input. Predict which overhead changes before measuring elapsed time; distinguish the transfer algorithm from the chosen chunk size.
2. Add an output-byte counter. Specify whether it counts bytes read, requested, or accepted. Check arithmetic before each addition, and explain what value remains meaningful if output fails partway through a chunk.
3. Inject scripted transfer results into `write_all`: short success, interruption, another short success, zero progress, and fatal failure. Assert requested offsets and lengths as well as final status. A test that only checks total output can miss a mistaken retry on a failure branch.

**Transfer task — change one assumption.** Input and output now use nonblocking descriptors, and other streams must progress while this copy waits. Design the persistent state that survives a return to an event dispatcher. Specify what is retained on would-block, when another input read is permitted, and when write readiness should be requested. Compare an immediate retry loop with suspending until readiness; derive the CPU and fairness consequences. Chapter 11 supplies the waiting interfaces later.

**Write your version.** Explain the copier through conservation of accepted input bytes and the decreasing pending range. Identify the environmental assumptions needed for eventual completion and a failure that can leave a valid but incomplete output prefix.

:::reveal Your review

The ten-byte write trace requests `(offset, length)` pairs `(0, 10)`, `(3, 7)`, `(3, 7)`, and `(5, 5)`. Interruption changes neither offset nor remaining bytes. The array is reusable only after all ten bytes have been accepted. Reads of four and three bytes produce seven bytes before the eventual EOF; discarding the second read loses data.

`cmp` reports no difference and returns zero on equal files. The binary example displays `41 00 42 0a`, and empty input succeeds with empty output. Closing input before launch produces status 1 and a read diagnostic; the checked close may produce a second diagnostic for the already invalid descriptor.

A one-byte buffer usually adds call overhead but does not change the required output. No universal speed ratio follows without measurement. A counter of accepted output bytes advances only after positive writes; requested sizes are not progress. A zero-progress result must not preserve an unbounded busy loop.

For the nonblocking transfer, the array, valid length, and consumed offset become connection state rather than temporary state confined to one blocking invocation. Would-block preserves them. Do not overwrite pending output, and request writable events only while such output exists. Readiness permits another attempt; it does not promise the entire remaining transfer will finish.

:::
