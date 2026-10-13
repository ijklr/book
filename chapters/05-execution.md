# Chapter 5 — Starting a program and collecting its result

Imagine a build helper that runs a compiler and then reports whether compilation succeeded. It receives a command and separate arguments, including filenames containing spaces. The helper must survive the compiler's execution, preserve the arguments, and distinguish an ordinary nonzero exit from termination by a signal. Its first version runs one command at a time.

Before choosing functions, identify the two roles. The compiler must run, and the helper must remain available to observe its result. Replacing the helper with the compiler would lose that observer. Our requirements therefore call for another process, or a service that provides equivalent supervision.

A **process** is a running instance with its own identity and resources. A **program image** is the code and data it is currently executing. Creating a process and replacing its program image are separate operations: `fork()` does the first; the `exec` family does the second. This distinction lets the helper remain one process while its child becomes the compiler.

## Define what the result means

Write a proposed interface before looking at the implementation:

```text
run COMMAND [ARG...]
input:   an existing vector of argument strings
output:  the child's inherited standard output and standard error
result:  the child's completion, translated into the runner's exit status
```

There are already two decisions to make. Does a command name search `PATH`, or must the caller supply a path? How do we report failure to start the requested program, as distinct from a program that starts and then reports failure? We could use a separate reporting channel, or share the small exit-status space with ordinary program results. Our first version searches `PATH` and uses conventional status values. Those choices make the tool convenient, while leaving some outcomes ambiguous.

Predict the result of running a nonexistent command, then running an existing program that deliberately exits with `127`. If both use the same status, what extra evidence could distinguish them? Keep that question open while deriving the lifecycle.

Before accepting “a launcher needs a child” as a rule, change one requirement. Suppose this helper only prepares arguments and hands control to a compiler; it has no later report, cleanup, or supervision to perform. The helper no longer needs to survive. Successful program replacement removes the need to create a child or collect its result. A failed replacement still needs a defined error path because the helper remains in control.

Ask **which requirement makes each role necessary**. Here, observing and translating completion requires the continuing parent. If that requirement disappears, direct program replacement becomes sufficient. The process structure follows from the work the helper still owes its caller.

## Derive the minimum state

The parent needs a child identifier while it owes a collection operation, and an encoded result after collection. It does not need a copy of the child's heap or a polling counter. The important state transitions are:

```text
no child
   | successful creation
   v
child exists; parent owes collection
   | child terminates
   v
completion available; parent still owes collection
   | successful wait for this child
   v
completion collected; parent can report it
```

Termination and collection are separate events. A child can finish before its parent begins waiting. The operating system retains completion information so that scheduling order does not erase the result. Under ordinary child-status handling, an exited, uncollected child is called a zombie; it is no longer executing, but its bookkeeping remains. [Waiting for children](https://man7.org/linux/man-pages/man2/waitpid.2.html)

Our ownership invariant is: **every successfully created child has one designated collector**. Creation failure produces no such obligation. The collector uses the child's process ID (PID) to wait for that child. A wait that accepts any child could collect a result another component expects, so a helper embedded in a larger application must coordinate collection and the application's `SIGCHLD` policy.

The progress claim is conditional: if the child eventually terminates, the parent gets execution time, and collection succeeds, the helper can finish. No waiting API can make an arbitrary child complete useful work. A timeout would be a new policy, requiring decisions about cancellation, descendants, and partial results.

## Separate contracts from our model

The interfaces supply facts that the algorithm relies on. Our surrounding assumptions and reporting policies supply different kinds of facts:

| Kind of statement | Statement used here |
| --- | --- |
| POSIX contract | `fork()` creates a child; its return values identify parent, child, or failure. Successful execution replaces the calling program image. |
| Model assumption | The launcher is single-threaded and uses ordinary child-status handling. The requested command and inherited environment are suitable for this caller. |
| Design choice | Search `PATH`, inherit standard streams, and translate signal termination to `128 + signal number`. |
| Observation | A particular child ran first, finished quickly, or printed a particular diagnostic on this machine. |

An observation about scheduling is not an ordering guarantee. An assumption about a trusted environment is not established by successful compilation. Marking these differences gives you something concrete to revisit when the launcher moves into another application.

## Choose between two viable launch designs

One design uses `fork()` to create a child, changes its setup, and calls an execution function. Another uses `posix_spawn()` or `posix_spawnp()` to describe supported setup actions and request creation and execution together. Both can preserve arguments and leave the parent available to wait.

| Design | What it makes convenient | What must be considered |
| --- | --- | --- |
| Separate creation and execution | Explicit child setup and a visible lifecycle, useful for understanding descriptor redirection. | Every child failure path must terminate correctly; threaded parents impose strict restrictions. |
| `posix_spawnp` with attributes and file actions | Common launch operations expressed before creation, including argument vectors and supported descriptor actions. | The setup vocabulary is constrained; creation/setup failure and eventual child completion still need handling. |

The spawn interface is a serious implementation option, especially when its supported actions match the requirement. Our example chooses separate steps because the next chapter changes descriptor ownership between creation and execution. This is a teaching and flexibility decision, not a claim about universal performance. [The spawn interfaces](https://man7.org/linux/man-pages/man3/posix_spawn.3.html)

## Map the lifecycle onto POSIX

`fork()` returns the child's positive PID in the parent, zero in the child, and `-1` on failure. Both successful branches continue after the call. Ordinary private variables initially have equal values, but later modifications are independent. Inherited descriptors instead refer to the same underlying open file descriptions, so an inherited regular-file offset can be shared. Copying a handle and copying its resource are different operations. [POSIX `fork` specification](https://man7.org/linux/man-pages/man3/fork.3p.html)

### Follow one variable and one file offset

Suppose the helper has an ordinary local `int value = 7` and one descriptor for a stable file containing `ABCDE`. It opened that file once, and the current offset is zero. The parent forks, then waits while the child changes `value` to `99`, reads exactly two bytes using `read()`, closes its descriptor, and exits. Assume these operations succeed; there are no buffered C streams or other users of this file description.

| Event | Parent's `value` | Child's `value` | Shared file offset |
| --- | --- | --- | --- |
| Immediately after the fork | `7` | `7` | `0` |
| Child assigns `99` | `7` | `99` | `0` |
| Child reads `AB` | `7` | `99` | `2` |
| Child closes and exits; parent collects it | `7` | No longer alive | `2` |
| Parent reads one byte, obtaining `C` | `7` | No longer alive | `3` |

Waiting establishes the order of this experiment; it does not copy the child's variables back into the parent. The offset changes because both descriptors refer to one open file description. The child's `close()` removes its reference, while the parent's reference keeps the description usable. The descriptor number is just an integer in each process; its referent has a different sharing rule from an ordinary local variable.

**Predict:** the child now calls `lseek(fd, 0, SEEK_SET)` after reading `AB`, then closes and exits. Which byte does the parent read after collecting it? Would assigning zero to a separate local variable named `position` have the same effect?

:::reveal Review the offset trace

The parent's read obtains `A`: the successful seek changes the shared open file description's offset back to zero. Assigning to `position` changes only the child's private variable and leaves the offset at two. To obtain independent sequential offsets, the processes need separate open file descriptions; `dup()` alone would not provide them. Alternatively, a positioned read can request an explicit offset without advancing the shared one. [POSIX positioned reads](https://pubs.opengroup.org/onlinepubs/9799919799/functions/read.html)

:::

### Replace the child's program image

The child calls `execvp()`. The `v` interface accepts a null-terminated argument vector; the `p` variant searches `PATH` for a command without a slash. Success replaces the program image and never returns to this code. The PID remains the child's identity. Our existing `argv` already supplies a terminating null pointer, so `&argv[1]` preserves the command's argument boundaries. [The `exec` family](https://www.man7.org/linux/man-pages/man3/execlp.3.html)

The invoking shell has already handled quoting. In `build/run printf '%s\n' 'two words'`, the final argument is one string. Concatenating the vector into a command string would create a new parsing problem. `execvp()` also has a historical shell fallback for certain executable text files; the example should not be treated as a hardened launcher for untrusted executable paths.

For that invocation, the executed program receives the vector below. Quotes are shell syntax and are already gone; the backslash and `n` remain two characters in the format string for `printf` to interpret.

```text
argv[0] -> "printf"
argv[1] -> "%s\n"
argv[2] -> "two words"
argv[3] -> NULL
```

Here the double quotes delimit the strings in the drawing; they are not argument bytes.

Now attack a tempting implementation: “Call `execvp()` in the child, then let the function finish normally.” `execvp()` returns only on failure, making that fall-through precisely the exceptional case:

```text
child attempts execution
execution fails
child falls into normal success return
parent collects exit status zero
caller concludes its requested program succeeded
```

This violates the result contract without crashing. The child must turn execution failure into an explicit failure result and stop. It uses `_exit()` rather than normal C shutdown. Otherwise, copied parent buffers and inherited exit callbacks can be acted on by the wrong process. If buffered text existed before creation and both branches flushed it, one logical message could appear twice.

## Design your branches before revealing the source

Sketch three creation outcomes and two child execution outcomes. For each arrow, identify who continues and who still owes collection. Then choose what happens if a wait is interrupted: does the child's existence change merely because the parent's wait returned early?

The reference implementation flushes parent streams before creation, saves `errno` immediately if `execvp()` fails, and writes a fixed diagnostic from the child. Its `write()` helper handles partial and interrupted writes. Even if that diagnostic cannot be delivered, the child must still report failure through its exit status.

:::reveal Reference implementation

{{include:examples/run.c}}

:::

The parent retries `waitpid(child, &status, 0)` after `EINTR`. Interruption ends that wait attempt; it does not collect the child or cancel the obligation. Keep three values distinct:

| Value | Meaning |
| --- | --- |
| Successful return from `waitpid` | The PID of the child whose result was collected. |
| `status`, written through the second argument | An encoded description of how the child changed state. |
| `WEXITSTATUS(status)` | The child's exit value, valid only when `WIFEXITED(status)` is true. |

For signal termination, first check `WIFSIGNALED(status)`, then use `WTERMSIG(status)`. With the options used here, ordinary child handling returns a termination result; this runner does not request reports of job-control stops. Only the low eight bits of a normal exit value are available through `WEXITSTATUS`, so the status channel cannot carry an arbitrary C integer. [POSIX wait-status interpretation](https://pubs.opengroup.org/onlinepubs/9699919799/functions/wait.html)

The runner distinguishes these cases internally, but translating a signal to `128 + signal number` loses that distinction in its own exit status: a child can exit normally with the same numeric value. The signal diagnostic supplies additional evidence for a person. A caller needing an unambiguous machine-readable termination kind needs a separate structured result, rather than interpreting the runner's exit value alone.

Review both safety and progress. The child never falls into the collector's role, arguments keep their boundaries, and status fields are decoded under the right conditions. For progress, execution failure ends the child and interruption of waiting does not abandon it. A child that never terminates still prevents completion. A diagnostic destination that blocks is another environmental dependency; this compact runner is not a bounded-time supervisor.

The single-threaded assumption also carries real weight. After a multithreaded parent forks, the child retains only the calling thread while copied memory can contain locks held by vanished threads. Before execution, it must stay within documented async-signal-safe operations. `execvp()` does not carry the same portable guarantee as `execve()` for that restricted interval. Reusing this implementation in a threaded program requires redesign, often toward spawning with supported actions. [Post-fork restrictions](https://man7.org/linux/man-pages/man2/fork.2.html)

## Test predictions against observations

From the project directory, predict each output and status before running:

```sh
make build/run
build/run printf '%s\n' 'two words'
build/run sh -c 'exit 7'
printf 'runner status: %s\n' "$?"
build/run this-command-does-not-exist-for-the-book
printf 'runner status: %s\n' "$?"
```

Expect `two words`, then runner status `7`, then an execution-failure diagnostic and status `127`. Other execution failures use `126`; usage errors use `2`. Save `$?` immediately because another command replaces it. These observations check the chosen reporting policy, not every possible launch failure.

The environment, working directory, and standard streams are inherited. Job control, resource limits, timeout handling, and explicit signal forwarding are outside this version. A terminal interrupt commonly reaches both processes through their foreground process group; a signal addressed solely to the parent's PID need not reach the child. State the addressed entity before claiming that “the command received the signal.”

## Exercises

1. **Explain an apparent success.** A launcher returns raw wait status to its caller. Its child exits with `23`. Identify the broken abstraction and construct a check that catches it without assuming a particular numeric encoding.
2. **Improve the evidence.** Design an extra pipe that reports an execution error number, with its child write end marked close-on-exec. Explain what data, EOF, and an unexpectedly dead child each allow the parent to conclude.
3. **Transfer: change the host.** Your launcher now lives in a program with worker threads. Compare a spawn-based design with a restricted fork/exec design. State what setup must move before creation and which guarantees you must verify.

**Write your version.** Explain the launcher from its obligations rather than its function names. Include one safety invariant, one conditional progress claim, and one reporting ambiguity you deliberately retained.

## Selected answers

:::reveal Review after your attempt

1. Raw wait status is an encoded result from a different interface. The test should require `WIFEXITED` and extracted status `23`, then require the runner itself to exit with `23`. Relying on a guessed bit layout would merely replace one abstraction leak with another.
2. An error record can identify a failed execution attempt; EOF after close-on-exec can indicate successful replacement. However, a child that dies before reporting also closes its descriptors. Combine the protocol with collected status and explicitly represent uncertainty rather than claiming EOF alone proves success. The added channel gives better evidence at the cost of more ownership and partial-I/O handling.
3. Spawn actions can express common descriptor and signal setup while avoiding application-defined child code in the restricted interval. Separate fork/exec remains viable when required setup fits the async-signal-safe rules; paths, argument storage, and other preparation should be settled beforehand. Neither approach removes the obligation to collect completion or define inherited signal policy. Choose by required setup and documented guarantees, then measure performance if it matters.

:::
