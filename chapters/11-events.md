# Chapter 11 — Deriving an event loop from stalled work

Suppose two peers have work in progress. One has sent half a request and pauses; the other has sent a complete request. We want the second to progress without waiting for the first. We also require bounded memory, preservation of every accepted byte, and an explicit way to retire completed or failed operations.

The central problem is unfinished work. We need to remember enough about an operation to pause it safely, attend to something else, and resume it later. An event loop is one architecture for meeting that need. This chapter derives its state and scheduling rules, then studies them in a finite pipe experiment before applying the argument to sockets.

## Compare where waiting can live

One thread per connection lets ordinary blocking calls preserve progress on each thread's stack. This can be a reasonable, clear design at an appropriate scale, with bounds on thread count and resource use. One process per connection adds stronger address-space separation and a different resource cost. A single event-loop thread keeps operation state in explicit objects and centralizes waiting, avoiding a separate blocked thread for each connection while making control flow more explicit.

Our design choice is the third architecture. It is not a claim that one thread is universally faster. The application requirement is independence of progress; threads and event loops can both satisfy it under suitable assumptions. Actual latency, throughput, and complexity must be assessed for the workload.

Here is the naive event loop: wait until some descriptor is readable, then call Chapter 10's blocking `read_full` for that connection. Before reading the review, trace what happens if one header byte is available and the other three are delayed. State whether placing a timeout on the readiness wait fixes the problem.

:::reveal Review the stalled loop

Readiness allows the first byte to be read. `read_full` then waits for the remaining bytes and prevents the dispatcher from visiting other connections. A timeout on the earlier readiness wait cannot bound a later blocking call. The operation handler must be able to return with incomplete work still represented somewhere.

:::

## Derive the three kinds of result

A resumable transfer needs to distinguish progress, temporary inability to progress, and final completion or failure. A positive byte count advances state. A temporary result preserves it for later. EOF or a fatal error invokes the protocol's completion or failure rules.

POSIX supplies nonblocking operation through `O_NONBLOCK`. A read or write that would wait can return `EAGAIN` or `EWOULDBLOCK`; portable code checks both. These are interface contracts, not guesses based on how fast a previous call returned. To preserve existing file status flags, fetch them with `F_GETFL` and combine them with `O_NONBLOCK` when calling `F_SETFL`. Duplicated and inherited descriptors may share those status flags through the same open file description. [File-status flag contract](https://man7.org/linux/man-pages/man2/F_GETFL.2const.html)

Now we need a way to avoid repeatedly trying an operation that cannot progress. `poll` accepts descriptors and desired events, then reports readiness through returned event bits. `POLLIN` concerns reading; `POLLOUT` concerns writing. Readiness does not promise that an entire application frame can transfer. We still use nonblocking calls and interpret their results. [POSIX poll contract](https://man7.org/linux/man-pages/man3/poll.3p.html)

Notice the division of responsibility: `poll` decides when trying is useful; the transfer result determines what happened; the protocol decides what that result means. Confusing these layers is how “readable” becomes the false claim “a complete request exists.”

## Derive the persistent state

A framed connection needs a current phase and enough progress information to restart that phase:

| Phase | Information that must survive a handler return | Condition for advancing |
| --- | --- | --- |
| Header | Four-byte storage and bytes collected | Header complete and length valid |
| Body | Validated length, storage, bytes collected | Declared body fully received |
| Reply | Output bytes and bytes already accepted | No output remains |
| Closing | Failure or completion reason and owned resources | Cleanup finishes |

For a reply of length `n`, maintain `0 <= sent <= n`. Only a positive write count changes `sent`; a temporary failure leaves it unchanged. A function-local offset that is discarded on return cannot satisfy this requirement. Neither can reusing the input buffer for the next request while the pending reply still refers to its bytes.

Suppose a socket write is asked to send 100 bytes, accepts 37, and the next attempt returns `EAGAIN`. Commit to the next buffer address and length before continuing: they are the original buffer plus 37 and 63 bytes. Returning to the dispatcher changes scheduling, not ownership of the remaining bytes.

Interest in events is itself derived state. Request `POLLOUT` while output remains. Remove that interest once the queue is empty. A socket may stay writable while we have nothing to send; continuously requesting write readiness can make the loop wake immediately and do no useful work.

## A smaller experiment with the same obligations

Our executable uses a pipe and two processes so we can inspect progress without also implementing connection management. The child generates exactly 65,536 bytes, with byte `i` equal to `i % 251`. The parent counts bytes and adds their values. Both pipe ends are nonblocking; buffers remain fixed in size.

The writer's state is `sent`, and its invariant is that the accepted prefix matches the specified sequence. The reader's state is a count and sum, with `count <= TOTAL_BYTES`. There is a separate lifecycle obligation: close unused pipe ends and reap the child. A stream ending is insufficient evidence that the producer completed successfully.

Before opening the code, sketch how the writer resumes after `EAGAIN` and how the reader recognizes final EOF. Include what happens if the child exits with failure after sending only a prefix.

:::reveal Reference implementation

{{include:examples/pollpipe.c}}

:::

Build and run from the book directory:

```sh
make build/pollpipe
build/pollpipe
```

The expected successful output is:

```text
bytes=65536 checksum=8189175
```

The writer regenerates each chunk from the total `sent`, so the next chunk begins at the correct position even after a short write. The consumer sums only the bytes actually returned and rejects a count beyond the planned total. Success also requires normal, successful child termination.

The compact example attempts unused-end closes once without inspecting their results; it checks the final closes of the active data ends. Its progress argument assumes the unused ends were released. It does not establish that every cleanup failure is detected. A production variant needs an explicit platform-specific close policy, especially where an interrupted close can leave ownership uncertain.

The count and additive checksum are observations with limits: they do not prove byte-for-byte order or uniqueness. Different byte sequences can have the same length and sum. The sequence-preservation argument comes from the producer and transfer invariants; a stronger test could compare every received byte with its expected value.

The pipe need not fill on every execution. Therefore this output establishes the observed result, not that an `EAGAIN` path necessarily ran. To test that path deliberately, control the reader's progress or instrument a test version to report it. Merely increasing the transfer size is a hypothesis about exercising the path, not proof that it did.

## Derive shutdown from remaining obligations

The writer has closed its end. The readiness result contains `POLLHUP`. Predict whether the reader can immediately close its descriptor. Your answer must account for bytes accepted by the pipe before the writer closed.

:::reveal Review the hangup trace

Accepted bytes may remain buffered. Closing immediately can discard them. The sample drains on either `POLLIN` or `POLLHUP` and treats a positive-length `read` returning zero as EOF. Hangup indicates a changed peer lifecycle, while reading establishes whether buffered work remains. [poll hangup semantics](https://man7.org/linux/man-pages/man2/poll.2.html)

:::

The opposite failure is equally revealing: if the parent accidentally retains an inherited write end, EOF may never arrive after the child finishes. Readiness cannot compensate for an unintended owner. Descriptor lifetime is part of the protocol's progress argument.

`POLLERR` and `POLLNVAL` must also be interpreted. The pipe example treats them as failures. An invalid descriptor points toward lifetime or bookkeeping trouble. A socket implementation would relate an error event to the connection's state and inspect relevant socket errors before cleanup. Cleanup itself needs a single owner that removes the descriptor from the event set before freeing its connection object.

## Derive backpressure from conservation

Let `q` be queued reply bytes, `a` newly accepted reply bytes, and `d` bytes successfully sent. Across one transition, the accounting model is `q_next = q + a - d`. If accepted work repeatedly exceeds completed work, the queue grows. Faster allocation cannot change that relationship.

For a fixed memory budget `B`, the invariant is `0 <= q <= B`. Before accepting work, reserve enough capacity for the output it can require. When that capacity is unavailable, the design must defer input, reject work according to its protocol, or release other capacity. Unlimited buffering is incompatible with the stated bound.

One policy pauses reading at a high-water mark and resumes below a lower threshold. The gap reduces repeated toggling near one boundary. Another policy allows only one outstanding request per connection. That simplifies ownership and bounds but limits pipelining. Both are plausible choices; the required invariant does not dictate one exact threshold.

Per-connection bounds do not establish a global bound when connection count is unlimited. Account for the number of connections, each connection's fixed storage, and all queued bytes. Also consider dependency cycles: if two peers stop reading until their own pending writes finish, both can stall. Backpressure needs a protocol in which capacity can eventually be released.

Put numbers into that model before tuning the allocator. Suppose an initially empty reply queue has 64 KiB of capacity, accepted work produces 48 KiB of reply bytes per second, and sending completes 16 KiB per second. Under these sustained rates, the queue fills in two seconds: `64 / (48 - 16)`. Doubling capacity buys two more seconds; it does not permit those rates indefinitely. A bigger buffer could absorb a short burst, but sustained excess requires changing acceptance or completion rates.

The uncertain assumption is now specific: is the excess a bounded burst or an ongoing condition? Record queue growth during the relevant workload instead of starting with an allocator comparison. Separately, keep offering requests while a test peer pauses its reading until capacity is exhausted, then resume reading. Predict when input should pause and resume, and verify the memory bound and accepted bytes. For this sustained-load problem, a faster dispatcher helps only if it addresses the limiting work identified by the model.

## Separate safety from fairness and deadlines

The offsets and bounds protect data and memory. Progress additionally depends on giving operations opportunities to run. Draining one busy socket until `EAGAIN` can monopolize the loop if new input keeps arriving. Limit bytes or operations per connection per pass when many connections share the dispatcher. Our pipe experiment has one producer and a fixed total, so its drain loop is bounded by that total.

The sample's five-second timeout is per wait. An `EINTR` retry restarts that interval; repeated progress starts new waits too. It is not an absolute deadline for the process. A real request deadline can be represented as an expiration on a monotonic clock, with remaining time recalculated before each wait. Handling expiry is another state transition.

Regular files generally report ready; a subsequent storage operation may still incur latency. Lengthy computation, synchronous file I/O, or a blocking log destination can therefore delay every connection on an event thread. “Uses nonblocking sockets” is narrower than “every handler has bounded execution time.”

## Transfer the scheduler

1. Change the read buffer to 17 bytes. Predict the total and checksum, then identify which costs might change without asserting a measured speed difference.
2. Change one assumption: the producer has an unbounded stream and the parent must service a second stream too. Which drain-loop rule is now insufficient? State a fairness policy and the assumptions under which it gives the second stream opportunities.
3. Design a nonblocking version of Chapter 10 for two clients. Draw its state object, output capacity rule, deadline, and cleanup ownership. Make one client pause halfway through a header and require the other to complete within a test timeout.

:::reveal Review after your attempt

For exercise 1, the count and checksum stay the same. Smaller transfers may increase call and loop overhead, but the actual time difference is a measurement question.

For exercise 2, draining until no data is available no longer provides a finite bound if input continually arrives. A per-pass byte or operation budget lets the dispatcher revisit the other descriptor, assuming handlers themselves return and the event thread is scheduled. That is an opportunity guarantee under assumptions, not a universal latency bound.

For exercise 3, save partial header, body, and reply progress across dispatches. Reserve reply capacity before accepting more work. The two-client test directly challenges head-of-line blocking by a partial request; an aggregate throughput test might hide that failure.

:::

## Write your version

Write a transition table that includes positive progress, temporary unavailability, EOF, fatal error, and deadline expiry. Beside each transition, record ownership and capacity changes. Then write two independent arguments: accepted bytes are preserved, and one stalled peer cannot indefinitely occupy the dispatcher. If either argument relies only on a successful run, identify the missing invariant or assumption.

For a compute-service variation, the network loop submits bounded jobs to workers and later receives completions. Socket readiness now says nothing about available worker capacity. Account separately for queued input, running work, and completed output. Before adding batching, predict how waiting to form a batch consumes a request's remaining deadline. A disconnect must also leave someone responsible for a job still using its resources. The local scheduler in [Appendix F](#appendix-f--from-c-systems-to-ai-infrastructure) lets you study these transitions with a simulated clock before introducing model execution or accelerators.
