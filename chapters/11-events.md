# Chapter 11 — Deriving an event loop from stalled work

Suppose two peers have work in progress. One has sent half a request and pauses; the other has sent a complete request. We want the second to progress without waiting for the first. We also require bounded memory, preservation of every accepted byte, and an explicit way to retire completed or failed operations.

The central problem is unfinished work. We need to remember enough about an operation to pause it safely, attend to something else, and resume it later. An event loop organizes these pauses: it waits for useful events, advances the corresponding operations, and retains their unfinished state. This chapter derives those state and scheduling rules through a finite pipe experiment, then applies the argument to sockets.

## Compare where waiting can live

One thread per connection lets ordinary blocking calls preserve progress on each thread's stack. This can be a reasonable, clear design at an appropriate scale, with bounds on thread count and resource use. One process per connection adds stronger address-space separation and a different resource cost. A single event-loop thread keeps operation state in explicit objects and centralizes waiting, avoiding a separate blocked thread for each connection while making control flow more explicit.

We choose a single event-loop thread for this experiment. Both threads and event loops can let one connection progress while another waits. Their latency, throughput, resource cost, and implementation complexity depend on the workload; the choice here lets us examine the unfinished state directly.

Here is the naive event loop: wait until some descriptor is readable, then call Chapter 10's blocking `read_full` for that connection. Before reading the review, trace what happens if one header byte is available and the other three are delayed. State whether placing a timeout on the readiness wait fixes the problem.

:::reveal Review the stalled loop

Readiness allows the first byte to be read. `read_full` then waits for the remaining bytes and prevents the dispatcher from visiting other connections. A timeout on the earlier readiness wait cannot bound a later blocking call. The operation handler must be able to return with incomplete work still represented somewhere.

:::

## Derive the three kinds of result

A resumable transfer needs to distinguish progress, temporary inability to progress, and final completion or failure. For a read into an unfinished header or body, the decisions are:

| Read result | State change | Next action |
| --- | --- | --- |
| Positive count | Advance the received-byte count | Process the completed field or continue within this turn's budget |
| `-1`, `EAGAIN` or `EWOULDBLOCK` | Preserve progress | Return to the dispatcher and wait for readiness |
| `-1`, `EINTR` | Preserve progress | Retry, subject to cancellation and deadline policy |
| Zero, after requesting at least one byte | Record the end of input | Apply the protocol's EOF rule for this phase |
| `-1`, other error | Record failure | Retire the operation and release its resources |

Inspect `errno` only after a failure result. An interrupted read that already transferred bytes returns the positive count, which must be consumed once. [POSIX read results](https://man7.org/linux/man-pages/man3/read.3p.html)

POSIX supplies nonblocking operation through `O_NONBLOCK`. A read or write that would wait can return `EAGAIN` or `EWOULDBLOCK`; portable code checks both. These are interface contracts, not guesses based on how fast a previous call returned. To preserve existing file status flags, fetch them with `F_GETFL` and combine them with `O_NONBLOCK` when calling `F_SETFL`. Duplicated and inherited descriptors may share those status flags through the same open file description. [File-status flag contract](https://man7.org/linux/man-pages/man2/F_GETFL.2const.html)

Now we need a way to avoid repeatedly trying an operation that cannot progress. `poll` accepts descriptors and desired events, then reports readiness through returned event bits. `POLLIN` concerns reading; `POLLOUT` concerns writing. Readiness does not promise that an entire application frame can transfer. We still use nonblocking calls and interpret their results. [POSIX poll contract](https://man7.org/linux/man-pages/man3/poll.3p.html)

Keep the three decisions separate. `poll` reports a reason to try an operation. The read or write result tells us whether bytes moved, the operation would block, or an end or error occurred. The protocol then interprets that result in its current phase. For example, EOF before a header and EOF halfway through one have different meanings even though both reads return zero.

## Derive the persistent state

A framed connection needs a current phase and enough progress information to restart that phase:

| Phase | Information that must survive a handler return | Condition for advancing |
| --- | --- | --- |
| Header | Four-byte storage and bytes collected | Header complete and length valid |
| Body | Validated length, storage, bytes collected | Declared body fully received |
| Reply | Output bytes and bytes already accepted | No output remains |
| Closing | Failure or completion reason and owned resources | Cleanup finishes |

For an output buffer containing `n` bytes, maintain `0 <= sent <= n`. Here `n` counts all bytes in that buffer, including framing bytes if the header and body are stored together. Only a positive write count changes `sent`; a temporary failure leaves it unchanged. A function-local offset that is discarded on return cannot satisfy this requirement. Neither can reusing the input buffer for the next request while the pending reply still refers to its bytes.

Suppose a socket write is asked to send 100 bytes, accepts 37, and the next attempt returns `EAGAIN`. Commit to the next buffer address and length before continuing: they are the original buffer plus 37 and 63 bytes. Returning to the dispatcher changes scheduling, not ownership of the remaining bytes.

Interest in events is itself derived state. Request `POLLOUT` while output remains. Remove that interest once the queue is empty. A socket may stay writable while we have nothing to send; continuously requesting write readiness can make the loop wake immediately and do no useful work.

## A smaller experiment with the same obligations

Our executable uses a pipe and two processes so we can inspect progress without also implementing connection management. It exercises the transfer rules for one stream; the two-client dispatcher remains an exercise. The child generates exactly 65,536 bytes, with byte `i` equal to `i % 251`. The parent counts bytes and adds their values. Both pipe ends are nonblocking; buffers remain fixed in size.

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

The writer regenerates each chunk from the total `sent`, so the next chunk begins at the correct position even after a short write. The consumer sums only the bytes actually returned and rejects a count beyond the planned total. Success also requires normal, successful child termination. To check the expected sum independently, divide the sequence into 261 complete cycles of bytes 0 through 250 and a final cycle of bytes 0 through 24: `261 × 31375 + 300 = 8189175`.

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

Let `q` be reply bytes waiting to be written, `a` bytes added to that queue, and `d` bytes removed after positive write results. Across one transition, the accounting model is `q_next = q + a - d`. Here, removing bytes means the local write accepted them; it does not prove the peer application processed them. If additions repeatedly exceed removals, the queue grows. Faster allocation cannot change that relationship.

For a fixed memory budget `B`, the invariant is `0 <= q <= B`. Before accepting work, reserve enough capacity for the output it can require. When that capacity is unavailable, the design must defer input, reject work according to its protocol, or release other capacity. Unlimited buffering is incompatible with the stated bound.

One policy pauses reading when queued output reaches a chosen upper threshold and resumes below a lower threshold. The gap avoids repeatedly pausing and resuming near a single boundary. Another policy allows only one outstanding request per connection. That simplifies ownership and bounds but limits pipelining. Both policies still need the capacity reservation above: a threshold alone does not bound the output of work already accepted.

Per-connection bounds do not establish a global bound when connection count is unlimited. Account for the number of connections, each connection's fixed storage, and all queued bytes. Also consider dependency cycles: if two peers stop reading until their own pending writes finish, both can stall. Backpressure needs a protocol in which capacity can eventually be released.

Put numbers into that model before tuning the allocator. Suppose an initially empty reply queue has 64 KiB of capacity, accepted work produces 48 KiB of reply bytes per second, and sending completes 16 KiB per second. Under these sustained rates, the queue fills in two seconds: `64 / (48 - 16)`. Doubling capacity buys two more seconds; it does not permit those rates indefinitely. A bigger buffer could absorb a short burst, but sustained excess requires changing acceptance or completion rates.

Now account for unfinished requests and storage that a partial write cannot release. Suppose each request produces exactly 4 KiB of output, including framing, in one buffer retained until its final byte is sent. The reply-buffer budget is 12 KiB. Let `r` be capacity reserved for unfinished requests and `b` be capacity held by completed reply buffers. The pending-byte count `q` can be smaller than `b`: sending half a reply leaves its entire buffer allocated.

Admission must preserve `b + r <= 12 KiB`. Completion transfers a four-KiB charge from `r` to `b`. A partial write decreases only `q`; finishing and releasing a buffer also decreases `b`. In this table, uncommitted capacity is `12 KiB - b - r`:

| Event | Pending `q` | Buffers `b` | Reserved `r` | Uncommitted capacity |
| --- | --- | --- | --- | --- |
| Admit A | 0 KiB | 0 KiB | 4 KiB | 8 KiB |
| Admit B | 0 KiB | 0 KiB | 8 KiB | 4 KiB |
| A completes | 4 KiB | 4 KiB | 4 KiB | 4 KiB |
| Admit C | 4 KiB | 4 KiB | 8 KiB | 0 KiB |
| Write 2 KiB of A's reply | 2 KiB | 4 KiB | 8 KiB | 0 KiB |
| B completes | 6 KiB | 8 KiB | 4 KiB | 0 KiB |
| Write A's final 2 KiB; release its buffer | 4 KiB | 4 KiB | 4 KiB | 4 KiB |

Before opening the review, identify the earliest row after admitting C at which D can be admitted. Does B's completion release capacity? What would go wrong if admission checked only `q`?

:::reveal Review the reservation trace

D can enter after the last row, when A's buffer is released. Its earlier partial write made progress without freeing reusable storage. B's completion also releases nothing: its reservation becomes a buffer charge. Checking only `q` misses both unfinished requests' future output and storage retained for bytes already sent. The table bounds reply byte-array capacity under this allocation policy; allocator overhead, input buffers, and worker state need separate accounting. If replies have variable size, reserve a justified upper bound or use a protocol that lets production pause as space becomes available.

:::

For the earlier rate example, the uncertain assumption is now specific: is the excess a bounded burst or an ongoing condition? Record queue growth during the relevant workload instead of starting with an allocator comparison. Separately, keep offering requests while a test peer pauses its reading until capacity is exhausted, then resume reading. Predict when input should pause and resume, and verify the memory bound and accepted bytes. For this sustained-load problem, a faster dispatcher helps only if it addresses the limiting work identified by the model.

## Separate safety from fairness and deadlines

The offsets and bounds protect data and memory. Progress additionally depends on giving operations opportunities to run. Draining one busy socket until `EAGAIN` can monopolize the loop if new input keeps arriving. Limit bytes or operations per connection per pass when many connections share the dispatcher. Our pipe experiment has one producer and a fixed total, so the successful byte-processing work is bounded by that total; retries and waits need separate reasoning.

For example, A has 8 KiB ready and B has 1 KiB. With a 1 KiB read budget per visit and both descriptors visited each pass, B gets a turn after at most 1 KiB is processed from A. Draining A first would put 8 KiB ahead of B, or indefinitely more if A keeps receiving. The budget bounds work ahead of B only if each byte's processing and each handler's other work are also bounded.

The sample's five-second timeout is per `poll` wait. An `EINTR` retry restarts that interval; repeated progress starts new waits too. The final `waitpid` and output flush also have no timeout. The five-second value therefore does not bound process completion. A request deadline can be represented as an expiration on a monotonic clock, with remaining time recalculated before each wait. Handling expiry is another state transition, with its own rules for cancelling work and releasing resources.

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
