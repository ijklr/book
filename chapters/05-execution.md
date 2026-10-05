# Chapter 5 — Starting a program and collecting its result

Imagine a build helper that runs a compiler and then reports whether compilation succeeded. It receives a command and separate arguments, including filenames containing spaces. The helper must survive the compiler's execution, preserve the arguments, and distinguish an ordinary nonzero exit from termination by a signal. Its first version runs one command at a time.

Before choosing functions, identify the required separation. The requested program needs an execution context. The helper needs its own continuing context to observe the result. Replacing the helper's program would satisfy “run the compiler” but lose the observer. Our requirements therefore call for another process, or a service that provides equivalent supervision. They do not imply that every operating system must expose the same process-creation API.

## Define what the result means

Write a proposed interface before looking at the implementation:

```text
run COMMAND [ARG...]
input:   an existing vector of argument strings
output:  the child's inherited standard output and standard error
result:  the child's completion, translated into the runner's exit status
```

There are already two decisions to make. Does a command name search `PATH`, or must the caller supply a path? Does execution failure have a separate reporting channel, or share the small exit-status space with ordinary program results? Our first version searches `PATH` and uses conventional status values. Those choices make the tool convenient, while leaving some outcomes ambiguous.

Predict the result of running a nonexistent command, then running an existing program that deliberately exits with `127`. If both use the same status, what extra evidence could distinguish them? Keep that question open while deriving the lifecycle.

Before accepting “a launcher needs a child” as a rule, change one requirement. Suppose this helper only prepares arguments and hands control to a compiler; it has no later report, cleanup, or supervision to perform. What now requires the helper to survive? Nothing in that revised contract. Successful execution can replace it directly, removing a process and the helper's collection path. Execution failure still needs a defined result because replacement did not occur.

This is a useful first-principles question: **which requirement pays for each role?** In our original problem, observing and translating completion pays for the continuing parent. “Launchers usually fork” does not. Before optimizing creation or waiting, check whether the observer is required at all. If it is, retain it; if the contract changes, rederive the design. Removing necessary supervision would be a bug, while preserving unnecessary supervision would be an unexamined cost.

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

Our ownership invariant is: **every successfully created child has one designated collector**. Creation failure produces no such obligation. Waiting for a different child violates the assignment even if that other child also belongs to the process. A helper embedded in a larger application must coordinate collection and the application's `SIGCHLD` policy.

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

The child calls `execvp()`. The `v` interface accepts a null-terminated argument vector; the `p` variant searches `PATH` for a command without a slash. Success replaces the program image and never returns to this code. The PID remains the child's identity. Our existing `argv` already supplies a terminating null pointer, so `&argv[1]` preserves the command's argument boundaries. [The `exec` family](https://www.man7.org/linux/man-pages/man3/execlp.3.html)

The invoking shell has already handled quoting. In `build/run printf '%s\n' 'two words'`, the final argument is one string. Concatenating the vector into a command string would create a new parsing problem. `execvp()` also has a historical shell fallback for certain executable text files; the example should not be treated as a hardened launcher for untrusted executable paths.

Now attack a tempting implementation: “Call execution in the child, then let the function finish normally.” Execution returns only on failure, making that fall-through precisely the exceptional case:

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

The reference implementation flushes parent streams before creation, saves execution's `errno` immediately, and uses a fixed child diagnostic through `write()`. That helper handles partial and interrupted writes; an unsuccessful diagnostic attempt does not justify reporting successful execution.

:::reveal Reference implementation

{{include:examples/run.c}}

:::

The parent retries `waitpid(child, &status, 0)` after `EINTR`. It then asks which kind of result it received before extracting any field. `WEXITSTATUS` is meaningful after `WIFEXITED`; `WTERMSIG` is used after `WIFSIGNALED`. The integer returned through `status` is not itself the child's ordinary exit code. [Wait-status interpretation](https://man7.org/linux/man-pages/man2/waitpid.2.html)

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
