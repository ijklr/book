# Chapter 10 — Deriving a protocol from incomplete information

We want two local processes to exchange a request and check that its reply contains exactly the same bytes. A request may contain up to 4096 arbitrary bytes, including zero bytes. If the stream ends partway through a request, the receiver must report failure instead of accepting the prefix. Neither side may allocate memory proportional to an unchecked value supplied by the other.

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

We choose a length prefix because it identifies the end of an arbitrary byte sequence and lets the receiver reject an oversized message before reading its body. Four bytes and network byte order are conventions we choose for this protocol; the requirements do not uniquely determine them.

| Field | Encoding | Rule |
| --- | --- | --- |
| Length | Four-byte unsigned integer, network byte order | Between zero and 4096 inclusive |
| Body | Exactly the declared number of bytes | No terminator or escaping |

The length excludes the header. An empty body is therefore represented by a complete four-byte header containing zero. It is different from a connection that sends no bytes at all. The reply uses the same framing rule as the request.

For example, the two body bytes `41 00` travel as this six-byte frame, written here in hexadecimal:

```text
header          body
00 00 00 02     41 00
```

The final zero is data, not a terminator. The receiver completes this frame after six bytes regardless of how many reads delivered them. It does not wait for the sender to close the connection.

Challenge the chosen width before treating it as necessary. How many distinct lengths must a fixed-width length field represent? Could one byte work? Could two? Justify your answer from the allowed values, without appealing to a familiar protocol.

:::reveal Derive the information needed

Lengths zero through 4096 require 4097 distinct values. Twelve bits represent only 4096 values, so this field needs at least thirteen bits; two whole bytes suffice. Our four-byte field is a convenient convention, not a lower bound. Reducing it would save two bytes per frame while retaining length validation and partial-header state. Whether that saving matters depends on the workload. If every body were required to contain exactly 4096 bytes, a revised protocol could omit the length field entirely because the agreement already supplies that information. This would remove variable-length messages from the contract, so it requires a real change in need. Derive the information first, then choose its representation.

:::

Network byte order puts the most significant byte first. We use `htonl` to encode and `ntohl` to decode the fixed-width header. Sending an ordinary host integer's bytes without a specified encoding would depend on representation; sending an entire C structure would add layout and padding dependencies. The required property is agreement about the bytes, while these conversion functions implement our selected convention. [Byte-order conversions](https://man7.org/linux/man-pages/man3/byteorder.3.html)

## Derive the receiver's memory and states

Before the first byte, the receiver needs four bytes of header storage and a count of how much is present. It cannot interpret an incomplete header. After collecting four bytes, it needs a validated body length and a count of body bytes received. Only after that count reaches the declared length is the request complete.

The invariants are small enough to state directly: header bytes collected stay between zero and four; a validated length stays at most 4096; body bytes collected stay between zero and that length. Buffer addresses always refer to the unfilled suffix, and each positive transfer advances progress exactly once. The maximum is checked before reading the body, so even an enormous declared length cannot expand our storage requirement.

Follow a complete frame through those states. Suppose its bytes are `00 00 00 05 41 00 42 ff 43`, written in hexadecimal. The first four declare a five-byte body. The following is one possible sequence of read results, not a prediction of packet boundaries:

| Call | Field and bytes requested | Bytes returned | State after the call |
| --- | --- | --- | --- |
| 1 | Header, 4 | `00` | Header offset 1; length still unknown |
| 2 | Header, 3 | `00 00` | Header offset 3; length still unknown |
| 3 | Header, 1 | `05` | Header complete; decode and validate length 5 |
| 4 | Body, 5 | `41 00` | Body offset 2; three bytes remain |
| 5 | Body, 3 | `42` | Body offset 3; two bytes remain |
| 6 | Body, 2 | `ff 43` | Body complete; reply may begin |

Call 2 writes at `header_bytes + 1`, preserving the byte already received. Call 5 writes at `body + 2`, preserving both earlier body bytes, including the zero. The receiving loop never asks for more than the current field's unfilled suffix. Even if the entire frame is already waiting in the socket, the first header read requests only four bytes; body bytes remain for the body read. A design that reads into a larger shared buffer instead needs an additional count of buffered bytes not yet parsed.

Before continuing, predict two variations. First, EOF follows call 4. Can the body be printed as a shorter valid request? Second, the completed header is `00 00 10 01`. What length does it declare, and which body read should happen?

:::reveal Review the frame trace

EOF after call 4 violates the promise of five body bytes: receiving two does not revise the promised length. Treating the body as a C string would also be wrong, because the zero byte would hide valid data. The second header declares hexadecimal `1001`, or decimal 4097. Reject it before any body read. Validation changes which state is reachable: the oversized length must never become the bound of a body-transfer loop.

:::

For blocking I/O, these states can live in a function's local variables and loop position. Chapter 11 will move them into longer-lived objects when a handler must return before completion. The logical state exists in either design, whether or not a `struct` names it.

Now classify EOF. Commit answers for four cases before opening the review: no header bytes; two header bytes; a complete header announcing five body bytes followed by only two; and a complete header announcing an empty body.

:::reveal Review the EOF cases

| State when the stream ends | Meaning and server action |
| --- | --- |
| No header bytes received | No request started; close successfully without replying |
| Two of four header bytes received | Truncated header; report failure |
| Complete header declares five body bytes; only two arrive | Truncated body; report failure |
| Complete header declares zero body bytes | Request already complete; reply with a zero-length frame |

The empty frame requires no body read. For the other cases, EOF means a read requesting a positive number of bytes returned zero. The protocol state tells us whether that end is acceptable.

:::

## Derive the sender's obligation

The sender faces the same progress problem in reverse. A successful write can accept only a prefix of the requested bytes. Its invariant is that bytes before `offset` have been accepted by the local write operation and bytes at or after `offset` remain to be submitted. A `-1` result with `errno == EINTR` leaves the offset unchanged; a positive result advances it by that count. Other errors fail the operation. Restarting from byte zero after a short write duplicates the prefix and corrupts the frame.

Both programs ignore `SIGPIPE`, allowing broken-connection writes to follow an error-return path instead of terminating through that signal. Successful writing does not prove that the peer application processed the request. The client therefore reads a reply and verifies both its length and its bytes. [Send interface and SIGPIPE](https://man7.org/linux/man-pages/man2/send.2.html)

This distinction matters when extending an echo protocol to operations with side effects. If the server performs an operation and the reply is lost, the client cannot infer from its read failure whether the operation happened. Retrying an echo is harmless; retrying a debit may require request identifiers and deduplication. A reliable stream does not, by itself, establish exactly-once application behavior.

## Choose the endpoint and its owners

Loopback confines the demonstration to the local machine. Binding port zero asks the kernel to choose an available port; `getsockname` retrieves it. Printing and flushing `PORT N` publishes the selected address to a supervising process. Merely calling `printf` would leave publication dependent on stdio buffering.

The listening descriptor refers to the socket that accepts connections. `accept` produces a separate descriptor for the connected stream, so the server now owns two descriptors to clean up. The program handles one connection by stopping after one successful `accept`. The backlog argument in `listen(..., 1)` concerns queued connection requests; it does not enforce this application policy.

Before opening the implementations, sketch the server's resource ownership after socket creation, binding, accepting, and receiving a bad length. Your cleanup path must work from every reached state, including failure before a connection descriptor exists.

:::reveal Reference implementation: server

{{include:examples/echo_server.c}}

:::

:::reveal Reference implementation: client

{{include:examples/echo_client.c}}

:::

Check the two `read_full` contracts rather than assuming identical helper names imply identical semantics. On the server, the helper's result 0 means EOF before any bytes of the current call, not necessarily before the request. The caller accepts that result while starting the header but rejects it while waiting for a nonempty body. The client is expecting a reply, so missing required reply bytes are always failure. Both helpers treat a request for zero bytes as already complete, without calling `read`.

The server consumes one frame and then closes. It does not wait for EOF to validate that no extra bytes followed the frame. A protocol that requires exactly one frame and rejects all trailing bytes would need another completion rule, such as the client's sending-direction shutdown. Our one-request policy means that a second frame receives no service.

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

A progress policy can set separate limits for connection establishment, complete-request arrival, reply delivery, and idle connections. With a five-second idle timeout that resets after each byte, a peer can send one byte every four seconds; a 4096-byte body then takes more than four hours without violating the idle limit. An absolute request deadline instead fixes when the complete request must arrive. Define its starting event and measure the remaining interval with a monotonic clock. Framing determines when a message is complete; the deadline determines how long we will wait for it.

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
