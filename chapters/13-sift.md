# Chapter 13 — Build and finish a streaming search tool

You need to find which lines of an input contain a literal pattern. The input can exceed memory; a single line can also exceed memory. Bytes may arrive in arbitrary groups, including a group ending halfway through the pattern. A caller needs a useful result even when a later file cannot be read, and a reliable indication that some work failed.

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

Possible revisions include a line-length limit, temporary-file storage, or rereading a stable seekable input. Each changes a resource or input assumption. Reporting line numbers removes the retention requirement only because it is the agreed output contract. Write down that agreement before celebrating the smaller buffer: removing necessary output would solve a different problem.

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

Line numbers start at 1. A newline byte ends a line. Carriage return is an ordinary byte, so CRLF input includes a carriage return just before each newline. A final line needs no newline to be searched. The program reports a line once even if the pattern occurs several times within it.

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

At a byte-processing boundary before reporting a line, `matched` records the length of the **longest** pattern prefix that is also a suffix of the bytes examined on that line. For pattern `needle`, after input `xnee`, that length is 3. The next byte `d` increases it to 4. A mismatch may reduce it; a read boundary does not change it. This statement is the state invariant to preserve, not merely an interpretation of a variable name.

The matcher also needs the current line number and a Boolean saying whether that line has already been reported. Once a line is reported, further bytes on that line require only newline detection. A newline resets the prefix state and the reported flag, then advances the line counter. This deliberate reset prevents a pattern from spanning two lines.

The line counter uses `uintmax_t`. It is large, but it is still finite. The implementation rejects an increment that would overflow. Size limits are part of the contract even when reaching them would be impractical in an ordinary session.

## Reuse information after a mismatch

Consider pattern `ababac` and input `ababababac`. After matching `ababa`, the next input byte is `b`, but the next pattern byte would be `c`. Starting over discards information: the suffix `aba` already matches the first three bytes of the pattern.

A prefix table records this information in advance:

```text
pattern index:  0 1 2 3 4 5
pattern byte:  a b a b a c
prefix length: 0 0 1 2 3 0
```

Each table entry is the length of the longest proper prefix that is also a suffix of the pattern through that position. “Proper” means shorter than the whole sequence under consideration. If `k` bytes matched and the next byte disagrees, the next viable shorter candidate must itself be a suffix of those `k` bytes. It must also be a prefix of the pattern. That is precisely the relation the table records. Repeating the fallback considers progressively shorter candidates without rereading old input.

The argument explains why simply setting `matched` to zero on every mismatch is incomplete: a nonempty suffix may remain a valid beginning. It also explains why subtracting one is insufficient as a complete rule: not every shorter suffix is a pattern prefix. You could check those suffixes directly, but precomputing their relationships avoids repeating that work.

Trace the mismatch above. With five bytes matched, consult table entry 4 and keep three. The incoming `b` agrees with pattern position 3, bringing the matched length to four. The remaining input is `a,b,a,c`: the first `a` brings the length to five, the next `b` requires the same fallback and leaves four, and the final `a,c` bring it to five and then six. No previous input bytes need to remain in memory.

This is the central idea of the Knuth–Morris–Pratt family of string-matching algorithms. The useful property is that total matching work grows linearly with input length plus pattern length: each input step increases the matched length by at most one, and fallback steps reduce progress previously accumulated. Account for both increases and decreases across the whole input rather than multiplying a worst-looking inner loop by every byte. The table has one entry per pattern byte; its construction admits the same style of accounting.

This bound concerns matching, not all costs of running the command. Formatting and writing each label also takes work, and an output operation can wait on an external consumer. An algorithmic bound does not establish a wall-clock deadline.

Together, the pattern copy, prefix table, fixed 8192-byte input buffer, and small state use memory proportional to the pattern length. A line can be enormous without requiring a line-sized allocation. Output volume still has a cost; reporting millions of short matching lines is different work from finding one match on a huge line.

## Separate matching from operating-system work

The matcher never opens files or writes to standard output. It calls an application-supplied function when it finds a matching line. This separates a deterministic state machine from operations that can block or fail for external reasons.

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

The public header expresses the contracts that a caller needs. The structure definition stays private in `matcher.c`, allowing the representation to change without changing callers. After a callback or feed error, callers must reset or destroy the matcher; the API does not promise resumability after failure.

Before opening the interface, write down which failure leaves a resumable operation and which failure ends it. This reference chooses a simple rule: reset or destroy after a feed or callback failure. It does not attempt to undo output already accepted by the callback.

:::reveal Reference interface — capstone/matcher.h

{{include:capstone/matcher.h}}

:::

## Implement the state machine

The constructor validates the pattern before allocation. It checks each allocation immediately and preserves `errno` before cleanup. Successful library calls are not a safe place to keep a previous error code. Destruction accepts a null pointer, making constructor cleanup simple. The structure assignment from `(struct matcher){0}` establishes null pointer members explicitly through C initialization rules; zero-filled allocation alone is not a portable promise of null pointer representations. [C committee draft, initialization and `calloc`](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).

The implementation stores pattern bytes without a terminating zero because every access is bounded by the recorded length. Its representation is a byte sequence, not a C string. Only the constructor accepts the caller's C-string representation.

Read the following source with the invariant beside you: outside a complete-match event, `matched` is smaller than the pattern length. When the lengths become equal, the code reports the line and resets `matched` before processing another byte. That prevents the next comparison from indexing beyond the pattern.

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

The tests do not make every failure physically happen. They do not exhaust available memory, simulate every filesystem, or prove the absence of races in an unrelated program. Their value comes from the connection between each case and a promised behavior. Read `tests/test_matcher.c` and `tests/check.py` as an executable companion to the contract.

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
