# Chapter 13 — Build and finish a streaming search tool

You need to find which lines of an input contain a literal pattern: an exact sequence of bytes, with no regular-expression syntax. The input can exceed memory; a single line can also exceed memory. Bytes may arrive in arbitrary groups, including a group ending halfway through the pattern. A caller needs the matches found before a later file fails, together with a reliable indication that the search was incomplete.

These constraints do not yet name an algorithm. Start by identifying information that a future decision needs. If a read ends with `nee` while the pattern is `needle`, forgetting that suffix makes the next `dle` indistinguishable from an unrelated beginning. Some information must survive the read. Whether you retain bytes, candidate positions, or a compact state is a design choice to investigate.

The final project, `sift`, combines an algorithmic argument with operating-system contracts. Derive them separately, then examine the boundary between them. A correct matcher does not make a failed output stream successful, and a reliable read loop does not prove that matching across chunks is correct.

## Establish what is fixed and what is chosen

| Claim | Its role in the design |
| --- | --- |
| Report each matching line once | Requirement; determines the observable events |
| Memory does not grow with input or line length | Requirement; rules out storing an arbitrarily long line |
| A positive read reports the bytes available from that call | External interface contract; the count determines the valid buffer range |
| Input contents do not form a guaranteed snapshot | Environment limit; results describe bytes actually observed |
| Patterns contain 1–4096 bytes and no newline | Chosen interface limit; simplifies matching and line semantics |
| Use an 8192-byte input buffer | Implementation choice; correctness must survive a different size |

Do not quietly turn the pattern limit or buffer size into a law of streaming search. A different interface could accept binary patterns from a file. A different algorithm could use a different representation of partial progress. Our task is to justify this particular set of promises and costs.

## Challenge the output requirement before choosing storage

Suppose the initial design says, “Allocate a growing buffer for each line, then print the line if it matches.” Before tuning allocation, ask why the line must be stored. Our caller needs the label and line number. Once a match is known, earlier line bytes cannot affect that report. Keeping the entire line is work introduced by the proposed design.

Change the promise to printing every matching line in full, with no bytes from nonmatching lines in the output. Now consider a nonseekable stream whose enormous first line has its only match near the end. Until that match appears, the program cannot know whether to emit the earlier bytes. Once it appears, it must reproduce them. A single pass therefore needs some way to retain an arbitrarily long prefix in the worst case. A more clever growth factor cannot remove that information requirement.

Possible revisions include a line-length limit, temporary-file storage, or rereading a stable seekable input. Each changes a resource or input assumption. Reporting line numbers removes the retention requirement only because it is the agreed output contract. The smaller buffer is justified by the information the caller needs, not by silently dropping part of the requested output.

## Define the command before the algorithm

The command is:

```text
sift PATTERN [FILE ...]
```

`PATTERN` is a nonempty string of at most 4096 bytes, with no newline. There is no option parser: the first argument is always the pattern, even when it begins with a hyphen. A command-line argument cannot contain a zero byte, so this interface cannot express a pattern containing one. The input itself may contain any byte value.

With no files, the program reads standard input. A filename of `-` also means standard input. Other arguments name inputs opened for reading; the program does not write to them. Ordinary files are the expected use, but a named pipe may also be read and can wait for a producer. Repeating `-` reads the same standard-input stream from its current position, not a fresh copy.

Each matching line produces one output record:

```text
label:line-number
```

Line numbers start at 1 for each input. A newline byte ends a line. Carriage return is an ordinary byte, so CRLF input includes a carriage return just before each newline. A final line needs no newline to be searched. The program reports a line as soon as its first match is found, without waiting for a newline or EOF. Empty input and empty lines cannot match a nonempty pattern.

Labels are escaped byte by byte. A colon, backslash, control byte, or non-ASCII byte appears as `\xHH`. This keeps filenames containing colons or newlines from changing the output structure. It is a display format, not a Unicode-aware filename presentation. Standard input uses the fixed label `(stdin)`.

The result statuses are part of the interface:

| Status | Meaning |
| --- | --- |
| 0 | At least one match, with no reported error |
| 1 | No matches, with no reported error |
| 2 | Invalid arguments or an input, output, allocation, or cleanup error |

An error takes precedence over matches. The program continues to later files after an input error, so partial results can still be useful. An output failure stops further searching once detected. Buffered output may reveal a failure only at the final flush.

## Derive it yourself before running the reference

Design a state record for one input. Identify what changes on an ordinary byte, a newline, a completed match, and the end of a read. Use the pattern `ababac` and input `ababababac\nno match\nababac`. Predict the reported lines if the first read stops after the first five bytes, then after six. The read division must not change the logical result.

Write two candidate designs before choosing one. Include memory cost for a line of unknown length, what happens to partial state between files, and how an output error affects the exit status. Your design may differ from the reference if its contract and argument are sound.

Now build and run the finished program to obtain an observation against which to compare your prediction:

```sh
make build/sift
printf 'ordinary\nneedle here\nneedle needle\nlast' > sift-input.txt
./build/sift needle sift-input.txt
```

Expected output:

```text
sift-input.txt:2
sift-input.txt:3
```

Now try a pattern with no matches and inspect the status immediately:

```sh
./build/sift absent sift-input.txt
printf 'status=%s\n' "$?"
```

You should see `status=1`. The shell expands `$?` before invoking `printf`, so it captures the previous command's result. A later unrelated command would replace that value.

## Choose what must survive a read

The operating system may divide `needle` into `nee` and `dle`. A matcher that starts over on each read misses that occurrence. The counterexample identifies the missing state; it does not by itself select a replacement algorithm.

Compare three useful candidates. Let `M` be pattern length and `L` the longest line encountered.

| Representation | Memory consequence | Reason to choose or reject it here |
| --- | --- | --- |
| Read a whole line, then search it | Storage grows with `L` | Simple when line length is bounded; violates our arbitrary-line memory requirement |
| Retain up to `M - 1` previous bytes and search each joined window | Bounded by pattern and buffer sizes | Correct with careful duplicate suppression; a simple scan can repeatedly compare the same prefixes |
| Record matched-prefix length and a precomputed fallback table | Storage grows with `M` | Supports a linear-work matching argument without retaining previous input |

The suffix-window design is a legitimate solution to the memory requirement. We choose the third because it also gives a useful bound on matching work. Memory constraints motivate persistent state; the additional cost goal motivates how that state is represented.

Before a line has been reported, `matched` records the length of the **longest** pattern prefix that is also a suffix of the bytes examined on that line. For pattern `needle`, input `xnee` leaves `matched == 3`: the suffix `nee` is a possible beginning of the pattern. The next byte `d` increases it to 4. A mismatch may reduce it; returning from one read and beginning another leaves it unchanged. Each byte-processing transition must preserve this invariant.

The matcher also needs the current line number and a Boolean saying whether that line has already been reported. These three values summarize the input history relevant to the next report:

| Event | Required transition |
| --- | --- |
| Non-newline byte on an unreported line | Update the longest matching prefix; report if it becomes a complete match |
| Non-newline byte on an already reported line | Skip matching; this line needs no second report |
| Newline | Advance the checked line counter; clear prefix progress and the reported flag |
| End of a read | Preserve all three values for the next chunk |
| EOF | Finish this input; no delayed match report is needed |
| Start of another input | Reset to line 1, no matched prefix, and no report |

The newline reset is justified by the ban on newline bytes in a pattern. Without that restriction, the reset could discard a valid match crossing a line boundary.

The line counter uses `uintmax_t`. It is large, but it is still finite. The implementation rejects an increment that would overflow. Size limits are part of the contract even when reaching them would be impractical in an ordinary session.

## Reuse information after a mismatch

Consider pattern `ababac` and input `ababababac`. After matching `ababa`, the next input byte is `b`, but the next pattern byte would be `c`. Starting over discards information: the suffix `aba` already matches the first three bytes of the pattern.

A prefix table records this information in advance:

```text
pattern index:  0 1 2 3 4 5
pattern byte:  a b a b a c
prefix length: 0 0 1 2 3 0
```

Each table entry describes the pattern prefix ending at that position: it gives the length of the longest shorter prefix that is also its suffix. For `ababa`, that shorter prefix and suffix is `aba`, of length 3. Such a shorter prefix is called a **proper prefix**. On a mismatch with `k > 0` bytes already matched, entry `k - 1` identifies the longest shorter candidate. It must be both a suffix of the bytes already matched and a prefix of the pattern. Repeating this lookup considers progressively shorter candidates without rereading old input.

We can derive the table instead of accepting it as an input. For each growing pattern prefix, look for its longest proper prefix that also appears at the end:

| Prefix considered | Longest matching proper prefix and suffix | Table entry |
| --- | --- | --- |
| `a` | Empty | 0 |
| `ab` | Empty | 0 |
| `aba` | `a` | 1 |
| `abab` | `ab` | 2 |
| `ababa` | `aba` | 3 |
| `ababac` | Empty | 0 |

The constructor avoids searching every possible suffix afresh. For index `i >= 1`, start with `k = prefix[i - 1]`. Those `k` bytes already match at both ends of the previous prefix. If the new byte equals `pattern[k]`, the match extends by one. On a mismatch with `k > 0`, `prefix[k - 1]` names the next shorter candidate, using an entry already computed.

For the final `c`, start at `k == 3`: the next expected byte is `pattern[3]`, or `b`. That fails. Fall back to `prefix[2] == 1`; `pattern[1]` is also `b`. Fall back again to `prefix[0] == 0`; `c` does not match `pattern[0]`, or `a`. Store zero. The constructor and the input scanner reuse the same relationship between prefixes and suffixes.

Predict the final entry if the pattern is `ababaa` instead. Does reaching candidate length zero always force the entry to zero?

:::reveal Review the table construction

The final entry is 1. The incoming `a` still causes the fallbacks from 3 to 1 to 0, but now it matches `pattern[0]`, creating a one-byte prefix. Zero is the length before comparing this byte, not an instruction to discard it. The full table is `0, 0, 1, 2, 3, 1`.

:::

The argument explains why simply setting `matched` to zero on every mismatch is incomplete: a nonempty suffix may remain a valid beginning. It also explains why subtracting one is insufficient as a complete rule: not every shorter suffix is a pattern prefix. You could check those suffixes directly, but precomputing their relationships avoids repeating that work.

Trace the mismatch above. The first five input bytes, `ababa`, leave five bytes matched. Continue with:

| Input index | Incoming byte | Matched length before | Action | Matched length after |
| --- | --- | --- | --- | --- |
| 5 | `b` | 5 | Fall back using entry 4 to length 3; compare the same `b` again | 4 |
| 6 | `a` | 4 | Extend the prefix | 5 |
| 7 | `b` | 5 | Fall back to 3; compare the same `b` again | 4 |
| 8 | `a` | 4 | Extend the prefix | 5 |
| 9 | `c` | 5 | Complete the pattern and report line 1 | 6, then reset to 0 |

Fallback changes the candidate prefix without consuming the incoming byte. Advancing to the next input byte during fallback would lose a possible match. The matching occurrence occupies indices 4 through 9; no earlier input bytes need to remain in memory. For the earlier input `ababababac\nno match\nababac`, the reports are lines 1 and 3 under either proposed read division.

This is the central idea of the Knuth–Morris–Pratt family of string-matching algorithms. The useful property is that total matching work grows linearly with input length plus pattern length: each input step increases the matched length by at most one, and fallback steps reduce progress previously accumulated. Account for both increases and decreases across the whole input rather than multiplying a worst-looking inner loop by every byte. The table has one entry per pattern byte; its construction admits the same style of accounting.

This bound concerns matching, not all costs of running the command. Formatting and writing each label also takes work, and an output operation can wait on an external consumer. An algorithmic bound does not establish a wall-clock deadline.

For a pattern of `M` bytes, the matcher allocates `M` bytes for its copy and `M * sizeof(size_t)` bytes for the prefix table, plus one fixed-size state object. The application adds an 8192-byte input buffer and bounded stdio storage. These costs are independent of the input and line lengths. Output volume still has a cost; reporting millions of short matching lines is different work from finding one match on a huge line.

## Separate matching from operating-system work

The matcher never opens files or writes to standard output. It calls an application-supplied callback when it finds a matching line. The same matching logic can therefore report line numbers, count matches, or record test events. The callback owns the output behavior and can block or fail, so its contract is part of the matcher's progress argument.

The application creates one matcher, feeds it bytes, resets it between inputs, and destroys it once. The matcher copies the pattern, so it owns that memory independently of the caller. Input buffers are borrowed only during `matcher_feed` and must remain unchanged until it returns. A callback receives the line number by value and a caller-owned context pointer. It must not feed, reset, or destroy the active matcher: same-thread callback reentry can invalidate state or lifetime even without a thread data race. Calls on a different matcher are allowed.

```text
main owns matcher
    matcher owns pattern + prefix table
search_fd borrows matcher + descriptor
    read fills a temporary input buffer
    matcher_feed borrows bytes for one call
        callback borrows output context
main closes owned input descriptors and destroys matcher
```

The public header expresses the contracts that a caller needs. The structure definition stays private in `matcher.c`, allowing the representation to change without changing callers. After a feed or callback error, callers must reset or destroy the matcher. Resetting starts a new input; it does not resume the failed feed or undo output already accepted by the callback.

Trace pattern `aba` through three feeds: `ab`, then `aaba\nab`, then `a`. Predict each callback's line number and the state left after the second feed. Assume callbacks succeed, then reconsider a failure at the first callback.

:::reveal Review the line and callback trace

The first feed leaves two bytes matched on line 1. The first byte of the second feed completes `aba` and reports line 1. Its remaining `aba` is skipped because that line is already reported. The newline resets reporting and matching; the final `ab` leaves two bytes matched on line 2. The third feed reports line 2 immediately. EOF adds no report.

If the first callback fails, the second feed stops before processing its remaining `aba\nab`. The API returns failure without a consumed-byte count, and the callback may have produced partial output. Feeding the same chunk again cannot safely resume the operation. The reset-or-destroy rule gives callers a defined response to that uncertainty.

:::

Before opening the interface, explain what a caller would need to know to resume after an output pause: which input bytes were consumed and whether the current match was accepted. The reference API does not report that progress. Appendix E's callback exercise asks you to design the stronger contract.

:::reveal Reference interface — capstone/matcher.h

{{include:capstone/matcher.h}}

:::

## Implement the state machine

The constructor validates the pattern before allocation, including checking that `n * sizeof(size_t)` cannot overflow. It checks each allocation immediately and preserves `errno` before cleanup, because cleanup may change it. Destruction accepts a null pointer, making constructor cleanup simple. The structure assignment from `(struct matcher){0}` initializes pointer members to null pointers through C's initialization rules; the all-zero bytes supplied by `calloc` alone do not guarantee that pointer value on every C implementation. See sections 6.7.9 and 7.22.3.2 of the [C committee draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).

The implementation stores pattern bytes without a terminating zero because every access is bounded by the recorded length. Its representation is a byte sequence, not a C string. Only the constructor accepts the caller's C-string representation.

Read the following source with the invariant beside you: before each pattern comparison, `matched` is smaller than the pattern length. When the lengths become equal, the code marks the line reported and resets `matched` before invoking the callback. The next comparison therefore cannot index beyond the pattern. If the callback fails, those state changes have already happened; the caller must reset or destroy the matcher, as the interface requires.

:::reveal Reference implementation — capstone/matcher.c

{{include:capstone/matcher.c}}

:::

## Connect the state machine to streams

The application has three jobs: interpret arguments, drive input, and translate matches into output records. `search_fd` owns no descriptor. That choice keeps resource cleanup with the code that decided whether a descriptor came from `open` or from standard input.

An interrupted read is retried, while a positive read is processed exactly once. A zero read finishes the input. Other errors return to the caller. The state survives across positive reads and resets only when a new input begins.

Output uses buffered standard I/O, which is convenient for small formatted records. The code checks formatting calls and the final flush. Ignoring `SIGPIPE` makes a closed output pipe visible as an I/O error instead of the default signal termination, allowing the tool to preserve its documented status 2.

The application uses a one-shot `close` check for an input it opened. If closing reports an error, it records failure and continues toward process termination; it does not blindly retry a possibly reused descriptor number. This is the short-lived program policy discussed in Chapter 3. A reusable library or long-running server would need a more explicit platform strategy.

The status-combination rule has its own invariant: once an error is recorded, later matches cannot turn the overall result into success. A match sets evidence that a matching line was found; the final output flush can still introduce an error. Keeping these facts separate prevents a last-file result from accidentally overwriting an earlier failure.

:::reveal Reference implementation — capstone/sift.c

{{include:capstone/sift.c}}

:::

## State the limits of the progress argument

For a finite supplied chunk, the matcher advances through its bytes and each fallback decreases a bounded index. It terminates if the callback returns. That is a local progress argument with an explicit dependency.

The command has additional dependencies: blocking reads need input or EOF, and buffered output can eventually require a downstream reader. A pipe writer that never closes or a consumer that never reads can prevent completion. Bounded input memory and a linear-work matcher do not remove those waits. If the requirement changes to a service with deadlines, the I/O architecture and callback contract must change along with the timeout policy.

## Establish evidence for the contract

Run `make test`. The matcher-specific C test forces different chunk boundaries in the same data. The Python suite compares whole-program results with an independent byte-string oracle. It places matches around the 8192-byte input-buffer capacity, although pipe delivery does not force a particular read division. It also checks embedded zero bytes, long lines, the maximum pattern length, invalid patterns, input errors, escaped filenames, standard input, and broken output.

The tests connect specific cases to promised behavior; they do not exercise every resource failure. Allocation failure and line-counter overflow, for example, need controlled fault injection or a test seam to reach reliably. Read `tests/test_matcher.c` and `tests/check.py` as an executable companion to the contract, and identify which claims are tested directly and which still rely on inspection and reasoning.

The program is deliberately sequential. Input files can change while being read, so results describe the bytes observed rather than a coherent filesystem snapshot. Blocking input may wait indefinitely for more data. It has no recursive directory walk, regular expressions, Unicode case folding, or parallel execution. Those are specific possible extensions, each with design work of its own.

## Use the capstone as an infrastructure building block

Suppose a research pipeline needs to search a versioned corpus repeatedly. Extend the contract before adding workers: identify the input version, pattern, matcher version, and completion status for each result. A pathname alone does not identify immutable input. Choose a snapshot or another controlled input-lifetime policy so a retry refers to the same bytes.

Begin by assigning whole files to jobs. Splitting one file at arbitrary byte offsets introduces questions the existing independent-file API does not answer: who owns a line crossing a split, how are line numbers established, and how are duplicates prevented? Compare the cost of resolving those boundaries with the cost of keeping each file intact. Build a Python test driver if your target role calls for Python, keeping the C matcher as the subject of the experiment. A local pipeline with reproducible inputs, explicit failures, and bounded queues is a useful next artifact. The distributed and ML extensions in [Appendix F](#appendix-f--from-c-systems-to-ai-infrastructure) begin from those same obligations.

## Exercises

1. Before looking at the prefix table builder, compute the table for `aaaa` and for `abcab`. Trace a mismatch by hand and explain what each fallback preserves.
2. Add a count-only mode that prints the number of matching lines per input. Write the new command contract first. Decide how an error affects a count already produced, then add tests before changing the implementation.
3. Add parallel processing of independent files while keeping output in argument order and bounding memory. Identify what each worker owns, how results reach the writer, and what happens when the first file is slow but later files finish quickly.

**Transfer test — change one assumption.** Allow a pattern to contain newline bytes. Define whether a reported line is the line where the match starts or ends. Identify the exact transition in the existing matcher that would discard valid progress. Redesign that transition and give a two-line counterexample before modifying code.

**Write your version.** Rewrite this chapter as the argument for your own `sift` variant. Start with the memory and reporting requirements; derive the information you must retain; compare the suffix-window alternative with your choice. Include an ownership diagram, state invariants, a failure trace, and three decisive tests. Explain which conclusion changes in the newline-pattern transfer test.

## Selected answers

:::reveal Review after your attempt

For exercise 1, `aaaa` has prefix lengths `0, 1, 2, 3`; `abcab` has `0, 0, 0, 1, 2`. A fallback preserves a suffix that could still be the beginning of a complete occurrence. It never assumes that bytes preceding that suffix can be reused as a match.

For exercise 2, counting can be implemented with a callback that increments a checked counter. The application can print it after successful completion of each input. If you expose partial counts, label them explicitly and keep the error exit status; do not let a partial count masquerade as a complete one.

For exercise 3, an unbounded queue of later results defeats the bounded-memory requirement. Options include limited per-file queues with backpressure, temporary result files, or relaxing output order. Each worker needs its own mutable matcher state. A single writer can own standard output, but it must have a policy that stops or releases workers if writing fails.

For the transfer test, pattern `a\nb` in input `a\nb` must now be matchable. Resetting prefix progress whenever a newline arrives prevents that. Track line numbering alongside matching rather than treating newline as an unconditional matching reset. The reference's skip-the-rest-of-a-reported-line optimization also needs reconsideration: a later start on that line might create a match with a different ending line. Your chosen reporting semantics determine which duplicates to suppress and what state may be discarded. A fixed completed pattern has a known number of newline bytes, which may help relate its start and end line numbers; justify that relation with your byte-processing convention.

:::

You have a complete example whose requirements, algorithm, ownership, and tests can be examined together. Extend it by revisiting those relationships. When a change invalidates an invariant, repairing the argument should come before preserving a familiar implementation.
