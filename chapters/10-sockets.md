# Chapter 10 — Deriving a protocol from incomplete information

We want two local processes to exchange a request and a verified reply. The request may contain arbitrary bytes, has a maximum size of 4096 bytes, and must be returned unchanged. The receiver must reject an incomplete request rather than silently accepting a prefix. Neither side should allocate memory proportional to an unchecked value supplied by the other.

These are application requirements. They do not yet prescribe TCP, a header, or a C function. Our chosen experiment uses one TCP connection on IPv4 loopback, handles at most one request, and exits. The command-line client accepts a string argument, so it cannot originate embedded NUL bytes even though the wire format can represent them.

## What information is missing?

The relevant transport contract is an ordered byte stream. TCP does not preserve the boundaries of application writes. One write can require several reads; several writes can be returned together. A positive receive count tells us how many bytes arrived in that call. An orderly peer shutdown eventually yields a zero count after available data is consumed. [Socket interface](https://man7.org/linux/man-pages/man2/socket.2.html), [Receive behavior](https://man7.org/linux/man-pages/man2/recv.2.html)

Imagine a sender writes five bytes spelling `hello`. Before continuing, predict what a receiver that performs exactly one read into a 4096-byte array can conclude if the call returns two. Has the sender violated the protocol? Has a complete request arrived? What information would distinguish a short complete message from a prefix of a longer one?

:::reveal Review the missing information

The transport has not violated its contract. The read can return `he` while `llo` arrives later. Two bytes alone cannot distinguish a complete two-byte message from an incomplete five-byte message. The receiver needs an application rule for completion. Increasing its buffer cannot supply that missing rule.

:::

This is a reasoning pattern worth preserving: identify what the observer can know, then add exactly the information required by the application. An observed five-byte read during a local test does not strengthen the transport contract.

## Compare ways to identify completion

A delimiter, a declared length, and connection termination are plausible designs. Delimiters are convenient for text, but arbitrary bytes require escaping or a restricted alphabet. Termination can delimit one request if the client closes its sending direction and leaves the receiving direction available for the reply; it couples message completion to connection lifecycle. A declared length supports arbitrary bytes and can later support several frames on a connection, but introduces a size field that must be validated.

We choose a length prefix. That choice follows our binary-message and explicit-validation goals. The exact four-byte width and byte-order convention remain design choices; they are not mathematical consequences of networking.

| Field | Encoding | Rule |
| --- | --- | --- |
| Length | Four-byte unsigned integer, network byte order | Between zero and 4096 inclusive |
| Body | Exactly the declared number of bytes | No terminator or escaping |

The length excludes the header. An empty body is therefore represented by a complete four-byte header containing zero. It is different from a connection that sends no bytes at all.

Challenge the inherited width before treating it as necessary. How many distinct lengths must a fixed-width length field represent? Could one byte work? Could two? Commit your answer from the allowed values, without using a familiar protocol as the justification.

:::reveal Derive the information needed

Lengths zero through 4096 require 4097 distinct values. Twelve bits represent only 4096 values, so this field needs at least thirteen bits; two whole bytes suffice. Our four-byte field is a convenient convention, not a lower bound. Reducing it would save two bytes per frame while retaining length validation and partial-header state. Whether that saving matters depends on the workload. If every body were required to contain exactly 4096 bytes, a revised protocol could omit the length field entirely because the agreement already supplies that information. This would remove variable-length messages from the contract, so it requires a real change in need. Derive the information first, then choose its representation.

:::

Network byte order puts the most significant byte first. We use `htonl` to encode and `ntohl` to decode the fixed-width header. Sending an ordinary host integer's bytes without a specified encoding would depend on representation; sending an entire C structure would add layout and padding dependencies. The required property is agreement about the bytes, while these conversion functions implement our selected convention. [Byte-order conversions](https://man7.org/linux/man-pages/man3/byteorder.3.html)

## Derive the receiver's memory and states

Before the first byte, the receiver needs four bytes of header storage and a count of how much is present. It cannot interpret an incomplete header. After collecting four bytes, it needs a validated body length and a count of body bytes received. Only after that count reaches the declared length is the request complete.

The invariants are small enough to state directly: header bytes collected stay between zero and four; a validated length stays at most 4096; body bytes collected stay between zero and that length. Buffer addresses always refer to the unfilled suffix, and each positive transfer advances progress exactly once. The maximum is checked before reading the body, so even an enormous declared length cannot expand our storage requirement.

For blocking I/O, these states can live in a function's local variables and loop position. Chapter 11 will move them into longer-lived objects when a handler must return before completion. The logical state exists in either design, whether or not a `struct` names it.

Now classify EOF. Commit answers for four cases before opening the review: no header bytes, two header bytes, a complete header announcing five followed by two body bytes, and a complete zero-length header.

:::reveal Review the EOF cases

With no header bytes, no request started; our server chooses to close successfully without replying. Two header bytes mean a truncated header. Five promised body bytes followed by only two mean a truncated body. A zero-length header describes a complete request immediately and receives a zero-length reply. Completion depends on protocol position, not simply on whether `read` returned zero somewhere.

:::

## Derive the sender's obligation

The sender faces the same progress problem in reverse. A successful write can accept only a prefix of the requested bytes. Its invariant is that bytes before `offset` have been accepted by the local write operation and bytes at or after `offset` remain to be submitted. Retry an interrupted transfer without advancing; advance by the returned positive count; fail on other errors. Restarting from byte zero after a short write duplicates the prefix and corrupts the frame.

Both programs ignore `SIGPIPE`, allowing broken-connection writes to follow an error-return path instead of terminating through that signal. Successful writing does not prove that the peer application processed the request. The client therefore reads a reply and verifies both its length and its bytes. [Send interface and SIGPIPE](https://man7.org/linux/man-pages/man2/send.2.html)

This distinction matters when extending an echo protocol to operations with side effects. If the server performs an operation and the reply is lost, the client cannot infer from its read failure whether the operation happened. Retrying an echo is harmless; retrying a debit may require request identifiers and deduplication. A reliable stream does not, by itself, establish exactly-once application behavior.

## Choose the endpoint and its owners

Loopback confines the demonstration to the local machine. Binding port zero asks the kernel to choose an available port; `getsockname` retrieves it. Printing and flushing `PORT N` publishes the selected address to a supervising process. Merely calling `printf` would leave publication dependent on stdio buffering.

The listening descriptor owns the ability to accept a connection. `accept` produces a separate descriptor for that connection's stream. Both need cleanup. One connection is a deliberate bound on this experiment, not a claim that `listen(..., 1)` prevents every other connection attempt or defines a service-wide concurrency policy.

Before opening the implementations, sketch the server's resource ownership after socket creation, binding, accepting, and receiving a bad length. Your cleanup path must work from every reached state, including failure before a connection descriptor exists.

:::reveal Reference implementation: server

{{include:examples/echo_server.c}}

:::

:::reveal Reference implementation: client

{{include:examples/echo_client.c}}

:::

Check the two `read_full` contracts rather than assuming identical helper names imply identical semantics. The server needs to distinguish clean initial EOF from truncation. The client is expecting a reply, so missing required reply bytes are always failure. Both treat a request for zero bytes as already complete.

The client treats interrupted `connect` as failure and closes the socket. A connection attempt is not a byte-transfer loop whose remaining suffix is described by an offset. A more capable client would explicitly manage connection progress. Likewise, final `close` calls are attempted once; blindly retrying descriptor cleanup can act on a number whose underlying lifetime has changed.

## Run an observation with a stated boundary

Build from the book directory:

```sh
make build/echo_server build/echo_client
```

The following Python driver supervises the C programs. It gives the complete readiness record a deadline and applies separate timeouts to the client and server completion. Those checks are supervisor policies; they do not become internal server deadlines or a hard bound on operating-system process cleanup.

```sh
python3 - <<'PY'
import os
import select
import subprocess
import time

server = subprocess.Popen(["build/echo_server"], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
try:
    os.set_blocking(server.stdout.fileno(), False)
    deadline = time.monotonic() + 5
    announcement = bytearray()
    while b"\n" not in announcement:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([server.stdout], [], [], remaining)[0]:
            raise RuntimeError("server startup timed out")
        if len(announcement) >= 64:
            raise RuntimeError("oversized readiness record")
        try:
            chunk = os.read(server.stdout.fileno(), 1)
        except BlockingIOError:
            continue
        if not chunk:
            raise RuntimeError("server exited before readiness")
        announcement.extend(chunk)
    os.set_blocking(server.stdout.fileno(), True)
    fields = announcement.decode("ascii").split()
    if len(fields) != 2 or fields[0] != "PORT" or not fields[1].isdigit():
        raise RuntimeError("server failed to announce its port")
    port = fields[1]
    reply = subprocess.run(["build/echo_client", port, "hello, streams"],
                           capture_output=True, text=True, timeout=5,
                           check=True)
    print(reply.stdout, end="")
    out, err = server.communicate(timeout=5)
    if server.returncode != 0:
        raise RuntimeError(err.decode("utf-8", errors="replace"))
finally:
    if server.poll() is None:
        server.kill()
        server.wait()
PY
```

The expected output is `hello, streams` and a newline. An empty message prints one newline. A message longer than 4096 bytes fails before connecting. If an execution environment forbids local binding, report that limitation: compiling the program cannot establish that this exchange ran successfully.

Readiness for one byte is not readiness for a whole line. The driver accumulates a bounded record with nonblocking reads and recomputes the remaining startup interval; a blocking `readline()` after one readiness event would reintroduce an unbounded wait on a partial announcement. [Python readiness interface](https://docs.python.org/3/library/select.html#select.select).

A stronger correctness experiment varies how a test peer submits header and body bytes while requiring the same reply. However, splitting writes does not force matching packet or read boundaries. Combine such tests with the offset invariant; do not mistake the observed calls for a transport guarantee.

## Safety is bounded; waiting still is not

The receiver's storage bound is a safety property. It says unchecked input cannot grow this body buffer. It says nothing about how soon the operation completes. A peer can send one header byte and remain connected indefinitely. The server can also wait indefinitely in `accept` before any peer connects. The C examples deliberately have no internal deadlines.

A useful progress policy distinguishes connection establishment, complete-request arrival, queued-reply delivery, and idle connections. Resetting a five-second timer after every byte lets a sufficiently slow sender extend the session indefinitely. An absolute request deadline states when the complete request must arrive. It is a policy choice, expressed against an elapsed-time clock, rather than an inevitable consequence of framing.

The service also has no authentication, encryption, or general concurrent-client handling. Moving the address from loopback to a remote-facing interface changes the assumptions under which those omissions were acceptable for this experiment. No measurement here establishes network throughput or response-time guarantees.

## Transfer the protocol

1. Change one assumption: messages are printable text with no newlines. Compare newline termination with our length prefix. Identify which state simplifies and which limit must still be enforced.
2. Send header bytes individually, irregular body chunks, a truncated body, and lengths 4096 and 4097. State the expected behavior before running. Which tests demonstrate behavior, and which protocol properties still require reasoning?
3. Allow several requests on one connection. Define normal EOF positions, cap pending replies, and explain what happens when requests arrive faster than replies leave. Then change an echo into an increment operation and identify the new retry ambiguity.

:::reveal Review after your attempt

For exercise 1, newline becomes unambiguous under the new alphabet restriction, so a receiver can scan for the terminator instead of decoding a length. It still needs a maximum line length and an incomplete-line policy. Removing a header does not remove resource limits.

For exercise 2, delivery divisions must not affect the echo; a short body fails; 4096 succeeds and 4097 is rejected from its header. These cases do not exhaust every timing or failure sequence. The receiver's progress and bounds invariants explain the general case.

For exercise 3, EOF between frames can be normal while EOF inside a field is truncation. An output queue needs an aggregate bound and a policy for pausing input. If an increment occurs but its reply disappears, retrying may increment twice. The original echo contract did not need to resolve that ambiguity.

:::

## Write your version

Write a protocol specification that another person could implement without seeing the C. For each rule, label whether it is imposed by the application, inherited from TCP, or selected by you. Include a counterexample against “one read equals one request,” a memory bound, and a progress policy. The specification is complete only when it tells a peer how success and incomplete work differ.

Now replace the echo with a deterministic computation over a named input. The connection fails after the server finishes but before the client receives its reply. Give the logical job an identity that survives reconnection; keep each execution attempt separately identifiable. Specify what happens if an ID is reused with different input, how long a completed result is retained, and what a restart loses. A request ID alone supplies none of those policies. [Appendix F](#appendix-f--from-c-systems-to-ai-infrastructure) derives the distinction between accepting one logical result and executing work only once.
