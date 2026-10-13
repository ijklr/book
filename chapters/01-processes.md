# Chapter 1 — Programs become processes

A test harness launches the same executable twice. One invocation should succeed; the other should deliberately return a chosen failure status. A person wants a readable account of each run, while the harness needs a result it can interpret without reading English. Design a command for both callers.

Call it `identity [STATUS]`. It reports its process and account identifiers, its argument list, and the requested status. It accepts at most one optional decimal status from 0 through 125. Invalid arguments produce a diagnostic and status 2. A detected output error takes precedence over the requested status and produces status 1. Otherwise the command returns the requested status, defaulting to zero. Even this small program needs a contract for input, output, and failure.

## Decide what the environment gives you

You cannot derive an operating-system interface from a preference for simple designs. You choose an environment, learn its contracts, then reason within them. Here the environment is C17 with a common POSIX interface set on macOS and Linux.

| Statement | What kind of knowledge is it? |
| --- | --- |
| The runtime supplies arguments to `main`. | An external language/runtime contract. |
| A process has an identifier obtainable through a POSIX interface. | An external operating-system contract. |
| Repeated runs on your machine printed increasing identifiers. | An observation, not a promised ordering. |
| Invalid input returns 2, and a detected output failure returns 1. | Our command's design choices. |
| Standard output is connected to a usable destination. | An environmental assumption to test, not silently promote into a guarantee. |

A program describes computation. An executable file contains code and information needed to run it on a particular platform. A process is an instance with memory, execution state, and operating-system resources. Running one executable twice creates separate instances whose arguments and connections can differ. This distinction explains why recompiling is unnecessary to request a different exit status.

In `main(int argc, char **argv)`, `argc` counts argument strings and `argv` points to their array. The arguments occupy indices below `argc`; `argv[argc]` is a null pointer, not another string to print. In our shell examples, `argv[0]` names the invocation and `argv[1]`, when present, holds the requested status. The startup contract appears in section 5.1.2.2.1 of the [WG14 C committee draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).

The kernel manages shared resources and enforces access between processes. Ordinary C code runs in user space. A library interface may enter the kernel, perform work locally, or combine both. Formatting an integer is library work; delivering the resulting characters eventually involves the operating system. One C function call does not imply one system call.

## Compare two interfaces before implementing either

One plausible design prints `success` or `failure` and always exits zero. A human can read it, but the harness must now parse text whose wording needs its own stable contract. An output failure can also erase the only reported result. Most directly, always exiting zero violates the promise to return the requested status. Text can form a machine-readable protocol, but it does not fulfill that particular promise.

A second design returns a status and prints everything to the same output stream. That gives the harness a result, but a caller saving normal output also saves argument-error messages. A consumer expecting records must distinguish data from diagnostics.

We choose three channels: normal reports on standard output, diagnostics on standard error, and a bounded exit status for completion. The report is for a person; it prints argument strings directly, so an argument containing a newline can span several lines. It is not an unambiguous serialization of the argument list. The harness uses the status instead of parsing this display.

This contract deliberately allows overlapping meanings: status 1 can mean “the caller requested 1” or “output failed.” A harness that must distinguish those cases needs a revised status scheme or an additional structured result. Separating data from diagnostics also cannot guarantee delivery: if standard error fails, there is no further guaranteed reporting channel.

The shell connects these channels before our code runs. Therefore argument validation cannot protect a preexisting file from a shell redirection that already truncated it. Keep responsibility with the component that performs the action.

Suppose someone adds a requirement: “Write a result file so the harness can discover whether the run succeeded.” Separate the desired fact from the proposed mechanism. Does the harness need a completion status immediately, or a record it can recover after restarting? For the first need, the existing exit status may be sufficient. A result file would introduce naming, ownership, stale-record detection, and cleanup without necessarily supplying new information. For the second need, an exit status alone is insufficient; persistence is a real additional requirement.

Test the uncertain assumption with a small harness that discards normal output, waits for the command, and records its status immediately. Run requests for zero and seven. If the caller can distinguish them, text parsing and a result file are unnecessary for that particular decision. This experiment tests the caller's capabilities; it does not decide whether persistent history matters to its users.

## Derive the minimum state and error precedence

The requested status must survive parsing so completion can return it. Parsing also needs a temporary result and evidence about which input characters were consumed. Process identifiers can be retrieved and printed without retaining a history of earlier runs. Keeping such a history would add state without serving this contract.

Our control flow has three phases: validate, report, complete. Before reporting, the status must be representable and within our chosen range. During argument output, each accessed index `i` must satisfy `0 <= i < argc`. These are invariants: properties that must hold at the relevant stage. Status selection follows the stated precedence: invalid arguments return 2 before normal reporting; a detected output failure returns 1; otherwise completion returns the requested status.

Consider an apparently convenient conversion that maps unrecognized text to zero. Both `0` and `nonsense` could then lead to success. Information lost at conversion cannot be recovered by a later range check. We need separate evidence for whether conversion started, where it stopped, and whether its result was representable.

Safety is not the same as completion. A finite argument list gives a finite amount of formatting work, but a write to a blocked pipe can wait indefinitely. This program has no deadline. Its progress claim is conditional: if its library operations return, its bounded loops finish. Signal termination is another possible outcome; the reference retains the default signal policy, so its status-1 rule covers detected output errors, not every possible termination.

## Derive it yourself

Before opening the reference implementation, write a decision table for no argument, `7`, `7x`, an empty argument, and an integer far larger than a `long`. Add a row where parsing succeeds but the final output flush fails. State which phase runs and which status wins.

Then predict this sequence without running it: request status 7, run another successful command, and only then inspect `$?`. What information has been overwritten, and by which component? Finally, decide whether leading spaces, a plus sign, and trailing spaces belong in your input grammar. A convenient library parser must not make that decision invisibly.

## Map the design to documented interfaces

`getpid` and `getppid` obtain the process and parent identifiers. These identifiers describe current process relationships, not permanent identities across time. The [process identification manual](https://man7.org/linux/man-pages/man2/getpid.2.html) documents those interfaces and Linux-specific details. `getuid` and `getgid` report real account identifiers, not a complete authorization model.

The reference uses `strtol` with base 10. It clears `errno` before conversion, then checks for a conversion error, an unchanged end pointer, trailing characters, and an out-of-range value. Its grammar permits leading whitespace and an optional sign, provided the numeric result is 0–125; it rejects trailing text, including trailing spaces. Thus `" +7"` and `"007"` request 7, `"7 "` is invalid, and `"-0"` requests zero. A stricter decimal grammar would need explicit lexical validation.

Types such as `pid_t` and `uid_t` belong to the platform interface. Converting to `intmax_t` or `uintmax_t` and using the corresponding `j` format modifier gives the variadic print function the type it expects. A number that looks small does not justify passing the wrong argument type.

Returning from the initial invocation of `main` performs normal C termination. Normal termination flushes standard-I/O streams, but an explicit flush permits us to inspect failure before choosing a status. POSIX exposes a limited status to the waiting parent, not an arbitrary integer return value. The [exit manual](https://man7.org/linux/man-pages/man3/exit.3.html) describes these termination rules.

:::reveal Reference implementation

{{include:examples/identity.c}}

:::

## Build an experiment, not just an executable

Run commands from the book directory:

```sh
mkdir -p build
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -g examples/identity.c -o build/identity
./build/identity 7
printf 'status=%s\n' "$?"
./build/identity nonsense
printf 'status=%s\n' "$?"
```

`cc` coordinates preprocessing, compilation, assembly, and linking. The language flag selects C17; the feature-test macro requests POSIX declarations rather than adding missing operating-system facilities. Warnings expose suspicious source, `-Werror` makes them stop these teaching builds, and `-g` supplies debugging information. Compiler-specific details appear in the [Clang user manual](https://clang.llvm.org/docs/UsersManual.html).

Separate compilation from linking to test which responsibility belongs where:

```sh
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -g -c examples/identity.c -o build/identity.o
cc build/identity.o -o build/identity
```

An object file can contain unresolved references. Linking connects references to definitions and produces an executable. A header supplies declarations; including `<stdio.h>` does not paste the implementation of `printf` into your source. Source portability also does not imply binary portability: compile separately for macOS and Linux.

## Follow the shell, the program, and the observer

The three result channels become easier to distinguish when you follow one invocation in order. Use scratch paths and assume the file operations below succeed:

```sh
printf 'before\n' > build/identity.txt
./build/identity 7 > build/identity.txt 2> build/identity.err
saved_status=$?
printf 'saved status=%s\n' "$saved_status"
cat build/identity.txt
printf 'status after cat=%s\n' "$?"
```

| Step | Component acting | Result |
| --- | --- | --- |
| Prepare redirections | Shell | Truncate `identity.txt`; open `identity.err` for diagnostics |
| Validate `7` | `identity` | Retain the requested status; produce no diagnostic |
| Format and flush the report | `identity` | Put the report in `identity.txt` |
| Complete | `identity`, then shell | Return 7; make that status available through `$?` |
| Save the status | Shell | Store 7 in `saved_status` before another command replaces `$?` |
| Display the file | `cat` | Print the report and return its own status, normally 0 |

The two printed status lines therefore contain 7 and 0. They describe different commands. The variable preserves the first value; `$?` is not a history of past results. The initial `before` line has disappeared because truncation happened before `identity` wrote anything. Neither the shell's redirection operators nor the output filenames become arguments to `identity`; the shell uses them to establish its connections. These rules belong to the [shell command language](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html).

Now predict the same destinations after validation fails:

```sh
./build/identity bad > build/identity.txt 2> build/identity.err
saved_status=$?
printf 'saved status=%s\n' "$saved_status"
cat build/identity.err
```

Before running it, answer three questions. Does the earlier successful report remain in `identity.txt`? Which command actually makes the diagnostic visible on the terminal? Would an empty `identity.err` alone establish that the requested status was zero?

:::reveal Review the channel trace

The failed invocation leaves `identity.txt` empty: the shell has already truncated it, and argument validation returns before normal reporting. `identity` writes the diagnostic into `identity.err`; the later `cat` copies it onto the terminal. The saved status is 2 even if `cat` subsequently returns zero.

An empty diagnostic file establishes neither a zero status nor successful completion. In the first run it accompanies the deliberately requested status 7. Inspect the channel that actually carries the fact you need. A complete report and a nonzero status are compatible under this command's contract.

:::

## Exercises and a changed requirement

1. Run `identity` three times. Identify one invariant the source establishes, one external guarantee you rely on, and one pattern in the observed numbers that is not guaranteed. Explain why three agreeing observations cannot turn the last claim into an interface contract.
2. Copy the source to `build/identity-experiment.c`. Declare `void missing(void);` and call it inside `main`. Predict the separate compile and link outcomes, then test them. Remove the declaration and explain why the failure moves earlier.
3. Add `--quiet` without making error handling ambiguous. Write the exact argument grammar and status precedence before changing the parser. Cover invalid combinations, accepted zero, and output suppression.

**Transfer task — change one assumption.** The caller now requires a permanent run identifier that remains unique across machine restarts and multiple machines. Keep the simple command as your starting point, but retire any assumption that a PID supplies this property. Compare a shared counter service and independently generated identifiers. State the extra coordination, storage, or probabilistic assumption each needs. Do not label a finite successful test as proof of permanent uniqueness.

**Write your version.** Explain the command as an agreement between caller, shell, runtime, and program. For every requirement, identify the component that can enforce it. Include one requirement this process cannot enforce after startup.

:::reveal Your review

The default request returns zero; `7` returns 7 after successful reporting. Invalid text, an empty argument, and an overflowing conversion return 2 before normal reporting. A detected final output error returns 1 even when the requested result was 7. This precedence is our choice, not a general law of Unix commands.

`$?` describes the most recently completed relevant shell command. An intervening successful command replaces the status 7 you meant to examine. Observe it immediately or save it first.

The redirection experiment leaves the invalid invocation's diagnostic in `identity.err`; its standard-output file is empty because the shell truncated it before the program rejected the argument. The program's validation cannot undo that prior action.

For exercise 1, PIDs ordinarily differ and account IDs ordinarily remain the same across your runs. Those observations do not imply monotonic PID allocation or an unchanging parent relationship. For exercise 2, a declaration permits compiling a call with an unresolved reference; linking fails without a definition. Omitting the declaration causes strict compilation to reject a different problem.

For the transfer task, a central durable allocator moves uniqueness enforcement into shared state and makes its availability and recovery relevant. Independent generation avoids that coordination but needs a precisely stated uniqueness argument, often probabilistic. The important revision is recognizing which original assumption ceased to support the requirement.

:::
