# Chapter 1 — Programs become processes

A test harness launches the same executable twice. One invocation should succeed; the other should deliberately return a chosen failure status. A person wants a readable account of each run, while the harness needs a result it can interpret without reading English. Design a command for both callers.

Call it `identity [STATUS]`. It should report its process and account identifiers, its argument list, and the requested status. It accepts at most one optional decimal status from 0 through 125. Invalid arguments produce a diagnostic and status 2. A detected output error takes precedence over the requested status and produces status 1. Otherwise the command returns the requested status, defaulting to zero. This is a small program with several observable promises, not simply a request to print some numbers.

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

The kernel manages shared resources and enforces access between processes. Ordinary C code runs in user space. A library interface may enter the kernel, perform work locally, or combine both. Formatting an integer is library work; delivering the resulting characters eventually involves the operating system. One C function call does not imply one system call.

## Compare two interfaces before implementing either

One plausible design prints `success` or `failure` and always exits zero. A human can read it, but the harness must now parse text. If a diagnostic changes language or wording, the harness can break. Worse, an output failure can erase the only result. This design does not satisfy our machine-readable completion requirement.

A second design returns a status and prints everything to the same output stream. That gives the harness a result, but a caller saving normal output also saves argument-error messages. A consumer expecting records must distinguish data from diagnostics.

We choose three channels: normal records on standard output, diagnostics on standard error, and a bounded exit status for completion. That choice follows from our callers' needs; another application could legitimately choose a structured message protocol instead. Separating channels does not ensure that every message reaches a human. If the diagnostic destination fails too, the command has no additional guaranteed reporting channel.

The shell connects these channels before our code runs. Therefore argument validation cannot protect a preexisting file from a shell redirection that already truncated it. Keep responsibility with the component that performs the action.

Suppose someone adds a requirement: “Write a result file so the harness can discover whether the run succeeded.” Separate the desired fact from the proposed mechanism. Does the harness need a completion status immediately, or a record it can recover after restarting? For the first need, the existing exit status may be sufficient. A result file would introduce naming, ownership, stale-record detection, and cleanup without necessarily supplying new information. For the second need, an exit status alone is insufficient; persistence is a real additional requirement.

Test the uncertain assumption with a small harness that discards normal output, waits for the command, and records its status immediately. Run requests for zero and seven. If the caller can distinguish them, text parsing and a result file are unnecessary for that particular decision. This experiment tests the caller's capabilities; it does not decide whether persistent history matters to its users.

## Derive the minimum state and error precedence

The only persistent decision is the requested status. Parsing also needs a temporary result and evidence about which input characters were consumed. Process identifiers can be retrieved and printed without retaining a history of earlier runs. Keeping such a history would add state without serving this contract.

Our control flow has three phases: validate, report, complete. The invariant before reporting is that the status is representable and within our chosen range. The invariant during argument output is that only entries below `argc` are accessed. Completion must select the strongest detected outcome: invalid syntax prevents reporting; a later detected output failure overrides an otherwise valid requested status.

Consider an apparently convenient conversion that maps unrecognized text to zero. Both `0` and `nonsense` could then lead to success. Information lost at conversion cannot be recovered by a later range check. We need separate evidence for whether conversion started, where it stopped, and whether its result was representable.

Safety is not the same as completion. A finite argument list gives a finite amount of formatting work, but a write to a blocked pipe can wait indefinitely. This program has no deadline. Its progress claim is conditional: if its library operations return, its bounded loops finish. Signal termination is another possible outcome; the reference retains the default signal policy, so its status-1 rule covers detected output errors, not every possible termination.

## Derive it yourself

Before opening the reference implementation, write a decision table for no argument, `7`, `7x`, an empty argument, and an integer far larger than a `long`. Add a row where parsing succeeds but the final output flush fails. State which phase runs and which status wins.

Then predict this sequence without running it: request status 7, run another successful command, and only then inspect `$?`. What information has been overwritten, and by which component? Finally, decide whether leading spaces, a plus sign, and trailing spaces belong in your input grammar. A convenient library parser must not make that decision invisibly.

## Map the design to documented interfaces

`getpid` and `getppid` obtain the process and parent identifiers. These identifiers describe current process relationships, not permanent identities across time. The [process identification manual](https://man7.org/linux/man-pages/man2/getpid.2.html) documents those interfaces and Linux-specific details. `getuid` and `getgid` report real account identifiers, not a complete authorization model.

The reference uses `strtol`. It checks a reported conversion error, an unchanged end pointer, trailing characters, and the accepted range. Its chosen grammar permits the leading whitespace and optional sign recognized by `strtol`, provided the numeric result is 0–125; it rejects trailing text, including trailing spaces. In particular, textual `-0` is accepted because its value is zero. A stricter decimal grammar would need explicit lexical validation.

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

Now test the channel design using scratch paths:

```sh
./build/identity 0 > build/identity.txt
cat build/identity.txt
./build/identity bad > build/identity.txt 2> build/identity.err
cat build/identity.err
```

Record both the command's result and the resulting files. Do not infer a program's success solely from the absence of visible terminal output. Redirection has changed where evidence appears.

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
